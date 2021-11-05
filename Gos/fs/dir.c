#include "dir.h"
#include "stdint.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "stdio-kernel.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"
struct dir root_dir;

/**
 * @brief 打开根目录
 * @param part 分区指针
 */
void open_root_dir(struct partition *part)
{
    root_dir.inode = inode_open(part, part->su_block->root_inode_no);
    root_dir.dir_pos = 0;
}

/**
 * @brief 在分区part上打开inode节点为inode_no的目录并返回目录指针
 * @param part 分区指针
 * @param inode_no inode节点号
 * @return 返回dir目录指针
 */
struct dir *dir_open(struct partition *part, uint32_t inode_no)
{
    struct dir *pdir = (struct dir *)sys_malloc(sizeof(struct dir));
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

/**
 * @brief 在part分区的pdir目录内寻找名为name的文件或目录
 * @param part 分区指针
 * @param pdir 目录指针
 * @param name 目录名称
 * @param dir_e 输出参数，存储找到的目录项
 * @return 成功返回true，失败返回false
 */
bool search_dir_entry(struct partition *part, struct dir *pdir, const char *name, struct dir_entry *dir_e)
{
    uint32_t block_cnt = 140; //12个直接快+128个一级块

    //12个直接块48，128个一级块128*4=512
    uint32_t *all_blcoks = (uint32_t *)sys_malloc(48 + 512);
    if (all_blcoks == NULL)
    {
        printk("search dir entry:sysmalloc error!\n");
        return false;
    }

    //初始化直接块,里面的内容为扇区地址
    uint32_t block_idx = 0;
    for (block_idx = 0; block_idx < 12; block_idx++)
    {
        all_blcoks[block_idx] = pdir->inode->inode_sectors[block_idx];
    }
    block_idx = 0;

    //初始化间接块
    if (pdir->inode->inode_sectors[12] != 0)
    {
        //存在一级间接块表
        ide_read(part->my_disk, pdir->inode->inode_sectors[12], all_blcoks[12], 1);
    }

    uint8_t *buf = (uint8_t *)sys_malloc(SECTOR_SIZE);
    struct dir_entry *pdir_e = (struct dir_entry *)buf; //指向目录项的指针
    uint32_t dir_entry_size = part->su_block->dir_entry_size;
    uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size; //1扇区中可容纳的目录项的个数

    while (block_idx < block_cnt)
    {
        //为0表示快中无数据，继续寻找
        if (all_blcoks[block_idx] == 0)
        {
            block_idx++;
            continue;
        }
        //有数据，读出来判断是不是想要的
        ide_read(part->my_disk, all_blcoks[block_idx], buf, 1);

        uint32_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entry_cnt)
        {
            //找到了就复制整个目录项
            if (!strcmp(pdir_e->filename, name))
            {
                memcpy(dir_e, pdir_e, dir_entry_size);
                sys_free(buf);
                sys_free(all_blcoks);
                return true;
            }
            dir_entry_idx++;
            pdir_e++;
        }
        block_idx++;
        pdir_e = (struct dir_entry *)buf;
        memset(buf, 0, SECTOR_SIZE);
    }
    sys_free(buf);
    sys_free(all_blcoks);
    return false;
}

/**
 * @brief 关闭dir指针所指向的目录
 * @param dir 目录指针
 */
void dir_close(struct dir *dir)
{
    if (dir == &root_dir)
    {
        return;
    }
    inode_close(dir->inode);
    sys_free(dir);
}

/**
 * @brief 在内存中初始化目录项pdir_e
 * @param filename 目录项的名称
 * @param inode_no inode节点号
 * @param file_type 文件类型
 * @param pdir_e 目录指针
 */
void create_dir_entry(char *filename, uint32_t inode_no, uint8_t file_type, struct dir_entry *pdir_e)
{
    ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);

    //初始化目录项
    memcpy(pdir_e->filename, filename, strlen(filename));
    pdir_e->inode_no = inode_no;
    pdir_e->file_type = file_type;
}

/**
 * @brief 将目录项pdir_e写入父目录parent_dir中，io_buf由主调函数提供
 * @param parent_dir 父目录
 * @param pdir_e 当前目录项
 * @param io_buf 缓冲区，主调函数提供
 * @return 成功返回true，失败返回false
 */
bool sync_dir_entry(struct dir *parent_dir, struct dir_entry *pdir_e, void *io_buf)
{
    struct inode *dir_inode = parent_dir->inode;
    uint32_t dir_size = dir_inode->inode_size;                             //父目录的大小
    uint32_t dir_entry_size = current_partition->su_block->dir_entry_size; //目录项大小

    ASSERT(dir_size % dir_entry_size == 0);

    uint32_t dir_entrys_per_sec = (512 / dir_entry_size); //1扇区可以容纳的完整目录项数
    int32_t block_lba = -1;

    //# 将该目录的所有扇区地址存入all_blocks
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0};

    //直接块
    while (block_idx < 12)
    {
        //目录项的12个直接块地址收集到all_blocks中
        all_blocks[block_idx] = dir_inode->inode_sectors[block_idx];
        block_idx++;
    }

    struct dir_entry *dir_e = (struct dir_entry *)io_buf; //dir_e用来在io_buf中遍历目录项
    int32_t block_bitmap_idx = -1;

    //# 开始遍历所有块寻找目录项空位，若没有则申请新扇区存储新目录项
    block_idx = 0;
    while (block_idx < 140)
    {
        block_bitmap_idx = -1;
        //如果未使用
        if (all_blocks[block_idx] == 0)
        {
            block_lba = block_bitmap_alloc(current_partition);
            if (block_lba == -1)
            {
                printk("alloc block bitmap for sync dir entry failed!\n");
                return false;
            }

            //每次分配一个块就同步一次
            block_bitmap_idx = block_lba - current_partition->su_block->data_start_lba; //得到距离起始的偏移量
            ASSERT(block_bitmap_idx != -1);
            bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP); //同步到硬盘

            block_bitmap_idx = -1;
            //直接块
            if (block_idx < 12)
            {
                dir_inode->inode_sectors[block_idx] = all_blocks[block_idx] = block_lba;
            }
            else if (block_idx == 12)
            {
                //一级块指针
                dir_inode->inode_sectors[12] = block_lba;
                block_lba = -1;
                block_lba = block_bitmap_alloc(current_partition);
                if (block_lba == -1)
                {
                    //失败
                    block_bitmap_idx = dir_inode->inode_sectors[12] - current_partition->su_block->data_start_lba;
                    bitmap_set(&current_partition->block_bitmap, block_bitmap_idx, 0); //释放空间
                    dir_inode->inode_sectors[12] = 0;
                    printk("alloc block bitmap for sync dir failed!\n");
                    return false;
                }

                //每分配一次就同步一次
                block_bitmap_idx = block_lba - current_partition->su_block->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP);

                all_blocks[12] = block_lba;
                ide_write(current_partition->my_disk, dir_inode->inode_sectors[12], all_blocks + 12, 1);
            }
            else
            {
                //间接块
                all_blocks[block_idx] = block_lba;
                //写入间接块指针所在的扇区，建立映射关系
                ide_write(current_partition->my_disk, dir_inode->inode_sectors[12], all_blocks + 12, 1);
            }

            memset(io_buf, 0, 512);
            memcpy(io_buf, pdir_e, dir_entry_size);
            ide_write(current_partition->my_disk, all_blocks[block_idx], io_buf, 1);
            dir_inode->inode_size += dir_entry_size;
            return true;
        }

        ide_read(current_partition->my_disk, all_blocks[block_idx], io_buf, 1);
        uint8_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entrys_per_sec)
        {
            if ((dir_e + dir_entry_idx)->file_type == FT_UNKNOWN)
            {
                //未知文件
                memcpy(dir_e + dir_entry_idx, pdir_e, dir_entry_size);
                ide_write(current_partition->my_disk, all_blocks[block_idx], io_buf, 1);

                dir_inode->inode_size += dir_entry_size;
                return true;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    printk("directory is full!\n");
    return false;
}

/**
 * @brief 把分区part目录pdir中编号为inode_no的目录项删除
 * 
 * @param part 分区指针
 * @param pdir 目录项指针
 * @param inode_no 要删除的inode的编号
 * @param io_buf 主调函数提供的缓冲区
 * @return true 成功返回
 * @return false 失败返回
 */
bool delete_dir_entry(struct partition *part, struct dir *pdir, uint32_t inode_no, void *io_buf)
{
    //获取父目录项的dir指针信息
    struct inode *dir_inode = pdir->inode;
    uint32_t block_idx = 0;
    uint32_t all_blocks[140] = {0};

    //# 1.收集all_blcoks
    while (block_idx < 12)
    {
        //收集直接块信息
        all_blocks[block_idx] = dir_inode->inode_sectors[block_idx];
        block_idx++;
    }

    //收集间接表信息
    if (dir_inode->inode_sectors[12] != 0)
    {
        ide_read(part->my_disk, dir_inode->inode_sectors[12], all_blocks + 12, 1);
    }

    //# 2.收集目录项信息
    //目录项在存储时保证不会跨扇区
    uint32_t dir_entry_size = part->su_block->dir_entry_size;     //目录项大小
    uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size); //每个扇区存储的目录项数量
    struct dir_entry *dir_e = (struct dir_entry *)io_buf;
    struct dir_entry *dir_entry_found = NULL;
    uint8_t dir_entry_idx;
    uint8_t dir_entry_count;         //目录项的数量
    bool is_dir_first_block = false; //是否是目录的第一个块

    //# 3.遍历所有块，寻找目录项
    block_idx = 0;
    while (block_idx < 140)
    {
        is_dir_first_block = false;
        //跨过第12项间接表
        if (all_blocks[block_idx] == 0)
        {
            block_idx++;
            continue;
        }
        dir_entry_idx = dir_entry_count = 0;
        memset(io_buf, 0, SECTOR_SIZE);

        //读取扇区，获得目录项
        ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1);

        //遍历这个扇区读出来的目录项，统计该扇区的目录项数量以及是否有待删除的目录项
        while (dir_entry_idx < dir_entrys_per_sec)
        {
            if ((dir_e + dir_entry_idx)->file_type != FT_UNKNOWN)
            {
                if (!strcmp((dir_e + dir_entry_idx)->filename, "."))
                {
                    is_dir_first_block = true;
                }
                else if (strcmp((dir_e + dir_entry_idx)->filename, ".") && strcmp((dir_e + dir_entry_idx)->filename, ".."))
                {
                    dir_entry_count++;
                    if ((dir_e + dir_entry_idx)->inode_no == inode_no)
                    {
                        //找到此inode节点
                        ASSERT(dir_entry_found == NULL);
                        dir_entry_found = dir_e + dir_entry_idx;
                    }
                }
            }
            dir_entry_idx++;
        }

        //这个扇区没找到就下个扇区接着找
        if (dir_entry_found == NULL)
        {
            block_idx++;
            continue;
        }

        //这个扇区找到了目标文件
        ASSERT(dir_entry_count >= 1);
        //# 4.除目录第一个扇区外，若该扇区上只有该目录项自己，则将整个扇区回收
        if (dir_entry_count == 1 && !is_dir_first_block)
        {
            //位图中回收该块
            uint32_t block_bitmap_idx = all_blocks[block_idx] - part->su_block->data_start_lba;
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
            bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP);

            //将块地址从数组inode_sectors或者索引表中去掉
            if (dir_entry_count < 12)
            {
                //去除直接块信息
                dir_inode->inode_sectors[block_idx] = 0;
            }
            else
            {
                uint32_t indirect_block_count = 0;
                uint32_t indirect_block_idx = 12;

                //处理间接块
                while (indirect_block_idx++ < 140)
                {
                    //计算间接块数量
                    //TODO 有问题的点
                    if (all_blocks[indirect_block_idx] != 0)
                    {
                        indirect_block_count++;
                    }
                }
                ASSERT(indirect_block_count >= 1);

                if (indirect_block_count > 1)
                {
                    //不只有一个间接块
                    all_blocks[block_idx] = 0;
                    ide_write(part->my_disk, dir_inode->inode_sectors[12], all_blocks + 12, 1);
                }
                else
                {
                    //只有这个间接块，连同间接表一起回收了
                    block_bitmap_idx = dir_inode->inode_sectors[12] - part->su_block->data_start_lba;
                    bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
                    bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP);

                    dir_inode->inode_sectors[12] = 0;
                }
            }
        }
        else
        {
            //仅将该目录项清空
            memset(dir_entry_found, 0, dir_entry_size);
            ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
        }

        //更新inode节点信息并同步到硬盘
        ASSERT(dir_inode->inode_size >= dir_entry_size);
        dir_inode->inode_size -= dir_entry_size;
        memset(io_buf, 0, SECTOR_SIZE * 2);
        inode_sync(part, dir_inode, io_buf);

        return true;
    }
    return false;
}

/**
 * @brief 读取目录
 * 
 * @param dir 父目录指针
 * @return struct dir_entry* 成功返回一个目录项，失败返回null 
 */
struct dir_entry *dir_read(struct dir *dir)
{
    struct dir_entry *dir_e = (struct dir_entry *)dir->dir_buf;
    struct inode *dir_inode = dir->inode;
    uint32_t all_blocks[140] = {0};
    uint32_t block_count = 12;
    uint32_t block_idx = 0;
    uint32_t dir_entry_idx = 0;

    //# 1.收集all_blocks
    while (block_idx < 12)
    {
        all_blocks[block_idx] = dir_inode->inode_sectors[block_idx];
        block_idx++;
    }

    if (dir_inode->inode_sectors[12] != 0)
    {
        //处理间接块
        ide_read(current_partition->my_disk, dir_inode->inode_sectors[12], all_blocks + 12, 1);
        block_count = 140;
    }
    block_idx = 0;

    uint32_t cur_dir_entry_pos = 0; //当前目录项的偏移
    uint32_t dir_entry_size = current_partition->su_block->dir_entry_size;
    uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size;

    while (block_idx < block_count)
    {
        if (dir->dir_pos >= dir_inode->inode_size)
        {
            return NULL;
        }
        if (all_blocks[block_idx] == 0)
        {
            block_idx++;
            continue;
        }

        memset(dir_e, 0, SECTOR_SIZE);
        ide_read(current_partition->my_disk, all_blocks[block_idx], dir_e, 1);
        dir_entry_idx = 0;
        //遍历扇区所有目录项
        while (dir_entry_idx < dir_entrys_per_sec)
        {
            if ((dir_e + dir_entry_idx)->file_type != FT_UNKNOWN)
            {
                if (cur_dir_entry_pos < dir->dir_pos)
                {
                    cur_dir_entry_pos += dir_entry_size;
                    dir_entry_idx++;
                    continue;
                }
                ASSERT(cur_dir_entry_pos == dir->dir_pos);
                dir->dir_pos += dir_entry_size;
                //更新为下一个目录项的地址
                return dir_e + dir_entry_idx;
            }
            dir_entry_idx++; //下一个目录项
        }
        block_idx++; //下一个扇区
    }
    return NULL;
}

/**
 * @brief 判断目录是否为空
 * 
 * @param dir 目录指针
 * @return true 空返回true，非空返回false
 * @return false 
 */
bool dir_is_empty(struct dir *dir)
{
    struct inode *dir_inode = dir->inode;
    //如果只有. 和..两个目录那就是空目录
    return (dir_inode->inode_size == current_partition->su_block->dir_entry_size * 2);
}

/**
 * @brief 在父目录parent_dir中删除子目录child_dir
 * 
 * @param parent_dir 父目录指针
 * @param child_dir 子目录指针
 * @return int32_t 成功返回0，失败返回-1
 */
int32_t dir_remove(struct dir *parent_dir, struct dir *child_dir)
{
    struct inode *child_dir_inode = child_dir->inode;

    //首先断定child_dir是个空目录，空目录只有在扇区0号有. 和.. 两个内容
    int32_t block_idx = 1;
    while (block_idx < 13)
    {
        ASSERT(child_dir_inode->inode_sectors[block_idx] == 0);
        block_idx++;
    }

    void *io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL)
    {
        printk("dir_remove: io_buf alloc memory error!\n");
        return -1;
    }

    //在父目录中深处子目录所对应的目录项
    delete_dir_entry(current_partition, parent_dir, child_dir_inode->inode_no, io_buf);

    //回收inode中inode_sectors所占的扇区并同步到此磁盘
    inode_release(current_partition, child_dir_inode->inode_no);
    sys_free(io_buf);
    return 0;
}

