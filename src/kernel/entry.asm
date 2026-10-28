BITS 32

section .text
global kernel_entry
extern kernel_main
extern __bss_start
extern __bss_end

kernel_entry:
    cld
    mov ebp, ebx
    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    rep stosb

    and esp, 0xFFFFFFF0
    sub esp, 12
    push ebp
    call kernel_main
    add esp, 16

.halt:
    cli
    hlt
    jmp .halt
