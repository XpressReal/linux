/*
 * Realtek video decoder v4l2 driver
 *
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */
#ifndef __DRV_IF_H__
#define __DRV_IF_H__

#include <linux/debugfs.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#define xstr(s) str(s)
#define str(s) #s

#define RTK_V4L2_SET_SECURE (V4L2_CID_USER_REALTEK_BASE + 0)
#define RTK_V4L2_DEC_PARMS_CONFIG (V4L2_CID_USER_REALTEK_BASE + 1)

//Attentation!This value should not be included in v4l2_ctrl_type
#define V4L2_CTRL_TYPE_RTK_DEC_PARAM 0x9000

struct videc_dev {
	struct v4l2_device v4l2_dev;
	struct video_device video_dev[2];

	atomic_t num_inst;
	struct mutex dev_mutex;
	struct mutex ve1_hw_mutex;
	spinlock_t irqlock;

	struct v4l2_m2m_dev *m2m_dev;
	struct device *dev;
	struct dentry *debugfs_root;

	uint8_t en_enhance;

	void *ve1_devinfo; /* struct rtkve1_dev_info */
	void __iomem *ve1_register;
	struct workqueue_struct *encode_workqueue;
	u32 ve1_instance_nums; /* total instance number including ve1 decoder and encoder */
};

struct rtk_dec_params {
	//Colorimetry
	uint32_t matrix_coefficients;
	uint32_t range;
	uint32_t transfer_characteristics;
	uint32_t primaries;

	uint8_t en_adaptive_playback;
	uint8_t en_pts_reorder;
	uint8_t en_enhance;
};

struct videc_params {
	uint8_t is_secure;

	struct v4l2_ctrl_hdr10_mastering_display mastering;
	struct v4l2_ctrl_hdr10_cll_info cll;
	struct rtk_dec_params dec_params;
};

struct videc_ctx {
	/* RTKDEV_FOURCC_ENC or RTKDEV_FOURCC_DEC for distinguish priv in device_run() */
	u32 rtkdev_fourcc;
	struct v4l2_fh fh;
	struct videc_dev *dev;

	void *file; /* struct file */
	void *vpu_ctx; /* context of vpu */
	void *ve_ctx; /* context of video engine */

	int reqbuf_out;
	int reqbuf_cap;
	bool is_sub_res_chg;
	bool has_stream_on;
	bool params_update;

	struct v4l2_ctrl_handler ctrl_hdl;
	struct videc_params params;
};
#ifdef VPU_GET_CC
int cc_data_channel_init(void);
void cc_data_channel_exit(void);
void cc_data_channel_send(char *message, int total_size, int pid);
bool cc_isCCReaderReady(void);
bool cc_isCCInit(void);
__u32 cc_getCCReaderPid(void);
#endif
#endif
