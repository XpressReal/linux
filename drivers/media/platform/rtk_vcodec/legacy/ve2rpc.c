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
 */
#include <linux/errno.h>
#include <linux/v4l2-common.h>
#include <linux/mm.h>
#include <linux/kthread.h> // for threads
#include <linux/time.h> // for using jiffies
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#ifdef ENABLE_TEE_DRM_FLOW
#include <linux/tee_drv.h>
#endif
#include "debug.h"
#include "vpu.h"
#include "ve2rpc.h"
#include "ve2rpc_cmd.h"
#include "ve2_frame.h"
#ifdef PREPEND_METADATA
#include "ve_common.h"
#endif

#include <soc/realtek/memory.h>

#define RES_4K_SIZE (5570560) //2560*2176

struct ve2rpc_qframe_st {
	struct list_head list;
	volatile ve2rpc_flash_frame_info_t *frame;
	volatile uint8_t *buflock_va;
	uint32_t buflock_pa;
	struct mutex *buflock_mutex;
	struct mutex *mainrb_mutex;
	uint32_t frm_idx;
	struct ve2rpc *hndl;
};
struct traveling_frame_st {
	struct list_head list;
	uint32_t phy_addr;
	uint32_t vb2_q_idx;
	uint32_t buflock_phy_addr;
	void *vb2_v4l2_buf;
};
struct ve2rpc_dqframe {
	struct list_head list;
};

#ifdef ENABLE_TEE_DRM_FLOW
extern int ta_TEEapi_memcpy_a7(struct tee_context *teeapi_ctx,
			       unsigned int teeapi_tee_session,
			       unsigned int dstPAddr, unsigned char *buf,
			       int size);
extern int ta_TEEapi_memcpy(struct tee_context *teeapi_ctx,
			    unsigned int teeapi_tee_session,
			    unsigned int dstPhysAddr, unsigned int srtPhysAddr,
			    int size);
#endif

static void memset_volatile(volatile void *dest, char val, size_t len)
{
	volatile char *ptr = dest;
	while (len-- > 0) {
		*ptr++ = val;
	}
}

static void __maybe_unused dump_frame(volatile ve2rpc_flash_frame_info_t *frame)
{
	vpu_output_dbg("\n");
	vpu_output_dbg("frame->nSize = %x\n", htonl(frame->nSize));
	vpu_output_dbg("frame->nVersion = %x\n", htonl(frame->nVersion));
	vpu_output_dbg("frame->pUserData = %x\n", htonl(frame->pUserData));
	vpu_output_dbg("frame->nRRKey = %x\n", htonl(frame->nRRKey));
	vpu_output_dbg("frame->nContext = %x\n", htonl(frame->nContext));
	vpu_output_dbg("frame->nBufID = %x\n", htonl(frame->nBufID));
	vpu_output_dbg("frame->nPicFlags = %x\n", htonl(frame->nPicFlags));
	vpu_output_dbg("frame->nPicWidth = %x\n", htonl(frame->nPicWidth));
	vpu_output_dbg("frame->nPicHeight = %x\n", htonl(frame->nPicHeight));
	vpu_output_dbg("frame->nDecimatePicWidth = %x\n",
		       htonl(frame->nDecimatePicWidth));
	vpu_output_dbg("frame->nDecimateicHeight = %x\n",
		       htonl(frame->nDecimateicHeight));
	vpu_output_dbg("frame->nBitDepthLuma = %x\n",
		       htonl(frame->nBitDepthLuma));
	vpu_output_dbg("frame->nBitDepthChroma = %x\n",
		       htonl(frame->nBitDepthChroma));
	vpu_output_dbg("frame->nPtsHigh = %x\n", htonl(frame->nPtsHigh));
	vpu_output_dbg("frame->nPtsLow = %x\n", htonl(frame->nPtsLow));
	vpu_output_dbg("frame->nRPtsHigh = %x\n", htonl(frame->nRPtsHigh));
	vpu_output_dbg("frame->nRPtsLow = %x\n", htonl(frame->nRPtsLow));
	vpu_output_dbg("frame->nPts2High = %x\n", htonl(frame->nPts2High));
	vpu_output_dbg("frame->nPts2Low = %x\n", htonl(frame->nPts2Low));
	vpu_output_dbg("frame->nPicPhysicalAddr = %x\n",
		       htonl(frame->nPicPhysicalAddr));
	vpu_output_dbg("frame->nPicPitch = %x\n", htonl(frame->nPicPitch));
	vpu_output_dbg("frame->nClkTimeHigh = %x\n",
		       htonl(frame->nClkTimeHigh));
	vpu_output_dbg("frame->nClkTimeLow = %x\n", htonl(frame->nClkTimeLow));
	vpu_output_dbg("frame->nDecClkTime = %x\n", htonl(frame->nDecClkTime));
	vpu_output_dbg("frame->nDecFrameCount = %x\n",
		       htonl(frame->nDecFrameCount));
	vpu_output_dbg("frame->nFramerateD = %x\n", htonl(frame->nFramerateD));
	vpu_output_dbg("frame->nFramerateN = %x\n", htonl(frame->nFramerateN));
	vpu_output_dbg("frame->eScanType = %x\n", htonl(frame->eScanType));
	vpu_output_dbg("frame->nInterlaceMode = %x\n",
		       htonl(frame->nInterlaceMode));
	vpu_output_dbg("frame->nSeiPtsHigh = %x\n", htonl(frame->nSeiPtsHigh));
	vpu_output_dbg("frame->nSeiPtsSLow = %x\n", htonl(frame->nSeiPtsSLow));
	vpu_output_dbg("frame->nPicCPitch = %x\n", htonl(frame->nPicCPitch));
	vpu_output_dbg("frame->nPicCPhysicalAddr = %x\n",
		       htonl(frame->nPicCPhysicalAddr));
	vpu_output_dbg("frame->nSampleWidth = %x\n",
		       htonl(frame->nSampleWidth));
	vpu_output_dbg("frame->nSampleHeight = %x\n",
		       htonl(frame->nSampleHeight));
	vpu_output_dbg("frame->qlevel_sel_y = %x\n",
		       htonl(frame->qlevel_sel_y));
	vpu_output_dbg("frame->qlevel_sel_c = %x\n",
		       htonl(frame->qlevel_sel_c));
	vpu_output_dbg("frame->nHDR_Type = %x\n", htonl(frame->nHDR_Type));

	vpu_output_dbg("frame->nDisplayPrimaries_X[0] = %x\n",
		       htonl(frame->nDisplayPrimaries_X[0]));
	vpu_output_dbg("frame->nDisplayPrimaries_X[1] = %x\n",
		       htonl(frame->nDisplayPrimaries_X[1]));
	vpu_output_dbg("frame->nDisplayPrimaries_X[2] = %x\n",
		       htonl(frame->nDisplayPrimaries_X[2]));

	vpu_output_dbg("frame->nDisplayPrimaries_Y[0] = %x\n",
		       htonl(frame->nDisplayPrimaries_Y[0]));
	vpu_output_dbg("frame->nDisplayPrimaries_Y[1] = %x\n",
		       htonl(frame->nDisplayPrimaries_Y[1]));
	vpu_output_dbg("frame->nDisplayPrimaries_Y[2] = %x\n",
		       htonl(frame->nDisplayPrimaries_Y[2]));

	vpu_output_dbg("frame->nWhitePoint_X = %x\n",
		       htonl(frame->nWhitePoint_X));
	vpu_output_dbg("frame->nWhitePoint_Y = %x\n",
		       htonl(frame->nWhitePoint_Y));
	vpu_output_dbg("frame->nMaxDisplayMasteringLuminance = %x\n",
		       htonl(frame->nMaxDisplayMasteringLuminance));
	vpu_output_dbg("frame->nMinDisplayMasteringLuminance = %x\n",
		       htonl(frame->nMinDisplayMasteringLuminance));
	vpu_output_dbg("frame->nTransferCharacteristics = %x\n",
		       htonl(frame->nTransferCharacteristics));
	vpu_output_dbg("frame->nMatrixCoefficiets = %x\n",
		       htonl(frame->nMatrixCoefficiets));
	vpu_output_dbg("frame->nVideoFullRangeFlag = %x\n",
		       htonl(frame->nVideoFullRangeFlag));
	vpu_output_dbg("frame->nMaxCLL = %x\n", htonl(frame->nMaxCLL));
	vpu_output_dbg("frame->nMaxFALL = %x\n", htonl(frame->nMaxFALL));
	vpu_output_dbg("frame->hdr_metadata_addr = %x\n",
		       htonl(frame->hdr_metadata_addr));
	vpu_output_dbg("frame->hdr_metadata_size = %x\n",
		       htonl(frame->hdr_metadata_size));
	vpu_output_dbg("frame->tch_metadata_addr = %x\n",
		       htonl(frame->tch_metadata_addr));
	vpu_output_dbg("frame->tch_metadata_size = %x\n",
		       htonl(frame->tch_metadata_size));
	vpu_output_dbg("frame->film_grain_metadata_addr = %x\n",
		       htonl(frame->film_grain_metadata_addr));
	vpu_output_dbg("frame->film_grain_metadata_size = %x\n",
		       htonl(frame->film_grain_metadata_size));

	vpu_output_dbg("frame->nCmprsMode = %x\n", htonl(frame->nCmprsMode));
	vpu_output_dbg("frame->nPicYCmprsHdrAddr = %x\n",
		       htonl(frame->nPicYCmprsHdrAddr));
	vpu_output_dbg("frame->nPicCCmprsHdrAddr = %x\n",
		       htonl(frame->nPicCCmprsHdrAddr));
	vpu_output_dbg("frame->nPicCmprsPitch = %x\n",
		       htonl(frame->nPicCmprsPitch));
	vpu_output_dbg("frame->nPicCPhysicalAddr2 = %x\n",
		       htonl(frame->nPicCPhysicalAddr2));

	vpu_output_dbg("frame->nBufLockPhysicalAddr = %x\n",
		       htonl(frame->nBufLockPhysicalAddr));
	vpu_output_dbg("frame->max_fb_num = %x\n", htonl(frame->max_fb_num));
	vpu_output_dbg("frame->max_cmprs_head_size = %x\n",
		       htonl(frame->max_cmprs_head_size));

	vpu_output_dbg("frame->nLinearPicPhysicalAddr = %x\n",
		       htonl(frame->nLinearPicPhysicalAddr));
	vpu_output_dbg("frame->nLinearPicCPhysicalAddr = %x\n",
		       htonl(frame->nLinearPicCPhysicalAddr));
	vpu_output_dbg("frame->nLinearPicWidth = %x\n",
		       htonl(frame->nLinearPicWidth));
	vpu_output_dbg("frame->nLinearPicHeight = %x\n",
		       htonl(frame->nLinearPicHeight));
	vpu_output_dbg("frame->nLinearPicPitch = %x\n",
		       htonl(frame->nLinearPicPitch));

	vpu_output_dbg("frame->nPixelAR_hor = %x\n",
		       htonl(frame->nPixelAR_hor));
	vpu_output_dbg("frame->nPixelAR_ver = %x\n",
		       htonl(frame->nPixelAR_ver));
}

static void __maybe_unused _dump_ringbuf(struct ve2rpc_ringbuf_t *prb)
{
	mutex_lock(&prb->lock);
	vpu_output_dbg("\n");
	vpu_output_dbg("prb->pRBH->magic %x\n", htonl(prb->pRBH->magic));
	vpu_output_dbg("prb->pRBH->beginAddr %x\n",
		       htonl(prb->pRBH->beginAddr));
	vpu_output_dbg("prb->pRBH->size %x\n", htonl(prb->pRBH->size));
	vpu_output_dbg("prb->pRBH->bufferID %x\n", htonl(prb->pRBH->bufferID));

	vpu_output_dbg("prb->pRBH->writePtr %x\n", htonl(prb->pRBH->writePtr));
	vpu_output_dbg("prb->pRBH->numOfReadPtr %x\n",
		       htonl(prb->pRBH->numOfReadPtr));
	vpu_output_dbg("prb->pRBH->reserve2 %x\n", htonl(prb->pRBH->reserve2));
	vpu_output_dbg("prb->pRBH->reserve3 %x\n", htonl(prb->pRBH->reserve3));

	vpu_output_dbg("prb->pRBH->readPtr[0] %x\n",
		       htonl(prb->pRBH->readPtr[0]));
	vpu_output_dbg("prb->pRBH->readPtr[1] %x\n",
		       htonl(prb->pRBH->readPtr[1]));
	vpu_output_dbg("prb->pRBH->readPtr[2] %x\n",
		       htonl(prb->pRBH->readPtr[2]));
	vpu_output_dbg("prb->pRBH->readPtr[3] %x\n",
		       htonl(prb->pRBH->readPtr[3]));

	vpu_output_dbg("prb->pRBH->fileOffset %x\n",
		       htonl(prb->pRBH->fileOffset));
	vpu_output_dbg("prb->pRBH->requestedFileOffset %x\n",
		       htonl(prb->pRBH->requestedFileOffset));
	vpu_output_dbg("prb->pRBH->fileSize %x\n", htonl(prb->pRBH->fileSize));
	vpu_output_dbg("prb->pRBH->bSeekable %x\n",
		       htonl(prb->pRBH->bSeekable));
	mutex_unlock(&prb->lock);
}

static struct ve2rpc_ion_object *_ve2rpc_ion_create(struct ve2rpc *hndl,
						    size_t size,
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 116)
						    char *name,
#else
						    unsigned int mask,
#endif
						    unsigned int flags)
{
	struct ve2rpc_ion_object *ion_obj;
	struct dma_buf_attachment *attachment __maybe_unused;
	struct sg_table *sgt __maybe_unused;
	dma_addr_t dma_addr;
	void *vaddr;

	if (!hndl) {
		vpu_err("allocate ion_obj fail, No initital\n");
		return ERR_PTR(-ENOMEM);
	}

	ion_obj = kzalloc(sizeof(*ion_obj), GFP_KERNEL);
	if (!ion_obj) {
		vpu_err("%s allocate ion_obj fail, No Memory\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	/* We can't limit the address from dma_alloc_coherent when size <= 4096 */
	if (size < SZ_8K)
		size = SZ_8K;

	vaddr = dma_alloc_coherent(hndl->dev, size, &dma_addr, GFP_KERNEL);
	if (!vaddr) {
		vpu_err("%s dma_alloc fail \n", __func__);
		kfree(ion_obj);
		return ERR_PTR(-ENOMEM);
	}
	ion_obj->vaddr = vaddr;
	ion_obj->paddr = dma_addr;
	ion_obj->size = size;

	memset(ion_obj->vaddr, 0, size);

	return ion_obj;
}

static void _ve2rpc_ion_free(struct ve2rpc *hndl,
								struct ve2rpc_ion_object *ion_obj)
{
	if (!ion_obj) {
		vpu_err("_ve2rpc_ion_free ion_obj is NULL\n");
		return;
	}

	if (ion_obj->dmabuf) {
		dma_buf_vunmap(ion_obj->dmabuf, &ion_obj->map);
		dma_buf_end_cpu_access(ion_obj->dmabuf, DMA_BIDIRECTIONAL);
		dma_buf_unmap_attachment(ion_obj->attach, ion_obj->sgt,
					 DMA_TO_DEVICE);
		dma_buf_detach(ion_obj->dmabuf, ion_obj->attach);
		dma_buf_put(ion_obj->dmabuf);
	}
	else
	{
		if (ion_obj->vaddr) {
			dma_free_coherent(hndl->dev, ion_obj->size, ion_obj->vaddr, ion_obj->paddr);
		}
	}

	kfree(ion_obj);
}

static struct ve2rpc_ion_object *_ve2rpc_audio_ion_create(struct ve2rpc *hndl,
							  size_t size)
{
	return _ve2rpc_ion_create(hndl, size,
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 116)
				  "rtk_audio_heap",
				  RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
					  RTK_FLAG_ACPUACC);
#else
				  RTK_ION_HEAP_AUDIO_MASK,
				  ION_FLAG_NONCACHED | ION_FLAG_SCPUACC |
					  ION_FLAG_ACPUACC);
#endif
}

static __maybe_unused struct ve2rpc_ion_object *
_ve2rpc_secure_media_ion_create(struct ve2rpc *hndl, uint32_t size)
{
	return _ve2rpc_ion_create(hndl, size,
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 116)
				  "rtk_media_heap",
				  RTK_FLAG_NONCACHED | RTK_FLAG_HWIPACC |
					  RTK_FLAG_VCPU_FWACC |
					  RTK_FLAG_PROTECTED_VIDEO);
#else
				  RTK_ION_HEAP_SECURE_MASK |
					  RTK_ION_HEAP_MEDIA_MASK,
				  ION_FLAG_NONCACHED | ION_FLAG_HWIPACC |
					  ION_FLAG_VCPU_FWACC |
					  ION_FLAG_PROTECTED_VIDEO);
#endif
}

static struct ve2rpc_ion_object *_ve2rpc_media_ion_create(struct ve2rpc *hndl,
							  size_t size)
{
	return _ve2rpc_ion_create(hndl, size,
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 10, 116)
				  "rtk_media_heap",
				  RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
					  RTK_FLAG_ACPUACC |
					  RTK_FLAG_VCPU_FWACC);
#else
				  RTK_ION_HEAP_MEDIA_MASK,
				  ION_FLAG_NONCACHED | ION_FLAG_SCPUACC |
					  ION_FLAG_ACPUACC |
					  ION_FLAG_VCPU_FWACC);
#endif
}

static int SendReply(struct rtk_krpc_ept_info *krpc_ept_info,
		     uint32_t req_taskID, int32_t req_context,
		     char *ReplyParameter, // parameter's start address
		     uint32_t ParameterSize) // parameter's size
{
	ssize_t ret;
	struct rpc_struct *rpc;
	char *mem_ToShm;
	char *p;
	int size_ToShm = 0; // total mem size for writing to share memory
	uint32_t *context;
	mem_ToShm =
		kmalloc(sizeof(RPC_STRUCT) + sizeof(uint32_t) + ParameterSize,
			GFP_KERNEL | __GFP_ZERO);
	if (!mem_ToShm) {
		pr_err("SendReply malloc fail\n");
		return -ENOMEM;
	}
	p = mem_ToShm;
	rpc = (struct rpc_struct *)p;
	p += sizeof(struct rpc_struct);
	context = (uint32_t *)p;
	*context = htonl(req_taskID);
	size_ToShm += sizeof(uint32_t);
	p += sizeof(uint32_t);
	for (context = (uint32_t *)p; (char *)context < p + ParameterSize;
	     context++) {
		*context = *ReplyParameter;
		ReplyParameter += sizeof(uint32_t);
	}
	size_ToShm += ParameterSize;
	rpc->programID = REPLYID;
	rpc->versionID = REPLYID;
	rpc->procedureID = 0;
	rpc->mycontext = req_context; // fill in req's para addr
	rpc->taskID = 0xffffffff;
	rpc->sysPID = 0xffffffff;
	rpc->parameterSize = size_ToShm;
	size_ToShm += sizeof(struct rpc_struct);
	ret = rtk_send_rpc(krpc_ept_info, mem_ToShm, size_ToShm);
	if (ret != size_ToShm) {
		pr_err("ve2RPC: ERROR in send kernel RPC\n");
	}
	kfree(mem_ToShm);
	if (ret)
		return 0;
	else
		return ret;
}

static int handle_rpc_command(struct rtk_krpc_ept_info *krpc_ept_info,
			      char *buf)
{
	struct ve2rpc *hndl = (struct ve2rpc *)krpc_ept_info->priv;
	struct v4l2_fh *fh = hndl->fh;
	int cmd;
	struct VIDEO_RPC_VOUT_MESSAGE *event;
	HRESULT retval = S_OK;
	ssize_t size = 0;
	struct rpc_struct *rpc_head = (struct rpc_struct *)buf;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t ddr_width = 0;
	uint32_t ddr_height = 0;
	uint32_t is_ten_bits = 0;
	uint32_t dpb_cnt = 0;

	vpu_input_dbg("rpc_kern_ve2_read, cmd %d, count %lu, size %d\n",
		      rpc_head->procedureID, sizeof(rpc_head),
		      rpc_head->parameterSize);
	cmd = rpc_head->procedureID;
	switch (cmd) {
	case VIDEO_RPC_DEC_ToSystem_FatalError:
		event = (struct VIDEO_RPC_VOUT_MESSAGE *)kmalloc(sizeof(struct VIDEO_RPC_DEC_ERROR_INFO),
				GFP_KERNEL | __GFP_ZERO);
		if (!event) {
			vpu_err("VIDEO_RPC_DEC_ERROR_INFO event malloc fail\n");
			return -ENOMEM;
		}
		if (rpc_head->parameterSize !=
		    sizeof(struct VIDEO_RPC_DEC_ERROR_INFO)) {
			pr_err("vclient: VIDEO_RPC_DEC_ToSystem_FatalError: rpc data size not match(expect:%lu real:%ld rpc->parameterSize:%d)\n",
			       sizeof(struct VIDEO_RPC_DEC_ERROR_INFO), size,
			       rpc_head->parameterSize);
			return -EINVAL;
		}
		memcpy(event, buf + sizeof(struct rpc_struct),
		       rpc_head->parameterSize);
		hndl->is_error = true;
		vpu_err("ve2 decode error!!\n");
		kfree(event);
		break;
	case VIDEO_RPC_DEC_ToSystem_Deliver_MediaInfo:
		event = (struct VIDEO_RPC_VOUT_MESSAGE *)kmalloc(sizeof(struct VIDEO_RPC_DEC_MEDIA_INFO),
				GFP_KERNEL | __GFP_ZERO);
		if (!event) {
			vpu_err("VIDEO_RPC_DEC_MEDIA_INFO event malloc fail\n");
			return -ENOMEM;
		}
		if (rpc_head->parameterSize !=
		    sizeof(struct VIDEO_RPC_DEC_MEDIA_INFO)) {
			pr_err("vclient: VIDEO_RPC_DEC_MEDIA_INFO: rpc data size not match(expect:%lu real:%ld rpc->parameterSize:%d)\n",
			       sizeof(struct VIDEO_RPC_DEC_MEDIA_INFO), size,
			       rpc_head->parameterSize);
		}
		memcpy(event, buf + sizeof(struct rpc_struct),
		       rpc_head->parameterSize);
		kfree(event);
		break;
	case VIDEO_RPC_ToSystem_VoutMessage:
		event = (struct VIDEO_RPC_VOUT_MESSAGE *)kmalloc(sizeof(struct VIDEO_RPC_VOUT_MESSAGE),
				GFP_KERNEL | __GFP_ZERO);
		if (!event) {
			vpu_err("VIDEO_RPC_VOUT_MESSAGE event malloc fail\n");
			return -ENOMEM;
		}

		if (rpc_head->parameterSize !=
		    sizeof(struct VIDEO_RPC_VOUT_MESSAGE)) {
			pr_err("vclient: VIDEO_RPC_VOUT_MESSAGE: rpc data size not match(expect:%lu real:%ld rpc->parameterSize:%d)\n",
			       sizeof(struct VIDEO_RPC_VOUT_MESSAGE), size,
			       rpc_head->parameterSize);
		}
		memcpy(event, buf + sizeof(struct rpc_struct),
		       rpc_head->parameterSize);

		width = event->reserved1;
		height = event->reserved2;
		ddr_width = event->reserved3 >> 16;
		ddr_height = event->reserved3 & 0xffff;
		is_ten_bits = event->reserved4 >> 16;
		dpb_cnt = event->reserved4 & 0xffff;

		vpu_info("Resolution change! width %d, height %d, ddr_width %d, ddr_height %d, is_ten_bits %d, dpb_cnt %d",
			width, height, ddr_width, ddr_height, is_ten_bits, dpb_cnt);

#ifdef SUPPORT_ADAPTIVE_PLAYBACK
		if (!hndl->is_adaptive_playback)
			hndl->main_rb.pRBH->reserve3 = htonl(width << 16 | height);
#endif
		if (vpu_check_sub_res_chg(fh))
			vpu_update_resolution_change(fh, width, height, ddr_width, ddr_height,
				is_ten_bits, dpb_cnt);
		else
			hndl->is_error = true;

		kfree(event);
		break;
	default:
		break;
	}
	if (rpc_head->taskID != 0)
		SendReply(hndl->vcpu_ept_info, rpc_head->taskID,
			  rpc_head->mycontext, (char *)&retval, sizeof(retval));

	return 0;
}

static char *prepare_rpc_data(struct rtk_krpc_ept_info *krpc_ept_info,
			      uint32_t command, uint32_t param1,
			      uint32_t param2, int *len)
{
	struct rpc_struct *rpc;
	uint32_t *tmp;
	char *buf;

	*len = sizeof(struct rpc_struct) + 3 * sizeof(uint32_t);
	buf = kmalloc(sizeof(struct rpc_struct) + 3 * sizeof(uint32_t),
		      GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	rpc = (struct rpc_struct *)buf;
	rpc->programID = KERNELID;
	rpc->versionID = KERNELID;
	rpc->procedureID = 0;
	rpc->taskID = krpc_ept_info->id;
	rpc->sysTID = krpc_ept_info->id;
	rpc->sysPID = krpc_ept_info->id;
	rpc->parameterSize = 3 * sizeof(uint32_t);
	rpc->mycontext = 0;
	tmp = (uint32_t *)(buf + sizeof(struct rpc_struct));
	*tmp = command;
	*(tmp + 1) = param1;
	*(tmp + 2) = param2;

	return buf;
}

int ve2_send_rpc(struct rtk_krpc_ept_info *krpc_ept_info, char *buf, int len,
		 uint32_t *retval)
{
	int ret = 0;

	mutex_lock(&krpc_ept_info->send_mutex);

	krpc_ept_info->retval = retval;
	ret = rtk_send_rpc(krpc_ept_info, buf, len);
	if (!wait_for_completion_timeout(&krpc_ept_info->ack, RPC_TIMEOUT)) {
		pr_err("[%s]kernel rpc timeout: %s...\n", __func__,
		       krpc_ept_info->name);
		rtk_krpc_dump_ringbuf_info(krpc_ept_info);
		mutex_unlock(&krpc_ept_info->send_mutex);
		return -EINVAL;
	}
	mutex_unlock(&krpc_ept_info->send_mutex);

	return 0;
}

static int send_rpc(struct ve2rpc *hndl, int opt, uint32_t command,
		    uint32_t param1, uint32_t param2, uint32_t *retval)
{
	int ret = 0;
	char *buf;
	int len;

	if (opt == RPC_VIDEO) {
		buf = prepare_rpc_data(hndl->vcpu_ept_info, command, param1,
				       param2, &len);
		if (!IS_ERR(buf)) {
			ret = ve2_send_rpc(hndl->vcpu_ept_info, buf, len,
					   retval);
			kfree(buf);
		}
	}

	return ret;
}

static int krpc_vcpu_cb(struct rtk_krpc_ept_info *krpc_ept_info, char *buf)
{
	uint32_t *tmp;
	struct rpc_struct *rpc = (struct rpc_struct *)buf;

	if (rpc->programID == REPLYID) {
		tmp = (uint32_t *)(buf + sizeof(struct rpc_struct));
		*(krpc_ept_info->retval) = *(tmp + 1);

		complete(&krpc_ept_info->ack);
	} else {
		handle_rpc_command(krpc_ept_info, buf);
	}

	return 0;
}

static struct rtk_krpc_ept_info *get_ve2_krpc_info(void)
{
	struct device_node *np;
	struct rtk_krpc_ept_info *vcpu_ept_info;

	np = of_find_compatible_node(NULL, NULL, "realtek,ve2rpc");
	if (!np)
		return ERR_PTR(-ENODEV);

	vcpu_ept_info = of_krpc_ept_info_get(np, 0);

	return vcpu_ept_info;
}

static int _ve2rpc_shuttle(struct ve2rpc *hndl, int cmd, void *data, int size,
			   void *rpc_ret, int rpc_ret_size)
{
	struct ve2rpc_ion_object *ion_obj;
	int offset;
	uint32_t dat;
	unsigned int RPC_ret;

	ion_obj = _ve2rpc_audio_ion_create(hndl, 1024);
	if (IS_ERR(ion_obj)) {
		vpu_err("allocate ion_obj fail, No Memory\n");
		return -ENOMEM;
	}

	memcpy_toio(ion_obj->vaddr, data, size);
	dsb(sy);
	offset = get_rpc_alignment_offset(size);
	dat = ion_obj->paddr;

	if (send_rpc(hndl, RPC_VIDEO, cmd, dat, dat + offset, &RPC_ret))
	{
		_ve2rpc_ion_free(hndl, ion_obj);
		vpu_err("ve2rpc fail, cmd %d\n", cmd);
		return -EPERM;
	} else {
		if (RPC_ret == S_OK) {
			if (rpc_ret)
				memcpy_toio(rpc_ret, ion_obj->vaddr + offset,
					    rpc_ret_size);
			_ve2rpc_ion_free(hndl, ion_obj);
			return 0;
		} else {
			_ve2rpc_ion_free(hndl, ion_obj);
			vpu_err("ve2rpc return fail, cmd %d\n", cmd);
			return -EPERM;
		}
	}
	return 0;
}

static int _ve2rpc_ringbuf_release(struct ve2rpc *hndl,
				   struct ve2rpc_ringbuf_t *ringbuf)
{
	if (!ringbuf) {
		vpu_err("Invaild input\n");
		return -EPERM;
	}

	if (ringbuf->rbinfo.hdr_hdl) {
		mutex_lock(&ringbuf->lock);
		_ve2rpc_ion_free(hndl, ringbuf->rbinfo.hdr_hdl);
		ringbuf->rbinfo.hdr_hdl = NULL;
		mutex_unlock(&ringbuf->lock);
	}

	if (ringbuf->rbinfo.buf_hdl) {
		mutex_lock(&ringbuf->lock);
		_ve2rpc_ion_free(hndl, ringbuf->rbinfo.buf_hdl);
		ringbuf->rbinfo.buf_hdl = NULL;
		mutex_unlock(&ringbuf->lock);
	}

	return 0;
}

int ve2rpc_SetRingBuffer(struct ve2rpc *hndl, struct ve2rpc_ringbuf_t *prb,
			 uint32_t bodysize, RINGBUFFER_TYPE type,
			 uint8_t is_secure)
{
	RPC_RINGBUFFER ringbuffer;
	struct ve2rpc_ringbuf_info_t *pRB_info;
	struct ve2rpc_ion_object *body, *head;
	int ret;

	if (!hndl) {
		vpu_err("Invaild input\n");
		return -EPERM;
	}

	if (is_secure)
		body = _ve2rpc_secure_media_ion_create(hndl, bodysize);
	else
		body = _ve2rpc_media_ion_create(hndl, bodysize);
	if (IS_ERR(body)) {
		vpu_err("allocate body fail, No Memory\n");
		return -ENOMEM;
	}

	head = _ve2rpc_media_ion_create(hndl,
					sizeof(struct _tagRingBufferHeader));
	if (IS_ERR(head)) {
		_ve2rpc_ion_free(hndl, body);
		vpu_err("allocate head fail, No Memory\n");
		return -ENOMEM;
	}

	memset(prb, 0, sizeof(struct ve2rpc_ringbuf_t));
	prb->phyaddr_hdr = head->paddr;
	prb->phyaddr = body->paddr;
	prb->size = bodysize;
	prb->base = (uint8_t *)body->vaddr;
	prb->limit = (uint8_t *)body->vaddr + bodysize;
	prb->buf_cached = body->vaddr;
	prb->buf_uncached = body->vaddr;
	prb->hdr_cached = head->vaddr;
	prb->hdr_uncached = head->vaddr;
	prb->secure = is_secure;

	mutex_init(&prb->lock);
	pRB_info = &prb->rbinfo;
	pRB_info->buf_type = VE2RPC_MEMORY_CPB;
	pRB_info->buf_addr = body->paddr;
	pRB_info->buf_size = bodysize;
	pRB_info->buf_cached = body->vaddr;
	pRB_info->buf_uncached = body->vaddr;
	pRB_info->buf_limit = (uint32_t)(uintptr_t)body->vaddr + bodysize;
	pRB_info->buf_hdl = body;

	pRB_info->hdr_type = VE2RPC_MEMORY_CMA;
	pRB_info->hdr_addr = head->paddr;
	pRB_info->hdr_size = sizeof(struct _tagRingBufferHeader);
	pRB_info->hdr_cached = (uintptr_t)head->vaddr;
	pRB_info->hdr_uncached = (uintptr_t)head->vaddr;
	pRB_info->hdr_limit =
		(uintptr_t)head->vaddr + sizeof(struct _tagRingBufferHeader);
	pRB_info->hdr_hdl = head;

	pRB_info->bufex_type = VE2RPC_MEMORY_NONE;
	pRB_info->bufex_addr = 0;
	pRB_info->bufex_size = 0;

	prb->pRBH = (volatile struct _tagRingBufferHeader *)head->vaddr;
	prb->pRBH->size = htonl(bodysize);
	prb->pRBH->numOfReadPtr = htonl(1);
	prb->pRBH->beginAddr = htonl(body->paddr);
	prb->pRBH->writePtr = htonl(body->paddr);
	prb->pRBH->readPtr[0] = htonl(body->paddr);
	prb->pRBH->readPtr[1] = htonl(body->paddr);
	prb->pRBH->readPtr[2] = htonl(body->paddr);
	prb->pRBH->readPtr[3] = htonl(body->paddr);
#define VRB_STRUCT_VERSION 1
#define VRPC_FLASH_FORMAT_SEND_BUF_ID 5
	prb->pRBH->reserve2 = 0;
	prb->pRBH->reserve3 = 0;
	prb->pRBH->bufferID = htonl(type);

	if (RINGBUFFER_FAKE == type)
		return 0;

	mutex_lock(&hndl->lock);
	ringbuffer.instanceID = htonl(hndl->instanceID);
	ringbuffer.readPtrIndex = 0;
	ringbuffer.pinID = 0;
	ringbuffer.pRINGBUFF_HEADER = htonl(head->paddr);
	ret = _ve2rpc_shuttle(hndl, VIDEO_RPC_COMMON_ToAgent_InitRingBuffer,
			      &ringbuffer, sizeof(ringbuffer), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret)
		return ret;

	return 0;
}

static int _ve2rpc_open(struct ve2rpc *hndl, int type, struct v4l2_fh *fh)
{
	VIDEO_RPC_INSTANCE instance;
	unsigned int ret;
	RPCRES_LONG retval;
	hndl->fh = fh;
	hndl->vcpu_ept_info = get_ve2_krpc_info();
	ret = krpc_info_init(hndl->vcpu_ept_info, "ve2rpc", krpc_vcpu_cb);
	hndl->vcpu_ept_info->priv = (void *)hndl;

	instance.type = htonl(type);
	ret = _ve2rpc_shuttle(hndl, VIDEO_RPC_COMMON_ToAgent_Create, &instance,
			      sizeof(instance), &retval, sizeof(retval));
	if (ret) {
		vpu_err("fail to open decoder(%s)\n",
			V4L2_TYPE_TO_STR(hndl->type));
		return (-EPERM);
	}

	mutex_lock(&hndl->lock);
	hndl->instanceType = type;
	if (htonl(retval.result) == S_OK) {
		hndl->instanceID = htonl(retval.data);
	} else {
		vpu_err("fail to get instance(%s)\n",
			V4L2_TYPE_TO_STR(hndl->type));
		mutex_unlock(&hndl->lock);
		return (-EPERM);
	}
	mutex_unlock(&hndl->lock);
	return 0;
}

int ve2rpc_close(struct ve2rpc *hndl)
{
	if (!hndl) {
		vpu_err("Invaild handle\n");
		return -EPERM;
	}

	mutex_lock(&hndl->lock);
	if (hndl->instanceID) {
		uint32_t instanceID;
		int ret;

		instanceID = htonl(hndl->instanceID);
		ret = _ve2rpc_shuttle(hndl, VIDEO_RPC_COMMON_ToAgent_Destroy,
				      &instanceID, sizeof(instanceID), NULL, 0);
		if (ret) {
			mutex_unlock(&hndl->lock);
			vpu_err("fail to close decoder(%s)\n",
				V4L2_TYPE_TO_STR(hndl->type));
			return (-EPERM);
		}
	}

	krpc_info_deinit(hndl->vcpu_ept_info);
	krpc_ept_info_put(hndl->vcpu_ept_info);

	hndl->instanceType = -1;
	hndl->instanceID = -1;
	mutex_unlock(&hndl->lock);
	return 0;
}

int ve2rpc_connect(struct ve2rpc *src, struct ve2rpc *dst)
{
	RPC_CONNECTION connection;
	int ret;

	if (!src || !src->instanceID) {
		pr_err("%s out handler is NULL", __func__);
		return -EPERM;
	}

	if (!dst || !dst->instanceID) {
		pr_err("%s cap handler is NULL", __func__);
		return -EPERM;
	}

	mutex_lock(&dst->lock);
	mutex_lock(&src->lock);

	memset(&connection, 0, sizeof(connection));
	connection.srcInstanceID = htonl(src->instanceID);
	connection.desInstanceID = htonl(dst->instanceID);

	ret = _ve2rpc_shuttle(src, VIDEO_RPC_COMMON_ToAgent_Connect,
			      &connection, sizeof(connection), NULL, 0);
	mutex_unlock(&src->lock);
	mutex_unlock(&dst->lock);
	if (ret) {
		vpu_err("fail to connect decoder\n");
		return (-EPERM);
	}
	return 0;
}

int ve2rpc_setRole(struct ve2rpc *hndl, VIDEO_STREAM_TYPE type)
{
	VIDEO_RPC_DEC_INIT info;
	int ret;

	if (!hndl || !hndl->instanceID) {
		vpu_err("hndl = %p hndl->instanceID=%x ", hndl,
			(hndl == NULL) ? 0 : hndl->instanceID);
		return -EPERM;
	}

	mutex_lock(&hndl->lock);

	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->instanceID);
	info.set_speed.instanceID = htonl(hndl->instanceID);
	info.set_speed.displaySpeed = 0;
	info.set_speed.decodeSkip = 0;
	info.type = htonl(type);
	ret = _ve2rpc_shuttle(hndl, VIDEO_RPC_DEC_ToAgent_Init, &info,
			      sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		vpu_err("fail to connect decoder\n");
		return (-EPERM);
	}

	return 0;
}

int ve2rpc_set_cmprs(struct ve2rpc *hndl, uint8_t enable)
{
	VIDEO_RPC_DEC_CMPRS_CTRL info;
	int ret;

	if (!hndl || !hndl->instanceID)
		return -1;

	mutex_lock(&hndl->lock);
	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->instanceID);
	info.mode = 0; //0: lossless, 1: lossy
	info.ratio = htonl(CMPRS_RATIO_75);
	info.enable = enable;

	ret = _ve2rpc_shuttle(hndl, VIDEO_RPC_DEC_ToAgent_CmprsCtrl, &info,
			      sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		vpu_err("fail to do cmd %d \n",
			VIDEO_RPC_DEC_ToAgent_CmprsCtrl);
		return (-EPERM);
	}

	return 0;
}

int ve2rpc_enable_drop_cnt(struct ve2rpc *hndl)
{
	VIDEO_RPC_RESOURCE_INFO info;
	int ret;

	if (!hndl || !hndl->instanceID)
		return -1;

	mutex_lock(&hndl->lock);
	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->instanceID);
	info.resource_ctrl_sets = htonl(0x80);

	ret = _ve2rpc_shuttle(hndl, VIDEO_RPC_ToAgent_SetResourceInfo, &info,
			      sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		vpu_err("fail to do cmd %d \n",
			VIDEO_RPC_ToAgent_SetResourceInfo);
		return (-EPERM);
	}

	return 0;
}

int ve2rpc_get_bs_info(struct device *dev, void *fh, uint32_t codec,
		       uint32_t size, void *buf, uint32_t *width,
		       uint32_t *height, uint32_t *ddr_width,
		       uint32_t *ddr_height, uint32_t *min_reqbuf,
		       uint32_t *bit_depth)
{
	struct ve2rpc *hndl;
	struct ve2rpc_ion_object *bs_buf;
	VIDEO_RPC_DEC_BITSTREAM_BUFFER input = { 0 };
	VIDEO_RPC_DEC_PV_RESULT output = { 0 };
	uint32_t paddr = 0;
	int ret = 0;

	hndl = kzalloc(sizeof(struct ve2rpc), GFP_KERNEL);
	hndl->dev = dev;
	hndl->vcpu_ept_info = get_ve2_krpc_info();
	ret = krpc_info_init(hndl->vcpu_ept_info, "ve2rpc", krpc_vcpu_cb);
	hndl->vcpu_ept_info->priv = (void *)hndl;

	bs_buf = _ve2rpc_media_ion_create(hndl, size);
	if (IS_ERR(bs_buf)) {
		vpu_err("allocate bs_buf fail, No Memory\n");
		return -ENOMEM;
	}

	memcpy_toio(bs_buf->vaddr, buf, size);

	paddr = (uint32_t)bs_buf->paddr; // TODO
	input.bsBase = htonl(paddr);
	input.bsSize = htonl(size);
	if (codec == V4L2_PIX_FMT_HEVC) {
		input.type = htonl(VF_TYPE_VIDEO_H265_DECODER);
	} else if (codec == V4L2_PIX_FMT_VP9) {
		input.type = htonl(VF_TYPE_VIDEO_VP9_DECODER);
	} else if (codec == V4L2_PIX_FMT_AV1) {
		input.type = htonl(VF_TYPE_VIDEO_AV1_DECODER);
	} else {
		input.type = htonl(VF_TYPE_VIDEO_H265_DECODER);
		vpu_err("%s, unsupport codec %d", __func__, codec);
	}
	ret = _ve2rpc_shuttle(hndl, VIDEO_RPC_DEC_ToAgent_ParseResolution,
			      &input, sizeof(VIDEO_RPC_DEC_BITSTREAM_BUFFER),
			      &output, sizeof(VIDEO_RPC_DEC_PV_RESULT));
	if (ret) {
		vpu_err("fail to do cmd ParseResolution %d \n",
			VIDEO_RPC_DEC_ToAgent_BitstreamValidation);
	}

	*width = htonl(output.width) >> 16;
	*height = htonl(output.height) >> 16;
	*ddr_width = htonl(output.width) & 0xffff;
	*ddr_height = htonl(output.height) & 0xffff;
	*bit_depth = htonl(output.bit_depth);
	*min_reqbuf = htonl(output.DPB_size);

	vpu_input_dbg("ve2rpc_get_bs_info width %d, height %d, ddr_width %d, ddr_height %d, bit_depth %d, min_reqbuf %d", *width, *height, *ddr_width, *ddr_height, *bit_depth, *min_reqbuf);

	_ve2rpc_ion_free(hndl, bs_buf);
	krpc_info_deinit(hndl->vcpu_ept_info);
	krpc_ept_info_put(hndl->vcpu_ept_info);
	kfree(hndl);

	return ret;
}

static int _ve2rpc_common(struct ve2rpc *hndl, int cmd)
{
	uint32_t instanceID;
	int ret;

	if (!hndl || !hndl->instanceID) {
		vpu_err("handle = %p hndl->instanceID=%d ", hndl,
			(hndl == NULL) ? 0 : hndl->instanceID);
		return -EPERM;
	}
	mutex_lock(&hndl->lock);
	instanceID = htonl(hndl->instanceID);
	ret = _ve2rpc_shuttle(hndl, cmd, &instanceID, sizeof(instanceID), NULL,
			      0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		vpu_err("fail to do cmd %d \n", cmd);
		return (-EPERM);
	}
	return 0;
}

int ve2rpc_run(struct ve2rpc *hndl)
{
	int ret;
	ret = _ve2rpc_common(hndl, VIDEO_RPC_COMMON_ToAgent_Run);

	return ret;
}

int ve2rpc_pause(struct ve2rpc *hndl)
{
	int ret;

	ret = _ve2rpc_common(hndl, VIDEO_RPC_COMMON_ToAgent_Pause);
	return ret;
}

int ve2rpc_flush(struct ve2rpc *hndl)
{
	int ret;

	ret = _ve2rpc_common(hndl, VIDEO_RPC_COMMON_ToAgent_Flush);
	return ret;
}

int ve2rpc_stop(struct ve2rpc *hndl)
{
	int ret;

	ret = _ve2rpc_common(hndl, VIDEO_RPC_COMMON_ToAgent_Stop);
	return ret;
}

static void _inband_memcpy(uint8_t *des, uint8_t *src, unsigned int size)
{
	unsigned int *src_int32 = (unsigned int *)src;
	unsigned int *des_int32 = (unsigned int *)des;
	unsigned int i;

	for (i = 0; i < (size / sizeof(int)); i++)
		des_int32[i] = htonl(src_int32[i]);

	dsb(sy);
}

static int _ve2rpc_write(struct ve2rpc_ringbuf_t *ringbuf, int type,
			 uint8_t *buf, int size)
{
	volatile struct ve2rpc_ringbuf_t *rb = ringbuf;
	uint32_t wp, rp;
	void *wptr, *next, *addr_end;
#ifdef ENABLE_TEE_DRM_FLOW
	uint32_t wptr_s, next_s, addr_end_s;
	uint8_t secure = rb->secure;
#endif
	uint8_t over = 0;

	if (!ringbuf) {
		vpu_err("invaild input ringbuf %p", ringbuf);
		return -EPERM;
	}

	mutex_lock(&ringbuf->lock);

	wp = htonl(rb->pRBH->writePtr);
	rp = htonl(rb->pRBH->readPtr[0]);

	if (rp > wp && (int)(rp - wp - 1) < size) {
		mutex_unlock(&ringbuf->lock);
		return -EPERM;
	}

	if (wp > rp && (int)(rp + rb->size - wp - 1) < size) {
		mutex_unlock(&ringbuf->lock);
		return -EPERM;
	}

#ifdef ENABLE_TEE_DRM_FLOW
	if (secure) {
		wptr_s = wp;
		addr_end_s = rb->phyaddr + rb->size;
		next_s = wptr_s + size;
		if (next_s >= addr_end_s) {
			over = 1;
			next_s -= rb->size;
		}
	} else
#endif
	{
		wptr = rb->buf_cached + (wp - rb->phyaddr);
		addr_end = rb->buf_cached + rb->size;
		next = wptr + size;
		if (next >= addr_end) {
			over = 1;
			next -= rb->size;
		}
	}

	if (over) {
		int size0 = 0;
		int size1 = 0;
#ifdef ENABLE_TEE_DRM_FLOW
		if (secure) {
			size0 = rb->phyaddr + rb->size - wptr_s;
			size1 = size - size0;
		} else
#endif
		{
			size0 = rb->buf_cached + rb->size - (uint8_t *)wptr;
			size1 = size - size0;
		}

		if (type == RINGBUFFER_STREAM) {
#ifdef ENABLE_TEE_DRM_FLOW
			if (secure && rb->teeapi_ctx &&
			    rb->teeapi_tee_session) {
				int ret = 0;
				if (rb->memory == V4L2_MEMORY_MMAP) {
					if (size0 != 0) {
						ret = ta_TEEapi_memcpy_a7(
							(struct tee_context *)
								rb->teeapi_ctx,
							rb->teeapi_tee_session,
							(uintptr_t)wptr_s, buf,
							size0);
						if (ret < 0) {
							vpu_err("%s %d, ta_TEEapi_memcpy_a7 fail ret:%d\n",
								__func__,
								__LINE__, ret);
						}
					}

					if (size1 != 0) {
						ret = ta_TEEapi_memcpy_a7(
							(struct tee_context *)
								rb->teeapi_ctx,
							rb->teeapi_tee_session,
							rb->phyaddr,
							buf + size0, size1);
						if (ret < 0) {
							vpu_err("%s %d, ta_TEEapi_memcpy_a7 fail ret:%d\n",
								__func__,
								__LINE__, ret);
						}
					}
				} else if (rb->memory == V4L2_MEMORY_DMABUF) {
					if (size0 != 0) {
						ret = ta_TEEapi_memcpy(
							(struct tee_context *)
								rb->teeapi_ctx,
							rb->teeapi_tee_session,
							(uintptr_t)wptr_s,
							(uintptr_t)buf, size0);
						if (ret < 0)
							vpu_err("%s %d, ta_TEEapi_memcpy_a7 fail ret:%d\n",
								__func__,
								__LINE__, ret);
					}

					if (size1 != 0) {
						ret = ta_TEEapi_memcpy(
							(struct tee_context *)
								rb->teeapi_ctx,
							rb->teeapi_tee_session,
							rb->phyaddr,
							(uintptr_t)(buf +
								    size0),
							size1);
						if (ret < 0)
							vpu_err("%s %d, ta_TEEapi_memcpy_a7 fail ret:%d\n",
								__func__,
								__LINE__, ret);
					}
				} else
					vpu_err("%s %d, Not support this memory mode %d\n",
						__func__, __LINE__, rb->memory);
			} else
#endif
			{
				if (size0 != 0)
					memcpy_toio(wptr, buf, size0);

				if (size1 != 0)
					memcpy_toio(rb->buf_cached, buf + size0,
						    size1);
			}
		} else {
			if (size0 != 0)
				_inband_memcpy(wptr, buf, (unsigned int)size0);

			if (size1 != 0)
				_inband_memcpy(rb->buf_cached, buf + size0,
					       (unsigned int)size1);
		}
	} else {
		if (type == RINGBUFFER_STREAM) {
#ifdef ENABLE_TEE_DRM_FLOW
			if (secure && rb->teeapi_ctx &&
			    rb->teeapi_tee_session) {
				int ret = 0;
				if (rb->memory == V4L2_MEMORY_MMAP) {
					ret = ta_TEEapi_memcpy_a7(
						(struct tee_context *)
							rb->teeapi_ctx,
						rb->teeapi_tee_session,
						(uintptr_t)wptr_s, buf, size);
					if (ret < 0)
						vpu_err("%s ta_TEEapi_memcpy_a7 fail ret:%d\n",
							__func__, ret);
				} else if (rb->memory == V4L2_MEMORY_DMABUF) {
					ret = ta_TEEapi_memcpy(
						(struct tee_context *)
							rb->teeapi_ctx,
						rb->teeapi_tee_session,
						(uintptr_t)wptr_s,
						(uintptr_t)buf, size);
					if (ret < 0)
						vpu_err("%s ta_TEEapi_memcpy_a7 fail ret:%d\n",
							__func__, ret);
				} else
					vpu_err("%s %d, Not support this memory mode %d\n",
						__func__, __LINE__, rb->memory);
			} else
#endif
			{
				memcpy_toio(wptr, buf, size);
			}
		} else {
			_inband_memcpy(wptr, buf, (unsigned int)size);
		}
	}
#ifdef ENABLE_TEE_DRM_FLOW
	if (secure)
		rb->pRBH->writePtr = htonl(next_s);
	else
#endif
		rb->pRBH->writePtr =
			htonl(rb->phyaddr + ((uint8_t *)next - rb->buf_cached));

	dsb(sy);
	mutex_unlock(&ringbuf->lock);

	return 0;
}

int ve2rpc_inband_newseg(struct ve2rpc *vout_hndl)
{
	int ret;
	NEW_SEG cmd;
	struct ve2rpc_ringbuf_t *cmb_ringbuf = &vout_hndl->sub_rb;
	struct ve2rpc_ringbuf_t *bs_ringbuf = &vout_hndl->main_rb;

	mutex_lock(&bs_ringbuf->lock);
	cmd.header.type = INBAND_CMD_TYPE_NEW_SEG;
	cmd.header.size = sizeof(NEW_SEG);
	cmd.wPtr = htonl(bs_ringbuf->pRBH->writePtr);
	mutex_unlock(&bs_ringbuf->lock);
	ret = _ve2rpc_write(cmb_ringbuf, RINGBUFFER_COMMAND, (uint8_t *)&cmd,
			    sizeof(NEW_SEG));

	return ret;
}

int ve2rpc_inband_decode(struct ve2rpc *vout_hndl, DECODE_MODE mode)
{
	DECODE_NEW cmd;
	int64_t relativePTS = 0;
	int64_t duration = -1;
	int ret;

	cmd.header.type = INBAND_CMD_TYPE_DECODE;
	cmd.header.size = sizeof(DECODE_NEW);
	cmd.RelativePTSH = relativePTS >> 32;
	cmd.RelativePTSL = relativePTS;
	cmd.PTSDurationH = duration >> 32;
	cmd.PTSDurationL = duration;
	cmd.skip_GOP = 0;
	cmd.mode = mode;
	cmd.isHM91 = 0;
	cmd.useAbsolutePTS = 1;

	ret = _ve2rpc_write(&vout_hndl->sub_rb, RINGBUFFER_COMMAND,
			    (uint8_t *)&cmd, sizeof(DECODE_NEW));
	return ret;
}

int ve2rpc_space(struct ve2rpc_ringbuf_t *ringbuf, char bRead, char bAtom)
{
	volatile struct ve2rpc_ringbuf_t *rb = ringbuf;
	unsigned int wp, rp, space;

	if (!ringbuf) {
		vpu_err("invaild input ringbuf %p", ringbuf);
		return -EPERM;
	}
	mutex_lock(&ringbuf->lock);
	wp = htonl(rb->pRBH->writePtr);
	rp = htonl(rb->pRBH->readPtr[0]);
	if (bRead) {
		space = (rp > wp) ? (wp + rb->size - rp - rb->dummy) :
				    (wp - rp);
	} else {
		if (bAtom && rp <= wp) {
			int s1 = rb->phyaddr + rb->size - wp - 1;
			int s2 = rp - rb->phyaddr - 1;
			space = (s1 > s2) ? s1 : s2;
		} else {
			space = (rp > wp) ? (rp - wp - 1) :
					    (rp + rb->size - wp - 1);
		}
	}
	dsb(sy);
	mutex_unlock(&ringbuf->lock);

	return space;
}

static int ve2rpc_inband_pts2(struct ve2rpc_ringbuf_t *ringbuf, uint32_t wptr,
			      uint64_t pts, uint64_t pts2, uint32_t length,
			      uint32_t flag)
{
	int ret;
	PTS_INFO2 cmd;
	if (!length) {
		vpu_err("\n wrong length %d\n", length);
		return 0;
	}

	cmd.header.type = INBAND_CMD_TYPE_PTS;
	cmd.header.size = sizeof(PTS_INFO2);
	cmd.wPtr = wptr;
	cmd.PTSH = pts >> 32;
	cmd.PTSL = pts;
	cmd.PTSH2 = pts2 >> 32;
	cmd.PTSL2 = pts2;
	cmd.length = length;
	cmd.flag = flag;

	ret = _ve2rpc_write(ringbuf, RINGBUFFER_COMMAND, (uint8_t *)&cmd,
			    sizeof(PTS_INFO2));

	return ret;
}

static int __maybe_unused
ve2rpc_inband_eof(struct ve2rpc_ringbuf_t *ringbuf,
		  volatile struct _tagRingBufferHeader *pRBH, uint64_t pts,
		  uint64_t pts2, uint32_t length, uint32_t flag)
{
	int ret;
	PTS_INFO2 cmd;
	uint32_t wptr;

	if (!length) {
		vpu_err("\n wrong length %d\n", length);
		return 0;
	}

	wptr = htonl(pRBH->writePtr);
	cmd.header.type = VIDEO_INBAND_CMD_TYPE_FRAME_BOUNDARY;
	cmd.header.size = sizeof(PTS_INFO2);
	cmd.wPtr = wptr;
	cmd.PTSH = pts >> 32;
	cmd.PTSL = pts;
	cmd.PTSH2 = pts2 >> 32;
	cmd.PTSL2 = pts2;
	cmd.length = 0;
	cmd.flag = 0;
	ret = _ve2rpc_write(ringbuf, RINGBUFFER_COMMAND, (uint8_t *)&cmd,
			    sizeof(PTS_INFO2));
	return ret;
}

int ve2rpc_inband_eos_event(struct ve2rpc_ringbuf_t *ringbuf,
			    volatile struct _tagRingBufferHeader *pRBH,
			    unsigned int event_id)
{
	int ret;
	EOS cmd;
	uint32_t wptr;
	wptr = htonl(pRBH->writePtr);
	cmd.header.type = INBAND_CMD_TYPE_EOS;
	cmd.header.size = sizeof(EOS);
	cmd.eventID = event_id;
	cmd.wPtr = wptr;

	ret = _ve2rpc_write(ringbuf, RINGBUFFER_COMMAND, (uint8_t *)&cmd,
			    sizeof(EOS));

	vpu_input_dbg("Send EOS event\n");

	return ret;
}

int ve2rpc_inband_add_buf(struct ve2rpc_ringbuf_t *ringbuf,
			  struct rtkve2_reg_dpb_t dpb,
			  uint32_t cmprs_hdr_lu, uint32_t cmprs_hdr_ch,
			  uint32_t cmprs_hdr_size)
{
	int ret = 0;
	FRAME_INFO_IN cmd = {0};

	if(dpb.bit_depth != 8 && dpb.bit_depth != 10)
		dpb.bit_depth = 0;

	cmd.header.type = VIDEO_FRAME_INBAND_ADD;
	cmd.header.size = sizeof(FRAME_INFO_IN);
	cmd.lu_addr = dpb.y_phy_addr;
	cmd.ch_addr = dpb.c_phy_addr;;
	cmd.decimate_lu_addr = 0;
	cmd.decimate_ch_addr = 0;
	cmd.width = dpb.width;
	cmd.height = dpb.height;
	cmd.decimate_width = 0;
	cmd.decimate_height = 0;
	cmd.ddr_width = dpb.dpb_width;
	cmd.ddr_height = dpb.dpb_height;
	cmd.bit_depth = dpb.bit_depth;
	cmd.decimate_en = 0;
	cmd.decimate_ratio = 0;
	if (cmprs_hdr_size) {
		cmd.cmprs_hdr_size = cmprs_hdr_size;
		cmd.cmprs_hdr_lu = cmprs_hdr_lu;
		cmd.cmprs_hdr_ch = cmprs_hdr_ch;
		cmd.cmprs_en = 1;
		cmd.lossy_en = 0;
		cmd.lossy_ratio = 1;
	}

	ret = _ve2rpc_write(ringbuf, RINGBUFFER_FRAME_USER, (uint8_t *)&cmd,
			    cmd.header.size);

	return ret;
}

int ve2rpc_inband_del_buf(struct ve2rpc_ringbuf_t *ringbuf, uint32_t y_phy_addr)
{
	int ret = 0;
	FRAME_INFO_OUT cmd;

	cmd.header.type = VIDEO_FRAME_INBAND_DELETE;
	cmd.header.size = sizeof(FRAME_INFO_OUT);
	cmd.lu_addr = y_phy_addr;

	ret = _ve2rpc_write(ringbuf, RINGBUFFER_FRAME_USER, (uint8_t *)&cmd,
			    cmd.header.size);

	return ret;
}

int ve2rpc_write_bs(struct ve2rpc *hndl, uint8_t *buf, uint32_t len,
		    uint64_t pts, uint32_t sequence)
{
	int space, ret;
	uint32_t writePtrforPTS;
	//int i=0;

	space = ve2rpc_space(&hndl->main_rb, 0, 1);
	if (space < len) {
		vpu_input_dbg("bitstream buffer is too small %d, %d\n", space,
			      len);
		return -ENOSPC;
	}

	space = ve2rpc_space(&hndl->sub_rb, 0, 1);
	if (space < 512) {
		vpu_input_dbg("command buffer is too small %d\n", space);
		return -ENOSPC;
	}

	writePtrforPTS = htonl(hndl->main_rb.pRBH->writePtr);
#ifdef REORDER_PTS
	if (hndl->is_pts_reorder) {
		ret = ve2rpc_inband_pts2(&hndl->sub_rb, writePtrforPTS,
					 (uint64_t)sequence << 32, pts, len, 0);
	} else
#endif
	{
		ret = ve2rpc_inband_pts2(&hndl->sub_rb, writePtrforPTS,
					 (uint64_t)pts, sequence, len, 0);
	}
	if (ret) {
		vpu_err("ve2rpc_inband_pts2 fail %d\n", ret);
		return -EFAULT;
	}

	{
		ret = _ve2rpc_write(&hndl->main_rb, RINGBUFFER_STREAM, buf,
				    len);
	}

	if (ret) {
		vpu_err("_ve2rpc_write RINGBUFFER_STREAM fail %d\n", ret);
		return -EFAULT;
	}
	if (hndl->vType == VIDEO_STREAM_H265) {
		ret = ve2rpc_inband_eof(&hndl->sub_rb, hndl->main_rb.pRBH,
					(uint64_t)sequence << 32, pts, len, 0);
		if (ret) {
			vpu_err("ve2rpc_inband_eof fail %d\n", ret);
			return -EFAULT;
		}
	}
	return 0;
}

static uint8_t *_get_buflock_va(struct ve2rpc_ion_object *buflock,
				uint32_t buflock_pa)
{
	if ((buflock_pa >= buflock->paddr) &&
	    (buflock_pa < (buflock->paddr + buflock->size))) {
		return buflock->vaddr + buflock_pa - buflock->paddr;
	}
	return NULL;
}

void __maybe_unused dump_buflock(struct ve2rpc *cap_hndl)
{
	struct ve2rpc_ion_object *buflock;
	volatile uint8_t *buflock_va;
	int i = 0;

	buflock = (struct ve2rpc_ion_object *)cap_hndl->buflock;

	for (i = 0; i < VE2_MAX_DPB_NUM; i++) {
		buflock_va = cap_hndl->buflock_info[i].buflock_va;
		if (buflock_va) {
			pr_err("buflock_va %d) 0x%x, %d, used %d", i, cap_hndl->buflock_info[i].buflock_pa, *buflock_va, cap_hndl->buflock_info[i].is_used);
		} else
			vpu_input_dbg("idx %d, state unknow, NULL, 0\n", i);
	}
}

int ve2rpc_get_decoded_frm_cnt(struct ve2rpc *hndl)
{
	struct ve2rpc_ringbuf_t *prb;
	int disp_frm_cnt = 0;
	volatile uint32_t wptr = 0;
	volatile uint32_t rptr = 0;

	prb = &hndl->main_rb;
	mutex_lock(&prb->lock);
	wptr = htonl(prb->pRBH->writePtr);
	rptr = htonl(prb->pRBH->readPtr[0]);
	if (prb->pRBH && wptr != rptr) {
		if (wptr >= rptr)
			disp_frm_cnt = (wptr - rptr) /
				       sizeof(ve2rpc_flash_frame_info_t);
		else
			disp_frm_cnt = (wptr + prb->size - rptr) /
				       sizeof(ve2rpc_flash_frame_info_t);
	}
	mutex_unlock(&prb->lock);

	return disp_frm_cnt;
}

static struct rtkve2_dpb_t *ve2rpc_find_dpb(struct ve2rpc *cap_hndl,
	struct vb2_v4l2_buffer *vb2_v4l2_buffer)
{
	int i = 0;

	for (i = 0; i < VE2_MAX_DPB_NUM; i++) {
		if ((cap_hndl->dpb[i].status != RTKVE2_DPB_ST_EMPTY) &&
		    (cap_hndl->dpb[i].vb2_v4l2_buf == vb2_v4l2_buffer))
			return (void *)&(cap_hndl->dpb[i]);
	}

	return NULL;
}


void ve2rpc_update_dpb_st(struct ve2rpc *cap_hndl,
	struct vb2_v4l2_buffer *vb2_v4l2_buffer, unsigned int status)
{
	struct rtkve2_dpb_t *dpb = NULL;

	if (cap_hndl == NULL) {
		vpu_err("%s cap_hndl is NULL", __func__);
		return;
	}

	mutex_lock(&cap_hndl->dpb_mutex);
	dpb = ve2rpc_find_dpb(cap_hndl, vb2_v4l2_buffer);
	if (!dpb) {
		vpu_err("%s, can't find vb2_v4l2_buffer:%px in dpb[]\n",
			__func__, vb2_v4l2_buffer);
		mutex_unlock(&cap_hndl->dpb_mutex);
		return;
	}

	dpb->status = status;
	mutex_unlock(&cap_hndl->dpb_mutex);

}

int ve2rpc_add_travel_entry(struct ve2rpc *cap_hndl,
	uint32_t y_phy_addr, uint32_t buflock_phy_addr,
	void *vb2_v4l2_buf, uint32_t idx)
{
	struct traveling_frame_st *tframe;
	int ret = 0;

	tframe = kmalloc(sizeof(struct traveling_frame_st),
			 GFP_KERNEL | __GFP_ZERO);
	if (!tframe) {
		vpu_err("kmalloc traveling_frame_st fail\n");
		ret = -ENOMEM;
		goto exit;
	}

	tframe->phy_addr = y_phy_addr;
	tframe->vb2_q_idx = idx;
	tframe->vb2_v4l2_buf = vb2_v4l2_buf;
	tframe->buflock_phy_addr = buflock_phy_addr;

	mutex_lock(&cap_hndl->travel_mutex);
	list_add_tail(&tframe->list, &cap_hndl->qframe.tlist);
	mutex_unlock(&cap_hndl->travel_mutex);
exit:
	return ret;
}

int ve2rpc_add_capbuf_to_dpb(struct ve2rpc *out_hndl, struct ve2rpc *cap_hndl,
			     struct rtkve2_reg_dpb_t dpb, bool is_cmprs)
{
	int ret = 0;
	int i = 0;
	uint32_t cmprs_hdr_lu = 0;
	uint32_t cmprs_hdr_ch = 0;
	uint32_t cmprs_hdr_size = 0;
	struct ve2rpc_ion_object *cmprs_hdr_buf = NULL;

	if ((out_hndl == NULL) || (cap_hndl == NULL) || (dpb.size == 0) ||
	    (dpb.y_phy_addr == 0) || (dpb.vb2_v4l2_buf == NULL)) {
		vpu_err("%s invalid parameters out_hndl:%px, cap_hndl:%px, size:%d, phys_addr:0x%llx, vb2_v4l2_buf:%px\n",
			__func__, out_hndl, cap_hndl, dpb.size, dpb.y_phy_addr,
			dpb.vb2_v4l2_buf);
		ret = -EINVAL;
		goto exit;
	}

	if (is_cmprs) {
		uint32_t cmprs_lu_size1 = ALIGN((ALIGN(dpb.width / 8, 64) * dpb.height / 128), 64) * 32;
		uint32_t cmprs_lu_size2 = ALIGN((ALIGN(dpb.height / 8, 64) * dpb.width / 128), 64) * 32;
		uint32_t cmprs_hdr_lu_size = max(cmprs_lu_size1, cmprs_lu_size2);

		cmprs_hdr_size = cmprs_hdr_lu_size * 3 / 2;
		if (cmprs_hdr_size != 0) {
			cmprs_hdr_buf = _ve2rpc_media_ion_create(cap_hndl, cmprs_hdr_size);
			if (IS_ERR(cmprs_hdr_buf))
				vpu_err("allocate cmprs buffer fail, No Memory\n");

			cmprs_hdr_lu = cmprs_hdr_buf->paddr;
			cmprs_hdr_ch = cmprs_hdr_lu + cmprs_hdr_lu_size;
		} else {
			vpu_err("cmprs_hdr_size is incorrect (%dx%d). Back to normal mode.", dpb.width, dpb.height);
		}
	}

	mutex_lock(&cap_hndl->dpb_mutex);
	for (i = 0; i < cap_hndl->dpb_cnt; i++)
		if (cap_hndl->dpb[i].y_phy_addr == dpb.y_phy_addr)
			break;
	if (i != cap_hndl->dpb_cnt) {
		if (cmprs_hdr_buf)
			_ve2rpc_ion_free(cap_hndl, cmprs_hdr_buf);
		mutex_unlock(&cap_hndl->dpb_mutex);
		goto exit;
	}
	for (i = 0; i < VE2_MAX_DPB_NUM; i++)
		if (cap_hndl->dpb[i].status == RTKVE2_DPB_ST_EMPTY)
			break;

	if (i == VE2_MAX_DPB_NUM) {
		if (cmprs_hdr_buf)
			_ve2rpc_ion_free(cap_hndl, cmprs_hdr_buf);
		vpu_err("all dpb buffers are used\n");
		mutex_unlock(&cap_hndl->dpb_mutex);
		ret = -ENOMEM;
		goto exit;
	}

	cap_hndl->dpb[i].size = dpb.size;
	cap_hndl->dpb[i].status = RTKVE2_DPB_ST_VALID;
	cap_hndl->dpb[i].y_phy_addr = dpb.y_phy_addr;
	cap_hndl->dpb[i].c_phy_addr = dpb.c_phy_addr;
	cap_hndl->dpb[i].vb2_v4l2_buf = dpb.vb2_v4l2_buf;
	cap_hndl->dpb[i].idx = dpb.idx;
	cap_hndl->dpb[i].cmprs_hdr_buf = cmprs_hdr_buf;
	cap_hndl->dpb_cnt++;
	mutex_unlock(&cap_hndl->dpb_mutex);

	ret = ve2rpc_inband_add_buf(&out_hndl->dpb_rb, dpb,
				    cmprs_hdr_lu, cmprs_hdr_ch, cmprs_hdr_size);
exit:
	return ret;
}

int ve2rpc_del_capbuf_from_dpb(struct ve2rpc *out_hndl, struct ve2rpc *cap_hndl)
{
	int ret = 0;
	int i = 0;

	if ((out_hndl == NULL) || (cap_hndl == NULL)) {
		vpu_err("%s invalid parameters out_hndl:%px, cap_hndl:%px\n",
			__func__, out_hndl, cap_hndl);
		ret = -EINVAL;
		goto exit;
	}

	mutex_lock(&cap_hndl->dpb_mutex);
	for (i = 0; i < VE2_MAX_DPB_NUM; i++) {
		if (cap_hndl->dpb[i].status != RTKVE2_DPB_ST_EMPTY) {
			ret = ve2rpc_inband_del_buf(
				&out_hndl->dpb_rb, cap_hndl->dpb[i].y_phy_addr);
			if (cap_hndl->dpb[i].cmprs_hdr_buf != 0)
				_ve2rpc_ion_free(cap_hndl, cap_hndl->dpb[i].cmprs_hdr_buf);
			cap_hndl->dpb[i].y_phy_addr = 0;
			cap_hndl->dpb[i].status = RTKVE2_DPB_ST_EMPTY;
		}
	}
	cap_hndl->dpb_cnt = 0;

	mutex_unlock(&cap_hndl->dpb_mutex);
exit:
	return ret;
}

struct rtkve2_buflock_t *ve2rpc_get_unused_buflock(struct ve2rpc *cap_hndl)
{
	int i = 0;
	struct rtkve2_buflock_t *buflock = NULL;
	volatile uint8_t *buflock_va;

	if (!cap_hndl) {
		vpu_err("%s cap isn't ready\n", __func__);
		return 0;
	}
	mutex_lock(&cap_hndl->buflock_mutex);
	for (i = 0; i < VE2_MAX_DPB_NUM; i++) {
		buflock_va = cap_hndl->buflock_info[i].buflock_va;
		if (cap_hndl->buflock_info[i].is_used == 0 &&
			*buflock_va == E_BUFLOCK_ST_NORMAL) {
			buflock = &cap_hndl->buflock_info[i];
			cap_hndl->buflock_info[i].is_used = 1;
			break;
		}
	}
	mutex_unlock(&cap_hndl->buflock_mutex);

	return buflock;
}

static int ve2rpc_clear_buflock_thread(void *data)
{
	struct ve2rpc *hndl = (struct ve2rpc *)data;
	struct ve2rpc_qframe_st *entry = NULL;
	struct ve2rpc_qframe_st *tmp_entry = NULL;
	uint32_t buflock_pa = 0;
	volatile uint8_t *buflock_va;
	int i = 0;

	while (1) {
		int ret;
		ret = wait_event_interruptible_timeout(
			hndl->buflock_waitq,
			kthread_should_stop(),
			msecs_to_jiffies(5));

		if (kthread_should_stop() || (ret == -ERESTART)) {
			mutex_lock(&hndl->qframe.lock);
			list_for_each_entry_safe (entry, tmp_entry, &hndl->qframe.list, list) {
				list_del(&entry->list);
				kfree(entry);
			}
			mutex_unlock(&hndl->qframe.lock);
			break;
		}

		mutex_lock(&hndl->qframe.lock);
		list_for_each_entry_safe (entry, tmp_entry, &hndl->qframe.list, list) {
			buflock_va = entry->buflock_va;
			buflock_pa = entry->buflock_pa;
			mutex_lock(&hndl->buflock_mutex);
			if (hndl->buflock_force_quit) {
				list_del(&entry->list);
				kfree(entry);
			} else if (*buflock_va == E_BUFLOCK_ST_RELEASE) {
				*buflock_va = E_BUFLOCK_ST_NORMAL;
				for (i = 0; i < VE2_MAX_DPB_NUM; i++) {
					if (buflock_pa == hndl->buflock_info[i].buflock_pa) {
						hndl->buflock_info[i].is_used = 0;
					}
				}
				list_del(&entry->list);
				kfree(entry);
			}
			mutex_unlock(&hndl->buflock_mutex);
		}
		mutex_unlock(&hndl->qframe.lock);
	}

	hndl->buflock_force_quit = 0;
	return 1;
}

int ve2rpc_add_to_buflock_clear_q(struct ve2rpc *cap_hndl, uint32_t buflock_pa,
	volatile uint8_t *buflock_va)
{
	struct ve2rpc_qframe_st *re_qframe;

	if (!cap_hndl->buflock_thread) {
		init_waitqueue_head(&cap_hndl->buflock_waitq);
		cap_hndl->buflock_thread = kthread_run(
			ve2rpc_clear_buflock_thread, cap_hndl, "buflockthread");
	}

	re_qframe = kmalloc(sizeof(struct ve2rpc_qframe_st),
			GFP_KERNEL | __GFP_ZERO);
	if (!re_qframe) {
		vpu_err("kmalloc ve2rpc_qframe_st fail\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&re_qframe->list);
	re_qframe->buflock_va = buflock_va;
	re_qframe->buflock_pa = buflock_pa;
	re_qframe->hndl = cap_hndl;

	mutex_lock(&cap_hndl->qframe.lock);
	list_add_tail(&re_qframe->list, &cap_hndl->qframe.list);
	mutex_unlock(&cap_hndl->qframe.lock);

	wake_up_interruptible(&cap_hndl->buflock_waitq);
	return 0;
}

static void ve2rpc_add_to_msgQ(struct ve2rpc *cap_hndl, uint32_t buflock_pa)
{
	uint32_t frm_idx = 0;
	volatile ve2rpc_flash_frame_info_t *frame;

	frm_idx = cap_hndl->outputRingIdx++;
	if (cap_hndl->outputRingIdx >= VE2_MAX_DPB_NUM)
		cap_hndl->outputRingIdx = 0;

	frame = (volatile ve2rpc_flash_frame_info_t *)cap_hndl->frame[frm_idx];

	memset_volatile(frame, 0, sizeof(ve2rpc_flash_frame_info_t));
	frame->nPicFlags = 0;
	frame->nPicWidth = PIC_SIZE_INVALID;
	frame->nPicHeight = PIC_SIZE_INVALID;
	frame->nClkTimeHigh = -1;
	frame->nClkTimeLow = -1;
	frame->nBufLockPhysicalAddr = htonl(buflock_pa);
	frame->pUserData = true;
	dsb(sy);
}

int ve2rpc_qframe(struct ve2rpc *cap_hndl, dma_addr_t phy_addr,
		  uint32_t vb2_q_idx)
{
	struct ve2rpc_ion_object *buflock;
	struct ve2rpc_ringbuf_t *prb;
	struct rtkve2_buflock_t * buflock_info = NULL;
	uint32_t buflock_pa = 0;
	volatile uint8_t *buflock_va;
	struct traveling_frame_st *tentry;
	struct traveling_frame_st *tmp_tentry = NULL;
	int ret = 0;

	if (!cap_hndl || !cap_hndl->instanceID) {
		vpu_err("handle = %p hndl->instanceID=%d ", cap_hndl,
			(cap_hndl == NULL) ? 0 : cap_hndl->instanceID);
		return -EPERM;
	}

	prb = &cap_hndl->main_rb;

	if (vb2_q_idx >= cap_hndl->dpb_cnt) {
		vpu_err("invaild vb2_q_idx %d %d", vb2_q_idx,
			cap_hndl->dpb_cnt);
		return -EPERM;
	}

	mutex_lock(&cap_hndl->travel_mutex);

	list_for_each_entry_safe (tentry, tmp_tentry,
		&cap_hndl->qframe.tlist, list) {
		if ((tentry->phy_addr == phy_addr) &&
			(tentry->vb2_q_idx == vb2_q_idx)) {
				buflock_pa = tentry->buflock_phy_addr;
				tentry->phy_addr = 0;
				tentry->vb2_q_idx = -1;
				tentry->vb2_v4l2_buf = NULL;
				list_del(&tentry->list);
				kfree(tentry);
				break;
			}
		}

	mutex_unlock(&cap_hndl->travel_mutex);

	buflock = (struct ve2rpc_ion_object *)cap_hndl->buflock;
	if (buflock_pa) {
		buflock_va = _get_buflock_va(buflock, buflock_pa);
		if (!buflock_va) {
			vpu_err("can't find buflock_va by buflock_pa %x, buflock %p\n",
				buflock_pa, buflock);
			return -EINVAL;
		}

		mutex_lock(&cap_hndl->buflock_mutex);
		if (*buflock_va == E_BUFLOCK_ST_LOCK) {
			*buflock_va = E_BUFLOCK_ST_UNLOCK;
			dsb(sy);
		}
		mutex_unlock(&cap_hndl->buflock_mutex);

		ret = ve2rpc_add_to_buflock_clear_q(cap_hndl, buflock_pa, buflock_va);
		if (ret)
			return ret;
	}

	buflock_info = ve2rpc_get_unused_buflock(cap_hndl);
	if (!buflock_info) {
		vpu_err("can't find buflock_pa\n");
		return -EINVAL;
	}

	buflock_va = buflock_info->buflock_va;

	mutex_lock(&prb->lock);
	ve2rpc_add_to_msgQ(cap_hndl, buflock_info->buflock_pa);
	mutex_unlock(&prb->lock);

	return ret;
}

static uint64_t _ve2rpc_update_PTS(struct ve2rpc *cap_hndl,
			     uint32_t ptsHigh, uint32_t ptsLow)
{
	uint64_t frmPTS = 0;
#ifdef REORDER_PTS
	uint64_t estPTS = ULLONG_MAX;
	uint32_t estIdx = UINT_MAX;
	struct pts_queue *entry = NULL;
	struct pts_queue *match_entry = NULL;
	struct pts_queue *tmp_entry = NULL;

	if (cap_hndl->is_pts_reorder) {
		mutex_lock(&cap_hndl->pts_mutex);
		//find the smallest pts
		if (cap_hndl->pts_queue) {
			list_for_each_entry (entry, cap_hndl->pts_queue, list) {
				if (entry && entry->pts < estPTS) {
					estPTS = entry->pts;
					estIdx = entry->idx;
					match_entry = entry;
				}
			}
		}

		//delete this node
		if (cap_hndl->pts_queue) {
			list_for_each_entry_safe (entry, tmp_entry, cap_hndl->pts_queue,
						  list) {
				if (entry && entry == match_entry) {
					list_del(&entry->list);
					kfree(entry);
				}
			}
		}
		mutex_unlock(&cap_hndl->pts_mutex);

		frmPTS = estPTS;
		if (ptsHigh != estIdx)
			vpu_output_dbg("Refine PTS\n");

		if (cap_hndl->pre_pts > frmPTS)
			vpu_warn("PTS roll back %lld => %lld\n", cap_hndl->pre_pts,
				 frmPTS);

		cap_hndl->pre_pts = frmPTS;

		if (!ptsLow) {
			cap_hndl->lastID = ptsHigh;
		} else {
			if (ptsHigh == cap_hndl->lastID || ptsLow < 0) {
				frmPTS += (ptsLow * 100 / 9);
			} else {
				vpu_warn("invalid pts %d %d", ptsHigh, ptsLow);
			}
		}
	}else
#endif
	{
		frmPTS = ptsHigh;
		frmPTS = (frmPTS << 32) | ptsLow;
	}

	return frmPTS;
}

static void _ve2rpc_update_buflock(struct ve2rpc *cap_hndl,
			     uint32_t buflock_phy_addr)
{
	struct ve2rpc_ion_object *buflock;
	volatile uint8_t *buflock_va;

	mutex_lock(&cap_hndl->buflock_mutex);
	buflock = (struct ve2rpc_ion_object *)cap_hndl->buflock;
	buflock_va =
		_get_buflock_va(buflock, buflock_phy_addr);
	if (!buflock_va)
		vpu_err("can't find buflock_va by buflock_pa 0x%x, buflock %p\n",
			buflock_phy_addr, buflock);

	if (*buflock_va == E_BUFLOCK_ST_TOUCH) {
		*buflock_va = E_BUFLOCK_ST_LOCK;
		dsb(sy);
	}
	mutex_unlock(&cap_hndl->buflock_mutex);

}

static int _ve2rpc_update_dpb(struct ve2rpc *cap_hndl,
			     bool no_frame, void **disp_buf, uint32_t y_phy_addr, uint32_t *idx)
{
	int ret = 0;
	int i = 0;

	mutex_lock(&cap_hndl->dpb_mutex);
	if (no_frame == 0) {
		for (i = 0; i < VE2_MAX_DPB_NUM; i++) {
			if (cap_hndl->dpb[i].y_phy_addr == y_phy_addr) {
				*disp_buf = cap_hndl->dpb[i].vb2_v4l2_buf;
				*idx = cap_hndl->dpb[i].idx;
				break;
			}
		}

		if (i == VE2_MAX_DPB_NUM) {
			vpu_err("Can't find vb2_v4l2_buf for 0x%x\n",
				y_phy_addr);
			mutex_unlock(&cap_hndl->dpb_mutex);
			ret = -ENOBUFS;
			goto exit;
		}
	} else {
		for (i = VE2_MAX_DPB_NUM - 1; i >= 0; i--) {
			struct vb2_v4l2_buffer *buf = (struct vb2_v4l2_buffer *)cap_hndl->dpb[i].vb2_v4l2_buf;
			if (cap_hndl->dpb[i].status == RTKVE2_DPB_ST_VALID &&
					buf->vb2_buf.state == VB2_BUF_STATE_ACTIVE) {
				*disp_buf = cap_hndl->dpb[i].vb2_v4l2_buf;
				*idx = cap_hndl->dpb[i].idx;
				break;
			}
		}

		if (i == -1) {
			vpu_err("Can't find valid buffer for EOS\n");
			mutex_unlock(&cap_hndl->dpb_mutex);
			ret = -ENOBUFS;
			goto exit;
		}
	}
	mutex_unlock(&cap_hndl->dpb_mutex);

exit:
	return ret;
}

#ifdef PREPEND_METADATA
static void _ve2rpc_fill_frm_info(struct ve2rpc *cap_hndl,
			     volatile ve2rpc_flash_frame_info_t *frame,
			     struct vb2_v4l2_buffer *buf)
{
	struct ve_frame_info *ve2frame_info = NULL;
	unsigned int pic_cmprs_mode = 0;

	ve2frame_info = (struct ve_frame_info *)vb2_plane_vaddr(&buf->vb2_buf, 0);
	if (!ve2frame_info) {
		vpu_err("%s Can't get plane virtual address\n", __func__);
		return ;
	}

	ve2frame_info->yuvs.lumaOffTblAddr = 0xffffffff;
	ve2frame_info->yuvs.chromaOffTblAddr = 0xffffffff;
	ve2frame_info->yuvs.lumaOffTblAddrR = 0xffffffff;
	ve2frame_info->yuvs.chromaOffTblAddrR = 0xffffffff;
	ve2frame_info->yuvs.bufBitDepth = 8;
	ve2frame_info->yuvs.matrix_coefficients = 1;
	ve2frame_info->yuvs.tch_hdr_metadata[0] = -1;

	ve2frame_info->yuvs.Y_addr_Right = 0xffffffff;
	ve2frame_info->yuvs.U_addr_Right = 0xffffffff;
	ve2frame_info->yuvs.pLock_Right = 0xffffffff;

	ve2frame_info->rtk_meta_buf_id = 0x52544B6D; //RTKm
	ve2frame_info->is_ve1_buf = 0;

	pic_cmprs_mode = htonl(frame->nCmprsMode);

	ve2frame_info->yuvs.width = htonl(frame->nPicWidth);
	ve2frame_info->yuvs.height = htonl(frame->nPicHeight);
	ve2frame_info->yuvs.Y_addr = htonl(frame->nPicPhysicalAddr);
	ve2frame_info->yuvs.U_addr = htonl(frame->nPicCPhysicalAddr);
	ve2frame_info->yuvs.Y_pitch = pic_cmprs_mode ?
					      htonl(frame->nPicCmprsPitch) :
					      htonl(frame->nPicPitch);
	ve2frame_info->yuvs.C_pitch = pic_cmprs_mode ?
					      htonl(frame->nPicCmprsPitch) :
					      htonl(frame->nPicCPitch);
	ve2frame_info->yuvs.slice_height = htonl(frame->nPicHeight);
	ve2frame_info->yuvs.mode = htonl(frame->nInterlaceMode);

	ve2frame_info->yuvs.tvve_picture_width = htonl(frame->nSampleWidth);
	ve2frame_info->yuvs.tvve_lossy_en = (pic_cmprs_mode == 2) ? 1 : 0;
	ve2frame_info->yuvs.tvve_bypass_en = (pic_cmprs_mode == 0) ? 1 : 0;
	ve2frame_info->yuvs.tvve_qlevel_sel_y = htonl(frame->qlevel_sel_y);
	ve2frame_info->yuvs.tvve_qlevel_sel_c = htonl(frame->qlevel_sel_c);
	ve2frame_info->yuvs.is_ve_tile_mode = 0;
	ve2frame_info->yuvs.film_grain_metadat_addr =
		htonl(frame->film_grain_metadata_addr);
	ve2frame_info->yuvs.film_grain_metadat_size =
		htonl(frame->film_grain_metadata_size);
	ve2frame_info->yuvs.hdr_metadata_addr = htonl(frame->hdr_metadata_addr);
	ve2frame_info->yuvs.hdr_metadata_size = htonl(frame->hdr_metadata_size);
	ve2frame_info->yuvs.video_full_range_flag =
		htonl(frame->nVideoFullRangeFlag);
	ve2frame_info->yuvs.pFrameBufferDbg =
		0;
	ve2frame_info->yuvs.pixelAR_hor = htonl(frame->nPixelAR_hor);
	ve2frame_info->yuvs.pixelAR_ver = htonl(frame->nPixelAR_ver);

	ve2frame_info->yuvs.is_dolby_video = 0;
	ve2frame_info->yuvs.bufBitDepth = htonl(frame->nBitDepthLuma);
	ve2frame_info->yuvs.lumaOffTblAddr =
		pic_cmprs_mode ? htonl(frame->nPicYCmprsHdrAddr) : -1U;
	ve2frame_info->yuvs.chromaOffTblAddr =
		pic_cmprs_mode ? htonl(frame->nPicCCmprsHdrAddr) : -1U;
	ve2frame_info->yuvs.lumaOffTblSize = htonl(frame->max_cmprs_head_size);
	ve2frame_info->yuvs.chromaOffTblSize =
		htonl(frame->max_cmprs_head_size);

	ve2frame_info->yuvs.Combine_Y_Addr =
		htonl(frame->nLinearPicPhysicalAddr);
	ve2frame_info->yuvs.Combine_U_Addr =
		htonl(frame->nLinearPicCPhysicalAddr);
	ve2frame_info->yuvs.Combine_Width = htonl(frame->nLinearPicWidth);
	ve2frame_info->yuvs.Combine_Height = htonl(frame->nLinearPicHeight);
	ve2frame_info->yuvs.Combine_Y_Pitch = htonl(frame->nLinearPicPitch);

	if (cap_hndl->col_matrix.matrix_coefficients !=
	    COLOR_MATRIX_COEF_DEFAULT) {
		ve2frame_info->yuvs.matrix_coefficients =
			cap_hndl->col_matrix.matrix_coefficients;
		ve2frame_info->yuvs.transferCharacteristics =
			cap_hndl->col_matrix.transfer_characteristics;
		ve2frame_info->yuvs.display_primaries_x0 =
			cap_hndl->col_matrix.primary_r_chromaticity_x;
		ve2frame_info->yuvs.display_primaries_x1 =
			cap_hndl->col_matrix.primary_g_chromaticity_x;
		ve2frame_info->yuvs.display_primaries_x2 =
			cap_hndl->col_matrix.primary_b_chromaticity_x;
		ve2frame_info->yuvs.display_primaries_y0 =
			cap_hndl->col_matrix.primary_r_chromaticity_y;
		ve2frame_info->yuvs.display_primaries_y1 =
			cap_hndl->col_matrix.primary_g_chromaticity_y;
		ve2frame_info->yuvs.display_primaries_y2 =
			cap_hndl->col_matrix.primary_b_chromaticity_y;
		ve2frame_info->yuvs.white_point_x =
			cap_hndl->col_matrix.whitepoint_chromaticity_x;
		ve2frame_info->yuvs.white_point_y =
			cap_hndl->col_matrix.whitepoint_chromaticity_y;
		ve2frame_info->yuvs.max_display_mastering_luminance =
			cap_hndl->col_matrix.luminance_max;
		ve2frame_info->yuvs.min_display_mastering_luminance =
			cap_hndl->col_matrix.luminance_min;
	}

	if ((ve2frame_info->yuvs.transferCharacteristics == 1) ||
	    (ve2frame_info->yuvs.transferCharacteristics == 2) ||
	    ((htonl(frame->nHDR_Type) == 5 || htonl(frame->nHDR_Type) == 3) &&
	     ve2frame_info->yuvs.hdr_metadata_addr)) {
		/*HDR 10*/
		ve2frame_info->hdr_type = 2;
	} else if ((ve2frame_info->yuvs.transferCharacteristics == 6) ||
		   (htonl(frame->nHDR_Type) == 6 &&
		    ve2frame_info->yuvs.hdr_metadata_addr)) {
		/*HLG*/
		ve2frame_info->hdr_type = 3;
	} else if (htonl(frame->nHDR_Type) == 0 ||
		   htonl(frame->nHDR_Type) == 1) {
		ve2frame_info->hdr_type = 0;
	}

}
#endif

static int _ve2rpc_get_frame(struct ve2rpc *cap_hndl, void **disp_buf,
			     uint64_t *pts, int frm_idx, bool *eos,
			     bool *no_frame, uint32_t *no_show_frm_cnt, uint8_t secure)
{
	volatile ve2rpc_flash_frame_info_t *frame;
	uint32_t flags;
	uint32_t nVersion = 0;
	uint32_t ptsHigh = 0;
	uint32_t ptsLow = 0;
	uint32_t y_phy_addr = 0;
	uint32_t c_phy_addr = 0;
	uint32_t buflock_phy_addr = 0;
	uint32_t idx = 0xFFFFFFFF;
	struct traveling_frame_st *tentry;
	struct traveling_frame_st *tmp_tentry = NULL;
	int ret = 0;

	frame = (volatile ve2rpc_flash_frame_info_t *)cap_hndl->frame[frm_idx];

	frame->pUserData = false;
	dsb(sy);
	y_phy_addr = htonl(frame->nPicPhysicalAddr);
	c_phy_addr = htonl(frame->nPicCPhysicalAddr);
	buflock_phy_addr =  htonl(frame->nBufLockPhysicalAddr);
	*no_show_frm_cnt = htonl(frame->noShowFrame_count);
	ptsLow = htonl(frame->nPtsLow);
	ptsHigh = htonl(frame->nPtsHigh);

	flags = htonl(frame->nPicFlags);
	nVersion = htonl(frame->nVersion);

#ifdef ENABLE_SHOW_VIDEO_INFO
	vpu_keep_fm_info(htonl(frame->nPicWidth), htonl(frame->nPicHeight));
#endif // #ifdef ENABLE_SHOW_VIDEO_INFO

	dsb(sy);

	if (nVersion == 0x8001) {
		vpu_output_dbg("No decoded frame with no show frame count %d", *no_show_frm_cnt);
		ret = -ENODATA;
		*no_frame = true;
		ve2rpc_add_to_msgQ(cap_hndl, buflock_phy_addr);
		goto exit;
	}

	if(*no_show_frm_cnt)
		vpu_output_dbg("decoded frame with no show frame count %d", *no_show_frm_cnt);

	mutex_lock(&cap_hndl->travel_mutex);
	list_for_each_entry_safe (tentry, tmp_tentry, &cap_hndl->qframe.tlist,
		list) {
		struct vb2_v4l2_buffer *buf = (struct vb2_v4l2_buffer *)tentry->vb2_v4l2_buf;
		if (y_phy_addr && tentry->phy_addr == y_phy_addr &&
			buf->vb2_buf.state != VB2_BUF_STATE_ACTIVE ) {
			vpu_output_dbg("Waiting for show existing frame %d!!!!", buf->vb2_buf.state);
			ret = -EADDRINUSE;
			mutex_unlock(&cap_hndl->travel_mutex);
			return ret;
		}
	}
	mutex_unlock(&cap_hndl->travel_mutex);

	if (pts)
		*pts = _ve2rpc_update_PTS(cap_hndl, ptsHigh, ptsLow);

	*eos = false;
	*no_frame = false;
	if (flags & VRPC_FRAME_INFO_FLAG_EOS) {
		//The buflock may be E_BUFLOCK_ST_NORMAL when EOS
		*eos = true;
		vpu_output_dbg("%s eos %d, no_frame %d\n", __func__, *eos,
			       *no_frame);
	}

	if (y_phy_addr == 0 && c_phy_addr == 0) {
		*no_frame = true;
		if (flags == 0)
			vpu_err("Something wrong with the ring buffer logic");
	}

	_ve2rpc_update_buflock(cap_hndl, buflock_phy_addr);

	ret = _ve2rpc_update_dpb(cap_hndl, *no_frame, disp_buf, y_phy_addr, &idx);
	if (ret)
		goto exit;

#ifdef PREPEND_METADATA
	_ve2rpc_fill_frm_info(cap_hndl, frame, *disp_buf);
#endif

	ret = ve2rpc_add_travel_entry(cap_hndl,
		y_phy_addr, buflock_phy_addr, *disp_buf, idx);
exit:
	return ret;
}

int ve2rpc_dqframe(struct ve2rpc *cap_hndl, void *disp_buf, uint64_t *pts,
		   bool *eos, bool *no_frame, uint32_t *no_show_frm_cnt)
{
	struct ve2rpc_ringbuf_t *prb;

	int ret = 0;

	if (!cap_hndl || !cap_hndl->instanceID) {
		vpu_err("handle = %p hndl->instanceID=%d ", cap_hndl,
			(cap_hndl == NULL) ? 0 : cap_hndl->instanceID);
		return -EPERM;
	}

	prb = &cap_hndl->main_rb;
	mutex_lock(&prb->lock);

	if (prb->pRBH) {
		if (prb->pRBH->readPtr[0] == prb->pRBH->writePtr) {
			mutex_unlock(&prb->lock);
			return -EAGAIN;
		} else {
			int rp_idx;
			uint32_t next_rp;

			rp_idx = (htonl(prb->pRBH->readPtr[0]) -
				  htonl(prb->pRBH->beginAddr)) /
				 sizeof(ve2rpc_flash_frame_info_t);
			next_rp = htonl(prb->pRBH->beginAddr) +
				  ((rp_idx + 1) % VE2_MAX_DPB_NUM) *
					  sizeof(ve2rpc_flash_frame_info_t);

			ret = _ve2rpc_get_frame(cap_hndl, disp_buf, pts, rp_idx,
						eos, no_frame, no_show_frm_cnt, prb->secure);
			if (ret == -ENODATA) {
				ret = -ENODATA;
			} else if (ret != 0) {
				mutex_unlock(&prb->lock);
				if (ret != -EADDRINUSE)
					vpu_err("fail to get a frame, try again ret %d\n",
						ret);
				return -EAGAIN;
			}
			prb->pRBH->readPtr[0] = htonl(next_rp);
			dsb(sy);
		}
	} else {
		vpu_err("wrong ringbuffer header\n");
	}
	mutex_unlock(&prb->lock);

	return ret;
}
#ifdef REORDER_PTS
int ve2rpc_free_pts(struct ve2rpc *cap_hndl)
{
	struct pts_queue *entry;
	struct pts_queue *tmp_entry;

	if (!cap_hndl) {
		vpu_err("cap isn't ready\n");
		return -EPERM;
	}

	mutex_lock(&cap_hndl->pts_mutex);
	list_for_each_entry_safe (entry, tmp_entry, cap_hndl->pts_queue, list) {
		if (entry) {
			list_del(&entry->list);
			kfree(entry);
		}
	}
	mutex_unlock(&cap_hndl->pts_mutex);
	return 0;
}
#endif

int ve2rpc_free_travel_frame(struct ve2rpc *cap_hndl)
{
	struct traveling_frame_st *tentry;
	struct traveling_frame_st *tmp_tentry = NULL;

	if (!cap_hndl) {
		vpu_err("cap isn't ready\n");
		return -EPERM;
	}

	mutex_lock(&cap_hndl->travel_mutex);
	list_for_each_entry_safe (tentry, tmp_tentry, &cap_hndl->qframe.tlist,
				  list) {
		if (tentry) {
			tentry->phy_addr = 0;
			tentry->phy_addr = 0;
			tentry->vb2_q_idx = -1;
			tentry->vb2_v4l2_buf = NULL;
			list_del(&tentry->list);
			kfree(tentry);
		} else {
			vpu_err("tentry is NULL\n");
		}
	}
	mutex_unlock(&cap_hndl->travel_mutex);

	return 0;
}

int ve2rpc_reset_bs_ring_rwptr(struct ve2rpc *out_hndl)
{
	struct ve2rpc_ringbuf_t *prb;
	int ret = 0;

	if (!out_hndl) {
		vpu_err("out isn't ready\n");
		ret = -EPERM;
		goto exit;
	}

	prb = &out_hndl->main_rb;
	if (!prb) {
		vpu_err("%s ringbuffer isn't ready\n", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&prb->lock);

	if (prb->pRBH)
		prb->pRBH->readPtr[0] = prb->pRBH->writePtr;

	mutex_unlock(&prb->lock);
exit:
	return ret;
}

int ve2rpc_reset_msg_ring_rwptr(struct ve2rpc *cap_hndl)
{
	volatile ve2rpc_flash_frame_info_t *frame;
	volatile uint32_t rptr = 0;
	struct ve2rpc_ringbuf_t *prb;
	int ret = 0;

	if (!cap_hndl) {
		vpu_err("cap isn't ready\n");
		ret = -EPERM;
		goto exit;
	}

	prb = &cap_hndl->main_rb;
	if (!prb) {
		vpu_err("%s ringbuffer isn't ready\n", __func__);
		ret = -EPERM;
		goto exit;
	}

	mutex_lock(&prb->lock);

	while (prb->pRBH->readPtr[0] != prb->pRBH->writePtr) {
		rptr = (htonl(prb->pRBH->readPtr[0]) -
					  htonl(prb->pRBH->beginAddr)) /
					 sizeof(ve2rpc_flash_frame_info_t);
		frame = (volatile ve2rpc_flash_frame_info_t *)cap_hndl->frame[rptr];
		frame->pUserData = false;
		rptr = htonl(prb->pRBH->beginAddr) +
			  ((rptr + 1) % VE2_MAX_DPB_NUM) *
				  sizeof(ve2rpc_flash_frame_info_t);
		prb->pRBH->readPtr[0] = htonl(rptr);
		dsb(sy);
	}

	cap_hndl->outputRingIdx = (htonl(prb->pRBH->writePtr) -
				  htonl(prb->pRBH->beginAddr)) /
				 sizeof(ve2rpc_flash_frame_info_t);

	mutex_unlock(&prb->lock);
exit:
	return ret;
}

int ve2rpc_reset_buflock(struct ve2rpc *cap_hndl, bool force_unlock)
{
	volatile uint8_t *buflock_va;
	int i = 0;

	for (i = 0; i < VE2_MAX_DPB_NUM; i++) {
		int add_to_wait_q = 0;

		buflock_va = cap_hndl->buflock_info[i].buflock_va;
		mutex_lock(&cap_hndl->buflock_mutex);
		if (force_unlock) {
			if (*buflock_va != E_BUFLOCK_ST_NORMAL &&
				cap_hndl->buflock_info[i].is_used != 0) {
				*buflock_va = E_BUFLOCK_ST_UNLOCK;
				add_to_wait_q = 1;
			}
		}
		else {
			if (*buflock_va == E_BUFLOCK_ST_NORMAL) {
				cap_hndl->buflock_info[i].is_used = 0;
			} else if (*buflock_va == E_BUFLOCK_ST_TOUCH) {
				*buflock_va = E_BUFLOCK_ST_UNLOCK;
				add_to_wait_q = 1;
				cap_hndl->buflock_info[i].is_used = 0;
			} if (*buflock_va == E_BUFLOCK_ST_RELEASE) {
				*buflock_va = E_BUFLOCK_ST_NORMAL;
				cap_hndl->buflock_info[i].is_used = 0;
			}
		}
		dsb(sy);
		mutex_unlock(&cap_hndl->buflock_mutex);

		if (add_to_wait_q) {
			int ret = 0;
			ret = ve2rpc_add_to_buflock_clear_q(cap_hndl,
				cap_hndl->buflock_info[i].buflock_pa,
				cap_hndl->buflock_info[i].buflock_va);
			if (ret)
				vpu_err("%s add to buflock wq fail, ret %d", __func__, ret);
		}
	}

	return 0;
}

int ve2rpc_init_out_handle(struct device *dev, struct ve2rpc **handle,
			   uint8_t is_secure, struct v4l2_fh *fh)
{
	struct ve2rpc *hndl;
	int ret;

	if (!handle) {
		vpu_info("capture ve2 rpc is inited\n");
		return 0;
	}

	*handle = kzalloc(sizeof(struct ve2rpc), GFP_KERNEL);
	if (!*handle) {
		vpu_err("capture allocate ve2rpc handle fail\n");
		return -ENOMEM;
	}

	hndl = *handle;
	hndl->dev = dev;
	hndl->is_secure = is_secure;
	hndl->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	mutex_init(&hndl->lock);

	ret = _ve2rpc_open(hndl, VF_TYPE_VIDEO_MPEG2_DECODER, fh);
	if (ret) {
		vpu_err("ve2rpc_out fail to open vpu decoder\n");
		return -EPERM;
	}

	ret = ve2rpc_SetRingBuffer(hndl, &hndl->main_rb, 0x1000000,
				   RINGBUFFER_STREAM, is_secure);
	if (ret) {
		vpu_err("ve2rpc_out fail to initial bs rb\n");
		return -EPERM;
	}

	ret = ve2rpc_SetRingBuffer(hndl, &hndl->sub_rb, 0x40000,
				   RINGBUFFER_COMMAND, false);
	if (ret) {
		vpu_err("ve2rpc_out fail to initial inband rb\n");
		return -EPERM;
	}

	ret = ve2rpc_SetRingBuffer(hndl, &hndl->dpb_rb, 0x40000,
				   RINGBUFFER_FRAME_USER, false);
	if (ret) {
		vpu_err("ve2rpc_out fail to initial dpb rb\n");
		return -EPERM;
	}

	hndl->buflock = NULL;
	hndl->frame = NULL;

	return 0;
}

int ve2rpc_init_cap_handle(struct device *dev, struct ve2rpc **handle,
			   uint8_t is_secure, struct v4l2_fh *fh)
{
	struct ve2rpc *hndl;
	volatile uint8_t *buflock_va;
	uint32_t buflock_pa;
	volatile ve2rpc_flash_frame_info_t *frame;
	struct ve2rpc_ion_object *buflock;
	int ret;
	int i;

	if (!handle) {
		vpu_info("capture ve2 rpc is inited\n");
		return 0;
	}
#ifdef PREPEND_METADATA
	if (METADATA_OFFSET < sizeof(struct ve_frame_info)) {
		vpu_err("sizeof ve_frame_info %ld is over offset\n", sizeof(struct ve_frame_info));
		return -EPERM;
	}
#endif
	*handle = kzalloc(sizeof(struct ve2rpc), GFP_KERNEL);
	if (!*handle) {
		vpu_err("capture allocate ve2rpc handle fail\n");
		return -ENOMEM;
	}

	hndl = *handle;
	hndl->dev = dev;
	hndl->is_secure = is_secure;
	hndl->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	mutex_init(&hndl->lock);

	ret = _ve2rpc_open(hndl, VF_TYPE_FLASH, fh);
	if (ret) {
		vpu_err("ve2rpc_cap fail to open vpu flash\n");
		return -EPERM;
	}

	ret = ve2rpc_SetRingBuffer(hndl, &hndl->main_rb,
				   (sizeof(ve2rpc_flash_frame_info_t)) *
					   VE2_MAX_DPB_NUM,
				   RINGBUFFER_MESSAGE, false);
	if (ret) {
		vpu_err("ve2rpc_cap fail to initial rb\n");
		return -EPERM;
	}

	{
#define VRB_STRUCT_VERSION 1
#define VRPC_FLASH_FORMAT_SEND_BUF_ID 5
		hndl->main_rb.pRBH->reserve2 =
			htonl((VE2_MAX_DPB_NUM << 24 & 0xff000000) |
			      (is_secure << 16 & 0x00ff0000) |
			      (VRB_STRUCT_VERSION << 8 & 0xff00) |
			      VRPC_FLASH_FORMAT_SEND_BUF_ID);
#ifdef SUPPORT_ADAPTIVE_PLAYBACK
		hndl->main_rb.pRBH->reserve3 =
			htonl(1 /*width*/ << 16 | 1 /*height*/);
#endif
	}

	buflock = (void *)_ve2rpc_media_ion_create(hndl, 4096);
	if (IS_ERR(buflock)) {
		vpu_err("allocate buflock fail, No Memory\n");
		return -ENOMEM;
	}

	mutex_init(&hndl->buflock_mutex);
	hndl->buflock = (void *)buflock;
	buflock_va = (uint8_t *)buflock->vaddr;
	buflock_pa = (uint32_t)buflock->paddr;
	mutex_lock(&hndl->buflock_mutex);
	memset_volatile(buflock_va, E_BUFLOCK_ST_ERROR, buflock->size);
	mutex_unlock(&hndl->buflock_mutex);

	hndl->frame = kzalloc(sizeof(uintptr_t) * VE2_MAX_DPB_NUM, GFP_KERNEL);
	hndl->buflock_info =
		kzalloc(sizeof(struct rtkve2_buflock_t) * VE2_MAX_DPB_NUM, GFP_KERNEL);
	frame = (volatile ve2rpc_flash_frame_info_t *)
			hndl->main_rb.rbinfo.buf_uncached;

	hndl->outputRingIdx = 0;
	hndl->col_matrix.matrix_coefficients = COLOR_MATRIX_COEF_DEFAULT;
	for (i = 0; i < VE2_MAX_DPB_NUM; i++) {
		hndl->frame[i] = (uintptr_t)&frame[i];
		hndl->buflock_info[i].buflock_pa = buflock_pa + i * sizeof(uintptr_t);
		hndl->buflock_info[i].buflock_va = buflock_va + i * sizeof(uintptr_t);
		hndl->buflock_info[i].idx = i;
		/* DO NOT CHANGE THE ORDER BEGIN*/
		mutex_lock(&hndl->buflock_mutex);
		buflock_va[i * sizeof(uintptr_t)] = E_BUFLOCK_ST_NORMAL;
		dsb(sy);
		mutex_unlock(&hndl->buflock_mutex);
		frame[i].nBufLockPhysicalAddr = htonl(hndl->buflock_info[i].buflock_pa);
		dsb(sy);
		frame[i].nClkTimeHigh = -1;
		frame[i].nClkTimeLow = -1;
		frame[i].nPicPhysicalAddr =
			htonl((uint32_t)(uintptr_t)&frame[i]);
		frame[i].nPicCPhysicalAddr = 0;
		frame[i].nPicWidth = PIC_SIZE_INVALID;
		frame[i].nPicHeight = PIC_SIZE_INVALID;
		frame[i].pUserData = false;
		dsb(sy);
		/* DO NOT CHANGE THE ORDER END*/
	}
	INIT_LIST_HEAD(&hndl->qframe.list);
	INIT_LIST_HEAD(&hndl->qframe.tlist);
	mutex_init(&hndl->qframe.lock);
	mutex_init(&hndl->travel_mutex);
	mutex_init(&hndl->dpb_mutex);

	return 0;
}

int ve2rpc_uninit_handle(struct ve2rpc *hndl)
{
	if (!hndl) {
		vpu_err("invaild handler");
		return -EINVAL;
	}

	if (V4L2_TYPE_IS_CAPTURE(hndl->type)) {
		hndl->buflock_force_quit = 1;
		if(hndl->buflock_thread) {
			wake_up_interruptible(&hndl->buflock_waitq);
			kthread_stop(hndl->buflock_thread);
			hndl->buflock_thread = NULL;
		}
		mutex_destroy(&hndl->qframe.lock);
		mutex_destroy(&hndl->travel_mutex);
		mutex_destroy(&hndl->dpb_mutex);
	}

	mutex_destroy(&hndl->lock);

	_ve2rpc_ringbuf_release(hndl, &hndl->main_rb);
	if (V4L2_TYPE_IS_OUTPUT(hndl->type)) {
		_ve2rpc_ringbuf_release(hndl, &hndl->sub_rb);
		_ve2rpc_ringbuf_release(hndl, &hndl->cc_rb);
		_ve2rpc_ringbuf_release(hndl, &hndl->dpb_rb);
	}

	hndl->type = -1;

	if (hndl->buflock_info) {
		kfree(hndl->buflock_info);
		hndl->buflock_info = NULL;
	}

	if (hndl->buflock) {
		_ve2rpc_ion_free(hndl, hndl->buflock);
		hndl->buflock = NULL;
		mutex_destroy(&hndl->buflock_mutex);
	}

	if (hndl->frame) {
		kfree(hndl->frame);
		hndl->frame = NULL;
	}

	if (hndl)
		kfree(hndl);

	return 0;
}

#ifdef VPU_GET_CC
int ve2rpc_setDecoderCCBypass(struct ve2rpc *hndl, int mode)
{
	VIDEO_RPC_DEC_CC_BYPASS_MODE info;
	int ret;

	if (!hndl || !hndl->instanceID)
		return -1;

	mutex_lock(&hndl->lock);
	memset(&info, 0, sizeof(info));

	info.instanceID = htonl(hndl->instanceID);
	info.cc_mode = htonl(mode);

	ret = _ve2rpc_shuttle(hndl, VIDEO_RPC_DEC_ToAgent_SetDecoderCCBypass,
			      &info, sizeof(info), NULL, 0);
	mutex_unlock(&hndl->lock);
	if (ret) {
		vpu_err("fail to do cmd %d \n",
			VIDEO_RPC_DEC_ToAgent_SetDecoderCCBypass);
		return (-EPERM);
	}

	return 0;
}

int ve2rpc_readCcRingBuf(struct ve2rpc_ringbuf_t *prb, uint32_t length,
			 char *pcDst)
{
	int ret = -1;
	uint32_t read_len = 0, remind_len = 0, rp_offset = 0;
	uint32_t wp, rp, base, size, space;
	char *pcSrc = NULL;
	do {
		if (!prb) {
			vpu_err("%s : ERROR! (prb=%p)", __func__, prb);
			break;
		}
		mutex_lock(&prb->lock);
		wp = ntohl(prb->pRBH->writePtr);
		rp = ntohl(prb->pRBH->readPtr[0]);
		base = ntohl(prb->pRBH->beginAddr);
		size = ntohl(prb->pRBH->size);
		space = (rp > wp) ? rp - wp : size - (wp - rp);
		pcSrc = (char *)prb->buf_uncached;
		if (!pcSrc || !pcDst) {
			mutex_unlock(&prb->lock);
			vpu_err("%s : pcSrc=%p pcDst=%p", __func__, pcSrc,
				pcDst);
			break;
		}

		rp_offset = rp - base;
		if (wp >= rp) {
			if (wp >= rp + length)
				read_len = length;
			else
				read_len = wp - rp;
			memcpy(pcDst, pcSrc + rp_offset, read_len);
		} else {
			uint32_t top = base + size;
			uint32_t top_space = top - rp;
			if (top_space >= length) {
				read_len = length;
				memcpy(pcDst, pcSrc + rp_offset, read_len);
			} else {
				uint32_t data_bytes_in_buff =
					top_space + (wp - base);
				read_len = top_space;
				memcpy(pcDst, pcSrc + rp_offset, top_space);
				data_bytes_in_buff -= read_len;
				rp_offset += read_len;
				if (data_bytes_in_buff >= length - read_len) {
					remind_len = length - read_len;
				} else {
					remind_len = data_bytes_in_buff;
				}
				read_len += remind_len;
				memcpy(pcDst + top_space, pcSrc + rp_offset,
				       remind_len);
			}
		}
		rp += read_len;
		if (rp >= base + size)
			rp -= size;
		prb->pRBH->readPtr[0] = htonl(rp);
		ret = read_len;
		mutex_unlock(&prb->lock);
	} while (0);
	return ret;
}
#endif
