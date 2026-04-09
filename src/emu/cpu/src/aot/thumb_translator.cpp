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
        }

        // Bail due to unsupported instruction — marks the translation as incomplete
        void bail_unsupported(std::uint32_t pc, std::uint32_t instr_count) {
            unsupported = true;
            if (!logged_unsupported) {
                logged_unsupported = true;
                fprintf(stderr, "AOT: unsupported instruction at 0x%08X\n", pc);
            }
            bail(pc, instr_count);
        }
        bool logged_unsupported = false;
    };

    // First pass: scan for branch targets within the block
    static std::set<std::uint32_t> find_branch_targets(
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
        }
        return targets;
    }

    translate_result translate_thumb_block(
        const std::uint8_t *code,
        std::size_t code_size,
        std::uint32_t start_address)
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

        // Find branch targets
        auto targets = find_branch_targets(code, code_size, start_address);

        // Use a simple dispatch loop with br_table for branches within the block.
        // Structure:
        //   (block $exit
        //     (loop $loop
        //       ;; emit all instructions linearly
        //       ;; branches set pc_idx and br $loop
        //       ;; end of block falls through to $exit
        //     )
        //   )
        //   return instr_count

        // Initialize pc_idx = 0
        w.i32_const(0);
        w.set_local(PC_IDX);

        // block $exit (label 0 for br = exit)
        w.op(op_block); w.op(type_void);
        // loop $loop (label 0 for br = loop back, label 1 for br = exit)
        w.op(op_loop); w.op(type_void);

        // Emit each instruction with branch target checks
        std::uint32_t insn_idx = 0;
        for (std::size_t i = 0; i + 1 < code_size; i += 2) {
            std::uint16_t insn = code[i] | (code[i+1] << 8);
            std::uint32_t insn_addr = start_address + static_cast<std::uint32_t>(i);

            // No per-instruction skip check needed — forward branches bail to interpreter.

            // Check for 32-bit Thumb (wide instruction)
            // First halfword: bits[15:11] == 11101/11110/11111 → 0xE800-0xFFFF.
            // 0xE000-0xE7FF is unconditional B (16-bit), don't match that.
            bool is_wide = ((insn & 0xF800) == 0xE800) || ((insn & 0xF000) == 0xF000);
            if (is_wide) {
                // Decode BL (T1): 11110 S imm10 | 11 J1 1 J2 imm11
                // If it's a BL, set LR to next PC and jump to target — the
                // interpreter dispatches there, which may be another AOT function.
                // This allows AOT functions to "call" each other through the
                // interpreter's AOT dispatch loop.
                if (i + 3 < code_size) {
                    std::uint16_t insn2 = code[i+2] | (code[i+3] << 8);
                    bool is_bl = ((insn & 0xF800) == 0xF000) && ((insn2 & 0xD000) == 0xD000);
                    if (is_bl) {
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
                        std::uint32_t next_pc = insn_addr + 4;
                        // Set LR = next_pc | 1 (Thumb)
                        w.store_i32_const(S::LR, static_cast<std::int32_t>(next_pc | 1));
                        // Set PC to target and bail — interpreter re-dispatches
                        w.store_i32_const(S::PC, static_cast<std::int32_t>(target));
                        w.i32_const(static_cast<std::int32_t>(insn_idx + 1));
                        w.ret();
                        i += 2; // skip second halfword
                        insn_idx++;
                        continue;
                    }
                }
                // Not a BL — bail normally, interpreter runs the instruction
                w.bail(insn_addr, insn_idx);
                if (i + 3 < code_size) {
                    i += 2; // skip second halfword of the 32-bit instruction
                }
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

                // Check if target is within our block
                auto it = addr_to_idx.find(target);
                if (it == addr_to_idx.end()) {
                    // Branch outside block — bail
                    // Emit: if (cond) { set PC = target; return }
                    // Evaluate condition from flags
                    switch (cond) {
                    case 0: w.load_i32(S::ZFLAG); break; // BEQ: Z==1
                    case 1: w.load_i32(S::ZFLAG); w.op(op_i32_eqz); break; // BNE: Z==0
                    case 2: w.load_i32(S::CFLAG); break; // BCS/BHS: C==1
                    case 3: w.load_i32(S::CFLAG); w.op(op_i32_eqz); break; // BCC/BLO: C==0
                    case 4: w.load_i32(S::NFLAG); break; // BMI: N==1
                    case 5: w.load_i32(S::NFLAG); w.op(op_i32_eqz); break; // BPL: N==0
                    case 6: w.load_i32(S::VFLAG); break; // BVS: V==1
                    case 7: w.load_i32(S::VFLAG); w.op(op_i32_eqz); break; // BVC: V==0
                    case 8: // BHI: C==1 && Z==0
                        w.load_i32(S::CFLAG);
                        w.load_i32(S::ZFLAG); w.op(op_i32_eqz);
                        w.op(op_i32_and); break;
                    case 9: // BLS: C==0 || Z==1
                        w.load_i32(S::CFLAG); w.op(op_i32_eqz);
                        w.load_i32(S::ZFLAG);
                        w.op(op_i32_or); break;
                    case 10: // BGE: N==V
                        w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_eq); break;
                    case 11: // BLT: N!=V
                        w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_ne); break;
                    case 12: // BGT: Z==0 && N==V
                        w.load_i32(S::ZFLAG); w.op(op_i32_eqz);
                        w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_eq);
                        w.op(op_i32_and); break;
                    case 13: // BLE: Z==1 || N!=V
                        w.load_i32(S::ZFLAG);
                        w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_ne);
                        w.op(op_i32_or); break;
                    default: w.bail_unsupported(insn_addr, insn_idx); insn_idx++; continue;
                    }
                    w.op(op_if); w.op(type_void);
                    w.bail(target, insn_idx + 1);
                    w.op(op_end);
                } else {
                    // Branch within block
                    std::uint32_t target_idx = it->second;

                    if (target_idx > insn_idx) {
                        // Forward branch — bail to interpreter (can't skip in WASM structured flow)
                        switch (cond) {
                        case 0: w.load_i32(S::ZFLAG); break;
                        case 1: w.load_i32(S::ZFLAG); w.op(op_i32_eqz); break;
                        case 2: w.load_i32(S::CFLAG); break;
                        case 3: w.load_i32(S::CFLAG); w.op(op_i32_eqz); break;
                        case 4: w.load_i32(S::NFLAG); break;
                        case 5: w.load_i32(S::NFLAG); w.op(op_i32_eqz); break;
                        case 6: w.load_i32(S::VFLAG); break;
                        case 7: w.load_i32(S::VFLAG); w.op(op_i32_eqz); break;
                        case 8: w.load_i32(S::CFLAG); w.load_i32(S::ZFLAG); w.op(op_i32_eqz); w.op(op_i32_and); break;
                        case 9: w.load_i32(S::CFLAG); w.op(op_i32_eqz); w.load_i32(S::ZFLAG); w.op(op_i32_or); break;
                        case 10: w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_eq); break;
                        case 11: w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_ne); break;
                        case 12: w.load_i32(S::ZFLAG); w.op(op_i32_eqz); w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_eq); w.op(op_i32_and); break;
                        case 13: w.load_i32(S::ZFLAG); w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_ne); w.op(op_i32_or); break;
                        default: w.bail_unsupported(insn_addr, insn_idx); insn_idx++; continue;
                        }
                        w.op(op_if); w.op(type_void);
                        w.bail(target, insn_idx + 1);
                        w.op(op_end);
                    } else {
                        // Backward branch — set pc_idx and loop
                        switch (cond) {
                        case 0: w.load_i32(S::ZFLAG); break;
                        case 1: w.load_i32(S::ZFLAG); w.op(op_i32_eqz); break;
                        case 2: w.load_i32(S::CFLAG); break;
                        case 3: w.load_i32(S::CFLAG); w.op(op_i32_eqz); break;
                        case 4: w.load_i32(S::NFLAG); break;
                        case 5: w.load_i32(S::NFLAG); w.op(op_i32_eqz); break;
                        case 6: w.load_i32(S::VFLAG); break;
                        case 7: w.load_i32(S::VFLAG); w.op(op_i32_eqz); break;
                        case 8: w.load_i32(S::CFLAG); w.load_i32(S::ZFLAG); w.op(op_i32_eqz); w.op(op_i32_and); break;
                        case 9: w.load_i32(S::CFLAG); w.op(op_i32_eqz); w.load_i32(S::ZFLAG); w.op(op_i32_or); break;
                        case 10: w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_eq); break;
                        case 11: w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_ne); break;
                        case 12: w.load_i32(S::ZFLAG); w.op(op_i32_eqz); w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_eq); w.op(op_i32_and); break;
                        case 13: w.load_i32(S::ZFLAG); w.load_i32(S::NFLAG); w.load_i32(S::VFLAG); w.op(op_i32_ne); w.op(op_i32_or); break;
                        default: w.bail_unsupported(insn_addr, insn_idx); insn_idx++; continue;
                        }
                        w.op(op_if); w.op(type_void);
                        w.i32_const(target_idx);
                        w.set_local(PC_IDX);
                        w.op(op_br); leb(result.body, 1); // br $loop
                        w.op(op_end);
                    }
                }
            } else if ((insn & 0xF800) == 0xE000) {
                // Unconditional branch B
                std::int16_t offset = static_cast<std::int16_t>((insn & 0x7FF) << 5) >> 5;
                std::uint32_t target = insn_addr + 4 + offset * 2;

                auto it = addr_to_idx.find(target);
                if (it != addr_to_idx.end() && it->second <= insn_idx) {
                    // Backward branch within block — set pc_idx and loop
                    w.i32_const(it->second);
                    w.set_local(PC_IDX);
                    w.op(op_br); leb(result.body, 1); // br $loop
                } else {
                    // Outside block — bail
                    w.bail(target, insn_idx + 1);
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
                    // Return to interpreter to handle PC change
                    w.bail(insn_addr, insn_idx + 1);
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
                // BX LR — function return
                w.bail(insn_addr + 2, insn_idx + 1);
            } else if ((insn & 0xFF00) == 0x4700) {
                // BX Rm / BLX Rm
                // Both bail to interpreter — BLX also sets LR
                int rm = (insn >> 3) & 0xF;
                if (insn & 0x80) {
                    // BLX Rm — set LR = next instruction | 1 (Thumb)
                    w.store_i32_const(S::LR, static_cast<std::int32_t>((insn_addr + 2) | 1));
                }
                w.load_reg(rm);
                w.set_local(TMP1);
                w.store_reg(15, TMP1);
                w.bail(insn_addr, insn_idx + 1);
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

            insn_idx++;
        }

        // End of loop and block
        w.op(op_end); // end loop
        w.op(op_end); // end block

        // Return total instruction count
        w.i32_const(num_insns);
        w.ret();

        tr.complete = !w.unsupported;
        return tr;
    }
}
