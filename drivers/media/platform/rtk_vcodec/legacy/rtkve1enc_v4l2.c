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
#include "rtkve1enc_common.h"

void rtkve_update_pix_fmt(struct rtkve1enc_ctx *ctx,
			  struct v4l2_pix_format_mplane *pix_mp,
			  unsigned int width, unsigned int height)
{
	pix_mp->flags = 0;
	pix_mp->field = V4L2_FIELD_NONE;
	memset(pix_mp->reserved, 0, sizeof(pix_mp->reserved));

	switch (pix_mp->pixelformat) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		pix_mp->width = width;
		pix_mp->height = height;
		dev_dbg(ctx->dev->dev, "%d.%s.%p4cc.width:%d.height:%d\n",
			__LINE__, __func__,
			&pix_mp->pixelformat,
			pix_mp->width, pix_mp->height);

		pix_mp->plane_fmt[0].bytesperline = pix_mp->width;
		pix_mp->plane_fmt[0].sizeimage =
			pix_mp->width * pix_mp->height * 3 / 2;
		break;
	default:
		pix_mp->width = width;
		pix_mp->height = height;
		dev_dbg(ctx->dev->dev, "%d.%s.%p4cc.width:%d.height:%d\n",
			__LINE__, __func__,
			&pix_mp->pixelformat,
			pix_mp->width, pix_mp->height);
		pix_mp->plane_fmt[0].bytesperline = 0;
		if (!pix_mp->plane_fmt[0].sizeimage)
			pix_mp->plane_fmt[0].sizeimage = width * height;
		break;
	}
}

void rtkve_set_default_format(struct rtkve1enc_ctx *ctx,
			      struct v4l2_pix_format_mplane *src_fmt,
			      struct v4l2_pix_format_mplane *dst_fmt)
{
	const struct vpu_format *vpu_fmt;

	vpu_fmt = ctx->ops->find_vpu_fmt_by_idx(0, VPU_FMT_TYPE_RAW);

	src_fmt->pixelformat = vpu_fmt->v4l2_pix_fmt;
	src_fmt->num_planes = vpu_fmt->num_planes;
	rtkve_update_pix_fmt(ctx, src_fmt, vpu_fmt->max_width, vpu_fmt->max_height);

	vpu_fmt = ctx->ops->find_vpu_fmt_by_idx(0, VPU_FMT_TYPE_CODEC);

	dst_fmt->pixelformat = vpu_fmt->v4l2_pix_fmt;
	dst_fmt->num_planes = vpu_fmt->num_planes;
	rtkve_update_pix_fmt(ctx, dst_fmt, vpu_fmt->max_width, vpu_fmt->max_height);
}

static int rtkve_enc_querycap(struct file *file, void *fh,
			      struct v4l2_capability *cap)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px\n",
		__LINE__, __func__, ctx);
	strscpy(cap->driver, RTKVE1_ENC_DEV_NAME, sizeof(cap->driver));
	strscpy(cap->card, RTKVE1_ENC_DEV_NAME, sizeof(cap->card));
	strscpy(cap->bus_info, "platform:" RTKVE1_ENC_DEV_NAME,
		sizeof(cap->bus_info));
	dev_dbg(ctx->dev->dev, "%d.%s.[-]\n",
		__LINE__, __func__);

	return 0;
}

static int rtkve_enc_enum_framesizes(struct file *f, void *fh,
				     struct v4l2_frmsizeenum *fsize)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	const struct vpu_format *vpu_fmt;
	int ret = 0;

	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px.index:%d\n",
        __LINE__, __func__,
		ctx, fsize->index);
	if (fsize->index) {
		ret = -EINVAL;
		goto exit;
	}

	vpu_fmt = ctx->ops->find_vpu_fmt(fsize->pixel_format,
					  VPU_FMT_TYPE_CODEC);
	if (!vpu_fmt) {
		vpu_fmt = ctx->ops->find_vpu_fmt(fsize->pixel_format,
						  VPU_FMT_TYPE_RAW);
		if (!vpu_fmt) {
			ret = -EINVAL;
			dev_err(ctx->dev->dev, "%d.%s.can't find match VPU_FMT_TYPE_RAW.pixel_format:0x%x\n",
				__LINE__, __func__, fsize->pixel_format);
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
	dev_dbg(ctx->dev->dev, "%d.%s.[-] ret:%d\n",
        __LINE__, __func__, ret);
	return ret;
}

static int rtkve_enc_s_parm(struct file *file, void *fh,
			    struct v4l2_streamparm *a)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	struct v4l2_fract *timeperframe = &a->parm.output.timeperframe;
	int ret = 0;

	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px.type:%d\n",
        __LINE__, __func__, ctx, a->type);
	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = -EINVAL;
		goto exit;
	}

	dev_dbg(ctx->dev->dev, "%d.%s.numerator:%d.denominator:%d\n",
		__LINE__, __func__,
		timeperframe->numerator,
		timeperframe->denominator);
	if (timeperframe->numerator == 0 || timeperframe->denominator == 0) {
		ctx->enc_params.framerate_num = DEFAULT_FRAMERATE_NUM;
		ctx->enc_params.framerate_denom = DEFAULT_FRAMERATE_DENOM;
	}
	// refer product.c
	if ((timeperframe->denominator/timeperframe->numerator) < 15) {
		dev_err(ctx->dev->dev, "%d.%s.invalid timeperframe %d/%d\n",
			__LINE__, __func__,
			timeperframe->numerator,
			timeperframe->denominator);
		ctx->enc_params.framerate_num = DEFAULT_FRAMERATE_NUM;
		ctx->enc_params.framerate_denom = DEFAULT_FRAMERATE_DENOM;
	}
	//
	else {
		/* the meaning of numerator/denominator of timeperframe is different than framerate_num/framerate_denom */
		/* ex: timeperframe is 1/30, then framerate is 30/1 */
		ctx->enc_params.framerate_num = timeperframe->denominator;
		ctx->enc_params.framerate_denom = timeperframe->numerator;
	}

	//ctx->enc_params.framerate = ctx->enc_params.framerate_num /
	//			     ctx->enc_params.framerate_denom;
	ctx->enc_params.framerate = (ctx->enc_params.framerate_denom - 1) << 16;
	ctx->enc_params.framerate |= ctx->enc_params.framerate_num;
	dev_dbg(ctx->dev->dev, "%d.%s.framerate:0x%x (%d/%d)\n",
		__LINE__, __func__,
		ctx->enc_params.framerate,
		ctx->enc_params.framerate_num,
		ctx->enc_params.framerate_denom);

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;

exit:
	dev_dbg(ctx->dev->dev, "%d.%s.[-] ret:%d\n",
		__LINE__, __func__, ret);
	return ret;
}

static int rtkve_enc_g_parm(struct file *file, void *fh,
			    struct v4l2_streamparm *a)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	int ret = 0;

	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px.type:%d\n",
        __LINE__, __func__, ctx, a->type);
	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = -EINVAL;
		goto exit;
	}

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.output.timeperframe.denominator =
		ctx->enc_params.framerate_num;
	a->parm.output.timeperframe.numerator =
		ctx->enc_params.framerate_denom;

exit:
	dev_dbg(ctx->dev->dev, "%d.%s.[-] ret:%d\n",
		__LINE__, __func__, ret);
	return ret;
}

static int rtkve_enc_enum_fmt_cap(struct file *file, void *fh,
				  struct v4l2_fmtdesc *f)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	const struct vpu_format *vpu_fmt;
	int ret = 0;

	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px.index:%d\n",
        __LINE__, __func__, ctx, f->index);
	vpu_fmt = ctx->ops->find_vpu_fmt_by_idx(f->index, VPU_FMT_TYPE_CODEC);
	if (!vpu_fmt) {
		ret = -EINVAL;
		goto exit;
	}

	f->pixelformat = vpu_fmt->v4l2_pix_fmt;
	f->flags = 0;

exit:
	dev_dbg(ctx->dev->dev, "%d.%s.[-] ret:%d\n",
        __LINE__, __func__, ret);
	return ret;
}

static int rtkve_enc_try_fmt_cap(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct vpu_format *vpu_fmt;
	int width, height;
	int ret = 0;

	dev_dbg(ctx->dev->dev, "%s: 4cc %d w %d h %d plane %d colorspace %d\n",
		__func__, pix_mp->pixelformat, pix_mp->width, pix_mp->height,
		pix_mp->num_planes, pix_mp->colorspace);

	if (!V4L2_TYPE_IS_CAPTURE(f->type)) {
		ret = -EINVAL;
		goto exit;
	}

	vpu_fmt = ctx->ops->find_vpu_fmt(pix_mp->pixelformat,
					  VPU_FMT_TYPE_CODEC);
	if (!vpu_fmt) {
		width = ctx->dst_fmt.width;
		height = ctx->dst_fmt.height;
		pix_mp->pixelformat = ctx->dst_fmt.pixelformat;
		pix_mp->num_planes = ctx->dst_fmt.num_planes;
	} else {
		width = clamp(pix_mp->width, vpu_fmt->min_width,
			      ctx->src_fmt.width);
		dev_dbg(ctx->dev->dev, "%d.%s.width:%d.%d.%d.%d\n",
			__LINE__, __func__,
			width, pix_mp->width, vpu_fmt->min_width, ctx->src_fmt.width);
		height = clamp(pix_mp->height, vpu_fmt->min_height,
			       ctx->src_fmt.height);
		dev_dbg(ctx->dev->dev, "%d.%s.height:%d.%d.%d.%d\n",
			__LINE__, __func__,
			height, pix_mp->height, vpu_fmt->min_height, ctx->src_fmt.height);
		pix_mp->pixelformat = vpu_fmt->v4l2_pix_fmt;
		pix_mp->num_planes = vpu_fmt->num_planes;
	}

	rtkve_update_pix_fmt(ctx, pix_mp, width, height);
	pix_mp->colorspace = ctx->colorspace;
	pix_mp->ycbcr_enc = ctx->ycbcr_enc;
	pix_mp->quantization = ctx->quantization;
	pix_mp->xfer_func = ctx->xfer_func;

exit:
	return ret;
}

static int rtkve_enc_s_fmt_cap(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	int i, ret = 0;

	dev_dbg(ctx->dev->dev, "%d.%s.%p4cc w %d h %d plane %d colorspace %d\n",
		__LINE__, __func__,
		&pix_mp->pixelformat, pix_mp->width, pix_mp->height,
		pix_mp->num_planes, pix_mp->colorspace);

	ret = rtkve_enc_try_fmt_cap(file, fh, f);
	if (ret) {
		dev_err(ctx->dev->dev, "rtkve_enc_try_fmt_cap fail");
		goto exit;
	}

	ctx->dst_fmt.width = pix_mp->width;
	ctx->dst_fmt.height = pix_mp->height;
	dev_dbg(ctx->dev->dev, "%d.%s.dst_fmt.width:%d.height:%d\n",
		__LINE__, __func__,
		ctx->dst_fmt.width, ctx->dst_fmt.height);
	ctx->dst_fmt.pixelformat = pix_mp->pixelformat;
	ctx->dst_fmt.field = pix_mp->field;
	ctx->dst_fmt.flags = pix_mp->flags;
	ctx->dst_fmt.num_planes = pix_mp->num_planes;
	for (i = 0; i < ctx->dst_fmt.num_planes; i++) {
		ctx->dst_fmt.plane_fmt[i].bytesperline =
			pix_mp->plane_fmt[i].bytesperline;
		ctx->dst_fmt.plane_fmt[i].sizeimage =
			pix_mp->plane_fmt[i].sizeimage;
	}

	rtkve_update_pix_fmt(ctx, &ctx->src_fmt, pix_mp->width,
			     pix_mp->height);

exit:
	dev_dbg(ctx->dev->dev, "%d.%s.[-] ret:%d\n",
        __LINE__, __func__, ret);
	return ret;
}

static int rtkve_enc_g_fmt_cap(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	int i;

	pix_mp->width = ctx->dst_fmt.width;
	pix_mp->height = ctx->dst_fmt.height;
	pix_mp->pixelformat = ctx->dst_fmt.pixelformat;
	pix_mp->field = ctx->dst_fmt.field;
	pix_mp->flags = ctx->dst_fmt.flags;
	pix_mp->num_planes = ctx->dst_fmt.num_planes;
	for (i = 0; i < pix_mp->num_planes; i++) {
		pix_mp->plane_fmt[i].bytesperline =
			ctx->dst_fmt.plane_fmt[i].bytesperline;
		pix_mp->plane_fmt[i].sizeimage =
			ctx->dst_fmt.plane_fmt[i].sizeimage;
	}

	pix_mp->colorspace = ctx->colorspace;
	pix_mp->ycbcr_enc = ctx->ycbcr_enc;
	pix_mp->quantization = ctx->quantization;
	pix_mp->xfer_func = ctx->xfer_func;

	return 0;
}

static int rtkve_enc_enum_fmt_out(struct file *file, void *fh,
				  struct v4l2_fmtdesc *f)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	const struct vpu_format *vpu_fmt;
	int ret = 0;

	dev_dbg(ctx->dev->dev, "%s: index %d\n", __func__, f->index);

	vpu_fmt = ctx->ops->find_vpu_fmt_by_idx(f->index, VPU_FMT_TYPE_RAW);
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
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct vpu_format *vpu_fmt;
	int width, height;
	int ret = 0;

	dev_dbg(ctx->dev->dev, "%d.%s.%p4cc w %d h %d plane %d colorspace %d\n",
		__LINE__, __func__,
		&pix_mp->pixelformat, pix_mp->width, pix_mp->height,
		pix_mp->num_planes, pix_mp->colorspace);

	if (!V4L2_TYPE_IS_OUTPUT(f->type)) {
		ret = -EINVAL;
		goto exit;
	}

	vpu_fmt =
		ctx->ops->find_vpu_fmt(pix_mp->pixelformat, VPU_FMT_TYPE_RAW);
	if (!vpu_fmt) {
		width = ctx->src_fmt.width;
		height = ctx->src_fmt.height;
		pix_mp->pixelformat = ctx->src_fmt.pixelformat;
		pix_mp->num_planes = ctx->src_fmt.num_planes;
	} else {
		width  = round_up(pix_mp->width, 32);
		height = round_up(pix_mp->height, 16);
		dev_dbg(ctx->dev->dev, "%d.%s.width:%d.height:%d\n",
			__LINE__, __func__,
			width, height);
		pix_mp->pixelformat = vpu_fmt->v4l2_pix_fmt;
		pix_mp->num_planes = vpu_fmt->num_planes;
	}

	rtkve_update_pix_fmt(ctx, pix_mp, width, height);
exit:
	dev_dbg(ctx->dev->dev, "%d.%s.[-] ret:%d\n",
        __LINE__, __func__, ret);
	return ret;
}

static int rtkve_enc_s_fmt_out(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	int i, ret = 0;

	dev_dbg(ctx->dev->dev, "%d.%s.%p4cc w %d h %d plane %d colorspace %d\n",
		__LINE__, __func__,
		&pix_mp->pixelformat, pix_mp->width, pix_mp->height,
		pix_mp->num_planes, pix_mp->colorspace);

	ret = rtkve_enc_try_fmt_out(file, fh, f);
	if (ret) {
		dev_err(ctx->dev->dev, "rtkve_dec_try_fmt_out fail");
		goto exit;
	}

	ctx->src_fmt.width = pix_mp->width;
	ctx->src_fmt.height = pix_mp->height;
	ctx->src_fmt.pixelformat = pix_mp->pixelformat;
	ctx->src_fmt.field = pix_mp->field;
	ctx->src_fmt.flags = pix_mp->flags;
	ctx->src_fmt.num_planes = pix_mp->num_planes;
	for (i = 0; i < ctx->src_fmt.num_planes; i++) {
		ctx->src_fmt.plane_fmt[i].bytesperline =
			pix_mp->plane_fmt[i].bytesperline;
		ctx->src_fmt.plane_fmt[i].sizeimage =
			pix_mp->plane_fmt[i].sizeimage;
	}

	ctx->colorspace = pix_mp->colorspace;
	ctx->ycbcr_enc = pix_mp->ycbcr_enc;
	ctx->quantization = pix_mp->quantization;
	ctx->xfer_func = pix_mp->xfer_func;

	//rtkve_update_pix_fmt(ctx, &ctx->dst_fmt,
	//	pix_mp->width, pix_mp->height);
exit:
	return ret;
}

static int rtkve_enc_g_fmt_out(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	int i;

	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px\n",
        __LINE__, __func__, ctx);
	pix_mp->width = ctx->src_fmt.width;
	pix_mp->height = ctx->src_fmt.height;
	pix_mp->pixelformat = ctx->src_fmt.pixelformat;
	pix_mp->field = ctx->src_fmt.field;
	pix_mp->flags = ctx->src_fmt.flags;
	pix_mp->num_planes = ctx->src_fmt.num_planes;
	for (i = 0; i < pix_mp->num_planes; i++) {
		pix_mp->plane_fmt[i].bytesperline =
			ctx->src_fmt.plane_fmt[i].bytesperline;
		pix_mp->plane_fmt[i].sizeimage =
			ctx->src_fmt.plane_fmt[i].sizeimage;
	}

	pix_mp->colorspace = ctx->colorspace;
	pix_mp->ycbcr_enc = ctx->ycbcr_enc;
	pix_mp->quantization = ctx->quantization;
	pix_mp->xfer_func = ctx->xfer_func;

	dev_dbg(ctx->dev->dev, "%d.%s.[-]\n",
        __LINE__, __func__);
	return 0;
}

static int rtkve_enc_reqbufs(struct file *file, void *priv,
			     struct v4l2_requestbuffers *rb)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(priv);
	int ret = 0;

	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px\n",
        __LINE__, __func__, ctx);

	ret = v4l2_m2m_ioctl_reqbufs(file, priv, rb);
	if (ret) {
		dev_err(ctx->dev->dev, "%d.%s.v4l2_m2m_ioctl_reqbufs() fail.ctx:0x%px.ret:%d\n",
			__LINE__, __func__,
			ctx, ret);
		goto exit;
	}

	if (ctx->ops->reqbufs) {
		ret = ctx->ops->reqbufs(ctx, rb);
		if (ret) {
			dev_err(ctx->dev->dev, "%d.%s.reqbufs() fail.ctx:0x%px\n",
				__LINE__, __func__, ctx);
		}
	}

exit:
	dev_dbg(ctx->dev->dev, "%d.%s.[-] ret:%d\n",
        __LINE__, __func__, ret);
	return ret;
}

static int rtkve_enc_subscribe_event(struct v4l2_fh *fh,
				     const struct v4l2_event_subscription *sub)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(fh);
	dev_dbg(ctx->dev->dev, "%d.%s.ctx:0x%px.type:%d\n",
        __LINE__, __func__,
		ctx, sub->type);
/*
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
*/
	return 0;
}

static int rtkve_enc_cmd(struct file *file, void *priv,
			 struct v4l2_encoder_cmd *cmd)
{
	struct rtkve1enc_ctx *ctx = v4l2fh_to_ctx(priv);
	struct v4l2_m2m_ctx *m2m_ctx = ctx->v4l2_fh.m2m_ctx;
	struct vb2_v4l2_buffer *buf;
	int ret;

	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px.cmd:%d\n",
        __LINE__, __func__,
		ctx, cmd->cmd);

	ret = v4l2_m2m_ioctl_try_encoder_cmd(file, priv, cmd);
	if (ret)
		return ret;

	if (!vb2_is_streaming(v4l2_m2m_get_src_vq(m2m_ctx)) ||
	    !vb2_is_streaming(v4l2_m2m_get_dst_vq(m2m_ctx)))
		return 0;

	switch (cmd->cmd) {
	case V4L2_ENC_CMD_STOP:
		buf = v4l2_m2m_last_src_buf(m2m_ctx);
		if (buf) {
			buf->flags |= V4L2_BUF_FLAG_LAST;
			ctx->eos = true;
		} else {
			struct vb2_queue *dst_vq;

			dst_vq = v4l2_m2m_get_vq(m2m_ctx,
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

	dev_dbg(ctx->dev->dev, "%d.%s.[-] ctx:0x%px.cmd:%d\n",
        __LINE__, __func__,
		ctx, cmd->cmd);
	return 0;
}

struct v4l2_ioctl_ops rtkve1enc_ioctl_ops = {
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
#if 0
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
#endif
