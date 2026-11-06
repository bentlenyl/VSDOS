#include "driver_api.h"

extern int floppy_initialize(drv_u8 drive);
extern int floppy_read_sector(drv_u32 lba, drv_u8 *buffer);
extern int floppy_write_sector(drv_u32 lba, const drv_u8 *buffer);
extern drv_u8 floppy_initialization_error(void);
extern void floppy_set_callbacks(drv_timer_fn timer, drv_irq_count_fn irq_count);

drv_u32 driver_entry(drv_u32 command, drv_u32 argument1, drv_u32 argument2)
{
    if (command == DRIVER_INIT) {
        driver_context *context = (driver_context *)argument1;
        floppy_set_callbacks(context->timer_get_ticks,
            context->interrupt_irq_count);
        return floppy_initialize((drv_u8)context->bios_drive) ? 1 : 0;
    }
    if (command == DRIVER_READ) {
        return floppy_read_sector(argument1, (drv_u8 *)argument2) ? 1 : 0;
    }
    if (command == DRIVER_WRITE) {
        return floppy_write_sector(argument1, (const drv_u8 *)argument2) ? 1 : 0;
    }
    if (command == DRIVER_ERROR) {
        return floppy_initialization_error();
    }
    return 0;
}
