#include <stdint.h>
#include <stdlib.h>

#include "linkedlist.h"

struct node{
	void* data;
	nodePtr next;
	int index;
	int struct_type;
};

struct linkedlist {
	nodePtr head;
	nodePtr tail;
	int size;
};

int getIndex(nodePtr nd){
	return nd->index;
}

int getType(nodePtr nd){
	return nd->struct_type;
}

void* getData(nodePtr nd){
	return nd->data;
}

list_t list_create(void)
{
	list_t listPtr = (list_t) malloc(sizeof(struct linkedlist));
	listPtr->head = NULL;
	listPtr->tail = NULL;
	listPtr->size = 0;
	return listPtr;
}

int list_length(list_t list)
{
	return (list == NULL ? -1 : list->size);
}

int list_destroy(list_t list)
{
    int listLen = list_length(list);
    if(listLen == -1){
        free(list);
        return 0;
    }

    for(int i = 0; i<listLen; i++){
        nodePtr nd = list_get(list, i);
        free(nd);
    }
    free(list);
	return 0;
}

int list_add(list_t list, void *data, int struct_type)
{
	if (list == NULL || data == NULL) {
		return -1;
	}
	nodePtr newTail = (nodePtr)(malloc(sizeof(struct node)));
//	if (newTail == NULL){
//		return -1;
//	}
	newTail->struct_type = struct_type;
	newTail->data = data;
	if (list->size == 0) {
		list->head = newTail;
		list->tail = newTail;
		newTail->index = 0;
		list->size++;
		return 0;
	}

	newTail->index = list->tail->index + 1;
	list->tail->next = newTail;
	list->tail = newTail;
	list->size++;
	return 0;
}

nodePtr list_get(list_t list, int index)
{
     if (list == NULL || index < 0) {
             return NULL;
     }
     nodePtr current = list->head;
	if(current == NULL){
		return NULL;
	}
     while (current != NULL) {
             if(current->index == index){
             	return current;
		}
	current = current->next;
     }
     return NULL;
 }

//TODO once we move this all to the fs.c file, change void* to fdOp*
int list_remove(list_t, void* data){
    if (list == NULL || index < 0) {
            return -1;
    }

    //TODO write remove method. Make sure it actually works this time
    //this method will be exclusively used to remove file descriptors from the openFilesList
}
