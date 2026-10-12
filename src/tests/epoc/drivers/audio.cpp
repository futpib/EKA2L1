/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <catch2/catch.hpp>
#include <drivers/audio/backend/dsp_shared.h>
#include <drivers/audio/stream.h>
#include <drivers/audio/backend/player_pcm_wave.h>
#include <common/buffer.h>

namespace {
    std::vector<std::uint8_t> pcm_wave(const std::uint16_t bits, const std::uint16_t channels,
        const std::vector<std::uint8_t> &samples) {
        std::vector<std::uint8_t> bytes;
        auto text = [&](const char *s) { bytes.insert(bytes.end(), s, s + 4); };
        auto word = [&](std::uint32_t value, unsigned size) {
            for (unsigned i = 0; i < size; ++i) bytes.push_back(value >> (i * 8));
        };
        text("RIFF"); word(36 + samples.size() + (samples.size() & 1), 4); text("WAVE");
        text("fmt "); word(16, 4); word(1, 2); word(channels, 2);
        word(8000, 4); word(8000 * channels * bits / 8, 4);
        word(channels * bits / 8, 2); word(bits, 2);
        text("data"); word(samples.size(), 4);
        bytes.insert(bytes.end(), samples.begin(), samples.end());
        if (samples.size() & 1) bytes.push_back(0);
        return bytes;
    }

    bool open_wave(eka2l1::drivers::player_pcm_wave &player, std::vector<std::uint8_t> bytes) {
        eka2l1::common::ro_buf_stream stream(bytes.data(), bytes.size());
        return player.open_wave(&stream);
    }
}

TEST_CASE("PCM WAVE player accepts empty initialization media and decodes real samples", "[audio][wave]") {
    eka2l1::drivers::player_pcm_wave player(nullptr);
    REQUIRE(open_wave(player, pcm_wave(16, 1, {})));
    REQUIRE(player.duration() == 0);
    std::int16_t decoded[8]{};
    REQUIRE(player.data_supply_callback(decoded, 8) == 0);

    REQUIRE(open_wave(player, pcm_wave(8, 2, {0, 128, 255, 64})));
    REQUIRE(player.duration() == 250);
    REQUIRE(player.get_dest_channel_count() == 2);
    REQUIRE(player.data_supply_callback(decoded, 8) == 2);
    REQUIRE(decoded[0] == -32768);
    REQUIRE(decoded[1] == 0);
    REQUIRE(decoded[2] == 32512);
    REQUIRE(decoded[3] == -16384);

    REQUIRE(open_wave(player, pcm_wave(16, 1, {0, 128, 255, 127})));
    REQUIRE(player.data_supply_callback(decoded, 1) == 1);
    REQUIRE(decoded[0] == -32768);
    REQUIRE(player.data_supply_callback(decoded, 8) == 1);
    REQUIRE(decoded[0] == 32767);
    REQUIRE(player.data_supply_callback(decoded, 8) == 0);
    player.set_position(125);
    REQUIRE(player.data_supply_callback(decoded, 8) == 1);
    REQUIRE(decoded[0] == 32767);
    player.reset_request();
    player.set_repeat(1, 0);
    REQUIRE(player.data_supply_callback(decoded, 8) == 4);
    REQUIRE(decoded[2] == -32768);
    REQUIRE(decoded[3] == 32767);
}

TEST_CASE("PCM WAVE parsing honors chunk bounds, padding and encoding", "[audio][wave]") {
    eka2l1::drivers::player_pcm_wave player(nullptr);
    auto valid = pcm_wave(8, 1, {128});
    REQUIRE(open_wave(player, valid));
    for (std::size_t length = 0; length < valid.size(); ++length) {
        auto truncated = valid;
        truncated.resize(length);
        REQUIRE_FALSE(open_wave(player, truncated));
    }
    auto bad = valid;
    bad[20] = 7; // Mu-law is a different codec.
    REQUIRE_FALSE(open_wave(player, bad));
    bad = valid; bad[40] = 0xff; // Data size exceeds its RIFF container.
    REQUIRE_FALSE(open_wave(player, bad));
    REQUIRE_FALSE(open_wave(player, pcm_wave(16, 2, {0, 0})));
    REQUIRE_FALSE(open_wave(player, pcm_wave(8, 0, {})));

    // Unknown odd-sized chunks are skipped with their required pad byte.
    auto padded = pcm_wave(16, 1, {});
    const std::vector<std::uint8_t> junk{'J','U','N','K',1,0,0,0,42,0};
    padded.insert(padded.begin() + 12, junk.begin(), junk.end());
    padded[4] += junk.size();
    REQUIRE(open_wave(player, padded));
}

TEST_CASE("Audio frame positions use frames rather than channel samples", "[audio]") {
    using eka2l1::drivers::frames_to_microseconds;

    REQUIRE(frames_to_microseconds(48000, 48000) == 1000000);
    REQUIRE(frames_to_microseconds(22050, 44100) == 500000);
    REQUIRE(frames_to_microseconds(1234, 0) == 0);
}

namespace {
    // Only the counters matter here; nothing reaches a driver.
    struct counting_output_stream : public eka2l1::drivers::dsp_output_stream_shared {
        explicit counting_output_stream(const std::uint32_t freq, const std::uint8_t channels,
            const std::size_t samples)
            : dsp_output_stream_shared(nullptr) {
            freq_ = freq;
            channels_ = channels;
            samples_played_.store(samples);
        }

        ~counting_output_stream() override {
            shutdown_stream();
        }

        bool decode_data(std::vector<std::uint8_t> &) override {
            return false;
        }

        void queue_data_decode(const std::uint8_t *, const std::size_t) override {
        }

        void get_supported_formats(std::vector<eka2l1::drivers::four_cc> &) override {
        }
    };
}

// CMdaAudioOutputStream::Position() is "the current position within the stream in
// microseconds" (mdaaudiooutputstream.h), so a second of audio is a second whatever
// the channel count, and a second of 16-bit stereo at 16 kHz is 64000 bytes.
TEST_CASE("Stream position is playback time rather than channel samples", "[audio]") {
    counting_output_stream mono(16000, 1, 16000);
    counting_output_stream stereo(16000, 2, 32000);

    REQUIRE(mono.position() == 1000000);
    REQUIRE(stereo.position() == 1000000);

    REQUIRE(mono.bytes_rendered() == 32000);
    REQUIRE(stereo.bytes_rendered() == 64000);

    counting_output_stream unconfigured(0, 0, 0);
    REQUIRE(unconfigured.position() == 0);
}

#ifdef EKA2L1_HAS_FFMPEG
#include <common/buffer.h>
#include <drivers/audio/audio.h>
#include <drivers/audio/backend/ffmpeg/player_ffmpeg.h>

namespace {
    struct memory_audio_stream : eka2l1::common::rw_stream {
        std::vector<std::uint8_t> data = std::vector<std::uint8_t>(65536, 0);
        eka2l1::common::ro_buf_stream reader{data.data(), data.size()};
        std::uint64_t read(void *buffer, const std::uint64_t size) override {
            return reader.read(buffer, size);
        }
        void seek(const std::int64_t amount, eka2l1::common::seek_where where) override {
            reader.seek(amount, where);
        }
        std::uint64_t tell() override { return reader.tell(); }
        std::uint64_t left() override { return reader.left(); }
        std::uint64_t size() override { return data.size(); }
    };

    struct queued_audio_output : eka2l1::drivers::audio_output_stream {
        eka2l1::drivers::data_callback source;
        std::uint64_t frames = 0;
        bool playing = false;
        bool paused = false;
        bool settle_on_stop = false;
        float volume = 1.0f;

        queued_audio_output(eka2l1::drivers::audio_driver *driver, std::uint32_t rate,
            std::uint8_t channels, eka2l1::drivers::data_callback callback)
            : audio_output_stream(driver, rate, channels), source(std::move(callback)) {}

        bool start() override { playing = true; paused = false; return true; }
        bool stop() override {
            if (settle_on_stop) {
                settle_on_stop = false;
                queue(16);
                drain();
            }
            playing = false;
            paused = false;
            return true;
        }
        void pause() override { paused = true; }
        bool is_playing() override { return playing; }
        bool is_pausing() override { return paused; }
        bool set_volume(float value) override { volume = value; return true; }
        float get_volume() const override { return volume; }
        bool current_frame_position(std::uint64_t *result) override { *result = frames; return true; }

        std::size_t queue(std::size_t count) {
            std::vector<std::int16_t> buffer(count * channels);
            const auto supplied = source(buffer.data(), count);
            frames += supplied;
            return supplied;
        }

        void drain() {
            playing = false;
            if (drained_callback_) drained_callback_();
        }
    };

    struct queued_audio_driver : eka2l1::drivers::audio_driver {
        queued_audio_output *output = nullptr;
        std::unique_ptr<eka2l1::drivers::audio_output_stream> new_output_stream(std::uint32_t rate,
            std::uint8_t channels, eka2l1::drivers::data_callback callback) override {
            auto result = std::make_unique<queued_audio_output>(this, rate, channels, std::move(callback));
            output = result.get();
            return result;
        }
        std::unique_ptr<eka2l1::drivers::audio_input_stream> new_input_stream(std::uint32_t,
            std::uint8_t, eka2l1::drivers::data_callback) override { return nullptr; }
        std::uint32_t native_sample_rate() override { return 8000; }
    };
}

TEST_CASE("Clip completion follows output drain and resets its position", "[audio]") {
    memory_audio_stream source;
    const std::uint8_t wav[] = {
        'R', 'I', 'F', 'F', 40, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
        0x40, 0x1F, 0, 0, 0x40, 0x1F, 0, 0, 1, 0, 8, 0,
        'd', 'a', 't', 'a', 4, 0, 0, 0, 0, 128, 255, 128
    };
    std::copy(std::begin(wav), std::end(wav), source.data.begin());
    queued_audio_driver driver;
    eka2l1::drivers::player_ffmpeg player(&driver);
    REQUIRE(player.open_custom(&source));
    int completions = 0;
    std::uint64_t position_on_completion = 1;
    REQUIRE(player.notify_any_done([&](std::uint8_t *) {
        ++completions;
        position_on_completion = player.position();
    }, nullptr, 0));
    REQUIRE(player.play());
    REQUIRE(driver.output->queue(16) == 4);
    REQUIRE(completions == 0);
    REQUIRE(player.position() == 500);

    SECTION("Drained notification occurs once, before the next playback") {
        driver.output->drain();
        REQUIRE(completions == 1);
        REQUIRE(position_on_completion == 0);
        REQUIRE(player.position() == 0);
        REQUIRE_FALSE(player.is_playing());
        driver.output->drain();
        REQUIRE(completions == 1);
        REQUIRE(player.play());
        REQUIRE(player.position() == 0);
        REQUIRE(driver.output->queue(16) == 4);
        REQUIRE(completions == 1);
        driver.output->drain();
        REQUIRE(completions == 2);
    }

    SECTION("Pause preserves position and stop cancels completion") {
        auto *output = driver.output;
        player.pause();
        REQUIRE(player.position() == 500);
        REQUIRE(player.play());
        REQUIRE(driver.output == output);
        REQUIRE(player.position() == 500);
        output->settle_on_stop = true;
        REQUIRE(player.stop());
        REQUIRE(player.position() == 0);
        output->drain();
        REQUIRE(completions == 0);
    }

    SECTION("Reopening waits for callbacks before replacing the decoder") {
        driver.output->settle_on_stop = true;
        SECTION("Custom stream") {
            REQUIRE(player.open_custom(&source));
        }
        SECTION("File") {
            REQUIRE(player.open_url("audioassets/padded-silence.mp3"));
        }
        REQUIRE(completions == 0);
        REQUIRE(player.position() == 0);
        REQUIRE(player.play());
        REQUIRE(driver.output->queue(16) > 0);
    }

    SECTION("Short repeats fill the output before it drains") {
        player.set_repeat(2, 0);
        REQUIRE(player.play());
        REQUIRE(driver.output->queue(16) == 12);
        REQUIRE(completions == 0);
        driver.output->drain();
        REQUIRE(completions == 1);
    }
}

TEST_CASE("Invalid custom audio survives probing and repeated cleanup", "[audio]") {
    memory_audio_stream stream;
    eka2l1::drivers::player_ffmpeg player(nullptr);
    REQUIRE_FALSE(player.open_custom(&stream));
    stream.seek(0, eka2l1::common::seek_where::beg);
    REQUIRE_FALSE(player.open_custom(&stream));
}

TEST_CASE("MP3 playback preserves the full Symbian frame timeline", "[audio]") {
    // CMP3AudioControllerUtility counts 1152 samples per MPEG-1 frame, without LAME trimming.
    // This generated silent clip has five frames and 4410 samples before encoder padding.
    eka2l1::drivers::player_ffmpeg player(nullptr);
    REQUIRE(player.open_url("audioassets/padded-silence.mp3"));
    REQUIRE(player.duration() == 5 * 1152 * 1000000LL / 44100);
    std::vector<std::int16_t> output(6000 * 2);
    REQUIRE(player.data_supply_callback(output.data(), 6000) == 5 * 1152);
}

TEST_CASE("Closing a playing clip quiesces callbacks before decoder destruction", "[audio]") {
    queued_audio_driver driver;
    int completions = 0;
    {
        eka2l1::drivers::player_ffmpeg player(&driver);
        REQUIRE(player.open_url("audioassets/padded-silence.mp3"));
        REQUIRE(player.notify_any_done([&](std::uint8_t *) { ++completions; }, nullptr, 0));
        REQUIRE(player.play());
        REQUIRE(driver.output->queue(16) == 16);
        driver.output->settle_on_stop = true;
    }
    REQUIRE(completions == 0);
}

TEST_CASE("EPOC Record custom audio decodes and can be reopened", "[audio]") {
    memory_audio_stream stream;
    auto word = [&](std::size_t offset, std::uint32_t value) {
        for (int i = 0; i < 4; ++i) stream.data[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
    };
    word(0, 0x10000037); word(4, 0x1000006d); word(8, 0x1000007e);
    word(12, 0x5508accf); word(16, 20); stream.data[20] = 4;
    word(21, 0x10000052); word(25, 52); word(29, 0x10000089); word(33, 37);
    word(52, 4000); word(56, 0); word(60, 0); word(64, 0); word(68, 4000);
    std::fill(stream.data.begin() + 72, stream.data.begin() + 4072, 0xd5);
    eka2l1::drivers::player_ffmpeg player(nullptr);
    REQUIRE(player.open_custom(&stream));
    REQUIRE(player.duration() == 500000);
    REQUIRE(player.open_custom(&stream));
    REQUIRE(player.duration() == 500000);
    player.set_repeat(1, 1000);
    std::vector<std::int16_t> output(4500, -1);
    REQUIRE(player.data_supply_callback(output.data(), output.size()) == output.size());
    REQUIRE(std::all_of(output.begin(), output.begin() + 4000, [](auto sample) { return sample == 8; }));
    REQUIRE(std::all_of(output.begin() + 4000, output.begin() + 4008, [](auto sample) { return sample == 0; }));
    REQUIRE(std::all_of(output.begin() + 4008, output.end(), [](auto sample) { return sample == 8; }));
}

TEST_CASE("PCM encoder accepts unrestricted sample rates and channel layouts", "[audio]") {
    eka2l1::drivers::player_ffmpeg player(nullptr);
    REQUIRE(player.set_dest_encoding(eka2l1::drivers::AUDIO_PCM16_CODEC_4CC));
    REQUIRE(player.set_dest_freq(8000));
    REQUIRE(player.set_dest_channel_count(1));
    REQUIRE(player.get_dest_freq() == 8000);
    REQUIRE(player.get_dest_channel_count() == 1);
    REQUIRE(player.set_dest_freq(44100));
    REQUIRE(player.set_dest_channel_count(2));
    REQUIRE(player.get_dest_freq() == 44100);
    REQUIRE(player.get_dest_channel_count() == 2);
    REQUIRE_FALSE(player.set_dest_freq(0));
    REQUIRE_FALSE(player.set_dest_channel_count(0));
}

TEST_CASE("8-bit PCM WAV uses unsigned samples with silence at 128", "[audio]") {
    memory_audio_stream stream;
    const std::uint8_t wav[] = {
        'R', 'I', 'F', 'F', 40, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
        0x40, 0x1F, 0, 0, 0x40, 0x1F, 0, 0, 1, 0, 8, 0,
        'd', 'a', 't', 'a', 4, 0, 0, 0, 0, 128, 255, 128
    };
    std::copy(std::begin(wav), std::end(wav), stream.data.begin());
    eka2l1::drivers::player_ffmpeg player(nullptr);
    REQUIRE(player.open_custom(&stream));
    std::vector<std::int16_t> output(4);
    REQUIRE(player.data_supply_callback(output.data(), output.size()) == output.size());
    REQUIRE(output == std::vector<std::int16_t>{-32768, 0, 32512, 0});
}
#include <common/platform.h>
#if !EKA2L1_PLATFORM(IOS)
#include <drivers/audio/backend/dsp_mp3.h>
#include <fstream>

TEST_CASE("Stopped DSP streams do not request guest buffers from the running device", "[audio][mp3]") {
    using namespace eka2l1::drivers;
    queued_audio_driver driver;
    dsp_output_stream_mp3 stream(&driver);
    REQUIRE(stream.set_properties(22050, 1));
    unsigned requests = 0;
    stream.register_callback(dsp_stream_notification_more_buffer,
        [&](void *) { ++requests; return true; }, nullptr);
    REQUIRE(stream.start());
    driver.output->queue(256);
    REQUIRE(requests == 1);
    REQUIRE(stream.stop());
    // The host device remains active across a virtual stop. No callback may
    // recreate the MMF chunk after the client released its buffer descriptor.
    driver.output->queue(256);
    REQUIRE(requests == 1);
    REQUIRE(stream.start());
    driver.output->queue(256);
    REQUIRE(requests == 2);
}

TEST_CASE("MP3 DSP preserves split frames and restarts without stale audio", "[audio][mp3]") {
    using namespace eka2l1::drivers;
    // Generated 0.2 s, 440 Hz mono at 22050 Hz, 32 kb/s; no tags or Xing header.
    std::ifstream file("audioassets/stream-tone.mp3", std::ios::binary);
    REQUIRE(file.good());
    const std::vector<std::uint8_t> input((std::istreambuf_iterator<char>(file)), {});
    queued_audio_driver driver;
    dsp_output_stream_mp3 stream(&driver);
    REQUIRE(stream.set_properties(22050, 1));
    REQUIRE(stream.format(MP3_FOUR_CC_CODE));
    auto drain = [&] {
        std::vector<std::uint8_t> result, block;
        while (stream.decode_data(block)) result.insert(result.end(), block.begin(), block.end());
        return result;
    };
    stream.write(input.data(), input.size());
    const auto whole = drain();
    REQUIRE(whole.size() >= 4410 * 2);
    REQUIRE(std::count_if(whole.begin(), whole.end(), [](auto v) { return v != 0; }) > 4000);
    std::vector<std::uint8_t> empty;
    REQUIRE_FALSE(stream.decode_data(empty));

    for (const unsigned chunk : {1, 3, 97, 512}) {
        REQUIRE(stream.stop());
        std::vector<std::uint8_t> fragmented;
        for (std::size_t i = 0; i < input.size(); i += chunk) {
            stream.write(input.data() + i, std::min<std::size_t>(chunk, input.size() - i));
            const auto block = drain();
            fragmented.insert(fragmented.end(), block.begin(), block.end());
        }
        REQUIRE(fragmented == whole);
    }

    // Resampling and mono-to-stereo conversion must preserve duration and channels.
    REQUIRE(stream.stop());
    REQUIRE(stream.set_properties(44100, 2));
    stream.write(input.data(), input.size());
    const auto stereo = drain();
    REQUIRE(stereo.size() == whole.size() * 4);
    for (std::size_t i = 0; i < stereo.size(); i += 4) {
        REQUIRE(stereo[i] == stereo[i + 2]);
        REQUIRE(stereo[i + 1] == stereo[i + 3]);
    }
    REQUIRE(stream.format(PCM16_FOUR_CC_CODE));
    REQUIRE_FALSE(stream.decode_data(empty));
    REQUIRE_FALSE(stream.format(AMR_FOUR_CC_CODE));
}
#endif
#endif
