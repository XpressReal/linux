/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rtk_drm_vowb

#if !defined(_RTK_DRM_VOWB_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _RTK_DRM_VOWB_TRACE_H_

#include <linux/types.h>
#include <linux/device.h>
#include <linux/tracepoint.h>
#include "rtk_drm_vowb.h"

#define RTK_DRM_VOWB_JOB_STATUS_LIST \
	{RTK_DRM_VOWB_JOB_STATUS_UNDEFINED, "UNDEFINED"}, \
	{RTK_DRM_VOWB_JOB_STATUS_START, "START"}, \
	{RTK_DRM_VOWB_JOB_STATUS_DONE, "DONE"},   \
	{RTK_DRM_VOWB_JOB_STATUS_TIMEOUT, "TIMEOUT"}

TRACE_EVENT(vowb_job_update_status,
	TP_PROTO(const struct rtk_drm_vowb_job *job),

	TP_ARGS(job),

	TP_STRUCT__entry(
		__field(const struct rtk_drm_vowb_job *, job)
		__field(u64, job_id)
		__field(u32, status)
	),

	TP_fast_assign(
		__entry->job    = job;
		__entry->job_id = job->job_id;
		__entry->status = job->status;
	),

	TP_printk("job=%p, id=%lld, status=%s",  __entry->job,  __entry->job_id,
		 __print_symbolic(__entry->status, RTK_DRM_VOWB_JOB_STATUS_LIST)
	)
);

TRACE_EVENT(vowb_update_resp_job_id,
	TP_PROTO(u64 resp_job_id),

	TP_ARGS(resp_job_id),

	TP_STRUCT__entry(
		__field(u64, resp_job_id)
	),

	TP_fast_assign(
		__entry->resp_job_id = resp_job_id;
	),

	TP_printk("resp_job_id=%lld", __entry->resp_job_id)
);

TRACE_EVENT(vowb_func1_statistics,
	TP_PROTO(const char *func, u64 cnt_display, u64 cnt_vowb),

	TP_ARGS(func, cnt_display, cnt_vowb),

	TP_STRUCT__entry(
		__string(func, func)
		__field(u64, cnt_display)
		__field(u64, cnt_vowb)
	),

	TP_fast_assign(
		__assign_str(func, func);
		__entry->cnt_display = cnt_display;
		__entry->cnt_vowb = cnt_vowb;
	),

	TP_printk("%s: cnt_display=%lld, cnt_vowb=%lld",
		__get_str(func), __entry->cnt_display, __entry->cnt_vowb)
);

TRACE_EVENT(vowb_func1_warn_resp_us,
	TP_PROTO(u32 resp_us),

	TP_ARGS(resp_us),

	TP_STRUCT__entry(
		__field(u32, resp_us)
	),

	TP_fast_assign(
		__entry->resp_us = resp_us;
	),

	TP_printk("resp_us=%d", __entry->resp_us)
);

#endif /* _RTK_DRM_VOWB_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/realtek
#define TRACE_INCLUDE_FILE rtk_drm_vowb_trace
#include <trace/define_trace.h>

