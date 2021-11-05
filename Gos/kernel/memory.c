#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "global.h"
#include "debug.h"
#include "string.h"
#include "sync.h"
#include "stdio.h" //TODO delete
#include "interrupt.h"

#define PG_SIZE 4096 //定义页大小

/*
 * 0xc009f000为内核主线程栈顶，0xc009e000是内核主线程的pcb
 * 一个页框大小的位图可表示128MB的内存，位图位置安排在地址0xc009a000
 * 0xc009a000~0xc009b000这样的空间大小称之为一个页框，总共4096字节，其中每一位可以
 * 表示4k大小的一个页。每字节可以表示32k大小，4096字节可以表示128MB的空间。
 * 像这样的页框到0xc009e00总共有4个，所以可以表示512MB的空间
 */
#define MEM_BITMAP_BASE 0xc009a000

/*
 * 0xc0000000表示内核开始的1G虚拟空间，由于低端1M空间有他用，
 * 所以从0xc0100000开始
 */
#define K_HEAP_START 0xc0100000

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

//内存池结构
struct pool
{
    struct bitmap pool_bitmap; //用于管理物理内存
    uint32_t phy_addr_start;   //管理的物理内存的起始地址
    uint32_t pool_size;        //本内存池字节容量
    struct lock lock;          //申请内存时互斥
};

struct arena
{
    struct mem_block_desc *desc; //与此arena关联的mem_block_desc，其实也就是元信息啦
    uint32_t cnt;                //mem_block数量
    bool large;                  //大于1024是大请求，其cnt就表示的是页框数
};

struct mem_block_desc k_block_descs[MEM_DESC_CNT]; //定义7种内存块规格

struct pool kernel_pool, user_pool; //生成内核内存池和用户内存池
struct virtual_addr kernel_vaddr;   //用来给内核分配虚拟地址

/**
 * @brief 初始化内存池
 * @param all_mem 内存容量
 */
static void mem_pool_init(uint32_t all_mem)
{
    put_str("memory pool init start...\n");
    put_str("    total memory:");
    put_int(all_mem);
    put_str("\n");
    //页表大小 = 1页的页目录表+第0和第769页目录项指向同一个页表+第769~1022目录项指向254个页表，
    //供256个页框
    uint32_t page_table_size = PG_SIZE * 256;       //记录内核所用的页目录项和页表所占用的字节大小
    uint32_t used_mem = page_table_size + 0x100000; //总共使用的内存
    uint32_t free_mem = all_mem - used_mem;
    uint16_t all_free_pages = free_mem / PG_SIZE; //剩余多少页

    //计算内核和用户分别所剩的空间大小
    uint16_t kernel_free_pages = all_free_pages / 2;
    uint16_t user_free_pages = all_free_pages - kernel_free_pages;

    //kernel bitmap的长度，以字节为单位
    uint32_t kbm_length = kernel_free_pages / 8;
    //user bitmap长度，以字节为单位
    uint32_t ubm_length = user_free_pages / 8;

    //内核内存池起始地址
    uint32_t kp_start = used_mem;
    //用户内存池起始地址
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE;

    kernel_pool.phy_addr_start = kp_start;
    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;

    user_pool.phy_addr_start = up_start;
    user_pool.pool_size = user_free_pages * PG_SIZE;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

    kernel_pool.pool_bitmap.bits = (void *)MEM_BITMAP_BASE;
    user_pool.pool_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length);

    //输出内存池信息
    put_str("    kernel_pool_bitmap_start:");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str(" kernel_pool_phy_addr_start:");
    put_int(kernel_pool.phy_addr_start);
    put_str("\n");
    put_str("    user_pool_bitmap_start:");
    put_int((int)user_pool.pool_bitmap.bits);
    put_str(" user_pool_phy_addr_start:");
    put_int(user_pool.phy_addr_start);
    put_str("\n");

    //位图置为0，内存初始化
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    //初始化锁
    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);

    //初始化内核虚拟地址的位图，按照实际物理内存大小生成数组
    //用于维护内核堆的虚拟地址，所以和内核内存池大小一致
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;

    //位图的数组指向一块未使用的内存，定位在内核内存池和用户内存池之外
    kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    kernel_vaddr.vaddr_start = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("memory pool init done!\n");
}

/**
 * @brief 内存管理部分初始化入口
 */
void mem_init()
{
    put_str("memory init statr!\n");
    //从物理地址0xb00处取值，这个是之前在loader.S中计算过的
    uint32_t mem_bytes_total = (*(uint32_t *)(0xb00));
    mem_pool_init(mem_bytes_total);
    block_desc_init(k_block_descs);
    put_str("memory init done!\n");
}

/**
 * @brief 根据标记pf从内核/用户内存空间中得到pg_cnt块内存
 * @param pf pool_flags结构体，用于标记是内核空间还是用户空间
 * @param pg_cnt 内存块数量
 * @return 得到内存块集合的起始地址
 */
static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt)
{
    int vaddr_start = 0;
    int bit_idx_start = -1;
    uint32_t count = 0;

    //内核内存池中申请空间
    if (pf == PF_KERNEL)
    {
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1)
        {
            put_str("bitmap_scan error!!!!\n");
            return NULL;
        }

        //将位图置为使用状态
        while (count < pg_cnt)
        {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + count++, 1);
        }
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
    }
    else //用户进程池中申请内存
    {
        //TODO 用户内存池，将来实现用户进程再补充
        struct task_struct *current_thread = running_thread();
        bit_idx_start = bitmap_scan(&current_thread->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1)
        {
            return NULL;
        }

        while (count < pg_cnt)
        {
            //内存位图置为被使用
            bitmap_set(&current_thread->userprog_vaddr.vaddr_bitmap, bit_idx_start + count++, 1);
        }
        //获得起始地址
        vaddr_start = current_thread->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;

        //保证不进入内核区域
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
    }
    return (void *)vaddr_start;
}

/**
 * @brief 得到虚拟地址vaddr对应的页表项pte的指针
 * @param vaddr 虚拟地址
 * @return 返回能够访问vaddr所在的pte的虚拟地址
 */
uint32_t *pte_ptr(uint32_t vaddr)
{
    //1.首先处理高10位的页目录项pde索引，得到页表物理地址
    //2.处理中间10位的pte索引，进而处理器得到普通物理页的物理地址
    //3.把低12位作为普通物理页内的物理偏移地址，相加得到最终的物理地址

    //注：页目录项的1023个元素就是页目录项的地址，1023十六进制表示是0x3ff，移到高10位便是0xffc00000
    uint32_t *pte = (uint32_t *)(0xffc00000 +
                                 ((vaddr & 0xffc00000) >> 10) +
                                 PTE_IDX(vaddr) * 4);
    return pte;
}

/**
 * @brief 根据虚拟地址vaddr得到所在的页目录项pde的指针
 * @param vaddr 虚拟地址
 * @return 返回能够访问该pde的虚拟地址
 */
uint32_t *pde_ptr(uint32_t vaddr)
{
    uint32_t *pde = (uint32_t *)((0xfffff000) + PDE_IDX(vaddr) * 4);
    return pde;
}

/**
 * @brief 再m_pool中分配一个物理页并返回该物理页的地址
 * @param m_pool 一个内存池(内核/用户)的地址
 * @return 成功返回该物理页地址，失败返回NULL
 */
static void *palloc(struct pool *m_pool)
{
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1); //找到一个未使用的物理页
    if (bit_idx == -1)
    {
        return NULL;
    }

    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1); //将此位置为1
    uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
    return (void *)page_phyaddr;
}

/**
 * @brief 页表中添加虚拟地址vaddr和物理地址page_phyaddr的映射
 * @param vaddr 虚拟地址
 * @param page_phyaddr 物理地址
 */
static void page_table_add(void *vaddr_, void *page_phyaddr_)
{
    uint32_t vaddr = (uint32_t)vaddr_;
    uint32_t page_phyaddr = (uint32_t)page_phyaddr_;

    //得到页表项和页表的地址
    uint32_t *pde = pde_ptr(vaddr);
    uint32_t *pte = pte_ptr(vaddr);

    //先判断目录项的P位是否为1,代表是否存在再内存之中
    if (*pde & 0x00000001)
    {
        //断言此页表项不为1
        ASSERT(!(*pte & 0x00000001));
        if (!(*pte & 0x00000001))
        {
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
        else
        {
            PANIC("pte repeat!");
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
    }
    else
    {
        //不存在就申请页目录项内存，建立映射
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);

        *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);

        //清空页表项
        memset((void *)((int)pte & 0xfffff000), 0, PG_SIZE);
        ASSERT(!(*pte & 0x00000001));
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
    }
}

/**
 * @brief 从pf所代表的内存池(内核/用户)分配pg_cnt个内存块，并返回起始地址，其中会建立页表映射
 * @param pf 代表是内核内存池还是用户内存池
 * @param pg_cnt 要分配的内存块的数量
 * @return 分配的内存的起始地址，失败返回NULL
 */
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt)
{
    //2000表示最大内存数量，我随便写的
    ASSERT(pg_cnt > 0 && pg_cnt < 2000);
    void *vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL)
    {
        return NULL;
    }

    uint32_t vaddr = (uint32_t)vaddr_start;
    uint32_t cnt = pg_cnt;
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

    while (cnt-- > 0)
    {
        void *page_phyaddr = palloc(mem_pool);
        if (page_phyaddr == NULL)
        {
            return NULL;
        }
        page_table_add((void *)vaddr, page_phyaddr);
        vaddr += PG_SIZE; //下一个虚拟页
    }
    return vaddr_start;
}

/**
 * @brief 在内核内存池中申请pg_cnt个内存块，返回地址
 * @param pg_cnt 申请内存块的数量
 * @return 成功返回起始地址，失败返回NULL
 */
void *get_kernel_pages(uint32_t pg_cnt)
{
    get_lock(&kernel_pool.lock);
    void *vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL)
    {
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    abandon_lock(&kernel_pool.lock);
    return vaddr;
}

/**
 * @brief 在用户内存池中申请pg_cnt个内存块，返回地址
 * @param pg_cnt 申请内存块的数量
 * @return 成功返回起始地址，失败返回NULL
 * @note 由于可能造成多线程访问，所以需要加锁
 */
void *get_user_pages(uint32_t pg_cnt)
{
    get_lock(&user_pool.lock);
    void *vaddr = malloc_page(PF_USER, pg_cnt);
    memset(vaddr, 0, pg_cnt * PG_SIZE);
    abandon_lock(&user_pool.lock);
    return vaddr;
}

/**
 * @brief 建立虚拟地址vaddr和物理地址在页表中的映射关系
 * @param pf 表示是内核内存池还是用户内存池
 * @param vaddr 虚拟地址
 * @return 输入的虚拟地址
 */
void *get_a_page(enum pool_flags pf, uint32_t vaddr)
{
    //根据pf标记选择哪个内存池
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

    get_lock(&mem_pool->lock);
    struct task_struct *current_thread = running_thread();
    int32_t bit_idx = -1;
    if (current_thread->pgdir != NULL && pf == PF_USER)
    {
        //得到这个vaddr是进程页表第几项，即下标
        bit_idx = (vaddr - current_thread->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        //标记为已使用
        bitmap_set(&current_thread->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    else if (current_thread->pgdir == NULL && pf == PF_KERNEL)
    {
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    else
    {
        PANIC("get_a_page: not allow kernel malloc userspace or user alloc kernelspace!\n");
    }

    void *page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL)
    {
        return NULL;
    }

    page_table_add((void *)vaddr, page_phyaddr);
    abandon_lock(&mem_pool->lock);
    return (void *)vaddr;
}

/**
 * @brief 通过传入一个虚拟地址，查表得到它的物理地址
 * @param vaddr 虚拟地址
 * @return 物理地址
 */
uint32_t addr_v2p(uint32_t vaddr)
{
    uint32_t *pte = pte_ptr(vaddr);
    //物理页起始地址+物理页内偏移量
    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

/**
 * @brief 初始化内存描述符块的元信息
 * @param desc_array 待初始化的内存描述块数组
 */
void block_desc_init(struct mem_block_desc *desc_array)
{
    uint16_t desc_idx;        //数组下标
    uint16_t block_size = 16; //定义最小规格块大小16进制

    for (desc_idx = 0; desc_idx < MEM_DESC_CNT; desc_idx++)
    {
        desc_array[desc_idx].block_size = block_size;

        //初始化其中内存块数量
        desc_array[desc_idx].blocks_per_arena = (PG_SIZE - sizeof(struct arena)) / block_size;
        list_init(&desc_array[desc_idx].free_list);
        block_size *= 2;
    }
}

/**
 * @brief 返回are内存仓库中第idx个内存块的首地址
 * @param are 内存仓库地址
 * @param idx 内存块的下标
 * @return are内存仓库中第idx个内存块的首地址
 */
static struct mem_block *arena2block(struct arena *are, uint32_t idx)
{
    return (struct mem_block *)((uint32_t)are + sizeof(struct arena) + idx * are->desc->block_size);
}

/**
 * @brief 返回内存块block所在的arena地址
 * @param block 内存块的地址
 * @return arena内存仓库的地址
 */
static struct arena *block2arena(struct mem_block *block)
{
    //block & 0xfffff000 可以得到页表的地址
    return (struct arena *)((uint32_t)block & 0xfffff000);
}

/**
 * @brief 在堆中申请size字节大小的内存
 * @param size 申请的字节大小
 * @return 内存的首地址
 */
void *sys_malloc(uint32_t size)
{
    enum pool_flags PF;
    struct pool *mem_pool;
    uint32_t pool_size;
    struct mem_block_desc *desc;
    struct task_struct *current_thread = running_thread();

    if (current_thread->pgdir == NULL)
    {
        //内核线程
        PF = PF_KERNEL;
        pool_size = kernel_pool.pool_size;
        mem_pool = &kernel_pool;
        desc = k_block_descs;
    }
    else
    {
        //用户进程
        PF = PF_USER;
        pool_size = user_pool.pool_size;
        mem_pool = &user_pool;
        desc = current_thread->u_block_desc;
    }

    //如果申请内存不在内存池容量范围内，直接返回NULL
    if (!(size > 0 && size < pool_size))
    {
        return NULL;
    }

    struct arena *are;
    struct mem_block *block;
    get_lock(&mem_pool->lock);
    if (size > 1024)
    {
        //得到要分配的页数
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE);

        are = malloc_page(PF, page_cnt);
        if (are != NULL)
        {
            memset(are, 0, page_cnt * PG_SIZE);
            are->desc = NULL;
            are->cnt = page_cnt;
            are->large = true;
            abandon_lock(&mem_pool->lock);
            return (void *)(are + 1);
        }
        else
        {
            abandon_lock(&mem_pool->lock);
            return NULL;
        }
    }
    else
    {
        uint8_t desc_idx;
        for (desc_idx = 0; desc_idx < MEM_DESC_CNT; desc_idx++)
        {
            if (size <= desc[desc_idx].block_size)
            {
                break;
            }
        }

        //如果没有可用内存了
        if (list_empty(&desc[desc_idx].free_list))
        {
            are = malloc_page(PF, 1);
            if (are == NULL)
            {
                abandon_lock(&mem_pool->lock);
                return NULL;
            }
            memset(are, 0, PG_SIZE);

            are->desc = &desc[desc_idx];
            are->large = false;
            are->cnt = desc[desc_idx].blocks_per_arena;
            uint32_t block_idx;

            enum intr_status old_status = intr_disable();
            for (block_idx = 0; block_idx < desc[desc_idx].blocks_per_arena; block_idx++)
            {
                block = arena2block(are, block_idx);
                ASSERT(!elem_find(&are->desc->free_list, &block->free_elem));
                list_append(&are->desc->free_list, &block->free_elem);
            }
            intr_set_status(old_status);
        }

        //开始分配内存块
        block = elem2entry(struct mem_block, free_elem, list_pop(&(desc[desc_idx].free_list)));
        memset(block, 0, desc[desc_idx].block_size);

        are = block2arena(block);
        abandon_lock(&mem_pool->lock);
        return (void *)block;
    }
}

/**
 * @brief 将物理地址pg_phyaddr回收到物理内存池
 * @param pg_phyaddr 物理地址
 */
void pfree(uint32_t pg_phyaddr)
{
    struct pool *mem_pool;
    uint32_t bit_idx = 0;
    if (pg_phyaddr >= user_pool.phy_addr_start)
    {
        //用户物理内存池
        mem_pool = &user_pool;
        //得到是第几个页
        bit_idx = (pg_phyaddr - user_pool.phy_addr_start) / PG_SIZE;
    }
    else
    {
        //内核物理内存池
        mem_pool = &kernel_pool;
        bit_idx = (pg_phyaddr - kernel_pool.phy_addr_start) / PG_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0);
}

/**
 * @brief 去掉页表中vaddr的映射，只去掉pte就行
 * @param vaddr 虚拟地址
 */
static void page_table_pte_remove(uint32_t vaddr)
{
    uint32_t *pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1; //页表项pte的p位置为0
    //下面的命令更新页表高速缓存tlb，这里只用更新vaddr对应的页表项就可以了
    asm volatile("invlpg %0" ::"m"(vaddr)
                 : "memory");
}

/**
 * @brief 在虚拟地址池中释放vaddr起始的pg_cnt个虚拟页地址
 * @param pf 标记是内核还是用户虚拟地址池
 * @param vaddr_  起始地址
 * @param pg_cnt 虚拟地址页的个数
 */
static void vaddr_remove(enum pool_flags pf, void *vaddr_, uint32_t pg_cnt)
{
    uint32_t bit_idx_start = 0;
    uint32_t vaddr = (uint32_t)vaddr_;
    uint32_t cnt = 0;

    if (pf == PF_KERNEL)
    {
        //移出内核的pte
        //得到具体位图信息
        bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt)
        {
            //位图置0，表示释放内存
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    }
    else
    {
        //用户虚拟内存池
        struct task_struct *current_thread = running_thread();
        bit_idx_start = (vaddr - current_thread->userprog_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt)
        {
            //位图置0，表示释放内存
            bitmap_set(&current_thread->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    }
}

/**
 * @brief 释放以虚拟地址vaddr起始的cnt个物理页框
 * @param pf 内核还是用户的标记
 * @param vaddr_ 虚拟地址
 * @param pg_cnt 要释放物理页框的数量
 * @note 内存回收三步：     ① 调用pfree清空物理地址位图中的相应位，
 *                          ② 再调用page_table_pte_remove删除页表
 *                          ③ 清除虚拟地址位图中的相应位
 */
void mfree_page(enum pool_flags pf, void *vaddr_, uint32_t pg_cnt)
{
    uint32_t pg_phyaddr;
    uint32_t vaddr = (int32_t)vaddr_;
    uint32_t page_cnt = 0;
    ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);

    pg_phyaddr = addr_v2p(vaddr); //获得物理地址

    //0x102000 表示的是低端1M的物理内存
    ASSERT((pg_phyaddr % PG_SIZE) == 0 && pg_phyaddr >= 0x102000);

    if (pg_phyaddr >= user_pool.phy_addr_start)
    {
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt)
        {
            vaddr += PG_SIZE;
            pg_phyaddr = addr_v2p(vaddr);

            //确保物理地址属于用户物理内存池
            ASSERT((pg_phyaddr % PG_SIZE) == 0 && pg_phyaddr >= user_pool.phy_addr_start);

            //归还物理地址和虚拟地址
            pfree(pg_phyaddr);
            page_table_pte_remove(vaddr);
            page_cnt++;
        }

        vaddr_remove(pf, vaddr_, pg_cnt);
    }
    else
    {
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt)
        {
            vaddr += PG_SIZE;
            pg_phyaddr = addr_v2p(vaddr);

            ASSERT((pg_phyaddr % PG_SIZE) == 0 && pg_phyaddr >= kernel_pool.phy_addr_start && pg_phyaddr < user_pool.phy_addr_start);
            pfree(pg_phyaddr);
            page_table_pte_remove(vaddr);
            page_cnt++;
        }
        vaddr_remove(pf, vaddr_, pg_cnt);
    }
}

/**
 * @brief 回收内存ptr
 * @param ptr 待回收的地址
 * @note 主要有两步 1.物理内存的bitmap置为1
 * @note           2. 虚拟内存的页表映射关系解除
 */
void sys_free(void *ptr)
{
    ASSERT(ptr != NULL);
    if (ptr != NULL)
    {
        enum pool_flags PF;
        struct pool *mem_pool;

        //如果是线程
        if (running_thread()->pgdir == NULL)
        {
            ASSERT((uint32_t)ptr >= K_HEAP_START);
            PF = PF_KERNEL;
            mem_pool = &kernel_pool;
        }
        else
        {
            PF = PF_USER;
            mem_pool = &user_pool;
        }

        get_lock(&mem_pool->lock);
        struct mem_block *block = ptr;
        struct arena *are = block2arena(block);

        ASSERT(are->large == 0 || are->large == 1);
        if (are->desc == NULL && are->large == true)
        {
            mfree_page(PF, are, are->cnt);
        }
        else
        {
            list_append(&are->desc->free_list, &block->free_elem);

            if (++are->cnt == are->desc->blocks_per_arena)
            {
                uint32_t block_idx;
                for (block_idx = 0; block_idx < are->desc->blocks_per_arena; block_idx++)
                {
                    struct mem_block *block = arena2block(are, block_idx);
                    ASSERT(elem_find(&are->desc->free_list, &block->free_elem));
                    list_remove(&block->free_elem);
                }
                mfree_page(PF, are, 1);
            }
        }
        abandon_lock(&mem_pool->lock);
    }
}

/**
 * @brief 得到一页大小的vaddr，针对fork时虚拟地址位图无需操作的情况.主要是分配物理内存，然后建立物理内存和虚拟地址的映射关系
 * 
 * @param pf 内存池标记
 * @param vaddr 虚拟地址
 * @return void* 成功返回虚拟地址，失败返回null
 */
void *get_one_page_without_operate_vaddr_bitmap(enum pool_flags pf, uint32_t vaddr)
{
    //根据pf判断具体时需要对哪个内存池操作
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

    get_lock(&mem_pool->lock);
    void *page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL)
    {
        abandon_lock(&mem_pool->lock);
        return NULL;
    }
    page_table_add((void *)vaddr, page_phyaddr);
    abandon_lock(&mem_pool->lock);
    return (void *)vaddr;
}

