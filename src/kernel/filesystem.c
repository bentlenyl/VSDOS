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
#define FAT_ATTRIBUTE_ARCHIVE 0x20
#define FAT12_FREE 0x0000

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


static int next_cluster(u16 cluster, u16 *next)
{
    u32 offset = cluster + cluster / 2;
    u16 value;
    if (cluster < FAT_CLUSTER_MIN || cluster >= cluster_count + 2 || offset + 1 >= sizeof(fat_table)) return 0;
    value = read_u16(&fat_table[offset]);
    if (cluster & 1) value >>= 4;
    *next = value & 0x0FFF;
    return 1;
}
static u16 fat_get(u16 c) { u32 o=c+c/2; u16 v=read_u16(&fat_table[o]); if(c&1)v>>=4; return v&0x0FFF; }
static void fat_set(u16 c,u16 v) { u32 o=c+c/2, old=read_u16(&fat_table[o]); v&=0x0fff; if(!(c&1)){fat_table[o]=(u8)v;fat_table[o+1]=(u8)((old&0xf000)|(v>>8));}else{fat_table[o]=(u8)((old&15)|(v<<4));fat_table[o+1]=(u8)(v>>4);} }
static int write_fat(void) { u8 b[512]; u16 r,spf; u8 nf; u32 c,s; if(!disk_read_sector(0,b))return 0; r=read_u16(&b[14]);nf=b[16];spf=read_u16(&b[22]);for(c=0;c<nf;++c)for(s=0;s<spf;++s)if(!disk_write_sector(r+c*spf+s,&fat_table[s*512]))return 0;return 1; }
static int active(const u8 *r) { return r[0]!=0&&r[0]!=0xe5&&r[11]!=FAT_ATTRIBUTE_LONG_NAME&&(r[11]&FAT_ATTRIBUTE_VOLUME)==0; }
static int same(const u8 *r,const u8 w[11]) { u32 i;if(!active(r))return 0;for(i=0;i<11;++i)if(uppercase(r[i])!=w[i])return 0;return 1; }
static void fill_entry(const u8 *r,fs_entry *e){format_filename(r,e->name);e->size=read_u32(&r[28]);e->first_cluster=read_u16(&r[26]);e->attributes=r[11];}
static int read_dir_entry(u16 dir,u32 wanted_index,u8 *out)
{
    u16 c=dir;u32 seen=0,lim=cluster_count,s,e;
    while(1){u32 count=dir?sectors_per_cluster:root_directory_sectors, lba=dir?data_lba+(u32)(c-2)*sectors_per_cluster:root_directory_lba;
        for(s=0;s<count;++s){if(dir&&!disk_read_sector(lba+s,sector_buffer))return 0;for(e=0;e<16;++e){u8 *r=dir?&sector_buffer[e*32]:&root_directory[s*512+e*32];if(r[0]==0)return 0;if(active(r)){if(seen++==wanted_index){for(u32 j=0;j<32;++j)out[j]=r[j];return 1;}}}}
        if(!dir)return 0;{u16 next;if(!next_cluster(c,&next)||next>=FAT12_EOC||--lim==0)return 0;c=next;}
    }
}
static int find_entry(u16 dir,const u8 w[11],u8 *out,u16 *where,u32 *lba,u32 *off)
{
    u16 c=dir;u32 lim=cluster_count,s,e;
    while(1){u32 count=dir?sectors_per_cluster:root_directory_sectors,base=dir?data_lba+(u32)(c-2)*sectors_per_cluster:root_directory_lba;
        for(s=0;s<count;++s){if(dir&&!disk_read_sector(base+s,sector_buffer))return 0;for(e=0;e<16;++e){u8 *r=dir?&sector_buffer[e*32]:&root_directory[s*512+e*32];if(r[0]==0)return 0;if(same(r,w)){if(out)for(u32 j=0;j<32;++j)out[j]=r[j];if(where)*where=c;if(lba)*lba=base+s;if(off)*off=e*32;return 1;}}}
        if(!dir)return 0;{u16 next;if(!next_cluster(c,&next)||next>=FAT12_EOC||--lim==0)return 0;c=next;}
    }
}
static int resolve(const char *path,u16 *parent,u8 leaf[11])
{
    char part[13];const char *p=path,*q;u8 w[11];u8 raw[32];fs_entry e;u32 n,i;
    *parent=0;while(*p=='/'||*p=='\\')++p;if(!*p)return 0;
    for(;;){q=p;while(*q&&*q!='/'&&*q!='\\')++q;n=(u32)(q-p);if(n==0||n>=sizeof(part))return 0;for(i=0;i<n;++i)part[i]=p[i];part[n]=0;while(*q=='/'||*q=='\\')++q;
        if(!*q)return make_fat_filename(part,leaf);
        if(!make_fat_filename(part,w)||!find_entry(*parent,w,raw,0,0,0))return 0;fill_entry(raw,&e);if(!(e.attributes&FAT_ATTRIBUTE_DIRECTORY))return 0;*parent=e.first_cluster;p=q;
    }
}
static int write_entry(u16 dir,u32 lba,u32 off,const u8 *raw){u32 i;if(!disk_read_sector(lba,sector_buffer))return 0;for(i=0;i<32;++i)sector_buffer[off+i]=raw[i];if(!disk_write_sector(lba,sector_buffer))return 0;if(!disk_read_sector(lba,sector_buffer)||sector_buffer[off]!=raw[0])return 0;if(!dir)for(i=0;i<32;++i)root_directory[(lba-root_directory_lba)*512+off+i]=raw[i];return 1;}
static int free_slot(u16 dir,u32 *lba,u32 *off)
{
    u16 c=dir;u32 lim=cluster_count,s,e;while(1){u32 count=dir?sectors_per_cluster:root_directory_sectors,base=dir?data_lba+(u32)(c-2)*sectors_per_cluster:root_directory_lba;if(dir){*lba=base;*off=64;return 1;}for(s=0;s<count;++s){for(e=0;e<16;++e){u8 *r=&root_directory[s*512+e*32];if(r[0]==0||r[0]==0xe5){*lba=base+s;*off=e*32;return 1;}}}if(!dir){return 0;}{u16 next;if(!next_cluster(c,&next)||next>=FAT12_EOC||--lim==0){return 0;}c=next;}}
}
static int release(u16 c){u32 n=0;while(c){u16 x;if(c<2||c>=cluster_count+2||++n>cluster_count||!next_cluster(c,&x))return 0;fat_set(c,0);c=x>=FAT12_EOC?0:x;}return 1;}
static int alloc(u32 count,u16 *first){u32 c,n=0;u16 prev=0;*first=0;for(c=2;c<cluster_count+2&&n<count;++c)if(fat_get((u16)c)==0&&(((data_lba+(c-2)*sectors_per_cluster)/18)%2)==0){fat_set((u16)c,FAT12_EOC);if(prev)fat_set(prev,(u16)c);else *first=(u16)c;prev=(u16)c;++n;}return n==count;}
static int file_clusters(u16 c){u32 n=0,lim=0;while(c){u16 x;if(c<2||c>=cluster_count+2||++lim>cluster_count||!next_cluster(c,&x))return -1;++n;c=x>=FAT12_EOC?0:x;}return (int)n;}
static int locate(const char *path,u16 *dir,u8 w[11],u8 raw[32],u32 *lba,u32 *off){if(!resolve(path,dir,w))return 0;return find_entry(*dir,w,raw,0,lba,off);}
int filesystem_get_entry_in(const char *path,u32 index,fs_entry *e){u16 d;u8 w[11],r[32];if(path&&*path&&!(path[0]=='\\'&&path[1]==0)){if(!resolve(path,&d,w))return 0;if(!find_entry(d,w,r,0,0,0))return 0;fill_entry(r,e);if(!(e->attributes&FAT_ATTRIBUTE_DIRECTORY))return 0;d=e->first_cluster;}else d=0;if(!read_dir_entry(d,index,r))return 0;fill_entry(r,e);return 1;}
int filesystem_get_entry(u32 i,fs_entry *e){return filesystem_get_entry_in("\\",i,e);}
int filesystem_find(const char *path,fs_entry *e){u16 d;u8 w[11],r[32];u32 l,o;if(!filesystem_mounted||!locate(path,&d,w,r,&l,&o))return 0;fill_entry(r,e);return 1;}
int filesystem_read_file(const fs_entry *e,void(*write)(const char*,u32)){u32 left=e?e->size:0,steps=0;u16 c=e?e->first_cluster:0;if(!filesystem_mounted||!e||!write||(e->attributes&16))return 0;while(left){u32 s;if(c<2||c>=cluster_count+2||++steps>cluster_count)return 0;for(s=0;s<sectors_per_cluster&&left;++s){u32 bytes=left<512?left:512;if(!disk_read_sector(data_lba+(u32)(c-2)*sectors_per_cluster+s,sector_buffer))return 0;write((const char*)sector_buffer,bytes);left-=bytes;}if(left){u16 next;if(!next_cluster(c,&next))return 0;c=next;}}return 1;}
int filesystem_delete_file(const char *path){u16 d;u8 w[11],r[32];u32 l,o;if(!locate(path,&d,w,r,&l,&o)||(r[11]&16))return 0;if(read_u16(&r[26])&&!release(read_u16(&r[26])))return 0;r[0]=0xe5;return write_entry(d,l,o,r)&&write_fat();}
int filesystem_rename_file(const char *oldp,const char *newp){u16 d,nd;u8 w[11],nw[11],r[32],nr[32];u32 l,o;if(!locate(oldp,&d,w,r,&l,&o)||!resolve(newp,&nd,nw)||find_entry(nd,nw,nr,0,0,0)||d!=nd)return 0;for(u32 i=0;i<11;++i)r[i]=nw[i];return write_entry(d,l,o,r);}
int filesystem_write_file(const char *path,const char *data,u32 size){u16 d,first=0,old=0,next;u8 w[11],r[32];u32 l,o,oldn=0,need,c,off=0,s;if(locate(path,&d,w,r,&l,&o)){fs_entry e;fill_entry(r,&e);if(e.attributes&16)return 0;old=e.first_cluster;oldn=file_clusters(old);if(oldn<0)return 0;}else{if(!resolve(path,&d,w)||find_entry(d,w,r,0,0,0)||!free_slot(d,&l,&o))return 0;}need=size?(size+sectors_per_cluster*512-1)/(sectors_per_cluster*512):0;if(need&& !alloc(need,&first))return 0;if(old&& !release(old))return 0;for(c=first,off=0;off<size;off+=sectors_per_cluster*512){u32 bytes=size-off<sectors_per_cluster*512?size-off:sectors_per_cluster*512;for(s=0;s<sectors_per_cluster;++s){u32 at=s*512,copy=bytes>at?(bytes-at<512?bytes-at:512):0;for(u32 i=0;i<512;++i)sector_buffer[i]=i<copy?(u8)data[off+at+i]:0;if(!disk_write_sector(data_lba+(u32)(c-2)*sectors_per_cluster+s,sector_buffer))return 0;}if(off+bytes<size&&!next_cluster(c,&next))return 0;c=next;}for(u32 i=0;i<11;++i)r[i]=w[i];r[11]=FAT_ATTRIBUTE_ARCHIVE;r[26]=(u8)first;r[27]=(u8)(first>>8);r[28]=(u8)size;r[29]=(u8)(size>>8);r[30]=(u8)(size>>16);r[31]=(u8)(size>>24);return write_entry(d,l,o,r)&&write_fat();}
int filesystem_make_directory(const char *path)
{
    u16 d, c;
    u8 w[11], r[32];
    u32 l, o, i;

    if (!resolve(path, &d, w)) return 0;
    if (find_entry(d, w, r, 0, 0, 0)) return 0;
    if (!free_slot(d, &l, &o)) return 0;
    if (!alloc(1, &c)) return 0;
    for (i = 0; i < 512; ++i) sector_buffer[i] = 0;
    sector_buffer[0] = '.';
    for (i = 1; i < 11; ++i) sector_buffer[i] = ' ';
    sector_buffer[11] = 16;
    sector_buffer[26] = (u8)c; sector_buffer[27] = (u8)(c >> 8);
    sector_buffer[32] = '.'; sector_buffer[33] = '.';
    for (i = 34; i < 43; ++i) sector_buffer[i] = ' ';
    sector_buffer[43] = 16;
    sector_buffer[58] = (u8)d; sector_buffer[59] = (u8)(d >> 8);
    if (!disk_write_sector(data_lba + (u32)(c - 2) * sectors_per_cluster, sector_buffer) &&
        !disk_write_sector(data_lba + (u32)(c - 2) * sectors_per_cluster, sector_buffer)) {
        fat_set(c, 0); return 0;
    }
    for (i = 0; i < 11; ++i) r[i] = w[i];
    r[11] = 16; r[26] = (u8)c; r[27] = (u8)(c >> 8);
    r[28] = r[29] = r[30] = r[31] = 0;
    if (!write_entry(d, l, o, r)) { fat_set(c, 0); return 0; }
    if (!write_fat()) return 0;
    return 1;
}int filesystem_remove_directory(const char *path){u16 d,c;u8 w[11],r[32],x[32];u32 l,o;if(!locate(path,&d,w,r,&l,&o)||(!(r[11]&16))||(read_u16(&r[26])==0))return 0;c=read_u16(&r[26]);if(read_dir_entry(c,0,x)&&x[0]!='.' )return 0;/* reject any active entry other than . and .. */for(u32 i=2;read_dir_entry(c,i,x);++i)return 0;if(!release(c))return 0;r[0]=0xe5;return write_entry(d,l,o,r)&&write_fat();}