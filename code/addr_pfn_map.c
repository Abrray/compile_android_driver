//
// Created by fuqiuluo on 25-2-16.
//
#include "addr_pfn_map.h"

static const struct rhashtable_params addr_pfn_params = {
	.key_offset = offsetof(struct addr_pfn_map, addr),
	.head_offset = offsetof(struct addr_pfn_map, node),
	.key_len = sizeof(unsigned long),
	.automatic_shrinking = true,
};

static struct rhashtable addr_pfn_table;

int init_addr_pfn_map(void)
{
	return rhashtable_init(&addr_pfn_table, &addr_pfn_params);
}

int insert_addr_pfn(unsigned long addr, unsigned long pfn)
{
	struct addr_pfn_map *map;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->addr = addr;
	map->pfn = pfn;

	if (rhashtable_insert_fast(&addr_pfn_table, &map->node, addr_pfn_params) < 0) {
		kfree(map);
		return -EEXIST;
	}
	return 0;
}

unsigned long lookup_pfn(unsigned long addr)
{
	struct addr_pfn_map *map;

	map = rhashtable_lookup_fast(&addr_pfn_table, &addr, addr_pfn_params);
	return map ? map->pfn : 0;
}

int remove_addr_pfn(unsigned long addr)
{
	struct addr_pfn_map *map;

	map = rhashtable_lookup_fast(&addr_pfn_table, &addr, addr_pfn_params);
	if (map) {
		rhashtable_remove_fast(&addr_pfn_table, &map->node, addr_pfn_params);
		kfree(map);
		return 0;
	}
	return -ENOENT;
}

// void clear_addr_pfn_map(void)
// {
// 	destroy_addr_pfn_map();
// 	init_addr_pfn_map();
// }

void destroy_addr_pfn_map(void)
{
	rhashtable_destroy(&addr_pfn_table);
}
