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
    // TODO possibly put on heap to not blow kernel stack
    char kprogram[NAME_MAX];
    int argc = 0;
    struct addrspace *as;
    struct addrspace *old_as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;

    // To save the kernel acquire a lock before allocating a huge chunk of memory for copied strings
    lock_acquire(exec_lock);

    // Copy and check program name
    err = copyinstr(program, kprogram, sizeof kprogram, NULL);
    if (err != 0) goto out;
    if (strlen(kprogram) == 0) {
        err = EISDIR; 
        goto out;
    }

    // check argument list ptr
    userptr_t ptr;
    if (copyin((const_userptr_t)args, &ptr, sizeof(userptr_t))) { 
        err = EFAULT;
        goto out;
    }

    // check actual pointers to arguments
    while(args[argc] != NULL) {
        if (copyin((const_userptr_t)args[argc], &ptr, sizeof(userptr_t))) {     
            err = EFAULT;
            goto out;
        }                                  
        argc++;                                              
    } 

    
    // Declare and nullify the argument array
    char **kargs = kmalloc((argc+1) * sizeof(char*));
    if(kargs == NULL) goto out;
    for(int i = 0; i < (argc + 1); i++)
        kargs[i] = NULL;

    // Add the null terminator and all pointers
    vaddr_t offset = (argc*4)+4;

    // Fill in the rest of the arguments
    for(int i = 0; i < argc; i++){
        // Plus one because of the null terminator which is also copied
        size_t len = strlen((const char *)args[i])+1;

        // Calculate how much space it will use up on the user stack and check if it blows arg max
        offset += (len + 4 - (len % 4));
        if(offset > ARG_MAX){
            err = E2BIG;
            goto args_out;
        }

        // Malloc our argument
        kargs[i] = kmalloc(len);
        if(kargs[i] == NULL) goto args_out;

        // Copy into kernel
        err = copyinstr(args[i], kargs[i], len, NULL);
        if(err != 0) goto args_out;
    }

    /* Create a new address space. */
    as = as_create();
    if (as == NULL) {
        err = ENOMEM;
        goto args_out;
    }

    /* Switch to it and activate it, while clearing the old address space */
    old_as = proc_setas(as);
    as_activate();

    /* Open the file. */
    err = vfs_open(kprogram, O_RDONLY, 0, &v);
    if (err != 0) goto addr_out;

    /* Load the executable. */
    err = load_elf(v, &entrypoint);
    if (err != 0) {
        vfs_close(v);
        goto addr_out;
    }

    /* Done with the file now. */
    vfs_close(v);

    /* Set stack pointer */
    err = as_define_stack(as, &stackptr);
    if (err != 0) goto addr_out;

    vaddr_t stack = stackptr-offset;
    // Plus 4 because of the null2 terminator
    vaddr_t heap = stack+(argc*4)+4;

    // Write a Null separator first
    userptr_t *temp = kmalloc(sizeof(userptr_t));
    if(temp == NULL) goto addr_out;

    // Copy out the pointers and arguments
    for(int i = 0; i < argc; i++){
        // TODO I am re-calculating str len
        size_t len = strlen(kargs[i])+1;
        size_t real_len = (len + 4 - (len % 4));

        // Copy into local buffer
        char* buf = kmalloc(real_len);
        if(buf == NULL){
            kfree(temp);
            goto addr_out;
        }
        strcpy(buf, kargs[i]);

        // Write object at offset, with correct buffer
        err = copyout(buf, (userptr_t)(heap), real_len);
        if (err != 0){
            kfree(buf);
            kfree(temp);
            goto addr_out;
        }

        // Free the pointer after copying out
        kfree(kargs[i]);
        kfree(buf);
        kargs[i] = NULL;

        // Write pointer to object at offset
        *temp = (userptr_t)(heap);
        err = copyout(temp, (userptr_t)(stack), sizeof(userptr_t));
        if (err != 0) {
            kfree(temp);
            goto addr_out;
        }

        // Augment heap
        // Augment stack
        heap += real_len;
        stack += sizeof(userptr_t);
    }

    *temp = NULL;
    err = copyout(temp, (userptr_t)(stack), sizeof(userptr_t));
    if (err != 0) {
        kfree(temp);
        goto addr_out;
    }
    kfree(kargs);
    kfree(temp);

    lock_release(exec_lock);

    as_destroy(old_as);

    // TODO is this the right addr for argv???
    enter_new_process(argc, (userptr_t)(stackptr-offset) /*userspace addr of argv*/,
            NULL /*userspace addr of environment*/,
            stackptr-offset, entrypoint);

    /* enter_new_process does not return. */
    panic("enter_new_process returned\n");
    return EINVAL;


addr_out:
    as_destroy(proc_setas(old_as));
    as_activate();
args_out:
    for(int i = 0; i < argc; i++){
        if(kargs[i] != NULL){
            kfree(kargs[i]);
        }
    }
    kfree(kargs);
out:
    lock_release(exec_lock);
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

