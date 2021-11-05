#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "console.h"
#include "keyboard.h"
#include "syscall-init.h"
#include "ide.h"
#include "fs.h"
/*
 * @brief 初始化所有模块
 */
void init_all()
{
    put_str("init Gos's all begin,please wait...\n");
    idt_init();        //初始化中断
    mem_init();        // 初始化内存管理系统
    thread_init();     // 初始化线程相关结构
    timer_init();      //初始化时钟
    console_init();    //初始化终端
    keyboard_init();   //键盘驱动初始化
    tss_init();        // tss初始化
    syscall_init();    //初始化系统调用
    intr_enable();     // 后面的ide_init需要打开中断
    ide_init();        //初始化硬盘
    filesystem_init(); //初始化文件系统
}