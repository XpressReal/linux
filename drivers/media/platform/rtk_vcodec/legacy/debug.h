/*
 * Realtek video decoder v4l2 driver
 *
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <linux/videodev2.h>
#include <linux/printk.h>

/*
 * The following categories are defined:
 *
 * INPUT: Used for gerneral input debug (output type)
 *
 * OUTPUT: Used for gerneral output debug (capture type)
 *
 * Enabling verbose debug messages is done through the rtkvdec.debug parameter,
 * each category being enabled by a bit.
 *
 * rtkvdec.debug=0x1 will enable INPUT messages
 * rtkvdec.debug=0x2 will enable OUTPUT messages
 * rtkvdec.debug=0x3 will enable INPUT and OUTPUT messages
 * ...
 *
 * An interesting feature is that it's possible to enable verbose logging at
 * run-time by echoing the debug value in its sysfs node:
 *   # echo 0xf > /sys/module/rtkvdec/parameters/debug
 */
#define VPU_DBG_NONE 0x00
#define VPU_DBG_INPUT (1 << 0) // enable INPUT messages
#define VPU_DBG_OUTPUT (1 << 1) // enable OUTPUT messages
// ve1 log will use bit 16 ~ bit 31 of rtkvdec.debug for different category
#define VPU_DBG_VE1_ALL (1 << 16) // enable all ve1 log
#define VPU_DBG_VE1_UP_BS (1 << 17) // enable ve1 update bitstream buffer log
#define VPU_DBG_VE1_DEC (1 << 18) // enable ve1 decode log
#define VPU_DBG_VE1_DIS (1 << 19) // enable ve1 display log

extern __printf(3, 4) void vpu_printk(const char *level, unsigned int category,
				      const char *format, ...);

#define vpu_err(fmt, ...) vpu_printk(KERN_ERR, VPU_DBG_NONE, fmt, ##__VA_ARGS__)

#define vpu_warn(fmt, ...)                                                     \
	vpu_printk(KERN_WARNING, VPU_DBG_NONE, fmt, ##__VA_ARGS__)

#define vpu_info(fmt, ...)                                                     \
	vpu_printk(KERN_INFO, VPU_DBG_NONE, fmt, ##__VA_ARGS__)

#define vpu_input_dbg(fmt, ...)                                                \
	vpu_printk(KERN_DEBUG, VPU_DBG_INPUT, fmt, ##__VA_ARGS__)

#define vpu_output_dbg(fmt, ...)                                               \
	vpu_printk(KERN_DEBUG, VPU_DBG_OUTPUT, fmt, ##__VA_ARGS__)

#define V4L2_TYPE_TO_STR(type) (V4L2_TYPE_IS_OUTPUT(type) ? "out" : "cap")

#endif