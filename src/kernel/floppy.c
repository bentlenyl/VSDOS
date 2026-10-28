#include "disk.h"

#define FDC_DOR 0x3F2
#define FDC_MSR 0x3F4
#define FDC_FIFO 0x3F5
#define FDC_CCR 0x3F7
#define FDC_DMA_MASK 0x0A
#define FDC_DMA_MODE 0x0B
#define FDC_DMA_CLEAR 0x0C
#define FDC_DMA_CHANNEL2_ADDRESS 0x04
#define FDC_DMA_CHANNEL2_COUNT 0x05
#define FDC_DMA_PAGE_CHANNEL2 0x81
#define FDC_SECTOR_SIZE 512
#define FDC_SECTORS_PER_TRACK 18
#define FDC_HEADS 2
#define FDC_TOTAL_SECTORS 2880
#define FDC_COMMAND_TIMEOUT_TICKS 300
#define FDC_FIFO_POLL_LIMIT 1000000

static u8 selected_drive;
static u8 current_cylinder[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
static u8 motor_running[4];
static u8 controller_ready;
static u8 initialization_error;
static u8 dma_buffer[FDC_SECTOR_SIZE] __attribute__((aligned(65536)));

extern u32 interrupt_irq_count(u8 irq);
extern u32 timer_get_ticks(void);

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

static int wait_fifo_write(void)
{
    u32 poll;

    for (poll = 0; poll < FDC_FIFO_POLL_LIMIT; ++poll) {
        u8 status = inb(FDC_MSR);

        if ((status & 0x80) != 0 && (status & 0x40) == 0) {
            return 1;
        }
    }
    return 0;
}

static int wait_fifo_read(void)
{
    u32 poll;

    for (poll = 0; poll < FDC_FIFO_POLL_LIMIT; ++poll) {
        u8 status = inb(FDC_MSR);

        if ((status & 0xC0) == 0xC0) {
            return 1;
        }
    }
    return 0;
}

static int write_fifo(u8 value)
{
    if (!wait_fifo_write()) {
        return 0;
    }
    outb(FDC_FIFO, value);
    return 1;
}

static int read_fifo(u8 *value)
{
    if (!wait_fifo_read()) {
        return 0;
    }
    *value = inb(FDC_FIFO);
    return 1;
}

static int wait_irq(u32 previous_count)
{
    u32 start = timer_get_ticks();

    while (interrupt_irq_count(6) == previous_count) {
        if (timer_get_ticks() - start >= FDC_COMMAND_TIMEOUT_TICKS) {
            return 0;
        }
        __asm__ volatile ("pause");
    }
    return 1;
}

static int wait_data_result(void)
{
    u32 start = timer_get_ticks();

    while ((inb(FDC_MSR) & 0xC0) != 0xC0) {
        if (timer_get_ticks() - start >= FDC_COMMAND_TIMEOUT_TICKS) {
            return 0;
        }
        __asm__ volatile ("pause");
    }
    return 1;
}

static int sense_interrupt(u8 *status, u8 *cylinder)
{
    if (!write_fifo(0x08) ||
        !read_fifo(status) ||
        !read_fifo(cylinder)) {
        return 0;
    }
    return 1;
}

static u8 drive_motor_value(void)
{
    return (u8)(0x0C | selected_drive | (0x10 << selected_drive));
}

static int start_motor(void)
{
    if (!controller_ready) {
        return 0;
    }
    outb(FDC_DOR, drive_motor_value());
    if (!motor_running[selected_drive]) {
        u32 start = timer_get_ticks();

        while (timer_get_ticks() - start < 50) {
            __asm__ volatile ("pause");
        }
        motor_running[selected_drive] = 1;
    }
    return 1;
}

static int recalibrate(void)
{
    u8 status;
    u8 cylinder;
    u32 previous_irq;

    previous_irq = interrupt_irq_count(6);
    if (!write_fifo(0x07) ||
        !write_fifo(selected_drive) ||
        !wait_irq(previous_irq) ||
        !sense_interrupt(&status, &cylinder)) {
        return 0;
    }
    if ((status & 0xC0) != 0 || (status & 0x20) == 0 ||
        (status & 0x03) != selected_drive || cylinder != 0) {
        return 0;
    }
    current_cylinder[selected_drive] = 0;
    return 1;
}

static int seek_track(u8 cylinder, u8 head)
{
    u8 status;
    u8 actual_cylinder;
    u8 head_drive = (u8)((head << 2) | selected_drive);
    u32 previous_irq;

    if (current_cylinder[selected_drive] == cylinder) {
        return 1;
    }

    previous_irq = interrupt_irq_count(6);
    if (!write_fifo(0x0F) ||
        !write_fifo(head_drive) ||
        !write_fifo(cylinder) ||
        !wait_irq(previous_irq) ||
        !sense_interrupt(&status, &actual_cylinder)) {
        return 0;
    }
    if ((status & 0xC0) != 0 || (status & 0x20) == 0 ||
        (status & 0x07) != head_drive || actual_cylinder != cylinder) {
        current_cylinder[selected_drive] = 0xFF;
        return 0;
    }
    current_cylinder[selected_drive] = cylinder;
    return 1;
}

static int prepare_dma(int device_to_memory)
{
    u32 address = (u32)dma_buffer;

    if ((address & 0xFFFF) + FDC_SECTOR_SIZE > 0x10000 ||
        address >= 0x01000000) {
        return 0;
    }

    outb(FDC_DMA_MASK, 0x06);
    outb(FDC_DMA_CLEAR, 0xFF);
    outb(FDC_DMA_CHANNEL2_ADDRESS, (u8)address);
    outb(FDC_DMA_CHANNEL2_ADDRESS, (u8)(address >> 8));
    outb(FDC_DMA_PAGE_CHANNEL2, (u8)(address >> 16));
    outb(FDC_DMA_CLEAR, 0xFF);
    outb(FDC_DMA_CHANNEL2_COUNT, (u8)(FDC_SECTOR_SIZE - 1));
    outb(FDC_DMA_CHANNEL2_COUNT, (u8)((FDC_SECTOR_SIZE - 1) >> 8));
    outb(FDC_DMA_MODE, (u8)((device_to_memory ? 0x46 : 0x4A)));
    outb(FDC_DMA_MASK, 0x02);
    return 1;
}

static int transfer_sector(u32 lba, int write)
{
    u8 cylinder;
    u8 head;
    u8 sector;
    u8 result[7];
    u8 retry;

    if (lba >= FDC_TOTAL_SECTORS || !start_motor()) {
        return 0;
    }

    cylinder = (u8)(lba / (FDC_HEADS * FDC_SECTORS_PER_TRACK));
    head = (u8)((lba / FDC_SECTORS_PER_TRACK) % FDC_HEADS);
    sector = (u8)(lba % FDC_SECTORS_PER_TRACK + 1);
    if (!seek_track(cylinder, head)) {
        if (!recalibrate() || !seek_track(cylinder, head)) {
            return 0;
        }
    }

    for (retry = 0; retry < 3; ++retry) {
        u8 index;
        u8 command = write ? 0x45 : 0x46;

        if (!prepare_dma(!write) ||
            !write_fifo(command) ||
            !write_fifo((u8)((head << 2) | selected_drive)) ||
            !write_fifo(cylinder) ||
            !write_fifo(head) ||
            !write_fifo(sector) ||
            !write_fifo(2) ||
            !write_fifo(sector) ||
            !write_fifo(0x1B) ||
            !write_fifo(0xFF) ||
            !wait_data_result()) {
            return 0;
        }

        for (index = 0; index < sizeof(result); ++index) {
            if (!read_fifo(&result[index])) {
                return 0;
            }
        }
        if ((result[0] & 0xC0) == 0 && result[1] == 0 &&
            result[2] == 0 && result[6] == 2) {
            return 1;
        }

        current_cylinder[selected_drive] = 0xFF;
        if (!recalibrate() || !seek_track(cylinder, head)) {
            return 0;
        }
    }
    return 0;
}

int floppy_initialize(u8 drive)
{
    u32 previous_irq;
    u8 status;
    u8 cylinder;
    u8 index;

    initialization_error = 0;
    if (drive > 1) {
        initialization_error = 1;
        return 0;
    }

    selected_drive = drive;
    controller_ready = 0;
    previous_irq = interrupt_irq_count(6);
    outb(FDC_DOR, 0x00);
    outb(FDC_DOR, 0x0C);
    if (!wait_irq(previous_irq)) {
        initialization_error = 2;
        return 0;
    }
    for (index = 0; index < 4; ++index) {
        if (!sense_interrupt(&status, &cylinder)) {
            initialization_error = 3;
            return 0;
        }
    }

    outb(FDC_CCR, 0x00);
    if (!write_fifo(0x03) ||
        !write_fifo(0xDF) ||
        !write_fifo(0x02)) {
        initialization_error = 4;
        return 0;
    }
    current_cylinder[drive] = 0xFF;
    controller_ready = 1;
    if (!start_motor()) {
        initialization_error = 5;
        controller_ready = 0;
        return 0;
    }
    if (!recalibrate()) {
        initialization_error = 6;
        controller_ready = 0;
        return 0;
    }
    current_cylinder[drive] = 0xFF;
    return 1;
}

u8 floppy_initialization_error(void)
{
    return initialization_error;
}

int floppy_read_sector(u32 lba, u8 *buffer)
{
    u32 index;

    if (buffer == 0 || !transfer_sector(lba, 0)) {
        return 0;
    }
    for (index = 0; index < FDC_SECTOR_SIZE; ++index) {
        buffer[index] = dma_buffer[index];
    }
    return 1;
}

int floppy_write_sector(u32 lba, const u8 *buffer)
{
    u32 index;

    if (buffer == 0) {
        return 0;
    }
    for (index = 0; index < FDC_SECTOR_SIZE; ++index) {
        dma_buffer[index] = buffer[index];
    }
    return transfer_sector(lba, 1);
}
