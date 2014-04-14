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

    // no need to make a function call and acquire lock -> return immideately
    if (num_bytes == 0) goto done;

    if (num_bytes % PAGE_SIZE != 0) return EINVAL;


	// We ran out of memory or (given the amount of physical memory, we cannot
    // grow heap till the red zone)
    bool allocated[PD_SIZE];
	for(int i = 0; i < PD_SIZE; i++)
		allocated[i] = false;

	// TODO ensure there is space for user to have at least 2-3 pages to return error to
	// TODO is it safe to set to the beggining of the next page (for allocing last page)?
	if(expand_as(as, as->heap_end, (size_t)num_bytes, 1, 1, 1, allocated) != 0){
		for(int i =0; i < PD_SIZE; i++){
			if(allocated[i]){
				if(as->page_dir->dir[i]->lock != NULL)
					lock_destroy(as->page_dir->dir[i]->lock);
				if(as->page_dir->dir[i]->cv != NULL)
					cv_destroy(as->page_dir->dir[i]->cv);
				if(as->page_dir->dir[i]->table != NULL)
					kfree(as->page_dir->dir[i]->table);
				kfree(as->page_dir->dir[i]);
				as->page_dir->dir[i] = NULL;
			}
		}

		return ENOMEM;
	}


done:
	*prev = prev_break;
	return 0;
}
