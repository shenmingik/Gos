#include "tss.h"
#include "stdint.h"
#include "global.h"
#include "string.h"
#include "print.h"

#define PG_SIZE 4096

struct tss
{
    uint32_t backlink;  //上一个任务的TSS指针，其实也就是段选择子
    uint32_t *esp0;
    uint32_t *ss0;
    uint32_t *esp1;
    uint32_t ss1;
    uint32_t *esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t (*eip)(void);
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;           //ldt选择子
    uint32_t trace;         
    uint32_t io_base;       //IO位图在tss中偏移地址
};

//实例化tss
static struct tss tss;

/*
 * @brief 更新tss中esp0字段的值为pthread的地址
 * @param pthread 待指定的线程
 */
void update_tss_esp(struct task_struct* pthread)
{
    tss.esp0 = (uint32_t *)((uint32_t)pthread + PG_SIZE);   //得到线程的其实地址
}


/*
 * @brief 创建gdt描述符并初始化
 * @param desc_addr 段基址
 * @param limit 段界限大小
 * @param attr_low 段描述信息低8位
 * @param attr_high 段描述信息高8位
 * @return 返回一个gdt描述符
 */
static struct gdt_desc make_gdt_desc(uint32_t* desc_addr,uint32_t limit,uint8_t attr_low,uint8_t attr_high)
{
    uint32_t desc_base = (uint32_t)desc_addr;
    struct gdt_desc desc;

    desc.limit_low_word = limit & 0x0000ffff;
    desc.limit_high_attr_high = (((limit & 0x000f0000) >> 16) + (uint8_t)(attr_high));
    desc.base_low_word = desc_base & 0x0000ffff;
    desc.base_mid_byte = ((desc_base & 0x00ff0000) >> 16);
    desc.base_high_byte = desc_base >> 24;
    desc.attr_low_byte = (uint8_t)(attr_low);
    return desc;
}

/*
 * @brief tss 初始化
 */
void tss_init()
{
    put_str("tss_init statr...\n");
    uint32_t tss_size = sizeof(tss);
    memset(&tss, 0, tss_size);

    tss.ss0 = SELECTOR_K_STACK;
    tss.io_base = tss_size;

    //gdt的段基址位0x900，把tss放到第一个选择子的位置也就是0x920
    *((struct gdt_desc *)0xc0000920) = make_gdt_desc((uint32_t *)&tss, tss_size - 1, TSS_ATTR_LOW, TSS_ATTR_HIGH);

    //gdt中添加dpl为3的数据段和代码段描述符
    *((struct gdt_desc *)0xc0000928) = make_gdt_desc((uint32_t *)0, 0xfffff, GDT_CODE_ATTR_LOW_DPL3, GDT_ATTR_HIGH);
    *((struct gdt_desc *)0xc0000930) = make_gdt_desc((uint32_t *)0, 0xfffff, GDT_DATA_ATTR_LOW_DPL3, GDT_ATTR_HIGH);

    //lgdt 16位表界限&32位表的起始地址
    //(8 * 7 - 1) 为表界限值
    uint64_t gdt_operand = ((8 * 7 - 1) | ((uint64_t)(uint32_t)0xc0000900 << 16));

    //加载gdt和tss
    asm volatile("lgdt %0" ::"m"(gdt_operand));
    asm volatile("ltr %w0" ::"r"(SELECTOR_TSS));
    put_str("tss init and ltr load done!\n");
}
