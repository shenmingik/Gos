#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "file.h"
#include "console.h"
#include "ioqueue.h"
#include "keyboard.h"

struct partition *current_partition; //默认情况下操作的是哪个分区

/**
 * @brief 初始化分区的元信息，创建文件系统
 * @param hd 硬盘指针
 * @param part 分区指针
 * @note    1.根据分区大小，计算分区元信息写入超级块
 * @note    2.在内存中创建超级块，将以上信息写入超级块
 * @note    3.将超级块写入磁盘
 * @note    4.将元信息写入磁盘上各自位置
 * @note    5.将根目录写入磁盘
 */
static void partition_format(struct partition *part)
{
    //block_bitmap_init,一个块大小是一扇区，用来表示磁盘的一个块被没被使用
    uint32_t boot_sector_sects = 1;                                                                        //根扇区数量,其为引导块
    uint32_t super_block_sects = 1;                                                                        //超级块数量
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);                       //inode节点位图占用的扇区数
    uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(struct inode) * MAX_FILES_PER_PART)), SECTOR_SIZE); //计算inode表所占用的扇区数

    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects; //已使用的扇区数
    uint32_t free_sects = part->sec_cnt - used_sects;                                                     //剩余扇区数

    //处理空闲块和空闲块位图，每个扇区位图可以表示4096个空闲扇区

    uint32_t block_bitmap_sects;                                     //空闲块最终占用的位图数
    block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);  //空闲块位图所占用的扇区数
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects; //真正的空闲块数量
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

    //超级块初始化
    struct super_block super_block;
    super_block.magic = 0x20000314;
    super_block.sec_cnt = part->sec_cnt;
    super_block.inode_cnt = MAX_FILES_PER_PART;
    super_block.part_lba_base = part->start_lba;

    //0号是引导块，1号是超级块
    //空闲块位图信息
    super_block.block_bitmap_lba = super_block.part_lba_base + 2;
    super_block.block_bitmap_sects = block_bitmap_sects;

    //inode节点信息
    super_block.inode_bitmap_lba = super_block.block_bitmap_lba + super_block.block_bitmap_sects;
    super_block.inode_bitmap_sects = inode_bitmap_sects;

    //inode表信息
    super_block.inode_table_lba = super_block.inode_bitmap_lba + inode_bitmap_sects;
    super_block.inode_table_sects = inode_table_sects;

    super_block.data_start_lba = super_block.inode_table_lba + super_block.inode_table_sects;
    super_block.root_inode_no = 0;
    super_block.dir_entry_size = sizeof(struct dir_entry);

    //打印分区信息
    printk("    %s info:\n", part->name);
    printk("    magic:0x%x\n", super_block.magic);
    printk("    part lba:0x%x\n", super_block.part_lba_base);
    printk("    all sectors:0x%x\n", super_block.sec_cnt);
    printk("    inode sectors count:0x%x\n", super_block.inode_cnt);

    printk("    block bitmap lba:0x%x\n", super_block.block_bitmap_lba);
    printk("    block bitmap sector count:0x%x\n", super_block.block_bitmap_sects);
    printk("    inode bitmap lba:0x%x\n", super_block.inode_bitmap_lba);
    printk("    inode bitmap sector count:0x%x\n", super_block.inode_bitmap_sects);
    printk("    inode table lba:0x%x\n", super_block.inode_table_lba);
    printk("    inode table sector count:0x%x\n", super_block.inode_table_sects);

    printk("    data lba:0x%x\n", super_block.data_start_lba);

    struct disk *hd = part->my_disk;

    //# 1.超级块写入本分区的1扇区
    ide_write(hd, part->start_lba + 1, &super_block, 1);
    printk("    super block lba:0x%x\n", part->start_lba + 1);

    //找出数据量最大的元信息，用其尺寸做存储缓冲区,这个空间太大，最好用堆存储
    uint32_t buf_size = (super_block.block_bitmap_sects >= super_block.inode_bitmap_sects ? super_block.block_bitmap_sects : super_block.inode_bitmap_sects);
    buf_size = (buf_size >= super_block.inode_table_sects ? buf_size : super_block.inode_table_sects) * SECTOR_SIZE;

    uint8_t *buf = (uint8_t *)sys_malloc(buf_size);

    //# 2.位图初始化并写入block_bitmap_lba
    //TODO 这里可能缺 memset(buf, 0, buf_size);
    buf[0] |= 0x01; //占位,第0个空闲块是根目录
    //下面将块位图的最后一个扇区不属于空闲块的位初始为1
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;                  //最后一个字节
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;                    //最后一字节中的有效位数
    uint32_t lastat_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE); //最后一个扇区中不足扇区的其余部分

    //先把最后一字节内所有置为1
    memset(&buf[block_bitmap_last_byte], 0xff, lastat_size);
    //之后把最后一字节的有效位重新置为0
    uint8_t bit_idx = 0;
    while (bit_idx <= block_bitmap_last_bit)
    {
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
    }
    ide_write(hd, super_block.block_bitmap_lba, buf, super_block.block_bitmap_sects);

    //# 3.将inode位图初始化并写入inode_bitmap_lba
    memset(buf, 0, buf_size);
    buf[0] |= 0x1; //根目录
    ide_write(hd, super_block.inode_bitmap_lba, buf, super_block.inode_bitmap_sects);

    //# 4.将inode数组初始化闭关写入inode_table_lba
    memset(buf, 0, buf_size);
    struct inode *inode = (struct inode *)buf;
    inode->inode_size = super_block.dir_entry_size * 2; // . 和 ..这两个
    inode->inode_no = 0;                                //根目录inode号
    inode->inode_sectors[0] = super_block.data_start_lba;
    ide_write(hd, super_block.inode_table_lba, buf, super_block.inode_table_sects);

    //# 5.将根目录写入data_start_lba
    memset(buf, 0, buf_size);
    //初始化当前目录.
    struct dir_entry *pdir_entry = (struct dir_entry *)buf;
    memcpy(pdir_entry->filename, ".", 1);
    pdir_entry->inode_no = 0;
    pdir_entry->file_type = FT_DIRECTORY;
    pdir_entry++;
    //初始化当前目录..
    memcpy(pdir_entry->filename, "..", 2);
    pdir_entry->inode_no = 0;
    pdir_entry->file_type = FT_DIRECTORY;

    //写入根目录项
    ide_write(hd, super_block.data_start_lba, buf, 1);

    printk("    root dir lba:0x%x\n", super_block.data_start_lba);
    sys_free(buf);
}

/**
 * @brief 在分区链表中找到名为part_name的分区，并将其指针赋值给current_partition
 * @param pelem 传入的列表元素
 * @param arg 分区名称
 * @return 无效返回值
 */
static bool mount_partition(struct list_elem *pelem, int arg)
{
    char *part_name = (char *)arg;
    struct partition *part = elem2entry(struct partition, part_tag, pelem);
    if (!strcmp(part->name, part_name))
    {
        current_partition = part;
        struct disk *hd = current_partition->my_disk;

        // 存储从硬盘读来的超级块
        struct super_block *super_block_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);

        //在内存中创建分区current_partition的超级块
        current_partition->su_block = (struct super_block *)sys_malloc(sizeof(struct super_block));
        if (current_partition->su_block == NULL)
        {
            PANIC("alloc memory failed!\n");
        }

        //读入超级块
        memset(super_block_buf, 0, SECTOR_SIZE);
        ide_read(hd, current_partition->start_lba + 1, super_block_buf, 1);

        //把super_block_buf中的超级块信息复制到分区的超级块super_block中
        memcpy(current_partition->su_block, super_block_buf, sizeof(struct super_block));

        //# 1.硬盘中的块位图读取内存
        current_partition->block_bitmap.bits = (uint8_t *)sys_malloc(super_block_buf->block_bitmap_sects * SECTOR_SIZE);
        if (current_partition->block_bitmap.bits == NULL)
        {
            PANIC("alloc memory failed!\n");
        }

        current_partition->block_bitmap.btmp_bytes_len = super_block_buf->block_bitmap_sects * SECTOR_SIZE;
        //硬盘上读入块位图到分区的block_bitmap.bits
        ide_read(hd, super_block_buf->block_bitmap_lba, current_partition->block_bitmap.bits, super_block_buf->block_bitmap_sects);

        //# 2.硬盘中读入inode位图到内存
        current_partition->inode_bitmap.bits = (uint8_t *)sys_malloc(super_block_buf->inode_bitmap_sects * SECTOR_SIZE);
        if (current_partition->inode_bitmap.bits == NULL)
        {
            PANIC("alloc memory failed!\n");
        }

        current_partition->inode_bitmap.btmp_bytes_len = super_block_buf->inode_bitmap_sects * SECTOR_SIZE;
        //硬盘上读入块位图到分区的block_bitmap.bits
        ide_read(hd, super_block_buf->inode_bitmap_lba, current_partition->inode_bitmap.bits, super_block_buf->inode_bitmap_sects);

        list_init(&current_partition->open_inodes);
        printk("mount %s done!\n", part_name);

        //无关代码,只是为了和list_traversal相和
        return true;
    }
    return false;
}

/**
 * @brief 磁盘上搜索文件系统，若没有则格式化分区并创建文件系统
 */
void filesystem_init()
{
    uint8_t channel_no = 0;
    uint8_t dev_no = 0;
    uint8_t part_idx = 0;

    struct super_block *super_block_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);
    if (super_block_buf == NULL)
    {
        PANIC("alloc memory failed!\n");
    }
    printk("searching file system...\n");
    while (channel_no < channel_cnt)
    {
        dev_no = 0;
        while (dev_no < 2)
        {
            //跳过hd60M
            if (dev_no == 0)
            {
                dev_no++;
                continue;
            }

            struct disk *hd = &channels[channel_no].devices[dev_no];
            struct partition *part = hd->prim_parts;
            //4主分区+8逻辑分区
            while (part_idx < 12)
            {
                //逻辑分区
                if (part_idx == 4)
                {
                    part = hd->logic_parts;
                }

                if (part->sec_cnt != 0)
                {
                    //分区存在
                    memset(super_block_buf, 0, SECTOR_SIZE);
                    ide_read(hd, part->start_lba + 1, super_block_buf, 1);
                    if (super_block_buf->magic == 0x20000314)
                    {
                        //已经初始化过了
                        printk("%s has filesystem!\n", part->name);
                    }
                    else
                    {
                        //没有初始化过
                        printk("formating %s's partition %s info:\n", hd->name, part->name);
                        partition_format(part);
                    } //其他文件系统按照无文件系统处理
                }
                part_idx++;
                part++; //下一个文曲
            }
            dev_no++; //下一个磁盘
        }
        channel_no++; //下一通道
    }
    sys_free(super_block_buf);

    char default_part[8] = "sdb1";
    list_traversal(&partition_list, mount_partition, (int)default_part);

    //将当前分区的根目录打卡
    open_root_dir(current_partition);

    //初始化文件表
    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILE_OPEN)
    {
        file_table[fd_idx++].fd_inode = NULL;
    }
}

/**
 * @brief 解析最上层路径名称 /home/ik --> home
 * @param pathname 待解析的路径名
 * @param name_store 存储解析结果
 */
char *path_parse(char *pathname, char *name_store)
{
    if (pathname[0] == '/')
    {
        //根目录不需要单独解析
        //路径中出现1个或多个连续的字符'/',将这些'/'跳过,如"///a/b" */
        while (*(++pathname) == '/')
            ;
    }

    while (*pathname != '/' && *pathname != 0)
    {
        *name_store++ = *pathname++;
    }

    if (pathname[0] == 0)
    {
        //若路径字符串为空则返回NULL
        return NULL;
    }
    return pathname;
}

/**
 * @brief 返回路径深度 /home/ik = 2
 * @param pathname 路径名称
 * @return 路径的深度
 */
int32_t path_depth_cnt(char *pathname)
{
    ASSERT(pathname != NULL);
    char *path = pathname;
    char name[MAX_FILE_NAME_LEN]; // 用于path_parse的参数做路径解析
    uint32_t depth = 0;

    //解析路径,从中拆分出各级名称
    path = path_parse(path, name);
    while (name[0])
    {
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        if (path)
        {
            // 如果p不等于NULL,继续分析路径
            path = path_parse(path, name);
        }
    }
    return depth;
}

/**
 * @brief 搜索文件pathname
 * @param pathname 要搜索的文件名
 * @param search_record 路径搜索记录指针
 * @return 找到返回其inode号，失败返回-1
 * @note 父路径会被打开，需要上层去关闭
 */
static int search_file(const char *pathname, struct path_search_record *search_record)
{
    //# 1.如果是根目录，直接返回已知根目录信息
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/.."))
    {
        search_record->parent_dir = &root_dir;
        search_record->file_type = FT_DIRECTORY;
        search_record->searched_path[0] = 0; //搜索路径置为空
        return 0;
    }

    uint32_t path_len = strlen(pathname);                                 //路径名称长度
    ASSERT(pathname[0] == '/' & path_len > 1 && path_len < MAX_PATH_LEN); //确保路径格式

    char *sub_path = (char *)pathname;
    struct dir *parent_dir = &root_dir;
    struct dir_entry dir_e;

    //# 2.解析各级路径名称
    char name[MAX_FILE_NAME_LEN] = {0};
    search_record->parent_dir = parent_dir;
    search_record->file_type = FT_UNKNOWN;
    uint32_t parent_inode_no = 0; //父目录inode号

    sub_path = path_parse(sub_path, name);
    while (name[0])
    {
        ASSERT(strlen(search_record->searched_path) < 512);

        //记录已存在的父目录
        strcat(search_record->searched_path, "/");
        strcat(search_record->searched_path, name);

        //在所给目录中查找文件
        if (search_dir_entry(current_partition, parent_dir, name, &dir_e))
        {
            //找到了
            memset(name, 0, MAX_FILE_NAME_LEN);

            if (sub_path)
            {
                //获得下一个目录项
                sub_path = path_parse(sub_path, name);
            }

            if (dir_e.file_type == FT_DIRECTORY)
            {
                //如果是目录
                parent_inode_no = parent_dir->inode->inode_no;
                dir_close(parent_dir);                                    //关闭上一级父目录
                parent_dir = dir_open(current_partition, dir_e.inode_no); //当前目录为父目录
                search_record->parent_dir = parent_dir;
                continue;
            }
            else if (dir_e.file_type == FT_REGULAR)
            {
                //若是普通文件
                search_record->file_type = FT_REGULAR;
                return dir_e.inode_no;
            }
        }
        else
        {
            return -1;
        }
    }
    //执行到此处是遍历了完整的路径了
    dir_close(search_record->parent_dir);

    //保存被查找的目录的直接父目录
    search_record->parent_dir = dir_open(current_partition, parent_inode_no);
    search_record->file_type = FT_DIRECTORY;
    return dir_e.inode_no;
}

/**
 * @brief 打开或创建文件
 * @param path_name 文件路径
 * @param flags 文件操作标识信息
 * @return 成功返回文件描述符，失败返回-1
 */
int32_t sys_open(const char *path_name, uint8_t flags)
{
    //# 1.首先判断是否是目录文件，目录可不能打开
    if (path_name[strlen(path_name) - 1] == '/')
    {
        printk("sys_open: can't open dir file!\n");
        return -1;
    }

    ASSERT(flags < 7);
    int32_t fd = -1;

    //# 2.然后再找到这个文件
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    //记录目录深度
    uint32_t pathname_depth = path_depth_cnt((char *)path_name);

    //检查文件是否存在
    int inode_no = search_file(path_name, &searched_record);
    bool file_found = inode_no != -1 ? true : false;

    if (searched_record.file_type == FT_DIRECTORY)
    {
        printk("sys_open: can't open dir!\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    //检查中间路径是否存在
    //记录寻找的路径深度，如果查找/home/ik/a ，ik目录存在此变量为3，不存在则只有/home/ik = 2
    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
    if (path_searched_depth != pathname_depth)
    {
        //中间路径不存在
        printk("sys_open: lack Intermediate path!\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    //如果文件不存在且不是要创建这个文件
    if (!file_found && !(flags & O_CREAT))
    {
        printk("sys_open: file not found!\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }
    else if (file_found && (flags & O_CREAT))
    {
        //如果是文件存在且创建文件
        printk("sys_open: file %s has already exits!\n", path_name);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    switch (flags & O_CREAT)
    {
    case O_CREAT:
        printk("create files: %s\n", path_name);
        fd = file_create(searched_record.parent_dir, (strrchr(path_name, '/') + 1), flags);
        dir_close(searched_record.parent_dir);
        break;
    //其余均为打开已存在文件
    //主要有O_RDONLY、O_WRONLY、O_RWRD
    default:
        fd = file_open(inode_no, flags);
        break;
    }
    return fd;
}

/**
 * @brief 进程文件描述符转换为内核文件描述符
 * 
 * @param local_fd 进程的文件描述符
 * @return uint32_t 内核的文件描述符
 */
static uint32_t fd_local2global(uint32_t local_fd)
{
    struct task_struct *current_thread = running_thread();
    //其实也就是一个查表的过程
    int32_t global_fd = current_thread->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

/**
 * @brief 关闭文件描述符fd指向的文件
 * 
 * @param fd 文件描述符
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t sys_close(int32_t fd)
{
    int32_t ret = -1;
    if (fd > 2)
    {
        uint32_t fd_ = fd_local2global(fd);
        ret = file_close(&file_table[fd_]);
        running_thread()->fd_table[fd] = -1;
    }
    return ret;
}

/**
 * @brief 将buf中连续count个字节写入文件描述符fd所指代的文件中
 * 
 * @param fd 文件描述符
 * @param buf  待写入数据的缓冲区
 * @param count 待写入的字节数
 * @return int32_t 成功返回写入的字节数，失败返回-1
 */
int32_t sys_write(int32_t fd, const void *buf, uint32_t count)
{
    if (fd < 0)
    {
        printk("sys_write: fd error!\n");
        return -1;
    }
    if (fd == stdout_no)
    {
        //标准输出
        char temp_buf[1024] = {0};
        memcpy(temp_buf, buf, count);
        console_put_str(temp_buf);
        return count;
    }

    uint32_t global_fd = fd_local2global(fd);
    struct file *write_file = &file_table[global_fd];
    if (write_file->fd_flag & O_WRONLY || write_file->fd_flag & O_RDWR)
    {
        //判断文件的条件
        uint32_t bytes_written = file_write(write_file, buf, count);
        return bytes_written;
    }
    else
    {
        console_put_str("sys_write: not allowed to write file without O_WRONLY and O_RDWR!\n");
        return -1;
    }
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
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence)
{
    if (fd < 0)
    {
        printk("sys_lseek: fd error!\n");
        return -1;
    }

    ASSERT(whence > 0 && whence < 4);
    uint32_t global_fd = fd_local2global(fd);
    struct file *pfile = &file_table[global_fd];

    int32_t new_pos = 0; //新的偏移量
    int32_t file_size = (int32_t)pfile->fd_inode->inode_size;
    switch (whence)
    {
    case SEEK_START:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = (int32_t)pfile->fd_pos + offset;
        break;
    case SEEK_END:
        //此时offset应该为负值
        new_pos = file_size + offset;
        break;
    default:
        break;
    }

    if (new_pos < 0 || new_pos > (file_size - 1))
    {
        return -1;
    }
    pfile->fd_pos = new_pos;
    return pfile->fd_pos;
}

/**
 * @brief 删除文件（非目录）
 * 
 * @param pathname 要删除的文件路径
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t sys_unlink(const char *pathname)
{
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    //# 1.检查待删除文件是否存在
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    int inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);
    if (inode_no == -1)
    {
        //文件不存在
        printk("file: %s is not exist!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }
    if (searched_record.file_type == FT_DIRECTORY)
    {
        //文件是目录
        printk("can't delete dir with function unlink()");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    //# 2.检查是否已打开文件列表
    uint32_t file_idx = 0;
    while (file_idx < MAX_FILE_OPEN)
    {
        if (file_table[file_idx].fd_inode != NULL && (uint32_t)inode_no == file_table[file_idx].fd_inode->inode_no)
        {
            //要删除的文件被打开了
            break;
        }
        file_idx++;
    }
    if (file_idx < MAX_FILE_OPEN)
    {
        dir_close(searched_record.parent_dir);
        printk("file %s has opened, please unlink it after it be closed!\n", pathname);
        return -1;
    }
    ASSERT(file_idx == MAX_FILE_OPEN);

    //# 3.为delete_dir_entry申请缓冲区
    void *io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL)
    {
        dir_close(searched_record.parent_dir);
        printk("sys_unlink: io_buf alloc error!\n");
        return -1;
    }

    struct dir *parent_dir = searched_record.parent_dir;
    delete_dir_entry(current_partition, parent_dir, inode_no, io_buf);
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0;
}

/**
 * @brief 创建目录pathname
 * 
 * @param pathname 要创建的目录
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t sys_mkdir(const char *pathname)
{
    uint8_t rollback_step = 0; //用于回滚
    void *io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL)
    {
        printk("sys_mkdir: io_buf alloc error!\n");
        return -1;
    }

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = -1;
    //# 1.先判断这个路径是否存在
    inode_no = search_file(pathname, &searched_record);
    if (inode_no != -1)
    {
        printk("dir %s has exist!\n", pathname);
        rollback_step = 1;
        goto rollback;
    }
    else
    {
        /**
         * @brief 如果没找到需要分情况
         * 1.最终目录没找到
         * 2.中间路径不存在
         * 
         */
        uint32_t pathname_depth = path_depth_cnt((char *)pathname);
        uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
        if (pathname_depth != path_searched_depth)
        {
            //说明是没访问到全部路径，中间目录缺失
            printk("sys_mkdir: lack Intermediate path!\n");
            rollback_step = 1;
            goto rollback;
        }
    }

    struct dir *parent_dir = searched_record.parent_dir;
    //# 2.得到目录名称，不带路径的那种
    char *dirname = strrchr(searched_record.searched_path, '/') + 1;
    inode_no = inode_bitmap_alloc(current_partition);
    if (inode_no == -1)
    {
        printk("sys_mkdir: inode_no alloc error!\n");
        rollback_step = 1;
        goto rollback;
    }

    struct inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode); //初始化inode节点
    uint32_t block_bitmap_idx = 0;
    int32_t block_lba = -1;

    //# 3.为目录分配一个块，用来写入. 和 ..
    block_lba = block_bitmap_alloc(current_partition);
    if (block_lba == -1)
    {
        printk("sys_mkdir: block bitmap alloc error!\n");
        rollback_step = 2;
        goto rollback;
    }

    new_dir_inode.inode_sectors[0] = block_lba;
    //同步到硬盘
    block_bitmap_idx = block_lba - current_partition->su_block->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP);

    //# 4.将.和..写入写入目录
    memset(io_buf, 0, SECTOR_SIZE * 2);
    struct dir_entry *pdir_e = (struct dir_entry *)io_buf;
    //初始化当前目录.
    memcpy(pdir_e->filename, ".", 1);
    pdir_e->inode_no = inode_no;
    pdir_e->file_type = FT_DIRECTORY;
    pdir_e++;

    //初始化当前目录..
    memcpy(pdir_e->filename, "..", 2);
    pdir_e->inode_no = parent_dir->inode->inode_no;
    pdir_e->file_type = FT_DIRECTORY;

    //同步到磁盘
    ide_write(current_partition->my_disk, new_dir_inode.inode_sectors[0], io_buf, 1);
    new_dir_inode.inode_size = 2 * current_partition->su_block->dir_entry_size;

    //# 5.在父目录项中添加自己的目录项
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);
    memset(io_buf, 0, SECTOR_SIZE * 2);

    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf))
    {
        printk("sys_mkdir: sync dir_entry failed!\n");
        rollback_step = 2;
        goto rollback;
    }

    //# 6.父目录的inode同步到硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(current_partition, parent_dir->inode, io_buf);

    //# 7.新创建的inode同步到硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(current_partition, &new_dir_inode, io_buf);

    //# 8.inode位图同步到硬盘
    bitmap_sync(current_partition, inode_no, INODE_BITMAP);

    sys_free(io_buf);

    //关闭所创建目录的父目录
    dir_close(searched_record.parent_dir);
    return 0;

rollback:
    switch (rollback_step)
    {
    case 2:
        bitmap_set(&current_partition->inode_bitmap, inode_no, 0);

    case 1:
        dir_close(searched_record.parent_dir);
        break;

    default:
        break;
    }
    sys_free(io_buf);
    return -1;
}

/**
 * @brief 打开name所指定的目录
 * 
 * @param name 目录的路径
 * @return struct dir* 成功返回目录指针，失败返回null
 */
struct dir *sys_opendir(const char *name)
{
    ASSERT(strlen(name) < MAX_PATH_LEN);

    //根目录直接返回啦
    if (name[0] == '/' && name[1] == 0 || name[0] == '.')
    {
        return &root_dir;
    }

    //# 1.先检查待打开的目录是否存在
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(searched_record));
    int inode_no = search_file(name, &searched_record);

    struct dir *ret = NULL;
    if (inode_no == -1)
    {
        printk("dir %s has not exist!\n", name);
    }
    else
    {
        if (searched_record.file_type == FT_REGULAR)
        {
            //如果是一个普通文件
            printk("%s is not a dir!\n", name);
        }
        else if (searched_record.file_type == FT_DIRECTORY)
        {
            ret = dir_open(current_partition, inode_no);
        }
    }

    dir_close(searched_record.parent_dir);
    return ret;
}

/**
 * @brief 关闭目录
 * 
 * @param dir 目录指针
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t sys_closedir(struct dir *dir)
{
    int32_t ret = -1;
    if (dir != NULL)
    {
        dir_close(dir);
        ret = 0;
    }
    return ret;
}

/**
 * @brief 读取目录
 * 
 * @param dir 父目录指针
 * @return struct dir_entry* 成功返回一个目录项，失败返回null 
 */
struct dir_entry *sys_readdir(struct dir *dir)
{
    ASSERT(dir != NULL);
    return dir_read(dir);
}

/**
 * @brief 把目录dir的指针dir_pos置为0
 * 
 * @param dir 目录指针
 */
void sys_rewinddir(struct dir *dir)
{
    dir->dir_pos = 0;
}

/**
 * @brief 删除空目录
 * 
 * @param pathname 目录的路径信息
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t sys_rmdir(const char *pathname)
{
    //# 1.先检查待删除文件是否存在
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    int inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);

    int ret = -1; //默认返回值
    if (inode_no == -1)
    {
        printk("sys_rmdir: please check the intermediate path exist?\n");
    }
    else
    {
        if (searched_record.file_type == FT_REGULAR)
        {
            //非目录，为普通文件
            printk("sys_rmdir: %s is not a dir!\n");
        }
        else
        {
            struct dir *dir = dir_open(current_partition, inode_no);
            if (!dir_is_empty(dir))
            {
                //非空不可删除
                printk("sys_rmdir: directory %s is not empty!\n");
            }
            else
            {
                if (!dir_remove(searched_record.parent_dir, dir))
                {
                    ret = 0;
                }
            }
            dir_close(dir);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

/**
 * @brief 获得父目录的inode编号
 * 
 * @param child_inode_no 子目录的inode编号
 * @param io_buf 上层传进来的缓冲区
 * @return uint32_t 
 * @note 子目录有..可以获取父目录信息
 */
static uint32_t get_parent_dir_inode_no(uint32_t child_inode_no, void *io_buf)
{
    struct inode *child_dir_inode = inode_open(current_partition, child_inode_no);

    //子目录..中包含父目录inode信息
    uint32_t block_lba = child_dir_inode->inode_sectors[0];
    ASSERT(block_lba >= current_partition->su_block->data_start_lba);
    inode_close(child_dir_inode);

    //读取第一个扇区中的信息
    ide_read(current_partition->my_disk, block_lba, io_buf, 1);
    struct dir_entry *dir_e = (struct dir_entry *)io_buf;
    //第0个目录项是".",第1个目录项是".."
    ASSERT(dir_e[1].inode_no < 4096 && dir_e[1].file_type == FT_DIRECTORY);
    return dir_e[1].inode_no; //返回父目录的inode编号
}

/**
 * @brief 在编号的parent_inode_no的inode节点中查找编号为child_inode_no的子目录的名字，并将名字存储path
 * 
 * @param parent_inode_no 父目录inode编号
 * @param child_inode_no 子目录inode编号
 * @param path 存储路径的缓冲区
 * @param io_buf 硬盘读写缓冲区
 * @return int 成功返回0，失败返回-1
 */
static int get_child_dir_name(uint32_t parent_inode_no, uint32_t child_inode_no, char *path, void *io_buf)
{
    struct inode *parent_dir_inode = inode_open(current_partition, parent_inode_no);

    //# 1.填充all_blcoks
    uint8_t block_idx = 0;
    uint32_t all_blcoks[140] = {0};
    uint32_t block_count = 12;

    while (block_idx < 12)
    {
        //先处理直接块
        all_blcoks[block_idx] = parent_dir_inode->inode_sectors[block_idx];
        block_idx++;
    }

    if (parent_dir_inode->inode_sectors[12] != 0)
    {
        //处理间接块
        ide_read(current_partition->my_disk, parent_dir_inode->inode_sectors[12], all_blcoks + 12, 1);
        block_count = 140;
    }
    inode_close(parent_dir_inode);

    //# 2.遍历父目录中的信息，找到与子目录名称匹配的信息
    struct dir_entry *dir_e = (struct dir_entry *)io_buf;
    uint32_t dir_entry_size = current_partition->su_block->dir_entry_size;
    uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size);
    block_idx = 0;

    //遍历所有块
    while (block_idx < block_count)
    {
        if (all_blcoks[block_idx])
        {
            //如果相应块不为空，则读入相应块
            //这一步可以跳过间接表那个块
            ide_read(current_partition->my_disk, all_blcoks[block_idx], io_buf, 1);
            uint8_t dir_e_idx = 0;
            //遍历每个目录项
            while (dir_e_idx < dir_entrys_per_sec)
            {
                if ((dir_e + dir_e_idx)->inode_no == child_inode_no)
                {
                    //找到了子目录项
                    strcat(path, "/");
                    strcat(path, (dir_e + dir_e_idx)->filename);
                    return 0;
                }
                dir_e_idx++;
            }
        }
        block_idx++;
    }
    return -1;
}

/**
 * @brief 得到当前工作路径并放到buf中
 * 
 * @param buf 存放路径的缓冲区，如果其为null，则内核自己分配一个空间
 * @param size buf的大小
 * @return char* 当buf为空的时候，内核分配的空间的地址。成功返回地址，失败返回null
 */
char *sys_getcwd(char *buf, uint32_t size)
{
    ASSERT(buf != NULL);
    void *io_buf = sys_malloc(SECTOR_SIZE);
    if (io_buf == NULL)
    {
        return NULL;
    }

    struct task_struct *current_thread = running_thread();
    int32_t parent_inode_no = 0;
    int32_t child_inode_no = current_thread->cwd_inode_no; //得到当前默认工作路径
    ASSERT(child_inode_no >= 0 && child_inode_no < 4096);

    //如果当前目录是根目录，直接返回'/'
    if (child_inode_no == 0)
    {
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }

    memset(buf, 0, size);
    char full_path_reverse[MAX_PATH_LEN] = {0};

    //从子目录向上遍历，直到找到根目录
    while ((child_inode_no))
    {
        parent_inode_no = get_parent_dir_inode_no(child_inode_no, io_buf);
        //TODO 可能有问题
        if (get_child_dir_name(parent_inode_no, child_inode_no, full_path_reverse, io_buf) == -1)
        {
            //未找到子目录名字，失败退出
            sys_free(io_buf);
            return NULL;
        }
        child_inode_no = parent_inode_no;
    }
    ASSERT(strlen(full_path_reverse) <= size);

    //子目录当前情况如下ik/home/，所以需要反过来
    char *last_slash; //记录字符串中最后一个斜杠地址
    while ((last_slash = strrchr(full_path_reverse, '/')))
    {
        uint16_t len = strlen(buf);
        strcpy(buf + len, last_slash);
        *last_slash = 0;
    }

    sys_free(io_buf);
    return buf;
}

/**
 * @brief 更改当前工作目录为绝对路径path
 * 
 * @param path 新的工作路径
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t sys_chdir(const char *path)
{
    int32_t ret = -1;
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(searched_record));
    int inode_no = search_file(path, &searched_record);
    if (inode_no != -1)
    {
        if (searched_record.file_type == FT_DIRECTORY)
        {
            running_thread()->cwd_inode_no = inode_no;
            ret = 0;
        }
        else
        {
            printk("sys_chdir: %s is not a directory!\n");
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

/**
 * @brief 在stat_buf中填充文件结构相关信息，成功返回0，失败返回-1
 * 
 * @param path 
 * @param stat_buf 
 * @return int32_t 
 */
int32_t sys_stat(const char *path, struct stat *stat_buf)
{
    //如果是根目录、.、..这三个
    if (!strcmp(path, "/") || !strcmp(path, "/.") || !strcmp(path, "/.."))
    {
        stat_buf->stat_file_type = FT_DIRECTORY;
        stat_buf->stat_inode_no = 0;
        stat_buf->stat_size = root_dir.inode->inode_size;
        return 0;
    }

    int32_t ret = -1; //默认返回值
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    int inode_no = search_file(path, &searched_record);
    if (inode_no != -1)
    {
        struct inode *path_inode = inode_open(current_partition, inode_no);
        stat_buf->stat_size = path_inode->inode_size;
        inode_close(path_inode);
        stat_buf->stat_file_type = searched_record.file_type;
        stat_buf->stat_inode_no = inode_no;
        ret = 0;
    }
    else
    {
        printk("sys_stat: %s lack intermediate path!\n", path);
    }
    return ret;
}

/**
 * @brief 从文件描述符指向的文件中读出count个字节到buf中
 * 
 * @param fd 文件描述符
 * @param buf 存放读出字符的缓冲区
 * @param count 待读出的字节数
 * @return int32_t 成功返回读出的字节数，失败返回-1
 */
int32_t sys_read(int32_t fd, void *buf, uint32_t count)
{
    ASSERT(buf != NULL);
    int32_t ret = -1;
    if (fd < 0 || fd == stdout_no || fd == stderr_no)
    {
        printk("sys_read: fd wrongful!\n");
    }
    else if (fd == stdin_no)
    {
        //如果是从键盘读入
        char *buffer = buf;
        uint32_t bytes_read = 0;
        while (bytes_read < count)
        {
            *buffer = ioqueue_get_char(&keyboard_buff);
            bytes_read++;
            buffer++;
        }
        ret = (bytes_read == 0 ? -1 : (int32_t)bytes_read);
    }
    else
    {
        uint32_t global_fd = fd_local2global(fd);
        ret = file_read(&file_table[global_fd], buf, count);
    }
    return ret;
}

/**
 * @brief 向屏幕输出一个字符
 * 
 * @param input_char 待输出的字符
 */
void sys_putchar(char input_char)
{
    console_put_char(input_char);
}
