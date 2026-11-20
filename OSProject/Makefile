# ==============================
# CẤU HÌNH BIẾN
# ==============================
CC      = gcc
NASM    = nasm
LD      = ld

CFLAGS  = -ffreestanding -m64 -mno-red-zone -mcmodel=kernel -mgeneral-regs-only -Wall -Wextra
NASMFLAGS = -f elf64 -g -F dwarf
LDFLAGS = -nostdlib -T linker.ld

SRC_DIR = src
BUILD_DIR = build
ISO_DIR = iso

# ==============================
# DANH SÁCH FILE
# ==============================
C_SOURCES  := $(shell find $(SRC_DIR) -name '*.c')
ASM_SOURCES := $(shell find $(SRC_DIR) -name '*.asm')

C_OBJECTS  := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SOURCES))
ASM_OBJECTS := $(patsubst $(SRC_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))
OBJECTS     := $(C_OBJECTS) $(ASM_OBJECTS)

KERNEL_BIN = $(BUILD_DIR)/kernel.bin
ISO_FILE   = $(BUILD_DIR)/MyOS.iso

# ==============================
# MỤC TIÊU CHÍNH
# ==============================
.PHONY: all clean run

all: $(ISO_FILE)

$(KERNEL_BIN): $(OBJECTS) linker.ld
 $(LD) $(LDFLAGS) $(OBJECTS) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
 @mkdir -p $(dir $@)
 $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm
 @mkdir -p $(dir $@)
 $(NASM) $(NASMFLAGS) $< -o $@

$(ISO_FILE): $(KERNEL_BIN)
 @mkdir -p $(ISO_DIR)/boot/grub
 cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
 cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
 xorriso -as mkisofs -o $@ -isohybrid-mbr /usr/lib/grub/i386-pc/boot_hybrid.img \
 -b boot/grub/i386-pc/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table \
 --grub2-boot-info --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img $(ISO_DIR)

run: $(ISO_FILE)
 qemu-system-x86_64 -cdrom $(ISO_FILE)

clean:
 rm -rf $(BUILD_DIR) $(ISO_DIR)
