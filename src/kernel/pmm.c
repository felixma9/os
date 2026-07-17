#include "pmm.h"

// Set by the linker, it's where our bitmap will live
// Bitmap will be at first 4096-aligned addr after kernel
extern char _kernel_end[];

// We'll use uint8_t* since it's the smallest pointer type in C
// Think of this as:
//      bitmap[0] = frames 0-7
//      bitmap[1] = frames 8-15
//      bitmap[2] = frames 16-23
//      ...
static uint8_t* bitmap;
static uint32_t total_frames;

static void bitmap_set(uint32_t frame)
{
    bitmap[frame / 8] |= (1u << (frame % 8));
}

static void bitmap_clear(uint32_t frame)
{
    bitmap[frame / 8] &= ~(1u << (frame % 8));
}

static int bitmap_test(uint32_t frame)
{
    return (bitmap[frame / 8] >> (frame % 8)) & 1;
}

// Mark every frame that overlaps [start, end) as used.
static void mark_used(uint32_t start, uint32_t end)
{
    uint32_t p0 = start / PAGE_SIZE;
    uint32_t p1 = (end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t p = p0; p < p1 && p < total_frames; p++)
        bitmap_set(p);
}

// Mark only complete frames fully inside [start, end) as free.
static void mark_free(uint32_t start, uint32_t end)
{
    uint32_t p0 = (start + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t p1 = end / PAGE_SIZE;
    for (uint32_t p = p0; p < p1 && p < total_frames; p++)
        bitmap_clear(p);
}

void pmm_init()
{
    uint32_t       count   = E820_MAP[0];
    MemoryMapEntry* entries = (MemoryMapEntry*)(E820_MAP + 1);

    // Find the top of usable physical RAM, capped at 4GB for 32-bit mode.
    uint32_t top = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type != E820_USABLE) continue;
        if (entries[i].base > 0xFFFFFFFF)   continue;
        uint64_t end = entries[i].base + entries[i].length;
        if (end > 0xFFFFFFFF) end = 0xFFFFFFFF;
        if ((uint32_t)end > top) top = (uint32_t)end;
    }

    total_frames = top / PAGE_SIZE;

    // Place the bitmap at the first 4KB-aligned address after the kernel.
    uint32_t kernel_end  = (uint32_t)_kernel_end;
    uint32_t bitmap_addr = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    bitmap = (uint8_t*)bitmap_addr;

    uint32_t bitmap_bytes = (total_frames + 7) / 8;

    // 1. Start conservative: every page used.
    for (uint32_t i = 0; i < bitmap_bytes; i++)
        bitmap[i] = 0xFF;

    // 2. Free regions the BIOS says are usable.
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type != E820_USABLE) continue;
        if (entries[i].base > 0xFFFFFFFF)   continue;
        uint64_t end = entries[i].base + entries[i].length;
        if (end > 0xFFFFFFFF) end = 0xFFFFFFFF;
        mark_free((uint32_t)entries[i].base, (uint32_t)end);
    }

    // 3. Re-mark regions that are still in use regardless of the e820 map.
    mark_used(0x00000, 0x01000);                         // IVT, BDA, e820 map
    mark_used(0x20000, 0x30000);                         // stage2
    mark_used(0x30000, bitmap_addr + bitmap_bytes);      // kernel + bitmap
}

uint32_t pmm_alloc_frame()
{
    for (uint32_t i = 0; i < total_frames; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            return i * PAGE_SIZE;
        }
    }
    return 0;   // out of memory
}

void pmm_free_frame(uint32_t addr)
{
    bitmap_clear(addr / PAGE_SIZE);
}
