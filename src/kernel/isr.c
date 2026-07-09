#include "isr.h"
#include "idt.h"
#include "pic.h"
#include <stdint.h>

#define NUM_IRQS 33         // INCREMENT FOR EACH IRQ HANDLER ADDED, 
                            // should ALWAYS == len(exception_names)

extern uint32_t isr_stub_table[NUM_IRQS];

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
    // Handle hardware IRQs
    if (frame->vector >= 32) {
        // Hardware IRQs don't halt, just send EOI and return
        // Remember that we mapped IRQ numbers 0-15 to 32-47
        // Need to unmap back to 0-15 before passing to PIC
        pic_send_eoi(frame->vector - 32);
        return;
    }

    // Handle CPU exceptions
    switch (frame->vector) {
        default:
        break;
    }
    
    // Unhandled exception - print error msg
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    const char* msg = exception_names[frame->vector];
    for (int i = 0; msg[i] != '\0'; i++)
        vga[i] = (uint16_t)(0x4F00 | (uint8_t)msg[i]); // white on red

    __asm__ volatile ("cli; hlt");
}

void isr_init()
{
    for (int i = 0; i < NUM_IRQS; i++)
        idt_set_gate((uint8_t)i, isr_stub_table[i]);
}
