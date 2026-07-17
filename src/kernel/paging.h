#pragma once
#include <stdint.h>

#define PAGE_TABLE_SIZE 1024

// Page Directory Entry — points to a page table
typedef struct {
    uint32_t present        : 1;
    uint32_t read_write     : 1;
    uint32_t user_supervisor: 1;
    uint32_t write_through  : 1;
    uint32_t cache_disable  : 1;
    uint32_t accessed       : 1;
    uint32_t reserved       : 1;
    uint32_t page_size      : 1;   // 0 = 4KB pages
    uint32_t ignored        : 4;
    uint32_t frame          : 20;  // physical address of page table >> 12
} __attribute__((packed)) PageDirectoryEntry;

// Page Table Entry — points to a physical page
typedef struct {
    uint32_t present        : 1;
    uint32_t read_write     : 1;
    uint32_t user_supervisor: 1;
    uint32_t write_through  : 1;
    uint32_t cache_disable  : 1;
    uint32_t accessed       : 1;
    uint32_t dirty          : 1;   // CPU sets this on write
    uint32_t reserved       : 1;
    uint32_t global         : 1;
    uint32_t ignored        : 3;
    uint32_t frame          : 20;  // physical address of page >> 12
} __attribute__((packed)) PageTableEntry;

void paging_init();
