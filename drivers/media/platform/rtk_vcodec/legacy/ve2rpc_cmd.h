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
#ifndef __VE2RPC_CMD_H__
#define __VE2RPC_CMD_H__

#define RPC_VIDEO 0x1
#define S_OK 0x10000000

enum buflock_status {
	E_BUFLOCK_ST_ERROR,
	E_BUFLOCK_ST_NORMAL, /* initial state, set by ARM, allow VFW to access ve2rpc_flash_frame_info_t */
	E_BUFLOCK_ST_TOUCH, /* VFW has completed filling ve2rpc_flash_frame_info_t, the status is set by VFW */
	E_BUFLOCK_ST_LOCK, /* ARM start to access ve2rpc_flash_frame_info_t, the status is set by ARM */
	E_BUFLOCK_ST_UNLOCK,
	E_BUFLOCK_ST_RELEASE,
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
typedef enum VIDEO_STREAM_TYPE VIDEO_STREAM_TYPE;

#define VIDEO_RPC_COMMON_ToAgent_Create 10
#define VIDEO_RPC_COMMON_ToAgent_Connect 20
#define VIDEO_RPC_COMMON_ToAgent_InitRingBuffer 30
#define VIDEO_RPC_COMMON_ToAgent_Run 40
#define VIDEO_RPC_COMMON_ToAgent_Pause 50
#define VIDEO_RPC_COMMON_ToAgent_Stop 60
#define VIDEO_RPC_COMMON_ToAgent_Destroy 70
#define VIDEO_RPC_COMMON_ToAgent_Flush 80
#define VIDEO_RPC_COMMON_ToAgent_SetRefClock 90
#define VIDEO_RPC_COMMON_ToAgent_VideoCreate 100
#define VIDEO_RPC_COMMON_ToAgent_VideoConfig 105
#define VIDEO_RPC_COMMON_ToAgent_VideoMemoryConfig 108
#define VIDEO_RPC_COMMON_ToAgent_VideoChunkConfig 109
#define VIDEO_RPC_COMMON_ToAgent_VideoDestroy 110
#define VIDEO_RPC_COMMON_ToAgent_RequestBuffer 120
#define VIDEO_RPC_COMMON_ToAgent_ReleaseBuffer 130
#define VIDEO_RPC_COMMON_ToAgent_ConfigLowDelay 133
#define VIDEO_RPC_COMMON_ToAgent_SetDebugMemory 140
#define VIDEO_RPC_COMMON_ToAgent_VCPU_DEBUG_COMMAND 141
#define VIDEO_RPC_COMMON_ToAgent_VideoHalt 150
#define VIDEO_RPC_COMMON_ToAgent_YUYV2RGB 160
#define VIDEO_RPC_COMMON_ToAgent_Self_Destroy 170
#define VIDEO_RPC_ToAgent_SetResourceInfo 550
#define VIDEO_RPC_DEC_ToAgent_CmprsCtrl 1005
#define VIDEO_RPC_DEC_ToAgent_DecimateCtrl 1006
#define VIDEO_RPC_DEC_ToAgent_SetSpeed 1010
#define VIDEO_RPC_DEC_ToAgent_SetErrorConcealmentLevel 1015
#define VIDEO_RPC_DEC_ToAgent_Init 1020
#define VIDEO_RPC_DEC_ToAgent_SetDeblock 1030
#define VIDEO_RPC_DEC_ToAgent_GetVideoSequenceInfo 1035
#define VIDEO_RPC_DEC_ToAgent_GetVideoSequenceInfo_New 1036
#define VIDEO_RPC_DEC_ToAgent_BitstreamValidation 1040
#define VIDEO_RPC_DEC_ToAgent_ParseResolution 1041
#define VIDEO_RPC_DEC_ToAgent_Capability 1045
#define VIDEO_RPC_DEC_ToAgent_SetDecoderCCBypass 1050
#define VIDEO_RPC_DEC_ToAgent_SetDNR 1060
#define VIDEO_RPC_DEC_ToAgent_SetRefSyncLimit 1065
#define VIDEO_RPC_FLASH_ToAgent_SetOutput 1085
#define VIDEO_RPC_THUMBNAIL_ToAgent_SetVscalerOutputFormat 1070
#define VIDEO_RPC_THUMBNAIL_ToAgent_SetThreshold 1080
#define VIDEO_RPC_VOUT_ToAgent_SetV2alpha 3090
#define VIDEO_RPC_THUMBNAIL_ToAgent_SetStartPictureNumber 1090
#define VIDEO_RPC_DEC_ToAgent_PrivateInfo 1095
#define VIDEO_RPC_SUBPIC_DEC_ToAgent_Configure 5040
#define VIDEO_RPC_SUBPIC_DEC_ToAgent_Page 5050
#define VIDEO_RPC_JPEG_ToAgent_DEC 6010
#define VIDEO_RPC_JPEG_ToAgent_DEC_BATCH 6011
#define VIDEO_RPC_TRANSITION_ToAgent_Start 6020
#define VIDEO_RPC_MIXER_FILTER_ToAgent_Configure 8010
#define VIDEO_RPC_MIXER_FILTER_ToAgent_ConfigureWindow 8020
#define VIDEO_RPC_MIXER_FILTER_ToAgent_SetMasterWindow 8030
#define VIDEO_RPC_MIXER_ToAgent_PlayOneMotionJpegFrame 8040

typedef enum _tagRingBufferType {
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
	RINGBUFFER_FAKE
} RINGBUFFER_TYPE;

/** inband cmd type. I use prefix "VIDEO_DEC_" to label the cmd used in video decoder. */
typedef enum {
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
} INBAND_CMD_TYPE;

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
typedef enum VIDEO_VF_TYPE VIDEO_VF_TYPE;

typedef enum {
	NORMAL_DECODE = 0,
	I_ONLY_DECODE,
	FASTFR_DECODE,
	RESERVED1,
	TS_NORMAL_DECODE,
	TS_I_ONLY_DECODE,
	TS_FASTFR_DECODE,
	RESERVED2,
	BITSTREAM_PARSING,
	TRANSCODE_PARSING,
	NORMAL_DECODE_MVC,
	//TV code
	NORMAL_I_ONLY_DECODE,
	IP_ONLY_DECODE,
	VDEC_DIRECT_DECODE = 16,
	DRIP_I_ONLY_DECODE = 17,
	NORMAL_DECODE_LOWDELAY = 18,
	NO_REF_SYNC_DECODE = 32
} DECODE_MODE;

typedef struct {
	INBAND_CMD_TYPE type;
	unsigned int size;
} INBAND_CMD_PKT_HEADER;

typedef struct {
	INBAND_CMD_PKT_HEADER header;
	unsigned int wPtr;
} NEW_SEG;

typedef struct {
	INBAND_CMD_PKT_HEADER header;
	unsigned int RelativePTSH;
	unsigned int RelativePTSL;
	unsigned int PTSDurationH;
	unsigned int PTSDurationL;
	unsigned int skip_GOP;
	DECODE_MODE mode;
	unsigned int isHM91; /* for HEVC codec version 1: HM91 0: HM10+*/
	unsigned int useAbsolutePTS; /* 0: relative PTS, 1: absolute PTS */
} DECODE_NEW;

typedef struct {
	INBAND_CMD_PKT_HEADER header;
	unsigned int wPtr;
	unsigned int PTSH;
	unsigned int PTSL;
	unsigned int PTSH2;
	unsigned int PTSL2;
	unsigned int length;
	unsigned int flag;
} PTS_INFO2;

typedef struct {
	INBAND_CMD_PKT_HEADER header;
	unsigned int lu_addr;
	unsigned int ch_addr;
	unsigned int decimate_lu_addr;
	unsigned int decimate_ch_addr;
	unsigned int cmprs_hdr_lu;
	unsigned int cmprs_hdr_ch;
	unsigned int width;
	unsigned int height;
	unsigned int decimate_width;
	unsigned int decimate_height;
	unsigned int ddr_width;
	unsigned int ddr_height;
	unsigned int cmprs_hdr_size;
	unsigned int cmprs_en;
	unsigned int lossy_en;
	unsigned int lossy_ratio;
	unsigned int bit_depth;
	unsigned int decimate_en;
	unsigned int decimate_ratio;
} FRAME_INFO_IN;

typedef struct {
	INBAND_CMD_PKT_HEADER header;
	unsigned int lu_addr;
} FRAME_INFO_OUT;

/**
	\brief Mark a EOS on an address.
		This is the last inband command of a segment and is mandatary.
	\param wPtr
*/
typedef struct {
	INBAND_CMD_PKT_HEADER header;
	unsigned int wPtr;
	unsigned int eventID;
} EOS;

typedef struct RPCRES_LONG {
	uint32_t result;
	uint32_t data;
} RPCRES_LONG;

struct RPC_RINGBUFFER {
	unsigned int instanceID;
	unsigned int pinID;
	unsigned int readPtrIndex;
	unsigned int pRINGBUFF_HEADER;
};
typedef struct RPC_RINGBUFFER RPC_RINGBUFFER;

struct VIDEO_RPC_INSTANCE {
	enum VIDEO_VF_TYPE type;
};
typedef struct VIDEO_RPC_INSTANCE VIDEO_RPC_INSTANCE;

struct RPC_CONNECTION {
	unsigned int srcInstanceID;
	unsigned int srcPinID;
	unsigned int desInstanceID;
	unsigned int desPinID;
	unsigned int mediaType;
};
typedef struct RPC_CONNECTION RPC_CONNECTION;

struct VIDEO_RPC_DEC_SET_SPEED {
	uint32_t instanceID;
	uint32_t displaySpeed;
	uint32_t decodeSkip;
};
typedef struct VIDEO_RPC_DEC_SET_SPEED VIDEO_RPC_DEC_SET_SPEED;

struct VIDEO_RPC_DEC_INIT {
	uint32_t instanceID;
	VIDEO_STREAM_TYPE type;
	struct VIDEO_RPC_DEC_SET_SPEED set_speed;
};
typedef struct VIDEO_RPC_DEC_INIT VIDEO_RPC_DEC_INIT;
typedef struct RPC_STRUCT RPC_STRUCT;
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
;
struct VIDEO_RPC_VOUT_MESSAGE {
	uint32_t instanceID;
	uint32_t message;
	uint32_t PTShigh;
	uint32_t PTSlow;
	uint32_t reserved1;
	uint32_t reserved2;
	uint32_t reserved3;
	uint32_t reserved4;
};
typedef struct VIDEO_RPC_VOUT_MESSAGE VIDEO_RPC_VOUT_MESSAGE;
struct VIDEO_RPC_DEC_MEDIA_INFO {
	uint32_t instanceID;
	uint32_t width;
	uint32_t height;
	uint32_t frame_rate;
	uint32_t aspect_ratio_n;
	uint32_t aspect_ratio_d;
	uint32_t level;
	uint32_t profile;
	uint32_t type_3D;
	uint32_t par_width;
	uint32_t par_height;
	uint32_t type_LR;
	uint32_t type_Scan;
	uint32_t afd;
};
typedef struct VIDEO_RPC_DEC_MEDIA_INFO VIDEO_RPC_DEC_MEDIA_INFO;
struct VIDEO_RPC_DEC_ERROR_INFO {
	uint32_t instanceID;
	uint32_t errCode;
};
typedef struct VIDEO_RPC_DEC_ERROR_INFO VIDEO_RPC_DEC_ERROR_INFO;

enum VIDEO_DECODER_CC_BYPASS_MODE {
	VIDEODECODER_CC_DROP = 0,
	VIDEODECODER_CC_BYPASS = 1,
	VIDEODECODER_CC_DECODE = 2,
	VIDEODECODER_CC_CALLBACK = 3,
};

struct VIDEO_RPC_DEC_CC_BYPASS_MODE {
	uint32_t instanceID;
	enum VIDEO_DECODER_CC_BYPASS_MODE cc_mode;
};
typedef struct VIDEO_RPC_DEC_CC_BYPASS_MODE VIDEO_RPC_DEC_CC_BYPASS_MODE;

enum CMPRS_RATIO {
	CMPRS_RATIO_50 = 0,
	CMPRS_RATIO_75 = 1,
};
typedef enum CMPRS_RATIO CMPRS_RATIO;

struct VIDEO_RPC_DEC_CMPRS_CTRL {
	uint32_t instanceID;
	uint8_t mode;
	enum CMPRS_RATIO ratio;
	uint8_t enable;
};
typedef struct VIDEO_RPC_DEC_CMPRS_CTRL VIDEO_RPC_DEC_CMPRS_CTRL;

struct VIDEO_RPC_DEC_BITSTREAM_BUFFER {
	uint32_t bsBase;
	uint32_t bsSize;
	enum VIDEO_VF_TYPE type;
};
typedef struct VIDEO_RPC_DEC_BITSTREAM_BUFFER VIDEO_RPC_DEC_BITSTREAM_BUFFER;

struct VIDEO_RPC_DEC_BV_RESULT {
	uint32_t bitRate;
	uint32_t type;
};
typedef struct VIDEO_RPC_DEC_BV_RESULT VIDEO_RPC_DEC_BV_RESULT;

struct VIDEO_RPC_DEC_PV_RESULT {
	uint32_t width;
	uint32_t height;
	uint32_t bit_depth;
	uint32_t DPB_size;
};
typedef struct VIDEO_RPC_DEC_PV_RESULT VIDEO_RPC_DEC_PV_RESULT;

enum VIDEO_RESOURCE_CORE_TYPE {
        VIDEO_RESOURCE_CORE_REALTEK = 0,
        VIDEO_RESOURCE_CORE_GOOGLE = 0 + 1,
        VIDEO_RESOURCE_CORE_IP1 = 0 + 2,
        VIDEO_RESOURCE_CORE_IP2 = 0 + 3,
        VIDEO_RESOURCE_CORE_DUAL = 0 + 4,
};
typedef enum VIDEO_RESOURCE_CORE_TYPE VIDEO_RESOURCE_CORE_TYPE;

struct VIDEO_RPC_RESOURCE_INFO {
	int32_t resource_ctrl_sets;
	enum VIDEO_RESOURCE_CORE_TYPE core_type;
	int32_t video_port;
	int32_t max_width;
	int32_t max_height;
	uint32_t instanceID;
	int32_t width;
	int32_t height;
	int32_t framerate;
	int32_t second_resource_ctrl_sets;
};
typedef struct VIDEO_RPC_RESOURCE_INFO VIDEO_RPC_RESOURCE_INFO;

typedef int HRESULT;
#define VIDEO_RPC_DEC_ToSystem_FatalError 63
#define VIDEO_RPC_DEC_ToSystem_Deliver_MediaInfo 1020
#define VIDEO_RPC_ToSystem_VoutMessage 1021
#define REPLYID 99 // for registering the Reply_Handler
#endif