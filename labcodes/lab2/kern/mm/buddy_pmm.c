#include <buddy_pmm.h>
#include <stdio.h>

#define DEPTH_LIMIT 25

static uint32_t *buddy_arr;  // array to store buddy tree
static uint32_t max_buddy_pages; // max number of pages occupied by buddy
static uint32_t max_logical_pages;  // max logical pages
static uint32_t max_alloc_pages; // max pages for allocatable pages
static struct Page *alloc_base; // allocatable page base for buddy
static uint32_t nr_free;

#define left_child(x) (((x) << 1) | 1)
#define right_child(x) (((x) + 1) << 1)
#define parent(x) (((x) - 1) >> 1)
#define node_idx_limit(x) (((x) << 1) - 1)

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

static void buddy_init(void) {}

static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    max_logical_pages = 1;
    // assume there are x pages allocatable
    // x + x / 4kB * 4B (size overhead) * 2 (binary tree) <= n
    for(int i = 0; i < DEPTH_LIMIT; ++i, max_logical_pages <<= 1) {
        if(max_logical_pages +(max_logical_pages >> 9) >= n)
            break;
    }
    max_buddy_pages = max_logical_pages >> 9;
    nr_free = max_alloc_pages = n - max_buddy_pages;
    cprintf("Buddy init: total: %u, available: %u, internal: %u\n", n, max_alloc_pages, max_buddy_pages);
    for(int i = 0; i < max_buddy_pages; ++i) {
        SetPageReserved(base + i);
    }

    alloc_base = base + max_buddy_pages;
    for(struct Page *p = alloc_base; p != base + n; ++p) {
        assert(PageReserved(p));
        p->flags = 0;
        set_page_ref(p, 0);
        SetPageProperty(p);
    }
    SetPageProperty(base);
    buddy_arr = (uint32_t*)page2kva(base);
    // initialize buddy arr
    for(int i = max_logical_pages - 1; i < max_logical_pages - 1 + max_alloc_pages; ++i)
        buddy_arr[i] = 1;
    for(int i = max_logical_pages - 1 + max_alloc_pages; i < (max_logical_pages << 1) - 1; ++i)
        buddy_arr[i] = 0; // logical available but not physically so
    for(int i = max_logical_pages - 2; i >= 0; --i)
        buddy_arr[i] =  (buddy_arr[left_child(i)] == buddy_arr[right_child(i)]) ? 
        (buddy_arr[left_child(i)] << 1) : max(buddy_arr[left_child(i)], buddy_arr[right_child(i)]);
}

static struct Page * buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if(n > buddy_arr[0])
        return NULL;
    uint32_t idx = 0;
    uint32_t size = max_logical_pages;
    for(; size >= n && idx < max_logical_pages - 1; size >>= 1) {
        if(buddy_arr[left_child(idx)] >= n) idx = left_child(idx);
        else if(buddy_arr[right_child(idx)] >= n) idx = right_child(idx);
        else break;
    }
    assert(size >= n);
    buddy_arr[idx] = 0;
    // alloc
    struct Page *page = alloc_base + (idx + 1) * size - max_logical_pages;
    // node with index I is the (I - max_size / size + 1)-th subtree
    for(struct Page *p = page; p != page + size; ++p) {
        ClearPageProperty(p);
        set_page_ref(p, 0);
    }
    nr_free -= size;
    // push to root
    do {
        idx = parent(idx);
        buddy_arr[idx] = max(buddy_arr[left_child(idx)], buddy_arr[right_child(idx)]);
    } while (idx != 0);
    return page;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    uint32_t idx = (uint32_t)(base - alloc_base) + max_logical_pages - 1;
    uint32_t size = 1;
    for(; buddy_arr[idx] > 0; idx = parent(idx), size <<= 1);
    n = min(n, size);
    for(struct Page *p = base; p != base + n; ++p) {
        assert(!PageReserved(p) && !PageProperty(p));
        SetPageProperty(p);
        set_page_ref(p, 0);
    }
    buddy_arr[idx] = size;
    nr_free += size;
    // push to root
    do {
        idx = parent(idx);
        size <<= 1;
        if(buddy_arr[left_child(idx)] + buddy_arr[right_child(idx)] == size) {
            buddy_arr[idx] = size; // merge in this case
        }
        else {
            buddy_arr[idx]= max(buddy_arr[left_child(idx)], buddy_arr[right_child(idx)]);
        }
    } while (idx != 0);
}

static size_t buddy_nr_free_pages(void) {
    return nr_free;
}

static void buddy_check(void) {
    // check the validity of buddy algorithm
    size_t num_pages = buddy_nr_free_pages();
    assert(num_pages > 0);
    assert(alloc_pages(num_pages + 1) == NULL);

    struct Page *p0, *p1, *p2, *p3;
    p0 = alloc_pages(1);
    assert(p0 != NULL);

    p1 = alloc_pages(2);
    assert(p1 != NULL);
    assert(p0 + 2 == p1);

    p2 = alloc_pages(1);
    assert(p2 != NULL);
    assert(p0 + 1 == p2);

    p3 = alloc_pages(2);
    assert(p3 != NULL);
    assert(p0 + 4 == p3);

    assert(!PageReserved(p0) && !PageProperty(p0));
    assert(!PageReserved(p1) && !PageProperty(p1));
    assert(!PageReserved(p2) && !PageProperty(p2));
    assert(!PageReserved(p3) && !PageProperty(p3));

    free_pages(p0, 1);
    assert(p0->ref == 0);
    assert(PageProperty(p0));

    free_pages(p1, 2);
    free_pages(p2, 1);

    p0 = alloc_pages(3);
    assert(p0 + 4 == p3);

    free_pages(p0, 3);
    free_pages(p3, 2);

    p0 = alloc_pages(63);
    assert(nr_free_pages() == num_pages - 64);
    free_pages(p0, 64);
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};

