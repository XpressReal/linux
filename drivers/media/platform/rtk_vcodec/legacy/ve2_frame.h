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
#ifndef __VE2_FRAME_H__
#define __VE2_FRAME_H__

#include <linux/types.h>

#define PIC_SIZE_INVALID ((uint32_t)-1)

#define VE2RPC_FLASH_FRAME_INFO_VERSION 1
#define VE2RPC_FLASH_FRAME_INFO_SIZE 384

#define VE2RPC_DVO_INFO_DOLBY_VISION_SIGN (0xECECECEC)

/**
 * Indicate if a frame has a base layer frame dequeued from flash driver
 */
#define VE2RPC_DVO_INFO_FLAGS_BL_DEQUEUED (0x00000001)

/**
 * Indicate if a frame has a enhance layer frame dequeued from flash driver
 */
#define VE2RPC_DVO_INFO_FLAGS_EL_DEQUEUED (0x00000002)

/**
 * Indicate if a frame is the first frame after flushing
 */
#define VE2RPC_DVO_INFO_FLAGS_FLUSHED (0x00000004)

#define VRPC_FRAME_INFO_FLAG_EOS 0x00000001
#define VRPC_FRAME_INFO_FLAG_DATACORRUPT 0x00000002
#define VRPC_FRAME_INFO_FLAG_DISCARD 0x00000004

typedef struct __attribute__((__packed__)) ve2rpc_hdr_info_t {
	uint32_t hdr_type;
	uint8_t nTransferCharacteristics;
	uint8_t nMatrixCoefficiets;
	uint8_t nColorPrimaries;
	uint8_t bVideoFullRangeFlag;
	// offset = 8
	uint16_t nDisplayPrimariesX[3];
	uint16_t nDisplayPrimariesY[3];
	uint16_t nWhitePointX;
	uint16_t nWhitePointY;
	uint16_t reserve2[4];
	// offset = 8 + 24
	uint32_t nMaxDisplayMasteringLuminance;
	uint32_t nMinDisplayMasteringLuminance;
	uint32_t nMaxCLL;
	uint32_t nMaxFALL;
	uint32_t nHdrType;
	uint32_t reserve3[3];
	// size = 8 + 24 + 32 = 64
} ve2rpc_hdr_info_t;
typedef struct __attribute__((__packed__)) ve2rpc_frame_info_t {
	void *pixel;
	uint32_t phyaddr;
	uint32_t width;
	uint32_t height;
	uint32_t ptsLow;
	uint32_t ptsHigh;
	uint32_t pts2High;
	uint32_t pts2Low;
	uint32_t ptsSeiHigh;
	uint32_t ptsSeiLow;
	uint32_t flag;
	uint32_t keyId;
	uint32_t framerate;
	uint32_t progressive;
	uint32_t pitch;
	uint32_t cpitch;
	uint32_t reserve[2];
	// offset = 72
	uint8_t checkPicCoding;
	uint8_t bitDepthLuma;
	uint8_t bitDepthChroma;
	uint8_t completeFields;
	uint32_t phyaddrY;
	uint32_t phyaddrC;
	uint32_t offset;
	uint32_t picCoding;
	uint32_t interlaceMode;
	ve2rpc_hdr_info_t hdrInfo;
	uint32_t reserve2[8];
	uint32_t blid;
	uint32_t ext_data_in_use;
	uint32_t is_ext_video;
	uint32_t nDecClkTime;
	uint32_t nDecFrameCount;
	uint32_t is_frame_decoded;
	uint32_t hdr_metadata_addr;
	uint32_t hdr_metadata_size;
	bool has_pts;
	uint64_t nTimeStamp;
	bool bWaitReleaseByBLID;
	bool bPeekFrame;
	uint64_t nFilledLen;
} ve2rpc_frame_info_t;
/*
 * VCPU Flash output farme info data structure
 * Size = 256 bytes
 * */
typedef struct {
	unsigned int nSize;
	unsigned int nVersion;
	unsigned int pUserData;
	unsigned int nRRKey;
	unsigned int nContext;
	unsigned int nBufID;
	unsigned int nPicFlags;
	unsigned int nPicWidth;
	unsigned int nPicHeight;
	unsigned int nDecimatePicWidth;
	unsigned int nDecimateicHeight;
	unsigned int nBitDepthLuma;
	unsigned int nBitDepthChroma;

	unsigned int nPtsHigh;
	unsigned int nPtsLow;
	unsigned int nRPtsHigh;
	unsigned int nRPtsLow;
	unsigned int nPts2High;
	unsigned int nPts2Low;
	unsigned int nPicPhysicalAddr;
	unsigned int nPicPitch;
	unsigned int nClkTimeHigh;
	unsigned int nClkTimeLow;
	unsigned int nDecClkTime;
	unsigned int nDecFrameCount;
	unsigned int nFramerateD;
	unsigned int nFramerateN;
	unsigned int eScanType; /* 0: progressive, 1: interlaced */
	/* nInterlaceMode define  */
	unsigned int nInterlaceMode;
	unsigned int nSeiPtsHigh; /* SEI PTS High */
	unsigned int nSeiPtsSLow; /* SEI PTS Low */
	unsigned int nPicCPitch;
	unsigned int nPicCPhysicalAddr;
	unsigned int nSampleWidth;
	unsigned int nSampleHeight;
	unsigned int qlevel_sel_y;
	unsigned int qlevel_sel_c;
	/* nHDR_Type define */
	unsigned int nHDR_Type;
	unsigned int nDisplayPrimaries_X[3];
	unsigned int nDisplayPrimaries_Y[3];
	unsigned int nWhitePoint_X;
	unsigned int nWhitePoint_Y;
	unsigned int nMaxDisplayMasteringLuminance;
	unsigned int nMinDisplayMasteringLuminance;
	unsigned int nTransferCharacteristics;
	unsigned int nMatrixCoefficiets;
	unsigned int nVideoFullRangeFlag;
	unsigned int nMaxCLL;
	unsigned int nMaxFALL;
	unsigned int hdr_metadata_addr;
	unsigned int hdr_metadata_size;
	unsigned int tch_metadata_addr;
	unsigned int tch_metadata_size;
	unsigned int film_grain_metadata_addr;
	unsigned int film_grain_metadata_size;

	/* 0: raw, 1: lossless, 2: lossy */
	unsigned int nCmprsMode;
	unsigned int nPicYCmprsHdrAddr;
	unsigned int nPicCCmprsHdrAddr;
	unsigned int nPicCmprsPitch;
	unsigned int nPicCPhysicalAddr2;

	unsigned int nBufLockPhysicalAddr;
	unsigned int max_fb_num;
	unsigned int max_cmprs_head_size;

	unsigned int nLinearPicPhysicalAddr;
	unsigned int nLinearPicCPhysicalAddr;
	unsigned int nLinearPicWidth;
	unsigned int nLinearPicHeight;
	unsigned int nLinearPicPitch;

	unsigned int nPixelAR_hor;
	unsigned int nPixelAR_ver;
	union {
		unsigned int data;
		struct {
			unsigned int have_timecode:1;     // bit[0]
			unsigned int seconds:6;           // bit[6:1]
			unsigned int minutes:6;           // bit[12:7]
			unsigned int hours:5;             // bit[17:13]
			unsigned int frames:9;            // bit[26:18]
		};
	} nHevc_tc_timestamp;
	union{
		unsigned int data;
		struct{
			unsigned int colour_primaries:8; // bit[7:0]
			unsigned int transferCharacteristics:8; // bit[15:8]
		};
	}nVUI_information;
	unsigned int dv_rpu_metadata_addr;
	unsigned int dv_rpu_metadata_size;
	unsigned int noShowFrame_count;
	unsigned int noShowFrame_picId[8];
	unsigned int reserved[9];
} ve2rpc_flash_frame_info_t;

#endif
