#pragma once
#include <stdint.h>

// Saved CPU state passed to isr_handler. Layout must match isr_common_stub
// in isr.asm exactly — the C struct maps directly onto the stack frame.
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // pusha
    uint32_t vector, error_code;                        // pushed by stubs
    uint32_t eip, cs, eflags;                          // pushed by CPU
} __attribute__((packed)) ISR_Frame;

void init_isr();
void isr_handler(ISR_Frame* frame);
