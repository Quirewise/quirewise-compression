#ifndef QW_LZMA_SDK_ALLOCATOR_H
#define QW_LZMA_SDK_ALLOCATOR_H

#include "7zTypes.h"

typedef struct QwLzmaSdkAllocator {
    ISzAlloc interface;
    size_t memory_limit;
    size_t bytes_used;
} QwLzmaSdkAllocator;

void qw_lzma_sdk_allocator_init(QwLzmaSdkAllocator *allocator,
                                size_t memory_limit);
void qw_lzma_sdk_initialize(void);

#endif
