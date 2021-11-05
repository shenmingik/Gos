#pragma once

#include "stdint.h"
#include "thread.h"
#include "sync.h"

#define BUFF_SIZE 64    //环形缓冲区的大小

struct ioqueue
{
    struct lock lock;  //环形队列锁，用于同步
    struct task_struct *producer;
    struct task_struct *consummer;
    char buff[BUFF_SIZE];   //缓冲区
    int32_t head;   //队头
    int32_t tail;   //队尾
};

void ioqueue_init(struct ioqueue *ioqueue);
bool ioqueue_is_full(struct ioqueue *ioqueue);
char ioqueue_get_char(struct ioqueue *ioqueue);
void ioqueue_putchar(struct ioqueue *ioqueue, char ch);
