// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Airoha Technology Co., Ltd.
 *
 * Author: U-Boot port of ECNT TF-A Biwin SPI NAND table
 *
 * Biwin (佰维存储) SPI NAND driver ported from the ECNT
 * spi_nand_flash_table.c. Reference:
 *   BWJX08U (2Gbit, 2KiB page + 64B OOB)
 *   BWET08U (2Gbit, 2KiB page + 128B OOB)
 */

#include <linux/bitfield.h>
#ifndef __UBOOT__
#include <linux/device.h>
#include <linux/kernel.h>
#endif
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_BIWIN		0xBC

/*
 * Biwin SPI NAND ECC status uses bits 4-5 of the status register
 * (mask 0x30, uncorrectable value 0x20 per ECNT TF-A flash table).
 * Same encoding as HeyangTek and Macronix 4-state ECC.
 */
#define BW_STATUS_ECC_MASK		GENMASK(5, 4)
#define BW_STATUS_ECC_NO_BITFLIPS	(0 << 4)
#define BW_STATUS_ECC_HAS_BITFLIPS	(1 << 4)
#define BW_STATUS_ECC_UNCOR_ERROR	(2 << 4)
#define BW_STATUS_ECC_LIMIT_BITFLIPS	(3 << 4)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_4S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_2S_2S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_2S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_FAST_1S_1S_1S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_1S_OP(0, 1, NULL, 0, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_1S_1S_4S_OP(true, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_1S_1S_4S_OP(false, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(false, 0, NULL, 0));

/* OOB layout: ECC bytes occupy the upper half, free area starts at byte 2. */
static int bw_oob_ecc(struct mtd_info *mtd, int section,
		       struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int bw_oob_free(struct mtd_info *mtd, int section,
		       struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize / 2 - 2;

	return 0;
}

static const struct mtd_ooblayout_ops bw_oob_layout = {
	.ecc = bw_oob_ecc,
	.rfree = bw_oob_free,
};

static int bw_ecc_get_status(struct spinand_device *spinand,
			      u8 status)
{
	status = status & BW_STATUS_ECC_MASK;

	switch (status) {
	case BW_STATUS_ECC_NO_BITFLIPS:
		return 0;
	case BW_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;
	case BW_STATUS_ECC_HAS_BITFLIPS:
		/* 1-3 bits corrected */
		return 3;
	case BW_STATUS_ECC_LIMIT_BITFLIPS:
		/* 4-8 bits corrected */
		return 8;
	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info biwin_spinand_table[] = {
	SPINAND_INFO("BWJX08U",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xB1),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&bw_oob_layout, bw_ecc_get_status)),
	SPINAND_INFO("BWET08U",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xB2),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&bw_oob_layout, bw_ecc_get_status)),
};

static const struct spinand_manufacturer_ops biwin_spinand_manuf_ops = {
};

const struct spinand_manufacturer biwin_spinand_manufacturer = {
	.id = SPINAND_MFR_BIWIN,
	.name = "Biwin",
	.chips = biwin_spinand_table,
	.nchips = ARRAY_SIZE(biwin_spinand_table),
	.ops = &biwin_spinand_manuf_ops,
};
