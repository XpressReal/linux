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
#include "ve1_vpuconfig.h"
#include "ve1_product.h"
#include "ve1_regdefine.h"
#include "ve1_vpu_md5.h"
#include <linux/tee_drv.h>

//#define GET_PERFORMANCE
#ifdef GET_PERFORMANCE
#include <string.h>
#include <sys/time.h>
struct timeval start_dec_tv;
struct timeval start_enc_tv;
struct timeval end_tv;
#endif
#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

//#define CODA9_CHECK_CODE_BUFFER_MD5SUM
#if defined(CODA9_CHECK_CODE_BUFFER_MD5SUM)
static Uint32 gCodaFwSize = 0;
#endif

extern int ta_TEEapi_memcpy_a7(struct tee_context *teeapi_ctx,
			       unsigned int teeapi_tee_session,
			       unsigned int dstPAddr, unsigned char *buf,
			       int size);

static RetCode Coda9VpuSetVeProtMode(Uint32 coreIdx, BOOL enable,
				     void *sess __maybe_unused,
				     void *rtk_sess __maybe_unused)
{
	if (1) {
		if (vdi_set_ve_prot_mode(coreIdx, enable) == 0) {
			VLOG(ERR, "coreIdx %d fail to enable prot mode",
			     coreIdx);
			return RETCODE_FAILURE;
		}
	}

	return RETCODE_SUCCESS;
}

static void Coda9VpuDecSetCommonAddress(CodecInst *instance)
{
	CodecInst *pCodecInst;
	PhysicalAddress paraBuffer;
	PhysicalAddress tempBuffer;
	vpu_buffer_t vb;

	osal_memset((void *)&vb, 0, sizeof(vpu_buffer_t));
	pCodecInst = instance;

	if (pCodecInst->isUseProtectBuffer) //RTK, need to review it
	{
		Coda9VpuSetVeProtMode(pCodecInst->coreIdx, TRUE,
				      pCodecInst->sess, pCodecInst->rtk_sess);
		vdi_get_common_memory_protect(pCodecInst->coreIdx, &vb,
					      pCodecInst->filp);
	} else {
		Coda9VpuSetVeProtMode(pCodecInst->coreIdx, FALSE, NULL, NULL);
		vdi_get_common_memory(pCodecInst->coreIdx, &vb);
	}

	tempBuffer = vb.phys_addr + CODE_BUF_SIZE;
	paraBuffer = tempBuffer + TEMP_BUF_SIZE;
	VpuWriteReg(pCodecInst->coreIdx, BIT_PARA_BUF_ADDR, paraBuffer);
	VpuWriteReg(pCodecInst->coreIdx, BIT_TEMP_BUF_ADDR, tempBuffer);
}

#ifdef ENABLE_TEE_DRM_FLOW
static void LoadBitCode(Uint32 coreIdx, PhysicalAddress codeBase,
			const Uint16 *codeWord, int codeSize);
static void Coda9VpuWriteMem(unsigned long core_idx, unsigned int addr,
			     unsigned char *data, int len, int endian,
			     void *teeapi_ctx, unsigned int teeapi_tee_session)
{
	int ret;
	unsigned char *tmpData = NULL;

	tmpData = (unsigned char *)osal_malloc(len);
	if (tmpData == NULL) {
		VLOG(ERR, "In[%s][%d] malloc failed\n", __func__, __LINE__);
		return;
	}
	vdi_write_memory_va(core_idx, tmpData, data, len, endian);

	ret = ta_TEEapi_memcpy_a7((struct tee_context *)teeapi_ctx,
				  teeapi_tee_session, addr, tmpData, len);
	if (ret < 0) {
		VLOG(ERR, "[%d]%s.ta_TEEapi_memcpy_a7() fail.ret:%d\n",
		     __LINE__, __func__, ret);
	}

	osal_free(tmpData);
}

RetCode Coda9VpuInitProtect(Uint32 coreIdx, void *firmware, Uint32 size,
			    void *sess, void *rtk_sess, void *filp)
{
	Uint32 data;
	vpu_buffer_t vb;
	PhysicalAddress tempBuffer;
	PhysicalAddress paraBuffer;
	PhysicalAddress codeBuffer;
	PhysicalAddress codeBufferProt;
#if defined(CODA9_CHECK_CODE_BUFFER_MD5SUM)
	unsigned char md5hash[16];
#endif

	VLOG(TRACE, "[+] [%d]%s.coreIdx:%d.firmware:%p.size:%d\n",__LINE__,__func__,coreIdx,firmware,size);

	osal_memset((void *)&vb, 0, sizeof(vpu_buffer_t));
	Coda9VpuSetVeProtMode((unsigned long)coreIdx, TRUE, sess, rtk_sess);
	vdi_get_common_memory_protect((unsigned long)coreIdx, &vb, filp);

	codeBufferProt = vb.phys_addr;
	tempBuffer = codeBufferProt + CODE_BUF_SIZE;
	paraBuffer = tempBuffer + TEMP_BUF_SIZE;

	{
		int i;
		const Uint16 *codeWord = (const Uint16 *)firmware;
		vdi_get_common_memory((unsigned long)coreIdx, &vb);
		codeBuffer = vb.phys_addr;
		LoadBitCode(coreIdx, codeBuffer, codeWord, size);
#if defined(CODA9_CHECK_CODE_BUFFER_MD5SUM)
		gCodaFwSize = size;
		VLOG(TRACE,
		     "[%d]%s.codeBuffer(phys:0x%08x,virt:0x%08x).size:%d\n",
		     __LINE__, __func__, vb.phys_addr, vb.virt_addr, size);
		MD5(((unsigned char *)(vb.virt_addr)), (size_t)size, md5hash);
		VLOG(TRACE,
		     "[%d]%s.codeBuffer Hash: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		     __LINE__, __func__, md5hash[0], md5hash[1], md5hash[2],
		     md5hash[3], md5hash[4], md5hash[5], md5hash[6], md5hash[7],
		     md5hash[8], md5hash[9], md5hash[10], md5hash[11],
		     md5hash[12], md5hash[13], md5hash[14], md5hash[15]);
#endif

		VpuWriteReg(coreIdx, BIT_INT_ENABLE, 0);
		VpuWriteReg(coreIdx, BIT_CODE_RUN, 0);

		for (i = 0; i < 2048; ++i) {
			data = codeWord[i];
			VpuWriteReg(coreIdx, BIT_CODE_DOWN, (i << 16) | data);
		}
	}

	VpuWriteReg(coreIdx, BIT_PARA_BUF_ADDR, paraBuffer);
	VpuWriteReg(coreIdx, BIT_CODE_BUF_ADDR, codeBuffer);
	VpuWriteReg(coreIdx, BIT_TEMP_BUF_ADDR, tempBuffer);

	VpuWriteReg(coreIdx, BIT_BIT_STREAM_CTRL, VPU_STREAM_ENDIAN);
	VpuWriteReg(
		coreIdx, BIT_FRAME_MEM_CTRL,
		CBCR_INTERLEAVE << 2 |
			VPU_FRAME_ENDIAN); // Interleave bit position is modified
	VpuWriteReg(coreIdx, BIT_BIT_STREAM_PARAM, 0);

	VpuWriteReg(coreIdx, BIT_AXI_SRAM_USE, 0);
	VpuWriteReg(coreIdx, BIT_INT_ENABLE, 0);
	VpuWriteReg(coreIdx, BIT_ROLLBACK_STATUS, 0);

	data = (1 << INT_BIT_BIT_BUF_FULL);
	data |= (1 << INT_BIT_BIT_BUF_EMPTY);
	data |= (1 << INT_BIT_DEC_MB_ROWS);
	data |= (1 << INT_BIT_SEQ_INIT);
	data |= (1 << INT_BIT_DEC_FIELD);
	data |= (1 << INT_BIT_PIC_RUN);

	VpuWriteReg(coreIdx, BIT_INT_ENABLE, data);
	VpuWriteReg(coreIdx, BIT_INT_CLEAR, 0x1);
	VpuWriteReg(coreIdx, BIT_BUSY_FLAG, 0x1);
	VpuWriteReg(coreIdx, BIT_CODE_RESET, 1);
	VpuWriteReg(coreIdx, BIT_CODE_RUN, 1);

	if (vdi_wait_vpu_busy(coreIdx, __VPU_BUSY_TIMEOUT, BIT_BUSY_FLAG) == -1)
		return RETCODE_VPU_RESPONSE_TIMEOUT;

	return RETCODE_SUCCESS;
}

#endif

static void LoadBitCode(Uint32 coreIdx, PhysicalAddress codeBase,
			const Uint16 *codeWord, int codeSize)
{
	int i;
	BYTE code[8];

	for (i = 0; i < codeSize; i += 4) {
		// 2byte little endian variable to 1byte big endian buffer
		code[0] = (BYTE)(codeWord[i + 0] >> 8);
		code[1] = (BYTE)codeWord[i + 0];
		code[2] = (BYTE)(codeWord[i + 1] >> 8);
		code[3] = (BYTE)codeWord[i + 1];
		code[4] = (BYTE)(codeWord[i + 2] >> 8);
		code[5] = (BYTE)codeWord[i + 2];
		code[6] = (BYTE)(codeWord[i + 3] >> 8);
		code[7] = (BYTE)codeWord[i + 3];
		VpuWriteMem(coreIdx, codeBase + i * 2, (BYTE *)code, 8,
			    VDI_BIG_ENDIAN);
	}

	vdi_set_bit_firmware_to_pm(coreIdx, codeWord);
}

static RetCode BitLoadFirmware(Uint32 coreIdx, PhysicalAddress codeBase,
			       const Uint16 *codeWord, int codeSize)
{
	int i;
	Uint32 data;

	LoadBitCode(coreIdx, codeBase, codeWord, codeSize);

	VpuWriteReg(coreIdx, BIT_INT_ENABLE, 0);
	VpuWriteReg(coreIdx, BIT_CODE_RUN, 0);

	for (i = 0; i < 2048; ++i) {
		data = codeWord[i];
		VpuWriteReg(coreIdx, BIT_CODE_DOWN, (i << 16) | data);
	}
	return RETCODE_SUCCESS;
}

static void SetEncFrameMemInfo(CodecInst *pCodecInst)
{
	Uint32 val;
	EncInfo *pEncInfo = &pCodecInst->CodecInfo->encInfo;

	switch (pCodecInst->productId) {
	case PRODUCT_ID_980:
		val = (pEncInfo->openParam.bwbEnable << 15) |
		      (pEncInfo->linear2TiledMode << 13) |
		      (pEncInfo->mapType << 9);
		if (pEncInfo->openParam.EncStdParam.avcParam.chromaFormat400)
			val |= (FORMAT_400 << 6);
		else
			val |= (FORMAT_420 << 6);
		val |= ((pEncInfo->openParam.cbcrInterleave)
			<< 2); // Interleave bit position is modified
		val |= pEncInfo->openParam.frameEndian;
		VpuWriteReg(pCodecInst->coreIdx, BIT_FRAME_MEM_CTRL, val);
		break;
	case PRODUCT_ID_960:
		val = 0;
		if (pEncInfo->mapType) {
			if (pEncInfo->mapType == TILED_FRAME_MB_RASTER_MAP ||
			    pEncInfo->mapType == TILED_FIELD_MB_RASTER_MAP)
				val |= (pEncInfo->linear2TiledEnable << 11) |
				       (0x03 << 9) | (FORMAT_420 << 6);
			else
				val |= (pEncInfo->linear2TiledEnable << 11) |
				       (0x02 << 9) | (FORMAT_420 << 6);
		}
		val |= ((pEncInfo->openParam.cbcrInterleave)
			<< 2); // Interleave bit position is modified
		val |= (pEncInfo->openParam.cbcrInterleave &
			pEncInfo->openParam.nv21)
		       << 3;
		val |= (pEncInfo->openParam.bwbEnable << 12);
		val |= pEncInfo->openParam.frameEndian;
		VpuWriteReg(pCodecInst->coreIdx, BIT_FRAME_MEM_CTRL, val);
		break;
	}

	return;
}

static char cmd2string[12][32] = {
    "ENC_SEQ_INIT",
    "ENC_SEQ_END",
    "PIC_RUN",
    "SET_FRAME_BUF",
    "ENCODE_HEADER",
    "ENC_PARA_SET",
    "DEC_PARA_SET",
    "DEC_BUF_FLUSH",
    "RC_CHANGE_PARAMETER",
    "VPU_SLEEP",
    "VPU_WAKE",
    "ENC_ROI_INIT",
};

void Coda9BitIssueCommand(Uint32 coreIdx, CodecInst *inst, int cmd)
{
	int instIdx = 0;
	int cdcMode = 0;
	int auxMode = 0;

    VLOG(TRACE, "[+] [%d]%s.coreIdx:%d.cmd:%d(%s)\n",__LINE__,__func__,coreIdx,cmd,((cmd<=12)?cmd2string[cmd-1]:""));

	if (inst != NULL) // command is specific to instance
	{
		instIdx = inst->instIndex;
		cdcMode = inst->codecMode;
		auxMode = inst->codecModeAux;
	}

	if (inst) {
		if (inst->codecMode < AVC_ENC) {
			VpuWriteReg(coreIdx, BIT_WORK_BUF_ADDR,
				    inst->CodecInfo->decInfo.vbWork.phys_addr);
#ifdef ENABLE_CODA9_WRITE_PROTECT
			SetDecWriteProtectRegions(inst);
#endif
		} else {
			VpuWriteReg(coreIdx, BIT_WORK_BUF_ADDR,
				    inst->CodecInfo->encInfo.vbWork.phys_addr);
		}
	}

	VpuWriteReg(coreIdx, BIT_BUSY_FLAG, 1);
	VpuWriteReg(coreIdx, BIT_RUN_INDEX, instIdx);
	VpuWriteReg(coreIdx, BIT_RUN_COD_STD, cdcMode);
	VpuWriteReg(coreIdx, BIT_RUN_AUX_STD, auxMode);
	if (inst && inst->loggingEnable)
		vdi_log(coreIdx, cmd, 1);
	VpuWriteReg(coreIdx, BIT_RUN_COMMAND, cmd);
    VLOG(TRACE, "[-] [%d]%s.coreIdx:%d\n",__LINE__,__func__,coreIdx);
}

static void SetupCoda9Properties(Uint32 coreIdx, Uint32 productId)
{
	VpuAttr *pAttr = &g_VpuCoreAttributes[coreIdx];
	Int32 val;
	char *pstr;
	Uint32 support_vtype = 0;

	/* Setup Attributes */
	pAttr = &g_VpuCoreAttributes[coreIdx];

	// Hardware version information
	val = VpuReadReg(coreIdx, VPU_PRODUCT_CODE_REGISTER);
	if ((val & 0xff00) == 0x3200)
		val = 0x3200;
	val = VpuReadReg(coreIdx, DBG_CONFIG_REPORT_0);
	pstr = (char *)&val;
	pAttr->productName[0] = pstr[3];
	pAttr->productName[1] = pstr[2];
	pAttr->productName[2] = pstr[1];
	pAttr->productName[3] = pstr[0];
	pAttr->productName[4] = 0;

	pAttr->supportDecoders =
		(1 << STD_AVC) | (1 << STD_VC1) | (1 << STD_MPEG2) |
		(1 << STD_MPEG4) | (1 << STD_H263) | (1 << STD_AVS) |
		(1 << STD_RV) | (1 << STD_THO) | (1 << STD_VP8);

	support_vtype = vdi_get_support_vtype(coreIdx);
	pAttr->supportDecoders = (pAttr->supportDecoders & support_vtype);
	VLOG(TRACE,
	     "\033[0;31mpAttr->supportDecoders : 0x%08x, support_vtype : 0x%08x\033[m\n",
	     pAttr->supportDecoders, support_vtype);

	/* Encoder */
	pAttr->supportEncoders =
		(1 << STD_AVC) | (1 << STD_MPEG4) | (1 << STD_H263);

	/* WTL */
	if (productId == PRODUCT_ID_960 || productId == PRODUCT_ID_980) {
		pAttr->supportWTL = 1;
	}
	/* Tiled2Linear */
	pAttr->supportTiled2Linear = 1;
	/* Maptypes */
	pAttr->supportMapTypes =
		(1 << LINEAR_FRAME_MAP) | (1 << TILED_FRAME_V_MAP) |
		(1 << TILED_FRAME_H_MAP) | (1 << TILED_FIELD_V_MAP) |
		(1 << TILED_MIXED_V_MAP) | (1 << TILED_FRAME_MB_RASTER_MAP) |
		(1 << TILED_FIELD_MB_RASTER_MAP);
	if (productId == PRODUCT_ID_980) {
		pAttr->supportMapTypes |= (1 << TILED_FRAME_NO_BANK_MAP) |
					  (1 << TILED_FIELD_NO_BANK_MAP);
	}
	/* Linear2Tiled */
	if (productId == PRODUCT_ID_960 || productId == PRODUCT_ID_980) {
		pAttr->supportLinear2Tiled = 1;
	}
	/* Framebuffer Cache */
	if (productId == PRODUCT_ID_960)
		pAttr->framebufferCacheType = FramebufCacheMaverickI;
	else if (productId == PRODUCT_ID_980)
		pAttr->framebufferCacheType = FramebufCacheMaverickII;
	else
		pAttr->framebufferCacheType = FramebufCacheNone;
	/* AXI 128bit Bus */
	pAttr->support128bitBus = FALSE;
	pAttr->supportEndianMask =
		(1 << VDI_LITTLE_ENDIAN) | (1 << VDI_BIG_ENDIAN) |
		(1 << VDI_32BIT_LITTLE_ENDIAN) | (1 << VDI_32BIT_BIG_ENDIAN);
	pAttr->supportBitstreamMode = (1 << BS_MODE_INTERRUPT) |
				      (1 << BS_MODE_PIC_END) |
				      (1 << BS_MODE_ROLLBACK);
	pAttr->bitstreamBufferMargin = VPU_GBU_SIZE;
	pAttr->numberOfMemProtectRgns = 6;
}

Uint32 Coda9VpuGetProductId(Uint32 coreIdx)
{
	Uint32 productId;
	Uint32 val;

	val = VpuReadReg(coreIdx, VPU_PRODUCT_CODE_REGISTER);

	if (val == BODA950_CODE)
		productId = PRODUCT_ID_950;
	else if (val == CODA960_CODE)
		productId = PRODUCT_ID_960;
	else if (val == CODA980_CODE)
		productId = PRODUCT_ID_980;
	else
		productId = PRODUCT_ID_NONE;

	if (productId != PRODUCT_ID_NONE)
		SetupCoda9Properties(coreIdx, productId);

	return productId;
}

RetCode Coda9VpuGetVersion(Uint32 coreIdx, Uint32 *versionInfo,
			   Uint32 *revision)
{
	/* Get Firmware version */
	VpuWriteReg(coreIdx, RET_FW_VER_NUM, 0);
	Coda9BitIssueCommand(coreIdx, NULL, FIRMWARE_GET);
	if (vdi_wait_vpu_busy(coreIdx, __VPU_BUSY_TIMEOUT, BIT_BUSY_FLAG) == -1)
		return RETCODE_VPU_RESPONSE_TIMEOUT;

	if (versionInfo != NULL) {
		*versionInfo = VpuReadReg(coreIdx, RET_FW_VER_NUM);
	}
	if (revision != NULL) {
		*revision = VpuReadReg(coreIdx, RET_FW_CODE_REV);
	}

	return RETCODE_SUCCESS;
}

RetCode Coda9VpuInit(Uint32 coreIdx, void *firmware, Uint32 size)
{
	Uint32 data;
	vpu_buffer_t vb;
	PhysicalAddress tempBuffer;
	PhysicalAddress paraBuffer;
	PhysicalAddress codeBuffer;

    VLOG(TRACE, "[+] [%d]%s.coreIdx:%d.firmware:%p.size:%d\n",__LINE__,__func__,coreIdx,firmware,size);

	osal_memset((void *)&vb, 0, sizeof(vpu_buffer_t));
	Coda9VpuSetVeProtMode((unsigned long)coreIdx, FALSE, NULL, NULL);
	vdi_get_common_memory((unsigned long)coreIdx, &vb);

	codeBuffer = vb.phys_addr;
	tempBuffer = codeBuffer + CODE_BUF_SIZE;
	paraBuffer = tempBuffer + TEMP_BUF_SIZE;

	BitLoadFirmware(coreIdx, codeBuffer, (const Uint16 *)firmware, size);

	VpuWriteReg(coreIdx, BIT_PARA_BUF_ADDR, paraBuffer);
	VpuWriteReg(coreIdx, BIT_CODE_BUF_ADDR, codeBuffer);
	VpuWriteReg(coreIdx, BIT_TEMP_BUF_ADDR, tempBuffer);

	VpuWriteReg(coreIdx, BIT_BIT_STREAM_CTRL, VPU_STREAM_ENDIAN);
	VpuWriteReg(
		coreIdx, BIT_FRAME_MEM_CTRL,
		CBCR_INTERLEAVE << 2 |
			VPU_FRAME_ENDIAN); // Interleave bit position is modified
	VpuWriteReg(coreIdx, BIT_BIT_STREAM_PARAM, 0);

	VpuWriteReg(coreIdx, BIT_AXI_SRAM_USE, 0);
	VpuWriteReg(coreIdx, BIT_INT_ENABLE, 0);
	VpuWriteReg(coreIdx, BIT_ROLLBACK_STATUS, 0);

	data = (1 << INT_BIT_BIT_BUF_FULL);
	data |= (1 << INT_BIT_BIT_BUF_EMPTY);
	data |= (1 << INT_BIT_DEC_MB_ROWS);
	data |= (1 << INT_BIT_SEQ_INIT);
	data |= (1 << INT_BIT_DEC_FIELD);
	data |= (1 << INT_BIT_PIC_RUN);

	VpuWriteReg(coreIdx, BIT_INT_ENABLE, data);
	VpuWriteReg(coreIdx, BIT_INT_CLEAR, 0x1);
	VpuWriteReg(coreIdx, BIT_BUSY_FLAG, 0x1);
	VpuWriteReg(coreIdx, BIT_CODE_RESET, 1);
	VpuWriteReg(coreIdx, BIT_CODE_RUN, 1);

	if (vdi_wait_vpu_busy(coreIdx, __VPU_BUSY_TIMEOUT, BIT_BUSY_FLAG) ==
	    -1) {
		VLOG(ERR, "[-] [%d]%s.RETCODE_VPU_RESPONSE_TIMEOUT\n", __LINE__,
		     __func__);
		return RETCODE_VPU_RESPONSE_TIMEOUT;
	}

    VLOG(TRACE, "[-] [%d]%s.coreIdx:%d.firmware:%p.size:%d\n",__LINE__,__func__,coreIdx,firmware,size);
    return RETCODE_SUCCESS;
}

RetCode Coda9VpuReInit(Uint32 coreIdx, void *firmware, Uint32 size)
{
	vpu_buffer_t vb;
	PhysicalAddress tempBuffer;
	PhysicalAddress paraBuffer;
	PhysicalAddress codeBuffer;
	PhysicalAddress oldCodeBuffer;

	osal_memset((void *)&vb, 0, sizeof(vpu_buffer_t));
	Coda9VpuSetVeProtMode((unsigned long)coreIdx, FALSE, NULL, NULL);
	vdi_get_common_memory((unsigned long)coreIdx, &vb);

	codeBuffer = vb.phys_addr;
	tempBuffer = codeBuffer + CODE_BUF_SIZE;
	paraBuffer = tempBuffer + TEMP_BUF_SIZE;

	oldCodeBuffer = VpuReadReg(coreIdx, BIT_CODE_BUF_ADDR);

	VpuWriteReg(coreIdx, BIT_PARA_BUF_ADDR, paraBuffer);
	VpuWriteReg(coreIdx, BIT_CODE_BUF_ADDR, codeBuffer);
	VpuWriteReg(coreIdx, BIT_TEMP_BUF_ADDR, tempBuffer);

	if (oldCodeBuffer != codeBuffer) {
		LoadBitCode(coreIdx, codeBuffer, (const Uint16 *)firmware,
			    size);
	}

	return RETCODE_SUCCESS;
}

Uint32 Coda9VpuIsInit(Uint32 coreIdx)
{
	Uint32 pc;

	pc = VpuReadReg(coreIdx, BIT_CUR_PC);

	return pc;
}

Int32 Coda9VpuIsBusy(Uint32 coreIdx)
{
	return VpuReadReg(coreIdx, BIT_BUSY_FLAG);
}

Int32 Coda9VpuWaitInterrupt(CodecInst *handle, int timeout)
{
	Int32 reason = 0;
	unsigned int addr = 0;
	unsigned int boundary = 0;
	unsigned int value = 0;
#if defined(CODA9_CHECK_CODE_BUFFER_MD5SUM)
	vpu_buffer_t vb;
	unsigned char md5hash[16];
#endif

	reason = vdi_wait_interrupt(handle->coreIdx, timeout, BIT_INT_REASON);
	VLOG(TRACE, "[%d]%s.h:0x%x.reason:0x%x\n", __LINE__, __func__, handle,
	     reason);

	if (reason == -1) {
		handle->noInterruptCnt++;
		VLOG(TRACE, "[%d]%s.h:0x%x.interrupt -1.cnt:%d\n", __LINE__,
		     __func__, handle, handle->noInterruptCnt);
		if (handle->noInterruptCnt > 100) {
			VLOG(TRACE,
			     "[%d]%s.h:0x%x.BIT_BUSY_FLAG:0x%x.BIT_CUR_PC:0x%x\n",
			     __LINE__, __func__, handle,
			     VpuReadReg(handle->coreIdx, BIT_BUSY_FLAG),
			     VpuReadReg(handle->coreIdx, BIT_CUR_PC));

#if defined(CODA9_CHECK_CODE_BUFFER_MD5SUM)
			vdi_get_common_memory((unsigned long)handle->coreIdx,
					      &vb);
			VLOG(TRACE,
			     "[%d]%s.codeBuffer(phys:0x%08x,virt:0x%08x).size:%d.BIT_CODE_BUF_ADDR:0x%08x\n",
			     __LINE__, __func__, vb.phys_addr, vb.virt_addr,
			     gCodaFwSize,
			     VpuReadReg(handle->coreIdx, BIT_CODE_BUF_ADDR));
			MD5(((unsigned char *)(vb.virt_addr)),
			    (size_t)gCodaFwSize, md5hash);
			VLOG(TRACE,
			     "[%d]%s.codeBuffer Hash: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			     __LINE__, __func__, md5hash[0], md5hash[1],
			     md5hash[2], md5hash[3], md5hash[4], md5hash[5],
			     md5hash[6], md5hash[7], md5hash[8], md5hash[9],
			     md5hash[10], md5hash[11], md5hash[12], md5hash[13],
			     md5hash[14], md5hash[15]);
#endif
		}
		if ((handle->noInterruptCnt > 100) &&
		    (handle->noInterruptCnt % 100 == 1)) {
			VLOG(TRACE, "read register 0x98040000 ~ 0x98040200\n");
			addr = BIT_BASE;
			boundary = addr + 0x200;
			while (addr <= boundary) {
				value = VpuReadReg(handle->coreIdx, addr);
				VLOG(TRACE, "[0x%08x] = 0x%08x.\n", addr,
				     value);
				addr += 4;
			}

			VLOG(TRACE, "read register 0x98043000 ~ 0x98043100\n");
			addr = 0x98043000 - 0x98040000;
			boundary = addr + 0x100;
			while (addr <= boundary) {
				value = VpuReadReg(handle->coreIdx, addr);
				VLOG(TRACE, "[0x%08x] = 0x%08x.\n", addr,
				     value);
				addr += 4;
			}
		}
	} else {
		handle->noInterruptCnt = 0;
	}

	return reason;
}

RetCode Coda9VpuClearInterrupt(Uint32 coreIdx)
{
#ifdef GET_PERFORMANCE
	gettimeofday(&end_tv, NULL);
	unsigned int duration = 0;
	if (start_dec_tv.tv_sec != 0) {
		duration = (end_tv.tv_sec - start_dec_tv.tv_sec) * 1000 +
			   (end_tv.tv_usec - start_dec_tv.tv_usec) / 1000;
		if (duration > 3)
			VLOG(TRACE,
			     "[VPUAPI]  DEC time per frame : %d ms (cycle: %d)\n",
			     duration, VpuReadReg(coreIdx, BIT_FRAME_CYCLE));
	}
	if (start_enc_tv.tv_sec != 0) {
		duration = (end_tv.tv_sec - start_enc_tv.tv_sec) * 1000 +
			   (end_tv.tv_usec - start_enc_tv.tv_usec) / 1000;
		if (duration > 3)
			VLOG(TRACE,
			     "[VPUAPI]  ENC time per frame : %d ms (cycle: %d)\n",
			     duration, VpuReadReg(coreIdx, BIT_FRAME_CYCLE));
	}
	memset(&start_dec_tv, 0, sizeof(start_dec_tv));
	memset(&start_enc_tv, 0, sizeof(start_enc_tv));
#endif

#if 1
	VpuWriteReg(coreIdx, BIT_INT_REASON,
		    0); // tell to F/W that HOST received an interrupt.
#else
	(void)coreIdx; //unused
#endif

	return RETCODE_SUCCESS;
}

RetCode Coda9VpuReset(Uint32 coreIdx, SWResetMode resetMode)
{
	Uint32 cmd;
	Int32 productId = Coda9VpuGetProductId(coreIdx);

    VLOG(TRACE, "[+] [%d]%s.coreIdx:%d.resetMode:%d\n",__LINE__,__func__,coreIdx,resetMode);
	if (productId == PRODUCT_ID_960 || productId == PRODUCT_ID_980) {
		if (resetMode != SW_RESET_ON_BOOT) {
			cmd = VpuReadReg(coreIdx, BIT_RUN_COMMAND);
			if (cmd == DEC_SEQ_INIT || cmd == PIC_RUN) {
				if (VpuReadReg(coreIdx, BIT_BUSY_FLAG) ||
				    VpuReadReg(coreIdx, BIT_INT_REASON)) {
#define MBC_SET_SUBBLK_EN                                                      \
	(MBC_BASE + 0xA0) // subblk_man_mode[20] cr_subblk_man_en[19:0]
					// stop all of pipeline
					VpuWriteReg(coreIdx, MBC_SET_SUBBLK_EN,
						    ((1 << 20) | 0));

					// force to set the end of Bitstream to be decoded.
					cmd = VpuReadReg(coreIdx,
							 BIT_BIT_STREAM_PARAM);
					cmd |= 1 << 2;
					VpuWriteReg(coreIdx,
						    BIT_BIT_STREAM_PARAM, cmd);

					cmd = VpuReadReg(coreIdx, BIT_RD_PTR);
					VpuWriteReg(coreIdx, BIT_WR_PTR, cmd);

					cmd = vdi_wait_interrupt(
						coreIdx, __VPU_BUSY_TIMEOUT,
						BIT_INT_REASON);

					if (cmd != INTERRUPT_TIMEOUT_VALUE) {
						VpuWriteReg(coreIdx,
							    BIT_INT_REASON, 0);
						VpuWriteReg(
							coreIdx, BIT_INT_CLEAR,
							1); // clear HW signal
					}
					// now all of hardwares would be stop.
				}
			}
		}

		// Waiting for completion of BWB transaction first
		if (vdi_wait_vpu_busy(coreIdx, __VPU_BUSY_TIMEOUT,
				      GDI_BWB_STATUS) == -1) {
			vdi_log(coreIdx, 0x10, 2);
			VLOG(ERR, "[-] [%d]%s.RETCODE_VPU_RESPONSE_TIMEOUT\n",
			     __LINE__, __func__);
			return RETCODE_VPU_RESPONSE_TIMEOUT;
		}

		// Waiting for completion of bus transaction
		// Step1 : No more request
		VpuWriteReg(
			coreIdx, GDI_BUS_CTRL,
			0x11); // no more request {3'b0,no_more_req_sec,3'b0,no_more_req}

		// Step2 : Waiting for completion of bus transaction
		if (vdi_wait_bus_busy(coreIdx, __VPU_BUSY_TIMEOUT,
				      GDI_BUS_STATUS) == -1) {
			VpuWriteReg(coreIdx, GDI_BUS_CTRL, 0x00);
			vdi_log(coreIdx, 0x10, 2);
			VLOG(ERR, "[-] [%d]%s.RETCODE_VPU_RESPONSE_TIMEOUT\n",
			     __LINE__, __func__);
			return RETCODE_VPU_RESPONSE_TIMEOUT;
		}

		cmd = 0;
		// Software Reset Trigger
		if (resetMode != SW_RESET_ON_BOOT)
			cmd = VPU_SW_RESET_BPU_CORE | VPU_SW_RESET_BPU_BUS;
		cmd |= VPU_SW_RESET_VCE_CORE | VPU_SW_RESET_VCE_BUS;
		if (resetMode == SW_RESET_ON_BOOT)
			cmd |= VPU_SW_RESET_GDI_CORE |
			       VPU_SW_RESET_GDI_BUS; // If you reset GDI, tiled map should be reconfigured

		VpuWriteReg(coreIdx, BIT_SW_RESET, cmd);

		// wait until reset is done
		if (vdi_wait_vpu_busy(coreIdx, __VPU_BUSY_TIMEOUT,
				      BIT_SW_RESET_STATUS) == -1) {
			VpuWriteReg(coreIdx, BIT_SW_RESET, 0x00);
			VpuWriteReg(coreIdx, GDI_BUS_CTRL, 0x00);
			vdi_log(coreIdx, 0x10, 2);
			VLOG(ERR, "[-] [%d]%s.RETCODE_VPU_RESPONSE_TIMEOUT\n",
			     __LINE__, __func__);
			return RETCODE_VPU_RESPONSE_TIMEOUT;
		}

		VpuWriteReg(coreIdx, BIT_SW_RESET, 0);

		// Step3 : must clear GDI_BUS_CTRL after done SW_RESET
		VpuWriteReg(coreIdx, GDI_BUS_CTRL, 0x00);
	} else {
		vdi_log(coreIdx, 0x10, 0);
		VLOG(ERR, "[-] [%d]%s.RETCODE_NOT_FOUND_VPU_DEVICE\n", __LINE__,
		     __func__);
		return RETCODE_NOT_FOUND_VPU_DEVICE;
	}

    VLOG(TRACE, "[-] [%d]%s.coreIdx:%d.ret:RETCODE_SUCCESS\n",__LINE__,__func__,coreIdx);
	return RETCODE_SUCCESS;
}

RetCode Coda9VpuSleepWake(Uint32 coreIdx, int iSleepWake, const Uint16 *code,
			  Uint32 size)
{
	static unsigned int regBk[64];
	int i = 0;
	const Uint16 *bit_code = NULL;
	if (code && size > 0)
		bit_code = code;

	if (!bit_code)
		return RETCODE_INVALID_PARAM;

	if (vdi_wait_vpu_busy(coreIdx, __VPU_BUSY_TIMEOUT, BIT_BUSY_FLAG) ==
	    -1) {
		return RETCODE_VPU_RESPONSE_TIMEOUT;
	}

	if (iSleepWake == 1) {
		for (i = 0; i < 64; i++)
			regBk[i] =
				VpuReadReg(coreIdx, BIT_BASE + 0x100 + (i * 4));
	} else {
		VpuWriteReg(coreIdx, BIT_CODE_RUN, 0);

		for (i = 0; i < 64; i++)
			VpuWriteReg(coreIdx, BIT_BASE + 0x100 + (i * 4),
				    regBk[i]);

		VpuWriteReg(coreIdx, BIT_BUSY_FLAG, 1);
		VpuWriteReg(coreIdx, BIT_CODE_RESET, 1);
		VpuWriteReg(coreIdx, BIT_CODE_RUN, 1);

		if (vdi_wait_vpu_busy(coreIdx, __VPU_BUSY_TIMEOUT,
				      BIT_BUSY_FLAG) == -1) {
			return RETCODE_VPU_RESPONSE_TIMEOUT;
		}
	}

	return RETCODE_SUCCESS;
}

static RetCode SetupDecCodecInstance(Int32 productId, CodecInst *pCodec)
{
	DecInfo *pDecInfo = &pCodec->CodecInfo->decInfo;

	pDecInfo->streamRdPtrRegAddr = BIT_RD_PTR;
	pDecInfo->streamWrPtrRegAddr = BIT_WR_PTR;
	pDecInfo->frameDisplayFlagRegAddr = BIT_FRM_DIS_FLG;
	pDecInfo->currentPC = BIT_CUR_PC;
	pDecInfo->busyFlagAddr = BIT_BUSY_FLAG;

	if (productId == PRODUCT_ID_960) {
		pDecInfo->dramCfg.rasBit = EM_RAS;
		pDecInfo->dramCfg.casBit = EM_CAS;
		pDecInfo->dramCfg.bankBit = EM_BANK;
		pDecInfo->dramCfg.busBit = EM_WIDTH;
	}

	return RETCODE_SUCCESS;
}

static RetCode SetupEncCodecInstance(Int32 productId, CodecInst *pCodec)
{
	EncInfo *pEncInfo = &pCodec->CodecInfo->encInfo;

	pEncInfo->streamRdPtrRegAddr = BIT_RD_PTR;
	pEncInfo->streamWrPtrRegAddr = BIT_WR_PTR;
	pEncInfo->currentPC = BIT_CUR_PC;
	pEncInfo->busyFlagAddr = BIT_BUSY_FLAG;

	if (productId == PRODUCT_ID_960) {
		pEncInfo->dramCfg.rasBit = EM_RAS;
		pEncInfo->dramCfg.casBit = EM_CAS;
		pEncInfo->dramCfg.bankBit = EM_BANK;
		pEncInfo->dramCfg.busBit = EM_WIDTH;
	}

	return RETCODE_SUCCESS;
}

RetCode Coda9VpuBuildUpDecParam(CodecInst *pCodec, DecOpenParam *param)
{
	RetCode ret = RETCODE_SUCCESS;
#ifdef ENABLE_CODA9_WRITE_PROTECT
	Uint32 i;
	VpuAttr *pAttr;
#endif
	Uint32 coreIdx;
	Uint32 productId;
	DecInfo *pDecInfo = &pCodec->CodecInfo->decInfo;

	coreIdx = pCodec->coreIdx;
	productId = Coda9VpuGetProductId(coreIdx);
#ifdef ENABLE_CODA9_WRITE_PROTECT
	pAttr = &g_VpuCoreAttributes[coreIdx];
	pDecInfo->writeMemProtectCfg.numOfRegion =
		pAttr->numberOfMemProtectRgns;
	VLOG(INFO, "[%d]%s.numOfRegion:%d\n", __LINE__, __func__,
	     pDecInfo->writeMemProtectCfg.numOfRegion);
#endif

	if ((ret = SetupDecCodecInstance(productId, pCodec)) != RETCODE_SUCCESS)
		return ret;

	if (param->vbWork.size) {
		pDecInfo->vbWork = param->vbWork;
		pDecInfo->workBufferAllocExt = 1;
	} else {
		pDecInfo->vbWork.size = WORK_BUF_SIZE;
		if (pCodec->codecMode == AVC_DEC)
			pDecInfo->vbWork.size += PS_SAVE_SIZE;

		//ENABLE_TEE_DRM_FLOW
		if (pCodec->isUseProtectBuffer)
			pDecInfo->vbWork.req_spec_region = VE_SECURE_PROTECTION;
		else
			pDecInfo->vbWork.req_spec_region = 0;

		if (vdi_allocate_dma_memory_no_mmap(pCodec->coreIdx,
						    &pDecInfo->vbWork,
						    pCodec->filp) < 0)
			return RETCODE_INSUFFICIENT_RESOURCE;
		VLOG(TRACE,
		     "[%d]%s.vdi_allocate_dma_memory_no_mmap vbWork(0x%lx,0x%lx,0x%lx,%d,%d)\n",
		     __LINE__, __func__, pDecInfo->vbWork.phys_addr,
		     pDecInfo->vbWork.base, pDecInfo->vbWork.virt_addr,
		     pDecInfo->vbWork.size, pDecInfo->vbWork.req_spec_region);

		param->vbWork = pDecInfo->vbWork;
		pDecInfo->workBufferAllocExt = 0;
	}

	if (productId == PRODUCT_ID_960) {
		pDecInfo->dramCfg.bankBit = EM_BANK;
		pDecInfo->dramCfg.casBit = EM_CAS;
		pDecInfo->dramCfg.rasBit = EM_RAS;
		pDecInfo->dramCfg.busBit = EM_WIDTH;
	}

#ifdef ENABLE_CODA9_WRITE_PROTECT
	for (i = 0; i < WPROT_DEC_MAX; i++)
		pDecInfo->writeMemProtectCfg.decRegion[i].enable = 0;
	ConfigDecWPROTRegion(
		pCodec->coreIdx,
		pDecInfo); // set common & PS or Work buffer memory protection
#endif

	return ret;
}

RetCode Coda9VpuDecInitSeq(DecHandle handle)
{
	CodecInst *pCodecInst = (CodecInst *)handle;
	DecInfo *pDecInfo = &pCodecInst->CodecInfo->decInfo;
	Uint32 val = 0;

	VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_BB_START,
		    pDecInfo->streamBufStartAddr);
	VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_BB_SIZE,
		    pDecInfo->streamBufSize / 1024); // size in KBytes
	Coda9VpuDecSetCommonAddress(pCodecInst);

	if (pDecInfo->userDataEnable == TRUE) {
		val = 0;
		val |= (pDecInfo->userDataReportMode << 10);
		val |= (pDecInfo->userDataEnable << 5);
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_USER_DATA_OPTION,
			    val);
		VpuWriteReg(pCodecInst->coreIdx,
			    CMD_DEC_SEQ_USER_DATA_BASE_ADDR,
			    pDecInfo->userDataBufAddr);
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_USER_DATA_BUF_SIZE,
			    pDecInfo->userDataBufSize);
	} else {
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_USER_DATA_OPTION,
			    0);
		VpuWriteReg(pCodecInst->coreIdx,
			    CMD_DEC_SEQ_USER_DATA_BASE_ADDR, 0);
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_USER_DATA_BUF_SIZE,
			    0);
	}
	val = 0;

	val |= (pDecInfo->reorderEnable << 1) & 0x2;

	val |= (pDecInfo->openParam.mp4DeblkEnable & 0x1);
	val |= (pDecInfo->avcErrorConcealMode << 2);
	VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_OPTION, val);

	switch (pCodecInst->codecMode) {
	case VC1_DEC:
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_VC1_STREAM_FMT,
			    (0 << 3) & 0x08);
		break;
	case MP4_DEC:
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_MP4_ASP_CLASS,
			    (VPU_GMC_PROCESS_METHOD << 3) |
				    pDecInfo->openParam.mp4Class);
		break;
	case AVC_DEC:
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_X264_MV_EN,
			    VPU_AVC_X264_SUPPORT);
		break;
	}

	if (pCodecInst->codecMode == AVC_DEC)
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_SEQ_SPP_CHUNK_SIZE,
			    VPU_GBU_SIZE);

	VpuWriteReg(pCodecInst->coreIdx, pDecInfo->streamWrPtrRegAddr,
		    pDecInfo->streamWrPtr);
	VpuWriteReg(pCodecInst->coreIdx, pDecInfo->streamRdPtrRegAddr,
		    pDecInfo->streamRdPtr);

	if (pCodecInst->productId == PRODUCT_ID_980 ||
	    pCodecInst->productId == PRODUCT_ID_960) {
		pDecInfo->streamEndflag &= ~(3 << 3);
		if (pDecInfo->openParam.bitstreamMode ==
		    BS_MODE_ROLLBACK) //rollback mode
			pDecInfo->streamEndflag |= (1 << 3);
		else if (pDecInfo->openParam.bitstreamMode == BS_MODE_PIC_END)
			pDecInfo->streamEndflag |= (2 << 3);
		else { // Interrupt Mode
			if (pDecInfo->seqInitEscape) {
				pDecInfo->streamEndflag |= (2 << 3);
			}
		}
	}
	VpuWriteReg(pCodecInst->coreIdx, BIT_BIT_STREAM_PARAM,
		    pDecInfo->streamEndflag);

	val = pDecInfo->openParam.streamEndian;
	VpuWriteReg(pCodecInst->coreIdx, BIT_BIT_STREAM_CTRL, val);

	if (pCodecInst->productId == PRODUCT_ID_980) {
		val = 0;
		val |= (pDecInfo->openParam.bwbEnable << 15);
		val |= (pDecInfo->wtlMode << 17) |
		       (pDecInfo->tiled2LinearMode << 13) | (FORMAT_420 << 6);
		val |= ((pDecInfo->openParam.cbcrInterleave)
			<< 2); // Interleave bit position is modified
		val |= pDecInfo->openParam.frameEndian;
		val |= pDecInfo->openParam.nv21 << 3;
		VpuWriteReg(pCodecInst->coreIdx, BIT_FRAME_MEM_CTRL, val);
	} else if (pCodecInst->productId == PRODUCT_ID_960) {
		val = 0;
		val |= (pDecInfo->wtlEnable << 17);
		val |= (pDecInfo->openParam.bwbEnable << 12);
		val |= ((pDecInfo->openParam.cbcrInterleave)
			<< 2); // Interleave bit position is modified
		val |= pDecInfo->openParam.frameEndian;
		VpuWriteReg(pCodecInst->coreIdx, BIT_FRAME_MEM_CTRL, val);
	} else {
		return RETCODE_NOT_FOUND_VPU_DEVICE;
	}

	VpuWriteReg(pCodecInst->coreIdx, pDecInfo->frameDisplayFlagRegAddr, 0);
	Coda9BitIssueCommand(pCodecInst->coreIdx, pCodecInst, DEC_SEQ_INIT);

	return RETCODE_SUCCESS;
}

RetCode Coda9VpuFiniSeq(CodecInst *instance)
{
	Coda9BitIssueCommand(instance->coreIdx, instance, DEC_SEQ_END);
	if (vdi_wait_vpu_busy(instance->coreIdx, __VPU_BUSY_TIMEOUT,
			      BIT_BUSY_FLAG) == -1) {
		return RETCODE_VPU_RESPONSE_TIMEOUT;
	}

	return RETCODE_SUCCESS;
}

RetCode Coda9VpuDecode(CodecInst *instance, DecParam *param)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	Uint32 rotMir;
	Int32 val;
	vpu_instance_pool_t *vip;
#if defined(CODA9_CHECK_CODE_BUFFER_MD5SUM)
	vpu_buffer_t vb;
	unsigned char md5hash[16];
#endif

	pCodecInst = instance;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;
	vip = (vpu_instance_pool_t *)vdi_get_instance_pool(pCodecInst->coreIdx);
	if (!vip) {
		return RETCODE_INVALID_HANDLE;
	}

#if defined(CODA9_CHECK_CODE_BUFFER_MD5SUM)
	vdi_get_common_memory((unsigned long)instance->coreIdx, &vb);
	VLOG(TRACE,
	     "[%d]%s.codeBuffer(phys:0x%08x,virt:0x%08x).size:%d.BIT_CODE_BUF_ADDR:0x%08x\n",
	     __LINE__, __func__, vb.phys_addr, vb.virt_addr, gCodaFwSize,
	     VpuReadReg(instance->coreIdx, BIT_CODE_BUF_ADDR));
	MD5(((unsigned char *)(vb.virt_addr)), (size_t)gCodaFwSize, md5hash);
	VLOG(TRACE,
	     "[%d]%s.codeBuffer Hash: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     __LINE__, __func__, md5hash[0], md5hash[1], md5hash[2], md5hash[3],
	     md5hash[4], md5hash[5], md5hash[6], md5hash[7], md5hash[8],
	     md5hash[9], md5hash[10], md5hash[11], md5hash[12], md5hash[13],
	     md5hash[14], md5hash[15]);
#endif

	Coda9VpuDecSetCommonAddress(instance);

	rotMir = 0;
	if (pDecInfo->rotationEnable) {
		rotMir |= 0x10; // Enable rotator
		switch (pDecInfo->rotationAngle) {
		case 0:
			rotMir |= 0x0;
			break;

		case 90:
			rotMir |= 0x1;
			break;

		case 180:
			rotMir |= 0x2;
			break;

		case 270:
			rotMir |= 0x3;
			break;
		}
	}

	if (pDecInfo->mirrorEnable) {
		rotMir |= 0x10; // Enable rotator
		switch (pDecInfo->mirrorDirection) {
		case MIRDIR_NONE:
			rotMir |= 0x0;
			break;

		case MIRDIR_VER:
			rotMir |= 0x4;
			break;

		case MIRDIR_HOR:
			rotMir |= 0x8;
			break;

		case MIRDIR_HOR_VER:
			rotMir |= 0xc;
			break;
		}
	}

	if (pDecInfo->tiled2LinearEnable) {
		rotMir |= 0x10;
	}

	if (pDecInfo->deringEnable) {
		rotMir |= 0x20; // Enable Dering Filter
	}

	if (rotMir && !pDecInfo->rotatorOutputValid) {
		return RETCODE_ROTATOR_OUTPUT_NOT_SET;
	}

	VpuWriteReg(pCodecInst->coreIdx, RET_DEC_PIC_CROP_LEFT_RIGHT,
		    0); // frame crop information(left, right)
	VpuWriteReg(pCodecInst->coreIdx, RET_DEC_PIC_CROP_TOP_BOTTOM,
		    0); // frame crop information(top, bottom)

	if (pCodecInst->productId == PRODUCT_ID_960) {
		if (pDecInfo->mapType > LINEAR_FRAME_MAP &&
		    pDecInfo->mapType <= TILED_MIXED_V_MAP) {
			SetTiledFrameBase(pCodecInst->coreIdx,
					  pDecInfo->mapCfg.tiledBaseAddr);
		} else {
			SetTiledFrameBase(pCodecInst->coreIdx, 0);
		}
	}

	if (pDecInfo->mapType != LINEAR_FRAME_MAP &&
	    pDecInfo->mapType != LINEAR_FIELD_MAP) {
		val = SetTiledMapType(
			pCodecInst->coreIdx, &pDecInfo->mapCfg,
			pDecInfo->mapType,
			(pDecInfo->stride > pDecInfo->frameBufferHeight) ?
				pDecInfo->stride :
				pDecInfo->frameBufferHeight,
			pDecInfo->openParam.cbcrInterleave, &pDecInfo->dramCfg);
	} else {
		val = SetTiledMapType(pCodecInst->coreIdx, &pDecInfo->mapCfg,
				      pDecInfo->mapType, pDecInfo->stride,
				      pDecInfo->openParam.cbcrInterleave,
				      &pDecInfo->dramCfg);
	}
	if (val == 0) {
		return RETCODE_INVALID_PARAM;
	}

	if (rotMir & 0x30) { // rotator or dering or tiled2linear enabled
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_ROT_MODE, rotMir);
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_ROT_INDEX,
			    pDecInfo->rotatorOutput.myIndex);
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_ROT_ADDR_Y,
			    pDecInfo->rotatorOutput.bufY);
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_ROT_ADDR_CB,
			    pDecInfo->rotatorOutput.bufCb);
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_ROT_ADDR_CR,
			    pDecInfo->rotatorOutput.bufCr);
		if (pCodecInst->productId == PRODUCT_ID_980) {
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_DEC_PIC_ROT_BOTTOM_Y,
				    pDecInfo->rotatorOutput.bufYBot);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_DEC_PIC_ROT_BOTTOM_CB,
				    pDecInfo->rotatorOutput.bufCbBot);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_DEC_PIC_ROT_BOTTOM_CR,
				    pDecInfo->rotatorOutput.bufCrBot);
		}
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_ROT_STRIDE,
			    pDecInfo->rotatorStride);
	} else {
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_ROT_MODE, rotMir);
	}
	if (pDecInfo->userDataEnable) {
		VpuWriteReg(pCodecInst->coreIdx,
			    CMD_DEC_PIC_USER_DATA_BASE_ADDR,
			    pDecInfo->userDataBufAddr);
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_USER_DATA_BUF_SIZE,
			    pDecInfo->userDataBufSize);
	} else {
		VpuWriteReg(pCodecInst->coreIdx,
			    CMD_DEC_PIC_USER_DATA_BASE_ADDR, 0);
		VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_USER_DATA_BUF_SIZE,
			    0);
	}

	val = 0;
	if (param->iframeSearchEnable ==
	    TRUE) { // if iframeSearch is Enable, other bit is ignore;
		val |= (pDecInfo->userDataReportMode << 10);

		if (pCodecInst->codecMode == AVC_DEC ||
		    pCodecInst->codecMode == VC1_DEC) {
			if (param->iframeSearchEnable == 1)
				val |= (1 << 11) | (1 << 2);
			else if (param->iframeSearchEnable == 2)
				val |= (1 << 2);
		} else {
			val |= ((param->iframeSearchEnable & 0x1) << 2);
		}
	} else {
		val |= (pDecInfo->userDataReportMode << 10);
		val |= (pDecInfo->userDataEnable << 5);
		val |= (param->skipframeMode << 3);
	}

	if (pCodecInst->productId == PRODUCT_ID_980) {
		if (pCodecInst->codecMode == AVC_DEC &&
		    pDecInfo->lowDelayInfo.lowDelayEn) {
			val |= (pDecInfo->lowDelayInfo.lowDelayEn << 18);
		}
	}
	if (pCodecInst->codecMode == MP2_DEC) {
		val |= ((param->DecStdParam.mp2PicFlush & 1) << 15);
	}
	if (pCodecInst->codecMode == RV_DEC) {
		val |= ((param->DecStdParam.rvDbkMode & 0x0f) << 16);
	}

	VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_OPTION, val);

	if (pCodecInst->productId == PRODUCT_ID_980) {
		if (pDecInfo->lowDelayInfo.lowDelayEn == TRUE) {
			VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_NUM_ROWS,
				    pDecInfo->lowDelayInfo.numRows);
		} else {
			VpuWriteReg(pCodecInst->coreIdx, CMD_DEC_PIC_NUM_ROWS,
				    0);
		}
	}

	val = 0;

	val = ((pDecInfo->secAxiInfo.u.coda9.useBitEnable & 0x01) << 0 |
	       (pDecInfo->secAxiInfo.u.coda9.useIpEnable & 0x01) << 1 |
	       (pDecInfo->secAxiInfo.u.coda9.useDbkYEnable & 0x01) << 2 |
	       (pDecInfo->secAxiInfo.u.coda9.useDbkCEnable & 0x01) << 3 |
	       (pDecInfo->secAxiInfo.u.coda9.useOvlEnable & 0x01) << 4 |
	       (pDecInfo->secAxiInfo.u.coda9.useBtpEnable & 0x01) << 5 |
	       (pDecInfo->secAxiInfo.u.coda9.useBitEnable & 0x01) << 8 |
	       (pDecInfo->secAxiInfo.u.coda9.useIpEnable & 0x01) << 9 |
	       (pDecInfo->secAxiInfo.u.coda9.useDbkYEnable & 0x01) << 10 |
	       (pDecInfo->secAxiInfo.u.coda9.useDbkCEnable & 0x01) << 11 |
	       (pDecInfo->secAxiInfo.u.coda9.useOvlEnable & 0x01) << 12 |
	       (pDecInfo->secAxiInfo.u.coda9.useBtpEnable & 0x01) << 13);

	VpuWriteReg(pCodecInst->coreIdx, BIT_AXI_SRAM_USE, val);

	VpuWriteReg(pCodecInst->coreIdx, pDecInfo->streamWrPtrRegAddr,
		    pDecInfo->streamWrPtr);
	VpuWriteReg(pCodecInst->coreIdx, pDecInfo->streamRdPtrRegAddr,
		    pDecInfo->streamRdPtr);

	pDecInfo->streamEndflag &= ~(3 << 3);
	if (pDecInfo->openParam.bitstreamMode ==
	    BS_MODE_ROLLBACK) //rollback mode
		pDecInfo->streamEndflag |= (1 << 3);
	else if (pDecInfo->openParam.bitstreamMode == BS_MODE_PIC_END)
		pDecInfo->streamEndflag |= (2 << 3);

	VpuWriteReg(pCodecInst->coreIdx, BIT_BIT_STREAM_PARAM,
		    pDecInfo->streamEndflag);

	if (pCodecInst->productId == PRODUCT_ID_980) {
		val = 0;
		val |= (pDecInfo->openParam.bwbEnable << 15);
		val |= (pDecInfo->wtlMode << 17) |
		       (pDecInfo->tiled2LinearMode << 13) |
		       (pDecInfo->mapType << 9) | (FORMAT_420 << 6);
		if (pDecInfo->openParam.cbcrInterleave == 1)
			val |= pDecInfo->openParam.nv21 << 3;
	} else if (pCodecInst->productId == PRODUCT_ID_960) {
		val = 0;
		val |= (pDecInfo->wtlEnable << 17);
		val |= (pDecInfo->openParam.bwbEnable << 12);
		if (pDecInfo->mapType) {
			if (pDecInfo->mapType == TILED_FRAME_MB_RASTER_MAP ||
			    pDecInfo->mapType == TILED_FIELD_MB_RASTER_MAP)
				val |= (pDecInfo->tiled2LinearEnable << 11) |
				       (0x03 << 9) | (FORMAT_420 << 6);
			else
				val |= (pDecInfo->tiled2LinearEnable << 11) |
				       (0x02 << 9) | (FORMAT_420 << 6);
		}
	} else {
		return RETCODE_NOT_FOUND_VPU_DEVICE;
	}

	val |= ((pDecInfo->openParam.cbcrInterleave)
		<< 2); // Interleave bit position is modified
	val |= pDecInfo->openParam.frameEndian;
	VpuWriteReg(pCodecInst->coreIdx, BIT_FRAME_MEM_CTRL, val);

	val = pDecInfo->openParam.streamEndian;
	VpuWriteReg(pCodecInst->coreIdx, BIT_BIT_STREAM_CTRL, val);

	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_DELAY,
		    pDecInfo->frameDelay); // SA5-1256

	Coda9BitIssueCommand(pCodecInst->coreIdx, pCodecInst, PIC_RUN);

#ifdef GET_PERFORMANCE
	gettimeofday(&start_dec_tv, NULL);
#endif
	return RETCODE_SUCCESS;
}

RetCode Coda9VpuDecGetResult(CodecInst *instance, DecOutputInfo *result)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	Uint32 val = 0;

	pCodecInst = instance;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	if (pCodecInst->loggingEnable)
		vdi_log(pCodecInst->coreIdx, PIC_RUN, 0);

	result->warnInfo = 0;
	val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_SUCCESS);
	result->decodingSuccess = val;
	if (result->decodingSuccess & (1 << 31)) {
		result->wprotErrReason =
			VpuReadReg(pCodecInst->coreIdx, GDI_WPROT_ERR_RSN);
		result->wprotErrAddress =
			VpuReadReg(pCodecInst->coreIdx, GDI_WPROT_ERR_ADR);
		return RETCODE_MEMORY_ACCESS_VIOLATION;
	}

	if (pCodecInst->codecMode == AVC_DEC) {
		result->notSufficientPsBuffer = (val >> 3) & 0x1;
		result->notSufficientSliceBuffer = (val >> 2) & 0x1;
	}

	result->chunkReuseRequired = 0;
	if (pDecInfo->openParam.bitstreamMode == BS_MODE_PIC_END) {
		switch (pCodecInst->codecMode) {
		case AVC_DEC:
			result->chunkReuseRequired =
				((val >> 16) & 0x01); // in case of NPF frame
			val = VpuReadReg(pCodecInst->coreIdx,
					 RET_DEC_PIC_DECODED_IDX);
			if (val == (Uint32)-1) {
				result->chunkReuseRequired = TRUE;
			}
			break;
		case MP2_DEC:
		case MP4_DEC:
			result->chunkReuseRequired = ((val >> 16) & 0x01);
			break;
		default:
			break;
		}
	}

	result->indexFrameDecoded =
		VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_DECODED_IDX);
	result->indexFrameDisplay =
		VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_DISPLAY_IDX);
	if (pDecInfo->mapType == LINEAR_FRAME_MAP) {
		result->indexFrameDecodedForTiled = -1;
		result->indexFrameDisplayForTiled = -1;
	} else {
		result->indexFrameDecodedForTiled = result->indexFrameDecoded;
		result->indexFrameDisplayForTiled = result->indexFrameDisplay;
	}

	val = VpuReadReg(pCodecInst->coreIdx,
			 RET_DEC_PIC_SIZE); // decoding picture size
	result->decPicWidth = (val >> 16) & 0xFFFF;
	result->decPicHeight = (val)&0xFFFF;

	if (result->indexFrameDecoded >= 0 &&
	    result->indexFrameDecoded < MAX_GDI_IDX) {
		switch (pCodecInst->codecMode) {
		case VPX_DEC:
			if (pCodecInst->codecModeAux == VPX_AUX_VP8) {
				// VP8 specific header information
				// h_scale[31:30] v_scale[29:28] pic_width[27:14] pic_height[13:0]
				val = VpuReadReg(pCodecInst->coreIdx,
						 RET_DEC_PIC_VP8_SCALE_INFO);
				result->vp8ScaleInfo.hScaleFactor =
					(val >> 30) & 0x03;
				result->vp8ScaleInfo.vScaleFactor =
					(val >> 28) & 0x03;
				result->vp8ScaleInfo.picWidth =
					(val >> 14) & 0x3FFF;
				result->vp8ScaleInfo.picHeight =
					(val >> 0) & 0x3FFF;
				// ref_idx_gold[31:24], ref_idx_altr[23:16], ref_idx_last[15: 8],
				// version_number[3:1], show_frame[0]
				val = VpuReadReg(pCodecInst->coreIdx,
						 RET_DEC_PIC_VP8_PIC_REPORT);
				result->vp8PicInfo.refIdxGold =
					(val >> 24) & 0x0FF;
				result->vp8PicInfo.refIdxAltr =
					(val >> 16) & 0x0FF;
				result->vp8PicInfo.refIdxLast =
					(val >> 8) & 0x0FF;
				result->vp8PicInfo.versionNumber =
					(val >> 1) & 0x07;
				result->vp8PicInfo.showFrame =
					(val >> 0) & 0x01;
			}
			break;
		case AVC_DEC:
		case AVS_DEC: /* RTK need to check*/
			val = VpuReadReg(
				pCodecInst->coreIdx,
				RET_DEC_PIC_CROP_LEFT_RIGHT); // frame crop information(left, right)
			pDecInfo->initialInfo.picCropRect.left =
				(val >> 16) & 0xffff;
			pDecInfo->initialInfo.picCropRect.right =
				pDecInfo->initialInfo.picWidth - (val & 0xffff);
			val = VpuReadReg(
				pCodecInst->coreIdx,
				RET_DEC_PIC_CROP_TOP_BOTTOM); // frame crop information(top, bottom)
			pDecInfo->initialInfo.picCropRect.top =
				(val >> 16) & 0xffff;
			pDecInfo->initialInfo.picCropRect.bottom =
				pDecInfo->initialInfo.picHeight -
				(val & 0xffff);
			break;
		case MP2_DEC:
			val = VpuReadReg(pCodecInst->coreIdx,
					 RET_DEC_SEQ_MP2_BAR_LEFT_RIGHT);
			pDecInfo->initialInfo.mp2BardataInfo.barLeft =
				((val >> 16) & 0xFFFF);
			pDecInfo->initialInfo.mp2BardataInfo.barRight =
				(val & 0xFFFF);
			val = VpuReadReg(pCodecInst->coreIdx,
					 RET_DEC_SEQ_MP2_BAR_TOP_BOTTOM);
			pDecInfo->initialInfo.mp2BardataInfo.barTop =
				((val >> 16) & 0xFFFF);
			pDecInfo->initialInfo.mp2BardataInfo.barBottom =
				(val & 0xFFFF);
			result->mp2BardataInfo =
				pDecInfo->initialInfo.mp2BardataInfo;

			result->mp2PicDispExtInfo.offsetNum =
				VpuReadReg(pCodecInst->coreIdx,
					   RET_DEC_PIC_MP2_OFFSET_NUM);

			val = VpuReadReg(pCodecInst->coreIdx,
					 RET_DEC_PIC_MP2_OFFSET1);
			result->mp2PicDispExtInfo.horizontalOffset1 =
				(Int16)(val >> 16) & 0xFFFF;
			result->mp2PicDispExtInfo.verticalOffset1 =
				(Int16)(val & 0xFFFF);

			val = VpuReadReg(pCodecInst->coreIdx,
					 RET_DEC_PIC_MP2_OFFSET2);
			result->mp2PicDispExtInfo.horizontalOffset2 =
				(Int16)(val >> 16) & 0xFFFF;
			result->mp2PicDispExtInfo.verticalOffset2 =
				(Int16)(val & 0xFFFF);

			val = VpuReadReg(pCodecInst->coreIdx,
					 RET_DEC_PIC_MP2_OFFSET3);
			result->mp2PicDispExtInfo.horizontalOffset3 =
				(Int16)(val >> 16) & 0xFFFF;
			result->mp2PicDispExtInfo.verticalOffset3 =
				(Int16)(val & 0xFFFF);
			break;
		}
	}

	val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_TYPE);
	result->interlacedFrame = (val >> 18) & 0x1;
	result->topFieldFirst = (val >> 21) & 0x0001; // TopFieldFirst[21]
	if (result->interlacedFrame) {
		result->picTypeFirst =
			(val & 0x38) >> 3; // pic_type of 1st field
		result->picType = val & 7; // pic_type of 2nd field
	} else {
		result->picTypeFirst = PIC_TYPE_MAX; // no meaning
		result->picType = val & 7;
	}

	result->pictureStructure =
		(val >> 19) & 0x0003; // MbAffFlag[17], FieldPicFlag[16]
	result->repeatFirstField = (val >> 22) & 0x0001;
	result->progressiveFrame = (val >> 23) & 0x0003;

	if (pCodecInst->codecMode == AVC_DEC) {
		result->nalRefIdc =
			(val >> 7) & 0x0003; // RTHA-152, gregory add
		result->decFrameInfo = (val >> 15) & 0x0001;
		result->picStrPresent = (val >> 27) & 0x0001;
		result->picTimingStruct = (val >> 28) & 0x000f;
		//update picture type when IDR frame
		if (val & 0x40) { // 6th bit
			if (result->interlacedFrame)
				result->picTypeFirst = PIC_TYPE_IDR;
			else
				result->picType = PIC_TYPE_IDR;
		}
		result->decFrameInfo = (val >> 16) & 0x0003;
		if (result->indexFrameDisplay >= 0) {
			if (result->indexFrameDisplay ==
			    result->indexFrameDecoded)
				result->avcNpfFieldInfo = result->decFrameInfo;
			else
				result->avcNpfFieldInfo =
					pDecInfo->decOutInfo
						[result->indexFrameDisplay]
							.decFrameInfo;
		}
		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_HRD_INFO);
		result->avcHrdInfo.cpbMinus1 = val >> 2;
		result->avcHrdInfo.vclHrdParamFlag = (val >> 1) & 1;
		result->avcHrdInfo.nalHrdParamFlag = val & 1;

		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_VUI_INFO);
		result->avcVuiInfo.fixedFrameRateFlag = val & 1;
		result->avcVuiInfo.timingInfoPresent = (val >> 1) & 0x01;
		result->avcVuiInfo.chromaLocBotField = (val >> 2) & 0x07;
		result->avcVuiInfo.chromaLocTopField = (val >> 5) & 0x07;
		result->avcVuiInfo.chromaLocInfoPresent = (val >> 8) & 0x01;
		result->avcVuiInfo.colorPrimaries = (val >> 16) & 0xff;
		result->avcVuiInfo.colorDescPresent = (val >> 24) & 0x01;
		result->avcVuiInfo.isExtSAR = (val >> 25) & 0x01;
		result->avcVuiInfo.vidFullRange = (val >> 26) & 0x01;
		result->avcVuiInfo.vidFormat = (val >> 27) & 0x07;
		result->avcVuiInfo.vidSigTypePresent = (val >> 30) & 0x01;
		result->avcVuiInfo.vuiParamPresent = (val >> 31) & 0x01;
		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_VUI_INFO_2);
		result->avcVuiInfo.vuiMatrixCoefficients = val & 0xff;
		result->avcVuiInfo.vuiTransferCharacteristics =
			(val >> 8) & 0xff;
		val = VpuReadReg(pCodecInst->coreIdx,
				 RET_DEC_PIC_VUI_PIC_STRUCT);
		result->avcVuiInfo.vuiPicStructPresent = (val & 0x1);
		result->avcVuiInfo.vuiPicStruct = (val >> 1);
	}

	if (pCodecInst->codecMode == MP2_DEC) {
		result->fieldSequence = (val >> 25) & 0x0007;
		result->frameDct = (val >> 28) & 0x0001;
		result->progressiveSequence = (val >> 29) & 0x0001;
		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_VUI_INFO_2);
		result->mp2ColorPrimaries = (val >> 16) & 0xff;
		result->mp2TransferChar = (val >> 8) & 0xff;
		result->mp2MatrixCoeff = val & 0xff;
	}

	result->fRateNumerator = VpuReadReg(
		pCodecInst->coreIdx,
		RET_DEC_PIC_FRATE_NR); //Frame rate, Aspect ratio can be changed frame by frame.
	result->fRateDenominator =
		VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_FRATE_DR);
	if (pCodecInst->codecMode == AVC_DEC && result->fRateDenominator > 0)
		result->fRateDenominator *= 2;
	if (pCodecInst->codecMode == MP4_DEC) {
		result->mp4ModuloTimeBase = VpuReadReg(
			pCodecInst->coreIdx, RET_DEC_PIC_MODULO_TIME_BASE);
		result->mp4TimeIncrement = VpuReadReg(
			pCodecInst->coreIdx, RET_DEC_PIC_VOP_TIME_INCREMENT);
	}

	if (pCodecInst->codecMode == RV_DEC) {
		result->rvTr =
			VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_RV_TR);
		result->rvTrB = VpuReadReg(pCodecInst->coreIdx,
					   RET_DEC_PIC_RV_TR_BFRAME);
	}

	if (pCodecInst->codecMode == VPX_DEC) {
		result->aspectRateInfo = 0;
	} else {
		result->aspectRateInfo =
			VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_ASPECT);
	}

	// User Data
	if (pDecInfo->userDataEnable) {
		int userDataNum;
		int userDataSize;
		BYTE tempBuf[8] = {
			0,
		};

		if (!pCodecInst->isUseProtectBuffer)
			VpuReadMem(pCodecInst->coreIdx,
				   pDecInfo->userDataBufAddr + 0, tempBuf, 8,
				   VPU_USER_DATA_ENDIAN);

		val = ((tempBuf[0] << 24) & 0xFF000000) |
		      ((tempBuf[1] << 16) & 0x00FF0000) |
		      ((tempBuf[2] << 8) & 0x0000FF00) |
		      ((tempBuf[3] << 0) & 0x000000FF);

		userDataNum = (val >> 16) & 0xFFFF;
		userDataSize = (val >> 0) & 0xFFFF;
		if (userDataNum == 0)
			userDataSize = 0;

		result->decOutputExtData.userDataNum = userDataNum;
		result->decOutputExtData.userDataSize = userDataSize;

		val = ((tempBuf[4] << 24) & 0xFF000000) |
		      ((tempBuf[5] << 16) & 0x00FF0000) |
		      ((tempBuf[6] << 8) & 0x0000FF00) |
		      ((tempBuf[7] << 0) & 0x000000FF);

		if (userDataNum == 0)
			result->decOutputExtData.userDataBufFull = 0;
		else
			result->decOutputExtData.userDataBufFull =
				(val >> 16) & 0xFFFF;

		result->decOutputExtData.activeFormat =
			VpuReadReg(pCodecInst->coreIdx,
				   RET_DEC_PIC_ATSC_USER_DATA_INFO) &
			0xf;
	}

	result->numOfErrMBs =
		VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_ERR_MB);
	val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_SUCCESS);
	result->sequenceChanged = ((val >> 20) & 0x1);
	result->streamEndFlag = ((pDecInfo->streamEndflag >> 2) & 0x01);

	if (pCodecInst->codecMode == AVS_DEC) {
		Uint32 val = (Uint32)result->numOfErrMBs;
		result->numOfErrMBs = (int)(val & 0x00ffffff);
		result->errorReason = (val >> 24) & 0xff;
	}
	if (pCodecInst->codecMode == VC1_DEC &&
	    result->indexFrameDisplay != -3) {
		if (pDecInfo->vc1BframeDisplayValid == 0) {
			if (result->picType == 2) {
				result->indexFrameDisplay = -3;
			} else {
				pDecInfo->vc1BframeDisplayValid = 1;
			}
		}
	}
	if (pCodecInst->codecMode == AVC_DEC &&
	    pCodecInst->codecModeAux == AVC_AUX_MVC) {
		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_MVC_REPORT);
		result->mvcPicInfo.viewIdxDisplay = (val >> 0) & 1;
		result->mvcPicInfo.viewIdxDecoded = (val >> 1) & 1;
	}

	if (pCodecInst->codecMode == AVC_DEC) {
		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_AVC_FPA_SEI0);

		if ((int)val < 0) {
			result->avcFpaSei.exist = 0;
		} else {
			result->avcFpaSei.exist = 1;
			result->avcFpaSei.framePackingArrangementId = val;

			val = VpuReadReg(pCodecInst->coreIdx,
					 RET_DEC_PIC_AVC_FPA_SEI1);
			result->avcFpaSei.contentInterpretationType =
				val & 0x3F; // [5:0]
			result->avcFpaSei.framePackingArrangementType =
				(val >> 6) & 0x7F; // [12:6]
			result->avcFpaSei.framePackingArrangementExtensionFlag =
				(val >> 13) & 0x01; // [13]
			result->avcFpaSei.frame1SelfContainedFlag =
				(val >> 14) & 0x01; // [14]
			result->avcFpaSei.frame0SelfContainedFlag =
				(val >> 15) & 0x01; // [15]
			result->avcFpaSei.currentFrameIsFrame0Flag =
				(val >> 16) & 0x01; // [16]
			result->avcFpaSei.fieldViewsFlag =
				(val >> 17) & 0x01; // [17]
			result->avcFpaSei.frame0FlippedFlag =
				(val >> 18) & 0x01; // [18]
			result->avcFpaSei.spatialFlippingFlag =
				(val >> 19) & 0x01; // [19]
			result->avcFpaSei.quincunxSamplingFlag =
				(val >> 20) & 0x01; // [20]
			result->avcFpaSei.framePackingArrangementCancelFlag =
				(val >> 21) & 0x01; // [21]

			val = VpuReadReg(pCodecInst->coreIdx,
					 RET_DEC_PIC_AVC_FPA_SEI2);
			result->avcFpaSei
				.framePackingArrangementRepetitionPeriod =
				val & 0x7FFF; // [14:0]
			result->avcFpaSei.frame1GridPositionY =
				(val >> 16) & 0x0F; // [19:16]
			result->avcFpaSei.frame1GridPositionX =
				(val >> 20) & 0x0F; // [23:20]
			result->avcFpaSei.frame0GridPositionY =
				(val >> 24) & 0x0F; // [27:24]
			result->avcFpaSei.frame0GridPositionX =
				(val >> 28) & 0x0F; // [31:28]
		}

		result->avcPocTop =
			VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_POC_TOP);
		result->avcPocBot =
			VpuReadReg(pCodecInst->coreIdx, RET_DEC_PIC_POC_BOT);

		if (result->interlacedFrame) {
			if (result->avcPocTop > result->avcPocBot) {
				result->avcPocPic = result->avcPocBot;
			} else {
				result->avcPocPic = result->avcPocTop;
			}
		} else
			result->avcPocPic = VpuReadReg(pCodecInst->coreIdx,
						       RET_DEC_PIC_POC);
	}

	if (pCodecInst->codecMode == AVC_DEC) {
		val = VpuReadReg(pCodecInst->coreIdx,
				 RET_DEC_PIC_AVC_SEI_RP_INFO);

		if ((int)val < 0) {
			result->avcRpSei.exist = 0;
		} else {
			result->avcRpSei.exist = 1;
			result->avcRpSei.changingSliceGroupIdc =
				val & 0x3; // [1:0]
			result->avcRpSei.brokenLinkFlag =
				(val >> 2) & 0x01; // [2]
			result->avcRpSei.exactMatchFlag =
				(val >> 3) & 0x01; // [3]
			result->avcRpSei.recoveryFrameCnt =
				(val >> 4) & 0x3F; // [9:4]
		}
	}

	result->bytePosFrameStart =
		VpuReadReg(pCodecInst->coreIdx, BIT_BYTE_POS_FRAME_START);
	result->bytePosFrameEnd =
		VpuReadReg(pCodecInst->coreIdx, BIT_BYTE_POS_FRAME_END);

	if (result->indexFrameDecoded >= 0 &&
	    result->indexFrameDecoded < MAX_GDI_IDX)
		pDecInfo->decOutInfo[result->indexFrameDecoded] = *result;

	result->frameDisplayFlag = pDecInfo->frameDisplayFlag;
	result->frameCycle = VpuReadReg(pCodecInst->coreIdx, BIT_FRAME_CYCLE);

	return RETCODE_SUCCESS;
}

RetCode Coda9VpuDecSetBitstreamFlag(CodecInst *instance, BOOL running, BOOL eos)
{
	Uint32 val;
	DecInfo *pDecInfo;

	pDecInfo = &instance->CodecInfo->decInfo;

	if (eos & 0x01) {
		val = VpuReadReg(instance->coreIdx, BIT_BIT_STREAM_PARAM);
		val |= 1 << 2;
		pDecInfo->streamEndflag = val;
		if (running == TRUE)
			VpuWriteReg(instance->coreIdx, BIT_BIT_STREAM_PARAM,
				    val);
		return RETCODE_SUCCESS;
	} else {
		val = VpuReadReg(instance->coreIdx, BIT_BIT_STREAM_PARAM);
		val &= ~(1 << 2);
		pDecInfo->streamEndflag = val;
		if (running == TRUE)
			VpuWriteReg(instance->coreIdx, BIT_BIT_STREAM_PARAM,
				    val);

		return RETCODE_SUCCESS;
	}
}

RetCode Coda9VpuDecCpbFlush(CodecInst *instance)
{
	Uint32 val;
	DecInfo *pDecInfo;

	pDecInfo = &instance->CodecInfo->decInfo;

	if (pDecInfo->openParam.bitstreamMode != BS_MODE_INTERRUPT) {
		return RETCODE_INVALID_COMMAND;
	}

	val = VpuReadReg(instance->coreIdx, BIT_BIT_STREAM_PARAM);
	val &= ~(3 << 3);
	val |= (2 << 3); // set to pic_end mode
	VpuWriteReg(instance->coreIdx, BIT_BIT_STREAM_PARAM, val);

	return RETCODE_SUCCESS;
}

RetCode Coda9VpuDecGetSeqInfo(CodecInst *instance, DecInitialInfo *info)
{
	CodecInst *pCodecInst = NULL;
	DecInfo *pDecInfo = NULL;
	Uint32 val, val2;

	pCodecInst = instance;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	if (pCodecInst->loggingEnable) {
		vdi_log(pCodecInst->coreIdx, DEC_SEQ_INIT, 0);
	}

	info->warnInfo = 0;
	if (pDecInfo->openParam.bitstreamMode == BS_MODE_INTERRUPT &&
	    pDecInfo->seqInitEscape) {
		pDecInfo->streamEndflag &= ~(3 << 3);
		VpuWriteReg(pCodecInst->coreIdx, BIT_BIT_STREAM_PARAM,
			    pDecInfo->streamEndflag);
		pDecInfo->seqInitEscape = 0;
	}
	pDecInfo->streamRdPtr =
		VpuReadReg(instance->coreIdx, pDecInfo->streamRdPtrRegAddr);
	pDecInfo->frameDisplayFlag = VpuReadReg(
		pCodecInst->coreIdx, pDecInfo->frameDisplayFlagRegAddr);
	pDecInfo->streamEndflag =
		VpuReadReg(pCodecInst->coreIdx, BIT_BIT_STREAM_PARAM);

	info->seqInitErrReason = 0;
	val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_SUCCESS);
	if (val & (1 << 31)) {
		return RETCODE_MEMORY_ACCESS_VIOLATION;
	}

	if (pDecInfo->openParam.bitstreamMode == BS_MODE_PIC_END ||
	    pDecInfo->openParam.bitstreamMode == BS_MODE_ROLLBACK) {
		if (val & (1 << 4)) {
			info->seqInitErrReason =
				(VpuReadReg(pCodecInst->coreIdx,
					    RET_DEC_SEQ_SEQ_ERR_REASON));
			return RETCODE_FAILURE;
		}
	}

	if (val == 0) {
		info->seqInitErrReason = VpuReadReg(pCodecInst->coreIdx,
						    RET_DEC_SEQ_SEQ_ERR_REASON);
		return RETCODE_FAILURE;
	}

	val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_SRC_SIZE);
	info->picWidth = ((val >> 16) & 0xffff);
	info->picHeight = (val & 0xffff);
	info->lumaBitdepth = 8;
	info->chromaBitdepth = 8;
	info->fRateNumerator =
		VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_FRATE_NR);
	info->fRateDenominator =
		VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_FRATE_DR);
	if (pCodecInst->codecMode == AVC_DEC && info->fRateDenominator > 0) {
		info->fRateDenominator *= 2;
	}

	if (pCodecInst->codecMode == MP4_DEC) {
		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_INFO);
		info->mp4ShortVideoHeader = (val >> 2) & 1;
		info->mp4DataPartitionEnable = val & 1;
		info->mp4ReversibleVlcEnable =
			info->mp4DataPartitionEnable ? ((val >> 1) & 1) : 0;
		info->h263AnnexJEnable = (val >> 3) & 1;
	} else if (pCodecInst->codecMode == VPX_DEC &&
		   pCodecInst->codecModeAux == VPX_AUX_VP8) {
		// h_scale[31:30] v_scale[29:28] pic_width[27:14] pic_height[13:0]
		val = VpuReadReg(pCodecInst->coreIdx,
				 RET_DEC_SEQ_VP8_SCALE_INFO);
		info->vp8ScaleInfo.hScaleFactor = (val >> 30) & 0x03;
		info->vp8ScaleInfo.vScaleFactor = (val >> 28) & 0x03;
		info->vp8ScaleInfo.picWidth = (val >> 14) & 0x3FFF;
		info->vp8ScaleInfo.picHeight = (val >> 0) & 0x3FFF;
	}

	info->minFrameBufferCount =
		VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_FRAME_NEED);

	info->frameBufDelay =
		VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_FRAME_DELAY);

	/* RTK need to check */
	if (pCodecInst->codecMode == AVC_DEC ||
	    pCodecInst->codecMode == MP2_DEC ||
	    pCodecInst->codecMode == AVS_DEC) {
		val = VpuReadReg(pCodecInst->coreIdx,
				 RET_DEC_SEQ_CROP_LEFT_RIGHT);
		val2 = VpuReadReg(pCodecInst->coreIdx,
				  RET_DEC_SEQ_CROP_TOP_BOTTOM);

		info->picCropRect.left = ((val >> 16) & 0xFFFF);
		info->picCropRect.right = info->picWidth - (val & 0xFFFF);
		;
		info->picCropRect.top = ((val2 >> 16) & 0xFFFF);
		info->picCropRect.bottom = info->picHeight - (val2 & 0xFFFF);

		val = (info->picWidth * info->picHeight * 3 / 2) / 1024;
		info->normalSliceSize = val / 4;
		info->worstSliceSize = val / 2;
	} else {
		info->picCropRect.left = 0;
		info->picCropRect.right = info->picWidth;
		info->picCropRect.top = 0;
		info->picCropRect.bottom = info->picHeight;
	}

	if (pCodecInst->codecMode == MP2_DEC) {
		val = VpuReadReg(pCodecInst->coreIdx,
				 RET_DEC_SEQ_MP2_BAR_LEFT_RIGHT);
		val2 = VpuReadReg(pCodecInst->coreIdx,
				  RET_DEC_SEQ_MP2_BAR_TOP_BOTTOM);

		info->mp2BardataInfo.barLeft = ((val >> 16) & 0xFFFF);
		info->mp2BardataInfo.barRight = (val & 0xFFFF);
		info->mp2BardataInfo.barTop = ((val2 >> 16) & 0xFFFF);
		info->mp2BardataInfo.barBottom = (val2 & 0xFFFF);
	}

	val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_HEADER_REPORT);
	info->profile = (val >> 0) & 0xFF;
	info->level = (val >> 8) & 0xFF;
	info->interlace = (val >> 16) & 0x01;
	info->direct8x8Flag = (val >> 17) & 0x01;
	info->vc1Psf = (val >> 18) & 0x01;
	info->constraint_set_flag[0] = (val >> 19) & 0x01;
	info->constraint_set_flag[1] = (val >> 20) & 0x01;
	info->constraint_set_flag[2] = (val >> 21) & 0x01;
	info->constraint_set_flag[3] = (val >> 22) & 0x01;
	info->chromaFormatIDC = (val >> 23) & 0x03;
	info->isExtSAR = (val >> 25) & 0x01;
	info->maxNumRefFrm = (val >> 27) & 0x0f;
	info->maxNumRefFrmFlag = (val >> 31) & 0x01;
	val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_ASPECT);
	info->aspectRateInfo = val;

	val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_BIT_RATE);
	info->bitRate = val;

	if (pCodecInst->codecMode == AVC_DEC) {
		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_VUI_INFO);
		info->avcVuiInfo.fixedFrameRateFlag = val & 1;
		info->avcVuiInfo.timingInfoPresent = (val >> 1) & 0x01;
		info->avcVuiInfo.chromaLocBotField = (val >> 2) & 0x07;
		info->avcVuiInfo.chromaLocTopField = (val >> 5) & 0x07;
		info->avcVuiInfo.chromaLocInfoPresent = (val >> 8) & 0x01;
		info->avcVuiInfo.colorPrimaries = (val >> 16) & 0xff;
		info->avcVuiInfo.colorDescPresent = (val >> 24) & 0x01;
		info->avcVuiInfo.isExtSAR = (val >> 25) & 0x01;
		info->avcVuiInfo.vidFullRange = (val >> 26) & 0x01;
		info->avcVuiInfo.vidFormat = (val >> 27) & 0x07;
		info->avcVuiInfo.vidSigTypePresent = (val >> 30) & 0x01;
		info->avcVuiInfo.vuiParamPresent = (val >> 31) & 0x01;

		val = VpuReadReg(pCodecInst->coreIdx,
				 RET_DEC_SEQ_VUI_PIC_STRUCT);
		info->avcVuiInfo.vuiPicStructPresent = (val & 0x1);
		info->avcVuiInfo.vuiPicStruct = (val >> 1);
		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_VUI_INFO_2);
		info->avcVuiInfo.vuiMatrixCoefficients = val & 0xff;
		info->avcVuiInfo.vuiTransferCharacteristics = (val >> 8) & 0xff;
	}

	if (pCodecInst->codecMode == MP2_DEC) {
		// seq_ext info
		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_EXT_INFO);
		info->mp2LowDelay = val & 1;
		info->mp2DispVerSize = (val >> 1) & 0x3fff;
		info->mp2DispHorSize = (val >> 15) & 0x3fff;

		if (pDecInfo->userDataEnable) {
			Uint32 userDataNum = 0;
			Uint32 userDataSize = 0;
			BYTE tempBuf[8] = {
				0,
			};

			// user data
			VpuReadMem(pCodecInst->coreIdx,
				   pDecInfo->userDataBufAddr, tempBuf, 8,
				   VPU_USER_DATA_ENDIAN);

			val = ((tempBuf[0] << 24) & 0xFF000000) |
			      ((tempBuf[1] << 16) & 0x00FF0000) |
			      ((tempBuf[2] << 8) & 0x0000FF00) |
			      ((tempBuf[3] << 0) & 0x000000FF);

			userDataNum = (val >> 16) & 0xFFFF;
			userDataSize = (val >> 0) & 0xFFFF;
			if (userDataNum == 0) {
				userDataSize = 0;
			}

			info->userDataNum = userDataNum;
			info->userDataSize = userDataSize;

			val = ((tempBuf[4] << 24) & 0xFF000000) |
			      ((tempBuf[5] << 16) & 0x00FF0000) |
			      ((tempBuf[6] << 8) & 0x0000FF00) |
			      ((tempBuf[7] << 0) & 0x000000FF);

			if (userDataNum == 0) {
				info->userDataBufFull = 0;
			} else {
				info->userDataBufFull = (val >> 16) & 0xFFFF;
			}
		}
		val = VpuReadReg(pCodecInst->coreIdx, RET_DEC_SEQ_VUI_INFO_2);
		info->mp2ColorPrimaries = (val >> 16) & 0xff;
		info->mp2TransferChar = (val >> 8) & 0xff;
		info->mp2MatrixCoeff = val & 0xff;
	}

	return RETCODE_SUCCESS;
}

RetCode Coda9VpuDecRegisterFramebuffer(CodecInst *instance)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	PhysicalAddress paraBuffer;
	PhysicalAddress tempBuffer;
	vpu_buffer_t vb;
	Uint32 val;
	int i;
	BYTE frameAddr[MAX_GDI_IDX][3][4];
	BYTE colMvAddr[MAX_GDI_IDX][4];

	osal_memset((void *)&vb, 0, sizeof(vpu_buffer_t));
	osal_memset((void *)frameAddr, 0, sizeof(frameAddr));
	osal_memset((void *)colMvAddr, 0, sizeof(colMvAddr));
	pCodecInst = instance;
	pDecInfo = &pCodecInst->CodecInfo->decInfo;

	if (pCodecInst->isUseProtectBuffer) //RTK, need to review it
	{
		Coda9VpuSetVeProtMode(pCodecInst->coreIdx, TRUE,
				      pCodecInst->sess, pCodecInst->rtk_sess);
		vdi_get_common_memory_protect(pCodecInst->coreIdx, &vb,
					      pCodecInst->filp);
	} else {
		Coda9VpuSetVeProtMode(pCodecInst->coreIdx, FALSE, NULL, NULL);
		vdi_get_common_memory(pCodecInst->coreIdx, &vb);
	}
	tempBuffer = vb.phys_addr + CODE_BUF_SIZE;
	paraBuffer = tempBuffer + TEMP_BUF_SIZE;
	VpuWriteReg(pCodecInst->coreIdx, BIT_PARA_BUF_ADDR, paraBuffer);
	VpuWriteReg(pCodecInst->coreIdx, BIT_TEMP_BUF_ADDR, tempBuffer);

	pDecInfo->mapCfg.productId = pCodecInst->productId;

	if (pDecInfo->mapType != LINEAR_FRAME_MAP &&
	    pDecInfo->mapType != LINEAR_FIELD_MAP) {
		val = SetTiledMapType(
			pCodecInst->coreIdx, &pDecInfo->mapCfg,
			pDecInfo->mapType,
			(pDecInfo->stride > pDecInfo->frameBufferHeight) ?
				pDecInfo->stride :
				pDecInfo->frameBufferHeight,
			pDecInfo->openParam.cbcrInterleave, &pDecInfo->dramCfg);
	} else {
		val = SetTiledMapType(pCodecInst->coreIdx, &pDecInfo->mapCfg,
				      pDecInfo->mapType, pDecInfo->stride,
				      pDecInfo->openParam.cbcrInterleave,
				      &pDecInfo->dramCfg);
	}

	if (val == 0) {
		return RETCODE_INVALID_PARAM;
	}

	//Allocate frame buffer
	for (i = 0; i < pDecInfo->numFbsForDecoding; i++) {
		frameAddr[i][0][0] =
			(pDecInfo->frameBufPool[i].bufY >> 24) & 0xFF;
		frameAddr[i][0][1] =
			(pDecInfo->frameBufPool[i].bufY >> 16) & 0xFF;
		frameAddr[i][0][2] =
			(pDecInfo->frameBufPool[i].bufY >> 8) & 0xFF;
		frameAddr[i][0][3] =
			(pDecInfo->frameBufPool[i].bufY >> 0) & 0xFF;
		if (pDecInfo->openParam.cbcrOrder == CBCR_ORDER_NORMAL) {
			frameAddr[i][1][0] =
				(pDecInfo->frameBufPool[i].bufCb >> 24) & 0xFF;
			frameAddr[i][1][1] =
				(pDecInfo->frameBufPool[i].bufCb >> 16) & 0xFF;
			frameAddr[i][1][2] =
				(pDecInfo->frameBufPool[i].bufCb >> 8) & 0xFF;
			frameAddr[i][1][3] =
				(pDecInfo->frameBufPool[i].bufCb >> 0) & 0xFF;
			frameAddr[i][2][0] =
				(pDecInfo->frameBufPool[i].bufCr >> 24) & 0xFF;
			frameAddr[i][2][1] =
				(pDecInfo->frameBufPool[i].bufCr >> 16) & 0xFF;
			frameAddr[i][2][2] =
				(pDecInfo->frameBufPool[i].bufCr >> 8) & 0xFF;
			frameAddr[i][2][3] =
				(pDecInfo->frameBufPool[i].bufCr >> 0) & 0xFF;
		} else {
			frameAddr[i][2][0] =
				(pDecInfo->frameBufPool[i].bufCb >> 24) & 0xFF;
			frameAddr[i][2][1] =
				(pDecInfo->frameBufPool[i].bufCb >> 16) & 0xFF;
			frameAddr[i][2][2] =
				(pDecInfo->frameBufPool[i].bufCb >> 8) & 0xFF;
			frameAddr[i][2][3] =
				(pDecInfo->frameBufPool[i].bufCb >> 0) & 0xFF;
			frameAddr[i][1][0] =
				(pDecInfo->frameBufPool[i].bufCr >> 24) & 0xFF;
			frameAddr[i][1][1] =
				(pDecInfo->frameBufPool[i].bufCr >> 16) & 0xFF;
			frameAddr[i][1][2] =
				(pDecInfo->frameBufPool[i].bufCr >> 8) & 0xFF;
			frameAddr[i][1][3] =
				(pDecInfo->frameBufPool[i].bufCr >> 0) & 0xFF;
		}
	}

	if (pCodecInst->isUseProtectBuffer) //RTK, need to review it
	{
#ifdef ENABLE_TEE_DRM_FLOW
		VLOG(TRACE,
		     "[%d]%s.Coda9VpuWriteMem paraBuffer:0x%x with frameAddr.numFbsForDecoding:%d.sizeof(frameAddr):%d\n",
		     __LINE__, __func__, paraBuffer,
		     pDecInfo->numFbsForDecoding, sizeof(frameAddr));
		Coda9VpuWriteMem(pCodecInst->coreIdx, paraBuffer,
				 (BYTE *)frameAddr, sizeof(frameAddr),
				 VDI_BIG_ENDIAN, pCodecInst->teeapi_ctx,
				 pCodecInst->teeapi_tee_session);
#else
		VpuWriteMem(pCodecInst->coreIdx, paraBuffer, (BYTE *)frameAddr,
			    sizeof(frameAddr), VDI_BIG_ENDIAN);
#endif
	} else {
		VLOG(TRACE,
		     "[%d]%s.VpuWriteMem paraBuffer:0x%x with frameAddr.numFbsForDecoding:%d.sizeof(frameAddr):%d\n",
		     __LINE__, __func__, paraBuffer,
		     pDecInfo->numFbsForDecoding, sizeof(frameAddr));
		VpuWriteMem(pCodecInst->coreIdx, paraBuffer, (BYTE *)frameAddr,
			    sizeof(frameAddr), VDI_BIG_ENDIAN);
	}

	// MV allocation and register
	if (pCodecInst->codecMode == AVC_DEC ||
	    pCodecInst->codecMode == VC1_DEC ||
	    pCodecInst->codecMode == MP4_DEC ||
	    pCodecInst->codecMode == RV_DEC ||
	    pCodecInst->codecMode == AVS_DEC) {
		int size_mvcolbuf;
		vpu_buffer_t vbBuffer;
		size_mvcolbuf = ((pDecInfo->initialInfo.picWidth + 31) & ~31) *
				((pDecInfo->initialInfo.picHeight + 31) & ~31);
		size_mvcolbuf = (size_mvcolbuf * 3) / 2;
		size_mvcolbuf = (size_mvcolbuf + 4) / 5;
		size_mvcolbuf = ((size_mvcolbuf + 7) / 8) * 8;
		vbBuffer.size = size_mvcolbuf;
		vbBuffer.phys_addr = 0;
		for (i = 0; i < pDecInfo->numFbsForDecoding; i++) {
			//ENABLE_TEE_DRM_FLOW
			if (pCodecInst->isUseProtectBuffer)
				vbBuffer.req_spec_region = VE_SECURE_PROTECTION;
			else
				vbBuffer.req_spec_region = 0;

#ifdef DAH_222_PREALLOC_MV_SLICE_BUFFER
			if (pDecInfo->vbMV[i].size == 0 ||
			    pDecInfo->vbMV[i].size < vbBuffer.size)
#else
			if (pDecInfo->vbMV[i].size == 0)
#endif
			{
#ifdef DAH_222_PREALLOC_MV_SLICE_BUFFER
				if (pDecInfo->vbMV[i].size > 0 &&
				    pDecInfo->vbMV[i].size < vbBuffer.size) {
					VLOG(TRACE,
					     "[%d]%s.vdi_free_dma_memory vbMV[%d](0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
					     __LINE__, __func__, i,
					     pDecInfo->vbMV[i].phys_addr,
					     pDecInfo->vbMV[i].base,
					     pDecInfo->vbMV[i].virt_addr,
					     pDecInfo->vbMV[i].size,
					     pDecInfo->vbMV[i].req_spec_region);
					vdi_free_dma_memory(pCodecInst->coreIdx,
							    &pDecInfo->vbMV[i]);
				}
#endif
				if (vdi_allocate_dma_memory_no_mmap(
					    pCodecInst->coreIdx, &vbBuffer,
					    pCodecInst->filp) < 0) {

					return RETCODE_FAILURE;
				}
				pDecInfo->vbMV[i] = vbBuffer;
				VLOG(TRACE,
				     "[%d]%s.vdi_allocate_dma_memory_no_mmap vbMV[%d](0x%lx,0x%lx,0x%lx,%d,%d)\n",
				     __LINE__, __func__, i,
				     pDecInfo->vbMV[i].phys_addr,
				     pDecInfo->vbMV[i].base,
				     pDecInfo->vbMV[i].virt_addr,
				     pDecInfo->vbMV[i].size,
				     pDecInfo->vbMV[i].req_spec_region);
			}
		}
		if (pCodecInst->codecMode == AVC_DEC) {
			for (i = 0; i < pDecInfo->numFbsForDecoding; i++) {
				colMvAddr[i][0] =
					(pDecInfo->vbMV[i].phys_addr >> 24) &
					0xFF;
				colMvAddr[i][1] =
					(pDecInfo->vbMV[i].phys_addr >> 16) &
					0xFF;
				colMvAddr[i][2] =
					(pDecInfo->vbMV[i].phys_addr >> 8) &
					0xFF;
				colMvAddr[i][3] =
					(pDecInfo->vbMV[i].phys_addr >> 0) &
					0xFF;
			}
		} else {
			colMvAddr[0][0] =
				(pDecInfo->vbMV[0].phys_addr >> 24) & 0xFF;
			colMvAddr[0][1] =
				(pDecInfo->vbMV[0].phys_addr >> 16) & 0xFF;
			colMvAddr[0][2] =
				(pDecInfo->vbMV[0].phys_addr >> 8) & 0xFF;
			colMvAddr[0][3] =
				(pDecInfo->vbMV[0].phys_addr >> 0) & 0xFF;
		}
		if (pCodecInst->isUseProtectBuffer) //RTK, need to review it
		{
#ifdef ENABLE_TEE_DRM_FLOW
			VLOG(TRACE,
			     "[%d]%s.Coda9VpuWriteMem paraBuffer+384:0x%x with colMvAddr.numFbsForDecoding:%d.sizeof(colMvAddr):%d\n",
			     __LINE__, __func__, (paraBuffer + 384),
			     pDecInfo->numFbsForDecoding, sizeof(colMvAddr));
			Coda9VpuWriteMem(pCodecInst->coreIdx, paraBuffer + 384,
					 (BYTE *)colMvAddr, sizeof(colMvAddr),
					 VDI_BIG_ENDIAN, pCodecInst->teeapi_ctx,
					 pCodecInst->teeapi_tee_session);
#else
			VpuWriteMem(pCodecInst->coreIdx, paraBuffer + 384,
				    (BYTE *)colMvAddr, sizeof(colMvAddr),
				    VDI_BIG_ENDIAN);
#endif
		} else {
			VLOG(TRACE,
			     "[%d]%s.VpuWriteMem paraBuffer+384:0x%x with colMvAddr.numFbsForDecoding:%d.sizeof(colMvAddr):%d\n",
			     __LINE__, __func__, (paraBuffer + 384),
			     pDecInfo->numFbsForDecoding, sizeof(colMvAddr));
			VpuWriteMem(pCodecInst->coreIdx, paraBuffer + 384,
				    (BYTE *)colMvAddr, sizeof(colMvAddr),
				    VDI_BIG_ENDIAN);
		}
	}

	if (pCodecInst->productId == PRODUCT_ID_980) {
		for (i = 0; i < pDecInfo->numFbsForDecoding; i++) {
			frameAddr[i][0][0] =
				(pDecInfo->frameBufPool[i].bufYBot >> 24) &
				0xFF;
			frameAddr[i][0][1] =
				(pDecInfo->frameBufPool[i].bufYBot >> 16) &
				0xFF;
			frameAddr[i][0][2] =
				(pDecInfo->frameBufPool[i].bufYBot >> 8) & 0xFF;
			frameAddr[i][0][3] =
				(pDecInfo->frameBufPool[i].bufYBot >> 0) & 0xFF;
			if (pDecInfo->openParam.cbcrOrder ==
			    CBCR_ORDER_NORMAL) {
				frameAddr[i][1][0] =
					(pDecInfo->frameBufPool[i].bufCbBot >>
					 24) &
					0xFF;
				frameAddr[i][1][1] =
					(pDecInfo->frameBufPool[i].bufCbBot >>
					 16) &
					0xFF;
				frameAddr[i][1][2] =
					(pDecInfo->frameBufPool[i].bufCbBot >>
					 8) &
					0xFF;
				frameAddr[i][1][3] =
					(pDecInfo->frameBufPool[i].bufCbBot >>
					 0) &
					0xFF;
				frameAddr[i][2][0] =
					(pDecInfo->frameBufPool[i].bufCrBot >>
					 24) &
					0xFF;
				frameAddr[i][2][1] =
					(pDecInfo->frameBufPool[i].bufCrBot >>
					 16) &
					0xFF;
				frameAddr[i][2][2] =
					(pDecInfo->frameBufPool[i].bufCrBot >>
					 8) &
					0xFF;
				frameAddr[i][2][3] =
					(pDecInfo->frameBufPool[i].bufCrBot >>
					 0) &
					0xFF;
			} else {
				frameAddr[i][2][0] =
					(pDecInfo->frameBufPool[i].bufCbBot >>
					 24) &
					0xFF;
				frameAddr[i][2][1] =
					(pDecInfo->frameBufPool[i].bufCbBot >>
					 16) &
					0xFF;
				frameAddr[i][2][2] =
					(pDecInfo->frameBufPool[i].bufCbBot >>
					 8) &
					0xFF;
				frameAddr[i][2][3] =
					(pDecInfo->frameBufPool[i].bufCbBot >>
					 0) &
					0xFF;
				frameAddr[i][1][0] =
					(pDecInfo->frameBufPool[i].bufCrBot >>
					 24) &
					0xFF;
				frameAddr[i][1][1] =
					(pDecInfo->frameBufPool[i].bufCrBot >>
					 16) &
					0xFF;
				frameAddr[i][1][2] =
					(pDecInfo->frameBufPool[i].bufCrBot >>
					 8) &
					0xFF;
				frameAddr[i][1][3] =
					(pDecInfo->frameBufPool[i].bufCrBot >>
					 0) &
					0xFF;
			}
		}
		if (pCodecInst->isUseProtectBuffer) //RTK, need to review it
		{
#ifdef ENABLE_TEE_DRM_FLOW
			VLOG(TRACE,
			     "[%d]%s.Coda9VpuWriteMem paraBuffer+384+128:0x%x with frameAddr.numFbsForDecoding:%d.sizeof(frameAddr):%d\n",
			     __LINE__, __func__, (paraBuffer + 384 + 128),
			     pDecInfo->numFbsForDecoding, sizeof(frameAddr));
			Coda9VpuWriteMem(pCodecInst->coreIdx,
					 paraBuffer + 384 + 128,
					 (BYTE *)frameAddr, sizeof(frameAddr),
					 VDI_BIG_ENDIAN, pCodecInst->teeapi_ctx,
					 pCodecInst->teeapi_tee_session);
#else
			VpuWriteMem(pCodecInst->coreIdx, paraBuffer + 384 + 128,
				    (BYTE *)frameAddr, sizeof(frameAddr),
				    VDI_BIG_ENDIAN);
#endif
		} else {
			VLOG(TRACE,
			     "[%d]%s.VpuWriteMem paraBuffer+384+128:0x%x with frameAddr.numFbsForDecoding:%d.sizeof(frameAddr):%d\n",
			     __LINE__, __func__, (paraBuffer + 384 + 128),
			     pDecInfo->numFbsForDecoding, sizeof(frameAddr));
			VpuWriteMem(pCodecInst->coreIdx, paraBuffer + 384 + 128,
				    (BYTE *)frameAddr, sizeof(frameAddr),
				    VDI_BIG_ENDIAN);
		}

		if (pDecInfo->wtlEnable) {
			int num =
				pDecInfo->numFbsForDecoding; /* start index of WTL fb array */
			int end = pDecInfo->numFrameBuffers;
			for (i = num; i < end; i++) {
				frameAddr[i - num][0][0] =
					(pDecInfo->frameBufPool[i].bufY >> 24) &
					0xFF;
				frameAddr[i - num][0][1] =
					(pDecInfo->frameBufPool[i].bufY >> 16) &
					0xFF;
				frameAddr[i - num][0][2] =
					(pDecInfo->frameBufPool[i].bufY >> 8) &
					0xFF;
				frameAddr[i - num][0][3] =
					(pDecInfo->frameBufPool[i].bufY >> 0) &
					0xFF;
				if (pDecInfo->openParam.cbcrOrder ==
				    CBCR_ORDER_NORMAL) {
					frameAddr[i - num][1][0] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 24) &
						0xFF;
					frameAddr[i - num][1][1] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 16) &
						0xFF;
					frameAddr[i - num][1][2] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 8) &
						0xFF;
					frameAddr[i - num][1][3] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 0) &
						0xFF;
					frameAddr[i - num][2][0] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 24) &
						0xFF;
					frameAddr[i - num][2][1] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 16) &
						0xFF;
					frameAddr[i - num][2][2] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 8) &
						0xFF;
					frameAddr[i - num][2][3] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 0) &
						0xFF;
				} else {
					frameAddr[i - num][2][0] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 24) &
						0xFF;
					frameAddr[i - num][2][1] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 16) &
						0xFF;
					frameAddr[i - num][2][2] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 8) &
						0xFF;
					frameAddr[i - num][2][3] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 0) &
						0xFF;
					frameAddr[i - num][1][0] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 24) &
						0xFF;
					frameAddr[i - num][1][1] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 16) &
						0xFF;
					frameAddr[i - num][1][2] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 8) &
						0xFF;
					frameAddr[i - num][1][3] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 0) &
						0xFF;
				}
			}
			if (pCodecInst->isUseProtectBuffer) //RTK, need to review it
			{
#ifdef ENABLE_TEE_DRM_FLOW
				VLOG(TRACE,
				     "[%d]%s.Coda9VpuWriteMem paraBuffer+384+128+384:0x%x with frameAddr.numFbsForDecoding:%d.sizeof(frameAddr):%d\n",
				     __LINE__, __func__,
				     (paraBuffer + 384 + 128 + 384),
				     pDecInfo->numFbsForDecoding,
				     sizeof(frameAddr));
				Coda9VpuWriteMem(
					pCodecInst->coreIdx,
					paraBuffer + 384 + 128 + 384,
					(BYTE *)frameAddr, sizeof(frameAddr),
					VDI_BIG_ENDIAN, pCodecInst->teeapi_ctx,
					pCodecInst->teeapi_tee_session);
#else
				VpuWriteMem(pCodecInst->coreIdx,
					    paraBuffer + 384 + 128 + 384,
					    (BYTE *)frameAddr,
					    sizeof(frameAddr), VDI_BIG_ENDIAN);
#endif
			} else {
				VLOG(TRACE,
				     "[%d]%s.VpuWriteMem paraBuffer+384+128+384:0x%x with frameAddr.numFbsForDecoding:%d.sizeof(frameAddr):%d\n",
				     __LINE__, __func__,
				     (paraBuffer + 384 + 128 + 384),
				     pDecInfo->numFbsForDecoding,
				     sizeof(frameAddr));
				VpuWriteMem(pCodecInst->coreIdx,
					    paraBuffer + 384 + 128 + 384,
					    (BYTE *)frameAddr,
					    sizeof(frameAddr), VDI_BIG_ENDIAN);
			}

			if (pDecInfo->wtlMode == FF_FIELD) {
				for (i = num; i < num * 2; i++) {
					frameAddr[i - num][0][0] =
						(pDecInfo->frameBufPool[i]
							 .bufYBot >>
						 24) &
						0xFF;
					frameAddr[i - num][0][1] =
						(pDecInfo->frameBufPool[i]
							 .bufYBot >>
						 16) &
						0xFF;
					frameAddr[i - num][0][2] =
						(pDecInfo->frameBufPool[i]
							 .bufYBot >>
						 8) &
						0xFF;
					frameAddr[i - num][0][3] =
						(pDecInfo->frameBufPool[i]
							 .bufYBot >>
						 0) &
						0xFF;
					if (pDecInfo->openParam.cbcrOrder ==
					    CBCR_ORDER_NORMAL) {
						frameAddr[i - num][1][0] =
							(pDecInfo->frameBufPool[i]
								 .bufCbBot >>
							 24) &
							0xFF;
						frameAddr[i - num][1][1] =
							(pDecInfo->frameBufPool[i]
								 .bufCbBot >>
							 16) &
							0xFF;
						frameAddr[i - num][1][2] =
							(pDecInfo->frameBufPool[i]
								 .bufCbBot >>
							 8) &
							0xFF;
						frameAddr[i - num][1][3] =
							(pDecInfo->frameBufPool[i]
								 .bufCbBot >>
							 0) &
							0xFF;
						frameAddr[i - num][2][0] =
							(pDecInfo->frameBufPool[i]
								 .bufCrBot >>
							 24) &
							0xFF;
						frameAddr[i - num][2][1] =
							(pDecInfo->frameBufPool[i]
								 .bufCrBot >>
							 16) &
							0xFF;
						frameAddr[i - num][2][2] =
							(pDecInfo->frameBufPool[i]
								 .bufCrBot >>
							 8) &
							0xFF;
						frameAddr[i - num][2][3] =
							(pDecInfo->frameBufPool[i]
								 .bufCrBot >>
							 0) &
							0xFF;
					} else {
						frameAddr[i - num][2][0] =
							(pDecInfo->frameBufPool[i]
								 .bufCbBot >>
							 24) &
							0xFF;
						frameAddr[i - num][2][1] =
							(pDecInfo->frameBufPool[i]
								 .bufCbBot >>
							 16) &
							0xFF;
						frameAddr[i - num][2][2] =
							(pDecInfo->frameBufPool[i]
								 .bufCbBot >>
							 8) &
							0xFF;
						frameAddr[i - num][2][3] =
							(pDecInfo->frameBufPool[i]
								 .bufCbBot >>
							 0) &
							0xFF;
						frameAddr[i - num][1][0] =
							(pDecInfo->frameBufPool[i]
								 .bufCrBot >>
							 24) &
							0xFF;
						frameAddr[i - num][1][1] =
							(pDecInfo->frameBufPool[i]
								 .bufCrBot >>
							 16) &
							0xFF;
						frameAddr[i - num][1][2] =
							(pDecInfo->frameBufPool[i]
								 .bufCrBot >>
							 8) &
							0xFF;
						frameAddr[i - num][1][3] =
							(pDecInfo->frameBufPool[i]
								 .bufCrBot >>
							 0) &
							0xFF;
					}
				}
				if (pCodecInst->isUseProtectBuffer) //RTK, need to review it
				{
#ifdef ENABLE_TEE_DRM_FLOW
					VLOG(TRACE,
					     "[%d]%s.Coda9VpuWriteMem paraBuffer+384+128+384+384:0x%x with frameAddr.numFbsForDecoding:%d.sizeof(frameAddr):%d\n",
					     __LINE__, __func__,
					     (paraBuffer + 384 + 128 + 384 +
					      384),
					     pDecInfo->numFbsForDecoding,
					     sizeof(frameAddr));
					Coda9VpuWriteMem(
						pCodecInst->coreIdx,
						paraBuffer + 384 + 128 + 384 +
							384,
						(BYTE *)frameAddr,
						sizeof(frameAddr),
						VDI_BIG_ENDIAN,
						pCodecInst->teeapi_ctx,
						pCodecInst->teeapi_tee_session);
#else
					VpuWriteMem(pCodecInst->coreIdx,
						    paraBuffer + 384 + 128 +
							    384 + 384,
						    (BYTE *)frameAddr,
						    sizeof(frameAddr),
						    VDI_BIG_ENDIAN);
#endif
				} else {
					VLOG(TRACE,
					     "[%d]%s.VpuWriteMem paraBuffer+384+128+384+384:0x%x with frameAddr.numFbsForDecoding:%d.sizeof(frameAddr):%d\n",
					     __LINE__, __func__,
					     (paraBuffer + 384 + 128 + 384 +
					      384),
					     pDecInfo->numFbsForDecoding,
					     sizeof(frameAddr));
					VpuWriteMem(pCodecInst->coreIdx,
						    paraBuffer + 384 + 128 +
							    384 + 384,
						    (BYTE *)frameAddr,
						    sizeof(frameAddr),
						    VDI_BIG_ENDIAN);
				}
			}
		}
	} else {
		if (pDecInfo->wtlEnable) {
			int num =
				pDecInfo->numFbsForDecoding; /* start index of WTL fb array */
			int end = pDecInfo->numFrameBuffers;
			for (i = num; i < end; i++) {
				frameAddr[i - num][0][0] =
					(pDecInfo->frameBufPool[i].bufY >> 24) &
					0xFF;
				frameAddr[i - num][0][1] =
					(pDecInfo->frameBufPool[i].bufY >> 16) &
					0xFF;
				frameAddr[i - num][0][2] =
					(pDecInfo->frameBufPool[i].bufY >> 8) &
					0xFF;
				frameAddr[i - num][0][3] =
					(pDecInfo->frameBufPool[i].bufY >> 0) &
					0xFF;
				if (pDecInfo->openParam.cbcrOrder ==
				    CBCR_ORDER_NORMAL) {
					frameAddr[i - num][1][0] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 24) &
						0xFF;
					frameAddr[i - num][1][1] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 16) &
						0xFF;
					frameAddr[i - num][1][2] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 8) &
						0xFF;
					frameAddr[i - num][1][3] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 0) &
						0xFF;
					frameAddr[i - num][2][0] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 24) &
						0xFF;
					frameAddr[i - num][2][1] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 16) &
						0xFF;
					frameAddr[i - num][2][2] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 8) &
						0xFF;
					frameAddr[i - num][2][3] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 0) &
						0xFF;
				} else {
					frameAddr[i - num][2][0] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 24) &
						0xFF;
					frameAddr[i - num][2][1] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 16) &
						0xFF;
					frameAddr[i - num][2][2] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 8) &
						0xFF;
					frameAddr[i - num][2][3] =
						(pDecInfo->frameBufPool[i]
							 .bufCb >>
						 0) &
						0xFF;
					frameAddr[i - num][1][0] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 24) &
						0xFF;
					frameAddr[i - num][1][1] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 16) &
						0xFF;
					frameAddr[i - num][1][2] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 8) &
						0xFF;
					frameAddr[i - num][1][3] =
						(pDecInfo->frameBufPool[i]
							 .bufCr >>
						 0) &
						0xFF;
				}
			}
			if (pCodecInst->isUseProtectBuffer) //RTK, need to review it
			{
#ifdef ENABLE_TEE_DRM_FLOW
				VLOG(TRACE,
				     "[%d]%s.Coda9VpuWriteMem paraBuffer+384+128+384:0x%x with frameAddr.numFbsForDecoding:%d.sizeof(frameAddr):%d\n",
				     __LINE__, __func__,
				     (paraBuffer + 384 + 128 + 384),
				     pDecInfo->numFbsForDecoding,
				     sizeof(frameAddr));
				Coda9VpuWriteMem(
					pCodecInst->coreIdx,
					paraBuffer + 384 + 128 + 384,
					(BYTE *)frameAddr, sizeof(frameAddr),
					VDI_BIG_ENDIAN, pCodecInst->teeapi_ctx,
					pCodecInst->teeapi_tee_session);
#else
				VpuWriteMem(pCodecInst->coreIdx,
					    paraBuffer + 384 + 128 + 384,
					    (BYTE *)frameAddr,
					    sizeof(frameAddr), VDI_BIG_ENDIAN);
#endif
			} else {
				VLOG(TRACE,
				     "[%d]%s.VpuWriteMem paraBuffer+384+128+384:0x%x with frameAddr.numFbsForDecoding:%d.sizeof(frameAddr):%d\n",
				     __LINE__, __func__,
				     (paraBuffer + 384 + 128 + 384),
				     pDecInfo->numFbsForDecoding,
				     sizeof(frameAddr));
				VpuWriteMem(pCodecInst->coreIdx,
					    paraBuffer + 384 + 128 + 384,
					    (BYTE *)frameAddr,
					    sizeof(frameAddr), VDI_BIG_ENDIAN);
			}
		}
	}

	if (!ConfigSecAXICoda9(pCodecInst->coreIdx, pCodecInst->codecMode,
			       &pDecInfo->secAxiInfo, pDecInfo->stride,
			       pDecInfo->frameBufferHeight,
			       pDecInfo->initialInfo.profile & 0xff)) {
		return RETCODE_INSUFFICIENT_RESOURCE;
	}

#ifdef ENABLE_CODA9_WRITE_PROTECT
	if (pDecInfo->secAxiInfo.bufSize) {
		pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_SEC_AXI]
			.enable = 1;
		pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_SEC_AXI]
			.isSecondary = 1;
		pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_SEC_AXI]
			.startAddress = pDecInfo->secAxiInfo.bufBase;
		pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_SEC_AXI]
			.endAddress = pDecInfo->secAxiInfo.bufBase +
				      pDecInfo->secAxiInfo.bufSize;
		VLOG(INFO,
		     "[%d]%s.set decRegion[WPROT_DEC_SEC_AXI](%d,%d,0x%08x,0x%08x)\n",
		     __LINE__, __func__,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_SEC_AXI]
			     .enable,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_SEC_AXI]
			     .isSecondary,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_SEC_AXI]
			     .startAddress,
		     pDecInfo->writeMemProtectCfg.decRegion[WPROT_DEC_SEC_AXI]
			     .endAddress);
	}
#endif

	for (i = 0; i < pDecInfo->numFrameBuffers; i++) {
		pDecInfo->frameBufPool[i].nv21 =
			pDecInfo->openParam.nv21 &
			pDecInfo->openParam.cbcrInterleave;
	}

	// Tell the decoder how much frame buffers were allocated.
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_BUF_NUM,
		    pDecInfo->numFrameBuffers);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_BUF_STRIDE,
		    pDecInfo->stride);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_BIT_ADDR,
		    pDecInfo->secAxiInfo.u.coda9.bufBitUse);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_IPACDC_ADDR,
		    pDecInfo->secAxiInfo.u.coda9.bufIpAcDcUse);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_DBKY_ADDR,
		    pDecInfo->secAxiInfo.u.coda9.bufDbkYUse);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_DBKC_ADDR,
		    pDecInfo->secAxiInfo.u.coda9.bufDbkCUse);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_OVL_ADDR,
		    pDecInfo->secAxiInfo.u.coda9.bufOvlUse);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_BTP_ADDR,
		    pDecInfo->secAxiInfo.u.coda9.bufBtpUse);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_DELAY,
		    pDecInfo->frameDelay);

	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_CACHE_CONFIG,
		    pDecInfo->cacheConfig.type2.CacheMode);

	if (pCodecInst->codecMode == VPX_DEC) {
		vpu_buffer_t *pvbSlice = &pDecInfo->vbSlice;
#ifdef ENABLE_CODA9_WRITE_PROTECT
		WriteMemProtectCfg *pCgf = &pDecInfo->writeMemProtectCfg;
#endif
		if (pvbSlice->size == 0) {
			pvbSlice->size = VP8_MB_SAVE_SIZE;
			//ENABLE_TEE_DRM_FLOW
			if (pCodecInst->isUseProtectBuffer)
				pvbSlice->req_spec_region =
					VE_SECURE_PROTECTION;
			else
				pvbSlice->req_spec_region = 0;

			if (vdi_allocate_dma_memory_no_mmap(
				    pCodecInst->coreIdx, pvbSlice,
				    pCodecInst->filp) < 0) {
				return RETCODE_INSUFFICIENT_RESOURCE;
			}
			VLOG(TRACE,
			     "[%d]%s.vdi_allocate_dma_memory_no_mmap vbSlice(0x%lx,0x%lx,0x%lx,%d,%d)\n",
			     __LINE__, __func__, pvbSlice->phys_addr,
			     pvbSlice->base, pvbSlice->virt_addr,
			     pvbSlice->size, pvbSlice->req_spec_region);
		}
#ifdef ENABLE_CODA9_WRITE_PROTECT
		pCgf->decRegion[WPROT_DEC_PIC_SAVE].enable = 1;
		pCgf->decRegion[WPROT_DEC_PIC_SAVE].isSecondary = 0;
		pCgf->decRegion[WPROT_DEC_PIC_SAVE].startAddress =
			pvbSlice->phys_addr;
		pCgf->decRegion[WPROT_DEC_PIC_SAVE].endAddress =
			pvbSlice->phys_addr + pvbSlice->size;
		VLOG(INFO,
		     "[%d]%s.set decRegion[WPROT_DEC_PIC_SAVE](%d,%d,0x%08x,0x%08x)\n",
		     __LINE__, __func__,
		     pCgf->decRegion[WPROT_DEC_PIC_SAVE].enable,
		     pCgf->decRegion[WPROT_DEC_PIC_SAVE].isSecondary,
		     pCgf->decRegion[WPROT_DEC_PIC_SAVE].startAddress,
		     pCgf->decRegion[WPROT_DEC_PIC_SAVE].endAddress);
#endif
		VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_MB_BUF_BASE,
			    pvbSlice->phys_addr);
	}

	if (pCodecInst->codecMode == AVC_DEC) {
		vpu_buffer_t *pvbSlice = &pDecInfo->vbSlice;
#ifdef ENABLE_CODA9_WRITE_PROTECT
		WriteMemProtectCfg *pCgf = &pDecInfo->writeMemProtectCfg;
#endif
		if (pvbSlice->size == 0) {
			pvbSlice->size = SLICE_SAVE_SIZE;
			//ENABLE_TEE_DRM_FLOW
			if (pCodecInst->isUseProtectBuffer)
				pvbSlice->req_spec_region =
					VE_SECURE_PROTECTION;
			else
				pvbSlice->req_spec_region = 0;

			if (vdi_allocate_dma_memory_no_mmap(
				    pCodecInst->coreIdx, pvbSlice,
				    pCodecInst->filp) < 0) {
				return RETCODE_INSUFFICIENT_RESOURCE;
			}
			VLOG(TRACE,
			     "[%d]%s.vdi_allocate_dma_memory_no_mmap vbSlice(0x%lx,0x%lx,0x%lx,%d,%d)\n",
			     __LINE__, __func__, pvbSlice->phys_addr,
			     pvbSlice->base, pvbSlice->virt_addr,
			     pvbSlice->size, pvbSlice->req_spec_region);
		}
#ifdef ENABLE_CODA9_WRITE_PROTECT
		pCgf->decRegion[WPROT_DEC_PIC_SAVE].enable = 1;
		pCgf->decRegion[WPROT_DEC_PIC_SAVE].isSecondary = 0;
		pCgf->decRegion[WPROT_DEC_PIC_SAVE].startAddress =
			pvbSlice->phys_addr;
		pCgf->decRegion[WPROT_DEC_PIC_SAVE].endAddress =
			pvbSlice->phys_addr + pvbSlice->size;
		VLOG(INFO,
		     "[%d]%s.set decRegion[WPROT_DEC_PIC_SAVE](%d,%d,0x%08x,0x%08x)\n",
		     __LINE__, __func__,
		     pCgf->decRegion[WPROT_DEC_PIC_SAVE].enable,
		     pCgf->decRegion[WPROT_DEC_PIC_SAVE].isSecondary,
		     pCgf->decRegion[WPROT_DEC_PIC_SAVE].startAddress,
		     pCgf->decRegion[WPROT_DEC_PIC_SAVE].endAddress);
#endif
		VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_SLICE_BB_START,
			    pvbSlice->phys_addr);
		VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_SLICE_BB_SIZE,
			    (pvbSlice->size / 1024));
	}

	if (pCodecInst->productId == PRODUCT_ID_980) {
		val = 0;
		val |= (pDecInfo->openParam.bwbEnable << 15);
		val |= (pDecInfo->wtlMode << 17) |
		       (pDecInfo->tiled2LinearMode << 13) |
		       (pDecInfo->mapType << 9) | (FORMAT_420 << 6);
		val |= ((pDecInfo->openParam.cbcrInterleave)
			<< 2); // Interleave bit position is modified
		val |= pDecInfo->openParam.frameEndian;
		VpuWriteReg(pCodecInst->coreIdx, BIT_FRAME_MEM_CTRL, val);
	} else if (pCodecInst->productId == PRODUCT_ID_960) {
		val = 0;
		val |= (pDecInfo->wtlEnable << 17);
		val |= (pDecInfo->openParam.bwbEnable << 12);
		if (pDecInfo->mapType) {
			if (pDecInfo->mapType == TILED_FRAME_MB_RASTER_MAP ||
			    pDecInfo->mapType == TILED_FIELD_MB_RASTER_MAP)
				val |= (pDecInfo->tiled2LinearEnable << 11) |
				       (0x03 << 9) | (FORMAT_420 << 6);
			else
				val |= (pDecInfo->tiled2LinearEnable << 11) |
				       (0x02 << 9) | (FORMAT_420 << 6);
		}
		val |= ((pDecInfo->openParam.cbcrInterleave)
			<< 2); // Interleave bit position is modified
		val |= pDecInfo->openParam.frameEndian;
		VpuWriteReg(pCodecInst->coreIdx, BIT_FRAME_MEM_CTRL, val);
	} else {
		return RETCODE_NOT_FOUND_VPU_DEVICE;
	}

	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_MAX_DEC_SIZE, 0);
	Coda9BitIssueCommand(pCodecInst->coreIdx, pCodecInst, SET_FRAME_BUF);
	if (vdi_wait_vpu_busy(pCodecInst->coreIdx, __VPU_BUSY_TIMEOUT,
			      BIT_BUSY_FLAG) == -1) {
		if (pCodecInst->loggingEnable)
			vdi_log(pCodecInst->coreIdx, SET_FRAME_BUF, 2);
		return RETCODE_VPU_RESPONSE_TIMEOUT;
	}
	if (pCodecInst->loggingEnable)
		vdi_log(pCodecInst->coreIdx, SET_FRAME_BUF, 0);

	if (VpuReadReg(pCodecInst->coreIdx, RET_SET_FRAME_SUCCESS) &
	    (1 << 31)) {
		return RETCODE_MEMORY_ACCESS_VIOLATION;
	}

	return RETCODE_SUCCESS;
}

RetCode Coda9VpuDecFlush(CodecInst *instance,
			 FramebufferIndex *framebufferIndexes, Uint32 size)
{
	Uint32 i;
	DecInfo *pDecInfo = &instance->CodecInfo->decInfo;
#if defined(CODA9_CHECK_CODE_BUFFER_MD5SUM)
	vpu_buffer_t vb;
	unsigned char md5hash[16];

	vdi_get_common_memory((unsigned long)instance->coreIdx, &vb);
	VLOG(TRACE,
	     "[%d]%s.codeBuffer(phys:0x%08x,virt:0x%08x).size:%d.BIT_CODE_BUF_ADDR:0x%08x\n",
	     __LINE__, __func__, vb.phys_addr, vb.virt_addr, gCodaFwSize,
	     VpuReadReg(instance->coreIdx, BIT_CODE_BUF_ADDR));
	MD5(((unsigned char *)(vb.virt_addr)), (size_t)gCodaFwSize, md5hash);
	VLOG(TRACE,
	     "[%d]%s.codeBuffer Hash: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     __LINE__, __func__, md5hash[0], md5hash[1], md5hash[2], md5hash[3],
	     md5hash[4], md5hash[5], md5hash[6], md5hash[7], md5hash[8],
	     md5hash[9], md5hash[10], md5hash[11], md5hash[12], md5hash[13],
	     md5hash[14], md5hash[15]);
#endif

	Coda9BitIssueCommand(instance->coreIdx, instance, DEC_BUF_FLUSH);
	if (vdi_wait_vpu_busy(instance->coreIdx, __VPU_BUSY_TIMEOUT,
			      BIT_BUSY_FLAG) == -1) {
		return RETCODE_VPU_RESPONSE_TIMEOUT;
	}

	pDecInfo->frameDisplayFlag = VpuReadReg(
		instance->coreIdx, pDecInfo->frameDisplayFlagRegAddr); //RTK

	if (framebufferIndexes != NULL) {
		for (i = 0; i < size; i++) {
			framebufferIndexes[i].linearIndex = -2;
			framebufferIndexes[i].tiledIndex = -2;
		}
	}

	return RETCODE_SUCCESS;
}

/************************************************************************/
/* Encoder                                                              */
/************************************************************************/

RetCode Coda9VpuBuildUpEncParam(CodecInst *pCodec, EncOpenParam *param)
{
	RetCode ret = RETCODE_SUCCESS;
	Uint32 coreIdx;
	Int32 productId;
	EncInfo *pEncInfo = &pCodec->CodecInfo->encInfo;

    VLOG(TRACE, "[+] [%d]%s.h:%p\n",__LINE__,__func__,(void *)pCodec);
	coreIdx = pCodec->coreIdx;
	productId = Coda9VpuGetProductId(coreIdx);

	if ((ret = SetupEncCodecInstance(productId, pCodec)) != RETCODE_SUCCESS)
		return ret;

	if (param->bitstreamFormat == STD_MPEG4 ||
	    param->bitstreamFormat == STD_H263)
		pCodec->codecMode = MP4_ENC;
	else if (param->bitstreamFormat == STD_AVC)
		pCodec->codecMode = AVC_ENC;

	if (param->bitstreamFormat == STD_AVC &&
	    param->EncStdParam.avcParam.mvcExtension)
		pCodec->codecModeAux = AVC_AUX_MVC;
	else
		pCodec->codecModeAux = 0;

	if (productId == PRODUCT_ID_980) {
		pEncInfo->ActivePPSIdx = 0;
		pEncInfo->frameIdx = 0;
		pEncInfo->fieldDone = 0;
	}

	pEncInfo->vbWork.size = WORK_BUF_SIZE;
	pEncInfo->vbWork.req_spec_region = 0;
	if (vdi_allocate_dma_memory(pCodec->coreIdx, &pEncInfo->vbWork,
				    pCodec->filp) < 0)
		return RETCODE_INSUFFICIENT_RESOURCE;
	VLOG(TRACE,
	     "[%d]%s.vdi_allocate_dma_memory vbWork(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
	     __LINE__, __func__, pEncInfo->vbWork.phys_addr,
	     pEncInfo->vbWork.base, pEncInfo->vbWork.virt_addr,
	     pEncInfo->vbWork.size, pEncInfo->vbWork.req_spec_region);

	pEncInfo->streamRdPtr = param->bitstreamBuffer;
	pEncInfo->streamWrPtr = param->bitstreamBuffer;
	pEncInfo->lineBufIntEn = param->lineBufIntEn;
	pEncInfo->streamBufStartAddr = param->bitstreamBuffer;
	pEncInfo->streamBufSize = param->bitstreamBufferSize;
	pEncInfo->streamBufEndAddr =
		param->bitstreamBuffer + param->bitstreamBufferSize;
	pEncInfo->stride = 0;
	pEncInfo->vbFrame.size = 0;
	pEncInfo->vbPPU.size = 0;
	pEncInfo->frameAllocExt = 0;
	pEncInfo->ppuAllocExt = 0;
	pEncInfo->secAxiInfo.u.coda9.useBitEnable = 0;
	pEncInfo->secAxiInfo.u.coda9.useIpEnable = 0;
	pEncInfo->secAxiInfo.u.coda9.useDbkYEnable = 0;
	pEncInfo->secAxiInfo.u.coda9.useDbkCEnable = 0;
	pEncInfo->secAxiInfo.u.coda9.useOvlEnable = 0;
	pEncInfo->rotationEnable = 0;
	pEncInfo->mirrorEnable = 0;
	pEncInfo->mirrorDirection = MIRDIR_NONE;
	pEncInfo->rotationAngle = 0;
	pEncInfo->initialInfoObtained = 0;
	pEncInfo->ringBufferEnable = param->ringBufferEnable;
	pEncInfo->linear2TiledEnable = param->linear2TiledEnable;
	pEncInfo->linear2TiledMode = param->linear2TiledMode; // coda980 only
	if (!pEncInfo->linear2TiledEnable)
		pEncInfo->linear2TiledMode = 0;

	/* Maverick Cache I */
	osal_memset((void *)&pEncInfo->cacheConfig, 0x00,
		    sizeof(MaverickCacheConfig));

	if (productId == PRODUCT_ID_960) {
		pEncInfo->dramCfg.bankBit = EM_BANK;
		pEncInfo->dramCfg.casBit = EM_CAS;
		pEncInfo->dramCfg.rasBit = EM_RAS;
		pEncInfo->dramCfg.busBit = EM_WIDTH;
	}

    VLOG(TRACE, "[+] [%d]%s.h:%p.ret:%d\n",__LINE__,__func__,(void *)pCodec,ret);
	return ret;
}

RetCode Coda9VpuEncSetup(CodecInst *pCodecInst)
{
	Int32 picWidth, picHeight;
	Int32 data, val;
	Int32 productId, rcEnable;
	EncInfo *pEncInfo = &pCodecInst->CodecInfo->encInfo;

    VLOG(TRACE, "[+] [%d]%s.h:%p\n",__LINE__,__func__,(void *)pCodecInst);

	rcEnable = pEncInfo->openParam.rcEnable & 0xf;

	productId = pCodecInst->productId;
	picWidth = pEncInfo->openParam.picWidth;
	picHeight = pEncInfo->openParam.picHeight;
	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_BB_START,
		    pEncInfo->streamBufStartAddr);
	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_BB_SIZE,
		    pEncInfo->streamBufSize / 1024); // size in KB

	// Rotation Left 90 or 270 case : Swap XY resolution for VPU internal usage
	if (pEncInfo->rotationAngle == 90 || pEncInfo->rotationAngle == 270)
		data = (picHeight << 16) | picWidth;
	else
		data = (picWidth << 16) | picHeight;

	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_SRC_SIZE, data);
	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_SRC_F_RATE,
		    pEncInfo->openParam.frameRateInfo);

	if (pEncInfo->openParam.bitstreamFormat == STD_MPEG4) {
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_COD_STD, 3);
		data = pEncInfo->openParam.EncStdParam.mp4Param.mp4IntraDcVlcThr
			       << 2 |
		       pEncInfo->openParam.EncStdParam.mp4Param
				       .mp4ReversibleVlcEnable
			       << 1 |
		       pEncInfo->openParam.EncStdParam.mp4Param
			       .mp4DataPartitionEnable;

		data |= ((pEncInfo->openParam.EncStdParam.mp4Param.mp4HecEnable >
			  0) ?
				 1 :
				 0)
			<< 5;
		data |= ((pEncInfo->openParam.EncStdParam.mp4Param.mp4Verid ==
			  2) ?
				 0 :
				 1)
			<< 6;

		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_MP4_PARA, data);

		if (productId == PRODUCT_ID_980)
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_ME_OPTION,
				    (VPU_ME_LINEBUFFER_MODE << 9) |
					    (pEncInfo->openParam.meBlkMode
					     << 5) |
					    (pEncInfo->openParam.MEUseZeroPmv
					     << 4) |
					    (pEncInfo->openParam.MESearchRangeY
					     << 2) |
					    pEncInfo->openParam.MESearchRangeX);
		else
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_ME_OPTION,
				    (pEncInfo->openParam.meBlkMode << 3) |
					    (pEncInfo->openParam.MEUseZeroPmv
					     << 2) |
					    pEncInfo->openParam.MESearchRange);
	} else if (pEncInfo->openParam.bitstreamFormat == STD_H263) {
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_COD_STD, 11);
		data = pEncInfo->openParam.EncStdParam.h263Param.h263AnnexIEnable
			       << 3 |
		       pEncInfo->openParam.EncStdParam.h263Param.h263AnnexJEnable
			       << 2 |
		       pEncInfo->openParam.EncStdParam.h263Param.h263AnnexKEnable
			       << 1 |
		       pEncInfo->openParam.EncStdParam.h263Param
			       .h263AnnexTEnable;
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_263_PARA, data);
		if (productId == PRODUCT_ID_980)
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_ME_OPTION,
				    (VPU_ME_LINEBUFFER_MODE << 9) |
					    (pEncInfo->openParam.meBlkMode
					     << 5) |
					    (pEncInfo->openParam.MEUseZeroPmv
					     << 4) |
					    (pEncInfo->openParam.MESearchRangeY
					     << 2) |
					    pEncInfo->openParam.MESearchRangeX);
		else
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_ME_OPTION,
				    (pEncInfo->openParam.meBlkMode << 3) |
					    (pEncInfo->openParam.MEUseZeroPmv
					     << 2) |
					    pEncInfo->openParam.MESearchRange);
	} else if (pEncInfo->openParam.bitstreamFormat == STD_AVC) {
		if (productId == PRODUCT_ID_980) {
			int SliceNum = 0;
			AvcPpsParam *ActivePPS =
				&pEncInfo->openParam.EncStdParam.avcParam
					 .ppsParam[pEncInfo->ActivePPSIdx];
			if (ActivePPS->transform8x8Mode == 1 ||
			    pEncInfo->openParam.EncStdParam.avcParam
					    .chromaFormat400 == 1)
				pEncInfo->openParam.EncStdParam.avcParam
					.profile = 2;
			else if (ActivePPS->entropyCodingMode != 0 ||
				 pEncInfo->openParam.EncStdParam.avcParam
						 .fieldFlag == 1)
				pEncInfo->openParam.EncStdParam.avcParam
					.profile = 1;
			else
				pEncInfo->openParam.EncStdParam.avcParam
					.profile = 0;

			if (pEncInfo->openParam.sliceMode.sliceMode == 1 &&
			    pEncInfo->openParam.sliceMode.sliceSizeMode == 1)
				SliceNum =
					pEncInfo->openParam.sliceMode.sliceSize;

			if (!pEncInfo->openParam.EncStdParam.avcParam.level) {
				if (pEncInfo->openParam.EncStdParam.avcParam
					    .fieldFlag)
					pEncInfo->openParam.EncStdParam.avcParam
						.level = LevelCalculation(
						picWidth / 16,
						(picHeight + 31) / 32,
						pEncInfo->openParam
							.frameRateInfo,
						1, pEncInfo->openParam.bitRate,
						SliceNum);
				else
					pEncInfo->openParam.EncStdParam.avcParam
						.level = LevelCalculation(
						picWidth / 16, picHeight / 16,
						pEncInfo->openParam
							.frameRateInfo,
						0, pEncInfo->openParam.bitRate,
						SliceNum);
				if (pEncInfo->openParam.EncStdParam.avcParam
					    .level < 0)
					return RETCODE_INVALID_PARAM;
			}

			VpuWriteReg(
				pCodecInst->coreIdx, CMD_ENC_SEQ_COD_STD,
				(pEncInfo->openParam.EncStdParam.avcParam.profile
				 << 4) | 0x0);
			data = 0;
			if (pEncInfo->openParam.EncStdParam.avcParam
				    .videoSignalTypePresent) {
				data = (pEncInfo->openParam.EncStdParam.avcParam
						.videoSignalTypePresent
					<< 29) |
				       (pEncInfo->openParam.EncStdParam.avcParam
						.videoFormat
					<< 26) |
				       (pEncInfo->openParam.EncStdParam.avcParam
						.videoFullRangeFlag
					<< 25) |
				       (pEncInfo->openParam.EncStdParam.avcParam
						.colourDescripPresFlag
					<< 24) |
				       (pEncInfo->openParam.EncStdParam.avcParam
						.colourPrimaries
					<< 16) |
				       (pEncInfo->openParam.EncStdParam.avcParam
						.transferCharacteristics
					<< 8) |
				       (pEncInfo->openParam.EncStdParam.avcParam
						.matrixCoefficients
					<< 0);
			}

			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_SEQ_VIDEO_SIGNAL_TYPE_PRESENT,
				    data);
		} else {
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_COD_STD,
				    0x0);
		}

		data = (pEncInfo->openParam.EncStdParam.avcParam
				.deblkFilterOffsetBeta &
			15) << 12 |
		       (pEncInfo->openParam.EncStdParam.avcParam
				.deblkFilterOffsetAlpha &
			15) << 8 |
		       pEncInfo->openParam.EncStdParam.avcParam.disableDeblk
			       << 6 |
		       pEncInfo->openParam.EncStdParam.avcParam
				       .constrainedIntraPredFlag
			       << 5 |
		       (pEncInfo->openParam.EncStdParam.avcParam.chromaQpOffset &
			31);
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_264_PARA, data);
		if (productId == PRODUCT_ID_980) {
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_ME_OPTION,
				    (VPU_ME_LINEBUFFER_MODE << 9) |
					    (pEncInfo->openParam.meBlkMode
					     << 5) |
					    (pEncInfo->openParam.MEUseZeroPmv
					     << 4) |
					    (pEncInfo->openParam.MESearchRangeY
					     << 2) |
					    pEncInfo->openParam.MESearchRangeX);
		} else {
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_ME_OPTION,
				    (pEncInfo->openParam.meBlkMode << 3) |
					    (pEncInfo->openParam.MEUseZeroPmv
					     << 2) |
					    pEncInfo->openParam.MESearchRange);
		}
	}

	if (productId == PRODUCT_ID_980) {
		data = 0;
		if (pEncInfo->openParam.sliceMode.sliceMode != 0) {
			data = pEncInfo->openParam.sliceMode.sliceSize << 2 |
			       (pEncInfo->openParam.sliceMode.sliceSizeMode +
				1); // encoding mode 0,1,2
		}
	} else {
		data = pEncInfo->openParam.sliceMode.sliceSize << 2 |
		       pEncInfo->openParam.sliceMode.sliceSizeMode << 1 |
		       pEncInfo->openParam.sliceMode.sliceMode;
	}
	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_SLICE_MODE, data);

	if (rcEnable) { // rate control enabled
		if (productId == PRODUCT_ID_980) {
			if (pEncInfo->openParam.bitstreamFormat == STD_AVC) {
				int MinDeltaQp, MaxDeltaQp, QpMin, QpMax;

				data = (pEncInfo->openParam.idrInterval << 21) |
				       (pEncInfo->openParam.rcGopIQpOffsetEn
					<< 20) |
				       ((pEncInfo->openParam.rcGopIQpOffset &
					 0xF)
					<< 16) |
				       pEncInfo->openParam.gopSize;
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_SEQ_GOP_NUM, data);

				data = (pEncInfo->openParam.frameSkipDisable)
					       << 31 |
				       pEncInfo->openParam.initialDelay << 16 |
				       0;
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_SEQ_RC_PARA, data);
				if (pEncInfo->openParam.rcEnable ==
				    1) //OMX_Video_ControlRateConstant
					data = (pEncInfo->openParam.bitRate
						<< 4) |
					       (pEncInfo->openParam.rcEnable &
						0xf) |
					       (pEncInfo->openParam.strictCBR
						<< 22);
				else
					data = (pEncInfo->openParam.bitRate
						<< 4) |
					       (pEncInfo->openParam.rcEnable &
						0xf);

				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_SEQ_RC_PARA2, data);

				data = 0;
				if (pEncInfo->openParam.maxIntraSize > 0)
					data = (1 << 16) |
					       (pEncInfo->openParam.maxIntraSize &
						0xFFFF);
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_SEQ_RC_MAX_INTRA_SIZE,
					    data);

				if (pEncInfo->openParam.userMinDeltaQp < 0)
					MinDeltaQp = 0;
				else
					MinDeltaQp = (1 << 6) |
						     pEncInfo->openParam
							     .userMinDeltaQp;

				if (pEncInfo->openParam.userMaxDeltaQp < 0)
					MaxDeltaQp = 0;
				else
					MaxDeltaQp = (1 << 6) |
						     pEncInfo->openParam
							     .userMaxDeltaQp;

				if (pEncInfo->openParam.userQpMin < 0)
					QpMin = 0;
				else
					QpMin = (1 << 6) |
						pEncInfo->openParam.userQpMin;

				if (pEncInfo->openParam.userQpMax < 0)
					QpMax = 0;
				else
					QpMax = (1 << 6) |
						pEncInfo->openParam.userQpMax;

				data = MinDeltaQp << 24 | MaxDeltaQp << 16 |
				       QpMin << 8 | QpMax;
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_SEQ_QP_RANGE_SET, data);
			} else { // MP4
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_SEQ_GOP_NUM,
					    pEncInfo->openParam.gopSize);

				data = (pEncInfo->openParam.frameSkipDisable)
					       << 31 |
				       pEncInfo->openParam.initialDelay << 16 |
				       pEncInfo->openParam.bitRate << 1 | 1;
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_SEQ_RC_PARA, data);
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_SEQ_QP_RANGE_SET, 0);
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_SEQ_RC_PARA2, 0);
			}
		} else {
			/* coda960 ENCODER */
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_GOP_NUM,
				    pEncInfo->openParam.gopSize);

			data = (pEncInfo->openParam.frameSkipDisable) << 31 |
			       pEncInfo->openParam.initialDelay << 16 |
			       pEncInfo->openParam.bitRate << 1 | 1;
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_RC_PARA,
				    data);
		}
	} else {
		if (pEncInfo->openParam.bitstreamFormat == STD_AVC)
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_GOP_NUM,
				    (pEncInfo->openParam.idrInterval << 21) |
					    pEncInfo->openParam.gopSize);
		else
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_GOP_NUM,
				    pEncInfo->openParam.gopSize);
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_RC_PARA, 0);
		if (productId == PRODUCT_ID_980) {
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_SEQ_QP_RANGE_SET, 0);
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_RC_PARA2,
				    0);
		}
	}

	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_RC_BUF_SIZE,
		    pEncInfo->openParam.vbvBufferSize);
	data = pEncInfo->openParam.intraRefresh |
	       pEncInfo->openParam.ConscIntraRefreshEnable << 16 |
	       pEncInfo->openParam.CountIntraMbEnable << 17 |
	       pEncInfo->openParam.FieldSeqIntraRefreshEnable << 18;
	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_INTRA_REFRESH, data);

	data = 0;
	if (pEncInfo->openParam.rcIntraQp >= 0) {
		data = (1 << 5);
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_INTRA_QP,
			    pEncInfo->openParam.rcIntraQp);
	} else {
		data = 0;
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_INTRA_QP,
			    (Uint32)-1);
	}

	if (pCodecInst->codecMode == AVC_ENC) {
		data |= (pEncInfo->openParam.EncStdParam.avcParam.audEnable
			 << 2);
		if (pCodecInst->codecModeAux == AVC_AUX_MVC) {
			data |= (pEncInfo->openParam.EncStdParam.avcParam
					 .interviewEn
				 << 4);
			data |= (pEncInfo->openParam.EncStdParam.avcParam
					 .parasetRefreshEn
				 << 8);
			data |= (pEncInfo->openParam.EncStdParam.avcParam
					 .prefixNalEn
				 << 9);
		}

		if (productId == PRODUCT_ID_980) {
			data |= pEncInfo->openParam.EncStdParam.avcParam
					.fieldFlag
				<< 10;
			data |= pEncInfo->openParam.EncStdParam.avcParam
					.fieldRefMode
				<< 11;
		}
	}

	if (pEncInfo->openParam.userQpMax >= 0) {
		data |= (1 << 6);
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_RC_QP_MAX,
			    pEncInfo->openParam.userQpMax);
	} else {
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_RC_QP_MAX, 0);
	}

	if (pEncInfo->openParam.userGamma >= 0) {
		data |= (1 << 7);
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_RC_GAMMA,
			    pEncInfo->openParam.userGamma);
	} else {
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_RC_GAMMA, 0);
	}

	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_OPTION, data);
	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_RC_INTERVAL_MODE,
		    (pEncInfo->openParam.mbInterval << 2) |
			    pEncInfo->openParam.rcIntervalMode);
	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_SEQ_INTRA_WEIGHT,
		    pEncInfo->openParam.intraCostWeight);

	VpuWriteReg(pCodecInst->coreIdx, pEncInfo->streamWrPtrRegAddr,
		    pEncInfo->streamWrPtr);
	VpuWriteReg(pCodecInst->coreIdx, pEncInfo->streamRdPtrRegAddr,
		    pEncInfo->streamRdPtr);

	SetEncFrameMemInfo(pCodecInst);

	val = 0;
	if (pEncInfo->ringBufferEnable == 0) {
		if (pEncInfo->lineBufIntEn)
			val |= (0x1 << 6);
		val |= (0x1 << 5);
		val |= (0x1 << 4);
	} else {
		val |= (0x1 << 3);
	}
	val |= pEncInfo->openParam.streamEndian;
	VpuWriteReg(pCodecInst->coreIdx, BIT_BIT_STREAM_CTRL, val);

	Coda9BitIssueCommand(pCodecInst->coreIdx, pCodecInst, ENC_SEQ_INIT);

	if (vdi_wait_interrupt(pCodecInst->coreIdx, __VPU_BUSY_TIMEOUT,
			       BIT_INT_REASON) == -1) {
		if (pCodecInst->loggingEnable)
			vdi_log(pCodecInst->coreIdx, ENC_SEQ_INIT, 2);
		return RETCODE_VPU_RESPONSE_TIMEOUT;
	}
	VpuWriteReg(
		pCodecInst->coreIdx, BIT_INT_CLEAR,
		1); // that is OK. HW signal already is clear by device driver
	VpuWriteReg(pCodecInst->coreIdx, BIT_INT_REASON, 0);

	if (pCodecInst->loggingEnable)
		vdi_log(pCodecInst->coreIdx, ENC_SEQ_INIT, 0);

	if (VpuReadReg(pCodecInst->coreIdx, RET_ENC_SEQ_END_SUCCESS) &
	    (1 << 31))
		return RETCODE_MEMORY_ACCESS_VIOLATION;

	if (VpuReadReg(pCodecInst->coreIdx, RET_ENC_SEQ_END_SUCCESS) == 0)
		return RETCODE_FAILURE;

	pEncInfo->streamWrPtr =
		VpuReadReg(pCodecInst->coreIdx, pEncInfo->streamWrPtrRegAddr);
	pEncInfo->streamEndflag =
		VpuReadReg(pCodecInst->coreIdx, BIT_BIT_STREAM_PARAM);

    VLOG(TRACE, "[-] [%d]%s.h:%p.ret:RETCODE_SUCCESS\n",__LINE__,__func__,(void *)pCodecInst);
	return RETCODE_SUCCESS;
}

RetCode Coda9VpuEncRegisterFramebuffer(CodecInst *instance)
{
	CodecInst *pCodecInst = instance;
	EncInfo *pEncInfo;
	Int32 i, val;
	RetCode ret;
	PhysicalAddress paraBuffer;
	BYTE frameAddr[MAX_FRAMEBUFFER_COUNT][3][4];
	Int32 stride, height, mapType, num;
	VpuAttr *pAttr = &g_VpuCoreAttributes[instance->coreIdx];
    VLOG(TRACE, "[+] [%d]%s.h:%p\n",__LINE__,__func__,(void *)instance);

	osal_memset((void *)frameAddr, 0, sizeof(frameAddr));

	pEncInfo = &instance->CodecInfo->encInfo;
	stride = pEncInfo->stride;
	height = pEncInfo->frameBufferHeight;
	mapType = pEncInfo->mapType;

	if (pCodecInst->productId == PRODUCT_ID_960) {
		pEncInfo->mapCfg.tiledBaseAddr = pEncInfo->vbFrame.phys_addr;
	}

	if (!ConfigSecAXICoda9(pCodecInst->coreIdx, instance->codecMode,
			       &pEncInfo->secAxiInfo, stride, height, 0))
		return RETCODE_INSUFFICIENT_RESOURCE;

	if (pCodecInst->productId == PRODUCT_ID_960) {
		val = SetTiledMapType(pCodecInst->coreIdx, &pEncInfo->mapCfg,
				      mapType, stride,
				      pEncInfo->openParam.cbcrInterleave,
				      &pEncInfo->dramCfg);
	} else {
		if (mapType != LINEAR_FRAME_MAP && mapType != LINEAR_FIELD_MAP)
			val = SetTiledMapType(
				pCodecInst->coreIdx, &pEncInfo->mapCfg, mapType,
				(stride > height) ? stride : height,
				pEncInfo->openParam.cbcrInterleave,
				&pEncInfo->dramCfg);
		else
			val = SetTiledMapType(
				pCodecInst->coreIdx, &pEncInfo->mapCfg, mapType,
				stride, pEncInfo->openParam.cbcrInterleave,
				&pEncInfo->dramCfg);
	}

	if (val == 0)
		return RETCODE_INVALID_PARAM;

	SetEncFrameMemInfo(pCodecInst);

	paraBuffer = VpuReadReg(pCodecInst->coreIdx, BIT_PARA_BUF_ADDR);

	// Let the decoder know the addresses of the frame buffers.
	for (i = 0; i < pEncInfo->numFrameBuffers; i++) {
		frameAddr[i][0][0] =
			(pEncInfo->frameBufPool[i].bufY >> 24) & 0xFF;
		frameAddr[i][0][1] =
			(pEncInfo->frameBufPool[i].bufY >> 16) & 0xFF;
		frameAddr[i][0][2] =
			(pEncInfo->frameBufPool[i].bufY >> 8) & 0xFF;
		frameAddr[i][0][3] =
			(pEncInfo->frameBufPool[i].bufY >> 0) & 0xFF;
		frameAddr[i][1][0] =
			(pEncInfo->frameBufPool[i].bufCb >> 24) & 0xFF;
		frameAddr[i][1][1] =
			(pEncInfo->frameBufPool[i].bufCb >> 16) & 0xFF;
		frameAddr[i][1][2] =
			(pEncInfo->frameBufPool[i].bufCb >> 8) & 0xFF;
		frameAddr[i][1][3] =
			(pEncInfo->frameBufPool[i].bufCb >> 0) & 0xFF;
		frameAddr[i][2][0] =
			(pEncInfo->frameBufPool[i].bufCr >> 24) & 0xFF;
		frameAddr[i][2][1] =
			(pEncInfo->frameBufPool[i].bufCr >> 16) & 0xFF;
		frameAddr[i][2][2] =
			(pEncInfo->frameBufPool[i].bufCr >> 8) & 0xFF;
		frameAddr[i][2][3] =
			(pEncInfo->frameBufPool[i].bufCr >> 0) & 0xFF;
	}
	VpuWriteMem(pCodecInst->coreIdx, paraBuffer, (BYTE *)frameAddr,
		    sizeof(frameAddr), VDI_BIG_ENDIAN);

	if (pCodecInst->productId == PRODUCT_ID_980) {
		for (i = 0; i < pEncInfo->numFrameBuffers; i++) {
			frameAddr[i][0][0] =
				(pEncInfo->frameBufPool[i].bufYBot >> 24) &
				0xFF;
			frameAddr[i][0][1] =
				(pEncInfo->frameBufPool[i].bufYBot >> 16) &
				0xFF;
			frameAddr[i][0][2] =
				(pEncInfo->frameBufPool[i].bufYBot >> 8) & 0xFF;
			frameAddr[i][0][3] =
				(pEncInfo->frameBufPool[i].bufYBot >> 0) & 0xFF;
			frameAddr[i][1][0] =
				(pEncInfo->frameBufPool[i].bufCbBot >> 24) &
				0xFF;
			frameAddr[i][1][1] =
				(pEncInfo->frameBufPool[i].bufCbBot >> 16) &
				0xFF;
			frameAddr[i][1][2] =
				(pEncInfo->frameBufPool[i].bufCbBot >> 8) &
				0xFF;
			frameAddr[i][1][3] =
				(pEncInfo->frameBufPool[i].bufCbBot >> 0) &
				0xFF;
			frameAddr[i][2][0] =
				(pEncInfo->frameBufPool[i].bufCrBot >> 24) &
				0xFF;
			frameAddr[i][2][1] =
				(pEncInfo->frameBufPool[i].bufCrBot >> 16) &
				0xFF;
			frameAddr[i][2][2] =
				(pEncInfo->frameBufPool[i].bufCrBot >> 8) &
				0xFF;
			frameAddr[i][2][3] =
				(pEncInfo->frameBufPool[i].bufCrBot >> 0) &
				0xFF;
		}
		VpuWriteMem(pCodecInst->coreIdx, paraBuffer + 384 + 128,
			    (BYTE *)frameAddr, sizeof(frameAddr),
			    VDI_BIG_ENDIAN);
	}

	// Tell the codec how much frame buffers were allocated.
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_BUF_NUM,
		    pEncInfo->numFrameBuffers);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_BUF_STRIDE, stride);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_BIT_ADDR,
		    pEncInfo->secAxiInfo.u.coda9.bufBitUse);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_IPACDC_ADDR,
		    pEncInfo->secAxiInfo.u.coda9.bufIpAcDcUse);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_DBKY_ADDR,
		    pEncInfo->secAxiInfo.u.coda9.bufDbkYUse);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_DBKC_ADDR,
		    pEncInfo->secAxiInfo.u.coda9.bufDbkCUse);
	VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_AXI_OVL_ADDR,
		    pEncInfo->secAxiInfo.u.coda9.bufOvlUse);

	if (pAttr->framebufferCacheType == FramebufCacheMaverickII) {
		VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_CACHE_CONFIG,
			    pEncInfo->cacheConfig.type2.CacheMode);
	} else if (pAttr->framebufferCacheType == FramebufCacheMaverickI) {
		// Maverick Cache Configuration
		val = (pEncInfo->cacheConfig.type1.luma.cfg.PageSizeX << 28) |
		      (pEncInfo->cacheConfig.type1.luma.cfg.PageSizeY << 24) |
		      (pEncInfo->cacheConfig.type1.luma.cfg.CacheSizeX << 20) |
		      (pEncInfo->cacheConfig.type1.luma.cfg.CacheSizeY << 16) |
		      (pEncInfo->cacheConfig.type1.chroma.cfg.PageSizeX << 12) |
		      (pEncInfo->cacheConfig.type1.chroma.cfg.PageSizeY << 8) |
		      (pEncInfo->cacheConfig.type1.chroma.cfg.CacheSizeX << 4) |
		      (pEncInfo->cacheConfig.type1.chroma.cfg.CacheSizeY << 0);

		VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_CACHE_SIZE, val);

		val = (pEncInfo->cacheConfig.type1.Bypass << 4) |
		      (pEncInfo->cacheConfig.type1.DualConf << 2) |
		      (pEncInfo->cacheConfig.type1.PageMerge << 0);
		val = val << 24;
		val |= (pEncInfo->cacheConfig.type1.luma.cfg.BufferSize << 16) |
		       (pEncInfo->cacheConfig.type1.chroma.cfg.BufferSize
			<< 8) |
		       (pEncInfo->cacheConfig.type1.chroma.cfg.BufferSize << 8);

		VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_CACHE_CONFIG,
			    val);
	}

	num = pEncInfo->numFrameBuffers;
	if (pCodecInst->productId == PRODUCT_ID_960) {
		Uint32 subsampleLumaSize = stride * height;
		Uint32 subsampleChromaSize = stride * height / 4; // FORMAT_420
		vpu_buffer_t vbBuf;
		FrameBuffer *pFb;

		osal_memset((void *)&vbBuf, 0, sizeof(vpu_buffer_t));
		vbBuf.size = subsampleLumaSize + 2 * subsampleChromaSize;
		vbBuf.phys_addr = (PhysicalAddress)0;
		if (vdi_allocate_dma_memory(pCodecInst->coreIdx, &vbBuf,
					    pCodecInst->filp) < 0) {
			pEncInfo->vbSubSampFrame.size = 0;
			pEncInfo->vbSubSampFrame.phys_addr = 0;
			return RETCODE_INSUFFICIENT_RESOURCE;
		}
		VLOG(TRACE,
		     "[%d]%s.vdi_allocate_dma_memory vbBuf(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
		     __LINE__, __func__, vbBuf.phys_addr, vbBuf.base,
		     vbBuf.virt_addr, vbBuf.size, vbBuf.req_spec_region);
		pFb = &pEncInfo->frameBufPool[num];
		pFb->bufY = vbBuf.phys_addr;
		pFb->bufCb = (PhysicalAddress)-1;
		pFb->bufCr = (PhysicalAddress)-1;
		pFb->updateFbInfo = TRUE;
		ret = AllocateLinearFrameBuffer(LINEAR_FRAME_MAP, pFb, 1,
						subsampleLumaSize,
						subsampleChromaSize);
		if (ret != RETCODE_SUCCESS) {
			pEncInfo->vbSubSampFrame.size = 0;
			pEncInfo->vbSubSampFrame.phys_addr = 0;
			return RETCODE_INSUFFICIENT_RESOURCE;
		}
		pEncInfo->vbSubSampFrame = vbBuf;
		num++;

		// Set Sub-Sampling buffer for ME-Reference and DBK-Reconstruction
		// BPU will swap below two buffer internally every pic by pic
		val = GetXY2AXIAddr(&pEncInfo->mapCfg, 0, 0, 0, stride, pFb);
		VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_SUBSAMP_A, val);
		VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_SUBSAMP_B,
			    val + (stride * height / 2));

		if (pCodecInst->codecMode == AVC_ENC &&
		    pCodecInst->codecModeAux == AVC_AUX_MVC) {
			vbBuf.size =
				subsampleLumaSize + 2 * subsampleChromaSize;
			vbBuf.phys_addr = (PhysicalAddress)0;
			if (vdi_allocate_dma_memory(pCodecInst->coreIdx, &vbBuf,
						    pCodecInst->filp) < 0) {
				pEncInfo->vbSubSampFrame.size = 0;
				pEncInfo->vbSubSampFrame.phys_addr = 0;
				return RETCODE_INSUFFICIENT_RESOURCE;
			}
			VLOG(TRACE,
			     "[%d]%s.vdi_allocate_dma_memory vbBuf(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
			     __LINE__, __func__, vbBuf.phys_addr, vbBuf.base,
			     vbBuf.virt_addr, vbBuf.size,
			     vbBuf.req_spec_region);
			pFb = &pEncInfo->frameBufPool[num];
			pFb->bufY = vbBuf.phys_addr;
			pFb->bufCb = (PhysicalAddress)-1;
			pFb->bufCr = (PhysicalAddress)-1;
			pFb->updateFbInfo = TRUE;
			ret = AllocateLinearFrameBuffer(LINEAR_FRAME_MAP, pFb,
							1, subsampleLumaSize,
							subsampleChromaSize);
			if (ret != RETCODE_SUCCESS) {
				pEncInfo->vbMvcSubSampFrame.size = 0;
				pEncInfo->vbMvcSubSampFrame.phys_addr = 0;
				return RETCODE_INSUFFICIENT_RESOURCE;
			}
			pEncInfo->vbMvcSubSampFrame = vbBuf;
			num++;

			val = GetXY2AXIAddr(&pEncInfo->mapCfg, 0, 0, 0, stride,
					    pFb);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_SET_FRAME_SUBSAMP_A_MVC, val);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_SET_FRAME_SUBSAMP_B_MVC,
				    val + (stride * height / 2));
		}
	}

	if (pCodecInst->codecMode == MP4_ENC) {
		// MPEG4 Encoder Data-Partitioned bitstream temporal buffer
		pEncInfo->vbScratch.size = SIZE_MP4ENC_DATA_PARTITION;
		pEncInfo->vbScratch.req_spec_region = 0;
		if (vdi_allocate_dma_memory(pCodecInst->coreIdx,
					    &pEncInfo->vbScratch,
					    pCodecInst->filp) < 0)
			return RETCODE_INSUFFICIENT_RESOURCE;
		VLOG(TRACE,
		     "[%d]%s.vdi_allocate_dma_memory vbScratch(0x%08lx,0x%08lx,0x%08lx,%d,%d)\n",
		     __LINE__, __func__, pEncInfo->vbScratch.phys_addr,
		     pEncInfo->vbScratch.base, pEncInfo->vbScratch.virt_addr,
		     pEncInfo->vbScratch.size,
		     pEncInfo->vbScratch.req_spec_region);
		VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_DP_BUF_BASE,
			    pEncInfo->vbScratch.phys_addr);
		VpuWriteReg(pCodecInst->coreIdx, CMD_SET_FRAME_DP_BUF_SIZE,
			    pEncInfo->vbScratch.size >> 10);
	}

	Coda9BitIssueCommand(pCodecInst->coreIdx, pCodecInst, SET_FRAME_BUF);
	if (vdi_wait_vpu_busy(pCodecInst->coreIdx, __VPU_BUSY_TIMEOUT,
			      BIT_BUSY_FLAG) == -1) {
		if (pCodecInst->loggingEnable)
			vdi_log(pCodecInst->coreIdx, SET_FRAME_BUF, 2);
		return RETCODE_VPU_RESPONSE_TIMEOUT;
	}
	if (pCodecInst->loggingEnable)
		vdi_log(pCodecInst->coreIdx, SET_FRAME_BUF, 0);

	if (VpuReadReg(pCodecInst->coreIdx, RET_SET_FRAME_SUCCESS) &
	    (1 << 31)) {
		return RETCODE_MEMORY_ACCESS_VIOLATION;
	}

    VLOG(TRACE, "[-] [%d]%s.h:%p.ret:RETCODE_SUCCESS\n",__LINE__,__func__,(void *)instance);
	return RETCODE_SUCCESS;
}

RetCode Coda9VpuEncode(CodecInst *pCodecInst, EncParam *param)
{
	EncInfo *pEncInfo;
	FrameBuffer *pSrcFrame;
	Uint32 rotMirMode;
	Uint32 val;
	vpu_instance_pool_t *vip;
	pEncInfo = &pCodecInst->CodecInfo->encInfo;
    VLOG(TRACE, "[+] [%d]%s.h:%p\n",__LINE__,__func__,(void *)pCodecInst);

	vip = (vpu_instance_pool_t *)vdi_get_instance_pool(pCodecInst->coreIdx);
	if (!vip) {
		return RETCODE_INVALID_HANDLE;
	}

	pSrcFrame = param->sourceFrame;
	rotMirMode = 0;
	if (pEncInfo->rotationEnable == TRUE) {
		switch (pEncInfo->rotationAngle) {
		case 0:
			rotMirMode |= 0x0;
			break;
		case 90:
			rotMirMode |= 0x1;
			break;
		case 180:
			rotMirMode |= 0x2;
			break;
		case 270:
			rotMirMode |= 0x3;
			break;
		}
	}

	if (pEncInfo->mirrorEnable == TRUE) {
		switch (pEncInfo->mirrorDirection) {
		case MIRDIR_NONE:
			rotMirMode |= 0x0;
			break;
		case MIRDIR_VER:
			rotMirMode |= 0x4;
			break;
		case MIRDIR_HOR:
			rotMirMode |= 0x8;
			break;
		case MIRDIR_HOR_VER:
			rotMirMode |= 0xc;
			break;
		}
	}

	if (pCodecInst->productId == PRODUCT_ID_980) {
		rotMirMode |= ((pSrcFrame->endian & 0x03) << 16);
		rotMirMode |= ((pSrcFrame->cbcrInterleave & 0x01) << 18);
		rotMirMode |= ((pSrcFrame->sourceLBurstEn & 0x01) << 4);
	} else {
		rotMirMode |= ((pSrcFrame->sourceLBurstEn & 0x01) << 4);
		rotMirMode |= ((pSrcFrame->cbcrInterleave & 0x01) << 18);
		rotMirMode |= pEncInfo->openParam.nv21 << 21;
	}

	if (pCodecInst->productId == PRODUCT_ID_980 &&
	    pCodecInst->codecMode == AVC_ENC) {
		//ROI command
		if (param->setROI.mode) {
			int roi_number = param->setROI.number;
			int i;

			VpuWriteReg(
				pCodecInst->coreIdx, CMD_ENC_ROI_MODE,
				1); // currently, only mode 0 can be supported
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_ROI_NUM,
				    roi_number);

			for (i = 0; i < roi_number; i++) {
				VpuRect *rect = &param->setROI.region[i];
				int data;
				if (pEncInfo->openParam.EncStdParam.avcParam
					    .fieldFlag)
					data = ((((rect->bottom + 16) / 32) &
						 0xff)
						<< 24) |
					       ((((rect->top + 16) / 32) & 0xff)
						<< 16) |
					       ((((rect->right + 8) / 16) &
						 0xff)
						<< 8) |
					       (((rect->left + 8) / 16) & 0xff);
				else
					data = ((((rect->bottom + 8) / 16) &
						 0xff)
						<< 24) |
					       ((((rect->top + 8) / 16) & 0xff)
						<< 16) |
					       ((((rect->right + 8) / 16) &
						 0xff)
						<< 8) |
					       (((rect->left + 8) / 16) & 0xff);

#ifdef SUPPORT_ROI_50
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_ROI_POS_0, data);
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_ROI_QP_0,
					    param->setROI.qp[i]);
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_ROI_INDEX, i);
				Coda9BitIssueCommand(pCodecInst->coreIdx,
						     pCodecInst, ENC_ROI_INIT);
				if (vdi_wait_vpu_busy(pCodecInst->coreIdx,
						      __VPU_BUSY_TIMEOUT,
						      BIT_BUSY_FLAG) == -1) {
					if (pCodecInst->loggingEnable)
						vdi_log(pCodecInst->coreIdx,
							ENC_ROI_INIT, 2);
					SetPendingInst(pCodecInst->coreIdx, 0);
					LeaveLock(pCodecInst->coreIdx);
					return RETCODE_VPU_RESPONSE_TIMEOUT;
				}

				if (pCodecInst->loggingEnable)
					vdi_log(pCodecInst->coreIdx,
						ENC_ROI_INIT, 0);
				if (!VpuReadReg(pCodecInst->coreIdx,
						RET_ENC_ROI_SUCCESS)) {
					SetPendingInst(pCodecInst->coreIdx, 0);
					LeaveLock(pCodecInst->coreIdx);
					return RETCODE_FAILURE;
				}
#else
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_ROI_POS_0 + i * 8, data);
				VpuWriteReg(pCodecInst->coreIdx,
					    CMD_ENC_ROI_QP_0 + i * 8,
					    param->setROI.qp[i]);
#endif
			}
#ifdef SUPPORT_ROI_50
#else
			Coda9BitIssueCommand(pCodecInst->coreIdx, pCodecInst,
					     ENC_ROI_INIT);
			if (vdi_wait_vpu_busy(pCodecInst->coreIdx,
					      __VPU_BUSY_TIMEOUT,
					      BIT_BUSY_FLAG) == -1) {
				if (pCodecInst->loggingEnable)
					vdi_log(pCodecInst->coreIdx,
						ENC_ROI_INIT, 0);
				LeaveLock(pCodecInst->coreIdx);
				return RETCODE_VPU_RESPONSE_TIMEOUT;
			}
			if (pCodecInst->loggingEnable)
				vdi_log(pCodecInst->coreIdx, ENC_ROI_INIT, 0);
			if (!VpuReadReg(pCodecInst->coreIdx,
					RET_ENC_ROI_SUCCESS)) {
				LeaveLock(pCodecInst->coreIdx);
				return RETCODE_FAILURE;
			}
#endif
		} // if (param->setROI.mode)

		{
			int ActivePPSIdx = pEncInfo->ActivePPSIdx;
			AvcPpsParam *ActvePPS =
				&pEncInfo->openParam.EncStdParam.avcParam
					 .ppsParam[ActivePPSIdx];
			if (ActvePPS->entropyCodingMode == 2) {
				int pic_idx;
				int gop_num;
				int slice_type = 0; // 0 = Intra, 1 =inter
				int mvc_second_view;

				pic_idx = pEncInfo->openParam.EncStdParam
							  .avcParam.fieldFlag ?
						  2 * pEncInfo->frameIdx +
							  pEncInfo->fieldDone :
						  pEncInfo->frameIdx;
				gop_num =
					pEncInfo->openParam.EncStdParam.avcParam
							.fieldFlag ?
						2 * pEncInfo->openParam.gopSize :
						pEncInfo->openParam.gopSize;
				mvc_second_view =
					pEncInfo->openParam.EncStdParam.avcParam
						.mvcExtension &&
					(!pEncInfo->openParam.EncStdParam
						  .avcParam.interviewEn);
				if (pEncInfo->openParam.EncStdParam.avcParam
					    .mvcExtension)
					gop_num *= 2;
				UNREFERENCED_PARAMETER(mvc_second_view);
				UNREFERENCED_PARAMETER(slice_type);

				if (gop_num == 0) // Only first I
				{
					if (pic_idx == 0 || pic_idx == 1)
						slice_type = 0;
					else
						slice_type = 1;
				} else if (gop_num == 1) // All I
				{
					slice_type = 0;
				} else {
					if ((pic_idx % gop_num) == 0 ||
					    (pic_idx % gop_num) == 1) // I frame
						slice_type = 0;
					else // P frame
						slice_type = 1;
				}
				if (pEncInfo->openParam.EncStdParam.avcParam
					    .mvcExtension) {
					if (pEncInfo->openParam.EncStdParam
						    .avcParam.interviewEn) {
						if (slice_type == 0 &&
						    (pic_idx % 2) == 0) {
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_ENABLE,
								(1 << 11) +
									(1
									 << 7)); // change_enable. pps_id, entropy_coding_mode
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_CABAC_MODE,
								0); // pps-id
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_PPS_ID,
								0); // cabac_mode
						} else if (slice_type != 0 &&
							   (pic_idx % 2) == 0) {
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_ENABLE,
								(1 << 11) +
									(1
									 << 7)); // change_enable. pps_id, entropy_coding_mode
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_CABAC_MODE,
								1); // pps-id
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_PPS_ID,
								1); // cabac_mode
						}

						else //(slice_type==0 && pic_idx%2 !=0)
						{
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_ENABLE,
								(1 << 11) +
									(1
									 << 7)); // change_enable. pps_id, entropy_coding_mode
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_CABAC_MODE,
								1); // pps-id
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_PPS_ID,
								3); // cabac_mode
						}
					} else {
						if (slice_type == 0 &&
						    (pic_idx % 2) == 0) {
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_ENABLE,
								0x880); // change_enable. pps_id, entropy_coding_mode
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_CABAC_MODE,
								0); // pps-id
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_PPS_ID,
								0); // cabac_mode
						}

						else if (slice_type != 0 &&
							 (pic_idx % 2) == 0) {
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_ENABLE,
								0x880); // change_enable. pps_id, entropy_coding_mode
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_CABAC_MODE,
								1); // pps-id
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_PPS_ID,
								1); // cabac_mode
						}

						else if (slice_type == 0 &&
							 (pic_idx % 2) != 0) {
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_ENABLE,
								0x880); // change_enable. pps_id, entropy_coding_mode
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_CABAC_MODE,
								0); // pps-id
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_PPS_ID,
								2); // cabac_mode
						}

						else //if(slice_type!=0 && pic_idx%2 !=0)
						{
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_ENABLE,
								0x880); // change_enable. pps_id, entropy_coding_mode
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_CABAC_MODE,
								1); // pps-id
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_PPS_ID,
								3); // cabac_mode
						}
					}
				} else {
					VpuWriteReg(
						pCodecInst->coreIdx,
						CMD_ENC_PARAM_CHANGE_ENABLE,
						0x880); // change_enable. ppsId, entropyCodingMode
					//default set by I (ID=0, mode=0)
					VpuWriteReg(
						pCodecInst->coreIdx,
						CMD_ENC_PARAM_CHANGE_CABAC_MODE,
						0);
					VpuWriteReg(pCodecInst->coreIdx,
						    CMD_ENC_PARAM_CHANGE_PPS_ID,
						    0);
					if (gop_num == 0) // only first I
					{
						if (pic_idx != 0) {
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_CABAC_MODE,
								1);
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_PPS_ID,
								1);
						}
					} else if (gop_num != 1) // not All I
					{
						if ((pic_idx % gop_num) !=
						    0) // P frame
						{
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_CABAC_MODE,
								1);
							VpuWriteReg(
								pCodecInst
									->coreIdx,
								CMD_ENC_PARAM_CHANGE_PPS_ID,
								1);
						}
					}
				}
				Coda9BitIssueCommand(pCodecInst->coreIdx,
						     pCodecInst,
						     RC_CHANGE_PARAMETER);
				if (vdi_wait_vpu_busy(pCodecInst->coreIdx,
						      __VPU_BUSY_TIMEOUT,
						      BIT_BUSY_FLAG) == -1) {
					if (pCodecInst->loggingEnable)
						vdi_log(pCodecInst->coreIdx,
							RC_CHANGE_PARAMETER, 0);
					LeaveLock(pCodecInst->coreIdx);
					return RETCODE_VPU_RESPONSE_TIMEOUT;
				}
				if (pCodecInst->loggingEnable)
					vdi_log(pCodecInst->coreIdx,
						RC_CHANGE_PARAMETER, 0);
			}
		}
	}

	if (pCodecInst->productId == PRODUCT_ID_960) {
		if (pEncInfo->mapType > LINEAR_FRAME_MAP &&
		    pEncInfo->mapType <= TILED_MIXED_V_MAP) {
			SetTiledFrameBase(pCodecInst->coreIdx,
					  pEncInfo->vbFrame.phys_addr);
		} else {
			SetTiledFrameBase(pCodecInst->coreIdx, 0);
		}
	}

	if (pEncInfo->mapType != LINEAR_FRAME_MAP &&
	    pEncInfo->mapType != LINEAR_FIELD_MAP) {
		if (pEncInfo->stride > pEncInfo->frameBufferHeight)
			val = SetTiledMapType(
				pCodecInst->coreIdx, &pEncInfo->mapCfg,
				pEncInfo->mapType, pEncInfo->stride,
				pEncInfo->openParam.cbcrInterleave,
				&pEncInfo->dramCfg);
		else
			val = SetTiledMapType(
				pCodecInst->coreIdx, &pEncInfo->mapCfg,
				pEncInfo->mapType, pEncInfo->frameBufferHeight,
				pEncInfo->openParam.cbcrInterleave,
				&pEncInfo->dramCfg);
	} else {
		val = SetTiledMapType(pCodecInst->coreIdx, &pEncInfo->mapCfg,
				      pEncInfo->mapType, pEncInfo->stride,
				      pEncInfo->openParam.cbcrInterleave,
				      &pEncInfo->dramCfg);
	}
	if (val == 0) {
		return RETCODE_INVALID_PARAM;
	}

	rotMirMode |= pEncInfo->openParam.nv21 << 21;
	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_PIC_ROT_MODE, rotMirMode);
	VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_PIC_QS, param->quantParam);

	if (param->skipPicture) {
		if (param->fieldRun) { // not support field + skipPicture
			return RETCODE_INVALID_PARAM;
		}
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_PIC_OPTION,
			    (param->fieldRun << 8) | 1);
	} else {
		// Registering Source Frame Buffer information
		// Hide GDI IF under FW level
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_PIC_SRC_INDEX,
			    pSrcFrame->myIndex);
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_PIC_SRC_STRIDE,
			    pSrcFrame->stride);
		if (pEncInfo->openParam.cbcrOrder == CBCR_ORDER_NORMAL) {
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_PIC_SRC_ADDR_Y,
				    pSrcFrame->bufY);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_PIC_SRC_ADDR_CB, pSrcFrame->bufCb);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_PIC_SRC_ADDR_CR, pSrcFrame->bufCr);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_PIC_SRC_BOTTOM_Y,
				    pSrcFrame->bufYBot);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_PIC_SRC_BOTTOM_CB,
				    pSrcFrame->bufCbBot);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_PIC_SRC_BOTTOM_CR,
				    pSrcFrame->bufCrBot);
		} else { // CBCR_ORDER_REVERSED (YV12)
			VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_PIC_SRC_ADDR_Y,
				    pSrcFrame->bufY);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_PIC_SRC_ADDR_CB, pSrcFrame->bufCr);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_PIC_SRC_ADDR_CR, pSrcFrame->bufCb);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_PIC_SRC_BOTTOM_Y,
				    pSrcFrame->bufYBot);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_PIC_SRC_BOTTOM_CB,
				    pSrcFrame->bufCrBot);
			VpuWriteReg(pCodecInst->coreIdx,
				    CMD_ENC_PIC_SRC_BOTTOM_CR,
				    pSrcFrame->bufCbBot);
		}

		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_PIC_OPTION,
			    (param->fieldRun << 8) |
				    (param->forceIPicture << 1 & 0x2));
	}

	if (pEncInfo->ringBufferEnable == 0) {
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_PIC_BB_START,
			    param->picStreamBufferAddr);
		VpuWriteReg(pCodecInst->coreIdx, CMD_ENC_PIC_BB_SIZE,
			    param->picStreamBufferSize / 1024); // size in KB
		VpuWriteReg(pCodecInst->coreIdx, pEncInfo->streamRdPtrRegAddr,
			    param->picStreamBufferAddr);
		pEncInfo->streamRdPtr = param->picStreamBufferAddr;
	}

	val = 0;
	val = ((pEncInfo->secAxiInfo.u.coda9.useBitEnable & 0x01) << 0 |
	       (pEncInfo->secAxiInfo.u.coda9.useIpEnable & 0x01) << 1 |
	       (pEncInfo->secAxiInfo.u.coda9.useDbkYEnable & 0x01) << 2 |
	       (pEncInfo->secAxiInfo.u.coda9.useDbkCEnable & 0x01) << 3 |
	       (pEncInfo->secAxiInfo.u.coda9.useOvlEnable & 0x01) << 4 |
	       (pEncInfo->secAxiInfo.u.coda9.useBtpEnable & 0x01) << 5 |
	       (pEncInfo->secAxiInfo.u.coda9.useBitEnable & 0x01) << 8 |
	       (pEncInfo->secAxiInfo.u.coda9.useIpEnable & 0x01) << 9 |
	       (pEncInfo->secAxiInfo.u.coda9.useDbkYEnable & 0x01) << 10 |
	       (pEncInfo->secAxiInfo.u.coda9.useDbkCEnable & 0x01) << 11 |
	       (pEncInfo->secAxiInfo.u.coda9.useOvlEnable & 0x01) << 12 |
	       (pEncInfo->secAxiInfo.u.coda9.useBtpEnable & 0x01) << 13);

	VpuWriteReg(pCodecInst->coreIdx, BIT_AXI_SRAM_USE, val);

	VpuWriteReg(pCodecInst->coreIdx, pEncInfo->streamWrPtrRegAddr,
		    pEncInfo->streamWrPtr);
	VpuWriteReg(pCodecInst->coreIdx, pEncInfo->streamRdPtrRegAddr,
		    pEncInfo->streamRdPtr);
	VpuWriteReg(pCodecInst->coreIdx, BIT_BIT_STREAM_PARAM,
		    pEncInfo->streamEndflag);

	SetEncFrameMemInfo(pCodecInst);

	val = 0;
	if (pEncInfo->ringBufferEnable == 0) {
		if (pEncInfo->lineBufIntEn)
			val |= (0x1 << 6);
		val |= (0x1 << 5);
		val |= (0x1 << 4);
	} else {
		val |= (0x1 << 3);
	}
	val |= pEncInfo->openParam.streamEndian;
	VpuWriteReg(pCodecInst->coreIdx, BIT_BIT_STREAM_CTRL, val);

	if (pCodecInst->productId == PRODUCT_ID_980)
		VpuWriteReg(pCodecInst->coreIdx, BIT_ME_LINEBUFFER_MODE,
			    VPU_ME_LINEBUFFER_MODE); // default

	Coda9BitIssueCommand(pCodecInst->coreIdx, pCodecInst, PIC_RUN);

#ifdef GET_PERFORMANCE
	gettimeofday(&start_enc_tv, NULL);
#endif

    VLOG(TRACE, "[-] [%d]%s.h:%p.ret:RETCODE_SUCCESS\n",__LINE__,__func__,(void *)pCodecInst);
	return RETCODE_SUCCESS;
}

RetCode Coda9VpuEncGetResult(CodecInst *pCodecInst, EncOutputInfo *info)
{
	EncInfo *pEncInfo;
	PhysicalAddress rdPtr;
	PhysicalAddress wrPtr;
	Uint32 pic_enc_result;

    VLOG(TRACE, "[+] [%d]%s.h:%p\n",__LINE__,__func__,(void *)pCodecInst);
	pEncInfo = &pCodecInst->CodecInfo->encInfo;

	if (pCodecInst->loggingEnable)
		vdi_log(pCodecInst->coreIdx, PIC_RUN, 0);

	pic_enc_result = VpuReadReg(pCodecInst->coreIdx, RET_ENC_PIC_SUCCESS);
	if (pic_enc_result & (1 << 31)) {
		return RETCODE_MEMORY_ACCESS_VIOLATION;
	}

	if (pCodecInst->productId == PRODUCT_ID_980) {
		if (pic_enc_result & 2) { //top field coding done
			if (!pEncInfo->fieldDone)
				pEncInfo->fieldDone = 1;
		} else {
			pEncInfo->frameIdx = VpuReadReg(pCodecInst->coreIdx,
							RET_ENC_PIC_FRAME_NUM);
			pEncInfo->fieldDone = 0;
		}
	}

	info->picType = VpuReadReg(pCodecInst->coreIdx, RET_ENC_PIC_TYPE);

	if (pEncInfo->ringBufferEnable == 0) {
		rdPtr = VpuReadReg(pCodecInst->coreIdx,
				   pEncInfo->streamRdPtrRegAddr);
		wrPtr = VpuReadReg(pCodecInst->coreIdx,
				   pEncInfo->streamWrPtrRegAddr);
		info->bitstreamBuffer = rdPtr;
		info->bitstreamSize = wrPtr - rdPtr;
	}

	info->numOfSlices =
		VpuReadReg(pCodecInst->coreIdx, RET_ENC_PIC_SLICE_NUM);
	info->bitstreamWrapAround =
		VpuReadReg(pCodecInst->coreIdx, RET_ENC_PIC_FLAG);
	info->reconFrameIndex =
		VpuReadReg(pCodecInst->coreIdx, RET_ENC_PIC_FRAME_IDX);
	info->reconFrame = pEncInfo->frameBufPool[info->reconFrameIndex];
	info->encSrcIdx = info->reconFrameIndex;

	pEncInfo->streamWrPtr =
		VpuReadReg(pCodecInst->coreIdx, pEncInfo->streamWrPtrRegAddr);
	pEncInfo->streamEndflag =
		VpuReadReg(pCodecInst->coreIdx, BIT_BIT_STREAM_PARAM);

	info->frameCycle = VpuReadReg(pCodecInst->coreIdx, BIT_FRAME_CYCLE);
	info->rdPtr = pEncInfo->streamRdPtr;
	info->wrPtr = pEncInfo->streamWrPtr;

    VLOG(TRACE, "[-] [%d]%s.h:%p.ret:RETCODE_SUCCESS\n",__LINE__,__func__,(void *)pCodecInst);
	return RETCODE_SUCCESS;
}

RetCode Coda9VpuEncGiveCommand(CodecInst *pCodecInst, CodecCommand cmd,
			       void *param)
{
	RetCode ret = RETCODE_SUCCESS;

	UNREFERENCED_PARAMETER(cmd);
	UNREFERENCED_PARAMETER(param);

	switch (cmd) {
	default:
		ret = RETCODE_NOT_SUPPORTED_FEATURE;
	}

	return ret;
}
