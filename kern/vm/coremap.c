#include <types.h>
#include <spinlock.h>
#include <vm.h>
#include <lib.h>
#include <coremap.h>
#include <pagetable.h>

#include <synch.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <kern/errno.h>
#include <spl.h>
#include <addrspace.h>
#include <backingstore.h>

int stat_coremap(int nargs, char **args) {
    (void)nargs;
    (void)args;
	spinlock_acquire(&coremap.lock);

    kprintf("coremap.kernel: %d\n"
            "coremap.used: %d\n"
            "coremap.size: %d\n"
            "coremap.busy: %d\n"
            "coremap.last_alloc: %d\n",
            coremap.kernel, coremap.used, coremap.size, coremap.busy,
            coremap.last_allocated);

	spinlock_release(&coremap.lock);
    return 0;
}

/* must be called with acquired spinlock */
void
set_use_bit(int index, int bitvalue) {
    coremap.cm[index].use = bitvalue;
    (bitvalue) ? coremap.used++ : coremap.used--;
}

/* must be called with acquired spinlock */
void
set_busy_bit(int index, int bitvalue) {
    coremap.cm[index].busybit = bitvalue;
    (bitvalue) ? coremap.busy++ : coremap.busy--;
}

/* must be called with acquired spinlock */
void
set_kern_bit(int index, int bitvalue) {
    coremap.cm[index].kern = bitvalue;
    (bitvalue) ? coremap.kernel++ : coremap.kernel--;
}

void cm_bootstrap(void) {
    paddr_t lo;
    paddr_t hi;

    ram_getsize(&lo, &hi);
    uint32_t free_pages = (hi - lo) / PAGE_SIZE;    /* available aka not stolen */
    uint32_t stolen_pages = lo / PAGE_SIZE;
    uint32_t total_pages = free_pages + stolen_pages; /* available + stolen */

    spinlock_init(&coremap.lock);
    coremap.free = free_pages;
    coremap.size = total_pages;
    coremap.modified = 0;
    coremap.cm = (struct cme *)PADDR_TO_KVADDR(lo);

    uint32_t alloc_pages =  /* npages cm occupies + stolen_pages */
        (ROUNDUP(total_pages * sizeof(struct cme), PAGE_SIZE) / PAGE_SIZE)
        + stolen_pages;

    for (unsigned i = 0; i < alloc_pages; i++) {
        set_kern_bit(i, 1);
        set_use_bit(i, 1);
    }

    coremap.last_allocated = --alloc_pages;
}

void
vm_bootstrap(void)
{
    cm_bootstrap();
}

// Returns with busy bit set on the entry
paddr_t
get_free_cme(vaddr_t vpn, bool is_kern) {

	spinlock_acquire(&coremap.lock);
	int index = coremap.last_allocated;
	spinlock_release(&coremap.lock);
	for(unsigned round = 0; round <3; round++){
		for(unsigned i = 0; i < coremap.size; i++){
			index = (index+1) % coremap.size;
			// TODO we can probably wait here if this becomes an issue
			if(core_set_busy(index, NO_WAIT) == 0 && coremap.cm[index].kern == 0){
				// Check if in use
				if (coremap.cm[index].use == 0) {
					set_use_bit(index, 1);
					if (is_kern) set_kern_bit(index, 1);

					coremap.cm[index].slen = 1;
					coremap.cm[index].vpn = vpn;
					coremap.cm[index].pid = (is_kern) ? 0 : curproc->pid;

					spinlock_acquire(&coremap.lock);
					//coremap.last_allocated = index;
					kprintf("cmi: %zu; used: %zu\n", index, coremap.used);
					spinlock_release(&coremap.lock);

					return CMI_TO_PADDR(index);

				// TODO write a helper function to abstract the next two functions
				}else if(round >= 1 && coremap.cm[index].dirty == 0){
					kprintf("evicting clean page\n");
					// Steal cleaned page and evict
					// TODO Somehow get address space/ page dir from coremap.cm[index].pid
					struct addrspace *as;
					int pdi = PDI(coremap.cm[index].vpn);
					int pti = PTI(coremap.cm[index].vpn);

					// Give up here to avoid deadlock
					if(page_set_busy(as->page_dir->dir[pdi], pti, false) != 0){
						core_set_free(index);
						continue;
					}

					// TODO TLB shootdown this proc's stuff

					// TODO check if this is a symbolic page?
					as->page_dir->dir[pdi]->table[pti].present = 0;
					as->page_dir->dir[pdi]->table[pti].ppn = coremap.cm[index].swap;
					coremap.cm[index].swap = 0;

					// TODO Zero page here

					page_set_free(as->page_dir->dir[pdi], pti);

					coremap.cm[index].slen = 1;
					coremap.cm[index].vpn = vpn;
					coremap.cm[index].pid = (is_kern) ? 0 : curproc->pid;

					return CMI_TO_PADDR(index);

				}else if(round >= 2){
					kprintf("evicting dirty page\n");
					//Write dirty page to disk and then evict
					// TODO Somehow get address space/ page dir from coremap.cm[index].pid
					struct addrspace *as;
					int pdi = PDI(coremap.cm[index].vpn);
					int pti = PTI(coremap.cm[index].vpn);

					// Give up here to avoid deadlock
					if(page_set_busy(as->page_dir->dir[pdi], pti, false) != 0){
						core_set_free(index);
						continue;
					}

					// TODO TLB shootdown this proc's stuff

					as->page_dir->dir[pdi]->table[pti].present = 0;
					as->page_dir->dir[pdi]->table[pti].ppn = write_to_disk(CMI_TO_PADDR(index));

					// TODO Zero page here

					page_set_free(as->page_dir->dir[pdi], pti);

					coremap.cm[index].dirty = 0;
					coremap.cm[index].slen = 1;
					coremap.cm[index].vpn = vpn;
					coremap.cm[index].pid = (is_kern) ? 0 : curproc->pid;

					return CMI_TO_PADDR(index);
				}

				core_set_free(index);
			}
		}
	}

    return 0;   /* in this case busybit is unset */
}

static
paddr_t
get_kpage_seq(unsigned npages) {

    paddr_t pa, next_pa;

    pa = get_free_cme((vaddr_t)0, KERNEL_CMI);
    if (pa == 0) return 0;
    core_set_free(PADDR_TO_CMI(pa));
    coremap.cm[PADDR_TO_CMI(pa)].slen = npages;
    coremap.cm[PADDR_TO_CMI(pa)].seq = 0;
    unsigned count = 1; /* initial count set to 1, because we got one cme */
    // TODO change to garuntee continuity; possible livelock if we have fewer
    // free pages than npages that we need we'll loop forever; but now we never
    // call this function
    while (count != npages) {
        next_pa = get_free_cme((vaddr_t)0, KERNEL_CMI);
        if (next_pa == 0) {    /* out of free pages */
            free_kpages(PADDR_TO_KVADDR(pa));
            return 0;
        } else if (next_pa == pa + PAGE_SIZE) { /* hit for the next seq page */
        	core_set_free(PADDR_TO_CMI(next_pa));
            coremap.cm[PADDR_TO_CMI(next_pa)].seq = 1;
            count++;
        } else {                /* not contigious */
        	core_set_free(PADDR_TO_CMI(next_pa));
            free_kpages(PADDR_TO_KVADDR(pa));   /* free initial guess */
            pa = next_pa;                       /* set next_pa to guess */
            coremap.cm[PADDR_TO_CMI(pa)].slen = npages;    /* set the length */
            coremap.cm[PADDR_TO_CMI(pa)].seq = 0; /* first page in seq for free */
            count = 1;                          /* we have the first page */
        }
    }

    return pa;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages)
{
	paddr_t pa = get_kpage_seq(npages);
	if (pa == 0) return 0;
	return PADDR_TO_KVADDR(pa);
}

static
void
wait_for_busy(int index) {
    while(coremap.cm[index].busybit == 1){
        spinlock_release(&coremap.lock);
        thread_yield();
        spinlock_acquire(&coremap.lock);
    }
}

// We give the option to retry but use with care as it could lead to livelock
int core_set_busy(int index, bool wait) {
	spinlock_acquire(&coremap.lock);
	if(coremap.cm[index].busybit == 0) {
        set_busy_bit(index, 1);
		spinlock_release(&coremap.lock);
	}else if(wait){
		// At this point busy wait for the bit to be open by sleeping till it's available
        wait_for_busy(index);
        set_busy_bit(index, 1);
		spinlock_release(&coremap.lock);
	}else{
		spinlock_release(&coremap.lock);
		return 1;
	}
    return 0;
}

int core_set_free(int index){
	spinlock_acquire(&coremap.lock);
	if(coremap.cm[index].busybit == 1) {
        set_busy_bit(index, 0);
		spinlock_release(&coremap.lock);
		return 0;
	} else {    /* already set free */
        panic("busybit is already unset\n");
	}
}

static
void
kfree_one_page(unsigned cm_index) {
	core_set_busy(cm_index, WAIT);
	if (coremap.cm[cm_index].use == 0 )
		panic("free_kpages: freeing a free page\n");
	if (coremap.cm[cm_index].kern != 1)
		panic("free_kpages: freeing not a kernel's page\n");

	KASSERT(coremap.cm[cm_index].pid == 0);
	KASSERT(coremap.cm[cm_index].swap == 0);
	KASSERT(coremap.cm[cm_index].vpn == 0);

    set_kern_bit(cm_index, 0);
    set_use_bit(cm_index, 0);
	coremap.cm[cm_index].slen = 0;
	coremap.cm[cm_index].seq = 0;
	coremap.cm[cm_index].junk = 0;
	coremap.cm[cm_index].ref = 0;
	coremap.cm[cm_index].dirty = 0;

	/* zero out physical page */
	memset((void *)PADDR_TO_KVADDR(CMI_TO_PADDR(cm_index)), 0, PAGE_SIZE);

	core_set_free(cm_index);
}

void
free_kpages(vaddr_t addr)
{
    paddr_t pa = KVADDR_TO_PADDR(addr);
    KASSERT (pa % PAGE_SIZE == 0);  /* don't believe s/w you didn't wrote */
    unsigned cm_index = PADDR_TO_CMI(pa);

    // This is okay because we never hold a page here
	core_set_busy(cm_index, WAIT);
    unsigned slen = coremap.cm[cm_index].slen;
    /* check that we're given the page returned by kalloc_pages */
    KASSERT(coremap.cm[cm_index].seq == 0);
    core_set_free(cm_index);

    for (unsigned i = 0; i < slen; i++) /* can be reimplemeted using only seq bit */
        kfree_one_page(cm_index + i);
}

/* shoot down all TLB entries */
void
vm_tlbshootdown_all(void)
{
    int spl = splhigh();

    for (int entryno = 0; entryno < NUM_TLB; entryno++)
        tlb_write(TLBHI_INVALID(entryno), TLBLO_INVALID(), entryno);

    splx(spl);
}

/* shoot down one TLB entry given the information from ts */
void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    int spl = splhigh();

    int cmi = PADDR_TO_CMI(ts->ppn);
    if (coremap.cm[cmi].use == 0) goto done;
    uint32_t vpn = (cmi << 12) & TLBHI_VPAGE;
    int rv = tlb_probe(vpn, 0);
    if (rv >= 0)
        tlb_write(TLBHI_INVALID(rv), TLBLO_INVALID(), rv);

done:
    V(ts->tlb_sem); /* don't know where we should P() */
    splx(spl);
}

// Returns with the page table index locked, and a valid ppn that is in memory
static int validate_vaddr(vaddr_t vaddr, struct page_table *pt, int pti){
	page_set_busy(pt, pti, true);
	if(pt->table[pti].valid != 1)
		return -1;

    // Page exists but is not allocated
	if(pt->table[pti].present==1 && pt->table[pti].ppn == 0)
		pt->table[pti].ppn = get_free_cme(vaddr, false);
	else if(pt->table[pti].present==0)
		pt->table[pti].ppn = retrieve_from_disk(pt->table[pti].ppn, vaddr);
	else
		return 0;

	if(pt->table[pti].ppn == 0)
		return -1;

	// We can free this because any eviction has to get the page table busy bit which is already set
	core_set_free(PADDR_TO_CMI(pt->table[pti].ppn));

	return 0;
}

static
void
update_tlb(paddr_t pa, vaddr_t va, bool dirty, bool read_only) {
    uint32_t elo = (pa & TLBLO_PPAGE) | TLBLO_VALID;
    if (dirty) elo |= TLBLO_DIRTY;

    uint32_t ehi = va & TLBHI_VPAGE;
    va &= PAGE_FRAME;
    int spl = splhigh();

    if (read_only) {
        int tlbi = tlb_probe(va, 0);
        (tlbi >= 0) ? tlb_write(ehi, elo, tlbi) : tlb_random(ehi, elo);
    } else
        tlb_random(ehi, elo);

    splx(spl);
}

static int tlb_miss_on_load(vaddr_t vaddr){
	// TODO do I check permissions here?
	struct page_table *pt = curproc->p_addrspace->page_dir->dir[PDI(vaddr)];
	int pti = PTI(vaddr);
	if(validate_vaddr(vaddr, pt, pti) != 0)
		return -1;
    paddr_t paddr = pt->table[pti].ppn;
    update_tlb(paddr, vaddr, false, false);

	// Page is now allocated at ppn and pt->pti is locked so it is safe from eviction
	// TODO put pt->table[pti].ppn into the TLB
	page_set_free(pt, pti);
	return 0;
}

static int tbl_miss_on_store(vaddr_t vaddr){
	// TODO do I check permissions here?
	struct page_table *pt = curproc->p_addrspace->page_dir->dir[PDI(vaddr)];
	int pti = PTI(vaddr);
	if(validate_vaddr(vaddr, pt, pti) != 0)
		return -1;

    paddr_t paddr = pt->table[pti].ppn;
    update_tlb(paddr, vaddr, true, false);

	// Page is now allocated at ppn and pt->pti is locked so it is safe from eviction
	// TODO put pt->table[pti].ppn into the TLB
	page_set_free(pt, pti);
	return 0;
}

static int tlb_fault_readonly(vaddr_t vaddr){
	struct page_table *pt = curproc->p_addrspace->page_dir->dir[PDI(vaddr)];
	int pti = PTI(vaddr);
	if(validate_vaddr(vaddr, pt, pti) != 0) return -1;

    paddr_t paddr = pt->table[pti].ppn;
    update_tlb(paddr, vaddr, true, true);

	// TODO do i even need to get the coremap lock here to set the dirty bit?
	int cmi = PADDR_TO_CMI(pt->table[pti].ppn);
	core_set_busy(cmi, true);
	coremap.cm[cmi].dirty = 1;
	core_set_free(cmi);

	// TODO is it already in the TLB at this point? do I just have to change permissions?
	// TODO should I free page before I get the coremap lock
	page_set_free(pt, pti);
	return 0;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    KASSERT(faultaddress != 0);
    KASSERT(faultaddress < MIPS_KSEG0);

    switch(faulttype) {
        case VM_FAULT_READONLY:
        	tlb_fault_readonly(faultaddress);
            break;

        case VM_FAULT_READ:
        	tlb_miss_on_load(faultaddress);
            break;

        case VM_FAULT_WRITE:
        	tbl_miss_on_store(faultaddress);
            break;

        default: panic ("bad faulttype\n");
    }

    return 0;
}

/*
static
paddr_t
get_kpage_seq(unsigned npages) {
	// This globally locks to find kernel pages as it simplifies the process, and this function is used sparingly
	// so it wont cause great slowdown
	spinlock_acquire(&coremap.lock);

	int index = coremap.last_allocated;
	unsigned alloced = 0;
	for(unsigned i = 0; i<(2*coremap.size) && alloced<npages; i++){
		index = (index+1) % coremap.size;
		if(coremap.cm[index].busybit == 1 || coremap.cm[index].kern == 1){
			alloced = 0;
			continue;
		}else if (coremap.cm[index].use == 1){
			//Only evict on second run through coremap
			if(i>=coremap.size){
				// TODO evict and set as not in use
			}else{
				alloced = 0;
				continue;
			}
		}

		// We are garunteed a clean cme here
		coremap.cm[index].use = 1;
		coremap.cm[index].vpn = 0;
		coremap.cm[index].pid = 0;
		coremap.cm[index].kern = 1;
		alloced++;
	}

	if(alloced == npages){
		coremap.last_allocated = index;
		spinlock_release(&coremap.lock);
		return CMI_TO_PADDR(index-alloced+1);
	}else{
		spinlock_release(&coremap.lock);
		return 0;
	}
}
*/
