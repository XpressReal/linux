// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
#ifndef RTKVE_COMMON_H
#define RTKVE_COMMON_H

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ctrls.h>

#include "rtkve-rpc-def.h"

#define VPU_ENC_DRV_NAME "rtkve-enc"
#define VPU_ENC_DEV_NAME "RTK Video Engine encoder"

#define RTKVE_MAX_FBS 32

enum cod_std { STD_HEVC, STD_VP9, STD_AV1, STD_MAX };

enum vpu_fmt_type { VPU_FMT_TYPE_CODEC = 0, VPU_FMT_TYPE_RAW = 1 };

enum vpu_instance_state {
	VPU_INST_STATE_NONE = 0,
	VPU_INST_STATE_OPEN = 1,
	VPU_INST_STATE_INIT_SEQ = 2,
	VPU_INST_STATE_PIC_RUN = 3,
	VPU_INST_STATE_SEEK = 4,
	VPU_INST_STATE_STOP = 5
};

enum vpu_instance_type { VPU_INST_TYPE_DEC = 0, VPU_INST_TYPE_ENC = 1 };

struct vpu_buf {
	size_t size;
	dma_addr_t daddr;
	void *vaddr;
};

struct stateless_hevc_info {
	const struct v4l2_ctrl_hevc_sps *sps;
	const struct v4l2_ctrl_hevc_pps *pps;
	const struct v4l2_ctrl_hevc_scaling_matrix *sm;
	const struct v4l2_ctrl_hevc_slice_params *spram;
	const struct v4l2_ctrl_hevc_decode_params *dpram;
	uint32_t spram_cnt;
};

struct stateless_vp9_info {
	const struct v4l2_ctrl_vp9_frame *frame;
	const struct v4l2_ctrl_vp9_compressed_hdr *cmprs_hdr;
};

struct stateless_av1_info {
	const struct v4l2_ctrl_av1_sequence *seq;
	const struct v4l2_ctrl_av1_frame *frm;
	const struct v4l2_ctrl_av1_film_grain *film_grain;
	const struct v4l2_ctrl_av1_tile_group_entry *tge;
	uint32_t tge_cnt;
};

struct vpu_rect {
	u32 left; /* A horizontal pixel offset of top-left corner of rectangle from (0, 0) */
	u32 top; /* A vertical pixel offset of top-left corner of rectangle from (0, 0) */
	u32 right; /* A horizontal pixel offset of bottom-right corner of rectangle from (0, 0) */
	u32 bottom; /* A vertical pixel offset of bottom-right corner of rectangle from (0, 0) */
};

struct frame_buffer {
	dma_addr_t buf_y;
	dma_addr_t buf_cb;
	dma_addr_t buf_cr;
	dma_addr_t buf_y_bot;
	dma_addr_t buf_cb_bot;
	dma_addr_t buf_cr_bot;
	unsigned int stride; /* A horizontal stride for given frame buffer */
	unsigned int width; /* A width for given frame buffer */
	unsigned int height; /* A height for given frame buffer */
	unsigned int sequence_no;
	int index;
	u32 luma_bitdepth : 4;
	u32 chroma_bitdepth : 4;
	u32 chroma_format_idc : 2;
};

struct enc_output_info {
	int frm_size;
	uint64_t timestamp;
	unsigned char keyfrm;
	uint32_t picture_num;
};

struct enc_param {
	uint32_t framerate_num;
	uint32_t framerate_denom;
	uint32_t framerate;
	uint32_t bitrate;
	uint32_t bitrate_mode;
	uint32_t gop_size;
	uint32_t header_with_frm;
	bool force_key_frm;
};

struct dec_param {
	dma_addr_t
		buf_addr_y; /**< It specifies the Y buffer address of stateless decoding. */
	dma_addr_t
		buf_addr_c; /**< It specifies the Cb buffer address of stateless decoding. */
	unsigned int wPtr; //PTS_INFO
	unsigned int PTSH; //PTS_INFO
	unsigned int PTSL;
	unsigned int pre_PTSH; //PTS_INFO
	unsigned int pre_PTSL;
	u32 bs_len;
#if 0
	struct stateless_hevc_info	hevc;
	struct stateless_vp9_info	vp9;
	struct stateless_av1_info	av1;
#endif
	//struct AV1_V4L2_CTRL_INFO av1;
};

struct vpu_instance {
	struct list_head list;
	struct v4l2_fh v4l2_fh;
	struct v4l2_ctrl_handler v4l2_ctrl_hdl;
	struct vpu_device *dev;

	struct v4l2_pix_format_mplane src_fmt;
	struct v4l2_pix_format_mplane dst_fmt;
	struct v4l2_rect crop;
	enum v4l2_colorspace colorspace;
	enum v4l2_xfer_func xfer_func;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;

	enum vpu_instance_state state;
	enum vpu_instance_type type;

	struct enc_param enc_params;

	uint32_t id;
	uint32_t queued_src_buf_num;
	uint32_t queued_dst_buf_num;
	bool nv21;
	bool eos;
	bool is_10bit_bitstream;
	uint32_t feed_cnt;

	struct vpu_handler *enc_hdl;

	u8 frame_addr[RTKVE_MAX_FBS][3][4];
	struct task_struct *input_thread;
	wait_queue_head_t input_waitq;
	wait_queue_head_t output_waitq;
	struct work_struct encode_work;

	struct list_head srcbuf_list;
	struct list_head dstbuf_list;
	spinlock_t srcbuf_lock;
	spinlock_t dstbuf_lock;

	//stateful
	bool initialized;
	bool is_bs_error;
	bool is_decoder_error;
};

struct rtkve_match_data {
	const struct vpu_format *(*find_vpu_fmt)(unsigned int v4l2_pix_fmt,
						 enum vpu_fmt_type type);
	const struct vpu_format *(*find_vpu_fmt_by_idx)(unsigned int idx,
							enum vpu_fmt_type type);
	int (*ctrls_setup)(struct vpu_instance *inst);
	int (*queue_init)(void *priv, struct vb2_queue *src_vq,
			  struct vb2_queue *dst_vq);
	//void (*dev_run_work)(struct vpu_instance *inst);
	void (*dev_run_work)(struct work_struct *work);
	void (*stop_decode)(struct vpu_instance *inst);
	int (*create_instance)(struct vpu_instance *inst);
	void (*destroy_instance)(struct vpu_instance *inst);

	bool is_stateless;
};

struct vpu_device {
	struct device *dev;
	struct v4l2_device v4l2_dev;
	struct media_device mdev;
	struct v4l2_m2m_dev *m2m_dev;
	struct list_head instances;
	const struct rtkve_match_data *rtkve_mdata;
	struct video_device *video_dev_enc;
	struct mutex dev_lock; /* lock for the src, dst v4l2 queues */
	struct mutex hw_lock; /* lock hw configurations */
	struct kthread_work work;
	struct kthread_worker *worker;
	struct workqueue_struct *encode_workqueue;
};

struct vpu_buf *rtkve_allocate_dma_memory(struct device *dev, size_t size);
void rtkve_free_dma_memory(struct device *dev, struct vpu_buf *vb);
uint64_t htonll(long long val);
void word_endian_convert(uint8_t *dst, uint8_t *src, int len);
void dword_endian_convert(uint8_t *dst, uint8_t *src, int len);
void qword_endian_convert(uint8_t *dst, uint8_t *src, int len);
#endif
