#pragma once

#include <cpu/aot/thumb_translator.h>
#include <array>
#include <bit>
#include <map>
#include <tuple>

namespace eka2l1::arm::aot {
    // An ordered, typed value graph. Memory effects are never numbered or
    // reordered. Snapshots are architectural consumers, not incidental stores.
    // The first lowering uses an entry budget/proof guard and the original
    // compiler for every short budget or failed proof; only the final snapshot
    // is therefore live on its successful path. Intermediate snapshots retain
    // the precise contract used by segments with internal memory exits.
    struct region_ir {
        using value = unsigned;
        enum operation : unsigned { constant = 256, state, host, pack, low, high,
            read32, read8u, read8s, read16u, read16s,
            write32, write8, write16, guarded_host, choose };
        static bool is_read(unsigned op) { return op >= read32 && op <= read16s; }
        static bool is_write(unsigned op) { return op >= write32 && op <= write16; }
        static bool is_effect(unsigned op) { return is_read(op) || is_write(op) || op == guarded_host; }
        struct node {
            unsigned op, type;
            value a = 0, b = 0;
            std::uint64_t immediate = 0;
        };
        struct snapshot {
            std::array<value, 16> regs;
            std::array<value, 5> flags; // N Z C V T
            unsigned count;
        };
        static constexpr std::array<unsigned, 5> flag_offsets = {
            state_offsets::NFLAG, state_offsets::ZFLAG, state_offsets::CFLAG,
            state_offsets::VFLAG, state_offsets::TFLAG};
        std::vector<node> nodes{{constant, type_i32}}; // zero is invalid
        std::vector<snapshot> snapshots;
        std::vector<value> effects;
        std::map<std::tuple<unsigned,unsigned,value,value,std::uint64_t>, value> numbers;
        bool optimize = true;
        unsigned flag_instructions = 0, inline_transfers = 0, conditional_instructions = 0;

        explicit region_ir(std::uint32_t pc, bool optimize_ = true) : optimize(optimize_) {
            snapshot entry{};
            for (unsigned r = 0; r < 15; ++r) entry.regs[r] = make(state, type_i32, 0, 0, state_offsets::reg(r));
            entry.regs[15] = imm(pc);
            for (unsigned f = 0; f < 5; ++f) entry.flags[f] = make(state, type_i32, 0, 0, flag_offsets[f]);
            snapshots.push_back(entry);
        }
        value make(unsigned op, unsigned type, value a = 0, value b = 0, std::uint64_t immediate = 0) {
            const auto key = std::make_tuple(op, type, a, b, immediate);
            const bool pure = !is_effect(op);
            if (optimize && pure) {
                auto it = numbers.find(key);
                if (it != numbers.end()) return it->second;
            }
            auto id = static_cast<value>(nodes.size());
            nodes.push_back({op, type, a, b, immediate});
            if (pure) numbers.emplace(key, id); else effects.push_back(id);
            return id;
        }
        value imm(std::uint32_t n) { return make(constant, type_i32, 0, 0, n); }
        bool is_constant(value v, std::uint64_t n) const {
            return nodes[v].op == constant && nodes[v].immediate == n;
        }
        value unary(unsigned op, value a) {
            const unsigned type = (op == op_i64_extend_i32_s || op == op_i64_extend_i32_u) ? type_i64 : type_i32;
            if (optimize && nodes[a].op == constant) {
                auto n = nodes[a].immediate;
                if (op == op_i64_extend_i32_s) n = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(n)));
                else if (op == op_i64_extend_i32_u || op == low) n = static_cast<std::uint32_t>(n);
                else if (op == high) n >>= 32;
                else return make(op, type, a);
                return make(constant, type, 0, 0, n);
            }
            if (optimize && nodes[a].op == pack) {
                if (op == low) return nodes[a].a;
                if (op == high) return nodes[a].b;
            }
            return make(op, type, a);
        }
        value binary(unsigned op, value a, value b) {
            const unsigned type = (op == pack || op == op_i64_add || op == op_i64_mul) ? type_i64 : type_i32;
            if (optimize) {
                if (op == pack && nodes[a].op == low && nodes[b].op == high && nodes[a].a == nodes[b].a)
                    return nodes[a].a;
                if ((op == op_i32_add || op == op_i32_or || op == op_i32_xor || op == op_i64_add) && is_constant(a, 0)) return b;
                if ((op == op_i32_add || op == op_i32_sub || op == op_i32_or || op == op_i32_xor || op == op_i64_add) && is_constant(b, 0)) return a;
                if ((op == op_i32_and || op == op_i32_or) && a == b) return a;
                if ((op == op_i32_xor || op == op_i32_sub) && a == b) return imm(0);
                if (nodes[a].op == constant && nodes[b].op == constant) {
                    const auto x = nodes[a].immediate, y = nodes[b].immediate;
                    std::uint64_t n = 0; bool folded = true;
                    switch (op) {
                    case pack: n = std::uint32_t(x) | (std::uint64_t(std::uint32_t(y)) << 32); break;
                    case op_i32_add: case op_i64_add: n = x + y; break;
                    case op_i32_sub: n = x - y; break;
                    case op_i32_mul: case op_i64_mul: n = x * y; break;
                    case op_i32_and: n = x & y; break;
                    case op_i32_or: n = x | y; break;
                    case op_i32_xor: n = x ^ y; break;
                    case op_i32_shl: n = std::uint32_t(x) << (y & 31); break;
                    case op_i32_shr_u: n = std::uint32_t(x) >> (y & 31); break;
                    case op_i32_shr_s: n = std::int32_t(x) >> (y & 31); break;
                    case op_i32_eq: n = std::uint32_t(x) == std::uint32_t(y); break;
                    case op_i32_lt_u: n = std::uint32_t(x) < std::uint32_t(y); break;
                    case op_i32_gt_u: n = std::uint32_t(x) > std::uint32_t(y); break;
                    case op_i32_ge_u: n = std::uint32_t(x) >= std::uint32_t(y); break;
                    case op_i32_rotr: { auto s = y & 31; n = s ? (std::uint32_t(x) >> s) | (std::uint32_t(x) << (32 - s)) : std::uint32_t(x); break; }
                    default: folded = false;
                    }
                    if (folded) return make(constant, type, 0, 0, type == type_i32 ? std::uint32_t(n) : n);
                }
            }
            return make(op, type, a, b);
        }

        // Decode ordinary AL integer operations with entry-proved memory spans
        // or explicit dynamic guards. A missing proof or unsupported encoding
        // rejects the entire graph before any generated guest effect.
        template <typename AccessMap>
        bool append(std::uint32_t op, std::uint32_t pc, bool last, const AccessMap &accesses,
            bool dynamic_memory = false, bool allow_flags = false) {
            if ((op >> 28) != 14) return false;
            auto next = snapshots.back();
            auto reg = [&](unsigned r) { return r == 15 ? imm(pc + 8) : next.regs[r]; };
            auto alu = [&](unsigned code, value a, value b) { return binary(code, a, b); };
            unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15, rs = (op >> 8) & 15;
            bool writes_pc = false;
            value dynamic_host = 0;
            auto shifted = [&](value operand, unsigned shift, unsigned amount) {
                if (shift == 0) return amount ? alu(op_i32_shl, operand, imm(amount)) : operand;
                if (shift == 1) return amount ? alu(op_i32_shr_u, operand, imm(amount)) : imm(0);
                if (shift == 2) return alu(op_i32_shr_s, operand, imm(amount ? amount : 31));
                return amount ? alu(op_i32_rotr, operand, imm(amount))
                    : alu(op_i32_or, alu(op_i32_shr_u, operand, imm(1)), alu(op_i32_shl, next.flags[2], imm(31)));
            };
            auto memory = [&](bool load, unsigned r, unsigned offset, unsigned bytes = 4, bool sign = false) {
                const auto it = accesses.find(pc);
                if (!dynamic_host && it == accesses.end()) return false;
                auto address = dynamic_host ? dynamic_host : make(host, type_i32, 0, 0, it->second.host);
                const unsigned displacement = (dynamic_host ? 0 : it->second.offset) + offset;
                if (load) next.regs[r] = make(bytes == 4 ? read32 : bytes == 2 ? (sign ? read16s : read16u)
                    : (sign ? read8s : read8u), type_i32, address, 0, displacement);
                else make(bytes == 4 ? write32 : bytes == 2 ? write16 : write8, type_void, address, reg(r), displacement);
                return true;
            };
            if ((op & 0x0ffffff0u) == 0x012fff10u) {
                if (!last || rm == 15) return false;
                next.regs[15] = reg(rm); next.flags[4] = alu(op_i32_and, next.regs[15], imm(1)); writes_pc = true;
            } else if (((op >> 26) & 3) == 1) {
                const bool pre = op & (1u << 24), up = op & (1u << 23), load = op & (1u << 20);
                const bool wb = !pre || (op & (1u << 21));
                const bool register_offset = op & (1u << 25);
                const unsigned bytes = op & (1u << 22) ? 1 : 4;
                if (rd == 15 || (!pre && (op & (1u << 21))) || (wb && rn == rd)
                    || (rn == 15 && (!load || !pre || wb || register_offset))
                    || (register_offset && ((op & 16) || rm == 15))
                    || (!dynamic_memory && (register_offset || bytes != 4 || rn == 15))) return false;
                const auto base = reg(rn);
                const auto offset = register_offset ? shifted(reg(rm), (op >> 5) & 3, (op >> 7) & 31) : imm(op & 4095);
                if (dynamic_memory && !accesses.count(pc)) {
                    auto address = pre ? alu(up ? op_i32_add : op_i32_sub, base, offset) : base;
                    dynamic_host = make(guarded_host, type_i32, address, imm(bytes),
                        (snapshots.size() - 1) * 2 + !load);
                }
                if (!memory(load, rd, 0, bytes)) return false;
                if (wb) next.regs[rn] = alu(up ? op_i32_add : op_i32_sub, base, offset);
            } else if (((op >> 25) & 7) == 4) {
                const bool load = op & (1u << 20), wb = op & (1u << 21), up = op & (1u << 23);
                const auto list = op & 65535;
                if (!list || rn == 15 || (op & (1u << 22)) || (wb && (list & (1u << rn)))
                    || (load && (list & 32768) && !last)) return false;
                const auto base = reg(rn); unsigned offset = 0;
                if (dynamic_memory) {
                    const unsigned bytes = std::popcount(list) * 4;
                    const bool pre = op & (1u << 24);
                    auto address = alu(op_i32_add, base, imm(up ? (pre ? 4 : 0) : (pre ? -bytes : 4 - bytes)));
                    dynamic_host = make(guarded_host, type_i32, address, imm(bytes),
                        (snapshots.size() - 1) * 2 + !load);
                }
                for (unsigned r = 0; r < 16; ++r) if (list & (1u << r)) {
                    if (!memory(load, r, offset)) return false;
                    offset += 4;
                }
                if (wb) next.regs[rn] = alu(up ? op_i32_add : op_i32_sub, base, imm(offset));
                if (load && (list & 32768)) { next.flags[4] = alu(op_i32_and, next.regs[15], imm(1)); writes_pc = true; }
            } else if ((op & 0x0f8000f0u) == 0x00800090u) {
                if ((op & (1u << 20)) || rn == 15 || rd == 15 || rn == rd || rm == 15 || rs == 15) return false;
                const auto extend = op & (1u << 22) ? op_i64_extend_i32_s : op_i64_extend_i32_u;
                auto product = alu(op_i64_mul, unary(extend, reg(rm)), unary(extend, reg(rs)));
                if (op & (1u << 21)) product = alu(op_i64_add, product, alu(pack, reg(rd), reg(rn)));
                next.regs[rd] = unary(low, product); next.regs[rn] = unary(high, product);
            } else if ((op & 0x0fc000f0u) == 0x00000090u) {
                if ((op & (1u << 20)) || rn == 15 || rm == 15 || rs == 15 || ((op & (1u << 21)) && rd == 15)) return false;
                auto product = alu(op_i32_mul, reg(rm), reg(rs));
                if (op & (1u << 21)) product = alu(op_i32_add, product, reg(rd));
                next.regs[rn] = product;
            } else if ((op & 0x0e000090u) == 0x00000090u && (op & 0x60)) {
                const bool pre = op & (1u << 24), up = op & (1u << 23), load = op & (1u << 20);
                const bool immediate = op & (1u << 22), wb = !pre || (op & (1u << 21));
                const bool sign = op & (1u << 6);
                const unsigned bytes = op & (1u << 5) ? 2 : 1;
                if (!dynamic_memory || rd == 15 || (!load && (sign || bytes != 2))
                    || (!pre && (op & (1u << 21))) || (wb && rn == rd)
                    || (rn == 15 && (!load || !pre || wb || !immediate))
                    || (!immediate && ((op & 0xf00) || rm == 15))) return false;
                const auto base = reg(rn);
                const auto offset = immediate ? imm(((op >> 4) & 0xf0) | (op & 15)) : reg(rm);
                const auto address = pre ? alu(up ? op_i32_add : op_i32_sub, base, offset) : base;
                dynamic_host = make(guarded_host, type_i32, address, imm(bytes),
                    (snapshots.size() - 1) * 2 + !load);
                if (!memory(load, rd, 0, bytes, sign)) return false;
                if (wb) next.regs[rn] = alu(up ? op_i32_add : op_i32_sub, base, offset);
            } else if (((op >> 26) & 3) == 0) {
                // PSR/miscellaneous encodings have test-opcode bits without S.
                // Register-controlled shifts remain with the original compiler.
                const unsigned code = (op >> 21) & 15;
                const bool set_flags = op & (1u << 20), test = code >= 8 && code <= 11;
                if ((!allow_flags && (set_flags || (code >= 6 && code <= 11)))
                    || (test && !set_flags)
                    || (!test && rd == 15 && (set_flags || !last || code != 13))) return false;
                const auto old_carry = next.flags[2];
                value operand, shifter_carry = old_carry;
                if (op & (1u << 25)) {
                    unsigned rot = ((op >> 8) & 15) * 2, byte = op & 255;
                    operand = imm(rot ? (byte >> rot) | (byte << (32 - rot)) : byte);
                    if (set_flags && rot) shifter_carry = alu(op_i32_shr_u, operand, imm(31));
                } else {
                    if (op & (1u << 4)) return false;
                    const unsigned shift = (op >> 5) & 3, amount = (op >> 7) & 31;
                    const auto source = reg(rm);
                    operand = shifted(source, shift, amount);
                    if (set_flags && (shift || amount)) {
                        const unsigned bit = shift == 0 ? 32 - amount
                            : shift == 3 && !amount ? 0 : amount ? amount - 1 : 31;
                        shifter_carry = alu(op_i32_and, alu(op_i32_shr_u, source, imm(bit)), imm(1));
                    }
                }
                value result;
                switch (code) {
                case 0: case 8: result = alu(op_i32_and, reg(rn), operand); break;
                case 1: case 9: result = alu(op_i32_xor, reg(rn), operand); break;
                case 2: case 10: result = alu(op_i32_sub, reg(rn), operand); break;
                case 3: result = alu(op_i32_sub, operand, reg(rn)); break;
                case 4: case 11: result = alu(op_i32_add, reg(rn), operand); break;
                case 5: result = alu(op_i32_add, alu(op_i32_add, reg(rn), operand), next.flags[2]); break;
                case 6: result = alu(op_i32_sub, alu(op_i32_sub, reg(rn), operand), alu(op_i32_xor, old_carry, imm(1))); break;
                case 7: result = alu(op_i32_sub, alu(op_i32_sub, operand, reg(rn)), alu(op_i32_xor, old_carry, imm(1))); break;
                case 12: result = alu(op_i32_or, reg(rn), operand); break;
                case 13: result = operand; break;
                case 14: result = alu(op_i32_and, reg(rn), alu(op_i32_xor, operand, imm(0xffffffff))); break;
                case 15: result = alu(op_i32_xor, operand, imm(0xffffffff)); break;
                default: return false;
                }
                if (set_flags) {
                    ++flag_instructions;
                    next.flags[0] = alu(op_i32_shr_u, result, imm(31));
                    next.flags[1] = alu(op_i32_eq, result, imm(0));
                    const bool subtract = code == 2 || code == 3 || code == 6 || code == 7 || code == 10;
                    const bool add = code == 4 || code == 5 || code == 11;
                    if (subtract) {
                        const bool reverse = code == 3 || code == 7;
                        const auto a = reverse ? operand : reg(rn), b = reverse ? reg(rn) : operand;
                        next.flags[2] = code == 6 || code == 7
                            ? alu(op_i32_or, alu(op_i32_gt_u, a, b), alu(op_i32_and, alu(op_i32_eq, a, b), old_carry))
                            : alu(op_i32_ge_u, a, b);
                        next.flags[3] = alu(op_i32_shr_u,
                            alu(op_i32_and, alu(op_i32_xor, a, b), alu(op_i32_xor, a, result)), imm(31));
                    } else if (add) {
                        next.flags[2] = alu(op_i32_lt_u, result, reg(rn));
                        if (code == 5) next.flags[2] = alu(op_i32_or, next.flags[2],
                            alu(op_i32_and, alu(op_i32_eq, result, reg(rn)), old_carry));
                        next.flags[3] = alu(op_i32_shr_u,
                            alu(op_i32_and, alu(op_i32_xor, reg(rn), result), alu(op_i32_xor, operand, result)), imm(31));
                    } else next.flags[2] = shifter_carry;
                }
                if (!test) { next.regs[rd] = result; writes_pc = rd == 15; }
            } else return false;
            if (!writes_pc) next.regs[15] = imm(pc + 4);
            ++next.count; snapshots.push_back(next); return true;
        }

        value select(value predicate, value yes, value no) {
            if (optimize) {
                if (yes == no) return yes;
                if (nodes[predicate].op == constant) return nodes[predicate].immediate ? yes : no;
            }
            // choose.immediate is a third VALUE dependency, not a literal.
            return make(choose, type_i32, yes, no, predicate);
        }
        value condition(unsigned cond) {
            const auto &f = snapshots.back().flags;
            auto invert = [&](value v) { return binary(op_i32_xor, v, imm(1)); };
            switch (cond) {
            case 0: return f[1]; case 1: return invert(f[1]);
            case 2: return f[2]; case 3: return invert(f[2]);
            case 4: return f[0]; case 5: return invert(f[0]);
            case 6: return f[3]; case 7: return invert(f[3]);
            case 8: return binary(op_i32_and, f[2], invert(f[1]));
            case 9: return binary(op_i32_or, invert(f[2]), f[1]);
            case 10: return binary(op_i32_eq, f[0], f[3]);
            case 11: return binary(op_i32_xor, f[0], f[3]);
            case 12: return binary(op_i32_and, invert(f[1]), binary(op_i32_eq, f[0], f[3]));
            case 13: return binary(op_i32_or, f[1], binary(op_i32_xor, f[0], f[3]));
            default: return imm(1);
            }
        }
        template <typename AccessMap>
        bool append_conditional(std::uint32_t op, std::uint32_t pc, const AccessMap &accesses,
            bool dynamic_memory, bool allow_flags) {
            const unsigned cond = op >> 28;
            if (cond == 14) return append(op, pc, false, accesses, dynamic_memory, allow_flags);
            if (cond == 15) return false;
            const auto old = snapshots.back();
            const auto predicate = condition(cond); // pre-instruction NZCV
            const auto effect_count = effects.size();
            if (!append((op & 0x0fffffffu) | 0xe0000000u, pc, false, accesses, dynamic_memory, allow_flags)
                || effects.size() != effect_count || snapshots.back().regs[15] != imm(pc + 4)) return false;
            // Only nontrapping pure integer expressions reach this merge. Never
            // speculate conditional memory effects, PC writes or helper calls.
            auto next = snapshots.back();
            for (unsigned reg = 0; reg < 15; ++reg)
                next.regs[reg] = select(predicate, next.regs[reg], old.regs[reg]);
            for (unsigned flag = 0; flag < 5; ++flag)
                next.flags[flag] = select(predicate, next.flags[flag], old.flags[flag]);
            snapshots.back() = next; ++conditional_instructions; return true;
        }

        // Only the caller's validated inline-leaf stream may use these.
        // Counts and true guest PCs include both BL and BX LR. The leaf proof
        // excludes LR writes; its return is the known ARM caller continuation.
        void inline_call(std::uint32_t pc, std::uint32_t target) {
            auto next = snapshots.back();
            next.regs[14] = imm(pc + 4); next.regs[15] = imm(target);
            ++next.count; snapshots.push_back(next); ++inline_transfers;
        }
        void inline_return(std::uint32_t continuation) {
            auto next = snapshots.back(); next.regs[15] = imm(continuation);
            ++next.count; snapshots.push_back(next); ++inline_transfers;
        }

        bool valid() const {
            for (unsigned v = 1; v < nodes.size(); ++v) {
                const auto &n = nodes[v];
                if (n.a >= v || n.b >= v) return false;
                auto unary_type = [&](unsigned input, unsigned output) {
                    return n.a && !n.b && nodes[n.a].type == input && n.type == output;
                };
                auto binary_type = [&](unsigned input, unsigned output) {
                    return n.a && n.b && nodes[n.a].type == input && nodes[n.b].type == input && n.type == output;
                };
                switch (n.op) {
                case constant: if (n.a || n.b || (n.type != type_i32 && n.type != type_i64)) return false; break;
                case state: case host: if (n.a || n.b || n.type != type_i32) return false; break;
                case low: case high: if (!unary_type(type_i64, type_i32)) return false; break;
                case op_i64_extend_i32_s: case op_i64_extend_i32_u: if (!unary_type(type_i32, type_i64)) return false; break;
                case choose:
                    if (!binary_type(type_i32, type_i32) || !n.immediate || n.immediate >= v
                        || nodes[n.immediate].type != type_i32) return false;
                    break;
                case pack: if (!binary_type(type_i32, type_i64)) return false; break;
                case op_i64_add: case op_i64_mul: if (!binary_type(type_i64, type_i64)) return false; break;
                case read32: case read8u: case read8s: case read16u: case read16s: if (!unary_type(type_i32, type_i32)) return false; break;
                case write32: case write8: case write16: if (!binary_type(type_i32, type_void)) return false; break;
                case guarded_host:
                    if (!binary_type(type_i32, type_i32) || nodes[n.b].op != constant
                        || !nodes[n.b].immediate || nodes[n.b].immediate > 64
                        || (nodes[n.b].immediate > 2 && (nodes[n.b].immediate & 3)) || n.immediate / 2 >= snapshots.size()) return false;
                    break;
                case op_i32_add: case op_i32_sub: case op_i32_mul: case op_i32_and:
                case op_i32_or: case op_i32_xor: case op_i32_shl: case op_i32_shr_s:
                case op_i32_shr_u: case op_i32_rotr: case op_i32_eq: case op_i32_lt_u: case op_i32_gt_u: case op_i32_ge_u:
                    if (!binary_type(type_i32, type_i32)) return false; break;
                default: return false;
                }
            }
            for (const auto &s : snapshots) {
                for (auto v : s.regs) if (!v || v >= nodes.size() || nodes[v].type != type_i32) return false;
                for (auto v : s.flags) if (!v || v >= nodes.size() || nodes[v].type != type_i32) return false;
            }
            unsigned previous = 0;
            for (auto v : effects) {
                if (v <= previous || v >= nodes.size() || !is_effect(nodes[v].op)) return false;
                previous = v;
            }
            return true;
        }

        std::vector<bool> live_for(const std::vector<unsigned> &exits) const {
            std::vector<bool> live(nodes.size());
            auto mark = [&](auto &&self, value v) -> void {
                if (!v || live[v]) return;
                live[v] = true; self(self, nodes[v].a); self(self, nodes[v].b);
                if (nodes[v].op == choose) self(self, static_cast<value>(nodes[v].immediate));
            };
            for (auto v : effects) mark(mark, v);
            for (auto e : exits) {
                const auto &snapshot = snapshots.at(e);
                for (unsigned r = 0; r < 16; ++r)
                    if (r == 15 || snapshot.regs[r] != snapshots.front().regs[r]) mark(mark, snapshot.regs[r]);
                for (unsigned f = 0; f < 5; ++f)
                    if (snapshot.flags[f] != snapshots.front().flags[f]) mark(mark, snapshot.flags[f]);
            }
            return live;
        }
    };
}
