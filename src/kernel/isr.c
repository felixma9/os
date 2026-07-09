#include "isr.h"
#include "idt.h"
#include <stdint.h>

extern uint32_t isr_stub_table[32];

static const char* exception_names[] = {
    "Divide By Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating Point",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point",
    "Virtualization",
    "Control Protection",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved",
    "Security Exception",
    "Reserved",
};

// All interrupts funnel into this function, switch on frame->vector
void isr_handler(ISR_Frame* frame)
{
    // Write exception name directly to VGA buffer — no libc available
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    const char* msg = exception_names[frame->vector];
    for (int i = 0; msg[i] != '\0'; i++)
        vga[i] = (uint16_t)(0x4F00 | (uint8_t)msg[i]); // white on red

    __asm__ volatile("cli; hlt");
}

void isr_init()
{
    for (int i = 0; i < 32; i++)
        idt_set_gate((uint8_t)i, isr_stub_table[i]);
}
