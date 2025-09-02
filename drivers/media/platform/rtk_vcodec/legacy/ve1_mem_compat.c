#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/module.h>

#include "ve1_mem_uapi.h"
#include "ve1_mem_compat.h"

struct compat_ve1_mem_fd_data {
	compat_ulong_t phyAddr;
	compat_ulong_t ret_offset;
	compat_ulong_t ret_size;
	compat_int_t ret_fd;
};

#define COMPAT_VE1_MEM_IOC_EXPORT                                              \
	_IOWR(VE1_MEM_IOC_MAGIC, 0, struct compat_ve1_mem_fd_data)

extern int ve1_mem_reg_fd(unsigned long phys_addr, unsigned long *offset,
			  unsigned long *size);

long compat_ve1_mem_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	if (!filp->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case COMPAT_VE1_MEM_IOC_EXPORT: {
		struct compat_ve1_mem_fd_data data32;
		struct ve1_mem_fd_data data;

		if (copy_from_user(&data32, compat_ptr(arg), sizeof(data32)))
			return -EFAULT;

		data = (struct ve1_mem_fd_data){
			.phyAddr = data32.phyAddr,
			.ret_offset = data32.ret_offset,
			.ret_size = data32.ret_size,
			.ret_fd = data32.ret_fd,
		};

		data.ret_fd = ve1_mem_reg_fd(data.phyAddr, &data.ret_offset,
					     &data.ret_size);
		pr_debug(
			"ret_fd:%d.phyAddr:0x%lx.ret_offset:%ld.ret_size:%ld\n",
			data.ret_fd, data.phyAddr, data.ret_offset,
			data.ret_size);

		data32.phyAddr = data.phyAddr;
		data32.ret_offset = data.ret_offset;
		data32.ret_size = data.ret_size;
		data32.ret_fd = data.ret_fd;

		if (copy_to_user(compat_ptr(arg), &data32, sizeof(data32)))
			return -EFAULT;

		return 0;
	}

	default: {
		printk(KERN_ERR "[COMPAT_VE1_MEM] No such IOCTL, cmd is %d\n",
		       cmd);
		return -ENOIOCTLCMD;
	}
	}
}

MODULE_LICENSE("GPL v2");
