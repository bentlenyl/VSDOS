BITS 32

section .text

extern interrupt_dispatch

%macro ISR_NO_ERROR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp interrupt_common
%endmacro

%macro ISR_ERROR 1
global isr%1
isr%1:
    push dword %1
    jmp interrupt_common
%endmacro

%macro IRQ_STUB 2
global irq%1
irq%1:
    push dword 0
    push dword %2
    jmp interrupt_common
%endmacro

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR 8
ISR_NO_ERROR 9
ISR_ERROR 10
ISR_ERROR 11
ISR_ERROR 12
ISR_ERROR 13
ISR_ERROR 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR 29
ISR_ERROR 30
ISR_NO_ERROR 31

IRQ_STUB 0, 32
IRQ_STUB 1, 33
IRQ_STUB 2, 34
IRQ_STUB 3, 35
IRQ_STUB 4, 36
IRQ_STUB 5, 37
IRQ_STUB 6, 38
IRQ_STUB 7, 39
IRQ_STUB 8, 40
IRQ_STUB 9, 41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

interrupt_common:
    push ds
    push es
    push fs
    push gs
    pushad

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, esp
    and esp, 0xFFFFFFF0
    sub esp, 12
    push ebp
    call interrupt_dispatch
    mov esp, ebp

    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8
    iretd

global exception_stub_table
global irq_stub_table

section .rodata
exception_stub_table:
%assign vector 0
%rep 32
    dd isr%+vector
%assign vector vector + 1
%endrep

irq_stub_table:
%assign vector 0
%rep 16
    dd irq%+vector
%assign vector vector + 1
%endrep

section .text
global idt_load
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret
