#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <current.h>
#include <proc.h>
#include <elf.h>
#include <spl.h>
#include <synch.h>

/* Place your page table functions here */
int check_mode(struct as_region *region, vaddr_t faultaddress);
int insert_hpt(int index, struct addrspace *as, vaddr_t faultaddress, uint32_t elo, int empty_entry);
uint32_t check_found(int index, vaddr_t faultaddress, struct addrspace *as);
void insert_copy(struct hp_table *curr, struct hp_table *cp, struct addrspace *new, vaddr_t vaddr, vaddr_t paddr);
void vm_bootstrap(void)
{
	/* Initialise any global components of your VM sub-system here.
	 *
	 * You may or may not need to add anything here depending what's
	 * provided or required by the assignment spec.
	 */
	hpt_size = ram_getsize() / PAGE_SIZE * 2;
	hpt = kmalloc(hpt_size * sizeof(struct hp_table));
	table_lock = lock_create("hpt_lock");
}

int vm_copy(struct addrspace *old, struct addrspace *new)
{
	// copy all hpt entries of old to new
	struct hp_table *curr, *cp, *cd;
	lock_acquire(table_lock);
	size_t i = 0;
	while (i < hpt_size) {
		curr = &hpt[i];
		if (curr->pid == (int)old)
		{
			vaddr_t vaddr = curr->vaddr;
			paddr_t paddr_find = KVADDR_TO_PADDR(vaddr);
			int index = (paddr_find >> 12 ^ (uint32_t)new) % hpt_size;
			cp = &hpt[index];
			while (cp->pid)
			{
				if (!cp->next)
				{
					break;
				}
				index = cp->next;
			}
	
			vaddr_t kvaddr = alloc_kpages(1);
			vaddr_t paddr = KVADDR_TO_PADDR(kvaddr);
			if (!paddr)
			{
				lock_release(table_lock);
				return ENOMEM;
			}
			paddr_t paddr_select = curr->paddr & PAGE_FRAME;
			memmove((void *)PADDR_TO_KVADDR(paddr), (const void *)PADDR_TO_KVADDR(paddr_select), PAGE_SIZE);
			if (cp->pid)
			{
				int empty_entry = 0;
				int empty = hpt_size + index / 2;
				size_t j;
				int find = 0;
				for (j = 0; j < hpt_size; j++)
				{
					empty_entry = (empty - j) % hpt_size;
					cd = &hpt[empty_entry];
					if (!cd->pid && !cd->next)
					{
						cp->next = empty_entry;
						insert_copy(curr, cp, new, vaddr, paddr);
						find = 1;
						break;
					}
				}
				if (find == 0)
				{
					lock_release(table_lock);
					return ENOMEM;
				}
			}
			else
			{
				insert_copy(curr, cp, new, vaddr, paddr);
			}
		}
		i++;
	}
	lock_release(table_lock);
	return 0;
}

void vm_delete(struct addrspace *as)
{
	size_t i;
	lock_acquire(table_lock);
	struct hp_table *curr;
	for (i = 0; i < hpt_size; i++)
	{
		curr = &hpt[i];
		if (curr->pid == (int)as)
		{
			curr->pid = 0;
			free_kpages(PADDR_TO_KVADDR(curr->paddr & PAGE_FRAME));
		}
	}
	lock_release(table_lock);
}

void insert_copy(struct hp_table *curr, struct hp_table *cp, struct addrspace *new, vaddr_t vaddr, vaddr_t paddr) {
	paddr_t paddr_select = curr->paddr & TLBLO_DIRTY;
	cp->pid = (int)new;
	cp->vaddr = vaddr;
	cp->paddr = paddr | paddr_select | TLBLO_VALID;
}


uint32_t check_found(int index, vaddr_t faultaddress, struct addrspace *as)
{
	uint32_t record = 0;
	struct hp_table *cp;
	for (size_t i = 0;; i++)
	{
		cp = &hpt[index];
		if (cp->vaddr == faultaddress && cp->pid == (int)as)
		{
			record = cp->paddr;
			break;
		}
		if (!cp->next)
		{
			break;
		}
		index = cp->next;
	}
	return record;
}

int check_mode(struct as_region *region, vaddr_t faultaddress)
{
	int flags = 0;
	while (region != NULL)
	{
		vaddr_t base = region->base;
		vaddr_t top = region->base + region->size * PAGE_SIZE;
		if (faultaddress >= base && faultaddress < top)
		{
			flags = region->mode;
			break;
		}
		region = region->next;
	}
	return flags;
}

int insert_hpt(int index, struct addrspace *as, vaddr_t faultaddress, uint32_t entrylow, int empty)
{
	size_t size_num = 0;
	struct hp_table *cp, *cd;
	cp = &hpt[index];
	if (cp->pid)
	{
		if (size_num == hpt_size)
		{
			lock_release(table_lock);
			return ENOMEM;
		}
		while (size_num < hpt_size)
		{
			empty = (empty - size_num) % hpt_size;
			cd = &hpt[empty];
			if (!cd->pid && !cd->next)
			{
				cd->pid = (int)as;
				cp->next = empty;
				cd->vaddr = faultaddress;
				cd->paddr = entrylow;
				break;
			}
			size_num++;
		}
	}
	else
	{
		cp->pid = (int)as;
		cp->vaddr = faultaddress;
		cp->paddr = entrylow;
	}
	return 0;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	faultaddress = faultaddress & PAGE_FRAME;
	
	uint32_t entrylow = 0;
	
	if (!curproc || faultaddress == 0) {
        return EFAULT;
    }

	struct addrspace *as = proc_getas();
	
    if (as == NULL || as -> region == NULL) {
        return EFAULT;
    }

	switch (faulttype) {
        case VM_FAULT_READONLY:
            return EFAULT;
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            break;
        default:
            return EINVAL;
    }
	// struct hp_table *bb;
	paddr_t paddr_find = KVADDR_TO_PADDR(faultaddress);
	int index = (paddr_find >> 12 ^ (uint32_t)as) % hpt_size;
	int empty = index/2 + hpt_size;
	lock_acquire(table_lock);
	entrylow = check_found(index, faultaddress, as);
	struct as_region *region = as->region;
	int mode;
	if (entrylow == 0) {
		while (hpt[index].pid != 0) {
			if (hpt[index].next == 0) {
				break;
			}
			index = hpt[index].next;
		}

		mode = check_mode(region, faultaddress);
		if (mode == 0) {
			lock_release(table_lock);
			return EFAULT;
		} 
		
		vaddr_t page = alloc_kpages(1);
		bzero((void *)page, PAGE_SIZE);
		if (!page) {
			lock_release(table_lock);
			return ENOMEM;
		}
		paddr_t paddr = KVADDR_TO_PADDR(page);
		entrylow = paddr | TLBLO_DIRTY | TLBLO_VALID;
		
		int insert_flag;
		insert_flag = insert_hpt(index, as, faultaddress, entrylow, empty);
		if (insert_flag != 0) {
			return ENOMEM;
		}
		lock_release(table_lock);
	} else {  
		mode = check_mode(region, faultaddress);
		if (!(mode & PF_W) ) {
			entrylow = entrylow & ~TLBLO_DIRTY;
		}
		lock_release(table_lock);
	}

	uint32_t entryhi = faultaddress & PAGE_FRAME;
	int spl = splhigh();
	tlb_random(entryhi, entrylow);
	splx(spl);
	return 0;
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}
