#include "vsdos.h"

static char current_directory[128] = "\\";
static char line[128];
static char print_buffer[256];
static char batch_line[128];
static unsigned int batch_depth;

static unsigned int text_length(const char *text)
{
    unsigned int length = 0;

    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void print(const char *text)
{
    unsigned int length = 0;

    while (*text != '\0') {
        char character = *text++;

        if (character >= 'a' && character <= 'z') {
            character = (char)(character - 'a' + 'A');
        }
        print_buffer[length++] = character;
        if (length == sizeof(print_buffer)) {
            vsdos_print_text(print_buffer, length);
            length = 0;
        }
    }
    if (length != 0) {
        vsdos_print_text(print_buffer, length);
    }
}

static int equals(const char *left, const char *right)
{
    unsigned int index = 0;

    while (left[index] != '\0' || right[index] != '\0') {
        char a = left[index];
        char b = right[index];

        if (a >= 'a' && a <= 'z') {
            a = (char)(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = (char)(b - 'a' + 'A');
        }
        if (a != b) {
            return 0;
        }
        ++index;
    }
    return 1;
}

static void number(unsigned int value)
{
    char digits[10];
    unsigned int count = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        char digit = digits[--count];
        vsdos_print(&digit, 1);
    }
}

static void hexadecimal(unsigned int value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    print("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        char digit = digits[(value >> shift) & 0x0F];
        vsdos_print(&digit, 1);
    }
}

static int make_path(const char *input, char output[128])
{
    unsigned int length = 0;
    unsigned int index;

    if (*input == '\0') {
        return 0;
    }
    if (*input == '\\' || *input == '/') {
        while (*input != '\0' && length < 127) {
            output[length++] = *input++;
        }
    } else {
        while (current_directory[length] != '\0' && length < 127) {
            output[length] = current_directory[length];
            ++length;
        }
        if (length > 1 && length < 127) {
            output[length++] = '\\';
        }
        for (index = 0; input[index] != '\0' && length < 127; ++index) {
            output[length++] = input[index];
        }
        input += index;
    }
    if (*input != '\0') {
        return 0;
    }
    output[length] = '\0';
    return 1;
}

static int has_extension(const char *name, const char *extension)
{
    unsigned int name_length = text_length(name);
    unsigned int extension_length = text_length(extension);
    unsigned int index;

    if (name_length < extension_length) {
        return 0;
    }
    for (index = 0; index < extension_length; ++index) {
        char a = name[name_length - extension_length + index];
        char b = extension[index];
        if (a >= 'a' && a <= 'z') {
            a = (char)(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = (char)(b - 'a' + 'A');
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static int execute_batch(const char *path);
static void command(char *command_line);

static void execute_batch_line(char *batch_command)
{
    unsigned int index = 0;
    char rem[4];

    while (*batch_command == ' ' || *batch_command == '\t') {
        ++batch_command;
    }
    if (*batch_command == '@') {
        ++batch_command;
        while (*batch_command == ' ' || *batch_command == '\t') {
            ++batch_command;
        }
    }
    rem[0] = batch_command[0];
    rem[1] = batch_command[1];
    rem[2] = batch_command[2];
    rem[3] = batch_command[3];
    for (index = 0; index < 4; ++index) {
        if (rem[index] >= 'a' && rem[index] <= 'z') {
            rem[index] = (char)(rem[index] - 'a' + 'A');
        }
    }
    if (*batch_command == '\0' ||
        (rem[0] == 'R' && rem[1] == 'E' &&
         rem[2] == 'M' &&
         (rem[3] == ' ' || rem[3] == '\t' || rem[3] == '\0'))) {
        return;
    }
    index = 0;
    while (batch_command[index] != '\0' &&
           batch_command[index] != '\r' &&
           batch_command[index] != '\n') {
        ++index;
    }
    batch_command[index] = '\0';
    while (index != 0 &&
           (batch_command[index - 1] == ' ' ||
            batch_command[index - 1] == '\t')) {
        batch_command[--index] = '\0';
    }
    if (*batch_command != '\0') {
        command(batch_command);
    }
}

static int execute_batch(const char *path)
{
    static const unsigned int maximum_batch_bytes = 4096;
    char buffer[4096];
    unsigned int length;
    unsigned int index = 0;

    if (batch_depth >= 8) {
        print("BATCH NESTING TOO DEEP.\n");
        return 0;
    }
    length = vsdos_read_file(path, buffer, maximum_batch_bytes);
    if (length == 0xFFFFFFFF) {
        return 0;
    }
    if (length > maximum_batch_bytes) {
        length = maximum_batch_bytes;
    }
    ++batch_depth;
    while (index < length) {
        unsigned int line_length = 0;
        while (index < length &&
               buffer[index] != '\r' &&
               buffer[index] != '\n' &&
               line_length < sizeof(batch_line) - 1) {
            batch_line[line_length++] = buffer[index++];
        }
        while (index < length &&
               (buffer[index] == '\r' || buffer[index] == '\n')) {
            ++index;
        }
        batch_line[line_length] = '\0';
        execute_batch_line(batch_line);
    }
    --batch_depth;
    return 1;
}

static void prompt(void)
{
    char drive = vsdos_drive_letter();

    vsdos_print(&drive, 1);
    print(current_directory);
    print(">");
}

static int directory_exists(const char *path)
{
    vsdos_dirent entry;
    return vsdos_list(path, 0, &entry) == 0;
}

static void command(char *command_line)
{
    char *argument = command_line;
    char command_name[16];
    char path[128];
    vsdos_dirent entry;
    unsigned int command_length = 0;

    while (*argument == ' ' || *argument == '\t') {
        ++argument;
    }
    while (*argument != '\0' && *argument != ' ' && *argument != '\t') {
        if (command_length < sizeof(command_name) - 1) {
            command_name[command_length++] = *argument;
        }
        ++argument;
    }
    command_name[command_length] = '\0';
    if (*argument != '\0') {
        *argument++ = '\0';
        while (*argument == ' ' || *argument == '\t') {
            ++argument;
        }
    }
    if (equals(command_name, "CALL")) {
        if (!make_path(argument, path) || !execute_batch(path)) {
            print("BATCH FILE NOT FOUND: ");
            print(argument);
            print("\n");
        }
    } else if (has_extension(command_name, ".BAT")) {
        if (!make_path(command_name, path) || !execute_batch(path)) {
            print("BATCH FILE NOT FOUND: ");
            print(command_name);
            print("\n");
        }
    } else if (has_extension(command_name, ".APP")) {
        if (!make_path(command_name, path) ||
            vsdos_run_app_args(path, argument) != 0) {
            print("ERROR: APP FAILED OR NOT FOUND.\n");
        }
    } else if (equals(command_name, "HELP")) {
        print("COMMANDS: HELP VER CLS ECHO DIR TYPE RUN WRITE DEL REN MD RD CD TIME MEM PANIC CALL\n");
    } else if (equals(command_name, "VER")) {
        print("VSDOS version 0.1\n");
    } else if (equals(command_name, "CLS")) {
        vsdos_clear();
    } else if (equals(command_name, "ECHO")) {
        print(argument);
        print("\n");
    } else if (equals(command_name, "DIR")) {
        unsigned int index = 0;
        unsigned int files = 0;
        char *directory = argument;
        const char *list_path = current_directory;

        if (*directory != '\0') {
            if (!make_path(directory, path)) {
                print("ERROR: PATH TOO LONG.\n");
                return;
            }
            list_path = path;
        }
        char drive = vsdos_drive_letter();

        print("Directory of ");
        vsdos_print(&drive, 1);
        print(":");
        print(list_path);
        print("\n\n");
        while (vsdos_list(list_path, index++, &entry) == 0) {
            const char *display_name = entry.name;
            print(display_name);
            if ((entry.attributes & 0x10) != 0) {
                print("  <DIR>\n");
            } else {
                unsigned int length = text_length(display_name);
                while (length++ < 14) {
                    print(" ");
                }
                number(entry.size);
                print(" bytes\n");
                ++files;
            }
        }
        (void)files;
        print("\n");
    } else if (equals(command_name, "TYPE")) {
        if (!make_path(argument, path)) {
            print("ERROR: PATH TOO LONG.\n");
        } else {
            unsigned int result = vsdos_type_file(path);
            if (result == 0xFFFFFFFF) {
                print("FILE NOT FOUND: ");
                print(argument);
                print("\n");
            } else {
                print("\n");
            }
        }
    } else if (equals(command_name, "RUN")) {
        char *app_name = argument;
        char *app_arguments = argument;

        while (*app_arguments != '\0' && *app_arguments != ' ' &&
               *app_arguments != '\t') {
            ++app_arguments;
        }
        if (*app_arguments != '\0') {
            *app_arguments++ = '\0';
            while (*app_arguments == ' ' || *app_arguments == '\t') {
                ++app_arguments;
            }
        }
        if (!make_path(app_name, path) ||
            vsdos_run_app_args(path, app_arguments) != 0) {
            print("ERROR: APP FAILED OR NOT FOUND.\n");
        }
    } else if (equals(command_name, "WRITE")) {
        char *name = argument;

        while (*argument != '\0' && *argument != ' ' &&
            *argument != '\t') {
            ++argument;
        }
        if (*argument != '\0') {
            *argument++ = '\0';
            while (*argument == ' ' || *argument == '\t') {
                ++argument;
            }
        }
        if (!make_path(name, path) ||
            vsdos_write_file(path, argument, text_length(argument)) != 0) {
            print("ERROR: FILE WRITE FAILED.\n");
        } else {
            print("FILE WRITTEN.\n");
        }
    } else if (equals(command_name, "DEL")) {
        if (!make_path(argument, path) ||
            vsdos_delete_file(path) != 0) {
            print("ERROR: FILE DELETE FAILED.\n");
        } else {
            print("FILE DELETED.\n");
        }
    } else if (equals(command_name, "REN")) {
        char *old_name = argument;

        while (*argument != '\0' && *argument != ' ' &&
            *argument != '\t') {
            ++argument;
        }
        if (*argument != '\0') {
            *argument++ = '\0';
            while (*argument == ' ' || *argument == '\t') {
                ++argument;
            }
        }
        if (!make_path(old_name, path)) {
            print("ERROR: FILE RENAME FAILED.\n");
        } else {
            char new_path[128];
            if (!make_path(argument, new_path) ||
                vsdos_rename_file(path, new_path) != 0) {
                print("ERROR: FILE RENAME FAILED.\n");
            } else {
                print("FILE RENAMED.\n");
            }
        }
    } else if (equals(command_name, "MD")) {
        if (!make_path(argument, path) ||
            vsdos_make_directory(path) != 0) {
            print("ERROR: DIRECTORY CREATE FAILED.\n");
        } else {
            print("DIRECTORY CREATED.\n");
        }
    } else if (equals(command_name, "RD")) {
        if (!make_path(argument, path) ||
            vsdos_remove_directory(path) != 0) {
            print("ERROR: DIRECTORY REMOVE FAILED.\n");
        } else {
            print("DIRECTORY REMOVED.\n");
        }
    } else if (equals(command_name, "CD") || equals(command_name, "CHDIR")) {
        if (equals(argument, "\\") || equals(argument, "/")) {
            current_directory[0] = '\\';
            current_directory[1] = '\0';
        } else if (equals(argument, "..")) {
            unsigned int length = text_length(current_directory);
            while (length > 1 && current_directory[length - 1] != '\\') {
                --length;
            }
            if (length > 1) {
                current_directory[length - 1] = '\0';
            } else {
                current_directory[0] = '\\';
                current_directory[1] = '\0';
            }
        } else if (make_path(argument, path) && directory_exists(path)) {
            unsigned int index = 1;
            while (path[index] != '\0') {
                current_directory[index - 1] = path[index];
                ++index;
            }
            current_directory[index - 1] = '\0';
        } else {
            print("ERROR: INVALID DIRECTORY.\n");
        }
    } else if (equals(command_name, "TIME")) {
        unsigned int ticks = vsdos_ticks();
        print("Uptime: ");
        number(ticks / 6000);
        print(":");
        if ((ticks / 100) % 60 < 10) {
            print("0");
        }
        number((ticks / 100) % 60);
        print(".");
        number((ticks / 10) % 10);
        print(" seconds\n");
    } else if (equals(command_name, "MEM")) {
        print("Kernel image: ");
        number(vsdos_kernel_memory_size());
        print(" bytes\n");
        print("Kernel stack top: ");
        hexadecimal(vsdos_kernel_stack_top());
        print("\n");
    } else if (equals(command_name, "PANIC")) {
        __asm__ volatile ("ud2");
    } else {
        print("BAD COMMAND OR FILE NAME - TYPE HELP FOR COMMANDS.\n");
    }
}

static char key_character(unsigned int scan_code)
{
    static const char keys[] =
        "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0"
        "asdfghjkl;'" "`\0" "\\zxcvbnm,./" "\0\0\0" " ";

    if (scan_code < sizeof(keys)) {
        return keys[scan_code];
    }
    return '\0';
}

int main(void)
{
    unsigned int length = 0;

    print("VSDOS version 0.1\n");
    print("TYPE HELP FOR COMMANDS - USING FAT-12.\n\n");
    execute_batch("\\AUTOEXEC.BAT");
    prompt();
    for (;;) {
        unsigned int scan_code = vsdos_read_key();
        char character;

        if (scan_code == 0) {
            continue;
        }
        if (scan_code == 0x1C) {
            line[length] = '\0';
            print("\n");
            command(line);
            length = 0;
            prompt();
        } else if (scan_code == 0x0E && length != 0) {
            --length;
            vsdos_print("\b", 1);
        } else if ((scan_code & 0x80) == 0 &&
            (character = key_character(scan_code)) != '\0' &&
            length < sizeof(line) - 1) {
            line[length++] = character;
            vsdos_print(&character, 1);
        }
    }
}
