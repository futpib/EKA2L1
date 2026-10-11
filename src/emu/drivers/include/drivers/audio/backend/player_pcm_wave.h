/*
 * Copyright (c) 2026 EKA2L1 Team.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <drivers/audio/backend/player_shared.h>

namespace eka2l1::common {
    class ro_stream;
}

namespace eka2l1::drivers {
    // The shared player owns playback, repeats and drain notifications. This
    // backend only reads uncompressed RIFF/WAVE into signed 16-bit PCM.
    struct player_pcm_wave final : public player_shared {
    private:
        std::vector<std::uint8_t> pcm_;
        std::size_t next_byte_ = 0;
        bool ready_ = false;

        bool is_ready_to_play() override { return ready_; }

    public:
        explicit player_pcm_wave(audio_driver *driver);
        ~player_pcm_wave() override;

        bool open_wave(common::ro_stream *stream);
        bool open_url(const std::string &url) override;
        bool open_custom(common::rw_stream *stream) override;
        bool make_backend_source() override { return ready_; }
        void reset_request() override;
        void get_more_data() override;
        bool set_position_for_custom_format(std::uint64_t pos_in_us) override;
        bool crop() override { return false; }
        bool record() override { return false; }
        bool set_dest_freq(std::uint32_t freq) override { return ready_ && freq == freq_; }
        bool set_dest_channel_count(std::uint32_t channels) override { return ready_ && channels == channels_; }
        bool set_dest_encoding(std::uint32_t enc) override { return enc == player_audio_encoding_pcm16; }
        std::uint64_t duration() const override;
        player_type get_player_type() const override { return player_type_pcm_wave; }
    };
}
