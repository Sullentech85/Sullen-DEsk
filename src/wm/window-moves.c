/* Part of wm.c: moving a window: the corner's curl, throwing, unfolding, gliding.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static void screens_idle_cb(void *data)
{
	struct desk_server *server = data;
	server->screens_idle = NULL;
	side_desks_paint(server, false);
	windows_onto_screens(server);
	server_schedule_frames(server);
}

/* The screens set out some other way -- one plugged in, one taken away,
 * moved, made another size, turned off or on: once the change is over,
 * the bare desk beside the desk's screen, and the windows that were on a
 * screen that is not there now. */
static void layout_change(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, layout_change);
	(void)data;
	if (server->screens_idle || server->quitting) {
		return;
	}
	server->screens_idle = wl_event_loop_add_idle(wl_display_get_event_loop(server->wl_display),
		screens_idle_cb, server);
}

/* Where a window is on the screens: its middle -- or for one with the
 * whole of a screen, which has no middle of its own, that screen's corner
 * -- with the paper's move (desk_follow) added for one lying on the desk. */
static void view_middle(struct desk_toplevel *view, double *x, double *y)
{
	struct desk_server *server = view->server;
	const struct wlr_scene_node *node = &view->scene_tree->node;
	if (view->fullscreen) {
		*x = node->x + 1.0;
		*y = node->y + 1.0;
		return;
	}
	*x = node->x + view->outer_w * 0.5;
	*y = node->y + view->outer_h * 0.5;
	if (node->parent == server->layer_win) {
		*x += server->desk_dx;
		*y += server->desk_dy + server->win_dy;
	}
}

/* The screen a window is on: the one its middle is on. */
struct wlr_output *view_screen(struct desk_toplevel *view)
{
	if (!view->scene_tree) {
		return first_output(view->server);
	}
	double x, y;
	view_middle(view, &x, &y);
	return screen_at(view->server, x, y);
}

/* What a maximised sheet on screen out covers: on the desk's, the work
 * area, and the drawer's pull under it too -- a strip of wood under a
 * window that fills the desk is a gap, not a handle anybody is reaching
 * for; on a screen beside it, all of that screen. */
static struct wlr_box max_area(struct desk_server *server, struct wlr_output *out)
{
	struct wlr_box wa = screen_area(server, out);
	int h = server->desk_h;
	if (!screen_is_side(server, out) && h >= 8) {
		wa.height = h - wa.y;
	}
	return wa;
}

/* A sheet may hang off the sides or the bottom, but its top edge -- the
 * title bar you move it by -- never goes up into the tray, nor off the
 * top of a screen beside the desk's. x is where across it its middle is
 * (the paper's coordinates, as y). */
static int work_clamp_y(struct desk_server *server, int x, int y)
{
	struct wlr_box wa = screen_area(server, screen_at(server, x + server->desk_dx,
		y + server->desk_dy + server->win_dy));
	return y < wa.y ? wa.y : y;
}

/* Where the whole screen goes in the room. A film or a game in front of
 * everything on the desk -- the drawer's handle, the sheets parked at its
 * edge -- or it is not the whole screen; the drawer pulled out is in front
 * of the game, as you would pull it out over anything; and the bookcase,
 * the Control Panel and the post box, the lock and the lamp going out are
 * in front of it still, being where you look up to from the desk. With
 * nothing on the whole screen, back where it was, over the windows. */
static void fullscreen_layer_order(struct desk_server *server)
{
	bool any = false;
	struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		if (v->fullscreen && v->scene_tree) {
			any = true;
			break;
		}
	}
	bool drawer = server->drawer_moving || server->drawer_t > 0.004;
	struct wlr_scene_tree *over = any && !drawer ? server->layer_parked : server->layer_win;
	wlr_scene_node_place_above(&server->layer_full->node, &over->node);
	/* X11 menus over the game they belong to */
	wlr_scene_node_place_above(&server->layer_xover->node, &server->layer_full->node);
}

/* The whole screen: off the desk altogether, over everything on it, with
 * no frame and no shadow -- a film, a game, a slide show. And back to the
 * sheet it was, where it was, after. */
static void view_set_fullscreen(struct desk_toplevel *view, bool on)
{
	struct desk_server *server = view->server;
	/* the screen it is on, and the whole of that one */
	struct wlr_output *out = view_screen(view);
	if (!view->scene_tree || !out || on == view->fullscreen) {
		goto tell;
	}
	if (on) {
		view_peek_forget(view);
		int l, t, r, b;
		view_insets(view, &l, &t, &r, &b);
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, out, &box);
		if (wlr_box_empty(&box)) {
			box = (struct wlr_box){ 0, 0, out->width, out->height };
		}
		view->fs_x = view->scene_tree->node.x;
		view->fs_y = view->scene_tree->node.y;
		view->fs_inner_w = view->outer_w - l - r;
		view->fs_inner_h = view->outer_h - t - b;
		view->fullscreen = true;
		wlr_scene_node_reparent(&view->scene_tree->node, server->layer_full);
		wlr_scene_node_set_position(&view->scene_tree->node, box.x, box.y);
		view_repaint_chrome(view);
		view_sync_shadow(view);
		view_set_size(view, box.width, box.height);
	} else {
		view->fullscreen = false;
		wlr_scene_node_reparent(&view->scene_tree->node, server->layer_win);
		int w = view->fs_inner_w, h = view->fs_inner_h;
		if (view->maximized) {
			/* opened out: to all of its screen's desk again, whatever size
			 * it had got to on the way (an X window opened out and given
			 * the whole screen as it is mapped had not got there yet) */
			int l, t, r, b;
			view_insets(view, &l, &t, &r, &b);
			struct wlr_box ma = max_area(server, out);
			view->fs_x = ma.x;
			view->fs_y = ma.y;
			w = ma.width - l - r;
			h = ma.height - t - b;
		}
		wlr_scene_node_set_position(&view->scene_tree->node, view->fs_x, view->fs_y);
		view_repaint_chrome(view);
		view_sync_shadow(view);
		view_set_size(view, w, h);
	}
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	fullscreen_layer_order(server);
tell:
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		wlr_xwayland_surface_set_fullscreen(view->xsurface, view->fullscreen);
		return;
	}
#endif
	if (view->xdg_toplevel && view->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, view->fullscreen);
	}
}

/* Opened out, a thing may keep some of the desk on its left, and under it. */
static void view_max_keep_left(const struct desk_toplevel *view, struct wlr_box *box)
{
	const struct desk_thing *thing = view_thing(view);
	if (thing) {
		box->x += thing->open_out_left;
		box->width -= thing->open_out_left;
		box->height -= thing->open_out_below;
	}
}

static void view_toggle_max_now(struct desk_toplevel *view)
{
	struct wlr_output *out = view_screen(view);
	if (!out || !view->scene_tree || view->fullscreen || view_refuses(view, THING_NO_OPEN_OUT)) {
		return;
	}
	view_peek_forget(view);
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	if (!view->maximized) {
		view->saved_x = view->scene_tree->node.x;
		view->saved_y = view->scene_tree->node.y;
		view->saved_inner_w = view->outer_w - l - r;
		view->saved_inner_h = view->outer_h - t - b;
		/* all of the desk that is not the tray: down to the bottom edge,
		 * over the drawer's pull (the drawer still opens with Super+D); on
		 * a screen beside the desk's, all of it */
		struct wlr_box wa = max_area(view->server, out);
		view_max_keep_left(view, &wa);
		int iw = wa.width - l - r;
		int ih = wa.height - t - b;
		if (iw < 200) {
			iw = 200;
		}
		if (ih < 120) {
			ih = 120;
		}
		wlr_scene_node_set_position(&view->scene_tree->node, wa.x, wa.y);
		view_sync_shadow(view);
		view_set_maximized(view, true);
		view_set_size(view, iw, ih);
		view->maximized = 1;
	} else {
		view_set_maximized(view, false);
		wlr_scene_node_set_position(&view->scene_tree->node, view->saved_x,
			work_clamp_y(view->server, view->saved_x + (view->saved_inner_w + l + r) / 2,
				view->saved_y));
		view_set_size(view, view->saved_inner_w, view->saved_inner_h);
		view_sync_shadow(view);
		view->maximized = 0;
	}
}

/* The half of screen out a sheet laid on side (-1 the left, 1 the right)
 * covers, outer, in the paper's coordinates: the half of what an opened-out
 * sheet would, the right one taking the odd pixel. */
static struct wlr_box half_box(struct desk_server *server, struct wlr_output *out, int side)
{
	struct wlr_box wa = max_area(server, out);
	int lw = wa.width / 2;
	return (struct wlr_box){ side < 0 ? wa.x : wa.x + lw, wa.y,
		side < 0 ? lw : wa.width - lw, wa.height };
}

/* The tray is only known for certain once the room is traced, and moves
 * when the screen changes shape. Then: any window that has ended up on it
 * comes down off it, a maximised one fits what there is now, and X is told
 * where windows go (_NET_WORKAREA), for the programs that ask. */
static void windows_keep_off_tray(struct desk_server *server)
{
	struct wlr_box wa = work_area(server);
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (!view->scene_tree || view->fullscreen || view->unmanaged) {
			continue;
		}
		struct wlr_scene_node *node = &view->scene_tree->node;
		/* (one on a screen beside the desk's is clear of the tray, and
		 * opened out to fit that screen) */
		struct wlr_output *out = view_screen(view);
		if (view->maximized) {
			int l, t, r, b;
			view_insets(view, &l, &t, &r, &b);
			struct wlr_box ma = max_area(server, out);
			wlr_scene_node_set_position(node, ma.x, ma.y);
			view_set_size(view, ma.width - l - r, ma.height - t - b);
			view_sync_shadow(view);
		} else if (view->half) {
			/* and one laid on a half, the half there is now */
			int l, t, r, b;
			view_insets(view, &l, &t, &r, &b);
			struct wlr_box hb = half_box(server, out, view->half);
			wlr_scene_node_set_position(node, hb.x, hb.y);
			view_set_size(view, hb.width - l - r, hb.height - t - b);
			view_sync_shadow(view);
			view_send_position(view);
		} else if (!screen_is_side(server, out) || !out) {
			/* all of it on the desk's screen as it is now -- turned the other way up, a window
			 * that lay across the landscape one was half off the portrait one (2026-11-12, the
			 * owner: UIも合わせて動くように…少しはみ出たりしている): put in from the edges, and
			 * asked to be smaller if it is bigger than the screen */
			int x0 = node->x, y0 = node->y;
			if (node->y < wa.y) {
				wlr_scene_node_set_position(node, node->x, wa.y);
			}
			if (view->thing) {
				view_shift_on_screen(view);
			} else {
				view_fit_on_screen(view);
			}
			if (node->x != x0 || node->y != y0) {
				view_sync_shadow(view);
				view_send_position(view);
			}
		}
	}
#ifdef DESK_HAVE_XWAYLAND
	if (server->xwayland && server->xwayland->xwm) {
		wlr_xwayland_set_workareas(server->xwayland, &wa, 1);
	}
#endif
}

/* ---- the corner of a sheet ------------------------------------------
 *
 * The only controls a sheet has: its top right corner, turned down a
 * little, with Fold, Unfold and Discard printed on the back. Near it, the
 * corner lifts; pressing one of them turns the corner the way that
 * instruction would, and then does it. */

static void curl_sizes(const struct desk_toplevel *view, double *rest, double *open)
{
	desk_curl_sizes(view->server->desk_w, view->server->desk_h, rest, open);
	/* never more than a good part of a small sheet */
	double most = 0.46 * fmin(view->outer_w, view->outer_h);
	if (*open > most) {
		*open = most;
	}
	if (*rest > *open * 0.3) {
		*rest = *open * 0.3;
	}
}

static bool curl_wanted(const struct desk_toplevel *view)
{
	/* a sheet that draws its own frame has its own controls, where the
	 * corner would be */
	return view->scene_tree && view_framed(view) && view->sheet.phase == SHEET_IDLE
		&& !view->ua_on && view->outer_w > 60 && view->outer_h > 60 && !view_refuses(view, THING_NO_CORNER);
}

static bool curl_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	struct desk_toplevel *view = buffer->node.data;
	return view && desk_curl_contains(view->curl_box, view->curl_s, *sx, *sy);
}

static void view_curl_reset(struct desk_toplevel *view)
{
	if (view->curl_under) {
		cairo_surface_destroy(view->curl_under);
		view->curl_under = NULL;
	}
	view->curl = NULL;
	view->curl_s = 0.0;
	view->curl_v = 0.0;
	view->curl_at = 0.0;
	view->curl_near = false;
	view->curl_hover_at = 0.0;
	view->curl_hot = -1;
	view->curl_act = -1;
	view->curl_done = false;
	view->curl_key = 0;
}

/* The pixels of a scene buffer we drew ourselves, at its size on screen. */
static void paint_scene_buffer(cairo_t *cr, struct wlr_scene_buffer *sb, double x, double y)
{
	struct wlr_buffer *pic = sb && sb->node.enabled ? scene_pic_buffer(sb) : NULL;
	if (!pic || pic->impl != &cairo_buffer_impl) {
		return;
	}
	struct desk_buf *b = wl_container_of(pic, b, base);
	double bw = cairo_image_surface_get_width(b->surface);
	double bh = cairo_image_surface_get_height(b->surface);
	double dw = sb->dst_width > 0 ? sb->dst_width : bw;
	double dh = sb->dst_height > 0 ? sb->dst_height : bh;
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_scale(cr, dw / bw, dh / bh);
	cairo_set_source_surface(cr, b->surface, 0, 0);
	cairo_paint_with_alpha(cr, sb->opacity);
	cairo_restore(cr);
}

/* What is under a sheet's top right corner: the desk, and every sheet
 * lying beneath this one there -- its shadow, its paper, its own corner,
 * and, once the corner is lifted far enough to see it, what is written on
 * it. Put together once for a lift, and again only if the sheet moves;
 * reading back what another program drew is not a thing to do every frame. */
static cairo_surface_t *curl_underlay(struct desk_toplevel *view, bool full, double sz)
{
	struct desk_server *server = view->server;
	int cx = view->scene_tree->node.x + view->outer_w;
	int cy = view->scene_tree->node.y;
	int c = full ? (int)ceil(fmin(view->outer_w, view->outer_h) * 0.8 + 6.0)
		: (int)ceil(sz * 1.3 + 6.0);
	/* once more for each lift, so it shows what is there now; and back to
	 * the cheap one -- no other program's pixels -- when the corner is down */
	if (view->curl_under && view->curl_under_x == cx && view->curl_under_y == cy
		&& view->curl_under_c >= c && view->curl_under_full == full) {
		return view->curl_under;
	}
	if (view->curl_under) {
		cairo_surface_destroy(view->curl_under);
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, c, c);
	cairo_t *cr = cairo_create(img);
	cairo_translate(cr, -(cx - c), -cy);
	/* in the windows' own layer, which goes up with the desk by just the
	 * drawer's lift, the room lies where it does with the drawer shut --
	 * unless the thing has something of its own under there */
	const struct desk_thing *thing = view_thing(view);
	bool next = thing && thing->under_corner;
	if (next) {
		thing->under_corner(cr, cx, cy, c);
	} else {
		desk_paint_room_at(cr, server->desk_w, server->desk_h, 0.0);
	}
	/* the sheets beneath, bottom first: the list runs top to bottom */
	struct desk_toplevel *below[64];
	int n = 0;
	bool past = next;
	struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		if (next) {
			break;
		}
		if (v == view) {
			past = true;
			continue;
		}
		if (!past || n >= 64 || !v->scene_tree || !v->scene_tree->node.enabled
			|| v->fullscreen || v->scene_tree->node.parent != view->scene_tree->node.parent) {
			continue;
		}
		int vx = v->scene_tree->node.x, vy = v->scene_tree->node.y;
		int pad = DESK_SHADOW + 40;
		if (vx - pad > cx || vx + v->outer_w + pad < cx - c || vy - pad > cy + c
			|| vy + v->outer_h + pad < cy) {
			continue;
		}
		below[n++] = v;
	}
	for (int i = n - 1; i >= 0; i--) {
		v = below[i];
		double tx = v->scene_tree->node.x, ty = v->scene_tree->node.y;
		if (v->shadow.node) {
			paint_scene_buffer(cr, v->shadow.node, tx + v->shadow.node->node.x,
				ty + v->shadow.node->node.y);
		}
		if (v->chrome) {
			paint_scene_buffer(cr, v->chrome, tx + v->chrome->node.x, ty + v->chrome->node.y);
		}
		if (full && v->surface_tree) {
			cairo_save(cr);
			cairo_rectangle(cr, tx, ty, v->outer_w, v->outer_h);
			cairo_clip(cr);
			paint_client(cr, view_surface(v), tx + v->surface_tree->node.x,
				ty + v->surface_tree->node.y);
			cairo_restore(cr);
		}
		if (v->curl) {
			paint_scene_buffer(cr, v->curl, tx + v->curl->node.x, ty + v->curl->node.y);
		}
	}
	cairo_destroy(cr);
	cairo_surface_flush(img);
	view->curl_under = img;
	view->curl_under_x = cx;
	view->curl_under_y = cy;
	view->curl_under_c = c;
	view->curl_under_full = full;
	return img;
}

/* Draw the corner again, if anything it shows has changed -- including
 * where on the desk it is, since what is under it shows where it was. */
static void view_curl_paint(struct desk_toplevel *view)
{
	if (!curl_wanted(view)) {
		if (view->curl) {
			wlr_scene_node_set_enabled(&view->curl->node, false);
		}
		return;
	}
	double rest, open;
	curl_sizes(view, &rest, &open);
	if (view->curl_s <= 0.0) {
		view->curl_s = rest;
	}
	double sz = clampf_d(view->curl_s, 0.0, fmin(view->outer_w, view->outer_h) * 0.8);
	double box = ceil(fmax(sz, 1.0) * 1.12 + 18.0);
	double lift = clampf_d((sz - rest) / fmax(open - rest, 1.0), 0.0, 1.3);
	int bx = view->scene_tree->node.x + view->outer_w - (int)box;
	int by = view->scene_tree->node.y;
	uint64_t key = 14695981039346656037ull;
	int what[6] = { (int)(sz * 4.0), view->curl_hot, view->maximized, bx, by, (int)box };
	key = key_add(key, what, sizeof what);
	if (view->curl && view->curl_key == key) {
		wlr_scene_node_set_enabled(&view->curl->node, true);
		return;
	}
	view->curl_key = key;
	view->curl_box = box;
	struct desk_buf *buf = buf_create((int)box, (int)box);
	/* where the paper was, what is under it */
	bool full = sz > rest * 1.6 || view->curl_near || view->curl_act >= 0;
	cairo_surface_t *under = curl_underlay(view, full, sz);
	if (under && sz > 0.5) {
		cairo_save(buf->cr);
		cairo_move_to(buf->cr, box - sz, 0);
		cairo_line_to(buf->cr, box, 0);
		cairo_line_to(buf->cr, box, sz);
		cairo_close_path(buf->cr);
		cairo_clip(buf->cr);
		cairo_set_source_surface(buf->cr, under, box - view->curl_under_c, 0);
		cairo_paint(buf->cr);
		cairo_restore(buf->cr);
	}
	desk_draw_curl(buf->cr, box, sz, open, lift, view->curl_hot, view->maximized != 0,
		view_title(view));
	scene_put(&view->curl, view->scene_tree, buf, view->outer_w - (int)box, 0, view);
	scene_hold(view->curl);   /* read back under a corner (curl_underlay) */
	view->curl->point_accepts_input = curl_accepts;
	wlr_scene_node_set_enabled(&view->curl->node, true);
}

static void view_toggle_max(struct desk_toplevel *view);

/* Where the pointer is, for every sheet: near the corner of the one it is
 * over, or near none. */
/* How far past the corner the fold can be taken: a finger is wider than a pointer,
 * and it must not take more of what is the program's. */
static double curl_past(const struct desk_toplevel *view)
{
	return view->server->hand_unseen ? 18.0 * desk_ui_scale() : 6.0;
}

static void curl_track(struct desk_server *server, struct desk_toplevel *under,
	double px, double py)
{
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		bool near = view->curl_near;
		bool on_fold = false;
		int hot = -1;
		if (view == under && curl_wanted(view) && view->curl_act < 0) {
			double rest, open;
			curl_sizes(view, &rest, &open);
			double lx = px - view->scene_tree->node.x;
			double ly = py - view->scene_tree->node.y;
			double dx = view->outer_w - lx;
			/* On the fold itself -- not merely near the corner, where the
			 * pointer passes on its way to everything at the top right of
			 * a program -- the corner stirs; held there it turns up
			 * (curls_tick), and a click turns it up at once. Turned up,
			 * it stays so while the pointer is anywhere on its back. */
			double zone = rest * 2.6, past = curl_past(view);
			on_fold = dx >= -past && dx <= zone && ly >= -past && ly <= zone
				&& (!view->own_frame || mount_on_fold(view, dx, ly));
			if (near) {
				near = dx >= -6.0 && dx <= open * 1.30 && ly >= -6.0 && ly <= open * 1.30;
			}
			if (near && view->curl) {
				hot = desk_curl_hit(view->curl_box, view->curl_s, open,
					lx - (view->outer_w - view->curl_box), ly);
			}
		} else {
			near = false;
		}
		if (!near && on_fold) {
			if (view->curl_hover_at <= 0.0) {
				view->curl_hover_at = real_sec();
				server_schedule_frames(server);
			}
		} else {
			view->curl_hover_at = 0.0;
		}
		if (near != view->curl_near || hot != view->curl_hot) {
			view->curl_near = near;
			view->curl_hot = hot;
			view_curl_paint(view);
			server_schedule_frames(server);
		}
	}
}

/* A click on the fold: turned up at once, without the wait. */
static void curl_turn_up(struct desk_toplevel *view)
{
	if (!view->curl_near && view->scene_tree) {
		snd_at(view->server, DESK_SND_CORNER_LIFT,
			view->scene_tree->node.x + view->outer_w, 1.0);
	}
	view->curl_near = true;
	view->curl_hover_at = 0.0;
	server_schedule_frames(view->server);
}

/* One of the instructions on the back, pressed: the corner is turned the
 * way that instruction goes, and then it is done. */
static void curl_press(struct desk_toplevel *view, int item)
{
	if (view->scene_tree) {
		snd_at(view->server, DESK_SND_CORNER_TAP,
			view->scene_tree->node.x + view->outer_w, 1.0);
	}
	view->curl_act = item;
	view->curl_act_at = monotonic_sec();
	view->curl_done = false;
	view->curl_hot = item;
	server_schedule_frames(view->server);
}

/* Once a frame: every corner goes where it is going, on a spring soft
 * enough to be paper -- it goes a little past and comes back. */
static void curls_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	bool more = false;
	struct desk_toplevel *view, *tmp;
	wl_list_for_each_safe(view, tmp, &server->toplevels, link) {
		if (!curl_wanted(view)) {
			if (view->curl) {
				wlr_scene_node_set_enabled(&view->curl->node, false);
			}
			continue;
		}
		double rest, open;
		curl_sizes(view, &rest, &open);
		/* held on the fold long enough: turned up */
		if (!view->curl_near && view->curl_hover_at > 0.0) {
			if (real_sec() - view->curl_hover_at >= 0.5 && view->server->conf.corner_hover) {
				snd_at(server, DESK_SND_CORNER_LIFT,
					view->scene_tree->node.x + view->outer_w, 1.0);
				view->curl_near = true;
				view->curl_hover_at = 0.0;
			} else {
				more = true;
			}
		}
		/* the pointer resting on the fold stirs it, so it can be seen to be
		 * something that lifts */
		double to = view->curl_near ? open : (view->curl_hover_at > 0.0 ? rest * 1.7 : rest);
		if (view->curl_finger > 0.0) {
			/* (where the finger has the tip of it) */
			to = view->curl_s = view->curl_finger;
			view->curl_v = 0.0;
			more = true;
		}
		if (view->curl_act >= 0) {
			double e = now - view->curl_act_at;
			switch (view->curl_act) {
			case DESK_CURL_FOLD:
				/* turned further, as if to fold the sheet in -- then it is */
				to = open * 1.55;
				if (e > 0.20) {
					view->curl_act = -1;
					view->curl_near = false;
					view->curl_hot = -1;
					view->curl_s = rest;
					view->curl_v = 0.0;
					view_fold_toggle(view);
					continue;
				}
				break;
			case DESK_CURL_UNFOLD:
				/* laid flat, and the sheet opens out */
				to = rest * 0.15;
				if (e > 0.16) {
					view->curl_act = -1;
					view->curl_near = false;
					view->curl_hot = -1;
					view_toggle_max(view);
					to = rest;
				}
				break;
			default:
				/* peeled right back, the way a sheet is torn off a pad --
				 * and if the program keeps it (it asks about unsaved work)
				 * the corner goes back down */
				to = open * 1.9;
				if (e > 0.22 && !view->curl_done) {
					view->curl_done = true;
					view_close(view);
					/* it may be gone now; the list is walked safely */
				}
				if (e > 1.10) {
					view->curl_act = -1;
					view->curl_hot = -1;
					to = view->curl_near ? open : rest;
				}
				break;
			}
			more = true;
		}
		double dt = view->curl_at > 0.0 ? clampf_d(now - view->curl_at, 0.0, 0.05) : 0.0;
		view->curl_at = now;
		if (view->curl_s <= 0.0) {
			view->curl_s = rest;
		}
		const double k = 240.0, c = 2.0 * 0.60 * 15.49;
		double acc = k * (to - view->curl_s) - c * view->curl_v;
		view->curl_v += acc * dt;
		view->curl_s += view->curl_v * dt;
		if (gfx_flat()) {
			view->curl_s = to;
			view->curl_v = 0.0;
		}
		if (fabs(to - view->curl_s) < 0.15 && fabs(view->curl_v) < 2.0) {
			view->curl_s = to;
			view->curl_v = 0.0;
		} else {
			more = true;
		}
		/* the words move with the flap, under a pointer that may not */
		if (view->curl_near && view->curl_act < 0 && view->curl_box > 0.0) {
			view->curl_hot = desk_curl_hit(view->curl_box, view->curl_s, open,
				server->cursor->x - view->scene_tree->node.x
					- (view->outer_w - view->curl_box),
				server->cursor->y - view->scene_tree->node.y);
		}
		view_curl_paint(view);
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* ---- throwing a sheet away -------------------------------------------
 *
 * A sheet that goes -- discarded from its corner, closed by a key, or put
 * away by its own program -- is screwed up where it lay and thrown off the
 * nearer side of the desk: it goes up as it goes, so it grows a little and
 * its shadow falls further behind it, and it turns over in the air. */

#define THROW_CRUMPLE 0.42

struct desk_throw {
	struct wl_list link;
	struct desk_crumple *c;
	struct wlr_scene_buffer *node;
	double x0, y0;       /* the sheet's top left, as it lay */
	double cx, cy;       /* and its middle, where the ball ends up */
	double start;
	double dir;          /* -1 off the left, +1 off the right */
	double dist, flight, peak, vy, spin;
	unsigned seed;
	double drawn_at;
	double cost;         /* what drawing it last took */
};

/* Each sheet is screwed up its own way. */
static unsigned throw_seed(void)
{
	static unsigned seed;
	return ++seed;
}

static void throw_free(struct desk_throw *th)
{
	wl_list_remove(&th->link);
	if (th->node) {
		wlr_scene_node_destroy(&th->node->node);
	}
	desk_crumple_free(th->c);
	free(th);
}

/* Called as a window goes, while what it last drew is still there to read. */
static void view_throw_away(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	struct wlr_surface *s = view_surface(view);
	if (view->thing) {
		view->thing_placed = false;
		if (view->thing->goes && !server->quitting && view->scene_tree && view->scene_tree->node.enabled
			&& view->sheet.phase == SHEET_IDLE && s && s->buffer) {
			view->thing->goes(view);
		}
		if (view->thing->forget) {
			view->thing->forget(view);
		}
		if (view->thing->no & THING_NO_CRUMPLE) {
			return;
		}
	}
	if (!server->conf.throw_closed || server->quitting || !view->scene_tree || !view->scene_tree->node.enabled
		|| view->unmanaged || view->sheet.phase != SHEET_IDLE
		|| view->outer_w < 60 || view->outer_h < 60 || !s || !s->buffer) {
		return;
	}
	prof_begin(PROF_THROW);
	cairo_surface_t *snap = view_snapshot(view);
	if (cairo_surface_status(snap) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(snap);
		prof_end();
		return;
	}
	throw_paper(server, snap, view->scene_tree->node.x, view->scene_tree->node.y);
	prof_end();
}

static void throw_flight(struct desk_server *server, struct desk_throw *th, double in);

/* Screw a sheet up where it lies -- snap is what it looks like, its top
 * left at (x0, y0) on the desk -- and throw it off the nearer side. Takes
 * snap. */
static void throw_paper(struct desk_server *server, cairo_surface_t *snap, double x0,
	double y0)
{
	struct desk_throw *th = calloc(1, sizeof *th);
	if (!th) {
		cairo_surface_destroy(snap);
		return;
	}
	th->seed = throw_seed();
	th->c = desk_crumple_new(snap, th->seed * 2654435761u);
	int sw = cairo_image_surface_get_width(snap), sh = cairo_image_surface_get_height(snap);
	cairo_surface_destroy(snap);
	if (!th->c) {
		free(th);
		return;
	}
	th->x0 = x0;
	th->y0 = y0;
	th->cx = th->x0 + sw * 0.5;
	th->cy = th->y0 + sh * 0.5;
	th->start = monotonic_sec();
	/* off whichever side is nearer: nobody throws across the whole desk */
	th->dir = th->cx < server->desk_w * 0.5 ? -1.0 : 1.0;
	wl_list_insert(&server->throws, &th->link);
	snd_at(server, DESK_SND_PAPER_CRUMPLE, th->cx, 1.0);
	throw_flight(server, th, THROW_CRUMPLE);
}

/* How it will fly, and all that will be heard of it, from the moment it is thrown (in). */
static void throw_flight(struct desk_server *server, struct desk_throw *th, double in)
{
	unsigned seed = th->seed;
	double R = desk_crumple_radius(th->c);
	th->dist = (th->dir < 0 ? th->cx : server->desk_w - th->cx) + R * 1.6 + 80.0;
	th->flight = clampf_d(0.34 + th->dist / 2600.0, 0.40, 0.80);
	th->peak = R * 0.9 + 60.0;
	double h = (double)((seed * 40503u) % 1000u) / 1000.0;
	th->vy = -40.0 - 120.0 * h;
	th->spin = th->dir * (5.0 + 3.0 * h);
	/* and all that follows is known now: thrown, and landing off the
	 * edge of the desk */
	snd_for_in(server, DESK_SND_PAPER_TOSS, th->cx, 1.0, 0.42, th->flight, in);
	desk_sound_play_in(DESK_SND_PAPER_LAND, th->dir * 0.9, 1.0, 0.0,
		real_secs(in + th->flight) + SND_SEEN);
	server_schedule_frames(server);
}

static void throws_tick_body(struct desk_server *server);

static void throws_tick(struct desk_server *server)
{
	if (wl_list_empty(&server->throws)) {
		return;
	}
	prof_begin(PROF_THROW);
	throws_tick_body(server);
	prof_end();
}

/* A sheet being screwed up, its top left at (x0, y0): where on the desk that lands (bx, by, and
 * the size it is shown, dw by dh). */
static struct desk_buf *crumple_picture(struct desk_crumple *c, double u, double x0, double y0,
	int *bx, int *by, int *dw, int *dh)
{
	bool ultra = desk_gfx_level() == DESK_GFX_ULTRA;
	double ex0, ey0, ex1, ey1;
	desk_crumple_extent(c, u, &ex0, &ey0, &ex1, &ey1);
	double bw = ex1 - ex0, bh = ey1 - ey0;
	double k = u < 0.06 || ultra ? 1.0
		: clampf_d(sqrt(150000.0 / fmax(bw * bh, 1.0)), 0.4, 1.0);
	/* and at Low, smaller again (motion_res) */
	k *= motion_res();
	int w = (int)ceil(bw * k), h = (int)ceil(bh * k);
	struct desk_buf *buf = buf_create(w > 1 ? w : 1, h > 1 ? h : 1);
	cairo_scale(buf->cr, k, k);
	cairo_translate(buf->cr, -ex0, -ey0);
	desk_crumple_draw(buf->cr, c, u);
	*bx = (int)floor(x0 + ex0);
	*by = (int)floor(y0 + ey0);
	*dw = (int)ceil(bw);
	*dh = (int)ceil(bh);
	return buf;
}

/* ---- a sheet screwed up in the hand --------------------------------------
 *
 * As far as the fingers have closed, and it goes where they go. Let go of with them closed far
 * enough, the program is told; and when its window goes the ball is thrown from the hand. A
 * program that keeps its window, to ask about unsaved work, has it back: the ball opens out. */

static void hand_crumple_end(struct desk_server *server)
{
	struct desk_hand_crumple *hc = server->crumple;
	server->crumple = NULL;
	if (hc->view && hc->view->scene_tree) {
		wlr_scene_node_set_enabled(&hc->view->scene_tree->node, true);
	}
	if (hc->node) {
		wlr_scene_node_destroy(&hc->node->node);
	}
	desk_crumple_free(hc->c);
	free(hc);
	server_schedule_frames(server);
}

/* Its window has gone: thrown, from where the hand has it. */
static void hand_crumple_thrown(struct desk_server *server)
{
	struct desk_hand_crumple *hc = server->crumple;
	struct desk_throw *th = calloc(1, sizeof *th);
	if (!th) {
		hc->view = NULL;
		hand_crumple_end(server);
		return;
	}
	server->crumple = NULL;
	th->c = hc->c;
	th->node = hc->node;
	th->seed = throw_seed();
	th->x0 = hc->x0 + hc->dx;
	th->y0 = hc->y0 + hc->dy;
	th->cx = th->x0 + hc->sw * 0.5;
	th->cy = th->y0 + hc->sh * 0.5;
	th->start = monotonic_sec() - hc->u * THROW_CRUMPLE;
	/* the way the hand went, or off whichever side is nearer */
	th->dir = fabs(hc->vx) > 400.0 ? (hc->vx < 0.0 ? -1.0 : 1.0)
		: (th->cx < server->desk_w * 0.5 ? -1.0 : 1.0);
	wl_list_insert(&server->throws, &th->link);
	throw_flight(server, th, (1.0 - fmin(hc->u, 1.0)) * THROW_CRUMPLE);
	free(hc);
}

static void hand_crumple_tick(struct desk_server *server)
{
	struct desk_hand_crumple *hc = server->crumple;
	if (!hc) {
		return;
	}
	double now = monotonic_sec();
	double dt = clampf_d(now - hc->at, 0.0, 0.05);
	hc->at = now;
	if (hc->closed_at > 0.0 && !hc->back) {
		/* on, as it would be; and if the window stays, the program has kept it */
		hc->u = fmin(hc->u + dt / THROW_CRUMPLE, 1.0);
		if (now - hc->closed_at > 0.9) {
			hc->back = true;
			hc->v = 0.0;
		}
	}
	if (hc->back) {
		hc->v += 7.0 * dt;
		hc->u -= hc->v * dt;
		if (hc->u <= 0.0) {
			hand_crumple_end(server);
			return;
		}
	}
	server_schedule_frames(server);
	double every = desk_gfx_level() == DESK_GFX_ULTRA && hc->cost < 0.010 ? 0.0 : 0.030;
	if (hc->node && now - hc->drawn_at < every) {
		return;
	}
	hc->drawn_at = now;
	int bx, by, dw, dh;
	struct desk_buf *buf = crumple_picture(hc->c, clampf_d(hc->u, 0.0, 1.0), hc->x0 + hc->dx,
		hc->y0 + hc->dy, &bx, &by, &dw, &dh);
	hc->cost = monotonic_sec() - now;
	scene_put(&hc->node, server->layer_parked, buf, bx, by, NULL);
	wlr_scene_buffer_set_dest_size(hc->node, dw, dh);
	wlr_scene_node_raise_to_top(&hc->node->node);
}

static void throws_tick_body(struct desk_server *server)
{
	double now = monotonic_sec();
	bool more = false;
	struct desk_throw *th, *tmp;
	wl_list_for_each_safe(th, tmp, &server->throws, link) {
		double t = now - th->start;
		struct desk_buf *buf;
		int bx, by;
		int dw = 0, dh = 0;
		if (t < THROW_CRUMPLE) {
			/* Screwed up where it lay: thirty times a second, which is
			 * plenty for paper being crushed -- and, once it has begun
			 * to buckle and nobody can read it, drawn at a size the
			 * compositor scales up, since every facet is a picture
			 * turned and stretched, and that costs by the pixel. Not at
			 * Ultra, which is the picture whole however it moves: drawn
			 * full size, and every frame where a frame's worth of it
			 * leaves room for the rest (a big sheet is 25 ms and more on
			 * a laptop, and goes on at thirty). */
			more = true;
			double every = desk_gfx_level() == DESK_GFX_ULTRA && th->cost < 0.010 ? 0.0 : 0.030;
			if (th->node && now - th->drawn_at < every) {
				continue;
			}
			th->drawn_at = now;
			buf = crumple_picture(th->c, t / THROW_CRUMPLE, th->x0, th->y0, &bx, &by, &dw, &dh);
			th->cost = monotonic_sec() - now;
		} else {
			/* and thrown */
			double p = (t - THROW_CRUMPLE) / th->flight;
			if (p >= 1.0) {
				/* landed, off the edge of the desk (heard: asked for
				 * when it was thrown) */
				throw_free(th);
				continue;
			}
			/* away from the hand quickly, and then just going */
			double s = p < 0.2 ? p * p / 0.4 : p - 0.1;
			s /= 0.9;
			double z = th->peak * sin(3.14159265358979 * p * 0.85);
			double scale = 1.0 + z / 700.0;
			double x = th->cx + th->dir * th->dist * s;
			double y = th->cy + th->vy * (t - THROW_CRUMPLE);
			double R = desk_crumple_radius(th->c) * scale;
			int m = (int)ceil(R * 1.30 + z * 0.65 + 34.0);
			double k = motion_res();
			int pm = (int)ceil(m * 2 * k);
			buf = buf_create(pm, pm);
			if (pm != m * 2) {
				cairo_scale(buf->cr, (double)pm / (m * 2), (double)pm / (m * 2));
			}
			desk_crumple_ball(buf->cr, th->c, m, m, scale,
				th->spin * (t - THROW_CRUMPLE), z);
			bx = (int)floor(x) - m;
			by = (int)floor(y) - m;
			dw = dh = m * 2;
		}
		scene_put(&th->node, server->layer_parked, buf, bx, by, NULL);
		wlr_scene_buffer_set_dest_size(th->node, dw, dh);
		wlr_scene_node_raise_to_top(&th->node->node);
		more = true;
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* ---- making a sheet bigger or smaller ---------------------------------
 *
 * The rest of every sheet is folded under it at the edges. Pull an edge
 * and the paper comes out from under, round a turn of paper at the edge,
 * with the creases it was folded with; push it and the paper goes back
 * under the same way. The paper follows the hand at once; the program
 * draws into the new size when it can, and until then what it has drawn
 * stays put against the far edge, trimmed to the paper. */

/* The size of what the program has drawn: for an X program what it has
 * committed, not what it has been asked for. */
static void view_drawn_size(struct desk_toplevel *view, struct wlr_box *geo, int *w, int *h)
{
	view_geometry(view, geo);
	*w = geo->width;
	*h = geo->height;
	struct wlr_surface *s = view_surface(view);
	if (view->xsurface && s) {
		*w = s->current.width;
		*h = s->current.height;
		geo->x = geo->y = 0;
	}
}

static void view_sizing_place(struct desk_toplevel *view)
{
	if (!view->sizing || !view->scene_tree || !view->surface_tree) {
		return;
	}
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	struct wlr_box geo;
	int cw, ch;
	view_drawn_size(view, &geo, &cw, &ch);
	int nx = view->scene_tree->node.x, ny = view->scene_tree->node.y;
	/* against the edge that is not moving -- or, opened out or folded
	 * back, where it was until the program has redrawn it */
	int cx = (view->sz_edges & WLR_EDGE_LEFT) ? view->sz_x1 - cw : view->sz_x0;
	int cy = (view->sz_edges & WLR_EDGE_TOP) ? view->sz_y1 - ch : view->sz_y0;
	if (view->sz_hold) {
		if (cw == view->sz_x1 - view->sz_x0 && ch == view->sz_y1 - view->sz_y0) {
			view->sz_hold = false;
		} else {
			cx = view->sz_hx;
			cy = view->sz_hy;
		}
	}
	/* (the scene's node is at the window geometry's corner already) */
	wlr_scene_node_set_position(&view->surface_tree->node, cx - nx, cy - ny);
	/* trimmed to the paper: past its edge it is folded under */
	int vx0 = cx > view->sz_x0 ? cx : view->sz_x0;
	int vy0 = cy > view->sz_y0 ? cy : view->sz_y0;
	int vx1 = cx + cw < view->sz_x1 ? cx + cw : view->sz_x1;
	int vy1 = cy + ch < view->sz_y1 ? cy + ch : view->sz_y1;
	if (vx1 <= vx0 || vy1 <= vy0) {
		vx1 = vx0 + 1;
		vy1 = vy0 + 1;
	}
	if (vx0 == cx && vy0 == cy && vx1 == cx + cw && vy1 == cy + ch) {
		view_clip_rest(view, &geo);
	} else {
		struct wlr_box clip = { geo.x + vx0 - cx, geo.y + vy0 - cy, vx1 - vx0, vy1 - vy0 };
		struct wlr_scene_node *content = view_content_node(view);
		if (content) {
			wlr_scene_subsurface_tree_set_clip(content, &clip);
		}
	}

	if (view_refuses(view, THING_NO_CREASES)) {
		return;
	}
	/* and over it all, the creases and the turn at the edge */
	int ow = view->outer_w, oh = view->outer_h;
	double c0x = vx0 - nx, c0y = vy0 - ny, c1x = vx1 - nx, c1y = vy1 - ny;
	/* where the program's drawing reaches the paper's edge, no paper has
	 * come out on that side */
	if (vx0 <= view->sz_x0) {
		c0x = 0;
	}
	if (vy0 <= view->sz_y0) {
		c0y = 0;
	}
	if (vx1 >= view->sz_x1) {
		c1x = ow;
	}
	if (vy1 >= view->sz_y1) {
		c1y = oh;
	}
	/* opened out: the four creases it came out along, as it did */
	bool opened = view->sz_hold && view->sz_opened;
	if (opened) {
		c0x = view->sz_was.x - nx;
		c0y = view->sz_was.y - ny;
		c1x = c0x + view->sz_was.width;
		c1y = c0y + view->sz_was.height;
	}
	uint64_t key = 14695981039346656037ull;
	int what[9] = { ow, oh, (int)c0x, (int)c0y, (int)c1x, (int)c1y, (int)view->sz_edges,
		(int)(view->sz_lip * 12.0 + 0.5), opened };
	key = key_add(key, what, sizeof what);
	if (view->sz_over && view->sz_key == key) {
		return;
	}
	view->sz_key = key;
	const int m = 24;
	/* drawn again every time the hand moves: drawn smaller and stretched */
	int fw = ow + m * 2, fh = oh + m * 2;
	double k = motion_res();
	int pw = (int)ceil(fw * k), ph = (int)ceil(fh * k);
	struct desk_buf *buf = buf_create(pw, ph);
	if (pw != fw || ph != fh) {
		cairo_scale(buf->cr, (double)pw / fw, (double)ph / fh);
	}
	cairo_translate(buf->cr, m, m);
	if (opened) {
		desk_draw_unfolded_creases(buf->cr, ow, oh, c0x, c0y, c1x, c1y);
	} else {
		desk_draw_sizing(buf->cr, ow, oh, c0x, c0y, c1x, c1y, view->sz_edges, view->sz_lip,
			view->sz_seed);
	}
	scene_put(&view->sz_over, view->scene_tree, buf, -m, -m, NULL);
	wlr_scene_buffer_set_dest_size(view->sz_over, fw, fh);
	view->sz_over->point_accepts_input = shadow_accepts;
	wlr_scene_node_place_above(&view->sz_over->node, &view->surface_tree->node);
	wlr_scene_node_set_enabled(&view->sz_over->node, true);
}

static void view_sizing_apply(struct desk_toplevel *view);

/* Done: the program has caught up, or will not. The sheet is the size of
 * what it drew again. */
static void view_sizing_stop(struct desk_toplevel *view)
{
	if (!view->sizing) {
		return;
	}
	if (view->sz_glide) {
		/* stopped on the way: where it was going, at once (the program
		 * was asked for that already) */
		view->sz_glide = false;
		view->sz_x0 = view->sz_to.x;
		view->sz_y0 = view->sz_to.y;
		view->sz_x1 = view->sz_to.x + view->sz_to.width;
		view->sz_y1 = view->sz_to.y + view->sz_to.height;
		view->sz_dirty = true;
	}
	if (view->scene_tree) {
		view_sizing_apply(view);
	}
	view->sizing = false;
	view->sz_hold = false;
	view->sz_opened = false;
	if (view->sz_over) {
		wlr_scene_node_destroy(&view->sz_over->node);
		view->sz_over = NULL;
	}
	if (!view->scene_tree || !view->surface_tree) {
		return;
	}
	struct wlr_box rgeo;
	view_geometry(view, &rgeo);
	view_clip_rest(view, &rgeo);
	/* where the paper's top left is now, the program's drawing starts */
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	struct wlr_box geo;
	int cw, ch;
	view_drawn_size(view, &geo, &cw, &ch);
	int x = (view->sz_edges & WLR_EDGE_LEFT) ? view->sz_x1 - cw : view->sz_x0;
	int y = (view->sz_edges & WLR_EDGE_TOP) ? view->sz_y1 - ch : view->sz_y0;
	wlr_scene_node_set_position(&view->scene_tree->node, x - l, y - t);
	view->chrome_key = 0;
	view_repaint_chrome(view);
	view_sync_shadow(view);
	view_send_position(view);
}

static void view_sizing_move(struct desk_toplevel *view, int x0, int y0, int x1, int y1,
	unsigned edges)
{
	struct desk_server *server = view->server;
	double now = monotonic_sec();
	if (view->sz_glide) {
		view_sizing_stop(view);
	}
	if (!view->sizing) {
		view->sizing = true;
		view->sz_lip = 0.0;
		view->sz_at = now;
		view->sz_seed = (unsigned)(now * 1000.0);
		view->sz_x0 = x0;
		view->sz_y0 = y0;
		view->sz_x1 = x1;
		view->sz_y1 = y1;
	}
	view->sz_hold = false;
	view->sz_opened = false;
	/* how hard it is being pulled: how far the edge went since last time */
	int moved = abs(x0 - view->sz_x0) + abs(x1 - view->sz_x1)
		+ abs(y0 - view->sz_y0) + abs(y1 - view->sz_y1);
	double dt = fmax(now - view->sz_at, 0.008);
	view->sz_lip_to = clampf_d(0.50 + moved / dt / 1600.0, 0.50, 1.0);
	view->sz_at = now;
	view->sz_released = 0.0;
	view->sz_edges = edges;
	view->sz_x0 = x0;
	view->sz_y0 = y0;
	view->sz_x1 = x1;
	view->sz_y1 = y1;
	/* The pointer can report a hundred times a second; the paper is drawn
	 * and the program asked once a frame, with wherever it has got to. */
	view->sz_dirty = true;
	server_schedule_frames(server);
}

static void view_sizing_apply(struct desk_toplevel *view)
{
	if (!view->sz_dirty) {
		return;
	}
	view->sz_dirty = false;
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	wlr_scene_node_set_position(&view->scene_tree->node, view->sz_x0 - l, view->sz_y0 - t);
	view_set_size(view, view->sz_x1 - view->sz_x0, view->sz_y1 - view->sz_y0);
	view_repaint_chrome(view);
	view_sync_shadow(view);
}

/* ---- a sheet laid on a half, or taken off it --------------------------
 *
 * Not put there at once: its edges are pulled there by themselves, the
 * paper coming out from under them or going back under, as when a hand
 * pulls them (view_sizing_place) -- a third of a second, quick to start
 * and settling into place. The program is asked for the new size at the
 * start, once, and its drawing stays on the paper, at its top left, until
 * it has drawn the new size. */

#define HALF_GLIDE 0.32

static int glide_at(int a, int b, double e)
{
	return (int)lround(a + (b - a) * e);
}

/* where the paper has got to: e of the way, 0 to 1 */
static void view_glide_place(struct desk_toplevel *view, double e)
{
	struct wlr_box f = view->sz_from, to = view->sz_to;
	view->sz_x0 = glide_at(f.x, to.x, e);
	view->sz_y0 = glide_at(f.y, to.y, e);
	view->sz_x1 = glide_at(f.x + f.width, to.x + to.width, e);
	view->sz_y1 = glide_at(f.y + f.height, to.y + to.height, e);
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	wlr_scene_node_set_position(&view->scene_tree->node, view->sz_x0 - l, view->sz_y0 - t);
	view_repaint_chrome(view);
	view_sync_shadow(view);
}

/* Lay the sheet's paper over to (outer, on the screen), gliding there. */
static void view_glide_to(struct desk_toplevel *view, struct wlr_box to)
{
	struct desk_server *server = view->server;
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	struct wlr_box from = { view->scene_tree->node.x + l, view->scene_tree->node.y + t,
		view->outer_w - l - r, view->outer_h - t - b };
	struct wlr_box in = { to.x + l, to.y + t, to.width - l - r, to.height - t - b };
	/* the program asked, where it will be */
	wlr_scene_node_set_position(&view->scene_tree->node, to.x, to.y);
	view_set_size(view, in.width, in.height);
	bool same = from.x == in.x && from.y == in.y && from.width == in.width
		&& from.height == in.height;
	if (gfx_flat() || !view_framed(view) || !view->surface_tree || same) {
		view_sync_shadow(view);
		return;
	}
	double now = monotonic_sec();
	view->sizing = true;
	view->sz_glide = true;
	view->sz_glide_at = now;
	view->sz_from = from;
	view->sz_to = in;
	/* the drawing goes with the paper's top left corner, as it is read
	 * from there, and the paper comes out or goes under at the right and
	 * the bottom (held against an edge that moves, the drawing jumped
	 * when the program drew the new size) */
	view->sz_edges = WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
	view->sz_hold = false;
	view->sz_opened = false;
	view->sz_lip = view->sz_lip_to = 0.0;
	view->sz_at = now;
	view->sz_tick_at = 0.0;
	view->sz_released = 0.0;
	view->sz_seed = (unsigned)(now * 1000.0);
	view->sz_dirty = false;
	view->sz_key = 0;
	view->chrome_key = 0;
	view->shadow.settled = 0;
	view_glide_place(view, 0.0);
	server_schedule_frames(server);
}

/* Once a frame, for a sheet gliding: true while it still is. At the end it
 * is let go of where it is laid, and waits for the program as a sheet whose
 * edge has been let go of does (sizing_tick). */
static bool view_glide_tick(struct desk_toplevel *view, double now)
{
	double u = clampf_d((now - view->sz_glide_at) / HALF_GLIDE, 0.0, 1.0);
	/* quick away, and slowing into place: the paper is pulled hard, and
	 * its turn at the edge goes as the pull does */
	double e = 1.0 - pow(1.0 - u, 3.0);
	view->sz_lip = 0.85 * sin(3.14159265358979 * sqrt(u));
	if (u >= 1.0) {
		view->sz_glide = false;
		view->sz_released = real_sec();   /* waiting on a program: real time */
		view->sz_lip_to = 0.0;
	}
	view_glide_place(view, e);
	return view->sz_glide;
}

static void sizing_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	bool more = false;
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (!view->sizing) {
			continue;
		}
		if (view->sz_glide) {
			view_glide_tick(view, now);
			view->sz_tick_at = now;
			more = true;
			continue;
		}
		view_sizing_apply(view);
		double dt = view->sz_tick_at > 0.0 ? clampf_d(now - view->sz_tick_at, 0.0, 0.05) : 0.016;
		view->sz_tick_at = now;
		double to = view->sz_lip_to;
		if (view->sz_released > 0.0) {
			to = 0.0;
		} else if (now - view->sz_at > 0.15) {
			/* held still: the turn relaxes, but the edge is still in hand */
			to = 0.45;
		}
		view->sz_lip += (to - view->sz_lip) * (gfx_flat() ? 1.0 : clampf_d(dt * 12.0, 0.0, 1.0));
		if (view->sz_released > 0.0 && view->sz_lip < 0.02) {
			struct wlr_box geo;
			int cw, ch;
			view_drawn_size(view, &geo, &cw, &ch);
			bool caught_up = cw == view->sz_x1 - view->sz_x0 && ch == view->sz_y1 - view->sz_y0;
			/* a program asked to fill the desk may take a while about it */
			double wait = view->sz_hold ? 1.5 : 0.6;
			if (caught_up || real_sec() - view->sz_released > wait) {
				view_sizing_stop(view);
				server_schedule_frames(server);
				continue;
			}
		}
		view_sizing_place(view);
		more = true;
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* The pointer's shape for where it is on a sheet's edge, WLR_EDGE_ bits. */
static void cursor_for_edges(struct desk_server *server, unsigned e)
{
	const char *name = "default";
	if (e == (WLR_EDGE_TOP | WLR_EDGE_LEFT)) {
		name = "top_left_corner";
	} else if (e == (WLR_EDGE_TOP | WLR_EDGE_RIGHT)) {
		name = "top_right_corner";
	} else if (e == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)) {
		name = "bottom_left_corner";
	} else if (e == (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT)) {
		name = "bottom_right_corner";
	} else if (e & WLR_EDGE_TOP) {
		name = "top_side";
	} else if (e & WLR_EDGE_BOTTOM) {
		name = "bottom_side";
	} else if (e & WLR_EDGE_LEFT) {
		name = "left_side";
	} else if (e & WLR_EDGE_RIGHT) {
		name = "right_side";
	}
	cursor_image(server, name);
}

/* ---- opening a sheet out to the desk, and folding it back ------------ */

static void ua_live(struct desk_toplevel *view, bool on)
{
	if (view->chrome) {
		wlr_scene_node_set_enabled(&view->chrome->node, on && view_framed(view));
	}
	if (view->surface_tree) {
		wlr_scene_node_set_enabled(&view->surface_tree->node, on);
	}
	/* the corner comes back where the sheet's corner is now, which is for
	 * view_curl_paint to say */
	if (view->curl && !on) {
		wlr_scene_node_set_enabled(&view->curl->node, false);
	}
	if (view->shadow.node) {
		wlr_scene_node_set_enabled(&view->shadow.node->node, on && !gfx_flat());
	}
}

static void view_unfold_cancel(struct desk_toplevel *view)
{
	if (!view->ua_on) {
		return;
	}
	view->ua_on = false;
	view->ua_held = view->ua_free = false;
	if (view->ua_base) {
		wlr_scene_node_destroy(&view->ua_base->node);
		view->ua_base = NULL;
	}
	for (int i = 0; i < 4; i++) {
		if (view->ua_edge[i]) {
			wlr_scene_node_destroy(&view->ua_edge[i]->node);
			view->ua_edge[i] = NULL;
		}
	}
	if (view->ua_img) {
		cairo_surface_destroy(view->ua_img);
		view->ua_img = NULL;
	}
	ua_live(view, true);
}

#define UA_PHASE_DUR 0.50

/* The part of the sheet that is flat through a phase -- inside the creases
 * this phase opens or closes, less the edges that lift -- and the edges. */
static void ua_phase_setup(struct desk_toplevel *view, int phase)
{
	struct desk_server *server = view->server;
	struct wlr_box from = view->ua_from, to = view->ua_to;
	bool sides = (view->ua_dir > 0) == (phase == 0);
	/* the rectangle inside this phase's creases, and outside them */
	struct wlr_box in, out;
	if (view->ua_dir > 0) {
		in = phase == 0 ? from : (struct wlr_box){ to.x, from.y, to.width, from.height };
		out = phase == 0 ? (struct wlr_box){ to.x, from.y, to.width, from.height } : to;
	} else {
		in = phase == 0 ? (struct wlr_box){ from.x, to.y, from.width, to.height } : to;
		out = phase == 0 ? from : (struct wlr_box){ from.x, to.y, from.width, to.height };
	}
	struct wlr_box flat = in;
	for (int i = 0; i < 4; i++) {
		struct desk_unfold_edge *e = &view->ua_e[i];
		memset(e, 0, sizeof *e);
		e->side = i;
		e->sheet = e->panel = view->ua_img;
		e->sheet_x = e->panel_x = view->ua_img_box.x;
		e->sheet_y = e->panel_y = view->ua_img_box.y;
		e->vx = server->desk_w * 0.5;
		e->vy = server->desk_h * 0.5;
		e->dist = server->desk_h * 1.05;
		double across;
		if (i == 0) {
			e->crease = in.x + in.width;
			e->len = out.x + out.width - e->crease;
		} else if (i == 1) {
			e->crease = in.x;
			e->len = in.x - out.x;
		} else if (i == 2) {
			e->crease = in.y + in.height;
			e->len = out.y + out.height - e->crease;
		} else {
			e->crease = in.y;
			e->len = in.y - out.y;
		}
		if (i < 2) {
			e->t0 = in.y;
			e->t1 = in.y + in.height;
			across = in.width;
		} else {
			e->t0 = in.x;
			e->t1 = in.x + in.width;
			across = in.height;
		}
		/* how much of the sheet by the crease comes up with it: more for a
		 * longer panel, never much of a small sheet */
		e->bend = floor(fmin(fmin(e->len * 0.34 + 36.0, 170.0), across * 0.28));
		view->ua_e_on[i] = ((i < 2) == sides) && e->len >= 12.0 && e->bend >= 12.0;
		if (!view->ua_e_on[i]) {
			if (view->ua_edge[i]) {
				wlr_scene_node_set_enabled(&view->ua_edge[i]->node, false);
			}
			continue;
		}
		if (i == 0) {
			flat.width -= (int)e->bend;
		} else if (i == 1) {
			flat.x += (int)e->bend;
			flat.width -= (int)e->bend;
		} else if (i == 2) {
			flat.height -= (int)e->bend;
		} else {
			flat.y += (int)e->bend;
			flat.height -= (int)e->bend;
		}
	}
	/* The shadow under what is lying flat; what is in the air throws its
	 * own. The sheet's node is moved there -- its frame and its program's
	 * drawing are away until the end -- without the shadow swinging. */
	wlr_scene_node_set_position(&view->scene_tree->node, in.x, in.y);
	view->outer_w = in.width;
	view->outer_h = in.height;
	view->shadow.settled = 0;
	view_sync_shadow(view);
	/* the flat part: a window onto the picture of the sheet */
	if (view->ua_base && flat.width > 0 && flat.height > 0) {
		struct wlr_fbox src = { flat.x - view->ua_img_box.x, flat.y - view->ua_img_box.y,
			flat.width, flat.height };
		scene_source(view->ua_base, &src);
		wlr_scene_buffer_set_dest_size(view->ua_base, flat.width, flat.height);
		wlr_scene_node_set_position(&view->ua_base->node, flat.x - in.x, flat.y - in.y);
		wlr_scene_node_set_enabled(&view->ua_base->node, true);
	} else if (view->ua_base) {
		wlr_scene_node_set_enabled(&view->ua_base->node, false);
	}
	view->ua_phase = phase;
	view->ua_start = monotonic_sec();
}

static bool ua_phase_empty(const struct desk_toplevel *view)
{
	for (int i = 0; i < 4; i++) {
		if (view->ua_e_on[i]) {
			return false;
		}
	}
	return true;
}

static void ua_paint(struct desk_toplevel *view, double u)
{
	int nx = view->scene_tree->node.x, ny = view->scene_tree->node.y;
	for (int i = 0; i < 4; i++) {
		if (!view->ua_e_on[i]) {
			continue;
		}
		const struct desk_unfold_edge *e = &view->ua_e[i];
		double x0, y0, x1, y1;
		desk_unfold_edge_extent(e, u, &x0, &y0, &x1, &y1);
		int bx = (int)floor(x0), by = (int)floor(y0);
		int bw = (int)ceil(x1) - bx, bh = (int)ceil(y1) - by;
		if (bw < 1 || bh < 1) {
			continue;
		}
		/* in motion the whole time: drawn smaller and stretched */
		double k = motion_res();
		int pw = (int)ceil(bw * k), ph = (int)ceil(bh * k);
		struct desk_buf *buf = buf_create(pw, ph);
		if (pw != bw || ph != bh) {
			cairo_scale(buf->cr, (double)pw / bw, (double)ph / bh);
		}
		cairo_translate(buf->cr, -bx, -by);
		desk_draw_unfold_edge(buf->cr, e, u);
		scene_put(&view->ua_edge[i], view->scene_tree, buf, bx - nx, by - ny, NULL);
		wlr_scene_buffer_set_dest_size(view->ua_edge[i], bw, bh);
		view->ua_edge[i]->point_accepts_input = shadow_accepts;
		wlr_scene_node_set_enabled(&view->ua_edge[i]->node, true);
		wlr_scene_node_raise_to_top(&view->ua_edge[i]->node);
	}
}

static void view_unfold_finish(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	struct wlr_box to = view->ua_to, from = view->ua_from;
	int dir = view->ua_dir;
	view_unfold_cancel(view);
	/* the program is asked for the new size now, and its drawing stays
	 * where it was on the paper until it has redrawn it */
	wlr_scene_node_set_position(&view->scene_tree->node, to.x, to.y);
	view->outer_w = to.width;
	view->outer_h = to.height;
	view->sizing = true;
	view->sz_x0 = to.x + l;
	view->sz_y0 = to.y + t;
	view->sz_x1 = to.x + to.width - r;
	view->sz_y1 = to.y + to.height - b;
	view->sz_edges = 0;
	view->sz_hold = true;
	view->sz_hx = from.x + l;
	view->sz_hy = from.y + t;
	view->sz_opened = dir > 0;
	view->sz_was = from;
	view->sz_lip = view->sz_lip_to = 0.0;
	view->sz_at = monotonic_sec();
	view->sz_released = real_sec();   /* waiting on a program: real time */
	view->sz_seed = (unsigned)(view->sz_at * 1000.0);
	view->sz_dirty = false;
	view->chrome_key = 0;
	view->shadow.settled = 0;
	view_set_maximized(view, dir > 0);
	view_set_size(view, view->sz_x1 - view->sz_x0, view->sz_y1 - view->sz_y0);
	view_repaint_chrome(view);
	view_sync_shadow(view);
	view_curl_paint(view);
	server_schedule_frames(server);
}

/* Let go of with the sheet not far enough: back as it was, and nothing has happened. */
static void view_unfold_abort(struct desk_toplevel *view)
{
	struct wlr_box from = view->ua_from;
	int dir = view->ua_dir;
	view_unfold_cancel(view);
	wlr_scene_node_set_position(&view->scene_tree->node, from.x, from.y);
	view->outer_w = from.width;
	view->outer_h = from.height;
	view->maximized = dir < 0;
	view->chrome_key = 0;
	view->shadow.settled = 0;
	view_repaint_chrome(view);
	view_sync_shadow(view);
	view_curl_paint(view);
	server_schedule_frames(view->server);
}

/* Of all the way, a second, a second */
#define UA_FALLS 3.0

static void unfold_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	bool more = false;
	struct desk_toplevel *view, *tmp;
	wl_list_for_each_safe(view, tmp, &server->toplevels, link) {
		if (!view->ua_on) {
			continue;
		}
		if (view->ua_held || view->ua_free) {
			/* as far along as the fingers have it, each phase half of that; or let go of,
			 * going on the way it was going */
			if (view->ua_free) {
				double dt = clampf_d(now - view->ua_at, 0.0, 0.05);
				view->ua_at = now;
				view->ua_v += (view->ua_to_end ? UA_FALLS : -UA_FALLS) * dt;
				view->ua_p += view->ua_v * dt;
				if (view->ua_p >= 1.0) {
					view->ua_free = false;
					view_unfold_finish(view);
					continue;
				}
				if (view->ua_p <= 0.0) {
					view->ua_free = false;
					view_unfold_abort(view);
					continue;
				}
			}
			double q = clampf_d(view->ua_p, 0.0, 0.9999) * 2.0;
			int phase = q >= 1.0 ? 1 : 0;
			if (phase != view->ua_phase) {
				ua_phase_setup(view, phase);
			}
			double local = q - phase;
			ua_paint(view, view->ua_dir > 0 ? local : 1.0 - local);
			more = view->ua_free || more;
			continue;
		}
		double t = (now - view->ua_start) / UA_PHASE_DUR;
		while (t >= 1.0 || ua_phase_empty(view)) {
			if (view->ua_phase == 1) {
				view_unfold_finish(view);
				break;
			}
			ua_phase_setup(view, 1);
			t = 0.0;
		}
		if (!view->ua_on) {
			continue;
		}
		double u = view->ua_dir > 0 ? t : 1.0 - t;
		ua_paint(view, clampf_d(u, 0.0, 1.0));
		more = true;
	}
	if (more) {
		server_schedule_frames(server);
	}
}

static bool box_within(struct wlr_box in, struct wlr_box out)
{
	return in.x >= out.x && in.y >= out.y && in.x + in.width <= out.x + out.width
		&& in.y + in.height <= out.y + out.height;
}

/* The green instruction on the back of the corner, a program asking, or a
 * key: open the sheet out to the desk, or fold it back to what it was. */
static void view_toggle_max(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (!view->scene_tree || view->fullscreen || view->ua_on || view_refuses(view, THING_NO_OPEN_OUT)) {
		return;
	}
	if (!view_framed(view) || view->sheet.phase != SHEET_IDLE || !first_output(server)) {
		view_toggle_max_now(view);
		return;
	}
	view_peek_forget(view);
	view_sizing_stop(view);
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	struct wlr_box cur = { view->scene_tree->node.x, view->scene_tree->node.y,
		view->outer_w, view->outer_h };
	struct wlr_box from, to;
	int dir;
	if (!view->maximized) {
		dir = 1;
		from = cur;
		/* as view_toggle_max_now has it: over the drawer's pull, or all of
		 * a screen beside the desk's */
		to = max_area(server, view_screen(view));
		view_max_keep_left(view, &to);
	} else {
		dir = -1;
		from = cur;
		int sw = view->saved_inner_w + l + r;
		to = (struct wlr_box){ view->saved_x,
			work_clamp_y(server, view->saved_x + sw / 2, view->saved_y),
			sw, view->saved_inner_h + t + b };
	}
	/* Only a sheet that is opening out, or folding back into itself, can
	 * be shown doing it; anything else just goes there. */
	if (!(dir > 0 ? box_within(from, to) : box_within(to, from))) {
		view_toggle_max_now(view);
		return;
	}
	if (dir > 0) {
		view->saved_x = cur.x;
		view->saved_y = cur.y;
		view->saved_inner_w = cur.width - l - r;
		view->saved_inner_h = cur.height - t - b;
		view->maximized = 1;
	} else {
		view->maximized = 0;
	}
	view->ua_dir = dir;
	view->ua_from = from;
	view->ua_to = to;

	/* The picture everything is drawn from. Opening out: plain paper the
	 * size of the desk with the sheet as it is laid on it, and the creases
	 * where it was folded under. Folding back: the sheet as it is. */
	struct wlr_box big = dir > 0 ? to : from;
	struct desk_buf *buf = buf_create(big.width, big.height);
	cairo_surface_t *snap = view_snapshot(view);
	if (dir > 0) {
		/* the same paper the sheet's frame will be, so nothing changes
		 * under it when the program takes over */
		desk_draw_blank_sheet(buf->cr, 0, 0, big.width, big.height, view_title(view));
		cairo_set_source_surface(buf->cr, snap, from.x - big.x, from.y - big.y);
		cairo_paint(buf->cr);
		double x0 = from.x - big.x, y0 = from.y - big.y;
		desk_draw_unfolded_creases(buf->cr, big.width, big.height,
			x0, y0, x0 + from.width, y0 + from.height);
	} else {
		cairo_set_source_surface(buf->cr, snap, 0, 0);
		cairo_paint(buf->cr);
	}
	cairo_surface_destroy(snap);
	cairo_surface_flush(buf->surface);
	view->ua_img = cairo_surface_reference(buf->surface);
	view->ua_img_box = big;
	scene_put(&view->ua_base, view->scene_tree, buf, 0, 0, NULL);
	view->ua_base->point_accepts_input = shadow_accepts;

	view->ua_on = true;
	ua_live(view, false);
	ua_phase_setup(view, 0);
	ua_paint(view, dir > 0 ? 0.0 : 1.0);
	server_schedule_frames(server);
}

/* ---- a sheet on one half of the screen ---------------------------------
 *
 * Super+Left or Super+Right, or a sheet let go of with the pointer at the
 * left or right edge of its screen: it is laid over that half, top to
 * bottom, as an opened-out one is over all of it -- two side by side, to
 * work from both. The same key again puts it back as it was; so does
 * picking it up and moving it, at the size it was, under the hand.
 * (After dentarthurdent's window-snap, in the AiO v0.7.) */

#define HALF_EDGE 18        /* how near the edge of its screen it is let go of */
#define HALF_MIN_W 100      /* and a half too narrow for a window is none */
#define HALF_MIN_H 90

static void view_lay_half(struct desk_toplevel *view, int side, struct wlr_output *out)
{
	struct desk_server *server = view->server;
	if (!view->scene_tree || !out || view->fullscreen || view->unmanaged || view->ua_on
		|| view->sheet.phase != SHEET_IDLE || view_refuses(view, THING_NO_HALF)) {
		return;
	}
	view_sizing_stop(view);
	view_peek_forget(view);
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	/* The half it is on stays its own through the whole screen, an opened-
	 * out sheet and a fold, which all come back to it; but opened out, the
	 * key lays it on that half again rather than putting it back. */
	if (view->half == side && !view->maximized) {
		view->half = 0;
		int w = view->half_inner_w + l + r;
		view_glide_to(view, (struct wlr_box){ view->half_x,
			work_clamp_y(server, view->half_x + w / 2, view->half_y),
			w, view->half_inner_h + t + b });
	} else {
		struct wlr_box hb = half_box(server, out, side);
		if (hb.width - l - r < HALF_MIN_W || hb.height - t - b < HALF_MIN_H) {
			return;
		}
		/* what it goes back to: as it was before it was laid on either
		 * half, or opened out */
		if (view->maximized) {
			if (!view->half) {
				view->half_x = view->saved_x;
				view->half_y = view->saved_y;
				view->half_inner_w = view->saved_inner_w;
				view->half_inner_h = view->saved_inner_h;
			}
			view->maximized = 0;
			view_set_maximized(view, false);
		} else if (!view->half) {
			view->half_x = view->scene_tree->node.x;
			view->half_y = view->scene_tree->node.y;
			view->half_inner_w = view->outer_w - l - r;
			view->half_inner_h = view->outer_h - t - b;
		}
		view->half = side;
		view_glide_to(view, hb);
	}
	if (!view->sz_glide) {
		view_send_position(view);
	}
	server_schedule_frames(server);
}

/* A sheet on a half picked up and moved: back to the size it was, held
 * where across it the hand had it. */
static void view_half_lift(struct desk_server *server, struct desk_toplevel *view)
{
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	double across = view->outer_w > 1 ? server->grab_x / view->outer_w : 0.5;
	across = clampf_d(across, 0.05, 0.95);
	server->grab_x = across * (view->half_inner_w + l + r);
	view->half = 0;
	view_set_size(view, view->half_inner_w, view->half_inner_h);
}

/* A sheet being moved let go of: at the left or right edge of a screen --
 * an edge with no screen beyond it, which the hand is going over to -- it
 * is laid on that half. */
static void move_release(struct desk_server *server)
{
	struct desk_toplevel *view = server->grabbed;
	if (server->cursor_mode != DESK_CUR_MOVE || !view || !view->scene_tree) {
		return;
	}
	double x = server->cursor->x, y = server->cursor->y;
	if (fabs(x - server->move_from_x) < 8.0 && fabs(y - server->move_from_y) < 8.0) {
		return;
	}
	struct wlr_output *out = wlr_output_layout_output_at(server->output_layout, x, y);
	if (!out) {
		return;
	}
	struct wlr_box sb;
	wlr_output_layout_get_box(server->output_layout, out, &sb);
	int side = 0;
	if (x <= sb.x + HALF_EDGE
		&& !wlr_output_layout_output_at(server->output_layout, sb.x - 1, y)) {
		side = -1;
	} else if (x >= sb.x + sb.width - 1 - HALF_EDGE
		&& !wlr_output_layout_output_at(server->output_layout, sb.x + sb.width, y)) {
		side = 1;
	}
	/* a thing that has its own way with an edge: the front of the desk too */
	const struct desk_thing *thing = view_thing(view);
	if (thing && thing->at_edge) {
		if (!side && y >= sb.y + sb.height - 1 - HALF_EDGE
			&& !wlr_output_layout_output_at(server->output_layout, x, sb.y + sb.height)) {
			side = 2;
		}
		if (side) {
			thing->at_edge(view, side);
		}
		return;
	}
	if (side) {
		/* let go at an edge it is on already: it stays there */
		if (view->half == side) {
			return;
		}
		view_lay_half(view, side, out);
	}
}

/* ---- a sheet lifted off the drawer's pull -----------------------------
 *
 * A sheet down to the front edge of the desk -- opened out, laid on a half,
 * pulled down there -- lies over the drawer's pull, and the drawer is then
 * Super+D or nothing (GitHub #40). A click on its bottom edge lifts that
 * edge off the pull, as far up as a window is put down (the work area);
 * a click on it again lays it back down. A press there that moves pulls
 * the edge, as it always has. (After dentarthurdent's desk-peek, in the
 * AiO v0.7, which lifted it 20 px.) */

#define PEEK_DRAG 4.0       /* how far a press goes before it is a pull */
#define PEEK_MIN_H 120      /* and how short a sheet is not made */

/* Is there a pull under its bottom edge to lift it off, or is it lifted? */
static bool view_peek_able(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (!view->scene_tree || !view_framed(view) || view->unmanaged || view->ua_on
		|| view->sizing || view->sheet.phase != SHEET_IDLE) {
		return false;
	}
	if (view->peek_inner_h) {
		return true;
	}
	/* (on a screen beside the desk's, there is no drawer) */
	struct wlr_output *out = view_screen(view);
	if (!out || screen_is_side(server, out)) {
		return false;
	}
	struct wlr_box wa = work_area(server);
	int bottom = view->scene_tree->node.y + view->outer_h;
	return bottom > wa.y + wa.height + 2 && bottom <= server->desk_h + 4;
}

static void view_peek(struct desk_toplevel *view)
{
	if (!view_peek_able(view)) {
		return;
	}
	struct desk_server *server = view->server;
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	int iw = view->outer_w - l - r, ih = view->outer_h - t - b;
	int to;
	if (view->peek_inner_h && abs(ih - view->peek_to_h) <= 4) {
		/* down on the pull again, as it was */
		to = view->peek_inner_h;
		view_peek_forget(view);
	} else {
		/* (lifted, and made another size since: lifted from where it is) */
		view_peek_forget(view);
		struct wlr_box wa = work_area(server);
		int lift = view->scene_tree->node.y + view->outer_h - (wa.y + wa.height);
		if (lift <= 0 || ih - lift < PEEK_MIN_H) {
			return;
		}
		to = ih - lift;
		view->peek_inner_h = ih;
		view->peek_to_h = to;
	}
	/* the edge moved as a hand pulling it would, let go of at once */
	int x0 = view->scene_tree->node.x + l, y0 = view->scene_tree->node.y + t;
	view_sizing_move(view, x0, y0, x0 + iw, y0 + to, WLR_EDGE_BOTTOM);
	view->sz_released = real_sec();
	server_schedule_frames(server);
}
