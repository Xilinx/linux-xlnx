/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifndef _I40E_DEVIDS_H_
#define _I40E_DEVIDS_H_

/* Device IDs */
#define I40E_DEV_ID_SFP_XL710		0x1572
#define I40E_DEV_ID_QEMU		0x1574
#define I40E_DEV_ID_KX_B		0x1580
#define I40E_DEV_ID_KX_C		0x1581
#define I40E_DEV_ID_QSFP_A		0x1583
#define I40E_DEV_ID_QSFP_B		0x1584
#define I40E_DEV_ID_QSFP_C		0x1585
#define I40E_DEV_ID_10G_BASE_T		0x1586
#define I40E_DEV_ID_20G_KR2		0x1587
#define I40E_DEV_ID_20G_KR2_A		0x1588
#define I40E_DEV_ID_10G_BASE_T4		0x1589
#define I40E_DEV_ID_KX_X722		0x37CE
#define I40E_DEV_ID_QSFP_X722		0x37CF
#define I40E_DEV_ID_SFP_X722		0x37D0
#define I40E_DEV_ID_1G_BASE_T_X722	0x37D1
#define I40E_DEV_ID_10G_BASE_T_X722	0x37D2
#define I40E_DEV_ID_SFP_I_X722		0x37D3

#define i40e_is_40G_device(d)		((d) == I40E_DEV_ID_QSFP_A  || \
					 (d) == I40E_DEV_ID_QSFP_B  || \
					 (d) == I40E_DEV_ID_QSFP_C)

#endif /* _I40E_DEVIDS_H_ */
