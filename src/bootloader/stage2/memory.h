#pragma once
#include "stdint.h"

// Copy a number of bytes from src to dst
void far* memcpy(void far* dst, const void far* src, uint16_t num);

// Compare two blocks of memory based on alphabetical ordering
// If str1 < str2, return value < 0 (< = comes before)
// If str1 > str2, return value > 0 (> = comes after)
// If equal, return 0               (== = exactly the same)
int memcmp(const void far* str1, const void far* str2, uint16_t n);