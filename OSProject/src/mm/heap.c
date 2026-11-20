/*
 * Kernel heap — demand-mapped, first-fit allocator on top of paging.
 *
 * The heap lives in its own PML4 slot so it never collides with the
 * direct map (KERNEL_VMA + phys, which occupies the top 2 GiB of the
 * canonical address space).  New pages are mapped from the PMM on
 * demand as the heap grows.  Payloads are 16-byte aligned.
 */
#include "heap.h"
#include "common.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"

/*
 * Heap start: PML4 slot 256 base (0xFFFF800000000000).  This is a full
 * 512 GiB region reserved for the kernel heap, safely below the
 * direct-map range and above the user address-space limit.
 */
#define KHEAP_START   0xFFFF800000000000ULL
#define HEAP_ALIGN    16

typedef struct block {
    size_t        size;    /* payload bytes; low bit = 1 if in use      */
    struct block* next;    /* next block in the address-ordered list    */
} block_t;

#define USED_BIT      ((size_t)1)
#define BLK_USED(b)   ((b)->size & USED_BIT)
#define BLK_SIZE(b)   ((b)->size & ~(size_t)(HEAP_ALIGN - 1))
#define BLK_PAYLOAD(b) ((void*)((uint8_t*)(b) + sizeof(block_t)))

static block_t*  heap_head = NULL;   /* first block (lowest address)     */
static uintptr_t heap_end  = KHEAP_START; /* first unmapped heap byte    */

/* Grow the heap by at least `need` bytes; append a free block. */
static int heap_expand(size_t need)
{
    uintptr_t new_end = (uintptr_t)PAGE_ALIGN_UP(heap_end + need);
    if (new_end <= heap_end) return -1;

    for (uintptr_t v = heap_end; v < new_end; v += PAGE_SIZE) {
        uintptr_t phys = pmm_alloc_page();
        if (phys == 0) return -1;
        paging_map_page(v, phys, PAGE_PRESENT | PAGE_RW);
    }

    block_t* blk = (block_t*)heap_end;
    blk->size = (new_end - heap_end) - sizeof(block_t);   /* free, low bit 0 */
    blk->next = NULL;

    /* Append; merge with previous block if it is free. */
    if (heap_head == NULL) {
        heap_head = blk;
    } else {
        block_t* cur = heap_head;
        while (cur->next != NULL) cur = cur->next;
        if (!BLK_USED(cur) && (uint8_t*)cur + sizeof(block_t) + BLK_SIZE(cur) == (uint8_t*)blk) {
            cur->size += sizeof(block_t) + BLK_SIZE(blk);
            cur->next = NULL;
        } else {
            cur->next = blk;
        }
    }
    heap_end = new_end;
    return 0;
}

void kheap_init(void)
{
    heap_head = NULL;
    heap_end  = KHEAP_START;
}

void* kmalloc(size_t sz)
{
    if (sz == 0) sz = 1;
    sz = (size_t)ALIGN_UP(sz, HEAP_ALIGN);

retry:
    /* First-fit scan. */
    for (block_t* b = heap_head; b != NULL; b = b->next) {
        if (BLK_USED(b)) continue;
        size_t avail = BLK_SIZE(b);
        if (avail < sz) continue;

        /* Split if the remainder can hold a header + minimum payload. */
        if (avail >= sz + sizeof(block_t) + HEAP_ALIGN) {
            block_t* split = (block_t*)((uint8_t*)BLK_PAYLOAD(b) + sz);
            split->size = avail - sz - sizeof(block_t);   /* free */
            split->next = b->next;
            b->size = sz | USED_BIT;
            b->next = split;
        } else {
            b->size = avail | USED_BIT;
        }
        return BLK_PAYLOAD(b);
    }

    /* No suitable free block — grow the heap and try again. */
    if (heap_expand(sz + sizeof(block_t)) != 0)
        return NULL;
    goto retry;
}

void* kcalloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    if (n != 0 && total / n != sz) return NULL;   /* overflow */
    void* p = kmalloc(total);
    if (p != NULL) memset(p, 0, total);
    return p;
}

void kfree(void* p)
{
    if (p == NULL) return;
    block_t* blk = (block_t*)((uint8_t*)p - sizeof(block_t));
    blk->size &= ~USED_BIT;

    /* Merge with the next block if free. */
    if (blk->next != NULL && !BLK_USED(blk->next)) {
        blk->size += sizeof(block_t) + BLK_SIZE(blk->next);
        blk->next = blk->next->next;
    }

    /* Merge with the previous block if free. */
    if (heap_head != blk) {
        block_t* prev = heap_head;
        while (prev != NULL && prev->next != blk) prev = prev->next;
        if (prev != NULL && !BLK_USED(prev)) {
            prev->size += sizeof(block_t) + BLK_SIZE(blk);
            prev->next = blk->next;
        }
    }
}

void* krealloc(void* p, size_t sz)
{
    if (p == NULL) return kmalloc(sz);
    if (sz == 0) { kfree(p); return NULL; }

    block_t* blk = (block_t*)((uint8_t*)p - sizeof(block_t));
    size_t old = BLK_SIZE(blk);
    sz = (size_t)ALIGN_UP(sz, HEAP_ALIGN);

    /* Shrink in place: split off the tail if possible. */
    if (sz <= old) {
        if (old >= sz + sizeof(block_t) + HEAP_ALIGN) {
            block_t* split = (block_t*)((uint8_t*)BLK_PAYLOAD(blk) + sz);
            split->size = old - sz - sizeof(block_t);   /* free */
            split->next = blk->next;
            blk->size = sz | USED_BIT;
            blk->next = split;
            /* Merge split with its neighbour if free. */
            if (split->next != NULL && !BLK_USED(split->next)) {
                split->size += sizeof(block_t) + BLK_SIZE(split->next);
                split->next = split->next->next;
            }
        }
        return p;
    }

    /* Grow: try to absorb a free next block. */
    if (blk->next != NULL && !BLK_USED(blk->next)) {
        size_t combined = old + sizeof(block_t) + BLK_SIZE(blk->next);
        if (combined >= sz) {
            block_t* nxt = blk->next;
            blk->next = nxt->next;
            blk->size = combined;   /* still used */
            /* Re-split if we took too much. */
            if (combined >= sz + sizeof(block_t) + HEAP_ALIGN) {
                block_t* split = (block_t*)((uint8_t*)BLK_PAYLOAD(blk) + sz);
                split->size = combined - sz - sizeof(block_t);
                split->next = blk->next;
                blk->size = sz | USED_BIT;
                blk->next = split;
            } else {
                blk->size = combined | USED_BIT;
            }
            return p;
        }
    }

    /* Allocate fresh and copy. */
    void* np = kmalloc(sz);
    if (np == NULL) return NULL;
    memcpy(np, p, old);
    kfree(p);
    return np;
}

size_t kheap_used(void)
{
    size_t total = 0;
    for (block_t* b = heap_head; b != NULL; b = b->next)
        if (BLK_USED(b)) total += BLK_SIZE(b);
    return total;
}
