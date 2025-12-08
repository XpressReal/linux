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


#ifndef __gc_hal_kernel_allocator_array_h_
#define __gc_hal_kernel_allocator_array_h_

extern gceSTATUS
_GFPAlloctorInit(gckOS Os, gcsDEBUGFS_DIR *Parent, gckALLOCATOR *Allocator);

extern gceSTATUS
_UserMemoryAlloctorInit(gckOS Os, gcsDEBUGFS_DIR *Parent, gckALLOCATOR *Allocator);

extern gceSTATUS
_ReservedMemoryAllocatorInit(gckOS Os, gcsDEBUGFS_DIR *Parent, gckALLOCATOR *Allocator);

#ifdef CONFIG_DMA_SHARED_BUFFER
extern gceSTATUS
_DmabufAlloctorInit(gckOS Os, gcsDEBUGFS_DIR *Parent, gckALLOCATOR *Allocator);
#endif

#ifndef NO_DMA_COHERENT
extern gceSTATUS
_DmaAlloctorInit(gckOS Os, gcsDEBUGFS_DIR *Parent, gckALLOCATOR *Allocator);
#endif

/* Default allocator entry. */
gcsALLOCATOR_DESC allocatorArray[] = {
    /* GFP allocator. */
    gcmkDEFINE_ALLOCATOR_DESC("gfp", _GFPAlloctorInit),

    /* User memory importer. */
    gcmkDEFINE_ALLOCATOR_DESC("user", _UserMemoryAlloctorInit),

#ifdef CONFIG_DMA_SHARED_BUFFER
    /* Dmabuf allocator. */
    gcmkDEFINE_ALLOCATOR_DESC("dmabuf", _DmabufAlloctorInit),
#endif

#ifndef NO_DMA_COHERENT
    gcmkDEFINE_ALLOCATOR_DESC("dma", _DmaAlloctorInit),
#endif

    gcmkDEFINE_ALLOCATOR_DESC("reserved-mem", _ReservedMemoryAllocatorInit),
};

#endif
