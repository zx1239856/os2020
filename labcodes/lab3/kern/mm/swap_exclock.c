#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_exclock.h>
#include <list.h>

list_entry_t pra_list_head, *clock;

static int
_exclock_init_mm(struct mm_struct *mm)
{
    list_init(&pra_list_head);
    clock = mm->sm_priv = &pra_list_head;
    return 0;
}

static int
_exclock_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    list_entry_t *entry = &(page->pra_page_link);

    assert(entry != NULL && head != NULL);
    list_add(clock, entry);
    return 0;
}

static int
_exclock_swap_out_victim(struct mm_struct *mm, struct Page **ptr_page, int in_tick)
{
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    assert(head != NULL);
    assert(clock != NULL);
    assert(in_tick == 0);
#ifdef EXCLOCK_VERBOSE
    for(list_entry_t *st = head->prev; st != head; st = st->prev) {
        struct Page *page = le2page(st, pra_page_link);
        pte_t *ptep = get_pte(mm->pgdir, page->pra_vaddr, 0);
        uint32_t A = *ptep & PTE_A, D = *ptep & PTE_D;
        cprintf("Page: 0x%x (A:%d, D:%d)--> ", page->pra_vaddr, A != 0, D != 0);
    }
    cprintf("end\n");
    
    if(clock == head) clock = clock->prev;
    struct Page *page = le2page(clock, pra_page_link);
    pte_t *ptep = get_pte(mm->pgdir, page->pra_vaddr, 0);
    uint32_t A = *ptep & PTE_A, D = *ptep & PTE_D;
    cprintf("CLOCK Ptr ==> 0x%x (A:%d, D:%d)\n", page->pra_vaddr, A != 0, D != 0);
#endif
    // find victim
    for( ; ; clock = clock->prev) {
        if(clock == head) continue;
        struct Page *page = le2page(clock, pra_page_link);
        pte_t *ptep = get_pte(mm->pgdir, page->pra_vaddr, 0);
        assert(*ptep & PTE_P);
        uint32_t A = *ptep & PTE_A, D = *ptep & PTE_D;
        if(A == 0 && D == 0) {
            // 00, replace
            *ptr_page = page;
            break;
        } else {
            // 01 -> 00, write back
            if (A == 0) {
                if (swapfs_write( (page->pra_vaddr/PGSIZE+1)<<8, page) != 0) {
                    cprintf("SWAP: failed to save\n");
                    continue;
                }
                *ptep &= ~PTE_D;
            } else {
                // 1x -> 0x
                *ptep &= ~PTE_A;
            }
            tlb_invalidate(mm->pgdir, page->pra_vaddr);
        }
    }
    clock = clock->prev;
    list_del(clock->next);
    
    if(clock == head) clock = clock->prev;
#ifdef EXCLOCK_VERBOSE
    cprintf("After operation: \n");
    for(list_entry_t *st = head->prev; st != head; st = st->prev) {
        struct Page *page = le2page(st, pra_page_link);
        pte_t *ptep = get_pte(mm->pgdir, page->pra_vaddr, 0);
        uint32_t A = *ptep & PTE_A, D = *ptep & PTE_D;
        cprintf("Page: 0x%x (A:%d, D:%d)--> ", page->pra_vaddr, A != 0, D != 0);
    }
    cprintf("end");
    page = le2page(clock, pra_page_link);
    ptep = get_pte(mm->pgdir, page->pra_vaddr, 0);
    A = *ptep & PTE_A; D = *ptep & PTE_D;
    cprintf("\n CLOCK Ptr ==> 0x%x (A:%d, D:%d)\n", page->pra_vaddr, A != 0, D != 0);
#endif
    return 0;
}


static int
_exclock_check_swap(void)
{
    // clear write for a ~ d
    pde_t *pgdir = KADDR((pde_t*) rcr3());
    for (int i = 1; i <= 4; ++i) {
        pte_t *ptep = get_pte(pgdir, i * 0x1000, 0);
        swapfs_write((i * 0x1000 / PGSIZE + 1) << 8, pte2page(*ptep));
        *ptep &= ~(PTE_A | PTE_D);
        tlb_invalidate(pgdir, i * 0x1000);
    }

    cprintf("read Virt Page c in exclock_check_swap\n");
    unsigned char tmp = *(unsigned char *)0x3000;
    assert(pgfault_num == 4);
    cprintf("write Virt Page a in exclock_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 4);
    cprintf("read Virt Page d in exclock_check_swap\n");
    tmp = *(unsigned char *)0x4000;
    assert(pgfault_num == 4);
    cprintf("write Virt Page b in exclock_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 4);
    cprintf("read Virt Page e in exclock_check_swap\n");
    tmp = *(unsigned char *)0x5000;
    assert(pgfault_num == 5);
    cprintf("read Virt Page b in exclock_check_swap\n");
    tmp = *(unsigned char *)0x2000;
    assert(pgfault_num == 5);
    cprintf("write Virt Page a in exclock_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 5);
    cprintf("read Virt Page b in exclock_check_swap\n");
    tmp = *(unsigned char *)0x2000;
    assert(pgfault_num == 5);
    cprintf("read Virt Page c in exclock_check_swap\n");
    tmp = *(unsigned char *)0x3000;
    assert(pgfault_num == 6);
    cprintf("read Virt Page d in exclock_check_swap\n");
    tmp = *(unsigned char *)0x4000;
    assert(pgfault_num == 7);
    cprintf("read Virt Page e in exclock_check_swap\n");
    tmp = *(unsigned char *)0x5000;
    assert(pgfault_num == 7);
    cprintf("read Virt Page a in exclock_check_swap\n");
    tmp = *(unsigned char *)0x1000;
    assert(pgfault_num == 7);
    return 0;
}

static int
_exclock_init(void)
{
    return 0;
}

static int
_exclock_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_exclock_tick_event(struct mm_struct *mm)
{
    return 0;
}

struct swap_manager swap_manager_exclock =
    {
        .name = "exclock swap manager",
        .init = &_exclock_init,
        .init_mm = &_exclock_init_mm,
        .tick_event = &_exclock_tick_event,
        .map_swappable = &_exclock_map_swappable,
        .set_unswappable = &_exclock_set_unswappable,
        .swap_out_victim = &_exclock_swap_out_victim,
        .check_swap = &_exclock_check_swap,
};