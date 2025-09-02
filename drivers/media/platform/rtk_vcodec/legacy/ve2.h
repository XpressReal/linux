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
#ifndef __VE2_H__
#define __VE2_H__
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>

struct ve2_ops {
	int (*ve2_start_streaming)(int type, uint32_t count, uint32_t bufcnt,
				   int pixelformat);
	int (*ve2_stop_streaming)(int type);
	int (*ve2_out_qbuf)(uint8_t *buf, uint32_t len, uint64_t pts,
			    uint32_t sequence);
	int (*ve2_cap_qbuf)(void *fh, struct vb2_buffer *vb);
	int (*ve2_cap_dqbuf)(void *fh, uint8_t *buf, uint64_t *pts,
			     struct vb2_v4l2_buffer **disp_buf);
	int (*ve2_abort)(int type);
};

#endif
