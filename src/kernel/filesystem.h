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
int filesystem_find(const char *name, fs_entry *entry);
int filesystem_read_file(const fs_entry *entry, void (*write)(const char *, u32));

#endif
