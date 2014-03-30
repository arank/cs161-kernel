#include<types.h>
#include<synch.h>

struct pte {
	uint32_t ppn: 20,
			 busybit: 1,
			 present: 1,
			 valid: 1,
			 permissions: 2,
			 junk: 7;
};

struct page_table {
	struct lock *lock;
	struct cv *cv;
	struct pte* table;
};

struct page_dir{
	struct page_table* dir;
};


static int page_set_busy(struct page_table *pt, int index, bool wait){
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


static int page_set_free(struct page_table *pt, int index){
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

