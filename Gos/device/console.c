#include "console.h"
#include "print.h"
#include "stdint.h"
#include "sync.h"
#include "thread.h"

static struct lock console_lock; //控制台锁

/*
 * @brief 初始化终端
 */
void console_init()
{
    lock_init(&console_lock);
}

/*
 * @brief 获得终端使用权
 */
void get_console()
{
    get_lock(&console_lock);
}

/*
 * @brief 释放终端使用权
 */
void abandon_console()
{
    abandon_lock(&console_lock);
}

/*
 * @brief 在终端输出字符串str
 * @param str 待输出的字符串
 */
void console_put_str(char *str)
{
    get_console();
    put_str(str);
    abandon_console();
}

/*
 * @brief 在终端中输出字符ch
 * @param ch 待输出的字符
 */
void console_put_char(uint8_t ch)
{
    get_console();
    put_char(ch);
    abandon_console();
}

/*
 * @brief 在终端中输出数字number
 * @param number 带输出的数字
 */
void console_put_int(uint32_t number)
{
    get_console();
    put_int(number);
    abandon_console();
}
