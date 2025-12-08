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

static struct clk*
enable_hifi_pll_if_needed(
	struct device*
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

static struct clk *pll_hifi = NULL;

static struct _gcsPLATFORM_OPERATIONS default_ops =
{
	.adjustParam        = _AdjustParam,
	.getGPUPhysical     = _GetGPUPhysical,
	.getPower           = _GetPower,
	.putPower           = _PutPower,
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

	pll_hifi = enable_hifi_pll_if_needed(dev);
	if (IS_ERR(pll_hifi))
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

#define NO_NEED_TO_ENABLE_HIFI_PLL NULL
static struct clk* enable_hifi_pll_if_needed(struct device* dev)
{
	int ret;
	struct regmap *regmap;
	unsigned int val;
	struct clk *_pll_hifi = NULL;
	unsigned long rate;
	void __iomem *map_bit;

	printk(KERN_DEBUG "galcore: %s\n", __func__);
	regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "realtek,sb2");
	if (IS_ERR_OR_NULL(regmap)) {
		dev_warn(dev, "failed to get sb2 regmap from device tree, ret:%d.\n",
			PTR_ERR_OR_ZERO(regmap));
		return NO_NEED_TO_ENABLE_HIFI_PLL;
	}

	ret = regmap_read(regmap, 0xd00, &val);
	if (ret) {
		dev_err(dev, "failed to get sb2 register, ret:%d.\n", ret);
		return ERR_PTR(ret);
	}
	printk(KERN_DEBUG "galcore: sb2 register: 0x%x\n", val);

	if (val == 0xdeaddead)
		return NO_NEED_TO_ENABLE_HIFI_PLL;

	map_bit = ioremap(0x980001DC, 0x120);
	val = readl(map_bit);
	printk(KERN_DEBUG "galcore: hifi pll ADDR: 0x%p  Value: 0x%x\n", map_bit, val);

	if (val == 0x3) {
		_pll_hifi = devm_clk_get(dev, "pll_hifi");
		if (IS_ERR_OR_NULL(_pll_hifi)) {
			dev_err(dev, "failed to get clk pll_hifi, ret:%d.\n", PTR_ERR_OR_ZERO(_pll_hifi));
			return ERR_PTR(-1);
		}

		rate = clk_get_rate(_pll_hifi);
		devm_clk_put(dev, _pll_hifi);
		printk(KERN_DEBUG "galcore: pll_hifi rate: %lu Hz\n", rate);

		if (rate >= 500*1000*1000) {
			dev_err(dev, "pll_hifi should not be faster than 500 Mhz\n");
			dev_err(dev, "Load npu driver fail. Please load npu driver early than hifi driver.");
			return ERR_PTR(-1);
		}
		return NO_NEED_TO_ENABLE_HIFI_PLL;
	} else {
		_pll_hifi = devm_clk_get(dev, "pll_hifi");
		if (IS_ERR_OR_NULL(_pll_hifi)) {
			dev_err(dev, "failed to get clk pll_hifi, ret:%d.\n", PTR_ERR_OR_ZERO(_pll_hifi));
			return ERR_PTR(-1);
		}

		ret = clk_prepare_enable(_pll_hifi);
		if (ret) {
			dev_err(dev, "failed to enable clk pll_hifi, ret:%d\n", ret);
			devm_clk_put(dev, _pll_hifi);
			return ERR_PTR(-1);
		}

		rate = clk_get_rate(_pll_hifi);
		printk(KERN_DEBUG "galcore: pll_hifi rate: %lu Hz\n", rate);

		if (rate >= 500*1000*1000) {
			dev_err(dev, "pll_hifi should not be faster than 500 Mhz\n");
			clk_disable_unprepare(_pll_hifi);
			devm_clk_put(dev, _pll_hifi);
			return ERR_PTR(-1);
		}
		return _pll_hifi;
	}
}

static void disable_hifi_pll(struct device* dev)
{
	printk(KERN_DEBUG "galcore: %s\n", __func__);
	clk_disable_unprepare(pll_hifi);
	devm_clk_put(dev, pll_hifi);
}

static void rtk_rtd1619b_sram_workaround(uint8_t *reg_base)
{
	uint32_t val;
	printk(KERN_DEBUG "galcore: 1619b SRAM workaround\n");
	/* 1619b sram workaround: toggle 0x9808_6c08[2] */

	val = readl(reg_base + 0x1c08);
	printk(KERN_DEBUG "galcore: ADDR: 0x%p  Value: 0x%x\n", (reg_base + 0x1c08), val);

	val = val | (0x1 << 2);
	writel(val, reg_base + 0x1c08);

	val = readl(reg_base + 0x1c08);
	printk(KERN_DEBUG "galcore: ADDR: 0x%p  Value: 0x%x\n", (reg_base + 0x1c08), val);

	val = val & ~(0x1 << 2);
	writel(val, reg_base + 0x1c08);

	val = readl(reg_base + 0x1c08);
	printk(KERN_DEBUG "galcore: ADDR: 0x%p  Value: 0x%x\n", (reg_base + 0x1c08), val);

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
		/* 1619b NPU SRAM bisr takes less than 5ms */
		mdelay(5);
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

	/* clear bit 2 & bit 3 */
	val = val & (~((1 << 3) | (1 << 2)));
	/* set bit 1 */
	val = val | (1 << 1);
	writel(val, reg_base + 0x1800);

	rtk_rtd1619b_sram_workaround(reg_base);

	if (!IS_ERR_OR_NULL(pll_hifi)) {
		disable_hifi_pll(dev);
		pll_hifi = NULL;
	}

	printk(KERN_DEBUG "galcore: exit %s\n", __func__);

	return gcvSTATUS_OK;
}

