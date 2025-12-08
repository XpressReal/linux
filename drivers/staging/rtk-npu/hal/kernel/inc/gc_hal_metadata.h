/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2023 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************/


#ifndef __gc_hal_kernel_metadata_h_
#define __gc_hal_kernel_metadata_h_

#ifdef __cplusplus
extern "C" {
#endif

/* Macro to combine four characters into a Character Code. */
#define __FOURCC(a, b, c, d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define VIV_VIDMEM_METADATA_MAGIC __FOURCC('v', 'i', 'v', 'm')

/* Metadata for cross-device fd share with additional (ts) info. */
typedef struct _VIV_VIDMEM_METADATA {
    uint32_t magic;

    int32_t  ts_fd;
    void     *ts_dma_buf;

    uint32_t fc_enabled;
    uint32_t fc_value;
    uint32_t fc_value_upper;

    uint32_t compressed;
    uint32_t compress_format;
} _VIV_VIDMEM_METADATA;

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_kernel_metadata_h_ */


