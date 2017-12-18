/*******************************************************************************
 * Filename:  target_core_fabric_lib.c
 *
 * This file contains generic high level protocol identifier and PR
 * handlers for TCM fabric modules
 *
 * (c) Copyright 2010-2013 Datera, Inc.
 *
 * Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ******************************************************************************/

/*
 * See SPC4, section 7.5 "Protocol specific parameters" for details
 * on the formats implemented in this file.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <linux/export.h>

#include <scsi/scsi_proto.h>

#include <target/target_core_base.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_pr.h"


static int sas_get_pr_transport_id(
	struct se_node_acl *nacl,
	int *format_code,
	unsigned char *buf)
{
	int ret;

	/* Skip over 'naa. prefix */
	ret = hex2bin(&buf[4], &nacl->initiatorname[4], 8);
	if (ret) {
		pr_debug("%s: invalid hex string\n", __func__);
		return ret;
	}

	return 24;
}

static int fc_get_pr_transport_id(
	struct se_node_acl *se_nacl,
	int *format_code,
	unsigned char *buf)
{
	unsigned char *ptr;
	int i, ret;
	u32 off = 8;

	/*
	 * We convert the ASCII formatted N Port name into a binary
	 * encoded TransportID.
	 */
	ptr = &se_nacl->initiatorname[0];
	for (i = 0; i < 24; ) {
		if (!strncmp(&ptr[i], ":", 1)) {
			i++;
			continue;
		}
		ret = hex2bin(&buf[off++], &ptr[i], 1);
		if (ret < 0) {
			pr_debug("%s: invalid hex string\n", __func__);
			return ret;
		}
		i += 2;
	}
	/*
	 * The FC Transport ID is a hardcoded 24-byte length
	 */
	return 24;
}

static int sbp_get_pr_transport_id(
	struct se_node_acl *nacl,
	int *format_code,
	unsigned char *buf)
{
	int ret;

	ret = hex2bin(&buf[8], nacl->initiatorname, 8);
	if (ret) {
		pr_debug("%s: invalid hex string\n", __func__);
		return ret;
	}

	return 24;
}

static int srp_get_pr_transport_id(
	struct se_node_acl *nacl,
	int *format_code,
	unsigned char *buf)
{
	const char *p;
	unsigned len, count, leading_zero_bytes;
	int rc;

	p = nacl->initiatorname;
	if (strncasecmp(p, "0x", 2) == 0)
		p += 2;
	len = strlen(p);
	if (len % 2)
		return -EINVAL;

	count = min(len / 2, 16U);
	leading_zero_bytes = 16 - count;
	memset(buf + 8, 0, leading_zero_bytes);
	rc = hex2bin(buf + 8 + leading_zero_bytes, p, count);
	if (rc < 0) {
		pr_debug("hex2bin failed for %s: %d\n", __func__, rc);
		return rc;
	}

	return 24;
}

static int iscsi_get_pr_transport_id(
	struct se_node_acl *se_nacl,
	struct t10_pr_registration *pr_reg,
	int *format_code,
	unsigned char *buf)
{
	u32 off = 4, padding = 0;
	u16 len = 0;

	spin_lock_irq(&se_nacl->nacl_sess_lock);
	/*
	 * From spc4r17 Section 7.5.4.6: TransportID for initiator
	 * ports using SCSI over iSCSI.
	 *
	 * The null-terminated, null-padded (see 4.4.2) ISCSI NAME field
	 * shall contain the iSCSI name of an iSCSI initiator node (see
	 * RFC 3720). The first ISCSI NAME field byte containing an ASCII
	 * null character terminates the ISCSI NAME field without regard for
	 * the specified length of the iSCSI TransportID or the contents of
	 * the ADDITIONAL LENGTH field.
	 */
	len = sprintf(&buf[off], "%s", se_nacl->initiatorname);
	/*
	 * Add Extra byte for NULL terminator
	 */
	len++;
	/*
	 * If there is ISID present with the registration and *format code == 1
	 * 1, use iSCSI Initiator port TransportID format.
	 *
	 * Otherwise use iSCSI Initiator device TransportID format that
	 * does not contain the ASCII encoded iSCSI Initiator iSID value
	 * provied by the iSCSi Initiator during the iSCSI login process.
	 */
	if ((*format_code == 1) && (pr_reg->isid_present_at_reg)) {
		/*
		 * Set FORMAT CODE 01b for iSCSI Initiator port TransportID
		 * format.
		 */
		buf[0] |= 0x40;
		/*
		 * From spc4r17 Section 7.5.4.6: TransportID for initiator
		 * ports using SCSI over iSCSI.  Table 390
		 *
		 * The SEPARATOR field shall contain the five ASCII
		 * characters ",i,0x".
		 *
		 * The null-terminated, null-padded ISCSI INITIATOR SESSION ID
		 * field shall contain the iSCSI initiator session identifier
		 * (see RFC 3720) in the form of ASCII characters that are the
		 * hexadecimal digits converted from the binary iSCSI initiator
		 * session identifier value. The first ISCSI INITIATOR SESSION
		 * ID field byte containing an ASCII null character
		 */
		buf[off+len] = 0x2c; off++; /* ASCII Character: "," */
		buf[off+len] = 0x69; off++; /* ASCII Character: "i" */
		buf[off+len] = 0x2c; off++; /* ASCII Character: "," */
		buf[off+len] = 0x30; off++; /* ASCII Character: "0" */
		buf[off+len] = 0x78; off++; /* ASCII Character: "x" */
		len += 5;
		buf[off+len] = pr_reg->pr_reg_isid[0]; off++;
		buf[off+len] = pr_reg->pr_reg_isid[1]; off++;
		buf[off+len] = pr_reg->pr_reg_isid[2]; off++;
		buf[off+len] = pr_reg->pr_reg_isid[3]; off++;
		buf[off+len] = pr_reg->pr_reg_isid[4]; off++;
		buf[off+len] = pr_reg->pr_reg_isid[5]; off++;
		buf[off+len] = '\0'; off++;
		len += 7;
	}
	spin_unlock_irq(&se_nacl->nacl_sess_lock);
	/*
	 * The ADDITIONAL LENGTH field specifies the number of bytes that follow
	 * in the TransportID. The additional length shall be at least 20 and
	 * shall be a multiple of four.
	*/
	padding = ((-len) & 3);
	if (padding != 0)
		len += padding;

	buf[2] = ((len >> 8) & 0xff);
	buf[3] = (len & 0xff);
	/*
	 * Increment value for total payload + header length for
	 * full status descriptor
	 */
	len += 4;

	return len;
}

static int iscsi_get_pr_transport_id_len(
	struct se_node_acl *se_nacl,
	struct t10_pr_registration *pr_reg,
	int *format_code)
{
	u32 len = 0, padding = 0;

	spin_lock_irq(&se_nacl->nacl_sess_lock);
	len = strlen(se_nacl->initiatorname);
	/*
	 * Add extra byte for NULL terminator
	 */
	len++;
	/*
	 * If there is ISID present with the registration, use format code:
	 * 01b: iSCSI Initiator port TransportID format
	 *
	 * If there is not an active iSCSI session, use format code:
	 * 00b: iSCSI Initiator device TransportID format
	 */
	if (pr_reg->isid_present_at_reg) {
		len += 5; /* For ",i,0x" ASCII separator */
		len += 7; /* For iSCSI Initiator Session ID + Null terminator */
		*format_code = 1;
	} else
		*format_code = 0;
	spin_unlock_irq(&se_nacl->nacl_sess_lock);
	/*
	 * The ADDITIONAL LENGTH field specifies the number of bytes that follow
	 * in the TransportID. The additional length shall be at least 20 and
	 * shall be a multiple of four.
	 */
	padding = ((-len) & 3);
	if (padding != 0)
		len += padding;
	/*
	 * Increment value for total payload + header length for
	 * full status descriptor
	 */
	len += 4;

	return len;
}

static char *iscsi_parse_pr_out_transport_id(
	struct se_portal_group *se_tpg,
	const char *buf,
	u32 *out_tid_len,
	char **port_nexus_ptr)
{
	char *p;
	u32 tid_len, padding;
	int i;
	u16 add_len;
	u8 format_code = (buf[0] & 0xc0);
	/*
	 * Check for FORMAT CODE 00b or 01b from spc4r17, section 7.5.4.6:
	 *
	 *       TransportID for initiator ports using SCSI over iSCSI,
	 *       from Table 388 -- iSCSI TransportID formats.
	 *
	 *    00b     Initiator port is identified using the world wide unique
	 *            SCSI device name of the iSCSI initiator
	 *            device containing the initiator port (see table 389).
	 *    01b     Initiator port is identified using the world wide unique
	 *            initiator port identifier (see table 390).10b to 11b
	 *            Reserved
	 */
	if ((format_code != 0x00) && (format_code != 0x40)) {
		pr_err("Illegal format code: 0x%02x for iSCSI"
			" Initiator Transport ID\n", format_code);
		return NULL;
	}
	/*
	 * If the caller wants the TransportID Length, we set that value for the
	 * entire iSCSI Tarnsport ID now.
	 */
	if (out_tid_len) {
		/* The shift works thanks to integer promotion rules */
		add_len = (buf[2] << 8) | buf[3];

		tid_len = strlen(&buf[4]);
		tid_len += 4; /* Add four bytes for iSCSI Transport ID header */
		tid_len += 1; /* Add one byte for NULL terminator */
		padding = ((-tid_len) & 3);
		if (padding != 0)
			tid_len += padding;

		if ((add_len + 4) != tid_len) {
			pr_debug("LIO-Target Extracted add_len: %hu "
				"does not match calculated tid_len: %u,"
				" using tid_len instead\n", add_len+4, tid_len);
			*out_tid_len = tid_len;
		} else
			*out_tid_len = (add_len + 4);
	}
	/*
	 * Check for ',i,0x' separator between iSCSI Name and iSCSI Initiator
	 * Session ID as defined in Table 390 - iSCSI initiator port TransportID
	 * format.
	 */
	if (format_code == 0x40) {
		p = strstr(&buf[4], ",i,0x");
		if (!p) {
			pr_err("Unable to locate \",i,0x\" separator"
				" for Initiator port identifier: %s\n",
				&buf[4]);
			return NULL;
		}
		*p = '\0'; /* Terminate iSCSI Name */
		p += 5; /* Skip over ",i,0x" separator */

		*port_nexus_ptr = p;
		/*
		 * Go ahead and do the lower case conversion of the received
		 * 12 ASCII characters representing the ISID in the TransportID
		 * for comparison against the running iSCSI session's ISID from
		 * iscsi_target.c:lio_sess_get_initiator_sid()
		 */
		for (i = 0; i < 12; i++) {
			if (isdigit(*p)) {
				p++;
				continue;
			}
			*p = tolower(*p);
			p++;
		}
	}

	return (char *)&buf[4];
}

int target_get_pr_transport_id_len(struct se_node_acl *nacl,
		struct t10_pr_registration *pr_reg, int *format_code)
{
	switch (nacl->se_tpg->proto_id) {
	case SCSI_PROTOCOL_FCP:
	case SCSI_PROTOCOL_SBP:
	case SCSI_PROTOCOL_SRP:
	case SCSI_PROTOCOL_SAS:
		break;
	case SCSI_PROTOCOL_ISCSI:
		return iscsi_get_pr_transport_id_len(nacl, pr_reg, format_code);
	default:
		pr_err("Unknown proto_id: 0x%02x\n", nacl->se_tpg->proto_id);
		return -EINVAL;
	}

	/*
	 * Most transports use a fixed length 24 byte identifier.
	 */
	*format_code = 0;
	return 24;
}

int target_get_pr_transport_id(struct se_node_acl *nacl,
		struct t10_pr_registration *pr_reg, int *format_code,
		unsigned char *buf)
{
	switch (nacl->se_tpg->proto_id) {
	case SCSI_PROTOCOL_SAS:
		return sas_get_pr_transport_id(nacl, format_code, buf);
	case SCSI_PROTOCOL_SBP:
		return sbp_get_pr_transport_id(nacl, format_code, buf);
	case SCSI_PROTOCOL_SRP:
		return srp_get_pr_transport_id(nacl, format_code, buf);
	case SCSI_PROTOCOL_FCP:
		return fc_get_pr_transport_id(nacl, format_code, buf);
	case SCSI_PROTOCOL_ISCSI:
		return iscsi_get_pr_transport_id(nacl, pr_reg, format_code,
				buf);
	default:
		pr_err("Unknown proto_id: 0x%02x\n", nacl->se_tpg->proto_id);
		return -EINVAL;
	}
}

const char *target_parse_pr_out_transport_id(struct se_portal_group *tpg,
		const char *buf, u32 *out_tid_len, char **port_nexus_ptr)
{
	u32 offset;

	switch (tpg->proto_id) {
	case SCSI_PROTOCOL_SAS:
		/*
		 * Assume the FORMAT CODE 00b from spc4r17, 7.5.4.7 TransportID
		 * for initiator ports using SCSI over SAS Serial SCSI Protocol.
		 */
		offset = 4;
		break;
	case SCSI_PROTOCOL_SBP:
	case SCSI_PROTOCOL_SRP:
	case SCSI_PROTOCOL_FCP:
		offset = 8;
		break;
	case SCSI_PROTOCOL_ISCSI:
		return iscsi_parse_pr_out_transport_id(tpg, buf, out_tid_len,
					port_nexus_ptr);
	default:
		pr_err("Unknown proto_id: 0x%02x\n", tpg->proto_id);
		return NULL;
	}

	*port_nexus_ptr = NULL;
	*out_tid_len = 24;
	return buf + offset;
}
