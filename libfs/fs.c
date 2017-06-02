#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <sys/mman.h>
#include "disk.h"
#include "fs.h"
#include "linkedlist.h"

#define FAT_EOC 0xFFFF

#define BLOCK_SUPER 11
#define BLOCK_FAT 12
#define BLOCK_ROOT 13
#define BLOCK_DATA 14

#define STRUCT_FDOP 15

//global linked list of blocks
list_t blockList;
//global linked list of files that are open with what file descriptors and offests they have
//list_t openFilesList;
//global file descriptor count to make sure that each file descriptor is unique
int fdCount = 0;

int debug = 0;

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

typedef struct {
    char fileName[16];
    size_t offset;
} fdOp;

int *changedBlocks;

fdOp *fileDes[32];

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
        printf("read disk failed (1).\n");
        return NULL;
    }

    char* blockOffset = (char*)block;

    for(int i=0; i<2048; i++){
        uint16_t* entry = (uint16_t*)malloc(2);

        memcpy(entry, (void *)blockOffset, 2);
	if(debug == 1){
		if(*entry == 0xFFFF){
			printf("FAT[%d] = 0xFFFF\n", i);
		}
		else if(*entry != 0){
			printf("FAT[%d] = %d\n", i, *entry);
		}
	}
        fBlock->entries[i] = *entry;

        blockOffset += 2;
    }
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

    //use this variable to skip over previously read bytes
    char* blockOffset = (char*)block;

    for(int i=0; i<128; i++){
        rootEntry* entry = (rootEntry*)malloc(sizeof(rootEntry));

        memcpy(entry->fileName, (void *)blockOffset, 16);

        // if(entry->fileName[0] == '\0'){
        //     entry->fileName[0] = '0';
        // }
        //printf("fileName: %s\n", entry->fileName);

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

	if(entry->fileName[0] != '\0' && debug == 1){
		printf("Root[%d] : %s, %d\n", i, entry->fileName, entry->fileSize);
	}
        rootDir->entries[i] = *entry;
    }
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
	for(int i = 0; i < 32; i++){
		fileDes[i] = (fdOp*)malloc(sizeof(fdOp));
		fileDes[i]->fileName[0] = '\0';
		fileDes[i]->offset = 0;
	}	

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

	fat* fBlock = malloc(sBlock->fatBlockCount * sizeof(fat));
    while(sBlock->rootIndex - currentIndex >= 1){
        if(debug == 1){
		printf("\nFAT #%d\n", currentIndex);
	}
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

    for(int i = 0; i<listLen; i++){
 	nodePtr nd = list_get(blockList, i);
        if(changedBlocks[i] == 1){
		if(debug == 1){
			printf("Writing to blcok[%d]\n", i);
		}
	        int writeSuccess = block_write(i, getData(nd));

       		 if(writeSuccess == -1){
       		     printf("Writing to block %d failed!\n", i);
        	 	return -1;
        	}
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

	for(int i = 0; i < 32; i++){
		free(fileDes[i]);
	}

	free(changedBlocks);

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
    rootDirectory* rBlock = getRootDirectory();

    if(rBlock == NULL){
        return -1; //TODO change this to -1???
    }

	int count = 0;
	for(int i = 0; i < 128; i++){
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
	printf("rdir_free_ratio=%d/%d\n", rdir_count(), 128);
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
		if(debug == 1){
			printf("block[%d] is FAT\n", i);
		}
            fat* fBlock = (fat*) getData(nd);

        	for(int j = 0; j < 2048; j++){
        		if (fBlock->entries[j] == 0){
        			fBlock->entries[j] = FAT_EOC;

                    for(int k=0; k<128; k++){
                        //the first character of the filename of entry is '0'
                        if(rBlock->entries[k].fileName[0] == '\0'){
                            //TODO may need to find the size of fileName instead of using upper limit of 16 bytes
                            memcpy(rBlock->entries[k].fileName, (void *)filename , 16);
                            rBlock->entries[k].fileSize = 0;
                            rBlock->entries[k].dataStartIndex = j;
				
                            changedBlocks[i] = 1;
				changedBlocks[sBlock->rootIndex] = 1;
				if(debug ==1){
					printf("changed[%d]\n", i);
					printf("changed[%d]\n", sBlock->rootIndex);
				}
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
	

	
	for(int i = 0; i < 128; i++){
		if(strcmp(rBlock->entries[i].fileName, filename) == 0){
			int fatNum = rBlock->entries[i].dataStartIndex % 2048;   
//			do{
				int nextBlock = fBlock->entries[fatNum];
				fBlock->entries[fatNum] = 0;
//				fatNum = fBlock->entries[i];	
//				nd = list_get(blockList, (fatNum / 2048) + 1);
//				fBlock = (fat *)getData(nd);
			if(debug == 1){
				printf("Found %s\n", filename);
				printf("FAT[%d] = %d\n", fatNum, fBlock->entries[fatNum]);
			}
//			}while(fBlock->entries[fatNum] != 0xFFFF);
			while(nextBlock != 0xFFFF){
				
				nd = list_get(blockList, (nextBlock / 2048) + 1);
				fBlock = getData(nd);
				fatNum = nextBlock;
				nextBlock = fBlock->entries[nextBlock];
				fBlock->entries[fatNum] = 0;
				if(debug == 1){
					printf("Found %s\n", filename);
					printf("FAT[%d] = %d\n", nextBlock, fBlock->entries[nextBlock]);
				}		}
			rBlock->entries[i].fileName[0] = '\0';
			rBlock->entries[i].fileSize = 0;
			rBlock->entries[i].dataStartIndex = 0;

            //delete the associated fdOp (which should always exist at this point)
            //TODO we should remove completely from the openFilesList and free but for now setting name to null should be sufficient?
	 	           for(int i = 0; i < 32; i++){
				if(strcmp(fileDes[i]->fileName, filename) == 0){
					fileDes[i]->fileName[0] = '\0';
					fileDes[i]->offset = 0;
				}
			}
		changedBlocks[j] = 1;
		changedBlocks[sBlock->rootIndex] = 1;
		if(debug == 1){
			printf("changed[%d]\n", j);
			printf("changed[%d]\n", sBlock->rootIndex);
		}
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
	for(int i = 0; i < 128; i++){
		if (rBlock->entries[i].fileName[0] != '\0'){
			printf("file: %s, size: %d, data_blk: %d\n", rBlock->entries[i].fileName, rBlock->entries[i].fileSize, rBlock->entries[i].dataStartIndex);
		}
	}
	return 0;
}

int fs_open(const char *filename)
{
    //TODO first need to check if a file with that filename exists in the file system at all
    //if it doesn't, return -1
	rootDirectory* rBlock = getRootDirectory();
    if(rBlock == NULL){
        return -1;
    }
    
	for(int i = 0; i < 128; i++){
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
        return -1; //TODO this might need to be 0???
    }

	for(int i = 0; i < 128; i++){
		if(strcmp(rBlock->entries[i].fileName, name) == 0){ //TODO this may not work due to the absence of the '\0' character
            return rBlock->entries[i].fileSize;
		}
	}

	return -1; //TODO this might need to be 0???
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
	int blockNum = offset / 4096;
	rootDirectory *rBlock = getRootDirectory();
	for(int i = 0; i < 128; i++){
		if (strcmp(rBlock->entries[i].fileName, filename) == 0){
			if(offset > rBlock->entries[i].fileSize){
				printf("offset > size\n");
				return -1;
			}
			int currBlock = rBlock->entries[i].dataStartIndex;
			for(int j = 1; j < blockNum; j++){
				nodePtr nd = list_get(blockList, (currBlock / 2048) + 1);
				fat *fBlock = (fat *)getData(nd);
				currBlock = fBlock->entries[currBlock % 2048];
			}
		return currBlock;
		}
	}
	printf("%s not found\n", filename);
	return -1;
}

int findEmptyBlock(){
	
	for(int i = 1; i < 5; i++){
		nodePtr nd = list_get(blockList, i);
		if(getType(nd) == BLOCK_FAT){
			fat *fBlock = (fat *)getData(nd);
			for(int j = 0; j < 2048; j++){
				if(fBlock->entries[j] == 0){
					if(debug == 1){
						printf("FAT[%d]: empty\n", j);
					}
					return (2048 * (i - 1)) + j;
				}
			}
		}
	}
	return -1;
}

uint32_t getFileSize(char *filename){
	rootDirectory *rBlock  = getRootDirectory();
	for(int i = 0; i < 128; i++){
		if(strcmp(rBlock->entries[i].fileName, filename) == 0){
			return rBlock->entries[i].fileSize;
		}
		return -1;
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
	char hold[4096];

	if (f == NULL){
		printf(" Write error");
		return -1;
	}
	int currBlock = calcStartBlock(f->fileName, f->offset);
	nd = list_get(blockList, (currBlock / 2048) + 1);
	fat *fBlock = (fat *)getData(nd);
	if(debug == 1){
		printf("Block: %d\n", currBlock);
	}
	if((f->offset % 4096) != 0){
		printf("offset = %lu\n", f->offset);
		printf("count = %lu\n", count);
		if(debug == 1){
			printf("Start: FAT[%d]\n", currBlock);
		}
		int offCount = f->offset % 4096;
		printf("offCount = %i\n", offCount);
		char * bounce = malloc(4096 * sizeof(char));
		block_read(sBlock->dataStartIndex + currBlock, hold);
		printf("HOLD:\n%s\n", hold);
		if(count - offCount >= 4096){
			printf("1\n");
			memcpy(hold + offCount, buf, 4096 - offCount);
			currAmtCopied = 4096 - offCount;
		}
		else {
			printf("count = %lu\n", count);
//			memcpy(bounce + offCount, buf, count);
			strncpy(bounce, buf, count);
			for(int i = 0 ; i < count; i++){
				printf("hold[%d] = %c, bounce[%d] = %c\n", offCount + i, hold[offCount + i], i, bounce[i]);
				hold[offCount + i] = bounce[i];
//				memcpy(hold + offCount + i, (uint8_t *)buf + i, 1);
			}
//			strncpy(hold + offCount, buf, count);
			currAmtCopied = count;
		}
//		hold[0] = 'X';
		printf("HOLD:\n%s\n", hold);
		block_write(sBlock->dataStartIndex + currBlock, hold);
		if(fBlock->entries[currBlock % 2048] != 0xFFFF){	
			printf("next\n");
			nd = list_get(blockList, (currBlock / 2048) + 1);
			fBlock = (fat *)getData(nd);
			currBlock = fBlock->entries[currBlock % 2048];
		}
		else if(count - currAmtCopied > 0){
			printf("new\n");
			int newBlock = findEmptyBlock();
			fBlock->entries[currBlock] = newBlock;
			currBlock = newBlock;
			nd = list_get(blockList, (currBlock / 2048) + 1);
			fBlock = (fat *)getData(nd);
			fBlock->entries[currBlock % 2048] = 0xFFFF;
		}
		if(debug == 1){
			printf("Copied: %lu\n", currAmtCopied);
		}
//		free(bounce);
	}
	while(currAmtCopied < count){
		if(count - currAmtCopied >= 4096){
			if(debug == 1){
				printf("Loop: FAT[%d]\n", currBlock);
			}
			
//			int check = block_read(sBlock->dataStartIndex + currBlock, hold);
//			if(check == -1){
//				printf("fd is invalid\n");
//				return -1;
//			}
			memcpy(hold, (char *)buf + currAmtCopied, 4096);
			currAmtCopied += 4096;
			block_write(sBlock->dataStartIndex + currBlock, hold);

			if(fBlock->entries[currBlock % 2048] != 0xFFFF){
				nd = list_get(blockList, (currBlock / 2048) + 1);
				fBlock = (fat *)getData(nd);
				currBlock = fBlock->entries[currBlock % 2048];
			}
			else if(count - currAmtCopied > 0){
				int newBlock = findEmptyBlock();
				fBlock->entries[currBlock] = newBlock;
				currBlock = newBlock;
				nd = list_get(blockList, (currBlock / 2048) + 1);
				fBlock = (fat *)getData(nd);
				fBlock->entries[currBlock % 2048] = 0xFFFF;	
			}
			if(debug == 1){
				printf("Copied: %lu\n", currAmtCopied);
			}
		}
		else {
			if(debug ==1){
				printf("Finish: FAT[%d]\n", currBlock);
			}
//			int check = block_read(sBlock->dataStartIndex + currBlock, hold);
//			if(check == -1){
//				printf("fd is invalid\n");
//				return -1;
//			}

			memcpy(hold, (char *)buf + currAmtCopied, (count - currAmtCopied));
			block_write(sBlock->dataStartIndex + currBlock, hold);

			currAmtCopied += count - currAmtCopied;
			if(debug == 1){
				printf("Copied: %lu\n", currAmtCopied);
			}
		}
	}
	printf("filesize = %u\n", fs_stat(fd));
	if(fs_stat(fd) <= f->offset + count){
		for(int i = 0; i < 128; i++){
			if(strcmp(rBlock->entries[i].fileName, f->fileName) == 0){
				rBlock->entries[i].fileSize = (f->offset + count);
				changedBlocks[sBlock->rootIndex] = 1;
				if(debug == 1){
					printf("%s size: %lu\n", f->fileName, f->offset + count);
					printf("changed[%d]\n", sBlock->rootIndex);
				}
			}
		}
	}
	else if(fs_stat(fd) == UINT_MAX){
		for(int i = 0; i < 128; i++){
			if(strcmp(rBlock->entries[i].fileName, f->fileName) == 0){
				rBlock->entries[i].fileSize = count;
				changedBlocks[sBlock->rootIndex] = 1;
				if(debug == 1){
					printf("%s size: %lu\n", f->fileName, f->offset + count);
					printf("changed[%d]\n", sBlock->rootIndex);
				}
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
	char hold[4096];

	if (f == NULL || getFileSize(f->fileName) < (count + f->offset)){
		printf("Read error");
		return -1;
	}
	int currBlock = calcStartBlock(f->fileName, f->offset);
	if(debug == 1){
		printf("Block: %d\n", currBlock);
	}
	if((f->offset % 4096) != 0){
		if(debug == 1){
			printf("Start: FAT[%d]\n", currBlock);
		}
		currAmtCopied = f->offset % 4096;
		block_read(sBlock->dataStartIndex + currBlock, hold);
		memcpy((char *)buf, hold , currAmtCopied);
		nd = list_get(blockList, (currBlock / 2048) + 1);
		fat *fBlock = (fat *)getData(nd);
		currBlock = fBlock->entries[currBlock % 2048];
		if(debug == 1){
			printf("Copied: %lu\n", currAmtCopied);
		}
	}
	while(currAmtCopied < count){
		if(count - currAmtCopied >= 4096){
			if(debug == 1){
				printf("Loop: FAT[%d]\n", currBlock);
			}
			
			int check = block_read(sBlock->dataStartIndex + currBlock, hold);
			if(check == -1){
				printf("fd is invalid\n");
				return -1;
			}
			memcpy((char *)buf + currAmtCopied, hold, 4096);
			currAmtCopied += 4096;

			nd = list_get(blockList, (currBlock / 2048) + 1);
			fat *fBlock = (fat *)getData(nd);
			currBlock = fBlock->entries[currBlock % 2048];
			if(debug == 1){
				printf("Copied: %lu\n", currAmtCopied);
			}
		}
		else {
			if(debug ==1){
				printf("Finish: FAT[%d]\n", currBlock);
			}
			int check = block_read(sBlock->dataStartIndex + currBlock, hold);
			if(check == -1){
				printf("fd is invalid\n");
				return -1;
			}

			memcpy((char *)buf + currAmtCopied, hold, (count - currAmtCopied));
					
			currAmtCopied += count - currAmtCopied;
			if(debug == 1){
				printf("Copied: %lu\n", currAmtCopied);
			}
		}
	}
	return currAmtCopied;
}
