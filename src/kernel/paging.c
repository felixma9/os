#include "paging.h"
#include "pmm.h"
#include "mem.h"

void paging_init()
{
    // TODO:
    // 1. Allocate a page directory (one physical frame from pmm_alloc_frame)
    uint32_t free_frame_addr = pmm_alloc_frame();

    // 2. Zero it out
    memset((void*)free_frame_addr, 0, PAGE_SIZE);

    // 3. Identity map the kernel: for each page from 0 to some end address,
    //    allocate a page table if needed and add a mapping virtual == physical
    // Alloc a page table for mapping v-addr -> p-addr around where the kernel lives
    // We need to use the bitmap to allocate this, not stack, since CPU reads tables
    // directly with p-addr
    uint32_t kernel_page_table_addr = pmm_alloc_frame();
    memset((void*)kernel_page_table_addr, 0, PAGE_SIZE);

    // Setup the page that covers kernel and VGA
    PageTableEntry* kernel_pt = (PageTableEntry*)kernel_page_table_addr;
    
    // For identity mapping, all pages in the table should point to the frame with same addr
    // One page table covers 4MB of virtual addr space, enough to cover our kernel code
    // We are mapping:
    //      virtual 0    - 4095     == physical 0    - 4095
    //      virutal 4096 - 2*4096-1 == physical 4096 - 4096*2-1
    // and so on
    for (int i = 0; i < PAGE_TABLE_SIZE; ++i) {
        // The 'frame' field in each entry is 20b, corresponding to the frame's top 20b
        kernel_pt[i].present    = 1;
        kernel_pt[i].read_write = 1;
        kernel_pt[i].frame      = i;
    }


    // 4. Load CR3 with the physical address of the page directory

    // 5. Set CR0.PG to enable paging
}
