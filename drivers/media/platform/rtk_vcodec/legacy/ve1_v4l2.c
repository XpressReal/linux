
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-event.h>

#include "drv_if.h"
#include "vpu.h"
#include "ve1_v4l2.h"
#include "ve1_mem.h"
#include "debug.h"
#include "ve1_wrapper.h"
#include "ve1_vpuapi.h"

extern const struct ve1_ctx_ops ve1_decode_ops;
extern int ve1_fill_bitstream(struct ve1_ctx *ctx, uint8_t *buf, uint32_t len,
			      uint64_t timestamp, uint32_t sequence);
extern void ve1_show_displayable_frame_list(struct ve1_ctx *ctx);
#ifdef VPU_GET_CC
extern void ProcessCC_Display(void *ctx, long long PTS, int display_index);
#endif
#define VENG_ID 1

#define HAS_REG_DPBS(ctx) \
		((((struct ve1_ctx *)ctx)->fbAllocInfo) != NULL)

/*
 * Return vpu_ctx structure for a given struct v4l2_fh
 */
static struct vpu_ctx *fh_to_vpu(struct v4l2_fh *fh)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	return vid_ctx->vpu_ctx;
}

/*
 * Return ve1_ctx structure for a given struct v4l2_fh
 */
static struct ve1_ctx *fh_to_ve(struct v4l2_fh *fh)
{
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);
	return vid_ctx->ve_ctx;
}

/*
 * Return ve1_ctx structure for a given struct vb2_queue
 */
static struct ve1_ctx *vq_to_ve(struct vb2_queue *q)
{
	struct videc_ctx *vid_ctx = vb2_get_drv_priv(q);
	return vid_ctx->ve_ctx;
}

static void ve1_pic_run_work(struct work_struct *work)
{
	struct ve1_ctx *ctx = container_of(work, struct ve1_ctx, pic_run_work);
	int ret;
	bool queueRet = false;

	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx is NULL\n");
		return;
	}

	mutex_lock(&ctx->ve1_mutex);

	ctx->cntExecPicRunWork++;

	if (ctx->bFlush) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"do thing due to flushing\n");
		goto out;
	}

	// RDK-1479, while entering ve1_pic_run_work(), maybe seq_end_work is already done and ve1DecState is VE1_STATE_DEC_UNINIT, it should not start to decode
	if (ctx->ve1DecState < VE1_STATE_DEC_SEQ_INIT_DONE) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"exit ve1_pic_run_work due to ve1DecState:%d\n",
			ctx->ve1DecState);
		goto out;
	}

	if ((ctx->ve1DecState == VE1_STATE_DEC_SEQ_INIT_DONE) &&
	    (ctx->streamon_cap == 0)) {
		ve1_info(VE1_LOGTAG, "seq int done but not streamon_cap\n");
	}

	if (ctx->free_cap) {
		ctx->free_cap = 0;
		rtkve1_unreg_dpbs((void *)ctx);
		//ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
		//	"goto out\n");
		//goto out;
	}

	if (!HAS_REG_DPBS(ctx) && (ctx->capReqBufsCnt != 0) && (ctx->regFbCount == 0)) {
		ret = rtkve1_register_dpbs((void *)ctx);
		if (ret < 0) {
			ve1_err(VE1_LOGTAG,
				"[-] rtkve1_register_dpbs() fail.ret:%d\n", ret);
			goto out;
		}
		ctx->ve1DecState = VE1_STATE_DEC_SET_DPB;
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "set ve1DecState:%d\n",
		ctx->ve1DecState);
	}
	else if (HAS_REG_DPBS(ctx)) {
		ret = rtkve1_check_new_dpb((void *)ctx);
		if (ret != 0) {
			ve1_err(VE1_LOGTAG, "rtkve1_check_new_dpb() fail.ret:%d\n",ret);
		}
	}

	if (ctx->ve1DecState < VE1_STATE_DEC_SET_DPB) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"exit ve1_pic_run_work due to ve1DecState:%d\n",
			ctx->ve1DecState);
		goto out;
	}

	ret = ctx->ops->prepare_run(ctx);
	if (ret < 0) {
		goto out;
	}

	// if bBufEmptyFlag and no more out_qbuf (streamEnd), "queue_work pic_run_work" won't triggered in VE1_DecUpdateBS(), do it here
	if (ctx->bBufEmptyFlag && ctx->streamEnd) {
		queueRet = queue_work(ctx->workqueue, &ctx->pic_run_work);
		if (queueRet)
			ctx->cntQueuePicRunWorkOk++;
		else
			ctx->cntQueuePicRunWorkFail++;
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"queue_work pic_run_work.cnt(%d,%d).ret:%d\n",
			ctx->cntQueuePicRunWorkOk, ctx->cntQueuePicRunWorkFail,
			queueRet);
	}

	if ((ctx->ve1DecState == VE1_STATE_DEC_START_DEC_ISSUED) &&
	    (!ctx->bBufEmptyFlag) && (!ctx->bWaitNextField)) {
		ret = VE1_DecWaitPicDone(ctx);
		if (ret > 0) {
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "finish_run\n");
			ret = ctx->ops->finish_run(ctx);
			if (ret < 0) {
				ve1_err(VE1_LOGTAG,
					"af finish_run.fatal error.no more decode\n");
			} else if (ret) {
				queueRet = queue_work(ctx->workqueue,
						      &ctx->pic_run_work);
				if (queueRet)
					ctx->cntQueuePicRunWorkOk++;
				else
					ctx->cntQueuePicRunWorkFail++;
				ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
					"queue_work pic_run_work.cnt(%d,%d).ret:%d\n",
					ctx->cntQueuePicRunWorkOk,
					ctx->cntQueuePicRunWorkFail, queueRet);
			}
		}
		if (ctx->bWaitNextField) {
			queueRet =
				queue_work(ctx->workqueue, &ctx->pic_run_work);
			if (queueRet)
				ctx->cntQueuePicRunWorkOk++;
			else
				ctx->cntQueuePicRunWorkFail++;
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"queue_work pic_run_work.cnt(%d,%d).ret:%d\n",
				ctx->cntQueuePicRunWorkOk,
				ctx->cntQueuePicRunWorkFail, queueRet);
		}
	}

	if (ctx->aborting &&
	    ctx->ops->seq_end_work) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "queue_work seq_end_work\n");
		queue_work(ctx->workqueue, &ctx->seq_end_work);
	}

out:
	mutex_unlock(&ctx->ve1_mutex);
}

static int ve1_start_streaming(struct vb2_queue *q, uint32_t count,
			       int pixelformat)
{
	int ret = 0;
	struct ve1_ctx *ctx;
	struct ve1_decopen_param open_param;
	struct videc_ctx *vid_ctx = vb2_get_drv_priv(q);
	struct vpu_ctx *vpu_ctx = vid_ctx->vpu_ctx;
	bool queueRet = false;

	if (q == NULL) {
		ve1_err(VE1_LOGTAG, "q is NULL\n");
		return -EINVAL;
	}
	ctx = vq_to_ve(q);
	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx is NULL\n");
		return -EINVAL;
	}

	if (vid_ctx == NULL) {
		pr_err("%s [%d]%s.vid_ctx is NULL\n", VE1_LOGTAG, __LINE__,
		       __func__);
		return -EINVAL;
	}
	ctx->pdev = vid_ctx->dev->dev;

	ve1_dbg(VPU_DBG_INPUT, VE1_LOGTAG,
		"[+] ctx:0x%px.type:%s.count:%d.pixelformat:%4s.is_secure:%d.memory:%d.pdev:0x%px\n",
		ctx, V4L2_TYPE_TO_STR(q->type), count, (char *)&pixelformat,
		vid_ctx->params.is_secure, q->memory, ctx->pdev);

	if (!ctx->ops) // ve1 hasn't been inited
	{
		// set struct ve1_ctx_ops
		ctx->ops = &ve1_decode_ops;
		// allocate workqueue
		ctx->workqueue = alloc_workqueue(
			"v4l2_ve1", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
		if (!ctx->workqueue) {
			ve1_err(VE1_LOGTAG, "[-] unable to alloc workqueue\n");
			return -ENOMEM;
		}

		// init a struct work_struct for pic_run_work
		INIT_WORK(&ctx->pic_run_work, ve1_pic_run_work);
		if (ctx->ops->seq_init_work) {
			// init a struct work_struct for seq_init_work
			INIT_WORK(&ctx->seq_init_work, ctx->ops->seq_init_work);
		}
		if (ctx->ops->seq_end_work) {
			// init a struct work_struct for seq_end_work
			INIT_WORK(&ctx->seq_end_work, ctx->ops->seq_end_work);
		}

		// init struct mutex ve1_mutex
		mutex_init(&ctx->ve1_mutex);

		// init struct mutex ve1_dma_mutex
		mutex_init(&ctx->ve1_dma_mutex);

		// init struct list_head buffer_meta_list
		INIT_LIST_HEAD(&ctx->buffer_meta_list);
		// init spinlock_t buffer_meta_lock
		spin_lock_init(&ctx->buffer_meta_lock);

		// init struct list_head displayable_frame_list
		INIT_LIST_HEAD(&ctx->displayable_frame_list);
		// init spinlock_t displayable_frame_lock
		spin_lock_init(&ctx->displayable_frame_lock);
	}

	if (V4L2_TYPE_IS_OUTPUT(q->type) && (ctx->streamon_out == 1)) {
		ve1_err(VE1_LOGTAG, "[-] OUTPUT is already stream on\n");
		return -EPERM;
	} else if (!V4L2_TYPE_IS_OUTPUT(q->type) && (ctx->streamon_cap == 1)) {
		ve1_err(VE1_LOGTAG, "[-] CAPTURE is already stream on\n");
		return -EPERM;
	}

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		// ctx->ops->start_streaming(ctx) => ve1_start_decoding() => ve1_alloc_bitstream_buffer(), ve1_alloc_bitstream_buffer() needs ctx->is_svp
		mutex_lock(&ctx->ve1_mutex);
		ctx->is_svp = vid_ctx->params.is_secure;
		ctx->out_vb2_q_memory = q->memory;
		if (!ctx->is_svp &&
		    ctx->out_vb2_q_memory == V4L2_MEMORY_DMABUF) {
			ret = -EPERM;
			ve1_err(VE1_LOGTAG,
				"[-] not support nonsvp + OUTPUT memory is V4L2_MEMORY_DMABUF\n");
			goto out;
		}
		mutex_unlock(&ctx->ve1_mutex);
	}

	ret = ctx->ops->start_streaming(ctx);
	if (ret < 0) {
		ve1_err(VE1_LOGTAG, "[-] start_streaming() fail.ret:%d\n", ret);
		return ret;
	}

	mutex_lock(&ctx->ve1_mutex);
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"ve1DecState:%d.is_svp:%d.out_vb2_q_memory:%d\n",
			ctx->ve1DecState, ctx->is_svp, ctx->out_vb2_q_memory);

		if (ctx->ve1DecState == VE1_STATE_DEC_UNINIT) {
			ret = VE1_DecInit(ctx, vid_ctx->dev);
			if (ret < 0) {
				ve1_err(VE1_LOGTAG,
					"[-] VE1_DecInit() fail.ret:%d\n", ret);
				goto out;
			}

			memset(&open_param, 0,
			       sizeof(struct ve1_decopen_param));
			open_param.src_fmt_fourcc = pixelformat;
			// FIXME, at this time, vpu_ctx->cap.fmt.pix.pixelformat hasn't been set (it will be set on vpu_g_fmt() or vpu_s_fmt_cap())
			// and ve1 can't access the default value (cap_fmt[] in vpu.c), so set V4L2_PIX_FMT_NV12 directly
			open_param.dst_fmt_fourcc = V4L2_PIX_FMT_NV12;
			open_param.width = vpu_ctx->rect.width;
			open_param.height = vpu_ctx->rect.height;
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"src_fmt_fourcc:%4s.dst_fmt_fourcc:%4s\n",
				(char *)&open_param.src_fmt_fourcc,
				(char *)&open_param.dst_fmt_fourcc);
			ret = VE1_DecOpen(ctx, (void *)&open_param);
			if (ret < 0) {
				ve1_err(VE1_LOGTAG,
					"[-] VE1_DecOpen() fail.ret:%d\n", ret);
				goto out;
			}
		}

		ctx->streamon_out = 1;
		ctx->bNewBsDumpFile = 1; // #if defined(RTKVE1_DUMP_BS_EN) in ve1_wrapper.c
	} else {
		if (ctx->seqInited) {
			ctx->capReqBufsCnt = vid_ctx->reqbuf_cap;
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"capReqBufsCnt:%d\n",
				ctx->capReqBufsCnt);

			queueRet =
				queue_work(ctx->workqueue, &ctx->pic_run_work);
			if (queueRet)
				ctx->cntQueuePicRunWorkOk++;
			else
				ctx->cntQueuePicRunWorkFail++;
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"queue_work pic_run_work.cnt(%d,%d).ret:%d\n",
				ctx->cntQueuePicRunWorkOk,
				ctx->cntQueuePicRunWorkFail, queueRet);
		} else if (ctx->ve1DecState < VE1_STATE_DEC_SEQ_INIT_DONE) {
			ve1_info(VE1_LOGTAG,
				 "streamon_cap but not seq init done\n");
		}
		ctx->streamon_cap = 1;
		ctx->bNewYuvDumpFile = 1; // #if defined(RTKVE1_DUMP_YUV_EN) in ve1_wrapper.c
	}
	mutex_unlock(&ctx->ve1_mutex);
	ve1_dbg(VPU_DBG_INPUT, VE1_LOGTAG, "[-] type:%s\n",
		V4L2_TYPE_TO_STR(q->type));
	return 0;
out:
	mutex_unlock(&ctx->ve1_mutex);
	return ret;
}

static int ve1_stop_streaming(struct vb2_queue *q)
{
	struct ve1_ctx *ctx;
	struct ve1_meta *meta;
	struct ve1_displayable_frame *frame;
	unsigned long flags;
	//int i = 0;
	//struct vb2_buffer *vb2 = NULL;
	//dma_addr_t cap_buf_paddr;

	ve1_dbg(VPU_DBG_INPUT, VE1_LOGTAG, "[+] type:%s\n",
		V4L2_TYPE_TO_STR(q->type));

	if (q == NULL) {
		ve1_err(VE1_LOGTAG, "q is NULL\n");
		return -EINVAL;
	}
	ctx = vq_to_ve(q);
	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx is NULL\n");
		return -EINVAL;
	}

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		rtkve1_flush_bitstream((void *)ctx);

		mutex_lock(&ctx->ve1_mutex);
		spin_lock_irqsave(&ctx->buffer_meta_lock, flags);
		while (!list_empty(&ctx->buffer_meta_list)) {
			meta = list_first_entry(&ctx->buffer_meta_list,
						struct ve1_meta, list);
			list_del(&meta->list);
			kfree(meta);
		}
		ctx->num_metas = 0;
		spin_unlock_irqrestore(&ctx->buffer_meta_lock, flags);

		ctx->cntOutQbuf = 0;
		ctx->streamon_out = 0;
		ctx->outbuf_sequence = 0;
		mutex_unlock(&ctx->ve1_mutex);
	} else {
		rtkve1_flush((void *)ctx);
/*
		for (i=0;i<q->num_buffers;i++) {
			vb2 = q->bufs[i];
			cap_buf_paddr = vb2_dma_contig_plane_dma_addr(vb2, 0);
			ve1_dbg(VPU_DBG_OUTPUT, VE1_LOGTAG,
				"cap_vb2[%d].index:%d.state:%d.paddr:0x%lx.vb2_v4l2_buf:0x%px\n",i,
				vb2->index,
				vb2->state,
				cap_buf_paddr,
				to_vb2_v4l2_buffer(vb2));
		}
*/
		// remove all displayable frame info in ctx->displayable_frame_list
		spin_lock_irqsave(&ctx->displayable_frame_lock, flags);
		while (!list_empty(&ctx->displayable_frame_list)) {
			frame = list_first_entry(&ctx->displayable_frame_list,
						 struct ve1_displayable_frame,
						 list);
			list_del(&frame->list);
			kfree(frame);
		}
		spin_unlock_irqrestore(&ctx->displayable_frame_lock, flags);
		rtkve1_flush_dpbs((void *)ctx);

		mutex_lock(&ctx->ve1_mutex);
		ctx->cntAddToList = 0;
		ctx->cntFrameDq = 0;
		ctx->last_frame = 0;
		ctx->streamon_cap = 0;
		ctx->capbuf_sequence = 0;
		ctx->lastFrmReportedAfFrmDqSeqNo = 0;
		ctx->lastDqCapBuf = NULL;
		mutex_unlock(&ctx->ve1_mutex);
		mutex_lock(&ctx->ve1_dma_mutex);
		ctx->lastDoneCapBuf = NULL;
		mutex_unlock(&ctx->ve1_dma_mutex);
	}

	ve1_dbg(VPU_DBG_INPUT, VE1_LOGTAG,
		"[-] type:%s.streamon_out(%d,%d).streamEnd:%d.seqInited:%d\n",
		V4L2_TYPE_TO_STR(q->type), ctx->streamon_out, ctx->streamon_cap,
		ctx->streamEnd,ctx->seqInited);
	return 0;
}

// ve1_start_work() will be called only by ve1_out_qbuf() or ve1_cap_qbuf()
static void ve1_start_work(void *fh, struct ve1_ctx *ctx)
{
	bool queueRet = false;
	struct v4l2_rect rect;
	struct vpu_ctx *vpu_ctx;

	if (!ctx->seqInited && ctx->ops->seq_init_work) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "queue_work seq_init_work\n");
		queue_work(ctx->workqueue, &ctx->seq_init_work);
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "flush_work seq_init_work\n");
		flush_work(&ctx->seq_init_work);
		if (ctx->seqInited) {
			struct ve1_parsed_initial_info info;
			struct vpu_fmt vpu_fmt;

			mutex_lock(&ctx->ve1_mutex);
			VE1_GetParsedInfo(ctx, &info);
			mutex_unlock(&ctx->ve1_mutex);
			ve1_info(VE1_LOGTAG,
				"after seqInited.currSequenceNo:%d.get pic:%dx%d.display_rect(%d,%d,%d,%d) minDpbCount=%u\n",
				ctx->currSequenceNo, info.pic_width,
				info.pic_height, info.visible_rect_left,
				info.visible_rect_top, info.visible_rect_w,
				info.visible_rect_h, info.minDpbCount);
			vpu_get_cap_fmt(fh, (void *)&vpu_fmt);
			vpu_fmt.spec.fmt.pix_mp.width = info.pic_width;
			vpu_fmt.spec.fmt.pix_mp.height = info.pic_height;
			vpu_fmt.spec.fmt.pix_mp.plane_fmt[0].bytesperline = info.pic_width;
			vpu_fmt.misc.bufcnt = info.minDpbCount;
			vpu_update_cap_fmt(fh, (void *)&vpu_fmt);
			vpu_ctx = fh_to_vpu(fh);
			if (vpu_ctx->out_fmt.spec.fmt.pix_mp.pixelformat == V4L2_PIX_FMT_H264 &&
				(info.visible_rect_w != 0) &&
				(info.visible_rect_h != 0)) {
				rect.top = info.visible_rect_top;
				rect.left = info.visible_rect_left;
				rect.width = info.visible_rect_w;
				rect.height = info.visible_rect_h;
				vpu_update_rect(fh, &rect);
			}
#ifdef ENABLE_SHOW_VIDEO_INFO
			vpu_keep_fm_info(info.pic_width, info.pic_height);
#endif // #ifdef ENABLE_SHOW_VIDEO_INFO
			if (vpu_check_sub_res_chg(fh)) {
				ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
					"vpu_notify_event_resolution_change.currSequenceNo:%d\n",
					ctx->currSequenceNo);
				vpu_notify_event_resolution_change(fh);
			}
		}
	}
	// !ctx->seqChangeDone means seq_init_work() executed (ctx->seqChangeDone is set to 0 in ve1_dec_seq_init_work())
	if (ctx->seqInited && !ctx->startDecode && !ctx->seqChangeDone) {
		mutex_lock(&ctx->ve1_mutex);
		ctx->startDecode = 1;
		queueRet = queue_work(ctx->workqueue, &ctx->pic_run_work);
		if (queueRet)
			ctx->cntQueuePicRunWorkOk++;
		else
			ctx->cntQueuePicRunWorkFail++;
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"queue_work pic_run_work.cnt(%d,%d).ret:%d\n",
			ctx->cntQueuePicRunWorkOk, ctx->cntQueuePicRunWorkFail,
			queueRet);
		mutex_unlock(&ctx->ve1_mutex);
	}
	else if (ctx->seqInited && ctx->startDecode && ctx->bPostponeUpBs)
	{
		queueRet = queue_work(ctx->workqueue, &ctx->pic_run_work);
		if (queueRet)
			ctx->cntQueuePicRunWorkOk++;
		else
			ctx->cntQueuePicRunWorkFail++;
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"queue_work pic_run_work.cnt(%d,%d).ret:%d\n",
			ctx->cntQueuePicRunWorkOk, ctx->cntQueuePicRunWorkFail,
			queueRet);
		flush_work(&ctx->pic_run_work);
	}
}

#ifdef PREPEND_METADATA
#define PLOCK_VERSION (0x72746B3D) //'rtk13'
static void ve1_fill_display_frame_info(struct ve1_displayable_frame *frame,
                                        void *captureBuf,
                                        unsigned int secure_flag)
{
        struct ve_frame_info *info = (struct ve_frame_info *)captureBuf;
        unsigned long long pts[2];

        info->yuvs.lumaOffTblAddr = 0xffffffff;
        info->yuvs.chromaOffTblAddr = 0xffffffff;
        info->yuvs.lumaOffTblAddrR = 0xffffffff;
        info->yuvs.chromaOffTblAddrR = 0xffffffff;
        info->yuvs.bufBitDepth = 8;
        info->yuvs.matrix_coefficients = 1;
        info->yuvs.tch_hdr_metadata[0] = -1;

        info->yuvs.Y_addr_Right = 0xffffffff;
        info->yuvs.U_addr_Right = 0xffffffff;
        info->yuvs.pLock_Right = 0xffffffff;

        info->rtk_meta_buf_id = 0x52544B6D; //RTKm
        info->is_ve1_buf = 1;

        info->yuvs.Y_addr = frame->Y_addr;
        info->yuvs.U_addr = frame->U_addr;
        info->yuvs.Y_pitch = frame->bufStride;
        info->yuvs.C_pitch = frame->bufStride;
        info->yuvs.slice_height = frame->bufHeight;
        info->yuvs.width = frame->rectRight - frame->rectLeft;
        ;
        info->yuvs.height = frame->rectBottom - frame->rectTop;
        info->yuvs.mode = frame->mode;

        if (frame->mode == INTERLEAVED_BOT_TOP_FIELD ||
            frame->mode == INTERLEAVED_TOP_BOT_FIELD) {
                // interlace
                pts[1] = div_u64((frame->timestamp * 9), 100);
                if (pts[1] > (frame->timeTick >> 1)) {
                        pts[0] = pts[1] - (frame->timeTick >> 1);
                } else {
                        pts[0] = 0;
                }
        } else {
                // progressive
                pts[0] = div_u64((frame->timestamp * 9), 100);
                pts[1] = 0;
        }

        info->yuvs.PTSH2 = (unsigned int)(pts[1] >> 32);
        info->yuvs.RPTSH2 = (unsigned int)(pts[1] >> 32);
        info->yuvs.PTSL2 = (unsigned int)(pts[1] & 0xffffffffLL);
        info->yuvs.RPTSL2 = (unsigned int)(pts[1] & 0xffffffffLL);
        info->yuvs.PTSH = (unsigned int)(pts[0] >> 32);
        info->yuvs.PTSL = (unsigned int)(pts[0] & 0xffffffffLL);
        info->yuvs.RPTSH = (unsigned int)(pts[0] >> 32);
        info->yuvs.RPTSL = (unsigned int)(pts[0] & 0xffffffffLL);

        info->yuvs.video_full_range_flag = frame->video_full_range_flag;
        info->yuvs.matrix_coefficients =
                ((frame->matrix_coefficients > 0) ? frame->matrix_coefficients :
                                                    1);
        if (frame->transfer_characteristics == 18) {
                info->yuvs.transferCharacteristics = 6;
        } else if (frame->transfer_characteristics == 14 ||
                   frame->transfer_characteristics == 15) {
                info->yuvs.transferCharacteristics = 1;
        } else if (frame->transfer_characteristics == 16) {
                info->yuvs.transferCharacteristics = 2;
        } else {
                info->yuvs.transferCharacteristics = 0;
        }

        if ((info->yuvs.transferCharacteristics == 1) ||
            (info->yuvs.transferCharacteristics == 2)) {
                /*HDR 10*/
                info->hdr_type = 2;
        } else if (info->yuvs.transferCharacteristics == 6) {
                /*HLG*/
                info->hdr_type = 3;
        } else {
                info->hdr_type = 0;
        }

        info->yuvs.secure_flag = secure_flag;

        ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
                "tgid(%d,%d,%s).idx:%d.Y_addr:0x%x.U_addr:0x%x.bufW:%d.bufH:%d.w:%d.h:%d.pts(%lld,%lld).hdr(%d,%d,%d)\n",
                current->tgid, current->pid, current->comm,
                frame->frameBufIndex, info->yuvs.Y_addr, info->yuvs.U_addr,
                info->yuvs.Y_pitch, info->yuvs.slice_height, info->yuvs.width,
                info->yuvs.height, pts[0], pts[1],
                info->yuvs.video_full_range_flag,
                info->yuvs.transferCharacteristics,
                info->yuvs.matrix_coefficients);
}
#endif

static int ve1_out_qbuf(void *fh, uint8_t *buf, uint32_t len, uint64_t timestamp,
			uint32_t sequence)
{
	int ret = 0;
	struct ve1_ctx *ctx;
	DecInitialInfo *initialInfo;

	if ((fh == NULL) || (buf == NULL)) {
		ve1_err(VE1_LOGTAG, "fh or buf == NULL\n");
		return -EINVAL;
	}
	ctx = fh_to_ve(fh);
	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx is NULL\n");
		return -EINVAL;
	}

	if (ctx->error) {
		ve1_err(VE1_LOGTAG,
			"Something wrong, skip output qbuf\n");
		return -EIO;
	}

	ve1_dbg(VPU_DBG_INPUT, VE1_LOGTAG,
		"[+] fh:0x%px.ctx:0x%px.buf:0x%px.len:%d.timestamp:%lld.seq:%d.cnt:%d\n",
		fh, ctx, buf, len, timestamp, sequence, ctx->cntOutQbuf);

	if (ctx->initialInfo != NULL) {
		initialInfo = (DecInitialInfo *)ctx->initialInfo;
		if (initialInfo->seqInitErrReason == 0x40000) {
			ve1_err(VE1_LOGTAG, "Don't support extended profile\n");
			return -EINVAL;
		}
		if (initialInfo->seqInitErrReason == 0x20000) {
			ve1_err(VE1_LOGTAG, "the image size exceeds the supported limits\n");
			return -EINVAL;
		}
	}

	if (ctx->streamon_out == 1) {
		mutex_lock(&ctx->ve1_mutex);
		ret = ve1_fill_bitstream(ctx, buf, len, timestamp, sequence);
		if (ret == -EFAULT) {
			// real error
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"[-] ve1_fill_bitstream() fail.ret:%d\n", ret);
			mutex_unlock(&ctx->ve1_mutex);
			return ret;
		}
		else if (ret == 0) {
			ctx->cntOutQbuf++;
		}
		mutex_unlock(&ctx->ve1_mutex);

		ve1_start_work(fh, ctx);
	}

	// ret will be 0 or -ENOSPC, -ENOSPC means bitstream buffer doesn't have enough space to put new data
	//ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[-] ret:%d\n", ret);
	return ret;
}

static int ve1_cap_qbuf(void *fh, struct vb2_buffer *vb)
{
	struct ve1_ctx *ctx;
	struct vpu_ctx *vpu_ctx;
	unsigned long flags;
	struct ve1_displayable_frame *frame, *tmp;
	bool bSetFbReuse = false;
	PhysicalAddress dpb_paddr = 0;
	unsigned int regIndex;
	unsigned int sequenceNo;
	bool queueRet = false;
	unsigned long cap_buf_size = 0;
	dma_addr_t cap_buf_paddr;
	struct vb2_v4l2_buffer *vb2_v4l2_buf = NULL;
	struct vb2_v4l2_buffer *rm_vb2_v4l2_buf = NULL;
	int ret = 0;

	if (vb == NULL) {
		ve1_err(VE1_LOGTAG, "vb is NULL\n");
		return -EINVAL;
	}
	ctx = vq_to_ve(vb->vb2_queue);
	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx is NULL\n");
		return -EINVAL;
	}
	vpu_ctx = fh_to_vpu(fh);
	if (vpu_ctx == NULL) {
		ve1_err(VE1_LOGTAG, "vpu_ctx is NULL\n");
		return -EINVAL;
	}

	vb2_v4l2_buf = to_vb2_v4l2_buffer(vb);
	ve1_dbg(VPU_DBG_OUTPUT, VE1_LOGTAG,
		"[+] ctx:0x%px.flag(%d,%d,%d,%d).vb2_v4l2_buf:0x%px\n", ctx,
		ctx->dpbFull, ctx->streamEnd, ctx->seqInited, ctx->startDecode,
		vb2_v4l2_buf);

	if (ctx->streamon_cap == 1) {
		// ctx->streamEnd means EOS already happened, ve1_out_qbuf() won't be called anymore, we need to queue_work(seq_init_work or pic_run_work) by ve1_cap_qbuf()
		// CXI-3465, test_00.ts, from log, all video data have feed already before sequence change, after sequence changed, ve1_out_qbuf() won't be called anymore,
		// we need to queue_work(seq_init_work or pic_run_work) by ve1_cap_qbuf()
		if (ctx->streamEnd || !ctx->seqInited || !ctx->startDecode) {
			ve1_start_work(fh, ctx);
		}
	}

	if (HAS_REG_DPBS(ctx)) {
		// print displayable_frame_list for debug
		ve1_show_displayable_frame_list(ctx);

		// check if there is a corresponding frame buffer need to be recycled
		spin_lock_irqsave(&ctx->displayable_frame_lock, flags);
		if (!list_empty(&ctx->displayable_frame_list)) {
			list_for_each_entry_safe (frame, tmp,
						  &ctx->displayable_frame_list,
						  list) {
				if (frame->vb2_v4l2_buf == vb2_v4l2_buf) {
					if (frame->isDequeued == 0) {
						ve1_err(VE1_LOGTAG,
							"Unexpected fatal error.queue a never-dequeued cap_buf\n");
					}
					ve1_dbg(VPU_DBG_OUTPUT, VE1_LOGTAG,
						"vb2_v4l2_buf:0x%px re-qbuf.VE1_UpdateDPBStatus(0x%x,%d,%d)\n",
						frame->vb2_v4l2_buf,
						frame->dpb_paddr,
						frame->sequenceNo,
						frame->regIndex);
					dpb_paddr = frame->dpb_paddr;
					regIndex = frame->regIndex;
					sequenceNo = frame->sequenceNo;
					bSetFbReuse = true;
					list_del(&frame->list);
					kfree(frame);
					break;
				}
			}
		}
		spin_unlock_irqrestore(&ctx->displayable_frame_lock, flags);
		if (bSetFbReuse) {
			rm_vb2_v4l2_buf = v4l2_m2m_dst_buf_remove(
				((struct v4l2_fh *)fh)->m2m_ctx);
			if (rm_vb2_v4l2_buf != vb2_v4l2_buf) {
				ve1_err(VE1_LOGTAG,
					"the vb2_v4l2_buf:0x%px from v4l2_m2m_dst_buf_remove() not equals to cap_qbuf:0x%px\n",
					rm_vb2_v4l2_buf, vb2_v4l2_buf);
			}
			VE1_UpdateDPBStatus((void *)ctx, dpb_paddr, sequenceNo,
								RTKVE1_DPB_ST_VALID);
		} else {
			cap_buf_size = vb2_plane_size(vb, 0);
			if (cap_buf_size == 0) {
				ve1_err(VE1_LOGTAG, "cap_buf_size is 0\n");
				rm_vb2_v4l2_buf = v4l2_m2m_dst_buf_remove(
									((struct v4l2_fh *)fh)->m2m_ctx);
				if (rm_vb2_v4l2_buf != vb2_v4l2_buf) {
					ve1_err(VE1_LOGTAG,
						"the vb2_v4l2_buf:0x%px from v4l2_m2m_dst_buf_remove() not equals to cap_qbuf:0x%px\n",
						rm_vb2_v4l2_buf, vb2_v4l2_buf);
				}
				ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
					"v4l2_m2m_buf_done.vb2_v4l2_buf:0x%px\n",
					vb2_v4l2_buf);
				v4l2_m2m_buf_done(
					vb2_v4l2_buf,
					VB2_BUF_STATE_ERROR);
				return -EINVAL;
			}
#ifdef PREPEND_METADATA
			cap_buf_paddr = vb2_dma_contig_plane_dma_addr(vb, 0) + METADATA_OFFSET;
#else
			cap_buf_paddr = vb2_dma_contig_plane_dma_addr(vb, 0);
#endif
			//ve1_dbg(VPU_DBG_OUTPUT, VE1_LOGTAG,
			//	"cap_buf_paddr:0x%lx.cap_buf_size:%ld.vb2_v4l2_buf:0x%px\n",
			//	(unsigned long)cap_buf_paddr, cap_buf_size,
			//	vb2_v4l2_buf);

			ret = VE1_UpdateDPBStatus((void *)ctx, cap_buf_paddr, ctx->currSequenceNo,
										RTKVE1_DPB_ST_VALID);
			if (ret < 0) {
				if ((!ctx->streamon_cap) && (ctx->capReqBufsCnt != 0) && (ctx->cntCap2Dpb == ctx->capReqBufsCnt)) {
					ve1_err(VE1_LOGTAG, "unexpected cap_qbuf which not in dpb[].cntCap2Dpb:%d\n",
						ctx->cntCap2Dpb);
					rm_vb2_v4l2_buf = v4l2_m2m_dst_buf_remove(
										((struct v4l2_fh *)fh)->m2m_ctx);
					if (rm_vb2_v4l2_buf != vb2_v4l2_buf) {
						ve1_err(VE1_LOGTAG,
							"the vb2_v4l2_buf:0x%px from v4l2_m2m_dst_buf_remove() not equals to cap_qbuf:0x%px\n",
							rm_vb2_v4l2_buf, vb2_v4l2_buf);
					}
					ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
						"v4l2_m2m_buf_done.vb2_v4l2_buf:0x%px\n",
						vb2_v4l2_buf);
					v4l2_m2m_buf_done(
						vb2_v4l2_buf,
						VB2_BUF_STATE_ERROR);
					return -EINVAL;
				}
				else {
					rtkve1_add_capbuf_to_dpb((void *)ctx, cap_buf_size,
											(unsigned long)cap_buf_paddr,
											vb2_v4l2_buf);
				}
			}

			rm_vb2_v4l2_buf = v4l2_m2m_dst_buf_remove(
								((struct v4l2_fh *)fh)->m2m_ctx);
			if (rm_vb2_v4l2_buf != vb2_v4l2_buf) {
				ve1_err(VE1_LOGTAG,
					"the vb2_v4l2_buf:0x%px from v4l2_m2m_dst_buf_remove() not equals to cap_qbuf:0x%px\n",
					rm_vb2_v4l2_buf, vb2_v4l2_buf);
			}
		}

		// CXI-3663, queue_work pic_run_work after frame buffer is recycled
		if (ctx->dpbFull) {
			queueRet =
				queue_work(ctx->workqueue, &ctx->pic_run_work);
			if (queueRet)
				ctx->cntQueuePicRunWorkOk++;
			else
				ctx->cntQueuePicRunWorkFail++;
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"queue_work pic_run_work.cnt(%d,%d).ret:%d\n",
				ctx->cntQueuePicRunWorkOk,
				ctx->cntQueuePicRunWorkFail, queueRet);
		}
	} else {
		cap_buf_size = vb2_plane_size(vb, 0);
		if (cap_buf_size == 0) {
			ve1_err(VE1_LOGTAG, "cap_buf_size is 0\n");
			return -EINVAL;
		}
#ifdef PREPEND_METADATA
		cap_buf_paddr = vb2_dma_contig_plane_dma_addr(vb, 0) + METADATA_OFFSET;
#else
		cap_buf_paddr = vb2_dma_contig_plane_dma_addr(vb, 0);
#endif
		//ve1_dbg(VPU_DBG_OUTPUT, VE1_LOGTAG,
		//	"cap_buf_paddr:0x%lx.cap_buf_size:%ld.vb2_v4l2_buf:0x%px\n",
		//	(unsigned long)cap_buf_paddr, cap_buf_size,
		//	vb2_v4l2_buf);
		rtkve1_add_capbuf_to_dpb((void *)ctx, cap_buf_size,
								(unsigned long)cap_buf_paddr,
								vb2_v4l2_buf);
		rm_vb2_v4l2_buf = v4l2_m2m_dst_buf_remove(
			((struct v4l2_fh *)fh)->m2m_ctx);
		if (rm_vb2_v4l2_buf != vb2_v4l2_buf) {
			ve1_err(VE1_LOGTAG,
				"the vb2_v4l2_buf:0x%px from v4l2_m2m_dst_buf_remove() not equals to cap_qbuf:0x%px\n",
				rm_vb2_v4l2_buf, vb2_v4l2_buf);
		}
	}

	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[-]\n");
	return 0;
}

static int ve1_cap_dqbuf(void *fh, uint8_t *buf, uint64_t *timestamp,
			 struct vb2_v4l2_buffer **disp_buf)
{
	struct ve1_ctx *ctx;
	struct vpu_ctx *vpu_ctx;
	unsigned long flags;
	struct ve1_displayable_frame *frame;
	bool bFillFrameInfo = false;
	PhysicalAddress dpb_paddr = 0;
	unsigned int regIndex;
	unsigned int sequenceNo;
	int ret = -EINVAL;
	//unsigned int unDqFrmCount = 0;
	//int lastFrmFlag = 0;
	struct rtkve1_dpb_t *dpb_undq = NULL;

	if ((fh == NULL) || (timestamp == NULL) || (disp_buf == NULL)) {
		ve1_err(VE1_LOGTAG, "invalid parameters\n");
		return ret;
	}
	ctx = fh_to_ve(fh);
	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx is NULL\n");
		return ret;
	}
	vpu_ctx = fh_to_vpu(fh);
	if (vpu_ctx == NULL) {
		ve1_err(VE1_LOGTAG, "vpu_ctx is NULL\n");
		return -EINVAL;
	}

	//ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[+]\n");

	if (ctx->streamon_cap == 1) {
		// print displayable_frame_list for debug
		//ve1_show_displayable_frame_list(ctx);
/*
		if ((ctx->handle_eos_by >= VE1_HANDLE_EOS_SET_END) && (ctx->last_frame == 0)) {
			spin_lock_irqsave(&ctx->displayable_frame_lock, flags);
			if (!list_empty(&ctx->displayable_frame_list)) {
				list_for_each_entry (
					frame, &ctx->displayable_frame_list, list) {
					if ((frame->vb2_v4l2_buf != NULL) &&
						(frame->isDequeued == 0)) {
						unDqFrmCount++;
					}
					if ((frame->vb2_v4l2_buf != NULL) &&
						(frame->last_frame == 1)) {
						lastFrmFlag = 1;
					}
				}
			}
			spin_unlock_irqrestore(&ctx->displayable_frame_lock, flags);
			//ve1_dbg(VPU_DBG_OUTPUT, VE1_LOGTAG,
			//		"unDqFrmCount:%d.lastFrmFlag:%d\n",unDqFrmCount,lastFrmFlag);
			if ((unDqFrmCount == 1) && (lastFrmFlag == 0)) {
				ve1_dbg(VPU_DBG_OUTPUT, VE1_LOGTAG,
					"streamEnd + unDqFrmCount==1 + !lastFrmFlag, wait ve1 report last frame\n");
				return -EINVAL;
			}
		}
*/
		spin_lock_irqsave(&ctx->displayable_frame_lock, flags);
		if (!list_empty(&ctx->displayable_frame_list)) {
			list_for_each_entry (
				frame, &ctx->displayable_frame_list, list) {
#ifdef PREPEND_METADATA
				void *captureBuf = NULL;
#endif
				if ((frame->vb2_v4l2_buf != NULL) &&
				    (frame->isDequeued == 0)) {
					*timestamp = frame->timestamp;

					if (frame->last_frame) {
						ve1_info(
							VE1_LOGTAG,
							"ctx->last_frame = 1\n");
						ctx->last_frame = 1;
					}
					ctx->cntFrameDq++;
					ve1_dbg(VPU_DBG_OUTPUT, VE1_LOGTAG,
						"dq vb2_v4l2_buf:0x%px.regIndex:%d.dpb_paddr:0x%x.isDequeued:%d.timestamp:%lld.POC:%d.last_frame:%d.cnt:%d\n",
						frame->vb2_v4l2_buf,
						frame->regIndex,
						frame->dpb_paddr,
						frame->isDequeued,
						frame->timestamp,
						frame->POC,
						ctx->last_frame,
						ctx->cntFrameDq);
#ifdef PREPEND_METADATA
					captureBuf = vb2_plane_vaddr(&frame->vb2_v4l2_buf->vb2_buf, 0);
					if (captureBuf)
						ve1_fill_display_frame_info(
							frame, captureBuf, ctx->is_svp);
					else
						ve1_err(VE1_LOGTAG, "captureBuf is NULL\n");
#endif
					frame->isDequeued = 1;
					*disp_buf = frame->vb2_v4l2_buf;
					dpb_paddr = frame->dpb_paddr;
					regIndex = frame->regIndex;
					sequenceNo = frame->sequenceNo;
					bFillFrameInfo = true;
					if ((frame->POC != 0) && (frame->POC < ctx->lastDispPOC)) {
						ve1_dbg(VPU_DBG_OUTPUT, VE1_LOGTAG,
							"incorrect POC.curr:%d.last:%d\n",frame->POC,ctx->lastDispPOC);
					}
					ctx->lastDispPOC = frame->POC;
					ctx->lastDqCapBuf = frame->vb2_v4l2_buf;
					break;
				}
			}
		}

		spin_unlock_irqrestore(&ctx->displayable_frame_lock, flags);
		if (bFillFrameInfo) {
#ifdef VPU_GET_CC
			// CXI-3837, VE1 send a displayable frame info to CAPTURE buffer, call ProcessCC_Display() here
			ProcessCC_Display((void *)ctx, frame->timestamp,
					  regIndex);
#endif
			VE1_UpdateDPBStatus((void *)ctx, dpb_paddr, sequenceNo,
								RTKVE1_DPB_ST_DQ);
			ret = 0;
		}
		else if (ctx->lastFrmReportedAfFrmDqSeqNo) {
			mutex_lock(&ctx->ve1_dma_mutex);
			if (!ctx->lastDoneCapBuf) {
				dpb_undq = (struct rtkve1_dpb_t *)rtkve1_find_dpb_undequeue((void *)ctx, ctx->lastFrmReportedAfFrmDqSeqNo);
				if (dpb_undq) {
					*disp_buf = (struct vb2_v4l2_buffer *)(dpb_undq->vb2_v4l2_buf);
					ctx->lastDoneCapBuf = *disp_buf;
					ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
						"lastFrmReportedAfFrmDqSeqNo.let flow to vpu_buf_done() in threadcap().undq_v4l2_buf:0x%px.lastDqCapBuf:0x%px\n",
						*disp_buf, ctx->lastDqCapBuf);
					// return 0 to let flow to ve_get_info(get no_frame==1) => vpu_buf_done() in threadcap() (vpu.c)
					ret = 0;
				}
			}
			mutex_unlock(&ctx->ve1_dma_mutex);
		}
	}

	//ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[-] ret:%d\n", ret);
	return ret;
}

static int ve1_abort(void *pCtx, int type)
{
	struct ve1_ctx *ctx;
	if (!pCtx) {
		ve1_err(VE1_LOGTAG, "pCtx is NULL\n");
		return -EINVAL;
	}
	ctx = (struct ve1_ctx *)pCtx;

	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[+] ctx:0x%px.type:%s\n", ctx,
		V4L2_TYPE_TO_STR(type));

	mutex_lock(&ctx->ve1_mutex);
	ctx->aborting = 1;
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
		"VE1_DecCheckComplete\n");
	VE1_DecCheckComplete(ctx);
	mutex_unlock(&ctx->ve1_mutex);

	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[-] ctx:0x%px.type:%s\n", ctx,
		V4L2_TYPE_TO_STR(type));
	return 0;
}

static void *ve1_alloc_context(void *fh)
{
	struct ve1_ctx *ctx = NULL;
	struct videc_ctx *vid_ctx = container_of(fh, struct videc_ctx, fh);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ve1_err(VE1_LOGTAG, "Failed to allocate video engine\n");
		return ctx;
	}
	ctx->filp = vid_ctx->file;
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "ctx:0x%px.filp:0x%px\n", ctx,
		ctx->filp);

	return ctx;
}

static void ve1_free_context(void *pCtx)
{
	struct ve1_ctx *ctx;
	int i = 0;
	struct vb2_v4l2_buffer *vb2_v4l2_buf = NULL;

	if (!pCtx) {
		ve1_err(VE1_LOGTAG, "pCtx is NULL\n");
		return;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->ops && ctx->ops->seq_end_work) {
		// queue seq_end_work on workqueue
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "queue_work seq_end_work\n");
		queue_work(ctx->workqueue, &ctx->seq_end_work);
		// Wait until seq_end_work has finished execution
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "flush_work seq_end_work\n");
		flush_work(&ctx->seq_end_work);
	}

	mutex_lock(&ctx->ve1_dma_mutex);
	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		if (IS_RTKVE1_DPB_VALID(ctx->dpb[i].status)) {
			vb2_v4l2_buf = (struct vb2_v4l2_buffer *)(ctx->dpb[i].vb2_v4l2_buf);
			if (vb2_v4l2_buf->vb2_buf.state == VB2_BUF_STATE_ACTIVE) {
				ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
					"v4l2_m2m_buf_done.status:0x%x.regIndex:%d.vb2_v4l2_buf:0x%px.phys_addr:0x%lx.seqNo:%u\n",
					ctx->dpb[i].status,
					ctx->dpb[i].regIndex,
					ctx->dpb[i].vb2_v4l2_buf,
					ctx->dpb[i].phys_addr,
					ctx->dpb[i].seqNo);
				v4l2_m2m_buf_done(
					(struct vb2_v4l2_buffer *)(ctx->dpb[i].vb2_v4l2_buf),
					VB2_BUF_STATE_ERROR);
			}
		}
	}
	mutex_unlock(&ctx->ve1_dma_mutex);

	if (ctx->workqueue) {
		// destroy workqueue
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"destroy_workqueue workqueue:0x%px\n", ctx->workqueue);
		destroy_workqueue(ctx->workqueue);
		ctx->workqueue = NULL;
	}

	if (ctx->ops && ctx->ops->release) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "ops->release\n");
		ctx->ops->release(ctx);
	}

	if (ctx->ops)
		mutex_lock(&ctx->ve1_mutex);

	if (ctx->decOP) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "kfree decOP\n");
		kfree(ctx->decOP);
		ctx->decOP = NULL;
	}
	if (ctx->initialInfo) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "kfree initialInfo\n");
		kfree(ctx->initialInfo);
		ctx->initialInfo = NULL;
	}
	if (ctx->fbAllocInfo) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "kfree fbAllocInfo\n");
		kfree(ctx->fbAllocInfo);
		ctx->fbAllocInfo = NULL;
	}
	if (ctx->fbUser) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "kfree fbUser\n");
		kfree(ctx->fbUser);
		ctx->fbUser = NULL;
	}
	if (ctx->decParam) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "kfree decParam\n");
		kfree(ctx->decParam);
		ctx->decParam = NULL;
	}
	if (ctx->secAxiUse) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "kfree secAxiUse\n");
		kfree(ctx->secAxiUse);
		ctx->secAxiUse = NULL;
	}
	if (ctx->decCacheConfig) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "kfree decCacheConfig\n");
		kfree(ctx->decCacheConfig);
		ctx->decCacheConfig = NULL;
	}
	if (ctx->outputInfo) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "kfree outputInfo\n");
		kfree(ctx->outputInfo);
		ctx->outputInfo = NULL;
	}
	if (ctx->seqHeader) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "kfree seqHeader\n");
		kfree(ctx->seqHeader);
		ctx->seqHeader = NULL;
	}
	if (ctx->picHeader) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "kfree picHeader\n");
		kfree(ctx->picHeader);
		ctx->picHeader = NULL;
	}

	if (ctx->ops)
		mutex_unlock(&ctx->ve1_mutex);

	kfree(ctx);
}

static void ve1_free_capture(void *pCtx)
{
	struct ve1_ctx *ctx;
	if (!pCtx) {
		ve1_err(VE1_LOGTAG, "pCtx is NULL\n");
		return;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->streamon_cap) {
		ve1_err(VE1_LOGTAG, "fatal.reqbufs 0 while cap streamon\n");
		return;
	}

	ctx->cntCap2Dpb = 0;
	//rtkve1_unreg_dpbs(pCtx);
	ctx->free_cap = 1;
}

static int ve1_force_eos(void *fh, struct ve1_ctx *ctx)
{
	const struct v4l2_event eos_event = { .type = V4L2_EVENT_EOS };
	struct v4l2_m2m_ctx *m2m_ctx = ((struct v4l2_fh *)fh)->m2m_ctx;
	struct vb2_v4l2_buffer *buf = NULL;
	int i = 0;


	mutex_lock(&ctx->ve1_dma_mutex);
	for (i = VE1_ION_STRUCT_NUM - 1; i >= 0; i--) {
		struct vb2_v4l2_buffer *tmp = (struct vb2_v4l2_buffer *)ctx->dpb[i].vb2_v4l2_buf;
		if (ctx->dpb[i].status == RTKVE1_DPB_ST_VALID &&
				tmp->vb2_buf.state == VB2_BUF_STATE_ACTIVE) {
			buf = ctx->dpb[i].vb2_v4l2_buf;
			break;
		}
	}

	if (i == -1) {
		vpu_err("Can't find valid buffer for EOS\n");
		mutex_unlock(&ctx->ve1_dma_mutex);
		return -ENOBUFS;
	}
	mutex_unlock(&ctx->ve1_dma_mutex);

	vb2_set_plane_payload(&buf->vb2_buf, 0, 0);
	v4l2_m2m_last_buffer_done(m2m_ctx, buf);
	v4l2_event_queue_fh(fh, &eos_event);

	return 0;
}

static int ve1_stop_cmd(void *fh, int pixelformat)
{
	int ret = 0;
	struct ve1_ctx *ctx = NULL;
	struct vpu_ctx *vpu_ctx;
	bool queueRet = false;

	if (fh == NULL) {
		ve1_err(VE1_LOGTAG, "invalid parameters\n");
		return -1;
	}
	ctx = fh_to_ve(fh);
	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx is NULL\n");
		return -1;
	}
	vpu_ctx = fh_to_vpu(fh);
	if (vpu_ctx == NULL) {
		ve1_err(VE1_LOGTAG, "vpu_ctx is NULL\n");
		return -1;
	}

	mutex_lock(&ctx->ve1_mutex);
	if (ctx->int_reason & (1 << INT_BIT_DEC_FIELD)) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "eos with interrupt reason:0x%x\n",
				ctx->int_reason);
		ret = -1;
		goto out;
	}

	if (!ctx->seqInited) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"eos but not seq init done\n");
		ret = -1;
		goto out;
	}
	if (ctx->streamon_out &&
	    (ctx->handle_eos_by == VE1_HANDLE_EOS_BY_NONE)) {
		ctx->handle_eos_by = VE1_HANDLE_EOS_BY_PREPARE_RUN;
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "set handle_eos_by:%d\n",
			ctx->handle_eos_by);
		if (ctx->bBufEmptyFlag) {
			ctx->bBufEmptyFlag = false;
			queueRet =
				queue_work(ctx->workqueue, &ctx->pic_run_work);
			if (queueRet)
				ctx->cntQueuePicRunWorkOk++;
			else
				ctx->cntQueuePicRunWorkFail++;
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"queue_work pic_run_work.cnt(%d,%d).ret:%d\n",
				ctx->cntQueuePicRunWorkOk,
				ctx->cntQueuePicRunWorkFail, queueRet);
		}

		if (ctx->error)
			ve1_force_eos(fh, ctx);
	}

out:
	mutex_unlock(&ctx->ve1_mutex);
	return ret;
}

static int ve1_start_cmd(void *fh)
{
	int ret = 0;
	struct ve1_ctx *ctx = NULL;

	if (fh == NULL) {
		ve1_err(VE1_LOGTAG, "invalid parameters\n");
		return -1;
	}
	ctx = fh_to_ve(fh);
	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx is NULL\n");
		return -1;
	}

	mutex_lock(&ctx->ve1_mutex);
	ve1_info(VE1_LOGTAG, "%s.%d\n",
				__func__, __LINE__);
	ctx->handle_eos_by = VE1_HANDLE_EOS_BY_NONE;
	ctx->last_frame = 0;

	mutex_unlock(&ctx->ve1_mutex);
	return ret;
}

static void ve1_get_info(void *fh, bool *eos, bool *no_frame)
{
	struct ve1_ctx *ctx;

	if ((fh == NULL) || (eos == NULL)) {
		ve1_err(VE1_LOGTAG, "invalid parameters\n");
		return;
	}
	ctx = fh_to_ve(fh);
	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx is NULL\n");
		return;
	}

	*eos = (bool)ctx->last_frame;
	*no_frame = ctx->lastFrmReportedAfFrmDqSeqNo;
}

static int ve1_get_undq_dispFrm_cnt(void *fh)
{
	int cnt = 0;
	struct ve1_ctx *ctx;
	unsigned long flags;
	struct ve1_displayable_frame *frame;

	if (fh == NULL) {
		ve1_err(VE1_LOGTAG, "invalid parameters\n");
		goto out;
	}
	ctx = fh_to_ve(fh);
	if (ctx == NULL) {
		goto out;
	}

	spin_lock_irqsave(&ctx->displayable_frame_lock, flags);
	if (!list_empty(&ctx->displayable_frame_list)) {
		list_for_each_entry (frame, &ctx->displayable_frame_list,
				     list) {
			if (frame->isDequeued == 0) {
				cnt++;
			}
		}
	}
	spin_unlock_irqrestore(&ctx->displayable_frame_lock, flags);

out:
	return cnt;
}

static struct veng_ops ve_ops = {
	.ve_start_streaming = ve1_start_streaming,
	.ve_stop_streaming = ve1_stop_streaming,
	.ve_out_qbuf = ve1_out_qbuf,
	.ve_cap_qbuf = ve1_cap_qbuf,
	.ve_cap_dqbuf = ve1_cap_dqbuf,
	.ve_abort = ve1_abort,
	.ve_alloc_context = ve1_alloc_context,
	.ve_free_context = ve1_free_context,
	.ve_free_capture = ve1_free_capture,
	.ve_stop_cmd = ve1_stop_cmd,
	.ve_get_info = ve1_get_info,
	.ve_get_undq_dispFrm_cnt = ve1_get_undq_dispFrm_cnt,
	.ve_start_cmd = ve1_start_cmd,
};

static int __init ve1_init(void)
{
	int ret;
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[+]\n");

	ret = vpu_ve_register(VENG_ID, &ve_ops);
	if (ret) {
		ve1_err(VE1_LOGTAG, "[-] vpu_ve_register() fail\n");
		return ret;
	}

	ret = ve1_mem_device_create();
	if (ret) {
		ve1_err(VE1_LOGTAG, "[-] ve1_mem_device_create() fail\n");
		return ret;
	}

	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[-]\n");
	return 0;
}

static void __exit ve1_exit(void)
{
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[+]\n");
	ve1_mem_device_destroy();

	vpu_ve_unregister(VENG_ID);
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[-]\n");
}

module_init(ve1_init);
module_exit(ve1_exit);

MODULE_VERSION(xstr(GIT_VERSION));
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Scarly.Cheng <scarly.cheng@realtek.com>");
MODULE_DESCRIPTION("V4L2 Realtek Video Engine 1 Codec Driver");
