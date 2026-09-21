// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026
 *
 * JZ (Ingenic/北京君正) raw NAND manufacturer ops.
 *
 * Placeholder implementation: applies the generic SLC BBT flag.
 * Vendor-specific quirks (if any) to be added when real chip data is
 * available.
 */

#include <linux/mtd/rawnand.h>

static int jz_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	return 0;
}

const struct nand_manufacturer_ops jz_nand_manuf_ops = {
	.init = jz_nand_init,
};
