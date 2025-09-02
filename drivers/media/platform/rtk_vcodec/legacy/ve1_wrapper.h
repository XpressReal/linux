#ifndef __VE1_WRAPPER_H__
#define __VE1_WRAPPER_H__

#define VE1_COREIDX 0

#define USER_DATA_INFO_OFFSET (8 * 17)
#define USER_DATA_SRC_BUF_SIZE (2048) //jim.hsu, 512 for MPEG, 2048 for AVC

struct ve1_decopen_param {
	unsigned int src_fmt_fourcc; // ex: V4L2_PIX_FMT_H264
	unsigned int dst_fmt_fourcc; // ex: V4L2_PIX_FMT_NV21
	unsigned int width;
	unsigned int height;
};

typedef enum {
	VE1_STATE_DEC_UNINIT = 0,
	VE1_STATE_DEC_INITED,
	VE1_STATE_DEC_CLOSED,
	VE1_STATE_DEC_OPENED,
	VE1_STATE_DEC_SEQ_INIT_ISSUED,
	VE1_STATE_DEC_SEQ_INIT_DONE,
	VE1_STATE_DEC_SET_DPB,
	VE1_STATE_DEC_START_DEC_ISSUED,
	VE1_STATE_DEC_PIC_DONE,
	VE1_STATE_DEC_MAX_NUM
} ve1_dec_state;

typedef enum {
	VE1_DEC_RETURN_INVALID = -1,
	VE1_DEC_RETURN_OK = 0,
	VE1_DEC_RETURN_SEQ_CHANGE
} VE1_DEC_RETURN_VALUE;

enum { ENUM_CC_S = 0x0324,
       ENUM_CC_U = 0x1107,
       ENUM_CC_P = 0x2418,
       ENUM_CC_AU = 0x9631,
}; // for tee_api

int VE1_DecInit(void *pCtx, void *videc_dev);
int VE1_DecOpen(void *pCtx, void *pParam);
int VE1_DecGetRdWrPtr(void *pCtx);
int VE1_SetStreamEnd(void *pCtx);
int VE1_DecSeqInit(void *pCtx);
int VE1_DecStartDecode(void *pCtx);
int VE1_DecWaitPicDone(void *pCtx);
int VE1_UpdateDPBStatus(void *pCtx, unsigned int dpb_paddr,
			 unsigned int sequenceNo, unsigned int status);
/**
* @brief Do original sequence remain decoding and flush, prepare for new sequence
* @return VE1_DEC_RETURN_VALUE
*/
int ve1_prepare_seq_change(void *pCtx);
/**
* @brief Get decode result (outputinfo) from VE1
* @return VE1_DEC_RETURN_VALUE
*/
int VE1_DecPicDone(void *pCtx);
int VE1_DecCheckComplete(void *pCtx);
int VE1_DecClose(void *pCtx);
int VE1_DecDeInit(void *pCtx);
void VE1_GetParsedInfo(void *pCtx, void *pInfo);
void VE1_GetDisplayFrameInfo(void *pCtx, void *displayFrameInfo);
int VE1_AllocateBitstreamBuffer(struct device *dev, unsigned long *phys_addr,
				unsigned long *virt_addr, unsigned long size,
				unsigned int is_svp);
int VE1_FreeBitstreamBuffer(struct device *dev, unsigned long virt_addr,
			    unsigned long phys_addr, unsigned int size);
void VE1_UpdateFrameQueueInfo(void *pCtx);
int VE1_DecUpdateBS(void *pCtx, uint8_t *buf, uint32_t size);
void *rtkve1_find_dpb(void *pCtx, unsigned long dpb_paddr, unsigned int seqNo);
int rtkve1_add_capbuf_to_dpb(void *pCtx, unsigned long size,
			     unsigned long phys_addr, void *vb2_v4l2_buf);
int rtkve1_register_dpbs(void *pCtx);
int rtkve1_check_new_dpb(void *pCtx);
void rtkve1_flush_dpbs(void *pCtx);
int rtkve1_flush_bitstream(void *pCtx);
int rtkve1_flush(void *pCtx);
void *rtkve1_find_dpb_undequeue(void *pCtx, unsigned int seqNo);
int rtkve1_unreg_dpbs(void *pCtx);
#endif // #define __VE1_WRAPPER_H__
