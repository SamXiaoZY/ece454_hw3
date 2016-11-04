/*
 * This code implements a flexible segregated list memory
 * management architecture including mm_malloc, mm_free 
 * and mm_realloc.
 * 
 * A global list of free blocks is maintained with size 
 * of FREE_LIST_SIZE. Each element in the free list is a 
 * linked list containing free blocks with sizes ranging 
 * from 2**(i+2) to 2**(i+3)-1 of words. Each time a 
 * memory block is freed, the freed block will first be 
 * coalesced with its previous and next block in the heap 
 * and inserted at the head of the appropriate linked list 
 * in the free list based on its block size.
 * 
 * When mm_malloc is called with a size, the program will 
 * first search the free list to try to find a fitting 
 * free block, if no free block is found, more memory in 
 * the heap will be requested. mm_malloc uses find_fit
 * to search a fitting block, if a fitting free block is 
 * found, and the remaining size is enough to form another
 * free block (> MIN_BLOCK_SIZE), the block will be split
 * into two. The first part with request malloc size will
 * be return return to caller and the second part will be
 * added back to the free list.
 * 
 * When mm_realloc is called, a block pointer and a size 
 * will be passed in. If the size is the less than the 
 * original size of block pointer, no operation is needed
 * and the block pointer will be returned back. If the 
 * size is larger than the original block size, a new block
 * of memory with larger size will be allocated with malloc
 * and the old block pointer will be freed.
 * 
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
size_t get_flist_index(size_t asize);
void insert_free_block(void *bp);
void remove_free_block(void *bp);
void *find_block(size_t index, size_t asize);
void *handle_split_block(void *bp, size_t asize);
size_t get_extend_size(size_t asize);
int mm_check(void);
void print_flist(void);
int search_free_list_helper(void *bp);

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
#define DSIZE       (2 * WSIZE)            /* double word size (bytes) */
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

/* The global free list */
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
 **********************************************************/
void insert_free_block(void *bp)
{
    size_t asize = GET_SIZE_FROM_BLK(bp);
    size_t index = get_flist_index(asize);

    void *first_block = flist[index];

    if (first_block != NULL) {
        /* If list is not empty, insert in front of the first block */
        PUT_PREV_FBLOCK(first_block, bp);
        PUT_NEXT_FBLOCK(bp, first_block);
    } else {
        /* If list is empty, set next block to be NULL to identify the last block */
        PUT_NEXT_FBLOCK(bp, NULL);
    }
    /* Set the previous block to NULL to identify the first block */
    PUT_PREV_FBLOCK(bp, NULL);
    flist[index] = bp;
}

/**********************************************************
 * remove_free_block
 * Remove the free block from the free block list
 **********************************************************/
void remove_free_block(void *bp)
{
    size_t asize = GET_SIZE_FROM_BLK(bp);
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
 **********************************************************/
void *find_block(size_t index, size_t asize)
{
    void *bp = flist[index];
    size_t block_size;
    /* Loop through the entire bin to find a fit free block */
    while (bp != NULL) {
        block_size = GET_SIZE_FROM_BLK(bp);
        if (block_size >= asize) {
            bp = handle_split_block(bp, asize);
            break;
        }
        bp = GET_NEXT_FBLOCK(bp);
    }
    return bp;
}

/**********************************************************
 * handle_split_block
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
        return bp;
    }

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
 * get_extend_size
 * Given a desired block size, compute how much memory we
 * should be extend in the heap. 
 * If the the size is very small, extend only the size, 
 * otherwise extend CHUNKSIZE of memory.
 * If the last block in the heap is free, minus the extend 
 * size with the free block size to reduce fragmentation.
 **********************************************************/
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
    PUT(HDRP(bp), PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));
    coalesce(bp);
}

/**********************************************************
 * mm_malloc
 * Allocate a block of size bytes.
 * The type of search is determined by find_fit.
 * If no block satisfies the request, the heap is extended.
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

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = get_extend_size(asize);

    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    
    size_t block_size = GET_SIZE(HDRP(bp));

    /* If the extend block is very large, split the newly extended block */
    if (block_size >= asize) {
        bp = handle_split_block(bp, asize);
    }
    place(bp, asize);

    return bp;
}

/**********************************************************
 * mm_realloc
 * Implemented simply in terms of mm_malloc and mm_free
 *********************************************************/
void *mm_realloc(void *ptr, size_t size)
{
    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0){
        mm_free(ptr);
        return NULL;
    }
    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL)
        return (mm_malloc(size));

    size_t old_block_size = GET_SIZE(HDRP(ptr));
    size_t asize;

    /* Compute the adjusted block size */
    if (size <= DSIZE) {
        asize = 2 * DSIZE;
    } else {
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);
    }

    /* If the old block size is enough, return the original block pointer */
    if (old_block_size >= asize) {
        return ptr;
    }

    /* The old block size is not large enough */
    void *oldptr = ptr;
    void *newptr;

    newptr = mm_malloc((size_t)(size * 1.5));
    if (newptr == NULL)
        return NULL;

    /* Copy the old data. */
    old_block_size = GET_SIZE(HDRP(oldptr));
    if (size < old_block_size)
        old_block_size = size;
    memcpy(newptr, oldptr, old_block_size);
    mm_free(oldptr);

    return newptr;
}

/**********************************************************
 * 
 mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistant.
 
 1. check if the pointers in the free list point to valid free blocks
 	*if the block is marked as free
 	*if the address is in the heap
	*if the footer and header of the blocks matches with each other
2. check are there any contiguous free blocks that somehow escaped coalescing
	*check if the previous block and next block of any free blocks is also
	 in the free list. If the result is true, then print error message.
3. check if every free block is actually in the free list
	*search all the blocks in the heap. if it is a free block, check if it
	 exists in the free list.
 *********************************************************/
int mm_check(void)
{
	int i;
	void *heapstart = mem_heap_lo();
	void *heapend = mem_heap_hi();
	void *block = heapstart + 4*WSIZE; //the bp of the first block in the heap 
    
     
     for (i = 0; i < FREE_LIST_SIZE; i++){
	//starting from the first element in the free list
	void *currentbp = flist[i];  
	while (currentbp != NULL){
		void *currentheader = HDRP(currentbp);
               	int isfree = GET_ALLOC(currentheader);
               
	//check if every block in the free list marked as free
		if (isfree == 1){
			printf("The block %p is not free in the free list", currentbp);
		}
               
	//Check if the pointers in the free list points to address in heap
	//check if currentbp is within the range of heap
               if(currentbp < heapstart || currentbp > heapend){
		       printf("The free block %p is out of heap", currentbp);
               }
               
	//check if header=footer of the blocks
               if( *HDRP(currentbp) != *FTRP(currentbp)){
		       printf("The footer and header of %p does not match.", currentbp);
               }
		
               void *nextbp = GET_NEXT_FBLOCK(currentbp);
               currentbp = nextbp;
          }
          
     }
     
 
     //check are there any contiguous free blocks that somehow escaped coalescing
     
     for (i = 0; i < FREE_LIST_SIZE; i++){
	//starting from the first element in the free list
          void *currentbp = flist[i];
          while (currentbp!= NULL){
               void *nextbp = GET_NEXT_FBLOCK(currentbp);
		  
	//get the previous and next block and check if there are free.
	//if any of them is free, then coalescing is needed
               void *nextblock = NEXT_BLKP(currentbp);
               void *prevblock = PREV_BLKP(currentbp);
               int isprevfree = GET_ALLOC(HDRP(prevblock));
               int isnextfree = GET_ALLOC(HDRP(nextblock));
               if (isprevfree == 0){
                    printf("The block %p need coalescing in free list", prevblock);
               }
               if (isnextfree == 0){
                    printf("The block %p need coalescing in free list", nextblock);
               }
               currentbp = nextbp;
               
          }
    }
  

	//find all free blocks in the heap and check if there are in the free list
     while(block != NULL){
          int isfree = GET_ALLOC(HDRP(block));
          if (isfree == 0){
               int result = search_free_list_helper(block);
               if (result == 0){
                    printf("The free block %p is not in the free list", block);
               }     
          }
          void *nextblock = NEXT_BLKP(block);
          block = nextblock;
     }
  return 1;
}


/**********************************************************
 * search_free_list_helper
 * helper function, it will check if a block *bp is in the free list or not.
 * It will return 1 is their exist such block. otherwise, return 0.
 **********************************************************/

int search_free_list_helper(void *bp){
    int i;
    //get the index of the block
    i = get_flist_index(GET_SIZE_FROM_BLK(bp));
    void *currentbp = flist[i];
   
    //search the list to see if the block is there 
    while (currentbp != NULL){
          void *nextbp = GET_NEXT_FBLOCK(currentbp);
          if (bp == currentbp){
              return 1;
          }
          currentbp = nextbp;
          nextbp = GET_NEXT_FBLOCK(currentbp);
                         
    }
    return 0;
}


/**********************************************************
 * print_flist
 * Print out the entire free list for debugging purpose.
 **********************************************************/
void print_flist(void)
{
    int i;
    size_t size;
    void *bp;
    printf("Full Free List:\n");
    for (i = 0; i < FREE_LIST_SIZE; i++) {
        printf("%i -> ", i);
        bp = flist[i];
        while (bp != NULL) {
            size = GET_SIZE_FROM_BLK(bp);
            printf("%zu, ", size / WSIZE);
            bp = GET_NEXT_FBLOCK(bp);
        }
        printf("\n");
    }
}
