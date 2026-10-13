#include <emscripten/emscripten.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sys/stat.h>
#include <dirent.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <mutex>
#include <wasi/api.h>
#include <cmath>
#include <fcntl.h>

namespace {
    std::atomic<bool> tracking{false};
    std::mutex writes_mutex;
    std::map<std::uint64_t, std::uint64_t> writes;
    std::uint64_t write_sequence = 0;
    void modified(std::uint64_t inode) {
        const std::lock_guard<std::mutex> guard(writes_mutex);
        writes[inode] = ++write_sequence;
    }
    std::uint64_t revision(std::uint64_t inode) {
        const std::lock_guard<std::mutex> guard(writes_mutex);
        const auto entry = writes.find(inode);
        return entry == writes.end() ? 0 : entry->second;
    }
    struct fingerprint {
        std::uint64_t inode, size;
        std::int64_t seconds, nanos;
        unsigned mode;
        bool unstable;
        std::uint64_t write_version = 0;
        bool same(const fingerprint &other) const {
            return inode == other.inode && size == other.size && seconds == other.seconds
                && nanos == other.nanos && mode == other.mode && write_version == other.write_version;
        }
    };
    // Only the scan worker writes a pending batch. The main thread acknowledges
    // it after the browser transaction commits; failed saves retain the baseline.
    std::atomic<int> phase{0};
    std::map<std::string, fingerprint> committed, pending;
    std::string result;

    std::int64_t host_millis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    template<class Writer> void inventory(const std::string &path, Writer &writer) {
        DIR *directory = opendir(path.c_str());
        if (!directory) throw std::runtime_error("Cannot scan " + path);
        try {
            while (auto *entry = readdir(directory)) {
                if (entry->d_name[0] == '.' && (!entry->d_name[1]
                        || (entry->d_name[1] == '.' && !entry->d_name[2]))) continue;
                const auto name = path + "/" + entry->d_name;
                struct stat info{};
                if (lstat(name.c_str(), &info) != 0) throw std::runtime_error("File changed during scan: " + name);
                const bool dir = S_ISDIR(info.st_mode);
                if (!dir && !S_ISREG(info.st_mode)) throw std::runtime_error("Unsupported device file: " + name);
                const auto modified = std::int64_t(info.st_mtim.tv_sec) * 1000 + info.st_mtim.tv_nsec / 1000000;
                fingerprint stamp{std::uint64_t(info.st_ino), std::uint64_t(info.st_size),
                    info.st_mtim.tv_sec, info.st_mtim.tv_nsec, unsigned(info.st_mode), modified >= host_millis() - 2};
                stamp.write_version = revision(stamp.inode);
                const auto old = committed.find(name);
                const bool changed = old == committed.end() || old->second.unstable || !stamp.same(old->second);
                if (!pending.emplace(name, stamp).second)
                    throw std::runtime_error("Directory changed during scan: " + path);
                writer.StartObject();
                writer.Key("path"); writer.String(name.c_str());
                writer.Key("directory"); writer.Bool(dir);
                writer.Key("size"); writer.Uint64(dir ? 0 : stamp.size);
                writer.Key("inode"); writer.Uint64(stamp.inode);
                writer.Key("modified"); writer.Int64(modified);
                writer.Key("mode"); writer.Uint(stamp.mode);
                writer.Key("changed"); writer.Bool(changed);
                writer.EndObject();
                if (dir) inventory(name, writer);
            }
        } catch (...) { closedir(directory); throw; }
        closedir(directory);
    }
}

extern "C" {
__wasi_errno_t __real___wasi_fd_write(__wasi_fd_t, const __wasi_ciovec_t *, size_t, __wasi_size_t *);
__wasi_errno_t __real___wasi_fd_pwrite(__wasi_fd_t, const __wasi_ciovec_t *, size_t, __wasi_filesize_t, __wasi_size_t *);
// Observe filesystem writes, not guest instructions or memory stores. Versions
// also detect same-size writes whose guest explicitly restores the old mtime.
__wasi_errno_t __wrap___wasi_fd_write(__wasi_fd_t fd, const __wasi_ciovec_t *vectors,
        size_t count, __wasi_size_t *written) {
    struct stat info{};
    const bool observe = tracking.load(std::memory_order_acquire) && fd > 2 && fstat(fd, &info) == 0;
    if (observe) modified(info.st_ino);
    const auto error = __real___wasi_fd_write(fd, vectors, count, written);
    if (observe && !error && *written) modified(info.st_ino);
    return error;
}
__wasi_errno_t __wrap___wasi_fd_pwrite(__wasi_fd_t fd, const __wasi_ciovec_t *vectors,
        size_t count, __wasi_filesize_t offset, __wasi_size_t *written) {
    struct stat info{};
    const bool observe = tracking.load(std::memory_order_acquire) && fd > 2 && fstat(fd, &info) == 0;
    if (observe) modified(info.st_ino);
    const auto error = __real___wasi_fd_pwrite(fd, vectors, count, offset, written);
    if (observe && !error && *written) modified(info.st_ino);
    return error;
}
EMSCRIPTEN_KEEPALIVE int eka2l1_disk_scan() {
    int idle = 0;
    if (!phase.compare_exchange_strong(idle, 1)) return -1;
    tracking.store(true, std::memory_order_release);
    std::thread([] {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        try {
            // Include writes still buffered by libc. Directory walking runs on
            // a separate worker and adds no work to guest memory/read paths.
            fflush(nullptr);
            writer.StartObject(); writer.Key("entries"); writer.StartArray();
            inventory("/data", writer);
            writer.EndArray(); writer.EndObject();
            result.assign(buffer.GetString(), buffer.GetSize());
        } catch (const std::exception &error) {
            buffer.Clear(); writer.Reset(buffer);
            writer.StartObject(); writer.Key("error"); writer.String(error.what()); writer.EndObject();
            result.assign(buffer.GetString(), buffer.GetSize());
        }
        phase.store(2, std::memory_order_release);
    }).detach();
    return 0;
}
EMSCRIPTEN_KEEPALIVE const char *eka2l1_disk_scan_result() {
    return phase.load(std::memory_order_acquire) == 2 ? result.c_str() : "";
}
EMSCRIPTEN_KEEPALIVE int eka2l1_disk_file_matches(const char *path) {
    if (phase.load(std::memory_order_acquire) != 2 || !path) return 0;
    const auto entry = pending.find(path);
    struct stat info{};
    if (entry == pending.end() || lstat(path, &info)) return 0;
    fingerprint current{std::uint64_t(info.st_ino), std::uint64_t(info.st_size),
        info.st_mtim.tv_sec, info.st_mtim.tv_nsec, unsigned(info.st_mode), false};
    current.write_version = revision(current.inode);
    return current.same(entry->second);
}
EMSCRIPTEN_KEEPALIVE void eka2l1_disk_modified(const char *path) {
    struct stat info{};
    if (tracking.load(std::memory_order_acquire) && path && stat(path, &info) == 0) modified(info.st_ino);
}
EMSCRIPTEN_KEEPALIVE int eka2l1_disk_set_metadata(const char *path, unsigned mode, double modified) {
    const auto seconds = static_cast<std::int64_t>(std::floor(modified / 1000));
    const timespec times[]{{seconds, static_cast<long>((modified - seconds * 1000) * 1000000)},
        {seconds, static_cast<long>((modified - seconds * 1000) * 1000000)}};
    return chmod(path, mode & 0777) || utimensat(AT_FDCWD, path, times, 0) ? -1 : 0;
}
EMSCRIPTEN_KEEPALIVE void eka2l1_disk_ack(int saved) {
    if (phase.load(std::memory_order_acquire) != 2) return;
    if (saved) committed.swap(pending);
    pending.clear(); result.clear();
    phase.store(0, std::memory_order_release);
}
}
