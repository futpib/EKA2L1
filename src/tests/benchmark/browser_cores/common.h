#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>
#include "kernels.h"
#define RAM_SIZE (256*1024)
static uint8_t ram[RAM_SIZE] __attribute__((aligned(4096)));
static void memory_init(int kind) {
 memset(ram,0,sizeof ram);
 memcpy(ram+4096,kernels[kind],kernel_sizes[kind]);
 for(unsigned i=0;i<256;i++) ((uint32_t*)(ram+65536))[i]=i*2654435761u+17u;
}
static uint32_t memory_hash(void) {
 uint32_t x=2166136261u;
 for(unsigned i=0;i<256;i++) x=(x^((uint32_t*)(ram+65536))[i])*16777619u;
 return x;
}
