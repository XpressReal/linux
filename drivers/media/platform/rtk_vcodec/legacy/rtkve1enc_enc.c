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
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gcd.h>
#include <linux/genalloc.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/of.h>
#include <linux/ratelimit.h>
#include <linux/reset.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>

#include "rtkve1enc_common.h"
#include "rtkve1_regdefine.h"
#include "ve1_vdi.h"

//#define RTKVE1_DUMP_OUTBUF_EN
#if defined(RTKVE1_DUMP_OUTBUF_EN)
static int g_outbuf_dump_serial = 0;
#endif

//#define RTKVE1_DUMP_ENC_BS_EN
#if defined(RTKVE1_DUMP_ENC_BS_EN)
static int g_enc_bs_dump_serial = 0;
#endif

//#define VE1_MD5_SIZE 16
//static unsigned char md5[VE1_MD5_SIZE];

static const struct vpu_format rtkve1enc_fmt_list[2][3] = {
	[VPU_FMT_TYPE_CODEC] = {
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_H264,
			.max_width = H264_MAX_ENC_PIC_WIDTH,
			.min_width = H264_MIN_ENC_PIC_WIDTH,
			.max_height = H264_MAX_ENC_PIC_HEIGHT,
			.min_height = H264_MIN_ENC_PIC_HEIGHT,
			.num_planes = 1,
		},
	},
	[VPU_FMT_TYPE_RAW] = {
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV420,
			.max_width = RAW_MAX_ENC_PIC_WIDTH,
			.min_width = RAW_MIN_ENC_PIC_WIDTH,
			.max_height = RAW_MAX_ENC_PIC_HEIGHT,
			.min_height = RAW_MIN_ENC_PIC_HEIGHT,
			.num_planes = 1,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV12,
			.max_width = RAW_MAX_ENC_PIC_WIDTH,
			.min_width = RAW_MIN_ENC_PIC_WIDTH,
			.max_height = RAW_MAX_ENC_PIC_HEIGHT,
			.min_height = RAW_MIN_ENC_PIC_HEIGHT,
			.num_planes = 1,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV21,
			.max_width = RAW_MAX_ENC_PIC_WIDTH,
			.min_width = RAW_MIN_ENC_PIC_WIDTH,
			.max_height = RAW_MAX_ENC_PIC_HEIGHT,
			.min_height = RAW_MIN_ENC_PIC_HEIGHT,
			.num_planes = 1,
		},
	}
};

const char *rtk_ve1_enc_state_str[] = {
	[RTK_VE1_STATE_ENC_IDLE]		= "enc_idle",
	[RTK_VE1_STATE_ENC_SEQ_INIT]	= "seq_init",
	[RTK_VE1_STATE_ENC_REG_FBS]		= "reg_fbs",
	[RTK_VE1_STATE_ENC_HEADER]		= "enc_header",
	[RTK_VE1_STATE_ENC_PIC]			= "enc_pic",
	[RTK_VE1_STATE_ENC_SEQ_END]		= "seq_end",
	[RTK_VE1_STATE_ENC_TIMEOUT]		= "enc_timeout",
};

/* vb2_ops */
static int rtkve_enc_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
				 unsigned int *num_planes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct rtkve1enc_ctx *ctx = vb2_get_drv_priv(q);
	struct v4l2_pix_format_mplane inst_format =
		(V4L2_TYPE_IS_OUTPUT(q->type)) ? ctx->src_fmt : ctx->dst_fmt;
	unsigned int i;
	int ret = 0;

	dev_dbg(ctx->dev->dev, "%s: num_buffers %d num_planes %d type %d\n",
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
			dev_dbg(ctx->dev->dev, "size[%d] : %d\n", i, sizes[i]);
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
	struct rtkve1enc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_queue *vq = vb->vb2_queue;
	struct v4l2_pix_format_mplane *fmt;
	int i;

	dev_dbg(ctx->dev->dev, "%d.%s.ctx:0x%px.type:%s\n",
        __LINE__, __func__,
		ctx, v4l2_type_names[vq->type]);
	if (V4L2_TYPE_IS_OUTPUT(vb->type)) {
		//dev_dbg(ctx->dev->dev, "%d.%s.ctx:0x%px.type:%s.dma_addr:0x%llx\n",
		//	__LINE__, __func__,
		//	ctx, v4l2_type_names[vq->type],
		//	vb2_dma_contig_plane_dma_addr(vb, 0));
		fmt = &ctx->src_fmt;
	}
	else
		fmt = &ctx->dst_fmt;

	for (i = 0; i < fmt->num_planes; i++) {
		if (vb2_plane_size(vb, i) < fmt->plane_fmt[i].sizeimage) {
			dev_err(ctx->dev->dev,
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
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rtkve1enc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_queue *vq = vb->vb2_queue;

	if (V4L2_TYPE_IS_OUTPUT(vb->type))
		vbuf->sequence = ctx->queued_src_buf_num++;
	else
		vbuf->sequence = ctx->queued_dst_buf_num++;

	dev_dbg(ctx->dev->dev, "%d.%s.ctx:0x%px.type:%s.sequence:%d\n",
        __LINE__, __func__,
		ctx, v4l2_type_names[vq->type],
		vbuf->sequence);

	v4l2_m2m_buf_queue(ctx->v4l2_fh.m2m_ctx, vbuf);
}

static int rtkve_enc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct rtkve1enc_ctx *ctx = vb2_get_drv_priv(q);
	int ret = 0;

	dev_dbg(ctx->dev->dev, "%d.%s.ctx:0x%px.type:%s\n",
        __LINE__, __func__,
		ctx, v4l2_type_names[q->type]);

	return ret;
}

static void rtkve_enc_return_buffer(struct vb2_queue *vq, u32 state)
{
	struct rtkve1enc_ctx *ctx = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf;

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->v4l2_fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->v4l2_fh.m2m_ctx);

		if (!vbuf)
			break;

		dev_dbg(ctx->dev->dev, "%d.%s.type:%s.v4l2_m2m_buf_done.vbuf:0x%px\n",
			__LINE__, __func__,
			v4l2_type_names[vq->type],
			vbuf);
		v4l2_m2m_buf_done(vbuf, state);
	}
}

static void rtkve_enc_stop_streaming(struct vb2_queue *q)
{
	struct rtkve1enc_ctx *ctx = vb2_get_drv_priv(q);
	struct v4l2_m2m_ctx *m2m_ctx = ctx->v4l2_fh.m2m_ctx;

	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px.type:%s\n",
        __LINE__, __func__,
		ctx, v4l2_type_names[q->type]);

	v4l2_m2m_suspend(ctx->dev->m2m_dev);

	rtkve_enc_return_buffer(q, VB2_BUF_STATE_ERROR);

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		ctx->queued_src_buf_num = 0;
	} else {
		ctx->stopping = 1;
		queue_work(ctx->dev->encode_workqueue, &ctx->encode_work);
		flush_work(&ctx->encode_work);
		rtkve1_finalize(ctx->dev);

		if (v4l2_m2m_has_stopped(m2m_ctx))
			v4l2_m2m_clear_state(m2m_ctx);

		ctx->queued_dst_buf_num = 0;
	}

	v4l2_m2m_resume(ctx->dev->m2m_dev);
	dev_dbg(ctx->dev->dev, "%d.%s.[-] ctx:0x%px.type:%s\n",
        __LINE__, __func__,
		ctx, v4l2_type_names[q->type]);
}

static const struct vb2_ops rtkve1enc_vb2_ops = {
	.queue_setup = rtkve_enc_queue_setup,
	.buf_out_validate = rtkve_enc_buf_out_validate,
	.buf_prepare = rtkve_enc_buf_prepare,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_queue = rtkve_enc_buf_queue,
	.start_streaming = rtkve_enc_start_streaming,
	.stop_streaming = rtkve_enc_stop_streaming,
};

/* rtkve1_context_ops */
static const struct vpu_format *rtkve_enc_find_fmt(unsigned int v4l2_pix_fmt,
						enum vpu_fmt_type type)
{
	unsigned int index;
	const struct vpu_format *fmt = NULL;

	for (index = 0; index < ARRAY_SIZE(rtkve1enc_fmt_list[type]);
	     index++) {
		if (rtkve1enc_fmt_list[type][index].v4l2_pix_fmt ==
		    v4l2_pix_fmt)
			fmt = &rtkve1enc_fmt_list[type][index];
	}

	return fmt;
}

static const struct vpu_format *
rtkve_enc_find_fmt_by_idx(unsigned int idx, enum vpu_fmt_type type)
{
	const struct vpu_format *fmt = NULL;

	if (idx >= ARRAY_SIZE(rtkve1enc_fmt_list[type]))
		goto exit;

	if (!rtkve1enc_fmt_list[type][idx].v4l2_pix_fmt)
		goto exit;

	fmt = &rtkve1enc_fmt_list[type][idx];

exit:
	return fmt;
}

#define MAX_LEVEL_IDX 16
static const int g_anLevel[MAX_LEVEL_IDX] =
{
    10, 11, 11, 12, 13,
    //10, 16, 11, 12, 13,
    20, 21, 22,
    30, 31, 32,
    40, 41, 42,
    50, 51
};

static const int g_anLevelMaxMBPS[MAX_LEVEL_IDX] =
{
    1485,   1485,   3000,   6000, 11880,
    11880,  19800,  20250,
    40500,  108000, 216000,
    245760, 245760, 522240,
    589824, 983040
};

static const int g_anLevelMaxFS[MAX_LEVEL_IDX] =
{
    99,    99,   396, 396, 396,
    396,   792,  1620,
    1620,  3600, 5120,
    8192,  8192, 8704,
    22080, 36864
};

static const int g_anLevelMaxBR[MAX_LEVEL_IDX] =
{
    64,     64,   192,  384, 768,
    2000,   4000,  4000,
    10000,  14000, 20000,
    20000,  50000, 50000,
    135000, 240000
};

static const int g_anLevelMaxMbs[MAX_LEVEL_IDX] =
{
    28,   28,  56, 56, 56,
    56,   79, 113,
    113, 169, 202,
    256, 256, 263,
    420, 543
};

int rtkve1_h264_calc_level(int mb_num_x, int mb_num_y, int framerate, int bitrate)
{
    int mbps;
    int framerate_div, framerate_res;
    int mb_num_pic = (mb_num_x*mb_num_y);
    int LevelIdc = 0;
    int i, maxMbs;

    pr_info("[%d]%s.mb_num_pic:%d.mb_num_x:%d.mb_num_y:%d.framerate:%d.bitrate:%d\n",
        __LINE__, __func__,
        mb_num_pic, mb_num_x, mb_num_y, framerate, bitrate);

    framerate_div = (framerate >> 16) + 1;
    framerate_res  = framerate & 0xFFFF;
	framerate = framerate_res / framerate_div;
    pr_info("[%d]%s.framerate_div:%d.framerate_res:%d.framerate:%d\n",
        __LINE__, __func__,
        framerate_div, framerate_res, framerate);
    mbps = mb_num_pic*framerate;
    pr_info("[%d]%s.mbps:%d\n",
        __LINE__, __func__,
        mbps);

    for(i=0; i<MAX_LEVEL_IDX; i++)
    {
        maxMbs = g_anLevelMaxMbs[i];
        //if ( mbps <= g_anLevelMaxMBPS[i]
        //        && mb_num_pic <= g_anLevelMaxFS[i]
        //        && mb_num_x   <= maxMbs
        //        && mb_num_y   <= maxMbs
        //        && bitrate  <= g_anLevelMaxBR[i] )
        if ( mbps <= g_anLevelMaxMBPS[i]
                && mb_num_pic <= g_anLevelMaxFS[i]
                && mb_num_x   <= maxMbs
                && mb_num_y   <= maxMbs)
        {
            LevelIdc = g_anLevel[i];
            break;
        }
    }
    pr_info("[%d]%s.i:%d.LevelIdc:%d\n",
        __LINE__, __func__,
        i, LevelIdc);

    return LevelIdc;
}

static void dump_outbuf(struct rtkve1enc_ctx *ctx, uint8_t* buf,
					uint32_t offset, uint32_t len)
{
#if defined(RTKVE1_DUMP_OUTBUF_EN)
	int filp_open_flags;
	ssize_t bytes = 0;
	loff_t pos = 0;

	if (ctx == NULL) {
		return;
	}

	if ((buf != NULL) && (len != 0)) {
		if (ctx->outbuf_new_file == 1) {
			ctx->outbuf_new_file = 0;
			filp_open_flags = O_CREAT | O_WRONLY;
			memset(ctx->outbuf_file_name, 0, sizeof(unsigned char)*256);
			snprintf(ctx->outbuf_file_name, 256,
					"/mnt/outbuf_%d.yuv",
					g_outbuf_dump_serial);
			g_outbuf_dump_serial++;
			dev_dbg(ctx->dev->dev, "%d.%s.create new outbuf dump:%s\n",__LINE__,__func__,
					ctx->outbuf_file_name);
		} else {
			filp_open_flags = O_APPEND | O_WRONLY;
		}
		ctx->outbuf_fp =
			(void *)filp_open(ctx->outbuf_file_name, filp_open_flags, 0);
		if (IS_ERR((struct file *)ctx->outbuf_fp)) {
			dev_err(ctx->dev->dev, "%d.%s.filp_open %s fail\n",__LINE__,__func__,
					ctx->outbuf_file_name);
		} else {
			//dev_dbg(ctx->dev->dev, "%d.%s.filp_open %s ok\n",__LINE__,__func__,
			//		ctx->outbuf_file_name);
			bytes =
				kernel_write((struct file *)(ctx->outbuf_fp),
							(void *)(buf + offset), (size_t)len, &pos);
			//dev_dbg(ctx->dev->dev, "%d.%s.kernel_write bytes:%ld.pos:%lld\n",__LINE__,__func__,
			//		bytes, pos);
			filp_close((struct file *)(ctx->outbuf_fp), NULL);
			//dev_dbg(ctx->dev->dev, "%d.%s.filp_close %s\n",__LINE__,__func__,
			//		ctx->outbuf_file_name);
			ctx->outbuf_fp = NULL;
		}
	}
#endif
}

static void enc_dump_bs(struct rtkve1enc_ctx *ctx, uint8_t* buf,
				uint32_t len)
{
#if defined(RTKVE1_DUMP_ENC_BS_EN)
	int filp_open_flags;
	ssize_t bytes = 0;
	loff_t pos = 0;

	if (ctx == NULL) {
		return;
	}

	if ((buf != NULL) && (len != 0)) {
		if (ctx->enc_bs_new_file == 1) {
			ctx->enc_bs_new_file = 0;
			filp_open_flags = O_CREAT | O_WRONLY | O_TRUNC;
			memset(ctx->enc_bs_file_name, 0, sizeof(unsigned char)*256);
			snprintf(ctx->enc_bs_file_name, 256,
					"/mnt/encbs_%d.es",
					g_enc_bs_dump_serial);
			g_enc_bs_dump_serial++;
			dev_dbg(ctx->dev->dev, "%d.%s.create new outbuf dump:%s\n",__LINE__,__func__,
					ctx->enc_bs_file_name);
		} else {
			filp_open_flags = O_APPEND | O_WRONLY;
		}
		ctx->enc_bs_fp =
			(void *)filp_open(ctx->enc_bs_file_name, filp_open_flags, 0);
		if (IS_ERR((struct file *)ctx->enc_bs_fp)) {
			dev_err(ctx->dev->dev, "%d.%s.filp_open %s fail\n",__LINE__,__func__,
					ctx->enc_bs_file_name);
		} else {
			//dev_dbg(ctx->dev->dev, "%d.%s.filp_open %s ok\n",__LINE__,__func__,
			//		ctx->enc_bs_file_name);
			bytes =
				kernel_write((struct file *)(ctx->enc_bs_fp),
							(void *)buf, (size_t)len, &pos);
			//dev_dbg(ctx->dev->dev, "%d.%s.kernel_write bytes:%ld.pos:%lld\n",__LINE__,__func__,
			//		bytes, pos);
			filp_close((struct file *)(ctx->enc_bs_fp), NULL);
			//dev_dbg(ctx->dev->dev, "%d.%s.filp_close %s\n",__LINE__,__func__,
			//		ctx->enc_bs_file_name);
			ctx->enc_bs_fp = NULL;
		}
	}
#endif
}

static int enc_alloc_bsbuf_workbuf(struct rtkve1enc_ctx *ctx)
{
	struct videc_dev *dev = ctx->dev;
	int ret = 0;

	if (ctx->bitstream.size == 0) {
		ret = rtkve1_alloc_dma_memory(dev, &ctx->bitstream, BS_BUF_SIZE, "bsbuf",
				ctx->debugfs_entry);
		if (ret < 0) {
			return ret;
		}
		dev_dbg(dev->dev, "%d.%s.alloc_dma.name:bsbuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			ctx->bitstream.size,
			ctx->bitstream.paddr,
			ctx->bitstream.vaddr);
	}
	if (ctx->workbuf.size == 0) {
		ret = rtkve1_alloc_dma_memory(dev, &ctx->workbuf, WORK_BUF_SIZE, "workbuf",
				ctx->debugfs_entry);
		if (ret < 0) {
			return ret;
		}
		dev_dbg(dev->dev, "%d.%s.alloc_dma.name:workbuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			ctx->workbuf.size,
			ctx->workbuf.paddr,
			ctx->workbuf.vaddr);
	}

	return ret;
}

static int enc_alloc_frame_buffers(struct rtkve1enc_ctx *ctx)
{
	struct videc_dev *dev = ctx->dev;
	int ret = 0;
	int i = 0;
	char dbg_name[16];

	for (i=0; i < ctx->enc_min_fb_num; i++) {
		memset(dbg_name, 0, sizeof(dbg_name));
		snprintf(dbg_name, 16, "framebuf%d", i);
		ret = rtkve1_alloc_dma_memory(dev, &ctx->framebuf[i], ctx->src_buf_size, dbg_name,
				ctx->debugfs_entry);
		if (ret < 0) {
			return -ENOMEM;
		}
		dev_dbg(dev->dev, "%d.%s.alloc_dma.%d.name:%s.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			i, dbg_name, ctx->framebuf[i].size,
			ctx->framebuf[i].paddr,
			ctx->framebuf[i].vaddr);
	}

	return ret;
}

static void calc_src_buf_w_h(u32 src_buf_size,
	u32 width, u32 hieght,
	u32 *align_width, u32 *align_height)
{
	int align[4] = {16, 32, 64, 128};
	int i = 0;

	for (i = 0; i < 4; i++) {
		*align_width = ALIGN(width, align[i]);
		*align_height = ALIGN(hieght, align[i]);

		if ((*align_width) * (*align_height) * 3 /2 == src_buf_size)
			return;
	}

	*align_width = width;
	*align_height = hieght;
}

static int enc_seq_init(struct rtkve1enc_ctx *ctx)
{
	u32 bitstream_buf, bitstream_size;
	struct videc_dev *dev = ctx->dev;
	//struct rtkve1_dev_info *ve1_devinfo = (struct rtkve1_dev_info *)dev->ve1_devinfo;
	struct vb2_v4l2_buffer *src_buf = NULL;
	u32 cbcrInterleave = 0;
	u32 val;
	int ret = 0;
	int intr_reason = 0;
	//u32 transform_8x8_mode = 1;
	u32 chroma_format_400 = 0;
	//u32 entropy_coding_mode = 1; /* 1 means CABAC */
	//u32 field_flag = 0;
	u32 streamEndflag = 0;
	u32 rc_enable = 2;
	u32 idr_interval = 1;
	u32 slice_mode = 0;
	u32 slice_size_mode = 0;
	u32 slice_size = 0;
	u32 rc_gop_I_qp_offset_en = 0;
	u32 rc_gop_I_qp_offset = 0;
	u32 frame_skip_disable = 1;
	u32 initial_delay = 500;
	u32 aud_enable = 0;
	//u32 field_ref_mode = 0;
	u32 user_qp_max = 0;
	u32 user_gamma = 0;
	u32 mb_interval = 0;
	u32 rc_interval_mode = 0;
	u32 intra_cost_weight = 0;

	lockdep_assert_held(&dev->ve1_hw_mutex);

	src_buf = v4l2_m2m_next_src_buf(ctx->v4l2_fh.m2m_ctx);
	if (!src_buf) {
		dev_err(dev->dev, "%d.%s.v4l2_m2m_next_src_buf() fail\n", __LINE__, __func__);
		ret = -EINVAL;
		return ret;
	}

	if (ctx->dst_fmt.pixelformat == V4L2_PIX_FMT_H264) {
		ctx->codec_mode = AVC_ENC;
	}

	bitstream_buf = ctx->bitstream.paddr;
	bitstream_size = ctx->bitstream.size;

	dev_dbg(dev->dev, "%d.%s.s_ctrl_level_value:%d\n",
		__LINE__, __func__,
		ctx->enc_params.s_ctrl_level_value);
	if (ctx->enc_params.s_ctrl_level_value == 0) {
		ctx->enc_params.h264_level_idc = rtkve1_h264_calc_level(ctx->dst_fmt.width/16, ctx->dst_fmt.height/16, ctx->enc_params.framerate, ctx->enc_params.bitrate);
		dev_dbg(ctx->dev->dev, "%d.%s.s_ctrl_level_value:%d.h264_level_idc:%d\n",
			__LINE__, __func__,
			ctx->enc_params.s_ctrl_level_value,
			ctx->enc_params.h264_level_idc);
	}
	else {
		ctx->enc_params.h264_level_idc = g_anLevel[ctx->enc_params.s_ctrl_level_value];
		dev_dbg(ctx->dev->dev, "%d.%s.s_ctrl_level_value:%d.h264_level_idc:%d\n",
			__LINE__, __func__,
			ctx->enc_params.s_ctrl_level_value,
			ctx->enc_params.h264_level_idc);
	}

	//rtkve1_reg_writel(dev, BIT_PARA_BUF_ADDR, (u32)ve1_devinfo->parabuf.paddr);

	rtkve1_reg_writel(dev, CMD_ENC_SEQ_BB_START, bitstream_buf);
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_BB_SIZE, bitstream_size / 1024);

	ctx->src_buf_size = vb2_get_plane_payload(&src_buf->vb2_buf, 0);
	calc_src_buf_w_h(ctx->src_buf_size,
		ctx->src_fmt.width, ctx->src_fmt.height,
		&ctx->src_buf_width, &ctx->src_buf_height);
	dev_dbg(dev->dev, "%d.%s.src_fmt(w:%d.h:%d).src_buf(w:%d.h:%d).src_buf_size:%d\n",
		__LINE__, __func__,
		ctx->src_fmt.width, ctx->src_fmt.height,
		ctx->src_buf_width, ctx->src_buf_height,
		ctx->src_buf_size);
	val = (ctx->src_buf_width << 16) | ctx->src_buf_height;
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_SRC_SIZE, val);
	// frame rate
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_SRC_F_RATE, ctx->enc_params.framerate);
	/* profile, related to transform8x8Mode/chromaFormat400/entropyCodingMode/fieldFlag */
	if ((ctx->enc_params.transform_8x8_mode == 1) || (chroma_format_400 == 1)) {
		ctx->enc_params.h264_profile_idc = 2;
	}
	else if ((ctx->enc_params.entropy_coding_mode != 0) || (ctx->enc_params.field_flag == 1)) {
		ctx->enc_params.h264_profile_idc = 1;
	}
	else {
		ctx->enc_params.h264_profile_idc = 0;
	}
	val = ((ctx->enc_params.h264_profile_idc<<4) | 0x0);
	dev_dbg(dev->dev, "%d.%s.h264_profile_idc:%d(transform_8x8_mode:%d.entropy_coding_mode:%d)\n",
		__LINE__, __func__,
		ctx->enc_params.h264_profile_idc,
		ctx->enc_params.transform_8x8_mode,
		ctx->enc_params.entropy_coding_mode);
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_COD_STD, val);
	/* video Signal Type Present as 0 */
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_VIDEO_SIGNAL_TYPE_PRESENT, 0);
	// H264 para, related to deblkFilterOffsetBeta/deblkFilterOffsetAlpha/disableDeblk/constrainedIntraPredFlag/chromaQpOffset
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_264_PARA, 0);
	// ME, related to VPU_ME_LINEBUFFER_MODE/meBlkMode/MEUseZeroPmv/MESearchRangeY/MESearchRangeX
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_ME_OPTION, 0);
	// slice mode, related to sliceMode/sliceSize/sliceSizeMode
	val = 0;
	if (slice_mode != 0) {
		val = (slice_size << 2) | (slice_size_mode + 1);
	}
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_SLICE_MODE, val);

	if (rc_enable) {
		val = (idr_interval << 21) | (rc_gop_I_qp_offset_en<<20) | ((rc_gop_I_qp_offset & 0xF)<<16) | ctx->enc_params.gop_size;
		dev_dbg(dev->dev, "%d.%s.idr_interval:%d.gop_size:%d\n",
			__LINE__, __func__,
			idr_interval,
			ctx->enc_params.gop_size);
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_GOP_NUM, val);

		val = (frame_skip_disable << 31) | (initial_delay << 16) | 0;
		dev_dbg(dev->dev, "%d.%s.frame_skip_disable:%d.initial_delay:%d\n",
			__LINE__, __func__,
			frame_skip_disable,
			initial_delay);
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_PARA, val);

		if (rc_enable == 1) {
			/* CBR */
		}
		else {
			val = ((ctx->enc_params.bitrate/1024) << 4) | (rc_enable & 0xf);
			dev_dbg(dev->dev, "%d.%s.bitrate:%d.rc_enable:%d\n",
				__LINE__, __func__,
				ctx->enc_params.bitrate,
				rc_enable);
			rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_PARA2, val);
		}

		rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_MAX_INTRA_SIZE, 0);
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_QP_RANGE_SET, 0);
	}
	else {
		/* rate control - if rcEable == 0 begin */
		val = (idr_interval << 21) | ctx->enc_params.gop_size;
		dev_dbg(dev->dev, "%d.%s.idr_interval:%d.gop_size:%d\n",
			__LINE__, __func__,
			idr_interval,
			ctx->enc_params.gop_size);
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_GOP_NUM, val);
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_PARA, 0);
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_QP_RANGE_SET, 0);
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_PARA2, 0);
		/* rate control - if rcEable == 0 end */
	}

	// RC buffer size
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_BUF_SIZE, 0);
	// intra refresh, related to intraRefresh/ConscIntraRefreshEnable/CountIntraMbEnable/FieldSeqIntraRefreshEnable
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_INTRA_REFRESH, 0);

	// intra QP, rcIntraQp
	val = 0;
	dev_dbg(dev->dev, "%d.%s.intra_qp:%d\n",
		__LINE__, __func__,
		ctx->enc_params.intra_qp);
	if (ctx->enc_params.intra_qp >= 0) {
		val = (1 << 5);
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_INTRA_QP, ctx->enc_params.intra_qp);
	}
	else {
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_INTRA_QP, 0xffffffff);
	}
	// QP max, related to rcIntraQp/audEnable/fieldFlag/fieldRefMode/userQpMax
	val |= (aud_enable << 2);
	val |= (ctx->enc_params.field_flag << 10);
	val |= (ctx->enc_params.field_ref_mode << 11);
	dev_dbg(dev->dev, "%d.%s.aud_enable:%d.field_flag:%d.field_ref_mode:%d\n",
		__LINE__, __func__,
		aud_enable,
		ctx->enc_params.field_flag,
		ctx->enc_params.field_ref_mode);
	dev_dbg(dev->dev, "%d.%s.user_qp_max:%d.user_gamma:%d\n",
		__LINE__, __func__,
		user_qp_max,
		user_gamma);
	if (user_qp_max >= 0) {
		val |= (1 << 6);
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_QP_MAX, user_qp_max);
	}
	else {
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_QP_MAX, 0);
	}
	// gamma, userGamma
	if (user_gamma >= 0) {
		val |= (1 << 7);
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_GAMMA, user_gamma);
	}
	else {
		rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_GAMMA, 0);
	}
	// option, related to rcIntraQp/audEnable/fieldFlag/fieldRefMode/userQpMax/userGamma
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_OPTION, val);
	// interval mode, related to mbInterval/rcIntervalMode
	val = (mb_interval << 2) | rc_interval_mode;
	dev_dbg(dev->dev, "%d.%s.mb_interval:%d.rc_interval_mode:%d\n",
		__LINE__, __func__,
		mb_interval,
		rc_interval_mode);
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_RC_INTERVAL_MODE, val);
	// intra weight, intraCostWeight
	dev_dbg(dev->dev, "%d.%s.intra_cost_weight:%d\n",
		__LINE__, __func__,
		intra_cost_weight);
	rtkve1_reg_writel(dev, CMD_ENC_SEQ_INTRA_WEIGHT, intra_cost_weight);
	
	ctx->wrPtr = bitstream_buf;
	ctx->rdPtr = bitstream_buf;
	rtkve1_reg_writel(dev, BIT_WR_PTR, ctx->wrPtr);
	rtkve1_reg_writel(dev, BIT_RD_PTR, ctx->rdPtr);

	/* frame mem ctrl, related to bwbEnable/linear2TiledMode/mapType/chromaFormat400/cbcrInterleave/frameEndian */
	switch (ctx->src_fmt.pixelformat) {
		case V4L2_PIX_FMT_NV12:
			cbcrInterleave = 1;
			break;
		case V4L2_PIX_FMT_NV21:
			cbcrInterleave = 1;
			break;
		default:
			break;
	}
	val = (VE1_BWB_ENABLE<<15) | (cbcrInterleave<<2) | VPU_FRAME_ENDIAN;
	rtkve1_reg_writel(dev, BIT_FRAME_MEM_CTRL, val);
	/* stream ctrl, related to lineBufIntEn/streamEndian */
	rtkve1_reg_writel(dev, BIT_BIT_STREAM_CTRL, 0x30);

	/* issue command */
	rtkve1_issue_command(ctx, ENC_SEQ_INIT);

	/* wait interrupt */
	intr_reason = rtkve1_wait_interrupt(ctx, RTKVE1_ENC_TIMEOUT);
	if (intr_reason < 0) {
		dev_err(dev->dev, "%d.%s.rtkve1_wait_interrupt() fail\n",
			__LINE__, __func__);
		return intr_reason;
	}
	dev_dbg(dev->dev, "%d.%s.intr_reason:0x%x\n",
		__LINE__, __func__, intr_reason);
	rtkve1_clear_interrupt(ctx);

	// get result
	ret = rtkve1_reg_readl(dev, RET_ENC_SEQ_END_SUCCESS);
	dev_dbg(dev->dev, "%d.%s.r_reg RET_ENC_SEQ_END_SUCCESS(0x%X):0x%x\n", __LINE__, __func__,
		RET_ENC_SEQ_END_SUCCESS, ret);
	if (!ret) {
		dev_err(dev->dev, "%d.%s.RET_ENC_SEQ_END_SUCCESS fail \n",
			__LINE__, __func__);
		return ret;
	}
	if (ret & (1 << 31)) {
		dev_err(dev->dev, "%d.%s.memory access violation\n",
			__LINE__, __func__);
		return ret;
	}

	ctx->wrPtr = rtkve1_reg_readl(dev, BIT_WR_PTR);
	dev_dbg(dev->dev, "%d.%s.r_reg BIT_WR_PTR(0x%X):0x%x\n", __LINE__, __func__,
		BIT_WR_PTR, ctx->wrPtr);

	streamEndflag = rtkve1_reg_readl(dev, BIT_BIT_STREAM_PARAM);
	dev_dbg(dev->dev, "%d.%s.r_reg BIT_BIT_STREAM_PARAM(0x%X):0x%x\n", __LINE__, __func__,
		BIT_BIT_STREAM_PARAM, streamEndflag);

	ctx->enc_min_fb_num = 2; // reconstructed frame + reference frame
	dev_dbg(dev->dev, "%d.%s.enc_min_fb_num:%d\n", __LINE__, __func__, ctx->enc_min_fb_num);
	ctx->seq_init_done = 1;

	return ret;
}

static int enc_reg_fbs(struct rtkve1enc_ctx *ctx)
{
	int ret = 0;
	struct videc_dev *dev = ctx->dev;
	struct rtkve1_dev_info* ve1_devinfo = (struct rtkve1_dev_info*)ctx->dev->ve1_devinfo;
	int i = 0;
	u32 y=0, cb=0, cr=0;
	u32 ysize = 0;
	u32 cbcrInterleave = 0;
	u32 val;
	//unsigned char *p;
	u32 width_align_16 = 0;

	lockdep_assert_held(&dev->ve1_hw_mutex);

	// frame mem ctrl, related to bwbEnable/linear2TiledMode/mapType/chromaFormat400/cbcrInterleave/frameEndian
	switch (ctx->src_fmt.pixelformat) {
		case V4L2_PIX_FMT_NV12:
			cbcrInterleave = 1;
			break;
		case V4L2_PIX_FMT_NV21:
			cbcrInterleave = 1;
			break;
		default:
			break;
	}
	val = (VE1_BWB_ENABLE<<15) | (cbcrInterleave<<2) | VPU_FRAME_ENDIAN;
	rtkve1_reg_writel(dev, BIT_FRAME_MEM_CTRL, val);

#if 1
	// write frame buffer address (bufY/bufCb/bufCr) to param buffer
	ysize = ctx->src_buf_width * ctx->src_buf_height;
	for (i = 0; i < ctx->enc_min_fb_num; i++) {
		y = ctx->framebuf[i].paddr;
		cb = y + ysize;
		cr = y + ysize + ysize/4;
		if (cbcrInterleave) {
			cr = 0xffffffff;
		}
		dev_dbg(dev->dev, "%d.%s.%d.y:0x%x.cb:0x%x.cr:0x%x\n",
			__LINE__, __func__,
			i, y, cb, cr);
		ctx->frame_addr[i][0][0] = (y >> 24) & 0xFF;
		ctx->frame_addr[i][0][1] = (y >> 16) & 0xFF;
		ctx->frame_addr[i][0][2] = (y >> 8) & 0xFF;
		ctx->frame_addr[i][0][3] = (y >> 0) & 0xFF;

		ctx->frame_addr[i][1][0] = (cb >> 24) & 0xFF;
		ctx->frame_addr[i][1][1] = (cb >> 16) & 0xFF;
		ctx->frame_addr[i][1][2] = (cb >> 8) & 0xFF;
		ctx->frame_addr[i][1][3] = (cb >> 0) & 0xFF;

		ctx->frame_addr[i][2][0] = (cr >> 24) & 0xFF;
		ctx->frame_addr[i][2][1] = (cr >> 16) & 0xFF;
		ctx->frame_addr[i][2][2] = (cr >> 8) & 0xFF;
		ctx->frame_addr[i][2][3] = (cr >> 0) & 0xFF;
	}
	ret = rtkve1_write_memory(dev,
			ve1_devinfo->parabuf.vaddr, ve1_devinfo->parabuf.size,
			0,
			(u8 *)ctx->frame_addr,
			sizeof(ctx->frame_addr),
			VDI_BIG_ENDIAN);
	if (ret <= 0) {
		dev_err(dev->dev, "%d.%s.rtkve1_write_memory buf_y fail\n",
			__LINE__, __func__);
		return ret;
	}

	for (i = 0; i < ctx->enc_min_fb_num; i++) {
		ctx->frame_addr[i][0][0] = 0;
		ctx->frame_addr[i][0][1] = 0;
		ctx->frame_addr[i][0][2] = 0;
		ctx->frame_addr[i][0][3] = 0;

		ctx->frame_addr[i][1][0] = 0;
		ctx->frame_addr[i][1][1] = 0;
		ctx->frame_addr[i][1][2] = 0;
		ctx->frame_addr[i][1][3] = 0;

		ctx->frame_addr[i][2][0] = 0;
		ctx->frame_addr[i][2][1] = 0;
		ctx->frame_addr[i][2][2] = 0;
		ctx->frame_addr[i][2][3] = 0;
	}
	ret = rtkve1_write_memory(dev,
		ve1_devinfo->parabuf.vaddr, ve1_devinfo->parabuf.size,
		384 + 128,
		(u8 *)ctx->frame_addr,
		sizeof(ctx->frame_addr),
		VDI_BIG_ENDIAN);
	if (ret <= 0) {
		dev_err(dev->dev, "%d.%s.rtkve1_write_memory buf_y_bot fail\n",
			__LINE__, __func__);
		return ret;
	}
#else
	// write frame buffer address (bufY/bufCb/bufCr) to param buffer
	ysize = ctx->src_buf_width * ctx->src_buf_height;
	for (i=0; i < ctx->enc_min_fb_num; i++) {
		y = ctx->framebuf[i].paddr;
		cb = y + ysize;
		cr = y + ysize + ysize/4;
		if (cbcrInterleave) {
			cr = 0xffffffff;
		}
		dev_dbg(dev->dev, "%d.%s.%d.y:0x%x.cb:0x%x.cr:0x%x\n",
			__LINE__, __func__,
			i, y, cb, cr);
		rtkve1_parabuf_write(ctx, i * 3 + 0, y);
		rtkve1_parabuf_write(ctx, i * 3 + 1, cb);
		rtkve1_parabuf_write(ctx, i * 3 + 2, cr);
	}
	//p = ve1_devinfo->parabuf.vaddr;
	for (i=0; i < ctx->enc_min_fb_num; i++) {
		p = (unsigned char *)ve1_devinfo->parabuf.vaddr + i*12;
		dev_dbg(dev->dev, "%d.%s.%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			__LINE__, __func__,
			p[0], p[1], p[2], p[3], p[4], p[5],
			p[6], p[7], p[8], p[9], p[10], p[11]);
	}

	// write frame buffer address (bufYBot/bufCbBot/bufCrBot)(hardcode 0) to (param buffer + 384 + 128)
	for (i=0; i < ctx->enc_min_fb_num; i++) {
		rtkve1_parabuf_write(ctx, 128 + i * 3 + 0, 0); // bufYBot
		rtkve1_parabuf_write(ctx, 128 + i * 3 + 1, 0); // bufCbBot
		rtkve1_parabuf_write(ctx, 128 + i * 3 + 2, 0); // bufCrBot
	}
	for (i=0; i < ctx->enc_min_fb_num; i++) {
		p = (unsigned char *)ve1_devinfo->parabuf.vaddr + 384 + 128 + i*12;
		dev_dbg(dev->dev, "%d.%s.%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			__LINE__, __func__,
			p[0], p[1], p[2], p[3], p[4], p[5],
			p[6], p[7], p[8], p[9], p[10], p[11]);
	}
#endif

	// frame buffer number
	rtkve1_reg_writel(dev, CMD_SET_FRAME_BUF_NUM, ctx->enc_min_fb_num);
	// frame buffer stride, test prog set width_align_16(176) instead of width_align_32(192), need to check
	width_align_16 = ALIGN(ctx->dst_fmt.width, 16);
	dev_dbg(dev->dev, "%d.%s.width_align_16:%d.dst_fmt->width:%d\n", __LINE__, __func__,
		width_align_16, ctx->dst_fmt.width);
	rtkve1_reg_writel(dev, CMD_SET_FRAME_BUF_STRIDE, width_align_16);

	/* AXI */
	rtkve1_reg_writel(dev, CMD_SET_FRAME_AXI_BIT_ADDR, 0);
	rtkve1_reg_writel(dev, CMD_SET_FRAME_AXI_IPACDC_ADDR, 0);
	rtkve1_reg_writel(dev, CMD_SET_FRAME_AXI_DBKY_ADDR, 0);
	rtkve1_reg_writel(dev, CMD_SET_FRAME_AXI_DBKC_ADDR, 0);
	rtkve1_reg_writel(dev, CMD_SET_FRAME_AXI_OVL_ADDR, 0);

	// frame cache config, related to CacheMode
	rtkve1_reg_writel(dev, CMD_SET_FRAME_CACHE_CONFIG, 0x7e0);


	/* issue command */
	rtkve1_issue_command(ctx, SET_FRAME_BUF);

	/* wait busy flag */
	ret = rtkve1_wait_busy(dev, BIT_BUSY_FLAG);
	if (ret) {
		dev_err(dev->dev,
			"%d.%s.wait_timeout.BIT_BUSY_FLAG.ret:%d\n",
            __LINE__, __func__,
			ret);
		return -ETIMEDOUT;
	}

	// get result
	ret = rtkve1_reg_readl(dev, RET_SET_FRAME_SUCCESS);
	dev_dbg(dev->dev, "%d.%s.r_reg RET_SET_FRAME_SUCCESS(0x%X):0x%x\n",
		__LINE__, __func__,
		RET_SET_FRAME_SUCCESS, ret);
	if (!ret) {
		dev_err(dev->dev, "%d.%s.RET_SET_FRAME_SUCCESS fail \n",
			__LINE__, __func__);
		return -1;
	}
	if (ret & (1 << 31)) {
		dev_err(dev->dev, "%d.%s.memory access violation\n",
			__LINE__, __func__);
		return -1;
	}

	ctx->reg_fbs_done = 1;

	return ret;
}

static int enc_save_hdr(struct rtkve1enc_ctx *ctx, void *hdr_buf, int hdr_bytes, int append)
{
	struct videc_dev *dev = ctx->dev;
	void *tmp_buf = NULL;
	u32 hdr_buf_size = 0;

	if ((append == 0) && (ctx->enc_hdr_buf != NULL)) {
		vfree((void *)ctx->enc_hdr_buf);
		ctx->enc_hdr_buf = NULL;
		ctx->enc_hdr_buf_size = 0;
		ctx->enc_hdr_bytes = 0;
	}

	if (append == 0) {
		hdr_buf_size = (hdr_bytes<=RTKVE1_ENC_HEADER_TEMP_BUF_SIZE)?RTKVE1_ENC_HEADER_TEMP_BUF_SIZE:(hdr_bytes+RTKVE1_ENC_HEADER_TEMP_BUF_SIZE);
		tmp_buf = (unsigned char *)vmalloc(hdr_buf_size);
		if (!tmp_buf) {
			dev_err(dev->dev, "%d.%s.vmalloc() fail\n",
				__LINE__, __func__);
			return -ENOMEM;
		}
		memcpy(tmp_buf, hdr_buf, hdr_bytes);
		ctx->enc_hdr_buf = tmp_buf;
		ctx->enc_hdr_buf_size = hdr_buf_size;
		ctx->enc_hdr_bytes = hdr_bytes;
	}
	else if (append == 1)
	{
		if ((hdr_bytes + ctx->enc_hdr_bytes) > ctx->enc_hdr_buf_size) {
			hdr_buf_size = hdr_bytes + ctx->enc_hdr_bytes;
			tmp_buf = (unsigned char *)vmalloc(hdr_buf_size);
			if (!tmp_buf) {
				dev_err(dev->dev, "%d.%s.vmalloc() fail\n",
					__LINE__, __func__);
				return -ENOMEM;
			}
			memcpy(tmp_buf, ctx->enc_hdr_buf, ctx->enc_hdr_bytes);
			memcpy(((unsigned char *)tmp_buf+ctx->enc_hdr_bytes), hdr_buf, hdr_bytes);

			vfree((void *)ctx->enc_hdr_buf);
			ctx->enc_hdr_buf = tmp_buf;
			ctx->enc_hdr_buf_size = hdr_buf_size;
			ctx->enc_hdr_bytes += hdr_bytes;
		}
		else {
			memcpy(((unsigned char *)ctx->enc_hdr_buf+ctx->enc_hdr_bytes), hdr_buf, hdr_bytes);
			ctx->enc_hdr_bytes += hdr_bytes;
		}
	}

	return 0;
}

static int enc_header(struct rtkve1enc_ctx *ctx)
{
	int ret = 0;
	struct videc_dev *dev = ctx->dev;
	u32 header_code = 0;
	u32 crop_flag = 0;
	u32 sps_ID = 0;
	u32 pps_ID = 0;
	u32 zero_padding = 0;
	u32 enc_sps_size = 0;
	u32 enc_pps_size = 0;
	unsigned char *p = NULL;
	u32 crop_left=0, crop_right=0, crop_top=0, crop_bottom=0;
	u32 crop_h=0, crop_v=0;

	/* encode SPS begin */
	/* stream ctrl, related to lineBufIntEn/streamEndian */
	rtkve1_reg_writel(dev, BIT_BIT_STREAM_CTRL, 0x30);
	/* if ringBufferEnable == 0 */
	rtkve1_reg_writel(dev, CMD_ENC_HEADER_BB_START, (u32)(ctx->bitstream.paddr));
	rtkve1_reg_writel(dev, CMD_ENC_HEADER_BB_SIZE, (u32)(ctx->bitstream.size) / 1024);
	/* profile */
	rtkve1_reg_writel(dev, CMD_ENC_HEADER_PROFILE, ctx->enc_params.h264_profile_idc);
	/* chroma format, related to chromaFormat400 */
	rtkve1_reg_writel(dev, CMD_ENC_HEADER_CHROMA_FORMAT, 0);
	/* field flag, related to fieldRefMode/fieldFlag */
	rtkve1_reg_writel(dev, CMD_ENC_HEADER_FIELD_FLAG, ((ctx->enc_params.field_ref_mode<<1)|ctx->enc_params.field_flag));

	if ((ctx->dst_fmt.width != ctx->src_buf_width) || (ctx->dst_fmt.height != ctx->src_buf_height)) {
		if ((ctx->src_buf_width >= ctx->dst_fmt.width) && (ctx->src_buf_height > ctx->dst_fmt.height)) {
			crop_left = 0;
			crop_right = ctx->src_buf_width - ctx->dst_fmt.width;
			crop_top = 0;
			crop_bottom = ctx->src_buf_height - ctx->dst_fmt.height;
			crop_h = crop_left << 16;
			crop_h |= crop_right;
			crop_v = crop_top << 16;
			crop_v |= crop_bottom;
			crop_flag = 1;
			dev_dbg(dev->dev, "%d.%s.crop(%d,%d,%d,%d).crop_h:0x%x.crop_v:0x%x\n",
				__LINE__, __func__,
				crop_left, crop_right,
				crop_top, crop_bottom,
				crop_h, crop_v);
			rtkve1_reg_writel(dev, CMD_ENC_HEADER_FRAME_CROP_H, crop_h);
			rtkve1_reg_writel(dev, CMD_ENC_HEADER_FRAME_CROP_V, crop_v);
		}
	}

	/* header code, related to headerType/crop_flag/level/spsID/zeroPaddingEnable */
	dev_dbg(dev->dev, "%d.%s.headerType:%d.crop_flag:%d.h264_level_idc:%d.sps_ID:%d\n",
		__LINE__, __func__,
		HEADER_H264_SPS,
		crop_flag,
		ctx->enc_params.h264_level_idc,
		sps_ID);
	header_code = HEADER_H264_SPS | (crop_flag<<2);
	header_code |= ctx->enc_params.h264_level_idc<<8;
	header_code |= (sps_ID << 24);
	header_code |= (zero_padding&1) << 31;
	rtkve1_reg_writel(dev, CMD_ENC_HEADER_CODE, header_code);

	/* rdPtr, wrPtr */
	ctx->rdPtr = (u32)(ctx->bitstream.paddr);
	ctx->wrPtr = (u32)(ctx->bitstream.paddr);
	rtkve1_reg_writel(dev, BIT_RD_PTR, ctx->rdPtr);
	rtkve1_reg_writel(dev, BIT_WR_PTR, ctx->wrPtr);

	/* issue command */
	rtkve1_issue_command(ctx, ENCODE_HEADER);

	/* wait busy flag */
	ret = rtkve1_wait_busy(dev, BIT_BUSY_FLAG);
	if (ret) {
		dev_err(dev->dev,
			"%d.%s.wait_timeout.BIT_BUSY_FLAG.ret:%d\n",
            __LINE__, __func__,
			ret);
		return -ETIMEDOUT;
	}

	/* get result */
	ctx->wrPtr = rtkve1_reg_readl(dev, BIT_WR_PTR);
	dev_dbg(dev->dev, "%d.%s.r_reg BIT_WR_PTR(0x%X):0x%x\n",
		__LINE__, __func__,
		BIT_WR_PTR, ctx->wrPtr);
	enc_sps_size = ctx->wrPtr - (u32)(ctx->bitstream.paddr);
	dev_dbg(dev->dev, "%d.%s.enc_sps_size:%d\n",
		__LINE__, __func__,
		enc_sps_size);
	p = (unsigned char *)(ctx->bitstream.vaddr);
	dev_dbg(dev->dev, "%d.%s.sps:%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		__LINE__, __func__,
		p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
		p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
	//ctx->enc_bs_new_file = 1;
	//enc_dump_bs(ctx, p, enc_sps_size);
	enc_save_hdr(ctx, ctx->bitstream.vaddr, enc_sps_size, 0);
	/* encode SPS end */

	/* encode PPS begin */
	/* stream ctrl, related to lineBufIntEn/streamEndian */
	rtkve1_reg_writel(dev, BIT_BIT_STREAM_CTRL, 0x30);
	/* if ringBufferEnable == 0 */
	rtkve1_reg_writel(dev, CMD_ENC_HEADER_BB_START, (u32)(ctx->bitstream.paddr));
	rtkve1_reg_writel(dev, CMD_ENC_HEADER_BB_SIZE, (u32)(ctx->bitstream.size) / 1024);

	if (ctx->enc_params.entropy_coding_mode != 2) {
		/* if PPS, if entropyCodingMode != 2 (CAVLC/CABAC select according to PicType) */
		rtkve1_reg_writel(dev, CMD_ENC_HEADER_CABAC_MODE, ctx->enc_params.entropy_coding_mode);
		/* if PPS, if entropyCodingMode != 2 (CAVLC/CABAC select according to PicType), related to cabacInitIdc */
		rtkve1_reg_writel(dev, CMD_ENC_HEADER_CABAC_INIT_IDC, 0);
		/* if PPS, if entropyCodingMode != 2 (CAVLC/CABAC select according to PicType), related to transform8x8Mode */
		rtkve1_reg_writel(dev, CMD_ENC_HEADER_TRANSFORM_8X8, ctx->enc_params.transform_8x8_mode);
	}

	/* header code, related to headerType/crop_flag/level/spsID/zeroPaddingEnable */
	dev_dbg(dev->dev, "%d.%s.headerType:%d.crop_flag:%d.h264_level_idc:%d.sps_ID:%d.pps_ID:%d\n",
		__LINE__, __func__,
		HEADER_H264_PPS,
		crop_flag,
		ctx->enc_params.h264_level_idc,
		sps_ID, pps_ID);
	header_code = 0;
	header_code = HEADER_H264_PPS | (crop_flag<<2);
	header_code |= ctx->enc_params.h264_level_idc<<8;
	header_code |= (sps_ID << 24);
	header_code |= (pps_ID << 16);
	header_code |= (zero_padding&1) << 31;
	rtkve1_reg_writel(dev, CMD_ENC_HEADER_CODE, header_code);

	ctx->rdPtr = (u32)(ctx->bitstream.paddr);
	rtkve1_reg_writel(dev, BIT_RD_PTR, ctx->rdPtr);
	rtkve1_reg_writel(dev, BIT_WR_PTR, ctx->wrPtr);

	/* issue command */
	rtkve1_issue_command(ctx, ENCODE_HEADER);

	/* wait busy flag */
	ret = rtkve1_wait_busy(dev, BIT_BUSY_FLAG);
	if (ret) {
		dev_err(dev->dev,
			"%d.%s.wait_timeout.BIT_BUSY_FLAG.ret:%d\n",
            __LINE__, __func__,
			ret);
		return -ETIMEDOUT;
	}

	/* get result */
	ctx->wrPtr = rtkve1_reg_readl(dev, BIT_WR_PTR);
	dev_dbg(dev->dev, "%d.%s.r_reg BIT_WR_PTR(0x%X):0x%x\n",
		__LINE__, __func__,
		BIT_WR_PTR, ctx->wrPtr);
	enc_pps_size = ctx->wrPtr - (u32)(ctx->bitstream.paddr);
	dev_dbg(dev->dev, "%d.%s.enc_pps_size:%d\n",
		__LINE__, __func__,
		enc_pps_size);
	p = (unsigned char *)(ctx->bitstream.vaddr);
	dev_dbg(dev->dev, "%d.%s.pps:%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		__LINE__, __func__,
		p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
		p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
	//enc_dump_bs(ctx, p, enc_pps_size);
	enc_save_hdr(ctx, ctx->bitstream.vaddr, enc_pps_size, 1);
	/* encode PPS end */

	dev_dbg(dev->dev, "%d.%s.enc_hdr_bytes:%d\n",
		__LINE__, __func__,
		ctx->enc_hdr_bytes);
	p = (unsigned char *)ctx->enc_hdr_buf;
	while (p < ((unsigned char *)ctx->enc_hdr_buf + ctx->enc_hdr_bytes)) {
		dev_dbg(dev->dev, "%d.%s.hdr_buf:%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			__LINE__, __func__,
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
			p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
		p += 16;
	}

	ctx->enc_header_done = 1;

	return ret;
}

static int enc_copy_hdr(struct rtkve1enc_ctx *ctx, void *cap_buf, int cap_buf_size, int frm_bytes)
{
	int ret = 0;
	struct videc_dev *dev = ctx->dev;
	void *tmp_buf = NULL;

	if ((frm_bytes + ctx->enc_hdr_bytes) > cap_buf_size) {
		dev_err(dev->dev, "[%d]%s.no space for copy hdr to cap buf\n",
			__LINE__, __func__);
		return -EINVAL;
	}
	if ((ctx->enc_hdr_bytes == 0) || (ctx->enc_hdr_buf_size == 0)
		|| (ctx->enc_hdr_buf == NULL)) {
		dev_err(dev->dev, "[%d]%s.hdr buf is empty\n",
			__LINE__, __func__);
		return -EINVAL;
	}

	tmp_buf = (unsigned char *)vmalloc(frm_bytes);
	if (!tmp_buf) {
		dev_err(dev->dev, "[%d]%s.vmalloc() fail\n",
			__LINE__, __func__);
		return -ENOMEM;
	}

	memcpy(tmp_buf, cap_buf, frm_bytes);
	memcpy(cap_buf, ctx->enc_hdr_buf, ctx->enc_hdr_bytes);
	memcpy(((unsigned char *)cap_buf+ctx->enc_hdr_bytes), tmp_buf, frm_bytes);

	vfree(tmp_buf);
	tmp_buf = NULL;

	return ret;
}

static int enc_pic(struct rtkve1enc_ctx *ctx)
{
	int ret = 0;
	struct vb2_v4l2_buffer *src_buf = NULL;
	struct vb2_v4l2_buffer *dst_buf = NULL;
	unsigned char *p = NULL;
	u32 offset = 0;
	u32 len = 0;
	u32 cbcrInterleave = 0;
	u32 nv21 = 0;
	u32 rot_mode = 0;
	dma_addr_t src_buf_y=0, src_buf_cb=0, src_buf_cr=0;
	dma_addr_t dst_buf_paddr = 0;
	u32 dst_buf_size = 0;
	u32 dst_buf_payload = 0;
	void *dst_buf_vaddr = NULL;
	struct videc_dev *dev = ctx->dev;
	u32 src_buf_index = 0;
	u32 is_idr = 0;
	u32 value = 0;
	u32 frm_idx, pic_type, enc_frm_bs_bytes, num_of_slices, pic_flag, src_idx;
	//u32 i = 0;
	int intr_reason = 0;

	ctx->enc_pic_done = 0;

	src_buf = v4l2_m2m_next_src_buf(ctx->v4l2_fh.m2m_ctx);
	if (!src_buf) {
		dev_err(dev->dev, "%d.%s.v4l2_m2m_next_src_buf() fail\n", __LINE__, __func__);
		ret = -EINVAL;
		return ret;
	}

	dst_buf = v4l2_m2m_next_dst_buf(ctx->v4l2_fh.m2m_ctx);
	if (!dst_buf) {
		dev_err(dev->dev, "%d.%s.v4l2_m2m_next_dst_buf() fail\n", __LINE__, __func__);
		ret = -EINVAL;
		return ret;
	}

	//dev_dbg(dev->dev, "%d.%s.msleep 100ms\n",
	//	__LINE__, __func__);
	//msleep(100);

	dev_dbg(dev->dev, "%d.%s.src_buf:0x%px.index:%d.sequence:%d\n",
		__LINE__, __func__,
		src_buf, src_buf->vb2_buf.index,
		src_buf->sequence);
	dev_dbg(dev->dev, "%d.%s.dst_buf:0x%px.index:%d.sequence:%d\n",
		__LINE__, __func__,
		dst_buf, dst_buf->vb2_buf.index,
		dst_buf->sequence);

	p = (unsigned char *)vb2_plane_vaddr(&src_buf->vb2_buf, 0);
	len = src_buf->vb2_buf.planes[0].bytesused;
	offset = src_buf->vb2_buf.planes[0].data_offset;
	//dev_dbg(dev->dev, "%d.%s.src_buf.p:0x%px.len:%d.offset:%d\n",
	//	__LINE__, __func__,
	//	p, len, offset);

	if (src_buf->sequence == 0) {
		ctx->outbuf_new_file = 1;
	}
	if (src_buf->sequence == 0) {
	dump_outbuf(ctx, p, offset, len);
	}
	//rtkve1_md5_hash(md5, VE1_MD5_SIZE,
	//	(char *)p, len);
	//dev_dbg(dev->dev, "yuv hash: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
	//	md5[0], md5[1], md5[2], md5[3], md5[4],
	//	md5[5], md5[6], md5[7], md5[8], md5[9],
	//	md5[10], md5[11], md5[12], md5[13], md5[14],
	//	md5[15]);

	switch (ctx->src_fmt.pixelformat) {
		case V4L2_PIX_FMT_NV12:
			cbcrInterleave = 1;
			nv21 = 0;
			break;
		case V4L2_PIX_FMT_NV21:
			cbcrInterleave = 1;
			nv21 = 1;
			break;
		default:
			break;
	}

	src_buf_y = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0) +
		src_buf->vb2_buf.planes[0].data_offset;
	src_buf_cb = src_buf_y + ctx->src_buf_width * ctx->src_buf_height;
	src_buf_cr = src_buf_cb + (ctx->src_buf_width * ctx->src_buf_height / 4);
	if (cbcrInterleave) {
		src_buf_cr = 0xffffffff;
	}
	//dev_dbg(dev->dev, "%d.%s.src_buf_y:%pad.src_buf_cb:%pad.src_buf_cr:%pad\n",
	//	__LINE__, __func__,
	//	&src_buf_y, &src_buf_cb, &src_buf_cr);
	dev_dbg(dev->dev, "%d.%s.src_buf_y:0x%llx.src_buf_cb:0x%llx.src_buf_cr:0x%llx\n",
		__LINE__, __func__,
		src_buf_y, src_buf_cb, src_buf_cr);

	dst_buf_paddr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0) +
		dst_buf->vb2_buf.planes[0].data_offset;
	dst_buf_size = vb2_plane_size(&dst_buf->vb2_buf, 0);
	dst_buf_vaddr = vb2_plane_vaddr(&dst_buf->vb2_buf, 0);
	//dev_dbg(dev->dev, "%d.%s.dst_buf_paddr:%pad.dst_buf_size:%d.dst_buf_vaddr:%px\n",
	//	__LINE__, __func__,
	//	&dst_buf_paddr, dst_buf_size, dst_buf_vaddr);
	dev_dbg(dev->dev, "%d.%s.dst_buf_paddr:0x%llx.dst_buf_size:%d.dst_buf_vaddr:%px\n",
		__LINE__, __func__,
		dst_buf_paddr, dst_buf_size, dst_buf_vaddr);


	/* endian/cbcrInterleave/sourceLBurstEn */
	rot_mode = (cbcrInterleave&0x01) << 18;
	rot_mode |= nv21 << 21;
	rtkve1_reg_writel(dev, CMD_ENC_PIC_ROT_MODE, rot_mode);
	/* quantParam */
	rtkve1_reg_writel(dev, CMD_ENC_PIC_QS, 0xa);

	/* The index number set to CMD_ENC_PIC_SRC_INDEX must be larger than
	the number set to CMD_SET_FRAME_BUF_NUM to avoid reconstruction frame buffer area. */
	src_buf_index = src_buf->vb2_buf.index + ctx->enc_min_fb_num;
	dev_dbg(dev->dev, "%d.%s.src_buf_index:%d\n",
		__LINE__, __func__,
		src_buf_index);
	rtkve1_reg_writel(dev, CMD_ENC_PIC_SRC_INDEX, src_buf_index);

	rtkve1_reg_writel(dev, CMD_ENC_PIC_SRC_STRIDE, ctx->src_buf_width);

	rtkve1_reg_writel(dev, CMD_ENC_PIC_SRC_ADDR_Y, (u32)src_buf_y);
	rtkve1_reg_writel(dev, CMD_ENC_PIC_SRC_ADDR_CB, (u32)src_buf_cb);
	rtkve1_reg_writel(dev, CMD_ENC_PIC_SRC_ADDR_CR, (u32)src_buf_cr);
	rtkve1_reg_writel(dev, CMD_ENC_PIC_SRC_BOTTOM_Y, 0);
	rtkve1_reg_writel(dev, CMD_ENC_PIC_SRC_BOTTOM_CB, 0);
	rtkve1_reg_writel(dev, CMD_ENC_PIC_SRC_BOTTOM_CR, 0);

	/* pic option, related to fieldRun/forceIPicture */
	if (src_buf->sequence == 0) {
		is_idr = 1;
	}
	value = is_idr << 1 & 0x2;
	rtkve1_reg_writel(dev, CMD_ENC_PIC_OPTION, value);

	/* if ringBufferEnable == 0 */
	rtkve1_reg_writel(dev, CMD_ENC_PIC_BB_START, (u32)dst_buf_paddr);
	rtkve1_reg_writel(dev, CMD_ENC_PIC_BB_SIZE, dst_buf_size/1024);

	/* rdPtr, if ringBufferEnable == 0  */
	ctx->rdPtr = (u32)dst_buf_paddr;
	rtkve1_reg_writel(dev, BIT_RD_PTR, ctx->rdPtr);

	/* AXI use */
	rtkve1_reg_writel(dev, BIT_AXI_SRAM_USE, 0);

	/* rdPtr, wrPtr */
	ctx->wrPtr = (u32)dst_buf_paddr;
	ctx->rdPtr = (u32)dst_buf_paddr;
	rtkve1_reg_writel(dev, BIT_WR_PTR, ctx->wrPtr);
	rtkve1_reg_writel(dev, BIT_RD_PTR, ctx->rdPtr);

	/* streamEndflag */
	rtkve1_reg_writel(dev, BIT_BIT_STREAM_PARAM, 0);

	/* frame mem ctrl, related to bwbEnable/linear2TiledMode/mapType/chromaFormat400/cbcrInterleave/frameEndian */
	value = (VE1_BWB_ENABLE<<15) | (cbcrInterleave<<2) | VPU_FRAME_ENDIAN;
	rtkve1_reg_writel(dev, BIT_FRAME_MEM_CTRL, value);
	/* stream ctrl, related to lineBufIntEn/streamEndian */
	rtkve1_reg_writel(dev, BIT_BIT_STREAM_CTRL, 0x30);

	/* ME line buffer mode */
	rtkve1_reg_writel(dev, BIT_ME_LINEBUFFER_MODE, 0);
#if 0
	if (src_buf->sequence < 3) {
	//dev_dbg(dev->dev, "[%d]%s.r_register from 0x9804_0000~0x9804_2FFF start\n", __LINE__, __func__);
	//for (i=0; i<3072; i++) {
	dev_dbg(dev->dev, "[%d]%s.r_register from 0x9804_0000~0x9804_01FF start\n", __LINE__, __func__);
	for (i=0; i<128; i++) {
		value = rtkve1_reg_readl(dev, (i*4));
		dev_dbg(dev->dev, "[%d]%s.r_reg 0x%04X:0x%x\n", __LINE__, __func__,
			(i*4), value);
	}
	//dev_dbg(dev->dev, "[%d]%s.r_register from 0x9804_0000~0x9804_2FFF end\n", __LINE__, __func__);
	dev_dbg(dev->dev, "[%d]%s.r_register from 0x9804_0000~0x9804_01FF end\n", __LINE__, __func__);
	}
#endif
	/* issue command */
	rtkve1_issue_command(ctx, PIC_RUN);

	/* wait interrupt */
	intr_reason = rtkve1_wait_interrupt(ctx, RTKVE1_ENC_TIMEOUT);
	if (intr_reason < 0) {
		dev_err(dev->dev, "%d.%s.rtkve1_wait_interrupt() fail\n",
			__LINE__, __func__);
		return intr_reason;
	}
	dev_dbg(dev->dev, "%d.%s.intr_reason:0x%x\n",
		__LINE__, __func__, intr_reason);
	rtkve1_clear_interrupt(ctx);

	/* get result */
	ret = rtkve1_reg_readl(dev, RET_ENC_PIC_SUCCESS);
	dev_dbg(dev->dev, "%d.%s.r_reg RET_ENC_PIC_SUCCESS(0x%X):0x%x\n",
		__LINE__, __func__,
		RET_ENC_PIC_SUCCESS, ret);
	if (ret & (1 << 31)) {
		dev_err(dev->dev, "%d.%s.memory access violation\n",
			__LINE__, __func__);
		return ret;
	}

	frm_idx = rtkve1_reg_readl(dev, RET_ENC_PIC_FRAME_NUM);
	pic_type = rtkve1_reg_readl(dev, RET_ENC_PIC_TYPE);
	dev_dbg(dev->dev, "%d.%s.frm_idx:%d.pic_type:%d\n",
		__LINE__, __func__,
		frm_idx, pic_type);

	ctx->rdPtr = rtkve1_reg_readl(dev, BIT_RD_PTR);
	dev_dbg(dev->dev, "%d.%s.r_reg BIT_RD_PTR(0x%X):0x%x\n",
		__LINE__, __func__,
		BIT_RD_PTR, ctx->rdPtr);
	ctx->wrPtr = rtkve1_reg_readl(dev, BIT_WR_PTR);
	dev_dbg(dev->dev, "%d.%s.r_reg BIT_WR_PTR(0x%X):0x%x\n",
		__LINE__, __func__,
		BIT_WR_PTR, ctx->wrPtr);
	enc_frm_bs_bytes = ctx->wrPtr - ctx->rdPtr;
	dev_dbg(dev->dev, "%d.%s.encoded frame bytes:%d\n",
		__LINE__, __func__,
		enc_frm_bs_bytes);
	//enc_dump_bs(ctx, (unsigned char *)dst_buf_vaddr, enc_frm_bs_bytes);
	dst_buf_payload = enc_frm_bs_bytes;

	if (src_buf->sequence == 0) {
		enc_copy_hdr(ctx, dst_buf_vaddr, dst_buf_size, enc_frm_bs_bytes);
		dst_buf_payload = enc_frm_bs_bytes + ctx->enc_hdr_bytes;
		ctx->enc_bs_new_file = 1;
		enc_dump_bs(ctx, (unsigned char *)dst_buf_vaddr, dst_buf_payload);
		dev_dbg(dev->dev, "%d.%s.dst_buf_payload:%d\n",
			__LINE__, __func__,
			dst_buf_payload);
	}
	else {
		enc_dump_bs(ctx, (unsigned char *)dst_buf_vaddr, enc_frm_bs_bytes);
	}

	num_of_slices = rtkve1_reg_readl(dev, RET_ENC_PIC_SLICE_NUM);
	pic_flag = rtkve1_reg_readl(dev, RET_ENC_PIC_FLAG);
	src_idx = rtkve1_reg_readl(dev, RET_ENC_PIC_FRAME_IDX);
	dev_dbg(dev->dev, "%d.%s.num_of_slices:%d.pic_flag:0x%x.src_idx:%d\n",
		__LINE__, __func__,
		num_of_slices, pic_flag, src_idx);

	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, false);
	if (is_idr)
		dst_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;
	else
		dst_buf->flags |= V4L2_BUF_FLAG_PFRAME;
	dst_buf->flags |= src_buf->flags & V4L2_BUF_FLAG_LAST;
	dev_dbg(dev->dev, "%d.%s.dst_buf->flags:0x%x.src_buf->flags:0x%x\n",
		__LINE__, __func__,
		dst_buf->flags,
		src_buf->flags);

	vb2_set_plane_payload(&dst_buf->vb2_buf, 0, dst_buf_payload);

	dst_buf = v4l2_m2m_dst_buf_remove(ctx->v4l2_fh.m2m_ctx);
	dev_dbg(dev->dev, "%d.%s.v4l2_m2m_buf_done.dst_buf:0x%px\n",
		__LINE__, __func__,
		dst_buf);
	v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);

	src_buf = v4l2_m2m_src_buf_remove(ctx->v4l2_fh.m2m_ctx);
	dev_dbg(dev->dev, "%d.%s.v4l2_m2m_buf_done.src_buf:0x%px.src_buf_y:0x%llx\n",
		__LINE__, __func__,
		src_buf,
		src_buf_y);
	v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);

	ctx->enc_pic_done = 1;

	return ret;
}

static int enc_seq_end(struct rtkve1enc_ctx *ctx)
{
	struct videc_dev *dev = ctx->dev;
	int ret = 0;

	lockdep_assert_held(&dev->ve1_hw_mutex);

	/* if seq_init_done */
	rtkve1_reg_writel(dev, BIT_WR_PTR, ctx->wrPtr);
	rtkve1_reg_writel(dev, BIT_RD_PTR, ctx->rdPtr);

	/* issue command */
	rtkve1_issue_command(ctx, ENC_SEQ_END);

	/* wait busy flag */
	ret = rtkve1_wait_busy(dev, BIT_BUSY_FLAG);
	if (ret) {
		dev_err(dev->dev,
			"%d.%s.wait_timeout.BIT_BUSY_FLAG.ret:%d\n",
            __LINE__, __func__,
			ret);
		return -ETIMEDOUT;
	}

	ctx->wrPtr = rtkve1_reg_readl(dev, BIT_WR_PTR);
	dev_dbg(dev->dev, "%d.%s.r_reg BIT_WR_PTR(0x%X):0x%x\n", __LINE__, __func__,
		BIT_WR_PTR, ctx->rdPtr);

	ctx->seq_end_done = 1;
	ctx->stopping = 0;

	return ret;
}

static void enc_clear_ctx(struct rtkve1enc_ctx *ctx)
{
	/* clear state variables */
	ctx->enc_state = RTK_VE1_STATE_ENC_IDLE;
	ctx->seq_init_done = 0;
	ctx->reg_fbs_done = 0;
	ctx->enc_header_done = 0;
	ctx->enc_pic_done = 0;
	ctx->seq_end_done = 0;

	/* clear flags */
	ctx->stopping = 0;
	ctx->eos = 0;

	ctx->rdPtr = 0;
	ctx->wrPtr = 0;
	ctx->enc_min_fb_num = 0;

	if (ctx->enc_hdr_buf_size != 0) {
		vfree((void *)ctx->enc_hdr_buf);
		ctx->enc_hdr_buf = NULL;
	}
	ctx->enc_hdr_buf_size = 0;
	ctx->enc_hdr_bytes = 0;

	ctx->outbuf_new_file = 0;
	ctx->enc_bs_new_file = 0;
}

static int rtkve1enc_queue_init(void *priv, struct vb2_queue *src_vq,
                struct vb2_queue *dst_vq)
{
	struct rtkve1enc_ctx *ctx = priv;
	int ret;

	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px\n",
		__LINE__, __func__, ctx);

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->ops = &rtkve1enc_vb2_ops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->drv_priv = ctx;
	src_vq->lock = &ctx->dev->dev_mutex;
	src_vq->dev = ctx->dev->v4l2_dev.dev;
	//src_vq->supports_requests = true;
	ret = vb2_queue_init(src_vq);
	if (ret)
		goto exit;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->ops = &rtkve1enc_vb2_ops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->min_buffers_needed = 1;
	dst_vq->drv_priv = ctx;
	dst_vq->lock = &ctx->dev->dev_mutex;
	dst_vq->dev = ctx->dev->v4l2_dev.dev;
	ret = vb2_queue_init(dst_vq);
	if (ret)
		goto exit;

exit:
	dev_dbg(ctx->dev->dev, "%d.%s.[-] ret:%d\n",
		__LINE__, __func__, ret);
	return ret;
}

static int rtkve1enc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct rtkve1enc_ctx *ctx =
	container_of(ctrl->handler, struct rtkve1enc_ctx, v4l2_ctrl_hdl);

	dev_dbg(ctx->dev->dev, "%d.%s.id:%d(0x%x)\n",
		__LINE__, __func__,
		(ctrl->id-V4L2_CID_CODEC_BASE),
		ctrl->id);

	switch (ctrl->id) {
		case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
			dev_dbg(ctx->dev->dev, "%d.%s.H264_PROFILE.val:%d\n",
				__LINE__, __func__,
				ctrl->val);
			if (ctrl->val == V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE ||
				ctrl->val == V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) {
				/* Baseline profile */
				ctx->enc_params.h264_profile_idc = 0;
			}
			else if (ctrl->val == V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) {
				/* Main profile */
				ctx->enc_params.h264_profile_idc = 1;
			}
			else if (ctrl->val == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH) {
				/* High profile */
				ctx->enc_params.h264_profile_idc = 2;
			}
			else {
				ret = -EINVAL;
			}
			dev_dbg(ctx->dev->dev, "%d.%s.h264_profile_idc:%d\n",
				__LINE__, __func__,
				ctx->enc_params.h264_profile_idc);
			break;
		case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
			dev_dbg(ctx->dev->dev, "%d.%s.H264_LEVEL.val:%d\n",
				__LINE__, __func__,
				ctrl->val);
			if ((ctrl->val >= 0) && (ctrl->val <= 15)) {
				ctx->enc_params.s_ctrl_level_value = ctrl->val;
			}
			else {
				ret = -EINVAL;
			}
			break;
		case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
			dev_dbg(ctx->dev->dev, "%d.%s.GOP_SIZE.val:%d\n",
				__LINE__, __func__,
				ctrl->val);
			if ((ctrl->val >= 0) && (ctrl->val <= 60)) {
				ctx->enc_params.gop_size = ctrl->val;
			}
			else {
				ret = -EINVAL;
			}
			break;
		case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
			dev_dbg(ctx->dev->dev, "%d.%s.I_FRAME_QP.val:%d\n",
				__LINE__, __func__,
				ctrl->val);
			if ((ctrl->val >= 0) && (ctrl->val <= 51)) {
				ctx->enc_params.intra_qp = ctrl->val;
			}
			else {
				ret = -EINVAL;
			}
			break;
		case V4L2_CID_MPEG_VIDEO_BITRATE:
			dev_dbg(ctx->dev->dev, "%d.%s.VIDEO_BITRATE.val:%d\n",
				__LINE__, __func__,
				ctrl->val);
			if ((ctrl->val >= MIN_BITRATE) && (ctrl->val <= MAX_BITRATE)) {
				ctx->enc_params.bitrate = ctrl->val/8; /* ctrl->val is average video bitrate in bits per second. */
			}
			else {
				ret = -EINVAL;
			}
			break;
		default:
			break;
	}

    return ret;
}

static const struct v4l2_ctrl_ops rtkve1enc_ctrl_ops = {
	.s_ctrl = rtkve1enc_s_ctrl,
};

static int rtkve1enc_ctrls_setup(struct rtkve1enc_ctx *ctx)
{
    struct v4l2_ctrl_handler *hdl = &ctx->v4l2_ctrl_hdl;
    int ret = 0;
    int max_gop_size = 60;

	dev_dbg(ctx->dev->dev, "%d.%s.[+] ctx:0x%px\n",
		__LINE__, __func__, ctx);

    v4l2_ctrl_handler_init(hdl, 9);

    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MIN_BUFFERS_FOR_OUTPUT, 1, 32, 1, 4);

    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1, 4);

    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_BITRATE,
        MIN_BITRATE, MAX_BITRATE, 1, DEF_BITRATE);

    v4l2_ctrl_new_std_menu(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
        V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
        ~(1 << V4L2_MPEG_VIDEO_BITRATE_MODE_VBR),
        V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);

    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_GOP_SIZE, 0, max_gop_size, 1, DEFAULT_GOP);
    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, -1, 51, 1, DEFAULT_I_FRAME_QP);
/*
    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, 0, 51, 1, 25);
    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_H264_MAX_QP, 0, 51, 1, 51);
    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_H264_CONSTRAINED_INTRA_PREDICTION, 0, 1, 1,
        0);
    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 0, 1, 1, 1);
    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE, 0, 1, 1, 1);
    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_H264_CHROMA_QP_INDEX_OFFSET, -12, 12, 1, 0);
*/

    v4l2_ctrl_new_std_menu(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_H264_PROFILE,
        V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
        ~((1 << V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
          (1 << V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
          (1 << V4L2_MPEG_VIDEO_H264_PROFILE_HIGH)),
        V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);
    v4l2_ctrl_new_std_menu(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_H264_LEVEL,
        V4L2_MPEG_VIDEO_H264_LEVEL_4_2, 0x0,
        V4L2_MPEG_VIDEO_H264_LEVEL_4_2);
    v4l2_ctrl_new_std_menu(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE,
        V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_BYTES, 0x0,
        V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE);
    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB, 1, 0x3fffffff, 1, 1);
    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES, 1, 0x3fffffff, 1,
        500);
    v4l2_ctrl_new_std_menu(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_HEADER_MODE,
        V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
        (1 << V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE),
        V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);
    v4l2_ctrl_new_std(hdl, &rtkve1enc_ctrl_ops,
        V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB, 0,
        1920 * 1088 / 256, 1, 0);

    if (hdl->error) {
        dev_err(ctx->dev->dev, "%d.%s.Failed to initialize control handler\n", __LINE__, __func__);
        v4l2_ctrl_handler_free(hdl);
        ret = hdl->error;
        goto exit;
    }

	ctx->enc_params.transform_8x8_mode = 1;
	dev_dbg(ctx->dev->dev, "%d.%s.set default.transform_8x8_mode:%d\n",
		__LINE__, __func__,
		ctx->enc_params.transform_8x8_mode);

	ctx->enc_params.field_flag = 0;
	ctx->enc_params.field_ref_mode = 1;
	dev_dbg(ctx->dev->dev, "%d.%s.set default.field_flag:%d.field_ref_mode:%d\n",
		__LINE__, __func__,
		ctx->enc_params.field_flag,
		ctx->enc_params.field_ref_mode);

	ctx->enc_params.entropy_coding_mode = 1;
	dev_dbg(ctx->dev->dev, "%d.%s.set default.entropy_coding_mode:%d\n",
		__LINE__, __func__,
		ctx->enc_params.entropy_coding_mode);

	ctx->enc_params.gop_size = DEFAULT_GOP;
	dev_dbg(ctx->dev->dev, "%d.%s.set default.gop_size:%d\n",
		__LINE__, __func__,
		ctx->enc_params.gop_size);

	ctx->enc_params.bitrate = DEF_BITRATE;
	dev_dbg(ctx->dev->dev, "%d.%s.set default.bitrate:%d\n",
		__LINE__, __func__,
		ctx->enc_params.bitrate);

	ctx->enc_params.framerate_num = DEFAULT_FRAMERATE_DENOM;
	ctx->enc_params.framerate_denom = DEFAULT_FRAMERATE_NUM;
	dev_dbg(ctx->dev->dev, "%d.%s.set default.framerate_num:%d.framerate_denom:%d\n",
		__LINE__, __func__,
		ctx->enc_params.framerate_num,
		ctx->enc_params.framerate_denom);

	ctx->enc_params.intra_qp = -1;
	dev_dbg(ctx->dev->dev, "%d.%s.set default.intra_qp:%d\n",
		__LINE__, __func__,
		ctx->enc_params.intra_qp);

    ctx->v4l2_fh.ctrl_handler = hdl;
    v4l2_ctrl_handler_setup(hdl);
exit:
	dev_dbg(ctx->dev->dev, "%d.%s.[-] ret:%d\n",
		__LINE__, __func__, ret);
    return ret;
}

static int rtkve1enc_reqbufs(struct rtkve1enc_ctx *ctx,
            struct v4l2_requestbuffers *rb)
{
	dev_dbg(ctx->dev->dev, "%d.%s.ctx:0x%px.type:%s.count:%d\n",
        __LINE__, __func__,
		ctx, v4l2_type_names[rb->type], rb->count);

	if (V4L2_TYPE_IS_OUTPUT(rb->type) && (rb->count != 0)) {
		rtkve1_initialize(ctx, ctx->dev);
	}
	return 0;
}

static int rtkve1enc_decide_state(struct rtkve1enc_ctx *ctx, enum rtkve1_enc_state *new_state)
{
	int ret = 0;

	if (new_state == NULL) {
		return -EINVAL;
	}

	if (ctx->stopping && (ctx->enc_state >= RTK_VE1_STATE_ENC_SEQ_INIT)) {
		*new_state = RTK_VE1_STATE_ENC_SEQ_END;
		dev_dbg(ctx->dev->dev, "%d.%s.org_state:%s.new_state:%s\n",
			__LINE__, __func__,
			rtk_ve1_enc_state_str[ctx->enc_state],
			rtk_ve1_enc_state_str[*new_state]);
		return 0;
	}

	switch (ctx->enc_state) {
		case RTK_VE1_STATE_ENC_IDLE:
			*new_state = RTK_VE1_STATE_ENC_SEQ_INIT;
			break;
		case RTK_VE1_STATE_ENC_SEQ_INIT:
			if (!ctx->seq_init_done) {
				//dev_dbg(ctx->dev->dev, "%d.%s.!ctx->st_seq_init_done.state nochange\n", __LINE__, __func__);
				ret = -EPERM;
			}
			else {
				*new_state = RTK_VE1_STATE_ENC_REG_FBS;
			}
			break;
		case RTK_VE1_STATE_ENC_REG_FBS:
			if (!ctx->reg_fbs_done) {
				dev_dbg(ctx->dev->dev, "%d.%s.!ctx->reg_fbs_done.state nochange\n", __LINE__, __func__);
				ret = -EPERM;
			}
			else {
				*new_state = RTK_VE1_STATE_ENC_HEADER;
			}
			break;
		case RTK_VE1_STATE_ENC_HEADER:
			if (!ctx->enc_header_done) {
				pr_info("%d.%s.!ctx->enc_header_done.state nochange\n", __LINE__, __func__);
				ret = -EPERM;
			}
			else {
				*new_state = RTK_VE1_STATE_ENC_PIC;
			}
			break;
		case RTK_VE1_STATE_ENC_PIC:
			if (!ctx->enc_pic_done) {
				pr_info("%d.%s.!ctx->enc_pic_done.state nochange\n", __LINE__, __func__);
				ret = -EPERM;
			}
			else {
				*new_state = RTK_VE1_STATE_ENC_PIC;
			}
			break;
		default:
			ret = -EPERM;
			break;
	}

	if ((ret == 0) && (*new_state != ctx->enc_state)) {
		dev_dbg(ctx->dev->dev, "%d.%s.org_state:%s.new_state:%s\n",
			__LINE__, __func__,
			rtk_ve1_enc_state_str[ctx->enc_state],
			rtk_ve1_enc_state_str[*new_state]);
	}

	return ret;
}

static int rtkve1enc_seq_init(struct rtkve1enc_ctx *ctx)
{
	int ret = 0;
	struct videc_dev *dev = ctx->dev;

	dev_dbg(dev->dev, "%d.%s.[+]\n", __LINE__, __func__);
	mutex_lock(&dev->ve1_hw_mutex);

	ret = enc_alloc_bsbuf_workbuf(ctx);
	if (ret < 0) {
		goto exit;
	}

	ctx->enc_state = RTK_VE1_STATE_ENC_SEQ_INIT;
	if (!ctx->seq_init_done) {
		ret = enc_seq_init(ctx);
	}

exit:
	mutex_unlock(&dev->ve1_hw_mutex);
	dev_dbg(dev->dev, "%d.%s.[-]\n", __LINE__, __func__);

	return ret;
}

static int rtkve1enc_reg_fbs(struct rtkve1enc_ctx *ctx)
{
	int ret = 0;
	struct videc_dev *dev = ctx->dev;

	dev_dbg(dev->dev, "%d.%s.[+]\n", __LINE__, __func__);
	mutex_lock(&dev->ve1_hw_mutex);

	ret = enc_alloc_frame_buffers(ctx);
	if (ret < 0) {
		goto exit;
	}

	ctx->enc_state = RTK_VE1_STATE_ENC_REG_FBS;
	if (!ctx->reg_fbs_done) {
		ret = enc_reg_fbs(ctx);
	}

exit:
	mutex_unlock(&dev->ve1_hw_mutex);
	dev_dbg(dev->dev, "%d.%s.[-]\n", __LINE__, __func__);

	return ret;
}

static int rtkve1enc_header(struct rtkve1enc_ctx *ctx)
{
	int ret = 0;
	struct videc_dev *dev = ctx->dev;

	dev_dbg(dev->dev, "%d.%s.[+]\n", __LINE__, __func__);
	mutex_lock(&dev->ve1_hw_mutex);
	ctx->enc_state = RTK_VE1_STATE_ENC_HEADER;
	if (!ctx->enc_header_done) {
		ret = enc_header(ctx);
	}
	mutex_unlock(&dev->ve1_hw_mutex);
	dev_dbg(dev->dev, "%d.%s.[-]\n", __LINE__, __func__);

	return ret;
}

static int rtkve1enc_pic(struct rtkve1enc_ctx *ctx)
{
	int ret = 0;
	struct videc_dev *dev = ctx->dev;

	dev_dbg(dev->dev, "%d.%s.[+]\n", __LINE__, __func__);
	mutex_lock(&dev->ve1_hw_mutex);
	ctx->enc_state = RTK_VE1_STATE_ENC_PIC;
	ret = enc_pic(ctx);
	mutex_unlock(&dev->ve1_hw_mutex);
	dev_dbg(dev->dev, "%d.%s.[-]\n", __LINE__, __func__);

	return ret;
}

static void rtkve1enc_seq_end(struct rtkve1enc_ctx *ctx)
{
	int ret = 0;
	struct videc_dev *dev = ctx->dev;
	int i = 0;
	char dbg_name[16];

	dev_dbg(dev->dev, "%d.%s.[+]\n", __LINE__, __func__);
	mutex_lock(&dev->ve1_hw_mutex);
	ctx->enc_state = RTK_VE1_STATE_ENC_SEQ_END;
	if (!ctx->seq_end_done) {
		ret = enc_seq_end(ctx);
		if (ret == -ETIMEDOUT) {
		}
	}

	for (i=0; i < ctx->enc_min_fb_num; i++) {
		if (ctx->framebuf[i].size != 0) {
			memset(dbg_name, 0, sizeof(dbg_name));
			snprintf(dbg_name, 16, "framebuf%d", i);
			dev_dbg(dev->dev, "%d.%s.free_dma.%d.name:%s.size:%d.paddr:0x%llx.vaddr:0x%px\n",
				__LINE__, __func__,
				i, dbg_name, ctx->framebuf[i].size,
				ctx->framebuf[i].paddr,
				ctx->framebuf[i].vaddr);
			rtkve1_free_dma_memory(dev, &ctx->framebuf[i], dbg_name);
		}
	}

	if (ctx->bitstream.size != 0) {
		dev_dbg(dev->dev, "%d.%s.free_dma.name:bsbuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			ctx->bitstream.size,
			ctx->bitstream.paddr,
			ctx->bitstream.vaddr);
		rtkve1_free_dma_memory(dev, &ctx->bitstream, "bsbuf");
	}
	if (ctx->workbuf.size != 0) {
		dev_dbg(dev->dev, "%d.%s.free_dma.name:workbuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			ctx->workbuf.size,
			ctx->workbuf.paddr,
			ctx->workbuf.vaddr);
		rtkve1_free_dma_memory(dev, &ctx->workbuf, "workbuf");
	}

	enc_clear_ctx(ctx);

	mutex_unlock(&dev->ve1_hw_mutex);
	dev_dbg(dev->dev, "%d.%s.[-]\n", __LINE__, __func__);
}

static void rtkve1enc_timeout(struct rtkve1enc_ctx *ctx)
{
}

const struct rtkve1_context_ops rtkve1enc_ops = {
	.find_vpu_fmt = rtkve_enc_find_fmt,
	.find_vpu_fmt_by_idx = rtkve_enc_find_fmt_by_idx,
    .queue_init = rtkve1enc_queue_init,
    .ctrls_setup = rtkve1enc_ctrls_setup,
	.reqbufs = rtkve1enc_reqbufs,
	.decide_state = rtkve1enc_decide_state,
	.seq_init = rtkve1enc_seq_init,
	.reg_fbs = rtkve1enc_reg_fbs,
	.enc_header = rtkve1enc_header,
	.enc_pic = rtkve1enc_pic,
	.seq_end = rtkve1enc_seq_end,
	.run_timeout = rtkve1enc_timeout,
};
