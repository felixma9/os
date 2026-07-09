#include "gdt.h"

static GDT_entry gdt[3];
static GDT_Descriptor descriptor;

static void set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt[i].limit_low            = limit & 0xFFFF;
    gdt[i].base_low             = base & 0xFFFF;
    gdt[i].base_mid             = (base >> 16) & 0xFF;
    gdt[i].access_byte          = access;
    gdt[i].limit_high_and_flags = ((limit >> 16) & 0x0F) | (flags << 4);
    gdt[i].base_high            = (base >> 24) & 0xFF;
}

void gdt_init() {
    set_entry(0, 0, 0,       0,    0  );  // null descriptor
    set_entry(1, 0, 0xFFFFF, 0x9A, 0xC);  // kernel code (selector 0x08)
    set_entry(2, 0, 0xFFFFF, 0x92, 0xC);  // kernel data (selector 0x10)

    descriptor.limit = sizeof(gdt) - 1;
    descriptor.base  = (uint32_t)gdt;

    flush_gdt(&descriptor);
}
