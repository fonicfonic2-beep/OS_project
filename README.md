# MyOS x86_64 — Hệ điều hành tự viết (NASM + C)

Một hệ điều hành giáo dục/demo chạy trên kiến trúc **x86_64 (AMD64)**, boot được bằng **GRUB2 (Multiboot2)** trên **VirtualBox, QEMU và máy thật**. Kernel viết hoàn toàn bằng **NASM Assembly + C thuần**, không dùng thư viện chuẩn của host.

---

## 1. Tính năng

### Kernel
- Boot Multiboot2, vào **long mode 64-bit**, higher-half kernel (`0xFFFFFFFF80000000`).
- **PMM** (Physical Memory Manager) dạng bitmap, đọc memory map từ Multiboot2.
- **Paging 4 cấp** (PML4/PDPT/PD/PT), phân tách kernel space / user space, bảo vệ vùng kernel (US=0), phát hiện page fault / segmentation fault.
- **GDT + TSS** (có ring3), **IDT 256 entry**, **ISR 32 exception**, **IRQ 0–15** (PIC remap + APIC).
- Driver: **PS/2 Keyboard** (scancode set 1, shift/capslock, hỗ trợ tiếng Việt telex cơ bản), **PS/2 Mouse**, **PIT Timer** (1000 Hz), **RTC** (đọc giờ thực), **Local APIC + IO APIC** (fallback PIC).
- **Đa nhiệm Preemptive** (round-robin), quản lý tiến trình/thread (PID, state READY/RUNNING/BLOCKED/ZOMBIE), context switch bằng assembly.
- **Syscall** qua `int 0x80` (exit, fork, read, write, open, close, yield, sbrk, exec, getpid), kiểm tra con trỏ user, giới hạn tài nguyên.

### Hệ thống tệp
- **VFS** abstraction layer.
- **FAT32** đọc/ghi (tạo/xóa/đổi tên file & thư mục) chạy trên **initrd ramdisk**.
- **ext2** read-only (demo).
- Trình quản lý tệp console.

### Giao diện & ngôn ngữ
- **VGA text 80×25** + **VESA graphics 32-bit** (framebuffer qua Multiboot2, 1024×768×32).
- **Console** + **shell dòng lệnh** riêng.
- **Đa ngôn ngữ**: Tiếng Việt / English, **menu chọn ngôn ngữ ngay lúc boot**.

### Ứng dụng tích hợp
Shell, trình soạn thảo văn bản, máy tính (biểu thức +-*/()), trình quản lý tệp, Task Manager (liệt kê PID/state/CPU), đồng hồ hệ thống (RTC).

---

## 2. Cấu trúc thư mục

```
OSProject/
├── Makefile              # build tự động ra ISO
├── linker.ld             # linker script (higher-half)
├── README.md
├── iso/boot/grub/grub.cfg
└── src/
    ├── boot/boot.asm          # Multiboot2 header + long mode entry
    ├── kernel/                 # main.c, panic.c
    ├── arch/x86_64/            # gdt, idt, isr.asm, irq.asm, pic, apic, tss, context_switch.asm
    ├── mm/                     # pmm, paging, heap
    ├── drivers/                # keyboard, mouse, pit, rtc, vga (text+gfx+font), serial
    ├── task/                   # scheduler, process, syscall
    ├── fs/                     # vfs, fat32, ext2, initrd
    ├── ui/                     # console, shell, window (graphics)
    ├── apps/                   # editor, calculator, filemanager, taskmanager, clock
    ├── libc/                   # string, stdio (sprintf), stdlib, ctype (tự viết)
    ├── i18n/                   # i18n, lang_vi, lang_en
    └── include/                # toàn bộ header .h thống nhất
```

---

## 3. Cài đặt công cụ build

### Ubuntu / Debian
```bash
sudo apt install build-essential nasm xorriso grub-pc-bin mtools qemu-system-x86
```
(Để build trên máy thật/USB không cần cross-compiler; gcc native đủ. Nếu muốn dùng cross-compiler: `x86_64-elf-gcc`.)

### Arch Linux
```bash
sudo pacman -S base-devel nasm xorriso grub mtools qemu
```

---

## 4. Build

```bash
make            # build kernel + tạo ISO -> build/MyOS.iso
make run        # boot thử trong QEMU
make clean
```

Nếu không có `x86_64-elf-gcc`, sửa 2 dòng trong Makefile:
```makefile
CC = gcc
LD = ld
```

---

## 5. Boot trên VirtualBox

1. Mở VirtualBox → **New** → Type: `Other`, Version: `Other/Unknown (64-bit)`, RAM ≥ 256 MB, tạo đĩa ảo rỗng (không cần dùng).
2. **Settings → System → Motherboard**: bật **Enable EFI** (tuỳ chọn; kernel hỗ trợ cả Legacy BIOS). Nếu dùng Legacy, bỏ chọn EFI.
3. **Settings → Storage**: nhấn vào ổ CD trống → **Choose a disk file** → chọn `build/MyOS.iso`.
4. **Settings → System → Pointing Device**: chọn **PS/2 Mouse** (không dùng USB Tablet).
5. **Start**. Menu chọn ngôn ngữ hiện ra: nhấn `1` = Tiếng Việt, `2` = English.
6. Gõ `help` trong shell để xem danh sách lệnh.

---

## 6. Boot máy thật (USB)

Cần USB ≥ 64 MB. Cách 1 (dùng GRUB2):
```bash
sudo dd if=build/MyOS.iso of=/dev/sdX bs=4M status=progress && sync
```
(rủi ro: `dd` ghi đè ổ đĩa — kiểm tra kỹ `/dev/sdX` là USB!).

Cách 2 (USB có sẵn GRUB2): copy `iso/boot/myos.bin` và `iso/boot/initrd.img` vào phân vùng USB, thêm vào `grub.cfg`:
```
menuentry "MyOS" {
    multiboot2 /boot/myos.bin
    module2 /boot/initrd.img initrd
    boot
}
```

---

## 7. Lệnh shell

| Lệnh | Chức năng |
|---|---|
| `help` | Danh sách lệnh |
| `ls [dir]` | Liệt kê thư mục |
| `cd <dir>` | Đổi thư mục |
| `cat <file>` | Xem nội dung file |
| `echo <text>` | In chuỗi |
| `clear` | Xóa màn hình |
| `mkdir <name>` | Tạo thư mục |
| `rm <name>` | Xóa file/thư mục |
| `mv <old> <new>` | Đổi tên/di chuyển |
| `edit <file>` | Trình soạn thảo (Ctrl+S lưu, Ctrl+Q thoát) |
| `calc` | Máy tính biểu thức |
| `fileman` / `taskmgr` / `ps` | Quản lý tệp / Task Manager |
| `clock` | Đồng hồ RTC |
| `lang` | Đổi ngôn ngữ |
| `reboot` / `shutdown` | Khởi động lại / tắt máy |

---

## 8. Kiến trúc tổng quan

- **Boot**: GRUB2 load `myos.bin` ở 32-bit protected mode. `boot.asm` kiểm tra magic Multiboot2, dựng PML4 tạm (identity map 2MB + higher-half), bật PAE → long mode → paging, far jump sang 64-bit, gọi `kernel_main()`.
- **Memory**: PMM cấp trang vật lý; paging 4 cấp map toàn bộ RAM vào higher-half; heap cấp phát động cho kernel.
- **Interrupt**: IDT 256 gate; exception in ra `rip/cr2/err_code`; page fault ở user mode kill tiến trình, ở kernel mode panic.
- **Scheduler**: PIT 1000 Hz tick → `scheduler_tick` lưu `registers_t`, chuyển context round-robin.
- **Syscall**: `int 0x80`, số syscall trong `rax`, args trong `rdi,rsi,rdx,r10,r8,r9`; kiểm tra `user_ptr_ok` trước khi đọc buffer của user.
- **FS**: initrd (FAT32 image) được GRUB load làm module; VFS route tới fat32/ext2.

---

## 9. Ghi chú

- Đây là dự án giáo dục: ưu tiên **đầy đủ source code, cấu trúc rõ ràng, build được**; không nhằm mục đích sản xuất.
- Toàn bộ chuỗi i18n và README bằng tiếng Việt; comment trong code bằng tiếng Anh.
- Nếu QEMU/VirtualBox báo lỗi GRUB, kiểm tra đã cài `grub-pc-bin` và `xorriso`.

---

*MyOS x86_6
