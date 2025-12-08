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


#ifndef __gc_hal_kernel_hardware_func_flop_reset_h_
#define __gc_hal_kernel_hardware_func_flop_reset_h_
#ifdef __cplusplus
extern "C" {
#endif
#include "gc_hal.h"
#include "gc_hal_kernel.h"
#include "gc_hal_kernel_hardware.h"

gceSTATUS
gckHARDWARE_ResetFlopWithPPU(gckHARDWARE Hardware,
                             gctUINT32 AllocFlag,
                             gcePOOL *Pool,
                             gcsFUNCTION_COMMAND_PTR Command);

gceSTATUS
gckHARDWARE_ResetFlopWithNN(gckHARDWARE Hardware,
                            gctUINT32 AllocFlag,
                            gcePOOL *Pool,
                            gcsFUNCTION_COMMAND_PTR Command);

gceSTATUS
gckHARDWARE_ResetFlopWithTP(gckHARDWARE Hardware,
                            gctUINT32 AllocFlag,
                            gcePOOL *Pool,
                            gcsFUNCTION_COMMAND_PTR Command);
#ifdef __cplusplus
}
#endif

#ifndef gcdENABLE_FLOP_RESET_DEBUG
#define gcdENABLE_FLOP_RESET_DEBUG 0
#endif

#endif

