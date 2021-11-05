//主要是实现assert断言
#pragma once

void panic_spin(char *file_name, int line, const char *func, const char *condition);

/*
 * @brief 调用函数panic_spin
 */
#define PANIC(...) panic_spin(__FILE__,__LINE__,__func__,__VA_ARGS__)

#ifdef NDEBUG
#define ASSERT(CONDITION)((void)0)
#else
//如果condition为ture就什么都不做，否则调用PANIC
#define ASSERT(CONDITION)  \
    if (CONDITION)         \
    {                      \
    }                      \
    else                   \
    {                      \
        PANIC(#CONDITION); \
    } //'#'表示编译器将宏的参数转化为字符串字面量，a-->"a"
#endif