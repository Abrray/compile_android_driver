#include "mmuhack.h"
#include <linux/kallsyms.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include "kkit.h"
#include <linux/ftrace.h>
#include <asm/unistd.h>
#include <linux/unistd.h>
#include <linux/mm.h> // For PAGE_SIZE
#include <linux/version.h>
#include <linux/moduleloader.h>
#include <linux/stop_machine.h>

#if defined(CONFIG_ARM64) || defined(CONFIG_AARCH64)
#include <linux/pgtable.h>
#endif

static struct mm_struct *init_mm_ptr = NULL;

pte_t *page_from_virt_kernel(unsigned long addr) {
    pgd_t * pgd;
#if __PAGETABLE_P4D_FOLDED == 1
    p4d_t *p4d;
#endif
    pud_t *pudp, pud;
    pmd_t *pmdp, pmd;
    pte_t *ptep;

    if (addr & PAGE_SIZE - 1) {
        addr = addr + PAGE_SIZE & ~(PAGE_SIZE - 1);
    }

    if (!init_mm_ptr) {
        init_mm_ptr = (struct mm_struct *) ovo_kallsyms_lookup_name("init_mm");
    }

    pgd = pgd_offset(init_mm_ptr, addr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        return NULL;
    }
    // return if pgd is entry is here

#if __PAGETABLE_P4D_FOLDED == 1
    p4d = p4d_offset(pgd, addr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        return 0;
    }

    pudp = pud_offset(p4d, addr);
#else
    pudp = pud_offset(pgd, addr);
#endif
    pud = READ_ONCE(*pudp);
    if (pud_none(pud) || pud_bad(pud)) {
        return NULL;
    }

#if defined(pud_leaf)
    if (pud_leaf(pud))
        return (pte_t *)pudp;
#endif

    pmdp = pmd_offset(pudp, addr);
    pmd = READ_ONCE(*pmdp);
    if (pmd_none(pmd) || pmd_bad(pmd)) {
        return NULL;
    }

#if defined(pmd_leaf)
    if (pmd_leaf(pmd))
        return (pte_t *)pmdp;
#endif

    ptep = pte_offset_kernel(pmdp, addr);
    if (!ptep) {
        return NULL;
    }

    //pr_debug("[ovo] page_from_virt succes, virt (0x%lx), ptep @ %lx", (uintptr_t) addr, (uintptr_t) ptep);
    return ptep;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0) && defined(OVO_0X202501232117)
pte_t *page_from_virt_user(struct mm_struct *mm, unsigned long addr) {
    pte_t *pte;
    spinlock_t *ptlp;

    if (!mm) return NULL;

    follow_pte(mm, addr, &pte, &ptlp);

    //pte_unmap_unlock(pte, ptlp);

     //if (ptlp)
     //   spin_unlock(ptlp);
#error "OVO_0X202501232117"
    return pte;
}
#else
pte_t *page_from_virt_user(struct mm_struct *mm, unsigned long addr) {
    pgd_t * pgd;
#if __PAGETABLE_P4D_FOLDED == 1
    p4d_t *p4d;
#endif
    pud_t *pudp, pud;
    pmd_t *pmdp, pmd;
    pte_t *ptep;

    pgd = pgd_offset(mm, addr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        return NULL;
    }
    // return if pgd is entry is here

#if __PAGETABLE_P4D_FOLDED == 1
    p4d = p4d_offset(pgd, addr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        return 0;
    }

    pudp = pud_offset(p4d, addr);
#else
    pudp = pud_offset(pgd, addr);
#endif
    pud = READ_ONCE(*pudp);
    if (pud_none(pud) || pud_bad(pud)) {
        return NULL;
    }

#if defined(pud_leaf) && defined(BIG_PAGE)
    // 处理 PUD 级大页，直接操作 pud_val
    if (pud_leaf(pud)) {
        ptep = (pte_t *) pudp;
        goto ret;
    }
#endif

    pmdp = pmd_offset(pudp, addr);
    pmd = READ_ONCE(*pmdp);
    if (pmd_none(pmd) || pmd_bad(pmd)) {
        return NULL;
    }

#if defined(pmd_leaf) && defined(BIG_PAGE)
    if (pmd_leaf(pmd)) {
        ptep = (pte_t *) pmdp;
        goto ret;
    }
#endif

    ptep = pte_offset_kernel(pmdp, addr);
    if (!ptep) {
        return NULL;
    }

    ret:
    return ptep;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0)
static inline int my_set_pte_at(struct mm_struct *mm,
                                 uintptr_t __always_unused addr,
                                 pte_t *ptep, pte_t pte)
{
    typedef void (*f__sync_icache_dcache)(pte_t pteval);
    typedef void (*f_mte_sync_tags)(pte_t pte, unsigned int nr_pages);

    static f__sync_icache_dcache __sync_icache_dcache = NULL;
    static f_mte_sync_tags mte_sync_tags = NULL;

    if (__sync_icache_dcache == NULL) {
        __sync_icache_dcache = (f__sync_icache_dcache) ovo_kallsyms_lookup_name("__sync_icache_dcache");
    }

#if !defined(PTE_UXN)
#define PTE_UXN			(_AT(pteval_t, 1) << 54)	/* User XN */
#endif

#if !defined(pte_user_exec)
#define pte_user_exec(pte)	(!(pte_val(pte) & PTE_UXN))
#endif

	if (__sync_icache_dcache == NULL) {
		pr_warn("[ovo] symbol `__sync_icache_dcache` not found\n");
	} else {
		if (pte_present(pte) && pte_user_exec(pte) && !pte_special(pte))
                __sync_icache_dcache(pte);
	}

    /*
     * If the PTE would provide user space access to the tags associated
     * with it then ensure that the MTE tags are synchronised.  Although
     * pte_access_permitted() returns false for exec only mappings, they
     * don't expose tags (instruction fetches don't check tags).
     */
#if !defined(pte_tagged)
    #define pte_tagged(pte)		((pte_val(pte) & PTE_ATTRINDX_MASK) == \
    PTE_ATTRINDX(MT_NORMAL_TAGGED))
#endif

    if (system_supports_mte() && pte_access_permitted(pte, false) &&
        !pte_special(pte) && pte_tagged(pte)) {
        if (mte_sync_tags == NULL) {
            mte_sync_tags = (f_mte_sync_tags) ovo_kallsyms_lookup_name("mte_sync_tags");
        }
        if (mte_sync_tags == NULL) {
            pr_err("[ovo] symbol `mte_sync_tags` not found\n");
            return -2;
        }
        mte_sync_tags(pte, 1);
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    __check_safe_pte_update(mm, ptep, pte);
    __set_pte(ptep, pte);
#else
    __check_racy_pte_update(mm, ptep, pte);
    set_pte(ptep, pte);
#endif
    return 0;
}
#endif


int protect_rodata_memory(unsigned nr) {
    pte_t pte;
    pte_t* ptep;
    uintptr_t addr;

    addr = (uintptr_t) ((uintptr_t) ovo_find_syscall_table() + nr & PAGE_MASK);
    ptep = page_from_virt_kernel(addr);

    if (!pte_valid(READ_ONCE(*ptep))) { // arm64
        printk(KERN_INFO "[ovo] failed to get ptep from 0x%lx\n", addr);
        return -2;
    }
    pte = READ_ONCE(*ptep);
    pte = pte_wrprotect(pte);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0)
    if(my_set_pte_at(init_mm_ptr, addr, ptep, pte) != 0) {
        return -1;
    }
#else
    set_pte_at(init_mm_ptr, addr, ptep, pte);
#endif

    //flush_icache_range(addr, addr + PAGE_SIZE);
    //__clean_dcache_area_pou(data_addr, sizeof(data));
    __flush_tlb_kernel_pgtable(addr); // arm64
    return 0;
}

int unprotect_rodata_memory(unsigned nr) {
    pte_t pte;
    pte_t* ptep;
    uintptr_t addr;

    addr = (uintptr_t) ((uintptr_t) ovo_find_syscall_table() + nr & PAGE_MASK);
    ptep = page_from_virt_kernel(addr);

    if (!pte_valid(READ_ONCE(*ptep))) {
        printk(KERN_INFO "[ovo] failed to get ptep from 0x%lx\n", addr);
        return -2;
    }
    pte = READ_ONCE(*ptep);

    // 如果pte_mkwrite_novma无法使用，换成下面这两行
    // pte = set_pte_bit(pte, __pgprot(PTE_WRITE));
    // pte = clear_pte_bit(pte, __pgprot(PTE_RDONLY));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    pte = pte_mkwrite_novma(pte);
#else
    pte = pte_mkwrite(pte);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0)
    if(my_set_pte_at(init_mm_ptr, addr, ptep, pte) != 0) {
        return -1;
    }
#else
    set_pte_at(init_mm_ptr, addr, ptep, pte);
#endif
    __flush_tlb_kernel_pgtable(addr);
    return 0;
}

