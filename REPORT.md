# ECS 150: Project \#4 - File system Report

## Implementation

### Phase 1: Mounting / Unmounting

For the **fs_mount()** function, we first started by defining our data  
structures to be used to represent the three types of blocks containing the  
meta-information about the file system (superblock, FAT, and root directory).  
We followed the specifications of these different block types as explained in  
the documentation and defined their variables accordingly. We then attached a  
*packed attribute* to each structure in order to verify the saved memory was all  
packed together without gaps.  

Once we had all of our structures defined, we were able to populate them with  
values using the provided name of a virtual disk. For each of these block  
initializations, we defined a function for each one to populate their values.  
For example, in our superblock initialization, we create a blockOffset that we  
use to read in the correct amount of memory at the correct address as we  
populate the defined values from the specifications.  

```c
void *block = malloc(BLOCK_SIZE);
block_read(0, block); //take block at index 0 and copy into malloc'd block

superblock* sBlock = (superblock*)malloc(sizeof(superblock));

//use this variable to skip over previously read bytes
char *blockOffset = (char*)block;
memcpy(sBlock->signature, (void*)blockOffset, 8);
```
This made it much easier to keep track of exactly how many bytes were being  
allocated to each variable as we traversed through the memory of the different  
block types. We then store all of these in a global linked list as we  
initialize them in order to maintain a working set of blocks in the correct  
order.  

For the **fs_unmount()** function, we loop through all of the blocks in our  
global linked list and write all of the changed blocks back to the disk. We do  
this by keeping track of an array of dirty bits throughout the program where we  
set the index of the changed block to 1 when we make a change. This increases  
the efficiency of unmounting a disk because then we do not write all blocks  
to the disk (only the necessary ones). We then free all of our variables we  
allocated memory to and call *block_disk_close()*.  

### Phase 2: File Creation / Deletion

For the **fs_create()** function, we loop through our global linked list of  
blocks and for each one we check if the block is of type *BLOCK_FAT*. If it is,  
we loop through all of the entries in the fat block to locate the first empty  
entry. Once we are at the first empty entry, we locate the first empty entry in  
our root directory and set the data start index to the index of the first empty  
FAT block. This allows us to create a new file in our root directory block while  
allocating memory to a correct location in a fat block.  

For the **fs_delete()** function, we delete a file from our virtual disk in a  
similar way to how we created a file in the method described above. We loop  
through our list of fat blocks comparing file names and if one is found that is  
a match, we use a while loop to loop through all of the fat blocks that span our  
data (since it could be multiple).  

```c
int nextBlock = fBlock->entries[fatNum];
fBlock->entries[fatNum] = 0;

while(nextBlock != 0xFFFF){
    //...
    fBlock->entries[nextBlock] = 0;
    nextBlock = fBlock->entries[nextBlock];
    //...
}
```

This allows us to easily remove a file that spans multiple fat blocks and also    
remove the corresponding file from our root directory block. (In here we also  
set our dirty bits for changed blocks for the efficient unmounting as mentioned  
earlier in the report).

For the **fs_ls()** method, we then traverse through the entries in the root    
directory block and for every one without an empty name, we print the file name,  
file size, and the data block start index.      

### Phase 3: File Descriptor Operations

For the **fs_open()** method, we implemented our file descriptors using a struct  
which we called *fdOp* which contains the name of the associated file and the  
memory offset for reading and writing. Since there was a maximum amount of file  
descriptors that could be open at one time (32), we stored all of the fdOp  
structs in a global statically allocated array of size 32. We then use the  
indices of this array to represent the integer of the actual file descriptor. We  
did this because with this implementation we are able to get our fdOp structs  
(and by the same logic, the associated file name) in O(1) time given a file  
descriptor.    

For the **fs_close()** method, we are able to use the same logic as above only  
instead of returning an open index in the file descriptor array, we can simply  
clear the entry in the array at the provided index.  

For the **fs_lseek()** method, we are able to use the same logic as *fs_close()*  
only instead of removing the entire entry at the index, we set the offset  
variable of the entry (*of type fdOp*) to the provided offset.  

For the **fs_stat()** method, we loop through the entries of our root directory  
block and once we find the one with the matching file name (which we get using  
the same method as used in the previous 3 methods), we print the file size.   

### Phase 4: File Reading / Writing

For the **fs_read()** and the **fs_write()** methods, we needed to know exactly  
which block we wanted to start at based on the offset. In order to implement   
this we created a helper function called *calcStartBlock()*, which used division  
on the offset in order to calculate the block that we want to start our read or   
write on. In both read and write, we considered a combination of 4 possible   
cases when accessing blocks: accessing from the start of a block, accessing from  
the middle of a block, accessing to the middle of a block, or accessing to the   
end of a block. After reading or writing to the block, the next block is found   
based on the information in our FAT. For write, if the block contains our   
*FAT_EOC* and we still have more to write, another helper function called   
*findEmptyBlock()* would parse the FAT in order to find an new block to "append"  
to our file. The actual process of reading and writing is done with the   
functions *block_read()* and *block_write()* in combination with our buffer in   
order to execute the desired function. We update an offset counter every time we     
read or write in order to append read blocks to each other in read, or to write     
subsequent parts of our buffer to the file.   

## Sources Used:

* [CPP Reference: memcpy](http://en.cppreference.com/w/cpp/string/byte/memcpy)
* [CPP Reference: strcpy](http://www.cplusplus.com/reference/cstring/strcpy/)

### Authors
Tanner,Embry,913351817,tembry,tnembry@ucdavis.edu  
Kevin,Pham,999715697,kdpham,kdpham@ucdavis.edu
