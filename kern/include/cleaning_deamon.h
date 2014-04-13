
struct deamon {
    struct lock *lock;
    struct cv *cv;
} deamon;

void cleaning_bootstrap(void);
