#include <drivers/audio/deterministic.h>
#include <common/deterministic.h>
#include <common/log.h>

#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace eka2l1::drivers {
    namespace {
        constexpr std::uint32_t output_rate = 48000;
        class pcm_stream;
        std::vector<pcm_stream *> streams;
        std::vector<std::int16_t> mix;
        std::ostringstream events;
        std::uint64_t output_frames = 0;
        unsigned next_id = 0;

        void render_until(std::uint64_t us);

        class pcm_stream final : public dsp_output_stream {
            std::deque<std::array<std::int16_t, 2>> queue_;
            std::uint64_t phase_ = 0;
            std::uint64_t played_frames_ = 0;
            bool playing_ = false;
            bool requested_ = false;
            unsigned id_ = next_id++;

            void sync() { render_until(common::benchmark::virtual_us.load()); }
            void record(const char *event, std::uint64_t value = 0) {
                if (common::benchmark::interactive) return;
                events << "{\"virtual_us\":" << common::benchmark::virtual_us.load()
                       << ",\"stream\":" << id_ << ",\"event\":\"" << event
                       << "\",\"value\":" << value << "}\n";
            }
        public:
            pcm_stream() {
                freq_ = 8000;
                channels_ = 1;
                format_ = PCM16_FOUR_CC_CODE;
                sync();
                streams.push_back(this);
                record("create");
            }
            ~pcm_stream() override {
                sync();
                record("destroy");
                streams.erase(std::find(streams.begin(), streams.end(), this));
            }
            bool format(four_cc fmt) override {
                if (fmt != PCM16_FOUR_CC_CODE && fmt != PCM8_FOUR_CC_CODE) return false;
                sync();
                if (!queue_.empty()) return fmt == format_;
                format_ = fmt;
                record("format", fmt);
                return true;
            }
            void get_supported_formats(std::vector<four_cc> &formats) override {
                formats.push_back(PCM16_FOUR_CC_CODE);
                formats.push_back(PCM8_FOUR_CC_CODE);
            }
            bool set_properties(std::uint32_t rate, std::uint8_t channels) override {
                // The shared DSP treats zero properties as an open request
                // using its defaults; games may configure the rate afterwards.
                if (!rate || !channels) return true;
                if (rate > 192000 || (channels != 1 && channels != 2)) {
                    LOG_ERROR(DRIVER_AUD, "Unsupported benchmark PCM properties: {} Hz, {} channels", rate, channels);
                    return false;
                }
                sync();
                if (!queue_.empty() && (rate != freq_ || channels != channels_)) return false;
                freq_ = rate;
                channels_ = channels;
                phase_ = 0;
                record("rate", rate);
                record("channels", channels);
                return true;
            }
            void volume(std::uint32_t value) override {
                sync();
                volume_ = std::min(value, max_volume());
                record("volume", volume_);
            }
            bool write(const std::uint8_t *data, std::uint32_t size) override {
                const auto bytes_per_sample = format_ == PCM16_FOUR_CC_CODE ? 2U : 1U;
                const auto stride = bytes_per_sample * channels_;
                if ((size && !data) || size % stride || queue_.size() + size / stride > freq_ * 10ULL) return false;
                sync();
                for (std::uint32_t offset = 0; offset < size; offset += stride) {
                    std::array<std::int16_t, 2> sample{};
                    for (unsigned ch = 0; ch < channels_; ++ch) {
                        const auto *p = data + offset + ch * bytes_per_sample;
                        const int raw = bytes_per_sample == 2 ? (p[0] | (p[1] << 8)) : p[0];
                        sample[ch] = bytes_per_sample == 2 ? (raw >= 32768 ? raw - 65536 : raw)
                            : (raw >= 128 ? raw - 256 : raw) * 256;
                    }
                    if (channels_ == 1) sample[1] = sample[0];
                    queue_.push_back(sample);
                }
                requested_ = false;
                record("write_bytes", size);
                return true;
            }
            bool start() override {
                sync();
                playing_ = true;
                requested_ = false;
                record("start");
                return true;
            }
            bool stop() override {
                sync();
                playing_ = false;
                requested_ = false;
                queue_.clear();
                phase_ = 0;
                record("stop");
                auto callback = complete_callback_;
                auto userdata = complete_userdata_;
                if (callback) callback(userdata);
                return true;
            }
            bool is_playing() const override { return playing_; }
            void reset_stat() override {
                sync();
                dsp_stream::reset_stat();
                played_frames_ = 0;
            }
            std::uint64_t position() override {
                sync();
                return played_frames_ * 1000000ULL / freq_;
            }
            std::uint64_t real_time_position() override { return position(); }
            std::array<std::int32_t, 2> render() {
                if (!playing_ || queue_.empty()) return {};
                std::array<std::int32_t, 2> result{queue_.front()[0] * static_cast<int>(volume_) / 10,
                    queue_.front()[1] * static_cast<int>(volume_) / 10};
                // Integer zero-order hold: identical resampling on all hosts.
                phase_ += freq_;
                while (phase_ >= output_rate) {
                    phase_ -= output_rate;
                    if (!queue_.empty()) {
                        queue_.pop_front();
                        ++played_frames_;
                        samples_played_ += channels_;
                        samples_copied_ += channels_;
                    }
                }
                return result;
            }
            void notify() {
                // Request the next guest buffer with 40 ms or less remaining.
                if (!playing_ || requested_ || queue_.size() > freq_ * 40ULL / 1000) return;
                requested_ = true;
                record("more_buffer");
                auto callback = more_buffer_callback_;
                auto userdata = more_buffer_userdata_;
                const auto id = id_;
                if (callback && !callback(userdata)) {
                    // Upstream callbacks may decline delivery. Retry next guest
                    // tick, but never touch a stream destroyed by the callback.
                    if (std::find(streams.begin(), streams.end(), this) != streams.end() && id_ == id)
                        requested_ = false;
                }
            }
        };

        void render_until(std::uint64_t us) {
            const auto target = us * output_rate / 1000000;
            if (target < output_frames || (!common::benchmark::interactive && us > 120000000))
                throw std::runtime_error("Invalid benchmark audio clock");
            while (output_frames < target) {
                std::array<std::int64_t, 2> sum{};
                for (auto *stream : streams) {
                    const auto sample = stream->render();
                    sum[0] += sample[0];
                    sum[1] += sample[1];
                }
                if (!common::benchmark::interactive) for (auto value : sum)
                    mix.push_back(static_cast<std::int16_t>(std::clamp<std::int64_t>(value, -32768, 32767)));
                ++output_frames;
            }
        }
    }

    std::unique_ptr<dsp_stream> new_benchmark_dsp_out_stream() { return std::make_unique<pcm_stream>(); }

    std::size_t benchmark_audio_buffered_frames() { return mix.size() / 2; }

    void reset_benchmark_audio() {
        if (!streams.empty()) throw std::runtime_error("Benchmark audio reset with live streams");
        output_frames = 0;
        next_id = 0;
        mix.clear();
        events.str("");
        events.clear();
    }

    void pump_benchmark_audio(std::uint64_t us) {
        render_until(us);
        const auto snapshot = streams;
        for (auto *stream : snapshot)
            if (std::find(streams.begin(), streams.end(), stream) != streams.end()) stream->notify();
    }

    void export_benchmark_audio(const std::string &directory, std::uint64_t us) {
        render_until(us); // Final partial block, without delivering new guest callbacks.
        std::ofstream wav(directory + "/audio.wav", std::ios::binary);
        auto le = [&wav](std::uint32_t value, int bytes) {
            for (int i = 0; i < bytes; ++i) wav.put(static_cast<char>((value >> (i * 8)) & 255));
        };
        const std::uint32_t bytes = static_cast<std::uint32_t>(mix.size() * 2);
        wav.write("RIFF", 4); le(36 + bytes, 4); wav.write("WAVEfmt ", 8);
        le(16, 4); le(1, 2); le(2, 2); le(output_rate, 4); le(output_rate * 4, 4);
        le(4, 2); le(16, 2); wav.write("data", 4); le(bytes, 4);
        for (auto sample : mix) le(static_cast<std::uint16_t>(sample), 2);
        wav.close();
        std::ofstream trace(directory + "/audio.jsonl");
        trace << events.str();
        trace.close();
        if (!wav || !trace) throw std::runtime_error("Failed to export benchmark audio");
    }
}
