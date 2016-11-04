/*
 * This implementation replicates the implicit list implementation
 * provided in the textbook
 * "Computer Systems - A Programmer's Perspective"
 * Blocks are never coalesced or reused.
 * Realloc is implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * Function Prototypes
 ********************************************************/
void *find_block(size_t index, size_t asize);
size_t get_flist_index(size_t asize);
void insert_free_block(void *bp);
void remove_free_block(void *bp);
void *handle_split_block(void *bp, size_t asize);
void print_flist(void);
size_t get_extend_size(size_t asize);

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "First Blood",
    /* First member's full name */
    "Yang Chen",
    /* First member's email address */
    "robbie.chen@mail.utoronto.ca",
    /* Second member's full name (leave blank if none) */
    "Zhongyang Xiao",
    /* Second member's email address (leave blank if none) */
    "sam.xiao@mail.utoronto.ca"
};

/*************************************************************************
 * Basic Constants and Macros
 * You are not required to use these macros but may find them helpful.
*************************************************************************/
#define WSIZE       sizeof(void *)            /* word size (bytes) */
#define DSIZE       (2 * WSIZE)            /* doubleword size (bytes) */
#define CHUNKSIZE   (18 * WSIZE)      /* initial heap size (bytes) */

#define MAX(x,y) ((x) > (y)?(x) :(y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)          (*(uintptr_t *)(p))
#define PUT(p,val)      (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)     (GET(p) & ~(DSIZE - 1))
#define GET_ALLOC(p)    (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)        ((char *)(bp) - WSIZE)
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Given block ptr bp, compute the size of the block */
#define GET_SIZE_FROM_BLK(bp)   (GET_SIZE(HDRP(bp)))

/* The minimum number of words for a memory block is 4: 
 * header(1 word) + payload(2 words) + footer(1 word) = 4 words */
#define MIN_BLOCK_SIZE (4 * WSIZE)

/* Since the minimum payload of a free block is 2 words, use the first word
 * to store the pointer to the previous free block in a linked list, and 
 * use the second word to store the pointer to the next free block */
/* Given a free block pointer bp, return a pointer to the previous or next free block in a free list bin */
#define GET_PREV_FBLOCK(bp) ((void *) GET(bp))
#define GET_NEXT_FBLOCK(bp) ((void *) GET((char *)(bp) + WSIZE))

/* Given a free block pointer bp and a pointer to another free block, 
 * store as the previous or next free block in a free list bin */
#define PUT_PREV_FBLOCK(bp, ptr) (PUT(bp, (uintptr_t) ptr))
#define PUT_NEXT_FBLOCK(bp, ptr) (PUT((char *)(bp) + WSIZE, (uintptr_t) ptr))

#define FREE_LIST_SIZE 20

void *flist[FREE_LIST_SIZE];

/**********************************************************
 * get_flist_index
 * Compute the index of the free list for a given size,
 * each element of the free block is a linked list of free blocks, 
 * with block sizes ranging from 2**(i+2) to 2**(i+3)-1 of words.
 * Assuming block sizes includes header and footer size and are aligned.
 * 
 * Eg. block of 4 ~ 7 words in the 1st linked list
       block of 8 ~ 15 words in the 2nd linked list
       block of 16 ~ 31 words in the 3rd linked list
 * 
 * For block sizes greater than 2**(FREE_LIST_SIZE+2), place them in the
 * last linked list.
 **********************************************************/
size_t get_flist_index(size_t asize)
{
    size_t index;
    /* max_size is the max block size for each segregated linked list */
    size_t max_size = 8;
    for (index = 0; index < FREE_LIST_SIZE; index++) {
        if (asize < max_size) {
            break;
        }
        /* Multiply the max_size by 2 */
        max_size <<= 1;
    }
    /* Cap the index by size of free list */
    index = (index < FREE_LIST_SIZE) ? index : FREE_LIST_SIZE - 1;
    return index;
}

/**********************************************************
 * insert_free_block
 * Insert the free block to the start of the designated linked list 
 * in free block list
 * TODO: Maybe use binary search tree to replace linked list?
 **********************************************************/
void insert_free_block(void *bp)
{
    size_t asize = GET_SIZE_FROM_BLK(bp);
    size_t index = get_flist_index(asize);

    //printf("Insert free block size = %zu at index = %zu\n", asize / WSIZE, index);
    //fflush(stdout);

    void *first_block = flist[index];

    if (first_block != NULL) {
        /* If list is not empty, insert in front of the first block */
        PUT_PREV_FBLOCK(first_block, bp);
        PUT_NEXT_FBLOCK(bp, first_block);
        //assert(GET_NEXT_FBLOCK(bp) != NULL);
    } else {
        /* If list is empty, set next block to be NULL to identify the last block */
        PUT_NEXT_FBLOCK(bp, NULL);
        //assert(GET_NEXT_FBLOCK(bp) == NULL);
    }
    /* Set the previous block to NULL to identify the first block */
    PUT_PREV_FBLOCK(bp, NULL);
    flist[index] = bp;

    //assert(GET_PREV_FBLOCK(bp) == NULL);
}

/**********************************************************
 * remove_free_block
 * Remove the free block from the free block list
 * TODO: Maybe use binary search tree to replace linked list?
 **********************************************************/
void remove_free_block(void *bp)
{
    size_t asize = GET_SIZE_FROM_BLK(bp);
    //printf("Remove free block size = %zu\n", asize / WSIZE);
    //fflush(stdout);

    void *prev = GET_PREV_FBLOCK(bp);
    void *next = GET_NEXT_FBLOCK(bp);

    if (prev) {
        /* bp is not the first block */
        PUT_NEXT_FBLOCK(prev, next);
    } else {
        /* bp is the first block */
        size_t index = get_flist_index(asize);
        flist[index] = next;
    }
    if (next) {
        /* bp is not the last block */
        PUT_PREV_FBLOCK(next, prev);
    }
}

/**********************************************************
 * find_block
 * Given the bin index of free list and desired block size, 
 * traverse the linked list searching for a block to fit asize.

 * After finding a fit block, split the block if the remaining block size
 * is enough for another allocation, and remove the free block from free list.
 * TODO: Maybe use binary search tree to replace linked list?
 **********************************************************/
void *find_block(size_t index, size_t asize)
{
    void *bp = flist[index];
    size_t block_size;
    /* Loop through the entire bin to find a fit free block */
    while (bp != NULL) {
        block_size = GET_SIZE_FROM_BLK(bp);
        if (block_size >= asize) {
            //printf("Found fit free block size = %zu at index = %zu\n", block_size / WSIZE, index);
            bp = handle_split_block(bp, asize);
            break;
        }
        bp = GET_NEXT_FBLOCK(bp);
    }
    return bp;
}

/**********************************************************
 * split_block
 * Given a free block pointer and desired size, split the block into 2,
 * and place the 2 new free blocks into the correct bin in the free list
 **********************************************************/
void *handle_split_block(void *bp, size_t asize)
{
    size_t block_size = GET_SIZE_FROM_BLK(bp);
    //assert(block_size >= asize);
    size_t sub_size = block_size - asize;

    /* First, remove block from free list first */
    remove_free_block(bp);

    /* Do not split if block size is not large enough */
    if (block_size < asize + MIN_BLOCK_SIZE) {
        //printf("No need to split\n");
        return bp;
    }

    //printf("Splitting %zu into %zu and %zu\n", block_size / WSIZE, asize / WSIZE, sub_size / WSIZE);
    //fflush(stdout);

    /* Change size in header and footer of bp */
    /* Note that the order cannot be changed here, since all subsequence operations depends on the header */
    PUT(HDRP(bp), asize);
    PUT(FTRP(bp), asize);

    /* Change size in header and footer of sub block */
    void *sub_block = NEXT_BLKP(bp);
    PUT(HDRP(sub_block), sub_size);
    PUT(FTRP(sub_block), sub_size);

    /* Insert the sub block back to the free list */
    insert_free_block(sub_block);

    return bp;
}

/**********************************************************
 * mm_init
 * Initialize the heap, including "allocation" of the
 * prologue and epilogue
 **********************************************************/
int mm_init(void)
{
    void* heap_listp = NULL;
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) return -1;

    PUT(heap_listp, 0);                         // alignment padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));   // prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));   // prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));    // epilogue header
    heap_listp += DSIZE;

    /* Initialize the free block list to be NULL */
    int i;
    for (i = 0; i < FREE_LIST_SIZE; i ++) {
        flist[i] = NULL;
    }

    return 0;
}

/**********************************************************
 * coalesce
 * Covers the 4 cases discussed in the text:
 * - both neighbours are allocated
 * - the next block is available for coalescing
 * - the previous block is available for coalescing
 * - both neighbours are available for coalescing

 * Note that after coalescing, the coalesced blocks will be removed from free list,
 * and the new block will be added to free list
 **********************************************************/
void *coalesce(void *bp)
{
    void *new_block;
    void *prev = (void *) PREV_BLKP(bp);
    void *next = (void *) NEXT_BLKP(bp);
    size_t prev_alloc = GET_ALLOC(FTRP(prev));
    size_t next_alloc = GET_ALLOC(HDRP(next));
    size_t size = GET_SIZE(HDRP(bp));

    //printf("Coalescing\n");

    if (prev_alloc && next_alloc) {       /* Case 1 */
        new_block = bp;
    }

    else if (prev_alloc && !next_alloc) { /* Case 2 */
        /* Need to remove from free list because it is been coalesced */
        remove_free_block(next);
        size += GET_SIZE(HDRP(next));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        new_block = bp;
    }

    else if (!prev_alloc && next_alloc) { /* Case 3 */
        /* Need to remove prev from free list because the size is changed */
        remove_free_block(prev);
        size += GET_SIZE(HDRP(prev));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        new_block = PREV_BLKP(bp);
    }

    else {            /* Case 4 */
        remove_free_block(prev);
        remove_free_block(next);
        size += GET_SIZE(HDRP(prev)) + GET_SIZE(FTRP(next))  ;
        PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size,0));
        new_block = PREV_BLKP(bp);
    }
    insert_free_block(new_block);
    return new_block;
}

/**********************************************************
 * extend_heap
 * Extend the heap by "words" words, maintaining alignment
 * requirements of course. Free the former epilogue block
 * and reallocate its new header
 **********************************************************/
void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignments */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ( (bp = mem_sbrk(size)) == (void *)-1 )
        return NULL;

    //printf("Extend heap size = %zu\n", size / WSIZE);

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));                // free block header
    PUT(FTRP(bp), PACK(size, 0));                // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));        // new epilogue header

    /* Coalesce if the previous block was free */
    bp = coalesce(bp);

    return bp;
}


/**********************************************************
 * find_fit
 * Starting from the minimum bin index of free list that fits asize, 
 * traverse the list of remaining bins searching for a block to fit asize.
 * Return NULL if no free blocks can handle that size
 * Assumed that asize is aligned
 **********************************************************/
void *find_fit(size_t asize)
{
    //fflush(stdout);
    void *bp = NULL;
    size_t index;

    /* Loop through all bins with size larger than asize */
    for (index = get_flist_index(asize); index < FREE_LIST_SIZE; index++) {
        /* Try to find a fit free block in a bin */
        bp = find_block(index, asize);
        if (bp != NULL) {
            break;
        }
    }

    return bp;
}

/**********************************************************
 * place
 * Mark the block as allocated
 **********************************************************/
void place(void* bp, size_t asize)
{
  /* Get the current block size */
  size_t bsize = GET_SIZE(HDRP(bp));

  PUT(HDRP(bp), PACK(bsize, 1));
  PUT(FTRP(bp), PACK(bsize, 1));
}

/**********************************************************
 * mm_free
 * Free the block and coalesce with neighbouring blocks.
 * Add the freed block to free list
 **********************************************************/
void mm_free(void *bp)
{
    if(bp == NULL){
      return;
    }
    /* Clear allocated bit in header and footer, and coalesce freed block */
    size_t size = GET_SIZE(HDRP(bp));

    //printf("Free size = %zu\n", size / WSIZE);
    //fflush(stdout);

    PUT(HDRP(bp), PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));
    coalesce(bp);

    //print_flist();
    //printf("********************\n");
    //fflush(stdout);
}


/**********************************************************
 * mm_malloc
 * Allocate a block of size bytes.
 * The type of search is determined by find_fit
 * The decision of splitting the block, or not is determined
 *   in place(..)
 * If no block satisfies the request, the heap is extended
 * TODO: When asize > CHUNKSIZE, if the last block is free, 
         use that free block as well to reduce external fragmentation.
         eg. extend_heap(asize - last free block size)
   TODO: Remember to split the extended heap before place
 **********************************************************/
void *mm_malloc(size_t size)
{
    size_t asize; /* adjusted block size */
    size_t extendsize; /* amount to extend heap if no fit */
    char * bp;

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);

    //printf("Malloc size = %zu\n", asize / WSIZE);
    //fflush(stdout);
    //print_flist();

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);

        //printf("********************\n");
        //fflush(stdout);
        //print_flist();

        return bp;
    }

    /* No fit found. Get more memory and place the block */
    //printf("No free block, extending heap\n");
    //fflush(stdout);
    
    extendsize = get_extend_size(asize);

    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    
    size_t block_size = GET_SIZE(HDRP(bp));

    /* TODO: tune the number? */
    if (block_size >= asize + 0) {
        bp = handle_split_block(bp, asize);
    }
    
    place(bp, asize);

    //printf("bp size = %zu\n", block_size / WSIZE);
    //printf("********************\n");
    //fflush(stdout);
    //print_flist();

    return bp;
}


size_t get_extend_size(size_t asize)
{
    size_t extendsize;

    if (asize * 2 < CHUNKSIZE) {
        /* If asize is too small, don't allocate that much memory */
        extendsize = asize;
    } else {
        extendsize = MAX(CHUNKSIZE, asize);
    }

    /* If last block is free, only extend (extendsize - free_block_size) to reduce external fragmentation*/
    void *last_bp = PREV_BLKP(mem_heap_hi() + 1);
    if (!GET_ALLOC(HDRP(last_bp))) {
        extendsize = asize - GET_SIZE_FROM_BLK(last_bp);
    }
    return extendsize;
}

/**********************************************************
 * mm_realloc
 * Implemented simply in terms of mm_malloc and mm_free
 *********************************************************/
void *mm_realloc(void *ptr, size_t size)
{
    //printf("Realloc\n");
    //fflush(stdout);
    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0){
      mm_free(ptr);
      return NULL;
    }
    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL)
      return (mm_malloc(size));

    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;

    /* Copy the old data. */
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistant.
 *********************************************************/
int mm_check(void)
{
    //check are there any contiguous free blocks that somehow escaped coalescing
     
     for (i = 0; i < FREE_LIST_SIZE; i++){
		 //starting from the first element in the free list
          void *currentbp = flist[i];
          while (currentbp!= NULL){
               void *nextbp = GET_NEXT_FBLOCK(currentbp);
               void *nextblock = NEXT_BLKP(currentbp);
               void *prevblock = PREV_BLKP(currentbp);
               int isprevfree = GET_ALLOC(HDRP(prevblock));
               int isnextfree = GET_ALLOC(HDRP(nextblock));
               if (isprevblock ==0){
                    printf("The block %x need coalescing in free list", prevblock);
               }
               if (isnextfree == 0){
                    printf("The block %x need coalescing in free list", nextblock);
               }
               currentbp = nextbp;
               
          }
    }
  return 1;
}

void print_flist(void)
{
    int i;
    size_t size;
    void *bp;
    //printf("Full Free List:\n");
    for (i = 0; i < FREE_LIST_SIZE; i++) {
        //printf("%i -> ", i);
        bp = flist[i];
        while (bp != NULL) {
            size = GET_SIZE_FROM_BLK(bp);
            //printf("%zu, ", size / WSIZE);
            bp = GET_NEXT_FBLOCK(bp);
        }
        //printf("\n");
    }
    //fflush(stdout);
}
