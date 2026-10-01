#pragma once
namespace eka2l1::common::native_profile {
    // Opt-in Linux/x86-64, guest-thread wall-clock PC sampling. No sampling or
    // signal changes unless EKA2L1_NATIVE_SAMPLE_OUTPUT is explicitly set.
    void start();
    void stop();
}
