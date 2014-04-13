#include <types.h>
#include <syscall.h>
#include <current.h>
#include <synch.h>
#include <pagetable.h>
#include <mips/vm.h>
#include <addrspace.h>
#include <proc.h>
#include <kern/errno.h>
#include <coremap.h>

int sys_sbrk(intptr_t num_bytes, vaddr_t *prev){
    if (num_bytes < 0) return EINVAL;

	struct addrspace *as= curproc->p_addrspace;
	vaddr_t prev_break = as->heap_end;

    // no nede to make a function call and acquire lock -> return immideately
    if (num_bytes == 0) goto done;

    if (num_bytes % PAGE_SIZE != 0) return EINVAL;

    // check if we have enough space to accomodate the new page tables
    if ((unsigned)((num_bytes/4096)/1024) > (coremap.size - coremap.used)) return ENOMEM;


	// No multi-threaded processes but I'm still locking here to get the atomicity from the posix api
	lock_acquire(as->lock);

	// We ran out of memory or (given the amount of physical memory, we cannot
    // grow heap till the red zone)
	if (as_define_region(as, as->heap_end, (size_t)num_bytes, 1, 1, 1) != 0){
		lock_release(as->lock);
		return ENOMEM;
	}

	as->heap_end += num_bytes;

	lock_release(as->lock);

done:
	*prev = prev_break;;
	return 0;
}
