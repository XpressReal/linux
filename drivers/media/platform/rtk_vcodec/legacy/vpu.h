/*
 * Realtek video decoder v4l2 driver
 *
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#ifndef __VPU_H__
#define __VPU_H__
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>
#include "video_engine.h"

#define PREPEND_METADATA
#ifdef PREPEND_METADATA
#define METADATA_OFFSET (2048)
#endif

struct vpu_misc {
	uint32_t VideoEngine;
	uint32_t bufcnt;
	uint32_t ori_width;
	uint32_t ori_height;
	uint32_t max_resolution;
};

struct vpu_fmt {
	struct v4l2_format spec;
	struct v4l2_frmsize_stepwise frmsize;
	struct vpu_misc misc;
};

struct vpu_ctx {
	struct vpu_fmt out_fmt, cap_fmt;
	struct v4l2_rect rect;
	struct task_struct *thread_out, *thread_cap;
	int thread_out_interval, thread_cap_interval; // set in vpu_alloc_context(), no need to reset when vpu_stop_streaming
	uint32_t seq_out, seq_cap;
	struct mutex vpu_mutex; // init in vpu_alloc_context(), can't set to NULL when vpu_stop_streaming
	spinlock_t vpu_spin_lock; // init in vpu_alloc_context(), can't set to NULL when vpu_stop_streaming
	wait_queue_head_t vpu_out_waitq; // init in vpu_alloc_context(), can't set to NULL when vpu_stop_streaming
	wait_queue_head_t vpu_cap_waitq; // init in vpu_alloc_context(), can't set to NULL when vpu_stop_streaming

	/* video engine operations */
	struct veng_ops *veng_ops; // set in vpu_s_fmt_out()
	struct veng_ops *ve1_ops; // set in module_init() of ve1, can't set to NULL when vpu_stop_streaming
	struct veng_ops *ve2_ops; // set in module_init() of ve2, can't set to NULL when vpu_stop_streaming

	int is_cap_started, is_out_started;
	uint32_t memory_out, memory_cap;

	bool stop_cmd;
	bool last_buf_done;
	int cap_retry_cnt;
	uint64_t out_q_cnt;
	struct completion bs_parsing_comp; // init in vpu_alloc_context(), it should reinit_completion() when vpu_stop_streaming
	bool parse_header_done;
	uint32_t bit_depth;
	unsigned int ddr_width;
	unsigned int ddr_height;
	bool is_bs_error;
	bool is_decoder_error;

	int bNewOutbufDumpFile;		// #if defined(RTKVPU_DUMP_BS_EN) in vpu.c
	void *outbufDumpFile;		// #if defined(RTKVPU_DUMP_BS_EN) in vpu.c
	unsigned char outbufDumpFileName[256]; // #if defined(RTKVPU_DUMP_BS_EN) in vpu.c
	int bNewCapbufDumpFile;		// #if defined(RTKVPU_DUMP_BS_EN) in vpu.c
	void *capbufDumpFile;		// #if defined(RTKVPU_DUMP_BS_EN) in vpu.c
	unsigned char capbufDumpFileName[256]; // #if defined(RTKVPU_DUMP_BS_EN) in vpu.c
};

struct vpu_fmt_ops {
	int (*vpu_enum_fmt_cap)(struct v4l2_fmtdesc *f);
	int (*vpu_enum_fmt_out)(struct v4l2_fmtdesc *f);
	int (*vpu_g_fmt)(struct v4l2_fh *fh, struct v4l2_format *f);
	int (*vpu_try_fmt_cap)(struct v4l2_fh *fh, struct v4l2_format *f);
	int (*vpu_try_fmt_out)(struct v4l2_fh *fh, struct v4l2_format *f);
	int (*vpu_s_fmt_cap)(struct v4l2_fh *fh, struct v4l2_format *f);
	int (*vpu_s_fmt_out)(struct v4l2_fh *fh, struct v4l2_format *f);
	int (*vpu_queue_info)(struct vb2_queue *vq, int *bufcnt,
			      unsigned int *nplanes, int *sizeimage);
	int (*vpu_start_streaming)(struct vb2_queue *q, unsigned count);
	int (*vpu_stop_streaming)(struct vb2_queue *q);
	int (*vpu_qbuf)(struct v4l2_fh *fh, struct vb2_buffer *vb);
	void (*vpu_buf_finish)(struct vb2_buffer *vb);
	int (*vpu_abort)(void *priv, int type);
	int (*vpu_g_crop)(void *fh, struct v4l2_rect *rect);
	int (*vpu_stop_cmd)(void *fh);
	int (*vpu_free_capture)(void *fh);
	int (*vpu_reset_resource)(void *fh);
	int (*vpu_start_cmd)(void *fh);
};

const struct vpu_fmt_ops *get_vpu_fmt_ops(void);

/*
 * struct veng_ops - video engine operations
 */
struct veng_ops {
	int (*ve_start_streaming)(struct vb2_queue *q, uint32_t count,
				  int pixelformat);
	int (*ve_stop_streaming)(struct vb2_queue *q);
	int (*ve_out_qbuf)(void *fh, uint8_t *buf, uint32_t len, uint64_t timestamp,
			   uint32_t sequence);
	int (*ve_cap_qbuf)(void *fh, struct vb2_buffer *vb);
	int (*ve_cap_dqbuf)(void *fh, uint8_t *buf, uint64_t *timestamp,
			    struct vb2_v4l2_buffer **disp_buf);
	int (*ve_abort)(void *ctx, int type);
	void *(*ve_alloc_context)(void *fh);
	void (*ve_free_context)(void *ctx);
	void (*ve_free_capture)(void *ctx);
	int (*ve_stop_cmd)(void *fh, int pixelformat);
	void (*ve_get_info)(void *fh, bool *eos, bool *no_frame);
	int (*ve_get_undq_dispFrm_cnt)(void *fh);
	int (*ve_out_pre_parse)(void *fh, struct vb2_buffer *vb,
				uint32_t *width, uint32_t *height,
				uint32_t *minBufCnt, uint32_t *bitrate);
	int (*ve_get_request_buf_info)(void *fh,
				uint32_t *ddr_width, uint32_t *ddr_height);
	int (*ve_start_cmd)(void *fh);
};

/**
* @brief  Update the rect resolution
* @param fh [input] struct v4l2_fh
* @param rect [input] struct v4l2_rect
*/
int vpu_update_rect(void *fh, struct v4l2_rect *rect);

/**
* @brief Get the original v4l2_pix_format of cap in vpu_ctx
* @param fh [input] struct v4l2_fh
* @param cap_fmt [output] struct v4l2_pix_format, copied from the original v4l2_pix_format of cap in vpu_ctx
*/
int vpu_get_cap_fmt(void *fh, void *cap_fmt);
/**
* @brief Update the v4l2_pix_format of cap in vpu_ctx
* @param fh [input] struct v4l2_fh
* @param cap_fmt [input] struct v4l2_pix_format, it will be copied to the v4l2_pix_format of cap in vpu_ctx
*/
int vpu_update_cap_fmt(void *fh, void *cap_fmt);
/**
 * @brief Notify source resolution change event
 * @param fh [input] struct v4l2_fh
 */
void vpu_notify_event_resolution_change(void *fh);
void vpu_update_resolution_change(void *fh, uint32_t width, uint32_t height,
			      uint32_t ddr_width, uint32_t ddr_height, uint32_t bit_depth,
			      uint32_t min_reqbuf);
int vpu_check_sub_res_chg(void *fh);
void *vpu_alloc_context(void);
void vpu_free_context(void *ctx);
void *vpu_get_frmsize(uint32_t pixel_format);
int vpu_ve_register(int index, struct veng_ops *ops);
void vpu_ve_unregister(int index);
#ifdef ENABLE_SHOW_VIDEO_INFO
int vpu_get_video_info(char hasVideo, char *buf);
int vpu_keep_fm_info(int width, int height);
#endif // #ifdef ENABLE_SHOW_VIDEO_INFO
#endif
