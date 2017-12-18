/*
 * rc-map.h - define RC map names used by RC drivers
 *
 * Copyright (c) 2010 by Mauro Carvalho Chehab
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/input.h>

/**
 * enum rc_type - type of the Remote Controller protocol
 *
 * @RC_TYPE_UNKNOWN: Protocol not known
 * @RC_TYPE_OTHER: Protocol known but proprietary
 * @RC_TYPE_RC5: Philips RC5 protocol
 * @RC_TYPE_RC5X: Philips RC5x protocol
 * @RC_TYPE_RC5_SZ: StreamZap variant of RC5
 * @RC_TYPE_JVC: JVC protocol
 * @RC_TYPE_SONY12: Sony 12 bit protocol
 * @RC_TYPE_SONY15: Sony 15 bit protocol
 * @RC_TYPE_SONY20: Sony 20 bit protocol
 * @RC_TYPE_NEC: NEC protocol
 * @RC_TYPE_NECX: Extended NEC protocol
 * @RC_TYPE_NEC32: NEC 32 bit protocol
 * @RC_TYPE_SANYO: Sanyo protocol
 * @RC_TYPE_MCE_KBD: RC6-ish MCE keyboard/mouse
 * @RC_TYPE_RC6_0: Philips RC6-0-16 protocol
 * @RC_TYPE_RC6_6A_20: Philips RC6-6A-20 protocol
 * @RC_TYPE_RC6_6A_24: Philips RC6-6A-24 protocol
 * @RC_TYPE_RC6_6A_32: Philips RC6-6A-32 protocol
 * @RC_TYPE_RC6_MCE: MCE (Philips RC6-6A-32 subtype) protocol
 * @RC_TYPE_SHARP: Sharp protocol
 * @RC_TYPE_XMP: XMP protocol
 * @RC_TYPE_CEC: CEC protocol
 */
enum rc_type {
	RC_TYPE_UNKNOWN		= 0,
	RC_TYPE_OTHER		= 1,
	RC_TYPE_RC5		= 2,
	RC_TYPE_RC5X		= 3,
	RC_TYPE_RC5_SZ		= 4,
	RC_TYPE_JVC		= 5,
	RC_TYPE_SONY12		= 6,
	RC_TYPE_SONY15		= 7,
	RC_TYPE_SONY20		= 8,
	RC_TYPE_NEC		= 9,
	RC_TYPE_NECX		= 10,
	RC_TYPE_NEC32		= 11,
	RC_TYPE_SANYO		= 12,
	RC_TYPE_MCE_KBD		= 13,
	RC_TYPE_RC6_0		= 14,
	RC_TYPE_RC6_6A_20	= 15,
	RC_TYPE_RC6_6A_24	= 16,
	RC_TYPE_RC6_6A_32	= 17,
	RC_TYPE_RC6_MCE		= 18,
	RC_TYPE_SHARP		= 19,
	RC_TYPE_XMP		= 20,
	RC_TYPE_CEC		= 21,
};

#define RC_BIT_NONE		0ULL
#define RC_BIT_UNKNOWN		(1ULL << RC_TYPE_UNKNOWN)
#define RC_BIT_OTHER		(1ULL << RC_TYPE_OTHER)
#define RC_BIT_RC5		(1ULL << RC_TYPE_RC5)
#define RC_BIT_RC5X		(1ULL << RC_TYPE_RC5X)
#define RC_BIT_RC5_SZ		(1ULL << RC_TYPE_RC5_SZ)
#define RC_BIT_JVC		(1ULL << RC_TYPE_JVC)
#define RC_BIT_SONY12		(1ULL << RC_TYPE_SONY12)
#define RC_BIT_SONY15		(1ULL << RC_TYPE_SONY15)
#define RC_BIT_SONY20		(1ULL << RC_TYPE_SONY20)
#define RC_BIT_NEC		(1ULL << RC_TYPE_NEC)
#define RC_BIT_NECX		(1ULL << RC_TYPE_NECX)
#define RC_BIT_NEC32		(1ULL << RC_TYPE_NEC32)
#define RC_BIT_SANYO		(1ULL << RC_TYPE_SANYO)
#define RC_BIT_MCE_KBD		(1ULL << RC_TYPE_MCE_KBD)
#define RC_BIT_RC6_0		(1ULL << RC_TYPE_RC6_0)
#define RC_BIT_RC6_6A_20	(1ULL << RC_TYPE_RC6_6A_20)
#define RC_BIT_RC6_6A_24	(1ULL << RC_TYPE_RC6_6A_24)
#define RC_BIT_RC6_6A_32	(1ULL << RC_TYPE_RC6_6A_32)
#define RC_BIT_RC6_MCE		(1ULL << RC_TYPE_RC6_MCE)
#define RC_BIT_SHARP		(1ULL << RC_TYPE_SHARP)
#define RC_BIT_XMP		(1ULL << RC_TYPE_XMP)
#define RC_BIT_CEC		(1ULL << RC_TYPE_CEC)

#define RC_BIT_ALL	(RC_BIT_UNKNOWN | RC_BIT_OTHER | \
			 RC_BIT_RC5 | RC_BIT_RC5X | RC_BIT_RC5_SZ | \
			 RC_BIT_JVC | \
			 RC_BIT_SONY12 | RC_BIT_SONY15 | RC_BIT_SONY20 | \
			 RC_BIT_NEC | RC_BIT_NECX | RC_BIT_NEC32 | \
			 RC_BIT_SANYO | RC_BIT_MCE_KBD | RC_BIT_RC6_0 | \
			 RC_BIT_RC6_6A_20 | RC_BIT_RC6_6A_24 | \
			 RC_BIT_RC6_6A_32 | RC_BIT_RC6_MCE | RC_BIT_SHARP | \
			 RC_BIT_XMP | RC_BIT_CEC)


#define RC_SCANCODE_UNKNOWN(x)			(x)
#define RC_SCANCODE_OTHER(x)			(x)
#define RC_SCANCODE_NEC(addr, cmd)		(((addr) << 8) | (cmd))
#define RC_SCANCODE_NECX(addr, cmd)		(((addr) << 8) | (cmd))
#define RC_SCANCODE_NEC32(data)			((data) & 0xffffffff)
#define RC_SCANCODE_RC5(sys, cmd)		(((sys) << 8) | (cmd))
#define RC_SCANCODE_RC5_SZ(sys, cmd)		(((sys) << 8) | (cmd))
#define RC_SCANCODE_RC6_0(sys, cmd)		(((sys) << 8) | (cmd))
#define RC_SCANCODE_RC6_6A(vendor, sys, cmd)	(((vendor) << 16) | ((sys) << 8) | (cmd))

/**
 * struct rc_map_table - represents a scancode/keycode pair
 *
 * @scancode: scan code (u32)
 * @keycode: Linux input keycode
 */
struct rc_map_table {
	u32	scancode;
	u32	keycode;
};

/**
 * struct rc_map - represents a keycode map table
 *
 * @scan: pointer to struct &rc_map_table
 * @size: Max number of entries
 * @len: Number of entries that are in use
 * @alloc: size of \*scan, in bytes
 * @rc_type: type of the remote controller protocol, as defined at
 *	     enum &rc_type
 * @name: name of the key map table
 * @lock: lock to protect access to this structure
 */
struct rc_map {
	struct rc_map_table	*scan;
	unsigned int		size;
	unsigned int		len;
	unsigned int		alloc;
	enum rc_type		rc_type;
	const char		*name;
	spinlock_t		lock;
};

/**
 * struct rc_map_list - list of the registered &rc_map maps
 *
 * @list: pointer to struct &list_head
 * @map: pointer to struct &rc_map
 */
struct rc_map_list {
	struct list_head	 list;
	struct rc_map map;
};

/* Routines from rc-map.c */

/**
 * rc_map_register() - Registers a Remote Controler scancode map
 *
 * @map:	pointer to struct rc_map_list
 */
int rc_map_register(struct rc_map_list *map);

/**
 * rc_map_unregister() - Unregisters a Remote Controler scancode map
 *
 * @map:	pointer to struct rc_map_list
 */
void rc_map_unregister(struct rc_map_list *map);

/**
 * rc_map_get - gets an RC map from its name
 * @name: name of the RC scancode map
 */
struct rc_map *rc_map_get(const char *name);

/* Names of the several keytables defined in-kernel */

#define RC_MAP_ADSTECH_DVB_T_PCI         "rc-adstech-dvb-t-pci"
#define RC_MAP_ALINK_DTU_M               "rc-alink-dtu-m"
#define RC_MAP_ANYSEE                    "rc-anysee"
#define RC_MAP_APAC_VIEWCOMP             "rc-apac-viewcomp"
#define RC_MAP_ASUS_PC39                 "rc-asus-pc39"
#define RC_MAP_ASUS_PS3_100              "rc-asus-ps3-100"
#define RC_MAP_ATI_TV_WONDER_HD_600      "rc-ati-tv-wonder-hd-600"
#define RC_MAP_ATI_X10                   "rc-ati-x10"
#define RC_MAP_AVERMEDIA_A16D            "rc-avermedia-a16d"
#define RC_MAP_AVERMEDIA_CARDBUS         "rc-avermedia-cardbus"
#define RC_MAP_AVERMEDIA_DVBT            "rc-avermedia-dvbt"
#define RC_MAP_AVERMEDIA_M135A           "rc-avermedia-m135a"
#define RC_MAP_AVERMEDIA_M733A_RM_K6     "rc-avermedia-m733a-rm-k6"
#define RC_MAP_AVERMEDIA_RM_KS           "rc-avermedia-rm-ks"
#define RC_MAP_AVERMEDIA                 "rc-avermedia"
#define RC_MAP_AVERTV_303                "rc-avertv-303"
#define RC_MAP_AZUREWAVE_AD_TU700        "rc-azurewave-ad-tu700"
#define RC_MAP_BEHOLD_COLUMBUS           "rc-behold-columbus"
#define RC_MAP_BEHOLD                    "rc-behold"
#define RC_MAP_BUDGET_CI_OLD             "rc-budget-ci-old"
#define RC_MAP_CEC                       "rc-cec"
#define RC_MAP_CINERGY_1400              "rc-cinergy-1400"
#define RC_MAP_CINERGY                   "rc-cinergy"
#define RC_MAP_DELOCK_61959              "rc-delock-61959"
#define RC_MAP_DIB0700_NEC_TABLE         "rc-dib0700-nec"
#define RC_MAP_DIB0700_RC5_TABLE         "rc-dib0700-rc5"
#define RC_MAP_DIGITALNOW_TINYTWIN       "rc-digitalnow-tinytwin"
#define RC_MAP_DIGITTRADE                "rc-digittrade"
#define RC_MAP_DM1105_NEC                "rc-dm1105-nec"
#define RC_MAP_DNTV_LIVE_DVBT_PRO        "rc-dntv-live-dvbt-pro"
#define RC_MAP_DNTV_LIVE_DVB_T           "rc-dntv-live-dvb-t"
#define RC_MAP_DTT200U                   "rc-dtt200u"
#define RC_MAP_DVBSKY                    "rc-dvbsky"
#define RC_MAP_EMPTY                     "rc-empty"
#define RC_MAP_EM_TERRATEC               "rc-em-terratec"
#define RC_MAP_ENCORE_ENLTV2             "rc-encore-enltv2"
#define RC_MAP_ENCORE_ENLTV_FM53         "rc-encore-enltv-fm53"
#define RC_MAP_ENCORE_ENLTV              "rc-encore-enltv"
#define RC_MAP_EVGA_INDTUBE              "rc-evga-indtube"
#define RC_MAP_EZTV                      "rc-eztv"
#define RC_MAP_FLYDVB                    "rc-flydvb"
#define RC_MAP_FLYVIDEO                  "rc-flyvideo"
#define RC_MAP_FUSIONHDTV_MCE            "rc-fusionhdtv-mce"
#define RC_MAP_GADMEI_RM008Z             "rc-gadmei-rm008z"
#define RC_MAP_GENIUS_TVGO_A11MCE        "rc-genius-tvgo-a11mce"
#define RC_MAP_GOTVIEW7135               "rc-gotview7135"
#define RC_MAP_HAUPPAUGE_NEW             "rc-hauppauge"
#define RC_MAP_IMON_MCE                  "rc-imon-mce"
#define RC_MAP_IMON_PAD                  "rc-imon-pad"
#define RC_MAP_IODATA_BCTV7E             "rc-iodata-bctv7e"
#define RC_MAP_IT913X_V1                 "rc-it913x-v1"
#define RC_MAP_IT913X_V2                 "rc-it913x-v2"
#define RC_MAP_KAIOMY                    "rc-kaiomy"
#define RC_MAP_KWORLD_315U               "rc-kworld-315u"
#define RC_MAP_KWORLD_PC150U             "rc-kworld-pc150u"
#define RC_MAP_KWORLD_PLUS_TV_ANALOG     "rc-kworld-plus-tv-analog"
#define RC_MAP_LEADTEK_Y04G0051          "rc-leadtek-y04g0051"
#define RC_MAP_LIRC                      "rc-lirc"
#define RC_MAP_LME2510                   "rc-lme2510"
#define RC_MAP_MANLI                     "rc-manli"
#define RC_MAP_MEDION_X10                "rc-medion-x10"
#define RC_MAP_MEDION_X10_DIGITAINER     "rc-medion-x10-digitainer"
#define RC_MAP_MEDION_X10_OR2X           "rc-medion-x10-or2x"
#define RC_MAP_MSI_DIGIVOX_II            "rc-msi-digivox-ii"
#define RC_MAP_MSI_DIGIVOX_III           "rc-msi-digivox-iii"
#define RC_MAP_MSI_TVANYWHERE_PLUS       "rc-msi-tvanywhere-plus"
#define RC_MAP_MSI_TVANYWHERE            "rc-msi-tvanywhere"
#define RC_MAP_NEBULA                    "rc-nebula"
#define RC_MAP_NEC_TERRATEC_CINERGY_XS   "rc-nec-terratec-cinergy-xs"
#define RC_MAP_NORWOOD                   "rc-norwood"
#define RC_MAP_NPGTECH                   "rc-npgtech"
#define RC_MAP_PCTV_SEDNA                "rc-pctv-sedna"
#define RC_MAP_PINNACLE_COLOR            "rc-pinnacle-color"
#define RC_MAP_PINNACLE_GREY             "rc-pinnacle-grey"
#define RC_MAP_PINNACLE_PCTV_HD          "rc-pinnacle-pctv-hd"
#define RC_MAP_PIXELVIEW_NEW             "rc-pixelview-new"
#define RC_MAP_PIXELVIEW                 "rc-pixelview"
#define RC_MAP_PIXELVIEW_002T		 "rc-pixelview-002t"
#define RC_MAP_PIXELVIEW_MK12            "rc-pixelview-mk12"
#define RC_MAP_POWERCOLOR_REAL_ANGEL     "rc-powercolor-real-angel"
#define RC_MAP_PROTEUS_2309              "rc-proteus-2309"
#define RC_MAP_PURPLETV                  "rc-purpletv"
#define RC_MAP_PV951                     "rc-pv951"
#define RC_MAP_HAUPPAUGE                 "rc-hauppauge"
#define RC_MAP_RC5_TV                    "rc-rc5-tv"
#define RC_MAP_RC6_MCE                   "rc-rc6-mce"
#define RC_MAP_REAL_AUDIO_220_32_KEYS    "rc-real-audio-220-32-keys"
#define RC_MAP_REDDO                     "rc-reddo"
#define RC_MAP_SNAPSTREAM_FIREFLY        "rc-snapstream-firefly"
#define RC_MAP_STREAMZAP                 "rc-streamzap"
#define RC_MAP_TBS_NEC                   "rc-tbs-nec"
#define RC_MAP_TECHNISAT_TS35            "rc-technisat-ts35"
#define RC_MAP_TECHNISAT_USB2            "rc-technisat-usb2"
#define RC_MAP_TERRATEC_CINERGY_C_PCI    "rc-terratec-cinergy-c-pci"
#define RC_MAP_TERRATEC_CINERGY_S2_HD    "rc-terratec-cinergy-s2-hd"
#define RC_MAP_TERRATEC_CINERGY_XS       "rc-terratec-cinergy-xs"
#define RC_MAP_TERRATEC_SLIM             "rc-terratec-slim"
#define RC_MAP_TERRATEC_SLIM_2           "rc-terratec-slim-2"
#define RC_MAP_TEVII_NEC                 "rc-tevii-nec"
#define RC_MAP_TIVO                      "rc-tivo"
#define RC_MAP_TOTAL_MEDIA_IN_HAND       "rc-total-media-in-hand"
#define RC_MAP_TOTAL_MEDIA_IN_HAND_02    "rc-total-media-in-hand-02"
#define RC_MAP_TREKSTOR                  "rc-trekstor"
#define RC_MAP_TT_1500                   "rc-tt-1500"
#define RC_MAP_TWINHAN_DTV_CAB_CI        "rc-twinhan-dtv-cab-ci"
#define RC_MAP_TWINHAN_VP1027_DVBS       "rc-twinhan1027"
#define RC_MAP_VIDEOMATE_K100            "rc-videomate-k100"
#define RC_MAP_VIDEOMATE_S350            "rc-videomate-s350"
#define RC_MAP_VIDEOMATE_TV_PVR          "rc-videomate-tv-pvr"
#define RC_MAP_WINFAST                   "rc-winfast"
#define RC_MAP_WINFAST_USBII_DELUXE      "rc-winfast-usbii-deluxe"
#define RC_MAP_SU3000                    "rc-su3000"

/*
 * Please, do not just append newer Remote Controller names at the end.
 * The names should be ordered in alphabetical order
 */
