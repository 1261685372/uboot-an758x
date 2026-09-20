/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __NET_HTTPD_H__
#define __NET_HTTPD_H__

#include <linux/errno.h>
#include <linux/types.h>

struct netif;

int uboot_httpd_start(bool with_dhcp);
bool uboot_httpd_is_running(void);

#if CONFIG_IS_ENABLED(HTTPD_DHCP_SERVER)
int uboot_httpd_dhcp_start(struct netif *netif);
void uboot_httpd_dhcp_stop(void);
#else
static inline int uboot_httpd_dhcp_start(struct netif *netif)
{
	return -ENOSYS;
}

static inline void uboot_httpd_dhcp_stop(void)
{
}
#endif

#endif
