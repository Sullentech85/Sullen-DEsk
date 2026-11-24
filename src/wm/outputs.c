/* Part of wm.c: outputs and their frames.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static void server_cursor_axis(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, cursor_axis);
	server->pen_lately = false;
	mouse_back(server);
	pointer_axis(server, data);
}

static void server_cursor_frame(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, cursor_frame);
	(void)data;
	wlr_seat_pointer_notify_frame(server->seat);
}

/* The desk end of a move. Coming down from the bookcase, the console or
 * the post box, the camera goes a little past the desk and comes back to
 * it, as it does at the other end; the desk -- the room, what stands on
 * it, the windows and the folded sheets -- goes with it, until the move
 * is over. Setting off, it goes with it too, until the film is over it. */
/* How far the drawer, where it is, has pushed the desk up: 0 shut. */
static int drawer_lift(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	int ox0, oy0, ox1, oy1;
	double c0, c1;
	if (server->drawer_t <= 0.0005 || w < 8 || h < 8
		|| !desk_desk_place(w, h, 0.0, &ox0, &oy0, &c0)
		|| !desk_desk_place(w, h, server->drawer_t, &ox1, &oy1, &c1)) {
		return 0;
	}
	return oy1 - oy0;
}

/* Where the drawer begins on screen: what is above this is the desk. */
static double drawer_line(struct desk_server *server)
{
	int ox, oy;
	double clip;
	if (!desk_desk_place(server->desk_w, server->desk_h, server->drawer_t, &ox, &oy, &clip)) {
		return server->desk_h;
	}
	return clip;
}

static void desk_follow(struct desk_server *server)
{
	int which = -1;
	double t = 1.0;
	if (server->shelf_moving) {
		which = DESK_VIEW_SHELF;
		t = server->shelf_t;
	} else if (server->cc_moving) {
		which = DESK_VIEW_CONSOLE;
		t = server->cc_t;
	} else if (server->pb_moving) {
		which = DESK_VIEW_POST;
		t = server->pb_t;
	}
	int dx = 0, dy = 0;
	/* past a tenth of the way the film is over all of it */
	if (which >= 0 && t < 0.12) {
		double fx, fy;
		desk_tilt_desk_offset(server->desk_w, server->desk_h, which, t, &fx, &fy);
		dx = (int)lround(fx);
		dy = (int)lround(fy);
	}
	/* The drawer coming out pushes the desk up; the paper lying on it goes
	 * up with it. (The room and what is drawn over it are placed for the
	 * drawer where they are drawn: room_layout, overlay_place.) */
	int lift = drawer_lift(server);
	if (dx == server->desk_dx && dy == server->desk_dy && lift == server->win_dy) {
		return;
	}
	/* the windows on a screen beside the desk's stay where they lie: the
	 * paper is moved back by as much as the desk is moving it */
	int ddx = dx - server->desk_dx;
	int ddy = dy + lift - server->desk_dy - server->win_dy;
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->scene_tree && view->scene_tree->node.parent == server->layer_win
			&& screen_is_side(server, view_screen(view))) {
			struct wlr_scene_node *node = &view->scene_tree->node;
			wlr_scene_node_set_position(node, node->x - ddx, node->y - ddy);
			/* folded, it opens out to where the window is now */
			if (view->sheet.phase != SHEET_IDLE) {
				view->sheet.x0 -= ddx;
				view->sheet.y0 -= ddy;
			}
			view_sync_shadow(view);
		}
	}
	/* and so do the sheets folded on one, lying there or on their way to
	 * it (not one in the hand, which is where the hand is) */
	wl_list_for_each(view, &server->toplevels, link) {
		struct desk_sheet *s = &view->sheet;
		if (s->phase == SHEET_IDLE || s->in_drawer || s->to_drawer
			|| (s->tree && s->tree->node.parent == server->layer_drag)
			|| !screen_is_side(server, sheet_screen(server, s))) {
			continue;
		}
		s->x1 -= ddx;
		s->y1 -= ddy;
		s->hit_x -= ddx;
		s->hit_y -= ddy;
		if (s->tree) {
			wlr_scene_node_set_position(&s->tree->node, s->tree->node.x - ddx,
				s->tree->node.y - ddy);
		}
	}
	bool moved = dx != server->desk_dx || dy != server->desk_dy;
	server->desk_dx = dx;
	server->desk_dy = dy;
	server->win_dy = lift;
	if (moved && screens_beside(server)) {
		/* what of the room is on the desk's screen is another part now */
		room_layout(server);
	}
	struct wlr_scene_tree *desk[] = { server->layer_bg, server->layer_desk, server->layer_drawer };
	for (size_t i = 0; i < sizeof desk / sizeof desk[0]; i++) {
		if (desk[i]) {
			wlr_scene_node_set_position(&desk[i]->node, dx, dy);
		}
	}
	struct wlr_scene_tree *paper[] = {
		server->layer_scars, server->layer_folded, server->layer_win, server->layer_xover,
		server->layer_parked,
	};
	for (size_t i = 0; i < sizeof paper / sizeof paper[0]; i++) {
		if (paper[i]) {
			wlr_scene_node_set_position(&paper[i]->node, dx, dy + lift);
		}
	}
}

/* What moves, moved on a frame: the paper and everything else on the
 * desk, the views and their moves, the login, the door, the shows. */
static void output_frame_work(struct desk_server *server)
{
	folds_tick(server);
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->tick) {
			(*t)->tick(server);
		}
	}
	switcher_tick(server);
	snip_tick(server);
	paper_tick(server);
	curls_tick(server);
	paper_throw_tick(server);
	sizing_tick(server);
	unfold_tick(server);
	room_tick(server);
	throws_tick(server);
	hand_crumple_tick(server);
	drawer_tick(server);
	drawer_slide_tick(server);
	looks_tick(server);
	shelf_tick(server);
	console_tick(server);
	post_tick(server);
	binder_tick(server);
	cal_tick(server);
	memo_tick(server);
	boot_tick(server);
	relight_tick(server);
	mat_roll_tick(server);
	lock_tick(server);
	show_tick(server);
	agd_tick(server);
	agd_fly_tick(server, monotonic_sec());
	binder_warm_tick(server);
	take_tick(server);
	binder_pos_tick(server);
	pb_pos_tick(server);
	pbc_tick(server);
	bnc_tick(server);
	hover_tick(server);
	desk_follow(server);
	clock_tick_frame(server);
	slips_tick_frame(server);
}

/* A screen's frame gone out: Print's picture of it, if one was asked for
 * (snip_freeze). */
static void output_commit(struct wl_listener *listener, void *data)
{
	struct desk_output *output = wl_container_of(listener, output, commit);
	const struct wlr_output_event_commit *ev = data;
	if (!output->snip_want || !(ev->state->committed & WLR_OUTPUT_STATE_BUFFER)
		|| !ev->state->buffer) {
		return;
	}
	output->snip_want = false;
	snip_still_take(output->server, output->wlr_output, ev->state->buffer);
	snip_still_landed(output->server);
}

static void output_frame(struct wl_listener *listener, void *data)
{
	struct desk_output *output = wl_container_of(listener, output, frame);
	(void)data;
	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(output->server->scene,
		output->wlr_output);
	/* Off, or dark: nothing is drawn, nothing moves, and no program is
	 * asked for its next frame. Nested, the black over it goes out once. */
	if (!output->wlr_output->enabled || !scene_output || output->dark_shown) {
		return;
	}
	if (output->dark) {
		output->dark_shown = true;
		wlr_scene_output_commit(scene_output, NULL);
		return;
	}
	double f0 = g_prof.on ? real_sec() : 0.0;
	double c0 = g_trace.f ? thread_cpu_sec() : 0.0;
	/* What moves is moved once a frame, on the desk's screen's frames --
	 * or another's, if the desk's has not had one for most of a frame --
	 * not once for every screen there is. */
	double tick_now = monotonic_sec();
	bool tick = output->wlr_output == first_output(output->server)
		|| tick_now - output->server->ticked_at > 0.012;
	if (tick) {
		output->server->ticked_at = tick_now;
		output_frame_work(output->server);
	}
	double f1 = g_prof.on ? real_sec() : 0.0;
	double c1 = g_trace.f ? thread_cpu_sec() : 0.0;
	if (output->server->rec_output == output->wlr_output) {
		rec_output_commit(output->server, scene_output);
	} else if (output->gamma_dirty) {
		night_commit(output, scene_output);
	} else {
		wlr_scene_output_commit(scene_output, NULL);
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
	if (!output->server->first_frame_logged) {
		output->server->first_frame_logged = true;
		wlr_log(WLR_INFO, "the first frame (%.2f s)", real_sec() - g_start_real);
	}
	if (g_prof.on) {
		double f2 = real_sec();
		trace_frame(f0, f1, f2, c0, c1, g_trace.f ? thread_cpu_sec() : 0.0,
			g_prof.asked > 0.0 ? f0 - g_prof.asked : -1.0);
		/* How long a frame took to arrive once it was asked for. Up to a
		 * refresh is the display's own pace; late is more than one and a
		 * half of them. A pause between two things happening is not late:
		 * nobody asked for a frame in it. */
		int mhz = output->wlr_output->refresh;
		double budget = mhz > 0 ? 1000.0 / mhz : 1.0 / 60.0;
		if (g_prof.asked > 0.0) {
			double wait = f0 - g_prof.asked;
			g_prof.gap_max = wait > g_prof.gap_max ? wait : g_prof.gap_max;
			g_prof.late += wait > budget * 1.5;
			g_prof.asked = 0.0;
		}
		g_prof.last_frame = f0;
		g_prof.frames++;
		double fm = (f2 - f0) * 1000.0, cm = (f2 - f1) * 1000.0;
		g_prof.frame_ms += fm;
		g_prof.commit_ms += cm;
		g_prof.frame_max = fm > g_prof.frame_max ? fm : g_prof.frame_max;
		g_prof.commit_max = cm > g_prof.commit_max ? cm : g_prof.commit_max;
		prof_report(f2);
	}
}

static void output_request_state(struct wl_listener *listener, void *data)
{
	struct desk_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
	if (first_output(output->server) == output->wlr_output) {
		desk_resize_to(output->server, output->wlr_output->width,
			output->wlr_output->height);
	}
	om_publish(output->server);
}

static void output_destroy(struct wl_listener *listener, void *data)
{
	struct desk_output *output = wl_container_of(listener, output, destroy);
	struct desk_server *server = output->server;
	(void)data;
	bool window = output_is_window(output->wlr_output), was_dark = output->dark;
	/* the screens as they are set out without it: DISPLAY's way round goes */
	output->mirror = false;
	if (!server->quitting) {
		if (server->screens_home == output->wlr_output) {
			server->screens_home = NULL;
		}
		screens_forget(server);
	}
	/* Print's picture of it would be of a screen that is not there (and on
	 * the way out, the scene it is in has gone already); and a film of it
	 * ends here */
	if (!server->quitting) {
		snip_stop(server);
	}
	if (server->rec_output == output->wlr_output) {
		rec_let_go(server);
		rec_end(server);
	}
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->commit.link);
	wl_list_remove(&output->link);
	/* on the way out the scene, and the black in it, has gone already */
	if (output->dark_cover && !server->quitting) {
		wlr_scene_node_destroy(&output->dark_cover->node);
	}
	if (output->side && !server->quitting) {
		wlr_scene_node_destroy(&output->side->node);
	}
	screen_dark_sync(server);
	if (!server->quitting && !wl_list_empty(&server->outputs)) {
		/* out of the layout now, not once it is gone: until then it is
		 * still the screen at the corner */
		wlr_output_layout_remove(server->output_layout, output->wlr_output);
		/* the desk goes over to a screen that is left, which is moved to
		 * the corner it is drawn from, the rest with it */
		struct wlr_output *next = first_output(server);
		struct wlr_box at;
		wlr_output_layout_get_box(server->output_layout, next, &at);
		if (!wlr_box_empty(&at) && (at.x != 0 || at.y != 0)) {
			struct desk_output *o;
			wl_list_for_each(o, &server->outputs, link) {
				struct wlr_box box;
				wlr_output_layout_get_box(server->output_layout, o->wlr_output, &box);
				if (!wlr_box_empty(&box)) {
					wlr_output_layout_add(server->output_layout, o->wlr_output,
						box.x - at.x, box.y - at.y);
				}
			}
		}
		int w, h;
		wlr_output_effective_resolution(next, &w, &h);
		desk_resize_to(server, w, h);
		om_publish(server);
		/* the ones left may have been set out some way of their own */
		displays_changed(server);
	}
	/* the window still being named may be the one that just closed */
	if (server->name_output == output->wlr_output) {
		server->name_output = NULL;
	}
	free(output);

	/* Nested, the output IS the window, so closing the window has to end
	 * the session. Without this the compositor went on running with
	 * nothing to draw on -- invisible, holding its wayland socket, and
	 * indistinguishable from a machine that had simply stopped.
	 *
	 * A real screen going is not the end, though: a monitor unplugged, a
	 * switch turned to another machine, or one asleep after the desk went
	 * dark that says it has been unplugged (many do: an HDMI or DisplayPort
	 * monitor saving power drops its hot-plug line) -- which ended the
	 * session, and every program in it, ten minutes after anybody had last
	 * touched it, and left a black screen nothing would wake (#18). The
	 * desk waits for a screen to come; dark as the one that went, if it
	 * went dark (server_new_output). */
	if (!server->headless && wl_list_empty(&server->outputs)) {
		if (window || server->quitting) {
			wlr_log(WLR_INFO, "last output went away; shutting down");
			wl_display_terminate(server->wl_display);
		} else {
			server->dark_away = was_dark;
			wlr_log(WLR_INFO, "the last screen went away%s: waiting for one to come",
				was_dark ? " while it was dark (a monitor asleep may say it is unplugged)"
				: "");
			idle_rearm(server);
		}
	}
}

/* ---- the window we are in, when we are in one ------------------------
 *
 * Nested, DEsk is a window on somebody else's desktop, and wlroots calls
 * it "wlroots - X11-1" and gives it no class and no pid -- so the desktop's
 * dock cannot tell it from any other program: it shows a nameless window
 * beside the launcher that started it. Named, classed as the launcher's
 * desktop file is, and carrying our pid, it is the launcher's own window.
 * On a Wayland desktop that is the app_id; on X it has to be done from a
 * connection of our own, because wlroots keeps its window to itself -- so
 * the window is found by a title nobody else has, and then given the real
 * one. */
#define DESK_APP_ID "desk-window"

#if WLR_HAS_X11_BACKEND && defined(DESK_HAVE_XWAYLAND)
static xcb_atom_t x_atom(xcb_connection_t *c, const char *name)
{
	xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c,
		xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
	xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
	free(r);
	return a;
}

static xcb_window_t x_find_titled(xcb_connection_t *c, xcb_window_t w, xcb_atom_t name,
	xcb_atom_t utf8, const char *tag, int depth)
{
	xcb_get_property_reply_t *pr = xcb_get_property_reply(c,
		xcb_get_property(c, 0, w, name, utf8, 0, 64), NULL);
	bool hit = pr && xcb_get_property_value_length(pr) == (int)strlen(tag)
		&& memcmp(xcb_get_property_value(pr), tag, strlen(tag)) == 0;
	free(pr);
	if (hit) {
		return w;
	}
	if (depth <= 0) {
		return XCB_WINDOW_NONE;
	}
	/* a window manager puts a frame round it, so it is a level or two down */
	xcb_query_tree_reply_t *tr = xcb_query_tree_reply(c, xcb_query_tree(c, w), NULL);
	xcb_window_t found = XCB_WINDOW_NONE;
	if (tr) {
		xcb_window_t *kids = xcb_query_tree_children(tr);
		int n = xcb_query_tree_children_length(tr);
		for (int i = n - 1; i >= 0 && found == XCB_WINDOW_NONE; i--) {
			found = x_find_titled(c, kids[i], name, utf8, tag, depth - 1);
		}
		free(tr);
	}
	return found;
}

static int name_timer_cb(void *data)
{
	struct desk_server *server = data;
	struct wlr_output *out = server->name_output;
	bool done = false;
	xcb_connection_t *c = xcb_connect(server->parent_display, NULL);
	if (c && !xcb_connection_has_error(c)) {
		xcb_atom_t name = x_atom(c, "_NET_WM_NAME"), utf8 = x_atom(c, "UTF8_STRING");
		xcb_atom_t pid_atom = x_atom(c, "_NET_WM_PID");
		xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
		xcb_window_t w = x_find_titled(c, root, name, utf8, server->name_tag, 3);
		if (w != XCB_WINDOW_NONE) {
			static const char cls[] = DESK_APP_ID "\0" DESK_APP_ID;
			xcb_void_cookie_t k1 = xcb_change_property_checked(c, XCB_PROP_MODE_REPLACE, w,
				XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof cls, cls);
			uint32_t pid = (uint32_t)getpid();
			xcb_void_cookie_t k2 = xcb_change_property_checked(c, XCB_PROP_MODE_REPLACE, w,
				pid_atom, XCB_ATOM_CARDINAL, 32, 1, &pid);
			/* waited for: closing straight after sending them lost them */
			free(xcb_request_check(c, k1));
			free(xcb_request_check(c, k2));
			server->host_window = w;
			done = true;
		}
	}
	if (c) {
		xcb_disconnect(c);
	}
	if (done || ++server->name_tries >= 20) {
		if (out) {
			wlr_x11_output_set_title(out, "DEsk");
		}
		wl_event_source_remove(server->name_timer);
		server->name_timer = NULL;
		server->name_output = NULL;
		return 0;
	}
	wl_event_source_timer_update(server->name_timer, 150);
	return 0;
}

/* Where DEsk's window is on the screen it runs in, when it runs in a
 * window of an X server: for the input method's windows, which go on that
 * screen. */
static bool host_origin(struct desk_server *server, int *ox, int *oy)
{
	if (!server->parent_display || !server->host_window) {
		return false;
	}
	xcb_connection_t *c = server->host_xcb;
	if (!c || xcb_connection_has_error(c)) {
		if (c) {
			xcb_disconnect(c);
		}
		c = xcb_connect(server->parent_display, NULL);
		server->host_xcb = c;
		if (!c || xcb_connection_has_error(c)) {
			return false;
		}
	}
	xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
	xcb_translate_coordinates_reply_t *r = xcb_translate_coordinates_reply(c,
		xcb_translate_coordinates(c, server->host_window, root, 0, 0), NULL);
	if (!r) {
		return false;
	}
	*ox = r->dst_x;
	*oy = r->dst_y;
	free(r);
	return true;
}
#else
static bool host_origin(struct desk_server *server, int *ox, int *oy)
{
	(void)server;
	*ox = *oy = 0;
	return false;
}
#endif

static void nested_name(struct desk_server *server, struct wlr_output *out)
{
	if (wlr_output_is_wl(out)) {
		wlr_wl_output_set_title(out, "DEsk");
		wlr_wl_output_set_app_id(out, DESK_APP_ID);
		return;
	}
#if WLR_HAS_X11_BACKEND
	if (!wlr_output_is_x11(out)) {
		return;
	}
#ifdef DESK_HAVE_XWAYLAND
	if (server->parent_display && !server->name_timer) {
		snprintf(server->name_tag, sizeof server->name_tag, "DEsk %d finding itself",
			(int)getpid());
		wlr_x11_output_set_title(out, server->name_tag);
		server->name_output = out;
		server->name_tries = 0;
		server->name_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), name_timer_cb, server);
		if (server->name_timer) {
			wl_event_source_timer_update(server->name_timer, 150);
			return;
		}
	}
#else
	(void)server;   /* without xcb there is no finding the window to class it */
#endif
	wlr_x11_output_set_title(out, "DEsk");
#else
	(void)server;
#endif
}

/* Pixels to a point, the way a Mac has it: 1 on an ordinary screen, 2 on a
 * Retina one. DESK_SCALE says so outright; otherwise it is the screen's
 * density against 96 dots to the inch, in quarters, and 1 below one and a
 * half -- a laptop panel at 120 dpi is still an ordinary screen to anybody
 * sitting at it. A screen that does not say how big it is (a window on
 * another desktop, a projector, an EDID that gives the shape in place of
 * the size) is taken to be ordinary. A display tool's scale for the screen
 * is written into Shell Settings' (om_try). */
static double ui_scale_for(struct desk_server *server, struct wlr_output *out)
{
	double scale = 1.0;
	const char *env = getenv("DESK_SCALE");
	if (env && *env && atof(env) > 0.0) {
		scale = atof(env);
	} else if (server->conf.scale > 0.0) {
		/* Shell Settings, or a display tool through them */
		scale = server->conf.scale;
	} else if (out->phys_width >= 100 && out->phys_height >= 60 && out->width > 0) {
		double dpi = out->width * 25.4 / out->phys_width;
		if (dpi >= 144.0) {
			scale = round(dpi / 96.0 * 4.0) / 4.0;
		}
	}
	return clampf_d(scale, 0.5, 4.0);
}

static void ui_scale_pick(struct desk_server *server, struct wlr_output *out)
{
	double scale = ui_scale_for(server, out);
	desk_set_ui_scale(scale);
	wlr_log(WLR_INFO, "%s: %dx%d, %dx%d mm: a point is %.2f pixels", out->name,
		out->width, out->height, out->phys_width, out->phys_height, scale);
}
static void server_new_output(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;
	/* the desk already set out: a screen plugged in after the start, or
	 * come back after the last one went (output_destroy) */
	bool again = server->desk_w >= 8 && server->desk_h >= 8;
	/* A real screen plugged in while nobody is here, or come back while
	 * the desk is dark, comes in dark: not lit for a moment and put out
	 * again, which would wake a monitor that drops its hot-plug line
	 * asleep and raises it now and then, every time it did. It keeps the
	 * mode it is to be lit in (output_set_dark). */
	bool wait_dark = (server->dark || server->dark_away) && !output_is_window(wlr_output);
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);
	if (wlr_output_is_wl(wlr_output)) {
		/* A window on another Wayland desktop: the pointer drawn into the
		 * window's own picture, since some desktops do not show the
		 * pointer a window asks for over it (Gberfield, PR #29). */
		wlr_output_lock_software_cursors(wlr_output, true);
		wlr_log(WLR_INFO, "nested Wayland output: drawing cursor in the window");
	}
	/* --size picks the window's size under a nested backend too, not just
	 * the headless one: an X11 window would otherwise come up at whatever
	 * wlroots defaults to. */
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (!wait_dark) {
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, true);
		if (server->size_set) {
			wlr_output_state_set_custom_mode(&state, server->headless_w,
				server->headless_h, 0);
		} else if (mode != NULL) {
			wlr_output_state_set_mode(&state, mode);
		}
		wlr_output_commit_state(wlr_output, &state);
		wlr_output_state_finish(&state);
	}
	nested_name(server, wlr_output);

	struct desk_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	output->commit.notify = output_commit;
	wl_signal_add(&wlr_output->events.commit, &output->commit);
	wl_list_insert(&server->outputs, &output->link);
	/* the night's colours, if they are on */
	output->gamma_dirty = server->cc_night;
	/* (one more screen: as they are set out, whatever DISPLAY had made of them) */
	screens_forget(server);
	if (wait_dark) {
		output->dark = true;
		output->dark_mode = server->size_set ? NULL : mode;
		output->dark_w = server->size_set ? server->headless_w : mode ? mode->width : 0;
		output->dark_h = server->size_set ? server->headless_h : mode ? mode->height : 0;
		output->dark_refresh = !server->size_set && mode ? mode->refresh : 0;
	}

	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout, wlr_output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);

	if (first_output(server) == wlr_output && again) {
		/* the desk's screen come back, or another in its place: the desk
		 * set out again for it, as when the desk moves to a screen that
		 * is left (output_destroy) */
		int w = output->dark_w, h = output->dark_h;
		if (!wait_dark) {
			wlr_output_effective_resolution(wlr_output, &w, &h);
		}
		wlr_log(WLR_INFO, "%s: %dx%d, the desk's screen again%s", wlr_output->name, w, h,
			wait_dark ? ", waiting dark until somebody is back" : "");
		desk_resize_to(server, server->size_set ? server->headless_w : w,
			server->size_set ? server->headless_h : h);
	} else if (first_output(server) == wlr_output) {
		/* The size we ASKED for, not the one the backend happens to be
		 * showing this instant. Under X11 the window comes up at the
		 * backend's own default and only reaches the custom mode a beat
		 * later -- and by then the first paint has already put a whole
		 * second room on the ray tracer at 1024x768, which costs a minute
		 * of four cores and is thrown away unlooked at. */
		server->desk_w = server->size_set ? server->headless_w : wlr_output->width;
		server->desk_h = server->size_set ? server->headless_h : wlr_output->height;
		ui_scale_pick(server, wlr_output);
		g_motion_fine = desk_px_per_pt(server->desk_w);
	} else {
		/* a second screen plugged in is more room for windows; the desk
		 * stays where it is */
		wlr_log(WLR_INFO, "%s: %dx%d, a screen beside the desk's", wlr_output->name,
			wlr_output->width, wlr_output->height);
	}
	/* pointers that belong to this screen, here before it */
	struct desk_pointer *pt;
	wl_list_for_each(pt, &server->pointers, link) {
		input_map_to_output(server, pt->device);
	}
	touch_screens_map(server);
	/* what is on the shelf, read on a worker while the room bakes */
	desk_apps_init(true);
	desktop_paint(server);
	if (server->dump_path) {
		desk_write_png(server->dump_path, server->desk_w, server->desk_h, true);
		wlr_log(WLR_INFO, "dumped chrome to %s", server->dump_path);
	}
	/* plugged in while nobody is here: it waits dark with the rest -- or,
	 * when the last screen went while dark, as that one would have; a key
	 * or the pointer lights it. Plugged in otherwise, it is somebody at
	 * the desk. */
	if (server->dark || server->dark_away) {
		/* a real screen has come in dark already (wait_dark), a window
		 * on another desktop has its black put over it */
		output_set_dark(output, true);
		server->dark_away = false;
		screen_dark_sync(server);
		idle_rearm(server);
	} else if (again) {
		server->idle_at = real_sec();
		idle_rearm(server);
	}
	om_publish(server);
	/* and the screens set out as they were last time these were here */
	displays_changed(server);
}

/* ---- an input method's own windows -----------------------------------
 *
 * IBus's panel shows the candidates -- Mozc's among them, which on Wayland
 * gives up its own window for IBus's -- in a window of its own, and on
 * Wayland that is a toplevel like any program's: it came onto the desk as
 * a sheet of paper, offered a sheet's size, took the keys from the text
 * being written, and was crumpled up and thrown away every time it went,
 * which is every key or two. It is not a window on the desk but a moment
 * of one, as an override-redirect X window is (xview_passing): its own
 * size, nothing of ours round it, never the keys, over every window, and
 * gone without a word. And as a Wayland window cannot say where it goes,
 * it goes where IBus puts it on X: under the text cursor. */
static bool ime_window_is(const struct desk_toplevel *view)
{
	return view->xdg_toplevel && ime_window_name(view->xdg_toplevel->app_id);
}

/* Under the cursor of the text being written, or over it where there is
 * no room under it, and all of it on the screen the cursor is on; with
 * nothing being written, a third of the way down the screen. */
static void ime_window_place(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	struct wlr_xdg_surface *xs = view->xdg_toplevel->base;
	struct wlr_box g;
	wlr_xdg_surface_get_geometry(xs, &g);
	int w = g.width > 0 ? g.width : xs->surface->current.width;
	int h = g.height > 0 ? g.height : xs->surface->current.height;
	struct wlr_box at = server->ime_at;
	struct wlr_output *out = at.height > 0 ? wlr_output_layout_output_at(server->output_layout,
		at.x + at.width * 0.5, at.y + at.height * 0.5) : NULL;
	if (!out) {
		out = first_output(server);
	}
	struct wlr_box sc = { 0 };
	if (out) {
		wlr_output_layout_get_box(server->output_layout, out, &sc);
	}
	int x, y;
	if (at.height > 0) {
		x = at.x;
		y = at.y + at.height;
		if (sc.height > 0 && y + h > sc.y + sc.height && at.y - h >= sc.y) {
			y = at.y - h;
		}
	} else {
		x = sc.x + (sc.width - w) / 2;
		y = sc.y + sc.height / 3;
	}
	if (sc.width > 0) {
		x = x > sc.x + sc.width - w ? sc.x + sc.width - w : x;
		x = x < sc.x ? sc.x : x;
		y = y > sc.y + sc.height - h ? sc.y + sc.height - h : y;
		y = y < sc.y ? sc.y : y;
	}
	wlr_scene_node_set_position(&view->scene_tree->node, x - g.x, y - g.y);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	(void)data;
	if (toplevel->ime_popup) {
		wlr_scene_node_reparent(&toplevel->scene_tree->node, toplevel->server->layer_xover);
		ime_window_place(toplevel);
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		return;
	}
	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	/* onto the sheet taken out of a binder for it, or the next place down */
	struct desk_base *b = toplevel->xdg_toplevel->parent ? NULL
		: base_claim(toplevel->server, toplevel);
	int x, y;
	if (b) {
		x = b->x;
		y = b->y;
	} else {
		cascade_next(toplevel->server, &x, &y);
	}
	wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	/* A client that never asked for our frame draws its own -- a GTK one
	 * with a header bar does: it is laid on our paper, or left as it is
	 * (shell.conf) */
	view_frame_init(toplevel);
	view_repaint_chrome(toplevel);
	view_sync_shadow(toplevel);
	view_check_above(toplevel);
	if (!toplevel->thing || !(toplevel->thing->no & THING_NO_KEYS)) {
		focus_toplevel(toplevel);
	}
	base_after_map(toplevel->server, toplevel);
	/* asked to be opened out, or for the whole screen, before it was up --
	 * a game made on the whole screen: too early to be done then
	 * (xdg_toplevel_request_*), so now; opened out first, as for X */
	if (toplevel->xdg_toplevel->requested.maximized) {
		view_toggle_max_now(toplevel);
	}
	if (toplevel->xdg_toplevel->requested.fullscreen) {
		view_set_fullscreen(toplevel, true);
	}
	view_fit_on_screen(toplevel);
	wlr_log(WLR_INFO, "a window comes onto the desk: %s (%s) at %d,%d, %dx%d, %s%s",
		view_title(toplevel), toplevel->xdg_toplevel->app_id ? toplevel->xdg_toplevel->app_id : "-",
		toplevel->scene_tree->node.x, toplevel->scene_tree->node.y,
		toplevel->outer_w, toplevel->outer_h,
		toplevel->bare ? "its own frame" : toplevel->own_frame ? "its own frame, on paper"
			: "on our paper",
		toplevel->fullscreen ? ", on the whole screen" : toplevel->maximized ? ", opened out" : "");
}
