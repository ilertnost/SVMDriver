.section __TEXT,__text,aapcl
.globl svm_execute_vmrun

.p2align 3
svm_execute_vmrun:
    # Сохраняем все регистры общего назначения хоста в стек
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

    # Сохраняем RSP хоста в переменную, переданную первым аргументом (rdi)
    movq %rsp, (%rdi)

    # Загружаем физ. адрес VMCB в RAX (второй аргумент - rsi)
    movq %rsi, %rax

    # 1. VMSAVE (сохраняем FS/GS/TR/LDTR/MSRs)
    # Используем RAX как адрес для VMSAVE (но нам нужен отдельный save_pa)
    # Внимание: вызывающий должен передать host_pa в rdi и guest_pa в rsi.
    # Переделаем сигнатуру: rdi = host_pa, rsi = guest_pa, rdx = backup_rsp_ptr
    
    # Чтобы не путаться, перепишем логику:
    # rdi = pointer to host_rsp_backup
    # rsi = guest_pa
    # rdx = host_save_pa
    
    # 1. VMSAVE хост-состояние в host_save_pa
    movq %rdx, %rax
    .byte 0x0F, 0x01, 0xDB  # vmsave %rax
    
    # 2. VMRUN запуск гостя
    movq %rsi, %rax
    .byte 0x0F, 0x01, 0x0D8 # vmrun %rax
    
    # 3. VMLOAD восстановление хост-состояния
    movq %rdx, %rax
    .byte 0x0F, 0x01, 0x0DA # vmload %rax
    
    # 4. STGI - включаем прерывания
    stgi
    
    # 5. Восстанавливаем RSP хоста
    movq (%rdi), %rsp
    
    # 6. Восстанавливаем регистры в обратном порядке
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
