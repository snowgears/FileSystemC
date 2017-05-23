#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

//(hex)    (dec)
//0xFFFF = 65535

//global linked list of blocks
typedef struct {
    char *signature;
    uint16_t numBlocks;
    uint16_t rootIndex;
    uint16_t dataStartIndex;
    uint16_t dataBlockCount;
    uint8_t fatBlockCount;
} superblock;

typedef struct {
    uint16_t entries[2048]; //TODO in the future might want to not hardcode this upper limit???
} fat;

typedef struct {
    char *fileName;
    uint32_t fileSize;
    uint16_t dataStartIndex;
} rootEntry;

typedef struct {
    rootEntry* entries[128];
} rootDirectory;

superblock* init_superblock(){

    void* block = malloc(BLOCK_SIZE);
    int readSuccess = block_read(0, block); //take block at index 0 and copy into malloc'd block

    if(readSuccess == -1){
        printf("read disk failed (0).\n");
        return NULL;
    }

    superblock* sBlock = (superblock*)malloc(sizeof(superblock));

    //use this variable to skip over previously read bytes
    char* blockOffset = (char*)block;


    void * signature = malloc(8);
    memcpy(signature, (void *)blockOffset, 8);

    sBlock->signature = (char *)signature;
    //printf("Signature: %s\n", sBlock->signature);

    //======================================================//

    void * numBlocks = malloc(2);
    blockOffset += 8;
    memcpy(numBlocks, (void *)blockOffset , 2);

    sBlock->numBlocks = *((uint16_t*)numBlocks);
    //printf("Number of Blocks: %d\n", sBlock->numBlocks);

    //======================================================//

    void * rootIndex = malloc(2);
    blockOffset += 2;
    memcpy(rootIndex, (void *)blockOffset , 2);

    sBlock->rootIndex = *((uint16_t*)rootIndex);
    //printf("Root Directory Block Index: %d\n", sBlock->rootIndex);

    //======================================================//

    void * dataStartIndex = malloc(2);
    blockOffset += 2;
    memcpy(dataStartIndex, (void *)blockOffset , 2);

    sBlock->dataStartIndex = *((uint16_t*)dataStartIndex);
    //printf("Data Block Start Index: %d\n", sBlock->dataStartIndex);

    //======================================================//

    void * dataBlockCount = malloc(2);
    blockOffset += 2;
    memcpy(dataBlockCount, (void *)blockOffset , 2);

    sBlock->dataBlockCount = *((uint16_t*)dataBlockCount);
    //printf("Data Block Count: %d\n", sBlock->dataBlockCount);

    //======================================================//

    void * fatBlockCount = malloc(1);
    blockOffset += 2;
    memcpy(fatBlockCount, (void *)blockOffset , 1);

    sBlock->fatBlockCount = *((uint16_t*)fatBlockCount);
    //printf("Fat Block Count: %d\n", sBlock->fatBlockCount);

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

        //printf("Entry - (index, entry) =  (%d, %u)\n", i, *entry);

        blockOffset += 2;
    }

    return fBlock;
}

void init_rootDir(uint16_t rootIndex){

    void* block = malloc(BLOCK_SIZE);
    int readSuccess = block_read(rootIndex, block); //take block at index 0 and copy into malloc'd block

    if(readSuccess == -1){
        printf("read disk failed (rootIndex).\n");
        return; //TODO return null in the future
    }

    rootDirectory* rootDir = (rootDirectory*)malloc(sizeof(rootDirectory));

    //use this variable to skip over previously read bytes
    char* blockOffset = (char*)block;

    for(int i=0; i<128; i++){
        rootEntry* entry = (rootEntry*)malloc(sizeof(rootEntry));

        void * fileName = malloc(16);
        memcpy(fileName, (void *)blockOffset, 16);

        entry->fileName = (char *)fileName;
        printf("Entry - File Name: %s\n", entry->fileName);

        //======================================================//

        blockOffset += 16;
        void * fileSize = malloc(4);
        memcpy(fileSize, (void *)blockOffset , 4);

        entry->fileSize = *((uint32_t*)fileSize);
        printf("Entry - File Size: %d\n", entry->fileSize);

        //======================================================//

        blockOffset += 4;
        void * dataStartIndex = malloc(2);
        memcpy(dataStartIndex, (void *)blockOffset , 2);

        entry->dataStartIndex = *((uint16_t*)dataStartIndex);
        printf("Entry - Data Start Index: %d\n", entry->dataStartIndex);

        //move data offset by 10 to account for the unused/padding area of the entry
        blockOffset += 12;

        rootDir->entries[i] = entry;
    }

}

int fs_mount(const char *diskname)
{
    //TODO will need to initialize a new block and add to the global linked list of blocks

    int success = block_disk_open(diskname);

    if(success == -1){
        printf("diskname is invalid or already open\n");
        return -1;
    }


    superblock* sBlock = init_superblock();

    if(sBlock == NULL){
        printf("read superblock failed\n");
        return -1;
    }

    int currentIndex = 1;
    fat* fBlock;
    while(sBlock->rootIndex - currentIndex >= 1){

        //TODO in the future we will be pushing these blocks onto the global linked list...
        fBlock = init_fat(currentIndex++);

        if(fBlock == NULL){
            printf("read fat failed\n");
            return -1;
        }

        printf("fat block created\n");
    }



    init_rootDir(sBlock->rootIndex);

	return 0;
}

int fs_umount(void)
{
    //TODO will need to free block and remove from global linked list of blocks

    //all meta-information and file data must have been written out to the disk at this point
	return 0;
}

int fs_info(void)
{
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
