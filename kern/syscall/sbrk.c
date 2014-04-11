#include <types.h>
#include <syscall.h>
#include <current.h>
#include <synch.h>
#include <pagetable.h>
#include <mips/vm.h>
#include <addrspace.h>
#include <proc.h>

int sys_sbrk(intptr_t num_bytes, vaddr_t *top){
	if(num_bytes%PAGE_SIZE!=0)return -1;

	struct addrspace *p_addrspace= curproc->p_addrspace;
	vaddr_t old_heap = p_addrspace->heap_end;

	// No multi-threaded processes but I'm still locking here to get the atomicity from the posix api
	lock_acquire(p_addrspace->lock);

	// We ran out of memory or heap ran into stack....
	if (as_define_region(p_addrspace, p_addrspace->heap_end, (size_t)num_bytes, 1, 1, 1) != 0){
		lock_release(p_addrspace->lock);
		return -1;
	}

	p_addrspace->heap_end += num_bytes;

	lock_release(p_addrspace->lock);

	*top = old_heap;
	return 0;
}
