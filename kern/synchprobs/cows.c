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
 * SYNCHRONIZATION PROBLEM 1: SINGING COWS
 *
 * A cow has many children. Each baby cow puts on a performance by singing
 * lyrics to "Call Me Maybe." Like a good parent, the daddy cow must
 * sit through each one of its baby cow's performances until the end, in order
 * to say "Congratulations Baby N!" where N corresponds to the N-th baby cow.
 *
 * At any given moment, there is a single parent cow and possibly multiple
 * baby cows singing. The parent cow is not allowed to congratulate a baby
 * cow until that baby cow has finished singing. Your solution CANNOT
 * wait for ALL the cows to finish before starting to congratulate the babies.
 *
 * Here is an example of correct looking output:
...
Baby   1 Cow: Hot night, wind was blowin'
Baby   2 Cow: Ripped jeans, skin was showin'
Baby   4 Cow: Don't ask me, I'll never tell
Baby   5 Cow: And this is crazy
Baby   8 Cow: Hot night, wind was blowin'
Parent   Cow: Congratulations Baby 7!
Baby   1 Cow: And now you're in my way
Baby   2 Cow: And now you're in my way
Baby   4 Cow: Hey, I just met you
Baby   5 Cow: Pennies and dimes for a kiss
Baby   8 Cow: But now you're in my way
Parent   Cow: Congratulations Baby 1!
Baby   2 Cow: Ripped jeans, skin was showin'
Baby   4 Cow: I'd trade my soul for a wish
Baby   8 Cow: Hey, I just met you
Parent   Cow: Congratulations Baby 5!
Baby   2 Cow: Your stare was holdin'
Baby   4 Cow: But now you're in my way
Baby   8 Cow: Don't ask me, I'll never tell
Baby   2 Cow: Your stare was holdin'
Baby   4 Cow: Hot night, wind was blowin'
Baby   8 Cow: But now you're in my way
Baby   2 Cow: Your stare was holdin'
Baby   4 Cow: I'd trade my soul for a wish
Baby   8 Cow: But here's my number
Baby   2 Cow: Ripped jeans, skin was showin'
Baby   4 Cow: But now you're in my way
Baby   8 Cow: But now you're in my way
Parent   Cow: Congratulations Baby 2!
Baby   4 Cow: Your stare was holdin'
Baby   8 Cow: Hey, I just met you
Baby   4 Cow: And this is crazy
Baby   8 Cow: I wasn't looking for this
...
 */

#include <types.h>
#include <lib.h>
#include <wchan.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/errno.h>
#include "common.h"


#define NUM_LYRICS 16

const char *LYRICS[NUM_LYRICS] = {
    "I threw a wish in the well",
    "Don't ask me, I'll never tell",
    "I looked to you as it fell",
    "And now you're in my way",
    "I'd trade my soul for a wish",
    "Pennies and dimes for a kiss",
    "I wasn't looking for this",
    "But now you're in my way",
    "Your stare was holdin'",
    "Ripped jeans, skin was showin'",
    "Hot night, wind was blowin'",
    "Where do you think you're going, baby?",
    "Hey, I just met you",
    "And this is crazy",
    "But here's my number",
    "So call me, maybe!",
};

/* simple array Queue implementation */
static int *q; /* pointer to a queue */
struct cv *cv; /* cond var to notify the parent that a child is done */
struct lock *lk; /* lock for the cond var */
struct semaphore *main; /* sem for the main function to wait before exiting */

static struct {
    int head;
    int tail;
    int size;
} q_data;

static
void qInit(int max) {
    q = kmalloc((max+1) * sizeof *q);
    if (q == NULL) panic("kmalloc failed");
    q_data.size = max+1;
    q_data.head = q_data.size;
    q_data.tail = 0;
}
static 
void qDestory() {
    kfree(q);
}

static
int qEmpty() {
    return q_data.head % q_data.size == q_data.tail;
}

static
void qPut(int item) {
    q[q_data.tail++] = item;
    q_data.tail = q_data.tail % q_data.size;
}

static
int qGet() {
    q_data.head = q_data.head % q_data.size;
    return q[q_data.head++];
}

/* Cows' singing */

#define CONGR "Congratulations Baby"

/*
 * Do not modify this!
 */
static
void sing(unsigned cow_num) {
    int r = random() % NUM_LYRICS;
    while (r != 0) {
        kprintf("Baby %3u Cow: %s\n", cow_num, LYRICS[r]);
        r = random() % NUM_LYRICS;
        thread_yield(); // cause some interleaving!
    }
}

// One of these structs should be passed from the main driver thread
// to the parent cow thread.
struct parent_cow_args {
    unsigned total_babies;
};

// One of these structs should be passed from the parent cow thread
// to each of the baby cow threads.
struct baby_cow_args {
    unsigned cow_num;
};

static
void
baby_cow(void *args, unsigned long junk) {
    (void) junk; // suppress unused warnings

    struct baby_cow_args *bcargs = (struct baby_cow_args *) args;
    sing(bcargs->cow_num);

    lock_acquire(lk);   /* get the lock to signal the parent */
    qPut(bcargs->cow_num);  /* add you num to the queue */
    //kprintf("\t Baby %u finished\n", bcargs->cow_num); /* for debugging */
    cv_signal(cv, lk);  /* signal the parent */
    lock_release(lk);   /* release the lock for the parent (or other children) to run */
}

static
void
parent_cow(void *args, unsigned long junk) {
    (void) junk; // suppress unused warnings
    struct parent_cow_args *pcargs = (struct parent_cow_args *) args;
    unsigned finished = 0;  /* counter to stop looking */
    
    while (finished < pcargs->total_babies) {
        lock_acquire(lk);   /* get the lock */
        while(qEmpty()) cv_wait(cv, lk); /* cond wait for a signal from a child */
        kprintf("Parent Cow: %s %d\n", CONGR, qGet()); /* print the baby_num from the queue */
        finished++;         
        lock_release(lk);   /* release the lock for others to contunie */
    } 
    KASSERT(finished == pcargs->total_babies); /* congratulated all children */
    V(main);    /* makes main function (cows) to wait */
}

int
cows(int nargs, char **args) {
    // if an argument is passed, use that as the number of baby cows
    unsigned num_babies = 10;
    if (nargs == 2) {
        num_babies = atoi(args[1]);
    }
    
    cv = cv_create("cv");    
    if (cv == NULL) panic ("cv_create failed");

    lk = lock_create("lk_for_cv");   /* lock for cv */
    if (lk == NULL) panic ("lock_create failed");

    /* semaphore to synch children who finish before 
       the parent has reaped the index of the first finished child */
    main = sem_create("cows_fun", 0); /* sem to synch exit from the program */
    if (main == NULL) panic ("sem_create failed");

    qInit(num_babies); /* initialize the queue */

    struct parent_cow_args *pcargs = kmalloc(sizeof *pcargs);
    if (args == NULL) panic("kmalloc failed");      
    pcargs->total_babies = num_babies; /* let the parent know the num of babies */

    thread_fork_or_panic("Parent thread", NULL, parent_cow, pcargs, 0);
    
    struct baby_cow_args *bcarr[num_babies]; /* array of baby_cows ptrs */
    struct baby_cow_args *bcargs;
    for (unsigned b = 1; b <= num_babies; b++) {
        bcargs = kmalloc(sizeof *bcargs);
        if (bcargs == NULL) panic("kmalloc failed"); 

        bcargs->cow_num = b;     /* give each baby its number */
        bcarr[b-1] = bcargs;    /* save pointer to the struct to free later */
        
        thread_fork_or_panic("Baby thread", NULL, baby_cow, bcarr[b-1], 0);
    }

    P(main);    /* wait till the parent is done to clean up and exit */

    cv_destroy(cv);
    lock_destroy(lk);
    sem_destroy(main);
    qDestory();        
    kfree(pcargs);
    for (unsigned i = 0; i < num_babies; i++) 
        kfree(bcarr[i]);

    return 0;
}
