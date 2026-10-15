#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/watchdog.h>
#include <cpu/aot/svc_return.h>
#include <cpu/aot/memory_experiment.h>
#include <cpu/dyncom/armstate.h>
#include <cpu/arm_interface.h>
#include <cpu/12l1r/exclusive_monitor.h>
#include <cstdio>
#include <stdexcept>

namespace eka2l1::arm::aot {
#ifdef __EMSCRIPTEN__
static void load_flags(ARMul_State *cpu) {
    cpu->NFlag = cpu->Cpsr >> 31;
    cpu->ZFlag = (cpu->Cpsr >> 30) & 1;
    cpu->CFlag = (cpu->Cpsr >> 29) & 1;
    cpu->VFlag = (cpu->Cpsr >> 28) & 1;
    cpu->TFlag = (cpu->Cpsr >> 5) & 1;
}

void run_compiled_step(ARMul_State *cpu) {
    load_flags(cpu);
    cpu->Reg[15] &= cpu->TFlag ? ~1u : ~3u;
    cpu->aot_tlb = cpu->parent()->experimental_pointer;
    auto function = compile_single_instruction(cpu);
    if (function) execute_single(cpu, function);
    else cpu->RaiseException(exception_type_undefined_inst, cpu->Reg[15]);
    if ((cpu->aot_exit & (svc_pending | svc_taken)) == (svc_pending | svc_taken)) {
        cpu->PublishMemoryCallbackCpsr();
        cpu->RaiseSystemCall(cpu->aot_exit & 0x00ffffffu);
        cpu->exmonitor()->clear_exclusive();
        load_flags(cpu);
    }
    cpu->aot_exit = 0;
    cpu->PublishMemoryCallbackCpsr();
    cpu->NumInstrsToExecute = 0;
}

bool run_compiled_cpu(ARMul_State *cpu) {
    instantiate_staged_modules();
    load_flags(cpu);
    bool dispatch_boundary = true;
    bool dispatched = false;
    while (cpu->NumInstrsToExecute && !watchdog::requested()) {
        if (dispatch_boundary && !cpu->NirqSig && !(cpu->Cpsr & 0x80)) break;
        cpu->Reg[15] &= cpu->TFlag ? ~1u : ~3u;
        auto function = lookup_compiled(cpu);
        if (!function) function = compile_missing(cpu);
        if (!function) function = compile_single_instruction(cpu);
        if (!function) {
            const auto opcode = cpu->ReadCode(cpu->Reg[15] & ~3u);
            if (!cpu->NumInstrsToExecute) break;
            std::fprintf(stderr, "AOT-only: compiler rejected PC=0x%08x opcode=0x%08x Thumb=%u\n", cpu->Reg[15], opcode, cpu->TFlag);
            throw std::runtime_error("AOT-only execution could not compile guest instruction");
        }
        // Keep compilation-only yields responsive without charging guest time.
        // The installed function is ready for the next CPU slice.
        if (watchdog::requested()) break;
        dispatched = true;
        auto progress = execute_chain(cpu, function).progress;
        if (!progress && !(cpu->aot_exit & svc_pending) && cpu->NumInstrsToExecute && !watchdog::requested()) {
            function = compile_single_instruction(cpu);
            if (function) progress = execute_single(cpu, function);
        }
        dispatch_boundary = true;
        if (cpu->aot_exit & svc_pending) {
            const auto request = cpu->aot_exit;
            cpu->aot_exit = 0;
            const auto current_pc = cpu->Reg[15];
            const auto previous_thumb = cpu->TFlag;
            const auto return_lr = cpu->Reg[14];
            if (request & svc_taken) {
                cpu->PublishMemoryCallbackCpsr();
                cpu->RaiseSystemCall(request & 0x00ffffffu);
                cpu->exmonitor()->clear_exclusive();
                load_flags(cpu);
                if (current_pc != cpu->Reg[15]) continue;
            }
            if ((request & svc_page_end) || !cpu->NumInstrsToExecute) continue;
            if (previous_thumb != cpu->TFlag) {
                // Preserve the already decoded exchange immediately after SVC.
                function = compile_single_instruction(cpu, previous_thumb);
                if (!function || !execute_single(cpu, function))
                    throw std::runtime_error("AOT-only syscall continuation could not compile");
                cpu->PublishMemoryCallbackCpsr();
                continue;
            }
            if (request & svc_return) {
                const auto *memory = reinterpret_cast<const memory_experiment::direct_view *>(cpu->parent()->experimental_pointer);
                if (complete_svc_return(cpu, memory, request, current_pc, return_lr)) {
                    cpu->PublishMemoryCallbackCpsr();
                    continue;
                }
            }
            dispatch_boundary = false;
            continue;
        }
        if (!progress && cpu->NumInstrsToExecute && !watchdog::requested()) {
            const auto opcode = cpu->ReadCode(cpu->Reg[15] & ~3u);
            if (!cpu->NumInstrsToExecute) break;
            std::fprintf(stderr, "AOT-only: generated instruction made no progress PC=0x%08x opcode=0x%08x Thumb=%u exit=%u IRQ=%u CPSR=%x\n", cpu->Reg[15], opcode, cpu->TFlag, cpu->aot_exit, cpu->NirqSig, cpu->Cpsr);
            throw std::runtime_error("AOT-only execution requires a compiled recovery path");
        }
        cpu->PublishMemoryCallbackCpsr();
    }
    cpu->PublishMemoryCallbackCpsr();
    cpu->NumInstrsToExecute = 0;
    return dispatched;
}
#endif
}
