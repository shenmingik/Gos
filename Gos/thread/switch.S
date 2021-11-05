[bits 32]
section .text
global switch_to
switch_to:
    ;栈中此处为返回地址
    push esi
    push edi
    push ebx
    push ebp

    mov eax,[esp+20]    ;压入两个参数，第一个是current，第二个是next。由于有4个寄存器+返回地址
                        ;所以current的地址是esp+20
    mov [eax],esp       ;保存栈顶指针，也就是task_struct的self_kstack字段地址，即起始地址
                        ;其实也就是备份当前线程环境

    ;切换线程
    mov eax,[esp+24]    ;得到next
    mov esp,[eax]       ;切换到self_kstack

    ;以下这些寄存器都是next线程的寄存器，非current。其是next被换下处理器保存的
    pop ebp
    pop ebx
    pop edi
    pop esi
    ret
