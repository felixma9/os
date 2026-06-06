# OS from Scratch

A learning project building an OS from the ground up, starting with a bootloader in x86 assembly.

---

## How Booting Works

### 1. CPU Powers On → BIOS Runs

The instant the CPU powers on, it jumps to **BIOS** (Basic Input/Output System) — firmware code burned onto a chip on the motherboard. The BIOS is essentially the "OS before the OS": it runs before anything on disk is loaded.

The BIOS's jobs:
- Initialize hardware (CPU, RAM, buses, peripherals)
- Provide basic services via software interrupts (`INT 10h` for screen output, `INT 13h` for disk reads)
- Find and launch a bootloader

### 2. BIOS Finds the Bootloader

The BIOS reads **sector 0** (the very first 512 bytes) of the boot disk. It checks the last 2 bytes of that sector for the magic number `0xAA55`. If found, the sector is considered a valid boot sector.

The BIOS then:
- Copies those 512 bytes into RAM at address **`0x7C00`**
- Jumps to `0x7C00` — the CPU begins executing the bootloader code

### 3. Bootloader Runs (`boot.asm`)

The bootloader is our code. It lives in `src/bootloader/boot.asm` and is assembled into a 512-byte binary.

The 512-byte constraint is enforced at the bottom of `boot.asm`:
```asm
times 510-($-$$) db 0    ; pad with zeros to reach 510 bytes
dw 0xAA55                ; magic number in the final 2 bytes
```
`$-$$` is the actual assembled byte count, so NASM will error if the code exceeds 510 bytes.

The bootloader is also a valid **FAT12** formatted sector — it contains a BIOS Parameter Block (BPB) describing the floppy geometry so the disk can be mounted as a standard filesystem.

What the bootloader currently does:
1. Initializes CPU segments (`ds`, `es`, `ss`) and stack pointer
2. Saves the drive number passed by the BIOS in `DL`
3. Reads 1 sector from LBA 1 into RAM at `0x7E00` using BIOS `INT 13h`
4. Prints "Hello world!" to screen
5. Halts

### 4. Disk Reads (BIOS INT 13h)

The CPU has no direct access to disk — everything that executes must be in RAM first. BIOS `INT 13h` is the service that copies sectors from disk into RAM.

The bootloader uses two helpers for this:
- **`lba_to_chs`** — converts a logical sector number (LBA) into the Cylinder/Head/Sector format that `INT 13h` requires
- **`disk_read`** — calls `INT 13h` with retry logic (3 attempts, resets controller on failure)

When the bootloader reads LBA 1 into `0x7E00`, it is reading the first sector of the FAT1 table (not the kernel yet).

### 5. Kernel Placeholder (`main.asm`)

`src/kernel/main.asm` is a stub kernel. It currently just prints "Hello world!" and halts. It is not yet loaded or executed by the bootloader — it exists as the starting point for what will eventually run after the bootloader hands off control.

**Known issue:** `main.asm` currently uses `org 0x7C00` (the bootloader's address). This must be changed to match wherever the bootloader will load it in RAM before the handoff can work.

---

## Disk Image Layout

The build produces `main_floppy.img` — a 1.44MB FAT12 floppy image:

| Disk Offset | Sector(s) | Contents                        |
|-------------|-----------|----------------------------------|
| `0x0000`    | 0         | Boot sector (bootloader code)    |
| `0x0200`    | 1–9       | FAT1 (File Allocation Table)     |
| `0x4800`    | 10–18     | FAT2 (backup copy)               |
| `0x9000`    | 19–32     | Root directory entries           |
| `0xB200`+   | 33+       | File data area (`kernel.bin`)    |

`kernel.bin` lives in the data area around sector 33, placed there by `mcopy` during the build.

---

## Memory Layout at Boot

| Address       | Contents                                  |
|---------------|-------------------------------------------|
| `0x7C00`      | Bootloader (loaded by BIOS)               |
| `0x7E00`      | Disk read buffer (1 sector read here)     |

---

## Build

```
make
```

Produces `build/main_floppy.img`. The Makefile:
1. Assembles `boot.asm` → `bootloader.bin` (512 bytes raw binary)
2. Assembles `main.asm` → `kernel.bin` (512 bytes raw binary)
3. Creates a blank 1.44MB image, formats it as FAT12
4. Writes `bootloader.bin` directly to byte 0 of the image (`dd`)
5. Copies `kernel.bin` into the FAT filesystem (`mcopy`)

The BIOS doesn't need to be told where the bootloader is — by convention it always reads sector 0. Writing the bootloader to byte 0 of the image is sufficient.

---

## Running

Uses **Bochs** (x86 emulator with built-in debugger) configured via `bochs_config`. Bochs emulates the full machine: CPU, BIOS (SeaBIOS), RAM, floppy controller, and display. The `main_floppy.img` file is presented to the emulated BIOS as a floppy disk.

---

## x86 Register Cheat Sheet

In 16-bit Real Mode (where the bootloader runs), registers are 16-bit. Each general-purpose register can be split into high and low bytes:

```
AX = [ AH | AL ]   (16-bit = 8-bit high + 8-bit low)
```

In 32-bit Protected Mode the same registers are prefixed with `E` (EAX, EBX...), and in 64-bit Long Mode with `R` (RAX, RBX...).

### General Purpose Registers

| Register | Name            | Primary Use                                      |
|----------|-----------------|--------------------------------------------------|
| `AX`     | Accumulator     | Arithmetic, return values, BIOS call arguments   |
| `BX`     | Base            | Memory base address; also used as general storage|
| `CX`     | Counter         | Loop counter (`loop` instruction decrements CX)  |
| `DX`     | Data            | I/O port addresses, multiply/divide overflow     |
| `SI`     | Source Index    | Source pointer for string/memory operations      |
| `DI`     | Destination Index | Destination pointer for string/memory operations|
| `SP`     | Stack Pointer   | Points to the top of the stack (moves automatically on `push`/`pop`) |
| `BP`     | Base Pointer    | Points to the base of the current stack frame; used to access function arguments and locals |

### Segment Registers

In Real Mode, memory addresses are formed as `segment * 16 + offset`, allowing access to 1MB of RAM with 16-bit registers.

| Register | Name            | Use                                              |
|----------|-----------------|--------------------------------------------------|
| `CS`     | Code Segment    | Segment of currently executing code              |
| `DS`     | Data Segment    | Default segment for data reads/writes            |
| `SS`     | Stack Segment   | Segment where the stack lives                    |
| `ES`     | Extra Segment   | Extra segment, used by string instructions and BIOS calls |
| `FS`,`GS`| —               | Additional segments (32/64-bit only)             |

The bootloader sets `DS = ES = SS = 0` and `SP = 0x7C00` so the stack grows downward from the bootloader's base address.

### Special Purpose Registers

| Register | Name                | Use                                            |
|----------|---------------------|------------------------------------------------|
| `IP`     | Instruction Pointer | Address of the **next instruction** to execute. Cannot be set directly — only changed via `jmp`, `call`, `ret`, interrupts |
| `FLAGS`  | Flags Register      | Status bits set automatically by instructions  |

Common flags in the FLAGS register:

| Flag | Name       | Set when...                              |
|------|------------|------------------------------------------|
| `ZF` | Zero       | Result of last operation was zero        |
| `CF` | Carry      | Arithmetic produced a carry/borrow       |
| `SF` | Sign       | Result was negative                      |
| `OF` | Overflow   | Signed arithmetic overflowed             |
| `IF` | Interrupt  | CPU will respond to hardware interrupts (`sti` sets it, `cli` clears it) |

### How the Stack Works

The x86 stack grows **downward** in memory. `SP` starts high and decreases.

```
push ax   →   SP -= 2 ; memory[SS:SP] = AX
pop  ax   →   AX = memory[SS:SP] ; SP += 2
```

`call` pushes the return address (next `IP`) onto the stack; `ret` pops it back into `IP`.

### BIOS Calls (INT)

BIOS services are invoked by loading arguments into registers then calling a software interrupt. For example, the disk read your bootloader uses:

```asm
mov ah, 0x02   ; function: read sectors
mov al, 1      ; number of sectors
mov ch, 0      ; cylinder
mov cl, 2      ; sector (1-indexed)
mov dh, 0      ; head
mov dl, 0      ; drive number (0 = floppy A)
mov bx, 0x7E00 ; ES:BX = destination buffer
int 0x13       ; call BIOS disk service
```

---

## What's Next

1. Bootloader parses FAT12 to locate `kernel.bin` by filename
2. Bootloader loads `kernel.bin` into RAM at a chosen address
3. `main.asm` updated with the correct `org` address
4. Bootloader jumps to the kernel
5. Eventually: switch CPU from 16-bit Real Mode to 32-bit Protected Mode, then write the kernel in C
