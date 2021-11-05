#include "stdio.h"
#include "string.h"
#include "global.h"
#include "syscall.h"

/**
 * @brief 整型转换为字符串
 * @param value 待转换的数值
 * @param pbuff 指向存放这个字符串的缓冲区，是一个输出参数
 * @param base 进制
 * @note pbuff是一个二级指针，便于对缓冲区的一级指针进行直接的修改
 */
static void itoa(uint32_t value, char **pbuff, uint8_t base)
{
    uint32_t low_num = value % base;
    uint32_t next_value = value / base;
    if (next_value)
    {
        itoa(next_value, pbuff, base);
    }
    if (low_num < 10) //10进制
    {
        *((*pbuff)++) = low_num + '0';
    }
    else
    {
        //16进制
        *((*pbuff)++) = low_num - 10 + 'A';
    }
}

/**
 * @brief 解析format，将parg指向的参数和format拼接之后放到str中
 * @param str 存放结果的buff
 * @param format 格式，类似于：(“this is %x",0x16)
 * @param parg 格式化的参数，指的是上面的0x16
 * @return str的长度
 */
uint32_t vsprintf(char *str, const char *format, va_list parg)
{
    char *pbuf = str;
    const char *index_ptr = format;
    char index_char = *index_ptr;

    int32_t iarg;
    char *arg_str;

    //解析format字符串
    while (index_char)
    {
        if (index_char != '%')
        {
            //如果不是%，就直接拷贝format到buf缓冲区
            *(pbuf++) = index_char;
            index_char = *(++index_ptr);
            continue;
        }
        index_char = *(++index_ptr); //得到%的下一个字符
        switch (index_char)
        {
        case 'x': //%x  16进制
            iarg = va_arg(parg, int);
            itoa(iarg, &pbuf, 16);
            index_char = *(++index_ptr);
            break;
        case 's': //%s 字符串
            arg_str = va_arg(parg, char *);
            strcpy(pbuf, arg_str);
            pbuf += strlen(arg_str);
            index_char = *(++index_ptr);
            break;
        case 'c': //%c 字符
            *(pbuf++) = va_arg(parg, char);
            index_char = *(++index_ptr);
            break;
        case 'd':
            iarg = va_arg(parg, int);
            if (iarg < 0)
            {
                //负数
                iarg = 0 - iarg;
                *pbuf++ = '-';
            }
            itoa(iarg, &pbuf, 10);
            index_char = *(++index_ptr);
            break;
        default:
            break;
        }
    }
    return strlen(str);
}

/**
 * @brief 模仿C语言库函数printf，格式化打印
 * @param format 第一个是格式化控制字符串
 * @param arg1  参数1
 * @param ... ...
 * @param argn 参数n
 * @return 打印的字符串的长度
 */
uint32_t printf(const char *format, ...)
{
    va_list args;
    va_start(args, format); //args指向format
    char buf[1024] = {0};
    vsprintf(buf, format, args);
    va_end(args);
    return write(1, buf, strlen(buf));
}

/**
 * @brief 模仿C语言库函数sprintf，往buf写入格式化打印数据
 * @param buf 待写入数据的缓冲区
 * @param format 格式化控制字符串
 * @param arg1  参数1
 * @param ... ...
 * @param argn 参数n
 * @return 打印的字符串的长度
 */
uint32_t sprintf(char *buf, const char *format, ...)
{
    va_list args;
    uint32_t retval;
    va_start(args, format);
    retval = vsprintf(buf, format, args);
    va_end(args);
    return retval;
}
