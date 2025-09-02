//------------------------------------------------------------------------------
// File: vdi.c
//
// Copyright (c) 2006, Chips & Media.  All rights reserved.
//------------------------------------------------------------------------------

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/sched.h>

#include "ve1.h"
#include "ve1_vdi.h"
#include "ve1_vdi_osal.h"
#include "ve1_regdefine.h"
#include "drv_if.h"

#define VPU_DEVICE_NAME "/dev/vpu"
#define RTK_VPU_DEVICE_NAME "/rtk/vpu"

typedef struct mutex MUTEX_HANDLE;
typedef struct new_pthread_mutex_t {
	uint32_t val[10];
} new_pthread_mutex_t;
typedef new_pthread_mutex_t USERSPACE_MUTEX_HANDLE;
#ifndef ANDROID
// for gLinux, using mmap.
#define mmap64 mmap
#endif

#define VE1_PROT_CTRL 0x3050
#define VE1_CTRL 0x3000

#define SUPPORT_INTERRUPT
#define VPU_BIT_REG_SIZE 0xC000
#define VDI_SRAM_BASE_ADDR                                                     \
	0x00000000 // if we can know the sram address in SOC directly for vdi layer. it is possible to set in vdi layer without allocation from driver
#define VDI_SRAM_SIZE 0x1D000 // RTK SRAM MAX size for stark(116kB)
#define VDI_CODA9_SRAM_SIZE 0x1D000
#define VDI_SYSTEM_ENDIAN VDI_LITTLE_ENDIAN
#define VDI_128BIT_BUS_SYSTEM_ENDIAN VDI_128BIT_LITTLE_ENDIAN
#define VDI_NUM_LOCK_HANDLES 5

/* RTK, begin */
typedef struct vpu_sram_info {
	int inited;
	int owner_core;
	unsigned long generation;
} vpu_sram_info;

typedef struct vpu_sram_device {
	vpu_sram_info *info;
	struct mutex *lock;
	unsigned long generation;
	vpu_instance_pool_t *pvip[MAX_NUM_VPU_CORE];
} vpu_sram_device;

static DEFINE_MUTEX(vdi_info_mutex_init_lock);
static struct mutex vdi_info_mutex[MAX_NUM_VPU_CORE];
static struct mutex vdi_init_release_lock[MAX_NUM_VPU_CORE];

static int vdi_info_mutex_init = 0;

static struct mutex vpu_mutex;
static struct mutex vpu_disp_mutex;
static struct mutex vpu_sram_mutex;
static struct mutex vpu_thumb_mutex;

/* RTK, end */
// kernel 5.15, redefinition in ve1.h, so rename vpudrv_buffer_pool_t to vdi_vpudrv_buffer_pool_t
typedef struct vdi_vpudrv_buffer_pool_t {
	vpudrv_buffer_t vdb;
	int inuse;
} vdi_vpudrv_buffer_pool_t;

typedef struct {
	unsigned long core_idx;
	unsigned int product_code;
	int vpu_fd;
	vpu_instance_pool_t *pvip;
	int task_num;
	int clock_state;
	vpudrv_buffer_t vdb_register;
	vpu_buffer_t vpu_common_memory;
	vpu_buffer_t vpu_common_memory_protect;
	vdi_vpudrv_buffer_pool_t vpu_buffer_pool[MAX_VPU_BUFFER_POOL];
	vpudrv_buffer_t
		vdb_pvip; /* RTK, we didn't store pvip information into vpu_buffer_pool */
	int vpu_buffer_pool_count;
	unsigned int asic_id;
	vpudrv_buffer_t dcsys_register;
	vpudrv_buffer_t dmcsys_register;

	struct mutex *vpu_mutex;
	struct mutex *vpu_disp_mutex;
	struct mutex *vpu_sram_mutex;
	struct mutex *vpu_thumb_mutex;

	vpu_sram_device *sram_dev;
	int *thumb_used;
	int *thumb_num;

	struct mutex *ve1_hw_mutex;
	u32 *ve1_instance_nums;
} vdi_info_t;

static vdi_info_t s_vdi_info[MAX_NUM_VPU_CORE];
/* a mutex protecting the wrapper init */

/* define internal function begin */
static int Internal_swap_endian(unsigned long core_idx, unsigned char *data,
				int len, int endian);
static int Internal_allocate_common_memory(unsigned long core_idx);
static int Internal_vdi_set_bit_firmware_to_pm(unsigned long core_idx,
					       const unsigned short *code);
static int Internal_vdi_release(unsigned long core_idx);
static int Internal_vdi_lock(unsigned long core_idx);
static void Internal_vdi_set_rtk_clk_gating(Uint32 coreIdx, BOOL clk_en);
static unsigned int Internal_vdi_read_register(unsigned long core_idx,
					       unsigned int addr);
static void Internal_vdi_write_register(unsigned long core_idx,
					unsigned int addr, unsigned int data);
static int Internal_vdi_set_clock_gate(unsigned long core_idx, int enable);
static void Internal_vdi_unlock(unsigned long core_idx);
static void Internal_vdi_disp_unlock(unsigned long core_idx);
static int Internal_vdi_allocate_dma_memory(unsigned long core_idx,
					    vpu_buffer_t *vb, void *filp);
static int Internal_vdi_allocate_dma_memory_no_mmap(unsigned long core_idx,
						    vpu_buffer_t *vb,
						    void *filp);
static void Internal_vdi_free_dma_memory(unsigned long core_idx,
					 vpu_buffer_t *vb);
static void Internal_vdi_free_dma_memory_no_mmap(unsigned long core_idx,
						 vpu_buffer_t *vb);
static int Internal_vdi_convert_endian(unsigned long core_idx,
				       unsigned int endian);
static int Internal_vdi_write_memory(unsigned long core_idx, unsigned int addr,
				     unsigned char *data, int len, int endian);
static int Internal_vdi_get_total_instance_num(unsigned long core_idx);
static void Internal_vdi_set_thumb_num(unsigned long core_idx,
				       unsigned int enable);
static unsigned int Internal_vdi_get_thumb_num(unsigned long core_idx);
static unsigned int Internal_vdi_set_thumb_used(unsigned long core_idx,
						unsigned int enable);
static int Internal_vdi_get_common_memory(unsigned long core_idx,
					  vpu_buffer_t *vb);
static int Internal_vdi_get_common_memory_protect(unsigned long core_idx,
						  vpu_buffer_t *vb, void *filp);
static vpu_instance_pool_t *
Internal_vdi_get_instance_pool(unsigned long core_idx);
static int Internal_vdi_open_instance(unsigned long core_idx,
				      unsigned long inst_idx, void *filp);
static int Internal_vdi_close_instance(unsigned long core_idx,
				       unsigned long inst_idx);
static int Internal_vdi_get_instance_num(unsigned long core_idx);
static int Internal_vdi_hw_reset(unsigned long core_idx);
static void Internal_vdi_check_hwreset(unsigned long core_idx);
static int restore_mutex_in_dead(MUTEX_HANDLE *mutex);
static int Internal_vdi_init(unsigned long core_idx, void *videc_dev);

//#define DEBUG_VDI_INFO_LOCK
#ifdef DEBUG_VDI_INFO_LOCK
#define lock_vdi_info(core_idx) lock_vdi_info_debug(core_idx, __FUNCTION__)
#define unlock_vdi_info(vdi, core_idx)                                         \
	unlock_vdi_info_debug(vdi, core_idx, __FUNCTION__)
static vdi_info_t *lock_vdi_info_debug(unsigned long core_idx,
				       const char *parent_name);
static void unlock_vdi_info_debug(vdi_info_t *vdi, unsigned long core_idx,
				  const char *parent_name);
#else
static vdi_info_t *lock_vdi_info(unsigned long core_idx);
static void unlock_vdi_info(vdi_info_t *vdi, unsigned long core_idx);
#endif
/* define internal function end */

static vpu_sram_device *sram_device_create(vpu_sram_info *info, void *pMutex,
					   int *pMutexState, void *first_pvip,
					   unsigned long pvip_size)
{
	int i;
	int needToInited = 0;
	vpu_sram_device *device =
		(vpu_sram_device *)osal_malloc(sizeof(vpu_sram_device));

	device->info = info;
	device->lock = (struct mutex *)pMutex;
	for (i = 0; i < MAX_NUM_VPU_CORE; i++)
		device->pvip[i] =
			(vpu_instance_pool_t *)((unsigned long)first_pvip +
						(i * pvip_size));

	if (*pMutexState == FALSE) {
		mutex_init(device->lock);
		*pMutexState = TRUE;
		needToInited = 1;
	}

	mutex_lock(device->lock);

	if (device->info->inited == FALSE)
		needToInited = 1;

	if (needToInited) {
		device->info->owner_core = -1;
		device->info->inited = TRUE;
		device->info->generation++;
	}
	mutex_unlock(device->lock);

	return device;
}

static void sram_device_remove(vpu_sram_device *device)
{
	restore_mutex_in_dead((MUTEX_HANDLE *)device->lock);
	mutex_lock(device->lock);
	mutex_unlock(device->lock);
	osal_free(device);
}

static inline void vdi_info_mutex_init_check(void)
{
	mutex_lock(&vdi_info_mutex_init_lock);
	if (vdi_info_mutex_init == 0) {
		Int32 i;
		for (i = 0; i < MAX_NUM_VPU_CORE; i++) {
			mutex_init(&vdi_info_mutex[i]);
			mutex_init(&vdi_init_release_lock[i]);
			memset(&s_vdi_info[i], 0x00, sizeof(vdi_info_t));
			s_vdi_info[i].vpu_fd = -1;
		}
		vdi_info_mutex_init = 1;
	}
	mutex_unlock(&vdi_info_mutex_init_lock);
}

#ifdef DEBUG_VDI_INFO_LOCK
static vdi_info_t *lock_vdi_info_debug(unsigned long core_idx,
				       const char *parent_name)
{
	vdi_info_mutex_init_check();
	if (core_idx >= MAX_NUM_VPU_CORE)
		goto err;
	mutex_lock(&vdi_info_mutex[core_idx]);
	VLOG(ERR, "[%s:%s] core_idx = %ld", __FUNCTION__, parent_name,
	     core_idx);
	return &s_vdi_info[core_idx];
err:
	return NULL;
}

static void unlock_vdi_info_debug(vdi_info_t *vdi, unsigned long core_idx,
				  const char *parent_name)
{
	if (vdi) {
		mutex_unlock(&vdi_info_mutex[core_idx]);
		VLOG(ERR, "[%s:%s] core_idx = %ld", __FUNCTION__, parent_name,
		     core_idx);
	}
	return;
}
#else
static vdi_info_t *lock_vdi_info(unsigned long core_idx)
{
	vdi_info_mutex_init_check();
	if (core_idx >= MAX_NUM_VPU_CORE)
		goto err;
	mutex_lock(&vdi_info_mutex[core_idx]);
	return &s_vdi_info[core_idx];
err:
	return NULL;
}

static void unlock_vdi_info(vdi_info_t *vdi, unsigned long core_idx)
{
	if (vdi) {
		mutex_unlock(&vdi_info_mutex[core_idx]);
	}
	return;
}
#endif

static inline int vdi_pthread_mutex_trylock(unsigned long core_idx, int delayUs,
					    MUTEX_HANDLE *mutex)
{
	int ret;
	unlock_vdi_info(&s_vdi_info[core_idx], core_idx);
	ret = mutex_trylock((struct mutex *)mutex);
	ret = !ret; // mutex_trylock() return 1 means lock, pthread_mutex_trylock() return 0 means lock
	if (ret != 0 && delayUs > 0) {
		usleep_range(delayUs, delayUs);
	}
	lock_vdi_info(core_idx);
	return ret;
}

static void vdi_init_thumb_info(vdi_info_t *vdi, int *pMutexState,
				void *first_pvip, unsigned long pvip_size)
{
	int i, j;
	int inUse = 0;
	vpu_instance_pool_t *pvip;

	if (*pMutexState == FALSE) {
		mutex_init(vdi->vpu_thumb_mutex);
		*pMutexState = TRUE;
	}

	mutex_lock(vdi->vpu_thumb_mutex);
	for (i = 0; i < MAX_NUM_VPU_CORE; i++) {
		pvip = (vpu_instance_pool_t *)((unsigned long)first_pvip +
					       (i * pvip_size));
		for (j = 0; j < MAX_NUM_INSTANCE; j++) {
			int *pCodecInst = (int *)pvip->codecInstPool[j];
			if (pCodecInst[0] != 0) { // indicate inUse of CodecInst
				inUse = 1;
				break;
			}
		}
	}

	if (inUse == 0) {
		*vdi->thumb_used = 0;
		*vdi->thumb_num = 0;
	}
	mutex_unlock(vdi->vpu_thumb_mutex);
}

int vdi_probe(unsigned long core_idx)
{
	int ret;
	VLOG(TRACE, "[+] [%d]%s\n", __LINE__, __func__);

	mutex_init(&vdi_info_mutex_init_lock);

	ret = vdi_init(core_idx, NULL);
	vdi_release(core_idx);
	VLOG(TRACE, "[-] [%d]%s\n", __LINE__, __func__);
	return ret;
}

int vdi_init(unsigned long core_idx, void *videc_dev)
{
	int ret = -1;
	vdi_info_t *vdi = NULL;

	VLOG(TRACE, "[+] [%d]%s.core_idx:%d.MAX_NUM_VPU_CORE:%d\n", __LINE__,
	     __func__, core_idx, MAX_NUM_VPU_CORE);

	vdi = lock_vdi_info(core_idx);
	VLOG(TRACE, "[%d]%s.tgid(%d,%d,%s).vdi:0x%x\n", __LINE__, __func__,
	     current->tgid, current->pid, current->comm, vdi);
	if (vdi == NULL) {
		VLOG(ERR, "[-] [%d]%s.vdi == NULL\n", __LINE__, __func__);
		return ret;
	}
	mutex_lock(&vdi_init_release_lock[core_idx]);
	ret = Internal_vdi_init(core_idx, videc_dev);
	mutex_unlock(&vdi_init_release_lock[core_idx]);
	unlock_vdi_info(vdi, core_idx);

	VLOG(TRACE, "[-] [%d]%s.ret:%d\n", __LINE__, __func__, ret);
	return ret;
}

static int Internal_vdi_init(unsigned long core_idx, void *videc_dev)
{
	vdi_info_t *vdi;
	int i;
	struct videc_dev *dev = NULL;

	dev = (struct videc_dev *)videc_dev;
	VLOG(INFO, "[+] [%d]%s.dev:0x%px\n",
		__LINE__, __func__,
		dev);

	if (core_idx >= MAX_NUM_VPU_CORE) {
		VLOG(ERR, "[-] [%d]%s.core_idx:%d > MAX_NUM_VPU_CORE:%d\n",
		     __LINE__, __func__, core_idx, MAX_NUM_VPU_CORE);
		return 0;
	}

	vdi = &s_vdi_info[core_idx];
	if (vdi->vpu_fd != -1 && vdi->vpu_fd != 0x00) {
		vdi->task_num++;
		VLOG(TRACE, "[-] [%d]%s.task_num:%d\n", __LINE__, __func__,
		     vdi->task_num);
		return 0;
	}

	vdi->vpu_fd = 0xabcd;

	memset(&vdi->vpu_buffer_pool, 0x00,
	       sizeof(vdi_vpudrv_buffer_pool_t) * MAX_VPU_BUFFER_POOL);
	memset(&vdi->vdb_pvip, 0x00, sizeof(vpudrv_buffer_t)); //RTK
	memset(&vdi->dcsys_register, 0x00, sizeof(vpudrv_buffer_t)); //RTK
	memset(&vdi->dmcsys_register, 0x00, sizeof(vpudrv_buffer_t)); //RTK

	if (!Internal_vdi_get_instance_pool(core_idx)) {
		VLOG(TRACE,
		     "[%d][VDI] fail to create shared info for saving context \n",
		     __LINE__);
		goto ERR_VDI_INIT;
	}

	if (vdi->pvip->instance_pool_inited == FALSE) {
		int *pCodecInst;
		mutex_init(vdi->vpu_mutex);
		mutex_init(vdi->vpu_disp_mutex);

		for (i = 0; i < MAX_NUM_INSTANCE; i++) {
			pCodecInst = (int *)vdi->pvip->codecInstPool[i];
			pCodecInst[1] = i; // indicate instIndex of CodecInst
			pCodecInst[0] = 0; // indicate inUse of CodecInst
		}

		vdi->pvip->instance_pool_inited = TRUE;
	}

	if (rtd16xxb_vdi_ioctl_get_register_info(&vdi->vdb_register) < 0) {
		VLOG(ERR, "[%d][VDI] fail to get host interface register\n",
		     __LINE__);
		goto ERR_VDI_INIT;
	}

	VLOG(TRACE,
	     "[%d][VDI] map vdb_register core_idx=%d, virtaddr=0x%lx, size=%d\n",
	     __LINE__, core_idx, vdi->vdb_register.virt_addr,
	     vdi->vdb_register.size);

	Internal_vdi_set_clock_gate(core_idx, 1);

	vdi->ve1_hw_mutex = &dev->ve1_hw_mutex;
	vdi->ve1_instance_nums = &dev->ve1_instance_nums;
	VLOG(INFO, "[%d]%s.ve1_hw_mutex:0x%px.ve1_instance_nums:%d(0x%px)\n",
		__LINE__, __func__,
		vdi->ve1_hw_mutex,
		*vdi->ve1_instance_nums,
		vdi->ve1_instance_nums);

	//VLOG(INFO, "[%d]%s.mutex_lock(vdi->ve1_hw_mutex)\n",
	//	__LINE__, __func__);
	mutex_lock(vdi->ve1_hw_mutex);

	if (*vdi->ve1_instance_nums == 0) {
		Internal_vdi_set_rtk_clk_gating(core_idx, TRUE);
	}

	vdi->product_code =
		Internal_vdi_read_register(core_idx, VPU_PRODUCT_CODE_REGISTER);
	VLOG(TRACE, "[%d]product_code:0x%x\n", __LINE__, vdi->product_code);

	if (Internal_vdi_lock(core_idx) < 0) {
		VLOG(ERR, "[%d][VDI] fail to handle lock function\n", __LINE__);
		//VLOG(INFO, "%d.%s.mutex_unlock(vdi->ve1_hw_mutex)\n",
		//	__LINE__, __func__);
		mutex_unlock(vdi->ve1_hw_mutex);
		goto ERR_VDI_INIT;
	}

	if (PRODUCT_CODE_NOT_W_SERIES(vdi->product_code)) // CODA9XX
	{
		if (Internal_vdi_read_register(core_idx, BIT_CUR_PC) ==
		    0) // if BIT processor is not running.
		{
			VLOG(TRACE, "[%d]BIT processor is not running\n",
			     __LINE__);
			for (i = 0; i < 64; i++)
				Internal_vdi_write_register(
					core_idx, (i * 4) + 0x100, 0x0);
		}
	} else {
		VLOG(ERR, "[%d]Unknown product id : %08x\n", __LINE__,
		     vdi->product_code);
		//VLOG(INFO, "%d.%s.mutex_unlock(vdi->ve1_hw_mutex)\n",
		//	__LINE__, __func__);
		mutex_unlock(vdi->ve1_hw_mutex);
		goto ERR_VDI_INIT;
	}

	if (Internal_allocate_common_memory(core_idx) < 0) {
		VLOG(ERR,
		     "[%d][VDI] fail to get vpu common buffer from driver\n",
		     __LINE__);
		//VLOG(INFO, "%d.%s.mutex_unlock(vdi->ve1_hw_mutex)\n",
		//	__LINE__, __func__);
		mutex_unlock(vdi->ve1_hw_mutex);
		goto ERR_VDI_INIT;
	}

	vdi->core_idx = core_idx;
	vdi->task_num++;
	Internal_vdi_unlock(core_idx);

	*vdi->ve1_instance_nums = *vdi->ve1_instance_nums + 1;
	VLOG(INFO, "[%d]%s.[VDI] success to init driver.task_num:%d.ve1_instance_nums:%d\n",
		__LINE__, __func__,
		vdi->task_num, *vdi->ve1_instance_nums);
	//VLOG(INFO, "%d.%s.mutex_unlock(vdi->ve1_hw_mutex)\n",
	//	__LINE__, __func__);
	mutex_unlock(vdi->ve1_hw_mutex);

	VLOG(INFO, "[-] [%d]%s\n", __LINE__, __func__);
	return 0;

ERR_VDI_INIT:
	Internal_vdi_unlock(core_idx);
	Internal_vdi_release(core_idx);
	VLOG(ERR, "[-] [%d]%s\n", __LINE__, __func__);
	return -1;
}

int vdi_set_bit_firmware_to_pm(unsigned long core_idx,
			       const unsigned short *code)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_set_bit_firmware_to_pm(core_idx, code);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_set_bit_firmware_to_pm(unsigned long core_idx,
					       const unsigned short *code)
{
	int i;
	vpu_bit_firmware_info_t bit_firmware_info;
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return 0;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return 0;

	bit_firmware_info.size = sizeof(vpu_bit_firmware_info_t);
	bit_firmware_info.core_idx = core_idx;
	bit_firmware_info.reg_base_offset = 0;

	for (i = 0; i < 512; i++)
		bit_firmware_info.bit_code[i] = code[i];

	if (rtd16xxb_vdi_write_bit_firmware(&bit_firmware_info,
					    bit_firmware_info.size) < 0) {
		VLOG(ERR,
		     "[%d][VDI] fail to rtd16xxb_vdi_write_bit_firmware core=%d\n",
		     __LINE__, bit_firmware_info.core_idx);
		return -1;
	}

	return 0;
}

int vdi_release(unsigned long core_idx)
{
	int ret = -1;
	vdi_info_t *vdi = NULL;

	VLOG(TRACE, "[+] [%d]%s.core_idx:%d\n", __LINE__, __func__, core_idx);

	mutex_lock(&vdi_init_release_lock[core_idx]);
	vdi = lock_vdi_info(core_idx);
	if (vdi == NULL) {
		mutex_unlock(&vdi_init_release_lock[core_idx]);
		return ret;
	}
	ret = Internal_vdi_release(core_idx);
	unlock_vdi_info(vdi, core_idx);
	mutex_unlock(&vdi_init_release_lock[core_idx]);

	VLOG(TRACE, "[-] [%d]%s.core_idx:%d.ret:%d\n", __LINE__, __func__,
	     core_idx, ret);
	return ret;
}

#define VE1_CMD_TRAFFIC 0x3020
static int Internal_vdi_release(unsigned long core_idx)
{
	int i;
	vpudrv_buffer_t vdb;
	vdi_info_t *vdi;

	VLOG(INFO, "[+] [%d]%s.core_idx:%d\n", __LINE__, __func__, core_idx);
	if (core_idx >= MAX_NUM_VPU_CORE)
		return 0;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return 0;

	if (Internal_vdi_lock(core_idx) < 0) {
		VLOG(ERR, "[VDI] fail to handle lock function\n");
		return -1;
	}

	if (vdi->task_num > 1) // means that the opened instance remains
	{
		vdi->task_num--;
		Internal_vdi_unlock(core_idx);
		return 0;
	}

	if (vdi->sram_dev) {
		sram_device_remove(vdi->sram_dev);
		vdi->sram_dev = NULL;
	}

    //check cmd traffic is empty
    {
        int nTimeoutCnt = 10;
        unsigned int reg = 0;
        unsigned int cmdReg = VE1_CMD_TRAFFIC;
        do {
            reg = Internal_vdi_read_register(core_idx, cmdReg);
            if ((reg & 0x70000) == 0x70000)
                break;
            VLOG(INFO, "[VDI] reg 0x%x = 0x%x\n", cmdReg, reg);
            usleep_range(1000, 1000);
            nTimeoutCnt--;
        } while(nTimeoutCnt > 0);
    }

	osal_memset(&vdi->vdb_register, 0x00, sizeof(vpudrv_buffer_t));

	osal_memset(&vdi->dcsys_register, 0x00, sizeof(vpudrv_buffer_t));

	osal_memset(&vdi->dmcsys_register, 0x00, sizeof(vpudrv_buffer_t));

	vdb.size = 0;
	// get common memory information to free virtual address
	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_common_memory.phys_addr >=
			    vdi->vpu_buffer_pool[i].vdb.phys_addr &&
		    vdi->vpu_common_memory.phys_addr <
			    (vdi->vpu_buffer_pool[i].vdb.phys_addr +
			     vdi->vpu_buffer_pool[i].vdb.size)) {
			vdi->vpu_buffer_pool[i].inuse = 0;
			vdi->vpu_buffer_pool_count--;
			vdb = vdi->vpu_buffer_pool[i].vdb;
			break;
		}
	}

	if (vdb.size > 0) {
		memset(&vdi->vpu_common_memory, 0x00, sizeof(vpu_buffer_t));
	}

	if (vdi->vpu_common_memory_protect.size != 0) {
		Internal_vdi_free_dma_memory(core_idx,
					     &vdi->vpu_common_memory_protect);
	}

	vdi->task_num--;

	//VLOG(INFO, "[%d]%s.mutex_lock(vdi->ve1_hw_mutex)\n",
	//	__LINE__, __func__);
	mutex_lock(vdi->ve1_hw_mutex);
	*vdi->ve1_instance_nums = *vdi->ve1_instance_nums - 1;
	if ((Internal_vdi_get_instance_num(core_idx) == 0) && (*vdi->ve1_instance_nums == 0))
		Internal_vdi_set_rtk_clk_gating(core_idx, FALSE);
	VLOG(INFO, "[%d]%s.task_num:%d.ve1_instance_nums:%d\n",
		__LINE__, __func__,
		vdi->task_num, *vdi->ve1_instance_nums);
	//VLOG(INFO, "%d.%s.mutex_unlock(vdi->ve1_hw_mutex)\n",
	//	__LINE__, __func__);
	mutex_unlock(vdi->ve1_hw_mutex);

	Internal_vdi_unlock(core_idx);

	//RTK, free instance pool virtual address
	if (vdi->vdb_pvip.size > 0) {
		vdi->vdb_pvip.size = 0;
	}

	memset(vdi, 0x00, sizeof(vdi_info_t));
	vdi->vpu_fd = -1;

	VLOG(INFO, "[-] [%d]%s.core_idx:%d.ret:0\n", __LINE__, __func__,
		core_idx);
	return 0;
}

int vdi_get_common_memory(unsigned long core_idx, vpu_buffer_t *vb)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_get_common_memory(core_idx, vb);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_get_common_memory(unsigned long core_idx,
					  vpu_buffer_t *vb)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;

	osal_memcpy(vb, &vdi->vpu_common_memory, sizeof(vpu_buffer_t));

	return 0;
}

static int Internal_allocate_common_memory(unsigned long core_idx)
{
	vdi_info_t *vdi = &s_vdi_info[core_idx];
	vpudrv_buffer_t vdb;
	int i;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

	vdb.size = SIZE_COMMON * MAX_NUM_VPU_CORE;
	if (rtd16xxb_vdi_ioctl_get_common_memory(&vdb) < 0) {
		VLOG(ERR,
		     "[%d][VDI] fail to rtd16xxb_vdi_ioctl_get_common_memory size=%d\n",
		     __LINE__, vdb.size);
		return -1;
	}

	// convert os driver buffer type to vpu buffer type
	vdi->pvip->vpu_common_buffer.size = SIZE_COMMON;
	vdi->pvip->vpu_common_buffer.phys_addr = (unsigned long)(vdb.phys_addr);
	vdi->pvip->vpu_common_buffer.base = (unsigned long)(vdb.base);
	vdi->pvip->vpu_common_buffer.virt_addr = (unsigned long)(vdb.virt_addr);

	osal_memcpy(&vdi->vpu_common_memory, &vdi->pvip->vpu_common_buffer,
		    sizeof(vpudrv_buffer_t));

	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_buffer_pool[i].inuse == 0) {
			vdi->vpu_buffer_pool[i].vdb = vdb;
			vdi->vpu_buffer_pool_count++;
			vdi->vpu_buffer_pool[i].inuse = 1;
			break;
		}
	}

	VLOG(TRACE,
	     "[%d][VDI] vdi_get_common_memory physaddr=0x%lx, size=%d, virtaddr=0x%lx\n",
	     __LINE__, (int)vdi->vpu_common_memory.phys_addr,
	     (int)vdi->vpu_common_memory.size,
	     (int)vdi->vpu_common_memory.virt_addr);

	return 0;
}

static int allocate_common_memory_protect(unsigned long core_idx, void *filp)
{
	vdi_info_t *vdi = &s_vdi_info[core_idx];

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	if ((!vdi) || (vdi->vpu_fd == -1) || (vdi->vpu_fd == 0x00)) {
		return -1;
	}

	vdi->vpu_common_memory_protect.size = SIZE_COMMON;
	vdi->vpu_common_memory_protect.req_spec_region = VE_SECURE_PROTECTION;
	if (Internal_vdi_allocate_dma_memory(
		    core_idx, &vdi->vpu_common_memory_protect, filp) < 0) {
		VLOG(ERR,
		     "[VDI] fail to Internal_vdi_allocate_dma_memory size=%d\n",
		     SIZE_COMMON);
		return -1;
	}

	VLOG(TRACE,
	     "[VDI] allocate_common_memory_protect physaddr=0x%lx, size=%d, virtaddr=0x%lx\n",
	     (int)vdi->vpu_common_memory_protect.phys_addr,
	     (int)vdi->vpu_common_memory_protect.size,
	     (int)vdi->vpu_common_memory_protect.virt_addr);

	return 0;
}

int vdi_get_common_memory_protect(unsigned long core_idx, vpu_buffer_t *vb,
				  void *filp)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_get_common_memory_protect(core_idx, vb, filp);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_get_common_memory_protect(unsigned long core_idx,
						  vpu_buffer_t *vb, void *filp)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;

	if (vdi->vpu_common_memory_protect.size == 0)
		allocate_common_memory_protect(core_idx, filp);

	osal_memcpy(vb, &vdi->vpu_common_memory_protect, sizeof(vpu_buffer_t));

	return 0;
}

vpu_instance_pool_t *vdi_get_instance_pool(unsigned long core_idx)
{
	vpu_instance_pool_t *ret = NULL;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_get_instance_pool(core_idx);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static vpu_instance_pool_t *
Internal_vdi_get_instance_pool(unsigned long core_idx)
{
	vdi_info_t *vdi;
	vpudrv_buffer_t vdb;
	int instance_pool_size_per_core;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return NULL;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return NULL;

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));
	if (!vdi->pvip) {
		vdb.size =
			sizeof(vpu_instance_pool_t) +
			sizeof(USERSPACE_MUTEX_HANDLE) * VDI_NUM_LOCK_HANDLES;
		vdb.size += sizeof(int) + sizeof(vpu_sram_info);
		vdb.size += sizeof(int) * 3;
		VLOG(TRACE, "[%d]size:%d(%d+%d*%d+%d*4+%d)\n", __LINE__,
		     vdb.size, sizeof(vpu_instance_pool_t),
		     sizeof(USERSPACE_MUTEX_HANDLE), VDI_NUM_LOCK_HANDLES,
		     sizeof(int), sizeof(vpu_sram_info));
		vdb.size = (vdb.size + 0xfff) & ~0xfff; /* align 4096 */
		VLOG(TRACE, "[%d]size:%d\n", __LINE__, vdb.size);

		if (rtd16xxb_vdi_ioctl_get_instance_pool(&vdb) < 0) {
			VLOG(ERR,
			     "[%d][VDI] fail to allocate get instance pool physical space=%d\n",
			     __LINE__, (int)vdb.size);
			return NULL;
		}
		VLOG(TRACE, "[%d]vdb(0x%lx,0x%lx,%d)\n", __LINE__,
		     vdb.virt_addr, vdb.phys_addr, vdb.size);

		instance_pool_size_per_core = vdb.size / MAX_NUM_VPU_CORE;

		vdi->pvip =
			(vpu_instance_pool_t
				 *)(vdb.virt_addr +
				    (core_idx *
				     (instance_pool_size_per_core /*sizeof(vpu_instance_pool_t) + sizeof(MUTEX_HANDLE)*VDI_NUM_LOCK_HANDLES*/)));
		VLOG(TRACE, "[%d]%s.vdi->pvip:0x%px\n", __LINE__, __func__,
		     vdi->pvip);

		vdi->vpu_mutex = &vpu_mutex;
		vdi->vpu_disp_mutex = &vpu_disp_mutex;
		{
			int *sram_mutex_init;
			int *thumb_mutex_init;
			void *sram_info;
			vpu_instance_pool_t *first_pvip =
				(vpu_instance_pool_t *)vdb.virt_addr;
			vdi->vpu_sram_mutex = &vpu_sram_mutex;
			sram_mutex_init =
				(int *)((unsigned long)first_pvip +
					(instance_pool_size_per_core -
					 sizeof(USERSPACE_MUTEX_HANDLE) *
						 VDI_NUM_LOCK_HANDLES -
					 sizeof(int)));
			sram_info = (void *)((unsigned long)sram_mutex_init -
					     sizeof(vpu_sram_info));

			if (vdi->sram_dev == NULL) {
				vdi->sram_dev = sram_device_create(
					sram_info, vdi->vpu_sram_mutex,
					sram_mutex_init, first_pvip,
					instance_pool_size_per_core);
			}

			vdi->vpu_thumb_mutex = &vpu_thumb_mutex;
			vdi->thumb_used =
				(int *)((unsigned long)sram_info - sizeof(int));
			vdi->thumb_num =
				(int *)((unsigned long)vdi->thumb_used -
					sizeof(int));
			thumb_mutex_init =
				(int *)((unsigned long)vdi->thumb_num -
					sizeof(int));
			vdi_init_thumb_info(vdi, thumb_mutex_init, first_pvip,
					    instance_pool_size_per_core);
		}

		VLOG(TRACE,
		     "[%d][VDI] instance pool physaddr=0x%lx, virtaddr=0x%lx, base=0x%lx, size=%ld\n",
		     __LINE__, (int)vdb.phys_addr, (int)vdb.virt_addr,
		     (int)vdb.base, (int)vdb.size);

		//RTK
		vdi->vdb_pvip.size = vdb.size;
		vdi->vdb_pvip.virt_addr = vdb.virt_addr;
	}

	return (vpu_instance_pool_t *)vdi->pvip;
}

int vdi_open_instance(unsigned long core_idx, unsigned long inst_idx,
		      void *filp)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_open_instance(core_idx, inst_idx, filp);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_open_instance(unsigned long core_idx,
				      unsigned long inst_idx, void *filp)
{
	vdi_info_t *vdi;
	vpudrv_inst_info_t inst_info;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;

	inst_info.core_idx = core_idx;
	inst_info.inst_idx = inst_idx;

	if (rtd16xxb_vdi_ioctl_open_instance(filp, &inst_info) < 0) {
		VLOG(ERR,
		     "[%d][VDI] fail to deliver open instance num inst_idx=%d\n",
		     __LINE__, (int)inst_idx);
		return -1;
	}

	vdi->pvip->vpu_instance_num = inst_info.inst_open_count;

	return 0;
}

int vdi_close_instance(unsigned long core_idx, unsigned long inst_idx)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_close_instance(core_idx, inst_idx);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_close_instance(unsigned long core_idx,
				       unsigned long inst_idx)
{
	vdi_info_t *vdi;
	vpudrv_inst_info_t inst_info;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;

	inst_info.core_idx = core_idx;
	inst_info.inst_idx = inst_idx;

	if (rtd16xxb_vdi_ioctl_close_instance(&inst_info) < 0) {
		VLOG(ERR,
		     "[%d][VDI] fail to deliver open instance num inst_idx=%d\n",
		     __LINE__, (int)inst_idx);
		return -1;
	}

	vdi->pvip->vpu_instance_num = inst_info.inst_open_count;

	return 0;
}

int vdi_get_instance_num(unsigned long core_idx)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_get_instance_num(core_idx);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_get_instance_num(unsigned long core_idx)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;

	return vdi->pvip->vpu_instance_num;
}

int vdi_hw_reset(unsigned long core_idx) // DEVICE_ADDR_SW_RESET
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_hw_reset(core_idx);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_hw_reset(unsigned long core_idx)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;

	return 0;
}

int vdi_check_protect(unsigned long core_idx)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	unsigned int value = 0x0;
	if (vdi == NULL)
		return 0;

	if (core_idx == 1) {
	} else if (core_idx == 0) // CODA9XX
	{
		if ((Internal_vdi_read_register(core_idx, VE1_CTRL) & 0x2) !=
		    0) // if BIT processor is not running.
		{
			value = Internal_vdi_read_register(core_idx,
							   VE1_PROT_CTRL);
		} else {
			VLOG(ERR, "VE1 CTI didn't enable\n");
			goto ERR_VDI_GET_PROT;
		}
	} else {
		VLOG(ERR, "Unknown core_idx : %08x\n", core_idx);
		goto ERR_VDI_GET_PROT;
	}

	unlock_vdi_info(vdi, core_idx);
	return (value == 0x5 ? 1 : 0);

ERR_VDI_GET_PROT:
	VLOG(ERR, "Got VE prot mode failed!!!");
	unlock_vdi_info(vdi, core_idx);
	return 0;
}

void vdi_check_hwreset(unsigned long core_idx)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return;
	Internal_vdi_check_hwreset(core_idx);
	unlock_vdi_info(vdi, core_idx);
	return;
}

static void Internal_vdi_check_hwreset(unsigned long core_idx)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return;

	Internal_vdi_lock(core_idx);

	if (vdi->pvip->vpu_instance_num == 0) {
		usleep_range(1000, 1000);
	}

	Internal_vdi_unlock(core_idx);
}

static int restore_mutex_in_dead(MUTEX_HANDLE *mutex)
{
	int mutex_value;
	int *mutex_tmp = NULL;

	if (!mutex)
		return 0;
	mutex_tmp = (int *)mutex;
	mutex_value = (int)mutex_tmp[0];
	if (mutex_value == (int)0xdead10cc) // destroy by device driver
	{
		mutex_init(mutex);
		return 0;
	}

	return 1;
}

int vdi_lock(unsigned long core_idx)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_lock(core_idx);
	if (ret == 0) {
		//VLOG(INFO, "[%d]%s.mutex_lock(vdi->ve1_hw_mutex)\n",
		//	__LINE__, __func__);
		mutex_lock(vdi->ve1_hw_mutex);
	}
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_lock(unsigned long core_idx)
{
	vdi_info_t *vdi;
	const int MUTEX_TIMEOUT = 0x7fffffff;
#if defined(ANDROID) || !defined(PTHREAD_MUTEX_ROBUST_NP)
	int _ret = 0, j;
#endif

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;
#if defined(ANDROID) ||                                                        \
	!defined(PTHREAD_MUTEX_ROBUST_NP) //[r] need to be modify
	for (j = 0;
	     (_ret = vdi_pthread_mutex_trylock(
		      core_idx, 1000, (MUTEX_HANDLE *)vdi->vpu_mutex)) != 0 &&
	     j < MUTEX_TIMEOUT;
	     j++) {
		if (restore_mutex_in_dead((MUTEX_HANDLE *)vdi->vpu_mutex) ==
		    0) //Got 0xdead10cc
		{
			if (core_idx != 1) {
				if (Internal_vdi_read_register(
					    core_idx, BIT_BUSY_FLAG) == 1) {
					Internal_vdi_write_register(
						core_idx, BIT_BIT_STREAM_PARAM,
						(1 << 2));
					Internal_vdi_write_register(
						core_idx, BIT_INT_REASON, 0);
					Internal_vdi_write_register(
						core_idx, BIT_INT_CLEAR, 1);
				}
			}
		}
	}

	if (_ret == 0)
		return 0;
#else
	while (1) {
		int _ret, i;
		for (i = 0; (_ret = vdi_pthread_mutex_trylock(
				     core_idx, 1000,
				     (MUTEX_HANDLE *)vdi->vpu_mutex)) != 0 &&
			    i < MUTEX_TIMEOUT;
		     i++) {
			if (i == 0)
				VLOG(ERR,
				     "vdi_lock: mutex is already locked - try again\n");

			if (i == 1000) // check whether vpu is really busy
			{
				if (Internal_vdi_read_register(
					    core_idx, BIT_BUSY_FLAG) == 0)
					break;
			}

			if (i > VPU_BUSY_CHECK_TIMEOUT) {
				if (Internal_vdi_read_register(
					    core_idx, BIT_BUSY_FLAG) == 1) {
					Internal_vdi_write_register(
						core_idx, BIT_BIT_STREAM_PARAM,
						(1 << 2));
					Internal_vdi_write_register(
						core_idx, BIT_INT_REASON, 0);
					Internal_vdi_write_register(
						core_idx, BIT_INT_CLEAR, 1);
				}
				break;
			}
		}

		if (_ret == 0)
			break;

		VLOG(ERR,
		     "vdi_lock: can't get lock - force to unlock and clear pendingInst[%d:%s]\n",
		     _ret, strerror(_ret));
		if (_ret == EINVAL) {
			Uint32 *pInt = (Uint32 *)vdi->vpu_mutex;
			*pInt = 0xdead10cc;
			restore_mutex_in_dead((MUTEX_HANDLE *)vdi->vpu_mutex);
		}
		Internal_vdi_unlock(core_idx);
		vdi->pvip->pendingInst = NULL;
		vdi->pvip->pendingInstIdxPlus1 = 0;
	}
#endif

	return 0;
}

int vdi_lock_check(unsigned long core_idx)
{
	vdi_info_t *vdi;
	int ret;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(core_idx);

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	ret = vdi_pthread_mutex_trylock(core_idx, 0,
					(MUTEX_HANDLE *)vdi->vpu_mutex);
	if (ret == 0) {
		Internal_vdi_unlock(core_idx);
		unlock_vdi_info(vdi, core_idx);
		return -1;
	} else {
		unlock_vdi_info(vdi, core_idx);
		return 0;
	}
}

void vdi_unlock(unsigned long core_idx)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return;
	//VLOG(INFO, "%d.%s.mutex_unlock(vdi->ve1_hw_mutex)\n",
	//	__LINE__, __func__);
	mutex_unlock(vdi->ve1_hw_mutex);
	Internal_vdi_unlock(core_idx);
	unlock_vdi_info(vdi, core_idx);
	return;
}

static void Internal_vdi_unlock(unsigned long core_idx)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return;

	mutex_unlock(vdi->vpu_mutex);
}

int vdi_disp_lock(unsigned long core_idx)
{
	vdi_info_t *vdi;
	const int MUTEX_TIMEOUT = 5000; // ms
#if defined(ANDROID) || !defined(PTHREAD_MUTEX_ROBUST_NP)
	int _ret = 0, j;
#endif

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(core_idx);

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}
#if defined(ANDROID) ||                                                        \
	!defined(PTHREAD_MUTEX_ROBUST_NP) //[r] need to be modify
	for (j = 0; (_ret = vdi_pthread_mutex_trylock(
			     core_idx, 1000,
			     (MUTEX_HANDLE *)vdi->vpu_disp_mutex)) != 0 &&
		    j < MUTEX_TIMEOUT;
	     j++) {
		if (restore_mutex_in_dead(
			    (MUTEX_HANDLE *)vdi->vpu_disp_mutex) ==
		    0) //Got 0xdead10cc
		{
			if (core_idx != 1) {
				if (Internal_vdi_read_register(
					    core_idx, BIT_BUSY_FLAG) == 1) {
					Internal_vdi_write_register(
						core_idx, BIT_BIT_STREAM_PARAM,
						(1 << 2));
					Internal_vdi_write_register(
						core_idx, BIT_INT_REASON, 0);
					Internal_vdi_write_register(
						core_idx, BIT_INT_CLEAR, 1);
				}
			}
		}
	}

	if (_ret == 0) {
		unlock_vdi_info(vdi, core_idx);
		return 0;
	}
#else
	while (1) {
		int _ret, i;
		for (i = 0;
		     (_ret = vdi_pthread_mutex_trylock(
			      core_idx, 1000,
			      (MUTEX_HANDLE *)vdi->vpu_disp_mutex)) != 0 &&
		     i < MUTEX_TIMEOUT;
		     i++) {
			if (i == 0)
				VLOG(ERR,
				     "vdi_disp_lock: mutex is already locked - try again\n");
		}

		if (_ret == 0)
			break;

		if (_ret == EINVAL) {
			Uint32 *pInt = (Uint32 *)vdi->vpu_disp_mutex;
			*pInt = 0xdead10cc;
			restore_mutex_in_dead(
				(MUTEX_HANDLE *)vdi->vpu_disp_mutex);
		}

		VLOG(ERR,
		     "vdi_disp_lock: can't get lock - force to unlock. [%d:%s]\n",
		     _ret, strerror(_ret));
		Internal_vdi_disp_unlock(core_idx);
	}
#endif /* ANDROID */

	unlock_vdi_info(vdi, core_idx);
	return 0;
}

void vdi_disp_unlock(unsigned long core_idx)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return;
	Internal_vdi_disp_unlock(core_idx);
	unlock_vdi_info(vdi, core_idx);
	return;
}

static void Internal_vdi_disp_unlock(unsigned long core_idx)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return;

	mutex_unlock(vdi->vpu_disp_mutex);
}

void vdi_write_register(unsigned long core_idx, unsigned int addr,
			unsigned int data)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return;
	Internal_vdi_write_register(core_idx, addr, data);
	unlock_vdi_info(vdi, core_idx);
	return;
}

static void Internal_vdi_write_register(unsigned long core_idx,
					unsigned int addr, unsigned int data)
{
	vdi_info_t *vdi;
	unsigned long *reg_addr;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return;

	reg_addr =
		(unsigned long *)(addr +
				  (unsigned long)vdi->vdb_register.virt_addr);

	*(volatile unsigned int *)reg_addr = data;
	//VLOG(INFO, "%d.%s.w_register.0x%X = 0x%x\n",
	//	__LINE__, __func__,
	//	addr, data);
}

unsigned int vdi_read_register(unsigned long core_idx, unsigned int addr)
{
	unsigned int ret = -1U;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_read_register(core_idx, addr);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static unsigned int Internal_vdi_read_register(unsigned long core_idx,
					       unsigned int addr)
{
	vdi_info_t *vdi;
	unsigned long *reg_addr;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return (unsigned int)-1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return (unsigned int)-1;

	reg_addr =
		(unsigned long *)(addr +
				  (unsigned long)vdi->vdb_register.virt_addr);

	//VLOG(INFO, "%d.%s.r_register.0x%X = 0x%x\n",
	//	__LINE__, __func__,
	//	addr, *(volatile unsigned int *)reg_addr);
	return *(volatile unsigned int *)reg_addr;
}

#define FIO_TIMEOUT 100

#define VCORE_DBG_ADDR(__vCoreIdx) 0x8000 + (0x1000 * __vCoreIdx) + 0x300
#define VCORE_DBG_DATA(__vCoreIdx) 0x8000 + (0x1000 * __vCoreIdx) + 0x304
#define VCORE_DBG_READY(__vCoreIdx) 0x8000 + (0x1000 * __vCoreIdx) + 0x308

int vdi_clear_memory(unsigned long core_idx, unsigned int addr, int len,
		     int endian)
{
	vdi_info_t *vdi;
	vpudrv_buffer_t vdb;
	unsigned long offset;

	int i;
	Uint8 *zero;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(core_idx);

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_buffer_pool[i].inuse == 1) {
			vdb = vdi->vpu_buffer_pool[i].vdb;
			if (addr >= vdb.phys_addr &&
			    addr < (vdb.phys_addr + vdb.size))
				break;
		}
	}

	if (!vdb.size) {
		VLOG(ERR, "address 0x%08x is not mapped address!!!\n",
		     (int)addr);
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	zero = (Uint8 *)osal_malloc(len);
	osal_memset((void *)zero, 0x00, len);

	offset = addr - (unsigned long)vdb.phys_addr;
	osal_memcpy((void *)((unsigned long)vdb.virt_addr + offset), zero, len);

	osal_free(zero);

	unlock_vdi_info(vdi, core_idx);
	return len;
}

void vdi_set_sdram(unsigned long coreIdx, unsigned int addr, int len,
		   unsigned char data, int endian)
{
	vdi_info_t *vdi = lock_vdi_info(coreIdx);
	unsigned char *buf;

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, coreIdx);
		return;
	}

	buf = (unsigned char *)osal_malloc(len);
	memset(buf, 0x00, len);
	Internal_vdi_write_memory(coreIdx, addr, buf, len, endian);
	osal_free(buf);
	unlock_vdi_info(vdi, coreIdx);
}

void *vdi_get_virt_addr(unsigned long core_idx, unsigned int addr)
{
    vdi_info_t *vdi;
    vpudrv_buffer_t vdb;
    unsigned long offset;
    int i;
    void *result = NULL;

    if (core_idx >= MAX_NUM_VPU_CORE)
        return NULL;

    vdi = lock_vdi_info(core_idx);

    if(!vdi || vdi->vpu_fd==-1 || vdi->vpu_fd == 0x00)
    {
        unlock_vdi_info(vdi, core_idx);
        return NULL;
    }

    osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

    for (i=0; i<MAX_VPU_BUFFER_POOL; i++)
    {
        if (vdi->vpu_buffer_pool[i].inuse == 1)
        {
            vdb = vdi->vpu_buffer_pool[i].vdb;
            if (addr >= vdb.phys_addr && addr < (vdb.phys_addr + vdb.size)) {
                break;
            }
        }
    }

    if (!vdb.size) {
        VLOG(ERR, "address 0x%08x is not mapped address!!!\n", (int)addr);
        unlock_vdi_info(vdi, core_idx);
        return NULL;
    }

    offset = addr - (unsigned long)vdb.phys_addr;
    result = (void *)(vdb.virt_addr + offset);

    unlock_vdi_info(vdi, core_idx);
    return result;
}

int vdi_write_memory(unsigned long core_idx, unsigned int addr,
		     unsigned char *data, int len, int endian)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return -1;
	len = Internal_vdi_write_memory(core_idx, addr, data, len, endian);
	unlock_vdi_info(vdi, core_idx);
	return len;
}

static int Internal_vdi_write_memory(unsigned long core_idx, unsigned int addr,
				     unsigned char *data, int len, int endian)
{
	vdi_info_t *vdi;
	vpudrv_buffer_t vdb;
	unsigned long offset;
	int i;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		return -1;
	}

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_buffer_pool[i].inuse == 1) {
			vdb = vdi->vpu_buffer_pool[i].vdb;
			if (addr >= vdb.phys_addr &&
			    addr < (vdb.phys_addr + vdb.size)) {
				break;
			}
		}
	}

	if (!vdb.size) {
		VLOG(ERR, "address 0x%08x is not mapped address!!!\n",
		     (int)addr);
		return -1;
	}

	offset = addr - (unsigned long)vdb.phys_addr;
	Internal_swap_endian(core_idx, data, len, endian);
	osal_memcpy((void *)((unsigned long)vdb.virt_addr + offset), data, len);

	return len;
}

int vdi_read_memory(unsigned long core_idx, unsigned int addr,
		    unsigned char *data, int len, int endian)
{
	vdi_info_t *vdi;
	vpudrv_buffer_t vdb;
	unsigned long offset;
	int i;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(core_idx);

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_buffer_pool[i].inuse == 1) {
			vdb = vdi->vpu_buffer_pool[i].vdb;
			if (addr >= vdb.phys_addr &&
			    addr < (vdb.phys_addr + vdb.size)) {
				break;
			}
		}
	}

	if (i >= MAX_VPU_BUFFER_POOL || !vdb.size) {
		VLOG(ERR,
		     "[VDI] vdi_read_memory fail!! reach the MAX_VPU_BUFFER_POOL or size is 0\n");
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	offset = addr - (unsigned long)vdb.phys_addr;
	osal_memcpy(data, (const void *)((unsigned long)vdb.virt_addr + offset),
		    len);
	Internal_swap_endian(core_idx, data, len, endian);

	unlock_vdi_info(vdi, core_idx);
	return len;
}

int vdi_write_memory_va(unsigned long core_idx, unsigned char *addr,
			unsigned char *data, int len, int endian)
{
	vdi_info_t *vdi = NULL;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(core_idx);
	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	Internal_swap_endian(core_idx, data, len, endian);
	osal_memcpy((void *)(addr), data, len);

	unlock_vdi_info(vdi, core_idx);
	return len;
}

int vdi_allocate_dma_memory(unsigned long core_idx, vpu_buffer_t *vb,
			    void *filp)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL || vb == NULL || filp == NULL) {
		VLOG(ERR,
		     "[%d]%s.parameters NULL.vdi:0x%px.vb:0x%px.filp:0x%px\n",
		     __LINE__, __func__, vdi, vb, filp);
		return ret;
	}
	ret = Internal_vdi_allocate_dma_memory(core_idx, vb, filp);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_allocate_dma_memory(unsigned long core_idx,
					    vpu_buffer_t *vb, void *filp)
{
	vdi_info_t *vdi;
	int i;
	vpudrv_buffer_t vdb;
	int ret = -1;
	int retry = 10;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00 || !filp) {
		VLOG(ERR,
		     "[%d]%s.parameters NULL.vdi:0x%px.vpu_fd:%d.filp:0x%px\n",
		     __LINE__, __func__, vdi, vdi->vpu_fd, filp);
		return -1;
	}

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

	vdb.size = vb->size;

	if (vb->req_spec_region != 0) //ENABLE_TEE_DRM_FLOW
	{
		vdb.mem_type = vb->req_spec_region;
	}

	//RTK
	while (1) {
		ret = rtd16xxb_vdi_ioctl_allocate_physical_memory(filp, &vdb);
		if (ret >= 0)
			break;
		else {
			if (retry > 0) {
				retry--;
				VLOG(WARN,
				     "[%d][VDI] fail to rtd16xxb_vdi_ioctl_allocate_physical_memory size=%d, wait 50ms and retry count=%d\n",
				     __LINE__, vb->size, retry);
				msleep(50);
			} else {
				VLOG(ERR,
				     "[%d][VDI] fail to rtd16xxb_vdi_ioctl_allocate_physical_memory size=%d\n",
				     __LINE__, vb->size);
				return -1;
			}
		}
	}

	vb->phys_addr = (unsigned long)vdb.phys_addr;
	vb->base = (unsigned long)vdb.base;
	vb->virt_addr = vdb.virt_addr;

	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_buffer_pool[i].inuse == 0) {
			vdi->vpu_buffer_pool[i].vdb = vdb;
			vdi->vpu_buffer_pool_count++;
			vdi->vpu_buffer_pool[i].inuse = 1;
			break;
		}
	}

	if (i >= MAX_VPU_BUFFER_POOL) {
		VLOG(ERR,
		     "[VDI] vdi_allocate_dma_memory fail!! reach the MAX_VPU_BUFFER_POOL, physaddr=%p, virtaddr=%p~%p, size=%d,, index=%d\n",
		     vb->phys_addr, vb->virt_addr, vb->virt_addr + vb->size,
		     vb->size, i);
		return -1;
	}

	VLOG(TRACE,
	     "[%d][VDI] Internal_vdi_allocate_dma_memory, physaddr=0x%lx, virtaddr=0x%lx~0x%lx, size=%d,, index=%d\n",
	     __LINE__, vb->phys_addr, vb->virt_addr, vb->virt_addr + vb->size,
	     vb->size, i);
	return 0;
}

int vdi_allocate_dma_memory_no_mmap(unsigned long core_idx, vpu_buffer_t *vb,
				    void *filp)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL || vb == NULL || filp == NULL) {
		VLOG(ERR,
		     "[%d]%s.parameters NULL.vdi:0x%px.vb:0x%px.filp:0x%px\n",
		     __LINE__, __func__, vdi, vb, filp);
		return ret;
	}
	ret = Internal_vdi_allocate_dma_memory_no_mmap(core_idx, vb, filp);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_allocate_dma_memory_no_mmap(unsigned long core_idx,
						    vpu_buffer_t *vb,
						    void *filp)
{
	vdi_info_t *vdi;
	int i;
	vpudrv_buffer_t vdb;
	int ret = -1;
	int retry = 10;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00 || !filp) {
		VLOG(ERR,
		     "[%d]%s.parameters NULL.vdi:0x%px.vpu_fd:%d.filp:0x%px\n",
		     __LINE__, __func__, vdi, vdi->vpu_fd, filp);
		return -1;
	}

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

	vdb.size = vb->size;

	if (vb->req_spec_region != 0) //ENABLE_TEE_DRM_FLOW
	{
		vdb.mem_type = vb->req_spec_region;
	}

	//RTK
	while (1) {
		ret = rtd16xxb_vdi_ioctl_allocate_physical_memory_no_mmap(filp,
									  &vdb);
		if (ret >= 0)
			break;
		else {
			if (retry > 0) {
				retry--;
				VLOG(WARN,
				     "[%d][VDI] fail to rtd16xxb_vdi_ioctl_allocate_physical_memory_no_mmap size=%d, wait 50ms and retry count=%d\n",
				     __LINE__, vb->size, retry);
				msleep(50);
			} else {
				VLOG(ERR,
				     "[%d][VDI] fail to rtd16xxb_vdi_ioctl_allocate_physical_memory_no_mmap size=%d\n",
				     __LINE__, vb->size);
				return -1;
			}
		}
	}

	vb->phys_addr = vdb.phys_addr;
	vb->base = vdb.base;

	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_buffer_pool[i].inuse == 0) {
			vdi->vpu_buffer_pool[i].vdb = vdb;
			vdi->vpu_buffer_pool_count++;
			vdi->vpu_buffer_pool[i].inuse = 1;
			break;
		}
	}

	if (i >= MAX_VPU_BUFFER_POOL) {
		VLOG(ERR,
		     "[VDI] vdi_allocate_dma_memory_no_mmap fail!! reach the MAX_VPU_BUFFER_POOL, physaddr=%p, virtaddr=%p~%p, size=%d,, index=%d\n",
		     vb->phys_addr, vb->virt_addr, vb->virt_addr + vb->size,
		     vb->size, i);
		return -1;
	}

	VLOG(TRACE,
	     "[%d][VDI] Internal_vdi_allocate_dma_memory_no_mmap, physaddr=0x%lx, size=%d, index=%d\n",
	     __LINE__, vb->phys_addr, vb->size, i);
	return 0;
}

int vdi_attach_dma_memory(unsigned long core_idx, vpu_buffer_t *vb)
{
	vdi_info_t *vdi;
	int i;
	vpudrv_buffer_t vdb;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(core_idx);

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

	vdb.size = vb->size;
	vdb.phys_addr = vb->phys_addr;
	vdb.base = vb->base;

	vdb.virt_addr = vb->virt_addr;

	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_buffer_pool[i].vdb.phys_addr == vb->phys_addr) {
			vdi->vpu_buffer_pool[i].vdb = vdb;
			vdi->vpu_buffer_pool[i].inuse = 1;
			break;
		} else {
			if (vdi->vpu_buffer_pool[i].inuse == 0) {
				vdi->vpu_buffer_pool[i].vdb = vdb;
				vdi->vpu_buffer_pool_count++;
				vdi->vpu_buffer_pool[i].inuse = 1;
				break;
			}
		}
	}

	if (i >= MAX_VPU_BUFFER_POOL) {
		VLOG(ERR,
		     "[VDI] vdi_attach_dma_memory fail!! reach the MAX_VPU_BUFFER_POOL, physaddr=0x%lx, virtaddr=0x%lx, size=%d, index=%d\n",
		     vb->phys_addr, vb->virt_addr, vb->size, i);
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	VLOG(TRACE,
	     "[VDI] vdi_attach_dma_memory, physaddr=0x%lx, virtaddr=0x%lx, size=%d, index=%d\n",
	     vb->phys_addr, vb->virt_addr, vb->size, i);

	unlock_vdi_info(vdi, core_idx);
	return 0;
}

int vdi_dettach_dma_memory(unsigned long core_idx, vpu_buffer_t *vb)
{
	vdi_info_t *vdi;
	int i;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(core_idx);

	if (!vb || !vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	if (vb->size == 0) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_buffer_pool[i].vdb.phys_addr == vb->phys_addr) {
			vdi->vpu_buffer_pool[i].inuse = 0;
			vdi->vpu_buffer_pool_count--;
			break;
		}
	}

	if (i >= MAX_VPU_BUFFER_POOL) {
		VLOG(ERR,
		     "[VDI] vdi_dettach_dma_memory fail!! reach the MAX_VPU_BUFFER_POOL, physaddr=0x%lx, virtaddr=0x%lx, size=%d, index=%d\n",
		     vb->phys_addr, vb->virt_addr, vb->size, i);
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	VLOG(TRACE,
	     "[VDI] vdi_dettach_dma_memory, physaddr=0x%lx, virtaddr=0x%lx, size=%d, index=%d\n",
	     vb->phys_addr, vb->virt_addr, vb->size, i);

	unlock_vdi_info(vdi, core_idx);
	return 0;
}

void vdi_free_dma_memory(unsigned long core_idx, vpu_buffer_t *vb)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return;
	Internal_vdi_free_dma_memory(core_idx, vb);
	unlock_vdi_info(vdi, core_idx);
	return;
}

static void Internal_vdi_free_dma_memory(unsigned long core_idx,
					 vpu_buffer_t *vb)
{
	vdi_info_t *vdi;
	int i;
	vpudrv_buffer_t vdb;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return;

	vdi = &s_vdi_info[core_idx];

	if (!vb || !vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return;

	if (vb->size == 0)
		return;

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_buffer_pool[i].vdb.phys_addr == vb->phys_addr) {
			vdi->vpu_buffer_pool[i].inuse = 0;
			vdi->vpu_buffer_pool_count--;
			vdb = vdi->vpu_buffer_pool[i].vdb;
			VLOG(TRACE,
			     "[%d]%s.found target vdb.base:0x%lx.phys_addr:0x%lx.virt_addr:0x%lx.size:%d\n",
			     __LINE__, __func__, vdb.base, vdb.phys_addr,
			     vdb.virt_addr, vdb.size);
			break;
		}
	}

	if (i >= MAX_VPU_BUFFER_POOL || !vdb.size) {
		VLOG(ERR,
		     "[VDI] reach the MAX_VPU_BUFFER_POOL or invalid buffer to free address = 0x%lx\n",
		     (int)vdb.virt_addr);
		return;
	}

	rtd16xxb_vdi_ioctl_free_physical_memory(&vdb);

	VLOG(TRACE,
	     "[%d][VDI] Internal_vdi_free_dma_memory, physaddr=0x%lx, virtaddr=0x%lx, size=%d, index=%d\n",
	     __LINE__, vb->phys_addr, vb->virt_addr, vb->size, i);
	osal_memset(vb, 0, sizeof(vpu_buffer_t));
}

void vdi_free_dma_memory_no_mmap(unsigned long core_idx, vpu_buffer_t *vb)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return;
	Internal_vdi_free_dma_memory_no_mmap(core_idx, vb);
	unlock_vdi_info(vdi, core_idx);
	return;
}

static void Internal_vdi_free_dma_memory_no_mmap(unsigned long core_idx,
						 vpu_buffer_t *vb)
{
	vdi_info_t *vdi;
	int i;
	vpudrv_buffer_t vdb;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return;

	vdi = &s_vdi_info[core_idx];

	if (!vb || !vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return;

	if (vb->size == 0)
		return;

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

	for (i = 0; i < MAX_VPU_BUFFER_POOL; i++) {
		if (vdi->vpu_buffer_pool[i].vdb.phys_addr == vb->phys_addr) {
			vdi->vpu_buffer_pool[i].inuse = 0;
			vdi->vpu_buffer_pool_count--;
			vdb = vdi->vpu_buffer_pool[i].vdb;
			break;
		}
	}

	if (i >= MAX_VPU_BUFFER_POOL || !vdb.size) {
		VLOG(ERR,
		     "[VDI] reach the MAX_VPU_BUFFER_POOL or invalid buffer to free address = 0x%lx\n",
		     (int)vdb.virt_addr);
		return;
	}

	rtd16xxb_vdi_ioctl_free_physical_memory_no_mmap(&vdb);

	VLOG(TRACE,
	     "[%d][VDI] Internal_vdi_free_dma_memory_no_mmap, physaddr=0x%lx, virtaddr=0x%lx, size=%d, index=%d\n",
	     __LINE__, vb->phys_addr, vb->virt_addr, vb->size, i);
	osal_memset(vb, 0, sizeof(vpu_buffer_t));
}

int vdi_get_sram_memory(unsigned long core_idx, vpu_buffer_t *vb)
{
	vdi_info_t *vdi = NULL;
	vpudrv_buffer_t vdb;
	unsigned int sram_size = 0;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(core_idx);

	if (!vb || !vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	osal_memset(&vdb, 0x00, sizeof(vpudrv_buffer_t));

	switch (vdi->product_code) {
	case BODA950_CODE:
	case CODA960_CODE:
	case CODA980_CODE:
		sram_size = VDI_CODA9_SRAM_SIZE;
		vb->phys_addr = VDI_SRAM_BASE_ADDR;
		break;
	default:
		VLOG(ERR, "Check SRAM_SIZE(%d)\n", vdi->product_code);
		break;
	}

	if (sram_size >
	    0) // if we can know the sram address directly in vdi layer, we use it first for sdram address
	{
		vb->size = sram_size;
		VLOG(TRACE, "[%s:%d] core_idx:%d.phys_addr:0x%x.size:%d.\n",
		     __FUNCTION__, __LINE__, core_idx, vb->phys_addr, vb->size);

		unlock_vdi_info(vdi, core_idx);
		return 0;
	}

	unlock_vdi_info(vdi, core_idx);
	return 0;
}

int vdi_set_clock_gate(unsigned long core_idx, int enable)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_set_clock_gate(core_idx, enable);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_set_clock_gate(unsigned long core_idx, int enable)
{
	vdi_info_t *vdi = NULL;
	int ret = 0;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;
	vdi = &s_vdi_info[core_idx];
	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		return -1;
	}
	vdi->clock_state = enable;

	return ret;
}

int vdi_get_clock_gate(unsigned long core_idx)
{
	vdi_info_t *vdi;
	int ret;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(core_idx);

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	ret = vdi->clock_state;
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

int vdi_wait_bus_busy(unsigned long core_idx, int timeout,
		      unsigned int gdi_busy_flag)
{
	struct timespec64 ts_base;
	struct timespec64 ts;
	struct timespec64 ts_delta;
	vdi_info_t *vdi;

	vdi = lock_vdi_info(core_idx);

	ktime_get_ts64(&ts_base);

	while (1) {
		if (PRODUCT_CODE_NOT_W_SERIES(vdi->product_code)) {
			if (Internal_vdi_read_register(core_idx,
						       gdi_busy_flag) == 0x77)
				break;
		} else {
			VLOG(ERR, "Unknown product id : %08x\n",
			     vdi->product_code);
			unlock_vdi_info(vdi, core_idx);
			return -1;
		}

		if (timeout > 0) {
			ktime_get_ts64(&ts);
			ts_delta = timespec64_sub(ts, ts_base);

			if ((timespec64_to_ns(&ts_delta) / 1000000) > timeout)
			{
				VLOG(ERR,
				     "[VDI] vdi_wait_bus_busy timeout, PC=0x%lx\n",
				     Internal_vdi_read_register(core_idx,
								0x018));
				unlock_vdi_info(vdi, core_idx);
				return -1;
			}
		}
	}
	unlock_vdi_info(vdi, core_idx);
	return 0;
}

int vdi_wait_vpu_busy(unsigned long core_idx, int timeout,
		      unsigned int addr_bit_busy_flag)
{
	struct timespec64 ts_base;
	struct timespec64 ts;
	struct timespec64 ts_delta;
	Uint32 pc;
	Uint32 code, normalReg = TRUE;

	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return -1;

	ktime_get_ts64(&ts_base);

	code = Internal_vdi_read_register(
		core_idx, VPU_PRODUCT_CODE_REGISTER); /* read product code */

	if (PRODUCT_CODE_NOT_W_SERIES(code)) {
		pc = BIT_CUR_PC;
	} else {
		VLOG(ERR, "Unknown product id : %08x\n", code);
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	while (1) {
		if (normalReg == TRUE) {
			if (Internal_vdi_read_register(core_idx,
						       addr_bit_busy_flag) == 0)
				break;
		}

		if (timeout > 0) {
			ktime_get_ts64(&ts);
			ts_delta = timespec64_sub(ts, ts_base);

			if ((timespec64_to_ns(&ts_delta) / 1000000) > timeout)
			{
				Uint32 index;
				for (index = 0; index < 50; index++) {
					VLOG(ERR,
					     "[%d]%s [VDI] vdi_wait_vpu_busy timeout, PC=0x%lx\n",
					     __LINE__, __func__,
					     Internal_vdi_read_register(
						     core_idx, pc));
				}
				unlock_vdi_info(vdi, core_idx);
				return -1;
			}
		}
#ifdef SUPPORT_SW_UART
		usleep(1000);
#endif
	}

    //check cmd traffic is empty
    {
        int nTimeoutCnt = 10;
        unsigned int reg = 0;
        unsigned int cmdReg = VE1_CMD_TRAFFIC;
        do {
            reg = Internal_vdi_read_register(core_idx, cmdReg);
            if ((reg & 0x70000) == 0x70000)
                break;
            VLOG(INFO, "[VDI] reg 0x%x = 0x%x\n", cmdReg, reg);
            usleep_range(1000, 1000);
            nTimeoutCnt--;
        } while(nTimeoutCnt > 0);
    }

	unlock_vdi_info(vdi, core_idx);
	return 0;
}

int vdi_wait_interrupt(unsigned long coreIdx, int timeout,
		       unsigned int addr_bit_int_reason)
{
	int intr_reason = 0;
	int ret;
	vdi_info_t *vdi;
	vpudrv_intr_info_t intr_info;

	if (coreIdx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(coreIdx);

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, coreIdx);
		return -1;
	}
#ifdef SUPPORT_INTERRUPT
	intr_info.core_idx = coreIdx;
	intr_info.timeout = timeout;
	intr_info.intr_reason = 0;

	ret = rtd16xxb_vdi_ioctl_wait_interrupt(&intr_info);
	if (ret != 0) {
		unlock_vdi_info(vdi, coreIdx);
		return -1;
	}
	intr_reason = intr_info.intr_reason;
#else
	struct timeval tv = { 0 };
	Uint32 intrStatusReg;
	Uint32 pc;
	Int32 startTime, endTime, elaspedTime;

	UNREFERENCED_PARAMETER(intr_info);

	if (PRODUCT_CODE_NOT_W_SERIES(vdi->product_code)) {
		pc = BIT_CUR_PC;
		intrStatusReg = BIT_INT_STS;
	} else {
		VLOG(ERR, "Unknown product id : %08x\n", vdi->product_code);
		unlock_vdi_info(vdi, coreIdx);
		return -1;
	}

	gettimeofday(&tv, NULL);
	startTime = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	while (TRUE) {
		if (Internal_vdi_read_register(coreIdx, intrStatusReg)) {
			if ((intr_reason = Internal_vdi_read_register(
				     coreIdx, addr_bit_int_reason)))
				break;
		}
		gettimeofday(&tv, NULL);
		endTime = tv.tv_sec * 1000 + tv.tv_usec / 1000;
		if (timeout > 0 && (endTime - startTime) >= timeout) {
			unlock_vdi_info(vdi, coreIdx);
			return -1;
		}
	}
#endif

	unlock_vdi_info(vdi, coreIdx);
	return intr_reason;
}

static int read_pinfo_buffer(int core_idx, int addr)
{
	int ack;
	int rdata;
#define VDI_LOG_GDI_PINFO_ADDR (0x1068)
#define VDI_LOG_GDI_PINFO_REQ (0x1060)
#define VDI_LOG_GDI_PINFO_ACK (0x1064)
#define VDI_LOG_GDI_PINFO_DATA (0x106c)
	//------------------------------------------
	// read pinfo - indirect read
	// 1. set read addr     (GDI_PINFO_ADDR)
	// 2. send req          (GDI_PINFO_REQ)
	// 3. wait until ack==1 (GDI_PINFO_ACK)
	// 4. read data         (GDI_PINFO_DATA)
	//------------------------------------------
	Internal_vdi_write_register(core_idx, VDI_LOG_GDI_PINFO_ADDR, addr);
	Internal_vdi_write_register(core_idx, VDI_LOG_GDI_PINFO_REQ, 1);

	ack = 0;
	while (ack == 0) {
		ack = Internal_vdi_read_register(core_idx,
						 VDI_LOG_GDI_PINFO_ACK);
	}

	rdata = Internal_vdi_read_register(core_idx, VDI_LOG_GDI_PINFO_DATA);

	return rdata;
}

enum { VDI_PRODUCT_ID_980, VDI_PRODUCT_ID_960 };

static void printf_gdi_info(int core_idx, int num, int reset)
{
	int i;
	int bus_info_addr;
	int tmp;
	int val;
	int productId = 0;

	val = Internal_vdi_read_register(core_idx, VPU_PRODUCT_CODE_REGISTER);
	if ((val & 0xff00) == 0x3200)
		val = 0x3200;

	if (PRODUCT_CODE_NOT_W_SERIES(val)) {
		if (val == CODA960_CODE || val == BODA950_CODE)
			productId = VDI_PRODUCT_ID_960;
		else if (val == CODA980_CODE)
			productId = VDI_PRODUCT_ID_980;
	} else {
		VLOG(ERR, "Unknown product id : %08x\n", val);
		return;
	}

	if (productId == VDI_PRODUCT_ID_980)
		VLOG(TRACE, "\n**GDI information for GDI_20\n");
	else
		VLOG(TRACE, "\n**GDI information for GDI_10\n");

	for (i = 0; i < num; i++) {
#define VDI_LOG_GDI_INFO_CONTROL 0x1400
		if (productId == VDI_PRODUCT_ID_980)
			bus_info_addr = VDI_LOG_GDI_INFO_CONTROL + i * (0x20);
		else
			bus_info_addr = VDI_LOG_GDI_INFO_CONTROL + i * 0x14;
		if (reset) {
			Internal_vdi_write_register(core_idx, bus_info_addr,
						    0x00);
			bus_info_addr += 4;
			Internal_vdi_write_register(core_idx, bus_info_addr,
						    0x00);
			bus_info_addr += 4;
			Internal_vdi_write_register(core_idx, bus_info_addr,
						    0x00);
			bus_info_addr += 4;
			Internal_vdi_write_register(core_idx, bus_info_addr,
						    0x00);
			bus_info_addr += 4;
			Internal_vdi_write_register(core_idx, bus_info_addr,
						    0x00);

			if (productId == VDI_PRODUCT_ID_980) {
				bus_info_addr += 4;
				Internal_vdi_write_register(
					core_idx, bus_info_addr, 0x00);

				bus_info_addr += 4;
				Internal_vdi_write_register(
					core_idx, bus_info_addr, 0x00);

				bus_info_addr += 4;
				Internal_vdi_write_register(
					core_idx, bus_info_addr, 0x00);
			}

		} else {
			VLOG(TRACE, "index = %02d", i);

			tmp = read_pinfo_buffer(
				core_idx,
				bus_info_addr); //TiledEn<<20 ,GdiFormat<<17,IntlvCbCr,<<16 GdiYuvBufStride
			VLOG(TRACE, " control = 0x%08x", tmp);

			bus_info_addr += 4;
			tmp = read_pinfo_buffer(core_idx, bus_info_addr);
			VLOG(TRACE, " pic_size = 0x%08x", tmp);

			bus_info_addr += 4;
			tmp = read_pinfo_buffer(core_idx, bus_info_addr);
			VLOG(TRACE, " y-top = 0x%08x", tmp);

			bus_info_addr += 4;
			tmp = read_pinfo_buffer(core_idx, bus_info_addr);
			VLOG(TRACE, " cb-top = 0x%08x", tmp);

			bus_info_addr += 4;
			tmp = read_pinfo_buffer(core_idx, bus_info_addr);
			VLOG(TRACE, " cr-top = 0x%08x", tmp);
			if (productId == VDI_PRODUCT_ID_980) {
				bus_info_addr += 4;
				tmp = read_pinfo_buffer(core_idx,
							bus_info_addr);
				VLOG(TRACE, " y-bot = 0x%08x", tmp);

				bus_info_addr += 4;
				tmp = read_pinfo_buffer(core_idx,
							bus_info_addr);
				VLOG(TRACE, " cb-bot = 0x%08x", tmp);

				bus_info_addr += 4;
				tmp = read_pinfo_buffer(core_idx,
							bus_info_addr);
				VLOG(TRACE, " cr-bot = 0x%08x", tmp);
			}
			VLOG(TRACE, "\n");
		}
	}
}

static void vdi_make_log(unsigned long core_idx, const char *str, int step)
{
	int val = 0x0;
	(void)core_idx;

	val &= 0xffff;
	if (step == 1)
		VLOG(TRACE, "\n**%s start(%d)\n", str, val);
	else if (step == 2) //
		VLOG(TRACE, "\n**%s timeout(%d)\n", str, val);
	else
		VLOG(TRACE, "\n**%s end(%d)\n", str, val);
}

void vdi_log(unsigned long core_idx, int cmd, int step)
{
	vdi_info_t *vdi;
	int i;

	// BIT_RUN command
	enum { SEQ_INIT = 1,
	       SEQ_END = 2,
	       PIC_RUN = 3,
	       SET_FRAME_BUF = 4,
	       ENCODE_HEADER = 5,
	       ENC_PARA_SET = 6,
	       DEC_PARA_SET = 7,
	       DEC_BUF_FLUSH = 8,
	       RC_CHANGE_PARAMETER = 9,
	       VPU_SLEEP = 10,
	       VPU_WAKE = 11,
	       ENC_ROI_INIT = 12,
	       FIRMWARE_GET = 0xf,
	       VPU_RESET = 0x10,
	};

	if (core_idx >= MAX_NUM_VPU_CORE)
		return;

	vdi = lock_vdi_info(core_idx);

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0) {
		unlock_vdi_info(vdi, core_idx);
		return;
	}

	if (PRODUCT_CODE_NOT_W_SERIES(vdi->product_code)) {
		switch (cmd) {
		case SEQ_INIT:
			vdi_make_log(core_idx, "SEQ_INIT", step);
			break;
		case SEQ_END:
			vdi_make_log(core_idx, "SEQ_END", step);
			break;
		case PIC_RUN:
			vdi_make_log(core_idx, "PIC_RUN", step);
			break;
		case SET_FRAME_BUF:
			vdi_make_log(core_idx, "SET_FRAME_BUF", step);
			break;
		case ENCODE_HEADER:
			vdi_make_log(core_idx, "ENCODE_HEADER", step);
			break;
		case RC_CHANGE_PARAMETER:
			vdi_make_log(core_idx, "RC_CHANGE_PARAMETER", step);
			break;
		case DEC_BUF_FLUSH:
			vdi_make_log(core_idx, "DEC_BUF_FLUSH", step);
			break;
		case FIRMWARE_GET:
			vdi_make_log(core_idx, "FIRMWARE_GET", step);
			break;
		case VPU_RESET:
			vdi_make_log(core_idx, "VPU_RESET", step);
			break;
		case ENC_PARA_SET:
			vdi_make_log(core_idx, "ENC_PARA_SET", step);
			break;
		case DEC_PARA_SET:
			vdi_make_log(core_idx, "DEC_PARA_SET", step);
			break;
		default:
			vdi_make_log(core_idx, "ANY_CMD", step);
			break;
		}
	} else {
		VLOG(ERR, "Unknown product id : %08x\n", vdi->product_code);
		unlock_vdi_info(vdi, core_idx);
		return;
	}

	for (i = 0; i < 0x200; i = i + 16) {
		if (i == 0x20) {
			VLOG(TRACE,
			     "0x%04xh: 0x%08x 0x%08x 0xDEADDEAD 0xDEADDEAD\n",
			     i, Internal_vdi_read_register(core_idx, i),
			     Internal_vdi_read_register(core_idx, i + 4));
		} else if (i == 0x80) {
			VLOG(TRACE,
			     "0x%04xh: 0x%08x 0xDEADDEAD 0xDEADDEAD 0xDEADDEAD\n",
			     i, Internal_vdi_read_register(core_idx, i));
		} else if (i >= 0xc0 && i < 0x100) {
			VLOG(TRACE,
			     "0x%04xh: 0xDEADDEAD 0xDEADDEAD 0xDEADDEAD 0xDEADDEAD\n",
			     i);
		} else {
			VLOG(TRACE, "0x%04xh: 0x%08x 0x%08x 0x%08x 0x%08x\n", i,
			     Internal_vdi_read_register(core_idx, i),
			     Internal_vdi_read_register(core_idx, i + 4),
			     Internal_vdi_read_register(core_idx, i + 8),
			     Internal_vdi_read_register(core_idx, i + 0xc));
		}
	}

	if (PRODUCT_CODE_NOT_W_SERIES(vdi->product_code)) {
		if (cmd == VPU_RESET) {
			printf_gdi_info(core_idx, 32, 0);

#define VDI_LOG_MBC_BUSY 0x0440
#define VDI_LOG_MC_BASE 0x0C00
#define VDI_LOG_MC_BUSY 0x0C04
#define VDI_LOG_GDI_BUS_STATUS (0x10F4)
#define VDI_LOG_ROT_SRC_IDX (0x400 + 0x10C)
#define VDI_LOG_ROT_DST_IDX (0x400 + 0x110)

			VLOG(TRACE, "MBC_BUSY = %x\n",
			     Internal_vdi_read_register(core_idx,
							VDI_LOG_MBC_BUSY));
			VLOG(TRACE, "MC_BUSY = %x\n",
			     Internal_vdi_read_register(core_idx,
							VDI_LOG_MC_BUSY));
			VLOG(TRACE, "MC_MB_XY_DONE=(y:%d, x:%d)\n",
			     (Internal_vdi_read_register(core_idx,
							 VDI_LOG_MC_BASE) >>
			      20) & 0x3F,
			     (Internal_vdi_read_register(core_idx,
							 VDI_LOG_MC_BASE) >>
			      26) & 0x3F);
			VLOG(TRACE, "GDI_BUS_STATUS = %x\n",
			     Internal_vdi_read_register(
				     core_idx, VDI_LOG_GDI_BUS_STATUS));

			VLOG(TRACE, "ROT_SRC_IDX = %x\n",
			     Internal_vdi_read_register(core_idx,
							VDI_LOG_ROT_SRC_IDX));
			VLOG(TRACE, "ROT_DST_IDX = %x\n",
			     Internal_vdi_read_register(core_idx,
							VDI_LOG_ROT_DST_IDX));

			VLOG(TRACE, "P_MC_PIC_INDEX_0 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x200));
			VLOG(TRACE, "P_MC_PIC_INDEX_1 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x20c));
			VLOG(TRACE, "P_MC_PIC_INDEX_2 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x218));
			VLOG(TRACE, "P_MC_PIC_INDEX_3 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x230));
			VLOG(TRACE, "P_MC_PIC_INDEX_3 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x23C));
			VLOG(TRACE, "P_MC_PIC_INDEX_4 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x248));
			VLOG(TRACE, "P_MC_PIC_INDEX_5 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x254));
			VLOG(TRACE, "P_MC_PIC_INDEX_6 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x260));
			VLOG(TRACE, "P_MC_PIC_INDEX_7 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x26C));
			VLOG(TRACE, "P_MC_PIC_INDEX_8 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x278));
			VLOG(TRACE, "P_MC_PIC_INDEX_9 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x284));
			VLOG(TRACE, "P_MC_PIC_INDEX_a = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x290));
			VLOG(TRACE, "P_MC_PIC_INDEX_b = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x29C));
			VLOG(TRACE, "P_MC_PIC_INDEX_c = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x2A8));
			VLOG(TRACE, "P_MC_PIC_INDEX_d = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x2B4));

			VLOG(TRACE, "P_MC_PICIDX_0 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x028));
			VLOG(TRACE, "P_MC_PICIDX_1 = %x\n",
			     Internal_vdi_read_register(core_idx,
							MC_BASE + 0x02C));
		}
	} else {
		VLOG(ERR, "Unknown product id : %08x\n", vdi->product_code);
		unlock_vdi_info(vdi, core_idx);
		return;
	}
	unlock_vdi_info(vdi, core_idx);
}

static void byte_swap(unsigned char *data, int len)
{
	Uint8 temp;
	Int32 i;

	for (i = 0; i < len - 1; i += 2) {
		temp = data[i];
		data[i] = data[i + 1];
		data[i + 1] = temp;
	}
}

static void word_swap(unsigned char *data, int len)
{
	Uint16 temp;
	Uint16 *ptr = (Uint16 *)data;
	Int32 i, size = len / sizeof(Uint16);

	for (i = 0; i < size - 1; i += 2) {
		temp = ptr[i];
		ptr[i] = ptr[i + 1];
		ptr[i + 1] = temp;
	}
}

static void dword_swap(unsigned char *data, int len)
{
	Uint32 temp;
	Uint32 *ptr = (Uint32 *)data;
	Int32 i, size = len / sizeof(Uint32);

	for (i = 0; i < size - 1; i += 2) {
		temp = ptr[i];
		ptr[i] = ptr[i + 1];
		ptr[i + 1] = temp;
	}
}

static void lword_swap(unsigned char *data, int len)
{
	Uint64 temp;
	Uint64 *ptr = (Uint64 *)data;
	Int32 i, size = len / sizeof(Uint64);

	for (i = 0; i < size - 1; i += 2) {
		temp = ptr[i];
		ptr[i] = ptr[i + 1];
		ptr[i + 1] = temp;
	}
}

int vdi_get_system_endian(unsigned long core_idx)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = lock_vdi_info(core_idx);

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00) {
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}

	if (PRODUCT_CODE_NOT_W_SERIES(vdi->product_code)) {
		unlock_vdi_info(vdi, core_idx);
		return VDI_SYSTEM_ENDIAN;
	} else {
		VLOG(ERR, "Unknown product id : %08x\n", vdi->product_code);
		unlock_vdi_info(vdi, core_idx);
		return -1;
	}
}

int vdi_convert_endian(unsigned long core_idx, unsigned int endian)
{
	int ret = -1;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return ret;
	ret = Internal_vdi_convert_endian(core_idx, endian);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_convert_endian(unsigned long core_idx,
				       unsigned int endian)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || !vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;

	if (PRODUCT_CODE_NOT_W_SERIES(vdi->product_code)) {
	} else {
		VLOG(ERR, "Unknown product id : %08x\n", vdi->product_code);
		return -1;
	}

	return (endian & 0x0f);
}

static Uint32 convert_endian_coda9_to_wave4(Uint32 endian)
{
	Uint32 converted_endian = endian;
	switch (endian) {
	case VDI_LITTLE_ENDIAN:
		converted_endian = 0;
		break;
	case VDI_BIG_ENDIAN:
		converted_endian = 7;
		break;
	case VDI_32BIT_LITTLE_ENDIAN:
		converted_endian = 4;
		break;
	case VDI_32BIT_BIG_ENDIAN:
		converted_endian = 3;
		break;
	}
	return converted_endian;
}

static int Internal_swap_endian(unsigned long core_idx, unsigned char *data,
				int len, int endian)
{
	vdi_info_t *vdi;
	int changes;
	int sys_endian;
	BOOL byteChange, wordChange, dwordChange, lwordChange;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;

	if (PRODUCT_CODE_NOT_W_SERIES(vdi->product_code)) {
		sys_endian = VDI_SYSTEM_ENDIAN;
	} else {
		VLOG(ERR, "Unknown product id : %08x\n", vdi->product_code);
		return -1;
	}

	endian = Internal_vdi_convert_endian(core_idx, endian);
	sys_endian = Internal_vdi_convert_endian(core_idx, sys_endian);
	if (endian == sys_endian)
		return 0;

	if (PRODUCT_CODE_NOT_W_SERIES(vdi->product_code)) {
		endian = convert_endian_coda9_to_wave4(endian);
		sys_endian = convert_endian_coda9_to_wave4(sys_endian);
	} else {
		VLOG(ERR, "Unknown product id : %08x\n", vdi->product_code);
		return -1;
	}

	changes = endian ^ sys_endian;
	byteChange = changes & 0x01;
	wordChange = ((changes & 0x02) == 0x02);
	dwordChange = ((changes & 0x04) == 0x04);
	lwordChange = ((changes & 0x08) == 0x08);

	if (byteChange)
		byte_swap(data, len);
	if (wordChange)
		word_swap(data, len);
	if (dwordChange)
		dword_swap(data, len);
	if (lwordChange)
		lword_swap(data, len);

	return 1;
}

void vdi_set_rtk_clk_gating(Uint32 coreIdx, BOOL clk_en)
{
	vdi_info_t *vdi = lock_vdi_info(coreIdx);
	if (vdi == NULL)
		return;
	Internal_vdi_set_rtk_clk_gating(coreIdx, clk_en);
	unlock_vdi_info(vdi, coreIdx);
	return;
}

static void Internal_vdi_set_rtk_clk_gating(Uint32 coreIdx, BOOL clk_en)
{
	vpu_clock_info_t clockInfo;

	VLOG(TRACE, "[VDI]  %s, %d... coreIdx:%d, clk_en:%s\n", __FUNCTION__,
	     __LINE__, coreIdx, (clk_en == TRUE ? "TRUE" : "FALSE"));
	clockInfo.core_idx = coreIdx;
	clockInfo.enable = clk_en;
#ifdef SUPPORT_SW_UART
	if (clk_en == FALSE)
		return;
#endif

	rtd16xxb_vdi_ioctl_set_rtk_clk_gating(&clockInfo);
}

unsigned int vdi_get_rtk_clk_rate(Uint32 coreIdx)
{
	vdi_info_t *vdi = lock_vdi_info(coreIdx);
	vpu_clock_info_t clockInfo;

	clockInfo.core_idx = coreIdx;
	clockInfo.value = 0;

	VLOG(TRACE, "[VDI]  %s, %d... coreIdx:%d, clk_rate:%d\n", __FUNCTION__,
	     __LINE__, coreIdx, clockInfo.value);
	unlock_vdi_info(vdi, coreIdx);
	return clockInfo.value;
}

#define ENABLE_RM (1 << 3)
#define ENABLE_DOLBY_VISION (1 << 1)
typedef enum {
	STD_AVC,
	STD_VC1,
	STD_MPEG2,
	STD_MPEG4,
	STD_H263,
	STD_UNKNOWN3,
	STD_RV,
	STD_AVS,
	STD_THO = 9,
	STD_VP3,
	STD_VP8,
	STD_HEVC,
	STD_VP9,
	STD_AVS2,
	STD_DOLBY_VISION,
	STD_MAX
} CodStd;

unsigned int vdi_get_support_vtype(Uint32 coreIdx)
{
	unsigned int support_vtype =
		(1 << STD_AVC) | (1 << STD_VC1) | (1 << STD_MPEG2) |
		(1 << STD_MPEG4) | (1 << STD_H263) | (1 << STD_AVS) |
		(1 << STD_THO) | (1 << STD_VP8) | (1 << STD_HEVC) |
		(1 << STD_VP9) | (1 << STD_AVS2);

	return support_vtype;
}

int vdi_set_ve_prot_mode(Uint32 core_idx, BOOL enable)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return 0;

	vdi = lock_vdi_info(core_idx);

	if (PRODUCT_CODE_NOT_W_SERIES(vdi->product_code)) // CODA9XX
	{
		if ((Internal_vdi_read_register(core_idx, VE1_CTRL) & 0x2) !=
		    0) // if BIT processor is not running.
		{
			if (enable == TRUE) {
				Internal_vdi_write_register(core_idx,
							    VE1_PROT_CTRL, 0x5);
			} else {
				Internal_vdi_write_register(core_idx,
							    VE1_PROT_CTRL, 0x2);
			}
		} else {
			VLOG(ERR, "VE1 CTI didn't enable\n");
			goto ERR_VDI_SET_PROT;
		}
	} else {
		VLOG(ERR, "Unknown product id : %08x\n", vdi->product_code);
		goto ERR_VDI_SET_PROT;
	}

	unlock_vdi_info(vdi, core_idx);
	return 1;

ERR_VDI_SET_PROT:
	VLOG(ERR, "Set VE prot mode failed!!!");
	unlock_vdi_info(vdi, core_idx);
	return 0;
}

unsigned int vdi_set_dovi_flag(unsigned long core_idx, unsigned long inst_idx,
			       unsigned int enable)
{
	vdi_info_t *vdi;
	vpudrv_dovi_info_t dovi_flag;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return 0;

	vdi = lock_vdi_info(core_idx);

	dovi_flag.core_idx = core_idx;
	dovi_flag.inst_idx = inst_idx;
	dovi_flag.enable = enable;

	unlock_vdi_info(vdi, core_idx);
	return dovi_flag.enable;
}

int vdi_get_total_instance_num(unsigned long core_idx)
{
	int ret;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return -1;
	ret = Internal_vdi_get_total_instance_num(core_idx);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static int Internal_vdi_get_total_instance_num(unsigned long core_idx)
{
	vdi_info_t *vdi;
	vpudrv_inst_info_t inst_info;
	inst_info.inst_open_count = 4;
	//TODO: need to add rtd16xxb_vdi_ioctl_get_total_instance_num to get vpudrv_inst_info

	if (core_idx >= MAX_NUM_VPU_CORE)
		return -1;

	vdi = &s_vdi_info[core_idx];

	if (!vdi || vdi->vpu_fd == -1 || vdi->vpu_fd == 0x00)
		return -1;

	return inst_info.inst_open_count;
}

void vdi_set_thumb_num(unsigned long core_idx, unsigned int enable)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return;
	Internal_vdi_set_thumb_num(core_idx, enable);
	unlock_vdi_info(vdi, core_idx);
	return;
}

static void Internal_vdi_set_thumb_num(unsigned long core_idx,
				       unsigned int enable)
{
	vdi_info_t *vdi;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return;

	vdi = &s_vdi_info[core_idx];

	mutex_lock(vdi->vpu_thumb_mutex);
	if (enable) {
		*vdi->thumb_num = *vdi->thumb_num + 1;
		;
	} else {
		if (*vdi->thumb_num > 0)
			*vdi->thumb_num = *vdi->thumb_num - 1;
	}
	mutex_unlock(vdi->vpu_thumb_mutex);
}

unsigned int vdi_get_thumb_num(unsigned long core_idx)
{
	unsigned int ret;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return -1U;
	ret = Internal_vdi_get_thumb_num(core_idx);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static unsigned int Internal_vdi_get_thumb_num(unsigned long core_idx)
{
	vdi_info_t *vdi;
	unsigned int ret = 0;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return 0;

	vdi = &s_vdi_info[core_idx];

	mutex_lock(vdi->vpu_thumb_mutex);
	ret = *vdi->thumb_num;
	mutex_unlock(vdi->vpu_thumb_mutex);

	return ret;
}

unsigned int vdi_set_thumb_used(unsigned long core_idx, unsigned int enable)
{
	unsigned int ret;
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return -1U;
	ret = Internal_vdi_set_thumb_used(core_idx, enable);
	unlock_vdi_info(vdi, core_idx);
	return ret;
}

static unsigned int Internal_vdi_set_thumb_used(unsigned long core_idx,
						unsigned int enable)
{
	vdi_info_t *vdi;
	unsigned int ret = 0;

	if (core_idx >= MAX_NUM_VPU_CORE)
		return 0;

	vdi = &s_vdi_info[core_idx];

	mutex_lock(vdi->vpu_thumb_mutex);
	if (enable) {
		if (*vdi->thumb_used == 0) {
			*vdi->thumb_used = 1;
			ret = 1;
		}
	} else {
		*vdi->thumb_used = 0;
	}
	mutex_unlock(vdi->vpu_thumb_mutex);

	return ret;
}

void ve1_hw_lock(unsigned long core_idx)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return;

	//VLOG(INFO, "[%d]%s.mutex_lock(vdi->ve1_hw_mutex)\n",
	//	__LINE__, __func__);
	mutex_lock(vdi->ve1_hw_mutex);

	unlock_vdi_info(vdi, core_idx);
}

void ve1_hw_unlock(unsigned long core_idx)
{
	vdi_info_t *vdi = lock_vdi_info(core_idx);
	if (vdi == NULL)
		return;

	//VLOG(INFO, "%d.%s.mutex_unlock(vdi->ve1_hw_mutex)\n",
	//	__LINE__, __func__);
	mutex_unlock(vdi->ve1_hw_mutex);

	unlock_vdi_info(vdi, core_idx);
}