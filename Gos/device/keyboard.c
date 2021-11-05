#include "keyboard.h"
#include "print.h"
#include "interrupt.h"
#include "io.h"
#include "global.h"
#include "ioqueue.h"

#define KEYBOARD_BUFF_PORT 0x60 //键盘缓冲区的寄存器端口号

// * @brief 定义控制字符的ascii码
#define esc '\x1b'
#define backspace '\b'
#define tab '\t'
#define enter '\r'
#define delete '\x7f'

// * @brief 以下不可见字符，占位用
#define char_invisible 0
#define ctrl_l_char char_invisible //左边的ctrl键
#define ctrl_r_char char_invisible //右边的ctrl键
#define shift_l_char char_invisible
#define shift_r_char char_invisible
#define alt_l_char char_invisible
#define alt_r_char char_invisible
#define caps_lock_char char_invisible

// * @brief 定义字符的通码和断码，其是操作控制键的扫描码
#define shift_l_make 0x2a
#define shift_r_make 0x36
#define alt_l_make 0x38
#define alt_r_make 0xe038
#define alt_r_break 0xe0b8
#define ctrl_l_make 0x1d
#define ctrl_r_make 0xe01d
#define ctrl_r_break 0xe09d
#define caps_lock_make 0x3a

// * @brief 定义以下变量记录相应键是否按下的状态，用于组合键
static bool ctrl_status;      //ctrl
static bool shift_status;     //shift
static bool alt_status;       //alt
static bool caps_lock_status; //capslock
static bool ext_scancode;     //0xe0标记，代表是持续按下按键，会产生多个扫描码

struct ioqueue keyboard_buff; //定义键盘缓冲区

// * @brief 以通码make_code为索引的二维数组
static char keymap[][2] = {
    // * @brief 主要是定义当与shift组合和不组合的时候产生的不同的字符
    /* 扫描码   未与shift组合  与shift组合*/
    /* ---------------------------------- */
    /* 0x00 */ {0, 0},
    /* 0x01 */ {esc, esc},
    /* 0x02 */ {'1', '!'},
    /* 0x03 */ {'2', '@'},
    /* 0x04 */ {'3', '#'},
    /* 0x05 */ {'4', '$'},
    /* 0x06 */ {'5', '%'},
    /* 0x07 */ {'6', '^'},
    /* 0x08 */ {'7', '&'},
    /* 0x09 */ {'8', '*'},
    /* 0x0A */ {'9', '('},
    /* 0x0B */ {'0', ')'},
    /* 0x0C */ {'-', '_'},
    /* 0x0D */ {'=', '+'},
    /* 0x0E */ {backspace, backspace},
    /* 0x0F */ {tab, tab},
    /* 0x10 */ {'q', 'Q'},
    /* 0x11 */ {'w', 'W'},
    /* 0x12 */ {'e', 'E'},
    /* 0x13 */ {'r', 'R'},
    /* 0x14 */ {'t', 'T'},
    /* 0x15 */ {'y', 'Y'},
    /* 0x16 */ {'u', 'U'},
    /* 0x17 */ {'i', 'I'},
    /* 0x18 */ {'o', 'O'},
    /* 0x19 */ {'p', 'P'},
    /* 0x1A */ {'[', '{'},
    /* 0x1B */ {']', '}'},
    /* 0x1C */ {enter, enter},
    /* 0x1D */ {ctrl_l_char, ctrl_l_char},
    /* 0x1E */ {'a', 'A'},
    /* 0x1F */ {'s', 'S'},
    /* 0x20 */ {'d', 'D'},
    /* 0x21 */ {'f', 'F'},
    /* 0x22 */ {'g', 'G'},
    /* 0x23 */ {'h', 'H'},
    /* 0x24 */ {'j', 'J'},
    /* 0x25 */ {'k', 'K'},
    /* 0x26 */ {'l', 'L'},
    /* 0x27 */ {';', ':'},
    /* 0x28 */ {'\'', '"'},
    /* 0x29 */ {'`', '~'},
    /* 0x2A */ {shift_l_char, shift_l_char},
    /* 0x2B */ {'\\', '|'},
    /* 0x2C */ {'z', 'Z'},
    /* 0x2D */ {'x', 'X'},
    /* 0x2E */ {'c', 'C'},
    /* 0x2F */ {'v', 'V'},
    /* 0x30 */ {'b', 'B'},
    /* 0x31 */ {'n', 'N'},
    /* 0x32 */ {'m', 'M'},
    /* 0x33 */ {',', '<'},
    /* 0x34 */ {'.', '>'},
    /* 0x35 */ {'/', '?'},
    /* 0x36	*/ {shift_r_char, shift_r_char},
    /* 0x37 */ {'*', '*'},
    /* 0x38 */ {alt_l_char, alt_l_char},
    /* 0x39 */ {' ', ' '},
    /* 0x3A */ {caps_lock_char, caps_lock_char}
    /*其它按键暂不处理*/
};

/*
 * @brief 键盘中断程序
 */
static void intr_keyboard_handler(void)
{
    //检测上次中断发生前ctrl、shift以及capslock是否被按下
    bool ctrl_down_last = ctrl_status;
    bool shift_down_last = shift_status;
    bool capslock_down_last = caps_lock_status;

    bool break_code; //断码标记，断码我们有特殊的处理方式

    //获取上一次中断发生的字符
    uint16_t scancode = inb(KEYBOARD_BUFF_PORT);
    if (scancode == 0xe0)
    {
        //持续按下，会有后续案件
        ext_scancode = true;
        return;
    }

    if (ext_scancode)
    {
        scancode = ((0xe000) | scancode); //获取scancode的完整版本，即加上0xe0前缀
        ext_scancode = false;             //关闭0xe0标记
    }

    break_code = ((scancode & 0x0080) != 0); //判断是否是断码
    if (break_code)
    {
        //断码处理模块,做的工作主要是组合键状态改变的记录
        uint16_t make_code = (scancode &= 0xff7f); //得到按键按下时的扫描码
        if (make_code == ctrl_l_make || make_code == ctrl_r_make)
        {
            //如果时ctrl，那么代表ctrl键已经被松开了
            ctrl_status = false;
        }
        else if (make_code == shift_l_make || make_code == shift_r_make)
        {
            shift_status = false;
        }
        else if (make_code == alt_l_make || make_code == alt_r_make)
        {
            alt_status = false;
        }
        return;
    }
    else if ((scancode > 0x00 && scancode < 0x3b) || (scancode == alt_r_make) || (scancode == ctrl_r_make))
    {
        //通码处理，如果时范围在00~3b、alt_r_make、ctrl_r_make
        bool shift = false; //shift效果标记

        //扫描码转换为字符

        //先判断是否是二义性字符有0~9、[\;/,.=-'等等
        if ((scancode < 0x0e) || (scancode == 0x29) ||
            (scancode == 0x1a) || (scancode == 0x1b) ||
            (scancode == 0x2b) || (scancode == 0x27) ||
            (scancode == 0x28) || (scancode == 0x33) ||
            (scancode == 0x34) || (scancode == 0x35))
        {
            if (shift_down_last)
            {
                //按下了shift键
                shift = true;
            }
        }

        else
        {
            if (shift_down_last && capslock_down_last)
            {
                //shift 和 capslock同时按下
                shift = false;
            }
            else if (shift_down_last || capslock_down_last)
            {
                //任意按下其中之一
                shift = true;
            }
            else
            {
                //都没按下
                shift = false;
            }
        }

        uint8_t index = (scancode &= 0x00ff); //得到下标，0xe0开头的会去除前缀
        char current_char = keymap[index][shift];

        //这里处理组合键ctrl+l 或者ctrl+u
        if ((ctrl_down_last && current_char == 'l') || (ctrl_down_last && current_char == 'u'))
        {
            current_char -= 'a';
        }

        if (current_char)
        {
            //只处理ascii不为0的键
            if (!ioqueue_is_full(&keyboard_buff))
            {
                ioqueue_putchar(&keyboard_buff, current_char);
            }
            return;
        }

        //记录本次是否有按下控制键，供下一次时判断
        if (scancode == ctrl_l_make || scancode == ctrl_r_make)
        {
            ctrl_status = true;
        }
        else if (scancode == shift_l_make || scancode == shift_r_make)
        {
            shift_status = true;
        }
        else if (scancode == alt_l_make || scancode == alt_r_make)
        {
            alt_status = true;
        }
        else if (scancode == caps_lock_make)
        {
            /* 不管之前是否有按下caps_lock键,当再次按下时则状态取反,
       * 即:已经开启时,再按下同样的键是关闭。关闭时按下表示开启。*/
            caps_lock_status = !caps_lock_status;
        }
    }
    else
    {
        put_str("unknown char!\n");
    }
}

/*
 * @brief 键盘驱动初始化
 */
void keyboard_init()
{
    put_str("keyboard init start!\n");
    ioqueue_init(&keyboard_buff);
    register_handler(0x21, intr_keyboard_handler);
    put_str("keyboard init done!\n");
}
