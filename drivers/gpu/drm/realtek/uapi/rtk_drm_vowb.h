/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __UAPI_RTK_DRM_VOWB__
#define __UAPI_RTK_DRM_VOWB__

#define RTK_DRM_VOWB_MAX_SRC_PIC               16

struct rtk_drm_vowb_dst_pic {
	__u32 y_offset;
	__u32 c_offset;
	__u32 w;
	__u32 h;
	__u32 format;
};

#define RTK_DRM_VOWB_FLAGS_VALID_SETUP_FLAGS     (0)
#define RTK_DRM_VOWB_FLAGS_VALID_TEARDOWN_FLAGS  (0)
#define RTK_DRM_VOWB_FLAGS_VALID_START_FLAGS     (0)
#define RTK_DRM_VOWB_FLAGS_VALID_STOP_FLAGS      (0)
#define RTK_DRM_VOWB_FLAGS_VALID_RUN_CMD_FLAGS   (0)
#define RTK_DRM_VOWB_FLAGS_CHECK_CMD_BLOCK       (1)
#define RTK_DRM_VOWB_FLAGS_VALID_CHECK_CMD_FLAGS (RTK_DRM_VOWB_FLAGS_CHECK_CMD_BLOCK)

/**
 * struct rtk_drm_vowb_setup - setup vowb
 * @dst:      [in] dst pic info
 * @num_srcs: [in] max number of source
 * @flags:    [in] valid flags is in RTK_DRM_VOWB_FLAGS_VALID_SETUP_FLAGS
 */
struct rtk_drm_vowb_setup {
	__u32 flags;
	struct rtk_drm_vowb_dst_pic dst;
	__u32 handles[4];
	__u32 num_handles;
	__u32 num_srcs;
};

/**
 * struct rtk_drm_vowb_teardown - teardown vowb
 * @flags:     [in] valid flags is in RTK_DRM_VOWB_FLAGS_VALID_TEARDOWN_FLAGS
 */
struct rtk_drm_vowb_teardown {
	__u32 flags;
};

struct rtk_drm_vowb_src_pic {
	__u32 handle;
	__u32 y_offset;
	__u32 c_offset;
	__u32 w;
	__u32 h;
	__u32 y_pitch;
	__u32 c_pitch;

	__u32 resize_win_x;
	__u32 resize_win_y;
	__u32 resize_win_w;
	__u32 resize_win_h;

	__u32 contrast;
	__u32 brightness;
	__u32 hue;
	__u32 saturation;
	__u32 sharp_en;
	__u32 sharp_value;

	__u32 crop_x;
	__u32 crop_y;
	__u32 crop_w;
	__u32 crop_h;
};

/**
 * struct rtk_drm_vowb_add_src_pic - add a pic to index
 * @src:   [in] src pic info
 * @index: [in] index of the src pic slot
 */
struct rtk_drm_vowb_add_src_pic {
	struct rtk_drm_vowb_src_pic src;
	__u32 index;
};

/**
 * struct rtk_drm_vowb_start - start vowb
 * @flags:   [in] valid flags is in RTK_DRM_VOWB_FLAGS_VALID_START_FLAGS
 */
struct rtk_drm_vowb_start {
	__u32 flags;
};

/**
 * struct rtk_drm_vowb_stop - stop vowb
 * @flags:   [in] valid flags is in RTK_DRM_VOWB_FLAGS_VALID_STOP_FLAGS
 */
struct rtk_drm_vowb_stop {
	__u32 flags;
};

struct rtk_drm_vowb_pic {
	__u32 handles[4];
	__u32 offsets[4];
	__u32 pitches[4];
	__u32 mode;
	__u32 w;
	__u32 h;
	__u32 target_format;
	__u32 wb_w;
	__u32 wb_h;
	__u32 contrast;
	__u32 brightness;
	__u32 hue;
	__u32 saturation;
	__u32 sharp_en;
	__u32 sharp_value;
	__u32 crop_x;
	__u32 crop_y;
	__u32 crop_w;
	__u32 crop_h;
	__u32 buf_bit_depth;
	__u32 buf_format;
};

/**
 * struct rtk_drm_vowb_run_cmd - run a vowb cmd
 * @flags:   [in] valid flags is in RTK_DRM_VOWB_FLAGS_VALID_RUN_CMD_FLAGS
 * @cmd:     [in] cmd
 * @pic:     [in] pic info for vowb
 *                handles & offsets & pitches
 *                  y_addr    => handles[0] & offsets[0]
 *                  c_addr    => handles[0] & offsets[1]
 *                  wb_y_addr => handles[2] & offsets[2]
 *                  wb_y_addr => handles[2] & offsets[3]
 *                  y_pitch   => pitches[0]
 *                  c_pitch   => pitches[1]
 *                  wb_pitch  => pitches[2]
 *
 * job_id:   [out] job_id
 */
struct rtk_drm_vowb_run_cmd {
	__u32 flags;
	__u32 cmd;
	union {
		struct rtk_drm_vowb_pic pic;
	};
	__u64 job_id;
};

/**
 * struct rtk_drm_vowb_check_cmd - check a cmd
 * @flags:   [in] valid flags is in RTK_DRM_VOWB_FLAGS_VALID_CHECK_CMD_FLAGS
 * @job_id:  [in] job_id
 * @job_done: [out] job is done or not
 */
struct rtk_drm_vowb_check_cmd {
	__u32 flags;
	__u64 job_id;
	__u32 job_done;
};

#endif
