#pragma once
#include <drivers/audio/audio.h>
#include <string>
namespace eka2l1::drivers {
    // Guest-thread clocked device using the ordinary Qt DSP callbacks and Cubeb
    // resampler. Browser reads are a bounded sink, never the guest clock source.
    audio_driver_instance make_clocked_audio_driver(bool retain, bool playback);
    bool clocked_audio_active();
    void pump_clocked_audio(std::uint64_t us);
    void export_clocked_audio(const std::string &directory, std::uint64_t us);
    std::size_t read_clocked_audio(std::int16_t *out, std::size_t frames);
    std::string clocked_audio_stats();
}
