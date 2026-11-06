#include "vsdos.h"

static void print_arguments(int argc, char **argv)
{
    int index;
    vsdos_print("Arguments: ", 11);
    vsdos_print_unsigned((vsdos_u32)argc);
    vsdos_print("\r\n", 2);
    for (index = 0; index < argc; ++index) {
        vsdos_print("  ", 2);
        vsdos_print(argv[index], vsdos_strlen(argv[index]));
        vsdos_print("\r\n", 2);
    }
}

int main(int argc, char **argv)
{
    char name[64];
    char path[80];
    static const char prompt[] = "Enter filename: ";
    static const char content[] = "Created by the VSDOS SDK\r\n";
    char drive;

    vsdos_clear();
    {
        static const char title[] = "VSDOS SDK Utility\r\n\r\n";
        vsdos_print(title, sizeof(title) - 1);
    }
    print_arguments(argc, argv);

    vsdos_print(prompt, sizeof(prompt) - 1);
    if (vsdos_read_line(name, sizeof(name)) == 0) {
        vsdos_print("Canceled.\r\n", 11);
        return 0;
    }

    drive = vsdos_drive_letter();
    path[0] = '\\';
    path[1] = '\0';
    {
        vsdos_u32 name_length = vsdos_strlen(name);
        vsdos_u32 index;
        for (index = 0; index < name_length && index < 78; ++index) {
            path[index + 1] = name[index];
        }
        path[name_length + 1] = '\0';
    }
    if (vsdos_write_file(path, content, vsdos_strlen(content)) == 0) {
        vsdos_print("Created ", 8);
        vsdos_print(&drive, 1);
        vsdos_print(path, vsdos_strlen(path));
        vsdos_print("\r\n", 2);
        return 0;
    }
    vsdos_print("File creation failed.\r\n", 23);
    return 0;
}
