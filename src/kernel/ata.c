#include "interrupts.h"

#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DRIVE 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7
#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_DF 0x20
#define ATA_STATUS_BSY 0x80
#define ATA_POLL_LIMIT 1000000

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

static inline u16 inw(u16 port)
{
    u16 value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(u16 port, u16 value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static int ata_wait(u8 require_data)
{
    u32 timeout;

    for (timeout = 0; timeout < ATA_POLL_LIMIT; ++timeout) {
        u8 status = inb(ATA_STATUS);

        if (status == 0 || status == 0xFF) {
            return 0;
        }
        if ((status & ATA_STATUS_BSY) != 0) {
            continue;
        }
        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0) {
            return 0;
        }
        if (!require_data || (status & ATA_STATUS_DRQ) != 0) {
            return 1;
        }
    }
    return 0;
}

int ata_read_sector(u32 lba, u8 *buffer)
{
    u32 word;

    if (lba >= 0x10000000 || buffer == 0) {
        return 0;
    }
    if (!ata_wait(0)) {
        return 0;
    }

    outb(ATA_DRIVE, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, (u8)lba);
    outb(ATA_LBA_MID, (u8)(lba >> 8));
    outb(ATA_LBA_HIGH, (u8)(lba >> 16));
    outb(ATA_COMMAND, 0x20);

    if (!ata_wait(1)) {
        return 0;
    }
    for (word = 0; word < 256; ++word) {
        u16 value = inw(ATA_DATA);
        buffer[word * 2] = (u8)value;
        buffer[word * 2 + 1] = (u8)(value >> 8);
    }

    return ata_wait(0);
}

int ata_write_sector(u32 lba, const u8 *buffer)
{
    u32 word;

    if (lba >= 0x10000000 || buffer == 0 || !ata_wait(0)) {
        return 0;
    }

    outb(ATA_DRIVE, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, (u8)lba);
    outb(ATA_LBA_MID, (u8)(lba >> 8));
    outb(ATA_LBA_HIGH, (u8)(lba >> 16));
    outb(ATA_COMMAND, 0x30);

    if (!ata_wait(1)) {
        return 0;
    }
    for (word = 0; word < 256; ++word) {
        u16 value = (u16)buffer[word * 2] |
            ((u16)buffer[word * 2 + 1] << 8);
        outw(ATA_DATA, value);
    }

    return ata_wait(0);
}
