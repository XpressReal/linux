/*
 * Realtek video decoder v4l2 driver
 *
 * Copyright (c) 2021 Realtek Semiconductor Corp. All rights reserved.
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-Proprietary
 *
 * This software component is confidential and proprietary to Realtek
 * Semiconductor Corp. Disclosure, reproduction, redistribution, in whole
 * or in part, of this work and its derivatives without express permission
 * is prohibited.
 *
 * Release Log
 * v0.1 - Support H265 decode 2022 01 03
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#ifdef ENABLE_TEE_DRM_FLOW
#include <linux/tee_drv.h>
#endif
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>

#include "drv_if.h"
#include "vpu.h"
#include "ve2.h"
#include "ve2rpc.h"
#include "debug.h"

#define VENG_ID 2
#define VIDEO_CC_DATA_LENGTH 128
#define COUNTRY_CODE_OFFSET 3
#define CC_DATA_HEADER_LEN 16
#define CIE1931_TO_SMPTE (50000)
#define CANDELAS_PER_SQUARE_METER_BASE (10000)
//2K max resolution : 1920x1088=2088960
#define MAX_2K_RESOLUTION (2088960)

typedef struct {
	/* NTSC and PAL */
	unsigned int PTSHigh;
	unsigned int PTSLow;
	unsigned int cc_type;
	unsigned int repeat_first_field;
	unsigned int top_field_first;
	unsigned int size;
} VIDEO_CC_CALLBACK_HEADER;

struct ve2_ctx {
	struct ve2rpc *out_hndl;
	struct ve2rpc *cap_hndl;
#ifdef REORDER_PTS
	struct list_head pts_list;
#endif
	bool eosEvent;
	bool eos;
	bool no_frame;
	bool streamon_out;
	bool streamon_cap;
	bool streamoff_out;
	bool streamoff_cap;
	bool is_run;
	int32_t internal_buf_cnt;
	bool is_cmprs;
};

/* Return dma-buf fd and get offset & size from RPC driver */
extern int r_program_fd(unsigned long phys_addr, unsigned long *offset,
			unsigned long *size);
#ifdef ENABLE_TEE_DRM_FLOW
extern int ta_TEEapi_init(struct tee_context **teeapi_ctx,
			  unsigned int *teeapi_tee_session);
extern int ta_TEEapi_deinit(struct tee_context *teeapi_ctx,
			    unsigned int teeapi_tee_session);
#endif
/*
 * Return ve2_ctx structure for a given struct v4l2_fh
 */
static struct ve2_ctx *fh_to_ve(struct v4l2_fh *fh)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	return vid_ctx->ve_ctx;
}

static struct vpu_ctx *fh_to_vpu(struct v4l2_fh *fh)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	return vid_ctx->vpu_ctx;
}

/*
 * Return ve2_ctx structure for a given struct vb2_queue
 */
static struct ve2_ctx *vq_to_ve(struct vb2_queue *q)
{
	struct videc_ctx *vid_ctx = vb2_get_drv_priv(q);
	return vid_ctx->ve_ctx;
}

int ve2_start_streaming(struct vb2_queue *q, uint32_t count, int pixelformat)
{
	struct ve2_ctx *ctx = vq_to_ve(q);
	struct videc_ctx *vid_ctx = vb2_get_drv_priv(q);
	struct vpu_ctx *v_ctx = (struct vpu_ctx *)vid_ctx->vpu_ctx;
	int type = q->type;
	int ret;

	if(!ctx) {
		vpu_err("%s ctx is NULL\n", __func__);
		return -EINVAL;
	}

	pr_err("ve2_start_streaming(%s), codec %p4cc\n", v4l2_type_names[type],
	       &pixelformat);

	if (!(pixelformat == V4L2_PIX_FMT_HEVC ||
	      pixelformat == V4L2_PIX_FMT_AV1 ||
	      pixelformat == V4L2_PIX_FMT_VP9)) {
		vpu_err("Unsupport pixel format %s, %p4cc\n",
			V4L2_TYPE_TO_STR(type), &pixelformat);
		return -EINVAL;
	}

	if (V4L2_TYPE_IS_OUTPUT(q->type) && (ctx->streamon_out == 1)) {
		vpu_err("%s OUTPUT is already stream on\n", __func__);
		return -EPERM;
	} else if (V4L2_TYPE_IS_CAPTURE(q->type) && (ctx->streamon_cap == 1)) {
		vpu_err("%s CAPTURE is already stream on\n", __func__);
		return -EPERM;
	}

	if (V4L2_TYPE_IS_OUTPUT(type)) {
#ifdef VPU_GET_CC
		ret = ve2rpc_setDecoderCCBypass(ctx->out_hndl,
						VIDEODECODER_CC_CALLBACK);
		if (ret) {
			vpu_err("ve2rpc_inband_newseg fail\n");
			return ret;
		}
#endif
		ret = ve2rpc_set_cmprs(ctx->out_hndl, ctx->is_cmprs);
		if (ret) {
			vpu_err("ve2rpc fail to set cmprs\n");
			return ret;
		}

		ret = ve2rpc_inband_newseg(ctx->out_hndl);
		if (ret) {
			vpu_err("ve2rpc_inband_newseg fail\n");
			return ret;
		}

		ret = ve2rpc_inband_decode(ctx->out_hndl, NORMAL_DECODE);
		if (ret) {
			vpu_err("ve2rpc_inband_decode fail\n");
			return ret;
		}

		ret = ve2rpc_run(ctx->cap_hndl);
		if (ret) {
			vpu_err("ve2rpc_run cap_hndl fail\n");
			return ret;
		}

		ret = ve2rpc_run(ctx->out_hndl);
		if (ret) {
			vpu_err("ve2rpc_run out_hndl fail\n");
			return ret;
		}
		ctx->is_run = true;
#ifdef VPU_GET_CC
		ret = ve2rpc_SetRingBuffer(ctx->out_hndl, &ctx->out_hndl->cc_rb,
					   0x100000 /* 1MB */, RINGBUFFER_DTVCC,
					   false);
		if (ret) {
			vpu_err("ve2rpc fail to initial cc_rb\n");
			return -EPERM;
		}
#endif
#ifdef SUPPORT_ADAPTIVE_PLAYBACK
		if (vid_ctx->params.dec_params.en_adaptive_playback)
			ctx->cap_hndl->main_rb.pRBH->reserve3 =
				htonl(0 /*width*/ << 16 | 0 /*height*/);
		else
			ctx->cap_hndl->main_rb.pRBH->reserve3 = htonl(
				v_ctx->cap_fmt.misc.ori_width << 16 |
				v_ctx->cap_fmt.misc.ori_height);
#endif
		ctx->out_hndl->main_rb.memory = q->memory;

		ctx->streamon_out = 1;
		ctx->streamoff_out = 0;
		ctx->out_hndl->type = type;
#ifdef VPU_GET_CC
		cc_data_channel_init();
#endif
	} else {
		ret = ve2rpc_run(ctx->cap_hndl);
		if (ret) {
			vpu_err("ve2rpc_run cap_hndl fail\n");
			return ret;
		}

		ctx->streamon_cap = 1;
		ctx->streamoff_cap = 0;
		ctx->cap_hndl->type = type;
		ctx->cap_hndl->bit_depth = (v_ctx->bit_depth == 10)?10:8;
	}

	return 0;
}

int ve2_abort(void *ve_ctx, int type)
{
	return 0;
}

static void ve2_make_undq_capbuf_done(struct ve2_ctx *ctx)
{
	int i = 0;
	struct ve2rpc *cap_hndl;

	if (ctx == NULL) {
		vpu_err("%s invalid parameters\n", __func__);
		return;
	}

	cap_hndl = ctx->cap_hndl;
	mutex_lock(&cap_hndl->dpb_mutex);
	for (i = 0; i < VE2_MAX_DPB_NUM; i++) {
		struct vb2_v4l2_buffer *vb2_v4l2_buf =
			(struct vb2_v4l2_buffer *)cap_hndl->dpb[i].vb2_v4l2_buf;

		if (cap_hndl->dpb[i].status == RTKVE2_DPB_ST_VALID &&
			vb2_v4l2_buf->vb2_buf.state == VB2_BUF_STATE_ACTIVE) {
			v4l2_m2m_buf_done(
				(struct vb2_v4l2_buffer
					 *)(cap_hndl->dpb[i].vb2_v4l2_buf),
				VB2_BUF_STATE_ERROR);
		}
	}

	mutex_unlock(&cap_hndl->dpb_mutex);
}

int ve2_stop_streaming(struct vb2_queue *q)
{
	struct ve2_ctx *ctx = vq_to_ve(q);
	int type = q->type;
	struct ve2rpc *hndl;
	int ret = 0;

	if(!ctx) {
		vpu_err("%s ctx is NULL\n", __func__);
		goto exit;
	}

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		if (!ctx->streamon_out) {
			vpu_err("No out stream on !!!\n");
			ret = (-EPERM);
			goto exit;
		}
		hndl = ctx->out_hndl;
	} else {
		if (!ctx->streamon_cap) {
			vpu_err("No cap stream on !!!\n");
			ret = (-EPERM);
			goto exit;
		}
		hndl = ctx->cap_hndl;
	}

	if (!hndl) {
		vpu_err("ve2_stop_streaming %s hnd is NULL\n",
			V4L2_TYPE_TO_STR(q->type));
		goto exit;
	}

	if (!ctx->streamon_out && !ctx->streamon_cap) {
		vpu_err("No streamon can't streamoff !!!\n");
		ret = (-EPERM);
		goto exit;
	}

	if (ctx->streamoff_out && ctx->streamoff_cap) {
		vpu_err("Streamoff return directly\n");
		goto exit;
	}

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		ret = ve2rpc_pause(ctx->out_hndl);
		if (ret) {
			vpu_err("out ve2rpc_pause fail\n");
			goto exit;
		}

		ret = ve2rpc_flush(ctx->out_hndl);
		if (ret) {
			vpu_err("out ve2rpc_fluse fail\n");
			goto exit;
		}

		if(ctx->streamoff_cap) {
			ret = ve2rpc_pause(ctx->cap_hndl);
			if (ret) {
				vpu_err("cap ve2rpc_pause fail\n");
				goto exit;
			}

			ret = ve2rpc_flush(ctx->cap_hndl);
			if (ret) {
				vpu_err("cap ve2rpc_fluse fail\n");
				goto exit;
			}
		}

		ret = ve2rpc_reset_bs_ring_rwptr(ctx->out_hndl);
		if (ret) {
			vpu_err("reset bs_ringbuffer_rwptr fail\n");
			goto exit;
		}

		ctx->internal_buf_cnt = 0;
		ctx->streamoff_out = 1;
		ctx->streamon_out = 0;
	} else {
		ret = ve2rpc_pause(ctx->cap_hndl);
		if (ret) {
			vpu_err("cap ve2rpc_pause fail\n");
			goto exit;
		}
		ret = ve2rpc_flush(ctx->cap_hndl);
		if (ret) {
			vpu_err("cap ve2rpc_fluse fail\n");
			goto exit;
		}

		ret = ve2rpc_reset_msg_ring_rwptr(ctx->cap_hndl);
		if (ret) {
			vpu_err("reset frm_ringbuffer_rwptr fail\n");
			goto exit;
		}

		ret = ve2rpc_reset_buflock(ctx->cap_hndl, false);
		if (ret) {
			vpu_err("ve2rpc_reset_buflock fail\n");
			goto exit;
		}

		ve2_make_undq_capbuf_done(ctx);

		ctx->internal_buf_cnt = 0;
#ifdef REORDER_PTS
		ctx->cap_hndl->pre_pts = 0;
#endif
		ctx->streamoff_cap = 1;
		ctx->streamon_cap = 0;
	}

#ifdef REORDER_PTS
	if (ctx->streamoff_out && ctx->streamoff_cap) {
		if (ctx->out_hndl->is_pts_reorder) {
			ret = ve2rpc_free_pts(ctx->out_hndl);
			if (ret) {
				vpu_err("ve2rpc_free_pts fail\n");
				goto exit;
			}
		}
	}
#endif
exit:
	return ret;
}

static int ve2_out_qbuf(void *fh, uint8_t *buf, uint32_t len, uint64_t pts,
			uint32_t sequence)
{
	struct ve2_ctx *ctx = fh_to_ve(fh);
	struct ve2rpc *out_hdl = NULL;
	struct ve2rpc *cap_hdl = NULL;
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	struct vpu_ctx *v_ctx = (struct vpu_ctx *)vid_ctx->vpu_ctx;
	int ret;

	if(!ctx) {
		vpu_err("%s ctx is NULL\n", __func__);
		return -EINVAL;
	}
	out_hdl = ctx->out_hndl;
	cap_hdl = ctx->cap_hndl;

	vpu_input_dbg("ve2_out_qbuf seq %d, pts %lld, len %x\n", sequence, pts,
		      len);

	if (v_ctx->is_decoder_error || out_hdl->is_error) {
		vpu_err("%s decode error %d, %d", __func__,v_ctx->is_decoder_error, out_hdl->is_error);
		return -EIO;
	}
	if (v_ctx->is_bs_error)
		vpu_err("%s bitstream error %d", __func__, v_ctx->is_bs_error);

	if (ctx->internal_buf_cnt > 0 && v_ctx->cap_fmt.misc.bufcnt > 0 &&
		ctx->internal_buf_cnt > (v_ctx->cap_fmt.misc.bufcnt << 1)) {
		vpu_input_dbg("too many internal buffers %d, %d\n", ctx->internal_buf_cnt,
		      v_ctx->cap_fmt.misc.bufcnt);
		return -ENOSPC;
	}

	ret = ve2rpc_write_bs(out_hdl, buf, len, pts, sequence);
	if (ret) {
		vpu_input_dbg("ve2rpc_write_bs fail, ret %d\n", ret);
		return ret;
	} else {
#ifdef REORDER_PTS
		if (ctx->out_hndl->is_pts_reorder) {
			struct pts_queue *p = (struct pts_queue *)kmalloc(
				sizeof(struct pts_queue), GFP_KERNEL | __GFP_ZERO);
			if (!p) {
				vpu_err("ve2_out_qbuf malloc fail\n");
				return -ENOMEM;
			}
			p->pts = pts;
			p->idx = sequence;
			mutex_lock(&ctx->cap_hndl->pts_mutex);
			list_add_tail(&p->list, &ctx->pts_list);
			mutex_unlock(&ctx->cap_hndl->pts_mutex);
		}
#endif
		ctx->internal_buf_cnt++;
	}

	return 0;
}

static int ve2_cap_qbuf(void *fh, struct vb2_buffer *vb)
{
	struct ve2_ctx *ctx = vq_to_ve(vb->vb2_queue);
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	struct vpu_ctx *v_ctx = (struct vpu_ctx *)vid_ctx->vpu_ctx;
	struct vb2_v4l2_buffer *vb2_v4l2_buf = NULL;
	dma_addr_t cap_buf_paddr = 0;
	struct vb2_v4l2_buffer *rm_vb2_v4l2_buf = NULL;
	int ret = 0;

	if(!ctx) {
		vpu_err("%s ctx is NULL\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (!ctx->cap_hndl) {
		vpu_err("cap is uninited\n");
		ret = -EINVAL;
		goto exit;
	}

	vb2_v4l2_buf = to_vb2_v4l2_buffer(vb);

#ifdef PREPEND_METADATA
	cap_buf_paddr = vb2_dma_contig_plane_dma_addr(vb, 0) + METADATA_OFFSET;
#else
	cap_buf_paddr = vb2_dma_contig_plane_dma_addr(vb, 0);
#endif
	if (ctx->cap_hndl->dpb_cnt < vb->vb2_queue->num_buffers) {
		uint32_t cap_buf_size = 0;
		struct rtkve2_reg_dpb_t dpb = {0};

		cap_buf_size = vb2_plane_size(vb, 0);
		if (cap_buf_size == 0) {
			vpu_err("cap_buf_size is 0\n");
			ret = -EINVAL;
			goto exit;
		}

		dpb.width = v_ctx->cap_fmt.misc.ori_width;
		dpb.height = v_ctx->cap_fmt.misc.ori_height;
		dpb.dpb_width = v_ctx->cap_fmt.spec.fmt.pix_mp.width;
		dpb.dpb_height = v_ctx->cap_fmt.spec.fmt.pix_mp.height;
		dpb.size = cap_buf_size;
		dpb.y_phy_addr = (uint64_t)cap_buf_paddr;
		dpb.c_phy_addr = dpb.y_phy_addr + dpb.dpb_width * dpb.dpb_height;
		dpb.bit_depth = v_ctx->bit_depth;
		dpb.vb2_v4l2_buf = vb2_v4l2_buf;
		dpb.idx = vb->index;

		ret = ve2rpc_add_capbuf_to_dpb(
			ctx->out_hndl, ctx->cap_hndl, dpb, ctx->is_cmprs);
		if (ret != 0)
			goto exit;
	}

	rm_vb2_v4l2_buf =
		v4l2_m2m_dst_buf_remove(((struct v4l2_fh *)fh)->m2m_ctx);
	if (rm_vb2_v4l2_buf != vb2_v4l2_buf) {
		vpu_output_dbg(
			"the vb2_v4l2_buf:0x%px from v4l2_m2m_dst_buf_remove() not equals to cap_qbuf:0x%px\n",
			rm_vb2_v4l2_buf, vb2_v4l2_buf);
	}

	ret = ve2rpc_qframe(ctx->cap_hndl, cap_buf_paddr, vb->index);
	if (ret) {
		vpu_err("ve2rpc_qframe fail, ret %d\n", ret);
		goto exit;
	}

	ve2rpc_update_dpb_st(ctx->cap_hndl, vb2_v4l2_buf, RTKVE2_DPB_ST_VALID);

exit:
	return ret;
}

#ifdef VPU_GET_CC
int ve2_cc_wrapper_write(void *data, int data_size)
{
	// process cc data
	bool isCcData = false;
	char *pcSrc = (char *)data;

	short itu_t_t35_provider_code;
	char *pCCdata = pcSrc + sizeof(VIDEO_CC_CALLBACK_HEADER);
	itu_t_t35_provider_code = (short)((pCCdata[1] << 8) |
					  (pCCdata[2])); // ARM is little-endian

	switch (itu_t_t35_provider_code) {
	case 47: // Direct TV
		if (pCCdata[3] == 3) {
			isCcData = true;
		}
		break;
	case 49: // ATSC
		if (pCCdata[3] == 'G' && pCCdata[4] == 'A' &&
		    pCCdata[5] == '9' && pCCdata[6] == '4' && pCCdata[7] == 3) {
			isCcData = true;
		}
		break;
	default:
		break;
	}

	if (isCcData) {
		char ccOutputBuf[VIDEO_CC_DATA_LENGTH];
		short ccDataLen;

		// Add CC data header
		// CC MAGIC NUMBER
		ccOutputBuf[0] = 0x63;
		ccOutputBuf[1] = 0x4b;
		ccOutputBuf[2] = 0x74;
		ccOutputBuf[3] = 0x52;
		// CC data length
		ccOutputBuf[4] = pcSrc[23] - COUNTRY_CODE_OFFSET;
		ccOutputBuf[5] = pcSrc[22];
		ccOutputBuf[6] = pcSrc[21];
		ccOutputBuf[7] = pcSrc[20];
		// PTSHigh
		ccOutputBuf[8] = pcSrc[0];
		ccOutputBuf[9] = pcSrc[1];
		ccOutputBuf[10] = pcSrc[2];
		ccOutputBuf[11] = pcSrc[3];
		// PTSLow
		ccOutputBuf[12] = pcSrc[4];
		ccOutputBuf[13] = pcSrc[5];
		ccOutputBuf[14] = pcSrc[6];
		ccOutputBuf[15] = pcSrc[7];

		ccDataLen = ccOutputBuf[4];
		memcpy(&ccOutputBuf[CC_DATA_HEADER_LEN],
		       pCCdata + COUNTRY_CODE_OFFSET, ccDataLen);

		// send to ccReader
		if (cc_isCCReaderReady()) {
			cc_data_channel_send(ccOutputBuf,
					     ccDataLen + CC_DATA_HEADER_LEN,
					     cc_getCCReaderPid());
		}
	}

	return 0;
}
#endif

static int ve2_map_colorformat(struct ve2_ctx *ctx,
			    uint8_t ycbcr_enc, uint8_t xfer_func)
{
	switch(ycbcr_enc) {
	case V4L2_YCBCR_ENC_709:
	case V4L2_YCBCR_ENC_XV709:
		ctx->cap_hndl->col_matrix.matrix_coefficients = 1;
		break;
	case V4L2_YCBCR_ENC_601:
	case V4L2_YCBCR_ENC_XV601:
		ctx->cap_hndl->col_matrix.matrix_coefficients = 6;
		break;
	case V4L2_YCBCR_ENC_SMPTE240M:
		ctx->cap_hndl->col_matrix.matrix_coefficients = 7;
		break;
	case V4L2_YCBCR_ENC_BT2020:
		ctx->cap_hndl->col_matrix.matrix_coefficients = 9;
		break;
	case V4L2_YCBCR_ENC_BT2020_CONST_LUM:
		ctx->cap_hndl->col_matrix.matrix_coefficients = 10;
		break;
	case V4L2_YCBCR_ENC_DEFAULT:
	default: /* unknown */
		ctx->cap_hndl->col_matrix.matrix_coefficients = 0;
		break;
	}

	switch(xfer_func) {
	case V4L2_XFER_FUNC_SMPTE2084:
		ctx->cap_hndl->col_matrix.transfer_characteristics = 2;
		break;
#if 0  //TODO:HLG?
		ctx->cap_hndl->col_matrix.transfer_characteristics = 6;
		break;
#endif
	default:
		ctx->cap_hndl->col_matrix.transfer_characteristics = 0;
		break;
	}

	return 0;
}

static void update_color_matrix(void *fh)
{
	struct ve2_ctx *ctx = fh_to_ve(fh);
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	struct vpu_ctx *v_ctx = fh_to_vpu(fh);
	struct v4l2_ctrl_hdr10_mastering_display *mastering =
		&vid_ctx->params.mastering;

	if(!ctx || !v_ctx) {
		vpu_err("%s ctx or v_ctx is NULL\n", __func__);
		return;
	}

	if (vid_ctx->params_update) {
		ve2_map_colorformat(ctx, v_ctx->cap_fmt.spec.fmt.pix_mp.ycbcr_enc,
			v_ctx->cap_fmt.spec.fmt.pix_mp.xfer_func);

		ctx->cap_hndl->col_matrix.primary_r_chromaticity_x =
			mastering->display_primaries_x[0] * CIE1931_TO_SMPTE;
		ctx->cap_hndl->col_matrix.primary_g_chromaticity_x =
			mastering->display_primaries_x[1] * CIE1931_TO_SMPTE;
		ctx->cap_hndl->col_matrix.primary_b_chromaticity_x =
			mastering->display_primaries_x[2] * CIE1931_TO_SMPTE;
		ctx->cap_hndl->col_matrix.primary_r_chromaticity_y =
			mastering->display_primaries_y[0] * CIE1931_TO_SMPTE;
		ctx->cap_hndl->col_matrix.primary_g_chromaticity_y =
			mastering->display_primaries_y[1] * CIE1931_TO_SMPTE;
		ctx->cap_hndl->col_matrix.primary_b_chromaticity_y =
			mastering->display_primaries_y[2] * CIE1931_TO_SMPTE;
		ctx->cap_hndl->col_matrix.whitepoint_chromaticity_x =
			mastering->white_point_x * CIE1931_TO_SMPTE;
		ctx->cap_hndl->col_matrix.whitepoint_chromaticity_y =
			mastering->white_point_y * CIE1931_TO_SMPTE;
		ctx->cap_hndl->col_matrix.luminance_min =
			mastering->min_display_mastering_luminance * CANDELAS_PER_SQUARE_METER_BASE;
		ctx->cap_hndl->col_matrix.luminance_max =
			mastering->max_display_mastering_luminance * CANDELAS_PER_SQUARE_METER_BASE;
		vid_ctx->params_update = false;
	}
}

int ve2_cap_dqbuf(void *fh, uint8_t *buf, uint64_t *pts,
		  struct vb2_v4l2_buffer **disp_buf)
{
	struct ve2_ctx *ctx = fh_to_ve(fh);
	struct vpu_ctx *v_ctx = NULL;
	uint32_t no_show_frm_cnt = 0;
	int ret;
#ifdef VPU_GET_CC
	char cc_message[VIDEO_CC_DATA_LENGTH];
	int cc_len;
#endif

	if(!ctx) {
		vpu_err("%s ctx is NULL\n", __func__);
		return -EINVAL;
	}

	if (!ctx->cap_hndl) {
		vpu_err("cap is uninited\n");
		return -EINVAL;
	}

	v_ctx = fh_to_vpu(fh);
	if (v_ctx == NULL) {
		vpu_err("%s vpu_ctx is NULL\n", __func__);
		return -EINVAL;
	}
	update_color_matrix(fh);
	ret = ve2rpc_dqframe(ctx->cap_hndl, disp_buf, pts, &ctx->eos,
			     &ctx->no_frame, &no_show_frm_cnt);

	if (ret == 0) {
		ctx->internal_buf_cnt -= (no_show_frm_cnt + 1);
	} else {
		if (ret == -ENODATA)
			ctx->internal_buf_cnt -= no_show_frm_cnt;

		//ve2_cap_dqbuf fail is normal
		return ret;
	}

	vpu_output_dbg("ve2_cap_dqbuf pts %lld\n", *pts);

	// write CC data
#ifdef VPU_GET_CC
	cc_len = ve2rpc_readCcRingBuf(&ctx->out_hndl->cc_rb,
				      VIDEO_CC_DATA_LENGTH, cc_message);

	if (cc_len > 0)
		ve2_cc_wrapper_write(cc_message, cc_len);
#endif

	ve2rpc_update_dpb_st(ctx->cap_hndl,  *disp_buf, RTKVE2_DPB_ST_DQ);

	return 0;
}

static void *ve2_alloc_context(void *fh)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	struct vpu_ctx *vpu_ctx = fh_to_vpu(fh);
	struct ve2_ctx *ctx = NULL;
	VIDEO_STREAM_TYPE eStreamType = VIDEO_STREAM_H265;
	int pixelformat = vpu_ctx->out_fmt.spec.fmt.pix_mp.pixelformat;
	int ret = 0;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		vpu_err("Failed to allocate video engine\n");
		goto error;
	}

	ret = ve2rpc_init_cap_handle(vid_ctx->dev->dev, &ctx->cap_hndl,
				     vid_ctx->params.is_secure,
				     fh);
	if (ret) {
		vpu_err("ve2rpc_init_handle cap fail\n");
		goto error;
	}

	ret = ve2rpc_init_out_handle(vid_ctx->dev->dev, &ctx->out_hndl,
				     vid_ctx->params.is_secure,
				     fh);
	if (ret) {
		vpu_err("ve2rpc_init_handle out fail\n");
		goto error;
	}

	if (!ctx->out_hndl || !ctx->cap_hndl) {
		vpu_err("ve2rpc_init_handle fail\n");
		goto error;
	}

#ifdef SUPPORT_ADAPTIVE_PLAYBACK
	ctx->cap_hndl->is_adaptive_playback =
		ctx->out_hndl->is_adaptive_playback =
		vid_ctx->params.dec_params.en_adaptive_playback;
#endif

#ifdef REORDER_PTS
	ctx->cap_hndl->is_pts_reorder =
		ctx->out_hndl->is_pts_reorder =
		vid_ctx->params.dec_params.en_pts_reorder;

	if (vid_ctx->params.dec_params.en_pts_reorder) {
		ctx->cap_hndl->lastID = 0;
		mutex_init(&ctx->cap_hndl->pts_mutex);
		INIT_LIST_HEAD(&ctx->pts_list);
		ctx->cap_hndl->pts_queue = &ctx->pts_list;
		ctx->out_hndl->pts_queue = &ctx->pts_list;
	}
#endif

	ret = ve2rpc_connect(ctx->out_hndl, ctx->cap_hndl);
	if (ret) {
		vpu_err("ve2rpc_connect fail\n");
		goto error;
	}

#ifdef ENABLE_TEE_DRM_FLOW
	ret = ta_TEEapi_init(
		(struct tee_context **)&ctx->out_hndl->teeapi_ctx,
		&ctx->out_hndl->teeapi_tee_session);
	if (ret < 0) {
		vpu_err("%s ta_TEEapi_init() fail, ret:%d\n", __func__,
			ret);
		goto error;
	}

	ctx->out_hndl->main_rb.teeapi_ctx = ctx->out_hndl->teeapi_ctx;
	ctx->out_hndl->main_rb.teeapi_tee_session =
		ctx->out_hndl->teeapi_tee_session;
#endif


	if (pixelformat == V4L2_PIX_FMT_HEVC)
		eStreamType = VIDEO_STREAM_H265;
	else if (pixelformat == V4L2_PIX_FMT_VP9)
		eStreamType = VIDEO_STREAM_VP9;
	else if (pixelformat == V4L2_PIX_FMT_AV1)
		eStreamType = VIDEO_STREAM_AV1;
	else {
		vpu_err("unsupport codec! %p4cc\n", &pixelformat);
	}

	ctx->out_hndl->vType = eStreamType;
	ret = ve2rpc_setRole(ctx->out_hndl, eStreamType);
	if (ret) {
		vpu_err("ve2rpc_setRole fail %p4cc\n", &pixelformat);
		goto error;
	}

	ret = ve2rpc_enable_drop_cnt(ctx->out_hndl);
	if (ret) {
		vpu_err("ve2rpc fail to report drop cnt\n");
		goto error;
	}

	return ctx;
error:
	if (ctx)
		kfree(ctx);

	return NULL;
}

static void ve2_free_capture(void *ve_ctx)
{
	struct ve2_ctx *ctx = (struct ve2_ctx *)ve_ctx;
	int ret = 0;

	if (!ctx)
		goto exit;

	if ( !ctx->cap_hndl || !ctx->out_hndl)
		goto exit;

	if (ctx->is_run) {
		ret = ve2rpc_pause(ctx->cap_hndl);
		if (ret) {
			vpu_err("out ve2rpc_pause fail\n");
			goto exit;
		}

		ret = ve2rpc_free_travel_frame(ctx->cap_hndl);
		if (ret) {
			vpu_err("ve2rpc_free_travel_frame fail\n");
			goto exit;
		}

		ret = ve2rpc_reset_buflock(ctx->cap_hndl, true);
		if (ret) {
			vpu_err("ve2rpc_reset_buflock to UNLOCK fail\n");
			goto exit;
		}
	}

	ret = ve2rpc_del_capbuf_from_dpb(ctx->out_hndl, ctx->cap_hndl);
	if (ret) {
		vpu_err("out del_capbuf_from_dpb fail\n");
		goto exit;
	}

	if (ctx->is_run) {
		ret = ve2rpc_flush(ctx->cap_hndl);
		if (ret) {
			vpu_err("cap ve2rpc_fluse fail\n");
			goto exit;
		}
	}
exit:
	return;
}

static void ve2_free_context(void *ve_ctx)
{
	struct ve2_ctx *ctx = (struct ve2_ctx *)ve_ctx;
	int ret = 0;

	if (!ctx)
		goto exit;

	if ( !ctx->cap_hndl || !ctx->out_hndl)
		goto exit;

	if (ctx->is_run) {
		ret = ve2rpc_stop(ctx->cap_hndl);
		if (ret) {
			vpu_err("cap ve2rpc_stop fail\n");
			goto exit;
		}

		ret = ve2rpc_stop(ctx->out_hndl);
		if (ret) {
			vpu_err("out ve2rpc_stop fail\n");
			goto exit;
		}

		ret = ve2rpc_free_travel_frame(ctx->cap_hndl);
		if (ret) {
			vpu_err("ve2rpc_free_travel_frame fail\n");
			goto exit;
		}

		ret = ve2rpc_reset_buflock(ctx->cap_hndl, true);
		if (ret) {
			vpu_err("ve2rpc_reset_buflock fail\n");
			goto exit;
		}

		ctx->is_run = false;
	}

	ret = ve2rpc_close(ctx->cap_hndl);
	if (ret) {
		vpu_err("cap ve2rpc_close fail\n");
		goto exit;
	}

	ret = ve2rpc_close(ctx->out_hndl);
	if (ret) {
		vpu_err("out ve2rpc_close fail\n");
		goto exit;
	}
	ret = ve2rpc_del_capbuf_from_dpb(ctx->out_hndl, ctx->cap_hndl);
	if (ret) {
		vpu_err("out del_capbuf_from_dpb fail\n");
		goto exit;
	}

#ifdef ENABLE_TEE_DRM_FLOW
	ret = ta_TEEapi_deinit(
		(struct tee_context *)ctx->out_hndl->teeapi_ctx,
		ctx->out_hndl->teeapi_tee_session);
	if (ret < 0) {
		vpu_err("%s ta_TEEapi_deinit() fail, ret:%d\n",
			__func__, ret);
		goto exit;
	}
#endif

#ifdef REORDER_PTS
	if (ctx->cap_hndl->is_pts_reorder)
		mutex_destroy(&ctx->cap_hndl->pts_mutex);
#endif
	ret = ve2rpc_uninit_handle(ctx->out_hndl);
	if (ret) {
		vpu_err("ve2rpc_uninit_handle fail out\n");
		goto exit;
	}
	ctx->out_hndl = NULL;

	ret = ve2rpc_uninit_handle(ctx->cap_hndl);
	if (ret) {
		vpu_err("ve2rpc_uninit_handle fail out\n");
		goto exit;
	}
	ctx->cap_hndl = NULL;

exit:
	if (ctx)
		kfree(ctx);
}


static int ve2_force_eos(void *fh, struct ve2rpc *cap_hndl)
{
	const struct v4l2_event eos_event = { .type = V4L2_EVENT_EOS };
	struct vb2_v4l2_buffer *buf = NULL;
	int i = 0;

	mutex_lock(&cap_hndl->dpb_mutex);
	for (i = VE2_MAX_DPB_NUM - 1; i >= 0; i--) {
		struct vb2_v4l2_buffer *tmp = (struct vb2_v4l2_buffer *)cap_hndl->dpb[i].vb2_v4l2_buf;
		if (cap_hndl->dpb[i].status == RTKVE2_DPB_ST_VALID &&
				tmp->vb2_buf.state == VB2_BUF_STATE_ACTIVE) {
			buf = cap_hndl->dpb[i].vb2_v4l2_buf;
			break;
		}
	}

	if (i == -1) {
		vpu_err("Can't find valid buffer for EOS\n");
		mutex_unlock(&cap_hndl->dpb_mutex);
		return -ENOBUFS;
	}
	mutex_unlock(&cap_hndl->dpb_mutex);

	vb2_set_plane_payload(&buf->vb2_buf, 0, 0);
	v4l2_m2m_last_buffer_done(((struct v4l2_fh *)fh)->m2m_ctx, buf);
	v4l2_event_queue_fh(fh, &eos_event);

	return 0;
}

static int ve2_stop_cmd(void *fh, int pixelformat)
{
	struct ve2_ctx *ctx = NULL;
	struct ve2rpc *hndl = NULL;
	struct vpu_ctx *v_ctx = NULL;
	int ret = 0;

	if (!fh) {
		vpu_err("%s invalid parameters\n", __func__);
		goto out;
	}
	ctx = fh_to_ve(fh);
	if (!ctx) {
		vpu_err("%s ctx is NULL\n", __func__);
		goto out;
	}

	hndl = ctx->out_hndl;
	if (!hndl) {
		vpu_err("%s hndl is NULL\n", __func__);
		goto out;
	}

	v_ctx = fh_to_vpu(fh);
	if (v_ctx == NULL) {
		vpu_err("%s v_ctx is NULL\n", __func__);
		goto out;
	}

	if (ctx->eosEvent == 0) {
		if (v_ctx->out_fmt.spec.fmt.pix_mp.pixelformat ==
		    V4L2_PIX_FMT_HEVC)
			ve2rpc_inband_eos_event(&hndl->sub_rb,
						hndl->main_rb.pRBH, 2);
		else
			ve2rpc_inband_eos_event(&hndl->sub_rb,
						hndl->main_rb.pRBH, 0);

		if(hndl->is_error || v_ctx->is_bs_error || v_ctx->is_decoder_error)
			ve2_force_eos(fh, ctx->cap_hndl);

		ctx->eosEvent = 1;
	}

out:
	return ret;
}

static int ve2_start_cmd(void *fh)
{
	struct ve2_ctx *ctx = NULL;
	int ret = 0;

	if (!fh) {
		vpu_err("%s invalid parameters\n", __func__);
		goto out;
	}
	ctx = fh_to_ve(fh);
	if (!ctx) {
		vpu_err("%s ctx is NULL\n", __func__);
		goto out;
	}

	ctx->eosEvent = 0;
out:
	return ret;
}

static void ve2_get_info(void *fh, bool *eos, bool *no_frame)
{
	struct ve2_ctx *ctx = fh_to_ve(fh);

	if(!ctx) {
		vpu_err("%s ctx is NULL\n", __func__);
		return;
	}

	*eos = ctx->eos;
	*no_frame = ctx->no_frame;
}

static int ve2_get_undq_dispFrm_cnt(void *fh)
{
	struct ve2_ctx *ctx = fh_to_ve(fh);

	if(!ctx) {
		vpu_err("%s ctx is NULL\n", __func__);
		return -EINVAL;
	}

	return ve2rpc_get_decoded_frm_cnt(ctx->cap_hndl);
}

static int ve2_pasre_header(void *fh, struct vb2_buffer *vb, uint32_t *width,
			    uint32_t *height, uint32_t *min_reqbuf,
			    uint32_t *bit_depth)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	struct vpu_ctx *v_ctx = (struct vpu_ctx *)vid_ctx->vpu_ctx;
	struct ve2_ctx *ctx = fh_to_ve(fh);
	struct vb2_v4l2_buffer *v4l2_buf;
	uint32_t size = 0;
	uint32_t ddr_width = 0;
	uint32_t ddr_height = 0;
	void *buf = NULL;
	int ret;

	v4l2_buf = to_vb2_v4l2_buffer(vb);
	size = v4l2_buf->vb2_buf.planes[0].bytesused;
	buf = vb2_plane_vaddr(&v4l2_buf->vb2_buf, 0);

	ret = ve2rpc_get_bs_info(vb->vb2_queue->dev, fh,
				 v_ctx->out_fmt.spec.fmt.pix_mp.pixelformat, size,
				 buf, width, height, &ddr_width, &ddr_height,
				 min_reqbuf, bit_depth);

	if (!ret && *bit_depth == 0x80000000 && *min_reqbuf == 0) {
		v_ctx->is_bs_error = true;
		vpu_err("ve2_pasre_header: bitstream keyframe error!\n");
		ret = -EFAULT;
		goto exit;
	} else if (!ret && *bit_depth == 0x40000000 && *min_reqbuf == 0) {
		v_ctx->is_decoder_error = true;
		vpu_err("ve2_pasre_header: Codec not support!\n");
		ret = -EFAULT;
		goto exit;
	} else if (ret ||
		(*width == 0 && *height == 0 &&
		ddr_width == 0 && ddr_height == 0)) {
		vpu_err("ve2_pasre_header ve2rpc_get_bs_info fail %d\n", ret);
		ret = -EFAULT;
		goto exit;
	}

	vpu_update_resolution_change(fh, *width, *height, ddr_width, ddr_height,
		*bit_depth, *min_reqbuf);

	if (vid_ctx->params.dec_params.en_enhance == true &&
		(v_ctx->cap_fmt.misc.ori_width * v_ctx->cap_fmt.misc.ori_height >
			MAX_2K_RESOLUTION)) {
		ctx->is_cmprs = true;
		vpu_info("Enable enhance mode\n");
	}

	v_ctx->is_bs_error = false;
	ctx->internal_buf_cnt = 0;
exit:
	return ret;
}

static struct veng_ops ve_ops = {
	.ve_start_streaming = ve2_start_streaming,
	.ve_stop_streaming = ve2_stop_streaming,
	.ve_out_qbuf = ve2_out_qbuf,
	.ve_cap_qbuf = ve2_cap_qbuf,
	.ve_cap_dqbuf = ve2_cap_dqbuf,
	.ve_abort = ve2_abort,
	.ve_alloc_context = ve2_alloc_context,
	.ve_free_context = ve2_free_context,
	.ve_free_capture = ve2_free_capture,
	.ve_stop_cmd = ve2_stop_cmd,
	.ve_get_info = ve2_get_info,
	.ve_get_undq_dispFrm_cnt = ve2_get_undq_dispFrm_cnt,
	.ve_out_pre_parse = ve2_pasre_header,
	.ve_start_cmd = ve2_start_cmd,
};

static int __init ve2_init(void)
{
	int ret;

	ret = vpu_ve_register(VENG_ID, &ve_ops);
	if (ret) {
		vpu_err("Failed to register video engine VE%d ops\n", VENG_ID);
		return ret;
	}

	return 0;
}

static void __exit ve2_exit(void)
{
	vpu_ve_unregister(VENG_ID);
}

module_init(ve2_init);
module_exit(ve2_exit);

#if IS_ENABLED(CONFIG_RTK_V4L2_DECODER)
#if IS_MODULE(CONFIG_RTK_FW_REMOTEPROC)
MODULE_SOFTDEP("pre: rtk_fw_remoteproc");
#endif
#endif

MODULE_VERSION(xstr(GIT_VERSION));
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("William Lee <william.lee@realtek.com>");
MODULE_DESCRIPTION("V4L2 Realtek Video Engine 2 Codec Driver");
