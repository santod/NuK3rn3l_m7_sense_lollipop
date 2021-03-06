/*
 * This is part of the Sequans SQN1130 driver.
 * Copyright 2008 SEQUANS Communications
 * Written by Dmitriy Chumak <chumakd@gmail.com>,
 *            Andy Shevchenko <andy@smile.org.ua>
 *
 * Inspired by if_sdio.c, Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "version.h"

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/device.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/pm.h>
#include <linux/jiffies.h>
#include <linux/hardirq.h>
#include <linux/mutex.h>
#include <linux/wakelock.h>
#include <linux/bug.h>
#include <linux/irq.h>

/* GPIO_WAKEUP */
#include <linux/gpio.h>
#include <linux/gpio_event.h>
#include <linux/interrupt.h>
#include <linux/mfd/pm8xxx/core.h>

#include "msg.h"
#include "debugfs.h"
#include "sdio-netdev.h"
#include "sdio-sqn.h"
#include "sdio.h"
#include "thp.h"
#include "sdio-driver.h"
#include "sdio-fw.h"
#include "sdio-pm.h"

#define DUMP_NET_PKT 1
int dump_net_pkt = 0;
int reset_count = 0;

#define RESET_BY_SDIO 0
#define RESET_BY_WIMAXTRACKER 0

/*For Error Code*/
#include <../../../../include/asm-generic/errno.h>
#include <../../../../include/asm-generic/errno-base.h>

#if RESET_BY_WIMAXTRACKER
#include "sdio_netlink.h"
#endif

static u8 __sqn_per_file_dbg = 0;
u8 *__sqn_sdio_per_file_dbg_addr(void)
{
	return &__sqn_per_file_dbg;
}

static const struct sdio_device_id sqn_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_SEQUANS, SDIO_DEVICE_ID_SEQUANS_SQN1130) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_SEQUANS, SDIO_DEVICE_ID_SEQUANS_SQN1210) },
	{ SDIO_DEVICE(SDIO_VENDOR_ID_SEQUANS, SDIO_DEVICE_ID_SEQUANS_SQN1220) },
	/* { SDIO_DEVICE(SDIO_ANY_ID, SDIO_ANY_ID) }, */
	{ 0 },
};
MODULE_DEVICE_TABLE(sdio, sqn_sdio_ids);

/*******************************************************************/
/* TX handlers                                                     */
/*******************************************************************/

static void sqn_sdio_add_skb_to_tx_queue(struct sqn_private *priv
	, struct sk_buff *skb, u8 tail)
{
	struct sqn_sdio_card *card = priv->card;

	sqn_pr_enter();

	if (tail)
		skb_queue_tail(&card->tx_queue, skb);
	else
		skb_queue_head(&card->tx_queue, skb);

	if (skb_queue_len(&card->tx_queue) > TX_QUEUE_MAX_LEN
		&& !netif_queue_stopped(priv->dev)) {
		sqn_pr_info("tx_queue len %d, disabling netif_queue\n"
				, skb_queue_len(&card->tx_queue));
		netif_stop_queue(priv->dev);
	}

	if (!card->waiting_pm_notification
	    && !wake_lock_active(&card->wakelock_tx)) {
		if (mmc_wimax_get_sdio_wakelock_log()) {
			printk(KERN_INFO "[WIMAX] lock wl_tx,");
			PRINTRTC;
		}
		wake_lock(&card->wakelock_tx); /* TX */

		if (wake_lock_active(&card->wakelock_host)) {
			if (mmc_wimax_get_sdio_wakelock_log()) {
				printk(KERN_INFO "[WIMAX] release wl_host,");
				PRINTRTC;
			}
			wake_unlock(&card->wakelock_host);
		}
	}
	sqn_pr_leave();
}


static int sqn_sdio_is_tx_queue_empty(struct sqn_private *priv)
{
	int rv = 0;
	struct sqn_sdio_card *card = priv->card;

	sqn_pr_enter();

	rv = skb_queue_empty(&card->tx_queue);

	sqn_pr_leave();

	return rv;
}


static int sqn_sdio_get_rstn_wr_fifo_flag(struct sqn_private *priv)
{
	struct sqn_sdio_card *card = priv->card;

	sqn_pr_enter();

	if (0 == card->rstn_wr_fifo_flag) {
		int rv = 0;
		sdio_claim_host(card->func);
		card->rstn_wr_fifo_flag = sdio_readb(card->func,
			SQN_SDIO_RSTN_WR_FIFO(2), &rv);
		sdio_release_host(card->func);
		sqn_pr_dbg("RSTN_WR_FIFO2 = %d\n", card->rstn_wr_fifo_flag);
		if (rv) {
			sqn_pr_err("sdio_readb(RSTN_WR_FIFO2) - return error\n");
			card->rstn_wr_fifo_flag = 0;
			goto out;
		}
	}

	sqn_pr_leave();
out:
	return card->rstn_wr_fifo_flag;
}


static int sqn_sdio_recover_after_cmd53_timeout(struct sqn_sdio_card *card)
{
	int rv = 0;

	sqn_pr_enter();

	sqn_pr_info("Try to recovery after SDIO timeout error\n");
	sdio_claim_host(card->func);
	sdio_writeb(card->func, 1 << card->func->num, SDIO_CCCR_IO_ABORT, &rv);
	sdio_release_host(card->func);
	if (rv) {
		sqn_pr_err("sdio_writeb(SDIO_CCCR_IO_ABORT) - return error %d\n"
			, rv);
	}

	sqn_pr_leave();

	return rv;
}


/**
*	sqn_sdio_cmd52_read_buf - read @size bytes into @buf buffer from
*				  address @addr using CMD52
*	@card:	sqn sdio card structure
*	@buf:	buffer to return value, should be and address of u16, u32 variable
*	@size:	size of the @buf / count of bytes to read from @addr
*
*	@return error status - 0 if success, !0 otherwise
*/
static int sqn_sdio_cmd52_read_buf(struct sqn_sdio_card *card, void *buf, int size, int addr)
{
	u8 tmpbuf[4] = { 0xa7, 0xa7, 0xa7, 0xa7 };
	int i = 0;
	int rv = 0;

	sqn_pr_enter();
	sqn_pr_info("Trying to read %d bytes from 0x%x address using CMD52\n", size, addr);

	sdio_claim_host(card->func);
	for (i = 0; i < size; i++) {
		tmpbuf[i] = sdio_readb(card->func, addr + i, &rv);
		if (rv) {
			sqn_pr_err("sdio_readb(%x) - return error %d\n", addr + i, rv);
			break;
		}
	}
	sdio_release_host(card->func);

	switch (size) {
	case sizeof(u16):
		*((u16 *)buf) = le16_to_cpup((__le16 *)tmpbuf);
		break;
	case sizeof(u32):
		*((u32 *)buf) = le32_to_cpup((__le32 *)tmpbuf);
		break;
	default:
		sqn_pr_err("unsupported buffer size: %d\n", size);
	}

	sqn_pr_leave();

	return rv;
}


static int sqn_sdio_dump_registers(struct sqn_sdio_card *card)
{
	u8  b8 = 0;
	u16 b16 = 0;
	int rv = 0;

	sqn_pr_enter();
	sqn_pr_info("------------------ REG DUMP BEGIN ------------------\n");

	sdio_claim_host(card->func);

	b8 = sdio_readb(card->func, SQN_SDIO_IT_STATUS_LSBS, &rv);
	if (rv)
		sqn_pr_err("can't read SDIO_IT_STATUS_LSBS: %d\n", rv);
	else
		sqn_pr_info("SDIO_IT_STATUS_LSBS: 0x%x\n", b8);

	b8 = sdio_readb(card->func, SQN_SDIO_IT_STATUS_MSBS, &rv);
	if (rv)
		sqn_pr_err("can't read SDIO_IT_STATUS_MSBS: %d\n", rv);
	else
		sqn_pr_info("SDIO_IT_STATUS_MSBS: 0x%x\n", b8);

	b8 = sdio_readb(card->func, SQN_SDIO_RSTN_WR_FIFO(2), &rv);
	if (rv)
		sqn_pr_err("can't read SQN_SDIO_RSTN_WR_FIFO2: %d\n", rv);
	else
		sqn_pr_info("SQN_SDIO_RSTN_WR_FIFO: 0x%x\n", b8);

	b8 = sdio_readb(card->func, SQN_SOC_SIGS_LSBS, &rv);
	if (rv)
		sqn_pr_err("can't read SQN_SOC_SIGS_LSBS: %d\n", rv);
	else
		sqn_pr_info("SQN_SOC_SIGS_LSBS: 0x%x\n", b8);

	b8 = sdio_readb(card->func, SQN_HTS_SIGS, &rv);
	if (rv)
		sqn_pr_err("can't read SQN_HTS_SIGS: %d\n", rv);
	else
		sqn_pr_info("SQN_HTS_SIGS: 0x%x\n", b8);

	sdio_release_host(card->func);

	rv = sqn_sdio_cmd52_read_buf(card, &b16,  sizeof(b16), SQN_SDIO_WR_FIFO_BYTESLEFT(2));
	if (rv)
		sqn_pr_err("can't read SDIO_WR_FIFO_BYTESLEFT2: %d\n", rv);
	else
		sqn_pr_info("SDIO_WR_FIFO_BYTESLEFT2: 0x%x\n", b16);

	rv = sqn_sdio_cmd52_read_buf(card, &b16,  sizeof(b16), SQN_SDIO_WR_FIFO_LEVEL(2));
	if (rv)
		sqn_pr_err("can't read SQN_SDIO_WR_FIFO_LEVEL2: %d\n", rv);
	else
		sqn_pr_info("SQN_SDIO_WR_FIFO_LEVEL2: 0x%x\n", b16);

	rv = sqn_sdio_cmd52_read_buf(card, &b16,  sizeof(b16), SQN_SDIO_RD_FIFO_LEVEL(2));
	if (rv)
		sqn_pr_err("can't read SQN_SDIO_RD_FIFO_LEVEL2: %d\n", rv);
	else
		sqn_pr_info("SQN_SDIO_RD_FIFO_LEVEL2: 0x%x\n", b16);

	rv = sqn_sdio_cmd52_read_buf(card, &b16,  sizeof(b16), SDIO_CMN_CISTPLMID_MANF);
	if (rv)
		sqn_pr_err("can't read SDIO_CMN_CISTPLMID_MANF: %d\n", rv);
	else
		sqn_pr_info("SDIO_CMN_CISTPLMID_MANF: 0x%x\n", b16);

	rv = sqn_sdio_cmd52_read_buf(card, &b16,  sizeof(b16), SDIO_CMN_CISTPLMID_CARD);
	if (rv)
		sqn_pr_err("can't read SDIO_CMN_CISTPLMID_CARD: %d\n", rv);
	else
		sqn_pr_info("SDIO_CMN_CISTPLMID_CARD: 0x%x\n", b16);

	switch (rv) {
	case -ENOENT:
		sqn_pr_info("error due to ENOENT: No such file or directory\n");	break;
	case -EIO:
		sqn_pr_info("error due to EIO: I/O error\n");	break;
	case -ENOMEM:
		sqn_pr_info("error due to ENOMEM: Out of memory\n");	break;
	case -EINVAL:
		sqn_pr_info("error due to EINVAL: Invalid argument\n");	break;
	case -ERANGE:
		sqn_pr_info("error due to ERANGE: Math result not representable\n");	break;
	case -ENOSYS:
		sqn_pr_info("error due to ENOSYS: Function not implemented\n");	break;
	case -EILSEQ:
		sqn_pr_info("error due to EILSEQ: Illegal byte sequence\n");	break;
	case -ETIMEDOUT:
		sqn_pr_info("error due to ETIMEDOUT: Connection timed out\n");	break;
	case -ENOMEDIUM:
		sqn_pr_info("error due to ENOMEDIUM: No medium found\n");	break;
	default:
		sqn_pr_info("Unknown error code\n");
	}

	sqn_pr_info("------------------ REG DUMP END ------------------\n");
	sqn_pr_leave();

	return rv;
}


static int sqn_sdio_get_wr_fifo_level(struct sqn_private *priv)
{
	int level = 0;
	int rv = 0;
	struct sqn_sdio_card *card = priv->card;

	sqn_pr_enter();

	sdio_claim_host(card->func);
	/* level = sdio_readw(card->func, SQN_SDIO_WR_FIFO_LEVEL(2), &rv); */
	level = sdio_readl(card->func, 0x2050, &rv);
	level = (u32)level >> sizeof(u16);
	sdio_release_host(card->func);
	sqn_pr_dbg("SQN_SDIO_WR_FIFO_LEVEL2 = %d\n", level);
	if (rv) {
		sqn_pr_err("sdio_readw(WR_FIFO_LEVEL2) error %d\n", rv);
		level = -1;
		if (-ETIMEDOUT == rv)
			sqn_pr_info("SDIO CMD53 timeout error in %s\n", __func__);
			/* sqn_sdio_recover_after_cmd53_timeout(card); */
			/* sqn_sdio_dump_registers(card); */
		goto out;
	}

	sqn_pr_leave();
out:
	return level;
}

#if DUMP_NET_PKT
uint8_t is_thp_packet(uint8_t  *dest_addr);
int is_lsp_packet(const struct sk_buff *skb);
#endif

struct sk_buff *sqn_sdio_prepare_skb_for_tx(struct sk_buff *skb)
{
#define PDU_LEN_SIZE	2
#define CRC_SIZE	4
#define PAD_TO_VALUE	512

#if DUMP_NET_PKT
	struct ethhdr *eth = (struct ethhdr *)skb->data;
#endif

	/*
	 * Calculate padding, to workaround some SDIO controllers we need to pad
	 * each TX buffer so it size will be a multiple of PAD_TO_VALUE
	 */
	u32 padding = (skb->len + PDU_LEN_SIZE + CRC_SIZE) % PAD_TO_VALUE ?
		PAD_TO_VALUE - (skb->len + PDU_LEN_SIZE + CRC_SIZE) % PAD_TO_VALUE : 0;

	sqn_pr_enter();

	sqn_pr_dbg("length %d, padding %d\n", skb->len, padding);

	if (skb->len > (SQN_MAX_PDU_LEN - (PDU_LEN_SIZE + CRC_SIZE + padding))) {
		sqn_pr_info("%s: skb length %d error, free skb\n", __func__, skb->len);
		dev_kfree_skb_any(skb);
		return 0;
	}

#if DUMP_NET_PKT
	if (mmc_wimax_get_netlog_status() || mmc_wimax_get_HostWakeupFWEvent()) {
		/* printk(KERN_INFO "\n"); */
		if (mmc_wimax_get_HostWakeupFWEvent()) {
			sqn_pr_info("HostWakeupFWEvent TX:\n");
			mmc_wimax_set_HostWakeupFWEvent(0);
		}

		sqn_pr_info("TX PDU length %d\n", skb->len);
		if (sqn_is_tx_thp_packet(eth->h_source)) {
			sqn_pr_thp_info_dump("TX PDU", skb->data, skb->len);
		} else if (sqn_is_tx_lsp_packet(skb)) {
			sqn_pr_info("TX LSP packet\n");
		} else {
			/* if (!sqn_lsp_is_tx_lsp_packet(eth->h_source) && !sqn_lsp_is_tx_lsp_packet(skb)) { */
			sqn_pr_info_dump("TX PDU", skb->data, skb->len);
			if (mmc_wimax_get_sdio_wakeup_lite_dump())
				sqn_pr_info_dump_rawdata_lite("TX PDU",  skb->data, skb->len);
		}
		/* printk(KERN_INFO "\n"); */
	}

	if (mmc_wimax_get_netlog_withraw_status()) {
		/* if (!sqn_lsp_is_tx_lsp_packet(eth->h_source) && !sqn_lsp_is_tx_lsp_packet(skb)) */
		{
			sqn_pr_info("[RAW]-------------------------------------------------------------------\n");
			sqn_pr_info("TX PDU length %d\n", skb->len);
			sqn_pr_info_dump_rawdata("TX PDU",  skb->data, skb->len);
		}
	}
#endif

	if (mmc_wimax_get_packet_filter()) {
		if (sqn_filter_packet_check("TX PDU", skb->data, skb->len)) {
			/* sqn_pr_info("Drop TX packets len:%d\n", skb->len); */
			dev_kfree_skb_any(skb);
			return 0;
		}
	}

	/*
	 * Real size of the PDU is data_len + 2 bytes at begining of PDU
	 * for pdu_size + 4 bytes at the end of PDU for CRC of data
	 */
	if (skb_headroom(skb) < PDU_LEN_SIZE || skb_tailroom(skb) < CRC_SIZE + padding) {
		struct sk_buff *origin_skb = skb;
		gfp_t gfp_mask = GFP_DMA;
		if (in_interrupt() || irqs_disabled())
			gfp_mask |= GFP_ATOMIC;
		else
			gfp_mask |= GFP_KERNEL;
		sqn_pr_dbg("relocating TX skb, GFP mask %x\n", gfp_mask);
		skb = skb_copy_expand(skb, PDU_LEN_SIZE, CRC_SIZE + padding
				, gfp_mask);
		dev_kfree_skb_any(origin_skb);
		if (0 == skb) {
			/* An error occured, likely there is no memory to
			 * expand skb, so we drop it.
			 */
			return 0;
		}
	} else {
		sqn_pr_dbg("TX skb: headroom = %d tailroom = %d\n"
			, skb_headroom(skb), skb_tailroom(skb));
	}

	/*
	 * Add size of PDU before ethernet frame
	 * It should be in little endian byte order
	 */
	*((u8 *)skb->data - 2) = (skb->len + CRC_SIZE) & 0xff;
	*((u8 *)skb->data - 1) = ((skb->len + CRC_SIZE) >> 8) & 0xff;
	skb_push(skb, PDU_LEN_SIZE);

	/*
	 * Add CRC to the end of ethernet frame
	 * Now it simply set to 0
	 */
	memset(skb->tail, 0, CRC_SIZE);
	skb_put(skb, CRC_SIZE + padding);

	sqn_pr_leave();

	return skb;
}


int sqn_sdio_tx_skb(struct sqn_sdio_card *card, struct sk_buff *skb
	, u8 claim_host)
{
	int rv = 0;

	sqn_pr_enter();

	if (claim_host)
		sdio_claim_host(card->func);

	rv = sdio_writesb(card->func, SQN_SDIO_RDWR_FIFO(2), skb->data,
			skb->len);
	if (rv) {
		sqn_pr_err("call to sdio_writesb(RDWR_FIFO2) - return error %d\n", rv);
		if (-ETIMEDOUT == rv) {
			if (claim_host) {
				sdio_release_host(card->func);
				claim_host = 0;
			}
			sqn_pr_info("SDIO CMD53 timeout error: TX PDU length %d, PDU[0] 0x%x, PDU[1] 0x%x\n"
				, skb->len, *((u8 *)skb->data), *((u8 *)skb->data + 1));
			/* sqn_sdio_dump_registers(card); */
			/* sqn_sdio_recover_after_cmd53_timeout(card); */
		}
		goto release;
	}
release:
	if (claim_host)
		sdio_release_host(card->func);
	dev_kfree_skb_any(skb);

	sqn_pr_leave();
	return rv;
}


static void sqn_sdio_wake_lock_release_host_timer_fn(unsigned long data)
{
	struct sqn_sdio_card *card = (struct sqn_sdio_card *) data;

	sqn_pr_enter();

	/* No care if the TX and RX queues are empty, we can releas a wake_lock_host */
	if (mmc_wimax_get_sdio_wakelock_log()) {
		printk(KERN_INFO "[WIMAX] release wl_host2,");
		PRINTRTC;
	}

	if (wake_lock_active(&card->wakelock_host))
		wake_unlock(&card->wakelock_host); /* Release time_out */

	sqn_pr_leave();
}

static void sqn_sdio_wake_lock_release_tx_timer_fn(unsigned long data)
{
	struct sqn_sdio_card *card = (struct sqn_sdio_card *) data;

	sqn_pr_enter();

	/* if TX queues are empty, we can releas a wake_lock */
	if (skb_queue_empty(&card->tx_queue)) {
		if (mmc_wimax_get_sdio_wakelock_log()) {
			printk(KERN_INFO "[WIMAX] release wl_tx,");
			PRINTRTC;
		}
		if (wake_lock_active(&card->wakelock_host))
			wake_unlock(&card->wakelock_host); /* Release time_out */
		if (wake_lock_active(&card->wakelock_tx))
			wake_unlock(&card->wakelock_tx);
	}

	sqn_pr_leave();
}

static void sqn_sdio_wake_lock_release_rx_timer_fn(unsigned long data)
{
	struct sqn_sdio_card *card = (struct sqn_sdio_card *) data;

	sqn_pr_enter();

	/* if RX queues are empty, we can releas a wake_lock */
	if (skb_queue_empty(&card->rx_queue)) {
		if (mmc_wimax_get_sdio_wakelock_log()) {
			printk(KERN_INFO "[WIMAX] release wl_rx,");
			PRINTRTC;
		}
		if (wake_lock_active(&card->wakelock_host))
			wake_unlock(&card->wakelock_host); /* Release time_out */
		if (wake_lock_active(&card->wakelock_rx))
			wake_unlock(&card->wakelock_rx);
	}

	sqn_pr_leave();
}

#define SQN_WAKE_LOCK_RELEASE_DELAY_SECONDS	1

static void sqn_sdio_release_wake_lock_host(struct sqn_sdio_card *card)
{
	u32 delay = 0;

	sqn_pr_enter();

	/* if TX and RX queues are empty, we will wait some time before
	 * doing actual wake_lock release */
	if (wake_lock_active(&card->wakelock_host)) {
		sqn_pr_dbg("shedule wake_lock_host release in %d sec\n"
			, SQN_WAKE_LOCK_RELEASE_DELAY_SECONDS);

		delay = jiffies + msecs_to_jiffies(
			SQN_WAKE_LOCK_RELEASE_DELAY_SECONDS * MSEC_PER_SEC);

		mod_timer(&card->wakelock_timer_host, delay);
	}
	sqn_pr_leave();
}

static void sqn_sdio_release_wake_lock_tx(struct sqn_sdio_card *card)
{
	u32 delay = 0;

	sqn_pr_enter();

	/* if TX and RX queues are empty, we will wait some time before
	 * doing actual wake_lock release */
	if ((wake_lock_active(&card->wakelock_tx) || wake_lock_active(&card->wakelock_host))
		&& skb_queue_empty(&card->tx_queue)) {
		sqn_pr_dbg("shedule wake_lock_tx release in %d sec\n"
			, SQN_WAKE_LOCK_RELEASE_DELAY_SECONDS);

		delay = jiffies + msecs_to_jiffies(
			SQN_WAKE_LOCK_RELEASE_DELAY_SECONDS * MSEC_PER_SEC);

		mod_timer(&card->wakelock_timer_tx, delay);
	}

	sqn_pr_leave();
}

static void sqn_sdio_release_wake_lock_rx(struct sqn_sdio_card *card)
{
	u32 delay = 0;

	sqn_pr_enter();

	/* if TX and RX queues are empty, we will wait some time before
	 * doing actual wake_lock release */
	if ((wake_lock_active(&card->wakelock_rx) || wake_lock_active(&card->wakelock_host))
		&& skb_queue_empty(&card->rx_queue)) {
		sqn_pr_dbg("shedule wake_lock_rx release in %d sec\n"
			, SQN_WAKE_LOCK_RELEASE_DELAY_SECONDS);

		delay = jiffies + msecs_to_jiffies(
			SQN_WAKE_LOCK_RELEASE_DELAY_SECONDS * MSEC_PER_SEC);

		mod_timer(&card->wakelock_timer_rx, delay);
	}

	sqn_pr_leave();
}


static int sqn_sdio_host_to_card(struct sqn_private *priv)
{
	struct sqn_sdio_card *card = priv->card;
	struct sk_buff *skb = 0;
	unsigned long irq_flags = 0;
	int level = 0;
	int rv = 0;
	u8 need_to_ulock_mutex = 0;

	sqn_pr_enter();

	if (priv->removed) {
		/* sqn_pr_warn("%s: card/driver is removed, do nothing\n", __func__); */
		goto drv_removed;
	}

	spin_lock_irqsave(&priv->drv_lock, irq_flags);
	if (card->is_card_sleeps) {
		spin_unlock_irqrestore(&priv->drv_lock, irq_flags);
		/*
		 * Ignore return value of sqn_wakeup_fw() and try
		 * to send PDU even if wake up failed
		 */
		sqn_wakeup_fw(card->func);
	} else
		spin_unlock_irqrestore(&priv->drv_lock, irq_flags);

	if (0 == sqn_sdio_get_rstn_wr_fifo_flag(priv)) {
		rv = -1;
		goto dequeue_skb;
	}

	sqn_pr_dbg("acquire TX mutex\n");
	if (!mutex_trylock(&card->tx_mutex)) {
		sqn_pr_dbg("failed to acquire TX mutex, it means we are going"
			" to remove a network interface\n");
		need_to_ulock_mutex = 0;
		goto out;
	}
	need_to_ulock_mutex = 1;

	while (!priv->removed && !sqn_sdio_is_tx_queue_empty(priv)) {
		skb = skb_dequeue(&card->tx_queue);
		skb = sqn_sdio_prepare_skb_for_tx(skb);
		if (0 != skb) {
			if (0 == level) {
				int count = 20;
				while (0 == (level = sqn_sdio_get_wr_fifo_level(priv))) {
					if (0 == count--) {
						sqn_pr_err("WR_FIFO_LEVEL2 timeout\n");
						rv = -1;
						goto free_skb;
					}
					msleep(1);
				}
				if (level < 0) {
					rv = -1;
					goto free_skb;
				}
			}

			sqn_sdio_tx_skb(card, skb, 1);
			--level;

			if (!card->waiting_pm_notification
			    && netif_queue_stopped(priv->dev)
			    && skb_queue_len(&card->tx_queue) < TX_QUEUE_WM_LEN) {
				sqn_pr_info("tx_queue len %d, enabling netif_queue\n"
					, skb_queue_len(&card->tx_queue));
				netif_wake_queue(priv->dev);
			} else {
				sqn_pr_dbg("tx_queue len %d\n"
					, skb_queue_len(&card->tx_queue));
			}
		} else {
			priv->stats.tx_dropped++;
			priv->stats.tx_errors++;
		}
	}
out:
	if (need_to_ulock_mutex && mutex_is_locked(&card->tx_mutex)) {
		mutex_unlock(&card->tx_mutex);
		sqn_pr_dbg("release TX mutex\n");
	}

	sqn_sdio_release_wake_lock_tx(card);

	if (rv == 0)
		reset_count = 0;

	if ((0 != rv) || (mmc_wimax_get_CMD53_timeout_trigger_counter())) {
		/*
		 * Failed to send PDU - assume that card was removed or
		 * crashed/reset so initiate card detection.
		 */

		if (mmc_wimax_get_CMD53_timeout_trigger_counter()) {
			sqn_pr_info("Force CMD53 timeout to reset SDIO!\n");
			mmc_wimax_set_CMD53_timeout_trigger_counter(mmc_wimax_get_CMD53_timeout_trigger_counter()-1);
		}

		reset_count++;
		/* Reset WiMAX chip	*/
		if (mmc_wimax_get_sdio_hw_reset() && (reset_count > 5)) { /* mmc_wimax_get_sdio_hw_reset */

			sqn_pr_info("reset WiMAX chip by SDIO in time#%d\n", reset_count);
			reset_count = 0;

			/* HW Reset */
			mmc_wimax_power(0);
			msleep(5);
			mmc_wimax_power(1);
			/* To avoid re-initialized SDIO card failed */
			priv->removed = 1;

			sqn_pr_err("card seems to be dead/removed - initiate reinitialization\n");
			mmc_detect_change(card->func->card->host, 1);
		} else {
#if RESET_BY_WIMAXTRACKER
			sqn_pr_info("reset WiMAX chip by WimaxTracker\n");
			udp_broadcast(1, "ResetWimax_BySDIO\n");
#else
			sqn_pr_info("No reset WiMAX chip in time#%d\n", reset_count);
#endif
		} /* mmc_wimax_get_sdio_hw_reset ] */
	}
drv_removed:
	sqn_pr_leave();
	return rv;

dequeue_skb:
	if (!sqn_sdio_is_tx_queue_empty(priv)) {
		sqn_pr_dbg("remove skb from TX queue because of error\n");
		skb = skb_dequeue(&card->tx_queue);
	}
free_skb:
	sqn_pr_dbg("free TX skb because of error\n");
	dev_kfree_skb_any(skb);
	priv->stats.tx_dropped++;
	priv->stats.tx_errors++;
	goto out;
}

/*******************************************************************/
/* RX handlers                                                     */
/*******************************************************************/
static void sqn_sdio_process_rx_queue(struct work_struct *work)
{
	struct sqn_private *priv = container_of(work, struct sqn_private
		, rx_work_struct);
	struct sqn_sdio_card *card = (struct sqn_sdio_card *) priv->card;
	struct sk_buff *skb = 0;
	u8 need_to_ulock_mutex = 0;

	sqn_pr_enter();

	sqn_pr_dbg("acquire RXQ mutex\n");
	if (!mutex_trylock(&card->rxq_mutex)) {
		sqn_pr_dbg("failed to acquire RXQ mutex, it means we are going"
			" to remove a network interface\n");
		need_to_ulock_mutex = 0;
		goto out;
	}
	need_to_ulock_mutex = 1;

	while (!priv->removed && 0 != (skb = skb_dequeue(&card->rx_queue))) {
		sqn_rx_process(card->priv->dev, skb);
		if (waitqueue_active(&priv->rx_waitq)
			&& skb_queue_len(&card->rx_queue) < RX_QUEUE_WM_LEN) {
			sqn_pr_info("rx_queue len %d, enabling rx\n"
				, skb_queue_len(&card->rx_queue));
			wake_up_interruptible(&priv->rx_waitq);
		}
	}
out:
	if (need_to_ulock_mutex && mutex_is_locked(&card->rxq_mutex)) {
		mutex_unlock(&card->rxq_mutex);
		sqn_pr_dbg("release RXQ mutex\n");
	}

	sqn_sdio_release_wake_lock_rx(card);
	sqn_pr_leave();
}


static int sqn_sdio_card_to_host(struct sqn_sdio_card *card)
{
	struct timeval tv_start = { 0 }, tv_end = { 0 };

	u16 level = 0;
	int rv = 0;
	u8 need_to_ulock_mutex = 0;

	sqn_pr_enter();

	if (card->priv->removed) {
		/* sqn_pr_warn("%s: card/driver is removed, do nothing\n", __func__); */
		goto drv_removed;
	}

	sqn_pr_dbg("acquire RX mutex\n");
	if (!mutex_trylock(&card->rx_mutex)) {
		sqn_pr_dbg("failed to acquire RX mutex, it means we are going"
			" to remove a network interface\n");
		need_to_ulock_mutex = 0;
		goto out;
	}
	need_to_ulock_mutex = 1;

	/*
	 * NOTE: call to sdio_claim_host() is already done
	 * 	 in sqn_sdio_it_lsb() - our caller
	 */
check_level:
	/* Find out how many PDUs we have to read */
	level = sdio_readw(card->func, SQN_SDIO_RD_FIFO_LEVEL(2), &rv);
	if (rv) {
		sqn_pr_err("ERROR reading SDIO_RD_FIFO_LEVEL\n");
		goto out;
	}

	if (level == 0) {
		sqn_pr_dbg("no more PDUs to read\n");
		if (rv < 0)
			sqn_pr_warn("%s: no more PDUs left but status = %d\n", __func__, rv);
		goto out;
	}

	sqn_pr_dbg("PDUs to read %d\n", level);

	while (!card->priv->removed && level--) {
		struct sk_buff *skb = 0;
#if DUMP_NET_PKT
		struct ethhdr *eth = 0;
#endif
		u16 size = 0;

		/* Get the size of PDU */
		size = sdio_readw(card->func, SQN_SDIO_RDLEN_FIFO(2), &rv);
		if (rv) {
			sqn_pr_err("can't get FIFO read length, status = %d\n", rv);
			goto out;
		}
		sqn_pr_dbg("PDU #%u length %u\n", (u32)level, (u32)size);

		if (size > SQN_SDIO_PDU_MAXLEN || size < 1) {
			sqn_pr_err("RX PDU length %u is not correct\n",
				(u32)size);
			card->priv->stats.rx_length_errors++;
			card->priv->stats.rx_errors++;
			continue;
		}

		skb = __netdev_alloc_skb(card->priv->dev, SQN_SDIO_PDU_MAXLEN
				, GFP_ATOMIC | GFP_DMA);
		if (0 == skb) {
			sqn_pr_err("failed to alloc RX buffer\n");
			rv = -ENOMEM;
			goto out;
		}

		do_gettimeofday(&tv_start);

		rv = sdio_readsb(card->func, skb->data, SQN_SDIO_RDWR_FIFO(2),
				 (int)size);
		if (rv) {
			sqn_pr_err("RX PDU read failed: %d\n", rv);
			continue;
		}
		skb_put(skb, size);

		do_gettimeofday(&tv_end);
		sqn_dfs_pstat.rx[skb->len].count++;
		sqn_dfs_pstat.rx[skb->len].total_time += tv_end.tv_usec - tv_start.tv_usec
			+ (tv_end.tv_sec - tv_start.tv_sec) * USEC_PER_SEC;

#if DUMP_NET_PKT

		if (mmc_wimax_get_netlog_status() || mmc_wimax_get_FWWakeupHostEvent()) {
			/* printk(KERN_INFO "\n"); */
			if (mmc_wimax_get_FWWakeupHostEvent()) {
				sqn_pr_info("FWWakeupHostEvent RX:\n");
				mmc_wimax_set_FWWakeupHostEvent(0);
			}
			eth = (struct ethhdr *)skb->data;

			sqn_pr_info("RX PDU length %d\n", skb->len);
			if (sqn_is_rx_thp_packet(eth->h_dest)) {
				sqn_pr_thp_info_dump("RX PDU", skb->data, skb->len);
			} else if (sqn_is_rx_lsp_packet(skb)) {
				sqn_pr_info("RX LSP packet\n");
			} else {
				sqn_pr_info_dump("RX PDU", skb->data, skb->len);
				if (mmc_wimax_get_sdio_wakeup_lite_dump())
					sqn_pr_info_dump_rawdata_lite("RX PDU",  skb->data, skb->len);
			}
			/* printk(KERN_INFO "\n"); */
		}

		if (mmc_wimax_get_netlog_withraw_status()) {
			eth = (struct ethhdr *)skb->data;
			/* if (!sqn_lsp_is_rx_lsp_packet(eth->h_dest) && !is_lsp_packet(skb)) */
			{
				sqn_pr_info("[RAW]-------------------------------------------------------------------\n");
				sqn_pr_info("RX PDU length %d\n", skb->len);
				sqn_pr_info_dump_rawdata("RX PDU",  skb->data, skb->len);
			}
		}
#endif

		if (sqn_handle_lsp_packet(card->priv, skb))
			continue;
		/*
		 * If we have some not LSP PDUs to read, then card is not
		 * asleep any more, so we should notify waiters about this
		 */
		if (card->is_card_sleeps) {
			sqn_pr_info("got RX data, card is not asleep\n");
			/* signal_card_sleep_completion(card->priv); */
			card->is_card_sleeps = 0;
		}

		if (!card->waiting_pm_notification
		    && !wake_lock_active(&card->wakelock_rx)) {
			if (mmc_wimax_get_sdio_wakelock_log()) {
				printk(KERN_INFO "[WIMAX] lock wl_rx,");
				PRINTRTC;
			}
			wake_lock(&card->wakelock_rx); /* RX */

			if (wake_lock_active(&card->wakelock_host)) {
				if (mmc_wimax_get_sdio_wakelock_log()) {
					printk(KERN_INFO "[WIMAX] release wl_host,");
					PRINTRTC;
				}
				wake_unlock(&card->wakelock_host);
			}
		}

		/*
		 * Don't use internal RX queue, because kernel has its own.
		 * Just push RX packet directly to kernel
		 */
		sqn_rx_process(card->priv->dev, skb);

	}

	sqn_pr_dbg("check is there more PDU to read\n");
	goto check_level;
out:
	sqn_sdio_release_wake_lock_rx(card);
	if (need_to_ulock_mutex && mutex_is_locked(&card->rx_mutex)) {
		mutex_unlock(&card->rx_mutex);
		sqn_pr_dbg("release RX mutex\n");
	}
drv_removed:
	sqn_pr_leave();
	return rv;
}

/*******************************************************************/
/* Interrupt handling                                              */
/*******************************************************************/

int sqn_sdio_it_lsb(struct sdio_func *func)
{
	struct sqn_sdio_card *card = sdio_get_drvdata(func);
	int rc = 0;
	u8 status = 0;
	unsigned long irq_flags = 0;
	u8 is_card_sleeps = 0;
	int retry = 0;

	static int ReadDataFailed_ctr = 0;
	int DisableIT_retry = 0;

	struct mmc_host *host = func->card->host;
	sqn_pr_enter();

	/* NOTE: call of sdio_claim_host() is already done */

retry_LSB:
	/* Read the interrupt status */
	status = sdio_readb(func, SQN_SDIO_IT_STATUS_LSBS, &rc);

	if (!rc) {
		/* sqn_pr_info("%s: read interrupt(LSB) successful at #%d!\n", __func__, retry); */
		;
	} else {
		sqn_pr_info("%s: read interrupt(LSB) failed at #%d!\n", __func__, retry);
	}

	if (rc && retry < 5) {
		retry++;
		goto retry_LSB;
	}

	sqn_pr_dbg("interrupt(LSB): 0x%02X\n", (unsigned char) status);

	spin_lock_irqsave(&card->priv->drv_lock, irq_flags);
	is_card_sleeps = card->is_card_sleeps;
	spin_unlock_irqrestore(&card->priv->drv_lock, irq_flags);

	/* Handle interrupt */
	if (status & SQN_SDIO_IT_WR_FIFO2_WM) {
		sqn_pr_dbg("skipping FIFO2 write watermark interrupt...\n");

		retry = 0;

retry_ClearLSB_IN_WR_FIFO2:
		/* Clear interrupt flag */
		sdio_writeb(func, SQN_SDIO_IT_WR_FIFO2_WM,
				SQN_SDIO_IT_STATUS_LSBS, &rc);

		if (!rc) {
			/* sqn_pr_info("%s: clear interrupt(LSB) successful at #%d!\n", __func__, retry); */
			;
		} else {
			sqn_pr_info("%s: clear interrupt(LSB) failed at #%d!\n", __func__, retry);
		}

		if (rc && retry < 5) {
			retry++;
			goto retry_ClearLSB_IN_WR_FIFO2;
		}
	}

	if (status & SQN_SDIO_IT_RD_FIFO2_WM) {
		rc = sqn_sdio_card_to_host(card);

		if (rc || mmc_wimax_get_RD_FIFO_LEVEL_ERROR()) {
			sqn_pr_err("can't read data from card, error %d\n", rc);

			/* Disable RX interrupt for avoiding keep requesting CPU resource */
			if (ReadDataFailed_ctr == 20) {

				sqn_pr_info("Read data from card Failed successively 20 time! Disabled RX interrupt now!\n");

				DisableIT_retry = 0;

retry_DisableIT_LSB:
				/* disable LSB */
				sdio_writeb(func, 0, SQN_SDIO_IT_EN_LSBS, &rc);

				if (!rc)
					sqn_pr_info("disabled interrupt(LSB) successful at #%d!\n", retry);
				else
					sqn_pr_info("disabled interrupt(LSB) failed at #%d!\n", retry);

				if (rc && DisableIT_retry < 5) {
					DisableIT_retry++;
					goto retry_DisableIT_LSB;
				}

				DisableIT_retry = 0;

				if (mmc_wimax_get_wimax_FW_freeze_WK_RX()) {
					sqn_pr_info("Try to disable interrupt at host side: \n");
					sqn_pr_info("disable MMC_CAP_SDIO_IRQ & enable_sdio_irq 0, host->caps %lx\n", host->caps);
					host->caps &= ~MMC_CAP_SDIO_IRQ;
					host->ops->enable_sdio_irq(host, 0);

					sqn_pr_info("disable GPIO%d wakeup interrupt\n", mmc_wimax_get_hostwakeup_gpio());
					disable_irq_nosync(mmc_wimax_get_hostwakeup_IRQ_ID());
				}
			} else
				ReadDataFailed_ctr++;
		} else
			ReadDataFailed_ctr = 0;

		retry = 0;

retry_ClearLSB_IN_RD_FIFO2:
		/* Clear interrupt flag */
		sdio_writeb(func, SQN_SDIO_IT_RD_FIFO2_WM,
				SQN_SDIO_IT_STATUS_LSBS, &rc);

		if (!rc) {
			/* sqn_pr_info("%s: clear interrupt(LSB) successful at #%d!\n", __func__, retry); */
			;
		} else {
			sqn_pr_info("%s: clear interrupt(LSB) failed at #%d!\n", __func__, retry);
		}

		if (rc && retry < 5) {
			retry++;
			goto retry_ClearLSB_IN_RD_FIFO2;
		}
	}

out:
	sqn_pr_dbg("%s: returned code: %d\n", __func__, rc);
	sqn_pr_leave();
	return rc;
}


static int sqn_sdio_it_msb(struct sdio_func *func)
{
	int rc = 0;
	u8 status = 0;
	int retry = 0;

	sqn_pr_enter();

retry_MSB:
	/* Read the interrupt status */
	status = sdio_readb(func, SQN_SDIO_IT_STATUS_MSBS, &rc);

	if (!rc) {
		/* sqn_pr_info("%s: read interrupt(MSB) successful at #%d!\n", __func__, retry); */
		;
	} else {
		sqn_pr_info("%s: read interrupt(MSB) failed at #%d!\n", __func__, retry);
	}

	if (rc && retry < 5) {
		retry++;
		goto retry_MSB;
	}

	sqn_pr_dbg("interrupt(MSB): 0x%02X\n", (unsigned char) status);

	/* TODO: Handle interrupt */
	sqn_pr_dbg("skipping any interrupt...\n");

	retry = 0;

retry_ClearMSB:
	/* Clear interrupt flag */
	sdio_writeb(func, 0xff, SQN_SDIO_IT_STATUS_MSBS, &rc);

	if (!rc) {
		/* sqn_pr_info("%s: clear interrupt(MSB) successful at #%d!\n", __func__, retry); */
		;
	} else {
		sqn_pr_info("%s: clear interrupt(MSB) failed at #%d!\n", __func__, retry);
	}

	if (rc && retry < 5) {
		retry++;
		goto retry_ClearMSB;
	}

out:
	sqn_pr_dbg("%s: returned code: %d\n", __func__, rc);
	sqn_pr_leave();
	return rc;
}


/*
 * defined in "drivers/mmc/omap2430_hsmmc.c"
 * in linux kernel from TI
 */
int sdio_int_enable(int enable, int slot);


void sqn_sdio_interrupt(struct sdio_func *func)
{
	unsigned long irq_flags = 0;
	u8 is_card_sleeps = 0;
	struct sqn_sdio_card *card = sdio_get_drvdata(func);

	sqn_pr_enter();

	sqn_sdio_it_lsb(func);

	spin_lock_irqsave(&card->priv->drv_lock, irq_flags);
	is_card_sleeps = card->is_card_sleeps;
	spin_unlock_irqrestore(&card->priv->drv_lock, irq_flags);

	if (!is_card_sleeps)
		sqn_sdio_it_msb(func);

	sqn_pr_leave();
}


static int sqn_sdio_it_enable(struct sdio_func *func)
{
	u8 enable = 0;
	int rv = 0;
	int retry = 0;

	sqn_pr_enter();
	sdio_claim_host(func);

	/* enable LSB */
	enable = SQN_SDIO_IT_WR_FIFO2_WM | SQN_SDIO_IT_RD_FIFO2_WM |
		 SQN_SDIO_IT_SW_SIGN;

retry_EN_LSB:
	sdio_writeb(func, enable, SQN_SDIO_IT_EN_LSBS, &rv);
	sqn_pr_dbg("enabled LSBS interrupt: rv=0x%02X\n", rv);


	if (!rv) {
		/* sqn_pr_info("%s: enable interrupt(LSB) successful at #%d!\n", __func__, retry); */
		;
	} else {
		sqn_pr_info("%s: enable interrupt(LSB) failed at #%d!\n", __func__, retry);
	}

	if (rv && retry < 5) {
		retry++;
		goto retry_EN_LSB;
	}

	sqn_pr_dbg("enabled interrupt(LSB): 0x%02X\n",
		   (unsigned char) enable);

	retry = 0;

retry_RD_FIFO:
	/* Set RD watermark to enable interrups for RX packets */
	sdio_writew(func, 1, SQN_SDIO_WM_RD_FIFO(2), &rv);
	sqn_pr_dbg("enabled rd watermark: rv=%d\n", rv);

	if (!rv) {
		/* sqn_pr_info("%s: enable rd watermark successful at #%d!\n", __func__, retry); */
		;
	} else {
		sqn_pr_info("%s: enable rd watermark failed at #%d!\n", __func__, retry);
	}

	if (rv && retry < 5) {
		retry++;
		goto retry_RD_FIFO;
	}

out:
	sdio_release_host(func);
	sqn_pr_dbg("returned code: %d\n", rv);
	sqn_pr_leave();
	return rv;
}


static int sqn_sdio_it_disable(struct sdio_func *func)
{
	int rc = 0;
	int retry = 0;

	sqn_pr_enter();
	sdio_claim_host(func);

	retry = 0;

retry_LSB:
	/* disable LSB */
	sdio_writeb(func, 0, SQN_SDIO_IT_EN_LSBS, &rc);

	if (!rc)
		sqn_pr_info("disabled interrupt(LSB) successful at #%d!\n", retry);
	else
		sqn_pr_info("disabled interrupt(LSB) failed at #%d!\n", retry);

	if (rc && retry < 5) {
		retry++;
		goto retry_LSB;
	}

	retry = 0;

retry_MSB:
	/* disable MSB */
	sdio_writeb(func, 0, SQN_SDIO_IT_EN_MSBS, &rc);

	if (!rc)
		sqn_pr_info("disabled interrupt(MSB) successful! at #%d!\n", retry);
	else
		sqn_pr_info("disabled interrupt(MSB) failed! at #%d!\n", retry);

	if (rc && retry < 5) {
		retry++;
		goto retry_MSB;
	}

out:
	sqn_pr_dbg("returned code: %d\n", rc);
	sdio_release_host(func);
	sqn_pr_leave();
	return rc;
}


void sqn_sdio_stop_it_thread_from_itself(struct sqn_private *priv)
{
	struct sqn_sdio_card *card = priv->card;
	unsigned long irq_flags = 0;
	sqn_pr_enter();

	spin_lock_irqsave(&priv->drv_lock, irq_flags);
	card->it_thread_should_stop = 1;
	spin_unlock_irqrestore(&priv->drv_lock, irq_flags);

	sqn_pr_leave();
}

/*******************************************************************/
/* Driver registration                                             */
/*******************************************************************/

#ifdef DEBUG
static void sqn_sdio_debug_test(struct sdio_func *func)
{
	/* int rc = 0; */
	/* int val = 0; */

	sqn_pr_enter();
	sdio_claim_host(func);

	/* sqn_pr_dbg("write SQN_SOC_SIGS_LSBS\n"); */
	/* sdio_writeb(func, 1, SQN_SOC_SIGS_LSBS, &rc); */
	/* if (rc) */
		/* sqn_pr_dbg("error when writing to SQN_SOC_SIGS_LSBS: %d\n", rc); */

#if 0
	sqn_pr_dbg("readb 0x04\n");
	val = sdio_readb(func, 0x04, &rc);
	if (rc)
		sqn_pr_dbg("readb 0x04 failed %x\n", rc);
	else
		sqn_pr_dbg("readb 0x04 = %x\n", val);

	sqn_pr_dbg("readb 0x2028\n");
	val = sdio_readb(func, 0x2028, &rc);
	if (rc)
		sqn_pr_dbg("readb 0x2028 failed %x\n", rc);
	else
		sqn_pr_dbg("readb 0x2028 = %x\n", val);

	sqn_pr_dbg("readw 0x2028\n");
	val = sdio_readw(func, 0x2028, &rc);
	if (rc)
		sqn_pr_dbg("readw 0x2028 failed %x\n", rc);
	else
		sqn_pr_dbg("readw 0x2028 = %x\n", val);

	sqn_pr_dbg("readb RSTN\n");
	val = sdio_readb(func, SQN_SDIO_RSTN_WR_FIFO(2), &rc);
	if (rc)
		sqn_pr_dbg("readb RSTN failed %x\n", rc);
	else
		sqn_pr_dbg("readb RSTN = %x\n", val);

	sqn_pr_dbg("readl LEVEL\n");
	val = sdio_readw(func, SQN_SDIO_WR_FIFO_LEVEL(2), &rc);
	if (rc)
		sqn_pr_dbg("readl LEVEL failed %x\n", rc);
	else
		sqn_pr_dbg("readl LEVEL = %x\n", val);

	sqn_pr_dbg("readl 0x2060\n");
	val = sdio_readl(func, 0x2060, &rc);
	if (rc)
		sqn_pr_dbg("readl 0x2060 failed %x\n", rc);
	else
		sqn_pr_dbg("readl 0x2060 = %x\n", val);

	sqn_pr_dbg("writew SQN_SDIO_WM_RD_FIFO(2)\n");
	sdio_writel(func, 1, SQN_SDIO_WM_RD_FIFO(2), &rc);
	if (rc)
		sqn_pr_dbg("writel SQN_SDIO_WM_RD_FIFO(2) failed %x\n", rc);
	else
		sqn_pr_dbg("writel SQN_SDIO_WM_RD_FIFO(2) = %x\n", rc);
#endif

	sdio_release_host(func);
	sqn_pr_leave();
}

static void sqn_sdio_print_debug_info(struct sdio_func *func)
{
	sqn_pr_enter();

	sqn_pr_info("sdio_func: device[%02x]: %04x:%04x\n", func->class, func->vendor,
			func->device);
	sqn_pr_info("sdio_func: block size: %d (maximum %d)\n", func->cur_blksize,
			func->max_blksize);
	sqn_pr_info("sdio_func: func->state: 0x%04x, card->state: 0x%04x\n"
		, func->state, func->card->state);
	sqn_pr_info("mmc_bus: clock=%u, width=%u, mode=%u, vdd=%u\n"
			, func->card->host->ios.clock
			, func->card->host->ios.bus_width
			, func->card->host->ios.bus_mode
			, func->card->host->ios.vdd);

	sqn_pr_dbg("host->caps=%x\n", (u32) func->card->host->caps);

	sqn_pr_leave();
}
#endif /* DEBUG */


static void sqn_sdio_free_tx_queue(struct sqn_sdio_card *card)
{
	struct sk_buff *skb = 0;
	while (0 != (skb = skb_dequeue(&card->tx_queue)))
		dev_kfree_skb_any(skb);
}


static void sqn_sdio_free_rx_queue(struct sqn_sdio_card *card)
{
	struct sk_buff *skb = 0;
	while (0 != (skb = skb_dequeue(&card->rx_queue)))
		dev_kfree_skb_any(skb);
}


static int check_boot_from_host_mode(struct sdio_func *func)
{
	int rv = 0;
	int status = 0;

	sdio_claim_host(func);
	status = sdio_readb(func, SQN_H_BOOT_FROM_SPI, &rv);
	sdio_release_host(func);

	if (rv)	{
		sqn_pr_err("can't read boot flags from device");
		return 0;
	}

	return !status;
}


static int sqn_check_card_id(struct sdio_func *func)
{
	int rv = 0;
	unsigned short manf_id = 0;
	unsigned short card_id = 0;

	sqn_pr_enter();
	sqn_pr_info("Checking card IDs...\n");

	manf_id = sdio_readw(func, SDIO_CMN_CISTPLMID_MANF, &rv);
	if (rv) {
		sqn_pr_err("can't read card manufacturer id\n");
		rv = 0;
		goto out;
	}

	card_id = sdio_readw(func, SDIO_CMN_CISTPLMID_CARD, &rv);
	if (rv) {
		sqn_pr_err("can't read card id\n");
		rv = 0;
		goto out;
	}

	if (manf_id != SDIO_VENDOR_ID_SEQUANS
	    || card_id != SDIO_DEVICE_ID_SEQUANS_SQN1130) {
		sqn_pr_info("found card with UNSUPPORTED manf_id=%x card_id=%x\n"
			, manf_id, card_id);
		rv = 0;
	} else {
		sqn_pr_info("found card with SUPPORTED manf_id=%x card_id=%x\n"
			, manf_id, card_id);
		rv = 1;
	}

out:
	sqn_pr_leave();
	return rv;
}


static u8 sqn_get_card_version(struct sdio_func *func)
{
	int rv = 0;
	u32  version = 0;

	sqn_pr_enter();

	switch (func->device) {
	case SDIO_DEVICE_ID_SEQUANS_SQN1130:
		sqn_pr_info("found SQN1130 card\n");
		/*
		 * Let bootrom/firmware name to be overridden from userspace as
		 * a module parameter, so we change it only if it was not
		 * changed from its default value
		 */
		if (0 == strcmp(firmware_name, SQN_DEFAULT_FW_NAME))
			firmware_name = fw1130_name;
		rv = SQN_1130;
		break;
	case SDIO_DEVICE_ID_SEQUANS_SQN1210:
	case SDIO_DEVICE_ID_SEQUANS_SQN1220:
		sqn_pr_info("found SQN12x0 card\n");

#ifndef SQN_UNIQUE_FIRMWARE
		/*
		 * Let firmware_name to be overridden from userspace as a module
		 * parameter, so we change firmware_name only if it was not
		 * changed from its default value
		 */
		if (0 == strcmp(firmware_name, SQN_DEFAULT_FW_NAME))
			firmware_name = fw1210_name;
#endif
		rv = SQN_1210;
		break;
	default:
		sqn_pr_info("found UNKNOWN card with vendor_id 0x%x"
			" dev_id 0x%x\n", func->vendor, func->device);
		rv = 0;
	}

/* Maintain in compilable state but don't use it for now */
#if 0
/*
 * For production devices this is not needed, we can get a device id
 * from sdio_func
 */
	sqn_pr_info("Checking card version...\n");

	sdio_claim_host(func);
	version = sdio_readl(func, SQN_H_VERSION, &rv);
	sdio_release_host(func);
	if (rv) {
		sqn_pr_err("failed to read card version\n");
		rv = 0;
		goto out;
	}

#define SQN1130_MAJOR_VERSION 0x06
#define SQN12x0_MAJOR_VERSION 0x0a

	if (SQN1130_MAJOR_VERSION == (version & 0xff)) {
		sqn_pr_info("found SQN_1130 card with version id 0x%x\n"
			, version);
		rv = SQN_1130;
	} else if (SQN12x0_MAJOR_VERSION == (version & 0xff)) {
		sqn_pr_info("found SQN_1210 card with version id 0x%x\n"
			, version);
		rv = SQN_1210;
	} else {
		sqn_pr_info("found UNKNOWN card with version id 0x%x\n"
			, version);
		rv = 0;
	}
#endif

out:
	sqn_pr_leave();
	return rv;
}

int sqn_sdio_notify_host_wakeup(void);

static irqreturn_t wimax_wakeup_gpio_irq_handler(int irq, void *dev_id)
{
	struct sqn_sdio_card *card = g_priv->card;
	struct msmsdcc_host *msm_host = mmc_priv(card->func->card->host);
    struct mmc_card *mmc_card = card->func->card;

	printk(KERN_INFO "wimax_wakeup_gpio_irq_handler+\n");
	sqn_pr_enter();

	/* To avoid flush the logging in kmsg, remove it. */
	/* We have two wakeup source - async sdio irq and host wake up. To debug this, dump the gpio wakeup log */
	/* if (mmc_wimax_get_sdio_interrupt_log()) { */
	if (1) {
		if (printk_ratelimit())
			printk(KERN_INFO "WiMAX GPIO interrupt\n");
	}

    mmc_wimax_set_FWWakeupHostEvent(1); /* To enable RX Wakeup reason dumping */

#if 0
	if (!sqn_sdio_get_sdc_clocks()) {
		/* Handled by pm_runtime in mmc. */
		sqn_pr_info("%s: msmsdcc_enable_clocks \n", __func__);
		msmsdcc_enable_clocks(msm_host);
		sqn_pr_info("%s: msmsdcc_disable_clocks(msm_host, 5 * HZ)\n", __func__);
		msmsdcc_disable_clocks(msm_host, 5 * HZ);
	}
#endif

// ** [Start] wimax kennel 3.4 porting
    //msmsdcc_switch_clock(mmc_card->host, 1);
	//msmsdcc_setup_clocks(msm_host,true);
// ** [End] wimax kennel 3.4 porting

	sqn_pr_leave();

	printk(KERN_INFO "wimax_wakeup_gpio_irq_handler-\n");
	return IRQ_HANDLED;
}

int sqn_sdio_notify_host_wakeup(void)
{
	struct sqn_sdio_card *card = g_priv->card;
	int rv = 0;

	sqn_pr_enter();

	rv = sqn_wakeup_fw(card->func);

	sqn_pr_leave();

	return rv;
}
EXPORT_SYMBOL(sqn_sdio_notify_host_wakeup);

int  init_thp_handler(struct net_device *dev);
void cleanup_thp_handler(void);

#ifdef CONFIG_WIMAX_SDIO_HIGH_SPEED
int  mmc_sd_switch(struct mmc_card *card, int mode, int group, u8 value, u8 *resp);
void mmc_set_timing(struct mmc_host *host, unsigned int timing);

static int sqn_sdio_switch_hs(struct sqn_sdio_card *card)
{
       struct mmc_card *mmc_card = card->func->card;
       int rv = 0;
       u8 *status = 0;

       sqn_pr_enter();

       sqn_pr_info("switching SQN chip to High Speed mode\n");

       status = kmalloc(64, GFP_KERNEL);
       if (!status) {
               sqn_pr_err("failed to allocate a buffer for CMD6 response\n");
               rv = -ENOMEM;
               goto out;
       }

       sdio_claim_host(card->func);

       rv = mmc_sd_switch(mmc_card, 1, 0, 1, status);
       if (rv) {
               sqn_pr_err("mmc_sd_switch failed with error %d\n", rv);
               goto free;
       }

       sqn_pr_info_dump("CMD6 resp: ", status, 64);

       if ((status[16] & 0xF) != 1) {
               sqn_pr_err("problem switching SDIO card into high-speed mode\n");
       } else {
               mmc_card_set_highspeed(mmc_card);
               mmc_set_timing(mmc_card->host, MMC_TIMING_SD_HS);
       }
free:
       sdio_release_host(card->func);
       kfree(status);
out:
       sqn_pr_leave();
       return rv;
}

extern struct sqn_sdio_card *_g_sqn_sdio_card;
#endif

static int sqn_sdio_probe(struct sdio_func *func,
		const struct sdio_device_id *id)
{
	int rv = 0;
	struct sqn_sdio_card *sqn_card = 0;
	struct sqn_private *priv = 0;
	int counter = 0;
	int delay = 0;

	int err;
	u32 irq;
	u32 req_flags = IRQF_TRIGGER_RISING;

	sqn_pr_enter();

	sqn_pr_info("module parameters: firmware_name='%s' load_firmware=%d\n"
		, firmware_name, load_firmware);

#ifdef DEBUG
	sqn_sdio_print_debug_info(func);
	/* sqn_sdio_debug_test(func); */
#endif

	/* Allocate card's private data storage */
	sqn_card = kzalloc(sizeof(struct sqn_sdio_card), GFP_KERNEL);
	if (!sqn_card) {
		rv = -ENOMEM;
		goto out;
	}
	_g_sqn_sdio_card = sqn_card;

	sqn_card->version = sqn_get_card_version(func);

	if (0 == sqn_card->version) {
		rv = -EPROTO;
		goto free_card;
	}

	skb_queue_head_init(&sqn_card->tx_queue);
	skb_queue_head_init(&sqn_card->rx_queue);
	init_waitqueue_head(&sqn_card->pm_waitq);
	mutex_init(&sqn_card->tx_mutex);
	mutex_init(&sqn_card->rx_mutex);
	mutex_init(&sqn_card->rxq_mutex);

	wake_lock_init(&sqn_card->wakelock_host, WAKE_LOCK_SUSPEND, "sqnsdio_host");
	wake_lock_init(&sqn_card->wakelock_tx, WAKE_LOCK_SUSPEND, "sqnsdio_tx");
	wake_lock_init(&sqn_card->wakelock_rx, WAKE_LOCK_SUSPEND, "sqnsdio_rx");
	setup_timer(&sqn_card->wakelock_timer_host
		, sqn_sdio_wake_lock_release_host_timer_fn
		, (unsigned long) sqn_card);
	setup_timer(&sqn_card->wakelock_timer_tx
		, sqn_sdio_wake_lock_release_tx_timer_fn
		, (unsigned long) sqn_card);
	setup_timer(&sqn_card->wakelock_timer_rx
		, sqn_sdio_wake_lock_release_rx_timer_fn
		, (unsigned long) sqn_card);

	sqn_card->func = func;

    /* Prior the bootup sequnce to add_card to initilize the card->priv->drv_lock */
    sdio_set_drvdata(func, sqn_card);
	priv = sqn_add_card(sqn_card, &func->dev);
	if (!priv) {
		rv = -ENOMEM;
		goto reclaim;
	}
	/* Prior end */

	sqn_card->priv = priv;

	/* Activate SDIO function and register interrupt handler */
	sdio_claim_host(func);

	rv = sdio_enable_func(func);
	if (rv)
		goto release;

	rv = sdio_claim_irq(func, sqn_sdio_interrupt);
	if (rv)
		goto disable;

	sdio_release_host(func);

    /*
    // Prior the code as above. Andrew 0214

	sdio_set_drvdata(func, sqn_card);
	priv = sqn_add_card(sqn_card, &func->dev);
	if (!priv) {
		rv = -ENOMEM;
		goto reclaim;
	}

	sqn_card->priv = priv;
    */

	INIT_WORK(&priv->rx_work_struct, sqn_sdio_process_rx_queue);
	priv->card = sqn_card;
	priv->hw_host_to_card = sqn_sdio_host_to_card;
	priv->add_skb_to_tx_queue = sqn_sdio_add_skb_to_tx_queue;
	priv->is_tx_queue_empty = sqn_sdio_is_tx_queue_empty;

	/* Load firmware if card needs it */
	if (check_boot_from_host_mode(sqn_card->func)) {
		rv = sqn_load_firmware(sqn_card->func);
		if (rv)
			goto err_activate_card;
	}

	memcpy(priv->dev->dev_addr, priv->mac_addr, ETH_ALEN);

	rv = sqn_start_card(priv);
	if (rv)
		goto err_activate_card;

	/* We need to setup thp_handler now, to catch all THP packets
	 * as soon as they appear after interrupts are enabled
	 */
	rv = init_thp_handler(priv->dev);
	if (rv)
		goto unreg_netdev;

	/* Enable interrupts, now everything is set up */
	rv = sqn_sdio_it_enable(sqn_card->func);
	if (rv)
		goto clean_thp_handler;

	sqn_pr_info("wait until FW is started...\n");
	counter = 20;
	delay = 500;
	while (0 == sqn_sdio_get_rstn_wr_fifo_flag(priv) && --counter > 0) {
		sqn_pr_dbg("FW is not started yet, sleep for %d msecs,"
			" %d retries left\n"
			, delay
			, counter);
		msleep(delay);
	}

	if (0 == sqn_card->rstn_wr_fifo_flag)
		sqn_pr_warn("FW is still not started, anyway continue as is...\n");

    /* regiseter HOST_WAKEUP irq */
	sqn_pr_info("setup GPIO%d for wakeup form SQN1210\n", mmc_wimax_get_hostwakeup_gpio());
	rv = irq = mmc_wimax_get_hostwakeup_IRQ_ID(); /* HOST WAKEUP GPIO as wakeup */

	if (rv < 0) {
		sqn_pr_warn("wimax-gpio to irq failed\n");
		goto disable;
	}
	/*in shooter kernel1060 pmic driver would set every IRQ as nested function
	,so we need to use request_any_context_irq to detect it and pass the check*/
	/* IRQF_TRIGGER_RISING, raising trigger */
	rv = request_any_context_irq(irq, wimax_wakeup_gpio_irq_handler,
								  req_flags, "WiMAX0", sqn_card->priv->dev);
	if (rv < 0) {
		sqn_pr_warn("wimax-gpio request_irq %d failed=%d\n", irq, rv);
		goto disable;
	}
	sqn_pr_dbg("disable GPIO%d interrupt\n", mmc_wimax_get_hostwakeup_gpio());
	disable_irq(mmc_wimax_get_hostwakeup_IRQ_ID());

	rv = init_thp(priv->dev);
	if (rv)
		goto clean_thp_handler;

#ifdef DEBUG
	/* sqn_sdio_debug_test(sqn_card->func); */
#endif

#ifdef CONFIG_WIMAX_SDIO_HIGH_SPEED
	/* Switch card to high speed mode */
	rv = sqn_sdio_switch_hs(sqn_card);

	mmc_set_clock(func->card->host, 48000000);
#endif

out:
	sqn_pr_dbg("returned code: %d\n", rv);
	if (0 == rv)
		sqn_pr_info("card initialized successfuly\n");
	sqn_pr_leave();
	return rv;

clean_thp:
	cleanup_thp();
clean_thp_handler:
	cleanup_thp_handler();
unreg_netdev:
	unregister_netdev(priv->dev);
err_activate_card:
	flush_scheduled_work();
	free_netdev(priv->dev);
reclaim:
	sdio_claim_host(func);
	sdio_release_irq(func);
disable:
	sdio_disable_func(func);
release:
	sdio_release_host(func);
	sqn_sdio_free_tx_queue(sqn_card);
	sqn_sdio_free_rx_queue(sqn_card);

	/* release a wake_lock if it was not done for a some reason */
	if (wake_lock_active(&sqn_card->wakelock_host))
		wake_unlock(&sqn_card->wakelock_host);
	if (wake_lock_active(&sqn_card->wakelock_tx))
		wake_unlock(&sqn_card->wakelock_tx);
	if (wake_lock_active(&sqn_card->wakelock_rx))
		wake_unlock(&sqn_card->wakelock_rx);

	wake_lock_destroy(&sqn_card->wakelock_host);
	wake_lock_destroy(&sqn_card->wakelock_tx);
	wake_lock_destroy(&sqn_card->wakelock_rx);

free_card:
	kfree(sqn_card);

	goto out;
}

static void sqn_sdio_remove(struct sdio_func *func)
{
	struct sqn_sdio_card *sqn_card = sdio_get_drvdata(func);
	struct sqn_sdio_card *card = g_priv->card;
	struct msmsdcc_host *msm_host = mmc_priv(card->func->card->host);

	u32 count = 0;
	u32 delay = 0;
	int rv = 0;

	sqn_pr_enter();

	sqn_pr_info("free GPIO%d interrupt\n", mmc_wimax_get_hostwakeup_gpio());
	free_irq(mmc_wimax_get_hostwakeup_IRQ_ID(), sqn_card->priv->dev);

#if defined(DEBUG)
	sqn_sdio_print_debug_info(func);
#endif
	cleanup_thp();

	/*
	 * Let all running threads know that we are starting
	 * a remove procedure
	 */
	sqn_card->priv->removed = 1;
	delay = 1000;

	sqn_pr_info("+sqn_card->priv->removed = 1\n");

	sqn_sdio_it_disable(sqn_card->func);
	sqn_pr_info("+sqn_sdio_it_disable\n");

	/*
	 * Shooter: If we disable sdio thread too late, there will be lots IRQ block system and device hang.
	 * Sync the same flow as WIFI that "disable IRQ" -> "disable sdio thread" immediatelly.
	 */
	sdio_claim_host(func);
	sqn_pr_info("+sdio_claim_host\n");

	sdio_release_irq(func);
	sqn_pr_info("+sdio_release_irq\n");

	if (mmc_wimax_get_wimax_FW_freeze_WK_RX()) {

		sqn_pr_info("[WiMAX] reset controller for disable RX completely !!\n");
		msmsdcc_reset_and_restore(msm_host);

		func->card->host->caps |= MMC_CAP_SDIO_IRQ;
		sqn_pr_info("enable MMC_CAP_SDIO_IRQ - host->caps %lx\n", func->card->host->caps);
	}

	sdio_disable_func(func);
	sqn_pr_info("+sdio_disable_func\n");
	sdio_release_host(func);
	sqn_pr_info("+sdio_release_host\n");

	wake_up_interruptible(&g_card_sleep_waitq);

	sqn_pr_info("wait until RX is finished\n");
	count = 5;
	while (--count && !(rv = mutex_trylock(&sqn_card->rx_mutex)))
		msleep(delay);
	if (!rv)
		sqn_pr_warn("%s: failed to acquire RX mutex\n", __func__);

	sqn_stop_card(sqn_card->priv);
	wake_up_interruptible(&sqn_card->priv->tx_waitq);
	kthread_stop(sqn_card->priv->tx_thread);

	sqn_pr_info("wait until TX is finished\n");
	count = 5;
	while (--count && !(rv = mutex_trylock(&sqn_card->tx_mutex)))
		msleep(delay);
	if (!rv)
		sqn_pr_warn("%s: failed to acquire TX mutex\n", __func__);

	sqn_remove_card(sqn_card->priv);

	sqn_sdio_free_tx_queue(sqn_card);
	sqn_sdio_free_rx_queue(sqn_card);

	del_timer_sync(&sqn_card->wakelock_timer_host);
	del_timer_sync(&sqn_card->wakelock_timer_tx);
	del_timer_sync(&sqn_card->wakelock_timer_rx);

	/* release a wake_lock if it was not done for a some reason */
	if (wake_lock_active(&sqn_card->wakelock_host))
		wake_unlock(&sqn_card->wakelock_host);
	if (wake_lock_active(&sqn_card->wakelock_tx))
		wake_unlock(&sqn_card->wakelock_tx);
	if (wake_lock_active(&sqn_card->wakelock_rx))
		wake_unlock(&sqn_card->wakelock_rx);

	wake_lock_destroy(&sqn_card->wakelock_host);
	wake_lock_destroy(&sqn_card->wakelock_tx);
	wake_lock_destroy(&sqn_card->wakelock_rx);

	kfree(sqn_card);
	sdio_set_drvdata(func, 0);

	sqn_pr_info("card removed successfuly\n");
	mmc_detect_change(func->card->host, msecs_to_jiffies(500));
	sqn_pr_leave();
}


int sqn_sdio_suspend(struct sdio_func *func, pm_message_t msg)
{
	int rv = 0;
	// unsigned long irq_flags = 0;
	struct sqn_sdio_card *sqn_card = sdio_get_drvdata(func);

	sqn_pr_enter();
	sqn_pr_info("%s: enter\n", __func__);
	sqn_pr_dbg("pm_message = %x\n", msg.event);

	WARN(!skb_queue_empty(&sqn_card->tx_queue)
		, "BANG!!! TX queue is not empty in suspend(): %d"
		, skb_queue_len(&sqn_card->tx_queue));

	WARN(!skb_queue_empty(&sqn_card->rx_queue)
		, "BANG!!! RX queue is not empty in suspend(): %d"
		, skb_queue_len(&sqn_card->rx_queue));

	if (sqn_card->is_card_sleeps) {
		sqn_pr_info("card already asleep (pm_message = 0x%x)\n"
			, msg.event);
		goto out;
	}

	// Do nothing when system goes to power off
	if (PM_EVENT_SUSPEND != msg.event) {
		sqn_pr_warn("Not supported pm_message = %x\n", msg.event);
		goto out;
	}

	if (sqn_notify_host_sleep(func)) {
		sqn_pr_warn("Failed to suspend\n");
		goto out;
	}
out:

	mmc_wimax_enable_host_wakeup(1);

	sqn_pr_info("%s: leave\n", __func__);
	sqn_pr_leave();
	return rv;
}


int sqn_sdio_resume(struct sdio_func *func)
{
	int rv = 0;
	struct sqn_sdio_card *sqn_card = sdio_get_drvdata(func);

	sqn_pr_enter();
	sqn_pr_info("%s: enter\n", __func__);

	if (netif_queue_stopped(sqn_card->priv->dev)) {
		sqn_pr_dbg("wake netif_queue\n");
		netif_wake_queue(sqn_card->priv->dev);
	}

	// Dima: we don't need this, card will be woken up when there will be some TX data	
	// sqn_notify_host_wakeup(func);

	mmc_wimax_enable_host_wakeup(0);

	sqn_pr_info("%s: leave\n", __func__);
	sqn_pr_leave();
	return rv;
}

int sqn_sdio_dump_net_pkt(int on)
{
	printk(KERN_INFO "[WIMAX] [SDIO] %s: dump_net_pkt: %d\n", __func__, on);
	dump_net_pkt = on;

	return 0;
}

static struct sdio_driver sqn_sdio_driver = {
	.name		= SQN_MODULE_NAME
	, .id_table	= sqn_sdio_ids
	, .probe	= sqn_sdio_probe
	, .remove	= sqn_sdio_remove
	, .suspend 	= sqn_sdio_suspend
	, .resume	= sqn_sdio_resume
};


/*******************************************************************/
/* Module initialization                                           */
/*******************************************************************/

static int __init sqn_sdio_init_module(void)
{
	int rc = 0;

	sqn_pr_enter();

	sqn_pr_info("Sequans SDIO WiMAX driver, version %s\n"
		, SQN_MODULE_VERSION);
	sqn_pr_info("Copyright SEQUANS Communications\n");

	sqn_dfs_init();

	mmc_wimax_power(1);
	mmc_wimax_set_carddetect(1);
	/* thp_wimax_uart_switch(1); */
	mmc_wimax_set_status(1);
	dump_net_pkt = mmc_wimax_get_netlog_status();

	rc = sdio_register_driver(&sqn_sdio_driver);

	register_android_earlysuspend();

#if RESET_BY_WIMAXTRACKER
	sdio_netlink_register();
#endif

	sqn_pr_info("Driver has been registered\n");

	sqn_pr_leave();

	return rc;
}

static void __exit sqn_sdio_exit_module(void)
{
	sqn_pr_enter();

	sdio_unregister_driver(&sqn_sdio_driver);
	unregister_android_earlysuspend();

	mmc_wimax_set_carddetect(0);
	mmc_wimax_power(0);
	/* thp_wimax_uart_switch(0); */
	mmc_wimax_set_status(0);

	sqn_dfs_cleanup();

#if RESET_BY_WIMAXTRACKER
	sdio_netlink_deregister();
#endif

	sqn_pr_info("Driver has been removed\n");
	sqn_pr_leave();
}


module_init(sqn_sdio_init_module);
module_exit(sqn_sdio_exit_module);


MODULE_DESCRIPTION("Sequans WiMAX driver for SDIO devices");
MODULE_AUTHOR("Dmitriy Chumak, Andy Shevchenko");
MODULE_LICENSE("GPL");
MODULE_VERSION(SQN_MODULE_VERSION);
