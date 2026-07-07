bits 32

section .text
extern kernel_main
global _start
global flush_gdt

_start:
    mov esp, stack_top
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

flush_gdt:
    ; new stack
    push ebp
    mov ebp, esp

    ; save eax since we use it for ldgt
    push eax

    mov eax, [ebp + 8]
    lgdt [eax]

    ; perform a far jump to load cs and other segment regs
    push dword 0x08
    push dword .flush_after     ; retf pops into EIP then CS

    retf

.flush_after:
    ; cs is now 0x08, set other seg regs to 0x10
    mov ax, 0x10
    mov ds, ax      ; though we're in 32 bit mode, segment regs only accept
    mov es, ax      ; 16 bit source
    mov ss, ax
    mov fs, ax
    mov gs, ax

    pop eax
    mov esp, ebp
    pop ebp
    ret

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
