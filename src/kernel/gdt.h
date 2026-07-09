#pragma once
#include <stdint.h>

// This struct represents an actual entry in the gdt table
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access_byte;
    uint8_t  limit_high_and_flags;
    uint8_t  base_high;
} __attribute__((packed)) GDT_entry;

// This struct is what we pass to lgdt, and tells the cpu where the table is and
// how big it is, note that this matches the struct we define in pm_switch.asm
typedef struct {
    uint16_t limit;     // size of gdt array in bytes minus 1
    uint32_t base;      // physical addr of gdt array
} __attribute__((packed)) GDT_Descriptor;

// This function calls the lgdt instruction in assembly, which tells the CPU
// where our global descriptor table is
void init_gdt();
void flush_gdt(GDT_Descriptor* descriptor);
