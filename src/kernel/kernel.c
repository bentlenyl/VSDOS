#include "interrupts.h"
#include "filesystem.h"
#include "disk.h"

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

static u8 terminal_row;
static u8 terminal_column;
static u8 uart_ready;
static u16 input_start_position;
static char current_drive_letter = 'A';
static char *syscall_file_buffer;
static u32 syscall_file_capacity;
static u32 syscall_file_length;
static u32 app_exit_code;
static int run_app(const char *path, u32 load_address);

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
            run_app((const char *)frame->ebx, APP_LOAD_ADDRESS) ? 0 : 0xFFFFFFFF;
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

static int run_app(const char *path, u32 load_address)
{
    fs_entry entry;
    void (*entry_point)(void);

    if (!filesystem_find(path, &entry) || (entry.attributes & 0x10) != 0 ||
        entry.size == 0 || entry.size > APP_MAX_SIZE) {
        return 0;
    }
    app_copy_offset = 0;
    app_load_address = load_address;
    app_exit_code = 0;
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
    if (input_start_position >= VGA_WIDTH) {
        input_start_position = (u16)(input_start_position - VGA_WIDTH);
    } else {
        input_start_position = 0;
    }
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

        if (position > input_start_position) {
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
        if (character == '\n') {
            serial_write_byte('\r');
        }
        serial_write_byte(character);

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

#if 0
static char uppercase(char character)
{
    if (character >= 'a' && character <= 'z') {
        return (char)(character - 'a' + 'A');
    }
    return character;
}

static int string_equals(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (uppercase(*left) != uppercase(*right)) {
            return 0;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

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
    static const char hex_digits[] = "0123456789ABCDEF";
    int shift;

    terminal_write("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        terminal_write_char(hex_digits[(value >> shift) & 0x0F]);
    }
}

static void print_prompt(void)
{
    terminal_write_char(current_drive_letter);
    terminal_write(current_directory);
    terminal_write(">");
    input_start_position = (u16)terminal_row * VGA_WIDTH + terminal_column;
}

static int make_current_path(const char *input, char output[128])
{
    u32 length = 0;
    u32 index;

    if (input == 0 || *input == '\0') {
        return 0;
    }
    if (*input == '\\' || *input == '/') {
        while (*input != '\0' && length < 127) {
            output[length++] = *input++;
        }
    } else {
        for (index = 0; current_directory[index] != '\0' &&
            length < 127; ++index) {
            output[length++] = current_directory[index];
        }
        if (length > 1 && length < 127) {
            output[length++] = '\\';
        }
        while (*input != '\0' && length < 127) {
            output[length++] = *input++;
        }
    }
    if (*input != '\0') {
        return 0;
    }
    output[length] = '\0';
    return 1;
}

static int change_directory(const char *input)
{
    char path[128];
    fs_entry entry;
    u32 length;

    if (input == 0 || *input == '\0' || string_equals(input, ".")) {
        return 1;
    }
    if (input[0] == '\\' && input[1] == '\0') {
        current_directory[0] = '\\';
        current_directory[1] = '\0';
        return 1;
    }
    if (string_equals(input, "..")) {
        length = 0;
        while (current_directory[length] != '\0') {
            ++length;
        }
        while (length > 1 && current_directory[length - 1] != '\\') {
            --length;
        }
        if (length > 1) {
            current_directory[length - 1] = '\0';
        } else {
            current_directory[0] = '\\';
            current_directory[1] = '\0';
        }
        return 1;
    }
    if (!make_current_path(input, path) ||
        !filesystem_find(path, &entry) ||
        (entry.attributes & 0x10) == 0) {
        return 0;
    }
    if (path[0] == '\\') {
        u32 index = 1;
        while (path[index] != '\0' &&
            index < sizeof(current_directory) - 1) {
            current_directory[index - 1] = path[index];
            ++index;
        }
        current_directory[index - 1] = '\0';
        if (current_directory[0] == '\0') {
            current_directory[0] = '\\';
            current_directory[1] = '\0';
        }
    } else {
        u32 index;
        for (index = 0; path[index] != '\0' &&
            index < sizeof(current_directory) - 1; ++index) {
            current_directory[index] = path[index];
        }
        current_directory[index] = '\0';
    }
    return 1;
}

static void run_command(char *line)
{
    char *arguments = line;
    fs_entry entry;
    u32 ticks;
    extern char __kernel_end;

    while (*arguments == ' ' || *arguments == '\t') {
        ++arguments;
    }
    if (*arguments == '\0') {
        return;
    }

    line = arguments;
    while (*arguments != '\0' && *arguments != ' ' && *arguments != '\t') {
        ++arguments;
    }
    if (*arguments != '\0') {
        *arguments++ = '\0';
        while (*arguments == ' ' || *arguments == '\t') {
            ++arguments;
        }
    }

    if (string_equals(line, "HELP")) {
        terminal_write("COMMANDS: HELP VER CLS ECHO DIR TYPE RUN WRITE DEL REN MD RD CD TIME MEM PANIC\n");
    } else if (string_equals(line, "VER")) {
        terminal_write("VSDOS version 0.1\n");
    } else if (string_equals(line, "CLS")) {
        terminal_clear();
    } else if (string_equals(line, "ECHO")) {
        terminal_write(arguments);
        terminal_write("\n");
    } else if (string_equals(line, "DIR")) {
        u32 entry_index = 0;
        u32 file_count = 0;
        char *directory = arguments;
        char path[128];

        if (!filesystem_is_mounted()) {
            terminal_write("ERROR: FAT-12 VOLUME NOT MOUNTED.\n");
            return;
        }
        terminal_write("Directory of ");
        terminal_write_char(current_drive_letter);
        terminal_write(":\\");
        if (*directory != '\0') {
            if (!make_current_path(directory, path)) {
                terminal_write("ERROR: PATH TOO LONG.\n");
                return;
            }
            terminal_write(path);
        }
        terminal_write("\n\n");
        while (filesystem_get_entry_in(*directory ? path : current_directory,
                entry_index++, &entry)) {
            terminal_write(entry.name);
            if ((entry.attributes & 0x10) != 0) {
                terminal_write("  <DIR>\n");
            } else {
                u8 padding;
                u8 name_length = 0;
                while (entry.name[name_length] != '\0') {
                    ++name_length;
                }
                for (padding = name_length; padding < 14; ++padding) {
                    terminal_write_char(' ');
                }
                terminal_write_unsigned(entry.size);
                terminal_write(" bytes\n");
                ++file_count;
            }
        }
        terminal_write_unsigned(file_count);
        terminal_write(" file(s)\n");
    } else if (string_equals(line, "TYPE")) {
        char path[128];

        if (*arguments != '\0' && !make_current_path(arguments, path)) {
            terminal_write("ERROR: PATH TOO LONG.\n");
            return;
        }
        if (!filesystem_is_mounted()) {
            terminal_write("ERROR: FAT-12 VOLUME NOT MOUNTED.\n");
        } else if (!filesystem_find(*arguments ? path : current_directory,
                &entry)) {
            terminal_write("FILE NOT FOUND: ");
            terminal_write(arguments);
            terminal_write("\n");
        } else if ((entry.attributes & 0x10) != 0) {
            terminal_write("CAN'T TYPE A DIRECTORY.\n");
        } else if (!filesystem_read_file(&entry, terminal_write_bytes)) {
            terminal_write("\nERROR: DISK READ FAILED OR FAT-12 CHAIN INVALID.\n");
        } else {
            terminal_write("\n");
        }
    } else if (string_equals(line, "RUN")) {
        char path[128];

        if (!filesystem_is_mounted() || *arguments == '\0' ||
            !make_current_path(arguments, path) || !run_app(path)) {
            terminal_write("ERROR: APP FAILED OR NOT FOUND.\n");
        }
    } else if (string_equals(line, "WRITE")) {
        char *name = arguments;
        char *data;
        char path[128];
        u32 length = 0;

        while (*arguments != '\0' && *arguments != ' ' &&
            *arguments != '\t') {
            ++arguments;
        }
        if (*arguments != '\0') {
            *arguments++ = '\0';
            while (*arguments == ' ' || *arguments == '\t') {
                ++arguments;
            }
        }
        data = arguments;
        while (data[length] != '\0') {
            ++length;
        }
        if (!filesystem_is_mounted()) {
            terminal_write("ERROR: FAT-12 VOLUME NOT MOUNTED.\n");
        } else if (*name == '\0' || !make_current_path(name, path) ||
            !filesystem_write_file(path, data, length)) {
            terminal_write("ERROR: FILE WRITE FAILED.\n");
        } else {
            terminal_write("FILE WRITTEN.\n");
        }
    } else if (string_equals(line, "DEL")) {
        char path[128];

        if (!filesystem_is_mounted()) {
            terminal_write("ERROR: FAT-12 VOLUME NOT MOUNTED.\n");
        } else if (*arguments == '\0' || !make_current_path(arguments, path) ||
            !filesystem_delete_file(path)) {
            terminal_write("ERROR: FILE DELETE FAILED.\n");
        } else {
            terminal_write("FILE DELETED.\n");
        }
    } else if (string_equals(line, "REN")) {
        char *old_name = arguments;
        char *new_name;
        char *new_name_end;
        char old_path[128];
        char new_path[128];

        while (*arguments != '\0' && *arguments != ' ' &&
            *arguments != '\t') {
            ++arguments;
        }
        if (*arguments != '\0') {
            *arguments++ = '\0';
            while (*arguments == ' ' || *arguments == '\t') {
                ++arguments;
            }
        }
        new_name = arguments;
        new_name_end = new_name;
        while (*new_name_end != '\0' && *new_name_end != ' ' &&
            *new_name_end != '\t') {
            ++new_name_end;
        }
        *new_name_end = '\0';
        if (!filesystem_is_mounted()) {
            terminal_write("ERROR: FAT-12 VOLUME NOT MOUNTED.\n");
        } else if (*old_name == '\0' || *new_name == '\0' ||
            !make_current_path(old_name, old_path) ||
            !make_current_path(new_name, new_path) ||
            !filesystem_rename_file(old_path, new_path)) {
            terminal_write("ERROR: FILE RENAME FAILED.\n");
        } else {
            terminal_write("FILE RENAMED.\n");
        }
    } else if (string_equals(line, "MD")) {
        char path[128];

        if (!filesystem_is_mounted() || *arguments == '\0' ||
            !make_current_path(arguments, path) ||
            !filesystem_make_directory(path)) {
            terminal_write("ERROR: DIRECTORY CREATE FAILED.\n");
        } else {
            terminal_write("DIRECTORY CREATED.\n");
        }
    } else if (string_equals(line, "RD")) {
        char path[128];

        if (!filesystem_is_mounted() || *arguments == '\0' ||
            !make_current_path(arguments, path) ||
            !filesystem_remove_directory(path)) {
            terminal_write("ERROR: DIRECTORY REMOVE FAILED.\n");
        } else {
            terminal_write("DIRECTORY REMOVED.\n");
        }
    } else if (string_equals(line, "CD") ||
        string_equals(line, "CHDIR")) {
        if (!filesystem_is_mounted() || !change_directory(arguments)) {
            terminal_write("ERROR: INVALID DIRECTORY.\n");
        }
    } else if (string_equals(line, "TIME")) {
        ticks = timer_get_ticks();
        terminal_write("Uptime: ");
        terminal_write_unsigned(ticks / 6000);
        terminal_write_char(':');
        if ((ticks / 100) % 60 < 10) {
            terminal_write_char('0');
        }
        terminal_write_unsigned((ticks / 100) % 60);
        terminal_write_char('.');
        terminal_write_unsigned((ticks / 10) % 10);
        terminal_write(" seconds\n");
    } else if (string_equals(line, "MEM")) {
        terminal_write("Kernel image: ");
        terminal_write_unsigned((u32)&__kernel_end - 0x00010000);
        terminal_write(" bytes\n");
        terminal_write("Kernel stack top: 0x00090000\n");
    } else if (string_equals(line, "PANIC")) {
        __asm__ volatile ("ud2");
    } else {
        terminal_write("BAD COMMAND OR FILE NAME - TYPE HELP FOR COMMANDS.\n");
    }
}

static char key_to_ascii(u8 scan_code, u8 shifted)
{
    static const char plain[58] = {
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
        [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
        [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
        [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
        [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
        [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
        [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
        [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
        [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c',
        [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
        [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' '
    };
    static const char shifted_keys[58] = {
        [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
        [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
        [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
        [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
        [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
        [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
        [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
        [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
        [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
        [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C',
        [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
        [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' '
    };

    if (scan_code >= sizeof(plain)) {
        return '\0';
    }
    if (shifted && shifted_keys[scan_code] != '\0') {
        return shifted_keys[scan_code];
    }
    return plain[scan_code];
}

static void keyboard_process_scancode(u8 scan_code)
{
    static u8 left_shift;
    static u8 right_shift;
    static u8 caps_lock;
    u8 character;

    if (scan_code == 0xE0) {
        return;
    }
    if (scan_code == 0x2A || scan_code == 0x36) {
        if (scan_code == 0x2A) {
            left_shift = 1;
        } else {
            right_shift = 1;
        }
        return;
    }
    if (scan_code == 0xAA || scan_code == 0xB6) {
        if (scan_code == 0xAA) {
            left_shift = 0;
        } else {
            right_shift = 0;
        }
        return;
    }
    if ((scan_code & 0x80) != 0) {
        return;
    }
    if (scan_code == 0x3A) {
        caps_lock = (u8)!caps_lock;
        return;
    }
    if (scan_code == 0x1C) {
        terminal_write("\n");
        command_line[command_length] = '\0';
        run_command(command_line);
        command_length = 0;
        print_prompt();
        return;
    }
    if (scan_code == 0x0E) {
        if (command_length != 0) {
            --command_length;
            terminal_write_char('\b');
        }
        return;
    }

    character = (u8)key_to_ascii(
        scan_code,
        (u8)((left_shift || right_shift) != 0));
    if (character >= 'A' && character <= 'Z') {
        character = (u8)(character - 'A' + 'a');
    }
    if (character >= 'a' && character <= 'z' &&
        (((left_shift || right_shift) != 0) != (caps_lock != 0))) {
        character = (u8)uppercase((char)character);
    }
    if (character != '\0' && command_length < sizeof(command_line) - 1) {
        command_line[command_length++] = (char)character;
        terminal_write_char((char)character);
    }
}

#endif

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
    if (!run_app("\\COMMAND.APP", SHELL_LOAD_ADDRESS)) {
        terminal_write("[FAIL] COMMAND.APP COULD NOT BE STARTED\n");
        for (;;) {
            interrupts_wait();
        }
    }
}
