//--=========================================================================--
//  This file is a part of VPU Reference API project
//-----------------------------------------------------------------------------
//
//  This confidential and proprietary software may be used only
//  as authorized by a licensing agreement from Chips&Media Inc.
//  In the event of publication, the following notice is applicable:
//
//            (C) COPYRIGHT 2006 - 2013  CHIPS&MEDIA INC.
//                      ALL RIGHTS RESERVED
//
//   The entire notice above must be reproduced on all authorized copies.
//
//--=========================================================================--
#include "ve1_product.h"
#include "ve1_vpu.h"

VpuAttr g_VpuCoreAttributes[MAX_NUM_VPU_CORE];

static Int32 s_ProductIds[MAX_NUM_VPU_CORE] = {
	PRODUCT_ID_NONE,
};

typedef struct FrameBufInfoStruct {
	Uint32 unitSizeHorLuma;
	Uint32 sizeLuma;
	Uint32 sizeChroma;
	BOOL fieldMap;
} FrameBufInfo;

#ifdef ENABLE_TEE_DRM_FLOW
RetCode ProductVpuInitProtect(Uint32 coreIdx, void *firmware, Uint32 size,
			      void *sess, void *rtk_sess, void *filp) //[r]
{
	RetCode ret = RETCODE_SUCCESS;
	int productId;

	productId = s_ProductIds[coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuInitProtect(coreIdx, firmware, size, sess,
					  rtk_sess, filp);
		break;
	default:
		ret = RETCODE_NOT_FOUND_VPU_DEVICE;
	}
	return ret;
}
#endif

Uint32 ProductVpuScan(Uint32 coreIdx)
{
	Uint32 foundProducts = 0;

	/* Already scanned */
	if (s_ProductIds[0] != PRODUCT_ID_NONE)
		return 1;

	s_ProductIds[0] = PRODUCT_ID_980;
	foundProducts = MAX_NUM_VPU_CORE;

	return (foundProducts == MAX_NUM_VPU_CORE);
}

Int32 ProductVpuGetId(Uint32 coreIdx)
{
	return s_ProductIds[coreIdx];
}

RetCode ProductVpuGetVersion(Uint32 coreIdx, Uint32 *versionInfo,
			     Uint32 *revision)
{
	Int32 productId = s_ProductIds[coreIdx];
	RetCode ret = RETCODE_SUCCESS;

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuGetVersion(coreIdx, versionInfo, revision);
		break;
	default:
		ret = RETCODE_NOT_FOUND_VPU_DEVICE;
	}

	return ret;
}

RetCode ProductVpuInit(Uint32 coreIdx, void *firmware, Uint32 size)
{
	RetCode ret = RETCODE_SUCCESS;
	int productId;

	productId = s_ProductIds[coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuInit(coreIdx, firmware, size);
		break;
	default:
		ret = RETCODE_NOT_FOUND_VPU_DEVICE;
	}

	return ret;
}

RetCode ProductVpuReInit(Uint32 coreIdx, void *firmware, Uint32 size)
{
	RetCode ret = RETCODE_SUCCESS;
	int productId;

	productId = s_ProductIds[coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuReInit(coreIdx, firmware, size);
		break;
	default:
		ret = RETCODE_NOT_FOUND_VPU_DEVICE;
	}

	return ret;
}

Uint32 ProductVpuIsInit(Uint32 coreIdx)
{
	Uint32 pc = 0;
	int productId;

	productId = s_ProductIds[coreIdx];

	if (productId == PRODUCT_ID_NONE) {
		ProductVpuScan(coreIdx);
		productId = s_ProductIds[coreIdx];
	}

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		pc = Coda9VpuIsInit(coreIdx);
		break;
	}

	return pc;
}

Int32 ProductVpuIsBusy(Uint32 coreIdx)
{
	Int32 busy;
	int productId;

	productId = s_ProductIds[coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		busy = Coda9VpuIsBusy(coreIdx);
		break;
	default:
		busy = 0;
		break;
	}

	return busy;
}

Int32 ProductVpuWaitInterrupt(CodecInst *instance, Int32 timeout)
{
	int productId;
	int flag = -1;

	productId = s_ProductIds[instance->coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		flag = Coda9VpuWaitInterrupt(instance, timeout);
		break;
	default:
		flag = -1;
		break;
	}

	return flag;
}

RetCode ProductVpuReset(Uint32 coreIdx, SWResetMode resetMode)
{
	int productId;
	RetCode ret = RETCODE_SUCCESS;

	productId = s_ProductIds[coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuReset(coreIdx, resetMode);
		break;
	default:
		ret = RETCODE_NOT_FOUND_VPU_DEVICE;
		break;
	}

	return ret;
}

RetCode ProductVpuSleepWake(Uint32 coreIdx, int iSleepWake, const Uint16 *code,
			    Uint32 size)
{
	int productId;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	productId = s_ProductIds[coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuSleepWake(coreIdx, iSleepWake, (void *)code,
					size);
		break;
	}

	return ret;
}

RetCode ProductVpuClearInterrupt(Uint32 coreIdx, Uint32 flags)
{
	int productId;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	productId = s_ProductIds[coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuClearInterrupt(coreIdx);
		break;
	}

	return ret;
}

Uint32 ProductVpuGetProductId(Uint32 coreIdx) // [r]
{
	Uint32 pc = 0;
	int productId;

	productId = s_ProductIds[coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		pc = Coda9VpuGetProductId(coreIdx);
		break;
	}

	return pc;
}

RetCode ProductVpuDecBuildUpOpenParam(CodecInst *pCodec, DecOpenParam *param)
{
	Int32 productId;
	Uint32 coreIdx;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	coreIdx = pCodec->coreIdx;
	productId = s_ProductIds[coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuBuildUpDecParam(pCodec, param);
		break;
	}

	return ret;
}

PhysicalAddress ProductVpuDecGetRdPtr(CodecInst *instance) //[r]
{
	Int32 productId;
	Uint32 coreIdx;
	PhysicalAddress retRdPtr;
	DecInfo *pDecInfo;

	pDecInfo = VPU_HANDLE_TO_DECINFO(instance);

	coreIdx = instance->coreIdx;
	productId = s_ProductIds[coreIdx];

	switch (productId) {
	default:
		retRdPtr = VpuReadReg(coreIdx, pDecInfo->streamRdPtrRegAddr);
		break;
	}

	return retRdPtr;
}

RetCode ProductVpuEncBuildUpOpenParam(CodecInst *pCodec, EncOpenParam *param)
{
	Int32 productId;
	Uint32 coreIdx;
	RetCode ret = RETCODE_NOT_SUPPORTED_FEATURE;

	coreIdx = pCodec->coreIdx;
	productId = s_ProductIds[coreIdx];

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuBuildUpEncParam(pCodec, param);
		break;
	default:
		ret = RETCODE_NOT_SUPPORTED_FEATURE;
	}

	return ret;
}

RetCode ProductCheckDecOpenParam(DecOpenParam *param)
{
	Int32 productId;
	Uint32 coreIdx;
	VpuAttr *pAttr;

	if (param == 0)
		return RETCODE_INVALID_PARAM;

	if (param->coreIdx >= MAX_NUM_VPU_CORE)
		return RETCODE_INVALID_PARAM;

	coreIdx = param->coreIdx;
	productId = s_ProductIds[coreIdx];
	VLOG(TRACE, "[%d]%s.productId:0x%x.\n", __LINE__, __func__, productId);
	pAttr = &g_VpuCoreAttributes[coreIdx];

	if (param->bitstreamBuffer % 8) {
		VLOG(TRACE, "[%d]%s\n", __LINE__, __func__);
		return RETCODE_INVALID_PARAM;
	}

	if (param->bitstreamMode == BS_MODE_INTERRUPT) {
		if (param->bitstreamBufferSize % 1024 ||
		    param->bitstreamBufferSize < 1024) {
			VLOG(TRACE, "[%d]%s\n", __LINE__, __func__);
			return RETCODE_INVALID_PARAM;
		}
	}

	if (PRODUCT_ID_W_SERIES(productId)) {
		if (param->virtAxiID > 16) {
			// Maximum number of AXI channels is 15
			return RETCODE_INVALID_PARAM;
		}
	}

	// Check bitstream mode
	if ((pAttr->supportBitstreamMode & (1 << param->bitstreamMode)) == 0) {
		VLOG(TRACE, "[%d]%s\n", __LINE__, __func__);
		return RETCODE_INVALID_PARAM;
	}

	if ((pAttr->supportDecoders & (1 << param->bitstreamFormat)) == 0) {
		VLOG(TRACE, "[%d]%s\n", __LINE__, __func__);
		return RETCODE_INVALID_PARAM;
	}

	/* check framebuffer endian */
	if ((pAttr->supportEndianMask & (1 << param->frameEndian)) == 0) {
		APIDPRINT("%s:%d Invalid frame endian(%d)\n",
			  (Int32)param->frameEndian);
		VLOG(TRACE, "[%d]%s\n", __LINE__, __func__);
		return RETCODE_INVALID_PARAM;
	}

	/* check streambuffer endian */
	if ((pAttr->supportEndianMask & (1 << param->streamEndian)) == 0) {
		APIDPRINT("%s:%d Invalid stream endian(%d)\n",
			  (Int32)param->streamEndian);
		VLOG(TRACE, "[%d]%s\n", __LINE__, __func__);
		return RETCODE_INVALID_PARAM;
	}

	/* check WTL */
	if (param->wtlEnable) {
		if (pAttr->supportWTL == 0)
			return RETCODE_NOT_SUPPORTED_FEATURE;
		switch (productId) {
		case PRODUCT_ID_960:
		case PRODUCT_ID_980:
			if (param->wtlMode != FF_FRAME &&
			    param->wtlMode != FF_FIELD)
				return RETCODE_INVALID_PARAM;
		default:
			break;
		}
	}

	/* Tiled2Linear */
	if (param->tiled2LinearEnable) {
		if (pAttr->supportTiled2Linear == 0)
			return RETCODE_NOT_SUPPORTED_FEATURE;

		if (productId == PRODUCT_ID_960 ||
		    productId == PRODUCT_ID_980) {
			if (param->tiled2LinearMode != FF_FRAME &&
			    param->tiled2LinearMode != FF_FIELD) {
				APIDPRINT(
					"%s:%d Invalid Tiled2LinearMode(%d)\n",
					(Int32)param->tiled2LinearMode);
				return RETCODE_INVALID_PARAM;
			}
		}
	}
	if (productId == PRODUCT_ID_960 || productId == PRODUCT_ID_980) {
		if (param->mp4DeblkEnable == 1 &&
		    !(param->bitstreamFormat == STD_MPEG4 ||
		      param->bitstreamFormat == STD_H263 ||
		      param->bitstreamFormat == STD_MPEG2 ||
		      param->bitstreamFormat == STD_UNKNOWN3)) {
			VLOG(TRACE, "[%d]%s\n", __LINE__, __func__);
			return RETCODE_INVALID_PARAM;
		}
		if (param->wtlEnable && param->tiled2LinearEnable) {
			VLOG(TRACE, "[%d]%s\n", __LINE__, __func__);
			return RETCODE_INVALID_PARAM;
		}
	} else {
		if (param->mp4DeblkEnable || param->mp4Class)
			return RETCODE_INVALID_PARAM;
		if (param->avcExtension)
			return RETCODE_INVALID_PARAM;
		if (param->tiled2LinearMode != FF_NONE)
			return RETCODE_INVALID_PARAM;
	}

	return RETCODE_SUCCESS;
}

RetCode ProductVpuDecInitSeq(CodecInst *instance)
{
	int productId;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	productId = instance->productId;

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuDecInitSeq(instance);
		break;
	}

	return ret;
}

RetCode ProductVpuDecFiniSeq(CodecInst *instance)
{
	int productId;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	productId = instance->productId;

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuFiniSeq(instance);
		break;
	}

	return ret;
}

RetCode ProductVpuDecGetSeqInfo(CodecInst *instance, DecInitialInfo *info)
{
	int productId;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	productId = instance->productId;

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuDecGetSeqInfo(instance, info);
		break;
	}

	return ret;
}

RetCode ProductVpuDecCheckCapability(CodecInst *instance)
{
	DecInfo *pDecInfo;
	VpuAttr *pAttr = &g_VpuCoreAttributes[instance->coreIdx];

	pDecInfo = &instance->CodecInfo->decInfo;

	if ((pAttr->supportDecoders &
	     (1 << pDecInfo->openParam.bitstreamFormat)) == 0)
		return RETCODE_NOT_SUPPORTED_FEATURE;

	switch (instance->productId) {
	case PRODUCT_ID_960:
		if (pDecInfo->mapType >= TILED_FRAME_NO_BANK_MAP)
			return RETCODE_NOT_SUPPORTED_FEATURE;
		if (pDecInfo->tiled2LinearMode == FF_FIELD)
			return RETCODE_NOT_SUPPORTED_FEATURE;
		break;
	case PRODUCT_ID_980:
		if (pDecInfo->mapType >= COMPRESSED_FRAME_MAP)
			return RETCODE_NOT_SUPPORTED_FEATURE;
		break;
	}

	return RETCODE_SUCCESS;
}

RetCode ProductVpuDecode(CodecInst *instance, DecParam *option)
{
	int productId;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	productId = instance->productId;

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuDecode(instance, option);
		break;
	}

	return ret;
}

RetCode ProductVpuDecGetResult(CodecInst *instance, DecOutputInfo *result)
{
	int productId;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	productId = instance->productId;

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuDecGetResult(instance, result);
		break;
	}

	return ret;
}

RetCode ProductVpuDecFlush(CodecInst *instance, FramebufferIndex *retIndexes,
			   Uint32 size)
{
	RetCode ret = RETCODE_SUCCESS;

	switch (instance->productId) {
	default:
		ret = Coda9VpuDecFlush(instance, retIndexes, size);
		break;
	}

	return ret;
}

/************************************************************************/
/* Decoder & Encoder                                                    */
/************************************************************************/

RetCode ProductVpuDecSetBitstreamFlag(CodecInst *instance, BOOL running,
				      Int32 size)
{
	int productId;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;
	BOOL eos;
	BOOL checkEos;
	BOOL explicitEnd;
	DecInfo *pDecInfo = &instance->CodecInfo->decInfo;

	productId = instance->productId;

	eos = (BOOL)(size == 0);
	checkEos = (BOOL)(size > 0);
	explicitEnd = (BOOL)(size == -2);

	switch (productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		if (checkEos ||
		    explicitEnd /* explicitEnd alst need to check the old value of streamEndflag*/) // gregory, for RTPIC-109(CHTMOD-196)
			eos = (BOOL)((pDecInfo->streamEndflag & 0x04) == 0x04);
		ret = Coda9VpuDecSetBitstreamFlag(instance, running, eos);
		break;
	}

	return ret;
}

RetCode ProductCpbFlush(CodecInst *instance)
{
	RetCode ret;

	switch (instance->productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuDecCpbFlush(instance);
		break;
	default:
		ret = RETCODE_NOT_SUPPORTED_FEATURE;
		break;
	}

	return ret;
}

/**
 * \param   stride          stride of framebuffer in pixel.
 */
RetCode ProductVpuAllocateFramebuffer(
	CodecInst *inst, FrameBuffer *fbArr, TiledMapType mapType, Int32 num,
	Int32 stride, Int32 height, FrameBufferFormat format,
	BOOL cbcrInterleave, BOOL nv21, Int32 endian, vpu_buffer_t *vb,
	Int32 gdiIndex, FramebufferAllocType fbType)
{
	Int32 i;
	Uint32 coreIdx;
	vpu_buffer_t vbFrame;
	FrameBufInfo fbInfo;
	DecInfo *pDecInfo = &inst->CodecInfo->decInfo;
	EncInfo *pEncInfo = &inst->CodecInfo->encInfo;
	// Variables for TILED_FRAME/FILED_MB_RASTER
	Uint32 sizeLuma;
	Uint32 sizeChroma;
	ProductId productId = (ProductId)inst->productId;
	RetCode ret = RETCODE_SUCCESS;

	osal_memset((void *)&vbFrame, 0x00, sizeof(vpu_buffer_t));
	osal_memset((void *)&fbInfo, 0x00, sizeof(FrameBufInfo));

	coreIdx = inst->coreIdx;

	if (inst->codecMode == AVC_ENC)
		format = pEncInfo->openParam.EncStdParam.avcParam
					 .chromaFormat400 ?
				 FORMAT_400 :
				 FORMAT_420;
	if (inst->codecMode == W_VP9_DEC) {
		Uint32 framebufHeight = VPU_ALIGN64(height);
		sizeLuma = CalcLumaSize(inst->productId, stride, framebufHeight,
					format, cbcrInterleave, mapType, NULL);
		sizeChroma =
			CalcChromaSize(inst->productId, stride, framebufHeight,
				       format, cbcrInterleave, mapType, NULL);
	} else {
		DRAMConfig *dramConfig = NULL;
		if (productId == PRODUCT_ID_960) {
			dramConfig = &pDecInfo->dramCfg;
			dramConfig = (inst->isDecoder == TRUE) ?
					     &pDecInfo->dramCfg :
					     &pEncInfo->dramCfg;
		}
		sizeLuma = CalcLumaSize(inst->productId, stride, height, format,
					cbcrInterleave, mapType, dramConfig);
		sizeChroma =
			CalcChromaSize(inst->productId, stride, height, format,
				       cbcrInterleave, mapType, dramConfig);
	}

	// Framebuffer common informations
	for (i = 0; i < num; i++) {
		if (fbArr[i].updateFbInfo == TRUE) {
			fbArr[i].updateFbInfo = FALSE;
			fbArr[i].myIndex = i + gdiIndex;
			fbArr[i].stride = stride;
			fbArr[i].height = height;
			fbArr[i].mapType = mapType;
			fbArr[i].format = format;
			fbArr[i].cbcrInterleave =
				(mapType == COMPRESSED_FRAME_MAP ?
					 TRUE :
					 cbcrInterleave);
			fbArr[i].nv21 = nv21;
			fbArr[i].endian = endian;
			fbArr[i].lumaBitDepth =
				pDecInfo->initialInfo.lumaBitdepth;
			fbArr[i].chromaBitDepth =
				pDecInfo->initialInfo.chromaBitdepth;
			fbArr[i].sourceLBurstEn = FALSE;
		}
	}

	switch (mapType) {
	case LINEAR_FRAME_MAP:
	case LINEAR_FIELD_MAP:
	case COMPRESSED_FRAME_MAP:
	case ARM_COMPRESSED_FRAME_MAP:
		ret = AllocateLinearFrameBuffer(mapType, fbArr, num, sizeLuma,
						sizeChroma);
		break;

	default:
		/* Tiled map */
		if (productId == PRODUCT_ID_960) {
			DRAMConfig *pDramCfg;
			PhysicalAddress tiledBaseAddr = 0;
			TiledMapConfig *pMapCfg;

			pDramCfg = (inst->isDecoder == TRUE) ?
					   &pDecInfo->dramCfg :
					   &pEncInfo->dramCfg;
			pMapCfg = (inst->isDecoder == TRUE) ?
					  &pDecInfo->mapCfg :
					  &pEncInfo->mapCfg;
			vbFrame.phys_addr =
				GetTiledFrameBase(coreIdx, fbArr, num);
			if (fbType == FB_TYPE_PPU) {
				tiledBaseAddr = pMapCfg->tiledBaseAddr;
			} else {
				pMapCfg->tiledBaseAddr = vbFrame.phys_addr;
				tiledBaseAddr = vbFrame.phys_addr;
			}
			*vb = vbFrame;
			ret = AllocateTiledFrameBufferGdiV1(
				mapType, tiledBaseAddr, fbArr, num, sizeLuma,
				sizeChroma, pDramCfg);
		} else {
			// PRODUCT_ID_980
			ret = AllocateTiledFrameBufferGdiV2(
				mapType, fbArr, num, sizeLuma, sizeChroma);
		}
		break;
	}

	return ret;
}

RetCode ProductVpuRegisterFramebuffer(CodecInst *instance)
{
	RetCode ret = RETCODE_FAILURE;

	switch (instance->productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		if (IS_DECODER_HANDLE(instance))
			ret = Coda9VpuDecRegisterFramebuffer(instance);
		else
			ret = Coda9VpuEncRegisterFramebuffer(instance);
		break;
	}
	return ret;
}

RetCode ProductVpuDecUpdateFrameBuffer(CodecInst *instance, FrameBuffer *fbcFb,
				       FrameBuffer *linearFb, Uint32 mvColIndex,
				       Uint32 picWidth, Uint32 picHeight)
{
	RetCode ret = RETCODE_NOT_SUPPORTED_FEATURE;

	return ret;
}

Int32 ProductCalculateFrameBufSize(Int32 productId, Int32 stride, Int32 height,
				   TiledMapType mapType,
				   FrameBufferFormat format, BOOL interleave,
				   DRAMConfig *pDramCfg)
{
	Int32 size_dpb_lum, size_dpb_chr, size_dpb_all;

	size_dpb_lum = CalcLumaSize(productId, stride, height, format,
				    interleave, mapType, pDramCfg);
	size_dpb_chr = CalcChromaSize(productId, stride, height, format,
				      interleave, mapType, pDramCfg);
	size_dpb_all = size_dpb_lum + size_dpb_chr * 2;

	return size_dpb_all;
}

Int32 ProductCalculateAuxBufferSize(AUX_BUF_TYPE type, CodStd codStd,
				    Int32 width, Int32 height)
{
	Int32 size = 0;

	switch (type) {
	case AUX_BUF_TYPE_MVCOL:
		if (codStd == STD_AVC || codStd == STD_VC1 ||
		    codStd == STD_MPEG4 || codStd == STD_H263 ||
		    codStd == STD_RV || codStd == STD_AVS) {
			size = VPU_ALIGN32(width) * VPU_ALIGN32(height);
			size = (size * 3) / 2;
			size = (size + 4) / 5;
			size = ((size + 7) / 8) * 8;
		} else {
			size = 0;
		}
		break;
	case AUX_BUF_TYPE_FBC_Y_OFFSET:
	case AUX_BUF_TYPE_FBC_C_OFFSET:
	default:
		break;
	}

	return size;
}

/************************************************************************/
/* ENCODER                                                              */
/************************************************************************/
RetCode ProductCheckEncOpenParam(EncOpenParam *pop)
{
	Int32 coreIdx;
	Int32 picWidth;
	Int32 picHeight;
	Int32 productId;
	VpuAttr *pAttr;

	if (pop == 0)
		return RETCODE_INVALID_PARAM;

	if (pop->coreIdx >= MAX_NUM_VPU_CORE)
		return RETCODE_INVALID_PARAM;

	coreIdx = pop->coreIdx;
	picWidth = pop->picWidth;
	picHeight = pop->picHeight;
	productId = s_ProductIds[coreIdx];
	pAttr = &g_VpuCoreAttributes[coreIdx];

	if ((pAttr->supportEncoders & (1 << pop->bitstreamFormat)) == 0)
		return RETCODE_NOT_SUPPORTED_FEATURE;

	if (pop->ringBufferEnable == TRUE) {
		if (pop->bitstreamBuffer % 8) {
			return RETCODE_INVALID_PARAM;
		}

		if (pop->bitstreamBufferSize % 1024 ||
		    pop->bitstreamBufferSize < 1024)
			return RETCODE_INVALID_PARAM;
	}

	if (pop->frameRateInfo == 0)
		return RETCODE_INVALID_PARAM;

	if (pop->bitstreamFormat == STD_AVC) {
		if (productId == PRODUCT_ID_980) {
			if (pop->bitRate > 524288 || pop->bitRate < 0)
				return RETCODE_INVALID_PARAM;
		}
	} else {
		if (pop->bitRate > 32767 || pop->bitRate < 0)
			return RETCODE_INVALID_PARAM;
	}

	if (pop->bitRate != 0 && pop->initialDelay > 32767)
		return RETCODE_INVALID_PARAM;

	if (pop->bitRate != 0 && pop->initialDelay != 0 &&
	    pop->vbvBufferSize < 0)
		return RETCODE_INVALID_PARAM;

	if (pop->frameSkipDisable != 0 && pop->frameSkipDisable != 1)
		return RETCODE_INVALID_PARAM;

	if (pop->sliceMode.sliceMode != 0 && pop->sliceMode.sliceMode != 1)
		return RETCODE_INVALID_PARAM;

	if (pop->sliceMode.sliceMode == 1) {
		if (pop->sliceMode.sliceSizeMode != 0 &&
		    pop->sliceMode.sliceSizeMode != 1) {
			return RETCODE_INVALID_PARAM;
		}
		if (pop->sliceMode.sliceSizeMode == 1 &&
		    pop->sliceMode.sliceSize == 0) {
			return RETCODE_INVALID_PARAM;
		}
	}

	if (pop->intraRefresh < 0)
		return RETCODE_INVALID_PARAM;

	if (pop->MEUseZeroPmv != 0 && pop->MEUseZeroPmv != 1)
		return RETCODE_INVALID_PARAM;

	if (pop->intraCostWeight < 0 || pop->intraCostWeight >= 65535)
		return RETCODE_INVALID_PARAM;

	if (productId == PRODUCT_ID_980) {
		if (pop->MESearchRangeX < 0 || pop->MESearchRangeX > 4) {
			return RETCODE_INVALID_PARAM;
		}
		if (pop->MESearchRangeY < 0 || pop->MESearchRangeY > 3) {
			return RETCODE_INVALID_PARAM;
		}
	} else {
		if (pop->MESearchRange < 0 || pop->MESearchRange >= 4)
			return RETCODE_INVALID_PARAM;
	}

	if (pop->bitstreamFormat == STD_MPEG4) {
		EncMp4Param *param = &pop->EncStdParam.mp4Param;
		if (param->mp4DataPartitionEnable != 0 &&
		    param->mp4DataPartitionEnable != 1) {
			return RETCODE_INVALID_PARAM;
		}
		if (param->mp4DataPartitionEnable == 1) {
			if (param->mp4ReversibleVlcEnable != 0 &&
			    param->mp4ReversibleVlcEnable != 1) {
				return RETCODE_INVALID_PARAM;
			}
		}
		if (param->mp4IntraDcVlcThr < 0 ||
		    7 < param->mp4IntraDcVlcThr) {
			return RETCODE_INVALID_PARAM;
		}

		if (picWidth < MIN_ENC_PIC_WIDTH ||
		    picWidth > MAX_ENC_PIC_WIDTH) {
			return RETCODE_INVALID_PARAM;
		}

		if (picHeight < MIN_ENC_PIC_HEIGHT) {
			return RETCODE_INVALID_PARAM;
		}
	} else if (pop->bitstreamFormat == STD_H263) {
		EncH263Param *param = &pop->EncStdParam.h263Param;
#ifdef H263_FRAME_RATE_LIMIT_CLEAR
#else
		Uint32 frameRateInc, frameRateRes;
#endif

		if (param->h263AnnexJEnable != 0 &&
		    param->h263AnnexJEnable != 1) {
			return RETCODE_INVALID_PARAM;
		}
		if (param->h263AnnexKEnable != 0 &&
		    param->h263AnnexKEnable != 1) {
			return RETCODE_INVALID_PARAM;
		}
		if (param->h263AnnexTEnable != 0 &&
		    param->h263AnnexTEnable != 1) {
			return RETCODE_INVALID_PARAM;
		}

		if (picWidth < MIN_ENC_PIC_WIDTH ||
		    picWidth > MAX_ENC_PIC_WIDTH) {
			return RETCODE_INVALID_PARAM;
		}
		if (picHeight < MIN_ENC_PIC_HEIGHT) {
			return RETCODE_INVALID_PARAM;
		}

#ifdef H263_FRAME_RATE_LIMIT_CLEAR
#else
		frameRateInc = ((pop->frameRateInfo >> 16) & 0xFFFF) + 1;
		frameRateRes = pop->frameRateInfo & 0xFFFF;

		if ((frameRateRes / frameRateInc) < 15) {
			return RETCODE_INVALID_PARAM;
		}
#endif
	} else if (pop->bitstreamFormat == STD_AVC) {
		EncAvcParam *param = &pop->EncStdParam.avcParam;

		AvcPpsParam *ActivePPS = NULL;

		if (productId == PRODUCT_ID_980)
			ActivePPS = &param->ppsParam[0];

		if (param->constrainedIntraPredFlag != 0 &&
		    param->constrainedIntraPredFlag != 1)
			return RETCODE_INVALID_PARAM;
		if (param->disableDeblk != 0 && param->disableDeblk != 1 &&
		    param->disableDeblk != 2)
			return RETCODE_INVALID_PARAM;
		if (param->deblkFilterOffsetAlpha < -6 ||
		    6 < param->deblkFilterOffsetAlpha)
			return RETCODE_INVALID_PARAM;
		if (param->deblkFilterOffsetBeta < -6 ||
		    6 < param->deblkFilterOffsetBeta)
			return RETCODE_INVALID_PARAM;
		if (param->chromaQpOffset < -12 || 12 < param->chromaQpOffset)
			return RETCODE_INVALID_PARAM;
		if (param->audEnable != 0 && param->audEnable != 1)
			return RETCODE_INVALID_PARAM;
		if (param->frameCroppingFlag != 0 &&
		    param->frameCroppingFlag != 1)
			return RETCODE_INVALID_PARAM;
		if (param->frameCropLeft & 0x01 ||
		    param->frameCropRight & 0x01 ||
		    param->frameCropTop & 0x01 ||
		    param->frameCropBottom & 0x01) {
			return RETCODE_INVALID_PARAM;
		}

		if (productId == PRODUCT_ID_980) {
			if (picWidth < MIN_ENC_PIC_WIDTH ||
			    picWidth > MAX_ENC_AVC_PIC_WIDTH)
				return RETCODE_INVALID_PARAM;
			if (picHeight < MIN_ENC_PIC_HEIGHT ||
			    picHeight > MAX_ENC_AVC_PIC_HEIGHT)
				return RETCODE_INVALID_PARAM;
			if (ActivePPS->entropyCodingMode > 2 ||
			    ActivePPS->entropyCodingMode < 0)
				return RETCODE_INVALID_PARAM;
			if (ActivePPS->cabacInitIdc < 0 ||
			    ActivePPS->cabacInitIdc > 2)
				return RETCODE_INVALID_PARAM;
			if (ActivePPS->transform8x8Mode != 1 &&
			    ActivePPS->transform8x8Mode != 0)
				return RETCODE_INVALID_PARAM;
			if (param->chromaFormat400 != 1 &&
			    param->chromaFormat400 != 0)
				return RETCODE_INVALID_PARAM;
			if (param->fieldFlag != 1 && param->fieldFlag != 0)
				return RETCODE_INVALID_PARAM;
		} else {
			if (picWidth < MIN_ENC_PIC_WIDTH ||
			    picWidth > MAX_ENC_PIC_WIDTH)
				return RETCODE_INVALID_PARAM;
			if (picHeight < MIN_ENC_PIC_HEIGHT)
				return RETCODE_INVALID_PARAM;
		}
	}

	if (pop->linear2TiledEnable == TRUE) {
		if (pop->linear2TiledMode != FF_FRAME &&
		    pop->linear2TiledMode != FF_FIELD)
			return RETCODE_INVALID_PARAM;
	}

	return RETCODE_SUCCESS;
}

RetCode ProductVpuEncFiniSeq(CodecInst *instance)
{
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	switch (instance->productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuFiniSeq(instance);
		break;
	}
	return ret;
}

RetCode ProductVpuEncSetup(CodecInst *instance)
{
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	switch (instance->productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuEncSetup(instance);
		break;
	}

	return ret;
}

RetCode ProductVpuEncode(CodecInst *instance, EncParam *param)
{
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	switch (instance->productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuEncode(instance, param);
		break;
	default:
		break;
	}

	return ret;
}

RetCode ProductVpuEncGetResult(CodecInst *instance, EncOutputInfo *result)
{
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	switch (instance->productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuEncGetResult(instance, result);
		break;
	}

	return ret;
}

RetCode ProductVpuEncGiveCommand(CodecInst *instance, CodecCommand cmd,
				 void *param)
{
	RetCode ret = RETCODE_NOT_SUPPORTED_FEATURE;

	switch (instance->productId) {
	case PRODUCT_ID_960:
	case PRODUCT_ID_980:
		ret = Coda9VpuEncGiveCommand(instance, cmd, param);
		break;
	}

	return ret;
}

RetCode ProductVpuEncInitSeq(CodecInst *instance) //[r]
{
	int productId;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	productId = instance->productId;

	switch (productId) {
	default:
		break;
	}

	return ret;
}

RetCode ProductVpuEncGetSeqInfo(CodecInst *instance, EncInitialInfo *info) //[r]
{
	int productId;
	RetCode ret = RETCODE_NOT_FOUND_VPU_DEVICE;

	productId = instance->productId;

	switch (productId) {
	default:
		break;
	}

	return ret;
}
