bits 32
section .text

extern isr_handler

; Exceptions without an error code: push dummy 0 to keep frame layout uniform
%macro ISR_NO_ERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

; Exceptions that push an error code: CPU already did it, just push the vector
%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common_stub
%endmacro

ISR_NO_ERR 0   ; divide by zero
ISR_NO_ERR 1   ; debug
ISR_NO_ERR 2   ; NMI
ISR_NO_ERR 3   ; breakpoint
ISR_NO_ERR 4   ; overflow
ISR_NO_ERR 5   ; bound range exceeded
ISR_NO_ERR 6   ; invalid opcode
ISR_NO_ERR 7   ; device not available
ISR_ERR    8   ; double fault
ISR_NO_ERR 9   ; coprocessor segment overrun (legacy)
ISR_ERR    10  ; invalid TSS
ISR_ERR    11  ; segment not present
ISR_ERR    12  ; stack segment fault
ISR_ERR    13  ; general protection fault
ISR_ERR    14  ; page fault
ISR_NO_ERR 15
ISR_NO_ERR 16  ; x87 floating point
ISR_ERR    17  ; alignment check
ISR_NO_ERR 18  ; machine check
ISR_NO_ERR 19  ; SIMD floating point
ISR_NO_ERR 20  ; virtualization
ISR_ERR    21  ; control protection
ISR_NO_ERR 22
ISR_NO_ERR 23
ISR_NO_ERR 24
ISR_NO_ERR 25
ISR_NO_ERR 26
ISR_NO_ERR 27
ISR_NO_ERR 28
ISR_NO_ERR 29
ISR_ERR    30  ; security exception
ISR_NO_ERR 31

isr_common_stub:
    pusha               ; push eax, ecx, edx, ebx, esp, ebp, esi, edi

    push ds
    push es
    push fs
    push gs

    mov ax, 0x10        ; switch to kernel data segment in case we came from elsewhere
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp            ; pass pointer to the saved frame as argument
    call isr_handler
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds

    popa
    add esp, 8          ; discard vector and error code
    iret

; Table of stub addresses so isr_init() can register them without 32 extern declarations
section .data
global isr_stub_table
isr_stub_table:
    dd isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7
    dd isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15
    dd isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dd isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
