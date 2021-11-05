#include "stdio-kernel.h"
#include "stdio.h"
#include "console.h"
/*
 * @brief 模仿C语言库函数printf，格式化打印
 * @param format 第一个是格式化控制字符串
 * @param arg1  参数1
 * @param ... ...
 * @param argn 参数n
 */
void printk(const char *format, ...)
{
    va_list args;
    va_start(args, format); //args指向format
    char buf[1024] = {0};
    vsprintf(buf, format, args);
    va_end(args);
    console_put_str(buf);
}
