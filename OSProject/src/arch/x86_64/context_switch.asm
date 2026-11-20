; context_switch.asm — thread context switch and user-mode entry
; (NASM, intel syntax, elf64)
;
;   void switch_context(registers_t** old_ctx, registers_t* new_ctx)
;       rdi = old_ctx (pointer to where the old context pointer is stored)
;       rsi = new_ctx (pointer to saved context on the new kernel stack)
;
;   void enter_user_mode(uintptr_t entry, uintptr_t user_stack)
;       rdi = user entry point (rip), rsi = user stack top (rsp)
;
; The syscall stub (int 0x80) lives in isr.asm as `syscall_isr`.

        bits 64
        default rel
        section .text

; Selectors with RPL3 for user mode (GDT index 3 code, index 4 data).
USER_CODE_SEL equ 0x18 | 3      ; 0x1B
USER_DATA_SEL equ 0x20 | 3      ; 0x23
RFLAGS_IF     equ 0x202         ; IF=1 + reserved bit 1

; ----------------------------------------------------------------
; switch_context: save full GPR file on the old kernel stack in the
; same layout as registers_t (rax at lowest address), record the
; stack pointer in *old_ctx, then restore from new_ctx and return
; into the new thread.
; ----------------------------------------------------------------
        global switch_context
switch_context:
        ; Push in reverse struct order so rsp ends up pointing at rax.
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

        mov   [rdi], rsp       ; *old_ctx = current stack (registers_t*)
        mov   rsp, rsi         ; switch to new kernel stack / context

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

        ret                    ; pops the saved return address

; ----------------------------------------------------------------
; enter_user_mode: build an iretq frame on the kernel stack and
; return into ring 3 at `entry` with `user_stack` as RSP.
; ----------------------------------------------------------------
        global enter_user_mode
enter_user_mode:
        cli                    ; don't get interrupted mid-frame setup

        mov   rax, rdi         ; entry (rip)
        mov   rbx, rsi         ; user stack (rsp)

        ; Load user data selectors into segment registers.
        mov   rcx, USER_DATA_SEL
        mov   ds, cx
        mov   es, cx
        mov   fs, cx
        mov   gs, cx

        ; Zero scratch registers to avoid leaking kernel data.
        xor   rdx, rdx
        xor   rsi, rsi
        xor   rdi, rdi
        xor   r8,  r8
        xor   r9,  r9
        xor   r10, r10
        xor   r11, r11

        ; Build the iretq frame (high -> low pushed order).
        push  USER_DATA_SEL    ; ss
        push  rbx              ; rsp (user stack top)
        push  RFLAGS_IF        ; rflags
        push  USER_CODE_SEL    ; cs
        push  rax              ; rip (entry)

        xor   rax, rax
        xor   rbx, rbx
        xor   rcx, rcx
        xor   rbp, rbp
        xor   r12, r12
        xor   r13, r13
        xor   r14, r14
        xor   r15, r15

        iretq
