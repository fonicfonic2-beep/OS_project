/*
 * fat32.c — FAT32 read/write driver over a memory-mapped ramdisk.
 *
 * The disk image (initrd module or other memory region) is accessed
 * sector-by-sector through ramdisk_read_sector() / ramdisk_write_sector()
 * (defined in vfs.c), which boil down to memcpy() at disk_base + offset.
 *
 * Features:
 *   - BPB / FSInfo parsing (first FAT only for allocation scan)
 *   - cluster-chain traversal via 32-bit FAT entries (handles sector
 *     boundaries), cluster allocation and freeing
 *   - directory scanning with minimal Long File Name (LFN) assembly so
 *     that `ls` shows long names; lookup matches both long and 8.3 names
 *     (case-insensitive)
 *   - file read/write (extend file by allocating clusters, sync size and
 *     first-cluster back into the parent directory entry)
 *   - mkdir / create-file / unlink (mark 0xE5 + free clusters) / rename
 */
#include "fat32.h"
#include "string.h"
#include "heap.h"
#include "common.h"
#include "ctype.h"

/* errno values (no errno.h in this kernel) */
#define ENOENT     2
#define EIO        5
#define ENOMEM    12
#define EEXIST    17
#define ENOTDIR   20
#define EISDIR    21
#define EINVAL    22
#define ENOSPC    28
#define ENOTEMPTY 39

/* Shared ramdisk sector access (defined in vfs.c). */
extern void ramdisk_read_sector(void* disk_base, uint32_t lba,
                                uint32_t bytes_per_sector, void* buf);
extern void ramdisk_write_sector(void* disk_base, uint32_t lba,
                                 uint32_t bytes_per_sector, const void* buf);

/* ------------------------------------------------------------------ */
/* Little-endian accessors (avoid unaligned / endianness pitfalls)     */
/* ------------------------------------------------------------------ */
static uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static int stricmp(const char* a, const char* b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* ------------------------------------------------------------------ */
/* FAT32 filesystem context and per-inode private data                 */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t*  disk;
    size_t    size;
    uint16_t  bps;             /* bytes per sector                         */
    uint8_t   spc;             /* sectors per cluster                      */
    uint16_t  reserved;        /* reserved sectors (start of first FAT)    */
    uint8_t   num_fats;
    uint32_t  fat_size;        /* sectors per FAT                          */
    uint32_t  data_start;      /* first data sector                        */
    uint32_t  root_cluster;
    uint32_t  total_clusters;
    uint32_t  cluster_size;    /* bytes per cluster = bps * spc            */
    uint32_t  alloc_hint;      /* next cluster to try for allocation       */
    uint32_t  ino_counter;     /* monotonic inode number generator         */
    uint8_t*  secbuf;          /* one-sector scratch buffer                */
} fat_fs_t;

typedef struct {
    fat_fs_t* fs;
    uint32_t  first_cluster;
    uint32_t  entry_sector;    /* sector of the 32-byte dir entry (0xFFFFFFFF if none) */
    uint32_t  entry_offset;    /* byte offset within that sector           */
} fat_inode_info_t;

typedef struct {
    uint8_t  attr;
    uint32_t first_cluster;
    uint32_t size;
    uint32_t entry_sector;
    uint32_t entry_offset;
} fat_de_t;

#define FAT_NO_ENTRY  0xFFFFFFFFu
#define FAT_EOC       0x0FFFFFFFu
#define FAT_BAD       0x0FFFFFF7u

static inline uint32_t cluster_to_sector(fat_fs_t* fs, uint32_t cluster)
{
    return fs->data_start + (cluster - 2u) * fs->spc;
}

/* ------------------------------------------------------------------ */
/* Sector / multi-byte ramdisk helpers                                 */
/* ------------------------------------------------------------------ */
static void read_sector(fat_fs_t* fs, uint32_t lba, void* buf)
{
    ramdisk_read_sector(fs->disk, lba, fs->bps, buf);
}
static void write_sector(fat_fs_t* fs, uint32_t lba, const void* buf)
{
    ramdisk_write_sector(fs->disk, lba, fs->bps, buf);
}

/* Read n bytes starting at (lba, boff) within a cluster (sectors contiguous). */
static void read_n(fat_fs_t* fs, uint32_t lba, uint32_t boff,
                   uint8_t* dst, uint32_t n)
{
    while (n > 0) {
        uint32_t chunk = fs->bps - boff;
        if (chunk > n) chunk = n;
        read_sector(fs, lba, fs->secbuf);
        memcpy(dst, fs->secbuf + boff, chunk);
        dst += chunk;
        n -= chunk;
        boff = 0;
        lba++;
    }
}

/* Write n bytes starting at (lba, boff) within a cluster. */
static void write_n(fat_fs_t* fs, uint32_t lba, uint32_t boff,
                    const uint8_t* src, uint32_t n)
{
    while (n > 0) {
        uint32_t chunk = fs->bps - boff;
        if (chunk > n) chunk = n;
        read_sector(fs, lba, fs->secbuf);
        memcpy(fs->secbuf + boff, src, chunk);
        write_sector(fs, lba, fs->secbuf);
        src += chunk;
        n -= chunk;
        boff = 0;
        lba++;
    }
}

/* Read a 32-byte directory entry that may cross a sector boundary. */
static void read_dir_entry(fat_fs_t* fs, uint32_t lba, uint32_t boff, uint8_t* out)
{
    read_n(fs, lba, boff, out, 32);
}

/* Write a 32-byte directory entry that may cross a sector boundary. */
static void write_dir_entry(fat_fs_t* fs, uint32_t lba, uint32_t boff, const uint8_t* in)
{
    write_n(fs, lba, boff, in, 32);
}

static void zero_cluster(fat_fs_t* fs, uint32_t cluster)
{
    uint32_t sec = cluster_to_sector(fs, cluster);
    uint32_t i;
    memset(fs->secbuf, 0, fs->bps);
    for (i = 0; i < fs->spc; i++)
        write_sector(fs, sec + i, fs->secbuf);
}

/* ------------------------------------------------------------------ */
/* FAT entry access                                                    */
/* ------------------------------------------------------------------ */
static uint32_t fat_read_entry(fat_fs_t* fs, uint32_t cluster)
{
    uint64_t off = (uint64_t)cluster * 4;
    uint32_t sec = fs->reserved + (uint32_t)(off / fs->bps);
    uint32_t boff = (uint32_t)(off % fs->bps);
    uint8_t tmp[4];
    read_n(fs, sec, boff, tmp, 4);
    return rd32(tmp) & 0x0FFFFFFFu;
}

static void fat_write_entry(fat_fs_t* fs, uint32_t cluster, uint32_t value)
{
    uint64_t off = (uint64_t)cluster * 4;
    uint32_t boff = (uint32_t)(off % fs->bps);
    uint8_t tmp[4];
    uint32_t f;
    value &= 0x0FFFFFFFu;
    wr32(tmp, value);
    /* Update every copy of the FAT. */
    for (f = 0; f < fs->num_fats; f++) {
        uint32_t fat_base = fs->reserved + f * fs->fat_size;
        uint32_t sec = fat_base + (uint32_t)(off / fs->bps);
        write_n(fs, sec, boff, tmp, 4);
    }
}

static uint32_t fat_alloc_cluster(fat_fs_t* fs)
{
    uint32_t i;
    uint32_t start = fs->alloc_hint;
    if (start < 2) start = 2;
    for (i = 0; i < fs->total_clusters; i++) {
        uint32_t c = 2 + ((start - 2 + i) % fs->total_clusters);
        if (fat_read_entry(fs, c) == 0) {
            fat_write_entry(fs, c, FAT_EOC);
            fs->alloc_hint = c + 1;
            return c;
        }
    }
    return 0;
}

static void fat_free_chain(fat_fs_t* fs, uint32_t cluster)
{
    while (cluster >= 2 && cluster < FAT_BAD) {
        uint32_t next = fat_read_entry(fs, cluster);
        fat_write_entry(fs, cluster, 0);
        cluster = next;
    }
}

/* Walk the chain `idx` cluster steps from `first`; return that cluster (0 if short). */
static uint32_t fat_cluster_at(fat_fs_t* fs, uint32_t first, uint32_t byte_offset)
{
    uint32_t idx = byte_offset / fs->cluster_size;
    uint32_t c = first;
    uint32_t i;
    for (i = 0; i < idx; i++) {
        if (c < 2 || c >= FAT_BAD) return 0;
        c = fat_read_entry(fs, c);
    }
    if (c < 2 || c >= FAT_BAD) return 0;
    return c;
}

/* Ensure the cluster chain holds at least need_bytes; extend if necessary. */
static int fat_ensure_chain(fat_fs_t* fs, uint32_t* first, uint32_t need_bytes)
{
    uint32_t need = (need_bytes + fs->cluster_size - 1) / fs->cluster_size;
    uint32_t count = 0, last = 0, c = *first;
    if (need == 0) return 0;
    while (c >= 2 && c < FAT_BAD) {
        count++;
        last = c;
        c = fat_read_entry(fs, c);
    }
    while (count < need) {
        uint32_t nc = fat_alloc_cluster(fs);
        if (!nc) return -ENOSPC;
        zero_cluster(fs, nc);
        if (last == 0) *first = nc;
        else fat_write_entry(fs, last, nc);
        last = nc;
        count++;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Name helpers                                                        */
/* ------------------------------------------------------------------ */
static void format_short_name(const uint8_t* raw, char* out)
{
    int i, k = 0;
    for (i = 0; i < 8 && raw[i] != ' '; i++)
        out[k++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[k++] = '.';
        for (i = 8; i < 11 && raw[i] != ' '; i++)
            out[k++] = (char)raw[i];
    }
    out[k] = '\0';
}

static void make_short_name(const char* name, char out[11])
{
    const char* dot;
    size_t namelen, i;
    memset(out, ' ', 11);
    dot = strrchr(name, '.');
    if (dot == name) dot = NULL; /* dotfile: treat whole name as base */
    namelen = dot ? (size_t)(dot - name) : strlen(name);
    if (namelen > 8) namelen = 8;
    for (i = 0; i < namelen; i++)
        out[i] = (char)toupper((unsigned char)name[i]);
    if (dot) {
        const char* ext = dot + 1;
        size_t elen = strlen(ext);
        if (elen > 3) elen = 3;
        for (i = 0; i < elen; i++)
            out[8 + i] = (char)toupper((unsigned char)ext[i]);
    }
}

static int utf16_to_utf8(const uint16_t* src, char* dst, int dstsz)
{
    int o = 0, i;
    for (i = 0; src[i] && src[i] != 0xFFFF && o < dstsz - 1; i++) {
        uint32_t c = src[i];
        if (c < 0x80) {
            dst[o++] = (char)c;
        } else if (c < 0x800) {
            if (o + 2 >= dstsz) break;
            dst[o++] = (char)(0xC0 | (c >> 6));
            dst[o++] = (char)(0x80 | (c & 0x3F));
        } else {
            if (o + 3 >= dstsz) break;
            dst[o++] = (char)(0xE0 | (c >> 12));
            dst[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    dst[o] = '\0';
    return o;
}

/* ------------------------------------------------------------------ */
/* Directory scanning                                                  */
/* ------------------------------------------------------------------ */
typedef int (*fat_scan_cb)(void* arg, const uint8_t* raw, const char* name,
                           uint32_t entry_sector, uint32_t entry_offset);

/*
 * Scan every valid (non-deleted, non-LFN, non-volume-label) directory
 * entry. LFN entries are accumulated and assembled into a long name for
 * the following short entry. cb returns 0 to continue, non-zero to stop
 * (that value is propagated). Returns 0 at end of directory.
 */
static int fat_dir_scan(fat_fs_t* fs, uint32_t dir_cluster,
                        fat_scan_cb cb, void* arg)
{
    uint16_t lfn[256];
    int lfn_any = 0;
    uint32_t cluster = dir_cluster;
    uint32_t entries_per_cluster = fs->cluster_size / 32;
    char namebuf[VFS_NAME_MAX];
    static const uint8_t lfn_pos[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};

    memset(lfn, 0, sizeof(lfn));
    while (cluster >= 2 && cluster < FAT_BAD) {
        uint32_t e;
        for (e = 0; e < entries_per_cluster; e++) {
            uint8_t raw[32];
            uint32_t off_in_cl = e * 32;
            uint32_t sec = cluster_to_sector(fs, cluster) + off_in_cl / fs->bps;
            uint32_t boff = off_in_cl % fs->bps;
            uint8_t first, attr;
            read_dir_entry(fs, sec, boff, raw);
            first = raw[0];
            if (first == 0x00)
                return 0; /* end of directory */
            if (first == 0xE5) {
                lfn_any = 0;
                memset(lfn, 0, sizeof(lfn));
                continue;
            }
            attr = raw[11];
            if (attr == 0x0F) {
                uint8_t ord = first & 0x1F;
                int k;
                for (k = 0; k < 13; k++) {
                    uint16_t ch = (uint16_t)raw[lfn_pos[k]] |
                                  ((uint16_t)raw[lfn_pos[k] + 1] << 8);
                    uint32_t p;
                    if (ch == 0 || ch == 0xFFFF)
                        continue;
                    p = (uint32_t)(ord - 1) * 13 + (uint32_t)k;
                    if (p < 256)
                        lfn[p] = ch;
                }
                lfn_any = 1;
                continue;
            }
            if (attr & 0x08) { /* volume label */
                lfn_any = 0;
                memset(lfn, 0, sizeof(lfn));
                continue;
            }
            if (lfn_any)
                utf16_to_utf8(lfn, namebuf, sizeof(namebuf));
            else
                format_short_name(raw, namebuf);
            {
                int r = cb(arg, raw, namebuf, sec, boff);
                if (r != 0)
                    return r;
            }
            lfn_any = 0;
            memset(lfn, 0, sizeof(lfn));
        }
        cluster = fat_read_entry(fs, cluster);
    }
    return 0;
}

struct find_arg {
    const char* name;
    fat_de_t*   de;
    int         found;
};

static int find_cb(void* arg, const uint8_t* raw, const char* name,
                   uint32_t sec, uint32_t boff)
{
    struct find_arg* fa = (struct find_arg*)arg;
    if (stricmp(name, fa->name) != 0)
        return 0;
    fa->de->attr = raw[11];
    fa->de->first_cluster = ((uint32_t)rd16(raw + 20) << 16) | rd16(raw + 26);
    fa->de->size = rd32(raw + 28);
    fa->de->entry_sector = sec;
    fa->de->entry_offset = boff;
    fa->found = 1;
    return 1;
}

static int fat_dir_find(fat_fs_t* fs, uint32_t dir_cluster,
                        const char* name, fat_de_t* de)
{
    struct find_arg fa;
    fa.name = name;
    fa.de = de;
    fa.found = 0;
    fat_dir_scan(fs, dir_cluster, find_cb, &fa);
    return fa.found ? 0 : -1;
}

struct count_arg {
    unsigned int target;
    unsigned int count;
    fat_de_t*    de;
    char         name[VFS_NAME_MAX];
};

static int count_cb(void* arg, const uint8_t* raw, const char* name,
                    uint32_t sec, uint32_t boff)
{
    struct count_arg* ca = (struct count_arg*)arg;
    if (ca->count == ca->target) {
        ca->de->attr = raw[11];
        ca->de->first_cluster = ((uint32_t)rd16(raw + 20) << 16) | rd16(raw + 26);
        ca->de->size = rd32(raw + 28);
        ca->de->entry_sector = sec;
        ca->de->entry_offset = boff;
        strncpy(ca->name, name, VFS_NAME_MAX - 1);
        ca->name[VFS_NAME_MAX - 1] = '\0';
        return 1;
    }
    ca->count++;
    return 0;
}

static int fat_dir_get(fat_fs_t* fs, uint32_t dir_cluster,
                       unsigned int index, fat_de_t* de, char* name_out)
{
    struct count_arg ca;
    ca.target = index;
    ca.count = 0;
    ca.de = de;
    ca.name[0] = '\0';
    if (fat_dir_scan(fs, dir_cluster, count_cb, &ca) == 1) {
        strcpy(name_out, ca.name);
        return 0;
    }
    return -1;
}

/* Find a free directory slot (0xE5 reused or 0x00 end marker). Extend the
 * directory by one cluster if it is completely full. */
static int fat_dir_find_slot(fat_fs_t* fs, uint32_t dir_cluster,
                             uint32_t* out_sec, uint32_t* out_off)
{
    uint32_t cluster = dir_cluster;
    uint32_t entries_per_cluster = fs->cluster_size / 32;
    while (cluster >= 2 && cluster < FAT_BAD) {
        uint32_t e;
        for (e = 0; e < entries_per_cluster; e++) {
            uint8_t raw[32];
            uint32_t off_in_cl = e * 32;
            uint32_t sec = cluster_to_sector(fs, cluster) + off_in_cl / fs->bps;
            uint32_t boff = off_in_cl % fs->bps;
            read_dir_entry(fs, sec, boff, raw);
            if (raw[0] == 0x00 || raw[0] == 0xE5) {
                *out_sec = sec;
                *out_off = boff;
                return 0;
            }
        }
        cluster = fat_read_entry(fs, cluster);
    }
    /* Directory full: append a cluster and use its first slot. */
    {
        uint32_t last = dir_cluster, c = dir_cluster, nc;
        while (c >= 2 && c < FAT_BAD) {
            last = c;
            c = fat_read_entry(fs, c);
        }
        nc = fat_alloc_cluster(fs);
        if (!nc) return -ENOSPC;
        zero_cluster(fs, nc);
        fat_write_entry(fs, last, nc);
        *out_sec = cluster_to_sector(fs, nc);
        *out_off = 0;
    }
    return 0;
}

/* Mark a directory entry (and any immediately preceding LFN entries) deleted. */
static void fat_mark_deleted(fat_fs_t* fs, uint32_t sec, uint32_t boff)
{
    uint8_t raw[32];
    uint8_t del = 0xE5;
    /* Mark the short entry itself. */
    write_n(fs, sec, boff, &del, 1);
    /* Walk backwards marking LFN entries (attr == 0x0F). */
    for (;;) {
        uint32_t psec = sec, poff = boff;
        if (poff >= 32) {
            poff -= 32;
        } else {
            if (psec == 0) break;
            psec--;
            poff = fs->bps - 32;
        }
        read_dir_entry(fs, psec, poff, raw);
        if (raw[11] != 0x0F)
            break;
        write_n(fs, psec, poff, &del, 1);
        sec = psec;
        boff = poff;
    }
}

static void fat_sync_entry_size(fat_fs_t* fs, fat_inode_info_t* info, uint32_t size)
{
    uint8_t raw[32];
    if (info->entry_sector == FAT_NO_ENTRY)
        return;
    read_dir_entry(fs, info->entry_sector, info->entry_offset, raw);
    wr16(raw + 20, (uint16_t)(info->first_cluster >> 16));
    wr16(raw + 26, (uint16_t)(info->first_cluster & 0xFFFF));
    wr32(raw + 28, size);
    write_dir_entry(fs, info->entry_sector, info->entry_offset, raw);
}

/* ------------------------------------------------------------------ */
/* inode construction                                                  */
/* ------------------------------------------------------------------ */
static struct file_operations fat_fops;

static inode_t* fat_make_inode(fat_fs_t* fs, const fat_de_t* de, uint32_t cluster)
{
    inode_t* ino = (inode_t*)kmalloc(sizeof(inode_t));
    fat_inode_info_t* info;
    if (!ino) return NULL;
    info = (fat_inode_info_t*)kmalloc(sizeof(fat_inode_info_t));
    if (!info) { kfree(ino); return NULL; }
    info->fs = fs;
    info->first_cluster = cluster;
    info->entry_sector = de ? de->entry_sector : FAT_NO_ENTRY;
    info->entry_offset = de ? de->entry_offset : 0;
    ino->ino = ++fs->ino_counter;
    ino->mode = (de && (de->attr & 0x10)) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    ino->size = de ? de->size : 0;
    ino->uid = 0;
    ino->gid = 0;
    ino->nlink = 1;
    ino->fop = &fat_fops;
    ino->fs_private = info;
    ino->mount = NULL;
    return ino;
}

/* ------------------------------------------------------------------ */
/* file_operations implementations                                     */
/* ------------------------------------------------------------------ */
static int fat_open(inode_t* inode, file_t* file)
{
    fat_inode_info_t* info = (fat_inode_info_t*)inode->fs_private;
    if (!info) return -EIO;
    if ((file->flags & O_TRUNC) && S_ISREG(inode->mode)) {
        fat_free_chain(info->fs, info->first_cluster);
        info->first_cluster = 0;
        inode->size = 0;
        fat_sync_entry_size(info->fs, info, 0);
    }
    return 0;
}

static ssize_t fat_read(file_t* file, char* buf, size_t sz)
{
    inode_t* inode = file->inode;
    fat_inode_info_t* info = (fat_inode_info_t*)inode->fs_private;
    fat_fs_t* fs = info->fs;
    uint32_t avail, off;
    if (S_ISDIR(inode->mode)) return -EISDIR;
    if (file->offset >= inode->size) return 0;
    avail = inode->size - (uint32_t)file->offset;
    if (sz > avail) sz = avail;
    if (sz == 0 || info->first_cluster == 0) return 0;
    off = (uint32_t)file->offset;
    while (sz > 0) {
        uint32_t c = fat_cluster_at(fs, info->first_cluster, off);
        uint32_t in_cl = off % fs->cluster_size;
        uint32_t chunk = fs->cluster_size - in_cl;
        uint32_t sec, boff;
        if (chunk > sz) chunk = (uint32_t)sz;
        if (c == 0) break;
        sec = cluster_to_sector(fs, c) + in_cl / fs->bps;
        boff = in_cl % fs->bps;
        read_n(fs, sec, boff, (uint8_t*)buf, chunk);
        buf += chunk;
        off += chunk;
        sz -= chunk;
    }
    return (ssize_t)(off - (uint32_t)file->offset);
}

static ssize_t fat_write(file_t* file, const char* buf, size_t sz)
{
    inode_t* inode = file->inode;
    fat_inode_info_t* info = (fat_inode_info_t*)inode->fs_private;
    fat_fs_t* fs = info->fs;
    uint64_t end;
    uint32_t off;
    size_t orig = sz;
    int r;

    if (S_ISDIR(inode->mode)) return -EISDIR;
    if (file->flags & O_APPEND)
        file->offset = inode->size;

    end = file->offset + sz;
    if (end > inode->size) {
        r = fat_ensure_chain(fs, &info->first_cluster, (uint32_t)end);
        if (r < 0) return r;
        inode->size = (uint32_t)end;
        fat_sync_entry_size(fs, info, inode->size);
    }

    off = (uint32_t)file->offset;
    while (sz > 0) {
        uint32_t c = fat_cluster_at(fs, info->first_cluster, off);
        uint32_t in_cl = off % fs->cluster_size;
        uint32_t chunk = fs->cluster_size - in_cl;
        uint32_t sec, boff;
        if (chunk > sz) chunk = (uint32_t)sz;
        if (c == 0) break; /* should not happen after ensure */
        sec = cluster_to_sector(fs, c) + in_cl / fs->bps;
        boff = in_cl % fs->bps;
        write_n(fs, sec, boff, (const uint8_t*)buf, chunk);
        buf += chunk;
        off += chunk;
        sz -= chunk;
    }
    return (ssize_t)(orig - sz);
}

static int fat_close(file_t* file)
{
    (void)file;
    return 0;
}

static int fat_readdir(file_t* file, struct dirent* de, unsigned int index)
{
    inode_t* inode = file->inode;
    fat_inode_info_t* info = (fat_inode_info_t*)inode->fs_private;
    fat_fs_t* fs = info->fs;
    fat_de_t fde;
    char name[VFS_NAME_MAX];
    uint32_t dir_cluster = info->first_cluster;
    if (dir_cluster == 0) dir_cluster = fs->root_cluster;
    if (fat_dir_get(fs, dir_cluster, index, &fde, name) < 0)
        return 0; /* end of directory */
    strncpy(de->name, name, VFS_NAME_MAX - 1);
    de->name[VFS_NAME_MAX - 1] = '\0';
    de->ino = fde.first_cluster ? fde.first_cluster : (uint32_t)(index + 2);
    de->type = (fde.attr & 0x10) ? S_IFDIR : S_IFREG;
    return 1;
}

static inode_t* fat_lookup(inode_t* dir, const char* name)
{
    fat_inode_info_t* dinfo = (fat_inode_info_t*)dir->fs_private;
    fat_fs_t* fs = dinfo->fs;
    fat_de_t de;
    uint32_t cluster;
    uint32_t dir_cluster = dinfo->first_cluster ? dinfo->first_cluster : fs->root_cluster;

    if (!name || !*name) return NULL;
    if (fat_dir_find(fs, dir_cluster, name, &de) < 0)
        return NULL;

    cluster = de.first_cluster;
    if (name[0] == '.' && name[1] == '\0') {
        cluster = dir_cluster;
    } else if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        if (cluster == 0)
            cluster = fs->root_cluster;
    }
    return fat_make_inode(fs, &de, cluster);
}

static int fat_mkdir(inode_t* dir, const char* name, uint32_t mode)
{
    fat_inode_info_t* dinfo = (fat_inode_info_t*)dir->fs_private;
    fat_fs_t* fs = dinfo->fs;
    uint32_t dir_cluster = dinfo->first_cluster ? dinfo->first_cluster : fs->root_cluster;
    fat_de_t de;
    uint32_t sec, boff;
    uint8_t entry[32];
    uint32_t new_cluster = 0;
    char short_name[11];

    if (!name || !*name) return -EINVAL;
    if (fat_dir_find(fs, dir_cluster, name, &de) == 0)
        return -EEXIST;
    if (fat_dir_find_slot(fs, dir_cluster, &sec, &boff) < 0)
        return -ENOSPC;

    if (S_ISDIR(mode)) {
        new_cluster = fat_alloc_cluster(fs);
        if (!new_cluster) return -ENOSPC;
        zero_cluster(fs, new_cluster);
        /* Write "." and ".." entries in the new directory. */
        {
            uint8_t dot[32], dotdot[32];
            uint32_t parent_cluster = dir_cluster;
            if (parent_cluster == fs->root_cluster) parent_cluster = 0;
            memset(dot, 0, 32);
            memset(dot, ' ', 11);
            dot[0] = '.';
            dot[11] = 0x10;
            wr16(dot + 26, (uint16_t)(new_cluster & 0xFFFF));
            wr16(dot + 20, (uint16_t)(new_cluster >> 16));
            memset(dotdot, 0, 32);
            memset(dotdot, ' ', 11);
            dotdot[0] = '.'; dotdot[1] = '.';
            dotdot[11] = 0x10;
            wr16(dotdot + 26, (uint16_t)(parent_cluster & 0xFFFF));
            wr16(dotdot + 20, (uint16_t)(parent_cluster >> 16));
            write_dir_entry(fs, cluster_to_sector(fs, new_cluster), 0, dot);
            write_dir_entry(fs, cluster_to_sector(fs, new_cluster), 32, dotdot);
        }
    }

    make_short_name(name, short_name);
    memset(entry, 0, 32);
    memcpy(entry, short_name, 11);
    entry[11] = S_ISDIR(mode) ? 0x10 : 0x20; /* attr: dir / archive */
    if (new_cluster) {
        wr16(entry + 20, (uint16_t)(new_cluster >> 16));
        wr16(entry + 26, (uint16_t)(new_cluster & 0xFFFF));
    }
    wr32(entry + 28, 0); /* size */
    write_dir_entry(fs, sec, boff, entry);
    return 0;
}

struct empty_arg { int not_empty; };

static int empty_cb(void* arg, const uint8_t* raw, const char* name,
                    uint32_t sec, uint32_t boff)
{
    struct empty_arg* ea = (struct empty_arg*)arg;
    (void)raw; (void)sec; (void)boff;
    if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
        ea->not_empty = 1;
        return 1;
    }
    return 0;
}

static int fat_unlink(inode_t* dir, const char* name)
{
    fat_inode_info_t* dinfo = (fat_inode_info_t*)dir->fs_private;
    fat_fs_t* fs = dinfo->fs;
    uint32_t dir_cluster = dinfo->first_cluster ? dinfo->first_cluster : fs->root_cluster;
    fat_de_t de;

    if (!name || !*name) return -EINVAL;
    if (fat_dir_find(fs, dir_cluster, name, &de) < 0)
        return -ENOENT;

    if (de.attr & 0x10) {
        struct empty_arg ea;
        ea.not_empty = 0;
        fat_dir_scan(fs, de.first_cluster, empty_cb, &ea);
        if (ea.not_empty) return -ENOTEMPTY;
    }
    fat_free_chain(fs, de.first_cluster);
    fat_mark_deleted(fs, de.entry_sector, de.entry_offset);
    return 0;
}

static int fat_rename(inode_t* old_dir, const char* old_name,
                      inode_t* new_dir, const char* new_name)
{
    fat_inode_info_t* oinfo = (fat_inode_info_t*)old_dir->fs_private;
    fat_inode_info_t* ninfo = (fat_inode_info_t*)new_dir->fs_private;
    fat_fs_t* fs = oinfo->fs;
    uint32_t old_dirc = oinfo->first_cluster ? oinfo->first_cluster : fs->root_cluster;
    uint32_t new_dirc = ninfo->first_cluster ? ninfo->first_cluster : fs->root_cluster;
    fat_de_t old_de, tmp;
    uint8_t entry[32];
    uint32_t sec, boff;
    char short_name[11];

    if (!old_name || !*old_name || !new_name || !*new_name)
        return -EINVAL;
    if (fat_dir_find(fs, old_dirc, old_name, &old_de) < 0)
        return -ENOENT;
    if (fat_dir_find(fs, new_dirc, new_name, &tmp) == 0)
        return -EEXIST;

    read_dir_entry(fs, old_de.entry_sector, old_de.entry_offset, entry);
    make_short_name(new_name, short_name);
    memcpy(entry, short_name, 11);

    /* Find a free slot in the target directory. */
    if (fat_dir_find_slot(fs, new_dirc, &sec, &boff) < 0)
        return -ENOSPC;

    if (sec == old_de.entry_sector && boff == old_de.entry_offset) {
        /* Same slot: just rewrite the name in place. */
        write_dir_entry(fs, sec, boff, entry);
        return 0;
    }

    write_dir_entry(fs, sec, boff, entry);
    fat_mark_deleted(fs, old_de.entry_sector, old_de.entry_offset);
    return 0;
}

static struct file_operations fat_fops = {
    .open    = fat_open,
    .read    = fat_read,
    .write   = fat_write,
    .close   = fat_close,
    .readdir = fat_readdir,
    .lookup  = fat_lookup,
    .mkdir   = fat_mkdir,
    .unlink  = fat_unlink,
    .rename  = fat_rename,
};

/* ------------------------------------------------------------------ */
/* Mount                                                               */
/* ------------------------------------------------------------------ */
inode_t* fat32_mount(void* disk_base, size_t size)
{
    uint8_t boot[512];
    fat_fs_t* fs;
    inode_t* root;
    fat_inode_info_t* root_info;
    uint16_t bps;
    uint32_t total16, total32, total;

    if (!disk_base)
        return NULL;
    memcpy(boot, disk_base, 512);
    if (boot[510] != 0x55 || boot[511] != 0xAA)
        return NULL;

    bps = rd16(boot + 11);
    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096)
        return NULL;
    if (boot[13] == 0 || boot[16] == 0)
        return NULL;

    fs = (fat_fs_t*)kcalloc(1, sizeof(fat_fs_t));
    if (!fs) return NULL;
    fs->secbuf = (uint8_t*)kmalloc(bps);
    if (!fs->secbuf) { kfree(fs); return NULL; }

    fs->disk = (uint8_t*)disk_base;
    fs->size = size;
    fs->bps = bps;
    fs->spc = boot[13];
    fs->reserved = rd16(boot + 14);
    fs->num_fats = boot[16];
    fs->fat_size = rd32(boot + 36);
    fs->root_cluster = rd32(boot + 44);
    total16 = rd16(boot + 19);
    total32 = rd32(boot + 32);
    total = total16 ? total16 : total32;
    if (fs->fat_size == 0 || fs->root_cluster < 2 || total == 0 ||
        fs->reserved == 0) {
        kfree(fs->secbuf);
        kfree(fs);
        return NULL;
    }
    fs->data_start = fs->reserved + (uint32_t)fs->num_fats * fs->fat_size;
    fs->cluster_size = (uint32_t)fs->bps * fs->spc;
    fs->total_clusters = (total - fs->data_start) / fs->spc;
    if (fs->total_clusters == 0) {
        kfree(fs->secbuf);
        kfree(fs);
        return NULL;
    }
    fs->alloc_hint = 2;
    fs->ino_counter = 0;

    root = (inode_t*)kmalloc(sizeof(inode_t));
    root_info = (fat_inode_info_t*)kmalloc(sizeof(fat_inode_info_t));
    if (!root || !root_info) {
        kfree(root); kfree(root_info); kfree(fs->secbuf); kfree(fs);
        return NULL;
    }
    root_info->fs = fs;
    root_info->first_cluster = fs->root_cluster;
    root_info->entry_sector = FAT_NO_ENTRY;
    root_info->entry_offset = 0;

    root->ino = ++fs->ino_counter;
    root->mode = S_IFDIR | 0755;
    root->size = 0;
    root->uid = 0;
    root->gid = 0;
    root->nlink = 1;
    root->fop = &fat_fops;
    root->fs_private = root_info;
    root->mount = NULL;
    return root;
}
