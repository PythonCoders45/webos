; boot_img.asm — Bootloader that loads 320x200 image from disk
BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Set 320x200 256-color mode
    mov ax, 0x0013
    int 0x10

    ; Load image from disk (320*200 = 64000 bytes => 125 sectors)
    mov bx, 0x0000       ; VGA memory offset
    mov si, 2            ; first sector after bootloader
    mov cx, 125          ; number of sectors to read
.load_loop:
    push cx
    mov ah, 0x02         ; BIOS read sectors
    mov al, 1            ; read 1 sector
    mov ch, 0            ; cylinder
    mov dh, 0            ; head
    mov dl, 0x80         ; first HDD
    mov cl, si           ; sector number
    mov es, 0xA000
    mov di, bx
    int 0x13
    jc .disk_error
    add bx, 512
    inc si
    pop cx
    loop .load_loop

    ; Wait a key press
    xor ah, ah
    int 0x16

    ; Jump to kernel at 0x8000
    jmp 0x0000:0x8000

.disk_error:
    ; Print simple error
    mov si, diskerr
.print_char:
    lodsb
    cmp al, 0
    je .hang
    mov ah, 0x0E
    int 0x10
    jmp .print_char
.hang:
    hlt
    jmp .hang

diskerr db "Disk read error!",0

; Pad to 512 bytes
times 510-($-$$) db 0
dw 0xAA55
