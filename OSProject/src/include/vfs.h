#ifndef VFS_H
#define VFS_H

#include "types.h"

#define VFS_PATH_MAX    4096
#define VFS_NAME_MAX    256
#define VFS_FD_MAX      64

/* Forward declarations */
typedef struct inode inode_t;
typedef struct file  file_t;

/* File open flags */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0100
#define O_EXCL     0x0200
#define O_TRUNC    0x0400
#define O_APPEND   0x0800
#define O_DIRECTORY 0x1000

/* inode mode bits (subset) */
#define S_IFMT     0xF000
#define S_IFREG    0x8000
#define S_IFDIR    0x4000
#define S_IFCHR    0x2000
#define S_IFBLK    0x6000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)

struct dirent {
    char     name[VFS_NAME_MAX];
    uint32_t ino;
    uint32_t type;   /* S_IFREG / S_IFDIR ... */
};

/* Filesystem operations attached to an inode */
struct file_operations {
    int       (*open)(inode_t* inode, file_t* file);
    ssize_t   (*read)(file_t* file, char* buf, size_t sz);
    ssize_t   (*write)(file_t* file, const char* buf, size_t sz);
    int       (*close)(file_t* file);
    int       (*readdir)(file_t* file, struct dirent* de, unsigned int index);
    inode_t*  (*lookup)(inode_t* dir, const char* name);
    int       (*mkdir)(inode_t* dir, const char* name, uint32_t mode);
    int       (*unlink)(inode_t* dir, const char* name);
    int       (*rename)(inode_t* old_dir, const char* old_name,
                        inode_t* new_dir, const char* new_name);
};

struct inode {
    uint32_t              ino;
    uint32_t              mode;
    uint32_t              size;
    uint32_t              uid;
    uint32_t              gid;
    uint32_t              nlink;
    struct file_operations* fop;
    void*                 fs_private;   /* filesystem-specific data */
    inode_t*              mount;        /* != NULL if a mount point  */
};

struct file {
    inode_t* inode;
    uint64_t offset;
    uint32_t flags;
    int      refcount;
};

/* Per-process file descriptor table (implemented in process/syscall layer) */
typedef struct fd_table {
    file_t* slots[VFS_FD_MAX];
} fd_table_t;

/* VFS API */
int      vfs_mount(const char* path, inode_t* root);
int      vfs_open(const char* path, int flags, file_t** out);
ssize_t  vfs_read(file_t* file, char* buf, size_t sz);
ssize_t  vfs_write(file_t* file, const char* buf, size_t sz);
int      vfs_close(file_t* file);
int      vfs_mkdir(const char* path);
int      vfs_unlink(const char* path);
int      vfs_rename(const char* old_path, const char* new_path);
int      vfs_opendir(const char* path, file_t** out);
int      vfs_readdir(file_t* file, struct dirent* de);
char*    vfs_getcwd(char* buf, size_t sz);
int      vfs_chdir(const char* path);
inode_t* vfs_root(void);
const char* vfs_cwd(void);

#endif /* VFS_H */
