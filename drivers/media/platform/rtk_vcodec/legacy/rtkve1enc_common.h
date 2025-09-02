#ifndef __RTKVE1_ENC_COMMON_H__
#define __RTKVE1_ENC_COMMON_H__

#include <linux/debugfs.h>
#include <media/v4l2-device.h> // for struct v4l2_device, struct video_device
#include <media/v4l2-ctrls.h> // for struct v4l2_ctrl_handler
#include <media/v4l2-fh.h> // for struct v4l2_fh
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-event.h>
#include "drv_if.h"
#include "ve1.h"

#define RTKVE1_ENC_DEV_NAME "rtkve1-enc"

#define RTKVE1_CMD_TRAFFIC 0x3020

//#define VDI_LITTLE_ENDIAN 0

#define VE1_BWB_ENABLE 1

#define CODE_BUF_SIZE (248 * 1024)
#define TEMP_BUF_SIZE (204 * 1024)
#define WORK_BUF_SIZE (80 * 1024)
#define PARA_BUF_SIZE (10 * 1024)
#define BS_BUF_SIZE (4 * 1024)
#define RTKVE1_ENC_HEADER_TEMP_BUF_SIZE 256

#define RTKVE1_ENC_TIMEOUT 1000 /* ms */
#define RTKVE1_MAX_FRAME_BUFFERS 32

#define	HEADER_H264_SPS	0
#define	HEADER_H264_PPS	1

// COD_STD
enum { HEVC_DEC = 0,
       AVC_DEC = 0,
       VC1_DEC = 1,
       HEVC_ENC = 1,
       MP2_DEC = 2,
       MP4_DEC = 3,
       DV3_DEC = 3,
       RV_DEC = 4,
       AVS_DEC = 5,
       VPX_DEC = 7,
       MAX_DEC = 7,
       AVC_ENC = 8,
       MP4_ENC = 11,
       MAX_CODECS,
};

enum {
	INT_BIT_INIT = 0,
	INT_BIT_SEQ_INIT = 1,
	INT_BIT_SEQ_END = 2,
	INT_BIT_PIC_RUN = 3,
	INT_BIT_FRAMEBUF_SET = 4,
	INT_BIT_ENC_HEADER = 5,
	INT_BIT_DEC_PARA_SET = 7,
	INT_BIT_DEC_BUF_FLUSH = 8,
	INT_BIT_USERDATA = 9,
	INT_BIT_DEC_FIELD = 10,
#ifdef SUPPORT_CDB
	INT_BIT_DEBUFFER = 12,
#endif
	INT_BIT_DEC_MB_ROWS = 13,
	INT_BIT_BIT_BUF_EMPTY = 14,
	INT_BIT_BIT_BUF_FULL = 15
};

// BIT_RUN command
enum { DEC_SEQ_INIT = 1,
       ENC_SEQ_INIT = 1,
       DEC_SEQ_END = 2,
       ENC_SEQ_END = 2,
       PIC_RUN = 3,
       SET_FRAME_BUF = 4,
       ENCODE_HEADER = 5,
       ENC_PARA_SET = 6,
       DEC_PARA_SET = 7,
       DEC_BUF_FLUSH = 8,
       RC_CHANGE_PARAMETER = 9,
       VPU_SLEEP = 10,
       VPU_WAKE = 11,
       ENC_ROI_INIT = 12,
       FIRMWARE_GET = 0xf };

enum sw_reset_mode {
	SW_RESET_SAFETY, /**< It resets VPU in safe way. It waits until pending bus transaction is completed and then perform reset. */
	SW_RESET_FORCE, /**< It forces to reset VPU without waiting pending bus transaction to be completed. It is used for immediate termination such as system off. */
	SW_RESET_ON_BOOT /**< This is the default reset mode that is executed since system booting.  This mode is actually executed in VPU_Init(), so does not have to be used independently. */
};

#define H264_MIN_ENC_PIC_WIDTH 96U
#define H264_MIN_ENC_PIC_HEIGHT 16U
#define H264_MAX_ENC_PIC_WIDTH 4096U
#define H264_MAX_ENC_PIC_HEIGHT 2304U

#define RAW_MIN_ENC_PIC_WIDTH 96U
#define RAW_MIN_ENC_PIC_HEIGHT 16U
#define RAW_MAX_ENC_PIC_WIDTH 4096U
#define RAW_MAX_ENC_PIC_HEIGHT 2304U

#define ENC_PIC_SIZE_STEP 1

#define DEFAULT_FRAMERATE_NUM 30000
#define DEFAULT_FRAMERATE_DENOM 1000
#define DEFAULT_GOP 30
#define DEFAULT_I_FRAME_QP -1

#define MIN_BITRATE (64 * 1024)
#define MAX_BITRATE (50000 * 1024)
#define DEF_BITRATE (20000 * 1024)

struct vpu_format {
	unsigned int v4l2_pix_fmt;
	unsigned int max_width;
	unsigned int min_width;
	unsigned int max_height;
	unsigned int min_height;
	unsigned int num_planes;
};

enum vpu_fmt_type { VPU_FMT_TYPE_CODEC = 0, VPU_FMT_TYPE_RAW = 1 };

enum rtkve1_enc_state {
	RTK_VE1_STATE_ENC_IDLE = 0,
	RTK_VE1_STATE_ENC_SEQ_INIT,
	RTK_VE1_STATE_ENC_REG_FBS,
	RTK_VE1_STATE_ENC_HEADER,
	RTK_VE1_STATE_ENC_PIC,
	RTK_VE1_STATE_ENC_SEQ_END,
	RTK_VE1_STATE_ENC_TIMEOUT
};

struct rtkve1enc_params {
	u32 framerate_num;
	u32 framerate_denom;
	u32 framerate;
	u32 bitrate;
	u32 bitrate_mode;
	u32 gop_size;
	u32 header_with_frm;
	u32 force_key_frm;
	u32 h264_profile_idc;
	u32 h264_level_idc;
	s32 intra_qp;
	u32 transform_8x8_mode;
	u32 field_flag;
	u32 field_ref_mode;
	u32 entropy_coding_mode;
	u32 s_ctrl_level_value;
};

struct rtkve1_buf {
	void *vaddr;
	dma_addr_t paddr;
	u32 size;
	struct debugfs_blob_wrapper blob;
	struct dentry *dentry;
};

struct rtkve1_dev_info {
	struct rtkve1_buf codebuf;
	struct rtkve1_buf parabuf;
	struct rtkve1_buf tempbuf;
};


struct rtkve1enc_ctx {
	/* RTKDEV_FOURCC_ENC or RTKDEV_FOURCC_DEC for distinguish priv in device_run() */
	u32 rtkdev_fourcc;
	u32 inst_index;
    struct videc_dev *dev;
	struct v4l2_fh v4l2_fh;
	struct v4l2_ctrl_handler v4l2_ctrl_hdl;
	struct v4l2_pix_format_mplane src_fmt;
	struct v4l2_pix_format_mplane dst_fmt;
	const struct rtkve1_context_ops *ops;
	struct dentry *debugfs_entry;
	struct work_struct encode_work;
	struct vpudrv_buffer_t regs;

	enum v4l2_colorspace colorspace;
	enum v4l2_xfer_func xfer_func;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;

	u32 src_buf_size;
	u32 src_buf_width;
	u32 src_buf_height;

	u32 codec_mode;
	struct rtkve1enc_params enc_params;
	struct rtkve1_buf bitstream;
	struct rtkve1_buf workbuf;
	struct rtkve1_buf framebuf[RTKVE1_MAX_FRAME_BUFFERS];
	u8 frame_addr[RTKVE1_MAX_FRAME_BUFFERS][3][4];

	enum rtkve1_enc_state enc_state;
	int seq_init_done;
	int reg_fbs_done;
	int enc_header_done;
	int enc_pic_done;
	int seq_end_done;
	int stopping;
	bool eos;

	u32 rdPtr;
	u32 wrPtr;
	u32 enc_min_fb_num;
	u32 queued_src_buf_num;
	u32 queued_dst_buf_num;

	void *enc_hdr_buf;
	u32 enc_hdr_buf_size;
	u32 enc_hdr_bytes;

	int outbuf_new_file;
	void *outbuf_fp;
	unsigned char outbuf_file_name[256];
	int enc_bs_new_file;
	void *enc_bs_fp;
	unsigned char enc_bs_file_name[256];
};

struct rtkve1_context_ops {
	const struct vpu_format *(*find_vpu_fmt)(unsigned int v4l2_pix_fmt,
		enum vpu_fmt_type type);
	const struct vpu_format *(*find_vpu_fmt_by_idx)(unsigned int idx,
		enum vpu_fmt_type type);
	int (*queue_init)(void *priv, struct vb2_queue *src_vq,
		struct vb2_queue *dst_vq);
	int (*ctrls_setup)(struct rtkve1enc_ctx *inst);
	int (*reqbufs)(struct rtkve1enc_ctx *ctx, struct v4l2_requestbuffers *rb);
	int (*decide_state)(struct rtkve1enc_ctx *ctx, enum rtkve1_enc_state *new_state);
	int (*seq_init)(struct rtkve1enc_ctx *ctx);
	int (*reg_fbs)(struct rtkve1enc_ctx *ctx);
	int (*enc_header)(struct rtkve1enc_ctx *ctx);
	int (*enc_pic)(struct rtkve1enc_ctx *ctx);
	void (*seq_end)(struct rtkve1enc_ctx *ctx);
	void (*run_timeout)(struct rtkve1enc_ctx *ctx);
};

void rtkve_set_default_format(struct rtkve1enc_ctx *inst,
		struct v4l2_pix_format_mplane *src_fmt,
		struct v4l2_pix_format_mplane *dst_fmt);

extern struct v4l2_ioctl_ops rtkve1enc_ioctl_ops;
extern const struct rtkve1_context_ops rtkve1enc_ops;

static inline struct rtkve1enc_ctx *v4l2fh_to_ctx(struct v4l2_fh *vfh)
{
	return container_of(vfh, struct rtkve1enc_ctx, v4l2_fh);
}

int rtkve1_md5_hash(unsigned char *result, int resultLen, char *data, int dataLen);
int rtkve1_alloc_dma_memory(struct videc_dev *dev, struct rtkve1_buf *buf,
        size_t size, const char *name, struct dentry *parent);
void rtkve1_free_dma_memory(struct videc_dev *dev,
        struct rtkve1_buf *buf, const char *name);
void rtkve1_parabuf_write(struct rtkve1enc_ctx *ctx, int index, u32 value);
int rtkve1_write_memory(struct videc_dev *dev,
    void *vaddr, size_t bufsize,
    size_t offset, u8 *data, int len, int endian);
int rtkve1_read_memory(struct videc_dev *dev,
    void *vaddr, size_t bufsize,
    size_t offset, u8 *data, int len, int endian);
void rtkve1_reg_writel(struct videc_dev *dev, unsigned int addr,
		     unsigned int data);
unsigned int rtkve1_reg_readl(struct videc_dev *dev, u32 addr);
void rtkve1_issue_command(struct rtkve1enc_ctx *ctx, u32 cmd);
int rtkve1_wait_interrupt(struct rtkve1enc_ctx *ctx, unsigned int timeout);
void rtkve1_clear_interrupt(struct rtkve1enc_ctx *ctx);
int rtkve1_wait_busy(struct videc_dev *dev, unsigned int addr);
int rtkve1_initialize(struct rtkve1enc_ctx *ctx, struct videc_dev *dev);
int rtkve1_finalize(struct videc_dev *dev);

#endif // #define __RTKVE1_ENC_COMMON_H__