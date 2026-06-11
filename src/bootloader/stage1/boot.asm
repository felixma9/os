org 0x7C00
bits 16

%define ENDL 0x0D, 0x0A

; Reference the BPB table at https://wiki.osdev.org/FAT 

; FAT12 header
jmp short start     ; 2 bytes
nop                 ; 1 byte

; FAT specs say use the first 3 bytes to jump over the table

; We're now at offset 3 bytes into the disk, this is where fields are declared

; db = define byte,       1 byte, used for strings, "use one byte per char"
; dw = define word,       2 bytes
; dd = define doubleword, 4 bytes


bdb_oem:                    db "NBOS    " ; OEM name, 8 bytes
bdb_bytes_per_sector:       dw 512
bdb_sectors_per_cluster:    db 1
bdb_reserved_sectors:       dw 1
bdb_fat_count:              db 2
bdb_dir_entries_count:      dw 0E0h
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

; At this point, we are at offset 62, where 448 bytes are used as the boot code

start:
    ; setup data segments
    ; we want to be at segment 0!
    mov ax, 0
    mov ds, ax
    mov es, ax

    ; setup stack
    mov ss, ax
    mov sp, 0x7C00

    ; some BIOSes might start at 0x7C0:0000 instead of 0000:0x7C00, so make sure
    ; we are in the expected location
    
    ; in 16b real mode, addresses are NOT segment:offset
    ; they are segment * 16 + offset
    ; therefore, when the BIOS this into memory, it can do so at either:
    ;       0x7c0:0000, or 0000:0x7c00
    ; we want the second option, since we have 'org 0x7c00', which means that labels 
    ; in this file will have an offset of 0x7c00 applied to them
    ;       if we had the first option, then a label of 0x7c08 would be calculated as:
    ;           0x7c0 * 16 + 0x7c08 -> goes way off into random memory

    ; retf pops 2 values off the stack, ip and then cs
    ; therefore, we can load the cs:ip with whatever we want
    ; we manually set cs to 0x0000, and ip to .after, which we then jump to
    ;       note that near jump only changes ip
    push es
    push word .after
    retf

.after:
    ; read something from floppy disk
    ; BIOS should set dl to drive number
    mov [ebr_drive_number], dl

    ; show loading message
    mov si, msg_loading
    call puts

    ; read drive parameters
    push es
    mov ah, 08h
    int 13h
    jc floppy_error
    pop es

    ; the BIOS 'asks the hardware' for the shape of the drive
    ;       returns sectors_per_track and heads in cl and dh respectively
    ;       replace our hardcoded values with these dynamic values
    and cl, 0x3F                        ; remove top 2 bits, since sectors_per_track shares cl with overflow bits
    xor ch, ch
    mov [bdb_sectors_per_track], cx     ; sector count

    inc dh
    mov [bdb_heads], dh                 ; head count

    ; read FAT root directory
    ; compute lba of root dir in sectors, reserved + fats * sectors_per_fat
    ; this section can be hardcoded
    mov ax, [bdb_sectors_per_fat]       
    mov bl, [bdb_fat_count]
    xor bh, bh                          ; bdb_fat_count is only 1 byte, bh could be junk; reset bh to be safe
    mul bx                              ; dx:ax = ax * bx, ax = (fats * sectors_per_fat)
    add ax, word [bdb_reserved_sectors]      ; ax += reserved_sectors, ax = lba of root dir
    push ax

    ; compute size of root dir in sectors = (32 * number_of_entries) / bytes_per_sector
    mov ax, [bdb_dir_entries_count]
    shl ax, 5                           ; ax *= 32
    xor dx, dx                          ; dx = 0
    div word [bdb_bytes_per_sector]     ; number of sectors to read, divide dx:ax by operand, quotient -> ax, remainder -> dx

    test dx, dx                         ; if dx (remainder) != 0, add 1
    jz .root_dir_after
    inc ax                              ; dvision remainder != 0, add 1
                                        ; this means we have a sector only partially filled with entries
.root_dir_after:
    ; read root directory
    mov cl, al                          ; cl = number of sectors to read = size of root dir
    pop ax                              ; lba of root dir
    mov dl, [ebr_drive_number]          ; dl = drive number
    mov bx, buffer                      ; es:bx = offset
    call disk_read

    ; search for stage2.bin
    xor bx, bx
    mov di, buffer

.search_kernel:
    mov si, file_stage2_bin
    mov cx, 11                          ; compare up to 11 chars
    push di
    repe cmpsb                          ; compare string bytes, compare two bytes
                                        ;   repe = repeat while equal or until cx reaches 0, cx decremented each time
                                        ;   one in ds:si, other in es:di
    pop di
    je .found_stage2

    add di, 32                          ; jump di to next directory
    inc bx
    cmp bx, [bdb_dir_entries_count]     ; bx = how many files we've checked, see if we've checked all
    jl .search_kernel

    ; if we make it here, kernel was not found
    jmp kernel_not_found_error

.found_stage2:

    ; di points to dir entry
    mov ax, [di + 26]                   ; offset di into entry to get first cluster (refer to fat12 docs)
    mov [stage2_cluster], ax

    ; read the FAT table
    mov ax, [bdb_reserved_sectors]
    mov bx, buffer
    mov cl, [bdb_sectors_per_fat]
    mov dl, [ebr_drive_number]
    call disk_read

    ; read kernel and process FAT chain
    mov bx, STAGE2_LOAD_SEGMENT
    mov es, bx
    mov bx, STAGE2_LOAD_OFFSET

.load_kernel_loop:
    ; at this point we've found the starting address of stage2

    ; read next cluster
    mov ax, [stage2_cluster]

    ; --- hardcoded, fix later ---
    add ax, 31                          ; first cluster = (stage2_cluster - 2) * sectors_per_clister + start_sector
                                        ; start sector = reserved + fats + root_dir_size = 1 + 18 + 14 = 33
    mov cl, 1
    mov dl, [ebr_drive_number]
    call disk_read

    add bx, [bdb_bytes_per_sector]

    ; compute location of next cluster
    mov ax, [stage2_cluster]
    mov cx, 3
    mul cx
    mov cx, 2
    div cx                              ; ax = index of entry in FAT, dx = cluster mod 2

    mov si, buffer
    add si, ax
    mov ax, [ds:si]                     ; read entry from FAT table at index ax

    or dx, dx
    jz .even

.odd:
    shr ax, 4
    jmp .next_cluster_after

.even:
    and ax, 0x0FFF

.next_cluster_after:
    cmp ax, 0x0FF8                      ; in FAT, 0xFF8 indicates end of file chain
    jae .read_finish

    mov [stage2_cluster], ax
    jmp .load_kernel_loop

.read_finish:
    ; boot device in dl
    mov dl, [ebr_drive_number]

    ; set segment registers
    mov ax, STAGE2_LOAD_SEGMENT
    mov ds, ax
    mov es, ax

    ; stage2 loaded into RAM, directly jump there
    jmp STAGE2_LOAD_SEGMENT:STAGE2_LOAD_OFFSET

    ; should never reach here
    jmp wait_key_and_reboot

    cli          ; disable interrupts before halting
    hlt

floppy_error:
    mov si, msg_read_failed
    call puts
    jmp wait_key_and_reboot

kernel_not_found_error:
    mov si, msg_stage2_not_found
    call puts
    jmp wait_key_and_reboot

wait_key_and_reboot:
    mov ah, 0
    int 16h      ; wait for key press
    jmp 0FFFFh:0 ; reboot by jumping to the reset vector

.halt:
    cli          ; disable interrupts
    hlt

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
    ; If we get here all attempts were exhausted
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

 
msg_loading:          db 'Loading...', ENDL, 0
msg_read_failed:      db 'Failed to read from disk', ENDL, 0
msg_stage2_not_found: db 'STAGE2.BIN file not found!'
file_stage2_bin:      db 'STAGE2  BIN'
stage2_cluster:       dw 0

; Once stage2.bin is found, we want to load it it to 0x2000:0000
STAGE2_LOAD_SEGMENT   equ 0x2000
STAGE2_LOAD_OFFSET    equ 0

times 510-($-$$) db 0
; The specs say to put the below magic number at offset 510, so
; we pad until we read 510 and then emit the magic number
dw 0AA55h

buffer: