#pragma once
#include <stdint.h>

// Layout written by stage2 at physical 0x500 before the mode switch:
//   [0x500]  uint32_t count
//   [0x504]  MemoryMapEntry[count]  (20 bytes each)
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;      // 1 = usable RAM
} __attribute__((packed)) MemoryMapEntry;

void     pmm_init();
uint32_t pmm_alloc_page();
void     pmm_free_page(uint32_t addr);
