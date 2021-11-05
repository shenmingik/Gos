#include "string.h"
#include "global.h"
#include "debug.h"

/*
 * @brief 将从dst_内存位置的size个字节置为value
 * @param dst_ 起始地址
 * @param size 从起始位置开始多少个字节数
 * @param value 目标值
 */
void memset(void *dst_, uint8_t value, uint32_t size)
{
    ASSERT(dst_ != NULL);
    uint8_t* dst = (uint8_t *)dst_;
    while (size-- > 0)
    {
        *dst++ = value;
    }
}

/*
 * @brief 将从src位置的size个字节的数据拷贝到dst的位置上去
 * @param dst 目的地址
 * @param src 源地址
 * @param size 待拷贝字节大小
 */
void memcpy(void *dst_, const void *src_, uint32_t size)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    uint8_t *dst = dst_;
    const uint8_t *src = src_;
    while(size-- > 0)
    {
        *dst++ = *src++;
    }
}

/*
 * @brief 按顺序比较a开始和b开始的size个字节是否相同
 * @param a 待比较1
 * @param b 待比较2
 * @param size 待比较字节数
 * @return 如果a比b大，就返回1，比b小返回-1，一样返回0
 */
int memcmp(const void *a_, const void *b_, uint32_t size)
{
    const uint8_t *a = a_;
    const uint8_t *b = b_;
    ASSERT(a != NULL || b != NULL);
    while (size-- >0)
    {
        if(*a!=*b)
        {
            return *a > *b ? 1 : -1;
        }
        a++;
        b++;
    }
    return 0;
}

/*
 * @brief 字符串拷贝函数,将src的内容拷贝到dst_
 */
char *strcpy(char *dst_, const char *src_)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    char *r = dst_;
    while ((*dst_++ = *src_++))
        ;
    return r;
}

/*
 * @brief 计算字符串str的长度
 */
uint32_t strlen(const char *str)
{
    ASSERT(str != NULL);
    const char *p = str;
    while (*p++)
        ;
    return (p - str - 1);
}

/*
 * @brief 字符串比较函数，若a中元素大于b返回1；若小于返回-1；若相同返回0
 */
int8_t strcmp(const char *a, const char *b)
{
    ASSERT(a != NULL && b != NULL);
    while(*a!=0 && *a == *b)
    {
        a++;
        b++;
    }
    return *a < *b ? -1 : *a > *b;
}

/*
 * @brief 返回字符串str中第一次出现ch时候的地址
 */
char *strchr(const char *str, const uint8_t ch)
{
    ASSERT(str != NULL);
    while(*str!=0)
    {
        if(*str == ch)
        {
            return (char *)str;
        }
        str++;
    }
    return NULL;
}

/*
 * @brief 返回字符串str中最后一次出现ch的地址
 */
char *strrchr(const char *str, const uint8_t ch)
{
    ASSERT(str != NULL);
    const char *last_char = NULL;
    while(*str!=0)
    {
        if(*str == ch)
        {
            last_char = str;
        }
        str++;
    }
    return (char *)last_char;
}

/*
 * @brief 字符串拼接函数，将src的内容拼接到dst后面
 */
char *strcat(char *dst_, const char *src_)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    char *str = dst_;
    while(*str++)
        ;
    --str;
    while((*str++ = *src_++))
        ;
    return dst_;
}

/*
 * @brief 返回ch在str中出现的次数
 */
uint32_t strchrs(const char *str, uint8_t ch)
{
    ASSERT(str != NULL);
    uint32_t count = 0;
    const char *p = str;
    while(*p!=0)
    {
        if(*p == ch)
            count++;
        p++;
    }
    return count;
}