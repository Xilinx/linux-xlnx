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

#ifndef __DRM_BRIDGE_H__
#define __DRM_BRIDGE_H__

#include <linux/list.h>
#include <linux/ctype.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_modes.h>

struct drm_bridge;

/**
 * struct drm_bridge_funcs - drm_bridge control functions
 */
struct drm_bridge_funcs {
	/**
	 * @attach:
	 *
	 * This callback is invoked whenever our bridge is being attached to a
	 * &drm_encoder.
	 *
	 * The attach callback is optional.
	 *
	 * RETURNS:
	 *
	 * Zero on success, error code on failure.
	 */
	int (*attach)(struct drm_bridge *bridge);

	/**
	 * @detach:
	 *
	 * This callback is invoked whenever our bridge is being detached from a
	 * &drm_encoder.
	 *
	 * The detach callback is optional.
	 */
	void (*detach)(struct drm_bridge *bridge);

	/**
	 * @mode_fixup:
	 *
	 * This callback is used to validate and adjust a mode. The paramater
	 * mode is the display mode that should be fed to the next element in
	 * the display chain, either the final &drm_connector or the next
	 * &drm_bridge. The parameter adjusted_mode is the input mode the bridge
	 * requires. It can be modified by this callback and does not need to
	 * match mode.
	 *
	 * This is the only hook that allows a bridge to reject a modeset. If
	 * this function passes all other callbacks must succeed for this
	 * configuration.
	 *
	 * The mode_fixup callback is optional.
	 *
	 * NOTE:
	 *
	 * This function is called in the check phase of atomic modesets, which
	 * can be aborted for any reason (including on userspace's request to
	 * just check whether a configuration would be possible). Drivers MUST
	 * NOT touch any persistent state (hardware or software) or data
	 * structures except the passed in @state parameter.
	 *
	 * RETURNS:
	 *
	 * True if an acceptable configuration is possible, false if the modeset
	 * operation should be rejected.
	 */
	bool (*mode_fixup)(struct drm_bridge *bridge,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
	/**
	 * @disable:
	 *
	 * This callback should disable the bridge. It is called right before
	 * the preceding element in the display pipe is disabled. If the
	 * preceding element is a bridge this means it's called before that
	 * bridge's ->disable() function. If the preceding element is a
	 * &drm_encoder it's called right before the encoder's ->disable(),
	 * ->prepare() or ->dpms() hook from struct &drm_encoder_helper_funcs.
	 *
	 * The bridge can assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is still running when this callback is called.
	 *
	 * The disable callback is optional.
	 */
	void (*disable)(struct drm_bridge *bridge);

	/**
	 * @post_disable:
	 *
	 * This callback should disable the bridge. It is called right after
	 * the preceding element in the display pipe is disabled. If the
	 * preceding element is a bridge this means it's called after that
	 * bridge's ->post_disable() function. If the preceding element is a
	 * &drm_encoder it's called right after the encoder's ->disable(),
	 * ->prepare() or ->dpms() hook from struct &drm_encoder_helper_funcs.
	 *
	 * The bridge must assume that the display pipe (i.e. clocks and timing
	 * singals) feeding it is no longer running when this callback is
	 * called.
	 *
	 * The post_disable callback is optional.
	 */
	void (*post_disable)(struct drm_bridge *bridge);

	/**
	 * @mode_set:
	 *
	 * This callback should set the given mode on the bridge. It is called
	 * after the ->mode_set() callback for the preceding element in the
	 * display pipeline has been called already. The display pipe (i.e.
	 * clocks and timing signals) is off when this function is called.
	 */
	void (*mode_set)(struct drm_bridge *bridge,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode);
	/**
	 * @pre_enable:
	 *
	 * This callback should enable the bridge. It is called right before
	 * the preceding element in the display pipe is enabled. If the
	 * preceding element is a bridge this means it's called before that
	 * bridge's ->pre_enable() function. If the preceding element is a
	 * &drm_encoder it's called right before the encoder's ->enable(),
	 * ->commit() or ->dpms() hook from struct &drm_encoder_helper_funcs.
	 *
	 * The display pipe (i.e. clocks and timing signals) feeding this bridge
	 * will not yet be running when this callback is called. The bridge must
	 * not enable the display link feeding the next bridge in the chain (if
	 * there is one) when this callback is called.
	 *
	 * The pre_enable callback is optional.
	 */
	void (*pre_enable)(struct drm_bridge *bridge);

	/**
	 * @enable:
	 *
	 * This callback should enable the bridge. It is called right after
	 * the preceding element in the display pipe is enabled. If the
	 * preceding element is a bridge this means it's called after that
	 * bridge's ->enable() function. If the preceding element is a
	 * &drm_encoder it's called right after the encoder's ->enable(),
	 * ->commit() or ->dpms() hook from struct &drm_encoder_helper_funcs.
	 *
	 * The bridge can assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is running when this callback is called. This
	 * callback must enable the display link feeding the next bridge in the
	 * chain if there is one.
	 *
	 * The enable callback is optional.
	 */
	void (*enable)(struct drm_bridge *bridge);
};

/**
 * struct drm_bridge - central DRM bridge control structure
 * @dev: DRM device this bridge belongs to
 * @encoder: encoder to which this bridge is connected
 * @next: the next bridge in the encoder chain
 * @of_node: device node pointer to the bridge
 * @list: to keep track of all added bridges
 * @funcs: control functions
 * @driver_private: pointer to the bridge driver's internal context
 */
struct drm_bridge {
	struct drm_device *dev;
	struct drm_encoder *encoder;
	struct drm_bridge *next;
#ifdef CONFIG_OF
	struct device_node *of_node;
#endif
	struct list_head list;

	const struct drm_bridge_funcs *funcs;
	void *driver_private;
};

int drm_bridge_add(struct drm_bridge *bridge);
void drm_bridge_remove(struct drm_bridge *bridge);
struct drm_bridge *of_drm_find_bridge(struct device_node *np);
int drm_bridge_attach(struct drm_device *dev, struct drm_bridge *bridge);
void drm_bridge_detach(struct drm_bridge *bridge);

bool drm_bridge_mode_fixup(struct drm_bridge *bridge,
			const struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode);
void drm_bridge_disable(struct drm_bridge *bridge);
void drm_bridge_post_disable(struct drm_bridge *bridge);
void drm_bridge_mode_set(struct drm_bridge *bridge,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode);
void drm_bridge_pre_enable(struct drm_bridge *bridge);
void drm_bridge_enable(struct drm_bridge *bridge);

#endif
