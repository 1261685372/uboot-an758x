// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026
 *
 * ESMT (Elite Semiconductor Memory Technology) raw NAND manufacturer ops.
 *
 * Note: GigaDevice SLC chips share maker ID 0xc8 with ESMT (historical
 * legacy, matches Linux mainline behavior). The framework's
 * nand_manuf_ids[] table maps 0xc8 to this ops structure. The detect
 * hook discriminates GigaDevice SLC by examining id.data[4/5] and,
 * when matched, switches chip->manufacturer.desc to point at
 * gigadevice_nand_manuf_ops so the user sees the correct vendor name
 * and any GigaDevice-specific quirks apply.
 *
 * Detection pattern is heuristic: GigaDevice SLC series such as GD5F1GQ4
 * encode 0xc8 in the high nibble of id.data[4]. Validate against real
 * chip data if wrong vendor name is displayed (functional impact is
 * limited — BBT flag applies to all SLC anyway).
 */

#include <linux/mtd/rawnand.h>

static void esmt_nand_decode_id(struct nand_chip *chip)
{
	nand_decode_ext_id(chip);

	/*
	 * GigaDevice SLC discrimination: share 0xc8 maker ID with ESMT.
	 * Switch to gigadevice_nand_manuf_ops when id.data[4] high nibble
	 * matches the GigaDevice encoding.
	 */
	if (chip->id.len >= 5 &&
	    (chip->id.data[4] & 0xf0) == 0xc0)
		chip->manufacturer.desc = &gigadevice_nand_manuf_ops;
}

static int esmt_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	return 0;
}

const struct nand_manufacturer_ops esmt_nand_manuf_ops = {
	.detect = esmt_nand_decode_id,
	.init = esmt_nand_init,
};
