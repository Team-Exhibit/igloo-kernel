/*
 * mac80211 STA and AP API for mac80211 ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "cw1200.h"
#include "sta.h"
#include "ap.h"
#include "bh.h"

#if defined(CONFIG_CW1200_STA_DEBUG)
#define ap_printk(...) printk(__VA_ARGS__)
#else
#define ap_printk(...)
#endif

#define CW1200_LINK_ID_GC_TIMEOUT ((unsigned long)(10 * HZ))

static int cw1200_upload_beacon(struct cw1200_common *priv);
static int cw1200_upload_pspoll(struct cw1200_common *priv);
static int cw1200_upload_null(struct cw1200_common *priv);
static int cw1200_start_ap(struct cw1200_common *priv);
static int cw1200_update_beaconing(struct cw1200_common *priv);
static int cw1200_enable_beaconing(struct cw1200_common *priv,
				   bool enable);
static void __cw1200_sta_notify(struct ieee80211_hw *dev,
				struct ieee80211_vif *vif,
				enum sta_notify_cmd notify_cmd,
				struct ieee80211_sta *sta);

/* ******************************************************************** */
/* AP API								*/

int cw1200_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct cw1200_common *priv = hw->priv;
	struct cw1200_sta_priv *sta_priv =
			(struct cw1200_sta_priv *)&sta->drv_priv;
	struct cw1200_link_entry *entry;
	struct sk_buff *skb;

	if (priv->mode != NL80211_IFTYPE_AP)
		return 0;

	sta_priv->link_id = cw1200_find_link_id(priv, sta->addr);
	if (WARN_ON(!sta_priv->link_id)) {
		/* Impossible error */
		wiphy_info(priv->hw->wiphy,
			"[AP] No more link IDs available.\n");
		return -ENOENT;
	}

	entry = &priv->link_id_db[sta_priv->link_id - 1];
	spin_lock_bh(&priv->ps_state_lock);
	entry->status = CW1200_LINK_HARD;
	while ((skb = skb_dequeue(&entry->rx_queue)))
		ieee80211_rx_irqsafe(priv->hw, skb);
	spin_unlock_bh(&priv->ps_state_lock);
	return 0;
}

int cw1200_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta)
{
	struct cw1200_common *priv = hw->priv;
	struct cw1200_sta_priv *sta_priv =
			(struct cw1200_sta_priv *)&sta->drv_priv;
	struct cw1200_link_entry *entry;

	if (priv->mode != NL80211_IFTYPE_AP || !sta_priv->link_id)
		return 0;

	entry = &priv->link_id_db[sta_priv->link_id - 1];
	spin_lock_bh(&priv->ps_state_lock);
	entry->status = CW1200_LINK_SOFT;
	entry->timestamp = jiffies;
	if (!delayed_work_pending(&priv->link_id_gc_work))
		queue_delayed_work(priv->workqueue,
				&priv->link_id_gc_work,
				CW1200_LINK_ID_GC_TIMEOUT);
	spin_unlock_bh(&priv->ps_state_lock);
	return 0;
}

static void __cw1200_sta_notify(struct ieee80211_hw *dev,
				struct ieee80211_vif *vif,
				enum sta_notify_cmd notify_cmd,
				struct ieee80211_sta *sta)
{
	struct cw1200_common *priv = dev->priv;
	struct cw1200_sta_priv *sta_priv =
		(struct cw1200_sta_priv *)&sta->drv_priv;
	u32 bit = BIT(sta_priv->link_id);
	u32 prev = priv->sta_asleep_mask & bit;

	switch (notify_cmd) {
	case STA_NOTIFY_SLEEP:
		if (!prev) {
			if (priv->buffered_multicasts &&
					!priv->sta_asleep_mask)
				queue_work(priv->workqueue,
					&priv->multicast_start_work);
			priv->sta_asleep_mask |= bit;
		}
		break;
	case STA_NOTIFY_AWAKE:
		if (prev) {
			priv->sta_asleep_mask &= ~bit;
			priv->pspoll_mask &= ~bit;
			if (priv->tx_multicast &&
					!priv->sta_asleep_mask)
				queue_work(priv->workqueue,
					&priv->multicast_stop_work);
			cw1200_bh_wakeup(priv);
		}
		break;
	}
}

void cw1200_sta_notify(struct ieee80211_hw *dev,
		       struct ieee80211_vif *vif,
		       enum sta_notify_cmd notify_cmd,
		       struct ieee80211_sta *sta)
{
	struct cw1200_common *priv = dev->priv;
	spin_lock_bh(&priv->ps_state_lock);
	__cw1200_sta_notify(dev, vif, notify_cmd, sta);
	spin_unlock_bh(&priv->ps_state_lock);
}

static void cw1200_ps_notify(struct cw1200_common *priv,
		      int link_id, bool ps)
{
	struct ieee80211_sta *sta;

	if (!link_id || link_id > CW1200_MAX_STA_IN_AP_MODE)
		return;

	txrx_printk(KERN_DEBUG "%s for LinkId: %d. STAs asleep: %.8X\n",
			ps ? "Stop" : "Start",
			link_id, priv->sta_asleep_mask);

	rcu_read_lock();
	sta = ieee80211_find_sta(priv->vif,
			priv->link_id_db[link_id - 1].mac);
	if (sta) {
		__cw1200_sta_notify(priv->hw, priv->vif,
			ps ? STA_NOTIFY_SLEEP : STA_NOTIFY_AWAKE, sta);
	}
	rcu_read_unlock();
}

static int cw1200_set_tim_impl(struct cw1200_common *priv, bool aid0_bit_set)
{
	struct sk_buff *skb;
	struct wsm_update_ie update_ie = {
		.what = WSM_UPDATE_IE_BEACON,
		.count = 1,
	};
	u16 tim_offset, tim_length;

	ap_printk(KERN_DEBUG "[AP] %s mcast: %s.\n",
		__func__, aid0_bit_set ? "ena" : "dis");

	skb = ieee80211_beacon_get_tim(priv->hw, priv->vif,
			&tim_offset, &tim_length);
	if (!skb) {
		if (!__cw1200_flush(priv, true))
			wsm_unlock_tx(priv);
		return -ENOENT;
	}

	if (tim_offset && tim_length >= 6) {
		/* Ignore DTIM count from mac80211:
		 * firmware handles DTIM internally. */
		skb->data[tim_offset + 2] = 0;

		/* Set/reset aid0 bit */
		if (aid0_bit_set)
			skb->data[tim_offset + 4] |= 1;
		else
			skb->data[tim_offset + 4] &= ~1;
	}

	update_ie.ies = &skb->data[tim_offset];
	update_ie.length = tim_length;
	WARN_ON(wsm_update_ie(priv, &update_ie));

	dev_kfree_skb(skb);

	return 0;
}

void cw1200_set_tim_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, set_tim_work);
	(void)cw1200_set_tim_impl(priv, priv->aid0_bit_set);
}

int cw1200_set_tim(struct ieee80211_hw *dev, struct ieee80211_sta *sta,
		   bool set)
{
	struct cw1200_common *priv = dev->priv;
	queue_work(priv->workqueue, &priv->set_tim_work);
	return 0;
}

static int cw1200_set_btcoexinfo(struct cw1200_common *priv)
{
	struct wsm_override_internal_txrate arg;
	int ret = 0;

	if (priv->mode == NL80211_IFTYPE_STATION) {
		/* Plumb PSPOLL and NULL template */
		WARN_ON(cw1200_upload_pspoll(priv));
		WARN_ON(cw1200_upload_null(priv));
	} else {
		return 0;
	}

	memset(&arg, 0, sizeof(struct wsm_override_internal_txrate));

	if (!priv->vif->p2p) {
		/* STATION mode */
		if (priv->bss_params.operationalRateSet & ~0xF) {
			ap_printk(KERN_DEBUG "[STA] STA has ERP rates\n");
			/* G or BG mode */
			arg.internalTxRate = (__ffs(
			priv->bss_params.operationalRateSet & ~0xF));
		} else {
			ap_printk(KERN_DEBUG "[STA] STA has non ERP rates\n");
			/* B only mode */
			arg.internalTxRate = (__ffs(
			priv->association_mode.basicRateSet));
		}
		arg.nonErpInternalTxRate = (__ffs(
			priv->association_mode.basicRateSet));
	} else {
		/* P2P mode */
		arg.internalTxRate = (__ffs(
			priv->bss_params.operationalRateSet & ~0xF));
		arg.nonErpInternalTxRate = (__ffs(
			priv->bss_params.operationalRateSet & ~0xF));
	}

	ap_printk(KERN_DEBUG "[STA] BTCOEX_INFO"
		"MODE %d, internalTxRate : %x, nonErpInternalTxRate: %x\n",
		priv->mode,
		arg.internalTxRate,
		arg.nonErpInternalTxRate);

	ret = WARN_ON(wsm_write_mib(priv, WSM_MIB_ID_OVERRIDE_INTERNAL_TX_RATE,
		&arg, sizeof(arg)));

	return ret;
}

void cw1200_bss_info_changed(struct ieee80211_hw *dev,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *info,
			     u32 changed)
{
	struct cw1200_common *priv = dev->priv;
	struct ieee80211_conf *conf = &dev->conf;

	mutex_lock(&priv->conf_mutex);
	if (changed & BSS_CHANGED_BSSID) {
		memcpy(priv->bssid, info->bssid, ETH_ALEN);
		cw1200_setup_mac(priv);
	}

	/* TODO: BSS_CHANGED_IBSS */

	if (changed & BSS_CHANGED_ARP_FILTER) {
		struct wsm_arp_ipv4_filter filter = {0};
		int i;

		ap_printk(KERN_DEBUG "[STA] BSS_CHANGED_ARP_FILTER "
				     "enabled: %d, cnt: %d\n",
				     info->arp_filter_enabled,
				     info->arp_addr_cnt);

		if (info->arp_filter_enabled)
			filter.enable = __cpu_to_le32(1);

		/* Currently only one IP address is supported by firmware.
		 * In case of more IPs arp filtering will be disabled. */
		if (info->arp_addr_cnt > 0 &&
		    info->arp_addr_cnt <= WSM_MAX_ARP_IP_ADDRTABLE_ENTRIES) {
			for (i = 0; i < info->arp_addr_cnt; i++) {
				filter.ipv4Address[i] = info->arp_addr_list[i];
				ap_printk(KERN_DEBUG "[STA] addr[%d]: 0x%X\n",
					  i, filter.ipv4Address[i]);
			}
		} else
			filter.enable = 0;

		ap_printk(KERN_DEBUG "[STA] arp ip filter enable: %d\n",
			  __le32_to_cpu(filter.enable));

		if (wsm_set_arp_ipv4_filter(priv, &filter))
			WARN_ON(1);
	}


	if (changed & BSS_CHANGED_BEACON) {
		ap_printk(KERN_DEBUG "BSS_CHANGED_BEACON\n");
		WARN_ON(cw1200_update_beaconing(priv));
		WARN_ON(cw1200_upload_beacon(priv));
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		ap_printk(KERN_DEBUG "BSS_CHANGED_BEACON_ENABLED\n");

		if (priv->enable_beacon != info->enable_beacon) {
			WARN_ON(cw1200_enable_beaconing(priv,
							info->enable_beacon));
			priv->enable_beacon = info->enable_beacon;
		}
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		ap_printk(KERN_DEBUG "CHANGED_BEACON_INT\n");
		/* Restart AP only when connected */
		if (priv->join_status == CW1200_JOIN_STATUS_AP)
			WARN_ON(cw1200_update_beaconing(priv));
	}


	if (changed & BSS_CHANGED_ASSOC) {
		wsm_lock_tx(priv);
		priv->wep_default_key_id = -1;
		wsm_unlock_tx(priv);

		if (!info->assoc /* && !info->ibss_joined */) {
			priv->cqm_link_loss_count = 60;
			priv->cqm_beacon_loss_count = 20;
			priv->cqm_tx_failure_thold = 0;
		}
		priv->cqm_tx_failure_count = 0;
	}

	if (changed &
	    (BSS_CHANGED_ASSOC |
	     BSS_CHANGED_BASIC_RATES |
	     BSS_CHANGED_ERP_PREAMBLE |
	     BSS_CHANGED_HT |
	     BSS_CHANGED_ERP_SLOT)) {
		ap_printk(KERN_DEBUG "BSS_CHANGED_ASSOC.\n");
		if (info->assoc) { /* TODO: ibss_joined */
			int dtim_interval = conf->ps_dtim_period;
			int listen_interval = conf->listen_interval;
			struct ieee80211_sta *sta = NULL;

			/* Associated: kill join timeout */
			cancel_delayed_work_sync(&priv->join_timeout);

			rcu_read_lock();
			if (info->bssid)
				sta = ieee80211_find_sta(vif, info->bssid);
			if (sta) {
				BUG_ON(!priv->channel);
				priv->ht_info.ht_cap = sta->ht_cap;
				priv->bss_params.operationalRateSet =
					__cpu_to_le32(
					cw1200_rate_mask_to_wsm(priv,
					sta->supp_rates[priv->channel->band]));
				priv->ht_info.channel_type =
						info->channel_type;
				priv->ht_info.operation_mode =
						info->ht_operation_mode;
			} else {
				memset(&priv->ht_info, 0,
						sizeof(priv->ht_info));
				priv->bss_params.operationalRateSet = -1;
			}
			rcu_read_unlock();

			if (sta) {
				__le32 val = 0;
				if (priv->ht_info.operation_mode &
				IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT) {
					ap_printk(KERN_DEBUG"[STA]"
						" Non-GF STA present\n");
					/* Non Green field capable STA */
					val = __cpu_to_le32(BIT(1));
				}
				WARN_ON(wsm_write_mib(priv,
					WSM_MID_ID_SET_HT_PROTECTION,
					&val, sizeof(val)));
			}

			priv->association_mode.greenfieldMode =
				cw1200_ht_greenfield(&priv->ht_info);
			priv->association_mode.flags =
				WSM_ASSOCIATION_MODE_SNOOP_ASSOC_FRAMES |
				WSM_ASSOCIATION_MODE_USE_PREAMBLE_TYPE |
				WSM_ASSOCIATION_MODE_USE_HT_MODE |
				WSM_ASSOCIATION_MODE_USE_BASIC_RATE_SET |
				WSM_ASSOCIATION_MODE_USE_MPDU_START_SPACING;
			priv->association_mode.preambleType =
				info->use_short_preamble ?
				WSM_JOIN_PREAMBLE_SHORT :
				WSM_JOIN_PREAMBLE_LONG;
			priv->association_mode.basicRateSet = __cpu_to_le32(
				cw1200_rate_mask_to_wsm(priv,
				info->basic_rates));
			priv->association_mode.mpduStartSpacing =
				cw1200_ht_ampdu_density(&priv->ht_info);

#if defined(CONFIG_CW1200_USE_STE_EXTENSIONS)
			priv->cqm_beacon_loss_count =
					info->cqm_beacon_miss_thold;
			priv->cqm_tx_failure_thold =
					info->cqm_tx_fail_thold;
			priv->cqm_tx_failure_count = 0;
			cancel_delayed_work_sync(&priv->bss_loss_work);
			cancel_delayed_work_sync(&priv->connection_loss_work);
#endif /* CONFIG_CW1200_USE_STE_EXTENSIONS */

			priv->bss_params.beaconLostCount =
					priv->cqm_beacon_loss_count ?
					priv->cqm_beacon_loss_count :
					priv->cqm_link_loss_count;

			priv->bss_params.aid = info->aid;

			if (dtim_interval < 1)
				dtim_interval = 1;
			if (dtim_interval < priv->join_dtim_period)
				dtim_interval = priv->join_dtim_period;
			if (listen_interval < dtim_interval)
				listen_interval = 0;

			ap_printk(KERN_DEBUG "[STA] DTIM %d, listen %d\n",
				dtim_interval, listen_interval);
			ap_printk(KERN_DEBUG "[STA] Preamble: %d, " \
				"Greenfield: %d, Aid: %d, " \
				"Rates: 0x%.8X, Basic: 0x%.8X\n",
				priv->association_mode.preambleType,
				priv->association_mode.greenfieldMode,
				priv->bss_params.aid,
				priv->bss_params.operationalRateSet,
				priv->association_mode.basicRateSet);
			WARN_ON(wsm_set_association_mode(priv,
				&priv->association_mode));
			WARN_ON(wsm_set_bss_params(priv, &priv->bss_params));
			priv->setbssparams_done = true;
			WARN_ON(wsm_set_beacon_wakeup_period(priv,
				dtim_interval, listen_interval));
			cw1200_set_pm(priv, &priv->powersave_mode);

			if (priv->is_BT_Present)
				WARN_ON(cw1200_set_btcoexinfo(priv));
#if 0
			/* It's better to override internal TX rete; otherwise
			 * device sends RTS at too high rate. However device
			 * can't receive CTS at 1 and 2 Mbps. Well, 5.5 is a
			 * good choice for RTS/CTS, but that means PS poll
			 * will be sent at the same rate - impact on link
			 * budget. Not sure what is better.. */

			/* Update: internal rate selection algorythm is not
			 * bad: if device is not receiving CTS at high rate,
			 * it drops RTS rate.
			 * So, conclusion: if-0 the code. Keep code just for
			 * information:
			 * Do not touch WSM_MIB_ID_OVERRIDE_INTERNAL_TX_RATE! */

			/* ~3 is a bug in device: RTS/CTS is not working at
			 * low rates */

			__le32 internal_tx_rate = __cpu_to_le32(__ffs(
				priv->association_mode.basicRateSet & ~3));
			WARN_ON(wsm_write_mib(priv,
				WSM_MIB_ID_OVERRIDE_INTERNAL_TX_RATE,
				&internal_tx_rate,
				sizeof(internal_tx_rate)));
#endif
		} else {
			memset(&priv->association_mode, 0,
				sizeof(priv->association_mode));
			memset(&priv->bss_params, 0, sizeof(priv->bss_params));
		}
	}
	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_ERP_CTS_PROT)) {
		__le32 use_cts_prot = info->use_cts_prot ?
			__cpu_to_le32(1) : 0;

		ap_printk(KERN_DEBUG "[STA] CTS protection %d\n",
			info->use_cts_prot);
		WARN_ON(wsm_write_mib(priv, WSM_MIB_ID_NON_ERP_PROTECTION,
			&use_cts_prot, sizeof(use_cts_prot)));
	}
	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_ERP_SLOT)) {
		__le32 slot_time = info->use_short_slot ?
			__cpu_to_le32(9) : __cpu_to_le32(20);
		ap_printk(KERN_DEBUG "[STA] Slot time :%d us.\n",
			__le32_to_cpu(slot_time));
		WARN_ON(wsm_write_mib(priv, WSM_MIB_ID_DOT11_SLOT_TIME,
			&slot_time, sizeof(slot_time)));
	}
	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_CQM)) {
		struct wsm_rcpi_rssi_threshold threshold = {
			.rssiRcpiMode = WSM_RCPI_RSSI_USE_RSSI,
			.rollingAverageCount = 1,
		};

#if 0
		/* For verification purposes */
		info->cqm_rssi_thold = -50;
		info->cqm_rssi_hyst = 4;
#endif /* 0 */

		ap_printk(KERN_DEBUG "[CQM] RSSI threshold "
			"subscribe: %d +- %d\n",
			info->cqm_rssi_thold, info->cqm_rssi_hyst);
#if defined(CONFIG_CW1200_USE_STE_EXTENSIONS)
		ap_printk(KERN_DEBUG "[CQM] Beacon loss subscribe: %d\n",
			info->cqm_beacon_miss_thold);
		ap_printk(KERN_DEBUG "[CQM] TX failure subscribe: %d\n",
			info->cqm_tx_fail_thold);
		priv->cqm_rssi_thold = info->cqm_rssi_thold;
		priv->cqm_rssi_hyst = info->cqm_rssi_hyst;
#endif /* CONFIG_CW1200_USE_STE_EXTENSIONS */
		if (info->cqm_rssi_thold || info->cqm_rssi_hyst) {
			/* RSSI subscription enabled */
			/* TODO: It's not a correct way of setting threshold.
			 * Upper and lower must be set equal here and adjusted
			 * in callback. However current implementation is much
			 * more relaible and stable. */
			threshold.upperThreshold =
				info->cqm_rssi_thold + info->cqm_rssi_hyst;
			threshold.lowerThreshold =
				info->cqm_rssi_thold;
			threshold.rssiRcpiMode |=
				WSM_RCPI_RSSI_THRESHOLD_ENABLE;
		} else {
			/* There is a bug in FW, see sta.c. We have to enable
			 * dummy subscription to get correct RSSI values. */
			threshold.rssiRcpiMode |=
				WSM_RCPI_RSSI_THRESHOLD_ENABLE |
				WSM_RCPI_RSSI_DONT_USE_UPPER |
				WSM_RCPI_RSSI_DONT_USE_LOWER;
		}
		WARN_ON(wsm_set_rcpi_rssi_threshold(priv, &threshold));

#if defined(CONFIG_CW1200_USE_STE_EXTENSIONS)
		priv->cqm_tx_failure_thold = info->cqm_tx_fail_thold;
		priv->cqm_tx_failure_count = 0;

		if (priv->cqm_beacon_loss_count !=
				info->cqm_beacon_miss_thold) {
			priv->cqm_beacon_loss_count =
				info->cqm_beacon_miss_thold;
			priv->bss_params.beaconLostCount =
				priv->cqm_beacon_loss_count ?
				priv->cqm_beacon_loss_count :
				priv->cqm_link_loss_count;
			WARN_ON(wsm_set_bss_params(priv, &priv->bss_params));
			priv->setbssparams_done = true;
		}
#endif /* CONFIG_CW1200_USE_STE_EXTENSIONS */
	}
	mutex_unlock(&priv->conf_mutex);
}

void cw1200_multicast_start_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, multicast_start_work);
	long tmo = priv->join_dtim_period *
			(priv->beacon_int + 20) * HZ / 1024;

	if (!priv->aid0_bit_set) {
		wsm_lock_tx(priv);
		cw1200_set_tim_impl(priv, true);
		priv->aid0_bit_set = true;
		mod_timer(&priv->mcast_timeout, tmo);
		wsm_unlock_tx(priv);
	}
}

void cw1200_multicast_stop_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, multicast_stop_work);

	if (priv->aid0_bit_set) {
		wsm_lock_tx(priv);
		priv->aid0_bit_set = false;
		cw1200_set_tim_impl(priv, false);
		wsm_unlock_tx(priv);
	}
}

void cw1200_mcast_timeout(unsigned long arg)
{
	struct cw1200_common *priv =
		(struct cw1200_common *)arg;

	spin_lock_bh(&priv->ps_state_lock);
	priv->tx_multicast = priv->aid0_bit_set &&
			priv->buffered_multicasts;
	if (priv->tx_multicast)
		cw1200_bh_wakeup(priv);
	spin_unlock_bh(&priv->ps_state_lock);
}

int cw1200_ampdu_action(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			enum ieee80211_ampdu_mlme_action action,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn,
			u8 buf_size)
{
	/* Aggregation is implemented fully in firmware,
	 * including block ack negotiation. Do not allow
	 * mac80211 stack to do anything: it interferes with
	 * the firmware. */
	return -ENOTSUPP;
}

/* ******************************************************************** */
/* WSM callback								*/
void cw1200_suspend_resume(struct cw1200_common *priv,
			  struct wsm_suspend_resume *arg)
{
	/* if () is intendend to protect against spam. FW sends
	 * "start multicast" request on every DTIM. */
	if (arg->stop || !arg->multicast || priv->buffered_multicasts)
		ap_printk(KERN_DEBUG "[AP] %s: %s\n",
				arg->stop ? "stop" : "start",
				arg->multicast ? "broadcast" : "unicast");

	if (arg->multicast) {
		bool cancel_tmo = false;
		spin_lock_bh(&priv->ps_state_lock);
		if (arg->stop) {
			priv->tx_multicast = false;
		} else {
			/* Firmware sends this indication every DTIM if there
			 * is a STA in powersave connected. There is no reason
			 * to suspend, following wakeup will consume much more
			 * power than it could be saved. */
			cw1200_pm_stay_awake(&priv->pm_state,
					priv->join_dtim_period *
					(priv->beacon_int + 20) * HZ / 1024);
			priv->tx_multicast = priv->aid0_bit_set &&
					priv->buffered_multicasts;
			if (priv->tx_multicast) {
				cancel_tmo = true;
				cw1200_bh_wakeup(priv);
			}
		}
		spin_unlock_bh(&priv->ps_state_lock);
		if (cancel_tmo)
			del_timer_sync(&priv->mcast_timeout);
	} else {
		spin_lock_bh(&priv->ps_state_lock);
		cw1200_ps_notify(priv, arg->link_id, arg->stop);
		spin_unlock_bh(&priv->ps_state_lock);
		if (!arg->stop)
			cw1200_bh_wakeup(priv);
	}
	return;
}

/* ******************************************************************** */
/* AP privates								*/

static int cw1200_upload_beacon(struct cw1200_common *priv)
{
	int ret = 0;
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_BEACON,
	};


	frame.skb = ieee80211_beacon_get(priv->hw, priv->vif);
	if (WARN_ON(!frame.skb))
		return -ENOMEM;

	ret = wsm_set_template_frame(priv, &frame);
	if (!ret) {
		/* TODO: Distille probe resp; remove TIM
		 * and other beacon-specific IEs */
		*(__le16 *)frame.skb->data =
			__cpu_to_le16(IEEE80211_FTYPE_MGMT |
				      IEEE80211_STYPE_PROBE_RESP);
		frame.frame_type = WSM_FRAME_TYPE_PROBE_RESPONSE;
		ret = wsm_set_template_frame(priv, &frame);
	}
	dev_kfree_skb(frame.skb);

	return ret;
}

static int cw1200_upload_pspoll(struct cw1200_common *priv)
{
	int ret = 0;
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PS_POLL,
		.rate = 0xFF,
	};


	frame.skb = ieee80211_pspoll_get(priv->hw, priv->vif);
	if (WARN_ON(!frame.skb))
		return -ENOMEM;

	ret = wsm_set_template_frame(priv, &frame);

	dev_kfree_skb(frame.skb);

	return ret;
}

static int cw1200_upload_null(struct cw1200_common *priv)
{
	int ret = 0;
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_NULL,
		.rate = 0xFF,
	};


	frame.skb = ieee80211_nullfunc_get(priv->hw, priv->vif);
	if (WARN_ON(!frame.skb))
		return -ENOMEM;

	ret = wsm_set_template_frame(priv, &frame);

	dev_kfree_skb(frame.skb);

	return ret;
}

static int cw1200_enable_beaconing(struct cw1200_common *priv,
				   bool enable)
{
	struct wsm_beacon_transmit transmit = {
		.enableBeaconing = enable,
	};

	return wsm_beacon_transmit(priv, &transmit);
}

static int cw1200_start_ap(struct cw1200_common *priv)
{
	int ret;
	const u8 *ssidie;
	struct sk_buff *skb;
	int offset;
	struct ieee80211_bss_conf *conf = &priv->vif->bss_conf;
	struct wsm_start start = {
		.mode = priv->vif->p2p ?
				WSM_START_MODE_P2P_GO : WSM_START_MODE_AP,
		.band = (priv->channel->band == IEEE80211_BAND_5GHZ) ?
				WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G,
		.channelNumber = priv->channel->hw_value,
		.beaconInterval = conf->beacon_int,
		.DTIMPeriod = conf->dtim_period,
		.preambleType = conf->use_short_preamble ?
				WSM_JOIN_PREAMBLE_SHORT :
				WSM_JOIN_PREAMBLE_LONG,
		.probeDelay = 100,
		.basicRateSet = cw1200_rate_mask_to_wsm(priv,
				conf->basic_rates),
	};

	/* Get SSID */
	skb = ieee80211_beacon_get(priv->hw, priv->vif);
	if (WARN_ON(!skb))
		return -ENOMEM;

	offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	ssidie = cfg80211_find_ie(WLAN_EID_SSID, skb->data + offset,
				  skb->len - offset);

	memset(priv->ssid, 0, sizeof(priv->ssid));
	if (ssidie) {
		priv->ssid_length = ssidie[1];
		if (WARN_ON(priv->ssid_length > sizeof(priv->ssid)))
			priv->ssid_length = sizeof(priv->ssid);
		memcpy(priv->ssid, &ssidie[2], priv->ssid_length);
	} else {
		priv->ssid_length = 0;
	}
	dev_kfree_skb(skb);

	priv->beacon_int = conf->beacon_int;
	priv->join_dtim_period = conf->dtim_period;

	start.ssidLength = priv->ssid_length;
	memcpy(&start.ssid[0], priv->ssid, start.ssidLength);

	memset(&priv->link_id_db, 0, sizeof(priv->link_id_db));

	ap_printk(KERN_DEBUG "[AP] ch: %d(%d), bcn: %d(%d), "
		"brt: 0x%.8X, ssid: %.*s.\n",
		start.channelNumber, start.band,
		start.beaconInterval, start.DTIMPeriod,
		start.basicRateSet,
		start.ssidLength, start.ssid);
	ret = WARN_ON(wsm_start(priv, &start));
	if (!ret)
		ret = WARN_ON(cw1200_upload_keys(priv));
	if (!ret) {
		WARN_ON(wsm_set_block_ack_policy(priv,
			0, 0));
		priv->join_status = CW1200_JOIN_STATUS_AP;
		cw1200_update_filtering(priv);
	}
	return ret;
}

static int cw1200_update_beaconing(struct cw1200_common *priv)
{
	struct ieee80211_bss_conf *conf = &priv->vif->bss_conf;
	struct wsm_reset reset = {
		.link_id = 0,
		.reset_statistics = true,
	};

	if (priv->mode == NL80211_IFTYPE_AP) {
		/* TODO: check if changed channel, band */
		if (priv->join_status != CW1200_JOIN_STATUS_AP ||
		    priv->beacon_int != conf->beacon_int) {
			ap_printk(KERN_DEBUG "ap restarting\n");
			wsm_lock_tx(priv);
			if (priv->join_status != CW1200_JOIN_STATUS_PASSIVE)
				WARN_ON(wsm_reset(priv, &reset));
			priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
			WARN_ON(cw1200_start_ap(priv));
			wsm_unlock_tx(priv);
		} else
			ap_printk(KERN_DEBUG "ap started join_status: %d\n",
				  priv->join_status);
	}
	return 0;
}

int cw1200_find_link_id(struct cw1200_common *priv, const u8 *mac)
{
	int i, ret = 0;
	spin_lock_bh(&priv->ps_state_lock);
	for (i = 0; i < CW1200_MAX_STA_IN_AP_MODE; ++i) {
		if (!memcmp(mac, priv->link_id_db[i].mac, ETH_ALEN) &&
				priv->link_id_db[i].status) {
			priv->link_id_db[i].timestamp = jiffies;
			ret = i + 1;
			break;
		}
	}
	spin_unlock_bh(&priv->ps_state_lock);
	return ret;
}

int cw1200_alloc_link_id(struct cw1200_common *priv, const u8 *mac)
{
	int i, ret = 0;
	unsigned long max_inactivity = 0;
	unsigned long now = jiffies;

	spin_lock_bh(&priv->ps_state_lock);
	for (i = 0; i < CW1200_MAX_STA_IN_AP_MODE; ++i) {
		if (!priv->link_id_db[i].status) {
			ret = i + 1;
			break;
		} else if (priv->link_id_db[i].status != CW1200_LINK_HARD &&
			!priv->tx_queue_stats.link_map_cache[i + 1]) {

			unsigned long inactivity =
					now - priv->link_id_db[i].timestamp;
			if (inactivity < max_inactivity)
				continue;
			max_inactivity = inactivity;
			ret = i + 1;
		}
	}
	if (ret) {
		struct cw1200_link_entry *entry = &priv->link_id_db[ret - 1];
		ap_printk(KERN_DEBUG "[AP] STA added, link_id: %d\n",
			ret);
		entry->status = CW1200_LINK_RESERVE;
		memcpy(&entry->mac, mac, ETH_ALEN);
		memset(&entry->buffered, 0, CW1200_MAX_TID);
		skb_queue_head_init(&entry->rx_queue);
		wsm_lock_tx_async(priv);
		if (queue_work(priv->workqueue, &priv->link_id_work) <= 0)
			wsm_unlock_tx(priv);
	} else {
		wiphy_info(priv->hw->wiphy,
			"[AP] Early: no more link IDs available.\n");
	}

	spin_unlock_bh(&priv->ps_state_lock);
	return ret;
}

void cw1200_link_id_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, link_id_work);
	wsm_flush_tx(priv);
	cw1200_link_id_gc_work(&priv->link_id_gc_work.work);
	wsm_unlock_tx(priv);
}

void cw1200_link_id_gc_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, link_id_gc_work.work);
	struct wsm_reset reset = {
		.reset_statistics = false,
	};
	struct wsm_map_link map_link = {
		.link_id = 0,
	};
	unsigned long now = jiffies;
	unsigned long next_gc = -1;
	long ttl;
	bool need_reset;
	u32 mask;
	int i;

	if (priv->join_status != CW1200_JOIN_STATUS_AP)
		return;

	wsm_lock_tx(priv);
	spin_lock_bh(&priv->ps_state_lock);
	for (i = 0; i < CW1200_MAX_STA_IN_AP_MODE; ++i) {
		need_reset = false;
		mask = BIT(i + 1);
		if (priv->link_id_db[i].status == CW1200_LINK_RESERVE ||
			(priv->link_id_db[i].status == CW1200_LINK_HARD &&
			 !(priv->link_id_map & mask))) {
			if (priv->link_id_map & mask) {
				priv->sta_asleep_mask &= ~mask;
				priv->pspoll_mask &= ~mask;
				need_reset = true;
			}
			priv->link_id_map |= mask;
			if (priv->link_id_db[i].status != CW1200_LINK_HARD)
				priv->link_id_db[i].status = CW1200_LINK_SOFT;
			memcpy(map_link.mac_addr, priv->link_id_db[i].mac,
					ETH_ALEN);
			spin_unlock_bh(&priv->ps_state_lock);
			if (need_reset) {
				reset.link_id = i + 1;
				WARN_ON(wsm_reset(priv, &reset));
			}
			map_link.link_id = i + 1;
			WARN_ON(wsm_map_link(priv, &map_link));
			next_gc = min(next_gc, CW1200_LINK_ID_GC_TIMEOUT);
			spin_lock_bh(&priv->ps_state_lock);
		} else if (priv->link_id_db[i].status == CW1200_LINK_SOFT) {
			ttl = priv->link_id_db[i].timestamp - now +
					CW1200_LINK_ID_GC_TIMEOUT;
			if (ttl <= 0) {
				need_reset = true;
				priv->link_id_db[i].status = CW1200_LINK_OFF;
				priv->link_id_map &= ~mask;
				priv->sta_asleep_mask &= ~mask;
				priv->pspoll_mask &= ~mask;
				memset(map_link.mac_addr, 0, ETH_ALEN);
				spin_unlock_bh(&priv->ps_state_lock);
				reset.link_id = i + 1;
				WARN_ON(wsm_reset(priv, &reset));
				spin_lock_bh(&priv->ps_state_lock);
			} else {
				next_gc = min(next_gc, (unsigned long)ttl);
			}
		}
		if (need_reset) {
			skb_queue_purge(&priv->link_id_db[i].rx_queue);
			ap_printk(KERN_DEBUG "[AP] STA removed, link_id: %d\n",
					reset.link_id);
		}
	}
	spin_unlock_bh(&priv->ps_state_lock);
	if (next_gc != -1)
		queue_delayed_work(priv->workqueue,
				&priv->link_id_gc_work, next_gc);
	wsm_unlock_tx(priv);
}

