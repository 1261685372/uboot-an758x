// SPDX-License-Identifier: GPL-2.0+

#include <command.h>
#include <net/httpd.h>
#include <string.h>

static int do_httpd(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	bool with_dhcp = argc == 2 && !strcmp(argv[1], "dhcp");

	if (argc > 2 || (argc == 2 && !with_dhcp))
		return CMD_RET_USAGE;

	if (uboot_httpd_is_running())
		return CMD_RET_SUCCESS;

	return uboot_httpd_start(with_dhcp) ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(httpd, 2, 0, do_httpd,
	   "start the browser management and recovery service",
	   "[dhcp]\n"
	   "    - serve HTTP on the current ipaddr; 'dhcp' also leases one address");
