//
// Created by eyall on 12/12/2019.
//

#include <unistd.h>
#define ZERO 0
#define MAX_SMALLOC_SIZE 100000000

void* smalloc(size_t size){
    if (size==ZERO || size>MAX_SMALLOC_SIZE){
        return NULL;
    }
    void* res=sbrk(size);
    if (res==(void*)(-1)){
        return NULL;
    }
    return res;
}
