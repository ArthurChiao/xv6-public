#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <assert.h>
#include <sys/mman.h>

#include <stdint.h>


#define NALLOC  1024  /* minimum #units to request */

#define DEBUG 1

////////////////////////////////////////////////////////////////////////////
// K&R Allocator
//
// idea from K&R C book, Chapter 8.7 "Storage Allocator"
//
// Design:
// 1. Divide memory into blocks
// 2. Use a linked list to track all availabel blocks
// 3. Allocate strategy: first-fit
//
// Data structure
// 1. each block starts with a simple header
// 2. a header contains a pointer to the next availabel block, and the
//    available size in this block
// 3. make each block aliagned: sizeof(block) == N * sizeof(header)
//
// Pros & Cons
// 1. Memory overhead: if malloc 16 bytes, 50% overhead
// 2. Efficiency: workload dependent
//   a. sequentially return blocks: free list has only one element
//   b. out-of-order turn blocks: search long list
////////////////////////////////////////////////////////////////////////////

// Linked list
struct header {
    struct header *next;
    size_t size;         // in unit of sizeof(struct header)
};

static struct header base;
static struct header *free_list = NULL;

void
kr_free(void *addr) {
    if (!addr) {
        return;
    }

    struct header *curr;
    struct header *h = (struct header *)addr - 1; // point to block header

    // 找到插入位置，可能是：
    // * curr <= h <= curr->next 的位置
    // * 链表头或尾的位置
    for (curr=free_list; !(h > curr && h < curr->next); curr = curr->next) {
        if (curr >= curr->next && (h > curr || h < curr->next)) {
            break; /* freed block at start or end of the arena */
        }
    }

    // h 与 curr->next 是否能合并
    if (h + h->size == curr->next) { /* join to upper neighbor */
        h->size += curr->next->size;
        h->next = curr->next->next;
    } else {
        h->next = curr->next;
    }

    // curr 与 h 是否能合并
    if (curr + curr->size == h) { /* join to lower neighbor */
        curr->size += h->size;
        curr->next = h->next;
    } else {
        curr->next = h;
    }

    free_list = curr;
}

static struct header * moreheap(size_t nu) {
    if (nu < NALLOC) {
        nu = NALLOC;
    }

    char *blk = sbrk(nu * sizeof(struct header));
    if (blk == (char *) -1) {
        return NULL;
    }

    struct header *h = (struct header *)blk;
    h->size = nu;

    kr_free((void *)(h + 1));

    return free_list;
}

void *
kr_malloc(size_t nbytes) {
    struct header *prev, *curr;
    unsigned nunits;

    if ((prev = free_list) == NULL) { /* no free list yet */
        base.next = prev = free_list = &base;
        base.size = 0;
    }

    nunits = (nbytes + sizeof(struct header) - 1)/sizeof(struct header) + 1;

    for (curr=prev->next; ; prev = curr, curr = curr->next) {
        if (curr->size >= nunits) { /* big enough */
            if (curr->size == nunits) { /* exactly match */
                prev->next = curr->next;
            } else { /* allocate tail end */
                curr += (curr->size - nunits);   // offset by curr->size
                curr->size = nunits;
            }

            free_list = prev;
            return (void *)(curr + 1); // offset by sizeof(struct header) bytes
        }

        if (curr == free_list) { /* wrapped around free list */
            if ((curr = (struct header *)moreheap(nunits)) == NULL) {
                return NULL;
            }
        }
    }
}

void
kr_print(void) {
    struct header *prev = free_list;
    struct header *curr = NULL;

    for (curr = prev->next; ; prev = curr, curr = curr->next) {
        printf("%p %d\n", curr, (int)curr->size);

        if (curr == free_list) {
            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////
// struct region Allocator
//
// Desgin:
// 1. Allocate a memory region, malloc in this region
// 2. Free a region until the complete region is unused
//
// Pros & Cons
// 1. Fast allocate/free
// 2. Serious fragmentation (unused but could not be release holes)
// 3. Application must fit region use (non-agnostic)
////////////////////////////////////////////////////////////////////////////
struct region {
    void *start;
    void *curr;
    void *end;
};

static struct region rg_base;

struct region *
rg_create(size_t nbytes) {
    rg_base.start = sbrk(nbytes);
    rg_base.curr = rg_base.start;
    rg_base.end = rg_base.curr + nbytes;

    return &rg_base;
}

void *
rg_malloc(struct region *r, size_t nbytes) {
    assert(r->curr + nbytes <= r->end);
    void *p = r->curr;
    r->curr += nbytes;
    return p;
}

// free all memory in region
void
rg_free(struct region *r) {
    r->curr = r->start;
}


////////////////////////////////////////////////////////////////////////////
// Buddy Allocator
//
// Desgin: https://en.wikipedia.org/wiki/Buddy_memory_allocation
//
////////////////////////////////////////////////////////////////////////////

#define LEAF_SIZE_BYTE         16 // The smallest allocation size (in bytes)
#define MAX_LEVELS             15
#define BLK_SIZE_BYTE(k)       ((1L << (k)) * LEAF_SIZE_BYTE) // Size in bytes for size k
#define HEAP_SIZE              BLK_SIZE_BYTE(MAX_LEVELS-1) 
#define NUM_BLKS_AT_LEVEL(k)   (1 << (MAX_LEVELS-1-k))  // Number of block at level k
#define ROUND_UP(n,sz)         (((((n)-1)/(sz))+1)*(sz))  // Round up to the next multiple of sz

// 将用作循环链表
// * 初始化时自己的 next 指向自己
// * 非空时首尾相连
struct bd_list {
    struct bd_list *next;
    struct bd_list *prev;
};

// The allocator has level_info for each size k. Each level_info has a free
// list, an array alloc to keep track which blocks have been
// allocated, and an split array to to keep track which blocks have
// been split.  The arrays are of type char (which is 1 byte), but the
// allocator uses 1 bit per block (thus, one char records the info of
// 8 blocks).
struct level_info {
    struct bd_list free_list;
    char *alloc;
    char *split;
};

// Level 越大，对应的 block size 越大
// * level=0           ：对应最小的块
// * level=MAX_LEVELS-1：对应最大的块（整个堆）
static struct level_info bd_levels[MAX_LEVELS];
static void *bd_mem_base = NULL; /* start memory addr managed by buddy allocator */

void
list_init(struct bd_list *l){
    l->prev = l;
    l->next = l;
}

int
list_empty(struct bd_list *l) {
    return l->next == l;
}

void
list_remove(struct bd_list *l) {
    l->prev->next = l->next;
    l->next->prev = l->prev;
}

void *
list_pop(struct bd_list *l) {
    assert(l->next != l);
    struct bd_list *p = l->next;
    list_remove(p);
    return (void *)p;
}

void
list_push(struct bd_list *l, void *p) {
    struct bd_list *e = (struct bd_list *)p;
    e->next = l->next;
    e->prev = l;
    l->next->prev = e;
    l->next = e;
}

void
list_print(struct bd_list *l) {
    for(struct bd_list *p=l->next; p!=l; p=p->next) {
        printf(" %p", p);
    }
    printf("\n");
}

int
bit_isset(char *array, int index) {
    char b = array[index/8];
    char m = (1 << (index % 8));
    return (b & m) == m;
}

// Set bit at array[index] to 1
void
bit_set(char *array, int index) {
    char b = array[index/8];
    char m = (1 << (index % 8));
    array[index/8] = (b | m);
}

void
bit_clear(char *array, int index) {
    char b = array[index/8];
    char m = (1 << (index % 8));
    array[index/8] = (b & ~m);
}

void bd_init() {
    // allocate a kernel buffer with mmap
    bd_mem_base = mmap(NULL,  // let kernel choose the start addr
            HEAP_SIZE,        // buffer size
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,  // copy-on-write, zero initialized
            -1,               // file descriptor, not used
            0                 // file offset, not used
            );
    if (bd_mem_base == MAP_FAILED) {
        fprintf(stderr, "buddy allocator: mmap heap failed: %s\n", strerror(errno));
        return;
    }

    assert(bd_mem_base);

    // init bd_levels
    for (int k=0; k<MAX_LEVELS; k++) {
        list_init(&bd_levels[k].free_list);

        int n = sizeof(char) * ROUND_UP(NUM_BLKS_AT_LEVEL(k), 8) / 8;
        bd_levels[k].alloc = malloc(n);
        memset(bd_levels[k].alloc, 0, n);

        if (k > 0) {
            bd_levels[k].split = malloc(n);
            memset(bd_levels[k].split, 0, n);
        }
    }

    // 将堆的起始地址 push 到最大块组成的链表，即整个堆不作任何分割
    list_push(&bd_levels[MAX_LEVELS-1].free_list, bd_mem_base);
}

// What is the least k that 2^k >= n?
int
least_k(int n) {
   int k = 0;
   size_t m = LEAF_SIZE_BYTE;

   while (m < n) {
       m *= 2;
       k++;
   }

   return k;
}

// Compute the block index for address p at size k
int
addr_to_blk_idx(char *addr, int k) {
    int n = addr - (char *)bd_mem_base;
    return n / BLK_SIZE_BYTE(k);
}

// Convert a block index at size k back into an address
void *
blk_idx_to_addr(int idx, int k) {
    int n = BLK_SIZE_BYTE(k) * idx;
    return (char *)bd_mem_base + n;
}

void *
bd_malloc(size_t nbytes) {
    int min_level, k;

    assert(bd_mem_base != NULL);

    // Find a free block >= nbytes, starting with smallest k possible
    min_level = least_k(nbytes);
    for (k=min_level; k<MAX_LEVELS; k++) {
        if (!list_empty(&bd_levels[k].free_list)) {
            break;
        }
    }

    if (k >= MAX_LEVELS) { // No free blocks
        return NULL;
    }

    // Found one, pop it and potentially split it
    char *p = list_pop(&bd_levels[k].free_list);
    bit_set(bd_levels[k].alloc, addr_to_blk_idx(p, k));
    for (; k > min_level; k--) {
        char *q = p + BLK_SIZE_BYTE(k-1);
        bit_set(bd_levels[k].split, addr_to_blk_idx(p, k));
        bit_set(bd_levels[k-1].alloc, addr_to_blk_idx(p, k-1));
        list_push(&bd_levels[k-1].free_list, q);
    }

    return p;
}

int
least_split_k(char *p) {
    for (int k=0; k<MAX_LEVELS-1; k++) {
        if (!bit_isset(bd_levels[k+1].split, addr_to_blk_idx(p, k+1))) {
            return k;
        }
    }

    return 0;
}

void
bd_free(void *p) {
    void *q;
    int k;

    for (k=least_split_k(p); k<MAX_LEVELS-1; k++) {
        // 清除第 k 层的"已分配"标记
        int idx = addr_to_blk_idx(p, k);
        bit_clear(bd_levels[k].alloc, idx);

        // 伙伴（直接相邻的邻居）：左边或右边的同等大小块
        int buddy = (idx % 2 == 0) ? idx+1: idx-1;
        if (bit_isset(bd_levels[k].alloc, buddy)) {
            // 如果该伙伴已经分配出去，就不需要合并，直接返回
            break;
        }

        // 伙伴也已经是释放状态，执行合并
        q = blk_idx_to_addr(buddy, k);
        list_remove(q);
        if (buddy % 2 == 0) {
            p = q; // 合并后的新起始地址
        }

        // 清除上一层的 split 标记
        bit_clear(bd_levels[k+1].split, addr_to_blk_idx(p, k+1));
    }

    // 将最后计算出的空闲空间起始地址 push 到第 k 层的 free list
    // * 如果没有执行合并，那这个 p 就是 bd_free() 传入的 p
    // * 如果执行了合并，那这个 p 是合并之后块的起始地址，k 是对应的层级
    list_push(&bd_levels[k].free_list, p);
}

void
bd_print() {
  printf("=== buddy allocator state ===\n");
  for (int k = 0; k < MAX_LEVELS; k++) {
    printf("size %d (%ld): free list: ", k, BLK_SIZE_BYTE(k));
    list_print(&bd_levels[k].free_list);

    printf("  alloc:");
    for (int b = 0; b < NUM_BLKS_AT_LEVEL(k); b++) {
      printf(" %d", bit_isset(bd_levels[k].alloc, b));
    }
    printf("\n");

    if(k > 0) {
      printf("  split:");
      for (int b = 0; b < NUM_BLKS_AT_LEVEL(k); b++) {
          printf(" %d", bit_isset(bd_levels[k].split, b));
      }
      printf("\n");
    }
  }
}

//
// Example use case: a "weird" list of sz elements
//

struct list {
  struct list *next;
  int content;
};

// Use malloc and free for list of sz elements
void
workload(int sz, void* malloc(size_t),  void free(void *)) {
  struct list *head = 0;
  struct list *tail;
  struct list *end;
  struct list *p, *n;
  int i = 0;

  // initialize list with 2 elements: head and tail */
  head = malloc(sizeof(struct list));
  tail = malloc(sizeof(struct list));
  head->next = tail;
  tail->next = 0;

  // every even i: insert at head; every odd i: append, so that blocks
  // aren't freed in order below
  for (i = 0; i < sz; i++) {
    p = (struct list *) malloc(sizeof(struct list));
    assert(p != NULL);
    if(i % 2 ==0) {
      p->next = head;
      head = p;
    } else {
      tail->next = p;
      p->next = 0;
      tail = p;
    }
  }

  // now free list
  for (p = head; p != NULL; p = n) {
    n = p->next;
    free(p);
  }
}

#define HEAPINC (100*4096)


// Use a region for a list of sz elements
void
rg_workload(int sz) {
  struct list h;
  struct list *p;
  struct region *r = rg_create(HEAPINC);
  
  h.next = NULL;
  for (int i = 0; i < sz; i++) {
    p = rg_malloc(r, 16);
    assert(p != NULL);
    p->next = h.next;
    h.next = p;
  }
  rg_free(r);
}

int
main(char *argv, int argc)
{
  enum { N = 10000 };
  struct timeval start, end;

  gettimeofday(&start, NULL);
  workload(N, kr_malloc, kr_free);
  gettimeofday(&end, NULL);
  printf("elapsed time K&R is    %ld usec\n", 
	 (end.tv_sec-start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec);

  gettimeofday(&start, NULL);
  rg_workload(N);
  gettimeofday(&end, NULL);
  printf("elapsed time region is %ld usec\n", 
	 (end.tv_sec-start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec);
  
  bd_init();
  gettimeofday(&start, NULL);
  workload(N, bd_malloc, bd_free);
  gettimeofday(&end, NULL);
  printf("elapsed time buddy is  %ld usec\n", 
	 (end.tv_sec-start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec);

  /* set NSIZES to 4
  bd_init();
  bd_print();
  char *p1 = bd_malloc(8);
  bd_print();
  bd_free(p1);
  bd_print();
  */

  /*
  char *p2 = bd_malloc(8);
  bd_print();
  char *p3 = bd_malloc(8);
  bd_print();
  bd_free(p2);
  bd_print();
  bd_free(p3);
  bd_print();
  */
}
