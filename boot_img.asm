; boot_img.asm — Minimal graphical bootloader with 320x200 mode
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

    ; Clear screen (color 0 = black)
    xor di, di
    mov cx, 320*200
    mov al, 0
    mov es, 0xA000
    rep stosb

    ; Load kernel (assume sector 2 onward, 8 sectors for demo)
    mov bx, 0x0000       ; VGA memory offset (not used)
    mov si, 2            ; first sector after bootloader
    mov cx, 8            ; number of sectors to read
.load_loop:
    push cx
    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov dh, 0
    mov dl, 0x80
    mov cl, si
    mov es, 0x0000
    mov di, 0x8000
    int 0x13
    jc .disk_error
    inc si
    pop cx
    loop .load_loop

    ; Jump to kernel at 0x0000:0x8000
    jmp 0x0000:0x8000

.disk_error:
    mov si, err
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

err db "Disk read error!",0

; Pad to 512 bytes
times 510-($-$$) db 0
dw 0xAA55
