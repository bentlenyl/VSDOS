#include "filesystem.h"
#include "disk.h"

#define FAT_SECTOR_SIZE 512
#define FAT_MAX_SECTORS_PER_FAT 9
#define FAT_ROOT_ENTRY_COUNT 224
#define FAT_ROOT_BUFFER_SIZE (FAT_ROOT_ENTRY_COUNT * 32)
#define FAT_ROOT_SECTORS (FAT_ROOT_BUFFER_SIZE / FAT_SECTOR_SIZE)
#define FAT_CLUSTER_MIN 2
#define FAT12_EOC 0x0FF8
#define FAT_ATTRIBUTE_DIRECTORY 0x10
#define FAT_ATTRIBUTE_VOLUME 0x08
#define FAT_ATTRIBUTE_LONG_NAME 0x0F

static u8 fat_table[FAT_MAX_SECTORS_PER_FAT * FAT_SECTOR_SIZE];
static u8 root_directory[FAT_ROOT_BUFFER_SIZE];
static u8 sector_buffer[FAT_SECTOR_SIZE];
static u32 root_entry_count;
static u32 root_directory_sectors;
static u32 root_directory_lba;
static u32 data_lba;
static u32 cluster_count;
static u32 sectors_per_cluster;
static int filesystem_mounted;

static u16 read_u16(const u8 *bytes)
{
    return (u16)(bytes[0] | ((u16)bytes[1] << 8));
}

static u32 read_u32(const u8 *bytes)
{
    return (u32)bytes[0] |
        ((u32)bytes[1] << 8) |
        ((u32)bytes[2] << 16) |
        ((u32)bytes[3] << 24);
}

static u8 uppercase(u8 character)
{
    if (character >= 'a' && character <= 'z') {
        return (u8)(character - 'a' + 'A');
    }
    return character;
}

static int fat_entry_active(const u8 *raw_entry)
{
    return raw_entry[0] != 0 && raw_entry[0] != 0xE5 &&
        raw_entry[11] != FAT_ATTRIBUTE_LONG_NAME &&
        (raw_entry[11] & FAT_ATTRIBUTE_VOLUME) == 0;
}

static void format_filename(const u8 *raw_name, char *name)
{
    u32 output = 0;
    u32 index;

    for (index = 0; index < 8 && raw_name[index] != ' '; ++index) {
        name[output++] = (char)raw_name[index];
    }

    for (index = 0; index < 3 && raw_name[8 + index] != ' '; ++index) {
        if (index == 0) {
            name[output++] = '.';
        }
        name[output++] = (char)raw_name[8 + index];
    }
    name[output] = '\0';
}

static int make_fat_filename(const char *input, u8 output[11])
{
    u32 base_length = 0;
    u32 extension_length = 0;
    u32 index;
    int in_extension = 0;

    for (index = 0; index < 11; ++index) {
        output[index] = ' ';
    }

    if (*input == '\0' || *input == '.' || *input == '\\' || *input == '/') {
        return 0;
    }

    while (*input != '\0') {
        u8 character = uppercase((u8)*input++);

        if (character == '.') {
            if (in_extension || base_length == 0) {
                return 0;
            }
            in_extension = 1;
            continue;
        }
        if (character <= ' ' || character >= 0x7F ||
            character == '"' || character == '*' || character == '+' ||
            character == ',' || character == ':' || character == ';' ||
            character == '<' || character == '=' || character == '>' ||
            character == '?' || character == '[' || character == ']' ||
            character == '|') {
            return 0;
        }

        if (in_extension) {
            if (++extension_length > 3) {
                return 0;
            }
            output[7 + extension_length] = character;
        } else {
            if (++base_length > 8) {
                return 0;
            }
            output[base_length - 1] = character;
        }
    }

    return base_length != 0;
}

int filesystem_mount(void)
{
    u8 boot_sector[FAT_SECTOR_SIZE];
    u16 bytes_per_sector;
    u16 reserved_sectors;
    u16 root_entries;
    u16 total_sectors;
    u16 sectors_per_fat;
    u8 number_of_fats;
    u8 sectors_per_cluster_byte;
    u32 total_fat_sectors;
    u32 index;

    filesystem_mounted = 0;
    if (!disk_read_sector(0, boot_sector)) {
        return 0;
    }
    if (boot_sector[510] != 0x55 || boot_sector[511] != 0xAA) {
        return 0;
    }

    bytes_per_sector = read_u16(&boot_sector[11]);
    sectors_per_cluster_byte = boot_sector[13];
    reserved_sectors = read_u16(&boot_sector[14]);
    number_of_fats = boot_sector[16];
    root_entries = read_u16(&boot_sector[17]);
    total_sectors = read_u16(&boot_sector[19]);
    if (total_sectors == 0) {
        total_sectors = (u16)read_u32(&boot_sector[32]);
    }
    sectors_per_fat = read_u16(&boot_sector[22]);

    if (bytes_per_sector != FAT_SECTOR_SIZE ||
        sectors_per_cluster_byte == 0 ||
        (sectors_per_cluster_byte & (sectors_per_cluster_byte - 1)) != 0 ||
        reserved_sectors == 0 || number_of_fats == 0 ||
        root_entries == 0 || sectors_per_fat == 0 ||
        total_sectors == 0 || sectors_per_cluster_byte > 8) {
        return 0;
    }
    if (root_entries > FAT_ROOT_ENTRY_COUNT ||
        sectors_per_fat > FAT_MAX_SECTORS_PER_FAT) {
        return 0;
    }

    root_directory_sectors =
        ((u32)root_entries * 32 + FAT_SECTOR_SIZE - 1) / FAT_SECTOR_SIZE;
    total_fat_sectors = (u32)number_of_fats * sectors_per_fat;
    root_directory_lba = reserved_sectors + total_fat_sectors;
    data_lba = root_directory_lba + root_directory_sectors;
    if (data_lba >= total_sectors) {
        return 0;
    }

    cluster_count =
        (total_sectors - data_lba) / sectors_per_cluster_byte;
    if (cluster_count == 0 || cluster_count >= 4085 ||
        (cluster_count + 2) * 3 / 2 > sectors_per_fat * FAT_SECTOR_SIZE) {
        return 0;
    }

    for (index = 0; index < sectors_per_fat; ++index) {
        if (!disk_read_sector(reserved_sectors + index,
                &fat_table[index * FAT_SECTOR_SIZE])) {
            return 0;
        }
    }
    for (index = 0; index < root_directory_sectors; ++index) {
        if (!disk_read_sector(root_directory_lba + index,
                &root_directory[index * FAT_SECTOR_SIZE])) {
            return 0;
        }
    }

    root_entry_count = root_entries;
    sectors_per_cluster = sectors_per_cluster_byte;
    filesystem_mounted = 1;
    return 1;
}

int filesystem_is_mounted(void)
{
    return filesystem_mounted;
}

int filesystem_get_entry(u32 index, fs_entry *entry)
{
    u32 current;

    if (!filesystem_mounted || entry == 0) {
        return 0;
    }

    for (current = 0; current < root_entry_count; ++current) {
        const u8 *raw_entry = &root_directory[current * 32];

        if (raw_entry[0] == 0) {
            return 0;
        }
        if (!fat_entry_active(raw_entry)) {
            continue;
        }
        if (index != 0) {
            --index;
            continue;
        }

        format_filename(raw_entry, entry->name);
        entry->size = read_u32(&raw_entry[28]);
        entry->first_cluster = read_u16(&raw_entry[26]);
        entry->attributes = raw_entry[11];
        return 1;
    }

    return 0;
}

int filesystem_find(const char *name, fs_entry *entry)
{
    u8 wanted[11];
    u32 index;

    if (!filesystem_mounted || entry == 0 ||
        !make_fat_filename(name, wanted)) {
        return 0;
    }

    for (index = 0; index < root_entry_count; ++index) {
        const u8 *raw_entry = &root_directory[index * 32];
        u32 character;
        int matches = 1;

        if (raw_entry[0] == 0) {
            return 0;
        }
        if (!fat_entry_active(raw_entry)) {
            continue;
        }
        for (character = 0; character < sizeof(wanted); ++character) {
            if (uppercase(raw_entry[character]) != wanted[character]) {
                matches = 0;
                break;
            }
        }
        if (matches) {
            format_filename(raw_entry, entry->name);
            entry->size = read_u32(&raw_entry[28]);
            entry->first_cluster = read_u16(&raw_entry[26]);
            entry->attributes = raw_entry[11];
            return 1;
        }
    }
    return 0;
}

static int next_cluster(u16 cluster, u16 *next)
{
    u32 offset = cluster + cluster / 2;
    u16 value;

    if (cluster < FAT_CLUSTER_MIN || cluster >= cluster_count + 2 ||
        offset + 1 >= sizeof(fat_table)) {
        return 0;
    }
    value = read_u16(&fat_table[offset]);
    if ((cluster & 1) != 0) {
        value >>= 4;
    }
    *next = value & 0x0FFF;
    return 1;
}

int filesystem_read_file(const fs_entry *entry, void (*write)(const char *, u32))
{
    u32 remaining;
    u32 cluster_steps = 0;
    u16 cluster;

    if (!filesystem_mounted || entry == 0 || write == 0 ||
        (entry->attributes & FAT_ATTRIBUTE_DIRECTORY) != 0) {
        return 0;
    }
    if (entry->size == 0) {
        return 1;
    }

    remaining = entry->size;
    cluster = entry->first_cluster;
    while (remaining != 0) {
        u32 sector_in_cluster;

        if (cluster < FAT_CLUSTER_MIN || cluster >= cluster_count + 2 ||
            ++cluster_steps > cluster_count) {
            return 0;
        }

        for (sector_in_cluster = 0;
            sector_in_cluster < sectors_per_cluster && remaining != 0;
            ++sector_in_cluster) {
            u32 lba = data_lba +
                (u32)(cluster - FAT_CLUSTER_MIN) * sectors_per_cluster +
                sector_in_cluster;
            u32 bytes = remaining < FAT_SECTOR_SIZE
                ? remaining
                : FAT_SECTOR_SIZE;

            if (!disk_read_sector(lba, sector_buffer)) {
                return 0;
            }
            write((const char *)sector_buffer, bytes);
            remaining -= bytes;
        }

        if (remaining != 0) {
            u16 following_cluster;

            if (!next_cluster(cluster, &following_cluster) ||
                following_cluster >= FAT12_EOC) {
                return 0;
            }
            cluster = following_cluster;
        }
    }

    return 1;
}
