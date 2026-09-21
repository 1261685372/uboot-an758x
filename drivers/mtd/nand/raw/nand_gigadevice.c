// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026
 *
 * GigaDevice (兆易创新) raw NAND manufacturer ops.
 *
 * GigaDevice SLC chips share maker ID 0xc8 with ESMT. This file provides
 * ops used after esmt_nand_decode_id() detects a GigaDevice chip and
 * switches chip->manufacturer.desc. See nand_esmt.c for the dispatch.
 */

#include <linux/mtd/rawnand.h>

static int gigadevice_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	return 0;
}

const struct nand_manufacturer_ops gigadevice_nand_manuf_ops = {
	.init = gigadevice_nand_init,
};
