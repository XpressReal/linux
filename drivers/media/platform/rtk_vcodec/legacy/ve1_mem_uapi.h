#ifndef _RTK_VE1_MEM_UAPI_H
#define _RTK_VE1_MEM_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct ve1_mem_fd_data {
	unsigned long phyAddr;
	unsigned long ret_offset;
	unsigned long ret_size;
	int ret_fd;
};

#define VE1_MEM_IOC_MAGIC 'R'
#define VE1_MEM_IOC_EXPORT _IOWR(VE1_MEM_IOC_MAGIC, 0, struct ve1_mem_fd_data)

#endif /* _RTK_VE1_MEM_UAPI_H */
