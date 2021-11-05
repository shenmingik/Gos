#include "shell.h"
#include "stdint.h"
#include "fs.h"
#include "file.h"
#include "syscall.h"
#include "stdio.h"
#include "global.h"
#include "assert.h"
#include "string.h"
#include "in_cmd.h"
#define MAX_ARG_NO 16 //最大支持15个参数

char *argv[MAX_ARG_NO];
int32_t argc = -1;

//存储输入
static char cmd_line[MAX_PATH_LEN] = {0};
char final_path[MAX_PATH_LEN] = {0}; //多功能缓冲区
char cwd_cache[64] = {0};

/**
 * @brief 输出提示符
 * 
 */
void print_tips(void)
{
    printf("(Gos)[ik@localhost %s]$ ", cwd_cache);
}

/**
 * @brief 从键盘缓冲区读出count个字符到buf中
 * 
 * @param buf 存放读入字符的缓冲区
 * @param count 读入的字符数
 */
static void readline(char *buf, int32_t count)
{
    assert(buf != NULL && count > 0);
    char *pos = buf;

    while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count)
    {
        //不断读入字符
        switch (*pos)
        {
        case '\n':
        case '\r':
            *pos = 0;
            putchar('\n'); //下一个命令了
            return 0;
        case '\b':
            if (buf[0] != '\b')
            {
                //阻止删除非本次输入的信息
                --pos;
                putchar('\b');
            }
            break;
            //除了本次行输入内容，全部清屏
        case 'l' - 'a':
            *pos = 0;
            clear(); //清屏
            print_tips();
            //输出本次输入的内容
            printf("%s", buf);
            break;
            //清空本次输入内容
        case 'u' - 'a':
            while (buf != pos)
            {
                putchar('\b');
                *(pos--) = 0;
            }
            break;
        default:
            putchar(*pos); //其他字符直接输出
            pos++;
        }
    }
    printf("readline: can't find input in cmd, and max char is 128\n");
}

/**
 * @brief 解析输入的命令。将结果存入argv
 * 
 * @param cmd_str 输入的字符串
 * @param argv 存储各个单词的数组
 * @param token 分割单词的标识符
 * @return int32_t 成功返回参数个数，失败返回-1
 */
static int32_t cmd_parse(char *cmd_str, char **argv, char token)
{
    assert(cmd_str != NULL);
    int32_t arg_idx = 0;

    //初始化argv数组
    while (arg_idx < MAX_ARG_NO)
    {
        argv[arg_idx] = NULL;
        arg_idx++;
    }

    char *next = cmd_str;
    int32_t argc = 0;
    //? 假如输入: a  b c
    while (*next)
    {
        //? 此时:a  b c
        //去除命令字或者参数之间的标识符
        while (*next == token)
        {
            next++;
        }

        //如果最后一个参数有空格,就结束了参数解析
        if (*next == 0)
        {
            break;
        }
        //? ①此时:argv[argc]=a  b c
        argv[argc] = next;

        //? 此时:  b c
        while (*next && *next != token)
        {
            next++;
        }

        //截断字符串
        if (*next)
        {
            *next++ = 0;
        }

        if (argc > MAX_ARG_NO)
        {
            return -1;
        }
        argc++;
    }
    return argc;
}

/**
 * @brief shell程序，从init进程fork出来
 * 
 */
void my_shell(void)
{
    cwd_cache[0] = '/';
    while (1)
    {
        print_tips();
        memset(final_path, 0, MAX_PATH_LEN);
        memset(cmd_line, 0, MAX_PATH_LEN);
        readline(cmd_line, MAX_PATH_LEN);
        if (cmd_line[0] == 0)
        {
            //只键入回车
            continue;
        }

        //解析输入的参数
        argc = -1;
        argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1)
        {
            printf("shell: no parma input!\n");
            continue;
        }

        if (!strcmp("pwd", argv[0]))
        {
            in_pwd(argc, argv);
        }
        else if(!strcmp("cd",argv[0]))
        {
            in_cd(argc, argv);
        }
        else if (!strcmp("ls", argv[0]))
        {
            in_ls(argc, argv);
        }
        else if(!strcmp("ps",argv[0]))
        {
            in_ps(argc, argv);
        }
        else if(!strcmp("clear",argv[0]))
        {
            in_clear(argc, argv);
        }
        else if (!strcmp("mkdir", argv[0]))
        {
            in_mkdir(argc, argv);
        }
        else if (!strcmp("rmdir", argv[0]))
        {
            in_rmdir(argc, argv);
        }
        else if (!strcmp("mkfile", argv[0]))
        {
            in_mkfile(argc, argv);
        }
        else if (!strcmp("rm", argv[0]))
        {
            in_rm(argc, argv);
        }
    }
    panic("my_shell: should not be here");
}


