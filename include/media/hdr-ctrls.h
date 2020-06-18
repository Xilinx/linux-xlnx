/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * These are the HDR controls for use with the extended control API.
 *
 * It turns out that these structs are not stable yet and will undergo
 * more changes. So keep them private until they are stable and ready to
 * become part of the official public API.
 */

#ifndef _HDR_CTRLS_H_
#define _HDR_CTRLS_H_

#include <linux/types.h>

#define V4L2_CTRL_CLASS_METADATA 0x00b00000

#define V4L2_CID_METADATA_BASE (V4L2_CTRL_CLASS_METADATA | 0x900)
#define V4L2_CID_METADATA_CLASS (V4L2_CTRL_CLASS_METADATA | 1)

#define V4L2_CID_METADATA_HDR (V4L2_CID_METADATA_BASE + 1)

enum v4l2_eotf {
	/*
	 * EOTF values as per CTA 861.G spec (HDMI/DP).
	 * If v4l2 driver is being implemented for other connectivity devices,
	 * a conversion function must be implemented.
	 */
	V4L2_EOTF_TRADITIONAL_GAMMA_SDR,
	V4L2_EOTF_TRADITIONAL_GAMMA_HDR,
	V4L2_EOTF_SMPTE_ST2084,
	V4L2_EOTF_BT_2100_HLG,
};

enum v4l2_hdr_type {
	/*
	 * This is for the v4l2_metadata_hdr structure.
	 * MSB differentiates static (0) or dynamic (1) metadata.
	 * Other 15 bits represent specific HDR standards.
	 */

	/* static HDR */
	V4L2_HDR_TYPE_HDR10     = 0x0000,

	/* dynamic HDR */
	V4L2_HDR_TYPE_HDR10P    = 1 << 15 | V4L2_HDR_TYPE_HDR10,
};

/**
 * struct v4l2_hdr10_payload - HDR Metadata Payload which matches CTA 861.G spec
 *
 * @eotf:				Electro-Optical Transfer Function (EOTF)
 * @metadata_type:			Static_Metadata_Descriptor_ID
 * @display_primaries:			Color Primaries of the Data
 * @white_point:			White Point of Colorspace Data
 * @max_mdl:				Max Mastering Display Luminance
 * @min_mdl:				Min Mastering Display Luminance
 * @max_cll:				Max Content Light Level
 * @max_fall:				Max Frame Average Light Level
 */
struct v4l2_hdr10_payload {
	__u8 eotf;
	__u8 metadata_type;
	struct {
		__u16 x;
		__u16 y;
	} display_primaries[3];
	struct {
		__u16 x;
		__u16 y;
	} white_point;
	__u16 max_mdl;
	__u16 min_mdl;
	__u16 max_cll;
	__u16 max_fall;
};

/**
 * struct v4l2_metadata_hdr - Container for HDR metadata
 *
 * @metadata_type:	HDR type
 * @size:		Size of payload/metadata
 * @payload:		Actual metadata
 */
struct v4l2_metadata_hdr {
	__u16 metadata_type;
	__u16 size;
	/* Currently the largest extended HDR infoframe is 4000 bytes */
	__u8 payload[4000];
};

#endif
