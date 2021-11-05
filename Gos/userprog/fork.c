#include "fork.h"
#include "process.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "thread.h"
#include "string.h"
#include "file.h"

extern void intr_exit(void);

/**
 * @brief 将父进程的pcb拷贝给子进程
 * 
 * @param child_thread 子进程指针
 * @param parent_thread 父进程指针
 * @return int32_t 返回0
 */
static int32_t copy_pcb_vaddrbitmap_stack0(struct task_struct *child_thread, struct task_struct *parent_thread)
{
    //# 1.复制pcb所在的整个页，包含pcb以及0特权级栈，里面包含了返回地址
    memcpy(child_thread, parent_thread, PG_SIZE);

    //下面修改子进程自己的信息
    child_thread->pid = fork_pid();
    child_thread->elapsed_ticks = 0;
    child_thread->task_status = TASK_READY;
    child_thread->ticks = child_thread->priority;
    child_thread->parent_pid = parent_thread->pid;
    child_thread->general_tag.prev = child_thread->general_tag.next = NULL;
    child_thread->all_list_tag.prev = child_thread->all_list_tag.next = NULL;

    block_desc_init(child_thread->u_block_desc);

    //# 2.复制父进程虚拟地址池的位图
    uint32_t bitmap_page_count = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    //此时子进程的child_thread->userprog_vaddr.vaddr_bitmap.bits指向的还是父进程的位图地址，需要自己也整一个
    void *vaddr_bitmap = get_kernel_pages(bitmap_page_count);
    memcpy(vaddr_bitmap, child_thread->userprog_vaddr.vaddr_bitmap.bits, bitmap_page_count * PG_SIZE);
    ASSERT(strlen(child_thread->name) < 11);
    strcat(child_thread->name, "_fork"); //子进程为父进程名称的拷贝
    return 0;
}

/**
 * @brief 复制子进程的进程体以及用户栈
 * 
 * @param child_thread 子进程
 * @param parent_thraed 父进程
 * @param buf_page 页缓冲,此为内核页
 */
static void copy_body_stack3(struct task_struct *child_thread, struct task_struct *parent_thraed, void *buf_page)
{
    //获取父进程的位图信息
    uint8_t *vaddr_btmp = parent_thraed->userprog_vaddr.vaddr_bitmap.bits;
    uint32_t btmp_bytes_len = parent_thraed->userprog_vaddr.vaddr_bitmap.btmp_bytes_len;

    uint32_t vaddr_start = parent_thraed->userprog_vaddr.vaddr_start;
    uint32_t idx_byte = 0;
    uint32_t idx_bit = 0;
    uint32_t prog_vaddr = 0;

    //* 父进程的用户空间中查找已有数据的页,之后进行每个位拷贝
    while (idx_byte < btmp_bytes_len)
    {
        if (vaddr_btmp[idx_byte])
        {
            //此时这个字节中的位图代表的空间有数据
            //字节有8位，判断哪一位有数据
            idx_bit = 0;
            while (idx_bit < 8)
            {
                if ((BITMAP_MASK << idx_bit) & vaddr_btmp[idx_byte])
                {
                    //得到这个位代表的内存空间地址
                    prog_vaddr = (idx_byte * 8 + idx_bit) * PG_SIZE + vaddr_start;
                    //# 1.先把这个数据拷贝到内核空间中做中转，之后拷贝到子进程自己的空间
                    memcpy(buf_page, (void *)prog_vaddr, PG_SIZE);

                    //# 2.将页表切换到子进程，避免在父进程中申请内存
                    page_dir_activate(child_thread);
                    //# 3.申请虚拟地址
                    get_one_page_without_operate_vaddr_bitmap(PF_USER, prog_vaddr);

                    //# 4.将内核缓冲区中父进程的数据复制到子进程的用户空间之中
                    memcpy((void *)prog_vaddr, buf_page, PG_SIZE);

                    //# 5.恢复父进程的页表
                    page_dir_activate(parent_thraed);
                }
                idx_bit++; //继续判断下一个位
            }
        }
        idx_byte++; //继续判断下一个字节
    }
}

/**
 * @brief 为子进程构建线程栈thread_stack和修改返回值为intr_exit，直接从中断返回
 * 
 * @param child_thread 子进程
 * @return uint32_t 返回0
 */
static uint32_t build_child_stack(struct task_struct *child_thread)
{
    //# 1.使子进程pid返回值为0
    //获取子进程0级栈的栈顶
    struct intr_stack *intr_stack0 = (struct intr_stack *)((uint32_t)child_thread + PG_SIZE - sizeof(struct intr_stack));
    intr_stack0->eax = 0;

    //# 2.为switch_to构建struct thread_stack
    uint32_t *ret_addr_in_thread_stack = (uint32_t *)intr_stack0 - 1;

    //得到esi、edi、ebx指针
    uint32_t *esi_ptr_in_thread_stack = (uint32_t *)intr_stack0 - 2;
    uint32_t *edi_ptr_in_thread_stack = (uint32_t *)intr_stack0 - 3;
    uint32_t *ebx_ptr_in_thread_stack = (uint32_t *)intr_stack0 - 4;

    //ebp在thread_stack中的地址便是当时的esp,所以ebp=esp
    uint32_t *ebp_ptr_in_thread_stack = (uint32_t *)intr_stack0 - 5;

    //直接从中断返回
    *ret_addr_in_thread_stack = (uint32_t)intr_exit;

    *ebp_ptr_in_thread_stack = *ebx_ptr_in_thread_stack = *edi_ptr_in_thread_stack = *esi_ptr_in_thread_stack = 0;

    //# 3.把构建thread_stack的栈顶作为switch_to恢复数据时的栈顶
    child_thread->self_kstack = ebp_ptr_in_thread_stack;
    return 0;
}

/**
 * @brief 更新全局文件符表中inode的打开数量
 * 
 * @param thread 线程指针
 * @note 起始也就是判断当前thread是否打开了这个文件
 */
static void update_inode_open_cnts(struct task_struct *thread)
{
    int32_t local_fd = 3;
    int32_t global_fd = 0;
    while (local_fd < MAX_FILES_OPEN_PER_PROC)
    {
        global_fd = thread->fd_table[local_fd];
        ASSERT(global_fd < MAX_FILE_OPEN);
        if (global_fd != -1)
        {
            file_table[global_fd].fd_inode->inode_open_cnts++;
        }
        local_fd++;
    }
}

/**
 * @brief 给子进程拷贝父进程的资源
 * 
 * @param child_thread 子进程指针
 * @param parent_thread 父进程指针
 * @return uint32_t 成功返回0，失败返回-1 
 */
static uint32_t copy_process(struct task_struct *child_thread, struct task_struct *parent_thread)
{
    //内核缓冲区
    void *buf_page = get_kernel_pages(1);
    if (buf_page == NULL)
    {
        return -1;
    }

    //# 1.复制父进程的pcb、虚拟地址位图、内核栈
    if (copy_pcb_vaddrbitmap_stack0(child_thread, parent_thread) == -1)
    {
        //这种情况基本没有
        return -1;
    }

    //# 2.为子进程创建页表，此页表仅包括内核空间
    child_thread->pgdir = create_page_dir();
    if (child_thread->pgdir == NULL)
    {
        return -1;
    }

    //# 3.复制父进程的进程体给子进程
    copy_body_stack3(child_thread, parent_thread, buf_page);

    //# 4.构建子进程thread_stack和修改返回值
    build_child_stack(child_thread);

    //# 5.更新文件的inode打开数
    update_inode_open_cnts(child_thread);
    mfree_page(PF_KERNEL, buf_page, 1);
    return 0;
}

/**
 * @brief fork子进程
 * 
 * @return pid_t 成功，父进程返回子进程的pid;失败返回-1
 */
pid_t sys_fork(void)
{
    struct task_struct *parent_thread = running_thread();
    //为进程创建pcb
    struct task_struct *child_thread = get_kernel_pages(1);
    if (child_thread == NULL)
    {
        return -1;
    }

    ASSERT(INTR_OFF == intr_get_status() && parent_thread->pgdir != NULL);
    if (copy_process(child_thread, parent_thread) == -1)
    {
        return -1;
    }

    //添加到就绪线程队列和所有线程队列
    ASSERT(!elem_find(&thread_ready_list, &child_thread->general_tag));
    list_append(&thread_ready_list, &child_thread->general_tag);
    ASSERT(!elem_find(&thread_all_list, &child_thread->all_list_tag));
    list_append(&thread_all_list, &child_thread->all_list_tag);
    return child_thread->pid; //返回子进程pid
}
