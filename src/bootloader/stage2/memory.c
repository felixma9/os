#include "memory.h"

void far* memcpy(void far* dst, const void far* src, uint16_t num) {
    uint8_t far* u8Dst = (uint8_t far*)dst;
    const uint8_t far* u8Src = (const uint8_t far*) src;

    for (uint16_t i = 0; i < num; ++i) {
        u8Dst[i] = u8Src[i];
    }

    return dst;
}

int memcmp(const void far* str1, const void far* str2, uint16_t n) {
    const uint8_t far* u8Arg1 = (uint8_t far*) str1;
    const uint8_t far* u8Arg2 = (uint8_t far*) str2;

    for (uint16_t i = 0; i < n; ++i) {
        if (u8Arg1[i] != u8Arg2[i]) {
            // Two are not identical, return based on cur char
            if (u8Arg1[i] < u8Arg2[i])  return -1;
            else                        return 1;
        }
    }

    return 0;
}