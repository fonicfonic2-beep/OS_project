/*
 * initrd.c — locate the multiboot2 module loaded by GRUB.
 *
 * The module is a FAT32 disk image placed in low physical memory by the
 * bootloader. We return its higher-half virtual address so the FAT32
 * driver can operate on it as a memory-mapped ramdisk.
 */
#include "initrd.h"
#include "common.h"

void* initrd_init(multiboot_info_t* mbi, size_t* out_size)
{
    uint8_t* end;
    multiboot_tag_t* tag;

    if (out_size)
        *out_size = 0;
    if (!mbi || mbi->total_size < sizeof(multiboot_info_t))
        return NULL;

    end = (uint8_t*)mbi + mbi->total_size;
    tag = MULTIBOOT_TAG_FIRST(mbi);

    while ((uint8_t*)tag + sizeof(multiboot_tag_t) <= end &&
           tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            multiboot_tag_module_t* mod = (multiboot_tag_module_t*)tag;
            if (mod->mod_end > mod->mod_start && out_size)
                *out_size = (size_t)(mod->mod_end - mod->mod_start);
            return (void*)PHYS_TO_VIRT(mod->mod_start);
        }
        tag = MULTIBOOT_TAG_NEXT(tag);
        if ((uint8_t*)tag >= end)
            break;
    }
    return NULL;
}
