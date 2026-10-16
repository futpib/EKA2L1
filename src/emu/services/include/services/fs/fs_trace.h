#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace eka2l1 {
    struct fs_server_client;
    struct file;

    namespace service {
        struct ipc_context;

        extern thread_local bool fs_trace_guest_sync_send;

        class fs_trace_scope {
            ipc_context &ctx_;
            bool enabled_ = false;
            bool guest_sync_ = false;
            int rom_file_ = -1;
            std::uint32_t raw_op_ = 0;
            std::uint32_t op_ = 0;
            std::uint32_t pc_ = 0;
            std::uint32_t lr_ = 0;
            std::uint32_t session_ = 0;
            std::int32_t handle_ = -1;
            std::uint64_t length_ = 0;
            std::uint64_t actual_length_ = 0;
            std::uint64_t offset_ = 0;
            file *opened_ = nullptr;
            std::chrono::steady_clock::time_point began_;
            std::string process_;
            std::string thread_;
            std::string module_;
            std::string lr_module_;
            std::string path_;

        public:
            fs_trace_scope(fs_server_client &client, ipc_context &ctx, std::uint32_t raw_op);
            ~fs_trace_scope();
        };
    }
}
