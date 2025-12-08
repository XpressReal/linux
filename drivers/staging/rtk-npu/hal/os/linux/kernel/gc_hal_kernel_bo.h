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


#if gcdENABLE_TTM
#ifndef __gc_hal_kernel_bo_h_
#define __gc_hal_kernel_bo_h_

void
gckDRM_SetPlaceFromDomain(struct _viv_bo *bo, gctUINT32 domain);

void
gckDRM_BOUnreserve(struct _viv_bo *bo);

int
gckDRM_BOReserve(struct _viv_bo *bo, gctBOOL no_irq);

void
gckDRM_BODestroy(struct ttm_buffer_object *tbo);

int
gckDRM_BOCreateKernelResv(gckGALDEVICE dev,
                          gctUINT64 size,
                          gctINT32 align,
                          gctUINT32 domain,
                          struct _viv_bo **bo_ptr,
                          gctUINT64 *address,
                          gctPOINTER *logical);

gctINT32
gckDRM_BOCreate(gckGALDEVICE dev,
                struct viv_bo_param *bp,
                struct _viv_bo **bo_ptr);


gceSTATUS
gckDRM_BOLink2NodeObj(gckGALDEVICE gal_dev, gctUINT32 processID, gctUINT32 nodeHandle,
                      gckVIDMEM_NODE nodeObject, struct _viv_bo *bo);

gceSTATUS
gckDRM_BOUnlink2NodeObj(gckGALDEVICE gal_dev, gctUINT32 processID, gctUINT32 nodeHandle);

gceSTATUS
gckDRM_BOGetFromNodeHandle(gckGALDEVICE gal_dev, gctUINT32 processID,
                           gctUINT32 nodeHandle,
                           struct _viv_bo **bo);

gceSTATUS
gckDRM_BOValidate(gckGALDEVICE dev, struct _viv_bo *bo, gctUINT32 domain);

gceSTATUS
gckDRM_BOMove(gckGALDEVICE dev,
                gcsDMA_TRANS_INFO *info);

gceSTATUS
gckDRM_BOMoveClean(gckGALDEVICE gal_dev, struct _viv_bo *bo, struct ttm_mem_reg *new_reg);

gceSTATUS
gckDRM_BOLock(gckGALDEVICE gal_dev, struct _viv_bo *bo, gctBOOL cacheable,
              gctUINT64_PTR logical, gctUINT32_PTR address, gcsHAL_INTERFACE *interface);

gceSTATUS
gckDRM_BOUnlock(gckGALDEVICE gal_dev, struct _viv_bo *bo, gctUINT32 nodeHandle, gctBOOL *async, gctBOOL bottom);

void
gckDRM_BORevokeMap(gckVIDMEM_NODE nodeObj);

#endif
#endif /* gcdENABLE_TTM */
