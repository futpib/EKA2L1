/*
 * Copyright (c) 2020 EKA2L1 Team.
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

#include <common/deterministic.h>
#include <drivers/audio/deterministic.h>
#include <drivers/audio/backend/dsp_shared.h>
#include <drivers/audio/dsp.h>

#include <common/log.h>
#include <common/platform.h>

#if EKA2L1_HAS_FFMPEG
#include <drivers/audio/backend/ffmpeg/dsp_ffmpeg.h>
#endif

namespace eka2l1::drivers {
#if EKA2L1_PLATFORM(IOS) && !EKA2L1_HAS_FFMPEG
    struct dsp_output_stream_pcm final : public dsp_output_stream_shared {
        explicit dsp_output_stream_pcm(drivers::audio_driver *aud)
            : dsp_output_stream_shared(aud) {
            format(PCM16_FOUR_CC_CODE);
        }

        ~dsp_output_stream_pcm() override {
            // Stop the render callback before the vtable degrades to the abstract
            // base, so an in-flight data_callback() can't hit a pure virtual.
            shutdown_stream();
        }

        bool decode_data(std::vector<std::uint8_t> &dest) override {
            dest.clear();
            return false;
        }

        void queue_data_decode(const std::uint8_t *original, const std::size_t original_size) override {
            if (format_ == PCM8_FOUR_CC_CODE) {
                std::vector<std::int16_t> converted(original_size);
                for (std::size_t i = 0; i < original_size; ++i) {
                    converted[i] = static_cast<std::int16_t>(
                        static_cast<std::int8_t>(original[i])) << 8;
                }
                buffer_.push(reinterpret_cast<const std::uint16_t *>(converted.data()), converted.size());
                return;
            }

            buffer_.push(reinterpret_cast<const std::uint16_t *>(original), (original_size + 1) / 2);
        }

        bool format(const four_cc fmt) override {
            if ((fmt != PCM16_FOUR_CC_CODE) && (fmt != PCM8_FOUR_CC_CODE)) {
                LOG_WARN(DRIVER_AUD, "iOS DSP output only supports PCM formats for now");
                return false;
            }

            format_ = fmt;
            return true;
        }

        void get_supported_formats(std::vector<four_cc> &cc_list) override {
            cc_list = { PCM16_FOUR_CC_CODE, PCM8_FOUR_CC_CODE };
        }
    };
#endif

    dsp_stream::dsp_stream()
        : samples_played_(0)
        , samples_copied_(0)
        , freq_(0)
        , channels_(0)
        , complete_callback_(nullptr)
        , more_buffer_callback_(nullptr)
        , complete_userdata_(nullptr)
        , more_buffer_userdata_(nullptr) {
    }

    dsp_output_stream::dsp_output_stream()
        : dsp_stream()
        , volume_(10) {
    }

    void dsp_stream::reset_stat() {
        samples_played_.store(0, std::memory_order_relaxed);
        samples_copied_.store(0, std::memory_order_relaxed);
    }

    void dsp_stream::register_callback(dsp_stream_notification_type nof_type, dsp_stream_notification_callback callback,
        void *userdata) {
        const std::lock_guard<std::mutex> guard(callback_lock_);

        switch (nof_type) {
        case dsp_stream_notification_done:
            complete_callback_ = callback;
            complete_userdata_ = userdata;
            break;

        case dsp_stream_notification_more_buffer:
            more_buffer_userdata_ = userdata;
            more_buffer_callback_ = callback;
            break;

        default:
            LOG_ERROR(DRIVER_AUD, "Unsupport notification type!");
            break;
        }
    }

    void *dsp_stream::get_userdata(dsp_stream_notification_type nof_type) {
        switch (nof_type) {
        case dsp_stream_notification_done:
            return complete_userdata_;

        case dsp_stream_notification_more_buffer:
            return more_buffer_userdata_;

        default:
            LOG_ERROR(DRIVER_AUD, "Unsupport notification type!");
            break;
        }

        return nullptr;
    }


    struct noop_dsp_output_stream : public dsp_output_stream {
        bool write(const std::uint8_t *, const std::uint32_t) override { return true; }
        bool set_properties(const std::uint32_t freq, const std::uint8_t channels) override {
            freq_ = freq;
            channels_ = channels;
            return true;
        }
        void get_supported_formats(std::vector<four_cc> &cc_list) override {
            cc_list.push_back(PCM16_FOUR_CC_CODE);
        }
        bool start() override { return true; }
        bool stop() override { return true; }
        std::uint64_t position() override { return 0; }
        std::uint64_t real_time_position() override { return 0; }
    };

    struct noop_dsp_input_stream : public dsp_input_stream {
        bool read(std::uint8_t *, const std::uint32_t) override { return true; }
        bool set_properties(const std::uint32_t freq, const std::uint8_t channels) override {
            freq_ = freq;
            channels_ = channels;
            return true;
        }
        void get_supported_formats(std::vector<four_cc> &cc_list) override {
            cc_list.push_back(PCM16_FOUR_CC_CODE);
        }
        bool start() override { return true; }
        bool stop() override { return true; }
        std::uint64_t position() override { return 0; }
        std::uint64_t real_time_position() override { return 0; }
    };

    std::unique_ptr<dsp_stream> new_dsp_out_stream(drivers::audio_driver *aud, const dsp_stream_backend dsp_backend) {
        if (common::benchmark::enabled()) return new_benchmark_dsp_out_stream();
#if !EKA2L1_PLATFORM(EMSCRIPTEN)
        switch (dsp_backend) {
        case dsp_stream_backend_ffmpeg:
#if EKA2L1_HAS_FFMPEG
            return std::make_unique<dsp_output_stream_ffmpeg>(aud);
#elif EKA2L1_PLATFORM(IOS)
            return std::make_unique<dsp_output_stream_pcm>(aud);
#else
            break;
#endif

        default:
            break;
        }
#endif

        return std::make_unique<noop_dsp_output_stream>();
    }

    std::unique_ptr<dsp_stream> new_dsp_in_stream(drivers::audio_driver *aud, const dsp_stream_backend dsp_backend) {
        if (common::benchmark::enabled()) return std::make_unique<noop_dsp_input_stream>();
#if !EKA2L1_PLATFORM(EMSCRIPTEN)
        switch (dsp_backend) {
        case dsp_stream_backend_ffmpeg:
            return std::make_unique<dsp_input_stream_shared>(aud);

        default:
            break;
        }
#endif

        return std::make_unique<noop_dsp_input_stream>();
    }
}
