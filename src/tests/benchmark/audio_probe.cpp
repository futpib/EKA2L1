// Identical fixtures exercise Qt's FFmpeg/shared DSP and WASM's PCM/shared DSP.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <drivers/audio/audio.h>
#include <drivers/audio/clocked.h>
#include <drivers/audio/dsp.h>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace eka2l1::drivers;
void require(bool ok) {
    if (!ok)
        throw std::runtime_error("audio fixture assertion");
}
int main(int argc, char **argv) {
#ifndef __EMSCRIPTEN__
    if (argc == 2) {
        // Play an exported 48 kHz stereo WAV through the ordinary Qt Cubeb driver.
        // Used with an isolated recording sink, never changes the desktop default.
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<char> wav((std::istreambuf_iterator<char>(file)), {});
        require(wav.size() > 44 && (wav.size() - 44) % 4 == 0);
        std::atomic<std::size_t> cursor{ 0 };
        const std::size_t frames = (wav.size() - 44) / 4;
        auto driver = make_audio_driver(audio_driver_backend::cubeb, 100);
        auto stream = driver->new_output_stream(48000, 2, [&](std::int16_t *out, std::size_t n) {
            auto start = cursor.load();
            for (std::size_t i = 0; i < n * 2; ++i) {
                const auto at = start * 4 + i * 2 + 44;
                out[i] = at + 1 < wav.size() ? std::int16_t(std::uint8_t(wav[at]) | (std::uint8_t(wav[at + 1]) << 8)) : 0;
            }
            cursor += n;
            return n;
        });
        require(stream && stream->set_volume(1) && stream->start());
        while (cursor < frames)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        stream->stop();
        stream.reset();
        std::puts("PASS: native Cubeb device playback");
        return 0;
    }
#endif
    for (unsigned rate : { 8000, 11025, 22050, 44100, 48000 })
        for (unsigned channels : { 1, 2 })
            for (unsigned bits : { 8, 16 }) {
                auto driver = make_clocked_audio_driver(false, true);
                auto base = new_dsp_out_stream(driver.get(), dsp_stream_backend_ffmpeg);
                auto &dsp = *static_cast<dsp_output_stream *>(base.get());
                require(dsp.format(bits == 8 ? PCM8_FOUR_CC_CODE : PCM16_FOUR_CC_CODE));
                require(dsp.set_properties(rate, channels));
                unsigned callbacks = 0, done = 0;
                dsp.register_callback(dsp_stream_notification_more_buffer, [&](void *) { ++callbacks; return callbacks != 1; }, nullptr);
                dsp.register_callback(dsp_stream_notification_done, [&](void *) { ++done; return true; }, nullptr);
                std::vector<std::uint8_t> input((rate / 20) * channels * (bits / 8));
                for (unsigned i = 0; i < input.size(); ++i)
                    input[i] = (i * 73 + i / 17) & 255;
                dsp.volume(5);
                require(dsp.write(input.data(), input.size()));
                require(dsp.start());
                std::array<std::int16_t, 960> pcm{};
                std::uint64_t hash = 1469598103934665603ULL;
                unsigned nonzero = 0;
                for (unsigned step = 1; step <= 100; ++step) {
                    if (step == 20) {
                        require(dsp.stop());
                        dsp.reset_stat();
                        require(dsp.write(input.data(), input.size()));
                        require(dsp.start());
                    }
                    if (step % 4 == 0 && step < 70)
                        require(dsp.write(input.data(), input.size()));
                    if (step == 35)
                        dsp.volume(0);
                    if (step == 40)
                        dsp.volume(10);
                    if (step == 50)
                        driver->master_volume(25);
                    pump_clocked_audio(step * 10000);
                    require(read_clocked_audio(pcm.data(), 480) == 480);
                    for (auto value : pcm) {
                        hash = (hash ^ std::uint16_t(value)) * 1099511628211ULL;
                        nonzero += value != 0;
                    }
                    if (step >= 35 && step < 40)
                        for (auto value : pcm)
                            require(value == 0);
                    if (channels == 1)
                        for (unsigned i = 0; i < 480; ++i)
                            require(pcm[i * 2] == pcm[i * 2 + 1]);
                }
                require(callbacks > 2 && done == 1 && nonzero > 0);
                const auto position = dsp.position();
                require(position == dsp.real_time_position());
                dsp.stop();
                base.reset();
                // A deleted stream is never called again. Silent sink remains clocked.
                pump_clocked_audio(1010000);
                require(read_clocked_audio(pcm.data(), 480) == 480);
                for (auto value : pcm)
                    require(value == 0);
                // Slow browser consumer: bounded queue, with explicit dropped-frame count.
                pump_clocked_audio(2010000);
                auto stats = clocked_audio_stats();
                require(stats.find("\"queued\":24000") != std::string::npos);
                require(stats.find("\"dropped\":24000") != std::string::npos);
                std::printf("%u/%u/%u %016llx callbacks=%u done=%u position=%llu nonzero=%u\n", rate, channels, bits,
                    (unsigned long long)hash, callbacks, done, (unsigned long long)position, nonzero);
            }
    // Independent known-value oracle for signed PCM8, not merely two builds
    // executing the same implementation and agreeing on the same mistake.
    {
        auto driver = make_clocked_audio_driver(false, true);
        auto base = new_dsp_out_stream(driver.get(), dsp_stream_backend_ffmpeg);
        auto &dsp = *static_cast<dsp_output_stream *>(base.get());
        require(dsp.format(PCM8_FOUR_CC_CODE) && dsp.set_properties(48000, 1));
        std::array<std::uint8_t, 480> input{};
        const std::array<int, 8> values{-128, -64, -1, 0, 1, 63, 64, 127};
        for (unsigned i = 0; i < input.size(); ++i) input[i] = values[i % 8];
        dsp.volume(10);
        require(dsp.write(input.data(), input.size()) && dsp.start());
        pump_clocked_audio(10000);
        std::array<std::int16_t, 960> output{};
        require(read_clocked_audio(output.data(), 480) == 480);
        for (unsigned i = 0; i < 480; ++i)
            require(output[i*2] == values[i%8]*256 && output[i*2+1] == values[i%8]*256);
    }
    // Two simultaneous loud streams saturate rather than wrap; removing one
    // stream cannot unregister the other or destroy its resampler.
    auto driver = make_clocked_audio_driver(false, true);
    auto tone = [](std::int16_t *out, std::size_t n) { for (std::size_t i=0;i<n*2;++i) out[i]=30000; return n; };
    auto a = driver->new_output_stream(48000, 2, tone), b = driver->new_output_stream(48000, 2, tone);
    require(a->start() && b->start());
    pump_clocked_audio(10000);
    std::array<std::int16_t, 960> pcm{};
    require(read_clocked_audio(pcm.data(), 480) == 480);
    for (auto value : pcm)
        require(value == 32767);
    a.reset();
    pump_clocked_audio(20000);
    read_clocked_audio(pcm.data(), 480);
    for (auto value : pcm)
        require(value == 30000);
    b->pause();
    pump_clocked_audio(30000);
    read_clocked_audio(pcm.data(), 480);
    for (auto value : pcm)
        require(value == 0);
    b.reset();
    std::puts("PASS: shared DSP rates/channels/PCM/volume/retry/stop/reset/mixing/queue");
}
