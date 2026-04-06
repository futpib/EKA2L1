#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eka2l1::common {
    class frame_dumper {
    public:
        explicit frame_dumper(const std::string &output_dir, int total_frames = 16);

        // Call on every frame with pixel data to check for content and potentially capture.
        // Returns true if this frame was captured.
        bool on_frame(const std::uint8_t *rgba_data, int width, int height);

        // Check if the current frame needs pixel data (i.e., needs capture or content check).
        // Call this before doing an expensive glReadPixels.
        bool needs_pixel_data() const;

        bool done() const { return captured_ >= total_frames_; }
        int frame_index() const { return frame_index_; }
        int captured() const { return captured_; }

        // Advance frame counter without providing pixel data (skip frame).
        void skip_frame();

    private:
        std::string output_dir_;
        int total_frames_;
        int frame_index_ = 0;
        int captured_ = 0;

        // Precomputed fibonacci frame indices
        std::vector<int> fib_indices_;
        int next_fib_pos_ = 0;
        bool started_ = false;
    };
}
