#include <emscripten.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <sys/mman.h>

extern "C" const char *eka2l1_allocation_profile_report();
extern "C" void *emscripten_builtin_memalign(std::size_t, std::size_t);
extern "C" void emscripten_builtin_free(void *);

EM_JS(void, verify, (int stage), {
    const p = JSON.parse(UTF8ToString(_eka2l1_allocation_profile_report()));
    globalThis.profiles ||= [];
    globalThis.profiles.push({stage, profile:p});
    if (p.dropped || p.unknown_frees) throw Error('Allocation lifetime tracking lost coverage');
    if (p.groups.reduce((n,g) => n+g.live_bytes,0) !== p.live_bytes) throw Error('Group totals differ');
});

int main() {
    verify(0);
    auto *a = std::malloc(8 * 1024 * 1024);
    auto *b = std::calloc(1, 4 * 1024 * 1024);
    auto *c = std::aligned_alloc(65536, 2 * 1024 * 1024);
    auto *d = emscripten_builtin_memalign(65536, 1024 * 1024);
    auto *mapping = mmap(nullptr, 16 * 1024 * 1024, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(a && b && c && d && mapping != MAP_FAILED);
    std::memset(a, 0x5a, 8 * 1024 * 1024);
    verify(1);
    // Exercise growth, shrink, and failed realloc; failure retains ownership.
    a = std::realloc(a, 12 * 1024 * 1024); assert(a);
    b = std::realloc(b, 2 * 1024 * 1024); assert(b);
    volatile std::size_t impossible_size = 0x7fffffff;
    auto *failed = std::realloc(b, impossible_size);
    std::printf("Failed realloc control: %p, original: %p\n", failed, b);
    assert(!failed);
    verify(2);
    std::free(a); std::free(b); std::free(c); emscripten_builtin_free(d);
    assert(munmap(mapping, 16 * 1024 * 1024) == 0);
    verify(3);
    void *small[4096];
    for (unsigned round = 0; round < 30; ++round) {
        for (unsigned i = 0; i < 4096; ++i) small[i] = std::malloc((i * 61 + round * 17) % 1024 + 1);
        for (unsigned i = 0; i < 4096; ++i) std::free(small[(i * 1777) % 4096]);
    }
    verify(4);
}
