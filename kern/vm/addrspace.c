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
#include <proc.h>
#include <pagetable.h>
#include <synch.h>
#include <coremap.h>
#include <backingstore.h>

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

// TODO add cleanup on error code
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	// TODO do we need a lock on this function?
	lock_acquire(old->lock);

	// Copy heap pointers
	newas->heap_end = old->heap_end;
	newas->heap_start = old->heap_end;

	newas->page_dir = page_dir_init();
		if(newas->page_dir == NULL)
			return ENOMEM;

	for(int i=0; i<PD_SIZE; i++){
		if(old->page_dir->dir[i]!=NULL){
			if (page_table_add(i, newas->page_dir) == ENOMEM)
				return ENOMEM;
			for(int j=0; j<PT_SIZE; j++){
				page_set_busy(old->page_dir->dir[i], j, true);

				// Copy everything but the location of the physical page
				newas->page_dir->dir[i]->table[j].valid
                    = old->page_dir->dir[i]->table[j].valid;
				newas->page_dir->dir[i]->table[j].read
                    = old->page_dir->dir[i]->table[j].read;
				newas->page_dir->dir[i]->table[j].write
                    = old->page_dir->dir[i]->table[j].write;
				newas->page_dir->dir[i]->table[j].exec
                    = old->page_dir->dir[i]->table[j].exec;
				newas->page_dir->dir[i]->table[j].junk
                    = old->page_dir->dir[i]->table[j].junk;
				newas->page_dir->dir[i]->table[j].present =
						old->page_dir->dir[i]->table[j].present;

				// If not allocated or only symbolically linked
				if(old->page_dir->dir[i]->table[j].ppn == 0)
					continue;

				paddr_t free;
				vaddr_t vpn = (i<<22) | (j<<12);

				if(old->page_dir->dir[i]->table[j].present == 1){
					// Copy over page from old addr space memory
					free = get_free_cme(vpn, false);
					paddr_t ppn = old->page_dir->dir[i]->table[j].ppn;
					memcpy((void*)PADDR_TO_KVADDR(free), (void*)PADDR_TO_KVADDR(ppn), PAGE_SIZE);
				}else{
					// Copy over from disk
					free = retrieve_from_disk(newas->page_dir->dir[i]->table[j].ppn, vpn);
				}

				if(free == 0)
					return -1;
				newas->page_dir->dir[i]->table[j].ppn = free;
				// We are pulling into memory here and making it present
				newas->page_dir->dir[i]->table[j].present = 1;

				// We can free the core and the page at this point, if its evicted that is fine
				core_set_free(PADDR_TO_CMI(free));
				page_set_free(old->page_dir->dir[i], j);
			}
		}
	}

	lock_release(old->lock);

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
				// TODO dead lock here if eviction is coming in the other direction?
				// figure out who has to give up first, probably the evictor
				page_set_busy(as->page_dir->dir[i], j, true);

                if (as->page_dir->dir[i]->table[j].valid != 1) continue;

				if(as->page_dir->dir[i]->table[j].present == 1) {
                    if (as->page_dir->dir[i]->table[j].ppn == 0) continue;
					int cm_index = PADDR_TO_CMI(as->page_dir->dir[i]->table[j].ppn);
					// busily wait to get lock on memory
					core_set_busy(cm_index, true);

					KASSERT(coremap.cm[cm_index].kern == 0);
					set_use_bit(cm_index, 0);
					coremap.cm[cm_index].dirty = 0;
					coremap.cm[cm_index].slen = 0;
					coremap.cm[cm_index].seq = 0;
					coremap.cm[cm_index].junk = 0;
					coremap.cm[cm_index].ref = 0;
					coremap.cm[cm_index].pid = 0;
					coremap.cm[cm_index].vpn = 0;

					if(coremap.cm[cm_index].swap!=0){
						remove_from_disk(coremap.cm[cm_index].swap);
						coremap.cm[cm_index].swap = 0;
					}

					// set all of page to zero
					memset((void*)PADDR_TO_KVADDR(CMI_TO_PADDR(cm_index)), 0, PAGE_SIZE);

					core_set_free(cm_index);
				}else{
					remove_from_disk(as->page_dir->dir[i]->table[j].swap);
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
	int pdi = PDI(vaddr);
	if(page_table_add(pdi, as->page_dir) == ENOMEM)
		goto out;

	int pages_to_alloc = (OFFSET(vaddr) + sz) / PAGE_SIZE;
	if((OFFSET(vaddr) + sz) % PAGE_SIZE != 0)
		pages_to_alloc++;

	for(int i = 0, pti = PTI(vaddr); i < pages_to_alloc; i++, pti++){

		if(pti == PT_SIZE){
			if(page_table_add(++pdi, as->page_dir) == ENOMEM)
				goto out;
			pti = 0;
		}

		as->page_dir->dir[pdi]->table[pti].valid = 1;
		as->page_dir->dir[pdi]->table[pti].present = 1;
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
	 * None
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * None
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{

	/* Initial user-level stack pointer */
    // TODO error checking
    as_define_region(as, USERSTACK - (17 * PAGE_SIZE), PAGE_SIZE, 1, 1, 0);
    as_define_region(as, USERSTACK - (16 * PAGE_SIZE), 16 * PAGE_SIZE, 1, 1, 0);

	*stackptr = USERSTACK;

    /*
	if(as->page_dir->dir[PDI(*stackptr)] == NULL)
		page_table_add(PDI(*stackptr), as->page_dir);

	int cur_index = PDI(*stackptr);
	for(int i = 0, j = PTI(*stackptr); i < 17; i++, j--){

		if(j < 0){
			if(page_table_add(--cur_index, as->page_dir))
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
			as->page_dir->dir[cur_index]->table[j].present = 1;
			as->page_dir->dir[cur_index]->table[j].read = 1;
			as->page_dir->dir[cur_index]->table[j].write = 1;
			as->page_dir->dir[cur_index]->table[j].exec = 1;
		}
	}
    */
	return 0;
}

