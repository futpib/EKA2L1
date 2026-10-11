/*
 * Copyright (c) 2026 EKA2L1 Team.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <drivers/audio/backend/player_pcm_wave.h>
#include <common/buffer.h>

#include <algorithm>
#include <cstring>

namespace eka2l1::drivers {
    namespace {
        std::uint32_t le32(const std::uint8_t *p) {
            return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
        }

        std::uint16_t le16(const std::uint8_t *p) {
            return p[0] | (std::uint16_t(p[1]) << 8);
        }
    }

    player_pcm_wave::player_pcm_wave(audio_driver *driver) : player_shared(driver) {
        freq_ = channels_ = 0;
        encoding_ = player_audio_encoding_pcm16;
        format_ = 0;
    }

    player_pcm_wave::~player_pcm_wave() {
        // A callback must finish before the derived PCM storage disappears.
        stop();
        output_stream_.reset();
    }

    bool player_pcm_wave::open_wave(common::ro_stream *stream) {
        stop();
        const std::lock_guard<std::mutex> guard(lock_);
        ready_ = false;
        pcm_.clear();
        reset_request();
        if (!stream || stream->size() < 12) return false;

        std::uint8_t header[12];
        if (stream->read(0, header, sizeof(header)) != sizeof(header)
            || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return false;
        const std::uint64_t end = std::uint64_t(le32(header + 4)) + 8;
        if (end < 12 || end > stream->size()) return false;

        std::uint16_t bits = 0, channels = 0;
        std::uint32_t frequency = 0, data_size = 0;
        std::uint64_t data_offset = 0;
        bool have_format = false, have_data = false;
        for (std::uint64_t offset = 12; offset < end;) {
            std::uint8_t chunk[8];
            if (end - offset < 8 || stream->read(offset, chunk, 8) != 8) return false;
            const auto size = le32(chunk + 4);
            const auto payload = offset + 8;
            if (size > end - payload) return false;
            if (!std::memcmp(chunk, "fmt ", 4)) {
                std::uint8_t fmt[16];
                if (have_format || size < 16 || stream->read(payload, fmt, 16) != 16 || le16(fmt) != 1) return false;
                channels = le16(fmt + 2);
                frequency = le32(fmt + 4);
                bits = le16(fmt + 14);
                if ((channels != 1 && channels != 2) || !frequency || frequency > 384000
                    || (bits != 8 && bits != 16) || le16(fmt + 12) != channels * (bits / 8)
                    || le32(fmt + 8) != frequency * channels * (bits / 8)) return false;
                have_format = true;
            } else if (!std::memcmp(chunk, "data", 4)) {
                if (have_data) return false;
                data_offset = payload;
                data_size = size;
                have_data = true;
            }
            const auto padded_size = std::uint64_t(size) + (size & 1);
            if (padded_size > end - payload) return false;
            offset = payload + padded_size;
        }
        if (!have_format || !have_data || data_size % (channels * (bits / 8))) return false;

        std::vector<std::uint8_t> encoded(data_size);
        if (data_size && stream->read(data_offset, encoded.data(), data_size) != data_size) return false;
        if (bits == 16) {
            pcm_ = std::move(encoded);
        } else {
            pcm_.resize(std::size_t(data_size) * 2);
            for (std::size_t i = 0; i < data_size; ++i) {
                const auto sample = static_cast<std::int16_t>((int(encoded[i]) - 128) * 256);
                std::memcpy(pcm_.data() + i * 2, &sample, 2);
            }
        }
        freq_ = frequency;
        channels_ = channels;
        ready_ = true;
        return true;
    }

    bool player_pcm_wave::open_url(const std::string &url) {
        common::ro_std_file_stream stream(url, true);
        return stream.valid() && open_wave(&stream);
    }

    bool player_pcm_wave::open_custom(common::rw_stream *stream) {
        return open_wave(stream);
    }

    void player_pcm_wave::reset_request() {
        next_byte_ = 0;
        data_pointer_ = 0;
        data_.clear();
        flags_ = 0;
    }

    void player_pcm_wave::get_more_data() {
        const auto count = std::min<std::size_t>(4096, pcm_.size() - next_byte_);
        data_.assign(pcm_.begin() + next_byte_, pcm_.begin() + next_byte_ + count);
        next_byte_ += count;
        data_pointer_ = 0;
        if (!count) flags_ |= 1;
    }

    bool player_pcm_wave::set_position_for_custom_format(const std::uint64_t pos_in_us) {
        if (!ready_) return false;
        reset_request();
        next_byte_ = std::min<std::uint64_t>(pcm_.size(),
            std::min(pos_in_us, duration()) * freq_ / 1000000 * channels_ * 2);
        return true;
    }

    std::uint64_t player_pcm_wave::duration() const {
        return ready_ ? frames_to_microseconds(pcm_.size() / (channels_ * 2), freq_) : 0;
    }
}
