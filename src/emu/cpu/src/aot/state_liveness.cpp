#include <cpu/aot/state_liveness.h>
#include <cpu/aot/wasm_cost.h>
#include <array>
#include <cstring>
#include <limits>

namespace eka2l1::arm::aot {
    namespace {
        enum class immediate { none, leb, pair, table, float32, float64, unknown };
        constexpr auto encodings() {
            std::array<immediate, 256> table{};
            for (auto &entry : table) entry = immediate::unknown;
#define WASM_COST(first, last, category, format) \
            for (unsigned opcode = first; opcode <= last; ++opcode) table[opcode] = immediate::format;
#include <cpu/aot/wasm_cost_model.def>
#undef WASM_COST
            return table;
        }
        constexpr auto encoding = encodings();
        constexpr auto absent = std::numeric_limits<std::size_t>::max();

        struct reader {
            const std::vector<std::uint8_t> &bytes;
            std::size_t position = 0;
            bool valid = true;
            std::uint32_t leb() {
                std::uint32_t value = 0;
                for (unsigned n = 0; n < 5 && position < bytes.size(); ++n) {
                    const auto byte = bytes[position++];
                    if (n == 4 && byte > 15) break;
                    value |= std::uint32_t(byte & 127) << (7 * n);
                    if (!(byte & 128)) return value;
                }
                valid = false;
                return 0;
            }
            void skip_leb() {
                for (unsigned n = 0; n < 10 && position < bytes.size(); ++n)
                    if (!(bytes[position++] & 128)) return;
                valid = false;
            }
        };
        template <typename mask> struct node {
            mask reads = 0, writes = 0, clears = 0, reloads = 0;
            mask invalidates = 0;
            mask dirty = 0, live = 0;
            std::size_t next = absent, previous = absent;
            bool reachable = false, queued = false, fallthrough = true;
            bool locals = false;
        };
        struct scope {
            std::uint8_t opcode;
            std::size_t head, alternative;
            std::size_t exits = absent;
        };
        struct patch { std::size_t branch, next; };
        struct edge { std::size_t from, to, next = absent, previous = absent; };
        template <typename mask> struct workspace {
            std::vector<node<mask>> nodes;
            std::vector<scope> scopes;
            std::vector<patch> patches;
            std::vector<edge> edges;
            std::vector<std::size_t> transfer_nodes, work;
            std::vector<std::uint32_t> labels;
            std::vector<std::uint8_t> remove;
            void clear() {
                nodes.clear(); scopes.clear(); patches.clear(); edges.clear();
                transfer_nodes.clear(); work.clear(); labels.clear(); remove.clear();
            }
        };
    }

    template <typename mask> static bool prune(wasm_func_def &function,
        const std::vector<state_transfer> &transfers,
        std::uint32_t first_local, std::uint32_t local_count) {
        if (transfers.empty() || !local_count || local_count > 64) return false;
        const auto bit = [&](std::uint32_t slot) -> mask {
            return slot >= first_local && slot - first_local < local_count
                ? mask(1) << (slot - first_local) : 0;
        };
        std::size_t end = 0;
        for (const auto &transfer : transfers) {
            if (transfer.begin < end || transfer.end <= transfer.begin
                    || transfer.end > function.body.size() || !bit(transfer.slot)) return false;
            end = transfer.end;
        }

        // Non-state arithmetic contributes no dataflow facts. Keep only local
        // uses/definitions, transfers and structured-control events in the CFG.
        // No callback or recursive compilation occurs inside this pass. Each
        // compiler thread can reuse its scratch arrays across regions.
        thread_local workspace<mask> scratch;
        scratch.clear();
        auto &nodes = scratch.nodes;
        auto &scopes = scratch.scopes;
        auto &patches = scratch.patches;
        auto &edges = scratch.edges;
        auto &transfer_nodes = scratch.transfer_nodes;
        auto &labels = scratch.labels;
        auto &work = scratch.work;
        auto &remove = scratch.remove;
        nodes.reserve(function.body.size() / 16 + 16);
        transfer_nodes.reserve(transfers.size());
        reader input{function.body};
        while (input.valid && input.position < function.body.size()) {
            if (transfer_nodes.size() < transfers.size()
                    && input.position == transfers[transfer_nodes.size()].begin) {
                const auto &transfer = transfers[transfer_nodes.size()];
                const auto field = bit(transfer.slot);
                // Transfers in one barrier operate on distinct fields. Their
                // dataflow can share a node; deletion still uses each field bit.
                const auto count = transfer_nodes.size();
                if (!count || transfers[count - 1].end != transfer.begin
                        || transfers[count - 1].reload != transfer.reload
                        || (nodes.back().clears & field)) nodes.emplace_back();
                transfer_nodes.push_back(nodes.size() - 1);
                nodes.back().clears |= field;
                if (transfer.reload) nodes.back().reloads |= field;
                input.position = transfer.end;
                continue;
            }
            const auto opcode = function.body[input.position++];
            std::uint32_t operand = 0;
            labels.clear();
            switch (encoding[opcode]) {
            case immediate::none: break;
            case immediate::leb:
                if ((opcode >= op_local_get && opcode <= op_local_tee)
                        || opcode == op_br || opcode == op_br_if) operand = input.leb();
                else input.skip_leb();
                break;
            case immediate::pair: input.skip_leb(); input.skip_leb(); break;
            case immediate::table:
                operand = input.leb();
                if (operand >= function.body.size() - input.position) return false;
                for (std::uint32_t n = 0; n <= operand; ++n) labels.push_back(input.leb());
                break;
            case immediate::float32: input.position += 4; break;
            case immediate::float64: input.position += 8; break;
            case immediate::unknown: return false;
            }
            if (!input.valid || input.position > function.body.size()) return false;
            if (opcode >= op_local_get && opcode <= op_local_tee) {
                if (const auto field = bit(operand)) {
                    // Summarize uses-before-definitions over straight-line
                    // code. Control, call and transfer nodes delimit the group.
                    if (nodes.empty() || !nodes.back().locals) {
                        nodes.emplace_back(); nodes.back().locals = true;
                    }
                    if (opcode == op_local_get) nodes.back().reads |= field & ~nodes.back().writes;
                    else nodes.back().writes |= field;
                }
                continue;
            }
            if (opcode == op_call || opcode == 0x11) {
                // A call may change state memory without defining WASM locals.
                // Normal helper reloads restore equality. Bare/outlined calls
                // must not make a later stale-local store look redundant.
                nodes.emplace_back();
                nodes.back().invalidates = local_count == sizeof(mask) * 8 ? ~mask(0)
                    : (mask(1) << local_count) - 1;
                continue;
            }
            if (opcode > op_return || opcode == 1) continue;
            const auto index = nodes.size();
            nodes.emplace_back();
            if (opcode == op_block || opcode == op_loop || opcode == op_if) {
                scopes.push_back({opcode, index, absent});
            } else if (opcode == op_else) {
                if (scopes.empty() || scopes.back().opcode != op_if
                        || scopes.back().alternative != absent) return false;
                scopes.back().alternative = index;
                nodes[index].fallthrough = false;
                edges.push_back({scopes.back().head, index + 1});
            } else if (opcode == op_end) {
                if (scopes.empty()) return false;
                const auto &frame = scopes.back();
                for (auto p = frame.exits; p != absent; p = patches[p].next)
                    edges.push_back({patches[p].branch, index + 1});
                if (frame.alternative != absent) edges.push_back({frame.alternative, index + 1});
                else if (frame.opcode == op_if) edges.push_back({frame.head, index + 1});
                scopes.pop_back();
            } else if (opcode == op_br || opcode == op_br_if || opcode == op_br_table) {
                if (opcode != op_br_if) nodes[index].fallthrough = false;
                const auto branch = [&](auto depth) {
                    if (depth > scopes.size()) return false;
                    if (depth == scopes.size()) return true; // implicit function label
                    auto &frame = scopes[scopes.size() - 1 - depth];
                    if (frame.opcode == op_loop) edges.push_back({index, frame.head + 1});
                    else { patches.push_back({index, frame.exits}); frame.exits = patches.size() - 1; }
                    return true;
                };
                if (opcode == op_br_table) { for (auto depth : labels) if (!branch(depth)) return false; }
                else if (!branch(operand)) return false;
            } else if (opcode == op_return || opcode == op_unreachable) nodes[index].fallthrough = false;
            else return false;
        }
        if (!scopes.empty() || transfer_nodes.size() != transfers.size()) return false;
        // Fallthrough edges are implicit. Branch edges and predecessor links
        // share one flat allocation, not two heap vectors per instruction.
        for (std::size_t i = 0; i < edges.size(); ++i) {
            auto &link = edges[i];
            if (link.to > nodes.size()) return false;
            if (link.to == nodes.size()) continue;
            link.next = nodes[link.from].next; nodes[link.from].next = i;
            link.previous = nodes[link.to].previous; nodes[link.to].previous = i;
        }
        const auto successors = [&](std::size_t index, auto visit) {
            if (nodes[index].fallthrough && index + 1 < nodes.size()) visit(index + 1);
            for (auto e = nodes[index].next; e != absent; e = edges[e].next) visit(edges[e].to);
        };

        // A field may be dirty if any predecessor has changed its local since
        // the last publication/reload. Joins use union; loop backedges iterate
        // to a fixed point. Nothing is tracked while the guest executes.
        work.push_back(0);
        nodes[0].reachable = nodes[0].queued = true;
        while (!work.empty()) {
            const auto index = work.back(); work.pop_back();
            auto &current = nodes[index]; current.queued = false;
            const auto out = (current.dirty & ~current.clears) | current.writes | current.invalidates;
            successors(index, [&](auto successor) {
                auto &next = nodes[successor];
                if (!next.reachable || (out & ~next.dirty)) {
                    next.reachable = true; next.dirty |= out;
                    if (!next.queued) { next.queued = true; work.push_back(successor); }
                }
            });
        }
        remove.resize(transfers.size(), 0);
        for (std::size_t i = 0; i < transfers.size(); ++i) {
            auto &current = nodes[transfer_nodes[i]];
            if (!transfers[i].reload) {
                const auto field = bit(transfers[i].slot);
                remove[i] = !(current.dirty & field);
                if (!remove[i]) current.reads |= field;
            }
        }

        // Removed stores must not keep a reload alive. A retained publication
        // reads the local; each reload defines it. This also preserves values
        // used only on a callback, conditional exit or subsequent loop iteration.
        // A stack visits straight-line code backward once before propagating
        // loop facts, instead of repeatedly revisiting earlier instructions.
        for (std::size_t i = 0; i < nodes.size(); ++i) if (nodes[i].reachable) {
            nodes[i].queued = true; work.push_back(i);
        }
        while (!work.empty()) {
            const auto index = work.back(); work.pop_back();
            auto &current = nodes[index]; current.queued = false;
            mask out = 0;
            successors(index, [&](auto successor) { out |= nodes[successor].live; });
            const auto live = current.reads | (out & ~(current.writes | current.reloads));
            if (live != current.live) {
                current.live = live;
                const auto revisit = [&](auto previous) {
                    if (nodes[previous].reachable && !nodes[previous].queued) {
                        nodes[previous].queued = true; work.push_back(previous);
                    }
                };
                if (index && nodes[index - 1].fallthrough) revisit(index - 1);
                for (auto e = current.previous; e != absent; e = edges[e].previous) revisit(edges[e].from);
            }
        }
        for (std::size_t i = 0; i < transfers.size(); ++i) if (transfers[i].reload) {
            const auto &current = nodes[transfer_nodes[i]];
            mask out = 0;
            successors(transfer_nodes[i], [&](auto successor) { out |= nodes[successor].live; });
            remove[i] = !(out & bit(transfers[i].slot));
        }

        // Every edit removes a complete neutral transfer, never adding work or
        // changing control flow. The shared ledger checks each removed fragment;
        // paths missing these fragments remain identical. Require reachable work.
        // Each removed transfer has one of two fixed costs. Prove each kind
        // once, not once per field per barrier. No path acquires new work.
        constexpr bool transfer_costs_improve = [] {
            wasm_cost::path_gate savings;
            savings.compare(wasm_cost::sequence({op_local_get, op_i32_load, op_local_set}), {}, true);
            savings.compare(wasm_cost::sequence({op_local_get, op_local_get, op_i32_store}), {}, true);
            return savings.material_improves();
        }();
        static_assert(transfer_costs_improve);
        bool reachable_saving = false;
        for (std::size_t i = 0; i < transfers.size(); ++i) if (remove[i]) {
            reachable_saving |= nodes[transfer_nodes[i]].reachable;
        }
        if (!reachable_saving) return false;
        const auto relocate = [&](std::uint32_t offset) {
            std::size_t removed = 0;
            for (std::size_t i = 0; i < transfers.size(); ++i)
                if (remove[i] && transfers[i].end <= offset) removed += transfers[i].end - transfers[i].begin;
            return offset - static_cast<std::uint32_t>(removed);
        };
        if (function.outlined_callee) function.outlined_call_offset = relocate(function.outlined_call_offset);
        for (auto &call : function.outlined_calls) call.call_offset = relocate(call.call_offset);
        // All decisions and call relocations are complete. Compact the original
        // buffer in place instead of allocating and copying a second body.
        std::size_t previous = 0, written = 0;
        const auto append = [&](std::size_t end) {
            const auto size = end - previous;
            if (size) std::memmove(function.body.data() + written, function.body.data() + previous, size);
            written += size;
        };
        for (std::size_t i = 0; i < transfers.size(); ++i) if (remove[i]) {
            append(transfers[i].begin);
            previous = transfers[i].end;
        }
        append(function.body.size());
        function.body.resize(written);
        return true;
    }

    bool prune_state_transfers(wasm_func_def &function,
        const std::vector<state_transfer> &transfers,
        std::uint32_t first_local, std::uint32_t local_count) {
        if (local_count <= 32) return prune<std::uint32_t>(function, transfers, first_local, local_count);
        return prune<std::uint64_t>(function, transfers, first_local, local_count);
    }
}
