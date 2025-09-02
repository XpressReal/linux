#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/tee_drv.h>
#include <linux/dma-buf.h>
#include <media/v4l2-mem2mem.h>
#include "ve1_vpuapifunc.h"
#include "ve1_wrapper.h"
#include "ve1_v4l2.h"
#include "ve1_mem.h"
#include "drv_if.h"
#include "ve_common.h"
#include "vpu.h"
#include "ve1_config.h"
#define VE1_WRAPPER_TAG "[VE1_WRAPPER]"

// if #define VE1_ALLOC_FRAME_BUFFER_BY_VDI, ve1_wrapper allocate frame buffer by vdi_allocate_dma_memory_no_mmap()
// or ve1_wrapper allocate frame buffer by ve1_alloc_frame_buffer()
//#define VE1_ALLOC_FRAME_BUFFER_BY_VDI

//#define VE1_CHECK_DFB_MD5_EN
//#if defined(VE1_CHECK_DFB_MD5_EN)
#if defined(VE1_CHECK_DFB_MD5_EN) || defined(VE1_CHECK_USERDATA_MD5_EN)
#include <crypto/hash.h>
#define VE1_MD5_DIGEST_SIZE 16
static unsigned char ve1_md5_digest[VE1_MD5_DIGEST_SIZE];
#endif

//#define RTKVE1_DUMP_BS_EN
#if defined(RTKVE1_DUMP_BS_EN)
static int gBsDumpSerial = 0;
#endif

//#define RTKVE1_DUMP_YUV_EN
#if defined(RTKVE1_DUMP_YUV_EN)
//#define VE1_TEST_DUMP_YUV_FRAME_NUM 30
static int gYuvDumpSerial = 0;
#endif

#include <linux/dma-map-ops.h>
#include <soc/realtek/memory.h>

#define IS_4K(w, h) (((w) * (h)) > (3200 * 1800))
#define MAX_CHUNK_HEADER_SIZE                                                  \
	256 * 1024 //DEFAULT_STREAMBUFFER_SIZE //Fuchun 20131225 don't need big size
#define MAKE_FOURCC(a, b, c, d)                                                \
	(((unsigned char)a) | ((unsigned char)b << 8) |                        \
	 ((unsigned char)c << 16) | ((unsigned char)d << 24))

#define EXTRA_DEC_PIC_BUF 2

extern void rtkve1_add_displayble_frame_to_list(struct ve1_ctx *ctx);
extern void ve1_show_displayable_frame_list(struct ve1_ctx *ctx);
#if defined(ENABLE_TEE_DRM_FLOW)
extern int ta_TEEapi_memcpy_a7(struct tee_context *teeapi_ctx,
			       unsigned int teeapi_tee_session,
			       unsigned int dstPAddr, unsigned char *buf,
			       int size);
extern int ta_TEEapi_memcpy(struct tee_context *teeapi_ctx,
			    unsigned int teeapi_tee_session,
			    unsigned int dstPhysAddr, unsigned int srtPhysAddr,
			    int size);
#endif
extern int ta_TEEapi_OMX_CC_API(struct tee_context *teeapi_ctx,
				unsigned int teeapi_tee_session,
				unsigned int src_addr, unsigned char *dst_addr,
				unsigned int size, unsigned int codec_type,
				unsigned int mode);

extern unsigned int vpu_debug;

enum { H264_PIC_STRUCT_FRAME,
       H264_PIC_STRUCT_TOP_FIELD,
       H264_PIC_STRUCT_BOTTOM_FIELD,
       H264_PIC_STRUCT_TOP_BOTTOM,
       H264_PIC_STRUCT_BOTTOM_TOP,
       H264_PIC_STRUCT_TOP_BOTTOM_TOP,
       H264_PIC_STRUCT_BOTTOM_TOP_BOTTOM,
       H264_PIC_STRUCT_FRAME_DOUBLING,
       H264_PIC_STRUCT_FRAME_TRIPLING } VE1_H264_PIC_STRUCT;

char *GetGitVersion(void)
{
#if defined(VPUAPIGITVER)
	return (char *)VPUAPIGITVER;
#else
	return NULL;
#endif
}

#ifdef VPU_GET_CC
int ParseUserDataInfo(USER_DATA_INFO *pUserDataInfo, char *pUserDataBuf)
{
	char *pTmpBuf = NULL;
	int ret = 0, i, offset;

	pTmpBuf = pUserDataBuf;
	pUserDataInfo->nUserDataNum =
		(short)((pTmpBuf[0] << 8) | (pTmpBuf[1] << 0));
	pUserDataInfo->nTotalUserDataSize =
		(short)((pTmpBuf[2] << 8) | (pTmpBuf[3] << 0));
	pTmpBuf = pUserDataBuf + 8;
	for (i = 0, offset = USER_DATA_INFO_OFFSET;
	     i < pUserDataInfo->nUserDataNum; ++i, pTmpBuf += 8) {
		pUserDataInfo->nUserDataType[i] =
			(short)((pTmpBuf[0] << 8) | (pTmpBuf[1] << 0));
		pUserDataInfo->nUserDataSize[i] =
			(short)((pTmpBuf[2] << 8) | (pTmpBuf[3] << 0));
		pUserDataInfo->nUserDataOffset[i] = offset;

		offset += (pUserDataInfo->nUserDataSize[i] + 7) / 8 * 8;
		ret++;
	}
	return ret;
}

#define CC_MAGIC_NUMBER 0x01020304
#define SEI_USER_DATA_REGISTERED_ITU_T_T35 4 //temp define
#define itu_t_t35_country_code 181

void FindH264CCInUserData(USER_DATA_INFO *pUserDataInfo, char *pUserDataBuf,
			  long long PTS, char *dst)
{
	int i;
	char *pCCdata = NULL;
	short itu_t_t35_provider_code;
	unsigned int nUserDataSize = 0;
	int offset = 0;
	int dst_offset = 72;
	int magic = CC_MAGIC_NUMBER;
	int packet_num = 0;
	int *header = (int *)dst;

	for (i = 0; i < pUserDataInfo->nUserDataNum; ++i) {
		if (pUserDataInfo->nUserDataType[i] ==
		    SEI_USER_DATA_REGISTERED_ITU_T_T35) {
			pCCdata = (pUserDataBuf +
				   pUserDataInfo->nUserDataOffset[i]);
			if (pCCdata[0] != itu_t_t35_country_code) {
				ve1_err(VE1_WRAPPER_TAG,
					"[H264 CC] ERR @ %s %d\n", __func__,
					__LINE__);
				continue;
			}
			itu_t_t35_provider_code =
				(short)((pCCdata[1] << 8) |
					(pCCdata[2])); // ARM is little-endian

			switch (itu_t_t35_provider_code) {
			case 47: // Direct TV
				if (pCCdata[3] == 3) {
					int *packet_start = (int *)dst;
					unsigned int *pCCValue =
						(unsigned int *)(dst +
								 dst_offset);

					offset = pUserDataInfo
							 ->nUserDataOffset[i] +
						 2;
					nUserDataSize =
						pUserDataInfo->nUserDataSize[i] -
						2;
					packet_start[3 + packet_num] =
						dst_offset;
					pCCValue[0] = RTK_CC_SYNC;
					pCCValue[1] = nUserDataSize;
					dst_offset += 16;
					memcpy(dst + dst_offset,
					       pUserDataBuf + offset,
					       nUserDataSize);
					dst_offset += nUserDataSize;
					packet_num++;
				}
				break;
			case 49: // ATSC
				if (pCCdata[3] == 'G' && pCCdata[4] == 'A' &&
				    pCCdata[5] == '9' && pCCdata[6] == '4' &&
				    pCCdata[7] == 3) {
					int *packet_start = (int *)dst;
					unsigned int *pCCValue =
						(unsigned int *)(dst +
								 dst_offset);

					offset = pUserDataInfo
							 ->nUserDataOffset[i] +
						 3;
					nUserDataSize =
						pUserDataInfo->nUserDataSize[i] -
						3;
					packet_start[3 + packet_num] =
						dst_offset;
					pCCValue[0] = RTK_CC_SYNC;
					pCCValue[1] = nUserDataSize;
					dst_offset += 16;
					memcpy(dst + dst_offset,
					       pUserDataBuf + offset,
					       nUserDataSize);
					dst_offset += nUserDataSize;
					packet_num++;
				}
				break;
			default:
				break;
			}
		}
	}

	header = (int *)dst;
	header[0] = magic;
	header[1] = dst_offset;
	header[2] = packet_num;
}

void FindMpeg2CCInUserData(USER_DATA_INFO *pUserDataInfo, char *pUserDataBuf,
			   long long PTS, char *dst)
{
	int i;
	char *p = NULL;
	unsigned int nUserDataSize = 0;
	int offset = 0;
	int dst_offset = 72;
	int magic = CC_MAGIC_NUMBER;
	int packet_num = 0;
	bool have_ga94 = false;
	int *header = (int *)dst;

	for (i = 0; i < pUserDataInfo->nUserDataNum; ++i) {
		p = pUserDataBuf + pUserDataInfo->nUserDataOffset[i];
		if (p[0] == 'G' && p[1] == 'A' && p[2] == '9' && p[3] == '4') {
			have_ga94 = true;
		}
	}

	for (i = 0; i < pUserDataInfo->nUserDataNum; ++i) {
		p = pUserDataBuf + pUserDataInfo->nUserDataOffset[i];

		if (p[0] == 'G' && p[1] == 'A' && p[2] == '9' && p[3] == '4') {
			int *packet_start = (int *)dst;
			unsigned int *pCCValue =
				(unsigned int *)(dst + dst_offset);

			offset = pUserDataInfo->nUserDataOffset[i];
			nUserDataSize = pUserDataInfo->nUserDataSize[i];
			packet_start[3 + packet_num] = dst_offset;
			pCCValue[0] = RTK_CC_SYNC;
			pCCValue[1] = nUserDataSize;
			dst_offset += 16;
			memcpy(dst + dst_offset, pUserDataBuf + offset,
			       nUserDataSize);
			dst_offset += nUserDataSize;
			packet_num++;

		} else if (p[0] == 0x03 && (p[1] & 0x7f) == 0x01) {
			if (have_ga94 == false) {
				int *packet_start = (int *)dst;
				unsigned int *pCCValue =
					(unsigned int *)(dst + dst_offset);

				offset = pUserDataInfo->nUserDataOffset[i];
				nUserDataSize = pUserDataInfo->nUserDataSize[i];
				packet_start[3 + packet_num] = dst_offset;
				pCCValue[0] = RTK_CC_SYNC;
				pCCValue[1] = nUserDataSize;
				dst_offset += 16;
				memcpy(dst + dst_offset, pUserDataBuf + offset,
				       nUserDataSize);
				dst_offset += nUserDataSize;
				packet_num++;
			}
		}
	}

	header = (int *)dst;
	header[0] = magic;
	header[1] = dst_offset;
	header[2] = packet_num;
}

int ProcessCC(struct ve1_ctx *ctx, unsigned char *cc_buf, unsigned int cc_size,
	      long long PTS, int decode_index, int display_index,
	      int codec_type)
{
	if (display_index >= MPEG2_CC_REG_FRAME_MAX) {
		ve1_err(VE1_WRAPPER_TAG,
			"display_index is overflow!!! display_index %d > %d\n",
			display_index, MPEG2_CC_REG_FRAME_MAX);
		return -1;
	}

	if (!cc_size)
		return 0;

	if (ctx->is_svp) {
		DecHandle decHandle = NULL;

		if (ctx->decHandle == NULL) {
			ve1_err(VE1_WRAPPER_TAG, "decHandle == NULL\n");
			return -1;
		}
		decHandle = (DecHandle)ctx->decHandle;

		if (decode_index >= 0 &&
		    ctx->m_CCDecodeOrderWp[decode_index] != NULL) {
			if (ta_TEEapi_OMX_CC_API(
				    (struct tee_context *)decHandle->teeapi_ctx,
				    decHandle->teeapi_tee_session,
				    ctx->userDataBufPhysAddr,
				    ctx->m_CCDecodeOrderWp[decode_index],
				    USER_DATA_SRC_BUF_SIZE, codec_type,
				    ENUM_CC_P) < 0) {
				ve1_err(VE1_WRAPPER_TAG,
					"In[%s][%d] ta_TEEapi_OMX_CC_API fail!!",
					__func__, __LINE__);
			}
		}
	} else {
		USER_DATA_INFO nUserDataInfo;
		ParseUserDataInfo(&nUserDataInfo, (char *)cc_buf);
		switch (codec_type) {
		case ENUM_CC_MPGE2:
			FindMpeg2CCInUserData(
				&nUserDataInfo, (char *)cc_buf, PTS,
				ctx->m_CCDecodeOrderWp[decode_index]);
			break;
		case ENUM_CC_H264:
			FindH264CCInUserData(
				&nUserDataInfo, (char *)cc_buf, PTS,
				ctx->m_CCDecodeOrderWp[decode_index]);
			break;
		default:
			break;
		}
	}

	return 0;
}

void ProcessCC_Display(void *pCtx, long long PTS,
		       int display_index) //send CC to AP
{
	struct ve1_ctx *ctx;
	int i;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (display_index >= 0 &&
	    ctx->m_CCDecodeOrderWp[display_index] != NULL) {
		int *header = (int *)ctx->m_CCDecodeOrderWp[display_index];

		if (cc_isCCInit()) {
			ve1_err(VE1_WRAPPER_TAG, " cc_isCCInit = false/n");
			cc_data_channel_init();
		}
		// send data back to AP via fifo
		if (cc_isCCReaderReady()) {
			int total_size;
			long long *pRTKHeader;
			char *check;

			if (header[0] != CC_MAGIC_NUMBER) {
				return;
			}

			total_size = header[1] - 72;
			if (total_size <= 0) {
				ve1_err(VE1_WRAPPER_TAG,
					"get wrong cc header - magic:%x size:%d packet_num:%d\n",
					header[0], header[1], header[2]);
				return;
			}

			for (i = 0; i < header[2]; i++) {
				int offset = header[3 + i];

				if (offset >
				    USER_DATA_SRC_BUF_SIZE -
					    RTK_CC_HEADER_SIZE) { //USER_DATA_SRC_BUF_SIZE
					if ((ctx->cc_error_count % 100) == 0) {
						ve1_err(VE1_WRAPPER_TAG,
							"MPEG2 cc get wrong offset:%d index:%d\n",
							offset, i);
					}
					ctx->cc_error_count++;
					return;
				}

				pRTKHeader =
					(long long *)(ctx->m_CCDecodeOrderWp
							      [display_index] +
						      offset);
				pRTKHeader[1] = PTS;
			}

			check = ctx->m_CCDecodeOrderWp[display_index] + 72;
			if (check[0] == 0x63 && check[1] == 0x4b &&
			    check[2] == 0x74 && check[3] == 0x52) {
				cc_data_channel_send(
					ctx->m_CCDecodeOrderWp[display_index] +
						72,
					total_size, cc_getCCReaderPid());
			}
		}
		// reset rtk header
		for (i = 0; i < RTK_CC_HEADER_SIZE + 72; i++)
			ctx->m_CCDecodeOrderWp[display_index][i] = 0;
	}
}
#endif //defined(VPU_GET_CC)

#if defined(VE1_CHECK_DFB_MD5_EN) || defined(VE1_CHECK_USERDATA_MD5_EN)
int ve1_md5_hash(unsigned char *result, int resultLen, char *data, int dataLen)
{
	int ret = 0;
	struct crypto_shash *tfm = NULL;
	struct shash_desc *desc = NULL;

	if ((result == NULL) || (resultLen <= 0) || (data == NULL) ||
	    (dataLen <= 0)) {
		return -1;
	}
	//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "data:0x%px.dataLen:%d\n", data,
	//	dataLen);
	memset(result, 0, resultLen);

	tfm = crypto_alloc_shash("md5", 0, 0);
	if (IS_ERR(tfm)) {
		tfm = NULL;
		ve1_err(VE1_WRAPPER_TAG, "IS_ERR(tfm)\n");
		ret = -1;
		goto out;
	}

	desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (desc == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "kmalloc desc fail\n");
		ret = -1;
		goto out;
	}

	desc->tfm = tfm;

	if (crypto_shash_init(desc) < 0) {
		ve1_err(VE1_WRAPPER_TAG, "crypto_shash_init() fail\n");
		ret = -1;
		goto out;
	}
	//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "crypto_shash_init() ok\n");

	if (crypto_shash_update(desc, data, dataLen) < 0) {
		ve1_err(VE1_WRAPPER_TAG, "crypto_shash_update() fail\n");
		ret = -1;
		goto out;
	}
	//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "crypto_shash_update() ok\n");

	if (crypto_shash_final(desc, result) < 0) {
		ve1_err(VE1_WRAPPER_TAG, "crypto_shash_final() fail\n");
		ret = -1;
		goto out;
	}
	//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "crypto_shash_final() ok\n");

	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
		result[0], result[1], result[2], result[3], result[4],
		result[5], result[6], result[7], result[8], result[9],
		result[10], result[11], result[12], result[13], result[14],
		result[15]);
out:
	if (desc) {
		kfree(desc);
	}
	if (tfm) {
		crypto_free_shash(tfm);
	}

	return ret;
}
#endif // #if defined(VE1_CHECK_DFB_MD5_EN)

void rtkve1_recycle_dpb(struct ve1_ctx *ctx)
{
	int i = 0;
	struct rtkve1_dpb_t *dpb = NULL;

	if (ctx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx == NULL\n");
		return;
	}

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return;
	}

	mutex_lock(&ctx->ve1_dma_mutex);
	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		dpb = &(ctx->dpb[i]);
		if (IS_RTKVE1_DPB_VALID(dpb->status) &&
			IS_RTKVE1_DPB_REG(dpb->status) &&
			IS_RTKVE1_DPB_WAIT_RECYCLE(dpb->status)) {
			if (dpb->seqNo != ctx->currSequenceNo) {
				ve1_err(VE1_WRAPPER_TAG,
					"unexpect to recyle old sequence dpb[%d].seq(%u, %u).regIndex:%d.phys_addr:0x%lx.vb2_v4l2_buf:0x%px\n",
					i, dpb->seqNo, ctx->currSequenceNo,
					dpb->regIndex, dpb->phys_addr, dpb->vb2_v4l2_buf);
				mutex_unlock(&ctx->ve1_dma_mutex);
				WARN_ON(1);
				return;
			}

			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"i:%d.status:0x%x.seqNo:%u\n",
				i,dpb->status,dpb->seqNo);
			dpb->status &= ~RTKVE1_DPB_ST_WAIT_RECYCLE;
			if (dpb->regIndex < ctx->regFbCount) {
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"VPU_DecClrDispFlag(%d).phys_addr:0x%lx\n",
					dpb->regIndex, dpb->phys_addr);
				VPU_DecClrDispFlag((DecHandle)ctx->decHandle, dpb->regIndex);
			}
			else {
				ve1_err(VE1_WRAPPER_TAG,
					"invalid dpb regIndex:%d, not recycle.phys_addr:0x%lx\n",
					dpb->regIndex, dpb->phys_addr);
			}
		}
	}
	mutex_unlock(&ctx->ve1_dma_mutex);
}

int rtkve1_add_capbuf_to_dpb(void *pCtx, unsigned long size,
			     unsigned long phys_addr, void *vb2_v4l2_buf)
{
	int ret = -1;

	struct ve1_ctx *ctx;
	int i = 0;

	if ((pCtx == NULL) || (size == 0) || (phys_addr == 0) ||
	    (vb2_v4l2_buf == NULL)) {
		ve1_err(VE1_WRAPPER_TAG,
			"invalid parameters.pCtx:0x%px.size:%ld.phys_addr:0x%lx.vb2_v4l2_buf:0x%px\n",
			pCtx, size, phys_addr, vb2_v4l2_buf);
		return ret;
	}
	ctx = (struct ve1_ctx *)pCtx;

	mutex_lock(&ctx->ve1_dma_mutex);
	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		if (IS_RTKVE1_DPB_EMPTY(ctx->dpb[i].status)) {
			break;
		}
	}
	if (i == VE1_ION_STRUCT_NUM) {
		ve1_err(VE1_WRAPPER_TAG, "all ctx->dpb[] are used\n");
		mutex_unlock(&ctx->ve1_dma_mutex);
		return ret;
	}
	ctx->dpb[i].size = size;
	ctx->dpb[i].status |= RTKVE1_DPB_ST_VALID;
	ctx->dpb[i].phys_addr = phys_addr;
	ctx->dpb[i].vb2_v4l2_buf = vb2_v4l2_buf;
	ctx->dpb[i].seqNo = ctx->currSequenceNo;
	ctx->cntCap2Dpb++;
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"add capbuf to dpb[%d].status:0x%x.phys_addr:0x%lx.size:%u.vb2_v4l2_buf:0x%px.seqNo:%u.cntCap2Dpb:%d\n",
		i, ctx->dpb[i].status, ctx->dpb[i].phys_addr, ctx->dpb[i].size,
		ctx->dpb[i].vb2_v4l2_buf,
		ctx->dpb[i].seqNo,
		ctx->cntCap2Dpb);
	ret = i;
	mutex_unlock(&ctx->ve1_dma_mutex);

	return ret;
}

void *rtkve1_find_dpb_unreg(void *pCtx)
{
	int i = 0;
	struct ve1_ctx *ctx;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return NULL;
	}
	ctx = (struct ve1_ctx *)pCtx;

	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		if ((ctx->dpb[i].seqNo == ctx->currSequenceNo) &&
			IS_RTKVE1_DPB_VALID(ctx->dpb[i].status) &&
			!IS_RTKVE1_DPB_REG(ctx->dpb[i].status)) {
				return (void *)&(ctx->dpb[i]);
		}
	}

	return NULL;
}

void *rtkve1_find_dpb_undequeue(void *pCtx, unsigned int seqNo)
{
	int i = 0;
	struct ve1_ctx *ctx;
	struct rtkve1_dpb_t *dpb = NULL;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return NULL;
	}
	ctx = (struct ve1_ctx *)pCtx;

	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		if ((ctx->dpb[i].seqNo == seqNo) &&
			IS_RTKVE1_DPB_VALID(ctx->dpb[i].status) &&
			IS_RTKVE1_DPB_REG(ctx->dpb[i].status) &&
			!IS_RTKVE1_DPB_DQ(ctx->dpb[i].status)) {
			dpb = &(ctx->dpb[i]);
		}
	}

	return (void *)dpb;
}

int rtkve1_unreg_dpbs(void *pCtx)
{
	int ret = 0;
	struct ve1_ctx *ctx;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return -1;
	}

	if (ctx->regFbCount == 0) {
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "ignore due to not reg dpb yet\n");
		return 0;
	}

	//mutex_lock(&ctx->ve1_mutex);
	ret = VPU_DecGiveCommand((DecHandle)ctx->decHandle, DEC_FREE_FRAME_BUFFER, 0x00);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG, "VPU_DecGiveCommand(DEC_FREE_FRAME_BUFFER) fail\n");
		//mutex_unlock(&ctx->ve1_mutex);
		return ret;
	}

	mutex_lock(&ctx->ve1_dma_mutex);
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG, "clear ctx->dpb[]\n");
	memset(ctx->dpb, 0, sizeof(struct rtkve1_dpb_t)*VE1_ION_STRUCT_NUM);
	mutex_unlock(&ctx->ve1_dma_mutex);

	ctx->cntCap2Dpb = 0;
	ctx->capReqBufsCnt = 0;
	ctx->regFbCount = 0;

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
	ctx->ve1DecState = VE1_STATE_DEC_SEQ_INIT_DONE;
	//mutex_unlock(&ctx->ve1_mutex);

	return ret;
}

void rtkve1_show_dpbs(struct ve1_ctx *ctx)
{
	int i = 0;

	if (ctx == NULL) {
		return;
	}

	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		if (ctx->dpb[i].size == 0) {
			continue;
		}
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"dpb[%d].status:0x%x.size:%d.phys_addr:0x%lx.vb2_v4l2_buf:0x%px.regIndex:%d.seqNo:%d\n",i,
			ctx->dpb[i].status, ctx->dpb[i].size,
			ctx->dpb[i].phys_addr,
			ctx->dpb[i].vb2_v4l2_buf,
			ctx->dpb[i].regIndex,
			ctx->dpb[i].seqNo);
	}
}

int rtkve1_register_dpbs(void *pCtx)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;
	int i = 0;
	DecOpenParam *decOP;
	DecInitialInfo *initialInfo;
	int mapType = LINEAR_FRAME_MAP;
	int fbHeight;
	int fbStride;
	FrameBufferFormat fbFormat;
	FrameBufferFormat wtlFormat;
	FrameBufferAllocInfo *fbAllocInfo;
	FrameBuffer *fbUser;
	struct rtkve1_dpb_t *dpb_unreg = NULL;
	unsigned int dispFlag = 0;
	unsigned int clearDispIndex = 0;
	SecAxiUse *secAxiUse;
	MaverickCacheConfig *decCacheConfig;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG,
			"invalid parameters.pCtx:0x%px\n",
			pCtx);
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return -1;
	}

	if (ctx->fbAllocInfo == NULL) {
		ctx->fbAllocInfo =
			kzalloc(sizeof(FrameBufferAllocInfo), GFP_KERNEL);
		if (ctx->fbAllocInfo == NULL) {
			ve1_err(VE1_WRAPPER_TAG, "kzalloc fbAllocInfo fail\n");
			return -1;
		}
	}

	if (ctx->fbUser == NULL) {
		ctx->fbUser = kzalloc(sizeof(FrameBuffer) * MAX_REG_FRAME,
				      GFP_KERNEL);
		if (ctx->fbUser == NULL) {
			ve1_err(VE1_WRAPPER_TAG, "kzalloc fbUser fail\n");
			return -1;
		}
	}

	decOP = (DecOpenParam *)ctx->decOP;
	initialInfo = (DecInitialInfo *)ctx->initialInfo;
	fbAllocInfo = (FrameBufferAllocInfo *)ctx->fbAllocInfo;

	fbFormat = FORMAT_420;
	wtlFormat = FORMAT_420;

	if ((mapType == TILED_FRAME_V_MAP) && (decOP->wtlEnable == 0) &&
	    (decOP->tiled2LinearEnable == 0)) {
		// trace VPU_GetFrameBufSize -> ProductCalculateFrameBufSize -> CalcLumaSize
		// if TILED_FRAME_V_MAP and CODA980, VPU force use 64 align to calculate the frame buffer height (unit_size_ver_lum),
		// it will influence the U_addr of YUV_STATE
		fbHeight = VPU_ALIGN64(initialInfo->picHeight);
	} else {
		fbHeight = VPU_ALIGN32(initialInfo->picHeight);
		//fbHeight = VPU_ALIGN16(initialInfo->picHeight);
	}
	fbStride = CalcStride(initialInfo->picWidth, initialInfo->picHeight,
			      fbFormat, decOP->cbcrInterleave,
			      (TiledMapType)mapType, 0);
	ctx->framebufSize =
		VPU_GetFrameBufSize(VE1_COREIDX, fbStride, fbHeight, mapType,
				    fbFormat, decOP->cbcrInterleave, NULL);
	ve1_info(
		VE1_WRAPPER_TAG,
		"af VPU_GetFrameBufSize().framebufSize:%d.fbStride:%d.fbHeight:%d.mapType:%d\n",
		ctx->framebufSize, fbStride, fbHeight, mapType);

	if (fbHeight == 0 || fbStride == 0 || ctx->framebufSize == 0) {
		ve1_err(VE1_WRAPPER_TAG,
			"incorrect fbHeight:%d.fbStride:%d.framebufSize:%d\n",
			fbHeight, fbStride, ctx->framebufSize);
		return -1;
	}

	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"cntCap2Dpb:%d.capReqBufsCnt:%d.regFbCount:%d\n",
			ctx->cntCap2Dpb, ctx->capReqBufsCnt, ctx->regFbCount);

	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		if ((ctx->dpb[i].seqNo == ctx->currSequenceNo) &&
			(ctx->dpb[i].size > 0) &&
			(!IS_RTKVE1_DPB_REG(ctx->dpb[i].status)) &&
			(ctx->dpb[i].size < ctx->framebufSize)) {
			ctx->error = TRUE;
			ve1_err(VE1_WRAPPER_TAG,
				"dpb[%d].seqNo:%d.size:%u < expected framebufSize:%d\n",
				i, ctx->currSequenceNo,
				ctx->dpb[i].size,
				ctx->framebufSize);
			WARN_ON(1);
			return -1;
		}
	}

	fbAllocInfo->format = fbFormat;
	fbAllocInfo->cbcrInterleave = decOP->cbcrInterleave;
	fbAllocInfo->mapType = mapType;
	fbAllocInfo->stride = fbStride;
	fbAllocInfo->height = fbHeight;
	fbAllocInfo->lumaBitDepth = initialInfo->lumaBitdepth;
	fbAllocInfo->chromaBitDepth = initialInfo->chromaBitdepth;
	fbAllocInfo->num = ctx->capReqBufsCnt;
	fbAllocInfo->endian = decOP->frameEndian;
	fbAllocInfo->type = FB_TYPE_CODEC;

	mutex_lock(&ctx->ve1_dma_mutex);
	for (i = 0; i < ctx->capReqBufsCnt; i++) {
		dpb_unreg = (struct rtkve1_dpb_t *)rtkve1_find_dpb_unreg((void *)ctx);
		if (dpb_unreg == NULL) {
			break;
		}
		fbUser = ((FrameBuffer *)ctx->fbUser) + i;
		dpb_unreg->regIndex = i;
		dpb_unreg->status |= RTKVE1_DPB_ST_REG;
		fbUser->size = dpb_unreg->size;
		fbUser->bufY = dpb_unreg->phys_addr;
		fbUser->bufCb = -1;
		fbUser->bufCr = -1;
		fbUser->updateFbInfo = TRUE;
		ctx->regFbCount++;
		clearDispIndex |= (1<<i);
		dispFlag = ~clearDispIndex;
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"regFbCount:%d.dispFlag:0x%x\n",
			ctx->regFbCount,dispFlag);
	}
	mutex_unlock(&ctx->ve1_dma_mutex);

	ret = VPU_DecAllocateFrameBuffer((DecHandle)ctx->decHandle,
					 *((FrameBufferAllocInfo *)fbAllocInfo),
					 (FrameBuffer *)ctx->fbUser);
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"af VPU_DecAllocateFrameBuffer.ret:%d\n", ret);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG,
			"VPU_DecAllocateFrameBuffer fail.ret:%d\n", ret);
		return -1;
	}
	for (i = 0; i < ctx->capReqBufsCnt; i++) {
		fbUser = ((FrameBuffer *)ctx->fbUser) + i;
		ve1_dbg(
			VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"fbUser[%d].bufY:0x%x.size:%d.myIndex:%d.stride:%d.h:%d.seqNo:%d\n",
			i, fbUser->bufY, fbUser->size, fbUser->myIndex,
			fbUser->stride, fbUser->height, ctx->currSequenceNo);
	}

	if (ctx->secAxiUse == NULL) {
		ctx->secAxiUse = kzalloc(sizeof(SecAxiUse), GFP_KERNEL);
		if (ctx->secAxiUse == NULL) {
			ve1_err(VE1_WRAPPER_TAG, "kzalloc secAxiUse fail\n");
			return -1;
		}
	}
	secAxiUse = (SecAxiUse *)ctx->secAxiUse;
	memset(secAxiUse, 0, sizeof(SecAxiUse));
	secAxiUse->u.coda9.useBitEnable = USE_BIT_INTERNAL_BUF;
	secAxiUse->u.coda9.useIpEnable = USE_IP_INTERNAL_BUF;
	secAxiUse->u.coda9.useDbkYEnable = USE_DBKY_INTERNAL_BUF;
	secAxiUse->u.coda9.useDbkCEnable = USE_DBKC_INTERNAL_BUF;
	secAxiUse->u.coda9.useOvlEnable = USE_OVL_INTERNAL_BUF;
	secAxiUse->u.coda9.useBtpEnable = USE_BTP_INTERNAL_BUF;
	VPU_DecGiveCommand((DecHandle)ctx->decHandle, SET_SEC_AXI, secAxiUse);

	if (ctx->decCacheConfig == NULL) {
		ctx->decCacheConfig =
			kzalloc(sizeof(MaverickCacheConfig), GFP_KERNEL);
		if (ctx->decCacheConfig == NULL) {
			ve1_err(VE1_WRAPPER_TAG,
				"kzalloc decCacheConfig fail\n");
			return -1;
		}
	}
	decCacheConfig = (MaverickCacheConfig *)ctx->decCacheConfig;
	MaverickCache2Config(decCacheConfig,
			     TRUE, // decoder
			     (BOOL)decOP->cbcrInterleave, 0, 0, 3,
			     (TiledMapType)mapType, 15);
	VPU_DecGiveCommand((DecHandle)ctx->decHandle, SET_CACHE_CONFIG,
			   decCacheConfig);

	fbStride =
		CalcStride(initialInfo->picWidth, initialInfo->picHeight,
			fbFormat, decOP->cbcrInterleave,
			(decOP->wtlEnable == TRUE ? LINEAR_FRAME_MAP :
			(TiledMapType)(mapType)),
			0);
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"fbStride:%d.fbFormat:%d.mapType:%d\n", fbStride, fbFormat,
		mapType);

	ret = VPU_DecRegisterFrameBuffer((DecHandle)ctx->decHandle,
					 (FrameBuffer *)ctx->fbUser,
					 ctx->capReqBufsCnt, fbStride, fbHeight,
					 mapType);
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"af VPU_DecRegisterFrameBuffer.ret:%d\n", ret);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG,
			"VPU_DecRegisterFrameBuffer fail.ret:%d\n", ret);
		return -1;
	}

	ret = VPU_DecSetDispFlag((DecHandle)ctx->decHandle, dispFlag);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG,
			"VPU_DecSetDispFlag fail.ret:%d\n", ret);
		return -1;
	}
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"VPU_DecSetDispFlag.dispFlag:0x%x\n", dispFlag);

	return ret;
}

int rtkve1_register_new_dpb(void *pCtx, void* pDpb)
{
	int ret = 0;
	struct ve1_ctx *ctx;
	DecOpenParam *decOP;
	DecInitialInfo *initialInfo;
	int mapType = LINEAR_FRAME_MAP;
	int fbHeight;
	int fbStride;
	FrameBufferFormat fbFormat;
	FrameBufferFormat wtlFormat;
	FrameBufferAllocInfo *fbAllocInfo;
	FrameBuffer *fbUser;
	struct rtkve1_dpb_t *dpb = NULL;
	unsigned int clrDispFlagIndex = 0;
	unsigned long clrDispFlagPhysAddr = 0;

	if ((pCtx == NULL) || (pDpb == NULL)) {
		ve1_err(VE1_WRAPPER_TAG,
			"invalid parameters.pCtx:0x%px.pDpb:0x%px\n",
			pCtx,pDpb);
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;
	dpb = (struct rtkve1_dpb_t *)pDpb;

	if ((ctx->decHandle == NULL) || (ctx->fbAllocInfo == NULL) || (ctx->fbUser == NULL)) {
		ve1_err(VE1_WRAPPER_TAG,
			"ctx->decHandle == NULL || ctx->fbAllocInfo == NULL || ctx->fbUser == NULL\n");
		return -1;
	}

	decOP = (DecOpenParam *)ctx->decOP;
	initialInfo = (DecInitialInfo *)ctx->initialInfo;
	fbAllocInfo = (FrameBufferAllocInfo *)ctx->fbAllocInfo;

	fbFormat = FORMAT_420;
	wtlFormat = FORMAT_420;

	if ((mapType == TILED_FRAME_V_MAP) && (decOP->wtlEnable == 0) &&
		(decOP->tiled2LinearEnable == 0)) {
		// trace VPU_GetFrameBufSize -> ProductCalculateFrameBufSize -> CalcLumaSize
		// if TILED_FRAME_V_MAP and CODA980, VPU force use 64 align to calculate the frame buffer height (unit_size_ver_lum),
		// it will influence the U_addr of YUV_STATE
		fbHeight = VPU_ALIGN64(initialInfo->picHeight);
	} else {
		fbHeight = VPU_ALIGN32(initialInfo->picHeight);
	}
	fbStride = CalcStride(initialInfo->picWidth, initialInfo->picHeight,
				fbFormat, decOP->cbcrInterleave,
				(TiledMapType)mapType, 0);

	if (fbHeight == 0 || fbStride == 0) {
		ve1_err(VE1_WRAPPER_TAG,
			"incorrect fbHeight:%d.fbStride:%d.framebufSize:%d\n",
			fbHeight, fbStride, ctx->framebufSize);
		return -1;
	}

	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"cntCap2Dpb:%d.capReqBufsCnt:%d.regFbCount:%d\n",
		ctx->cntCap2Dpb, ctx->capReqBufsCnt, ctx->regFbCount);

	if (dpb->size < ctx->framebufSize) {
		ve1_err(VE1_WRAPPER_TAG,
			"dpb.size:%u < expected framebufSize:%d\n",
			dpb->size,
			ctx->framebufSize);
		WARN_ON(1);
		return -1;
	}

	mutex_lock(&ctx->ve1_dma_mutex);
	dpb->regIndex = ctx->regFbCount;
	dpb->status |= RTKVE1_DPB_ST_REG;
	fbUser = ((FrameBuffer *)ctx->fbUser) + ctx->regFbCount;
	fbUser->size = dpb->size;
	fbUser->bufY = dpb->phys_addr;
	fbUser->bufCb = -1;
	fbUser->bufCr = -1;
	fbUser->updateFbInfo = TRUE;
	clrDispFlagIndex = ctx->regFbCount;
	clrDispFlagPhysAddr = dpb->phys_addr;
	ctx->regFbCount++;
	mutex_unlock(&ctx->ve1_dma_mutex);

	ret = VPU_DecAllocateFrameBuffer((DecHandle)ctx->decHandle,
			*((FrameBufferAllocInfo *)fbAllocInfo),
			(FrameBuffer *)ctx->fbUser);
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"af VPU_DecAllocateFrameBuffer.ret:%d\n", ret);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG,
			"VPU_DecAllocateFrameBuffer fail.ret:%d\n", ret);
		return -1;
	}
	ve1_dbg(
		VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"fbUser[%d].bufY:0x%x.size:%d.myIndex:%d.stride:%d.h:%d.seqNo:%d\n",
		(ctx->regFbCount-1), fbUser->bufY, fbUser->size, fbUser->myIndex,
		fbUser->stride, fbUser->height, ctx->currSequenceNo);

	ret = VPU_DecRegisterFrameBuffer((DecHandle)ctx->decHandle,
			(FrameBuffer *)ctx->fbUser,
			ctx->capReqBufsCnt, fbStride, fbHeight,
			mapType);
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"af VPU_DecRegisterFrameBuffer.ret:%d\n", ret);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG,
			"VPU_DecRegisterFrameBuffer fail.ret:%d\n", ret);
		return -1;
	}

	ret = VPU_DecClrDispFlag((DecHandle)ctx->decHandle, clrDispFlagIndex);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG,
			"VPU_DecClrDispFlag fail.ret:%d\n", ret);
		return -1;
	}
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"VPU_DecClrDispFlag(%d).phys_addr:0x%lx\n",
		clrDispFlagIndex, clrDispFlagPhysAddr);

	return ret;
}

int rtkve1_check_new_dpb(void *pCtx)
{
	int ret = 0;
	struct ve1_ctx *ctx;
	struct rtkve1_dpb_t *dpb_unreg = NULL;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG,
			"invalid parameters.pCtx:0x%px\n",
			pCtx);
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	do {
		mutex_lock(&ctx->ve1_dma_mutex);
		dpb_unreg = (struct rtkve1_dpb_t *)rtkve1_find_dpb_unreg((void *)ctx);
		mutex_unlock(&ctx->ve1_dma_mutex);
		if (dpb_unreg == NULL) {
			break;
		}
		ret = rtkve1_register_new_dpb(pCtx, dpb_unreg);
		if (ret != 0) {
			ve1_err(VE1_WRAPPER_TAG,
				"rtkve1_register_new_dpb() fail.ret:%d\n",
				ret);
			break;
		}
	} while (1);

	return ret;
}

void rtkve1_flush_dpbs(void *pCtx)
{
	struct ve1_ctx *ctx;
	int i = 0;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "invalid parameters.pCtx:0x%px\n",
			pCtx);
		return;
	}
	ctx = (struct ve1_ctx *)pCtx;

	mutex_lock(&ctx->ve1_dma_mutex);
	//rtkve1_show_dpbs(ctx);
	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		if ((ctx->dpb[i].seqNo == ctx->currSequenceNo) &&
			IS_RTKVE1_DPB_VALID(ctx->dpb[i].status) &&
			IS_RTKVE1_DPB_REG(ctx->dpb[i].status) &&
			!IS_RTKVE1_DPB_DQ(ctx->dpb[i].status)) {
			if ((ctx->lastDoneCapBuf == NULL) ||
				((ctx->lastDoneCapBuf != NULL) && (ctx->dpb[i].vb2_v4l2_buf != ctx->lastDoneCapBuf))) {
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
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
		// for those cap_bufs which cap_qbuf before new seq inited after seq changed, its size may not match new seq
		if (IS_RTKVE1_DPB_VALID(ctx->dpb[i].status) &&
			!IS_RTKVE1_DPB_REG(ctx->dpb[i].status)) {
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
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
		if (IS_RTKVE1_DPB_VALID(ctx->dpb[i].status)) {
			//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			//	"clear valid status.status:0x%x.regIndex:%d.vb2_v4l2_buf:0x%px.phys_addr:0x%lx.seqNo:%u\n",
			//	ctx->dpb[i].status,
			//	ctx->dpb[i].regIndex,
			//	ctx->dpb[i].vb2_v4l2_buf,
			//	ctx->dpb[i].phys_addr,
			//	ctx->dpb[i].seqNo);
			ctx->dpb[i].status &= ~RTKVE1_DPB_ST_VALID;
		}
		if (IS_RTKVE1_DPB_DQ(ctx->dpb[i].status)) {
			//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			//	"clear dq status.status:0x%x.regIndex:%d.vb2_v4l2_buf:0x%px.phys_addr:0x%lx.seqNo:%u\n",
			//	ctx->dpb[i].status,
			//	ctx->dpb[i].regIndex,
			//	ctx->dpb[i].vb2_v4l2_buf,
			//	ctx->dpb[i].phys_addr,
			//	ctx->dpb[i].seqNo);
			ctx->dpb[i].status &= ~RTKVE1_DPB_ST_DQ;
		}
		if (IS_RTKVE1_DPB_WAIT_RECYCLE(ctx->dpb[i].status)) {
			//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			//	"clear wait recycle status.status:0x%x.regIndex:%d.vb2_v4l2_buf:0x%px.phys_addr:0x%lx.seqNo:%u\n",
			//	ctx->dpb[i].status,
			//	ctx->dpb[i].regIndex,
			//	ctx->dpb[i].vb2_v4l2_buf,
			//	ctx->dpb[i].phys_addr,
			//	ctx->dpb[i].seqNo);
			ctx->dpb[i].status &= ~RTKVE1_DPB_ST_WAIT_RECYCLE;
		}
	}

	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		if ((ctx->dpb[i].seqNo != ctx->currSequenceNo) &&
		    IS_RTKVE1_DPB_VALID(ctx->dpb[i].status)) {
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"unexpected different dpb seqNo.status:0x%x.regIndex:%d.vb2_v4l2_buf:0x%px.phys_addr:0x%lx.seqNo:%u\n",
				ctx->dpb[i].status,
				ctx->dpb[i].regIndex,
				ctx->dpb[i].vb2_v4l2_buf,
				ctx->dpb[i].phys_addr,
				ctx->dpb[i].seqNo);
		}
	}
	//rtkve1_show_dpbs(ctx);
	mutex_unlock(&ctx->ve1_dma_mutex);
}
#if 0
int ve1_alloc_frame_buffer(void *pCtx, unsigned int size,
			   unsigned long *phys_addr)
{
	int ret = -1;
	struct ve1_ctx *ctx;

	if ((pCtx == NULL) || (size == 0) || (phys_addr == NULL)) {
		ve1_err(VE1_WRAPPER_TAG,
			"invalid parameters.pCtx:0x%px.size:%d.phys_addr:0x%px\n",
			pCtx, size, phys_addr);
		return ret;
	}
	ctx = (struct ve1_ctx *)pCtx;

	mutex_lock(&ctx->ve1_dma_mutex);
	do {
		int i = 0;
		unsigned int flags = 0;
		dma_addr_t dma_phys_addr = 0;
		void *virt_addr = NULL;
		ve1_mem_reg_entry_t *entry;

		if (ctx->is_svp) {
			flags = RTK_FLAG_NONCACHED | RTK_FLAG_HWIPACC |
				RTK_FLAG_PROTECTED_V2_VO_POOL;
		} else {
			flags = RTK_FLAG_NONCACHED | RTK_FLAG_HWIPACC |
				RTK_FLAG_SCPUACC;
		}

		for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
			if (IS_RTKVE1_DPB_EMPTY(ctx->dpb[i].status)) {
				break;
			}
		}
		if (i == VE1_ION_STRUCT_NUM) {
			ve1_err(VE1_WRAPPER_TAG,
				"all ve1_ion_buffer[] are used\n");
			break;
		}

		/* We can't limit the address from dma_alloc_coherent when size <= 4096 */
		if (size < SZ_8K)
			size = SZ_8K;

		virt_addr = dma_alloc_coherent(ctx->pdev, PAGE_ALIGN(size),
					       &dma_phys_addr,
					       (GFP_DMA | GFP_KERNEL));
		if (virt_addr == NULL) {
			ve1_err(VE1_WRAPPER_TAG,
				"dma_alloc_coherent() fail.size:%d(%d).flags:0x%x\n",
				PAGE_ALIGN(size), size, flags);
			break;
		}
		ctx->totIonAllocatedBytes += PAGE_ALIGN(size);

		*phys_addr = (unsigned long)dma_phys_addr;

		ctx->dpb[i].size = PAGE_ALIGN(size);
		ctx->dpb[i].status |= RTKVE1_DPB_ST_VALID;
		ctx->dpb[i].phys_addr = (unsigned long)*phys_addr;
		ctx->dpb[i].virt_addr = (unsigned long)virt_addr;

		// register dpb[i] to ve1_mem
		entry = kzalloc(sizeof(ve1_mem_reg_entry_t), GFP_KERNEL);
		if (entry) {
			entry->dev = ctx->pdev;
			entry->phys_addr = ctx->dpb[i].phys_addr;
			entry->addr = (void *) ctx->dpb[i].virt_addr;
			entry->size = ctx->dpb[i].size;
			ve1_mem_reg_add(entry);
			ctx->dpb[i].reg_entry = (void *)entry;
		}

		ret = i;
		break;
	} while (0);
	mutex_unlock(&ctx->ve1_dma_mutex);

	return ret;
}

void ve1_free_frame_buffer(void *pCtx, unsigned long phys_addr)
{
	struct ve1_ctx *ctx;

	if ((pCtx == NULL) || (phys_addr == 0)) {
		ve1_err(VE1_WRAPPER_TAG,
			"invalid parameters.pCtx:0x%px.phys_addr:0x%lx\n", pCtx,
			phys_addr);
		return;
	}
	ctx = (struct ve1_ctx *)pCtx;

	mutex_lock(&ctx->ve1_dma_mutex);
	do {
		int i = 0;

		for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
			if (ctx->dpb[i].phys_addr == phys_addr) {
				break;
			}
		}
		if (i == VE1_ION_STRUCT_NUM) {
			ve1_err(VE1_WRAPPER_TAG,
				"not find phys_addr:0x%lx in dpb\n", phys_addr);
			break;
		}

		if (ctx->dpb[i].reg_entry) {
			ve1_mem_reg_remove(phys_addr);
			kfree(ctx->dpb[i].reg_entry);
		}

		if (ctx->dpb[i].virt_addr) {
			dma_free_coherent(ctx->pdev, ctx->dpb[i].size,
					  (void *)ctx->dpb[i].virt_addr,
					  (dma_addr_t)phys_addr);
			ctx->totIonAllocatedBytes -= ctx->dpb[i].size;
		}

		memset(&ctx->dpb[i], 0, sizeof(struct rtkve1_dpb_t));
	} while (0);
	mutex_unlock(&ctx->ve1_dma_mutex);
}
#endif

#ifdef VPU_GET_CC
int ve1_enable_userdata(struct ve1_ctx *ctx)
{
	int ret = 0;
	vpu_buffer_t vdb;
	DecHandle decHandle = NULL;
	DecOpenParam *pDecOp;

	if (ctx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "invalid parameters.ctx:0x%px\n", ctx);
		return VE1_DEC_RETURN_INVALID;
	}

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "decHandle == NULL\n");
		return -1;
	}
	decHandle = (DecHandle)ctx->decHandle;
	pDecOp = (DecOpenParam *)ctx->decOP;

	if (ctx->userDataBufPhysAddr == 0) {
		unsigned int nBufSize =
			USER_DATA_SRC_BUF_SIZE; //(pDecOp->bitstreamFormat == STD_MPEG2 ? VPU_CC_BUF_SIZE : MVC_USERDATA_BUF_SIZE);
		unsigned int codec_type =
			(pDecOp->bitstreamFormat == STD_MPEG2 ? ENUM_CC_MPGE2 :
								ENUM_CC_H264);
		memset(&vdb, 0, sizeof(vpu_buffer_t));
		vdb.size = SIZE_REPORT_BUF;
		vdb.req_spec_region = 0;
		if (ctx->is_svp) {
			vdb.req_spec_region = VE_SECURE_PROTECTION;
		}

		if (vdi_allocate_dma_memory(VE1_COREIDX, &vdb, ctx->filp) < 0) {
			ve1_err(VE1_WRAPPER_TAG,
				"vdi_allocate_dma_memory fail\n");
			return VE1_DEC_RETURN_INVALID;
		}
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"vdi_allocate_dma_memory userdata(0x%lx,0x%lx,0x%lx,%d,%d)\n",
			vdb.phys_addr, vdb.virt_addr, vdb.base, vdb.size,
			vdb.req_spec_region);

		ctx->userDataBufSize = vdb.size;
		ctx->userDataBufPhysAddr = vdb.phys_addr;
		ctx->pUserDataBufVirtAddr = (void *)vdb.virt_addr;
		if (ctx->is_svp) {
			ta_TEEapi_OMX_CC_API(
				(struct tee_context *)decHandle->teeapi_ctx,
				decHandle->teeapi_tee_session,
				ctx->userDataBufPhysAddr, ctx->pUserDataSrcBuf,
				nBufSize, codec_type, ENUM_CC_AU);
			ta_TEEapi_OMX_CC_API(
				(struct tee_context *)decHandle->teeapi_ctx,
				decHandle->teeapi_tee_session,
				ctx->userDataBufPhysAddr, ctx->pUserDataSrcBuf,
				nBufSize, codec_type, ENUM_CC_S);
		}
	}

	ctx->userDataEnable = 1;
	ctx->userDataReportMode = 0;
	VPU_DecGiveCommand(decHandle, SET_ADDR_REP_USERDATA,
			   &ctx->userDataBufPhysAddr);
	VPU_DecGiveCommand(decHandle, SET_SIZE_REP_USERDATA,
			   &ctx->userDataBufSize);
	VPU_DecGiveCommand(decHandle, SET_USERDATA_REPORT_MODE,
			   &ctx->userDataReportMode);
	VPU_DecGiveCommand(decHandle, ENABLE_REP_USERDATA, 0);
	ve1_info(
		VE1_WRAPPER_TAG,
		"alloc & enable ve1 userdata(size:%d,phys:0x%x,virt:0x%px).tot:%d\n",
		ctx->userDataBufSize, ctx->userDataBufPhysAddr,
		ctx->pUserDataBufVirtAddr, ctx->totIonAllocatedBytes);

	return ret;
}

int ve1_get_userdata(struct ve1_ctx *ctx, unsigned char *pBuf,
		     unsigned int nBufSize)
{
	int ret = 0;
	DecOutputInfo *outputInfo = NULL;
	unsigned int totalSize;

	if ((ctx == NULL) || (pBuf == NULL) || (nBufSize == 0)) {
		ve1_err(VE1_WRAPPER_TAG,
			"invalid parameters.ctx:0x%px.pBuf:0x%px.nBufSize:%d\n",
			ctx, pBuf, nBufSize);
		return -1;
	}

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return VE1_DEC_RETURN_INVALID;
	}

	if (ctx->outputInfo == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->outputInfo == NULL\n");
		return VE1_DEC_RETURN_INVALID;
	}
	outputInfo = (DecOutputInfo *)ctx->outputInfo;

	if (outputInfo->decOutputExtData.userDataSize) {
		// The first USER_DATA_INFO_OFFSET bytes in user data buffer is header including userDataNum/userDataSize/userDataBufFull
		// (userDataSize+7)/8*8: the actual user data bytes occupied are align to 8. If the user data size is 804, the actual bytes occupied are 808.
		totalSize = (outputInfo->decOutputExtData.userDataSize + 7) /
				    8 * 8 +
			    USER_DATA_INFO_OFFSET;
		if (totalSize > nBufSize) {
			ve1_err(VE1_WRAPPER_TAG,
				"buffer size:%d to get user data is smaller than user data size:%d\n",
				nBufSize, totalSize);
			return VE1_DEC_RETURN_INVALID;
		}
		vdi_read_memory(VE1_COREIDX, ctx->userDataBufPhysAddr, pBuf,
				totalSize, VDI_LITTLE_ENDIAN);
		ret = totalSize;
	}
	return ret;
}
#endif

int VE1_DecInit(void *pCtx, void *videc_dev)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;
	int productId;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->ve1DecState != VE1_STATE_DEC_UNINIT) {
		ve1_err(VE1_WRAPPER_TAG, "invalid ve1DecState:%d\n",
			ctx->ve1DecState);
		return -1;
	}

	productId = VPU_GetProductId(VE1_COREIDX);
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "productId:%d\n", productId);
	ret = VPU_Init(VE1_COREIDX, videc_dev);
	ve1_info(VE1_WRAPPER_TAG, "af VPU_Init.ret:%d\n", ret);
	if (ret == RETCODE_NOT_FOUND_BITCODE_PATH) {
#if defined(ENABLE_TEE_DRM_FLOW)
		ret = RTK_VPU_InitWithBitcodeExt(VE1_COREIDX, FALSE, NULL, NULL,
						 ctx, videc_dev);
		ve1_info(VE1_WRAPPER_TAG,
			 "af RTK_VPU_InitWithBitcodeExt.ret:%d\n", ret);
#else
		ret = RTK_VPU_InitWithBitcode(VE1_COREIDX, FALSE, videc_dev);
		ve1_info(VE1_WRAPPER_TAG, "af RTK_VPU_InitWithBitcode.ret:%d\n",
			 ret);
#endif
	}
	if (ret == RETCODE_SUCCESS || ret == RETCODE_CALLED_BEFORE) {
		ctx->ve1DecState = VE1_STATE_DEC_INITED;
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"ret:%d.set ve1DecState:%d\n", ret, ctx->ve1DecState);
	} else {
		ve1_err(VE1_WRAPPER_TAG, "unexpected ret:%d\n", ret);
		ret = -1;
	}

	return ret;
}

int VE1_DecOpen(void *pCtx, void *pParam)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;
	struct ve1_decopen_param *param;
	DecOpenParam *pDecOp;
	CodStd codec_type;
#ifdef VPU_GET_CC
	int i;
#endif
	unsigned int version;
	unsigned int revision;
	unsigned int productId;

	if ((pCtx == NULL) || (pParam == NULL)) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL or pParam == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;
	param = (struct ve1_decopen_param *)pParam;

	if (ctx->ve1DecState != VE1_STATE_DEC_INITED &&
	    ctx->ve1DecState != VE1_STATE_DEC_CLOSED) {
		ve1_err(VE1_WRAPPER_TAG, "invalid ve1DecState:%d\n",
			ctx->ve1DecState);
		return -1;
	}

	VPU_GetVersionInfo(VE1_COREIDX, &version, &revision, &productId);
    ve1_info(VE1_WRAPPER_TAG, "Firmware : CustomerCode: %04x | version : %d.%d.%d rev.%d\n",
         (unsigned int)(version>>16), (unsigned int)((version>>(12))&0x0f), (unsigned int)((version>>(8))&0x0f), (unsigned int)((version)&0xff), revision);
	ve1_info(VE1_WRAPPER_TAG, "Hardware : %04x\n", productId);
	ve1_info(VE1_WRAPPER_TAG, "API      : %d.%d.%d\n\n", API_VERSION_MAJOR, API_VERSION_MINOR, API_VERSION_PATCH);

	if (ctx->decOP == NULL) {
		ctx->decOP = kzalloc(sizeof(DecOpenParam), GFP_KERNEL);
		if (ctx->decOP == NULL) {
			ve1_err(VE1_WRAPPER_TAG, "kzalloc decOP fail\n");
			return -1;
		}
	}
	pDecOp = (DecOpenParam *)ctx->decOP;

	switch (param->src_fmt_fourcc) {
	case V4L2_PIX_FMT_MPEG1:
	case V4L2_PIX_FMT_MPEG2:
		codec_type = STD_MPEG2;
		break;
	case V4L2_PIX_FMT_MPEG4:
		codec_type = STD_MPEG4;
		break;
	case V4L2_PIX_FMT_VP8:
		codec_type = STD_VP8;
		break;
	case V4L2_PIX_FMT_VC1_ANNEX_G:
		codec_type = STD_VC1;
		break;
	case V4L2_PIX_FMT_VC1_ANNEX_L:
		codec_type = STD_VC1;
		break;
	case V4L2_PIX_FMT_H264:
	default:
		codec_type = STD_AVC;
		break;
	}

	memset(pDecOp, 0, sizeof(DecOpenParam));
	pDecOp->coreIdx = VE1_COREIDX;
	pDecOp->bitstreamFormat = codec_type;
	pDecOp->bitstreamBuffer = ctx->bitstream.paddr;
	pDecOp->bitstreamBufferSize = ctx->bitstream.size;
	ve1_info(VE1_WRAPPER_TAG,
		 "bitstreamBuffer:0x%x.bitstreamBufferSize:%d\n",
		 pDecOp->bitstreamBuffer, pDecOp->bitstreamBufferSize);
	pDecOp->avcExtension = 0;
	pDecOp->bitstreamMode = BS_MODE_PIC_END;

	if (param->dst_fmt_fourcc == V4L2_PIX_FMT_NV21 ||
	    param->dst_fmt_fourcc == V4L2_PIX_FMT_NV12) {
		pDecOp->cbcrInterleave = 1;
		if (param->dst_fmt_fourcc == V4L2_PIX_FMT_NV21) {
			pDecOp->nv21 = 1;
		}
	}
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"src_fourcc:%4s.dst_fourcc:%4s.cbcrInterleave:%d.nv21:%d\n",
		(char *)&param->src_fmt_fourcc, (char *)&param->dst_fmt_fourcc,
		pDecOp->cbcrInterleave, pDecOp->nv21);

	pDecOp->cbcrOrder = CBCR_ORDER_NORMAL;
	pDecOp->frameEndian = VPU_FRAME_ENDIAN;
	pDecOp->streamEndian = VPU_STREAM_ENDIAN;
	pDecOp->bwbEnable = VPU_ENABLE_BWB;
	pDecOp->filp = ctx->filp;
	pDecOp->frameWidth = param->width;
	pDecOp->frameHeight = param->height;

	pDecOp->isUseProtectBuffer = ctx->is_svp;

	ve1_info(
		VE1_WRAPPER_TAG,
		"------------------------------ DECODER OPTIONS ------------------------------\n");
	ve1_info(VE1_WRAPPER_TAG, "[bitstreamFormat    ]: %d\n",
		 pDecOp->bitstreamFormat);
	ve1_info(VE1_WRAPPER_TAG, "[bitstreamBuffer    ]: 0x%x\n",
		 pDecOp->bitstreamBuffer);
	ve1_info(VE1_WRAPPER_TAG, "[bitstreamBufferSize]: %d\n",
		 pDecOp->bitstreamBufferSize);
	ve1_info(VE1_WRAPPER_TAG, "[mp4DeblkEnable     ]: %d\n",
		 pDecOp->mp4DeblkEnable);
	ve1_info(VE1_WRAPPER_TAG, "[avcExtension       ]: %d\n",
		 pDecOp->avcExtension);
	ve1_info(VE1_WRAPPER_TAG, "[mp4Class           ]: %d\n",
		 pDecOp->mp4Class);
	ve1_info(VE1_WRAPPER_TAG, "[tiled2LinearEnable ]: %d\n",
		 pDecOp->tiled2LinearEnable);
	ve1_info(VE1_WRAPPER_TAG, "[tiled2LinearMode   ]: %d\n",
		 pDecOp->tiled2LinearMode);
	ve1_info(VE1_WRAPPER_TAG, "[wtlEnable          ]: %d\n",
		 pDecOp->wtlEnable);
	ve1_info(VE1_WRAPPER_TAG, "[wtlMode            ]: %d\n",
		 pDecOp->wtlMode);
	ve1_info(VE1_WRAPPER_TAG, "[cbcrInterleave     ]: %d\n",
		 pDecOp->cbcrInterleave);
	ve1_info(VE1_WRAPPER_TAG, "[nv21               ]: %d\n", pDecOp->nv21);
	ve1_info(VE1_WRAPPER_TAG, "[cbcrOrder          ]: %d\n",
		 pDecOp->cbcrOrder);
	ve1_info(VE1_WRAPPER_TAG, "[BWB                ]: %d\n",
		 pDecOp->bwbEnable);
	ve1_info(VE1_WRAPPER_TAG, "[frameEndian        ]: %d\n",
		 pDecOp->frameEndian);
	ve1_info(VE1_WRAPPER_TAG, "[streamEndian       ]: %d\n",
		 pDecOp->streamEndian);
	ve1_info(VE1_WRAPPER_TAG, "[bitstreamMode      ]: %d\n",
		 pDecOp->bitstreamMode);
	ve1_info(VE1_WRAPPER_TAG, "[coreIdx            ]: %d\n",
		 pDecOp->coreIdx);
	ve1_info(VE1_WRAPPER_TAG, "[vbWork.size        ]: %d\n",
		 pDecOp->vbWork.size);
	ve1_info(VE1_WRAPPER_TAG, "[vbWork.phys_addr   ]: 0x%lx\n",
		 pDecOp->vbWork.phys_addr);
	ve1_info(VE1_WRAPPER_TAG, "[vbWork.base        ]: 0x%lx\n",
		 pDecOp->vbWork.base);
	ve1_info(VE1_WRAPPER_TAG, "[vbWork.virt_addr   ]: 0x%lx\n",
		 pDecOp->vbWork.virt_addr);
	ve1_info(VE1_WRAPPER_TAG, "[fbc_mode           ]: %d\n",
		 pDecOp->fbc_mode);
	ve1_info(VE1_WRAPPER_TAG, "[virtAxiID          ]: %d\n",
		 pDecOp->virtAxiID);
	ve1_info(VE1_WRAPPER_TAG, "[bwOptimization     ]: %d\n",
		 pDecOp->bwOptimization);
	ve1_info(VE1_WRAPPER_TAG, "[afbceEnable        ]: %d\n",
		 pDecOp->afbceEnable);
	ve1_info(VE1_WRAPPER_TAG, "[afbceFormat        ]: %d\n",
		 pDecOp->afbceFormat);
	ve1_info(VE1_WRAPPER_TAG, "[isUseProtectBuffer ]: %d\n",
		 pDecOp->isUseProtectBuffer);
	ve1_info(
		VE1_WRAPPER_TAG,
		"-----------------------------------------------------------------------------\n");

	ret = VPU_DecOpen((DecHandle *)&ctx->decHandle, pDecOp);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG, "VPU_DecOpen fail.ret:%d\n", ret);
		return -1;
	}
	ve1_info(VE1_WRAPPER_TAG, "af VPU_DecOpen.ret:%d.handle:0x%px\n", ret,
		 ctx->decHandle);

#ifdef VPU_GET_CC
	if ((pDecOp->bitstreamFormat == STD_AVC) ||
	    (pDecOp->bitstreamFormat == STD_MPEG2)) {
		// allocate user data buffer and enable ve1 user data
		ctx->pUserDataSrcBuf = (unsigned char *)kmalloc(
			USER_DATA_SRC_BUF_SIZE, GFP_KERNEL);
		ve1_enable_userdata(ctx);
	}
#endif

	ctx->ve1DecState = VE1_STATE_DEC_OPENED;
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "[-] set ve1DecState:%d\n",
		ctx->ve1DecState);

#ifdef VPU_GET_CC
	if (pDecOp->bitstreamFormat == STD_MPEG2 ||
	    pDecOp->bitstreamFormat == STD_AVC) {
		for (i = 0; i < MPEG2_CC_REG_FRAME_MAX; i++) {
			ctx->m_CCDecodeOrderWp[i] = kmalloc(
				USER_DATA_SRC_BUF_SIZE +
					USER_DATA_NUM_MAX * RTK_CC_HEADER_SIZE,
				GFP_KERNEL);
			if (ctx->m_CCDecodeOrderWp[i])
				memset(ctx->m_CCDecodeOrderWp[i], 0,
				       USER_DATA_SRC_BUF_SIZE +
					       USER_DATA_NUM_MAX *
						       RTK_CC_HEADER_SIZE);
		}
		ctx->cc_error_count = 0;

		cc_data_channel_init();
	}
#endif
	return ret;
}

int VE1_DecGetRdWrPtr(void *pCtx)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle != NULL) {
		if (((DecOpenParam *)ctx->decOP)->bitstreamMode ==
		    BS_MODE_PIC_END) {
			ret = VPU_DecGetBitstreamBufferEx(
				(DecHandle)ctx->decHandle, &ctx->vpuRdPtr,
				&ctx->vpuWrPtr, &ctx->vpuBsRingRoom);
		} else {
			ret = VPU_DecGetBitstreamBuffer(
				(DecHandle)ctx->decHandle, &ctx->vpuRdPtr,
				&ctx->vpuWrPtr, &ctx->vpuBsRingRoom);
		}
	}

	return ret;
}

int VE1_SetStreamEnd(void *pCtx)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return -1;
	}

	ret = VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
					   STREAM_END_SIZE);
	ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
		"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d.accuBsFeedBytes:%d\n",
		ret, STREAM_END_SIZE, ctx->accuBsFeedBytes);
	return ret;
}

int VE1_DecSeqInit(void *pCtx)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;
	BOOL seqInitEscape = FALSE;
	int int_reason;
	int bSeqInited = 0;
	DecInitialInfo *initialInfo;
	unsigned long long pts_unit = PTS_UNIT;
	unsigned int fps;
	unsigned long valid_data = 0;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return -1;
	}

	VE1_DecGetRdWrPtr(ctx);
	if (((DecOpenParam *)ctx->decOP)->bitstreamMode == BS_MODE_INTERRUPT) {
		valid_data = ve1_ring_valid_data(ctx->bitstream.paddr,
						 ctx->bitstream.paddr +
							 ctx->bitstream.size,
						 ctx->vpuRdPtr, ctx->vpuWrPtr);
		if (valid_data < 1024) {
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"valid data size:%ld < 1024.vpuRdPtr:0x%x.vpuWrPtr:0x%x\n",
				valid_data, ctx->vpuRdPtr, ctx->vpuWrPtr);
			ctx->bBufEmptyFlag = true;
			return ret;
		}
	}

	if (ctx->initialInfo == NULL) {
		ctx->initialInfo = kzalloc(sizeof(DecInitialInfo), GFP_KERNEL);
		if (ctx->initialInfo == NULL) {
			ve1_err(VE1_WRAPPER_TAG, "kzalloc initialInfo fail\n");
			return -1;
		}
	}
	initialInfo = (DecInitialInfo *)ctx->initialInfo;

	if (seqInitEscape) {
		if (ctx->ve1DecState >= VE1_STATE_DEC_OPENED &&
		    ctx->ve1DecState != VE1_STATE_DEC_SEQ_INIT_ISSUED) {
			ret = VPU_DecSetEscSeqInit((DecHandle)ctx->decHandle,
						   seqInitEscape);
			if (ret != RETCODE_SUCCESS) {
				ve1_err(VE1_WRAPPER_TAG,
					"VPU_DecSetEscSeqInit fail.ret:%d\n",
					ret);
				return -1;
			}
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"af VPU_DecSetEscSeqInit.ret:%d\n", ret);

			ret = VPU_DecGetInitialInfo((DecHandle)ctx->decHandle,
						    initialInfo);
			if (ret != RETCODE_SUCCESS) {
				ctx->ve1DecState = VE1_STATE_DEC_SEQ_INIT_DONE;
				ve1_err(VE1_WRAPPER_TAG,
					"VPU_DecGetInitialInfo fail.ret:%d\n",
					ret);
				return -1;
			} else {
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"af VPU_DecGetInitialInfo.ret:%d\n",
					ret);
				ctx->seqInited = 1;
				ctx->ve1DecState = VE1_STATE_DEC_SEQ_INIT_DONE;
				ctx->currSequenceNo++;
				ctx->timeTick = div_u64(pts_unit * 100, 2997);
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"set ve1DecState:%d.currSequenceNo:%d.timeTick:%lld\n",
					ctx->ve1DecState, ctx->currSequenceNo,
					ctx->timeTick);

				if (initialInfo->fRateDenominator != -1) {
					fps = initialInfo->fRateNumerator *
					      100 /
					      initialInfo->fRateDenominator;
					ctx->timeTick =
						div_u64(pts_unit * 100, fps);
				}

				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"min:%d.%dx%d(%dx%d).fps(%d/%d).timeTick:%lld\n",
					initialInfo->minFrameBufferCount,
					initialInfo->picWidth,
					initialInfo->picHeight,
					(initialInfo->picCropRect.right -
					 initialInfo->picCropRect.left),
					(initialInfo->picCropRect.bottom -
					 initialInfo->picCropRect.top),
					initialInfo->fRateNumerator,
					initialInfo->fRateDenominator,
					ctx->timeTick);
			}
		}
	} else {
		if (ctx->ve1DecState == VE1_STATE_DEC_SEQ_INIT_ISSUED) {
			goto waitSeqInitDone;
		}

		if (ctx->ve1DecState >= VE1_STATE_DEC_OPENED &&
		    ctx->ve1DecState != VE1_STATE_DEC_SEQ_INIT_ISSUED) {
			ret = VPU_DecIssueSeqInit((DecHandle)ctx->decHandle);
			if (ret != RETCODE_SUCCESS) {
				ve1_err(VE1_WRAPPER_TAG,
					"VPU_DecIssueSeqInit fail.ret:%d\n",
					ret);
				return -1;
			}
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"af VPU_DecIssueSeqInit.ret:%d\n", ret);
			ctx->ve1DecState = VE1_STATE_DEC_SEQ_INIT_ISSUED;
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"set ve1DecState:%d\n", ctx->ve1DecState);
		}

	waitSeqInitDone:
		while (1) {
			int_reason = VPU_WaitInterrupt(VE1_COREIDX, 10);
			if (int_reason == -1) {
				int_reason = 0;
			}
			if (int_reason) {
				VPU_ClearInterrupt(VE1_COREIDX);
				if (int_reason & (1 << INT_BIT_SEQ_INIT)) {
					ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
						"INT_BIT_SEQ_INIT\n");
					bSeqInited = 1;
				} else if (int_reason &
					   (1 << INT_BIT_BIT_BUF_EMPTY)) {
					ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
						"INT_BIT_BIT_BUF_EMPTY\n");
					ctx->bBufEmptyFlag = true;
				}
				break;
			}
		}

		if (bSeqInited) {
			ret = VPU_DecCompleteSeqInit((DecHandle)ctx->decHandle,
						     initialInfo);
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"af VPU_DecCompleteSeqInit.ret:%d.seqInitErrReason:0x%x\n",
				ret, initialInfo->seqInitErrReason);
			if (ret != RETCODE_SUCCESS) {
				ctx->ve1DecState = VE1_STATE_DEC_SEQ_INIT_DONE;
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"VPU_DecCompleteSeqInit fail.ret:%d\n",
					ret);

				if (((DecOpenParam *)ctx->decOP)->bitstreamMode ==
					    BS_MODE_ROLLBACK &&
				    initialInfo->seqInitErrReason & (1 << 31)) {
					// this happens only ROLLBACK mode case
				}
				return -1;
			}
			ctx->seqInited = 1;
			ctx->ve1DecState = VE1_STATE_DEC_SEQ_INIT_DONE;
			ctx->currSequenceNo++;
			ctx->timeTick = div_u64(pts_unit * 100, 2997);
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"set ve1DecState:%d.currSequenceNo:%d.timeTick:%lld\n",
				ctx->ve1DecState, ctx->currSequenceNo,
				ctx->timeTick);

			if (initialInfo->fRateDenominator != -1) {
				fps = initialInfo->fRateNumerator * 100 /
				      initialInfo->fRateDenominator;
				ctx->timeTick = div_u64(pts_unit * 100, fps);
			}

			ve1_info(
				VE1_WRAPPER_TAG,
				"min:%d.%dx%d(%dx%d).fps(%d/%d).timeTick:%lld\n",
				initialInfo->minFrameBufferCount,
				initialInfo->picWidth, initialInfo->picHeight,
				(initialInfo->picCropRect.right -
				 initialInfo->picCropRect.left),
				(initialInfo->picCropRect.bottom -
				 initialInfo->picCropRect.top),
				initialInfo->fRateNumerator,
				initialInfo->fRateDenominator, ctx->timeTick);

			// update vpuRdPtr to bsRdPtr
			VE1_DecGetRdWrPtr(pCtx);
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"vpuRdPtr:0x%x.vpuWrPtr:0x%x\n",
				ctx->vpuRdPtr, ctx->vpuWrPtr);
			ctx->bsRdPtr = ctx->vpuRdPtr;
		} else {
			ve1_err(VE1_WRAPPER_TAG, "seq init failed\n");
			return -1;
		}
	}

	return ret;
}

int VE1_DecStartDecode(void *pCtx)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;
	unsigned long valid_data = 0;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return -1;
	}

	if (ctx->decParam == NULL) {
		ctx->decParam = kzalloc(sizeof(DecParam), GFP_KERNEL);
		if (ctx->decParam == NULL) {
			ve1_err(VE1_WRAPPER_TAG, "kzalloc decParam fail\n");
			return -1;
		}
	}

	if (ctx->seqInited &&
		(ctx->ve1DecState == VE1_STATE_DEC_SET_DPB ||
		ctx->ve1DecState == VE1_STATE_DEC_PIC_DONE)) {
		VE1_DecGetRdWrPtr(ctx);
		if ((((DecOpenParam *)ctx->decOP)->bitstreamMode ==
		     BS_MODE_INTERRUPT) &&
		    (!ctx->streamEnd)) {
			valid_data = ve1_ring_valid_data(
				ctx->bitstream.paddr,
				ctx->bitstream.paddr + ctx->bitstream.size,
				ctx->vpuRdPtr, ctx->vpuWrPtr);
			if (valid_data < 1024) {
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"valid data size:%ld < 1024.vpuRdPtr:0x%x.vpuWrPtr:0x%x\n",
					valid_data, ctx->vpuRdPtr,
					ctx->vpuWrPtr);
				ctx->bBufEmptyFlag = true;
				return ret;
			}
		} else if (((DecOpenParam *)ctx->decOP)->bitstreamMode ==
			   BS_MODE_PIC_END) {
			if ((ctx->vpuRdPtr == ctx->vpuWrPtr) &&
			    (!ctx->streamEnd)) {
				ctx->bBufEmptyFlag = true;
				return ret;
			}
		}

		rtkve1_recycle_dpb(ctx);
		ret = VPU_DecStartOneFrame((DecHandle)ctx->decHandle,
					   (DecParam *)ctx->decParam);
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"af VPU_DecStartOneFrame.ret:%d\n", ret);
		if (ret != RETCODE_SUCCESS) {
			ve1_err(VE1_WRAPPER_TAG,
				"VPU_DecStartOneFrame fail.ret:%d\n", ret);
			return -1;
		}
		ctx->ve1DecState = VE1_STATE_DEC_START_DEC_ISSUED;
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "set ve1DecState:%d\n",
			ctx->ve1DecState);
	}

	return ret;
}

int VE1_DecWaitPicDone(void *pCtx)
{
	struct ve1_ctx *ctx;
	int bPicDone = 0;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;
	ctx->timeoutCount = 0;

	while (1) {
		ctx->int_reason = VPU_WaitInterrupt(VE1_COREIDX, 10);
		if (ctx->int_reason == -1) {
			ctx->int_reason = 0;
			ctx->timeoutCount++;
			if (ctx->timeoutCount > 10000) {
				ve1_err(VE1_WRAPPER_TAG,
					"wait interrupt timeoutCount:%d > 10000\n",
					ctx->timeoutCount);
				VPU_DecUpdateBitstreamBuffer(
					(DecHandle)ctx->decHandle,
					STREAM_END_SIZE);
				VPU_SWReset(VE1_COREIDX, SW_RESET_FORCE,
					    (DecHandle)ctx->decHandle);
				VPU_DecUpdateBitstreamBuffer(
					(DecHandle)ctx->decHandle,
					STREAM_END_CLEAR_FLAG);
				bPicDone = -1;
				break;
			}
		}
		if (ctx->int_reason) {
			if (ctx->bGotNextField) {
				ctx->bGotNextField = false;
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"bWaitNextField.clear bGotNextField.int_reason:0x%x\n",
					ctx->int_reason);
			}
			ctx->timeoutCount = 0;
			if (ctx->int_reason & (1 << INT_BIT_PIC_RUN)) {
				VPU_ClearInterrupt(VE1_COREIDX);
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"INT_BIT_PIC_RUN(0x%x)\n",
					ctx->int_reason);
				ctx->int_reason = 0;
				bPicDone = 1;
			} else if (ctx->int_reason &
				   (1 << INT_BIT_BIT_BUF_EMPTY)) {
				VPU_ClearInterrupt(VE1_COREIDX);
				VE1_DecGetRdWrPtr(ctx);
				ctx->bufEmptyVpuWrPtr = ctx->vpuWrPtr;
				ctx->bBufEmptyFlag = true;
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"INT_BIT_BIT_BUF_EMPTYY(0x%x).bufEmptyVpuWrPtr:0x%x\n",
					ctx->int_reason, ctx->bufEmptyVpuWrPtr);
			} else if (ctx->int_reason & (1 << INT_BIT_DEC_FIELD)) {
				if (ctx->bPostponeUpBs) {
					ctx->bPostponeUpBs = false;
					ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
						"bPostponeUpBs.clear bPostponeUpBs.int_reason:0x%x\n",
						ctx->int_reason);
				}
				VE1_DecGetRdWrPtr(ctx);
				ctx->bWaitNextField = true;
				ctx->bGotNextField = false;
				ctx->fldDoneVpuRp = ctx->vpuRdPtr;
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"INT_BIT_DEC_FIELD(0x%x).bWaitNextField.frmNum:%u.fld_rp:0x%x.vpu(0x%x,0x%x)\n",
					ctx->int_reason, ctx->decodedFrmNum,
					ctx->fldDoneVpuRp, ctx->vpuRdPtr,
					ctx->vpuWrPtr);
			}
			break;
		}
	}

	return bPicDone;
}

void *rtkve1_find_dpb(void *pCtx, unsigned long dpb_paddr, unsigned int seqNo)
{
	int i = 0;
	struct ve1_ctx *ctx;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return NULL;
	}
	ctx = (struct ve1_ctx *)pCtx;

	mutex_lock(&ctx->ve1_dma_mutex);
	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		if ((ctx->dpb[i].seqNo == seqNo) &&
		    (ctx->dpb[i].phys_addr == dpb_paddr)) {
			mutex_unlock(&ctx->ve1_dma_mutex);
			return (void *)&(ctx->dpb[i]);
		}
	}

	mutex_unlock(&ctx->ve1_dma_mutex);
	return NULL;
}

int VE1_UpdateDPBStatus(void *pCtx, unsigned int dpb_paddr,
			 unsigned int sequenceNo, unsigned int status)
{
	int ret = 0;
	struct ve1_ctx *ctx;
#if defined(VE1_ALLOC_FRAME_BUFFER_BY_VDI)
	vpu_buffer_t vdb;
#endif
	void *tmp_dpb = NULL;
	struct rtkve1_dpb_t *dpb = NULL;
	unsigned int regIndex = 0;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL || ctx->decOP == NULL) {
		ve1_err(VE1_WRAPPER_TAG,
			"ctx->decHandle == NULL || ctx->decOP == NULL\n");
		return -1;
	}

	tmp_dpb = rtkve1_find_dpb(pCtx, dpb_paddr, sequenceNo);
	if (!tmp_dpb) {
		//ve1_err(VE1_WRAPPER_TAG, "can't find dpb_paddr:0x%x in dpb[]\n",
		//	dpb_paddr);
		return -1;
	}
	dpb = (struct rtkve1_dpb_t *)tmp_dpb;
	regIndex = dpb->regIndex;

	mutex_lock(&ctx->ve1_dma_mutex);
	// recycle the frame buffer
	if (IS_RTKVE1_DPB_VALID(status)) {
		dpb->status |= RTKVE1_DPB_ST_VALID;
		dpb->status &= ~RTKVE1_DPB_ST_DQ;
		// recycle the frame buffer of previous sequence
		if (sequenceNo < ctx->currSequenceNo) {
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"dpb_paddr:0x%x.vb2_v4l2_buf:0x%px.recycle prev seq dpb.status:0x%x.seq(%d,%d).regIndex:%d\n",
				dpb_paddr, dpb->vb2_v4l2_buf, dpb->status,
				sequenceNo, ctx->currSequenceNo, regIndex);
		}
		// recycle the frame buffer of current sequence
		else if (sequenceNo == ctx->currSequenceNo) {
			if (ctx->seqChangeDone) {
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"dpb_paddr:0x%x.vb2_v4l2_buf:0x%px.recycle prev seq dpb.status:0x%x.seq(%d,%d).regIndex:%d\n",
					dpb_paddr, dpb->vb2_v4l2_buf, dpb->status,
					sequenceNo, ctx->currSequenceNo, regIndex);
			} else {
				// normal case, call VPU_DecClrDispFlag() to recyle the frame buffer
				dpb->status |= RTKVE1_DPB_ST_WAIT_RECYCLE;
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"dpb_paddr:0x%x.vb2_v4l2_buf:0x%px.set status:0x%x.seq(%d,%d).regIndex:%d\n",
					dpb_paddr, dpb->vb2_v4l2_buf, dpb->status,
					sequenceNo, ctx->currSequenceNo, regIndex);
			}
		} else {
			ve1_err(VE1_WRAPPER_TAG,
				"invalid sequenceNo:%d.currSequenceNo:%d\n",
				sequenceNo, ctx->currSequenceNo);
		}
	}
	// cap_dqbuf the frame buffer
	else if (IS_RTKVE1_DPB_DQ(status)) {
		if (sequenceNo <= ctx->currSequenceNo) {
			dpb->status |= RTKVE1_DPB_ST_DQ;
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"dpb_paddr:0x%x.set status:0x%x.seq(%d,%d).regIndex:%d\n",
				dpb_paddr, dpb->status, sequenceNo,
				ctx->currSequenceNo, regIndex);
		} else {
			ve1_err(VE1_WRAPPER_TAG,
				"invalid sequenceNo:%d.currSequenceNo:%d\n",
				sequenceNo, ctx->currSequenceNo);
		}
	} else {
		ve1_err(VE1_WRAPPER_TAG, "invalid status:0x%x\n", status);
	}

	mutex_unlock(&ctx->ve1_dma_mutex);
	return ret;
}

void ve1_seq_change_free_fb(struct ve1_ctx *ctx)
{
	FrameBuffer *fbUser;
#if defined(VE1_ALLOC_FRAME_BUFFER_BY_VDI)
	vpu_buffer_t vdb;
#endif
	int i;
	unsigned long flags;
	struct ve1_displayable_frame *frame;

	if (ctx == NULL || ctx->decHandle == NULL || ctx->decOP == NULL ||
	    ctx->fbUser == NULL) {
		ve1_err(VE1_WRAPPER_TAG,
			"ctx == NULL || ctx->decHandle == NULL || ctx->decOP == NULL || ctx->fbUser == NULL\n");
		return;
	}

	if (((DecOpenParam *)ctx->decOP)->wtlEnable) {
	} else {
		// print displayable_frame_list for debug
		ve1_show_displayable_frame_list(ctx);

		// free DPBs which not use for display (not in displayable_frame_list)
		for (i = 0; i < ctx->regFbCount; i++) {
			bool bFound = false;
			fbUser = ((FrameBuffer *)ctx->fbUser) + i;
			spin_lock_irqsave(&ctx->displayable_frame_lock, flags);
			if (!list_empty(&ctx->displayable_frame_list)) {
				list_for_each_entry (
					frame, &ctx->displayable_frame_list,
					list) {
					if (frame->dpb_paddr == fbUser->bufY) {
						bFound = true;
						break;
					}
				}
			}
			spin_unlock_irqrestore(&ctx->displayable_frame_lock,
					       flags);
			if (!bFound) {
				// this DPB is not in displayable_frame_list, it can be freed
				ve1_info(
					VE1_WRAPPER_TAG,
					"free fbUser[%d].bufY:0x%x.size:%d.myIndex:%d.stride:%d.h:%d.seqNo:%d.tot:%d\n",
					i, fbUser->bufY, fbUser->size,
					fbUser->myIndex, fbUser->stride,
					fbUser->height, ctx->currSequenceNo,
					ctx->totIonAllocatedBytes);
				memset(fbUser, 0, sizeof(FrameBuffer));
			}
		}
	}
}

int ve1_prepare_seq_change(void *pCtx)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;
	DecOutputInfo *outputInfo;
	PhysicalAddress seqChangedRdPtr;
	PhysicalAddress seqChangedWrPtr;
	int seqChangedStreamEndFlag;
	int bPicDone = 0;
	int i = 0;

	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "[+]\n");

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return VE1_DEC_RETURN_INVALID;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return VE1_DEC_RETURN_INVALID;
	}

	if (ctx->outputInfo == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->outputInfo == NULL\n");
		return VE1_DEC_RETURN_INVALID;
	}
	outputInfo = (DecOutputInfo *)ctx->outputInfo;

	ctx->seqChangeRequest = 1;
	seqChangedRdPtr = outputInfo->bytePosFrameEnd;
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"use bytePosFrameEnd:0x%x instead of rdPtr:0x%x\n",
		outputInfo->bytePosFrameEnd, outputInfo->rdPtr);
	seqChangedWrPtr = outputInfo->wrPtr;
	seqChangedStreamEndFlag = outputInfo->streamEndFlag;
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"seqChangedRdPtr:0x%x.seqChangedWrPtr:0x%x.seqChangedStreamEndFlag:0x%x\n",
		seqChangedRdPtr, seqChangedWrPtr, seqChangedStreamEndFlag);
	ret = VPU_DecSetRdPtr((DecHandle)ctx->decHandle, seqChangedRdPtr, 1);
	VE1_DecGetRdWrPtr(pCtx);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG, "VPU_DecSetRdPtr fail.ret:%d\n", ret);
		return VE1_DEC_RETURN_INVALID;
	}
	ret = VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle, 1);
	ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
		"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:1.accuBsFeedBytes:%d\n",
		ret, ctx->accuBsFeedBytes);
	VE1_DecGetRdWrPtr(pCtx);
	ret = VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
					   STREAM_END_SET_FLAG);
	ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
		"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d.accuBsFeedBytes:%d\n",
		ret, STREAM_END_SET_FLAG, ctx->accuBsFeedBytes);
	VE1_DecGetRdWrPtr(pCtx);

	while (outputInfo->indexFrameDisplay != -1) {
		if (ctx->seqInited &&
		    (ctx->ve1DecState == VE1_STATE_DEC_SET_DPB ||
		     ctx->ve1DecState == VE1_STATE_DEC_PIC_DONE)) {
			rtkve1_recycle_dpb(ctx);
			ret = VPU_DecStartOneFrame((DecHandle)ctx->decHandle,
						   (DecParam *)ctx->decParam);
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"af VPU_DecStartOneFrame.ret:%d\n", ret);
			if (ret != RETCODE_SUCCESS) {
				ve1_err(VE1_WRAPPER_TAG,
					"VPU_DecStartOneFrame fail.ret:%d\n",
					ret);
				return VE1_DEC_RETURN_INVALID;
			}
			ctx->ve1DecState = VE1_STATE_DEC_START_DEC_ISSUED;
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"set ve1DecState:%d\n", ctx->ve1DecState);

			while (1) {
				ctx->int_reason =
					VPU_WaitInterrupt(VE1_COREIDX, 10);
				if (ctx->int_reason == -1) {
					ctx->int_reason = 0;
				}
				if (ctx->int_reason) {
					VPU_ClearInterrupt(VE1_COREIDX);
					if (ctx->int_reason &
					    (1 << INT_BIT_PIC_RUN)) {
						ve1_dbg(VPU_DBG_NONE,
							VE1_WRAPPER_TAG,
							"INT_BIT_PIC_RUN\n");
						ctx->int_reason = 0;
						bPicDone = 1;
					} else if (ctx->int_reason &
						   (1
						    << INT_BIT_BIT_BUF_EMPTY)) {
						VE1_DecGetRdWrPtr(ctx);
						ctx->bufEmptyVpuWrPtr =
							ctx->vpuWrPtr;
						ve1_dbg(VPU_DBG_NONE,
							VE1_WRAPPER_TAG,
							"INT_BIT_BIT_BUF_EMPTY.bufEmptyVpuWrPtr:0x%x\n",
							ctx->bufEmptyVpuWrPtr);
					}
					break;
				}
			}

			if (bPicDone) {
				ret = VPU_DecGetOutputInfo(
					(DecHandle)ctx->decHandle, outputInfo);
				if (ret != RETCODE_SUCCESS) {
					ve1_err(VE1_WRAPPER_TAG,
						"VPU_DecGetOutputInfo fail.ret:%d\n",
						ret);
					return VE1_DEC_RETURN_INVALID;
				}
				ctx->lastIndexFrameDecoded =
					outputInfo->indexFrameDecoded;
				ctx->lastIndexFrameDisplay =
					outputInfo->indexFrameDisplay;
				ctx->lastDisplayFrmBufY =
					outputInfo->dispFrame.bufY;
				ctx->ve1DecState = VE1_STATE_DEC_PIC_DONE;
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"set ve1DecState:%d\n",
					ctx->ve1DecState);

				ctx->outputinfoSN++;

				if (outputInfo->indexFrameDecoded >= 0) {
					ctx->decodedFrmNum++;
				}
				if (outputInfo->indexFrameDisplay >= 0) {
					ctx->displayFrmNum++;
				}

				ve1_dbg(VPU_DBG_VE1_DEC, VE1_WRAPPER_TAG,
					"%d.%d.%d.h:0x%px.seq:%d.dec:%d.dis:%d.POC:%d(%d,%d).type:%d(%d).pos(0x%x 0x%x 0x%x).size:%ld.suc:0x%x.err:%d.frmDisFlg:0x%x.warn:%d.nalRefIdc:%d.decFrameInfo:%d.\n",
					ctx->outputinfoSN, ctx->decodedFrmNum,
					ctx->displayFrmNum,
					ctx->decHandle, ctx->currSequenceNo,
					outputInfo->indexFrameDecoded,
					outputInfo->indexFrameDisplay,
					outputInfo->avcPocPic,
					outputInfo->avcPocTop,
					outputInfo->avcPocBot,
					outputInfo->picType,
					outputInfo->picTypeFirst,
					outputInfo->bytePosFrameStart,
					outputInfo->bytePosFrameEnd,
					outputInfo->rdPtr,
					ve1_ring_valid_data(
						ctx->bitstream.paddr,
						ctx->bitstream.paddr +
							ctx->bitstream.size,
						outputInfo->bytePosFrameStart,
						outputInfo->bytePosFrameEnd),
					outputInfo->decodingSuccess,
					outputInfo->numOfErrMBs,
					outputInfo->frameDisplayFlag,
					outputInfo->warnInfo,
					outputInfo->nalRefIdc,
					outputInfo->decFrameInfo);

				VE1_UpdateFrameQueueInfo(pCtx);
				// update vpuRdPtr to bsRdPtr
				VE1_DecGetRdWrPtr(pCtx);
				ctx->bsRdPtr = ctx->vpuRdPtr;

				rtkve1_add_displayble_frame_to_list(ctx);
			}
		} else {
			ve1_err(VE1_WRAPPER_TAG,
				"fail to continue decoding original sequence.ve1DecState:%d\n",
				ctx->ve1DecState);
		}
	}

	for (i = 0; i < ctx->regFbCount; i++) {
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"VPU_DecClrDispFlag(%d)\n", i);
		ret = VPU_DecClrDispFlag((DecHandle)ctx->decHandle, i);
	}

	ret = VPU_DecFrameBufferFlush((DecHandle)ctx->decHandle, NULL, NULL);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG,
			"VPU_DecFrameBufferFlush fail.ret:%d\n", ret);
		return VE1_DEC_RETURN_INVALID;
	}

	ctx->seqChangeRequest = 0;
	ctx->outputinfoSN = 0;
	ctx->decodedFrmNum = 0;
	ctx->displayFrmNum = 0;
	ctx->seqHeaderSize = 0;

	VPU_DecSetRdPtr((DecHandle)ctx->decHandle, seqChangedRdPtr, 1);
	VE1_DecGetRdWrPtr(pCtx);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG, "VPU_DecSetRdPtr fail.ret:%d\n", ret);
		return VE1_DEC_RETURN_INVALID;
	}

	if (seqChangedStreamEndFlag == 1) {
		ret = VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
						   STREAM_END_SET_FLAG);
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d.accuBsFeedBytes:%d\n",
			ret, STREAM_END_SET_FLAG, ctx->accuBsFeedBytes);
	} else {
		ret = VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
						   STREAM_END_CLEAR_FLAG);
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d.accuBsFeedBytes:%d\n",
			ret, STREAM_END_CLEAR_FLAG, ctx->accuBsFeedBytes);
	}
	VE1_DecGetRdWrPtr(pCtx);

	if (seqChangedWrPtr >= seqChangedRdPtr) {
		ret = VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
						   seqChangedWrPtr -
							   seqChangedRdPtr);
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d.accuBsFeedBytes:%d\n",
			ret, (seqChangedWrPtr - seqChangedRdPtr),
			ctx->accuBsFeedBytes);
	} else {
		ret = VPU_DecUpdateBitstreamBuffer(
			(DecHandle)ctx->decHandle,
			(((DecOpenParam *)ctx->decOP)->bitstreamBuffer +
			 ((DecOpenParam *)ctx->decOP)->bitstreamBufferSize) -
				seqChangedRdPtr +
				(seqChangedWrPtr -
				 ((DecOpenParam *)ctx->decOP)->bitstreamBuffer));
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d.accuBsFeedBytes:%d\n",
			ret,
			((((DecOpenParam *)ctx->decOP)->bitstreamBuffer +
			  ((DecOpenParam *)ctx->decOP)->bitstreamBufferSize) -
			 seqChangedRdPtr +
			 (seqChangedWrPtr -
			  ((DecOpenParam *)ctx->decOP)->bitstreamBuffer)),
			ctx->accuBsFeedBytes);
	}
	VE1_DecGetRdWrPtr(pCtx);

	VPU_DecGiveCommand((DecHandle)ctx->decHandle, DEC_FREE_FRAME_BUFFER,
			   0x00);

	//ve1_seq_change_free_fb(ctx);

	//memset(ctx->fbUser, 0, sizeof(FrameBuffer) * MAX_REG_FRAME);

	ctx->cntCap2Dpb = 0;
	ctx->capReqBufsCnt = 0;
	ctx->regFbCount = 0;
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

	mutex_lock(&ctx->ve1_dma_mutex);
	rtkve1_show_dpbs(ctx);
	for (i = 0; i < VE1_ION_STRUCT_NUM; i++) {
		if (ctx->dpb[i].seqNo == ctx->currSequenceNo)
		{
			if (!IS_RTKVE1_DPB_DQ(ctx->dpb[i].status) &&
				IS_RTKVE1_DPB_VALID(ctx->dpb[i].status)) {
				if ((ctx->lastDoneCapBuf == NULL) ||
					((ctx->lastDoneCapBuf != NULL) && (ctx->dpb[i].vb2_v4l2_buf != ctx->lastDoneCapBuf))) {
					ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
						"v4l2_m2m_buf_done.status:0x%x.regIndex:%d.vb2_v4l2_buf:0x%px.phys_addr:0x%lx.seqNo:%u\n",
						ctx->dpb[i].status,
						ctx->dpb[i].regIndex,
						ctx->dpb[i].vb2_v4l2_buf,
						ctx->dpb[i].phys_addr,
						ctx->dpb[i].seqNo);
					v4l2_m2m_buf_done((struct vb2_v4l2_buffer
						*)(ctx->dpb[i].vb2_v4l2_buf),
						VB2_BUF_STATE_ERROR);
				}
			}
			memset(&ctx->dpb[i], 0, sizeof(struct rtkve1_dpb_t));
		}
	}
	mutex_unlock(&ctx->ve1_dma_mutex);

	ctx->seqInited = 0;
	// for trigger queue_work pic_run_work in ve1_out_qbuf()
	ctx->startDecode = 0;
	ctx->seqChangeDone = 1;

	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "[-]\n");

	return VE1_DEC_RETURN_SEQ_CHANGE;
}

static int rtkve1_dump_yuv(void *pCtx, int fbIndex)
{
#if defined(RTKVE1_DUMP_YUV_EN)
	struct ve1_ctx *ctx;
	unsigned long fb_phys_addr = 0;
	void *fb_virt_addr = NULL;
	FrameBuffer *fbUser = NULL;
	void *tmp_dpb = NULL;
	struct rtkve1_dpb_t *dpb = NULL;
	struct vb2_v4l2_buffer *vb2_v4l2_buf = NULL;
	struct vb2_buffer *vb2_buf = NULL;
	int filp_open_flags;
	ssize_t bytes = 0;
	loff_t pos = 0;

	if ((pCtx == NULL) || (fbIndex < 0)) {
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	//if ((fbIndex >= 0) &&
	//    (ctx->displayFrmNum == 300)) {
	if (fbIndex >= 0) {
		fbUser = ((FrameBuffer *)ctx->fbUser) + fbIndex;
		fb_phys_addr = fbUser->bufY;
		tmp_dpb = rtkve1_find_dpb((void *)ctx, fb_phys_addr, ctx->currSequenceNo);
		if (!tmp_dpb) {
			ve1_err(VE1_WRAPPER_TAG,
					"can't find framePhysAddr:0x%lx in dpb[]\n",
					fb_phys_addr);
			return -1;
		}
		dpb = (struct rtkve1_dpb_t *)tmp_dpb;
		vb2_v4l2_buf = (struct vb2_v4l2_buffer *)(dpb->vb2_v4l2_buf);
		vb2_buf = &(vb2_v4l2_buf->vb2_buf);
		fb_virt_addr = vb2_plane_vaddr(vb2_buf, 0);
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"dpb.index:%d.virt:0x%px.phys:0x%lx.vb2_v4l2_buf:0x%px.vb2_buf:0x%px.size:%u\n",
				fbIndex, fb_virt_addr, fb_phys_addr,
				(void *)vb2_v4l2_buf, (void *)vb2_buf, dpb->size);

		if ((fb_virt_addr != NULL) && (dpb->size > 0)) {
			if (ctx->bNewYuvDumpFile == 1) {
				ctx->bNewYuvDumpFile = 0;
				filp_open_flags = O_CREAT | O_WRONLY;
				memset(ctx->yuvDumpFileName, 0, sizeof(unsigned char)*256);
				snprintf(ctx->yuvDumpFileName, 256,
						"/mnt/ve1yuv_%d.yuv",
						gYuvDumpSerial);
				gYuvDumpSerial++;
				vpu_info("%d.%s.create new ve1yuv dump:%s\n",__LINE__,__func__,
						ctx->yuvDumpFileName);
			} else {
				filp_open_flags = O_APPEND | O_WRONLY;
			}
			ctx->yuvDumpFile =
				(void *)filp_open(ctx->yuvDumpFileName, filp_open_flags, 0);
			if (IS_ERR((struct file *)ctx->bsDumpFile)) {
				ve1_err(VE1_WRAPPER_TAG, "filp_open %s fail\n",
						ctx->yuvDumpFileName);
			} else {
				//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				//	"filp_open %s ok\n",
				//	ctx->yuvDumpFileName);
				bytes =
					kernel_write((struct file *)(ctx->yuvDumpFile),
								(void *)fb_virt_addr, (size_t)dpb->size, &pos);
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
						"kernel_write bytes:%ld.pos:%lld\n", bytes, pos);
				filp_close((struct file *)(ctx->yuvDumpFile), NULL);
				//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				//		"filp_close %s\n",
				//		ctx->yuvDumpFileName);
				ctx->yuvDumpFile = NULL;
			}
		}
	}
	return 0;
#else
	return -1;
#endif
}

int VE1_DecPicDone(void *pCtx)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;
	DecOutputInfo *outputInfo;
#if defined(VE1_CHECK_DFB_MD5_EN)
	int fbIndex = 0;
	unsigned long fb_phys_addr = 0;
	void *fb_virt_addr = NULL;
	FrameBuffer *fbUser = NULL;
	void *tmp_dpb = NULL;
	struct rtkve1_dpb_t *dpb = NULL;
	struct vb2_v4l2_buffer *vb2_v4l2_buf = NULL;
	struct vb2_buffer *vb2_buf = NULL;
#endif

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return VE1_DEC_RETURN_INVALID;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return VE1_DEC_RETURN_INVALID;
	}

	if (ctx->outputInfo == NULL) {
		ctx->outputInfo = kzalloc(sizeof(DecOutputInfo), GFP_KERNEL);
		if (ctx->outputInfo == NULL) {
			ve1_err(VE1_WRAPPER_TAG, "kzalloc outputInfo fail\n");
			return VE1_DEC_RETURN_INVALID;
		}
	}
	outputInfo = (DecOutputInfo *)ctx->outputInfo;

	ret = VPU_DecGetOutputInfo((DecHandle)ctx->decHandle, outputInfo);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG, "VPU_DecGetOutputInfo fail.ret:%d\n",
			ret);
		return VE1_DEC_RETURN_INVALID;
	}

	if (outputInfo->indexFrameDecoded != -1) {
		if (ctx->bPostponeUpBs) {
			ctx->bPostponeUpBs = false;
			ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
				"bPostponeUpBs.clear bPostponeUpBs.int_reason:0x%x\n",
				ctx->int_reason);
		}
	}

	if (outputInfo->indexFrameDecoded >= 0) {
		ctx->decodedFrmNum++;
	}
	if (outputInfo->indexFrameDisplay >= 0) {
		ctx->displayFrmNum++;
	}

	if (!((ctx->lastInfoFrmStart == outputInfo->bytePosFrameStart) &&
	      (ctx->lastInfoFrmEnd == outputInfo->bytePosFrameEnd) &&
	      (outputInfo->indexFrameDecoded == -2) &&
	      (outputInfo->indexFrameDisplay == -3) &&
		  (ctx->lastIndexFrameDecoded != -1))) {
		ctx->outputinfoSN++;
	ve1_dbg(VPU_DBG_VE1_DEC, VE1_WRAPPER_TAG,
		"%d.%d.%d.h:0x%px.seq:%d.dec:%d.dis:%d.POC:%d(%d,%d).type:%d(%d).pos(0x%x 0x%x 0x%x).size:%ld.suc:0x%x.err:%d.frmDisFlg:0x%x.warn:%d.nalRefIdc:%d.decFrameInfo:%d.vpu_debug:0x%x\n",
		ctx->outputinfoSN, ctx->decodedFrmNum,
		ctx->displayFrmNum, ctx->decHandle,
		ctx->currSequenceNo, outputInfo->indexFrameDecoded,
		outputInfo->indexFrameDisplay, outputInfo->avcPocPic,
		outputInfo->avcPocTop, outputInfo->avcPocBot,
		outputInfo->picType, outputInfo->picTypeFirst,
		outputInfo->bytePosFrameStart,
		outputInfo->bytePosFrameEnd, outputInfo->rdPtr,
		ve1_ring_valid_data(ctx->bitstream.paddr,
			ctx->bitstream.paddr +
			ctx->bitstream.size,
			outputInfo->bytePosFrameStart,
			outputInfo->bytePosFrameEnd),
		outputInfo->decodingSuccess, outputInfo->numOfErrMBs,
		outputInfo->frameDisplayFlag, outputInfo->warnInfo,
		outputInfo->nalRefIdc, outputInfo->decFrameInfo,
		vpu_debug);
	}
	ctx->lastInfoFrmStart = outputInfo->bytePosFrameStart;
	ctx->lastInfoFrmEnd = outputInfo->bytePosFrameEnd;

	ctx->lastIndexFrameDecoded = outputInfo->indexFrameDecoded;
	ctx->lastIndexFrameDisplay = outputInfo->indexFrameDisplay;
	ctx->lastDisplayFrmBufY = outputInfo->dispFrame.bufY;
	ctx->ve1DecState = VE1_STATE_DEC_PIC_DONE;
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "set ve1DecState:%d\n",
		ctx->ve1DecState);

	if ((((DecOpenParam *)ctx->decOP)->bitstreamFormat == STD_AVC) &&
	    (outputInfo->indexFrameDecoded >= 0)) {

		if ((outputInfo->decodingSuccess & 0x00200000) &&
		    (outputInfo->nalRefIdc != 0)) {
			ve1_dbg(VPU_DBG_VE1_DEC, VE1_WRAPPER_TAG,
				"dec:%d.AVC missing reference\n",
				outputInfo->indexFrameDecoded);
		}
		if (outputInfo->decFrameInfo != 0) {
			ve1_dbg(VPU_DBG_VE1_DEC, VE1_WRAPPER_TAG,
				"dec:%d.AVC missing field\n",
				outputInfo->indexFrameDecoded);
		}
	}
	if (outputInfo->numOfErrMBs) {
		bool isThisFrmBeRefer = true;

		if ((((DecOpenParam *)ctx->decOP)->bitstreamFormat ==
		     STD_AVC) &&
		    (outputInfo->nalRefIdc == 0)) {
			isThisFrmBeRefer = false;
		}
		ve1_dbg(VPU_DBG_VE1_DEC, VE1_WRAPPER_TAG,
			"dec:%d.ErrorBlock:%d.ifRefFrame:%d\n",
			outputInfo->indexFrameDecoded, outputInfo->numOfErrMBs,
			isThisFrmBeRefer);
	}

	if (((DecOpenParam *)ctx->decOP)->bitstreamMode == BS_MODE_ROLLBACK &&
	    (outputInfo->decodingSuccess & 0x10)) {
		ve1_dbg(VPU_DBG_VE1_DEC, VE1_WRAPPER_TAG,
			"BS_MODE_ROLLBACK.empty.dec:%d.dis:%d.suc:0x%x\n",
			outputInfo->indexFrameDecoded,
			outputInfo->indexFrameDisplay,
			outputInfo->decodingSuccess);
	}

	VE1_UpdateFrameQueueInfo(pCtx);
	// update vpuRdPtr to bsRdPtr
	VE1_DecGetRdWrPtr(pCtx);
	ctx->bsRdPtr = ctx->vpuRdPtr;

#if defined(VE1_CHECK_DFB_MD5_EN)
	fbIndex = outputInfo->indexFrameDecoded;
	if (fbIndex >= 0) {
		fbUser = ((FrameBuffer *)ctx->fbUser) + fbIndex;
		fb_phys_addr = fbUser->bufY;
		tmp_dpb = rtkve1_find_dpb((void *)ctx, fb_phys_addr, ctx->currSequenceNo);
		if (!tmp_dpb) {
			ve1_err(VE1_WRAPPER_TAG,
				"can't find framePhysAddr:0x%lx in dpb[]\n",
				fb_phys_addr);
			return VE1_DEC_RETURN_INVALID;
		}
		dpb = (struct rtkve1_dpb_t *)tmp_dpb;
		vb2_v4l2_buf = (struct vb2_v4l2_buffer *)(dpb->vb2_v4l2_buf);
		vb2_buf = &(vb2_v4l2_buf->vb2_buf);
		fb_virt_addr = vb2_plane_vaddr(vb2_buf, 0);
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"dpb.index:%d.virt:0x%px.phys:0x%lx.vb2_v4l2_buf:0x%px.vb2_buf:0x%px.size:%u\n",
			fbIndex, fb_virt_addr, fb_phys_addr,
			(void *)vb2_v4l2_buf, (void *)vb2_buf, dpb->size);
		ve1_md5_hash(ve1_md5_digest, VE1_MD5_DIGEST_SIZE,
			(char *)fb_virt_addr, dpb->size);
	}
#endif

	rtkve1_dump_yuv((void *)ctx, outputInfo->indexFrameDisplay);

	if (((outputInfo->decodingSuccess & 0x1) != 0) &&
	    (outputInfo->sequenceChanged)) {
		//profileIdc/MbNumX/MbNumY/MaxDecFrameBuffering are changed
		ve1_info(VE1_WRAPPER_TAG,
			 "Sequence information has been changed (0x%x)\n",
			 outputInfo->sequenceChanged);
		ret = ve1_prepare_seq_change(pCtx);
		return ret;
	}

	if ((outputInfo->indexFrameDisplay == -1) && ctx->streamEnd) {
		ret = VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
											STREAM_END_CLEAR_FLAG);
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d\n",
			ret, STREAM_END_CLEAR_FLAG);
		VE1_DecGetRdWrPtr(ctx);
		ctx->streamEnd = 0;
		ctx->handle_eos_by = VE1_HANDLE_EOS_DEC_FINISH;
	}

	return VE1_DEC_RETURN_OK;
}

int VE1_DecCheckComplete(void *pCtx)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;
	int int_reason;
	unsigned int interrupt_timeout_cnt = 10;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return -1;
	}

	if (ctx->ve1DecState == VE1_STATE_DEC_SEQ_INIT_ISSUED ||
		ctx->ve1DecState == VE1_STATE_DEC_START_DEC_ISSUED) {
		ret = VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
						   STREAM_END_SET_FLAG);
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d.accuBsFeedBytes:%d\n",
			ret, STREAM_END_SET_FLAG, ctx->accuBsFeedBytes);
		VE1_DecGetRdWrPtr(ctx);

		int_reason = 1 << INT_BIT_BIT_BUF_EMPTY;
		while ((int_reason & (1 << INT_BIT_BIT_BUF_EMPTY)) &&
		       (interrupt_timeout_cnt > 0)) {
			int_reason = VPU_WaitInterrupt(VE1_COREIDX, 100);
			if (int_reason) {
				VPU_ClearInterrupt(VE1_COREIDX);
			}
			interrupt_timeout_cnt--;
		}
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"int_reason:0x%x.interrupt_timeout_cnt:%d\n",
			int_reason, interrupt_timeout_cnt);
		ret = VPU_DecGetOutputInfo((DecHandle)ctx->decHandle,
					   (DecOutputInfo *)ctx->outputInfo);
		if (ret != RETCODE_SUCCESS) {
			ve1_err(VE1_WRAPPER_TAG,
				"VPU_DecGetOutputInfo fail.ret:%d\n", ret);
		}
		ret = VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
						   STREAM_END_CLEAR_FLAG);
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d.accuBsFeedBytes:%d\n",
			ret, STREAM_END_CLEAR_FLAG, ctx->accuBsFeedBytes);
		VE1_DecGetRdWrPtr(ctx);
		ctx->ve1DecState = VE1_STATE_DEC_PIC_DONE;
	}

	return ret;
}
//EXPORT_SYMBOL(VE1_DecCheckComplete);

int VE1_DecClose(void *pCtx)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;
	FrameBuffer *fbUser;
	int i;
	DecHandle decHandle = NULL;
	DecOpenParam *pDecOp;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decHandle == NULL\n");
		return -1;
	}
	decHandle = (DecHandle)ctx->decHandle;
	pDecOp = (DecOpenParam *)ctx->decOP;

	if (ctx->ve1DecState < VE1_STATE_DEC_OPENED) {
		ve1_err(VE1_WRAPPER_TAG, "invalid ve1DecState:%d\n",
			ctx->ve1DecState);
		return -1;
	}

	/* This software reset for corner case is neccessary when VPU_DecStartOneFrame()
	 * EnterLock and doesn't LeaveLock, during this moment the system stops streaming.
	 * The next start will be deadlock at EnterLock().
	 */
	if (ctx->ve1DecState != VE1_STATE_DEC_OPENED &&
	    ctx->ve1DecState != VE1_STATE_DEC_PIC_DONE) {
		ve1_info(VE1_WRAPPER_TAG,
			"%s: ve1DecState=%d, VPU_SWReset for corner case\n",
			__func__, ctx->ve1DecState);
		VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
			STREAM_END_SIZE);
		VPU_SWReset(VE1_COREIDX, SW_RESET_SAFETY,
			(DecHandle)ctx->decHandle);
		VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
			STREAM_END_CLEAR_FLAG);
		ve1_info(VE1_WRAPPER_TAG, "%s: VPU_SWReset done\n", __func__);
	}

#ifdef VPU_GET_CC
	if (pDecOp->bitstreamFormat == STD_MPEG2 ||
	    pDecOp->bitstreamFormat == STD_AVC) {
		cc_data_channel_exit();

		for (i = 0; i < MPEG2_CC_REG_FRAME_MAX; i++) {
			if (ctx->m_CCDecodeOrderWp[i]) {
				kfree(ctx->m_CCDecodeOrderWp[i]);
				ctx->m_CCDecodeOrderWp[i] = NULL;
			}
		}

		ctx->cc_error_count = 0;
	}
#endif
	// clear frame buffers
	for (i = 0; i < ctx->regFbCount * 2; i++) {
		fbUser = ((FrameBuffer *)ctx->fbUser) + i;
		if (fbUser->size > 0) {
			ve1_info(
				VE1_WRAPPER_TAG,
				"clear fbUser[%d].bufY:0x%x.size:%d.myIndex:%d.stride:%d.h:%d.seqNo:%d.tot:%d\n",
				i, fbUser->bufY, fbUser->size, fbUser->myIndex,
				fbUser->stride, fbUser->height,
				ctx->currSequenceNo, ctx->totIonAllocatedBytes);
			memset(fbUser, 0, sizeof(FrameBuffer));
		}
	}

#ifdef VPU_GET_CC
	if (ctx->pUserDataSrcBuf) {
		unsigned int codec_type =
			(pDecOp->bitstreamFormat == STD_MPEG2 ? ENUM_CC_MPGE2 :
								ENUM_CC_H264);
		if (ctx->is_svp)
			ta_TEEapi_OMX_CC_API(
				(struct tee_context *)decHandle->teeapi_ctx,
				decHandle->teeapi_tee_session,
				ctx->userDataBufPhysAddr, ctx->pUserDataSrcBuf,
				USER_DATA_SRC_BUF_SIZE, codec_type, ENUM_CC_U);

		kfree(ctx->pUserDataSrcBuf);
		ctx->pUserDataSrcBuf = NULL;
	}
	if (ctx->userDataBufPhysAddr != 0) {
		vpu_buffer_t vdb;
		memset(&vdb, 0, sizeof(vpu_buffer_t));
		vdb.size = ctx->userDataBufSize;
		vdb.phys_addr = ctx->userDataBufPhysAddr;
		vdi_free_dma_memory(VE1_COREIDX, &vdb);
		ve1_info(VE1_WRAPPER_TAG, "free userdata(size:%d,phys:0x%x)\n",
			 ctx->userDataBufSize, ctx->userDataBufPhysAddr);
		ctx->userDataBufSize = 0;
		ctx->userDataBufPhysAddr = 0;
	}

	// clear user data variables
	ctx->userDataEnable = 0;
	ctx->userDataReportMode = 0;
#endif

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "decHandle == NULL\n");
		return -1;
	}
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"bf VPU_DecClose.ve1DecState:%d\n", ctx->ve1DecState);
	ret = VPU_DecClose((DecHandle)ctx->decHandle);
	ve1_info(VE1_WRAPPER_TAG, "af VPU_DecClose.ret:%d\n", ret);
	ctx->decHandle = NULL;
	ctx->ve1DecState = VE1_STATE_DEC_CLOSED;
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "set ve1DecState:%d\n",
		ctx->ve1DecState);

	return ret;
}

int VE1_DecDeInit(void *pCtx)
{
	RetCode ret = RETCODE_SUCCESS;
	struct ve1_ctx *ctx;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->ve1DecState != VE1_STATE_DEC_INITED &&
	    ctx->ve1DecState != VE1_STATE_DEC_CLOSED) {
		ve1_err(VE1_WRAPPER_TAG, "invalid ve1DecState:%d\n",
			ctx->ve1DecState);
		return -1;
	}

	ret = VPU_DeInit(VE1_COREIDX);
	ve1_info(VE1_WRAPPER_TAG, "af VPU_DeInit.ret:%d\n", ret);
	ctx->ve1DecState = VE1_STATE_DEC_UNINIT;
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG, "set ve1DecState:%d\n",
		ctx->ve1DecState);

	return ret;
}

void VE1_GetParsedInfo(void *pCtx, void *pInfo)
{
	struct ve1_ctx *ctx;
	DecInitialInfo *initialInfo;
	struct ve1_parsed_initial_info *info;

	if (pCtx == NULL || pInfo == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL or pInfo == NULL\n");
		return;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (!ctx->seqInited) {
		ve1_err(VE1_WRAPPER_TAG, "not seqInited\n");
		return;
	}

	initialInfo = (DecInitialInfo *)ctx->initialInfo;
	info = (struct ve1_parsed_initial_info *)pInfo;

	info->pic_width = initialInfo->picWidth;
	info->pic_height = initialInfo->picHeight;
	info->visible_rect_left = initialInfo->picCropRect.left;
	info->visible_rect_top = initialInfo->picCropRect.top;
	info->visible_rect_w =
		initialInfo->picCropRect.right - initialInfo->picCropRect.left;
	info->visible_rect_h =
		initialInfo->picCropRect.bottom - initialInfo->picCropRect.top;
	info->minDpbCount =
		initialInfo->minFrameBufferCount + EXTRA_DEC_PIC_BUF;
}

void VE1_GetDisplayFrameInfo(void *pCtx, void *displayFrameInfo)
{
	struct ve1_ctx *ctx;
	DecOpenParam *decOP;
	DecInitialInfo *initialInfo;
	DecOutputInfo *outputInfo;
	struct ve1_displayable_frame *frame;
	struct ve1_decoded_frame *frmInfo;

	if (pCtx == NULL || displayFrameInfo == NULL) {
		ve1_err(VE1_WRAPPER_TAG,
			"pCtx == NULL or displayFrameInfo == NULL\n");
		return;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decOP == NULL || ctx->initialInfo == NULL ||
	    ctx->outputInfo == NULL) {
		ve1_err(VE1_WRAPPER_TAG,
			"decOP == NULL or initialInfo == NULL or outputInfo == NULL\n");
		return;
	}

	decOP = (DecOpenParam *)ctx->decOP;
	initialInfo = (DecInitialInfo *)ctx->initialInfo;
	outputInfo = (DecOutputInfo *)ctx->outputInfo;
	frame = (struct ve1_displayable_frame *)displayFrameInfo;
	frmInfo = (struct ve1_decoded_frame *)&ctx
			  ->frameQueue[outputInfo->indexFrameDisplay];

	frame->frameBufIndex = outputInfo->indexFrameDisplay;
	frame->Y_addr = outputInfo->dispFrame.bufY;
	frame->U_addr =
		(outputInfo->dispFrame.bufY +
		 outputInfo->dispFrame.stride * outputInfo->dispFrame.height);
	frame->bufStride = outputInfo->dispFrame.stride;
	frame->bufHeight = outputInfo->dispFrame.height;
	frame->picWidth = initialInfo->picWidth;
	frame->picHeight = initialInfo->picHeight;
	frame->rectLeft = outputInfo->rcDisplay.left;
	frame->rectTop = outputInfo->rcDisplay.top;
	frame->rectRight = outputInfo->rcDisplay.right;
	frame->rectBottom = outputInfo->rcDisplay.bottom;
	frame->bitDepth = initialInfo->lumaBitdepth;
	if (frmInfo->pairedFldFrm) {
		frame->mode = (frmInfo->picMode == INTERLEAVED_BOT_FIELD) ?
				      INTERLEAVED_BOT_TOP_FIELD :
				      INTERLEAVED_TOP_BOT_FIELD;
	} else {
		frame->mode = frmInfo->picMode;
	}
	if (decOP->nv21 == 1) {
		frame->mode |=
			0x1u << 16; // todo, VO_NV21_MASK of VP_PICTURE_MODE_EXT
	}
	frame->timeTick = ctx->timeTick;
	frame->video_full_range_flag = frmInfo->video_full_range_flag;
	frame->transfer_characteristics = frmInfo->transfer_characteristics;
	frame->matrix_coefficients = frmInfo->matrix_coefficients;
	frame->POC = frmInfo->POC;
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"idx:%d.0x%x,0x%x.%d,%d,%d,%d.(%d,%d,%d,%d).%d.%d.%lld.avcvui(%d,%d,%d).POC:%d\n",
		frame->frameBufIndex, frame->Y_addr, frame->U_addr,
		frame->bufStride, frame->bufHeight, frame->picWidth,
		frame->picHeight, frame->rectLeft, frame->rectTop,
		frame->rectRight, frame->rectBottom, frame->bitDepth,
		frame->mode, frame->timeTick, frame->video_full_range_flag,
		frame->transfer_characteristics, frame->matrix_coefficients, frame->POC);
}

int VE1_AllocateBitstreamBuffer(struct device *dev, unsigned long *phys_addr,
				unsigned long *virt_addr, unsigned long size,
				unsigned int is_svp)
{
	int ret = 0;
	unsigned int flags = 0;
	dma_addr_t dma_phys_addr = 0;
	void *dma_virt_addr = NULL;

	if (dev == NULL || phys_addr == NULL || virt_addr == NULL ||
	    size == 0) {
		ve1_err(VE1_WRAPPER_TAG,
			"dev == NULL or phys_addr == NULL or virt_addr == NULL or size == 0\n");
		return -1;
	}

	if (is_svp) {
		flags = RTK_FLAG_NONCACHED | RTK_FLAG_HWIPACC |
			RTK_FLAG_PROTECTED_V2_VIDEO_POOL;
	} else {
		flags = RTK_FLAG_NONCACHED | RTK_FLAG_HWIPACC |
			RTK_FLAG_SCPUACC;
	}

	/* We can't limit the address from dma_alloc_coherent when size <= 4096 */
	if (size < SZ_8K)
		size = SZ_8K;

	dma_virt_addr = dma_alloc_coherent(
		dev, PAGE_ALIGN(size), &dma_phys_addr, (GFP_DMA | GFP_KERNEL));
	if (dma_virt_addr == NULL) {
		ve1_err(VE1_WRAPPER_TAG,
			"dma_alloc_coherent() fail.size:%lu(%ld).flags:0x%x\n",
			PAGE_ALIGN(size), size, flags);
		ret = -ENOMEM;
		return ret;
	}
	*phys_addr = (unsigned long)dma_phys_addr;
	*virt_addr = (unsigned long)dma_virt_addr;
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"dma_alloc_coherent() ok.phys_addr:0x%lx.virt_addr:0x%lx.size:%lu(%ld).flags:0x%x\n",
		*phys_addr, *virt_addr, PAGE_ALIGN(size), size, flags);

	return ret;
}

int VE1_FreeBitstreamBuffer(struct device *dev, unsigned long virt_addr,
			    unsigned long phys_addr, unsigned int size)
{
	int ret = 0;

	if (dev == NULL || virt_addr == 0 || phys_addr == 0 || size == 0) {
		ve1_err(VE1_WRAPPER_TAG,
			"dev == NULL or virt_addr == 0 or phys_addr == 0 or size == 0\n");
		return -1;
	}

	if (virt_addr != 0) {
		dma_free_coherent(dev, PAGE_ALIGN(size), (void *)virt_addr,
				  (dma_addr_t)phys_addr);
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"dma_free_coherent() ok.phys_addr:0x%lx.virt_addr:0x%lx.size:%d(%d)\n",
			phys_addr, virt_addr, PAGE_ALIGN(size), size);
	}

	return ret;
}

static enum PICTURE_MODE ve1_get_picture_mode(struct ve1_ctx *ctx)
{
	DecOpenParam *decOP;
	DecOutputInfo *info;
	struct ve1_decoded_frame *frame;
	enum PICTURE_MODE mode = CONSECUTIVE_FRAME;

	decOP = (DecOpenParam *)ctx->decOP;
	info = (DecOutputInfo *)ctx->outputInfo;
	frame = (struct ve1_decoded_frame *)&ctx
			->frameQueue[info->indexFrameDecoded];

	if (decOP->bitstreamFormat == STD_MPEG2) {
		if (info->pictureStructure == 3) // 1:TOP, 2: BOT, 3:FRAME
		{
			char is_prog = 0;
			// LINUX-74, mark the condition of "info->repeatFirstField" below,
			// according to ISO/IEC 13818-2: 1995 (E), pg 63, the explanation of repeat_first_field,
			// if progressive_sequence is equal to 0, whether progressive_frame is equal to 0 to 1, this reconstructed frame consists fields:
			// repeat_first_field is 1 -> consists 3 fields, repeat_first_field is 0 -> consists 2 fields,
			// so this frame is not progressive.
			// ref 1185 MpegDec_SetupLinks()
			is_prog =
				(info->progressiveSequence /* progressive sequence */
				 ||
				 frame->stillVOBU); /* progressive I-picture in a DVD still-VOBU */

			if (is_prog)
				mode = CONSECUTIVE_FRAME;
			else
				mode = MPEG2_PIC_MODE_NOT_PROG;
		} else {
			if (info->pictureStructure == 1)
				mode = INTERLEAVED_TOP_FIELD;
			if (info->pictureStructure == 2)
				mode = INTERLEAVED_BOT_FIELD;
		}
	} else if (decOP->bitstreamFormat == STD_AVC) {
		if (info->pictureStructure) // MbaffFrameFlag = ( mb_adaptive_frame_field_flag && !field_pic_flag )
		{
			if (info->picStrPresent == 1) {
				if (info->picTimingStruct == 4)
					mode = INTERLEAVED_BOT_FIELD;
				else
					mode = INTERLEAVED_TOP_FIELD;
			} else // according to poc
			{
				if (info->avcPocBot < info->avcPocTop)
					mode = INTERLEAVED_BOT_FIELD;
				else
					mode = INTERLEAVED_TOP_FIELD;
			}
		} else {
			/**decFrameInfo:  H.264/AVC, MPEG-2, and VC-1
            @** 0 : The decoded frame has paired fields.
            @** 1 : The decoded frame has a top-field missing.
            @** 2 : The decoded frame has a bottom-field missing.*/
			if (info->decFrameInfo ==
			    0) //decoded frame is paired field or frame.
			{
				if (info->interlacedFrame) //field_pic_flag
				{
					if (info->topFieldFirst)
						mode = INTERLEAVED_TOP_FIELD;
					else
						mode = INTERLEAVED_BOT_FIELD;
				} else if (info->picStrPresent == 1) {
					if (info->picTimingStruct ==
						    H264_PIC_STRUCT_FRAME ||
					    info->picTimingStruct ==
						    H264_PIC_STRUCT_FRAME_DOUBLING ||
					    info->picTimingStruct ==
						    H264_PIC_STRUCT_FRAME_TRIPLING) {
						mode = CONSECUTIVE_FRAME;
					} else if (info->picTimingStruct == 4) {
						mode = INTERLEAVED_BOT_FIELD;
					} else {
						mode = INTERLEAVED_TOP_FIELD;
					}
				} else {
					mode = CONSECUTIVE_FRAME;
				}
			} else if (info->picStrPresent == 1) {
				if (info->picTimingStruct == 0)
					mode = CONSECUTIVE_FRAME;
				else if (info->picTimingStruct == 4)
					mode = INTERLEAVED_BOT_FIELD;
				else
					mode = INTERLEAVED_TOP_FIELD;
			} else //(info->decFrameInfo != 0) //decoded frame is NPF.
			{
				if (info->avcNpfFieldInfo == 1)
					mode = INTERLEAVED_BOT_FIELD; //top missing
				else if (info->avcNpfFieldInfo == 2)
					mode = INTERLEAVED_TOP_FIELD; //bot missing
				else if (info->avcNpfFieldInfo == 3) {
					ve1_err(VE1_WRAPPER_TAG,
						"AVC: top and bot fld are missing!\n");
				} else if (info->avcNpfFieldInfo == 0) {
					if (info->avcPocBot <
					    info->avcPocTop) { //if pocPic = pocTop = pocBot, still assume top fld first
						mode = INTERLEAVED_BOT_FIELD;
					} else
						mode = INTERLEAVED_TOP_FIELD;
				}
			}
		}
	} else if (decOP->bitstreamFormat == STD_MPEG4) {
		if (info->interlacedFrame == 1) {
			if (info->topFieldFirst)
				mode = INTERLEAVED_TOP_FIELD;
			else
				mode = INTERLEAVED_BOT_FIELD;
		}
	} else {
		mode = CONSECUTIVE_FRAME;
	}

	return mode;
}

static int ve1_get_frame_poc(struct ve1_ctx *ctx)
{
	DecOpenParam *decOP;
	DecOutputInfo *outputInfo;
	struct ve1_decoded_frame *frame;
	int retPOC = -1;

	decOP = (DecOpenParam *)ctx->decOP;
	outputInfo = (DecOutputInfo *)ctx->outputInfo;
	frame = (struct ve1_decoded_frame *)&ctx
			->frameQueue[outputInfo->indexFrameDecoded];

	if (decOP->bitstreamFormat == STD_AVC) {
		if (frame->picMode == CONSECUTIVE_FRAME) {
			retPOC = outputInfo->avcPocPic;
		} else {
			if (frame->picMode == INTERLEAVED_TOP_FIELD) {
				retPOC = outputInfo->avcPocTop;
			} else {
				retPOC = outputInfo->avcPocBot;
			}

			if (retPOC == 0) {
				retPOC = outputInfo->avcPocPic;
			}
		}
	}

	return retPOC;
}

static char ve1_check_if_paired_field_frm(struct ve1_ctx *ctx)
{
	DecOpenParam *decOP;
	DecOutputInfo *info;
	struct ve1_decoded_frame *frame;
	char isPairedFld = 0;
	bool isH264PicStructPaired;

	decOP = (DecOpenParam *)ctx->decOP;
	info = (DecOutputInfo *)ctx->outputInfo;
	frame = (struct ve1_decoded_frame *)&ctx
			->frameQueue[info->indexFrameDecoded];

	if (info->picTimingStruct == H264_PIC_STRUCT_TOP_BOTTOM ||
	    info->picTimingStruct == H264_PIC_STRUCT_BOTTOM_TOP ||
	    info->picTimingStruct == H264_PIC_STRUCT_TOP_BOTTOM_TOP ||
	    info->picTimingStruct == H264_PIC_STRUCT_BOTTOM_TOP_BOTTOM) {
		isH264PicStructPaired = true;
	} else {
		isH264PicStructPaired = false;
	}

	if (((decOP->bitstreamFormat == STD_MPEG2) &&
	     (info->pictureStructure == 3) &&
	     (frame->picMode != CONSECUTIVE_FRAME)) ||
	    ((decOP->bitstreamFormat == STD_AVC) &&
	     ((info->pictureStructure == 1) ||
	      ((info->decFrameInfo == 0) &&
	       (frame->picMode != CONSECUTIVE_FRAME)) ||
	      ((info->picStrPresent == 1) && isH264PicStructPaired))) ||
	    ((decOP->bitstreamFormat == STD_MPEG4) &&
	     (info->interlacedFrame == 1) &&
	     (frame->picMode != CONSECUTIVE_FRAME))) {
		isPairedFld = 1;
	}

	return isPairedFld;
}

void VE1_UpdateFrameQueueInfo(void *pCtx)
{
	struct ve1_ctx *ctx;
	DecOpenParam *decOP;
	DecOutputInfo *outputInfo;
	struct ve1_decoded_frame *frame;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decOP == NULL || ctx->outputInfo == NULL) {
		ve1_err(VE1_WRAPPER_TAG,
			"decOP == NULL or outputInfo == NULL\n");
		return;
	}

	decOP = (DecOpenParam *)ctx->decOP;
	outputInfo = (DecOutputInfo *)ctx->outputInfo;

	if (outputInfo->indexFrameDecoded >= 0) {
		frame = (struct ve1_decoded_frame *)&ctx
				->frameQueue[outputInfo->indexFrameDecoded];

		frame->picType = (outputInfo->interlacedFrame ?
					  outputInfo->picTypeFirst :
					  outputInfo->picType);
		frame->repeatFirstField =
			(signed char)outputInfo->repeatFirstField;
		frame->errorBlock = outputInfo->numOfErrMBs;
		frame->mvcPairIdx = -1;
		frame->reSend = 0;
		frame->mvcViewIdx =
			(signed char)outputInfo->mvcPicInfo.viewIdxDecoded;
		frame->topFieldFirst = (signed char)outputInfo->topFieldFirst;
		frame->stillVOBU = 0;
		frame->qualityLevel = 0;
		frame->picMode = ve1_get_picture_mode(ctx);
		frame->POC = ve1_get_frame_poc(ctx);
		frame->pairedFldFrm = ve1_check_if_paired_field_frm(ctx);
		frame->decodingSuccess = outputInfo->decodingSuccess;
		frame->bytePosFrameStart = outputInfo->bytePosFrameStart;
		frame->bytePosFrameEnd = outputInfo->bytePosFrameEnd;
		if (outputInfo->avcVuiInfo.vidSigTypePresent) {
			frame->video_full_range_flag =
				outputInfo->avcVuiInfo.vidFullRange;
			if (outputInfo->avcVuiInfo.colorDescPresent) {
				frame->colour_primaries =
					outputInfo->avcVuiInfo.colorPrimaries;
				frame->transfer_characteristics =
					outputInfo->avcVuiInfo
						.vuiTransferCharacteristics;
				frame->matrix_coefficients =
					outputInfo->avcVuiInfo
						.vuiMatrixCoefficients;
			} else {
				frame->colour_primaries = 0;
				frame->transfer_characteristics = 0;
				frame->matrix_coefficients = 0;
			}
		} else {
			frame->video_full_range_flag = 0;
		}
		ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			"type:%d.mode:%d.poc:%d.paired:%d.err:%d.avcvui(%d,%d,%d,%d)\n",
			frame->picType, frame->picMode, frame->POC,
			frame->pairedFldFrm, frame->errorBlock,
			frame->video_full_range_flag, frame->colour_primaries,
			frame->transfer_characteristics,
			frame->matrix_coefficients);
	}
}

static void rtkve1_dump_bs(struct ve1_ctx *ctx, uint8_t *buf,
					uint32_t size)
{
#if defined(RTKVE1_DUMP_BS_EN)
	int filp_open_flags;
	ssize_t bytes = 0;
	loff_t pos = 0;

	if (ctx == NULL) {
		return;
	}

	if ((buf != NULL) && (size != 0)) {
		if (ctx->bNewBsDumpFile == 1) {
			ctx->bNewBsDumpFile = 0;
			filp_open_flags = O_CREAT | O_WRONLY;
			memset(ctx->bsDumpFileName, 0, sizeof(unsigned char)*256);
			snprintf(ctx->bsDumpFileName, 256,
					"/mnt/ve1bs_%d.es",
					gBsDumpSerial);
			gBsDumpSerial++;
			vpu_info("%d.%s.create new ve1bs dump:%s\n",__LINE__,__func__,
					ctx->bsDumpFileName);
		} else {
			filp_open_flags = O_APPEND | O_WRONLY;
		}
		ctx->bsDumpFile =
			(void *)filp_open(ctx->bsDumpFileName, filp_open_flags, 0);
		if (IS_ERR((struct file *)ctx->bsDumpFile)) {
			ve1_err(VE1_WRAPPER_TAG, "filp_open %s fail\n",
					ctx->bsDumpFileName);
		} else {
			//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			//		"filp_open %s ok\n",
			//		ctx->bsDumpFileName);
			bytes =
				kernel_write((struct file *)(ctx->bsDumpFile),
							(void *)buf, (size_t)size, &pos);
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"kernel_write bytes:%ld.pos:%lld\n", bytes, pos);
			filp_close((struct file *)(ctx->bsDumpFile), NULL);
			//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
			//		"filp_close %s\n",
			//		ctx->bsDumpFileName);
			ctx->bsDumpFile = NULL;
		}
	}
#endif
}

void rtkve1_copy_to_bitstream_buffer(struct ve1_ctx *ctx, uint8_t *buf,
				     uint32_t size)
{
	unsigned long bsEndAddr = 0; // physical address
	unsigned long newBsWrPtr = 0; // physical address
	unsigned long virtBsWrPtr = 0; // virtual address
	int size0 = 0;
	int size1 = 0;

	rtkve1_dump_bs(ctx, buf, size);

	bsEndAddr = ctx->bitstream.paddr + ctx->bitstream.size;
	newBsWrPtr = ctx->bsWrPtr + size;
	virtBsWrPtr =
		ve1_ring_phys_to_virt(ctx->bsWrPtr, ctx->bitstream.paddr,
				      (unsigned long)ctx->bitstream.vaddr);

	if (newBsWrPtr >= bsEndAddr) {
		size0 = bsEndAddr - ctx->bsWrPtr;
		size1 = size - size0;

		if (ctx->is_svp) {
#if defined(ENABLE_TEE_DRM_FLOW)
			if (ctx->out_vb2_q_memory == V4L2_MEMORY_MMAP) {
				ret = ta_TEEapi_memcpy_a7(
					(struct tee_context *)
						decHandle->teeapi_ctx,
					decHandle->teeapi_tee_session,
					ctx->bsWrPtr, buf, size0);
				if (ret < 0) {
					ve1_err(VE1_WRAPPER_TAG,
						"ta_TEEapi_memcpy_a7() fail.bsWrPtr:0x%lx.buf:0x%px.size0:%d\n",
						ctx->bsWrPtr, buf, size0);
					return -1;
				}

				ret = ta_TEEapi_memcpy_a7(
					(struct tee_context *)
						decHandle->teeapi_ctx,
					decHandle->teeapi_tee_session,
					ctx->bitstream.paddr, buf + size0,
					size1);
				if (ret < 0) {
					ve1_err(VE1_WRAPPER_TAG,
						"ta_TEEapi_memcpy_a7() fail.bsStartAddr:0x%lx.buf+size0:0x%px.size1:%d\n",
						ctx->bitstream.paddr,
						buf + size0, size1);
					return -1;
				}
			} else if (ctx->out_vb2_q_memory ==
				   V4L2_MEMORY_DMABUF) {
				ret = ta_TEEapi_memcpy(
					(struct tee_context *)
						decHandle->teeapi_ctx,
					decHandle->teeapi_tee_session,
					ctx->bsWrPtr, (uintptr_t)buf, size0);
				if (ret < 0) {
					ve1_err(VE1_WRAPPER_TAG,
						"ta_TEEapi_memcpy() fail.bsWrPtr:0x%lx.buf:0x%px.size0:%d\n",
						ctx->bsWrPtr, buf, size0);
					return -1;
				}

				ret = ta_TEEapi_memcpy(
					(struct tee_context *)
						decHandle->teeapi_ctx,
					decHandle->teeapi_tee_session,
					ctx->bitstream.paddr,
					(uintptr_t)buf + size0, size1);
				if (ret < 0) {
					ve1_err(VE1_WRAPPER_TAG,
						"ta_TEEapi_memcpy() fail.bsStartAddr:0x%lx.buf+size0:0x%px.size1:%d\n",
						ctx->bitstream.paddr,
						buf + size0, size1);
					return -1;
				}
			}
#endif // #if defined(ENABLE_TEE_DRM_FLOW)
		} else {
			osal_memcpy((void *)virtBsWrPtr, (void *)buf, size0);
			osal_memcpy((void *)ctx->bitstream.vaddr,
				    (void *)(buf + size0), size1);
		}
		ctx->bsWrPtr = ctx->bitstream.paddr + size1;
	} else {
		if (ctx->is_svp) {
#if defined(ENABLE_TEE_DRM_FLOW)
			if (ctx->out_vb2_q_memory == V4L2_MEMORY_MMAP) {
				ret = ta_TEEapi_memcpy_a7(
					(struct tee_context *)
						decHandle->teeapi_ctx,
					decHandle->teeapi_tee_session,
					ctx->bsWrPtr, buf, size);
				if (ret < 0) {
					ve1_err(VE1_WRAPPER_TAG,
						"ta_TEEapi_memcpy_a7() fail.bsWrPtr:0x%lx.buf:0x%px.size:%d\n",
						ctx->bsWrPtr, buf, size);
					return -1;
				}
			} else if (ctx->out_vb2_q_memory ==
				   V4L2_MEMORY_DMABUF) {
				ret = ta_TEEapi_memcpy(
					(struct tee_context *)
						decHandle->teeapi_ctx,
					decHandle->teeapi_tee_session,
					ctx->bsWrPtr, (uintptr_t)buf, size);
				if (ret < 0) {
					ve1_err(VE1_WRAPPER_TAG,
						"ta_TEEapi_memcpy() fail.bsWrPtr:0x%lx.buf:0x%px.size:%d\n",
						ctx->bsWrPtr, buf, size);
					return -1;
				}
			}
#endif // #if defined(ENABLE_TEE_DRM_FLOW)
		} else {
			osal_memcpy((void *)virtBsWrPtr, (void *)buf, size);
		}
		ctx->bsWrPtr = newBsWrPtr;
	}
}

int32_t BuildSeqHeader(void *pCtx, uint8_t *buf, uint32_t buf_size)
{
	struct ve1_ctx *ctx;
	DecOpenParam *decOP;
	uint8_t *pbMetaData = buf;
	uint8_t *p = pbMetaData;
	int32_t size = 0; // metadata header size
	uint32_t codingType = 0;
	uint32_t nFrameWidth, nFrameHeight, picWidth, picHeight;
	uint32_t width_in_pixels, height_in_pixels;
	uint32_t signature = MAKE_FOURCC('D', 'K', 'I', 'F');
	uint32_t version = 0x00;
	uint32_t length_of_header_in_bytes = 0x20;
	uint32_t codec_FourCC = MAKE_FOURCC('V', 'P', '8', '0');
	uint32_t time_base_denominator = 30;
//	uint32_t time_base_numerator = -1;
	uint32_t number_of_frames_in_file = 0;
	uint32_t unused = 0;


	if (buf == NULL || buf_size <= 0) {
		ve1_err(VE1_WRAPPER_TAG, "buf == NULL || size <= 0\n");
		return -1;
	}
	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}

	ctx = (struct ve1_ctx *)pCtx;
	decOP = (DecOpenParam *)ctx->decOP;

	if (ctx->seqHeader == NULL) {
		ctx->seqHeader = kmalloc(MAX_CHUNK_HEADER_SIZE, GFP_KERNEL);
	}

	codingType = decOP->bitstreamFormat;
	size = 0;

	if (codingType == STD_VP8) {
		if (ctx->seqHeaderSize == 0) {
			nFrameWidth = decOP->frameWidth;
			nFrameHeight = decOP->frameHeight;
			//20160926, workaround for invalid resolution
			if (buf_size >= 10) {
				picWidth =
					(uint32_t)(p[7] << 8 | p[6]);
				picHeight =
					(uint32_t)(p[9] << 8 | p[8]);
				if (picWidth <= 1920 && picHeight <= 1088) {
					nFrameWidth = picWidth;
					nFrameHeight = picHeight;
				}
			}
			width_in_pixels = nFrameWidth;
			height_in_pixels = nFrameHeight;

			//signature 'DKIF'
			ctx->seqHeader[0] = (unsigned char)(signature >> 0);
			ctx->seqHeader[1] = (unsigned char)(signature >> 8);
			ctx->seqHeader[2] = (unsigned char)(signature >> 16);
			ctx->seqHeader[3] = (unsigned char)(signature >> 24);
			//version
			ctx->seqHeader[4] = (unsigned char)(version >> 0);
			ctx->seqHeader[5] = (unsigned char)(version >> 8);
			//length of header in bytes
			ctx->seqHeader[6] =
				(unsigned char)(length_of_header_in_bytes >> 0);
			ctx->seqHeader[7] =
				(unsigned char)(length_of_header_in_bytes >> 8);
			//codec FourCC of VP80
			ctx->seqHeader[8] = (unsigned char)(codec_FourCC >> 0);
			ctx->seqHeader[9] = (unsigned char)(codec_FourCC >> 8);
			ctx->seqHeader[10] =
				(unsigned char)(codec_FourCC >> 16);
			ctx->seqHeader[11] =
				(unsigned char)(codec_FourCC >> 24);
			//width
			ctx->seqHeader[12] =
				(unsigned char)(width_in_pixels >> 0);
			ctx->seqHeader[13] =
				(unsigned char)(width_in_pixels >> 8);
			//height
			ctx->seqHeader[14] =
				(unsigned char)(height_in_pixels >> 0);
			ctx->seqHeader[15] =
				(unsigned char)(height_in_pixels >> 8);
			//frame rate
			ctx->seqHeader[16] =
				(unsigned char)(time_base_denominator >> 0);
			ctx->seqHeader[17] =
				(unsigned char)(time_base_denominator >> 8);
			ctx->seqHeader[18] =
				(unsigned char)(time_base_denominator >> 16);
			ctx->seqHeader[19] =
				(unsigned char)(time_base_denominator >> 24);
			//time scale(?)
			ctx->seqHeader[20] =
				(unsigned char)(number_of_frames_in_file >> 0);
			ctx->seqHeader[21] =
				(unsigned char)(number_of_frames_in_file >> 8);
			ctx->seqHeader[22] =
				(unsigned char)(number_of_frames_in_file >> 16);
			ctx->seqHeader[23] =
				(unsigned char)(number_of_frames_in_file >> 24);
			//number of frames in file
			ctx->seqHeader[24] =
				(unsigned char)(number_of_frames_in_file >> 0);
			ctx->seqHeader[25] =
				(unsigned char)(number_of_frames_in_file >> 8);
			ctx->seqHeader[26] =
				(unsigned char)(number_of_frames_in_file >> 16);
			ctx->seqHeader[27] =
				(unsigned char)(number_of_frames_in_file >> 24);
			//unused
			ctx->seqHeader[28] = (unsigned char)(unused >> 0);
			ctx->seqHeader[29] = (unsigned char)(unused >> 8);
			ctx->seqHeader[30] = (unsigned char)(unused >> 16);
			ctx->seqHeader[31] = (unsigned char)(unused >> 24);
			size += 32;
		}
	} else {
		size = 0;
	}

	ctx->seqHeaderSize += size;

	return size;
}

int32_t BuildPicHeader(void *pCtx, uint8_t *buf, uint32_t buf_size)
{
	struct ve1_ctx *ctx;
	DecOpenParam *decOP;
	int32_t size = 0;
	uint32_t codingType = 0;

	if (buf == NULL || buf_size <= 0) {
		ve1_err(VE1_WRAPPER_TAG, "buf == NULL || size <= 0\n");
		return -1;
	}
	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}

	ctx = (struct ve1_ctx *)pCtx;
	decOP = (DecOpenParam *)ctx->decOP;

	if (ctx->picHeader == NULL) {
		ctx->picHeader = kmalloc(MAX_CHUNK_HEADER_SIZE, GFP_KERNEL);
	}

	codingType = decOP->bitstreamFormat;
	size = 0;

	if (codingType == STD_VP8) {
		//size of frame in bytes (not including the 12-byte header)
		ctx->picHeader[0] = (unsigned char)(buf_size >> 0);
		ctx->picHeader[1] = (unsigned char)(buf_size >> 8);
		ctx->picHeader[2] = (unsigned char)(buf_size >> 16);
		ctx->picHeader[3] = (unsigned char)(buf_size >> 24);
		//64-bit presentation timestamp
		ctx->picHeader[4] = 0;
		ctx->picHeader[5] = 0;
		ctx->picHeader[6] = 0;
		ctx->picHeader[7] = 0;
		ctx->picHeader[8] = 0;
		ctx->picHeader[9] = 0;
		ctx->picHeader[10] = 0;
		ctx->picHeader[11] = 0;
		size += 12;
	}

	return size;
}

int VE1_DecUpdateBS(void *pCtx, uint8_t *buf, uint32_t size)
{
	struct ve1_ctx *ctx;
	DecHandle decHandle = NULL;
	int ret = 0;
	unsigned long valid_data = 0;
	bool queueRet = false;
	DecOpenParam *decOP;
	unsigned int seqHdrSize = 0;
	unsigned int picHdrSize = 0;
	unsigned int totalUpBsSize = 0;
	unsigned int frame_chunk_len = 0;

	if (buf == NULL || size <= 0) {
		ve1_err(VE1_WRAPPER_TAG, "buf == NULL || size <= 0\n");
		return -1;
	}
	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "decHandle == NULL\n");
		return -1;
	}
	decHandle = (DecHandle)ctx->decHandle;

	if (ctx->decOP == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "ctx->decOP is NULL\n");
		return -1;
	}
	decOP = (DecOpenParam *)ctx->decOP;

	if (decOP->bitstreamFormat == STD_VP8) {
		seqHdrSize = BuildSeqHeader((void *)ctx, buf, size);
		if (seqHdrSize > 0) {
			rtkve1_copy_to_bitstream_buffer(ctx, ctx->seqHeader,
							seqHdrSize);
			totalUpBsSize += seqHdrSize;
		}
		picHdrSize = BuildPicHeader((void *)ctx, buf, size);
		if (picHdrSize > 0) {
			frame_chunk_len = (ctx->picHeader[0]) |
					  (ctx->picHeader[1] << 8) |
					  (ctx->picHeader[2] << 16) |
					  (ctx->picHeader[3] << 24);
			rtkve1_copy_to_bitstream_buffer(ctx, ctx->picHeader,
							picHdrSize);
			totalUpBsSize += picHdrSize;
		}
	}

	rtkve1_copy_to_bitstream_buffer(ctx, buf, size);
	totalUpBsSize += size;

	ret = VPU_DecUpdateBitstreamBuffer(decHandle, totalUpBsSize);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG,
			"VPU_DecUpdateBitstreamBuffer fail.ret:%d.size:%d\n",
			ret, totalUpBsSize);
		return -1;
	} else {
		if ((ctx->seqInited) &&
			(decOP->bitstreamMode == BS_MODE_PIC_END) &&
		    (decOP->bitstreamFormat == STD_AVC ||
		     decOP->bitstreamFormat == STD_MPEG2 ||
			 decOP->bitstreamFormat == STD_VP8)) {
			ctx->bPostponeUpBs = true;
		}
		ctx->accuBsFeedBytes += totalUpBsSize;
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d.accuBsFeedBytes:%d\n",
			ret, totalUpBsSize, ctx->accuBsFeedBytes);

		VE1_DecGetRdWrPtr(ctx);
		// handle priority of bWaitNextField is higher than bBufEmptyFlag
		if (ctx->bWaitNextField) {
			if (ctx->bBufEmptyFlag) {
				ctx->bBufEmptyFlag = false;
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"bWaitNextField.clear bBufEmptyFlag\n");
			}
			ctx->bGotNextField = true;
			if (ctx->fldDoneVpuRp != ctx->vpuRdPtr) {
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"FATAL.bWaitNextField.fld_rp:0x%x != vpu_rp:0x%x\n",
					ctx->fldDoneVpuRp, ctx->vpuRdPtr);
			}
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"bWaitNextField.set rdPtr:0x%x.clear int_reason\n",
				ctx->vpuRdPtr);
			VPU_DecSetRdPtr((DecHandle)ctx->decHandle,
					ctx->vpuRdPtr, 0);
			VPU_ClearInterrupt(VE1_COREIDX);
			ctx->int_reason = 0;
			ctx->bWaitNextField = false;
			queueRet =
				queue_work(ctx->workqueue, &ctx->pic_run_work);
			if (queueRet)
				ctx->cntQueuePicRunWorkOk++;
			else
				ctx->cntQueuePicRunWorkFail++;
			ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
				"queue_work pic_run_work.cnt(%d,%d).ret:%d\n",
				ctx->cntQueuePicRunWorkOk,
				ctx->cntQueuePicRunWorkFail, queueRet);
		} else if (ctx->bBufEmptyFlag) {
			if (((DecOpenParam *)ctx->decOP)->bitstreamMode ==
			    BS_MODE_INTERRUPT) {
				valid_data = ve1_ring_valid_data(
					ctx->bitstream.paddr,
					ctx->bitstream.paddr +
						ctx->bitstream.size,
					ctx->vpuRdPtr, ctx->vpuWrPtr);
				if (valid_data >= 1024) {
					ctx->bBufEmptyFlag = false;
				}
			} else {
				ctx->bBufEmptyFlag = false;
			}
			if (!ctx->bBufEmptyFlag) {
				queueRet = queue_work(ctx->workqueue,
						      &ctx->pic_run_work);
				if (queueRet)
					ctx->cntQueuePicRunWorkOk++;
				else
					ctx->cntQueuePicRunWorkFail++;
				ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
					"queue_work pic_run_work.cnt(%d,%d).ret:%d\n",
					ctx->cntQueuePicRunWorkOk,
					ctx->cntQueuePicRunWorkFail, queueRet);
			}
		}
	}

	return 0;
}

int rtkve1_flush_bitstream(void *pCtx)
{
	int ret = 0;
	struct ve1_ctx *ctx;
	DecHandle decHandle = NULL;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "decHandle == NULL\n");
		return -1;
	}
	decHandle = (DecHandle)ctx->decHandle;

	mutex_lock(&ctx->ve1_mutex);
	ctx->accuBsFeedBytes = 0;
	ctx->bsRdPtr = ctx->bsWrPtr;
	ret = VPU_DecSetRdPtr(decHandle, ctx->bsWrPtr, 1);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG,
			"VPU_DecSetRdPtr fail.ret:%d\n", ret);
		ret = -1;
		goto out;
	}
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"VPU_DecSetRdPtr(0x%x, 1)\n",
		(unsigned int)ctx->bsWrPtr);
	VE1_DecGetRdWrPtr(pCtx);

	if (ctx->streamEnd) {
		ret = VPU_DecUpdateBitstreamBuffer((DecHandle)ctx->decHandle,
											STREAM_END_CLEAR_FLAG);
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d\n",
			ret, STREAM_END_CLEAR_FLAG);
		VE1_DecGetRdWrPtr(ctx);
		ctx->streamEnd = 0;
	}
	ctx->handle_eos_by = VE1_HANDLE_EOS_BY_NONE;
	ctx->bPostponeUpBs = false;
	ctx->dpbFull = 0;

out:
	mutex_unlock(&ctx->ve1_mutex);
	return ret;
}

int rtkve1_flush(void *pCtx)
{
	int ret = 0;
	struct ve1_ctx *ctx;
	DecHandle decHandle = NULL;
    int vpu_timeout_cnt = 1000;
	unsigned int dispFlag = 0xffffffff;

	if (pCtx == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "pCtx == NULL\n");
		return -1;
	}
	ctx = (struct ve1_ctx *)pCtx;

	if (ctx->decHandle == NULL) {
		ve1_err(VE1_WRAPPER_TAG, "decHandle == NULL\n");
		return -1;
	}
	decHandle = (DecHandle)ctx->decHandle;

	mutex_lock(&ctx->ve1_mutex);
	ctx->bFlush = true;
	mutex_unlock(&ctx->ve1_mutex);

	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
		"bf flush_work pic_run_work\n");
	flush_work(&ctx->pic_run_work);
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
		"af flush_work pic_run_work\n");

	mutex_lock(&ctx->ve1_mutex);
	ctx->bFlush = false;
	ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
		"VE1_DecCheckComplete\n");
	VE1_DecCheckComplete(ctx);

    if (VPU_IsBusy(VE1_COREIDX)) {
        VPU_DecUpdateBitstreamBuffer(decHandle, STREAM_END_SET_FLAG);
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d\n",
			ret, STREAM_END_SET_FLAG);
		VE1_DecGetRdWrPtr(pCtx);
        while (VPU_IsBusy(VE1_COREIDX) && vpu_timeout_cnt > 0) {
            vpu_timeout_cnt --;
            usleep_range(1000, 1000);
        }
        VPU_DecUpdateBitstreamBuffer(decHandle, STREAM_END_CLEAR_FLAG);
		ve1_dbg(VPU_DBG_VE1_UP_BS, VE1_WRAPPER_TAG,
			"af VPU_DecUpdateBitstreamBuffer.ret:%d.size:%d\n",
			ret, STREAM_END_CLEAR_FLAG);
		VE1_DecGetRdWrPtr(pCtx);
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"vpu_timeout_cnt:%d\n",vpu_timeout_cnt);
    }

	if (ctx->ve1DecState >= VE1_STATE_DEC_SET_DPB) {
		ret = VPU_DecFrameBufferFlush(decHandle, NULL, NULL);
		if (ret != RETCODE_SUCCESS) {
			ve1_err(VE1_WRAPPER_TAG,
				"VPU_DecFrameBufferFlush fail.ret:%d\n", ret);
			ret = -1;
			goto out;
		}
		ve1_dbg(VPU_DBG_NONE, VE1_LOGTAG,
			"af VPU_DecFrameBufferFlush.ret:%d\n", ret);
	}

	ret = VPU_DecSetDispFlag(decHandle, dispFlag);
	if (ret != RETCODE_SUCCESS) {
		ve1_err(VE1_WRAPPER_TAG,
			"VPU_DecSetDispFlag fail.ret:%d\n", ret);
		ret = -1;
		goto out;
	}
	ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
		"VPU_DecSetDispFlag.dispFlag:0x%x\n", dispFlag);

	ctx->dpbFull = 0;
	ctx->outputinfoSN = 0;
	ctx->decodedFrmNum = 0;
	ctx->displayFrmNum = 0;
	ctx->lastInfoFrmStart = 0;
	ctx->lastInfoFrmEnd = 0;
	//ctx->bPostponeUpBs = false;
	//ve1_dbg(VPU_DBG_NONE, VE1_WRAPPER_TAG,
	//	"set bPostponeUpBs:%d\n", ctx->bPostponeUpBs);

	ret = 0;
out:
	mutex_unlock(&ctx->ve1_mutex);
	return ret;
}