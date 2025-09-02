
#ifndef __VE1_V4L2_H__
#define __VE1_V4L2_H__

#include <linux/debugfs.h> // for struct debugfs_blob_wrapper

#include <media/v4l2-device.h> // for struct v4l2_device, struct video_device
#include <media/v4l2-ctrls.h> // for struct v4l2_ctrl_handler
#include <media/v4l2-fh.h> // for struct v4l2_fh
#include "ve_common.h"
#include "debug.h"

#define VE1_LOGTAG "[V4L2_VE1]"

#define MAX_VE1_FRAME_BUFFERS 32
#define VE1_STREAM_END_FLAG (1 << 2)
#define PTS_UNIT 90000L // 90000 pts/sec

#define VE1_ION_STRUCT_NUM 256

extern unsigned int vpu_debug;

#define ve1_printk(level, category, tag, fmt, arg...)                          \
	do {                                                                   \
		int show_log = 0;                                              \
		if (vpu_debug & VPU_DBG_VE1_ALL)                               \
			show_log = 1;                                          \
		else if (vpu_debug & category)                                 \
			show_log = 1;                                          \
		if (show_log)                                                  \
			printk(level "%s [%d]%s." fmt, tag, __LINE__,          \
			       __func__, ##arg);                               \
	} while (0)

#define ve1_err(tag, fmt, arg...)                                              \
	printk(KERN_ERR "%s [%d]%s." fmt, tag, __LINE__, __func__, ##arg);

#define ve1_warn(tag, fmt, arg...)                                             \
	printk(KERN_WARNING "%s [%d]%s." fmt, tag, __LINE__, __func__, ##arg);

#define ve1_info(tag, fmt, arg...)                                             \
	printk(KERN_INFO "%s [%d]%s." fmt, tag, __LINE__, __func__, ##arg);

#define ve1_dbg(category, tag, fmt, arg...)                                    \
	ve1_printk(KERN_DEBUG, category, tag, fmt, ##arg)

#define MPEG2_CC_REG_FRAME_MAX 32 // the maximum of VPU register frames
#define MPEG2_CC_RINGBUF_SIZE 2048
#define RTK_CC_SYNC 0x52744B63 // RtKc
#define RTK_CC_HEADER_SIZE 16
#define USER_DATA_NUM_MAX 8

enum { ENUM_CC_MPGE2 = 1, ENUM_CC_H264, ENUM_CC_H265 };

enum { ENUM_CC_A53_PART4 = 0, ENUM_CC_SCTE_20, ENUM_CC_DVD, ENUM_CC_TOTAL_NUM };

typedef struct {
	int nUserDataNum;
	int nTotalUserDataSize;
	int nUserDataType[USER_DATA_NUM_MAX];
	int nUserDataOffset[USER_DATA_NUM_MAX];
	int nUserDataSize[USER_DATA_NUM_MAX];
} USER_DATA_INFO;

/**
* RTKVE1_DPB_ST_EMPTY:			this dpb info is empty
* RTKVE1_DPB_ST_VALID:			this dpb is assigned by cap_qbuf
* this status means cap_buf is owned by driver
* set:		ve1_cap_qbuf() => VE1_UpdateDPBStatus() or rtkve1_add_capbuf_to_dpb()
* clear:	ve1_stop_streaming(cap) => rtkve1_flush_dpbs(), all cap_bufs which owned by driver should be done
*
* RTKVE1_DPB_ST_REG: 			this dpb is registered to VE1
* set:		rtkve1_register_dpbs(), rtkve1_register_new_dpb
* clear:	ve1_free_capture() caused rtkve1_unreg_dpbs(), all dpb info are cleared
*
* RTKVE1_DPB_ST_DQ:				this dpb is dequeued by cap_dqbuf
* set:		ve1_cap_dqbuf() => VE1_UpdateDPBStatus()
* clear:	(1) ve1_cap_qbuf() => VE1_UpdateDPBStatus(RTKVE1_DPB_ST_VALID)
*			(2) ve1_stop_streaming(cap) => rtkve1_flush_dpbs()
*
* RTKVE1_DPB_ST_WAIT_RECYCLE:	this dpb is waiting for recycle
* set:		ve1_cap_qbuf() => VE1_UpdateDPBStatus()
* clear:	(1) rtkve1_recycle_dpb()
*			(2) ve1_stop_streaming(cap) => rtkve1_flush_dpbs()
*/
typedef enum {
	RTKVE1_DPB_ST_EMPTY = 0x0000,
	RTKVE1_DPB_ST_VALID =
		0x0001,
	RTKVE1_DPB_ST_REG = 0x0002,
	RTKVE1_DPB_ST_DQ = 0x0004,
	RTKVE1_DPB_ST_WAIT_RECYCLE =
		0x0008
} RTKVE1_DPB_STATUS;

#define IS_RTKVE1_DPB_EMPTY(status) (((status) == RTKVE1_DPB_ST_EMPTY) ? 1 : 0)
#define IS_RTKVE1_DPB_VALID(status) ((status)&RTKVE1_DPB_ST_VALID)
#define IS_RTKVE1_DPB_REG(status) ((status)&RTKVE1_DPB_ST_REG)
#define IS_RTKVE1_DPB_DQ(status) ((status)&RTKVE1_DPB_ST_DQ)
#define IS_RTKVE1_DPB_WAIT_RECYCLE(status) ((status)&RTKVE1_DPB_ST_WAIT_RECYCLE)

struct rtkve1_dpb_t {
	unsigned int status; // RTKVE1_DPB_STATUS
	unsigned int size;
	unsigned long phys_addr;
	unsigned long virt_addr;
	void *vb2_v4l2_buf; // struct vb2_v4l2_buffer *
	unsigned long dmabuf; // struct dma_buf *
	unsigned long attach; // struct dma_buf_attachment *
	unsigned long table; // struct sg_table *
	void *reg_entry;
	unsigned int regIndex; // the index when registering dpb to VE1
	unsigned int seqNo; // sequence number
};

typedef uint32_t PhysicalAddress;

struct tch_metadata_variables {
	int tmInputSignalBlackLevelOffset;
	int tmInputSignalWhiteLevelOffset;
	int shadowGain;
	int highlightGain;
	int midToneWidthAdjFactor;
	int tmOutputFineTuningNumVal;
	int tmOutputFineTuningX[15];
	int tmOutputFineTuningY[15];
	int saturationGainNumVal;
	int saturationGainX[15];
	int saturationGainY[15];
};

struct tch_metadata_tables {
	int luminanceMappingNumVal;
	int luminanceMappingX[33];
	int luminanceMappingY[33];
	int colourCorrectionNumVal;
	int colourCorrectionX[33];
	int colourCorrectionY[33];
	int chromaToLumaInjectionMuA;
	int chromaToLumaInjectionMuB;
};

struct tch_metadata {
	int specVersion;
	int payloadMode;
	int hdrPicColourSpace;
	int hdrMasterDisplayColourSpace;
	int hdrMasterDisplayMaxLuminance;
	int hdrMasterDisplayMinLuminance;
	int sdrPicColourSpace;
	int sdrMasterDisplayColourSpace;
	union {
		struct tch_metadata_variables variables;
		struct tch_metadata_tables tables;
	} u;
};

typedef enum {
	VE1_DEC_CODEC_AVC = 0,
	VE1_DEC_CODEC_VC1,
	VE1_DEC_CODEC_MPEG2,
	VE1_DEC_CODEC_MPEG4,
	VE1_DEC_CODEC_H263,
	VE1_DEC_CODEC_RV = 6,
	VE1_DEC_CODEC_AVS,
	VE1_DEC_CODEC_THO = 9,
	VE1_DEC_CODEC_VP3,
	VE1_DEC_CODEC_VP8,
	VE1_DEC_CODEC_MAX
} ve1_decode_codec;

enum { V4L2_M2M_SRC = 0,
       V4L2_M2M_DST = 1,
};

enum { VE1_HANDLE_EOS_BY_NONE = 0,
       VE1_HANDLE_EOS_BY_PREPARE_RUN = 1,
       VE1_HANDLE_EOS_SET_END = 2,
	   VE1_HANDLE_EOS_DEC_FINISH = 3,
};

struct ve1_device;

struct ve1_devtype {
	const struct ve1_codec *codecs;
	unsigned int num_codecs;
	const struct ve1_device *vdev;
};

struct ve1_buf {
	void *vaddr;
	unsigned long paddr;
	unsigned long dma_buf;
	u32 size;
	struct debugfs_blob_wrapper blob;
	struct dentry *dentry;
};

struct ve1_dev {
	struct v4l2_device v4l2_dev;
	struct video_device vfd;
	struct platform_device *plat_dev;
	const struct ve1_devtype *devtype;
	spinlock_t irqlock;
	struct mutex dev_mutex;
	struct mutex ve1_mutex;
	struct workqueue_struct *workqueue;
	struct v4l2_m2m_dev *m2m_dev;
};

struct ve1_codec {
	u32 type;
	u32 src_fourcc;
	u32 dst_fourcc;
	u32 max_w;
	u32 max_h;
};

struct ve1_meta {
	struct list_head list;
	u32 sequence;
	struct v4l2_timecode timecode;
	u64 timestamp;
	u32 start;
	u32 end;
};

struct ve1_q_data {
	unsigned int width;
	unsigned int height;
	unsigned int bytesperline;
	unsigned int sizeimage;
	unsigned int fourcc;
	struct v4l2_rect rect;
};

struct ve1_parsed_initial_info {
	unsigned int pic_width; // picture width in sequence header
	unsigned int pic_height; // picture height in sequence header
	unsigned int visible_rect_left;
	unsigned int visible_rect_top;
	unsigned int visible_rect_w;
	unsigned int visible_rect_h;
	unsigned int minDpbCount;
};

struct ve1_displayable_frame {
	struct list_head list;
	unsigned int
		regIndex; // outputinfo.indexFrameDisplay or outputinfo.indexFrameDecoded
	PhysicalAddress
		dpb_paddr; // physical address of decoded picture buffer (decoded frame buffer)
	struct vb2_v4l2_buffer
		*vb2_v4l2_buf; // the struct vb2_v4l2_buffer of dpb (cap_buf)
	unsigned int
		isDequeued; // 1 means this vb2_v4l2_buf is dequeued in ve1_cap_dqbuf
	unsigned long size;
	unsigned long long
		timestamp; // timestamp get from src buffer (V4L2_BUF_TYPE_VIDEO_OUTPUT)
	struct v4l2_timecode
		timecode; // timecode get from src buffer (V4L2_BUF_TYPE_VIDEO_OUTPUT)
	unsigned int last_frame;
	unsigned int sequenceNo; // this frame belongs to which sequence

	unsigned int frameBufIndex; // index of frame buffer
	unsigned int
		Y_addr; // physical address of luma of decoded/display frame buffer
	unsigned int
		U_addr; // physical address of chroma of decoded/display frame buffer
	unsigned int bufStride; // allocated frame buffer width
	unsigned int bufHeight; // allocatae frame buffer height
	unsigned int picWidth; // picture width from Sequence Parameter Set
	unsigned int picHeight; // picture height from Sequence Parameter Set
	unsigned int rectLeft; // the left of display rectangle
	unsigned int rectTop; // the top of display rectangle
	unsigned int rectRight; // the right of display rectangle
	unsigned int rectBottom; // the bottom of display rectangle
	unsigned int bitDepth; // pixel bit-depth
	unsigned int mode; // progressive or interlace (top first or bottom first)
	unsigned long long timeTick; // 90K/fps
	unsigned int video_full_range_flag; // H264 vui_parameters
	unsigned int transfer_characteristics; // H264 vui_parameters
	unsigned int matrix_coefficients; // H264 vui_parameters
	int POC;
};

struct ve1_decoded_frame {
	int picType;
	int errorBlock;
	int POC;
	signed char repeatFirstField; // for mpeg2 pts inc calculation
	signed char mvcViewIdx;
	signed char topFieldFirst;
	char pairedFldFrm;
	char stillVOBU;
	char reSend;
	signed short mvcPairIdx;
	int qualityLevel;
	int decodingSuccess;
	int bytePosFrameStart;
	int bytePosFrameEnd;
	int ppuFbIndex;
	int video_full_range_flag; // H264 vui_parameters
	int colour_primaries; // H264 vui_parameters
	int transfer_characteristics; // H264 vui_parameters
	int matrix_coefficients; // H264 vui_parameters

	enum PICTURE_MODE picMode;
};

struct ve1_ctx;

struct ve1_ctx_ops {
	int (*queue_init)(void *priv, struct vb2_queue *src_vq,
			  struct vb2_queue *dst_vq);
	int (*reqbufs)(struct ve1_ctx *ctx, struct v4l2_requestbuffers *rb);
	int (*start_streaming)(struct ve1_ctx *ctx);
	int (*prepare_run)(struct ve1_ctx *ctx);
	int (*finish_run)(struct ve1_ctx *ctx);
	void (*run_timeout)(struct ve1_ctx *ctx);
	void (*seq_init_work)(struct work_struct *work);
	void (*seq_end_work)(struct work_struct *work);
	void (*release)(struct ve1_ctx *ctx);
};

struct ve1_ctx {
	struct ve1_dev *dev;
	struct mutex buffer_mutex;
	struct work_struct pic_run_work;
	struct work_struct seq_init_work;
	struct work_struct seq_end_work;
	const struct ve1_device *vd;
	const struct ve1_ctx_ops *ops;
	int aborting;
	int seqInited;
	int streamEnd;
	int startDecode;
	int dpbFull;
	int streamon_out;
	int streamon_cap;
	unsigned int out_vb2_q_memory; // V4L2_MEMORY_MMAP or V4L2_MEMORY_DMABUF
	u32 outbuf_sequence;
	u32 capbuf_sequence;
	struct ve1_q_data q_data[2];
	const struct ve1_codec *codec;
	enum v4l2_colorspace colorspace;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_fh fh;
	struct mutex bitstream_mutex;
	struct ve1_buf bitstream;
	unsigned long
		bsRdPtr; // whether svp or non-svp, it always store physical address
	unsigned long
		bsWrPtr; // whether svp or non-svp, it always store physical address
	struct ve1_meta frame_metas[MAX_VE1_FRAME_BUFFERS];
	struct list_head buffer_meta_list;
	spinlock_t buffer_meta_lock;
	struct list_head displayable_frame_list;
	spinlock_t displayable_frame_lock;
	int num_metas;
	int idx;

	void *filp; // struct file *file from ve1_open
	int ve1DecState;
	void *decOP;
	void *decHandle;
	void *initialInfo;
	int regFbCount;
	int framebufSize;
	void *fbAllocInfo;
	void *fbUser;
	void *decParam;
	void *secAxiUse;
	void *decCacheConfig;
	int int_reason;
	PhysicalAddress vpuRdPtr;
	PhysicalAddress vpuWrPtr;
	PhysicalAddress bufEmptyVpuWrPtr;
	void *outputInfo;
	unsigned int outputinfoSN;
	unsigned int decodedFrmNum;
	unsigned int displayFrmNum;
	unsigned int accuBsFeedBytes;
	int lastIndexFrameDecoded;
	int lastIndexFrameDisplay;
	PhysicalAddress lastDisplayFrmBufY;
	void *displayFrameInfo;
	unsigned int vpuBsRingRoom;

	// for merge rtk_ve1_v4l2 to rtk_vdec
	struct mutex ve1_mutex;
	struct workqueue_struct *workqueue;
	int is_svp;
	unsigned int
		currSequenceNo; // current sequence No, it increased by 1 when seq init completed in VE1_DecSeqInit()
	int seqChangeRequest;
	int seqChangeDone;
	int handle_eos_by;
	int last_frame;
	// cap_dqbuf on previous sequence maybe happened after new sequence inited,
	// save currSequenceNo when last frame reported after frame dequeued, this seqNo will be used on rtkve1_find_dpb_undequeue()
	unsigned int lastFrmReportedAfFrmDqSeqNo;
	struct vb2_v4l2_buffer *lastDqCapBuf;
	struct vb2_v4l2_buffer *lastDoneCapBuf;
	unsigned long long lastFrameTimestamp;
	struct ve1_decoded_frame frameQueue[MAX_VE1_FRAME_BUFFERS];
	unsigned long long timeTick; // 90KHz
	struct rtkve1_dpb_t dpb[VE1_ION_STRUCT_NUM];
	int bNewYuvDumpFile;	// #if defined(RTKVE1_DUMP_YUV_EN) in ve1_wrapper.c
	void *yuvDumpFile;		// #if defined(RTKVE1_DUMP_YUV_EN) in ve1_wrapper.c
	unsigned char yuvDumpFileName[256]; // #if defined(RTKVE1_DUMP_YUV_EN) in ve1_wrapper.c

	// info
	int cntQueuePicRunWorkOk;
	int cntQueuePicRunWorkFail;
	int cntExecPicRunWork;
	int cntOutQbuf;
	int cntFrameDq;

	struct device *pdev;
	// debug
	int totIonAllocatedBytes;

#ifdef VPU_GET_CC
	// user data info and buffer to config ve1
	int userDataEnable;
	int userDataReportMode;
	int userDataBufSize;
	PhysicalAddress
		userDataBufPhysAddr; // it is an ion/dma buffer which set to ve1, ve1 will fill user data to this buffer if user data existed
	void *pUserDataBufVirtAddr;
	unsigned char *
		pUserDataSrcBuf; // ve1_get_userdata() will parse header in pUserDataBufVirtAddr and copy actual user data bytes to this pUserDataSrcBuf
	char *m_CCDecodeOrderWp
		[MPEG2_CC_REG_FRAME_MAX]; //keeping write pointer by decoder's frame order, todo
	int cc_error_count;
#endif
	int timeoutCount;
	bool bBufEmptyFlag;
	int cntCap2Dpb;
	int32_t seqHeaderSize;
	uint8_t *seqHeader;
	uint8_t *picHeader;
	bool bWaitNextField;
	bool bGotNextField;
	PhysicalAddress fldDoneVpuRp;
	bool bPostponeUpBs; // postpone update bs. set to true after update bitstream, set to false after get int_reason
	struct mutex ve1_dma_mutex;
	PhysicalAddress lastInfoFrmStart;
	PhysicalAddress lastInfoFrmEnd;
	unsigned int cntAddToList;
	unsigned int capReqBufsCnt;
	bool bFlush;
	unsigned int lastDispPOC;

	int bNewBsDumpFile;		// #if defined(RTKVE1_DUMP_BS_EN) in ve1_wrapper.c
	void *bsDumpFile;		// #if defined(RTKVE1_DUMP_BS_EN) in ve1_wrapper.c
	unsigned char bsDumpFileName[256]; // #if defined(RTKVE1_DUMP_BS_EN) ve1_wrapper.c

	int free_cap;
	int error;
};

static inline unsigned long ve1_ring_valid_data(unsigned long ring_base,
						unsigned long ring_limit,
						unsigned long ring_rp,
						unsigned long ring_wp)
{
	if (ring_wp >= ring_rp) {
		return (ring_wp - ring_rp);
	} else {
		return ((ring_limit - ring_base) - (ring_rp - ring_wp));
	}
}

static inline unsigned long ve1_ring_phys_to_virt(unsigned long phys_addr,
						  unsigned long phys_base,
						  unsigned long virt_base)
{
	return (virt_base + (phys_addr - phys_base));
}

static inline unsigned int ve1_get_bitstream_payload(struct ve1_ctx *ctx)
{
	return ve1_ring_valid_data(ctx->bitstream.paddr,
				   ctx->bitstream.paddr + ctx->bitstream.size,
				   ctx->bsRdPtr, ctx->bsWrPtr);
}

#endif /* __VE1_V4L2_H__ */
