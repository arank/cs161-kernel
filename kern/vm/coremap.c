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
