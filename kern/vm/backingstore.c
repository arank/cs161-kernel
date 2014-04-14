#include <types.h>
#include <synch.h>
#include <mips/vm.h>
#include <lib.h>
#include <bitmap.h>
#include <coremap.h>
#include <kern/errno.h>
#include <uio.h>
#include <kern/iovec.h>
#include <vnode.h>
#include <kern/fcntl.h>
#include <backingstore.h>
#include <vfs.h>
#include <current.h>
#include <proc.h>

static struct vnode *bs;
extern struct semaphore *tlb_sem;

int init_backing_store(void) {

    if(vfs_open(kstrdup("lhd0raw:"), O_RDWR, 0, &bs) != 0)
        panic ("vfs_open failed\n");

    backing_store = kmalloc(sizeof *backing_store);
    if (backing_store == NULL) goto out;

    // Create dedicated swap space to temporarily pull data into before flushing to evictable user page
    backing_store->swap = get_free_cme((vaddr_t)0, KERNEL_CMI);
    if (backing_store->swap == 0) goto out;
    core_set_free(PADDR_TO_CMI(backing_store->swap));

    //TODO figure this out currently this bitmap size is the max our coremap and page table supports
    backing_store->bm = bitmap_create(MAX_BM);
    if (backing_store->bm == NULL) goto bm_out;

    backing_store->lock = lock_create("disk_lock");
    if (backing_store->lock == NULL) goto lk_out;

    tlb_sem = sem_create("tlb_sem", 0);
    if (tlb_sem == NULL) goto tlb_out;

    //Set 0 to in use, as that is reserved
    bitmap_mark(backing_store->bm, 0);

    return 0;

tlb_out:
    lock_destroy(backing_store->lock);
lk_out:
    bitmap_destroy(backing_store->bm);
bm_out:
    kfree(backing_store);
out:
    return 1;
}

void remove_from_disk(int swap_index){
	lock_acquire(backing_store->lock);
	bitmap_unmark(backing_store->bm, swap_index);
	lock_release(backing_store->lock);
}


// This assumes that the location has been set as busy by get free cme
// returns with lock set on swap_addr's cme
paddr_t retrieve_from_disk(int swap_index, vaddr_t swap_into){
	lock_acquire(backing_store->lock);
	if(!bitmap_isset(backing_store->bm, swap_index)){
		lock_release(backing_store->lock);
		return 0;
	}
	lock_release(backing_store->lock);
    core_set_busy(PADDR_TO_CMI(backing_store->swap), true);

	struct iovec iov;
	struct uio uio;
    uio_kinit(&iov, &uio, (void *)PADDR_TO_KVADDR(backing_store->swap), PAGE_SIZE, swap_index * PAGE_SIZE, UIO_READ);

    if (VOP_READ(bs, &uio) != 0) {
    	core_set_free(PADDR_TO_CMI(backing_store->swap));
		return 0;
    }

    paddr_t swap_addr = get_free_cme(swap_into, false);
    if(swap_addr == 0){
    	core_set_free(PADDR_TO_CMI(backing_store->swap));
    	return 0;
    }
    memcpy((void *)PADDR_TO_KVADDR(swap_addr), (void *)PADDR_TO_KVADDR(backing_store->swap), PAGE_SIZE);
    core_set_free(PADDR_TO_CMI(backing_store->swap));
    KASSERT(coremap.cm[PADDR_TO_CMI(swap_addr)].busybit == 1);
    return swap_addr;
}

// Assumes that cme for location is already locked, and returns with cme still locked
// TODO zero pages on allocation to allow for isolation between procs?
int write_to_disk(paddr_t location, int index){
	KASSERT(coremap.cm[PADDR_TO_CMI(location)].busybit == 1);

	lock_acquire(backing_store->lock);
	unsigned offset = index;

	if (index <= 0) {
		if (bitmap_alloc(backing_store->bm, &offset) == ENOSPC) {
			lock_release(backing_store->lock);
			return -1;
		}
	}
    lock_release(backing_store->lock);

	struct iovec iov;
	struct uio uio;
    uio_kinit(&iov, &uio, (void *)PADDR_TO_KVADDR(location), PAGE_SIZE, offset * PAGE_SIZE, UIO_WRITE);

	if (VOP_WRITE(bs, &uio) != 0)
		return -1;

	// At this point the data is now on disk
    kprintf("written to disk: cme %zu, swap_offset %zu\n", PADDR_TO_CMI(location), offset);
	return offset;
}
