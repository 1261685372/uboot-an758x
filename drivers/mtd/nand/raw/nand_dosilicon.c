// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026
 *
 * Dosilicon (东芯半导体) raw NAND manufacturer ops.
 */

#include <linux/mtd/rawnand.h>

static void dosilicon_nand_decode_id(struct nand_chip *chip)
{
	nand_decode_ext_id(chip);
}

static int dosilicon_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	return 0;
}

const struct nand_manufacturer_ops dosilicon_nand_manuf_ops = {
	.detect = dosilicon_nand_decode_id,
	.init = dosilicon_nand_init,
};
