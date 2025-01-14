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
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <synch.h>
#include <elf.h>
/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */


struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL)
	{
		return NULL;
	}

	as->region = NULL;

	// as->p_lock = lock_create("page_lock");

	return as;
}

int as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas == NULL)
	{
		return ENOMEM;
	}

	/*
	 * Write this.
	 */
	// Copy pages from old address to new address
	struct as_region *old_region = old->region;
	struct as_region **new_region = &newas->region;
	while (old_region)
	{
		struct as_region *as_region = kmalloc(sizeof(struct as_region));
		if (!as_region)
		{
			as_destroy(newas);
			return ENOMEM;
		}
		*as_region = *old_region;
		as_region->next = NULL;
		*new_region = as_region;
		old_region = old_region->next;
		new_region = &as_region->next;
	}

	int err = vm_copy(old, newas);
	if (err) {
		as_destroy(newas);
		return err;
	}

	*ret = newas;
	return 0;
}

void as_destroy(struct addrspace *as) 
{
	if (as->region == NULL)
	{
		return;
	}
	// lock_acquire(as->p_lock);
	for (struct as_region *temp; as->region != NULL;)
	{
		temp = as->region->next;
		kfree(as->region);
		as->region = temp;
	}
	vm_delete(as);
	// lock_release(as->p_lock);
	// lock_destroy(as->p_lock);
	kfree(as);
}
	

void as_activate(void)
{
	struct addrspace *as;

	int i, spl;
	as = proc_getas();
	if (as == NULL)
	{
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	spl = splhigh();

	for (i = 0; i < NUM_TLB; i++)
	{
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void as_deactivate(void)
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
int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
					 int readable, int writeable, int executable)
{

	memsize = ROUNDUP(memsize + (vaddr & ~PAGE_FRAME), PAGE_SIZE) / PAGE_SIZE;
	vaddr &= PAGE_FRAME;

	// Check for region within kuseg
	if (vaddr + memsize > MIPS_KSEG0)
	{
		return EFAULT;
	}

	for (struct as_region *cur_old = as->region; cur_old != NULL; cur_old = cur_old->next)
	{
		if (vaddr < cur_old->base + cur_old->size && vaddr + memsize > cur_old->base)
		{
			return EINVAL;
		}
	}

	struct as_region *curr_new = kmalloc(sizeof(struct as_region));
	if (curr_new == NULL)
	{
		return ENOMEM;
	}

	curr_new->base = vaddr;
	curr_new->size = memsize;
	if (readable)
	{
		curr_new->mode = curr_new->mode | PF_R;
	}
	if (writeable)
	{
		curr_new->mode = curr_new->mode | PF_W;
	}
	if (executable)
	{
		curr_new->mode = curr_new->mode | PF_X;
	}
	curr_new->pre_mode = curr_new->mode;
	curr_new->next = NULL;

	if (as->region != NULL)
	{
		// Add to end of linked list
		struct as_region *seg = as->region;
		while (seg->next != NULL)
		{
			seg = seg->next;
		}
		seg->next = curr_new;
	}
	else
	{
		as->region = curr_new;
	}

	return 0;
}

int as_prepare_load(struct addrspace *as)
{

	for (struct as_region *seg = as->region; seg != NULL; seg = seg->next)
	{
		if ((seg->mode & PF_W) == 0)
		{
			seg->mode = seg->mode | PF_X;
			seg->mode = seg->mode | PF_W;
			seg->pre_mode = 1;
		}
	}


	return 0;
}

int as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	as_activate();
	for (struct as_region *seg = as->region; seg != NULL; seg = seg->next)
	{
		if (seg->pre_mode == 1)
		{
			seg->mode = seg->mode & (~PF_W);
			seg->mode = seg->mode & ~(1 << 3);
			seg->pre_mode = 0;
		}
	}


	return 0;
}

int as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */
	*stackptr = USERSTACK;
	

	/* Initial user-level stack pointer */
	
	return as_define_region(as, *stackptr - 18 * PAGE_SIZE, 18 * PAGE_SIZE, 1, 1, 0);;
}
