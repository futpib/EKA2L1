#include <emscripten.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <malloc.h>

// This file is linked only into an explicitly selected diagnostic build.
// Count every allocator lifetime, unwinding allocations >= 64 KiB and
// retaining recent releases >= 1 MiB to identify reused address ranges.
// Static storage and nonallocating reporting avoid recursion in malloc hooks.
namespace {
constexpr unsigned minimum_stack_bytes = 65536;
constexpr unsigned slot_count = 1 << 16;
constexpr unsigned group_count = 2048;
constexpr unsigned stack_bytes = 4096;
constexpr unsigned release_count = 8192;
constexpr unsigned minimum_release_bytes = 1024 * 1024;
struct allocation { unsigned pointer, requested, usable, group; std::uint64_t sequence; };
struct release { unsigned pointer, usable, group; std::uint64_t allocated_sequence, sequence; };
struct group {
    char stack[stack_bytes];
    std::uint64_t live_bytes, peak_bytes, allocated_bytes, live_count, allocations;
};
allocation slots[slot_count];
release releases[release_count];
group groups[group_count];
unsigned used_groups = 1;
unsigned large_count;
std::uint64_t live_bytes, peak_bytes, live_count, allocations, dropped, unknown_frees, truncated_stacks, event_sequence, release_total;
std::atomic_flag mutex = ATOMIC_FLAG_INIT;
thread_local unsigned allocator_depth;
struct allocator_call {
    const bool outer = allocator_depth++ == 0;
    ~allocator_call() { --allocator_depth; }
};
struct lock {
    lock() { while (mutex.test_and_set(std::memory_order_acquire)) {} }
    ~lock() { mutex.clear(std::memory_order_release); }
};
unsigned home(unsigned p) { return ((p >> 3) * 2654435761u) & (slot_count - 1); }
unsigned find(unsigned p) {
    unsigned i = home(p);
    while (slots[i].pointer && slots[i].pointer != p) i = (i + 1) & (slot_count - 1);
    return i;
}

EM_JS(int, capture_stack, (char *destination, unsigned capacity), {
    const stack = new Error().stack || "";
    const length = Math.min(stack.length, capacity - 1);
    for (let i = 0; i < length; ++i) HEAPU8[destination + i] = stack.charCodeAt(i);
    HEAPU8[destination + length] = 0;
    return stack.length >= capacity;
});

void record(void *pointer, std::size_t requested) {
    if (!pointer) return;
    char stack[stack_bytes];
    const auto usable = malloc_usable_size(pointer);
    const bool large = usable >= minimum_stack_bytes;
    const bool truncated = large && capture_stack(stack, sizeof(stack));
    lock guard;
    ++allocations;
    unsigned id = 0;
    if (large) {
        if (large_count == slot_count - 1) { ++dropped; return; }
        if (truncated) ++truncated_stacks;
        for (id = 1; id < used_groups; ++id) if (!std::strcmp(groups[id].stack, stack)) break;
        if (id == used_groups) {
            if (used_groups == group_count) { ++dropped; id = 0; }
            else { std::strcpy(groups[id].stack, stack); ++used_groups; }
        }
    }
    const unsigned p = reinterpret_cast<std::uintptr_t>(pointer);
    if (large) { slots[find(p)] = {p, static_cast<unsigned>(requested), static_cast<unsigned>(usable), id, ++event_sequence}; ++large_count; }
    auto &g = groups[id];
    g.live_bytes += usable; g.allocated_bytes += usable; ++g.live_count; ++g.allocations;
    if (g.live_bytes > g.peak_bytes) g.peak_bytes = g.live_bytes;
    live_bytes += usable; ++live_count;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
}

allocation forget(void *pointer) {
    if (!pointer) return {};
    const unsigned usable = malloc_usable_size(pointer);
    lock guard;
    const unsigned p = reinterpret_cast<std::uintptr_t>(pointer);
    if (usable < minimum_stack_bytes) {
        auto &g = groups[0];
        g.live_bytes -= usable; --g.live_count;
        live_bytes -= usable; --live_count;
        return {p, usable, usable, 0, 0};
    }
    unsigned gap = find(p);
    if (!slots[gap].pointer) { ++unknown_frees; return {}; }
    const auto old = slots[gap];
    --large_count;
    auto &g = groups[old.group];
    g.live_bytes -= old.usable; --g.live_count;
    live_bytes -= old.usable; --live_count;
    // Close the probe-chain gap so repeated frees do not accumulate tombstones.
    unsigned next = (gap + 1) & (slot_count - 1);
    while (slots[next].pointer) {
        const unsigned origin = home(slots[next].pointer);
        if (((next - origin) & (slot_count - 1)) >= ((next - gap) & (slot_count - 1))) {
            slots[gap] = slots[next]; gap = next;
        }
        next = (next + 1) & (slot_count - 1);
    }
    slots[gap] = {};
    return old;
}

void record_release(const allocation &old) {
    if (!old.pointer || old.usable < minimum_release_bytes) return;
    lock guard;
    releases[release_total++ % release_count] = {old.pointer, old.usable, old.group, old.sequence, ++event_sequence};
}

char report[24 * 1024 * 1024];
char *cursor;
void escaped(const char *s) {
    *cursor++ = '"';
    for (; *s; ++s) {
        if (*s == '"' || *s == '\\') *cursor++ = '\\';
        if (*s == '\n') { *cursor++ = '\\'; *cursor++ = 'n'; }
        else if (*s == '\r') { *cursor++ = '\\'; *cursor++ = 'r'; }
        else if (static_cast<unsigned char>(*s) >= 32) *cursor++ = *s;
    }
    *cursor++ = '"';
}
}

extern "C" {
void *__real_emscripten_builtin_malloc(std::size_t);
void *__real_emscripten_builtin_calloc(std::size_t, std::size_t);
void *__real_emscripten_builtin_realloc(void *, std::size_t);
void __real_emscripten_builtin_free(void *);
void *__real_emscripten_builtin_memalign(std::size_t, std::size_t);
void *__real_eka2l1_sparse_aligned_alloc(std::size_t, std::size_t, std::size_t *);

void eka2l1_record_sparse_allocation(void *p, std::size_t n) { allocator_call call; if (call.outer) record(p, n); }
void *__wrap_eka2l1_sparse_aligned_alloc(std::size_t alignment, std::size_t n, std::size_t *dirty) {
    // The aligned allocator calls malloc internally before splitting its chunk.
    allocator_call call;
    auto *p = __real_eka2l1_sparse_aligned_alloc(alignment, n, dirty);
    if (call.outer) record(p, n);
    return p;
}
void *malloc(std::size_t n) { allocator_call call; auto *p = __real_emscripten_builtin_malloc(n); if (call.outer) record(p, n); return p; }
void *calloc(std::size_t n, std::size_t size) { allocator_call call; auto *p = __real_emscripten_builtin_calloc(n, size); if (call.outer) record(p, n * size); return p; }
void free(void *p) { allocator_call call; if (call.outer) record_release(forget(p)); __real_emscripten_builtin_free(p); }
void *realloc(void *p, std::size_t n) {
    allocator_call call;
    if (!call.outer) return __real_emscripten_builtin_realloc(p, n);
    // Remove the old key before libc can release it to another worker.
    const auto old = forget(p);
    auto *result = __real_emscripten_builtin_realloc(p, n);
    if (result || !n) {
        if (result != p) record_release(old);
        record(result, n);
    }
    else if (old.pointer) {
        lock guard;
        if (old.usable >= minimum_stack_bytes) { slots[find(old.pointer)] = old; ++large_count; }
        auto &g = groups[old.group];
        g.live_bytes += old.usable; ++g.live_count;
        live_bytes += old.usable; ++live_count;
    }
    return result;
}
void *memalign(std::size_t alignment, std::size_t n) { allocator_call call; auto *p = __real_emscripten_builtin_memalign(alignment, n); if (call.outer) record(p, n); return p; }
void *aligned_alloc(std::size_t alignment, std::size_t n) { return memalign(alignment, n); }
int posix_memalign(void **out, std::size_t alignment, std::size_t n) {
    if (alignment < sizeof(void *) || (alignment & (alignment - 1))) return EINVAL;
    auto *p = memalign(alignment, n);
    if (!p) return ENOMEM;
    *out = p; return 0;
}
void *__wrap_emscripten_builtin_malloc(std::size_t n) { return malloc(n); }
void *__wrap_emscripten_builtin_calloc(std::size_t n, std::size_t s) { return calloc(n, s); }
void *__wrap_emscripten_builtin_realloc(void *p, std::size_t n) { return realloc(p, n); }
void __wrap_emscripten_builtin_free(void *p) { free(p); }
void *__wrap_emscripten_builtin_memalign(std::size_t a, std::size_t n) { return memalign(a, n); }

EMSCRIPTEN_KEEPALIVE const char *eka2l1_allocation_profile_report() {
    const auto info = mallinfo();
    lock guard;
    cursor = report;
    cursor += std::sprintf(cursor,
        "{\"threshold\":%u,\"profiler_static_bytes\":%zu,\"live_bytes\":%llu,\"peak_bytes\":%llu,\"live_count\":%llu,\"allocations\":%llu,\"dropped\":%llu,\"unknown_frees\":%llu,\"truncated_stacks\":%llu,\"released_large_events\":%llu,\"released_large_capacity\":%u,\"mallinfo_allocated_bytes\":%zu,\"groups\":[",
        minimum_stack_bytes, sizeof(slots) + sizeof(releases) + sizeof(groups) + sizeof(report),
        live_bytes, peak_bytes, live_count, allocations, dropped, unknown_frees, truncated_stacks,
        release_total, release_count, info.uordblks);
    for (unsigned id = 0; id < used_groups; ++id) {
        const auto &g = groups[id];
        cursor += std::sprintf(cursor,
            "%s{\"id\":%u,\"live_bytes\":%llu,\"peak_bytes\":%llu,\"allocated_bytes\":%llu,\"live_count\":%llu,\"allocations\":%llu,\"stack\":",
            id ? "," : "", id, g.live_bytes, g.peak_bytes, g.allocated_bytes, g.live_count, g.allocations);
        escaped(g.stack); *cursor++ = '}';
    }
    cursor += std::sprintf(cursor, "],\"large_allocations\":[");
    bool first = true;
    for (const auto &slot : slots) if (slot.pointer) {
        cursor += std::sprintf(cursor, "%s{\"pointer\":%u,\"requested\":%u,\"usable\":%u,\"group\":%u,\"sequence\":%llu}",
            first ? "" : ",", slot.pointer, slot.requested, slot.usable, slot.group, slot.sequence);
        first = false;
    }
    cursor += std::sprintf(cursor, "],\"released_large_allocations\":[");
    first = true;
    for (std::uint64_t i = release_total > release_count ? release_total - release_count : 0; i < release_total; ++i) {
        const auto &row = releases[i % release_count];
        cursor += std::sprintf(cursor,
            "%s{\"pointer\":%u,\"usable\":%u,\"group\":%u,\"allocated_sequence\":%llu,\"sequence\":%llu}",
            first ? "" : ",", row.pointer, row.usable, row.group, row.allocated_sequence, row.sequence);
        first = false;
    }
    *cursor++ = ']'; *cursor++ = '}'; *cursor = 0;
    return report;
}
}
