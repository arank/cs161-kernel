#include <spinlock.h>

#ifndef _H_COREMAP_H_
#define _H_COREMAP_H_

#define KERNEL_CMI  1
#define USER_CMI    0
#define WAIT        1
#define NO_WAIT     0

void cm_bootstrap(void);
void cleaning_bootstrap(void);

void set_use_bit(int index, int bitvalue);
void set_busy_bit(int index, int bitvalue);
void set_kern_bit(int index, int bitvalue);
void set_dirty_bit(int index, int bitvalue);
vaddr_t alloc_kpages(int npages);

int core_set_busy(int index, bool wait);

int core_set_free(int index);

int clean_cme(int index);

paddr_t get_free_cme(vaddr_t vpn, bool kern);

int stat_coremap(int nargs, char **args);

void set_ref_bit(int index, int bitvalue);
void set_use_bit(int index, int bitvalue);

struct cme {
    uint32_t vpn:       20,
             pid:       9,
             busybit:   1,
             use:       1,
             kern:      1;
    uint32_t swap:      15, /* swap index */
             slen:      10, /* sequence length */
             seq:       1,  /* this bit is 1 if it's a sequence entry except for the first one */
             dirty:     1,
             ref:       1,
             junk:      4;
};

struct coremap {
    struct spinlock lock;
    unsigned free;
    unsigned modified;

    unsigned kernel;
    unsigned busy;
    unsigned used;
    //unsigned swap;
    unsigned ref;

    unsigned size;
    struct cme *cm;
    int last_allocated;
} coremap;

#endif
