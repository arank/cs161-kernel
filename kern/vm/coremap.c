#include <types.h>

struct cme {
    uint32_t vpn:       20,
             pid:       9;
    uint32_t swap:      15,
             dirty:     1,
             busybit:   1,
             ref:       1,
             use:       1,
             kern:      1;
};

struct coremap {
    struct spinlock *lock;
    unsigned free;
    unsigned modified;
    const unsigned size;
    const struct *cme;
    int last_allocated;
};

// We don't give the option to retry as that would
// involve sleeping which could lead to livelock
int core_set_in_use(int index){
	spinlock_acquire(coremap->lock);
	if(coremap->cme[index]->busybit == 0){
		coremap->cme[index]->busybit = 1;
		spinlock_release(coremap->lock);
	}else{
		spinlock_release(coremap->lock);
		return 1;
	}
}


int core_set_free(int index){
	spinlock_acquire(coremap->lock);
	if(coremap->cme[index]->busybit == 1){
		coremap->cme[index]->busybit = 0;
		spinlock_release(coremap->lock);
		return 0;
	}else{
		spinlock_release(coremap->lock);
		return 1;
	}
}

paddr_t get_free_cme(vaddr_t vpn, bool kern){

	spinlock_aquire(coremap->lock);
	int index = coremap->last_allocated;
	spinlock_release(coremap->lock);

	for(int i = 0; i < coremap->size; i++){
		int index = (index+1) % coremap->size;
		if(core_set_in_use(index) == 0){
			// Check if in use
			if(coremap->cme[index]->use == 0){
				coremap->cme[index]->use = 1;
				coremap->cme[index]->vpn = vpn;
				coremap->cme[index]->pid = curproc->pid;
				if(kern){
					coremap->cme[index]->kern = 1;
				}else{
					coremap->cme[index]->kern = 0;
				}
				core_set_free(index);
				// TODO possibly zero page here.
				// Multiply by page size to get paddr
				return index*4096;
			}
			// TODO add eviction later
			core_set_free(index);
		}
	}
}



