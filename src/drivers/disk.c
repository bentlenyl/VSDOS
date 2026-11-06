#include "disk.h"
#include "driver_api.h"

#define ATA_DRIVER_ADDRESS 0x00030000u
#define FLOPPY_DRIVER_ADDRESS 0x00031000u

typedef u32 (*driver_entry_fn)(u32 command, u32 argument1, u32 argument2);
static driver_entry_fn ata_driver;
static driver_entry_fn floppy_driver;

static u8 selected_floppy;
static u8 selected_drive;
static int disk_ready;

int disk_initialize(u8 bios_drive)
{
    selected_drive = bios_drive;
    selected_floppy = bios_drive < 0x80;
    ata_driver = (driver_entry_fn)ATA_DRIVER_ADDRESS;
    floppy_driver = (driver_entry_fn)FLOPPY_DRIVER_ADDRESS;
    if (selected_floppy) {
        driver_context context;
        context.bios_drive = bios_drive;
        context.timer_get_ticks = timer_get_ticks;
        context.interrupt_irq_count = interrupt_irq_count;
        disk_ready = floppy_driver(DRIVER_INIT, (u32)&context, 0) != 0;
    } else {
        disk_ready = bios_drive == 0x80;
    }
    return disk_ready;
}

int disk_read_sector(u32 lba, u8 *buffer)
{
    if (!disk_ready || buffer == 0) {
        return 0;
    }
    return selected_floppy
        ? floppy_driver(DRIVER_READ, lba, (u32)buffer) != 0
        : ata_driver(DRIVER_READ, lba, (u32)buffer) != 0;
}

int disk_write_sector(u32 lba, const u8 *buffer)
{
    if (!disk_ready || buffer == 0) {
        return 0;
    }
    return selected_floppy
        ? floppy_driver(DRIVER_WRITE, lba, (u32)buffer) != 0
        : ata_driver(DRIVER_WRITE, lba, (u32)buffer) != 0;
}

int disk_is_floppy(void)
{
    return disk_ready && selected_floppy;
}

u8 disk_floppy_initialization_error(void)
{
    return selected_floppy && floppy_driver
        ? (u8)floppy_driver(DRIVER_ERROR, 0, 0) : 0;
}

char disk_drive_letter(void)
{
    if (selected_floppy) {
        return (char)('A' + selected_drive);
    }
    return (char)('C' + selected_drive - 0x80);
}
