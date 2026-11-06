#include "driver_api.h"

extern int ata_read_sector(drv_u32 lba, drv_u8 *buffer);
extern int ata_write_sector(drv_u32 lba, const drv_u8 *buffer);

drv_u32 driver_entry(drv_u32 command, drv_u32 argument1, drv_u32 argument2)
{
    if (command == DRIVER_READ) {
        return ata_read_sector(argument1, (drv_u8 *)argument2) ? 1 : 0;
    }
    if (command == DRIVER_WRITE) {
        return ata_write_sector(argument1, (const drv_u8 *)argument2) ? 1 : 0;
    }
    return 0;
}
