bits 16

section _TEXT class=CODE

; U4D (division)
global __U4D
__U4D:
    shl edx, 16
    mov dx, ax
    mov eax, edx
    xor edx, edx

    shl ecx, 16
    mov cx, bx

    div ecx
    mov ebx, edx
    mov ecx, edx
    shr ecx, 16

    mov edx, eax
    shr edx, 16

    ret

; U4M (multiplication)
; in:  dx:ax = first operand (high:low), cx:bx = second operand (high:low)
; out: dx:ax = 32-bit product (high:low), truncated to 32 bits
global __U4M
__U4M:
    shl edx, 16
    mov dx, ax
    mov eax, edx        ; eax = first operand (full 32 bits)

    shl ecx, 16
    mov cx, bx
    mov ebx, ecx        ; ebx = second operand (full 32 bits)

    mul ebx             ; edx:eax = eax * ebx (64-bit product); only the low 32 bits (eax) matter here

    mov edx, eax
    shr edx, 16         ; dx = high 16 bits of the 32-bit product (ax already holds the low 16 bits)

    ret

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

global _x86_mul64_32
_x86_mul64_32:

    ; make new call frame
    push bp             ; save old call frame
    mov bp, sp          ; init new call frame

    push bx

    ; multiply upper 32 bits
    mov eax, [bp + 8]   ; eax <- upper 32 b of arg0
    mov ecx, [bp + 12]  ; ecx <- arg1
    xor edx, edx
    mul ecx

    ; store upper 32b of quotient
    mov ebx, [bp + 16]
    mov [bx + 4], eax

    ; multiple lower 32 bits
    mov eax, [bp + 4]   ; eax <- lower 32b of arg0
                        ; edx <- old remainder
    mul ecx

    ; store results
    mov [bx], eax
    mov bx, [bp + 18]
    mov [bx], edx

    pop bx

    ; restore old call frame
    mov sp, bp
    pop bp
    ret

; bool _cdecl x86_Disk_Reset(uint8_t drive);
global _x86_Disk_Reset
_x86_Disk_Reset:

    ; make new call frame
    push bp             ; save old call frame
    mov bp, sp          ; init new call frame

    mov dl, [bp + 4]    ; dl <- drive (this interrupt uses dl for arg)
    stc                 ; set carry
    int 13h

    mov ax, 1
    sbb ax, 0           ; 1 on success, 0 on fail

    ; restore old call frame
    mov sp, bp
    pop bp
    ret

; bool _cdecl x86_Disk_Read(uint8_t drive,
;                           uint16_t cylinder,
;                           uint16_t sector,
;                           uint16_t head,
;                           uint8_t count,
;                           uint8_t far* dataOut);

; [bp+4]  = drive
; [bp+6]  = cylinder
; [bp+8]  = sector
; [bp+10] = head
; [bp+12] = count
; [bp+14] = dataOut (offset half)
; [bp+16] = dataOut (segment half)

global _x86_Disk_Read
_x86_Disk_Read:

    ; make new call frame
    push bp             ; save old call frame
    mov bp, sp          ; init new call frame

    ; save modified registers
    push bx
    push es

    ; setup args
    mov dl, [bp + 4]    ; dl = drive

    mov ch, [bp + 6]    ; ch = low 8 bits of cylinder
    mov cl, [bp + 7]    ; if cylinder = FEDCBA:76543210, this puts A-F into cl
                        ; our target is to have cl be BAxxxxxx
                        ; so now cl is FEDCBA, shift left 6 to get BAxxxxxx
    shl cl, 6           ; cl = cylinder bits 6-7

    mov dh, [bp + 10]    ; dh = head

    mov al, [bp + 8]   ; we're using al as an intermediary here, to get upper 2 cylinder bits
    and al, 3Fh         ; 0b00111111, clears top 2 bits of al, now al = FExxxxxx
    or cl, al           ; load the upper two bits into cl

    mov al, [bp + 12]   ; al = count

    mov bx, [bp + 16]   ; es = segment half of far pointer
    mov es, bx
    mov bx, [bp + 14]   ; bx = offset half of far pointer

    ; call interrupt
    mov ah, 02h
    stc                 ; set carry
    int 13h

    mov ax, 1
    sbb ax, 0           ; 1 on success, 0 on fail

    ; restore args, in accordance with cdecl
    pop es
    pop bx

    ; restore old call frame
    mov sp, bp
    pop bp
    ret


; // Better to read params from disk directly instead of from our assembly code in case there's some issue with
; // our hard-coded values
; bool _cdecl x86_Disk_GetDriveParams(uint8_t drive,
;                                     uint8_t* driveTypeOut,
;                                     uint16_t* cylindersOut,
;                                     uint16_t* sectorsOut,
;                                     uint16_t* headsOut);
; void _cdecl x86_GetMemoryMap();
; Calls INT 0x15/E820 to query the BIOS memory map, storing results at
; physical 0x500 so the kernel can read them after the mode switch:
;   [0x500]  uint32_t count          -- number of valid entries
;   [0x504]  entry[0..count-1]       -- each entry is 20 bytes:
;              uint64_t base
;              uint64_t length
;              uint32_t type         -- 1 = usable RAM
; The FAT driver previously owned this region; by the time this is called
; FAT_Read has finished and the space is free to repurpose.
global _x86_GetMemoryMap
_x86_GetMemoryMap:
    push bp
    mov bp, sp
    push es
    push bx
    push di
    push si

    xor ax, ax
    mov es, ax                      ; ES = 0x0000 → ES:DI addresses physical memory
    mov dword [es:0x500], 0         ; zero out the entry count

    xor ebx, ebx                    ; EBX = 0 starts the e820 query
    mov di, 0x504                   ; first entry goes at physical 0x504

.e820_loop:
    mov eax, 0xE820
    mov ecx, 20                     ; entry size
    mov edx, 0x534D4150             ; 'SMAP' signature
    int 0x15
    jc .e820_done                   ; CF set = unsupported or end of list
    cmp eax, 0x534D4150             ; BIOS must echo 'SMAP'
    jne .e820_done

    inc dword [es:0x500]            ; count++
    add di, 20                      ; advance to next entry slot

    test ebx, ebx                   ; EBX = 0 after last entry
    jz .e820_done
    jmp .e820_loop

.e820_done:
    pop si
    pop di
    pop bx
    pop es
    mov sp, bp
    pop bp
    ret

global _x86_Disk_GetDriveParams
_x86_Disk_GetDriveParams:

    ; make new call frame
    push bp
    mov bp, sp

    ; save regs
    push es
    push bx
    push si
    push di

    ; call int13h
    mov dl, [bp + 4]    ; dl = disk drive
    mov ah, 08h
    mov di, 0           ; es:di = 0000:0000
    mov es, di
    stc
    int 13h

    ; return
    mov ax, 1
    sbb ax, 0

    ; out params
    mov si, [bp + 6]    ; drive type from bl
    mov [si], bl

    mov bl, ch          ; lower bits in ch
    mov bh, cl          ; upper bits in cl
    shr bh, 6
    mov si, [bp + 8]
    mov [si], bx

    xor ch, ch          ; sectors - lower 5 bits in cl
    and cl, 3Fh
    mov si, [bp + 10]
    mov [si], cx

    mov cl, dh          ; heads = dh
    inc cx
    mov si, [bp + 12]
    mov [si], cx

    ; restore args
    pop di
    pop si
    pop bx
    pop es

    ; restore old call frame
    mov sp, bp
    pop bp
    ret

