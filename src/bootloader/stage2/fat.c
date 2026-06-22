#include "fat.h"
#include "stdio.h"
#include "memdefs.h"
#include "utility.h"
#include "memory.h"

#define SECTOR_SIZE             512
#define MAX_PATH_SIZE           256
#define MAX_FILE_HANDLES        10
#define ROOT_DIRECTORY_HANDLE   -1  // Special index

#pragma pack(push, 1)

typedef struct {
    uint8_t     BootJumpInstruction[3];
    uint8_t     OemIdentifier[8];
    uint16_t    BytesPerSector;
    uint8_t     SectorsPerCluster;
    uint16_t    ReservedSectors;
    uint8_t     FatCount;
    uint16_t    DirEntryCount;
    uint16_t    TotalSectors;
    uint8_t     MediaDescriptorType;
    uint16_t    SectorsPerFat;
    uint16_t    SectorsPerTrack;
    uint16_t    Heads;
    uint32_t    HiddenSectors;
    uint32_t    LargeSectorCount;

    // extended boot record
    uint8_t     DriveNumber;
    uint8_t     _Reserved;
    uint8_t     Signature;
    uint32_t    VolumeId;          // serial number, value doesn't matter
    uint8_t     VolumeLabel[11];    // 11 bytes, padded with spaces
    uint8_t     SystemId[8];

    // don't care about the remaining code
} FAT_BootSector;

#pragma pack(pop)

typedef struct {
    FAT_File Public;                    // The data which is returned to user
    bool Opened;
    uint32_t FirstCluster;              // Where does this file's cluster chain start?
    uint32_t CurrentCluster;            // Which cluster in the chain is the buffer's current data from?
    uint32_t CurrentSectorInCluster;    // Which sector in the current cluster is the buffer's data from?
    uint8_t Buffer[SECTOR_SIZE];
} FAT_FileData;

// Contains all the memory
typedef struct {
    union {
        uint8_t BootSectorBytes[SECTOR_SIZE];       // We'll use this bytes view to init this bootsector
        FAT_BootSector BootSector;                  // This higher level sector view allows access via named fields
     } BS;

    FAT_FileData RootDirectory;

    FAT_FileData OpenedFiles[MAX_FILE_HANDLES];

} FAT_Data;

static FAT_Data far* g_Data;

// uint8_t * since we want to advance byte-by-byte in the FAT table, and uint8_t* just means 
// "this pointer is pointing to a single byte in memory, if we advance, advance by 1 byte"
static uint8_t far* g_Fat = NULL;
uint32_t g_DataSectionLba;
uint32_t g_RootDirNumSectors;

bool FAT_ReadBootSector(DISK* disk) {
    return DISK_ReadSectors(disk, 0, 1, &g_Data->BS.BootSectorBytes);
}

bool FAT_ReadFat(DISK* disk) {
    // Load [g_Fat] with the FAT from disk
    return DISK_ReadSectors(disk, g_Data->BS.BootSector.ReservedSectors, g_Data->BS.BootSector.SectorsPerFat, g_Fat);
}

bool FAT_Initialize(DISK* disk) {
    // Assign memory for g_Data
    g_Data = (FAT_Data far*)MEMORY_FAT_ADDR;

    if (!FAT_ReadBootSector(disk)) {
        printf("FAT: read boot sector failed\r\n");
        return false;
    }

    g_Fat = (uint8_t far*)g_Data + sizeof(FAT_Data);
    uint32_t fatSize = g_Data->BS.BootSector.BytesPerSector * g_Data->BS.BootSector.SectorsPerFat;
    if (sizeof(FAT_Data) + fatSize >= MEMORY_FAT_SIZE) {
        printf("FAT: not enough memory to read FAT! Required %u, only have %u\r\n", sizeof(FAT_Data) + fatSize, MEMORY_FAT_SIZE);
        return false;
    }

    if (!FAT_ReadFat(disk)) {
        printf("FAT: read FAT failed\r\n");
        return false;
    }

    // Read root directory
    uint32_t rootDirLba = g_Data->BS.BootSector.ReservedSectors + g_Data->BS.BootSector.SectorsPerFat * g_Data->BS.BootSector.FatCount;
    uint32_t rootDirSize = sizeof(FAT_DirectoryEntry) * g_Data->BS.BootSector.DirEntryCount;

    // Open root dir file
    g_Data->RootDirectory.Public.Handle = ROOT_DIRECTORY_HANDLE;
    g_Data->RootDirectory.Public.isDirectory = true;
    g_Data->RootDirectory.Public.Position = 0;
    g_Data->RootDirectory.Public.Size = sizeof(FAT_DirectoryEntry) * g_Data->BS.BootSector.DirEntryCount;;
    g_Data->RootDirectory.Opened = true;
    g_Data->RootDirectory.FirstCluster = rootDirLba;
    g_Data->RootDirectory.CurrentCluster = 0;
    g_Data->RootDirectory.CurrentSectorInCluster = 0;

    if (!DISK_ReadSectors(disk, rootDirLba, 1, g_Data->RootDirectory.Buffer)) {
        printf("FAT: read root directory failed");
        return false;
    }

    // Calculate data section
    g_RootDirNumSectors = (rootDirSize + g_Data->BS.BootSector.BytesPerSector - 1) / g_Data->BS.BootSector.BytesPerSector;
    g_DataSectionLba = rootDirLba + g_RootDirNumSectors;

    // Reset opened files
    for (int i = 0; i < MAX_FILE_HANDLES; ++i) {
        g_Data->OpenedFiles[i].Opened = false;
    }
}

uint32_t FAT_ClusterToLba(uint32_t cluster) {
    return g_DataSectionLba + (cluster - 2) * g_Data->BS.BootSector.SectorsPerCluster;
}

FAT_File far* FAT_OpenEntry(DISK* disk, FAT_DirectoryEntry* entry) {
    // Find empty handle
    int handle = -1;
    for (int i = 0; i < MAX_FILE_HANDLES; ++i) {
        if (!g_Data->OpenedFiles[i].Opened) {
            handle = i;
            break;
        }
    }

    if (handle < 0) {
        printf("FAT: out of file handles\r\n");
        return NULL;
    }

    // Init variables
    FAT_FileData far* fd = &g_Data->OpenedFiles[handle];
    fd->Public.Handle = handle;
    fd->Public.isDirectory = (entry->Attributes & FAT_ATTRIBUTE_DIRECTORY) != 0;
    fd->Public.Position = 0;
    fd->Public.Size = entry->Size;

    fd->Opened = true;
    fd->FirstCluster = entry->FirstClusterLow + ((uint32_t)entry->FirstClusterHigh << 16);
    fd->CurrentCluster = fd->FirstCluster;
    fd->CurrentSectorInCluster = 0;

    if (!DISK_ReadSectors(disk, FAT_ClusterToLba(fd->CurrentCluster), 1, fd->Buffer)) {
        printf("FAT: read error\r\n");
        return NULL;
    }

    return fd;
}

bool FAT_IncrementCluster(FAT_File far* file) {
    // FAT is a map, { CurrentCluster : NextCluster }
    // To find the next cluster, just index current cluster into FAT
    FAT_FileData far* fd = (FAT_FileData far* ) file;
    // Note that in FAT12, each key is 3 bytes

    uint32_t index = (fd->CurrentCluster * 3) / 2;
    // g_Fat is uint8*, explicitly cast to uint16_t*
    uint16_t far* p_nextCluster = (uint16_t far*)(g_Fat + index);
    uint16_t nextClusterNumber = *p_nextCluster;

    if (fd->CurrentCluster % 2 == 0) {
        // Even, take low 12 bits
        nextClusterNumber &= 0x0FFF;
    } else {
        // Odd, take high 12 bits
        nextClusterNumber >>= 4;
    }

    if (nextClusterNumber >= 0xFF8) {
        // End of chain or bad cluster
        return false;
    }

    fd->CurrentCluster = nextClusterNumber;
    return true;
}

// Search the given dir for the file name, return dir entry
bool FAT_FindFile(DISK* disk, FAT_File far* file, const char* name, FAT_DirectoryEntry* entryOut) {
    if (!file->isDirectory) {
        printf("FAT: Calling FAT_FindFile on a non-directory!\r\n");
        return false;
    }

    for (;;) {
        // Assume that file is a directory, meaning it contains FAT_DirectoryEntry pointers
        FAT_FileData far* fd = (FAT_FileData far*) file;
        FAT_DirectoryEntry far* entries = (FAT_DirectoryEntry far*) fd->Buffer;
        FAT_DirectoryEntry far* end = entries + (SECTOR_SIZE / sizeof(FAT_DirectoryEntry)); // we've hardcoded files to have a buffer of SECTOR_SIZE
        
        for (FAT_DirectoryEntry far* curEntry = entries; curEntry < end; ++curEntry) {
            if (curEntry->Name[0] == 0x00) return false;    // End of directory, nothing found
            if (curEntry->Name[0] == 0xE5) continue;        // This file has been deleted
            if (curEntry->Attributes & FAT_ATTRIBUTE_LFN) continue;   // This file is a dummy, long file name dir

            if (memcmp(curEntry->Name, name, 11) == 0) {
                printf("FAT: Found target file %s\r\n", name);
                *entryOut = *curEntry;
                return true;
            }
        }

        // Current sector is exhausted, load the next sector
        if (file->Handle == ROOT_DIRECTORY_HANDLE) {
            // This is root dir, so just increment the cur sec in cluster
            fd->CurrentSectorInCluster++;
            if (fd->CurrentSectorInCluster >= g_RootDirNumSectors) return false;     // Reached end of root dir

            uint32_t nextLba = g_Data->RootDirectory.FirstCluster + fd->CurrentSectorInCluster;
            if (!DISK_ReadSectors(disk, nextLba, 1, fd->Buffer)) {
                printf("FAT: Failed to read next sector in root dir\r\n");
                return false;
            }
        } else {
            // This is a normal subdir, use normal cluster-chain rules
            // Case 1: this is NOT the end of the cluster
            fd->CurrentSectorInCluster++;

            // Handle if this is the end of the cluster
            if (fd->CurrentSectorInCluster >= g_Data->BS.BootSector.SectorsPerCluster) {
                if (!FAT_IncrementCluster(file)) {
                    // If increment returns false, we've reached end of file or bad cluster
                    printf("FAT: Reached end of file/bad cluster\r\n");
                    return false;
                }
                fd->CurrentSectorInCluster = 0;
            }


            uint32_t nextLba = FAT_ClusterToLba(fd->CurrentCluster) + fd->CurrentSectorInCluster;
            if (!DISK_ReadSectors(disk, nextLba, 1, fd->Buffer)) {
                printf("FAT: Failed to read next sector into file buffer\r\n");
                return false;
            }
        }
    }
}

void FAT_Close(FAT_File far* file) {
    // Handle null file and closing root dir cases
    if (file == NULL) return;
    if (file->Handle == ROOT_DIRECTORY_HANDLE) return;

    FAT_FileData far* fd = (FAT_FileData far*) file;
    // No need to do more, since FAT_OpenFileEntry overwrites the first handle with Open == false
    fd->Opened = false;
}

// Opens a fat file whose name is **given in 8.3 padded form**
FAT_File* FAT_Open(DISK* disk, const char* path) {
    char curComponentName[MAX_PATH_SIZE];

    // Ignore leading slash
    if (path[0] == '/') path++;

    // Begin parsing path from root dir
    FAT_File far* parent = NULL;
    FAT_File far* current = &g_Data->RootDirectory.Public;

    bool isLast = false;

    // Iterate through all components of path
    // Open each component, and search for the next part of the path
    while (*path) {
        const char* delim = strchr(path, '/');
        // Case 1: we haven't reached the final component in path
        if (delim != NULL) {
            memcpy(curComponentName, path, delim - path);
            curComponentName[delim - path] = '\0';
            path = delim + 1;
        }

        // Case 2: we've reached the final path component
        else {
            unsigned len = strlen(path);
            memcpy(curComponentName, path, len);
            curComponentName[len] = '\0';
            path += len;
            isLast = true;
        }

        // find dir entry in current directory
        FAT_DirectoryEntry entry;
        if (!FAT_FindFile(disk, current, curComponentName, &entry)) {
            FAT_Close(current);
            printf("FAT: %s not found\r\n", curComponentName);
            return NULL;
        } else {
            // check if dir
            if (!isLast && (entry.Attributes & FAT_ATTRIBUTE_DIRECTORY) == 0) {
                printf("FAT: %s is not a directory!", curComponentName);
                return NULL;
            }

            // Close old parent, update parent
            FAT_Close(parent);
            parent = current;
            current = FAT_OpenEntry(disk, &entry);
        }

    }

    FAT_Close(parent);
    return current;
}