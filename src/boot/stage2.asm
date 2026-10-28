BITS 16
ORG 0x8000

ROOT_LBA equ 27
ROOT_SECTORS equ 14
ROOT_ENTRIES equ 224
FAT_LBA equ 9
FAT_SECTORS equ 9
DATA_LBA equ 41
KERNEL_SEGMENT equ 0x1000

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl
    cld
    sti

    mov si, loading_message
    call print_string

    mov word [disk_packet + 2], ROOT_SECTORS
    mov word [disk_packet + 4], 0
    mov word [disk_packet + 6], 0x0400
    mov dword [disk_packet + 8], ROOT_LBA
    mov dword [disk_packet + 12], 0
    call read_disk
    jc disk_error

    mov word [disk_packet + 2], FAT_SECTORS
    mov word [disk_packet + 4], 0
    mov word [disk_packet + 6], 0x0600
    mov dword [disk_packet + 8], FAT_LBA
    mov dword [disk_packet + 12], 0
    call read_disk
    jc disk_error

    mov ax, 0x0400
    mov es, ax
    xor di, di
    mov cx, ROOT_ENTRIES
.find_kernel:
    cmp byte [es:di], 0
    je kernel_not_found
    cmp byte [es:di], 0xE5
    je .next_entry
    mov al, [es:di + 11]
    cmp al, 0x0F
    je .next_entry
    test al, 0x18
    jnz .next_entry

    push cx
    push di
    mov si, kernel_filename
    mov cx, 11
    repe cmpsb
    pop di
    pop cx
    je .kernel_found

.next_entry:
    add di, 32
    loop .find_kernel
    jmp kernel_not_found

.kernel_found:
    mov ax, [es:di + 26]
    mov [current_cluster], ax
    mov eax, [es:di + 28]
    mov [remaining_bytes], eax
    test eax, eax
    jz kernel_not_found
    cmp eax, 65024
    ja kernel_not_found
    add eax, 511
    shr eax, 9
    mov [remaining_sectors], ax
    mov word [load_offset], 0

.load_cluster:
    mov ax, [current_cluster]
    cmp ax, 2
    jb disk_error
    cmp ax, 2840
    jae disk_error
    sub ax, 2
    add ax, DATA_LBA
    movzx eax, ax
    mov [disk_packet + 8], eax
    mov dword [disk_packet + 12], 0
    mov word [disk_packet + 2], 1
    mov ax, [load_offset]
    mov [disk_packet + 4], ax
    mov word [disk_packet + 6], KERNEL_SEGMENT
    call read_disk
    jc disk_error

    add word [load_offset], 512
    dec word [remaining_sectors]
    jz kernel_loaded

    mov ax, [current_cluster]
    mov bx, ax
    shr ax, 1
    add ax, bx
    mov di, ax
    mov ax, 0x0600
    mov es, ax
    mov ax, [es:di]
    test byte [current_cluster], 1
    jz .even_cluster
    shr ax, 4
    jmp .next_cluster
.even_cluster:
    and ax, 0x0FFF
.next_cluster:
    cmp ax, 0x0FF8
    jae disk_error
    mov [current_cluster], ax
    jmp .load_cluster

kernel_loaded:
    mov si, loaded_message
    call print_string
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode_start

kernel_not_found:
    mov si, not_found_message
    call print_string
    jmp stage2_halt

disk_error:
    mov si, disk_error_message
    call print_string
    jmp stage2_halt

read_disk:
    pusha
    push es
    mov eax, [disk_packet + 8]
    cmp dword [disk_packet + 12], 0
    jne .failed
    mov [request_lba], eax
    mov ax, [disk_packet + 2]
    mov [request_remaining], ax
    mov ax, [disk_packet + 4]
    mov [request_offset], ax
    mov ax, [disk_packet + 6]
    mov [request_segment], ax
    cmp byte [boot_drive], 0x80
    jae .extended_read
.next_sector:
    mov byte [retry_count], 3
.retry:
    mov eax, [request_lba]
    xor edx, edx
    mov ecx, 36
    div ecx
    cmp eax, 80
    jae .failed
    mov [request_cylinder], al
    mov ax, dx
    xor dx, dx
    mov bx, 18
    div bx
    mov [request_head], al
    mov al, dl
    inc al
    mov cl, al
    mov ch, [request_cylinder]
    mov dh, [request_head]
    mov ax, 0x0201
    mov bx, [request_offset]
    mov es, [request_segment]
    mov dl, [boot_drive]
    int 0x13
    jnc .sector_read
    dec byte [retry_count]
    jz .read_failed
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    jmp .retry
.sector_read:
    inc dword [request_lba]
    add word [request_offset], 512
    dec word [request_remaining]
    jnz .next_sector
    pop es
    popa
    clc
    ret
.extended_read:
    mov byte [retry_count], 3
.extended_retry:
    mov si, disk_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .extended_success
    dec byte [retry_count]
    jz .read_failed
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    jmp .extended_retry
.extended_success:
    pop es
    popa
    clc
    ret
.read_failed:
    pop es
    popa
    stc
    ret
.failed:
    pop es
    popa
    stc
    ret

print_string:
    lodsb
    test al, al
    jz .done
    cmp al, 10
    jne .write_character
    inc byte [boot_row]
.write_character:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_string
.done:
    ret

stage2_halt:
    cli
.halt:
    hlt
    jmp .halt

boot_drive db 0
boot_row db 1
retry_count db 0
current_cluster dw 0
remaining_sectors dw 0
remaining_bytes dd 0
load_offset dw 0
request_lba dd 0
request_remaining dw 0
request_offset dw 0
request_segment dw 0
request_cylinder db 0
request_head db 0
kernel_filename db 'KERNEL  BIN'
loading_message db 'BIOS: LOADING FAT-12 KERNEL', 13, 10, 0
loaded_message db 'BOOT: KERNEL LOADED', 13, 10, 0
not_found_message db 'ERROR: KERNEL.BIN MISSING, INCORRECT, OR CORRUPTED', 13, 10, 0
disk_error_message db 'ERROR: FAT-12 DISK READ FAIL', 13, 10, 0

align 4
disk_packet:
    db 0x10
    db 0
    dw 1
    dw 0
    dw 0
    dq 0

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BITS 32
protected_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    xor ebp, ebp
    movzx ebx, byte [boot_drive]
    shl ebx, 8
    movzx edx, byte [boot_row]
    or ebx, edx
    mov eax, KERNEL_SEGMENT * 16
    call eax

protected_mode_halt:
    cli
    hlt
    jmp protected_mode_halt

times 8 * 512 - ($ - $$) db 0
