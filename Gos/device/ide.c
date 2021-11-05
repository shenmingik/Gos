#include "ide.h"
#include "stdio-kernel.h"
#include "debug.h"
#include "stdio.h"
#include "io.h"
#include "timer.h"
#include "list.h"
#include "interrupt.h"
#include "string.h"

//基址+偏移=各个寄存器位
#define reg_data(channel) (channel->port_base + 0) //命令块寄存器
#define reg_error(channel) (channel->port_base + 1)
#define reg_sect_cnt(channel) (channel->port_base + 2)
#define reg_lba_l(channel) (channel->port_base + 3)
#define reg_lba_m(channel) (channel->port_base + 4)
#define reg_lba_h(channel) (channel->port_base + 5)
#define reg_dev(channel) (channel->port_base + 6)
#define reg_status(channel) (channel->port_base + 7)
#define reg_cmd(channel) (reg_status(channel))
#define reg_alt_status(channel) (channel->port_base + 0x206) //控制块寄存器
#define reg_ctl(channel) reg_alt_status(channel)

//reg_alt_status寄存器的一些关键位
#define BIT_ALT_STAT_BSY 0x80  //硬盘忙
#define BIT_ALT_STAT_DRDY 0x40 //驱动ready
#define BIT_ALT_STAT_DRQ 0x8   //数据传输ready

//device 寄存器的一些关键位
#define BIT_DEV_MBS 0xa0 //第5和7位固定为1
#define BIT_DEV_LBA 0x40
#define BIT_DEV_DEV 0x10

//一些硬盘操作指令
#define CMD_IDENTIFY 0xec     //identify指令
#define CMD_READ_SECTOR 0x20  //读扇区指令
#define CMD_WRITE_SECTOR 0x30 //写扇区指令

//定义可读写的最大扇区数,80M的硬盘数据
#define max_lba ((80 * 1024 * 1024 / 512) - 1)

uint8_t channel_cnt;            //通道数量
struct ide_channel channels[2]; //两个ide通道

//用于记录总扩展分区的起始lba，初始为0，分区扫描时以此为标记
int32_t ext_lba_base = 0;

uint8_t primary_hd_no = 0;
uint8_t logic_hd_no = 0;

struct list partition_list; //分区队列

// 16字节大小的结构体，用来存放分区表项
struct partition_table_entry
{
    uint8_t bootable;   //是否可引导
    uint8_t start_head; //起始磁头号
    uint8_t start_sec;  //起始扇区号
    uint8_t strat_chs;  //起始柱面号

    uint8_t fs_type;  //分区类型
    uint8_t end_head; //结束磁头号
    uint8_t end_sec;  //结束扇区号
    uint8_t end_chs;  //结束柱面号

    uint32_t start_lba;    //本分区起始扇区的lba地址
    uint32_t sec_cnt;      //本分区的扇区数目
} __attribute__((packed)); //__attribute__((packed))：取消对齐

struct boot_sector
{
    uint8_t other[446];                              //mbr的引导代码
    struct partition_table_entry partition_table[4]; //分区表总共有四个表项
    uint16_t signature;                              //启动扇区的结束标志0x55aa
} __attribute__((packed));

/*
 * @brief 选择操纵硬盘
 * @param hd 硬盘的指针
 */
static void select_disk(struct disk *hd)
{
    //设置硬件寄存器
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    if (hd->dev_no == 1) //从盘,dev位设置为1
    {
        reg_device |= BIT_DEV_DEV;
    }
    outb(reg_dev(hd->my_channel), reg_device);
}

/*
 * @brief 向硬盘控制器写入起始扇区地址以及要读写的扇区数
 * @param hd 硬盘指针
 * @param lba 扇区起始地址
 * @param sec_cnt 扇区数
 */
static void select_sector(struct disk *hd, uint32_t lba, uint8_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    struct ide_channel *channel = hd->my_channel;

    //# 1.写入要读取的扇区数
    //若sec_cnt为0，表示写入256个扇区
    outb(reg_sect_cnt(channel), sec_cnt);

    //# 2.写入扇区号
    outb(reg_lba_l(channel), lba);                                                                       //lba的低8位
    outb(reg_lba_m(channel), lba >> 8);                                                                  //lba的8~15位
    outb(reg_lba_h(channel), lba >> 16);                                                                 //lba的16~23位
    outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | lba >> 24); //写入lba的24~27位，这四位在device寄存器中
}

/*
 * @brief 向通道channel发出命令cmd
 * @param channel 通道指针
 * @param cmd 硬盘操作命令
 */
static void cmd_out(struct ide_channel *channel, uint8_t cmd)
{
    //只要向硬盘发出命令便将此标记置为true
    channel->expecting_intr = true;
    outb(reg_cmd(channel), cmd);
}

/*
 * @brief 硬盘读入sec_cnt个扇区的数据到buf
 * @param hd 硬盘指针
 * @param buff 存放读取数据的缓冲区
 * @param sec_cnt 读取的扇区数
 */
static void read_from_sector(struct disk *hd, void *buff, uint8_t sec_cnt)
{
    uint32_t size_in_byte; //要读取的字节数
    if (sec_cnt == 0)      //当sec_cnt为0时，表示256
    {
        size_in_byte = 256 * 512;
    }
    else
    {
        size_in_byte = sec_cnt * 512;
    }
    //因为写入的单位是字，所以这里要除二啦
    insw(reg_data(hd->my_channel), buff, size_in_byte / 2);
}

/*
 * @brief 将buff中sec_cnt个扇区的数据写入硬盘
 * @param hd 硬盘指针
 * @param buff 缓冲区
 * @param sec_cnt 写入的扇区数
 */
static void write2sector(struct disk *hd, void *buff, uint8_t sec_cnt)
{
    uint32_t size_in_bytes;

    if (sec_cnt == 0)
    {
        size_in_bytes = 256 * 512;
    }
    else
    {
        size_in_bytes = sec_cnt * 512;
    }
    outsw(reg_data(hd->my_channel), buff, size_in_bytes / 2);
}

/*
 * @brief 等待硬盘30s
 * @param hd 硬盘指针
 * @return 成功返回true，失败返回false
 */
static bool busy_wait(struct disk *hd)
{
    struct ide_channel *channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000;
    while (time_limit -= 10 >= 0)
    {
        //如果status寄存器BSY位不为1
        if (!(inb(reg_status(channel)) & BIT_ALT_STAT_BSY))
        {
            //读取DRQ位的值，其为1表示硬盘已经准备好数据了
            return (inb(reg_status(channel)) & BIT_ALT_STAT_DRQ);
        }
        else //如果BSY位为1
        {
            mtime_sleep(10); //睡眠10毫秒
        }
    }
    return false;
}

/*
 * @brief 将dst中len个相邻字节交换位置后存入buff
 * @param dest 数据源
 * @param buff 存放交换后数据的缓冲区
 * @param len 要交换数据的总长度
 */
static void swap_pairs_bytes(const char *dest, char *buff, uint32_t len)
{
    uint8_t idx;
    for (idx = 0; idx < len; idx += 2)
    {
        buff[idx + 1] = *dest++;
        buff[idx] = *dest++;
    }
    buff[idx] = '\0';
}

/*
 * @brief 获得硬盘参数信息
 * @param hd 硬盘指针
 */
static void identify_disk(struct disk *hd)
{
    char id_info[512]; //临时数据存储
    select_disk(hd);   //选择磁盘
    cmd_out(hd->my_channel, CMD_IDENTIFY);

    //待硬盘处理完之后唤醒自己
    sema_down(&hd->my_channel->disk_done);

    //以下为唤醒后执行的代码
    if (!busy_wait(hd))
    {
        char error[64];
        sprintf(error, "%s identify failed!\n", hd->name);
        PANIC(error);
    }

    //读取最开始那个扇区的数据
    read_from_sector(hd, id_info, 1);

    char buf[64];
    uint8_t sn_start = 10 * 2; //序列号起始字节地址,10为字偏移量
    uint8_t sn_len = 20;       //字偏移量
    uint8_t md_start = 27 * 2; //型号起始字节地址,27为字偏移量
    uint8_t md_len = 40;

    swap_pairs_bytes(&id_info[sn_start], buf, sn_len);
    printk("    disk %s info:\n        sn_start: %s\n", hd->name, buf);
    memset(buf, 0, sizeof(buf));

    swap_pairs_bytes(&id_info[md_start], buf, md_len);
    printk("        module: %s\n", buf);

    uint32_t sectors = *(uint32_t *)&id_info[60 * 2]; //扇区数
    printk("        sectors: %d\n", sectors);
    printk("        capacity: %dMB\n", sectors * 512 / 1024 / 1024);
}

/*
 * @brief 扫描硬盘hd中地址为ext_lba的扇区中的所有分区
 * @param hd 硬盘指针
 * @param ext_lba 扩展扇区地址
 */
static void partition_scan(struct disk *hd, uint32_t ext_lba)
{
    struct boot_sector *bs = sys_malloc(sizeof(struct boot_sector));
    ide_read(hd, ext_lba, bs, 1);
    uint8_t part_idx = 0;
    struct partition_table_entry *part_table = bs->partition_table;

    //遍历四个分区表项
    while (part_idx++ < 4)
    {
        //若为扩展分区
        if (part_table->fs_type == 0x5)
        {
            if (ext_lba_base != 0)
            {
                //此时是EBR扇区，start_lba是相对于主引导扇区中的总扩展地址
                partition_scan(hd, part_table->start_lba + ext_lba_base);
            }
            else
            {
                //是第一次调用，这个时候是MBR中的内容
                ext_lba_base = part_table->start_lba;
                partition_scan(hd, part_table->start_lba);
            }
        }
        else if (part_table->fs_type != 0) //若是有效的分区类型
        {
            //主分区
            if (ext_lba == 0)
            {
                hd->prim_parts[primary_hd_no].start_lba = ext_lba + part_table->start_lba;
                hd->prim_parts[primary_hd_no].sec_cnt = part_table->sec_cnt;
                hd->prim_parts[primary_hd_no].my_disk = hd;
                list_append(&partition_list, &hd->prim_parts[primary_hd_no].part_tag);
                sprintf(hd->prim_parts[primary_hd_no].name, "%s%d", hd->name, primary_hd_no + 1);

                primary_hd_no++;
                ASSERT(primary_hd_no < 4); //0 1 2 3 四个
            }
            else
            {
                //逻辑分区
                hd->logic_parts[logic_hd_no].start_lba = ext_lba + part_table->start_lba;
                hd->logic_parts[logic_hd_no].sec_cnt = part_table->sec_cnt;
                hd->logic_parts[logic_hd_no].my_disk = hd;
                list_append(&partition_list, &hd->logic_parts[logic_hd_no].part_tag);
                sprintf(hd->logic_parts[logic_hd_no].name, "%s%d", hd->name, logic_hd_no + 5);

                logic_hd_no++;
                if (logic_hd_no >= 8)
                    return;
            }
        }
        part_table++;
    }
    sys_free(bs);
}

/*
 * @brief 打印分区信息
 * @parma pelem 分区指针
 * @param arg 占位参数
 */
static bool partition_info(struct list_elem *pelem, int arg UNUSED)
{
    struct partition *part = elem2entry(struct partition, part_tag, pelem);
    printk("        %s start_lba: 0x%x, sec_cnt: 0x%x\n", part->name, part->start_lba, part->sec_cnt);
    return false;
}

/*
 * @brief 从硬盘读取sec_cnt个扇区到buf
 * @param hd 硬盘指针
 * @param lba 扇区地址
 * @param buff 缓冲区
 * @param sec_cnt 扇区数量
 */
void ide_read(struct disk *hd, uint32_t lba, void *buff, uint32_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    get_lock(&hd->my_channel->lock);

    //1.先选择操作的硬盘
    select_disk(hd);

    uint32_t per_op_sectors;   //每次操作的扇区数
    uint32_t done_sectors = 0; //已完成的扇区数
    while (done_sectors < sec_cnt)
    {
        //每次都尽量操纵256个
        if ((done_sectors + 256) <= sec_cnt)
        {
            per_op_sectors = 256;
        }
        else
        {
            per_op_sectors = sec_cnt - done_sectors;
        }

        //2. 写入待读入的扇区数和起始扇区号
        select_sector(hd, lba + done_sectors, per_op_sectors);

        //3. 执行的命令写入reg_cmd寄存器
        cmd_out(hd->my_channel, CMD_READ_SECTOR); //准备开始读数据

        //阻塞自己，等待硬盘完成读操作之后通过中断唤醒自己
        sema_down(&hd->my_channel->disk_done);

        //4. 检测硬盘的状态是否可读
        if (!busy_wait(hd))
        {
            char error[64];
            sprintf(error, "%s read sector %d failed!\n", hd->name, lba);
            PANIC(error);
        }

        //5. 把数据从硬盘的缓冲区中读出
        read_from_sector(hd, (void *)((uint32_t)buff + done_sectors * 512), per_op_sectors);
        done_sectors += per_op_sectors;
    }
    abandon_lock(&hd->my_channel->lock);
}

/*
 * @brief 将buf中的sec_cnt个扇区数据写入硬盘
 * @param hd 硬盘指针
 * @param lba 扇区地址
 * @param buff 缓冲区
 * @param sec_cnt 扇区数量
 */
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    get_lock(&hd->my_channel->lock);

    //1.选择操作的硬盘
    select_disk(hd);
    uint32_t per_op_sectors;
    uint32_t done_sectors = 0;
    while (done_sectors < sec_cnt)
    {
        if ((done_sectors + 256) <= sec_cnt)
        {
            per_op_sectors = 256;
        }
        else
        {
            per_op_sectors = sec_cnt - done_sectors;
        }

        //2.写入待写入的扇区数和起始扇区号
        select_sector(hd, lba + done_sectors, per_op_sectors);

        //3.执行命令写入red_cmd寄存器
        cmd_out(hd->my_channel, CMD_WRITE_SECTOR);

        //4.检测硬盘状态是否可读
        if (!busy_wait(hd))
        {
            char error[64];
            sprintf(error, "%s write sector %d failed!\n", hd->name, lba);
            PANIC(error);
        }

        //5.将数据写入硬盘
        write2sector(hd, (void *)((uint32_t)buf + done_sectors * 512), per_op_sectors);

        sema_down(&hd->my_channel->disk_done);
        done_sectors += per_op_sectors;
    }
    abandon_lock(&hd->my_channel->lock);
}

/*
 * @brief 硬盘中断处理程序
 * @param irq_no 中断号
 */
void intr_hd_handler(uint8_t irq_no)
{
    ASSERT(irq_no == 0x2e || irq_no == 0x2f);
    uint8_t ch_no = irq_no - 0x2e;
    struct ide_channel *channel = &channels[ch_no];
    ASSERT(channel->irq_no == irq_no);

    if (channel->expecting_intr)
    {
        //正在等待中断
        channel->expecting_intr = false;
        //唤醒线程
        sema_up(&channel->disk_done);
        inb(reg_status(channel));
    }
}

/*
 * @brief 硬盘数据结构初始化
 * @note    1.获取硬盘数量
 * @note    2.获取通道数量 = 硬盘数量/2
 * @note    3.处理每个通道上的信息，包括起始端口和通道号
 * @note    4.获得每个通道上硬盘的信息、扫描硬盘分区信息
 */
void ide_init()
{
    printk("ide init start...\n");
    uint8_t hd_cnt = *((uint8_t *)(0x475)); //0x475BIOS规定的读取硬盘的位置

    ASSERT(hd_cnt > 0);
    list_init(&partition_list);
    channel_cnt = DIV_ROUND_UP(hd_cnt, 2); //一个通道两个硬盘，根据硬盘数量反推ide通道数
    struct ide_channel *channel;
    uint8_t channelogic_hd_no = 0;
    uint8_t dev_no = 0; //硬盘标号

    printk("harddisk info:\n");
    printk("    harddisk number: %d\n", hd_cnt);
    printk("    channle number: %d\n", channel_cnt);

    //处理每个通道上的硬盘
    while (channelogic_hd_no < channel_cnt)
    {
        channel = &channels[channelogic_hd_no];
        sprintf(channel->name, "ide%d", channelogic_hd_no);

        switch (channelogic_hd_no)
        {
        case 0:                          //主硬盘
            channel->port_base = 0x1f0;  //ide0的通道起始端口号
            channel->irq_no = 0x20 + 14; //从片上倒数第二个中断引脚
            break;
        case 1:
            channel->port_base = 0x170;
            channel->irq_no = 0x20 + 15; //从片的最后一个中断引脚
            break;
        default:
            break;
        }

        channel->expecting_intr = false;
        lock_init(&channel->lock);

        /*
         * 初始化为0，目的是向硬盘控制器请求数据后，硬盘驱动器sema_down此信号会阻塞线程
         * 直到硬盘完成后通过发中断，由中断处理程序将此信号量sema_up，唤醒线程去处理
         */
        sema_init(&channel->disk_done, 0);
        register_handler(channel->irq_no, intr_hd_handler);

        //分别获取两个硬盘的参数和分区信息
        while (dev_no < 2)
        {
            struct disk *hd = &channel->devices[dev_no];
            hd->my_channel = channel;
            hd->dev_no = dev_no;
            sprintf(hd->name, "sd%c", 'a' + channelogic_hd_no * 2 + dev_no);
            identify_disk(hd); // 获取硬盘参数
            if (dev_no != 0)
            {                          // 内核本身的裸硬盘(hd60M.img)不处理
                partition_scan(hd, 0); // 扫描该硬盘上的分区
            }
            primary_hd_no = 0;
            logic_hd_no = 0;
            dev_no++;
        }
        dev_no = 0; // 将硬盘驱动器号置0,为下一个channel的两个硬盘初始化。

        channelogic_hd_no++;
    }

    printk("\n    all partition info:\n");
    /* 打印所有分区信息 */
    list_traversal(&partition_list, partition_info, (int)NULL);
    printk("ide init done!\n");
}