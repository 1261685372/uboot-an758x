// SPDX-License-Identifier: GPL-2.0+
#include <button.h>
#include <event.h>
#include <linker_lists.h>
#include <net/httpd.h>
#include <stdio.h>

static int recovery_button_check(void)
{
	struct udevice *button;
	int ret;

	ret = button_get_by_label("reset", &button);
	if (ret)
		return 0;
	ret = button_get_state(button);
	if (ret == BUTTON_ON) {
		/* CLI and networking are ready before the automatic boot menu runs. */
		puts("Reset held: entering HTTP recovery\n");
		ret = uboot_httpd_start(true);
		if (ret)
			printf("HTTP recovery failed (%d)\n", ret);
	} else if (ret < 0) {
		printf("Reset button read failed (%d)\n", ret);
	}
	return 0;
}
EVENT_SPY_SIMPLE(EVT_POST_PREBOOT, recovery_button_check);
