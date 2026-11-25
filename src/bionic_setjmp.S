/* bionic_setjmp.S -- ABI-safe setjmp/longjmp/sigsetjmp/siglongjmp for Bionic ARM32.
 *
 * Bionic jmp_buf is 64 ints (256 bytes); glibc jmp_buf is larger (~392 bytes).
 * Storing callee-saved core + VFP registers inside the first 104 bytes avoids
 * stack corruption when Bionic code allocates jmp_buf on the stack.
 */
#ifdef __arm__
    .syntax unified
    .arch armv7-a
    .fpu vfpv3
    .arm
    .align 2

    .global bionic_setjmp
    .type bionic_setjmp, %function
bionic_setjmp:
    mov     r2, sp
    mov     r3, lr
    stmia   r0!, {r4-r11}
    stmia   r0!, {r2, r3}
    vstmia  r0!, {d8-d15}
    mov     r0, #0
    bx      lr
    .size bionic_setjmp, .-bionic_setjmp

    .global bionic_longjmp
    .type bionic_longjmp, %function
bionic_longjmp:
    cmp     r1, #0
    moveq   r1, #1
    ldmia   r0!, {r4-r11}
    ldmia   r0!, {r2, r3}
    mov     sp, r2
    mov     lr, r3
    vldmia  r0!, {d8-d15}
    mov     r0, r1
    bx      lr
    .size bionic_longjmp, .-bionic_longjmp

    .global bionic_sigsetjmp
    .type bionic_sigsetjmp, %function
bionic_sigsetjmp:
    str     r1, [r0, #252]
    cmp     r1, #0
    beq     bionic_setjmp
    push    {r0, lr}
    add     r0, r0, #248
    bl      bionic_save_sigmask
    pop     {r0, lr}
    b       bionic_setjmp
    .size bionic_sigsetjmp, .-bionic_sigsetjmp

    .global bionic_siglongjmp
    .type bionic_siglongjmp, %function
bionic_siglongjmp:
    ldr     r2, [r0, #252]
    cmp     r2, #0
    beq     bionic_longjmp
    push    {r0, r1, lr}
    ldr     r0, [r0, #248]
    bl      bionic_restore_sigmask
    pop     {r0, r1, lr}
    b       bionic_longjmp
    .size bionic_siglongjmp, .-bionic_siglongjmp

    .section .note.GNU-stack,"",%progbits
#endif
