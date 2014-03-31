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
#define DUMBVM_STACKPAGES    18

static paddr_t get_free_cme(vaddr_t vpn, bool kern);

struct cme {
    uint32_t vpn:       20,
             pid:       9,
             busybit:   1,
             use:       1,
             kern:      1;
    uint32_t swap:      15,
             dirty:     1,
             ref:       1,
             junk:      15;
};

struct coremap {
    struct spinlock lock;
    unsigned free;
    unsigned modified;

    /* additional statics Daniel suggested to track, I'll init them all tonight
    unsigned kernel;
    unsigned user;
    unsigned busy;
    unsigned swap;
    unsigned ref;
    */
    unsigned size;
    struct cme *cm;
    int last_allocated;
} coremap;

void cm_bootstrap(void) {
    paddr_t lo;
    paddr_t hi;

    ram_getsize(&lo, &hi);
    uint32_t free_pages = (hi - lo) / PAGE_SIZE;    /* available aka not stolen */
    uint32_t total_pages = free_pages + lo / PAGE_SIZE; /* available + stolen */

    spinlock_init(&coremap.lock);
    coremap.free = free_pages;
    coremap.size = total_pages;
    coremap.modified = 0;
    coremap.cm = (struct cme *)PADDR_TO_KVADDR(lo);

    uint32_t alloc_pages =  /* npages cm occupies + stolen_pages */
        (ROUNDUP(total_pages * sizeof(struct cme), PAGE_SIZE) / PAGE_SIZE) + stolen_pages;

    for (unsigned i = 0; i < alloc_pages; i++) {
        coremap.cm[i].kern = 1;
        coremap.cm[i].use = 1;
    }
}


void
vm_bootstrap(void)
{
    cm_bootstrap();
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages)
{
    if (npages > 1) return 0;   /* for now max 1 page */

	paddr_t pa = get_free_cme((vaddr_t)0, true);
	if (pa == 0) return 0;
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */

	(void)addr;
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}

// We don't give the option to retry as that would
// involve sleeping which could lead to livelock
static int core_set_busy(int index) {
	spinlock_acquire(&coremap.lock);
	if(coremap.cm[index].busybit == 0){
		coremap.cm[index].busybit = 1;
		spinlock_release(&coremap.lock);
	}else{
		spinlock_release(&coremap.lock);
		return 1;
	}
    return 0;
}


static int core_set_free(int index){
	spinlock_acquire(&coremap.lock);
	if(coremap.cm[index].busybit == 1){
		coremap.cm[index].busybit = 0;
		spinlock_release(&coremap.lock);
		return 0;
	}else{
		spinlock_release(&coremap.lock);
		return 1;
	}
}

static paddr_t get_free_cme(vaddr_t vpn, bool is_kern) {

	spinlock_acquire(&coremap.lock);
	int index = coremap.last_allocated;
	spinlock_release(&coremap.lock);

	for(unsigned i = 0; i < coremap.size; i++){
		index = (index+1) % coremap.size;
		if(core_set_busy(index) == 0){
			// Check if in use
			if (coremap.cm[index].use == 0) {
				coremap.cm[index].use = 1;
				coremap.cm[index].vpn = (is_kern) ? 0 : vpn;
				coremap.cm[index].pid = (is_kern) ? 0 : curproc->pid;
                coremap.cm[index].kern = (is_kern) ? 1 : 0;
				core_set_free(index);
				// TODO possibly zero page here.
				// Multiply by page size to get paddr
				return index * PAGE_SIZE;
			}
			// TODO add eviction later
			core_set_free(index);
		}
	}

    return 0;
}

