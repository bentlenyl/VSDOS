#include "interrupts.h"
#include "filesystem.h"
#include "../drivers/disk.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile u16 *)0x000B8000)
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define COM1 0x3F8
#define SHELL_LOAD_ADDRESS 0x00200000
#define APP_LOAD_ADDRESS 0x00220000
#define APP_MAX_SIZE 0x00020000
#define SYS_WRITE 1
#define SYS_READ_KEY 2
#define SYS_READ_FILE 3
#define SYS_WRITE_FILE 4
#define SYS_TICKS 5
#define SYS_EXIT 6
#define SYS_LIST 7
#define SYS_DELETE 8
#define SYS_RENAME 9
#define SYS_MKDIR 10
#define SYS_RMDIR 11
#define SYS_RUN 12
#define SYS_CLEAR 13
#define SYS_WRITE_TEXT 14
#define SYS_TYPE_FILE 15
#define SYS_DRIVE 16
#define SYS_KERNEL_MEMORY_SIZE 17
#define SYS_KERNEL_STACK_TOP 18
#define SYS_GET_APP_ARGUMENTS 19
#define SYS_EXIT_CODE 20

static u8 terminal_row;
static u8 terminal_column;
static u8 uart_ready;
static char terminal_last_output_character;
static char current_drive_letter = 'A';
static char *syscall_file_buffer;
static u32 syscall_file_capacity;
static u32 syscall_file_length;
static u32 app_exit_code;
static char app_arguments[512];
static u32 app_arguments_length;
static int run_app(const char *path, const char *arguments, u32 load_address);

static void terminal_write_bytes(const char *text, u32 length);
static void terminal_write(const char *text);
static void terminal_write_char(char character);
static void terminal_clear(void);

static void terminal_write_unsigned(u32 value)
{
    char digits[10];
    u8 count = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        terminal_write_char(digits[--count]);
    }
}

static void terminal_write_hex(u32 value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    terminal_write("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        terminal_write_char(digits[(value >> shift) & 0x0F]);
    }
}

static void syscall_file_sink(const char *data, u32 length)
{
    u32 available = syscall_file_capacity - syscall_file_length;
    u32 index;

    if (length > available) {
        length = available;
    }
    for (index = 0; index < length; ++index) {
        syscall_file_buffer[syscall_file_length + index] = data[index];
    }
    syscall_file_length += length;
}

void syscall_dispatch(interrupt_frame *frame)
{
    const char *path;
    fs_entry entry;

    switch (frame->eax) {
    case SYS_WRITE:
        if (frame->ebx != 0 && frame->ecx != 0) {
            terminal_write_bytes((const char *)frame->ebx, frame->ecx);
        }
        frame->eax = 0;
        break;
    case SYS_WRITE_TEXT:
        if (frame->ebx != 0 && frame->ecx != 0) {
            terminal_write_bytes((const char *)frame->ebx, frame->ecx);
        }
        frame->eax = 0;
        break;
    case SYS_TYPE_FILE:
        path = (const char *)frame->ebx;
        if (path == 0 || !filesystem_find(path, &entry) ||
            !filesystem_read_file(&entry, terminal_write_bytes)) {
            frame->eax = 0xFFFFFFFF;
        } else {
            frame->eax = 0;
        }
        break;
    case SYS_DRIVE:
        frame->eax = (u32)(u8)current_drive_letter;
        break;
    case SYS_KERNEL_MEMORY_SIZE: {
        extern char __kernel_start;
        extern char __kernel_end;
        frame->eax = (u32)&__kernel_end - (u32)&__kernel_start;
        break;
    }
    case SYS_KERNEL_STACK_TOP:
        frame->eax = 0x00090000;
        break;
    case SYS_READ_KEY: {
        u8 scan_code;
        frame->eax = keyboard_read_scancode(&scan_code) ? scan_code : 0;
        break;
    }
    case SYS_READ_FILE:
        path = (const char *)frame->ebx;
        syscall_file_buffer = (char *)frame->ecx;
        syscall_file_capacity = frame->edx;
        syscall_file_length = 0;
        if (path == 0 || syscall_file_buffer == 0 ||
            !filesystem_find(path, &entry) ||
            !filesystem_read_file(&entry, syscall_file_sink)) {
            frame->eax = 0xFFFFFFFF;
        } else {
            frame->eax = syscall_file_length;
        }
        break;
    case SYS_WRITE_FILE:
        path = (const char *)frame->ebx;
        if (path == 0 || frame->ecx == 0) {
            frame->eax = 0xFFFFFFFF;
        } else {
            frame->eax = filesystem_write_file(
                path, (const char *)frame->ecx, frame->edx) ? 0 : 0xFFFFFFFF;
        }
        break;
    case SYS_LIST:
        if (frame->ebx == 0 || frame->edx == 0 ||
            !filesystem_get_entry_in((const char *)frame->ebx, frame->ecx,
                (fs_entry *)frame->edx)) {
            frame->eax = 0xFFFFFFFF;
        } else {
            frame->eax = 0;
        }
        break;
    case SYS_DELETE:
        frame->eax = frame->ebx != 0 &&
            filesystem_delete_file((const char *)frame->ebx) ? 0 : 0xFFFFFFFF;
        break;
    case SYS_RENAME:
        frame->eax = frame->ebx != 0 && frame->ecx != 0 &&
            filesystem_rename_file((const char *)frame->ebx,
                (const char *)frame->ecx) ? 0 : 0xFFFFFFFF;
        break;
    case SYS_MKDIR:
        frame->eax = frame->ebx != 0 &&
            filesystem_make_directory((const char *)frame->ebx) ? 0 : 0xFFFFFFFF;
        break;
    case SYS_RMDIR:
        frame->eax = frame->ebx != 0 &&
            filesystem_remove_directory((const char *)frame->ebx) ? 0 : 0xFFFFFFFF;
        break;
    case SYS_RUN:
        frame->eax = frame->ebx != 0 &&
            run_app((const char *)frame->ebx,
                (const char *)frame->ecx, APP_LOAD_ADDRESS) ? 0 : 0xFFFFFFFF;
        break;
    case SYS_CLEAR:
        terminal_clear();
        frame->eax = 0;
        break;
    case SYS_TICKS:
        frame->eax = timer_get_ticks();
        break;
    case SYS_EXIT:
        app_exit_code = frame->ebx;
        frame->eax = 0;
        break;
    case SYS_GET_APP_ARGUMENTS: {
        char *destination = (char *)frame->ebx;
        u32 capacity = frame->ecx;
        u32 length = app_arguments_length;
        if (destination == 0 || capacity == 0) {
            frame->eax = 0xFFFFFFFF;
            break;
        }
        if (length >= capacity) {
            length = capacity - 1;
        }
        for (u32 index = 0; index < length; ++index) {
            destination[index] = app_arguments[index];
        }
        destination[length] = '\0';
        frame->eax = length;
        break;
    }
    case SYS_EXIT_CODE:
        frame->eax = app_exit_code;
        break;
    default:
        frame->eax = 0xFFFFFFFF;
        break;
    }
}

static u32 app_copy_offset;
static u32 app_load_address;

static void app_file_sink(const char *data, u32 length)
{
    u32 index;
    volatile u8 *destination = (volatile u8 *)app_load_address;

    if (app_copy_offset + length > APP_MAX_SIZE) {
        length = APP_MAX_SIZE - app_copy_offset;
    }
    for (index = 0; index < length; ++index) {
        destination[app_copy_offset + index] = (u8)data[index];
    }
    app_copy_offset += length;
}

static int run_app(const char *path, const char *arguments, u32 load_address)
{
    fs_entry entry;
    void (*entry_point)(void);
    u32 path_length = 0;
    u32 arguments_length = 0;

    if (!filesystem_find(path, &entry) || (entry.attributes & 0x10) != 0 ||
        entry.size == 0 || entry.size > APP_MAX_SIZE) {
        return 0;
    }
    app_copy_offset = 0;
    app_load_address = load_address;
    app_exit_code = 0;
    while (path[path_length] != '\0' && path_length < sizeof(app_arguments) - 2) {
        app_arguments[path_length] = path[path_length];
        ++path_length;
    }
    app_arguments[path_length++] = '\0';
    if (arguments != 0) {
        while (arguments[arguments_length] != '\0' &&
               path_length + arguments_length < sizeof(app_arguments) - 1) {
            app_arguments[path_length + arguments_length] =
                arguments[arguments_length];
            ++arguments_length;
        }
    }
    app_arguments[path_length + arguments_length] = '\0';
    app_arguments_length = path_length + arguments_length;
    if (!filesystem_read_file(&entry, app_file_sink)) {
        return 0;
    }
    if (app_copy_offset != entry.size) {
        return 0;
    }
    entry_point = (void (*)(void))load_address;
    entry_point();
    return 1;
}

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

static void terminal_update_cursor(void)
{
    u16 position = (u16)terminal_row * VGA_WIDTH + terminal_column;

    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (u8)position);
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (u8)(position >> 8));
}

static void serial_initialize(void)
{
    outb(COM1 + 7, 0x55);
    if (inb(COM1 + 7) != 0x55) {
        return;
    }
    outb(COM1 + 7, 0xAA);
    if (inb(COM1 + 7) != 0xAA) {
        return;
    }

    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    uart_ready = 1;
}

static void serial_write_byte(char character)
{
    u32 timeout = 100000;

    if (!uart_ready) {
        return;
    }
    while (timeout != 0 && (inb(COM1 + 5) & 0x20) == 0) {
        --timeout;
    }
    if (timeout == 0) {
        uart_ready = 0;
        return;
    }
    outb(COM1, (u8)character);
}

static void serial_write_char(char character)
{
    if (!uart_ready) {
        return;
    }
    if (character == '\n') {
        serial_write_byte('\r');
    }
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    serial_write_byte(character);
}

static void terminal_scroll(void)
{
    u32 row;
    u32 column;

    for (row = 1; row < VGA_HEIGHT; ++row) {
        for (column = 0; column < VGA_WIDTH; ++column) {
            VGA_MEMORY[(row - 1) * VGA_WIDTH + column] =
                VGA_MEMORY[row * VGA_WIDTH + column];
        }
    }
    for (column = 0; column < VGA_WIDTH; ++column) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + column] =
            (u16)0x0700 | ' ';
    }
    terminal_row = VGA_HEIGHT - 1;
    terminal_update_cursor();
}

static void terminal_write_char(char character)
{
    serial_write_char(character);

    if (character == '\r') {
        terminal_column = 0;
        terminal_update_cursor();
        return;
    }
    if (character == '\n') {
        terminal_column = 0;
        ++terminal_row;
    } else if (character == '\b') {
        u16 position = (u16)terminal_row * VGA_WIDTH + terminal_column;

        if (position != 0) {
            if (terminal_column == 0) {
                --terminal_row;
                terminal_column = VGA_WIDTH;
            }
            --terminal_column;
            VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] =
                (u16)0x0700 | ' ';
        }
        terminal_update_cursor();
        return;
    } else {
        VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] =
            (u16)0x0700 | (u8)character;
        ++terminal_column;
    }

    if (terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        ++terminal_row;
    }
    if (terminal_row >= VGA_HEIGHT) {
        terminal_scroll();
    }
    terminal_update_cursor();
}

static void terminal_write(const char *text)
{
    while (*text != '\0') {
        terminal_write_char(*text);
        ++text;
    }
}

static void terminal_write_bytes(const char *text, u32 length)
{
    u32 index;

    for (index = 0; index < length; ++index) {
        char character = text[index];

        if (character == '\b') {
            terminal_write_char(character);
            continue;
        }
        if (character == '\n' && terminal_last_output_character != '\r') {
            serial_write_byte('\r');
        }
        serial_write_byte(character);
        terminal_last_output_character = character;

        if (character == '\r') {
            terminal_column = 0;
        } else if (character == '\n') {
            terminal_column = 0;
            ++terminal_row;
        } else {
            VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] =
                (u16)0x0700 | (u8)character;
            ++terminal_column;
        }
        if (terminal_column == VGA_WIDTH) {
            terminal_column = 0;
            ++terminal_row;
        }
        if (terminal_row >= VGA_HEIGHT) {
            terminal_scroll();
        }
    }
    terminal_update_cursor();
}

static void terminal_clear(void)
{
    u32 cell;

    for (cell = 0; cell < VGA_WIDTH * VGA_HEIGHT; ++cell) {
        VGA_MEMORY[cell] = (u16)0x0700 | ' ';
    }
    terminal_row = 0;
    terminal_column = 0;
    terminal_update_cursor();
}



void kernel_exception(const interrupt_frame *frame)
{
    terminal_write("\n\n*** KERNEL EXCEPTION ***\nVector: ");
    terminal_write_unsigned(frame->interrupt_number);
    terminal_write("  Error: ");
    terminal_write_hex(frame->error_code);
    terminal_write("\nInstruction: ");
    terminal_write_hex(frame->eip);
    terminal_write("\nSystem halted.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void kernel_main(u32 boot_information)
{
    u8 initial_row = (u8)boot_information;
    u8 bios_drive = (u8)(boot_information >> 8);
    int disk_initialized;

    if (initial_row >= VGA_HEIGHT) {
        initial_row = 0;
    }
    terminal_row = (u8)initial_row;
    serial_initialize();
    terminal_write("[ OK ] 32-BIT PROTECTED MODE KERNEL STARTED\n");
    terminal_write("[ OK ] VGA TEXT CONSOLE READY\n");
    if (uart_ready) {
        terminal_write("[ OK ] COM1 SERIAL LOGGER READY\n");
    } else {
        terminal_write("[INFO] COM1 UNAVALIBLE; USING VGA CONSOLE\n");
    }
    interrupts_initialize();
    terminal_write("[ OK ] IDT EXCEPTIONS AND PIC INTERRUPTS READY\n");
    terminal_write("[ OK ] PIT TIMER AT 100 Hz; KEYBOARD ON IRQ1\n");
    interrupts_enable();
    disk_initialized = disk_initialize(bios_drive);
    if (disk_initialized && filesystem_mount()) {
        current_drive_letter = disk_drive_letter();
        terminal_write("[ OK ] FAT12 VOLUME MOUNTED FROM ");
        if (disk_is_floppy()) {
            terminal_write("floppy drive ");
        } else {
            terminal_write("IDE hard disk ");
        }
        terminal_write_char(current_drive_letter);
        terminal_write("\n");
    } else {
        if (bios_drive < 0x80 && !disk_initialized) {
            terminal_write("[FAIL] FLOPPY INITIALIZATION ERROR STAGE ");
            terminal_write_unsigned(disk_floppy_initialization_error());
            terminal_write("\n");
        } else {
            terminal_write("[FAIL] FAT-12 VOLUME UNAVALIBLE; DIR AND TYPE DISABLED\n");
        }
    }
    if (!run_app("\\COMMAND.APP", 0, SHELL_LOAD_ADDRESS)) {
        terminal_write("[FAIL] COMMAND.APP COULD NOT BE STARTED\n");
        for (;;) {
            interrupts_wait();
        }
    }
}
