// SPDX-License-Identifier: GPL-2.0-only
/*
 * Airoha EN75xx parallel NAND protocol frontend
 *
 * The NFI register backend in spi_nfi.c is shared with SPI NAND.  Parallel
 * NAND differs at the wire protocol boundary: commands and row/column address
 * cycles are issued directly and page data moves through the NFI DMA engine.
 */

#include <stdint.h>
#include <string.h>

#include <arch_helpers.h>
#include <common/debug.h>
#include <lib/mmio.h>

#include <ecnt_spi_ecc.h>
#include <ecnt_spi_nand_flash.h>
#include <ecnt_spi_nfi.h>

#define PNAND_CHIP_COUNT	2
#define PNAND_ID_LEN		5
#define PNAND_STATUS_LEN	1
#define PNAND_COLUMN_CYCLES	2
#define PNAND_STATUS_FAIL	0x01

#if defined(TCSUPPORT_CPU_EN7581)
#define EN7581_CHIP_SCU_GPIO_PON_MODE	0x1fa2021c
#define EN7581_GPIO_PARALLEL_NAND_MODE	(1U << 14)
#define EN7581_NFI_MODE			0x1fa1155c
#define EN7581_NFI_SERIAL_MODE		(1U << 2)
#define EN7581_NFI_CNFG			0x1fa11000
#define EN7581_NFI_CNFG_OP_MODE		(7U << 12)
#define EN7581_NFI_CNFG_OP_SINGLE_READ	(2U << 12)
#define EN7581_NFI_CNFG_AUTO_FMT		(1U << 9)
#define EN7581_NFI_CNFG_HW_ECC		(1U << 8)
#define EN7581_NFI_CNFG_BYTE_RW		(1U << 6)
#define EN7581_NFI_CNFG_ECC_DATA_INV	(1U << 5)
#define EN7581_NFI_CNFG_DMA_BURST	(1U << 2)
#define EN7581_NFI_CNFG_READ_MODE	(1U << 1)
#define EN7581_NFI_CNFG_AHB		(1U << 0)
#define EN7581_NFI_CON			0x1fa11008
#define EN7581_NFI_CON_NOB_SHIFT		5
#define EN7581_NFI_CON_SRD		(1U << 4)
#define EN7581_NFI_INTR_EN		0x1fa11010
#define EN7581_NFI_INTR			0x1fa11014
#define EN7581_NFI_CMD			0x1fa11020
#define EN7581_NFI_CNRNB			0x1fa11044
#define EN7581_NFI_DATAR			0x1fa11054
#define EN7581_NFI_PIO_DIRDY		0x1fa11058
#define EN7581_NFI_PIO_READY		(1U << 0)
#define EN7581_NFI_STA			0x1fa11060
#define EN7581_NFI_STA_CMD		(1U << 0)
#define EN7581_NFI_RESET			0x00000003
#define EN7581_NFI_AHB_DONE_INTR		(1U << 6)
#define EN7581_NFI_FSM_BUSY		0x0000000f
#define EN7581_NFI_RESET_TIMEOUT		1000000U
#endif

extern struct SPI_NAND_FLASH_INFO_T _current_flash_info_t;
extern SPI_NAND_FLASH_RTN_T
parallel_nand_scan_flash_table(struct SPI_NAND_FLASH_INFO_T *info);

static struct SPI_NAND_FLASH_INFO_T *pnand_info(void)
{
	return &_current_flash_info_t;
}

static void pnand_select_chip(uint8_t chip)
{
	PARALLEL_NFI_SET_CHIP_SELECT(chip);
}

static void pnand_select_page(uint32_t *page)
{
	struct SPI_NAND_FLASH_INFO_T *info = pnand_info();
	uint32_t pages_per_chip;
	uint8_t chip = 0;

	/* Dual-CE devices split capacity evenly and restart row addressing on CE1. */
	if (info->feature & PARALLEL_NAND_FLASH_2CE) {
		pages_per_chip = info->device_size / info->page_size / 2;
		if (*page >= pages_per_chip) {
			*page -= pages_per_chip;
			chip = 1;
		}
	}

	pnand_select_chip(chip);
}

static SPI_NFI_RTN_T pnand_reset_controller(void)
{
#if defined(TCSUPPORT_CPU_EN7581)
	uint32_t timeout = EN7581_NFI_RESET_TIMEOUT;

	/*
	 * CON resets the FIFO and parallel state machine together.  Waiting for
	 * the command/address state bits to clear prevents READID from observing
	 * the reset transaction's data phase.
	 */
	mmio_write_32(EN7581_NFI_CON, EN7581_NFI_RESET);
	while ((mmio_read_32(EN7581_NFI_STA) & EN7581_NFI_FSM_BUSY) && --timeout)
		;
	if (!timeout) {
		ERROR("parallel NAND controller reset timeout: sta=%08x\n",
		      mmio_read_32(EN7581_NFI_STA));
		return SPI_NFI_RTN_WAIT_TIMEOUT;
	}

	/* The second reset edge drains FIFO state left by the completed reset. */
	mmio_write_32(EN7581_NFI_CON, EN7581_NFI_RESET);
#else
	SPI_NFI_Reset();
#endif

	return SPI_NFI_RTN_NO_ERROR;
}

static void pnand_hw_init(void)
{

#if defined(TCSUPPORT_CPU_EN7581)
	/*
	 * BL21 performs SoC setup after BootROM has read the first NAND block.
	 * Restoring the PNAND mux here keeps CLE, ALE, CE, RE, WE, R/B and the
	 * eight-bit data bus connected to NFI when BL23 begins independent I/O.
	 */
	mmio_setbits_32(EN7581_CHIP_SCU_GPIO_PON_MODE,
			EN7581_GPIO_PARALLEL_NAND_MODE);
	/* Bit 2 selects the serial frontend sharing this NFI register bank. */
	mmio_clrbits_32(EN7581_NFI_MODE, EN7581_NFI_SERIAL_MODE);

	/*
	 * The reset-ready checker uses CNRNB, while page DMA uses AHB_DONE.  Reading
	 * INTR consumes completion state inherited from BootROM before BL23 starts
	 * its first NAND transaction.
	 */
	mmio_write_32(EN7581_NFI_CNRNB, 0x000000f1);
	pnand_reset_controller();
	(void)mmio_read_32(EN7581_NFI_INTR);
	mmio_write_32(EN7581_NFI_INTR_EN, EN7581_NFI_AHB_DONE_INTR);
#else
	pnand_reset_controller();
	PARALLEL_NFI_INIT();
#endif
}

static SPI_NAND_FLASH_RTN_T pnand_transfer(unsigned long *data, uint8_t dir)
{
	if (PARALLEL_NFI_START_DMA(data, dir) != SPI_NFI_RTN_NO_ERROR)
		return SPI_NAND_FLASH_RTN_NFI_FAIL;

	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

static SPI_NAND_FLASH_RTN_T pnand_read_bytes(uint8_t len, uint8_t *data)
{
#if defined(TCSUPPORT_CPU_EN7581)
	uint32_t timeout;
	uint32_t pio;
	uint8_t remaining = len;

	if (!len || len > 7 || !data)
		return SPI_NAND_FLASH_RTN_NFI_FAIL;

	/*
	 * CON still contains the FIFO-reset edge after pnand_reset_controller().
	 * A full register write starts the short-read state machine with only SRD
	 * and its byte count asserted, matching the transaction used by U-Boot.
	 */
	mmio_write_32(EN7581_NFI_CON,
		      ((uint32_t)len << EN7581_NFI_CON_NOB_SHIFT) |
		      EN7581_NFI_CON_SRD);

	while (remaining) {
		uint32_t value;

		timeout = EN7581_NFI_RESET_TIMEOUT;
		do {
			pio = mmio_read_32(EN7581_NFI_PIO_DIRDY);
		} while (!(pio & EN7581_NFI_PIO_READY) && --timeout);
		if (!timeout) {
			ERROR("parallel NAND PIO read timeout: con=%08x sta=%08x\n",
			      mmio_read_32(EN7581_NFI_CON),
			      mmio_read_32(EN7581_NFI_STA));
			mmio_write_32(EN7581_NFI_CON, 0);
			return SPI_NAND_FLASH_RTN_NFI_FAIL;
		}

		/*
		 * Reading DATAR consumes one FIFO byte.  Completing that device access
		 * before the next PIO_DIRDY poll lets NFI expose the following byte.
		 */
		value = mmio_read_32(EN7581_NFI_DATAR);
		dsb();
		*data++ = value & 0xff;
		remaining--;
	}

	mmio_write_32(EN7581_NFI_CON, 0);
	return SPI_NAND_FLASH_RTN_NO_ERROR;
#else
	if (PARALLEL_NFI_START_BYTE_READ(len, data) != SPI_NFI_RTN_NO_ERROR)
		return SPI_NAND_FLASH_RTN_NFI_FAIL;

	return SPI_NAND_FLASH_RTN_NO_ERROR;
#endif
}

#if defined(TCSUPPORT_CPU_EN7581)
static SPI_NFI_RTN_T pnand_issue_short_read_command(uint8_t command)
{
	uint32_t config;
	uint32_t timeout = EN7581_NFI_RESET_TIMEOUT;

	/*
	 * BL21 and BootROM leave NFI configured for their final transfer.  The
	 * opening command owns the next transaction, so every short read replaces
	 * all direction, DMA, formatter, and ECC controls before CMD is asserted.
	 */
	config = mmio_read_32(EN7581_NFI_CNFG);
	config &= ~(EN7581_NFI_CNFG_OP_MODE |
		    EN7581_NFI_CNFG_AUTO_FMT |
		    EN7581_NFI_CNFG_HW_ECC |
		    EN7581_NFI_CNFG_BYTE_RW |
		    EN7581_NFI_CNFG_ECC_DATA_INV |
		    EN7581_NFI_CNFG_DMA_BURST |
		    EN7581_NFI_CNFG_READ_MODE |
		    EN7581_NFI_CNFG_AHB);
	config |= EN7581_NFI_CNFG_OP_SINGLE_READ |
		  EN7581_NFI_CNFG_BYTE_RW |
		  EN7581_NFI_CNFG_READ_MODE;
	mmio_write_32(EN7581_NFI_CNFG, config);
	mmio_write_32(EN7581_NFI_CMD, command);

	while ((mmio_read_32(EN7581_NFI_STA) & EN7581_NFI_STA_CMD) && --timeout)
		;
	if (!timeout) {
		ERROR("parallel NAND command 0x%02x timeout: cnfg=%08x sta=%08x\n",
		      command, mmio_read_32(EN7581_NFI_CNFG),
		      mmio_read_32(EN7581_NFI_STA));
		return SPI_NFI_RTN_WAIT_TIMEOUT;
	}

	return SPI_NFI_RTN_NO_ERROR;
}
#endif

static SPI_NAND_FLASH_RTN_T pnand_send_address(uint32_t column,
						uint32_t row,
						uint8_t column_cycles,
						uint8_t row_cycles)
{
	SPI_NFI_RTN_T ret;

	/* EN7581 latches column, row and both cycle counts as one address phase. */
	ret = PARALLEL_NFI_ISSUE_ADDR(column, row, column_cycles, row_cycles);
	if (ret == SPI_NFI_RTN_ACCESS_LOCK)
		return SPI_NAND_FLASH_RTN_CMD_ABORT;
	if (ret != SPI_NFI_RTN_NO_ERROR)
		return SPI_NAND_FLASH_RTN_NFI_FAIL;

	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

static SPI_NAND_FLASH_RTN_T pnand_send_command(uint8_t command)
{
	SPI_NFI_RTN_T ret = SPI_NFI_RTN_NO_ERROR;

	switch (command) {
	case NAND_CMD_READID:
	case NAND_CMD_STATUS:
	case NAND_CMD_ERASE1:
		/* A new transaction starts after clearing command, address, DMA, and IRQ state. */
		ret = pnand_reset_controller();
		if (ret != SPI_NFI_RTN_NO_ERROR)
			break;
		/* fall through */
	case NAND_CMD_READ:
	case NAND_CMD_SEQIN:
#if defined(TCSUPPORT_CPU_EN7581)
		if (command == NAND_CMD_READID || command == NAND_CMD_STATUS)
			ret = pnand_issue_short_read_command(command);
		else
			PARALLEL_NFI_ISSUE_CMD_1(command);
#else
		PARALLEL_NFI_ISSUE_CMD_1(command);
#endif
		break;
	case NAND_CMD_RESET:
	case NAND_CMD_ERASE2:
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_READSTART:
		ret = PARALLEL_NFI_ISSUE_CMD_2(command);
		break;
	default:
		ERROR("parallel NAND: unsupported command 0x%x\n", command);
		return SPI_NAND_FLASH_RTN_NFI_FAIL;
	}

	return ret == SPI_NFI_RTN_NO_ERROR ? SPI_NAND_FLASH_RTN_NO_ERROR :
						  SPI_NAND_FLASH_RTN_NFI_FAIL;
}

static SPI_NAND_FLASH_RTN_T pnand_read_page(uint32_t column, uint32_t page,
					     unsigned long *data)
{
	SPI_NAND_FLASH_RTN_T ret;

	ret = pnand_send_command(NAND_CMD_READ);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR)
		return ret;

	ret = pnand_send_address(column, page, PNAND_COLUMN_CYCLES,
				  pnand_info()->addr_cycle - PNAND_COLUMN_CYCLES);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR)
		return ret;

	ret = pnand_send_command(NAND_CMD_READSTART);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR)
		return ret;

	return pnand_transfer(data, SPI_NFI_READ_DATA);
}

static SPI_NAND_FLASH_RTN_T pnand_program_page(uint32_t column,
						uint32_t page,
						unsigned long *data)
{
	SPI_NAND_FLASH_RTN_T ret;

	ret = pnand_send_command(NAND_CMD_SEQIN);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR)
		return ret;

	ret = pnand_send_address(column, page, PNAND_COLUMN_CYCLES,
				  pnand_info()->addr_cycle - PNAND_COLUMN_CYCLES);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR)
		return ret;

	ret = pnand_transfer(data, SPI_NFI_WRITE_DATA);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR)
		return ret;

	return pnand_send_command(NAND_CMD_PAGEPROG);
}

SPI_NAND_FLASH_RTN_T parallel_nand_protocol_get_status(uint8_t *status)
{
	uint8_t value[8] = { 0 };
	SPI_NAND_FLASH_RTN_T ret;

	ret = pnand_send_command(NAND_CMD_STATUS);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR)
		return ret;

	ret = pnand_read_bytes(PNAND_STATUS_LEN, value);
	if (ret == SPI_NAND_FLASH_RTN_NO_ERROR)
		*status = value[0];

	return ret;
}

static SPI_NAND_FLASH_RTN_T pnand_erase_block(uint32_t page)
{
	SPI_NAND_FLASH_RTN_T ret;

	ret = pnand_send_command(NAND_CMD_ERASE1);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR)
		return ret;

	ret = pnand_send_address(0, page, 0,
				  pnand_info()->addr_cycle - PNAND_COLUMN_CYCLES);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR)
		return ret;

	return pnand_send_command(NAND_CMD_ERASE2);
}

static SPI_NAND_FLASH_RTN_T
pnand_read_id(struct SPI_NAND_FLASH_INFO_T *id)
{
	uint8_t value[8] = { 0 };
	SPI_NAND_FLASH_RTN_T ret;

	ret = pnand_send_command(NAND_CMD_READID);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR) {
		ERROR("parallel NAND READID command failed: status=%d\n", ret);
		return ret;
	}

	ret = pnand_send_address(0, 0, 1, 0);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR) {
		ERROR("parallel NAND READID address failed: status=%d\n", ret);
		return ret;
	}

	ret = pnand_read_bytes(PNAND_ID_LEN, value);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR) {
		ERROR("parallel NAND READID data failed: status=%d\n", ret);
		return ret;
	}

	id->mfr_id = value[0];
	id->dev_id = value[1];
	id->ext_id = value[2] | (value[3] << 8) | (value[4] << 16);

	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

SPI_NAND_FLASH_RTN_T parallel_nand_protocol_reset(void)
{
	return pnand_send_command(NAND_CMD_RESET);
}

SPI_NAND_FLASH_RTN_T
parallel_nand_probe(struct SPI_NAND_FLASH_INFO_T *result)
{
	struct SPI_NAND_FLASH_INFO_T detected[PNAND_CHIP_COUNT];
	SPI_NAND_FLASH_RTN_T ret = SPI_NAND_FLASH_RTN_PROBE_ERROR;
	unsigned int chip;

	memset(detected, 0, sizeof(detected));
	pnand_hw_init();

	for (chip = 0; chip < PNAND_CHIP_COUNT; chip++) {
		pnand_select_chip(chip);
		ret = parallel_nand_protocol_reset();
		if (ret != SPI_NAND_FLASH_RTN_NO_ERROR) {
			ERROR("parallel NAND CE%u reset failed: status=%d\n",
			      chip, ret);
			continue;
		}
		ret = pnand_read_id(&detected[chip]);
		if (ret == SPI_NAND_FLASH_RTN_NO_ERROR &&
		    detected[chip].mfr_id != 0 && detected[chip].mfr_id != 0xff &&
		    detected[chip].dev_id != 0 && detected[chip].dev_id != 0xff &&
		    detected[chip].ext_id != 0 && detected[chip].ext_id != 0xffffff)
			break;
	}

	if (chip == PNAND_CHIP_COUNT) {
		ERROR("parallel NAND probe exhausted all chip selects\n");
		return SPI_NAND_FLASH_RTN_PROBE_ERROR;
	}

	memcpy(result, &detected[chip], sizeof(*result));
	ret = parallel_nand_scan_flash_table(result);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR) {
		ERROR("parallel NAND: unsupported ID %02x:%02x:%06x\n",
		      detected[chip].mfr_id, detected[chip].dev_id,
		      detected[chip].ext_id);
		return ret;
	}

	if (result->soc_ecc_ability < result->min_ecc_req) {
		ERROR("parallel NAND: ECC%d is below device requirement ECC%d\n",
		      result->soc_ecc_ability, result->min_ecc_req);
		return SPI_NAND_FLASH_RTN_PROBE_ERROR;
	}

	NOTICE("Parallel NAND: %s, ID %02x:%02x:%06x, page %u, erase %u, ECC%u/512\n",
	       result->ptr_name, result->mfr_id, result->dev_id, result->ext_id,
	       result->page_size, result->erase_size, result->soc_ecc_ability);

	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

SPI_NAND_FLASH_RTN_T parallel_nand_dma_read(uint32_t column,
					     uint32_t page,
					     unsigned long *data)
{
	uint32_t requested_page = page;
	SPI_NAND_FLASH_RTN_T ret;

	pnand_select_page(&page);
	ret = pnand_read_page(column, page, data);
	if (ret != SPI_NAND_FLASH_RTN_NO_ERROR)
		ERROR("parallel NAND page read failed: requested-page=0x%x "
		      "selected-page=0x%x column=0x%x status=%d\n",
		      requested_page, page, column, ret);
	return ret;
}

SPI_NAND_FLASH_RTN_T parallel_nand_dma_write(uint32_t column,
					      uint32_t page,
					      unsigned long *data,
					      uint32_t oob_len,
					      uint8_t *oob)
{
	SPI_ECC_ENCODE_CONF_T ecc;
	SPI_NFI_CONF_T nfi;
	SPI_NAND_FLASH_RTN_T ret;
	uint8_t status;

	/*
	 * Program operations must restart the encoder for every page.  Auto-FDM
	 * copies the logical OOB bytes into FDM registers before DMA consumes them.
	 */
	SPI_NFI_Reset();
	SPI_NFI_Get_Configure(&nfi);
	SPI_NFI_Set_Configure(&nfi);
	SPI_ECC_Encode_Get_Configure(&ecc);
	SPI_ECC_Encode_Set_Configure(&ecc);

	if (nfi.hw_ecc_t == SPI_NFI_CON_HW_ECC_Enable &&
	    ecc.encode_en == SPI_ECC_ENCODE_ENABLE) {
		ecc.encode_block_size = nfi.fdm_ecc_num + 512;
		SPI_ECC_Encode_Set_Configure(&ecc);
		SPI_ECC_Encode_Disable();
		SPI_ECC_Decode_Disable();
		SPI_ECC_Encode_Enable();
	}

	if (nfi.auto_fdm_t == SPI_NFI_CON_AUTO_FDM_Enable)
		SPI_NFI_Write_SPI_NAND_FDM(oob, oob_len);

	pnand_select_page(&page);
	ret = pnand_program_page(column, page, data);
	if (ret == SPI_NAND_FLASH_RTN_NO_ERROR)
		ret = parallel_nand_protocol_get_status(&status);
	if (ret == SPI_NAND_FLASH_RTN_NO_ERROR && (status & PNAND_STATUS_FAIL))
		ret = SPI_NAND_FLASH_RTN_PROGRAM_FAIL;

	/* Access protection reports a transaction that left the media unchanged. */
	if (ret == SPI_NAND_FLASH_RTN_CMD_ABORT)
		ret = SPI_NAND_FLASH_RTN_NO_ERROR;

	return ret;
}

SPI_NAND_FLASH_RTN_T parallel_nand_erase(uint32_t page)
{
	SPI_NAND_FLASH_RTN_T ret;
	uint8_t status;

	pnand_select_page(&page);
	ret = pnand_erase_block(page);
	if (ret == SPI_NAND_FLASH_RTN_NO_ERROR)
		ret = parallel_nand_protocol_get_status(&status);
	if (ret == SPI_NAND_FLASH_RTN_NO_ERROR && (status & PNAND_STATUS_FAIL))
		ret = SPI_NAND_FLASH_RTN_ERASE_FAIL;
	if (ret == SPI_NAND_FLASH_RTN_CMD_ABORT)
		ret = SPI_NAND_FLASH_RTN_NO_ERROR;

	return ret;
}
