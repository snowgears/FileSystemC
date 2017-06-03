#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <sys/mman.h>
#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF
#define BLOCK_SIZE 4096
#define FAT_ARRAY_SIZE 2048
#define MAX_FILE_COUNT 128

#define BLOCK_SUPER 11
#define BLOCK_FAT 12
#define BLOCK_ROOT 13
#define BLOCK_DATA 14

//===========================================================================//
//                        LINKED LIST IMPLEMENTATION                         //
//===========================================================================//

struct node{
	void* data;
	struct node* next;
	int index;
	int struct_type;
};

struct linkedlist {
	struct node* head;
	struct node* tail;
	int size;
};

typedef struct node* nodePtr;
typedef struct linkedlist* list_t;

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

//===========================================================================//
//                   END OF LINKED LIST IMPLEMENTATION                       //
//===========================================================================//

typedef struct {
    char fileName[16];
    size_t offset;
} fdOp;

//===========================================================================//
//                            GLOBAL VARIABLES                               //
//===========================================================================//

//global linked list of blocks
list_t blockList;
//global file descriptor count to make sure that each file descriptor is unique
int fdCount = 0;
//global array of ints to keep track of dirty bits for changed blocks (for more efficient unmounting)
int *changedBlocks;
//global array of file descriptor structs where index is the file descriptor integer
fdOp *fileDes[32];

//===========================================================================//
//                        DEFINED BLOCK STRUCTS                              //
//===========================================================================//

typedef struct {
    char signature[8];
    uint16_t numBlocks;
    uint16_t rootIndex;
    uint16_t dataStartIndex;
    uint16_t dataBlockCount;
    uint8_t fatBlockCount;
    char padding[4079];
} __attribute__((packed)) superblock;

typedef struct {
    uint16_t entries[FAT_ARRAY_SIZE];
} fat;

typedef struct {
    char fileName[16];
    uint32_t fileSize;
    uint16_t dataStartIndex;
    char padding[10];
} __attribute__((packed)) rootEntry;

typedef struct {
    rootEntry entries[MAX_FILE_COUNT];
} __attribute__((packed)) rootDirectory;


//===========================================================================//
//                        GETTER HELPER METHODS                              //
//===========================================================================//

//get a fdOp struct from the openFilesList by the file name (if its exists)
fdOp* getFdOp(const char* filename){
    if(fileDes == NULL){
        return NULL;
    }

    for(int i = 0; i < 32; i++){
    	if(strcmp(fileDes[i]->fileName, filename) == 0){
            return fileDes[i];
        }
    }
    return NULL;
}

//get a fdOp struct from the openFilesList by the unique file descriptor integer (if its exists)
fdOp* getFdOpByDescriptor(int fd){
    if(fileDes == NULL || fd < 0 || fd > 31){
        return NULL;
    }
	return fileDes[fd];
}

rootDirectory * getRootDirectory(){
    nodePtr nd = list_get(blockList, 0);

    if(nd == NULL){
        return NULL;
    }

    superblock* sBlock = (superblock*)getData(nd);

    nodePtr rootNd = list_get(blockList, sBlock->rootIndex);
    rootDirectory* rBlock = (rootDirectory*)getData(rootNd);
    return rBlock;
}

//===========================================================================//
//                       BLOCK INITIALIZE METHODS                            //
//===========================================================================//

superblock* init_superblock(){

    void* block = malloc(BLOCK_SIZE);
    int readSuccess = block_read(0, block); //take block at index 0 and copy into malloc'd block

    if(readSuccess == -1){
        return NULL;
    }

    superblock* sBlock = (superblock*)malloc(sizeof(superblock));

    //use this variable to skip over previously read bytes
    char* blockOffset = (char*)block;

    //void * signature = malloc(8);
    memcpy(sBlock->signature, (void *)blockOffset, 8);

    //======================================================//

    void * numBlocks = malloc(2);
    blockOffset += 8;
    memcpy(numBlocks, (void *)blockOffset , 2);

    sBlock->numBlocks = *((uint16_t*)numBlocks);

    //======================================================//

    void * rootIndex = malloc(2);
    blockOffset += 2;
    memcpy(rootIndex, (void *)blockOffset , 2);

    sBlock->rootIndex = *((uint16_t*)rootIndex);

    //======================================================//

    void * dataStartIndex = malloc(2);
    blockOffset += 2;
    memcpy(dataStartIndex, (void *)blockOffset , 2);

    sBlock->dataStartIndex = *((uint16_t*)dataStartIndex);

    //======================================================//

    void * dataBlockCount = malloc(2);
    blockOffset += 2;
    memcpy(dataBlockCount, (void *)blockOffset , 2);

    sBlock->dataBlockCount = *((uint16_t*)dataBlockCount);

    //======================================================//

    void * fatBlockCount = malloc(1);
    blockOffset += 2;
    memcpy(fatBlockCount, (void *)blockOffset , 1);

    sBlock->fatBlockCount = *((uint16_t*)fatBlockCount);

	changedBlocks = (int *)calloc(sBlock->numBlocks, sizeof(int));

    return sBlock;
}

fat* init_fat(int index){

    fat* fBlock = (fat*)malloc(sizeof(fat));

    void* block = malloc(BLOCK_SIZE);
    int readSuccess = block_read(index, block); //take block at index 0 and copy into malloc'd block

    if(readSuccess == -1){
        return NULL;
    }

    char* blockOffset = (char*)block;

    for(int i=0; i<FAT_ARRAY_SIZE; i++){
        uint16_t* entry = (uint16_t*)malloc(2);
        memcpy(entry, (void *)blockOffset, 2);
        fBlock->entries[i] = *entry;
        blockOffset += 2;
    }
    return fBlock;
}

rootDirectory* init_rootDir(uint16_t rootIndex){

    void* block = malloc(BLOCK_SIZE);
    int readSuccess = block_read(rootIndex, block); //take block at index 0 and copy into malloc'd block

    if(readSuccess == -1){
        return NULL;
    }

    rootDirectory* rootDir = (rootDirectory*)malloc(sizeof(rootDirectory));

    //use this variable to skip over previously read bytes
    char* blockOffset = (char*)block;

    for(int i=0; i<MAX_FILE_COUNT; i++){
        rootEntry* entry = (rootEntry*)malloc(sizeof(rootEntry));

        memcpy(entry->fileName, (void *)blockOffset, 16);

        //======================================================//

        blockOffset += 16;
        void * fileSize = malloc(4);
        memcpy(fileSize, (void *)blockOffset , 4);

        entry->fileSize = *((uint32_t*)fileSize);

        //======================================================//

        blockOffset += 4;
        void * dataStartIndex = malloc(2);
        memcpy(dataStartIndex, (void *)blockOffset , 2);

        entry->dataStartIndex = *((uint16_t*)dataStartIndex);

        //move data offset by 10 to account for the unused/padding area of the entry
        blockOffset += 12;

        rootDir->entries[i] = *entry;
    }
	return rootDir;
}

//===========================================================================//
//                          IMPLEMENTED FS METHODS                           //
//===========================================================================//

int fs_mount(const char *diskname)
{
    int success = block_disk_open(diskname);

    if(success == -1){
        return -1;
    }

    blockList = list_create();
	for(int i = 0; i < 32; i++){
		fileDes[i] = (fdOp*)malloc(sizeof(fdOp));
		fileDes[i]->fileName[0] = '\0';
		fileDes[i]->offset = 0;
	}

	superblock* sBlock = init_superblock();
    if(sBlock == NULL){
        return -1;
    }

    char check[9];
    strcpy(check, sBlock->signature);
    check[8] = '\0';
    if(strcmp(check, "ECS150FS") != 0){
        return -1;
    }

    if(sBlock->numBlocks != block_disk_count()){
        return -1;
    }

    list_add(blockList, (void*)sBlock, BLOCK_SUPER);

    int currentIndex = 1;

	fat* fBlock = malloc(sBlock->fatBlockCount * sizeof(fat));
    while(sBlock->rootIndex - currentIndex >= 1){
        fBlock = init_fat(currentIndex++);

        if(fBlock == NULL){
            return -1;
        }

	    list_add(blockList, (void*)fBlock, BLOCK_FAT);
    }

    rootDirectory* rBlock = init_rootDir(sBlock->rootIndex);
    list_add(blockList, (void*)rBlock, BLOCK_ROOT);

    while(list_length(blockList) < sBlock->numBlocks){
        void * page = mmap(NULL, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        list_add(blockList, (void*)page, BLOCK_DATA);
    }

    return 0;
}

int fs_umount(void)
{
    int listLen = list_length(blockList);

    if(listLen == -1){
        return -1;
    }

    for(int i = 0; i<listLen; i++){
     	nodePtr nd = list_get(blockList, i);
            if(changedBlocks[i] == 1){
    	        int writeSuccess = block_write(i, getData(nd));

           		 if(writeSuccess == -1){
            	 	return -1;
            	}
    	    }
            void* data = getData(nd);
            if(getType(nd) == BLOCK_DATA){
                munmap(data, BLOCK_SIZE);
            }
            else{
                free(data);
            }
    }
    int closeSuccess = block_disk_close();

    if(closeSuccess == -1){
        return -1;
    }

	for(int i = 0; i < 32; i++){
		free(fileDes[i]);
	}

	free(changedBlocks);

    int destroySuccess = list_destroy(blockList);

    if(destroySuccess == -1){
        return -1;
    }
	return 0;
}

int fat_count(void)
{
	int count = 0;

    //account for multiple FAT blocks
    for(int i = 0; i<list_length(blockList); i++){

        nodePtr nd = list_get(blockList, i);
        //if the block is FAT, count the number of entries within it
        if(getType(nd) == BLOCK_FAT){
            fat* fBlock = (fat*) getData(nd);

        	for(int j = 0; j < FAT_ARRAY_SIZE; j++){
        		if (fBlock->entries[j] != 0){
        			count++;
        		}
        	}
        }
    }
	return count;
}

int rdir_count(void)
{
    rootDirectory* rBlock = getRootDirectory();

    if(rBlock == NULL){
        return -1;
    }

	int count = 0;
	for(int i = 0; i < MAX_FILE_COUNT; i++){
		if(rBlock->entries[i].fileSize == 0){
			if(rBlock->entries[i].fileName[0] == '\0'){
				count++;
			}
		}
	}
	return count;
}

int fs_info(void)
{
    nodePtr nd = list_get(blockList, 0);
    superblock* sBlock = (superblock*)getData(nd);

	printf("FS Info:\n");
	printf("total_blk_count=%d\n", sBlock->numBlocks);
	printf("fat_blk_count=%d\n", sBlock->fatBlockCount);
	printf("rdir_blk=%d\n", sBlock->rootIndex);
	printf("data_blk=%d\n", sBlock->dataStartIndex);
	printf("data_blk_count=%d\n", sBlock->dataBlockCount);
	printf("fat_free_ratio=%d/%d\n", (sBlock->dataBlockCount - fat_count())
					, sBlock->dataBlockCount);
	printf("rdir_free_ratio=%d/%d\n", rdir_count(), MAX_FILE_COUNT);
	return 0;
}

int fs_create(const char *filename)
{

	nodePtr nd = list_get(blockList, 0);

    if(nd == NULL){
        return -1;
    }

    superblock* sBlock = (superblock*)getData(nd);

    nodePtr rootNd = list_get(blockList, sBlock->rootIndex);
    rootDirectory* rBlock = (rootDirectory*)getData(rootNd);

    for(int i = 0; i<list_length(blockList); i++){

        nodePtr nd = list_get(blockList, i);

        //if the block is FAT, count the number of entries within it
        if(getType(nd) == BLOCK_FAT){
            fat* fBlock = (fat*) getData(nd);

        	for(int j = 0; j < FAT_ARRAY_SIZE; j++){
        		if (fBlock->entries[j] == 0){
        			fBlock->entries[j] = FAT_EOC;

                    for(int k=0; k<MAX_FILE_COUNT; k++){
                        //the first character of the filename of entry is '0'
                        if(rBlock->entries[k].fileName[0] == '\0'){

                            memcpy(rBlock->entries[k].fileName, (void *)filename , 16);
                            rBlock->entries[k].fileSize = 0;
                            rBlock->entries[k].dataStartIndex = j;

                            changedBlocks[i] = 1;
				            changedBlocks[sBlock->rootIndex] = 1;
                            return 0;
                        }
                    }
        		}
        	}
        }
    }

	return 0;
}

int fs_delete(const char *filename)
{
	nodePtr nd = list_get(blockList, 0);

    if(nd == NULL){
        return -1;
    }

    superblock* sBlock = (superblock*)getData(nd);

    nodePtr rootNd = list_get(blockList, sBlock->rootIndex);
    rootDirectory* rBlock = (rootDirectory*)getData(rootNd);

    for(int j = 0; j<list_length(blockList); j++){

        nodePtr nd = list_get(blockList, j);
        //if the block is FAT, count the number of entries within it
        if(getType(nd) == BLOCK_FAT){
            fat* fBlock = (fat*) getData(nd);

    	for(int i = 0; i < MAX_FILE_COUNT; i++){
    		if(strcmp(rBlock->entries[i].fileName, filename) == 0){
    			int fatNum = rBlock->entries[i].dataStartIndex % FAT_ARRAY_SIZE;

    			int nextBlock = fBlock->entries[fatNum];
    			fBlock->entries[fatNum] = 0;

    			while(nextBlock != FAT_EOC){

    				nd = list_get(blockList, (nextBlock / FAT_ARRAY_SIZE) + 1);
    				fBlock = getData(nd);
    				fatNum = nextBlock;
    				nextBlock = fBlock->entries[nextBlock];
    				fBlock->entries[fatNum] = 0;
    		    }
    			rBlock->entries[i].fileName[0] = '\0';
    			rBlock->entries[i].fileSize = 0;
    			rBlock->entries[i].dataStartIndex = 0;

    	 	    for(int i = 0; i < 32; i++){
    				if(strcmp(fileDes[i]->fileName, filename) == 0){
    					fileDes[i]->fileName[0] = '\0';
    					fileDes[i]->offset = 0;
    				}
    			}
    		    changedBlocks[j] = 1;
    		    changedBlocks[sBlock->rootIndex] = 1;
                return 0;
    		}
    	}
	}
}
	return -1;
}

int fs_ls(void)
{
    rootDirectory* rBlock = getRootDirectory();
    if(rBlock == NULL){
        return -1;
    }

	printf("FS Ls:\n");
	for(int i = 0; i < MAX_FILE_COUNT; i++){
		if (rBlock->entries[i].fileName[0] != '\0'){
			printf("file: %s, size: %d, data_blk: %d\n", rBlock->entries[i].fileName, rBlock->entries[i].fileSize, rBlock->entries[i].dataStartIndex);
		}
	}
	return 0;
}

int fs_open(const char *filename)
{
	rootDirectory* rBlock = getRootDirectory();
    if(rBlock == NULL){
        return -1;
    }

	for(int i = 0; i < MAX_FILE_COUNT; i++){
		if( strcmp(rBlock->entries[i].fileName, filename) == 0){
			for(int j = 0; j < 32; j++){
				if(fileDes[j]->fileName[0] == '\0'){
					strcpy(fileDes[j]->fileName, filename);
					return j;
				}
			}
			return -1;
		}
	}
	return -1;
}

int fs_close(int fd)
{
	if(fileDes[fd]->fileName[0] == '\0'){
		return -1;
	}
	fileDes[fd]->fileName[0] = '\0';
	fileDes[fd]->offset = 0;
	return 0;
}

int fs_stat(int fd)
{
    if(fileDes[fd]->fileName[0] == '\0'){
		return -1;
	}
	char* name = fileDes[fd]->fileName;

    rootDirectory* rBlock = getRootDirectory();
    if(rBlock == NULL){
        return -1;
    }

	for(int i = 0; i < MAX_FILE_COUNT; i++){
		if(strcmp(rBlock->entries[i].fileName, name) == 0){
            return rBlock->entries[i].fileSize;
		}
	}
	return -1;
}

int fs_lseek(int fd, size_t offset)
{
	if(fileDes[fd]->fileName[0] == '\0'){
		return -1;
	}

	int size = fs_stat(fd);
	if(offset > size){
		return -1;
	}

	fileDes[fd]->offset = offset;
	return 0;
}

int calcStartBlock(char *filename, int offset){
	int blockNum = offset / BLOCK_SIZE;
	rootDirectory *rBlock = getRootDirectory();

	for(int i = 0; i < MAX_FILE_COUNT; i++){
		if (strcmp(rBlock->entries[i].fileName, filename) == 0){
			if(offset > rBlock->entries[i].fileSize){
				return -1;
			}
			int currBlock = rBlock->entries[i].dataStartIndex;
			for(int j = 1; j < blockNum; j++){
				nodePtr nd = list_get(blockList, (currBlock / FAT_ARRAY_SIZE) + 1);
				fat *fBlock = (fat *)getData(nd);
				currBlock = fBlock->entries[currBlock % FAT_ARRAY_SIZE];
			}
		    return currBlock;
		}
	}
	return -1;
}

int findEmptyBlock(){
	for(int i = 1; i < 5; i++){
		nodePtr nd = list_get(blockList, i);

		if(getType(nd) == BLOCK_FAT){
			fat *fBlock = (fat *)getData(nd);

			for(int j = 0; j < FAT_ARRAY_SIZE; j++){
				if(fBlock->entries[j] == 0){
					return (FAT_ARRAY_SIZE * (i - 1)) + j;
				}
			}
		}
	}
	return -1;
}

int fs_write(int fd, void *buf, size_t count)
{
	nodePtr nd = list_get(blockList, 0);
	rootDirectory *rBlock = getRootDirectory();
	superblock *sBlock = (superblock *)getData(nd);
	fdOp *f = getFdOpByDescriptor(fd);
	size_t currAmtCopied = 0;
	char hold[BLOCK_SIZE];

	if (f == NULL){
		return -1;
	}
	int currBlock = calcStartBlock(f->fileName, f->offset);
	nd = list_get(blockList, (currBlock / FAT_ARRAY_SIZE) + 1);
	fat *fBlock = (fat *)getData(nd);

	if((f->offset % BLOCK_SIZE) != 0){

		int offCount = f->offset % BLOCK_SIZE;

		char * bounce = malloc(BLOCK_SIZE * sizeof(char));
		block_read(sBlock->dataStartIndex + currBlock, hold);

		if(count - offCount >= BLOCK_SIZE){
			memcpy(hold + offCount, buf, BLOCK_SIZE - offCount);
			currAmtCopied = BLOCK_SIZE - offCount;
		}
		else {
			strncpy(bounce, buf, count);
			for(int i = 0 ; i < count; i++){
				hold[offCount + i] = bounce[i];
			}
			currAmtCopied = count;
		}

		block_write(sBlock->dataStartIndex + currBlock, hold);
		if(fBlock->entries[currBlock % FAT_ARRAY_SIZE] != FAT_EOC){
			nd = list_get(blockList, (currBlock / FAT_ARRAY_SIZE) + 1);
			fBlock = (fat *)getData(nd);
			currBlock = fBlock->entries[currBlock % FAT_ARRAY_SIZE];
		}
		else if(count - currAmtCopied > 0){
			int newBlock = findEmptyBlock();
			fBlock->entries[currBlock] = newBlock;
			currBlock = newBlock;
			nd = list_get(blockList, (currBlock / FAT_ARRAY_SIZE) + 1);
			fBlock = (fat *)getData(nd);
			fBlock->entries[currBlock % FAT_ARRAY_SIZE] = FAT_EOC;
		}
	}
	while(currAmtCopied < count){
		if(count - currAmtCopied >= BLOCK_SIZE){

			memcpy(hold, (char *)buf + currAmtCopied, BLOCK_SIZE);
			currAmtCopied += BLOCK_SIZE;
			block_write(sBlock->dataStartIndex + currBlock, hold);

			if(fBlock->entries[currBlock % FAT_ARRAY_SIZE] != FAT_EOC){
				nd = list_get(blockList, (currBlock / FAT_ARRAY_SIZE) + 1);
				fBlock = (fat *)getData(nd);
				currBlock = fBlock->entries[currBlock % FAT_ARRAY_SIZE];
			}
			else if(count - currAmtCopied > 0){
				int newBlock = findEmptyBlock();
				fBlock->entries[currBlock] = newBlock;
				currBlock = newBlock;
				nd = list_get(blockList, (currBlock / FAT_ARRAY_SIZE) + 1);
				fBlock = (fat *)getData(nd);
				fBlock->entries[currBlock % FAT_ARRAY_SIZE] = FAT_EOC;
			}
		}
		else {
			memcpy(hold, (char *)buf + currAmtCopied, (count - currAmtCopied));
			block_write(sBlock->dataStartIndex + currBlock, hold);

			currAmtCopied += count - currAmtCopied;
		}
	}

	if(fs_stat(fd) <= f->offset + count){
		for(int i = 0; i < MAX_FILE_COUNT; i++){
			if(strcmp(rBlock->entries[i].fileName, f->fileName) == 0){
				rBlock->entries[i].fileSize = (f->offset + count);
				changedBlocks[sBlock->rootIndex] = 1;
			}
		}
	}
	else if(fs_stat(fd) == UINT_MAX){
		for(int i = 0; i < MAX_FILE_COUNT; i++){
			if(strcmp(rBlock->entries[i].fileName, f->fileName) == 0){
				rBlock->entries[i].fileSize = count;
				changedBlocks[sBlock->rootIndex] = 1;
			}
		}

	}
	return currAmtCopied;
}


int fs_read(int fd, void *buf, size_t count)
{
	nodePtr nd = list_get(blockList, 0);
	superblock *sBlock = (superblock *)getData(nd);
	fdOp *f = getFdOpByDescriptor(fd);
	size_t currAmtCopied = 0;
	char hold[BLOCK_SIZE];

	if (f == NULL || fs_stat(fd) < (count + f->offset)){
		return -1;
	}
	int currBlock = calcStartBlock(f->fileName, f->offset);

	if((f->offset % BLOCK_SIZE) != 0){
		currAmtCopied = f->offset % BLOCK_SIZE;
		block_read(sBlock->dataStartIndex + currBlock, hold);
		memcpy((char *)buf, hold , currAmtCopied);
		nd = list_get(blockList, (currBlock / FAT_ARRAY_SIZE) + 1);
		fat *fBlock = (fat *)getData(nd);
		currBlock = fBlock->entries[currBlock % FAT_ARRAY_SIZE];
	}
	while(currAmtCopied < count){
		if(count - currAmtCopied >= BLOCK_SIZE){

			int check = block_read(sBlock->dataStartIndex + currBlock, hold);
			if(check == -1){
				return -1;
			}
			memcpy((char *)buf + currAmtCopied, hold, BLOCK_SIZE);
			currAmtCopied += BLOCK_SIZE;

			nd = list_get(blockList, (currBlock / FAT_ARRAY_SIZE) + 1);
			fat *fBlock = (fat *)getData(nd);
			currBlock = fBlock->entries[currBlock % FAT_ARRAY_SIZE];
		}
		else {
			int check = block_read(sBlock->dataStartIndex + currBlock, hold);
			if(check == -1){
				return -1;
			}
			memcpy((char *)buf + currAmtCopied, hold, (count - currAmtCopied));
			currAmtCopied += count - currAmtCopied;
		}
	}
	return currAmtCopied;
}
