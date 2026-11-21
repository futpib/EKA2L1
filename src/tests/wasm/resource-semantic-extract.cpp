#include <common/buffer.h>
#include <loader/sis_fields.h>

#include <zlib.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace eka2l1::loader;

std::string normalized_target(const std::u16string &target) {
    std::string result;
    result.reserve(target.size());
    for (char16_t ch : target) {
        if (ch > 0x7f) return {};
        char c = static_cast<char>(ch);
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        result.push_back(c);
    }
    if (result.size() < 3 || result[1] != ':' || result[2] != '\\') return {};
    if (result[0] == '!') result[0] = 'e';
    if (result[0] != 'e') return {};
    return result;
}

std::vector<std::uint8_t> read_payload(const sis_file_des &file,
    const sis_file_data &payload, const std::vector<std::uint8_t> &source) {
    const auto &compressed = payload.raw_data;
    const std::uint64_t encoded_field_size = static_cast<std::uint64_t>(compressed.len_low)
        | (static_cast<std::uint64_t>(compressed.len_high) << 32);
    if (encoded_field_size < 12 || file.uncompressed_len != compressed.uncompressed_size) {
        throw std::runtime_error("SIS file descriptor and data payload sizes disagree");
    }
    const std::uint64_t stored_size = encoded_field_size - 12;
    if (compressed.offset > source.size() || stored_size > source.size() - compressed.offset
        || compressed.uncompressed_size > 16 * 1024 * 1024) {
        throw std::runtime_error("SIS payload range is outside the pinned input or exceeds the size limit");
    }

    const auto *stored = source.data() + compressed.offset;
    std::vector<std::uint8_t> output(static_cast<std::size_t>(compressed.uncompressed_size));
    if (compressed.algorithm == sis_compressed_algorithm::none) {
        if (stored_size != compressed.uncompressed_size) {
            throw std::runtime_error("Uncompressed SIS payload length mismatch");
        }
        std::copy(stored, stored + stored_size, output.begin());
    } else if (compressed.algorithm == sis_compressed_algorithm::deflated) {
        z_stream stream{};
        if (inflateInit(&stream) != Z_OK) throw std::runtime_error("zlib inflateInit failed");
        stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(stored));
        stream.avail_in = static_cast<uInt>(stored_size);
        stream.next_out = reinterpret_cast<Bytef *>(output.data());
        stream.avail_out = static_cast<uInt>(output.size());
        const int status = inflate(&stream, Z_FINISH);
        const bool exact = status == Z_STREAM_END && stream.total_in == stored_size
            && stream.total_out == output.size();
        inflateEnd(&stream);
        if (!exact) throw std::runtime_error("SIS payload did not inflate to its declared exact size");
    } else {
        throw std::runtime_error("Unsupported SIS compression method");
    }
    return output;
}

void write_file(const std::filesystem::path &root, const std::string &target,
    const std::vector<std::uint8_t> &bytes) {
    std::string relative = target.substr(3);
    std::replace(relative.begin(), relative.end(), '\\', '/');
    if (relative.empty() || relative.find("..") != std::string::npos) {
        throw std::runtime_error("Unsafe SIS target path");
    }
    const auto output = root / std::filesystem::path(relative);
    std::filesystem::create_directories(output.parent_path());
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("Failed to write extracted SIS payload");
}
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: extract-core-sis <SkyForce.sis> <installed-e-root>\n";
        return 2;
    }

    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open SIS input");
        std::vector<std::uint8_t> source((std::istreambuf_iterator<char>(input)), {});
        if (source.size() != 1176624 || source.size() < 16) throw std::runtime_error("Unexpected SIS input size");

        auto stream = std::make_shared<eka2l1::common::ro_buf_stream>(source.data(), source.size());
        sis_parser parser(stream);
        const sis_header header = parser.parse_header();
        if (header.uid1 != 0x10201a7a || header.uid3 != 0xa020d913) {
            throw std::runtime_error("SIS UID does not identify the pinned Sky Force package");
        }
        const sis_contents contents = parser.parse_contents();
        const auto &controller = contents.controller;
        if (!controller.install_block.if_blocks.fields.empty()
            || !controller.install_block.controllers.fields.empty()) {
            throw std::runtime_error("Sky Force SIS contains install conditions or nested controllers; refusing partial extraction");
        }
        const std::uint64_t data_index = controller.idx.data_index;
        if (data_index >= contents.data.data_units.fields.size()) {
            throw std::runtime_error("Sky Force SIS controller data-unit index is out of range");
        }
        const auto *unit = reinterpret_cast<const sis_data_unit *>(contents.data.data_units.fields[data_index].get());
        if (!unit) throw std::runtime_error("Sky Force SIS data unit is missing");

        const std::set<std::string> expected = {
            "e:\\sys\\bin\\skyforce.exe",
            "e:\\private\\a020d913\\data.pak",
        };
        std::set<std::string> emitted;
        const auto root = std::filesystem::path(argv[2]);
        for (const auto &field : controller.install_block.files.fields) {
            const auto *file = reinterpret_cast<const sis_file_des *>(field.get());
            const std::string target = normalized_target(file->target.unicode_string);
            if (expected.count(target) == 0) continue;
            if (file->op != ss_op::install || file->idx >= unit->data_unit.fields.size()) {
                throw std::runtime_error("Required Sky Force SIS entry is not a direct install payload");
            }
            if (!emitted.insert(target).second) throw std::runtime_error("Duplicate required SIS target");
            const auto *payload = reinterpret_cast<const sis_file_data *>(unit->data_unit.fields[file->idx].get());
            const auto bytes = read_payload(*file, *payload, source);
            write_file(root, target, bytes);
        }
        if (emitted != expected) {
            std::string missing;
            for (const auto &target : expected) if (emitted.count(target) == 0) missing += " " + target;
            throw std::runtime_error("Missing required Sky Force SIS targets:" + missing);
        }
        std::cout << "extracted " << emitted.size() << " pinned Sky Force SIS files\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
