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
    struct lock *lock;
    struct cv *cv;
    unsigned free;
    unsigned modified;
    const unsigned size;
    const struct *cme;
};

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
