#ifndef HEAP_H
#define HEAP_H

#include "types.h"

/*
 * Kernel heap — built on top of paging (demand-mapped anonymous pages).
 * kmalloc/kfree/krealloc are the general-purpose allocators used by
 * the whole kernel.
 */

void  kheap_init(void);
void* kmalloc(size_t sz);
void* kcalloc(size_t n, size_t sz);
void* krealloc(void* p, size_t sz);
void  kfree(void* p);
size_t kheap_used(void);

#endif /* HEAP_H */
