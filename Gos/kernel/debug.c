#include "debug.h"
#include "print.h"
#include "interrupt.h"

/*
 * @brief 打印文件名、行号、函数名和错误条件并使程序悬停
 * @param file_name 触发断言的文件名
 * @param line 触发断言的行号
 * @param func 触发断言的函数
 * @param 触发断言的条件
 */
void panic_spin(char *file_name, int line, const char *func, const char *condition)
{
    intr_disable(); //关中断
    //error occur:
    //  filename:"filename"
    //  line:0x(int)line
    //  function:"func name"
    //  condition:"condition"
    put_str("error occur:\n");
    put_str("    filename:");
    put_str(file_name);
    put_str("\n");
    put_str("    line:0x");
    put_int(line);
    put_str("\n");
    put_str("    function:");
    put_str((char *)func);
    put_str("\n");
    put_str("    condition:");
    put_str((char *)condition);
    put_str("\n");
    while (1)
        ;
}