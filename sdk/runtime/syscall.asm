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
global vsdos_run_app_args
global vsdos_clear
global vsdos_ticks
global vsdos_sleep
global vsdos_kernel_memory_size
global vsdos_kernel_stack_top
global vsdos_get_app_arguments
global vsdos_exit_code
global vsdos_exit
extern main

section .bss
app_argument_buffer: resb 512
app_argv: resd 16
app_argc: resd 1

section .text

_start:
    push dword 512
    push dword app_argument_buffer
    call vsdos_get_app_arguments
    add esp, 8

    mov dword [app_argc], 0
    mov dword [app_argv], app_argument_buffer
    mov dword [app_argc], 1

    mov esi, app_argument_buffer
.find_path_end:
    cmp byte [esi], 0
    je .path_done
    inc esi
    jmp .find_path_end
.path_done:
    inc esi
    mov edi, esi
    cmp byte [esi], 0
    je .args_done
.skip_space:
    cmp byte [esi], 32
    je .consume_space
    cmp byte [esi], 9
    jne .parse_argument
.consume_space:
    inc esi
    cmp byte [esi], 0
    jne .skip_space
.parse_argument:
    cmp byte [esi], 0
    je .args_done
    mov eax, [app_argc]
    cmp eax, 16
    jae .args_done
    mov ebx, app_argv
    lea ecx, [ebx + eax * 4]
    mov [ecx], esi
    inc dword [app_argc]
.argument_end:
    cmp byte [esi], 0
    je .args_done
    cmp byte [esi], 32
    je .terminate_argument
    cmp byte [esi], 9
    jne .next_argument_character
.terminate_argument:
    mov byte [esi], 0
    inc esi
    jmp .skip_space
.next_argument_character:
    inc esi
    jmp .argument_end
.args_done:
    push dword app_argv
    mov eax, [app_argc]
    push eax
    call main
    add esp, 8
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

vsdos_run_app_args:
    push ebx
    mov eax, 12
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
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

vsdos_sleep:
    push ebx
    mov eax, 5
    int 0x80
    mov ebx, eax
    mov ecx, [esp + 8]
    add ebx, ecx
.sleep_wait:
    mov eax, 5
    int 0x80
    cmp eax, ebx
    jb .sleep_wait
    xor eax, eax
    pop ebx
    ret

vsdos_kernel_memory_size:
    mov eax, 17
    int 0x80
    ret

vsdos_kernel_stack_top:
    mov eax, 18
    int 0x80
    ret

vsdos_get_app_arguments:
    push ebx
    mov eax, 19
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    int 0x80
    pop ebx
    ret

vsdos_exit_code:
    mov eax, 20
    int 0x80
    ret

vsdos_exit:
    push ebx
    mov eax, 6
    mov ebx, [esp + 8]
    int 0x80
    pop ebx
    ret
