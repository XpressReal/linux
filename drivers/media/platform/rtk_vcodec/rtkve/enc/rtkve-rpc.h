// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Realtek video decoder v4l2 driver
 *
 * Copyright (c) 2024 Realtek Semiconductor Corp. All rights reserved.
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-Proprietary
 *
 * This software component is confidential and proprietary to Realtek
 * Semiconductor Corp. Disclosure, reproduction, redistribution, in whole
 * or in part, of this work and its derivatives without express permission
 * is prohibited.
 */
#ifndef RTKVE_RPC_H
#define RTKVE_RPC_H

#include "rtkve-rpc-def.h"
#include "rtkve-common.h"

struct rtkve_ringbuf_t {
	struct mutex lock;
	volatile struct _tagRingBufferHeader *pRBH;
	uint32_t phyaddr;
	uint32_t phyaddr_hdr;
	uint32_t limit;
	uint32_t size;
	uint8_t *virtaddr;
	uint8_t *virtaddr_hdr;
	void *hdr_hdl;
	void *buf_hdl;
};

struct rtkve_buflock_t {
	volatile uint8_t *buflock_va;
	uint32_t buflock_pa;
	uint32_t idx;
	bool is_used;
};

struct rtkve_pre_parsing_info {
	uint32_t width;
	uint32_t height;
	uint32_t min_reqbuf;
	uint32_t bit_depth;
	uint32_t ddr_width;
	uint32_t ddr_height;
};

struct vpu_flash_info {
	struct rtkve_ringbuf_t mesg_rb;
	uint32_t outputRingIdx;
	uintptr_t *frame;
};

struct vpu_handler {
	struct rtk_krpc_ept_info *vcpu_ept_info;
	struct mutex lock;
	uint32_t inst_type;
	uint32_t inst_id;
	struct device *dev;
	int type;
	bool is_running;
	struct rtkve_ringbuf_t stream_rb;
	struct rtkve_ringbuf_t inband_rb;
	struct rtkve_ringbuf_t mesg_rb;
	struct vpu_buf *refbuf;
};

int rtkve_rpc_open(struct vpu_handler *hndl, int type);
int rtkve_rpc_close(struct vpu_handler *hndl);
int rtkve_rpc_set_srcfmt(struct vpu_handler *hndl, enum YUV_FMT fmt);
int rtkve_rpc_set_encfmt(struct vpu_handler *hndl, enum VIDEO_STREAM_TYPE fmt);
int rtkve_rpc_set_resolution(struct vpu_handler *hndl, uint32_t in_width,
			   uint32_t in_height, uint32_t out_width, uint32_t out_height);
int rtkve_rpc_set_GOPStruct(struct vpu_handler *hndl, uint32_t M, uint32_t N);
int rtkve_rpc_set_profile(struct vpu_handler *hndl,
			  enum VIDEO_ENC_PROFILE profile);
int rtkve_rpc_set_bitrate(struct vpu_handler *hndl, uint32_t mode,
			  uint32_t bitrate);
int rtkve_rpc_set_frmrate(struct vpu_handler *hndl, uint32_t frmrate);
int rtkve_rpc_req_keyfrm(struct vpu_handler *hndl);
int rtkve_rpc_start_record(struct vpu_handler *hndl);
int rtkve_rpc_stop_record(struct vpu_handler *hndl);
int rtkve_rpc_run(struct vpu_handler *hndl);
int rtkve_rpc_pause(struct vpu_handler *hndl);
int rtkve_rpc_stop(struct vpu_handler *hndl);
int rtkve_rpc_create_encoder(struct vpu_instance *inst);
int rtkve_rpc_destroy_encoder(struct vpu_instance *inst);

int rtkve_inband_pts(struct vpu_handler *hndl, uint32_t wptr, uint64_t pts);
int rtkve_inband_raw_input(struct vpu_handler *hndl, uint32_t luma_addr,
			   uint32_t luma_size, uint32_t chroma_addr,
			   uint32_t chroma_size, int64_t pts);
int rtkve_inband_set_ref_buffer(struct vpu_handler *hndl);
int rtkve_inband_release_ref_buffer(struct vpu_handler *hndl);
int rtkve_inband_set_eos(struct vpu_handler *hndl);

int rtkve_write_rb(struct rtkve_ringbuf_t *ringbuf, int type, uint8_t *buf,
		   int size);

#endif
