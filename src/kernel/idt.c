#include "idt.h"

static IDT_Entry idt[256];
static IDT_Descriptor descriptor;

// vector == index into IDT, handler == addr of function to run
void idt_set_gate(uint8_t vector, uint32_t handler)
{
    idt[vector].offset_low  = handler & 0xFFFF;
    idt[vector].selector    = 0x08;
    idt[vector].reserved    = 0;
    idt[vector].type_attr   = 0x8E;
    idt[vector].offset_high = (handler >> 16) & 0xFFFF;
}

void init_idt()
{
    descriptor.limit = sizeof(idt) - 1;
    descriptor.base  = (uint32_t)idt;

    // Table starts zeroed — entries get populated when handlers are registered.
    // idt_set_gate() is called per-vector from isr_init().

    __asm__ volatile("lidt %0" : : "m"(descriptor));
}
