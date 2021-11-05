#pragma once

#include "stdint.h"

/*
 * @brief 向端口写入一个字节的数据
 * @param   port 端口号
 * @param   data 待写入的数据
 */
static inline void outb(uint16_t port,uint8_t data)
{
    asm volatile("outb %b0,%w1" ::"a"(data), "Nd"(port));
}

/*
 * @brief 向端口写入N个字节的数据
 * @param   port 端口号
 * @param   addr 待写入的数据的起始地址
 * @param   word_cnt 写入数据的长度      
 */
static inline void outsw(uint16_t port,const void* addr,uint32_t word_cnt)
{
    asm volatile("cld;rep outsw"
                 : "+S"(addr), "+c"(word_cnt)
                 : "d"(port));
}

/*
 * @brief 从端口中读入一个字节并返回
 * @param   port 端口号
 * @return 读出的字节
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t data;
    asm volatile("inb %w1,%b0"
                 : "=a"(data)
                 : "Nd"(port));
    return data;
}

/*
 * @brief 向端口读出N个字节的数据
 * @param   port 端口号
 * @param   addr 存放数据的起始地址
 * @param   word_cnt 读出数据的长度      
 */
static inline void insw(uint16_t port,void *addr,uint32_t word_cnt)
{
    //这会改动内存，需要显示提示gcc
    asm volatile("cld;rep insw"
                 : "+D"(addr), "+c"(word_cnt)
                 : "d"(port)
                 : "memory");
}