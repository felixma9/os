#include "stdint.h"
#include "stdio.h"
#include "fat.h"
#include "disk.h"
#include "memdefs.h"
#include "x86.h"

void _cdecl cstart_(uint16_t bootDrive) {
    // printf("Formatted %% %c %s\r\n", 'a', "string");
    // printf("Formatted %d %i %x %p %o %hd %hi %hhu %hhd\r\n", 1234, -5678, 0xdead, 0xbeef, 012345, (short)27, (short)-42, (signed char) 20);
    // printf("Formatted %ld %lx %lld %llx\r\n", -100000000l, 0xdeadbeefl, 10200300400, 0xdeadbeeffeebdaedull);

    DISK disk;
    if (!DISK_Initialize(&disk, bootDrive)) {
        printf("Failed to initialize disk in main\r\n");
        return;
    }

    if (!FAT_Initialize(&disk)) {
        printf("Failed to init FAT\r\n");
        return;
    }

    FAT_File far* kernelFile;
    kernelFile = FAT_Open(&disk, "KERNEL  BIN");
    if (kernelFile == NULL) {
        printf("Failed to open kernel\r\n");
        return;
    }

    printf("Size of the kernel: %d\r\n", kernelFile->Size);

    uint32_t kernelSize = kernelFile->Size;
    if (kernelSize > MEMORY_KERNEL_SIZE) {
        printf("Kernel is too large to fit in memory\r\n");
        return;
    }

    if (FAT_Read(&disk, kernelFile, kernelFile->Size, MEMORY_KERNEL_ADDR) != kernelSize) {
        printf("Error, no bytes were read into kernel memory");
        return;
    }

    x86_GetMemoryMap();
    x86_EnterProtectedModeAndJumpToKernel();

    // Should never reach here
    printf("Something broke!\r\n");
}
