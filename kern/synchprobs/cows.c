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
    // Add stuff as necessary
};

// One of these structs should be passed from the parent cow thread
// to each of the baby cow threads.
struct baby_cow_args {
    // Add stuff as necessary
};

static
void
baby_cow(void *args, unsigned long junk) {
    (void) junk; // suppress unused warnings
    struct baby_cow_args *bcargs = (struct baby_cow_args *) args;
    (void) bcargs; // suppress unused warnings
    // TODO
}

static
void
parent_cow(void *args, unsigned long junk) {
    (void) junk; // suppress unused warnings
    struct parent_cow_args *pcargs = (struct parent_cow_args *) args;
    (void) pcargs; // suppress unused warnings
    // TODO
}

int
cows(int nargs, char **args) {
    // if an argument is passed, use that as the number of baby cows
    unsigned num_babies = 10;
    if (nargs == 2) {
        num_babies = atoi(args[1]);
    }

    // Suppress unused warnings. Remove these when finished.
    (void) sing;
    (void) parent_cow;
    (void) baby_cow;
    // TODO

    return 0;
}
