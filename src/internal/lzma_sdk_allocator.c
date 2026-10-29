#include "lzma_sdk_allocator.h"

#include "7zCrc.h"
#include "XzCrc64.h"

#include <stddef.h>
#include <stdatomic.h>
#include <stdlib.h>

typedef union QwLzmaSdkAllocationHeader {
    max_align_t alignment;
    size_t size;
} QwLzmaSdkAllocationHeader;

static void *qw_lzma_sdk_allocate(ISzAllocPtr interface, size_t size)
{
    QwLzmaSdkAllocator *allocator =
        (QwLzmaSdkAllocator *)(void *)interface;
    QwLzmaSdkAllocationHeader *header;
    size_t allocation_size;

    if (!size) return NULL;
    if (size > SIZE_MAX - sizeof(*header)) {
        return NULL;
    }
    allocation_size = sizeof(*header) + size;
    if (allocation_size > allocator->memory_limit ||
        allocator->bytes_used > allocator->memory_limit - allocation_size) {
        return NULL;
    }
    header = (QwLzmaSdkAllocationHeader *)malloc(allocation_size);
    if (!header) return NULL;
    header->size = allocation_size;
    allocator->bytes_used += allocation_size;
    return (void *)(header + 1);
}

static void qw_lzma_sdk_free(ISzAllocPtr interface, void *address)
{
    QwLzmaSdkAllocator *allocator =
        (QwLzmaSdkAllocator *)(void *)interface;
    QwLzmaSdkAllocationHeader *header;

    if (!address) return;
    header = (QwLzmaSdkAllocationHeader *)address - 1;
    if (header->size <= allocator->bytes_used) {
        allocator->bytes_used -= header->size;
    }
    free(header);
}

void qw_lzma_sdk_allocator_init(QwLzmaSdkAllocator *allocator,
                                size_t memory_limit)
{
    if (!allocator) return;
    allocator->interface.Alloc = qw_lzma_sdk_allocate;
    allocator->interface.Free = qw_lzma_sdk_free;
    allocator->memory_limit = memory_limit;
    allocator->bytes_used = 0u;
}

void qw_lzma_sdk_initialize(void)
{
    static atomic_flag lock = ATOMIC_FLAG_INIT;
    static int initialized;

    while (atomic_flag_test_and_set_explicit(&lock, memory_order_acquire)) {
    }
    if (!initialized) {
        CrcGenerateTable();
        Crc64GenerateTable();
        initialized = 1;
    }
    atomic_flag_clear_explicit(&lock, memory_order_release);
}
