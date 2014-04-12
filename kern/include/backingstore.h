#define BACKING_STORE "lhd0raw:"
#define MAX_BM 32768

struct backing_store{
	struct lock *lock;
	struct bitmap* bm;
	// TODO we don't need this
	paddr_t swap;
} *backing_store;



int init_backing_store(void);

void remove_from_disk(int swap_index);

paddr_t retrieve_from_disk(int swap_index, vaddr_t swap_into);

int write_to_disk(paddr_t location, int index);

