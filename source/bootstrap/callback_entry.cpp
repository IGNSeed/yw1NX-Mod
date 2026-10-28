// 新規callbackのOriginal呼出しBLを先に準備し、その後でMainのBLを公開する。
#if !defined(__aarch64__)
#error "The callback entry requires AArch64"
#endif

asm(R"(
.section .text.yw1_callback_entry, "ax", %progbits
.balign 4096
.global yw1_callback_entry
.hidden yw1_callback_entry
.type yw1_callback_entry, %function
yw1_callback_entry:
    stp x29, x30, [sp, #-48]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    stp x0, x1, [sp, #32]
    bl yw1_canary_enter
    ldp x0, x1, [sp, #32]
.global yw1_callback_original_call
.hidden yw1_callback_original_call
.type yw1_callback_original_call, %function
yw1_callback_original_call:
    .inst 0x94000000
.size yw1_callback_original_call, 4
    mov w19, w0
    bl yw1_canary_return
    mov w0, w19
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #48
    ret
.size yw1_callback_entry, .-yw1_callback_entry
)");
