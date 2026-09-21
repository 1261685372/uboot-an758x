// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026
 *
 * YMTC (Yangtze Memory Technologies Corp., 长江存储) raw NAND
 * manufacturer ops.
 */

#include <linux/mtd/rawnand.h>

static int ymtc_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	return 0;
}

const struct nand_manufacturer_ops ymtc_nand_manuf_ops = {
	.init = ymtc_nand_init,
};
