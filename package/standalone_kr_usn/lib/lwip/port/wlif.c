/* lwIP includes. */
#include <string.h>
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "netif/etharp.h"
#include "netif/conf_wl.h"
#include "driver_nrc_tx.h"
#include "nrc_wifi.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"

#define netifMTU                            ( 1500 )
#define netifINTERFACE_TASK_STACK_SIZE      ( 350 )
#define netifINTERFACE_TASK_PRIORITY        ( configMAX_PRIORITIES - 2 )
#define netifGUARD_BLOCK_TIME               ( 250 )


/* The time to block waiting for input. */
#define BLOCK_TIME_WAITING_FOR_INPUT    ( ( TickType_t ) 100 )

/* lwIP definitions. */
struct wlif
{
    struct eth_addr *ethaddr;
};

extern struct netif* nrc_netif[MAX_IF];
extern struct netif eth_netif;
/**
 * In this function, the hardware should be initialized.
 * Called from wlif_init().
 *
 * @param netif the already initialized lwip network interface structure
 *        for this ethernetif
 */
err_t low_level_init(struct netif *netif)
{
	unsigned portBASE_TYPE uxPriority;

	/* set MAC hardware address length */
	netif->hwaddr_len = NETIF_MAX_HWADDR_LEN;

	/* maximum transfer unit */
	netif->mtu = netifMTU;

	/* broadcast capability */
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

	return ERR_OK;
}
/*-----------------------------------------------------------*/

/*
 * low_level_output(): Should do the actual transmission of the packet. The
 * packet is contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 */

static err_t low_level_output( struct netif *netif, struct pbuf *p )
{
	struct pbuf *q;
	err_t xReturn = ERR_OK;
	const int MAX_FRAME_NUM = 4;
	uint8_t *frames[MAX_FRAME_NUM];
	uint16_t frame_len[MAX_FRAME_NUM];
	int i = 0;

	for( q = p; q != NULL; q = q->next )
	{
		frames[i] = q->payload;
		frame_len[i] = q->len;
		i++;
	}
	nrc_transmit_from_8023_mb(netif->num, frames, frame_len, i);
	LINK_STATS_INC(link.xmit);

	return ERR_OK;
}

#if 1
void lwif_input_from_net80211_pbuf(struct pbuf* p)
{
	struct eth_hdr      *ethhdr;
	struct netif *netif = nrc_netif[0];
	if (p != NULL) {
		LINK_STATS_INC(link.recv);
	} else {
		LINK_STATS_INC(link.memerr);
		LINK_STATS_INC(link.drop);
		return;
	}

	/* points to packet payload, which starts with an Ethernet header */
	ethhdr = p->payload;

	switch (htons(ethhdr->type)) {
		/* IP or ARP packet? */
		case ETHTYPE_IP:
		case ETHTYPE_ARP:
#if PPPOE_SUPPORT
		/* PPPoE packet? */
		case ETHTYPE_PPPOEDISC:
		case ETHTYPE_PPPOE:
#endif /* PPPOE_SUPPORT */
		/* full packet send to tcpip_thread to process */
			if (netif->input(p, netif)!=ERR_OK) {
				LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
				pbuf_free(p);
				p = NULL;
			}
			break;

		default:
			pbuf_free(p);
			p = NULL;
			break;
	}
}
#endif



void lwif_input(uint8_t vif_id, void *buffer, int data_len)
{
	struct eth_hdr      *ethhdr;
	struct netif *netif = nrc_netif[vif_id];
	struct pbuf         *p = NULL, *q;
	int remain = data_len;
	int offset = 0;
	int len = data_len;

	p = pbuf_alloc( PBUF_RAW, len, PBUF_POOL );

	if( p != NULL )
	{
		for( q = p; q != NULL; (q = q->next)&&remain )
		{
			/* Read enough bytes to fill this pbuf in the chain. The
			   available data in the pbuf is given by the q->len variable. */
			memcpy(q->payload, (uint8_t*)buffer + offset, q->len);
			remain -= q->len;
			offset += q->len;
		}
		LINK_STATS_INC(link.recv);
	}
	else
	{
		LINK_STATS_INC(link.memerr);
		LINK_STATS_INC(link.drop);
		return;
	}

	/* points to packet payload, which starts with an Ethernet header */
	ethhdr = p->payload;

	switch (htons(ethhdr->type)) {
		/* IP or ARP packet? */
		case ETHTYPE_ARP:
#if PPPOE_SUPPORT
		/* PPPoE packet? */
		case ETHTYPE_PPPOEDISC:
		case ETHTYPE_PPPOE:
#endif /* PPPOE_SUPPORT */
		case ETHTYPE_IP:
		/* full packet send to tcpip_thread to process */
#ifdef SUPPORT_ETHERNET_ACCESSPOINT
			LWIP_DEBUGF(NETIF_DEBUG, ("[%s] calling eth_netif.linkoutput...\n", __func__));
			eth_netif.linkoutput(&eth_netif, p);
			pbuf_free(p);
			p = NULL;
#else
			if (netif->input(p, netif)!=ERR_OK) {
				LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
				pbuf_free(p);
				p = NULL;
			}
#endif
			break;

		default:
			pbuf_free(p);
			p = NULL;
			break;
	}
}
#include "standalone.h"

err_t wlif_init( struct netif *netif )
{
	struct wlif   *wlif;
	A("wlif_init\r\n");

	LWIP_ASSERT("netif != NULL", (netif != NULL));

	wlif = mem_malloc( sizeof(struct wlif) );

	if( wlif == NULL )
	{
		LWIP_DEBUGF( NETIF_DEBUG, ("wlif_init: out of memory\n") );
		return ERR_MEM;
	}

	#if LWIP_NETIF_HOSTNAME
	/* Initialize interface hostname */
	netif->hostname = "lwip";
	#endif /* LWIP_NETIF_HOSTNAME */

	/*
	* Initialize the snmp variables and counters inside the struct netif.
	* The last argument should be replaced with your link speed, in units
	* of bits per second.
	*/
	NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, 100);

	netif->name[0] = IFNAME0;
	netif->name[1] = IFNAME1;
	/* We directly use etharp_output() here to save a function call.
	* You can instead declare your own function an call etharp_output()
	* from it if you have to do some checks before sending (e.g. if link
	* is available...)
	*/
	#if LWIP_IPV4
	netif->output = etharp_output;
	#endif /* LWIP_IPV4 */
	#if LWIP_IPV6
	netif->output_ip6 = ethip6_output;
	#endif /* LWIP_IPV6 */
	netif->linkoutput = low_level_output;

	/* set MAC hardware address length to be used by lwIP */
	netif->hwaddr_len = 6;

	wlif->ethaddr = ( struct eth_addr * ) &( netif->hwaddr[0] );

	/* initialize the hardware */
	low_level_init(netif);

	return ERR_OK;
}