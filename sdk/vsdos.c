#include "vsdos.h"

static char scan_to_ascii(vsdos_u32 scan_code)
{
    switch (scan_code) {
    case 0x10: return 'q'; case 0x11: return 'w';
    case 0x12: return 'e'; case 0x13: return 'r';
    case 0x14: return 't'; case 0x15: return 'y';
    case 0x16: return 'u'; case 0x17: return 'i';
    case 0x18: return 'o'; case 0x19: return 'p';
    case 0x1E: return 'a'; case 0x1F: return 's';
    case 0x20: return 'd'; case 0x21: return 'f';
    case 0x22: return 'g'; case 0x23: return 'h';
    case 0x24: return 'j'; case 0x25: return 'k';
    case 0x26: return 'l'; case 0x2C: return 'z';
    case 0x2D: return 'x'; case 0x2E: return 'c';
    case 0x2F: return 'v'; case 0x30: return 'b';
    case 0x31: return 'n'; case 0x32: return 'm';
    case 0x39: return ' ';
    case 0x02: return '1'; case 0x03: return '2';
    case 0x04: return '3'; case 0x05: return '4';
    case 0x06: return '5'; case 0x07: return '6';
    case 0x08: return '7'; case 0x09: return '8';
    case 0x0A: return '9'; case 0x0B: return '0';
    case 0x0C: return '-'; case 0x0D: return '=';
    case 0x1A: return '['; case 0x1B: return ']';
    case 0x27: return ';'; case 0x28: return '\'';
    case 0x2B: return '\\'; case 0x33: return ',';
    case 0x34: return '.'; case 0x35: return '/';
    }
    return '\0';
}

vsdos_u32 vsdos_strlen(const char *text)
{
    vsdos_u32 length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

void vsdos_print_unsigned(vsdos_u32 value)
{
    char digits[10];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        vsdos_print(&digits[--count], 1);
    }
}

void vsdos_print_signed(int value)
{
    if (value < 0) {
        vsdos_print("-", 1);
        vsdos_print_unsigned((vsdos_u32)(-value));
    } else {
        vsdos_print_unsigned((vsdos_u32)value);
    }
}

vsdos_u32 vsdos_read_line(char *buffer, vsdos_u32 capacity)
{
    vsdos_u32 length = 0;
    if (buffer == 0 || capacity < 2) {
        return 0;
    }
    for (;;) {
        vsdos_u32 scan_code = vsdos_read_key();
        char character;
        if (scan_code == 0) {
            continue;
        }
        if (scan_code == 0x1C) {
            buffer[length] = '\0';
            vsdos_print("\r\n", 2);
            return length;
        }
        if (scan_code == 0x0E) {
            if (length != 0) {
                --length;
                vsdos_print("\b \b", 3);
            }
            continue;
        }
        character = scan_to_ascii(scan_code);
        if (character != '\0' && length + 1 < capacity) {
            buffer[length++] = character;
            vsdos_print(&character, 1);
        }
    }
}
