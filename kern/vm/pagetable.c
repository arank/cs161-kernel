struct pte {
 	union {
 		paddr_t ppn: 20;
		unsigned swap: 20;
	};
	uint32_t busybit: 1,
			 present: 1,
			 valid: 1,
			 permissions: 2;
};

struct page_table {
	struct lock *lock;
	struct cv *cv;
	const struct pte*;
};

struct page_dir{
	struct pte* dir;
};


int page_set_in_use(struct page_table *pt, int index, bool wait){
	lock_acquire(pt->lock);
	if(pt->pte[index]->busybit == 0){
		pt->pte[index]->busybit = 1;
		lock_release(pt->lock);
	}else if(wait){
		while(pt->pte[index]->busybit == 1){
			cv_wait(pt->cv, pt->lock);
		}
		pt->pte[index]->busybit = 1;
		lock_release(pt->lock);
	}else{
		lock_release(pt->lock);
		return 1;
	}
	return 0;
}


int page_set_free(struct page_table *pt, int index){
	lock_acquire(pt->lock);
	if(pt->pte[index]->busybit == 1){
		pt->pte[index]->busybit = 0;
		cv_broadcast(pt->cv, pt->lock);
		lock_release(pt->lock);
		return 0;
	}else{
		lock_release(pt->lock);
		return 1;
	}
}
