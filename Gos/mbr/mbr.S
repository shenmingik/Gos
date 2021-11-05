; MBR引导程序，从BIOS接过权力，交接给loader内核加载器
;*************************
%include "boot.inc"
SECTION MBR vstart=0x7c00
    ; 初始化显卡控制信息，本质是往显存写入数据
    mov ax,cs
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax
    mov sp,0x7c00
    mov ax,0xb800
    mov gs,ax
;*************************
; 清屏利用0x06号功能，上卷全部行，进行清屏
; int 0x10  功能号：0x60    功能描述：上卷窗口
; 输入：
; AH 功能号： 0x06
; AL = 上卷的行数(0代表全部)
; BH = 上卷的行属性
; (CL,CH) = 窗口左上角(x,y) 的位置
; (DL,DH) = 窗口右下角(x,y)的位置
; 无返回值!
    mov ax,0600h
    mov bx,0700h
    mov cx,0        ;左上角(0,0)
    mov dx,184fh   ;右下角(80,25)
                    ;VAG文本模式中，一行只能容纳80个字符，总共25行
                    ;下标从0开始，所以0x18=24，0x4f=79
    int 10h
;*************************
    ;输出字符串：hello,Gos!
    mov byte [gs:0x00],'h'
    mov byte [gs:0x01],0x0F     ;黑底亮白不闪烁

    mov byte [gs:0x02],'e'
    mov byte [gs:0x03],0x0F

    mov byte [gs:0x04],'l'
    mov byte [gs:0x05],0x0F

    mov byte [gs:0x06],'l'
    mov byte [gs:0x07],0x0F

    mov byte [gs:0x08],'o'
    mov byte [gs:0x09],0x0F

    mov byte [gs:0x0a],','
    mov byte [gs:0x0b],0x0F

    mov byte [gs:0x0c],'G'
    mov byte [gs:0x0d],0x0F

    mov byte [gs:0x0e],'o'
    mov byte [gs:0x0f],0x0F

    mov byte [gs:0x10],'s'
    mov byte [gs:0x11],0x0F

    mov byte [gs:0x12],'!'
    mov byte [gs:0x13],0x0F
;*************************
;初始化磁盘信息
    mov eax,LOADER_START_SECTOR     ;起始扇区LBA地址
    mov bx,LOADER_BASE_ADDR         ;写入的地址
    mov cx,4                        ;待读入的扇区数
    call read_disk_m_16             ;调用读取程序起始部分的函数

    jmp LOADER_BASE_ADDR + 0x300
;*************************
;读取磁盘的n个扇区
read_disk_m_16:
                                    ;eax=LBA扇区号
                                    ;bx=将数据写入的内存地址
                                    ;cx=读入的扇区数
    mov esi,eax                     ;备份eax
    mov di,cx                       ;备份cx
;读写硬盘
                                    ;1.设置要读取的扇区数量
    mov dx,0x1f2
    mov al,cl
    out dx,al                       ;读取的扇区数
    mov eax,esi                     ;恢复eax

                                    ;2.将LBA地址存入0x1f3~0x1f6
                                    ;LBA地址7~0位写入端口0x1f3
    mov dx,0x1f3
    out dx,al

                                    ;LBA地址15~8位写入端口0x1f4
    mov cl,8
    shr eax,cl
    mov dx,0x1f4
    out dx,al

                                    ;LBA地址23~16位写入端口0x1f5
    shr eax,cl
    mov dx,0x1f5
    out dx,al

    shr eax,cl
    and al,0x0f                     ;LBA第24~27位
    or al,0xe0                      ;设置7~4位为1110，表示LBA模式
    mov dx,0x1f6
    out dx,al

                                    ;3.向0x1f7端口写入读命令，0x20
    mov dx,0x1f7
    mov al,0x20
    out dx,al

                                    ;4.检测硬盘状态
.not_ready:
    nop                             ;同一端口，写时表示写入命令字，读时表示读入硬盘状态
    in al,dx                        ;
    and al,0x88                     ;第3位为1表示硬盘控制器已经准备号数据传输了
                                    ;第7位为1表示硬盘忙
    cmp al,0x08                     
    jnz .not_ready                  ;若未准备号，继续等

                                    ;5.从0x1f0端口读数据
    mov ax,di
    mov dx,256                      ;一个扇区512字节，1字=2字节，所以时256
    mul dx
    mov cx,ax
    mov dx,0x1f0

.go_on_read:
    in ax,dx
    mov [bx],ax
    add bx,2
    loop .go_on_read
    ret

times 510-($-$$) db 0
db 0x55,0xaa