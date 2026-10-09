#include <common/frame_dumper.h>
#include <common/deterministic.h>
#include <common/performance.h>
#include <common/log.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <fstream>
#include <stdexcept>

namespace eka2l1::common {
    frame_dumper::frame_dumper(const std::string &output_dir, int total_frames)
        : output_dir_(output_dir)
        , total_frames_(total_frames) {
        if (benchmark::enabled()) {
            total_frames_ = benchmark::frame_count();
            if (const char *start = std::getenv("EKA2L1_BENCHMARK_START_US"))
                start_us_ = std::strtoull(start, nullptr, 10);
            if (const char *unique = std::getenv("EKA2L1_BENCHMARK_UNIQUE"))
                unique_ = std::strcmp(unique, "1") == 0;
            if (const char *path = std::getenv("EKA2L1_BENCHMARK_START_FRAME")) {
                std::ifstream input(path, std::ios::binary);
                if (!input) throw std::runtime_error("Cannot read benchmark start frame");
                start_pixels_.assign(std::istreambuf_iterator<char>(input), {});
                if (start_pixels_.empty()) throw std::runtime_error("Empty benchmark start frame");
            }
            if (total_frames_ <= 0 || total_frames_ > 100000)
                throw std::runtime_error("Benchmark frame count must be 1..100000");
            for (int i = 0; i < total_frames_; ++i) fib_indices_.push_back(i);
            return;
        }
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
        performance::scope capture_scope(performance::frame_capture);
        if (done()) {
            return false;
        }

        if (benchmark::enabled()) {
            ++presentation_;
            if (benchmark::virtual_us.load() < start_us_) return false;
        }

        // Don't start counting until we see a contentful frame
        if (!started_) {
            if (!start_pixels_.empty()) {
                if (start_pixels_.size() != static_cast<std::size_t>(width) * height * 4)
                    throw std::runtime_error("Benchmark start-frame dimensions differ");
                // Reference pixels are top-down RGBA; compare RGB because FBO
                // alpha is normalized to 255 in exported captures below.
                for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
                    if (std::memcmp(rgba_data + ((height - 1 - y) * width + x) * 4,
                            start_pixels_.data() + (y * width + x) * 4, 3)) return false;
                start_pixels_.clear();
            }
            if (!is_contentful(rgba_data, width, height)) {
                return false;
            }
            started_ = true;
            capture_start_ = std::chrono::steady_clock::now();
            first_virtual_us_ = benchmark::virtual_us.load();
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

            if (unique_) {
                if (previous_pixels_ == flipped) {
                    ++duplicates_;
                    return false;
                }
                previous_pixels_ = flipped;
            }

            int ok = 1;
            if (!performance::enabled || performance::capture_mode == 0) {
                performance::scope png_scope(performance::png_encode);
                ok = stbi_write_png(filename, width, height, 4, flipped.data(), width * 4);
            }
            if (ok) {
                LOG_INFO(COMMON, "Frame dumper: captured frame {} -> {}", frame_index_, filename);
            } else {
                LOG_ERROR(COMMON, "Frame dumper: failed to write {}", filename);
                throw std::runtime_error("Failed to write captured frame");
            }

            if (benchmark::enabled()) {
                std::ofstream manifest(output_dir_ + "/frames.jsonl", std::ios::app);
                manifest << "{\"frame\":" << frame_index_
                         << ",\"presentation\":" << presentation_
                         << ",\"virtual_us\":" << benchmark::virtual_us.load()
                         << ",\"instructions\":" << benchmark::instructions.load()
                         << ",\"width\":" << width << ",\"height\":" << height << "}\n";
                if (!manifest) throw std::runtime_error("Failed to write frame manifest");
            }

            captured_++;
            next_fib_pos_++;
            if (benchmark::enabled() && done()) {
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - capture_start_).count();
                std::ofstream metrics(output_dir_ + "/metrics.json");
                metrics << "{\"frames\":" << captured_ << ",\"capture_wall_seconds\":" << seconds
                        << ",\"first_virtual_us\":" << first_virtual_us_
                        << ",\"duplicate_presentations\":" << duplicates_
                        << ",\"last_virtual_us\":" << benchmark::virtual_us.load()
                        << ",\"instructions\":" << benchmark::instructions.load() << "}\n";
                if (!metrics) throw std::runtime_error("Failed to write benchmark metrics");
            }
        }

        frame_index_++;
        return should_capture;
    }
}
