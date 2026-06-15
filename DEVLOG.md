# Devlog

A running log of what was built, what broke, and what clicked.

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
