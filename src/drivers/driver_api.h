#ifndef DRIVER_API_H
#define DRIVER_API_H

typedef unsigned char drv_u8;
typedef unsigned int drv_u32;

typedef drv_u32 (*drv_timer_fn)(void);
typedef drv_u32 (*drv_irq_count_fn)(drv_u8 irq);

typedef struct {
    drv_u32 bios_drive;
    drv_timer_fn timer_get_ticks;
    drv_irq_count_fn interrupt_irq_count;
} driver_context;

#define DRIVER_INIT 0
#define DRIVER_READ 1
#define DRIVER_WRITE 2
#define DRIVER_ERROR 3

#endif
