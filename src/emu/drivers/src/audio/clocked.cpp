#include <common/platform.h>
#if !EKA2L1_PLATFORM(IOS)
#include <algorithm>
#include <array>
#include <cubeb_resampler.h>
#include <deque>
#include <drivers/audio/clocked.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace eka2l1::drivers {
    namespace {
        // All stream lifecycle and render operations belong to the guest thread.
        // Only the bounded sink crosses to the browser/UI thread. A render
        // callback may queue guest notifications, but cannot destroy its stream.
        constexpr unsigned rate = 48000, quantum = 480, capacity = rate / 2;
        std::mutex sink_mutex;
        std::deque<std::array<std::int16_t, 2>> sink;
        std::uint64_t sink_dropped = 0, sink_read = 0;
        class clock_driver;
        clock_driver *active = nullptr;
        class clock_stream final : public audio_output_stream {
        public:
            clock_driver &owner;
            data_callback callback;
            cubeb_resampler *resampler = nullptr;
            bool playing = false, paused = false;
            float volume = 1;
            std::uint64_t position = 0;
            unsigned id;
            clock_stream(clock_driver &, unsigned, unsigned, data_callback);
            ~clock_stream() override;
            bool start() override {
                playing = true;
                paused = false;
                return true;
            }
            bool stop() override {
                playing = false;
                return true;
            }
            void pause() override { paused = true; }
            bool is_playing() override { return playing; }
            bool is_pausing() override { return paused; }
            bool set_volume(float v) override {
                volume = std::clamp(v, 0.0f, 1.0f);
                return true;
            }
            float get_volume() const override { return volume; }
            bool current_frame_position(std::uint64_t *p) override {
                if (!p)
                    return false;
                *p = position;
                return true;
            }
        };
        class clock_driver final : public audio_driver {
        public:
            bool retain, playback;
            unsigned next_id = 0;
            std::uint64_t frames = 0;
            std::vector<clock_stream *> streams;
            std::vector<std::int16_t> pcm;
            std::ostringstream events;
            clock_driver(bool r, bool p)
                : retain(r)
                , playback(p) {
                if (active)
                    throw std::runtime_error("Only one clocked audio device is supported");
                active = this;
                std::lock_guard<std::mutex> lock(sink_mutex);
                sink.clear();
                sink_dropped = sink_read = 0;
            }
            ~clock_driver() override { active = nullptr; }
            std::unique_ptr<audio_output_stream> new_output_stream(std::uint32_t r, std::uint8_t c, data_callback cb) override {
                if (!r || r > 192000 || (c != 1 && c != 2))
                    return nullptr;
                return std::make_unique<clock_stream>(*this, r, c, std::move(cb));
            }
            std::unique_ptr<audio_input_stream> new_input_stream(std::uint32_t, std::uint8_t, data_callback) override { return nullptr; }
            std::uint32_t native_sample_rate() override { return rate; }
            void pump(std::uint64_t us) {
                auto target = us * rate / 1000000;
                if (target < frames || (retain && us > 120000000))
                    throw std::runtime_error("Invalid clocked audio time");
                while (frames < target) {
                    const auto n = static_cast<unsigned>(std::min<std::uint64_t>(quantum, target - frames));
                    std::array<float, quantum * 2> sum{};
                    const auto current = streams;
                    for (auto *stream : current) {
                        if (std::find(streams.begin(), streams.end(), stream) == streams.end() || !stream->playing || stream->paused || suspending())
                            continue;
                        std::array<std::int16_t, quantum * 2> output{};
                        auto got = cubeb_resampler_fill(stream->resampler, nullptr, nullptr, output.data(), n);
                        if (got < 0 || got > n)
                            throw std::runtime_error("Clocked audio resampler failed");
                        const auto channels = stream->get_channels();
                        const float gain = stream->volume * master_volume() / 100.0f;
                        for (long i = 0; i < got; ++i)
                            for (unsigned ch = 0; ch < 2; ++ch)
                                sum[i * 2 + ch] += output[i * channels + (channels == 1 ? 0 : ch)] * gain;
                        if (retain)
                            events << "{\"virtual_us\":" << us << ",\"stream\":" << stream->id << ",\"event\":\"render\",\"value\":" << got << ",\"rate\":" << stream->get_sample_rate() << ",\"channels\":" << unsigned(channels) << ",\"gain\":" << gain << "}\n";
                    }
                    std::vector<std::array<std::int16_t, 2>> packet(n);
                    for (unsigned i = 0; i < n; ++i)
                        for (unsigned ch = 0; ch < 2; ++ch) {
                            auto sample = static_cast<std::int16_t>(std::clamp(sum[i * 2 + ch], -32768.0f, 32767.0f));
                            packet[i][ch] = sample;
                            if (retain)
                                pcm.push_back(sample);
                        }
                    if (playback) {
                        std::lock_guard<std::mutex> lock(sink_mutex);
                        for (auto value : packet) {
                            if (sink.size() == capacity) {
                                sink.pop_front();
                                ++sink_dropped;
                            }
                            sink.push_back(value);
                        }
                    }
                    frames += n;
                }
            }
        };
        clock_stream::clock_stream(clock_driver &d, unsigned r, unsigned c, data_callback cb)
            : audio_output_stream(&d, r, c)
            , owner(d)
            , callback(std::move(cb))
            , id(d.next_id++) {
            cubeb_stream_params params{};
            params.format = CUBEB_SAMPLE_S16LE;
            params.rate = rate;
            params.channels = c;
            params.layout = c == 1 ? CUBEB_LAYOUT_MONO : CUBEB_LAYOUT_STEREO;
            resampler = cubeb_resampler_create(nullptr, nullptr, &params, r, [](cubeb_stream *, void *user, const void *, void *out, long count) -> long {
                auto &self = *static_cast<clock_stream *>(user);
                const auto got = self.callback(static_cast<std::int16_t *>(out), count);
                self.position += got;
                return got;
            }, this, CUBEB_RESAMPLER_QUALITY_DESKTOP);
            if (!resampler)
                throw std::runtime_error("Unable to create clocked audio resampler");
            d.streams.push_back(this);
        }
        clock_stream::~clock_stream() {
            owner.streams.erase(std::find(owner.streams.begin(), owner.streams.end(), this));
            cubeb_resampler_destroy(resampler);
        }
    }
    audio_driver_instance make_clocked_audio_driver(bool retain, bool playback) { return std::make_unique<clock_driver>(retain, playback); }
    bool clocked_audio_active() { return active != nullptr; }
    void pump_clocked_audio(std::uint64_t us) {
        if (active)
            active->pump(us);
    }
    std::size_t read_clocked_audio(std::int16_t *out, std::size_t frames) {
        std::lock_guard<std::mutex> lock(sink_mutex);
        auto n = std::min(frames, sink.size());
        for (std::size_t i = 0; i < n; ++i) {
            out[i * 2] = sink.front()[0];
            out[i * 2 + 1] = sink.front()[1];
            sink.pop_front();
        }
        sink_read += n;
        return n;
    }
    std::string clocked_audio_stats() {
        std::lock_guard<std::mutex> lock(sink_mutex);
        return "{\"queued\":" + std::to_string(sink.size()) + ",\"dropped\":" + std::to_string(sink_dropped) + ",\"read\":" + std::to_string(sink_read) + "}";
    }
    void export_clocked_audio(const std::string &dir, std::uint64_t us) {
        if (!active || !active->retain)
            return;
        // Export must never deliver new guest callbacks after the final captured
        // instruction. Pad the unserviced partial 10 ms quantum with silence.
        const auto target = us * rate / 1000000;
        if (target < active->frames || us > 120000000)
            throw std::runtime_error("Invalid audio export time");
        active->pcm.resize(target * 2, 0);
        std::ofstream wav(dir + "/audio.wav", std::ios::binary);
        auto le = [&](std::uint32_t value, int bytes) {
            for (int i = 0; i < bytes; ++i) wav.put(char(value >> (i * 8)));
        };
        auto bytes = active->pcm.size() * 2;
        wav.write("RIFF", 4);
        le(36 + bytes, 4);
        wav.write("WAVEfmt ", 8);
        le(16, 4);
        le(1, 2);
        le(2, 2);
        le(rate, 4);
        le(rate * 4, 4);
        le(4, 2);
        le(16, 2);
        wav.write("data", 4);
        le(bytes, 4);
        for (auto value : active->pcm)
            le(std::uint16_t(value), 2);
        std::ofstream events(dir + "/audio.jsonl");
        events << active->events.str();
        std::ofstream meta(dir + "/audio-backend.json");
        meta << "{\"backend\":\"shared-dsp-cubeb-resampler\",\"rate\":48000,\"quantum\":480,\"quality\":\"desktop\"}\n";
        if (!wav || !events || !meta)
            throw std::runtime_error("Audio export failed");
    }
}

#else
#include <drivers/audio/clocked.h>
namespace eka2l1::drivers {
    audio_driver_instance make_clocked_audio_driver(bool, bool) { return nullptr; }
    bool clocked_audio_active() { return false; }
    void pump_clocked_audio(std::uint64_t) {}
    void export_clocked_audio(const std::string &, std::uint64_t) {}
    std::size_t read_clocked_audio(std::int16_t *, std::size_t) { return 0; }
    std::string clocked_audio_stats() { return "{}"; }
}
#endif
