struct backing_store{
	struct lock *lock;
	struct bitmap* bm;
} *backing_store;

// TODO  how to get disk size and location on disk to use
int init_backing_store(void) {

    backing_store = kmalloc (sizeof *backing_store);
    if (backing_store == NULL) goto out;

    backing_store->bm = bitmap_create(1);//TODO figure this out
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



// This assumes that the location has been set as busy
int retrieve_from_disk(unsigned swap_index, paddr_t location){
	lock_acquire(backing_store->lock);
	if(!bitmap_isset(backing_store->bm, swap_index)){
		lock_release(backing_store->lock);
		return -1;
	}
	// TODO figure out how to retrieve from location
    bitmap_unmark(backing_store->bm, swap_index);
    lock_release(backing_store->lock);
}

int write_to_disk(paddr_t location){
	lock_acquire(backing_store->lock);
	unsigned spot;
	if(bitmap_alloc(backing_store->bm, &spot) == ENOSPC){
	    	lock_release(backing_store->lock);
	    	return -1;
	}
	// TODO Figure out how to put the pages from location into spot
	lock_release(backing_store->lock);
}
