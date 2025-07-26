//
// Created by fuqiuluo on 25-2-16.
//
#include "vma.h"
#include <linux/tty.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <asm/page.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include "addr_pfn_map.h"

#if BUILD_REMAP == 1
static int (*my_remap_pfn_range)(struct vm_area_struct *, unsigned long addr,
					unsigned long pfn, unsigned long size, pgprot_t) = NULL;

int process_vaddr_to_pfn(pid_t from, void __user* from_addr, unsigned long* pfn, size_t size) {
	phys_addr_t pa;
	struct task_struct *task;
	struct mm_struct *mm;
	struct pid *pid_struct;

	if(!pfn) {
		return -EINVAL;
	}

	pid_struct = find_get_pid(from);
	if (!pid_struct) {
		pr_err("[ovo] failed to find pid_struct: %s\n", __func__);
		return -ESRCH;
	}

	task = get_pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);
	if(!task) {
		pr_err("[ovo] failed to get task from pid_struct: %s\n", __func__);
		return -ESRCH;
	}

	mm = get_task_mm(task);
	if (!mm) {
		pr_err("[ovo] failed to get mm from task: %s\n", __func__);
		return -ESRCH;
	}

	MM_READ_LOCK(mm)
	pa = vaddr_to_phy_addr(mm, (uintptr_t)from_addr);
	MM_READ_UNLOCK(mm)
	mmput(mm);
	put_task_struct(task);

	if(pa == 0) {
		return -EFAULT;
	}

	if (!pa) return -EFAULT;

	*pfn = __phys_to_pfn(pa);
	if (pfn_valid(*pfn) && IS_VALID_PHYS_ADDR_RANGE(pa, size)) {
		return 0;
	}

	return -EFAULT;
}

int remap_process_memory(struct vm_area_struct *vma, unsigned long pfn, size_t size) {
	int ret;

	if (!vma) {
		return -EFAULT;
	}

	// vm_fault_t vmf_insert_pfn_prot(struct vm_area_struct *vma, unsigned long addr,
	//unsigned long pfn, pgprot_t pgprot
	//)

#if ENABLE_REMAP2 == 1
	ret = vmf_insert_pfn(vma, vma->vm_start, pfn);
	if (ret) {
		return -EAGAIN;
	}
#endif

	if (!my_remap_pfn_range) {
		my_remap_pfn_range = (void *) ovo_kallsyms_lookup_name("remap_pfn_range");
		if (!my_remap_pfn_range) {
			pr_err("[ovo] failed to find io_remap_pfn_range: %s\n", __func__);
			return -ENOSYS;
		}
	}

	ret = my_remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);

	if (ret) return -EAGAIN;

	return 0;
}
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(6, 12, 0))
static inline int vma_iter_area_lowest(struct vma_iterator *vmi, unsigned long min,
					   unsigned long max, unsigned long size)
{
	return mas_empty_area(&vmi->mas, min, max - 1, size);
}

static inline void vma_iter_reset(struct vma_iterator *vmi)
{
	mas_reset(&vmi->mas);
}

#if defined(CONFIG_ARM64_GCS)
/*
 * arm64's Guarded Control Stack implements similar functionality and
 * has similar constraints to shadow stacks.
 */
# define VM_SHADOW_STACK	VM_HIGH_ARCH_6
#endif

#ifndef VM_SHADOW_STACK
# define VM_SHADOW_STACK	VM_NONE
#endif

#define VM_STARTGAP_FLAGS_BAK (VM_GROWSDOWN | VM_SHADOW_STACK)

unsigned long unmapped_area_mm(struct mm_struct *mm, size_t length)
{
	unsigned long gap;
	unsigned long low_limit, high_limit;
	struct vm_area_struct *tmp;

	VMA_ITERATOR(vmi, mm, 0);

	low_limit = mm->mmap_base;
	if (low_limit < 0)
		low_limit = 0;
	high_limit = TASK_SIZE;

	retry:
		if (vma_iter_area_lowest(&vmi, low_limit, high_limit, length))
			return -ENOMEM;

	/*
	 * Adjust for the gap first so it doesn't interfere with the
	 * later alignment. The first step is the minimum needed to
	 * fulill the start gap, the next steps is the minimum to align
	 * that. It is the minimum needed to fulill both.
	 */
	gap = vma_iter_addr(&vmi);
	tmp = vma_next(&vmi);
	if (tmp && (tmp->vm_flags & VM_STARTGAP_FLAGS_BAK)) { /* Avoid prev check if possible */
		if (vm_start_gap(tmp) < gap + length - 1) {
			low_limit = tmp->vm_end;
			vma_iter_reset(&vmi);
			goto retry;
		}
	} else {
		tmp = vma_prev(&vmi);
		if (tmp && vm_end_gap(tmp) > gap) {
			low_limit = vm_end_gap(tmp);
			vma_iter_reset(&vmi);
			goto retry;
		}
	}

	return gap;
}
#else
unsigned long unmapped_area_mm(struct mm_struct *mm, size_t len) {
	unsigned long (*get_area)(struct file *, unsigned long,
				  unsigned long, unsigned long, unsigned long);

	unsigned long addr;
	/* Careful about overflows.. */
	if (len > TASK_SIZE)
		return -ENOMEM;

	get_area = mm->get_unmapped_area;
	addr = get_area(NULL, 0, len, 0, 0);
	if (IS_ERR_VALUE(addr))
		return addr;

	if (addr > TASK_SIZE - len)
		return -ENOMEM;
	if (offset_in_page(addr))
		return -EINVAL;

	return addr;
}
#endif

int get_unmapped_area_pid(pid_t pid, unsigned long* addr, size_t size) {
	struct task_struct *task;
	struct mm_struct *mm;
	struct pid *pid_struct;

	if(!pid) {
		return -ESRCH;
	}

	pid_struct = find_get_pid(pid);
	if (!pid_struct) {
		pr_err("[ovo] failed to find pid_struct: %s\n", __func__);
		return -ESRCH;
	}

	task = get_pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);
	if(!task) {
		pr_err("[ovo] failed to get task from pid_struct: %s\n", __func__);
		return -ESRCH;
	}

	mm = get_task_mm(task);
	put_task_struct(task);
	if (!mm) {
		pr_err("[ovo] failed to get mm from task: %s\n", __func__);
		return -ESRCH;
	}

	MM_READ_LOCK(mm)
	*addr = unmapped_area_mm(mm, size);
	MM_READ_UNLOCK(mm)
	mmput(mm);

	return 0;
}

int get_unmapped_area_mm(struct mm_struct* mm, unsigned long* addr, size_t size) {
	*addr = unmapped_area_mm(mm, size);
	return 0;
}

static int ovo_mremap(const struct vm_special_mapping *sm,
			 struct vm_area_struct *new_vma) {
	return 0;
}

static vm_fault_t ovo_fault(const struct vm_special_mapping *sm,
				struct vm_area_struct *vma,
				struct vm_fault *vmf) {

	unsigned long pfn;

	pfn = lookup_pfn(vma->vm_start);
	if (!pfn) {
		pr_err("[ovo] failed to find pfn: addr = 0x%lx\n", vma->vm_start);
		return VM_FAULT_SIGBUS;
	}

	vmf->page = pfn_to_page(pfn);
	get_page(vmf->page);

	return 0;
}

static struct vm_special_mapping aarch64_ovo_map __ro_after_init = {
	.name	= "",
	.mremap = ovo_mremap,
	.fault = ovo_fault
};

int alloc_process_special_memory(pid_t pid, unsigned long addr, size_t size, int writable) {
	int ret;
	struct task_struct *task;
	struct mm_struct *mm;
	struct pid *pid_struct;

	if (!pid || !size || !addr) {
		return -EINVAL;
	}

	pid_struct = find_get_pid(pid);
	if (!pid_struct) {
		pr_err("[ovo] failed to find pid_struct: %s\n", __func__);
		return -ESRCH;
	}

	task = get_pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);
	if(!task) {
		pr_err("[ovo] failed to get task from pid_struct: %s\n", __func__);
		return -ESRCH;
	}

	mm = get_task_mm(task);
	put_task_struct(task);
	if (!mm) {
		pr_err("[ovo] failed to get mm from task: %s\n", __func__);
		return -ESRCH;
	}

	MM_READ_LOCK(mm)
	ret = alloc_process_special_memory_mm(mm, addr, size, writable);
	MM_READ_UNLOCK(mm)
	mmput(mm);

	return ret;
}

int alloc_process_special_memory_mm(struct mm_struct* mm, unsigned long addr, size_t size, int writable) {
	static struct vm_area_struct *(*my_install_special_mapping)(struct mm_struct *mm,
				   unsigned long addr, unsigned long len,
				   unsigned long flags,
				   const struct vm_special_mapping *spec) = NULL;
	unsigned long flags;
	struct vm_area_struct * ret;

	if (addr & (PAGE_SIZE - 1)) {
		return -EINVAL;
	}

	if (my_install_special_mapping == NULL) {
		my_install_special_mapping = (void*) ovo_kallsyms_lookup_name("_install_special_mapping");
		if (!my_install_special_mapping) {
			pr_err("[ovo] failed to find install_special_mapping: %s\n", __func__);
			return -ENOSYS;
		}
	}

	flags = VM_READ | VM_SHARED | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
	if (writable) {
		flags |= VM_WRITE;
	} else {
		flags |= VM_EXEC;
	}
	ret = my_install_special_mapping(mm, addr, size, flags, &aarch64_ovo_map);

	return ret != NULL ? 0 : -ENOMEM;
}

struct vm_area_struct * find_vma_pid(pid_t pid, unsigned long addr) {
	struct task_struct *task;
	struct mm_struct *mm;
	struct pid *pid_struct;
	struct vm_area_struct *vma;

	if(!pid) {
		return NULL;
	}

	pid_struct = find_get_pid(pid);
	if (!pid_struct) {
		pr_err("[ovo] failed to find pid_struct: %s\n", __func__);
		return NULL;
	}

	task = get_pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);
	if(!task) {
		pr_err("[ovo] failed to get task from pid_struct: %s\n", __func__);
		return NULL;
	}

	mm = get_task_mm(task);
	if (!mm) {
		pr_err("[ovo] failed to get mm from task: %s\n", __func__);
		return NULL;
	}

	vma = find_vma(mm, addr);

	mmput(mm);
	put_task_struct(task);

	return vma;
}
