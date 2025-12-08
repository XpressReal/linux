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


#include "gc_hal_kernel_linux.h"

#define _GC_OBJ_ZONE gcvZONE_OS

#if gcdENABLE_TRUST_APPLICATION

gceSTATUS
gckOS_OpenSecurityChannel(gckOS Os, gceCORE Core, gctUINT32 *Channel)
{
    *Channel = Core + 1;
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_InitSecurityChannel(gctUINT32 Channel)
{
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_CloseSecurityChannel(gctUINT32 Channel)
{
    return gcvSTATUS_OK;
}

extern gceSTATUS
TAEmulator(gceCORE, void *);

gceSTATUS
gckOS_CallSecurityService(gctUINT32 Channel, gcsTA_INTERFACE *Interface)
{
    gceCORE core;
    gceSTATUS status;

    gcmkHEADER();
    gcmkVERIFY_ARGUMENT(Channel != 0);

    core = (gceCORE)(Channel - 1);

    TAEmulator(core, Interface);

    status = Interface->result;

    gcmkFOOTER();
    return status;
}

#endif
