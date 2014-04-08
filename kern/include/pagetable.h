#ifndef _H_PAGETABLE_H_
#define _H_PAGETABLE_H_

#define PT_SIZE 1024
#define PD_SIZE 1024

struct pte {
    union {
        uint32_t ppn    : 20; // Doubles as swap if not present
        uint32_t swap   : 20;
    };
	uint32_t busybit    : 1;
    uint32_t present    : 1;
    uint32_t valid      : 1;
    uint32_t read       : 1;
    uint32_t write      : 1;
    uint32_t exec       : 1;
    uint32_t junk       : 6;
};
struct page_table {
	struct lock *lock;
	struct cv *cv;
	struct pte* table;
};
struct page_dir{
	struct page_table* dir[PD_SIZE];
};
struct page_dir* page_dir_init(void);
int page_table_add(int index, struct page_dir* pd);
int page_set_busy(struct page_table *pt, int index, bool wait);
int page_set_free(struct page_table *pt, int index);
int page_dir_destroy(struct page_dir* pd);

#endif
