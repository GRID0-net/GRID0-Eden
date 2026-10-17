/* SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project */
/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.global _start
_start:
    b main
.org 0x80
main:
    mov x2, #4096
1:
    sub x2, x2, #1
    cbnz x2, 1b

    mrs x19, tpidr_el0
    mov x3, #0x1357
    msr tpidr_el0, x3
    mrs x4, tpidr_el0
    cmp x4, x3
    b.ne failed
    msr tpidr_el0, x19
    mrs x20, tpidrro_el0
    mrs x21, cntpct_el0
    mrs x22, cntfrq_el0
    cbz x22, failed

    sub sp, sp, #32
    mov x5, #0x1234
    str x5, [sp]
    ldr x6, [sp]
    cmp x5, x6
    b.ne failed
    mov x0, sp
2:
    ldaxr x6, [x0]
    add x6, x6, #1
    stlxr w7, x6, [x0]
    cbnz w7, 2b
    ldr x6, [sp]
    mov x5, #0x1235
    cmp x5, x6
    b.ne failed
    add sp, sp, #32

    adr x9, constant
    ldr x10, [x9]
    mov x11, #0x4567
    cmp x10, x11
    b.ne failed
    adr x9, mutable_data
    str x10, [x9]
    ldr x12, [x9]
    cmp x12, x11
    b.ne failed

    adr x0, passed_message
    mov x1, #passed_length
    svc #0x27
    svc #0x7
    b .
failed:
    adr x0, failed_message
    mov x1, #failed_length
    svc #0x27
    svc #0x7
    b .
passed_message:
    .ascii "GRID0 NCE SMOKE PASS\n"
.set passed_length, . - passed_message
failed_message:
    .ascii "GRID0 NCE SMOKE FAIL\n"
.set failed_length, . - failed_message

/* Deliberately share a 16K host page between RX, R, RW and unmapped guest pages. */
.org 0x5000
constant:
    .quad 0x4567
.org 0x6000
mutable_data:
    .quad 0
.org 0x7000
