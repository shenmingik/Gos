#pragma once
#include "thread.h"
void update_tss_esp(struct task_struct *pthread);
void tss_init(void);