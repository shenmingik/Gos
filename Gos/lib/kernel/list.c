#include "list.h"
#include "interrupt.h"

/*
 * @brief 初始化链表
 * @param list 待初始化的链表结构
 */
void list_init (struct list* list) {
   list->head.prev = NULL;
   list->head.next = &list->tail;
   list->tail.prev = &list->head;
   list->tail.next = NULL;
}

/*
 * @brief 把元素elem插入到before之前
 * @param before 待插入的元素的后一个元素
 * @param elem 待插入元素
 * @note 需要处于关中断状态，即不能被打断
 */
void list_insert_before(struct list_elem* before, struct list_elem* elem) { 
   enum intr_status old_status = intr_disable();

   before->prev->next = elem; 
   elem->prev = before->prev;
   elem->next = before;
   before->prev = elem;

   intr_set_status(old_status);
}

/*
 * @brief 将元素elem插入到链表头
 * @param plist 要操作的链表
 * @param elem 待插入元素
 */
void list_push(struct list* plist, struct list_elem* elem) {
   list_insert_before(plist->head.next, elem); // 在队头插入elem
}

/*
 * @brief 追加元素到链表尾部
 * @param plist 要操作的链表
 * @param elem 待插入的元素
 */
void list_append(struct list* plist, struct list_elem* elem) {
   list_insert_before(&plist->tail, elem);     // 在队尾的前面插入
}


/*
 * @beief 删除元素pelem的下一个元素
 * @param pelem 待操作元素
 */
void list_remove(struct list_elem* pelem) {
   enum intr_status old_status = intr_disable();
   
   pelem->prev->next = pelem->next;
   pelem->next->prev = pelem->prev;

   intr_set_status(old_status);
}

/*
 * @brief 链表元素出栈一个
 * @param plist 待操作链表
 * @return 出栈元素地址
 */
struct list_elem* list_pop(struct list* plist) {
   struct list_elem* elem = plist->head.next;
   list_remove(elem);
   return elem;
} 

/*
 * @brief 在链表plist中查找元素obj_elem
 * @param plist 待操作链表
 * @param obj_elem 待寻找的链表元素
 * @return 是否找到这个元素，找到返回true
 */
bool elem_find(struct list* plist, struct list_elem* obj_elem) {
   struct list_elem* elem = plist->head.next;
   while (elem != &plist->tail) {
      if (elem == obj_elem) {
	 return true;
      }
      elem = elem->next;
   }
   return false;
}

/*
 * @brief 在链表中找到符合回调函数条件的元素，返回其地址
 * @param plist 待操作的链表
 * @param func 判定标准函数
 * @param arg 判定标准值
 * @return 成功返回元素地址，失败返回NULL
 */
struct list_elem* list_traversal(struct list* plist, function func, int arg) {
   struct list_elem* elem = plist->head.next;
   if (list_empty(plist)) { 
      return NULL;
   }

   while (elem != &plist->tail) {
      if (func(elem, arg)) {		  // func返回ture则认为该元素在回调函数中符合条件,命中,故停止继续遍历
	 return elem;
      }					  // 若回调函数func返回true,则继续遍历
      elem = elem->next;	       
   }
   return NULL;
}

/*
 * @brief 返回链表的长度
 * @param plist 待操作的链表
 * @return 链表的长度
 */
uint32_t list_len(struct list* plist) {
   struct list_elem* elem = plist->head.next;
   uint32_t length = 0;
   while (elem != &plist->tail) {
      length++; 
      elem = elem->next;
   }
   return length;
}

/*
 * @brief 判断链表是否为空
 * @param plist 待操作的链表
 * @return 空返回true，非空返回false
 */
bool list_empty(struct list* plist) {		// 判断队列是否为空
   return (plist->head.next == &plist->tail ? true : false);
}
