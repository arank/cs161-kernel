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

#include <types.h>
#include <kern/errno.h>
#include <mips/vm.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <pagetable.h>
#include <synch.h>
#include <coremap.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	as->page_dir = page_dir_init();
	if(as->page_dir == NULL)
		goto out;

	as->lock = lock_create("address space lock");
	if(as->lock == NULL)
		goto lock_out;

	return as;

lock_out:
	page_dir_destroy(as->page_dir);
out:
	return NULL;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	(void)old;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	// free all cme entries
	for(int i = 0; i < 1024; i++){
		if(as->page_dir->dir[i] != NULL){
			for(int j = 0; j < 1024; j++){
				// TODO dead lock here if eviction is coming in the other direction
				// figure out who has to give up first, probably the evictor
				page_set_busy(as->page_dir->dir[i], j, true);

				if(as->page_dir->dir[i]->table[j].present == 1){
					int cm_index = (int)as->page_dir->dir[i]->table[j].ppn;
					// busily wait to get lock on memory
					while (core_set_busy(cm_index) != 0);

					// TODO should I clean the cme more?
					coremap.cm[cm_index].use = 0;
                    coremap.cm[cm_index].seq = 0;
                    coremap.cm[cm_index].slen = 0;
					// set all of page to zero
					memset((void*)CMI_TO_PADDR(cm_index), 0, (size_t)4096);

					core_set_free(cm_index);
				}else{
					// TODO page on disk, handle deleting this later
					// TODO this may be some hairy synch
				}

				as->page_dir->dir[i]->table[j].valid = 0;

				page_set_free(as->page_dir->dir[i], j);

			}
		}
	}

	page_dir_destroy(as->page_dir);
	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

    vm_tlbshootdown_all();
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	// Calculate offset into dir, and define new table, set the addr to valid, plus the offset.
	if(page_table_add(PDI(vaddr), as->page_dir) == ENOMEM)
		goto out;

	int pages_to_alloc = (OFFSET(vaddr) + sz) / PAGE_SIZE;
	if((OFFSET(vaddr) + sz) % PAGE_SIZE != 0)
		pages_to_alloc++;

	int pdi = PDI(vaddr);
	for(int i = 0, pti = PTI(vaddr); i < pages_to_alloc; i++, pti++){

		if(pti == PT_SIZE){
			if(page_table_add(++pdi, as->page_dir))
				goto out;
			pti = 0;
		}

		as->page_dir->dir[pdi]->table[pti].valid = 1;
        as->page_dir->dir[pdi]->table[pti].read = readable;
        as->page_dir->dir[pdi]->table[pti].write = writeable;
        as->page_dir->dir[pdi]->table[pti].exec = executable;
	}

	return 0;

out:
	page_dir_destroy(as->page_dir);
	return -1;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Initialize HEAP after all elf regions are defined
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * red zone already defined with the stack
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	if(as->page_dir->dir[PDI(*stackptr)] == NULL)
		page_table_add(PDI(*stackptr), as->page_dir);

	int cur_index = PDI(*stackptr);
	for(int i = 0, j = PTI(*stackptr); i < 17; i++, j--){

		if(j < 0){
			if(page_table_add(++cur_index, as->page_dir))
				return -1;
			j=1023;
		}

		// For last page add redzone
		if(i == 16){
			as->page_dir->dir[cur_index]->table[j].valid = 1;
			as->page_dir->dir[cur_index]->table[j].read = 0;
			as->page_dir->dir[cur_index]->table[j].write = 0;
			as->page_dir->dir[cur_index]->table[j].exec = 0;
		}else{
			as->page_dir->dir[cur_index]->table[j].valid = 1;
			as->page_dir->dir[cur_index]->table[j].read = 1;
			as->page_dir->dir[cur_index]->table[j].write = 1;
			// TODO should exec be 1 for this?
			as->page_dir->dir[cur_index]->table[j].exec = 1;
		}
	}

	return 0;
}

