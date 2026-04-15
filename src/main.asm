org 0x7C00
bits 16

%DEFINE ENDL 0x0D, 0x0A

start:
    jmp main

; Prints a string to the screen
; Params:
;   ds:si is the address of the null-terminated string to print
puts:
    ; Push the registers we will use onto the stack to preserve their values
    push si
    push ax

.loop:          ; Period makes loop local to puts function
    lodsb       ; load string byte, loads ds:si into al, then si++
    or al, al   ; bit-wise or on al, doesn't change, value, but can change the zero flag
                ; if zero flag is set, we've reached the end of the string
    jz .done    ; Jump to .done if zero flag is set

    ; Setup for interrupt call to print to screen
    mov ah, 0x0E
    mov bh, 0
    int 0x10    ; Print as TTY expects these values

    jmp .loop

.done:
    pop ax
    pop si      ; restore the registers that puts modified
    ret

main:
    ; setup data segments
    mov ax, 0
    mov ds, ax
    mov es, ax

    ; setup stack
    mov ss, ax
    mov sp, 0x7C00

    ; load si with address of string
    mov si, msg_hello
    call puts

    hlt

.halt:
    jmp .halt

msg_hello: db "Hello world!", ENDL, 0

times 510-($-$$) db 0
dw 0AA55h