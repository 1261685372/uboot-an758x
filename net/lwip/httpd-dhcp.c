// SPDX-License-Identifier: GPL-2.0+

#include <env.h>
#include <time.h>
#include <net/httpd.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <lwip/def.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>

#define DHCP_SERVER_PORT	67
#define DHCP_CLIENT_PORT	68
#define DHCP_MAGIC_COOKIE	0x63825363
#define DHCP_MIN_PACKET_LEN	300
#define DHCP_LEASE_SECONDS	3600
#define DHCP_MAX_LEASES		16

#define DHCP_BOOTREQUEST	1
#define DHCP_BOOTREPLY		2
#define DHCP_HTYPE_ETHERNET	1

#define DHCP_OPT_PAD		0
#define DHCP_OPT_SUBNET_MASK	1
#define DHCP_OPT_ROUTER		3
#define DHCP_OPT_DNS		6
#define DHCP_OPT_REQUESTED_IP	50
#define DHCP_OPT_LEASE_TIME	51
#define DHCP_OPT_MESSAGE_TYPE	53
#define DHCP_OPT_SERVER_ID	54
#define DHCP_OPT_END		255

#define DHCP_DISCOVER		1
#define DHCP_OFFER		2
#define DHCP_REQUEST		3
#define DHCP_ACK		5
#define DHCP_NAK		6
#define DHCP_RELEASE		7

struct dhcp_packet {
	u8 op;
	u8 htype;
	u8 hlen;
	u8 hops;
	u32 xid;
	u16 secs;
	u16 flags;
	u32 ciaddr;
	u32 yiaddr;
	u32 siaddr;
	u32 giaddr;
	u8 chaddr[16];
	u8 sname[64];
	u8 file[128];
	u32 cookie;
	u8 options[1232];
} __packed;

static struct udp_pcb *dhcp_pcb;
static ip4_addr_t dhcp_server_addr;
static ip4_addr_t dhcp_netmask;
static u32 dhcp_pool_start;
static u32 dhcp_pool_end;

struct dhcp_lease {
	bool used;
	u8 hlen;
	u8 chaddr[16];
	ip4_addr_t addr;
	ulong renewed;
	ulong lifetime_ms;
};

/* Recovery sessions are short and normally serve one directly attached host. */
static struct dhcp_lease dhcp_leases[DHCP_MAX_LEASES];

struct dhcp_options {
	int type;
	u32 requested;
	u32 server;
};

static int dhcp_parse_options(const struct dhcp_packet *packet, size_t len,
			      struct dhcp_options *parsed)
{
	const u8 *p = packet->options;
	const u8 *end = (const u8 *)packet + len;

	memset(parsed, 0, sizeof(*parsed));
	while (p < end) {
		u8 code = *p++, size;

		if (code == DHCP_OPT_END)
			return 0;
		if (code == DHCP_OPT_PAD)
			continue;
		if (p == end)
			return -EINVAL;
		size = *p++;
		if (size > end - p)
			return -EINVAL;
		switch (code) {
		case DHCP_OPT_MESSAGE_TYPE:
			if (size != 1)
				return -EINVAL;
			parsed->type = p[0];
			break;
		case DHCP_OPT_REQUESTED_IP:
		case DHCP_OPT_SERVER_ID:
			if (size != 4)
				return -EINVAL;
			memcpy(code == DHCP_OPT_REQUESTED_IP ?
			       &parsed->requested : &parsed->server, p, 4);
			break;
		}
		p += size;
	}
	return 0;
}

static u8 *dhcp_add_option(u8 *option, u8 code, const void *data, u8 len)
{
	*option++ = code;
	*option++ = len;
	memcpy(option, data, len);
	return option + len;
}

static struct dhcp_lease *dhcp_get_lease(const struct dhcp_packet *request,
					 u32 preferred)
{
	struct dhcp_lease *free_lease = NULL;
	u32 server = PP_NTOHL(dhcp_server_addr.addr);
	u32 mask = PP_NTOHL(dhcp_netmask.addr);
	u32 network = server & mask;
	u32 broadcast = network | ~mask;
	u64 candidate;
	int i;

	if (!request->hlen || request->hlen > sizeof(request->chaddr))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(dhcp_leases); i++) {
		if (dhcp_leases[i].used &&
		    get_timer(dhcp_leases[i].renewed) >= dhcp_leases[i].lifetime_ms)
			dhcp_leases[i].used = false;
		if (dhcp_leases[i].used &&
		    dhcp_leases[i].hlen == request->hlen &&
		    !memcmp(dhcp_leases[i].chaddr, request->chaddr,
			    request->hlen))
			return &dhcp_leases[i];
		if (!dhcp_leases[i].used && !free_lease)
			free_lease = &dhcp_leases[i];
	}
	if (!free_lease)
		return NULL;

	for (candidate = preferred ? preferred : dhcp_pool_start;
	     candidate <= (preferred ? preferred : dhcp_pool_end);
	     candidate++) {
		bool assigned = false;

		if (candidate == network || candidate == broadcast ||
		    candidate == server)
			continue;
		for (i = 0; i < ARRAY_SIZE(dhcp_leases); i++) {
			if (dhcp_leases[i].used &&
			    PP_NTOHL(dhcp_leases[i].addr.addr) == candidate) {
				assigned = true;
				break;
			}
		}
		if (assigned)
			continue;

		free_lease->renewed = get_timer(0);
		free_lease->lifetime_ms = 60000;
		free_lease->used = true;
		free_lease->hlen = request->hlen;
		memcpy(free_lease->chaddr, request->chaddr,
		       sizeof(free_lease->chaddr));
		free_lease->addr.addr = PP_HTONL((u32)candidate);
		return free_lease;
	}

	return NULL;
}

static void dhcp_send_reply(const struct dhcp_packet *request,
			    const struct dhcp_lease *lease, u8 message_type)
{
	struct dhcp_packet reply;
	ip4_addr_t broadcast;
	struct pbuf *p;
	u32 lease_time = PP_HTONL(DHCP_LEASE_SECONDS);
	u8 *option;
	size_t len;
	err_t err;

	memset(&reply, 0, sizeof(reply));
	reply.op = DHCP_BOOTREPLY;
	reply.htype = DHCP_HTYPE_ETHERNET;
	reply.hlen = request->hlen;
	reply.xid = request->xid;
	reply.flags = request->flags;
	reply.yiaddr = lease ? ip4_addr_get_u32(&lease->addr) : 0;
	reply.siaddr = ip4_addr_get_u32(&dhcp_server_addr);
	memcpy(reply.chaddr, request->chaddr, sizeof(reply.chaddr));
	reply.cookie = PP_HTONL(DHCP_MAGIC_COOKIE);

	option = reply.options;
	option = dhcp_add_option(option, DHCP_OPT_MESSAGE_TYPE,
				 &message_type, sizeof(message_type));
	option = dhcp_add_option(option, DHCP_OPT_SERVER_ID,
				 &dhcp_server_addr.addr, sizeof(dhcp_server_addr.addr));
	if (message_type != DHCP_NAK) {
		option = dhcp_add_option(option, DHCP_OPT_LEASE_TIME,
					 &lease_time, sizeof(lease_time));
		option = dhcp_add_option(option, DHCP_OPT_SUBNET_MASK,
					 &dhcp_netmask.addr, sizeof(dhcp_netmask.addr));
		option = dhcp_add_option(option, DHCP_OPT_ROUTER,
					 &dhcp_server_addr.addr, sizeof(dhcp_server_addr.addr));
		option = dhcp_add_option(option, DHCP_OPT_DNS,
					 &dhcp_server_addr.addr, sizeof(dhcp_server_addr.addr));
	
	}
	*option++ = DHCP_OPT_END;

	len = max_t(size_t, DHCP_MIN_PACKET_LEN,
		    offsetof(struct dhcp_packet, options) +
		    (option - reply.options));
	p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
	if (!p)
		return;
	pbuf_take(p, &reply, len);
	IP4_ADDR(&broadcast, 255, 255, 255, 255);
	err = udp_sendto(dhcp_pcb, p, &broadcast, DHCP_CLIENT_PORT);
	if (err != ERR_OK)
		printf("DHCP reply failed: %d\n", err);
	pbuf_free(p);
}

static void dhcp_receive(void *arg, struct udp_pcb *pcb, struct pbuf *p,
			 const ip_addr_t *addr, u16_t port)
{
	struct dhcp_packet request;
	struct dhcp_options options;
	struct dhcp_lease *lease;
	u32 requested, server = PP_NTOHL(dhcp_server_addr.addr);
	u32 mask = PP_NTOHL(dhcp_netmask.addr);
	int i;

	if (port != DHCP_CLIENT_PORT ||
	    p->tot_len < offsetof(struct dhcp_packet, options) ||
	    p->tot_len > sizeof(request))
		goto out;
	memset(&request, 0, sizeof(request));
	pbuf_copy_partial(p, &request, p->tot_len, 0);
	if (request.op != DHCP_BOOTREQUEST || request.hlen != 6 ||
	    request.htype != DHCP_HTYPE_ETHERNET || request.giaddr ||
	    request.cookie != PP_HTONL(DHCP_MAGIC_COOKIE) ||
	    dhcp_parse_options(&request, p->tot_len, &options))
		goto out;
	if (options.server && options.server != dhcp_server_addr.addr)
		goto out;
	if (options.type == DHCP_RELEASE) {
		for (i = 0; i < ARRAY_SIZE(dhcp_leases); i++)
			if (dhcp_leases[i].used &&
			    !memcmp(dhcp_leases[i].chaddr, request.chaddr, 6))
				dhcp_leases[i].used = false;
		goto out;
	}
	if (options.type != DHCP_DISCOVER && options.type != DHCP_REQUEST)
		goto out;
	requested = PP_NTOHL(options.requested ? options.requested : request.ciaddr);
	if (requested && (requested < dhcp_pool_start || requested > dhcp_pool_end ||
	    requested == server || requested == (server & mask) ||
	    requested == ((server & mask) | ~mask))) {
		if (options.type == DHCP_REQUEST) {
			/* A stale address from another subnet restarts client discovery. */
			dhcp_send_reply(&request, NULL, DHCP_NAK);
			goto out;
		}
		requested = 0;
	}
	lease = dhcp_get_lease(&request, requested);
	if (!lease && requested && options.type == DHCP_DISCOVER)
		lease = dhcp_get_lease(&request, 0);
	if (!lease || (options.type == DHCP_REQUEST && requested &&
		       PP_NTOHL(lease->addr.addr) != requested)) {
		if (options.type == DHCP_REQUEST)
			dhcp_send_reply(&request, NULL, DHCP_NAK);
		goto out;
	}
	lease->renewed = get_timer(0);
	lease->lifetime_ms = options.type == DHCP_REQUEST ?
			    DHCP_LEASE_SECONDS * 1000UL : 60000;
	dhcp_send_reply(&request, lease, options.type == DHCP_DISCOVER ?
			DHCP_OFFER : DHCP_ACK);
out:
	pbuf_free(p);
}

int uboot_httpd_dhcp_start(struct netif *netif)
{
	const char *pool_start = env_get("httpd_dhcp_start");
	const char *pool_end = env_get("httpd_dhcp_end");
	ip4_addr_t first;
	ip4_addr_t last;
	u32 server;
	u32 mask;
	u32 network;
	err_t err;

	if (dhcp_pcb)
		return 0;

	ip4_addr_copy(dhcp_server_addr, *netif_ip4_addr(netif));
	ip4_addr_copy(dhcp_netmask, *netif_ip4_netmask(netif));
	server = PP_NTOHL(dhcp_server_addr.addr);
	mask = PP_NTOHL(dhcp_netmask.addr);
	network = server & mask;

	if (pool_start || pool_end) {
		if (!pool_start || !pool_end ||
		    !ip4addr_aton(pool_start, &first) ||
		    !ip4addr_aton(pool_end, &last))
			return -EINVAL;
		dhcp_pool_start = PP_NTOHL(first.addr);
		dhcp_pool_end = PP_NTOHL(last.addr);
	} else {
		/* Host-number defaults follow the configured recovery subnet. */
		dhcp_pool_start = network | CONFIG_HTTPD_DHCP_POOL_START;
		dhcp_pool_end = network | CONFIG_HTTPD_DHCP_POOL_END;
	}
	if (dhcp_pool_start > dhcp_pool_end ||
	    (dhcp_pool_start & mask) != network ||
	    (dhcp_pool_end & mask) != network)
		return -EINVAL;
	memset(dhcp_leases, 0, sizeof(dhcp_leases));

	dhcp_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
	if (!dhcp_pcb)
		return -ENOMEM;
	err = udp_bind(dhcp_pcb, IP_ANY_TYPE, DHCP_SERVER_PORT);
	if (err != ERR_OK) {
		udp_remove(dhcp_pcb);
		dhcp_pcb = NULL;
		return err;
	}
	udp_recv(dhcp_pcb, dhcp_receive, NULL);
	first.addr = PP_HTONL(dhcp_pool_start);
	last.addr = PP_HTONL(dhcp_pool_end);
	printf("DHCP pool: %s - ", ip4addr_ntoa(&first));
	printf("%s\n", ip4addr_ntoa(&last));
	return 0;
}

void uboot_httpd_dhcp_stop(void)
{
	if (!dhcp_pcb)
		return;
	udp_remove(dhcp_pcb);
	dhcp_pcb = NULL;
}
