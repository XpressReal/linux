#ifndef _RTK_VE1_MEM_H
#define _RTK_VE1_MEM_H

#include <linux/types.h>
#include <linux/dma-buf.h>

typedef struct ve1_mem_reg_entry {
	struct device *dev;
	unsigned long phys_addr;
	void *addr;
	unsigned long size;
	struct dma_buf *dmabuf;
	struct ve1_mem_reg_entry *next;
} ve1_mem_reg_entry_t;

struct ve1_mem_dma_buf_attachment {
	struct sg_table sgt;
};

void ve1_mem_reg_add(ve1_mem_reg_entry_t *entry);
ve1_mem_reg_entry_t *ve1_mem_reg_remove(unsigned long phys_addr);

int ve1_mem_device_create(void);
void ve1_mem_device_destroy(void);

#endif /* _RTK_VE1_MEM_H */
