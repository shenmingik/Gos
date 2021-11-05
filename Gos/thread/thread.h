#pragma once
#include "stdint.h"
#include "list.h"
#include "memory.h"
#include "global.h"

#define MAX_FILES_OPEN_PER_PROC 8

#define TASK_NAME_LEN 16

typedef void thread_func(void *);
typedef int16_t pid_t;
//定义进程或者线程状态
enum task_status
{
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};

/*
 * @brief 中断栈
 * @note 此结构用于中断发生时保护程序的上下文环境
 * @note 当被外部打断的时候，会将此结构压入上下文寄存器
 * @note 此栈再线程自己的内核栈中位置固定，所在页的最顶端
 */
struct intr_stack
{
    uint32_t vec_no; //kernel.S中宏vector中push压入的中断号
    uint32_t edi;    //以下用于通用寄存器现场的保存
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy; //esp会变化，所以会被popad忽略，所以栈指针我们也要保存
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    uint32_t gs; //保存特殊寄存器
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    //以下由cpu从低特权级进入高特权级时压入
    uint32_t err_code; //error会被压入在eip之后
    void (*eip)(void);
    uint32_t cs; //段基址会变，所以这个也要保存
    uint32_t eflags;
    void *esp;
    uint32_t ss;
};

/*
 * @brief 线程栈
 * @note 用于存储线程中待执行的函数
 * @note 此结构在线程自己的内核栈中位置不固定
 * @note 记录在switch_to时保存线程环境
 */
struct thread_stack
{
    uint32_t ebp; //被调函数中用于保存主调函数中这几个寄存器的值,主要是怕破坏现场
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

    // * @brief 当线程第一次执行的时候，eip指向待调用函数；其他时候，eip指向switch_to的返回地址
    void (*eip)(thread_func *func, void *func_arg);

    //以下仅供第一次被调度上cpu时使用
    void (*unused_retaddr); //返回地址，按道理说线程一般执行结束之后是没有返回地址这一说的，所以这个仅仅占位用啦
    thread_func *function; //保存所调用的函数地址
    void *func_arg;        //保存所调用函数的所需参数
};

//进程或线程的pcb
struct task_struct
{
    uint32_t *self_kstack; //各个内核线程都有自己的内核栈
    pid_t pid;
    enum task_status task_status; //线程状态
    char name[TASK_NAME_LEN];     //线程名称

    uint8_t priority;       //线程优先级
    uint8_t ticks;          //每一次线程占用CPU的时间数
    uint32_t elapsed_ticks; //线程从诞生起总共执行的CPU数


    struct list_elem general_tag;  //表示线程在一般队列中的节点身份
    struct list_elem all_list_tag; //作用于线程队列thread_all_list中的节点

    uint32_t *pgdir;                                  //进程页表的虚拟地址
    struct virtual_addr userprog_vaddr;               //用户进程的虚拟地址
    struct mem_block_desc u_block_desc[MEM_DESC_CNT]; //进程的内存管理模块

    int32_t fd_table[MAX_FILES_OPEN_PER_PROC]; //文件描述符数组

    uint32_t cwd_inode_no; //进程所在的工作目录的inode编号
    uint16_t parent_pid;   //父进程的pid
    uint32_t stack_magic;  //栈的边界标记，用于检测栈溢出
};

extern struct list thread_ready_list;
extern struct list thread_all_list;

void thread_create(struct task_struct *pthread, thread_func function, void *func_arg);
void init_thread(struct task_struct *pthread, char *name, int prio);
struct task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg);
struct task_struct *running_thread(void);
void schedule(void);
void thread_init(void);
void thread_block(enum task_status stat);
void thread_unblock(struct task_struct *pthread);
void thread_yield(void);
pid_t fork_pid(void);
void sys_ps(void);