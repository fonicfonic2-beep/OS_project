# ============================================================================
#  MyOS x86_64 - Makefile
#  Build: make          -> build/kernel.bin + iso/myos.bin
#         make iso      -> build/MyOS.iso (bootable, GRUB2 Multiboot2)
#         make run      -> boot ISO in qemu-system-x86_64
#         make clean
# ============================================================================

# Toolchain: prefer x86_64-elf cross compiler; fall back to native gcc.
CC      ?= x86_64-elf-gcc
LD      ?= x86_64-elf-ld
AS      ?= nasm
GRUBMK  ?= grub-mkrescue
QEMU    ?= qemu-system-x86_64

# If cross compiler not installed, native gcc/ld can build this freestanding
# kernel too (tested with gcc 11). Uncomment the two lines below:
# CC = gcc
# LD = ld

CFLAGS  := -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
           -mno-red-zone -mcmodel=kernel -mgeneral-regs-only -fno-pic \
           -Wall -Wextra -I src/include -g -O2
ASFLAGS := -f elf64 -g -F dwarf
LDFLAGS := -n -T linker.ld

BUILD   := build
ISO     := iso
TARGET  := $(BUILD)/MyOS.iso
KERNEL  := $(ISO)/boot/myos.bin
INITRD  := $(ISO)/boot/initrd.img

# Discover all sources
C_SRC   := $(shell find src -name '*.c')
ASM_SRC := $(shell find src -name '*.asm')
OBJ     := $(patsubst src/%.c,$(BUILD)/%.o,$(C_SRC)) \
           $(patsubst src/%.asm,$(BUILD)/%.o,$(ASM_SRC))

.PHONY: all iso run clean kernel

all: iso

kernel: $(KERNEL)

# Link kernel ELF (64-bit, higher-half)
$(BUILD)/kernel.elf: $(OBJ) linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

# Strip to flat binary for GRUB (GRUB multiboot2 loads ELF too, but flat is safe)
$(KERNEL): $(BUILD)/kernel.elf
	@mkdir -p $(ISO)/boot
	objcopy -O binary $< $@
	@# If you prefer loading the ELF directly, copy the .elf instead:
	@# cp $< $@

# Compile C
$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble NASM
$(BUILD)/%.o: src/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# Create a small FAT32 ramdisk image as initrd (if not present)
$(INITRD):
	@mkdir -p $(ISO)/boot
	@echo "Creating minimal FAT32 initrd image (32 MB)..."
	dd if=/dev/zero of=$@ bs=1M count=32 status=none
	@# Format as FAT32 if mkfs.fat exists; otherwise leave blank (kernel
	@# will still boot, fs commands report "no filesystem").
	@if command -v mkfs.fat >/dev/null 2>&1; then \
	    mkfs.fat -F 32 -n MYOS-RD $@ >/dev/null 2>&1 || true; \
	fi

iso: $(KERNEL) $(INITRD) iso/boot/grub/grub.cfg
	@mkdir -p $(BUILD)
	$(GRUBMK) -o $(TARGET) $(ISO)
	@echo "Built: $(TARGET)"

run: iso
	$(QEMU) -cdrom $(TARGET) -m 256M -serial stdio -vga std

clean:
	rm -rf $(BUILD) $(KERNEL) $(INITRD)
