ccflags-y += -DANDROID -DENABLE_SHOW_VIDEO_INFO
ccflags-y += -Werror -Wuninitialized  -Wmaybe-uninitialized -Wstrict-prototypes
ifeq ($(CONFIG_ENABLE_CC), y)
	ccflags-y += -DVPU_GET_CC
endif
ccflags-y += -I$(srctree)/drivers/soc/realtek/rtd16xxb/rtk_ve/ve1

rtkvdec-y := legacy/drv_if.o \
	legacy/vpu.o \
	legacy/rtkve1enc_v4l2.o \
	legacy/rtkve1enc_enc.o \
	legacy/rtkve1_common.o
rtkve1-y := legacy/ve1_v4l2.o \
	legacy/ve1_decode.o \
	legacy/ve1_mem.o \
	legacy/ve1_mm.o \
	legacy/ve1_vdi.o \
	legacy/ve1_vdi_osal.o \
	legacy/ve1_vpu.o \
	legacy/ve1_product.o \
	legacy/ve1_vpuapifunc.o \
	legacy/ve1_vpuapi.o \
	legacy/ve1_wrapper.o
rtkve2-y := legacy/ve2.o \
	legacy/ve2rpc.o
rtkve_enc-y := rtkve/enc/rtkve-common.o        \
	rtkve/enc/rtkve-rpc.o   \
	rtkve/enc/rtkve-stateful-vpu-enc.o      \
	rtkve/enc/rtkve-enc-v4l2.o      \
	rtkve/enc/rtkve-enc-drv.o

ifeq ($(CONFIG_RTK_V4L2_VDEC), y)
obj-m += rtkvdec.o rtkve1.o rtkve2.o
endif

ifeq ($(CONFIG_RTK_V4L2_VENC), y)
obj-m += rtkve_enc.o
endif

ifdef CONFIG_COMPAT
	rtkve1-y       += legacy/ve1_mem_compat.o
endif
