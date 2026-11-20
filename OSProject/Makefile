# ==========================================
# CẤU HÌNH
# ==========================================
CC      := gcc
NASM    := nasm
LD      := ld

CFLAGS  := -ffreestanding -m64 -mno-red-zone -mcmodel=kernel -mgeneral-regs-only -Wall -Wextra
NASMFLAGS := -f elf64 -g -F dwarf
LDFLAGS := -nostdlib -T linker.ld

SRC     := src
BUILD   := build
ISODIR  := iso

C_SRC   := $(shell find $(SRC) -name "*.c")
ASM_SRC := $(shell find $(SRC) -name "*.asm")

C_OBJ   := $(patsubst $(SRC)/%.c, $(BUILD)/%.o, $(C_SRC))
ASM_OBJ := $(patsubst $(SRC)/%.asm, $(BUILD)/%.o, $(ASM_SRC))
OBJS    := $(C_OBJ) $(ASM_OBJ)

KERNEL  := $(BUILD)/kernel.bin
ISO     := $(BUILD)/MyOS.iso

# ==========================================
# MỤC TIÊU
# ==========================================
.PHONY: all clean run

all: $(ISO)

$(KERNEL): $(OBJS) linker.ld
 $(LD) $(LDFLAGS) $(OBJS) -o $@

$(BUILD)/%.o: $(SRC)/%.c
 @mkdir -p $(dir $@)
 $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.asm
 @mkdir -p $(dir $@)
 $(NASM) $(NASMFLAGS) $< -o $@

$(ISO): $(KERNEL)
 @mkdir -p $(ISODIR)/boot/grub
 cp $(KERNEL) $(ISODIR)/boot/kernel.bin
 cp grub.cfg $(ISODIR)/boot/grub/grub.cfg
 xorriso -as mkisofs -o $@ -isohybrid-mbr /usr/lib/grub/i386-pc/boot_hybrid.img \
 -b boot/grub/i386-pc/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table \
 --grub2-boot-info --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img $(ISODIR)

run: $(ISO)
 qemu-system-x86_64 -cdrom $(ISO)
