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

    # 2. Бэкапим RSP хоста в переменную памяти (адрес в RDI)
    movq %rsp, (%rdi)

    # 3. VMSAVE хост-состояние в host_save_pa (адрес в RDX)
    movq %rdx, %rax
    .byte 0x0F, 0x01, 0xD8  # vmsave %rax
    
    # 4. КРИТИЧНО: Сохраняем host_pa в стек ПЕРЕД VMRUN, 
    # так как VMRUN затрёт RDX гостевым мусором.
    pushq %rdx

    # 5. VMRUN запуск гостя (адрес в RSI)
    movq %rsi, %rax
    .byte 0x0F, 0x01, 0xD8 # vmrun %rax
    
    # ==========================================
    # ---- ПРОИЗОШЕЛ #VMEXIT (Мы в macOS) ----
    # ==========================================

    # 6. ПЕРВЫМ ДЕЛОМ: восстанавливаем RSP хоста
    movq (%rdi), %rsp

    # 7. Сразу достаём host_pa из стека и восстанавливаем GS.base / MSRs
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
