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
#ifndef RTKVE_VPU_H
#define RTKVE_VPU_H

#include <media/v4l2-ctrls.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>

#include "rtkve-common.h"

#define HEVC_MIN_ENC_PIC_WIDTH 224U
#define HEVC_MIN_ENC_PIC_HEIGHT 96U
#define HEVC_MAX_ENC_PIC_WIDTH 1920U
#define HEVC_MAX_ENC_PIC_HEIGHT 1920U

#define RAW_MIN_ENC_PIC_WIDTH 224U
#define RAW_MIN_ENC_PIC_HEIGHT 96U
#define RAW_MAX_ENC_PIC_WIDTH 1920U
#define RAW_MAX_ENC_PIC_HEIGHT 1920U

#define ENC_PIC_SIZE_STEP 1

#define DEFAULT_FRAMERATE_NUM 30000
#define DEFAULT_FRAMERATE_DENOM 1000
#define DEFAULT_GOP 29

struct vpu_format {
	unsigned int v4l2_pix_fmt;
	unsigned int max_width;
	unsigned int min_width;
	unsigned int max_height;
	unsigned int min_height;
	unsigned int num_planes;
};

struct vpu_buffer {
	struct v4l2_m2m_buffer v4l2_m2m_buf;
	bool consumed;
	bool referenced;
	struct list_head list;
};

struct stateless_info {
	struct vb2_v4l2_buffer *src;
	struct vb2_v4l2_buffer *dst;
	union {
		struct stateless_hevc_info hevc;
		struct stateless_vp9_info vp9;
		struct stateless_av1_info av1;
	};
};

extern struct v4l2_ioctl_ops rtkve_enc_ioctl_ops;
extern const struct rtkve_match_data rtkve3_data_stateful;

static inline struct vpu_instance *rtkve_to_vpu_inst(struct v4l2_fh *vfh)
{
	return container_of(vfh, struct vpu_instance, v4l2_fh);
}

static inline struct vpu_buffer *rtkve_to_vpu_buf(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct vpu_buffer, v4l2_m2m_buf.vb);
}

void rtkve_set_default_format(struct vpu_instance *inst,
			      struct v4l2_pix_format_mplane *src_fmt,
			      struct v4l2_pix_format_mplane *dst_fmt);
int rtkve_enc_init_m2m_dev(struct vpu_device *dev);
#endif
