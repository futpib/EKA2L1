#include <cpu/dyncom/armstate.h>
#include <cstddef>

extern "C" unsigned state_size() { return sizeof(ARMul_State); }
extern "C" unsigned decoded_capacity() { return TRANS_CACHE_SIZE; }
extern "C" unsigned decoded_used_offset() { return offsetof(ARMul_State, trans_cache_buf_top); }
