; isr.asm — x86_64 exception stub routines (NASM, intel syntax, elf64)
;
; Stack layout built by these stubs matches struct registers_t in isr.h
; (low address -> high address):
;   rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14,
;   r15, int_no, err_code, rip, cs, rflags, rsp, ss
;
; Exceptions that push a hardware error code: 8, 10, 11, 12, 13, 14, 17, 21.
; All others push a dummy 0 so the stack layout is uniform.

        bits 64
        default rel
        section .text

; C handler called with rdi = registers_t*
        extern isr_common_handler

; ---------- macros ----------

; Exception without hardware error code: push dummy err, then vector number.
%macro ISR_NOERR 1
        global isr%1
isr%1:
        push  qword 0          ; dummy error code
        push  qword %1         ; interrupt number
        jmp   isr_common_stub
%endmacro

; Exception with hardware error code (already on stack): push vector number.
%macro ISR_ERR 1
        global isr%1
isr%1:
        push  qword %1         ; interrupt number (err code already pushed)
        jmp   isr_common_stub
%endmacro

; ---------- 32 exception stubs ----------
        ISR_NOERR 0            ; #DE  Divide Error
        ISR_NOERR 1            ; #DB  Debug
        ISR_NOERR 2            ; NMI
        ISR_NOERR 3            ; #BP  Breakpoint
        ISR_NOERR 4            ; #OF  Overflow
        ISR_NOERR 5            ; #BR  Bound Range Exceeded
        ISR_NOERR 6            ; #UD  Invalid Opcode
        ISR_NOERR 7            ; #NM  Device Not Available
        ISR_ERR   8            ; #DF  Double Fault            (err)
        ISR_NOERR 9            ; reserved (old coprocessor)
        ISR_ERR   10           ; #TS  Invalid TSS             (err)
        ISR_ERR   11           ; #NP  Segment Not Present     (err)
        ISR_ERR   12           ; #SS  Stack-Segment Fault     (err)
        ISR_ERR   13           ; #GP  General Protection      (err)
        ISR_ERR   14           ; #PF  Page Fault              (err)
        ISR_NOERR 15           ; reserved
        ISR_NOERR 16           ; #MF  x87 Floating-Point
        ISR_ERR   17           ; #AC  Alignment Check         (err)
        ISR_NOERR 18           ; #MC  Machine Check
        ISR_NOERR 19           ; #XM/#XF SIMD Floating-Point
        ISR_NOERR 20           ; #VE  Virtualization
        ISR_ERR   21           ; #CP  Control Protection      (err)
        ISR_NOERR 22
        ISR_NOERR 23
        ISR_NOERR 24
        ISR_NOERR 25
        ISR_NOERR 26
        ISR_NOERR 27
        ISR_NOERR 28
        ISR_NOERR 29
        ISR_NOERR 30
        ISR_NOERR 31

; ---------- syscall stub (int 0x80) ----------
; No hardware error code; push dummy 0 then vector 0x80.
        global syscall_isr
syscall_isr:
        push  qword 0
        push  qword 0x80
        jmp   isr_common_stub

; ---------- common stub ----------
; On entry: [rsp] = int_no, [rsp+8] = err_code, then hardware iret frame.
; Push GPRs so that rsp points at rax (first field of registers_t).
isr_common_stub:
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
        call  isr_common_handler

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

        add   rsp, 16          ; drop int_no and err_code
        iretq
