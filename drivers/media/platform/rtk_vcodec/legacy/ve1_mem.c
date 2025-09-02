#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/ioctl.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fdtable.h>
#include <linux/syscalls.h>
#include <linux/export.h>
#include <linux/dma-map-ops.h>

#include "ve1_mem.h"
#include "ve1_mem_uapi.h"
#include "ve1_mem_compat.h"

#define VE1_MEM_TAG "[VE1_MEM]"

#define ve1mem_printk(level, tag, fmt, arg...)                                 \
	printk(level "%s [%d]%s." fmt, tag, __LINE__, __func__, ##arg)

#define ve1mem_err(tag, fmt, arg...) ve1mem_printk(KERN_ERR, tag, fmt, ##arg)

#define ve1mem_warn(tag, fmt, arg...)                                          \
	ve1mem_printk(KERN_WARNING, tag, fmt, ##arg)

#define ve1mem_info(tag, fmt, arg...) ve1mem_printk(KERN_INFO, tag, fmt, ##arg)

#define ve1mem_dbg(tag, fmt, arg...)

static DEFINE_MUTEX(ve1_mem_lock);
static ve1_mem_reg_entry_t *ve1_mem_reg_head;
static int ve1_mem_reg_count;

void ve1_mem_list(void)
{
	ve1_mem_reg_entry_t *curr = NULL;
	curr = ve1_mem_reg_head;
	while (curr != NULL) {
		ve1mem_dbg(
			VE1_MEM_TAG,
			"curr:0x%px.phys_addr:0x%lx.virt_addr:0x%px.size:%ld.dmabuf:0x%px.next:0x%px\n",
			curr, curr->phys_addr, curr->addr, curr->size,
			curr->dmabuf, curr->next);
		curr = curr->next;
	}
}

void ve1_mem_reg_add(ve1_mem_reg_entry_t *entry)
{
	mutex_lock(&ve1_mem_lock);
	ve1_mem_list();
	ve1mem_dbg(VE1_MEM_TAG,
		   "entry:0x%px.phys_addr:0x%lx.virt_addr:0x%px.size:%ld\n",
		   entry, entry->phys_addr, entry->addr, entry->size);
	entry->next = ve1_mem_reg_head;
	ve1_mem_reg_head = entry;
	ve1_mem_reg_count++;
	mutex_unlock(&ve1_mem_lock);
}

ve1_mem_reg_entry_t *ve1_mem_reg_remove(unsigned long phys_addr)
{
	ve1_mem_reg_entry_t *prev = NULL;
	ve1_mem_reg_entry_t *curr = NULL;

	mutex_lock(&ve1_mem_lock);
	ve1_mem_list();
	curr = ve1_mem_reg_head;
	while (curr != NULL) {
		if (curr->phys_addr != phys_addr) {
			prev = curr;
			curr = curr->next;
			continue;
		}

		if (prev == NULL) {
			ve1_mem_reg_head = curr->next;
		} else {
			prev->next = curr->next;
		}
		ve1_mem_reg_count--;
		ve1mem_dbg(
			VE1_MEM_TAG,
			"found.curr:0x%px.phys_addr:0x%lx.virt_addr:0x%px.size:%ld.dmabuf:0x%px\n",
			curr, curr->phys_addr, curr->addr, curr->size,
			curr->dmabuf);
		mutex_unlock(&ve1_mem_lock);

		return curr;
	}
	ve1mem_dbg(
		VE1_MEM_TAG,
		"fail.curr:0x%px.phys_addr:0x%lx.virt_addr:0x%px.size:%ld.dmabuf:0x%px\n",
		curr, curr->phys_addr, curr->addr, curr->size, curr->dmabuf);
	mutex_unlock(&ve1_mem_lock);
	return NULL;
}

static int ve1_mem_dma_buf_attach(struct dma_buf *dmabuf,
				  struct dma_buf_attachment *attach)
{
	struct ve1_mem_dma_buf_attachment *a;
	ve1_mem_reg_entry_t *curr = dmabuf->priv;
	struct device *dev = curr->dev;

	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	ret = dma_get_sgtable(dev, &a->sgt, curr->addr, curr->phys_addr,
			      curr->size);
	if (ret < 0) {
		ve1mem_err(VE1_MEM_TAG,
			   "failed to get scatterlist from DMA API\n");
		kfree(a);
		return -EINVAL;
	}

	attach->priv = a;

	return 0;
}

static void ve1_mem_dma_buf_detatch(struct dma_buf *dmabuf,
				    struct dma_buf_attachment *attach)
{
	struct ve1_mem_dma_buf_attachment *a = attach->priv;

	sg_free_table(&a->sgt);
	kfree(a);
}

static struct sg_table *ve1_mem_map_dma_buf(struct dma_buf_attachment *attach,
					    enum dma_data_direction dir)
{
	struct ve1_mem_dma_buf_attachment *a = attach->priv;
	struct sg_table *table;
	int ret;

	table = &a->sgt;

	ret = dma_map_sgtable(attach->dev, table, dir, 0);
	if (ret)
		table = ERR_PTR(ret);
	return table;
}

static void ve1_mem_unmap_dma_buf(struct dma_buf_attachment *attach,
				  struct sg_table *table,
				  enum dma_data_direction dir)
{
	dma_unmap_sgtable(attach->dev, table, dir, 0);
}

static int ve1_mem_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	ve1_mem_reg_entry_t *curr = dmabuf->priv;
	size_t size = vma->vm_end - vma->vm_start;
	struct device *dev = curr->dev;
	dma_addr_t daddr = curr->phys_addr;
	void *vaddr = curr->addr;

	ve1mem_dbg(VE1_MEM_TAG, "daddr:0x%lx.vaddr:0x%px.size:%ld\n", daddr,
		   vaddr, size);
	return dma_mmap_coherent(dev, vma, vaddr, daddr, size);
}

static void ve1_mem_release(struct dma_buf *dmabuf)
{
}

static const struct dma_buf_ops ve1_dma_buf_ops = {
	.attach = ve1_mem_dma_buf_attach,
	.detach = ve1_mem_dma_buf_detatch,
	.map_dma_buf = ve1_mem_map_dma_buf,
	.unmap_dma_buf = ve1_mem_unmap_dma_buf,
	.mmap = ve1_mem_mmap,
	.release = ve1_mem_release,
};

int ve1_mem_reg_fd(unsigned long phys_addr, unsigned long *offset,
		   unsigned long *size)
{
	int ret_fd = -1;
	ve1_mem_reg_entry_t *curr = NULL;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	if (mutex_lock_interruptible(&ve1_mem_lock)) {
		return -ERESTARTSYS;
	}
	curr = ve1_mem_reg_head;

	while (curr != NULL) {
		if ((phys_addr >= curr->phys_addr) &&
		    (phys_addr < (curr->phys_addr + curr->size))) {
			exp_info.ops = &ve1_dma_buf_ops;
			exp_info.size = curr->size;
			exp_info.flags = O_RDWR;
			exp_info.priv = curr;
			curr->dmabuf = dma_buf_export(&exp_info);
			if (IS_ERR(curr->dmabuf)) {
				ret_fd = PTR_ERR(curr->dmabuf);
				goto out;
			}

			ret_fd = dma_buf_fd(curr->dmabuf, O_CLOEXEC);

			if (offset) {
				*offset = phys_addr - curr->phys_addr;
			}

			if (size) {
				*size = curr->size;
			}
			ve1mem_dbg(
				VE1_MEM_TAG,
				"get ret_fd(%d,0x%lx,%ld).curr:0x%px.phys_addr:0x%lx.size:%ld.dmabuf:0x%px\n",
				ret_fd, *offset, *size, curr, curr->phys_addr,
				curr->size, curr->dmabuf);
			break;
		} else {
			curr = curr->next;
		}
	}
out:
	mutex_unlock(&ve1_mem_lock);
	return ret_fd;
}

static long ve1_mem_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	long ret = -ENOTTY;
	struct ve1_mem_fd_data data;

	ve1mem_dbg(VE1_MEM_TAG, "[+] filp:0x%px.cmd:0x%x.arg:0x%lx\n", filp,
		   cmd, arg);

	switch (cmd) {
	case VE1_MEM_IOC_EXPORT:
		if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
			ve1mem_err(VE1_MEM_TAG, "copy_from_user ERROR!\n");
			break;
		}

		data.ret_fd = ve1_mem_reg_fd(data.phyAddr, &data.ret_offset,
					     &data.ret_size);
		ve1mem_dbg(
			VE1_MEM_TAG,
			"ret_fd:%d.phyAddr:0x%lx.ret_offset:%ld.ret_size:%ld\n",
			data.ret_fd, data.phyAddr, data.ret_offset,
			data.ret_size);

		if (data.ret_fd < 0) {
			break;
		}

		if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
			/* copy from rpc_mem_ioctl() in rpc_mem.c
>------->------- * The usercopy failed, but we can't do much about it, as
>------->------- * dma_buf_fd() already called fd_install() and made the
>------->------- * file descriptor accessible for the current process. It
>------->------- * might already be closed and dmabuf no longer valid when
>------->------- * we reach this point. Therefore "leak" the fd and rely on
>------->------- * the process exit path to do any required cleanup.
>------->------- */
			ve1mem_err(VE1_MEM_TAG,
				   "copy_to_user failed! (phyAddr=0x%lx)\n",
				   data.phyAddr);
			break;
		}
		ret = 0;
		break;
	default:
		ve1mem_err(VE1_MEM_TAG, "Unknown ioctl (cmd=0x%x)\n", cmd);
		ret = -ENOTTY;
		break;
	}

	ve1mem_dbg(VE1_MEM_TAG, "[-]\n");
	return ret;
}

static const struct file_operations ve1_mem_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ve1_mem_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ve1_mem_ioctl,
#endif
};

static struct miscdevice ve1_mem_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ve1_mem",
	.fops = &ve1_mem_fops,
	.parent = NULL,
};

int ve1_mem_device_create(void)
{
	int ret;

	ret = misc_register(&ve1_mem_miscdev);
	if (ret) {
		ve1mem_err(VE1_MEM_TAG, "failed to register misc device\n");
		return ret;
	}

	return 0;
}

void ve1_mem_device_destroy(void)
{
	misc_deregister(&ve1_mem_miscdev);
}

