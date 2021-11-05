#include "timer.h"
#include "io.h"
#include "print.h"
#include "thread.h"
#include "debug.h"
#include "interrupt.h"
#include "stdio.h"

#define IRQ0_FREQUENCY 100 //中断频率每秒100次

#define IRQ0_FREQUENCY 100                              //时钟周期100HZ
#define INPUT_FREQUENCY 1193180                         //输入频率
#define COUNTER0_VALUE INPUT_FREQUENCY / IRQ0_FREQUENCY //输出频率
#define COUNTER0_PORT 0x40                              //第一个计时器的端口
#define COUNTER0_NO 0                                   //操作的计时器
#define COUNTER_MODE 2                                  //比率发生器工作方式
#define READ_WRITE_LATCH 3                              //读写方式为先读写低字节，后读写高字节
#define PIT_CONTROL_PORT 0x43                           //控制寄存器端口

#define mil_seconds_per_intr (1000 / IRQ0_FREQUENCY) //每秒触发中断数

uint32_t ticks; //ticks是内核自中断开启以来总共的滴答数

/*
 * @brief 让当前线程睡眠
 * @param sleep_ticks 睡眠的时钟频率数
 */
static void ticks_to_sleep(uint32_t sleep_ticks)
{
    uint32_t start_tick = ticks;
    while (ticks - start_tick < sleep_ticks)
    {
        thread_yield();
    }
}

/*
 * @brief 让当前线程睡眠
 * @param seconds 睡眠的时间数(毫秒)
 */
void mtime_sleep(uint32_t seconds)
{
    //先算出要睡眠多少时钟频率数
    uint32_t sleep_ticks = DIV_ROUND_UP(seconds, mil_seconds_per_intr);
    ASSERT(sleep_ticks > 0);
    ticks_to_sleep(seconds);
}

/*
 * @brief 计时器属性以及初始值设置
 * @param counter_port 计时器端口
 * @param counter_no 计时器标号
 * @param rwl 读写方式
 * @param counter_mode 计时器模式
 * @param counter_value 计时器初始值 
 */
static void frequency_set(uint8_t counter_port, uint8_t counter_no, uint8_t rwl, uint8_t counter_mode, uint16_t counter_value)
{
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
    //先写入低8位
    outb(counter_port, (uint8_t)counter_value);
    outb(counter_port, (uint8_t)counter_value >> 8);
}

/*
 * @brief 时钟的中断处理函数
 */
static void intr_timer_handler(void)
{
    struct task_struct *current_thread = running_thread();

    //检查是否栈溢出
    ASSERT(current_thread->stack_magic == 0x20000314);

    current_thread->elapsed_ticks++; //记录此线程占用的cpu时间数
    ticks++;                         //内核时间++

    if (current_thread->ticks == 0)
    {
        schedule();
    }
    else
    {
        current_thread->ticks--;
    }
}

/*
 * @brief 计时器初始化
 */
void timer_init()
{
    put_str("timer init start, please wait a moment...\n");
    frequency_set(COUNTER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
    register_handler(0x20, intr_timer_handler);
    put_str("timer init finished!\n");
}