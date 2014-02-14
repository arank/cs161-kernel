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
 * SYNCHRONIZATION PROBLEM 2: DELETION GAMES
 *
 * Katniss and Peeta are tired of Hunger Games and want to play a new kind
 * of game instead, the Deletion Games! They want to sever all ties between
 * the Capitol and all of its districts (for the sake of this problem, assume
 * that there are actually NSLOTS districts). Katniss is severing ties
 * from the Capitol side, and Peeta is severing ties from the Districts' side.
 *
 * There is a 1:1 correspondence between capitol_slots and district_slots. This
 * means that each slot in capitol_slots has exactly one corresponding entry in
 * district_slots, and each slot in district_slots has exactly one corresponding
 * entry in capitol_slots. More formally:
 *
 * For all i in {0, 1, 2, ..., NSLOTS - 1},
 * if capitol_slot[i].is_mapped == true, then
 * district_slot[capitol_slot[i].district_index].is_mapped == true AND
 * district_slot[capitol_slot[i].district_index].capitol_index == i
 * AND similarly if district_slot[i].is_mapped == true.
 *
 * Katniss and Peeta each will use NTHREADS to delete these mappings. Katniss
 * will delete mappings based on randomly generated capitol indices, and Peeta
 * will delete mappings based on randomly generated district indices.
 *
 * For example, suppose Katniss randomly chooses capitol index 4 to delete.
 * She looks at capital slot 4, sees that the slot is still mapped, and finds
 * the corresponding district index is 12. Then Katniss will free the mappings
 * in capitol slot 4 and district slot 12.
 *
 * Suppose Peeta, on the other hand, randomly chooses district index 12 to
 * delete. He looks at district slot 12, sees that the slot is still mapped,
 * and finds the corresponding capitol index is 4. Then Peeta will free the
 * mappings in district slot 12 and capitol slot 4.
 *
 * However, without proper synchronization, we may get
 * - race conditions: if multiple threads attempt to delete the same mappings
 *   at the same time
 * - deadlock: Katniss and Peeta try to delete the (capitol 4, district 12)
 *   mappings at the same time starting from opposite sides.
 *
 * Your solution must satisfy these conditions:
 * - Avoid race conditions.
 * - Guarantee no deadlock can occur. Your invariants and comments should
 *   provide a convincing proof of this.
 *   HINT: You should insert well-placed thread_yield() calls in your code to
 *   convince yourself of no deadlock.
 * - When Katniss and Peeta generate random indices to delete, you may decide
 *   to IGNORE that index if you wish and move onto the next index. However,
 *   all mappings must eventually be deleted.
 *   HINT: Use this to your advantage to introduce some asymmetry to the
 *   problem.
 *
 * Here is an example of correct looking output:
...
{who: katniss, capitol: 86, district: 16, deleted: 114}
{who: katniss, capitol: 108, district: 97, deleted: 115}
{who: peeta, capitol: 89, district: 13, deleted: 116}
{who: katniss, capitol: 103, district: 49, deleted: 117}
{who: katniss, capitol: 5, district: 91, deleted: 118}
{who: peeta, capitol: 57, district: 44, deleted: 119}
{who: katniss, capitol: 39, district: 81, deleted: 120}
{who: katniss, capitol: 55, district: 96, deleted: 121}
{who: peeta, capitol: 101, district: 64, deleted: 122}
{who: katniss, capitol: 44, district: 58, deleted: 123}
{who: katniss, capitol: 59, district: 78, deleted: 124}
{who: katniss, capitol: 80, district: 85, deleted: 125}
{who: katniss, capitol: 122, district: 88, deleted: 126}
{who: katniss, capitol: 67, district: 114, deleted: 127}
{who: katniss, capitol: 61, district: 17, deleted: 128}
 */

#include <types.h>
#include <lib.h>
#include <wchan.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/errno.h>
#include "common.h"

#define NSLOTS 128
#define NTHREADS 32 // number of threads for each person

unsigned num_deleted = 0; // number of deleted mappings

struct capitol_slot {
    struct semaphore *slot_lock;    /* mutx for every slot */
    unsigned district_index;
    bool is_mapped; // indicates this slot is still in use
};

struct district_slot {
    struct semaphore *slot_lock;    /* mutex for every slot */
    unsigned capitol_index;
    bool is_mapped; // indicates this slot is still in use
};

struct semaphore *main; /* semaphore for the main thread to exit */

// Contains capitol <-> district mapping
struct capitol_slot capitol_slots[NSLOTS];
struct district_slot district_slots[NSLOTS];

/*
 * Indicates who is deleting the current mapping. Used for
 * print_deleted_mapping.
 */
enum person {
    KATNISS,
    PEETA,
};

/*
 * DO NOT MODIFY THIS. Call this function after every delete.
 */
static
void
print_deleted_mapping(enum person who,
                      unsigned capitol_index,
                      unsigned district_index,
                      unsigned num_deleted_current) {
    const char *name = (who == KATNISS) ? "katniss" : "peeta";
    kprintf("{who: %s, capitol: %u, district: %u, deleted: %u}\n",
            name, capitol_index, district_index, num_deleted_current);
    thread_yield(); // cause some interleaving
}

/*
 * Do not modify this!
 */
static
void
init_mappings(void) {
    unsigned array[NSLOTS];
    for (unsigned i = 0; i < NSLOTS; i++) {
        array[i] = i;
    }
    // generate a random bijection between capitol indices and district indices
    shuffle(array, NSLOTS);
    for (unsigned i = 0; i < NSLOTS; i++) {
        unsigned capitol_index = i;
        unsigned district_index = array[i];
        capitol_slots[capitol_index].district_index = district_index;
        capitol_slots[capitol_index].is_mapped = true;
        district_slots[district_index].capitol_index = capitol_index;
        district_slots[district_index].is_mapped = true;
    }
}

static
void
katniss(void *data, unsigned long junk) {
    (void) data;
    (void) junk;

    while (1) {
        // Check if there are any more slots left
        if (num_deleted == NSLOTS) {
            break;
        }

        // generate random capitol index to delete
        unsigned capitol_index = random() % NSLOTS;

        if (!capitol_slots[capitol_index].is_mapped) {
            continue;
        }

        unsigned district_index = capitol_slots[capitol_index].district_index;

        /* if we are here, we hold both locks and do the work */
        P(district_slots[district_index].slot_lock); /* get foreign lock */
        P(capitol_slots[capitol_index].slot_lock);   /* get your lock */

        // do modifications
        capitol_slots[capitol_index].is_mapped = false;
        district_slots[district_index].is_mapped = false;
        unsigned num_deleted_current = ++num_deleted;

        V(capitol_slots[capitol_index].slot_lock);    /* release you lock */
        V(district_slots[district_index].slot_lock);  /* release foreign lock */

        print_deleted_mapping(KATNISS, capitol_index, district_index,
                              num_deleted_current);
    }
    KASSERT (num_deleted == NSLOTS);    /* just to be sure */
    V(main);                            /* let main finish */
}

/** comments for peeta are the same, mirror refletion */
static
void
peeta(void *data, unsigned long junk) {
    (void) data;
    (void) junk;

    while (1) {
        // Check if there are any more slots left
        if (num_deleted == NSLOTS) {
            break;
        }

        // generate random district index to delete
        unsigned district_index = random() % NSLOTS;

        if (!district_slots[district_index].is_mapped) {
            continue;
        }

        unsigned capitol_index = district_slots[district_index].capitol_index;

        P(capitol_slots[capitol_index].slot_lock);
        P(district_slots[district_index].slot_lock);

        capitol_slots[capitol_index].is_mapped = false;
        district_slots[district_index].is_mapped = false;

        V(district_slots[district_index].slot_lock);
        V(capitol_slots[capitol_index].slot_lock);

        unsigned num_deleted_current = ++num_deleted;
        print_deleted_mapping(PEETA, capitol_index, district_index,
                              num_deleted_current);
    }
    KASSERT (num_deleted == NSLOTS);
    V(main);
}

int
deletiongames(int nargs, char **args) {
    (void) nargs;
    (void) args;
    init_mappings();
    num_deleted = 0;

    main = sem_create("main", 0);   /* sem for the main thread */
    for (int i = 0; i < NSLOTS; i++) {
        district_slots[i].slot_lock = sem_create("district", 1);
        capitol_slots[i].slot_lock = sem_create("district", 1);
    }
    
    // spawn Katniss's and Peeta's threads
    for (unsigned i = 0; i < NTHREADS; i++) {
        thread_fork_or_panic("katniss", NULL, katniss, NULL, 0);
        thread_fork_or_panic("peeta", NULL, peeta, NULL, 0);
    }

    /* wait for all threads to finish, clean up and return */
    for (int i = 0; i < 2*NTHREADS; i++)
        P(main);

    /* release memory for sems */
    for (int i = 0; i < NSLOTS; i++) {
        sem_destroy(district_slots[i].slot_lock);
        sem_destroy(capitol_slots[i].slot_lock);
    }

    sem_destroy(main);
    KASSERT (num_deleted == NSLOTS); /* just to be extra sure */

    return 0;
}
