#include "process.h"
#include "thread.h"
#include "global.h"
#include "memory.h"
#include "debug.h"
#include "tss.h"
#include "console.h"
#include "string.h"
#include "interrupt.h"
#include "list.h"
#include "stdio.h"

#define PG_SIZE 4096
//此函数定义在kernel.S
extern void intr_exit(void);

/*
 * @brief 构建用户进程的初始上下文
 * @param 进程名称
 * @note 实际上是初始化中断栈，并将其作为进程的特权级3下的栈
 */
void start_process(void *filename_)
{
    void *function = filename_;

    struct task_struct *current_thread = running_thread();
    //由于在PCB中intr_stack和thread_stack是相邻的，self_kstack指向了thread_stack的栈顶，所以需要先定位到intr_stack
    current_thread->self_kstack += sizeof(struct thread_stack);

    struct intr_stack *proc_stack = (struct intr_stack *)current_thread->self_kstack;
    //初始化8个通用寄存器
    proc_stack->edi = 0;
    proc_stack->esi = 0;
    proc_stack->ebp = 0;
    proc_stack->esp_dummy = 0;
    proc_stack->ebx = 0;
    proc_stack->edx = 0;
    proc_stack->ecx = 0;
    proc_stack->eax = 0;

    proc_stack->gs = 0; //显存段寄存器gs初始化,为0便可
    proc_stack->ds = SELECTOR_U_DATA;
    proc_stack->es = SELECTOR_U_DATA;
    proc_stack->fs = SELECTOR_U_DATA;

    proc_stack->eip = function;
    proc_stack->cs = SELECTOR_U_CODE;
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
    //分配特权级3下的栈
    proc_stack->esp = (void *)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
    proc_stack->ss = SELECTOR_U_DATA;
    asm volatile("movl %0,%%esp; jmp intr_exit" ::"g"(proc_stack)
                 : "memory");
}

/*
 * @brief 激活页表
 * @param pthread 待激活的进程
 * @note 如果是内核线程默认地址是0x100000，用户进程就需要进行其虚拟地址到物理地址的转换获得物理地址
 */
void page_dir_activate(struct task_struct *pthread)
{
    uint32_t pagedir_phyaddr = 0x100000; //此为内核使用的页表的物理地址
    if (pthread->pgdir != NULL)
    {
        //代表其实是用户进程，获得其物理地址
        pagedir_phyaddr = addr_v2p((uint32_t)pthread->pgdir);
    }

    //重新激活页表
    asm volatile("movl %0,%%cr3" ::"r"(pagedir_phyaddr)
                 : "memory");
}

/*
 * @brief 激活线程或者进程的页表，更新tss中的esp0为进程的特权级0的栈
 * @param pthread 待激活的进程或者线程
 */
void process_activate(struct task_struct *pthread)
{
    ASSERT(pthread != NULL);
    page_dir_activate(pthread);

    if (pthread->pgdir)
    {
        //如果是用户进程，就涉及到特权级3->1那么代表着其实我们需要去获取tss中esp0的值作为进程在内核中的栈地址
        update_tss_esp(pthread);
    }
}

/*
 * @brief 创建页目录表，将当前页表的内核空间的pde复制
 * @return 成功返回页目录的虚拟地址，失败返回NULL
 */
uint32_t *create_page_dir(void)
{
    uint32_t *page_dir_vaddr = get_kernel_pages(1);
    if (page_dir_vaddr == NULL)
    {
        console_put_str("create_page_dir: get_kernel_page failed");
        return NULL;
    }

    //1.复制内核页目录表的镜像到用户进程页目录表768~1024的位置
    //0x300代表768，也就是内核空间的开始位置,总共256项，每项4字节，所以要复制1024个
    //0xfffff000 表示内核页目录表的基地址
    memcpy((uint32_t *)((uint32_t)page_dir_vaddr + 0x300 * 4), (uint32_t *)(0xfffff000 + 0x300 * 4), 1024);

    //2.更新页目录地址
    uint32_t new_page_dir_phyaddr = addr_v2p((uint32_t)page_dir_vaddr);
    //指向此页目录表的起始位置
    page_dir_vaddr[1023] = new_page_dir_phyaddr | PG_US_U | PG_RW_W | PG_P_1;
    return page_dir_vaddr;
}

/*
 * @brief 创建用户进程的位图信息
 * @param user_prog 待创建信息的进程地址
 */
void create_user_vaddr_bitmap(struct task_struct *user_prog)
{
    //代码段起始位置，也是linux程序入口地址
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;

    //得到用来表示内存所需要的位图需要几个页
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);

    //创建位图信息
    user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

/*
 * @brief 创建用户进程
 * @param filename 程序文件名称
 * @param name 进程的名字
 */
void create_process(void *filename, char *name)
{
    //分配内存进程实体
    struct task_struct *thread = get_kernel_pages(1);
    init_thread(thread, name, default_prio);
    //创建进程位图信息
    create_user_vaddr_bitmap(thread);
    //进程作为线程的执行函数
    thread_create(thread, start_process, filename);
    thread->pgdir = create_page_dir();
    block_desc_init(thread->u_block_desc);

    //加入线程就绪队列
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);

    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    intr_set_status(old_status);
}
