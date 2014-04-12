
struct deamon {
    struct lock *lock;
    struct cv *cv;
} deamon;

void start_deamon_thread(unsigned upper);
