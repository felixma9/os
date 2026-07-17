#include "mem.h"

void* memset(void* ptr, int x, uint32_t n) {
    // Starting at ptr, fill n bytes with value x
    uint8_t* iter = (uint8_t*) ptr;

    for (uint32_t i = 0; i < n; ++i) {
        *iter = x;
        iter++;
    }

    return ptr;
}
