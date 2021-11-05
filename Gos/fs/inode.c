#include "inode.h"
#include "fs.h"
#include "file.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"

//存储inode在磁盘中位置
struct inode_position
{
    bool two_sec;      //inode是否跨扇区
    uint32_t sec_lba;  //inode所在扇区号
    uint32_t off_size; //inode在所在扇区内的字节偏移量
};

/**
 * @brief 获取inode所在的扇区和扇区内的偏移量
 * @param part 扇区地址
 * @param inode_no 带查找的inode的编号
 * @param inode_pos 输出参数，记录此inode的磁盘中的信息
 */
static void inode_locate(struct partition *part, uint32_t inode_no, struct inode_position *inode_pos)
{

    ASSERT(inode_no < 4096);
    //得到inode table在磁盘中的偏移量
    uint32_t inode_table_lba = part->su_block->inode_table_lba;

    uint32_t inode_size = sizeof(struct inode);
    //得到inode在表中的偏移量
    uint32_t off_size = inode_no * inode_size;
    //得到相对应的扇区号
    uint32_t off_sec = off_size / 512;

    //得到对应的扇区起始偏移量
    uint32_t off_size_in_sec = off_size % 512;
    //判断是否要跨扇区
    uint32_t left_in_sec = 512 - off_size_in_sec;
    if (left_in_sec < inode_size)
    {
        //若剩余空间小于inode的大小，那么就是跨越了两个扇区
        inode_pos->two_sec = true;
    }
    else
    {
        inode_pos->two_sec = false;
    }

    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}

/**
 * @brief 将inode写入到分区part
 * @param part 分区指针
 * @param inode 待同步的inode指针
 * @param io_buf 主调函数提供的操作缓冲区
 */
void inode_sync(struct partition *part, struct inode *inode, void *io_buf)
{
    //# 1.定位该inode在磁盘中的位置
    uint8_t inode_no = inode->inode_no;
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);

    //# 2.清空磁盘中不需要的三项 inode_open_cnts write_deny inode_tag
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));
    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(struct inode));
    pure_inode.inode_open_cnts = 0;
    pure_inode.write_deny = false;
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    //# 3.写入磁盘中
    char *inode_buf = (char *)io_buf;
    if (inode_pos.two_sec)
    {
        //读出两个删除中的数据，然后拼接写入
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
        //再写入原来的位置
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else
    {
        //读出两个删除中的数据，然后拼接写入
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        //再写入原来的位置
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}

/**
 * @brief 根据inode节点号返回inode节点
 * @param part 分区指针
 * @param inode_no inode号
 * @return 这个inode节点号代表的inode
 */
struct inode *inode_open(struct partition *part, uint32_t inode_no)
{
    //# 1.遍历链表，找到inode节点号之后返回
    struct list_elem *elem = part->open_inodes.head.next;
    struct inode *inode_found;
    while (elem != &part->open_inodes.tail)
    {
        inode_found = elem2entry(struct inode, inode_tag, elem);
        if (inode_found->inode_no == inode_no)
        {
            inode_found->inode_open_cnts++;
            return inode_found;
        }
        elem = elem->next;
    }

    //# 2.在内存中没有，那就去磁盘中打开
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);

    struct task_struct *current_thread = running_thread();
    uint32_t *current_pagedir_back = current_thread->pgdir;
    current_thread->pgdir = NULL;

    //分配内存空间,因为pgdir=NULL，所以inode实际上是在内核内存池被分配
    inode_found = (struct inode *)sys_malloc(sizeof(struct inode));
    current_thread->pgdir = current_pagedir_back;

    char *inode_buf;
    if (inode_pos.two_sec)
    {
        //两个扇区大小
        inode_buf = (char *)sys_malloc(1024);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else
    {
        //两个扇区大小
        inode_buf = (char *)sys_malloc(512);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
    memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));

    //# 3.加入内核维护的inode队列中
    list_push(&part->open_inodes, &inode_found->inode_tag);
    inode_found->inode_open_cnts = 1;

    sys_free(inode_buf);
    return inode_found;
}

/**
 * @brief 关闭inode或者减少inode打开次数
 * @param inode 待关闭的inode
 * @note 引用计数，为0才真正关闭
 * @note 必须为原子操作
 */
void inode_close(struct inode *inode)
{
    enum intr_status old_status = intr_disable();

    if (--inode->inode_open_cnts == 0)
    {
        list_remove(&inode->inode_tag);
        struct task_struct *current_thread = running_thread();
        uint32_t *current_pgdir_back = current_thread->pgdir;
        current_thread->pgdir = NULL;
        sys_free(inode);
        current_thread->pgdir = current_pgdir_back;
    }
    intr_set_status(old_status);
}

/**
 * @brief 初始化new_inode
 * @param inode_no inode节点号
 * @param new_inode 待初始化的inode节点
 */
void inode_init(uint32_t inode_no, struct inode *new_inode)
{
    new_inode->inode_no = inode_no;
    new_inode->inode_size = 0;
    new_inode->inode_open_cnts = 0;
    new_inode->write_deny = false;

    //初始化快索引数组
    uint8_t sec_idx = 0;
    while (sec_idx < 13)
    {
        new_inode->inode_sectors[sec_idx] = 0;
        sec_idx++;
    }
}

/**
 * @brief 将硬盘分区part上的inode清空
 *  
 * @param part 分区指针 
 * @param inode_no 要清空的inode的inode号
 * @param io_buf 用于缓存磁盘数据的缓冲区
 */
void inode_delete(struct partition *part, uint32_t inode_no, void *io_buf)
{
    ASSERT(inode_no < 4096);

    //# 1.获取inode_no在磁盘中的偏移量等信息
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    //# 2.清空数据
    char *inode_buf = (char *)io_buf;
    if (inode_pos.two_sec)
    {
        //跨扇区，需要先将原来硬盘的内容读出来
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
        //将inode_buf中属于此inode的部分清空
        memset((inode_buf + inode_pos.off_size), 0, sizeof(struct inode));
        //这部分清空数据覆盖原有数据
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else
    {
        //不跨扇区
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        //将inode_buf中属于此inode的部分清空
        memset((inode_buf + inode_pos.off_size), 0, sizeof(struct inode));
        //这部分清空数据覆盖原有数据
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}

/**
 * @brief 回收inode数据块和inode本身
 * 
 * @param part 分区指针
 * @param inode_no 要回收的inode的inode号
 */
void inode_release(struct partition *part, uint32_t inode_no)
{
    //# 1.获取inode信息
    struct inode *delete_inode = inode_open(part, inode_no);
    ASSERT(delete_inode->inode_no == inode_no);

    //# 2.回收inode占用的所有块
    uint8_t block_idx = 0;
    uint8_t block_cnt = 12; //块的数量
    uint32_t block_bitmap_idx = 0;
    uint32_t all_blocks[140] = {0};

    //先获取12个直接块
    while (block_idx < 12)
    {
        all_blocks[block_idx] = delete_inode->inode_sectors[block_idx];
        block_idx++;
    }

    //如果一级间接表存在，先读入所有的间接块，然后释放间接表所占的扇区
    if (delete_inode->inode_sectors[12] != 0)
    {
        //间接表存在
        ide_read(part->my_disk, delete_inode->inode_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;

        //回收一级间接表块所占的扇区
        //先获取位图偏移量
        block_bitmap_idx = delete_inode->inode_sectors[12] - part->su_block->data_start_lba;
        ASSERT(block_bitmap_idx > 0);
        bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
        bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP);
    }

    //逐个回收直接块和间接块所占的空间
    block_idx = 0;
    while (block_idx < block_cnt)
    {
        //因为这里没读入间接表那个块，所以all_blocks[12] = 0
        if (all_blocks[block_idx] != 0)
        {
            block_bitmap_idx = 0;
            block_bitmap_idx = all_blocks[block_idx] - part->su_block->data_start_lba;
            ASSERT(block_bitmap_idx > 0);
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
            bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP);
        }
        block_idx++;
    }

    //# 3.回收该indeo所占用的inode
    bitmap_set(&part->inode_bitmap, inode_no, 0);
    bitmap_sync(current_partition, inode_no, BLOCK_BITMAP);

    //# 4.调试用信息
    //inode分配由inode位图控制，起始不需要清空磁盘上的情况，直接就覆盖啦
    void *io_buf = sys_malloc(1024);
    inode_delete(part, inode_no, io_buf);
    sys_free(io_buf);

    inode_close(delete_inode);
}
