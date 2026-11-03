#ifndef VSDOS_SDK_H
#define VSDOS_SDK_H

typedef unsigned int vsdos_u32;

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
void vsdos_clear(void);
vsdos_u32 vsdos_ticks(void);
void vsdos_exit(vsdos_u32 status);

#endif
