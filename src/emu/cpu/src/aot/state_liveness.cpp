#include <cpu/aot/state_liveness.h>
#include <cpu/aot/wasm_cost.h>
#include <array>
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
            std::uint64_t leb() {
                std::uint64_t value = 0;
                for (unsigned n = 0; n < 10 && position < bytes.size(); ++n) {
                    const auto byte = bytes[position++];
                    value |= std::uint64_t(byte & 127) << (7 * n);
                    if (!(byte & 128)) return value;
                }
                valid = false;
                return 0;
            }
        };
        struct node {
            std::uint64_t reads = 0, writes = 0, clears = 0, reloads = 0;
            std::uint64_t invalidates = 0;
            std::uint64_t dirty = 0, live = 0;
            std::size_t next = absent, previous = absent;
            bool reachable = false, queued = false, fallthrough = true;
        };
        struct scope {
            std::uint8_t opcode;
            std::size_t head, alternative;
            std::size_t exits = absent;
        };
        struct patch { std::size_t branch, next; };
        struct edge { std::size_t from, to, next = absent, previous = absent; };
    }

    bool prune_state_transfers(wasm_func_def &function,
        const std::vector<state_transfer> &transfers,
        std::uint32_t first_local, std::uint32_t local_count) {
        if (transfers.empty() || !local_count || local_count > 64) return false;
        const auto bit = [&](std::uint64_t slot) -> std::uint64_t {
            return slot >= first_local && slot - first_local < local_count
                ? std::uint64_t(1) << (slot - first_local) : 0;
        };
        std::size_t end = 0;
        for (const auto &transfer : transfers) {
            if (transfer.begin < end || transfer.end <= transfer.begin
                    || transfer.end > function.body.size() || !bit(transfer.slot)) return false;
            end = transfer.end;
        }

        // Non-state arithmetic contributes no dataflow facts. Keep only local
        // uses/definitions, transfers and structured-control events in the CFG.
        std::vector<node> nodes;
        std::vector<scope> scopes;
        std::vector<patch> patches;
        std::vector<edge> edges;
        std::vector<std::size_t> transfer_nodes;
        nodes.reserve(transfers.size() + function.body.size() / 4);
        transfer_nodes.reserve(transfers.size());
        reader input{function.body};
        while (input.valid && input.position < function.body.size()) {
            if (transfer_nodes.size() < transfers.size()
                    && input.position == transfers[transfer_nodes.size()].begin) {
                const auto &transfer = transfers[transfer_nodes.size()];
                transfer_nodes.push_back(nodes.size());
                nodes.emplace_back();
                nodes.back().clears = bit(transfer.slot);
                nodes.back().reloads = transfer.reload ? bit(transfer.slot) : 0;
                input.position = transfer.end;
                continue;
            }
            const auto opcode = function.body[input.position++];
            std::uint64_t operand = 0;
            std::vector<std::uint64_t> labels;
            switch (encoding[opcode]) {
            case immediate::none: break;
            case immediate::leb: operand = input.leb(); break;
            case immediate::pair: input.leb(); input.leb(); break;
            case immediate::table:
                operand = input.leb();
                if (operand >= function.body.size() - input.position) return false;
                for (std::uint64_t n = 0; n <= operand; ++n) labels.push_back(input.leb());
                break;
            case immediate::float32: input.position += 4; break;
            case immediate::float64: input.position += 8; break;
            case immediate::unknown: return false;
            }
            if (!input.valid || input.position > function.body.size()) return false;
            if (opcode >= op_local_get && opcode <= op_local_tee) {
                if (const auto field = bit(operand)) {
                    nodes.emplace_back();
                    if (opcode == op_local_get) nodes.back().reads = field;
                    else nodes.back().writes = field;
                }
                continue;
            }
            if (opcode == op_call || opcode == 0x11) {
                // A call may change state memory without defining WASM locals.
                // Normal helper reloads restore equality. Bare/outlined calls
                // must not make a later stale-local store look redundant.
                nodes.emplace_back();
                nodes.back().invalidates = local_count == 64 ? ~std::uint64_t(0)
                    : (std::uint64_t(1) << local_count) - 1;
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
        std::vector<std::size_t> work{0};
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
        std::vector<bool> remove(transfers.size());
        for (std::size_t i = 0; i < transfers.size(); ++i) {
            auto &current = nodes[transfer_nodes[i]];
            if (!transfers[i].reload) {
                remove[i] = !(current.dirty & current.clears);
                if (!remove[i]) current.reads = current.clears;
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
            std::uint64_t out = 0;
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
            std::uint64_t out = 0;
            successors(transfer_nodes[i], [&](auto successor) { out |= nodes[successor].live; });
            remove[i] = !(out & current.clears);
        }

        // Every edit removes a complete neutral transfer, never adding work or
        // changing control flow. The shared ledger checks each removed fragment;
        // paths missing these fragments remain identical. Require reachable work.
        wasm_cost::path_gate savings;
        bool reachable_saving = false;
        for (std::size_t i = 0; i < transfers.size(); ++i) if (remove[i]) {
            savings.compare(transfers[i].reload
                ? wasm_cost::sequence({op_local_get, op_i32_load, op_local_set})
                : wasm_cost::sequence({op_local_get, op_local_get, op_i32_store}), {}, true);
            reachable_saving |= nodes[transfer_nodes[i]].reachable;
        }
        if (!reachable_saving || !savings.material_improves()) return false;
        const auto relocate = [&](std::uint32_t offset) {
            std::size_t removed = 0;
            for (std::size_t i = 0; i < transfers.size(); ++i)
                if (remove[i] && transfers[i].end <= offset) removed += transfers[i].end - transfers[i].begin;
            return offset - static_cast<std::uint32_t>(removed);
        };
        if (function.outlined_callee) function.outlined_call_offset = relocate(function.outlined_call_offset);
        for (auto &call : function.outlined_calls) call.call_offset = relocate(call.call_offset);
        std::vector<std::uint8_t> body;
        body.reserve(function.body.size());
        std::size_t previous = 0;
        for (std::size_t i = 0; i < transfers.size(); ++i) if (remove[i]) {
            body.insert(body.end(), function.body.begin() + previous, function.body.begin() + transfers[i].begin);
            previous = transfers[i].end;
        }
        body.insert(body.end(), function.body.begin() + previous, function.body.end());
        function.body = std::move(body);
        return true;
    }
}
