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
#include <bitmap.h>
#include <synch.h>
#include <pid_table.h>
#include <kern/errno.h>

#define PID_MAX 512

static struct {
    struct bitmap *pid_map;
    struct lock *lock;
    struct proc *proc_map[PID_MAX];
} *pid_table;

/* called in bootstrap */
int init_pid_table(void) {

    pid_table = kmalloc (sizeof *pid_table);
    if (pid_table == NULL) goto out;

    pid_table->pid_map = bitmap_create(PID_MAX);
    if (pid_table->pid_map == NULL) goto bm_out;

    pid_table->lock = lock_create("pid_table_lock");
    if (pid_table->lock == NULL) goto lk_out;

    // Set kernel proc's pid
    bitmap_mark(pid_table->pid_map, 0);

    return 0;

lk_out:
    bitmap_destroy(pid_table->pid_map);
bm_out:
    kfree(pid_table);
out:
    return 1;
}

void
procmap_add(unsigned pid, struct proc *proc) {
    lock_acquire(pid_table->lock);
    pid_table->proc_map[pid] = proc;
    lock_release(pid_table->lock);
}

struct proc *
get_proc(unsigned pid) {
    struct proc *proc = NULL;
    lock_acquire(pid_table->lock);
    proc = pid_table->proc_map[pid];
    lock_release(pid_table->lock);
    return proc;
}

/*
 * called in shutdown when there are no threads left except for the kernel
 * thread
 */
void destroy_pid_table(void) {
    lock_acquire(pid_table->lock);

    bitmap_destroy(pid_table->pid_map);
    pid_table->pid_map = NULL;

    lock_release(pid_table->lock);

    //TODO: failing to destroy, thread.c:1028 line, KASSERT
    //lock_destroy(pid_table->lock);
    kfree(pid_table);
}

pid_t pid_get(void) {
    lock_acquire(pid_table->lock);

    unsigned pid;
    if (bitmap_alloc(pid_table->pid_map, &pid) != ENOSPC){
    	lock_release(pid_table->lock);
    	return pid;
    }

    lock_release(pid_table->lock);

    return (pid_t) -1;
}

void pid_destroy(pid_t pid) {
    lock_acquire(pid_table->lock);
    // TODO: check that this thread holds the pid
    KASSERT(bitmap_isset(pid_table->pid_map, (unsigned)pid));
    bitmap_unmark(pid_table->pid_map, (unsigned)pid);
    pid_table->proc_map[pid] = NULL;
    lock_release(pid_table->lock);
}

bool pid_in_use(pid_t pid) {
    lock_acquire(pid_table->lock);
    if (bitmap_isset(pid_table->pid_map, (unsigned)pid)) {
        lock_release(pid_table->lock);
        return true;
    }
    lock_release(pid_table->lock);
    return false;
}

