#ifndef DISK_H
#define DISK_H

#include "interrupts.h"

int disk_initialize(u8 bios_drive);
int disk_read_sector(u32 lba, u8 *buffer);
int disk_write_sector(u32 lba, const u8 *buffer);
int disk_is_floppy(void);
char disk_drive_letter(void);
u8 disk_floppy_initialization_error(void);

#endif
