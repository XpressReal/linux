//------------------------------------------------------------------------------
// File: config.h
//
// Copyright (c) 2006, Chips & Media.  All rights reserved.
// This file should be modified by some developers of C&M according to product version.
//------------------------------------------------------------------------------
#ifndef __CONFIG_H__
#define __CONFIG_H__

#define PLATFORM_LINUX
#if defined(_MSC_VER)
#include <windows.h>
#define inline _inline
#elif defined(__GNUC__)
#elif defined(__ARMCC__)
#else
#error "Unknown compiler."
#endif

#define API_VERSION_MAJOR 5
#define API_VERSION_MINOR 5
#define API_VERSION_PATCH 38
#define API_VERSION                                                            \
	((API_VERSION_MAJOR << 16) | (API_VERSION_MINOR << 8) |                \
	 API_VERSION_PATCH)

//------------------------------------------------------------------------------
// COMMON
//------------------------------------------------------------------------------

// do not define BIT_CODE_FILE_PATH in case of multiple product support. because wave410 and coda980 has different firmware binary format.
#define CORE_0_BIT_CODE_FILE_PATH "coda960.out" // for coda960
#define CORE_1_BIT_CODE_FILE_PATH "/vendor/lib/fw/ve1.bin" // for coda980

//------------------------------------------------------------------------------
// OMX
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// CODA960
//------------------------------------------------------------------------------
#define SUPPORT_ENC_NV21

//------------------------------------------------------------------------------
// WAVE512
//------------------------------------------------------------------------------
#define NO_COMMAND_QUEUE

//------------------------------------------------------------------------------
// CUSTOMER
//------------------------------------------------------------------------------
#define FIX_SET_GET_RD_PTR_BUG

#define USE_OS_SCHEDULE_YIELD

#define FIX_PEDING_INSTANCE_CHECK_BUG
#define USE_TIMESTAMP_FOR_MULTI_INSTANCE

#define SUPPORT_GET_NAL_START_POS

#endif /* __CONFIG_H__ */
