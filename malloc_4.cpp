//
// Created by eyall on 12/13/2019.
//

#include <unistd.h>
#include <iostream>
#include <sys/mman.h>
#include <cstring>

#define ZERO 0
#define MAX_SMALLOC_SIZE 100000000
#define MMAP_THRESHOLD 131072
#define MINIMUM_SPLIT_BLOCK_SIZE 128

struct MallocMetadata;

struct MallocMetadata {
    size_t user_size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
};


// the list is sorted by mem low to high addresses by design
MallocMetadata* sbrk_blocks_head = NULL;

MallocMetadata* mmap_blocks_head = NULL;

MallocMetadata* getWilderness(){
    if(!sbrk_blocks_head){
        return NULL;
    }
    MallocMetadata* iterator=sbrk_blocks_head;
    while(iterator->next){
        iterator=iterator->next;
    }
    return iterator;
}

//ptr - pointer to the block start (metadata)
// return the pointer to the start of block
void* getUserAllocAddress(MallocMetadata *system_alloc_addr) {
    system_alloc_addr+=1;
    return (void*) system_alloc_addr;

}

bool metadataExistsInList(MallocMetadata **head, MallocMetadata *metadata){
    MallocMetadata* iterator=*head;
    while(iterator){
        if(iterator==metadata){
            return true;
        }
        iterator=iterator->next;
    }
    return false;
}

MallocMetadata* getMetadata(void* user_alloc_address){ //returns NULL
    MallocMetadata *md_addrress = (MallocMetadata*)user_alloc_address;
    md_addrress -=1;
    if (!metadataExistsInList(&sbrk_blocks_head, md_addrress) &&
    !metadataExistsInList(&mmap_blocks_head, md_addrress)){
        return nullptr;
    }
    return md_addrress;
}

void splitBlockIfNeeded(size_t current_alloc_size, MallocMetadata *free_block) {
    free_block->is_free = false;

    ssize_t left_after_alloc = (ssize_t)(free_block->user_size -
                    current_alloc_size - sizeof(MallocMetadata));
    if(left_after_alloc >= MINIMUM_SPLIT_BLOCK_SIZE) {
        void *split_block_system_add = getUserAllocAddress(free_block);
        // using char* so the compiler will add current_alloc_size * 1 (size of char)
        split_block_system_add = ((char*)split_block_system_add) + current_alloc_size;
        MallocMetadata *split_block_md = (MallocMetadata*)split_block_system_add;
        split_block_md->user_size = (size_t)left_after_alloc;
        split_block_md->prev = free_block;
        split_block_md->next = free_block->next;
        if(split_block_md->next) {
            split_block_md->next->prev = split_block_md;
        }
        split_block_md->is_free = true;

        free_block->next = split_block_md;
        free_block->user_size = current_alloc_size;
    }
}

void* getFirstFreeBlock(size_t size) {
    MallocMetadata *iterator = sbrk_blocks_head;
    while(iterator) {
        if(iterator->is_free && size <= iterator->user_size) {
            splitBlockIfNeeded(size, iterator);
            return iterator;
        }
        iterator = iterator->next;
    }

    MallocMetadata *wilderness = getWilderness();
    if(!wilderness || !wilderness->is_free) {
        return NULL;
    }

    size_t missing_chunk = size - wilderness->user_size;
    while (missing_chunk % 8 != 0) {
        missing_chunk++;
    }
    void* chunk_alloc = sbrk(missing_chunk);
    if ((void*)(-1) == chunk_alloc){
        return (void*)(-1);
    }
    wilderness->is_free = false;
    wilderness->user_size = size;
    return wilderness;
}

void removeBlockFromList(MallocMetadata **head, MallocMetadata*
block_to_remove){
    if (!block_to_remove->prev){ //first block in list
        *head=block_to_remove->next;
        if (block_to_remove->next){//maybe only one block in list
            block_to_remove->next->prev=nullptr;
        }
        return;
    }
    if (!block_to_remove->next){ //last block in list
        block_to_remove->prev->next= nullptr;
        return;
    }
    //if we got here the block has prev and next blocks
    block_to_remove->prev->next=block_to_remove->next;
    block_to_remove->next->prev=block_to_remove->prev;
}

void pushBackBlockToList(MallocMetadata **head, MallocMetadata *new_block) {
    if (!(*head)) {
        *head = new_block;
        return;
    }

    MallocMetadata *iterator = *head;
    while (iterator->next) {
        iterator = iterator->next;
    }
    iterator->next = new_block;
    new_block->prev = iterator;
}

void* allocateMmapBlock(size_t size) {
    void *new_block = mmap(0, size+ sizeof(MallocMetadata),
            PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if ((void*)(-1) == new_block){
        return NULL;
    }
    MallocMetadata *metadata = (MallocMetadata*)new_block;
    metadata->is_free=false;
    metadata->user_size = size;
    metadata->prev = NULL;
    metadata->next = NULL;
    pushBackBlockToList(&mmap_blocks_head, metadata);

    return getUserAllocAddress(metadata);
}

void* allocateSbrkBlock(size_t size) {
    void* f_block=getFirstFreeBlock(size);
    if ((void*)(-1) == f_block ){ // sbrk of wilderness failed
        return NULL;
    }
    MallocMetadata* free_block = (MallocMetadata*)f_block;
    if(free_block) {
        return getUserAllocAddress(free_block);
    }

    MallocMetadata* metadata =
            (MallocMetadata*) sbrk(size + sizeof(MallocMetadata));
    if ((void*)(-1) == metadata){
        return NULL;
    }

    metadata->user_size = size;
    metadata->is_free = false;
    metadata->next = NULL;
    metadata->prev = NULL;
    pushBackBlockToList(&sbrk_blocks_head, metadata);

    return getUserAllocAddress(metadata);
}

void* smalloc(size_t size) {
    if (ZERO == size || size > MAX_SMALLOC_SIZE){
        return NULL;
    }
    if(size >= MMAP_THRESHOLD) {
        return allocateMmapBlock(size);
    }
    while (size % 8 != 0) {
        size++;
    }
    return allocateSbrkBlock(size);
}

void* scalloc(size_t num, size_t size) {
    if(ZERO == size) {
        return NULL;
    }
    size_t block_size = num*size;
    void *block = smalloc(block_size);
    if(!block) {
        return block;
    }
    std::memset(block, 0, block_size);
    return block;
}

//returns metadata of merged blocked
MallocMetadata* mergeBlocks(MallocMetadata* left_block, MallocMetadata*
right_block){
    left_block->user_size+=right_block->user_size += sizeof(MallocMetadata);
    left_block->next = right_block->next;
    if (left_block->next){
        left_block->next->prev=left_block;
    }
    return left_block;
}

void mergeFreeAdjacentBlocksIfNeeded(MallocMetadata *curr_block){
    MallocMetadata *prev_block=NULL, *next_block=NULL, *merged_block=NULL;
    if (curr_block->prev && curr_block->prev->is_free){
        prev_block=curr_block->prev;
        merged_block=mergeBlocks(prev_block,curr_block); //merge prev with
        // current
    }
    if (curr_block->next && curr_block->next->is_free){
        next_block=curr_block->next;
        if(merged_block){
            mergeBlocks(merged_block,next_block); //merge all 3 blocks
        }
        else{
            mergeBlocks(curr_block,next_block); //merge current with next
        }
    }
}

void freeMmapBlock(MallocMetadata* block_to_free){
    removeBlockFromList(&mmap_blocks_head,block_to_free);
    munmap((void*)block_to_free,block_to_free->user_size+ sizeof
    (MallocMetadata)); //should return 0 in case of success
}

void sfree(void* p){
    if(!p) {
        return;
    }
    MallocMetadata* metadata = getMetadata(p);
    if(!metadata || metadata->is_free) {
        return;
    }
    if (metadata->user_size>=MMAP_THRESHOLD){
        freeMmapBlock(metadata);
        return;
    }
    metadata->is_free = true;
    mergeFreeAdjacentBlocksIfNeeded(metadata);
}

void* reallocMergeAux(MallocMetadata* left_block,MallocMetadata* right_block,
        size_t realloc_size){
    MallocMetadata* realloc=mergeBlocks(left_block, right_block);
    splitBlockIfNeeded(realloc_size,realloc);
    return getUserAllocAddress(realloc);
}

void* reallocMergeAdjacentBlocksIfNeeded(MallocMetadata *curr_metadata, size_t
realloc_size){
    size_t curr_size=curr_metadata->user_size, prev_size=0,next_size=0;
    bool next_md=false,prev_md=false;
    if (curr_metadata->next && curr_metadata->next->is_free){
        next_size=curr_metadata->next->user_size;
        next_md=true;
    }
    if (curr_metadata->prev && curr_metadata->prev->is_free){
        prev_size=curr_metadata->prev->user_size;
        prev_md=true;
    }
    if (next_md && next_size+curr_size +sizeof(MallocMetadata)>=realloc_size)
    { //priority b
        return reallocMergeAux(curr_metadata,curr_metadata->next,realloc_size);
    }
    if (prev_md && prev_size+curr_size +sizeof(MallocMetadata)>=realloc_size)
    { //priority c
        return reallocMergeAux(curr_metadata->prev,curr_metadata,realloc_size);
    }
    if (next_md && prev_md && (prev_size+curr_size+next_size+2*sizeof(MallocMetadata))
                              >=realloc_size){
        //priority d
        return reallocMergeAux(mergeBlocks(curr_metadata->prev,curr_metadata)
                ,curr_metadata->next,realloc_size);
    }
    return nullptr; //no adjacent blocks to merge
}

void* enlargeWilderness(size_t requested_size){
    MallocMetadata* wilderness=getWilderness();
    size_t enlargment_size= requested_size-wilderness->user_size;
    void* added_chunk=sbrk(enlargment_size);
    if ((void*)(-1)==added_chunk){
        return nullptr;
    }
    wilderness->user_size=requested_size;
    return getUserAllocAddress(wilderness);
}

size_t minSize(size_t size1, size_t size2){
    if (size1>=size2) return size2;
    return size1;
}

void* reallocMmap(void* oldp, size_t realloc_size){
    MallocMetadata* metadata=getMetadata(oldp);
    if (metadata->user_size==realloc_size){
        return oldp; //same size-no need to mmap and memcpy
    }
    void* realloc_block=smalloc(realloc_size); //mmap new block and add to list
    if (!realloc_block){
        return nullptr;
    }
    memcpy(realloc_block,oldp,minSize(metadata->user_size,realloc_size));
    freeMmapBlock(metadata);
    return realloc_block;
}

void* reallocSbrk(void* oldp, size_t realloc_size){
    MallocMetadata* metadata=getMetadata(oldp);
    while (realloc_size % 8 != 0) { // Challenge 5
        realloc_size++;
    }
    if (realloc_size <= metadata->user_size){
        splitBlockIfNeeded(realloc_size,metadata);
        return oldp; //priority a
    }
    if (metadata==getWilderness()){
        return enlargeWilderness(realloc_size); //enlrage wilderness if possible
    }
    void* realloc_block= reallocMergeAdjacentBlocksIfNeeded(metadata, realloc_size);//priorities b,c,d
    if(!realloc_block) {
        realloc_block = smalloc(realloc_size); //priorities e,f
        if (!realloc_block) {
            return nullptr;
        }
        sfree(oldp); //free only after priorities e,f
    }
    memcpy(realloc_block,oldp,minSize(realloc_size,metadata->user_size));
    return realloc_block;
}


void* srealloc(void* oldp, size_t size){
    if (size ==ZERO || size>MAX_SMALLOC_SIZE){
        return nullptr;
    }
    if (!oldp){
        return smalloc(size);
    }
    MallocMetadata* metadata=getMetadata(oldp);
    if (metadata->user_size>=MMAP_THRESHOLD){
        return reallocMmap(oldp,size);
    }
    return reallocSbrk(oldp,size);
}

size_t _num_free_blocks(){ //only for sbrk list
    if (!sbrk_blocks_head){
        return ZERO;
    }
    size_t counter=0;
    MallocMetadata *iterator = sbrk_blocks_head;
    while(iterator) {
        if (iterator->is_free){
            counter++;
        }
        iterator=iterator->next;
    }
    return counter;
}

size_t _num_free_bytes(){ //only for sbrk list
    if (!sbrk_blocks_head){
        return ZERO;
    }
    size_t free_bytes=0;
    MallocMetadata *iterator = sbrk_blocks_head;
    while(iterator) {
        if (iterator->is_free){
            free_bytes+=iterator->user_size;
        }
        iterator = iterator->next;
    }
    return free_bytes;
}

size_t __num_allocated_blocks_in_list(MallocMetadata *head){
    size_t counter=0;
    MallocMetadata *iterator = head;
    while(iterator) {
        counter++;
        iterator = iterator->next;
    }
    return counter;
}

size_t _num_allocated_blocks(){
    return __num_allocated_blocks_in_list(sbrk_blocks_head)
    +__num_allocated_blocks_in_list(mmap_blocks_head);
}

size_t __num_allocated_bytes_in_list(MallocMetadata *head){
    size_t allocated_bytes =0;
    MallocMetadata* iterator = head;
    while(iterator) {
        allocated_bytes += iterator->user_size;
        iterator = iterator->next;
    }
    return allocated_bytes;
}

size_t _num_allocated_bytes() {
    return __num_allocated_bytes_in_list(sbrk_blocks_head)
           +__num_allocated_bytes_in_list(mmap_blocks_head);
}

size_t __num_meta_data_bytes_in_list(MallocMetadata *head){
    size_t blocks =0;
    MallocMetadata* iterator = head;
    while(iterator) {
        blocks++;
        iterator = iterator->next;
    }
    return blocks * sizeof(MallocMetadata);
}

size_t _num_meta_data_bytes() {
    return __num_meta_data_bytes_in_list(sbrk_blocks_head)
           + __num_meta_data_bytes_in_list(mmap_blocks_head);
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}
