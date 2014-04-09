#include <types.h>
#include <spinlock.h>
#include <vm.h>
#include <lib.h>
#include <coremap.h>

#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <kern/errno.h>
#include <spl.h>
#include <addrspace.h>

int stat_coremap(int nargs, char **args) {
    (void)nargs;
    (void)args;
	spinlock_acquire(&coremap.lock);
    kprintf("coremap.kernel: %d\n"
            "coremap.used: %d\n"
            "coremap.size: %d\n"
            "coremap.busy: %d\n",
            coremap.kernel, coremap.used, coremap.size, coremap.busy);
	spinlock_release(&coremap.lock);
    return 0;
}

/* must be called with acquired spinlock */
static
void
set_use_bit(int index, int bitvalue) {
    coremap.cm[index].use = bitvalue;
    (bitvalue) ? coremap.used++ : coremap.used--;
}

/* must be called with acquired spinlock */
static
void
set_busy_bit(int index, int bitvalue) {
    coremap.cm[index].busybit = bitvalue;
    (bitvalue) ? coremap.busy++ : coremap.busy--;
}

/* must be called with acquired spinlock */
static
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
        (ROUNDUP(total_pages * sizeof(struct cme), PAGE_SIZE) / PAGE_SIZE) + stolen_pages;

    for (unsigned i = 0; i < alloc_pages; i++) {
        /*
        coremap.cm[i].kern = 1;
        coremap.cm[i].use = 1;
        */
        set_kern_bit(i, 1);
        set_use_bit(i, 1);
    }
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

	for(unsigned i = 0; i < coremap.size; i++){
		index = (index+1) % coremap.size;
		if(core_set_busy(index, true) == 0){
			// Check if in use
			if (coremap.cm[index].use == 0) {
                set_use_bit(index, 1);
				//coremap.cm[index].use = 1;
                set_kern_bit(index, is_kern);
                //coremap.cm[index].kern = (is_kern) ? 1 : 0;

				coremap.cm[index].vpn = vpn;
				coremap.cm[index].pid = (is_kern) ? 0 : curproc->pid;

                spinlock_acquire(&coremap.lock);
                coremap.last_allocated = index;
                spinlock_release(&coremap.lock);

				return CMI_TO_PADDR(index);
			}
			core_set_free(index);
			// TODO add eviction later
		}
	}

    return 0;   /* in this case busybit is unset */
}

static
paddr_t
get_cme_seq(unsigned npages) {

    paddr_t pa, next_pa;

    pa = get_free_cme((vaddr_t)0, true);
    if (pa == 0) return 0;
    coremap.cm[PADDR_TO_CMI(pa)].slen = npages;
    coremap.cm[PADDR_TO_CMI(pa)].seq = 0;
    unsigned count = 1; /* initial count set to 1, because we got one cme */
    core_set_free(PADDR_TO_CMI(pa));
    // TODO change to garuntee continuity; possible livelock if we have fewer
    // free pages than npages that we need we'll loop forever; but now we never
    // call this function
    while (count != npages) {
        next_pa = get_free_cme((vaddr_t)0, true);
        if (next_pa == 0) {    /* out of free pages */
            free_kpages(PADDR_TO_KVADDR(pa));
            return 0;
        } else if (next_pa == pa + PAGE_SIZE) { /* hit */
        	core_set_free(PADDR_TO_CMI(next_pa));
            coremap.cm[PADDR_TO_CMI(next_pa)].seq = 1;
            count++;
        } else {                /* not contigious */
        	core_set_free(PADDR_TO_CMI(next_pa));
            free_kpages(PADDR_TO_KVADDR(pa));   /* free initial guess */
            pa = next_pa;                       /* set next_pa to guess */
            coremap.cm[PADDR_TO_CMI(pa)].slen = npages;    /* set the length */
            coremap.cm[PADDR_TO_CMI(pa)].seq = 0;   /* first page in seq for assert when freeing */
            count = 1;                          /* we have the first page */
        }
    }

    return pa;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages)
{
	paddr_t pa;
	// We don't need complex logic to alloc a single page
    /* this resulted in a bug of never freeing things */
    /*
	if(npages==1){
		pa = get_free_cme((vaddr_t)0, true);
		core_set_free(PADDR_TO_CMI(pa));
	}else
    */
    pa = get_cme_seq(npages);
	if (pa == 0) return 0;

	return PADDR_TO_KVADDR(pa);
}
static
void
wait_for_busy(int index) {
    while(coremap.cm[index].busybit == 1){
        // TODO Ask david if this is ok
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
		//coremap.cm[index].busybit = 1;
		spinlock_release(&coremap.lock);
	}else if(wait){
		// At this point busy wait for the bit to be open by sleeping till it's available
        wait_for_busy(index);
        set_busy_bit(index, 1);
		//coremap.cm[index].busybit = 1;
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
		//coremap.cm[index].busybit = 0;
		spinlock_release(&coremap.lock);
		return 0;
	} else {
		spinlock_release(&coremap.lock);
		return 1;
	}
}

static
void
kfree_one_page(unsigned cm_index) {
	core_set_busy(cm_index, true);
	if (coremap.cm[cm_index].use == 0 )
		panic("free_kpages: freeing a free page\n");
	if (coremap.cm[cm_index].kern != 1)
		panic("free_kpages: freeing not a kernel's page\n");

	KASSERT(coremap.cm[cm_index].pid == 0);
	KASSERT(coremap.cm[cm_index].swap == 0);
	KASSERT(coremap.cm[cm_index].vpn == 0);

	coremap.cm[cm_index].dirty = 0;
    set_kern_bit(cm_index, 0);
    set_use_bit(cm_index, 0);
	//coremap.cm[cm_index].kern = 0;
	//coremap.cm[cm_index].use = 0;
	coremap.cm[cm_index].slen = 0;
	coremap.cm[cm_index].seq = 0;
	coremap.cm[cm_index].junk = 0;
	coremap.cm[cm_index].ref = 0;

	/* zero out physical page */
	memset((void *)PADDR_TO_KVADDR(CMI_TO_PADDR(cm_index)), 0, PAGE_SIZE);

	int rv = core_set_free(cm_index);
    (void)rv; //TODO: check for return value, passibly useful
}

void
free_kpages(vaddr_t addr)
{
    paddr_t pa = KVADDR_TO_PADDR(addr);
    KASSERT (pa % PAGE_SIZE == 0);  /* don't believe s/w you didn't wrote */
    unsigned cm_index = PADDR_TO_CMI(pa);

    // This is okay because we never hold a page here
	core_set_busy(cm_index, true);
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
	(void)ts;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    (void)faulttype;
    (void)faultaddress;
    return 0;
}

/*
static
paddr_t
get_kern_cme_seq(unsigned npages) {
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
