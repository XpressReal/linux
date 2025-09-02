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
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#ifdef VPU_GET_CC
#include <linux/module.h>
#include <linux/netlink.h>
#include <net/netlink.h>
#include <net/net_namespace.h>
#endif
#include <linux/platform_device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include "drv_if.h"
#include "vpu.h"
#include "debug.h"
#include "rtkve1enc_common.h"

unsigned int vpu_debug = 0;
EXPORT_SYMBOL(vpu_debug);
MODULE_PARM_DESC(
	debug,
	"activates debug info, where each bit enables a debug category.\n"
	"\t\tBit 0 (0x01) will enable input messages (output type)\n"
	"\t\tBit 1 (0x02) will enable output messages (capture type)\n");
module_param_named(debug, vpu_debug, int, 0600);

#define VPU_NAME "realtek-vpu"
#define ENABLE_ADAPTIVE_PLAYBACK (0)
#define ENABLE_REORDER_PTS (0)
#define INVERT_BITVAL_1 (~1)

#define RTKDEV_FOURCC_ENC v4l2_fourcc('R', 'E', 'N', 'C')
#define RTKDEV_FOURCC_DEC v4l2_fourcc('R', 'D', 'E', 'C')

ssize_t get_video_status(struct device *dev, struct device_attribute *attr,
			 char *buf);

static DEVICE_ATTR(video_status, S_IRUSR, get_video_status, NULL);

static char hasVideo = 0;

#ifdef ENABLE_SHOW_VIDEO_INFO
ssize_t get_video_info(struct device *dev, struct device_attribute *attr,
		       char *buf);

static DEVICE_ATTR(video_info, S_IRUSR, get_video_info, NULL);
#endif // #ifdef ENABLE_SHOW_VIDEO_INFO

ssize_t set_enhance_mode(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count);

static DEVICE_ATTR(enhance, S_IWUSR, NULL, set_enhance_mode);

static void vpu_dev_release(struct device *dev)
{
}

static struct platform_device videc_pdev = {
	.name = VPU_NAME,
	.dev.release = vpu_dev_release,
};

enum { V4L2_M2M_SRC = 0,
       V4L2_M2M_DST = 1,
};

#ifdef VPU_GET_CC
static struct sock *cc_data_sk = NULL;
__u32 ccReaderPid;
bool bcc_data_channel_bind;
#define NETLINK_CC_DATA 31
#define CC_DATA_LINK 0x12
#define CC_DATA_UNLINK 0x13
#endif

static inline struct videc_ctx *file2ctx(struct file *file)
{
	return container_of(file->private_data, struct videc_ctx, fh);
}

void vpu_printk(const char *level, unsigned int category, const char *format,
		...)
{
	struct va_format vaf;
	va_list args;

	if (category != VPU_DBG_NONE && !(vpu_debug & category))
		return;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	printk("%s"
	       "[VDEC] %s %pV",
	       level, strcmp(level, KERN_ERR) == 0 ? " *ERROR*" : "", &vaf);

	va_end(args);
}
EXPORT_SYMBOL(vpu_printk);

/*
 * mem2mem callbacks
 */

/**
 * job_ready() - check whether an instance is ready to be scheduled to run
 */
static int job_ready(void *priv)
{
	(void)priv;
	vpu_input_dbg("%s\n", __func__);
	return 1;
}

static void job_abort(void *priv)
{
	u32 *rtk_fourcc = (u32 *)priv;
	struct rtkve1enc_ctx *enc_ctx;
	struct videc_ctx *ctx = NULL;
	struct videc_dev *dev = NULL;
	const struct vpu_fmt_ops *op = NULL;
	vpu_input_dbg("%s\n", __func__);

	if (*rtk_fourcc == RTKDEV_FOURCC_ENC) {
		enc_ctx = (struct rtkve1enc_ctx *)priv;
		dev_dbg(enc_ctx->dev->dev, "%d.%s\n",
			__LINE__, __func__);
	}
	else {
		ctx = priv;
		dev = ctx->dev;
		op = get_vpu_fmt_ops();

		if (!op)
			return;
		/* Will cancel the transaction in the next interrupt handler */
		op->vpu_abort(priv, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		op->vpu_abort(priv, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		v4l2_m2m_job_finish(dev->m2m_dev, ctx->fh.m2m_ctx);
	}

	vpu_input_dbg("%s done\n", __func__);
}

/* device_run() - prepares and starts the device
 *
 * This simulates all the immediate preparations required before starting
 * a device. This will be called by the framework when it decides to schedule
 * a particular instance.
 */
static void device_run(void *priv)
{
	u32 *rtk_fourcc = (u32 *)priv;
	struct rtkve1enc_ctx *enc_ctx;

	if (*rtk_fourcc == RTKDEV_FOURCC_ENC) {
		//pr_err("%d.%s.priv:0x%px.encode rtk_fourcc:0x%x\n",
		//	__LINE__, __func__,
		//	priv, *rtk_fourcc);
		enc_ctx = (struct rtkve1enc_ctx *)priv;
		queue_work(enc_ctx->dev->encode_workqueue, &enc_ctx->encode_work);
	}
	else if (*rtk_fourcc == RTKDEV_FOURCC_DEC) {
		//pr_err("%d.%s.priv:0x%px.decode rtk_fourcc:0x%x\n",
		//	__LINE__, __func__,
		//	priv, *rtk_fourcc);
	}
	else {
		pr_err("%d.%s.priv:0x%px.unkown rtk_fourcc:0x%x\n",
			__LINE__, __func__,
			priv, *rtk_fourcc);
	}
}

/*
 * video ioctls
 */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strncpy(cap->driver, VPU_NAME, sizeof(cap->driver) - 1);
	strncpy(cap->card, VPU_NAME, sizeof(cap->card) - 1);
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s", VPU_NAME);
	cap->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int videc_enum_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}

	return op->vpu_enum_fmt_cap(f);
}

static int videc_enum_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}

	return op->vpu_enum_fmt_out(f);
}

static int videc_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct v4l2_fh *fh = file->private_data;
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	struct vb2_queue *vq;

	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return -EINVAL;
	}

	vq = v4l2_m2m_get_vq(fh->m2m_ctx, f->type);
	if (!vq) {
		vpu_err("no vb2 queue for type=%d", f->type);
		return -EINVAL;
	}

	return op->vpu_g_fmt(priv, f);
}

static int videc_try_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct v4l2_fh *fh = file->private_data;
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();

	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}

	return op->vpu_try_fmt_cap(fh, f);
}

static int videc_try_fmt_vid_out(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct v4l2_fh *fh = file->private_data;
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}

	return op->vpu_try_fmt_out(fh, f);
}

static int videc_s_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}

	return op->vpu_s_fmt_cap(priv, f);
}

static int videc_s_fmt_vid_out(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}

	return op->vpu_s_fmt_out(priv, f);
}

static int vpu_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct videc_ctx *ctx =
		container_of(ctrl->handler, struct videc_ctx, ctrl_hdl);

	switch (ctrl->id) {
	case RTK_V4L2_SET_SECURE: {
		ctx->params.is_secure = ctrl->val;
#ifndef ENABLE_TEE_DRM_FLOW
		if (ctx->params.is_secure) {
			ctx->params.is_secure = 0;
			vpu_err("Driver doesn't support secure, force normal buffer\n");
		}
#endif
		break;
	}
	case RTK_V4L2_DEC_PARMS_CONFIG: {
		struct rtk_dec_params *param = ctrl->p_new.p;
		memcpy(&ctx->params.dec_params, param,
		       sizeof(struct rtk_dec_params));

		ctx->params.dec_params.en_adaptive_playback = ENABLE_ADAPTIVE_PLAYBACK;
#ifndef REORDER_PTS
		if (ctx->params.dec_params.en_pts_reorder) {
			ctx->params.dec_params.en_pts_reorder = 0;
			vpu_err("Driver doesn't support enabling pts reorder\n");
		}
#endif
		break;
	}
	case V4L2_CID_COLORIMETRY_HDR10_CLL_INFO: {
		memcpy(&ctx->params.cll, ctrl->p_new.p_hdr10_cll,
		       sizeof(struct v4l2_ctrl_hdr10_cll_info));
		ctx->params_update = true;
		break;
	}
	case V4L2_CID_COLORIMETRY_HDR10_MASTERING_DISPLAY: {
		memcpy(&ctx->params.mastering, ctrl->p_new.p_hdr10_mastering,
		       sizeof(struct v4l2_ctrl_hdr10_mastering_display));
		ctx->params_update = true;
		break;
	}
	default: {
		vpu_err("Invalid control, id=%d, val=%d\n", ctrl->id,
			ctrl->val);
		return -EINVAL;
	}
	}
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,6,0)
static bool vpu_ctrl_type_equal(const struct v4l2_ctrl *ctrl, u32 idx,
				union v4l2_ctrl_ptr ptr1,
				union v4l2_ctrl_ptr ptr2)
{
	idx *= ctrl->elem_size;
	return !memcmp(ptr1.p_const + idx, ptr2.p_const + idx, ctrl->elem_size);
}
#else
static bool vpu_ctrl_type_equal(const struct v4l2_ctrl *ctrl,
				union v4l2_ctrl_ptr ptr1,
				union v4l2_ctrl_ptr ptr2)
{
	return !memcmp(ptr1.p_const, ptr2.p_const, ctrl->elems * ctrl->elem_size);
}
#endif

static void vpu_ctrl_type_init(const struct v4l2_ctrl *ctrl, u32 idx,
			       union v4l2_ctrl_ptr ptr)
{
	void *p = ptr.p + idx * ctrl->elem_size;

	if (ctrl->p_def.p_const)
		memcpy(p, ctrl->p_def.p_const, ctrl->elem_size);
	else
		memset(p, 0, ctrl->elem_size);
}

static void vpu_ctrl_type_log(const struct v4l2_ctrl *ctrl)
{
	if (ctrl->is_array) {
		unsigned i;

		for (i = 0; i < ctrl->nr_of_dims; i++)
			pr_cont("[%u]", ctrl->dims[i]);
		pr_cont(" ");
	}

	pr_cont("RTK DEC PARAMS");
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,6,0)
static int vpu_ctrl_type_validate(const struct v4l2_ctrl *ctrl, u32 idx,
#else
static int vpu_ctrl_type_validate(const struct v4l2_ctrl *ctrl,
#endif
				  union v4l2_ctrl_ptr ptr)
{
	return 0;
}

static int vpu_g_v_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpu_fmt vpu_fmt;
	int ret = 0;
	struct videc_ctx *vid_ctx =
		container_of(ctrl->handler, struct videc_ctx, ctrl_hdl);
	struct v4l2_fh *fh = &vid_ctx->fh;

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		vpu_get_cap_fmt(fh, (void *)&vpu_fmt);
		ctrl->val = vpu_fmt.misc.bufcnt;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops vpu_ctrl_ops = {
	.s_ctrl = vpu_s_ctrl,
	.g_volatile_ctrl = vpu_g_v_ctrl,
};

static const struct v4l2_ctrl_type_ops vpu_type_ops = {
	.equal = vpu_ctrl_type_equal,
	.init = vpu_ctrl_type_init,
	.log = vpu_ctrl_type_log,
	.validate = vpu_ctrl_type_validate,
};

static const struct v4l2_ctrl_config rtk_ctrl_set_secure = {
	.ops = &vpu_ctrl_ops,
	.id = RTK_V4L2_SET_SECURE,
	.name = "Enable Secure Buffer(SVP)",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.def = 0,
	.min = 0,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config rtk_ctrl_dec_params = {
	.ops = &vpu_ctrl_ops,
	.type_ops = &vpu_type_ops,
	.id = RTK_V4L2_DEC_PARMS_CONFIG,
	.name = "Decoder parameters",
	.type = V4L2_CTRL_TYPE_RTK_DEC_PARAM,
	.def = 0,
	.min = 0,
	.max = UINT_MAX,
	.step = 1,
	.elem_size = sizeof(struct rtk_dec_params),
};

int vpu_ctrls_setup(struct videc_ctx *ctx)
{
	struct v4l2_ctrl *ctrl;
	struct v4l2_ctrl_hdr10_cll_info p_hdr10_cll = { 1000, 400 };
	struct v4l2_ctrl_hdr10_mastering_display p_hdr10_mastering = {
		{ 34000, 13250, 7500 },
		{ 16000, 34500, 3000 },
		15635,
		16450,
		10000000,
		500,
	};

	v4l2_ctrl_handler_init(&ctx->ctrl_hdl, 5);

	ctrl = v4l2_ctrl_new_std(&ctx->ctrl_hdl, &vpu_ctrl_ops,
				 V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1, 1);
	ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_new_std_compound(&ctx->ctrl_hdl, &vpu_ctrl_ops,
				   V4L2_CID_COLORIMETRY_HDR10_CLL_INFO,
				   v4l2_ctrl_ptr_create(&p_hdr10_cll));

	v4l2_ctrl_new_std_compound(&ctx->ctrl_hdl, &vpu_ctrl_ops,
				   V4L2_CID_COLORIMETRY_HDR10_MASTERING_DISPLAY,
				   v4l2_ctrl_ptr_create(&p_hdr10_mastering));

	v4l2_ctrl_new_custom(&ctx->ctrl_hdl, &rtk_ctrl_set_secure, NULL);
	v4l2_ctrl_new_custom(&ctx->ctrl_hdl, &rtk_ctrl_dec_params, NULL);

	if (ctx->ctrl_hdl.error) {
		vpu_err("control initialization error (%d)",
			ctx->ctrl_hdl.error);
		v4l2_ctrl_handler_free(&ctx->ctrl_hdl);
		return -EINVAL;
	}

	return v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);
}

static int videc_reqbufs(struct file *file, void *priv,
				struct v4l2_requestbuffers *rb)
{
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	struct v4l2_fh *fh = file->private_data;
	struct videc_ctx *ctx = file2ctx(file);
	int ret = 0;

	ret = v4l2_m2m_ioctl_reqbufs(file, priv, rb);
	if (ret)
		return ret;

	vpu_info("%s reqbuf %d", v4l2_type_names[rb->type], rb->count);

	if (V4L2_TYPE_IS_OUTPUT(rb->type))
		ctx->reqbuf_out = rb->count;
	else
		ctx->reqbuf_cap = rb->count;

	if (ctx->has_stream_on &&
		ctx->reqbuf_out == 0 && ctx->reqbuf_cap == 0) {
		op->vpu_reset_resource(fh);
		ctx->ve_ctx = NULL;
		ctx->reqbuf_out = -1;
		ctx->reqbuf_cap = -1;
	} else if (ctx->reqbuf_cap == 0) {
		if (op->vpu_free_capture)
			op->vpu_free_capture(fh);
	}

	return ret;
}

static int videc_querybuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	int ret;

	ret = v4l2_m2m_ioctl_querybuf(file, priv, buf);
	return ret;
}

static int videc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct v4l2_fh *fh = file->private_data;

	if (V4L2_TYPE_IS_OUTPUT(buf->type) &&
	    buf->memory == V4L2_MEMORY_DMABUF) {
		struct vb2_queue *vq;
		struct v4l2_fh *fh = file->private_data;
		struct vb2_buffer *vb;

		vq = v4l2_m2m_get_vq(fh->m2m_ctx, buf->type);
		vb = vq->bufs[buf->index];
		vb->planes[0].length = PAGE_ALIGN(buf->length);
		buf->length = 0;
	}

	return v4l2_m2m_qbuf(file, fh->m2m_ctx, buf);
}

static int videc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct v4l2_fh *fh = file->private_data;

	struct videc_ctx *vid_ctx = file2ctx(file);
	struct vpu_ctx *vpu_ctx = vid_ctx->vpu_ctx;
	if(vpu_ctx->is_decoder_error){
		vpu_err("Call on DQBUF after decoder error");
		return -EIO;
	}

	return v4l2_m2m_dqbuf(file, fh->m2m_ctx, buf);
}

static int videc_create_bufs(struct file *file, void *priv,
			     struct v4l2_create_buffers *create)
{
	int ret;

	ret = v4l2_m2m_ioctl_create_bufs(file, priv, create);
	return ret;
}
int videc_prepare_buf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	int ret;

	ret = v4l2_m2m_ioctl_prepare_buf(file, priv, buf);
	return ret;
}

int videc_expbuf(struct file *file, void *priv, struct v4l2_exportbuffer *eb)
{
	int ret;

	ret = v4l2_m2m_ioctl_expbuf(file, priv, eb);

	return ret;
}

static int videc_g_selection(struct file *file, void *fh,
			     struct v4l2_selection *sel)
{
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	struct v4l2_rect rsel;
	int ret;
	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}


	ret = op->vpu_g_crop(fh, &rsel);

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		//todo
		/* fallthrough */
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		//todo
		break;
	default:
		return -EINVAL;
	}

	memcpy(&sel->r, &rsel, sizeof(struct v4l2_rect));

	return ret;
}

static int videc_frmsizeenum(struct file *file, void *priv,
			     struct v4l2_frmsizeenum *fsize)
{
	int ret = 0;
	struct v4l2_frmsize_stepwise *frmsize = NULL;

	if (fsize->index != 0)
		return -EINVAL;

	frmsize = (struct v4l2_frmsize_stepwise *)vpu_get_frmsize(
		fsize->pixel_format);
	if (frmsize) {
		memcpy(&fsize->stepwise, frmsize,
		       sizeof(struct v4l2_frmsize_stepwise));
	} else {
		ret = -ENOTTY;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	return ret;
}

int videc_subscribe_event(struct v4l2_fh *fh,
			  const struct v4l2_event_subscription *sub)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);

	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		vid_ctx->is_sub_res_chg = true;
		return v4l2_src_change_event_subscribe(fh, sub);
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static int videc_try_decoder_cmd(struct file *file, void *fh,
				 struct v4l2_decoder_cmd *dc)
{
	return v4l2_m2m_ioctl_try_decoder_cmd(file, fh, dc);
}

static int videc_decoder_cmd(struct file *file, void *fh,
			     struct v4l2_decoder_cmd *dc)
{
	struct videc_ctx *vid_ctx = file2ctx(file);
	struct vpu_ctx *vpu_ctx = vid_ctx->vpu_ctx;
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	int ret = 0;

	ret = videc_try_decoder_cmd(file, fh, dc);
	if (ret < 0)
		return ret;

	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}

	vpu_info("decoder cmd=%u", dc->cmd);

	if (vpu_ctx->stop_cmd) {
		// the drain sequence is initiated
		if (!vpu_ctx->last_buf_done) {
			// the drain sequence is in progress and is not completed
			if ((dc->cmd == V4L2_DEC_CMD_STOP) || (dc->cmd == V4L2_DEC_CMD_START)) {
				vpu_err("return -EBUSY when received cmd:%d while the drain sequence is in progress\n",dc->cmd);
				return -EBUSY;
			}
		}
	}

	switch (dc->cmd) {
	case V4L2_DEC_CMD_STOP:
		/* Defer vpu stop */
		vpu_ctx->stop_cmd = true;
		break;
	case V4L2_DEC_CMD_START:
		vpu_ctx->stop_cmd = false;
		vpu_ctx->last_buf_done = false;
		ret = op->vpu_start_cmd(fh);
		break;
	default:
		break;
	}

	return ret;
}

static const struct v4l2_ioctl_ops vpu_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = videc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap_mplane = videc_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane	= videc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap_mplane	= videc_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out = videc_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out_mplane = videc_g_fmt,
	.vidioc_try_fmt_vid_out_mplane	= videc_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out_mplane = videc_s_fmt_vid_out,

	.vidioc_reqbufs = videc_reqbufs,
	.vidioc_querybuf = videc_querybuf,
	.vidioc_qbuf = videc_qbuf,
	.vidioc_dqbuf = videc_dqbuf,
	.vidioc_prepare_buf = videc_prepare_buf,
	.vidioc_create_bufs = videc_create_bufs,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_g_selection = videc_g_selection,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,

	.vidioc_enum_framesizes = videc_frmsizeenum,
	.vidioc_subscribe_event = videc_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,

	.vidioc_try_decoder_cmd = videc_try_decoder_cmd,
	.vidioc_decoder_cmd = videc_decoder_cmd,
};

/*
 * Queue operations
 */

static int videc_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			     unsigned int *nplanes, unsigned int sizes[],
			     struct device *alloc_devs[])
{
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	struct videc_ctx *ctx = vb2_get_drv_priv(vq);
	vpu_input_dbg("%s\n", __func__);
	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}

	alloc_devs[0] = ctx->dev->v4l2_dev.dev;
	return op->vpu_queue_info(vq, nbuffers, nplanes, &sizes[0]);
}

static int videc_buf_prepare(struct vb2_buffer *vb)
{
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	int sizeimages, ret;
	unsigned int nplanes = 0;

	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}

	ret = op->vpu_queue_info(vb->vb2_queue, NULL, &nplanes, &sizeimages);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = V4L2_FIELD_NONE;
		if (vbuf->field != V4L2_FIELD_NONE) {
			vpu_err("%s field isn't supported\n", __func__);
			return -EINVAL;
		}
	}

	if (vb2_plane_size(vb, 0) < sizeimages) {
		vpu_err("%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0), (long)sizeimages);
		return -EINVAL;
	}

	if (!V4L2_TYPE_IS_OUTPUT(vb->type)) {
		vb2_set_plane_payload(vb, 0, sizeimages);
#ifdef PREPEND_METADATA
		vb->planes[0].data_offset = METADATA_OFFSET;
#endif
	}

	return 0;
}

static void videc_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct videc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return;
	}
	op->vpu_qbuf(&ctx->fh, vb);
}

static int videc_start_streaming(struct vb2_queue *q, unsigned count)
{
	struct videc_ctx *ctx = vb2_get_drv_priv(q);
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	int ret;
	vpu_info("%s %s\n", __func__, V4L2_TYPE_TO_STR(q->type));

	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return 0;
	}
	ret = op->vpu_start_streaming(q, count);
	if (ret) {
		vpu_err("vpu_start_streaming fail ret %d\n", ret);
		return ret;
	}

	ctx->has_stream_on = true;
	return 0;
}

static void videc_stop_streaming(struct vb2_queue *q)
{
	struct videc_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vbuf;
	unsigned long flags;
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();
	int ret;
	vpu_info("%s %s\n", __func__, V4L2_TYPE_TO_STR(q->type));

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		for (;;) {
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
			if (vbuf == NULL)
				break;
			spin_lock_irqsave(&ctx->dev->irqlock, flags);
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
			spin_unlock_irqrestore(&ctx->dev->irqlock, flags);
		}
	}

	if (!op) {
		vpu_err("vpu ops is NULL\n");
		return;
	}

	ret = op->vpu_stop_streaming(q);
	if (ret) {
		vpu_err("vpu_stop_streaming fail ret %d\n", ret);
	}
	return;
}

#ifdef VPU_GET_CC
bool cc_isCCReaderReady(void)
{
	return bcc_data_channel_bind;
}
EXPORT_SYMBOL(cc_isCCReaderReady);

__u32 cc_getCCReaderPid(void)
{
	return ccReaderPid;
}
EXPORT_SYMBOL(cc_getCCReaderPid);

bool cc_isCCInit(void)
{
	return (cc_data_sk == NULL);
}
EXPORT_SYMBOL(cc_isCCInit);

void cc_data_channel_send(char *message, int total_size, int pid)
{
	struct sk_buff *out_skb;
	struct nlmsghdr *out_nlh;
	int ret;
	//vpu_err(" cc_data_channel_send start!\n");

	if (pid == 0)
		goto failure;

	out_skb = nlmsg_new(total_size, GFP_KERNEL);
	if (!out_skb)
		goto failure;

	out_nlh = nlmsg_put(out_skb, 0, 0, CC_DATA_LINK, total_size, 0);
	if (!out_nlh) {
		nlmsg_free(out_skb);
		goto failure;
	}

	memcpy(nlmsg_data(out_nlh), message, total_size);

	ret = nlmsg_unicast(cc_data_sk, out_skb, pid);

	if (ret < 0) {
		bcc_data_channel_bind = false;
		return;
	}

	return;
failure:
	vpu_err(" failed in cc_data_channel_send!\n");
}
EXPORT_SYMBOL(cc_data_channel_send);

static int cc_data_channel_bind(struct net *net, int group)
{
	bcc_data_channel_bind = true;

	return 0;
}

static void cc_data_channel_unbind(struct net *net, int group)
{
	bcc_data_channel_bind = false;
}

static void cc_data_channel_receive(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	void *payload; //message from user space
	int payload_len; // with padding, but ok for echo

	nlh = nlmsg_hdr(skb);

	switch (nlh->nlmsg_type) {
	case CC_DATA_LINK:
		payload = nlmsg_data(nlh);
		payload_len = nlmsg_len(nlh);
		ccReaderPid = nlh->nlmsg_pid;
		bcc_data_channel_bind = true;
		break;
	case CC_DATA_UNLINK:
		payload = nlmsg_data(nlh);
		payload_len = nlmsg_len(nlh);
		ccReaderPid = 0;
		bcc_data_channel_bind = false;
		cc_data_channel_send((char *)payload, payload_len,
				     nlh->nlmsg_pid); // for unlock
		break;
	default:
		vpu_err("Unknow msgtype recieved!\n");
	}
	return;
}

int cc_data_channel_init(void)
{
	struct netlink_kernel_cfg nlcfg = {
		.groups = 1,
		.input = cc_data_channel_receive,
		.bind = cc_data_channel_bind,
		.unbind = cc_data_channel_unbind,
	};

	bcc_data_channel_bind = false;
	cc_data_sk = netlink_kernel_create(&init_net, NETLINK_CC_DATA, &nlcfg);

	if (cc_data_sk == NULL) {
		vpu_err("cc_data_channel_init netlink create error!\n");
	} else {
		vpu_info("cc_data_channel_init initialed ok!\n");
	}

	return 0;
}
EXPORT_SYMBOL(cc_data_channel_init);

void cc_data_channel_exit(void)
{
	if (cc_data_sk == NULL)
		return;

	vpu_info("cc_data_channel_exit...\n");

	netlink_kernel_release(cc_data_sk);

	cc_data_sk = NULL;
	bcc_data_channel_bind = false;
}
EXPORT_SYMBOL(cc_data_channel_exit);

#endif

static const struct vb2_ops vpu_qops = {
	.queue_setup = videc_queue_setup,
	.buf_prepare = videc_buf_prepare,
	.buf_queue = videc_buf_queue,
	.start_streaming = videc_start_streaming,
	.stop_streaming = videc_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct videc_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &vpu_qops;
	src_vq->mem_ops = &vb2_vmalloc_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->dev->dev_mutex;
	src_vq->dev = ctx->dev->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &vpu_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->dev->dev_mutex;
	dst_vq->dev = ctx->dev->dev;

	return vb2_queue_init(dst_vq);
}

/*
 * File operations
 */
static int vpu_open(struct file *file)
{
	struct videc_dev *dev = video_drvdata(file);
	struct videc_ctx *ctx = NULL;
	int rc = 0;

	if (mutex_lock_interruptible(&dev->dev_mutex))
		return -ERESTARTSYS;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto open_unlock;
	}

	ctx->rtkdev_fourcc = RTKDEV_FOURCC_DEC;
	ctx->vpu_ctx = vpu_alloc_context();
	if (!ctx->vpu_ctx) {
		rc = -ENOMEM;
		kfree(ctx);
		goto open_unlock;
	}
	ctx->reqbuf_out = -1;
	ctx->reqbuf_cap = -1;

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	ctx->dev = dev;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx, &queue_init);

	if (IS_ERR(ctx->fh.m2m_ctx)) {
		rc = PTR_ERR(ctx->fh.m2m_ctx);

		vpu_free_context(ctx->vpu_ctx);
		kfree(ctx);
		goto open_unlock;
	}

	ctx->file = file;

	if (vpu_ctrls_setup(ctx)) {
		v4l2_err(&dev->v4l2_dev,
			 "failed to setup realtek vpu controls\n");
		goto open_unlock;
	}

	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;

	v4l2_fh_add(&ctx->fh);

	memset(&ctx->params, 0, sizeof(struct videc_params));
	ctx->params.dec_params.en_adaptive_playback = ENABLE_ADAPTIVE_PLAYBACK;
	ctx->params.dec_params.en_pts_reorder = ENABLE_REORDER_PTS;
	ctx->params.dec_params.en_enhance = dev->en_enhance;

	atomic_inc(&dev->num_inst);
	hasVideo = 1;

	vpu_info("Created instance: %px, m2m_ctx: %px\n", ctx, ctx->fh.m2m_ctx);

open_unlock:
	mutex_unlock(&dev->dev_mutex);
	return rc;
}

static int vpu_release(struct file *file)
{
	struct videc_dev *dev = video_drvdata(file);
	struct videc_ctx *ctx = file2ctx(file);
	const struct vpu_fmt_ops *op = get_vpu_fmt_ops();

	vpu_info("Releasing instance %px\n", ctx);

	mutex_lock(&dev->dev_mutex);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	op->vpu_reset_resource(&ctx->fh);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);

	if (ctx->vpu_ctx)
		vpu_free_context(ctx->vpu_ctx);

	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);

	kfree(ctx);
	mutex_unlock(&dev->dev_mutex);
	atomic_dec(&dev->num_inst);
	hasVideo = 0;

	vpu_info("Releasing done\n");
	return 0;
}

static const struct v4l2_file_operations vpu_fops = {
	.owner = THIS_MODULE,
	.open = vpu_open,
	.release = vpu_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static struct video_device vpu_videodev = {
	.name = VPU_NAME,
	.vfl_dir = VFL_DIR_M2M,
	.fops = &vpu_fops,
	.ioctl_ops = &vpu_ioctl_ops,
	.minor = -1,
	.release = video_device_release_empty,
};

static void rtkve1_encode_work(struct work_struct *work)
{
	struct rtkve1enc_ctx *ctx = container_of(work, struct rtkve1enc_ctx, encode_work);
	int ret;
	enum rtkve1_enc_state new_state = 0;

	/* check and decide state */
	ret = ctx->ops->decide_state(ctx, &new_state);
	if (ret < 0) {
		goto exit;
	}

	switch (new_state) {
		case RTK_VE1_STATE_ENC_SEQ_INIT:
			ctx->ops->seq_init(ctx);
			break;
		case RTK_VE1_STATE_ENC_REG_FBS:
			ret = ctx->ops->reg_fbs(ctx);
			break;
		case RTK_VE1_STATE_ENC_HEADER:
			ret = ctx->ops->enc_header(ctx);
			break;
		case RTK_VE1_STATE_ENC_PIC:
			ret = ctx->ops->enc_pic(ctx);
			break;
		case RTK_VE1_STATE_ENC_SEQ_END:
			ctx->ops->seq_end(ctx);
			break;
		case RTK_VE1_STATE_ENC_TIMEOUT:
			//ret = ctx->ops->run_timeout(ctx);
			break;
		default:
			break;
	}

exit:
	v4l2_m2m_job_finish(ctx->dev->m2m_dev, ctx->v4l2_fh.m2m_ctx);
}

static int rtkve1enc_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct videc_dev *dev = video_drvdata(file);
	struct rtkve1enc_ctx *ctx = NULL;
	int ret = 0;

	dev_dbg(dev->dev, "%d.%s.[+] video_device:0x%px.videc_dev:0x%px\n", __LINE__, __func__,
		vdev, dev);

	if (mutex_lock_interruptible(&dev->dev_mutex)) {
		dev_err(dev->dev, "%d.%s.mutex_lock_interruptible() fail\n", __LINE__, __func__);
		return -ERESTARTSYS;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		dev_err(dev->dev, "%d.%s.kzalloc ctx fail\n", __LINE__, __func__);
		goto open_unlock;
	}

	ctx->rtkdev_fourcc = RTKDEV_FOURCC_ENC;
	ctx->dev = dev;
	dev_dbg(dev->dev, "%d.%s.ctx:0x%px\n",
		__LINE__, __func__,
		ctx);

	v4l2_fh_init(&ctx->v4l2_fh, vdev);
	file->private_data = &ctx->v4l2_fh;
	v4l2_fh_add(&ctx->v4l2_fh);

	ctx->ops = &rtkve1enc_ops;
	/* queue_init */
	ctx->v4l2_fh.m2m_ctx =
		v4l2_m2m_ctx_init(dev->m2m_dev, ctx, ctx->ops->queue_init);
	if (IS_ERR(ctx->v4l2_fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->v4l2_fh.m2m_ctx);
		dev_err(dev->dev, "%d.%s.v4l2_m2m_ctx_init() fail\n", __LINE__, __func__);
		goto free_ctx;
	}
	dev_dbg(dev->dev, "%d.%s.m2m_ctx:0x%px\n", __LINE__, __func__,
		ctx->v4l2_fh.m2m_ctx);

	/* ctrls_setup */
	if (ctx->ops->ctrls_setup(ctx)) {
		ret = -ENODEV;
		dev_err(dev->dev, "%d.%s.ctrls_setup() fail\n", __LINE__, __func__);
		goto err_m2m_release;
	}

	rtkve_set_default_format(ctx, &ctx->src_fmt, &ctx->dst_fmt);
	ctx->colorspace = V4L2_COLORSPACE_DEFAULT;
	ctx->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->quantization = V4L2_QUANTIZATION_DEFAULT;
	ctx->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	INIT_WORK(&ctx->encode_work, rtkve1_encode_work);
	ctx->debugfs_entry = debugfs_create_dir(RTKVE1_ENC_DEV_NAME, dev->debugfs_root);

	//rtkve1_initialize(ctx);

	mutex_unlock(&dev->dev_mutex);
	dev_dbg(dev->dev, "%d.%s.[-] ret:%d\n",
		__LINE__, __func__, ret);
	return ret;

err_m2m_release:
	v4l2_m2m_ctx_release(ctx->v4l2_fh.m2m_ctx);
free_ctx:
	v4l2_fh_del(&ctx->v4l2_fh);
	v4l2_fh_exit(&ctx->v4l2_fh);
	kfree(ctx);
open_unlock:
	mutex_unlock(&dev->dev_mutex);
	return ret;
}

static int rtkve1enc_release(struct file *file)
{
	struct videc_dev *dev = video_drvdata(file);
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(file->private_data);

	dev_dbg(dev->dev, "%d.%s.[+] ctx:%px\n", __LINE__, __func__,
		ctx);
	mutex_lock(&dev->dev_mutex);
	v4l2_m2m_ctx_release(ctx->v4l2_fh.m2m_ctx);
	// release/reset resource
	v4l2_ctrl_handler_free(&ctx->v4l2_ctrl_hdl);
	v4l2_fh_del(&ctx->v4l2_fh);
	v4l2_fh_exit(&ctx->v4l2_fh);
	debugfs_remove_recursive(ctx->debugfs_entry);
	kfree(ctx);
	ctx = NULL;
	mutex_unlock(&dev->dev_mutex);
	dev_dbg(dev->dev, "%d.%s.[-]\n", __LINE__, __func__);

	return 0;
}

static const struct v4l2_file_operations rtkve1enc_fops = {
	.owner = THIS_MODULE,
	.open = rtkve1enc_open,
	.release = rtkve1enc_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static struct video_device rtkve1enc_videodev = {
	.name = RTKVE1_ENC_DEV_NAME,
	.vfl_dir = VFL_DIR_M2M,
	.fops = &rtkve1enc_fops,
	.ioctl_ops = &rtkve1enc_ioctl_ops,
	.minor = -1,
	.release = video_device_release_empty,
};

static struct v4l2_m2m_ops m2m_ops = {
	.device_run = device_run,
	.job_ready = job_ready,
	.job_abort = job_abort,
};

ssize_t get_video_status(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	return sprintf(buf, "%d\n", hasVideo);
}
#ifdef ENABLE_SHOW_VIDEO_INFO
ssize_t get_video_info(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return vpu_get_video_info(hasVideo, buf);
}
#endif // #ifdef ENABLE_SHOW_VIDEO_INFO

ssize_t set_enhance_mode(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct videc_dev *v_dev = dev_get_drvdata(dev);
	unsigned long mode;
	int ret = 0;

	ret = kstrtol(buf, 0, &mode);
	if (ret != 0 || mode & INVERT_BITVAL_1) {
		pr_err("Incorrect input value. Please input 1 or 0.");
		return -EINVAL;
	}

	v_dev->en_enhance = mode;

	return count;
}

static int vpu_probe(struct platform_device *pdev)
{
	struct videc_dev *dev;
	struct video_device *video_dev;
	int ret;

	/* Allocate a new instance */
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->ve1_devinfo = kzalloc(sizeof(struct rtkve1_dev_info), GFP_KERNEL);
	if (!dev->ve1_devinfo) {
		vpu_err("%d.%s.kzalloc struct rtkve1_dev_info fail\n",
			__LINE__, __func__);
		return -ENOMEM;
	}

	spin_lock_init(&dev->irqlock);

	/* Initialize the top-level structure */
	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		return ret;

	dev->dev = &pdev->dev;
	atomic_set(&dev->num_inst, 0);
	mutex_init(&dev->dev_mutex);
	mutex_init(&dev->ve1_hw_mutex);
	vpu_info("%d.%s.ve1_hw_mutex:0x%px.ve1_instance_nums:0x%px\n",
		__LINE__, __func__,
		(void *)&dev->ve1_hw_mutex,
		(void *)&dev->ve1_instance_nums);

	/* Initialize the video_device structure */
	dev->video_dev[0] = vpu_videodev;
	video_dev = &dev->video_dev[0];
	video_dev->lock = &dev->dev_mutex;
	video_dev->v4l2_dev = &dev->v4l2_dev;
	video_dev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	/* Register video4linux device */
	ret = video_register_device(video_dev, VFL_TYPE_VIDEO, 0);
	if (ret) {
		vpu_err("Failed to register video device\n");
		goto unreg_dev;
	}

	/* Set private data */
	video_set_drvdata(video_dev, dev);
	snprintf(video_dev->name, sizeof(video_dev->name), "%s",
		 vpu_videodev.name);
	vpu_info("Device registered as /dev/video%d\n", video_dev->num);

	/* register rtkve1-enc video_device */
	dev->video_dev[1] = rtkve1enc_videodev;
	video_dev = &dev->video_dev[1];
	video_dev->lock = &dev->dev_mutex;
	video_dev->v4l2_dev = &dev->v4l2_dev;
	video_dev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	ret = video_register_device(video_dev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		vpu_err("Failed to register video device\n");
		goto reg_dev0;
	}
	video_set_drvdata(video_dev, dev);
	vpu_info("%d.%s.rtkve1 encoder registered as /dev/video%d.name=%s\n",
		__LINE__, __func__,
		video_dev->num, video_dev->name);

	platform_set_drvdata(pdev, dev);

	/* Initialize per-driver m2m data */
	dev->m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		vpu_err("Failed to init mem2mem device\n");
		ret = PTR_ERR(dev->m2m_dev);
		goto err_m2m;
	}

	dev->encode_workqueue = alloc_ordered_workqueue(
		RTKVE1_ENC_DEV_NAME, WQ_MEM_RECLAIM | WQ_FREEZABLE);
	if (!dev->encode_workqueue) {
		vpu_err("Failed to create encode workqueue\n");
		ret = -EINVAL;
		goto err_m2m;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_video_status);
	if (ret < 0)
		vpu_err("failed to create v4l2 video attribute\n");

#ifdef ENABLE_SHOW_VIDEO_INFO
	ret = device_create_file(&pdev->dev, &dev_attr_video_info);
	if (ret < 0)
		vpu_err("failed to create v4l2 video information!\n");
#endif // #ifdef ENABLE_SHOW_VIDEO_INFO

	ret = device_create_file(&pdev->dev, &dev_attr_enhance);
	if (ret < 0)
		vpu_err("failed to create v4l2 enhance attribute\n");

	dev->debugfs_root = debugfs_create_dir("rtkvdec-dbg", NULL);

	return 0;

err_m2m:
	v4l2_m2m_release(dev->m2m_dev);
	video_unregister_device(&dev->video_dev[0]);
	video_unregister_device(&dev->video_dev[1]);
reg_dev0:
	video_unregister_device(&dev->video_dev[0]);
unreg_dev:
	v4l2_device_unregister(&dev->v4l2_dev);

	return ret;
}

static int vpu_remove(struct platform_device *pdev)
{
	struct videc_dev *dev = platform_get_drvdata(pdev);

	vpu_info("Removing %s\n", VPU_NAME);

	if (dev->encode_workqueue)
		destroy_workqueue(dev->encode_workqueue);
#if 0
	if (ve1_devinfo->codebuf.size != 0) {
		dev_dbg(dev->dev, "%d.%s.free_dma.name:codebuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			ve1_devinfo->codebuf.size,
			ve1_devinfo->codebuf.paddr,
			ve1_devinfo->codebuf.vaddr);
		rtkve1_free_dma_memory(dev, &ve1_devinfo->codebuf, "codebuf");
	}
	if (ve1_devinfo->parabuf.size != 0) {
		dev_dbg(dev->dev, "%d.%s.free_dma.name:parabuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			ve1_devinfo->parabuf.size,
			ve1_devinfo->parabuf.paddr,
			ve1_devinfo->parabuf.vaddr);
		rtkve1_free_dma_memory(dev, &ve1_devinfo->parabuf, "parabuf");
	}
	if (ve1_devinfo->tempbuf.size != 0) {
		dev_dbg(dev->dev, "%d.%s.free_dma.name:tempbuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			ve1_devinfo->tempbuf.size,
			ve1_devinfo->tempbuf.paddr,
			ve1_devinfo->tempbuf.vaddr);
		rtkve1_free_dma_memory(dev, &ve1_devinfo->tempbuf, "tempbuf");
	}
#endif
	kfree(dev->ve1_devinfo);
	dev->ve1_devinfo = NULL;

	device_remove_file(&pdev->dev, &dev_attr_video_status);
#ifdef ENABLE_SHOW_VIDEO_INFO
	device_remove_file(&pdev->dev, &dev_attr_video_info);
#endif // #ifdef ENABLE_SHOW_VIDEO_INFO
	v4l2_m2m_release(dev->m2m_dev);
	video_unregister_device(&dev->video_dev[0]);
	video_unregister_device(&dev->video_dev[1]);
	v4l2_device_unregister(&dev->v4l2_dev);
	debugfs_remove_recursive(dev->debugfs_root);

	return 0;
}

static struct platform_driver vpu_pdrv = {
	.probe		= vpu_probe,
	.remove		= vpu_remove,
	.driver		= {
		.name	= VPU_NAME,
	},
};

static void __exit vpu_exit(void)
{
	platform_driver_unregister(&vpu_pdrv);
	platform_device_unregister(&videc_pdev);
}

static int __init vpu_init(void)
{
	int ret;

	ret = platform_device_register(&videc_pdev);
	if (ret)
		return ret;

	ret = platform_driver_register(&vpu_pdrv);
	if (ret)
		platform_device_unregister(&videc_pdev);

	return ret;
}

module_init(vpu_init);
module_exit(vpu_exit);

MODULE_VERSION(xstr(GIT_VERSION));
MODULE_LICENSE("GPL");
MODULE_AUTHOR("William Lee <william.lee@realtek.com>");
MODULE_DESCRIPTION("Realtek V4L2 VE2 Codec Driver");
