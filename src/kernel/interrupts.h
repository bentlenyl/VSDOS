#ifndef INTERRUPTS_H
#define INTERRUPTS_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef struct {
    u32 edi;
    u32 esi;
    u32 ebp;
    u32 saved_esp;
    u32 ebx;
    u32 edx;
    u32 ecx;
    u32 eax;
    u32 gs;
    u32 fs;
    u32 es;
    u32 ds;
    u32 interrupt_number;
    u32 error_code;
    u32 eip;
    u32 cs;
    u32 eflags;
} interrupt_frame;

void interrupts_initialize(void);
void interrupts_enable(void);
void interrupts_wait(void);
int keyboard_read_scancode(u8 *scan_code);
u32 timer_get_ticks(void);
u32 interrupt_irq_count(u8 irq);
void kernel_exception(const interrupt_frame *frame);

#endif
