#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

/*
 * @brief 初始化环形缓冲区
 */
void ioqueue_init(struct ioqueue *ioqueue)
{
    lock_init(&ioqueue->lock);
    ioqueue->producer = NULL;
    ioqueue->consummer = NULL;
    ioqueue->head = 0;
    ioqueue->tail = 0;
}

/*
 * @brief 得到下一个元素的位置
 * @param pos 当前元素的位置
 * @return 下一个元素的位置
 */
static int32_t next_pos(int32_t pos)
{
    return (pos + 1) % BUFF_SIZE;
}

/*
 * @brief 判断ioqueue是否元素满了
 * @param ioqueue 待判断的io队列
 * @return 满了返回true，没满返回false
 */
bool ioqueue_is_full(struct ioqueue *ioqueue)
{
    ASSERT(intr_get_status() == INTR_OFF);
    return next_pos(ioqueue->head) == ioqueue->tail;
}

/*
 * @brief 判断队列是否为空
 * @param ioqueue 待判断的队列
 * @return 为空返回true，未空返回false
 */
static bool ioqueue_is_empty(struct ioqueue *ioqueue)
{
    ASSERT(intr_get_status() == INTR_OFF);
    return ioqueue->head == ioqueue->tail;
}

/*
 * @brief 使当前生产者或者消费者在此缓冲区上等待
 * @param 是一个输出参数，其会指向当前线程的地址
 */
static void ioqueue_wait(struct task_struct **waiter)
{
    ASSERT(*waiter == NULL && waiter != NULL);
    *waiter = running_thread();
    thread_block(TASK_BLOCKED);
}

/*
 * @brief 唤醒waiter
 * @param waiter 待唤醒的线程
 */
static void wakeup(struct task_struct **waiter)
{
    ASSERT(*waiter != NULL);
    thread_unblock(*waiter);
    *waiter = NULL;
}

/*
 * @brief 从环形缓冲区中得到一个字符
 * @param ioqueue 存储数据的缓冲区
 * @return 返回取出的字符
 */
char ioqueue_get_char(struct ioqueue *ioqueue)
{
    ASSERT(intr_get_status() == INTR_OFF);

    while (ioqueue_is_empty(ioqueue))
    {
        //队列为空，先休眠
        get_lock(&ioqueue->lock);
        ioqueue_wait(&ioqueue->consummer);
        abandon_lock(&ioqueue->lock);
    }

    char byte = ioqueue->buff[ioqueue->tail];
    ioqueue->tail = next_pos(ioqueue->tail); //tail++

    if (ioqueue->producer != NULL)
    {
        //唤醒生产者
        wakeup(&ioqueue->producer);
    }
    return byte;
}

/*
 * @brief 往缓冲区中输入一个字符
 * @param ioqueue 存放字符的缓冲区
 * @param ch 待输入的字符
 */
void ioqueue_putchar(struct ioqueue *ioqueue, char ch)
{
    ASSERT(intr_get_status() == INTR_OFF);

    while (ioqueue_is_full(ioqueue))
    {
        //满了就阻塞生产者
        get_lock(&ioqueue->lock);
        ioqueue_wait(&ioqueue->producer);
        abandon_lock(&ioqueue->lock);
    }

    ioqueue->buff[ioqueue->head] = ch;
    ioqueue->head = next_pos(ioqueue->head);

    if (ioqueue->consummer != NULL)
    {
        //唤醒消费者
        wakeup(&ioqueue->consummer);
    }
}
