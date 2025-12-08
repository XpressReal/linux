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


#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
# include <linux/stdarg.h>
#else
# include <stdarg.h>
#endif

#ifndef __gc_hal_kernel_debugfs_h_
# define __gc_hal_kernel_debugfs_h_

# define MAX_LINE_SIZE   768 /* Max bytes for a line of debug info */

typedef struct _gcsDEBUGFS_DIR *gckDEBUGFS_DIR;
typedef struct _gcsDEBUGFS_DIR {
    struct dentry       *root;
    struct list_head     nodeList;
} gcsDEBUGFS_DIR;

typedef struct _gcsINFO {
    const char          *name;
    int                 (*show)(struct seq_file *m, void *data);
    int                 (*write)(const char __user *buf, size_t count, void *data);
} gcsINFO;

typedef struct _gcsINFO_NODE {
    gcsINFO             *info;
    gctPOINTER           device;
    struct dentry       *entry;
    struct list_head     head;
} gcsINFO_NODE;

gceSTATUS
gckDEBUGFS_DIR_Init(gckDEBUGFS_DIR  Dir, struct dentry *root, gctCONST_STRING Name);

gceSTATUS
gckDEBUGFS_DIR_CreateFiles(gckDEBUGFS_DIR Dir, gcsINFO *List,
                           int count, gctPOINTER Data);

gceSTATUS
gckDEBUGFS_DIR_RemoveFiles(gckDEBUGFS_DIR Dir, gcsINFO *List, int count);

void
gckDEBUGFS_DIR_Deinit(gckDEBUGFS_DIR Dir);

#endif /* __gc_hal_kernel_debugfs_h_ */
