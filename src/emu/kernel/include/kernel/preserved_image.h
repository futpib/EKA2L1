#pragma once

#include <loader/e32img.h>
#include <xxhash.h>

#include <cstring>
#include <string>

namespace eka2l1 {
    class io_system;
}

namespace eka2l1::hle {
    bool repair_preserved_resources(io_system &io, const std::u16string &executable);

    inline bool repair_preserved_image(loader::e32img &image) {
        if (image.header.uid3 != 0x2000afe3 || image.header.code_size != 0x160c30
            || image.header.compression_type != 0
            || image.header.code_offset > image.data.size()
            || image.header.code_size > image.data.size() - image.header.code_offset) {
            return false;
        }

        char *code = image.data.data() + image.header.code_offset;
        if (XXH64(code, image.header.code_size, 0) != 0x7a9ed46e51197171ULL) {
            return false;
        }

        // BodyZ 1.2.7 changes executable code without updating the game's
        // integrity records. Restore the official code before relocation;
        // the installed executable and its metadata stay intact.
        const unsigned char dispatch[] = { 0x2c, 0xf0, 0x93, 0xe5, 0x21, 0x30, 0xa0, 0xe3 };
        const unsigned char conditional_branch[] = { 0x05, 0x00, 0x00, 0x0a };
        std::memcpy(code + 0x3f24, dispatch, sizeof(dispatch));
        std::memcpy(code + 0x5654, conditional_branch, sizeof(conditional_branch));
        const unsigned char status[] = { 0x40, 0x30, 0x1b, 0xe5 };
        const unsigned char response[] = { 0x52, 0x33, 0x43, 0xe2, 0xf9, 0x38, 0x43, 0xe2, 0x02, 0x3b, 0x43, 0xe2 };
        std::memcpy(code + 0xdf0b4, status, sizeof(status));
        std::memcpy(code + 0xdf30c, response, sizeof(response));
        return true;
    }
}
