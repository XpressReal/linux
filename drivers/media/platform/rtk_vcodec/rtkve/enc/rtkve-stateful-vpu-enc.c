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
#include "rtkve-rpc.h"
#include "rtkve-vpu.h"

#define MAX_CTRL_LIST (3)
#define CTRL_RINGBUF_NUM (32)
#define MEANINGLESS (~0)
#define MIN_BITRATE (64)
#define MAX_BITRATE (40 * 1024 * 1024)
#define DEF_BITRATE (5 * 1024 * 1024)
#define DEF_OUTPUT_BUF (4)
#define DEF_CAPTURE_BUF (4)

static int rtkve_enc_stop(struct vpu_instance *inst, unsigned int type);
static void rtkve_enc_return_dstbuf(struct vpu_instance *inst);
extern void rtkve_update_pix_fmt(struct vpu_instance *inst,
				 struct v4l2_pix_format_mplane *pix_mp,
				 unsigned int width, unsigned int height);

static const struct vpu_format rtkve_stateful_enc_fmt_list[2][2] = {
	[VPU_FMT_TYPE_CODEC] = {
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_HEVC,
			.max_width = HEVC_MAX_ENC_PIC_WIDTH,
			.min_width = HEVC_MIN_ENC_PIC_WIDTH,
			.max_height = HEVC_MAX_ENC_PIC_HEIGHT,
			.min_height = HEVC_MIN_ENC_PIC_HEIGHT,
			.num_planes = 1,
		},
	},
	[VPU_FMT_TYPE_RAW] = {
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV12,
			.max_width = RAW_MAX_ENC_PIC_WIDTH,
			.min_width = RAW_MIN_ENC_PIC_WIDTH,
			.max_height = RAW_MAX_ENC_PIC_HEIGHT,
			.min_height = RAW_MIN_ENC_PIC_HEIGHT,
			.num_planes = 1,
		},
	}
};

static inline struct vpu_instance *ctrl_to_dec_inst(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct vpu_instance, v4l2_ctrl_hdl);
}

static int rtkve_enc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpu_instance *inst = ctrl_to_dec_inst(ctrl);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		dev_dbg(inst->dev->dev, "CTRL set bitrate mode = %d",
			ctrl->val);
		if (ctrl->val == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) {
			inst->enc_params.bitrate_mode = VIDEO_RATE_VBR;
		} else if (ctrl->val == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR) {
			inst->enc_params.bitrate_mode = VIDEO_RATE_CBR;
		} else {
			inst->enc_params.bitrate_mode = VIDEO_RATE_CVBR;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		dev_dbg(inst->dev->dev, "CTRL set bitrate = %d", ctrl->val);
		inst->enc_params.bitrate = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		dev_dbg(inst->dev->dev, "CTRL set header mode = %d", ctrl->val);
		inst->enc_params.header_with_frm = ctrl->val; //TODO
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_PROFILE:
		dev_dbg(inst->dev->dev, "CTRL set profile = %d", ctrl->val);
		if (ctrl->val != V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN) {
			dev_err(inst->dev->dev, "Unsupported profile = %d",
				ctrl->val);
		}
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_LEVEL:
		dev_dbg(inst->dev->dev, "CTRL set level = %d", ctrl->val);
		if (ctrl->val > V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1) {
			dev_err(inst->dev->dev, "Unsupported level = %d",
				ctrl->val);
		}
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		dev_dbg(inst->dev->dev, "CTRL set GOP size = %d", ctrl->val);
		inst->enc_params.gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
		dev_dbg(inst->dev->dev, "CTRL force key frame = %d", ctrl->val);
		inst->enc_params.force_key_frm = ctrl->val; //TODO
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops rtkve_enc_ctrl_ops = {
	.s_ctrl = rtkve_enc_s_ctrl,
};

static const struct vpu_format *rtkve_enc_find_fmt(unsigned int v4l2_pix_fmt,
						   enum vpu_fmt_type type)
{
	unsigned int index;
	const struct vpu_format *fmt = NULL;

	for (index = 0; index < ARRAY_SIZE(rtkve_stateful_enc_fmt_list[type]);
	     index++) {
		if (rtkve_stateful_enc_fmt_list[type][index].v4l2_pix_fmt ==
		    v4l2_pix_fmt)
			fmt = &rtkve_stateful_enc_fmt_list[type][index];
	}

	return fmt;
}

static const struct vpu_format *
rtkve_enc_find_fmt_by_idx(unsigned int idx, enum vpu_fmt_type type)
{
	const struct vpu_format *fmt = NULL;

	if (idx >= ARRAY_SIZE(rtkve_stateful_enc_fmt_list[type]))
		goto exit;

	if (!rtkve_stateful_enc_fmt_list[type][idx].v4l2_pix_fmt)
		goto exit;

	fmt = &rtkve_stateful_enc_fmt_list[type][idx];

exit:
	return fmt;
}

static struct vpu_instance *fh_to_inst(struct v4l2_fh *fh)
{
	struct vpu_instance *inst =
		container_of(fh, struct vpu_instance, v4l2_fh);
	return inst;
}

static int rtkve_enc_create_instance(struct vpu_instance *inst)
{
	int ret = 0;

	dev_dbg(inst->dev->dev, "%s: enter\n", __func__);

	ret = rtkve_rpc_create_encoder(inst);
	if (ret) {
		dev_err(inst->dev->dev, "create encoder fail\n");
		goto exit;
	}

	INIT_LIST_HEAD(&inst->srcbuf_list);
	INIT_LIST_HEAD(&inst->dstbuf_list);
	spin_lock_init(&inst->srcbuf_lock);
	spin_lock_init(&inst->dstbuf_lock);

	inst->state = VPU_INST_STATE_OPEN;
exit:
	return ret;
}

static void rtkve_enc_destroy_instance(struct vpu_instance *inst)
{
	struct vpu_handler *enc_hndl = inst->enc_hdl;
	int ret = 0;

	if (!inst || !enc_hndl) {
		goto exit;
	}

	ret = rtkve_rpc_stop(enc_hndl);
	if (ret) {
		dev_err(inst->dev->dev, "failed destroy encode instance: %d\n",
			ret);
	}
	enc_hndl->is_running = false;

	if (inst->input_thread) {
		wake_up_interruptible(&inst->input_waitq);
		kthread_stop(inst->input_thread);
		inst->input_thread = NULL;
	}

	ret = rtkve_inband_release_ref_buffer(enc_hndl);
	if (ret) {
		dev_err(inst->dev->dev, "failed free reference buf: %d\n", ret);
	}

	ret = rtkve_rpc_destroy_encoder(inst);
	if (ret) {
		dev_err(inst->dev->dev, "failed destroy encoder instance: %d\n",
			ret);
	}

exit:
	return;
}

static void rtkve_enc_buf_queue_src(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_instance *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vpu_buffer *src_buf = rtkve_to_vpu_buf(vbuf);
	unsigned long flags;

	dev_dbg(inst->dev->dev,
		"type %4d index %4d size[0] %4ld size[1] : %4ld | size[2] : %4ld\n",
		vb->type, vb->index, vb2_plane_size(&vbuf->vb2_buf, 0),
		vb2_plane_size(&vbuf->vb2_buf, 1),
		vb2_plane_size(&vbuf->vb2_buf, 2));

	vbuf->sequence = inst->queued_src_buf_num++;

	v4l2_m2m_buf_queue(inst->v4l2_fh.m2m_ctx, vbuf);
	spin_lock_irqsave(&inst->srcbuf_lock, flags);
	list_add_tail(&src_buf->list, &inst->srcbuf_list);
	spin_unlock_irqrestore(&inst->srcbuf_lock, flags);

	wake_up_interruptible(&inst->input_waitq);
	if (inst->state == VPU_INST_STATE_PIC_RUN)
		wake_up_interruptible(&inst->output_waitq);
}

static struct vpu_buffer *rtkve_enc_next_srcbuf(struct vpu_instance *inst,
						bool check_consume)
{
	struct vpu_buffer *b = NULL;
	unsigned long flags;
	bool is_found = 0;

	spin_lock_irqsave(&inst->srcbuf_lock, flags);
	if (list_empty(&inst->srcbuf_list)) {
		spin_unlock_irqrestore(&inst->srcbuf_lock, flags);
		goto exit;
	}

	list_for_each_entry (b, &inst->srcbuf_list, list) {
		if (b && ((check_consume && b->consumed == false) ||
			  (!check_consume))) {
			is_found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&inst->srcbuf_lock, flags);
exit:
	return (is_found ? b : NULL);
}

static struct vb2_v4l2_buffer *
rtkve_enc_remove_srcbuf(struct vpu_instance *inst, struct vpu_buffer *buf)
{
	struct vpu_buffer *b = NULL;
	struct vpu_buffer *b_tmp = NULL;
	unsigned long flags;
	bool is_found = 0;

	spin_lock_irqsave(&inst->srcbuf_lock, flags);
	list_for_each_entry_safe (b, b_tmp, &inst->srcbuf_list, list) {
		if (buf == b) {
			b->consumed = false;
			list_del(&b->list);
			is_found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&inst->srcbuf_lock, flags);

	return (is_found ? &b->v4l2_m2m_buf.vb : NULL);
}

static void rtkve_enc_return_srcbuf(struct vpu_instance *inst)
{
	struct vpu_buffer *vpu_src = rtkve_enc_next_srcbuf(inst, false);

	while (vpu_src) {
		rtkve_enc_remove_srcbuf(inst, vpu_src);
		vpu_src = rtkve_enc_next_srcbuf(inst, false);
	}
}

static void rtkve_enc_buf_queue_dst(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_buffer *dst_buf = rtkve_to_vpu_buf(vbuf);
	struct vpu_instance *inst = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long flags;

	dev_dbg(inst->dev->dev,
		"type %4d index %4d size[0] %4ld size[1] : %4ld | size[2] : %4ld\n",
		vb->type, vb->index, vb2_plane_size(&vbuf->vb2_buf, 0),
		vb2_plane_size(&vbuf->vb2_buf, 1),
		vb2_plane_size(&vbuf->vb2_buf, 2));

	vbuf->sequence = inst->queued_dst_buf_num++;
	v4l2_m2m_buf_queue(inst->v4l2_fh.m2m_ctx, vbuf);
	spin_lock_irqsave(&inst->dstbuf_lock, flags);
	list_add_tail(&dst_buf->list, &inst->dstbuf_list);
	spin_unlock_irqrestore(&inst->dstbuf_lock, flags);
}

static void rtkve_enc_return_buffer(struct vb2_queue *vq, u32 state)
{
	struct vpu_instance *inst = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf;

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(inst->v4l2_fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(inst->v4l2_fh.m2m_ctx);

		if (!vbuf)
			break;

		if (vbuf->vb2_buf.req_obj.req)
			v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req,
						   &inst->v4l2_ctrl_hdl);

		v4l2_m2m_buf_done(vbuf, state);
		dev_dbg(inst->dev->dev, "Marked request %px as complete\n",
			vbuf->vb2_buf.req_obj.req);
	}
}

static int rtkve_enc_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
				 unsigned int *num_planes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_pix_format_mplane inst_format =
		(V4L2_TYPE_IS_OUTPUT(q->type)) ? inst->src_fmt : inst->dst_fmt;
	unsigned int i;
	int ret = 0;

	dev_dbg(inst->dev->dev, "%s: num_buffers %d num_planes %d type %d\n",
		__func__, *num_buffers, *num_planes, q->type);

	if (*num_planes) {
		if (inst_format.num_planes != *num_planes) {
			ret = -EINVAL;
			goto exit;
		}

		for (i = 0; i < *num_planes; i++) {
			if (sizes[i] < inst_format.plane_fmt[i].sizeimage) {
				ret = -EINVAL;
				goto exit;
			}
		}
	} else {
		*num_planes = inst_format.num_planes;
		for (i = 0; i < *num_planes; i++) {
			sizes[i] = inst_format.plane_fmt[i].sizeimage;
			dev_dbg(inst->dev->dev, "size[%d] : %d\n", i, sizes[i]);
		}
	}
exit:
	return ret;
}

static int rtkve_enc_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static int rtkve_enc_buf_prepare(struct vb2_buffer *vb)
{
	struct vpu_instance *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format_mplane *fmt;
	int i;

	if (V4L2_TYPE_IS_OUTPUT(vb->type))
		fmt = &inst->src_fmt;
	else
		fmt = &inst->dst_fmt;

	for (i = 0; i < fmt->num_planes; i++) {
		if (vb2_plane_size(vb, i) < fmt->plane_fmt[i].sizeimage) {
			dev_err(inst->dev->dev,
				"data will not fit into plane %d (%lu < %d)", i,
				vb2_plane_size(vb, i),
				fmt->plane_fmt[i].sizeimage);
			return -EINVAL;
		}
	}

	return 0;
}

static void rtkve_enc_buf_queue(struct vb2_buffer *vb)
{
	if (V4L2_TYPE_IS_OUTPUT(vb->type))
		rtkve_enc_buf_queue_src(vb);
	else
		rtkve_enc_buf_queue_dst(vb);
}

static int rtkve_enc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	int ret = 0;

	dev_dbg(inst->dev->dev, "%s: type %s\n", __func__,
		v4l2_type_names[q->type]);

	return ret;
}

static void rtkve_enc_stop_streaming(struct vb2_queue *q)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;

	dev_dbg(inst->dev->dev, "%s: type %s\n", __func__,
		v4l2_type_names[q->type]);

	v4l2_m2m_suspend(inst->dev->m2m_dev);

	rtkve_enc_stop(inst, q->type);

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		if (inst->input_thread) {
			kthread_stop(inst->input_thread);
			wake_up_interruptible(&inst->input_waitq);
			inst->input_thread = NULL;
		}
		rtkve_enc_return_srcbuf(inst);
	} else {
		rtkve_enc_return_dstbuf(inst);
	}

	rtkve_enc_return_buffer(q, VB2_BUF_STATE_ERROR);

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		inst->queued_src_buf_num = 0;
	} else {
		if (v4l2_m2m_has_stopped(m2m_ctx))
			v4l2_m2m_clear_state(m2m_ctx);
		inst->state = VPU_INST_STATE_OPEN;

		inst->queued_dst_buf_num = 0;
	}
	v4l2_m2m_resume(inst->dev->m2m_dev);
}

static const struct vb2_ops rtkve_enc_vb2_ops = {
	.queue_setup = rtkve_enc_queue_setup,
	.buf_out_validate = rtkve_enc_buf_out_validate,
	.buf_prepare = rtkve_enc_buf_prepare,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_queue = rtkve_enc_buf_queue,
	.start_streaming = rtkve_enc_start_streaming,
	.stop_streaming = rtkve_enc_stop_streaming,
};

static int rtkve_enc_queue_init(void *priv, struct vb2_queue *src_vq,
				struct vb2_queue *dst_vq)
{
	struct vpu_instance *inst = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->ops = &rtkve_enc_vb2_ops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->buf_struct_size = sizeof(struct vpu_buffer);
	src_vq->drv_priv = inst;
	src_vq->lock = &inst->dev->dev_lock;
	src_vq->dev = inst->dev->v4l2_dev.dev;
	src_vq->supports_requests = true;
	ret = vb2_queue_init(src_vq);
	if (ret)
		goto exit;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->ops = &rtkve_enc_vb2_ops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->buf_struct_size = sizeof(struct vpu_buffer);
	dst_vq->min_buffers_needed = 1;
	dst_vq->drv_priv = inst;
	dst_vq->lock = &inst->dev->dev_lock;
	dst_vq->dev = inst->dev->v4l2_dev.dev;
	ret = vb2_queue_init(dst_vq);
	if (ret)
		goto exit;

exit:
	return ret;
}

static void rtkve_enc_handle_src_buf(struct vpu_instance *inst)
{
	struct vb2_v4l2_buffer *src_buf;
	struct vpu_buffer *vpu_buf;

	src_buf = v4l2_m2m_next_src_buf(inst->v4l2_fh.m2m_ctx);
	if (!src_buf) {
		dev_info(inst->dev->dev, "not found src buffer \n");
		goto exit;
	}

	vpu_buf = rtkve_to_vpu_buf(src_buf);
	if (!vpu_buf)
		goto exit;

	rtkve_enc_remove_srcbuf(inst, vpu_buf);
	src_buf = v4l2_m2m_src_buf_remove(inst->v4l2_fh.m2m_ctx);
	v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);

exit:
	return;
}

static struct vpu_buffer *rtkve_enc_next_dstbuf(struct vpu_instance *inst,
						bool check_consume)
{
	struct vpu_buffer *b = NULL;
	unsigned long flags;
	bool is_found = 0;

	spin_lock_irqsave(&inst->dstbuf_lock, flags);
	if (list_empty(&inst->dstbuf_list)) {
		spin_unlock_irqrestore(&inst->dstbuf_lock, flags);
		goto exit;
	}

	list_for_each_entry (b, &inst->dstbuf_list, list) {
		if (b && ((check_consume && b->consumed == false) ||
			  (!check_consume))) {
			is_found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&inst->dstbuf_lock, flags);
exit:
	return (is_found ? b : NULL);
}

static struct vb2_v4l2_buffer *
rtkve_enc_remove_dstbuf(struct vpu_instance *inst, struct vpu_buffer *buf)
{
	struct vpu_buffer *b = NULL;
	struct vpu_buffer *b_tmp = NULL;
	unsigned long flags;
	bool is_found = 0;

	spin_lock_irqsave(&inst->dstbuf_lock, flags);
	list_for_each_entry_safe (b, b_tmp, &inst->dstbuf_list, list) {
		if (buf == b) {
			b->consumed = false;
			list_del(&b->list);
			is_found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&inst->dstbuf_lock, flags);

	return (is_found ? &b->v4l2_m2m_buf.vb : NULL);
}

static void rtkve_enc_return_dstbuf(struct vpu_instance *inst)
{
	struct vpu_buffer *vpu_dst = rtkve_enc_next_dstbuf(inst, false);

	while (vpu_dst) {
		rtkve_enc_remove_dstbuf(inst, vpu_dst);
		vpu_dst = rtkve_enc_next_dstbuf(inst, false);
	}
}

static int rtkve_enc_fill_dst(struct vpu_handler *hndl,
			      struct vb2_v4l2_buffer *dst, int size)
{
	struct rtkve_ringbuf_t *prb;
	uint32_t wp, rp;
	uint8_t *rptr, *next, *addr_end;
	void *dst_buf;
	int ret = 0;

	prb = &hndl->stream_rb;
	dst_buf = vb2_plane_vaddr(&dst->vb2_buf, 0);
	if (!dst_buf)
		pr_err("%s dst_buf is NULL", __func__);

	mutex_lock(&prb->lock);
	wp = htonl(prb->pRBH->writePtr);
	rp = htonl(prb->pRBH->readPtr[0]);

	if (rp > wp && (int)(rp - wp - 1) < size) {
		mutex_unlock(&prb->lock);
		ret = -EPERM;
		goto exit;
	}

	if (wp > rp && (int)(rp + prb->size - wp - 1) < size) {
		mutex_unlock(&prb->lock);
		ret = -EPERM;
		goto exit;
	}

	rptr = prb->virtaddr + (rp - prb->phyaddr);
	addr_end = prb->virtaddr + prb->size;

	next = rptr + size;

	if (next >= addr_end) {
		int size0 = addr_end - rptr;
		int size1 = size - size0;

		memcpy(dst_buf, rptr, size0);
		memcpy(dst_buf + size0, prb->virtaddr, size1);

		next -= prb->size;
	} else {
		memcpy(dst_buf, rptr, size);
	}
	prb->pRBH->readPtr[0] = htonl(prb->phyaddr + (next - prb->virtaddr));
	mutex_unlock(&prb->lock);
exit:
	return ret;
}

static int rtkve_enc_check_wrptr(struct rtkve_ringbuf_t *prb)
{
	int ret = 0;

	mutex_lock(&prb->lock);
	if (prb->pRBH->readPtr[0] != prb->pRBH->writePtr)
		ret = 1;
	mutex_unlock(&prb->lock);

	return ret;
}

static int rtkve_enc_get_frm(struct vpu_instance *inst,
			     struct vpu_handler *hndl,
			     struct vb2_v4l2_buffer *dst_buf,
			     struct enc_output_info *enc_info)
{
	struct rtkve_ringbuf_t *prb;
	struct VIDEO_RPC_ENC_ELEM_FRAME_INFO *frm_info;
	int ret = 0;

	prb = &hndl->mesg_rb;

	if (prb->pRBH) {
		int val = 0;
		val = wait_event_interruptible_timeout(
			inst->output_waitq,
			kthread_should_stop() || rtkve_enc_check_wrptr(prb),
			msecs_to_jiffies(10));
		if (!val) {
			ret = -EAGAIN;
			goto exit;
		} else {
			uint32_t next_rp;

			mutex_lock(&prb->lock);
			frm_info = (struct VIDEO_RPC_ENC_ELEM_FRAME_INFO
					    *)(prb->virtaddr +
					       (htonl(prb->pRBH->readPtr[0]) -
						htonl(prb->pRBH->beginAddr)));
			if (htonl(frm_info->infoType) !=
				    VIDEOENCODER_VideoFrameInfo ||
			    frm_info->frameSize == 0) {
				pr_err("incorrect info type %d, size %d\n",
				       htonl(frm_info->infoType),
				       htonl(frm_info->frameSize));
			}

			enc_info->frm_size = htonl(frm_info->frameSize);
			enc_info->timestamp =
				(((uint64_t)htonl(frm_info->PTShigh)) << 32) |
				htonl(frm_info->PTSlow);
			enc_info->keyfrm = htonl(frm_info->KeyFrame);
			enc_info->picture_num = htonl(frm_info->pictureNumber);

			rtkve_enc_fill_dst(hndl, dst_buf, enc_info->frm_size);

			next_rp = htonl(prb->pRBH->readPtr[0]) +
				  sizeof(struct VIDEO_RPC_ENC_ELEM_FRAME_INFO);
			if (next_rp < prb->limit)
				prb->pRBH->readPtr[0] = htonl(next_rp);
			else
				prb->pRBH->readPtr[0] = prb->pRBH->beginAddr;
			dsb(sy);
			mutex_unlock(&prb->lock);
		}
	} else {
		pr_err("wrong ringbuffer header\n");
	}
exit:
	return ret;
}

static void rtkve_enc_handle_dst_buf(struct vpu_instance *inst,
				     struct vb2_v4l2_buffer *dst_buf,
				     struct enc_output_info enc_info)
{
	struct vpu_buffer *vpu_dst;

	vb2_set_plane_payload(&dst_buf->vb2_buf, 0, enc_info.frm_size);

	vpu_dst = rtkve_to_vpu_buf(dst_buf);
	rtkve_enc_remove_dstbuf(inst, vpu_dst);

	v4l2_m2m_dst_buf_remove_by_buf(inst->v4l2_fh.m2m_ctx, dst_buf);
	v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);

	return;
}

static int rtkve_enc_prepare_raw(struct vpu_instance *inst,
				 struct vb2_v4l2_buffer *src_buf,
				 struct enc_param *pic_param)
{
	u64 timestamp = 0;
	dma_addr_t luma_addr = 0, chroma_addr = 0;
	uint32_t luma_size = 0, chroma_size = 0;
	int ret = 0;

	dev_dbg(inst->dev->dev, "%d.%s.enter\n", __LINE__, __func__);

	if (!src_buf) {
		ret = -EINVAL;
		dev_err(inst->dev->dev, "%s, src_buf is NULL !!", __func__);
		goto exit;
	}

	if (inst->src_fmt.num_planes == 1) {
		luma_addr =
			vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0) +
			src_buf->planes[0].data_offset;
		luma_size = vb2_get_plane_payload(&src_buf->vb2_buf, 0) * 2 / 3;
		chroma_addr = luma_addr + luma_size;
		chroma_size = vb2_get_plane_payload(&src_buf->vb2_buf, 0) / 3;
	} else if (inst->src_fmt.num_planes == 2) {
		luma_addr =
			vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0) +
			src_buf->planes[0].data_offset;
		luma_size = vb2_get_plane_payload(&src_buf->vb2_buf, 0);
		chroma_addr =
			vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 1) +
			src_buf->planes[1].data_offset;
		;
		chroma_size = vb2_get_plane_payload(&src_buf->vb2_buf, 1);
	} else {
		dev_err(inst->dev->dev,
			"%s, don't support more than 2 planes !!", __func__);
	}

	timestamp = src_buf->vb2_buf.timestamp / 100000;

	ret = rtkve_inband_raw_input(inst->enc_hdl, (uint32_t)luma_addr,
				     luma_size, (uint32_t)chroma_addr,
				     chroma_size, timestamp);
	if (ret) {
		dev_err(inst->dev->dev, "%s, inband_raw_input fail", __func__);
	}

exit:
	return ret;
}

static int rtkve_enc_prepare_input(struct vpu_instance *inst)
{
	struct vpu_buffer *vpu_src, *vpu_dst;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct enc_param pic_param;
	unsigned long flags;
	int ret = 0;

	vpu_src = rtkve_enc_next_srcbuf(inst, true);
	if (!vpu_src) {
		ret = -EAGAIN;
		goto exit;
	}

	vpu_dst = rtkve_enc_next_dstbuf(inst, true);
	if (!vpu_dst) {
		ret = -EAGAIN;
		goto exit;
	}

	dst_buf = &vpu_dst->v4l2_m2m_buf.vb;
	src_buf = &vpu_src->v4l2_m2m_buf.vb;

	ret = rtkve_enc_prepare_raw(inst, src_buf, &pic_param);
	if (ret < 0)
		goto exit;

	inst->feed_cnt++;

	if (inst->eos && inst->feed_cnt == inst->queued_src_buf_num)
		rtkve_inband_set_eos(inst->enc_hdl);

	spin_lock_irqsave(&inst->srcbuf_lock, flags);
	vpu_src->consumed = true;
	spin_unlock_irqrestore(&inst->srcbuf_lock, flags);

	spin_lock_irqsave(&inst->dstbuf_lock, flags);
	vpu_dst->consumed = true;
	spin_unlock_irqrestore(&inst->dstbuf_lock, flags);
	wake_up_interruptible(&inst->output_waitq);
exit:
	return ret;
}

static int rtkve_enc_has_resource(struct vpu_instance *inst)
{
	struct vpu_buffer *vpu_src = NULL;
	struct vpu_buffer *vpu_dst = NULL;
	int ret = 0;

	vpu_src = rtkve_enc_next_srcbuf(inst, true);
	if (!vpu_src)
		goto exit;

	vpu_dst = rtkve_enc_next_dstbuf(inst, true);
	if (!vpu_dst)
		goto exit;

	ret = 1;
exit:
	return ret;
}

static int input_thread(void *data)
{
	struct v4l2_fh *fh = (struct v4l2_fh *)data;
	struct vpu_instance *inst = fh_to_inst(fh);
	int ret = 0;

	while (1) {
		ret = wait_event_interruptible_timeout(
			inst->input_waitq,
			kthread_should_stop() || rtkve_enc_has_resource(inst),
			msecs_to_jiffies(10));

		if (kthread_should_stop() || (ret == -ERESTART)) {
			ret = 1;
			break;
		} else if (ret == 0) {
			continue;
		}

		ret = rtkve_enc_prepare_input(inst);
		if (ret != 0)
			continue;
	}
	return ret;
}

static void rtkve_rpc_calc_res(struct vb2_v4l2_buffer *src_buf,
				uint32_t width, uint32_t hieght,
				uint32_t *align_width, uint32_t *align_height)
{
	unsigned long size = vb2_get_plane_payload(&src_buf->vb2_buf, 0);
	int align[3] = {32, 64, 128};
	int i = 0;

	for(i = 0; i < 3; i++) {
		*align_width = ALIGN(width, align[i]);
		*align_height = ALIGN(hieght, align[i]);

		if((*align_width) * (*align_height) * 3 /2 == size)
			return;
	}

	*align_width = width;
	*align_height = hieght;
}

static int rtkve_enc_start(struct vpu_instance *inst,
				struct vb2_v4l2_buffer *src_buf)
{
	int ret = 0;
	enum YUV_FMT src_fmt;
	enum VIDEO_STREAM_TYPE dst_fmt;
	uint32_t width = 0;
	uint32_t height = 0;

	switch (inst->src_fmt.pixelformat) {
	case V4L2_PIX_FMT_NV12:
	default:
		src_fmt = F_YUV420_Semi;
		break;
	}

	ret = rtkve_rpc_set_srcfmt(inst->enc_hdl, src_fmt);
	if (ret) {
		dev_err(inst->dev->dev, "set src fmt fail\n");
		goto exit;
	}

	switch (inst->dst_fmt.pixelformat) {
	case V4L2_PIX_FMT_HEVC:
	default:
		dst_fmt = VIDEO_STREAM_H265;
		break;
	}

	ret = rtkve_rpc_set_encfmt(inst->enc_hdl, dst_fmt);
	if (ret) {
		dev_err(inst->dev->dev, "set encode fmt fail\n");
		goto exit;
	}

	rtkve_rpc_calc_res(src_buf,
				    inst->dst_fmt.width, inst->dst_fmt.height,
				    &width, &height);

	ret = rtkve_rpc_set_resolution(inst->enc_hdl, width, height,
				    inst->dst_fmt.width, inst->dst_fmt.height);
	if (ret) {
		dev_err(inst->dev->dev, "set resolution fail\n");
		goto exit;
	}

	ret = rtkve_rpc_set_frmrate(inst->enc_hdl, inst->enc_params.framerate);
	if (ret) {
		dev_err(inst->dev->dev, "set frame rate fail\n");
		goto exit;
	}

	ret = rtkve_rpc_set_bitrate(inst->enc_hdl,
				    inst->enc_params.bitrate_mode,
				    inst->enc_params.bitrate);
	if (ret) {
		dev_err(inst->dev->dev, "set bit rate fail\n");
		goto exit;
	}

	ret = rtkve_rpc_set_GOPStruct(inst->enc_hdl, inst->enc_params.gop_size,
				      inst->enc_params.gop_size);
	if (ret) {
		dev_err(inst->dev->dev, "set GOP fail\n");
		goto exit;
	}

	ret = rtkve_inband_set_ref_buffer(inst->enc_hdl);
	if (ret) {
		dev_err(inst->dev->dev, "set reference buffer fail\n");
		goto exit;
	}

	ret = rtkve_rpc_start_record(inst->enc_hdl);
	if (ret) {
		dev_err(inst->dev->dev, "start record fail\n");
		goto exit;
	}

	ret = rtkve_rpc_pause(inst->enc_hdl);
	if (ret) {
		dev_err(inst->dev->dev, "%s pause fail\n", __func__);
		goto exit;
	}

	ret = rtkve_rpc_run(inst->enc_hdl);
	if (ret) {
		dev_err(inst->dev->dev, "%s pause fail\n", __func__);
		goto exit;
	}
	inst->enc_hdl->is_running = true;

	inst->input_thread =
		kthread_run(input_thread, &inst->v4l2_fh, "inputhread");

exit:
	return ret;
}

static int rtkve_enc_reset_ring(struct vpu_handler *hndl,
				enum RINGBUFFER_TYPE type)
{
	struct rtkve_ringbuf_t *prb;
	int ret = 0;

	if (!hndl) {
		pr_err("dec isn't ready\n");
		ret = -EPERM;
		goto exit;
	}

	if (type == RINGBUFFER_STREAM) {
		prb = &hndl->stream_rb;
	} else if (type == RINGBUFFER_COMMAND) {
		prb = &hndl->inband_rb;
	} else if (type == RINGBUFFER_MESSAGE) {
		prb = &hndl->mesg_rb;
	} else {
		pr_err("%s ringbuffer type %d is incorrect\n", __func__, type);
		goto exit;
	}

	mutex_lock(&prb->lock);

	if (prb->pRBH)
		prb->pRBH->readPtr[0] = prb->pRBH->writePtr;

	dsb(sy);
	mutex_unlock(&prb->lock);
exit:
	return ret;
}

static int rtkve_enc_stop(struct vpu_instance *inst, unsigned int type)
{
	int ret = 0;

	if (!inst || !inst->enc_hdl) {
		ret = -EINVAL;
		goto exit;
	}

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		if (inst->enc_hdl->is_running) {
			ret = rtkve_rpc_pause(inst->enc_hdl);
			if (ret) {
				dev_err(inst->dev->dev,
					"rtkve_rpc_pause fail\n");
				ret = -EPERM;
				goto exit;
			}
		}
	} else {
		if (inst->enc_hdl->is_running) {
			ret = rtkve_rpc_pause(inst->enc_hdl);
			if (ret) {
				dev_err(inst->dev->dev,
					"rtkve_rpc_pause fail\n");
				ret = -EPERM;
				goto exit;
			}
		}

		if (inst->enc_hdl->is_running) {
			ret = rtkve_rpc_pause(inst->enc_hdl);
			if (ret) {
				dev_err(inst->dev->dev,
					"rtkve_rpc_pause fail\n");
				ret = -EPERM;
				goto exit;
			}

			ret = rtkve_enc_reset_ring(inst->enc_hdl,
						   RINGBUFFER_MESSAGE);
			if (ret) {
				dev_err(inst->dev->dev,
					"flash reset message rwptr fail\n");
				goto exit;
			}

			ret = rtkve_enc_reset_ring(inst->enc_hdl,
						   RINGBUFFER_STREAM);
			if (ret) {
				dev_err(inst->dev->dev,
					"flash reset stream rwptr fail\n");
				goto exit;
			}
		}
	}

exit:
	return ret;
}

static void rtkve_enc_start_encode(struct work_struct *work)
{
	struct vpu_instance *inst =
		container_of(work, struct vpu_instance, encode_work);
	struct vpu_handler *hndl = inst->enc_hdl;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct enc_output_info enc_info = { 0 };
	int ret = 0;

	dev_dbg(inst->dev->dev, "%d.%s.enter\n", __LINE__, __func__);

	if (inst->eos && inst->feed_cnt == inst->queued_src_buf_num) {
		rtkve_inband_set_eos(inst->enc_hdl);
		inst->eos = false;
	}

	src_buf = v4l2_m2m_next_src_buf(inst->v4l2_fh.m2m_ctx);
	if (!src_buf)
		goto exit;

	dst_buf = v4l2_m2m_next_dst_buf(inst->v4l2_fh.m2m_ctx);
	if (!dst_buf)
		goto exit;

	switch (inst->state) {
	case VPU_INST_STATE_NONE:
		dev_dbg(inst->dev->dev, "%d.%s.VPU_INST_STATE_NONE\n", __LINE__,
			__func__);
		fallthrough;
	case VPU_INST_STATE_OPEN:
		rtkve_enc_start(inst, src_buf);
		inst->state = VPU_INST_STATE_INIT_SEQ;
		fallthrough;
	case VPU_INST_STATE_INIT_SEQ:
		inst->state = VPU_INST_STATE_PIC_RUN;
		fallthrough;
	case VPU_INST_STATE_PIC_RUN:
		ret = rtkve_enc_get_frm(inst, hndl, dst_buf, &enc_info);
		if (ret)
			break;

		dev_dbg(inst->dev->dev, "get frame %d !!!!", enc_info.frm_size);
		v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, false);
		if (enc_info.keyfrm)
			dst_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;
		else
			dst_buf->flags |= V4L2_BUF_FLAG_PFRAME;
		dst_buf->flags |= src_buf->flags & V4L2_BUF_FLAG_LAST;

		rtkve_enc_handle_dst_buf(inst, dst_buf, enc_info);

		rtkve_enc_handle_src_buf(inst);
		break;
	default:
		break;
	}
exit:
	v4l2_m2m_job_finish(inst->dev->m2m_dev, inst->v4l2_fh.m2m_ctx);

	dev_dbg(inst->dev->dev, "%d.%s.leave.ret:%d\n", __LINE__, __func__,
		ret);
	return;
}

static void rtkve_enc_stop_decode(struct vpu_instance *inst)
{
	dev_dbg(inst->dev->dev, "%s: state %d\n", __func__, inst->state);

	inst->state = VPU_INST_STATE_STOP;
}

static int rtkve_enc_init_ctrls(struct vpu_instance *inst)
{
	struct v4l2_ctrl_handler *hdl = &inst->v4l2_ctrl_hdl;
	int ret = 0;

	v4l2_ctrl_handler_init(hdl, 7);

	v4l2_ctrl_new_std(hdl, &rtkve_enc_ctrl_ops,
			  V4L2_CID_MIN_BUFFERS_FOR_OUTPUT, 1, 32, 1,
			  DEF_OUTPUT_BUF);

	v4l2_ctrl_new_std(hdl, &rtkve_enc_ctrl_ops,
			  V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1,
			  DEF_CAPTURE_BUF);

	v4l2_ctrl_new_std(hdl, &rtkve_enc_ctrl_ops, V4L2_CID_MPEG_VIDEO_BITRATE,
			  MIN_BITRATE, MAX_BITRATE, 1, DEF_BITRATE);

	v4l2_ctrl_new_std(hdl, &rtkve_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_GOP_SIZE, 0, 65535, 1, 0);

	v4l2_ctrl_new_std(hdl, &rtkve_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 0, 0, 0, 0);

	v4l2_ctrl_new_std_menu(hdl, &rtkve_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR,
			       ~((1 << V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) |
				 (1 << V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)),
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);

	v4l2_ctrl_new_std_menu(
		hdl, &rtkve_enc_ctrl_ops, V4L2_CID_MPEG_VIDEO_HEADER_MODE,
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME, 0,
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);

	v4l2_ctrl_new_std_menu(hdl, &rtkve_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
			       V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10,
			       ~(1 << V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN),
			       V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN);

	v4l2_ctrl_new_std_menu(hdl, &rtkve_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_HEVC_LEVEL,
			       V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2,
			       ~((1 << V4L2_MPEG_VIDEO_HEVC_LEVEL_1) |
				 (1 << V4L2_MPEG_VIDEO_HEVC_LEVEL_2) |
				 (1 << V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1) |
				 (1 << V4L2_MPEG_VIDEO_HEVC_LEVEL_3) |
				 (1 << V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1) |
				 (1 << V4L2_MPEG_VIDEO_HEVC_LEVEL_4) |
				 (1 << V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1)),
			       V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1);

	if (hdl->error) {
		dev_err(inst->dev->dev,
			"Failed to initialize control handler\n");
		v4l2_ctrl_handler_free(hdl);
		ret = hdl->error;
		goto exit;
	}

	inst->v4l2_fh.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);
exit:
	return ret;
}

const struct rtkve_match_data rtkve3_data_stateful = {
	.ctrls_setup = rtkve_enc_init_ctrls,
	.dev_run_work = rtkve_enc_start_encode,
	.find_vpu_fmt = rtkve_enc_find_fmt,
	.find_vpu_fmt_by_idx = rtkve_enc_find_fmt_by_idx,
	.queue_init = rtkve_enc_queue_init,
	.create_instance = rtkve_enc_create_instance,
	.stop_decode = rtkve_enc_stop_decode,
	.destroy_instance = rtkve_enc_destroy_instance,
	.is_stateless = false,
};
