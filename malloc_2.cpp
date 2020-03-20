//
// Created by eyall on 12/12/2019.
//

#include <unistd.h>
#include <cstring>

#define ZERO 0
#define MAX_SMALLOC_SIZE 100000000

struct MallocMetadata;

struct MallocMetadata {
    size_t user_size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
};

// the list is sorted by mem low to high addresses by design
MallocMetadata* meta_data_head = NULL;

MallocMetadata* findFirstFreeBlock(size_t size) {
    MallocMetadata *iterator = meta_data_head;
    while(iterator) {
        if(iterator->is_free && size <= iterator->user_size) {
            iterator->is_free = false;
            return iterator;
        }
        iterator = iterator->next;
    }
    return NULL;
}
//ptr - pointer to the block start (metadata)
// return the pointer to the start of block
void* getUserAllocAddress(MallocMetadata *system_alloc_addr) {
    MallocMetadata *md_addrress = (MallocMetadata*)system_alloc_addr;
    md_addrress +=1;
    return (void*) md_addrress;

}

MallocMetadata* getMetadata(void* user_alloc_address){
    MallocMetadata *md_addrress = (MallocMetadata*)user_alloc_address;
    md_addrress -=1;
    MallocMetadata* iterator=meta_data_head;
    while (iterator){
        if (iterator==md_addrress){
            return md_addrress;
        }
        iterator=iterator->next;
    }
    return nullptr;
}

void addNewBlockToList(void* new_alloc) {
    MallocMetadata *new_alloc_md = (MallocMetadata *) new_alloc;

    if (!meta_data_head) {
        meta_data_head = new_alloc_md;
        return;
    }

    MallocMetadata *iterator = meta_data_head;
    while (iterator->next) {
        iterator = iterator->next;
    }
    iterator->next = new_alloc_md;
    new_alloc_md->prev = iterator;
}

void* smalloc(size_t size) {
    if (ZERO == size || size > MAX_SMALLOC_SIZE){
        return NULL;
    }

    MallocMetadata* free_block = findFirstFreeBlock(size);
    if(free_block) {
        return getUserAllocAddress(free_block);
    }

    MallocMetadata* new_alloc =
            (MallocMetadata*) sbrk(size + sizeof(MallocMetadata));
    if ((void*)(-1) == new_alloc){
        return NULL;
    }

    MallocMetadata metadata;
    metadata.user_size = size;
    metadata.is_free = false;
    metadata.next = NULL;
    metadata.prev = NULL;
    *new_alloc = metadata;
    addNewBlockToList(new_alloc);

    void *returnAdd = getUserAllocAddress(new_alloc);
    return returnAdd;
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

void sfree(void* p){
    if(!p) {
        return;
    }
    MallocMetadata* metadata = getMetadata(p);
    if (!metadata) {
        return;
    }
    metadata->is_free = true;
}

size_t minSize(size_t size1, size_t size2){
    if (size1>=size2) return size2;
    return size1;
}

void* srealloc(void* oldp, size_t size){
    if (size ==ZERO || size>MAX_SMALLOC_SIZE){
        return nullptr;
    }
    if (!oldp){
         return smalloc(size);
    }
    MallocMetadata* metadata=getMetadata(oldp);
    if (size<metadata->user_size){
        return oldp;
    }
    void* newp=smalloc(size);
    if (!newp){
        return nullptr;
    }
    memcpy(newp,oldp,minSize(size,metadata->user_size));
    sfree(oldp);
    return newp;
}

size_t _num_free_blocks(){
    if (!meta_data_head){
        return ZERO;
    }
    size_t counter=0;
    MallocMetadata *iterator = meta_data_head;
    while(iterator) {
        if (iterator->is_free){
            counter++;
        }
        iterator=iterator->next;
    }
    return counter;
}

size_t _num_free_bytes(){
    if (!meta_data_head){
        return ZERO;
    }
    size_t free_bytes=0;
    MallocMetadata *iterator = meta_data_head;
    while(iterator) {
        if (iterator->is_free){
            free_bytes+=iterator->user_size;
        }
        iterator = iterator->next;
    }
    return free_bytes;
}

size_t _num_allocated_blocks(){
    if (!meta_data_head){
        return ZERO;
    }
    size_t counter=0;
    MallocMetadata *iterator = meta_data_head;
    while(iterator) {
        counter++;
        iterator = iterator->next;
    }
    return counter;
}

size_t _num_allocated_bytes() {
    size_t allocated_bytes =0;
    MallocMetadata* iterator = meta_data_head;

    while(iterator) {
        allocated_bytes += iterator->user_size;
        iterator = iterator->next;
    }
    return allocated_bytes;
}

size_t _num_meta_data_bytes() {
    size_t blocks =0;
    MallocMetadata* iterator = meta_data_head;

    while(iterator) {
        blocks++;
        iterator = iterator->next;
    }
    return blocks * sizeof(MallocMetadata);
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}

