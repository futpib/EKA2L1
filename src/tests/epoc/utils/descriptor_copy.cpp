#include <catch2/catch.hpp>
#include <utils/descriptor_copy.h>
#include <array>
#include <cstring>

TEST_CASE("descriptor copies support every layout without exposing guest pointers", "[descriptor]") {
    using namespace eka2l1::epoc;
    for (const auto type : {buf_const, ptr_const, ptr, buf, ptr_to_buf}) {
        std::array<std::uint8_t, 128> memory{};
        const auto put = [&](unsigned at, std::uint32_t value) { std::memcpy(memory.data()+at, &value, 4); };
        put(16, (type << 28) | 3);
        unsigned data = type == buf_const ? 20 : type == buf ? 24 : 64;
        if (type == ptr_const) put(20, data);
        if (type == ptr || type == ptr_to_buf) put(24, type == ptr ? data : data-4);
        std::memcpy(memory.data()+data, u"abc", 6);
        const auto read = [&](std::uint32_t at, void *out, std::size_t size) {
            if (std::uint64_t(at)+size > memory.size()) return false;
            std::memcpy(out, memory.data()+at, size); return true;
        };
        REQUIRE(copy_descriptor<char16_t>(read, 16) == std::u16string(u"abc"));
        REQUIRE_FALSE(copy_descriptor<char16_t>(read, 127));
        put(16, (type << 28) | 100);
        REQUIRE_FALSE(copy_descriptor<char16_t>(read, 16));
        put(16, (type << 28));
        REQUIRE(copy_descriptor<char>(read, 16) == std::string());
        put(16, 15u << 28);
        REQUIRE_FALSE(copy_descriptor<char>(read, 16));
    }
}
