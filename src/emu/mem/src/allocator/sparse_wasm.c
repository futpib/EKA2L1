#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

// Record fresh morecore bytes inside the allocator's existing lock. A separate
// sbrk reservation would race its multi-call growth sequence. Each thread tracks
// only growth performed by its own allocation; recycled chunks remain dirty.
static _Thread_local uintptr_t fresh_begin = UINTPTR_MAX;
static void *sparse_morecore(intptr_t increment) {
    void *result = sbrk(increment);
    if (increment && result != (void *)-1 && (uintptr_t)result < fresh_begin)
        fresh_begin = (uintptr_t)result;
    return result;
}
#define MORECORE sparse_morecore
#include EKA2L1_DLMALLOC_SOURCE

extern void eka2l1_record_sparse_allocation(void *, size_t) __attribute__((weak));

void *eka2l1_sparse_aligned_alloc(size_t alignment, size_t size, size_t *dirty) {
    fresh_begin = UINTPTR_MAX;
    void *result = dlmemalign(alignment, size);
    const uintptr_t base = (uintptr_t)result;
    *dirty = fresh_begin <= base ? 0 : fresh_begin - base < size ? fresh_begin - base : size;
    if (eka2l1_record_sparse_allocation) eka2l1_record_sparse_allocation(result, size);
    return result;
}
