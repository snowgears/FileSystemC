#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include "disk.h"
#include "fs.h"
#include "linkedlist.h"

//(hex)    (dec)
//0xFFFF = 65535
#define BLOCK_SUPER 11
#define BLOCK_FAT 12
#define BLOCK_ROOT 13
#define BLOCK_DATA 14

list_t blockList;

//global linked list of blocks
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
    uint16_t entries[2048]; //TODO in the future might want to not hardcode this upper limit???
} fat;

typedef struct {
    char fileName[16];
    uint32_t fileSize;
    uint16_t dataStartIndex;
    char padding[10];
} __attribute__((packed)) rootEntry;

typedef struct {
    rootEntry entries[128];
} __attribute__((packed)) rootDirectory;

superblock* init_superblock(){

    void* block = malloc(BLOCK_SIZE);
    int readSuccess = block_read(0, block); //take block at index 0 and copy into malloc'd block

    if(readSuccess == -1){
        printf("read disk failed (0).\n");
        return NULL;
    }

    superblock* sBlock = (superblock*)malloc(sizeof(superblock));

//	printf("Superblock:\n");

    //use this variable to skip over previously read bytes
    char* blockOffset = (char*)block;


    //void * signature = malloc(8);
    memcpy(sBlock->signature, (void *)blockOffset, 8);

    //sBlock->signature = (char *)signature;
//    printf("Signature: %s\n", sBlock->signature);

    //======================================================//

    void * numBlocks = malloc(2);
    blockOffset += 8;
    memcpy(numBlocks, (void *)blockOffset , 2);

    sBlock->numBlocks = *((uint16_t*)numBlocks);
//    printf("Number of Blocks: %d\n", sBlock->numBlocks);

    //======================================================//

    void * rootIndex = malloc(2);
    blockOffset += 2;
    memcpy(rootIndex, (void *)blockOffset , 2);

    sBlock->rootIndex = *((uint16_t*)rootIndex);
//    printf("Root Directory Block Index: %d\n", sBlock->rootIndex);

    //======================================================//

    void * dataStartIndex = malloc(2);
    blockOffset += 2;
    memcpy(dataStartIndex, (void *)blockOffset , 2);

    sBlock->dataStartIndex = *((uint16_t*)dataStartIndex);
//    printf("Data Block Start Index: %d\n", sBlock->dataStartIndex);

    //======================================================//

    void * dataBlockCount = malloc(2);
    blockOffset += 2;
    memcpy(dataBlockCount, (void *)blockOffset , 2);

    sBlock->dataBlockCount = *((uint16_t*)dataBlockCount);
//    printf("Data Block Count: %d\n", sBlock->dataBlockCount);

    //======================================================//

    void * fatBlockCount = malloc(1);
    blockOffset += 2;
    memcpy(fatBlockCount, (void *)blockOffset , 1);

    sBlock->fatBlockCount = *((uint16_t*)fatBlockCount);
//    printf("Fat Block Count: %d\n\n", sBlock->fatBlockCount);

    return sBlock;
}

fat* init_fat(int index){

    fat* fBlock = (fat*)malloc(sizeof(fat));

    void* block = malloc(BLOCK_SIZE);
    int readSuccess = block_read(index, block); //take block at index 0 and copy into malloc'd block

    if(readSuccess == -1){
        printf("read disk failed (1).\n");
        return NULL;
    }

    char* blockOffset = (char*)block;

    for(int i=0; i<2048; i++){
        uint16_t* entry = (uint16_t*)malloc(2);

        memcpy(entry, (void *)blockOffset, 2);

        fBlock->entries[i] = *entry;

//        printf("Entry - (index, entry) =  (%d, %u)\n", i, *entry);

        blockOffset += 2;
    }
//	printf("\n");

    return fBlock;
}

rootDirectory* init_rootDir(uint16_t rootIndex){

    void* block = malloc(BLOCK_SIZE);
    int readSuccess = block_read(rootIndex, block); //take block at index 0 and copy into malloc'd block

    if(readSuccess == -1){
        printf("read disk failed (rootIndex).\n");
        return NULL; //TODO return null in the future
    }

    rootDirectory* rootDir = (rootDirectory*)malloc(sizeof(rootDirectory));

//	printf("Root Directory\n");

    //use this variable to skip over previously read bytes
    char* blockOffset = (char*)block;

    for(int i=0; i<128; i++){
        rootEntry* entry = (rootEntry*)malloc(sizeof(rootEntry));

        //void * fileName = malloc(16);
        memcpy(entry->fileName, (void *)blockOffset, 16);

        //entry->fileName = (char *)fileName;
//        printf("Entry - File Name: %s\n", entry->fileName);

        //======================================================//

        blockOffset += 16;
        void * fileSize = malloc(4);
        memcpy(fileSize, (void *)blockOffset , 4);

        entry->fileSize = *((uint32_t*)fileSize);
//        printf("Entry - File Size: %d\n", entry->fileSize);

        //======================================================//

        blockOffset += 4;
        void * dataStartIndex = malloc(2);
        memcpy(dataStartIndex, (void *)blockOffset , 2);

        entry->dataStartIndex = *((uint16_t*)dataStartIndex);
//        printf("Entry - Data Start Index: %d\n", entry->dataStartIndex);

        //move data offset by 10 to account for the unused/padding area of the entry
        blockOffset += 12;

        rootDir->entries[i] = *entry;
    }
//	printf("\n");
	return rootDir;
}

int fs_mount(const char *diskname)
{
    int success = block_disk_open(diskname);

    if(success == -1){
        printf("diskname is invalid or already open\n");
        return -1;
    }

    blockList = list_create();


//    superblock* sBlock = init_superblock();
	superblock* sBlock = init_superblock();
    if(sBlock == NULL){
        printf("read superblock failed\n");
        return -1;
    }

    char check[9];
    strcpy(check, sBlock->signature);
    check[8] = '\0';
    if(strcmp(check, "ECS150FS") != 0){
        printf("sigantures do not match\n");
        printf("block signature: %s\n", check);
        return -1;
    }

    if(sBlock->numBlocks != block_disk_count()){
        printf("number of blocks do not match\n");
        return -1;
    }

    list_add(blockList, (void*)sBlock, BLOCK_SUPER);

    int currentIndex = 1;
//    fat* fBlock;
	fat* fBlock = malloc(sBlock->fatBlockCount * sizeof(fat));
    while(sBlock->rootIndex - currentIndex >= 1){
        //	printf("FAT #%d\n", currentIndex);
        fBlock = init_fat(currentIndex++);

        if(fBlock == NULL){
            printf("read fat failed\n");
            return -1;
        }

	    list_add(blockList, (void*)fBlock, BLOCK_FAT);
    }

    rootDirectory* rBlock = init_rootDir(sBlock->rootIndex);
    list_add(blockList, (void*)rBlock, BLOCK_ROOT);

    while(list_length(blockList) < sBlock->numBlocks){
        void * page = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
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
    printf("List length before unmounting: %d\n", listLen);
    for(int i = 0; i<listLen; i++){
        nodePtr nd = list_get(blockList, i);

        int writeSuccess = block_write(i, getData(nd));

        if(writeSuccess == -1){
            printf("Writing to block %d failed!\n", i);
            return -1;
        }

        void* data = getData(nd);
        if(getType(nd) == BLOCK_DATA){
            munmap(data, 4096);
        }
        else{
            free(data);
        }
    }
    int closeSuccess = block_disk_close();

    if(closeSuccess == -1){
        printf("No virtual disk is currently open\n");
        return -1;
    }

    int destroySuccess = list_destroy(blockList);

    if(destroySuccess == -1){
        printf("Failed to destroy list\n");
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

        	for(int j = 0; j < 2048; j++){
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
    nodePtr nd = list_get(blockList, 0);
    superblock* sBlock = (superblock*)getData(nd);

    nodePtr rootNd = list_get(blockList, sBlock->rootIndex);
    rootDirectory* rBlock = (rootDirectory*)getData(rootNd);

	int count = 0;
	for(int i = 0; i < 128; i++){
		if(rBlock->entries[i].fileSize == 0){
			count++;
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
	printf("rdir_free_ratio=%d/%d\n", rdir_count(), 128);
	return 0;
}

int fs_create(const char *filename)
{
	return 0;
}

int fs_delete(const char *filename)
{
	return 0;
}

int fs_ls(void)
{
	return 0;
}

int fs_open(const char *filename)
{
	return 0;
}

int fs_close(int fd)
{
	return 0;
}

int fs_stat(int fd)
{
	return 0;
}

int fs_lseek(int fd, size_t offset)
{
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	return 0;
}

int fs_read(int fd, void *buf, size_t count)
{
	return 0;
}
