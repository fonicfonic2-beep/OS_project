; ============================================================================
;  MyOS x86_64 boot stub (NASM, intel syntax, elf64 output)
;  Multiboot2 compliant -> GRUB2 loads us in 32-bit protected mode.
;  Kernel is linked at higher-half virtual address KERNEL_VMA.
;  Before paging is on we must use PHYSICAL addresses = symbol - KERNEL_VMA.
;  We build a minimal PML4: identity-map first 2MB (so this code keeps
;  running) + map same 2MB at KERNEL_VMA, enable long mode, then far-jump
;  to the higher-half 64-bit entry and call kernel_main(magic, mbi).
; ============================================================================

KERNEL_VMA equ 0xFFFFFFFF80000000

global _start
extern kernel_main           ; void kernel_main(uint32_t magic, void* mbi)

MB2_MAGIC     equ 0xE85250D6
MB2_ARCH_I386 equ 0
MB2_BOOT_MAGIC equ 0x36D76289

section .multiboot
align 8
mb2_header_start:
    dd MB2_MAGIC
    dd MB2_ARCH_I386
    dd (mb2_header_end - mb2_header_start)
    dd -(MB2_MAGIC + MB2_ARCH_I386 + (mb2_header_end - mb2_header_start))

    ; Framebuffer request tag: 1024x768x32 (GRUB picks closest available)
    align 8
    dw 5                      ; type = framebuffer
    dw 0                      ; flags
    dd 20                     ; size
    dd 1024                   ; width
    dd 768                    ; height
    dd 32                     ; depth

    ; Module align tag (align multiboot modules on page boundary)
    align 8
    dw 6
    dw 0
    dd 8

    ; End tag
    align 8
    dw 0
    dw 0
    dd 8
mb2_header_end:

; ---------------------------------------------------------------------------
; 32-bit entry point (runs at physical address, identity mapped)
; ---------------------------------------------------------------------------
section .text
bits 32
_start:
    cli
    ; Temporary stack: physical address of boot_stack_top
    mov esp, boot_stack_top - KERNEL_VMA

    ; Save magic and multiboot info pointer to memory (physical addresses)
    mov [saved_magic - KERNEL_VMA], eax
    mov [saved_mbi - KERNEL_VMA], ebx

    ; Accept multiboot2 (0x36D76289) or multiboot1 (0x2BADB002) magic
    cmp eax, MB2_BOOT_MAGIC
    je .mb_ok
    cmp eax, 0x2BADB002
    je .mb_ok
    mov dword [0xB8000], 0x0F450F45   ; "EE" = unknown bootloader
.hang:
    hlt
    jmp .hang

.mb_ok:
    ; Zero the four temporary page tables (physical addresses)
    mov edi, pml4 - KERNEL_VMA
    mov ecx, (4096 * 4) / 4
    xor eax, eax
    rep stosd

    ; PML4[0] -> PDPT_low (identity map, covers 0x00000000...)
    mov eax, pdpt_low - KERNEL_VMA
    or eax, 0b11                       ; present + writable
    mov [pml4 - KERNEL_VMA + 0], eax

    ; PML4[511] -> PDPT_high (higher half, covers 0xFFFF800000000000...)
    mov eax, pdpt_high - KERNEL_VMA
    or eax, 0b11
    mov [pml4 - KERNEL_VMA + 511*8], eax

    ; PDPT_low[0] -> PD_low
    mov eax, pd_low - KERNEL_VMA
    or eax, 0b11
    mov [pdpt_low - KERNEL_VMA + 0], eax

    ; PDPT_high[510] -> PD_high  (0xFFFFFFFF80000000 -> PDPT index 510)
    mov eax, pd_high - KERNEL_VMA
    or eax, 0b11
    mov [pdpt_high - KERNEL_VMA + 510*8], eax

    ; PD_low[0] = 2MB huge page at physical 0 (present + rw + PS)
    mov dword [pd_low - KERNEL_VMA + 0], 0x83
    mov dword [pd_low - KERNEL_VMA + 4], 0

    ; PD_high[0] = 2MB huge page at physical 0 mapped at high virtual
    mov dword [pd_high - KERNEL_VMA + 0], 0x83
    mov dword [pd_high - KERNEL_VMA + 4], 0

    ; CR3 = physical address of PML4
    mov eax, pml4 - KERNEL_VMA
    mov cr3, eax

    ; Enable PAE (CR4 bit 5)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Enable long mode: EFER.LME (MSR 0xC0000080, bit 8)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Enable paging (CR0.PG bit 31) + write protect (CR0.WP bit 16)
    mov eax, cr0
    or eax, (1 << 31) | (1 << 16)
    mov cr0, eax

    ; Load temporary 64-bit GDT (pointer lives at high virt, which is now mapped)
    lgdt [gdt64_pointer - KERNEL_VMA]
    ; Far jump to 64-bit higher-half entry. long_mode_start is a high virtual
    ; symbol; the jump lands in the higher-half mapping.
    jmp 0x08:long_mode_start

; ---------------------------------------------------------------------------
; 64-bit higher-half code
; ---------------------------------------------------------------------------
bits 64
long_mode_start:
    ; Reload data segments with 0x10
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Switch stack to higher-half virtual address
    mov rsp, boot_stack_top

    ; Zero BSS (linker symbols are virtual now)
    extern _bss_start, _bss_end
    mov rdi, _bss_start
    mov rcx, _bss_end
    sub rcx, rdi
    xor eax, eax
    rep stosb

    ; Arguments: rdi = magic, rsi = multiboot info pointer (physical -> convert to virt)
    mov edi, [saved_magic]
    mov rax, [saved_mbi]
    add rax, KERNEL_VMA           ; mbi is in low memory -> higher-half virt
    mov rsi, rax

    call kernel_main

.hang64:
    cli
    hlt
    jmp .hang64

; ---------------------------------------------------------------------------
; Temporary 64-bit GDT (kernel reloads its own later)
; ---------------------------------------------------------------------------
section .data
align 16
gdt64:
    dq 0                                      ; 0x00 null
    dq 0x00AF9A000000FFFF                     ; 0x08 code64, DPL0, present
    dq 0x00AF92000000FFFF                     ; 0x10 data64, DPL0, present
gdt64_pointer:
    dw gdt64_pointer - gdt64 - 1
    dq gdt64                                  ; virtual address (higher half)

saved_magic: dd 0
saved_mbi:   dq 0

; ---------------------------------------------------------------------------
; Temporary page tables + boot stack (in .bss, page aligned)
; ---------------------------------------------------------------------------
section .bss
align 4096
pml4:        resb 4096
pdpt_low:    resb 4096
pdpt_high:   resb 4096
pd_low:      resb 4096
pd_high:     resb 4096

align 16
boot_stack_bottom: resb 16384   ; 16 KB temporary stack
boot_stack_top:
