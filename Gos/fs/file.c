#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "string.h"
#include "thread.h"
#include "global.h"

struct file file_table[MAX_FILE_OPEN];

/**
 * @brief 从文件表file_table中获取一个空闲位
 * @return 成功返回下标，失败返回-1
 */
int32_t get_free_slot_in_global(void)
{
    uint32_t fd_idx = 3;
    for (fd_idx = 3; fd_idx < MAX_FILE_OPEN; fd_idx++)
    {
        if (file_table[fd_idx].fd_inode == NULL)
        {
            break;
        }
    }
    if (fd_idx == MAX_FILE_OPEN)
    {
        printk("no unused global fd!\n");
        return -1;
    }
    return fd_idx;
}

/**
 * @brief 将全局描述符下标安装到进程或者线程自己的文件描述符数组中
 * @param global_fd_idx 全局描述符下标
 * @return 成功返回下标，失败返回-1
 * @note 其实也就是建立映射关系
 */
int32_t pcb_fd_install(int32_t global_fd_idx)
{
    struct task_struct *current_thread = running_thread();
    uint8_t local_fd_ix = 3;
    //# 1.找到自己的可用的第一个文件描述符,建立映射关系
    for (local_fd_ix = 3; local_fd_ix < MAX_FILES_OPEN_PER_PROC; local_fd_ix++)
    {
        if (current_thread->fd_table[local_fd_ix] == -1)
        {
            current_thread->fd_table[local_fd_ix] = global_fd_idx;
            break;
        }
    }

    if (local_fd_ix == MAX_FILES_OPEN_PER_PROC)
    {
        printk("no unused local fd!\n");
        return -1;
    }
    return local_fd_ix;
}

/**
 * @brief 分配一个inode节点，返回inode节点的节点号
 * @param part 分区指针
 * @return 成功返回节点号，失败返回-1
 */
int32_t inode_bitmap_alloc(struct partition *part)
{
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
    if (bit_idx == -1)
    {
        return -1;
    }
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
}

/**
 * @brief 分配一个扇区
 * @return 成功返回扇区地址，失败返回NULL
 */
int32_t block_bitmap_alloc(struct partition *part)
{
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if (bit_idx == -1)
    {
        return -1;
    }
    bitmap_set(&part->block_bitmap, bit_idx, 1);
    return (part->su_block->data_start_lba + bit_idx);
}

/**
 * @brief 将内存中bitmap第bit_idx位所在的512字节同步到硬盘
 * @param part 分区指针
 * @param bit_idx 位索引
 * @param btmp 位图类型
 */
void bitmap_sync(struct partition *part, uint32_t bit_idx, uint8_t btmp)
{
    uint32_t off_sec = bit_idx / 4096;        //本inode节点索引相对于位图的扇区偏移量
    uint32_t off_size = off_sec * BLOCK_SIZE; //本inode节点索引 相对于位图的字节偏移量

    uint32_t sec_lba;    //扇区起始地址
    uint8_t *bitmap_off; //位图中偏移

    //同步inode_bitmap和block_bitmap
    switch (btmp)
    {
    case INODE_BITMAP:
        sec_lba = part->su_block->inode_bitmap_lba + off_sec;
        bitmap_off = part->inode_bitmap.bits + off_size;
        break;
    case BLOCK_BITMAP:
        sec_lba = part->su_block->block_bitmap_lba + off_sec;
        bitmap_off = part->block_bitmap.bits + off_size;
        break;
    default:
        break;
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}

/**
 * @brief 创建文件
 * @param parent_dir 父目录的指针
 * @param file_name  此文件的文件名
 * @param flag 文件标志位
 * @return 成功返回进程中的文件描述符，失败返回-1
 */
int32_t file_create(struct dir *parent_dir, char *file_name, uint8_t flag)
{
    //# 1.申请缓冲区
    void *io_buf = sys_malloc(1024);
    if (io_buf == NULL)
    {
        printk("file_create: sys malloc for io_buf error!\n");
        return -1;
    }

    //用于回滚各种资源
    uint8_t roll_back_step = 0;

    //# 2.新文件分配inode节点
    int32_t inode_no = inode_bitmap_alloc(current_partition); //分配一个位图为0的节点号
    if (inode_no == -1)
    {
        //申请失败
        printk("file_create: inode bitmap alloc error!\n");
        return -1;
    }

    struct inode *new_file_inode = (struct inode *)sys_malloc(sizeof(struct inode));
    if (new_file_inode == NULL)
    {
        //失败了就回滚
        printk("file_create: new file node alloc error!\n");
        roll_back_step = 1;
        goto roll_back_step;
    }
    //初始化inode信息
    inode_init(inode_no, new_file_inode);

    //# 3.内核文件表file_table中注册此文件的信息
    //得到内核文件表的一个空闲位
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1)
    {
        //失败了就回滚
        printk("file_create: file identift alloc error!\n");
        roll_back_step = 2;
        goto roll_back_step;
    }

    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    file_table[fd_idx].fd_inode->write_deny = false;

    //# 4.初始化目录项实体
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(file_name, inode_no, FT_REGULAR, &new_dir_entry);

    //# 5.同步内存数据到磁盘
    //目录项new_dir_entry写入磁盘的parent_dir目录项
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf))
    {
        //失败就回滚
        printk("file_create: sync dir_entry to disk error!\n");
        roll_back_step = 3;
        goto roll_back_step;
    }

    //父目录的inode被更改了，同步到磁盘中
    memset(io_buf, 0, 1024);
    inode_sync(current_partition, parent_dir->inode, io_buf);

    //此文件的inode同步到磁盘中
    memset(io_buf, 0, 1024);
    inode_sync(current_partition, new_file_inode, io_buf);

    //将inode_bitmap同步到磁盘
    bitmap_sync(current_partition, inode_no, INODE_BITMAP);

    //此文件加入内核打开的文件中
    list_push(&current_partition->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->inode_open_cnts = 1;

    sys_free(io_buf);
    //建立内核文件描述符和进程文件描述符的映射关系
    return pcb_fd_install(fd_idx);

    //# 6.判断回滚情况,主要工作就是释放资源
roll_back_step:
    switch (roll_back_step)
    {
    case 3:
        memset(&file_table[fd_idx], 0, sizeof(struct file));
    case 2:
        sys_free(new_file_inode);
    case 1:
        bitmap_set(&current_partition->inode_bitmap, inode_no, 0);
        break;
    default:
        break;
    }
    sys_free(io_buf);
    return -1;
}

/**
 * @brief 打开编号为inode_no的inode对应的文件
 * @param inode_no inode号
 * @param flag 文件操作标识
 * @return 成功返回文件描述符，失败返回-1
 * @note write_deny设置的时候保证原子性
 */
int32_t file_open(uint32_t inode_no, uint8_t flag)
{
    //得到一个空闲的内核文件描述符
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1)
    {
        printk("file_open: exceed max open files\n");
        return -1;
    }

    //初始化file_table中fd_idx项
    file_table[fd_idx].fd_inode = inode_open(current_partition, inode_no); //得到这个inode节点号对应的inode结点指针
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;

    bool *write_deny = &file_table[fd_idx].fd_inode->write_deny;

    //判断文件是否是关于写文件,因为不能多个进程同时写一个文件
    if (flag & O_WRONLY || flag & O_RDWR)
    {
        enum intr_status old_status = intr_get_status();
        if (!(*write_deny))
        {
            //此进程占有此文件
            *write_deny = true;
            intr_set_status(old_status);
        }
        else
        {
            intr_set_status(old_status);
            printk("file_open: can't open this file, maybe occured by other process!\n");
            return -1;
        }
    }
    //建立进程文件描述符和内核文件描述符的映射关系
    return pcb_fd_install(fd_idx);
}

/**
 * @brief 关闭文件
 * @param file 待关闭的文件指针
 * @return 成功返回0，失败返回-1
 */
int32_t file_close(struct file *file)
{
    if (file == NULL)
    {
        return -1;
    }
    file->fd_inode->write_deny = false;
    inode_close(file->fd_inode);
    file->fd_inode = NULL;
    return 0;
}

/**
 * @brief 把buf中的count个字节写入file中
 * 
 * @param file 文件指针
 * @param buf  待写入数据的缓冲区
 * @param count 写入的数据字节数
 * @return int32_t 成功返回写入的字节数，失败返回-1
 */
int32_t file_write(struct file *file, const void *buf, uint32_t count)
{
    //# 1.预先条件保证
    //保证文件写入后不超过514*140=71680字节，毕竟就支持这么大
    if ((file->fd_inode->inode_size + count) > (BLOCK_SIZE * 140))
    {
        printk("file_write: exceed max file size: 71680 Bytes!\n");
        return -1;
    }

    //申请缓冲区
    uint8_t *io_buf = sys_malloc(512);
    if (io_buf == NULL)
    {
        printk("file_write: io_buf alloc error!\n");
        return -1;
    }

    //用于存储所有块地址 12+128
    uint32_t *all_blocks = (uint32_t *)sys_malloc(BLOCK_SIZE + 48);
    if (all_blocks == NULL)
    {
        printk("file_write: all blocks alloc error!\n");
        return -1;
    }

    const uint8_t *src = buf;      //待写入数据
    uint32_t bytes_written = 0;    //已写入数据大小
    uint32_t size_left = count;    //待写入数据大小
    int32_t block_lba = -1;        //块地址
    uint32_t block_bitmap_idx = 0; //记录block_bitmap中的索引

    uint32_t sec_idx;             //扇区索引
    uint32_t sec_lba;             //扇区地址
    uint32_t sec_off_bytes;       //扇区内字节偏移量
    uint32_t sec_left_bytes;      //扇区内剩余字节量
    uint32_t chunk_size;          //每次写入硬盘的数据块大小
    int32_t indirect_block_table; //用来获取一级间接表地址
    uint32_t block_idx;           //块索引

    //# 2.判断文件是否是第一次写，如果是就为其分配一个块
    if (file->fd_inode->inode_sectors[0] == 0)
    {
        block_lba = block_bitmap_alloc(current_partition);
        if (block_lba == -1)
        {
            printk("file_write: block_bitmap all error!\n");
            return -1;
        }

        file->fd_inode->inode_sectors[0] = block_lba;

        //同步此分配块到磁盘
        block_bitmap_idx = block_lba - current_partition->su_block->data_start_lba;
        ASSERT(block_bitmap_idx != 0);
        bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP);
    }

    //# 3.计算将要占用的扇区数
    //写入count个字节前，该文件已经占用的块数
    uint32_t file_has_used_blocks = file->fd_inode->inode_size / BLOCK_SIZE + 1;

    //存储count字节后，该文件占用的块数
    uint32_t file_will_use_blocks = (file->fd_inode->inode_size + count) / BLOCK_SIZE + 1;
    ASSERT(file_will_use_blocks <= 140);

    //得到需要添加的块数
    uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;

    //# 4.将文件用到的地址收集到all_blocks中
    //# @note两种情况 add_blocks = 0和不为0
    if (add_blocks == 0)
    {
        //不用添加新扇区就直接写了
        if (file_will_use_blocks <= 12)
        {
            //在直接块中添加
            block_idx = file_has_used_blocks - 1;
            //指向最后一个有数据的扇区
            all_blocks[block_idx] = file->fd_inode->inode_sectors[block_idx];
        }
        else
        {
            //间接块
            ASSERT(file->fd_inode->inode_sectors[12] != 0);
            indirect_block_table = file->fd_inode->inode_sectors[12];
            //读出所有间接块的地主之
            ide_read(current_partition->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }
    else
    {
        /**
         * @note 三种情况
         *  1. 12个直接块够用了
         *  2. 有的数据在12个直接块内，有的数据将使用间接块
         *  3. 新数据占据间接块
         */
        if (file_will_use_blocks <= 12)
        {
            //1. 12个直接块够用了
            block_idx = file_has_used_blocks - 1;
            ASSERT(file->fd_inode->inode_sectors[block_idx] != 0);
            all_blocks[block_idx] = file->fd_inode->inode_sectors[block_idx];

            //再将未来要用的扇区分配好之后写入all_blocks中
            block_idx = file_has_used_blocks; //指向第一个要分配的新扇区
            //不断处理要分配的新扇区
            while (block_idx < file_will_use_blocks)
            {
                block_lba = block_bitmap_alloc(current_partition);
                if (block_lba == -1)
                {
                    printk("file_write: block_bitmap alloc error!\n");
                    return -1;
                }

                //确保尚未分配扇区地址
                ASSERT(file->fd_inode->inode_sectors[block_idx] == 0);
                file->fd_inode->inode_sectors[block_idx] = all_blocks[block_idx] = block_lba;

                //同步到硬盘
                block_bitmap_idx = block_lba - current_partition->su_block->data_start_lba;
                bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP);
                block_idx++; //分配下一个新扇区
            }
        }
        else if (file_has_used_blocks <= 12 && file_will_use_blocks > 12)
        {
            //2. 有的数据在12个直接块内，有的数据将使用间接块
            block_idx = file_has_used_blocks - 1;
            all_blocks[block_idx] = file->fd_inode->inode_sectors[block_idx];

            //创建一级间接块表
            block_lba = block_bitmap_alloc(current_partition);
            if (block_lba == -1)
            {
                printk("file_write: block_bitmap alloc error!\n");
                return -1;
            }

            //确保一级间接块表不存在
            ASSERT(file->fd_inode->inode_sectors[12] == 0);
            indirect_block_table = file->fd_inode->inode_sectors[12] = block_lba;

            //第一个未使用的块
            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks)
            {
                block_lba = inode_bitmap_alloc(current_partition);
                if (block_lba == -1)
                {
                    printk("file_write: block_bitmap alloc error!\n");
                    return -1;
                }
                if (block_idx < 12)
                {
                    //直接块
                    //确保尚未分配扇区
                    ASSERT(file->fd_inode->inode_sectors[block_idx] == 0);

                    file->fd_inode->inode_sectors[block_idx] = all_blocks[block_idx] = block_lba;
                }
                else
                {
                    //先写进数据，待分配完再同步到磁盘
                    all_blocks[block_idx] = block_lba;
                }

                //每分配一个块就将位图同步到硬盘
                block_bitmap_idx = block_lba - current_partition->su_block->data_start_lba;
                bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP);

                block_idx++; //下一个扇区
            }
            //同步一级间接块表到磁盘
            ide_write(current_partition->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
        else if (file_has_used_blocks > 12)
        {
            //3. 新数据占据间接块
            //确保一级间接表已经被分配了
            ASSERT(file->fd_inode->inode_sectors[12] != 0);

            //获取一级间接表的地址
            indirect_block_table = file->fd_inode->inode_sectors[12];
            //获取所有间接块地址
            ide_read(current_partition->my_disk, indirect_block_table, all_blocks + 12, 1);

            //获取第一个未使用的块
            block_idx = file_has_used_blocks;

            while (block_idx < file_will_use_blocks)
            {
                block_lba = block_bitmap_alloc(current_partition);
                if (block_lba == -1)
                {
                    printk("file_write: block_bitmap alloc error!\n");
                    return -1;
                }

                all_blocks[block_idx] = block_lba;
                block_idx++;

                //同步到硬盘
                block_bitmap_idx = block_lba - current_partition->su_block->data_start_lba;
                bitmap_sync(current_partition, block_bitmap_idx, BLOCK_BITMAP);
            }
            //同步all_blocks到磁盘
            ide_write(current_partition->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }

    //# 5.下面开始写数据
    bool first_write_block = true; //磁盘中扇区含有剩余空间的块标识
    file->fd_pos = file->fd_inode->inode_size - 1;
    while (bytes_written < count)
    {
        memset(io_buf, 0, BLOCK_SIZE);
        sec_idx = file->fd_inode->inode_size / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_inode->inode_size % BLOCK_SIZE;
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;

        //判断此次写入硬盘的数据大小
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
        if (first_write_block)
        {
            ide_read(current_partition->my_disk, sec_lba, io_buf, 1);
            first_write_block = false;
        }

        memcpy(io_buf + sec_off_bytes, src, chunk_size);
        ide_write(current_partition->my_disk, sec_lba, io_buf, 1);
        printk("file write at lba: 0x%x about 0x%x bytes\n", sec_lba, chunk_size);

        src += chunk_size;                        //放入下一个待写入的数据
        file->fd_inode->inode_size += chunk_size; //更新文件大小
        file->fd_pos += chunk_size;
        bytes_written += chunk_size;
        size_left -= chunk_size;
    }
    //file_inode数据同步到磁盘中
    inode_sync(current_partition, file->fd_inode, io_buf);
    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_written;
}

/**
 * @brief 从文件file读取count个字节到buf中
 * 
 * @param file 文件指针
 * @param buf 存储读出数据的缓冲区
 * @param count 读出的字节数
 * @return int32_t 成功返回读出的字节数，失败返回-1
 */
int32_t file_read(struct file *file, void *buf, uint32_t count)
{
    uint8_t *buf_dst = (uint8_t *)buf; //设置输出目的地
    uint32_t size = count;             //要读取的字节数
    uint32_t size_left = size;         //剩余待读取的字节数

    //# 1.如果要读取的字节数+文件偏移量是要大于文件总共的字节数的,那就有多少读多少
    if ((file->fd_pos + count) > file->fd_inode->inode_size)
    {
        size = file->fd_inode->inode_size - file->fd_pos;
        size_left = size;
        if (size == 0)
        {
            //文件到文件尾就没有啥要读取的了
            return -1;
        }
    }

    //# 2.申请一个缓冲区以及存放inode表的all_blocks
    uint8_t *io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL)
    {
        printk("file_read: io_buf allco error!\n");
        return -1;
    }

    uint32_t *all_blocks = (uint32_t *)sys_malloc(BLOCK_SIZE + 48);
    if (all_blocks == NULL)
    {
        printk("file_read: all_blocks alloc error!\n");
        return -1;
    }

    uint32_t block_read_start_idx = file->fd_pos / BLOCK_SIZE;        //数据块的起始地址
    uint32_t block_read_end_idx = (file->fd_pos + size) / BLOCK_SIZE; //数据块的终止地址
    uint32_t read_blocks = block_read_start_idx - block_read_end_idx; //要读取的数据块数量
    ASSERT(block_read_start_idx < 139 && block_read_end_idx < 139);   //保证这两个值都在文件管理范围之内

    int32_t indirect_block_table; //一级间接表指针
    uint32_t block_idx;           //获取待读取的块地址

    //# 4.开始构建all_blocks
    if (read_blocks == 0)
    {
        //同一扇区内读数据，不涉及到跨扇区
        ASSERT(block_read_end_idx == block_read_start_idx);

        if (block_read_end_idx < 12)
        {
            //保证在直接块内读数据
            //那么其实就是在block_read_end_idx指向的数据块内读数据
            block_idx = block_read_end_idx;
            all_blocks[block_idx] = file->fd_inode->inode_sectors[block_idx];
        }
        else
        {
            //在间接块内读数据
            //先获取间接表的指针信息
            indirect_block_table = file->fd_inode->inode_sectors[12];
            //填充间接块信息
            ide_read(current_partition->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }
    else
    {
        //这次涉及到跨扇区读取信息了
        /**
         * @brief 分三种情况
         * 1.起始块和终止块均属于直接块
         * 2.起始块是直接块，终止块是间接块
         * 3.起始块和间接块都是间接块
         */

        //1.起始块和终止块均属于直接块
        if (block_read_end_idx < 12)
        {
            block_idx = block_read_start_idx;
            while (block_idx <= block_read_end_idx)
            {
                all_blocks[block_idx] = file->fd_inode->inode_sectors[block_idx];
                block_idx++;
            }
        }
        else if (block_read_start_idx < 12 && block_read_end_idx >= 12)
        {
            //2.起始块是直接块，终止块是间接块
            //先写入直接块部分到all_blocks
            block_idx = block_read_start_idx;
            while (block_idx < 12)
            {
                all_blocks[block_idx] = file->fd_inode->inode_sectors[block_idx];
                block_idx++;
            }

            ASSERT(file->fd_inode->inode_sectors[12] != 0);

            indirect_block_table = file->fd_inode->inode_sectors[12];
            ide_read(current_partition->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
        else
        {
            //3.起始块和间接块都是间接块
            ASSERT(file->fd_inode->inode_sectors[12] != 0);
            indirect_block_table = file->fd_inode->inode_sectors[12];
            ide_read(current_partition->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }

    //# 5.开始读取数据了
    uint32_t sec_idx;        //扇区索引
    uint32_t sec_lba;        //扇区地址
    uint32_t sec_off_bytes;  //扇区内字节偏移量
    uint32_t sec_left_bytes; //扇区内剩余字节量
    uint32_t chunk_size;     //每次写入硬盘的数据块大小
    uint32_t bytes_read = 0; //已读取的数据量
    while (bytes_read < size)
    {
        sec_idx = file->fd_pos / BLOCK_SIZE;         //得到起始扇区的索引
        sec_lba = all_blocks[sec_idx];               //得到扇区的地址
        sec_off_bytes = file->fd_pos % BLOCK_SIZE;   //得到此扇区中的偏移量信息
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes; //得到这个扇区中剩余待读取的字节数
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;

        memset(io_buf, 0, BLOCK_SIZE);
        ide_read(current_partition->my_disk, sec_lba, io_buf, 1);
        memcpy(buf_dst, io_buf + sec_off_bytes, chunk_size);

        buf_dst += chunk_size;
        file->fd_pos += chunk_size;
        bytes_read += chunk_size;
        size_left -= chunk_size;
    }

    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_read;
}
