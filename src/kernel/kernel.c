#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "pic.h"
#include "pmm.h"

#define VGA_BUFFER ((volatile uint16_t*)0xB8000)
#define VGA_WHITE_ON_BLACK 0x0F

void temp_print(const char* msg) {
    for (int i = 0; msg[i] != '\0'; ++i) {
        VGA_BUFFER[i] = ((uint16_t)VGA_WHITE_ON_BLACK << 8) | (uint8_t)msg[i];
    }
}

void kernel_main() {
    const char* msg = "Hello world FROM 32-BIT C KERNEL!!!";
    temp_print(msg);
    
    gdt_init();
    idt_init();
    pic_init();
    isr_init();
    pmm_init();

    // Enable interrupts
    __asm__ volatile ("sti");
}

