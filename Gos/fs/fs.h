#pragma once

#include "stdint.h"

#define MAX_FILES_PER_PART 4096 //每个分区所支持最大创建的文件数
#define BITS_PER_SECTOR 4096    //每个扇区的位数
#define SECTOR_SIZE 512         //每个扇区的大小
#define BLOCK_SIZE SECTOR_SIZE  //块大小，可以等于好几个扇区

#define MAX_PATH_LEN 512

//文件类型
enum file_types
{
    FT_UNKNOWN,  // 不支持的文件类型
    FT_REGULAR,  // 普通文件
    FT_DIRECTORY // 目录
};

//打开的文件选项
enum oflags
{
    O_RDONLY,   //只读
    O_WRONLY,   //只写
    O_RDWR,     //读写
    O_CREAT = 4 //创建
};

//文件读写位置偏移量
enum whence
{
    SEEK_START = 1, //以文件开始处为基准
    SEEK_CUR,       //以文件当前位置为基准
    SEEK_END        //以文件末尾为基准
};

struct path_search_record
{
    char searched_path[MAX_PATH_LEN]; //查找过程中的父路径
    struct dir *parent_dir;           //父目录
    enum file_types file_type;        //找到的文件类型
};

struct stat
{
    uint32_t stat_inode_no;         //inode编号
    uint32_t stat_size;             //尺寸
    enum file_types stat_file_type; //文件类型
};

extern struct partition *current_partition;
void filesystem_init(void);
int32_t path_depth_cnt(char *pathname);
char *path_parse(char *pathname, char *name_store);
int32_t sys_open(const char *pathname, uint8_t flags);
int32_t path_depth_cnt(char *pathname);
int32_t sys_close(int32_t fd);
int32_t sys_write(int32_t fd, const void *buf, uint32_t count);
int32_t sys_read(int32_t fd, void *buf, uint32_t count);

int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence);
int32_t sys_unlink(const char *pathname);
struct dir *sys_opendir(const char *name);
int32_t sys_closedir(struct dir *dir);
struct dir_entry *sys_readdir(struct dir *dir);
void sys_rewinddir(struct dir *dir);
int32_t sys_mkdir(const char *pathname);
int32_t sys_rmdir(const char *pathname);
char *sys_getcwd(char *buf, uint32_t size);
int32_t sys_chdir(const char *path);
int32_t sys_stat(const char *path, struct stat *stat_buf);
int32_t sys_read(int32_t fd, void *buf, uint32_t count);
void sys_putchar(char input_char);
