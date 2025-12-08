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


#ifndef __gc_hal_base_shared_h_
#define __gc_hal_base_shared_h_

#ifdef __cplusplus
extern "C" {
#endif

#define gcdBINARY_TRACE_MESSAGE_SIZE    240

typedef struct _gcsBINARY_TRACE_MESSAGE *gcsBINARY_TRACE_MESSAGE_PTR;
typedef struct _gcsBINARY_TRACE_MESSAGE {
    gctUINT32   signature;
    gctUINT32   pid;
    gctUINT32   tid;
    gctUINT32   line;
    gctUINT32   numArguments;
    gctUINT8    payload;
} gcsBINARY_TRACE_MESSAGE;

/* gcsOBJECT object defintinon. */
typedef struct _gcsOBJECT {
    /* Type of an object. */
    gceOBJECT_TYPE      type;
} gcsOBJECT;

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_base_shared_h_ */


