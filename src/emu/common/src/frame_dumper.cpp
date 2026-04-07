#include <common/frame_dumper.h>
#include <common/log.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <cstring>
#include <set>

namespace eka2l1::common {
    frame_dumper::frame_dumper(const std::string &output_dir, int total_frames)
        : output_dir_(output_dir)
        , total_frames_(total_frames) {
        // Generate fibonacci indices, deduplicated
        int a = 0, b = 1;
        while (static_cast<int>(fib_indices_.size()) < total_frames_) {
            if (fib_indices_.empty() || a != fib_indices_.back()) {
                fib_indices_.push_back(a);
            }
            int c = a + b;
            a = b;
            b = c;
        }
    }

    bool frame_dumper::needs_pixel_data() const {
        if (done()) return false;
        // Need pixels if not started (to check for content) or if this is a capture frame
        if (!started_) return true;
        return (next_fib_pos_ < static_cast<int>(fib_indices_.size()))
            && (frame_index_ == fib_indices_[next_fib_pos_]);
    }

    void frame_dumper::skip_frame() {
        if (!done() && started_) {
            frame_index_++;
        }
    }

    static bool is_contentful(const std::uint8_t *rgba_data, int width, int height) {
        std::set<std::uint32_t> colors;
        int total = width * height * 4;
        for (int i = 0; i < total; i += 40) {
            colors.insert((rgba_data[i] << 16) | (rgba_data[i+1] << 8) | rgba_data[i+2]);
            if (colors.size() > 3) return true;
        }
        return false;
    }

    bool frame_dumper::on_frame(const std::uint8_t *rgba_data, int width, int height) {
        if (done()) {
            return false;
        }

        // Don't start counting until we see a contentful frame
        if (!started_) {
            if (!is_contentful(rgba_data, width, height)) {
                return false;
            }
            started_ = true;
            LOG_INFO(COMMON, "Frame dumper: first contentful frame detected, starting capture");
        }

        bool should_capture = (next_fib_pos_ < static_cast<int>(fib_indices_.size()))
            && (frame_index_ == fib_indices_[next_fib_pos_]);

        if (should_capture) {
            char filename[256];
            std::snprintf(filename, sizeof(filename), "%s/frame-%04d.png",
                output_dir_.c_str(), frame_index_);

            // stbi_write_png expects top-to-bottom rows, but GL gives bottom-to-top.
            // Flip vertically and force alpha=255 (WebGL2 FBO may lack alpha).
            std::vector<std::uint8_t> flipped(width * height * 4);
            for (int y = 0; y < height; y++) {
                const std::uint8_t *src_row = rgba_data + (height - 1 - y) * width * 4;
                std::uint8_t *dst_row = flipped.data() + y * width * 4;
                std::memcpy(dst_row, src_row, width * 4);
                for (int x = 0; x < width; x++) {
                    dst_row[x * 4 + 3] = 255;
                }
            }

            int ok = stbi_write_png(filename, width, height, 4, flipped.data(), width * 4);
            if (ok) {
                LOG_INFO(COMMON, "Frame dumper: captured frame {} -> {}", frame_index_, filename);
            } else {
                LOG_ERROR(COMMON, "Frame dumper: failed to write {}", filename);
            }

            captured_++;
            next_fib_pos_++;
        }

        frame_index_++;
        return should_capture;
    }
}
