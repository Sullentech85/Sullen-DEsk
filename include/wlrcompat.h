#pragma once

/* wlroots, one series to the next.
 *
 * wlroots changes its API with each series (0.18, 0.19, 0.20 ...), and a
 * program is built against one of them. DEsk is written to the newest it
 * knows and builds against the ones before it too: what differs between them
 * is here, each under the name DEsk calls it by, so the code in src/wm* has
 * no #if of its own for it. Include this after wlroots' own headers (and,
 * for the Xwayland ones, after <wlr/xwayland.h>).
 *
 * To take a new series on: build against it, and for each thing the
 * compiler no longer finds, add its old and new spelling here. */

#include <wlr/version.h>

#define DESK_WLR_AT_LEAST(minor) (WLR_VERSION_NUM >= ((0 << 16) | ((minor) << 8)))

/* ---- 0.19 ----
 *
 * - a surface's window geometry is a field, no longer asked for
 * - wlr_presentation_create() takes the protocol's version
 * - wlr_swapchain_acquire() no longer gives the buffer's age
 * - wlr_surface_accepts_touch() and wlr_surface_accepts_tablet_v2() take
 *   the surface first
 * - a scene output's own fields (pending_commit_damage) are in a struct
 *   wlroots marks private
 * - Xwayland: maximized is set for each direction, and two functions were
 *   renamed */
#if DESK_WLR_AT_LEAST(19)

#define desk_xdg_surface_geometry(surface, box) (*(box) = (surface)->geometry)
#define desk_presentation_create(display, backend) wlr_presentation_create((display), (backend), 2)
#define desk_swapchain_acquire(swapchain) wlr_swapchain_acquire(swapchain)
#define desk_surface_accepts_touch(seat, surface) wlr_surface_accepts_touch((surface), (seat))
#define desk_surface_accepts_tablet(tablet, surface) wlr_surface_accepts_tablet_v2((surface), (tablet))
/* (wlroots builds itself with WLR_PRIVATE defined to nothing; to anybody
 * else the struct is a member of that name) */
#define desk_scene_output_pending_damage(so) (&(so)->WLR_PRIVATE.pending_commit_damage)
#define desk_xwayland_set_maximized(xs, on) wlr_xwayland_surface_set_maximized((xs), (on), (on))
#define desk_xwayland_input_model(xs) wlr_xwayland_surface_icccm_input_model(xs)
#define desk_xwayland_or_wants_focus(xs) wlr_xwayland_surface_override_redirect_wants_focus(xs)

#else

#define desk_xdg_surface_geometry(surface, box) wlr_xdg_surface_get_geometry((surface), (box))
#define desk_presentation_create(display, backend) wlr_presentation_create((display), (backend))
#define desk_swapchain_acquire(swapchain) wlr_swapchain_acquire((swapchain), NULL)
#define desk_surface_accepts_touch(seat, surface) wlr_surface_accepts_touch((seat), (surface))
#define desk_surface_accepts_tablet(tablet, surface) wlr_surface_accepts_tablet_v2((tablet), (surface))
#define desk_scene_output_pending_damage(so) (&(so)->pending_commit_damage)
#define desk_xwayland_set_maximized(xs, on) wlr_xwayland_surface_set_maximized((xs), (on))
#define desk_xwayland_input_model(xs) wlr_xwayland_icccm_input_model(xs)
#define desk_xwayland_or_wants_focus(xs) wlr_xwayland_or_surface_wants_focus(xs)

#endif

/* ---- 0.20 ----
 *
 * - an output's gamma ramps are a colour transform (three 1D tables) set on
 *   its state; wlr_output_state_set_gamma_lut() is gone
 * - wlr_xwayland_set_cursor() takes a buffer, which a cursor image now gives
 * - the text-input manager's signal is new_text_input
 * - wlroots' xdg-shell header no longer brings in the protocol's own
 *   (xdg-shell-protocol.h, for the *_SINCE_VERSION numbers): wm.c includes it */
#if DESK_WLR_AT_LEAST(20)

#include <wlr/render/color.h>
#include <wlr/xcursor.h>

/* n entries each of red, green and blue; n of 0 takes the ramps off. False
 * when the tables could not be made. */
static inline bool desk_output_state_set_gamma(struct wlr_output_state *state, size_t n,
	const uint16_t *r, const uint16_t *g, const uint16_t *b)
{
	if (n == 0) {
		wlr_output_state_set_color_transform(state, NULL);
		return true;
	}
	struct wlr_color_transform *tr = wlr_color_transform_init_lut_3x1d(n, r, g, b);
	if (!tr) {
		return false;
	}
	/* (the state keeps its own reference) */
	wlr_output_state_set_color_transform(state, tr);
	wlr_color_transform_unref(tr);
	return true;
}

#define desk_xwayland_set_cursor_image(xwayland, im) \
	wlr_xwayland_set_cursor((xwayland), wlr_xcursor_image_get_buffer(im), \
		(int32_t)(im)->hotspot_x, (int32_t)(im)->hotspot_y)
#define desk_text_input_new(manager) (&(manager)->events.new_text_input)

#else

static inline bool desk_output_state_set_gamma(struct wlr_output_state *state, size_t n,
	const uint16_t *r, const uint16_t *g, const uint16_t *b)
{
	wlr_output_state_set_gamma_lut(state, n, r, g, b);
	return true;
}

#define desk_xwayland_set_cursor_image(xwayland, im) \
	wlr_xwayland_set_cursor((xwayland), (im)->buffer, (im)->width * 4, (im)->width, \
		(im)->height, (int32_t)(im)->hotspot_x, (int32_t)(im)->hotspot_y)
#define desk_text_input_new(manager) (&(manager)->events.text_input)

#endif
