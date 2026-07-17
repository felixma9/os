#pragma once
#include <stdint.h>

#define PAGE_SIZE   4096
#define E820_USABLE 1
#define E820_MAP    ((uint32_t*)0x500)


// Layout written by stage2 at physical 0x500 before the mode switch:
//   [0x500]  uint32_t count
//   [0x504]  MemoryMapEntry[count]  (20 bytes each)
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;      // 1 = usable RAM
} __attribute__((packed)) MemoryMapEntry;

void     pmm_init();
uint32_t pmm_alloc_frame();
void     pmm_free_frame(uint32_t addr);
