#pragma once

#include "stdint.h"
#include "global.h"

/*
 * @brief 把参数指针parg指向第一个固定参数val
 * @param parg 指向参数的指针
 * @param val 第一个固定参数
 */
#define va_start(parg, val) parg = (va_list)&val

/*
 * @brief 把参数指针parg指向下一个参数并返回其值
 * @param parg 指向参数的指针
 * @param type 下一个参数的类型
 * @return 返回下一个参数的值
 */
#define va_arg(parg, type) *((type *)(parg += 4))

/*
 * @brief 把参数指针parg置为NULL
 * @param parg 参数指针
 */
#define va_end(parg) parg = NULL

typedef char *va_list;
uint32_t printf(const char *format, ...);
uint32_t vsprintf(char *str, const char *format, va_list parg);
uint32_t sprintf(char *buf, const char *format, ...);