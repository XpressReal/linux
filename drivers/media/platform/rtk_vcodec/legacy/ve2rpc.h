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
#ifndef __VE2RPC_H__
#define __VE2RPC_H__
#include <linux/videodev2.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <soc/realtek/rtk-krpc-agent.h>
#include "ve2rpc_cmd.h"

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 10, 116)
struct dma_buf *ext_rtk_ion_alloc(size_t len, unsigned int heap_type_mask,
				  unsigned int flags);
int ext_rtk_ion_close_fd(struct files_struct *files, unsigned fd);

#define ion_alloc ext_rtk_ion_alloc
#endif

#define COLOR_MATRIX_COEF_DEFAULT (-1)
#define MAX_VE2_FRAME_BUFFERS 20
#define VE2_MAX_DPB_NUM 64

typedef enum {
	VE2RPC_MEMORY_NONE = 0,
	VE2RPC_MEMORY_CMA,
	VE2RPC_MEMORY_CMA_DECRYPT_BUF,
	VE2RPC_MEMORY_CPB,
	VE2RPC_MEMORY_CPB_DECRYPT_BUF,
	VE2RPC_MEMORY_INBAND
} VE2RPC_MEMORY_TYPE;

struct _tagRingBufferHeader {
	volatile uint32_t magic; //Magic number
	volatile uint32_t beginAddr;
	volatile uint32_t size;
	volatile uint32_t
		bufferID; // RINGBUFFER_TYPE, choose a type from RINGBUFFER_TYPE
	volatile uint32_t writePtr;
	volatile uint32_t numOfReadPtr;
	volatile uint32_t reserve2; //Reserve for Red Zone
	volatile uint32_t reserve3; //Reserve for Red Zone
	volatile uint32_t readPtr[4];
	volatile int32_t fileOffset;
	volatile int32_t requestedFileOffset;
	volatile int32_t fileSize;
	volatile int32_t
		bSeekable; /* Can't be sought if data is streamed by HTTP */
};

struct ve2rpc_ion_object {
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,6,0)
	struct dma_buf_map map;
#else
	struct iosys_map map;
#endif
	dma_addr_t paddr;
	void *vaddr;
	size_t size;
};

struct ve2rpc_ringbuf_info_t {
	void *hdr_hdl;
	VE2RPC_MEMORY_TYPE buf_type;
	uint32_t buf_addr;
	uint32_t buf_size;
	void *buf_cached;
	void *buf_uncached;
	uint32_t buf_limit;

	void *buf_hdl;
	VE2RPC_MEMORY_TYPE hdr_type;
	uint32_t hdr_addr;
	uint32_t hdr_size;
	uint32_t hdr_cached;
	uint32_t hdr_uncached;
	uint32_t hdr_limit;

	VE2RPC_MEMORY_TYPE bufex_type;
	uint32_t bufex_addr;
	uint32_t bufex_size;
};

struct ve2rpc_ringbuf_t {
	struct mutex lock;

	struct ve2rpc_ringbuf_info_t rbinfo;
	volatile struct _tagRingBufferHeader *pRBH;
	uint8_t *buf_cached;
	uint8_t *buf_uncached;
	uint8_t *hdr_cached;
	uint8_t *hdr_uncached;
	uint8_t *base;
	uint8_t *limit;
	uint32_t size;
	uint32_t phyaddr;
	uint32_t phyaddr_hdr;
	uint32_t read_start;
	int dummy; /* used for atom access */
	uint8_t secure;
	uint32_t memory;
#ifdef ENABLE_TEE_DRM_FLOW
	void *teeapi_ctx;
	unsigned int teeapi_tee_session;
#endif
};

struct color_metrix {
	uint32_t matrix_coefficients;
	uint32_t range;
	uint32_t transfer_characteristics;
	uint32_t primaries;
	uint32_t max_cll;
	uint32_t max_fall;
	uint32_t primary_r_chromaticity_x;
	uint32_t primary_r_chromaticity_y;
	uint32_t primary_g_chromaticity_x;
	uint32_t primary_g_chromaticity_y;
	uint32_t primary_b_chromaticity_x;
	uint32_t primary_b_chromaticity_y;
	uint32_t whitepoint_chromaticity_x;
	uint32_t whitepoint_chromaticity_y;
	uint32_t luminance_max;
	uint32_t luminance_min;
};

struct ve2rpc_qframe_wq {
	struct list_head list;
	struct list_head tlist; //for traveling frame
	struct mutex lock;
	struct task_struct *vclient;
};

enum dpb_st {
	RTKVE2_DPB_ST_EMPTY,
	RTKVE2_DPB_ST_VALID,
	RTKVE2_DPB_ST_DQ
};

struct rtkve2_reg_dpb_t {
	uint32_t width;
	uint32_t height;
	uint32_t dpb_width;
	uint32_t dpb_height;
	uint32_t size;
	uint64_t y_phy_addr;
	uint64_t c_phy_addr;
	uint32_t bit_depth;
	void *vb2_v4l2_buf;
	uint32_t idx;
};

struct rtkve2_dpb_t {
	unsigned int size;
	unsigned int width;
	unsigned int height;
	uint32_t y_phy_addr;
	uint32_t c_phy_addr;
	struct ve2rpc_ion_object *cmprs_hdr_buf;
	void *vb2_v4l2_buf;
	uint8_t idx;
	enum dpb_st status;
};

struct rtkve2_buflock_t {
	volatile uint8_t *buflock_va;
	uint32_t buflock_pa;
	uint32_t idx;
	bool is_used;
};

struct ve2rpc {
	int type;
	struct ve2rpc_ringbuf_t main_rb;
	struct ve2rpc_ringbuf_t sub_rb;
	struct ve2rpc_ringbuf_t cc_rb;
	struct ve2rpc_ringbuf_t dpb_rb;
	uint32_t instanceType;
	uint32_t instanceID;
	struct task_struct *buflock_thread;
	wait_queue_head_t buflock_waitq;
	struct mutex buflock_mutex;
	void *buflock;
	struct rtkve2_buflock_t *buflock_info;
	bool buflock_force_quit;
	uintptr_t *frame;
	struct ve2rpc_qframe_wq qframe;
	struct mutex lock;
#ifdef REORDER_PTS
	struct mutex pts_mutex;
	struct list_head *pts_queue;
	uint64_t pre_pts;
	int lastID;
#endif
	uint32_t rpc_id;
	struct rtk_krpc_ept_info *vcpu_ept_info;
	struct v4l2_fh *fh;
	struct mutex travel_mutex;
	uint32_t outputRingIdx;
	struct device *dev;
#ifdef ENABLE_TEE_DRM_FLOW
	void *teeapi_ctx;
	unsigned int teeapi_tee_session;
#endif
	struct color_metrix col_matrix;
	VIDEO_STREAM_TYPE vType;
	struct rtkve2_dpb_t dpb[VE2_MAX_DPB_NUM];
	struct mutex dpb_mutex;
	uint32_t dpb_cnt;
	uint8_t is_secure;
	uint8_t is_adaptive_playback;
	uint8_t is_error;
	uint8_t is_pts_reorder;
	uint32_t bit_depth;
};

struct pts_queue {
	struct list_head list;
	uint64_t pts;
	uint32_t idx;
};

int ve2rpc_dqframe(struct ve2rpc *cap_hndl, void *disp_buf, uint64_t *pts,
		   bool *eos, bool *no_frame, uint32_t *no_show_frm_cnt);
int ve2rpc_qframe(struct ve2rpc *cap_hndl, dma_addr_t phy_addr,
		  uint32_t work_idx);
int ve2rpc_write_bs(struct ve2rpc *hndl, uint8_t *buf, uint32_t len,
		    uint64_t pts, uint32_t sequence);
int ve2rpc_inband_decode(struct ve2rpc *vout_hndl, DECODE_MODE mode);
int ve2rpc_inband_newseg(struct ve2rpc *vout_hndl);
int ve2rpc_pause(struct ve2rpc *hndl);
int ve2rpc_flush(struct ve2rpc *hndl);
int ve2rpc_stop(struct ve2rpc *hndl);
int ve2rpc_close(struct ve2rpc *hndl);
int ve2rpc_setRole(struct ve2rpc *hndl, VIDEO_STREAM_TYPE type);
int ve2rpc_connect(struct ve2rpc *src, struct ve2rpc *dst);
int ve2rpc_init_out_handle(struct device *dev, struct ve2rpc **handle,
			   uint8_t is_secure, struct v4l2_fh *fh);
int ve2rpc_init_cap_handle(struct device *dev, struct ve2rpc **handle,
			   uint8_t is_secure, struct v4l2_fh *fh);
int ve2rpc_uninit_handle(struct ve2rpc *hndl);
#ifdef REORDER_PTS
int ve2rpc_free_pts(struct ve2rpc *cap_hndl);
#endif
int ve2rpc_free_travel_frame(struct ve2rpc *cap_hndl);
int ve2rpc_add_travel_entry(struct ve2rpc *cap_hndl,
	uint32_t y_phy_addr, uint32_t buflock_phy_addr,
	void *vb2_v4l2_buf, uint32_t idx);
int ve2rpc_reset_buflock(struct ve2rpc *cap_hndl, bool reset_all);
int ve2rpc_run(struct ve2rpc *hndl);
int ve2rpc_get_info(struct ve2rpc *cap_hndl, uint32_t *info);
int ve2rpc_inband_eos_event(struct ve2rpc_ringbuf_t *ringbuf,
			   volatile struct _tagRingBufferHeader *pRBH,
			   unsigned int event_id);
int ve2rpc_readCcRingBuf(struct ve2rpc_ringbuf_t *prb, uint32_t length,
			   char *pcDst);
int ve2rpc_setDecoderCCBypass(struct ve2rpc *hndl, int mode);
int ve2rpc_SetRingBuffer(struct ve2rpc *hndl, struct ve2rpc_ringbuf_t *prb,
			   uint32_t bodysize, RINGBUFFER_TYPE type,
			   uint8_t is_secure);
int ve2rpc_get_decoded_frm_cnt(struct ve2rpc *hndl);
int ve2rpc_add_capbuf_to_dpb(struct ve2rpc *out_hndl, struct ve2rpc *cap_hndl,
			     struct rtkve2_reg_dpb_t dpb, bool is_cmprs);
int ve2rpc_del_capbuf_from_dpb(struct ve2rpc *out_hndl,
			   struct ve2rpc *cap_hndl);
void ve2rpc_update_dpb_st(struct ve2rpc *cap_hndl,
			   struct vb2_v4l2_buffer *vb2_v4l2_buffer, unsigned int status);
int ve2rpc_inband_add_buf(struct ve2rpc_ringbuf_t *ringbuf,
			  struct rtkve2_reg_dpb_t dpb,
			  uint32_t cmprs_hdr_lu, uint32_t cmprs_hdr_ch,
			  uint32_t cmprs_hdr_size);
int ve2rpc_inband_del_buf(struct ve2rpc_ringbuf_t *ringbuf,
			   uint32_t y_phy_addr);
int ve2rpc_set_cmprs(struct ve2rpc *hndl, uint8_t enable);
int ve2rpc_get_bs_info(struct device *dev, void *fh, uint32_t codec,
		       uint32_t size, void *buf, uint32_t *width,
		       uint32_t *height, uint32_t *ddr_width,
		       uint32_t *ddr_height, uint32_t *min_reqbuf,
		       uint32_t *bit_depth);
int ve2rpc_enable_drop_cnt(struct ve2rpc *hndl);
int ve2rpc_reset_msg_ring_rwptr(struct ve2rpc *cap_hndl);
int ve2rpc_reset_bs_ring_rwptr(struct ve2rpc *out_hndl);
void __maybe_unused dump_buflock(struct ve2rpc *cap_hndl);

#endif
