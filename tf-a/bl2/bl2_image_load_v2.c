/*
 * Copyright (c) 2016-2022, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stdint.h>

#include <arch.h>
#include <arch_helpers.h>
#include "bl2_private.h"
#include <common/bl_common.h>
#include <common/debug.h>
#include <common/desc_image_load.h>
#include <drivers/auth/auth_mod.h>
#include <plat/common/platform.h>

#include <platform_def.h>

#if defined(IMAGE_BL23)
extern void bl2_mem_params_backup(void);
extern void bl2_mem_params_restore(void);
extern void plat_ecnt_io_switch_to_memmap(void);
extern int fip_image_xmodem_load(void *loadaddr, int max_size);
#endif

static int bl2_do_load_images(void)
{
	bl_load_info_t *bl2_load_info;
	const bl_load_info_node_t *bl2_node_info;
	static int plat_setup_done;
	int err;

	bl2_load_info = plat_get_bl_image_load_info();
	assert(bl2_load_info != NULL);
	assert(bl2_load_info->head != NULL);
	assert(bl2_load_info->h.type == PARAM_BL_LOAD_INFO);
	assert(bl2_load_info->h.version >= VERSION_2);
	bl2_node_info = bl2_load_info->head;

	while (bl2_node_info != NULL) {
		if ((bl2_node_info->image_info->h.attr &
		    IMAGE_ATTRIB_PLAT_SETUP) != 0U) {
			if (plat_setup_done != 0) {
				WARN("BL2: Platform setup already done!!\n");
			} else {
				INFO("BL2: Doing platform setup\n");
				bl2_platform_setup();
				plat_setup_done = 1;
			}
		}

		err = bl2_plat_handle_pre_image_load(bl2_node_info->image_id);
		if (err != 0) {
			ERROR("BL2: Failure in pre image load handling (%i)\n", err);
			return err;
		}

		if ((bl2_node_info->image_info->h.attr &
		    IMAGE_ATTRIB_SKIP_LOADING) == 0U) {
			INFO("BL2: Loading image id %u\n", bl2_node_info->image_id);
			err = load_auth_image(bl2_node_info->image_id,
				bl2_node_info->image_info);
			if (err != 0) {
				ERROR("BL2: Failed to load image id %u (%i)\n",
				      bl2_node_info->image_id, err);
				return err;
			}
		} else {
			INFO("BL2: Skip loading image id %u\n", bl2_node_info->image_id);
		}

		err = bl2_plat_handle_post_image_load(bl2_node_info->image_id);
		if (err != 0) {
			ERROR("BL2: Failure in post image load handling (%i)\n", err);
			return err;
		}

		bl2_node_info = bl2_node_info->next_load_info;
	}

	return 0;
}

struct entry_point_info *bl2_load_images(void)
{
	bl_params_t *bl2_to_next_bl_params;
	int err;

#if defined(IMAGE_BL23)
	bl2_mem_params_backup();
#endif

	err = bl2_do_load_images();
	if (err != 0) {
#if !defined(IMAGE_BL23)
		plat_error_handler(err);
#else
		for (;;) {
			bl2_mem_params_restore();
			plat_ecnt_io_switch_to_memmap();

			ERROR("Stored BL31 + U-Boot FIP failed (%i)\n", err);
			fip_image_xmodem_load((void *)(uintptr_t)PLAT_ECNT_FIP_BASE,
					      PLAT_ECNT_FIP_MAX_SIZE);

			err = bl2_do_load_images();
			if (err == 0)
				break;

			ERROR("Downloaded FIP failed verification (%i); retrying\n", err);
		}
#endif
	}

	bl2_to_next_bl_params = plat_get_next_bl_params();
	assert(bl2_to_next_bl_params != NULL);
	assert(bl2_to_next_bl_params->head != NULL);
	assert(bl2_to_next_bl_params->h.type == PARAM_BL_PARAMS);
	assert(bl2_to_next_bl_params->h.version >= VERSION_2);
	assert(bl2_to_next_bl_params->head->ep_info != NULL);

	if (bl2_to_next_bl_params->head->ep_info->args.arg0 == (u_register_t)0)
		bl2_to_next_bl_params->head->ep_info->args.arg0 =
					(u_register_t)bl2_to_next_bl_params;

	plat_flush_next_bl_params();

	return bl2_to_next_bl_params->head->ep_info;
}
