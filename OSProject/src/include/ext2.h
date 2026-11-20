#ifndef EXT2_H
#define EXT2_H

#include "types.h"
#include "vfs.h"

/*
 * ext2 (second extended filesystem) read/write driver over a
 * memory-mapped disk image. Returns the root inode to be mounted.
 */
inode_t* ext2_mount(void* disk_base, size_t size);

#endif /* EXT2_H */
