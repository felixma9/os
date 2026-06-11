bits 16

section _ENTRY class=CODE

; extern = "this function not defined here, but continue anyway, linker will fill it in"
extern _cstart_
; global = opposite of extern - "this function is defined here, let other files use it"
global entry

; we far jumped to here, meaning cs (code segment) is correctly set to 0x2000
; however, watcom requires that all segments == one another here, so we reset
; all segments (we should do this anyway after a large jump)

; note that in 16b real mode, addresses are NOT segment:offset, rather they are
; segment * 16 + offset (see boot.asm's trick with far jump to reset cs to 0x0000)

entry:
    cli
    mov ax, cs          ; we jumped here with a far jump, meaning cs = 0x2000
    mov ds, ax          ; reset all the other segment registers to be consistent
    mov ss, ax
    ; mov ax, dx
    ; mov ss, ax
    mov sp, 0
    mov bp, sp
    sti

    ; expect boot drive in dl, send it as arg to cstart
    xor dh, dh
    push dx
    call _cstart_

    cli 
    hlt