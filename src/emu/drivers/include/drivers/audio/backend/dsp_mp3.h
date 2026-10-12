/* Copyright (c) 2026 EKA2L1 Team. Licensed under GPLv3 or later. */
#pragma once

#include <drivers/audio/backend/dsp_shared.h>

namespace eka2l1::drivers {
    // Streaming PCM/MP3 output for hosts without FFmpeg.
    struct dsp_output_stream_mp3 final : public dsp_output_stream_shared {
    private:
        struct decoder;
        std::unique_ptr<decoder> decoder_;
        std::mutex decode_lock_;

        bool internal_decode_running_out() override;

    public:
        explicit dsp_output_stream_mp3(audio_driver *audio);
        ~dsp_output_stream_mp3() override;
        bool format(four_cc format) override;
        void get_supported_formats(std::vector<four_cc> &formats) override;
        void queue_data_decode(const std::uint8_t *data, std::size_t size) override;
        bool decode_data(std::vector<std::uint8_t> &dest) override;
        bool stop() override;
    };
}
