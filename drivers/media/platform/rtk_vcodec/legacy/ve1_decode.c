
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "ve1_v4l2.h"
#include "debug.h"
#include "vpu.h"
#include "ve1_wrapper.h"
#include "ve1_vpuapi.h"

#define VE1_BITSTREAM_BUFFER_SIZE (8 * 1024 * 1024)
#ifdef VPU_GET_CC
extern int ve1_get_userdata(struct ve1_ctx *ctx, unsigned char *pBuf,
			    unsigned int nBufSize);
extern void ProcessCC(struct ve1_ctx *ctx, unsigned char *cc_buf,
		      unsigned int cc_size, long long PTS, int decode_index,
		      int display_index, int codec_type);
extern bool cc_isCCReaderReady(void);
#endif
static void ve1_set_stream_end(struct ve1_ctx *ctx)
{
	int ret;
	if ((ctx != NULL) && (ctx->decHandle != NULL) &&
	    (ctx->streamEnd == 0)) {
		ctx->streamEnd = 1;
		ret = VE1_SetStreamEnd(ctx);
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"EOS.af VE1_SetStreamEnd.ret:%d.size:0.accuBsFeedBytes:%d\n",
			ret, ctx->accuBsFeedBytes);
	}
}

int ve1_fill_bitstream(struct ve1_ctx *ctx, uint8_t *buf, uint32_t len,
		       uint64_t timestamp, uint32_t sequence)
{
	struct ve1_meta *meta;
	unsigned long flags;
	unsigned long curBsWrPtr = 0;
	int ret = 0;

	// check if bitstream buffer has available space to put new data
	if (ve1_get_bitstream_payload(ctx) + len >= ctx->bitstream.size) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "ENOSPC.payload:%d + len:%d >= bs_bufsize:%d\n",
			ve1_get_bitstream_payload(ctx), len, ctx->bitstream.size);
		return -ENOSPC;
	}
	if (ctx->decHandle != NULL) {
		VE1_DecGetRdWrPtr(ctx);
		if (ctx->vpuBsRingRoom <= len) {
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "ENOSPC.vpuBsRingRoom:%d <= len:%d\n",
				ctx->vpuBsRingRoom, len);
			return -ENOSPC;
		}
	}
	if (ctx->bGotNextField) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"ENOSPC.bWaitNextField.only update next field before continue decoding\n");
		return -ENOSPC;
	}
	if (ctx->bPostponeUpBs) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "ENOSPC.bPostponeUpBs.postpone fill bs\n");
		return -ENOSPC;
	}
	if (ctx->dpbFull && (!ctx->bWaitNextField)) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "ENOSPC.dpb full.postpone fill bs\n");
		return -ENOSPC;
	}
	if (ctx->seqInited && (ctx->ve1DecState == VE1_STATE_DEC_SEQ_INIT_DONE)) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "ENOSPC.no update bs until SET_FB\n");
		return -ENOSPC;
	}

	curBsWrPtr = ctx->bsWrPtr;

	ret = VE1_DecUpdateBS((void *)ctx, buf, len);
	if (ret < 0) {
		ve1_err(VE1_LOGTAG,
			"VE1_DecUpdateBS() fail.ret:%d.ctx:0x%px.buf:0x%px.len:%d\n",
			ret, ctx, buf, len);
		return -EFAULT;
	} else {
		meta = kmalloc(sizeof(*meta), GFP_KERNEL);
		if (meta) {
			meta->sequence = sequence;
			meta->timestamp = timestamp;
			meta->start = curBsWrPtr;
			meta->end = ctx->bsWrPtr;
			spin_lock_irqsave(&ctx->buffer_meta_lock, flags);
			list_add_tail(&meta->list, &ctx->buffer_meta_list);
			ctx->num_metas++;
			spin_unlock_irqrestore(&ctx->buffer_meta_lock, flags);
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"num_metas:%d.sequence:%d.len:%d.timestamp:%lld.start:0x%x.end:0x%x\n",
				ctx->num_metas, sequence, len, timestamp, meta->start,
				meta->end);
		}
	}

	return 0;
}

static void ve1_seq_end_work(struct work_struct *work)
{
	struct ve1_ctx *ctx = container_of(work, struct ve1_ctx, seq_end_work);
	int ret;

	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[+]\n");

	mutex_lock(&ctx->ve1_mutex);

	if (ctx->ve1DecState < VE1_STATE_DEC_OPENED) {
		goto out;
	}

	ret = VE1_DecClose(ctx);
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
		"af VE1_DecClose.ret:%d\n", ret);

	ret = VE1_DecDeInit(ctx);
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
		"af VE1_DecDeInit.ret:%d\n", ret);

	ctx->seqInited = 0;
	ctx->handle_eos_by = VE1_HANDLE_EOS_BY_NONE;
	ctx->last_frame = 0;

	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[-]\n");
out:
	mutex_unlock(&ctx->ve1_mutex);
}

static int ve1_alloc_bitstream_buffer(struct ve1_ctx *ctx)
{
	int ret = 0;
	unsigned long virt_addr = 0;
	if (ctx->bitstream.paddr) {
		return 0;
	}

	ctx->bitstream.size = VE1_BITSTREAM_BUFFER_SIZE;
	ret = VE1_AllocateBitstreamBuffer(ctx->pdev, &ctx->bitstream.paddr,
					  &virt_addr, ctx->bitstream.size,
					  ctx->is_svp);
	if (ret < 0) {
		VE1_FreeBitstreamBuffer(ctx->pdev,
					(unsigned long)ctx->bitstream.vaddr,
					ctx->bitstream.paddr,
					ctx->bitstream.size);
		ve1_err(VE1_LOGTAG,
			"ctx:0x%px.VE1_AllocateBitstreamBuffer() fail.ret:%d\n",
			ctx, ret);
		return -ENOMEM;
	}
	ctx->totIonAllocatedBytes += ctx->bitstream.size;
	ctx->bitstream.vaddr = (void *)virt_addr;
	ve1_info(
		VE1_LOGTAG,
		"ctx:0x%px.VE1_AllocateBitstreamBuffer() ok.bitstream(0x%px,%lx,%lx,%d).tot:%d\n",
		ctx, ctx->bitstream.vaddr, ctx->bitstream.paddr,
		ctx->bitstream.dma_buf, ctx->bitstream.size,
		ctx->totIonAllocatedBytes);

	ctx->bsRdPtr = ctx->bsWrPtr = ctx->bitstream.paddr;

	return 0;
}

static void ve1_free_bitstream_buffer(struct ve1_ctx *ctx)
{
	if (ctx->bitstream.paddr == 0) {
		return;
	}

	VE1_FreeBitstreamBuffer(ctx->pdev, (unsigned long)ctx->bitstream.vaddr,
				ctx->bitstream.paddr, ctx->bitstream.size);
	ctx->totIonAllocatedBytes -= ctx->bitstream.size;
	memset(&ctx->bitstream, 0, sizeof(struct ve1_buf));
	ve1_info(VE1_LOGTAG, "ctx:0x%px.VE1_FreeBitstreamBuffer() ok.tot:%d\n",
		 ctx, ctx->totIonAllocatedBytes);

	ctx->bsRdPtr = ctx->bsWrPtr = 0;
}

static int rtkve1_get_meta(struct ve1_ctx *ctx, struct ve1_meta *result)
{
	int ret = 0;
	struct ve1_meta *meta = NULL;
	unsigned long flags;

	if ((ctx == NULL) || (result == NULL)) {
		ve1_err(VE1_LOGTAG, "ctx == NULL || result == NULL\n");
		return -1;
	}

	spin_lock_irqsave(&ctx->buffer_meta_lock, flags);
	if (!list_empty(&ctx->buffer_meta_list)) {
		meta = list_first_entry(&ctx->buffer_meta_list,
				struct ve1_meta, list);
		*result = *meta;
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"timestamp:0x%lld.start:0x%x.end:0x%x.num_metas:%d.ve1DecState:%d.vpuRdPtr:0x%x\n",
			meta->timestamp,
			meta->start,
			meta->end,
			ctx->num_metas,
			ctx->ve1DecState,
			ctx->vpuRdPtr);
		if ((ctx->ve1DecState <= VE1_STATE_DEC_SEQ_INIT_DONE) && (ctx->vpuRdPtr != meta->end)) {
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"processing seq init, vpuRdPtr:0x%x != fill_bs_end:0x%x, bs may contain frame, let decoding to consume meta\n",
				ctx->vpuRdPtr,
				meta->end);
		}
		else {
			ctx->num_metas--;
			list_del(&meta->list);
			kfree(meta);
			meta = NULL;
		}
	} else {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "buffer_meta_list is empty\n");
		ret = -1;
	}
	spin_unlock_irqrestore(&ctx->buffer_meta_lock, flags);

	return ret;
}

static int __ve1_decoder_seq_init(struct ve1_ctx *ctx)
{
	int ret;
	struct ve1_meta meta;

	lockdep_assert_held(&ctx->ve1_mutex);

	if (ctx == NULL || ctx->decHandle == NULL) {
		ve1_err(VE1_LOGTAG, "ctx == NULL || ctx->decHandle == NULL\n");
		return -EINVAL;
	}

	ret = VE1_DecSeqInit(ctx);
	if (ret < 0) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "VE1_DecSeqInit() fail\n");
		ret = -EINVAL;
	}
	else {
		ret = 0;
	}

	// consume ve1_meta of out_buf which used to parse headers
	rtkve1_get_meta(ctx, &meta);

	return ret;
}

static void ve1_dec_seq_init_work(struct work_struct *work)
{
	struct ve1_ctx *ctx = container_of(work, struct ve1_ctx, seq_init_work);
	int ret;

	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[+]\n");

	mutex_lock(&ctx->ve1_mutex);

	if (ctx->seqInited == 1) {
		goto out;
	}

	ret = __ve1_decoder_seq_init(ctx);
	if (ret < 0) {
		//ve1_info(VE1_LOGTAG, "__ve1_decoder_seq_init() fail.ret:%d\n",
		//	 ret);
		goto out;
	}
	ctx->seqChangeDone = 0;

	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[-]\n");
out:
	mutex_unlock(&ctx->ve1_mutex);
}

static int ve1_start_decoding(struct ve1_ctx *ctx)
{
	int ret;
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[+]\n");

	mutex_lock(&ctx->ve1_mutex);
	if (ctx->bitstream.paddr == 0) {
		ret = ve1_alloc_bitstream_buffer(ctx);
		if (ret < 0) {
			ve1_err(VE1_LOGTAG,
				"[-] ve1_alloc_bitstream_buffer() fail.ret:%d\n",
				ret);
			goto out;
		}
	}
	mutex_unlock(&ctx->ve1_mutex);
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[-] ret:%d\n", ret);
	return 0;
out:
	mutex_unlock(&ctx->ve1_mutex);
	return ret;
}

static int ve1_prepare_decode(struct ve1_ctx *ctx)
{
	int ret = 0;

	// handle EOS
	if (ctx->handle_eos_by == VE1_HANDLE_EOS_BY_PREPARE_RUN) {
		ctx->handle_eos_by = VE1_HANDLE_EOS_SET_END;
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"call ve1_set_stream_end()\n");
		ve1_set_stream_end(ctx);
	}

	ret = VE1_DecStartDecode(ctx);
	if (ret < 0) {
		ve1_err(VE1_LOGTAG, "VE1_DecStartDecode() fail\n");
		return -EINVAL;
	}

	return 0;
}

void rtkve1_add_displayble_frame_to_list(struct ve1_ctx *ctx)
{
	unsigned long flags;
	struct ve1_displayable_frame *frame;
	int frameIndex = 0;
	PhysicalAddress framePhysAddr = 0;
	unsigned long frameSize = 0;
	void *tmp_dpb = NULL;
	struct rtkve1_dpb_t *dpb = NULL;

	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx == NULL\n");
		return;
	}

	frameIndex = ctx->lastIndexFrameDisplay;

	if (frameIndex >= 0) {
		framePhysAddr = ctx->lastDisplayFrmBufY;
		frameSize = (((FrameBuffer *)ctx->fbUser) + frameIndex)->size;
		tmp_dpb = rtkve1_find_dpb((void *)ctx, framePhysAddr, ctx->currSequenceNo);
		if (!tmp_dpb) {
			ve1_err(VE1_LOGTAG,
				"can't find framePhysAddr:0x%x in dpb[]\n",
				framePhysAddr);
			return;
		}
		dpb = (struct rtkve1_dpb_t *)tmp_dpb;

		// save displayable frame info to displayable_frame_list
		spin_lock_irqsave(&ctx->displayable_frame_lock, flags);
		frame = kzalloc(sizeof(struct ve1_displayable_frame),
				GFP_KERNEL);
		if (frame) {
			VE1_GetDisplayFrameInfo(ctx, frame);
			frame->regIndex = frameIndex;
			frame->dpb_paddr = framePhysAddr;
			frame->vb2_v4l2_buf = dpb->vb2_v4l2_buf;
			frame->size = frameSize;
			frame->timestamp = ctx->frame_metas[frameIndex].timestamp;
			frame->timecode = ctx->frame_metas[frameIndex].timecode;
			memset(&frame->timecode, 0, sizeof(struct v4l2_timecode));
			frame->sequenceNo = ctx->currSequenceNo;
			ctx->cntAddToList++;
			list_add_tail(&frame->list,
				      &ctx->displayable_frame_list);
			ve1_dbg(VPU_DBG_VE1_DIS, VE1_LOGTAG,
				"add displayable_frame_list.regIndex:%d.dpb_paddr:0x%x.vb2_v4l2_buf:0x%px.size:%ld.timestamp:%lld.POC:%d.last_frame:%d.sequenceNo:%d.cnt:%u\n",
				frameIndex, frame->dpb_paddr,
				frame->vb2_v4l2_buf, frame->size,
				frame->timestamp, frame->POC,
				frame->last_frame, frame->sequenceNo,
				ctx->cntAddToList);

			// check if timestame back track
			if ((frame->timestamp <= ctx->lastFrameTimestamp) &&
			    (ctx->lastFrameTimestamp != 0)) {
				ve1_dbg(VPU_DBG_VE1_DIS, VE1_LOGTAG,
					"timestamp back track.curr:%lld(pts:%lld).last:%lld(pts:%lld)\n",
					frame->timestamp,
					div_u64((frame->timestamp * 9), 10000),
					ctx->lastFrameTimestamp,
					div_u64((ctx->lastFrameTimestamp * 9),
						10000));
			}
			ctx->lastFrameTimestamp = frame->timestamp;
		} else {
			ve1_err(VE1_LOGTAG, "kzalloc frame fail\n");
		}
		spin_unlock_irqrestore(&ctx->displayable_frame_lock, flags);
	} else if ((ctx->lastIndexFrameDisplay == -1) &&
		   (!ctx->seqChangeDone)) {
		struct ve1_displayable_frame *frame;
		spin_lock_irqsave(&ctx->displayable_frame_lock, flags);
		if (!list_empty(&ctx->displayable_frame_list)) {
			frame = list_last_entry(&ctx->displayable_frame_list,
						struct ve1_displayable_frame, list);
			ve1_dbg(VPU_DBG_VE1_DIS, VE1_LOGTAG,
				"last frame in displayable_frame_list.regIndex:%d.dpb_paddr:0x%x.vb2_v4l2_buf:0x%px.timestamp:%lld\n",
				frame->regIndex, frame->dpb_paddr, frame->vb2_v4l2_buf,
				frame->timestamp);
			frame->last_frame = 1;
			if (frame->isDequeued)
			{
				ctx->last_frame = 1;
				ctx->lastFrmReportedAfFrmDqSeqNo = ctx->currSequenceNo;
				ve1_info(VE1_LOGTAG, "set ctx->last_frame=%d, ctx->lastFrmReportedAfFrmDqSeqNo:%d\n",
					ctx->last_frame, ctx->lastFrmReportedAfFrmDqSeqNo);
			}
		}
		else {
			ctx->last_frame = 1;
			ctx->lastFrmReportedAfFrmDqSeqNo = ctx->currSequenceNo;
			ve1_info(VE1_LOGTAG, "set ctx->last_frame=%d, ctx->lastFrmReportedAfFrmDqSeqNo:%d\n",
				ctx->last_frame, ctx->lastFrmReportedAfFrmDqSeqNo);
		}
		spin_unlock_irqrestore(&ctx->displayable_frame_lock, flags);
	}
}
EXPORT_SYMBOL(rtkve1_add_displayble_frame_to_list);

void ve1_show_displayable_frame_list(struct ve1_ctx *ctx)
{
	unsigned long flags;
	struct ve1_displayable_frame *frame;

	if (ctx == NULL) {
		ve1_err(VE1_LOGTAG, "ctx == NULL\n");
		return;
	}

	spin_lock_irqsave(&ctx->displayable_frame_lock, flags);
	if (!list_empty(&ctx->displayable_frame_list)) {
		list_for_each_entry (frame, &ctx->displayable_frame_list,
				     list) {
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"iterate displayable_frame_list.regIndex:%d.dpb_paddr:0x%x.vb2_v4l2_buf:0x%px.isDequeued:%d.timestamp:0x%llx.sequenceNo:%d.last_frame:%d\n",
				frame->regIndex, frame->dpb_paddr,
				frame->vb2_v4l2_buf, frame->isDequeued,
				frame->timestamp, frame->sequenceNo,
				frame->last_frame);
		}
	}
	spin_unlock_irqrestore(&ctx->displayable_frame_lock, flags);
}
EXPORT_SYMBOL(ve1_show_displayable_frame_list);

static int ve1_finish_decode(struct ve1_ctx *ctx)
{
	int ret = 0;
	struct ve1_meta meta;

	ret = VE1_DecPicDone(ctx);
	if (ret < 0) {
		ve1_err(VE1_LOGTAG, "VE1_DecPicDone() fail\n");
		return ret;
	}

	if (ctx->lastIndexFrameDecoded == -2) {
		// when BS_MODE_PIC_END, indexFrameDecoded==-2 means ve1 can't decode a frame on current valid bitstream
		// it should consume ve1_meta of out_buf
		rtkve1_get_meta(ctx, &meta);
	}

	if (ctx->lastIndexFrameDecoded >= 0) {
		rtkve1_get_meta(ctx, &meta);
		ctx->frame_metas[ctx->lastIndexFrameDecoded] = meta;
#ifdef VPU_GET_CC
		if ((ctx->userDataEnable) && (ctx->pUserDataSrcBuf != NULL) &&
		    (ctx->lastIndexFrameDecoded >= 0)) {
			unsigned int actualUserDataSize = 0;
			DecOpenParam *pDecOp;

			pDecOp = (DecOpenParam *)ctx->decOP;
			//call ProcessCC() here
			if (pDecOp->bitstreamFormat == STD_MPEG2) {
				if (cc_isCCReaderReady()) {
					if (ctx->is_svp) {
						actualUserDataSize =
							USER_DATA_SRC_BUF_SIZE;
					} else {
						memset(ctx->pUserDataSrcBuf, 0,
						       USER_DATA_SRC_BUF_SIZE);
						actualUserDataSize = ve1_get_userdata(
							ctx,
							ctx->pUserDataSrcBuf,
							USER_DATA_SRC_BUF_SIZE);
#if defined(VE1_CHECK_USERDATA_MD5_EN)
						if (actualUserDataSize > 0) {
							ve1_md5_hash(
								ve1_md5_digest,
								VE1_MD5_DIGEST_SIZE,
								(char *)ctx
									->pUserDataSrcBuf,
								actualUserDataSize);
							ve1_dbg(VPU_DBG_VE1_DEC,
								VE1_WRAPPER_TAG,
								"%d.userdata size:%d.MD5:0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
								ctx->decodedFrmNum,
								actualUserDataSize,
								ve1_md5_digest[0],
								ve1_md5_digest[1],
								ve1_md5_digest[2],
								ve1_md5_digest[3],
								ve1_md5_digest[4],
								ve1_md5_digest[5],
								ve1_md5_digest[6],
								ve1_md5_digest[7],
								ve1_md5_digest[8],
								ve1_md5_digest[9],
								ve1_md5_digest
									[10],
								ve1_md5_digest
									[11],
								ve1_md5_digest
									[12],
								ve1_md5_digest
									[13],
								ve1_md5_digest
									[14],
								ve1_md5_digest
									[15]);
						}
#endif
					}

					ProcessCC(
						ctx,
						(unsigned char *)
							ctx->pUserDataSrcBuf,
						actualUserDataSize,
						ctx->frame_metas
							[ctx->lastIndexFrameDecoded]
								.timestamp,
						ctx->lastIndexFrameDecoded,
						ctx->lastIndexFrameDisplay,
						ENUM_CC_MPGE2);
				}
			} else if ((pDecOp->bitstreamFormat == STD_AVC)) {
				if (cc_isCCReaderReady()) {
					if (ctx->is_svp) {
						actualUserDataSize =
							USER_DATA_SRC_BUF_SIZE;
					} else {
						memset(ctx->pUserDataSrcBuf, 0,
						       USER_DATA_SRC_BUF_SIZE);
						actualUserDataSize = ve1_get_userdata(
							ctx,
							ctx->pUserDataSrcBuf,
							USER_DATA_SRC_BUF_SIZE);
#if defined(VE1_CHECK_USERDATA_MD5_EN)
						if (actualUserDataSize > 0) {
							ve1_md5_hash(
								ve1_md5_digest,
								VE1_MD5_DIGEST_SIZE,
								(char *)ctx
									->pUserDataSrcBuf,
								actualUserDataSize);
							ve1_dbg(VPU_DBG_VE1_DEC,
								VE1_WRAPPER_TAG,
								"%d.userdata size:%d.MD5:0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
								ctx->decodedFrmNum,
								actualUserDataSize,
								ve1_md5_digest[0],
								ve1_md5_digest[1],
								ve1_md5_digest[2],
								ve1_md5_digest[3],
								ve1_md5_digest[4],
								ve1_md5_digest[5],
								ve1_md5_digest[6],
								ve1_md5_digest[7],
								ve1_md5_digest[8],
								ve1_md5_digest[9],
								ve1_md5_digest
									[10],
								ve1_md5_digest
									[11],
								ve1_md5_digest
									[12],
								ve1_md5_digest
									[13],
								ve1_md5_digest
									[14],
								ve1_md5_digest
									[15]);
						}
#endif
					}

					ProcessCC(
						ctx,
						(unsigned char *)
							ctx->pUserDataSrcBuf,
						actualUserDataSize,
						ctx->frame_metas
							[ctx->lastIndexFrameDecoded]
								.timestamp,
						ctx->lastIndexFrameDecoded,
						ctx->lastIndexFrameDisplay,
						ENUM_CC_H264);
				}
			}
		}
#endif
	}

	rtkve1_add_displayble_frame_to_list(ctx);

	ret = 1; // defaultly, return 1 to let ve1_pic_run_work() queue_work pic_run_work

	if ((ctx->lastIndexFrameDecoded == -1) &&
	    (ctx->lastIndexFrameDisplay == -3 ||
	     ctx->lastIndexFrameDisplay == -1)) {
		ctx->dpbFull = 1;
		// DBP full happened, return 0 to let ve1_pic_run_work() not queue_work pic_run_work
		// instead, ve1_cap_qbuf() queue_work pic_run_work
		ret = 0;
	} else if (ctx->lastIndexFrameDecoded != -1) {
		ctx->dpbFull = 0;
	}

	if (ctx->lastIndexFrameDisplay == -1) {
		// decode finish, no more queue_work pic_run_work
		ret = 0;
	}

	return ret;
}

static void ve1_release(struct ve1_ctx *ctx)
{
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[+]\n");
	mutex_lock(&ctx->ve1_mutex);
	ve1_free_bitstream_buffer(ctx);
	mutex_unlock(&ctx->ve1_mutex);
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "[-]\n");
}

const struct ve1_ctx_ops ve1_decode_ops = {
	.queue_init = NULL /*ve1_decoder_queue_init*/,
	.reqbufs = NULL /*ve1_decoder_reqbufs*/,
	.start_streaming = ve1_start_decoding,
	.prepare_run = ve1_prepare_decode,
	.finish_run = ve1_finish_decode,
	.seq_init_work = ve1_dec_seq_init_work,
	.seq_end_work = ve1_seq_end_work,
	.release = ve1_release,
};
