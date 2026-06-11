#include "stdint.h"
#include "stdio.h"

void _cdecl cstart_(uint16_t bootDrive) {
    // puts("Hello world! FROM C!!!");

    // printf("Formatted %c", 'c');

    printf("Formatted %% %c %s\r\n", 'a', "string");
    printf("Formatted %d %i %x %p %o %hd %hi %hhu %hhd\r\n", 1234, -5678, 0xdead, 0xbeef, 012345, (short)27, (short)-42, (signed char) 20);
    printf("Formatted %ld %lx %lld %llx\r\n", -100000000l, 0xdeadbeefl, 10200300400, 0xdeadbeeffeebdaedull);

    for (;;);
}