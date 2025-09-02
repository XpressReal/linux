//--=========================================================================--
//  This file is a part of VPU Reference API project
//-----------------------------------------------------------------------------
//
//       This confidential and proprietary software may be used only
//     as authorized by a licensing agreement from Chips&Media Inc.
//     In the event of publication, the following notice is applicable:
//
//            (C) COPYRIGHT 2006 - 2013  CHIPS&MEDIA INC.
//                      ALL RIGHTS RESERVED
//
//       The entire notice above must be reproduced on all authorized
//       copies.
//
//--=========================================================================--

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/tee_drv.h>
#include "ve1_vpuapifunc.h"
#include "ve1_product.h"

#ifdef DAH_222_PREALLOC_MV_SLICE_BUFFER
/*
 * Fixed DAH-222 , Stark NTS DRS AL1 video freeze
 * Sometime during video resultion change, ion realloc need cost long time.
 * In NonTunnel playback, we prealloc max buffer can reduce realloc time for these items.
 */
#include "ve1_fw.h"
#include "ve1_vpuconfig.h"
#endif

#ifdef VE1_CHECKSUM
#include <mcp_api.h>
#define HASH_SIZE 32
#endif

#include "ve1_fw.h"
#ifdef BIT_CODE_FILE_PATH
#include BIT_CODE_FILE_PATH
#endif

#define INVALID_CORE_INDEX_RETURN_ERROR(_coreIdx)                              \
	if (_coreIdx >= MAX_NUM_VPU_CORE)                                      \
		return -1;

Uint32 __VPU_BUSY_TIMEOUT = VPU_BUSY_CHECK_TIMEOUT;

unsigned long vpu_ring_valid_data(unsigned long ring_base,
				  unsigned long ring_limit,
				  unsigned long ring_rp, unsigned long ring_wp)
{
	if (ring_wp >= ring_rp) {
		return (ring_wp - ring_rp);
	} else {
		return (ring_limit - ring_base) - (ring_rp - ring_wp);
	}
}

#ifdef ENABLE_TEE_DRM_FLOW
extern int ta_TEEapi_init(struct tee_context **teeapi_ctx,
			  unsigned int *teeapi_tee_session);
extern int ta_TEEapi_deinit(struct tee_context *teeapi_ctx,
			    unsigned int teeapi_tee_session);
extern int ta_TEEapi_bitstreamprint(struct tee_context *teeapi_ctx,
				    unsigned int teeapi_tee_session,
				    unsigned int phy_addr, int size);
extern int ta_TEEapi_bitstreamout(struct tee_context *teeapi_ctx,
				  unsigned int teeapi_tee_session,
				  unsigned int srcPAddr, unsigned char *buf,
				  int size);
extern int ta_TEEapi_memcpy(struct tee_context *teeapi_ctx,
			    unsigned int teeapi_tee_session,
			    unsigned int dstPhysAddr, unsigned int srtPhysAddr,
			    int size);

RetCode VPU_InitWithBitcodeProtect(Uint32 coreIdx, const Uint16 *code,
				   Uint32 size, void *sess, void *rtk_sess,
				   void *filp)
{
	RetCode ret;

	if (coreIdx >= MAX_NUM_VPU_CORE)
		return RETCODE_INVALID_PARAM;
	if (code == NULL || size == 0)
		return RETCODE_INVALID_PARAM;

	if (vdi_init(coreIdx) < 0)
		return RETCODE_FAILURE;

	if (ProductVpuScan(coreIdx) == 0) {
		return RETCODE_NOT_FOUND_VPU_DEVICE;
	}

	InitCodecInstancePool(coreIdx);

	ret = ProductVpuReset(coreIdx, SW_RESET_ON_BOOT);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	ret = ProductVpuInitProtect(coreIdx, (void *)code, size, sess, rtk_sess,
				    filp);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}
	return RETCODE_SUCCESS;
}
#endif

static RetCode CheckInstanceValidity(CodecInst *pCodecInst)
{
	int i;
	vpu_instance_pool_t *vip;

	vip = (vpu_instance_pool_t *)vdi_get_instance_pool(pCodecInst->coreIdx);
	if (!vip)
		return RETCODE_INSUFFICIENT_RESOURCE;

	for (i = 0; i < MAX_NUM_INSTANCE; i++) {
		if ((CodecInst *)vip->codecInstPool[i] == pCodecInst)
			return RETCODE_SUCCESS;
	}

	return RETCODE_INVALID_HANDLE;
}

static RetCode CheckDecInstanceValidity(CodecInst *pCodecInst)
{
	RetCode ret;

	if (pCodecInst == NULL)
		return RETCODE_INVALID_HANDLE;

	ret = CheckInstanceValidity(pCodecInst);
	if (ret != RETCODE_SUCCESS) {
		return RETCODE_INVALID_HANDLE;
	}
	if (!pCodecInst->inUse) {
		return RETCODE_INVALID_HANDLE;
	}

	return ProductVpuDecCheckCapability(pCodecInst);
}

Int32 VPU_IsBusy(Uint32 coreIdx)
{
	Uint32 ret = 0;

	INVALID_CORE_INDEX_RETURN_ERROR(coreIdx);

	SetClockGate(coreIdx, 1);
	ret = ProductVpuIsBusy(coreIdx);
	SetClockGate(coreIdx, 0);

	return ret != 0;
}

Int32 VPU_IsInit(Uint32 coreIdx)
{
	Int32 pc;
	VLOG(TRACE, "[+] [%d]%s\n", __LINE__, __func__);

	INVALID_CORE_INDEX_RETURN_ERROR(coreIdx);

	SetClockGate(coreIdx, 1);
	pc = ProductVpuIsInit(coreIdx);
	SetClockGate(coreIdx, 0);

	VLOG(TRACE, "[-] [%d]%s.pc:0x%x\n", __LINE__, __func__, pc);
	return pc;
}

Int32 VPU_WaitInterrupt(Uint32 coreIdx, int timeout)
{
	Int32 ret;
	CodecInst *instance;

	INVALID_CORE_INDEX_RETURN_ERROR(coreIdx);

	if ((instance = GetPendingInst(coreIdx)) != NULL) {
		ret = ProductVpuWaitInterrupt(instance, timeout);
		VLOG(TRACE, "[%d]%s.coreIdx:%d.timeout:%d.ret:%d\n", __LINE__,
		     __func__, coreIdx, timeout, ret);
	} else {
		ret = -1;
		VLOG(TRACE, "[%d]%s.coreIdx:%d.timeout:%d.ret:-1\n", __LINE__,
		     __func__, coreIdx, timeout);
	}

	return ret;
}

Int32 VPU_WaitInterruptEx(VpuHandle handle, int timeout)
{
	Int32 ret;
	CodecInst *pCodecInst;

	pCodecInst = handle;

	INVALID_CORE_INDEX_RETURN_ERROR(pCodecInst->coreIdx);

	ret = ProductVpuWaitInterrupt(pCodecInst, timeout);

	return ret;
}

void VPU_ClearInterrupt(Uint32 coreIdx)
{
	/* clear all interrupt flags */
	ProductVpuClearInterrupt(coreIdx, 0xffff);
	VLOG(TRACE, "[%d]%s.coreIdx:%d\n", __LINE__, __func__, coreIdx);
}

void VPU_ClearInterruptEx(VpuHandle handle, Int32 intrFlag)
{
	CodecInst *pCodecInst;

	pCodecInst = handle;

	ProductVpuClearInterrupt(pCodecInst->coreIdx, intrFlag);
}

int VPU_GetMvColBufSize(CodStd codStd, int width, int height, int num)
{
	int size_mvcolbuf = ProductCalculateAuxBufferSize(
		AUX_BUF_TYPE_MVCOL, codStd, width, height);

	if (codStd == STD_AVC || codStd == STD_HEVC || codStd == STD_VP9)
		size_mvcolbuf *= num;

	return size_mvcolbuf;
}

RetCode VPU_GetFBCOffsetTableSize(CodStd codStd, int width, int height,
				  int *ysize, int *csize)
{
	if (ysize == NULL || csize == NULL)
		return RETCODE_INVALID_PARAM;

	*ysize = ProductCalculateAuxBufferSize(AUX_BUF_TYPE_FBC_Y_OFFSET,
					       codStd, width, height);
	*csize = ProductCalculateAuxBufferSize(AUX_BUF_TYPE_FBC_C_OFFSET,
					       codStd, width, height);

	return RETCODE_SUCCESS;
}

int VPU_GetFrameBufSize(int coreIdx, int stride, int height, int mapType,
			int format, int interleave, DRAMConfig *pDramCfg)
{
	int productId;
	UNREFERENCED_PARAMETER(interleave); /*!<< for backward compatiblity */

	if (coreIdx < 0 || coreIdx >= MAX_NUM_VPU_CORE)
		return -1;

	productId = ProductVpuGetId(coreIdx);

	return ProductCalculateFrameBufSize(productId, stride, height,
					    (TiledMapType)mapType,
					    (FrameBufferFormat)format,
					    (BOOL)interleave, pDramCfg);
}

int VPU_GetProductId(int coreIdx)
{
	Int32 productId = -1;

	VLOG(TRACE, "[+] [%d]%s\n", __LINE__, __func__);

	INVALID_CORE_INDEX_RETURN_ERROR(coreIdx);

	if (ProductVpuScan(coreIdx) == FALSE) {
		VLOG(ERR, "[-] [%d]%s.ProductVpuScan() fail.coreIdx:%d\n",
		     __LINE__, __func__, coreIdx);
		return -1;
	}
	productId = ProductVpuGetId(coreIdx);
	VLOG(TRACE, "[-] [%d]%s.productId:%d\n", __LINE__, __func__, productId);
	return productId;
}

int VPU_GetOpenInstanceNum(Uint32 coreIdx)
{
	INVALID_CORE_INDEX_RETURN_ERROR(coreIdx);

	return vdi_get_instance_num(coreIdx);
}

static RetCode InitializeVPU(Uint32 coreIdx, const Uint16 *code, Uint32 size, void *videc_dev)
{
	RetCode ret;
	VLOG(TRACE, "[+] [%d]%s.code:0x%px.size:%d\n", __LINE__, __func__, code,
	     size);

	if (vdi_init(coreIdx, videc_dev) < 0) {
		VLOG(ERR, "[%d]vdi_init() fail.coreIdx:%d", __LINE__, coreIdx);
		return RETCODE_FAILURE;
	}

	EnterLock(coreIdx);

	if (ProductVpuScan(coreIdx) == 0) {
		LeaveLock(coreIdx);
		VLOG(ERR, "[-] [%d]%s.RETCODE_NOT_FOUND_VPU_DEVICE\n", __LINE__,
		     __func__);
		return RETCODE_NOT_FOUND_VPU_DEVICE;
	}

	if (VPU_IsInit(coreIdx) != 0) {
		SetClockGate(coreIdx, 1);
		ProductVpuGetProductId(coreIdx);
		LeaveLock(coreIdx);
		VLOG(INFO, "[-] [%d]%s.RETCODE_CALLED_BEFORE\n", __LINE__,
		     __func__);
		return RETCODE_CALLED_BEFORE;
	} else if (size == 0) //RTK
	{
		VLOG(WARN,
		     "[%d]VPU didn't initial, we should re-load fw again\n",
		     __LINE__);
		LeaveLock(coreIdx);
		vdi_release(coreIdx);
		VLOG(TRACE, "[-] [%d]%s.RETCODE_NOT_FOUND_BITCODE_PATH\n",
		     __LINE__, __func__);
		return RETCODE_NOT_FOUND_BITCODE_PATH;
	}

	InitCodecInstancePool(coreIdx);

	SetClockGate(coreIdx, 1);
	ret = ProductVpuReset(coreIdx, SW_RESET_ON_BOOT);
	if (ret != RETCODE_SUCCESS) {
		LeaveLock(coreIdx);
		VLOG(ERR, "[-] [%d]%s.ProductVpuReset() fail.ret:%d\n",
		     __LINE__, __func__, ret);
		return ret;
	}

	ret = ProductVpuInit(coreIdx, (void *)code, size);
	if (ret != RETCODE_SUCCESS) {
		LeaveLock(coreIdx);
		VLOG(ERR, "[-] [%d]%s.ProductVpuInit() fail.ret:%d\n", __LINE__,
		     __func__, ret);
		return ret;
	}
	LeaveLock(coreIdx);
	VLOG(TRACE, "[-] [%d]%s\n", __LINE__, __func__);
	return RETCODE_SUCCESS;
}

RetCode VPU_Init(Uint32 coreIdx, void *videc_dev)
{
	RetCode ret = RETCODE_SUCCESS;
	VLOG(INFO, "[+] [%d]%s.coreIdx:%d\n", __LINE__, __func__, coreIdx);
	if (coreIdx >= MAX_NUM_VPU_CORE) {
		VLOG(ERR, "[-] [%d]%s.coreIdx:%d.ret:RETCODE_INVALID_PARAM\n",
		     __LINE__, __func__, coreIdx);
		return RETCODE_INVALID_PARAM;
	}

	ret = InitializeVPU(coreIdx, NULL, 0, videc_dev);
	VLOG(INFO, "[-] [%d]%s.coreIdx:%d.ret:%d\n", __LINE__, __func__,
	     coreIdx, ret);
	return ret;
}

RetCode VPU_InitWithBitcode(Uint32 coreIdx, const Uint16 *code, Uint32 size, void *videc_dev)
{
	RetCode ret = RETCODE_SUCCESS;
	VLOG(TRACE, "[+] [%d]%s.code:%px.size:%d\n", __LINE__, __func__, code,
	     size);
	if (coreIdx >= MAX_NUM_VPU_CORE) {
		VLOG(ERR, "[-] [%d]%s.RETCODE_INVALID_PARAM\n", __LINE__,
		     __func__);
		return RETCODE_INVALID_PARAM;
	}
	if (code == NULL || size == 0) {
		VLOG(ERR, "[-] [%d]%s.RETCODE_INVALID_PARAM\n", __LINE__,
		     __func__);
		return RETCODE_INVALID_PARAM;
	}

	ret = InitializeVPU(coreIdx, code, size, videc_dev);
	VLOG(TRACE, "[-] [%d]%s.ret:%d\n", __LINE__, __func__, ret);
	return ret;
}

RetCode VPU_DeInit(Uint32 coreIdx)
{
	int ret;

	if (coreIdx >= MAX_NUM_VPU_CORE)
		return RETCODE_INVALID_PARAM;

	ret = vdi_release(coreIdx);
	if (ret != 0) {
		VLOG(ERR, "[%d]%s.coreIdx:%d.ret:0x%x\n", __LINE__, __func__,
		     coreIdx, ret);
		return RETCODE_FAILURE;
	}

	VLOG(INFO, "[%d]%s.coreIdx:%d.ret:RETCODE_SUCCESS\n", __LINE__,
	     __func__, coreIdx);
	return RETCODE_SUCCESS;
}

RetCode VPU_GetVersionInfo(Uint32 coreIdx, Uint32 *versionInfo,
			   Uint32 *revision, Uint32 *productId)
{
	RetCode ret;

	if (coreIdx >= MAX_NUM_VPU_CORE)
		return RETCODE_INVALID_PARAM;

	EnterLock(coreIdx);

	if (ProductVpuIsInit(coreIdx) == 0) {
		LeaveLock(coreIdx);
		return RETCODE_NOT_INITIALIZED;
	}

	if (GetPendingInst(coreIdx)) {
		LeaveLock(coreIdx);
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	if (productId != NULL) {
		*productId = ProductVpuGetId(coreIdx);
	}
	ret = ProductVpuGetVersion(coreIdx, versionInfo, revision);

	LeaveLock(coreIdx);

	return ret;
}
RetCode VPU_DecOpen(DecHandle *pHandle, DecOpenParam *pop)
{
	CodecInst *pCodecInst = 0;
	DecInfo *pDecInfo;
	RetCode ret;
#if defined(ENABLE_TEE_DRM_FLOW)
	int ret_teeapi;
#endif

	VLOG(TRACE, "[+] [%d]%s\n", __LINE__, __func__);
	ret = ProductCheckDecOpenParam(pop);
	if (ret != RETCODE_SUCCESS) {
		VLOG(ERR, "[-] [%d]%s.ProductCheckDecOpenParam fail.ret:%d\n",
		     __LINE__, __func__, ret);
		return ret;
	}

	VLOG(TRACE, "[bitstreamFormat    ]: %d\n", pop->bitstreamFormat);
	VLOG(TRACE, "[bitstreamBuffer    ]: 0x%08x\n", pop->bitstreamBuffer);
	VLOG(TRACE, "[bitstreamBufferSize]: %d\n", pop->bitstreamBufferSize);
	VLOG(TRACE, "[mp4DeblkEnable     ]: %d\n", pop->mp4DeblkEnable);
	VLOG(TRACE, "[avcExtension       ]: %d\n", pop->avcExtension);
	VLOG(TRACE, "[mp4Class           ]: %d\n", pop->mp4Class);
	VLOG(TRACE, "[tiled2LinearEnable ]: %d\n", pop->tiled2LinearEnable);
	VLOG(TRACE, "[tiled2LinearMode   ]: %d\n", pop->tiled2LinearMode);
	VLOG(TRACE, "[wtlEnable          ]: %d\n", pop->wtlEnable);
	VLOG(TRACE, "[wtlMode            ]: %d\n", pop->wtlMode);
	VLOG(TRACE, "[cbcrInterleave     ]: %d\n", pop->cbcrInterleave);
	VLOG(TRACE, "[nv21               ]: %d\n", pop->nv21);
	VLOG(TRACE, "[cbcrOrder          ]: %d\n", pop->cbcrOrder);
	VLOG(TRACE, "[BWB                ]: %d\n", pop->bwbEnable);
	VLOG(TRACE, "[frameEndian        ]: %d\n", pop->frameEndian);
	VLOG(TRACE, "[streamEndian       ]: %d\n", pop->streamEndian);
	VLOG(TRACE, "[bitstreamMode      ]: %d\n", pop->bitstreamMode);
	VLOG(TRACE, "[coreIdx            ]: %d\n", pop->coreIdx);
	VLOG(TRACE, "[vbWork.size        ]: %d\n", pop->vbWork.size);
	VLOG(TRACE, "[vbWork.phys_addr   ]: 0x%08lx\n", pop->vbWork.phys_addr);
	VLOG(TRACE, "[vbWork.base        ]: 0x%08lx\n", pop->vbWork.base);
	VLOG(TRACE, "[vbWork.virt_addr   ]: 0x%08lx\n", pop->vbWork.virt_addr);
	VLOG(TRACE, "[vbWork.region      ]: %d\n", pop->vbWork.req_spec_region);
	VLOG(TRACE, "[fbc_mode           ]: %d\n", pop->fbc_mode);
	VLOG(TRACE, "[virtAxiID          ]: %d\n", pop->virtAxiID);
	VLOG(TRACE, "[bwOptimization     ]: %d\n", pop->bwOptimization);
	VLOG(TRACE, "[afbceEnable        ]: %d\n", pop->afbceEnable);
	VLOG(TRACE, "[afbceFormat        ]: %d\n", pop->afbceFormat);
	VLOG(TRACE, "[isUseProtectBuffer ]: %d\n", pop->isUseProtectBuffer);
	VLOG(TRACE, "[sess               ]: %p\n", pop->sess);
	VLOG(TRACE, "[rtk_sess           ]: %p\n", pop->rtk_sess);

	EnterLock(pop->coreIdx);

	if (VPU_IsInit(pop->coreIdx) == 0) {
		LeaveLock(pop->coreIdx);
		VLOG(ERR, "[-] [%d]%s.ret:RETCODE_NOT_INITIALIZED\n",
		     __LINE__, __func__);
		return RETCODE_NOT_INITIALIZED;
	}

	ret = GetCodecInstance(pop->coreIdx, &pCodecInst, pop->filp);
	if (ret != RETCODE_SUCCESS) {
		*pHandle = 0;
		LeaveLock(pop->coreIdx);
		VLOG(ERR, "[-] [%d]%s.GetCodecInstance fail.ret:%d\n", __LINE__,
		     __func__, ret);
		return ret;
	}

#if defined(ENABLE_TEE_DRM_FLOW)
	ret_teeapi =
		ta_TEEapi_init((struct tee_context **)&pCodecInst->teeapi_ctx,
			       &pCodecInst->teeapi_tee_session);
	if (ret_teeapi < 0) {
		*pHandle = 0;
		LeaveLock(pop->coreIdx);
		VLOG(ERR, "[-] [%d]%s.ta_TEEapi_init() fail.ret:%d\n", __LINE__,
		     __func__, ret_teeapi);
		return RETCODE_FAILURE;
	}
#endif

	pCodecInst->isDecoder = TRUE;
	*pHandle = pCodecInst;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	osal_memset(pDecInfo, 0x00, sizeof(DecInfo));
	osal_memcpy((void *)&pDecInfo->openParam, pop, sizeof(DecOpenParam));

	if (pop->bitstreamFormat == STD_MPEG4) {
		pCodecInst->codecMode = MP4_DEC;
		pCodecInst->codecModeAux = MP4_AUX_MPEG4;
	} else if (pop->bitstreamFormat == STD_AVC) {
		pCodecInst->codecMode = AVC_DEC;
		pCodecInst->codecModeAux = pop->avcExtension;
	} else if (pop->bitstreamFormat == STD_VC1) {
		pCodecInst->codecMode = VC1_DEC;
	} else if (pop->bitstreamFormat == STD_MPEG2) {
		pCodecInst->codecMode = MP2_DEC;
	} else if (pop->bitstreamFormat == STD_H263) {
		pCodecInst->codecMode = MP4_DEC;
		pCodecInst->codecModeAux = MP4_AUX_MPEG4;
	} else if (pop->bitstreamFormat == STD_UNKNOWN3) {
		pCodecInst->codecMode = DV3_DEC;
		pCodecInst->codecModeAux = MP4_AUX_UNKNOWN3;
	} else if (pop->bitstreamFormat == STD_RV) {
		pCodecInst->codecMode = RV_DEC;
	} else if (pop->bitstreamFormat == STD_AVS) {
		pCodecInst->codecMode = AVS_DEC;
	} else if (pop->bitstreamFormat == STD_THO) {
		pCodecInst->codecMode = VPX_DEC;
		pCodecInst->codecModeAux = VPX_AUX_THO;
	} else if (pop->bitstreamFormat == STD_VP3) {
		pCodecInst->codecMode = VPX_DEC;
		pCodecInst->codecModeAux = VPX_AUX_THO;
	} else if (pop->bitstreamFormat == STD_VP8) {
		pCodecInst->codecMode = VPX_DEC;
		pCodecInst->codecModeAux = VPX_AUX_VP8;
	} else if (pop->bitstreamFormat == STD_HEVC) {
		pCodecInst->codecMode = HEVC_DEC;
	} else if (pop->bitstreamFormat == STD_VP9) {
		pCodecInst->codecMode = W_VP9_DEC;
	} else if (pop->bitstreamFormat == STD_AVS2) {
		pCodecInst->codecMode = W_AVS2_DEC;
	} else {
		LeaveLock(pop->coreIdx);
		VLOG(ERR,
		     "[-] [%d]%s.ret:RETCODE_INVALID_PARAM.unknown bitstreamFormat:%d\n",
		     __LINE__, __func__, pop->bitstreamFormat);
		return RETCODE_INVALID_PARAM;
	}

	pDecInfo->enableAfbce = pop->afbceEnable;
	pDecInfo->afbceFormat = pop->afbceFormat;
	pDecInfo->wtlEnable = pop->wtlEnable;
	pDecInfo->wtlMode = pop->wtlMode;
	if (!pDecInfo->wtlEnable)
		pDecInfo->wtlMode = 0;

	pDecInfo->streamWrPtr = pop->bitstreamBuffer;
	pDecInfo->streamRdPtr = pop->bitstreamBuffer;
	pDecInfo->frameDelay = -1;
	pDecInfo->streamBufStartAddr = pop->bitstreamBuffer;
	pDecInfo->streamBufSize = pop->bitstreamBufferSize;
	pDecInfo->streamBufEndAddr =
		pop->bitstreamBuffer + pop->bitstreamBufferSize;
	pDecInfo->reorderEnable = VPU_REORDER_ENABLE;
	pDecInfo->mirrorDirection = MIRDIR_NONE;
#ifdef FIX_SET_GET_RD_PTR_BUG
#else
	pDecInfo->prevFrameEndPos = pop->bitstreamBuffer;
#endif

	//ENABLE_TEE_DRM_FLOW //For RTK DRM flow
	pCodecInst->isUseProtectBuffer = pop->isUseProtectBuffer;
	pCodecInst->sess = pop->sess;
	pCodecInst->rtk_sess = pop->rtk_sess;
	pCodecInst->enableDcsysDebug = pop->enableDcsysDebug;

	SetClockGate(pop->coreIdx, TRUE);
	if ((ret = ProductVpuDecBuildUpOpenParam(pCodecInst, pop)) !=
	    RETCODE_SUCCESS) {
		SetClockGate(pop->coreIdx, FALSE);
		*pHandle = 0;
		LeaveLock(pCodecInst->coreIdx);
		VLOG(ERR,
		     "[-] [%d]%s.ProductVpuDecBuildUpOpenParam fail.ret:%d\n",
		     __LINE__, __func__, ret);
		return ret;
	}
	SetClockGate(pop->coreIdx, FALSE);

	pDecInfo->tiled2LinearEnable = pop->tiled2LinearEnable;
	pDecInfo->tiled2LinearMode = pop->tiled2LinearMode;
	if (!pDecInfo->tiled2LinearEnable)
		pDecInfo->tiled2LinearMode = 0; //coda980 only

	if (!pDecInfo->wtlEnable) //coda980, wave320, wave410 only
		pDecInfo->wtlMode = 0;

	osal_memset((void *)&pDecInfo->cacheConfig, 0x00,
		    sizeof(MaverickCacheConfig));
#ifdef VE1_CHECKSUM
	memset(&pDecInfo->hashTable, 0, sizeof(vpu_buffer_t));
	pDecInfo->hashTable.size = HASH_SIZE;
	if (vdi_allocate_dma_memory(pCodecInst->coreIdx, &pDecInfo->hashTable,
				    pCodecInst->filp) < 0) {
		pDecInfo->hashTable.size = 0;
		VLOG(ERR, "fail to allocate checksum buffer");
		return RETCODE_FAILURE;
	}
	VLOG(TRACE,
	     "[%d]%s.vdi_allocate_dma_memory hashTable(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
	     __LINE__, __func__, pDecInfo->hashTable.phys_addr,
	     pDecInfo->hashTable.base, pDecInfo->hashTable.virt_addr,
	     pDecInfo->hashTable.size, pDecInfo->hashTable.req_spec_region);
#endif

#ifdef DAH_222_PREALLOC_MV_SLICE_BUFFER
	int size_mvcolbuf = 0;
	vpu_buffer_t vbBuffer;
	size_mvcolbuf = ((PREALLOC_MV_WIDTH + 31) & ~31) *
			((PREALLOC_MV_HEIGHT + 31) & ~31);
	size_mvcolbuf = (size_mvcolbuf * 3) / 2;
	size_mvcolbuf = (size_mvcolbuf + 4) / 5;
	size_mvcolbuf = ((size_mvcolbuf + 7) / 8) * 8;
	vbBuffer.size = size_mvcolbuf;
	vbBuffer.phys_addr = 0;
	for (int i = 0; i < PREALLOC_MV_BUFFER_COUNT; i++) {
		//ENABLE_TEE_DRM_FLOW
		if (pCodecInst->isUseProtectBuffer)
			vbBuffer.req_spec_region = VE_SECURE_PROTECTION;
		else
			vbBuffer.req_spec_region = 0;

		if (pDecInfo->vbMV[i].size == 0) {
			if (vdi_allocate_dma_memory(pCodecInst->coreIdx,
						    &vbBuffer) < 0) {
				return RETCODE_FAILURE;
			}
			pDecInfo->vbMV[i] = vbBuffer;
			VLOG(TRACE,
			     "[%d]%s.vdi_allocate_dma_memory vbMV[%d](0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, i, pDecInfo->vbMV[i].phys_addr,
			     pDecInfo->vbMV[i].base,
			     pDecInfo->vbMV[i].virt_addr,
			     pDecInfo->vbMV[i].size,
			     pDecInfo->vbMV[i].req_spec_region);
		}
	}

	if (pCodecInst->codecMode == VPX_DEC) {
		vpu_buffer_t *pvbSlice = &pDecInfo->vbSlice;
		if (pvbSlice->size == 0) {
			pvbSlice->size = VP8_MB_SAVE_SIZE;
			//ENABLE_TEE_DRM_FLOW
			if (pCodecInst->isUseProtectBuffer)
				pvbSlice->req_spec_region =
					VE_SECURE_PROTECTION;
			else
				pvbSlice->req_spec_region = 0;

			if (vdi_allocate_dma_memory(pCodecInst->coreIdx,
						    pvbSlice) < 0) {
				return RETCODE_INSUFFICIENT_RESOURCE;
			}
			VLOG(TRACE,
			     "[%d]%s.vdi_allocate_dma_memory vbSlice(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, pvbSlice->phys_addr,
			     pvbSlice->base, pvbSlice->virt_addr,
			     pvbSlice->size, pvbSlice->req_spec_region);
		}
	}

	if (pCodecInst->codecMode == AVC_DEC) {
		vpu_buffer_t *pvbSlice = &pDecInfo->vbSlice;
		if (pvbSlice->size == 0) {
			pvbSlice->size = SLICE_SAVE_SIZE;
			//ENABLE_TEE_DRM_FLOW
			if (pCodecInst->isUseProtectBuffer)
				pvbSlice->req_spec_region =
					VE_SECURE_PROTECTION;
			else
				pvbSlice->req_spec_region = 0;

			if (vdi_allocate_dma_memory(pCodecInst->coreIdx,
						    pvbSlice) < 0) {
				return RETCODE_INSUFFICIENT_RESOURCE;
			}
			VLOG(TRACE,
			     "[%d]%s.vdi_allocate_dma_memory vbSlice(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, pvbSlice->phys_addr,
			     pvbSlice->base, pvbSlice->virt_addr,
			     pvbSlice->size, pvbSlice->req_spec_region);
		}
	}
#endif // #ifdef DAH_222_PREALLOC_MV_SLICE_BUFFER

	// for debug, enable logging by vpuapi self
	//pCodecInst->loggingEnable = 1;

	LeaveLock(pCodecInst->coreIdx);

	VLOG(TRACE, "[-] [%d]%s.h:0x%x.ret:RETCODE_SUCCESS\n", __LINE__,
	     __func__, *pHandle);
	return RETCODE_SUCCESS;
}

RetCode VPU_DecClose(DecHandle handle)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;
	int i;
#if defined(ENABLE_TEE_DRM_FLOW)
	int ret_teeapi;
#endif

	VLOG(TRACE, "[+] [%d]%s.h:0x%x\n", __LINE__, __func__, handle);
	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__,
		     handle, ret);
		return ret;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	EnterLock(pCodecInst->coreIdx);

	if ((ret = ProductVpuDecFiniSeq(pCodecInst)) != RETCODE_SUCCESS) {
		if (pCodecInst->loggingEnable)
			vdi_log(pCodecInst->coreIdx, DEC_SEQ_END, 0);

		if (ret == RETCODE_VPU_STILL_RUNNING) {
			LeaveLock(pCodecInst->coreIdx);
			VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__,
			     __func__, handle, ret);
			return ret;
		}
	}

#if defined(ENABLE_TEE_DRM_FLOW)
	ret_teeapi =
		ta_TEEapi_deinit((struct tee_context *)pCodecInst->teeapi_ctx,
				 pCodecInst->teeapi_tee_session);
	if (ret_teeapi < 0) {
		LeaveLock(pCodecInst->coreIdx);
		VLOG(ERR, "[%d]%s.ta_TEEapi_deinit() fail.ret:%d\n", __LINE__,
		     __func__, ret_teeapi);
		return RETCODE_FAILURE;
	}
#endif

	if (pDecInfo->vbSlice.size) {
		VLOG(TRACE,
		     "[%d]%s.vdi_free_dma_memory_no_mmap vbSlice(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
		     __LINE__, __func__, pDecInfo->vbSlice.phys_addr,
		     pDecInfo->vbSlice.base, pDecInfo->vbSlice.virt_addr,
		     pDecInfo->vbSlice.size, pDecInfo->vbSlice.req_spec_region);
		vdi_free_dma_memory_no_mmap(pCodecInst->coreIdx,
					    &pDecInfo->vbSlice);
	}

	if (pDecInfo->vbWork.size) {
		if (pDecInfo->workBufferAllocExt == 0) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory_no_mmap vbWork(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, pDecInfo->vbWork.phys_addr,
			     pDecInfo->vbWork.base, pDecInfo->vbWork.virt_addr,
			     pDecInfo->vbWork.size,
			     pDecInfo->vbWork.req_spec_region);
			vdi_free_dma_memory_no_mmap(pCodecInst->coreIdx,
						    &pDecInfo->vbWork);
		} else {
			vdi_dettach_dma_memory(pCodecInst->coreIdx,
					       &pDecInfo->vbWork);
		}
	}

	if (pDecInfo->vbFrame.size) {
		if (pDecInfo->frameAllocExt == 0) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory vbFrame(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, pDecInfo->vbFrame.phys_addr,
			     pDecInfo->vbFrame.base,
			     pDecInfo->vbFrame.virt_addr,
			     pDecInfo->vbFrame.size,
			     pDecInfo->vbFrame.req_spec_region);
			vdi_free_dma_memory(pCodecInst->coreIdx,
					    &pDecInfo->vbFrame);
		}
	}
	for (i = 0; i < MAX_REG_FRAME; i++) {
		if (pDecInfo->vbMV[i].size) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory_no_mmap vbMV[%d](0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, i, pDecInfo->vbMV[i].phys_addr,
			     pDecInfo->vbMV[i].base,
			     pDecInfo->vbMV[i].virt_addr,
			     pDecInfo->vbMV[i].size,
			     pDecInfo->vbMV[i].req_spec_region);
			vdi_free_dma_memory_no_mmap(pCodecInst->coreIdx,
						    &pDecInfo->vbMV[i]);
		}
		if (pDecInfo->vbFbcYTbl[i].size) {
			if (pDecInfo->fbcTblAllocExt == 0) {
				VLOG(TRACE,
				     "[%d]%s.vdi_free_dma_memory vbFbcYTbl[%d](0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
				     __LINE__, __func__, i,
				     pDecInfo->vbFbcYTbl[i].phys_addr,
				     pDecInfo->vbFbcYTbl[i].base,
				     pDecInfo->vbFbcYTbl[i].virt_addr,
				     pDecInfo->vbFbcYTbl[i].size,
				     pDecInfo->vbFbcYTbl[i].req_spec_region);
				vdi_free_dma_memory(pCodecInst->coreIdx,
						    &pDecInfo->vbFbcYTbl[i]);
			}
		}
		if (pDecInfo->vbFbcCTbl[i].size) {
			if (pDecInfo->fbcTblAllocExt == 0) {
				VLOG(TRACE,
				     "[%d]%s.vdi_free_dma_memory vbFbcCTbl[%d](0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
				     __LINE__, __func__, i,
				     pDecInfo->vbFbcCTbl[i].phys_addr,
				     pDecInfo->vbFbcCTbl[i].base,
				     pDecInfo->vbFbcCTbl[i].virt_addr,
				     pDecInfo->vbFbcCTbl[i].size,
				     pDecInfo->vbFbcCTbl[i].req_spec_region);
				vdi_free_dma_memory(pCodecInst->coreIdx,
						    &pDecInfo->vbFbcCTbl[i]);
			}
		}
	}

	if (pDecInfo->vbTemp.size)
		vdi_dettach_dma_memory(pCodecInst->coreIdx, &pDecInfo->vbTemp);

	if (pDecInfo->vbPPU.size) {
		if (pDecInfo->ppuAllocExt == 0) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory vbPPU(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, pDecInfo->vbPPU.phys_addr,
			     pDecInfo->vbPPU.base, pDecInfo->vbPPU.virt_addr,
			     pDecInfo->vbPPU.size,
			     pDecInfo->vbPPU.req_spec_region);
			vdi_free_dma_memory(pCodecInst->coreIdx,
					    &pDecInfo->vbPPU);
		}
	}

	if (pDecInfo->vbWTL.size) {
		VLOG(TRACE,
		     "[%d]%s.vdi_free_dma_memory vbWTL(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
		     __LINE__, __func__, pDecInfo->vbWTL.phys_addr,
		     pDecInfo->vbWTL.base, pDecInfo->vbWTL.virt_addr,
		     pDecInfo->vbWTL.size, pDecInfo->vbWTL.req_spec_region);
		vdi_free_dma_memory(pCodecInst->coreIdx, &pDecInfo->vbWTL);
	}

	if (pDecInfo->vbUserData.size)
		vdi_dettach_dma_memory(pCodecInst->coreIdx,
				       &pDecInfo->vbUserData);

	if (pDecInfo->vbReport.size) {
		VLOG(TRACE,
		     "[%d]%s.vdi_free_dma_memory vbReport(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
		     __LINE__, __func__, pDecInfo->vbReport.phys_addr,
		     pDecInfo->vbReport.base, pDecInfo->vbReport.virt_addr,
		     pDecInfo->vbReport.size,
		     pDecInfo->vbReport.req_spec_region);
		vdi_free_dma_memory(pCodecInst->coreIdx, &pDecInfo->vbReport);
	}

	if (GetPendingInst(pCodecInst->coreIdx) == pCodecInst)
		ClearPendingInst(pCodecInst->coreIdx);

#ifdef VE1_CHECKSUM
	if (pDecInfo->hashTable.size) {
		Uint8 *result = (Uint8 *)pDecInfo->hashTable.virt_addr;
		Uint32 sum = 0;
		int i;
		for (i = 0; i < HASH_SIZE; i++)
			sum += result[i];
		if (sum > 0)
			VLOG(TRACE,
			     "[RTKCKS]= Fianl Hash = %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x",
			     result[0], result[1], result[2], result[3],
			     result[4], result[5], result[6], result[7],
			     result[8], result[9], result[10], result[11],
			     result[12], result[13], result[14], result[15],
			     result[16], result[17], result[18], result[19],
			     result[20], result[21], result[22], result[23],
			     result[24], result[25], result[26], result[27],
			     result[28], result[29], result[30], result[31]);
		VLOG(TRACE,
		     "[%d]%s.vdi_free_dma_memory hashTable(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
		     __LINE__, __func__, pDecInfo->hashTable.phys_addr,
		     pDecInfo->hashTable.base, pDecInfo->hashTable.virt_addr,
		     pDecInfo->hashTable.size,
		     pDecInfo->hashTable.req_spec_region);
		vdi_free_dma_memory(pCodecInst->coreIdx, &pDecInfo->hashTable);
	}
#endif

	LeaveLock(pCodecInst->coreIdx);

	FreeCodecInstance(pCodecInst);

	VLOG(TRACE, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__, handle,
	     ret);
	return ret;
}

RetCode VPU_DecSetEscSeqInit(DecHandle handle, int escape)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	if (pDecInfo->openParam.bitstreamMode != BS_MODE_INTERRUPT)
		return RETCODE_INVALID_PARAM;

	pDecInfo->seqInitEscape = escape;

	return RETCODE_SUCCESS;
}

RetCode VPU_DecGetInitialInfo(DecHandle handle, DecInitialInfo *info)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;
	Int32 flags;
	Uint32 interruptBit;
	VpuAttr *pAttr;

	/* CODA9xx */
	interruptBit = INT_BIT_SEQ_INIT;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (info == NULL)
		return RETCODE_INVALID_PARAM;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	ret = ProductVpuDecCheckCapability(pCodecInst);
	if (ret != RETCODE_SUCCESS)
		return ret;

	EnterLock(pCodecInst->coreIdx);

	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];

	if (GetPendingInst(pCodecInst->coreIdx)) {
		/* The other instance is running */
		if (VPU_GetOpenInstanceNum(pCodecInst->coreIdx) > 1) //RTK
		{
			VLOG(WARN, "In[%s][%d] usleep 50ms and try again\n",
			     __func__, __LINE__);
			msleep(50);
			if (GetPendingInst(pCodecInst->coreIdx)) {
				LeaveLock(pCodecInst->coreIdx);
				return RETCODE_FRAME_NOT_COMPLETE;
			}
		} else {
			LeaveLock(pCodecInst->coreIdx);
			return RETCODE_FRAME_NOT_COMPLETE;
		}
	}

	if (DecBitstreamBufEmpty(pDecInfo)) {
		LeaveLock(pCodecInst->coreIdx);
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	ret = ProductVpuDecInitSeq(handle);
	if (ret != RETCODE_SUCCESS) {
		LeaveLock(pCodecInst->coreIdx);
		return ret;
	}

	if (pAttr->supportCommandQueue == TRUE) {
		LeaveLock(pCodecInst->coreIdx);
	}

	flags = ProductVpuWaitInterrupt(pCodecInst, __VPU_BUSY_TIMEOUT);

	if (pAttr->supportCommandQueue == TRUE) {
		EnterLock(pCodecInst->coreIdx);
	}

	if (flags == -1) {
		info->rdPtr = VpuReadReg(pCodecInst->coreIdx,
					 pDecInfo->streamRdPtrRegAddr);
		info->wrPtr = VpuReadReg(pCodecInst->coreIdx,
					 pDecInfo->streamWrPtrRegAddr);
		ret = RETCODE_VPU_RESPONSE_TIMEOUT;
	} else {
		if (flags & (1 << interruptBit))
			ProductVpuClearInterrupt(pCodecInst->coreIdx,
						 (1 << interruptBit));

		if (flags != (1 << interruptBit))
			ret = RETCODE_FAILURE;
		else
			ret = ProductVpuDecGetSeqInfo(handle, info);
	}

	info->rdPtr =
		VpuReadReg(pCodecInst->coreIdx, pDecInfo->streamRdPtrRegAddr);
	info->wrPtr =
		VpuReadReg(pCodecInst->coreIdx, pDecInfo->streamWrPtrRegAddr);

	pDecInfo->initialInfo = *info;
	if (ret == RETCODE_SUCCESS) {
		pDecInfo->initialInfoObtained = 1;
	}

	SetPendingInst(pCodecInst->coreIdx, 0);

	LeaveLock(pCodecInst->coreIdx);

	return ret;
}

RetCode VPU_DecIssueSeqInit(DecHandle handle)
{
	CodecInst *pCodecInst;
	RetCode ret;
	VpuAttr *pAttr;

	VLOG(TRACE, "[+] [%d]%s.h:0x%x\n", __LINE__, __func__, handle);
	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__,
		     handle, ret);
		return ret;
	}

	pCodecInst = handle;

	EnterLock(pCodecInst->coreIdx);

	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];

	if (GetPendingInst(pCodecInst->coreIdx)) {
		if (VPU_GetOpenInstanceNum(pCodecInst->coreIdx) > 1) //RTK
		{
			VLOG(WARN, "In[%s][%d] usleep 50ms and try again\n",
			     __func__, __LINE__);
			msleep(50);
			if (GetPendingInst(pCodecInst->coreIdx)) {
				LeaveLock(pCodecInst->coreIdx);
				VLOG(ERR,
				     "[-] [%d]%s.h:0x%x.ret:RETCODE_FRAME_NOT_COMPLETE\n",
				     __LINE__, __func__, handle);
				return RETCODE_FRAME_NOT_COMPLETE;
			}
		} else {
			LeaveLock(pCodecInst->coreIdx);
			VLOG(ERR,
			     "[-] [%d]%s.h:0x%x.ret:RETCODE_FRAME_NOT_COMPLETE\n",
			     __LINE__, __func__, handle);
			return RETCODE_FRAME_NOT_COMPLETE;
		}
	}

	ret = ProductVpuDecInitSeq(handle);
	if (ret == RETCODE_SUCCESS) {
		SetPendingInst(pCodecInst->coreIdx, pCodecInst);
	}

	if (pAttr->supportCommandQueue == TRUE) {
		SetPendingInst(pCodecInst->coreIdx, NULL);
		LeaveLock(pCodecInst->coreIdx);
	}

	VLOG(TRACE, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__, handle,
	     ret);
	return ret;
}

RetCode VPU_DecCompleteSeqInit(DecHandle handle, DecInitialInfo *info)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;
	VpuAttr *pAttr;

	VLOG(TRACE, "[+] [%d]%s.h:0x%x\n", __LINE__, __func__, handle);
	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__,
		     handle, ret);
		return ret;
	}

	if (info == 0) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:RETCODE_INVALID_PARAM\n",
		     __LINE__, __func__, handle);
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];

	if (pAttr->supportCommandQueue == TRUE) {
		EnterLock(pCodecInst->coreIdx);
	} else {
		if (pCodecInst != GetPendingInst(pCodecInst->coreIdx)) {
			SetPendingInst(pCodecInst->coreIdx, 0);
			LeaveLock(pCodecInst->coreIdx);
			VLOG(ERR,
			     "[-] [%d]%s.h:0x%x.ret:RETCODE_WRONG_CALL_SEQUENCE\n",
			     __LINE__, __func__, handle);
			return RETCODE_WRONG_CALL_SEQUENCE;
		}
	}

	ret = ProductVpuDecGetSeqInfo(handle, info);
	if (ret == RETCODE_SUCCESS) {
		pDecInfo->initialInfoObtained = 1;
	}

	info->rdPtr = ProductVpuDecGetRdPtr(pCodecInst);
	info->wrPtr =
		VpuReadReg(pCodecInst->coreIdx, pDecInfo->streamWrPtrRegAddr);
#ifdef FIX_SET_GET_RD_PTR_BUG
#else
	pDecInfo->prevFrameEndPos = info->rdPtr;
#endif
	pDecInfo->initialInfo = *info;

	VLOG(TRACE, "[picWidth                ]: %d\n", info->picWidth);
	VLOG(TRACE, "[picHeight               ]: %d\n", info->picHeight);
	VLOG(TRACE, "[fRateNumerator          ]: %d\n", info->fRateNumerator);
	VLOG(TRACE, "[fRateDenominator        ]: %d\n", info->fRateDenominator);
	VLOG(TRACE, "[picCropRect.left        ]: %d\n", info->picCropRect.left);
	VLOG(TRACE, "[picCropRect.top         ]: %d\n", info->picCropRect.top);
	VLOG(TRACE, "[picCropRect.right       ]: %d\n",
	     info->picCropRect.right);
	VLOG(TRACE, "[picCropRect.bottom      ]: %d\n",
	     info->picCropRect.bottom);
	VLOG(TRACE, "[mp4DataPartitionEnable  ]: %d\n",
	     info->mp4DataPartitionEnable);
	VLOG(TRACE, "[mp4ReversibleVlcEnable  ]: %d\n",
	     info->mp4ReversibleVlcEnable);
	VLOG(TRACE, "[mp4ShortVideoHeader     ]: %d\n",
	     info->mp4ShortVideoHeader);
	VLOG(TRACE, "[h263AnnexJEnable        ]: %d\n", info->h263AnnexJEnable);
	VLOG(TRACE, "[minFrameBufferCount     ]: %d\n",
	     info->minFrameBufferCount);
	VLOG(TRACE, "[frameBufDelay           ]: %d\n", info->frameBufDelay);
	VLOG(TRACE, "[normalSliceSize         ]: %d\n", info->normalSliceSize);
	VLOG(TRACE, "[worstSliceSize          ]: %d\n", info->worstSliceSize);
	VLOG(TRACE, "[maxSubLayers            ]: %d\n", info->maxSubLayers);
	VLOG(TRACE, "[profile                 ]: %d\n", info->profile);
	VLOG(TRACE, "[level                   ]: %d\n", info->level);
	VLOG(TRACE, "[tier                    ]: %d\n", info->tier);
	VLOG(TRACE, "[interlace               ]: %d\n", info->interlace);
	VLOG(TRACE, "[constraint_set_flag     ]: 0x%08x 0x%08x 0x%08x 0x%08x\n",
	     info->constraint_set_flag[0], info->constraint_set_flag[1],
	     info->constraint_set_flag[2], info->constraint_set_flag[3]);
	VLOG(TRACE, "[direct8x8Flag           ]: %d\n", info->direct8x8Flag);
	VLOG(TRACE, "[vc1Psf                  ]: %d\n", info->vc1Psf);
	VLOG(TRACE, "[isExtSAR                ]: %d\n", info->isExtSAR);
	VLOG(TRACE, "[maxNumRefFrmFlag        ]: %d\n", info->maxNumRefFrmFlag);
	VLOG(TRACE, "[maxNumRefFrm            ]: %d\n", info->maxNumRefFrm);
	VLOG(TRACE, "[aspectRateInfo          ]: %d\n", info->aspectRateInfo);
	VLOG(TRACE, "[bitRate                 ]: %d\n", info->bitRate);
	VLOG(TRACE, "[mp2LowDelay             ]: %d\n", info->mp2LowDelay);
	VLOG(TRACE, "[mp2DispVerSize          ]: %d\n", info->mp2DispVerSize);
	VLOG(TRACE, "[mp2DispHorSize          ]: %d\n", info->mp2DispHorSize);
	VLOG(TRACE, "[userDataNum             ]: %d\n", info->userDataNum);
	VLOG(TRACE, "[userDataSize            ]: %d\n", info->userDataSize);
	VLOG(TRACE, "[chromaFormatIDC         ]: %d\n", info->chromaFormatIDC);
	VLOG(TRACE, "[lumaBitdepth            ]: %d\n", info->lumaBitdepth);
	VLOG(TRACE, "[chromaBitdepth          ]: %d\n", info->chromaBitdepth);
	VLOG(TRACE, "[seqInitErrReason        ]: 0x%08x\n",
	     info->seqInitErrReason);
	VLOG(TRACE, "[warnInfo                ]: %d\n", info->warnInfo);
	VLOG(TRACE, "[rdPtr                   ]: 0x%08x\n", info->rdPtr);
	VLOG(TRACE, "[wrPtr                   ]: 0x%08x\n", info->wrPtr);
	VLOG(TRACE, "[sequenceNo              ]: %d\n", info->sequenceNo);

	SetPendingInst(pCodecInst->coreIdx, NULL);

	LeaveLock(pCodecInst->coreIdx);

	VLOG(TRACE, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__, handle,
	     ret);
	return ret;
}

static RetCode DecRegisterFrameBuffer(DecHandle handle, FrameBuffer *bufArray,
				      int numFbsForDecoding, int numFbsForWTL,
				      int stride, int height, int mapType)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	Int32 i;
	Uint32 size, totalAllocSize;
	RetCode ret;
	FrameBuffer *fb, nullFb;
	vpu_buffer_t *vb;
	FrameBufferFormat format = FORMAT_420;
	Int32 totalNumOfFbs;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (numFbsForDecoding > MAX_FRAMEBUFFER_COUNT ||
	    numFbsForWTL > MAX_FRAMEBUFFER_COUNT) {
		return RETCODE_INVALID_PARAM;
	}

	osal_memset(&nullFb, 0x00, sizeof(FrameBuffer));
	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	pDecInfo->numFbsForDecoding = numFbsForDecoding;
	pDecInfo->numFbsForWTL = numFbsForWTL;
	pDecInfo->numFrameBuffers = numFbsForDecoding + numFbsForWTL;
	pDecInfo->stride = stride;
	if (pCodecInst->codecMode == VPX_DEC ||
	    pCodecInst->codecMode == W_VP8_DEC)
		pDecInfo->frameBufferHeight = VPU_ALIGN64(height);
	else if (pCodecInst->codecMode == W_VP9_DEC)
		pDecInfo->frameBufferHeight = VPU_ALIGN64(height);
	else
		pDecInfo->frameBufferHeight = height;
	pDecInfo->mapType = mapType;
	pDecInfo->mapCfg.productId = pCodecInst->productId;

	ret = ProductVpuDecCheckCapability(pCodecInst);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (!pDecInfo->initialInfoObtained)
		return RETCODE_WRONG_CALL_SEQUENCE;

	if ((stride < pDecInfo->initialInfo.picWidth) || (stride % 8 != 0) ||
	    (height < pDecInfo->initialInfo.picHeight)) {
		return RETCODE_INVALID_STRIDE;
	}

	EnterLock(pCodecInst->coreIdx);
	if (GetPendingInst(pCodecInst->coreIdx)) {
		if (VPU_GetOpenInstanceNum(pCodecInst->coreIdx) > 1) //RTK
		{
			VLOG(WARN, "In[%s][%d] usleep 50ms and try again\n",
			     __func__, __LINE__);
			msleep(50);
			if (GetPendingInst(pCodecInst->coreIdx)) {
				LeaveLock(pCodecInst->coreIdx);
				return RETCODE_FRAME_NOT_COMPLETE;
			}
		} else {
			LeaveLock(pCodecInst->coreIdx);
			return RETCODE_FRAME_NOT_COMPLETE;
		}
	}

	/* clear frameBufPool */
	for (i = 0;
	     i < (int)(sizeof(pDecInfo->frameBufPool) / sizeof(FrameBuffer));
	     i++) {
		pDecInfo->frameBufPool[i] = nullFb;
	}

	/* LinearMap or TiledMap, compressed framebuffer inclusive. */
	if (pDecInfo->initialInfo.lumaBitdepth > 8 ||
	    pDecInfo->initialInfo.chromaBitdepth > 8)
		format = FORMAT_420_P10_16BIT_LSB;

	totalNumOfFbs = numFbsForDecoding + numFbsForWTL;
	VLOG(TRACE,
	     "[%d]%s.numFbsForDecoding:%d.numFbsForWTL:%d.totalNumOfFbs:%d(%d).\n",
	     __LINE__, __func__, pDecInfo->numFbsForDecoding,
	     pDecInfo->numFbsForWTL, totalNumOfFbs, pDecInfo->numFrameBuffers);
	if (bufArray) {
		for (i = 0; i < totalNumOfFbs; i++)
			pDecInfo->frameBufPool[i] = bufArray[i];
	} else {
		vb = &pDecInfo->vbFrame;
		fb = &pDecInfo->frameBufPool[0];
		ret = ProductVpuAllocateFramebuffer(
			(CodecInst *)handle, fb, (TiledMapType)mapType,
			numFbsForDecoding, stride, height, format,
			pDecInfo->openParam.cbcrInterleave,
			pDecInfo->openParam.nv21,
			pDecInfo->openParam.frameEndian, vb, 0, FB_TYPE_CODEC);
		if (ret != RETCODE_SUCCESS) {
			LeaveLock(pCodecInst->coreIdx);
			return ret;
		}
	}
	totalAllocSize = 0;
	if (pCodecInst->productId != PRODUCT_ID_960) {
		pDecInfo->mapCfg.tiledBaseAddr = pDecInfo->frameBufPool[0].bufY;
	}

	if (numFbsForDecoding == 1) {
		size = ProductCalculateFrameBufSize(
			handle->productId, stride, height,
			(TiledMapType)mapType, format,
			pDecInfo->openParam.cbcrInterleave, &pDecInfo->dramCfg);
	} else {
		size = pDecInfo->frameBufPool[1].bufY -
		       pDecInfo->frameBufPool[0].bufY;
	}
	size *= numFbsForDecoding;
	totalAllocSize += size;

	/* LinearMap */
	if (pDecInfo->wtlEnable == TRUE || pDecInfo->enableAfbce == TRUE ||
	    numFbsForWTL != 0) {
		pDecInfo->stride = stride;
		if (bufArray) {
			format = pDecInfo->frameBufPool[0].format;
		} else {
			TiledMapType map;
			map = pDecInfo->enableAfbce == TRUE ?
				      ARM_COMPRESSED_FRAME_MAP :
				      ((pDecInfo->wtlMode == FF_FRAME ?
						LINEAR_FRAME_MAP :
						LINEAR_FIELD_MAP));
			format = pDecInfo->wtlFormat;
			vb = &pDecInfo->vbWTL;
			fb = &pDecInfo->frameBufPool[numFbsForDecoding];

			ret = ProductVpuAllocateFramebuffer(
				(CodecInst *)handle, fb, map, numFbsForWTL,
				stride, height, pDecInfo->wtlFormat,
				pDecInfo->openParam.cbcrInterleave,
				pDecInfo->openParam.nv21,
				pDecInfo->openParam.frameEndian, vb, 0,
				FB_TYPE_PPU);

			if (ret != RETCODE_SUCCESS) {
				LeaveLock(pCodecInst->coreIdx);
				return ret;
			}
		}
		if (numFbsForWTL == 1) {
			size = ProductCalculateFrameBufSize(
				handle->productId, stride, height,
				(TiledMapType)mapType, format,
				pDecInfo->openParam.cbcrInterleave,
				&pDecInfo->dramCfg);
		} else {
			size = pDecInfo->frameBufPool[numFbsForDecoding + 1]
				       .bufY -
			       pDecInfo->frameBufPool[numFbsForDecoding].bufY;
		}
		size *= numFbsForWTL;
		totalAllocSize += size;
	}

	ret = ProductVpuRegisterFramebuffer(pCodecInst);

	LeaveLock(pCodecInst->coreIdx);

#ifdef ENABLE_CODA9_WRITE_PROTECT
	{
		PhysicalAddress startAddr = 0xffffffff;
		PhysicalAddress endAddr = 0;
		Int32 maxSize = 0;

		startAddr = 0xffffffff;
		endAddr = 0;
		for (i = 0; i < totalNumOfFbs; i++) {
			startAddr =
				(startAddr > pDecInfo->frameBufPool[i].bufY) ?
					pDecInfo->frameBufPool[i].bufY :
					startAddr;
			endAddr = (endAddr < pDecInfo->frameBufPool[i].bufY) ?
					  pDecInfo->frameBufPool[i].bufY :
					  endAddr;
			maxSize = (maxSize < pDecInfo->frameBufPool[i].size) ?
					  pDecInfo->frameBufPool[i].size :
					  maxSize;
		}
		endAddr += maxSize;
		VLOG(INFO,
		     "[%d]%s.registered frameBuf startAddr:0x%08x.endAddr:0x%08x.size:%d\n",
		     __LINE__, __func__, startAddr, endAddr, maxSize);

		if (pDecInfo->vbMV[0].phys_addr > 0) {
			for (i = 0; i < numFbsForDecoding; i++) {
				PhysicalAddress mvColEndAddr =
					pDecInfo->vbMV[i].phys_addr +
					pDecInfo->vbMV[i].size;
				VLOG(INFO,
				     "[%d]%s.vbMV[%d](0x%08x,0x%08x).startAddr:0x%08x.endAddr:0x%08x\n",
				     __LINE__, __func__, i,
				     pDecInfo->vbMV[i].phys_addr, mvColEndAddr,
				     startAddr, endAddr);
				startAddr = (startAddr <
					     pDecInfo->vbMV[i].phys_addr) ?
						    startAddr :
						    pDecInfo->vbMV[i].phys_addr;
				endAddr = (endAddr < mvColEndAddr) ?
						  mvColEndAddr :
						  endAddr;
				VLOG(INFO,
				     "[%d]%s.new startAddr:0x%08x.endAddr:0x%08x\n",
				     __LINE__, __func__, startAddr, endAddr);
			}
		}

		if (pDecInfo->secAxiInfo.bufSize) {
			pDecInfo->writeMemProtectCfg
				.decRegion[WPROT_DEC_SEC_AXI]
				.enable = TRUE;
			pDecInfo->writeMemProtectCfg
				.decRegion[WPROT_DEC_SEC_AXI]
				.isSecondary = TRUE;
			pDecInfo->writeMemProtectCfg
				.decRegion[WPROT_DEC_SEC_AXI]
				.startAddress = pDecInfo->secAxiInfo.bufBase;
			pDecInfo->writeMemProtectCfg
				.decRegion[WPROT_DEC_SEC_AXI]
				.endAddress = pDecInfo->secAxiInfo.bufBase +
					      pDecInfo->secAxiInfo.bufSize;
			VLOG(INFO,
			     "[%d]%s.set decRegion[WPROT_DEC_SEC_AXI](%d,%d,0x%08x,0x%08x)\n",
			     __LINE__, __func__,
			     pDecInfo->writeMemProtectCfg
				     .decRegion[WPROT_DEC_SEC_AXI]
				     .enable,
			     pDecInfo->writeMemProtectCfg
				     .decRegion[WPROT_DEC_SEC_AXI]
				     .isSecondary,
			     pDecInfo->writeMemProtectCfg
				     .decRegion[WPROT_DEC_SEC_AXI]
				     .startAddress,
			     pDecInfo->writeMemProtectCfg
				     .decRegion[WPROT_DEC_SEC_AXI]
				     .endAddress);
		}
		pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME].enable =
			TRUE;
		pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			.isSecondary = FALSE;
		pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			.startAddress = startAddr;
		pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			.endAddress = endAddr;
		VLOG(INFO,
		     "[%d]%s.set decRegion[WPROT_DEC_FRAME](%d,%d,0x%08x,0x%08x)\n",
		     __LINE__, __func__,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			     .enable,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			     .isSecondary,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			     .startAddress,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			     .endAddress);
	}
#endif

	return ret;
}

RetCode VPU_DecRegisterFrameBuffer(DecHandle handle, FrameBuffer *bufArray,
				   int num, int stride, int height, int mapType)
{
	DecInfo *pDecInfo = &handle->CodecInfo->decInfo;
	Uint32 numWTL = 0;
	RetCode ret;
	int i;

	VLOG(TRACE, "[+] [%d]%s.h:0x%x.num:%d.stride:%d.height:%d.mapType:%d\n",
	     __LINE__, __func__, handle, num, stride, height, mapType);
	if (num) {
		for (i = 0; i < num; i++) {
			VLOG(TRACE,
			     "[%d]%s.h:0x%x.size:%d.bufY:0x%x.0x%x.0x%x.updateFbInfo:%d\n",
			     __LINE__, __func__, handle, bufArray[i].size,
			     bufArray[i].bufY, bufArray[i].bufCb,
			     bufArray[i].bufCr, bufArray[i].updateFbInfo);
		}
	}
	if (pDecInfo->wtlEnable == TRUE)
		numWTL = num;
	ret = DecRegisterFrameBuffer(handle, bufArray, num, numWTL, stride,
				     height, mapType);
	VLOG(TRACE, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__, handle,
	     ret);
	return ret;
}

RetCode VPU_DecRegisterFrameBufferEx(DecHandle handle, FrameBuffer *bufArray,
				     int numOfDecFbs, int numOfDisplayFbs,
				     int stride, int height, int mapType)
{
	return DecRegisterFrameBuffer(handle, bufArray, numOfDecFbs,
				      numOfDisplayFbs, stride, height, mapType);
}

RetCode VPU_DecGetFrameBuffer(DecHandle handle, int frameIdx,
			      FrameBuffer *frameBuf)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (frameBuf == 0)
		return RETCODE_INVALID_PARAM;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	if (frameIdx < 0 || frameIdx >= pDecInfo->numFrameBuffers)
		return RETCODE_INVALID_PARAM;

	*frameBuf = pDecInfo->frameBufPool[frameIdx];

	return RETCODE_SUCCESS;
}

RetCode VPU_DecUpdateFrameBuffer(DecHandle handle, FrameBuffer *fbcFb,
				 FrameBuffer *linearFb, Int32 mvColIndex,
				 Int32 picWidth, Int32 picHeight)
{
	if (handle == NULL) {
		return RETCODE_INVALID_HANDLE;
	}

	return ProductVpuDecUpdateFrameBuffer((CodecInst *)handle, fbcFb,
					      linearFb, mvColIndex, picWidth,
					      picHeight);
}

RetCode VPU_DecSetBitstreamBuffer(DecHandle handle, PhysicalAddress rdPtr,
				  PhysicalAddress wrPtr)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	SetClockGate(pCodecInst->coreIdx, 1);

	if (GetPendingInst(pCodecInst->coreIdx) == pCodecInst)
		VpuWriteReg(pCodecInst->coreIdx, pDecInfo->streamRdPtrRegAddr,
			    rdPtr);
	else
		pDecInfo->streamRdPtr = rdPtr;

	SetClockGate(pCodecInst->coreIdx, 0);

	pDecInfo->streamWrPtr = wrPtr;

	return RETCODE_SUCCESS;
}

RetCode VPU_DecSetDispFlag(DecHandle handle, int dispFlag) // [r] unused
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;
	VpuAttr *pAttr;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];
	if (pAttr->supportCommandQueue == FALSE) {
		EnterDispFlagLock(pCodecInst->coreIdx);
		pDecInfo->frameDisplayFlag = dispFlag;
		pDecInfo->clearDisplayIndexes = 0;
		LeaveDispFlagLock(pCodecInst->coreIdx);
	} else {
		EnterLock(pCodecInst->coreIdx);
		LeaveLock(pCodecInst->coreIdx);
	}

	return RETCODE_SUCCESS;
}

RetCode VPU_DecGetBitstreamBuffer(DecHandle handle, PhysicalAddress *prdPtr,
				  PhysicalAddress *pwrPtr, Uint32 *size)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	PhysicalAddress rdPtr;
	PhysicalAddress wrPtr;
	PhysicalAddress tempPtr;
	int room;
	Int32 coreIdx;
	VpuAttr *pAttr;

	coreIdx = handle->coreIdx;
	pAttr = &g_VpuCoreAttributes[coreIdx];
	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	SetClockGate(coreIdx, TRUE);

	if (pAttr->supportCommandQueue == TRUE) {
#ifdef FIX_SET_GET_RD_PTR_BUG
		EnterLock(pCodecInst->coreIdx);
		rdPtr = ProductVpuDecGetRdPtr(pCodecInst);
		LeaveLock(pCodecInst->coreIdx);
#else
		if (pDecInfo->rdPtrValidFlag ==
		    TRUE) { // when RdPtr has been updated by calling SetRdPtr.
			rdPtr = pDecInfo->streamRdPtr;
		} else {
			EnterLock(pCodecInst->coreIdx);
			rdPtr = ProductVpuDecGetRdPtr(pCodecInst);
			LeaveLock(pCodecInst->coreIdx);
		}
#endif
	} else {
		if (GetPendingInst(coreIdx) == pCodecInst) {
			if (pCodecInst->codecMode == AVC_DEC &&
			    pCodecInst->codecModeAux == AVC_AUX_MVC) {
				rdPtr = pDecInfo->streamRdPtr;
			} else {
				rdPtr = VpuReadReg(
					coreIdx, pDecInfo->streamRdPtrRegAddr);
			}
		} else {
			rdPtr = pDecInfo->streamRdPtr;
		}
	}

	SetClockGate(coreIdx, FALSE);

	wrPtr = pDecInfo->streamWrPtr;

	pAttr = &g_VpuCoreAttributes[coreIdx];

	tempPtr = rdPtr;

	if (pDecInfo->openParam.bitstreamMode != BS_MODE_PIC_END) {
		if (wrPtr < tempPtr) {
			room = tempPtr - wrPtr -
			       pAttr->bitstreamBufferMargin * 2;
		} else {
			room = (pDecInfo->streamBufEndAddr - wrPtr) +
			       (tempPtr - pDecInfo->streamBufStartAddr) -
			       pAttr->bitstreamBufferMargin * 2;
		}
		room--;
	} else {
		room = (pDecInfo->streamBufEndAddr - wrPtr);
	}

	if (prdPtr)
		*prdPtr = tempPtr;
	if (pwrPtr)
		*pwrPtr = wrPtr;
	if (size)
		*size = room;

	VLOG(TRACE, "[%d]%s.h:0x%x.rdPtr:0x%x.wrPtr:0x%x.room:%d\n", __LINE__,
	     __func__, handle, tempPtr, wrPtr, room);
	return RETCODE_SUCCESS;
}

// RTHA-133, for PIC_END + ring buffer
RetCode VPU_DecGetBitstreamBufferEx(DecHandle handle, PhysicalAddress *prdPtr,
				    PhysicalAddress *pwrPtr, Uint32 *size)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	PhysicalAddress rdPtr;
	PhysicalAddress wrPtr;
	PhysicalAddress tempPtr;
	int room;
	Int32 coreIdx;
	VpuAttr *pAttr;

	coreIdx = handle->coreIdx;
	pAttr = &g_VpuCoreAttributes[coreIdx];
	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	SetClockGate(coreIdx, TRUE);

	if (pAttr->supportCommandQueue == TRUE) {
#ifdef FIX_SET_GET_RD_PTR_BUG
		EnterLock(pCodecInst->coreIdx);
		rdPtr = ProductVpuDecGetRdPtr(pCodecInst);
		LeaveLock(pCodecInst->coreIdx);
#else
		if (pDecInfo->rdPtrValidFlag ==
		    TRUE) { // when RdPtr has been updated by calling SetRdPtr.
			rdPtr = pDecInfo->streamRdPtr;
		} else {
			EnterLock(pCodecInst->coreIdx);
			rdPtr = ProductVpuDecGetRdPtr(pCodecInst);
			LeaveLock(pCodecInst->coreIdx);
		}
#endif
	} else {
		if (GetPendingInst(coreIdx) == pCodecInst) {
			if (pCodecInst->codecMode == AVC_DEC &&
			    pCodecInst->codecModeAux == AVC_AUX_MVC) {
				rdPtr = pDecInfo->streamRdPtr;
			} else {
				rdPtr = VpuReadReg(
					coreIdx, pDecInfo->streamRdPtrRegAddr);
			}
		} else {
			rdPtr = pDecInfo->streamRdPtr;
		}
	}

	SetClockGate(coreIdx, FALSE);

	wrPtr = pDecInfo->streamWrPtr;

	pAttr = &g_VpuCoreAttributes[coreIdx];

	tempPtr = rdPtr;

	if (wrPtr < tempPtr) {
		room = tempPtr - wrPtr - pAttr->bitstreamBufferMargin * 2;
	} else {
		room = (pDecInfo->streamBufEndAddr - wrPtr) +
		       (tempPtr - pDecInfo->streamBufStartAddr) -
		       pAttr->bitstreamBufferMargin * 2;
	}
	room--;

	if (prdPtr)
		*prdPtr = tempPtr;
	if (pwrPtr)
		*pwrPtr = wrPtr;
	if (size)
		*size = room;

	return RETCODE_SUCCESS;
}

RetCode VPU_DecUpdateBitstreamBuffer(DecHandle handle, int size)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	PhysicalAddress wrPtr;
	PhysicalAddress rdPtr;
	RetCode ret;
	BOOL running;
	VpuAttr *pAttr;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.size:%d.ret:%d\n", __LINE__,
		     __func__, handle, size, ret);
		return ret;
	}

	pCodecInst = handle;
	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	wrPtr = pDecInfo->streamWrPtr;

	SetClockGate(pCodecInst->coreIdx, 1);

	if (pAttr->supportCommandQueue == TRUE) {
		running = FALSE;
	} else {
		running = (BOOL)(GetPendingInst(pCodecInst->coreIdx) ==
				 pCodecInst);
	}

	if (size > 0) {
		Uint32 room = 0;

		if (running == TRUE)
			rdPtr = VpuReadReg(pCodecInst->coreIdx,
					   pDecInfo->streamRdPtrRegAddr);
		else
			rdPtr = pDecInfo->streamRdPtr;

		if (wrPtr < rdPtr) {
			if (rdPtr <= wrPtr + size) {
				SetClockGate(pCodecInst->coreIdx, 0);
				VLOG(ERR,
				     "[-] [%d]%s.h:0x%x.size:%d.ret:RETCODE_INVALID_PARAM\n",
				     __LINE__, __func__, handle, size);
				return RETCODE_INVALID_PARAM;
			}
		}

		wrPtr += size;

		// Discuss with FuChun, vpuapi can't know BS buffer is ring buffer or line buffer.
		// Gregory use "bitstreamMode != BS_MODE_PIC_END" to determine BS buffer is ring buffer is not correct due to we have PIC_END + ring buffer case.
		// If line buffer, wrPts won't be bigger than pDecInfo->streamBufEndAddr, so mark this condition is also ok for line buffer.
		if (wrPtr > pDecInfo->streamBufEndAddr) {
			room = wrPtr - pDecInfo->streamBufEndAddr;
			wrPtr = pDecInfo->streamBufStartAddr;
			wrPtr += room;
		} else if (wrPtr == pDecInfo->streamBufEndAddr) {
			wrPtr = pDecInfo->streamBufStartAddr;
		}

		pDecInfo->streamWrPtr = wrPtr;
		pDecInfo->streamRdPtr = rdPtr;

		if (running == TRUE) {
			VpuWriteReg(pCodecInst->coreIdx,
				    pDecInfo->streamWrPtrRegAddr, wrPtr);
		}
	}

	ret = ProductVpuDecSetBitstreamFlag(pCodecInst, running, size);

	SetClockGate(pCodecInst->coreIdx, 0);
	VLOG(TRACE, "[%d]%s.h:0x%x.size:%d.ret:%d\n", __LINE__, __func__,
	     handle, size, ret);
	return ret;
}

RetCode VPU_HWReset(Uint32 coreIdx)
{
	if (vdi_hw_reset(coreIdx) < 0)
		return RETCODE_FAILURE;

	if (GetPendingInst(coreIdx)) {
		SetPendingInst(coreIdx, 0);
		LeaveLock(
			coreIdx); //if vpu is in a lock state. release the state;
	}
	return RETCODE_SUCCESS;
}

/**
* VPU_SWReset
* IN
*    forcedReset : 1 if there is no need to waiting for BUS transaction,
*                  0 for otherwise
* OUT
*    RetCode : RETCODE_FAILURE if failed to reset,
*              RETCODE_SUCCESS for otherwise
*/
RetCode VPU_SWReset(Uint32 coreIdx, SWResetMode resetMode, void *pendingInst)
{
	RetCode ret = RETCODE_SUCCESS;
	CodecInst *pCodecInst = (CodecInst *)pendingInst;

	VLOG(TRACE, "[+] [%d]%s.coreIdx:%d.resetMode:%d.pendingInst:%p\n",
	     __LINE__, __func__, coreIdx, resetMode, pendingInst);
	SetClockGate(coreIdx, 1);
	ret = ProductVpuReset(coreIdx, resetMode);

	if (ret != RETCODE_SUCCESS) //RTK
	{
		ret = VPU_HWReset(coreIdx);
	} else {
		if (pCodecInst) {
			SetPendingInst(pCodecInst->coreIdx, 0);
			LeaveLock(coreIdx);
			SetClockGate(coreIdx, 1);
			if (pCodecInst->loggingEnable) {
				vdi_log(pCodecInst->coreIdx,
					(pCodecInst->productId ==
						 PRODUCT_ID_960 ||
					 pCodecInst->productId ==
						 PRODUCT_ID_980) ?
						0x10 :
						0x10000,
					1);
			}
		}

		if (pCodecInst) {
			if (pCodecInst->loggingEnable) {
				vdi_log(pCodecInst->coreIdx,
					(pCodecInst->productId ==
						 PRODUCT_ID_960 ||
					 pCodecInst->productId ==
						 PRODUCT_ID_980) ?
						0x10 :
						0x10000,
					0);
			}
		}
	}

	SetClockGate(coreIdx, 0);

	VLOG(TRACE, "[-] [%d]%s.coreIdx:%d.ret:%d\n", __LINE__, __func__,
	     coreIdx, ret);
	return ret;
}

//---- VPU_SLEEP/WAKE
RetCode VPU_SleepWake(Uint32 coreIdx, int iSleepWake)
{
	SetClockGate(coreIdx, TRUE);
	SetClockGate(coreIdx, FALSE);

	return 0;
}

RetCode VPU_DecStartOneFrame(DecHandle handle, DecParam *param)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	Uint32 val = 0;
	RetCode ret = RETCODE_SUCCESS;
	VpuAttr *pAttr = NULL;

	VLOG(TRACE, "[+] [%d]%s.h:0x%x.DecParam(%d,%d,%d)\n", __LINE__,
	     __func__, handle, param->iframeSearchEnable, param->skipframeMode,
	     param->craAsBlaFlag);

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__,
		     handle, ret);
		return ret;
	}

	pCodecInst = (CodecInst *)handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	if (pDecInfo->stride ==
	    0) { // This means frame buffers have not been registered.
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:RETCODE_WRONG_CALL_SEQUENCE\n",
		     __LINE__, __func__, handle);
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];

	EnterLock(pCodecInst->coreIdx);
	if (GetPendingInst(pCodecInst->coreIdx)) {
		if (VPU_GetOpenInstanceNum(pCodecInst->coreIdx) > 1) //RTK
		{
			VLOG(WARN, "In[%s][%d] usleep 50ms and try again\n",
			     __func__, __LINE__);
			msleep(50);
			if (GetPendingInst(pCodecInst->coreIdx)) {
				LeaveLock(pCodecInst->coreIdx);
				VLOG(ERR,
				     "[-] [%d]%s.h:0x%x.ret:RETCODE_FRAME_NOT_COMPLETE\n",
				     __LINE__, __func__, handle);
				return RETCODE_FRAME_NOT_COMPLETE;
			}
		} else {
			LeaveLock(pCodecInst->coreIdx);
			VLOG(ERR,
			     "[-] [%d]%s.h:0x%x.ret:RETCODE_FRAME_NOT_COMPLETE\n",
			     __LINE__, __func__, handle);
			return RETCODE_FRAME_NOT_COMPLETE;
		}
	}

	if (pAttr->supportCommandQueue == FALSE) {
		EnterDispFlagLock(pCodecInst->coreIdx);
		val = pDecInfo->frameDisplayFlag;
		val |= pDecInfo->setDisplayIndexes;
		val &= ~(Uint32)(pDecInfo->clearDisplayIndexes);
		VpuWriteReg(pCodecInst->coreIdx,
			    pDecInfo->frameDisplayFlagRegAddr, val);
		pDecInfo->clearDisplayIndexes = 0;
		pDecInfo->setDisplayIndexes = 0;
		LeaveDispFlagLock(pCodecInst->coreIdx);
	}

	pDecInfo->frameStartPos = pDecInfo->streamRdPtr;

	ret = ProductVpuDecode(pCodecInst, param);

	if (pAttr->supportCommandQueue == TRUE) {
		SetPendingInst(pCodecInst->coreIdx, NULL);
		LeaveLock(pCodecInst->coreIdx);
	} else {
		SetPendingInst(pCodecInst->coreIdx, pCodecInst);
	}

	VLOG(TRACE, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__, handle,
	     ret);
	return ret;
}

RetCode VPU_DecGetOutputInfo(DecHandle handle, DecOutputInfo *info)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;
	VpuRect rectInfo;
	Uint32 val;
	Int32 decodedIndex;
	Int32 displayIndex;
	Uint32 maxDecIndex;
	VpuAttr *pAttr;
#ifdef VE1_CHECKSUM_LOG_TO_TMP
	char hash[128];
	FILE *hash_out_file;
#endif

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__,
		     handle, ret);
		return ret;
	}

	if (info == 0) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:RETCODE_INVALID_PARAM\n",
		     __LINE__, __func__, handle);
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];

	if (pAttr->supportCommandQueue == TRUE) {
		EnterLock(pCodecInst->coreIdx);
	} else {
		if (pCodecInst != GetPendingInst(pCodecInst->coreIdx)) {
			SetPendingInst(pCodecInst->coreIdx, 0);
			LeaveLock(pCodecInst->coreIdx);
			VLOG(ERR,
			     "[-] [%d]%s.h:0x%x.ret:RETCODE_WRONG_CALL_SEQUENCE\n",
			     __LINE__, __func__, handle);
			return RETCODE_WRONG_CALL_SEQUENCE;
		}
	}

	osal_memset((void *)info, 0x00, sizeof(DecOutputInfo));

	ret = ProductVpuDecGetResult(pCodecInst, info);
	if (ret != RETCODE_SUCCESS) {
		info->rdPtr = pDecInfo->streamRdPtr;
		info->wrPtr = pDecInfo->streamWrPtr;
		SetPendingInst(pCodecInst->coreIdx, 0);
		LeaveLock(pCodecInst->coreIdx);
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__,
		     handle, ret);
		return ret;
	}

	decodedIndex = info->indexFrameDecoded;

	if (pDecInfo->openParam.afbceEnable) {
		maxDecIndex =
			(pDecInfo->numFbsForDecoding > pDecInfo->numFbsForWTL) ?
				pDecInfo->numFbsForDecoding :
				pDecInfo->numFbsForWTL;
		if (0 <= decodedIndex && decodedIndex < (int)maxDecIndex) {
			val = pDecInfo->numFbsForDecoding; //fbOffset
			pDecInfo->frameBufPool[val + decodedIndex].lfEnable =
				info->lfEnable;
		}
	}

	// Calculate display frame region
	val = 0;
	if (decodedIndex >= 0 && decodedIndex < MAX_GDI_IDX) {
		//default value
		rectInfo.left = 0;
		rectInfo.right = info->decPicWidth;
		rectInfo.top = 0;
		rectInfo.bottom = info->decPicHeight;

		if (pCodecInst->codecMode == HEVC_DEC ||
		    pCodecInst->codecMode == AVC_DEC ||
		    pCodecInst->codecMode == W_AVC_DEC ||
		    pCodecInst->codecMode == AVS_DEC)
			rectInfo = pDecInfo->initialInfo.picCropRect;

		info->rcDecoded.left =
			pDecInfo->decOutInfo[decodedIndex].rcDecoded.left =
				rectInfo.left;
		info->rcDecoded.right =
			pDecInfo->decOutInfo[decodedIndex].rcDecoded.right =
				rectInfo.right;
		info->rcDecoded.top =
			pDecInfo->decOutInfo[decodedIndex].rcDecoded.top =
				rectInfo.top;
		info->rcDecoded.bottom =
			pDecInfo->decOutInfo[decodedIndex].rcDecoded.bottom =
				rectInfo.bottom;
	} else {
		info->rcDecoded.left = 0;
		info->rcDecoded.right = info->decPicWidth;
		info->rcDecoded.top = 0;
		info->rcDecoded.bottom = info->decPicHeight;
	}

	displayIndex = info->indexFrameDisplay;
	if (info->indexFrameDisplay >= 0 &&
	    info->indexFrameDisplay < MAX_GDI_IDX) {
		if (pCodecInst->codecMode == VC1_DEC ||
		    pCodecInst->codecMode ==
			    W_VC1_DEC) // vc1 rotates decoded frame buffer region. the other std rotated whole frame buffer region.
		{
			if (pDecInfo->rotationEnable &&
			    (pDecInfo->rotationAngle == 90 ||
			     pDecInfo->rotationAngle == 270)) {
				info->rcDisplay.left =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.top;
				info->rcDisplay.right =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.bottom;
				info->rcDisplay.top =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.left;
				info->rcDisplay.bottom =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.right;
			} else {
				info->rcDisplay.left =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.left;
				info->rcDisplay.right =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.right;
				info->rcDisplay.top =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.top;
				info->rcDisplay.bottom =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.bottom;
			}
		} else {
			if (pDecInfo->rotationEnable) {
				switch (pDecInfo->rotationAngle) {
				case 90:
					info->rcDisplay.left =
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.top;
					info->rcDisplay.right =
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.bottom;
					info->rcDisplay.top =
						info->decPicWidth -
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.right;
					info->rcDisplay.bottom =
						info->decPicWidth -
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.left;
					break;
				case 270:
					info->rcDisplay.left =
						info->decPicHeight -
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.bottom;
					info->rcDisplay.right =
						info->decPicHeight -
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.top;
					info->rcDisplay.top =
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.left;
					info->rcDisplay.bottom =
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.right;
					break;
				case 180:
					info->rcDisplay.left =
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.left;
					info->rcDisplay.right =
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.right;
					info->rcDisplay.top =
						info->decPicHeight -
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.bottom;
					info->rcDisplay.bottom =
						info->decPicHeight -
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.top;
					break;
				default:
					info->rcDisplay.left =
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.left;
					info->rcDisplay.right =
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.right;
					info->rcDisplay.top =
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.top;
					info->rcDisplay.bottom =
						pDecInfo->decOutInfo[displayIndex]
							.rcDecoded.bottom;
					break;
				}

			} else {
				info->rcDisplay.left =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.left;
				info->rcDisplay.right =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.right;
				info->rcDisplay.top =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.top;
				info->rcDisplay.bottom =
					pDecInfo->decOutInfo[displayIndex]
						.rcDecoded.bottom;
			}

			if (pDecInfo->mirrorEnable) {
				Uint32 temp;
				if (pDecInfo->mirrorDirection & MIRDIR_VER) {
					temp = info->rcDisplay.top;
					info->rcDisplay.top =
						info->decPicHeight -
						info->rcDisplay.bottom;
					info->rcDisplay.bottom =
						info->decPicHeight - temp;
				}
				if (pDecInfo->mirrorDirection & MIRDIR_HOR) {
					temp = info->rcDisplay.left;
					info->rcDisplay.left =
						info->decPicWidth -
						info->rcDisplay.right;
					info->rcDisplay.right =
						info->decPicWidth - temp;
				}
			}

			switch (pCodecInst->codecMode) {
			default:
				break;
			}
		}

		if (info->indexFrameDisplay == info->indexFrameDecoded) {
			info->dispPicWidth = info->decPicWidth;
			info->dispPicHeight = info->decPicHeight;
		} else {
			info->dispPicWidth =
				pDecInfo->decOutInfo[displayIndex].decPicWidth;
			info->dispPicHeight =
				pDecInfo->decOutInfo[displayIndex].decPicHeight;
		}

		if (pDecInfo->scalerEnable == TRUE) {
			if ((pDecInfo->scaleWidth != 0) &&
			    (pDecInfo->scaleHeight != 0)) {
				info->dispPicWidth = pDecInfo->scaleWidth;
				info->dispPicHeight = pDecInfo->scaleHeight;
				info->rcDisplay.right = pDecInfo->scaleWidth;
				info->rcDisplay.bottom = pDecInfo->scaleHeight;
			}
		}
	} else {
		info->rcDisplay.left = 0;
		info->rcDisplay.right = 0;
		info->rcDisplay.top = 0;
		info->rcDisplay.bottom = 0;

		if (pDecInfo->rotationEnable || pDecInfo->mirrorEnable ||
		    pDecInfo->tiled2LinearEnable || pDecInfo->deringEnable) {
			info->dispPicWidth = info->decPicWidth;
			info->dispPicHeight = info->decPicHeight;
		} else {
			info->dispPicWidth = 0;
			info->dispPicHeight = 0;
		}
	}

	if ((pCodecInst->codecMode == VC1_DEC ||
	     pCodecInst->codecMode == W_VC1_DEC) &&
	    info->indexFrameDisplay != -3) {
		if (pDecInfo->vc1BframeDisplayValid == 0) {
			if (info->picType == 2)
				info->indexFrameDisplay = -3;
			else
				pDecInfo->vc1BframeDisplayValid = 1;
		}
	}

	pDecInfo->streamRdPtr = ProductVpuDecGetRdPtr(pCodecInst);
	pDecInfo->frameDisplayFlag = VpuReadReg(
		pCodecInst->coreIdx, pDecInfo->frameDisplayFlagRegAddr);
	if (pCodecInst->codecMode == W_VP9_DEC) {
		pDecInfo->frameDisplayFlag &= 0xFFFF;
	}
	pDecInfo->frameEndPos = pDecInfo->streamRdPtr;

	if (pDecInfo->frameEndPos < pDecInfo->frameStartPos)
		info->consumedByte = pDecInfo->frameEndPos +
				     pDecInfo->streamBufSize -
				     pDecInfo->frameStartPos;
	else
		info->consumedByte =
			pDecInfo->frameEndPos - pDecInfo->frameStartPos;

	if (pDecInfo->deringEnable || pDecInfo->mirrorEnable ||
	    pDecInfo->rotationEnable || pDecInfo->tiled2LinearEnable) {
		info->dispFrame = pDecInfo->rotatorOutput;
		info->dispFrame.stride = pDecInfo->rotatorStride;
	} else {
		val = ((pDecInfo->openParam.wtlEnable == TRUE ||
			pDecInfo->openParam.afbceEnable) ?
			       pDecInfo->numFbsForDecoding :
			       0); //fbOffset
		maxDecIndex =
			(pDecInfo->numFbsForDecoding > pDecInfo->numFbsForWTL) ?
				pDecInfo->numFbsForDecoding :
				pDecInfo->numFbsForWTL;

		if (0 <= info->indexFrameDisplay &&
		    info->indexFrameDisplay < (int)maxDecIndex)
			info->dispFrame =
				pDecInfo->frameBufPool[val +
						       info->indexFrameDisplay];
	}

	info->rdPtr = pDecInfo->streamRdPtr;
	info->wrPtr = pDecInfo->streamWrPtr;
	info->frameDisplayFlag = pDecInfo->frameDisplayFlag;

	info->sequenceNo = pDecInfo->initialInfo.sequenceNo;
	if (decodedIndex >= 0 && decodedIndex < MAX_GDI_IDX) {
		pDecInfo->decOutInfo[decodedIndex] = *info;
	}

	if (displayIndex >= 0 && displayIndex < MAX_GDI_IDX) {
		info->numOfTotMBs = info->numOfTotMBs;
		info->numOfErrMBs = info->numOfErrMBs;
		info->numOfTotMBsInDisplay =
			pDecInfo->decOutInfo[displayIndex].numOfTotMBs;
		info->numOfErrMBsInDisplay =
			pDecInfo->decOutInfo[displayIndex].numOfErrMBs;
		info->dispFrame.sequenceNo = info->sequenceNo;
	} else {
		info->numOfTotMBsInDisplay = 0;
		info->numOfErrMBsInDisplay = 0;
	}

	if (info->sequenceChanged != 0) {
		if (!(pCodecInst->productId == PRODUCT_ID_960 ||
		      pCodecInst->productId == PRODUCT_ID_980)) {
			/* Update new sequence information */
			osal_memcpy((void *)&pDecInfo->initialInfo,
				    (void *)&pDecInfo->newSeqInfo,
				    sizeof(DecInitialInfo));
		}
		if ((info->sequenceChanged & SEQ_CHANGE_INTER_RES_CHANGE) !=
		    SEQ_CHANGE_INTER_RES_CHANGE) {
			pDecInfo->initialInfo.sequenceNo++;
		}
	}

	SetPendingInst(pCodecInst->coreIdx, 0);
#ifdef VE1_CHECKSUM
	if (info->indexFrameDisplay >= 0) {
		FrameBuffer fbTmp =
			pDecInfo->frameBufPool[info->indexFrameDisplay];
		int fbSize =
			VPU_GetFrameBufSize(pCodecInst->coreIdx, fbTmp.stride,
					    fbTmp.height, fbTmp.mapType,
					    fbTmp.format, fbTmp.cbcrInterleave,
					    &pDecInfo->dramCfg);
		MCP_SHA256_Hash(info->dispFrame.bufY, fbSize,
				pDecInfo->hashTable.phys_addr);
		Uint8 *result = (Uint8 *)pDecInfo->hashTable.virt_addr;
		VLOG(TRACE,
		     "addr 0x%x size %zu Cur Hash= %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x",
		     info->dispFrame.bufY, fbSize, result[0], result[1],
		     result[2], result[3], result[4], result[5], result[6],
		     result[7], result[8], result[9], result[10], result[11],
		     result[12], result[13], result[14], result[15], result[16],
		     result[17], result[18], result[19], result[20], result[21],
		     result[22], result[23], result[24], result[25], result[26],
		     result[27], result[28], result[29], result[30],
		     result[31]);
#ifdef VE1_CHECKSUM_LOG_TO_TMP
		memset(hash, 0, sizeof(hash));
		sprintf(hash,
			"%.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x "
			"%.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x "
			"%.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x "
			"%.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x\n",
			result[0], result[1], result[2], result[3], result[4],
			result[5], result[6], result[7], result[8], result[9],
			result[10], result[11], result[12], result[13],
			result[14], result[15], result[16], result[17],
			result[18], result[19], result[20], result[21],
			result[22], result[23], result[24], result[25],
			result[26], result[27], result[28], result[29],
			result[30], result[31]);
		VLOG(TRACE, "%s", hash);
		//open
		hash_out_file = fopen("/tmp/hash.log", "a+");
		if (hash_out_file) {
			//write
			fputs(hash, hash_out_file);
			//close
			fclose(hash_out_file);
		} else {
			VLOG(TRACE, "open /tmp/hash.log failed");
		}
#endif
	}
#endif

	LeaveLock(pCodecInst->coreIdx);

	if (pCodecInst->coreIdx == 0) {
		pDecInfo->outputinfoSN++;
		if ((pDecInfo->openParam.bitstreamMode == BS_MODE_ROLLBACK) &&
		    !(info->decodingSuccess & 0x10) &&
		    (info->indexFrameDecoded >= 0 ||
		     info->indexFrameDecoded == -2)) {
			pDecInfo->decodedFrmNum++;
		} else if (info->indexFrameDecoded >= 0 ||
			   info->indexFrameDecoded == -2) {
			pDecInfo->decodedFrmNum++;
		}
	}
	VLOG(TRACE,
	     "[%d]%s.h:0x%x.%d.%d.dec:%d.dis:%d.POC:%d(%d,%d).type:%d(%d).pos(0x%x 0x%x 0x%x).size:%d.suc:0x%x.err:%d.frmDisFlg:0x%x.warn:%d.nalRefIdc:%d.decFrameInfo:%d.sc:0x%x.sef:%d\n",
	     __LINE__, __func__, handle, pDecInfo->outputinfoSN,
	     pDecInfo->decodedFrmNum, info->indexFrameDecoded,
	     info->indexFrameDisplay, info->avcPocPic, info->avcPocTop,
	     info->avcPocBot, info->picType, info->picTypeFirst,
	     info->bytePosFrameStart, info->bytePosFrameEnd, info->rdPtr,
	     vpu_ring_valid_data(
		     pDecInfo->streamBufStartAddr,
		     pDecInfo->streamBufStartAddr + pDecInfo->streamBufSize,
		     info->bytePosFrameStart, info->bytePosFrameEnd),
	     info->decodingSuccess, info->numOfErrMBs, info->frameDisplayFlag,
	     info->warnInfo, info->nalRefIdc, info->decFrameInfo,
	     info->sequenceChanged, info->streamEndFlag);
	return RETCODE_SUCCESS;
}

RetCode VPU_DecFrameBufferFlush(DecHandle handle, DecOutputInfo *pRemainings,
				Uint32 *retNum)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	DecOutputInfo *pOut;
	RetCode ret;
	FramebufferIndex retIndex[MAX_GDI_IDX];
	Uint32 retRemainings = 0;
	Int32 i, index, val;
	VpuAttr *pAttr = NULL;

	VLOG(TRACE, "[+] [%d]%s.h:0x%x\n", __LINE__, __func__, handle);
	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__,
		     handle, ret);
		return ret;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	EnterLock(pCodecInst->coreIdx);

	if (GetPendingInst(pCodecInst->coreIdx)) {
		if (VPU_GetOpenInstanceNum(pCodecInst->coreIdx) > 1) //RTK
		{
			VLOG(WARN, "In[%s][%d] usleep 50ms and try again\n",
			     __func__, __LINE__);
			msleep(50);
			if (GetPendingInst(pCodecInst->coreIdx)) {
				LeaveLock(pCodecInst->coreIdx);
				VLOG(ERR,
				     "[-] [%d]%s.h:0x%x.ret:RETCODE_FRAME_NOT_COMPLETE\n",
				     __LINE__, __func__, handle);
				return RETCODE_FRAME_NOT_COMPLETE;
			}
		} else {
			LeaveLock(pCodecInst->coreIdx);
			VLOG(ERR,
			     "[-] [%d]%s.h:0x%x.ret:RETCODE_FRAME_NOT_COMPLETE\n",
			     __LINE__, __func__, handle);
			return RETCODE_FRAME_NOT_COMPLETE;
		}
	}

	osal_memset((void *)retIndex, 0xff, sizeof(retIndex));

	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];
	if (pAttr->supportCommandQueue == FALSE) {
		EnterDispFlagLock(pCodecInst->coreIdx);
		val = pDecInfo->frameDisplayFlag;
		val |= pDecInfo->setDisplayIndexes;
		val &= ~(Uint32)(pDecInfo->clearDisplayIndexes);
		VpuWriteReg(pCodecInst->coreIdx,
			    pDecInfo->frameDisplayFlagRegAddr, val);
		pDecInfo->clearDisplayIndexes = 0;
		pDecInfo->setDisplayIndexes = 0;
		LeaveDispFlagLock(pCodecInst->coreIdx);
	}

	if ((ret = ProductVpuDecFlush(pCodecInst, retIndex, MAX_GDI_IDX)) !=
	    RETCODE_SUCCESS) {
		LeaveLock(pCodecInst->coreIdx);
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__,
		     handle, ret);
		return ret;
	}

	if (pRemainings != NULL) {
		for (i = 0; i < MAX_GDI_IDX; i++) {
			index = (pDecInfo->wtlEnable == TRUE) ?
					retIndex[i].tiledIndex :
					retIndex[i].linearIndex;
			if (index < 0)
				continue;
			pRemainings[i] = pDecInfo->decOutInfo[index];
			pOut = &pRemainings[i];
			pOut->indexFrameDisplay = pOut->indexFrameDecoded;
			pOut->indexFrameDisplayForTiled =
				pOut->indexFrameDecodedForTiled;
			if (pDecInfo->wtlEnable == TRUE)
				pOut->dispFrame =
					pDecInfo->frameBufPool
						[pDecInfo->numFbsForDecoding +
						 retIndex[i].linearIndex];
			else
				pOut->dispFrame = pDecInfo->frameBufPool[index];

			pOut->dispFrame.sequenceNo = pOut->sequenceNo;
			pOut->dispPicWidth = pOut->decPicWidth;
			pOut->dispPicHeight = pOut->decPicHeight;

			if (pDecInfo->rotationEnable) {
				switch (pDecInfo->rotationAngle) {
				case 90:
					pOut->rcDisplay.left =
						pDecInfo->decOutInfo[index]
							.rcDecoded.top;
					pOut->rcDisplay.right =
						pDecInfo->decOutInfo[index]
							.rcDecoded.bottom;
					pOut->rcDisplay.top =
						pOut->decPicWidth -
						pDecInfo->decOutInfo[index]
							.rcDecoded.right;
					pOut->rcDisplay.bottom =
						pOut->decPicWidth -
						pDecInfo->decOutInfo[index]
							.rcDecoded.left;
					break;
				case 270:
					pOut->rcDisplay.left =
						pOut->decPicHeight -
						pDecInfo->decOutInfo[index]
							.rcDecoded.bottom;
					pOut->rcDisplay.right =
						pOut->decPicHeight -
						pDecInfo->decOutInfo[index]
							.rcDecoded.top;
					pOut->rcDisplay.top =
						pDecInfo->decOutInfo[index]
							.rcDecoded.left;
					pOut->rcDisplay.bottom =
						pDecInfo->decOutInfo[index]
							.rcDecoded.right;
					break;
				case 180:
					pOut->rcDisplay.left =
						pDecInfo->decOutInfo[index]
							.rcDecoded.left;
					pOut->rcDisplay.right =
						pDecInfo->decOutInfo[index]
							.rcDecoded.right;
					pOut->rcDisplay.top =
						pOut->decPicHeight -
						pDecInfo->decOutInfo[index]
							.rcDecoded.bottom;
					pOut->rcDisplay.bottom =
						pOut->decPicHeight -
						pDecInfo->decOutInfo[index]
							.rcDecoded.top;
					break;
				default:
					pOut->rcDisplay.left =
						pDecInfo->decOutInfo[index]
							.rcDecoded.left;
					pOut->rcDisplay.right =
						pDecInfo->decOutInfo[index]
							.rcDecoded.right;
					pOut->rcDisplay.top =
						pDecInfo->decOutInfo[index]
							.rcDecoded.top;
					pOut->rcDisplay.bottom =
						pDecInfo->decOutInfo[index]
							.rcDecoded.bottom;
					break;
				}
			} else {
				pOut->rcDisplay.left =
					pDecInfo->decOutInfo[index]
						.rcDecoded.left;
				pOut->rcDisplay.right =
					pDecInfo->decOutInfo[index]
						.rcDecoded.right;
				pOut->rcDisplay.top =
					pDecInfo->decOutInfo[index]
						.rcDecoded.top;
				pOut->rcDisplay.bottom =
					pDecInfo->decOutInfo[index]
						.rcDecoded.bottom;
			}
			retRemainings++;
		}
	}

	if (retNum)
		*retNum = retRemainings;

	if (pCodecInst->loggingEnable)
		vdi_log(pCodecInst->coreIdx, DEC_BUF_FLUSH, 0);

	LeaveLock(pCodecInst->coreIdx);

	VLOG(TRACE, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__, handle,
	     ret);
	return ret;
}

RetCode VPU_DecSetRdPtr(DecHandle handle, PhysicalAddress addr, int updateWrPtr)
{
	CodecInst *pCodecInst;
	CodecInst *pPendingInst;
	DecInfo *pDecInfo;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}
	pCodecInst = (CodecInst *)handle;
	ret = ProductVpuDecCheckCapability(pCodecInst);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	pPendingInst = GetPendingInst(pCodecInst->coreIdx);
	if (pCodecInst == pPendingInst) {
		VpuWriteReg(pCodecInst->coreIdx, pDecInfo->streamRdPtrRegAddr,
			    addr);
	} else {
		EnterLock(pCodecInst->coreIdx);
		VpuWriteReg(pCodecInst->coreIdx, pDecInfo->streamRdPtrRegAddr,
			    addr);
		LeaveLock(pCodecInst->coreIdx);
	}

	pDecInfo->streamRdPtr = addr;
	pDecInfo->prevFrameEndPos = addr;
	if (updateWrPtr == TRUE) {
		pDecInfo->streamWrPtr = addr;
	}
	pDecInfo->rdPtrValidFlag = 1;
	VLOG(TRACE, "[%d]%s.h:0x%x.addr:0x%x.updateWrPtr:%d\n", __LINE__,
	     __func__, handle, addr, updateWrPtr);
	return RETCODE_SUCCESS;
}

RetCode VPU_EncSetWrPtr(EncHandle handle, PhysicalAddress addr, int updateRdPtr)
{
	CodecInst *pCodecInst;
	CodecInst *pPendingInst;
	EncInfo *pEncInfo;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;
	pCodecInst = (CodecInst *)handle;

	if (pCodecInst->productId == PRODUCT_ID_960 ||
	    pCodecInst->productId == PRODUCT_ID_980) {
		return RETCODE_NOT_SUPPORTED_FEATURE;
	}

	pEncInfo = &handle->CodecInfo->encInfo;
	pPendingInst = GetPendingInst(pCodecInst->coreIdx);
	if (pCodecInst == pPendingInst) {
		VpuWriteReg(pCodecInst->coreIdx, pEncInfo->streamWrPtrRegAddr,
			    addr);
	} else {
		EnterLock(pCodecInst->coreIdx);
		VpuWriteReg(pCodecInst->coreIdx, pEncInfo->streamWrPtrRegAddr,
			    addr);
		LeaveLock(pCodecInst->coreIdx);
	}
	pEncInfo->streamWrPtr = addr;
	if (updateRdPtr)
		pEncInfo->streamRdPtr = addr;

	return RETCODE_SUCCESS;
}

RetCode VPU_DecClrDispFlag(DecHandle handle, int index)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret = RETCODE_SUCCESS;
	Int32 endIndex;
	VpuAttr *pAttr = NULL;
	BOOL supportCommandQueue;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];

	endIndex = (pDecInfo->openParam.wtlEnable == TRUE) ?
			   pDecInfo->numFbsForWTL :
			   pDecInfo->numFbsForDecoding;

	if ((index < 0) || (index > (endIndex - 1))) {
		return RETCODE_INVALID_PARAM;
	}

	supportCommandQueue = (pAttr->supportCommandQueue == TRUE);
	if (supportCommandQueue == TRUE) {
		EnterLock(pCodecInst->coreIdx);
		LeaveLock(pCodecInst->coreIdx);
	} else {
		EnterDispFlagLock(pCodecInst->coreIdx);
		pDecInfo->clearDisplayIndexes |= (1 << index);
		LeaveDispFlagLock(pCodecInst->coreIdx);
	}

	VLOG(TRACE, "[%d]%s.h:0x%x.index:%d\n", __LINE__, __func__, handle,
	     index);
	return ret;
}

RetCode VPU_DecGiveCommand(DecHandle handle, CodecCommand cmd, void *param)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;
	DecOpenParam *pop = NULL;

	VLOG(TRACE, "[%d]%s.h:0x%x.cmd:%d\n", __LINE__, __func__, handle, cmd);
	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	switch (cmd) {
	case ENABLE_ROTATION: {
		if (pDecInfo->rotatorStride == 0) {
			return RETCODE_ROTATOR_STRIDE_NOT_SET;
		}
		pDecInfo->rotationEnable = 1;
		break;
	}

	case DISABLE_ROTATION: {
		pDecInfo->rotationEnable = 0;
		break;
	}

	case ENABLE_MIRRORING: {
		if (pDecInfo->rotatorStride == 0) {
			return RETCODE_ROTATOR_STRIDE_NOT_SET;
		}
		pDecInfo->mirrorEnable = 1;
		break;
	}
	case DISABLE_MIRRORING: {
		pDecInfo->mirrorEnable = 0;
		break;
	}
	case SET_MIRROR_DIRECTION: {
		MirrorDirection mirDir;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		mirDir = *(MirrorDirection *)param;
		if (!(mirDir == MIRDIR_NONE) && !(mirDir == MIRDIR_HOR) &&
		    !(mirDir == MIRDIR_VER) && !(mirDir == MIRDIR_HOR_VER)) {
			return RETCODE_INVALID_PARAM;
		}
		pDecInfo->mirrorDirection = mirDir;

		break;
	}
	case SET_ROTATION_ANGLE: {
		int angle;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		angle = *(int *)param;
		if (angle != 0 && angle != 90 && angle != 180 && angle != 270) {
			return RETCODE_INVALID_PARAM;
		}
		if (pDecInfo->rotatorStride != 0) {
			if (angle == 90 || angle == 270) {
				if (pDecInfo->initialInfo.picHeight >
				    pDecInfo->rotatorStride) {
					return RETCODE_INVALID_PARAM;
				}
			} else {
				if (pDecInfo->initialInfo.picWidth >
				    pDecInfo->rotatorStride) {
					return RETCODE_INVALID_PARAM;
				}
			}
		}

		pDecInfo->rotationAngle = angle;
		break;
	}
	case SET_ROTATOR_OUTPUT: {
#ifdef ENABLE_CODA9_WRITE_PROTECT
		PhysicalAddress start, end, ppuAddr;
#endif
		FrameBuffer *frame;
		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		frame = (FrameBuffer *)param;

		pDecInfo->rotatorOutput = *frame;
		pDecInfo->rotatorOutputValid = 1;
#ifdef ENABLE_CODA9_WRITE_PROTECT
		start = pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
				.startAddress;
		end = pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			      .endAddress;
		ppuAddr = GetXY2AXIAddr(&pDecInfo->mapCfg, 0, 0, 0,
					pDecInfo->rotatorStride,
					&pDecInfo->rotatorOutput);
		start = (start < ppuAddr) ? start : ppuAddr;
		ppuAddr += VPU_GetFrameBufSize(
			pCodecInst->coreIdx, pDecInfo->rotatorStride,
			pDecInfo->rotatorOutput.height,
			pDecInfo->rotatorOutput.mapType, FORMAT_420,
			pDecInfo->openParam.cbcrInterleave, &pDecInfo->dramCfg);
		end = (end > ppuAddr) ? end : ppuAddr;

		pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			.startAddress = start;
		pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			.endAddress = end;
		VLOG(INFO,
		     "[%d]%s.set decRegion[WPROT_DEC_FRAME](%d,%d,0x%08x,0x%08x)\n",
		     __LINE__, __func__,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			     .enable,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			     .isSecondary,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			     .startAddress,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_FRAME]
			     .endAddress);
#endif
		break;
	}

	case SET_ROTATOR_STRIDE: {
		int stride;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		stride = *(int *)param;
		if (stride % 8 != 0 || stride == 0) {
			return RETCODE_INVALID_STRIDE;
		}

		if (pDecInfo->rotationAngle == 90 ||
		    pDecInfo->rotationAngle == 270) {
			if (pDecInfo->initialInfo.picHeight > stride) {
				return RETCODE_INVALID_STRIDE;
			}
		} else {
			if (pDecInfo->initialInfo.picWidth > stride) {
				return RETCODE_INVALID_STRIDE;
			}
		}

		pDecInfo->rotatorStride = stride;
		break;
	}
	case DEC_SET_SPS_RBSP: {
		if (pCodecInst->codecMode != AVC_DEC &&
		    pCodecInst->codecMode != W_AVC_DEC) {
			return RETCODE_INVALID_COMMAND;
		}
		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}

		return SetParaSet(handle, 0, (DecParamSet *)param);
	}

	case DEC_SET_PPS_RBSP: {
		if (pCodecInst->codecMode != AVC_DEC &&
		    pCodecInst->codecMode != W_AVC_DEC) {
			return RETCODE_INVALID_COMMAND;
		}
		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}

		return SetParaSet(handle, 1, (DecParamSet *)param);
	}
	case ENABLE_DERING: {
		if (pDecInfo->rotatorStride == 0) {
			return RETCODE_ROTATOR_STRIDE_NOT_SET;
		}
		pDecInfo->deringEnable = 1;
		break;
	}

	case DISABLE_DERING: {
		pDecInfo->deringEnable = 0;
		break;
	}
	case SET_SEC_AXI: {
		SecAxiUse secAxiUse;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}

		secAxiUse = *(SecAxiUse *)param;

		pDecInfo->secAxiInfo.u.coda9.useBitEnable =
			secAxiUse.u.coda9.useBitEnable;
		pDecInfo->secAxiInfo.u.coda9.useIpEnable =
			secAxiUse.u.coda9.useIpEnable;
		pDecInfo->secAxiInfo.u.coda9.useDbkYEnable =
			secAxiUse.u.coda9.useDbkYEnable;
		pDecInfo->secAxiInfo.u.coda9.useDbkCEnable =
			secAxiUse.u.coda9.useDbkCEnable;
		pDecInfo->secAxiInfo.u.coda9.useOvlEnable =
			secAxiUse.u.coda9.useOvlEnable;
		pDecInfo->secAxiInfo.u.coda9.useBtpEnable =
			secAxiUse.u.coda9.useBtpEnable;

		break;
	}
	case ENABLE_AFBCE: {
		pDecInfo->enableAfbce = 1;
		break;
	}
	case DISABLE_AFBCE: {
		pDecInfo->enableAfbce = 0;
		break;
	}
	case ENABLE_REP_USERDATA: {
#ifdef ENABLE_CODA9_WRITE_PROTECT
		WriteMemProtectCfg *pCgf = &pDecInfo->writeMemProtectCfg;
#endif
		if (!pDecInfo->userDataBufAddr) {
			return RETCODE_USERDATA_BUF_NOT_SET;
		}
		if (pDecInfo->userDataBufSize == 0) {
			return RETCODE_USERDATA_BUF_NOT_SET;
		}
		switch (pCodecInst->productId) {
		case PRODUCT_ID_420L:
			pDecInfo->userDataEnable = *(Uint32 *)param;
			break;
		case PRODUCT_ID_960:
		case PRODUCT_ID_980:
			pDecInfo->userDataEnable = TRUE;
			break;
		default:
			VLOG(INFO,
			     "%s(ENABLE_REP_DATA) invalid productId(%d)\n",
			     __FUNCTION__, pCodecInst->productId);
			return RETCODE_INVALID_PARAM;
		}
#ifdef ENABLE_CODA9_WRITE_PROTECT
		pCgf->decRegion[WPROT_DEC_REPORT].enable = 1;
		pCgf->decRegion[WPROT_DEC_REPORT].isSecondary = 0;
		pCgf->decRegion[WPROT_DEC_REPORT].startAddress =
			pDecInfo->userDataBufAddr;
		pCgf->decRegion[WPROT_DEC_REPORT].endAddress =
			pDecInfo->userDataBufAddr + pDecInfo->userDataBufSize;
		VLOG(INFO,
		     "[%d]%s.set decRegion[WPROT_DEC_REPORT](%d,%d,0x%08x,0x%08x)\n",
		     __LINE__, __func__,
		     pCgf->decRegion[WPROT_DEC_REPORT].enable,
		     pCgf->decRegion[WPROT_DEC_REPORT].isSecondary,
		     pCgf->decRegion[WPROT_DEC_REPORT].startAddress,
		     pCgf->decRegion[WPROT_DEC_REPORT].endAddress);
#endif
		break;
	}
	case DISABLE_REP_USERDATA: {
		pDecInfo->userDataEnable = 0;
		break;
	}
	case SET_ADDR_REP_USERDATA: {
		PhysicalAddress userDataBufAddr;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		userDataBufAddr = *(PhysicalAddress *)param;
		if (userDataBufAddr % 8 != 0 || userDataBufAddr == 0) {
			return RETCODE_INVALID_PARAM;
		}

		pDecInfo->userDataBufAddr = userDataBufAddr;
		break;
	}
	case SET_VIRT_ADDR_REP_USERDATA: {
		unsigned long userDataVirtAddr;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}

		if (!pDecInfo->userDataBufAddr) {
			return RETCODE_USERDATA_BUF_NOT_SET;
		}
		if (pDecInfo->userDataBufSize == 0) {
			return RETCODE_USERDATA_BUF_NOT_SET;
		}

		userDataVirtAddr = *(unsigned long *)param;
		if (!userDataVirtAddr) {
			return RETCODE_INVALID_PARAM;
		}

		pDecInfo->vbUserData.phys_addr = pDecInfo->userDataBufAddr;
		pDecInfo->vbUserData.size = pDecInfo->userDataBufSize;
		pDecInfo->vbUserData.virt_addr =
			(unsigned long)userDataVirtAddr;
		if (vdi_attach_dma_memory(pCodecInst->coreIdx,
					  &pDecInfo->vbUserData) != 0) {
			return RETCODE_INSUFFICIENT_RESOURCE;
		}
		break;
	}
	case SET_SIZE_REP_USERDATA: {
		PhysicalAddress userDataBufSize;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		userDataBufSize = *(PhysicalAddress *)param;

		pDecInfo->userDataBufSize = userDataBufSize;
		break;
	}

	case SET_USERDATA_REPORT_MODE: {
		int userDataMode;

		userDataMode = *(int *)param;
		if (userDataMode != 1 && userDataMode != 0) {
			return RETCODE_INVALID_PARAM;
		}
		pDecInfo->userDataReportMode = userDataMode;
		break;
	}
	case SET_CACHE_CONFIG: {
		MaverickCacheConfig *mcCacheConfig;
		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		mcCacheConfig = (MaverickCacheConfig *)param;
		pDecInfo->cacheConfig = *mcCacheConfig;
	} break;
	case SET_LOW_DELAY_CONFIG: {
		LowDelayInfo *lowDelayInfo;
		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		if (pCodecInst->productId != PRODUCT_ID_980) {
			return RETCODE_NOT_SUPPORTED_FEATURE;
		}
		lowDelayInfo = (LowDelayInfo *)param;

		if (lowDelayInfo->lowDelayEn) {
			if ((pCodecInst->codecMode != AVC_DEC &&
			     pCodecInst->codecMode != W_AVC_DEC) ||
			    pDecInfo->rotationEnable ||
			    pDecInfo->mirrorEnable ||
			    pDecInfo->tiled2LinearEnable ||
			    pDecInfo->deringEnable) {
				return RETCODE_INVALID_PARAM;
			}
		}

		pDecInfo->lowDelayInfo.lowDelayEn = lowDelayInfo->lowDelayEn;
		pDecInfo->lowDelayInfo.numRows = lowDelayInfo->numRows;
	} break;

	case SET_DECODE_FLUSH: // interrupt mode to pic_end
		ret = ProductCpbFlush((CodecInst *)handle);
		break;

	case DEC_SET_FRAME_DELAY: {
		pDecInfo->frameDelay = *(int *)param;
		break;
	}
	case DEC_ENABLE_REORDER: {
		if ((handle->productId == PRODUCT_ID_980) ||
		    (handle->productId == PRODUCT_ID_960) ||
		    (handle->productId == PRODUCT_ID_950)) {
			if (pDecInfo->initialInfoObtained) {
				return RETCODE_WRONG_CALL_SEQUENCE;
			}
		}

		pDecInfo->reorderEnable = 1;
		break;
	}
	case DEC_DISABLE_REORDER: {
		if ((handle->productId == PRODUCT_ID_980) ||
		    (handle->productId == PRODUCT_ID_960) ||
		    (handle->productId == PRODUCT_ID_950)) {
			if (pDecInfo->initialInfoObtained) {
				return RETCODE_WRONG_CALL_SEQUENCE;
			}

			if (pCodecInst->codecMode != AVC_DEC &&
			    pCodecInst->codecMode != VC1_DEC &&
			    pCodecInst->codecMode != AVS_DEC &&
			    pCodecInst->codecMode != W_AVC_DEC &&
			    pCodecInst->codecMode != W_VC1_DEC &&
			    pCodecInst->codecMode != W_AVS_DEC) {
				return RETCODE_INVALID_COMMAND;
			}
		}

		pDecInfo->reorderEnable = 0;
		break;
	}
	case DEC_SET_AVC_ERROR_CONCEAL_MODE: {
		if (pCodecInst->codecMode != AVC_DEC &&
		    pCodecInst->codecMode != W_AVC_DEC) {
			return RETCODE_INVALID_COMMAND;
		}

		pDecInfo->avcErrorConcealMode = *(int *)param;
		break;
	}
	case DEC_FREE_FRAME_BUFFER: {
		int i;
		if (pDecInfo->vbSlice.size) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory_no_mmap vbSlice(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, pDecInfo->vbSlice.phys_addr,
			     pDecInfo->vbSlice.base,
			     pDecInfo->vbSlice.virt_addr,
			     pDecInfo->vbSlice.size,
			     pDecInfo->vbSlice.req_spec_region);
			vdi_free_dma_memory_no_mmap(pCodecInst->coreIdx,
						    &pDecInfo->vbSlice);
		}

		if (pDecInfo->vbFrame.size) {
			if (pDecInfo->frameAllocExt == 0) {
				VLOG(TRACE,
				     "[%d]%s.vdi_free_dma_memory vbFrame(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
				     __LINE__, __func__,
				     pDecInfo->vbFrame.phys_addr,
				     pDecInfo->vbFrame.base,
				     pDecInfo->vbFrame.virt_addr,
				     pDecInfo->vbFrame.size,
				     pDecInfo->vbFrame.req_spec_region);
				vdi_free_dma_memory(pCodecInst->coreIdx,
						    &pDecInfo->vbFrame);
			}
		}
		for (i = 0; i < MAX_REG_FRAME; i++) {
			if (pDecInfo->vbFbcYTbl[i].size) {
				if (pDecInfo->fbcTblAllocExt == 0) {
					VLOG(TRACE,
					     "[%d]%s.vdi_free_dma_memory vbFbcYTbl[%d](0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
					     __LINE__, __func__, i,
					     pDecInfo->vbFbcYTbl[i].phys_addr,
					     pDecInfo->vbFbcYTbl[i].base,
					     pDecInfo->vbFbcYTbl[i].virt_addr,
					     pDecInfo->vbFbcYTbl[i].size,
					     pDecInfo->vbFbcYTbl[i]
						     .req_spec_region);
					vdi_free_dma_memory(
						pCodecInst->coreIdx,
						&pDecInfo->vbFbcYTbl[i]);
				}
			}

			if (pDecInfo->vbFbcCTbl[i].size) {
				if (pDecInfo->fbcTblAllocExt == 0) {
					VLOG(TRACE,
					     "[%d]%s.vdi_free_dma_memory vbFbcCTbl[%d](0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
					     __LINE__, __func__, i,
					     pDecInfo->vbFbcCTbl[i].phys_addr,
					     pDecInfo->vbFbcCTbl[i].base,
					     pDecInfo->vbFbcCTbl[i].virt_addr,
					     pDecInfo->vbFbcCTbl[i].size,
					     pDecInfo->vbFbcCTbl[i]
						     .req_spec_region);
					vdi_free_dma_memory(
						pCodecInst->coreIdx,
						&pDecInfo->vbFbcCTbl[i]);
				}
			}

#if !defined(DAH_222_PREALLOC_MV_SLICE_BUFFER)
			if (pDecInfo->vbMV[i].size)
				vdi_free_dma_memory_no_mmap(pCodecInst->coreIdx,
							    &pDecInfo->vbMV[i]);
#endif
		}

		if (pDecInfo->vbPPU.size) {
			if (pDecInfo->ppuAllocExt == 0) {
				VLOG(TRACE,
				     "[%d]%s.vdi_free_dma_memory vbPPU(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
				     __LINE__, __func__,
				     pDecInfo->vbPPU.phys_addr,
				     pDecInfo->vbPPU.base,
				     pDecInfo->vbPPU.virt_addr,
				     pDecInfo->vbPPU.size,
				     pDecInfo->vbPPU.req_spec_region);
				vdi_free_dma_memory(pCodecInst->coreIdx,
						    &pDecInfo->vbPPU);
			}
		}

		if (pDecInfo->wtlEnable) {
			if (pDecInfo->vbWTL.size) {
				VLOG(TRACE,
				     "[%d]%s.vdi_free_dma_memory vbWTL(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
				     __LINE__, __func__,
				     pDecInfo->vbWTL.phys_addr,
				     pDecInfo->vbWTL.base,
				     pDecInfo->vbWTL.virt_addr,
				     pDecInfo->vbWTL.size,
				     pDecInfo->vbWTL.req_spec_region);
				vdi_free_dma_memory(pCodecInst->coreIdx,
						    &pDecInfo->vbWTL);
			}
		}
		break;
	}
	case DEC_GET_FRAMEBUF_INFO: {
		DecGetFramebufInfo *fbInfo = (DecGetFramebufInfo *)param;
		Uint32 i;
		fbInfo->vbFrame = pDecInfo->vbFrame;
		fbInfo->vbWTL = pDecInfo->vbWTL;
		for (i = 0; i < MAX_REG_FRAME; i++) {
			fbInfo->vbFbcYTbl[i] = pDecInfo->vbFbcYTbl[i];
			fbInfo->vbFbcCTbl[i] = pDecInfo->vbFbcCTbl[i];
			fbInfo->vbMvCol[i] = pDecInfo->vbMV[i];
		}

		for (i = 0; i < MAX_GDI_IDX * 2; i++) {
			fbInfo->framebufPool[i] = pDecInfo->frameBufPool[i];
		}
	} break;
	case DEC_RESET_FRAMEBUF_INFO: {
		int i;

		pDecInfo->vbFrame.base = 0;
		pDecInfo->vbFrame.phys_addr = 0;
		pDecInfo->vbFrame.virt_addr = 0;
		pDecInfo->vbFrame.size = 0;
		pDecInfo->vbWTL.base = 0;
		pDecInfo->vbWTL.phys_addr = 0;
		pDecInfo->vbWTL.virt_addr = 0;
		pDecInfo->vbWTL.size = 0;
		for (i = 0; i < MAX_REG_FRAME; i++) {
			pDecInfo->vbFbcYTbl[i].base = 0;
			pDecInfo->vbFbcYTbl[i].phys_addr = 0;
			pDecInfo->vbFbcYTbl[i].virt_addr = 0;
			pDecInfo->vbFbcYTbl[i].size = 0;
			pDecInfo->vbFbcCTbl[i].base = 0;
			pDecInfo->vbFbcCTbl[i].phys_addr = 0;
			pDecInfo->vbFbcCTbl[i].virt_addr = 0;
			pDecInfo->vbFbcCTbl[i].size = 0;
			pDecInfo->vbMV[i].base = 0;
			pDecInfo->vbMV[i].phys_addr = 0;
			pDecInfo->vbMV[i].virt_addr = 0;
			pDecInfo->vbMV[i].size = 0;
		}

		pDecInfo->frameDisplayFlag = 0;
		pDecInfo->setDisplayIndexes = 0;
		pDecInfo->clearDisplayIndexes = 0;
		break;
	}
	case DEC_GET_QUEUE_STATUS: {
		DecQueueStatusInfo *queueInfo = (DecQueueStatusInfo *)param;
		queueInfo->instanceQueueCount = pDecInfo->instanceQueueCount;
		queueInfo->totalQueueCount = pDecInfo->totalQueueCount;
		break;
	}

	case ENABLE_DEC_THUMBNAIL_MODE: {
		pDecInfo->thumbnailMode = 1;
		break;
	}
	case DEC_GET_SEQ_INFO: {
		DecInitialInfo *seqInfo = (DecInitialInfo *)param;
		*seqInfo = pDecInfo->initialInfo;
		break;
	}
	case DEC_SET_SEQ_INFO: {
		DecInitialInfo *seqInfo = (DecInitialInfo *)param;
		pDecInfo->initialInfo = *seqInfo;
		break;
	}
	case DEC_GET_FIELD_PIC_TYPE: {
		return RETCODE_FAILURE;
	}
	case DEC_GET_DISPLAY_OUTPUT_INFO: {
		DecOutputInfo *pDecOutInfo = (DecOutputInfo *)param;
		*pDecOutInfo =
			pDecInfo->decOutInfo[pDecOutInfo->indexFrameDisplay];
		break;
	}
	case GET_TILEDMAP_CONFIG: {
		TiledMapConfig *pMapCfg = (TiledMapConfig *)param;
		if (!pMapCfg) {
			return RETCODE_INVALID_PARAM;
		}
		if (!pDecInfo->stride) {
			return RETCODE_WRONG_CALL_SEQUENCE;
		}
		*pMapCfg = pDecInfo->mapCfg;
		break;
	}
	case SET_DRAM_CONFIG: {
		DRAMConfig *cfg = (DRAMConfig *)param;

		if (!cfg) {
			return RETCODE_INVALID_PARAM;
		}

		pDecInfo->dramCfg = *cfg;
		break;
	}
	case GET_DRAM_CONFIG: {
		DRAMConfig *cfg = (DRAMConfig *)param;

		if (!cfg) {
			return RETCODE_INVALID_PARAM;
		}

		*cfg = pDecInfo->dramCfg;

		break;
	}
	case GET_LOW_DELAY_OUTPUT: {
		DecOutputInfo *lowDelayOutput;
		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}

		if (!pDecInfo->lowDelayInfo.lowDelayEn ||
		    pCodecInst->codecMode != AVC_DEC) {
			return RETCODE_INVALID_COMMAND;
		}

		if (pCodecInst != GetPendingInst(pCodecInst->coreIdx)) {
			return RETCODE_WRONG_CALL_SEQUENCE;
		}

		lowDelayOutput = (DecOutputInfo *)param;

		GetLowDelayOutput(pCodecInst, lowDelayOutput);
	} break;
	case ENABLE_LOGGING: {
		pCodecInst->loggingEnable = 1;
	} break;
	case DISABLE_LOGGING: {
		pCodecInst->loggingEnable = 0;
	} break;
	case DEC_SET_SEQ_CHANGE_MASK:
		if (PRODUCT_ID_NOT_W_SERIES(pCodecInst->productId))
			return RETCODE_INVALID_PARAM;
		pDecInfo->seqChangeMask = *(int *)param;
		break;
	case DEC_SET_WTL_FRAME_FORMAT:
		pDecInfo->wtlFormat = *(FrameBufferFormat *)param;
		break;
	case DEC_SET_DISPLAY_FLAG: {
		Int32 index;
		VpuAttr *pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];
		BOOL supportCommandQueue = FALSE;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}

		index = *(Int32 *)param;

		supportCommandQueue = (pAttr->supportCommandQueue == TRUE);
		if (supportCommandQueue == TRUE) {
			EnterLock(pCodecInst->coreIdx);
			LeaveLock(pCodecInst->coreIdx);
		} else {
			EnterDispFlagLock(pCodecInst->coreIdx);
			pDecInfo->setDisplayIndexes |= (1 << index);
			LeaveDispFlagLock(pCodecInst->coreIdx);
		}

	} break;
	case DEC_GET_SCALER_INFO: {
		ScalerInfo *scalerInfo = (ScalerInfo *)param;
		if (scalerInfo == NULL) {
			return RETCODE_INVALID_PARAM;
		}
		scalerInfo->enScaler = pDecInfo->scalerEnable;
		scalerInfo->scaleWidth = pDecInfo->scaleWidth;
		scalerInfo->scaleHeight = pDecInfo->scaleHeight;
	} break;
	case DEC_SET_SCALER_INFO: {
		ScalerInfo *scalerInfo = (ScalerInfo *)param;

		if (!pDecInfo->initialInfoObtained) {
			return RETCODE_WRONG_CALL_SEQUENCE;
		}

		if (scalerInfo == NULL) {
			return RETCODE_INVALID_PARAM;
		}

		pDecInfo->scalerEnable = scalerInfo->enScaler;
		if (scalerInfo->enScaler == TRUE) {
			// minW = Ceil8(picWidth/8), minH = Ceil8(picHeight/8)
			Uint32 minScaleWidth =
				VPU_ALIGN8(pDecInfo->initialInfo.picWidth >> 3);
			Uint32 minScaleHeight = VPU_ALIGN8(
				pDecInfo->initialInfo.picHeight >> 3);

			if (minScaleWidth == 0)
				minScaleWidth = 8;
			if (minScaleHeight == 0)
				minScaleHeight = 8;

			if (scalerInfo->scaleWidth < minScaleWidth ||
			    scalerInfo->scaleHeight < minScaleHeight) {
				return RETCODE_INVALID_PARAM;
			}

			if (scalerInfo->scaleWidth > 0 ||
			    scalerInfo->scaleHeight > 0) {
				if ((scalerInfo->scaleWidth % 8) ||
				    scalerInfo->scaleWidth >
					    (Uint32)(VPU_ALIGN8(
						    pDecInfo->initialInfo
							    .picWidth))) {
					return RETCODE_INVALID_PARAM;
				}

				if ((scalerInfo->scaleHeight % 8) ||
				    scalerInfo->scaleHeight >
					    (Uint32)(VPU_ALIGN8(
						    pDecInfo->initialInfo
							    .picHeight))) {
					return RETCODE_INVALID_PARAM;
				}
				pDecInfo->scaleWidth = scalerInfo->scaleWidth;
				pDecInfo->scaleHeight = scalerInfo->scaleHeight;
				pDecInfo->scalerEnable = scalerInfo->enScaler;
			}
		}
		break;
	}
	case DEC_SET_TARGET_TEMPORAL_ID:
		if (param == NULL) {
			return RETCODE_INVALID_PARAM;
		}
		if (pCodecInst->codecMode != HEVC_DEC) {
			return RETCODE_NOT_SUPPORTED_FEATURE;
		}
		break;
	case DEC_SET_BWB_CUR_FRAME_IDX:
		pDecInfo->chBwbFrameIdx = *(Uint32 *)param;
		break;
	case DEC_SET_FBC_CUR_FRAME_IDX:
		pDecInfo->chFbcFrameIdx = *(Uint32 *)param;
		break;
	case DEC_SET_INTER_RES_INFO_ON:
		pDecInfo->interResChange = 1;
		break;
	case DEC_SET_INTER_RES_INFO_OFF:
		pDecInfo->interResChange = 0;
		break;
	case DEC_FREE_FBC_TABLE_BUFFER: {
		Uint32 fbcCurFrameIdx = *(Uint32 *)param;
		if (pDecInfo->vbFbcYTbl[fbcCurFrameIdx].size > 0) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory vbFbcYTbl[%d](0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, fbcCurFrameIdx,
			     pDecInfo->vbFbcYTbl[fbcCurFrameIdx].phys_addr,
			     pDecInfo->vbFbcYTbl[fbcCurFrameIdx].base,
			     pDecInfo->vbFbcYTbl[fbcCurFrameIdx].virt_addr,
			     pDecInfo->vbFbcYTbl[fbcCurFrameIdx].size,
			     pDecInfo->vbFbcYTbl[fbcCurFrameIdx]
				     .req_spec_region);
			vdi_free_dma_memory(
				pCodecInst->coreIdx,
				&pDecInfo->vbFbcYTbl[fbcCurFrameIdx]);
			pDecInfo->vbFbcYTbl[fbcCurFrameIdx].size = 0;
		}
		if (pDecInfo->vbFbcCTbl[fbcCurFrameIdx].size > 0) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory vbFbcCTbl[%d](0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, fbcCurFrameIdx,
			     pDecInfo->vbFbcCTbl[fbcCurFrameIdx].phys_addr,
			     pDecInfo->vbFbcCTbl[fbcCurFrameIdx].base,
			     pDecInfo->vbFbcCTbl[fbcCurFrameIdx].virt_addr,
			     pDecInfo->vbFbcCTbl[fbcCurFrameIdx].size,
			     pDecInfo->vbFbcCTbl[fbcCurFrameIdx]
				     .req_spec_region);
			vdi_free_dma_memory(
				pCodecInst->coreIdx,
				&pDecInfo->vbFbcCTbl[fbcCurFrameIdx]);
			pDecInfo->vbFbcCTbl[fbcCurFrameIdx].size = 0;
		}
	} break;
	case DEC_FREE_MV_BUFFER: {
		Uint32 fbcCurFrameIdx = *(Uint32 *)param;
		if (pDecInfo->vbMV[fbcCurFrameIdx].size > 0) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory_no_mmap vbMV[%d](0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, fbcCurFrameIdx,
			     pDecInfo->vbMV[fbcCurFrameIdx].phys_addr,
			     pDecInfo->vbMV[fbcCurFrameIdx].base,
			     pDecInfo->vbMV[fbcCurFrameIdx].virt_addr,
			     pDecInfo->vbMV[fbcCurFrameIdx].size,
			     pDecInfo->vbMV[fbcCurFrameIdx].req_spec_region);
			vdi_free_dma_memory_no_mmap(
				pCodecInst->coreIdx,
				&pDecInfo->vbMV[fbcCurFrameIdx]);
			pDecInfo->vbMV[fbcCurFrameIdx].size = 0;
		}
	} break;
	case DEC_SET_YTBL_ADDR:
	case DEC_SET_CTBL_ADDR:
		if (param == NULL) {
			return RETCODE_INVALID_PARAM;
		}
		if (pCodecInst->codecMode != HEVC_DEC &&
		    pCodecInst->codecMode != W_VP9_DEC) {
			return RETCODE_NOT_SUPPORTED_FEATURE;
		} else {
			int i = 0;
			vpu_buffer_t *vb = (vpu_buffer_t *)param;
			pDecInfo->fbcTblAllocExt = 1;
			for (i = 0; i < MAX_GDI_IDX; i++) {
				if (cmd == DEC_SET_YTBL_ADDR)
					osal_memcpy((void *)&pDecInfo
							    ->extVbFbcYTbls[i],
						    &vb[i],
						    sizeof(vpu_buffer_t));
				else
					osal_memcpy((void *)&pDecInfo
							    ->extVbFbcCTbls[i],
						    &vb[i],
						    sizeof(vpu_buffer_t));
			}
		}
		break;
	case DEC_SET_WTL_MODE: {
		if (param == NULL) {
			return RETCODE_INVALID_PARAM;
		}

		pop = (DecOpenParam *)param;

		pDecInfo->openParam.wtlEnable = pop->wtlEnable;
		pDecInfo->openParam.wtlMode = pop->wtlMode;
		pDecInfo->wtlEnable = pop->wtlEnable;
		pDecInfo->wtlMode = pop->wtlMode;
		if (!pDecInfo->wtlEnable)
			pDecInfo->wtlMode = 0;
	} break;
	case DEC_SET_T2L_MODE: {
		if (param == NULL) {
			return RETCODE_INVALID_PARAM;
		}

		pop = (DecOpenParam *)param;

		pDecInfo->openParam.tiled2LinearEnable =
			pop->tiled2LinearEnable;
		pDecInfo->openParam.tiled2LinearMode = pop->tiled2LinearMode;
		pDecInfo->tiled2LinearEnable = pop->tiled2LinearEnable;
		pDecInfo->tiled2LinearMode = pop->tiled2LinearMode;
	} break;
#ifdef SUPPORT_GET_NAL_START_POS
	case DEC_GET_NAL_START_POS: {
		PhysicalAddress *pos = (PhysicalAddress *)param;

		*pos = pDecInfo->nalStartPtr;
	} break;
#endif

	default:
		return RETCODE_INVALID_COMMAND;
	}

	return ret;
}

RetCode VPU_DecAllocateFrameBuffer(DecHandle handle, FrameBufferAllocInfo info,
				   FrameBuffer *frameBuffer)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;
	Uint32 gdiIndex;
	int i;

	VLOG(TRACE, "[+] [%d]%s.h:0x%x.mapType:%d.%d.%d.%d\n", __LINE__,
	     __func__, handle, info.mapType, info.cbcrInterleave, info.nv21,
	     info.format);
	VLOG(TRACE, "[+] [%d]%s.h:0x%x.stride:%d.%d.%d.%d.%d\n", __LINE__,
	     __func__, handle, info.stride, info.height, info.size,
	     info.lumaBitDepth, info.chromaBitDepth);
	VLOG(TRACE, "[+] [%d]%s.h:0x%x.endian:%d.%d.%d\n", __LINE__, __func__,
	     handle, info.endian, info.num, info.type);
	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__,
		     handle, ret);
		return ret;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	if (!frameBuffer) {
		VLOG(ERR, "[-] [%d]%s.h:0x%x.ret:RETCODE_INVALID_PARAM\n",
		     __LINE__, __func__, handle);
		return RETCODE_INVALID_PARAM;
	}

	if (info.num) {
		for (i = 0; i < info.num; i++) {
			VLOG(TRACE,
			     "[%d]%s.h:0x%x.size:%d.bufY:0x%x.0x%x.0x%x.updateFbInfo:%d\n",
			     __LINE__, __func__, handle, frameBuffer[i].size,
			     frameBuffer[i].bufY, frameBuffer[i].bufCb,
			     frameBuffer[i].bufCr, frameBuffer[i].updateFbInfo);
		}
	}

	if (info.type == FB_TYPE_PPU) {
		if (pDecInfo->numFrameBuffers == 0)
			return RETCODE_WRONG_CALL_SEQUENCE;
		if (frameBuffer[0].updateFbInfo == TRUE) {
			pDecInfo->ppuAllocExt = TRUE;
		}
		pDecInfo->ppuAllocExt = frameBuffer[0].updateFbInfo;
		gdiIndex = pDecInfo->numFbsForDecoding;
		ret = ProductVpuAllocateFramebuffer(
			pCodecInst, frameBuffer, (TiledMapType)info.mapType,
			(Int32)info.num, info.stride, info.height, info.format,
			info.cbcrInterleave, info.nv21, info.endian,
			&pDecInfo->vbPPU, gdiIndex, FB_TYPE_PPU);
	} else if (info.type == FB_TYPE_CODEC) {
		gdiIndex = 0;
		if (frameBuffer[0].updateFbInfo == TRUE) {
			pDecInfo->frameAllocExt = TRUE;
		}
		ret = ProductVpuAllocateFramebuffer(
			pCodecInst, frameBuffer, (TiledMapType)info.mapType,
			(Int32)info.num, info.stride, info.height, info.format,
			info.cbcrInterleave, info.nv21, info.endian,
			&pDecInfo->vbFrame, gdiIndex,
			(FramebufferAllocType)info.type);

		pDecInfo->mapCfg.tiledBaseAddr = pDecInfo->vbFrame.phys_addr;
	}

	VLOG(TRACE, "[-] [%d]%s.h:0x%x.ret:%d\n", __LINE__, __func__, handle,
	     ret);
	return ret;
}

RetCode VPU_EncOpen(EncHandle *pHandle, EncOpenParam *pop)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;

	if ((ret = ProductCheckEncOpenParam(pop)) != RETCODE_SUCCESS)
		return ret;

	EnterLock(pop->coreIdx);

	if (VPU_IsInit(pop->coreIdx) == 0) {
		LeaveLock(pop->coreIdx);
		return RETCODE_NOT_INITIALIZED;
	}
	ret = GetCodecInstance(pop->coreIdx, &pCodecInst, pop->filp);
	if (ret == RETCODE_FAILURE) {
		*pHandle = 0;
		LeaveLock(pop->coreIdx);
		return RETCODE_FAILURE;
	}

	pCodecInst->isDecoder = FALSE;
	*pHandle = pCodecInst;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;

	osal_memset(pEncInfo, 0x00, sizeof(EncInfo));
	pEncInfo->openParam = *pop;

	SetClockGate(pop->coreIdx, TRUE);
	if ((ret = ProductVpuEncBuildUpOpenParam(pCodecInst, pop)) !=
	    RETCODE_SUCCESS) {
		*pHandle = 0;
	}
	SetClockGate(pop->coreIdx, FALSE);

	LeaveLock(pCodecInst->coreIdx);

	return ret;
}

RetCode VPU_EncClose(EncHandle handle)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;

	EnterLock(pCodecInst->coreIdx);

	if (pEncInfo->initialInfoObtained) {
		VpuWriteReg(pCodecInst->coreIdx, pEncInfo->streamWrPtrRegAddr,
			    pEncInfo->streamWrPtr);
		VpuWriteReg(pCodecInst->coreIdx, pEncInfo->streamRdPtrRegAddr,
			    pEncInfo->streamRdPtr);

		if ((ret = ProductVpuEncFiniSeq(pCodecInst)) !=
		    RETCODE_SUCCESS) {
			if (pCodecInst->loggingEnable)
				vdi_log(pCodecInst->coreIdx, ENC_SEQ_END, 0);

			if (ret == RETCODE_VPU_STILL_RUNNING) {
				LeaveLock(pCodecInst->coreIdx);
				return ret;
			}
		}
		if (pCodecInst->loggingEnable)
			vdi_log(pCodecInst->coreIdx, ENC_SEQ_END, 0);
		pEncInfo->streamWrPtr = VpuReadReg(
			pCodecInst->coreIdx, pEncInfo->streamWrPtrRegAddr);
	}

	if (pEncInfo->vbScratch.size) {
		VLOG(TRACE,
		     "[%d]%s.vdi_free_dma_memory vbScratch(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
		     __LINE__, __func__, pEncInfo->vbScratch.phys_addr,
		     pEncInfo->vbScratch.base, pEncInfo->vbScratch.virt_addr,
		     pEncInfo->vbScratch.size,
		     pEncInfo->vbScratch.req_spec_region);
		vdi_free_dma_memory(pCodecInst->coreIdx, &pEncInfo->vbScratch);
	}

	if (pEncInfo->vbWork.size) {
		VLOG(TRACE,
		     "[%d]%s.vdi_free_dma_memory vbWork(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
		     __LINE__, __func__, pEncInfo->vbWork.phys_addr,
		     pEncInfo->vbWork.base, pEncInfo->vbWork.virt_addr,
		     pEncInfo->vbWork.size, pEncInfo->vbWork.req_spec_region);
		vdi_free_dma_memory(pCodecInst->coreIdx, &pEncInfo->vbWork);
	}

	if (pEncInfo->vbFrame.size) {
		if (pEncInfo->frameAllocExt == 0) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory vbFrame(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, pEncInfo->vbFrame.phys_addr,
			     pEncInfo->vbFrame.base,
			     pEncInfo->vbFrame.virt_addr,
			     pEncInfo->vbFrame.size,
			     pEncInfo->vbFrame.req_spec_region);
			vdi_free_dma_memory(pCodecInst->coreIdx,
					    &pEncInfo->vbFrame);
		}
	}

	if (pCodecInst->codecMode == W_AVC_ENC) {
		if (pEncInfo->vbFbcYTbl.size) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory vbFbcYTbl(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, pEncInfo->vbFbcYTbl.phys_addr,
			     pEncInfo->vbFbcYTbl.base,
			     pEncInfo->vbFbcYTbl.virt_addr,
			     pEncInfo->vbFbcYTbl.size,
			     pEncInfo->vbFbcYTbl.req_spec_region);
			vdi_free_dma_memory(pCodecInst->coreIdx,
					    &pEncInfo->vbFbcYTbl);
		}

		if (pEncInfo->vbFbcCTbl.size) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory vbFbcCTbl(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, pEncInfo->vbFbcCTbl.phys_addr,
			     pEncInfo->vbFbcCTbl.base,
			     pEncInfo->vbFbcCTbl.virt_addr,
			     pEncInfo->vbFbcCTbl.size,
			     pEncInfo->vbFbcCTbl.req_spec_region);
			vdi_free_dma_memory(pCodecInst->coreIdx,
					    &pEncInfo->vbFbcCTbl);
		}
	}

	if (pEncInfo->vbPPU.size) {
		if (pEncInfo->ppuAllocExt == 0) {
			VLOG(TRACE,
			     "[%d]%s.vdi_free_dma_memory vbPPU(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, pEncInfo->vbPPU.phys_addr,
			     pEncInfo->vbPPU.base, pEncInfo->vbPPU.virt_addr,
			     pEncInfo->vbPPU.size,
			     pEncInfo->vbPPU.req_spec_region);
			vdi_free_dma_memory(pCodecInst->coreIdx,
					    &pEncInfo->vbPPU);
		}
	}
	if (pEncInfo->vbSubSampFrame.size) {
		VLOG(TRACE,
		     "[%d]%s.vdi_free_dma_memory vbSubSampFrame(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
		     __LINE__, __func__, pEncInfo->vbSubSampFrame.phys_addr,
		     pEncInfo->vbSubSampFrame.base,
		     pEncInfo->vbSubSampFrame.virt_addr,
		     pEncInfo->vbSubSampFrame.size,
		     pEncInfo->vbSubSampFrame.req_spec_region);
		vdi_free_dma_memory(pCodecInst->coreIdx,
				    &pEncInfo->vbSubSampFrame);
	}
	if (pEncInfo->vbMvcSubSampFrame.size) {
		VLOG(TRACE,
		     "[%d]%s.vdi_free_dma_memory vbMvcSubSampFrame(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
		     __LINE__, __func__, pEncInfo->vbMvcSubSampFrame.phys_addr,
		     pEncInfo->vbMvcSubSampFrame.base,
		     pEncInfo->vbMvcSubSampFrame.virt_addr,
		     pEncInfo->vbMvcSubSampFrame.size,
		     pEncInfo->vbMvcSubSampFrame.req_spec_region);
		vdi_free_dma_memory(pCodecInst->coreIdx,
				    &pEncInfo->vbMvcSubSampFrame);
	}

	LeaveLock(pCodecInst->coreIdx);

	FreeCodecInstance(pCodecInst);

	return ret;
}

RetCode VPU_EncGetInitialInfo(EncHandle handle, EncInitialInfo *info)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (info == 0) {
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;

	EnterLock(pCodecInst->coreIdx);

	if (GetPendingInst(pCodecInst->coreIdx)) {
		if (VPU_GetOpenInstanceNum(pCodecInst->coreIdx) > 1) //RTK
		{
			VLOG(WARN, "In[%s][%d] usleep 50ms and try again\n",
			     __func__, __LINE__);
			msleep(50);
			if (GetPendingInst(pCodecInst->coreIdx)) {
				LeaveLock(pCodecInst->coreIdx);
				return RETCODE_FRAME_NOT_COMPLETE;
			}
		} else {
			LeaveLock(pCodecInst->coreIdx);
			return RETCODE_FRAME_NOT_COMPLETE;
		}
	}

	if ((ret = ProductVpuEncSetup(pCodecInst)) != RETCODE_SUCCESS) {
		LeaveLock(pCodecInst->coreIdx);
		return ret;
	}

	if (pCodecInst->codecMode == AVC_ENC &&
	    pCodecInst->codecModeAux == AVC_AUX_MVC)
		info->minFrameBufferCount =
			3; // reconstructed frame + 2 reference frame
	else if (pCodecInst->codecMode == W_AVC_ENC &&
		 pCodecInst->codecModeAux == AVC_AUX_MVC)
		info->minFrameBufferCount =
			3; // reconstructed frame + 2 reference frame
	else
		info->minFrameBufferCount =
			2; // reconstructed frame + reference frame

	pEncInfo->initialInfo = *info;
	pEncInfo->initialInfoObtained = TRUE;

	LeaveLock(pCodecInst->coreIdx);

	return RETCODE_SUCCESS;
}

RetCode VPU_EncRegisterFrameBuffer(EncHandle handle, FrameBuffer *bufArray,
				   int num, int stride, int height, int mapType)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	Int32 i;
	RetCode ret;
	EncOpenParam *openParam;
	FrameBuffer *fb;

	ret = CheckEncInstanceValidity(handle);
	// FIXME temp
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;
	openParam = &pEncInfo->openParam;

	if (pEncInfo->stride)
		return RETCODE_CALLED_BEFORE;

	if (!pEncInfo->initialInfoObtained)
		return RETCODE_WRONG_CALL_SEQUENCE;

	if (num < pEncInfo->initialInfo.minFrameBufferCount)
		return RETCODE_INSUFFICIENT_FRAME_BUFFERS;

	if (stride == 0 || (stride % 8 != 0) || stride < 0)
		return RETCODE_INVALID_STRIDE;

	if (height == 0 || height < 0)
		return RETCODE_INVALID_PARAM;

	EnterLock(pCodecInst->coreIdx);

	if (GetPendingInst(pCodecInst->coreIdx)) {
		if (VPU_GetOpenInstanceNum(pCodecInst->coreIdx) > 1) //RTK
		{
			VLOG(WARN, "In[%s][%d] usleep 50ms and try again\n",
			     __func__, __LINE__);
			msleep(50);
			if (GetPendingInst(pCodecInst->coreIdx)) {
				LeaveLock(pCodecInst->coreIdx);
				return RETCODE_FRAME_NOT_COMPLETE;
			}
		} else {
			LeaveLock(pCodecInst->coreIdx);
			return RETCODE_FRAME_NOT_COMPLETE;
		}
	}

	pEncInfo->numFrameBuffers = num;
	pEncInfo->stride = stride;
	pEncInfo->frameBufferHeight = height;
	pEncInfo->mapType = mapType;
	pEncInfo->mapCfg.productId = pCodecInst->productId;

	if (bufArray) {
		for (i = 0; i < num; i++)
			pEncInfo->frameBufPool[i] = bufArray[i];
	}

	if (pEncInfo->frameAllocExt == FALSE) {
		fb = pEncInfo->frameBufPool;
		if (bufArray) {
			if (bufArray[0].bufCb == (Uint32)-1 &&
			    bufArray[0].bufCr == (Uint32)-1) {
				Uint32 size;
				pEncInfo->frameAllocExt = TRUE;
				size = ProductCalculateFrameBufSize(
					pCodecInst->productId, stride, height,
					(TiledMapType)mapType,
					(FrameBufferFormat)openParam->srcFormat,
					(BOOL)openParam->cbcrInterleave, NULL);
				if (mapType == LINEAR_FRAME_MAP) {
					pEncInfo->vbFrame.phys_addr =
						bufArray[0].bufY;
					pEncInfo->vbFrame.size = size * num;
				}
			}
		}
		ret = ProductVpuAllocateFramebuffer(
			pCodecInst, fb, (TiledMapType)mapType, num, stride,
			height, (FrameBufferFormat)openParam->srcFormat,
			openParam->cbcrInterleave, FALSE,
			openParam->frameEndian, &pEncInfo->vbFrame, 0,
			FB_TYPE_CODEC);
		if (ret != RETCODE_SUCCESS) {
			LeaveLock(pCodecInst->coreIdx);
			return ret;
		}
	}
	ret = ProductVpuRegisterFramebuffer(pCodecInst);

	LeaveLock(pCodecInst->coreIdx);

	return ret;
}

RetCode VPU_EncGetFrameBuffer(EncHandle handle, int frameIdx,
			      FrameBuffer *frameBuf)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;

	if (frameIdx < 0 || frameIdx > pEncInfo->numFrameBuffers)
		return RETCODE_INVALID_PARAM;

	if (frameBuf == 0)
		return RETCODE_INVALID_PARAM;

	*frameBuf = pEncInfo->frameBufPool[frameIdx];

	return RETCODE_SUCCESS;
}

RetCode VPU_EncGetBitstreamBuffer(EncHandle handle, PhysicalAddress *prdPrt,
				  PhysicalAddress *pwrPtr, int *size)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	PhysicalAddress rdPtr;
	PhysicalAddress wrPtr;
	Uint32 room;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (prdPrt == 0 || pwrPtr == 0 || size == 0) {
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;

	rdPtr = pEncInfo->streamRdPtr;

	SetClockGate(pCodecInst->coreIdx, 1);

	if (GetPendingInst(pCodecInst->coreIdx) == pCodecInst)
		wrPtr = VpuReadReg(pCodecInst->coreIdx,
				   pEncInfo->streamWrPtrRegAddr);
	else
		wrPtr = pEncInfo->streamWrPtr;

	SetClockGate(pCodecInst->coreIdx, 0);
	if (pEncInfo->ringBufferEnable == 1 || pEncInfo->lineBufIntEn == 1) {
		if (wrPtr >= rdPtr) {
			room = wrPtr - rdPtr;
		} else {
			room = (pEncInfo->streamBufEndAddr - rdPtr) +
			       (wrPtr - pEncInfo->streamBufStartAddr);
		}
	} else {
		if (wrPtr >= rdPtr)
			room = wrPtr - rdPtr;
		else
			return RETCODE_INVALID_PARAM;
	}

	*prdPrt = rdPtr;
	*pwrPtr = wrPtr;
	*size = room;

	return RETCODE_SUCCESS;
}

RetCode VPU_EncUpdateBitstreamBuffer(EncHandle handle, int size)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	PhysicalAddress wrPtr;
	PhysicalAddress rdPtr;
	RetCode ret;
	int room = 0;
	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;

	rdPtr = pEncInfo->streamRdPtr;

	SetClockGate(pCodecInst->coreIdx, 1);

	if (GetPendingInst(pCodecInst->coreIdx) == pCodecInst)
		wrPtr = VpuReadReg(pCodecInst->coreIdx,
				   pEncInfo->streamWrPtrRegAddr);
	else
		wrPtr = pEncInfo->streamWrPtr;

	if (rdPtr < wrPtr) {
		if (rdPtr + size > wrPtr) {
			SetClockGate(pCodecInst->coreIdx, 0);
			return RETCODE_INVALID_PARAM;
		}
	}

	if (pEncInfo->ringBufferEnable == TRUE ||
	    pEncInfo->lineBufIntEn == TRUE) {
		rdPtr += size;
		if (rdPtr > pEncInfo->streamBufEndAddr) {
			if (pEncInfo->lineBufIntEn == TRUE) {
				return RETCODE_INVALID_PARAM;
			}
			room = rdPtr - pEncInfo->streamBufEndAddr;
			rdPtr = pEncInfo->streamBufStartAddr;
			rdPtr += room;
		}

		if (rdPtr == pEncInfo->streamBufEndAddr) {
			rdPtr = pEncInfo->streamBufStartAddr;
		}
	} else {
		rdPtr = pEncInfo->streamBufStartAddr;
	}

	pEncInfo->streamRdPtr = rdPtr;
	pEncInfo->streamWrPtr = wrPtr;
	if (GetPendingInst(pCodecInst->coreIdx) == pCodecInst)
		VpuWriteReg(pCodecInst->coreIdx, pEncInfo->streamRdPtrRegAddr,
			    rdPtr);

	if (pEncInfo->lineBufIntEn == TRUE) {
		pEncInfo->streamRdPtr = pEncInfo->streamBufStartAddr;
	}

	SetClockGate(pCodecInst->coreIdx, 0);
	return RETCODE_SUCCESS;
}

RetCode VPU_EncStartOneFrame(EncHandle handle, EncParam *param)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;
	VpuAttr *pAttr = NULL;
	vpu_instance_pool_t *vip;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;
	vip = (vpu_instance_pool_t *)vdi_get_instance_pool(pCodecInst->coreIdx);
	if (!vip) {
		return RETCODE_INVALID_HANDLE;
	}

	if (pEncInfo->stride ==
	    0) { // This means frame buffers have not been registered.
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	ret = CheckEncParam(handle, param);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];

	EnterLock(pCodecInst->coreIdx);

	pEncInfo->ptsMap[param->srcIdx] =
		(pEncInfo->openParam.enablePTS == TRUE) ? GetTimestamp(handle) :
							  param->pts;

	if (GetPendingInst(pCodecInst->coreIdx)) {
		if (VPU_GetOpenInstanceNum(pCodecInst->coreIdx) > 1) //RTK
		{
			VLOG(WARN, "In[%s][%d] usleep 50ms and try again\n",
			     __func__, __LINE__);
			msleep(50);
			if (GetPendingInst(pCodecInst->coreIdx)) {
				LeaveLock(pCodecInst->coreIdx);
				return RETCODE_FRAME_NOT_COMPLETE;
			}
		} else {
			LeaveLock(pCodecInst->coreIdx);
			return RETCODE_FRAME_NOT_COMPLETE;
		}
	}

	ret = ProductVpuEncode(pCodecInst, param);

	if (pAttr->supportCommandQueue == TRUE) {
		SetPendingInst(pCodecInst->coreIdx, NULL);
		LeaveLock(pCodecInst->coreIdx);
	} else {
		SetPendingInst(pCodecInst->coreIdx, pCodecInst);
	}

	return ret;
}

RetCode VPU_EncGetOutputInfo(EncHandle handle, EncOutputInfo *info)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;
	VpuAttr *pAttr;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	if (info == 0) {
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;
	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];

	if (pAttr->supportCommandQueue == TRUE) {
		EnterLock(pCodecInst->coreIdx);
	} else {
		if (pCodecInst != GetPendingInst(pCodecInst->coreIdx)) {
			SetPendingInst(pCodecInst->coreIdx, 0);
			LeaveLock(pCodecInst->coreIdx);
			return RETCODE_WRONG_CALL_SEQUENCE;
		}
	}

	ret = ProductVpuEncGetResult(pCodecInst, info);

	if (ret == RETCODE_SUCCESS) {
		info->pts = pEncInfo->ptsMap[info->encSrcIdx];
	} else {
		info->pts = 0LL;
	}

	SetPendingInst(pCodecInst->coreIdx, 0);
	LeaveLock(pCodecInst->coreIdx);

	return ret;
}

RetCode VPU_EncGiveCommand(EncHandle handle, CodecCommand cmd, void *param)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;
	switch (cmd) {
	case ENABLE_ROTATION: {
		pEncInfo->rotationEnable = 1;
	} break;
	case DISABLE_ROTATION: {
		pEncInfo->rotationEnable = 0;
	} break;
	case ENABLE_MIRRORING: {
		pEncInfo->mirrorEnable = 1;
	} break;
	case DISABLE_MIRRORING: {
		pEncInfo->mirrorEnable = 0;
	} break;
	case SET_MIRROR_DIRECTION: {
		MirrorDirection mirDir;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		mirDir = *(MirrorDirection *)param;
		if (!(mirDir == MIRDIR_NONE) && !(mirDir == MIRDIR_HOR) &&
		    !(mirDir == MIRDIR_VER) && !(mirDir == MIRDIR_HOR_VER)) {
			return RETCODE_INVALID_PARAM;
		}
		pEncInfo->mirrorDirection = mirDir;
	} break;
	case SET_ROTATION_ANGLE: {
		int angle;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		angle = *(int *)param;
		if (angle != 0 && angle != 90 && angle != 180 && angle != 270) {
			return RETCODE_INVALID_PARAM;
		}
		if (pEncInfo->initialInfoObtained &&
		    (angle == 90 || angle == 270)) {
			return RETCODE_INVALID_PARAM;
		}
		pEncInfo->rotationAngle = angle;
	} break;
	case SET_CACHE_CONFIG: {
		MaverickCacheConfig *mcCacheConfig;
		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		mcCacheConfig = (MaverickCacheConfig *)param;
		pEncInfo->cacheConfig = *mcCacheConfig;
	} break;
	case ENC_PUT_MP4_HEADER:
	case ENC_PUT_AVC_HEADER:
	case ENC_PUT_VIDEO_HEADER: {
		EncHeaderParam *encHeaderParam;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		encHeaderParam = (EncHeaderParam *)param;
		if (pCodecInst->codecMode == MP4_ENC ||
		    pCodecInst->codecMode == W_MP4_ENC) {
			if (!(VOL_HEADER <= encHeaderParam->headerType &&
			      encHeaderParam->headerType <= VIS_HEADER)) {
				return RETCODE_INVALID_PARAM;
			}
		} else if (pCodecInst->codecMode == AVC_ENC ||
			   pCodecInst->codecMode == W_AVC_ENC) {
			if (!(SPS_RBSP <= encHeaderParam->headerType &&
			      encHeaderParam->headerType <= PPS_RBSP_MVC)) {
				return RETCODE_INVALID_PARAM;
			}
		} else
			return RETCODE_INVALID_PARAM;

		if (pEncInfo->ringBufferEnable == 0) {
			if (encHeaderParam->buf % 8 ||
			    encHeaderParam->size == 0) {
				return RETCODE_INVALID_PARAM;
			}
		}

		return GetEncHeader(handle, encHeaderParam);
	}
	case ENC_SET_ACTIVE_PPS: {
		int ActivePPSIdx = (int)(*(int *)param);
		if (pCodecInst->codecMode != AVC_ENC &&
		    pCodecInst->codecMode != W_AVC_ENC)
			return RETCODE_INVALID_COMMAND;
		if (ActivePPSIdx < 0 ||
		    ActivePPSIdx >
			    pEncInfo->openParam.EncStdParam.avcParam.ppsNum)
			return RETCODE_INVALID_COMMAND;

		pEncInfo->ActivePPSIdx = ActivePPSIdx;
		return EncParaSet(handle, PPS_RBSP);
	} break;
	case ENC_GET_ACTIVE_PPS:
		if (pCodecInst->codecMode != AVC_ENC &&
		    pCodecInst->codecMode != W_AVC_ENC)
			return RETCODE_INVALID_COMMAND;
		*((int *)param) = pEncInfo->ActivePPSIdx;
		break;
	case ENC_SET_GOP_NUMBER: {
		int *pGopNumber = (int *)param;
		if (pCodecInst->codecMode != MP4_ENC &&
		    pCodecInst->codecMode != AVC_ENC &&
		    pCodecInst->codecMode != W_MP4_ENC &&
		    pCodecInst->codecMode != W_AVC_ENC) {
			return RETCODE_INVALID_COMMAND;
		}
		if (*pGopNumber < 0)
			return RETCODE_INVALID_PARAM;
		pEncInfo->openParam.gopSize = *pGopNumber;
		SetGopNumber(handle, (Uint32 *)pGopNumber);
	} break;
	case ENC_SET_INTRA_QP: {
		int *pIntraQp = (int *)param;
		if (pCodecInst->codecMode != MP4_ENC &&
		    pCodecInst->codecMode != AVC_ENC &&
		    pCodecInst->codecMode != W_MP4_ENC &&
		    pCodecInst->codecMode != W_AVC_ENC) {
			return RETCODE_INVALID_COMMAND;
		}
		if (pCodecInst->codecMode == MP4_ENC ||
		    pCodecInst->codecMode == W_MP4_ENC) {
			if (*pIntraQp < 1 || *pIntraQp > 31)
				return RETCODE_INVALID_PARAM;
		}
		if (pCodecInst->codecMode == AVC_ENC ||
		    pCodecInst->codecMode == W_AVC_ENC) {
			if (*pIntraQp < 0 || *pIntraQp > 51)
				return RETCODE_INVALID_PARAM;
		}
		SetIntraQp(handle, (Uint32 *)pIntraQp);
	} break;
	case ENC_SET_BITRATE: {
		int *pBitrate = (int *)param;
		if (pCodecInst->codecMode != MP4_ENC &&
		    pCodecInst->codecMode != AVC_ENC &&
		    pCodecInst->codecMode != W_MP4_ENC &&
		    pCodecInst->codecMode != W_AVC_ENC) {
			return RETCODE_INVALID_COMMAND;
		}
		if (pCodecInst->codecMode == AVC_ENC ||
		    pCodecInst->codecMode == W_AVC_ENC) {
			if (*pBitrate < 0 || *pBitrate > 524288) {
				return RETCODE_INVALID_PARAM;
			}

		} else // MP4_ENC
		{
			if (*pBitrate < 0 || *pBitrate > 32767) {
				return RETCODE_INVALID_PARAM;
			}
		}
		SetBitrate(handle, (Uint32 *)pBitrate);
	} break;
	case ENC_SET_FRAME_RATE: {
		int *pFramerate = (int *)param;

		if (pCodecInst->codecMode != MP4_ENC &&
		    pCodecInst->codecMode != AVC_ENC &&
		    pCodecInst->codecMode != W_MP4_ENC &&
		    pCodecInst->codecMode != W_AVC_ENC) {
			return RETCODE_INVALID_COMMAND;
		}
		if (*pFramerate <= 0) {
			return RETCODE_INVALID_PARAM;
		}
		SetFramerate(handle, (Uint32 *)pFramerate);
	} break;
	case ENC_SET_INTRA_MB_REFRESH_NUMBER: {
		int *pIntraRefreshNum = (int *)param;
		SetIntraRefreshNum(handle, (Uint32 *)pIntraRefreshNum);
	} break;
	case ENC_SET_SLICE_INFO: {
		EncSliceMode *pSliceMode = (EncSliceMode *)param;
		if (pSliceMode->sliceMode < 0 || pSliceMode->sliceMode > 1) {
			return RETCODE_INVALID_PARAM;
		}
		if (pSliceMode->sliceSizeMode < 0 ||
		    pSliceMode->sliceSizeMode > 1) {
			return RETCODE_INVALID_PARAM;
		}

		SetSliceMode(handle, (EncSliceMode *)pSliceMode);
	} break;
	case ENC_ENABLE_HEC: {
		if (pCodecInst->codecMode != MP4_ENC &&
		    pCodecInst->codecMode != W_MP4_ENC) {
			return RETCODE_INVALID_COMMAND;
		}
		SetHecMode(handle, 1);
	} break;
	case ENC_DISABLE_HEC: {
		if (pCodecInst->codecMode != MP4_ENC &&
		    pCodecInst->codecMode != W_MP4_ENC) {
			return RETCODE_INVALID_COMMAND;
		}
		SetHecMode(handle, 0);
	} break;
	case SET_SEC_AXI: {
		SecAxiUse secAxiUse;

		if (param == 0) {
			return RETCODE_INVALID_PARAM;
		}
		secAxiUse = *(SecAxiUse *)param;

		// coda9 or coda7q or ...
		pEncInfo->secAxiInfo.u.coda9.useBitEnable =
			secAxiUse.u.coda9.useBitEnable;
		pEncInfo->secAxiInfo.u.coda9.useIpEnable =
			secAxiUse.u.coda9.useIpEnable;
		pEncInfo->secAxiInfo.u.coda9.useDbkYEnable =
			secAxiUse.u.coda9.useDbkYEnable;
		pEncInfo->secAxiInfo.u.coda9.useDbkCEnable =
			secAxiUse.u.coda9.useDbkCEnable;
		pEncInfo->secAxiInfo.u.coda9.useOvlEnable =
			secAxiUse.u.coda9.useOvlEnable;
		pEncInfo->secAxiInfo.u.coda9.useBtpEnable =
			secAxiUse.u.coda9.useBtpEnable;
	} break;
	case GET_TILEDMAP_CONFIG: {
		TiledMapConfig *pMapCfg = (TiledMapConfig *)param;
		if (!pMapCfg) {
			return RETCODE_INVALID_PARAM;
		}
		*pMapCfg = pEncInfo->mapCfg;
		break;
	}
	case SET_DRAM_CONFIG: {
		DRAMConfig *cfg = (DRAMConfig *)param;

		if (!cfg) {
			return RETCODE_INVALID_PARAM;
		}

		pEncInfo->dramCfg = *cfg;
		break;
	}
	case GET_DRAM_CONFIG: {
		DRAMConfig *cfg = (DRAMConfig *)param;

		if (!cfg) {
			return RETCODE_INVALID_PARAM;
		}

		*cfg = pEncInfo->dramCfg;

		break;
	}
	case ENABLE_LOGGING: {
		pCodecInst->loggingEnable = 1;
	} break;
	case DISABLE_LOGGING: {
		pCodecInst->loggingEnable = 0;
	} break;
	case ENC_SET_PARA_CHANGE: {
		return RETCODE_INVALID_PARAM;
	} break;
	default:
		return RETCODE_INVALID_COMMAND;
	}
	return RETCODE_SUCCESS;
}

RetCode VPU_EncAllocateFrameBuffer(EncHandle handle, FrameBufferAllocInfo info,
				   FrameBuffer *frameBuffer)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;
	int gdiIndex;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;

	if (!frameBuffer) {
		return RETCODE_INVALID_PARAM;
	}
	if (info.num == 0 || info.num < 0) {
		return RETCODE_INVALID_PARAM;
	}
	if (info.stride == 0 || info.stride < 0) {
		return RETCODE_INVALID_PARAM;
	}
	if (info.height == 0 || info.height < 0) {
		return RETCODE_INVALID_PARAM;
	}

	if (info.type == FB_TYPE_PPU) {
		if (pEncInfo->numFrameBuffers == 0) {
			return RETCODE_WRONG_CALL_SEQUENCE;
		}
		pEncInfo->ppuAllocExt = frameBuffer[0].updateFbInfo;
		gdiIndex = pEncInfo->numFrameBuffers;
		ret = ProductVpuAllocateFramebuffer(
			pCodecInst, frameBuffer, (TiledMapType)info.mapType,
			(Int32)info.num, info.stride, info.height, info.format,
			info.cbcrInterleave, info.nv21, info.endian,
			&pEncInfo->vbPPU, gdiIndex,
			(FramebufferAllocType)info.type);
	} else if (info.type == FB_TYPE_CODEC) {
		gdiIndex = 0;
		pEncInfo->frameAllocExt = frameBuffer[0].updateFbInfo;
		ret = ProductVpuAllocateFramebuffer(
			pCodecInst, frameBuffer, (TiledMapType)info.mapType,
			(Int32)info.num, info.stride, info.height, info.format,
			info.cbcrInterleave, FALSE, info.endian,
			&pEncInfo->vbFrame, gdiIndex,
			(FramebufferAllocType)info.type);
	} else {
		ret = RETCODE_INVALID_PARAM;
	}

	return ret;
}

RetCode VPU_EncIssueSeqInit(EncHandle handle)
{
	CodecInst *pCodecInst;
	RetCode ret;
	VpuAttr *pAttr;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;

	EnterLock(pCodecInst->coreIdx);

	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];

	if (GetPendingInst(pCodecInst->coreIdx)) {
		LeaveLock(pCodecInst->coreIdx);
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	ret = ProductVpuEncInitSeq(handle);
	if (ret == RETCODE_SUCCESS) {
		SetPendingInst(pCodecInst->coreIdx, pCodecInst);
	}

	if (pAttr->supportCommandQueue == TRUE) {
		SetPendingInst(pCodecInst->coreIdx, NULL);
		LeaveLock(pCodecInst->coreIdx);
	}

	return ret;
}

RetCode VPU_EncCompleteSeqInit(EncHandle handle, EncInitialInfo *info)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;
	VpuAttr *pAttr;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	if (info == 0) {
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;

	pAttr = &g_VpuCoreAttributes[pCodecInst->coreIdx];

	if (pAttr->supportCommandQueue == TRUE) {
		EnterLock(pCodecInst->coreIdx);
	} else {
		if (pCodecInst != GetPendingInst(pCodecInst->coreIdx)) {
			SetPendingInst(pCodecInst->coreIdx, 0);
			LeaveLock(pCodecInst->coreIdx);
			return RETCODE_WRONG_CALL_SEQUENCE;
		}
	}

	ret = ProductVpuEncGetSeqInfo(handle, info);
	if (ret == RETCODE_SUCCESS) {
		pEncInfo->initialInfoObtained = 1;
	}

	pEncInfo->initialInfo = *info;

	SetPendingInst(pCodecInst->coreIdx, NULL);

	LeaveLock(pCodecInst->coreIdx);

	return ret;
}

RetCode VPU_DecGetRdPtr(DecHandle handle, PhysicalAddress *prdPtr)
{
	CodecInst *pCodecInst;
	CodecInst *pPendingInst;
	DecInfo *pDecInfo;
	RetCode ret;

	PhysicalAddress rdPtr;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}
	pCodecInst = (CodecInst *)handle;
	ret = ProductVpuDecCheckCapability(pCodecInst);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	pPendingInst = GetPendingInst(pCodecInst->coreIdx);
	if (pCodecInst == pPendingInst) {
		rdPtr = VpuReadReg(pCodecInst->coreIdx,
				   pDecInfo->streamRdPtrRegAddr);
	} else {
		EnterLock(pCodecInst->coreIdx);
		rdPtr = VpuReadReg(pCodecInst->coreIdx,
				   pDecInfo->streamRdPtrRegAddr);
		LeaveLock(pCodecInst->coreIdx);
	}

	if (prdPtr)
		*prdPtr = rdPtr;

	return RETCODE_SUCCESS;
}

RetCode VPU_DecSetWrPtr(DecHandle handle, PhysicalAddress addr, int updateRdPtr)
{
	CodecInst *pCodecInst;
	CodecInst *pPendingInst;
	DecInfo *pDecInfo;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;
	pCodecInst = (CodecInst *)handle;
	pDecInfo = &handle->CodecInfo->decInfo;
	pPendingInst = GetPendingInst(pCodecInst->coreIdx);
	if (pCodecInst == pPendingInst) {
		VpuWriteReg(pCodecInst->coreIdx, pDecInfo->streamWrPtrRegAddr,
			    addr);
	} else {
		EnterLock(pCodecInst->coreIdx);
		VpuWriteReg(pCodecInst->coreIdx, pDecInfo->streamWrPtrRegAddr,
			    addr);
		LeaveLock(pCodecInst->coreIdx);
	}
	pDecInfo->streamWrPtr = addr;
	if (updateRdPtr)
		pDecInfo->streamRdPtr = addr;

	VLOG(TRACE, "[%d]%s.h:0x%x.addr:0x%x.updateRdPtr:%d\n", __LINE__,
	     __func__, handle, addr, updateRdPtr);
	return RETCODE_SUCCESS;
}

RetCode VPU_DecGetBitstreamBufferNoHW(DecHandle handle, PhysicalAddress *prdPtr,
				      PhysicalAddress *pwrPtr, Uint32 *size)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	PhysicalAddress rdPtr;
	PhysicalAddress wrPtr;
	PhysicalAddress tempPtr;
	int room;
	Int32 coreIdx;
	VpuAttr *pAttr;

	coreIdx = handle->coreIdx;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	rdPtr = pDecInfo->streamRdPtr;
	wrPtr = pDecInfo->streamWrPtr;

	pAttr = &g_VpuCoreAttributes[coreIdx];

	tempPtr = rdPtr;

	if (pDecInfo->openParam.bitstreamMode != BS_MODE_PIC_END) {
		if (wrPtr < tempPtr) {
			room = tempPtr - wrPtr -
			       pAttr->bitstreamBufferMargin * 2;
		} else {
			room = (pDecInfo->streamBufEndAddr - wrPtr) +
			       (tempPtr - pDecInfo->streamBufStartAddr) -
			       pAttr->bitstreamBufferMargin * 2;
		}
		room--;
	} else {
		room = (pDecInfo->streamBufEndAddr - wrPtr);
	}

	if (prdPtr)
		*prdPtr = tempPtr;
	if (pwrPtr)
		*pwrPtr = wrPtr;
	if (size)
		*size = room;

	return RETCODE_SUCCESS;
}

RetCode VPU_DecUpdateBitstreamBufferNoHW(DecHandle handle, int size)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	PhysicalAddress wrPtr;
	PhysicalAddress rdPtr;
	RetCode ret;
	BOOL running;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	wrPtr = pDecInfo->streamWrPtr;

	running = FALSE;

	if (size > 0) {
		Uint32 room = 0;

		rdPtr = pDecInfo->streamRdPtr;

		if (wrPtr < rdPtr) {
			if (rdPtr <= wrPtr + size) {
				return RETCODE_INVALID_PARAM;
			}
		}

		wrPtr += size;

		if (wrPtr > pDecInfo->streamBufEndAddr) {
			room = wrPtr - pDecInfo->streamBufEndAddr;
			wrPtr = pDecInfo->streamBufStartAddr;
			wrPtr += room;
		} else if (wrPtr == pDecInfo->streamBufEndAddr) {
			wrPtr = pDecInfo->streamBufStartAddr;
		}

		pDecInfo->streamWrPtr = wrPtr;
		pDecInfo->streamRdPtr = rdPtr;

		if (running == TRUE) {
			VpuAttr *pAttr =
				&g_VpuCoreAttributes[pCodecInst->coreIdx];
			if (pAttr->supportCommandQueue == FALSE) {
				VpuWriteReg(pCodecInst->coreIdx,
					    pDecInfo->streamWrPtrRegAddr,
					    wrPtr);
			}
		}
	}

	ret = ProductVpuDecSetBitstreamFlag(pCodecInst, running, size);

	return ret;
}

RetCode RTK_VPU_InitWithBitcode(Uint32 coreIdx, BOOL protect, void *videc_dev)
{
	RetCode ret = RETCODE_SUCCESS;
	Uint8 *firmware = NULL;
	Uint32 totalRead = 0;
	(void)protect; //unused

	VLOG(TRACE, "[+] [%d]%s.coreIdx:%d.protect:%d\n", __LINE__, __func__,
	     coreIdx, protect);
	if (coreIdx >= MAX_NUM_VPU_CORE) {
		VLOG(ERR, "[-] [%d]%s.coreIdx:%d.ret:RETCODE_FAILURE\n",
		     __LINE__, __func__, coreIdx);
		return RETCODE_FAILURE;
	}

	if (coreIdx == 0) {
		firmware = (Uint8 *)osal_malloc(VPU_FIRMWARE_SIZE);
		VLOG(TRACE, "[%d]firmware:0x%px\n", __LINE__, firmware);
		VLOG(TRACE, "[%d]size:%d\n", __LINE__, VPU_FIRMWARE_SIZE);
		VLOG(TRACE, "[%d]bit_code:0x%lx.sizeof(bit_code):%d\n",
		     __LINE__, bit_code, sizeof(bit_code));
		if (firmware != NULL) {
			osal_memcpy((void *)firmware, bit_code,
				    sizeof(bit_code));
			totalRead = sizeof(bit_code) / 2;
			VLOG(TRACE, "[%d]totalRead:%d\n", __LINE__, totalRead);
		}
	}

	if (firmware != NULL) {
		ret = VPU_InitWithBitcode(coreIdx, (const Uint16 *)firmware,
					  totalRead, videc_dev);
		osal_free(firmware);
	}

	VLOG(TRACE, "[-] [%d]%s.coreIdx:%d.ret:%d\n", __LINE__, __func__,
	     coreIdx, ret);
	return ret;
}

RetCode RTK_VPU_InitWithBitcodeExt(Uint32 coreIdx, BOOL protect, void *sess,
				   void *rtk_sess, void *filp, void *videc_dev)
{
	RetCode ret = RETCODE_SUCCESS;
	Uint8 *firmware = NULL;
	Uint32 totalRead = 0;

	VLOG(TRACE, "[+] [%d]%s.coreIdx:%d.protect:%d.sess:%p.rtk_sess:%p\n",
	     __LINE__, __func__, coreIdx, protect, sess, rtk_sess);
	if (coreIdx >= MAX_NUM_VPU_CORE) {
		VLOG(TRACE, "[-] [%d]%s.coreIdx:%d.ret:RETCODE_FAILURE\n",
		     __LINE__, __func__, coreIdx);
		return RETCODE_FAILURE;
	}

	if (coreIdx == 0) {
		firmware = (Uint8 *)osal_malloc(VPU_FIRMWARE_SIZE);
		if (firmware != NULL) {
			osal_memcpy((void *)firmware, bit_code,
				    sizeof(bit_code));
			totalRead = sizeof(bit_code) / 2;
		}
	}

	if (firmware != NULL) {
#ifdef ENABLE_TEE_DRM_FLOW
		if (protect == TRUE)
			ret = VPU_InitWithBitcodeProtect(
				coreIdx, (const Uint16 *)firmware, totalRead,
				sess, rtk_sess, filp);
		else
			ret = VPU_InitWithBitcode(
				coreIdx, (const Uint16 *)firmware, totalRead, videc_dev);
#else
		ret = VPU_InitWithBitcode(coreIdx, (const Uint16 *)firmware,
					  totalRead, videc_dev);
#endif
		osal_free(firmware);
	}

	VLOG(TRACE, "[-] [%d]%s.coreIdx:%d.ret:%d\n", __LINE__, __func__,
	     coreIdx, ret);
	return ret;
}

RetCode VPU_DBG_DUMP_SDATA(DecHandle handle, unsigned int phy_addr,
			   unsigned char *dst_buf, int dst_buf_size)
{
	RetCode ret = RETCODE_SUCCESS;
#ifdef ENABLE_TEE_DRM_FLOW
	int taRet;
	CodecInst *pCodecInst;
	vpu_buffer_t *vb = NULL;

	VLOG(TRACE,
	     "[+] [%d]%s.handle:0x%px.phy_addr:0x%x.dst_buf:0x%px.dst_buf_size:%d\n",
	     __LINE__, __func__, handle, phy_addr, dst_buf, dst_buf_size);

	if (handle == NULL || phy_addr == 0 || dst_buf == NULL ||
	    dst_buf_size <= 0) {
		VLOG(ERR,
		     "[-] [%d]%s.invalid parameters.handle:0x%px.phy_addr:0x%x.dst_buf:0x%px.dst_buf_size:%d\n",
		     __LINE__, __func__, handle, phy_addr, dst_buf,
		     dst_buf_size);
		return RETCODE_INVALID_PARAM;
	}
	pCodecInst = handle;

	vb = kzalloc(sizeof(DecOpenParam), GFP_KERNEL);
	if (!vb) {
		VLOG(ERR, "[%d]%s.handle:0x%px.kzalloc vb fail\n", __LINE__,
		     __func__, handle);
		goto exit;
	}

	vb->size = dst_buf_size;
	vb->req_spec_region = VE_SECURE_PROTECTION;
	if (vdi_allocate_dma_memory_no_mmap(pCodecInst->coreIdx, (void *)vb,
					    pCodecInst->filp) < 0) {
		VLOG(ERR,
		     "[%d]%s.handle:0x%px.vdi_allocate_dma_memory_no_mmap() fail\n",
		     __LINE__, __func__, handle);
		ret = RETCODE_INSUFFICIENT_RESOURCE;
		goto free_vb;
	}

	taRet = ta_TEEapi_memcpy((struct tee_context *)pCodecInst->teeapi_ctx,
				 pCodecInst->teeapi_tee_session, vb->phys_addr,
				 phy_addr, dst_buf_size);
	if (taRet < 0) {
		VLOG(ERR, "[%d]%s.ta_TEEapi_memcpy() fail.ret:%d\n", __LINE__,
		     __func__, taRet);
		ret = RETCODE_FAILURE;
		goto free_vb_ion;
	}

	taRet = ta_TEEapi_bitstreamprint(
		(struct tee_context *)pCodecInst->teeapi_ctx,
		pCodecInst->teeapi_tee_session, vb->phys_addr, dst_buf_size);
	if (taRet < 0) {
		VLOG(ERR, "[%d]%s.ta_TEEapi_bitstreamprint() fail.ret:%d\n",
		     __LINE__, __func__, taRet);
		ret = RETCODE_FAILURE;
		goto free_vb_ion;
	}

	taRet = ta_TEEapi_bitstreamout(
		(struct tee_context *)pCodecInst->teeapi_ctx,
		pCodecInst->teeapi_tee_session, vb->phys_addr, dst_buf,
		dst_buf_size);
	if (taRet < 0) {
		VLOG(ERR, "[%d]%s.ta_TEEapi_bitstreamout() fail.ret:%d\n",
		     __LINE__, __func__, taRet);
		ret = RETCODE_FAILURE;
		goto free_vb_ion;
	}

free_vb_ion:
	vdi_free_dma_memory_no_mmap(pCodecInst->coreIdx, vb);
free_vb:
	kfree(vb);
exit:
	VLOG(TRACE, "[-] [%d]%s.ret:%d\n", __LINE__, __func__, ret);
#else
	VLOG(ERR, "[%d]%s.unsupport ifndef ENABLE_TEE_DRM_FLOW\n", __LINE__,
	     __func__);
	ret = RETCODE_FAILURE;
#endif // #ifdef ENABLE_TEE_DRM_FLOW
	return ret;
}