/*
 * ext2.c — ext2 (second extended filesystem) read-only driver over a
 * memory-mapped disk image.
 *
 * Supports lookup, read and readdir (direct + single + double indirect
 * blocks). All mutating operations (write/mkdir/unlink/rename) return
 * -EROFS. If the image is not a valid ext2 filesystem, ext2_mount returns
 * NULL and the kernel falls back to FAT32 as the root.
 *
 * Block access is a direct memcpy from the mapped image (the image is a
 * ramdisk); the shared ramdisk_read_sector() helper is used by FAT32.
 */
#include "ext2.h"
#include "string.h"
#include "heap.h"
#include "common.h"

/* errno values (no errno.h in this kernel) */
#define ENOENT   2
#define EIO      5
#define ENOMEM  12
#define EISDIR  21
#define EINVAL  22
#define ENOSPC  28
#define EROFS   30
#define ENOSYS  38

/* ------------------------------------------------------------------ */
/* Little-endian accessors                                             */
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

/* ------------------------------------------------------------------ */
/* ext2 filesystem context and per-inode private data                  */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t*  disk;
    uint32_t  block_size;
    uint32_t  blocks_per_group;
    uint32_t  inodes_per_group;
    uint32_t  inode_size;
    uint32_t  gdt_block;         /* block containing the group descriptor table */
    uint32_t  ino_counter;
    uint8_t*  blockbuf;          /* one-block scratch buffer */
} ext2_fs_t;

typedef struct {
    ext2_fs_t* fs;
    uint32_t   ino;
    uint32_t   block[15];        /* i_block[] copied from the on-disk inode */
    uint32_t   size;
} ext2_inode_info_t;

#define EXT2_ROOT_INO  2
#define EXT2_MAGIC     0xEF53

static void ext2_read_block(ext2_fs_t* fs, uint32_t block, void* buf)
{
    memcpy(buf, fs->disk + (uint64_t)block * fs->block_size, fs->block_size);
}

/* Map a logical block of a file to its physical block (direct + single
 * + double indirect; triple indirect is not used by small demo images). */
static uint32_t ext2_block_at(ext2_fs_t* fs, ext2_inode_info_t* info, uint32_t lb)
{
    uint32_t per = fs->block_size / 4; /* pointers per block */
    uint32_t ind, dbl, val;

    if (lb < 12)
        return info->block[lb];
    lb -= 12;

    if (lb < per) {
        ind = info->block[12];
        if (!ind) return 0;
        ext2_read_block(fs, ind, fs->blockbuf);
        return rd32(fs->blockbuf + lb * 4);
    }
    lb -= per;

    if (lb < per * per) {
        dbl = info->block[13];
        if (!dbl) return 0;
        ext2_read_block(fs, dbl, fs->blockbuf);
        ind = rd32(fs->blockbuf + (lb / per) * 4);
        if (!ind) return 0;
        ext2_read_block(fs, ind, fs->blockbuf);
        val = rd32(fs->blockbuf + (lb % per) * 4);
        return val;
    }
    return 0; /* triple indirect not supported */
}

/* Read an on-disk inode into the caller-provided buffers. */
static int ext2_read_inode(ext2_fs_t* fs, uint32_t ino,
                           uint16_t* mode, uint32_t* size, uint32_t block[15])
{
    uint32_t group, index, desc_off, inode_off;
    uint32_t inode_table, iblock, ioff;
    uint32_t i;

    if (ino == 0) return -1;
    group = (ino - 1) / fs->inodes_per_group;
    index = (ino - 1) % fs->inodes_per_group;
    desc_off = group * 32;

    ext2_read_block(fs, fs->gdt_block + desc_off / fs->block_size, fs->blockbuf);
    inode_table = rd32(fs->blockbuf + (desc_off % fs->block_size) + 8);

    inode_off = index * fs->inode_size;
    iblock = inode_table + inode_off / fs->block_size;
    ioff = inode_off % fs->block_size;

    ext2_read_block(fs, iblock, fs->blockbuf);
    if (mode) *mode = rd16(fs->blockbuf + ioff + 0);
    if (size) *size = rd32(fs->blockbuf + ioff + 4);
    if (block) {
        for (i = 0; i < 15; i++)
            block[i] = rd32(fs->blockbuf + ioff + 40 + i * 4);
    }
    return 0;
}

static struct file_operations ext2_fops;

static inode_t* ext2_make_inode(ext2_fs_t* fs, uint32_t ino,
                                uint16_t mode, uint32_t size,
                                const uint32_t block[15])
{
    inode_t* inode = (inode_t*)kmalloc(sizeof(inode_t));
    ext2_inode_info_t* info;
    uint32_t i;
    if (!inode) return NULL;
    info = (ext2_inode_info_t*)kmalloc(sizeof(ext2_inode_info_t));
    if (!info) { kfree(inode); return NULL; }
    info->fs = fs;
    info->ino = ino;
    info->size = size;
    for (i = 0; i < 15; i++) info->block[i] = block[i];
    inode->ino = ino;
    inode->mode = (uint32_t)mode & 0xF000;
    if ((mode & 0xF000) == 0) inode->mode = S_IFREG; /* fallback */
    inode->mode |= (mode & 0xFFF);
    inode->size = size;
    inode->uid = 0;
    inode->gid = 0;
    inode->nlink = 1;
    inode->fop = &ext2_fops;
    inode->fs_private = info;
    inode->mount = NULL;
    return inode;
}

/* ------------------------------------------------------------------ */
/* file_operations (read-only)                                         */
/* ------------------------------------------------------------------ */
static int ext2_open(inode_t* inode, file_t* file)
{
    (void)inode; (void)file;
    return 0;
}

static ssize_t ext2_read(file_t* file, char* buf, size_t sz)
{
    inode_t* inode = file->inode;
    ext2_inode_info_t* info = (ext2_inode_info_t*)inode->fs_private;
    ext2_fs_t* fs = info->fs;
    uint32_t off, avail;
    size_t done = 0;

    if (S_ISDIR(inode->mode)) return -EISDIR;
    if (file->offset >= info->size) return 0;
    avail = info->size - (uint32_t)file->offset;
    if (sz > avail) sz = avail;
    off = (uint32_t)file->offset;

    while (done < sz) {
        uint32_t lb = off / fs->block_size;
        uint32_t in_block = off % fs->block_size;
        uint32_t chunk = fs->block_size - in_block;
        uint32_t phys;
        if (chunk > sz - done) chunk = (uint32_t)(sz - done);
        phys = ext2_block_at(fs, info, lb);
        if (phys == 0) {
            memset(buf + done, 0, chunk); /* sparse hole */
        } else {
            ext2_read_block(fs, phys, fs->blockbuf);
            memcpy(buf + done, fs->blockbuf + in_block, chunk);
        }
        done += chunk;
        off += chunk;
    }
    return (ssize_t)done;
}

static ssize_t ext2_write(file_t* file, const char* buf, size_t sz)
{
    (void)file; (void)buf; (void)sz;
    return -(ssize_t)EROFS;
}

static int ext2_close(file_t* file)
{
    (void)file;
    return 0;
}

static int ext2_readdir(file_t* file, struct dirent* de, unsigned int index)
{
    inode_t* inode = file->inode;
    ext2_inode_info_t* info = (ext2_inode_info_t*)inode->fs_private;
    ext2_fs_t* fs = info->fs;
    uint32_t off = 0;
    unsigned int count = 0;

    while (off < info->size) {
        uint32_t lb = off / fs->block_size;
        uint32_t in_block = off % fs->block_size;
        uint32_t phys = ext2_block_at(fs, info, lb);
        uint32_t rec_len, name_len, ftype, ino;
        const uint8_t* p;
        if (phys == 0) break;
        ext2_read_block(fs, phys, fs->blockbuf);
        p = fs->blockbuf + in_block;
        ino = rd32(p + 0);
        rec_len = rd16(p + 4);
        name_len = p[6];
        ftype = p[7];
        if (rec_len == 0) break;
        if (ino != 0 && name_len > 0) {
            if (count == index) {
                size_t n = name_len;
                if (n >= VFS_NAME_MAX) n = VFS_NAME_MAX - 1;
                memcpy(de->name, p + 8, n);
                de->name[n] = '\0';
                de->ino = ino;
                de->type = (ftype == 2) ? S_IFDIR : S_IFREG;
                return 1;
            }
            count++;
        }
        off += rec_len;
    }
    return 0; /* end of directory */
}

static inode_t* ext2_lookup(inode_t* dir, const char* name)
{
    ext2_inode_info_t* dinfo = (ext2_inode_info_t*)dir->fs_private;
    ext2_fs_t* fs = dinfo->fs;
    uint32_t off = 0;
    size_t namelen = strlen(name);

    if (!name || !*name) return NULL;

    while (off < dinfo->size) {
        uint32_t lb = off / fs->block_size;
        uint32_t in_block = off % fs->block_size;
        uint32_t phys = ext2_block_at(fs, dinfo, lb);
        uint32_t rec_len, name_len, ino, ftype;
        const uint8_t* p;
        if (phys == 0) break;
        ext2_read_block(fs, phys, fs->blockbuf);
        p = fs->blockbuf + in_block;
        ino = rd32(p + 0);
        rec_len = rd16(p + 4);
        name_len = p[6];
        ftype = p[7];
        if (rec_len == 0) break;
        if (ino != 0 && name_len == namelen &&
            memcmp(p + 8, name, namelen) == 0) {
            uint16_t mode;
            uint32_t size, block[15];
            (void)ftype;
            if (ext2_read_inode(fs, ino, &mode, &size, block) < 0)
                return NULL;
            return ext2_make_inode(fs, ino, mode, size, block);
        }
        off += rec_len;
    }
    return NULL;
}

static int ext2_mkdir(inode_t* dir, const char* name, uint32_t mode)
{
    (void)dir; (void)name; (void)mode;
    return -EROFS;
}

static int ext2_unlink(inode_t* dir, const char* name)
{
    (void)dir; (void)name;
    return -EROFS;
}

static int ext2_rename(inode_t* old_dir, const char* old_name,
                       inode_t* new_dir, const char* new_name)
{
    (void)old_dir; (void)old_name; (void)new_dir; (void)new_name;
    return -EROFS;
}

static struct file_operations ext2_fops = {
    .open    = ext2_open,
    .read    = ext2_read,
    .write   = ext2_write,
    .close   = ext2_close,
    .readdir = ext2_readdir,
    .lookup  = ext2_lookup,
    .mkdir   = ext2_mkdir,
    .unlink  = ext2_unlink,
    .rename  = ext2_rename,
};

/* ------------------------------------------------------------------ */
/* Mount                                                               */
/* ------------------------------------------------------------------ */
inode_t* ext2_mount(void* disk_base, size_t size)
{
    uint8_t sb[1024];
    ext2_fs_t* fs;
    uint32_t log_block_size;
    uint16_t magic, inode_size_raw;
    uint16_t root_mode;
    uint32_t root_size, root_block[15];
    inode_t* root;

    (void)size;
    if (!disk_base)
        return NULL;

    /* Superblock lives at offset 1024. */
    memcpy(sb, (uint8_t*)disk_base + 1024, 1024);
    magic = rd16(sb + 56);
    if (magic != EXT2_MAGIC)
        return NULL;

    fs = (ext2_fs_t*)kcalloc(1, sizeof(ext2_fs_t));
    if (!fs) return NULL;

    log_block_size = rd32(sb + 24);
    fs->block_size = 1024u << log_block_size;
    fs->blocks_per_group = rd32(sb + 32);
    fs->inodes_per_group = rd32(sb + 40);
    inode_size_raw = rd16(sb + 88);
    fs->inode_size = (inode_size_raw >= 128) ? inode_size_raw : 128;
    fs->gdt_block = (fs->block_size == 1024) ? 1 : 0;
    fs->disk = (uint8_t*)disk_base;
    fs->ino_counter = 0;

    fs->blockbuf = (uint8_t*)kmalloc(fs->block_size);
    if (!fs->blockbuf) { kfree(fs); return NULL; }

    if (ext2_read_inode(fs, EXT2_ROOT_INO, &root_mode, &root_size, root_block) < 0) {
        kfree(fs->blockbuf);
        kfree(fs);
        return NULL;
    }
    root = ext2_make_inode(fs, EXT2_ROOT_INO, root_mode, root_size, root_block);
    if (!root) {
        kfree(fs->blockbuf);
        kfree(fs);
        return NULL;
    }
    return root;
}
