bits 16

section _TEXT class=CODE

; Stage2 is always loaded at this fixed physical address (memdefs.h has no
; equivalent define for asm, so it's repeated here). Needed below because once
; CR0.PE is set, the CPU stops doing segment:offset translation -- any address
; used by the CPU directly as a linear address (the GDT base, and the far
; jump's offset into 32-bit code) must be the real physical address, not an
; offset relative to this file's start.
STAGE2_PHYSICAL_BASE equ 0x20000

; Where the kernel was loaded by FAT_Read (memdefs.h: MEMORY_KERNEL_ADDR).
KERNEL_PHYSICAL_ADDR equ 0x30000

global _x86_EnterProtectedModeAndJumpToKernel
_x86_EnterProtectedModeAndJumpToKernel:

    ; --- Enable the A20 line ---
    ; Try the BIOS function first; fall back to the fast A20 gate (port 0x92)
    ; if the BIOS doesn't support it.
    mov ax, 0x2401
    int 0x15
    jnc .a20_done

    in al, 0x92
    or al, 2
    out 0x92, al
.a20_done:

    ; --- Load the flat GDT ---
    lgdt [gdt_descriptor]

    ; --- Switch to protected mode ---
    cli                         ; no interrupt table yet in protected mode, interrupts would crash
    mov eax, cr0                
    or eax, 1                   ; set the PE bit
    mov cr0, eax                ; set bit 0 of CR0, now we're in protected mode!!!

    ; Far jump to flush the prefetch queue and load CS with the new 32-bit
    ; code selector. The offset must be a real physical address (see
    ; STAGE2_PHYSICAL_BASE comment above), not a file-relative label.
    jmp dword 0x08:(protected_mode_entry + STAGE2_PHYSICAL_BASE)

bits 32
protected_mode_entry:
    ; Reload the rest of the segment registers with the flat data selector.
    ; Stale real-mode segment values are a guaranteed crash on first access.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Jump into the kernel. KERNEL_PHYSICAL_ADDR is already a flat physical
    ; address, so no adjustment is needed here (unlike protected_mode_entry
    ; above, which lives inside this file and needed STAGE2_PHYSICAL_BASE).
    ; Indirect through a register: a plain "jmp KERNEL_PHYSICAL_ADDR" would be
    ; encoded as a near *relative* jump, but this code's own final position is
    ; relocatable (resolved by the linker), so NASM can't compute a fixed
    ; displacement to a literal absolute address at assemble time.
    mov eax, KERNEL_PHYSICAL_ADDR
    jmp eax

bits 16

; --- Flat GDT: null, 32-bit flat code, 32-bit flat data ---
gdt_start:
    dq 0x0000000000000000      ; null descriptor

gdt_code:                      ; selector 0x08
    dw 0xFFFF                  ; limit (low)
    dw 0x0000                  ; base (low)
    db 0x00                    ; base (mid)
    db 10011010b                ; access: present, ring 0, code, exec/read
    db 11001111b                ; flags (4K granularity, 32-bit) + limit (high)
    db 0x00                    ; base (high)

gdt_data:                      ; selector 0x10
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b                ; access: present, ring 0, data, read/write
    db 11001111b
    db 0x00
gdt_end:

; lgdt command loads the global descriptor table pointer with the following struct
; note that lgdt loads from a 6b structure, 2b for limit, 4b for base address
gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; GDT size, minus 1, per LGDT's spec
    dd gdt_start + STAGE2_PHYSICAL_BASE  ; GDT base must be a physical address
