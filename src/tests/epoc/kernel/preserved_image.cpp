#include <catch2/catch.hpp>
#include <common/buffer.h>
#include <common/cvt.h>
#include <common/fileutils.h>
#include <kernel/preserved_image.h>
#include <vfs/vfs.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>

TEST_CASE("preserved_image_repair_requires_the_known_complete_code", "kernel") {
    eka2l1::loader::e32img image{};
    image.header.uid3 = 0x2000afe3;
    image.header.code_size = 0x160c30;
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_image(image));

    image.header.code_offset = std::numeric_limits<std::uint32_t>::max();
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_image(image));

    image.header.code_offset = 0;
    image.data.resize(image.header.code_size);
    const auto before = image.data;
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_image(image));
    REQUIRE(image.data == before);
}

TEST_CASE("asphalt_preservation_image_restores_the_entire_official_code", "[.preserved-asphalt-image]") {
    const auto load = [](const char *variable) {
        const char *path = std::getenv(variable);
        REQUIRE(path);
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.good());
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        eka2l1::common::ro_buf_stream stream(bytes.data(), bytes.size());
        auto image = eka2l1::loader::parse_e32img(&stream, true);
        REQUIRE(image);
        return *image;
    };

    auto preserved = load("EKA2L1_ASPHALT3_BODYZ_EXE");
    auto official = load("EKA2L1_ASPHALT3_OFFICIAL_EXE");
    const auto before = preserved.data;
    auto wrong_uid = preserved;
    wrong_uid.header.uid3 = 0;
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_image(wrong_uid));
    REQUIRE(wrong_uid.data == before);
    auto damaged = preserved;
    damaged.data[damaged.header.code_offset + 4] ^= 1;
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_image(damaged));
    auto compressed = preserved;
    compressed.header.compression_type = 0x101f7afc;
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_image(compressed));
    REQUIRE(compressed.data == before);

    REQUIRE(eka2l1::hle::repair_preserved_image(preserved));
    REQUIRE(preserved.data.size() == before.size());
    REQUIRE(preserved.header.code_size == official.header.code_size);
    const auto begin = preserved.data.begin() + preserved.header.code_offset;
    const auto end = begin + preserved.header.code_size;
    REQUIRE(std::equal(begin, end, official.data.begin() + official.header.code_offset));
    REQUIRE(std::equal(preserved.data.begin(), begin, before.begin()));
    REQUIRE(std::equal(end, preserved.data.end(), before.begin() + (end - preserved.data.begin())));
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_image(preserved));
    const auto official_before = official.data;
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_image(official));
    REQUIRE(official.data == official_before);
}

TEST_CASE("preserved_asphalt_resource_repair_requires_the_exact_archive", "[.preserved-asphalt-resources]") {
    const char *fixture = std::getenv("EKA2L1_ASPHALT3_TRACKOBJ");
    REQUIRE(fixture);
    std::ifstream input(fixture, std::ios::binary);
    REQUIRE(input.good());
    const std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    REQUIRE(bytes.size() == 196585);

    const std::string root = "preserved_asphalt_resources";
    eka2l1::common::delete_folder(root);
    eka2l1::common::create_directories(root + "/Private/2000AFE3/A3");
    const auto source = root + "/Private/2000AFE3/A3/trackobj.bar.ngdat";
    const auto target = root + "/Private/2000AFE3/A3/trackobj.bar";
    const auto write = [](const std::string &path, const std::vector<char> &data) {
        std::ofstream output(path, std::ios::binary);
        output.write(data.data(), data.size());
        REQUIRE(output.good());
    };

    eka2l1::io_system io;
    auto fs = eka2l1::create_physical_filesystem(epocver::epoc94, "");
    io.add_filesystem(fs);
    REQUIRE(io.mount_physical_path(drive_number::drive_a, drive_media::physical,
        io_attrib_internal, eka2l1::common::utf8_to_ucs2(root)));

    write(source, bytes);
    write(target, { 'k', 'e', 'e', 'p' });
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_resources(io, u"A:\\sys\\bin\\asphalt3.exe"));
    std::ifstream existing(target, std::ios::binary);
    REQUIRE(std::string((std::istreambuf_iterator<char>(existing)), {}) == "keep");
    existing.close();
    REQUIRE(std::remove(target.c_str()) == 0);

    auto damaged = bytes;
    damaged[128] ^= 1;
    write(source, damaged);
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_resources(io, u"A:\\sys\\bin\\asphalt3.exe"));
    REQUIRE(io.exist(u"A:\\Private\\2000AFE3\\A3\\trackobj.bar.ngdat"));
    REQUIRE_FALSE(io.exist(u"A:\\Private\\2000AFE3\\A3\\trackobj.bar"));

    write(source, bytes);
    REQUIRE(eka2l1::hle::repair_preserved_resources(io, u"A:\\sys\\bin\\asphalt3.exe"));
    REQUIRE_FALSE(io.exist(u"A:\\Private\\2000AFE3\\A3\\trackobj.bar.ngdat"));
    std::ifstream renamed(target, std::ios::binary);
    REQUIRE(std::vector<char>((std::istreambuf_iterator<char>(renamed)), {}) == bytes);
    REQUIRE_FALSE(eka2l1::hle::repair_preserved_resources(io, u"A:\\sys\\bin\\asphalt3.exe"));
    renamed.close();
    eka2l1::common::delete_folder(root);
}
