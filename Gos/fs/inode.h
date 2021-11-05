#pragma once

#include "stdint.h"
#include "list.h"
#include "ide.h"

struct inode
{
    uint32_t inode_no;        //inode编号
    uint32_t inode_size;      //当inode表示目录，其表示目录项下文件大小和
                              //若为文件，则表示文件大小
    uint32_t inode_open_cnts; //记录此文件被打开的次数
    bool write_deny;          //写文件标志，因为不能同时写

    uint32_t inode_sectors[13]; //数据块指针,0~11表示直接块，12表示一级间接块指针
                                //简单实现一下，不需要13的二级间接块指针
                                //也不需要14的三级间接块指针

    struct list_elem inode_tag; //在list链表中的标志位,这个list表示已打开的inode列表
};

void inode_init(uint32_t inode_no, struct inode *new_inode);
void inode_close(struct inode *inode);
struct inode *inode_open(struct partition *part, uint32_t inode_no);
void inode_sync(struct partition *part, struct inode *inode, void *io_buf);
void inode_delete(struct partition *part, uint32_t inode_no, void *io_buf);
void inode_release(struct partition *part, uint32_t inode_no);