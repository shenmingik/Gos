BUILD_DIR = ./build
# 设置程序入口地址
ENTRY_POINT = 0xc0001500

# 设置快捷词
AS = nasm
CC = gcc
LD = ld

# 设置选项参数信息
LIB = -I lib/ -I lib/kernel/ -I lib/user/ -I kernel/ -I device/
ASFLAGS = -f elf
CFLAGS = -Wall $(LIB) -m32 -c -fno-builtin -W -Wstrict-prototypes \
         -Wmissing-prototypes 
LDFLAGS = -Ttext -m elf_i386 $(ENTRY_POINT) -e main -Map $(BUILD_DIR)/kernel.map
OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/init.o $(BUILD_DIR)/interrupt.o \
      $(BUILD_DIR)/timer.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/print.o \
      $(BUILD_DIR)/debug.o

#------------------------------- C 代码编译 -------------------------------
$(BUILD_DIR)/main.o:kernel/main.c lib/kernel/print.h lib/stdint.h kernel/init.h 
					$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/init.o:kernel/init.c kernel/init.h lib/kernel/print.h lib/stdint.h \
					kernel/interrupt.h device/timer.h 
					$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o:kernel/interrupt.c kernel/interrupt.h lib/stdint.h \
						kernel/global.h lib/kernel/io.h lib/kernel/print.h 
						$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/timer.o:device/timer.c device/timer.h lib/stdint.h lib/kernel/io.h \
					lib/kernel/print.h 
					$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/debug.o:kernel/debug.c kernel/debug.h lib/kernel/print.h \
					lib/stdint.h kernel/interrupt.h 
					$(CC) $(CFLAGS) $< -o $@

#------------------------------- 汇编代码编译 -------------------------------
$(BUILD_DIR)/kernel.o:kernel/kernel.S 
						$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/print.o:lib/kernel/print.S
					$(AS) $(ASFLAGS) $< -o $@

#------------------------------- 链接所有目标文件 -------------------------------
$(BUILD_DIR)/kernel.bin:$(OBJS)
						$(LD) $(ASFLAGS) $^ -o $@

# 定义伪目标
.PHONY : mk_dir hd clean all

# 创建文件夹$(BUILD_DIR)
mk_dir:
	if [[ ! -d $(BUILD_DIR) ]];then mkdir $(BUILD_DIR);fi

# 写入磁盘
hd:
	dd if=$(BUILD_DIR)/kernel.bin \
	of=/bochs/bo_tmp/bin/hd60M.img \
	bs=512 count=200 seek=9 conv=notrunc

# 清空文件夹$(BUILD_DIR)
clean:
	cd $(BUILD_DIR) && rm -f ./*

build:
	$(BUILD_DIR)/kernel.bin

all: mk_dir build hd
