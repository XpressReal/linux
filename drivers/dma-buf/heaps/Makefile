# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_DMABUF_HEAPS_DEFERRED_FREE) += deferred-free-helper.o
obj-$(CONFIG_DMABUF_HEAPS_SYSTEM)	+= system_heap.o
obj-$(CONFIG_DMABUF_HEAPS_CMA)		+= cma_heap.o
obj-$(CONFIG_DMABUF_HEAPS_REALTEK)	+= rtk_media_heaps.o
rtk_media_heaps-y			+= rtk_media_heap.o rtk_heap_debug.o \
					   rtk_heap_device.o rtk_heap_helpers.o \
					   rtk_heap_dma.o rtk_protect.o
ccflags-y				+= -I$(srctree)/mm
