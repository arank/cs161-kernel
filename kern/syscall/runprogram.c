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
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <kern/unistd.h>
#include <synch.h>
#include <copyinout.h>
#include <limits.h>

struct lock *exec_lock;

int sys_execv(const_userptr_t program, const_userptr_t *args){
	int err;
	char kprogram[NAME_MAX];
	int argc;
	struct addrspace *as;
	struct addrspace *old_as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;

	// Read in number of arguments
	argc = 0;
	while(args[argc] != NULL){
		argc++;
	}
	// Add one more for the program name
	argc++;
	// Declare and nullify the argument array
	char *kargs[argc];
	for(int i = 0; i < argc; i++)
		kargs[i] = NULL;

	// Open the program
	err = copyinstr(program, kprogram, sizeof kprogram, NULL);
	if (err != 0) goto out;

    /* Open the file. */
	err = vfs_open(kprogram, O_RDONLY, 0, &v);
	if (err != 0) goto out;

	// To save the kernel acquire a lock before allocating a huge chunk of memory for copied strings
	lock_acquire(exec_lock);

	// Add the null terminator and all pointers
	unsigned limit, offset = (argc*4)+4;

	// copy in program string
	size_t len =strlen(kprogram)+1;
	kargs[0] = kmalloc(len);
	strcpy(kprogram, kargs[0]);
	limit += (len + (len % 4));

	// Fill in the rest of the arguments
	for(int i = 1; i < argc; i++){
		// Plus one because of the null terminator which is also copied
	    len = strlen((const char *)args[i-1])+1;

		// Calulate how much space it will use up on the user stack and check if it blows arg max
		limit += (len + (len % 4));
		if(limit > ARG_MAX){
			err = E2BIG;
			goto vfs_out;
		}

	    // Malloc our argument
		kargs[i] = kmalloc(len);

		// Copy into kernel
		err = copyinstr(args[i-1], kargs[i], len, NULL);
		if(err != 0) goto vfs_out;

	}

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		err = ENOMEM;
		goto vfs_out;
	}

	/* Switch to it and activate it, while clearing the old address space */
	old_as = proc_setas(as);
	as_destroy(old_as);
	as_activate();

	/* Load the executable. */
	err = load_elf(v, &entrypoint);
	if (err != 0) goto vfs_out;

	/* Done with the file now. */
	vfs_close(v);

	/* Set stack pointer */
	err = as_define_stack(as, &stackptr);
	if (err != 0) goto args_out;

	// Write a Null separator first
	// TODO is this type right for temp?
	userptr_t temp = NULL;
	err = copyout(&temp, (userptr_t)stackptr+(argc*4), sizeof(userptr_t));
	if (err != 0) goto args_out;

	// Copy out the pointers and arguments
	for(int i = 0; i < argc; i++){
		// TODO I am re-calculating str len
		len = strlen(kargs[i])+1;
		size_t real_len = len + (len % 4);

		// Copy into local buffer
		char buf[real_len];
		strcpy(buf, kargs[i]);

		// Write object at offset, with correct buffer
		err = copyout(buf, (userptr_t)stackptr+offset, real_len);
		if (err != 0) goto args_out;

		// Free the pointer after copying out
		kfree(kargs[i]);
		kargs[i] = NULL;

		// Write pointer to object at offset
		temp = (userptr_t)(stackptr+offset);
		err = copyout(&temp, (userptr_t)stackptr+(i*4), sizeof(userptr_t));
		if (err != 0) goto args_out;

		// Augment offset
		offset += real_len;

	}

	lock_release(exec_lock);

	// TODO is this the right addr for argv???
	enter_new_process(argc, (userptr_t)stackptr+offset /*userspace addr of argv*/,
	            NULL /*userspace addr of environment*/,
	            stackptr, entrypoint);

    /* enter_new_process does not return. */
    panic("enter_new_process returned\n");
    return EINVAL;

vfs_out:
    	vfs_close(v);
args_out:
	for(int i = 0; i < argc; i++){
		if(kargs[i] != NULL){
			kfree(kargs[i]);
		}
	}
	lock_release(exec_lock);
out:
	return err;
}


/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 * 'Exec' for kernel
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;

    /* Open the file. */
    result = vfs_open(progname, O_RDONLY, 0, &v);
    if (result) {
        return result;
    }

    /* We should be a new process. */
    KASSERT(proc_getas() == NULL);

    /* Create a new address space. */
    as = as_create();
    if (as == NULL) {
        vfs_close(v);
        return ENOMEM;
    }

    /* Switch to it and activate it. */
    proc_setas(as);
    as_activate();

    /* Load the executable. */
    result = load_elf(v, &entrypoint);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        vfs_close(v);
        return result;
    }

    /* Done with the file now. */
    vfs_close(v);

    /* Define the user stack in the address space */
    result = as_define_stack(as, &stackptr);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        return result;
    }

    /* Warp to user mode. */
    enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
            NULL /*userspace addr of environment*/,
            stackptr, entrypoint);

    /* enter_new_process does not return. */
    panic("enter_new_process returned\n");
    return EINVAL;
}

