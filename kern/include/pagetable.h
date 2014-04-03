#ifndef _H_PAGETABLE_H_
#define _H_PAGETABLE_H_

struct pte {
	uint32_t ppn: 20, // Doubles as swap if not present
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
	struct page_table* dir[1024];
};

struct page_dir* page_dir_init(void);

int page_table_add(int index, struct page_dir* pd);

int page_set_busy(struct page_table *pt, int index, bool wait);

int page_set_free(struct page_table *pt, int index);

int page_dir_destroy(struct page_dir* pd);

#endif
