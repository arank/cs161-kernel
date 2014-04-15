#include <types.h>
#include <synch.h>
#include <lib.h>
#include <pagetable.h>
#include <kern/errno.h>

// TODO Handle kmalloc failures
struct page_dir*
page_dir_init(){
	struct page_dir* pd = kmalloc(sizeof(struct page_dir));
	if(pd->dir == NULL){
		return NULL;
	}
	// TODO should this be KVADDR?
    memset(pd, 0, sizeof (struct page_dir));
	for(int i = 0; i < PD_SIZE; i++)
		KASSERT(pd->dir[i] == NULL);
	return pd;
}

int page_table_add(int index, struct page_dir* pd){
	if(pd->dir[index] != NULL)
        return -1;  /* we already have this table */

	pd->dir[index] = kmalloc(sizeof(struct page_table));
	if(pd->dir[index] == NULL) goto out;

	pd->dir[index]->lock = lock_create("page_table_lock");
	if(pd->dir[index]->lock == NULL) goto pt_out;

	pd->dir[index]->cv = cv_create("page_table_cv");
	if(pd->dir[index]->cv == NULL) goto lock_out;

	pd->dir[index]->table = kmalloc(sizeof(struct pte) * PT_SIZE);
	if(pd->dir[index]->table == NULL) goto cv_out;

	// Null all entries in page table
	for(int i = 0; i < PT_SIZE; i++)
		pd->dir[index]->table[i].valid = 0;

	return 0;

	cv_out:
		cv_destroy(pd->dir[index]->cv);
	lock_out:
		lock_destroy(pd->dir[index]->lock);
	pt_out:
		kfree(pd->dir[index]);
	out:
		pd->dir[index]->lock = NULL;
		pd->dir[index]->cv = NULL;
		pd->dir[index]->table = NULL;
        pd->dir[index] = NULL;
		return ENOMEM;
}

int page_dir_destroy(struct page_dir* pd){
	for(int i = 0; i < PD_SIZE; i++){
		if(pd->dir[i] != NULL){
			if(pd->dir[i]->lock != NULL)
				lock_destroy(pd->dir[i]->lock);
			if(pd->dir[i]->cv != NULL)
				cv_destroy(pd->dir[i]->cv);
			if(pd->dir[i]->table != NULL)
				kfree(pd->dir[i]->table);
		}
	}
	kfree(pd);
	return 0;
}


int page_set_busy(struct page_table *pt, int index, bool wait){
	lock_acquire(pt->lock);
	if(pt->table[index].busybit == 0){
		pt->table[index].busybit = 1;
		lock_release(pt->lock);
	}else if(wait){
		while(pt->table[index].busybit == 1){
			cv_wait(pt->cv, pt->lock);
		}
		pt->table[index].busybit = 1;
		lock_release(pt->lock);
	}else{
		lock_release(pt->lock);
		return 1;
	}
	return 0;
}


int page_set_free(struct page_table *pt, int index){
	lock_acquire(pt->lock);
	if(pt->table[index].busybit == 1){
		pt->table[index].busybit = 0;
		cv_broadcast(pt->cv, pt->lock);
		lock_release(pt->lock);
		return 0;
	}else{
		lock_release(pt->lock);
		return 1;
	}
}

