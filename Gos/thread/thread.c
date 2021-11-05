#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "print.h"
#include "process.h"
#include "sync.h"
#include "stdio.h"
#include "fs.h"
#include "file.h"
extern void init(void);

#define PG_SIZE 4096

struct task_struct *main_thread;     //主线程PCB
struct list thread_ready_list;       //就绪队列
struct list thread_all_list;         //所有任务队列
static struct list_elem *thread_tag; //用于保存队列中的线程节点

struct lock pid_lock;

struct task_struct *idle_thread; //idle线程,用于thread_ready_list队列中没有线程运行的时候，默认运行这个线程

/*
 * @brief 由当前线程current切换到下个线程next
 * @param current 当前线程PCB的起始地址
 * @param next 下一个线程PCB的起始地址
 */
extern void switch_to(struct task_struct *current, struct task_struct *next);

/*
 * @brief 给线程分配pid
 * @return 线程的pid
 */
static pid_t allocate_pid(void)
{
    static pid_t next_pid = 0;
    get_lock(&pid_lock);
    next_pid++;
    // put_str("next pid:");
    // put_int(next_pid);
    abandon_lock(&pid_lock);
    return next_pid;
}

/*
 * @brief 获取当前线程的PCB指针
 * @return 返回当前线程的地址
 * @note 每个线程占一页的空间大小，即4096字节，所以这里进行esp & 0xfffff000，便能得到起始地址
 */
struct task_struct *running_thread()
{
    uint32_t esp;
    asm("mov %%esp,%0"
        : "=g"(esp));
    return (struct task_struct *)(esp & 0xfffff000);
}

/*
 * @brief 执行传递过来的函数
 * @param function 函数的地址
 * @param func_arg 函数参数
 * @note 此时应该处于开中断状态，并避免后面时钟中断被频闭，无法调度其他线程
 */
static void kernel_thread(thread_func *function, void *func_arg)
{
    intr_enable();
    function(func_arg);
}

/*
 * @brief 初始化线程栈信息
 * @param pthread 待初始化的线程的地址
 * @param function 线程待执行函数信息
 * @param func_arg 函数所需参数
 */
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg)
{
    //预留中断使用栈的空间
    pthread->self_kstack -= sizeof(struct intr_stack);

    //预留线程栈空间
    pthread->self_kstack -= sizeof(struct thread_stack);

    //初始化线程栈信息
    struct thread_stack *kthread_stack = (struct thread_stack *)pthread->self_kstack;
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

/*
 * @brief 初始化线程基本信息
 * @param pthread 待初始化的线程地址
 * @param name 线程名称
 * @param pthread_priority 线程的优先级
 */
void init_thread(struct task_struct *pthread, char *name, int pthread_priority)
{
    memset(pthread, 0, sizeof(*pthread));
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);

    //增加一个判断，如果是main函数，则其运行状态一直是running
    if (pthread == main_thread)
    {
        pthread->task_status = TASK_RUNNING;
    }
    else
    {
        pthread->task_status = TASK_READY;
    }

    pthread->priority = pthread_priority;
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE);
    pthread->ticks = pthread_priority; //设置线程运行时间为线程的优先级，无疑优先级越高，运行时间越高
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;

    //初始化文件描述符信息
    pthread->fd_table[0] = 0; //标准输入
    pthread->fd_table[1] = 1; //标准输出
    pthread->fd_table[2] = 2; //标准错误
    //其余全部为-1
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC)
    {
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }

    pthread->cwd_inode_no = 0; //以根目录为默认工作路径
    pthread->parent_pid = -1;
    pthread->stack_magic = 0x20000314; //自定义的魔数，这里是我的生日
}

/*
 * @brief 创建一个优先级为priority并且名称为name的线程，并指定其执行函数和函数参数
 * @param name 线程名称
 * @param priority 线程优先级
 * @param function 待执行函数地址
 * @param func_arg 函数的参数
 */
struct task_struct *thread_start(char *name, int priority, thread_func function, void *func_arg)
{
    //申请一页的内核空间
    struct task_struct *thread = get_kernel_pages(1);
    init_thread(thread, name, priority);
    thread_create(thread, function, func_arg);

    //确保线程不在就绪队列中
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);

    //确保线程不在总队列中
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);

    return thread;
}

/*
 * @brief 初始化主线程
 */
static void make_main_thread(void)
{
    main_thread = running_thread();
    init_thread(main_thread, "main_thread", 31);

    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}

/*
 * @brief 线程调度器,调度下一个就绪线程执行
 * @note 必须处于关中断状态，不能被其他线程打断，原子操作
 */
void schedule()
{
    ASSERT(intr_get_status() == INTR_OFF);

    //得到当前线程的地址
    struct task_struct *current = running_thread();
    if (current->task_status == TASK_RUNNING)
    {
        //这种情况属于时间片到了，该轮转了
        //将其加入到就绪队列队尾就好啦
        ASSERT(!elem_find(&thread_ready_list, &current->general_tag));
        list_append(&thread_ready_list, &current->general_tag);

        current->ticks = current->priority; //重新赋予时间片
        current->task_status = TASK_READY;
    }
    else
    {
        //TODO 其他情况，之后再处理
    }

    if (list_empty(&thread_ready_list))
    {
        //如果没有待运行的线程就唤醒idle_thread线程，让它执行
        thread_unblock(idle_thread);
    }

    //从就绪队列出队一个元素
    ASSERT(!list_empty(&thread_ready_list));
    thread_tag = NULL;
    thread_tag = list_pop(&thread_ready_list);
    struct task_struct *next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->task_status = TASK_RUNNING;
    process_activate(next);
    switch_to(current, next);
}

/*
 * @brief 系统运行时运行的线程，一般就是阻塞线程
 */
static void idle(void *arg UNUSED)
{
    while (1)
    {
        //阻塞线程
        thread_block(TASK_BLOCKED);
        asm volatile("sti;hlt" ::
                         : "memory");
    }
}

/*
 * @brief 当前线程将自己阻塞，标志其状态为state
 * @param state 线程待设置的状态，必须为blocked、waiting以及hangding三者之一
 */
void thread_block(enum task_status state)
{
    //只有处于blocked、waiting以及hangding状态才不会被调度
    ASSERT(((state == TASK_BLOCKED) || (state == TASK_WAITING) || (state == TASK_HANGING)));

    //关中断
    enum intr_status old_status = intr_disable();
    struct task_struct *current_thread = running_thread();
    current_thread->task_status = state;
    schedule();
    intr_set_status(old_status); //这个由于此线程已经处于不可执行态了，只能等其再次被调度才能执行
}

/*
 * @brief 将线程pthread解锁
 * @param pthread 待解锁的线程
 */
void thread_unblock(struct task_struct *pthread)
{
    //先关中断
    enum intr_status old_status = intr_disable();
    ASSERT(((pthread->task_status == TASK_BLOCKED) || (pthread->task_status == TASK_WAITING) || (pthread->task_status == TASK_HANGING)));

    if (pthread->task_status != TASK_READY)
    {
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        if (elem_find(&thread_ready_list, &pthread->general_tag))
        {
            //线程此时不应该处于就绪队列，报错
            PANIC("thread_unblock: blocked thread in ready_list\n");
        }
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->task_status = TASK_READY;
    }
    //恢复中断状态
    intr_set_status(old_status);
}

/*
 * @brief 线程让出CPU，让其他线程运行
 * @note 这个必须是原子操作，所以需要关中断
 */
void thread_yield(void)
{
    struct task_struct *current_thread = running_thread();
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &current_thread->general_tag));
    list_append(&thread_ready_list, &current_thread->general_tag);
    current_thread->task_status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}

/*
 * @brief 线程模块初始化
 * @note 主要是初始化线程队列和线程就绪队列
 * @note 为main函数创建线程
 */
void thread_init(void)
{
    put_str("thread init start...\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);

    lock_init(&pid_lock);

    //创建第一个用户进程
    create_process(init, "init");

    //为main函数创建线程
    make_main_thread();

    idle_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread init done!\n");
}

/**
 * @brief 格式化输出信息，
 * 
 * @param buff 存放字符的缓冲区
 * @param buf_len 要输出的格式长度
 * @param ptr 存放待输出数据的指针
 * @param format 按照字符串还是数字个数输出
 */
static void format_print(char *buff, int32_t buf_len, void *ptr, char format)
{
    memset(buff, 0, buf_len);
    uint8_t out_pad_idx0 = 0; //打印的长度
    switch (format)
    {
    case 's':
        //如果是字符串
        out_pad_idx0 = sprintf(buff, "%s", ptr);
        break;
    case 'd':
        out_pad_idx0 = sprintf(buff, "%d", *((uint16_t *)ptr));
        break;
    case 'x':
        out_pad_idx0 = sprintf(buff, "%x", *((uint32_t *)ptr));
        break;
    default:
        break;
    }

    while (out_pad_idx0 < buf_len)
    {
        //如果长度不够，则以空格填充
        buff[out_pad_idx0] = ' ';
        out_pad_idx0++;
    }
    sys_write(stdout_no, buff, buf_len - 1);
}

/**
 * @brief list_traversal函数中的回调函数，用于针对线程队列的处理
 * 
 * @param pelem 待打印的线程元素
 * @param arg   占位 
 * @return true 成功返回true，失败返回false
 * @return false 
 */
static bool elem2thread_info(struct list_elem *pelem, int arg UNUSED)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    char out_pad[16] = {0};

    format_print(out_pad, 16, &pthread->pid, 'd');

    if (pthread->parent_pid == -1)
    {
        format_print(out_pad, 16, "NULL", 's');
    }
    else
    {
        format_print(out_pad, 16, &pthread->parent_pid, 'd');
    }

    switch (pthread->task_status)
    {
    case TASK_RUNNING:
        format_print(out_pad, 16, "RUNNING", 's');
        break;
    case TASK_READY:
        format_print(out_pad, 16, "READY", 's');
        break;
    case TASK_BLOCKED:
        format_print(out_pad, 16, "BLOCKED", 's');
        break;
    case TASK_WAITING:
        format_print(out_pad, 16, "WAITING", 's');
        break;
    case TASK_HANGING:
        format_print(out_pad, 16, "HANGING", 's');
        break;
    case TASK_DIED:
        format_print(out_pad, 16, "DIED", 's');
        break;

    default:
        break;
    }
    format_print(out_pad, 16, &pthread->elapsed_ticks, 'x');

    memset(out_pad, 0, 16);
    ASSERT(strlen(pthread->name) < 17);
    memcpy(out_pad, pthread->name, strlen(pthread->name));
    strcat(out_pad, "\n");
    sys_write(stdout_no, out_pad, strlen(out_pad));
    return false;
}

/**
 * @brief 打印任务列表，类似于ps
 * 
 */
void sys_ps(void)
{
    char *ps_title = "PID             PPID            STAT            TICKS           COMMAND\n";
    sys_write(stdout_no, ps_title, strlen(ps_title));
    list_traversal(&thread_all_list, elem2thread_info, 0);
}

/**
 * @brief 分配pid
 * 
 * @return pid_t 进程的pid
 */
pid_t fork_pid(void)
{
    return allocate_pid();
}