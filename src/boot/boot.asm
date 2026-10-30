BITS 16
ORG 0x7C00

STAGE2_SECTORS equ 8

boot_start:
    jmp short boot_entry
    nop
    db 'VSDOS   '
    dw 512
    db 1
    dw 9
    db 2
    dw 224
    dw 2880
    db 0xF0
    dw 9
    dw 18
    dw 2
    dd 0
    dd 0
    db 0
    db 0x29
    dd 0x20261012
    db 'VERYSTUPIDOS'
    db 'FAT12   '

boot_entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl
    cld
    sti

    mov ax, 0x0003
    int 0x10

    mov si, boot_message
    call print_string

    cmp byte [boot_drive], 0x80
    jae .read_hard_disk

    mov byte [retry_count], 3
.read_stage2:
    mov ax, 0x0208
    mov bx, 0x8000
    mov cx, 0x0002
    xor dh, dh
    mov dl, [boot_drive]
    int 0x13
    jnc .stage2_loaded
    dec byte [retry_count]
    jz boot_error
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    jmp .read_stage2

.read_hard_disk:
    mov bx, 0x55AA
    mov ah, 0x41
    mov dl, [boot_drive]
    int 0x13
    jc boot_error
    cmp bx, 0xAA55
    jne boot_error
    test cx, 1
    jz boot_error
    mov si, disk_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc boot_error
    jmp .stage2_loaded

.stage2_loaded:
    jmp 0x0000:0x8000

boot_error:
    mov si, error_message
    call print_string
.halt:
    cli
    hlt
    jmp .halt

print_string:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_string
.done:
    ret

boot_drive db 0
retry_count db 0
align 4
disk_packet:
    db 0x10
    db 0
    dw STAGE2_SECTORS
    dw 0
    dw 0x0800
    dq 1
boot_message db 'VSDOS FAT-12 BOOT', 13, 10, 0
error_message db "ERROR: CAN'T LOAD DISK", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
