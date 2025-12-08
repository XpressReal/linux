/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2022 Vivante Corporation
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
#include "linux/of_irq.h"
#include "linux/of_address.h"
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

static int check_dtb(
	struct device *dev
	);

gceSTATUS
rtk_npu_wrapper_init(
	struct device *dev,
	uint8_t *reg_base
	);

gceSTATUS
_AdjustParam(
	IN gcsPLATFORM *Platform,
	OUT gcsMODULE_PARAMETERS *Args
	);

gceSTATUS
_GetGPUPhysical(
	IN gcsPLATFORM * Platform,
	IN gctPHYS_ADDR_T CPUPhysical,
	OUT gctPHYS_ADDR_T *GPUPhysical
	);

gceSTATUS
_GetPower(
	IN gcsPLATFORM * Platform
	);

gceSTATUS
_PutPower(
	IN gcsPLATFORM * Platform
	);

static struct _gcsPLATFORM_OPERATIONS default_ops =
{
	.adjustParam        = _AdjustParam,
	.getGPUPhysical     = _GetGPUPhysical,
#if !gcdREALTEK_FPGA_BUILD
	 .getPower           = _GetPower,
	 .putPower           = _PutPower,
#endif
};

static struct _gcsPLATFORM default_platform =
{
	.name = __FILE__,
	.ops  = &default_ops,
};

gceSTATUS
_AdjustParam(
	IN gcsPLATFORM *Platform,
	OUT gcsMODULE_PARAMETERS *Args
	)
{
	struct platform_device *pdev = Platform->device;
	u64 contSize, contBase;
	struct resource res;
	int irq, ShowArgs;
	gceSTATUS ret = gcvSTATUS_OK;

    printk(KERN_DEBUG "galcore: enter %s\n", __func__);
	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);

	of_address_to_resource(pdev->dev.of_node, 0, &res);
	Args->irqs[gcvCORE_MAJOR]          = irq;
	Args->registerBases[gcvCORE_MAJOR] = res.start;
	Args->registerSizes[gcvCORE_MAJOR] = res.end - res.start + 1;

	if (of_property_read_u64(pdev->dev.of_node, "contiguousSize", &contSize) == 0)
		Args->contiguousSize = contSize;

	if (of_property_read_u64(pdev->dev.of_node, "contiguousBase", &contBase) == 0)
		Args->contiguousBase = contBase;

	if (of_property_read_u32(pdev->dev.of_node, "showArgs", &ShowArgs) == 0)
		Args->showArgs = ShowArgs;

	/* Map register in advance to init wrapper */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
	Args->registerBasesMapped[gcvCORE_MAJOR] = (gctPOINTER)ioremap(
		Args->registerBases[gcvCORE_MAJOR],
		Args->registerSizes[gcvCORE_MAJOR]);
#else
	Args->registerBasesMapped[gcvCORE_MAJOR] = (gctPOINTER)ioremap_nocache(
		Args->registerBases[gcvCORE_MAJOR],
		Args->registerSizes[gcvCORE_MAJOR]);
#endif

	if (!Args->registerBasesMapped[gcvCORE_MAJOR]) {
		dev_err(&pdev->dev, "Unable to map %zu bytes @ 0x%llx\n",
			Args->registerSizes[gcvCORE_MAJOR],
			Args->registerBases[gcvCORE_MAJOR]);
		return gcvSTATUS_OUT_OF_RESOURCES;
	}

	ret = rtk_npu_wrapper_init(&pdev->dev, Args->registerBasesMapped[gcvCORE_MAJOR]);

    printk(KERN_DEBUG "galcore: exit %s\n", __func__);
	return ret;
}

gceSTATUS
_GetGPUPhysical(
	IN gcsPLATFORM * Platform,
	IN gctPHYS_ADDR_T CPUPhysical,
	OUT gctPHYS_ADDR_T *GPUPhysical
	)
{
	*GPUPhysical = CPUPhysical;

	return gcvSTATUS_OK;
}

gceSTATUS
_GetPower(
	IN gcsPLATFORM * Platform
	)
{
	int retval;
	struct device *dev = &Platform->device->dev;

	printk(KERN_DEBUG "galcore: enter %s\n", __func__);

	if (check_dtb(dev))
		return -1;

	pm_runtime_enable(dev);

	retval = pm_runtime_get_sync(dev);
	if (retval) {
		dev_err(dev, "pm_runtime_get_sync() retval:%d\n", retval);
		return -1;
	}

	printk(KERN_DEBUG "galcore: exit %s\n", __func__);

	return gcvSTATUS_OK;
}

gceSTATUS
_PutPower(
	IN gcsPLATFORM * Platform
)
{
	int retval;
	struct device *dev = &Platform->device->dev;

	printk(KERN_DEBUG "galcore: enter %s\n", __func__);

	retval = pm_runtime_put_sync_suspend(dev);
	if (retval)
		dev_err(dev, "pm_runtime_put_sync_suspend() retval:%d\n", retval);

	pm_runtime_disable(dev);

	printk(KERN_DEBUG "galcore: exit %s\n", __func__);

	return gcvSTATUS_OK;
}

static const struct of_device_id npu_wrapper_dt_ids[] = {
	{ .compatible = "realtek,npu-wrapper", },
    {},
};
MODULE_DEVICE_TABLE(of, npu_wrapper_dt_ids);

int gckPLATFORM_Init(struct platform_driver *pdrv,
	struct _gcsPLATFORM **platform)
{
	pdrv->driver.of_match_table = of_match_ptr(npu_wrapper_dt_ids);
	*platform = (gcsPLATFORM *)&default_platform;

	return gcvSTATUS_OK;
}

int gckPLATFORM_Terminate(struct _gcsPLATFORM *platform)
{
	return gcvSTATUS_OK;
}

static int check_dtb(struct device *dev)
{
	const char *status;

	if (!dev->of_node) {
		dev_err(dev, "NPU Driver Cannot work without DTB");
		return -ENODEV;
	}
	status = of_get_property(dev->of_node, "status", NULL);

	if (status && strcmp(status, "okay") != 0 && strcmp(status, "ok") != 0) {
		dev_err(dev, "NPU Driver Disabled by DTB");
		return -ENODEV;
	}

	printk(KERN_DEBUG "galcore: NPU Status %s\n", status);
	return 0;
}

gceSTATUS rtk_npu_wrapper_init(struct device *dev, uint8_t *reg_base)
{
	uint32_t val;
	printk(KERN_DEBUG "galcore: enter %s\n", __func__);

	/* reg_base should be 9808_5000
	   Read 9808_6800 to val */
	val = readl(reg_base + 0x1800); 
	printk(KERN_DEBUG "galcore: NPU wrapper clock gated register(0x9808_6800): 0x%x\n", val);

	if (((val >> 4) & 0x1) != 1) {
		/* 1625 NPU SRAM bisr takes less than 10ms */
		mdelay(10);
		val = readl(reg_base + 0x1800);

		if (((val >> 4) & 0x1) != 1) {
			pr_err("NPU BISR has not done");
			return gcvSTATUS_DEVICE;
		}
	}

	if (((val >> 11) & 0x1) != 1) {
		pr_err("NPU reset has not done");
		return gcvSTATUS_DEVICE;
	}

	if (((val >> 8) & 0x7) != 0x7) {
		pr_err("NPU is not idle");
		return gcvSTATUS_DEVICE;
	}

	/* set bit 1 */
	val = val | (1 << 1);
	writel(val, reg_base + 0x1800);

	printk(KERN_DEBUG "galcore: exit %s\n", __func__);

	return gcvSTATUS_OK;
}

