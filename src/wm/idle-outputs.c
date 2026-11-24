/* Part of wm.c: going idle, output management, displays and turning the screen.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* How long nobody touching anything takes to turn the screen off; 0 never. */
static int idle_ms(const struct desk_server *server)
{
	const char *test = getenv("DESK_IDLE_TEST_S");
	int ms = test && atoi(test) > 0 ? atoi(test) * 1000 : server->conf.screen_off * 60000;
	/* the door need not stand lit long for nobody */
	if (ms > IDLE_AT_DOOR_MS && locked(server)) {
		ms = IDLE_AT_DOOR_MS;
	}
	return ms;
}

/* Has every screen that is on gone dark? With none, whether the desk had
 * gone dark when the last went. */
static bool screen_all_dark(struct desk_server *server)
{
	if (wl_list_empty(&server->outputs)) {
		return server->dark_away;
	}
	bool any = false;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (!o->off && !o->dark) {
			return false;
		}
		any |= o->dark;
	}
	return any;
}

/* The timer set for when the wait runs out, counted from the last touch;
 * none while it is dark already, or never to be. */
static void idle_rearm(struct desk_server *server)
{
	if (!server->idle_timer) {
		return;
	}
	int ms = idle_ms(server);
	if (ms <= 0 || screen_all_dark(server)) {
		wl_event_source_timer_update(server->idle_timer, 0);
		return;
	}
	double left = server->idle_at + ms / 1000.0 - real_sec();
	wl_event_source_timer_update(server->idle_timer,
		left > 0.001 ? (int)ceil(left * 1000.0) : 1);
}

/* Is a program holding the screen up: a window that asked, out on the desk
 * to be seen -- not folded away, not in the drawer, not behind the door? */
static bool idle_held(struct desk_server *server)
{
	if (locked(server)) {
		return false;
	}
#ifdef DESK_HAVE_POST
	if (desk_saver_holds(NULL, 0) > 0) {
		return true;   /* asked on the bus, with no window to go by */
	}
#endif
	struct desk_idle_hold *hold;
	wl_list_for_each(hold, &server->idle_holds, link) {
		struct wlr_surface *s = wlr_surface_get_root_surface(hold->inhibitor->surface);
		if (!s || !s->mapped) {
			continue;
		}
		struct desk_toplevel *view = view_of_surface(server, s);
		if (!view || view->sheet.phase == SHEET_IDLE) {
			return true;
		}
	}
	return false;
}

static void idle_hold_update(struct desk_server *server)
{
	bool held = idle_held(server);
	if (held == server->idle_held) {
		return;
	}
	server->idle_held = held;
	if (server->idle_notifier) {
		wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, held);
	}
	wlr_log(WLR_INFO, "screen: %s", held ? "kept lit for a program (idle inhibitor, or on "
		"the bus)" : "no program keeping it lit any more");
	if (!held) {
		/* let go of: counted from now, as if somebody had just looked up */
		server->idle_at = real_sec();
		idle_rearm(server);
	}
}

static void idle_hold_gone(struct wl_listener *listener, void *data)
{
	struct desk_idle_hold *hold = wl_container_of(listener, hold, destroy);
	struct desk_server *server = hold->server;
	(void)data;
	wl_list_remove(&hold->destroy.link);
	wl_list_remove(&hold->map.link);
	wl_list_remove(&hold->unmap.link);
	wl_list_remove(&hold->link);
	free(hold);
	idle_hold_update(server);
}

static void idle_hold_map(struct wl_listener *listener, void *data)
{
	struct desk_idle_hold *hold = wl_container_of(listener, hold, map);
	(void)data;
	idle_hold_update(hold->server);
}

static void idle_hold_unmap(struct wl_listener *listener, void *data)
{
	struct desk_idle_hold *hold = wl_container_of(listener, hold, unmap);
	(void)data;
	idle_hold_update(hold->server);
}

static void server_new_idle_hold(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_idle_hold);
	struct wlr_idle_inhibitor_v1 *inhibitor = data;
	struct desk_idle_hold *hold = calloc(1, sizeof *hold);
	if (!hold) {
		return;
	}
	hold->server = server;
	hold->inhibitor = inhibitor;
	hold->destroy.notify = idle_hold_gone;
	wl_signal_add(&inhibitor->events.destroy, &hold->destroy);
	hold->map.notify = idle_hold_map;
	wl_signal_add(&inhibitor->surface->events.map, &hold->map);
	hold->unmap.notify = idle_hold_unmap;
	wl_signal_add(&inhibitor->surface->events.unmap, &hold->unmap);
	wl_list_insert(&server->idle_holds, &hold->link);
	idle_hold_update(server);
}

/* Is this screen a window on another desktop? */
static bool output_is_window(struct wlr_output *out)
{
#if WLR_HAS_X11_BACKEND
	if (wlr_output_is_x11(out)) {
		return true;
	}
#endif
	return wlr_output_is_wl(out);
}

static bool output_power(struct wlr_output *out, bool on, struct wlr_output_mode *mode,
	int w, int h, int refresh)
{
	struct wlr_output_state st;
	wlr_output_state_init(&st);
	wlr_output_state_set_enabled(&st, on);
	if (on && mode) {
		wlr_output_state_set_mode(&st, mode);
	} else if (on && w > 0 && h > 0) {
		wlr_output_state_set_custom_mode(&st, w, h, refresh);
	}
	bool ok = wlr_output_commit_state(out, &st);
	wlr_output_state_finish(&st);
	return ok;
}

/* One screen dark, or lit again in the mode it was in. */
static void output_set_dark(struct desk_output *o, bool dark)
{
	struct desk_server *server = o->server;
	struct wlr_output *out = o->wlr_output;
	if (o->dark == dark || o->off) {
		return;
	}
	if (output_is_window(out)) {
		if (dark) {
			struct wlr_box box;
			wlr_output_layout_get_box(server->output_layout, out, &box);
			const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			o->dark_cover = wlr_scene_rect_create(server->layer_dark,
				box.width > 0 ? box.width : out->width,
				box.height > 0 ? box.height : out->height, black);
			if (o->dark_cover) {
				wlr_scene_node_set_position(&o->dark_cover->node, box.x, box.y);
			}
		} else if (o->dark_cover) {
			wlr_scene_node_destroy(&o->dark_cover->node);
			o->dark_cover = NULL;
		}
		o->dark = dark;
		o->dark_shown = false;
		wlr_output_schedule_frame(out);
		return;
	}
	if (dark) {
		o->dark_mode = out->current_mode;
		o->dark_w = out->width;
		o->dark_h = out->height;
		o->dark_refresh = out->refresh;
		if (!output_power(out, false, NULL, 0, 0, 0)) {
			wlr_log(WLR_ERROR, "screen: %s would not turn off", out->name);
			return;
		}
		o->dark = true;
		return;
	}
	/* lit again whatever happens: a screen that will not come back must
	 * not leave every key and click after it swallowed as waking it */
	o->dark = false;
	if (!output_power(out, true, o->dark_mode, o->dark_w, o->dark_h, o->dark_refresh)
		&& !output_power(out, true, NULL, 0, 0, 0)) {
		wlr_log(WLR_ERROR, "screen: %s would not turn on again", out->name);
	}
	wlr_output_schedule_frame(out);
}

static void displays_changed(struct desk_server *server);

static void screen_dark_sync(struct desk_server *server)
{
	server->dark = false;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		server->dark |= o->dark;
	}
	if (!server->dark && server->disp_after_dark) {
		/* screens that came while it was dark, set out now it is lit */
		server->disp_after_dark = false;
		displays_changed(server);
	}
}

/* Nobody here: the door down first, if it is to come down, and then dark. */
static void screen_go_dark(struct desk_server *server)
{
	double idle = real_sec() - server->idle_at;
	bool lock = server->conf.lock_screen_off && !locked(server);
	if (lock) {
		lock_begin(server, LOCK_COME_NOW, NULL, LOCK_FROM_ELSEWHERE);
	}
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		output_set_dark(o, true);
	}
	/* with no screen there, it is the next to come that waits dark */
	server->dark_away = wl_list_empty(&server->outputs);
	screen_dark_sync(server);
	idle_rearm(server);
	wlr_log(WLR_INFO, "screen: nobody has touched anything for %.0f s: dark%s", idle,
		lock ? ", the door locked first" : "");
}

/* Somebody back: every screen lit again, the door's caret going. */
static void screen_wake(struct desk_server *server)
{
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		output_set_dark(o, false);
	}
	server->dark_away = false;
	screen_dark_sync(server);
	server->idle_at = real_sec();
	idle_rearm(server);
	if (locked(server)) {
		server->lock_caret_on = true;
		if (server->lock_timer) {
			wl_event_source_timer_update(server->lock_timer, LOCK_CARET_MS);
		}
		paint_lock(server);
	}
	server_schedule_frames(server);
}

static int idle_cb(void *data)
{
	struct desk_server *server = data;
	int ms = idle_ms(server);
	if (ms <= 0 || screen_all_dark(server)) {
		return 0;
	}
	double idle = real_sec() - server->idle_at;
	if (idle * 1000.0 < ms - 5) {
		/* touched since the timer was set: set it again from then */
		idle_rearm(server);
		return 0;
	}
	if (idle_held(server)) {
		/* a film playing is somebody watching it */
		server->idle_at = real_sec();
		idle_rearm(server);
		return 0;
	}
	screen_go_dark(server);
	return 0;
}

/* Somebody at the desk: a key, the pointer, the screen touched. Counts
 * against the screen going dark, and programs that ask are told. When it
 * had gone dark, this brings it back, and a press does nothing else: false,
 * and the caller drops it -- this one, and any other press until the screen
 * has had time to be seen (a monitor takes a moment to light, and a hand
 * that nudged the mouse to wake it is often already clicking) -- and any
 * press while there is no screen at all. The pointer moving goes where it
 * goes either way. */
static bool input_seen(struct desk_server *server, const char *what)
{
	double now = real_sec();
	server->idle_at = now;
	if (server->idle_notifier) {
		wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
	}
	if (server->dark) {
		wlr_log(WLR_INFO, "screen: woken by %s", what);
		screen_wake(server);
		server->wake_at = now;
		return false;
	}
	if (wl_list_empty(&server->outputs) && !server->headless) {
		/* no screen at all -- a monitor asleep that says it has been
		 * unplugged, or one taken away: somebody back says the next to
		 * come is lit, and a press lands on nothing, as on a dark screen,
		 * for nobody can see what it would do */
		if (server->dark_away) {
			wlr_log(WLR_INFO, "screen: woken by %s, with no screen yet: the next to come "
				"is lit", what);
			server->dark_away = false;
			idle_rearm(server);
		}
		server->wake_at = now;
		return false;
	}
	return now - server->wake_at >= WAKE_BLIND_S;
}

#ifdef DESK_HAVE_POST
/* A program on the bus has asked for the screen to stay lit, or let go
 * (saver.c): an X one, or one in a Flatpak through the portal. */
static void saver_changed(void *data)
{
	struct desk_server *server = data;
	char who[64];
	int n = desk_saver_holds(who, sizeof who);
	if (n) {
		wlr_log(WLR_INFO, "screen: %d hold%s on the bus, the latest for %s", n,
			n == 1 ? "" : "s", who);
	} else {
		wlr_log(WLR_INFO, "screen: no holds on the bus");
	}
	idle_hold_update(server);
}

/* and one saying somebody is there (SimulateUserActivity) */
static void saver_awake(void *data)
{
	input_seen(data, "a program on the bus");
}
#endif

/* wlopm, or swayidle through it: a screen off, or on again. */
static void power_set_mode(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, power_set_mode);
	const struct wlr_output_power_v1_set_mode_event *ev = data;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output != ev->output) {
			continue;
		}
		bool off = ev->mode == ZWLR_OUTPUT_POWER_V1_MODE_OFF;
		wlr_log(WLR_INFO, "screen: %s turned %s by a program", ev->output->name,
			off ? "off" : "on");
		output_set_dark(o, off);
		screen_dark_sync(server);
		if (!off) {
			server->idle_at = real_sec();
			idle_rearm(server);
			server_schedule_frames(server);
		}
		return;
	}
}

/* ---- the screens, as display tools see them ----------------------------
 *
 * wlr-randr, kanshi, wdisplays (wlr-output-management): each screen's
 * modes and the one it is in, where it is, which way up, whether it is on,
 * and its scale -- which on DEsk is the size of a point, the same as
 * shell.conf's scale, and is written there: the desk is laid out in the
 * screen's own pixels, and a program's window is the size it draws itself
 * either way. A screen's own scale stays 1. What is asked is tried on the
 * backend first and then done all at once; the desk is then set out again
 * for the new size or point as for a new screen. Screens are placed with
 * the desk's own at the corner it is drawn from, and one at least stays
 * on: with every screen off there would be nothing to turn one on again
 * from. What a tool has done is kept, and done again the next time the
 * same screens are there (displays.conf, below). */

static struct desk_output *desk_output_of(struct desk_server *server, struct wlr_output *out)
{
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output == out) {
			return o;
		}
	}
	return NULL;
}

static void om_publish(struct desk_server *server)
{
	if (!server->om || server->quitting) {
		return;
	}
	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	if (!config) {
		return;
	}
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		struct wlr_output_configuration_head_v1 *head =
			wlr_output_configuration_head_v1_create(config, o->wlr_output);
		if (!head) {
			continue;
		}
		/* dark is not off: it is still there, in the mode it was in */
		head->state.enabled = !o->off;
		if (o->dark && !output_is_window(o->wlr_output)) {
			head->state.mode = o->dark_mode;
			head->state.custom_mode.width = o->dark_w;
			head->state.custom_mode.height = o->dark_h;
			head->state.custom_mode.refresh = o->dark_refresh;
		}
		head->state.scale = (float)desk_ui_scale();
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, o->wlr_output, &box);
		if (!wlr_box_empty(&box)) {
			head->state.x = box.x;
			head->state.y = box.y;
		}
	}
	wlr_output_manager_v1_set_configuration(server->om, config);
}

/* The head for the screen the desk will be on: the one put at the corner,
 * or else the one furthest left (and then up) of those left on. */
static struct wlr_output_configuration_head_v1 *om_desk_head(struct desk_server *server,
	struct wlr_output_configuration_v1 *config)
{
	struct wlr_output_configuration_head_v1 *head, *best = NULL;
	wl_list_for_each(head, &config->heads, link) {
		const struct wlr_output_head_v1_state *s = &head->state;
		const struct desk_output *o = desk_output_of(server, s->output);
		if (!s->enabled || (o && o->mirror)) {
			continue;   /* (one to show the desk's own, on it, is not the desk) */
		}
		if (s->x == 0 && s->y == 0) {
			return head;
		}
		if (!best || s->x < best->state.x
			|| (s->x == best->state.x && s->y < best->state.y)) {
			best = head;
		}
	}
	return best;
}

static void om_log(const struct wlr_output_configuration_v1 *config, const char *who,
	const char *what, bool ok)
{
	const struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		const struct wlr_output_head_v1_state *s = &head->state;
		if (!s->enabled) {
			wlr_log(WLR_INFO, "%s: %s %s: off: %s", who, what, s->output->name,
				ok ? "done" : "refused");
			continue;
		}
		int w = s->mode ? s->mode->width : s->custom_mode.width;
		int h = s->mode ? s->mode->height : s->custom_mode.height;
		int r = s->mode ? s->mode->refresh : s->custom_mode.refresh;
		wlr_log(WLR_INFO, "%s: %s %s: %dx%d@%.3f Hz at %d,%d, scale %.2f, "
			"transform %d: %s", who, what, s->output->name, w, h, r / 1000.0, s->x, s->y,
			s->scale, (int)s->transform, ok ? "done" : "refused");
	}
}

/* Is mode one of out's own (a mode kept from before may not be, the
 * screen having been plugged in again)? */
static bool om_mode_of(struct wlr_output *out, const struct wlr_output_mode *mode)
{
	struct wlr_output_mode *m;
	wl_list_for_each(m, &out->modes, link) {
		if (m == mode) {
			return true;
		}
	}
	return false;
}

/* Where a head asked to be on would lie, and how big, turned as asked. */
static struct wlr_box om_head_box(const struct wlr_output_head_v1_state *s)
{
	int w = s->mode ? s->mode->width : s->custom_mode.width;
	int h = s->mode ? s->mode->height : s->custom_mode.height;
	if (s->transform % 2 == 1) {
		int k = w;
		w = h;
		h = k;
	}
	return (struct wlr_box){ s->x, s->y, w, h };
}

/* A screen a display tool turns on that was off: the tool was told of no
 * mode and no place for it while it was off (an off head has none), so it
 * may ask for no mode, or for one of its own choosing at 0,0, on top of the
 * desk's own screen -- wdisplays did, at 640x480. Asked with no mode, it
 * goes on in the one it was turned off in, or else its best; asked on top
 * of a screen that is on, it is put beside them, to the right (the desk
 * stays where it is). */
static void om_turned_on(struct desk_server *server, struct wlr_output_configuration_v1 *config,
	const char *who)
{
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		struct wlr_output_head_v1_state *s = &head->state;
		struct wlr_output *out = s->output;
		struct desk_output *o = desk_output_of(server, out);
		/* (one only gone dark keeps its mode and place: om_publish) */
		if (!s->enabled || out->enabled || output_is_window(out) || (o && o->dark && !o->off)) {
			continue;
		}
		if (!s->mode && s->custom_mode.width <= 0) {
			if (o && o->off_mode && om_mode_of(out, o->off_mode)) {
				s->mode = o->off_mode;
			} else if (o && o->off_w > 0 && o->off_h > 0) {
				s->custom_mode.width = o->off_w;
				s->custom_mode.height = o->off_h;
				s->custom_mode.refresh = o->off_refresh;
			} else {
				s->mode = wlr_output_preferred_mode(out);
			}
			wlr_log(WLR_INFO, "%s: %s asked on with no mode: in %dx%d", who, out->name,
				s->mode ? s->mode->width : s->custom_mode.width,
				s->mode ? s->mode->height : s->custom_mode.height);
		}
		struct wlr_box box = om_head_box(s);
		bool over = false;
		int right = 0;
		struct wlr_output_configuration_head_v1 *other;
		wl_list_for_each(other, &config->heads, link) {
			if (other == head || !other->state.enabled) {
				continue;
			}
			struct wlr_box ob = om_head_box(&other->state), cut;
			over = over || wlr_box_intersection(&cut, &box, &ob);
			right = ob.x + ob.width > right ? ob.x + ob.width : right;
		}
		if (over) {
			wlr_log(WLR_INFO, "%s: %s asked on at %d,%d, over another screen: put "
				"beside them at %d,0", who, out->name, s->x, s->y, right);
			s->x = right;
			s->y = 0;
		}
	}
}

/* Before a set of screens is tried, each that is to be on is given a
 * picture of the size it is to be: a real screen (DRM) will not be tried
 * lit with nothing to show, and one turned on again had nothing -- which is
 * why a display tool could turn a screen off and never on again. One with
 * a piece of the scene is drawn as it stands; one without (off till now)
 * is black till its first frame. The pictures come from the swapchains
 * wlr_output_swapchain_manager makes for the set (om_try). */
static bool om_picture(struct desk_server *server, struct wlr_output_swapchain_manager *swm,
	struct wlr_backend_output_state *st)
{
	if (!st->base.enabled) {
		return true;
	}
	struct wlr_swapchain *sc = wlr_output_swapchain_manager_get_swapchain(swm, st->output);
	if (!sc) {
		return false;
	}
	struct wlr_scene_output *so = wlr_scene_get_scene_output(server->scene, st->output);
	if (so) {
		struct wlr_scene_output_state_options opts = { .swapchain = sc };
		if (!wlr_scene_output_build_state(so, &st->base, &opts)) {
			return false;
		}
		if (st->base.committed & WLR_OUTPUT_STATE_BUFFER) {
			return true;
		}
	}
	struct wlr_buffer *buf = desk_swapchain_acquire(sc);
	if (!buf) {
		return false;
	}
	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(server->renderer, buf, NULL);
	bool ok = pass != NULL;
	if (pass) {
		wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
			.box = { 0, 0, buf->width, buf->height },
			.color = { 0.0f, 0.0f, 0.0f, 1.0f },
		});
		ok = wlr_render_pass_submit(pass);
	}
	if (ok) {
		wlr_output_state_set_buffer(&st->base, buf);
	}
	wlr_buffer_unlock(buf);
	return ok;
}

/* Tried, and done if it is to be (apply): what a display tool asks, or
 * what displays.conf has kept (who says which, for the log). */
static bool om_try(struct desk_server *server, struct wlr_output_configuration_v1 *config,
	bool apply, const char *who)
{
	const char *what = apply ? "apply" : "test";
	om_turned_on(server, config, who);
	struct wlr_output_configuration_head_v1 *desk = om_desk_head(server, config);
	if (!desk) {
		wlr_log(WLR_INFO, "%s: %s refused: it would leave no screen on", who, what);
		return false;
	}
	double scale = desk->state.scale;
	if (scale < 0.5 || scale > 4.0) {
		wlr_log(WLR_INFO, "%s: %s refused: the desk is drawn for a point of "
			"0.5 to 4 pixels, not %.2f", who, what, scale);
		return false;
	}
	const char *env = getenv("DESK_SCALE");
	if (env && *env && atof(env) > 0.0 && fabs(scale - desk_ui_scale()) > 1e-3) {
		wlr_log(WLR_INFO, "%s: %s refused: DESK_SCALE holds the size of a point",
			who, what);
		return false;
	}
	size_t n = 0;
	struct wlr_backend_output_state *states =
		wlr_output_configuration_v1_build_state(config, &n);
	if (!states) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		/* the scale is the desk's point, not the screen's */
		states[i].base.committed &= ~(uint32_t)WLR_OUTPUT_STATE_SCALE;
	}
	if (apply && server->dark) {
		/* a screen set up anew is somebody at it */
		screen_wake(server);
	}
	/* the mode each screen to be turned off is in, for when it is on again */
	for (size_t i = 0; i < n; i++) {
		struct wlr_output *out = states[i].output;
		struct desk_output *o = desk_output_of(server, out);
		if (o && !states[i].base.enabled && out->enabled) {
			o->off_mode = out->current_mode;
			o->off_w = out->width;
			o->off_h = out->height;
			o->off_refresh = out->refresh;
		}
	}
	struct wlr_output_swapchain_manager swm;
	wlr_output_swapchain_manager_init(&swm, server->backend);
	const char *why = NULL;
	bool ok = wlr_output_swapchain_manager_prepare(&swm, states, n);
	if (!ok) {
		why = "no pictures could be made for those screens together";
	}
	for (size_t i = 0; ok && i < n; i++) {
		ok = om_picture(server, &swm, &states[i]);
		why = ok ? NULL : "a screen's first picture could not be drawn";
	}
	if (ok && !wlr_backend_test(server->backend, states, n)) {
		ok = false;
		why = "the screens would not take it";
	}
	if (ok && apply) {
		ok = wlr_backend_commit(server->backend, states, n);
		why = ok ? NULL : "the screens would not take it";
		if (ok) {
			wlr_output_swapchain_manager_apply(&swm);
		}
	}
	wlr_output_swapchain_manager_finish(&swm);
	for (size_t i = 0; i < n; i++) {
		wlr_output_state_finish(&states[i].base);
	}
	if (why) {
		wlr_log(WLR_INFO, "%s: %s refused: %s", who, what, why);
	}
	free(states);
	om_log(config, who, what, ok);
	if (!ok || !apply) {
		return ok;
	}

	int dx = desk->state.x, dy = desk->state.y;
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		struct desk_output *o = desk_output_of(server, head->state.output);
		if (!o) {
			continue;
		}
		struct wlr_output *out = o->wlr_output;
		if (!head->state.enabled) {
			o->off = true;
			wlr_output_layout_remove(server->output_layout, out);
			continue;
		}
		o->off = false;
		bool was = wlr_output_layout_get(server->output_layout, out) != NULL;
		struct wlr_output_layout_output *lo = wlr_output_layout_add(server->output_layout,
			out, head->state.x - dx, head->state.y - dy);
		if (!was && lo) {
			/* back on: its piece of the scene, which went with it */
			struct wlr_scene_output *so =
				wlr_scene_get_scene_output(server->scene, out);
			if (!so) {
				so = wlr_scene_output_create(server->scene, out);
			}
			if (so) {
				wlr_scene_output_layout_add_output(server->scene_layout, lo, so);
			}
		}
		wlr_output_schedule_frame(out);
	}

	/* and the desk set out again, for its screen's new size or point --
	 * a new point being Shell Settings' scale from now on */
	if (fabs(scale - desk_ui_scale()) > 1e-3) {
		shell_conf_put_scale(server, scale);
	}
	struct wlr_output *out = first_output(server);
	double point = ui_scale_for(server, out);
	bool rescale = fabs(point - desk_ui_scale()) > 1e-3;
	if (rescale) {
		wlr_log(WLR_INFO, "a point is %.2f pixels now, as a display tool asked", point);
		desk_set_ui_scale(point);
		g_motion_fine = desk_px_per_pt(server->desk_w);
	}
	int w, h;
	wlr_output_effective_resolution(out, &w, &h);
	if (w != server->desk_w || h != server->desk_h) {
		desk_resize_to(server, w, h);
	} else if (rescale) {
		desk_relayout(server);
	}
	return true;
}

static void displays_keep(struct desk_server *server);

/* ---- the screen turned the way the machine is held -------------------------------
 *
 * (2026-11-12, the owner: 後は自動回転ぐらいかな) A tablet's screen -- the built-in one, eDP,
 * LVDS or DSI -- is turned to whichever edge is up (src/orient.c), and the desk set out again for
 * its new shape, as a display tool's turn is; its touch screen and pen are mapped to it, and turn
 * with it. auto_rotate = no in shell.conf leaves it as it is. */
static void touch_screens_map(struct desk_server *server);

static struct wlr_output *builtin_output(struct desk_server *server)
{
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		const char *n = o->wlr_output->name;
		if (!strncmp(n, "eDP", 3) || !strncmp(n, "LVDS", 4) || !strncmp(n, "DSI", 3)) {
			return o->wlr_output;
		}
	}
	/* (the lab's fake: the one screen there is) */
	return getenv("DESK_ORIENT_FAKE") ? first_output(server) : NULL;
}

static void orient_apply(struct desk_server *server)
{
	struct wlr_output *out = builtin_output(server);
	int t = server->orient_want;
	if (!out || t < 0 || !server->conf.auto_rotate || (int)out->transform == t) {
		return;
	}
	struct wlr_output_state st;
	wlr_output_state_init(&st);
	wlr_output_state_set_transform(&st, (enum wl_output_transform)t);
	bool ok = wlr_output_commit_state(out, &st);
	wlr_output_state_finish(&st);
	if (!ok) {
		wlr_log(WLR_INFO, "%s: would not turn", out->name);
		return;
	}
	int w, h;
	wlr_output_effective_resolution(out, &w, &h);
	wlr_log(WLR_INFO, "%s: turned the way it is held (transform %d): %dx%d", out->name, t, w, h);
	if (out == first_output(server) && (w != server->desk_w || h != server->desk_h)) {
		desk_resize_to(server, w, h);
	}
	touch_screens_map(server);
	om_publish(server);
	server_schedule_frames(server);
}

/* auto_rotate switched in Shell Settings: on, as it is held now; off, the right way up again */
static void orient_switched(struct desk_server *server)
{
	int want = server->orient_want;
	if (!server->conf.auto_rotate) {
		server->orient_want = WL_OUTPUT_TRANSFORM_NORMAL;
		server->conf.auto_rotate = true;
		orient_apply(server);
		server->conf.auto_rotate = false;
		server->orient_want = want;
	} else {
		orient_apply(server);
	}
}

static void orient_said(void *data, int transform)
{
	struct desk_server *server = data;
	server->orient_want = transform;
	orient_apply(server);
}

static int orient_fake_cb(void *data)
{
	struct desk_server *server = data;
	const char *o = getenv("DESK_ORIENT_FAKE");
	int t = !strcmp(o, "left-up") ? WL_OUTPUT_TRANSFORM_90 : !strcmp(o, "bottom-up")
		? WL_OUTPUT_TRANSFORM_180 : !strcmp(o, "right-up") ? WL_OUTPUT_TRANSFORM_270
		: WL_OUTPUT_TRANSFORM_NORMAL;
	wlr_log(WLR_INFO, "orientation: %s (DESK_ORIENT_FAKE)", o);
	orient_said(server, t);
	return 0;
}

static void om_apply_cb(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, om_apply);
	struct wlr_output_configuration_v1 *config = data;
	/* (what DISPLAY made of them goes: a tool sets them out as it says) */
	screens_forget(server);
	if (om_try(server, config, true, "display tool")) {
		wlr_output_configuration_v1_send_succeeded(config);
		/* and kept, for the next time these screens are there */
		displays_keep(server);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);
	/* what the screens are now, whichever it was */
	om_publish(server);
}

static void om_test_cb(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, om_test);
	struct wlr_output_configuration_v1 *config = data;
	if (om_try(server, config, false, "display tool")) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);
}

/* ---- DISPLAY: how the screens are used ----------------------------------
 *
 * DISPLAY on the Control Panel's lid, and Super+P (Windows+P elsewhere): each press the next of
 * the four ways Windows' Project has (2026-11-10, the owner) -- the desk going on onto the
 * screens beside it (EXTEND), the desk's own screen on all of them (DUPLICATE), the desk's
 * screen only (THIS SCREEN), and the others only, the desk going over onto one of them (OTHER
 * SCREEN) -- and round again. Done as a display tool's settings are (om_try), and kept for the
 * session only: a screen plugged in or taken out, or a display tool, and the screens are as
 * they are set out (EXTEND). The lamp in the button has the colour of the way they are, and
 * the tube says it in words a moment. A way the screens will not take is passed over. */

static int screens_count(const struct desk_server *server)
{
	int n = 0;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		n++;
	}
	return n;
}

/* the screen that is the desk's when they are extended */
static struct desk_output *screens_home(struct desk_server *server)
{
	struct desk_output *o;
	if (server->screens_home) {
		wl_list_for_each(o, &server->outputs, link) {
			if (o->wlr_output == server->screens_home) {
				return o;
			}
		}
	}
	return desk_output_of(server, first_output(server));
}

/* every screen its own again, at a point to the pixel */
static void screens_unmirror(struct desk_server *server)
{
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->mirror && fabs(o->mirror_scale - 1.0) > 1e-6 && o->wlr_output->enabled) {
			struct wlr_output_state st;
			wlr_output_state_init(&st);
			wlr_output_state_set_scale(&st, 1.0f);
			if (!wlr_output_commit_state(o->wlr_output, &st)) {
				wlr_log(WLR_INFO, "screens: %s would not go back to a scale of 1",
					o->wlr_output->name);
			}
			wlr_output_state_finish(&st);
		}
		o->mirror = false;
		o->mirror_scale = 1.0;
	}
}

static void screens_forget(struct desk_server *server)
{
	if (server->screens_mode != DESK_SCREENS_EXTEND) {
		wlr_log(WLR_INFO, "screens: as they are set out, the desk going on onto them");
	}
	screens_unmirror(server);
	server->screens_mode = DESK_SCREENS_EXTEND;
	server->screens_home = NULL;
}

static const char *const SCREENS_NAME[DESK_SCREENS_MODES] = {
	"extend", "duplicate", "this screen only", "the other only",
};

static bool screens_set(struct desk_server *server, int mode)
{
	struct desk_output *home = screens_home(server);
	if (!home || screens_count(server) < 2) {
		return false;
	}
	struct desk_output *o;
	if (server->screens_mode == DESK_SCREENS_EXTEND) {
		/* leaving the screens as they are set out: where each lies from the desk's, for
		 * coming back to it */
		struct wlr_box hb;
		wlr_output_layout_get_box(server->output_layout, home->wlr_output, &hb);
		wl_list_for_each(o, &server->outputs, link) {
			struct wlr_box b;
			wlr_output_layout_get_box(server->output_layout, o->wlr_output, &b);
			o->ext_known = !wlr_box_empty(&b) && !wlr_box_empty(&hb);
			o->ext_x = b.x - hb.x;
			o->ext_y = b.y - hb.y;
		}
		server->screens_home = home->wlr_output;
	}
	screens_unmirror(server);
	int hw = home->wlr_output->enabled ? home->wlr_output->width : home->off_w;
	int hh = home->wlr_output->enabled ? home->wlr_output->height : home->off_h;
	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	if (!config) {
		return false;
	}
	int right = hw > 0 ? hw : 0;
	wl_list_for_each(o, &server->outputs, link) {
		struct wlr_output_configuration_head_v1 *head =
			wlr_output_configuration_head_v1_create(config, o->wlr_output);
		if (!head) {
			continue;
		}
		struct wlr_output_head_v1_state *st = &head->state;
		bool is_home = o == home;
		int w = o->wlr_output->enabled ? o->wlr_output->width : o->off_w;
		/* beside the desk where it was, or else along to the right of it */
		int bx = o->ext_known ? o->ext_x : right, by = o->ext_known ? o->ext_y : 0;
		if (!is_home && !o->ext_known) {
			right += w > 0 ? w : hw;
		}
		st->scale = (float)desk_ui_scale();
		switch (mode) {
		case DESK_SCREENS_MIRROR:
			st->enabled = true;
			st->x = 0;
			st->y = 0;
			o->mirror = !is_home;
			if (!is_home) {
				/* as big as the desk's, where it has a mode that is */
				struct wlr_output_mode *m, *best = NULL;
				wl_list_for_each(m, &o->wlr_output->modes, link) {
					if (m->width == hw && m->height == hh
						&& (!best || m->refresh > best->refresh)) {
						best = m;
					}
				}
				if (best) {
					st->mode = best;
				}
			}
			break;
		case DESK_SCREENS_THIS:
			st->enabled = is_home;
			st->x = is_home ? 0 : bx;
			st->y = is_home ? 0 : by;
			break;
		case DESK_SCREENS_OTHER:
			st->enabled = !is_home;
			st->x = is_home ? 0 : bx;
			st->y = is_home ? 0 : by;
			break;
		default:
			st->enabled = true;
			st->x = is_home ? 0 : bx;
			st->y = is_home ? 0 : by;
			break;
		}
	}
	bool ok = om_try(server, config, true, "console");
	wlr_output_configuration_v1_destroy(config);
	if (!ok) {
		wl_list_for_each(o, &server->outputs, link) {
			o->mirror = false;
		}
		return false;
	}
	if (mode == DESK_SCREENS_MIRROR) {
		/* each the desk's screen, all of it, fitted into it: where it is not as big, at a
		 * scale that makes it so (and black beside the desk where its shape is not the same) */
		wl_list_for_each(o, &server->outputs, link) {
			struct wlr_output *out = o->wlr_output;
			if (!o->mirror || !out->enabled || hw <= 0 || hh <= 0
				|| (out->width == hw && out->height == hh)) {
				continue;
			}
			double k = fmin((double)out->width / hw, (double)out->height / hh);
			struct wlr_output_state st;
			wlr_output_state_init(&st);
			wlr_output_state_set_scale(&st, (float)k);
			if (wlr_output_commit_state(out, &st)) {
				o->mirror_scale = k;
			} else {
				wlr_log(WLR_INFO, "screens: %s would not take a scale of %.3f", out->name, k);
			}
			wlr_output_state_finish(&st);
		}
	}
	server->screens_mode = mode;
	om_publish(server);
	side_desks_paint(server, true);
	server_schedule_frames(server);
	return true;
}

/* The next way round; one the screens will not take is passed over. What the tube says is
 * what they are now, or that there is only one screen, or that none would do. */
static void screens_next(struct desk_server *server)
{
	server->screens_said_at = monotonic_sec();
	if (screens_count(server) < 2) {
		server->screens_said = DESK_SCREENS_ONE;
		wlr_log(WLR_INFO, "screens: there is only the one");
		return;
	}
	for (int k = 1; k <= DESK_SCREENS_MODES; k++) {
		int mode = (server->screens_mode + k) % DESK_SCREENS_MODES;
		if (screens_set(server, mode)) {
			server->screens_said = mode;
			wlr_log(WLR_INFO, "screens: %s", SCREENS_NAME[mode]);
			return;
		}
		wlr_log(WLR_INFO, "screens: they will not take %s; the next", SCREENS_NAME[mode]);
	}
	server->screens_said = DESK_SCREENS_REFUSED;
}

/* ---- the screens as they were left -----------------------------------
 *
 * $XDG_CONFIG_HOME/desk/displays.conf: how a display tool last set out
 * each set of screens that have been plugged in together -- each screen's
 * mode, where it is, which way up, whether it is on -- written when a tool
 * has done something, and done again whenever the same screens are there:
 * at the start, and when one is plugged in or taken away. A screen is
 * known by its maker, model and serial number, or by the connector it is
 * on where it gives no serial number (or two give the same one). The size
 * of a point is not kept here: it is shell.conf's scale. Only real screens
 * are set out from it -- a window on another desktop is that desktop's to
 * size, and is not kept either -- and not when the command line has said
 * what size the screen is to be (--size). */

#define DISP_SCREENS 8    /* in a set, at most */
#define DISP_SETS 16      /* sets kept, the newest last */

struct disp_screen {
	char id[192];
	struct wlr_output *out;     /* the screen it is now, when it is there */
	bool enabled;
	bool have_mode, custom;
	int w, h, refresh;          /* refresh in mHz; 0 for whatever it does */
	bool have_pos;
	int x, y;                   /* the desk's screen at 0,0 */
	int transform;              /* enum wl_output_transform */
};

struct disp_set {
	int n;
	struct disp_screen s[DISP_SCREENS];
};

/* as wlr-randr has them */
static const char *const DISP_TRANSFORM[] = { "normal", "90", "180", "270", "flipped",
	"flipped-90", "flipped-180", "flipped-270" };

static void displays_path(char *out, size_t n)
{
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg) {
		snprintf(out, n, "%s/desk/displays.conf", cfg);
	} else if (home && *home) {
		snprintf(out, n, "%s/.config/desk/displays.conf", home);
	} else {
		out[0] = '\0';
	}
}

static bool str_same(const char *a, const char *b)
{
	return !strcmp(a ? a : "", b ? b : "");
}

/* What a screen is known by (see above). */
static void disp_name(struct desk_server *server, struct wlr_output *out, char *id, size_t n)
{
	bool own = out->serial && *out->serial;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		const struct wlr_output *other = o->wlr_output;
		if (own && other != out && str_same(other->serial, out->serial)
			&& str_same(other->make, out->make) && str_same(other->model, out->model)) {
			own = false;
		}
	}
	if (own) {
		snprintf(id, n, "%s / %s / %s", out->make ? out->make : "",
			out->model ? out->model : "", out->serial);
	} else {
		snprintf(id, n, "%s", out->name);
	}
	/* a line of the file each */
	for (char *p = id; *p; p++) {
		if ((unsigned char)*p < ' ') {
			*p = ' ';
		}
	}
}

static int disp_by_name(const void *a, const void *b)
{
	return strcmp(((const struct disp_screen *)a)->id, ((const struct disp_screen *)b)->id);
}

/* The screens there are now, as they are set out, in the order of their
 * names. False when they are not to be kept or set out from the file: one
 * of them a window on another desktop, or more of them than a set holds. */
static bool displays_now(struct desk_server *server, struct disp_set *set)
{
	set->n = 0;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		struct wlr_output *out = o->wlr_output;
		if (output_is_window(out) || set->n == DISP_SCREENS) {
			return false;
		}
		struct disp_screen *d = &set->s[set->n++];
		memset(d, 0, sizeof *d);
		disp_name(server, out, d->id, sizeof d->id);
		d->out = out;
		d->enabled = !o->off;
		if (!d->enabled) {
			continue;
		}
		/* dark is not off: it is in the mode it was in */
		const struct wlr_output_mode *mode = o->dark ? o->dark_mode : out->current_mode;
		d->have_mode = true;
		d->custom = mode == NULL;
		d->w = mode ? mode->width : o->dark ? o->dark_w : out->width;
		d->h = mode ? mode->height : o->dark ? o->dark_h : out->height;
		d->refresh = mode ? mode->refresh : o->dark ? o->dark_refresh : out->refresh;
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, out, &box);
		d->have_pos = !wlr_box_empty(&box);
		d->x = box.x;
		d->y = box.y;
		d->transform = (int)out->transform;
	}
	qsort(set->s, (size_t)set->n, sizeof set->s[0], disp_by_name);
	return set->n > 0;
}

static const struct disp_screen *disp_find(const struct disp_set *set, const char *id)
{
	for (int i = 0; i < set->n; i++) {
		if (!strcmp(set->s[i].id, id)) {
			return &set->s[i];
		}
	}
	return NULL;
}

/* The same screens, in whatever order? */
static bool disp_same_set(const struct disp_set *a, const struct disp_set *b)
{
	if (a->n != b->n) {
		return false;
	}
	bool used[DISP_SCREENS] = { false };
	for (int i = 0; i < a->n; i++) {
		int k = 0;
		while (k < b->n && (used[k] || strcmp(a->s[i].id, b->s[k].id))) {
			k++;
		}
		if (k == b->n) {
			return false;
		}
		used[k] = true;
	}
	return true;
}

/* Every set the file keeps, up to max, in its order; how many. */
static int displays_read(struct disp_set *sets, int max)
{
	char path[512];
	displays_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return 0;
	}
	int n = 0;
	struct disp_set *set = NULL;
	struct disp_screen *d = NULL;
	char line[512];
	while (fgets(line, sizeof line, f)) {
		char *s = conf_trim(line);
		char *eq = *s == '#' ? NULL : strchr(s, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		char *key = conf_trim(s), *val = conf_trim(eq + 1);
		if (!strcmp(key, "set")) {
			if (n == max) {
				break;
			}
			set = &sets[n++];
			set->n = 0;
			d = NULL;
		} else if (!set) {
			continue;
		} else if (!strcmp(key, "screen")) {
			d = set->n < DISP_SCREENS ? &set->s[set->n++] : NULL;
			if (d) {
				memset(d, 0, sizeof *d);
				d->enabled = true;
				snprintf(d->id, sizeof d->id, "%s", val);
			}
		} else if (!d) {
			continue;
		} else if (!strcmp(key, "enabled")) {
			conf_bool(val, &d->enabled);
		} else if (!strcmp(key, "mode")) {
			double hz = 0.0;
			int got = sscanf(val, "%dx%d@%lf", &d->w, &d->h, &hz);
			d->have_mode = got >= 2 && d->w > 0 && d->h > 0;
			d->refresh = got == 3 && hz > 0.0 ? (int)lround(hz * 1000.0) : 0;
			d->custom = strstr(val, "custom") != NULL;
		} else if (!strcmp(key, "position")) {
			d->have_pos = sscanf(val, "%d,%d", &d->x, &d->y) == 2;
		} else if (!strcmp(key, "transform")) {
			for (int i = 0; i < 8; i++) {
				if (!strcasecmp(val, DISP_TRANSFORM[i])) {
					d->transform = i;
				}
			}
		}
	}
	fclose(f);
	return n;
}

static void disp_write_set(FILE *f, const struct disp_set *set)
{
	fputs("\nset =", f);
	for (int i = 0; i < set->n; i++) {
		fprintf(f, "%s %s", i ? " +" : "", set->s[i].id);
	}
	fputc('\n', f);
	for (int i = 0; i < set->n; i++) {
		const struct disp_screen *d = &set->s[i];
		fprintf(f, "screen = %s\nenabled = %s\n", d->id, d->enabled ? "yes" : "no");
		if (d->have_mode) {
			fprintf(f, "mode = %dx%d@%.3f%s\n", d->w, d->h, d->refresh / 1000.0,
				d->custom ? " custom" : "");
		}
		if (d->have_pos) {
			fprintf(f, "position = %d,%d\n", d->x, d->y);
		}
		if (d->enabled) {
			fprintf(f, "transform = %s\n", DISP_TRANSFORM[d->transform & 7]);
		}
	}
}

/* After a display tool has done something: how the screens are set out
 * now, kept for this set of them in place of what was kept for it. */
static void displays_keep(struct desk_server *server)
{
	struct disp_set now;
	char path[512];
	displays_path(path, sizeof path);
	if (!path[0] || !displays_now(server, &now)) {
		return;
	}
	struct disp_set *sets = calloc(DISP_SETS, sizeof *sets);
	if (!sets) {
		return;
	}
	int n = displays_read(sets, DISP_SETS);
	conf_dirs_for(path);
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		wlr_log(WLR_ERROR, "cannot keep how the screens are set out in %s", tmp);
		free(sets);
		return;
	}
	fputs("# DEsk's screens, as a display tool last set them out (wdisplays, Xfce's\n"
		"# Display settings, wlr-randr), for each set of screens plugged in together.\n"
		"# DEsk writes this when a tool has changed something, and sets the screens\n"
		"# out by it again whenever the same ones are there. The size of a point,\n"
		"# which the tools call the scale, is shell.conf's scale.\n"
		"#\n"
		"#   set = <screen> + <screen> ...     a set, and then each screen in it:\n"
		"#   screen = <make> / <model> / <serial>, or the connector it is on (DP-1)\n"
		"#   enabled = yes | no\n"
		"#   mode = <width>x<height>@<Hz> [custom]\n"
		"#   position = <x>,<y>                in pixels, the desk's screen at 0,0\n"
		"#   transform = normal | 90 | 180 | 270 | flipped | flipped-90 | flipped-180\n"
		"#               | flipped-270\n", f);
	/* the other sets as they were, the oldest let go once there are too
	 * many, and this one last */
	int others = 0;
	for (int i = 0; i < n; i++) {
		others += !disp_same_set(&sets[i], &now);
	}
	int drop = others - (DISP_SETS - 1);
	for (int i = 0; i < n; i++) {
		if (disp_same_set(&sets[i], &now)) {
			continue;
		}
		if (drop > 0) {
			drop--;
			continue;
		}
		disp_write_set(f, &sets[i]);
	}
	disp_write_set(f, &now);
	free(sets);
	bool ok = fflush(f) == 0 && !ferror(f);
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		wlr_log(WLR_ERROR, "cannot keep how the screens are set out in %s", path);
		unlink(tmp);
		return;
	}
	wlr_log(WLR_INFO, "displays.conf: kept for this set of %d screen%s", now.n,
		now.n == 1 ? "" : "s");
}

/* The screen's own mode for what was kept: the same size, and the refresh
 * nearest to it. None if it has none that size. */
static struct wlr_output_mode *disp_mode(struct wlr_output *out, const struct disp_screen *d)
{
	struct wlr_output_mode *m, *best = NULL;
	wl_list_for_each(m, &out->modes, link) {
		if (m->width == d->w && m->height == d->h && (!best
				|| abs(m->refresh - d->refresh) < abs(best->refresh - d->refresh))) {
			best = m;
		}
	}
	return best;
}

/* The screens set out as displays.conf keeps them for this set of them,
 * if it keeps them and they are not so already. */
static void displays_put_back(struct desk_server *server)
{
	struct disp_set now;
	if (server->dark) {
		/* a set out screen is somebody at it (om_try), which a screen
		 * plugged in or come back while the desk is dark is not: later */
		server->disp_after_dark = true;
		return;
	}
	if (server->quitting || server->size_set || !displays_now(server, &now)) {
		return;
	}
	struct disp_set *sets = calloc(DISP_SETS, sizeof *sets);
	if (!sets) {
		return;
	}
	int n = displays_read(sets, DISP_SETS);
	const struct disp_set *kept = NULL;
	for (int i = n - 1; i >= 0 && !kept; i--) {
		if (disp_same_set(&sets[i], &now)) {
			kept = &sets[i];
		}
	}
	struct wlr_output_configuration_v1 *config = kept ? wlr_output_configuration_v1_create()
		: NULL;
	bool differ = false;
	for (int i = 0; config && i < now.n; i++) {
		const struct disp_screen *is = &now.s[i];
		const struct disp_screen *was = disp_find(kept, is->id);
		struct wlr_output_configuration_head_v1 *head =
			wlr_output_configuration_head_v1_create(config, is->out);
		if (!was || !head) {
			wlr_output_configuration_v1_destroy(config);
			config = NULL;
			break;
		}
		struct wlr_output_head_v1_state *st = &head->state;
		st->enabled = was->enabled;
		st->scale = (float)desk_ui_scale();
		differ |= was->enabled != is->enabled;
		if (!was->enabled) {
			continue;
		}
		if (was->have_mode) {
			struct wlr_output_mode *mode = was->custom ? NULL : disp_mode(is->out, was);
			int w = mode ? mode->width : was->w, h = mode ? mode->height : was->h;
			int r = mode ? mode->refresh : was->refresh;
			st->mode = mode;
			st->custom_mode.width = w;
			st->custom_mode.height = h;
			st->custom_mode.refresh = r;
			differ |= !is->have_mode || is->w != w || is->h != h
				|| (r != 0 && is->refresh != r) || is->custom != (mode == NULL);
		}
		st->x = was->have_pos ? was->x : is->x;
		st->y = was->have_pos ? was->y : is->y;
		differ |= st->x != is->x || st->y != is->y || !is->have_pos;
		st->transform = (enum wl_output_transform)was->transform;
		differ |= was->transform != is->transform;
	}
	free(sets);
	if (!config) {
		return;
	}
	if (differ) {
		wlr_log(WLR_INFO, "displays.conf: this set of %d screen%s set out as kept",
			now.n, now.n == 1 ? "" : "s");
		om_try(server, config, true, "displays.conf");
		om_publish(server);
	}
	wlr_output_configuration_v1_destroy(config);
}

static void displays_idle_cb(void *data)
{
	struct desk_server *server = data;
	server->disp_idle = NULL;
	displays_put_back(server);
}

/* The screens have changed -- the first of them have come, one has been
 * plugged in or taken away: once those coming at the same time have all
 * come, they are set out as they were kept. */
static void displays_changed(struct desk_server *server)
{
	if (server->disp_idle || server->quitting) {
		return;
	}
	server->disp_idle = wl_event_loop_add_idle(wl_display_get_event_loop(server->wl_display),
		displays_idle_cb, server);
}

/* ---- a day kept: the shows ----
 *
 * On a birthday, at Christmas and over the New Year, coming in at the
 * front door -- and starting the desk up -- the lamp is brought down a
 * little and the clock says so, and does a little more: plays the birthday
 * tune on its buzzer in time with a metronome, Jingle Bells on bells, and
 * at the New Year a few fireworks far off. At the stroke of the New Year,
 * wherever you are, fireworks: their whistles and bangs, and their light
 * coming in. About twenty seconds; Esc stops it. The tunes are old enough
 * to be anybody's. */
struct show_note {
	double at;         /* seconds into it */
	int snd;
	double semis;      /* above the sound's own note */
	double gain, pan;
};
struct show_burst {
	double at;         /* when it bursts */
	double x;          /* where across the sky, 0..1 */
	double r, g, b;
	double power;
};
#define SHOW_NOTES 192
#define SHOW_BURSTS 24
#define SHOW_S 20.0          /* how long */
#define SHOW_DIM 0.55        /* the lamp, while it is on */
static struct show_note g_show_notes[SHOW_NOTES];
static int g_show_n;
static struct show_burst g_show_bursts[SHOW_BURSTS];
static int g_show_nb;
static uint64_t g_show_dice = 0x9E3779B97F4A7C15ull;

static double show_dice(void)
{
	g_show_dice ^= g_show_dice << 13;
	g_show_dice ^= g_show_dice >> 7;
	g_show_dice ^= g_show_dice << 17;
	return (double)(g_show_dice >> 11) * (1.0 / 9007199254740992.0);
}

static void show_note(double at, int snd, double semis, double gain, double pan)
{
	if (g_show_n < SHOW_NOTES) {
		g_show_notes[g_show_n++] = (struct show_note){ at, snd, semis, gain, pan };
	}
}

/* Happy Birthday to You, on the clock's buzzer, and the metronome under it:
 * a bar of it alone to count in, the first of each bar the louder */
static void score_birthday(void)
{
	const double beat = DESK_SHOW_BEAT;
	static const struct { double at, semis; } M[] = {
		{ 2.0, -5 }, { 2.75, -5 }, { 3, -3 }, { 4, -5 }, { 5, 0 }, { 6, -1 },
		{ 8.0, -5 }, { 8.75, -5 }, { 9, -3 }, { 10, -5 }, { 11, 2 }, { 12, 0 },
		{ 14.0, -5 }, { 14.75, -5 }, { 15, 7 }, { 16, 4 }, { 17, 0 }, { 18, -1 }, { 19, -3 },
		{ 20.0, 5 }, { 20.75, 5 }, { 21, 4 }, { 22, 0 }, { 23, 2 }, { 24, 0 },
	};
	for (int i = 0; i <= 26; i++) {
		bool down = i % 3 == 0;
		show_note(i * beat, down ? DESK_SND_TOCK : DESK_SND_TICK, 0, down ? 0.55 : 0.40, 0.3);
	}
	for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) {
		show_note(M[i].at * beat, DESK_SND_BEEP, M[i].semis, 0.85, -0.1);
	}
	static const double F[4] = { 0, 4, 7, 12 };
	for (int i = 0; i < 4; i++) {
		show_note(27.0 * beat + i * 0.10, DESK_SND_BEEP, F[i], 0.65, -0.1);
	}
}

/* Jingle Bells, the chorus with its last line, on bells, the sleigh bells
 * shaken on the beat */
static void score_xmas(void)
{
	const double beat = 0.44;
	static const struct { double at, semis; } M[] = {
		{ 0, 4 }, { 1, 4 }, { 2, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 },
		{ 8, 4 }, { 9, 7 }, { 10, 0 }, { 11.5, 2 }, { 12, 4 },
		{ 16, 5 }, { 17, 5 }, { 18, 5 }, { 19.5, 5 }, { 20, 5 }, { 21, 4 }, { 22, 4 },
		{ 23, 4 }, { 23.5, 4 }, { 24, 7 }, { 25, 7 }, { 26, 5 }, { 27, 2 }, { 28, 0 },
	};
	for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) {
		show_note(M[i].at * beat, DESK_SND_BELL, M[i].semis - 12, 0.75, -0.05);
	}
	/* the last chord, and the bells shaken through it all */
	static const double C[3] = { 4, 7, 12 };
	for (int i = 0; i < 3; i++) {
		show_note(28.0 * beat + 0.02 * i, DESK_SND_BELL, C[i] - 12, 0.45, 0.1 * i);
	}
	for (int i = 0; i < 31; i++) {
		show_note(i * beat + 0.01, DESK_SND_JINGLE, 0, i % 2 ? 0.30 : 0.45, 0.35);
	}
}

static void show_burst(double at, double x, double power)
{
	static const double PAL[6][3] = { { 1.0, 0.72, 0.30 }, { 1.0, 0.32, 0.26 },
		{ 0.38, 1.0, 0.48 }, { 0.42, 0.58, 1.0 }, { 0.92, 0.46, 1.0 }, { 1.0, 0.98, 0.92 } };
	if (g_show_nb >= SHOW_BURSTS) {
		return;
	}
	const double *c = PAL[(int)(show_dice() * 6.0) % 6];
	g_show_bursts[g_show_nb++] = (struct show_burst){ at, x, c[0], c[1], c[2], power };
}

/* a firework: up with a whistle, the bang, its stars crackling down */
static void firework(double at, double power, bool whistle)
{
	double pan = show_dice() * 1.6 - 0.8;
	double x = 0.5 + pan * 0.55;
	if (whistle) {
		show_note(at - 1.15, DESK_SND_FW_WHISTLE, show_dice() * 2.0 - 1.0, 0.45 * power, pan);
	}
	show_note(at, DESK_SND_FW_BOOM, show_dice() * 2.0 - 1.5, 0.95 * power, pan);
	if (show_dice() < 0.75) {
		show_note(at + 0.22, DESK_SND_FW_CRACKLE, show_dice() * 2.0, 0.40 * power, pan);
	}
	show_burst(at, x, power);
}

/* a party: the poppers going off in the corners */
static void party(void)
{
	show_note(0.10, DESK_SND_POPPER, 0.0, 0.75, -0.8);
	show_note(0.32, DESK_SND_POPPER, 0.6, 0.75, 0.8);
}

/* the New Year, coming in: a party, bells up an arpeggio, and fireworks
 * far off */
static void score_newyear(void)
{
	party();
	static const double A[4] = { 0, 4, 7, 12 };
	for (int i = 0; i < 4; i++) {
		show_note(2.3 + i * 0.22, DESK_SND_BELL, A[i] - 12, 0.6, 0.0);
	}
	static const double T[5] = { 4.2, 7.2, 9.9, 12.9, 15.8 };
	for (int i = 0; i < 5; i++) {
		firework(T[i], 0.45, false);
	}
}

/* the stroke of the New Year: a party, and the sky full of fireworks */
static void score_midnight(void)
{
	party();
	static const double T[] = { 2.4, 3.6, 4.3, 6.1, 7.6, 8.3, 10.1, 11.7, 12.5, 14.4, 15.1,
		15.6, 16.8 };
	for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
		firework(T[i], i % 4 == 3 ? 0.6 : 1.0, true);
	}
}

static void fw_paint(struct desk_server *server, double t);
static void confetti_paint(struct desk_server *server, double t);

static void show_stop(struct desk_server *server, bool skipped)
{
	if (!server->show) {
		return;
	}
	for (int i = 0; i < server->show_nh; i++) {
		desk_sound_cancel(server->show_handles[i]);
		desk_sound_stop(server->show_handles[i], skipped ? 0.25 : 0.8);
	}
	server->show_nh = 0;
	server->show = DESK_SHOW_NONE;
	if (!server->boot_on) {
		show_paint_night(server, 1.0);
	}
	if (server->fw_node) {
		wlr_scene_node_set_enabled(&server->fw_node->node, false);
	}
	if (server->confetti_node) {
		wlr_scene_node_set_enabled(&server->confetti_node->node, false);
	}
	clock_publish(server);
	server_schedule_frames(server);
	wlr_log(WLR_INFO, "show: %s", skipped ? "stopped" : "over");
}

static void show_start(struct desk_server *server, int kind)
{
	if (server->show) {
		show_stop(server, true);
	}
	g_show_n = g_show_nb = 0;
	g_show_dice ^= (uint64_t)time(NULL) * 0x2545F4914F6CDD1Dull;
	switch (kind) {
	case DESK_SHOW_BIRTHDAY:
		score_birthday();
		break;
	case DESK_SHOW_XMAS:
		score_xmas();
		break;
	case DESK_SHOW_NEWYEAR:
		score_newyear();
		break;
	default:
		score_midnight();
		break;
	}
	/* in the order they are to be asked for */
	for (int i = 1; i < g_show_n; i++) {
		struct show_note n = g_show_notes[i];
		int j = i - 1;
		while (j >= 0 && g_show_notes[j].at > n.at) {
			g_show_notes[j + 1] = g_show_notes[j];
			j--;
		}
		g_show_notes[j + 1] = n;
	}
	server->show = kind;
	/* real time: a tune keeps its own tempo, however fast things move */
	server->show_t0 = real_sec();
	server->show_next = 0;
	server->show_nh = 0;
	clock_publish(server);
	server_schedule_frames(server);
	static const char *const NAME[] = { "", "birthday", "christmas", "new year", "midnight" };
	wlr_log(WLR_INFO, "show: %s", NAME[kind]);
}

/* whatever today is being kept as, if it is */
static void show_for_today(struct desk_server *server)
{
	unsigned o = desk_occasion_today();
	int kind = o & DESK_OCC_BIRTHDAY ? DESK_SHOW_BIRTHDAY
		: o & DESK_OCC_NEWYEAR ? DESK_SHOW_NEWYEAR
		: o & DESK_OCC_XMAS ? DESK_SHOW_XMAS : DESK_SHOW_NONE;
	if (kind) {
		show_start(server, kind);
	}
}

/* Once a frame while one is on: the notes coming due asked for (a little
 * ahead, so they are heard on time), the lamp, the fireworks' light. */
static void show_tick(struct desk_server *server)
{
	if (!server->show) {
		return;
	}
	double t = real_sec() - server->show_t0;
	while (server->show_next < g_show_n && g_show_notes[server->show_next].at <= t + 0.45) {
		const struct show_note *n = &g_show_notes[server->show_next++];
		int hd = desk_sound_play_exact(n->snd, n->pan, n->gain, pow(2.0, n->semis / 12.0),
			fmax(n->at - t, 0.0));
		if (!hd) {
			continue;
		}
		if (server->show_nh == SHOW_HANDLES) {
			memmove(server->show_handles, server->show_handles + 1,
				(SHOW_HANDLES - 1) * sizeof server->show_handles[0]);
			server->show_nh--;
		}
		server->show_handles[server->show_nh++] = hd;
	}
	/* in 2D the clock and the tune only: no lamp going down over the
	 * desk, no light of the fireworks, no confetti, nothing laid over it */
	if (!locked(server) && !server->boot_on && !gfx_flat()) {
		double down = clampf_d(t / 1.4, 0.0, 1.0), up = clampf_d((SHOW_S - t) / 1.6, 0.0, 1.0);
		show_paint_night(server, 1.0 - (1.0 - SHOW_DIM) * lock_ease(fmin(down, up)));
	}
	if (g_show_nb > 0 && !gfx_flat()) {
		fw_paint(server, t);
	}
	if ((server->show == DESK_SHOW_NEWYEAR || server->show == DESK_SHOW_MIDNIGHT)
		&& !gfx_flat()) {
		confetti_paint(server, t);
	}
	if (t >= SHOW_S) {
		show_stop(server, false);
		return;
	}
	server_schedule_frames(server);
}

/* The light of the fireworks, coming in over everything -- the door too:
 * added to what is under it, not laid over it. A sheet an eighth of the
 * screen's size, stretched, the light in its colour and hardly any alpha:
 * put over the rest, it (nearly) adds. Not none at all -- pixman passes
 * over a pixel with no alpha in it without a look at its colour. */
static void fw_paint(struct desk_server *server, double t)
{
	int w = server->desk_w, h = server->desk_h;
	double I[SHOW_BURSTS], any = 0.0;
	for (int b = 0; b < g_show_nb; b++) {
		double dt = t - g_show_bursts[b].at;
		I[b] = 0.0;
		if (dt < 0.0 || dt > 3.2) {
			continue;
		}
		double rise = dt < 0.04 ? dt / 0.04 : 1.0;
		double flick = 0.82 + 0.18 * sin(dt * 47.0 + b * 1.3) * sin(dt * 31.0 + b);
		I[b] = g_show_bursts[b].power * rise * (exp(-dt * 3.2) + 0.28 * exp(-dt * 1.0)) * flick;
		any += I[b];
	}
	if (any < 0.004 || w < 8 || h < 8) {
		if (server->fw_node) {
			wlr_scene_node_set_enabled(&server->fw_node->node, false);
		}
		return;
	}
	int gw = w / 8 + 1, gh = h / 8 + 1;
	struct desk_buf *buf = buf_create(gw, gh);
	cairo_surface_flush(buf->surface);
	unsigned char *px = cairo_image_surface_get_data(buf->surface);
	int stride = cairo_image_surface_get_stride(buf->surface);
	double R2 = 0.85 * w * 0.85 * w;
	for (int y = 0; y < gh; y++) {
		uint32_t *row = (uint32_t *)(void *)(px + (size_t)y * stride);
		double sy = (y + 0.5) * 8.0 + 0.30 * h;
		for (int x = 0; x < gw; x++) {
			double sx = (x + 0.5) * 8.0, r = 0.0, g = 0.0, bl = 0.0;
			for (int b = 0; b < g_show_nb; b++) {
				if (I[b] <= 0.0) {
					continue;
				}
				double dx = sx - g_show_bursts[b].x * w;
				double f = I[b] / (1.0 + (dx * dx + sy * sy) / R2 * 1.4);
				r += g_show_bursts[b].r * f;
				g += g_show_bursts[b].g * f;
				bl += g_show_bursts[b].b * f;
			}
			/* a little: light in through a window, not a floodlight */
			int R = (int)fmin(255.0, r * 0.46 * 255.0), G = (int)fmin(255.0, g * 0.46 * 255.0);
			int B = (int)fmin(255.0, bl * 0.46 * 255.0);
			int m = R > G ? (R > B ? R : B) : (G > B ? G : B);
			uint32_t A = m > 0 ? (uint32_t)(1 + m / 6) : 0u;
			row[x] = (A << 24) | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
		}
	}
	cairo_surface_mark_dirty(buf->surface);
	scene_put(&server->fw_node, server->layer_glow, buf, 0, 0, NULL);
	if (server->fw_node) {
		wlr_scene_buffer_set_dest_size(server->fw_node, w, h);
		server->fw_node->point_accepts_input = shadow_accepts;
		wlr_scene_node_set_enabled(&server->fw_node->node, true);
		wlr_scene_node_set_enabled(&server->layer_glow->node, true);
	}
}

/* the poppers' confetti, over everything while it is coming down */
static void confetti_paint(struct desk_server *server, double t)
{
	int w = server->desk_w, h = server->desk_h;
	if (t > 7.6 || w < 8 || h < 8) {
		if (server->confetti_node) {
			wlr_scene_node_set_enabled(&server->confetti_node->node, false);
		}
		return;
	}
	int ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	struct desk_buf *buf = buf_create(ow, oh);
	cairo_save(buf->cr);
	cairo_scale(buf->cr, (double)ow / w, (double)oh / h);
	bool any = desk_draw_poppers(buf->cr, w, h, t);
	cairo_restore(buf->cr);
	scene_put(&server->confetti_node, server->layer_glow, buf, 0, 0, NULL);
	if (server->confetti_node) {
		wlr_scene_buffer_set_dest_size(server->confetti_node, w, h);
		server->confetti_node->point_accepts_input = shadow_accepts;
		wlr_scene_node_raise_to_top(&server->confetti_node->node);
		wlr_scene_node_set_enabled(&server->confetti_node->node, any);
		wlr_scene_node_set_enabled(&server->layer_glow->node, true);
	}
}

/* the stroke of the New Year: looked for once a second */
static void show_midnight_check(struct desk_server *server)
{
	time_t now = desk_occasion_now();
	struct tm tm;
	localtime_r(&now, &tm);
	int ymd = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
	int was = server->occ_ymd;
	server->occ_ymd = ymd;
	if (was && was % 10000 == 1231 && tm.tm_mon == 0 && tm.tm_mday == 1
		&& (desk_occasion_today() & DESK_OCC_NEWYEAR)) {
		show_start(server, DESK_SHOW_MIDNIGHT);
	}
	if (was && was != ymd && locked(server)) {
		/* a new day at the door: dressed for it, or not any more */
		paint_lock(server);
		server_schedule_frames(server);
	}
}

/* ---- Print: an area of the screen for a picture -----------------------
 *
 * Print is a camera put up to the eye. The screens are held as they were
 * (snip_freeze); the camera comes up from under the screen's foot, and
 * nearer the eye the screen round it goes dark, as the camera's body comes
 * up round the eye, till what is left is the viewfinder: each screen's
 * picture a little in from its edges, thick glass at its edge, the
 * eyecup's dark round it and the viewfinder's display in the dark under it
 * (desk_draw_finder_line). The hand draws an area out over the picture --
 * pressed at one corner, let go at the other, the dark round the picture
 * taking it to the picture's edge -- or, the dial on the camera in the hand
 * at the screen's foot set so, points at a window or a screen; what is to
 * be taken is lit, and the rest falls into shade. Let go, the shutter goes
 * off: its sound, and the flash over what is taken; then the camera comes
 * down and the picture -- cut from the screens as they
 * were -- is handed to desk-screenshot to file. Esc or the right button,
 * and nothing is. A game holding the pointer lets go of it meanwhile, and
 * is told nothing of where the hand goes. Ctrl+Print is the whole screen,
 * Alt+Print the sheet in front, as before. (After dentarthurdent's
 * region, in the AiO v0.7.) */

#define SNIP_UP_S 0.60      /* the camera coming up to the eye: rising, then brought close */
#define SNIP_RISE 0.45      /* (the part of that it rises in) */
#define SNIP_DOWN_S 0.40    /* and taken down again */
#define SNIP_HERO 0.62      /* the camera coming up, as wide as this much of the screen */
#define SNIP_HAND_PT 480.0  /* the camera in the hand, as wide */
#define SNIP_HAND_SHOW 186.0  /* and how much of it is over the screen's foot, in its units */
#define SNIP_TURN_S 0.18    /* a dial turning a step, a slide sliding, a lever going over */
#define SNIP_SHUT_MS 90     /* the mirror up: the shutter's moment, before the camera goes down */
#define SNIP_FREEZE_MS 250  /* the longest a screen's picture is waited for */
#define SNIP_RIM_PT 36.0    /* the glass at the viewfinder's edge: how wide */
#define SNIP_ROUND_PT 52.0  /* and its corners, how round */
#define SNIP_SHADE 0.50     /* the shade round the picture, at its deepest */
#define SNIP_PEN_PT 20.0    /* the light falling off round it, how far */
#define SNIP_PEN_N 64       /* that drawn this deep, and stretched */
#define SNIP_SHADE_S 0.22   /* the light gathering on the picture, or spreading again */
#define SNIP_GLASS_PT 8.0   /* the glass round the picture, how wide */
#define SNIP_BEVEL_N 32     /* its edge drawn this deep, and stretched */
#define SNIP_VIEW_TOP 24.0  /* the viewfinder: the dark over the picture, in points */
#define SNIP_VIEW_FOOT 76.0 /* and under it, where its display is */
#define SNIP_TAPE_S 0.62    /* a tape brought up and pushed into its slot, or out and away */
#define SNIP_FLASH_S 0.45   /* the flash going off and dying away */
#define REC_FPS 30.0        /* the film's frames: read no more often than this a second */
#define REC_READY_S 0.25    /* desk-record started at least this long before the film begins */

static void desk_constraint_refresh(struct desk_server *server);
static struct wlr_box snip_hand_box(struct desk_server *server, double m);
static void snip_count_stop(struct desk_server *server);
static void snip_shut_after(struct desk_server *server, double ms);
static void rec_timer_next(struct desk_server *server);
