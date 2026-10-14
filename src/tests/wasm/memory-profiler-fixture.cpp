#include <emscripten.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

static void *held_allocation;
static volatile unsigned js_call_count;

EM_JS(void, js_array_from_cpp, (unsigned elements), {
    globalThis.cpp_js_array = new Array(elements).fill(0.5);
});

extern "C" {
EMSCRIPTEN_KEEPALIVE unsigned allocate_cpp_control(unsigned bytes) {
    held_allocation = std::malloc(bytes);
    if (held_allocation) std::memset(held_allocation, 0x5a, bytes);
    return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(held_allocation));
}

EMSCRIPTEN_KEEPALIVE void release_cpp_control() {
    std::free(held_allocation);
    held_allocation = nullptr;
}

EMSCRIPTEN_KEEPALIVE unsigned allocated_cpp_bytes() {
    return mallinfo().uordblks;
}

EMSCRIPTEN_KEEPALIVE unsigned allocate_js_from_cpp(unsigned elements) {
    js_array_from_cpp(elements);
    return ++js_call_count;
}
}

int main() {}
