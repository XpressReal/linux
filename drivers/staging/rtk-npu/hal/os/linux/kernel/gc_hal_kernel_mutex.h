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


#ifndef _gc_hal_kernel_mutex_h_
#define _gc_hal_kernel_mutex_h_

#include "gc_hal.h"
#include <linux/mutex.h>

#if IS_ENABLED(CONFIG_PROVE_LOCKING) && (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0))

struct key_mutex {
    struct mutex mut;
    struct lock_class_key key;
};


#define gckOS_CreateMutex(Os, Mutex)                                               \
({                                                                                 \
    /* Allocate the mutex structure. */                                            \
    struct key_mutex *key_mut;                                                     \
    gceSTATUS _status = gckOS_Allocate(Os, gcmSIZEOF(struct key_mutex), (gctPOINTER *) &key_mut); \
                                                                                   \
    if (gcmIS_SUCCESS(_status)) {                                                  \
        /* Initialize the mutex. */                                                \
        lockdep_register_key(&key_mut->key);                                       \
        __mutex_init((&key_mut->mut), #Mutex, (&key_mut->key));                    \
    }                                                                              \
    *(Mutex) = (gctPOINTER)key_mut;                                                \
    _status;                                                                       \
})
#else

/* Create a new mutex. */
#define gckOS_CreateMutex(Os, Mutex)                                        \
({                                                                          \
    /* Allocate the mutex structure. */                                     \
    gceSTATUS _status = gckOS_Allocate(Os, gcmSIZEOF(struct mutex), Mutex); \
                                                                            \
    if (gcmIS_SUCCESS(_status)) {                                           \
        /* Initialize the mutex. */                                         \
        mutex_init(*(struct mutex **)Mutex);                                \
    }                                                                       \
                                                                            \
    _status;                                                                \
})
#endif

#endif
