#include "disk.h"

extern int ata_read_sector(u32 lba, u8 *buffer);
extern int ata_write_sector(u32 lba, const u8 *buffer);
extern int floppy_initialize(u8 drive);
extern int floppy_read_sector(u32 lba, u8 *buffer);
extern int floppy_write_sector(u32 lba, const u8 *buffer);
extern u8 floppy_initialization_error(void);

static u8 selected_floppy;
static u8 selected_drive;
static int disk_ready;

int disk_initialize(u8 bios_drive)
{
    selected_drive = bios_drive;
    selected_floppy = bios_drive < 0x80;
    if (selected_floppy) {
        disk_ready = floppy_initialize(bios_drive);
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
        ? floppy_read_sector(lba, buffer)
        : ata_read_sector(lba, buffer);
}

int disk_write_sector(u32 lba, const u8 *buffer)
{
    if (!disk_ready || buffer == 0) {
        return 0;
    }
    return selected_floppy
        ? floppy_write_sector(lba, buffer)
        : ata_write_sector(lba, buffer);
}

int disk_is_floppy(void)
{
    return disk_ready && selected_floppy;
}

u8 disk_floppy_initialization_error(void)
{
    return selected_floppy ? floppy_initialization_error() : 0;
}

char disk_drive_letter(void)
{
    if (selected_floppy) {
        return (char)('A' + selected_drive);
    }
    return (char)('C' + selected_drive - 0x80);
}
