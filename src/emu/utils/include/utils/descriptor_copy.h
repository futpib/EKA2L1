#pragma once
#include <utils/des.h>
#include <optional>
#include <string>

namespace eka2l1::epoc {
    // Read must copy the requested bytes; no guest pointer leaves this helper.
    template <typename Char, typename Read>
    std::optional<std::basic_string<Char>> copy_descriptor(Read read, std::uint32_t addr) {
        std::uint32_t info = 0, data = 0;
        if (!read(addr, &info, sizeof(info))) return std::nullopt;
        const auto type = static_cast<epoc::des_type>(info >> 28);
        const std::size_t length = info & 0xFFFFFF;
        const auto add = [](std::uint32_t base, unsigned offset, std::uint32_t &result) {
            if (std::uint64_t(base) + offset > UINT32_MAX) return false;
            result = base + offset; return true;
        };
        std::uint32_t field = 0;
        switch (type) {
        case epoc::buf_const: if (!add(addr, 4, data)) return std::nullopt; break;
        case epoc::buf: if (!add(addr, 8, data)) return std::nullopt; break;
        case epoc::ptr_const:
        case epoc::ptr:
        case epoc::ptr_to_buf:
            if (!add(addr, type == epoc::ptr_const ? 4 : 8, field)
                || !read(field, &data, sizeof(data))) return std::nullopt;
            if (type == epoc::ptr_to_buf && !add(data, 4, data)) return std::nullopt;
            break;
        default: return std::nullopt;
        }
        std::basic_string<Char> result(length, Char{});
        if (!read(data, result.data(), length * sizeof(Char))) return std::nullopt;
        return result;
    }

}
