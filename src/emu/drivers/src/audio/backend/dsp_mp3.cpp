/* Copyright (c) 2026 EKA2L1 Team. Licensed under GPLv3 or later. */
#include <drivers/audio/backend/dsp_mp3.h>
#include <common/log.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include <minimp3.h>

#define OUTSIDE_SPEEX
#define RANDOM_PREFIX speex
#include <speex/speex_resampler.h>

#include <algorithm>
#include <cstring>

namespace eka2l1::drivers {
    struct dsp_output_stream_mp3::decoder {
        mp3dec_t mp3{};
        std::vector<std::uint8_t> queued;
        SpeexResamplerState *resampler = nullptr;
        unsigned input_rate = 0, output_rate = 0, output_channels = 0;

        decoder() { mp3dec_init(&mp3); }
        ~decoder() {
            if (resampler) speex_resampler_destroy(resampler);
        }
    };

    dsp_output_stream_mp3::dsp_output_stream_mp3(audio_driver *audio)
        : dsp_output_stream_shared(audio), decoder_(std::make_unique<decoder>()) {
        format_ = PCM16_FOUR_CC_CODE;
    }

    dsp_output_stream_mp3::~dsp_output_stream_mp3() {
        shutdown_stream();
    }

    bool dsp_output_stream_mp3::format(const four_cc value) {
        if (value != PCM16_FOUR_CC_CODE && value != PCM8_FOUR_CC_CODE && value != MP3_FOUR_CC_CODE) {
            LOG_WARN(DRIVER_AUD, "Unsupported DSP format 0x{:08x}", value);
            return false;
        }
        const std::lock_guard<std::mutex> guard(decode_lock_);
        decoder_ = std::make_unique<decoder>();
        format_ = value;
        return true;
    }

    void dsp_output_stream_mp3::get_supported_formats(std::vector<four_cc> &formats) {
        formats = { PCM16_FOUR_CC_CODE, PCM8_FOUR_CC_CODE, MP3_FOUR_CC_CODE };
    }

    void dsp_output_stream_mp3::queue_data_decode(const std::uint8_t *data, const std::size_t size) {
        const std::lock_guard<std::mutex> guard(decode_lock_);
        decoder_->queued.insert(decoder_->queued.end(), data, data + size);
    }

    bool dsp_output_stream_mp3::decode_data(std::vector<std::uint8_t> &dest) {
        const std::lock_guard<std::mutex> guard(decode_lock_);
        dest.clear();
        auto &dec = *decoder_;
        if (!freq_ || !channels_ || channels_ > 2) return false;
        while (!dec.queued.empty()) {
            const auto before = dec.mp3;
            mp3dec_frame_info_t info{};
            mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
            const int frames = mp3dec_decode_frame(&dec.mp3, dec.queued.data(),
                static_cast<int>(dec.queued.size()), pcm, &info);
            if (!info.hz) {
                // A guest buffer can end anywhere, even inside a frame header.
                // Keep the decoder reservoir and enough bytes to establish sync
                // when the next write arrives; temporary starvation is not EOF.
                dec.mp3 = before;
                if (dec.queued.size() > 16384)
                    dec.queued.erase(dec.queued.begin(), dec.queued.end() - 16384);
                return false;
            }
            dec.queued.erase(dec.queued.begin(), dec.queued.begin() + info.frame_bytes);
            if (!frames) continue;

            std::vector<std::int16_t> mixed(frames * channels_);
            for (int i = 0; i < frames; ++i) {
                if (info.channels == static_cast<int>(channels_)) {
                    for (unsigned c = 0; c < channels_; ++c) mixed[i * channels_ + c] = pcm[i * channels_ + c];
                } else if (channels_ == 1) {
                    mixed[i] = (static_cast<int>(pcm[i * 2]) + pcm[i * 2 + 1]) / 2;
                } else {
                    mixed[i * 2] = mixed[i * 2 + 1] = pcm[i];
                }
            }
            if (info.hz != static_cast<int>(freq_)) {
                if (dec.input_rate != info.hz || dec.output_rate != freq_ || dec.output_channels != channels_) {
                    if (dec.resampler) speex_resampler_destroy(dec.resampler);
                    int error = 0;
                    dec.resampler = speex_resampler_init(channels_, info.hz, freq_, 5, &error);
                    dec.input_rate = info.hz;
                    dec.output_rate = freq_;
                    dec.output_channels = channels_;
                }
                if (!dec.resampler) return false;
                spx_uint32_t input = frames;
                spx_uint32_t output = (static_cast<std::uint64_t>(frames) * freq_ + info.hz - 1) / info.hz + 64;
                std::vector<std::int16_t> converted(output * channels_);
                if (speex_resampler_process_interleaved_int(dec.resampler, mixed.data(), &input,
                        converted.data(), &output) != RESAMPLER_ERR_SUCCESS) return false;
                converted.resize(output * channels_);
                mixed.swap(converted);
            }
            dest.resize(mixed.size() * sizeof(std::int16_t));
            std::memcpy(dest.data(), mixed.data(), dest.size());
            return true;
        }
        return false;
    }

    bool dsp_output_stream_mp3::internal_decode_running_out() {
        if (format_ != MP3_FOUR_CC_CODE) return dsp_output_stream_shared::internal_decode_running_out();
        const std::lock_guard<std::mutex> guard(decode_lock_);
        return decoder_->queued.size() <= 16384;
    }

    bool dsp_output_stream_mp3::stop() {
        dsp_output_stream_shared::stop();
        const std::lock_guard<std::mutex> guard(decode_lock_);
        decoder_ = std::make_unique<decoder>();
        return true;
    }
}
