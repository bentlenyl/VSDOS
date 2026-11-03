BITS 32

section .text

global _start
global vsdos_print
global vsdos_print_text
global vsdos_type_file
global vsdos_drive_letter
global vsdos_read_key
global vsdos_read_file
global vsdos_write_file
global vsdos_list
global vsdos_delete_file
global vsdos_rename_file
global vsdos_make_directory
global vsdos_remove_directory
global vsdos_run_app
global vsdos_clear
global vsdos_ticks
global vsdos_exit
extern main

_start:
    call main
    push eax
    call vsdos_exit
    add esp, 4
    ret

vsdos_print:
    push ebx
    mov eax, 1
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    int 0x80
    pop ebx
    ret

vsdos_print_text:
    push ebx
    mov eax, 14
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    int 0x80
    pop ebx
    ret

vsdos_type_file:
    push ebx
    mov eax, 15
    mov ebx, [esp + 8]
    int 0x80
    pop ebx
    ret

vsdos_drive_letter:
    mov eax, 16
    int 0x80
    ret

vsdos_read_key:
    mov eax, 2
    int 0x80
    ret

vsdos_read_file:
    push ebx
    mov eax, 3
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    int 0x80
    pop ebx
    ret

vsdos_write_file:
    push ebx
    mov eax, 4
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    int 0x80
    pop ebx
    ret

vsdos_list:
    push ebx
    mov eax, 7
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    int 0x80
    pop ebx
    ret

vsdos_delete_file:
    push ebx
    mov eax, 8
    mov ebx, [esp + 8]
    int 0x80
    pop ebx
    ret

vsdos_rename_file:
    push ebx
    mov eax, 9
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    int 0x80
    pop ebx
    ret

vsdos_make_directory:
    push ebx
    mov eax, 10
    mov ebx, [esp + 8]
    int 0x80
    pop ebx
    ret

vsdos_remove_directory:
    push ebx
    mov eax, 11
    mov ebx, [esp + 8]
    int 0x80
    pop ebx
    ret

vsdos_run_app:
    push ebx
    mov eax, 12
    mov ebx, [esp + 8]
    int 0x80
    pop ebx
    ret

vsdos_clear:
    mov eax, 13
    int 0x80
    ret

vsdos_ticks:
    mov eax, 5
    int 0x80
    ret

vsdos_exit:
    push ebx
    mov eax, 6
    mov ebx, [esp + 8]
    int 0x80
    pop ebx
    ret
