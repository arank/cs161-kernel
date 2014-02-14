/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization test code.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>

#define NSEMLOOPS     63
#define NLOCKLOOPS    120
#define NCVLOOPS      5
#define NTHREADS      42

static volatile unsigned long testval1;
static volatile unsigned long testval2;
static volatile unsigned long testval3;
static struct semaphore *testsem;
static struct semaphore *testsem2;
static struct lock *testlock;
static struct cv *testcv;
static struct semaphore *donesem;

static
void
inititems(void)
{
	if (testsem==NULL) {
		testsem = sem_create("testsem", 2);
		if (testsem == NULL) {
			panic("synchtest: sem_create failed\n");
		}
	}
	if (testlock==NULL) {
		testlock = lock_create("testlock");
		if (testlock == NULL) {
			panic("synchtest: lock_create failed\n");
		}
	}
	if (testcv==NULL) {
		testcv = cv_create("testlock");
		if (testcv == NULL) {
			panic("synchtest: cv_create failed\n");
		}
	}
	if (donesem==NULL) {
		donesem = sem_create("donesem", 0);
		if (donesem == NULL) {
			panic("synchtest: sem_create failed\n");
		}
	}
}

static
void
semtestthread(void *junk, unsigned long num)
{
	int i;
	(void)junk;

	/*
	 * Only one of these should print at a time.
	 */
	P(testsem);
	kprintf("Thread %2lu: ", num);
	for (i=0; i<NSEMLOOPS; i++) {
		kprintf("%c", (int)num+64);
	}
	kprintf("\n");
	V(donesem);
}

int
semtest(int nargs, char **args)
{
	int i, result;

	(void)nargs;
	(void)args;

	inititems();
	kprintf("Starting semaphore test...\n");
	kprintf("If this hangs, it's broken: ");
	P(testsem);
	P(testsem);
	kprintf("ok\n");

	for (i=0; i<NTHREADS; i++) {
		result = thread_fork("semtest", NULL, semtestthread, NULL, i);
		if (result) {
			panic("semtest: thread_fork failed: %s\n",
			      strerror(result));
		}
	}

	for (i=0; i<NTHREADS; i++) {
		V(testsem);
		P(donesem);
	}

	/* so we can run it again */
	V(testsem);
	V(testsem);

	kprintf("Semaphore test done.\n");
	return 0;
}

static
void
fail(unsigned long num, const char *msg)
{
	kprintf("thread %lu: Mismatch on %s\n", num, msg);
	kprintf("Test failed\n");

	lock_release(testlock);

	V(donesem);
	thread_exit();
}

static
void
locktestthread(void *junk, unsigned long num)
{
	int i;
	(void)junk;

	for (i=0; i<NLOCKLOOPS; i++) {
		lock_acquire(testlock);
		testval1 = num;
		testval2 = num*num;
		testval3 = num%3;

		if (testval2 != testval1*testval1) {
			fail(num, "testval2/testval1");
		}

		if (testval2%3 != (testval3*testval3)%3) {
			fail(num, "testval2/testval3");
		}

		if (testval3 != testval1%3) {
			fail(num, "testval3/testval1");
		}

		if (testval1 != num) {
			fail(num, "testval1/num");
		}

		if (testval2 != num*num) {
			fail(num, "testval2/num");
		}

		if (testval3 != num%3) {
			fail(num, "testval3/num");
		}

		lock_release(testlock);
	}
	V(donesem);
}

int
locktest(int nargs, char **args) {
	int i, result;

	(void)nargs;
	(void)args;

	inititems();
	kprintf("Starting lock test...\n");

	for (i=0; i<NTHREADS; i++) {
		result = thread_fork("synchtest", NULL, locktestthread, NULL, i);
		if (result)
			panic("locktest: thread_fork failed: %s\n", strerror(result));
	}

	for (i=0; i<NTHREADS; i++) P(donesem);

	kprintf("Lock test done.\n");

	return 0;
}




static 
void
test_lock_create(){
    struct lock *lk = lock_create("testlock");

    KASSERT(!strcmp(lk->lk_name, "testlock"));
    KASSERT(lk->lk_holder == NULL);

    lock_destroy(lk);

    kprintf("test_lock_create: Passed\n");
}

static 
void
test_holder_helper(void *p, unsigned long i){
    (void)i;
    struct lock *lk = p;
    lock_release(lk); 
}

static 
void
test_relese_holder(){
    kprintf("test_holder: this test will fail with the following message:\n"
            "panic: Assertion failed: lock->holder == curthread, at ../../thread/synch.c\n");

    struct lock *lk = lock_create("testlock");
    lock_acquire(lk);

    int err = thread_fork("test_holder_helper", NULL, test_holder_helper, (char *)lk, 0);
    if (err) 
        panic("test_relese_holder: thread_fork failed: %s\n", strerror(err));

    kprintf("test_relese_holder: Passed\n");
}

static 
void
test_do_i_hold_helper(void *p, unsigned long i){
    (void)i;
    struct lock *lk = p;
    KASSERT(!lock_do_i_hold(lk));

    V(testsem2);
}

static 
void
test_do_i_hold(){
    testsem2 = sem_create("testsem2", 0);
    struct lock *lk = lock_create("lock");
    lock_acquire(lk);
    KASSERT(lock_do_i_hold(lk));

    int err = thread_fork("test_do_i_hold_helper", NULL, test_do_i_hold_helper, (char *)lk, 0);
    if (err) 
        panic("test_do_i_hold: thread_fork failed: %s\n", strerror(err));

    // release resources
    P(testsem2);
    lock_release(lk);
    lock_destroy(lk);
    sem_destroy(testsem2);

    kprintf("test_do_i_hold: Passed\n");
}

static 
void
acquire_release_helper(void *p, unsigned long i){
    (void)i;
    struct lock *lk = p;
    lock_acquire(lk);
    kprintf("Thread %d acquired the lock\n", (int)i);
    lock_release(lk);
    V(testsem2);
}

static 
void
test_acquire_release(int td_num){
    int i;

    testsem2 = sem_create("testsem2", 0);
    struct lock *lk = lock_create("testlock");

    // Fork 20 threads that all try to acquire, then release the lock
    for(i = 0; i < td_num; i++) {
        int err = thread_fork("helper", NULL, acquire_release_helper, (char *)lk, i);
        if (err) 
            panic("test_acquire_release: thread_fork failed: %s\n", strerror(err));
    }

    // release recourses
    for(i = 0; i < td_num; i++) P(testsem2);
    sem_destroy(testsem2);
    lock_destroy(lk);

    kprintf("test_acquire_release: Passed\n");
    
    V(testsem); // End of the last unit test; exit the main function
}

#define NUM_THREADS 20

int 
lock_unittest(int nargs, char **args){
    (void)nargs;
    (void)args;

    testsem = sem_create("testsem", 0);

    kprintf("Starting Locks Unit Tests....\n");

    /* Tests that lock is properly created and fields are initialized */
    test_lock_create();

    /* Tests that lock_acquire() and lock_release() are properly synchronize by 
       spawning num threads, all of which try to acquire and release the lock */
    test_acquire_release(NUM_THREADS);

    /* Tests that lock_do_i_hold() returns whether the curthread holds the lock */
    test_do_i_hold();

    /* Tests that only the thread holding the lock may release it; */
    if(0) test_relese_holder();

    // Menu output synchronization
    P(testsem);
    sem_destroy(testsem);

    return 0;
}

static 
void
cvtestthread(void *junk, unsigned long num)
{
	int i;
	volatile int j;
	struct timespec ts1, ts2;

	(void)junk;

	for (i=0; i<NCVLOOPS; i++) {
		lock_acquire(testlock);
		while (testval1 != num) {
			gettime(&ts1);
			cv_wait(testcv, testlock);
			gettime(&ts2);

			/* ts2 -= ts1 */
			timespec_sub(&ts2, &ts1, &ts2);

			/* Require at least 2000 cpu cycles (we're 25mhz) */
			if (ts2.tv_sec == 0 && ts2.tv_nsec < 40*2000) {
				kprintf("cv_wait took only %u ns\n", ts2.tv_nsec);
				kprintf("That's too fast... you must be " "busy-looping\n");
				V(donesem);
				thread_exit();
			}

		}
		kprintf("Thread %lu\n", num);
		testval1 = (testval1 + NTHREADS - 1)%NTHREADS;

		/*
		 * loop a little while to make sure we can measure the
		 * time waiting on the cv.
		 */
		for (j=0; j<3000; j++);

		cv_broadcast(testcv, testlock);
		lock_release(testlock);
	}
	V(donesem);
}

int
cvtest(int nargs, char **args)
{

	int i, result;

	(void)nargs;
	(void)args;

	inititems();
	kprintf("Starting CV test...\n");
	kprintf("Threads should print out in reverse order.\n");

	testval1 = NTHREADS-1;

	for (i=0; i<NTHREADS; i++) {
		result = thread_fork("synchtest", NULL, cvtestthread, NULL, i);
		if (result) {
			panic("cvtest: thread_fork failed: %s\n", strerror(result));
		}
	}
	for (i=0; i<NTHREADS; i++) {
		P(donesem);
	}

	kprintf("CV test done\n");

	return 0;
}

#if 0
static void
test_cv_create(){
    struct cv *cv = cv_create("cv");

    KASSERT(!strcmp(cv->cv_name, "cv"));
    KASSERT(cv->cv_wchan != NULL);

    cv_destroy(cv);

    kprintf("test_cv_create: Passed.....\n");
}


static void
test_cv_signal_helper(void *p, unsigned long i){
    (void)i;
    struct cv *cv = p;

    lock_acquire(cv_lock);
    V(channel_1);
    cv_wait(cv, cv_lock);
    kprintf("Signaled!\n");
    lock_release(cv_lock);

    V(channel_1);
}

static void
test_cv_signal(){
    int i;
    struct cv *cv = cv_create("cv");
    cv_lock = lock_create("cv lock");
    channel_1 = sem_create("channel 1", 0);

    kprintf("Signal recieved should only print once:\n");

    // Fork 2 threads, only 1 of which should recieve the signal
    for(i=0;i<2;i++){
        int err = thread_fork("test_cv_signal_helper", test_cv_signal_helper, \
            (char *)cv, 0, NULL);
        if (err) {
            panic("test_cv_signal_helper: thread_fork failed: %s\n", strerror(err));
        }
    }
    // Wait for threads to be ready
    for(i=0;i<2;i++){
        P(channel_1);
    }

    lock_acquire(cv_lock);
    cv_signal(cv, cv_lock);
    lock_release(cv_lock);

    // Clean up
    P(channel_1);
    sem_destroy(channel_1);

    kprintf("test_cv_signal: Passed.....\n");
}

static void
test_cv_broadcast_helper(void *p, unsigned long i){
    struct cv *cv = p;

    lock_acquire(cv_lock);
    V(channel_1);
    cv_wait(cv, cv_lock);
    kprintf("Thread %d signaled!\n", (int) i);
    lock_release(cv_lock);

    V(channel_1);
}

static void
test_cv_broadcast(){
    int i;
    struct cv *cv = cv_create("cv");
    cv_lock = lock_create("cv lock");
    channel_1 = sem_create("channel 1", 0);

    // Fork 10 threads; all should recieve the signal
    for(i=0;i<10;i++){
        int err = thread_fork("test_cv_broadcast_helper", test_cv_broadcast_helper, \
            (char *)cv, i, NULL);
        if (err) {
            panic("test_cv_broadcast_helper: thread_fork failed: %s\n", strerror(err));
        }
    }
    // Wait for threads to be ready
    for(i=0;i<10;i++){
        P(channel_1);
    }

    lock_acquire(cv_lock);
    cv_broadcast(cv, cv_lock);
    lock_release(cv_lock);

    // Clean up
    for(i=0;i<10;i++){
        P(channel_1);
    }

    cv_destroy(cv);
    lock_destroy(cv_lock);
    sem_destroy(channel_1);

    kprintf("test_cv_broadcast: Passed.....\n");

    V(driver); // Placed at the end of the last unit test
}

#endif
int cv_unittest(int nargs, char **args){
    (void)nargs;
    (void)args;
    return 0;
#if 0
    driver = sem_create("driver", 0);

    kprintf("Starting Unit Test Suite for CVs..........\n");

    /* Test that cv_create() creates a cv with cv_name and cv_wchan properly
       initialized. */
    test_cv_create();

    /* Test that cv_signal() signals 1 waiting thread and only 1. */
    test_cv_signal();

    /* Test that cv_broadcast signals all waiting threads. */
    test_cv_broadcast();

    // Synchronize with menu
    P(driver);
    sem_destroy(driver);
#endif
}
