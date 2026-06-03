.text
.globl _svm_execute_vmrun

.p2align 3
_svm_execute_vmrun:
    # 1. Сохраняем все регистры общего назначения хоста в стек
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    # 2. Включаем SVME на текущем ядре (thread мог мигрировать)
    movl    $0xC0000080, %ecx
    rdmsr
    orl     $0x1000, %eax
    wrmsr
    movq    0x58(%rsp), %rdx       # восстанавливаем host_pa (rdx сброшен rdmsr)

    # 3. VMSAVE хост-состояние в host_save_pa (адрес в RDX)
    movq %rdx, %rax
    .byte 0x0F, 0x01, 0xD8  # vmsave %rax
    
    # 4. Сохраняем host_pa в стек ПЕРЕД VMRUN
    pushq %rdx
    # Бэкапим RSP ПОСЛЕ pushq, чтобы на VMEXIT popq %rax достал host_pa
    movq %rsp, (%rdi)

    # 5. VMRUN запуск гостя (адрес в RSI)
    movq %rsi, %rax
    .byte 0x0F, 0x01, 0xD8 # vmrun %rax
    
    # ==========================================
    # ---- ПРОИЗОШЕЛ #VMEXIT (Мы в macOS) ----
    # ==========================================

    # 6. Восстанавливаем RSP (теперь указывает на host_pa в стеке)
    movq (%rdi), %rsp

    # 7. Достаём host_pa из стека и восстанавливаем GS.base / MSRs
    popq %rax
    .byte 0x0F, 0x01, 0xDA # vmload %rax (Восстанавливаем GS.base ядра!)
    
    # 8. Оживляем прерывания
    stgi
    
    # 9. Теперь безопасно восстанавливаем остальные регистры
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    ret
