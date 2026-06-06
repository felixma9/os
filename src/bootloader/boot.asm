org 0x7C00
bits 16

%define ENDL 0x0D, 0x0A

; FAT12 header
jmp short start
nop

bdb_oem:                    db "NBOS    " ; OEM name, 8 bytes
bdb_bytes_per_sector:       dw 512
bdb_sectors_per_cluster:    db 1
bdb_reserved_sectors:       dw 1
bdb_fat_count:              db 2
dbd_dir_entries_count:      dw 0E0h
bdb_total_sectors:          dw 2880
bdb_media_descriptor_type:  db 0F0h
bdb_sectors_per_fat:        dw 9
bdb_sectors_per_track:      dw 18
bdb_heads:                  dw 2
bdb_hidden_sectors:         dd 0
bdb_large_sector_count:     dd 0

ebr_drive_number:           db 0
                            db 0
ebr_signature:              db 29h
ebr_volume_id:              db 12h, 34h, 56h, 78h
ebr_volume_label:           db 'FELIX OS   '
ebr_system_id:              db 'FAT12   '



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

    ; read something from floppy disk
    ; BIOS should set dl to drive number
    mov [ebr_drive_number], dl

    mov ax, 1        ; LBA address to read from, second sector in this case
    mov cl, 1        ; number of sectors to read
    mov bx, 0x7E00   ; memory address to read into, should be after bootloader
    call disk_read


    ; load si with address of string
    mov si, msg_hello
    call puts

    cli          ; disable interrupts before halting
    hlt

floppy_error:
    mov si, msg_read_failed
    call puts
    jmp wait_key_and_reboot

wait_key_and_reboot:
    mov ah, 0
    int 16h      ; wait for key press
    jmp 0FFFFh:0 ; reboot by jumping to the reset vector

.halt:
    cli          ; disable interrupts
    hlt

; Disk routines

; Converts an LBA address to a CHS address
; Params:
;   ax: LBA address to convert
; Returns:
;   cx[0:5] = sector number
;   cx[6:15] = cylinder
;   dh = head
lba_to_chs:
    push ax
    push dx

    xor dx, dx                          ; dx = 0, since we only need ax / y (no upper bits needed)
                                        ; div y -> dx:ax / y
                                        ; div writes quotient into ax, remainder into dx

    div word [bdb_sectors_per_track]    ; ax = LBA / sectors_per_track
                                        ; dx = LBA % sectors_per_track

    inc dx                              ; dx + 1, -> sector (1-based)
    mov cx, dx                          ; save sector in CL (bits 0-5)

    xor dx, dx                          ; dx = 0
    div word [bdb_heads]                ; ax = (LBA / sectors_per_track) / heads = cylinder
                                        ; dx = (LBA / sectors_per_track) % heads = head

    mov dh, dl                          ; dh = head
                                        ; cx is a litte weird, it should be formatted like:
                                        ; cx[0:5] = sector number
                                        ; cx[6:15] = cylinder number
                                        ; this can be done by modifying ch and cl
                                        
    mov ch, al                          ; ch = lower 8 bits of cylinder
    shl ah, 6                           ; ah << 6
    and cl, 3Fh                         ; cl[6:7] = 0, clear upper 2 bits
                                        ; and cl  with 0011 1111
    or  cl, ah                          ; cl[6:7] = lower 2 bits of cylinder

    pop ax
    mov dl, al                          ; Restore dl
    pop ax
    ret

; Reads sectors from disk
; Parameters:
;   ax: LBA address
;   cl: number of sectors to read (up to 128)
;   dl: drive number
;   es:bx: memory address to store data into
disk_read:
    push ax
    push bx
    push cx
    push dx
    push di

    push cx             ; save cx since lba_to_chs modifies it
    call lba_to_chs     ; convert LBA to CHS, results in cx and dh being set
    pop ax              ; al = number of sectors to read

    mov ah, 0x02        ; BIOS read sectors function
    mov di, 3           ; number of times to retry the read, in case of failure

.retry:
    pusha               ; push all registers, don't know which registers BIOS modifies
    stc                 ; set carry flag, carry cleared = success = jump out of loop
    int 13h             ; Call BIOS interrupt to read sectors
    jnc .done

    ; failed
    popa                ; restore registers before retrying
    call disk_reset     ; reset disk, in case of error

    dec di
    test di, di
    jnz .retry          ; if di != 0, retry

.fail:
    ; If we get here all attemps were exhausted
    jmp floppy_error

.done:
    popa

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Resets disk controller
; Parameters: 
;   dl: drive number
disk_reset:
    pusha
    mov ah, 0x00        ; BIOS reset disk function
    stc
    int 13h
    jc floppy_error
    popa
    ret

 
msg_hello: db "Hello world!", ENDL, 0
msg_read_failed: db 'Failed to read from disk', ENDL, 0

times 510-($-$$) db 0
dw 0AA55h