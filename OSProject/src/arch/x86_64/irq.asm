; irq.asm — x86_64 IRQ stub routines (NASM, intel syntax, elf64)
;
; 16 stubs irq0..irq15. PIC is remapped so IRQ n fires on vector 32+n.
; Hardware never pushes an error code for IRQs, so we push a dummy 0,
; then the vector number, then jump to the common stub.

        bits 64
        default rel
        section .text

        extern irq_common_handler

; irq n -> vector 32 + n
%macro IRQ_STUB 2
        global irq%1
irq%1:
        push  qword 0          ; dummy error code
        push  qword %2         ; vector number (32 + irq)
        jmp   irq_common_stub
%endmacro

        IRQ_STUB 0,  32
        IRQ_STUB 1,  33
        IRQ_STUB 2,  34
        IRQ_STUB 3,  35
        IRQ_STUB 4,  36
        IRQ_STUB 5,  37
        IRQ_STUB 6,  38
        IRQ_STUB 7,  39
        IRQ_STUB 8,  40
        IRQ_STUB 9,  41
        IRQ_STUB 10, 42
        IRQ_STUB 11, 43
        IRQ_STUB 12, 44
        IRQ_STUB 13, 45
        IRQ_STUB 14, 46
        IRQ_STUB 15, 47

; Same register-save layout as isr_common_stub (registers_t).
irq_common_stub:
        push  r15
        push  r14
        push  r13
        push  r12
        push  r11
        push  r10
        push  r9
        push  r8
        push  rbp
        push  rdi
        push  rsi
        push  rdx
        push  rcx
        push  rbx
        push  rax

        mov   rdi, rsp         ; arg1 = registers_t*
        call  irq_common_handler

        pop   rax
        pop   rbx
        pop   rcx
        pop   rdx
        pop   rsi
        pop   rdi
        pop   rbp
        pop   r8
        pop   r9
        pop   r10
        pop   r11
        pop   r12
        pop   r13
        pop   r14
        pop   r15

        add   rsp, 16          ; drop vector number and dummy err code
        iretq
