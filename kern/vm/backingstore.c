#include <types.h>
#include <synch.h>
#include <mips/vm.h>
#include <lib.h>
#include <bitmap.h>
#include <coremap.h>
#include <kern/errno.h>

struct backing_store{
	struct lock *lock;
	struct bitmap* bm;
	paddr_t swap;
} *backing_store;

int init_backing_store(void) {

    backing_store = kmalloc(sizeof *backing_store);
    if (backing_store == NULL) goto out;

    // Create dedicated swap space to temporarily pull data into before flushing to evictable user page
    backing_store->swap = get_free_cme((vaddr_t)0, true);
    if (backing_store->swap==0) goto out;

    //TODO figure this out currently this bitmap size is the max our coremap and page table supports
    backing_store->bm = bitmap_create((unsigned)33554432);
    if (backing_store->bm == NULL) goto bm_out;

    backing_store->lock = lock_create("disk_lock");
    if (backing_store->lock == NULL) goto lk_out;

    //Set 0 to in use, as that is reserved for clean pages
    bitmap_mark(backing_store->bm, 0);

    return 0;

lk_out:
    bitmap_destroy(backing_store->bm);
bm_out:
    kfree(backing_store);
out:
    return 1;
}



// This assumes that the location has been set as busy by get free cme
// returns with lock set on swap_addr's cme
int retrieve_from_disk(int swap_index, vaddr_t swap_into){
	lock_acquire(backing_store->lock);
	if(!bitmap_isset(backing_store->bm, swap_index)){
		lock_release(backing_store->lock);
		return 0;
	}
    core_set_busy(PADDR_TO_CMI(backing_store->swap), true);
	// TODO figure out how to retrieve from index and write into backing_store->swap
	bitmap_unmark(backing_store->bm, swap_index);
    lock_release(backing_store->lock);
    paddr_t swap_addr = get_free_cme(swap_into, false);
    if(swap_addr == 0){
    	core_set_free(PADDR_TO_CMI(backing_store->swap));
    	return 0;
    }
    memcpy((void*)swap_addr, (void*)backing_store->swap, PAGE_SIZE);
    core_set_free(PADDR_TO_CMI(backing_store->swap));
    return swap_addr;
}

// Assumes that cme for location is already locked
int write_to_disk(paddr_t location){
	KASSERT(coremap.cm[PADDR_TO_CMI(location)].busybit == 1);
	lock_acquire(backing_store->lock);
	unsigned spot;
	if(bitmap_alloc(backing_store->bm, &spot) == ENOSPC){
	    	lock_release(backing_store->lock);
	    	return -1;
	}
	// TODO Figure out how to put the page from location into spot
	(void) location;
	lock_release(backing_store->lock);
	return 0;
}
