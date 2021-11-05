#pragma once
#include "list.h"
#include "stdint.h"
#include "thread.h"

// * @brief 信号量结构体
struct semaphore
{
    uint8_t value;            //信号量值
    struct list wait_threads; //等待被唤醒的线程
};

// * @brief 锁结构体
struct lock
{
    struct task_struct *holder; //锁的持有者
    struct semaphore sema;      //二元信号量实现锁
    uint32_t holder_repeat_nr;  //锁的持有者重复申请锁的次数
};

void sema_init(struct semaphore *psema, uint8_t value);
void sema_down(struct semaphore *psema);
void sema_up(struct semaphore *psema);
void lock_init(struct lock *plock);
void get_lock(struct lock *plock);
void abandon_lock(struct lock *plock);