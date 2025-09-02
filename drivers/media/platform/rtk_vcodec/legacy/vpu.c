/*
 * Realtek video decoder v4l2 driver
 *
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/kthread.h> // for threads
#include <linux/time.h> // for using jiffies
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include "drv_if.h"
#include "debug.h"
#include "vpu.h"

struct veng_ops *vpu_ve1_ops = NULL;
struct veng_ops *vpu_ve2_ops = NULL;

//#define RTKVPU_DUMP_OUTBUF_EN
#if defined(RTKVPU_DUMP_OUTBUF_EN)
static int gOutbufDumpSerial = 0;
#endif

//#define RTKVPU_DUMP_CAPBUF_EN
#if defined(RTKVPU_DUMP_CAPBUF_EN)
static int gCapbufDumpSerial = 0;
#endif

#ifdef ENABLE_SHOW_VIDEO_INFO
static int width = 0;
static int height = 0;
static uint32_t pixfmt = 0;
#endif // #ifdef ENABLE_SHOW_VIDEO_INFO

#define RTK_DPB_WIDTH_ALIGN 32
#define RTK_DPB_HEIGHT_ALIGN 32
#define RTK_VE2_WIDTH_ALIGN 128
#define RTK_VE2_HEIGHT_ALIGN 128

#define RTK_VPU_DEC_4K_CODED_MAX_WIDTH 4096
#define RTK_VPU_DEC_4K_CODED_MAX_HEIGHT 4096
//4K max resolution : 4096x2304=9437184
#define RTK_VPU_DEC_4K_CODED_MAX_RESOLUTION 9437184
#define RTK_VPU_DEC_2K_CODED_MAX_WIDTH 1920
#define RTK_VPU_DEC_2K_CODED_MAX_HEIGHT 1920
//2K max resolution : 1920x1088=2088960
#define RTK_VPU_DEC_2K_CODED_MAX_RESOLUTION 2088960

#define RTK_VE1_DEC_4K_CODED_MAX_WIDTH 4096
#define RTK_VE1_DEC_4K_CODED_MAX_HEIGHT 3024
//4K max resolution : 4096x3024=12386304
#define RTK_VE1_DEC_4K_CODED_MAX_RESOLUTION 12386304

void vpu_cap_buf_done(struct v4l2_fh *fh, struct vb2_v4l2_buffer *buf, bool eos,
		      enum vb2_buffer_state state);

const static struct vpu_fmt out_fmt[] = {
	/* video engine VE1 output format */
	{
		/* struct v4l2_pix_format */
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 0,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3 * 1024 * 1024, // FIXME: 1024*1024
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		/* enum v4l2_buf_type */
		.spec.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VE1_DEC_4K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VE1_DEC_4K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 4,
		.misc.max_resolution = RTK_VE1_DEC_4K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_ENGINE_1,
	},
	{
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_MPEG1,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 0,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3 * 1024 * 1024, // FIXME: 1024*1024
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		.spec.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VPU_DEC_2K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VPU_DEC_2K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 4,
		.misc.max_resolution = RTK_VPU_DEC_2K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_ENGINE_1,
	},
	{
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_MPEG2,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 0,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3 * 1024 * 1024, // FIXME: 1024*1024
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		.spec.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VPU_DEC_2K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VPU_DEC_2K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 4,
		.misc.max_resolution = RTK_VPU_DEC_2K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_ENGINE_1,
	},
	{
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_MPEG4,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 0,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3 * 1024 * 1024, // FIXME: 1024*1024
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		.spec.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VPU_DEC_2K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VPU_DEC_2K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 4,
		.misc.max_resolution = RTK_VPU_DEC_2K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_ENGINE_1,
	},
	{
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_VP8,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 0,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3 * 1024 * 1024, // FIXME: 1024*1024
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		.spec.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VPU_DEC_2K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VPU_DEC_2K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 4,
		.misc.max_resolution = RTK_VPU_DEC_2K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_ENGINE_1,
	},
	{
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_VC1_ANNEX_G,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 0,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3 * 1024 * 1024, // FIXME: 1024*1024
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		.spec.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VPU_DEC_2K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VPU_DEC_2K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 4,
		.misc.max_resolution = RTK_VPU_DEC_2K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_ENGINE_1,
	},
	{
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_VC1_ANNEX_L,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 0,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3 * 1024 * 1024, // FIXME: 1024*1024
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		.spec.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VPU_DEC_2K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VPU_DEC_2K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 4,
		.misc.max_resolution = RTK_VPU_DEC_2K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_ENGINE_1,
	},
	/* video engine VE2 output format */
	{
		/* struct v4l2_pix_format */
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_HEVC,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 0,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3 * 1024 * 1024,
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		/* enum v4l2_buf_type */
		.spec.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VPU_DEC_4K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VPU_DEC_4K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 15,
		.misc.max_resolution = RTK_VPU_DEC_4K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_ENGINE_2,
	},
	{
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_VP9,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 0,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3 * 1024 * 1024,
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		.spec.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VPU_DEC_4K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VPU_DEC_4K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 10,
		.misc.max_resolution = RTK_VPU_DEC_4K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_ENGINE_2,
	},
	{
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_AV1,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 0,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3 * 1024 * 1024,
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		.spec.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VPU_DEC_4K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VPU_DEC_4K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 10,
		.misc.max_resolution = RTK_VPU_DEC_4K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_ENGINE_2,
	},
};

static const struct vpu_fmt cap_fmt[] = {
	{
		.spec.fmt.pix_mp.width = 1920,
		.spec.fmt.pix_mp.height = 1088,
		.spec.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12,
		.spec.fmt.pix_mp.field = V4L2_FIELD_NONE,
		.spec.fmt.pix_mp.plane_fmt[0].bytesperline = 1920,
		.spec.fmt.pix_mp.plane_fmt[0].sizeimage = 3133440, // 1920*1088*3/2
		.spec.fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
		.spec.fmt.pix_mp.num_planes = 1,
		.spec.fmt.pix_mp.flags = 0,
		.spec.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT,
		.spec.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.spec.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		.spec.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,

		/* struct v4l2_frmsize_stepwise */
		.frmsize.min_width = 64,
		.frmsize.max_width = RTK_VPU_DEC_4K_CODED_MAX_WIDTH,
		.frmsize.step_width = 1,
		.frmsize.min_height = 64,
		.frmsize.max_height = RTK_VPU_DEC_4K_CODED_MAX_HEIGHT,
		.frmsize.step_height = 1,

		/* struct vpu_misc */
		.misc.bufcnt = 10,
		.misc.max_resolution = RTK_VPU_DEC_4K_CODED_MAX_RESOLUTION,
		.misc.VideoEngine = VIDEO_OUTPUT_1,
	},
};

#define OUT_NUM ARRAY_SIZE(out_fmt)
#define CAP_NUM ARRAY_SIZE(cap_fmt)

/*
 * Return vpu_ctx structure for a given struct v4l2_fh
 */
static struct vpu_ctx *fh_to_vpu(struct v4l2_fh *fh)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	return vid_ctx->vpu_ctx;
}

/*
 * Return vpu_ctx structure for a given struct vb2_queue
 */
static struct vpu_ctx *vq_to_vpu(struct vb2_queue *q)
{
	struct videc_ctx *vid_ctx = vb2_get_drv_priv(q);
	return vid_ctx->vpu_ctx;
}

static int vpu_buf_done(struct v4l2_fh *fh, struct vb2_v4l2_buffer *v4l2_buf,
			uint64_t timestamp, uint32_t sizeimage, uint32_t sequence,
			bool eos, bool no_frame)
{
	struct vpu_ctx *ctx = fh_to_vpu(fh);
	unsigned long flags;
	const struct v4l2_event eos_event = { .type = V4L2_EVENT_EOS };

	v4l2_buf->field = V4L2_FIELD_NONE;
	v4l2_buf->flags = V4L2_BUF_FLAG_KEYFRAME;
	v4l2_buf->vb2_buf.timestamp = timestamp;
	v4l2_buf->sequence = sequence++;
	mutex_lock(&ctx->vpu_mutex);
	ctx->seq_cap = sequence;
	mutex_unlock(&ctx->vpu_mutex);

	spin_lock_irqsave(&ctx->vpu_spin_lock, flags);

	if (eos) {
		vpu_info("%d.%s.ctx->last_buf_done = true\n", __LINE__,
			 __func__);
		if (no_frame)
			vb2_set_plane_payload(&v4l2_buf->vb2_buf, 0, 0);
		v4l2_m2m_last_buffer_done(fh->m2m_ctx, v4l2_buf);
		v4l2_event_queue_fh(fh, &eos_event);
		ctx->last_buf_done = true;
	} else {
		v4l2_m2m_buf_done(v4l2_buf, VB2_BUF_STATE_DONE);
	}

	spin_unlock_irqrestore(&ctx->vpu_spin_lock, flags);
	return 0;
}

static int rtkvpu_dump_capbuf(struct vpu_ctx *ctx, struct vb2_v4l2_buffer *v4l2_buf, uint32_t size)
{
#if defined(RTKVPU_DUMP_CAPBUF_EN)
	struct vb2_buffer *vb2_buf = NULL;
	void *vb2_virt_addr = NULL;
	int filp_open_flags;
	ssize_t bytes = 0;
	loff_t pos = 0;

	if ((v4l2_buf == NULL) || (size == 0)) {
		return -1;
	}

	vb2_buf = &(v4l2_buf->vb2_buf);
	vb2_virt_addr = vb2_plane_vaddr(vb2_buf, 0);
	vpu_info("%d.%s.v4l2_buf:0x%px.vb2_virt_addr:0x%px.size:%d\n",__LINE__,__func__,
			(void *)v4l2_buf,vb2_virt_addr,size);

	if (ctx->bNewCapbufDumpFile == 1) {
		ctx->bNewCapbufDumpFile = 0;
		filp_open_flags = O_CREAT | O_WRONLY;
		memset(ctx->capbufDumpFileName, 0, sizeof(unsigned char)*256);
		snprintf(ctx->capbufDumpFileName, 256,
				"/media/removable/32G_BLACK/capbuf_%d.yuv",
				gCapbufDumpSerial);
		gCapbufDumpSerial++;
		vpu_info("%d.%s.create new capbuf dump:%s\n",__LINE__,__func__,
				ctx->capbufDumpFileName);
	} else {
		filp_open_flags = O_APPEND | O_WRONLY;
	}
	ctx->capbufDumpFile =
		(void *)filp_open(ctx->capbufDumpFileName, filp_open_flags, 0);
	if (IS_ERR((struct file *)ctx->capbufDumpFile)) {
		vpu_err("%d.%s.filp_open %s fail\n",__LINE__,__func__,
				ctx->capbufDumpFileName);
	} else {
		//vpu_info("%d.%s.filp_open %s ok\n",__LINE__,__func__,
		//		ctx->capbufDumpFileName);
		bytes =
			kernel_write((struct file *)(ctx->capbufDumpFile),
						(void *)vb2_virt_addr, (size_t)size, &pos);
		//vpu_info("%d.%s.kernel_write bytes:%ld.pos:%lld\n",__LINE__,__func__,
		//		bytes, pos);
		filp_close((struct file *)(ctx->capbufDumpFile), NULL);
		//vpu_info("%d.%s.filp_close %s\n",__LINE__,__func__,
		//		ctx->capbufDumpFileName);
		ctx->capbufDumpFile = NULL;
	}
	return 0;
#else
	return -1;
#endif
}

static int threadcap(void *data)
{
	struct v4l2_fh *fh = (struct v4l2_fh *)data;
	struct vpu_ctx *ctx = fh_to_vpu(fh);
	uint32_t sizeimage, sequence;
	struct vb2_v4l2_buffer *disp_buf = NULL;

	while (1) {
		int ret;
		ret = wait_event_interruptible_timeout(
			ctx->vpu_cap_waitq,
			kthread_should_stop() ||
				(ctx->veng_ops->ve_get_undq_dispFrm_cnt(fh)),
			msecs_to_jiffies(ctx->thread_cap_interval));

		if (kthread_should_stop() || (ret == -ERESTART)) {
			if (!ctx->veng_ops->ve_get_undq_dispFrm_cnt(fh)) {
				vpu_info("%d.%s.return 1\n", __LINE__,
					 __func__);
				return 1;
			}
		}

		if (!ctx->veng_ops->ve_get_undq_dispFrm_cnt(fh) &&
		    !ctx->stop_cmd) {
			// SW-7622, sometimes ve1_ctx->last_frame is set after all 30 frames are dequeued, threadcap won't flow to ve_cap_dqbuf() + ve_get_info() + vpu_buf_done(), vpu_ctx->last_buf_done won't be set.
			// Add "!ctx->stop_cmd" to flow to ve_cap_dqbuf() + ve_get_info() + vpu_buf_done(), ve1_cap_dqbuf() will set vpu_ctx->last_frame if ve1_ctx->last_frame is set
			continue;
		}

		ret = 0;
		for (; !ret;) {
			uint64_t timestamp = 0;
			bool eos = 0;
			bool no_frame = 0;

			mutex_lock(&ctx->vpu_mutex);
			sizeimage = ctx->cap_fmt.spec.fmt.pix_mp.plane_fmt[0].sizeimage;
			sequence = ctx->seq_cap;
			mutex_unlock(&ctx->vpu_mutex);

			ret = ctx->veng_ops->ve_cap_dqbuf(fh, NULL, &timestamp,
							  &disp_buf);
			if (!ret) {
				rtkvpu_dump_capbuf(ctx, disp_buf, sizeimage);

				if (ctx->veng_ops->ve_get_info)
					ctx->veng_ops->ve_get_info(fh, &eos,
								   &no_frame);

				if (vpu_buf_done(fh, disp_buf, timestamp, sizeimage,
						 sequence, eos, no_frame))
					break;

				ctx->cap_retry_cnt = 0;
			} else {
				if (ret == -EAGAIN) {
					ctx->cap_retry_cnt++;

					if (ctx->cap_retry_cnt > 1000) {
						ctx->cap_retry_cnt = 0;
					}
				}
				usleep_range(1000, 1000);
				break;
			}
		}
	}

	return 0;
}

static void rtkvpu_dump_outbuf(struct vpu_ctx *ctx, uint8_t* buf,
					uint32_t offset, uint32_t len)
{
#if defined(RTKVPU_DUMP_OUTBUF_EN)
	int filp_open_flags;
	ssize_t bytes = 0;
	loff_t pos = 0;

	if (ctx == NULL) {
		return;
	}

	if ((buf != NULL) && (len != 0)) {
		if (ctx->bNewOutbufDumpFile == 1) {
			ctx->bNewOutbufDumpFile = 0;
			filp_open_flags = O_CREAT | O_WRONLY;
			memset(ctx->outbufDumpFileName, 0, sizeof(unsigned char)*256);
			snprintf(ctx->outbufDumpFileName, 256,
					"/mnt/outbuf_%d.es",
					gOutbufDumpSerial);
			gOutbufDumpSerial++;
			vpu_info("%d.%s.create new outbuf dump:%s\n",__LINE__,__func__,
					ctx->outbufDumpFileName);
		} else {
			filp_open_flags = O_APPEND | O_WRONLY;
		}
		ctx->outbufDumpFile =
			(void *)filp_open(ctx->outbufDumpFileName, filp_open_flags, 0);
		if (IS_ERR((struct file *)ctx->outbufDumpFile)) {
			vpu_err("%d.%s.filp_open %s fail\n",__LINE__,__func__,
					ctx->outbufDumpFileName);
		} else {
			//vpu_info("%d.%s.filp_open %s ok\n",__LINE__,__func__,
			//		ctx->outbufDumpFileName);
			bytes =
				kernel_write((struct file *)(ctx->outbufDumpFile),
							(void *)(buf + offset), (size_t)len, &pos);
			//vpu_info("%d.%s.kernel_write bytes:%ld.pos:%lld\n",__LINE__,__func__,
			//		bytes, pos);
			filp_close((struct file *)(ctx->outbufDumpFile), NULL);
			//vpu_info("%d.%s.filp_close %s\n",__LINE__,__func__,
			//		ctx->outbufDumpFileName);
			ctx->outbufDumpFile = NULL;
		}
	}
#endif
}

static int threadout(void *data)
{
	struct v4l2_fh *fh = (struct v4l2_fh *)data;
	struct vpu_ctx *ctx = fh_to_vpu(fh);
	uint32_t sizeimage, sequence;
	struct vb2_v4l2_buffer *v4l2_buf;
	unsigned long flags;
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	int ret = 0, qbuf_ret = 0;
	const struct v4l2_event eos_event = { .type = V4L2_EVENT_EOS };
	struct vb2_queue *dst_vq = NULL;
	bool bForceEscapeDone = false;

	while (1) {
		ret = wait_event_interruptible_timeout(
			ctx->vpu_out_waitq,
			(kthread_should_stop() ||
			(v4l2_m2m_num_src_bufs_ready(fh->m2m_ctx)
			&& (qbuf_ret != -ENOSPC))),
			msecs_to_jiffies(ctx->thread_out_interval));

		if (kthread_should_stop() || (ret == -ERESTART)) {
			for (;;) {
				v4l2_buf = v4l2_m2m_src_buf_remove(fh->m2m_ctx);
				if (v4l2_buf == NULL)
					return 1;

				spin_lock_irqsave(&ctx->vpu_spin_lock, flags);
				v4l2_m2m_buf_done(v4l2_buf,
						  VB2_BUF_STATE_ERROR);
				spin_unlock_irqrestore(&ctx->vpu_spin_lock,
						       flags);
			}
		}

		if (ctx->stop_cmd &&
		    !v4l2_m2m_num_src_bufs_ready(fh->m2m_ctx) &&
		    !ctx->last_buf_done) {
			ret = op->vpu_stop_cmd(fh);
			if ((ret < 0) && (!bForceEscapeDone)) {
				dst_vq = v4l2_m2m_get_vq(
					fh->m2m_ctx,
					V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

				if (list_empty(&dst_vq->done_list)) {
					vpu_info("%d.%s.force last_buffer_dequeued\n",
						 __LINE__, __func__);
					dst_vq->last_buffer_dequeued = true;
					wake_up(&dst_vq->done_wq);
					v4l2_event_queue_fh(fh, &eos_event);
					bForceEscapeDone = true;
				}
			}
		}

#ifdef ENABLE_SHOW_VIDEO_INFO
		pixfmt = ctx->out_fmt.spec.fmt.pix_mp.pixelformat;
#endif // #ifdef ENABLE_SHOW_VIDEO_INFO

		if (v4l2_m2m_num_src_bufs_ready(fh->m2m_ctx)) {
			uint8_t *p;
			uint32_t offset, len;
			for (;;) {
				v4l2_buf = v4l2_m2m_next_src_buf(fh->m2m_ctx);
				if (!v4l2_buf)
					break;

				mutex_lock(&ctx->vpu_mutex);
				sizeimage = ctx->out_fmt.spec.fmt.pix_mp.plane_fmt[0].sizeimage;
				sequence = ctx->seq_out;
				mutex_unlock(&ctx->vpu_mutex);

				p = (ctx->memory_out == V4L2_MEMORY_DMABUF) ?
					    ((uint8_t *)
						     vb2_dma_contig_plane_dma_addr(
							     &v4l2_buf->vb2_buf,
							     0)) :
					    (vb2_plane_vaddr(&v4l2_buf->vb2_buf,
							     0));
				len = v4l2_buf->vb2_buf.planes[0].bytesused;
				offset =
					v4l2_buf->vb2_buf.planes[0].data_offset;

				qbuf_ret = ctx->veng_ops->ve_out_qbuf(fh, p + offset,
						len, v4l2_buf->vb2_buf.timestamp,
						sequence);
				if (!qbuf_ret) {
					rtkvpu_dump_outbuf(ctx, p, offset, len);

					v4l2_buf = v4l2_m2m_src_buf_remove(
						fh->m2m_ctx);
					if (!v4l2_buf) {
						vpu_warn(
							"No src buffer to remove\n");
						break;
					}

					/* Complete first bitstream parsing only first */
					if (!sequence) {
						complete(&ctx->bs_parsing_comp);
						vpu_info(
							"%d.%s.complete bs_parsing_comp for bitstream parsing\n",
							__LINE__, __func__);
					}
					v4l2_buf->sequence = sequence++;

					mutex_lock(&ctx->vpu_mutex);
					ctx->seq_out = sequence;
					mutex_unlock(&ctx->vpu_mutex);

					vb2_set_plane_payload(
						&v4l2_buf->vb2_buf, 0,
						sizeimage);

					spin_lock_irqsave(&ctx->vpu_spin_lock,
							  flags);
					v4l2_m2m_buf_done(v4l2_buf,
							  VB2_BUF_STATE_DONE);
					spin_unlock_irqrestore(
						&ctx->vpu_spin_lock, flags);
					continue;
				} else if (qbuf_ret == -EINVAL || qbuf_ret == -EIO) {
					vpu_info("%s ve_out_qbuf error, buf done VB2_BUF_STATE_ERROR\n", __func__);
					v4l2_buf = v4l2_m2m_src_buf_remove(fh->m2m_ctx);
					if (v4l2_buf)
						v4l2_m2m_buf_done(v4l2_buf, VB2_BUF_STATE_ERROR);
					continue;
				}

				break;
			}
		}
	}

	return 0;
}

static int vpu_enum_fmt_cap(struct v4l2_fmtdesc *f)
{
	if (f->index < ARRAY_SIZE(cap_fmt)) {
		/* Format found */
		f->pixelformat = cap_fmt[f->index].spec.fmt.pix_mp.pixelformat;
		return 0;
	}
	return -EINVAL;
}

static int vpu_enum_fmt_out(struct v4l2_fmtdesc *f)
{
	if (f->index < ARRAY_SIZE(out_fmt)) {
		/* Format found */
		f->pixelformat = out_fmt[f->index].spec.fmt.pix_mp.pixelformat;
		return 0;
	}
	return -EINVAL;
}

static int vpu_g_fmt(struct v4l2_fh *fh, struct v4l2_format *f)
{
	struct vpu_ctx *ctx = fh_to_vpu(fh);

	mutex_lock(&ctx->vpu_mutex);
	if (V4L2_TYPE_IS_OUTPUT(f->type))
		memcpy(&f->fmt.pix_mp, &ctx->out_fmt.spec.fmt.pix_mp,
		       sizeof(struct v4l2_pix_format_mplane));
	else
		memcpy(&f->fmt.pix_mp, &ctx->cap_fmt.spec.fmt.pix_mp,
		       sizeof(struct v4l2_pix_format_mplane));
	mutex_unlock(&ctx->vpu_mutex);
	return 0;
}

static const struct vpu_fmt *find_format(struct v4l2_format *f)
{
	const struct vpu_fmt *fmt;
	unsigned int size = 0;
	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		fmt = &out_fmt[0];
		size = OUT_NUM;
	} else {
		fmt = &cap_fmt[0];
		size = CAP_NUM;
	}

	for (i = 0; i < size; i++) {
		if (fmt[i].spec.fmt.pix_mp.pixelformat == f->fmt.pix_mp.pixelformat)
			break;
	}

	if (i == size) {
		vpu_warn("unknow type %d format %p4cc, use default\n",
			 f->type, &f->fmt.pix_mp.pixelformat);
		return NULL;
	}

	return &fmt[i];
}

static int vpu_try_fmt(struct v4l2_fh *fh, struct v4l2_format *f)
{
	struct vpu_ctx *ctx = fh_to_vpu(fh);
	const struct vpu_fmt *fmt;
	enum v4l2_field field;
	unsigned int width = 0;
	unsigned int height = 0;
	unsigned int bytesperline = 0;
	unsigned int sizeimage = 0;

	fmt = find_format(f);
	if (!fmt) {
		if (V4L2_TYPE_IS_OUTPUT(f->type))
			fmt = &out_fmt[0];
		else
			fmt = &cap_fmt[0];

		f->fmt.pix_mp.pixelformat = fmt->spec.fmt.pix_mp.pixelformat;
	}

	field = V4L2_FIELD_NONE;

	width = clamp(f->fmt.pix_mp.width, fmt->frmsize.min_width,
		fmt->frmsize.max_width);
	height = clamp(f->fmt.pix_mp.height, fmt->frmsize.min_height,
		fmt->frmsize.max_height);

	if (V4L2_TYPE_IS_CAPTURE(f->type)) {
		if (ctx->out_fmt.misc.VideoEngine == VIDEO_ENGINE_2) {
			if(ctx->ddr_width && ctx->ddr_height) {
				width = ctx->ddr_width;
				height = ctx->ddr_height;
			}
			else if(ctx->is_bs_error){
				width = ALIGN(width, RTK_VE2_WIDTH_ALIGN);
				height = ALIGN(height, RTK_VE2_HEIGHT_ALIGN);
			}
		} else {
			width = ALIGN(width, RTK_DPB_WIDTH_ALIGN);
			height = ALIGN(height, RTK_DPB_HEIGHT_ALIGN);
		}

		bytesperline = width;
		sizeimage = bytesperline * height * 3 / 2;
		if(ctx->is_bs_error) sizeimage = sizeimage * 10 / 8;
	} else {
		width = ALIGN(width, RTK_DPB_WIDTH_ALIGN);
		height = ALIGN(height, RTK_DPB_HEIGHT_ALIGN);

		bytesperline = 0;
		sizeimage = fmt->spec.fmt.pix_mp.plane_fmt[0].sizeimage;
	}

	f->fmt.pix_mp.width = width;
	f->fmt.pix_mp.height = height;
	f->fmt.pix_mp.field = field;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = bytesperline;
	f->fmt.pix_mp.plane_fmt[0].sizeimage = sizeimage;
	f->fmt.pix_mp.num_planes = 1;

	return 0;
}

static int vpu_s_fmt_cap(struct v4l2_fh *fh, struct v4l2_format *f)
{
	const struct vpu_fmt *fmt;
	struct vpu_ctx *ctx = fh_to_vpu(fh);
	unsigned int width = 0;
	unsigned int height = 0;

	fmt = find_format(f);
	if (!fmt)
		memcpy(f, &cap_fmt[0], sizeof(struct v4l2_format));

	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	vpu_try_fmt(fh, f);

	mutex_lock(&ctx->vpu_mutex);
	memcpy(&ctx->cap_fmt.spec, f, sizeof(struct v4l2_format));
	ctx->cap_fmt.misc.ori_width = (width > 0) ? width : f->fmt.pix_mp.width;
	ctx->cap_fmt.misc.ori_height = (height > 0) ? height : f->fmt.pix_mp.height;
	ctx->out_fmt.spec.fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	ctx->out_fmt.spec.fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	ctx->out_fmt.spec.fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;
	ctx->out_fmt.spec.fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;

	mutex_unlock(&ctx->vpu_mutex);

	return 0;
}

static int vpu_s_fmt_out(struct v4l2_fh *fh, struct v4l2_format *f)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	struct vpu_ctx *ctx = fh_to_vpu(fh);
	const struct vpu_fmt *fmt = NULL;
	int pixelformat = 0;
	int ret = 0;

	fmt = find_format(f);
	if (!fmt) {
		fmt = &out_fmt[0];
		memcpy(f, &fmt->spec, sizeof(struct v4l2_format));
	}

	mutex_lock(&ctx->vpu_mutex);
	if (f->fmt.pix_mp.width * f->fmt.pix_mp.height > fmt->misc.max_resolution) {
		vpu_err("%s, %d x %d over spec ", __func__, f->fmt.pix_mp.width, f->fmt.pix_mp.height);
		ret = -EINVAL;
		goto exit;
	}

	ctx->rect.width = f->fmt.pix_mp.width;
	ctx->rect.height = f->fmt.pix_mp.height;
	mutex_unlock(&ctx->vpu_mutex);

	vpu_try_fmt(fh, f);

	pixelformat = f->fmt.pix_mp.pixelformat;
	mutex_lock(&ctx->vpu_mutex);
	switch (pixelformat) {
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_MPEG1:
	case V4L2_PIX_FMT_MPEG2:
	case V4L2_PIX_FMT_MPEG4:
	case V4L2_PIX_FMT_VP8:
	case V4L2_PIX_FMT_VC1_ANNEX_G:
	case V4L2_PIX_FMT_VC1_ANNEX_L:
		/* Set VE1 ops */
		if (!ctx->ve1_ops) {
			ret = -EINVAL;
			goto exit;
		}
		ctx->veng_ops = ctx->ve1_ops;
		ctx->out_fmt.misc.VideoEngine = VIDEO_ENGINE_1;
		break;
	case V4L2_PIX_FMT_HEVC:
	case V4L2_PIX_FMT_VP9:
	case V4L2_PIX_FMT_AV1:
		/* Set VE2 ops */
		if (!ctx->ve2_ops) {
			ret =  -EINVAL;
			goto exit;
		}
		ctx->veng_ops = ctx->ve2_ops;
		ctx->out_fmt.misc.VideoEngine = VIDEO_ENGINE_2;
		break;
	default:
		vpu_err("Unsupported format=%p4cc\n", &pixelformat);
		break;
	}

	memcpy(&ctx->out_fmt.spec, f, sizeof(struct v4l2_format));
	memcpy(&ctx->out_fmt.frmsize, &fmt->frmsize, sizeof(struct v4l2_frmsize_stepwise));
	memcpy(&ctx->out_fmt.misc, &fmt->misc, sizeof(struct vpu_misc));
	ctx->cap_fmt.spec.fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	ctx->cap_fmt.spec.fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	ctx->cap_fmt.spec.fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;
	ctx->cap_fmt.spec.fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
	ctx->cap_fmt.spec.fmt.pix_mp.width = f->fmt.pix_mp.width;
	ctx->cap_fmt.spec.fmt.pix_mp.height = f->fmt.pix_mp.height;
	ctx->cap_fmt.spec.fmt.pix_mp.plane_fmt[0].bytesperline = f->fmt.pix_mp.width;
	ctx->cap_fmt.spec.fmt.pix_mp.plane_fmt[0].sizeimage =
		f->fmt.pix_mp.width * f->fmt.pix_mp.height * 3 / 2;

	/* Allocate video engine context */
	if (!vid_ctx->ve_ctx) {
		vid_ctx->ve_ctx =
			ctx->veng_ops->ve_alloc_context(fh);
		if (!vid_ctx->ve_ctx) {
			ret = -EINVAL;
			goto exit;
		}
	}

exit:
	mutex_unlock(&ctx->vpu_mutex);

	return ret;
}

static int vpu_queue_info(struct vb2_queue *vq, int *bufcnt,
			  unsigned int *nplanes, int *sizeimage)
{
	struct vpu_ctx *ctx = vq_to_vpu(vq);
	const struct vpu_fmt *fmt;
	int type = vq->type;

	mutex_lock(&ctx->vpu_mutex);
	if (V4L2_TYPE_IS_OUTPUT(type)) {
		if (ctx->out_fmt.spec.type == -1) {
			mutex_unlock(&ctx->vpu_mutex);
			return -EPERM;
		}

		fmt = &ctx->out_fmt;
		if (bufcnt)
			*bufcnt = fmt->misc.bufcnt;
	} else {
		if (ctx->cap_fmt.spec.type == -1) {
			mutex_unlock(&ctx->vpu_mutex);
			return -EPERM;
		}

		fmt = &ctx->cap_fmt;
	}

	if (nplanes) {
		if (*nplanes) {
			if (*sizeimage < fmt->spec.fmt.pix_mp.plane_fmt[0].sizeimage) {
				mutex_unlock(&ctx->vpu_mutex);
				return -EINVAL;
			}
		} else {
			*nplanes = 1;
		}
	}

	if (sizeimage) {
		*sizeimage = fmt->spec.fmt.pix_mp.plane_fmt[0].sizeimage;
#ifdef PREPEND_METADATA
		if (V4L2_TYPE_IS_CAPTURE(type))
			*sizeimage += METADATA_OFFSET;
#endif
	}

	mutex_unlock(&ctx->vpu_mutex);
	return 0;
}

int vpu_start_streaming(struct vb2_queue *q, uint32_t count)
{
	struct videc_ctx *vid_ctx = vb2_get_drv_priv(q);
	struct vpu_ctx *ctx = vid_ctx->vpu_ctx;
	struct v4l2_fh *fh = &vid_ctx->fh;
	int ret,i;
	int pixelformat = ctx->out_fmt.spec.fmt.pix_mp.pixelformat;

	if (!ctx->veng_ops){
		ret = -EINVAL;
		goto exit;
	}

	mutex_lock(&ctx->vpu_mutex);
	ret = ctx->veng_ops->ve_start_streaming(q, count, pixelformat);
	if (ret) {
		vpu_err("Failed to start streaming %d\n", ret);
		if (vid_ctx->ve_ctx) {
			ctx->veng_ops->ve_free_context(vid_ctx->ve_ctx);
			vid_ctx->ve_ctx = NULL;
		}
		mutex_unlock(&ctx->vpu_mutex);
		goto exit;
	}

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		ctx->seq_out = 0;
		ctx->memory_out = q->memory;
		ctx->thread_out = kthread_run(
			threadout, fh, "outthread %p4cc", &pixelformat);
		ctx->is_out_started = 1;
#if defined(RTKVPU_DUMP_OUTBUF_EN)
		ctx->bNewOutbufDumpFile = 1;
#endif
	} else {
		ctx->seq_cap = 0;
		ctx->memory_cap = q->memory;
		ctx->thread_cap = kthread_run(
			threadcap, fh, "capthread %p4cc", &pixelformat);
		ctx->is_cap_started = 1;
#if defined(RTKVPU_DUMP_CAPBUF_EN)
		ctx->bNewCapbufDumpFile = 1;
#endif
	}
	mutex_unlock(&ctx->vpu_mutex);

	if (ctx->out_fmt.misc.VideoEngine == VIDEO_ENGINE_1 &&
	    v4l2_m2m_num_src_bufs_ready(fh->m2m_ctx) &&
	    V4L2_TYPE_IS_OUTPUT(q->type)) {
		vpu_info(
			"%d.%s.waiting most 1s for bitstream parsing to be done\n",
			__LINE__, __func__);
		wait_for_completion_timeout(&ctx->bs_parsing_comp,
					    msecs_to_jiffies(1000));
	}

	return ret;

exit:
	for (i = 0; i < q->num_buffers; ++i) {
		struct vb2_buffer *buf = vb2_get_buffer(q, i);
		if (buf->state == VB2_BUF_STATE_ACTIVE) {
			vpu_err("id=%d, type=%d, %d -> VB2_BUF_STATE_QUEUED",
					i, q->type,(int)buf->state);
			v4l2_m2m_buf_done(to_vb2_v4l2_buffer(buf),
					  VB2_BUF_STATE_QUEUED);
		}
	}

	return ret;
}

int vpu_stop_streaming(struct vb2_queue *q)
{
	struct videc_ctx *vid_ctx = vb2_get_drv_priv(q);
	struct vpu_ctx *ctx = vid_ctx->vpu_ctx;
	int ret = 0;
	int num_src_bufs_ready = 0;
	struct vb2_v4l2_buffer *v4l2_buf = NULL;
	//int i = 0;
	//struct vb2_buffer *vb2 = NULL;

	if (!ctx || !ctx->veng_ops)
		return -EINVAL;

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		if (ctx->thread_out) {
			wake_up_interruptible(&ctx->vpu_out_waitq);
			kthread_stop(ctx->thread_out);
			ctx->thread_out = NULL;
		}
	} else {
		if (ctx->thread_cap) {
			wake_up_interruptible(&ctx->vpu_cap_waitq);
			kthread_stop(ctx->thread_cap);
			ctx->thread_cap = NULL;
		}
	}

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		num_src_bufs_ready = v4l2_m2m_num_src_bufs_ready(vid_ctx->fh.m2m_ctx);
		vpu_info("%d.%s.num_src_bufs_ready:%d\n",
					__LINE__, __func__, num_src_bufs_ready);
		if (num_src_bufs_ready) {
			do {
				v4l2_buf = v4l2_m2m_next_src_buf(vid_ctx->fh.m2m_ctx);
				if (!v4l2_buf) {
					break;
				}
				v4l2_buf = v4l2_m2m_src_buf_remove(vid_ctx->fh.m2m_ctx);
				v4l2_m2m_buf_done(v4l2_buf, VB2_BUF_STATE_ERROR);
			} while (1);
		}
		/*for (i=0;i<q->num_buffers;i++) {
			vb2 = q->bufs[i];
			vpu_info("%d.%s.out_vb2[%d].index:%d.state:%d.vb2:0x%px\n",
				__LINE__, __func__, i,
				vb2->index,
				vb2->state,
				vb2);
		}*/
	}

	mutex_lock(&ctx->vpu_mutex);
	if (ctx && ctx->veng_ops) {
		ret = ctx->veng_ops->ve_stop_streaming(q);
		if (ret)
			vpu_err("fail to stop streaming %d\n", ret);
	}

	if (V4L2_TYPE_IS_OUTPUT(q->type))
		ctx->is_out_started = 0;
	else
		ctx->is_cap_started = 0;
	if (!ctx->is_cap_started && !ctx->is_out_started) {
		reinit_completion(&ctx->bs_parsing_comp);
	}

	mutex_unlock(&ctx->vpu_mutex);
	return ret;
}

static int vpu_qbuf(struct v4l2_fh *fh, struct vb2_buffer *vb)
{
	struct vpu_ctx *ctx = vq_to_vpu(vb->vb2_queue);
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	int ret;

	if (!ctx->veng_ops)
		return -EINVAL;

	if (V4L2_TYPE_IS_OUTPUT(vb->type)) {
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t min_reqbuf = 0;
		uint32_t bit_depth = 0;
		ctx->out_q_cnt++;
		if (!ctx->parse_header_done && !ctx->is_decoder_error &&
		    ctx->veng_ops->ve_out_pre_parse) {
			ret = ctx->veng_ops->ve_out_pre_parse(
				fh, vb, &width, &height, &min_reqbuf, &bit_depth);
			if (ret == 0 && (width && height && min_reqbuf))
				ctx->parse_header_done = true;
		}

		wake_up_interruptible(&ctx->vpu_out_waitq);
		return 0;
	} else {
		if (ctx->stop_cmd && !v4l2_m2m_num_src_bufs_ready(fh->m2m_ctx))
			op->vpu_stop_cmd(fh);

		ret = ctx->veng_ops->ve_cap_qbuf(fh, vb);
		if (!ret) {
			wake_up_interruptible(&ctx->vpu_cap_waitq);
		}
		return ret;
	}
	return -EINVAL;
}

static int vpu_abort(void *priv, int type)
{
	struct videc_ctx *vid_ctx = priv;
	struct vpu_ctx *ctx = vid_ctx->vpu_ctx;
	int ret;

	if (!ctx->veng_ops)
		return -EINVAL;

	ret = ctx->veng_ops->ve_abort(vid_ctx->ve_ctx, type);
	if (ret) {
		vpu_err("fail to abort streaming(ve2_abort %d)\n", ret);
	}

	return 0;
}

static int vpu_g_crop(void *fh, struct v4l2_rect *rect)
{
	/* G_SELECTION */
	struct vpu_ctx *ctx = fh_to_vpu(fh);

	mutex_lock(&ctx->vpu_mutex);
	memcpy(rect, &ctx->rect, sizeof(ctx->rect));
	mutex_unlock(&ctx->vpu_mutex);
	return 0;
}

static int vpu_stop_cmd(void *fh)
{
	struct vpu_ctx *ctx = fh_to_vpu(fh);
	int ret = 0;

	if (!ctx->veng_ops)
		return -EINVAL;

	if (ctx->veng_ops->ve_stop_cmd)
		ret = ctx->veng_ops->ve_stop_cmd(
			fh, ctx->out_fmt.spec.fmt.pix_mp.pixelformat);

	return ret;
}

static int vpu_start_cmd(void *fh)
{
	struct vpu_ctx *ctx = fh_to_vpu(fh);
	int ret = 0;

	if (!ctx->veng_ops)
		return -EINVAL;

	if (ctx->veng_ops->ve_start_cmd)
		ret = ctx->veng_ops->ve_start_cmd(fh);

	return ret;
}

static int vpu_reset_resource(void *fh)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	struct vpu_ctx *ctx = fh_to_vpu(fh);

	if(vid_ctx && vid_ctx->ve_ctx &&
		ctx && ctx->veng_ops) {
		if (ctx->thread_out) {
			wake_up_interruptible(&ctx->vpu_out_waitq);
			kthread_stop(ctx->thread_out);
			ctx->thread_out = NULL;
		}

		if (ctx->thread_cap) {
			wake_up_interruptible(&ctx->vpu_cap_waitq);
			kthread_stop(ctx->thread_cap);
			ctx->thread_cap = NULL;
		}

		mutex_lock(&ctx->vpu_mutex);
		ctx->veng_ops->ve_free_context(vid_ctx->ve_ctx);
		ctx->veng_ops = NULL;

		// reset other variables of struct videc_ctx
		// init ctx->out with default value
		memcpy(&ctx->out_fmt, &out_fmt[0], sizeof(struct vpu_fmt));
		// init ctx->cap with default value
		memcpy(&ctx->cap_fmt, &cap_fmt[0], sizeof(struct vpu_fmt));
		memset(&ctx->rect, 0, sizeof(struct v4l2_rect));
		ctx->thread_out = NULL;
		ctx->thread_cap = NULL;
		ctx->seq_out = 1;
		ctx->seq_cap = 1;
		ctx->memory_out = 0;
		ctx->memory_cap = 0;
		ctx->stop_cmd = false;
		ctx->last_buf_done = false;
		ctx->cap_retry_cnt = 0;
		ctx->out_q_cnt = 0;
		mutex_unlock(&ctx->vpu_mutex);
	}

	return 0;
}

static int vpu_free_capture(void *fh)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	struct vpu_ctx *ctx = fh_to_vpu(fh);

	mutex_lock(&ctx->vpu_mutex);
	if(vid_ctx && vid_ctx->ve_ctx &&
		ctx && ctx->veng_ops && ctx->veng_ops->ve_free_capture) {
		if (ctx->thread_cap) {
			wake_up_interruptible(&ctx->vpu_cap_waitq);
			kthread_stop(ctx->thread_cap);
			ctx->thread_cap = NULL;
		}
		ctx->veng_ops->ve_free_capture(vid_ctx->ve_ctx);
		ctx->seq_cap = 1;
		ctx->memory_cap = 0;
		ctx->cap_retry_cnt = 0;
	}
	mutex_unlock(&ctx->vpu_mutex);

	return 0;
}

void *vpu_get_frmsize(uint32_t pixel_format)
{
	int i = 0;

	for (i = 0; i < OUT_NUM; i++) {
		if (pixel_format == out_fmt[i].spec.fmt.pix_mp.pixelformat)
			return (void *)&out_fmt[i].frmsize;
	}

	for (i = 0; i < CAP_NUM; i++) {
		if (pixel_format == cap_fmt[i].spec.fmt.pix_mp.pixelformat)
			return (void *)&cap_fmt[i].frmsize;
	}

	return NULL;
}

int vpu_update_rect(void *fh, struct v4l2_rect *rect)
{
	struct vpu_ctx *ctx = fh_to_vpu(fh);
	if (ctx && rect) {
		mutex_lock(&ctx->vpu_mutex);
		memcpy(&ctx->rect, rect, sizeof(struct v4l2_rect));
		vpu_info("%d.%s.rect(%d,%d,%d,%d)\n",
			__LINE__, __func__,
			ctx->rect.top, ctx->rect.left,
			ctx->rect.width, ctx->rect.height);
		mutex_unlock(&ctx->vpu_mutex);
	}

	return 0;
}
EXPORT_SYMBOL(vpu_update_rect);

int vpu_get_cap_fmt(void *fh, void *cap_fmt)
{
	struct vpu_ctx *ctx = fh_to_vpu(fh);

	if (ctx && cap_fmt) {
		mutex_lock(&ctx->vpu_mutex);
		memcpy(cap_fmt, &ctx->cap_fmt, sizeof(struct vpu_fmt));
		mutex_unlock(&ctx->vpu_mutex);
	}
	return 0;
}
EXPORT_SYMBOL(vpu_get_cap_fmt);

int vpu_update_cap_fmt(void *fh, void *cap_fmt)
{
	struct vpu_ctx *ctx = fh_to_vpu(fh);
	struct vpu_fmt *v_fmt = (struct vpu_fmt *)cap_fmt;

	if (ctx && cap_fmt) {
		mutex_lock(&ctx->vpu_mutex);
		v_fmt->misc.ori_width = v_fmt->spec.fmt.pix_mp.width;
		v_fmt->misc.ori_height = v_fmt->spec.fmt.pix_mp.height;
		ctx->rect.width = v_fmt->misc.ori_width;
		ctx->rect.height = v_fmt->misc.ori_height;

		vpu_try_fmt(fh, &v_fmt->spec);
		memcpy(&ctx->cap_fmt, v_fmt, sizeof(struct vpu_fmt));
		mutex_unlock(&ctx->vpu_mutex);
	}
	return 0;
}
EXPORT_SYMBOL(vpu_update_cap_fmt);

#ifdef ENABLE_SHOW_VIDEO_INFO
int vpu_keep_fm_info(int pix_width, int pix_height)
{
	width = pix_width;
	height = pix_height;
	return 0;
}
EXPORT_SYMBOL(vpu_keep_fm_info);

int vpu_get_video_info(char hasVideo, char *buf)
{
	char buffer[8];

	if (hasVideo == 0) {
		pixfmt = 0;
		width = 0;
		height = 0;
	}

	if (pixfmt)
		snprintf(buffer, 5, "%s", (char *)&pixfmt);
	else
		sprintf(buffer, "Unknown");

	return sprintf(buf, "%d %s %d %d\n", hasVideo, buffer, width, height);
}
#endif // #ifdef ENABLE_SHOW_VIDEO_INFO

void vpu_notify_event_resolution_change(void *fh)
{
	static const struct v4l2_event event_source_change = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION
	};

	v4l2_event_queue_fh(fh, &event_source_change);
}
EXPORT_SYMBOL(vpu_notify_event_resolution_change);

void vpu_update_resolution_change(void *fh, uint32_t width, uint32_t height,
			      uint32_t ddr_width, uint32_t ddr_height, uint32_t bit_depth,
			      uint32_t min_reqbuf)
{
	struct vpu_ctx *v_ctx =  fh_to_vpu(fh);
	struct vpu_fmt vpu_fmt;

	vpu_get_cap_fmt(fh, (void *)&vpu_fmt);
	if (vpu_fmt.misc.ori_width != width ||
			vpu_fmt.misc.ori_height != height ||
			vpu_fmt.misc.bufcnt != min_reqbuf ||
			v_ctx->bit_depth != ((bit_depth == 0)?8:10) ||
			v_ctx->ddr_width != ddr_width ||
			v_ctx->ddr_height != ddr_height) {
		vpu_fmt.spec.fmt.pix_mp.width = width;
		vpu_fmt.spec.fmt.pix_mp.height = height;
		vpu_fmt.spec.fmt.pix_mp.plane_fmt[0].bytesperline = width;
		vpu_fmt.misc.bufcnt = min_reqbuf;
		v_ctx->bit_depth = (bit_depth == 0)?8:10;
		v_ctx->ddr_width = ddr_width;
		v_ctx->ddr_height = ddr_height;

		vpu_update_cap_fmt(fh, (void *)&vpu_fmt);
		vpu_notify_event_resolution_change(fh);
	}
}
EXPORT_SYMBOL(vpu_update_resolution_change);

int vpu_check_sub_res_chg(void *fh)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);

	if(!fh) {
		vpu_err("%s fh is NULL", __func__);
		return 0;
	}

	if(!vid_ctx) {
		vpu_err("%s vid_ctx is NULL", __func__);
		return 0;
	}

	return vid_ctx->is_sub_res_chg;
}
EXPORT_SYMBOL(vpu_check_sub_res_chg);


const static struct vpu_fmt_ops ops = {
	.vpu_enum_fmt_cap = vpu_enum_fmt_cap,
	.vpu_enum_fmt_out = vpu_enum_fmt_out,
	.vpu_g_fmt = vpu_g_fmt,
	.vpu_try_fmt_cap = vpu_try_fmt,
	.vpu_try_fmt_out = vpu_try_fmt,
	.vpu_s_fmt_cap = vpu_s_fmt_cap,
	.vpu_s_fmt_out = vpu_s_fmt_out,
	.vpu_queue_info = vpu_queue_info,
	.vpu_start_streaming = vpu_start_streaming,
	.vpu_stop_streaming = vpu_stop_streaming,
	.vpu_qbuf = vpu_qbuf,
	.vpu_abort = vpu_abort,
	.vpu_g_crop = vpu_g_crop,
	.vpu_stop_cmd = vpu_stop_cmd,
	.vpu_free_capture = vpu_free_capture,
	.vpu_reset_resource = vpu_reset_resource,
	.vpu_start_cmd = vpu_start_cmd,
};

const struct vpu_fmt_ops *get_vpu_fmt_ops(void)
{
	return &ops;
}

void *vpu_alloc_context(void)
{
	struct vpu_ctx *ctx = NULL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		vpu_err("Failed to allocate vpu\n");
		return ctx;
	}

	ctx->out_fmt.spec.type = -1;
	ctx->cap_fmt.spec.type = -1;
	ctx->thread_out = NULL;
	ctx->thread_cap = NULL;
	ctx->seq_out = 1;
	ctx->seq_cap = 1;
	ctx->thread_out_interval = 10;
	ctx->thread_cap_interval = 10;
	ctx->rect.left = 0;
	ctx->rect.top = 0;
	ctx->rect.width = 1920;
	ctx->rect.height = 1080;
	ctx->veng_ops = NULL;
	ctx->ve1_ops = NULL;
	ctx->ve2_ops = NULL;
	ctx->is_cap_started = 0;
	ctx->is_out_started = 0;
	ctx->ve1_ops = vpu_ve1_ops;
	ctx->ve2_ops = vpu_ve2_ops;
	ctx->cap_retry_cnt = 0;
	ctx->stop_cmd = false;
	ctx->last_buf_done = false;
	ctx->out_q_cnt = 0;
	ctx->bit_depth = 8;

	mutex_init(&ctx->vpu_mutex);
	spin_lock_init(&ctx->vpu_spin_lock);
	init_waitqueue_head(&ctx->vpu_out_waitq);
	init_waitqueue_head(&ctx->vpu_cap_waitq);
	init_completion(&ctx->bs_parsing_comp);

	// init ctx->out with default value
	memcpy(&ctx->out_fmt, &out_fmt[0], sizeof(struct vpu_fmt));
	// init ctx->cap with default value
	memcpy(&ctx->cap_fmt, &cap_fmt[0], sizeof(struct vpu_fmt));

	return ctx;
}

void vpu_free_context(void *ctx)
{
	struct vpu_ctx *vpu_ctx = ctx;

	// reset other variables of struct videc_ctx
	// init ctx->out with default value
	memcpy(&vpu_ctx->out_fmt, &out_fmt[0], sizeof(struct vpu_fmt));
	// init ctx->cap with default value
	memcpy(&vpu_ctx->cap_fmt, &cap_fmt[0], sizeof(struct vpu_fmt));
	memset(&vpu_ctx->rect, 0, sizeof(struct v4l2_rect));
	vpu_ctx->thread_out = NULL;
	vpu_ctx->thread_cap = NULL;
	vpu_ctx->seq_out = 1;
	vpu_ctx->seq_cap = 1;
	vpu_ctx->memory_out = 0;
	vpu_ctx->memory_cap = 0;
	vpu_ctx->stop_cmd = false;
	vpu_ctx->last_buf_done = false;
	vpu_ctx->cap_retry_cnt = 0;
	vpu_ctx->out_q_cnt = 0;

	kfree(vpu_ctx);
}
/*
 * Register video engine operations.
 */
int vpu_ve_register(int id, struct veng_ops *ops)
{
	if (id == 1)
		vpu_ve1_ops = ops;
	else if (id == 2)
		vpu_ve2_ops = ops;
	else {
		vpu_err("Register invalid video engine VE%d ops\n", id);
		return -EINVAL;
		;
	}

	vpu_info("Registered video engine VE%d ops\n", id);

	return 0;
}
EXPORT_SYMBOL(vpu_ve_register);

/*
 * Unregister video engine operations.
 */
void vpu_ve_unregister(int id)
{
	if (id == 1)
		vpu_ve1_ops = NULL;
	else if (id == 2)
		vpu_ve2_ops = NULL;

	vpu_info("Unregistered video engine VE%d ops\n", id);
}
EXPORT_SYMBOL(vpu_ve_unregister);
