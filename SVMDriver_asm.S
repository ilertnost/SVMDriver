.globl svm_execute_vmrun

.p2align 3
svm_execute_vmrun:
    # 1. Сохраняем чистый RSP хоста до всех пушей
    movq %rsp, (%rdi)

    # 2. Сохраняем все регистры общего назначения хоста в стек
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

    # Загружаем физ. адрес VMCB в RAX (второй аргумент - rsi)
    movq %rsi, %rax

    # 3. VMSAVE (сохраняем FS/GS/TR/LDTR/MSRs)
    # Используем RDX как адрес для VMSAVE (третий аргумент - rdx)
    movq %rdx, %rax
    .byte 0x0F, 0x01, 0xD8  # vmsave %rax
    
    # 4. VMRUN запуск гостя
    movq %rsi, %rax
    .byte 0x0F, 0x01, 0xD8 # vmrun %rax
    
    # 5. VMLOAD восстановление хост-состояния
    movq %rdx, %rax
    .byte 0x0F, 0x01, 0xDA # vmload %rax
    
    # 6. STGI - включаем прерывания
    stgi
    
    # 7. Восстанавливаем регистры в строго обратном порядке
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

    # 8. Восстанавливаем чистый RSP хоста
    movq (%rdi), %rsp

    ret
