#include "in_cmd.h"
#include "syscall.h"
#include "stdio.h"
#include "string.h"
#include "fs.h"
#include "global.h"
#include "dir.h"
#include "shell.h"
#include "assert.h"

/**
 * @brief 将old_abs_path中的.和..转换为实际路径存入new_abs_path,比如说/home/ik 下的..就会被转换为/home
 * 
 * @param old_abs_path 原来的路径
 * @param new_abs_path 新路径
 */
static void wash_path(char *old_abs_path, char *new_abs_path)
{
    assert(old_abs_path[0] == '/');
    char name[MAX_FILE_NAME_LEN] = {0};
    char *sub_path = old_abs_path;
    sub_path = path_parse(sub_path, name); //得到最上层目录名称
    if (name[0] == 0)
    {
        //如果只键入'/'，就直接将其存入new_abs_path中
        new_abs_path[0] = '/';
        new_abs_path[1] = 0;
        return;
    }

    //先传入一个 '/'
    new_abs_path[0] = 0;
    strcat(new_abs_path, "/");
    while (name[0])
    {
        //如果是上一级目录
        if (!strcmp("..", name))
        {
            //找到最后一个反斜杠
            char *slash_ptr = strrchr(new_abs_path, '/');
            if (slash_ptr != new_abs_path)
            {
                //如果不是最顶层的目录，最后一个/换成0
                // /home/ik-->/home
                *slash_ptr = 0;
            }
            else
            {
                //如果是/home，那么便是/
                *(slash_ptr + 1) = 0;
            }
        }
        else if (strcmp(".", name))
        {
            //如果是当前目录
            if (strcmp(new_abs_path, "/"))
            {
                //如果不是/
                strcat(new_abs_path, "/");
            }
            strcat(new_abs_path, name);
        }

        memset(name, 0, MAX_FILE_NAME_LEN);
        if (sub_path)
        {
            sub_path = path_parse(sub_path, name);
        }
    }
}

/**
 * @brief 将path处理为绝对路径，存储到final_path中
 * 
 * @param path  待处理的相对路径
 * @param final_path 最终路径
 */
void make_clear_abs_path(char *path, char *final_path)
{
    char abs_path[MAX_PATH_LEN] = {0};
    if (path[0] != '/')
    {
        memset(abs_path, 0, MAX_PATH_LEN);
        if (getcwd(abs_path, MAX_PATH_LEN) != NULL)
        {
            if (!((abs_path[0] == '/') && (abs_path[1] == 0)))
            {
                strcat(abs_path, "/");
            }
        }
    }
    strcat(abs_path, path);
    wash_path(abs_path, final_path);
}

/**
 * @brief pwd函数，显示当前路径信息
 * 
 * @param argc 输入的参数个数
 * @param argv 占位用
 */
void in_pwd(uint32_t argc, char **argv UNUSED)
{
    if (argc != 1)
    {
        printf("(Gos)pwd: no argument!\n");
        return;
    }
    else
    {
        if (getcwd(final_path, MAX_PATH_LEN) != NULL)
        {
            //输出当前路径
            printf("%s\n", final_path);
        }
        else
        {
            printf("(Gos)pwd: get current path error!\n");
        }
    }
}

/**
 * @brief cd命令,转换当前目录
 * 
 * @param argc 命令参数个数
 * @param argv 路径参数
 * @return char* 成功返回转到的路径名称，失败返回null
 */
char *in_cd(uint32_t argc, char **argv)
{
    if (argc > 2)
    {
        printf("(Gos)cd: cd command limit 2 argument!\n");
        return NULL;
    }

    //如果只键入cd 不带参数，则默认转到根目录
    if (argc == 1)
    {
        final_path[0] = '/';
        final_path[1] = 0;
    }
    else
    {
        //解析当前路径
        make_clear_abs_path(argv[1], final_path);
    }

    if (chdir(final_path) == -1)
    {
        printf("(Gos)cd: no such dir: %s\n", final_path);
        return NULL;
    }
    return final_path;
}

/**
 * @brief ls命令,显示当前目录下所有文件信息
 * 
 * @param argc 输入参数的个数
 * @param argv 输入的参数
 */
void in_ls(uint32_t argc, char **argv)
{
    char *pathname = NULL;
    struct stat file_stat;
    memset(&file_stat, 0, sizeof(struct stat));
    bool long_info = false;
    uint32_t arg_path_no = 0;
    uint32_t arg_idx = 1; //跨过argv[0],argv[0]是ls

    while (arg_idx < argc)
    {
        //现在判断选项，是-l -h 还是其他参数
        if (argv[arg_idx][0] == '-')
        {
            if (!strcmp(argv[arg_idx], "-l"))
            {
                long_info = true;
            }
            else if (!strcmp(argv[arg_idx], "-h"))
            {
                printf("(Gos)ls: use -l show all information\n");
                printf("(Gos)ls: use -h for help\n");
                return;
            }
            else
            {
                printf("(Gos)ls: Gos can't support it now!\n");
                return;
            }
        }
        else
        {
            //不带-l -h参数
            //但是有ls的路径参数
            if (arg_path_no == 0)
            {
                pathname = argv[arg_idx];
                arg_path_no = 1;
            }
            else
            {
                printf("(Gos)ls: only support one argument now!\n");
                return;
            }
        }
        arg_idx++;
    }

    if (pathname == NULL)
    {
        //如果没有路径，也没有选项参数，就只输出当前路径的信息
        if (getcwd(final_path, MAX_PATH_LEN) != NULL)
        {
            pathname = final_path;
        }
        else
        {
            printf("(Gos)ls: get_cwd failed!\n");
            return;
        }
    }
    else
    {
        //路径为输入的参数
        make_clear_abs_path(pathname, final_path);
        pathname = final_path;
    }

    if (state(pathname, &file_stat) == -1)
    {
        printf("(Gos)ls: can't get file %s infomation", pathname);
        return;
    }

    if (file_stat.stat_file_type == FT_DIRECTORY)
    {
        //输出目录
        struct dir *dir = opendir(pathname);
        struct dir_entry *dir_e = NULL;
        char sub_pathname[MAX_PATH_LEN] = {0};
        uint32_t pathname_len = strlen(pathname);
        uint32_t last_char_idx = pathname_len - 1;
        memcpy(sub_pathname, pathname, pathname_len);
        if (sub_pathname[last_char_idx] != '/')
        {
            sub_pathname[pathname_len] = '/';
            pathname_len++;
        }

        //目录指针置为0
        rewinddir(dir);
        if (long_info)
        {
            //-l 参数
            //文件类型
            char file_type;
            //输出文件信息
            printf("file total size: %d\n", file_stat.stat_size);
            printf("FileType   Inode   FileSize   FileName\n");
            while ((dir_e = readdir(dir)))
            {
                file_type = 'd';
                if (dir_e->file_type == FT_REGULAR)
                {
                    //普通文件
                    file_type = 'f';
                }
                sub_pathname[pathname_len] = 0;
                strcat(sub_pathname, dir_e->filename);
                memset(&file_stat, 0, sizeof(struct stat));
                if (state(sub_pathname, &file_stat) == -1)
                {
                    printf("(Gos)ls: can't get file %s infomation", dir_e->filename);
                    return;
                }
                printf("%c          %d       %d          %s\n", file_type, dir_e->inode_no, file_stat.stat_size, dir_e->filename);
            }
        }
        else
        {
            while ((dir_e = readdir(dir)))
            {
                printf("%s ", dir_e->filename);
            }
            printf("\n");
        }
        closedir(dir);
    }
    else
    {
        //输出文件
        if (long_info)
        {
            printf("f   %d   %d   %s\n", file_stat.stat_inode_no, file_stat.stat_size, pathname);
        }
        else
        {
            printf("%s\n", pathname);
        }
    }
}

/**
 * @brief ps命令，显示当前进程
 * 
 * @param argc 输入参数的个数
 * @param argv 输入的参数
 */
void in_ps(uint32_t argc, char **argv UNUSED)
{
    if (argc != 1)
    {
        printf("(Gos)ps: too much argument!\n");
        return;
    }
    ps();
}

/**
 * @brief clear命令，清屏
 * 
 * @param argc 输入参数的个数
 * @param argv 输入的参数
 */
void in_clear(uint32_t argc, char **argv UNUSED)
{
    if (argc != 1)
    {
        printf("(Gos)ps: too much argument!\n");
        return;
    }
    clear();
}

/**
 * @brief mkdir命令，创建指定目录
 * 
 * @param argc 输入参数的个数
 * @param argv 输入的参数
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t in_mkdir(uint32_t argc, char **argv)
{
    int32_t ret = -1;
    if (argc != 2)
    {
        printf("(Gos)mkdir: mkdir need one argument!\n");
        return;
    }
    else
    {
        make_clear_abs_path(argv[1], final_path);
        if (strcmp(final_path, "/"))
        {
            if (mkdir(final_path) == 0)
            {
                return 0;
            }
            else
            {
                printf("(Gos)mkdir: create dir %s failed!\n", final_path);
            }
        }
    }
    return ret;
}

/**
 * @brief rmdir命令，删除指定目录
 * 
 * @param argc 输入参数的个数
 * @param argv 输入的参数
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t in_rmdir(uint32_t argc, char **argv)
{
    int32_t ret = -1;
    if (argc != 2)
    {
        printf("(Gos)rmdir: rmdir need one argument!\n");
        return;
    }
    else
    {
        make_clear_abs_path(argv[1], final_path);
        if (strcmp(final_path, "/"))
        {
            if (rmdir(final_path) == 0)
            {
                return 0;
            }
            else
            {
                printf("(Gos)rmdir: remove dir %s failed!\n", final_path);
            }
        }
    }
    return ret;
}

/**
 * @brief 创建文件
 * 
 * @param argc 输入参数的个数
 * @param argv 输入的参数
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t in_mkfile(uint32_t argc, char **argv)
{
    int32_t ret = -1;
    if (argc != 2)
    {
        printf("(Gos)mkfile: mkfile need one argument!\n");
        return;
    }
    else
    {
        make_clear_abs_path(argv[1], final_path);
        if (strcmp(final_path, "/"))
        {
            int fd = open(final_path, O_CREAT);
            if (fd != -1)
            {
                //close(fd);
                return 0;
            }
            else
            {
                printf("(Gos)mkfile: create file %s failed!\n", final_path);
            }
        }
    }
    return ret;
}

/**
 * @brief rm命令 删除文件
 * 
 * @param argc 
 * @param argv 
 * @return int32_t 
 */
int32_t in_rm(uint32_t argc, char **argv)
{
    int32_t ret = -1;
    if (argc != 2)
    {
        printf("(Gos)rm: rm need one argument!\n");
        return;
    }
    else
    {
        make_clear_abs_path(argv[1], final_path);
        if (strcmp(final_path, "/"))
        {
            if (unlink(final_path) == 0)
            {
                return 0;
            }
            else
            {
                printf("(Gos)rm: remove file %s failed!\n", final_path);
            }
        }
    }
    return ret;
}