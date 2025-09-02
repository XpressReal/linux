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
#ifndef RTKVE_RPC_DEF_H
#define RTKVE_RPC_DEF_H

#include <linux/types.h>

#define RTKVE_MAX_MSG_NUM (32)

#define VIDEO_RPC_VENC_ToAgent_Create 2010
#define VIDEO_RPC_VENC_ToAgent_InitRingBuffer 2030
#define VIDEO_RPC_VENC_ToAgent_Run 2040
#define VIDEO_RPC_VENC_ToAgent_Pause 2050
#define VIDEO_RPC_VENC_ToAgent_Stop 2060
#define VIDEO_RPC_VENC_ToAgent_Destroy 2070
#define VIDEO_RPC_VENC_ToAgent_VideoCreate 2100
#define VIDEO_RPC_VENC_ToAgent_VideoDestroy 2110
#define VIDEO_RPC_VENC_ToAgent_VCPU_DEBUG_COMMAND 2141
#define VIDEO_RPC_VENC_ToAgent_Self_Destroy 2170
#define VIDEO_RPC_VENC_ToAgent_Init 2510
#define VIDEO_RPC_VENC_ToAgent_SetNewResolution 2535
#define VIDEO_RPC_VENC_ToAgent_SetBitRate 2540
#define VIDEO_RPC_VENC_ToAgent_SetGOPStructure 2550
#define VIDEO_RPC_VENC_ToAgent_SetEncodeFormat 2610
#define VIDEO_RPC_VENC_ToAgent_StartRecord 2640
#define VIDEO_RPC_VENC_ToAgent_PauseRecord 2650
#define VIDEO_RPC_VENC_ToAgent_StopRecord 2660
#define VIDEO_RPC_VENC_ToAgent_SetFrameRate 2740
#define VIDEO_RPC_VENC_ToAgent_ReqKeyFrame 2750
#define VIDEO_RPC_VENC_ToAgent_SetProfile 2760

typedef int HRESULT;
#define VIDEO_RPC_DEC_ToSystem_FatalError 63
#define VIDEO_RPC_DEC_ToSystem_Deliver_MediaInfo 1020
#define VIDEO_RPC_ToSystem_VoutMessage 1021
#define REPLYID 99 // for registering the Reply_Handler

enum RINGBUFFER_TYPE {
	RINGBUFFER_STREAM,
	RINGBUFFER_COMMAND,
	RINGBUFFER_MESSAGE,
	RINGBUFFER_VBI,
	RINGBUFFER_PTS,
	RINGBUFFER_DTVCC,
	RINGBUFFER_STREAM1,
	RINGBUFFER_COMMAND1,
	RINGBUFFER_MESSAGE1,
	RINGBUFFER_STREAM_BL,
	RINGBUFFER_COMMAND_BL,
	RINGBUFFER_STREAM_EL,
	RINGBUFFER_COMMAND_EL,
	RINGBUFFER_STREAM_MD,
	RINGBUFFER_COMMAND_MD,
	RINGBUFFER_STREAM_SUBES,
	RINGBUFFER_COMMAND_SUBIB,
	RINGBUFFER_FRAME_USER,
	RINGBUFFER_V4L2_CONTROL,
	RINGBUFFER_FAKE
};

enum VIDEO_VF_TYPE {
	VF_TYPE_VIDEO_MPEG2_DECODER = 0,
	VF_TYPE_VIDEO_MPEG4_DECODER = 1,
	VF_TYPE_VIDEO_DV_DECODER = 2,
	VF_TYPE_VIDEO_H263_DECODER = 3,
	VF_TYPE_VIDEO_H264_DECODER = 4,
	VF_TYPE_VIDEO_VC1_DECODER = 5,
	VF_TYPE_VIDEO_REAL_DECODER = 6,
	VF_TYPE_VIDEO_JPEG_DECODER = 7,
	VF_TYPE_VIDEO_MJPEG_DECODER = 8,
	VF_TYPE_SPU_DECODER = 9,
	VF_TYPE_VIDEO_OUT = 10,
	VF_TYPE_TRANSITION = 11,
	VF_TYPE_THUMBNAIL = 12,
	VF_TYPE_VIDEO_VP6_DECODER = 13,
	VF_TYPE_VIDEO_IMAGE_DECODER = 14,
	VF_TYPE_FLASH = 15,
	VF_TYPE_VIDEO_AVS_DECODER = 16,
	VF_TYPE_MIXER = 17,
	VF_TYPE_VIDEO_VP8_DECODER = 18,
	VF_TYPE_VIDEO_WMV7_DECODER = 19,
	VF_TYPE_VIDEO_WMV8_DECODER = 20,
	VF_TYPE_VIDEO_RAW_DECODER = 21,
	VF_TYPE_VIDEO_THEORA_DECODER = 22,
	VF_TYPE_VIDEO_FJPEG_DECODER = 23,
	VF_TYPE_VIDEO_H265_DECODER = 24,
	VF_TYPE_VIDEO_VP9_DECODER = 25,
	VF_TYPE_VIDEO_H264lv51_DECODER = 26,
	VF_TYPE_VIDEO_CAPTURER = 27,
	VF_TYPE_RAWCONV = 28,
	VF_TYPE_TVD = 29,
	VF_TYPE_VIDEO_ENCODER = 30,
	VF_TYPE_VIDEO_SPLITTER = 31,
	VF_TYPE_VIDEO_AVS2_DECODER = 32,
	VF_TYPE_VIDEO_AV1_DECODER = 33,
};

enum VIDEO_STREAM_TYPE {
	VIDEO_STREAM_MPEG1 = 0,
	VIDEO_STREAM_MPEG2 = 1,
	VIDEO_STREAM_MPEG4 = 2,
	VIDEO_STREAM_DV3 = 3,
	VIDEO_STREAM_H263 = 4,
	VIDEO_STREAM_H264 = 5,
	VIDEO_STREAM_VC1 = 6,
	VIDEO_STREAM_REALVIDEO = 7,
	VIDEO_STREAM_MJPEG = 8,
	VIDEO_STREAM_VP6 = 9,
	VIDEO_STREAM_AVS = 10,
	VIDEO_STREAM_YUV = 11,
	VIDEO_STREAM_VP8 = 12,
	VIDEO_STREAM_WMV7 = 13,
	VIDEO_STREAM_WMV8 = 14,
	VIDEO_STREAM_RAW = 15,
	VIDEO_STREAM_THEORA = 16,
	VIDEO_STREAM_UNKNOWN = 17,
	VIDEO_STREAM_FJPEG = 18,
	VIDEO_STREAM_H265 = 19,
	VIDEO_STREAM_VP9 = 20,
	VIDEO_STREAM_H264lv51 = 21,
	VIDEO_STREAM_AVS2 = 22,
	VIDEO_STREAM_AV1 = 23,
};

/** inband cmd type. I use prefix "VIDEO_DEC_" to label the cmd used in video decoder. */
enum INBAND_CMD_TYPE {
	INBAND_CMD_TYPE_PTS = 0,
	INBAND_CMD_TYPE_PTS_SKIP,
	INBAND_CMD_TYPE_NEW_SEG,
	INBAND_CMD_TYPE_SEQ_END,
	INBAND_CMD_TYPE_EOS,
	INBAND_CMD_TYPE_CONTEXT,
	INBAND_CMD_TYPE_DECODE,

	/* Video Decoder In-band Command */
	VIDEO_DEC_INBAND_CMD_TYPE_VOBU,
	VIDEO_DEC_INBAND_CMD_TYPE_DVDVR_DCI_CCI,
	VIDEO_DEC_INBAND_CMD_TYPE_DVDV_VATR,

	/* MSG Type for parse mode */
	VIDEO_DEC_INBAND_CMD_TYPE_SEG_INFO,
	VIDEO_DEC_INBAND_CMD_TYPE_PIC_INFO,

	/* Sub-picture Decoder In-band Command */
	VIDEO_SUBP_INBAND_CMD_TYPE_SET_PALETTE,
	VIDEO_SUBP_INBAND_CMD_TYPE_SET_HIGHLIGHT,

	/* Video Mixer In-band Command */
	VIDEO_MIXER_INBAND_CMD_TYPE_SET_BG_COLOR,
	VIDEO_MIXER_INBAND_CMD_TYPE_SET_MIXER_RPTS,
	VIDEO_MIXER_INBAND_CMD_TYPE_BLEND,

	/* Video Scaler In-band Command */
	VIDEO_SCALER_INBAND_CMD_TYPE_OUTPUT_FMT,

	/*Dv3 resolution In-band Command*/
	VIDEO_DV3_INBAND_CMD_TYPE_RESOLUTION,

	/*MPEG4 detected In-band command*/
	VIDEO_MPEG4_INBAND_CMD_TYPE_MP4,
	/* Audio In-band Commands Start Here */

	/* DV In-band Commands */
	VIDEO_DV_INBAND_CMD_TYPE_VAUX,
	VIDEO_DV_INBAND_CMD_TYPE_FF, //fast forward

	/* Transport Demux In-band command */
	VIDEO_TRANSPORT_DEMUX_INBAND_CMD_TYPE_PID,
	VIDEO_TRANSPORT_DEMUX_INBAND_CMD_TYPE_PTS_OFFSET,
	VIDEO_TRANSPORT_DEMUX_INBAND_CMD_TYPE_PACKET_SIZE,

	/* Real Video In-band command */
	VIDEO_RV_INBAND_CMD_TYPE_FRAME_INFO,
	VIDEO_RV_INBAND_CMD_TYPE_FORMAT_INFO,
	VIDEO_RV_INBAND_CMD_TYPE_SEGMENT_INFO,

	/*VC1 video In-band command*/
	VIDEO_VC1_INBAND_CMD_TYPE_SEQ_INFO,

	/* general video properties */
	VIDEO_INBAND_CMD_TYPE_VIDEO_USABILITY_INFO,
	VIDEO_INBAND_CMD_TYPE_VIDEO_MPEG4_USABILITY_INFO,

	/*MJPEG resolution In-band Command*/
	VIDEO_MJPEG_INBAND_CMD_TYPE_RESOLUTION,

	/* picture object for graphic */
	VIDEO_GRAPHIC_INBAND_CMD_TYPE_PICTURE_OBJECT,
	VIDEO_GRAPHIC_INBAND_CMD_TYPE_DISPLAY_INFO,

	/* subtitle offset sequence id for 3D video */
	VIDEO_DEC_INBAND_CMD_TYPE_SUBP_OFFSET_SEQUENCE_ID,

	VIDEO_H264_INBAND_CMD_TYPE_DPBBYPASS,

	/* Clear back frame to black color and send it to VO */
	VIDEO_FJPEG_INBAND_CMD_TYPE_CLEAR_SCREEN,

	/* each picture info of MJPEG */
	VIDEO_FJPEG_INBAND_CMD_TYPE_PIC_INFO,

	/*FJPEG resolution In-band Command*/
	VIDEO_FJPEG_INBAND_CMD_TYPE_RESOLUTION,

	/*VO receive VP_OBJ_PICTURE_TYPE In-band Command*/
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_PIC,
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_DVD_SP,
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_DVB_SP,
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_BD_SP,
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_SP_FLUSH,
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_SP_RESOLUTION,

	/* VO receive writeback buffers In-band Command */
	VIDEO_VO_INBAND_CMD_TYPE_WRITEBACK_BUFFER,

	/* for VO debug, VO can dump picture */
	VIDEO_VO_INBAND_CMD_TYPE_DUMP_PIC,
	VIDEO_CURSOR_INBAND_CMD_TYPE_PICTURE_OBJECT,
	VIDEO_CURSOR_INBAND_CMD_TYPE_COORDINATE_OBJECT,
	VIDEO_TRANSCODE_INBAND_CMD_TYPE_PICTURE_OBJECT,
	VIDEO_WRITEBACK_INBAND_CMD_TYPE_PICTURE_OBJECT,

	VIDEO_VO_INBAND_CMD_TYPE_OBJ_BD_SCALE_RGB_SP,

	// TV code
	VIDEO_INBAND_CMD_TYPE_DV_CERTIFY,

	/*M_DOMAIN resolution In-band Command*/
	VIDEO_INBAND_CMD_TYPE_M_DOMAIN_RESOLUTION,

	/* DTV source In-band Command */
	VIDEO_INBAND_CMD_TYPE_SOURCE_DTV,

	/* Din source copy mode In-band Command */
	VIDEO_DIN_INBAND_CMD_TYPE_COPY_MODE,

	/* Video Decoder AU In-band command */
	VIDEO_DEC_INBAND_CMD_TYPE_AU,

	/* Video Decoder parse frame In-band command */
	VIDEO_DEC_INBAND_CMD_TYPE_PARSE_FRAME_IN,
	VIDEO_DEC_INBAND_CMD_TYPE_PARSE_FRAME_OUT,

	/* Set video decode mode In-band command */
	VIDEO_DEC_INBAND_CMD_TYPE_NEW_DECODE_MODE,

	/* Secure buffer protection */
	VIDEO_INBAND_CMD_TYPE_SECURE_PROTECTION,

	/* Dolby HDR inband command */
	VIDEO_DEC_INBAND_CMD_TYPE_DV_PROFILE,

	/* VP9 HDR10 In-band command */
	VIDEO_VP9_INBAND_CMD_TYPE_HDR10_METADATA,

	/* AV1 HDR10 In-band command */
	VIDEO_AV1_INBAND_CMD_TYPE_HDR10_METADATA,

	/* DvdPlayer tell RVSD video BS ring buffer is full */
	VIDEO_DEC_INBAND_CMD_TYPE_BS_RINGBUF_FULL,

	/* Frame Boundary In-band command */
	VIDEO_INBAND_CMD_TYPE_FRAME_BOUNDARY = 100,

	/* VO receive npp writeback buffers In-band Command */
	VIDEO_NPP_INBAND_CMD_TYPE_WRITEBACK_BUFFER,
	VIDEO_NPP_OUT_INBAND_CMD_TYPE_OBJ_PIC,

	/* hevc encoder raw yuv data In-band Commnad */
	VENC_INBAND_CMD_TYPE_RAWYUV,

	/* hevc encoder ref yuv addr In-band Commnad */
	VENC_INBAND_CMD_TYPE_REFYUV,

	/* add frame info for user allocate */
	VIDEO_FRAME_INBAND_ADD,

	/* delete frame info for user allocate */
	VIDEO_FRAME_INBAND_DELETE,

	VIDEO_AV1_INBAND_CMD_TYPE_V4L2_CTRL = 108,
	VIDEO_HEVC_INBAND_CMD_TYPE_V4L2_CTRL = 109,
	VIDEO_VP9_INBAND_CMD_TYPE_V4L2_CTRL = 110,
};

enum ENUM_DVD_VIDEO_ENCODER_OUTPUT_INFO_TYPE {
	VIDEOENCODER_VideoGEN,
	VIDEOENCODER_VideoFrameInfo,
	VIDEOENCODER_VideoVBID_WSS_Info,
	VIDEOENCODER_VideoEndOfStream_Info,
	VIDEOENCODER_Input_EndOfStream_Info,
	VIDEOENCODER_VideoPauseInfo,
	VIDEOENCODER_AutoPauseInfo,
	VIDEOENCODER_TotalInfo
};

enum YUV_FMT {
	F_YUV420_Semi = 0,
	F_YUV420P = 1,
	F_YUV422 = 2,
	F_YUYV422 = 3,
	F_ARGB = 4,
	F_MMCOMP = 5,
};

enum VIDEO_ENC_PROFILE {
	VIDEO_PROFILE_MAIN = 0,
	VIDEO_PROFILE_MAIN10 = 1,
};

enum VIDEO_RATE_CONTROL_MODE {
	VIDEO_RATE_CBR = 0,
	VIDEO_RATE_VBR = 1,
	VIDEO_RATE_CVBR = 2,
};

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
	volatile int32_t bSeekable; //Can't be sought if data is streamed by HTTP
};

struct RPC_RINGBUFFER {
	uint32_t instanceID;
	uint32_t pinID;
	uint32_t readPtrIndex;
	uint32_t pRINGBUFF_HEADER;
};

struct VIDEO_RPC_ENC_ELEM_FRAME_INFO {
	enum ENUM_DVD_VIDEO_ENCODER_OUTPUT_INFO_TYPE infoType;
	unsigned int pictureNumber;
	unsigned char pictureType;
	unsigned char topFieldFirst;
	unsigned char numOfField;
	unsigned char newScene;
	unsigned int PTShigh;
	unsigned int PTSlow;
	unsigned int DTShigh;
	unsigned int DTSlow;
	unsigned int VBIData;
	unsigned int VBVfullness;
	unsigned char resumedVideoFrame;
	unsigned char newVOBUStart;
	unsigned char KeyFrame;
	unsigned char AGCDetection;
	unsigned int CCData;
	unsigned int CCStatus;
	int frameSize;
	unsigned int buf_idx;
};

struct VIDEO_RPC_ENC_INIT {
	uint32_t instanceID;
	uint32_t type;
	enum YUV_FMT yuvFormat;
};

struct VIDEO_RPC_ENC_SET_ENCFORMAT {
	uint32_t instanceID;
	enum VIDEO_STREAM_TYPE streamType;
};

struct VIDEO_RPC_ENC_SET_NEW_RESOLUTION {
	uint32_t instanceID;
	uint32_t in_width;
	uint32_t in_height;
	uint32_t out_width;
	uint32_t out_height;
	uint32_t bit_depth;
};

struct VIDEO_RPC_ENC_SET_GOPSTRUCTURE {
	uint32_t instanceID;
	uint32_t M;
	uint32_t N;
};

struct VIDEO_RPC_ENC_SET_PROFILE {
	uint32_t instanceID;
	enum VIDEO_ENC_PROFILE profile;
};

struct VIDEO_RPC_ENC_SET_BITRATE {
	uint32_t instanceID;
	enum VIDEO_RATE_CONTROL_MODE rateControlMode;
	uint32_t peakBitRate;
	uint32_t aveBitRate;
	uint32_t bitBufferSize;
	uint32_t initBufferFullness;
	uint32_t time;
};

struct VIDEO_RPC_ENC_SET_FRAME_RATE {
	uint32_t instanceID;
	uint32_t frame_rate;
};

struct VIDEO_RPC_ENC_REQ_KEY_FRAME {
	uint32_t instanceID;
};

struct VIDEO_RPC_ENC_START_ENC {
	uint32_t instanceID;
	uint32_t startMode;
};

struct VIDEO_RPC_ENC_STOP_ENC {
	uint32_t instanceID;
};

struct RPC_STRUCT {
	uint32_t programID; // program ID defined in IDL file
	uint32_t versionID; // version ID defined in IDL file
	uint32_t procedureID; // function ID defined in IDL file
	uint32_t taskID; // the caller's task ID, assign 0 if NONBLOCK_MODE
	uint32_t sysTID;
	uint32_t sysPID; // the callee's task ID
	uint32_t parameterSize; // packet's body size
	uint32_t context; // return address of reply value
};

struct RPC_CONNECTION {
	uint32_t srcInstanceID;
	uint32_t srcPinID;
	uint32_t desInstanceID;
	uint32_t desPinID;
	uint32_t mediaType;
};

struct RPCRES_LONG {
	uint32_t result;
	uint32_t data;
};

//inband
struct INBAND_CMD_PKT_HEADER {
	enum INBAND_CMD_TYPE type;
	uint32_t size;
};

struct VIDEO_RPC_INSTANCE {
	enum VIDEO_VF_TYPE type;
};

struct PTS_INFO {
	struct INBAND_CMD_PKT_HEADER header;
	unsigned int wPtr;
	unsigned int PTSH;
	unsigned int PTSL;
};

struct RAWYUV_INFO {
	struct INBAND_CMD_PKT_HEADER header;
	unsigned int luma_addr;
	unsigned int luma_size;
	unsigned int chroma_addr;
	unsigned int chroma_size;
	unsigned int PTSH;
	unsigned int PTSL;
	unsigned int buf_index;
};

struct REFYUV_INFO {
	struct INBAND_CMD_PKT_HEADER header;
	unsigned int start_addr;
	unsigned int size;
};

struct EOS {
	struct INBAND_CMD_PKT_HEADER header;
	unsigned int wPtr;
	unsigned int eventID;
};

#endif
