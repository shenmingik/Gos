#include "sync.h"
#include "list.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"

/*
 * @brief 初始化信号量
 * @param psema 信号量的地址
 * @param value 信号量的初始信号值
 */
void sema_init(struct semaphore *psema, uint8_t value)
{
    psema->value = value;            // 为信号量赋初值
    list_init(&psema->wait_threads); //初始化信号量的等待队列
}

/*
 * @brief 初始化锁plock
 * @param plock 锁的地址
 */
void lock_init(struct lock *plock)
{
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_init(&plock->sema, 1); // 信号量初值为1
}

/*
 * @brief 信号量down操作
 * @param psema 待操作的信号量
 */
void sema_down(struct semaphore *psema)
{
    enum intr_status old_status = intr_disable();
    while (psema->value == 0)
    {
        //代表锁被别人持有
        //判断不在等待队列中
        ASSERT(!elem_find(&psema->wait_threads, &running_thread()->general_tag));

        if (elem_find(&psema->wait_threads, &running_thread()->general_tag))
        {
            PANIC("sema_down: thread blocked has been in wait_threads");
        }

        //加入等待队列
        list_append(&psema->wait_threads, &running_thread()->general_tag);
        thread_block(TASK_BLOCKED);
    }
    //下面代码处于value为1或者被唤醒的情况下
    psema->value--;
    ASSERT(psema->value == 0);
    intr_set_status(old_status);
}

/*
 * @brief 信号量psema的down操作
 * @param psema 待操作的信号量
 */
void sema_up(struct semaphore *psema)
{
    enum intr_status old_status = intr_disable();

    ASSERT(psema->value == 0);
    if (!list_empty(&psema->wait_threads))
    {
        //得到一个被锁住的线程
        struct task_struct *thread_blocked = elem2entry(struct task_struct, general_tag, list_pop(&psema->wait_threads));
        thread_unblock(thread_blocked);
    }
    psema->value++;
    ASSERT(psema->value == 1);
    intr_set_status(old_status);
}

/*
 * @brief 获得锁plock的所有权
 * @param plock 待操作的锁
 */
void get_lock(struct lock *plock)
{
    if (plock->holder != running_thread())
    {
        //while(1)等待别的线程主动释放锁
        sema_down(&plock->sema);
        //此时代表持有锁的线程已经释放了
        plock->holder = running_thread();
        ASSERT(plock->holder_repeat_nr == 0);
        plock->holder_repeat_nr = 1;
    }
    else
    {
        plock->holder_repeat_nr++;
    }
}

/*
 * @brief 锁的持有者放弃plock的所有权
 * @param plock 待操作的锁
 */
void abandon_lock(struct lock *plock)
{
    ASSERT(plock->holder == running_thread());
    if (plock->holder_repeat_nr > 1)
    {
        plock->holder_repeat_nr--;
        return;
    }
    ASSERT(plock->holder_repeat_nr == 1);
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->sema);
}
