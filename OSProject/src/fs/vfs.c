/*
 * vfs.c — Virtual File System layer.
 *
 * Responsibilities:
 *   - mount table (up to 4 mounts, "/" is the FAT32 root by convention)
 *   - global current working directory (defaults to "/")
 *   - path resolution (split on '/', handle "." and "..", cross mounts
 *     via inode->mount, delegate each component to inode->fop->lookup)
 *   - global file-descriptor table (up to VFS_FD_MAX slots); vfs_open
 *     returns the allocated slot index on success (>= 0) and also writes
 *     the file_t* to *out for callers that prefer object-style access.
 *
 * All filesystem-specific work is delegated through struct file_operations.
 */
#include "vfs.h"
#include "string.h"
#include "heap.h"
#include "common.h"

/* --- errno values (this kernel has no errno.h) --- */
#define EPERM      1
#define ENOENT     2
#define EIO        5
#define EBADF      9
#define ENOMEM    12
#define EACCES    13
#define EEXIST    17
#define EXDEV     18
#define ENOTDIR   20
#define EISDIR    21
#define EINVAL    22
#define EMFILE    24
#define ENOSPC    28
#define EROFS     30
#define ENOSYS    38
#define ENOTEMPTY 39

#define VFS_MAX_MOUNTS 4

/* ------------------------------------------------------------------ */
/* Generic ramdisk sector access (shared by FAT32 / ext2 drivers).     */
/* The disk image is memory-mapped, so a sector read/write is a memcpy */
/* at disk_base + lba * bytes_per_sector.                              */
/* ------------------------------------------------------------------ */
void ramdisk_read_sector(void* disk_base, uint32_t lba,
                         uint32_t bytes_per_sector, void* buf)
{
    memcpy(buf,
           (uint8_t*)disk_base + (uint64_t)lba * bytes_per_sector,
           bytes_per_sector);
}

void ramdisk_write_sector(void* disk_base, uint32_t lba,
                          uint32_t bytes_per_sector, const void* buf)
{
    memcpy((uint8_t*)disk_base + (uint64_t)lba * bytes_per_sector,
           buf, bytes_per_sector);
}

/* ------------------------------------------------------------------ */
/* Mount table + root + cwd state                                      */
/* ------------------------------------------------------------------ */
static struct {
    char*     path;     /* mount point path (owned copy) */
    inode_t*  covered;  /* inode covered by the mount (NULL for "/") */
    inode_t*  root;     /* root inode of the mounted filesystem */
} mounts[VFS_MAX_MOUNTS];
static int nr_mounts = 0;

static inode_t* vfs_root_inode = NULL;

static char     cwd_path[VFS_PATH_MAX] = "/";
static inode_t* cwd_inode = NULL;

/* Global file-descriptor table. */
static file_t* fd_table[VFS_FD_MAX];

/* ------------------------------------------------------------------ */
/* Path resolution                                                     */
/* ------------------------------------------------------------------ */

/*
 * Walk a path component by component.
 *   parent_only != 0: stop before the last component, write its name into
 *                     'last_name' (empty if the path ends in '/') and the
 *                     parent directory inode into *parent_out. Returns the
 *                     parent inode (== *parent_out) or NULL on failure.
 *   parent_only == 0: resolve the whole path, return the final inode.
 *
 * Mount crossing: whenever a lookup yields an inode whose ->mount is set,
 * we descend into the mounted root. "." and ".." are delegated to the
 * filesystem's lookup, which must keep ".." at the filesystem root.
 */
static inode_t* walk_path(const char* path, int parent_only,
                          char last_name[VFS_NAME_MAX],
                          inode_t** parent_out)
{
    inode_t* cur;
    const char* p;
    char comp[VFS_NAME_MAX];

    if (parent_out)
        *parent_out = NULL;
    if (last_name)
        last_name[0] = '\0';
    if (!path || !*path)
        return NULL;

    p = path;
    if (*p == '/') {
        cur = vfs_root_inode;
        p++;
    } else {
        cur = cwd_inode ? cwd_inode : vfs_root_inode;
    }
    if (!cur)
        return NULL;
    if (cur->mount)
        cur = cur->mount;

    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;

        {
            size_t i = 0;
            while (*p && *p != '/' && i < VFS_NAME_MAX - 1)
                comp[i++] = *p++;
            comp[i] = '\0';
        }

        /* If a slash follows, this component has more path after it. */
        {
            int more = (*p == '/');
            while (*p == '/')
                p++;

            if (parent_only && !*p && !more) {
                if (last_name)
                    strcpy(last_name, comp);
                if (parent_out)
                    *parent_out = cur;
                return cur;
            }
        }

        if (!S_ISDIR(cur->mode))
            return NULL;
        if (!cur->fop || !cur->fop->lookup)
            return NULL;

        {
            inode_t* next = cur->fop->lookup(cur, comp);
            if (!next)
                return NULL;
            if (next->mount)
                next = next->mount;
            cur = next;
        }
    }

    if (parent_only) {
        /* Path was "/" or ended with '/' — target is the directory itself. */
        if (last_name)
            last_name[0] = '\0';
        if (parent_out)
            *parent_out = cur;
    }
    return cur;
}

/* Collapse "/./" and "/../" in a path; base is used when rel is relative. */
static void normalize_path(char* out, const char* base, const char* rel)
{
    static char tmp[VFS_PATH_MAX];
    char* comps[128];
    int n = 0;
    char* p;

    if (rel[0] == '/') {
        strncpy(tmp, rel, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
    } else {
        size_t bl = strlen(base);
        strncpy(tmp, base, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        if (bl == 0 || tmp[bl - 1] != '/') {
            if (bl + 1 < sizeof(tmp)) {
                tmp[bl] = '/';
                tmp[bl + 1] = '\0';
            }
        }
        strncat(tmp, rel, sizeof(tmp) - strlen(tmp) - 1);
    }

    p = tmp;
    while (*p) {
        char* start;
        char saved;
        while (*p == '/')
            p++;
        if (!*p)
            break;
        start = p;
        while (*p && *p != '/')
            p++;
        saved = *p;
        *p = '\0';
        if (strcmp(start, ".") == 0) {
            /* skip */
        } else if (strcmp(start, "..") == 0) {
            if (n > 0)
                n--;
        } else if (n < 128) {
            comps[n++] = start;
        }
        *p = saved;
    }

    out[0] = '/';
    out[1] = '\0';
    for (int i = 0; i < n; i++) {
        if (i > 0)
            strcat(out, "/");
        strncat(out, comps[i], VFS_PATH_MAX - strlen(out) - 1);
    }
}

/* ------------------------------------------------------------------ */
/* VFS public API                                                      */
/* ------------------------------------------------------------------ */

int vfs_mount(const char* path, inode_t* root)
{
    if (!path || !root)
        return -EINVAL;
    if (nr_mounts >= VFS_MAX_MOUNTS)
        return -ENOSPC;

    if (strcmp(path, "/") == 0) {
        vfs_root_inode = root;
        if (!cwd_inode) {
            cwd_inode = root;
            strcpy(cwd_path, "/");
        }
        mounts[nr_mounts].path = strdup("/");
        mounts[nr_mounts].covered = NULL;
        mounts[nr_mounts].root = root;
        nr_mounts++;
        return 0;
    }

    {
        inode_t* covered = walk_path(path, 0, NULL, NULL);
        if (!covered)
            return -ENOENT;
        if (!S_ISDIR(covered->mode))
            return -ENOTDIR;
        covered->mount = root;
        mounts[nr_mounts].path = strdup(path);
        mounts[nr_mounts].covered = covered;
        mounts[nr_mounts].root = root;
        nr_mounts++;
        return 0;
    }
}

inode_t* vfs_root(void)
{
    return vfs_root_inode;
}

const char* vfs_cwd(void)
{
    return cwd_path;
}

char* vfs_getcwd(char* buf, size_t sz)
{
    if (!buf || sz == 0)
        return NULL;
    if (strlen(cwd_path) + 1 > sz)
        return NULL;
    strcpy(buf, cwd_path);
    return buf;
}

int vfs_chdir(const char* path)
{
    inode_t* ino;
    if (!path)
        return -EINVAL;
    ino = walk_path(path, 0, NULL, NULL);
    if (!ino)
        return -ENOENT;
    if (!S_ISDIR(ino->mode))
        return -ENOTDIR;
    cwd_inode = ino;
    normalize_path(cwd_path, cwd_path, path);
    return 0;
}

/* Allocate a file descriptor slot. Returns index or -EMFILE. */
static int fd_alloc(file_t* f)
{
    int i;
    for (i = 0; i < VFS_FD_MAX; i++) {
        if (!fd_table[i]) {
            fd_table[i] = f;
            return i;
        }
    }
    return -EMFILE;
}

int vfs_open(const char* path, int flags, file_t** out)
{
    char name[VFS_NAME_MAX];
    inode_t* dir;
    inode_t* ino;
    file_t* f;
    int fd;

    if (out)
        *out = NULL;
    if (!path)
        return -EINVAL;

    dir = walk_path(path, 1, name, NULL);
    if (!dir)
        return -ENOENT;

    if (name[0] == '\0') {
        /* Path refers to a directory itself ("/", "/foo/", ...). */
        ino = dir;
        if (!S_ISDIR(ino->mode))
            return -ENOTDIR;
        if ((flags & (O_WRONLY | O_RDWR)) && !(flags & O_DIRECTORY))
            return -EISDIR;
    } else {
        if (!S_ISDIR(dir->mode))
            return -ENOTDIR;
        if (!dir->fop || !dir->fop->lookup)
            return -ENOSYS;

        ino = dir->fop->lookup(dir, name);
        if (!ino) {
            if (!(flags & O_CREAT))
                return -ENOENT;
            if (!dir->fop || !dir->fop->mkdir)
                return -ENOSYS;
            {
                int r = dir->fop->mkdir(dir, name, S_IFREG | 0644);
                if (r < 0)
                    return r;
            }
            ino = dir->fop->lookup(dir, name);
            if (!ino)
                return -ENOENT;
        } else if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
            return -EEXIST;
        }

        if ((flags & O_DIRECTORY) && !S_ISDIR(ino->mode))
            return -ENOTDIR;
        if (S_ISDIR(ino->mode) && (flags & (O_WRONLY | O_RDWR)))
            return -EISDIR;
    }

    f = (file_t*)kmalloc(sizeof(file_t));
    if (!f)
        return -ENOMEM;
    f->inode = ino;
    f->offset = 0;
    f->flags = (uint32_t)flags;
    f->refcount = 1;

    if (ino->fop && ino->fop->open) {
        int r = ino->fop->open(ino, f);
        if (r < 0) {
            kfree(f);
            return r;
        }
    }

    fd = fd_alloc(f);
    if (fd < 0) {
        if (ino->fop && ino->fop->close)
            ino->fop->close(f);
        kfree(f);
        return fd;
    }

    if (out)
        *out = f;
    return fd;
}

ssize_t vfs_read(file_t* file, char* buf, size_t sz)
{
    ssize_t r;
    if (!file || !buf)
        return -EBADF;
    if (!file->inode || !file->inode->fop || !file->inode->fop->read)
        return -EINVAL;
    r = file->inode->fop->read(file, buf, sz);
    if (r > 0)
        file->offset += (uint64_t)r;
    return r;
}

ssize_t vfs_write(file_t* file, const char* buf, size_t sz)
{
    ssize_t r;
    if (!file || !buf)
        return -EBADF;
    if (!file->inode || !file->inode->fop || !file->inode->fop->write)
        return -EINVAL;
    if ((file->flags & 0x3) == O_RDONLY)
        return -EBADF;
    r = file->inode->fop->write(file, buf, sz);
    if (r > 0)
        file->offset += (uint64_t)r;
    return r;
}

int vfs_close(file_t* file)
{
    int i;
    if (!file)
        return -EBADF;
    if (file->inode && file->inode->fop && file->inode->fop->close)
        file->inode->fop->close(file);
    for (i = 0; i < VFS_FD_MAX; i++) {
        if (fd_table[i] == file)
            fd_table[i] = NULL;
    }
    file->refcount--;
    kfree(file);
    return 0;
}

int vfs_mkdir(const char* path)
{
    char name[VFS_NAME_MAX];
    inode_t* dir;

    if (!path)
        return -EINVAL;
    dir = walk_path(path, 1, name, NULL);
    if (!dir)
        return -ENOENT;
    if (name[0] == '\0')
        return -EEXIST;
    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;
    if (!dir->fop || !dir->fop->mkdir)
        return -ENOSYS;
    return dir->fop->mkdir(dir, name, S_IFDIR | 0755);
}

int vfs_unlink(const char* path)
{
    char name[VFS_NAME_MAX];
    inode_t* dir;

    if (!path)
        return -EINVAL;
    dir = walk_path(path, 1, name, NULL);
    if (!dir)
        return -ENOENT;
    if (name[0] == '\0')
        return -EISDIR;
    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;
    if (!dir->fop || !dir->fop->unlink)
        return -ENOSYS;
    return dir->fop->unlink(dir, name);
}

int vfs_rename(const char* old_path, const char* new_path)
{
    char old_name[VFS_NAME_MAX], new_name[VFS_NAME_MAX];
    inode_t *old_dir, *new_dir;

    if (!old_path || !new_path)
        return -EINVAL;
    old_dir = walk_path(old_path, 1, old_name, NULL);
    if (!old_dir || old_name[0] == '\0')
        return -ENOENT;
    new_dir = walk_path(new_path, 1, new_name, NULL);
    if (!new_dir || new_name[0] == '\0')
        return -ENOENT;
    if (!S_ISDIR(old_dir->mode) || !S_ISDIR(new_dir->mode))
        return -ENOTDIR;
    if (old_dir->fop != new_dir->fop)
        return -EXDEV; /* cross-filesystem rename not supported */
    if (!old_dir->fop || !old_dir->fop->rename)
        return -ENOSYS;
    return old_dir->fop->rename(old_dir, old_name, new_dir, new_name);
}

int vfs_opendir(const char* path, file_t** out)
{
    int fd = vfs_open(path, O_RDONLY | O_DIRECTORY, out);
    if (fd < 0)
        return fd;
    return 0;
}

int vfs_readdir(file_t* file, struct dirent* de)
{
    int r;
    if (!file || !de)
        return -EBADF;
    if (!file->inode || !file->inode->fop || !file->inode->fop->readdir)
        return -ENOTDIR;
    r = file->inode->fop->readdir(file, de, (unsigned int)file->offset);
    if (r > 0)
        file->offset++;
    return r; /* >0: entry filled, 0: end of directory, <0: error */
}
