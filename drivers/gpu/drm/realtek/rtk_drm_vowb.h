/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __RTK_DRM_VOWB_H__
#define __RTK_DRM_VOWB_H__

#include <linux/iosys-map.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct dma_buf;
struct dma_buf_attachment;
struct sg_table;
struct tag_ringbuffer_header;
struct rtk_rpc_info;
struct rpmsg_device;
struct drm_device;

struct refclock_data {
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct iosys_map map;
};

struct rtk_drm_ringbuffer {
	struct tag_ringbuffer_header *shm_ringheader;
	dma_addr_t addr;
	void *virt;
	u32 size;
	u32 header_offset;
	struct rpmsg_device *rpdev;
	struct drm_device *drm;
};

struct rtk_drm_vowb;

#define RTK_DRM_VOWB_JOB_STATUS_UNDEFINED 0
#define RTK_DRM_VOWB_JOB_STATUS_START 1
#define RTK_DRM_VOWB_JOB_STATUS_DONE 2
#define RTK_DRM_VOWB_JOB_STATUS_TIMEOUT 3

struct rtk_drm_vowb_job {
	u64 job_id;
	ktime_t time;
	void (*job_done_cb)(struct rtk_drm_vowb *vowb, struct rtk_drm_vowb_job *job);
	u32 status;
};

#ifdef CONFIG_DRM_RTK_VOWB
struct rtk_drm_vowb *rtk_drm_vowb_create(struct drm_device *drm, struct rtk_rpc_info *rpc_info);
void rtk_drm_vowb_destroy(struct rtk_drm_vowb *vowb);
int rtk_drm_vowb_release(struct inode *inode, struct file *filp);
void rtk_drm_vowb_isr(struct drm_device *dev);

int rtk_drm_vowb_setup_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int rtk_drm_vowb_teardown_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int rtk_drm_vowb_add_src_pic_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int rtk_drm_vowb_start_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int rtk_drm_vowb_stop_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int rtk_drm_vowb_get_dst_pic_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int rtk_drm_vowb_run_cmd(struct drm_device *dev, void *data, struct drm_file *file_priv);
int rtk_drm_vowb_check_cmd(struct drm_device *dev, void *data, struct drm_file *file_priv);
#else
static inline
struct rtk_drm_vowb *rtk_drm_vowb_create(struct drm_device *drm, struct rtk_rpc_info *rpc_info)
{
	return ERR_PTR(-ENODEV);
}

static inline
void rtk_drm_vowb_destroy(struct rtk_drm_vowb *vowb)
{}

static inline
int rtk_drm_vowb_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static inline
int rtk_drm_vowb_setup_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return -ENOIOCTLCMD;
}

static inline
void rtk_drm_vowb_isr(struct drm_device *dev)
{}

static inline
int rtk_drm_vowb_teardown_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return -ENOIOCTLCMD;
}

static inline
int rtk_drm_vowb_add_src_pic_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return -ENOIOCTLCMD;
}

static inline
int rtk_drm_vowb_start_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return -ENOIOCTLCMD;
}

static inline
int rtk_drm_vowb_stop_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return -ENOIOCTLCMD;
}

static inline
int rtk_drm_vowb_get_dst_pic_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return -ENOIOCTLCMD;
}

static inline
int rtk_drm_vowb_run_cmd(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return -ENOIOCTLCMD;
}

static inline
int rtk_drm_vowb_check_cmd(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	return -ENOIOCTLCMD;
}

#endif /* CONFIG_DRM_RTK_VOWB */

static inline u32 get_rheap_flags(struct rtk_rpc_info *rpc_info)
{
	// FIXME
	return  RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC | RTK_FLAG_ACPUACC;
}

static inline struct rpmsg_device *get_rpdev(struct rtk_rpc_info *rpc_info)
{
	// FIXME
	return rpc_info->acpu_ept_info->rpdev;
}

#endif
