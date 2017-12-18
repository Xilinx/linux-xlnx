/* Copyright (C) 2007-2016  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich, Antonio Quartulli
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _NET_BATMAN_ADV_TRANSLATION_TABLE_H_
#define _NET_BATMAN_ADV_TRANSLATION_TABLE_H_

#include "main.h"

#include <linux/types.h>

struct netlink_callback;
struct net_device;
struct seq_file;
struct sk_buff;

int batadv_tt_init(struct batadv_priv *bat_priv);
bool batadv_tt_local_add(struct net_device *soft_iface, const u8 *addr,
			 unsigned short vid, int ifindex, u32 mark);
u16 batadv_tt_local_remove(struct batadv_priv *bat_priv,
			   const u8 *addr, unsigned short vid,
			   const char *message, bool roaming);
int batadv_tt_local_seq_print_text(struct seq_file *seq, void *offset);
int batadv_tt_global_seq_print_text(struct seq_file *seq, void *offset);
int batadv_tt_local_dump(struct sk_buff *msg, struct netlink_callback *cb);
int batadv_tt_global_dump(struct sk_buff *msg, struct netlink_callback *cb);
void batadv_tt_global_del_orig(struct batadv_priv *bat_priv,
			       struct batadv_orig_node *orig_node,
			       s32 match_vid, const char *message);
int batadv_tt_global_hash_count(struct batadv_priv *bat_priv,
				const u8 *addr, unsigned short vid);
struct batadv_orig_node *batadv_transtable_search(struct batadv_priv *bat_priv,
						  const u8 *src, const u8 *addr,
						  unsigned short vid);
void batadv_tt_free(struct batadv_priv *bat_priv);
bool batadv_is_my_client(struct batadv_priv *bat_priv, const u8 *addr,
			 unsigned short vid);
bool batadv_is_ap_isolated(struct batadv_priv *bat_priv, u8 *src, u8 *dst,
			   unsigned short vid);
void batadv_tt_local_commit_changes(struct batadv_priv *bat_priv);
bool batadv_tt_global_client_is_roaming(struct batadv_priv *bat_priv,
					u8 *addr, unsigned short vid);
bool batadv_tt_local_client_is_roaming(struct batadv_priv *bat_priv,
				       u8 *addr, unsigned short vid);
void batadv_tt_local_resize_to_mtu(struct net_device *soft_iface);
bool batadv_tt_add_temporary_global_entry(struct batadv_priv *bat_priv,
					  struct batadv_orig_node *orig_node,
					  const unsigned char *addr,
					  unsigned short vid);
bool batadv_tt_global_is_isolated(struct batadv_priv *bat_priv,
				  const u8 *addr, unsigned short vid);

int batadv_tt_cache_init(void);
void batadv_tt_cache_destroy(void);

#endif /* _NET_BATMAN_ADV_TRANSLATION_TABLE_H_ */
