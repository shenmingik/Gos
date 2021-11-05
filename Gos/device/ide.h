#pragma once
#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "sync.h"
#include "super_block.h"

// * @brief 分区结构体
struct partition
{
    uint32_t start_lba;           //起始扇区
    uint32_t sec_cnt;             //扇区数
    struct disk *my_disk;         //分区所属的硬盘
    struct list_elem part_tag;    //队列中的标记
    char name[8];                 //分区名称
    struct super_block *su_block; //本分区的超级块
    struct bitmap block_bitmap;   //块位图
    struct bitmap inode_bitmap;   //i节点位图
    struct list open_inodes;      //本分区打开的i节点队列
};

// * @brief 磁盘结构
struct disk
{
    char name[8];                    //磁盘名称
    struct ide_channel *my_channel;  //此硬盘归属哪个ide通道
    uint8_t dev_no;                  //主硬盘0，从硬盘1
    struct partition prim_parts[4];  //主分区
    struct partition logic_parts[8]; //逻辑分区
};

// * @brief ata通道结构
struct ide_channel
{
    char name[8]; //本ata通道名称

    /*
     * 主通道的命令寄存器端口是0x1F0~0x1F7,控制寄存器是0x3F6
     * 从通道命令寄存器端口是0x170~0x177,控制寄存器是0x376
     */
    uint16_t port_base;         //本通道起始端口号
    uint8_t irq_no;             //本通道所用的中断号
    struct lock lock;           //通道锁
    bool expecting_intr;        //表示当前通道是否在等待硬盘中断
    struct semaphore disk_done; //用于阻塞、唤醒驱动程序
    struct disk devices[2];     //一个通道会连接一个主盘和一个从盘
};

void ide_init();
void ide_read(struct disk *hd, uint32_t lba, void *buff, uint32_t sec_cnt);
void intr_hd_handler(uint8_t irq_no);
extern uint8_t channel_cnt;
extern struct ide_channel channels[];
extern struct list partition_list;
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt);