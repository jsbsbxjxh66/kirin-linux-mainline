/* mainline compat stub for the vendor Huawei hwnet WiFi delay statistics */
#ifndef _HWNET_IPV4_WIFI_DELAYST_H
#define _HWNET_IPV4_WIFI_DELAYST_H

#include <linux/types.h>
#include <linux/skbuff.h>

/*
 * The vendor stack records per-skb send/receive timestamps through
 * tracepoints hooked into net/ipv4 and net/core.  That instrumentation is
 * debug-only and is not ported, so the calls compile away to nothing.
 */
enum delay_record_point {
	TP_SKB_SEND		= 0,
	TP_SKB_IP		= 1,
	TP_SKB_HMAC_XMIT	= 2,
	TP_SKB_HMAC_TX		= 3,
	TP_SKB_DMAC		= 4,

	TP_SKB_RECV		= 0,
	TP_SKB_HMAC_UPLOAD	= 2,
	TP_SKB_HMAC_RX		= 3,

	TP_SKB_MAX_ENTRY	= 5
};

static inline bool hisi_wifi_delay_statistic_enabled(void)
{
	return false;
}

#define DELAY_STATISTIC_SWITCH_ON		hisi_wifi_delay_statistic_enabled()
#define IS_NEED_RECORD_DELAY(__skb, __index)	false

static inline void skbprobe_record_first(struct sk_buff *skb, u32 type)
{
	(void)skb;
	(void)type;
}

static inline void skbprobe_record_time(struct sk_buff *skb, int index)
{
	(void)skb;
	(void)index;
}

static inline void delay_record_snd_combine(struct sk_buff *skb)
{
	(void)skb;
}

#endif /* _HWNET_IPV4_WIFI_DELAYST_H */
