/* Part of wm.c: fingers, hands and the pen.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static struct desk_finger *finger_of(struct desk_server *server, int32_t id)
{
	for (int i = 0; i < DESK_FINGERS; i++) {
		if (server->fingers[i].on && server->fingers[i].id == id) {
			return &server->fingers[i];
		}
	}
	return NULL;
}

static bool fingers_on(const struct desk_server *server)
{
	for (int i = 0; i < DESK_FINGERS; i++) {
		if (server->fingers[i].on) {
			return true;
		}
	}
	return false;
}

/* What the finger has moved to, or put down on. */
static void hand_to(struct desk_server *server, double x, double y, uint32_t time)
{
	wlr_cursor_warp_closest(server->cursor, NULL, x, y);
	process_cursor_motion(server, time);
}

static void hand_button(struct desk_server *server, uint32_t button, bool down, uint32_t time)
{
	struct wlr_pointer_button_event ev = {
		.pointer = NULL,
		.time_msec = time,
		.button = button,
		.state = down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED,
	};
	pointer_button(server, &ev);
}

/* The hand lifted: nothing is under it now. A corner it turned up stays up. */
static void hand_gone(struct desk_server *server)
{
	server->hand_on = false;
	if (server->touch_timer) {
		wl_event_source_timer_update(server->touch_timer, 0);
	}
	desk_hover_off(server);
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		view->curl_hover_at = 0.0;
	}
	wlr_seat_pointer_clear_focus(server->seat);
}

/* ---- the drawer on a finger ---------------------------------------------- */

static void hand_drawer_begin(struct desk_server *server, struct desk_finger *f)
{
	server->hand_work = HAND_DRAWER;
	server->hand_t0 = server->drawer_t;
	server->hand_y0 = f->y;
	card_gone(server);
	server->drawer_held = true;
	server->drawer_free = false;
	server->drawer_moving = 1;
	server->drawer_to = server->drawer_t > 0.5 ? 0.0 : 1.0;
	/* its runs as fast as the finger takes it (drawer_snd_motion), from nothing */
	desk_sound_stop(server->snd_drawer, 0.04);
	server->snd_drawer = 0;
	snd_forget(&server->snd_drawer_stop);
	drawer_snd_end(server, 0.04);
	server->drun_out = server->drawer_to > 0.5;
	drawer_snd_motion(server);
	server->drw_dirty = true;
	drawer_compose(server);
	fullscreen_layer_order(server);
}

/* The desk stays under the finger: however the drawer's way out is eased, it is drawn out as far
 * as the finger has come. Past its stop it gives only a little. */
static void hand_drawer_move(struct desk_server *server, const struct desk_finger *f)
{
	double full = drawer_lift_at(server, 1.0);
	double want = drawer_lift_at(server, server->hand_t0) + (server->hand_y0 - f->y);
	if (want > full && full > 1.0) {
		server->drawer_t = fmin(1.0 + (want - full) * 0.12 / full, 1.03);
	} else {
		server->drawer_t = drawer_t_of_lift(server, want);
	}
	drawer_snd_motion(server);
	server_schedule_frames(server);
}

/* Let go: on to the end it was going to, or the nearer one, as fast as the finger had it. */
static void hand_drawer_lift(struct desk_server *server, double vy)
{
	double full = drawer_lift_at(server, 1.0);
	double t = server->drawer_t;
	double seen = t;
	if (full > 1.0) {
		seen += -vy / full * 0.18;
	}
	double now = monotonic_sec();
	server->drawer_held = false;
	server->drawer_free = true;
	server->drawer_from = t;
	server->drawer_to = seen > 0.5 ? 1.0 : 0.0;
	server->drawer_x = fmin(drawer_lift_at(server, fmin(t, 1.0)), full);
	server->drawer_v = clampf_d(-vy, -DRAWER_FASTEST_PT * desk_ui_scale(),
		DRAWER_FASTEST_PT * desk_ui_scale());
	server->drawer_at = now;
	server->drawer_start = now;
	server->drawer_dur = 0.3;
	if (server->drawer_to < 0.5) {
		server->drw_hover = -1;
	}
	snd_forget(&server->snd_drawer_stop);
	server_schedule_frames(server);
}

/* ---- a look round on a finger -------------------------------------------- */

#define LOOK_DOWN_FULL 0.60   /* pulled down from the top: of the screen's height, all the way */

/* Each look is a camera on its way somewhere, and what it does to the desk on the screen is no
 * straight line. So how far the desk has gone at each twentieth of the way is measured: the finger
 * says how far the desk is to have gone, and that says how far along the look is. */
static void look_measure(struct desk_server *server, int which)
{
	double *a = server->look_along;
	double last = 0.0, slope = 0.0;
	int have = -1;
	if (server->look_down && which != DESK_VIEW_SHELF) {
		/* Pulled down from the top of the desk: the desk goes off sideways as the look goes round,
		 * and a long way, which no finger coming down the screen could follow. The look is all
		 * the way round when the finger has come down three fifths of the screen. */
		for (int i = 0; i <= 20; i++) {
			a[i] = server->desk_h * LOOK_DOWN_FULL * i / 20.0;
		}
		return;
	}
	for (int i = 0; i <= 20; i++) {
		double fx = 0.0, fy = 0.0;
		desk_tilt_desk_offset(server->desk_w, server->desk_h, which, i / 20.0, &fx, &fy);
		double v = which == DESK_VIEW_SHELF ? fy : which == DESK_VIEW_CONSOLE ? fx : -fx;
		/* (off the film's own measure the desk is out of sight: as it was going) */
		if (i == 0 || ((fx != 0.0 || fy != 0.0) && v > last)) {
			if (have >= 0) {
				slope = (v - last) / (i - have);
			}
			a[i] = v;
			last = v;
			have = i;
		} else {
			a[i] = -1.0;
		}
	}
	if (have < 2) {
		/* (no room to measure, drawn flat: as if it went at one rate) */
		double full = (which == DESK_VIEW_SHELF ? server->desk_h : server->desk_w) * 0.7;
		for (int i = 0; i <= 20; i++) {
			a[i] = full * i / 20.0;
		}
		return;
	}
	for (int i = 1; i <= 20; i++) {
		if (a[i] < 0.0) {
			/* a gap in the middle, or the end of it */
			int j = i + 1;
			while (j <= 20 && a[j] < 0.0) {
				j++;
			}
			a[i] = j <= 20 ? a[i - 1] + (a[j] - a[i - 1]) / (j - i + 1) : a[i - 1] + slope;
		}
	}
}

static double look_along_at(const struct desk_server *server, double t)
{
	double f = clampf_d(t, 0.0, 1.0) * 20.0;
	int i = (int)f;
	if (i >= 20) {
		return server->look_along[20];
	}
	return server->look_along[i] + (server->look_along[i + 1] - server->look_along[i]) * (f - i);
}

static double look_t_at(const struct desk_server *server, double along)
{
	const double *a = server->look_along;
	if (along <= a[0]) {
		return 0.0;
	}
	for (int i = 0; i < 20; i++) {
		if (along <= a[i + 1]) {
			return (i + (along - a[i]) / fmax(a[i + 1] - a[i], 1e-6)) / 20.0;
		}
	}
	return 1.0;
}

/* Turned round as far as the finger has come. Turning sets up all there is to set up, as ever,
 * and then the look goes no further by itself. */
static void hand_look_begin(struct desk_server *server, struct desk_finger *f, int which)
{
	double t;
	if (which == DESK_VIEW_SHELF) {
		shelf_toggle(server);
		t = server->shelf_moving ? server->shelf_t : -1.0;
	} else if (which == DESK_VIEW_CONSOLE) {
		console_toggle(server);
		t = server->cc_moving ? server->cc_t : -1.0;
	} else {
		post_toggle(server);
		t = server->pb_moving ? server->pb_t : -1.0;
	}
	if (t < 0.0) {
		/* nothing to turn to yet */
		server->hand_work = HAND_DONE;
		return;
	}
	look_measure(server, which);
	server->look_held = which + 1;
	server->look_along0 = look_along_at(server, t);
	server->hand_x0 = f->x;
	server->hand_y0 = f->y;
	server->hand_work = HAND_LOOK;
}

static void hand_look_move(struct desk_server *server, const struct desk_finger *f)
{
	int which = server->look_held - 1;
	double d = which == DESK_VIEW_SHELF || server->look_down ? f->y - server->hand_y0
		: which == DESK_VIEW_CONSOLE ? f->x - server->hand_x0 : server->hand_x0 - f->x;
	double t = look_t_at(server, server->look_along0 + d);
	if (which == DESK_VIEW_SHELF) {
		server->shelf_t = t;
	} else if (which == DESK_VIEW_CONSOLE) {
		server->cc_t = t;
	} else {
		server->pb_t = t;
	}
	server_schedule_frames(server);
}

/* Let go: on, if it was going that way or has come far enough, and back if not; and at the speed
 * it was going, which is the finger's. */
static void hand_look_lift(struct desk_server *server, double vx, double vy)
{
	int which = server->look_held - 1;
	server->look_held = 0;
	double *t, *to;
	if (which == DESK_VIEW_SHELF) {
		t = &server->shelf_t;
		to = &server->shelf_to;
	} else if (which == DESK_VIEW_CONSOLE) {
		t = &server->cc_t;
		to = &server->cc_to;
	} else {
		t = &server->pb_t;
		to = &server->pb_to;
	}
	double v = which == DESK_VIEW_SHELF || server->look_down ? vy
		: which == DESK_VIEW_CONSOLE ? vx : -vx;
	server->look_down = false;
	double along = look_along_at(server, *t), full = server->look_along[20];
	double seen = along + v * 0.16;
	bool on = *to > 0.5 ? seen > full * 0.25 : seen < full * 0.75;
	if (!on) {
		*to = 1.0 - *to;
	}
	/* (the look's measure a second, from where it is) */
	double f = clampf_d(*t, 0.0, 0.9999) * 20.0;
	int i = (int)f;
	double per = (server->look_along[i + 1] - server->look_along[i]) * 20.0;
	server->look_free = which + 1;
	server->look_v = clampf_d(per > 1e-6 ? v / per : 0.0, -6.0, 6.0);
	server->look_at = monotonic_sec();
	server_schedule_frames(server);
}

/* What is still going by itself, a finger stops where it is and has on it. */
static bool hand_catches(struct desk_server *server, struct desk_finger *f)
{
	int which = -1;
	double t = 0.0;
	if (server->shelf_moving && server->shelf_binder < 0) {
		which = DESK_VIEW_SHELF;
		t = server->shelf_t;
	} else if (server->cc_moving) {
		which = DESK_VIEW_CONSOLE;
		t = server->cc_t;
	} else if (server->pb_moving) {
		which = DESK_VIEW_POST;
		t = server->pb_t;
	}
	if (which >= 0) {
		server->look_down = false;
		look_measure(server, which);
		server->look_free = 0;
		server->look_held = which + 1;
		server->look_along0 = look_along_at(server, t);
		server->hand_x0 = f->x;
		server->hand_y0 = f->y;
		server->hand_work = HAND_LOOK;
		return true;
	}
	if (server->drawer_moving) {
		hand_drawer_begin(server, f);
		return true;
	}
	return false;
}

/* ---- a sheet thrown ------------------------------------------------------ */

/* A sheet thrown slides on the wood: the wood takes the same off its speed every second, and the air
 * a little more when it is fast. It comes up against the end of the desk and comes back a little. */
#define THROW_WOOD_PT 2400.0
#define THROW_AIR 0.9
#define THROW_COMES_BACK 0.3
/* The speed a hand has to be going to throw one, and the most, in points a second */
#define THROW_SPEED_PT 500.0
#define THROW_MOST_PT 4000.0

static void paper_throw(struct desk_server *server, struct desk_toplevel *view, double vx, double vy)
{
	double pt = desk_ui_scale();
	double speed = hypot(vx, vy);
	if (speed < THROW_SPEED_PT * pt || !view->scene_tree || view->half || view->maximized
		|| view->fullscreen) {
		return;
	}
	double most = THROW_MOST_PT * pt;
	if (speed > most) {
		vx *= most / speed;
		vy *= most / speed;
	}
	server->throw_view = view;
	server->throw_x = view->scene_tree->node.x;
	server->throw_y = view->scene_tree->node.y;
	server->throw_vx = vx;
	server->throw_vy = vy;
	server->throw_at = monotonic_sec();
	server_schedule_frames(server);
}

/* A window has gone: nothing has hold of it, and a ball in the hand is thrown. */
static void fingers_forget_view(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (server->throw_view == view) {
		server->throw_view = NULL;
	}
	if (server->curl_view == view) {
		server->curl_view = NULL;
	}
	if (server->whole_view == view) {
		server->whole_view = NULL;
		server->whole_work = WHOLE_NOTHING;
	}
	if (server->crumple && server->crumple->view == view) {
		hand_crumple_thrown(server);
	}
}

static void paper_throw_tick(struct desk_server *server)
{
	struct desk_toplevel *view = server->throw_view;
	if (!view) {
		return;
	}
	if (!view->scene_tree) {
		server->throw_view = NULL;
		return;
	}
	double now = monotonic_sec();
	double dt = clampf_d(now - server->throw_at, 0.0, 0.05);
	server->throw_at = now;
	server->throw_x += server->throw_vx * dt;
	server->throw_y += server->throw_vy * dt;
	double speed = hypot(server->throw_vx, server->throw_vy);
	double lose = THROW_WOOD_PT * desk_ui_scale() * dt + THROW_AIR * speed * dt;
	if (speed <= lose) {
		server->throw_view = NULL;
		speed = 0.0;
	} else {
		double keep = (speed - lose) / speed;
		server->throw_vx *= keep;
		server->throw_vy *= keep;
	}
	struct wlr_box all;
	wlr_output_layout_get_box(server->output_layout, NULL, &all);
	/* (enough of it left to take hold of) */
	double reach = 80.0 * desk_ui_scale();
	double x0 = all.x + reach - view->outer_w, x1 = all.x + all.width - reach;
	if (server->throw_x < x0 || server->throw_x > x1) {
		server->throw_x = clampf_d(server->throw_x, x0, x1);
		server->throw_vx *= -THROW_COMES_BACK;
	}
	int x = (int)lround(server->throw_x);
	int y = work_clamp_y(server, x + view->outer_w / 2, (int)lround(server->throw_y));
	/* and at the foot of the lowest screen under it, as at the sides: not on down off it (a
	 * screen under the desk's, as on a ZenBook Duo, is the desk going on) */
	double mid = x + view->outer_w * 0.5 + server->desk_dx;
	int foot = all.y + all.height;
	bool under = false;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		struct wlr_box ob;
		if (o->off || !wlr_output_layout_get(server->output_layout, o->wlr_output)) {
			continue;
		}
		wlr_output_layout_get_box(server->output_layout, o->wlr_output, &ob);
		if (mid >= ob.x && mid < ob.x + ob.width) {
			foot = under ? (ob.y + ob.height > foot ? ob.y + ob.height : foot) : ob.y + ob.height;
			under = true;
		}
	}
	int y1 = (int)lround(foot - server->desk_dy - server->win_dy - reach);
	if (y > y1) {
		y = y1;
	}
	if (y != (int)lround(server->throw_y)) {
		server->throw_y = y;
		server->throw_vy *= -THROW_COMES_BACK;
	}
	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
	view_sync_shadow(view);
	view_send_position(view);
	if (server->throw_view) {
		server_schedule_frames(server);
	}
}

/* ---- a corner on a finger ------------------------------------------------ */

/* The tip of the corner is where the finger is, so the fold lies half way to it. */
static void hand_curl_move(struct desk_server *server, const struct desk_finger *f)
{
	struct desk_toplevel *view = server->curl_view;
	if (!view || !view->scene_tree) {
		return;
	}
	double rest, open;
	curl_sizes(view, &rest, &open);
	double dx = view->scene_tree->node.x + view->outer_w - f->x;
	double dy = f->y - view->scene_tree->node.y;
	view->curl_finger = clampf_d((dx + dy) * 0.5, rest, open * 1.3);
	view->curl_at = monotonic_sec();
	server_schedule_frames(server);
}

/* Let go: up, if it has been brought up far enough to read what is on its back. */
static void hand_curl_lift(struct desk_server *server, double vx, double vy)
{
	struct desk_toplevel *view = server->curl_view;
	server->curl_view = NULL;
	if (!view || !view->scene_tree) {
		return;
	}
	double rest, open;
	curl_sizes(view, &rest, &open);
	bool up = view->curl_finger >= rest + (open - rest) * 0.25;
	view->curl_finger = 0.0;
	view->curl_v = clampf_d((vy - vx) * 0.5, -1500.0, 1500.0);
	if (up && !view->curl_near) {
		snd_at(server, DESK_SND_CORNER_LIFT, view->scene_tree->node.x + view->outer_w, 1.0);
	}
	view->curl_near = up;
	view->curl_hot = -1;
	view_curl_paint(view);
	server_schedule_frames(server);
}

/* ---- where a finger is going, over nothing ------------------------------- */

static bool hand_has_sheets(const struct desk_server *server)
{
	return post_up(server) || (shelf_up(server)
		&& (memo_up(server) || cal_up(server) || server->shelf_binder >= 0));
}

/* What the desk has that a hand could take hold of. */
static bool hand_on_something(struct desk_server *server)
{
	if (window_under(server) || post_paper_under(server) >= 0
		|| parked_sheet_at(server, server->cursor->x, server->cursor->y)) {
		return true;
	}
	if (card_out(server)) {
		return true;
	}
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->hand_here && (*t)->hand_here(server)) {
			return true;
		}
	}
	return clock_track(server, true) || slips_track(server, true);
}

/* What a finger set down here has of a sheet, off the program on it. The head of a sheet is a
 * line to a finger, so it reaches a way past the top edge, and picks the sheet up; past the other
 * edges, and in its margins, it pulls them, and all the more at a corner. */
static struct desk_toplevel *paper_under_finger(struct desk_server *server, double x, double y,
	unsigned *edges)
{
	double pt = desk_ui_scale();
	double past = TOUCH_PAST_EDGE_PT * pt, corner = TOUCH_CORNER_PT * pt;
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (!view->scene_tree || !view->scene_tree->node.enabled || view->fullscreen
			|| view->unmanaged || !view_framed(view)) {
			continue;
		}
		const struct desk_thing *thing = view_thing(view);
		if (thing && thing->press) {
			continue;
		}
		double lx = x - view->scene_tree->node.x, ly = y - view->scene_tree->node.y;
		double w = view->outer_w, h = view->outer_h;
		if (lx < -past || ly < -past || lx > w + past || ly > h + past) {
			continue;
		}
		int l, t, r, b;
		view_insets(view, &l, &t, &r, &b);
		if (lx >= l && ly >= t && lx <= w - r && ly <= h - b) {
			return NULL;    /* on the program */
		}
		unsigned e = 0;
		bool low = h - ly < corner, left = lx < corner, right = w - lx < corner;
		if (low && (left || right)) {
			e = WLR_EDGE_BOTTOM | (left ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT);
		} else if (ly < corner && left) {
			e = WLR_EDGE_TOP | WLR_EDGE_LEFT;
		} else if (ly < t) {
			e = 0;          /* its head */
		} else if (lx < l) {
			e = WLR_EDGE_LEFT;
		} else if (lx > w - r) {
			e = WLR_EDGE_RIGHT;
		} else {
			e = WLR_EDGE_BOTTOM;
		}
		if (e && thing && thing->edges) {
			e = thing->edges(view, lx, ly, e);
		}
		*edges = e;
		return view;
	}
	return NULL;
}

/* What is under a finger that has set off. */
/* The Control Panel's lid on a finger (2026-11-12, the owner: コントロールセンターの蓋を開ける時も
 * 1：1のタッチへの最適化): taken by its edge, with the key home, it comes up as far as the finger
 * goes -- its edge stays under the finger -- and let go, it goes the rest of the way up, or back
 * down, as the finger was going. */
/* (not right at the hinge, where every pixel the finger went would be the lid's whole swing) */
#define LID_ALONG_MIN 0.35

static bool hand_lid_begin(struct desk_server *server, double fx, double fy)
{
	int w = server->desk_w, h = server->desk_h;
	/* Where on the lid the finger is, from the hinge to the free edge, and that place, not the
	 * edge, on the finger (2026-11-20, the owner: 蓋の感度が悪い -- held half way down, the edge
	 * went as far as the finger and the place under it half as far). */
	double hx, hy, ex, ey;
	if (!desk_console_lid_point(w, h, server->cc_guard, 0.0, &hx, &hy)
		|| !desk_console_lid_point(w, h, server->cc_guard, 1.0, &ex, &ey)) {
		return false;
	}
	double dx = ex - hx, dy = ey - hy, len2 = dx * dx + dy * dy;
	double along = len2 > 1.0 ? ((fx - hx) * dx + (fy - hy) * dy) / len2 : 1.0;
	along = clampf_d(along, LID_ALONG_MIN, 1.0);
	for (int i = 0; i <= 20; i++) {
		double x, y;
		if (!desk_console_lid_point(w, h, i / 20.0, along, &x, &y)) {
			return false;
		}
		server->lid_y[i] = y;
	}
	if (server->lid_y[20] >= server->lid_y[0] - 1.0) {
		return false;   /* (a lid that does not come up the screen at all: nothing to hold) */
	}
	double at = server->lid_y[0] + (server->lid_y[20] - server->lid_y[0]) * server->cc_guard;
	for (int i = 0; i < 20; i++) {
		double u = server->cc_guard * 20.0 - i;
		if (u >= 0.0 && u <= 1.0) {
			at = server->lid_y[i] + (server->lid_y[i + 1] - server->lid_y[i]) * u;
			break;
		}
	}
	server->lid_off = fy - at;
	server->cc_guard_moving = 0;
	desk_sound_play(server->cc_guard < 0.5 ? DESK_SND_CC_LID_UP : DESK_SND_CC_LID_DOWN, -0.55, 0.8, 0.0);
	return true;
}

static void hand_lid_move(struct desk_server *server, double fy)
{
	double want = fy - server->lid_off;
	/* The edge's height up the screen, back to how far up the lid is: the first place on the way
	 * the edge is at that height. (Coming up off the panel towards the eye, it goes a little down
	 * the screen first.) Above anywhere it gets to, as far as is highest; below, shut. */
	double t = -1.0, top = server->lid_y[0];
	int top_i = 0;
	for (int i = 0; i < 20; i++) {
		double a = server->lid_y[i], b = server->lid_y[i + 1];
		if (b < top) {
			top = b;
			top_i = i + 1;
		}
		if (t < 0.0 && fabs(a - b) > 1e-6 && want <= fmax(a, b) && want >= fmin(a, b)) {
			t = (i + (a - want) / (a - b)) / 20.0;
		}
	}
	if (t < 0.0) {
		t = want < top ? top_i / 20.0 : 0.0;
	}
	server->cc_guard = server->cc_guard_to = clampf_d(t, 0.0, 1.0);
	/* drawn on the set's next tick, a millisecond on, as far as the finger has got by then: a
	 * picture of the lid for every touch the screen reported was more than a Surface Pro could
	 * draw, and the lid fell further and further behind the finger (2026-11-20) */
	if (server->cc_timer) {
		wl_event_source_timer_update(server->cc_timer, 1);
	}
}

static void hand_lid_lift(struct desk_server *server, double vy)
{
	/* where it would be a moment on at the speed the finger was going */
	double span = fmax(1.0, server->lid_y[0] - server->lid_y[20]);
	double seen = server->cc_guard - vy * 0.16 / span;
	console_guard_to(server, seen > 0.5 ? 1.0 : 0.0);
}

/* Which of the views a thing along the top of the desk is the way to, at a point of the desk: the
 * Control Panel at the back left, the post box at the back right with the letter in its mouth (a
 * little below it), ALL APPS's plate in the corner; -1 none. */
static int top_view_at(struct desk_server *server, double x, double y)
{
	int w = server->desk_w, h = server->desk_h;
	if (desk_console_desk_hit(w, h, x, y, server->drawer_t)) {
		return DESK_VIEW_CONSOLE;
	}
	double below = 34.0 * desk_px_per_pt(w);
	if (desk_post_desk_hit(w, h, x, y, server->drawer_t)
		|| desk_post_desk_hit(w, h, x, y - below, server->drawer_t)) {
		return DESK_VIEW_POST;
	}
	if (desk_allapps_hit(w, h, x, y)) {
		return DESK_VIEW_SHELF;
	}
	return -1;
}

static enum hand_work hand_what_it_does(struct desk_server *server, struct desk_finger *f)
{
	if (server->hand_edge) {
		return HAND_SWIPES;
	}
	if (server->sw_shown) {
		return HAND_DONE;
	}
	if (locked(server) || server->snip_on || card_out(server)) {
		return HAND_HOLDS;
	}
	if (pointer_on_side(server)) {
		return HAND_HOLDS;
	}
	double cx = server->cursor->x, cy = server->cursor->y;
	if (console_up(server)) {
		if (server->cc_t < 0.9995) {
			return HAND_DONE;
		}
		int which = desk_console_hit_lid(server->desk_w, server->desk_h, cx, cy,
			server->cc_guard);
		bool on_lid = which == DESK_CC_LID || (server->cc_guard > 0.0005
			&& desk_console_guard_hit(server->desk_w, server->desk_h, cx, cy, server->cc_guard));
		/* The four buttons on the lid are the lid too, to a finger pulling it up or down: they
		 * are most of it, and one set down on them and pulled did nothing (2026-11-20, the owner:
		 * 蓋の感度が悪い). Tapped, they are still pressed. */
		double fdx = f->x - server->hand_x0, fdy = f->y - server->hand_y0;
		bool lid_face = on_lid || (fabs(fdy) > fabs(fdx)
			&& desk_console_guard_hit(server->desk_w, server->desk_h, cx, cy, server->cc_guard));
		if (lid_face && server->cc_key >= 0.999 && !server->cc_key_moving
			&& desk_console_lid_ready(server->desk_w, server->desk_h)
			&& hand_lid_begin(server, server->hand_x0, server->hand_y0)) {
			return HAND_LID;
		}
		return which == DESK_CC_VOL || which == DESK_CC_BRIGHT || which == DESK_CC_SCREEN
			|| on_lid ? HAND_HOLDS : HAND_SWIPES;
	}
	if (post_up(server) || shelf_up(server)) {
		return HAND_SWIPES;
	}
	if (server->drawer_t > 0.004) {
		if (drawer_entry_at(server, cx, cy) >= 0 || parked_sheet_at(server, cx, cy)) {
			return HAND_HOLDS;
		}
		hand_drawer_begin(server, f);
		return HAND_DRAWER;
	}
	/* Put down on one of the things along the top of the desk and pulled down (2026-11-12, the
	 * owner: 項目で上からスライドしても開くように…例：コントロールセンターの部分から上からスライドでも
	 * 開くように; 右上の紙の部分…も引っ張って出てくるように): a slip under the Control Panel has its
	 * card come out on the finger; the Control Panel, the post box and the letter in its mouth, and
	 * ALL APPS's plate, have the look round to them come down on it, one to one, as the drawer
	 * comes up on it. Anything else they are, as they were: a tap is a click. */
	double dx = f->x - server->hand_x0, dy = f->y - server->hand_y0;
	if (dy > fabs(dx) && !window_under(server)) {
		if (hand_card_begin(server, server->hand_x0, server->hand_y0, f->y)) {
			return HAND_CARD;
		}
		int which = top_view_at(server, server->hand_x0, server->hand_y0);
		if (which >= 0) {
			server->look_down = which != DESK_VIEW_SHELF;
			hand_look_begin(server, f, which);
			if (server->hand_work != HAND_LOOK) {
				server->look_down = false;
			}
			return server->hand_work;
		}
	}
	return hand_on_something(server) ? HAND_HOLDS : HAND_SWIPES;
}

/* A binder's sheets on a finger (2026-11-12, the owner: 本の紙あたりがタッチ感度が変): they hang
 * on a ring, and turning one over is swinging it up round the ring and over (binder_angle). So
 * the finger is measured round the ring as well, and the sheet in front goes round as far as
 * the finger has gone round: up and over for the next, and the one gone over comes back down
 * for the one before -- under the finger, rather than a sheet for every so many points the
 * finger went whichever way. (Before, it was a sheet each 36 points, a spring behind the finger,
 * and a fling of a fifth of a second's worth more: a short stroke turned several.) */
#define BINDER_TURN_RAD 1.05   /* how far round one sheet goes, turned over (draw.c binder_angle) */
/* and how much further the sheets go than the finger: one to one asked too much of a hand on a
 * real screen (2026-11-16, the owner: 本棚のタッチ感度が逆にちょっと低すぎる) */
#define BINDER_FINGER_GAIN 1.5

/* the way t of a smoothstep goes for y of the way round, back */
static double smoothstep_back(double y)
{
	y = clampf_d(y, 0.0, 1.0);
	return 0.5 - sin(asin(1.0 - 2.0 * y) / 3.0);
}

static bool binder_block_up(struct desk_server *server)
{
	return shelf_up(server) && server->shelf_binder >= 0 && !memo_up(server) && !cal_up(server)
		&& server->shelf_binder != DESK_SHELF_BOOK;
}

static bool hand_turns_binder(struct desk_server *server, double fx, double fy)
{
	double a, r;
	if (!desk_binder_finger_at(server->desk_w, server->desk_h, fx, fy, &a, &r)
		|| r < 8.0 * desk_px_per_pt(server->desk_w)) {
		return false;
	}
	server->sheets_fx = fx;
	server->sheets_fy = fy;
	if (server->hand_work != HAND_SHEETS) {
		server->hand_work = HAND_SHEETS;
		server->sheets_from = server->binder_pos_to;
		server->sheets_a0 = a;
		server->sheets_a = a;
		server->sheets_round = 0.0;
	}
	/* round as far as it has gone, through the half turn and on */
	double d = a - server->sheets_a;
	d -= 2.0 * TM_PI * floor((d + TM_PI) / (2.0 * TM_PI));
	server->sheets_round += d;
	server->sheets_a = a;
	/* up and over (back round the ring) is the next sheet; each a whole turn over, its start and
	 * end slow as the sheet's are */
	double x = -server->sheets_round * BINDER_FINGER_GAIN / BINDER_TURN_RAD;
	double whole = floor(fabs(x)), part = fabs(x) - whole;
	double sheets = (whole + smoothstep_back(part)) * (x < 0.0 ? -1.0 : 1.0);
	int n = binder_count(server);
	double to = clampf_d(server->sheets_from + sheets, 0.0, n > 0 ? n - 1 : 0);
	server->sheets_to = to;
	/* on the finger: no spring between them */
	server->binder_pos = server->binder_pos_to = to;
	server->binder_pos_vel = 0.0;
	server->binder_pos_moving = 1;
	server->binder_sel = -1;
	paint_shelf(server);
	server_schedule_frames(server);
	return true;
}

/* The book's leaves on a finger (2026-11-12): a spread a page's width of the finger, the leaf
 * going over the spine under it -- left (or up) for the next -- rather than a turn of the wheel
 * every 36 points, which turned ten spreads in a stroke across the page. */
static bool book_open_up(struct desk_server *server)
{
	return shelf_up(server) && server->shelf_binder == DESK_SHELF_BOOK && server->binder_to > 0.5
		&& !memo_up(server) && !cal_up(server);
}

static double book_page_w(struct desk_server *server)
{
	struct desk_book_look lk;
	book_look_still(server, &lk);
	double bb[4];
	desk_book_box(server->desk_w, server->desk_h, &lk, bb);
	return fmax(bb[2] * 0.5, 40.0 * desk_px_per_pt(server->desk_w));
}

static bool hand_turns_book(struct desk_server *server, double along)
{
	if (server->hand_work != HAND_SHEETS) {
		server->hand_work = HAND_SHEETS;
		server->sheets_from = server->binder_pos_to;
	}
	int n = binder_count(server);
	double to = clampf_d(server->sheets_from - along / book_page_w(server), 0.0, n > 0 ? n - 1 : 0);
	server->sheets_to = to;
	server->binder_pos = server->binder_pos_to = to;
	server->binder_pos_vel = 0.0;
	server->binder_pos_moving = 1;
	paint_shelf(server);
	server_schedule_frames(server);
	return true;
}

static void hand_book_lift(struct desk_server *server, double vx, double vy)
{
	double v = fabs(vx) > fabs(vy) ? vx : vy;
	double more = clampf_d(-v * 0.10 / book_page_w(server), -1.0, 1.0);
	binder_turn_to(server, round(server->sheets_to + more));
}

/* Let go of the binder's sheets: on to the nearest, or on round as far as the finger was going
 * round a moment longer. */
static void hand_binder_lift(struct desk_server *server, double vx, double vy)
{
	double fx = server->sheets_fx, fy = server->sheets_fy;
	double a, r;
	double sheets = 0.0;
	if (desk_binder_finger_at(server->desk_w, server->desk_h, fx, fy, &a, &r) && r > 1.0) {
		/* how fast round the ring, from how fast across it */
		double a2, r2;
		if (desk_binder_finger_at(server->desk_w, server->desk_h, fx + vx * 0.01, fy + vy * 0.01,
				&a2, &r2)) {
			double w = a2 - a;
			w -= 2.0 * TM_PI * floor((w + TM_PI) / (2.0 * TM_PI));
			sheets = -(w / 0.01) * 0.12 * BINDER_FINGER_GAIN / BINDER_TURN_RAD;
		}
	}
	binder_turn_to(server, round(server->sheets_to + clampf_d(sheets, -6.0, 6.0)));
}

/* Over sheets: they turn as far as the finger goes, up or left for the next. */
static void hand_turns_sheets(struct desk_server *server, double along, uint32_t time)
{
	double step = TOUCH_SHEET_PT * desk_ui_scale();
	/* a block, or letters: they go over as far as the finger goes */
	bool block = shelf_up(server) && !memo_up(server) && !cal_up(server)
		&& server->shelf_binder != DESK_SHELF_BOOK;
	if (block || post_up(server)) {
		if (server->hand_work != HAND_SHEETS) {
			server->hand_work = HAND_SHEETS;
			server->sheets_from = post_up(server) ? server->pb_pos_to : server->binder_pos_to;
		}
		server->sheets_to = server->sheets_from - along / step;
		if (post_up(server)) {
			pb_turn_to(server, server->sheets_to);
		} else {
			binder_turn_to(server, server->sheets_to);
		}
		return;
	}
	double d = -(along - server->hand_fed) / step * DESK_NOTCH;
	server->hand_fed = along;
	struct wlr_pointer_axis_event ev = {
		.pointer = NULL,
		.time_msec = time,
		.source = WL_POINTER_AXIS_SOURCE_FINGER,
		.orientation = WL_POINTER_AXIS_VERTICAL_SCROLL,
		.relative_direction = WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL,
		.delta = d,
		.delta_discrete = 0,
	};
	pointer_axis(server, &ev);
}

static void hand_swipes(struct desk_server *server, struct desk_finger *f, uint32_t time)
{
	double dx = f->x - server->hand_x0, dy = f->y - server->hand_y0;
	if (!server->hand_axis) {
		server->hand_axis = true;
		server->hand_across = fabs(dx) > fabs(dy);
	}
	bool across = server->hand_across;
	bool edge = server->hand_edge;
	if (edge) {
		/* in from the rim, the way it came in from it, or nothing */
		unsigned rim = 0;
		struct wlr_output *out = wlr_output_layout_output_at(server->output_layout,
			server->hand_x0, server->hand_y0);
		if (out) {
			struct wlr_box b;
			double r = TOUCH_RIM_PT * desk_ui_scale();
			wlr_output_layout_get_box(server->output_layout, out, &b);
			rim |= server->hand_x0 < b.x + r ? WLR_EDGE_LEFT : 0;
			rim |= server->hand_x0 >= b.x + b.width - r ? WLR_EDGE_RIGHT : 0;
			rim |= server->hand_y0 < b.y + r ? WLR_EDGE_TOP : 0;
			rim |= server->hand_y0 >= b.y + b.height - r ? WLR_EDGE_BOTTOM : 0;
		}
		bool in = across ? ((dx > 0 && (rim & WLR_EDGE_LEFT)) || (dx < 0 && (rim & WLR_EDGE_RIGHT)))
			: ((dy > 0 && (rim & WLR_EDGE_TOP)) || (dy < 0 && (rim & WLR_EDGE_BOTTOM)));
		if (!in) {
			server->hand_work = HAND_DONE;
			return;
		}
	}
	if (!post_up(server) && !console_up(server) && !shelf_up(server) && !across && dy < 0.0) {
		hand_drawer_begin(server, f);
		return;
	}
	/* In from the top of the screen, down over the Control Panel or the post box: that, down on
	 * the finger (2026-11-12); anywhere else along the top, the bookcase, as before. The thing
	 * is looked for down the screen from where the finger came in, the top edge itself being
	 * above it. */
	if (edge && !across && dy > 0.0 && !post_up(server) && !console_up(server) && !shelf_up(server)) {
		double pt = desk_px_per_pt(server->desk_w);
		int which = -1;
		for (double yy = server->hand_y0; which < 0 && yy < server->hand_y0 + 90.0 * pt;
				yy += 6.0 * pt) {
			which = top_view_at(server, server->hand_x0, yy);
		}
		if (which == DESK_VIEW_CONSOLE || which == DESK_VIEW_POST) {
			server->look_down = true;
			hand_look_begin(server, f, which);
			if (server->hand_work != HAND_LOOK) {
				server->look_down = false;
			}
			return;
		}
	}
	if (hand_has_sheets(server) && !(post_up(server) && across)) {
		if (binder_block_up(server) && hand_turns_binder(server, f->x, f->y)) {
			return;
		}
		if (book_open_up(server) && hand_turns_book(server, across ? dx : dy)) {
			return;
		}
		hand_turns_sheets(server, across ? dx : dy, time);
		return;
	}
	if (console_up(server)) {
		if (across && dx < 0.0) {
			hand_look_begin(server, f, DESK_VIEW_CONSOLE);
		} else {
			server->hand_work = HAND_DONE;
			server->hand_may_tap = true;
		}
	} else if (post_up(server)) {
		if (across && dx > 0.0) {
			hand_look_begin(server, f, DESK_VIEW_POST);
		} else {
			server->hand_work = HAND_DONE;
		}
	} else if (shelf_up(server)) {
		if (!across && dy < 0.0) {
			hand_look_begin(server, f, DESK_VIEW_SHELF);
		} else {
			server->hand_work = HAND_DONE;
		}
	} else if (!across) {
		hand_look_begin(server, f, DESK_VIEW_SHELF);
	} else if (dx > 0.0) {
		hand_look_begin(server, f, DESK_VIEW_CONSOLE);
	} else {
		hand_look_begin(server, f, DESK_VIEW_POST);
	}
}

/* ---- the whole hand ------------------------------------------------------ */

/* What is on the finger, let go of, as it was going. */
static void hand_lifted(struct desk_server *server, double vx, double vy)
{
	double step = TOUCH_SHEET_PT * desk_ui_scale();
	switch (server->hand_work) {
	case HAND_DRAWER:
		hand_drawer_lift(server, vy);
		break;
	case HAND_LOOK:
		hand_look_lift(server, vx, vy);
		break;
	case HAND_CURL:
		hand_curl_lift(server, vx, vy);
		break;
	case HAND_CARD:
		hand_card_lift(server, vy);
		break;
	case HAND_LID:
		hand_lid_lift(server, vy);
		break;
	case HAND_SHEETS: {
		if (binder_block_up(server)) {
			hand_binder_lift(server, vx, vy);
			break;
		}
		if (book_open_up(server)) {
			hand_book_lift(server, vx, vy);
			break;
		}
		/* on to the next that it was going to, and square */
		double along = fabs(vx) > fabs(vy) ? vx : vy;
		double to = round(server->sheets_to - along * 0.20 / step);
		if (post_up(server)) {
			pb_turn_to(server, to);
		} else if (shelf_up(server)) {
			binder_turn_to(server, to);
		}
		break;
	}
	default:
		break;
	}
}

static void hand_let_go(struct desk_server *server, uint32_t time)
{
	if (!server->hand_on) {
		return;
	}
	if (server->hand_work == HAND_HOLDS) {
		hand_button(server, BTN_LEFT, false, time);
	} else {
		hand_lifted(server, 0.0, 0.0);
	}
	hand_gone(server);
}

/* Fingers that have turned out to be a hand: what they were doing, they were not. */
static void fingers_become_whole(struct desk_server *server, uint32_t time)
{
	hand_let_go(server, time);
	for (int i = 0; i < DESK_FINGERS; i++) {
		struct desk_finger *f = &server->fingers[i];
		if (!f->on) {
			continue;
		}
		if (f->goes == FINGER_PROGRAM) {
			struct wlr_touch_point *point = wlr_seat_touch_get_point(server->seat, f->id);
			if (point && point->client) {
				wlr_seat_touch_notify_cancel(server->seat, point->client);
			}
		}
		if (f->goes != FINGER_NOWHERE) {
			f->goes = FINGER_WHOLE;
		}
	}
}

/* How far on, after the first flap, the rest of a fold goes, in points */
#define WHOLE_FOLD_ON_PT 120.0
/* How fast a hand throwing a sheet to a side lays it on that half, in points a second */
#define WHOLE_HALF_SPEED_PT 1400.0

/* What of it the desk is doing that a hand could not start something over. */
static bool whole_hand_busy(const struct desk_server *server)
{
	return locked(server) || console_dying(server) || server->snip_on || server->sw_on
		|| server->sw_shown || shelf_up(server) || console_up(server) || post_up(server)
		|| server->drawer_t > 0.004 || server->crumple;
}

static void whole_begins_crumple(struct desk_server *server, struct desk_toplevel *view)
{
	struct wlr_surface *surface = view_surface(view);
	if (!server->conf.throw_closed || view_refuses(view, THING_NO_CRUMPLE) || view->unmanaged
		|| view->sheet.phase != SHEET_IDLE || view->outer_w < 60 || view->outer_h < 60
		|| !surface || !surface->buffer || !view->scene_tree->node.enabled) {
		server->whole_work = WHOLE_CLOSE;
		return;
	}
	cairo_surface_t *snap = view_snapshot(view);
	struct desk_hand_crumple *hc = calloc(1, sizeof *hc);
	if (cairo_surface_status(snap) != CAIRO_STATUS_SUCCESS || !hc) {
		cairo_surface_destroy(snap);
		free(hc);
		server->whole_work = WHOLE_CLOSE;
		return;
	}
	hc->sw = cairo_image_surface_get_width(snap);
	hc->sh = cairo_image_surface_get_height(snap);
	hc->c = desk_crumple_new(snap, throw_seed() * 2654435761u);
	cairo_surface_destroy(snap);
	if (!hc->c) {
		free(hc);
		server->whole_work = WHOLE_CLOSE;
		return;
	}
	hc->view = view;
	hc->x0 = view->scene_tree->node.x;
	hc->y0 = view->scene_tree->node.y;
	hc->at = monotonic_sec();
	server->crumple = hc;
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);
	snd_at(server, DESK_SND_PAPER_CRUMPLE, hc->x0 + hc->sw * 0.5, 1.0);
	server->whole_work = WHOLE_CRUMPLE;
	server_schedule_frames(server);
}

/* All of them taken up, as far as the hand goes. With one window there is nothing to choose
 * from: as Super+Tab is. */
static void whole_begins_pick(struct desk_server *server)
{
	switcher_step(server, 1);
	if (server->sw_on && !server->sw_shown) {
		switcher_spread(server);
	}
	if (!server->sw_shown) {
		return;
	}
	server->sw_held = true;
	server->sw_hand = 0.0;
	server->whole_full = server->desk_h * 0.38;
	server->whole_work = WHOLE_PICK;
}

static void whole_begins_fold(struct desk_server *server, struct desk_toplevel *view)
{
	if (view->fullscreen) {
		view_set_fullscreen(view, false);
	}
	if (view->sheet.phase != SHEET_IDLE) {
		return;
	}
	view->sheet.by_hand = true;
	view_fold_toggle(view);
	if (view->sheet.phase != SHEET_FOLDING || !view->sheet.fold) {
		view->sheet.by_hand = false;
		return;
	}
	view->sheet.hand_held = true;
	view->sheet.hand_free = false;
	view->sheet.hand_t = 0.0;
	/* the end of the first flap is under the fingers (unless it is further than a hand
	 * goes: and then it goes faster) */
	server->whole_full = fmin(desk_fold_hand_reach(view->sheet.fold), server->desk_h * 0.5);
	server->whole_work = WHOLE_FOLD;
}

/* How far through its folding a sheet is, the fingers so far down it: the first flap is on them,
 * and a little further the rest goes. */
static double whole_fold_t(const struct desk_server *server, const struct desk_sheet *sheet,
	double dy)
{
	double first = desk_fold_hand_t(sheet->fold, 1.0);
	if (dy <= server->whole_full) {
		return desk_fold_hand_t(sheet->fold, dy / server->whole_full);
	}
	return first + (1.0 - first) * clampf_d((dy - server->whole_full)
		/ (WHOLE_FOLD_ON_PT * desk_ui_scale()), 0.0, 0.99);
}

static void whole_begins_open(struct desk_server *server, struct desk_toplevel *view)
{
	view_toggle_max(view);
	if (!view->ua_on) {
		return;    /* (it just went there: nothing to be seen doing) */
	}
	view->ua_held = true;
	view->ua_free = false;
	view->ua_p = 0.0;
	server->whole_work = WHOLE_OPEN;
}

static void whole_begins_carry(struct desk_server *server, struct desk_toplevel *view, double x,
	double y)
{
	if (view->fullscreen || view->maximized) {
		return;
	}
	focus_toplevel(view);
	wlr_cursor_warp_closest(server->cursor, NULL, x, y);
	begin_interactive(view, DESK_CUR_MOVE, 0);
	server->whole_work = WHOLE_CARRY;
}

/* Three fingers or more have set off. What is under them, and which way, says what it is. */
static void whole_begins(struct desk_server *server, const struct desk_touch_said *said)
{
	server->whole_work = WHOLE_NOTHING;
	server->whole_view = NULL;
	if (whole_hand_busy(server)) {
		return;
	}
	bool down = fabs(said->dy) > fabs(said->dx);
	if (said->what == DESK_TOUCH_HAND_SWIPE && down && said->dy < 0.0) {
		whole_begins_pick(server);
		return;
	}
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct desk_toplevel *view = view_at(server, said->x, said->y, &surface, &sx, &sy);
	if (!view || !view->scene_tree || view->unmanaged || view->ua_on) {
		return;
	}
	server->whole_view = view;
	if (said->what == DESK_TOUCH_HAND_SWIPE && down) {
		whole_begins_fold(server, view);
	} else if (said->what == DESK_TOUCH_HAND_SWIPE) {
		whole_begins_carry(server, view, said->x + said->dx, said->y + said->dy);
	} else if (said->what == DESK_TOUCH_HAND_SPREAD) {
		if (!view->maximized && !view->fullscreen) {
			whole_begins_open(server, view);
		}
	} else if (view->fullscreen) {
		view_set_fullscreen(view, false);
	} else if (view->maximized) {
		whole_begins_open(server, view);
	} else {
		whole_begins_crumple(server, view);
	}
}

/* How closed a hand is, to screw a sheet up all the way */
#define WHOLE_CRUMPLED 0.62

static void whole_moves(struct desk_server *server, const struct desk_touch_said *said)
{
	struct desk_toplevel *view = server->whole_view;
	switch (server->whole_work) {
	case WHOLE_FOLD:
		if (view) {
			view->sheet.hand_t = whole_fold_t(server, &view->sheet, said->dy);
			server->whole_dy = said->dy;
		}
		break;
	case WHOLE_OPEN:
		if (view) {
			view->ua_p = view->ua_dir > 0 ? (said->spread - 1.0) : (1.0 - said->spread) * 2.0;
			view->ua_p = clampf_d(view->ua_p, 0.0, 0.9999);
		}
		break;
	case WHOLE_CRUMPLE:
		if (server->crumple) {
			server->crumple->u = clampf_d((1.0 - said->spread) / WHOLE_CRUMPLED, 0.0, 1.0);
			server->crumple->dx = said->dx;
			server->crumple->dy = said->dy;
		}
		break;
	case WHOLE_CARRY:
		wlr_cursor_warp_closest(server->cursor, NULL, said->x + said->dx, said->y + said->dy);
		if (server->cursor_mode == DESK_CUR_MOVE && server->grabbed) {
			process_cursor_move(server);
		}
		break;
	case WHOLE_PICK:
		server->sw_hand = clampf_d(-said->dy / server->whole_full, 0.0, 1.0);
		break;
	default:
		return;
	}
	server_schedule_frames(server);
}

/* The fold let go of. The flap has weight: past standing up it falls on over, and short of that
 * back, unless it was going fast enough to get there. */
static void whole_fold_lift(struct desk_server *server, struct desk_toplevel *view, double vy)
{
	struct desk_sheet *sheet = &view->sheet;
	if (!sheet->hand_held) {
		return;
	}
	double dy = server->whole_dy;
	double v = (whole_fold_t(server, sheet, dy + vy * 0.01) - whole_fold_t(server, sheet, dy))
		/ 0.01;
	double seen = whole_fold_t(server, sheet, dy + vy * 0.16);
	double now = monotonic_sec();
	sheet->hand_held = false;
	sheet->hand_free = true;
	sheet->hand_at = now;
	if (seen > desk_fold_hand_t(sheet->fold, 0.5)) {
		sheet->hand_v = clampf_d(v, 0.15, 3.0);
	} else {
		sheet_snd_forget(sheet);
		sheet->snd_k = desk_fold_stage(sheet->fold, sheet->hand_t);
		sheet->phase = SHEET_UNFOLDING;
		sheet->to_drawer = false;
		sheet->hand_v = clampf_d(v, -3.0, -0.15);
	}
	server_schedule_frames(server);
}

static void whole_lifted(struct desk_server *server, const struct desk_touch_said *said)
{
	struct desk_toplevel *view = server->whole_view;
	enum whole_work work = server->whole_work;
	server->whole_work = WHOLE_NOTHING;
	server->whole_view = NULL;
	double pt = desk_ui_scale();
	switch (work) {
	case WHOLE_FOLD:
		if (view) {
			whole_fold_lift(server, view, said->vy);
		}
		break;
	case WHOLE_OPEN:
		if (view && view->ua_on) {
			double v = view->ua_dir > 0 ? said->vspread : -said->vspread * 2.0;
			view->ua_held = false;
			view->ua_free = true;
			view->ua_at = monotonic_sec();
			view->ua_to_end = view->ua_p + v * 0.16 > 0.30;
			view->ua_v = view->ua_to_end ? fmax(v, 0.3) : fmin(v, -0.3);
		}
		break;
	case WHOLE_CRUMPLE: {
		struct desk_hand_crumple *hc = server->crumple;
		if (!hc) {
			break;
		}
		hc->vx = said->vx;
		if (hc->u - said->vspread / WHOLE_CRUMPLED * 0.16 > 0.50 && hc->view) {
			hc->closed_at = monotonic_sec();
			view_close(hc->view);
		} else {
			hc->back = true;
			hc->v = 0.0;
		}
		break;
	}
	case WHOLE_CLOSE:
		if (view && (said->spread < 0.72 || said->vspread < -1.2)) {
			view_close(view);
		}
		break;
	case WHOLE_CARRY: {
		struct desk_toplevel *carried = server->grabbed;
		double x = said->x + said->dx;
		if (server->cursor_mode == DESK_CUR_MOVE && carried && carried->scene_tree) {
			/* thrown hard to a side, or let go of well over at it: laid on that half */
			struct wlr_output *out = view_screen(carried);
			struct wlr_box b = { 0 };
			if (out) {
				wlr_output_layout_get_box(server->output_layout, out, &b);
			}
			int side = 0;
			if (out && !carried->half && (fabs(said->vx) > WHOLE_HALF_SPEED_PT * pt
					|| x < b.x + b.width * 0.10 || x > b.x + b.width * 0.90)) {
				double to = fabs(said->vx) > WHOLE_HALF_SPEED_PT * pt ? said->vx
					: x - (b.x + b.width * 0.5);
				side = to < 0.0 ? -1 : 1;
			}
			double vx = said->vx, vy = said->vy;
			if (!side) {
				move_release(server);
			}
			reset_cursor_mode(server);
			view_sync_shadow(carried);
			if (side) {
				view_lay_half(carried, side, out);
			} else {
				paper_throw(server, carried, vx, vy);
			}
		}
		break;
	}
	case WHOLE_PICK: {
		double k = clampf_d(server->sw_hand, 0.0, 1.0);
		server->sw_held = false;
		if (k - said->vy / server->whole_full * 0.16 > 0.25) {
			/* on: the way they were going, from where they have got to */
			server->sw_at = monotonic_sec() - (1.0 - cbrt(1.0 - k)) * SPREAD_S;
			server->sw_k = k;
			server_schedule_frames(server);
		} else {
			server->sw_k = k;
			switcher_finish(server, false);
		}
		break;
	}
	default:
		break;
	}
}

/* ---- whose a finger is --------------------------------------------------- */

/* On the rim of the screen, with no screen beyond it? */
static bool finger_on_rim(struct desk_server *server, struct wlr_output *out, double x, double y)
{
	if (!out || screen_is_side(server, out)) {
		return false;
	}
	struct wlr_box b;
	double r = TOUCH_RIM_PT * desk_ui_scale();
	wlr_output_layout_get_box(server->output_layout, out, &b);
	struct wlr_output_layout *l = server->output_layout;
	return (x < b.x + r && !wlr_output_layout_output_at(l, b.x - 1, y))
		|| (x >= b.x + b.width - r && !wlr_output_layout_output_at(l, b.x + b.width, y))
		|| (y < b.y + r && !wlr_output_layout_output_at(l, x, b.y - 1))
		|| (y >= b.y + b.height - r && !wlr_output_layout_output_at(l, x, b.y + b.height));
}

/* On the fold of a sheet's corner, there to be turned up? */
static bool finger_on_fold(const struct desk_toplevel *v, double x, double y)
{
	double rest, open;
	curl_sizes(v, &rest, &open);
	double lx = x - v->scene_tree->node.x, ly = y - v->scene_tree->node.y;
	double zone = rest * 2.6, dx = v->outer_w - lx, past = curl_past(v);
	return curl_wanted(v) && dx >= -past && dx <= zone && ly >= -past && ly <= zone
		&& (!v->own_frame || mount_on_fold(v, dx, ly));
}

/* Put down here: the program's, or the desk's? */
static enum finger_goes finger_whose(struct desk_server *server, double x, double y,
	struct desk_toplevel **view, struct wlr_surface **surface, double *sx, double *sy, bool *edge)
{
	*edge = false;
	*surface = NULL;
	*view = NULL;
	if (locked(server) || console_dying(server) || server->sw_on || server->snip_on
		|| card_out(server)) {
		return FINGER_HAND;
	}
	struct wlr_output *out = wlr_output_layout_output_at(server->output_layout, x, y);
	if (!screen_is_side(server, out) && (server->drawer_t > 0.004 || post_up(server)
			|| console_up(server) || shelf_up(server))) {
		return FINGER_HAND;
	}
	*view = view_at(server, x, y, surface, sx, sy);
	if (!*surface) {
		return FINGER_HAND;
	}
	struct desk_toplevel *v = *view;
	if (v && !v->fullscreen && finger_on_rim(server, out, x, y)) {
		*edge = true;
		return FINGER_HAND;
	}
	unsigned edges;
	if (paper_under_finger(server, x, y, &edges)) {
		return FINGER_HAND;
	}
	if (v && v->scene_tree) {
		const struct desk_thing *thing = view_thing(v);
		if (thing && thing->press && !thing->program_fingers) {
			return FINGER_HAND;
		}
		if (view_framed(v)) {
			double lx = x - v->scene_tree->node.x, ly = y - v->scene_tree->node.y;
			if (finger_on_fold(v, x, y)) {
				return FINGER_HAND;
			}
			if (!v->own_frame
				&& desk_frame_resize_edges(lx, ly, v->outer_w, v->outer_h)) {
				return FINGER_HAND;
			}
		}
	}
	/* (a program that does not take fingers is clicked, by the hand) */
	return wlr_surface_accepts_touch(server->seat, *surface) ? FINGER_PROGRAM : FINGER_HAND;
}

static int touch_timer_cb(void *data)
{
	struct desk_server *server = data;
	struct desk_touch_said said = desk_touch_tick(&server->touch, hand_sec());
	if (said.what == DESK_TOUCH_LONG && server->hand_on && said.id == server->hand_id
		&& server->hand_work == HAND_WAITING) {
		uint32_t time = (uint32_t)(real_sec() * 1000.0);
		server->hand_work = HAND_DONE;
		hand_button(server, BTN_RIGHT, true, time);
		hand_button(server, BTN_RIGHT, false, time);
	}
	return 0;
}

static void hand_begin(struct desk_server *server, struct desk_finger *f, bool edge, uint32_t time)
{
	server->hand_on = true;
	server->hand_id = f->id;
	server->hand_work = HAND_WAITING;
	server->hand_edge = edge;
	server->hand_axis = false;
	server->hand_x0 = f->x;
	server->hand_y0 = f->y;
	server->hand_fed = 0.0;
	server->hand_may_tap = false;
	if (!edge && hand_catches(server, f)) {
		return;
	}
	hand_to(server, f->x, f->y, time);
	if (!server->touch_timer) {
		server->touch_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), touch_timer_cb, server);
	}
	if (server->touch_timer) {
		wl_event_source_timer_update(server->touch_timer,
			(int)(DESK_TOUCH_LONG_SEC * g_time_slow * 1000.0) + 1);
	}
}

static void server_touch_down(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, touch_down);
	struct wlr_touch_down_event *event = data;
	double x, y;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x, event->y,
		&x, &y);
	struct desk_finger *f = NULL;
	for (int i = 0; !f && i < DESK_FINGERS; i++) {
		if (!server->fingers[i].on) {
			f = &server->fingers[i];
		}
	}
	if (!f) {
		return;
	}
	*f = (struct desk_finger){ .on = true, .id = event->touch_id, .x = x, .y = y };
	/* the hand that has the pen in it, resting on the screen: not a finger */
	if (server->pens_near > 0 || real_sec() - server->pen_left_at < PEN_PALM_AFTER) {
		f->goes = FINGER_NOWHERE;
		return;
	}
	server->finger_screen = wlr_output_layout_output_at(server->output_layout, x, y);
	/* a finger on the dark screen wakes it, and all of that touch lands on nothing */
	if (!input_seen(server, "a touch")) {
		server->touch_woke = true;
	}
	if (server->touch_woke) {
		f->goes = FINGER_NOWHERE;
		return;
	}
	desk_sound_awake();
	server->throw_view = NULL;
	if (!server->hand_unseen) {
		server->hand_unseen = true;
		wlr_cursor_unset_image(server->cursor);
	}
	/* at the door there is a password to type */
	if (locked(server) && server->lock_phase == LOCK_SHUT) {
		things_text_wanted(server, true);
	}
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->finger_down && (*t)->finger_down(server, f->id, x, y)) {
			f->goes = FINGER_THING;
			f->thing = *t;
			return;
		}
	}
	struct desk_touch_said said = desk_touch_down(&server->touch, f->id, x, y, hand_sec(),
		desk_ui_scale());
	if (said.what == DESK_TOUCH_HAND) {
		fingers_become_whole(server, event->time_msec);
		f->goes = FINGER_WHOLE;
		return;
	}
	if (said.what == DESK_TOUCH_SIX) {
		/* six together, both hands down as if on keys: the keyboard on the screen comes up
		 * (2026-11-11, the owner), not at the door, where the password's own keys come */
		f->goes = FINGER_WHOLE;
		for (const struct desk_thing *const *t = desk_things; *t; t++) {
			if (!strcmp((*t)->name, "keyboard") && (*t)->take_out && !locked(server)
				&& (!(*t)->offered || (*t)->offered(server))) {
				wlr_log(WLR_INFO, "fingers: six together: the keyboard on the screen");
				(*t)->take_out(server);
			}
		}
		return;
	}
	if (server->touch.hand) {
		f->goes = FINGER_WHOLE;
		return;
	}
	struct desk_toplevel *view;
	struct wlr_surface *surface;
	double sx = 0.0, sy = 0.0;
	bool edge;
	f->goes = finger_whose(server, x, y, &view, &surface, &sx, &sy, &edge);
	f->view = view;
	/* a corner turned up goes down when a finger comes down anywhere off it */
	curl_track(server, view, x, y);
	if (f->goes == FINGER_PROGRAM) {
		bool first = true;
		for (int i = 0; i < DESK_FINGERS; i++) {
			struct desk_finger *o = &server->fingers[i];
			first = first && !(o != f && o->on && o->goes == FINGER_PROGRAM && o->view == view);
		}
		if (view && first) {
			focus_toplevel(view);
		}
		f->ox = x - sx;
		f->oy = y - sy;
		wlr_seat_touch_notify_down(server->seat, surface, event->time_msec, f->id, sx, sy);
		/* typing in it already: a finger on it asks for what to type on again */
		if (ti_of_focus(server)) {
			things_told_text_wanted(server, true);
		}
		return;
	}
	if (server->hand_on) {
		f->goes = FINGER_ASIDE;
		return;
	}
	hand_begin(server, f, edge, event->time_msec);
}

static void hand_sets_off(struct desk_server *server, struct desk_finger *f, uint32_t time)
{
	if (server->touch_timer) {
		wl_event_source_timer_update(server->touch_timer, 0);
	}
	/* (it has something already: caught, or a program's own heading) */
	if (server->hand_work != HAND_WAITING) {
		return;
	}
	if (!server->hand_edge) {
		/* a sheet, by its corner, its head or its edges */
		unsigned edges = 0;
		struct desk_toplevel *view = NULL;
		bool free_hand = !(post_up(server) || console_up(server) || shelf_up(server)
			|| locked(server) || server->sw_on || server->snip_on || card_out(server)
			|| server->drawer_t > 0.004);
		if (free_hand) {
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct desk_toplevel *on = view_at(server, server->hand_x0, server->hand_y0, &surface,
				&sx, &sy);
			view = paper_under_finger(server, server->hand_x0, server->hand_y0, &edges);
			struct desk_toplevel *corner = on ? on : view;
			if (corner && corner->scene_tree && curl_wanted(corner) && corner->curl_act < 0
				&& (finger_on_fold(corner, server->hand_x0, server->hand_y0)
					|| (corner->curl_near && !surface))) {
				focus_toplevel(corner);
				server->curl_view = corner;
				server->hand_work = HAND_CURL;
				return;
			}
		}
		if (view) {
			focus_toplevel(view);
			begin_interactive(view, edges ? DESK_CUR_RESIZE : DESK_CUR_MOVE, edges);
			server->hand_work = HAND_HOLDS;
			hand_to(server, f->x, f->y, time);
			return;
		}
	}
	server->hand_work = hand_what_it_does(server, f);
	if (server->hand_work == HAND_HOLDS) {
		/* pressed where it was put down, and on from there */
		hand_button(server, BTN_LEFT, true, time);
		hand_to(server, f->x, f->y, time);
	}
}

/* A finger that has moved, of the hand. */
static void hand_moves(struct desk_server *server, struct desk_finger *f, uint32_t time)
{
	switch (server->hand_work) {
	case HAND_HOLDS:
		hand_to(server, f->x, f->y, time);
		break;
	case HAND_SWIPES:
		hand_swipes(server, f, time);
		break;
	case HAND_SHEETS:
		hand_swipes(server, f, time);
		break;
	case HAND_DRAWER:
		hand_drawer_move(server, f);
		break;
	case HAND_LOOK:
		hand_look_move(server, f);
		break;
	case HAND_CURL:
		hand_curl_move(server, f);
		break;
	case HAND_CARD:
		hand_card_move(server, f->y);
		break;
	case HAND_LID:
		hand_lid_move(server, f->y);
		break;
	default:
		break;
	}
}

static void server_touch_motion(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, touch_motion);
	struct wlr_touch_motion_event *event = data;
	input_seen(server, "a touch");
	struct desk_finger *f = finger_of(server, event->touch_id);
	if (!f) {
		return;
	}
	double x, y;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x, event->y,
		&x, &y);
	f->x = x;
	f->y = y;
	if (f->goes == FINGER_NOWHERE) {
		return;
	}
	if (f->goes == FINGER_THING) {
		if (f->thing->finger_move) {
			f->thing->finger_move(server, f->id, x, y);
		}
		return;
	}
	struct desk_touch_said said = desk_touch_move(&server->touch, f->id, x, y, hand_sec(),
		desk_ui_scale());
	switch (f->goes) {
	case FINGER_PROGRAM:
		if (server->seat->drag) {
			/* something carried out of a program: it goes where the finger goes */
			double sx = 0.0, sy = 0.0;
			struct wlr_surface *surface = NULL;
			view_at(server, x, y, &surface, &sx, &sy);
			if (server->drag_icon) {
				wlr_scene_node_set_position(&server->drag_icon->node, (int)x, (int)y);
			}
			if (surface) {
				wlr_seat_touch_point_focus(server->seat, surface, event->time_msec, f->id, sx, sy);
			} else {
				wlr_seat_touch_point_clear_focus(server->seat, event->time_msec, f->id);
			}
			wlr_seat_touch_notify_motion(server->seat, event->time_msec, f->id, sx, sy);
		} else {
			wlr_seat_touch_notify_motion(server->seat, event->time_msec, f->id, x - f->ox, y - f->oy);
		}
		break;
	case FINGER_HAND:
		if (said.what == DESK_TOUCH_SETS_OFF) {
			hand_sets_off(server, f, event->time_msec);
		}
		if (said.what == DESK_TOUCH_MOVES || said.what == DESK_TOUCH_SETS_OFF) {
			hand_moves(server, f, event->time_msec);
		}
		break;
	case FINGER_WHOLE:
		if (said.what == DESK_TOUCH_HAND_MOVES) {
			whole_moves(server, &said);
		} else if (said.what != DESK_TOUCH_NOTHING) {
			whole_begins(server, &said);
			whole_moves(server, &said);
		}
		break;
	default:
		break;
	}
}

/* A finger on one of the Control Panel's buttons that rolled or slid a little as it pressed,
 * further than a tap, into no way of going that does anything there: let go on the same button,
 * not far off, it pressed it, as a finger on a button does (2026-11-20, the owner: タッチ感度が変;
 * the key, upright and narrow, mostly did nothing). The pointer is still where it went down. */
#define TOUCH_SLID_TAP_PT 24.0
static bool hand_slid_tap(struct desk_server *server, const struct desk_finger *f)
{
	if (!server->hand_may_tap || server->hand_work != HAND_DONE || !console_up(server)
		|| server->cc_t < 0.9995) {
		return false;
	}
	if (hypot(f->x - server->hand_x0, f->y - server->hand_y0)
		> TOUCH_SLID_TAP_PT * desk_ui_scale()) {
		return false;
	}
	int w = server->desk_w, h = server->desk_h;
	int down = desk_console_hit_lid(w, h, server->hand_x0, server->hand_y0, server->cc_guard);
	int up = desk_console_hit_lid(w, h, f->x, f->y, server->cc_guard);
	return down >= 0 && down == up;
}

static void finger_off(struct desk_server *server, struct desk_finger *f)
{
	f->on = false;
	if (!fingers_on(server)) {
		server->touch_woke = false;
	}
}

static void server_touch_up(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, touch_up);
	struct wlr_touch_up_event *event = data;
	struct desk_finger *f = finger_of(server, event->touch_id);
	if (!f) {
		return;
	}
	if (f->goes == FINGER_THING) {
		if (f->thing->finger_up) {
			f->thing->finger_up(server, f->id, false);
		}
		finger_off(server, f);
		return;
	}
	struct desk_touch_said said = desk_touch_up(&server->touch, f->id, hand_sec());
	if (said.what == DESK_TOUCH_HAND_LIFTED) {
		whole_lifted(server, &said);
	}
	if (f->goes == FINGER_PROGRAM) {
		wlr_seat_touch_notify_up(server->seat, event->time_msec, f->id);
	} else if (f->goes == FINGER_HAND && server->hand_on) {
		if ((server->hand_work == HAND_WAITING && said.what == DESK_TOUCH_TAP)
			|| hand_slid_tap(server, f)) {
			hand_button(server, BTN_LEFT, true, event->time_msec);
			hand_button(server, BTN_LEFT, false, event->time_msec);
		} else if (server->hand_work == HAND_HOLDS) {
			struct desk_toplevel *carried = server->cursor_mode == DESK_CUR_MOVE
				? server->grabbed : NULL;
			hand_to(server, f->x, f->y, event->time_msec);
			hand_button(server, BTN_LEFT, false, event->time_msec);
			if (carried) {
				paper_throw(server, carried, said.vx, said.vy);
			}
		} else {
			hand_lifted(server, said.vx, said.vy);
		}
		hand_gone(server);
	}
	finger_off(server, f);
}

static void finger_taken_away(struct desk_server *server, struct desk_finger *f, uint32_t time)
{
	if (f->goes == FINGER_THING) {
		if (f->thing->finger_up) {
			f->thing->finger_up(server, f->id, true);
		}
		finger_off(server, f);
		return;
	}
	desk_touch_cancel(&server->touch, f->id);
	if (f->goes == FINGER_WHOLE && server->whole_work != WHOLE_NOTHING) {
		/* (taken away: as if let go of, going nowhere) */
		struct desk_touch_said still = { .what = DESK_TOUCH_HAND_LIFTED, .x = f->x, .y = f->y,
			.spread = 1.0 };
		whole_lifted(server, &still);
	}
	if (f->goes == FINGER_PROGRAM) {
		struct wlr_touch_point *point = wlr_seat_touch_get_point(server->seat, f->id);
		if (point && point->client) {
			wlr_seat_touch_notify_cancel(server->seat, point->client);
		}
	} else if (f->goes == FINGER_HAND) {
		hand_let_go(server, time);
	}
	finger_off(server, f);
}

static void server_touch_cancel(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, touch_cancel);
	struct wlr_touch_cancel_event *event = data;
	struct desk_finger *f = finger_of(server, event->touch_id);
	if (f) {
		finger_taken_away(server, f, event->time_msec);
	}
}

static void server_touch_frame(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, touch_frame);
	(void)data;
	wlr_seat_touch_notify_frame(server->seat);
}

/* A program asks to be moved or pulled, from a finger on it (its own heading):
 * then that finger is no longer the program's, but the hand that has hold of it. */
static void finger_gets_hold(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (server->hand_on) {
		return;
	}
	for (int i = 0; i < DESK_FINGERS; i++) {
		struct desk_finger *f = &server->fingers[i];
		if (!f->on || f->goes != FINGER_PROGRAM || f->view != view) {
			continue;
		}
		struct wlr_touch_point *point = wlr_seat_touch_get_point(server->seat, f->id);
		if (point && point->client) {
			wlr_seat_touch_notify_cancel(server->seat, point->client);
		}
		f->goes = FINGER_HAND;
		server->hand_on = true;
		server->hand_id = f->id;
		server->hand_work = HAND_HOLDS;
		server->hand_edge = false;
		wlr_cursor_warp_closest(server->cursor, NULL, f->x, f->y);
		return;
	}
}

/* ---- the pen -------------------------------------------------------------
 *
 * A pen is a hand that can be seen coming. It has a place before it touches: the pointer goes where
 * it is, for as long as it is near, and is gone when it is lifted away. Its tip is the left button
 * and its side button the other, for the desk and for a program that knows nothing of pens; a
 * program that does know is told as a pen, how hard and how it leans, from when the pen comes
 * over it, and the pointer is not there for it.
 *
 * The hand that holds it rests on the screen: while the pen is near, and for a moment after,
 * a touch is not a finger. */

static struct desk_pen *pen_of(struct desk_server *server, struct wlr_tablet *tablet, struct wlr_tablet_tool *tool);

static bool pen_over_program(struct desk_server *server, struct wlr_surface *surface, double sx, double sy)
{
	struct desk_pen *pen = server->pen_now;
	if (!pen || !pen->tablet || !wlr_surface_accepts_tablet_v2(pen->tablet->v2, surface)) {
		return false;
	}
	wlr_seat_pointer_clear_focus(server->seat);
	if (pen->v2->focused_surface != surface) {
		wlr_tablet_v2_tablet_tool_notify_proximity_in(pen->v2, pen->tablet->v2, surface);
	}
	wlr_tablet_v2_tablet_tool_notify_motion(pen->v2, sx, sy);
	pen->sx = sx;
	pen->sy = sy;
	server->pen_took = true;
	return true;
}

static bool pen_down_on_program(struct desk_server *server, struct wlr_pointer_button_event *event)
{
	struct desk_pen *pen = server->pen_now;
	if (!pen || !pen->v2->focused_surface || event->button != BTN_LEFT
		|| event->state != WL_POINTER_BUTTON_STATE_PRESSED) {
		return false;
	}
	pen->on_program = true;
	pen->off_x = server->cursor->x - pen->sx;
	pen->off_y = server->cursor->y - pen->sy;
	wlr_tablet_v2_tablet_tool_notify_down(pen->v2);
	wlr_tablet_tool_v2_start_implicit_grab(pen->v2);
	return true;
}

/* The pen has moved: where it now is is what is under it, a program, or else whatever is under a
 * pointer. */
static void pen_moved(struct desk_server *server, struct desk_pen *pen, uint32_t time)
{
	server->finger_screen = wlr_output_layout_output_at(server->output_layout, server->cursor->x,
		server->cursor->y);
	if (pen->on_program && server->cursor_mode == DESK_CUR_PASSTHROUGH) {
		wlr_tablet_v2_tablet_tool_notify_motion(pen->v2, server->cursor->x - pen->off_x,
			server->cursor->y - pen->off_y);
		return;
	}
	bool was_program = pen->v2->focused_surface != NULL;
	server->pen_now = pen;
	server->pen_took = false;
	mouse_back(server);
	process_cursor_motion(server, time);
	server->pen_now = NULL;
	if (!server->pen_took && was_program && !pen->on_program) {
		wlr_tablet_v2_tablet_tool_notify_proximity_out(pen->v2);
		/* (its picture of the pointer was the program's) */
		cursor_image(server, "default");
	}
}

static void pen_gone(struct wl_listener *listener, void *data)
{
	struct desk_pen *pen = wl_container_of(listener, pen, destroy);
	(void)data;
	if (pen->near) {
		pen->server->pens_near--;
	}
	wl_list_remove(&pen->set_cursor.link);
	wl_list_remove(&pen->destroy.link);
	wl_list_remove(&pen->link);
	free(pen);
}

/* A program that has the pen has its own picture of the pointer. */
static void pen_set_cursor(struct wl_listener *listener, void *data)
{
	struct desk_pen *pen = wl_container_of(listener, pen, set_cursor);
	struct wlr_tablet_v2_event_cursor *event = data;
	struct wlr_surface *focus = pen->v2->focused_surface;
	if (focus && event->seat_client && wl_resource_get_client(focus->resource) == event->seat_client->client) {
		wlr_cursor_set_surface(pen->server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

static struct desk_pen *pen_of(struct desk_server *server, struct wlr_tablet *tablet, struct wlr_tablet_tool *tool)
{
	struct desk_pen *pen = tool->data;
	if (!pen) {
		pen = calloc(1, sizeof *pen);
		if (!pen) {
			return NULL;
		}
		pen->server = server;
		pen->tool = tool;
		/* (told it is going before wlroots' half of it goes, which is made after) */
		pen->destroy.notify = pen_gone;
		wl_signal_add(&tool->events.destroy, &pen->destroy);
		pen->v2 = wlr_tablet_tool_create(server->tablet_mgr, server->seat, tool);
		if (!pen->v2) {
			wl_list_remove(&pen->destroy.link);
			free(pen);
			return NULL;
		}
		tool->data = pen;
		pen->set_cursor.notify = pen_set_cursor;
		wl_signal_add(&pen->v2->events.set_cursor, &pen->set_cursor);
		wl_list_insert(&server->pens, &pen->link);
	}
	pen->tablet = tablet->data;
	return pen->tablet ? pen : NULL;
}

/* Where the tablet says it is: a pen's own place, or the way a mouse on a tablet goes. */
static void pen_to(struct desk_server *server, struct desk_pen *pen, struct wlr_tablet *tablet, double x, double y,
	double dx, double dy)
{
	if (pen->tool->type == WLR_TABLET_TOOL_TYPE_MOUSE || pen->tool->type == WLR_TABLET_TOOL_TYPE_LENS) {
		wlr_cursor_move(server->cursor, &tablet->base, dx, dy);
	} else {
		wlr_cursor_warp_absolute(server->cursor, &tablet->base, x, y);
	}
}

/* The tip's letting go. */
static void pen_tip_up(struct desk_server *server, struct desk_pen *pen, uint32_t time)
{
	if (pen->on_program) {
		pen->on_program = false;
		wlr_tablet_v2_tablet_tool_notify_up(pen->v2);
		/* (it went down as a press, and the program had it) */
		if (server->cursor_mode == DESK_CUR_PASSTHROUGH) {
			if (server->buttons_down) {
				server->buttons_down--;
			}
			return;
		}
		/* the program asked to be moved, from the pen */
	}
	hand_button(server, BTN_LEFT, false, time);
}
