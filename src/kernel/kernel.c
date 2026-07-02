#include <stdint.h>

#define VGA_BUFFER ((volatile uint16_t*)0xB8000)
#define VGA_WHITE_ON_BLACK 0x0F

void kernel_main(void) {
    const char* msg = "Hello world FROM 32-BIT C KERNEL!!!";

    for (int i = 0; msg[i] != '\0'; ++i) {
        VGA_BUFFER[i] = ((uint16_t)VGA_WHITE_ON_BLACK << 8) | (uint8_t)msg[i];
    }
}
