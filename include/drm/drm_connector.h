/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef __DRM_CONNECTOR_H__
#define __DRM_CONNECTOR_H__

#include <linux/list.h>
#include <linux/ctype.h>
#include <drm/drm_mode_object.h>

#include <uapi/drm/drm_mode.h>

struct drm_device;

struct drm_connector_helper_funcs;
struct drm_device;
struct drm_crtc;
struct drm_encoder;
struct drm_property;
struct drm_property_blob;
struct edid;

enum drm_connector_force {
	DRM_FORCE_UNSPECIFIED,
	DRM_FORCE_OFF,
	DRM_FORCE_ON,         /* force on analog part normally */
	DRM_FORCE_ON_DIGITAL, /* for DVI-I use digital connector */
};

/**
 * enum drm_connector_status - status for a &drm_connector
 *
 * This enum is used to track the connector status. There are no separate
 * #defines for the uapi!
 */
enum drm_connector_status {
	/**
	 * @connector_status_connected: The connector is definitely connected to
	 * a sink device, and can be enabled.
	 */
	connector_status_connected = 1,
	/**
	 * @connector_status_disconnected: The connector isn't connected to a
	 * sink device which can be autodetect. For digital outputs like DP or
	 * HDMI (which can be realiable probed) this means there's really
	 * nothing there. It is driver-dependent whether a connector with this
	 * status can be lit up or not.
	 */
	connector_status_disconnected = 2,
	/**
	 * @connector_status_unknown: The connector's status could not be
	 * reliably detected. This happens when probing would either cause
	 * flicker (like load-detection when the connector is in use), or when a
	 * hardware resource isn't available (like when load-detection needs a
	 * free CRTC). It should be possible to light up the connector with one
	 * of the listed fallback modes. For default configuration userspace
	 * should only try to light up connectors with unknown status when
	 * there's not connector with @connector_status_connected.
	 */
	connector_status_unknown = 3,
};

enum subpixel_order {
	SubPixelUnknown = 0,
	SubPixelHorizontalRGB,
	SubPixelHorizontalBGR,
	SubPixelVerticalRGB,
	SubPixelVerticalBGR,
	SubPixelNone,
};

/**
 * struct drm_display_info - runtime data about the connected sink
 *
 * Describes a given display (e.g. CRT or flat panel) and its limitations. For
 * fixed display sinks like built-in panels there's not much difference between
 * this and struct &drm_connector. But for sinks with a real cable this
 * structure is meant to describe all the things at the other end of the cable.
 *
 * For sinks which provide an EDID this can be filled out by calling
 * drm_add_edid_modes().
 */
struct drm_display_info {
	/**
	 * @name: Name of the display.
	 */
	char name[DRM_DISPLAY_INFO_LEN];

	/**
	 * @width_mm: Physical width in mm.
	 */
        unsigned int width_mm;
	/**
	 * @height_mm: Physical height in mm.
	 */
	unsigned int height_mm;

	/**
	 * @pixel_clock: Maximum pixel clock supported by the sink, in units of
	 * 100Hz. This mismatches the clok in &drm_display_mode (which is in
	 * kHZ), because that's what the EDID uses as base unit.
	 */
	unsigned int pixel_clock;
	/**
	 * @bpc: Maximum bits per color channel. Used by HDMI and DP outputs.
	 */
	unsigned int bpc;

	/**
	 * @subpixel_order: Subpixel order of LCD panels.
	 */
	enum subpixel_order subpixel_order;

#define DRM_COLOR_FORMAT_RGB444		(1<<0)
#define DRM_COLOR_FORMAT_YCRCB444	(1<<1)
#define DRM_COLOR_FORMAT_YCRCB422	(1<<2)

	/**
	 * @color_formats: HDMI Color formats, selects between RGB and YCrCb
	 * modes. Used DRM_COLOR_FORMAT\_ defines, which are _not_ the same ones
	 * as used to describe the pixel format in framebuffers, and also don't
	 * match the formats in @bus_formats which are shared with v4l.
	 */
	u32 color_formats;

	/**
	 * @bus_formats: Pixel data format on the wire, somewhat redundant with
	 * @color_formats. Array of size @num_bus_formats encoded using
	 * MEDIA_BUS_FMT\_ defines shared with v4l and media drivers.
	 */
	const u32 *bus_formats;
	/**
	 * @num_bus_formats: Size of @bus_formats array.
	 */
	unsigned int num_bus_formats;

#define DRM_BUS_FLAG_DE_LOW		(1<<0)
#define DRM_BUS_FLAG_DE_HIGH		(1<<1)
/* drive data on pos. edge */
#define DRM_BUS_FLAG_PIXDATA_POSEDGE	(1<<2)
/* drive data on neg. edge */
#define DRM_BUS_FLAG_PIXDATA_NEGEDGE	(1<<3)

	/**
	 * @bus_flags: Additional information (like pixel signal polarity) for
	 * the pixel data on the bus, using DRM_BUS_FLAGS\_ defines.
	 */
	u32 bus_flags;

	/**
	 * @max_tmds_clock: Maximum TMDS clock rate supported by the
	 * sink in kHz. 0 means undefined.
	 */
	int max_tmds_clock;

	/**
	 * @dvi_dual: Dual-link DVI sink?
	 */
	bool dvi_dual;

	/**
	 * @edid_hdmi_dc_modes: Mask of supported hdmi deep color modes. Even
	 * more stuff redundant with @bus_formats.
	 */
	u8 edid_hdmi_dc_modes;

	/**
	 * @cea_rev: CEA revision of the HDMI sink.
	 */
	u8 cea_rev;
};

int drm_display_info_set_bus_formats(struct drm_display_info *info,
				     const u32 *formats,
				     unsigned int num_formats);

/**
 * struct drm_connector_state - mutable connector state
 * @connector: backpointer to the connector
 * @best_encoder: can be used by helpers and drivers to select the encoder
 * @state: backpointer to global drm_atomic_state
 */
struct drm_connector_state {
	struct drm_connector *connector;

	/**
	 * @crtc: CRTC to connect connector to, NULL if disabled.
	 *
	 * Do not change this directly, use drm_atomic_set_crtc_for_connector()
	 * instead.
	 */
	struct drm_crtc *crtc;

	struct drm_encoder *best_encoder;

	struct drm_atomic_state *state;
};

/**
 * struct drm_connector_funcs - control connectors on a given device
 *
 * Each CRTC may have one or more connectors attached to it.  The functions
 * below allow the core DRM code to control connectors, enumerate available modes,
 * etc.
 */
struct drm_connector_funcs {
	/**
	 * @dpms:
	 *
	 * Legacy entry point to set the per-connector DPMS state. Legacy DPMS
	 * is exposed as a standard property on the connector, but diverted to
	 * this callback in the drm core. Note that atomic drivers don't
	 * implement the 4 level DPMS support on the connector any more, but
	 * instead only have an on/off "ACTIVE" property on the CRTC object.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_connector_dpms() to implement this hook.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*dpms)(struct drm_connector *connector, int mode);

	/**
	 * @reset:
	 *
	 * Reset connector hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 *
	 * Atomic drivers can use drm_atomic_helper_connector_reset() to reset
	 * atomic state using this hook.
	 */
	void (*reset)(struct drm_connector *connector);

	/**
	 * @detect:
	 *
	 * Check to see if anything is attached to the connector. The parameter
	 * force is set to false whilst polling, true when checking the
	 * connector due to a user request. force can be used by the driver to
	 * avoid expensive, destructive operations during automated probing.
	 *
	 * FIXME:
	 *
	 * Note that this hook is only called by the probe helper. It's not in
	 * the helper library vtable purely for historical reasons. The only DRM
	 * core	entry point to probe connector state is @fill_modes.
	 *
	 * RETURNS:
	 *
	 * drm_connector_status indicating the connector's status.
	 */
	enum drm_connector_status (*detect)(struct drm_connector *connector,
					    bool force);

	/**
	 * @force:
	 *
	 * This function is called to update internal encoder state when the
	 * connector is forced to a certain state by userspace, either through
	 * the sysfs interfaces or on the kernel cmdline. In that case the
	 * @detect callback isn't called.
	 *
	 * FIXME:
	 *
	 * Note that this hook is only called by the probe helper. It's not in
	 * the helper library vtable purely for historical reasons. The only DRM
	 * core	entry point to probe connector state is @fill_modes.
	 */
	void (*force)(struct drm_connector *connector);

	/**
	 * @fill_modes:
	 *
	 * Entry point for output detection and basic mode validation. The
	 * driver should reprobe the output if needed (e.g. when hotplug
	 * handling is unreliable), add all detected modes to connector->modes
	 * and filter out any the device can't support in any configuration. It
	 * also needs to filter out any modes wider or higher than the
	 * parameters max_width and max_height indicate.
	 *
	 * The drivers must also prune any modes no longer valid from
	 * connector->modes. Furthermore it must update connector->status and
	 * connector->edid.  If no EDID has been received for this output
	 * connector->edid must be NULL.
	 *
	 * Drivers using the probe helpers should use
	 * drm_helper_probe_single_connector_modes() or
	 * drm_helper_probe_single_connector_modes_nomerge() to implement this
	 * function.
	 *
	 * RETURNS:
	 *
	 * The number of modes detected and filled into connector->modes.
	 */
	int (*fill_modes)(struct drm_connector *connector, uint32_t max_width, uint32_t max_height);

	/**
	 * @set_property:
	 *
	 * This is the legacy entry point to update a property attached to the
	 * connector.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_connector_set_property() to implement this hook.
	 *
	 * This callback is optional if the driver does not support any legacy
	 * driver-private properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_property)(struct drm_connector *connector, struct drm_property *property,
			     uint64_t val);

	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the connector, light backlight control, i2c,
	 * DP aux or similar interfaces. It is called late in the driver load
	 * sequence from drm_connector_register() when registering all the
	 * core drm connector interfaces. Everything added from this callback
	 * should be unregistered in the early_unregister callback.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_connector *connector);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the connector from
	 * late_register(). It is called from drm_connector_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 */
	void (*early_unregister)(struct drm_connector *connector);

	/**
	 * @destroy:
	 *
	 * Clean up connector resources. This is called at driver unload time
	 * through drm_mode_config_cleanup(). It can also be called at runtime
	 * when a connector is being hot-unplugged for drivers that support
	 * connector hotplugging (e.g. DisplayPort MST).
	 */
	void (*destroy)(struct drm_connector *connector);

	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current atomic state for this connector and return it.
	 * The core and helpers guarantee that any atomic state duplicated with
	 * this hook and still owned by the caller (i.e. not transferred to the
	 * driver by calling ->atomic_commit() from struct
	 * &drm_mode_config_funcs) will be cleaned up by calling the
	 * @atomic_destroy_state hook in this structure.
	 *
	 * Atomic drivers which don't subclass struct &drm_connector_state should use
	 * drm_atomic_helper_connector_duplicate_state(). Drivers that subclass the
	 * state structure to extend it with driver-private state should use
	 * __drm_atomic_helper_connector_duplicate_state() to make sure shared state is
	 * duplicated in a consistent fashion across drivers.
	 *
	 * It is an error to call this hook before connector->state has been
	 * initialized correctly.
	 *
	 * NOTE:
	 *
	 * If the duplicate state references refcounted resources this hook must
	 * acquire a reference for each of them. The driver must release these
	 * references again in @atomic_destroy_state.
	 *
	 * RETURNS:
	 *
	 * Duplicated atomic state or NULL when the allocation failed.
	 */
	struct drm_connector_state *(*atomic_duplicate_state)(struct drm_connector *connector);

	/**
	 * @atomic_destroy_state:
	 *
	 * Destroy a state duplicated with @atomic_duplicate_state and release
	 * or unreference all resources it references
	 */
	void (*atomic_destroy_state)(struct drm_connector *connector,
				     struct drm_connector_state *state);

	/**
	 * @atomic_set_property:
	 *
	 * Decode a driver-private property value and store the decoded value
	 * into the passed-in state structure. Since the atomic core decodes all
	 * standardized properties (even for extensions beyond the core set of
	 * properties which might not be implemented by all drivers) this
	 * requires drivers to subclass the state structure.
	 *
	 * Such driver-private properties should really only be implemented for
	 * truly hardware/vendor specific state. Instead it is preferred to
	 * standardize atomic extension and decode the properties used to expose
	 * such an extension in the core.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_connector_set_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * NOTE:
	 *
	 * This function is called in the state assembly phase of atomic
	 * modesets, which can be aborted for any reason (including on
	 * userspace's request to just check whether a configuration would be
	 * possible). Drivers MUST NOT touch any persistent state (hardware or
	 * software) or data structures except the passed in @state parameter.
	 *
	 * Also since userspace controls in which order properties are set this
	 * function must not do any input validation (since the state update is
	 * incomplete and hence likely inconsistent). Instead any such input
	 * validation must be done in the various atomic_check callbacks.
	 *
	 * RETURNS:
	 *
	 * 0 if the property has been found, -EINVAL if the property isn't
	 * implemented by the driver (which shouldn't ever happen, the core only
	 * asks for properties attached to this connector). No other validation
	 * is allowed by the driver. The core already checks that the property
	 * value is within the range (integer, valid enum value, ...) the driver
	 * set when registering the property.
	 */
	int (*atomic_set_property)(struct drm_connector *connector,
				   struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t val);

	/**
	 * @atomic_get_property:
	 *
	 * Reads out the decoded driver-private property. This is used to
	 * implement the GETCONNECTOR IOCTL.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_connector_get_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the property isn't implemented by the
	 * driver (which shouldn't ever happen, the core only asks for
	 * properties attached to this connector).
	 */
	int (*atomic_get_property)(struct drm_connector *connector,
				   const struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t *val);
};

/* mode specified on the command line */
struct drm_cmdline_mode {
	bool specified;
	bool refresh_specified;
	bool bpp_specified;
	int xres, yres;
	int bpp;
	int refresh;
	bool rb;
	bool interlace;
	bool cvt;
	bool margins;
	enum drm_connector_force force;
};

/**
 * struct drm_connector - central DRM connector control structure
 * @dev: parent DRM device
 * @kdev: kernel device for sysfs attributes
 * @attr: sysfs attributes
 * @head: list management
 * @base: base KMS object
 * @name: human readable name, can be overwritten by the driver
 * @connector_type: one of the DRM_MODE_CONNECTOR_<foo> types from drm_mode.h
 * @connector_type_id: index into connector type enum
 * @interlace_allowed: can this connector handle interlaced modes?
 * @doublescan_allowed: can this connector handle doublescan?
 * @stereo_allowed: can this connector handle stereo modes?
 * @registered: is this connector exposed (registered) with userspace?
 * @modes: modes available on this connector (from fill_modes() + user)
 * @status: one of the drm_connector_status enums (connected, not, or unknown)
 * @probed_modes: list of modes derived directly from the display
 * @funcs: connector control functions
 * @edid_blob_ptr: DRM property containing EDID if present
 * @properties: property tracking for this connector
 * @dpms: current dpms state
 * @helper_private: mid-layer private data
 * @cmdline_mode: mode line parsed from the kernel cmdline for this connector
 * @force: a DRM_FORCE_<foo> state for forced mode sets
 * @override_edid: has the EDID been overwritten through debugfs for testing?
 * @encoder_ids: valid encoders for this connector
 * @encoder: encoder driving this connector, if any
 * @eld: EDID-like data, if present
 * @latency_present: AV delay info from ELD, if found
 * @video_latency: video latency info from ELD, if found
 * @audio_latency: audio latency info from ELD, if found
 * @null_edid_counter: track sinks that give us all zeros for the EDID
 * @bad_edid_counter: track sinks that give us an EDID with invalid checksum
 * @edid_corrupt: indicates whether the last read EDID was corrupt
 * @debugfs_entry: debugfs directory for this connector
 * @state: current atomic state for this connector
 * @has_tile: is this connector connected to a tiled monitor
 * @tile_group: tile group for the connected monitor
 * @tile_is_single_monitor: whether the tile is one monitor housing
 * @num_h_tile: number of horizontal tiles in the tile group
 * @num_v_tile: number of vertical tiles in the tile group
 * @tile_h_loc: horizontal location of this tile
 * @tile_v_loc: vertical location of this tile
 * @tile_h_size: horizontal size of this tile.
 * @tile_v_size: vertical size of this tile.
 *
 * Each connector may be connected to one or more CRTCs, or may be clonable by
 * another connector if they can share a CRTC.  Each connector also has a specific
 * position in the broader display (referred to as a 'screen' though it could
 * span multiple monitors).
 */
struct drm_connector {
	struct drm_device *dev;
	struct device *kdev;
	struct device_attribute *attr;
	struct list_head head;

	struct drm_mode_object base;

	char *name;

	/**
	 * @index: Compacted connector index, which matches the position inside
	 * the mode_config.list for drivers not supporting hot-add/removing. Can
	 * be used as an array index. It is invariant over the lifetime of the
	 * connector.
	 */
	unsigned index;

	int connector_type;
	int connector_type_id;
	bool interlace_allowed;
	bool doublescan_allowed;
	bool stereo_allowed;
	bool registered;
	struct list_head modes; /* list of modes on this connector */

	enum drm_connector_status status;

	/* these are modes added by probing with DDC or the BIOS */
	struct list_head probed_modes;

	/**
	 * @display_info: Display information is filled from EDID information
	 * when a display is detected. For non hot-pluggable displays such as
	 * flat panels in embedded systems, the driver should initialize the
	 * display_info.width_mm and display_info.height_mm fields with the
	 * physical size of the display.
	 */
	struct drm_display_info display_info;
	const struct drm_connector_funcs *funcs;

	struct drm_property_blob *edid_blob_ptr;
	struct drm_object_properties properties;

	/**
	 * @path_blob_ptr:
	 *
	 * DRM blob property data for the DP MST path property.
	 */
	struct drm_property_blob *path_blob_ptr;

	/**
	 * @tile_blob_ptr:
	 *
	 * DRM blob property data for the tile property (used mostly by DP MST).
	 * This is meant for screens which are driven through separate display
	 * pipelines represented by &drm_crtc, which might not be running with
	 * genlocked clocks. For tiled panels which are genlocked, like
	 * dual-link LVDS or dual-link DSI, the driver should try to not expose
	 * the tiling and virtualize both &drm_crtc and &drm_plane if needed.
	 */
	struct drm_property_blob *tile_blob_ptr;

/* should we poll this connector for connects and disconnects */
/* hot plug detectable */
#define DRM_CONNECTOR_POLL_HPD (1 << 0)
/* poll for connections */
#define DRM_CONNECTOR_POLL_CONNECT (1 << 1)
/* can cleanly poll for disconnections without flickering the screen */
/* DACs should rarely do this without a lot of testing */
#define DRM_CONNECTOR_POLL_DISCONNECT (1 << 2)

	/**
	 * @polled:
	 *
	 * Connector polling mode, a combination of
	 *
	 * DRM_CONNECTOR_POLL_HPD
	 *     The connector generates hotplug events and doesn't need to be
	 *     periodically polled. The CONNECT and DISCONNECT flags must not
	 *     be set together with the HPD flag.
	 *
	 * DRM_CONNECTOR_POLL_CONNECT
	 *     Periodically poll the connector for connection.
	 *
	 * DRM_CONNECTOR_POLL_DISCONNECT
	 *     Periodically poll the connector for disconnection.
	 *
	 * Set to 0 for connectors that don't support connection status
	 * discovery.
	 */
	uint8_t polled;

	/* requested DPMS state */
	int dpms;

	const struct drm_connector_helper_funcs *helper_private;

	/* forced on connector */
	struct drm_cmdline_mode cmdline_mode;
	enum drm_connector_force force;
	bool override_edid;

#define DRM_CONNECTOR_MAX_ENCODER 3
	uint32_t encoder_ids[DRM_CONNECTOR_MAX_ENCODER];
	struct drm_encoder *encoder; /* currently active encoder */

#define MAX_ELD_BYTES	128
	/* EDID bits */
	uint8_t eld[MAX_ELD_BYTES];
	bool latency_present[2];
	int video_latency[2];	/* [0]: progressive, [1]: interlaced */
	int audio_latency[2];
	int null_edid_counter; /* needed to workaround some HW bugs where we get all 0s */
	unsigned bad_edid_counter;

	/* Flag for raw EDID header corruption - used in Displayport
	 * compliance testing - * Displayport Link CTS Core 1.2 rev1.1 4.2.2.6
	 */
	bool edid_corrupt;

	struct dentry *debugfs_entry;

	struct drm_connector_state *state;

	/* DisplayID bits */
	bool has_tile;
	struct drm_tile_group *tile_group;
	bool tile_is_single_monitor;

	uint8_t num_h_tile, num_v_tile;
	uint8_t tile_h_loc, tile_v_loc;
	uint16_t tile_h_size, tile_v_size;
};

#define obj_to_connector(x) container_of(x, struct drm_connector, base)

int drm_connector_init(struct drm_device *dev,
		       struct drm_connector *connector,
		       const struct drm_connector_funcs *funcs,
		       int connector_type);
int drm_connector_register(struct drm_connector *connector);
void drm_connector_unregister(struct drm_connector *connector);
int drm_mode_connector_attach_encoder(struct drm_connector *connector,
				      struct drm_encoder *encoder);

void drm_connector_cleanup(struct drm_connector *connector);
static inline unsigned drm_connector_index(struct drm_connector *connector)
{
	return connector->index;
}

/**
 * drm_connector_lookup - lookup connector object
 * @dev: DRM device
 * @id: connector object id
 *
 * This function looks up the connector object specified by id
 * add takes a reference to it.
 */
static inline struct drm_connector *drm_connector_lookup(struct drm_device *dev,
		uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_CONNECTOR);
	return mo ? obj_to_connector(mo) : NULL;
}

/**
 * drm_connector_reference - incr the connector refcnt
 * @connector: connector
 *
 * This function increments the connector's refcount.
 */
static inline void drm_connector_reference(struct drm_connector *connector)
{
	drm_mode_object_reference(&connector->base);
}

/**
 * drm_connector_unreference - unref a connector
 * @connector: connector to unref
 *
 * This function decrements the connector's refcount and frees it if it drops to zero.
 */
static inline void drm_connector_unreference(struct drm_connector *connector)
{
	drm_mode_object_unreference(&connector->base);
}

const char *drm_get_connector_status_name(enum drm_connector_status status);
const char *drm_get_subpixel_order_name(enum subpixel_order order);
const char *drm_get_dpms_name(int val);
const char *drm_get_dvi_i_subconnector_name(int val);
const char *drm_get_dvi_i_select_name(int val);
const char *drm_get_tv_subconnector_name(int val);
const char *drm_get_tv_select_name(int val);

int drm_mode_create_dvi_i_properties(struct drm_device *dev);
int drm_mode_create_tv_properties(struct drm_device *dev,
				  unsigned int num_modes,
				  const char * const modes[]);
int drm_mode_create_scaling_mode_property(struct drm_device *dev);
int drm_mode_create_aspect_ratio_property(struct drm_device *dev);
int drm_mode_create_suggested_offset_properties(struct drm_device *dev);

int drm_mode_connector_set_path_property(struct drm_connector *connector,
					 const char *path);
int drm_mode_connector_set_tile_property(struct drm_connector *connector);
int drm_mode_connector_update_edid_property(struct drm_connector *connector,
					    const struct edid *edid);

/**
 * drm_for_each_connector - iterate over all connectors
 * @connector: the loop cursor
 * @dev: the DRM device
 *
 * Iterate over all connectors of @dev.
 */
#define drm_for_each_connector(connector, dev) \
	for (assert_drm_connector_list_read_locked(&(dev)->mode_config),	\
	     connector = list_first_entry(&(dev)->mode_config.connector_list,	\
					  struct drm_connector, head);		\
	     &connector->head != (&(dev)->mode_config.connector_list);		\
	     connector = list_next_entry(connector, head))

#endif
