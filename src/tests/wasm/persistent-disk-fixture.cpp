#include <emscripten/emscripten.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE int test_disk_write(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    const auto size = strlen(text), written = fwrite(text, 1, size, file);
    const auto closed = fclose(file);
    return written == size && closed == 0 ? 0 : -1;
}
EMSCRIPTEN_KEEPALIVE int test_disk_large(const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    unsigned char data[65536];
    constexpr unsigned size = 5 * 1024 * 1024 + 31;
    for (unsigned offset = 0; offset < size;) {
        const unsigned count = size - offset < sizeof(data) ? size - offset : sizeof(data);
        for (unsigned i = 0; i < count; ++i) data[i] = ((offset + i) * 17u + 3) % 251;
        if (fwrite(data, 1, count, file) != count) { fclose(file); return -1; }
        offset += count;
    }
    return fclose(file);
}
EMSCRIPTEN_KEEPALIVE std::uint32_t test_disk_checksum(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    std::uint32_t hash = 2166136261u;
    unsigned char bytes[65536];
    while (const auto count = fread(bytes, 1, sizeof(bytes), file))
        for (unsigned i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    fclose(file);
    return hash;
}
EMSCRIPTEN_KEEPALIVE int test_disk_rename(const char *from, const char *to) { return rename(from, to); }
EMSCRIPTEN_KEEPALIVE int test_disk_remove(const char *path) { return unlink(path); }
EMSCRIPTEN_KEEPALIVE int test_disk_truncate(const char *path, int length) { return truncate(path, length); }
EMSCRIPTEN_KEEPALIVE int test_disk_preserve_time(const char *path, const char *text) {
    struct stat info{};
    if (stat(path, &info) || test_disk_write(path, text)) return -1;
    const timespec times[]{info.st_atim, info.st_mtim};
    return utimensat(AT_FDCWD, path, times, 0);
}
EMSCRIPTEN_KEEPALIVE double test_disk_modified(const char *path) {
    struct stat info{};
    if (stat(path, &info)) return -1;
    return double(info.st_mtim.tv_sec) * 1000 + info.st_mtim.tv_nsec / 1000000;
}
EMSCRIPTEN_KEEPALIVE int test_disk_buffered() {
    static FILE *file = fopen("/data/c/buffered", "wb");
    return file && fputs("buffered native write", file) >= 0 ? 0 : -1;
}
}
int main() { emscripten_exit_with_live_runtime(); }
