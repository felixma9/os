#pragma once
#include <stdint.h>

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  reserved;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed)) IDT_Entry;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) IDT_Descriptor;

void idt_set_gate(uint8_t vector, uint32_t handler);
void init_idt();
