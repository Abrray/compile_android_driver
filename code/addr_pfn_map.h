//
// Created by fuqiuluo on 25-2-16.
//

#ifndef ADDR_PFN_MAP_H
#define ADDR_PFN_MAP_H

#include <linux/rhashtable.h>
#include <linux/slab.h>

struct addr_pfn_map {
    unsigned long addr;
    unsigned long pfn;
    struct rhash_head node;
};

int init_addr_pfn_map(void);

int insert_addr_pfn(unsigned long addr, unsigned long pfn);

unsigned long lookup_pfn(unsigned long addr);

int remove_addr_pfn(unsigned long addr);

void destroy_addr_pfn_map(void);
#endif //ADDR_PFN_MAP_H
