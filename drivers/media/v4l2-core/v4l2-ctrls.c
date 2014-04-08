/*
    V4L2 controls framework implementation.

    Copyright (C) 2010  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-dev.h>

#define has_op(master, op) \
	(master->ops && master->ops->op)
#define call_op(master, op) \
	(has_op(master, op) ? master->ops->op(master) : 0)

/* Internal temporary helper struct, one for each v4l2_ext_control */
struct v4l2_ctrl_helper {
	/* Pointer to the control reference of the master control */
	struct v4l2_ctrl_ref *mref;
	/* The control corresponding to the v4l2_ext_control ID field. */
	struct v4l2_ctrl *ctrl;
	/* v4l2_ext_control index of the next control belonging to the
	   same cluster, or 0 if there isn't any. */
	u32 next;
};

/* Small helper function to determine if the autocluster is set to manual
   mode. */
static bool is_cur_manual(const struct v4l2_ctrl *master)
{
	return master->is_auto && master->cur.val == master->manual_mode_value;
}

/* Same as above, but this checks the against the new value instead of the
   current value. */
static bool is_new_manual(const struct v4l2_ctrl *master)
{
	return master->is_auto && master->val == master->manual_mode_value;
}

/* Returns NULL or a character pointer array containing the menu for
   the given control ID. The pointer array ends with a NULL pointer.
   An empty string signifies a menu entry that is invalid. This allows
   drivers to disable certain options if it is not supported. */
const char * const *v4l2_ctrl_get_menu(u32 id)
{
	static const char * const mpeg_audio_sampling_freq[] = {
		"44.1 kHz",
		"48 kHz",
		"32 kHz",
		NULL
	};
	static const char * const mpeg_audio_encoding[] = {
		"MPEG-1/2 Layer I",
		"MPEG-1/2 Layer II",
		"MPEG-1/2 Layer III",
		"MPEG-2/4 AAC",
		"AC-3",
		NULL
	};
	static const char * const mpeg_audio_l1_bitrate[] = {
		"32 kbps",
		"64 kbps",
		"96 kbps",
		"128 kbps",
		"160 kbps",
		"192 kbps",
		"224 kbps",
		"256 kbps",
		"288 kbps",
		"320 kbps",
		"352 kbps",
		"384 kbps",
		"416 kbps",
		"448 kbps",
		NULL
	};
	static const char * const mpeg_audio_l2_bitrate[] = {
		"32 kbps",
		"48 kbps",
		"56 kbps",
		"64 kbps",
		"80 kbps",
		"96 kbps",
		"112 kbps",
		"128 kbps",
		"160 kbps",
		"192 kbps",
		"224 kbps",
		"256 kbps",
		"320 kbps",
		"384 kbps",
		NULL
	};
	static const char * const mpeg_audio_l3_bitrate[] = {
		"32 kbps",
		"40 kbps",
		"48 kbps",
		"56 kbps",
		"64 kbps",
		"80 kbps",
		"96 kbps",
		"112 kbps",
		"128 kbps",
		"160 kbps",
		"192 kbps",
		"224 kbps",
		"256 kbps",
		"320 kbps",
		NULL
	};
	static const char * const mpeg_audio_ac3_bitrate[] = {
		"32 kbps",
		"40 kbps",
		"48 kbps",
		"56 kbps",
		"64 kbps",
		"80 kbps",
		"96 kbps",
		"112 kbps",
		"128 kbps",
		"160 kbps",
		"192 kbps",
		"224 kbps",
		"256 kbps",
		"320 kbps",
		"384 kbps",
		"448 kbps",
		"512 kbps",
		"576 kbps",
		"640 kbps",
		NULL
	};
	static const char * const mpeg_audio_mode[] = {
		"Stereo",
		"Joint Stereo",
		"Dual",
		"Mono",
		NULL
	};
	static const char * const mpeg_audio_mode_extension[] = {
		"Bound 4",
		"Bound 8",
		"Bound 12",
		"Bound 16",
		NULL
	};
	static const char * const mpeg_audio_emphasis[] = {
		"No Emphasis",
		"50/15 us",
		"CCITT J17",
		NULL
	};
	static const char * const mpeg_audio_crc[] = {
		"No CRC",
		"16-bit CRC",
		NULL
	};
	static const char * const mpeg_audio_dec_playback[] = {
		"Auto",
		"Stereo",
		"Left",
		"Right",
		"Mono",
		"Swapped Stereo",
		NULL
	};
	static const char * const mpeg_video_encoding[] = {
		"MPEG-1",
		"MPEG-2",
		"MPEG-4 AVC",
		NULL
	};
	static const char * const mpeg_video_aspect[] = {
		"1x1",
		"4x3",
		"16x9",
		"2.21x1",
		NULL
	};
	static const char * const mpeg_video_bitrate_mode[] = {
		"Variable Bitrate",
		"Constant Bitrate",
		NULL
	};
	static const char * const mpeg_stream_type[] = {
		"MPEG-2 Program Stream",
		"MPEG-2 Transport Stream",
		"MPEG-1 System Stream",
		"MPEG-2 DVD-compatible Stream",
		"MPEG-1 VCD-compatible Stream",
		"MPEG-2 SVCD-compatible Stream",
		NULL
	};
	static const char * const mpeg_stream_vbi_fmt[] = {
		"No VBI",
		"Private Packet, IVTV Format",
		NULL
	};
	static const char * const camera_power_line_frequency[] = {
		"Disabled",
		"50 Hz",
		"60 Hz",
		"Auto",
		NULL
	};
	static const char * const camera_exposure_auto[] = {
		"Auto Mode",
		"Manual Mode",
		"Shutter Priority Mode",
		"Aperture Priority Mode",
		NULL
	};
	static const char * const camera_exposure_metering[] = {
		"Average",
		"Center Weighted",
		"Spot",
		"Matrix",
		NULL
	};
	static const char * const camera_auto_focus_range[] = {
		"Auto",
		"Normal",
		"Macro",
		"Infinity",
		NULL
	};
	static const char * const colorfx[] = {
		"None",
		"Black & White",
		"Sepia",
		"Negative",
		"Emboss",
		"Sketch",
		"Sky Blue",
		"Grass Green",
		"Skin Whiten",
		"Vivid",
		"Aqua",
		"Art Freeze",
		"Silhouette",
		"Solarization",
		"Antique",
		"Set Cb/Cr",
		NULL
	};
	static const char * const auto_n_preset_white_balance[] = {
		"Manual",
		"Auto",
		"Incandescent",
		"Fluorescent",
		"Fluorescent H",
		"Horizon",
		"Daylight",
		"Flash",
		"Cloudy",
		"Shade",
		NULL,
	};
	static const char * const camera_iso_sensitivity_auto[] = {
		"Manual",
		"Auto",
		NULL
	};
	static const char * const scene_mode[] = {
		"None",
		"Backlight",
		"Beach/Snow",
		"Candle Light",
		"Dusk/Dawn",
		"Fall Colors",
		"Fireworks",
		"Landscape",
		"Night",
		"Party/Indoor",
		"Portrait",
		"Sports",
		"Sunset",
		"Text",
		NULL
	};
	static const char * const tune_emphasis[] = {
		"None",
		"50 Microseconds",
		"75 Microseconds",
		NULL,
	};
	static const char * const header_mode[] = {
		"Separate Buffer",
		"Joined With 1st Frame",
		NULL,
	};
	static const char * const multi_slice[] = {
		"Single",
		"Max Macroblocks",
		"Max Bytes",
		NULL,
	};
	static const char * const entropy_mode[] = {
		"CAVLC",
		"CABAC",
		NULL,
	};
	static const char * const mpeg_h264_level[] = {
		"1",
		"1b",
		"1.1",
		"1.2",
		"1.3",
		"2",
		"2.1",
		"2.2",
		"3",
		"3.1",
		"3.2",
		"4",
		"4.1",
		"4.2",
		"5",
		"5.1",
		NULL,
	};
	static const char * const h264_loop_filter[] = {
		"Enabled",
		"Disabled",
		"Disabled at Slice Boundary",
		NULL,
	};
	static const char * const h264_profile[] = {
		"Baseline",
		"Constrained Baseline",
		"Main",
		"Extended",
		"High",
		"High 10",
		"High 422",
		"High 444 Predictive",
		"High 10 Intra",
		"High 422 Intra",
		"High 444 Intra",
		"CAVLC 444 Intra",
		"Scalable Baseline",
		"Scalable High",
		"Scalable High Intra",
		"Multiview High",
		NULL,
	};
	static const char * const vui_sar_idc[] = {
		"Unspecified",
		"1:1",
		"12:11",
		"10:11",
		"16:11",
		"40:33",
		"24:11",
		"20:11",
		"32:11",
		"80:33",
		"18:11",
		"15:11",
		"64:33",
		"160:99",
		"4:3",
		"3:2",
		"2:1",
		"Extended SAR",
		NULL,
	};
	static const char * const h264_fp_arrangement_type[] = {
		"Checkerboard",
		"Column",
		"Row",
		"Side by Side",
		"Top Bottom",
		"Temporal",
		NULL,
	};
	static const char * const h264_fmo_map_type[] = {
		"Interleaved Slices",
		"Scattered Slices",
		"Foreground with Leftover",
		"Box Out",
		"Raster Scan",
		"Wipe Scan",
		"Explicit",
		NULL,
	};
	static const char * const mpeg_mpeg4_level[] = {
		"0",
		"0b",
		"1",
		"2",
		"3",
		"3b",
		"4",
		"5",
		NULL,
	};
	static const char * const mpeg4_profile[] = {
		"Simple",
		"Advanced Simple",
		"Core",
		"Simple Scalable",
		"Advanced Coding Efficiency",
		NULL,
	};

	static const char * const vpx_golden_frame_sel[] = {
		"Use Previous Frame",
		"Use Previous Specific Frame",
		NULL,
	};

	static const char * const flash_led_mode[] = {
		"Off",
		"Flash",
		"Torch",
		NULL,
	};
	static const char * const flash_strobe_source[] = {
		"Software",
		"External",
		NULL,
	};

	static const char * const jpeg_chroma_subsampling[] = {
		"4:4:4",
		"4:2:2",
		"4:2:0",
		"4:1:1",
		"4:1:0",
		"Gray",
		NULL,
	};
	static const char * const dv_tx_mode[] = {
		"DVI-D",
		"HDMI",
		NULL,
	};
	static const char * const dv_rgb_range[] = {
		"Automatic",
		"RGB limited range (16-235)",
		"RGB full range (0-255)",
		NULL,
	};


	switch (id) {
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
		return mpeg_audio_sampling_freq;
	case V4L2_CID_MPEG_AUDIO_ENCODING:
		return mpeg_audio_encoding;
	case V4L2_CID_MPEG_AUDIO_L1_BITRATE:
		return mpeg_audio_l1_bitrate;
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
		return mpeg_audio_l2_bitrate;
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE:
		return mpeg_audio_l3_bitrate;
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:
		return mpeg_audio_ac3_bitrate;
	case V4L2_CID_MPEG_AUDIO_MODE:
		return mpeg_audio_mode;
	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION:
		return mpeg_audio_mode_extension;
	case V4L2_CID_MPEG_AUDIO_EMPHASIS:
		return mpeg_audio_emphasis;
	case V4L2_CID_MPEG_AUDIO_CRC:
		return mpeg_audio_crc;
	case V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK:
	case V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK:
		return mpeg_audio_dec_playback;
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		return mpeg_video_encoding;
	case V4L2_CID_MPEG_VIDEO_ASPECT:
		return mpeg_video_aspect;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		return mpeg_video_bitrate_mode;
	case V4L2_CID_MPEG_STREAM_TYPE:
		return mpeg_stream_type;
	case V4L2_CID_MPEG_STREAM_VBI_FMT:
		return mpeg_stream_vbi_fmt;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		return camera_power_line_frequency;
	case V4L2_CID_EXPOSURE_AUTO:
		return camera_exposure_auto;
	case V4L2_CID_EXPOSURE_METERING:
		return camera_exposure_metering;
	case V4L2_CID_AUTO_FOCUS_RANGE:
		return camera_auto_focus_range;
	case V4L2_CID_COLORFX:
		return colorfx;
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		return auto_n_preset_white_balance;
	case V4L2_CID_ISO_SENSITIVITY_AUTO:
		return camera_iso_sensitivity_auto;
	case V4L2_CID_SCENE_MODE:
		return scene_mode;
	case V4L2_CID_TUNE_PREEMPHASIS:
		return tune_emphasis;
	case V4L2_CID_TUNE_DEEMPHASIS:
		return tune_emphasis;
	case V4L2_CID_FLASH_LED_MODE:
		return flash_led_mode;
	case V4L2_CID_FLASH_STROBE_SOURCE:
		return flash_strobe_source;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		return header_mode;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
		return multi_slice;
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
		return entropy_mode;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		return mpeg_h264_level;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
		return h264_loop_filter;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		return h264_profile;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:
		return vui_sar_idc;
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE:
		return h264_fp_arrangement_type;
	case V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE:
		return h264_fmo_map_type;
	case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:
		return mpeg_mpeg4_level;
	case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:
		return mpeg4_profile;
	case V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_SEL:
		return vpx_golden_frame_sel;
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:
		return jpeg_chroma_subsampling;
	case V4L2_CID_DV_TX_MODE:
		return dv_tx_mode;
	case V4L2_CID_DV_TX_RGB_RANGE:
	case V4L2_CID_DV_RX_RGB_RANGE:
		return dv_rgb_range;

	default:
		return NULL;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_get_menu);

#define __v4l2_qmenu_int_len(arr, len) ({ *(len) = ARRAY_SIZE(arr); arr; })
/*
 * Returns NULL or an s64 type array containing the menu for given
 * control ID. The total number of the menu items is returned in @len.
 */
const s64 *v4l2_ctrl_get_int_menu(u32 id, u32 *len)
{
	static const s64 qmenu_int_vpx_num_partitions[] = {
		1, 2, 4, 8,
	};

	static const s64 qmenu_int_vpx_num_ref_frames[] = {
		1, 2, 3,
	};

	switch (id) {
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_PARTITIONS:
		return __v4l2_qmenu_int_len(qmenu_int_vpx_num_partitions, len);
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_REF_FRAMES:
		return __v4l2_qmenu_int_len(qmenu_int_vpx_num_ref_frames, len);
	default:
		*len = 0;
		return NULL;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_get_int_menu);

/* Return the control name. */
const char *v4l2_ctrl_get_name(u32 id)
{
	switch (id) {
	/* USER controls */
	/* Keep the order of the 'case's the same as in videodev2.h! */
	case V4L2_CID_USER_CLASS:		return "User Controls";
	case V4L2_CID_BRIGHTNESS:		return "Brightness";
	case V4L2_CID_CONTRAST:			return "Contrast";
	case V4L2_CID_SATURATION:		return "Saturation";
	case V4L2_CID_HUE:			return "Hue";
	case V4L2_CID_AUDIO_VOLUME:		return "Volume";
	case V4L2_CID_AUDIO_BALANCE:		return "Balance";
	case V4L2_CID_AUDIO_BASS:		return "Bass";
	case V4L2_CID_AUDIO_TREBLE:		return "Treble";
	case V4L2_CID_AUDIO_MUTE:		return "Mute";
	case V4L2_CID_AUDIO_LOUDNESS:		return "Loudness";
	case V4L2_CID_BLACK_LEVEL:		return "Black Level";
	case V4L2_CID_AUTO_WHITE_BALANCE:	return "White Balance, Automatic";
	case V4L2_CID_DO_WHITE_BALANCE:		return "Do White Balance";
	case V4L2_CID_RED_BALANCE:		return "Red Balance";
	case V4L2_CID_BLUE_BALANCE:		return "Blue Balance";
	case V4L2_CID_GAMMA:			return "Gamma";
	case V4L2_CID_EXPOSURE:			return "Exposure";
	case V4L2_CID_AUTOGAIN:			return "Gain, Automatic";
	case V4L2_CID_GAIN:			return "Gain";
	case V4L2_CID_HFLIP:			return "Horizontal Flip";
	case V4L2_CID_VFLIP:			return "Vertical Flip";
	case V4L2_CID_POWER_LINE_FREQUENCY:	return "Power Line Frequency";
	case V4L2_CID_HUE_AUTO:			return "Hue, Automatic";
	case V4L2_CID_WHITE_BALANCE_TEMPERATURE: return "White Balance Temperature";
	case V4L2_CID_SHARPNESS:		return "Sharpness";
	case V4L2_CID_BACKLIGHT_COMPENSATION:	return "Backlight Compensation";
	case V4L2_CID_CHROMA_AGC:		return "Chroma AGC";
	case V4L2_CID_COLOR_KILLER:		return "Color Killer";
	case V4L2_CID_COLORFX:			return "Color Effects";
	case V4L2_CID_AUTOBRIGHTNESS:		return "Brightness, Automatic";
	case V4L2_CID_BAND_STOP_FILTER:		return "Band-Stop Filter";
	case V4L2_CID_ROTATE:			return "Rotate";
	case V4L2_CID_BG_COLOR:			return "Background Color";
	case V4L2_CID_CHROMA_GAIN:		return "Chroma Gain";
	case V4L2_CID_ILLUMINATORS_1:		return "Illuminator 1";
	case V4L2_CID_ILLUMINATORS_2:		return "Illuminator 2";
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:	return "Min Number of Capture Buffers";
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:	return "Min Number of Output Buffers";
	case V4L2_CID_ALPHA_COMPONENT:		return "Alpha Component";
	case V4L2_CID_COLORFX_CBCR:		return "Color Effects, CbCr";

	/* Codec controls */
	/* The MPEG controls are applicable to all codec controls
	 * and the 'MPEG' part of the define is historical */
	/* Keep the order of the 'case's the same as in videodev2.h! */
	case V4L2_CID_MPEG_CLASS:		return "Codec Controls";
	case V4L2_CID_MPEG_STREAM_TYPE:		return "Stream Type";
	case V4L2_CID_MPEG_STREAM_PID_PMT:	return "Stream PMT Program ID";
	case V4L2_CID_MPEG_STREAM_PID_AUDIO:	return "Stream Audio Program ID";
	case V4L2_CID_MPEG_STREAM_PID_VIDEO:	return "Stream Video Program ID";
	case V4L2_CID_MPEG_STREAM_PID_PCR:	return "Stream PCR Program ID";
	case V4L2_CID_MPEG_STREAM_PES_ID_AUDIO: return "Stream PES Audio ID";
	case V4L2_CID_MPEG_STREAM_PES_ID_VIDEO: return "Stream PES Video ID";
	case V4L2_CID_MPEG_STREAM_VBI_FMT:	return "Stream VBI Format";
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ: return "Audio Sampling Frequency";
	case V4L2_CID_MPEG_AUDIO_ENCODING:	return "Audio Encoding";
	case V4L2_CID_MPEG_AUDIO_L1_BITRATE:	return "Audio Layer I Bitrate";
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:	return "Audio Layer II Bitrate";
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE:	return "Audio Layer III Bitrate";
	case V4L2_CID_MPEG_AUDIO_MODE:		return "Audio Stereo Mode";
	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION: return "Audio Stereo Mode Extension";
	case V4L2_CID_MPEG_AUDIO_EMPHASIS:	return "Audio Emphasis";
	case V4L2_CID_MPEG_AUDIO_CRC:		return "Audio CRC";
	case V4L2_CID_MPEG_AUDIO_MUTE:		return "Audio Mute";
	case V4L2_CID_MPEG_AUDIO_AAC_BITRATE:	return "Audio AAC Bitrate";
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:	return "Audio AC-3 Bitrate";
	case V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK:	return "Audio Playback";
	case V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK: return "Audio Multilingual Playback";
	case V4L2_CID_MPEG_VIDEO_ENCODING:	return "Video Encoding";
	case V4L2_CID_MPEG_VIDEO_ASPECT:	return "Video Aspect";
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:	return "Video B Frames";
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:	return "Video GOP Size";
	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:	return "Video GOP Closure";
	case V4L2_CID_MPEG_VIDEO_PULLDOWN:	return "Video Pulldown";
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:	return "Video Bitrate Mode";
	case V4L2_CID_MPEG_VIDEO_BITRATE:	return "Video Bitrate";
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:	return "Video Peak Bitrate";
	case V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION: return "Video Temporal Decimation";
	case V4L2_CID_MPEG_VIDEO_MUTE:		return "Video Mute";
	case V4L2_CID_MPEG_VIDEO_MUTE_YUV:	return "Video Mute YUV";
	case V4L2_CID_MPEG_VIDEO_DECODER_SLICE_INTERFACE:	return "Decoder Slice Interface";
	case V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER:	return "MPEG4 Loop Filter Enable";
	case V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB:	return "Number of Intra Refresh MBs";
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:		return "Frame Level Rate Control Enable";
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:			return "H264 MB Level Rate Control";
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:			return "Sequence Header Mode";
	case V4L2_CID_MPEG_VIDEO_MAX_REF_PIC:			return "Max Number of Reference Pics";
	case V4L2_CID_MPEG_VIDEO_H263_I_FRAME_QP:		return "H263 I-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H263_P_FRAME_QP:		return "H263 P-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H263_B_FRAME_QP:		return "H263 B-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H263_MIN_QP:			return "H263 Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_H263_MAX_QP:			return "H263 Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:		return "H264 I-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:		return "H264 P-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP:		return "H264 B-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:			return "H264 Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:			return "H264 Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:		return "H264 8x8 Transform Enable";
	case V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE:			return "H264 CPB Buffer Size";
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:		return "H264 Entropy Mode";
	case V4L2_CID_MPEG_VIDEO_H264_I_PERIOD:			return "H264 I-Frame Period";
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:			return "H264 Level";
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA:	return "H264 Loop Filter Alpha Offset";
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA:		return "H264 Loop Filter Beta Offset";
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:		return "H264 Loop Filter Mode";
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:			return "H264 Profile";
	case V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT:	return "Vertical Size of SAR";
	case V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH:	return "Horizontal Size of SAR";
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE:		return "Aspect Ratio VUI Enable";
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:		return "VUI Aspect Ratio IDC";
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FRAME_PACKING:	return "H264 Enable Frame Packing SEI";
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_CURRENT_FRAME_0:	return "H264 Set Curr. Frame as Frame0";
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE:	return "H264 FP Arrangement Type";
	case V4L2_CID_MPEG_VIDEO_H264_FMO:			return "H264 Flexible MB Ordering";
	case V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE:		return "H264 Map Type for FMO";
	case V4L2_CID_MPEG_VIDEO_H264_FMO_SLICE_GROUP:		return "H264 FMO Number of Slice Groups";
	case V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_DIRECTION:	return "H264 FMO Direction of Change";
	case V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_RATE:		return "H264 FMO Size of 1st Slice Grp";
	case V4L2_CID_MPEG_VIDEO_H264_FMO_RUN_LENGTH:		return "H264 FMO No. of Consecutive MBs";
	case V4L2_CID_MPEG_VIDEO_H264_ASO:			return "H264 Arbitrary Slice Ordering";
	case V4L2_CID_MPEG_VIDEO_H264_ASO_SLICE_ORDER:		return "H264 ASO Slice Order";
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING:	return "Enable H264 Hierarchical Coding";
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE:	return "H264 Hierarchical Coding Type";
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER:return "H264 Number of HC Layers";
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER_QP:
								return "H264 Set QP Value for HC Layers";
	case V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP:		return "MPEG4 I-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP:		return "MPEG4 P-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_MPEG4_B_FRAME_QP:		return "MPEG4 B-Frame QP Value";
	case V4L2_CID_MPEG_VIDEO_MPEG4_MIN_QP:			return "MPEG4 Minimum QP Value";
	case V4L2_CID_MPEG_VIDEO_MPEG4_MAX_QP:			return "MPEG4 Maximum QP Value";
	case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:			return "MPEG4 Level";
	case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:			return "MPEG4 Profile";
	case V4L2_CID_MPEG_VIDEO_MPEG4_QPEL:			return "Quarter Pixel Search Enable";
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES:		return "Maximum Bytes in a Slice";
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB:		return "Number of MBs in a Slice";
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:		return "Slice Partitioning Method";
	case V4L2_CID_MPEG_VIDEO_VBV_SIZE:			return "VBV Buffer Size";
	case V4L2_CID_MPEG_VIDEO_DEC_PTS:			return "Video Decoder PTS";
	case V4L2_CID_MPEG_VIDEO_DEC_FRAME:			return "Video Decoder Frame Count";
	case V4L2_CID_MPEG_VIDEO_VBV_DELAY:			return "Initial Delay for VBV Control";
	case V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER:		return "Repeat Sequence Header";

	/* VPX controls */
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_PARTITIONS:		return "VPX Number of Partitions";
	case V4L2_CID_MPEG_VIDEO_VPX_IMD_DISABLE_4X4:		return "VPX Intra Mode Decision Disable";
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_REF_FRAMES:		return "VPX No. of Refs for P Frame";
	case V4L2_CID_MPEG_VIDEO_VPX_FILTER_LEVEL:		return "VPX Loop Filter Level Range";
	case V4L2_CID_MPEG_VIDEO_VPX_FILTER_SHARPNESS:		return "VPX Deblocking Effect Control";
	case V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_REF_PERIOD:	return "VPX Golden Frame Refresh Period";
	case V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_SEL:		return "VPX Golden Frame Indicator";

	/* CAMERA controls */
	/* Keep the order of the 'case's the same as in videodev2.h! */
	case V4L2_CID_CAMERA_CLASS:		return "Camera Controls";
	case V4L2_CID_EXPOSURE_AUTO:		return "Auto Exposure";
	case V4L2_CID_EXPOSURE_ABSOLUTE:	return "Exposure Time, Absolute";
	case V4L2_CID_EXPOSURE_AUTO_PRIORITY:	return "Exposure, Dynamic Framerate";
	case V4L2_CID_PAN_RELATIVE:		return "Pan, Relative";
	case V4L2_CID_TILT_RELATIVE:		return "Tilt, Relative";
	case V4L2_CID_PAN_RESET:		return "Pan, Reset";
	case V4L2_CID_TILT_RESET:		return "Tilt, Reset";
	case V4L2_CID_PAN_ABSOLUTE:		return "Pan, Absolute";
	case V4L2_CID_TILT_ABSOLUTE:		return "Tilt, Absolute";
	case V4L2_CID_FOCUS_ABSOLUTE:		return "Focus, Absolute";
	case V4L2_CID_FOCUS_RELATIVE:		return "Focus, Relative";
	case V4L2_CID_FOCUS_AUTO:		return "Focus, Automatic Continuous";
	case V4L2_CID_ZOOM_ABSOLUTE:		return "Zoom, Absolute";
	case V4L2_CID_ZOOM_RELATIVE:		return "Zoom, Relative";
	case V4L2_CID_ZOOM_CONTINUOUS:		return "Zoom, Continuous";
	case V4L2_CID_PRIVACY:			return "Privacy";
	case V4L2_CID_IRIS_ABSOLUTE:		return "Iris, Absolute";
	case V4L2_CID_IRIS_RELATIVE:		return "Iris, Relative";
	case V4L2_CID_AUTO_EXPOSURE_BIAS:	return "Auto Exposure, Bias";
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE: return "White Balance, Auto & Preset";
	case V4L2_CID_WIDE_DYNAMIC_RANGE:	return "Wide Dynamic Range";
	case V4L2_CID_IMAGE_STABILIZATION:	return "Image Stabilization";
	case V4L2_CID_ISO_SENSITIVITY:		return "ISO Sensitivity";
	case V4L2_CID_ISO_SENSITIVITY_AUTO:	return "ISO Sensitivity, Auto";
	case V4L2_CID_EXPOSURE_METERING:	return "Exposure, Metering Mode";
	case V4L2_CID_SCENE_MODE:		return "Scene Mode";
	case V4L2_CID_3A_LOCK:			return "3A Lock";
	case V4L2_CID_AUTO_FOCUS_START:		return "Auto Focus, Start";
	case V4L2_CID_AUTO_FOCUS_STOP:		return "Auto Focus, Stop";
	case V4L2_CID_AUTO_FOCUS_STATUS:	return "Auto Focus, Status";
	case V4L2_CID_AUTO_FOCUS_RANGE:		return "Auto Focus, Range";

	/* FM Radio Modulator control */
	/* Keep the order of the 'case's the same as in videodev2.h! */
	case V4L2_CID_FM_TX_CLASS:		return "FM Radio Modulator Controls";
	case V4L2_CID_RDS_TX_DEVIATION:		return "RDS Signal Deviation";
	case V4L2_CID_RDS_TX_PI:		return "RDS Program ID";
	case V4L2_CID_RDS_TX_PTY:		return "RDS Program Type";
	case V4L2_CID_RDS_TX_PS_NAME:		return "RDS PS Name";
	case V4L2_CID_RDS_TX_RADIO_TEXT:	return "RDS Radio Text";
	case V4L2_CID_AUDIO_LIMITER_ENABLED:	return "Audio Limiter Feature Enabled";
	case V4L2_CID_AUDIO_LIMITER_RELEASE_TIME: return "Audio Limiter Release Time";
	case V4L2_CID_AUDIO_LIMITER_DEVIATION:	return "Audio Limiter Deviation";
	case V4L2_CID_AUDIO_COMPRESSION_ENABLED: return "Audio Compression Enabled";
	case V4L2_CID_AUDIO_COMPRESSION_GAIN:	return "Audio Compression Gain";
	case V4L2_CID_AUDIO_COMPRESSION_THRESHOLD: return "Audio Compression Threshold";
	case V4L2_CID_AUDIO_COMPRESSION_ATTACK_TIME: return "Audio Compression Attack Time";
	case V4L2_CID_AUDIO_COMPRESSION_RELEASE_TIME: return "Audio Compression Release Time";
	case V4L2_CID_PILOT_TONE_ENABLED:	return "Pilot Tone Feature Enabled";
	case V4L2_CID_PILOT_TONE_DEVIATION:	return "Pilot Tone Deviation";
	case V4L2_CID_PILOT_TONE_FREQUENCY:	return "Pilot Tone Frequency";
	case V4L2_CID_TUNE_PREEMPHASIS:		return "Pre-Emphasis";
	case V4L2_CID_TUNE_POWER_LEVEL:		return "Tune Power Level";
	case V4L2_CID_TUNE_ANTENNA_CAPACITOR:	return "Tune Antenna Capacitor";

	/* Flash controls */
	case V4L2_CID_FLASH_CLASS:		return "Flash Controls";
	case V4L2_CID_FLASH_LED_MODE:		return "LED Mode";
	case V4L2_CID_FLASH_STROBE_SOURCE:	return "Strobe Source";
	case V4L2_CID_FLASH_STROBE:		return "Strobe";
	case V4L2_CID_FLASH_STROBE_STOP:	return "Stop Strobe";
	case V4L2_CID_FLASH_STROBE_STATUS:	return "Strobe Status";
	case V4L2_CID_FLASH_TIMEOUT:		return "Strobe Timeout";
	case V4L2_CID_FLASH_INTENSITY:		return "Intensity, Flash Mode";
	case V4L2_CID_FLASH_TORCH_INTENSITY:	return "Intensity, Torch Mode";
	case V4L2_CID_FLASH_INDICATOR_INTENSITY: return "Intensity, Indicator";
	case V4L2_CID_FLASH_FAULT:		return "Faults";
	case V4L2_CID_FLASH_CHARGE:		return "Charge";
	case V4L2_CID_FLASH_READY:		return "Ready to Strobe";

	/* JPEG encoder controls */
	/* Keep the order of the 'case's the same as in videodev2.h! */
	case V4L2_CID_JPEG_CLASS:		return "JPEG Compression Controls";
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:	return "Chroma Subsampling";
	case V4L2_CID_JPEG_RESTART_INTERVAL:	return "Restart Interval";
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:	return "Compression Quality";
	case V4L2_CID_JPEG_ACTIVE_MARKER:	return "Active Markers";

	/* Image source controls */
	case V4L2_CID_IMAGE_SOURCE_CLASS:	return "Image Source Controls";
	case V4L2_CID_VBLANK:			return "Vertical Blanking";
	case V4L2_CID_HBLANK:			return "Horizontal Blanking";
	case V4L2_CID_ANALOGUE_GAIN:		return "Analogue Gain";

	/* Image processing controls */
	case V4L2_CID_IMAGE_PROC_CLASS:		return "Image Processing Controls";
	case V4L2_CID_LINK_FREQ:		return "Link Frequency";
	case V4L2_CID_PIXEL_RATE:		return "Pixel Rate";
	case V4L2_CID_TEST_PATTERN:		return "Test Pattern";

	/* DV controls */
	case V4L2_CID_DV_CLASS:			return "Digital Video Controls";
	case V4L2_CID_DV_TX_HOTPLUG:		return "Hotplug Present";
	case V4L2_CID_DV_TX_RXSENSE:		return "RxSense Present";
	case V4L2_CID_DV_TX_EDID_PRESENT:	return "EDID Present";
	case V4L2_CID_DV_TX_MODE:		return "Transmit Mode";
	case V4L2_CID_DV_TX_RGB_RANGE:		return "Tx RGB Quantization Range";
	case V4L2_CID_DV_RX_POWER_PRESENT:	return "Power Present";
	case V4L2_CID_DV_RX_RGB_RANGE:		return "Rx RGB Quantization Range";

	case V4L2_CID_FM_RX_CLASS:		return "FM Radio Receiver Controls";
	case V4L2_CID_TUNE_DEEMPHASIS:		return "De-Emphasis";
	case V4L2_CID_RDS_RECEPTION:		return "RDS Reception";
	default:
		return NULL;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_get_name);

void v4l2_ctrl_fill(u32 id, const char **name, enum v4l2_ctrl_type *type,
		    s32 *min, s32 *max, s32 *step, s32 *def, u32 *flags)
{
	*name = v4l2_ctrl_get_name(id);
	*flags = 0;

	switch (id) {
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_LOUDNESS:
	case V4L2_CID_AUTO_WHITE_BALANCE:
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
	case V4L2_CID_HUE_AUTO:
	case V4L2_CID_CHROMA_AGC:
	case V4L2_CID_COLOR_KILLER:
	case V4L2_CID_AUTOBRIGHTNESS:
	case V4L2_CID_MPEG_AUDIO_MUTE:
	case V4L2_CID_MPEG_VIDEO_MUTE:
	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:
	case V4L2_CID_MPEG_VIDEO_PULLDOWN:
	case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
	case V4L2_CID_FOCUS_AUTO:
	case V4L2_CID_PRIVACY:
	case V4L2_CID_AUDIO_LIMITER_ENABLED:
	case V4L2_CID_AUDIO_COMPRESSION_ENABLED:
	case V4L2_CID_PILOT_TONE_ENABLED:
	case V4L2_CID_ILLUMINATORS_1:
	case V4L2_CID_ILLUMINATORS_2:
	case V4L2_CID_FLASH_STROBE_STATUS:
	case V4L2_CID_FLASH_CHARGE:
	case V4L2_CID_FLASH_READY:
	case V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER:
	case V4L2_CID_MPEG_VIDEO_DECODER_SLICE_INTERFACE:
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE:
	case V4L2_CID_MPEG_VIDEO_MPEG4_QPEL:
	case V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER:
	case V4L2_CID_WIDE_DYNAMIC_RANGE:
	case V4L2_CID_IMAGE_STABILIZATION:
	case V4L2_CID_RDS_RECEPTION:
		*type = V4L2_CTRL_TYPE_BOOLEAN;
		*min = 0;
		*max = *step = 1;
		break;
	case V4L2_CID_PAN_RESET:
	case V4L2_CID_TILT_RESET:
	case V4L2_CID_FLASH_STROBE:
	case V4L2_CID_FLASH_STROBE_STOP:
	case V4L2_CID_AUTO_FOCUS_START:
	case V4L2_CID_AUTO_FOCUS_STOP:
		*type = V4L2_CTRL_TYPE_BUTTON;
		*flags |= V4L2_CTRL_FLAG_WRITE_ONLY;
		*min = *max = *step = *def = 0;
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
	case V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ:
	case V4L2_CID_MPEG_AUDIO_ENCODING:
	case V4L2_CID_MPEG_AUDIO_L1_BITRATE:
	case V4L2_CID_MPEG_AUDIO_L2_BITRATE:
	case V4L2_CID_MPEG_AUDIO_L3_BITRATE:
	case V4L2_CID_MPEG_AUDIO_AC3_BITRATE:
	case V4L2_CID_MPEG_AUDIO_MODE:
	case V4L2_CID_MPEG_AUDIO_MODE_EXTENSION:
	case V4L2_CID_MPEG_AUDIO_EMPHASIS:
	case V4L2_CID_MPEG_AUDIO_CRC:
	case V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK:
	case V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK:
	case V4L2_CID_MPEG_VIDEO_ENCODING:
	case V4L2_CID_MPEG_VIDEO_ASPECT:
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
	case V4L2_CID_MPEG_STREAM_TYPE:
	case V4L2_CID_MPEG_STREAM_VBI_FMT:
	case V4L2_CID_EXPOSURE_AUTO:
	case V4L2_CID_AUTO_FOCUS_RANGE:
	case V4L2_CID_COLORFX:
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
	case V4L2_CID_TUNE_PREEMPHASIS:
	case V4L2_CID_FLASH_LED_MODE:
	case V4L2_CID_FLASH_STROBE_SOURCE:
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE:
	case V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE:
	case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:
	case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:
	case V4L2_CID_ISO_SENSITIVITY_AUTO:
	case V4L2_CID_EXPOSURE_METERING:
	case V4L2_CID_SCENE_MODE:
	case V4L2_CID_DV_TX_MODE:
	case V4L2_CID_DV_TX_RGB_RANGE:
	case V4L2_CID_DV_RX_RGB_RANGE:
	case V4L2_CID_TEST_PATTERN:
	case V4L2_CID_TUNE_DEEMPHASIS:
	case V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_SEL:
		*type = V4L2_CTRL_TYPE_MENU;
		break;
	case V4L2_CID_LINK_FREQ:
		*type = V4L2_CTRL_TYPE_INTEGER_MENU;
		break;
	case V4L2_CID_RDS_TX_PS_NAME:
	case V4L2_CID_RDS_TX_RADIO_TEXT:
		*type = V4L2_CTRL_TYPE_STRING;
		break;
	case V4L2_CID_ISO_SENSITIVITY:
	case V4L2_CID_AUTO_EXPOSURE_BIAS:
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_PARTITIONS:
	case V4L2_CID_MPEG_VIDEO_VPX_NUM_REF_FRAMES:
		*type = V4L2_CTRL_TYPE_INTEGER_MENU;
		break;
	case V4L2_CID_USER_CLASS:
	case V4L2_CID_CAMERA_CLASS:
	case V4L2_CID_MPEG_CLASS:
	case V4L2_CID_FM_TX_CLASS:
	case V4L2_CID_FLASH_CLASS:
	case V4L2_CID_JPEG_CLASS:
	case V4L2_CID_IMAGE_SOURCE_CLASS:
	case V4L2_CID_IMAGE_PROC_CLASS:
	case V4L2_CID_DV_CLASS:
	case V4L2_CID_FM_RX_CLASS:
		*type = V4L2_CTRL_TYPE_CTRL_CLASS;
		/* You can neither read not write these */
		*flags |= V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_WRITE_ONLY;
		*min = *max = *step = *def = 0;
		break;
	case V4L2_CID_BG_COLOR:
		*type = V4L2_CTRL_TYPE_INTEGER;
		*step = 1;
		*min = 0;
		/* Max is calculated as RGB888 that is 2^24 */
		*max = 0xFFFFFF;
		break;
	case V4L2_CID_FLASH_FAULT:
	case V4L2_CID_JPEG_ACTIVE_MARKER:
	case V4L2_CID_3A_LOCK:
	case V4L2_CID_AUTO_FOCUS_STATUS:
	case V4L2_CID_DV_TX_HOTPLUG:
	case V4L2_CID_DV_TX_RXSENSE:
	case V4L2_CID_DV_TX_EDID_PRESENT:
	case V4L2_CID_DV_RX_POWER_PRESENT:
		*type = V4L2_CTRL_TYPE_BITMASK;
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
		*type = V4L2_CTRL_TYPE_INTEGER;
		*flags |= V4L2_CTRL_FLAG_READ_ONLY;
		break;
	case V4L2_CID_MPEG_VIDEO_DEC_FRAME:
	case V4L2_CID_MPEG_VIDEO_DEC_PTS:
		*flags |= V4L2_CTRL_FLAG_VOLATILE;
		/* Fall through */
	case V4L2_CID_PIXEL_RATE:
		*type = V4L2_CTRL_TYPE_INTEGER64;
		*flags |= V4L2_CTRL_FLAG_READ_ONLY;
		*min = *max = *step = *def = 0;
		break;
	default:
		*type = V4L2_CTRL_TYPE_INTEGER;
		break;
	}
	switch (id) {
	case V4L2_CID_MPEG_AUDIO_ENCODING:
	case V4L2_CID_MPEG_AUDIO_MODE:
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
	case V4L2_CID_MPEG_STREAM_TYPE:
		*flags |= V4L2_CTRL_FLAG_UPDATE;
		break;
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_RED_BALANCE:
	case V4L2_CID_BLUE_BALANCE:
	case V4L2_CID_GAMMA:
	case V4L2_CID_SHARPNESS:
	case V4L2_CID_CHROMA_GAIN:
	case V4L2_CID_RDS_TX_DEVIATION:
	case V4L2_CID_AUDIO_LIMITER_RELEASE_TIME:
	case V4L2_CID_AUDIO_LIMITER_DEVIATION:
	case V4L2_CID_AUDIO_COMPRESSION_GAIN:
	case V4L2_CID_AUDIO_COMPRESSION_THRESHOLD:
	case V4L2_CID_AUDIO_COMPRESSION_ATTACK_TIME:
	case V4L2_CID_AUDIO_COMPRESSION_RELEASE_TIME:
	case V4L2_CID_PILOT_TONE_DEVIATION:
	case V4L2_CID_PILOT_TONE_FREQUENCY:
	case V4L2_CID_TUNE_POWER_LEVEL:
	case V4L2_CID_TUNE_ANTENNA_CAPACITOR:
		*flags |= V4L2_CTRL_FLAG_SLIDER;
		break;
	case V4L2_CID_PAN_RELATIVE:
	case V4L2_CID_TILT_RELATIVE:
	case V4L2_CID_FOCUS_RELATIVE:
	case V4L2_CID_IRIS_RELATIVE:
	case V4L2_CID_ZOOM_RELATIVE:
		*flags |= V4L2_CTRL_FLAG_WRITE_ONLY;
		break;
	case V4L2_CID_FLASH_STROBE_STATUS:
	case V4L2_CID_AUTO_FOCUS_STATUS:
	case V4L2_CID_FLASH_READY:
	case V4L2_CID_DV_TX_HOTPLUG:
	case V4L2_CID_DV_TX_RXSENSE:
	case V4L2_CID_DV_TX_EDID_PRESENT:
	case V4L2_CID_DV_RX_POWER_PRESENT:
		*flags |= V4L2_CTRL_FLAG_READ_ONLY;
		break;
	}
}
EXPORT_SYMBOL(v4l2_ctrl_fill);

/* Helper function to determine whether the control type is compatible with
   VIDIOC_G/S_CTRL. */
static bool type_is_int(const struct v4l2_ctrl *ctrl)
{
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER64:
	case V4L2_CTRL_TYPE_STRING:
		/* Nope, these need v4l2_ext_control */
		return false;
	default:
		return true;
	}
}

static void fill_event(struct v4l2_event *ev, struct v4l2_ctrl *ctrl, u32 changes)
{
	memset(ev->reserved, 0, sizeof(ev->reserved));
	ev->type = V4L2_EVENT_CTRL;
	ev->id = ctrl->id;
	ev->u.ctrl.changes = changes;
	ev->u.ctrl.type = ctrl->type;
	ev->u.ctrl.flags = ctrl->flags;
	if (ctrl->type == V4L2_CTRL_TYPE_STRING)
		ev->u.ctrl.value64 = 0;
	else
		ev->u.ctrl.value64 = ctrl->cur.val64;
	ev->u.ctrl.minimum = ctrl->minimum;
	ev->u.ctrl.maximum = ctrl->maximum;
	if (ctrl->type == V4L2_CTRL_TYPE_MENU
	    || ctrl->type == V4L2_CTRL_TYPE_INTEGER_MENU)
		ev->u.ctrl.step = 1;
	else
		ev->u.ctrl.step = ctrl->step;
	ev->u.ctrl.default_value = ctrl->default_value;
}

static void send_event(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl, u32 changes)
{
	struct v4l2_event ev;
	struct v4l2_subscribed_event *sev;

	if (list_empty(&ctrl->ev_subs))
		return;
	fill_event(&ev, ctrl, changes);

	list_for_each_entry(sev, &ctrl->ev_subs, node)
		if (sev->fh != fh ||
		    (sev->flags & V4L2_EVENT_SUB_FL_ALLOW_FEEDBACK))
			v4l2_event_queue_fh(sev->fh, &ev);
}

/* Helper function: copy the current control value back to the caller */
static int cur_to_user(struct v4l2_ext_control *c,
		       struct v4l2_ctrl *ctrl)
{
	u32 len;

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_STRING:
		len = strlen(ctrl->cur.string);
		if (c->size < len + 1) {
			c->size = len + 1;
			return -ENOSPC;
		}
		return copy_to_user(c->string, ctrl->cur.string,
						len + 1) ? -EFAULT : 0;
	case V4L2_CTRL_TYPE_INTEGER64:
		c->value64 = ctrl->cur.val64;
		break;
	default:
		c->value = ctrl->cur.val;
		break;
	}
	return 0;
}

/* Helper function: copy the caller-provider value as the new control value */
static int user_to_new(struct v4l2_ext_control *c,
		       struct v4l2_ctrl *ctrl)
{
	int ret;
	u32 size;

	ctrl->is_new = 1;
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER64:
		ctrl->val64 = c->value64;
		break;
	case V4L2_CTRL_TYPE_STRING:
		size = c->size;
		if (size == 0)
			return -ERANGE;
		if (size > ctrl->maximum + 1)
			size = ctrl->maximum + 1;
		ret = copy_from_user(ctrl->string, c->string, size);
		if (!ret) {
			char last = ctrl->string[size - 1];

			ctrl->string[size - 1] = 0;
			/* If the string was longer than ctrl->maximum,
			   then return an error. */
			if (strlen(ctrl->string) == ctrl->maximum && last)
				return -ERANGE;
		}
		return ret ? -EFAULT : 0;
	default:
		ctrl->val = c->value;
		break;
	}
	return 0;
}

/* Helper function: copy the new control value back to the caller */
static int new_to_user(struct v4l2_ext_control *c,
		       struct v4l2_ctrl *ctrl)
{
	u32 len;

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_STRING:
		len = strlen(ctrl->string);
		if (c->size < len + 1) {
			c->size = ctrl->maximum + 1;
			return -ENOSPC;
		}
		return copy_to_user(c->string, ctrl->string,
						len + 1) ? -EFAULT : 0;
	case V4L2_CTRL_TYPE_INTEGER64:
		c->value64 = ctrl->val64;
		break;
	default:
		c->value = ctrl->val;
		break;
	}
	return 0;
}

/* Copy the new value to the current value. */
static void new_to_cur(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl, u32 ch_flags)
{
	bool changed = false;

	if (ctrl == NULL)
		return;
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_BUTTON:
		changed = true;
		break;
	case V4L2_CTRL_TYPE_STRING:
		/* strings are always 0-terminated */
		changed = strcmp(ctrl->string, ctrl->cur.string);
		strcpy(ctrl->cur.string, ctrl->string);
		break;
	case V4L2_CTRL_TYPE_INTEGER64:
		changed = ctrl->val64 != ctrl->cur.val64;
		ctrl->cur.val64 = ctrl->val64;
		break;
	default:
		changed = ctrl->val != ctrl->cur.val;
		ctrl->cur.val = ctrl->val;
		break;
	}
	if (ch_flags & V4L2_EVENT_CTRL_CH_FLAGS) {
		/* Note: CH_FLAGS is only set for auto clusters. */
		ctrl->flags &=
			~(V4L2_CTRL_FLAG_INACTIVE | V4L2_CTRL_FLAG_VOLATILE);
		if (!is_cur_manual(ctrl->cluster[0])) {
			ctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
			if (ctrl->cluster[0]->has_volatiles)
				ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
		}
		fh = NULL;
	}
	if (changed || ch_flags) {
		/* If a control was changed that was not one of the controls
		   modified by the application, then send the event to all. */
		if (!ctrl->is_new)
			fh = NULL;
		send_event(fh, ctrl,
			(changed ? V4L2_EVENT_CTRL_CH_VALUE : 0) | ch_flags);
		if (ctrl->call_notify && changed && ctrl->handler->notify)
			ctrl->handler->notify(ctrl, ctrl->handler->notify_priv);
	}
}

/* Copy the current value to the new value */
static void cur_to_new(struct v4l2_ctrl *ctrl)
{
	if (ctrl == NULL)
		return;
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_STRING:
		/* strings are always 0-terminated */
		strcpy(ctrl->string, ctrl->cur.string);
		break;
	case V4L2_CTRL_TYPE_INTEGER64:
		ctrl->val64 = ctrl->cur.val64;
		break;
	default:
		ctrl->val = ctrl->cur.val;
		break;
	}
}

/* Return non-zero if one or more of the controls in the cluster has a new
   value that differs from the current value. */
static int cluster_changed(struct v4l2_ctrl *master)
{
	int diff = 0;
	int i;

	for (i = 0; !diff && i < master->ncontrols; i++) {
		struct v4l2_ctrl *ctrl = master->cluster[i];

		if (ctrl == NULL)
			continue;
		switch (ctrl->type) {
		case V4L2_CTRL_TYPE_BUTTON:
			/* Button controls are always 'different' */
			return 1;
		case V4L2_CTRL_TYPE_STRING:
			/* strings are always 0-terminated */
			diff = strcmp(ctrl->string, ctrl->cur.string);
			break;
		case V4L2_CTRL_TYPE_INTEGER64:
			diff = ctrl->val64 != ctrl->cur.val64;
			break;
		default:
			diff = ctrl->val != ctrl->cur.val;
			break;
		}
	}
	return diff;
}

/* Control range checking */
static int check_range(enum v4l2_ctrl_type type,
		s32 min, s32 max, u32 step, s32 def)
{
	switch (type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
		if (step != 1 || max > 1 || min < 0)
			return -ERANGE;
		/* fall through */
	case V4L2_CTRL_TYPE_INTEGER:
		if (step <= 0 || min > max || def < min || def > max)
			return -ERANGE;
		return 0;
	case V4L2_CTRL_TYPE_BITMASK:
		if (step || min || !max || (def & ~max))
			return -ERANGE;
		return 0;
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		if (min > max || def < min || def > max)
			return -ERANGE;
		/* Note: step == menu_skip_mask for menu controls.
		   So here we check if the default value is masked out. */
		if (step && ((1 << def) & step))
			return -EINVAL;
		return 0;
	case V4L2_CTRL_TYPE_STRING:
		if (min > max || min < 0 || step < 1 || def)
			return -ERANGE;
		return 0;
	default:
		return 0;
	}
}

/* Validate a new control */
static int validate_new(const struct v4l2_ctrl *ctrl,
			struct v4l2_ext_control *c)
{
	size_t len;
	u32 offset;
	s32 val;

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER:
		/* Round towards the closest legal value */
		val = c->value + ctrl->step / 2;
		val = clamp(val, ctrl->minimum, ctrl->maximum);
		offset = val - ctrl->minimum;
		offset = ctrl->step * (offset / ctrl->step);
		c->value = ctrl->minimum + offset;
		return 0;

	case V4L2_CTRL_TYPE_BOOLEAN:
		c->value = !!c->value;
		return 0;

	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		if (c->value < ctrl->minimum || c->value > ctrl->maximum)
			return -ERANGE;
		if (ctrl->menu_skip_mask & (1 << c->value))
			return -EINVAL;
		if (ctrl->type == V4L2_CTRL_TYPE_MENU &&
		    ctrl->qmenu[c->value][0] == '\0')
			return -EINVAL;
		return 0;

	case V4L2_CTRL_TYPE_BITMASK:
		c->value &= ctrl->maximum;
		return 0;

	case V4L2_CTRL_TYPE_BUTTON:
	case V4L2_CTRL_TYPE_CTRL_CLASS:
		c->value = 0;
		return 0;

	case V4L2_CTRL_TYPE_INTEGER64:
		return 0;

	case V4L2_CTRL_TYPE_STRING:
		len = strlen(c->string);
		if (len < ctrl->minimum)
			return -ERANGE;
		if ((len - ctrl->minimum) % ctrl->step)
			return -ERANGE;
		return 0;

	default:
		return -EINVAL;
	}
}

static inline u32 node2id(struct list_head *node)
{
	return list_entry(node, struct v4l2_ctrl_ref, node)->ctrl->id;
}

/* Set the handler's error code if it wasn't set earlier already */
static inline int handler_set_err(struct v4l2_ctrl_handler *hdl, int err)
{
	if (hdl->error == 0)
		hdl->error = err;
	return err;
}

/* Initialize the handler */
int v4l2_ctrl_handler_init_class(struct v4l2_ctrl_handler *hdl,
				 unsigned nr_of_controls_hint,
				 struct lock_class_key *key, const char *name)
{
	hdl->lock = &hdl->_lock;
	mutex_init(hdl->lock);
	lockdep_set_class_and_name(hdl->lock, key, name);
	INIT_LIST_HEAD(&hdl->ctrls);
	INIT_LIST_HEAD(&hdl->ctrl_refs);
	hdl->nr_of_buckets = 1 + nr_of_controls_hint / 8;
	hdl->buckets = kcalloc(hdl->nr_of_buckets, sizeof(hdl->buckets[0]),
			       GFP_KERNEL);
	hdl->error = hdl->buckets ? 0 : -ENOMEM;
	return hdl->error;
}
EXPORT_SYMBOL(v4l2_ctrl_handler_init_class);

/* Free all controls and control refs */
void v4l2_ctrl_handler_free(struct v4l2_ctrl_handler *hdl)
{
	struct v4l2_ctrl_ref *ref, *next_ref;
	struct v4l2_ctrl *ctrl, *next_ctrl;
	struct v4l2_subscribed_event *sev, *next_sev;

	if (hdl == NULL || hdl->buckets == NULL)
		return;

	mutex_lock(hdl->lock);
	/* Free all nodes */
	list_for_each_entry_safe(ref, next_ref, &hdl->ctrl_refs, node) {
		list_del(&ref->node);
		kfree(ref);
	}
	/* Free all controls owned by the handler */
	list_for_each_entry_safe(ctrl, next_ctrl, &hdl->ctrls, node) {
		list_del(&ctrl->node);
		list_for_each_entry_safe(sev, next_sev, &ctrl->ev_subs, node)
			list_del(&sev->node);
		kfree(ctrl);
	}
	kfree(hdl->buckets);
	hdl->buckets = NULL;
	hdl->cached = NULL;
	hdl->error = 0;
	mutex_unlock(hdl->lock);
}
EXPORT_SYMBOL(v4l2_ctrl_handler_free);

/* For backwards compatibility: V4L2_CID_PRIVATE_BASE should no longer
   be used except in G_CTRL, S_CTRL, QUERYCTRL and QUERYMENU when dealing
   with applications that do not use the NEXT_CTRL flag.

   We just find the n-th private user control. It's O(N), but that should not
   be an issue in this particular case. */
static struct v4l2_ctrl_ref *find_private_ref(
		struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref;

	id -= V4L2_CID_PRIVATE_BASE;
	list_for_each_entry(ref, &hdl->ctrl_refs, node) {
		/* Search for private user controls that are compatible with
		   VIDIOC_G/S_CTRL. */
		if (V4L2_CTRL_ID2CLASS(ref->ctrl->id) == V4L2_CTRL_CLASS_USER &&
		    V4L2_CTRL_DRIVER_PRIV(ref->ctrl->id)) {
			if (!type_is_int(ref->ctrl))
				continue;
			if (id == 0)
				return ref;
			id--;
		}
	}
	return NULL;
}

/* Find a control with the given ID. */
static struct v4l2_ctrl_ref *find_ref(struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref;
	int bucket;

	id &= V4L2_CTRL_ID_MASK;

	/* Old-style private controls need special handling */
	if (id >= V4L2_CID_PRIVATE_BASE)
		return find_private_ref(hdl, id);
	bucket = id % hdl->nr_of_buckets;

	/* Simple optimization: cache the last control found */
	if (hdl->cached && hdl->cached->ctrl->id == id)
		return hdl->cached;

	/* Not in cache, search the hash */
	ref = hdl->buckets ? hdl->buckets[bucket] : NULL;
	while (ref && ref->ctrl->id != id)
		ref = ref->next;

	if (ref)
		hdl->cached = ref; /* cache it! */
	return ref;
}

/* Find a control with the given ID. Take the handler's lock first. */
static struct v4l2_ctrl_ref *find_ref_lock(
		struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref = NULL;

	if (hdl) {
		mutex_lock(hdl->lock);
		ref = find_ref(hdl, id);
		mutex_unlock(hdl->lock);
	}
	return ref;
}

/* Find a control with the given ID. */
struct v4l2_ctrl *v4l2_ctrl_find(struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref = find_ref_lock(hdl, id);

	return ref ? ref->ctrl : NULL;
}
EXPORT_SYMBOL(v4l2_ctrl_find);

/* Allocate a new v4l2_ctrl_ref and hook it into the handler. */
static int handler_new_ref(struct v4l2_ctrl_handler *hdl,
			   struct v4l2_ctrl *ctrl)
{
	struct v4l2_ctrl_ref *ref;
	struct v4l2_ctrl_ref *new_ref;
	u32 id = ctrl->id;
	u32 class_ctrl = V4L2_CTRL_ID2CLASS(id) | 1;
	int bucket = id % hdl->nr_of_buckets;	/* which bucket to use */

	/* Automatically add the control class if it is not yet present. */
	if (id != class_ctrl && find_ref_lock(hdl, class_ctrl) == NULL)
		if (!v4l2_ctrl_new_std(hdl, NULL, class_ctrl, 0, 0, 0, 0))
			return hdl->error;

	if (hdl->error)
		return hdl->error;

	new_ref = kzalloc(sizeof(*new_ref), GFP_KERNEL);
	if (!new_ref)
		return handler_set_err(hdl, -ENOMEM);
	new_ref->ctrl = ctrl;
	if (ctrl->handler == hdl) {
		/* By default each control starts in a cluster of its own.
		   new_ref->ctrl is basically a cluster array with one
		   element, so that's perfect to use as the cluster pointer.
		   But only do this for the handler that owns the control. */
		ctrl->cluster = &new_ref->ctrl;
		ctrl->ncontrols = 1;
	}

	INIT_LIST_HEAD(&new_ref->node);

	mutex_lock(hdl->lock);

	/* Add immediately at the end of the list if the list is empty, or if
	   the last element in the list has a lower ID.
	   This ensures that when elements are added in ascending order the
	   insertion is an O(1) operation. */
	if (list_empty(&hdl->ctrl_refs) || id > node2id(hdl->ctrl_refs.prev)) {
		list_add_tail(&new_ref->node, &hdl->ctrl_refs);
		goto insert_in_hash;
	}

	/* Find insert position in sorted list */
	list_for_each_entry(ref, &hdl->ctrl_refs, node) {
		if (ref->ctrl->id < id)
			continue;
		/* Don't add duplicates */
		if (ref->ctrl->id == id) {
			kfree(new_ref);
			goto unlock;
		}
		list_add(&new_ref->node, ref->node.prev);
		break;
	}

insert_in_hash:
	/* Insert the control node in the hash */
	new_ref->next = hdl->buckets[bucket];
	hdl->buckets[bucket] = new_ref;

unlock:
	mutex_unlock(hdl->lock);
	return 0;
}

/* Add a new control */
static struct v4l2_ctrl *v4l2_ctrl_new(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, const char *name, enum v4l2_ctrl_type type,
			s32 min, s32 max, u32 step, s32 def,
			u32 flags, const char * const *qmenu,
			const s64 *qmenu_int, void *priv)
{
	struct v4l2_ctrl *ctrl;
	unsigned sz_extra = 0;
	int err;

	if (hdl->error)
		return NULL;

	/* Sanity checks */
	if (id == 0 || name == NULL || id >= V4L2_CID_PRIVATE_BASE ||
	    (type == V4L2_CTRL_TYPE_MENU && qmenu == NULL) ||
	    (type == V4L2_CTRL_TYPE_INTEGER_MENU && qmenu_int == NULL)) {
		handler_set_err(hdl, -ERANGE);
		return NULL;
	}
	err = check_range(type, min, max, step, def);
	if (err) {
		handler_set_err(hdl, err);
		return NULL;
	}
	if (type == V4L2_CTRL_TYPE_BITMASK && ((def & ~max) || min || step)) {
		handler_set_err(hdl, -ERANGE);
		return NULL;
	}

	if (type == V4L2_CTRL_TYPE_BUTTON)
		flags |= V4L2_CTRL_FLAG_WRITE_ONLY;
	else if (type == V4L2_CTRL_TYPE_CTRL_CLASS)
		flags |= V4L2_CTRL_FLAG_READ_ONLY;
	else if (type == V4L2_CTRL_TYPE_STRING)
		sz_extra += 2 * (max + 1);

	ctrl = kzalloc(sizeof(*ctrl) + sz_extra, GFP_KERNEL);
	if (ctrl == NULL) {
		handler_set_err(hdl, -ENOMEM);
		return NULL;
	}

	INIT_LIST_HEAD(&ctrl->node);
	INIT_LIST_HEAD(&ctrl->ev_subs);
	ctrl->handler = hdl;
	ctrl->ops = ops;
	ctrl->id = id;
	ctrl->name = name;
	ctrl->type = type;
	ctrl->flags = flags;
	ctrl->minimum = min;
	ctrl->maximum = max;
	ctrl->step = step;
	if (type == V4L2_CTRL_TYPE_MENU)
		ctrl->qmenu = qmenu;
	else if (type == V4L2_CTRL_TYPE_INTEGER_MENU)
		ctrl->qmenu_int = qmenu_int;
	ctrl->priv = priv;
	ctrl->cur.val = ctrl->val = ctrl->default_value = def;

	if (ctrl->type == V4L2_CTRL_TYPE_STRING) {
		ctrl->cur.string = (char *)&ctrl[1] + sz_extra - (max + 1);
		ctrl->string = (char *)&ctrl[1] + sz_extra - 2 * (max + 1);
		if (ctrl->minimum)
			memset(ctrl->cur.string, ' ', ctrl->minimum);
	}
	if (handler_new_ref(hdl, ctrl)) {
		kfree(ctrl);
		return NULL;
	}
	mutex_lock(hdl->lock);
	list_add_tail(&ctrl->node, &hdl->ctrls);
	mutex_unlock(hdl->lock);
	return ctrl;
}

struct v4l2_ctrl *v4l2_ctrl_new_custom(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_config *cfg, void *priv)
{
	bool is_menu;
	struct v4l2_ctrl *ctrl;
	const char *name = cfg->name;
	const char * const *qmenu = cfg->qmenu;
	const s64 *qmenu_int = cfg->qmenu_int;
	enum v4l2_ctrl_type type = cfg->type;
	u32 flags = cfg->flags;
	s32 min = cfg->min;
	s32 max = cfg->max;
	u32 step = cfg->step;
	s32 def = cfg->def;

	if (name == NULL)
		v4l2_ctrl_fill(cfg->id, &name, &type, &min, &max, &step,
								&def, &flags);

	is_menu = (cfg->type == V4L2_CTRL_TYPE_MENU ||
		   cfg->type == V4L2_CTRL_TYPE_INTEGER_MENU);
	if (is_menu)
		WARN_ON(step);
	else
		WARN_ON(cfg->menu_skip_mask);
	if (cfg->type == V4L2_CTRL_TYPE_MENU && qmenu == NULL)
		qmenu = v4l2_ctrl_get_menu(cfg->id);
	else if (cfg->type == V4L2_CTRL_TYPE_INTEGER_MENU &&
		 qmenu_int == NULL) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}

	ctrl = v4l2_ctrl_new(hdl, cfg->ops, cfg->id, name,
			type, min, max,
			is_menu ? cfg->menu_skip_mask : step,
			def, flags, qmenu, qmenu_int, priv);
	if (ctrl)
		ctrl->is_private = cfg->is_private;
	return ctrl;
}
EXPORT_SYMBOL(v4l2_ctrl_new_custom);

/* Helper function for standard non-menu controls */
struct v4l2_ctrl *v4l2_ctrl_new_std(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, s32 min, s32 max, u32 step, s32 def)
{
	const char *name;
	enum v4l2_ctrl_type type;
	u32 flags;

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);
	if (type == V4L2_CTRL_TYPE_MENU
	    || type == V4L2_CTRL_TYPE_INTEGER_MENU) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, id, name, type,
			     min, max, step, def, flags, NULL, NULL, NULL);
}
EXPORT_SYMBOL(v4l2_ctrl_new_std);

/* Helper function for standard menu controls */
struct v4l2_ctrl *v4l2_ctrl_new_std_menu(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, s32 max, s32 mask, s32 def)
{
	const char * const *qmenu = NULL;
	const s64 *qmenu_int = NULL;
	unsigned int qmenu_int_len = 0;
	const char *name;
	enum v4l2_ctrl_type type;
	s32 min;
	s32 step;
	u32 flags;

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);

	if (type == V4L2_CTRL_TYPE_MENU)
		qmenu = v4l2_ctrl_get_menu(id);
	else if (type == V4L2_CTRL_TYPE_INTEGER_MENU)
		qmenu_int = v4l2_ctrl_get_int_menu(id, &qmenu_int_len);

	if ((!qmenu && !qmenu_int) || (qmenu_int && max > qmenu_int_len)) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, id, name, type,
			     0, max, mask, def, flags, qmenu, qmenu_int, NULL);
}
EXPORT_SYMBOL(v4l2_ctrl_new_std_menu);

/* Helper function for standard menu controls with driver defined menu */
struct v4l2_ctrl *v4l2_ctrl_new_std_menu_items(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops, u32 id, s32 max,
			s32 mask, s32 def, const char * const *qmenu)
{
	enum v4l2_ctrl_type type;
	const char *name;
	u32 flags;
	s32 step;
	s32 min;

	/* v4l2_ctrl_new_std_menu_items() should only be called for
	 * standard controls without a standard menu.
	 */
	if (v4l2_ctrl_get_menu(id)) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);
	if (type != V4L2_CTRL_TYPE_MENU || qmenu == NULL) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, id, name, type, 0, max, mask, def,
			     flags, qmenu, NULL, NULL);

}
EXPORT_SYMBOL(v4l2_ctrl_new_std_menu_items);

/* Helper function for standard integer menu controls */
struct v4l2_ctrl *v4l2_ctrl_new_int_menu(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, s32 max, s32 def, const s64 *qmenu_int)
{
	const char *name;
	enum v4l2_ctrl_type type;
	s32 min;
	s32 step;
	u32 flags;

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);
	if (type != V4L2_CTRL_TYPE_INTEGER_MENU) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, id, name, type,
			     0, max, 0, def, flags, NULL, qmenu_int, NULL);
}
EXPORT_SYMBOL(v4l2_ctrl_new_int_menu);

/* Add a control from another handler to this handler */
struct v4l2_ctrl *v4l2_ctrl_add_ctrl(struct v4l2_ctrl_handler *hdl,
					  struct v4l2_ctrl *ctrl)
{
	if (hdl == NULL || hdl->error)
		return NULL;
	if (ctrl == NULL) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	if (ctrl->handler == hdl)
		return ctrl;
	return handler_new_ref(hdl, ctrl) ? NULL : ctrl;
}
EXPORT_SYMBOL(v4l2_ctrl_add_ctrl);

/* Add the controls from another handler to our own. */
int v4l2_ctrl_add_handler(struct v4l2_ctrl_handler *hdl,
			  struct v4l2_ctrl_handler *add,
			  bool (*filter)(const struct v4l2_ctrl *ctrl))
{
	struct v4l2_ctrl_ref *ref;
	int ret = 0;

	/* Do nothing if either handler is NULL or if they are the same */
	if (!hdl || !add || hdl == add)
		return 0;
	if (hdl->error)
		return hdl->error;
	mutex_lock(add->lock);
	list_for_each_entry(ref, &add->ctrl_refs, node) {
		struct v4l2_ctrl *ctrl = ref->ctrl;

		/* Skip handler-private controls. */
		if (ctrl->is_private)
			continue;
		/* And control classes */
		if (ctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS)
			continue;
		/* Filter any unwanted controls */
		if (filter && !filter(ctrl))
			continue;
		ret = handler_new_ref(hdl, ctrl);
		if (ret)
			break;
	}
	mutex_unlock(add->lock);
	return ret;
}
EXPORT_SYMBOL(v4l2_ctrl_add_handler);

bool v4l2_ctrl_radio_filter(const struct v4l2_ctrl *ctrl)
{
	if (V4L2_CTRL_ID2CLASS(ctrl->id) == V4L2_CTRL_CLASS_FM_TX)
		return true;
	if (V4L2_CTRL_ID2CLASS(ctrl->id) == V4L2_CTRL_CLASS_FM_RX)
		return true;
	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_LOUDNESS:
		return true;
	default:
		break;
	}
	return false;
}
EXPORT_SYMBOL(v4l2_ctrl_radio_filter);

/* Cluster controls */
void v4l2_ctrl_cluster(unsigned ncontrols, struct v4l2_ctrl **controls)
{
	bool has_volatiles = false;
	int i;

	/* The first control is the master control and it must not be NULL */
	BUG_ON(ncontrols == 0 || controls[0] == NULL);

	for (i = 0; i < ncontrols; i++) {
		if (controls[i]) {
			controls[i]->cluster = controls;
			controls[i]->ncontrols = ncontrols;
			if (controls[i]->flags & V4L2_CTRL_FLAG_VOLATILE)
				has_volatiles = true;
		}
	}
	controls[0]->has_volatiles = has_volatiles;
}
EXPORT_SYMBOL(v4l2_ctrl_cluster);

void v4l2_ctrl_auto_cluster(unsigned ncontrols, struct v4l2_ctrl **controls,
			    u8 manual_val, bool set_volatile)
{
	struct v4l2_ctrl *master = controls[0];
	u32 flag = 0;
	int i;

	v4l2_ctrl_cluster(ncontrols, controls);
	WARN_ON(ncontrols <= 1);
	WARN_ON(manual_val < master->minimum || manual_val > master->maximum);
	WARN_ON(set_volatile && !has_op(master, g_volatile_ctrl));
	master->is_auto = true;
	master->has_volatiles = set_volatile;
	master->manual_mode_value = manual_val;
	master->flags |= V4L2_CTRL_FLAG_UPDATE;

	if (!is_cur_manual(master))
		flag = V4L2_CTRL_FLAG_INACTIVE |
			(set_volatile ? V4L2_CTRL_FLAG_VOLATILE : 0);

	for (i = 1; i < ncontrols; i++)
		if (controls[i])
			controls[i]->flags |= flag;
}
EXPORT_SYMBOL(v4l2_ctrl_auto_cluster);

/* Activate/deactivate a control. */
void v4l2_ctrl_activate(struct v4l2_ctrl *ctrl, bool active)
{
	/* invert since the actual flag is called 'inactive' */
	bool inactive = !active;
	bool old;

	if (ctrl == NULL)
		return;

	if (inactive)
		/* set V4L2_CTRL_FLAG_INACTIVE */
		old = test_and_set_bit(4, &ctrl->flags);
	else
		/* clear V4L2_CTRL_FLAG_INACTIVE */
		old = test_and_clear_bit(4, &ctrl->flags);
	if (old != inactive)
		send_event(NULL, ctrl, V4L2_EVENT_CTRL_CH_FLAGS);
}
EXPORT_SYMBOL(v4l2_ctrl_activate);

/* Grab/ungrab a control.
   Typically used when streaming starts and you want to grab controls,
   preventing the user from changing them.

   Just call this and the framework will block any attempts to change
   these controls. */
void v4l2_ctrl_grab(struct v4l2_ctrl *ctrl, bool grabbed)
{
	bool old;

	if (ctrl == NULL)
		return;

	v4l2_ctrl_lock(ctrl);
	if (grabbed)
		/* set V4L2_CTRL_FLAG_GRABBED */
		old = test_and_set_bit(1, &ctrl->flags);
	else
		/* clear V4L2_CTRL_FLAG_GRABBED */
		old = test_and_clear_bit(1, &ctrl->flags);
	if (old != grabbed)
		send_event(NULL, ctrl, V4L2_EVENT_CTRL_CH_FLAGS);
	v4l2_ctrl_unlock(ctrl);
}
EXPORT_SYMBOL(v4l2_ctrl_grab);

/* Log the control name and value */
static void log_ctrl(const struct v4l2_ctrl *ctrl,
		     const char *prefix, const char *colon)
{
	if (ctrl->flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_WRITE_ONLY))
		return;
	if (ctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS)
		return;

	printk(KERN_INFO "%s%s%s: ", prefix, colon, ctrl->name);

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER:
		printk(KERN_CONT "%d", ctrl->cur.val);
		break;
	case V4L2_CTRL_TYPE_BOOLEAN:
		printk(KERN_CONT "%s", ctrl->cur.val ? "true" : "false");
		break;
	case V4L2_CTRL_TYPE_MENU:
		printk(KERN_CONT "%s", ctrl->qmenu[ctrl->cur.val]);
		break;
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		printk(KERN_CONT "%lld", ctrl->qmenu_int[ctrl->cur.val]);
		break;
	case V4L2_CTRL_TYPE_BITMASK:
		printk(KERN_CONT "0x%08x", ctrl->cur.val);
		break;
	case V4L2_CTRL_TYPE_INTEGER64:
		printk(KERN_CONT "%lld", ctrl->cur.val64);
		break;
	case V4L2_CTRL_TYPE_STRING:
		printk(KERN_CONT "%s", ctrl->cur.string);
		break;
	default:
		printk(KERN_CONT "unknown type %d", ctrl->type);
		break;
	}
	if (ctrl->flags & (V4L2_CTRL_FLAG_INACTIVE |
			   V4L2_CTRL_FLAG_GRABBED |
			   V4L2_CTRL_FLAG_VOLATILE)) {
		if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
			printk(KERN_CONT " inactive");
		if (ctrl->flags & V4L2_CTRL_FLAG_GRABBED)
			printk(KERN_CONT " grabbed");
		if (ctrl->flags & V4L2_CTRL_FLAG_VOLATILE)
			printk(KERN_CONT " volatile");
	}
	printk(KERN_CONT "\n");
}

/* Log all controls owned by the handler */
void v4l2_ctrl_handler_log_status(struct v4l2_ctrl_handler *hdl,
				  const char *prefix)
{
	struct v4l2_ctrl *ctrl;
	const char *colon = "";
	int len;

	if (hdl == NULL)
		return;
	if (prefix == NULL)
		prefix = "";
	len = strlen(prefix);
	if (len && prefix[len - 1] != ' ')
		colon = ": ";
	mutex_lock(hdl->lock);
	list_for_each_entry(ctrl, &hdl->ctrls, node)
		if (!(ctrl->flags & V4L2_CTRL_FLAG_DISABLED))
			log_ctrl(ctrl, prefix, colon);
	mutex_unlock(hdl->lock);
}
EXPORT_SYMBOL(v4l2_ctrl_handler_log_status);

int v4l2_ctrl_subdev_log_status(struct v4l2_subdev *sd)
{
	v4l2_ctrl_handler_log_status(sd->ctrl_handler, sd->name);
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_subdev_log_status);

/* Call s_ctrl for all controls owned by the handler */
int v4l2_ctrl_handler_setup(struct v4l2_ctrl_handler *hdl)
{
	struct v4l2_ctrl *ctrl;
	int ret = 0;

	if (hdl == NULL)
		return 0;
	mutex_lock(hdl->lock);
	list_for_each_entry(ctrl, &hdl->ctrls, node)
		ctrl->done = false;

	list_for_each_entry(ctrl, &hdl->ctrls, node) {
		struct v4l2_ctrl *master = ctrl->cluster[0];
		int i;

		/* Skip if this control was already handled by a cluster. */
		/* Skip button controls and read-only controls. */
		if (ctrl->done || ctrl->type == V4L2_CTRL_TYPE_BUTTON ||
		    (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY))
			continue;

		for (i = 0; i < master->ncontrols; i++) {
			if (master->cluster[i]) {
				cur_to_new(master->cluster[i]);
				master->cluster[i]->is_new = 1;
				master->cluster[i]->done = true;
			}
		}
		ret = call_op(master, s_ctrl);
		if (ret)
			break;
	}
	mutex_unlock(hdl->lock);
	return ret;
}
EXPORT_SYMBOL(v4l2_ctrl_handler_setup);

/* Implement VIDIOC_QUERYCTRL */
int v4l2_queryctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_queryctrl *qc)
{
	u32 id = qc->id & V4L2_CTRL_ID_MASK;
	struct v4l2_ctrl_ref *ref;
	struct v4l2_ctrl *ctrl;

	if (hdl == NULL)
		return -EINVAL;

	mutex_lock(hdl->lock);

	/* Try to find it */
	ref = find_ref(hdl, id);

	if ((qc->id & V4L2_CTRL_FLAG_NEXT_CTRL) && !list_empty(&hdl->ctrl_refs)) {
		/* Find the next control with ID > qc->id */

		/* Did we reach the end of the control list? */
		if (id >= node2id(hdl->ctrl_refs.prev)) {
			ref = NULL; /* Yes, so there is no next control */
		} else if (ref) {
			/* We found a control with the given ID, so just get
			   the next one in the list. */
			ref = list_entry(ref->node.next, typeof(*ref), node);
		} else {
			/* No control with the given ID exists, so start
			   searching for the next largest ID. We know there
			   is one, otherwise the first 'if' above would have
			   been true. */
			list_for_each_entry(ref, &hdl->ctrl_refs, node)
				if (id < ref->ctrl->id)
					break;
		}
	}
	mutex_unlock(hdl->lock);
	if (!ref)
		return -EINVAL;

	ctrl = ref->ctrl;
	memset(qc, 0, sizeof(*qc));
	if (id >= V4L2_CID_PRIVATE_BASE)
		qc->id = id;
	else
		qc->id = ctrl->id;
	strlcpy(qc->name, ctrl->name, sizeof(qc->name));
	qc->minimum = ctrl->minimum;
	qc->maximum = ctrl->maximum;
	qc->default_value = ctrl->default_value;
	if (ctrl->type == V4L2_CTRL_TYPE_MENU
	    || ctrl->type == V4L2_CTRL_TYPE_INTEGER_MENU)
		qc->step = 1;
	else
		qc->step = ctrl->step;
	qc->flags = ctrl->flags;
	qc->type = ctrl->type;
	return 0;
}
EXPORT_SYMBOL(v4l2_queryctrl);

int v4l2_subdev_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	if (qc->id & V4L2_CTRL_FLAG_NEXT_CTRL)
		return -EINVAL;
	return v4l2_queryctrl(sd->ctrl_handler, qc);
}
EXPORT_SYMBOL(v4l2_subdev_queryctrl);

/* Implement VIDIOC_QUERYMENU */
int v4l2_querymenu(struct v4l2_ctrl_handler *hdl, struct v4l2_querymenu *qm)
{
	struct v4l2_ctrl *ctrl;
	u32 i = qm->index;

	ctrl = v4l2_ctrl_find(hdl, qm->id);
	if (!ctrl)
		return -EINVAL;

	qm->reserved = 0;
	/* Sanity checks */
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_MENU:
		if (ctrl->qmenu == NULL)
			return -EINVAL;
		break;
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		if (ctrl->qmenu_int == NULL)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (i < ctrl->minimum || i > ctrl->maximum)
		return -EINVAL;

	/* Use mask to see if this menu item should be skipped */
	if (ctrl->menu_skip_mask & (1 << i))
		return -EINVAL;
	/* Empty menu items should also be skipped */
	if (ctrl->type == V4L2_CTRL_TYPE_MENU) {
		if (ctrl->qmenu[i] == NULL || ctrl->qmenu[i][0] == '\0')
			return -EINVAL;
		strlcpy(qm->name, ctrl->qmenu[i], sizeof(qm->name));
	} else {
		qm->value = ctrl->qmenu_int[i];
	}
	return 0;
}
EXPORT_SYMBOL(v4l2_querymenu);

int v4l2_subdev_querymenu(struct v4l2_subdev *sd, struct v4l2_querymenu *qm)
{
	return v4l2_querymenu(sd->ctrl_handler, qm);
}
EXPORT_SYMBOL(v4l2_subdev_querymenu);



/* Some general notes on the atomic requirements of VIDIOC_G/TRY/S_EXT_CTRLS:

   It is not a fully atomic operation, just best-effort only. After all, if
   multiple controls have to be set through multiple i2c writes (for example)
   then some initial writes may succeed while others fail. Thus leaving the
   system in an inconsistent state. The question is how much effort you are
   willing to spend on trying to make something atomic that really isn't.

   From the point of view of an application the main requirement is that
   when you call VIDIOC_S_EXT_CTRLS and some values are invalid then an
   error should be returned without actually affecting any controls.

   If all the values are correct, then it is acceptable to just give up
   in case of low-level errors.

   It is important though that the application can tell when only a partial
   configuration was done. The way we do that is through the error_idx field
   of struct v4l2_ext_controls: if that is equal to the count field then no
   controls were affected. Otherwise all controls before that index were
   successful in performing their 'get' or 'set' operation, the control at
   the given index failed, and you don't know what happened with the controls
   after the failed one. Since if they were part of a control cluster they
   could have been successfully processed (if a cluster member was encountered
   at index < error_idx), they could have failed (if a cluster member was at
   error_idx), or they may not have been processed yet (if the first cluster
   member appeared after error_idx).

   It is all fairly theoretical, though. In practice all you can do is to
   bail out. If error_idx == count, then it is an application bug. If
   error_idx < count then it is only an application bug if the error code was
   EBUSY. That usually means that something started streaming just when you
   tried to set the controls. In all other cases it is a driver/hardware
   problem and all you can do is to retry or bail out.

   Note that these rules do not apply to VIDIOC_TRY_EXT_CTRLS: since that
   never modifies controls the error_idx is just set to whatever control
   has an invalid value.
 */

/* Prepare for the extended g/s/try functions.
   Find the controls in the control array and do some basic checks. */
static int prepare_ext_ctrls(struct v4l2_ctrl_handler *hdl,
			     struct v4l2_ext_controls *cs,
			     struct v4l2_ctrl_helper *helpers)
{
	struct v4l2_ctrl_helper *h;
	bool have_clusters = false;
	u32 i;

	for (i = 0, h = helpers; i < cs->count; i++, h++) {
		struct v4l2_ext_control *c = &cs->controls[i];
		struct v4l2_ctrl_ref *ref;
		struct v4l2_ctrl *ctrl;
		u32 id = c->id & V4L2_CTRL_ID_MASK;

		cs->error_idx = i;

		if (cs->ctrl_class && V4L2_CTRL_ID2CLASS(id) != cs->ctrl_class)
			return -EINVAL;

		/* Old-style private controls are not allowed for
		   extended controls */
		if (id >= V4L2_CID_PRIVATE_BASE)
			return -EINVAL;
		ref = find_ref_lock(hdl, id);
		if (ref == NULL)
			return -EINVAL;
		ctrl = ref->ctrl;
		if (ctrl->flags & V4L2_CTRL_FLAG_DISABLED)
			return -EINVAL;

		if (ctrl->cluster[0]->ncontrols > 1)
			have_clusters = true;
		if (ctrl->cluster[0] != ctrl)
			ref = find_ref_lock(hdl, ctrl->cluster[0]->id);
		/* Store the ref to the master control of the cluster */
		h->mref = ref;
		h->ctrl = ctrl;
		/* Initially set next to 0, meaning that there is no other
		   control in this helper array belonging to the same
		   cluster */
		h->next = 0;
	}

	/* We are done if there were no controls that belong to a multi-
	   control cluster. */
	if (!have_clusters)
		return 0;

	/* The code below figures out in O(n) time which controls in the list
	   belong to the same cluster. */

	/* This has to be done with the handler lock taken. */
	mutex_lock(hdl->lock);

	/* First zero the helper field in the master control references */
	for (i = 0; i < cs->count; i++)
		helpers[i].mref->helper = NULL;
	for (i = 0, h = helpers; i < cs->count; i++, h++) {
		struct v4l2_ctrl_ref *mref = h->mref;

		/* If the mref->helper is set, then it points to an earlier
		   helper that belongs to the same cluster. */
		if (mref->helper) {
			/* Set the next field of mref->helper to the current
			   index: this means that that earlier helper now
			   points to the next helper in the same cluster. */
			mref->helper->next = i;
			/* mref should be set only for the first helper in the
			   cluster, clear the others. */
			h->mref = NULL;
		}
		/* Point the mref helper to the current helper struct. */
		mref->helper = h;
	}
	mutex_unlock(hdl->lock);
	return 0;
}

/* Handles the corner case where cs->count == 0. It checks whether the
   specified control class exists. If that class ID is 0, then it checks
   whether there are any controls at all. */
static int class_check(struct v4l2_ctrl_handler *hdl, u32 ctrl_class)
{
	if (ctrl_class == 0)
		return list_empty(&hdl->ctrl_refs) ? -EINVAL : 0;
	return find_ref_lock(hdl, ctrl_class | 1) ? 0 : -EINVAL;
}



/* Get extended controls. Allocates the helpers array if needed. */
int v4l2_g_ext_ctrls(struct v4l2_ctrl_handler *hdl, struct v4l2_ext_controls *cs)
{
	struct v4l2_ctrl_helper helper[4];
	struct v4l2_ctrl_helper *helpers = helper;
	int ret;
	int i, j;

	cs->error_idx = cs->count;
	cs->ctrl_class = V4L2_CTRL_ID2CLASS(cs->ctrl_class);

	if (hdl == NULL)
		return -EINVAL;

	if (cs->count == 0)
		return class_check(hdl, cs->ctrl_class);

	if (cs->count > ARRAY_SIZE(helper)) {
		helpers = kmalloc_array(cs->count, sizeof(helper[0]),
					GFP_KERNEL);
		if (helpers == NULL)
			return -ENOMEM;
	}

	ret = prepare_ext_ctrls(hdl, cs, helpers);
	cs->error_idx = cs->count;

	for (i = 0; !ret && i < cs->count; i++)
		if (helpers[i].ctrl->flags & V4L2_CTRL_FLAG_WRITE_ONLY)
			ret = -EACCES;

	for (i = 0; !ret && i < cs->count; i++) {
		int (*ctrl_to_user)(struct v4l2_ext_control *c,
				    struct v4l2_ctrl *ctrl) = cur_to_user;
		struct v4l2_ctrl *master;

		if (helpers[i].mref == NULL)
			continue;

		master = helpers[i].mref->ctrl;
		cs->error_idx = i;

		v4l2_ctrl_lock(master);

		/* g_volatile_ctrl will update the new control values */
		if ((master->flags & V4L2_CTRL_FLAG_VOLATILE) ||
			(master->has_volatiles && !is_cur_manual(master))) {
			for (j = 0; j < master->ncontrols; j++)
				cur_to_new(master->cluster[j]);
			ret = call_op(master, g_volatile_ctrl);
			ctrl_to_user = new_to_user;
		}
		/* If OK, then copy the current (for non-volatile controls)
		   or the new (for volatile controls) control values to the
		   caller */
		if (!ret) {
			u32 idx = i;

			do {
				ret = ctrl_to_user(cs->controls + idx,
						   helpers[idx].ctrl);
				idx = helpers[idx].next;
			} while (!ret && idx);
		}
		v4l2_ctrl_unlock(master);
	}

	if (cs->count > ARRAY_SIZE(helper))
		kfree(helpers);
	return ret;
}
EXPORT_SYMBOL(v4l2_g_ext_ctrls);

int v4l2_subdev_g_ext_ctrls(struct v4l2_subdev *sd, struct v4l2_ext_controls *cs)
{
	return v4l2_g_ext_ctrls(sd->ctrl_handler, cs);
}
EXPORT_SYMBOL(v4l2_subdev_g_ext_ctrls);

/* Helper function to get a single control */
static int get_ctrl(struct v4l2_ctrl *ctrl, struct v4l2_ext_control *c)
{
	struct v4l2_ctrl *master = ctrl->cluster[0];
	int ret = 0;
	int i;

	/* String controls are not supported. The new_to_user() and
	 * cur_to_user() calls below would need to be modified not to access
	 * userspace memory when called from get_ctrl().
	 */
	if (ctrl->type == V4L2_CTRL_TYPE_STRING)
		return -EINVAL;

	if (ctrl->flags & V4L2_CTRL_FLAG_WRITE_ONLY)
		return -EACCES;

	v4l2_ctrl_lock(master);
	/* g_volatile_ctrl will update the current control values */
	if (ctrl->flags & V4L2_CTRL_FLAG_VOLATILE) {
		for (i = 0; i < master->ncontrols; i++)
			cur_to_new(master->cluster[i]);
		ret = call_op(master, g_volatile_ctrl);
		new_to_user(c, ctrl);
	} else {
		cur_to_user(c, ctrl);
	}
	v4l2_ctrl_unlock(master);
	return ret;
}

int v4l2_g_ctrl(struct v4l2_ctrl_handler *hdl, struct v4l2_control *control)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(hdl, control->id);
	struct v4l2_ext_control c;
	int ret;

	if (ctrl == NULL || !type_is_int(ctrl))
		return -EINVAL;
	ret = get_ctrl(ctrl, &c);
	control->value = c.value;
	return ret;
}
EXPORT_SYMBOL(v4l2_g_ctrl);

int v4l2_subdev_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *control)
{
	return v4l2_g_ctrl(sd->ctrl_handler, control);
}
EXPORT_SYMBOL(v4l2_subdev_g_ctrl);

s32 v4l2_ctrl_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_ext_control c;

	/* It's a driver bug if this happens. */
	WARN_ON(!type_is_int(ctrl));
	c.value = 0;
	get_ctrl(ctrl, &c);
	return c.value;
}
EXPORT_SYMBOL(v4l2_ctrl_g_ctrl);

s64 v4l2_ctrl_g_ctrl_int64(struct v4l2_ctrl *ctrl)
{
	struct v4l2_ext_control c;

	/* It's a driver bug if this happens. */
	WARN_ON(ctrl->type != V4L2_CTRL_TYPE_INTEGER64);
	c.value = 0;
	get_ctrl(ctrl, &c);
	return c.value;
}
EXPORT_SYMBOL(v4l2_ctrl_g_ctrl_int64);


/* Core function that calls try/s_ctrl and ensures that the new value is
   copied to the current value on a set.
   Must be called with ctrl->handler->lock held. */
static int try_or_set_cluster(struct v4l2_fh *fh, struct v4l2_ctrl *master,
			      bool set, u32 ch_flags)
{
	bool update_flag;
	int ret;
	int i;

	/* Go through the cluster and either validate the new value or
	   (if no new value was set), copy the current value to the new
	   value, ensuring a consistent view for the control ops when
	   called. */
	for (i = 0; i < master->ncontrols; i++) {
		struct v4l2_ctrl *ctrl = master->cluster[i];

		if (ctrl == NULL)
			continue;

		if (!ctrl->is_new) {
			cur_to_new(ctrl);
			continue;
		}
		/* Check again: it may have changed since the
		   previous check in try_or_set_ext_ctrls(). */
		if (set && (ctrl->flags & V4L2_CTRL_FLAG_GRABBED))
			return -EBUSY;
	}

	ret = call_op(master, try_ctrl);

	/* Don't set if there is no change */
	if (ret || !set || !cluster_changed(master))
		return ret;
	ret = call_op(master, s_ctrl);
	if (ret)
		return ret;

	/* If OK, then make the new values permanent. */
	update_flag = is_cur_manual(master) != is_new_manual(master);
	for (i = 0; i < master->ncontrols; i++)
		new_to_cur(fh, master->cluster[i], ch_flags |
			((update_flag && i > 0) ? V4L2_EVENT_CTRL_CH_FLAGS : 0));
	return 0;
}

/* Validate controls. */
static int validate_ctrls(struct v4l2_ext_controls *cs,
			  struct v4l2_ctrl_helper *helpers, bool set)
{
	unsigned i;
	int ret = 0;

	cs->error_idx = cs->count;
	for (i = 0; i < cs->count; i++) {
		struct v4l2_ctrl *ctrl = helpers[i].ctrl;

		cs->error_idx = i;

		if (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY)
			return -EACCES;
		/* This test is also done in try_set_control_cluster() which
		   is called in atomic context, so that has the final say,
		   but it makes sense to do an up-front check as well. Once
		   an error occurs in try_set_control_cluster() some other
		   controls may have been set already and we want to do a
		   best-effort to avoid that. */
		if (set && (ctrl->flags & V4L2_CTRL_FLAG_GRABBED))
			return -EBUSY;
		ret = validate_new(ctrl, &cs->controls[i]);
		if (ret)
			return ret;
	}
	return 0;
}

/* Obtain the current volatile values of an autocluster and mark them
   as new. */
static void update_from_auto_cluster(struct v4l2_ctrl *master)
{
	int i;

	for (i = 0; i < master->ncontrols; i++)
		cur_to_new(master->cluster[i]);
	if (!call_op(master, g_volatile_ctrl))
		for (i = 1; i < master->ncontrols; i++)
			if (master->cluster[i])
				master->cluster[i]->is_new = 1;
}

/* Try or try-and-set controls */
static int try_set_ext_ctrls(struct v4l2_fh *fh, struct v4l2_ctrl_handler *hdl,
			     struct v4l2_ext_controls *cs,
			     bool set)
{
	struct v4l2_ctrl_helper helper[4];
	struct v4l2_ctrl_helper *helpers = helper;
	unsigned i, j;
	int ret;

	cs->error_idx = cs->count;
	cs->ctrl_class = V4L2_CTRL_ID2CLASS(cs->ctrl_class);

	if (hdl == NULL)
		return -EINVAL;

	if (cs->count == 0)
		return class_check(hdl, cs->ctrl_class);

	if (cs->count > ARRAY_SIZE(helper)) {
		helpers = kmalloc_array(cs->count, sizeof(helper[0]),
					GFP_KERNEL);
		if (!helpers)
			return -ENOMEM;
	}
	ret = prepare_ext_ctrls(hdl, cs, helpers);
	if (!ret)
		ret = validate_ctrls(cs, helpers, set);
	if (ret && set)
		cs->error_idx = cs->count;
	for (i = 0; !ret && i < cs->count; i++) {
		struct v4l2_ctrl *master;
		u32 idx = i;

		if (helpers[i].mref == NULL)
			continue;

		cs->error_idx = i;
		master = helpers[i].mref->ctrl;
		v4l2_ctrl_lock(master);

		/* Reset the 'is_new' flags of the cluster */
		for (j = 0; j < master->ncontrols; j++)
			if (master->cluster[j])
				master->cluster[j]->is_new = 0;

		/* For volatile autoclusters that are currently in auto mode
		   we need to discover if it will be set to manual mode.
		   If so, then we have to copy the current volatile values
		   first since those will become the new manual values (which
		   may be overwritten by explicit new values from this set
		   of controls). */
		if (master->is_auto && master->has_volatiles &&
						!is_cur_manual(master)) {
			/* Pick an initial non-manual value */
			s32 new_auto_val = master->manual_mode_value + 1;
			u32 tmp_idx = idx;

			do {
				/* Check if the auto control is part of the
				   list, and remember the new value. */
				if (helpers[tmp_idx].ctrl == master)
					new_auto_val = cs->controls[tmp_idx].value;
				tmp_idx = helpers[tmp_idx].next;
			} while (tmp_idx);
			/* If the new value == the manual value, then copy
			   the current volatile values. */
			if (new_auto_val == master->manual_mode_value)
				update_from_auto_cluster(master);
		}

		/* Copy the new caller-supplied control values.
		   user_to_new() sets 'is_new' to 1. */
		do {
			ret = user_to_new(cs->controls + idx, helpers[idx].ctrl);
			idx = helpers[idx].next;
		} while (!ret && idx);

		if (!ret)
			ret = try_or_set_cluster(fh, master, set, 0);

		/* Copy the new values back to userspace. */
		if (!ret) {
			idx = i;
			do {
				ret = new_to_user(cs->controls + idx,
						helpers[idx].ctrl);
				idx = helpers[idx].next;
			} while (!ret && idx);
		}
		v4l2_ctrl_unlock(master);
	}

	if (cs->count > ARRAY_SIZE(helper))
		kfree(helpers);
	return ret;
}

int v4l2_try_ext_ctrls(struct v4l2_ctrl_handler *hdl, struct v4l2_ext_controls *cs)
{
	return try_set_ext_ctrls(NULL, hdl, cs, false);
}
EXPORT_SYMBOL(v4l2_try_ext_ctrls);

int v4l2_s_ext_ctrls(struct v4l2_fh *fh, struct v4l2_ctrl_handler *hdl,
					struct v4l2_ext_controls *cs)
{
	return try_set_ext_ctrls(fh, hdl, cs, true);
}
EXPORT_SYMBOL(v4l2_s_ext_ctrls);

int v4l2_subdev_try_ext_ctrls(struct v4l2_subdev *sd, struct v4l2_ext_controls *cs)
{
	return try_set_ext_ctrls(NULL, sd->ctrl_handler, cs, false);
}
EXPORT_SYMBOL(v4l2_subdev_try_ext_ctrls);

int v4l2_subdev_s_ext_ctrls(struct v4l2_subdev *sd, struct v4l2_ext_controls *cs)
{
	return try_set_ext_ctrls(NULL, sd->ctrl_handler, cs, true);
}
EXPORT_SYMBOL(v4l2_subdev_s_ext_ctrls);

/* Helper function for VIDIOC_S_CTRL compatibility */
static int set_ctrl(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl,
		    struct v4l2_ext_control *c, u32 ch_flags)
{
	struct v4l2_ctrl *master = ctrl->cluster[0];
	int i;

	/* String controls are not supported. The user_to_new() and
	 * cur_to_user() calls below would need to be modified not to access
	 * userspace memory when called from set_ctrl().
	 */
	if (ctrl->type == V4L2_CTRL_TYPE_STRING)
		return -EINVAL;

	/* Reset the 'is_new' flags of the cluster */
	for (i = 0; i < master->ncontrols; i++)
		if (master->cluster[i])
			master->cluster[i]->is_new = 0;

	/* For autoclusters with volatiles that are switched from auto to
	   manual mode we have to update the current volatile values since
	   those will become the initial manual values after such a switch. */
	if (master->is_auto && master->has_volatiles && ctrl == master &&
	    !is_cur_manual(master) && c->value == master->manual_mode_value)
		update_from_auto_cluster(master);

	user_to_new(c, ctrl);
	return try_or_set_cluster(fh, master, true, ch_flags);
}

/* Helper function for VIDIOC_S_CTRL compatibility */
static int set_ctrl_lock(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl,
			 struct v4l2_ext_control *c)
{
	int ret = validate_new(ctrl, c);

	if (!ret) {
		v4l2_ctrl_lock(ctrl);
		ret = set_ctrl(fh, ctrl, c, 0);
		if (!ret)
			cur_to_user(c, ctrl);
		v4l2_ctrl_unlock(ctrl);
	}
	return ret;
}

int v4l2_s_ctrl(struct v4l2_fh *fh, struct v4l2_ctrl_handler *hdl,
					struct v4l2_control *control)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(hdl, control->id);
	struct v4l2_ext_control c;
	int ret;

	if (ctrl == NULL || !type_is_int(ctrl))
		return -EINVAL;

	if (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY)
		return -EACCES;

	c.value = control->value;
	ret = set_ctrl_lock(fh, ctrl, &c);
	control->value = c.value;
	return ret;
}
EXPORT_SYMBOL(v4l2_s_ctrl);

int v4l2_subdev_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *control)
{
	return v4l2_s_ctrl(NULL, sd->ctrl_handler, control);
}
EXPORT_SYMBOL(v4l2_subdev_s_ctrl);

int v4l2_ctrl_s_ctrl(struct v4l2_ctrl *ctrl, s32 val)
{
	struct v4l2_ext_control c;

	/* It's a driver bug if this happens. */
	WARN_ON(!type_is_int(ctrl));
	c.value = val;
	return set_ctrl_lock(NULL, ctrl, &c);
}
EXPORT_SYMBOL(v4l2_ctrl_s_ctrl);

int v4l2_ctrl_s_ctrl_int64(struct v4l2_ctrl *ctrl, s64 val)
{
	struct v4l2_ext_control c;

	/* It's a driver bug if this happens. */
	WARN_ON(ctrl->type != V4L2_CTRL_TYPE_INTEGER64);
	c.value64 = val;
	return set_ctrl_lock(NULL, ctrl, &c);
}
EXPORT_SYMBOL(v4l2_ctrl_s_ctrl_int64);

void v4l2_ctrl_notify(struct v4l2_ctrl *ctrl, v4l2_ctrl_notify_fnc notify, void *priv)
{
	if (ctrl == NULL)
		return;
	if (notify == NULL) {
		ctrl->call_notify = 0;
		return;
	}
	if (WARN_ON(ctrl->handler->notify && ctrl->handler->notify != notify))
		return;
	ctrl->handler->notify = notify;
	ctrl->handler->notify_priv = priv;
	ctrl->call_notify = 1;
}
EXPORT_SYMBOL(v4l2_ctrl_notify);

int v4l2_ctrl_modify_range(struct v4l2_ctrl *ctrl,
			s32 min, s32 max, u32 step, s32 def)
{
	int ret = check_range(ctrl->type, min, max, step, def);
	struct v4l2_ext_control c;

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
	case V4L2_CTRL_TYPE_BITMASK:
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}
	v4l2_ctrl_lock(ctrl);
	ctrl->minimum = min;
	ctrl->maximum = max;
	ctrl->step = step;
	ctrl->default_value = def;
	c.value = ctrl->cur.val;
	if (validate_new(ctrl, &c))
		c.value = def;
	if (c.value != ctrl->cur.val)
		ret = set_ctrl(NULL, ctrl, &c, V4L2_EVENT_CTRL_CH_RANGE);
	else
		send_event(NULL, ctrl, V4L2_EVENT_CTRL_CH_RANGE);
	v4l2_ctrl_unlock(ctrl);
	return ret;
}
EXPORT_SYMBOL(v4l2_ctrl_modify_range);

static int v4l2_ctrl_add_event(struct v4l2_subscribed_event *sev, unsigned elems)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(sev->fh->ctrl_handler, sev->id);

	if (ctrl == NULL)
		return -EINVAL;

	v4l2_ctrl_lock(ctrl);
	list_add_tail(&sev->node, &ctrl->ev_subs);
	if (ctrl->type != V4L2_CTRL_TYPE_CTRL_CLASS &&
	    (sev->flags & V4L2_EVENT_SUB_FL_SEND_INITIAL)) {
		struct v4l2_event ev;
		u32 changes = V4L2_EVENT_CTRL_CH_FLAGS;

		if (!(ctrl->flags & V4L2_CTRL_FLAG_WRITE_ONLY))
			changes |= V4L2_EVENT_CTRL_CH_VALUE;
		fill_event(&ev, ctrl, changes);
		/* Mark the queue as active, allowing this initial
		   event to be accepted. */
		sev->elems = elems;
		v4l2_event_queue_fh(sev->fh, &ev);
	}
	v4l2_ctrl_unlock(ctrl);
	return 0;
}

static void v4l2_ctrl_del_event(struct v4l2_subscribed_event *sev)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(sev->fh->ctrl_handler, sev->id);

	v4l2_ctrl_lock(ctrl);
	list_del(&sev->node);
	v4l2_ctrl_unlock(ctrl);
}

void v4l2_ctrl_replace(struct v4l2_event *old, const struct v4l2_event *new)
{
	u32 old_changes = old->u.ctrl.changes;

	old->u.ctrl = new->u.ctrl;
	old->u.ctrl.changes |= old_changes;
}
EXPORT_SYMBOL(v4l2_ctrl_replace);

void v4l2_ctrl_merge(const struct v4l2_event *old, struct v4l2_event *new)
{
	new->u.ctrl.changes |= old->u.ctrl.changes;
}
EXPORT_SYMBOL(v4l2_ctrl_merge);

const struct v4l2_subscribed_event_ops v4l2_ctrl_sub_ev_ops = {
	.add = v4l2_ctrl_add_event,
	.del = v4l2_ctrl_del_event,
	.replace = v4l2_ctrl_replace,
	.merge = v4l2_ctrl_merge,
};
EXPORT_SYMBOL(v4l2_ctrl_sub_ev_ops);

int v4l2_ctrl_log_status(struct file *file, void *fh)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_fh *vfh = file->private_data;

	if (test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) && vfd->v4l2_dev)
		v4l2_ctrl_handler_log_status(vfh->ctrl_handler,
			vfd->v4l2_dev->name);
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_log_status);

int v4l2_ctrl_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	if (sub->type == V4L2_EVENT_CTRL)
		return v4l2_event_subscribe(fh, sub, 0, &v4l2_ctrl_sub_ev_ops);
	return -EINVAL;
}
EXPORT_SYMBOL(v4l2_ctrl_subscribe_event);

int v4l2_ctrl_subdev_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				     struct v4l2_event_subscription *sub)
{
	if (!sd->ctrl_handler)
		return -EINVAL;
	return v4l2_ctrl_subscribe_event(fh, sub);
}
EXPORT_SYMBOL(v4l2_ctrl_subdev_subscribe_event);

unsigned int v4l2_ctrl_poll(struct file *file, struct poll_table_struct *wait)
{
	struct v4l2_fh *fh = file->private_data;

	if (v4l2_event_pending(fh))
		return POLLPRI;
	poll_wait(file, &fh->wait, wait);
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_poll);
