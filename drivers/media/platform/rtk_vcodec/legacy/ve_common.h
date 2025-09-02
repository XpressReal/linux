/*
 * Realtek video decoder v4l2 driver
 *
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */
#ifndef __VE_COMMON_H__
#define __VE_COMMON_H__

enum PICTURE_MODE {
	INTERLEAVED_TOP_FIELD = 0,  /* top	field data stored in even lines of a frame buffer */
	INTERLEAVED_BOT_FIELD,	  /* bottom field data stored in odd  lines of a frame buffer */
	CONSECUTIVE_TOP_FIELD,	  /* top	field data stored consecutlively in all lines of a field buffer */
	CONSECUTIVE_BOT_FIELD,	  /* bottom field data stored consecutlively in all lines of a field buffer */
	CONSECUTIVE_FRAME,		   /* progressive frame data stored consecutlively in all lines of a frame buffer */
	INTERLEAVED_TOP_FIELD_422,  /* top	field data stored in even lines of a frame buffer */
	INTERLEAVED_BOT_FIELD_422,	  /* bottom field data stored in odd  lines of a frame buffer */
	CONSECUTIVE_TOP_FIELD_422,	  /* top	field data stored consecutlively in all lines of a field buffer */
	CONSECUTIVE_BOT_FIELD_422,	  /* bottom field data stored consecutlively in all lines of a field buffer */
	CONSECUTIVE_FRAME_422,		/* progressive frame with 4:2:2 chroma */
	TOP_BOTTOM_FRAME,			/* top field in the 0~height/2-1, bottom field in the height/2~height-1 in the frame */
	INTERLEAVED_TOP_BOT_FIELD,   /* one frame buffer contains one top and one bot field, top field first */
	INTERLEAVED_BOT_TOP_FIELD,   /* one frame buffer contains one bot and one top field, bot field first */

	MPEG2_PIC_MODE_NOT_PROG	  /*yllin: for MPEG2 check pic mode usage */
};

struct yuv_state {
	unsigned int version;
	unsigned int mode;
	unsigned int Y_addr;
	unsigned int U_addr;
	unsigned int pLock;
	unsigned int width;
	unsigned int height;
	unsigned int Y_pitch;
	unsigned int C_pitch;
	unsigned int RPTSH;
	unsigned int RPTSL;
	unsigned int PTSH;
	unsigned int PTSL;

	/* for send two interlaced fields in the same packet,
    valid only when mode is INTERLEAVED_TOP_BOT_FIELD or INTERLEAVED_BOT_TOP_FIELD*/
	unsigned int RPTSH2;
	unsigned int RPTSL2;
	unsigned int PTSH2;
	unsigned int PTSL2;

	unsigned int context;
	unsigned int pRefClock; /* not used now */

	unsigned int pixelAR_hor; /* pixel aspect ratio hor, not used now */
	unsigned int pixelAR_ver; /* pixel aspect ratio ver, not used now */

	unsigned int Y_addr_Right; /* for mvc */
	unsigned int U_addr_Right; /* for mvc */
	unsigned int pLock_Right; /* for mvc */
	unsigned int mvc; /* 1: mvc */
	unsigned int
		subPicOffset; /* 3D Blu-ray dependent-view sub-picture plane offset metadata as defined in BD spec sec. 9.3.3.6.
    				Valid only when Y_BufId_Right and C_BufId_Right are both valid */
	unsigned int pReceived; // fix bug 44329 by version 0x72746B30 'rtk0'
	unsigned int
		pReceived_Right; // fix bug 44329 by version 0x72746B30 'rtk0'

	unsigned int fps; // 'rtk1'

	unsigned int
		IsForceDIBobMode; // force vo use bob mode to do deinterlace, 'rtk2'.
	unsigned int lumaOffTblAddr; // 'rtk3'
	unsigned int chromaOffTblAddr; // 'rtk3'
	unsigned int lumaOffTblAddrR; // for mvc, 'rtk3'
	unsigned int chromaOffTblAddrR; // for mvc, 'rtk3'

	unsigned int bufBitDepth; // 'rtk3'
	unsigned int
		bufFormat; // 'rtk3', according to VO spec: 10bits Pixel Packing mode selection,
		// "0": use 2 bytes to store 1 components. MSB justified.
		// "1": use 4 bytes to store 3 components. LSB justified.

	// VUI (Video Usability Information)
	unsigned int transferCharacteristics; // 0:SDR, 1:HDR, 2:ST2084, 'rtk3'
	// Mastering display colour volume SEI, 'rtk3'
	unsigned int display_primaries_x0;
	unsigned int display_primaries_y0;
	unsigned int display_primaries_x1;
	unsigned int display_primaries_y1;
	unsigned int display_primaries_x2;
	unsigned int display_primaries_y2;
	unsigned int white_point_x;
	unsigned int white_point_y;
	unsigned int max_display_mastering_luminance;
	unsigned int min_display_mastering_luminance;

	// for transcode interlaced feild use.	//'rtk4'
	unsigned int Y_addr_prev; //'rtk4'
	unsigned int U_addr_prev; //'rtk4'
	unsigned int Y_addr_next; //'rtk4'
	unsigned int U_addr_next; //'rtk4'
	unsigned int video_full_range_flag; //'rtk4' default= 1
	unsigned int matrix_coefficients; //'rtk4' default= 1

	// for transcode interlaced feild use.	//'rtk5'
	unsigned int pLock_prev;
	unsigned int pReceived_prev;
	unsigned int pLock_next;
	unsigned int pReceived_next;

	unsigned int is_tch_video; //'rtk6'
	unsigned int tch_hdr_metadata[144]; //'rtk6'

	unsigned int pFrameBufferDbg; //'rtk7'
	unsigned int pFrameBufferDbg_Right;
	unsigned int Y_addr_EL; //'rtk8' for dolby vision
	unsigned int U_addr_EL;
	unsigned int width_EL;
	unsigned int height_EL;
	unsigned int Y_pitch_EL;
	unsigned int C_pitch_EL;
	unsigned int lumaOffTblAddr_EL;
	unsigned int chromaOffTblAddr_EL;

	unsigned int dm_reg1_addr;
	unsigned int dm_reg1_size;
	unsigned int dm_reg2_addr;
	unsigned int dm_reg2_size;
	unsigned int dm_reg3_addr;
	unsigned int dm_reg3_size;
	unsigned int dv_lut1_addr;
	unsigned int dv_lut1_size;
	unsigned int dv_lut2_addr;
	unsigned int dv_lut2_size;

	unsigned int slice_height; //'rtk8'

	unsigned int hdr_metadata_addr; //'rtk9'
	unsigned int hdr_metadata_size; //'rtk9'
	unsigned int tch_metadata_addr; //'rtk9'
	unsigned int tch_metadata_size; //'rtk9'
	unsigned int is_dolby_video; //'rtk10'

	unsigned int lumaOffTblSize; //'rtk11'
	unsigned int chromaOffTblSize; //'rtk11'
	// 'rtk12'
	unsigned int Combine_Y_Addr;
	unsigned int Combine_U_Addr;
	unsigned int Combine_Width;
	unsigned int Combine_Height;
	unsigned int Combine_Y_Pitch;
	unsigned int secure_flag;

	// 'rtk13'
	unsigned int tvve_picture_width;
	unsigned int tvve_lossy_en;
	unsigned int tvve_bypass_en;
	unsigned int tvve_qlevel_sel_y;
	unsigned int tvve_qlevel_sel_c;
	unsigned int is_ve_tile_mode;
	unsigned int film_grain_metadat_addr;
	unsigned int film_grain_metadat_size;

	// 'rtk14'
	unsigned int partialSrcWin_x; //rtk14 0x72746B3E
	unsigned int partialSrcWin_y;
	unsigned int partialSrcWin_w;
	unsigned int partialSrcWin_h;

	// 'rtk15'
	unsigned int dolby_out_hdr_metadata_addr; //rtk 15 0x72746B3F
	unsigned int dolby_out_hdr_metadata_size;
};

/* simplified structure for capture buffer */
struct ve_frame_info {
	unsigned int rtk_meta_buf_id;
	unsigned int is_ve1_buf;

	struct yuv_state yuvs;

	// priviate data
	unsigned int hdr_type;
};

#endif

