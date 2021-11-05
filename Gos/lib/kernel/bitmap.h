//位图，用于管理容量较大的资源
#pragma once
#include "global.h"

#define BITMAP_MASK 1       //用于在位图中逐位判断

struct bitmap
{
    uint32_t btmp_bytes_len;    //位图总共字节大小
    uint8_t *bits;              //位图指针
};

void bitmap_init(struct bitmap *btmp);
bool bitmap_scan_test(struct bitmap *btmp, uint32_t bit_idx);
int bitmap_scan(struct bitmap *btmp, uint32_t cnt);
void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value);