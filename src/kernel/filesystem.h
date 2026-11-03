#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "interrupts.h"

typedef struct {
    char name[13];
    u32 size;
    u16 first_cluster;
    u8 attributes;
} fs_entry;

int filesystem_mount(void);
int filesystem_is_mounted(void);
int filesystem_get_entry(u32 index, fs_entry *entry);
int filesystem_get_entry_in(const char *path, u32 index, fs_entry *entry);
int filesystem_find(const char *name, fs_entry *entry);
int filesystem_read_file(const fs_entry *entry, void (*write)(const char *, u32));
int filesystem_write_file(const char *name, const char *data, u32 size);
int filesystem_delete_file(const char *name);
int filesystem_rename_file(const char *old_name, const char *new_name);
int filesystem_make_directory(const char *name);
int filesystem_remove_directory(const char *name);

#endif
