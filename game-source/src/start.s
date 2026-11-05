.syntax unified
.cpu arm7tdmi
.section .gbaheader,"ax",%progbits
.arm
.global _start
_start:
    b reset
    .space 188
.section .text.start,"ax",%progbits
.arm
reset:
    mov r0, #0x12
    msr cpsr_c, r0
    ldr sp, =0x03007fa0
    mov r0, #0x1f
    msr cpsr_c, r0
    /* Keep672 bytes for the BIOS trampoline and nested audio IRQ calls.
     * The packed-music path needs168 bytes; the old160-byte gap overlapped
     * main's frame timing state. Main retains7424 bytes above fast code. */
    ldr sp, =0x03007d00
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:  cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    ldr r0, =__data_load
    ldr r1, =__data_start
    ldr r2, =__data_end
2:  cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 2b
    ldr r0, =__fast_load
    ldr r1, =__fast_start
    ldr r2, =__fast_end
4:  cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 4b
    ldr r0, =main
    bx r0
3:  b 3b

/* Zig0.13 compiler-rt's weak divmod adapters return with LDR pc,[sp],#4.
 * ARM7TDMI does not interwork on that instruction: a Thumb caller resumes
 * as ARM. Preserve the ABI's8-byte stack alignment and r0/r1 results, then
 * return with BX. Keep these cold adapters in ROM, not the IWRAM budget. */
.section .text.ui,"ax",%progbits
.arm
.balign 4
.global __aeabi_uidivmod
.type __aeabi_uidivmod,%function
__aeabi_uidivmod:
    push {lr}
    sub sp, sp, #4
    mov r2, sp
    bl __udivmodsi4
    ldr r1, [sp]
    add sp, sp, #4
    pop {lr}
    bx lr
.size __aeabi_uidivmod, .-__aeabi_uidivmod
.global __aeabi_idivmod
.type __aeabi_idivmod,%function
__aeabi_idivmod:
    push {lr}
    sub sp, sp, #4
    mov r2, sp
    bl __divmodsi4
    ldr r1, [sp]
    add sp, sp, #4
    pop {lr}
    bx lr
.size __aeabi_idivmod, .-__aeabi_idivmod
