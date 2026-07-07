#include <stdint.h>

// Define the GDT structure
// 	Base = 0 -> pointer to some address (32b, 4B)
//  Limit = 0x00000000, 8B
//  Access Byte = 0x00, 2B
//  Flags = 0x0

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access_byte;
    uint8_t  limit_high_and_flags;
    uint8_t  base_high;
} __attribute__((packed)) GDT_entry;

// This function calls the lgdt instruction in assembly, which tells the CPU
// where our global descriptor table is
int init_gdt();