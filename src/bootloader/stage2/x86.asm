bits 16

section _TEXT class=CODE

;
; int 10h ah=0Eh
; args: character, page
;
;
global _x86_Video_WriteCharTeletype
_x86_Video_WriteCharTeletype:

    ; make new call frame
    push bp             ; save old call frame
    mov bp, sp          ; init new call frame

    ; save bx
    push bx

    ; [bp + 0] = old call frame
    ; [bp + 2] = return address
    ; [bp + 4] = first arg (char) -> 2 bytes (no pushing single B onto stack)
    ; [bp + 6] = second arg (page)
    mov ah, 0Eh
    mov al, [bp + 4]
    mov bh, [bp + 6]

    int 10h

    ; restore bx
    pop bx

    ; restore old call frame
    mov sp, bp
    pop bp
    ret
                        ; to the left of bp is the stack that the function uses
                        ; +0    +2        +4                   +12                 +16               +18
                        ; bp    return    uint64_t dividend    uint32_t divisor    uint64_t* qOut    uint32_t* rOut       
global _x86_div64_32
_x86_div64_32:

    ; make new call frame
    push bp             ; save old call frame
    mov bp, sp          ; init new call frame

    push bx

    ; divide upper 32 bits
    mov eax, [bp + 8]   ; eax <- upper 32 b of dividend
    mov ecx, [bp + 12]  ; ecx <- divisor
    xor edx, edx
    div ecx

    ; store upper 32b of quotient
    mov ebx, [bp + 16]
    mov [bx + 4], eax

    ; divide lower 32 bits
    mov eax, [bp + 4]   ; eax <- lower 32b of dividend
                        ; edx <- old remainder
    div ecx

    ; store results
    mov [bx], eax
    mov bx, [bp + 18]
    mov [bx], edx

    pop bx

    ; restore old call frame
    mov sp, bp
    pop bp
    ret