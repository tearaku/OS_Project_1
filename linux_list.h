#ifndef LINUX_LIST_H
#define LINUX_LIST_H
// Using linux's list implementations
#define POISON_POINTER_DELTA 0
#define LIST_POISON1  ((void *) 0x00100100 + POISON_POINTER_DELTA)
#define LIST_POISON2  ((void *) 0x00200200 + POISON_POINTER_DELTA)
#define list_for_each(pos, head) \
        for (pos = (head)->next; pos != (head); pos = pos->next)

#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) ({                      \
        const typeof(((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define list_entry(ptr,type,member) \
	container_of(ptr, type, member)

struct linux_list_head{
	struct linux_list_head *next, *prev;
} typedef list;

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) \
	list name = LIST_HEAD_INIT(name)
void INIT_LIST_HEAD(list *head) {
    head->next = head;
	head->prev = head;
}
int list_empty(const list *head) {
	return (head->next == head);
}
static void __list_add(list *new_lst, list *prev, list *next) {
    next->prev = new_lst;
	new_lst->next = next;
	new_lst->prev = prev;
	prev->next = new_lst;
}
void list_add(list *new_lst, list *head) {
	__list_add(new_lst, head, head->next);
}
void list_add_tail(list *new_lst, list *head) {
	__list_add(new_lst, head->prev, head);
}
static void __list_del(list *prev, list *next) {
    next->prev = prev;
	prev->next = next;
}
void list_del(list * entry) {
	__list_del(entry->prev,entry->next);
	entry->next = LIST_POISON1;
	entry->prev = LIST_POISON2;
}
#endif