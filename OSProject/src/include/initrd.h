#ifndef INITRD_H
#define INITRD_H

#include "types.h"
#include "multiboot2.h"

/*
 * Locate the first multiboot2 module (the initrd / ramdisk image),
 * return its virtual address and write its size in bytes to *out_size.
 * Returns NULL if no module was loaded.
 */
void* initrd_init(multiboot_info_t* mbi, size_t* out_size);

#endif /* INITRD_H */
