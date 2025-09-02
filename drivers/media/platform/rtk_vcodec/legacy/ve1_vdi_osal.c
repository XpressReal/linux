//------------------------------------------------------------------------------
// File: ve1_vdi_osal.c
//
// Copyright (c) 2006, Chips & Media.  All rights reserved.
//------------------------------------------------------------------------------
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/string.h>

#include "ve1config.h"
#include "ve1_vdi_osal.h"
static int log_colors[MAX_LOG_LEVEL] = {
	0,
	TERM_COLOR_R | TERM_COLOR_G | TERM_COLOR_B | TERM_COLOR_BRIGHT, //INFO
	TERM_COLOR_R | TERM_COLOR_B | TERM_COLOR_BRIGHT, //WARN
	TERM_COLOR_R | TERM_COLOR_BRIGHT, // ERR
	TERM_COLOR_R | TERM_COLOR_G | TERM_COLOR_B //TRACE
};

static unsigned log_decor = LOG_HAS_TIME | LOG_HAS_FILE | LOG_HAS_MICRO_SEC |
			    LOG_HAS_NEWLINE | LOG_HAS_SPACE | LOG_HAS_COLOR;
static int max_log_level = MAX_LOG_LEVEL;

static void term_restore_color(void);
static void term_set_color(int color);
#ifdef SUPPORT_SW_UART
static pthread_mutex_t s_log_mutex;
#endif
int InitLog(void)
{
#ifdef SUPPORT_SW_UART
	pthread_mutex_init(&s_log_mutex, NULL);
#endif
	return 1;
}

void DeInitLog(void)
{
#ifdef SUPPORT_SW_UART
	pthread_mutex_destroy(&s_log_mutex);
#endif
}

void SetLogColor(int level, int color)
{
	log_colors[level] = color;
}

int GetLogColor(int level)
{
	return log_colors[level];
}

void SetLogDecor(int decor)
{
	log_decor = decor;
}

int GetLogDecor(void)
{
	return log_decor;
}

void SetMaxLogLevel(int level)
{
	max_log_level = level;
}
int GetMaxLogLevel(void)
{
	return max_log_level;
}

void LogMsg(int level, const char *format, ...)
{
	va_list ptr;
	char logBuf[MAX_PRINT_LENGTH] = { 0 };
#if !defined(ANDROID)
	char logBuf_tag[MAX_PRINT_LENGTH] = { 0 };
#endif

	if (level > max_log_level)
		return;
#ifdef SUPPORT_SW_UART
	pthread_mutex_lock(&s_log_mutex);
#endif

	va_start(ptr, format);
#if defined(WIN32) || defined(__MINGW32__)
	_vsnprintf(logBuf, MAX_PRINT_LENGTH, format, ptr);
#else
	vsnprintf(logBuf, MAX_PRINT_LENGTH, format, ptr);
#endif
	va_end(ptr);

	if (log_decor & LOG_HAS_COLOR)
		term_set_color(log_colors[level]);

#ifdef ANDROID
	if (level == ERR) {
		pr_err("%s %s", VLOG_TAG, logBuf);
	} else if (level == INFO) {
		pr_info("%s %s", VLOG_TAG, logBuf);
	} else {
		pr_debug("%s %s", VLOG_TAG, logBuf);
	}
#else
	snprintf(logBuf_tag, MAX_PRINT_LENGTH, "%s %s", VLOG_TAG, logBuf);
	fputs(logBuf_tag, stdout);
#endif

	if (log_decor & LOG_HAS_COLOR)
		term_restore_color();
#ifdef SUPPORT_SW_UART
	pthread_mutex_unlock(&s_log_mutex);
#endif
}

static void term_set_color(int color)
{
	/* put bright prefix to ansi_color */
	char ansi_color[12] = "\033[01;3";

	return;

	if (color & TERM_COLOR_BRIGHT)
		color ^= TERM_COLOR_BRIGHT;
	else
		strcpy(ansi_color, "\033[00;3");

	switch (color) {
	case 0:
		/* black color */
		strcat(ansi_color, "0m");
		break;
	case TERM_COLOR_R:
		/* red color */
		strcat(ansi_color, "1m");
		break;
	case TERM_COLOR_G:
		/* green color */
		strcat(ansi_color, "2m");
		break;
	case TERM_COLOR_B:
		/* blue color */
		strcat(ansi_color, "4m");
		break;
	case TERM_COLOR_R | TERM_COLOR_G:
		/* yellow color */
		strcat(ansi_color, "3m");
		break;
	case TERM_COLOR_R | TERM_COLOR_B:
		/* magenta color */
		strcat(ansi_color, "5m");
		break;
	case TERM_COLOR_G | TERM_COLOR_B:
		/* cyan color */
		strcat(ansi_color, "6m");
		break;
	case TERM_COLOR_R | TERM_COLOR_G | TERM_COLOR_B:
		/* white color */
		strcat(ansi_color, "7m");
		break;
	default:
		/* default console color */
		strcpy(ansi_color, "\033[00m");
		break;
	}

}

static void term_restore_color(void)
{
	term_set_color(log_colors[4]);
}

#define VE1_ALIGN(value, base) (((value) + ((base)-1)) & ~((base)-1))
#define DEVICE_ALIGN (0x8) /* for non-cacheable memory */
void *osal_memcpy(void *dst, const void *src, int count)
{
	if ((count % DEVICE_ALIGN) != 0 || ((long)dst % DEVICE_ALIGN) != 0) {
		unsigned char *cDst = (unsigned char *)dst;
		unsigned char *cSrc = (unsigned char *)src;
		int copyCount = count;
		if (((long)cDst % DEVICE_ALIGN) != 0) {
			int i;
			long copySize = VE1_ALIGN(((long)cDst), DEVICE_ALIGN) -
					(long)cDst;
			for (i = 0; i < (int)copySize; i++) {
				cDst[0] = cSrc[0];
				cDst += 1;
				cSrc += 1;
				copyCount -= 1;
			}
		}

		if ((copyCount % DEVICE_ALIGN) != 0) {
			int i;
			long copySize = copyCount % DEVICE_ALIGN;
			for (i = (copyCount - copySize); i < (int)copyCount;
			     i++)
				cDst[i] = cSrc[i];
			copyCount -= copySize;
		}

		memcpy((void *)cDst, (void *)cSrc, copyCount);
		return dst;
	} else {
		return memcpy(dst, src, count);
	}
}

int osal_memcmp(const void *src, const void *dst, int size)
{
	return memcmp(src, dst, size);
}

void *osal_memset(void *dst, int val, int count)
{
	if (((long)dst % DEVICE_ALIGN) != 0) {
		unsigned char *cDst = (unsigned char *)dst;
		int copyCount = count;
		if (((long)cDst % DEVICE_ALIGN) != 0) {
			int i;
			long copySize = VE1_ALIGN(((long)cDst), DEVICE_ALIGN) -
					(long)cDst;
			for (i = 0; i < (int)copySize; i++) {
				cDst[0] = (unsigned char)(val & 0xff);
				cDst += 1;
				copyCount -= 1;
			}
		}

		if ((copyCount % DEVICE_ALIGN) != 0) {
			int i;
			long copySize = copyCount % DEVICE_ALIGN;
			for (i = (copyCount - copySize); i < (int)copyCount;
			     i++)
				cDst[i] = (unsigned char)(val & 0xff);
			copyCount -= copySize;
		}

		memset((void *)cDst, val, copyCount);
		return dst;
	} else
		return memset(dst, val, count);
}

void *osal_malloc(int size)
{
	return vmalloc(size);
}

void osal_free(void *p)
{
	vfree(p);
}

//------------------------------------------------------------------------------
// math related api
//------------------------------------------------------------------------------
#ifndef I64
typedef long long I64;
#endif

// 32 bit / 16 bit ==> 32-n bit remainder, n bit quotient
static int fixDivRq(int a, int b, int n)
{
	I64 c;
	I64 a_36bit;
	I64 mask, signBit, signExt;
	int i;

	// DIVS emulation for BPU accumulator size
	// For SunOS build
	mask = 0x0F;
	mask <<= 32;
	mask |= 0x00FFFFFFFF; // mask = 0x0FFFFFFFFF;
	signBit = 0x08;
	signBit <<= 32; // signBit = 0x0800000000;
	signExt = 0xFFFFFFF0;
	signExt <<= 32; // signExt = 0xFFFFFFF000000000;

	a_36bit = (I64)a;

	for (i = 0; i < n; i++) {
		c = a_36bit - (b << 15);
		if (c >= 0)
			a_36bit = (c << 1) + 1;
		else
			a_36bit = a_36bit << 1;

		a_36bit = a_36bit & mask;
		if (a_36bit & signBit)
			a_36bit |= signExt;
	}

	a = (int)a_36bit;
	return a; // R = [31:n], Q = [n-1:0]
}

int math_div(int number, int denom)
{
	int c;
	c = fixDivRq(number, denom, 17); // R = [31:17], Q = [16:0]
	c = c & 0xFFFF;
	c = (c + 1) >> 1; // round
	return (c & 0xFFFF);
}

