/*
 *   Driver for KeyStream IEEE802.11 b/g wireless LAN cards.
 *
 *   Copyright (C) 2006-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 */

#ifndef _KS_WLAN_H
#define _KS_WLAN_H

#define WPS

#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/spinlock.h>	/* spinlock_t                                   */
#include <linux/sched.h>	/* wait_queue_head_t                            */
#include <linux/types.h>	/* pid_t                                        */
#include <linux/netdevice.h>	/* struct net_device_stats,  struct sk_buff     */
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <asm/atomic.h>	/* struct atmic_t                               */
#include <linux/timer.h>	/* struct timer_list */
#include <linux/string.h>
#include <linux/completion.h>	/* struct completion */
#include <linux/workqueue.h>

#include <asm/io.h>

#include "ks7010_sdio.h"

#ifdef KS_WLAN_DEBUG
#define DPRINTK(n, fmt, args...) \
                 if (KS_WLAN_DEBUG>(n)) printk(KERN_NOTICE "%s: "fmt, __FUNCTION__, ## args)
#else
#define DPRINTK(n, fmt, args...)
#endif

struct ks_wlan_parameter {
	uint8_t operation_mode;	/* Operation Mode */
	uint8_t channel;	/*  Channel */
	uint8_t tx_rate;	/*  Transmit Rate */
	struct {
		uint8_t size;
		uint8_t body[16];
	} rate_set;
	uint8_t bssid[ETH_ALEN];	/* BSSID */
	struct {
		uint8_t size;
		uint8_t body[32 + 1];
	} ssid;	/*  SSID */
	uint8_t preamble;	/*  Preamble */
	uint8_t powermgt;	/*  PowerManagementMode */
	uint32_t scan_type;	/*  AP List Scan Type */
#define BEACON_LOST_COUNT_MIN 0
#define BEACON_LOST_COUNT_MAX 65535
	uint32_t beacon_lost_count;	/*  Beacon Lost Count */
	uint32_t rts;	/*  RTS Threashold */
	uint32_t fragment;	/*  Fragmentation Threashold */
	uint32_t privacy_invoked;
	uint32_t wep_index;
	struct {
		uint8_t size;
		uint8_t val[13 * 2 + 1];
	} wep_key[4];
	uint16_t authenticate_type;
	uint16_t phy_type;	/* 11b/11g/11bg mode type */
	uint16_t cts_mode;	/* for 11g/11bg mode cts mode */
	uint16_t phy_info_timer;	/* phy information timer */
};

enum {
	DEVICE_STATE_OFF = 0,	/* this means hw_unavailable is != 0 */
	DEVICE_STATE_PREBOOT,	/* we are in a pre-boot state (empty RAM) */
	DEVICE_STATE_BOOT,	/* boot state (fw upload, run fw) */
	DEVICE_STATE_PREINIT,	/* pre-init state */
	DEVICE_STATE_INIT,	/* init state (restore MIB backup to device) */
	DEVICE_STATE_READY,	/* driver&device are in operational state */
	DEVICE_STATE_SLEEP	/* device in sleep mode */
};

/* SME flag */
#define SME_MODE_SET	    (1<<0)
#define SME_RTS             (1<<1)
#define SME_FRAG            (1<<2)
#define SME_WEP_FLAG        (1<<3)
#define SME_WEP_INDEX       (1<<4)
#define SME_WEP_VAL1        (1<<5)
#define SME_WEP_VAL2        (1<<6)
#define SME_WEP_VAL3        (1<<7)
#define SME_WEP_VAL4        (1<<8)
#define SME_WEP_VAL_MASK    (SME_WEP_VAL1|SME_WEP_VAL2|SME_WEP_VAL3|SME_WEP_VAL4)
#define SME_RSN             (1<<9)
#define SME_RSN_MULTICAST   (1<<10)
#define SME_RSN_UNICAST	    (1<<11)
#define SME_RSN_AUTH	    (1<<12)

#define SME_AP_SCAN         (1<<13)
#define SME_MULTICAST       (1<<14)

/* SME Event */
enum {
	SME_START,

	SME_MULTICAST_REQUEST,
	SME_MACADDRESS_SET_REQUEST,
	SME_BSS_SCAN_REQUEST,
	SME_SET_FLAG,
	SME_SET_TXKEY,
	SME_SET_KEY1,
	SME_SET_KEY2,
	SME_SET_KEY3,
	SME_SET_KEY4,
	SME_SET_PMK_TSC,
	SME_SET_GMK1_TSC,
	SME_SET_GMK2_TSC,
	SME_SET_GMK3_TSC,
	SME_SET_PMKSA,
	SME_POW_MNGMT_REQUEST,
	SME_PHY_INFO_REQUEST,
	SME_MIC_FAILURE_REQUEST,
	SME_GET_MAC_ADDRESS,
	SME_GET_PRODUCT_VERSION,
	SME_STOP_REQUEST,
	SME_RTS_THRESHOLD_REQUEST,
	SME_FRAGMENTATION_THRESHOLD_REQUEST,
	SME_WEP_INDEX_REQUEST,
	SME_WEP_KEY1_REQUEST,
	SME_WEP_KEY2_REQUEST,
	SME_WEP_KEY3_REQUEST,
	SME_WEP_KEY4_REQUEST,
	SME_WEP_FLAG_REQUEST,
	SME_RSN_UCAST_REQUEST,
	SME_RSN_MCAST_REQUEST,
	SME_RSN_AUTH_REQUEST,
	SME_RSN_ENABLED_REQUEST,
	SME_RSN_MODE_REQUEST,
#ifdef WPS
	SME_WPS_ENABLE_REQUEST,
	SME_WPS_PROBE_REQUEST,
#endif
	SME_SET_GAIN,
	SME_GET_GAIN,
	SME_SLEEP_REQUEST,
	SME_SET_REGION,
	SME_MODE_SET_REQUEST,
	SME_START_REQUEST,
	SME_GET_EEPROM_CKSUM,

	SME_MIC_FAILURE_CONFIRM,
	SME_START_CONFIRM,

	SME_MULTICAST_CONFIRM,
	SME_BSS_SCAN_CONFIRM,
	SME_GET_CURRENT_AP,
	SME_POW_MNGMT_CONFIRM,
	SME_PHY_INFO_CONFIRM,
	SME_STOP_CONFIRM,
	SME_RTS_THRESHOLD_CONFIRM,
	SME_FRAGMENTATION_THRESHOLD_CONFIRM,
	SME_WEP_INDEX_CONFIRM,
	SME_WEP_KEY1_CONFIRM,
	SME_WEP_KEY2_CONFIRM,
	SME_WEP_KEY3_CONFIRM,
	SME_WEP_KEY4_CONFIRM,
	SME_WEP_FLAG_CONFIRM,
	SME_RSN_UCAST_CONFIRM,
	SME_RSN_MCAST_CONFIRM,
	SME_RSN_AUTH_CONFIRM,
	SME_RSN_ENABLED_CONFIRM,
	SME_RSN_MODE_CONFIRM,
	SME_MODE_SET_CONFIRM,
	SME_SLEEP_CONFIRM,

	SME_RSN_SET_CONFIRM,
	SME_WEP_SET_CONFIRM,
	SME_TERMINATE,

	SME_EVENT_SIZE	/* end */
};

/* SME Status */
enum {
	SME_IDLE,
	SME_SETUP,
	SME_DISCONNECT,
	SME_CONNECT
};

#define	SME_EVENT_BUFF_SIZE	128

struct sme_info {
	int sme_status;
	int event_buff[SME_EVENT_BUFF_SIZE];
	unsigned int qhead;
	unsigned int qtail;
#ifdef KS_WLAN_DEBUG
	/* for debug */
	unsigned int max_event_count;
#endif
	spinlock_t sme_spin;
	unsigned long sme_flag;
};

struct hostt_t {
	int buff[SME_EVENT_BUFF_SIZE];
	unsigned int qhead;
	unsigned int qtail;
};

#define RSN_IE_BODY_MAX 64
struct rsn_ie_t {
	uint8_t id;	/* 0xdd = WPA or 0x30 = RSN */
	uint8_t size;	/* max ? 255 ? */
	uint8_t body[RSN_IE_BODY_MAX];
} __packed;

#ifdef WPS
#define WPS_IE_BODY_MAX 255
struct wps_ie_t {
	uint8_t id;	/* 221 'dd <len> 00 50 F2 04' */
	uint8_t size;	/* max ? 255 ? */
	uint8_t body[WPS_IE_BODY_MAX];
} __packed;
#endif /* WPS */

struct local_ap_t {
	uint8_t bssid[6];
	uint8_t rssi;
	uint8_t sq;
	struct {
		uint8_t size;
		uint8_t body[32];
		uint8_t ssid_pad;
	} ssid;
	struct {
		uint8_t size;
		uint8_t body[16];
		uint8_t rate_pad;
	} rate_set;
	uint16_t capability;
	uint8_t channel;
	uint8_t noise;
	struct rsn_ie_t wpa_ie;
	struct rsn_ie_t rsn_ie;
#ifdef WPS
	struct wps_ie_t wps_ie;
#endif /* WPS */
};

#define LOCAL_APLIST_MAX 31
#define LOCAL_CURRENT_AP LOCAL_APLIST_MAX
struct local_aplist_t {
	int size;
	struct local_ap_t ap[LOCAL_APLIST_MAX + 1];
};

struct local_gain_t {
	uint8_t TxMode;
	uint8_t RxMode;
	uint8_t TxGain;
	uint8_t RxGain;
};

struct local_eeprom_sum_t {
	uint8_t type;
	uint8_t result;
};

enum {
	EEPROM_OK,
	EEPROM_CHECKSUM_NONE,
	EEPROM_FW_NOT_SUPPORT,
	EEPROM_NG,
};

/* Power Save Status */
enum {
	PS_NONE,
	PS_ACTIVE_SET,
	PS_SAVE_SET,
	PS_CONF_WAIT,
	PS_SNOOZE,
	PS_WAKEUP
};

struct power_save_status_t {
	atomic_t status;	/* initialvalue 0 */
	struct completion wakeup_wait;
	atomic_t confirm_wait;
	atomic_t snooze_guard;
};

struct sleep_status_t {
	atomic_t status;	/* initialvalue 0 */
	atomic_t doze_request;
	atomic_t wakeup_request;
};

/* WPA */
struct scan_ext_t {
	unsigned int flag;
	char ssid[IW_ESSID_MAX_SIZE + 1];
};

enum {
	CIPHER_NONE,
	CIPHER_WEP40,
	CIPHER_TKIP,
	CIPHER_CCMP,
	CIPHER_WEP104
};

#define CIPHER_ID_WPA_NONE    "\x00\x50\xf2\x00"
#define CIPHER_ID_WPA_WEP40   "\x00\x50\xf2\x01"
#define CIPHER_ID_WPA_TKIP    "\x00\x50\xf2\x02"
#define CIPHER_ID_WPA_CCMP    "\x00\x50\xf2\x04"
#define CIPHER_ID_WPA_WEP104  "\x00\x50\xf2\x05"

#define CIPHER_ID_WPA2_NONE   "\x00\x0f\xac\x00"
#define CIPHER_ID_WPA2_WEP40  "\x00\x0f\xac\x01"
#define CIPHER_ID_WPA2_TKIP   "\x00\x0f\xac\x02"
#define CIPHER_ID_WPA2_CCMP   "\x00\x0f\xac\x04"
#define CIPHER_ID_WPA2_WEP104 "\x00\x0f\xac\x05"

#define CIPHER_ID_LEN    4

enum {
	KEY_MGMT_802_1X,
	KEY_MGMT_PSK,
	KEY_MGMT_WPANONE,
};

#define KEY_MGMT_ID_WPA_NONE     "\x00\x50\xf2\x00"
#define KEY_MGMT_ID_WPA_1X       "\x00\x50\xf2\x01"
#define KEY_MGMT_ID_WPA_PSK      "\x00\x50\xf2\x02"
#define KEY_MGMT_ID_WPA_WPANONE  "\x00\x50\xf2\xff"

#define KEY_MGMT_ID_WPA2_NONE    "\x00\x0f\xac\x00"
#define KEY_MGMT_ID_WPA2_1X      "\x00\x0f\xac\x01"
#define KEY_MGMT_ID_WPA2_PSK     "\x00\x0f\xac\x02"
#define KEY_MGMT_ID_WPA2_WPANONE "\x00\x0f\xac\xff"

#define KEY_MGMT_ID_LEN  4

#define MIC_KEY_SIZE 8

struct wpa_key_t {
	uint32_t ext_flags;	/* IW_ENCODE_EXT_xxx */
	uint8_t tx_seq[IW_ENCODE_SEQ_MAX_SIZE];	/* LSB first */
	uint8_t rx_seq[IW_ENCODE_SEQ_MAX_SIZE];	/* LSB first */
	struct sockaddr addr;	/* ff:ff:ff:ff:ff:ff for broadcast/multicast
				 * (group) keys or unicast address for
				 * individual keys */
	uint16_t alg;
	uint16_t key_len;	/* WEP: 5 or 13, TKIP: 32, CCMP: 16 */
	uint8_t key_val[IW_ENCODING_TOKEN_MAX];
	uint8_t tx_mic_key[MIC_KEY_SIZE];
	uint8_t rx_mic_key[MIC_KEY_SIZE];
};
#define WPA_KEY_INDEX_MAX 4
#define WPA_RX_SEQ_LEN 6

struct mic_failure_t {
	uint16_t failure;	/* MIC Failure counter 0 or 1 or 2 */
	uint16_t counter;	/* 1sec counter 0-60 */
	uint32_t last_failure_time;
	int stop;	/* stop flag */
};

struct wpa_status_t {
	int wpa_enabled;
	unsigned int rsn_enabled;
	int version;
	int pairwise_suite;	/* unicast cipher */
	int group_suite;	/* multicast cipher */
	int key_mgmt_suite;	/* authentication key management suite */
	int auth_alg;
	int txkey;
	struct wpa_key_t key[WPA_KEY_INDEX_MAX];
	struct scan_ext_t scan_ext;
	struct mic_failure_t mic_failure;
};

#include <linux/list.h>
#define PMK_LIST_MAX 8
struct pmk_list_t {
	uint16_t size;
	struct list_head head;
	struct pmk_t {
		struct list_head list;
		uint8_t bssid[ETH_ALEN];
		uint8_t pmkid[IW_PMKID_LEN];
	} pmk[PMK_LIST_MAX];
};

#ifdef WPS
struct wps_status_t {
	int wps_enabled;
	int ielen;
	uint8_t ie[255];
};
#endif /* WPS */

struct ks_wlan_private {

	struct hw_info_t ks_wlan_hw;	/* hardware information */

	struct net_device *net_dev;
	int reg_net;	/* register_netdev */
	struct net_device_stats nstats;
	struct iw_statistics wstats;

	struct completion confirm_wait;

	/* trx device & sme */
	struct tx_device tx_dev;
	struct rx_device rx_dev;
	struct sme_info sme_i;
	u8 *rxp;
	unsigned int rx_size;
	struct tasklet_struct sme_task;
	struct work_struct ks_wlan_wakeup_task;
	int scan_ind_count;

	unsigned char eth_addr[ETH_ALEN];

	struct local_aplist_t aplist;
	struct local_ap_t current_ap;
	struct power_save_status_t psstatus;
	struct sleep_status_t sleepstatus;
	struct wpa_status_t wpa;
	struct pmk_list_t pmklist;
	/* wireless parameter */
	struct ks_wlan_parameter reg;
	uint8_t current_rate;

	char nick[IW_ESSID_MAX_SIZE + 1];

	spinlock_t multicast_spin;

	spinlock_t dev_read_lock;
	wait_queue_head_t devread_wait;

	unsigned int need_commit;	/* for ioctl */

	/* DeviceIoControl */
	int device_open_status;
	atomic_t event_count;
	atomic_t rec_count;
	int dev_count;
#define DEVICE_STOCK_COUNT 20
	unsigned char *dev_data[DEVICE_STOCK_COUNT];
	int dev_size[DEVICE_STOCK_COUNT];

	/* ioctl : IOCTL_FIRMWARE_VERSION */
	unsigned char firmware_version[128 + 1];
	int version_size;

	int mac_address_valid;	/* Mac Address Status */

	int dev_state;

	struct sk_buff *skb;
	unsigned int cur_rx;	/* Index into the Rx buffer of next Rx pkt. */
	/* spinlock_t lock; */
#define FORCE_DISCONNECT    0x80000000
#define CONNECT_STATUS_MASK 0x7FFFFFFF
	uint32_t connect_status;	/* connect status */
	int infra_status;	/* Infractructure status */

	uint8_t data_buff[0x1000];

	uint8_t scan_ssid_len;
	uint8_t scan_ssid[IW_ESSID_MAX_SIZE + 1];
	struct local_gain_t gain;
#ifdef WPS
	struct net_device *l2_dev;
	int l2_fd;
	struct wps_status_t wps;
#endif /* WPS */
	uint8_t sleep_mode;

	uint8_t region;
	struct local_eeprom_sum_t eeprom_sum;
	uint8_t eeprom_checksum;

	struct hostt_t hostt;

	unsigned long last_doze;
	unsigned long last_wakeup;

	uint wakeup_count;	/* for detect wakeup loop */
};

int ks_wlan_net_start(struct net_device *dev);
int ks_wlan_net_stop(struct net_device *dev);

#endif /* _KS_WLAN_H */
