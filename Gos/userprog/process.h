#pragma once
#include "thread.h"
#include "stdint.h"

#define default_prio 31
#define USER_STACK3_VADDR (0xc0000000 - 0x1000)
#define USER_VADDR_START 0x8048000

void create_process(void *filename, char *name);
void create_user_vaddr_bitmap(struct task_struct *user_prog);
uint32_t *create_page_dir(void);
void process_activate(struct task_struct *pthread);
void page_dir_activate(struct task_struct *pthread);
void start_process(void *filename_);