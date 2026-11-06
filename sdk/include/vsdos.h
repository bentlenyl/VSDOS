#ifndef VSDOS_SDK_H
#define VSDOS_SDK_H

typedef unsigned int vsdos_u32;

#define VSDOS_ERROR ((vsdos_u32)0xFFFFFFFF)
#define VSDOS_ATTR_DIRECTORY 0x10

typedef struct {
    char name[13];
    vsdos_u32 size;
    unsigned short first_cluster;
    unsigned char attributes;
} vsdos_dirent;

void vsdos_print(const char *text, vsdos_u32 length);
void vsdos_print_text(const char *text, vsdos_u32 length);
vsdos_u32 vsdos_type_file(const char *path);
char vsdos_drive_letter(void);
vsdos_u32 vsdos_read_key(void);
vsdos_u32 vsdos_read_file(const char *path, void *buffer, vsdos_u32 capacity);
vsdos_u32 vsdos_write_file(const char *path, const void *data, vsdos_u32 length);
vsdos_u32 vsdos_list(const char *path, vsdos_u32 index, vsdos_dirent *entry);
vsdos_u32 vsdos_delete_file(const char *path);
vsdos_u32 vsdos_rename_file(const char *old_path, const char *new_path);
vsdos_u32 vsdos_make_directory(const char *path);
vsdos_u32 vsdos_remove_directory(const char *path);
vsdos_u32 vsdos_run_app(const char *path);
vsdos_u32 vsdos_run_app_args(const char *path, const char *arguments);
void vsdos_clear(void);
vsdos_u32 vsdos_ticks(void);
vsdos_u32 vsdos_sleep(vsdos_u32 ticks);
vsdos_u32 vsdos_kernel_memory_size(void);
vsdos_u32 vsdos_kernel_stack_top(void);
vsdos_u32 vsdos_get_app_arguments(char *buffer, vsdos_u32 capacity);
vsdos_u32 vsdos_exit_code(void);
void vsdos_exit(vsdos_u32 status);

vsdos_u32 vsdos_strlen(const char *text);
void vsdos_print_unsigned(vsdos_u32 value);
void vsdos_print_signed(int value);
vsdos_u32 vsdos_read_line(char *buffer, vsdos_u32 capacity);

#endif
