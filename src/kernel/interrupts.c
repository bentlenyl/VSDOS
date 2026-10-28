#include "interrupts.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20
#define PIT_COMMAND 0x43
#define PIT_CHANNEL_0 0x40
#define PIT_INPUT_HZ 1193182
#define PIT_FREQUENCY 100
#define KEYBOARD_QUEUE_MASK 127

typedef struct {
    u16 offset_low;
    u16 selector;
    u8 zero;
    u8 type_attributes;
    u16 offset_high;
} __attribute__((packed)) idt_gate;

typedef struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) idt_register;

static idt_gate interrupt_table[48];
static volatile u8 keyboard_queue[128];
static volatile u8 keyboard_head;
static volatile u8 keyboard_tail;
static volatile u32 clock_ticks;
static volatile u32 irq_counts[16];

extern void (*exception_stub_table[32])(void);
extern void (*irq_stub_table[16])(void);
extern void idt_load(const idt_register *register_pointer);

static inline void outb(u16 port, u8 value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void io_wait(void)
{
    outb(0x80, 0);
}

static void idt_set_gate(u8 vector, void (*handler)(void))
{
    u32 address = (u32)handler;

    interrupt_table[vector].offset_low = (u16)(address & 0xFFFF);
    interrupt_table[vector].selector = 0x08;
    interrupt_table[vector].zero = 0;
    interrupt_table[vector].type_attributes = 0x8E;
    interrupt_table[vector].offset_high = (u16)(address >> 16);
}

static void pic_remap(void)
{
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    outb(PIC1_DATA, 0xB8);
    outb(PIC2_DATA, 0xFF);
}

static void pit_initialize(void)
{
    u16 divisor = (u16)(PIT_INPUT_HZ / PIT_FREQUENCY);

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL_0, (u8)(divisor & 0xFF));
    outb(PIT_CHANNEL_0, (u8)(divisor >> 8));
}

void interrupts_initialize(void)
{
    idt_register descriptor;
    u32 index;

    for (index = 0; index < 32; ++index) {
        idt_set_gate((u8)index, exception_stub_table[index]);
    }
    for (index = 0; index < 16; ++index) {
        idt_set_gate((u8)(index + 32), irq_stub_table[index]);
    }

    descriptor.limit = (u16)(sizeof(interrupt_table) - 1);
    descriptor.base = (u32)interrupt_table;
    idt_load(&descriptor);

    pic_remap();
    pit_initialize();
}

void interrupts_enable(void)
{
    __asm__ volatile ("sti");
}

void interrupts_wait(void)
{
    __asm__ volatile ("sti; hlt");
}

int keyboard_read_scancode(u8 *scan_code)
{
    u8 tail = keyboard_tail;

    if (tail == keyboard_head) {
        return 0;
    }

    *scan_code = keyboard_queue[tail];
    keyboard_tail = (u8)((tail + 1) & KEYBOARD_QUEUE_MASK);
    return 1;
}

u32 timer_get_ticks(void)
{
    return clock_ticks;
}

u32 interrupt_irq_count(u8 irq)
{
    if (irq >= 16) {
        return 0;
    }
    return irq_counts[irq];
}

void interrupt_dispatch(const interrupt_frame *frame)
{
    u32 vector = frame->interrupt_number;
    u32 irq;

    if (vector < 32) {
        kernel_exception(frame);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    if (vector == 32) {
        ++clock_ticks;
    } else if (vector == 33) {
        u8 next_head = (u8)((keyboard_head + 1) & KEYBOARD_QUEUE_MASK);

        if (next_head != keyboard_tail) {
            keyboard_queue[keyboard_head] = inb(0x60);
            keyboard_head = next_head;
        } else {
            (void)inb(0x60);
        }
    } else if (vector == 39) {
        outb(PIC1_COMMAND, 0x0B);
        if ((inb(PIC1_COMMAND) & 0x80) == 0) {
            return;
        }
    } else if (vector == 47) {
        outb(PIC2_COMMAND, 0x0B);
        if ((inb(PIC2_COMMAND) & 0x80) == 0) {
            outb(PIC1_COMMAND, PIC_EOI);
            return;
        }
    }

    irq = vector - 32;
    if (irq < 16) {
        ++irq_counts[irq];
    }
    if (vector >= 40) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}
