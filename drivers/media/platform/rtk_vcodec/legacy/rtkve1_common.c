// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Realtek video encoder v4l2 driver
 *
 * Copyright (c) 2024 Realtek Semiconductor Corp. All rights reserved.
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-Proprietary
 *
 * This software component is confidential and proprietary to Realtek
 * Semiconductor Corp. Disclosure, reproduction, redistribution, in whole
 * or in part, of this work and its derivatives without express permission
 * is prohibited.
 */
#include <linux/iopoll.h>
#include <crypto/hash.h>
#include "rtkve1enc_common.h"
#include "rtkve1_regdefine.h"
#include "ve1_fw.h"
#include "ve1_vdi.h"

#define VDI_SYSTEM_ENDIAN VDI_LITTLE_ENDIAN
#define VDI_128BIT_BUS_SYSTEM_ENDIAN VDI_128BIT_LITTLE_ENDIAN

#define RTKVE1_BUSY_CHECK_TIMEOUT_US 10000000
#define RTKVE1_WAIT_CMD_IDLE_TIMEOUT_US 10000000
#define MBC_SET_SUBBLK_EN                                                      \
	(MBC_BASE + 0xA0) // subblk_man_mode[20] cr_subblk_man_en[19:0]

int rtkve1_md5_hash(unsigned char *result, int resultLen, char *data, int dataLen)
{
	int ret = 0;
	struct crypto_shash *tfm = NULL;
	struct shash_desc *desc = NULL;

	if ((result == NULL) || (resultLen <= 0) || (data == NULL) ||
	    (dataLen <= 0)) {
		return -1;
	}
	pr_info("data:0x%px.dataLen:%d\n", data,
		dataLen);
	memset(result, 0, resultLen);

	tfm = crypto_alloc_shash("md5", 0, 0);
	if (IS_ERR(tfm)) {
		tfm = NULL;
		pr_err("IS_ERR(tfm)\n");
		ret = -1;
		goto out;
	}

	desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (desc == NULL) {
		pr_err("kmalloc desc fail\n");
		ret = -1;
		goto out;
	}

	desc->tfm = tfm;

	if (crypto_shash_init(desc) < 0) {
		pr_err("crypto_shash_init() fail\n");
		ret = -1;
		goto out;
	}
	//pr_info("crypto_shash_init() ok\n");

	if (crypto_shash_update(desc, data, dataLen) < 0) {
		pr_err("crypto_shash_update() fail\n");
		ret = -1;
		goto out;
	}
	//pr_info("crypto_shash_update() ok\n");

	if (crypto_shash_final(desc, result) < 0) {
		pr_err("crypto_shash_final() fail\n");
		ret = -1;
		goto out;
	}
	//pr_info("crypto_shash_final() ok\n");

	//pr_info("%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
	//	result[0], result[1], result[2], result[3], result[4],
	//	result[5], result[6], result[7], result[8], result[9],
	//	result[10], result[11], result[12], result[13], result[14],
	//	result[15]);
out:
	if (desc) {
		kfree(desc);
	}
	if (tfm) {
		crypto_free_shash(tfm);
	}

	return ret;
}

int rtkve1_alloc_dma_memory(struct videc_dev *dev, struct rtkve1_buf *buf,
        size_t size, const char *name, struct dentry *parent)
{
    buf->vaddr = dma_alloc_coherent(dev->dev, size, &buf->paddr,
                    GFP_KERNEL);
    if (!buf->vaddr) {
        dev_err(dev->dev, "%d.%s.dma_alloc_coherent() fail.name:%s.size:%zu\n",
            __LINE__, __func__,
            name, size);
        return -ENOMEM;
    }
    buf->size = size;
    dev_dbg(dev->dev, "%d.%s.alloc memory.name:%s.size:%zu.paddr:0x%llx.vaddr:0x%px\n",
        __LINE__, __func__,
        name, size,
        buf->paddr, buf->vaddr);

    if (name && parent) {
        buf->blob.data = buf->vaddr;
        buf->blob.size = size;
        buf->dentry = debugfs_create_blob(name, 0444, parent,
                        &buf->blob);
	}

	return 0;
}

void rtkve1_free_dma_memory(struct videc_dev *dev,
        struct rtkve1_buf *buf, const char *name)
{
    if (buf->vaddr) {
        dev_dbg(dev->dev, "%d.%s.free memory.name:%s.size:%d.paddr:0x%llx.vaddr:0x%px\n",
            __LINE__, __func__,
            name, buf->size,
            buf->paddr, buf->vaddr);

        dma_free_coherent(dev->dev, buf->size, buf->vaddr, buf->paddr);
        buf->vaddr = NULL;
        buf->size = 0;
        debugfs_remove(buf->dentry);
        buf->dentry = NULL;
	}
}

void rtkve1_parabuf_write(struct rtkve1enc_ctx *ctx, int index, u32 value)
{
    struct rtkve1_dev_info* ve1_devinfo = (struct rtkve1_dev_info*)ctx->dev->ve1_devinfo;
	u32 *p = ve1_devinfo->parabuf.vaddr;

	p[index ^ 1] = value;
}

static int convert_endian(unsigned int endian)
{
	return (endian & 0x0f);
}

static uint32_t convert_endian_coda9_to_wave4(uint32_t endian)
{
	uint32_t converted_endian = endian;
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

static void byte_swap(unsigned char *data, int len)
{
	u8 temp;
	int i;

	for (i = 0; i < len; i += 2) {
		temp = data[i];
		data[i] = data[i + 1];
		data[i + 1] = temp;
	}
}

static void word_swap(unsigned char *data, int len)
{
	u16 temp;
	u16 *ptr = (u16 *)data;
	int i;
	s32 size = len / sizeof(uint16_t);

	for (i = 0; i < size; i += 2) {
		temp = ptr[i];
		ptr[i] = ptr[i + 1];
		ptr[i + 1] = temp;
	}
}

static void dword_swap(unsigned char *data, int len)
{
	u32 temp;
	u32 *ptr = (u32 *)data;
	s32 size = len / sizeof(uint32_t);
	int i;

	for (i = 0; i < size; i += 2) {
		temp = ptr[i];
		ptr[i] = ptr[i + 1];
		ptr[i + 1] = temp;
	}
}

static void lword_swap(unsigned char *data, int len)
{
	u64 temp;
	u64 *ptr = (u64 *)data;
	s32 size = len / sizeof(uint64_t);
	int i;

	for (i = 0; i < size; i += 2) {
		temp = ptr[i];
		ptr[i] = ptr[i + 1];
		ptr[i + 1] = temp;
	}
}

static int swap_endian(unsigned char *data, int len, int endian)
{
	int changes;
	int sys_endian;
	bool byteChange, wordChange, dwordChange, lwordChange;

	sys_endian = VDI_SYSTEM_ENDIAN;

	endian = convert_endian(endian);
	sys_endian = convert_endian(sys_endian);
	if (endian == sys_endian)
		return 0;

	endian = convert_endian_coda9_to_wave4(endian);
	sys_endian = convert_endian_coda9_to_wave4(sys_endian);

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

int rtkve1_write_memory(struct videc_dev *dev,
    void *vaddr, size_t bufsize,
    size_t offset, u8 *data, int len, int endian)
{
    if (!vaddr || bufsize <= 0) {
        dev_err(dev->dev, "%d.%s.vaddr is NULL or bufsize <= 0\n",
            __LINE__, __func__);
        return -EINVAL;
    }

    if (offset > bufsize || len > bufsize || offset + len > bufsize) {
        dev_err(dev->dev, "%d.%s.invalid params.offset:%ld.len:%d.bufsize:%ld\n",
            __LINE__, __func__,
            offset, len, bufsize);
        return -ENOSPC;
    }

    swap_endian(data, len, endian);
    memcpy(vaddr + offset, data, len);
    return len;
}

int rtkve1_read_memory(struct videc_dev *dev,
    void *vaddr, size_t bufsize,
    size_t offset, u8 *data, int len, int endian)
{
    if (!vaddr || bufsize <= 0) {
        dev_err(dev->dev, "%d.%s.vaddr is NULL or bufsize <= 0\n",
            __LINE__, __func__);
        return -EINVAL;
    }

    if (offset > bufsize || len > bufsize || offset + len > bufsize) {
        dev_err(dev->dev, "%d.%s.invalid params.offset:%ld.len:%d.bufsize:%ld\n",
            __LINE__, __func__,
            offset, len, bufsize);
        return -ENOSPC;
    }

    memcpy(data, vaddr + offset, len);
    swap_endian(data, len, endian);
    return len;
}

void rtkve1_reg_writel(struct videc_dev *dev, unsigned int addr,
        unsigned int data)
{
    //dev_dbg(dev->dev, "%d.%s.0x%X = 0x%x\n",
    //    __LINE__, __func__,
    //    addr, data);
	writel(data, dev->ve1_register + addr);
}

unsigned int rtkve1_reg_readl(struct videc_dev *dev, u32 addr)
{
    u32 value = 0;
    value = readl(dev->ve1_register + addr);
    //dev_dbg(dev->dev, "%d.%s.0x%X = 0x%x\n",
    //    __LINE__, __func__,
    //    addr, value);
	return value;
}

void rtkve1_issue_command(struct rtkve1enc_ctx *ctx, u32 cmd)
{
    struct videc_dev *dev = ctx->dev;

	rtkve1_reg_writel(dev, BIT_WORK_BUF_ADDR, ctx->workbuf.paddr);

	rtkve1_reg_writel(dev, BIT_BUSY_FLAG, 1);
	rtkve1_reg_writel(dev, BIT_RUN_INDEX, ctx->inst_index);
	rtkve1_reg_writel(dev, BIT_RUN_COD_STD, ctx->codec_mode);
	rtkve1_reg_writel(dev, BIT_RUN_AUX_STD, 0);
	rtkve1_reg_writel(dev, BIT_RUN_COMMAND, cmd);
}

int rtkve1_wait_interrupt(struct rtkve1enc_ctx *ctx, unsigned int timeout)
{
    int ret = 0;
    int intr_reason = 0;
    vpudrv_intr_info_t intr_info;

    intr_info.core_idx = 0;
    intr_info.timeout = timeout;
    intr_info.intr_reason = 0;

	ret = rtd16xxb_vdi_ioctl_wait_interrupt(&intr_info);
	if (ret != 0) {
		return -ETIMEDOUT;
	}
	intr_reason = intr_info.intr_reason;
    return intr_reason;
#if 0
	int ret;

	ret = wait_for_completion_timeout(&ctx->dev->irq_done,
					  msecs_to_jiffies(timeout));
	if (!ret)
		return -ETIMEDOUT;

	reinit_completion(&ctx->dev->irq_done);

	return 0;
#endif
}

void rtkve1_clear_interrupt(struct rtkve1enc_ctx *ctx)
{
    rtkve1_reg_writel(ctx->dev, BIT_INT_CLEAR, 1);
    rtkve1_reg_writel(ctx->dev, BIT_INT_REASON, 0);
}

bool rtkve1_is_init(struct videc_dev *dev)
{
    u32 pc = rtkve1_reg_readl(dev, BIT_CUR_PC);
    dev_dbg(dev->dev, "%d.%s.pc:0x%x\n",
        __LINE__, __func__,
        pc);
	return rtkve1_reg_readl(dev, BIT_CUR_PC) != 0;
}

int rtkve1_wait_cmd_traffic_idle(struct videc_dev *dev)
{
	u32 data;

	return read_poll_timeout(rtkve1_reg_readl, data, ((data & 0x70000) == 0x70000), 0,
                RTKVE1_WAIT_CMD_IDLE_TIMEOUT_US, false, dev, RTKVE1_CMD_TRAFFIC);
}

int rtkve1_wait_busy(struct videc_dev *dev, unsigned int addr)
{
    int ret = 0;
	u32 data;

	ret = read_poll_timeout(rtkve1_reg_readl, data, data == 0, 0,
            RTKVE1_BUSY_CHECK_TIMEOUT_US, false, dev, addr);
    if (ret < 0) {
        dev_dbg(dev->dev, "%d.%s.poll reg 0x%X timeout\n",
            __LINE__, __func__,
            data);
        return ret;
    }

    ret = rtkve1_wait_cmd_traffic_idle(dev);
    if (ret < 0) {
        dev_dbg(dev->dev, "%d.%s.poll cmd traffic timeout\n",
            __LINE__, __func__);
    }

    return ret;
}

int rtkve1_reset(struct videc_dev *dev, enum sw_reset_mode reset_mode)
{
    uint32_t cmd;
    int ret;

    if (reset_mode != SW_RESET_ON_BOOT) {
        cmd = rtkve1_reg_readl(dev, BIT_RUN_COMMAND);
        if (cmd == DEC_SEQ_INIT || cmd == PIC_RUN) {
            if (rtkve1_reg_readl(dev, BIT_BUSY_FLAG) ||
                rtkve1_reg_readl(dev, BIT_INT_REASON)) {
                // stop all of pipeline
                rtkve1_reg_writel(dev, MBC_SET_SUBBLK_EN,
                    ((1 << 20) | 0));

                // force to set the end of Bitstream to be decoded.
                cmd = rtkve1_reg_readl(dev,
                        BIT_BIT_STREAM_PARAM);
                cmd |= 1 << 2;
                rtkve1_reg_writel(dev, BIT_BIT_STREAM_PARAM,
                    cmd);

                cmd = rtkve1_reg_readl(dev, BIT_RD_PTR);
                rtkve1_reg_writel(dev, BIT_WR_PTR, cmd);

                ret = rtkve1_wait_busy(dev,
                        BIT_INT_REASON);
                if (ret) {
                    dev_err(dev->dev, "%d.%s.wait_timeout.BIT_INT_REASON.ret:%d\n",
                        __LINE__, __func__,
                        ret);
                    return -ETIMEDOUT;
                }
                // clear HW signal
                rtkve1_reg_writel(dev, BIT_INT_REASON, 0);
                rtkve1_reg_writel(dev, BIT_INT_CLEAR, 1);
            }
        }
    }

    // Waiting for completion of BWB transaction first
    ret = rtkve1_wait_busy(dev, GDI_BWB_STATUS);
    if (ret) {
        dev_err(dev->dev, "%d.%s.wait_timeout.GDI_BWB_STATUS.ret:%d\n",
            __LINE__, __func__,
            ret);
        return -ETIMEDOUT;
    }

    // Waiting for completion of bus transaction
    // Step1 : No more request
    rtkve1_reg_writel(
        dev, GDI_BUS_CTRL,
        0x11); // no more request {3'b0,no_more_req_sec,3'b0,no_more_req}

    ret = rtkve1_wait_busy(dev, GDI_BWB_STATUS);
    if (ret) {
        dev_err(dev->dev, "%d.%s.wait_timeout.GDI_BWB_STATUS.ret:%d\n",
            __LINE__, __func__,
            ret);
        rtkve1_reg_writel(dev, GDI_BUS_CTRL, 0x00);
        return -ETIMEDOUT;
    }

    cmd = 0;
    // Software Reset Trigger
    if (reset_mode != SW_RESET_ON_BOOT)
        cmd = VPU_SW_RESET_BPU_CORE | VPU_SW_RESET_BPU_BUS;

    cmd |= VPU_SW_RESET_VCE_CORE | VPU_SW_RESET_VCE_BUS;
    if (reset_mode == SW_RESET_ON_BOOT)
        cmd |= VPU_SW_RESET_GDI_CORE |
               VPU_SW_RESET_GDI_BUS; // If you reset GDI, tiled map should be reconfigured

    rtkve1_reg_writel(dev, BIT_SW_RESET, cmd);

    // wait until reset is done
    if (rtkve1_wait_busy(dev, BIT_SW_RESET_STATUS) != 0) {
        dev_err(dev->dev, "%d.%s.wait BIT_SW_RESET_STATUS timeout\n",
            __LINE__, __func__);
        rtkve1_reg_writel(dev, BIT_SW_RESET, 0x00);
        rtkve1_reg_writel(dev, GDI_BUS_CTRL, 0x00);
        return -ETIMEDOUT;
    }

    rtkve1_reg_writel(dev, BIT_SW_RESET, 0);

    // Step3 : must clear GDI_BUS_CTRL after done SW_RESET
    rtkve1_reg_writel(dev, GDI_BUS_CTRL, 0x00);

	return 0;
}

static int write_fw(uint16_t *code_word, uint32_t size)
{
    int i;
    vpu_bit_firmware_info_t bit_firmware_info;

    bit_firmware_info.size = sizeof(vpu_bit_firmware_info_t);
    bit_firmware_info.core_idx = 0;
    bit_firmware_info.reg_base_offset = 0;

    for (i = 0; i < 512; i++)
        bit_firmware_info.bit_code[i] = code_word[i];

    if (rtd16xxb_vdi_write_bit_firmware(&bit_firmware_info,
        bit_firmware_info.size) < 0) {
        return -EINVAL;
    }

    return 0;
}

static int load_fw(struct videc_dev *dev,
            uint16_t *code_word, uint32_t size)
{
    int ret = 0;
    uint32_t data;
    struct rtkve1_dev_info *dev_info = NULL;
    u32 *src = (u32 *)code_word;
    int i = 0;

    dev_info = (struct rtkve1_dev_info *)dev->ve1_devinfo;

    /* Check if the firmware has a 16-byte Freescale header, skip it */
    if (code_word[0] == 'M' && code_word[1] == 'X')
        src += 4;
    /*
     * Check whether the firmware is in native order or pre-reordered for
     * memory access. The first instruction opcode always is 0xe40e.
     */
    if (__le16_to_cpup((__le16 *)src) == 0xe40e) {
		u32 *dst = dev_info->codebuf.vaddr;
        for (i = 0; i < (size - 16) / 4; i += 2) {
            dst[i] = (src[i + 1] << 16) | (src[i + 1] >> 16);
            dst[i + 1] = (src[i] << 16) | (src[i] >> 16);
        }
    } else {
        /* Copy the already reordered firmware image */
        memcpy(dev_info->codebuf.vaddr, src, size);
	}

    rtkve1_reg_writel(dev, BIT_INT_ENABLE, 0);
    rtkve1_reg_writel(dev, BIT_CODE_RUN, 0);

    for (i = 0; i < 2048; ++i) {
        data = code_word[i];
        rtkve1_reg_writel(dev, BIT_CODE_DOWN, (i << 16) | data);
    }

    dev_dbg(dev->dev, "%d.%s.ret:%d\n",
        __LINE__, __func__, ret);
    return ret;
}

int rtkve1_init(struct videc_dev *dev, u8 *firmware, uint32_t size)
{
    struct rtkve1_dev_info *dev_info = NULL;
	int ret;
	uint32_t data;

	dev_info = (struct rtkve1_dev_info *)dev->ve1_devinfo;

	ret = load_fw(dev, (uint16_t *)firmware, size);
	if (ret < 0) {
		dev_err(dev->dev, "%d.%s.failed to load a firmware.ret:%d\n",
            __LINE__, __func__, ret);
		return ret;
	}
    if (write_fw((uint16_t *)firmware, size) < 0) {
		dev_err(dev->dev, "%d.%s.write_fw() fail.ret:%d\n",
            __LINE__, __func__, ret);
    }

	rtkve1_reg_writel(dev, BIT_PARA_BUF_ADDR, dev_info->parabuf.paddr);
	rtkve1_reg_writel(dev, BIT_CODE_BUF_ADDR, dev_info->codebuf.paddr);
	rtkve1_reg_writel(dev, BIT_TEMP_BUF_ADDR, dev_info->tempbuf.paddr);

	rtkve1_reg_writel(dev, BIT_BIT_STREAM_CTRL, VPU_STREAM_ENDIAN);
	rtkve1_reg_writel(dev, BIT_FRAME_MEM_CTRL,
        CBCR_INTERLEAVE << 2 | VPU_FRAME_ENDIAN);

	rtkve1_reg_writel(dev, BIT_BIT_STREAM_PARAM, 0);

	rtkve1_reg_writel(dev, BIT_AXI_SRAM_USE, 0);
	rtkve1_reg_writel(dev, BIT_INT_ENABLE, 0);
	rtkve1_reg_writel(dev, BIT_ROLLBACK_STATUS, 0);

	data = (1 << INT_BIT_BIT_BUF_FULL);
	data |= (1 << INT_BIT_BIT_BUF_EMPTY);
	data |= (1 << INT_BIT_DEC_MB_ROWS);
	data |= (1 << INT_BIT_SEQ_INIT);
	data |= (1 << INT_BIT_DEC_FIELD);
	data |= (1 << INT_BIT_PIC_RUN);

	rtkve1_reg_writel(dev, BIT_INT_ENABLE, data);
	rtkve1_reg_writel(dev, BIT_INT_CLEAR, 0x1);
	rtkve1_reg_writel(dev, BIT_BUSY_FLAG, 0x1);
	rtkve1_reg_writel(dev, BIT_CODE_RESET, 1);
	rtkve1_reg_writel(dev, BIT_CODE_RUN, 1);

	ret = rtkve1_wait_busy(dev, BIT_BUSY_FLAG);
	if (ret) {
		dev_err(dev->dev,
			"%d.%s.timeout for checking BIT_BUSY_FLAG.ret:%d\n",
            __LINE__, __func__, ret);
		return -ETIMEDOUT;
	}
	dev_dbg(dev->dev, "%d.%s.BIT_CUR_PC:0x%x\n",
        __LINE__, __func__,
        rtkve1_reg_readl(dev, BIT_CUR_PC));
	return 0;
}

int rtkve1_initialize(struct rtkve1enc_ctx *ctx, struct videc_dev *dev)
{
    int ret = 0;
    struct vpudrv_buffer_t reg;
    u32 product_code;
    struct rtkve1_dev_info *dev_info = NULL;
    vpu_clock_info_t clockInfo;
    int i = 0;
    vpudrv_buffer_t vdb;

    //if (dev->ve1_register == NULL) {
        if (rtd16xxb_vdi_ioctl_get_register_info(&reg) < 0) {
            return -EINVAL;
        }
        dev_dbg(dev->dev, "%d.%s.reg.size:%d.virt_addr:0x%lx\n",
            __LINE__, __func__,
            reg.size,
            reg.virt_addr);
        dev->ve1_register = (void __iomem *)reg.virt_addr;
    //}

    mutex_lock(&dev->ve1_hw_mutex);

    ctx->inst_index = dev->ve1_instance_nums;
    dev->ve1_instance_nums++;
    dev_dbg(dev->dev, "%d.%s.inst_index:%d.ve1_instance_nums:%d\n",
        __LINE__, __func__,
        ctx->inst_index,
        dev->ve1_instance_nums);

    if (dev->ve1_instance_nums == 1) {
        clockInfo.core_idx = 0;
        clockInfo.enable = 1;
        rtd16xxb_vdi_ioctl_set_rtk_clk_gating(&clockInfo);
        dev_dbg(dev->dev, "%d.%s.set rtk_clk_gating on\n",
            __LINE__, __func__);
    }

    product_code = rtkve1_reg_readl(dev, VPU_PRODUCT_CODE_REGISTER);
    dev_dbg(dev->dev, "%d.%s.product_code:0x%x\n",
        __LINE__, __func__,
        product_code);

    if (!rtkve1_is_init(dev)) {
        /* Clear registers */
        dev_dbg(dev->dev, "%d.%s.w_register clear start\n", __LINE__, __func__);
        for (i = 0; i < 64; i++)
            rtkve1_reg_writel(dev, BIT_CODE_BUF_ADDR + i * 4, 0);
        dev_dbg(dev->dev, "%d.%s.w_register clear end\n", __LINE__, __func__);
    }
    
    dev_info = (struct rtkve1_dev_info *)dev->ve1_devinfo;

#if 1
    vdb.size = SIZE_COMMON;
    if (rtd16xxb_vdi_ioctl_get_common_memory(&vdb) < 0) {
        dev_err(dev->dev, "%d.%s.rtd16xxb_vdi_ioctl_get_common_memory() fail\n",
            __LINE__, __func__);
        ret = -EINVAL;
        goto mutex_unlock;
    }
    dev_info->codebuf.size = CODE_BUF_SIZE;
    dev_info->codebuf.paddr = (dma_addr_t)(vdb.phys_addr);
    dev_info->codebuf.vaddr = (void *)(vdb.virt_addr);
    dev_dbg(dev->dev, "%d.%s.get_dma.name:codebuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
        __LINE__, __func__,
        dev_info->codebuf.size,
        dev_info->codebuf.paddr,
        dev_info->codebuf.vaddr);

    dev_info->tempbuf.size = TEMP_BUF_SIZE;
    dev_info->tempbuf.paddr = (dma_addr_t)(dev_info->codebuf.paddr + CODE_BUF_SIZE);
    dev_info->tempbuf.vaddr = (void *)(dev_info->codebuf.vaddr + CODE_BUF_SIZE);
    dev_dbg(dev->dev, "%d.%s.get_dma.name:tempbuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
        __LINE__, __func__,
        dev_info->tempbuf.size,
        dev_info->tempbuf.paddr,
        dev_info->tempbuf.vaddr);

    dev_info->parabuf.size = PARA_BUF_SIZE;
    dev_info->parabuf.paddr = (dma_addr_t)(dev_info->tempbuf.paddr + TEMP_BUF_SIZE);
    dev_info->parabuf.vaddr = (void *)(dev_info->tempbuf.vaddr + TEMP_BUF_SIZE);
    dev_dbg(dev->dev, "%d.%s.get_dma.name:parabuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
        __LINE__, __func__,
        dev_info->parabuf.size,
        dev_info->parabuf.paddr,
        dev_info->parabuf.vaddr);
#else
	if (dev_info->codebuf.size == 0) {
		ret = rtkve1_alloc_dma_memory(dev, &dev_info->codebuf, CODE_BUF_SIZE, "codebuf",
				dev->debugfs_root);
		if (ret < 0) {
			return ret;
		}
		dev_dbg(dev->dev, "%d.%s.alloc_dma.name:codebuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			dev_info->codebuf.size,
			dev_info->codebuf.paddr,
			dev_info->codebuf.vaddr);
	}
	if (dev_info->parabuf.size == 0) {
		ret = rtkve1_alloc_dma_memory(dev, &dev_info->parabuf, PARA_BUF_SIZE, "parabuf",
				dev->debugfs_root);
		if (ret < 0) {
			return ret;
		}
		dev_dbg(dev->dev, "%d.%s.alloc_dma.name:parabuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			dev_info->parabuf.size,
			dev_info->parabuf.paddr,
			dev_info->parabuf.vaddr);
	}
	if (dev_info->tempbuf.size == 0) {
		ret = rtkve1_alloc_dma_memory(dev, &dev_info->tempbuf, TEMP_BUF_SIZE, "tempbuf",
				dev->debugfs_root);
		if (ret < 0) {
			return ret;
		}
		dev_dbg(dev->dev, "%d.%s.alloc_dma.name:tempbuf.size:%d.paddr:0x%llx.vaddr:0x%px\n",
			__LINE__, __func__,
			dev_info->tempbuf.size,
			dev_info->tempbuf.paddr,
			dev_info->tempbuf.vaddr);
	}
#endif

    if (rtkve1_is_init(dev))
        goto mutex_unlock;

    ret = rtkve1_reset(dev, SW_RESET_ON_BOOT);
    if (ret) {
        dev_err(dev->dev, "%d.%s.failed to reset rtkve1.ret:%d\n",
            __LINE__, __func__,
            ret);
        goto mutex_unlock;
    }

    ret = rtkve1_init(dev, (u8 *)bit_code, sizeof(bit_code));
    if (ret) {
        dev_err(dev->dev,
            "%d.%s.failed to initialize rtkve1.ret:%d\n",
            __LINE__, __func__,
            ret);
    }

mutex_unlock:
    mutex_unlock(&dev->ve1_hw_mutex);

    return ret;
}

int rtkve1_finalize(struct videc_dev *dev)
{
    int ret = 0;
    vpu_clock_info_t clockInfo;

    dev_dbg(dev->dev, "%d.%s.[+]\n", __LINE__, __func__);
	mutex_lock(&dev->ve1_hw_mutex);

    ret = rtkve1_wait_cmd_traffic_idle(dev);
	if (ret) {
		dev_err(dev->dev,
			"%d.%s.timeout for checking RTKVE1_CMD_TRAFFIC.ret:%d\n",
            __LINE__, __func__, ret);
		return -ETIMEDOUT;
	}

    dev->ve1_instance_nums--;
    dev_dbg(dev->dev, "%d.%s.ve1_instance_nums:%d\n",
        __LINE__, __func__,
        dev->ve1_instance_nums);

    if (dev->ve1_instance_nums == 0) {
        clockInfo.core_idx = 0;
        clockInfo.enable = 0;
        rtd16xxb_vdi_ioctl_set_rtk_clk_gating(&clockInfo);
        dev_dbg(dev->dev, "%d.%s.set rtk_clk_gating off\n",
            __LINE__, __func__);
    }

	mutex_unlock(&dev->ve1_hw_mutex);
	dev_dbg(dev->dev, "%d.%s.[-]\n", __LINE__, __func__);

    return ret;
}