/**
 * @brief Implementation of an 'simple' first-fit allocator
 **/

/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */

//     poolHdr_t      pool header annex      glebe
//   +--------------+----------------------+------------------------------------------------+
//   |              |                      |                                                |
//   | see notes at | see notes at         |                                                |
//   | poolHdr_t    | poolHdr_t            |                                                |
//   | typedef      | typedef              |                                                |
//   | for          | for                  |                                                |
//   | contents     | contents             |                                                |
//   |              |                      |                                                |
//   +--------------+----------------------+------------------------------------------------+
//                                         .                                                .
//                                         .                                                .
//                                         .                                                .
//                                         .    *  The glebe contains the blocks:           .
//   . . . . . . . . . . . . . . . . . . . .       ==============================           .
//   .                                                                                      .
//   .                                                                                      .
//   . used block    free block    used block        used blk  free block        used block .
//   +-------------+-------------+-----------------+---------+-----------------+------------+
//   |             |             |                 |         |                 |            |
//   |             |             |                 |         |                 |            |
//   +-------------+-------------+-----------------+---------+-----------------+------------+
//   .             .                                         .                 .
//   .             .                                         .                 .
//   .             . * Blocks have a header, space, tail     .                 .
//   .             .   =================================     .                 .
//   .             .                                         .                 .
//   . used block: . . . . . . . . . . . .         . . . . . . free block:     . . . . . . .
//   .                                   .         .                                       .
//   . blkHdr_t   payload                .         . blkHdr_t   free space                 .
//   +----------+------------------------+---+     +----------+------------------------+---+
//   |          |                        |   |     |          |                        |   |
//   | see      |                        | T |     | see      |                        | T |
//   | blkHdr_t | user-visible datablock | a |     | blkHdr_t |                        | a |
//   | typedef  |                        | i |     | typedef  |                        | i |
//   |          |                        | l |     |          |                        | l |
//   +----------+------------------------+---+     +----------+------------------------+---+

#include "ocr-config.h"
#ifdef ENABLE_ALLOCATOR_SIMPLE
#include "ocr-hal.h"
#include "debug.h"
#include "ocr-policy-domain.h"
#include "ocr-runtime-types.h"
#include "ocr-sysboot.h"
#include "ocr-types.h"
#include "utils/ocr-utils.h"
#include "simple-allocator.h"
#include "allocator/allocator-all.h"
#ifdef HAL_FSIM_CE
#include "rmd-map.h"
#endif

#define ALIGNMENT 8LL

#define DEBUG_TYPE ALLOCATOR

//helpful to run this code on x86
//#define DPRINTF(X, ...)    printf(__VA_ARGS__)
//#define ASSERT  assert

// start of simple_alloc core part

// Block header layout (old blkHeader_t)
//
// * free block
//
//  x[0]  x[1]  x[2]  x[3]  x[4]
// +-----+-----+-----+-----+-----+-----------+-----+
// |HEAD |INFO1|INFO2|NEXT |PREV |    ...    |TAIL |
// |     |     |     |     |     |           |     |
// +-----+-----+-----+-----+-----+-----------+-----+
//
// * allocated block
//
//  x[0]  x[1]  x[2]
// +-----+-----+-----+-----------------------+-----+
// |HEAD |INFO1|INFO2|          ...          |TAIL |
// |     |     |     |      user-visible     |     |
// +-----+-----+-----+-----------------------+-----+
//
// HEAD and TAIL contains full size including HEAD/INFOs/TAIL in bytes and HEAD also has MARK in its higher 16 bits.
// HEAD's bit0 is 1 for allocated block, or 0 for free block.
// i.e.  HEAD == ( MARK | size | bit0 )  , and   TAIL == size
// PEER_LEFT and PEER_RIGHT helps access neightbor blocks.
// NEXT and PREV is valid for free blocks and basically forms linked list for free list.
// INFO1 contains a pointer to the pool header which it belongs to. i.e. poolHdr_t
// INFO2 contains
// These INFOs are only for TG arch, not for x86.

// arbitrary value 0xfeef. This mark helps detect invalid ptr or double free.
#define MARK                    (0xfeefL << 48)
#define ALIGNMENT_MASK          (ALIGNMENT-1)
#define HEAD(X)                 ((X)[0])
#define TAIL(X,SIZE)            (*(u64 *)((u8 *)(X)+(SIZE)-sizeof(u64)))
#define PEER_LEFT(X)            ((X)[-( (X)[-1] >> 3 )])
#define PEER_RIGHT(X,SIZE)      (*(u64 *)((u8 *)(X)+(SIZE)))
#define GET_MARK(X)             ((((1UL << 16)-1) << 48) & (X))
#define GET_SIZE(X)             ((((1UL << 48)-1-3)    ) & (X))
#define GET_BIT0(X)             ((                   1) & (X))
#define GET_BIT1(X)             ((                   2) & (X))

#define INFO1(X)                ((X)[1])
#define INFO2(X)                ((X)[2])
#define NEXT(X)                 ((X)[3])
#define PREV(X)                 ((X)[4])
#define HEAD_TO_USER(X)         ((X)+3)
#define USER_TO_HEAD(X)         (((u64 *)(X))-3)
#define MIN_SIZE_FREE           (6*sizeof(u64))
#define ALLOC_OVERHEAD          (4*sizeof(u64))

// VALGRIND SUPPORT
//
// VALGRIND_MEMPOOL_ALLOC: If the pool was created with the is_zeroed argument set, Memcheck will mark the chunk as DEFINED, otherwise Memcheck will mark the chunk as UNDEFINED.
// VALGRIND_MEMPOOL_FREE:  Memcheck will mark the chunk associated with addr as NOACCESS, and delete its record of the chunk's existence.
// VALGRIND_MAKE_MEM_NOACCESS, VALGRIND_MAKE_MEM_UNDEFINED and VALGRIND_MAKE_MEM_DEFINED. These mark address ranges as completely inaccessible, accessible but containing undefined data, and accessible and containing defined data, respectively. They return -1, when run on Valgrind and 0 otherwise.

// VALGRIND_CREATE_MEMPOOL :
// VALGRIND_DESTROY_MEMPOOL :    // Memcheck resets the redzones of any live chunks in the pool to NOACCESS.

#ifdef ENABLE_VALGRIND
#include <valgrind/memcheck.h>
#define VALGRIND_POOL_OPEN(X)   VALGRIND_MAKE_MEM_DEFINED((X) , sizeof(pool_t))
#define VALGRIND_POOL_CLOSE(X)  VALGRIND_MAKE_MEM_NOACCESS((X), sizeof(pool_t))
#define VALGRIND_CHUNK_OPEN(X)  do {VALGRIND_MAKE_MEM_DEFINED(&HEAD(X), 3*sizeof(u64)); if (GET_BIT0(HEAD(X)) == 0) VALGRIND_MAKE_MEM_DEFINED((u64 *)(X)+3 , 2*sizeof(u64));  VALGRIND_MAKE_MEM_DEFINED(&TAIL((X), GET_SIZE(HEAD(X))), sizeof(u64)); } while(0)
#define VALGRIND_CHUNK_OPEN_INIT(X, Y)   do {VALGRIND_MAKE_MEM_DEFINED(&HEAD(X), 5*sizeof(u64)); VALGRIND_MAKE_MEM_DEFINED(&TAIL((X), (Y)), sizeof(u64)); } while(0)
#define VALGRIND_CHUNK_OPEN_LEFT(X)     VALGRIND_MAKE_MEM_DEFINED(&(X)[-1], sizeof(u64));
#define VALGRIND_CHUNK_OPEN_COND(X, Y)  if ((X) != (Y)) VALGRIND_CHUNK_OPEN(Y);
#define VALGRIND_CHUNK_CLOSE(X)         do {VALGRIND_MAKE_MEM_NOACCESS((X) , GET_SIZE(HEAD(X))); } while(0)
#define VALGRIND_CHUNK_CLOSE_COND(X, Y)  if ((X) != (Y)) VALGRIND_CHUNK_CLOSE(Y);
#else
#define VALGRIND_POOL_OPEN(X)
#define VALGRIND_POOL_CLOSE(X)
#define VALGRIND_CHUNK_OPEN(X)
#define VALGRIND_CHUNK_OPEN_INIT(X, Y)
#define VALGRIND_CHUNK_OPEN_LEFT(X)
#define VALGRIND_CHUNK_OPEN_COND(X, Y)
#define VALGRIND_CHUNK_CLOSE(X)
#define VALGRIND_CHUNK_CLOSE_COND(X, Y)
#endif

// This is for only TG. It's no-op for other arch.
// On TG, this canonicalize the given address. This ensures correct memory deallocations when
// EDTs are scheduled to other blocks and the address is passed to free() on that other block.
void *addrGlobalizeOnTG(void *result, ocrPolicyDomain_t *self)
{
#if defined(HAL_FSIM_CE)
    // ideally we'd like to use the size of each memory as the end of each area.
    // canonicalize addresses for L3 SPAD (USM -- unit shared mem)
    if((u64)result <= UR_BSM_BASE(0, 0) /* as Bala suggested */ && (u64)result >= UR_USM_BASE) {
        void *newresult = (void *)DR_USM_BASE(CHIP_FROM_ID(self->myLocation),
                                    UNIT_FROM_ID(self->myLocation))
                                    + (u64)(result - UR_USM_BASE);
        //DPRINTF(DEBUG_LVL_INFO, "USM conv: %p -> %p\n", result, newresult );
        result = newresult;
    }
    // Canonicalize addresses for L2 SPAD (BSM -- block shared mem)
    if((u64)result <= BSM_MSR_BASE(0) && (u64)result >= BR_BSM_BASE(0)) {
        void *newresult = (void *)DR_BSM_BASE(CHIP_FROM_ID(self->myLocation),
                                    UNIT_FROM_ID(self->myLocation),
                                    BLOCK_FROM_ID(self->myLocation), 0)
                                    + (u64)(result - BR_BSM_BASE(0));
        //DPRINTF(DEBUG_LVL_INFO, "BSM conv: %p -> %p\n", result, newresult );
        result = newresult;
    }
    if((u64)result <= CE_MSR_BASE && (u64)result >= BR_CE_BASE) {
        result = (void *)DR_CE_BASE(CHIP_FROM_ID(self->myLocation),
                                    UNIT_FROM_ID(self->myLocation),
                                    BLOCK_FROM_ID(self->myLocation)) +
                                    (u64)(result - BR_CE_BASE);
        u64 check = MAKE_CORE_ID(0,
                                 0,
                                 ((((u64)result >> MAP_CHIP_SHIFT) & ((1ULL<<MAP_CHIP_LEN) - 1)) - 1),
                                 ((((u64)result >> MAP_UNIT_SHIFT) & ((1ULL<<MAP_UNIT_LEN) - 1)) - 2),
                                 ((((u64)result >> MAP_BLOCK_SHIFT) & ((1ULL<<MAP_BLOCK_LEN) - 1)) - 2),
                                 ID_AGENT_CE);
        //DPRINTF(DEBUG_LVL_WARN, "check:%p , self:%p, myloc:0x%lx\n", check, self, self->myLocation);
        ASSERT(check==self->myLocation);
    }
#endif
    return result;
}


static void simpleTest(u64 start, u64 size)
{
#if 1
    // boundary check code for sanity check.
    // This helps early detection of malformed addresses.
    do {
        DPRINTF(DEBUG_LVL_INFO, "simpleBegin : pool range [%p - %p)\n", start, start+size);

        u8 *p = (u8 *)((start + size - 128)&(~0x7UL));      // at least 128 bytes
        u8 *q = (u8 *)(start + size);
        u64 size = q-p;

        while (size >= 8) {
            *((u64 *)p) = 0xdeadbeef0000dead;   // just random value
            p+=8; size -= 8;
        }
        while (size) {
            *p = '0';
            p++; size--;
        }
    } while(0);
    DPRINTF(DEBUG_LVL_INFO, "simpleBegin : simple test passed\n");
#endif
}

static void simpleInit(pool_t *pool, u64 size)
{
    u8 *p = (u8 *)pool;
    u64 *q;
    q = (u64 *)(p + sizeof(pool_t));
    ASSERT(((u64)q & ALIGNMENT_MASK) == 0);
    ASSERT((sizeof(pool_t) & ALIGNMENT_MASK) == 0);
    ASSERT((size & ALIGNMENT_MASK) == 0);
    size = size - sizeof(pool_t);

    // assume pool->lock and pool->inited is already 0 at startup
#ifdef ENABLE_VALGRIND
    VALGRIND_MAKE_MEM_DEFINED(&(pool->lock), sizeof(pool->lock));
    hal_lock32(&(pool->lock));
    VALGRIND_MAKE_MEM_NOACCESS(&(pool->lock), sizeof(pool->lock));
#else
    hal_lock32(&(pool->lock));
#endif

#ifdef ENABLE_VALGRIND
    VALGRIND_MAKE_MEM_DEFINED(&(pool->inited), sizeof(pool->inited));
#endif
    if (!(pool->inited)) {
        simpleTest((u64)pool, size+sizeof(pool_t));
        HEAD(q) = MARK | size;
        NEXT(q) = 0;
        PREV(q) = 0;
        TAIL(q,size) = size;
        pool->pool_start = (u64 *)q;
        pool->pool_end = (u64 *)(p+size+sizeof(pool_t));
        pool->freelist = (u64 *)q;
        DPRINTF(DEBUG_LVL_INFO, "init'ed pool %p, avail %ld bytes , sizeof(pool_t) = %ld\n", pool, size, sizeof(pool_t));
        pool->inited = 1;
#ifdef ENABLE_VALGRIND
        VALGRIND_CREATE_MEMPOOL(p, 0, 1);  // TODO: destory.
        VALGRIND_MAKE_MEM_NOACCESS(p, size+sizeof(pool_t));
#endif
    } else {
        DPRINTF(DEBUG_LVL_INFO, "init skip for pool %p\n", pool);
    }
#ifdef ENABLE_VALGRIND
    VALGRIND_MAKE_MEM_NOACCESS(&(pool->inited), sizeof(pool->inited));
#endif

#ifdef ENABLE_VALGRIND
    VALGRIND_MAKE_MEM_DEFINED(&(pool->lock), sizeof(pool->lock));
    hal_unlock32(&(pool->lock));
    VALGRIND_MAKE_MEM_NOACCESS(&(pool->lock), sizeof(pool->lock));
#else
    hal_unlock32(&(pool->lock));
#endif
}

static void simplePrint(pool_t *pool)
{
/*
    u64 *p = pool->freelist;
    u64 *next;
    u64 size = 0, count = 0;

    if (p == NULL) {
        DPRINTF(DEBUG_LVL_VERB, "[free list] empty.\n");
        return;
    }

    do {
        count++;
        size += GET_SIZE(HEAD(p));
        ASSERT(GET_BIT0(HEAD(p)) == 0);
        //printf("%p [%d]: size %d next %d prev %d \n", p, p-(pool->pool_start) , HEAD(p), NEXT(p), PREV(p) );
        next = NEXT(p) + pool->pool_start;
        if (next == pool->freelist)
            break;
        p = next;
    } while(1);
    DPRINTF(DEBUG_LVL_VERB, "[free list] count %ld  size %ld (%lx)\n", count, size, size);
*/
}

static void simpleInsertFree(pool_t *pool,u64 *p, u64 size)
{
    VALGRIND_CHUNK_OPEN_INIT(p, size);
    ASSERT((size & ALIGNMENT_MASK) == 0);
    HEAD(p) = MARK | size;
    TAIL(p, size) = size;

    VALGRIND_POOL_OPEN(pool);
    if (pool->freelist == NULL) {
        NEXT(p) = p-(pool->pool_start);
        PREV(p) = p-(pool->pool_start);
        pool->freelist = p;
    } else {
        u64 *q = pool->freelist;
        VALGRIND_CHUNK_OPEN(q);
        u64 *r = PREV(q)+(pool->pool_start);
        VALGRIND_CHUNK_OPEN_COND(q, r);
        NEXT(r) = p-(pool->pool_start);
        VALGRIND_CHUNK_CLOSE_COND(q, r);
        NEXT(p) = q-(pool->pool_start);
        PREV(p) = PREV(q);
        PREV(q) = p-(pool->pool_start);
        VALGRIND_CHUNK_CLOSE(q);
    }
    //simplePrint(pool);
    VALGRIND_POOL_CLOSE(pool);
    VALGRIND_CHUNK_CLOSE(p);
}

static void simpleSplitFree(pool_t *pool,u64 *p, u64 size)
{
    VALGRIND_CHUNK_OPEN_INIT(p, size);
    u64 remain = GET_SIZE(HEAD(p)) - size;
    ASSERT( remain < GET_SIZE(HEAD(p)) );
    ASSERT((size & ALIGNMENT_MASK) == 0);
    if (remain >= MIN_SIZE_FREE) {
        HEAD(p) = MARK | size | 0x1;    // in-use mark
        TAIL(p, size) = size;
        VALGRIND_CHUNK_CLOSE(p);
        simpleInsertFree(pool, &PEER_RIGHT(p, size), remain);
    } else {
        HEAD(p) |= 0x1;         // in-use mark
        VALGRIND_CHUNK_CLOSE(p);
    }
}

static void simpleDeleteFree(pool_t *pool,u64 *p)
{
    VALGRIND_POOL_OPEN(pool);
    VALGRIND_CHUNK_OPEN(p);
    u64 *next = NEXT(p) + pool->pool_start;
    u64 *prev = PREV(p) + pool->pool_start;
    ASSERT(GET_BIT0(HEAD(p)) == 0);

    if (next == p) {
        pool->freelist = NULL;
        VALGRIND_CHUNK_CLOSE(p);
        VALGRIND_POOL_CLOSE(pool);
        return;
    }
    VALGRIND_CHUNK_OPEN(next);
    VALGRIND_CHUNK_OPEN_COND(next, prev);

    NEXT(prev) = NEXT(p);
    PREV(next) = PREV(p);
    if (p == pool->freelist) {
        pool->freelist = next;
    }
    VALGRIND_CHUNK_CLOSE(p);
    VALGRIND_CHUNK_CLOSE(next);
    VALGRIND_CHUNK_CLOSE_COND(next, prev);
    VALGRIND_POOL_CLOSE(pool);
}

static void *simpleMalloc(pool_t *pool,u64 size, struct _ocrPolicyDomain_t *pd)
{
    VALGRIND_POOL_OPEN(pool);
    hal_lock32(&(pool->lock));
    u64 *p = pool->freelist;
    VALGRIND_POOL_CLOSE(pool);
    u64 *next;
#ifdef ENABLE_VALGRIND
    u64 size_orig = size;
#endif
    DPRINTF(DEBUG_LVL_VERB, "before malloc size %ld:\n", size);
    simplePrint(pool);
    if (p == NULL)
        goto exit_fail;

    size = (size + ALIGNMENT_MASK)&(~ALIGNMENT_MASK);   // ceiling
    do {
        VALGRIND_CHUNK_OPEN(p);
        if (GET_SIZE(HEAD(p)) >= size + ALLOC_OVERHEAD) {
            VALGRIND_CHUNK_CLOSE(p);
            simpleDeleteFree(pool, p);
            simpleSplitFree(pool, p, size + ALLOC_OVERHEAD);

            void *ret = HEAD_TO_USER(p);
            VALGRIND_CHUNK_OPEN(p);
            INFO1(p) = (u64)addrGlobalizeOnTG((void *)pool, pd);   // old : INFO1(p) = (u64)pool;
            INFO2(p) = (u64)addrGlobalizeOnTG((void *)ret, pd);    // old : INFO2(p) = (u64)ret;
            if ((*(u8 *)(&INFO2(p)) & POOL_HEADER_TYPE_MASK) != allocatorSimple_id) {
                DPRINTF(DEBUG_LVL_WARN, "SimpleAlloc : id != allocatorSimple_id \n");
            }
            VALGRIND_CHUNK_CLOSE(p);
            VALGRIND_POOL_OPEN(pool);
            hal_unlock32(&(pool->lock));
            VALGRIND_POOL_CLOSE(pool);
#ifdef ENABLE_VALGRIND
            VALGRIND_MEMPOOL_ALLOC(pool, ret, size_orig);
//            printf("mempool_alloc, pool %p , ret %p\n", pool, ret);
#endif
            return ret;
        }
        next = NEXT(p) + pool->pool_start;
        VALGRIND_CHUNK_CLOSE(p);
        if (next == pool->freelist)
            break;
        p = next;
    } while(1);
exit_fail:
    //DPRINTF(DEBUG_LVL_INFO, "OUT OF HEAP! malloc failed\n");
    VALGRIND_POOL_OPEN(pool);
    hal_unlock32(&(pool->lock));
    VALGRIND_POOL_CLOSE(pool);
    return NULL;
}

void simpleFree(void *p)
{
    if (p == NULL)
        return;
    u64 *q = USER_TO_HEAD(p);
    VALGRIND_CHUNK_OPEN(q);
    pool_t *pool = (pool_t *)INFO1(q);
    if (  GET_MARK(HEAD(q)) != MARK ) {
        DPRINTF(DEBUG_LVL_WARN, "SimpleAlloc : free: cannot find mark. Probably wrong address is passed to free()? %p\n", p);
    VALGRIND_CHUNK_CLOSE(q);
#ifdef ENABLE_VALGRIND
    VALGRIND_MEMPOOL_FREE(pool, p);
#endif
        return;
    }
    VALGRIND_POOL_OPEN(pool);
    u64 start = (u64)pool->pool_start;
    u64 end   = (u64)pool->pool_end;
    hal_lock32(&(pool->lock));
    VALGRIND_POOL_CLOSE(pool);
    q = USER_TO_HEAD(INFO2(q)); // For TG. no effects on x86

    if (  GET_MARK(HEAD(q)) != MARK ) {
        DPRINTF(DEBUG_LVL_WARN, "SimpleAlloc : free: mark not found %p\n", p);
        goto free_exit;
    }
    if (!(GET_BIT0(HEAD(q)))) {
        DPRINTF(DEBUG_LVL_WARN, "SimpleAlloc : free not-allocated block? double free? p=%p\n", p);
        goto free_exit;
    }
    u64 size = GET_SIZE(HEAD(q));
    if (TAIL(q, size) != size) {
        DPRINTF(DEBUG_LVL_INFO, "SimpleAlloc : two sizes doesn't match. p=%p\n", p);   // TODO: make it WARN
        goto free_exit;
    }

    DPRINTF(DEBUG_LVL_VERB, "before free : pool = %p, addr=%p\n", pool, INFO2(q));
    simplePrint(pool);

    u64 *peer_right = &PEER_RIGHT(q, size);
    if ((u64)peer_right > end) {
        DPRINTF(DEBUG_LVL_INFO, "SimpleAlloc : PEER_RIGHT address %p is above the heap area\n", peer_right);
        goto free_exit;
    }
    if ((u64)&HEAD(q) < start) {
        DPRINTF(DEBUG_LVL_INFO, "SimpleAlloc : address %p is below the heap area\n", &HEAD(q));
        goto free_exit;
    }
    VALGRIND_CHUNK_CLOSE(q);
    if ((u64)peer_right != end) {
        VALGRIND_CHUNK_OPEN(peer_right);
        if (  GET_MARK(HEAD(peer_right)) != MARK ) {
            DPRINTF(DEBUG_LVL_INFO, "SimpleAlloc : right neighbor's mark not found %p\n", p);
        } else {
            if (!(GET_BIT0(HEAD(peer_right)))) {     // right block is free?
                size += GET_SIZE(HEAD(peer_right));
                VALGRIND_CHUNK_CLOSE(peer_right);
                simpleDeleteFree(pool, peer_right);
                VALGRIND_CHUNK_OPEN(peer_right);
                HEAD(peer_right) = 0;    // erase header (and mark)
            }
        }
        VALGRIND_CHUNK_CLOSE(peer_right);
    }
    VALGRIND_CHUNK_OPEN(q);
    if ((u64)&HEAD(q) != start) {
        VALGRIND_CHUNK_OPEN_LEFT(q);
        u64 *peer_left = &PEER_LEFT(q);
        VALGRIND_CHUNK_CLOSE(q);
        // just omit chunk_close_left()
        VALGRIND_CHUNK_OPEN(peer_left);
        if (  GET_MARK(HEAD(peer_left)) != MARK ) {
            DPRINTF(DEBUG_LVL_INFO, "SimpleAlloc : left neighbor's mark not found %p\n", p);
        } else {
            if (!(GET_BIT0(HEAD(peer_left)))) {      // left block is free?
                size += GET_SIZE(HEAD(peer_left));
                HEAD(peer_left) = MARK | size;
                VALGRIND_CHUNK_CLOSE(peer_left);        // this closes old tail
                ASSERT((size & ALIGNMENT_MASK) == 0);
                VALGRIND_CHUNK_OPEN(peer_left);         // this opens new tail due to the changed size
                TAIL(peer_left, size) = size;
                VALGRIND_CHUNK_CLOSE(peer_left);
                VALGRIND_CHUNK_OPEN(q);
                HEAD(q) = 0;    // erase header (and mark)
                goto free_exit;
            }
        }
        VALGRIND_CHUNK_CLOSE(peer_left);
    } else {
        VALGRIND_CHUNK_CLOSE(q);
    }
    simpleInsertFree(pool, &HEAD(q), size);
    VALGRIND_CHUNK_OPEN(q);
free_exit:
    VALGRIND_CHUNK_CLOSE(q);
    VALGRIND_POOL_OPEN(pool);
    hal_unlock32(&(pool->lock));
    VALGRIND_POOL_CLOSE(pool);
#ifdef ENABLE_VALGRIND
    VALGRIND_MEMPOOL_FREE(pool, p);
//    printf("mempool_free, pool %p , p %p\n", pool, p);
#endif
}

// end of simple_alloc core part

void simpleDestruct(ocrAllocator_t *self) {
    DPRINTF(DEBUG_LVL_VERB, "Entered simpleDesctruct (This is x86 only?) on allocator 0x%lx\n", (u64) self);
    ASSERT(self->memoryCount == 1);
    self->memories[0]->fcts.destruct(self->memories[0]);
    /*
    // TODO: Should we do this? It is the clean thing to do but may
    // cause mismatch between way it was created and freed
    runtimeChunkFree((u64)self->memories, NULL);
    DPRINTF(DEBUG_LVL_WARN, "simpleDestruct free %p\n", (u64)self->memories );
    */
    runtimeChunkFree((u64)self, NULL);
    DPRINTF(DEBUG_LVL_INFO, "Leaving simpleDesctruct on allocator 0x%lx (free)\n", (u64) self);
}

void simpleBegin(ocrAllocator_t *self, ocrPolicyDomain_t * PD ) {
    DPRINTF(DEBUG_LVL_INFO, "Entered simpleBegin on allocator 0x%lx\n", (u64) self);
    self->pd = PD;	// I needed this here in simpleBegin, maybe because simpleMalloc() is called before simpleStart() ?
    ASSERT(self->memoryCount == 1);
    ocrAllocatorSimple_t * rself = (ocrAllocatorSimple_t *) self;

    self->memories[0]->fcts.begin(self->memories[0], PD);
    u64 poolAddr = 0;
    DPRINTF(DEBUG_LVL_INFO, "simpleBegin : poolsize 0x%llx, level %d\n",  rself->poolSize, self->memories[0]->level);
    RESULT_ASSERT(self->memories[0]->fcts.chunkAndTag(
        self->memories[0], &poolAddr, rself->poolSize,
        USER_FREE_TAG, USER_USED_TAG), ==, 0);
    rself->poolAddr = poolAddr;
    DPRINTF(DEBUG_LVL_INFO, "simpleBegin : %p\n", poolAddr);

    // Adjust alignment if required
    u64 fiddlyBits = ((u64) rself->poolAddr) & (ALIGNMENT - 1LL);
    if (fiddlyBits == 0) {
        rself->poolStorageOffset = 0;
    } else {
        rself->poolStorageOffset = ALIGNMENT - fiddlyBits;
        rself->poolAddr += rself->poolStorageOffset;
        rself->poolSize -= rself->poolStorageOffset;
    }
    rself->poolStorageSuffix = rself->poolSize & (ALIGNMENT-1LL);
    rself->poolSize &= ~(ALIGNMENT-1LL);
    DPRINTF(DEBUG_LVL_VERB,
        "SIMPLE Allocator @ 0x%llx/0x%llx got pool at address 0x%llx of size 0x%llx(%lld), offset from storage addr by %lld\n",
        (u64) rself, (u64) self,
        (u64) (rself->poolAddr), (u64) (rself->poolSize), (u64)(rself->poolSize), (u64) (rself->poolStorageOffset));

    simpleInit( (pool_t *)addrGlobalizeOnTG((void *)rself->poolAddr, PD), rself->poolSize);
}

void simpleStart(ocrAllocator_t *self, ocrPolicyDomain_t * PD ) {
    //self->pd = PD;    // this didn't work, so I've moved it to simpleBegin
    DPRINTF(DEBUG_LVL_VERB, "simpleStart : skip\n");
}

void simpleStop(ocrAllocator_t *self, ocrRunLevel_t newRl, u32 action) {
    switch(newRl) {
        case RL_STOP: {
            DPRINTF(DEBUG_LVL_VERB, "simpleStop : skip\n");
            self->memories[0]->fcts.stop(self->memories[0], newRl, action);
            break;
        }
        case RL_SHUTDOWN: {
            DPRINTF(DEBUG_LVL_VERB, "simpleFinish called (This is x86 only?)\n");

            ocrAllocatorSimple_t * rself = (ocrAllocatorSimple_t *) self;
            ASSERT(self->memoryCount == 1);

            RESULT_ASSERT(self-> /*rAnchorCE->base.*/ memories[0]->fcts.tag(
            rself->base.memories[0],
            rself->poolAddr - rself->poolStorageOffset,
            rself->poolAddr + rself->poolSize + rself->poolStorageSuffix,
            USER_FREE_TAG), ==, 0);

            self->memories[0]->fcts.stop(self->memories[0], newRl, action);
            break;
        }
        default:
            ASSERT("Unknown runlevel in stop function");
    }

}

void* simpleAllocate(
    ocrAllocator_t *self,   // Allocator to attempt block allocation
    u64 size,               // Size of desired block, in bytes
    u64 hints) {            // Allocator-dependent hints; SIMPLE supports reduced contention

    ocrAllocatorSimple_t * rself = (ocrAllocatorSimple_t *) self;
    void *ret = simpleMalloc((pool_t *)rself->poolAddr, size, self->pd);
    DPRINTF(DEBUG_LVL_VERB, "simpleAllocate called, ret %p from PoolAddr %p\n", ret, rself->poolAddr);
    return ret;
}
void simpleDeallocate(void* address) {
    DPRINTF(DEBUG_LVL_VERB, "simpleDeallocate called, %p\n", address);
    simpleFree(address);
}
void* simpleReallocate(
    ocrAllocator_t *self,   // Allocator to attempt block allocation
    void * pCurrBlkPayload, // Address of existing block.  (NOT necessarily allocated to this Allocator instance, nor even in an allocator of this type.)
    u64 size,               // Size of desired block, in bytes
    u64 hints) {            // Allocator-dependent hints; SIMPLE supports reduced contention
    ASSERT(0);
    return 0;
}

/******************************************************/
/* OCR ALLOCATOR SIMPLE FACTORY                         */
/******************************************************/

// Method to create the SIMPLE allocator
ocrAllocator_t * newAllocatorSimple(ocrAllocatorFactory_t * factory, ocrParamList_t *perInstance) {

    ocrAllocatorSimple_t *result = (ocrAllocatorSimple_t*)
        runtimeChunkAlloc(sizeof(ocrAllocatorSimple_t), PERSISTENT_CHUNK);
    DPRINTF(DEBUG_LVL_VERB, "newAllocator called. (This is x86 only ? ? ) alloc %p\n", result);
    ocrAllocator_t * base = (ocrAllocator_t *) result;
    SET_MODULE_STATE(SIMPLE-ALLOCATOR, base, NEW);
    factory->initialize(factory, base, perInstance);
    return (ocrAllocator_t *) result;
}
void initializeAllocatorSimple(ocrAllocatorFactory_t * factory, ocrAllocator_t * self, ocrParamList_t * perInstance) {
    DPRINTF(DEBUG_LVL_VERB, "Simple: initialize (This is x86 only ? ? ) called\n");
    initializeAllocatorOcr(factory, self, perInstance);

    ocrAllocatorSimple_t *derived = (ocrAllocatorSimple_t *)self;
    paramListAllocatorSimple_t *perInstanceReal = (paramListAllocatorSimple_t*)perInstance;

    derived->poolAddr          = 0ULL;
    derived->poolSize          = perInstanceReal->base.size;
    derived->poolStorageOffset = 0;
    derived->poolStorageSuffix = 0;
}

static void destructAllocatorFactorySimple(ocrAllocatorFactory_t * factory) {
    DPRINTF(DEBUG_LVL_VERB, "destructSimple called. (This is x86 only?) free %p\n", factory);
    runtimeChunkFree((u64)factory, NULL);
}

ocrAllocatorFactory_t * newAllocatorFactorySimple(ocrParamList_t *perType) {
    ocrAllocatorFactory_t* base = (ocrAllocatorFactory_t*)
        runtimeChunkAlloc(sizeof(ocrAllocatorFactorySimple_t), NONPERSISTENT_CHUNK);
    ASSERT(base);
    DPRINTF(DEBUG_LVL_VERB,
        "newAllocatorFactorySimple called, (This is x86 only?) alloc %p (Q: who free this?)\n", base);
    base->instantiate = &newAllocatorSimple;
    base->initialize = &initializeAllocatorSimple;
    base->destruct = &destructAllocatorFactorySimple;
    base->allocFcts.destruct = FUNC_ADDR(void (*)(ocrAllocator_t*), simpleDestruct);
    base->allocFcts.begin = FUNC_ADDR(void (*)(ocrAllocator_t*, ocrPolicyDomain_t*), simpleBegin);
    base->allocFcts.start = FUNC_ADDR(void (*)(ocrAllocator_t*, ocrPolicyDomain_t*), simpleStart);
    base->allocFcts.stop = FUNC_ADDR(void (*)(ocrAllocator_t*,ocrRunLevel_t,u32), simpleStop);
    base->allocFcts.allocate = FUNC_ADDR(void* (*)(ocrAllocator_t*, u64, u64), simpleAllocate);
    //base->allocFcts.free = FUNC_ADDR(void (*)(void*), simpleDeallocate);
    base->allocFcts.reallocate = FUNC_ADDR(void* (*)(ocrAllocator_t*, void*, u64), simpleReallocate);
    return base;
}

#endif /* ENABLE_ALLOCATOR_SIMPLE */
