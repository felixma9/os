#pragma once
#include <stdint.h>

void pic_init();

// Call this at the end of each IRQ (interrupt request handler)
void pic_send_eoi(uint8_t irq);
