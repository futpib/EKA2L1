#include <common/buffer.h>
#include <loader/rom.h>

#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    std::vector<std::uint8_t> bytes(0x500);
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    };
    const auto entry = [&](std::size_t offset, const char16_t *name, bool directory, std::uint32_t address) {
        const auto length = std::char_traits<char16_t>::length(name);
        put32(offset, 123);
        put32(offset + 4, address);
        bytes[offset + 8] = directory ? 0x10 : 0;
        bytes[offset + 9] = length;
        std::memcpy(bytes.data() + offset + 10, name, length * 2);
        const auto end = offset + 10 + length * 2;
        // Nonzero padding must not become part of a name or the next header.
        for (auto i = end; i < (end + 3) / 4 * 4; ++i) bytes[i] = 0xa5;
        return (end + 3) / 4 * 4;
    };
    put32(0x8c, 0x80000000);
    put32(0x90, bytes.size());
    put32(0x94, 0x80000300);
    put32(0x300, 1);
    put32(0x308, 0x80000340);
    auto end = entry(0x344, u"A", false, 0x80000480);
    end = entry(end, u"BB", true, 0x80000400);
    end = entry(end, u"CCC", false, 0x80000480);
    put32(0x340, end - 0x340);
    put32(0x400, entry(0x404, u"DD", false, 0x80000480) - 0x400);

    eka2l1::common::ro_buf_stream stream(bytes.data(), bytes.size());
    const auto rom = eka2l1::loader::load_rom(&stream);
    if (!rom || rom->root.root_dirs.size() != 1) return 1;
    const auto &root = rom->root.root_dirs[0].dir;
    if (root.entries.size() != 3 || root.subdirs.size() != 1
        || root.entries[0].name != u"A" || root.entries[1].name != u"BB"
        || root.entries[2].name != u"CCC" || root.subdirs[0].entries.size() != 1
        || root.subdirs[0].entries[0].name != u"DD"
        || root.entries[2].size != 123) return 1;
    std::puts("PASS ROM directory alignment across odd/even names and nested directories");
}
