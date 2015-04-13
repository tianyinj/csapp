/*
 * mm.c
 *
 * Tianying Jiang
 * tianyinj@andrew.cmu.edu
 *
 * malloc lab, a dynamic memory allocator with malloc, free,realloc and calloc
 *
 * Free block data structure : Segregated free lists
 * with linked free blocks:
 *   nth -- 2^(n) ~ 2^(n+1) bytes
 *
 * Each free block has a OVERHEAD of 16 bytes:
 *
 * header:4, NEXT:4, PREV:4, footer:4
 *
 * Optimize the next and prev pointer by saving the left most 4 byte as
 * unsigned int rather than the 8 byte pointer.
 *
 * Within each segregated class, size of free blocks are in ascending order.
 * Inserting coalesed free block with ascending order as well.
 *
 * Placing a block is First fit/Best fit policy.
 *
 * Insert immediatly after free.
 * Coalesce immediatly after insert.
 *
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
#define CHECKHEAP(verbose) printf("%s\n",__func__); mm_checkheap(verbose):
# define dbg_printf(...) printf(__VA_ARGS__)
# define dbg_assert(...) assert(__VA_ARGS__)
# define dbg(...)
#else
# define dbg_printf(...)
# define dbg_assert(...)
# define dbg(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  511  /* Extend heap by this amount (bytes) */
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define OVERHEAD    8

#define class 27
#define l_size 16


/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc))


/* Read and write a word at address p */
#define GET(p)       (*((unsigned int *)(p)))
#define PUT(p, val)  (*(unsigned int *)(p) = (val))

//#define PUT_PTR(p1,p2) (p1=p2)

/*Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp,compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Given block ptr bp and pointer to another block, trunc the pointer
 * to 4-Byte unsigned int and PUT */
#define MAKE_NEXT(p,val)  PUT(p,p2w(val))
#define MAKE_PREV(p,val)  PUT(p+WSIZE,p2w(val))

/* Given block ptr bp, compute pointer of NEXT and PREV free block*/
#define NEXT(bp) (w2p(GET(bp)))
#define PREV(bp) (w2p(GET(bp+WSIZE)))

#define heap_start 0x800000000   //heap starts at here by observation

char *heap_listp=0;

/* Cast an unsigned int into a pointer*/
static inline void* w2p(unsigned int w){
    if (w==0) return NULL;
    return (void *)((unsigned long)(w)+heap_start);
}

/* Cast a pointer into unsigned int*/
static inline unsigned int p2w(void *p){
    if (p==NULL) return 0;
    return (unsigned int)((uint64_t)(p));
}

/* Return the right most bit*/
static inline int find_bound(size_t n){
    for (int i=0; i<class; i++){
        n=n>>1;
        if (n==0) {
            return i;
        }
    }
    return -1;
}

/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}
/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}

void *extend_heap(size_t size);
void insert(size_t size, void *bp);
void *find_fit(size_t asize);
void *place(size_t size,void *bp);
void coalesce(void *bp);
void unlink_blk(void *ptr);

char *seg[class];


/*
 * Initialize: return -1 on error, 0 on success.
 *
 * Initialize all entries in the seg-list as null;
 */
int mm_init(void) {

    heap_listp=0;
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
         return -1;

    PUT(heap_listp, 0); /* Alignment padding */
    PUT(heap_listp+WSIZE, PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp+DSIZE, PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp+(3*WSIZE), PACK(0, 1)); /* Epilogue header */
    heap_listp += DSIZE;

    for (int i=0; i<class; i++){
        seg[i]=NULL;
    }
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;

    return 0;
}

/* find fitted free block for the given adjusted size
 *
 * Find the class of size, and start to search towards higher class
 *   Within each class, linear search each linked block, find the
 *   first block with size larger than its own
 *
 * Since linked block are in ascending order, this is first fit as well
 * as best fit
 */
void *find_fit(size_t asize){
    int bound;
    void *bp;

    bound = find_bound(asize);
    //assert(bound>=4);
    assert(bound<class);


    for (int i=bound; i<class; i++){
        for (bp=seg[i]; bp!=NULL; bp=NEXT(bp)){
            if ((!GET_ALLOC(HDRP(bp)))&&(GET_SIZE(HDRP(bp))>=asize)){
                return bp;

            }

        }
    }

    return NULL;
}

/* Insert the given pointer in to its size class
 *  and make sure within the class blocks are ordered
 */
void insert(size_t size, void *bp){
    int bound=find_bound(size);
    void* next;
    void* ptr=seg[bound];

    if (ptr==NULL){
        seg[bound]=bp;
        MAKE_NEXT(bp,NULL);
        MAKE_PREV(bp,NULL);
        return;
    }

    if (GET_SIZE(HDRP(ptr))>=size){
        MAKE_PREV(ptr,bp);
        MAKE_NEXT(bp,ptr);
        MAKE_PREV(bp,NULL);
        seg[bound]=bp;
        return;
    }

    while(ptr!=NULL){
        next=NEXT(ptr);

        if (next==NULL){
            MAKE_NEXT(ptr,bp);
            MAKE_PREV(bp,ptr);
            MAKE_NEXT(bp,NULL);
            return;
        }
        if (GET_SIZE(HDRP(next))>=size){
            MAKE_NEXT(bp,next);
            MAKE_PREV(bp,ptr);
            MAKE_NEXT(ptr,bp);
            MAKE_PREV(next,bp);
            return;
        }

        ptr=next;
    }

    printf("Shouldn't get here\n");
    return;
}

/* Allocate a memory chunk at bp. If size of bp is
 *  larger than required size and reminder > l_size, split the
 *  block and insert the splited block in to free class of its size
 */
void *place(size_t size,void *bp){
    //int bound= find_bound(size);
    size_t b_size = GET_SIZE(HDRP(bp));
    size_t split_size = b_size-size;

    unlink_blk(bp);

    if (split_size < l_size){

        PUT(HDRP(bp),PACK(b_size,1));
        PUT(FTRP(bp),PACK(b_size,1));
    }

    else{

        PUT(HDRP(bp),PACK(size,1));
        PUT(FTRP(bp),PACK(size,1));

        assert(NEXT_BLKP(bp) != NULL);

        PUT(HDRP(NEXT_BLKP(bp)),PACK(split_size,0));
        PUT(FTRP(NEXT_BLKP(bp)),PACK(split_size,0));

        insert(split_size,NEXT_BLKP(bp));
        coalesce(NEXT_BLKP(bp));

    }

    return bp;

}


/* unlink the given ptr with its next and prev
 *   if there is any!
 */
void unlink_blk(void *ptr){
    int bound=find_bound(GET_SIZE(HDRP(ptr)));
    void* next=NEXT(ptr);
    void* prev=PREV(ptr);

    if ((prev==NULL)&&(next==NULL)){
        seg[bound]=NULL;
    }

    else if((prev!=NULL)&&(next==NULL)){
        MAKE_NEXT(prev,NULL);
    }

    else if((prev==NULL)&&(next!=NULL)){
        MAKE_PREV(next,NULL);
        seg[bound]=next;
    }

    else{
        assert(prev!=NULL);
        assert(next!=NULL);
        // printf("%p\n",next);
        MAKE_NEXT(prev,next);
        MAKE_PREV(next,prev);
    }

    MAKE_NEXT(ptr,NULL);
    MAKE_PREV(ptr,NULL);

    return;
}

/* If the immediate adjacent neighbor of bp are free,
 *   (since bp is free we know for sure), coalesce them
 *   into a larger chunk and insert into proper size class
 */
void coalesce(void *bp){
    size_t total_size=GET_SIZE(HDRP(bp));
    char *prev;
    char *next;
    int nalloc;
    int palloc;

    prev=PREV_BLKP(bp);
    next=NEXT_BLKP(bp);

    palloc=GET_ALLOC(HDRP(prev));
    nalloc=GET_ALLOC(HDRP(next));

    if (palloc&&nalloc){
        return;
    }

    unlink_blk(bp);

    if (palloc&&!nalloc){
        unlink_blk(next);
        total_size+=GET_SIZE(HDRP(next));
        PUT(HDRP(bp),PACK(total_size,0));
        PUT(FTRP(bp),PACK(total_size,0));
    }
    else if (!palloc&&nalloc){

        unlink_blk(prev);
        total_size+=GET_SIZE(HDRP(prev));
        PUT(HDRP(prev),PACK(total_size,0));
        PUT(FTRP(prev),PACK(total_size,0));
        bp=prev;
    }

    else if (!palloc&& !nalloc){
        unlink_blk(next);
        unlink_blk(prev);
        total_size+=GET_SIZE(HDRP(prev));
        total_size+=GET_SIZE(HDRP(next));
        PUT(HDRP(prev),PACK(total_size,0));
        PUT(FTRP(prev),PACK(total_size,0));
        bp=prev;
    }

    insert(total_size,bp);
}

/*
 * Extend_heap:
 *   Allocate fresh chunk of memory, insert into proper
 *   size class, and return the pointer to it
 */
void *extend_heap(size_t words){
    size_t asize;
    unsigned int *new=0;

    asize=((words % 2) ? (words+1) * WSIZE : words) * WSIZE;

    if ((new=mem_sbrk(asize))==(void*)-1){
        return NULL;
    }

    PUT(HDRP(new),PACK(asize,0));
    PUT(FTRP(new),PACK(asize,0));
    PUT(HDRP(NEXT_BLKP(new)), PACK(0, 1));

    insert(asize,new);
    //coalesce(new);
    return new;
}


/*
 * malloc
 *
 * Ignore memory request with bogus size.
 * Align the required size and add OVERHEAD to real size.
 *
 * If a fit can be find in the exisiting free blks, go ahead and
 *  allocate and return the pointer to it.
 *
 * Else, expend the heap, allocate, and return pointer.
 */
void *malloc (size_t size) {

    size_t asize;   /* Adjusted block size */
    size_t extendsize;  /* Amount to extend heap if no fit */
    void *bp=NULL;

    /* Ignore spurious requests */
    if (size <= 0)
        return NULL;


    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE){
            asize = l_size;
    }
    else{
        asize = ALIGN(size)+OVERHEAD;
    }

    //printf("[%zu,%zu]\n",size,asize);
    if ((bp=find_fit(asize))!= NULL){
        return place(asize,bp);
    }

    else {
        extendsize = MAX(asize,CHUNKSIZE);;
        if ((bp=extend_heap(extendsize/WSIZE))==NULL){
            return NULL;
        }
        assert(bp != NULL);
        return place(asize,bp);
    }
}



/*
 * free
 *
 * Dis-allocate previously allocated memory at bp.
 *  Insert the newly made free blk into proper size class
 *  Coalesce with adjacent blks.
 */
void free (void *bp) {
    //printf("entered free\n");

    //Must be valid address
    if (bp==NULL){
        return;
    }

    //Must be previously allocated
    if (GET_ALLOC(HDRP(bp))!=1){
        printf("INVALID FREE POINTER\n");
        return;
    }

    //Must be in the range of heap addr
    if (!in_heap(bp)){
        printf("BOGUS!\n");
        return;
    }
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp),PACK(size, 0));
    PUT(FTRP(bp),PACK(size, 0));

    insert(size,bp);
    coalesce(bp);

    return;
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size) {
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        free(oldptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(oldptr == NULL) {
        return malloc(size);
    }

    newptr = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = *SIZE_PTR(oldptr);
    if(size < oldsize) oldsize = size;
    memcpy(newptr, oldptr, oldsize);

    /* Free the old block. */
    free(oldptr);

    return newptr;
}

/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc (size_t nmemb, size_t size)
{
    size_t bytes = nmemb * size;
    void *newptr;

    newptr = malloc(bytes);
    memset(newptr, 0, bytes);

    return newptr;
}


void printblock(void *bp)
{
    size_t hsize, fsize;
    int halloc, falloc;

    mm_checkheap(0);
    hsize = GET_SIZE(HDRP(bp));
    halloc = GET_ALLOC(HDRP(bp));
    fsize = GET_SIZE(FTRP(bp));
    falloc = GET_ALLOC(FTRP(bp));

    printf("%p: header:[%d,%zu], footer:[%d,%zu]\n",bp,halloc,hsize,
             falloc,fsize);
}


/*
 * mm_checkheap
 */
void mm_checkheap(int verbose) {

    char *bp = heap_listp;

    if (verbose){
        printf("Heap (%p):\n", heap_listp);
        printf(heap_listp);
    }

    if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp))){
        printf("Wrong prologue header\n");
    }

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (verbose){
            printblock(bp);
        }
        if (!aligned(bp)){
            printf("[%p] NOT ALIGNED\n", bp);
        }

        if (GET(HDRP(bp)) != GET(FTRP(bp))){
            printf("HEADER AND FOOTER NOT MATCH");
        }
    }

    if ((GET_SIZE(HDRP(bp))) || !(GET_ALLOC(HDRP(bp)))){
        printf("Wrong prologue footer\n");
    }

    for (void* bp=heap_listp;GET_SIZE(HDRP(bp))>0;bp=NEXT_BLKP(bp)){
        if ((bp<mem_heap_lo())||(bp>mem_heap_hi())){
            printf("POINTER OUT OF BOUND\n");
        }
        if (GET_ALLOC(HDRP(bp))){
            if ((GET_ALLOC(HDRP(bp))!=GET_ALLOC(FTRP(bp)))||
                (GET_SIZE(HDRP(bp)))!=(GET_SIZE(FTRP(bp)))){
                printf("inconsitent header and footer\n");
                printf("size1:%u\n",GET_SIZE(HDRP(bp)));
                printf("size2:%u\n",GET_SIZE(FTRP(bp)));
                printf("alloc1:%u\n",GET_ALLOC(HDRP(bp)));
                printf("alloc2:%u\n",GET_ALLOC(FTRP(bp)));
            }
        }
    }


    for (int i=0; i<class; i++){
        for (void* bp=seg[i];bp!=NULL;bp=NEXT(bp)){

            if ((GET_ALLOC(HDRP(bp)) != 0)){
                printf("ALLOC INDEX ON FREE BLOCK\n");
            }

            if ((GET_SIZE(HDRP(bp)))>>i == 0 ||
                (GET_SIZE(HDRP(bp)))>>(i+1) != 0){
                printf("WRONG CLASS\n");
                printf("%u\n",GET_SIZE(HDRP(bp)));
            }

            if (NEXT(bp)!=NULL &&NEXT_BLKP(bp)==NEXT(bp)){
                printf("CONSECUTIVE FREE BLOCKS\n");
            }

            if (NEXT(bp)!=NULL){
                if (PREV(NEXT(bp))!=bp){
                    printf("TINGLED FREE LIST\n");
                }
            }
        }
    }
}

