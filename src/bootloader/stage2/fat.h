#pragma once
#include "stdint.h"
#include "disk.h"

#pragma pack(push, 1)

typedef struct {
    uint8_t Name[11];       // file name padded to 11 bytes, e.g. "KERNEL  BIN"
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
    uint32_t Size;          // size of the file in bytes
} FAT_DirectoryEntry;

#pragma pack(pop)

typedef struct {
    int Handle;
    bool isDirectory;
    uint32_t Position;
    uint32_t Size;
} FAT_File;

enum FAT_Attributes {
    FAT_ATTRIBUTE_READ_ONLY     = 0x01,
    FAT_ATTRIBUTE_HIDDEN        = 0x02,
    FAT_ATTRIBUTE_SYSTEM        = 0x04,
    FAT_ATTRIBUTE_VOLUME_ID     = 0x08,
    FAT_ATTRIBUTE_DIRECTORY     = 0x10,
    FAT_ATTRIBUTE_ARCHIVE       = 0x20,
    FAT_ATTRIBUTE_LFN           = FAT_ATTRIBUTE_READ_ONLY | FAT_ATTRIBUTE_HIDDEN | FAT_ATTRIBUTE_SYSTEM | FAT_ATTRIBUTE_VOLUME_ID
};

bool FAT_Initialize(DISK* disk);
FAT_File far* FAT_OpenEntry(DISK* disk, const char* path);
// Reads file contents into buffer
uint32_t FAT_Read(DISK* disk, FAT_File far* file, uint32_t byteCount, void* dataOut);
uint16_t FAT_IncrementCluster(FAT_File far* file);
bool FAT_FindFile(DISK* disk, FAT_File far* file, const char* name, FAT_DirectoryEntry* entryOut);
// Loads entry in directory
bool FAT_ReadEntry(DISK* disk, FAT_File far* file, FAT_DirectoryEntry* dirEntry);
void FAT_Close(FAT_File far* file);

