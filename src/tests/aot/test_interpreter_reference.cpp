#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/dyncom/arm_dyncom_interpreter.h>

namespace eka2l1::arm {
    void dyncom_core::interpret_step_for_test() {
        publish_memory_view();
        ticks_executed_ = 0;
        state_->NumInstrsToExecute = 1;
        InterpreterMainLoop(state_.get(), ticks_executed_);
    }
}
