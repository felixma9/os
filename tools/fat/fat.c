// Implementing fat in C to learn it first
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef uint8_t bool;
#define true 1
#define false 0

typedef struct {
    uint8_t BootJumpInstruction[3];
    uint8_t OemIdentifier[8];
    uint16_t BytesPerSector;
    uint8_t  SectorsPerCluster;
    uint16_t ReservedSectors;
    uint8_t FatCount;
    uint16_t DirEntryCount;
    uint16_t TotalSectors;
    uint8_t MediaDescriptorType;
    uint16_t SectorsPerFat;
    uint16_t SectorsPerTrack;
    uint16_t Heads;
    uint32_t HiddenSectors;
    uint32_t LargeSectorCount;

    // extended boot record
    uint8_t DriveNumber;
    uint8_t _Reserved;
    uint8_t Signature;
    uint32_t VolumeId;          // serial number, value doesn't matter
    uint8_t VolumeLabel[11];    // 11 bytes, padded with spaces
    uint8_t SystemId[8];

    // don't care about the remaining code
} __attribute__((packed)) BootSector; // in gcc, this attribute macro is used to ensure
                                      // compiler doesn't pad our struct

// One entry in the directory, according to FAT12 documentation
typedef struct {
    uint8_t Name[11];
    uint8_t Attributes;
    uint8_t _Reserved;
    uint8_t CreatedTimeTenths;
    uint16_t CreatedTime;
    uint16_t CreatedDate;
    uint16_t AccessedDate;
    uint16_t FirstClusterHigh;
    uint16_t ModifiedTime;
    uint16_t ModifiedDate;
    uint16_t FirstClusterLow;
    uint32_t Size;      // size of the file in bytes
} __attribute__((packed)) DirectoryEntry;

// global boot sector
BootSector g_BootSector;
// uint8_t * since we want to advance byte-by-byte in the FAT table, and uint8_t* just means 
// "this pointer is pointing to a single byte in memory, if we advance, advance by 1 byte"
uint8_t* g_Fat = NULL;
DirectoryEntry* g_RootDirectory = NULL;
uint32_t g_RootDirectoryEnd;        // Save where root dir ends and data begins

// Load g_BootSector with the data from the floppy_image (headers loaded in boot.asm)
int readBootSector(FILE* disk) {
    return fread(&g_BootSector, sizeof(g_BootSector), 1, disk);
}

bool readSectors(FILE* disk, uint32_t lba, uint32_t count, void* bufferOut) {
    bool ok = true;
    // FILE struct has a file position indicator, the position in the file we're reading
    // fseek(...) moves this cursor to the specified target
    ok = ok && (fseek(disk, lba * g_BootSector.BytesPerSector, SEEK_SET) == 0);
    ok = ok && (fread(bufferOut, g_BootSector.BytesPerSector, count, disk) == count);
    return ok;
}

bool readFat(FILE* disk) {
    g_Fat = (uint8_t*) malloc(g_BootSector.SectorsPerFat * g_BootSector.BytesPerSector);
    // After readSectors(...), g_Fat is a uint8_t* that stores the address of the beginning of the FAT table
    return readSectors(disk, g_BootSector.ReservedSectors, g_BootSector.SectorsPerFat, g_Fat);
}

bool readRootDirectory(FILE* disk) {
    // Calculate start of root dir
    uint32_t lba = g_BootSector.ReservedSectors + g_BootSector.SectorsPerFat * g_BootSector.FatCount;
    // lba is now holds the address AFTER the reserved sectors and the FAT, meaning root dir beginning
    // One DirectoryEntry in the root dir is 32 bytes, according to FAT12 docs
    uint32_t size = sizeof(DirectoryEntry) * g_BootSector.DirEntryCount;
    // How many sectors is our root dir?
    uint32_t sectors = (size / g_BootSector.BytesPerSector);
    if (size % g_BootSector.BytesPerSector > 0) {
        // Round up number of sectors needed
        sectors++;
    }

    // Right after root dir is data section, so this is useful to have
    g_RootDirectoryEnd = lba + sectors;
    g_RootDirectory = (DirectoryEntry*) malloc(sectors * g_BootSector.BytesPerSector);
    // Read from disk's root dir location into our g_RootDirectory, which is a series of DirectoryEntry objects
    return readSectors(disk, lba, sectors, g_RootDirectory);
}

DirectoryEntry* findFile(const char* name) {
    for (uint32_t i = 0; i < g_BootSector.DirEntryCount; ++i) {
        // strcmp -> compare null-terminated strings, don't need to pass in # of bytes
        // memcmp -> compare fixed-length blocks of any data
        if (memcmp(name, g_RootDirectory[i].Name, 11) == 0) {
            return &g_RootDirectory[i];
        }
    }

    return NULL;
}

bool readFile(DirectoryEntry* fileEntry, FILE* disk, uint8_t* outputBuffer) {
    bool ok = true;
    // Recall that in FAT12, only the lowest 12 bits of this firstClusterLow are used
    uint16_t currentCluster = fileEntry->FirstClusterLow;

    // Read each cluster
    do {
        // Convert from cluster to sector
        uint32_t lba = g_RootDirectoryEnd + (currentCluster - 2) * g_BootSector.SectorsPerCluster;

        // Read that sector into the outputBuffer (sectorsPerCluster can be 1 or greater)
        ok = ok && readSectors(disk, lba, g_BootSector.SectorsPerCluster, outputBuffer);

        // Increment outputBuffer ahead by however many bytes we read into it
        // Remember that in C arguments are pass-by-value, so we don't lose the reference to the start of outputBuffer
        // If we wanted to pass an arg by reference, we'd pass a pointer to it
        // In this case we'd pass a pointer to a pointer, so uint8_t**
        outputBuffer += g_BootSector.SectorsPerCluster * g_BootSector.BytesPerSector;

        // Determine next cluster (look up in FAT table)
        // Note that entries are 12 bits wide (1.5 bytes), which is a little annoying (Bytes can store half of one entry and half another)
        //      FF F0 00 -> this is actually 2 entries, split across 3 bytes
        // Multiply current cluster by 3 then divide by 2 to get offset into table
        uint32_t fatIndex = currentCluster * 3 / 2;

        // Case 1: currentCluster is even, take lower bits
        if (currentCluster % 2 == 0) {
            currentCluster = (*(uint16_t*)(g_Fat + fatIndex)) & 0x0FFF;
        }
        // Case 2: currentCluster is odd, take upper 12 bits
        else {
            currentCluster = (*(uint16_t*)(g_Fat + fatIndex)) >> 4;
        }

    } while (ok && currentCluster < 0xFF8); // Once cluster reaches 0xFF8, the chain is complete (reached end of file)

    return ok;
}

int main(int argc, char** argv) {

    // Check that CLI argument number is correct
    if (argc < 3) {
        printf("Syntax: %s <disk_image> <file_name>, where file_name is 11 bytes all caps, like \"TEST    TXT\"\n", argv[0]);
        return -1;
    }

    // argv[0] is the program name itself
    FILE* disk = fopen(argv[1], "rb");
    if (!disk) {
        // fprintf when we want to printf to a specified stream (stderr in this case)
        fprintf(stderr, "Cannot open disk image %s!", argv[1]);
        return -1;
    }

    if (!readBootSector(disk)) {
        fprintf(stderr, "Could not read boot sector!\n");
        return -2;
    }

    // At this point, g_BootSector contains the FAT data we declared in boot.asm, we read from the disk 

    if (!readFat(disk)) {
        fprintf(stderr, "Could not read FAT!\n");
        free(g_Fat);
        return -3;
    }

    // At this point, g_Fat contains the memory address of the first byte of the FAT table

    if (!readRootDirectory(disk)) {
        fprintf(stderr, "Could not read FAT!\n");
        free(g_Fat);
        free(g_RootDirectory);
        return -4;
    }

    // At this point, g_RootDirectory is a pointer to the first DirectoryEntry in our root dir
    // Root dir includes all the files we copied into the floppy image in the MakeFile

    DirectoryEntry* fileEntry = findFile(argv[2]);
    if (!fileEntry) {
        fprintf(stderr, "Could not find file %s!\n", argv[2]);
        free(g_Fat);
        free(g_RootDirectory);
        return -5;
    }

    // At this point fileEntry is a pointer to the DirectoryEntry that contains our file's information
    // This includes which FAT entry is the entrypoint for our file

    // Allocate a buffer into which we read the file from disk
    // Files take up a minimum of 1 cluster, so allocate clusters as needed
    uint32_t bytesPerCluster = g_BootSector.BytesPerSector * g_BootSector.SectorsPerCluster;
    uint32_t clusters = (fileEntry->Size + bytesPerCluster - 1) / bytesPerCluster;
    uint32_t bufferSize = clusters * bytesPerCluster;

    uint8_t* buffer = (uint8_t*) malloc(bufferSize);
    if (!readFile(fileEntry, disk, buffer)) {
        fprintf(stderr, "Could not read file %s!\n", argv[2]);
        free(g_Fat);
        free(g_RootDirectory);
        free(buffer);
        return -6;
    }

    // At this point buffer contains the file's contents

    // Now print the file we read
    for (size_t i = 0; i < fileEntry->Size; ++i) {
        if (isprint(buffer[i])) {
            // isprint tells us if the current char is printable ASCII
            // Yes, so we use fputc, the one char equivalent of fprintf
            fputc(buffer[i], stdout);
        } else {
            // Not printable, print it as <...>, where %02x is a format specifier
            // %x = print as hex
            // 2  = min. width of 2 chars
            // 0  = pad with 0 if less than 2 chars
            printf("<%02x>", buffer[i]);
        }
    }
    printf("\n");

    free(g_Fat);
    free(g_RootDirectory);
    return 0;
}