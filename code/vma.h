//
// Created by fuqiuluo on 25-2-16.
//

#ifndef VMA_H
#define VMA_H

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/version.h>
#include "memory.h"

#if BUILD_REMAP == 1
int process_vaddr_to_pfn(pid_t from, void __user* from_addr, unsigned long* pfn, size_t size);

// 内存重映射
int remap_process_memory(struct vm_area_struct *vma, unsigned long pfn, size_t size);
#endif

int get_unmapped_area_pid(pid_t pid, unsigned long* addr, size_t size);
int get_unmapped_area_mm(struct mm_struct* mm, unsigned long* addr, size_t size);
int alloc_process_special_memory(pid_t pid, unsigned long addr, size_t size, int writable);
int alloc_process_special_memory_mm(struct mm_struct* mm, unsigned long addr, size_t size, int writable);
struct vm_area_struct* find_vma_pid(pid_t pid, unsigned long addr);

#endif //VMA_H
