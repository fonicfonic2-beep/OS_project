#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include "types.h"

/* --- Magic values --- */
#define MULTIBOOT2_HEADER_MAGIC         0xE85250D6
#define MULTIBOOT2_BOOTLOADER_MAGIC     0x36D76289
#define MULTIBOOT2_ARCH_I386            0

/* --- OS image header (placed in .multiboot section) --- */
typedef struct PACKED multiboot_header {
    uint32_t magic;
    uint32_t architecture;
    uint32_t header_length;
    uint32_t checksum;
} multiboot_header_t;

typedef struct PACKED multiboot_header_tag {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
} multiboot_header_tag_t;

/* Header tag types */
#define MULTIBOOT_HEADER_TAG_END            0
#define MULTIBOOT_HEADER_TAG_INFO_REQ       1
#define MULTIBOOT_HEADER_TAG_ADDRESS        2
#define MULTIBOOT_HEADER_TAG_ENTRY_ADDRESS  3
#define MULTIBOOT_HEADER_TAG_FRAMEBUFFER    5
#define MULTIBOOT_HEADER_TAG_MODULE_ALIGN   6
#define MULTIBOOT_HEADER_TAG_EFI_BS         7
#define MULTIBOOT_HEADER_TAG_RELOCATABLE    10

/* --- Bootloader-provided information structure --- */
typedef struct PACKED multiboot_info {
    uint32_t total_size;
    uint32_t reserved;
} multiboot_info_t;

typedef struct PACKED multiboot_tag {
    uint32_t type;
    uint32_t size;
} multiboot_tag_t;

/* Info tag types */
#define MULTIBOOT_TAG_TYPE_END              0
#define MULTIBOOT_TAG_TYPE_CMDLINE          1
#define MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME 2
#define MULTIBOOT_TAG_TYPE_MODULE           3
#define MULTIBOOT_TAG_TYPE_BASIC_MEMINFO    4
#define MULTIBOOT_TAG_TYPE_BOOTDEV          5
#define MULTIBOOT_TAG_TYPE_MMAP             6
#define MULTIBOOT_TAG_TYPE_VBE              7
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER      8
#define MULTIBOOT_TAG_TYPE_ELF_SYMBOLS      9
#define MULTIBOOT_TAG_TYPE_APM              10
#define MULTIBOOT_TAG_TYPE_EFI32            11
#define MULTIBOOT_TAG_TYPE_EFI64            12
#define MULTIBOOT_TAG_TYPE_SMBIOS           13
#define MULTIBOOT_TAG_TYPE_ACPI_OLD         14
#define MULTIBOOT_TAG_TYPE_ACPI_NEW         15
#define MULTIBOOT_TAG_TYPE_NETWORK          16
#define MULTIBOOT_TAG_TYPE_EFI_MMAP         17
#define MULTIBOOT_TAG_TYPE_EFI_BS           18
#define MULTIBOOT_TAG_TYPE_EFI32_IH         19
#define MULTIBOOT_TAG_TYPE_EFI64_IH         20
#define MULTIBOOT_TAG_TYPE_LOAD_BASE        21

/* --- Memory map tag --- */
typedef struct PACKED multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} multiboot_mmap_entry_t;

#define MULTIBOOT_MEMORY_AVAILABLE          1
#define MULTIBOOT_MEMORY_RESERVED           2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE   3
#define MULTIBOOT_MEMORY_NVS                4
#define MULTIBOOT_MEMORY_BADRAM             5

typedef struct PACKED multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    multiboot_mmap_entry_t entries[1];
} multiboot_tag_mmap_t;

/* --- String tags (cmdline / boot loader name) --- */
typedef struct PACKED multiboot_tag_string {
    uint32_t type;
    uint32_t size;
    char string[1];
} multiboot_tag_string_t;

typedef multiboot_tag_string_t multiboot_tag_cmdline_t;
typedef multiboot_tag_string_t multiboot_tag_boot_loader_name_t;

/* --- Module tag (initrd etc.) --- */
typedef struct PACKED multiboot_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[1];
} multiboot_tag_module_t;

/* --- Basic memory info tag --- */
typedef struct PACKED multiboot_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} multiboot_tag_basic_meminfo_t;

/* --- BIOS boot device tag --- */
typedef struct PACKED multiboot_tag_bootdev {
    uint32_t type;
    uint32_t size;
    uint32_t biosdev;
    uint32_t partition;
    uint32_t subpartition;
} multiboot_tag_bootdev_t;

/* --- Framebuffer tag --- */
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED   0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB       1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT  2

typedef struct PACKED multiboot_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
    /* RGB color fields (valid when framebuffer_type == RGB) */
    uint16_t red_pos;
    uint16_t red_mask;
    uint16_t green_pos;
    uint16_t green_mask;
    uint16_t blue_pos;
    uint16_t blue_mask;
    uint16_t color_reserved;
} multiboot_tag_framebuffer_t;

/* --- ELF symbols / sections tag --- */
typedef struct PACKED multiboot_elf_section_header {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} multiboot_elf_section_header_t;

#define MULTIBOOT_ELF_SHN_UNDEF  0

typedef struct PACKED multiboot_tag_elf_sections {
    uint32_t type;
    uint32_t size;
    uint32_t num;          /* number of section headers (shnum) */
    uint32_t entsize;      /* size of one section header (shentsize) */
    uint32_t shndx;        /* index of section-name string table */
    uint32_t reserved;
    multiboot_elf_section_header_t sections[1];
} multiboot_tag_elf_sections_t;

/* --- ACPI RSDP tags --- */
typedef struct PACKED multiboot_tag_old_acpi {
    uint32_t type;
    uint32_t size;
    uint8_t  rsdp[1];
} multiboot_tag_old_acpi_t;

typedef struct PACKED multiboot_tag_new_acpi {
    uint32_t type;
    uint32_t size;
    uint8_t  rsdp[1];
} multiboot_tag_new_acpi_t;

/* --- SMBIOS tag --- */
typedef struct PACKED multiboot_tag_smbios {
    uint32_t type;
    uint32_t size;
    uint8_t  major;
    uint8_t  minor;
    uint8_t  reserved[6];
    uint8_t  tables[1];
} multiboot_tag_smbios_t;

/* --- EFI memory map tag --- */
typedef struct PACKED multiboot_tag_efi_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t descr_size;
    uint32_t descr_version;
    uint8_t  efi_mmap[1];
} multiboot_tag_efi_mmap_t;

/* --- Image load base address tag --- */
typedef struct PACKED multiboot_tag_load_base {
    uint32_t type;
    uint32_t size;
    uint32_t load_base_addr;
} multiboot_tag_load_base_t;

/* --- Tag iteration helpers (tags are 8-byte aligned) --- */
#define MULTIBOOT_TAG_FIRST(info) \
    ((multiboot_tag_t*)((uint8_t*)(info) + sizeof(multiboot_info_t)))

#define MULTIBOOT_TAG_NEXT(tag) \
    ((multiboot_tag_t*)((uint8_t*)(tag) + (((tag)->size + 7u) & ~7u)))

#endif /* MULTIBOOT2_H */
