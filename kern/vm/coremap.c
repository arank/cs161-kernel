#include <types.h>
#include <spinlock.h>
#include <vm.h>
#include <lib.h>
#include <coremap.h>
#include <pagetable.h>
#include <pid_table.h>
#include <synch.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <mips/vm.h>
#include <kern/errno.h>
#include <spl.h>
#include <addrspace.h>
#include <backingstore.h>
#include <cleaning_deamon.h>
#include <cpu.h>

extern char _end;

int stat_coremap(int nargs, char **args) {
    (void)nargs;
    (void)args;

    kprintf("coremap.kernel: %d\n"
            "coremap.used: %d\n"
            "coremap.size: %d\n"
            "coremap.busy: %d\n"
            "coremap.last_alloc: %d\n",
            coremap.kernel, coremap.used, coremap.size, coremap.busy,
            coremap.last_allocated);

    return 0;
}

/* must be called with acquired spinlock */
// TODO can we encapsulate getting and releasing the spinlock in these functions
void
set_use_bit(int index, int bitvalue) {
    //if (coremap.cm[index].use == 1)
        //kprintf("index %d set free\n", index);
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

void
set_dirty_bit(int index, int bitvalue) {
    coremap.cm[index].dirty = bitvalue;
    (bitvalue) ? coremap.modified++ : coremap.modified--;
    // Signal deamon
    // TODO is it safe to do this in here
//    if(bitvalue && ((coremap.modified*4)>((coremap.used - coremap.kernel)*3))){
//    	lock_acquire(deamon.lock);
//    	cv_signal(deamon.cv, deamon.lock);
//    	lock_release(deamon.lock);
//    }

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
vm_bootstrap(void) {
    cm_bootstrap();
    kprintf("%c\n", _end);
}

// Given a locked non-kern dirty cme, it cleans it to disk
int clean_cme(int index){
	KASSERT(coremap.cm[index].pid!=0);
	KASSERT(coremap.cm[index].kern != 1);
	KASSERT(coremap.cm[index].dirty == 1);
	KASSERT(coremap.cm[index].busybit == 1);

	struct addrspace *as = get_proc(coremap.cm[index].pid)->p_addrspace;
	int pdi = VPN_PDI(coremap.cm[index].vpn);
	int pti = VPN_PTI(coremap.cm[index].vpn);

	// Give up here to avoid deadlock
	// TODO must I lock here?
	if(page_set_busy(as->page_dir->dir[pdi], pti, false) != 0)
		return -1;

	flush_ppn(index);

    coremap.cm[index].swap = write_to_disk(CMI_TO_PADDR(index), (int)coremap.cm[index].swap);

	spinlock_acquire(&coremap.lock);
	set_dirty_bit(index, 0);
	spinlock_release(&coremap.lock);

	page_set_free(as->page_dir->dir[pdi], pti);

	return 0;
}

// Given a locked non-kern cme it forcibly evicts it
static int evict_cme(int index){
	KASSERT(coremap.cm[index].pid != 0);
	KASSERT(coremap.cm[index].kern != 1);
	KASSERT(coremap.cm[index].busybit == 1);

	struct addrspace *as = get_proc(coremap.cm[index].pid)->p_addrspace;
	int pdi = VPN_PDI(coremap.cm[index].vpn);
	int pti = VPN_PTI(coremap.cm[index].vpn);

	// Give up here to avoid deadlock
	if(page_set_busy(as->page_dir->dir[pdi], pti, false) != 0)
		return -1;

	flush_ppn(index);

	// Page is clean
	if (coremap.cm[index].dirty == 0) {
		// Reset swap to either 0 if symbolic or the dedicated swap addr if it is swapped
		as->page_dir->dir[pdi]->table[pti].ppn = coremap.cm[index].swap;

        as->page_dir->dir[pdi]->table[pti].present =
            (as->page_dir->dir[pdi]->table[pti].ppn == 0) ? 1 : 0;

	} else {
		// Evict all data to dedicated disk swap space, or assign new swap space and evict to there
        coremap.cm[index].swap = write_to_disk(CMI_TO_PADDR(index), (int)coremap.cm[index].swap);
        as->page_dir->dir[pdi]->table[pti].ppn = coremap.cm[index].swap;

        KASSERT(as->page_dir->dir[pdi]->table[pti].ppn != 0);
		as->page_dir->dir[pdi]->table[pti].present = 0;
	}

	page_set_free(as->page_dir->dir[pdi], pti);

	return 0;
}

// Updates cme to clean and new
static void update_cme(int index, vaddr_t vaddr, bool is_kern){
	coremap.cm[index].age = 0;
	coremap.cm[index].swap = 0;
	coremap.cm[index].slen = 1;
	coremap.cm[index].vpn = vaddr >> 12;
	coremap.cm[index].pid = (is_kern) ? 0 : curproc->pid;
	spinlock_acquire(&coremap.lock);
	if (coremap.cm[index].dirty==1) set_dirty_bit(index, 0);
	if (is_kern) set_kern_bit(index, 1);
	coremap.last_allocated = index;
    //kprintf("cme: %zu (%s) vaddr: %x\n", index, (is_kern) ? "kern" : "user", vaddr);
    set_use_bit(index, 1);
	spinlock_release(&coremap.lock);
}

// Returns with busy bit set on the entry, on fail it returns 0
paddr_t
get_free_cme(vaddr_t vaddr, bool is_kern) {
    if (is_kern == false && vaddr == 0)
        panic ("kern is false, vpn is 0\n");

	spinlock_acquire(&coremap.lock);
	int index = coremap.last_allocated;
	spinlock_release(&coremap.lock);

    while (1) {

    unsigned evictable = 0;

	for(unsigned round = 0; round < 3; round++){
		for(unsigned i = 0; i < coremap.size; i++){
			index = (index+1) % coremap.size;
			if (core_set_busy(index, NO_WAIT) == 0){
				if(coremap.cm[index].kern == 1) { // Free core if kernel
					core_set_free(index);
					continue;
				}
                evictable++;
				if (coremap.cm[index].use == 0) { // Check if in use
					//set_use_bit(index, 1); set it in update_cme
					goto out;
				} else {
                    if (round == 0) {
                        core_set_free(index);
                        continue;
                    }
                    coremap.cm[index].age++;

                    if (round >= 1 && coremap.cm[index].dirty == 0
                        && coremap.cm[index].age < CLEAN_AGE_THRESHOLD) {
                    //coremap.cm[index].age++;
						core_set_free(index);
                        continue;
                    }

                    if (round >= 2 && coremap.cm[index].age < DIRTY_AGE_THRESHOLD) {
                        core_set_free(index);
                        continue;
                    }

                    if (evict_cme(index) != 0) { // Steal cleaned page and evict
                        core_set_free(index);
                        continue;
                    }
                    goto out;
                }

				core_set_free(index);
			}

		}
	}
    if(evictable == 0) break;
    }

    kprintf("all pages in use by the kernel\n");
    return 0;   /* in this case busybit is unset */

out:
	memset((void *)PADDR_TO_KVADDR(CMI_TO_PADDR(index)), 0, PAGE_SIZE);
	update_cme(index, vaddr, is_kern);
    KASSERT(coremap.cm[index].busybit == 1);
	return CMI_TO_PADDR(index);

}


// TODO re-write this
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
    // call this loop yet
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
alloc_kpages(int npages) {
	paddr_t pa = get_kpage_seq(npages);
	if (pa == 0) return 0;
	//memset((void *)PADDR_TO_KVADDR(pa), 0, PAGE_SIZE * npages);
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
	if(coremap.cm[index].busybit == 0) {
        stat_coremap(0, NULL);
		panic("busybit is already unset: %d\n", index);
    }
	set_busy_bit(index, 0);
	spinlock_release(&coremap.lock);
	return 0;
}

static
void
kfree_one_page(unsigned cm_index) {
    if (cm_index <= 70) panic ("freeing kernel code page");

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
	coremap.cm[cm_index].age = 0;
	coremap.cm[cm_index].dirty = 0;

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

    int cmi = ts->ppn;
    if (coremap.cm[cmi].use == 0) goto done;
    KASSERT(coremap.cm[cmi].busybit == 1);
    uint32_t ehi = (coremap.cm[cmi].vpn << 12) & TLBHI_VPAGE;
    int entry = tlb_probe(ehi, 0);
    if (entry >= 0)
        tlb_write(TLBHI_INVALID(entry), TLBLO_INVALID(), entry);

done:
    V(ts->tlb_sem); /* don't know where we should P() */
    splx(spl);
}

// Returns with the page table index locked, and a valid ppn that is in memory
static int validate_vaddr(vaddr_t vaddr, struct page_table *pt, int pti){
	page_set_busy(pt, pti, true);
	if (pt->table[pti].valid != 1) return EFAULT;

	// Check for un-initalized paddr
	KASSERT(pt->table[pti].present != 0 || pt->table[pti].ppn != 0);

    // Page exists but is not allocated
	if (pt->table[pti].present == 1 && pt->table[pti].ppn == 0) {

		pt->table[pti].ppn = PADDR_TO_CMI(get_free_cme(vaddr, USER_CMI));
        if (pt->table[pti].ppn == 0) return ENOMEM;

        KASSERT(coremap.cm[pt->table[pti].ppn].kern == 0);
        KASSERT(coremap.cm[pt->table[pti].ppn].pid != 0);

        coremap.cm[pt->table[pti].ppn].age = 0;
        core_set_free(pt->table[pti].ppn);

    // Page on disk
	} else if(pt->table[pti].present == 0 && pt->table[pti].ppn > 0) {

		pt->table[pti].ppn = PADDR_TO_CMI(retrieve_from_disk(pt->table[pti].ppn, vaddr));
		if(pt->table[pti].ppn == 0) return ENOMEM;

        KASSERT(coremap.cm[pt->table[pti].ppn].kern == 0);
        KASSERT(coremap.cm[pt->table[pti].ppn].pid != 0);

		pt->table[pti].present = 1;
        coremap.cm[pt->table[pti].ppn].age = 0;

        core_set_free(pt->table[pti].ppn);
	}

	KASSERT(pt->table[pti].busybit == 1);
	// Page is now in memory and no validation is nessecary
	return 0;
}

static
void
update_tlb(uint32_t ppn, vaddr_t va, bool modified, bool read_only_fault) {
    if (ppn < 70) panic ("ppn is too low\n");

    uint32_t ehi = va & TLBHI_VPAGE;

    // We store the PPN (NUMBER!!!) in TLB, not Physical Address
    uint32_t elo = ((ppn << 12) & TLBLO_PPAGE) | TLBLO_VALID;
    if (modified) elo |= TLBLO_DIRTY;

    va &= PAGE_FRAME;

    int spl = splhigh();

    if (read_only_fault) {
        int tlbi = tlb_probe(va, 0);
        (tlbi >= 0) ? tlb_write(ehi, elo, tlbi) : tlb_random(ehi, elo);
    } else
        tlb_random(ehi, elo);

    splx(spl);
}

static
int
tlb_miss_on_load(vaddr_t vaddr, struct page_table *pt){
	int pti = PTI(vaddr);
    KASSERT(pti >= 0 && pti < PT_SIZE);

	if (validate_vaddr(vaddr, pt, pti) != 0) return EFAULT;

    KASSERT(pt->table[pti].present == 1);
    update_tlb(pt->table[pti].ppn, vaddr, false, false);

	page_set_free(pt, pti);
	return 0;
}

static
int
tlb_miss_on_store(vaddr_t vaddr, struct page_table *pt){
	int pti = PTI(vaddr);
    KASSERT(pti >= 0 && pti < PT_SIZE);

	if (validate_vaddr(vaddr, pt, pti) != 0) return EFAULT;

    unsigned pid = coremap.cm[pt->table[pti].ppn].pid;
    KASSERT(pid != 0);
    KASSERT(coremap.cm[pt->table[pti].ppn].kern == 0);

    struct addrspace *as = get_proc(pid)->p_addrspace;
    if (pt->table[pti].write == 0 && !as->loading) return EFAULT;

	core_set_busy(pt->table[pti].ppn, WAIT);
    set_dirty_bit(pt->table[pti].ppn, 1);
	core_set_free(pt->table[pti].ppn);

    KASSERT(pt->table[pti].present == 1);
    update_tlb(pt->table[pti].ppn, vaddr, true, false);

	page_set_free(pt, pti);
	return 0;
}

static int tlb_fault_readonly(vaddr_t vaddr, struct page_table *pt){
	int pti = PTI(vaddr);
    KASSERT(pti >= 0 && pti < PT_SIZE);

    uint32_t cmi = pt->table[pti].ppn;
    unsigned pid = coremap.cm[cmi].pid;
    struct addrspace *as = get_proc(pid)->p_addrspace;
    if (pt->table[pti].write == 0 && !as->loading) return EFAULT;

	core_set_busy(cmi, WAIT);
    set_dirty_bit(cmi, 1);
	core_set_free(cmi);

    KASSERT(pt->table[pti].present == 1);
    update_tlb(pt->table[pti].ppn, vaddr, true, true);
    coremap.cm[pt->table[pti].ppn].age = 0;

	return 0;
}

static
bool
is_valid_addr(vaddr_t faultaddr, struct addrspace *as) {
    if (faultaddr >= USERSTACK - (STACK_PAGES * PAGE_SIZE)) goto done;
    if (faultaddr >= TEXT_START && faultaddr <= as->heap_end) goto done;
    return false;

done:
    return true;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    if (faultaddress > MIPS_KSEG0 || faultaddress < TEXT_START) return EFAULT;

    if (!is_valid_addr(faultaddress, curproc->p_addrspace)) return EFAULT;

    uint32_t pdi = PDI(faultaddress);
    KASSERT(pdi > 0 && pdi < PD_SIZE);

	struct page_table *pt = curproc->p_addrspace->page_dir->dir[pdi];

    switch(faulttype) {
        case VM_FAULT_READONLY:
        	return tlb_fault_readonly(faultaddress, pt);

        case VM_FAULT_READ:
        	return tlb_miss_on_load(faultaddress, pt);

        case VM_FAULT_WRITE:
            return tlb_miss_on_store(faultaddress, pt);

        default: panic ("bad faulttype\n");
    }

    return -1;  /* should never get here */
}

