/*
 * Copyright (c) 2013
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

#include <types.h>
#include <current.h>
#define PID_MAX 512

static struct {
    struct bitmap *pid_map;
    unsigned counter;
    struct lock *lock;
} pid_table;

/* called in bootstrap */
int init_pid_table(void) {
    pid_table = kmalloc (sizeof *pid_table);
    if (pid_table == NULL) goto out;

    pid_table->pid_map = pid_map_create(PID_MAX);
    if (pid_table->pid_map == NULL) goto bm_out;

    pid_table->lock = lock_create("pid_table_lock");
    if (pid_table->lock == NULL) goto lk_out;

    pid_table->counter = 0;
    
    return 0;

lk_out:
    destoy_pid_map(pid_table->pid_map);
bm_out:
    kfree(pid_table);
out:
    return 1;
}

/* called in shutdown when there are no threads left except for the kernel
 * thread
 */
void destroy_pid_table(void) {
    lock_acquire(pid_table->lock);

    pid_map_destroy(pid_table->pid_map);
    pid_table->pid_map = NULL;

    lock_release(pid_table->lock);

    lock_destoy(pid_table->lock);
    kfree(pid_table); 
}

pid_t pid_get(void) {
    lock_acquire(pid_table->lock);
    
    unsigned pid = pid_table->counter;
    while(pid < pid_table->counter + MAX_PID) {
        unsigned index = pid % PID_MAX;
        // TODO: investigate bitmap_alloc, how does it work
        if (!bitmap_isset(pid_table->pid_map, index)) {
            bitmap_mark(pid_table->pid_map, index);
            pid_table->counter = index;
            lock_release(pid_table->lock);
            return (pid_t) index;
        }
        pid++;
    }

    return (pid_t) -1;
}

void pid_destroy(pid_t pid) {
    lock_acquire(pid_table->lock);
    
    // TODO: check that this threads holds the pid
    assert(bitmap_isset(pid_table->pid_map, (unsigned)pid);
    bitmap_unmark(pid_table->pid_map, (unsigned)pid);

    lock_release(pid_table->lock);
}

int pid_in_use(pid_t pid) {
    lock_acquire(pid_table->lock);
    if (bitmap_isset(pid_table->pid_map, (unsigned)pid)) {
        lock_release(pid_table->lock);
        return 1;
    }
    return 0;
}
