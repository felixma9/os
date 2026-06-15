# OS from Scratch

A learning project building an OS from the ground up in x86 assembly and C, starting from the BIOS boot sequence.

---

## Table of Contents

1. [Boot Sequence Overview](#boot-sequence-overview)
2. [Real Mode Memory Addressing](#real-mode-memory-addressing)
3. [Stage 1 — `boot.asm`](#stage-1--bootasm)
4. [Stage 2 — Mixed ASM + C](#stage-2--mixed-asm--c)
5. [Build System](#build-system)
6. [Memory Layout](#memory-layout)
7. [Register Reference](#register-reference)

---

## Boot Sequence Overview

```
Power on
  → BIOS initializes hardware
  → BIOS loads sector 0 of floppy into RAM at 0x7C00
  → BIOS jumps to 0x7C00 (stage1 runs)
  → stage1 parses FAT12 filesystem, finds STAGE2.BIN
  → stage1 loads STAGE2.BIN into RAM at 0x2000:0000
  → stage1 far-jumps to 0x2000:0000 (stage2 runs)
  → stage2 resets segment registers, calls cstart_() in C
  → C code runs (printf, etc.)
```

The floppy disk and RAM are completely separate address spaces. The floppy stores bytes magnetically by sector number (LBA/CHS). RAM is what the CPU actually executes. The only way to move code from floppy to RAM is through a BIOS call (INT 13h), which copies sectors from the floppy and deposits them at a given RAM address. After that, the floppy is not needed again for that code.

---

## Real Mode Memory Addressing

When the CPU first boots, it is in **16-bit Real Mode**. Physical addresses are formed as:

```
physical address = segment * 16 + offset
```

Both the segment register and offset register are 16-bit, but the result is a 20-bit address — giving access to exactly **1MB of RAM**.

This is **not** `segment:offset` directly. The segment is shifted left by 4 bits (multiplied by 16) before being added to the offset. The same physical address can be expressed multiple ways:

```
0x7C0:0x0000  →  0x7C0 * 16 + 0x0000 = 0x7C00
0x000:0x7C00  →  0x000 * 16 + 0x7C00 = 0x7C00   ← same physical byte
```

The BIOS may load the boot sector using either form. Stage1 normalizes CS to `0x0000` immediately using a `retf` trick (see below), so that all label addresses computed from `org 0x7C00` resolve correctly.

### Segment Registers

The CPU uses different segment registers automatically depending on the type of memory access:

| Register | Used for |
|----------|----------|
| `CS` | Instruction fetches |
| `DS` | Data reads/writes |
| `SS` | Stack (push/pop) |
| `ES` | String instructions, BIOS calls |

Segment registers cannot be loaded with immediate values directly — you must go through a general-purpose register:

```asm
mov ax, 0x2000
mov ds, ax      ; OK
mov ds, 0x2000  ; INVALID
```

`CS` is even more restricted: the only way to change it is via a far jump, far call, or far return.

---

## Stage 1 — `boot.asm`

**Source:** `src/bootloader/stage1/boot.asm`  
**Purpose:** The BIOS unconditionally loads sector 0 of the floppy into RAM at `0x7C00` and jumps there. Stage1's job is to find `STAGE2.BIN` on the floppy filesystem and load it into RAM.

### Why `org 0x7C00`

When NASM assembles the file, it assigns each label an address based on an internal counter. `org 0x7C00` sets that counter to `0x7C00` at the start, so every label gets an address offset from `0x7C00`.

At runtime, with `CS = 0x0000`, the physical address of a label is:

```
physical = CS * 16 + label_address = 0 + 0x7C08 = 0x7C08   ✓
```

If `CS` were `0x7C0` instead, the same label would resolve to:

```
physical = 0x7C0 * 16 + 0x7C08 = 0x7C00 + 0x7C08 = 0xF808  ✗
```

`org 0x7C00` and `CS = 0x0000` must agree, or every label access is wrong.

### The `retf` Trick

The BIOS might jump to our code as either `0x7C0:0x0000` or `0x0000:0x7C00` — both point to the same physical byte but leave `CS` at different values. To guarantee `CS = 0x0000`, stage1 does a fake far return:

```asm
push es          ; es = 0x0000 → becomes new CS
push word .after ; label address → becomes new IP
retf             ; pops IP then CS, effectively: jmp 0x0000:.after
```

`retf` pops two values off the stack: IP first, then CS. By pushing the values we want, we force `CS:IP` to exactly `0x0000:.after`. A regular near `jmp` cannot do this — it only changes IP, not CS.

### FAT12 Headers

The FAT12 filesystem spec requires the very first 3 bytes of the boot sector to be a jump over the BPB (BIOS Parameter Block):

```asm
jmp short start  ; 2 bytes
nop              ; 1 byte (pad to 3)
```

Bytes 3–61 must contain the BPB fields in exact order (`bdb_bytes_per_sector`, `bdb_sectors_per_fat`, etc.). These are how the filesystem knows the geometry of the disk. The Makefile writes `stage1.bin` directly over sector 0 of the floppy image, so by declaring these fields ourselves we preserve the FAT12 headers the filesystem expects.

All actual boot code must fit between offset 62 and offset 510 — roughly **448 bytes**.

### Floppy Disk Layout (FAT12)

```
[ Reserved (1 sector) ][ FAT #1 (9 sectors) ][ FAT #2 (9 sectors) ][ Root Dir (14 sectors) ][ Data clusters ]
  sector 0               sector 1–9             sector 10–18          sector 19–32             sector 33+
```

- The root directory is `ceil(224 entries × 32 bytes / 512) = 14 sectors`
- Data starts at sector `1 + 18 + 14 = 33`
- Converting cluster number to LBA: `LBA = (cluster - 2) + 33 = cluster + 31`

### Finding STAGE2.BIN

FAT12 directory entries are 32 bytes each. The filename occupies the first 11 bytes in 8.3 format (space-padded). Stage1 uses `repe cmpsb` to compare 11 bytes at a time against `"STAGE2  BIN"` until a match is found. Offset 26 in the directory entry contains the starting cluster number.

### Following the FAT Chain

FAT12 entries are 12 bits wide (1.5 bytes), so two entries share 3 bytes. The lookup for cluster `N`:

```asm
index = (N * 3) / 2           ; byte offset into FAT table
entry = FAT[index] (16 bits)  ; read 2 bytes (spans 1.5-byte boundary)

if N is even: take lower 12 bits  (AND 0x0FFF)
if N is odd:  take upper 12 bits  (SHR 4)
```

A value of `0xFF8` or higher means end-of-file. Otherwise, the value is the next cluster number — repeat until end.

### Loading Stage2

Stage2 is loaded to physical address `0x20000` (segment `0x2000`, offset `0`):

```asm
mov bx, 0x2000
mov es, bx
mov bx, 0          ; ES:BX = 0x2000:0x0000
call disk_read      ; BIOS copies floppy sectors into RAM here
```

The address `0x20000` (128KB) was chosen to avoid conflicts with:
- IVT at `0x00000`–`0x003FF`
- BIOS data area at `0x00400`–`0x004FF`
- Boot sector at `0x07C00`
- Stack and FAT/root-dir read buffer around `0x07E00`–`0x09A00`

After loading, stage1 does a far jump to hand off control:

```asm
jmp 0x2000:0x0000
```

This sets `CS = 0x2000` and `IP = 0x0000`. The boot drive number is preserved in `DL` across the jump.

---

## Stage 2 — Mixed ASM + C

**Sources:** `src/bootloader/stage2/`  
**Purpose:** The first real logic layer. Stage2 is written in a mix of assembly (`main.asm`, `x86.asm`) and C (`main.c`, `stdio.c`), compiled with OpenWatcom and linked into a flat binary.

### Why a Linker Is Needed Here

Stage1 was a single `.asm` file — NASM assembled it directly to a flat binary. Stage2 has multiple files (`main.asm`, `main.c`, `stdio.c`, `x86.asm`) that are each compiled independently into `.obj` files. The linker (`wlink`) combines them:

```
main.asm  ──[NASM]──► main.obj  ──┐
x86.asm   ──[NASM]──► x86.obj   ──┤
main.c    ──[wcc]───► main.obj  ──┼──[wlink]──► stage2.bin
stdio.c   ──[wcc]───► stdio.obj ──┘
```

The linker's three jobs:
1. **Symbol resolution** — patches `extern _cstart_` in `main.asm` to the actual address of `cstart_()` from `main.c`
2. **Section ordering** — ensures `entry:` is at byte 0 of the output binary (so the far jump lands in the right place)
3. **Output format** — produces `FORMAT RAW BIN`: a flat binary with no file headers, since there is no OS to parse ELF or PE headers

### `linker.lnk`

```
FORMAT RAW BIN          ; flat bytes, no headers
OPTION NODEFAULTLIBS    ; don't link Watcom's C runtime (assumes an OS exists)
OPTION START=entry      ; entry: label goes at byte 0
OPTION OFFSET=0         ; addresses start at 0 (matching KERNEL_LOAD_OFFSET=0)

ORDER
    CLNAME CODE
        SEGMENT _ENTRY  ; main.asm's section — must be first (contains entry:)
        SEGMENT _TEXT   ; all C code (wcc puts it here by default)
    CLNAME DATA         ; global variables, string literals
```

`CLNAME` and `SEGMENT` are wlink keywords. `CODE` and `DATA` are class names you define — they're grouping labels. `_ENTRY` and `_TEXT` are section names that must match what's declared in the source files:

```asm
section _ENTRY class=CODE   ; in main.asm
section _TEXT  class=CODE   ; in x86.asm
```

Watcom's C compiler puts all compiled C into `_TEXT class=CODE` automatically — no declaration needed in `.c` files.

The `extern`/`global` keywords bridge the ASM and linker worlds:
- `global entry` — exposes the `entry:` label so the linker can place it at byte 0
- `extern _cstart_` — promises the assembler this symbol exists in another object file; the linker will resolve it

### Segment Register Reset

When stage1's far jump lands at `0x2000:0x0000`, only `CS` is correctly set to `0x2000`. `DS`, `SS`, and `ES` still hold whatever stage1 left in them. Stage2 immediately resets them:

```asm
mov ax, cs    ; ax = 0x2000
mov ds, ax
mov ss, ax
mov sp, 0     ; stack at top of 64KB segment, grows downward
mov bp, sp
```

This is required by OpenWatcom's **small memory model** (`-ms`). In the small model, the compiler assumes `CS = DS = SS` — all near pointers are 16-bit offsets from the same segment base. If `DS ≠ SS`, then dereferencing a stack pointer (which uses `SS`) through a C pointer (which the compiler indexes via `DS`) reads the wrong physical memory.

`SP = 0` places the stack at the top of the segment: the first `push` decrements SP to `0xFFFE` and writes there. Since stage2's code is near the bottom of the 64KB segment, there is plenty of room for the stack to grow downward.

### `__cdecl` Calling Convention

Stage2 uses Watcom's 16-bit `__cdecl` convention (stack-based). It must be declared explicitly because Watcom's default uses registers, not the stack:

```c
void _cdecl cstart_(uint16_t bootDrive);
```

In `__cdecl`:
- Arguments are pushed right-to-left onto the stack
- Each argument occupies a minimum of 2 bytes (one 16-bit slot)
- The **caller** cleans up the stack after the call
- Stack frame at function entry (after `push bp; mov bp, sp`):

```
[bp + 0]  = saved bp
[bp + 2]  = return address
[bp + 4]  = first argument
[bp + 6]  = second argument
...
```

`_cdecl` must be on both the **declaration** and the **definition**. Watcom applies the calling convention from the definition — if it is missing there, the definition uses the register-based default while callers expect stack-based, causing a mismatch.

### `printf` Implementation

#### `argp` — Walking the Argument Stack

Variadic arguments in `printf` are accessed by walking the stack manually:

```c
int* argp = (int*) &fmt;
argp++;   // skip past fmt, now points to first variadic arg
```

`int*` is used because `sizeof(int) == 2` in 16-bit mode, matching the minimum stack slot size. Advancing `argp++` moves exactly 2 bytes — one slot. Wider types take proportionally more: `long` takes 4 bytes (`argp += 2`), `long long` takes 8 bytes (`argp += 4`).

#### The `%s` Case

String arguments are passed as `char*`. On the stack, that means `argp` (an `int*`) points to a slot that *contains* the address of the first character:

```
argp  →  int*        (2-byte slot on stack)
*argp →  int         (the value, which is actually a char* address)
(char*) *argp        → char*  (the string pointer)
```

Equivalently: `*(char**) argp` — cast `argp` to `char**`, then dereference to get the `char*`.

#### 64-bit Division (`x86_div64_32`)

In 16-bit real mode, there is no single instruction to divide a 64-bit number by a 32-bit number. The division is done in two 32-bit steps using `div ecx` with the 32-bit operand size prefix:

```
Step 1 (high 32 bits): eax = high, edx = 0         → div ecx → quotient_high, remainder_high
Step 2 (low 32 bits):  eax = low,  edx = remainder_high → div ecx → quotient_low,  final_remainder
```

The stack layout for `x86_div64_32(uint64_t dividend, uint32_t divisor, uint64_t* quotientOut, uint32_t* remainderOut)`:

```
[bp + 4]   low 32 bits of dividend  (pushed last = at lowest address)
[bp + 8]   high 32 bits of dividend (pushed first = at higher address)
[bp + 12]  divisor (uint32_t)
[bp + 16]  quotientOut (near ptr, 2 bytes)
[bp + 18]  remainderOut (near ptr, 2 bytes)
```

Watcom pushes the high half of a 64-bit argument first (higher address) and the low half last (lower address), consistent with little-endian memory layout.

---

## Build System

```
make
```

The Makefile (using WSL for path translation) produces `build/stage2.bin` and `build/main_floppy.img`.

```
src/bootloader/stage1/boot.asm  ──[nasm -f bin]──────────────────► bootloader.bin (512 bytes)
src/bootloader/stage2/main.asm  ──[nasm -f obj]──► main.obj  ──┐
src/bootloader/stage2/x86.asm   ──[nasm -f obj]──► x86.obj   ──┤
src/bootloader/stage2/main.c    ──[wcc -ms]──────► main.obj  ──┼──[wlink]──► stage2.bin
src/bootloader/stage2/stdio.c   ──[wcc -ms]──────► stdio.obj ──┘

dd bootloader.bin → sector 0 of main_floppy.img
mcopy stage2.bin  → STAGE2.BIN in FAT filesystem
```

Stage1 uses `-f bin` (NASM direct binary output, no linker needed — single file, no cross-file references). Stage2 uses `-f obj` (COFF intermediate objects) because multiple files must be combined.

Watcom compiler flags for stage2:
- `-ms` — small memory model (CS=DS=SS, near pointers)
- `-4` — generate 486-level instructions (enables 32-bit registers in 16-bit mode)
- `-s` — no stack overflow checks
- `-zl` — no default library references
- `-wx` — maximum warnings

---

## Memory Layout

| Address | Contents |
|---------|----------|
| `0x00000`–`0x003FF` | Interrupt Vector Table (IVT) |
| `0x00400`–`0x004FF` | BIOS Data Area |
| `0x07C00`–`0x07DFF` | Stage1 (boot sector, 512 bytes) |
| `0x07E00`–`~0x09A00` | Stage1 scratch buffer (FAT + root dir reads) |
| `0x20000`+ | Stage2 (loaded by stage1, runs here) |
| `0xA0000`+ | Video memory, BIOS ROM (off-limits) |

Stage2 has the full range from `0x20000` to `0x9FFFF` (640KB boundary) available for code, stack, and data.

---

## Register Reference

### General Purpose (16-bit Real Mode)

| Register | Name | Primary Use |
|----------|------|-------------|
| `AX` | Accumulator | Arithmetic, BIOS call arguments, return values |
| `BX` | Base | Memory addressing, general storage |
| `CX` | Counter | Loop counter, `repe`/`repne` string operations |
| `DX` | Data | I/O ports, multiply/divide overflow |
| `SI` | Source Index | Source for string operations (`lodsb`, `movsb`) |
| `DI` | Destination Index | Destination for string operations (`cmpsb`, `stosb`) |
| `SP` | Stack Pointer | Top of stack — moves automatically on `push`/`pop` |
| `BP` | Base Pointer | Stack frame anchor — `[bp+N]` addresses function arguments |

### Flags

| Flag | Set when |
|------|----------|
| `ZF` | Result was zero |
| `CF` | Arithmetic produced a carry or borrow |
| `SF` | Result was negative |
| `IF` | CPU responds to interrupts (`sti` sets, `cli` clears) |

### BIOS Interrupts Used

| Interrupt | Function | Used for |
|-----------|----------|----------|
| `INT 10h AH=0Eh` | Teletype output | Print a character to screen |
| `INT 13h AH=02h` | Read sectors | Copy sectors from floppy into RAM |
| `INT 13h AH=08h` | Get drive params | Read actual sectors/track and head count |
| `INT 13h AH=00h` | Reset disk | Reset floppy controller before retry |
| `INT 16h AH=00h` | Wait for keypress | Used before rebooting on error |

---

## Running

```
make run
```

Uses **Bochs** (x86 emulator with built-in debugger) configured via `bochs_config`. Bochs emulates the full machine: CPU, BIOS, RAM, floppy controller, and display. `main_floppy.img` is presented as a floppy disk to the emulated BIOS.
