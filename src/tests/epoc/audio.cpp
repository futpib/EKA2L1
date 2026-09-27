#include <catch2/catch.hpp>
#include <common/deterministic.h>
#include <drivers/audio/deterministic.h>
#include <filesystem>
#include <fstream>

namespace {
    struct audio_fixture {
        audio_fixture() {
            eka2l1::common::benchmark::virtual_us = 0;
            eka2l1::drivers::reset_benchmark_audio();
        }
        void advance(std::uint64_t us) {
            eka2l1::common::benchmark::virtual_us = us;
            eka2l1::drivers::pump_benchmark_audio(us);
        }
    };
}

TEST_CASE("Deterministic PCM consumes source frames on the guest clock", "audio") {
    using namespace eka2l1::drivers;
    audio_fixture fixture;
    auto stream = new_benchmark_dsp_out_stream();
    auto &out = static_cast<dsp_output_stream &>(*stream);
    REQUIRE(out.set_properties(8000, 2));
    REQUIRE(out.set_properties(0, 1));
    REQUIRE_FALSE(out.set_properties(8000, 3));
    REQUIRE_FALSE(out.format(MP3_FOUR_CC_CODE));
    unsigned requests = 0;
    out.register_callback(dsp_stream_notification_more_buffer, [&](void *) { ++requests; return true; }, nullptr);
    std::vector<std::uint8_t> pcm(800 * 4, 0x10); // 100 ms stereo.
    REQUIRE_FALSE(out.write(pcm.data(), 3));
    REQUIRE(out.write(pcm.data(), pcm.size()));
    REQUIRE(out.start());
    fixture.advance(50000);
    REQUIRE(out.samples_played() == 800);
    REQUIRE(out.samples_copied() == 800);
    REQUIRE(out.bytes_rendered() == 1600);
    REQUIRE(out.position() == 50000);
    REQUIRE(requests == 0);
    fixture.advance(60000);
    REQUIRE(requests == 1);
    fixture.advance(100000);
    REQUIRE(out.position() == 100000);
    REQUIRE(requests == 1);
    fixture.advance(200000);
    REQUIRE(out.position() == 100000); // Underrun is silence, not consumed PCM.
    REQUIRE(out.stop());
    REQUIRE_FALSE(out.is_playing());
    REQUIRE(out.write(pcm.data(), pcm.size()));
    REQUIRE(out.start());
    fixture.advance(300000);
    REQUIRE(out.position() == 200000);
    REQUIRE(requests == 2);
    out.reset_stat();
    REQUIRE(out.position() == 0);
}

TEST_CASE("Deterministic mixer exports signed PCM with exact duration and safe callbacks", "audio") {
    using namespace eka2l1::drivers;
    audio_fixture fixture;
    auto stream = new_benchmark_dsp_out_stream();
    auto &out = static_cast<dsp_output_stream &>(*stream);
    REQUIRE(out.set_properties(8000, 1));
    REQUIRE(out.format(PCM8_FOUR_CC_CODE));
    const std::uint8_t pcm[] = {0x80, 0x7f};
    REQUIRE(out.write(pcm, sizeof(pcm)));
    REQUIRE(out.start());
    out.register_callback(dsp_stream_notification_more_buffer, [&](void *) { stream.reset(); return false; }, nullptr);
    fixture.advance(250);
    REQUIRE_FALSE(stream);
    const auto dir = std::filesystem::temp_directory_path() / "eka-audio-unit";
    REQUIRE(std::filesystem::create_directory(dir));
    export_benchmark_audio(dir.string(), 250);
    std::ifstream file(dir / "audio.wav", std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), {}};
    REQUIRE(bytes.size() == 44 + 12 * 4);
    for (unsigned i = 0; i < 12; ++i) {
        const int expected = i < 6 ? 0x8000 : 0x7f00;
        REQUIRE((bytes[44 + i * 4] | bytes[45 + i * 4] << 8) == expected);
        REQUIRE((bytes[46 + i * 4] | bytes[47 + i * 4] << 8) == expected);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("Deterministic PCM retries declined buffer notifications", "audio") {
    using namespace eka2l1::drivers;
    audio_fixture fixture;
    auto stream = new_benchmark_dsp_out_stream();
    auto &out = static_cast<dsp_output_stream &>(*stream);
    unsigned requests = 0;
    out.register_callback(dsp_stream_notification_more_buffer,
        [&](void *) { return ++requests == 3; }, nullptr);
    REQUIRE(out.start());
    for (unsigned tick = 1; tick <= 4; ++tick) fixture.advance(tick * 10000);
    REQUIRE(requests == 3);
}

TEST_CASE("Live deterministic audio runs past capture limit without retaining PCM", "audio") {
    using namespace eka2l1;
    struct live_guard {
        live_guard() { common::benchmark::interactive = true; }
        ~live_guard() { common::benchmark::interactive = false; }
    } guard;
    audio_fixture fixture;
    auto stream = drivers::new_benchmark_dsp_out_stream();
    auto &out = static_cast<drivers::dsp_output_stream &>(*stream);
    REQUIRE(out.set_properties(8000, 1));
    unsigned requests = 0;
    out.register_callback(drivers::dsp_stream_notification_more_buffer,
        [&](void *) { ++requests; return true; }, nullptr);
    std::vector<std::uint8_t> pcm(1600, 0x10);
    REQUIRE(out.write(pcm.data(), pcm.size()));
    REQUIRE(out.start());
    fixture.advance(100000);
    REQUIRE(out.position() == 100000);
    REQUIRE(requests == 1);
    fixture.advance(121000000);
    REQUIRE(out.position() == 100000);
    REQUIRE(drivers::benchmark_audio_buffered_frames() == 0);
}
