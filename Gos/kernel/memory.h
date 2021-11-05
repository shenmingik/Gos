//通过内核内存池以及用户内存池进行内存管理
#pragma once
#include "stdint.h"
#include "bitmap.h"
#include "list.h"

#define PG_P_1 1 //表示页表项或页目录P位的值为1，表示此页内存已存在
#define PG_P_0 0
#define PG_RW_R 0 //表示RW位为R，表示此页允许读、执行
#define PG_RW_W 2 //表示RW位为w，表示此页允许读、写、执行
#define PG_US_S 0 //表示US位的值为S，只允许特权级0 1 2的程序访问
#define PG_US_U 4 //表示都能访问

//虚拟内存池，用于虚拟地址管理
struct virtual_addr
{
    struct bitmap vaddr_bitmap; //虚拟地址用到的位图结构
    uint32_t vaddr_start;       //虚拟地址起始地址
};

//内存池标记，用于判断用哪个内存池
enum pool_flags
{
    PF_KERNEL = 1, //内核内存池
    PF_USER = 2    //用户内存池
};

extern struct pool kernel_pool,
    user_pool;

//内存块
struct mem_block
{
    struct list_elem free_elem;
};

struct mem_block_desc
{
    uint32_t block_size;       //内存块大小
    uint32_t blocks_per_arena; //本内存仓库arena中可容纳此mem_block的数量
    struct list free_list;     //mem_block 链表
};

#define MEM_DESC_CNT 7

void mem_init(void);
void *get_kernel_pages(uint32_t pg_cnt);
void *get_user_pages(uint32_t pg_cnt);
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt);
void malloc_init(void);
uint32_t *pte_ptr(uint32_t vaddr);
uint32_t *pde_ptr(uint32_t vaddr);
void *get_a_page(enum pool_flags pf, uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);
void block_desc_init(struct mem_block_desc *desc_array);
void *sys_malloc(uint32_t size);
void sys_free(void *ptr);
void *get_one_page_without_operate_vaddr_bitmap(enum pool_flags pf, uint32_t vaddr);
void mfree_page(enum pool_flags pf, void *vaddr_, uint32_t pg_cnt);