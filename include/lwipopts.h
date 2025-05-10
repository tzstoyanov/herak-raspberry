#ifndef _LWIPOPTS_COMMONH_H
#define _LWIPOPTS_COMMONH_H

#include <stdio.h>
#include <stdint.h>

#define NO_SYS						1
#define LWIP_SOCKET					0
#define LWIP_TIMERS 				1

#define MEMP_NUM_SYS_TIMEOUT	(LWIP_NUM_SYS_TIMEOUT_INTERNAL + 5)
#define MQTT_REQ_MAX_IN_FLIGHT	(5) /* maximum of subscribe requests */

#if PICO_CYW43_ARCH_POLL
#define MEM_LIBC_MALLOC				1
#else
// MEM_LIBC_MALLOC is incompatible with non polling versions
#define MEM_LIBC_MALLOC				0
#endif

#define MEM_ALIGNMENT				4
#define MEM_SIZE				8192
#define MEMP_NUM_TCP_SEG			32
#define MEMP_NUM_ARP_QUEUE			5
#define PBUF_POOL_SIZE				32
#define LWIP_TCPIP_CORE_LOCKING		1
#define LWIP_TCPIP_CORE_LOCKING_INPUT	1
#define SYS_LIGHTWEIGHT_PROT		1
#define LWIP_MPU_COMPATIBLE			0
#define LWIP_ARP					1
#define LWIP_ETHERNET				1
#define LWIP_ICMP					1
#define LWIP_RAW					1
#define TCP_WND						(8 * TCP_MSS)
#define TCP_MSS						1100
#define TCP_SND_BUF					(8 * TCP_MSS)
#define TCP_SND_QUEUELEN			((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK	1
#define LWIP_NETIF_LINK_CALLBACK	1
#define LWIP_NETIF_HOSTNAME			1
#define LWIP_NETCONN				0
#define MEM_STATS				1
#define SYS_STATS				1
#define MEMP_STATS				1
#define LINK_STATS				1
#define IP_STATS           			1
#define ETHARP_STATS       			1
#define TCP_STATS          			1
#define UDP_STATS          			1
//#define ETH_PAD_SIZE				2
#define LWIP_CHKSUM_ALGORITHM		3
#define LWIP_DHCP					1
#define LWIP_ICMP					1
#define LWIP_IPV4					1
#define LWIP_TCP					1
#define LWIP_UDP					1
#define LWIP_DNS					1
#define LWIP_CALLBACK_API			1
#define LWIP_ALTCP				1
#define LWIP_ALTCP_TLS				0
#define DNS_TABLE_SIZE				6
#define LWIP_TCP_KEEPALIVE			1
#define LWIP_NETIF_TX_SINGLE_PBUF	1
#define DHCP_DOES_ARP_CHECK			0
#define LWIP_DHCP_DOES_ACD_CHECK	0

#define LWIP_NOASSERT				0
//#ifndef NDEBUG
#define LWIP_DEBUG					1
#define LWIP_STATS					1
#define LWIP_STATS_DISPLAY			1
//#define DNS_DEBUG					LWIP_DBG_TYPES_ON
//#define MQTT_DEBUG				LWIP_DBG_TYPES_ON
//#endif

/*
void hlog_any(int severity, const char *topic, const char *fmt, ...);
#define hlog_sys(args...) hlog_any(-1, NULL, args)
#define LWIP_PLATFORM_DIAG(x)       do {hlog_sys x;} while(0)
*/

#define MQTT_OUTPUT_RINGBUF_SIZE	1024

#define ETHARP_DEBUG				LWIP_DBG_OFF
#define NETIF_DEBUG					LWIP_DBG_OFF
#define PBUF_DEBUG					LWIP_DBG_OFF
#define API_LIB_DEBUG				LWIP_DBG_OFF
#define API_MSG_DEBUG				LWIP_DBG_OFF
#define SOCKETS_DEBUG				LWIP_DBG_OFF
#define ICMP_DEBUG					LWIP_DBG_OFF
#define INET_DEBUG					LWIP_DBG_OFF
#define IP_DEBUG					LWIP_DBG_OFF
#define IP_REASS_DEBUG				LWIP_DBG_OFF
#define RAW_DEBUG					LWIP_DBG_OFF
#define MEM_DEBUG					LWIP_DBG_OFF
#define MEMP_DEBUG					LWIP_DBG_OFF
#define SYS_DEBUG					LWIP_DBG_OFF
#define TCP_DEBUG					LWIP_DBG_OFF
#define TCP_INPUT_DEBUG				LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG			LWIP_DBG_OFF
#define TCP_RTO_DEBUG				LWIP_DBG_OFF
#define TCP_CWND_DEBUG				LWIP_DBG_OFF
#define TCP_WND_DEBUG				LWIP_DBG_OFF
#define TCP_FR_DEBUG				LWIP_DBG_OFF
#define TCP_QLEN_DEBUG				LWIP_DBG_OFF
#define TCP_RST_DEBUG				LWIP_DBG_OFF
#define UDP_DEBUG					LWIP_DBG_OFF
#define TCPIP_DEBUG					LWIP_DBG_OFF
#define PPP_DEBUG					LWIP_DBG_OFF
#define SLIP_DEBUG					LWIP_DBG_OFF
#define DHCP_DEBUG					LWIP_DBG_OFF

#define LWIP_DHCP_MAX_NTP_SERVERS			2
#define SNTP_SERVER_DNS						1
#define SNTP_MONITOR_SERVER_REACHABILITY	1
#define SNTP_SET_SYSTEM_TIME		herak_set_system_time

#ifdef __cplusplus
extern "C" {
#endif
void herak_set_system_time(uint32_t sec);
#ifdef __cplusplus
}
#endif

#endif /* _LWIPOPTS_COMMONH_H */
