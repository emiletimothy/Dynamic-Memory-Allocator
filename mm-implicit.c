/*
 * mm-implicit.c
 */

#include <stdint.h>
#include <string.h>
#include "memlib.h"
#include "mm.h"

/** The required alignment of heap payloads */
const size_t ALIGNMENT = 2 * sizeof(size_t);

/** The layout of each block allocated on the heap */
typedef struct {
    /** The size of the block and whether it is allocated (stored in the low bit) */
    size_t header;
    /* The payload size is unknown so we declare it a zero-length array to get a pointer
     * to the payload. */
    uint8_t payload[];
} block_t;

/** The first and last blocks on the heap */
static block_t *mm_heap_first = NULL;
static block_t *mm_heap_last = NULL;        

/** Rounds up `size` to the nearest multiple of `n` */
static size_t round_up(size_t size, size_t n) {
    return (size + (n - 1)) / n * n;
}

/** Set's a block's header with the given size and allocation state */
static void set_header(block_t *block, size_t size, bool is_allocated) {
    block->header = size | is_allocated;
}

/** Extracts a block's size from its header */
static size_t get_size(block_t *block) {
    return block->header & ~1;
}

/** Extracts a block's allocation state from its header */
static bool is_allocated(block_t *block) {
    return block->header & 1;
}

/** Find the first free block with at least @param size. If no block is large enough,
 * returns NULL. */
static block_t *find_fit(size_t size) {
    // Traverse the blocks in the heap using the implicit list
    for (block_t *curr = mm_heap_first; mm_heap_last != NULL && curr <= mm_heap_last;
         curr = (void *) curr + get_size(curr)) {
        /* Check if the free block is larger than needed. If yes, split the block and
         * change the header to true. Change the second header to the location of the
         * split and the
         * size difference between the block that we have and what the user asked for. */
        size_t block_size = get_size(curr);
        if (!is_allocated(curr) && block_size >= size) {
            set_header(curr, size, true);
            if (block_size - size >= 16) {
                set_header((void *) curr + size, block_size - size, false);
                if (curr == mm_heap_last) {
                    mm_heap_last = (void *) curr + size;
                }
            }
            return curr;
        }
    }
    return NULL;
}

/** Gets the header corresponding to a given payload pointer */
static block_t *block_from_payload(void *ptr) {
    return ptr - offsetof(block_t, payload);
}

/* mm_init - Initializes the allocator state */
bool mm_init(void) {
    // We want the first payload to start at ALIGNMENT bytes from the start of the heap
    void *padding = mem_sbrk(ALIGNMENT - sizeof(block_t));
    if (padding == (void *) -1) {
        return false;
    }
    // Initialize the heap with no blocks
    mm_heap_first = NULL;
    mm_heap_last = NULL;
    return true;
}

/** Delayed coalescing helper function. No arguments. Loops from mm_heap_first to the end
 * until a free block is found. Searches blocks next to the free block to the end. Adds up
 * the total size of the blocks and assigns the appropriate header */
void coalesce(void) {
    block_t *curr = mm_heap_first;
    while ((mm_heap_last != NULL) && (curr < mm_heap_last)) {
        if (!is_allocated(curr)) {
            // A free block is found. Searches the blocks ahead of it to find the total
            // free size.
            size_t total_block_size = get_size(curr);
            block_t *next_block = (void *) curr + total_block_size;
            while ((next_block <= mm_heap_last) && (!is_allocated(next_block))) {
                total_block_size += get_size(next_block);
                next_block = (void *) next_block + get_size(next_block);
                // If the last free block is the end of the heap, add the size and break.
                if (next_block == mm_heap_last) {
                    if (!is_allocated(next_block)) {
                        total_block_size += get_size(next_block);
                    }
                    break;
                }
            } // Set the total header and then iterate to the next free block.
            set_header(curr, total_block_size, false);
        }
        curr = (void *) curr + get_size(curr);
    }
}

/* mm_malloc - Allocates a block with the given size */
void *mm_malloc(size_t size) {
    coalesce();
    // The block must have enough space for a header and be 16-byte aligned
    size = round_up(sizeof(block_t) + size, ALIGNMENT);
    // If there is a large enough free block, use it
    block_t *block = find_fit(size);
    if (block != NULL) {
        return block->payload;
    }
    // Otherwise, a new block needs to be allocated at the end of the heap
    block = mem_sbrk(size);
    if (block == (void *) -1) {
        return NULL;
    }
    // Update mm_heap_first and mm_heap_last since we extended the heap
    if (mm_heap_first == NULL) {
        mm_heap_first = block;
    }
    mm_heap_last = block;
    // Initialize the block with the allocated size
    set_header(block, size, true);
    return block->payload;
}

/* mm_free - Releases a block to be reused for future allocations. */
void mm_free(void *ptr) {
    // mm_free(NULL) does nothing
    if (ptr == NULL) {
        return;
    }
    // Mark the block as unallocated
    block_t *block = block_from_payload(ptr);
    set_header(block, get_size(block), false);
}

/* mm_realloc - Change block size by mm_mallocing a new block - copy the data - mm_free
 * the old block. */
void *mm_realloc(void *old_ptr, size_t size) {
    // If old_ptr is null, just mm_malloc the size and return its pointer
    if (old_ptr == NULL) {
        return (mm_malloc(size));
    }
    // If size is 0, just free the old pointer an d return NULL
    else if (size == 0) {
        mm_free(old_ptr);
        return (NULL);
    }
    else {
        void *new_ptr = mm_malloc(size); // Malloc a new block.
        size_t old_size = get_size(block_from_payload(old_ptr));
        // Copies the data to the smaller structure and frees the old pointer.
        size_t smaller_size = (old_size > size) ? size : old_size;
        memcpy(new_ptr, old_ptr, smaller_size);
        mm_free(old_ptr);
        return (new_ptr);
    }
}

/* mm_calloc - Allocate the block and set it to zero. */
void *mm_calloc(size_t nmemb, size_t size) {
    // mm_malloc's a block of size nmemb * size and uses memset to set it to 0.
    void *ptr = mm_malloc(nmemb * size);
    ptr = memset(ptr, 0, nmemb * size);
    return (ptr);
}

// This function is not used here.
void mm_checkheap(void) {};
