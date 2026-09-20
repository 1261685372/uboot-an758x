// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Airoha Technology Co., Ltd.
 *
 * Author: U-Boot port of ECNT TF-A HeyangTek SPI NAND table
 *
 * HeYangTek (合阳半导体) SPI NAND driver ported from the ECNT
 * spi_nand_flash_table.c. Reference:
 *   HYF1GQ4UAACAE / HYF2GQ4UAACAE / HYF2GQ4UHCCAE
 *   HYF4GQ4UAACBE / HYF1GQ4UDACAE / HYF2GQ4UDACAE
 */

#include <linux/bitfield.h>
#ifndef __UBOOT__
#include <linux/device.h>
#include <linux/kernel.h>
#endif
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_HEYANG		0xC9

/*
 * HeYangTek SPI NAND ECC status uses bits 4-5 of the status register
 * (mask 0x30, uncorrectable value 0x20 per ECNT TF-A flash table).
 * This matches the conventional 4-state encoding used by Macronix:
 *   0b00 = no bitflips, 0b01 = 1-3 corrected, 0b10 = uncorrectable,
 *   0b11 = 4-8 corrected.
 */
#define HYF_STATUS_ECC_MASK		GENMASK(5, 4)
#define HYF_STATUS_ECC_NO_BITFLIPS	(0 << 4)
#define HYF_STATUS_ECC_HAS_BITFLIPS	(1 << 4)
#define HYF_STATUS_ECC_UNCOR_ERROR	(2 << 4)
#define HYF_STATUS_ECC_LIMIT_BITFLIPS	(3 << 4)

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

/*
 * 1Gbit / 2Gbit chips with 2KiB page + 64B OOB: ECC bytes occupy the
 * upper half of OOB (offset 32), free area starts at byte 2 (byte 0 is
 * reserved for the bad block marker).
 */
static int hyf_oob_64_ecc(struct mtd_info *mtd, int section,
			   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int hyf_oob_64_free(struct mtd_info *mtd, int section,
			   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize / 2 - 2;

	return 0;
}

static const struct mtd_ooblayout_ops hyf_oob_64_layout = {
	.ecc = hyf_oob_64_ecc,
	.rfree = hyf_oob_64_free,
};

/*
 * 2Gbit / 4Gbit chips with 2KiB or 4KiB page + 128B / 256B OOB: ECC
 * bytes occupy the upper half of OOB; free area starts at byte 2.
 */
static int hyf_oob_128_ecc(struct mtd_info *mtd, int section,
			    struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int hyf_oob_128_free(struct mtd_info *mtd, int section,
			    struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize / 2 - 2;

	return 0;
}

static const struct mtd_ooblayout_ops hyf_oob_128_layout = {
	.ecc = hyf_oob_128_ecc,
	.rfree = hyf_oob_128_free,
};

static int hyf_ecc_get_status(struct spinand_device *spinand,
			       u8 status)
{
	status = status & HYF_STATUS_ECC_MASK;

	switch (status) {
	case HYF_STATUS_ECC_NO_BITFLIPS:
		return 0;
	case HYF_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;
	case HYF_STATUS_ECC_HAS_BITFLIPS:
		/* 1-3 bits corrected, report 3 to satisfy UBI threshold */
		return 3;
	case HYF_STATUS_ECC_LIMIT_BITFLIPS:
		/* 4-8 bits corrected */
		return 8;
	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info heyang_spinand_table[] = {
	SPINAND_INFO("HYF1GQ4UAACAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x51),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf_oob_128_layout,
				     hyf_ecc_get_status)),
	SPINAND_INFO("HYF2GQ4UAACAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x52),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf_oob_128_layout,
				     hyf_ecc_get_status)),
	SPINAND_INFO("HYF2GQ4UHCCAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x5A),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf_oob_128_layout,
				     hyf_ecc_get_status)),
	SPINAND_INFO("HYF4GQ4UAACBE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xD4),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf_oob_128_layout,
				     hyf_ecc_get_status)),
	SPINAND_INFO("HYF1GQ4UDACAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x21),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf_oob_64_layout,
				     hyf_ecc_get_status)),
	SPINAND_INFO("HYF2GQ4UDACAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x22),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf_oob_64_layout,
				     hyf_ecc_get_status)),
};

static const struct spinand_manufacturer_ops heyang_spinand_manuf_ops = {
};

const struct spinand_manufacturer heyang_spinand_manufacturer = {
	.id = SPINAND_MFR_HEYANG,
	.name = "HeYangTek",
	.chips = heyang_spinand_table,
	.nchips = ARRAY_SIZE(heyang_spinand_table),
	.ops = &heyang_spinand_manuf_ops,
};
