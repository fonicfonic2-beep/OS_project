#ifndef FAT32_H
#define FAT32_H

#include "types.h"
#include "vfs.h"

/*
 * FAT32 read/write driver over a memory-mapped disk image
 * (e.g. initrd or AHCI-mapped region). Returns the root inode
 * to be mounted via vfs_mount.
 */
inode_t* fat32_mount(void* disk_base, size_t size);

#endif /* FAT32_H */
