// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Realtek video encoder v4l2 driver
 *
 * Copyright (c) 2024 Realtek Semiconductor Corp. All rights reserved.
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-Proprietary
 *
 * This software component is confidential and proprietary to Realtek
 * Semiconductor Corp. Disclosure, reproduction, redistribution, in whole
 * or in part, of this work and its derivatives without express permission
 * is prohibited.
 */
#include "rtkve-vpu.h"

void rtkve_update_pix_fmt(struct vpu_instance *inst,
			  struct v4l2_pix_format_mplane *pix_mp,
			  unsigned int width, unsigned int height)
{
	pix_mp->flags = 0;
	pix_mp->field = V4L2_FIELD_NONE;
	memset(pix_mp->reserved, 0, sizeof(pix_mp->reserved));

	switch (pix_mp->pixelformat) {
	case V4L2_PIX_FMT_NV12:
		pix_mp->width = width;
		pix_mp->height = height;

		pix_mp->plane_fmt[0].bytesperline = pix_mp->width;
		pix_mp->plane_fmt[0].sizeimage =
			pix_mp->width * pix_mp->height * 3 / 2;
		break;
	default:
		pix_mp->width = width;
		pix_mp->height = height;
		pix_mp->plane_fmt[0].bytesperline = 0;
		if (!pix_mp->plane_fmt[0].sizeimage)
			pix_mp->plane_fmt[0].sizeimage = width * height;
		break;
	}
}

void rtkve_set_default_format(struct vpu_instance *inst,
			      struct v4l2_pix_format_mplane *src_fmt,
			      struct v4l2_pix_format_mplane *dst_fmt)
{
	const struct rtkve_match_data *enc_pdata = inst->dev->rtkve_mdata;
	const struct vpu_format *vpu_fmt;

	vpu_fmt = enc_pdata->find_vpu_fmt_by_idx(0, VPU_FMT_TYPE_RAW);

	src_fmt->pixelformat = vpu_fmt->v4l2_pix_fmt;
	src_fmt->num_planes = vpu_fmt->num_planes;
	rtkve_update_pix_fmt(inst, src_fmt, 1920, 1080);

	vpu_fmt = enc_pdata->find_vpu_fmt_by_idx(0, VPU_FMT_TYPE_CODEC);

	dst_fmt->pixelformat = vpu_fmt->v4l2_pix_fmt;
	dst_fmt->num_planes = vpu_fmt->num_planes;
	rtkve_update_pix_fmt(inst, dst_fmt, 1920, 1080);
}

static int rtkve_enc_querycap(struct file *file, void *fh,
			      struct v4l2_capability *cap)
{
	strscpy(cap->driver, VPU_ENC_DRV_NAME, sizeof(cap->driver));
	strscpy(cap->card, VPU_ENC_DRV_NAME, sizeof(cap->card));
	strscpy(cap->bus_info, "platform:" VPU_ENC_DRV_NAME,
		sizeof(cap->bus_info));

	return 0;
}

static int rtkve_enc_enum_framesizes(struct file *f, void *fh,
				     struct v4l2_frmsizeenum *fsize)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	const struct rtkve_match_data *dec_pdata = inst->dev->rtkve_mdata;
	const struct vpu_format *vpu_fmt;
	int ret = 0;

	if (fsize->index) {
		ret = -EINVAL;
		goto exit;
	}

	vpu_fmt = dec_pdata->find_vpu_fmt(fsize->pixel_format,
					  VPU_FMT_TYPE_CODEC);
	if (!vpu_fmt) {
		vpu_fmt = dec_pdata->find_vpu_fmt(fsize->pixel_format,
						  VPU_FMT_TYPE_RAW);
		if (!vpu_fmt) {
			ret = -EINVAL;
			goto exit;
		}
	}

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = vpu_fmt->min_width;
	fsize->stepwise.max_width = vpu_fmt->max_width;
	fsize->stepwise.step_width = ENC_PIC_SIZE_STEP;
	fsize->stepwise.min_height = vpu_fmt->min_height;
	fsize->stepwise.max_height = vpu_fmt->max_height;
	fsize->stepwise.step_height = ENC_PIC_SIZE_STEP;

exit:
	return ret;
}

static int rtkve_enc_s_parm(struct file *file, void *fh,
			    struct v4l2_streamparm *a)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	struct v4l2_fract *timeperframe = &a->parm.output.timeperframe;
	int ret = 0;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = -EINVAL;
		goto exit;
	}

	if (timeperframe->numerator == 0 || timeperframe->denominator == 0) {
		timeperframe->numerator = DEFAULT_FRAMERATE_DENOM;
		timeperframe->denominator = DEFAULT_FRAMERATE_NUM;
	}

	inst->enc_params.framerate_denom = timeperframe->numerator;
	inst->enc_params.framerate_num = timeperframe->denominator;
	inst->enc_params.framerate = inst->enc_params.framerate_num /
				     inst->enc_params.framerate_denom;
	inst->enc_params.gop_size = inst->enc_params.framerate;
	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;

exit:
	return ret;
}

static int rtkve_enc_g_parm(struct file *file, void *fh,
			    struct v4l2_streamparm *a)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	int ret = 0;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = -EINVAL;
		goto exit;
	}

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.output.timeperframe.denominator =
		inst->enc_params.framerate_num;
	a->parm.output.timeperframe.numerator =
		inst->enc_params.framerate_denom;

exit:
	return ret;
}

static int rtkve_enc_enum_fmt_cap(struct file *file, void *fh,
				  struct v4l2_fmtdesc *f)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	const struct rtkve_match_data *dec_pdata = inst->dev->rtkve_mdata;
	const struct vpu_format *vpu_fmt;
	int ret = 0;

	vpu_fmt = dec_pdata->find_vpu_fmt_by_idx(f->index, VPU_FMT_TYPE_CODEC);
	if (!vpu_fmt) {
		ret = -EINVAL;
		goto exit;
	}

	f->pixelformat = vpu_fmt->v4l2_pix_fmt;
	f->flags = 0;

exit:
	return ret;
}

static int rtkve_enc_try_fmt_cap(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	const struct rtkve_match_data *dec_pdata = inst->dev->rtkve_mdata;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct vpu_format *vpu_fmt;
	int width, height;
	int ret = 0;

	dev_dbg(inst->dev->dev, "%s: %p4cc w %d h %d plane %d colorspace %d\n",
		__func__, &pix_mp->pixelformat, pix_mp->width, pix_mp->height,
		pix_mp->num_planes, pix_mp->colorspace);

	if (!V4L2_TYPE_IS_CAPTURE(f->type)) {
		ret = -EINVAL;
		goto exit;
	}

	vpu_fmt = dec_pdata->find_vpu_fmt(pix_mp->pixelformat,
					  VPU_FMT_TYPE_CODEC);
	if (!vpu_fmt) {
		width = inst->dst_fmt.width;
		height = inst->dst_fmt.height;
		pix_mp->pixelformat = inst->dst_fmt.pixelformat;
		pix_mp->num_planes = inst->dst_fmt.num_planes;
	} else {
		width = clamp(pix_mp->width, vpu_fmt->min_width,
			      inst->src_fmt.width);
		height = clamp(pix_mp->height, vpu_fmt->min_height,
			       inst->src_fmt.height);
		pix_mp->pixelformat = vpu_fmt->v4l2_pix_fmt;
		pix_mp->num_planes = vpu_fmt->num_planes;
	}

	rtkve_update_pix_fmt(inst, pix_mp, width, height);
	pix_mp->colorspace = inst->colorspace;
	pix_mp->ycbcr_enc = inst->ycbcr_enc;
	pix_mp->quantization = inst->quantization;
	pix_mp->xfer_func = inst->xfer_func;

exit:
	return ret;
}

static int rtkve_enc_s_fmt_cap(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	int i, ret = 0;

	dev_dbg(inst->dev->dev, "%s: %p4cc w %d h %d plane %d colorspace %d\n",
		__func__, &pix_mp->pixelformat, pix_mp->width, pix_mp->height,
		pix_mp->num_planes, pix_mp->colorspace);

	ret = rtkve_enc_try_fmt_cap(file, fh, f);
	if (ret) {
		dev_err(inst->dev->dev, "rtkve_enc_try_fmt_cap fail");
		goto exit;
	}

	inst->dst_fmt.width = pix_mp->width;
	inst->dst_fmt.height = pix_mp->height;
	inst->dst_fmt.pixelformat = pix_mp->pixelformat;
	inst->dst_fmt.field = pix_mp->field;
	inst->dst_fmt.flags = pix_mp->flags;
	inst->dst_fmt.num_planes = pix_mp->num_planes;
	for (i = 0; i < inst->dst_fmt.num_planes; i++) {
		inst->dst_fmt.plane_fmt[i].bytesperline =
			pix_mp->plane_fmt[i].bytesperline;
		inst->dst_fmt.plane_fmt[i].sizeimage =
			pix_mp->plane_fmt[i].sizeimage;
	}

	rtkve_update_pix_fmt(inst, &inst->src_fmt, pix_mp->width,
			     pix_mp->height);

exit:
	return ret;
}

static int rtkve_enc_g_fmt_cap(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	int i;

	pix_mp->width = inst->dst_fmt.width;
	pix_mp->height = inst->dst_fmt.height;
	pix_mp->pixelformat = inst->dst_fmt.pixelformat;
	pix_mp->field = inst->dst_fmt.field;
	pix_mp->flags = inst->dst_fmt.flags;
	pix_mp->num_planes = inst->dst_fmt.num_planes;
	for (i = 0; i < pix_mp->num_planes; i++) {
		pix_mp->plane_fmt[i].bytesperline =
			inst->dst_fmt.plane_fmt[i].bytesperline;
		pix_mp->plane_fmt[i].sizeimage =
			inst->dst_fmt.plane_fmt[i].sizeimage;
	}

	pix_mp->colorspace = inst->colorspace;
	pix_mp->ycbcr_enc = inst->ycbcr_enc;
	pix_mp->quantization = inst->quantization;
	pix_mp->xfer_func = inst->xfer_func;

	return 0;
}

static int rtkve_enc_enum_fmt_out(struct file *file, void *fh,
				  struct v4l2_fmtdesc *f)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	const struct rtkve_match_data *dec_pdata = inst->dev->rtkve_mdata;
	const struct vpu_format *vpu_fmt;
	int ret = 0;

	dev_dbg(inst->dev->dev, "%s: index %d\n", __func__, f->index);

	vpu_fmt = dec_pdata->find_vpu_fmt_by_idx(f->index, VPU_FMT_TYPE_RAW);
	if (!vpu_fmt) {
		ret = -EINVAL;
		goto exit;
	}

	f->pixelformat = vpu_fmt->v4l2_pix_fmt;

exit:
	return ret;
}

static int rtkve_enc_try_fmt_out(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	const struct rtkve_match_data *dec_pdata = inst->dev->rtkve_mdata;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct vpu_format *vpu_fmt;
	int width, height;
	int ret = 0;

	dev_dbg(inst->dev->dev, "%s %p4cc w %d h %d plane %d colorspace %d\n",
		__func__, &pix_mp->pixelformat, pix_mp->width, pix_mp->height,
		pix_mp->num_planes, pix_mp->colorspace);

	if (!V4L2_TYPE_IS_OUTPUT(f->type)) {
		ret = -EINVAL;
		goto exit;
	}

	vpu_fmt =
		dec_pdata->find_vpu_fmt(pix_mp->pixelformat, VPU_FMT_TYPE_RAW);
	if (!vpu_fmt) {
		width = inst->src_fmt.width;
		height = inst->src_fmt.height;
		pix_mp->pixelformat = inst->src_fmt.pixelformat;
		pix_mp->num_planes = inst->src_fmt.num_planes;
	} else {
		width = pix_mp->width;
		height = pix_mp->height;
		pix_mp->pixelformat = vpu_fmt->v4l2_pix_fmt;
		pix_mp->num_planes = vpu_fmt->num_planes;
	}

	rtkve_update_pix_fmt(inst, pix_mp, width, height);
exit:
	return ret;
}

static int rtkve_enc_s_fmt_out(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	int i, ret = 0;

	dev_dbg(inst->dev->dev, "%s: %p4cc w %d h %d plane %d colorspace %d\n",
		__func__, &pix_mp->pixelformat, pix_mp->width, pix_mp->height,
		pix_mp->num_planes, pix_mp->colorspace);

	ret = rtkve_enc_try_fmt_out(file, fh, f);
	if (ret) {
		dev_err(inst->dev->dev, "rtkve_dec_try_fmt_out fail");
		goto exit;
	}

	inst->src_fmt.width = pix_mp->width;
	inst->src_fmt.height = pix_mp->height;
	inst->src_fmt.pixelformat = pix_mp->pixelformat;
	inst->src_fmt.field = pix_mp->field;
	inst->src_fmt.flags = pix_mp->flags;
	inst->src_fmt.num_planes = pix_mp->num_planes;
	for (i = 0; i < inst->src_fmt.num_planes; i++) {
		inst->src_fmt.plane_fmt[i].bytesperline =
			pix_mp->plane_fmt[i].bytesperline;
		inst->src_fmt.plane_fmt[i].sizeimage =
			pix_mp->plane_fmt[i].sizeimage;
	}

	inst->colorspace = pix_mp->colorspace;
	inst->ycbcr_enc = pix_mp->ycbcr_enc;
	inst->quantization = pix_mp->quantization;
	inst->xfer_func = pix_mp->xfer_func;

	rtkve_update_pix_fmt(inst, &inst->dst_fmt,
		pix_mp->width, pix_mp->height);
exit:
	return ret;
}

static int rtkve_enc_g_fmt_out(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(fh);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	int i;

	pix_mp->width = inst->src_fmt.width;
	pix_mp->height = inst->src_fmt.height;
	pix_mp->pixelformat = inst->src_fmt.pixelformat;
	pix_mp->field = inst->src_fmt.field;
	pix_mp->flags = inst->src_fmt.flags;
	pix_mp->num_planes = inst->src_fmt.num_planes;
	for (i = 0; i < pix_mp->num_planes; i++) {
		pix_mp->plane_fmt[i].bytesperline =
			inst->src_fmt.plane_fmt[i].bytesperline;
		pix_mp->plane_fmt[i].sizeimage =
			inst->src_fmt.plane_fmt[i].sizeimage;
	}

	pix_mp->colorspace = inst->colorspace;
	pix_mp->ycbcr_enc = inst->ycbcr_enc;
	pix_mp->quantization = inst->quantization;
	pix_mp->xfer_func = inst->xfer_func;

	return 0;
}

static int rtkve_enc_reqbufs(struct file *file, void *priv,
			     struct v4l2_requestbuffers *rb)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(priv);
	struct vpu_device *dev = inst->dev;
	const struct rtkve_match_data *enc_pdata = dev->rtkve_mdata;
	int ret = 0;

	ret = v4l2_m2m_ioctl_reqbufs(file, priv, rb);
	if (ret)
		goto exit;

	if (rb->count != 0 && !inst->enc_hdl)
		ret = enc_pdata->create_instance(inst);

exit:
	return ret;
}

static int rtkve_enc_subscribe_event(struct v4l2_fh *fh,
				     const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static int rtkve_enc_cmd(struct file *file, void *priv,
			 struct v4l2_encoder_cmd *cmd)
{
	struct vpu_instance *inst = rtkve_to_vpu_inst(priv);
	struct v4l2_m2m_ctx *ctx = inst->v4l2_fh.m2m_ctx;
	struct vb2_v4l2_buffer *buf;
	int ret;

	ret = v4l2_m2m_ioctl_try_encoder_cmd(file, priv, cmd);
	if (ret)
		return ret;

	if (!vb2_is_streaming(v4l2_m2m_get_src_vq(ctx)) ||
	    !vb2_is_streaming(v4l2_m2m_get_dst_vq(ctx)))
		return 0;

	switch (cmd->cmd) {
	case V4L2_ENC_CMD_STOP:
		buf = v4l2_m2m_last_src_buf(ctx);
		if (buf) {
			buf->flags |= V4L2_BUF_FLAG_LAST;
			inst->eos = true;
		} else {
			struct vb2_queue *dst_vq;

			dst_vq = v4l2_m2m_get_vq(ctx,
						 V4L2_BUF_TYPE_VIDEO_CAPTURE);
			dst_vq->last_buffer_dequeued = true;
			wake_up(&dst_vq->done_wq);
		}
		break;
	case V4L2_ENC_CMD_START:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

struct v4l2_ioctl_ops rtkve_enc_ioctl_ops = {
	.vidioc_querycap = rtkve_enc_querycap,
	.vidioc_enum_framesizes = rtkve_enc_enum_framesizes,

	.vidioc_s_parm = rtkve_enc_s_parm,
	.vidioc_g_parm = rtkve_enc_g_parm,

	.vidioc_enum_fmt_vid_cap = rtkve_enc_enum_fmt_cap,
	.vidioc_s_fmt_vid_cap_mplane = rtkve_enc_s_fmt_cap,
	.vidioc_g_fmt_vid_cap_mplane = rtkve_enc_g_fmt_cap,
	.vidioc_try_fmt_vid_cap_mplane = rtkve_enc_try_fmt_cap,

	.vidioc_enum_fmt_vid_out = rtkve_enc_enum_fmt_out,
	.vidioc_s_fmt_vid_out_mplane = rtkve_enc_s_fmt_out,
	.vidioc_g_fmt_vid_out_mplane = rtkve_enc_g_fmt_out,
	.vidioc_try_fmt_vid_out_mplane = rtkve_enc_try_fmt_out,

	.vidioc_reqbufs = rtkve_enc_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,

	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event = rtkve_enc_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,

	.vidioc_encoder_cmd = rtkve_enc_cmd,
	.vidioc_try_encoder_cmd = v4l2_m2m_ioctl_try_encoder_cmd,
};

static void rtkve_enc_device_run(void *priv)
{
	struct vpu_instance *inst = priv;

	queue_work(inst->dev->encode_workqueue, &inst->encode_work);
}

static int rtkve_enc_job_ready(void *priv)
{
	struct vpu_instance *inst = priv;
	int ret = 0;

	dev_dbg(inst->dev->dev, "[%d]%s: state %d\n", inst->id, __func__,
		inst->state);

	if (inst->state == VPU_INST_STATE_STOP && inst->eos)
		ret = 0;
	else
		ret = 1;

	return ret;
}

static void rtkve_enc_job_abort(void *priv)
{
	struct vpu_instance *inst = priv;
	struct vpu_device *dev = inst->dev;
	const struct rtkve_match_data *dec_pdata = dev->rtkve_mdata;

	dev_dbg(inst->dev->dev, "[%d]%s: state %d\n", inst->id, __func__,
		inst->state);

	dec_pdata->stop_decode(inst);
}

static const struct v4l2_m2m_ops rtkve_enc_m2m_ops = {
	.device_run = rtkve_enc_device_run,
	.job_ready = rtkve_enc_job_ready,
	.job_abort = rtkve_enc_job_abort,
};

int rtkve_enc_init_m2m_dev(struct vpu_device *dev)
{
	dev->m2m_dev = v4l2_m2m_init(&rtkve_enc_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		dev_err(dev->dev, "v4l2_m2m_init fail: %ld\n",
			PTR_ERR(dev->m2m_dev));
		return PTR_ERR(dev->m2m_dev);
	}

	return 0;
}
