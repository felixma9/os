# Devlog

A running log of what was built, what broke, and what clicked.

---

## 2026-07-17 — Paging and Identity Mapping

Paging makes the CPU translate every memory access through a two-level table lookup before it hits RAM. A **page directory** (1024 entries) points to **page tables** (each 1024 entries), and each page table entry points to a 4KB physical frame. The CPU uses bits 31-22 of a virtual address to index the page directory, bits 21-12 to index the page table, and bits 11-0 as the byte offset within the frame. The virtual address is never stored anywhere — it's implicit in the position of the entry.

Turning on paging is done by loading CR3 with the physical address of the page directory, then setting the PG bit in CR0. The moment that bit flips, every pointer the CPU sees is treated as virtual. If the kernel's addresses have no mapping, the CPU immediately page faults and crashes.

**Identity mapping** is how you safely make the transition. You set up a page table where entry `i` points to physical frame `i` — so virtual address X maps to the same physical address X. The kernel is at `0x30000`, which falls within the first 4MB covered by one page table (entries 0-1023, covering `0x000000`-`0x3FFFFF`). By filling all 1024 entries with `frame = i`, `present = 1`, `read_write = 1`, every address the kernel is already using remains valid after paging is enabled. The page table doesn't know it's covering the kernel — it's just that the kernel happens to live within the range it covers.

---

## 2026-07-16 — Physical Memory Manager (PMM)

The kernel has no idea what RAM is available to use. The CPU is in 32-bit flat mode — every pointer is a raw physical address — but that doesn't mean every address is safe. The BIOS surveys the machine's RAM layout and makes it available via INT 0x15/E820, a list of memory regions each tagged with a type (usable, reserved, ACPI, bad, etc.). Since this is a BIOS call, it can only be made in real mode — so stage2 collects the map before the mode switch and stores it at physical `0x500`, where the kernel picks it up.

The PMM tracks which 4KB **pages** of physical RAM are free using a **bitmap** — one bit per page, 0 = free, 1 = used. For 128MB of RAM that's 32,768 pages = 4KB of bitmap. Two operations:

- `pmm_alloc_page()` — scan for a free bit, mark it used, return the physical address
- `pmm_free_page(addr)` — clear the bit

Initialization (`pmm_init`) runs in three passes:
1. **Mark everything used** — conservative default.
2. **Walk the e820 map** — for each usable region, free those pages.
3. **Re-mark known-reserved regions** — IVT/BDA, stage2, the kernel itself, and the bitmap's own pages.

The bitmap lives immediately after `_kernel_end` (a linker symbol marking the end of the kernel binary), 4KB-aligned. No hardcoded address needed.

Everything downstream depends on the PMM: paging needs it to allocate page tables, the kernel heap needs it to get memory to carve up, and process creation needs it to hand pages to user programs.

---

## 2026-07-08 — GDT, IDT, and ISR

### Global Descriptor Table (GDT)

The CPU requires a GDT to enter 32-bit protected mode. In real mode, memory addresses were computed as `segment * 16 + offset`. Protected mode replaces that with a table lookup: each segment register holds a **selector** (an index into the GDT), and each GDT entry describes a region of memory with a base address, a size limit, and permission flags.

In practice we use a **flat model** — both the code and data descriptors have base=0 and limit=4GB, so the segmentation mechanism does nothing meaningful. The GDT is mandatory boilerplate to satisfy the CPU. Real memory protection comes later from paging.

The CPU is told where the GDT lives via the `LGDT` instruction, which loads a 6-byte structure (base address + size) into the GDTR register.

### Interrupt Descriptor Table (IDT)

The IDT is the protected-mode equivalent of the real-mode interrupt vector table. It has up to 256 entries, one per **vector number**. Each entry is a gate descriptor: it holds a handler address and the code segment selector to run the handler in.

When the CPU encounters an exception (divide by zero, page fault, etc.) or hardware interrupt, it looks up the vector number in the IDT and jumps to the registered handler. Without a populated IDT, any fault causes a triple fault and a silent crash. The CPU is told where the IDT lives via `LIDT`, same shape as `LGDT`.

Vectors 0–31 are reserved for CPU exceptions. Vectors 32+ are free for hardware interrupts and syscalls.

### Interrupt Service Routines (ISRs)

An ISR is simply the function that runs when an interrupt fires. Because the CPU doesn't call handlers like normal functions (it jumps there after pushing its own register state), each ISR needs a thin assembly **stub** that bridges the gap: it saves registers, pushes context onto the stack in a known layout, then calls a C dispatcher.

All 32 exception stubs funnel into a single `isr_handler(ISR_Frame*)` function. The `ISR_Frame` struct maps directly onto the saved stack, giving the C handler access to the vector number, error code, and all CPU registers at the time of the fault.

---

## 2026-06-22 — Where `bootDrive` actually comes from

`cstart_(uint16_t bootDrive)`'s argument isn't computed anywhere in C — it's the BIOS's boot drive number, threaded through two assembly hand-offs before it ever reaches C code:

1. **BIOS sets it.** When the BIOS boots a disk, it loads sector 0 into RAM, jumps to it, and leaves that drive's number in `DL` by convention (`0x00` = first floppy, `0x80` = first hard disk, etc.) — free information, not something this code computes.
2. **Stage1 preserves it.** `boot.asm` never touches `DL` after that point, so it survives the far jump from stage1 to stage2 untouched.
3. **Stage2 forwards it explicitly.** In `main.asm`:
   ```asm
   xor dh, dh      ; zero the upper byte
   push dx         ; push the (now 16-bit) drive number
   call _cstart_   ; becomes cstart_'s one argument
   ```
   That's the entire origin of `bootDrive` — it's `DL`, widened to 16 bits, passed as a normal `_cdecl` argument.

### Why `DISK_Initialize` needs it

Every disk operation goes through `INT 13h` BIOS calls (`x86_Disk_Read`, `x86_Disk_GetDriveParams`, `x86_Disk_Reset`), all of which require a drive number matching the BIOS's own numbering. Hardcoding `0` would be fragile — there's no guarantee the BIOS booted from drive `0` on every machine/emulator/config. `DISK_Initialize(DISK* disk, uint8_t driveNumber)` stores `bootDrive` into `disk->id` and immediately uses it to query the real drive geometry via `x86_Disk_GetDriveParams(disk->id, ...)` — the same "ask the BIOS, don't hardcode" philosophy already noted in `x86.h`'s comment above that function. From then on, every `DISK_ReadSectors` call uses that stored `disk->id` to know which physical drive to read from.

---

## 2026-06-21 — LBA formulas, disk order vs. RAM order, and a map of the FAT driver's memory

### LBA formulas, gathered in one place

Cluster → LBA (`FAT_ClusterToLba`):

```
LBA = DataSectionLba + (cluster - 2) * SectorsPerCluster
```

The `-2` exists because FAT cluster numbering starts at `2` — clusters `0` and `1` are reserved by the spec, so cluster `2` is the first real data cluster and maps to `DataSectionLba + 0`.

A specific sector within that cluster:

```c
uint32_t lba = FAT_ClusterToLba(fd->CurrentCluster) + fd->CurrentSectorInCluster;
```

Root directory LBA (fixed, no cluster math — root isn't cluster-based):

```
rootDirLba = ReservedSectors + SectorsPerFat * FatCount
```

FAT12's 12-bit packed entry lookup (same math as `boot.asm`, needed for following a subdirectory's cluster chain):

```
byteIndex   = (cluster * 3) / 2
entry16     = *(uint16_t far*)(g_Fat + byteIndex)
nextCluster = (cluster % 2 == 0) ? (entry16 & 0x0FFF) : (entry16 >> 4)
```

### Disk layout order ≠ RAM layout order

On disk, FAT tables come before the root directory (confirmed by the floppy layout in the README: reserved sector → FAT #1 → FAT #2 → root dir → data clusters), and `FAT_Initialize`'s read order matches that.

But where things land *in RAM* is a separate, unrelated decision. This driver places `FAT_Data` (which contains the root directory's buffer as a member) first in the fixed `MEMORY_FAT_ADDR` region, and the FAT table (`g_Fat`) right after it — the reverse of the on-disk order. That's fine: reading sectors off disk in one order and placing the bytes in RAM in a different order are independent operations: nothing requires them to match.

### Map of the FAT driver's memory region

```
0x00000 ┌─────────────────────────────────┐
        │ Interrupt Vector Table (IVT)    │
0x00400 ├─────────────────────────────────┤
        │ BIOS Data Area                  │
0x00500 ├─────────────────────────────────┤  ◄── MEMORY_FAT_ADDR
        │   "FAT driver" region            │      (see zoom-in below)
        │   (64KB budget, MEMORY_FAT_SIZE) │
0x10500 ├─────────────────────────────────┤  ◄── end of FAT driver budget
        │              ...free...          │
0x20000 ├─────────────────────────────────┤
        │ stage2 itself: code, stack,      │
        │ AND the pointer *variables*      │
        │ g_Data / g_Fat / g_DataSectionLba│
        │ / g_RootDirectoryEnd live here   │
0x80000 ├─────────────────────────────────┤
        │ Extended BIOS Data Area          │
0xA0000 ├─────────────────────────────────┤
        │ Video memory / BIOS ROM          │
0xFFFFF └─────────────────────────────────┘
```

Zoomed in on `0x00500`–`0x10500` — what the pointer *values* actually point to:

```
0x00500  g_Data ──────► ┌──────────────────────────────────────┐
                         │ FAT_Data                              │
                         │  ├─ BS  (union, 512 bytes)             │
                         │  │    .BootSectorBytes[512]            │ ← raw boot sector
                         │  │    .BootSector (named-field view)   │   (same bytes)
                         │  │                                     │
                         │  ├─ RootDirectory  (FAT_FileData)      │
                         │  │    .Public.Handle = -1               │
                         │  │    .Public.isDirectory = true        │
                         │  │    .FirstCluster = rootDirLba (disk #)│
                         │  │    .CurrentCluster                   │
                         │  │    .CurrentSectorInCluster            │
                         │  │    .Buffer[512] ← currently-loaded   │
                         │  │                    root dir sector   │
                         │  │                                     │
                         │  └─ OpenedFiles[10]  (FAT_FileData[])  │
                         │       each slot: same shape as          │
                         │       RootDirectory above, .Opened flag │
                         └──────────────────────────────────────┘
0x00500 + sizeof(FAT_Data)
         g_Fat ────────► ┌──────────────────────────────────────┐
                         │ FAT table bytes                       │
                         │ (SectorsPerFat × BytesPerSector)      │
                         │ — the on-disk FAT, copied in by       │
                         │   FAT_ReadFat                         │
                         └──────────────────────────────────────┘
```

Global pointer/value summary:

| Global | Type | Points to / holds | Lives where (variable itself) |
|---|---|---|---|
| `g_Data` | `FAT_Data far*` | `0x00500` — start of the struct above | stage2's data segment (`0x20000`+) |
| `g_Fat` | `uint8_t far*` | `0x00500 + sizeof(FAT_Data)` — the FAT table bytes | stage2's data segment |
| `g_DataSectionLba` | `uint32_t` | **not a RAM address** — a disk LBA number (where cluster `2`'s data starts on disk) | stage2's data segment |
| `g_RootDirectoryEnd` | `uint32_t` | meant to be a sector *count* (root dir size), currently unassigned | stage2's data segment |
| `g_RootDirectory` | `FAT_DirectoryEntry*` | `NULL` — declared, never used | stage2's data segment |

The key distinction: `g_Data`/`g_Fat` are pointers *into* the `0x500`–`0x10500` region, but the 4-byte pointer variables themselves are ordinary globals sitting in stage2's own segment up at `0x20000`. `g_DataSectionLba`/`g_RootDirectoryEnd` aren't addresses at all — they're disk sector numbers, used as inputs to `FAT_ClusterToLba` and the root-dir-bounds check, never dereferenced as pointers.

---

## 2026-06-20 — FAT_FileData internals, and what FAT_FindFile actually has to do

### How `FAT_FileData` works

- **Public/private split**: `FAT_File` (in `fat.h`) is the opaque handle callers see (`Handle`, `isDirectory`, `Position`, `Size`). `FAT_FileData` (private to `fat.c`) is the real struct, with `FAT_File Public` as its first member — so a `FAT_File far*` and a `FAT_FileData far*` pointing at the same handle share the same address, and the two are cast between freely inside `fat.c`.
- **`Buffer[SECTOR_SIZE]`** holds exactly *one sector's* worth of the file/directory's data at a time — never the whole thing. A file (or directory) can span many clusters, each cluster many sectors, but only 512 bytes of it are ever in memory at once.
- **`FirstCluster`** — fixed at open time: where this file/directory's cluster chain begins (or, for root, a special-cased starting LBA — root isn't actually cluster-based).
- **`CurrentCluster` / `CurrentSectorInCluster`** — together, these say *which* sector of the file is currently sitting in `Buffer`, so you know what to load next once it's consumed.
- **Two different "advance" rules**, gated by `SectorsPerCluster`:
  - Still inside the current cluster → increment `CurrentSectorInCluster` and the LBA by 1.
  - Exhausted the current cluster → look up `CurrentCluster` in the FAT table (`g_Fat`) to get the next cluster number (the FAT is a linked list — each cluster's successor lives in the FAT, not in the data itself), reset `CurrentSectorInCluster` to 0, recompute LBA via `FAT_ClusterToLba`.
- **Root directory is the exception**: no cluster chain, no FAT lookups — just a fixed run of sectors, so "next" is always `LBA + 1` until the whole fixed-size region has been read.

### What `FAT_FindFile` has to do (high level)

1. Confirm `file` is a directory — searching only makes sense on something whose `Buffer` bytes are actually `FAT_DirectoryEntry` records.
2. Reinterpret the *currently loaded* sector of `Buffer` as 16 `FAT_DirectoryEntry far*` slots and scan them, skipping deleted (`0xE5`), end-of-directory (`0x00` → stop entirely), and LFN entries — comparing the real 11-byte `Name` field via `memcmp` against the target name.
3. On a match, **copy** the entry into `*entryOut` (not just reassign the local pointer) and return true.
4. If the current sector is exhausted without a match, **refill the buffer** with the next sector — root-dir-vs-cluster-chain branching as above — and keep scanning.
5. Stop and return false at the directory's logical end (root: ran past its fixed sector count; subdirectory: FAT gives an end-of-chain marker).

This needs a `DISK*` parameter (currently missing from the signature) to perform those refill reads, and persisted root-directory-size info (currently computed then discarded in `FAT_Initialize`) to know when to stop for root.

---

## 2026-06-16 — Why ReservedSectors is already an LBA

FAT12 disk layout is linear from LBA 0:

```
LBA 0                       → boot sector
LBA 1 … ReservedSectors-1   → additional reserved sectors (if any)
LBA ReservedSectors         → FAT starts here
```

`ReservedSectors` is a *count* of sectors at the front of the disk. Because the disk starts at LBA 0, that count is also the starting LBA of the FAT — they're the same number by definition. So you can pass `ReservedSectors` directly to `DISK_ReadSectors` with no conversion.

This only works for the FAT. The root directory requires an explicit calculation because it sits after both the reserved region and all FAT copies:

```c
uint32_t lba = ReservedSectors + (SectorsPerFat * FatCount);
```

---

## 2026-06-15 (update 2) — Using a union for dual views of the same memory

A `union` in C makes all members share the same memory location. Size is determined by the largest member. Used in `FAT_Data` to get two views of the boot sector without copying:

```c
typedef struct {
    union {
        uint8_t BootSectorBytes[SECTOR_SIZE];  // raw byte buffer — passed to DISK_ReadSectors
        FAT_BootSector BootSector;             // struct view — access fields by name after reading
    } BS;
} FAT_Data;
```

`DISK_ReadSectors` writes raw bytes into `BootSectorBytes`. Immediately after, the same 512 bytes are accessible as named struct fields via `BootSector.BytesPerSector`, `BootSector.SectorsPerFat`, etc. No copy, no cast — the union provides both views for free.

---

## 2026-06-15 (update) — Manual memory placement instead of malloc

In the bootloader there's no heap allocator. Instead of `malloc`, memory is managed by placing pointers manually at known offsets within a fixed region:

```c
g_Data = (FAT_Data far*)MEMORY_FAT_ADDR;           // g_Data starts at a known address
g_Fat  = (uint8_t far*)g_Data + sizeof(FAT_Data);  // g_Fat starts immediately after
```

`sizeof(FAT_Data)` returns the byte size of the struct. Casting `g_Data` to `uint8_t far*` (a byte pointer) before adding makes pointer arithmetic advance by bytes, not by `sizeof(FAT_Data)` units. The result is a flat memory region with each section placed at a known offset:

```
MEMORY_FAT_ADDR
  ├── [FAT_Data struct]    ← g_Data
  └── [FAT table bytes]    ← g_Fat  (= g_Data + sizeof(FAT_Data))
```

---

## 2026-06-15 — FAT layer; uint8_t vs char, and far pointers on struct parameters

### `uint8_t` vs `char` for byte arrays

`uint8_t` and `char` are both single bytes and both work for ASCII. The FAT12 directory entry stores filenames as `uint8_t Name[11]` rather than `char Name[11]` to be explicit that it's a raw fixed-width byte array with no string semantics — no null terminator, no `strlen`, compared with `repe cmpsb cx=11` rather than `strcmp`. The `uint8_t` signals "treat this as bytes", not "treat this as a C string".

---

### Why `FAT_File far*` instead of `FAT_File*`

In the small memory model, a near pointer (`FAT_File*`) assumes the target lives within the current DS segment. `FAT_File far*` is a full 32-bit segment:offset pointer that can reach anywhere in the 1MB address space.

FAT functions like `FAT_Read` and `FAT_ReadEntry` use `far*` because they're general-purpose — they shouldn't constrain where the caller allocates their `FAT_File`. If the caller's `FAT_File` happens to be in a different segment from DS, a near pointer would silently read the wrong memory.

---

## 2026-06-14 (update) — Disk read function; far pointers and cdecl layout

### Watcom far pointer convention

In the small memory model, all pointers are **near** by default — 16-bit offsets relative to DS. A `far*` is a 32-bit segment:offset pair that can address anywhere in the 1MB address space regardless of DS.

When a far pointer is passed as a `_cdecl` argument, Watcom pushes it in two 16-bit halves: **segment first** (higher address), **offset second** (lower address). So to reconstruct it in ASM:

```asm
mov bx, [bp + 16]   ; segment half
mov es, bx
mov bx, [bp + 14]   ; offset half
                    ; es:bx now holds the full far pointer
```

This matters for disk reads: INT 13h uses ES:BX as the output buffer address. Using a far pointer lets the caller place the buffer anywhere in RAM, not just within the current DS segment.

---

### `_cdecl` stack layout in 16-bit Watcom

Each argument takes a minimum of 2 bytes. After `push bp; mov bp, sp`, arguments are at:

```
[bp+0]   saved bp
[bp+2]   return address
[bp+4]   first argument
[bp+6]   second argument
...
```

For `x86_Disk_Read(drive, cylinder, head, sector, count, far* dataOut)`:

```
[bp+4]   drive     (uint8_t,  2 bytes)
[bp+6]   cylinder  (uint16_t, 2 bytes)
[bp+8]   head      (uint16_t, 2 bytes)
[bp+10]  sector    (uint16_t, 2 bytes)
[bp+12]  count     (uint8_t,  2 bytes)
[bp+14]  dataOut offset   (far ptr low half)
[bp+16]  dataOut segment  (far ptr high half)
```

The shift count for `shr`/`sar` must be in `cl` specifically — no other register works. So loading `count` for a shift: `mov cl, [bp + 12]`.

---

## 2026-06-14 — Devlog created; stage1 + stage2 working

Created this devlog. At this point stage1 (boot.asm) is fully working and stage2 is running C code with a working `printf` implementation.

What follows are the key lessons learned getting here.

---

### Real mode addressing: it's not segment:offset

When I first saw `0x7C0:0x0000` and `0x0000:0x7C00` described as the same address, I didn't get it. The physical address formula in real mode is:

```
physical = segment * 16 + offset
```

Both of those resolve to `0x7C00`. The segment is shifted left 4 bits (×16), not just concatenated. This is why real mode can only address 1MB — the result is a 20-bit number even though both operands are 16-bit.

---

### `org 0x7C00` and why it matters

The assembler assigns each label a numeric address based on an internal counter. `org 0x7C00` sets that counter to `0x7C00` at the start, so labels get addresses starting from there.

At runtime, with CS=0x0000, `physical = 0 * 16 + label_address = label_address`. So `org 0x7C00` and `CS = 0x0000` must agree. If CS were `0x7C0` instead, a label at `0x7C08` would resolve to `0x7C0 * 16 + 0x7C08 = 0xF808` — completely wrong memory.

This is why stage1 normalizes CS immediately on startup.

---

### The `retf` trick for fixing CS

The BIOS may load the boot sector and jump to it as either `0x7C0:0x0000` or `0x0000:0x7C00`. You can't use a near jump to fix CS — near jumps only change IP. A far jump like `jmp 0x0000:.after` would work syntactically but creates a chicken-and-egg problem since `.after` is a label computed from `org`.

The solution: fake a far return.

```asm
push es          ; es = 0x0000 → will become new CS
push word .after ; will become new IP
retf             ; pops IP then CS → CS:IP = 0x0000:.after
```

`retf` pops two values: IP first, then CS. By pushing exactly what you want, you force CS to `0x0000` without needing a literal far jump target.

---

### Why FAT12 headers live in boot.asm

The FAT12 spec requires the first 3 bytes of the boot sector to be a jump over the BPB (BIOS Parameter Block), and bytes 3–61 to contain specific filesystem fields at exact offsets. The Makefile writes `boot.bin` directly over sector 0 of the floppy image, so if we didn't declare these fields ourselves, we'd corrupt them. Declaring them in the assembly preserves the layout the filesystem expects, while still letting us put boot code in the remaining ~448 bytes.

---

### Segment registers: DS, SS, CS must all agree (small model)

After stage1's far jump to `0x2000:0x0000`, only CS is correctly set to `0x2000`. DS and SS still hold whatever stage1 left in them. Stage2 resets everything to CS immediately:

```asm
mov ax, cs
mov ds, ax
mov ss, ax
mov sp, 0     ; stack at top of 64KB segment, grows downward
```

This is required by Watcom's small memory model (`-ms`): the compiler assumes CS=DS=SS everywhere. If they differ, C pointer dereferences (which use DS) read the wrong physical memory when the pointer points into the stack (which uses SS).

This was a hard bug to find: `*argp` was returning `0x0000` even though the value on the stack was correct. Root cause: `mov ax, dx; mov ss, ax` in the original main.asm was setting SS to the boot drive number (0 for floppy A:), not `0x2000`. So DS=0x2000, SS=0x0000 — every stack pointer read through C was landing in the wrong segment.

---

### `__cdecl` must be on the definition, not just the declaration

Watcom's default calling convention is register-based. `_cdecl` switches to stack-based. The key gotcha: Watcom applies the convention from the **function definition**, not the declaration. If `_cdecl` is in the header but not on the `.c` definition, callers (who see the header) expect stack-based, but the function itself uses registers — a silent mismatch that produces garbage.

Every function that crosses the ASM/C boundary or is variadic must have `_cdecl` on both declaration and definition.

---

### `argp` — walking variadic arguments

Variadic args in our `printf` are accessed by walking the stack manually:

```c
int* argp = (int*) &fmt;
argp++;   // skip past fmt, now points to first variadic arg
```

`int*` is used because `sizeof(int) == 2` in 16-bit mode, which matches the minimum stack slot size in `_cdecl`. Each `argp++` steps 2 bytes — one slot. Wider types take more slots: `long` is 4 bytes (`argp += 2`), `long long` is 8 bytes (`argp += 4`).

---

### The `%s` case: argp is char**

When `printf` receives a string argument, what's on the stack is the address of the first character — a `char*`. But `argp` is an `int*`. So `argp` is pointing to a slot that *contains* a `char*` address:

```
argp   → int*        (points to the stack slot)
*argp  → int         (the value in that slot — actually a char* address)
(char*) *argp        → char*   (the string pointer)
```

Equivalently: `*(char**) argp`. Cast `argp` to `char**` (pointer to a pointer), then dereference to get the `char*`. Two levels of indirection.

---

### 64-bit division in 16-bit real mode

There is no single instruction for 64-bit ÷ 32-bit in real mode. It requires two 32-bit `div` steps:

```
Step 1: eax = high 32 bits, edx = 0      → div ecx → quotient_high, remainder
Step 2: eax = low 32 bits, edx = remainder → div ecx → quotient_low, final remainder
```

High bits must be divided first. The remainder from step 1 becomes the upper 32 bits of the dividend for step 2 (`edx` carries over automatically between the two `div` instructions).

Stack layout for `x86_div64_32(uint64_t dividend, ...)` after `push bp; mov bp, sp`:

```
[bp+4]   low 32 bits of dividend   ← pushed last = closest to bp
[bp+8]   high 32 bits of dividend  ← pushed first = further from bp
[bp+12]  divisor (uint32_t)
[bp+16]  quotientOut (near ptr)
[bp+18]  remainderOut (near ptr)
```

Watcom pushes the high half first and low half last, consistent with little-endian layout.

---

### Why stage2 needs a linker but stage1 didn't

Stage1 is one `.asm` file — NASM assembles it directly to a flat binary with `-f bin`. No cross-file references, no sections to order.

Stage2 has four files (`main.asm`, `x86.asm`, `main.c`, `stdio.c`) that are each compiled/assembled independently into `.obj` files. The linker combines them, resolves cross-file symbols (`extern`/`global`), enforces section ordering (so `entry:` is at byte 0), and produces the final `FORMAT RAW BIN` output.

`NODEFAULTLIBS` is critical: Watcom's default C runtime assumes an OS exists (for heap, I/O, startup). We explicitly exclude it.

---
