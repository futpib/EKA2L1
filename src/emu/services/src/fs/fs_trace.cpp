#include <services/fs/fs_trace.h>

#include <services/fs/fs.h>
#include <services/fs/op.h>
#include <services/fs/std.h>
#include <services/context.h>

#include <common/cvt.h>
#include <cpu/aot/aot_registry.h>
#include <cpu/arm_interface.h>
#include <kernel/kernel.h>
#include <kernel/process.h>
#include <kernel/session.h>
#include <kernel/thread.h>
#include <system/epoc.h>
#include <utils/reqsts.h>
#include <vfs/vfs.h>

#include <emscripten/emscripten.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace eka2l1::service {
    thread_local bool fs_trace_guest_sync_send = false;

    namespace {
        std::atomic<bool> recording{false};
        std::mutex trace_lock;
        std::vector<std::string> trace_lines;
        std::chrono::steady_clock::time_point trace_epoch;

        std::string field(std::string value) {
            std::replace_if(value.begin(), value.end(), [](unsigned char c) {
                return c == '\t' || c == '\n' || c == '\r';
            }, ' ');
            return value;
        }

        std::string module_at(std::uint32_t pc) {
            const auto *module = arm::aot::lookup_module(pc & ~std::uint32_t{1});
            return module ? module->name : "?";
        }

        bool path_in_slot_zero(std::uint32_t op) {
            switch (op) {
            case epoc::fs_msg_file_open:
            case epoc::fs_msg_file_create:
            case epoc::fs_msg_file_replace:
            case epoc::fs_msg_file_temp:
            case epoc::fs_msg_dir_open:
            case epoc::fs_msg_entry:
            case epoc::fs_msg_delete:
            case epoc::fs_msg_mkdir:
            case epoc::fs_msg_rmdir:
            case epoc::fs_msg_rename:
            case epoc::fs_msg_replace:
            case epoc::fs_msg_set_entry:
                return true;
            default:
                return false;
            }
        }
    }

    fs_trace_scope::fs_trace_scope(fs_server_client &client, ipc_context &ctx, std::uint32_t raw_op)
        : ctx_(ctx) {
        enabled_ = recording.load(std::memory_order_relaxed);
        if (!enabled_) return;

        began_ = std::chrono::steady_clock::now();
        raw_op_ = raw_op;
        op_ = ctx.msg->function & 0xff;
        guest_sync_ = fs_trace_guest_sync_send;
        auto *kernel = ctx.sys->get_kernel_system();
        pc_ = kernel->get_cpu()->get_pc();
        lr_ = kernel->get_cpu()->get_lr();
        module_ = module_at(pc_);
        lr_module_ = module_at(lr_);
        thread_ = ctx.msg->own_thr->name();
        process_ = ctx.msg->own_thr->owning_process()->name();
        session_ = ctx.msg->msg_session ? ctx.msg->msg_session->unique_id() : 0;
        offset_ = std::numeric_limits<std::uint64_t>::max();

        const int path_slot = op_ == epoc::fs_msg_read_file_section ? 1 : 0;
        if (path_in_slot_zero(op_) || op_ == epoc::fs_msg_read_file_section) {
            const auto path = ctx.get_argument_value<std::u16string>(path_slot);
            if (path) {
                path_ = field(common::ucs2_to_utf8(get_full_symbian_path(client.ss_path, *path)));
                if (op_ == epoc::fs_msg_read_file_section)
                    rom_file_ = !path_.empty() && (path_[0] == 'z' || path_[0] == 'Z') ? 1 : 0;
            }
        } else {
            const auto handle = ctx.get_argument_value<std::int32_t>(3);
            if (handle) {
                handle_ = *handle;
                auto *node = client.get_file_node(*handle);
                if (node && node->vfs_node && node->vfs_node->type == io_component_type::file) {
                    auto *opened = static_cast<file *>(node->vfs_node.get());
                    opened_ = opened;
                    path_ = field(common::ucs2_to_utf8(opened->file_name()));
                    rom_file_ = opened->is_in_rom() ? 1 : 0;
                } else path_ = fmt::format("handle:{}", *handle);
            }
        }

        if (op_ == epoc::fs_msg_file_read || op_ == epoc::fs_msg_file_write) {
            const auto length = ctx.get_argument_value<std::int32_t>(1);
            if (length && *length >= 0) length_ = static_cast<std::uint32_t>(*length);
            if (ctx.msg->function & epoc::fs::ipc_arg_slot2_des) {
                const auto offset = ctx.get_argument_data_from_descriptor<std::uint64_t>(2);
                if (offset) offset_ = *offset;
            } else {
                const auto offset = ctx.get_argument_value<std::int32_t>(2);
                if (offset && *offset >= 0) offset_ = static_cast<std::uint32_t>(*offset);
            }
            if (offset_ == std::numeric_limits<std::uint64_t>::max() && opened_)
                offset_ = opened_->tell();
        } else if (op_ == epoc::fs_msg_read_file_section) {
            const auto length = ctx.get_argument_value<std::uint32_t>(3);
            if (length) length_ = *length;
            if (ctx.msg->function & 0x00040000) {
                const auto offset = ctx.get_argument_data_from_descriptor<std::uint64_t>(2);
                if (offset) offset_ = *offset;
            } else {
                const auto offset = ctx.get_argument_value<std::uint32_t>(2);
                if (offset) offset_ = *offset;
            }
        }
    }

    fs_trace_scope::~fs_trace_scope() {
        if (!enabled_) return;
        const auto ended = std::chrono::steady_clock::now();
        if ((op_ == epoc::fs_msg_file_read || op_ == epoc::fs_msg_file_write) && opened_
            && offset_ != std::numeric_limits<std::uint64_t>::max()) {
            const auto position = opened_->tell();
            if (position >= offset_) actual_length_ = position - offset_;
        } else if (op_ == epoc::fs_msg_read_file_section) {
            actual_length_ = ctx_.get_argument_data_size(0);
        }
        int result = std::numeric_limits<int>::min();
        if (ctx_.msg->request_sts) {
            auto *status = ctx_.msg->request_sts.get(ctx_.msg->own_thr->owning_process());
            if (status) result = status->status;
        }
        if (result == 0 && (op_ == epoc::fs_msg_file_open || op_ == epoc::fs_msg_file_create
            || op_ == epoc::fs_msg_file_replace || op_ == epoc::fs_msg_file_temp
            || op_ == epoc::fs_msg_dir_open)) {
            const auto handle = ctx_.get_argument_data_from_descriptor<std::int32_t>(3);
            if (handle) handle_ = *handle;
        }
        const auto start_us = std::chrono::duration_cast<std::chrono::microseconds>(began_ - trace_epoch).count();
        const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(ended - began_).count();
        const auto line = fmt::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n",
            start_us, duration_us, raw_op_, op_, guest_sync_ ? 1 : 0, result,
            pc_, lr_, field(module_), field(lr_module_), field(process_), field(thread_), session_,
            handle_, path_, length_, actual_length_, offset_, rom_file_);
        const std::lock_guard<std::mutex> lock(trace_lock);
        trace_lines.push_back(line);
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void eka2l1_fs_trace_enable(int enabled) {
    const std::lock_guard<std::mutex> lock(eka2l1::service::trace_lock);
    eka2l1::service::trace_lines.clear();
    eka2l1::service::trace_epoch = std::chrono::steady_clock::now();
    eka2l1::service::recording.store(enabled != 0, std::memory_order_release);
}

extern "C" EMSCRIPTEN_KEEPALIVE int eka2l1_fs_trace_dump() {
    const std::lock_guard<std::mutex> lock(eka2l1::service::trace_lock);
    FILE *file = fopen("/tmp/fs-trace.tsv", "wb");
    if (!file) return -1;
    fputs("start_us\tduration_us\traw_op\top\tguest_sync\tresult\tpc\tlr\tmodule\tlr_module\tprocess\tthread\tsession\thandle\tpath\tlength\tactual_length\toffset\trom_file\n", file);
    for (const auto &line : eka2l1::service::trace_lines) fputs(line.c_str(), file);
    const int result = ferror(file) ? -1 : static_cast<int>(eka2l1::service::trace_lines.size());
    fclose(file);
    return result;
}
