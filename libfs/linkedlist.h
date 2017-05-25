#ifndef _LIST_H
#define _LIST_H

typedef struct node* nodePtr;
typedef struct linkedlist* list_t;

int getIndex(nodePtr nd);

int getType(nodePtr nd);

void *getData(nodePtr nd);

list_t list_create(void);

int list_destroy(list_t list);

int list_add(list_t list, void *data, int struct_type);

nodePtr list_get(list_t lit, int index);

int list_length(list_t list);

#endif /* _LIST_H */
