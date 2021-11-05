#include "syscall.h"
#include "print.h"
#include "memory.h"
#include "fs.h"

/**
 * @brief 无参数的系统调用
 * @param NUMBER 系统调用号
 * @return 返回调用函数的返回值，先存储在eax，后被赋值给retval
 */
#define _syscall0(NUMBER) ( \
    {                       \
        int retval;         \
        asm volatile(       \
            "int $0x80"     \
            : "=a"(retval)  \
            : "a"(NUMBER)   \
            : "memory");    \
        retval;             \
    })

/**
 * @brief 无参数的系统调用
 * @param NUMBER 系统调用号
 * @param ARG1 参数一
 * @return 返回调用函数的返回值，先存储在eax，后被赋值给retval
 */
#define _syscall1(NUMBER, ARG1) (             \
    {                                         \
        int retval;                           \
        asm volatile("int $0x80"              \
                     : "=a"(retval)           \
                     : "a"(NUMBER), "b"(ARG1) \
                     : "memory");             \
        retval;                               \
    })

/**
 * @brief 无参数的系统调用
 * @param NUMBER 系统调用号
 * @param ARG1 参数一
 * @param ARG2 参数二
 * @return 返回调用函数的返回值，先存储在eax，后被赋值给retval
 */
#define _syscall2(NUMBER, ARG1, ARG2) (                  \
    {                                                    \
        int retval;                                      \
        asm volatile("int $0x80"                         \
                     : "=a"(retval)                      \
                     : "a"(NUMBER), "b"(ARG1), "c"(ARG2) \
                     : "memory");                        \
        retval;                                          \
    })

/**
 * @brief 无参数的系统调用
 * @param NUMBER 系统调用号
 * @param ARG1 参数一
 * @param ARG2 参数二
 * @param ARG3 参数三
 * @return 返回调用函数的返回值，先存储在eax，后被赋值给retval
 */
#define _syscall3(NUMBER, ARG1, ARG2, ARG3) (                       \
    {                                                               \
        int retval;                                                 \
        asm volatile("int $0x80"                                    \
                     : "=a"(retval)                                 \
                     : "a"(NUMBER), "b"(ARG1), "c"(ARG2), "d"(ARG3) \
                     : "memory");                                   \
        retval;                                                     \
    })

/**
 * @brief 得到线程的pid
 * @return 线程pid
 */
uint32_t getpid()
{
    return _syscall0(SYS_GETPID);
}

/**
 * @brief 申请size字节的内存
 * @param size 申请的内存的大小
 * @return 申请的内存的起始地址
 */
void *malloc(uint32_t size)
{
    return (void *)_syscall1(SYS_MALLOC, size);
}

/**
 * @brief 释放ptr指向的内存空间
 * @param ptr 待释放的内存空间的指针
 */
void free(void *ptr)
{
    _syscall1(SYS_FREE, ptr);
}
/**
 * @brief 把buf中的count个字符写入fd中
 * 
 * @param fd 文件描述符
 * @param buf 待写入数据的缓冲区
 * @param count 待写入的字节数
 * @return uint32_t 成功返回写入的字节数，失败返回-1
 */
uint32_t write(int32_t fd, const void *buf, uint32_t count)
{
    return _syscall3(SYS_WRITE, fd, buf, count);
}

/**
 * @brief 创建子进程
 * 
 * @return pid_t 子进程的pid
 */
pid_t fork(void)
{
    return _syscall0(SYS_FORK);
}

/**
 * @brief 从文件描述符指向的文件中读出count个字节到buf中
 * 
 * @param fd 文件描述符
 * @param buf 存放读出字符的缓冲区
 * @param count 待读出的字节数
 * @return int32_t 成功返回读出的字节数，失败返回-1
 */
int32_t read(int32_t fd, void *buf, uint32_t count)
{
    return _syscall3(SYS_READ, fd, buf, count);
}

/**
 * @brief 往屏幕打印一个字符
 * 
 * @param input_char 要打印的字符
 */
void putchar(char input_char)
{
    _syscall1(SYS_PUTCHAR, input_char);
}

/**
 * @brief 清空屏幕
 * 
 */
void clear(void)
{
    _syscall0(SYS_CLEAR);
}

/**
 * @brief 得到当前工作路径并放到buf中
 * 
 * @param buf 存放路径的缓冲区，如果其为null，则内核自己分配一个空间
 * @param size buf的大小
 * @return char* 当buf为空的时候，内核分配的空间的地址。成功返回地址，失败返回null
 */
char *getcwd(char *buf, uint32_t size)
{
    return (char *)_syscall2(SYS_GETCWD, buf, size);
}

/**
 * @brief 打开或创建文件
 * @param pathname 文件路径
 * @param flag 文件操作标识信息
 * @return 成功返回文件描述符，失败返回-1
 */
int32_t open(char *pathname, uint8_t flag)
{
    return _syscall2(SYS_OPEN, pathname, flag);
}

/**
 * @brief 关闭文件描述符fd指向的文件
 * 
 * @param fd 文件描述符
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t close(int32_t fd)
{
    return _syscall1(SYS_CLOSE, fd);
}

/**
 * @brief 重置用于文件读写操作的偏移指针
 * 
 * @param fd 文件描述符
 * @param offset 偏移量
 * @param whence 文件读写位置基准标志
 * @return int32_t 成功返回新的偏移量，失败返回-1
 * @note 如果whence=SEEK_END，那么offset应该为负值
 */
int32_t lseek(int32_t fd,int32_t offset,uint8_t whence)
{
    return _syscall3(SYS_LSEEK, fd, offset, whence);
}

/**
 * @brief 删除文件（非目录）
 * 
 * @param pathname 要删除的文件路径
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t unlink(const char *pathname)
{
    return _syscall1(SYS_UNLINK, pathname);
}

/**
 * @brief 
 * 
 * @param pathname 
 * @return int32_t 
 */
int32_t mkdir(const char* pathname)
{
    return _syscall1(SYS_MKDIR, pathname);
}

/**
 * @brief 
 * 
 */
struct dir *opendir(const char *name)
{
    return (struct dir *)_syscall1(SYS_OPENDIR, name);
}

/**
 * @brief 
 * 
 */
int32_t closedir(struct dir *dir)
{
    return _syscall1(SYS_CLOSEDIR, dir);
}

/**
 * @brief 
 * 
 */
int32_t rmdir(const char *pathname)
{
    return _syscall1(SYS_RMDIR, pathname);
}

/**
 * @brief 
 * 
 */
struct dir_entry *readdir(struct dir *dir)
{
    return (struct dir_entry *)_syscall1(SYS_READDIR, dir);
}

/**
 * @brief 
 * 
 */
void rewinddir(struct dir *dir)
{
    _syscall1(SYS_REWINDDIR, dir);
}

// /**
//  * @brief 
//  * 
//  */
int32_t state(const char *path, struct stat *stat_buf)
{
    return _syscall2(SYS_STAT, path, stat_buf);
}

/**
 * @brief 
 * 
 */
int32_t chdir(const char *path)
{
    return _syscall1(SYS_CHDIR, path);
}

/**
 * @brief 
 * 
 */
void ps(void)
{
    _syscall0(SYS_PS);
}
