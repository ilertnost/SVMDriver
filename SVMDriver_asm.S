.text
.globl _svm_execute_vmrun

.p2align 3
_svm_execute_vmrun:
    // 1. Save all host GPRs
    pushq   %rax
    pushq   %rbx
    pushq   %rcx
    pushq   %rdx
    pushq   %rsi
    pushq   %rdi
    pushq   %rbp
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    // 2. Save host_pa in r15 before rdmsr clobbers rdx
    movq    0x58(%rsp), %r15      // r15 = host_pa

    // 3. Enable SVME on current core (thread may have migrated)
    movl    $0xC0000080, %ecx     // MSR_EFER
    rdmsr
    orl     $0x1000, %eax         // EFER_SVME
    wrmsr

    // 4. Set VM_HSAVE_PA on current core (per-core MSR!)
    movq    %r15, %rdx            // rdx = host_pa
    shrq    $32, %rdx             // rdx = high 32 bits
    movq    %r15, %rax            // rax = host_pa (low 32 in eax)
    movl    $0xC0010117, %ecx     // MSR_VM_HSAVE_PA
    wrmsr

    // 5. VMSAVE host state to host_save_pa
    movq    %r15, %rax
    .byte 0x0F, 0x01, 0xD8       // vmsave %rax

    // 5b. Clear Busy bit in saved host TR so VMLOAD at exit doesn't #GP.
    //     TR attrib is at state-block offset 0x090 + 2 = 0x092.
    //     4th arg (host_vaddr) is at RSP+0x60 (saved rcx in push sequence).
    movq    0x60(%rsp), %rbx      // rbx = host_vaddr
    andb    $~0x02, 0x92(%rbx)    // clear bit 1 (Busy) in TR.attrib

    // 6. Disable host interrupts before VMRUN
    cli

    // 7. Push host_pa on stack, save RSP backup
    pushq   %r15
    movq    %rsp, (%rdi)

    // 8. VMRUN - enter guest (guest_pa in rsi)
    movq    %rsi, %rax
    .byte 0x0F, 0x01, 0xD8       // vmrun %rax

    // ==========================================
    // VMEXIT - back in host context
    // ==========================================

    // 9. Restore RSP (pops the pushq %r15 above)
    movq    (%rdi), %rsp

    // 10. Pop host_pa, VMLOAD to restore host state (GS.base etc.)
    popq    %rax
    .byte 0x0F, 0x01, 0xDA       // vmload %rax

    // 11. Re-enable host interrupts
    sti

    // 12. Restore all host GPRs
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rbp
    popq    %rdi
    popq    %rsi
    popq    %rdx
    popq    %rcx
    popq    %rbx
    popq    %rax

    ret
