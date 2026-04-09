/*
 * Copyright (c) 2024 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cpu/aot/thumb_translator.h>

#include <cstring>
#include <map>
#include <set>

namespace eka2l1::arm::aot {
    using S = state_offsets;

    static void leb(std::vector<std::uint8_t> &out, std::uint32_t v) {
        do {
            std::uint8_t b = v & 0x7F;
            v >>= 7;
            if (v) b |= 0x80;
            out.push_back(b);
        } while (v);
    }

    static void sleb(std::vector<std::uint8_t> &out, std::int32_t v) {
        bool more = true;
        while (more) {
            std::uint8_t b = v & 0x7F;
            v >>= 7;
            if ((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40)))
                more = false;
            else
                b |= 0x80;
            out.push_back(b);
        }
    }

    // Code emitter helper
    struct emit {
        std::vector<std::uint8_t> &b;
        bool unsupported = false; // set by bail_unsupported()
        // Number of early-exit bails emitted into the function body.
        // Incremented every time the decoder gives up mid-function and
        // hands control back to the interpreter. Lower is better — high
        // bail counts mean the generated WASM yields often and the
        // interpreter does most of the work. Used by tests to assert the
        // decoder doesn't emit bails for literal-pool data it misdecodes
        // as instructions.
        std::uint32_t bail_count = 0;

        void op(std::uint8_t o) { b.push_back(o); }
        void state_ptr() { op(op_local_get); leb(b, 0); }
        void get_local(std::uint32_t i) { op(op_local_get); leb(b, i); }
        void set_local(std::uint32_t i) { op(op_local_set); leb(b, i); }
        void tee_local(std::uint32_t i) { op(op_local_tee); leb(b, i); }
        void i32_const(std::int32_t v) { op(op_i32_const); sleb(b, v); }

        void load_i32(std::uint32_t offset) {
            state_ptr();
            op(op_i32_load); leb(b, 2); leb(b, offset);
        }
        void store_i32(std::uint32_t offset, std::uint32_t local) {
            state_ptr();
            get_local(local);
            op(op_i32_store); leb(b, 2); leb(b, offset);
        }
        void store_i32_const(std::uint32_t offset, std::int32_t val) {
            state_ptr();
            i32_const(val);
            op(op_i32_store); leb(b, 2); leb(b, offset);
        }

        // Load from arbitrary address in linear memory (for emulated RAM)
        void load_mem32(std::uint32_t addr_local) {
            get_local(addr_local);
            op(op_i32_load); leb(b, 2); leb(b, 0);
        }
        void load_mem16u(std::uint32_t addr_local) {
            get_local(addr_local);
            op(op_i32_load16_u); leb(b, 1); leb(b, 0);
        }
        void load_mem8u(std::uint32_t addr_local) {
            get_local(addr_local);
            op(op_i32_load8_u); leb(b, 0); leb(b, 0);
        }
        void store_mem32(std::uint32_t addr_local, std::uint32_t val_local) {
            get_local(addr_local);
            get_local(val_local);
            op(op_i32_store); leb(b, 2); leb(b, 0);
        }
        void store_mem16(std::uint32_t addr_local, std::uint32_t val_local) {
            get_local(addr_local);
            get_local(val_local);
            op(op_i32_store16); leb(b, 1); leb(b, 0);
        }
        void store_mem8(std::uint32_t addr_local, std::uint32_t val_local) {
            get_local(addr_local);
            get_local(val_local);
            op(op_i32_store8); leb(b, 0); leb(b, 0);
        }

        void load_reg(int r) { load_i32(S::reg(r)); }
        void store_reg(int r, std::uint32_t local) { store_i32(S::reg(r), local); }

        // Call imported function (index relative to imports)
        void call(std::uint32_t func_idx) { op(op_call); leb(b, func_idx); }

        void ret() { op(op_return); }

        // Bail: set PC, return instruction count (normal control flow exit)
        void bail(std::uint32_t pc, std::uint32_t instr_count) {
            store_i32_const(S::PC, static_cast<std::int32_t>(pc));
            i32_const(static_cast<std::int32_t>(instr_count));
            ret();
            bail_count++;
        }

        // Bail without touching PC — use when the instruction already
        // computed and stored PC (e.g. POP {PC}, BX LR, BLX Rm).
        // Leaves the state's PC alone so the interpreter dispatches at the
        // computed target instead of re-running the current instruction.
        void bail_preserve_pc(std::uint32_t instr_count) {
            i32_const(static_cast<std::int32_t>(instr_count));
            ret();
            bail_count++;
        }

        // Bail due to unsupported instruction — marks the translation as incomplete
        void bail_unsupported(std::uint32_t pc, std::uint32_t instr_count) {
            unsupported = true;
            bail(pc, instr_count);
        }
    };

    // Walk the code slice linearly from offset 0, following branches and
    // stopping at unconditional terminators (POP {PC}, BX LR, BX Rm, B imm
    // that falls outside the slice, or the end of the slice).
    //
    // Returns the set of byte offsets where an instruction actually begins
    // along a reachable control-flow path. Anything outside this set is
    // either a middle halfword of a 32-bit Thumb insn or trailing literal
    // pool data that the translator's internal forward-target scanner must
    // ignore (to avoid opening nested blocks for phantom targets).
    //
    // This is a conservative CFG walk: we follow every B/B<cond>/CBZ/CBNZ
    // target that stays inside the slice, recurse on it, and stop each
    // path at the first unconditional terminator. The result is the
    // transitive closure of all reachable instruction-start offsets.
    static std::set<std::size_t> find_reachable_offsets(
        const std::uint8_t *code, std::size_t code_size)
    {
        std::set<std::size_t> reachable;
        if (code_size < 2) return reachable;
        std::vector<std::size_t> worklist;
        worklist.push_back(0);
        while (!worklist.empty()) {
            std::size_t i = worklist.back();
            worklist.pop_back();
            while (i + 1 < code_size) {
                if (reachable.count(i)) break; // already walked from here
                reachable.insert(i);
                std::uint16_t insn = code[i] | (code[i+1] << 8);
                // 32-bit Thumb: 0xE800-0xFFFF first halfword.
                // Note: 0xE000-0xE7FF is the unconditional 16-bit B, not wide.
                bool is_wide = ((insn & 0xF800) == 0xE800)
                            || ((insn & 0xF000) == 0xF000);
                if (is_wide) {
                    // Treat all wide insns as fall-through (the main loop
                    // either translates them or bails; in either case
                    // control proceeds to the next insn). BL/BLX imm are
                    // wide and unconditional calls — they also return via
                    // LR, so fall-through is correct.
                    i += 4;
                    continue;
                }
                // 16-bit conditional branch: B<cond> imm8 (0xDxxx).
                // cond==0xE is undefined, cond==0xF is SVC.
                if ((insn & 0xF000) == 0xD000) {
                    std::uint8_t cond = (insn >> 8) & 0xF;
                    if (cond < 0xE) {
                        std::int8_t off = static_cast<std::int8_t>(insn & 0xFF);
                        std::int32_t target_off =
                            static_cast<std::int32_t>(i) + 4 + off * 2;
                        if (target_off >= 0
                            && static_cast<std::size_t>(target_off) + 1 < code_size) {
                            worklist.push_back(static_cast<std::size_t>(target_off));
                        }
                        // Conditional — also fall through.
                        i += 2;
                        continue;
                    }
                    // cond==0xF is SVC — treat as fall-through (it's a
                    // system call, the interpreter handles it).
                    i += 2;
                    continue;
                }
                // 16-bit unconditional B: 0xE000-0xE7FF.
                if ((insn & 0xF800) == 0xE000) {
                    std::int16_t off =
                        static_cast<std::int16_t>((insn & 0x7FF) << 5) >> 5;
                    std::int32_t target_off =
                        static_cast<std::int32_t>(i) + 4 + off * 2;
                    if (target_off >= 0
                        && static_cast<std::size_t>(target_off) + 1 < code_size) {
                        i = static_cast<std::size_t>(target_off);
                        continue;
                    }
                    // Out-of-slice target: path ends here (interpreter takes over).
                    break;
                }
                // CBZ/CBNZ (T1): 1011 x0 i1 1 imm5 Rn. Encoded as
                // 0xB100-0xB13F and 0xB900-0xB93F (plus i1 variants).
                if ((insn & 0xF500) == 0xB100) {
                    // Forward-only compare-and-branch. imm = i:imm5:0
                    // (i bit is bit 9, imm5 is bits 7:3). Target = PC+4+imm.
                    std::uint32_t imm5 = (insn >> 3) & 0x1F;
                    std::uint32_t i_bit = (insn >> 9) & 1;
                    std::uint32_t imm = (i_bit << 6) | (imm5 << 1);
                    std::int32_t target_off =
                        static_cast<std::int32_t>(i) + 4 + static_cast<std::int32_t>(imm);
                    if (target_off >= 0
                        && static_cast<std::size_t>(target_off) + 1 < code_size) {
                        worklist.push_back(static_cast<std::size_t>(target_off));
                    }
                    i += 2;
                    continue;
                }
                // POP with PC bit set: 0xBD00-0xBDFF (register return).
                if ((insn & 0xFF00) == 0xBD00) {
                    break;
                }
                // BX Rm: 0x4700-0x477F (bit 7 = 0). Unconditional register
                // branch (function return if Rm==LR).
                if ((insn & 0xFF80) == 0x4700) {
                    break;
                }
                // BLX Rm: 0x4780-0x47FF (bit 7 = 1). Register call — returns
                // via LR, so fall through.
                if ((insn & 0xFF80) == 0x4780) {
                    i += 2;
                    continue;
                }
                // Default: 16-bit fall-through.
                i += 2;
            }
        }
        return reachable;
    }

    // Broad scan: treat every halfword-aligned offset as a potential
    // branch or call instruction. Used to populate
    // `translate_result::branch_targets`, which the caller (aot_setup.cpp
    // extra-entry discovery) probes as candidate function entries.
    //
    // This is intentionally liberal: some "branches" it finds may be
    // halfwords inside a 32-bit Thumb insn or literal-pool data. The caller
    // probes each candidate with `try_translate_at`, which rejects invalid
    // addresses, so false positives are harmless. False negatives would
    // lose real entry points, so we err toward inclusion here.
    //
    // Recorded candidates:
    //   - 16-bit B<cond> imm8 targets (Dxxx)
    //   - 16-bit unconditional B imm11 targets (Exxx)
    //   - Return addresses right after BL/BLX imm (T1/T2 wide): when the
    //     callee returns via BX LR, control lands at `next_pc`, which may
    //     be a useful re-entry point into AOT.
    static std::set<std::uint32_t> find_branch_targets_broad(
        const std::uint8_t *code, std::size_t code_size, std::uint32_t start)
    {
        std::set<std::uint32_t> targets;
        for (std::size_t i = 0; i + 1 < code_size; i += 2) {
            std::uint16_t insn = code[i] | (code[i+1] << 8);
            std::uint32_t pc = start + static_cast<std::uint32_t>(i);

            // Conditional branches: B<cond> offset
            if ((insn & 0xF000) == 0xD000) {
                std::uint8_t cond = (insn >> 8) & 0xF;
                if (cond < 0xE) { // not SVC
                    std::int8_t offset = static_cast<std::int8_t>(insn & 0xFF);
                    std::uint32_t target = pc + 4 + offset * 2;
                    if (target >= start && target < start + code_size) {
                        targets.insert(target);
                    }
                }
            }
            // Unconditional branch: B offset
            if ((insn & 0xF800) == 0xE000) {
                std::int16_t offset = static_cast<std::int16_t>((insn & 0x7FF) << 5) >> 5;
                std::uint32_t target = pc + 4 + offset * 2;
                if (target >= start && target < start + code_size) {
                    targets.insert(target);
                }
            }
            // BL/BLX imm (wide): record the return address (next_pc = pc+4)
            // as a candidate entry. The callee returns via BX LR which
            // lands here; if this is a reachable AOT entry the interpreter
            // can dispatch back into WASM directly.
            if (i + 3 < code_size) {
                std::uint16_t insn2 = code[i+2] | (code[i+3] << 8);
                bool is_bl_or_blx = ((insn & 0xF800) == 0xF000)
                                 && ((insn2 & 0xC000) == 0xC000);
                if (is_bl_or_blx) {
                    std::uint32_t next_pc = pc + 4;
                    if (next_pc >= start && next_pc < start + code_size) {
                        targets.insert(next_pc);
                    }
                }
            }
        }
        return targets;
    }

    translate_result translate_thumb_block(
        const std::uint8_t *code,
        std::size_t code_size,
        std::uint32_t start_address,
        const sibling_map *siblings,
        const code_window *dll_code)
    {
        translate_result tr;
        tr.complete = false;
        wasm_func_def &result = tr.func;
        result.export_name = "f_" + std::to_string(start_address);

        // Locals: 0=state_ptr(param), 1=tmp1, 2=tmp2, 3=tmp3, 4=tmp4, 5=pc_idx, 6=addr_tmp
        result.num_locals = 6;
        const std::uint32_t TMP1 = 1, TMP2 = 2, TMP3 = 3, TMP4 = 4;
        const std::uint32_t PC_IDX = 5, ADDR_TMP = 6;

        emit w{result.body, false};

        // Build instruction address → index map
        std::map<std::uint32_t, std::uint32_t> addr_to_idx;
        std::uint32_t num_insns = 0;
        for (std::size_t i = 0; i + 1 < code_size; i += 2) {
            std::uint16_t insn = code[i] | (code[i+1] << 8);
            std::uint32_t addr = start_address + static_cast<std::uint32_t>(i);
            addr_to_idx[addr] = num_insns;

            // Check for 32-bit Thumb (any wide instruction: 0xE800-0xFFFF)
            bool is_wide = ((insn & 0xF800) == 0xE800) || ((insn & 0xF000) == 0xF000);
            if (is_wide) {
                if (i + 3 < code_size) i += 2; // skip second halfword
            }
            num_insns++;
        }

        // Compute reachable offsets via a simple CFG walk. This lets the
        // internal forward-target scanner ignore trailing literal pools and
        // other non-code data that happens to match a branch encoding.
        auto reachable = find_reachable_offsets(code, code_size);

        // Populate tr.branch_targets with the broad scan so the caller
        // (extra-entry discovery) sees every potential entry, including
        // ones that happen to sit at halfword offsets the reachability
        // walker couldn't prove were instructions. False positives are
        // cheap: try_translate_at rejects invalid addresses.
        {
            auto broad = find_branch_targets_broad(code, code_size, start_address);
            tr.branch_targets.assign(broad.begin(), broad.end());
        }

        // Second pre-scan: collect forward branch targets. Forward means the
        // target address is strictly greater than the branch source address.
        // We use these to open nested WASM blocks so forward branches can
        // stay inside the AOT function instead of bailing to the interpreter.
        std::set<std::uint32_t> forward_targets_set;
        for (std::size_t i : reachable) {
            if (i + 1 >= code_size) continue;
            std::uint16_t insn = code[i] | (code[i+1] << 8);
            std::uint32_t src = start_address + static_cast<std::uint32_t>(i);
            std::uint32_t target = 0;
            bool is_branch = false;
            if ((insn & 0xF000) == 0xD000) {
                std::uint8_t cond = (insn >> 8) & 0xF;
                if (cond < 0xE) { // not SVC
                    std::int8_t off = static_cast<std::int8_t>(insn & 0xFF);
                    target = src + 4 + off * 2;
                    is_branch = true;
                }
            } else if ((insn & 0xF800) == 0xE000) {
                std::int16_t off = static_cast<std::int16_t>((insn & 0x7FF) << 5) >> 5;
                target = src + 4 + off * 2;
                is_branch = true;
            }
            if (is_branch && target > src &&
                target >= start_address && target < start_address + code_size) {
                forward_targets_set.insert(target);
            }
        }
        // Sorted ascending: earliest target first.
        std::vector<std::uint32_t> fwd_sorted(
            forward_targets_set.begin(), forward_targets_set.end());

        // Build a map: target_addr -> index in fwd_sorted.
        std::unordered_map<std::uint32_t, std::uint32_t> fwd_idx;
        for (std::uint32_t k = 0; k < fwd_sorted.size(); k++) {
            fwd_idx[fwd_sorted[k]] = k;
        }

        // Structure:
        //   (block $exit                   ;; outermost
        //     (loop $loop                  ;; for backward branches
        //       (block                     ;; blk_N   (last forward target)
        //         ...
        //           (block                 ;; blk_0 (earliest forward target)
        //             ;; insns from start to fwd_0
        //           end)                   ;; closes blk_0 -> lands at fwd_0
        //           ;; insns from fwd_0 to fwd_1
        //         end)                     ;; closes blk_1 -> lands at fwd_1
        //         ...
        //       end)                       ;; closes blk_N -> lands at fwd_N
        //       ;; insns from fwd_N to end
        //       br $exit                   ;; fall-through exit
        //     end)                         ;; end loop
        //   end)                           ;; end block $exit
        //
        // Depths from inside blk_0:
        //   br 0 -> exit blk_0 -> lands at fwd_0
        //   br 1 -> exit blk_0 + blk_1 -> lands at fwd_1
        //   ...
        //   br N -> lands at fwd_N
        //   br N+1 -> $loop (loop top)
        //   br N+2 -> $exit (function exit)
        // After closing blk_0, depths shift: innermost is now blk_1,
        // so the same target fwd_1 is now at depth 0 (K - closed_count).

        // Initialize pc_idx = 0
        w.i32_const(0);
        w.set_local(PC_IDX);

        // block $exit (outermost)
        w.op(op_block); w.op(type_void);
        // loop $loop
        w.op(op_loop); w.op(type_void);
        // Open one block per forward target. Outermost first (blk_N), so
        // the innermost at the start of the code is blk_0 (earliest target).
        for (std::size_t k = 0; k < fwd_sorted.size(); k++) {
            w.op(op_block); w.op(type_void);
        }
        // Number of forward-target blocks currently open. Decrements as
        // translation reaches each forward target's address.
        std::uint32_t closed_count = 0;
        const std::uint32_t N_fwd = static_cast<std::uint32_t>(fwd_sorted.size());

        // Emit each instruction with branch target checks
        std::uint32_t insn_idx = 0;
        // One past the last byte consumed by a successfully-decoded insn.
        // Tracks how far the decoder walked before breaking out of the
        // loop (typically at a terminator like POP {PC}).
        std::uint32_t decoded_end_offset = 0;
        for (std::size_t i = 0; i + 1 < code_size; i += 2) {
            // Skip unreachable offsets. `reachable` is the CFG closure
            // from offset 0, so anything not in it is either the middle
            // halfword of a wide insn or literal-pool data past the
            // function's real end. Forward branch targets are always
            // reachable (they come from reachable sources), so this
            // never skips a valid landing pad. This eliminates the
            // mid-function wide-insn bails that would otherwise yield
            // execution back to the interpreter when the decoder hits
            // a literal-pool word (e.g. `F004 E51F` ARM veneer pattern,
            // `FFFF FFFF` sentinel, or function pointers matching wide
            // insn patterns) between a forward branch and its target.
            if (!reachable.count(i)) {
                continue;
            }
            std::uint16_t insn = code[i] | (code[i+1] << 8);
            std::uint32_t insn_addr = start_address + static_cast<std::uint32_t>(i);

            // Close any forward-target blocks whose end is at this address.
            // Emitting `end` here means: the instruction we're about to emit
            // is a forward branch target, and `br (depth_for_this_target)`
            // from earlier in the block will land here.
            while (closed_count < N_fwd && fwd_sorted[closed_count] == insn_addr) {
                w.op(op_end);
                closed_count++;
            }

            // Check for 32-bit Thumb (wide instruction)
            // First halfword: bits[15:11] == 11101/11110/11111 → 0xE800-0xFFFF.
            // 0xE000-0xE7FF is unconditional B (16-bit), don't match that.
            bool is_wide = ((insn & 0xF800) == 0xE800) || ((insn & 0xF000) == 0xF000);
            if (is_wide) {
                // Decode BL (T1): 11110 S imm10 | 11 J1 1 J2 imm11
                // Decode BLX imm (T2): same as BL but bit 12 of insn2 = 0.
                // BLX targets ARM mode; RVCT often emits BLX to small ARM
                // veneers of the form `LDR PC, [PC, #-4]; <thumb_target>`
                // (i.e. the ARM instruction 0xE51FF004 followed by a 4-byte
                // literal with the Thumb bit set). We can detect that
                // pattern via dll_code and fold the BLX into a direct call
                // to the real Thumb target, avoiding the ARM bail entirely.
                if (i + 3 < code_size) {
                    std::uint16_t insn2 = code[i+2] | (code[i+3] << 8);
                    bool is_bl_or_blx = ((insn & 0xF800) == 0xF000)
                        && ((insn2 & 0xC000) == 0xC000); // bits 15:14 = 11
                    bool is_bl = is_bl_or_blx && ((insn2 & 0x1000) != 0);
                    bool is_blx = is_bl_or_blx && ((insn2 & 0x1000) == 0)
                        && ((insn2 & 1) == 0); // BLX imm requires bit 0 = 0
                    if (is_bl || is_blx) {
                        std::uint32_t s = (insn >> 10) & 1;
                        std::uint32_t imm10 = insn & 0x3FF;
                        std::uint32_t j1 = (insn2 >> 13) & 1;
                        std::uint32_t j2 = (insn2 >> 11) & 1;
                        std::uint32_t imm11 = insn2 & 0x7FF;
                        std::uint32_t i1 = !(j1 ^ s);
                        std::uint32_t i2 = !(j2 ^ s);
                        std::int32_t imm32 = static_cast<std::int32_t>(
                            (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1));
                        if (s) imm32 |= 0xFF000000; // sign-extend bit 24
                        std::uint32_t target = insn_addr + 4 + imm32;
                        if (is_blx) {
                            // For BLX the target is 4-aligned (ARM mode).
                            // Align the source PC to 4 before computing the
                            // target, per ARM ARM.
                            std::uint32_t aligned_src = (insn_addr + 4) & ~3u;
                            target = aligned_src + imm32;

                            // Try to inline the ARM veneer:
                            //   E51FF004         LDR PC, [PC, #-4]
                            //   <thumb_addr>     literal loaded into PC
                            if (dll_code) {
                                std::uint32_t veneer[2] = {};
                                if (dll_code->read(target, veneer, 8)) {
                                    if (veneer[0] == 0xE51FF004) {
                                        std::uint32_t real = veneer[1];
                                        if (real & 1) {
                                            // Thumb target — fold into BL
                                            target = real & ~1u;
                                            is_blx = false;
                                            is_bl = true;
                                        }
                                    }
                                }
                            }
                        }
                        std::uint32_t next_pc = insn_addr + 4;

                        // Sibling lookup is Thumb-only; skip for unresolved BLX.
                        if (is_bl && siblings) {
                            auto it = siblings->find(target);
                            if (it != siblings->end()) {
                                // Set LR = next_pc | 1 before the call
                                w.store_i32_const(S::LR, static_cast<std::int32_t>(next_pc | 1));
                                // Call sibling: returns instruction count
                                w.state_ptr();
                                w.op(op_call);
                                leb(result.body, it->second);
                                // Add our BL's 1 instruction + our prior count
                                w.i32_const(static_cast<std::int32_t>(insn_idx + 1));
                                w.op(op_i32_add);
                                w.ret();
                                i += 2;
                                insn_idx++;
                                continue;
                            }
                        }

                        // Set LR = next_pc | 1 (Thumb return)
                        w.store_i32_const(S::LR, static_cast<std::int32_t>(next_pc | 1));
                        if (is_blx) {
                            // BLX enters ARM mode at target (word-aligned).
                            // Clear T flag so the interpreter decodes ARM.
                            w.store_i32_const(S::TFLAG, 0);
                        }
                        // Set PC to target and bail — interpreter re-dispatches
                        w.store_i32_const(S::PC, static_cast<std::int32_t>(target));
                        w.i32_const(static_cast<std::int32_t>(insn_idx + 1));
                        w.ret();
                        // Resume point at next_pc: when control returns from
                        // the external callee via BX LR, we want to dispatch
                        // back into AOT instead of the interpreter. Only
                        // record for external BL imm (target outside any
                        // known sibling) — internal tail calls to siblings
                        // handled separately.
                        tr.resume_points.push_back(next_pc);
                        i += 2; // skip second halfword
                        insn_idx++;
                        continue;
                    }
                }
                // Not a BL — mark as unsupported so this function is rejected.
                // Bailing at insn_addr with insn_idx=0 would cause an infinite
                // loop (interpreter re-dispatches to the same PC, AOT bails again).
                // For non-BL wide instructions we don't have a safe way to advance
                // PC from within WASM (the interpreter's wide-insn decoder is
                // instruction-specific), so we reject the translation.
                // Log the wide opcode so we can prioritize which wide insns to
                // implement next. Dedupe by opcode to keep the log small.
                {
                    std::uint16_t insn2_hw = (i + 3 < code_size)
                        ? static_cast<std::uint16_t>(code[i+2] | (code[i+3] << 8))
                        : 0;
                    static std::set<std::uint32_t> seen_wide;
                    std::uint32_t key = (static_cast<std::uint32_t>(insn) << 16) | insn2_hw;
                    if (seen_wide.insert(key).second) {
                        fprintf(stderr, "AOT: wide insn bail %04X %04X at 0x%08X (insn_idx=%u)\n",
                            insn, insn2_hw, insn_addr, insn_idx);
                    }
                }
                if (insn_idx == 0) {
                    w.bail_unsupported(insn_addr, insn_idx);
                } else {
                    // Later in the block — safe to bail because the interpreter
                    // running the wide insn will advance PC. insn_idx>0 so the
                    // caller sees forward progress.
                    w.bail(insn_addr, insn_idx);
                }
                if (i + 3 < code_size) {
                    i += 2; // skip second halfword of the 32-bit instruction
                }
                decoded_end_offset = static_cast<std::uint32_t>(i) + 2;
                insn_idx++;
                continue;
            }

            // Decode 16-bit Thumb
            bool handled = true;

            if ((insn & 0xF800) == 0x2000) {
                // MOVS Rd, #imm8
                int rd = (insn >> 8) & 7;
                int imm8 = insn & 0xFF;
                w.store_i32_const(S::reg(rd), imm8);
                // Update flags
                w.store_i32_const(S::NFLAG, 0);
                w.store_i32_const(S::ZFLAG, imm8 == 0 ? 1 : 0);
            } else if ((insn & 0xFE00) == 0x1C00) {
                // ADDS Rd, Rn, #imm3
                int rd = insn & 7;
                int rn = (insn >> 3) & 7;
                int imm3 = (insn >> 6) & 7;
                w.load_reg(rn);
                w.i32_const(imm3);
                w.op(op_i32_add);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
                // N flag
                w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP2);
                w.store_i32(S::NFLAG, TMP2);
                // Z flag
                w.get_local(TMP1); w.op(op_i32_eqz); w.set_local(TMP2);
                w.store_i32(S::ZFLAG, TMP2);
            } else if ((insn & 0xF800) == 0x3000) {
                // ADDS Rd, #imm8
                int rd = (insn >> 8) & 7;
                int imm8 = insn & 0xFF;
                w.load_reg(rd);
                w.i32_const(imm8);
                w.op(op_i32_add);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
                w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP2);
                w.store_i32(S::NFLAG, TMP2);
                w.get_local(TMP1); w.op(op_i32_eqz); w.set_local(TMP2);
                w.store_i32(S::ZFLAG, TMP2);
            } else if ((insn & 0xFA00) == 0x1A00) {
                // SUBS Rd, Rn, #imm3 (0x1E00) or SUBS Rd, Rn, Rm (0x1A00)
                int rd = insn & 7;
                int rn = (insn >> 3) & 7;
                w.load_reg(rn);
                w.set_local(TMP3); // save Rn for C flag
                if (insn & 0x0400) {
                    int imm3 = (insn >> 6) & 7;
                    w.i32_const(imm3);
                    w.set_local(TMP4);
                } else {
                    int rm = (insn >> 6) & 7;
                    w.load_reg(rm);
                    w.set_local(TMP4);
                }
                w.get_local(TMP3);
                w.get_local(TMP4);
                w.op(op_i32_sub);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
                w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP2);
                w.store_i32(S::NFLAG, TMP2);
                w.get_local(TMP1); w.op(op_i32_eqz); w.set_local(TMP2);
                w.store_i32(S::ZFLAG, TMP2);
                // C = (Rn >= operand) unsigned (no borrow)
                w.get_local(TMP3); w.get_local(TMP4); w.op(op_i32_ge_u); w.set_local(TMP2);
                w.store_i32(S::CFLAG, TMP2);
            } else if ((insn & 0xF800) == 0x3800) {
                // SUBS Rd, #imm8
                int rd = (insn >> 8) & 7;
                int imm8 = insn & 0xFF;
                w.load_reg(rd);
                w.set_local(TMP3); // save original for C flag
                w.get_local(TMP3);
                w.i32_const(imm8);
                w.op(op_i32_sub);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
                w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP2);
                w.store_i32(S::NFLAG, TMP2);
                w.get_local(TMP1); w.op(op_i32_eqz); w.set_local(TMP2);
                w.store_i32(S::ZFLAG, TMP2);
                // C = (Rd_orig >= imm8) unsigned
                w.get_local(TMP3); w.i32_const(imm8); w.op(op_i32_ge_u); w.set_local(TMP2);
                w.store_i32(S::CFLAG, TMP2);
            } else if ((insn & 0xFFC0) == 0x4280) {
                // CMP Rn, Rm
                int rn = insn & 7;
                int rm = (insn >> 3) & 7;
                w.load_reg(rn); w.set_local(TMP1);
                w.load_reg(rm); w.set_local(TMP2);
                w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_sub); w.set_local(TMP3);
                // N
                w.get_local(TMP3); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP4);
                w.store_i32(S::NFLAG, TMP4);
                // Z
                w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_eq); w.set_local(TMP4);
                w.store_i32(S::ZFLAG, TMP4);
                // C (unsigned >=)
                w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_ge_u); w.set_local(TMP4);
                w.store_i32(S::CFLAG, TMP4);
                // V (simplified: 0)
                w.store_i32_const(S::VFLAG, 0);
            } else if ((insn & 0xF800) == 0x2800) {
                // CMP Rn, #imm8
                int rn = (insn >> 8) & 7;
                int imm8 = insn & 0xFF;
                w.load_reg(rn); w.set_local(TMP1);
                // Z
                w.get_local(TMP1); w.i32_const(imm8); w.op(op_i32_eq); w.set_local(TMP2);
                w.store_i32(S::ZFLAG, TMP2);
                // N
                w.get_local(TMP1); w.i32_const(imm8); w.op(op_i32_sub);
                w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP2);
                w.store_i32(S::NFLAG, TMP2);
                // C
                w.get_local(TMP1); w.i32_const(imm8); w.op(op_i32_ge_u); w.set_local(TMP2);
                w.store_i32(S::CFLAG, TMP2);
                w.store_i32_const(S::VFLAG, 0);
            } else if ((insn & 0xF800) == 0x6800) {
                // LDR Rt, [Rn, #imm5*4]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int imm5 = (insn >> 6) & 0x1F;
                // addr = Rn + imm5*4, load from emulated memory via imported tlb_read32
                w.load_reg(rn);
                if (imm5 > 0) { w.i32_const(imm5 * 4); w.op(op_i32_add); }
                w.set_local(ADDR_TMP);
                // Call imported tlb_read32(state_ptr, arm_addr) -> host_value
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.call(0); // import index 0 = tlb_read32
                w.set_local(TMP1);
                w.store_reg(rt, TMP1);
            } else if ((insn & 0xFE00) == 0x5800) {
                // LDR Rt, [Rn, Rm]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int rm = (insn >> 6) & 7;
                w.load_reg(rn);
                w.load_reg(rm);
                w.op(op_i32_add);
                w.set_local(ADDR_TMP);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.call(0); // tlb_read32
                w.set_local(TMP1);
                w.store_reg(rt, TMP1);
            } else if ((insn & 0xF800) == 0x6000) {
                // STR Rt, [Rn, #imm5*4]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int imm5 = (insn >> 6) & 0x1F;
                w.load_reg(rn);
                if (imm5 > 0) { w.i32_const(imm5 * 4); w.op(op_i32_add); }
                w.set_local(ADDR_TMP);
                w.load_reg(rt);
                w.set_local(TMP1);
                // Call imported tlb_write32(state_ptr, arm_addr, value)
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.get_local(TMP1);
                w.call(1); // import index 1 = tlb_write32
            } else if ((insn & 0xF800) == 0x9800) {
                // LDR Rt, [SP, #imm8*4]
                int rt = (insn >> 8) & 7;
                int imm8 = insn & 0xFF;
                w.load_reg(13); // SP
                if (imm8 > 0) { w.i32_const(imm8 * 4); w.op(op_i32_add); }
                w.set_local(ADDR_TMP);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.call(0);
                w.set_local(TMP1);
                w.store_reg(rt, TMP1);
            } else if ((insn & 0xF800) == 0x9000) {
                // STR Rt, [SP, #imm8*4]
                int rt = (insn >> 8) & 7;
                int imm8 = insn & 0xFF;
                w.load_reg(13);
                if (imm8 > 0) { w.i32_const(imm8 * 4); w.op(op_i32_add); }
                w.set_local(ADDR_TMP);
                w.load_reg(rt);
                w.set_local(TMP1);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.get_local(TMP1);
                w.call(1);
            } else if ((insn & 0xFFC0) == 0x6A00) {
                // LDR Rt, [Rn, #imm5*4] with higher offset bits
                // Actually 0x6A00 is LDR with imm5 bits [10:6]
                // Already handled above in 0x6800 range
                handled = false;
            } else if ((insn & 0xFF00) == 0x4400) {
                // ADD Rdn, Rm (T2, high register) — no flags update
                int rdn = (insn & 7) | ((insn >> 4) & 8);
                int rm = (insn >> 3) & 0xF;
                w.load_reg(rdn);
                w.load_reg(rm);
                w.op(op_i32_add);
                w.set_local(TMP1);
                w.store_reg(rdn, TMP1);
            } else if ((insn & 0xFF00) == 0x4600) {
                // MOV Rd, Rm (high register)
                int rd = (insn & 7) | ((insn >> 4) & 8);
                int rm = (insn >> 3) & 0xF;
                w.load_reg(rm);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFFC0) == 0x0000 && (insn & 0x07C0) != 0) {
                // LSLS Rd, Rm, #imm5
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                int imm5 = (insn >> 6) & 0x1F;
                w.load_reg(rm);
                w.i32_const(imm5);
                w.op(op_i32_shl);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xF800) == 0x0800) {
                // LSRS Rd, Rm, #imm5
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                int imm5 = (insn >> 6) & 0x1F;
                if (imm5 == 0) imm5 = 32;
                w.load_reg(rm);
                w.i32_const(imm5);
                w.op(op_i32_shr_u);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFFC0) == 0x4000) {
                // ANDS Rd, Rm
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                w.load_reg(rd);
                w.load_reg(rm);
                w.op(op_i32_and);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFFC0) == 0x43C0) {
                // MVNS Rd, Rm
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                w.load_reg(rm);
                w.i32_const(-1);
                w.op(op_i32_xor);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
                // Update N, Z flags
                w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP2);
                w.store_i32(S::NFLAG, TMP2);
                w.get_local(TMP1); w.op(op_i32_eqz); w.set_local(TMP2);
                w.store_i32(S::ZFLAG, TMP2);
            } else if ((insn & 0xF000) == 0xD000) {
                // Conditional branch: B<cond> offset
                std::uint8_t cond = (insn >> 8) & 0xF;
                if (cond >= 0xE) {
                    // SVC or undefined — bail
                    w.bail_unsupported(insn_addr, insn_idx);
                    insn_idx++;
                    continue;
                }
                std::int8_t offset = static_cast<std::int8_t>(insn & 0xFF);
                std::uint32_t target = insn_addr + 4 + offset * 2;

                // Emit the boolean expression for `cond`. Leaves a bool on
                // the stack. Unsupported cond bails and returns false.
                auto emit_cond = [&]() -> bool {
                    switch (cond) {
                    case 0: w.load_i32(S::ZFLAG); return true; // BEQ: Z==1
                    case 1: w.load_i32(S::ZFLAG); w.op(op_i32_eqz); return true; // BNE: Z==0
                    case 2: w.load_i32(S::CFLAG); return true; // BCS/BHS: C==1
                    case 3: w.load_i32(S::CFLAG); w.op(op_i32_eqz); return true; // BCC/BLO: C==0
                    case 4: w.load_i32(S::NFLAG); return true; // BMI: N==1
                    case 5: w.load_i32(S::NFLAG); w.op(op_i32_eqz); return true; // BPL: N==0
                    case 6: w.load_i32(S::VFLAG); return true; // BVS: V==1
                    case 7: w.load_i32(S::VFLAG); w.op(op_i32_eqz); return true; // BVC: V==0
                    case 8: // BHI: C==1 && Z==0
                        w.load_i32(S::CFLAG);
                        w.load_i32(S::ZFLAG); w.op(op_i32_eqz);
                        w.op(op_i32_and); return true;
                    case 9: // BLS: C==0 || Z==1
                        w.load_i32(S::CFLAG); w.op(op_i32_eqz);
                        w.load_i32(S::ZFLAG);
                        w.op(op_i32_or); return true;
                    case 10: // BGE: N==V
                        w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_eq); return true;
                    case 11: // BLT: N!=V
                        w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_ne); return true;
                    case 12: // BGT: Z==0 && N==V
                        w.load_i32(S::ZFLAG); w.op(op_i32_eqz);
                        w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_eq);
                        w.op(op_i32_and); return true;
                    case 13: // BLE: Z==1 || N!=V
                        w.load_i32(S::ZFLAG);
                        w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_ne);
                        w.op(op_i32_or); return true;
                    }
                    return false;
                };

                // Resolve the target:
                //   (a) Forward target (target > insn_addr AND in fwd_idx) →
                //       `cond; br_if depth` where depth is
                //       fwd_idx[target] - closed_count.
                //   (b) Backward target that's a known sibling AOT entry →
                //       `if (cond) { tail-call sibling; return }`. The
                //       sibling is a separate f_<target> function registered
                //       via branch_targets discovery in aot_setup.
                //   (c) Backward target without a sibling → `if (cond) bail(target)`.
                //       The interpreter re-dispatches to the target PC; if
                //       it's also an AOT entry, we re-enter AOT at the
                //       correct PC with no WASM loop-top re-run.
                //   (d) Forward target outside the block → bail(target).
                // Important: a target can be BOTH a forward target (of some
                // earlier branch) and a backward target (of this one) when a
                // loop head is entered via a forward skip. Require target
                // > insn_addr before using the forward path; otherwise the
                // depth computation underflows once the forward block has
                // been closed.
                auto fwd_it = fwd_idx.find(target);
                bool is_fwd = (fwd_it != fwd_idx.end()) && (target > insn_addr);
                if (is_fwd) {
                    if (!emit_cond()) { w.bail_unsupported(insn_addr, insn_idx); insn_idx++; continue; }
                    std::uint32_t depth = fwd_it->second - closed_count;
                    w.op(op_br_if); leb(result.body, depth);
                } else {
                    // Backward (or out-of-block) target. Prefer a direct
                    // sibling call so the loop stays entirely inside WASM;
                    // otherwise bail to the target PC.
                    if (!emit_cond()) { w.bail_unsupported(insn_addr, insn_idx); insn_idx++; continue; }
                    w.op(op_if); w.op(type_void);
                    std::uint32_t sibling_idx = 0;
                    bool has_sibling = false;
                    if (siblings) {
                        auto sit = siblings->find(target);
                        if (sit != siblings->end()) {
                            sibling_idx = sit->second;
                            has_sibling = true;
                        }
                    }
                    if (has_sibling) {
                        // Call sibling f_<target>: returns instruction count
                        w.state_ptr();
                        w.op(op_call);
                        leb(result.body, sibling_idx);
                        // Add our prior count + this branch insn (1)
                        w.i32_const(static_cast<std::int32_t>(insn_idx + 1));
                        w.op(op_i32_add);
                        w.ret();
                    } else {
                        w.bail(target, insn_idx + 1);
                    }
                    w.op(op_end);
                }
            } else if ((insn & 0xF800) == 0xE000) {
                // Unconditional branch B
                std::int16_t offset = static_cast<std::int16_t>((insn & 0x7FF) << 5) >> 5;
                std::uint32_t target = insn_addr + 4 + offset * 2;

                auto fwd_it = fwd_idx.find(target);
                // See comment in the B<cond> handler about fwd/backward
                // overlap: require target > insn_addr before using the
                // forward-block path.
                bool is_fwd = (fwd_it != fwd_idx.end()) && (target > insn_addr);
                if (is_fwd) {
                    // Forward branch within block — skip via nested block exit.
                    std::uint32_t depth = fwd_it->second - closed_count;
                    w.op(op_br); leb(result.body, depth);
                } else {
                    // Backward (or out-of-block) target. Prefer a direct
                    // sibling call; otherwise bail to target PC so the
                    // interpreter re-dispatches at the correct address.
                    std::uint32_t sibling_idx = 0;
                    bool has_sibling = false;
                    if (siblings) {
                        auto sit = siblings->find(target);
                        if (sit != siblings->end()) {
                            sibling_idx = sit->second;
                            has_sibling = true;
                        }
                    }
                    if (has_sibling) {
                        w.state_ptr();
                        w.op(op_call);
                        leb(result.body, sibling_idx);
                        w.i32_const(static_cast<std::int32_t>(insn_idx + 1));
                        w.op(op_i32_add);
                        w.ret();
                    } else {
                        w.bail(target, insn_idx + 1);
                    }
                }
                // Unconditional B terminates linear control flow. If
                // forward targets remain, keep decoding (they may be hit
                // by forward branches from earlier). Otherwise stop.
                if (closed_count >= N_fwd) {
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 2;
                    insn_idx++;
                    break;
                }
            } else if ((insn & 0xFE00) == 0xB400) {
                // PUSH {reglist} — bit 8 = LR
                std::uint16_t reglist = insn & 0xFF;
                bool push_lr = (insn & 0x100) != 0;
                // Count registers to push
                int count = 0;
                for (int r = 0; r < 8; r++) {
                    if (reglist & (1 << r)) count++;
                }
                if (push_lr) count++;
                // SP -= count * 4
                w.load_reg(13);
                w.i32_const(count * 4);
                w.op(op_i32_sub);
                w.set_local(TMP1);
                w.store_reg(13, TMP1);
                // Store registers at ascending addresses from new SP
                int offset = 0;
                for (int r = 0; r < 8; r++) {
                    if (reglist & (1 << r)) {
                        w.load_reg(r);
                        w.set_local(TMP2);
                        // tlb_write32(state_ptr, SP + offset, value)
                        w.state_ptr();
                        w.get_local(TMP1);
                        if (offset > 0) { w.i32_const(offset); w.op(op_i32_add); }
                        w.get_local(TMP2);
                        w.call(1); // tlb_write32
                        offset += 4;
                    }
                }
                if (push_lr) {
                    w.load_reg(14); // LR
                    w.set_local(TMP2);
                    w.state_ptr();
                    w.get_local(TMP1);
                    if (offset > 0) { w.i32_const(offset); w.op(op_i32_add); }
                    w.get_local(TMP2);
                    w.call(1);
                }
            } else if ((insn & 0xFE00) == 0xBC00) {
                // POP {reglist} — bit 8 = PC
                std::uint16_t reglist = insn & 0xFF;
                bool pop_pc = (insn & 0x100) != 0;
                // Load registers from SP at ascending addresses
                w.load_reg(13);
                w.set_local(TMP1); // current SP
                int offset = 0;
                for (int r = 0; r < 8; r++) {
                    if (reglist & (1 << r)) {
                        // tlb_read32(state_ptr, SP + offset)
                        w.state_ptr();
                        w.get_local(TMP1);
                        if (offset > 0) { w.i32_const(offset); w.op(op_i32_add); }
                        w.call(0); // tlb_read32
                        w.set_local(TMP2);
                        w.store_reg(r, TMP2);
                        offset += 4;
                    }
                }
                if (pop_pc) {
                    // Load PC value
                    w.state_ptr();
                    w.get_local(TMP1);
                    if (offset > 0) { w.i32_const(offset); w.op(op_i32_add); }
                    w.call(0);
                    w.set_local(TMP2);
                    w.store_reg(15, TMP2); // set PC
                    offset += 4;
                }
                // Count registers popped
                int count = 0;
                for (int r = 0; r < 8; r++) {
                    if (reglist & (1 << r)) count++;
                }
                if (pop_pc) count++;
                // SP += count * 4
                w.get_local(TMP1);
                w.i32_const(count * 4);
                w.op(op_i32_add);
                w.set_local(TMP2);
                w.store_reg(13, TMP2);
                if (pop_pc) {
                    // PC has been set by the pop above; bail without
                    // overwriting it so the interpreter dispatches at the
                    // return address.
                    w.bail_preserve_pc(insn_idx + 1);
                    // Function return — stop decoding past the POP if there
                    // are no more forward targets ahead.
                    if (closed_count >= N_fwd) {
                        decoded_end_offset = static_cast<std::uint32_t>(i) + 2;
                        insn_idx++;
                        break;
                    }
                }
            } else if ((insn & 0xFF80) == 0xB080) {
                // SUB SP, #imm7*4
                int imm7 = insn & 0x7F;
                w.load_reg(13);
                w.i32_const(imm7 * 4);
                w.op(op_i32_sub);
                w.set_local(TMP1);
                w.store_reg(13, TMP1);
            } else if ((insn & 0xFF80) == 0xB000) {
                // ADD SP, #imm7*4
                int imm7 = insn & 0x7F;
                w.load_reg(13);
                w.i32_const(imm7 * 4);
                w.op(op_i32_add);
                w.set_local(TMP1);
                w.store_reg(13, TMP1);
            } else if ((insn & 0xFE00) == 0x5C00) {
                // LDRB Rt, [Rn, Rm]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int rm = (insn >> 6) & 7;
                w.load_reg(rn);
                w.load_reg(rm);
                w.op(op_i32_add);
                w.set_local(ADDR_TMP);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.call(2); // tlb_read8
                w.set_local(TMP1);
                w.store_reg(rt, TMP1);
            } else if (insn == 0x4770) {
                // BX LR — function return. Load LR into PC (clearing the
                // Thumb bit since the interpreter masks it anyway) and bail.
                w.load_reg(14);
                w.i32_const(~1);
                w.op(op_i32_and);
                w.set_local(TMP1);
                w.store_reg(15, TMP1);
                w.bail_preserve_pc(insn_idx + 1);
                // Function return — linear control flow ends. If there are
                // no more forward targets past this point, safe to stop
                // decoding. Otherwise keep decoding: forward branches from
                // earlier may still target instructions past this BX LR,
                // and those targets need real emitted bodies.
                if (closed_count >= N_fwd) {
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 2;
                    insn_idx++;
                    break;
                }
            } else if ((insn & 0xFF00) == 0x4700) {
                // BX Rm / BLX Rm
                int rm = (insn >> 3) & 0xF;
                bool is_blx = (insn & 0x80) != 0;
                if (is_blx) {
                    // BLX Rm — set LR = next instruction | 1 (Thumb)
                    w.store_i32_const(S::LR, static_cast<std::int32_t>((insn_addr + 2) | 1));
                    // Register a resume point at the instruction after the
                    // BLX: when the callee returns via BX LR, we want to
                    // re-enter AOT instead of falling into the interpreter.
                    tr.resume_points.push_back(insn_addr + 2);
                }
                // Set PC = Rm (mask Thumb bit) and bail without overwriting PC.
                w.load_reg(rm);
                w.i32_const(~1);
                w.op(op_i32_and);
                w.set_local(TMP1);
                w.store_reg(15, TMP1);
                w.bail_preserve_pc(insn_idx + 1);
                // Both BX and BLX are unconditional; linear control flow
                // ends here. BLX re-entry is handled via its resume point.
                // Stop decoding if no more forward targets lie ahead.
                if (closed_count >= N_fwd) {
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 2;
                    insn_idx++;
                    break;
                }
            } else if ((insn & 0xF800) == 0x0000) {
                // LSLS Rd, Rm, #imm5 (MOVS Rd, Rm when imm5==0)
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                int imm5 = (insn >> 6) & 0x1F;
                w.load_reg(rm);
                if (imm5 > 0) {
                    w.i32_const(imm5);
                    w.op(op_i32_shl);
                }
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
                // Update N, Z flags
                w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP2);
                w.store_i32(S::NFLAG, TMP2);
                w.get_local(TMP1); w.op(op_i32_eqz); w.set_local(TMP2);
                w.store_i32(S::ZFLAG, TMP2);
            } else if ((insn & 0xF800) == 0x8800) {
                // LDRH Rt, [Rn, #imm5*2]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int imm5 = (insn >> 6) & 0x1F;
                w.load_reg(rn);
                if (imm5 > 0) { w.i32_const(imm5 * 2); w.op(op_i32_add); }
                w.set_local(ADDR_TMP);
                // TODO: use tlb_read16 import when available
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.call(0); // tlb_read32 (reads 32 bits, we mask to 16)
                w.i32_const(0xFFFF);
                w.op(op_i32_and);
                w.set_local(TMP1);
                w.store_reg(rt, TMP1);
            } else if ((insn & 0xF800) == 0x8000) {
                // STRH Rt, [Rn, #imm5*2]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int imm5 = (insn >> 6) & 0x1F;
                w.load_reg(rn);
                if (imm5 > 0) { w.i32_const(imm5 * 2); w.op(op_i32_add); }
                w.set_local(ADDR_TMP);
                w.load_reg(rt);
                w.set_local(TMP1);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.get_local(TMP1);
                w.call(1); // tlb_write32 (writes full word — TODO: use write16)
            } else if ((insn & 0xF800) == 0x7800) {
                // LDRB Rt, [Rn, #imm5]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int imm5 = (insn >> 6) & 0x1F;
                w.load_reg(rn);
                if (imm5 > 0) { w.i32_const(imm5); w.op(op_i32_add); }
                w.set_local(ADDR_TMP);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.call(2); // tlb_read8
                w.set_local(TMP1);
                w.store_reg(rt, TMP1);
            } else if ((insn & 0xF800) == 0x7000) {
                // STRB Rt, [Rn, #imm5]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int imm5 = (insn >> 6) & 0x1F;
                w.load_reg(rn);
                if (imm5 > 0) { w.i32_const(imm5); w.op(op_i32_add); }
                w.set_local(ADDR_TMP);
                w.load_reg(rt);
                w.set_local(TMP1);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.get_local(TMP1);
                w.call(1); // tlb_write32 (TODO: write8)
            } else if ((insn & 0xFE00) == 0x5600) {
                // LDRSB Rt, [Rn, Rm]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int rm = (insn >> 6) & 7;
                w.load_reg(rn);
                w.load_reg(rm);
                w.op(op_i32_add);
                w.set_local(ADDR_TMP);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.call(2); // tlb_read8
                // Sign-extend from 8 bits
                w.i32_const(24);
                w.op(op_i32_shl);
                w.i32_const(24);
                w.op(op_i32_shr_s);
                w.set_local(TMP1);
                w.store_reg(rt, TMP1);
            } else if ((insn & 0xFE00) == 0x5E00) {
                // LDRSH Rt, [Rn, Rm]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int rm = (insn >> 6) & 7;
                w.load_reg(rn);
                w.load_reg(rm);
                w.op(op_i32_add);
                w.set_local(ADDR_TMP);
                // tlb_read32 then mask to 16 bits and sign-extend
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.call(0); // tlb_read32
                w.i32_const(0xFFFF);
                w.op(op_i32_and);
                w.i32_const(16);
                w.op(op_i32_shl);
                w.i32_const(16);
                w.op(op_i32_shr_s);
                w.set_local(TMP1);
                w.store_reg(rt, TMP1);
            } else if ((insn & 0xFE00) == 0x5A00) {
                // LDRH Rt, [Rn, Rm]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int rm = (insn >> 6) & 7;
                w.load_reg(rn);
                w.load_reg(rm);
                w.op(op_i32_add);
                w.set_local(ADDR_TMP);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.call(0); // tlb_read32
                w.i32_const(0xFFFF);
                w.op(op_i32_and);
                w.set_local(TMP1);
                w.store_reg(rt, TMP1);
            } else if ((insn & 0xFE00) == 0x5000) {
                // STR Rt, [Rn, Rm]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int rm = (insn >> 6) & 7;
                w.load_reg(rn);
                w.load_reg(rm);
                w.op(op_i32_add);
                w.set_local(ADDR_TMP);
                w.load_reg(rt);
                w.set_local(TMP1);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.get_local(TMP1);
                w.call(1); // tlb_write32
            } else if ((insn & 0xFE00) == 0x5200) {
                // STRH Rt, [Rn, Rm]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int rm = (insn >> 6) & 7;
                w.load_reg(rn);
                w.load_reg(rm);
                w.op(op_i32_add);
                w.set_local(ADDR_TMP);
                w.load_reg(rt);
                w.i32_const(0xFFFF);
                w.op(op_i32_and);
                w.set_local(TMP1);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.get_local(TMP1);
                w.call(1); // tlb_write32 (TODO: write16 — may overwrite adjacent halfword)
            } else if ((insn & 0xFE00) == 0x5400) {
                // STRB Rt, [Rn, Rm]
                int rt = insn & 7;
                int rn = (insn >> 3) & 7;
                int rm = (insn >> 6) & 7;
                w.load_reg(rn);
                w.load_reg(rm);
                w.op(op_i32_add);
                w.set_local(ADDR_TMP);
                w.load_reg(rt);
                w.i32_const(0xFF);
                w.op(op_i32_and);
                w.set_local(TMP1);
                w.state_ptr();
                w.get_local(ADDR_TMP);
                w.get_local(TMP1);
                w.call(1); // tlb_write32 (TODO: write8)
            } else if ((insn & 0xF800) == 0xA000) {
                // ADR Rd, label (ADD Rd, PC, #imm8*4)
                int rd = (insn >> 8) & 7;
                int imm8 = insn & 0xFF;
                // effective address = (PC+4) & ~3 + imm8*4
                std::uint32_t addr = ((insn_addr + 4) & ~3u) + imm8 * 4;
                w.i32_const(static_cast<std::int32_t>(addr));
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xF800) == 0xC000) {
                // STMIA Rn!, {reglist}
                int rn = (insn >> 8) & 7;
                std::uint16_t reglist = insn & 0xFF;
                w.load_reg(rn);
                w.set_local(ADDR_TMP);
                int count = 0;
                for (int r = 0; r < 8; r++) {
                    if (reglist & (1 << r)) {
                        w.load_reg(r);
                        w.set_local(TMP1);
                        w.state_ptr();
                        w.get_local(ADDR_TMP);
                        if (count > 0) { w.i32_const(count * 4); w.op(op_i32_add); }
                        w.get_local(TMP1);
                        w.call(1); // tlb_write32
                        count++;
                    }
                }
                // Writeback: Rn += count*4
                w.get_local(ADDR_TMP);
                w.i32_const(count * 4);
                w.op(op_i32_add);
                w.set_local(TMP1);
                w.store_reg(rn, TMP1);
            } else if ((insn & 0xF800) == 0xC800) {
                // LDMIA Rn!, {reglist}
                int rn = (insn >> 8) & 7;
                std::uint16_t reglist = insn & 0xFF;
                w.load_reg(rn);
                w.set_local(ADDR_TMP);
                int count = 0;
                for (int r = 0; r < 8; r++) {
                    if (reglist & (1 << r)) {
                        w.state_ptr();
                        w.get_local(ADDR_TMP);
                        if (count > 0) { w.i32_const(count * 4); w.op(op_i32_add); }
                        w.call(0); // tlb_read32
                        w.set_local(TMP1);
                        w.store_reg(r, TMP1);
                        count++;
                    }
                }
                // Writeback only if Rn not in reglist
                if (!(reglist & (1 << rn))) {
                    w.get_local(ADDR_TMP);
                    w.i32_const(count * 4);
                    w.op(op_i32_add);
                    w.set_local(TMP1);
                    w.store_reg(rn, TMP1);
                }
            } else if ((insn & 0xF800) == 0x4800) {
                // LDR Rt, [PC, #imm8*4] — literal pool load
                int rt = (insn >> 8) & 7;
                int imm8 = insn & 0xFF;
                // PC is aligned to 4 bytes, then add offset
                // Effective address = (PC & ~3) + 4 + imm8*4
                // But we don't track exact PC in WASM. Use the known instruction address.
                std::uint32_t effective_addr = ((insn_addr + 4) & ~3u) + imm8 * 4;
                // Read from this fixed ROM address
                w.state_ptr();
                w.i32_const(static_cast<std::int32_t>(effective_addr));
                w.call(0); // tlb_read32
                w.set_local(TMP1);
                w.store_reg(rt, TMP1);
            } else if ((insn & 0xF800) == 0xA800) {
                // ADD Rd, SP, #imm8*4
                int rd = (insn >> 8) & 7;
                int imm8 = insn & 0xFF;
                w.load_reg(13); // SP
                w.i32_const(imm8 * 4);
                w.op(op_i32_add);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFFC0) == 0x4240) {
                // NEGS Rd, Rm (RSB Rd, Rm, #0)
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                w.i32_const(0);
                w.load_reg(rm);
                w.op(op_i32_sub);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
                w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP2);
                w.store_i32(S::NFLAG, TMP2);
                w.get_local(TMP1); w.op(op_i32_eqz); w.set_local(TMP2);
                w.store_i32(S::ZFLAG, TMP2);
            } else if ((insn & 0xFFC0) == 0x4340) {
                // MULS Rd, Rm
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                w.load_reg(rd);
                w.load_reg(rm);
                w.op(op_i32_mul);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFFC0) == 0x4300) {
                // ORRS Rd, Rm
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                w.load_reg(rd);
                w.load_reg(rm);
                w.op(op_i32_or);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFFC0) == 0x4380) {
                // BICS Rd, Rm
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                w.load_reg(rd);
                w.load_reg(rm);
                w.i32_const(-1);
                w.op(op_i32_xor); // ~Rm
                w.op(op_i32_and); // Rd & ~Rm
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFFC0) == 0x4040) {
                // EORS Rd, Rm
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                w.load_reg(rd);
                w.load_reg(rm);
                w.op(op_i32_xor);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFFC0) == 0x40C0) {
                // LSRS Rd, Rs (register shift)
                int rd = insn & 7;
                int rs = (insn >> 3) & 7;
                w.load_reg(rd);
                w.load_reg(rs);
                w.op(op_i32_shr_u);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFFC0) == 0x4080) {
                // LSLS Rd, Rs (register shift)
                int rd = insn & 7;
                int rs = (insn >> 3) & 7;
                w.load_reg(rd);
                w.load_reg(rs);
                w.op(op_i32_shl);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFFC0) == 0x4100) {
                // ASRS Rd, Rs (register shift)
                int rd = insn & 7;
                int rs = (insn >> 3) & 7;
                w.load_reg(rd);
                w.load_reg(rs);
                w.op(op_i32_shr_s);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
                w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP2);
                w.store_i32(S::NFLAG, TMP2);
                w.get_local(TMP1); w.op(op_i32_eqz); w.set_local(TMP2);
                w.store_i32(S::ZFLAG, TMP2);
            } else if ((insn & 0xF800) == 0x1000) {
                // ASRS Rd, Rm, #imm5
                int rd = insn & 7;
                int rm = (insn >> 3) & 7;
                int imm5 = (insn >> 6) & 0x1F;
                if (imm5 == 0) imm5 = 32;
                w.load_reg(rm);
                w.i32_const(imm5);
                w.op(op_i32_shr_s);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFE00) == 0x1800) {
                // ADDS Rd, Rn, Rm
                int rd = insn & 7;
                int rn = (insn >> 3) & 7;
                int rm = (insn >> 6) & 7;
                w.load_reg(rn);
                w.load_reg(rm);
                w.op(op_i32_add);
                w.set_local(TMP1);
                w.store_reg(rd, TMP1);
            } else if ((insn & 0xFF00) == 0xBF00) {
                // NOP / IT hints — ignore
            } else if ((insn & 0xFFC0) == 0x42C0) {
                // CMN Rn, Rm
                int rn = insn & 7;
                int rm = (insn >> 3) & 7;
                w.load_reg(rn); w.set_local(TMP1);
                w.load_reg(rm); w.set_local(TMP2);
                w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_add); w.set_local(TMP3);
                w.get_local(TMP3); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP4);
                w.store_i32(S::NFLAG, TMP4);
                w.get_local(TMP3); w.op(op_i32_eqz); w.set_local(TMP4);
                w.store_i32(S::ZFLAG, TMP4);
            } else if ((insn & 0xFF00) == 0x4500) {
                // CMP Rn, Rm (T2, one or both regs high)
                int rn = (insn & 7) | ((insn >> 4) & 8);
                int rm = (insn >> 3) & 0xF;
                w.load_reg(rn); w.set_local(TMP1);
                w.load_reg(rm); w.set_local(TMP2);
                w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_sub); w.set_local(TMP3);
                w.get_local(TMP3); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP4);
                w.store_i32(S::NFLAG, TMP4);
                w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_eq); w.set_local(TMP4);
                w.store_i32(S::ZFLAG, TMP4);
                w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_ge_u); w.set_local(TMP4);
                w.store_i32(S::CFLAG, TMP4);
                w.store_i32_const(S::VFLAG, 0);
            } else if ((insn & 0xFFC0) == 0x4200) {
                // TST Rn, Rm
                int rn = insn & 7;
                int rm = (insn >> 3) & 7;
                w.load_reg(rn);
                w.load_reg(rm);
                w.op(op_i32_and);
                w.set_local(TMP1);
                w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP2);
                w.store_i32(S::NFLAG, TMP2);
                w.get_local(TMP1); w.op(op_i32_eqz); w.set_local(TMP2);
                w.store_i32(S::ZFLAG, TMP2);
            } else {
                handled = false;
            }

            if (!handled) {
                // Unsupported — bail to interpreter
                fprintf(stderr, "AOT: unsupported insn 0x%04X at 0x%08X\n", insn, insn_addr);
                w.bail_unsupported(insn_addr, insn_idx);
                break;
            }

            decoded_end_offset = static_cast<std::uint32_t>(i) + 2;
            insn_idx++;
        }

        // Close any remaining forward-target blocks. These are blocks whose
        // target address is past the end of the code we emitted (e.g., the
        // instruction loop broke out via an unconditional terminator before
        // reaching the last target's address). If a `br depth` from earlier
        // in the function targets one of these blocks, control lands right
        // after the block's `end` — we insert a bail there with PC set to
        // the target address, so the interpreter picks up execution.
        while (closed_count < N_fwd) {
            w.op(op_end); // close innermost still-open forward block
            // If a br landed here, bail at the forward target address.
            w.bail(fwd_sorted[closed_count], insn_idx);
            closed_count++;
        }

        // End of loop and outer block
        w.op(op_end); // end loop
        w.op(op_end); // end block

        // Return total instruction count
        w.i32_const(num_insns);
        w.ret();

        tr.complete = !w.unsupported;
        tr.end_address = start_address + decoded_end_offset;
        tr.bail_count = w.bail_count;
        return tr;
    }
}
