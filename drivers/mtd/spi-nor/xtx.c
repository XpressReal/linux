// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info xtx_parts[] = {
	{ "XT25F32B", INFO(0x0b4016, 0, 64 * 1024, 64)
				FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
				NO_SFDP_FLAGS(SECT_4K) },
	{ "XT25F64F", INFO(0x0b4017, 0, 64 * 1024, 128)
			    FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
			    NO_SFDP_FLAGS(SECT_4K) },
	{ "XT25F128B", INFO(0x0b4018, 0, 64 * 1024, 256)
			    FLAGS(SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
			    NO_SFDP_FLAGS(SECT_4K) },
};

const struct spi_nor_manufacturer spi_nor_xtx = {
	.name = "xtx",
	.parts = xtx_parts,
	.nparts = ARRAY_SIZE(xtx_parts),
};
