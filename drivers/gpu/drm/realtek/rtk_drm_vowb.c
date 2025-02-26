// SPDX-License-Identifier: GPL-2.0-only
#include <linux/delay.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_print.h>
#include "rtk_drm_drv.h"
#include "rtk_drm_gem.h"
#include "rtk_drm_rpc.h"
#include "rtk_drm_vowb.h"
#include "uapi/rtk_drm_vowb.h"

#define CREATE_TRACE_POINTS
#include "rtk_drm_vowb_trace.h"

#define RTK_DRM_VOWB_BRINGBUFFER_SIZE      (16*1024)
#define RTK_DRM_RINGBUFFER_HEADER_SIZE     (1024)
#define RTK_DRM_VOWB_REFCLOCK_SIZE         (2048)

struct rtk_drm_vowb_func1_data {
	struct rtk_drm_vowb_job job;
	struct drm_file *file_priv;

	struct rtk_drm_vowb_src_pic srcs[RTK_DRM_VOWB_MAX_SRC_PIC];
	u32 num_srcs;

	struct rtk_drm_vowb_dst_pic dst;
	u32 handles[4];
	u32 num_handles;
	u32 dst_id;

	u32 flags;
	u32 enabled;

	u64 cnt_display;
	u32 cnt_vowb;
};

struct rtk_drm_vowb_func2_data {
	struct rtk_drm_vowb_job job;
	struct drm_file *file_priv;
};

struct rtk_drm_vowb {
	struct drm_device *dev;
	struct rtk_rpc_info *rpc_info;
	u32 instance;
	struct tag_refclock *shm_refclock;
	struct refclock_data refclock_data;

	struct rtk_drm_ringbuffer tx;
	struct rtk_drm_ringbuffer rx;

	u64 emit_job_id;
	atomic64_t resp_job_id;
	wait_queue_head_t wq;
	struct delayed_work work;
	struct rtk_drm_vowb_job *cur_job;

	void (*vsync_isr_func)(struct rtk_drm_vowb *vowb, void *data);
	void *vsync_isr_data;

	spinlock_t lock;

	struct rtk_drm_vowb_func1_data func1_data;
	struct rtk_drm_vowb_func2_data func2_data;
};

static void rtk_drm_vowb_check_resp(struct work_struct *work);

static u32 get_rheap_flags(struct rtk_rpc_info *rpc_info)
{
	// FIXME
	return  RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC | RTK_FLAG_ACPUACC;
}

static struct rpmsg_device *get_rpdev(struct rtk_rpc_info *rpc_info)
{
	// FIXME
	return rpc_info->acpu_ept_info->rpdev;
}

static int rtk_drm_vowb_setup_agent(struct rtk_drm_vowb *vowb)
{
	struct rtk_rpc_info *rpc_info = vowb->rpc_info;

	if (rpc_create_video_agent(rpc_info, &vowb->instance, VF_TYPE_VIDEO_OUT)) {
		DRM_ERROR("failed in rpc_create_video_agent()\n");
		return -1;
	}
	return 0;
}

/*static int rtk_drm_vowb_display(struct rtk_drm_vowb *vowb, bool zero_buffer)
{
	struct rtk_rpc_info *rpc_info = vowb->rpc_info;
	struct rpc_vo_filter_display info = {};

	info.instance = vowb->instance;
	info.videoPlane = VO_VIDEO_PLANE_V1;
	info.zeroBuffer = zero_buffer ? 1 : 0;
	info.realTimeSrc = 0;
	if (rpc_video_display(rpc_info, &info)) {
		DRM_ERROR("failed in rpc_video_display()\n");
		return -EINVAL;
	}
	return 0;
}*/

static int rtk_drm_ringbuffer_alloc(struct drm_device *drm,
				    struct rtk_rpc_info *rpc_info,
				    struct rtk_drm_ringbuffer *rb,
				    u32 buffer_size)
{
	struct rpmsg_device *rpdev = get_rpdev(rpc_info);

	rb->drm = drm;
	rb->rpdev = rpdev;
	rb->size = buffer_size + RTK_DRM_RINGBUFFER_HEADER_SIZE;
	rb->header_offset = buffer_size;

	rheap_setup_dma_pools(drm->dev, "rtk_audio_heap", get_rheap_flags(rpc_info), __func__);
	rb->virt = dma_alloc_coherent(drm->dev, rb->size, &rb->addr,
				      GFP_KERNEL | __GFP_NOWARN);
	if (!rb->virt) {
		DRM_ERROR("failed to alloc rb ring\n");
		return -ENOMEM;
	}

	rb->shm_ringheader = rb->virt + rb->header_offset;
	rb->shm_ringheader->beginAddr  = cpu_to_rpmsg32(rpdev, (u32)rb->addr);
	rb->shm_ringheader->size       = cpu_to_rpmsg32(rpdev, buffer_size);
	rb->shm_ringheader->bufferID   = cpu_to_rpmsg32(rpdev, 1);
	rb->shm_ringheader->writePtr   = rb->shm_ringheader->beginAddr;
	rb->shm_ringheader->readPtr[0] = rb->shm_ringheader->beginAddr;
	return  0;
}

static void rtk_drm_ringbuffer_free(struct rtk_drm_ringbuffer *rb)
{
	dma_free_coherent(rb->drm->dev, rb->size, rb->virt, rb->addr);
}

static int rtk_drm_ringbuffer_write(struct rtk_drm_ringbuffer *rb, void *cmd, u32 size)
{
	u32 buf_r, buf_w, buf_b, buf_s, buf_l;
	u32 buf_space;
	void *ptr_b, *ptr_w;

	buf_r = rpmsg32_to_cpu(rb->rpdev, rb->shm_ringheader->readPtr[0]);
	buf_w = rpmsg32_to_cpu(rb->rpdev, rb->shm_ringheader->writePtr);
	buf_b = rpmsg32_to_cpu(rb->rpdev, rb->shm_ringheader->beginAddr);
	buf_s = rpmsg32_to_cpu(rb->rpdev, rb->shm_ringheader->size);
	buf_l = buf_b + buf_s;
	buf_space = buf_r + (buf_r > buf_w ? 0 : buf_s) - buf_w;

	if (buf_space < size)
		return -ENOBUFS;

	ptr_b = rb->virt;
	ptr_w = ptr_b + (buf_w - buf_b);

	if (buf_w + size <= buf_l) {
		memcpy_toio(ptr_w, cmd, size);
	} else {
		memcpy_toio(ptr_w, cmd, buf_l - buf_w);
		memcpy_toio(ptr_b, cmd + buf_l - buf_w, size - (buf_l - buf_w));
	}
	buf_w += size;
	if (buf_w >= buf_l)
		buf_w -= buf_s;

	rb->shm_ringheader->writePtr = cpu_to_rpmsg32(rb->rpdev, buf_w);
	return 0;
}

static int rtk_drm_ringbuffer_read(struct rtk_drm_ringbuffer *rb, void *data, u32 data_size,
				   bool update_read_ptr)
{
	void *ptr_b, *ptr_r;
	u32 buf_r, buf_w, buf_b, buf_s, buf_l;
	u32 buf_data_size;

	buf_r = rpmsg32_to_cpu(rb->rpdev, rb->shm_ringheader->readPtr[0]);
	buf_w = rpmsg32_to_cpu(rb->rpdev, rb->shm_ringheader->writePtr);
	buf_b = rpmsg32_to_cpu(rb->rpdev, rb->shm_ringheader->beginAddr);
	buf_s = rpmsg32_to_cpu(rb->rpdev, rb->shm_ringheader->size);
	buf_l = buf_b + buf_s;

	buf_data_size = buf_w + (buf_w < buf_r ? buf_s :  0) - buf_r;

	if (buf_data_size < data_size)
		return -ENODATA;

	ptr_b = rb->virt;
	ptr_r = ptr_b + (buf_r - buf_b);

	if (data) {
		if (buf_r + data_size <= buf_l) {
			memcpy_fromio(data, ptr_r, data_size);
		} else {
			memcpy_fromio(data, ptr_r,  buf_l - buf_r);
			memcpy_fromio(data + buf_l - buf_r, ptr_b, data_size - (buf_l - buf_r));
		}
	}

	if (update_read_ptr) {
		buf_r += data_size;
		if (buf_r >= buf_l)
			buf_r -= buf_s;

		rb->shm_ringheader->readPtr[0] = cpu_to_rpmsg32(rb->rpdev, buf_r);
	}
	return 0;
}

static int rtk_drm_vowb_setup_ringbuffer(struct rtk_drm_vowb *vowb, struct rtk_drm_ringbuffer *rb, u32 pin_id)
{
	struct rtk_rpc_info *rpc_info = vowb->rpc_info;
	struct rpc_ringbuffer ringbuffer = {};

	ringbuffer.instance = vowb->instance;
	ringbuffer.readPtrIndex = 0;
	ringbuffer.pinID = pin_id;
	ringbuffer.pRINGBUFF_HEADER = (u32)rb->addr + rb->header_offset;
	if (rpc_video_init_ringbuffer(rpc_info, &ringbuffer)) {
		DRM_ERROR("failed in rpc_video_init_ringbuffer()\n");
		return -EINVAL;
	}
	return 0;
}

static int rtk_drm_config_display_window(struct rtk_drm_vowb *vowb,
					 struct vo_rectangle *video_win,
					 struct vo_rectangle *border_win)
{
	struct rtk_rpc_info *rpc_info = vowb->rpc_info;
	struct vo_rectangle rect = {};
	struct vo_color blueBorder = {0, 0, 255, 1};
	struct rpc_config_disp_win disp_win = {};

	disp_win.videoPlane = VO_VIDEO_PLANE_V1 | (0 << 16);
	disp_win.videoWin = video_win ? *video_win : rect;
	disp_win.borderWin = border_win ? *border_win : rect;
	disp_win.borderColor = blueBorder;
	disp_win.enBorder = 0;
	if (rpc_video_config_disp_win(rpc_info, &disp_win)) {
		DRM_ERROR("failed rpc_video_config_disp_win()\n");
		return -EINVAL;
	}
	return 0;
}

static int rtk_drm_alloc_refclock(struct rtk_drm_vowb *vowb)
{
	struct drm_device *drm = vowb->dev;
	struct rtk_rpc_info *rpc_info = vowb->rpc_info;
	struct rpmsg_device *rpdev = get_rpdev(rpc_info);
	struct refclock_data *r = &vowb->refclock_data;
	int ret;

	r->dmabuf = rheap_alloc("rtk_audio_heap", RTK_DRM_VOWB_REFCLOCK_SIZE, get_rheap_flags(rpc_info));
	if (IS_ERR_OR_NULL(r->dmabuf)) {
		DRM_ERROR("failed to alloc refclock dmabuf\n");
		return -ENOMEM;
	}

	dma_buf_set_name(r->dmabuf, __func__);
	r->attach = dma_buf_attach(r->dmabuf, drm->dev);
	if (IS_ERR(r->attach)) {
		DRM_ERROR("failed to attach refclock dmabuf to %s\n", dev_name(drm->dev));
		ret = PTR_ERR(r->attach);
		goto put_dmabuf;
	}

	r->sgt = dma_buf_map_attachment(r->attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(r->sgt)) {
		DRM_ERROR("failed to map attachment of refclock dmabuf\n");
		ret = PTR_ERR(r->sgt);
		goto detach_dmabuf;
	}

	dma_buf_begin_cpu_access(r->dmabuf, DMA_BIDIRECTIONAL);
	ret = dma_buf_vmap(r->dmabuf, &r->map);
	if (ret) {
		DRM_ERROR("failed to map refclock dmabuf\n");
		goto unmap_attachment;
	}

	vowb->shm_refclock = r->map.vaddr;
	vowb->shm_refclock->RCD = cpu_to_rpmsg64(rpdev, -1LL);
	vowb->shm_refclock->RCD_ext = cpu_to_rpmsg32(rpdev, -1L);
	vowb->shm_refclock->masterGPTS = cpu_to_rpmsg64(rpdev, -1LL);
	vowb->shm_refclock->GPTSTimeout = cpu_to_rpmsg64(rpdev, 0LL);
	vowb->shm_refclock->videoSystemPTS = cpu_to_rpmsg64(rpdev, -1LL);
	vowb->shm_refclock->audioSystemPTS = cpu_to_rpmsg64(rpdev, -1LL);
	vowb->shm_refclock->videoRPTS = cpu_to_rpmsg64(rpdev, -1LL);
	vowb->shm_refclock->audioRPTS = cpu_to_rpmsg64(rpdev, -1LL);
	vowb->shm_refclock->videoContext = cpu_to_rpmsg32(rpdev, -1);
	vowb->shm_refclock->audioContext = cpu_to_rpmsg32(rpdev, -1);
	vowb->shm_refclock->videoEndOfSegment = cpu_to_rpmsg32(rpdev, -1);
	vowb->shm_refclock->videoFreeRunThreshold = cpu_to_rpmsg32(rpdev, 0x7FFFFFFF);
	vowb->shm_refclock->audioFreeRunThreshold = cpu_to_rpmsg32(rpdev, 0x7FFFFFFF);
	vowb->shm_refclock->VO_Underflow = cpu_to_rpmsg32(rpdev, 0);
	vowb->shm_refclock->AO_Underflow = cpu_to_rpmsg32(rpdev, 0);
	vowb->shm_refclock->mastership.systemMode = (unsigned char)AVSYNC_FORCED_SLAVE;
	vowb->shm_refclock->mastership.videoMode = (unsigned char)AVSYNC_FORCED_MASTER;
	vowb->shm_refclock->mastership.audioMode = (unsigned char)AVSYNC_FORCED_MASTER;
	vowb->shm_refclock->mastership.masterState = (unsigned char)AUTOMASTER_NOT_MASTER;
	return 0;

unmap_attachment:
	dma_buf_end_cpu_access(r->dmabuf, DMA_BIDIRECTIONAL);
	dma_buf_unmap_attachment(r->attach, r->sgt, DMA_BIDIRECTIONAL);
detach_dmabuf:
	dma_buf_detach(r->dmabuf, r->attach);
put_dmabuf:
	dma_buf_put(r->dmabuf);
	return ret;
}

static void rtk_drm_vowb_free_refclock(struct rtk_drm_vowb *vowb)
{
	struct refclock_data *r = &vowb->refclock_data;

	dma_buf_vunmap(r->dmabuf, &r->map);
	dma_buf_end_cpu_access(r->dmabuf, DMA_BIDIRECTIONAL);
	dma_buf_unmap_attachment(r->attach, r->sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(r->dmabuf, r->attach);
	dma_buf_put(r->dmabuf);
}

static int rtk_drm_vowb_setup_refclock(struct rtk_drm_vowb *vowb)
{
	struct rtk_rpc_info *rpc_info = vowb->rpc_info;
	struct refclock_data *r = &vowb->refclock_data;
	struct rpc_refclock refclock = {};

	refclock.instance = vowb->instance;
	refclock.pRefClock = (u32)sg_dma_address(r->sgt->sgl);
	if (rpc_video_set_refclock(rpc_info, &refclock)) {
		DRM_ERROR("failed in rpc_video_set_refclock()\n");
		return -EINVAL;
	}
	return 0;
}

static int rtk_drm_vowb_run(struct rtk_drm_vowb *vowb)
{
	struct rtk_rpc_info *rpc_info = vowb->rpc_info;

	if (rpc_video_run(rpc_info, vowb->instance)) {
		DRM_ERROR("failed in rpc_video_run()\n");
		return -EINVAL;
	}
	return 0;
}

static void rtk_drm_destroy_agent(struct rtk_drm_vowb *vowb)
{
	struct rtk_rpc_info *rpc_info = vowb->rpc_info;

	WARN_ON_ONCE(rpc_destroy_video_agent(rpc_info, vowb->instance));
}

struct rtk_drm_vowb *rtk_drm_vowb_create(struct drm_device *drm, struct rtk_rpc_info *rpc_info)
{
	int ret;
	struct rtk_drm_vowb *vowb;

	vowb = kzalloc(sizeof(*vowb), GFP_KERNEL);
	if (!vowb)
		return ERR_PTR(-ENOMEM);

	vowb->dev = drm;
	vowb->rpc_info = rpc_info;
	INIT_DELAYED_WORK(&vowb->work, rtk_drm_vowb_check_resp);
	spin_lock_init(&vowb->lock);
	init_waitqueue_head(&vowb->wq);

	ret = rtk_drm_ringbuffer_alloc(vowb->dev, vowb->rpc_info, &vowb->tx,
				       RTK_DRM_VOWB_BRINGBUFFER_SIZE);
	if (ret)
		return ERR_PTR(ret);
	ret = rtk_drm_ringbuffer_alloc(vowb->dev, vowb->rpc_info, &vowb->rx,
				       RTK_DRM_VOWB_BRINGBUFFER_SIZE);
	if (ret)
		goto free_txbuf;
	ret = rtk_drm_alloc_refclock(vowb);
	if (ret)
		goto free_rxbuf;
	ret = rtk_drm_vowb_setup_agent(vowb);
	if (ret)
		goto free_refclock;
	/*ret = rtk_drm_vowb_display(vowb, 0);
	if (ret)
		goto destroy_agent;*/
	ret = rtk_drm_config_display_window(vowb, NULL, NULL);
	if (ret)
		goto destroy_agent;
	ret = rtk_drm_vowb_setup_refclock(vowb);
	if (ret)
		goto destroy_agent;
	ret = rtk_drm_vowb_setup_ringbuffer(vowb, &vowb->tx, 0);
	if (ret)
		goto destroy_agent;
	ret = rtk_drm_vowb_setup_ringbuffer(vowb, &vowb->rx, 0x20140507);
	if (ret)
		goto destroy_agent;
	ret = rtk_drm_vowb_run(vowb);
	if (ret)
		goto destroy_agent;
	/*ret = rtk_drm_vowb_display(vowb, 1);
	if (ret)
		goto destroy_agent;*/

	return vowb;

destroy_agent:
	rtk_drm_destroy_agent(vowb);
free_refclock:
	rtk_drm_vowb_free_refclock(vowb);
free_rxbuf:
	rtk_drm_ringbuffer_free(&vowb->rx);
free_txbuf:
	rtk_drm_ringbuffer_free(&vowb->tx);
	return ERR_PTR(ret);
}

void rtk_drm_vowb_destroy(struct rtk_drm_vowb *vowb)
{
	if (!vowb)
		return;

	rtk_drm_destroy_agent(vowb);
	rtk_drm_vowb_free_refclock(vowb);
	rtk_drm_ringbuffer_free(&vowb->rx);
	rtk_drm_ringbuffer_free(&vowb->tx);
	kfree(vowb);
}

static int rtk_drm_vowb_handle_to_addr(struct drm_file *file_priv, u32 handle, dma_addr_t *addr)
{
	struct drm_gem_object *obj;
	struct rtk_gem_object *robj;

	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj) {
		DRM_ERROR("Failed to lookup GEM object of handle %d\n", handle);
		return -ENXIO;
	}

	robj = to_rtk_gem_obj(obj);
	*addr = robj->paddr;
	drm_gem_object_put(obj);

	return 0;
}

static int rtk_drm_vowb_get_resp(struct rtk_drm_vowb *vowb,
			     struct video_writeback_picture_object *ret_obj)
{
	struct inband_cmd_pkg_header header = {};
	int ret;
	u32 size;

	ret = rtk_drm_ringbuffer_read(&vowb->rx, &header, sizeof(header), false);
	if (ret)
		return ret;

	size = rpmsg32_to_cpu(vowb->rx.rpdev, header.size);
	if (size != sizeof(*ret_obj)) {
		rtk_drm_ringbuffer_read(&vowb->rx, NULL, size, true);
		return -EINVAL;
	}

	rtk_drm_ringbuffer_read(&vowb->rx, ret_obj, sizeof(*ret_obj), true);
	return 0;
}

void rtk_drm_vowb_isr(struct drm_device *dev)
{
	struct rtk_drm_vowb *vowb = ((struct rtk_drm_private *)dev->dev_private)->vowb;

	if (!vowb->vsync_isr_func)
		return;

	vowb->vsync_isr_func(vowb, vowb->vsync_isr_data);
}

static void rtk_drm_vowb_check_resp(struct work_struct *work)
{
	struct rtk_drm_vowb *vowb = container_of(work, struct rtk_drm_vowb, work.work);
	struct rpmsg_device *rpdev = vowb->rx.rpdev;
	u64 job_id;
	int ret;

	do {
		struct video_writeback_picture_object ret_obj = {0};
		struct rtk_drm_vowb_job *job = vowb->cur_job;

		if (!job) {
			DRM_DEBUG("no cur job\n");
			return;
		}

		ret = rtk_drm_vowb_get_resp(vowb, &ret_obj);
		if (ret == -ENODATA) {
			if (ktime_to_ms(ktime_sub(ktime_get(), job->time)) >= 1000) {
				DRM_WARN("job %p: timedout\n", job);
				job->status = RTK_DRM_VOWB_JOB_STATUS_TIMEOUT;
				trace_vowb_job_update_status(job);
				vowb->cur_job = NULL;
				return;
			}
			goto resched;
		} else if (ret) {
			DRM_ERROR("rtk_drm_vowb_get_resp() returns %d\n", ret);
			return;
		}

		job_id = ((u64)rpmsg32_to_cpu(rpdev, ret_obj.bufferID_H) << 32) |
			rpmsg32_to_cpu(rpdev, ret_obj.bufferID_L);

		if (job_id > atomic64_read(&vowb->resp_job_id)) {
			atomic64_set(&vowb->resp_job_id, job_id);
			trace_vowb_update_resp_job_id(job_id);
		}

		if (job->job_id <= job_id) {
			if (job->job_done_cb)
				job->job_done_cb(vowb, job);
			wake_up_all(&vowb->wq);
			job->status = RTK_DRM_VOWB_JOB_STATUS_DONE;
			trace_vowb_job_update_status(job);
			vowb->cur_job = NULL;
			return;
		}
	} while (1);

	return;
resched:
	schedule_delayed_work(&vowb->work, 1);
}

static int rtk_drm_vowb_queue_job(struct rtk_drm_vowb *vowb, struct rtk_drm_vowb_job *job,
				  void *cmds, u32 cmds_size)
{
	int ret;

	if (vowb->cur_job) {
		DRM_INFO("queue job %p failed (job %p not completed)\n", job, vowb->cur_job);
		return -EBUSY;
	}

	ret = rtk_drm_ringbuffer_write(&vowb->tx, cmds, cmds_size);
	if (ret) {
		DRM_ERROR("rtk_drm_ringbuffer_write() returns %d\n", ret);
		return ret;
	}

	job->job_id = vowb->emit_job_id;
	job->time = ktime_get();
	job->status = RTK_DRM_VOWB_JOB_STATUS_START;
	vowb->cur_job = job;

	trace_vowb_job_update_status(job);

	schedule_delayed_work(&vowb->work, 1);
	return 0;
}

static void inband_cmd_video_object(struct rpmsg_device *rpdev, struct video_object *cmd,
				    struct rtk_drm_vowb_dst_pic *dst, dma_addr_t dst_addr)
{
	cmd->lumaOffTblAddr               = cpu_to_rpmsg32(rpdev, 0xffffffff);
	cmd->chromaOffTblAddr             = cpu_to_rpmsg32(rpdev, 0xffffffff);
	cmd->lumaOffTblAddrR              = cpu_to_rpmsg32(rpdev, 0xffffffff);
	cmd->chromaOffTblAddrR            = cpu_to_rpmsg32(rpdev, 0xffffffff);
	cmd->bufBitDepth                  = cpu_to_rpmsg32(rpdev, 8);
	cmd->matrix_coefficients          = cpu_to_rpmsg32(rpdev, 1);
	cmd->tch_hdr_metadata.specVersion = cpu_to_rpmsg32(rpdev, -1);
	cmd->Y_addr_Right                 = cpu_to_rpmsg32(rpdev, 0xffffffff);
	cmd->U_addr_Right                 = cpu_to_rpmsg32(rpdev, 0xffffffff);
	cmd->pLock_Right                  = cpu_to_rpmsg32(rpdev, 0xffffffff);

	cmd->header.type                  = cpu_to_rpmsg32(rpdev, VIDEO_VO_INBAND_CMD_TYPE_OBJ_PIC);
	cmd->header.size                  = cpu_to_rpmsg32(rpdev, sizeof(struct video_object));
	cmd->version                      = cpu_to_rpmsg32(rpdev, 0x72746b3f);
	cmd->width                        = cpu_to_rpmsg32(rpdev, dst->w);
	cmd->height                       = cpu_to_rpmsg32(rpdev, dst->h);
	cmd->Y_pitch                      = cpu_to_rpmsg32(rpdev, dst->w);
	cmd->mode                         = cpu_to_rpmsg32(rpdev, CONSECUTIVE_FRAME);
	cmd->Y_addr                       = cpu_to_rpmsg32(rpdev, dst_addr + dst->y_offset);
	cmd->U_addr                       = cpu_to_rpmsg32(rpdev, dst_addr + dst->c_offset);
}


static void inband_cmd_video_transcode_picture_object(struct rpmsg_device *rpdev,
						      struct video_transcode_picture_object *cmd,
						      struct rtk_drm_vowb_dst_pic *dst,
						      dma_addr_t dst_addr,
						      struct rtk_drm_vowb_src_pic *src,
						      dma_addr_t src_addr,
						      u64 job_id)
{
	cmd->header.size  = cpu_to_rpmsg32(rpdev, sizeof(*cmd));
	cmd->header.type  = cpu_to_rpmsg32(rpdev, VIDEO_TRANSCODE_INBAND_CMD_TYPE_PICTURE_OBJECT);
	cmd->version      = cpu_to_rpmsg32(rpdev, 0x54524134);
	cmd->bufferID_H   = cpu_to_rpmsg32(rpdev, job_id >> 32);
	cmd->bufferID_L   = cpu_to_rpmsg32(rpdev, job_id & 0xffffffff);
	cmd->Y_addr       = cpu_to_rpmsg32(rpdev, src_addr + src->y_offset);
	cmd->U_addr       = cpu_to_rpmsg32(rpdev, src_addr + src->c_offset);
	cmd->width        = cpu_to_rpmsg32(rpdev, src->w);
	cmd->height       = cpu_to_rpmsg32(rpdev, src->h);
	cmd->Y_pitch      = cpu_to_rpmsg32(rpdev, src->y_pitch);
	cmd->C_pitch      = cpu_to_rpmsg32(rpdev, src->c_pitch);
	cmd->crop_width   = cpu_to_rpmsg32(rpdev, src->crop_w);
	cmd->crop_height  = cpu_to_rpmsg32(rpdev, src->crop_h);
	cmd->crop_x       = cpu_to_rpmsg32(rpdev, src->crop_x);
	cmd->crop_y       = cpu_to_rpmsg32(rpdev, src->crop_y);
	cmd->targetFormat = cpu_to_rpmsg32(rpdev, 0); // nv12
	cmd->wb_y_addr    = cpu_to_rpmsg32(rpdev, dst_addr + dst->y_offset + src->resize_win_x +
					   src->resize_win_y * dst->w);
	cmd->wb_c_addr    = cpu_to_rpmsg32(rpdev, dst_addr + dst->c_offset + src->resize_win_x +
					   src->resize_win_y / 2 * dst->w);
	cmd->wb_w         = cpu_to_rpmsg32(rpdev, src->resize_win_w);
	cmd->wb_h         = cpu_to_rpmsg32(rpdev, src->resize_win_h);
	cmd->wb_pitch     = cpu_to_rpmsg32(rpdev, dst->w);
	cmd->contrast     = cpu_to_rpmsg32(rpdev, src->contrast);
	cmd->brightness   = cpu_to_rpmsg32(rpdev, src->brightness);
	cmd->hue          = cpu_to_rpmsg32(rpdev, src->hue);
	cmd->saturation   = cpu_to_rpmsg32(rpdev, src->saturation);
	cmd->sharp_en     = cpu_to_rpmsg32(rpdev, src->sharp_en);
	cmd->sharp_value  = cpu_to_rpmsg32(rpdev, src->sharp_value);
}

static int rtk_drm_vowb_func1_prepare_cmds(struct rtk_drm_vowb *vowb,
					   struct rtk_drm_vowb_func1_data *func1,
					   struct rpmsg_device *rpdev,
					   struct video_transcode_picture_object *cmds,
					   u32 num_cmds, u64 job_id, u32 dst_id)
{
	struct drm_file *file_priv = func1->file_priv;
	dma_addr_t dst_addr;
	dma_addr_t src_addr[RTK_DRM_VOWB_MAX_SRC_PIC] = {};
	int i, j = 0;
	int ret;
	unsigned long flags;

	if (num_cmds < func1->num_srcs)
		return -EINVAL;

	spin_lock_irqsave(&vowb->lock, flags);
	ret = rtk_drm_vowb_handle_to_addr(file_priv, func1->handles[dst_id], &dst_addr);
	if (ret) {
		spin_unlock_irqrestore(&vowb->lock, flags);
		return ret;
	}
	for (i = 0; i < func1->num_srcs; i++) {
		struct rtk_drm_vowb_src_pic *src = &func1->srcs[i];

		if (!src->handle)
			continue;

		ret = rtk_drm_vowb_handle_to_addr(file_priv, src->handle, &src_addr[i]);
		if (ret)
			continue;
	}
	spin_unlock_irqrestore(&vowb->lock, flags);

	for (i = 0; i < func1->num_srcs; i++) {
		if (!src_addr[i])
			continue;

		inband_cmd_video_transcode_picture_object(rpdev, &cmds[j], &func1->dst, dst_addr,
							  &func1->srcs[i], src_addr[i], ++job_id);
		j++;
	}

	return j;
}

static int rtk_drm_vowb_func1_start(struct rtk_drm_vowb *vowb, struct rtk_drm_vowb_func1_data *func1)
{
	struct video_transcode_picture_object *cmds;
	int ret;

	cmds = kcalloc(func1->num_srcs, sizeof(*cmds), GFP_KERNEL);
	if (!cmds)
		return -ENOMEM;

	ret = rtk_drm_vowb_func1_prepare_cmds(vowb, func1, vowb->tx.rpdev, cmds, func1->num_srcs,
					      vowb->emit_job_id, func1->dst_id);
	if (ret <= 0) {
		DRM_ERROR("rtk_drm_vowb_func1_prepare_cmds() returns %d\n", ret);
		goto free_cmds;
	}

	vowb->emit_job_id += ret;

	ret = rtk_drm_vowb_queue_job(vowb, &func1->job, cmds, sizeof(*cmds) * ret);
	if (!ret)
		++func1->cnt_vowb;
	trace_vowb_func1_statistics(__func__, func1->cnt_display, func1->cnt_vowb);
free_cmds:
	kfree(cmds);
	return ret;
}

static void rtk_drm_vowb_func1_init(struct rtk_drm_vowb_func1_data *func1)
{
	memset(func1, 0, sizeof(*func1));
}

static void rtk_drm_vowb_func1_job_done_cb(struct rtk_drm_vowb *vowb, struct rtk_drm_vowb_job *job)
{
	struct rtk_drm_vowb_func1_data *func1 = container_of(job, struct rtk_drm_vowb_func1_data, job);
	struct drm_file *file_priv = func1->file_priv;
	dma_addr_t dst_addr;
	struct video_object cmd = {};
	unsigned long flags;
	int resp = (u32)ktime_to_us(ktime_sub(ktime_get(), job->time));
	int ret;

	if (resp > 16667)
		trace_vowb_func1_warn_resp_us(resp);

	spin_lock_irqsave(&vowb->lock, flags);
	ret = rtk_drm_vowb_handle_to_addr(file_priv, func1->handles[func1->dst_id], &dst_addr);
	spin_unlock_irqrestore(&vowb->lock, flags);
	if (ret)
		return;
	inband_cmd_video_object(vowb->tx.rpdev, &cmd, &func1->dst, dst_addr);

	func1->dst_id = (func1->dst_id + 1) % func1->num_handles;

	ret = rtk_drm_ringbuffer_write(&vowb->tx, &cmd, sizeof(cmd));
	if (ret)
		DRM_ERROR("rtk_drm_ringbuffer_write() returns %d\n", ret);
	else
		trace_vowb_func1_statistics(__func__, ++func1->cnt_display, func1->cnt_vowb);
}

static void rtk_drm_vowb_func1_vsync_isr(struct rtk_drm_vowb *vowb, void *data)
{
	struct rtk_drm_vowb_func1_data *func1 = data;

	if (!func1->enabled)
		return;
	rtk_drm_vowb_func1_start(vowb, func1);
}

int rtk_drm_vowb_setup_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct rtk_drm_vowb_setup *arg = data;
	struct rtk_drm_vowb *vowb = ((struct rtk_drm_private *)dev->dev_private)->vowb;
	struct rtk_drm_vowb_func1_data *func1 = &vowb->func1_data;

	if (!vowb)
		return -ENOIOCTLCMD;

	if (func1->file_priv || vowb->vsync_isr_func != NULL)
		return -EBUSY;

	// if dst valid
	rtk_drm_vowb_func1_init(func1);
	memcpy(func1->handles, arg->handles, sizeof(arg->handles));
	func1->num_handles     = arg->num_handles;
	func1->dst             = arg->dst;
	func1->num_srcs        = arg->num_srcs;
	func1->flags           = arg->flags;
	func1->file_priv       = file_priv;
	func1->job.job_done_cb = rtk_drm_vowb_func1_job_done_cb;

	vowb->vsync_isr_data = func1;
	vowb->vsync_isr_func = rtk_drm_vowb_func1_vsync_isr;
	return 0;
}

int rtk_drm_vowb_teardown_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct rtk_drm_vowb *vowb = ((struct rtk_drm_private *)dev->dev_private)->vowb;
	struct rtk_drm_vowb_func1_data *func1 = &vowb->func1_data;

	if (!vowb)
		return -ENOIOCTLCMD;

	if (func1->file_priv != file_priv || vowb->vsync_isr_func != rtk_drm_vowb_func1_vsync_isr)
		return -EINVAL;

	vowb->vsync_isr_func = NULL;
	vowb->vsync_isr_data = NULL;
	flush_delayed_work(&vowb->work);
	rtk_drm_vowb_func1_init(func1);
	return 0;
}

int rtk_drm_vowb_release(struct inode *inode, struct file *filp)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_minor *minor = file_priv->minor;
	struct drm_device *dev = minor->dev;
	struct rtk_drm_vowb *vowb = ((struct rtk_drm_private *)dev->dev_private)->vowb;
	struct rtk_drm_vowb_func1_data *func1 = &vowb->func1_data;

	if (!vowb)
		return 0;

	if (func1->file_priv != file_priv || vowb->vsync_isr_func != rtk_drm_vowb_func1_vsync_isr)
		return 0;

	vowb->vsync_isr_func = NULL;
	vowb->vsync_isr_data = NULL;
	flush_delayed_work(&vowb->work);
	rtk_drm_vowb_func1_init(func1);
	return 0;
}

int rtk_drm_vowb_add_src_pic_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct rtk_drm_vowb_add_src_pic *arg = data;
	struct rtk_drm_vowb *vowb = ((struct rtk_drm_private *)dev->dev_private)->vowb;
	struct rtk_drm_vowb_func1_data *func1 = &vowb->func1_data;
	unsigned long flags;

	if (!vowb)
		return -ENOIOCTLCMD;

	if (func1->file_priv != file_priv)
		return -EINVAL;

	if (arg->index >= func1->num_srcs)
		return -EINVAL;

	// if src valid
	spin_lock_irqsave(&vowb->lock, flags);
	func1->srcs[arg->index] = arg->src;
	spin_unlock_irqrestore(&vowb->lock, flags);
	return 0;
}

int rtk_drm_vowb_get_dst_pic_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct rtk_drm_vowb_dst_pic *arg = data;
	struct rtk_drm_vowb *vowb = ((struct rtk_drm_private *)dev->dev_private)->vowb;
	struct rtk_drm_vowb_func1_data *func1 = &vowb->func1_data;

	if (!vowb)
		return -ENOIOCTLCMD;

	if (func1->file_priv != file_priv)
		return -EINVAL;

	// if dst valid
	*arg = func1->dst;
	return 0;
}

int rtk_drm_vowb_start_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct rtk_drm_vowb *vowb = ((struct rtk_drm_private *)dev->dev_private)->vowb;
	struct rtk_drm_vowb_func1_data *func1 = &vowb->func1_data;
	int ret;
	struct rtk_drm_vowb_dst_pic *dst = &func1->dst;
	struct vo_rectangle rect = {0, 0, dst->w, dst->h};

	if (!vowb)
		return -ENOIOCTLCMD;

	if (func1->file_priv != file_priv)
		return -EINVAL;

	func1->dst_id = 0;
	ret = rtk_drm_config_display_window(vowb, &rect, &rect);
	if (ret)
		DRM_ERROR("rtk_drm_config_display_window() returns %d\n", ret);

	func1->enabled = 1;
	return rtk_drm_vowb_func1_start(vowb, func1);
}

int rtk_drm_vowb_stop_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct rtk_drm_vowb *vowb = ((struct rtk_drm_private *)dev->dev_private)->vowb;
	struct rtk_drm_vowb_func1_data *func1 = &vowb->func1_data;

	if (!vowb)
		return -ENOIOCTLCMD;

	if (func1->file_priv != file_priv)
		return -EINVAL;

	func1->enabled = 0;
	return 0;
}

static void func2_inband_cmd_video_transcode_picture_object(struct rpmsg_device *rpdev,
							    struct video_transcode_picture_object *cmd,
							    struct rtk_drm_vowb_pic *pic,
							    dma_addr_t dst_addr,
							    dma_addr_t src_addr,
							    u64 job_id)
{
	cmd->header.size  = cpu_to_rpmsg32(rpdev, sizeof(*cmd));
	cmd->header.type  = cpu_to_rpmsg32(rpdev, VIDEO_TRANSCODE_INBAND_CMD_TYPE_PICTURE_OBJECT);
	cmd->version      = cpu_to_rpmsg32(rpdev, 0x54524134);
	cmd->bufferID_H   = cpu_to_rpmsg32(rpdev, job_id >> 32);
	cmd->bufferID_L   = cpu_to_rpmsg32(rpdev, job_id & 0xffffffff);
	cmd->mode         = cpu_to_rpmsg32(rpdev, pic->mode);
	cmd->Y_addr       = cpu_to_rpmsg32(rpdev, src_addr + pic->offsets[0]);
	cmd->U_addr       = cpu_to_rpmsg32(rpdev, src_addr + pic->offsets[1]);
	cmd->width        = cpu_to_rpmsg32(rpdev, pic->w);
	cmd->height       = cpu_to_rpmsg32(rpdev, pic->h);
	cmd->Y_pitch      = cpu_to_rpmsg32(rpdev, pic->pitches[0]);
	cmd->C_pitch      = cpu_to_rpmsg32(rpdev, pic->pitches[1]);
	cmd->targetFormat = cpu_to_rpmsg32(rpdev, pic->target_format);
	cmd->wb_y_addr    = cpu_to_rpmsg32(rpdev, dst_addr + pic->offsets[2]);
	cmd->wb_c_addr    = cpu_to_rpmsg32(rpdev, dst_addr + pic->offsets[3]);
	cmd->wb_w         = cpu_to_rpmsg32(rpdev, pic->wb_w);
	cmd->wb_h         = cpu_to_rpmsg32(rpdev, pic->wb_h);
	cmd->wb_pitch     = cpu_to_rpmsg32(rpdev, pic->pitches[2]);
	cmd->contrast     = cpu_to_rpmsg32(rpdev, pic->contrast);
	cmd->brightness   = cpu_to_rpmsg32(rpdev, pic->brightness);
	cmd->hue          = cpu_to_rpmsg32(rpdev, pic->hue);
	cmd->saturation   = cpu_to_rpmsg32(rpdev, pic->saturation);
	cmd->sharp_en     = cpu_to_rpmsg32(rpdev, pic->sharp_en);
	cmd->sharp_value  = cpu_to_rpmsg32(rpdev, pic->sharp_value);
	cmd->crop_width   = cpu_to_rpmsg32(rpdev, pic->crop_w);
	cmd->crop_height  = cpu_to_rpmsg32(rpdev, pic->crop_h);
	cmd->crop_x       = cpu_to_rpmsg32(rpdev, pic->crop_x);
	cmd->crop_y       = cpu_to_rpmsg32(rpdev, pic->crop_y);
	cmd->bufBitDepth  = cpu_to_rpmsg32(rpdev, pic->buf_bit_depth);
	cmd->bufFormat    = cpu_to_rpmsg32(rpdev, pic->buf_format);
	cmd->lumaOffTblAddr = cpu_to_rpmsg32(rpdev, 0);
	cmd->chromaOffTblAddr = cpu_to_rpmsg32(rpdev, 0);
}

int rtk_drm_vowb_run_cmd(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct rtk_drm_vowb *vowb = ((struct rtk_drm_private *)dev->dev_private)->vowb;
	struct rtk_drm_vowb_func2_data *func2 = &vowb->func2_data;
	struct rtk_drm_vowb_run_cmd *arg = data;
	struct video_transcode_picture_object cmd = {};
	dma_addr_t dst_addr, src_addr;
	unsigned long flags;
	int ret;
	spin_lock_irqsave(&vowb->lock, flags);
	ret = rtk_drm_vowb_handle_to_addr(file_priv, arg->pic.handles[0], &src_addr);
	if (!ret)
		ret = rtk_drm_vowb_handle_to_addr(file_priv, arg->pic.handles[2], &dst_addr);
	spin_unlock_irqrestore(&vowb->lock, flags);
	if (ret)
		return ret;

	func2_inband_cmd_video_transcode_picture_object(vowb->tx.rpdev, &cmd, &arg->pic,
							dst_addr, src_addr, ++vowb->emit_job_id);

	ret = rtk_drm_vowb_queue_job(vowb, &func2->job, &cmd, sizeof(cmd));
	if (!ret)
		arg->job_id = func2->job.job_id;
	return ret;
}

int rtk_drm_vowb_check_cmd(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct rtk_drm_vowb *vowb = ((struct rtk_drm_private *)dev->dev_private)->vowb;
	struct rtk_drm_vowb_check_cmd *arg = data;
	u32 arg_flags = arg->flags & RTK_DRM_VOWB_FLAGS_VALID_CHECK_CMD_FLAGS;
	int ret = -EAGAIN;

	if (arg_flags & RTK_DRM_VOWB_FLAGS_CHECK_CMD_BLOCK) {
		ret = wait_event_interruptible(vowb->wq,
					       arg->job_id <= atomic64_read(&vowb->resp_job_id));
	}

	if (arg->job_id <= atomic64_read(&vowb->resp_job_id)) {
		arg->job_done = 1;
		return 0;
	}
	return ret;
}
