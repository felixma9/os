#pragma once

#include <stdint.h>

// ptr ==> Starting address of memory to be filled
// x   ==> Value to be filled
// n   ==> Number of bytes to be filled starting 
//         from ptr to be filled
// returns ptr
void* memset(void* ptr, int x, uint32_t n);