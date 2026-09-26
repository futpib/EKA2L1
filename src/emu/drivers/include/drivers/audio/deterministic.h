#pragma once

#include <drivers/audio/dsp.h>
#include <string>

namespace eka2l1::drivers {
    // Single guest-thread producer. Export only after the guest is paused at a
    // synchronized presentation. Device callbacks never drive this clock.
    void reset_benchmark_audio();
    void pump_benchmark_audio(std::uint64_t virtual_us);
    void export_benchmark_audio(const std::string &directory, std::uint64_t virtual_us);
    std::unique_ptr<dsp_stream> new_benchmark_dsp_out_stream();
}
