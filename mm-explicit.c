/*
 * mm-explicit.c - Design choices: First-Fit, LIFO
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/** The required alignment of heap payloads */
const size_t ALIGNMENT = 2 * sizeof(size_t);

/* Defines block_header_t and block_footer_t for the block_t structs. */
typedef size_t block_header_t;
typedef size_t block_footer_t;

/** The layout of each block allocated on the heap */
typedef struct {
    /** Footer contains the size of previous block and whether it is allocated*/
    block_footer_t footer;
    /** header contains the size of this block and whether it is allocated*/
    block_header_t header;
    /* The payload (unknown size) is declared as a zero-length array to get a pointer to
     * the payload. */
    uint8_t payload[];
} block_t;

/** The layout of each 'free-block' which would just be the linked node contained in the
 * payload. */
typedef struct linked_node_t {
    /** *prev is a pointer to the previous free-block. */
    struct linked_node_t *prev;
    /** *next is a pointer to the next free-block. */
    struct linked_node_t *next;
} linked_node_t;

/** Define first and last free blocks as global. Size of first = 16 bytes. Size of last =
 * 16 bytes. */
linked_node_t *first = NULL;
linked_node_t *last = NULL;

/** Rounds up `size` to the nearest multiple of `n` */
static size_t round_up(size_t size, size_t n) {
    return (size + (n - 1)) / n * n;
}

/* Set block header with payload-size and allocation. Set footer in the adjacent sbrk'd
 * struct with payload-size and allocation of the previous block. */
static void set_header_footer(block_t *block, size_t payload_size, bool is_allocated) {
    block->header = payload_size | is_allocated;
    block_footer_t *footer =
        (block_footer_t *) ((void *) block + ALIGNMENT + payload_size);
    *footer = payload_size | is_allocated;
}

/** Extract the payload size of the entire block size from its header */
static size_t get_size(block_t *block) {
    return block->header & ~1;
}

/** Extracts the previous block's payload size from its footer */
static size_t get_previous_size(block_t *block) {
    return block->footer & ~1;
}

/** Extracts the previous block's allocation state from its footer */
bool is_previous_allocated(block_t *block) {
    return block->footer & 1;
}

/** Extracts the next block's (or epilogue's) allocation state. */
bool is_next_allocated(block_t *block) {
    block_header_t *header = (block_header_t *) ((void *) block + ALIGNMENT +
                                                 get_size(block) + sizeof(size_t));
    if ((*header & ~1) != 0) {
        return (*header & 1);
    }
    else {
        return true;
    }
}

/** Extracts the next block from the location of a block. **/
static block_t *get_next_block(block_t *block) {
    return (block_t *) ((void *) block + ALIGNMENT + get_size(block));
}

/* Extracts the previous block from the location of a block. **/
block_t *get_previous_block(block_t *block) {
    return (block_t *) ((void *) block - get_previous_size(block) - ALIGNMENT);
}

/** Gets the block pointer from a payload pointer */
static block_t *block_from_payload(void *ptr) {
    return ptr - ALIGNMENT;
}

/** Get the block pointer from a linked_node_t ptr */
static block_t *get_block_from_linked_node(linked_node_t *ptr) {
    return (block_t *) ((void *) ptr - ALIGNMENT);
}

/** Get the linked node pointer from the block. */
static linked_node_t *get_linked_node_from_block(block_t *block) {
    return (linked_node_t *) ((void *) block + ALIGNMENT);
}

/** Deletes the linked node in @block from a free list. */
static void delete_linked_node_from_block(block_t *block) {
    linked_node_t *unlink = get_linked_node_from_block(block);
    (unlink->prev)->next = unlink->next;
    (unlink->next)->prev = unlink->prev;
}

static void add_linked_node_to_block(block_t *block) {
    linked_node_t *link = get_linked_node_from_block(block);
    link->prev = last->prev;
    link->next = last;
    last->prev = link;
    (link->prev)->next = link;
}

/*Helper function to implement block splitting. 
   @param block is a pointer to the block to be split
   @param block_size is the size of the payload to be split
   @param size is the size of the payload that is given to the user
*/
static void block_split(block_t *block, size_t block_size, size_t size) {
    //Gets the right-part of the split block. Sets its headers and footers and moves
    //linked node.
    set_header_footer(block, size, true);
    block_t *next_block = get_next_block(block);
    set_header_footer(next_block, block_size - size - ALIGNMENT, false);
    delete_linked_node_from_block(block);
    add_linked_node_to_block(next_block);
}

/** Helper function to coalesce. */
static void coalesce(block_t *block, size_t size) {
    if (!is_previous_allocated(block)) {
        // Left block should coalesce with center block.
        size += get_previous_size(block) + ALIGNMENT;
        set_header_footer(get_previous_block(block), size, false);
        // Checks if the block should coalesce to the right.
        delete_linked_node_from_block(block);
        if (!is_next_allocated(block)) {
            block_t *next_block = get_next_block(block);
            size += get_size(next_block) + ALIGNMENT;
            set_header_footer(get_previous_block(block), size, false);
            delete_linked_node_from_block(next_block);
            return;
        }
        return;
    }
    if (!is_next_allocated(block)) {
        // Center block should coalesce to the right.
        block_t *next_block = get_next_block(block);
        size += get_size(next_block) + ALIGNMENT;
        set_header_footer(block, size, false);
        delete_linked_node_from_block(next_block);
    }
}

/* mm_init - Initialize the allocator state */
bool mm_init(void) {
    // sbrk two free-nodes. first is the start of the free-list. last is the last
    // linked-node.
    first = (linked_node_t *) mem_sbrk(ALIGNMENT);
    last = (linked_node_t *) mem_sbrk(ALIGNMENT);
    if ((first == (void *) -1) || (last == (void *) -1)) {
        return false;
    }
    // first->prev = last->next = NULL. first->next and last->next can change during
    // run-time. When freeing blocks in mm_free, add a free_node before last.
    first->prev = NULL;
    first->next = last;
    last->prev = first;
    last->next = NULL;
    // Set prologue and epilogue in heap, with 0 size and allocated so no coalescing
    // happens with them. The epilogue has to be updated everytime a new memory is sbrk'd
    // in malloc.
    block_footer_t *prologue = (block_footer_t *) mem_sbrk(sizeof(size_t));
    block_header_t *epilogue = (block_header_t *) mem_sbrk(sizeof(size_t));
    if ((prologue == (void *) -1) || (epilogue == (void *) -1)) {
        return false;
    }
    *prologue = 0 | true;
    *epilogue = 0 | true;
    return true;
}

/** Find the first free block with a size that is >= size. Return NULL If no block is
 * large enough. */
static block_t *find_fit(size_t size) {
    for (linked_node_t *free_node = last->prev; free_node != first;
         free_node = free_node->prev) {
        block_t *free_block = get_block_from_linked_node(free_node);
        size_t block_size = get_size(free_block);
        // Checks if the free block is suitable for return. Sets header/footer.
        if (block_size >= size) {
            set_header_footer(free_block, get_size(free_block), true);
            if (block_size < size + 2 * ALIGNMENT) {
                delete_linked_node_from_block(free_block);
                return (free_block);
            }
            block_split(free_block, block_size, size);
            return free_block;
        }
    }
    return NULL;
}

/* mm_malloc - Allocates a block with the given size */
void *mm_malloc(size_t size) {
    size = round_up(size, ALIGNMENT);
    // If there is a large enough free block, use it.
    block_t *block = find_fit(size);
    if (block != NULL) {
        return block->payload;
    }
    /* sbrk payload,footer,epilogue at end of heap. Prev epilogue now becomes header for
     * struct. */
    void *block_address = mem_sbrk(size) - ALIGNMENT;
    block_footer_t *new_footer = (block_footer_t *) mem_sbrk(sizeof(block_footer_t));
    if (new_footer == (void *) -1) {
        return NULL;
    }
    block = (block_t *) block_address;
    if (block == (void *) -1) {
        return NULL;
    }
    block_header_t *epilogue = (block_header_t *) mem_sbrk(sizeof(block_header_t));
    *epilogue = 0 | true;
    // Set the header and footer for this block. Allocate to true.
    set_header_footer(block, size, true);
    return block->payload;
}

/* mm_free - Releases a block to be reused for future allocations. */
void mm_free(void *ptr) {
    if (ptr == NULL) {
        return; // mm_free(NULL) does nothing.
    }
    // Mark the block as unallocated.
    block_t *block = block_from_payload(ptr);
    set_header_footer(block, get_size(block), false);
    add_linked_node_to_block(block);
    // Check if coalescing is required.
    if (!is_previous_allocated(block) || !is_next_allocated(block)) {
         coalesce(block, get_size(block));
    }
}

/* mm_realloc - Change block size by mm_mallocing a new block, copying the data,
 * mm_freeing the old block. */
void *mm_realloc(void *old_ptr, size_t size) {
    // If old_ptr is null, just mm_malloc the size and return its pointer
    if (old_ptr == NULL) {
        return (mm_malloc(size));
    }
    // If size is 0, just free the old pointer and return NULL
    else if (size == 0) {
        mm_free(old_ptr);
        return (NULL);
    }
    else {
        void *new_ptr = mm_malloc(size); // Malloc a new block.
        size_t old_size = get_size(block_from_payload(old_ptr));
        size_t new_size = get_size(block_from_payload(new_ptr));
        // Copies the data to the smaller structure and frees the old pointer.
        size_t smaller_size = (old_size > new_size) ? new_size : old_size;
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

void mm_checkheap(void) {
}
