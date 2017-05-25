#include <stdio.h>
#include <linkedlist.h>

int main(int argc, char **argv){
	list_t list = list_create();
	if(list == NULL){
		printf("list NULL\n");
	}
//	printf("1\n");
	int j[] = {0, 1, 2, 3, 4};	
	for (int i = 0; i < 5; i++){
//		int j = i;
		list_add(list, (void *)&j[i], 10);
		
	}
//	printf("2\n");
	
	for (int i = 0; i < 5; i++){
	nodePtr nd = (nodePtr)list_get(list, i);
	if(nd == NULL){
		printf("Node NULL\n");
	}
//	printf("3\n");
	printf("index: %d\n", getIndex(nd));
	printf("type: %d\n", getType(nd));
	int *data = (int* )getData(nd);
	if(data == NULL){
		printf("data NULL\n");
	}
//	printf("4\n");
	printf("data: %d\n", *data);
	}
//	printf("node index, type, data: %d, %d, %d\n", getIndex(nd), getType(nd),
//					 *((int*)getData(nd)));
//	printf("5\n");
	return 0;
}
