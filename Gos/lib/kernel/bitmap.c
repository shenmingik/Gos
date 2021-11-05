#include "bitmap.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"

/*
 * @brief 初始化结构体bitmap为0
 * @param btmp 结构体bitmap的指针
 */
void bitmap_init(struct bitmap *btmp)
{
    memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

/*
 * @brief 判断bitmap的bit_idx位是否为1
 * @param btmp 结构体bitmap的指针
 * @param bit_idx bitmap具体的某一位
 * @return 如果为1返回true，否则返回false
 */
bool bitmap_scan_test(struct bitmap *btmp, uint32_t bit_idx)
{
    uint32_t byte_idx = bit_idx / 8; //定位到字节
    uint32_t bit_odd = bit_idx % 8;  //定位到那个字节的第几位
    return (btmp->bits[byte_idx] & (BITMAP_MASK << bit_odd));
}

/*
 * @brief 在bitmap中申请连续count个位，成功返回其起始位下标；失败，返回-1
 * @param btmp 结构体bitmap的指针
 * @param cnt 申请的位个数
 * @return 申请的起始下标，如果不够用就返回-1
 */
int bitmap_scan(struct bitmap *btmp, uint32_t cnt)
{
    uint32_t idx_byte = 0; //记录空闲位所在字节
    //略过分配过的字节
    while ((0xff == btmp->bits[idx_byte]) && (idx_byte < btmp->btmp_bytes_len))
    {
        idx_byte++;
    }

    //判读是否到末尾了
    ASSERT(idx_byte < btmp->btmp_bytes_len);
    if (idx_byte == btmp->btmp_bytes_len)
    {
        return -1;
    }

    int idx_bit = 0;
    //找这个字节第一个空闲的位
    while ((uint8_t)(BITMAP_MASK << idx_bit) & btmp->bits[idx_byte])
    {
        idx_bit++;
    }

    //现在定位到bitmap的第idx_byte个字节的第idx_bit位是空闲的
    int bit_idx_start = idx_byte * 8 + idx_bit; //定位到具体是bitmap的哪一位
    if (cnt == 1)
    {
        return bit_idx_start;
    }

    //记录还有多少位可以判断
    uint32_t bit_left = (btmp->btmp_bytes_len * 8 - bit_idx_start);
    uint32_t next_bit = bit_idx_start + 1;
    uint32_t count = 1; //记录找到的空闲位的个数
    bit_idx_start = -1;
    while (bit_left-- > 0)
    {
        if (!(bitmap_scan_test(btmp, next_bit)))
        {
            count++;
        }
        else
        {
            count = 0;
        }
        //找到了连续的cnt个空位
        if (count == cnt)
        {
            bit_idx_start = next_bit - cnt + 1;
            break;
        }
        next_bit++;
    }
    return bit_idx_start;
}

/*
 * @brief 将bitmap的bit_idx位置为value
 * @param btmp 结构体bitmap的指针
 * @param bit_idx bitmap的位下标
 * @param value 待赋给bitmap的bit_idx位的值
 */
void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value)
{
    ASSERT((value == 0) || (value == 1));
    uint32_t byte_idx = bit_idx / 8;
    uint32_t bit_odd = bit_idx % 8;
    if(value)
    {
        btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd);
    }
    else
    {
        btmp->bits[byte_idx] &= ~(BITMAP_MASK << bit_odd);
    }
}