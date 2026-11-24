/* Part of wm.c: views: text, pasting and what a window asks for.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static void server_pen_proximity(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, pen_proximity);
	struct wlr_tablet_tool_proximity_event *event = data;
	struct desk_pen *pen = pen_of(server, event->tablet, event->tool);
	if (!pen) {
		return;
	}
	if (event->state == WLR_TABLET_TOOL_PROXIMITY_IN) {
		if (!pen->near) {
			pen->near = true;
			server->pens_near++;
		}
		/* the hand comes down with it */
		for (int i = 0; i < DESK_FINGERS; i++) {
			if (server->fingers[i].on && server->fingers[i].goes != FINGER_NOWHERE) {
				finger_taken_away(server, &server->fingers[i], event->time_msec);
			}
		}
		if (server->dark) {
			return;
		}
		input_seen(server, "a pen");
		server->pen_lately = true;
		pen_to(server, pen, event->tablet, event->x, event->y, 0.0, 0.0);
		pen_moved(server, pen, event->time_msec);
		return;
	}
	if (pen->near) {
		pen->near = false;
		server->pens_near--;
	}
	server->pen_left_at = real_sec();
	if (pen->on_program) {
		pen_tip_up(server, pen, event->time_msec);
	}
	if (pen->held_button) {
		hand_button(server, pen->held_button, false, event->time_msec);
		pen->held_button = 0;
	}
	if (pen->v2->focused_surface) {
		wlr_tablet_v2_tablet_tool_notify_proximity_out(pen->v2);
	}
	/* nothing is under a pen that is not there */
	if (!server->hand_on && !server->buttons_down) {
		server->hand_unseen = true;
		wlr_cursor_unset_image(server->cursor);
		hand_gone(server);
	}
}

static void server_pen_axis(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, pen_axis);
	struct wlr_tablet_tool_axis_event *event = data;
	struct desk_pen *pen = pen_of(server, event->tablet, event->tool);
	if (!pen || server->dark) {
		return;
	}
	input_seen(server, "a pen");
	server->pen_lately = true;
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE) {
		server->pen_pressure = event->pressure;
	}
	if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_X | WLR_TABLET_TOOL_AXIS_Y)) {
		pen_to(server, pen, event->tablet, event->updated_axes & WLR_TABLET_TOOL_AXIS_X ? event->x : NAN,
			event->updated_axes & WLR_TABLET_TOOL_AXIS_Y ? event->y : NAN, event->dx, event->dy);
		pen_moved(server, pen, event->time_msec);
	}
	if (!pen->v2->focused_surface) {
		return;
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE) {
		wlr_tablet_v2_tablet_tool_notify_pressure(pen->v2, event->pressure);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE) {
		wlr_tablet_v2_tablet_tool_notify_distance(pen->v2, event->distance);
	}
	if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y)) {
		wlr_tablet_v2_tablet_tool_notify_tilt(pen->v2, event->tilt_x, event->tilt_y);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION) {
		wlr_tablet_v2_tablet_tool_notify_rotation(pen->v2, event->rotation);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER) {
		wlr_tablet_v2_tablet_tool_notify_slider(pen->v2, event->slider);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL) {
		wlr_tablet_v2_tablet_tool_notify_wheel(pen->v2, event->wheel_delta, (int32_t)lround(event->wheel_delta / 15.0));
	}
}

static void server_pen_tip(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, pen_tip);
	struct wlr_tablet_tool_tip_event *event = data;
	struct desk_pen *pen = pen_of(server, event->tablet, event->tool);
	if (!pen) {
		return;
	}
	server->pen_lately = true;
	if (event->state == WLR_TABLET_TOOL_TIP_DOWN) {
		if (!server->dark) {
			pen_to(server, pen, event->tablet, event->x, event->y, 0.0, 0.0);
			pen_moved(server, pen, event->time_msec);
		}
		server->pen_now = pen;
		hand_button(server, BTN_LEFT, true, event->time_msec);
		server->pen_now = NULL;
	} else {
		pen_tip_up(server, pen, event->time_msec);
		if (!server->dark) {
			pen_moved(server, pen, event->time_msec);
		}
	}
}

static void server_pen_button(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, pen_button);
	struct wlr_tablet_tool_button_event *event = data;
	struct desk_pen *pen = pen_of(server, event->tablet, event->tool);
	if (!pen) {
		return;
	}
	bool down = event->state == WLR_BUTTON_PRESSED;
	if (!pen->held_button && pen->v2->focused_surface) {
		wlr_tablet_v2_tablet_tool_notify_button(pen->v2, event->button,
			down ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED : ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
		return;
	}
	if (down) {
		pen->held_button = event->button == BTN_STYLUS2 ? BTN_MIDDLE : BTN_RIGHT;
		hand_button(server, pen->held_button, true, event->time_msec);
	} else if (pen->held_button) {
		hand_button(server, pen->held_button, false, event->time_msec);
		pen->held_button = 0;
	}
}

/* ---- room for the keyboard on the screen ---------------------------------
 *
 * What is being typed in is kept above it: the window with the keys is lifted up the desk, as far
 * as it takes to bring where the program says the typing is (or else its foot) over the keyboard,
 * and goes back down when it is no longer in the way. */

static void room_update(struct desk_server *server)
{
	struct desk_toplevel *focus = view_of_surface(server, server->seat->keyboard_state.focused_surface);
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		double to = 0.0;
		if (view == focus && server->room_top >= 0 && view->scene_tree && !view->fullscreen
			&& view->sheet.phase == SHEET_IDLE) {
			double y0 = view->scene_tree->node.y + view->room_lift;
			double foot = y0 + view->outer_h;
			struct desk_ti *t = ti_of_focus(server);
			if (t && t->ti->current.features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE) {
				struct wlr_box r = t->ti->current.cursor_rectangle;
				int l, top, right, b;
				view_insets(view, &l, &top, &right, &b);
				foot = y0 + top + r.y + r.height;
			}
			to = fmax(foot + 16.0 * desk_ui_scale() - server->room_top, 0.0);
			/* (its head stays on the desk, if it is not what is typed in) */
			if (!t) {
				to = fmin(to, fmax(y0 - work_area(server).y, 0.0));
			}
		}
		if (to != view->room_to) {
			view->room_to = to;
			server_schedule_frames(server);
		}
	}
}

static void room_tick(struct desk_server *server)
{
	bool more = false;
	double now = monotonic_sec();
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->room_lift == view->room_to || !view->scene_tree) {
			continue;
		}
		double dt = clampf_d(now - view->room_at, 0.0, 0.05);
		view->room_at = now;
		double was = view->room_lift;
		double next = was + (view->room_to - was) * (1.0 - exp(-dt * 13.0));
		if (fabs(view->room_to - next) < 0.6) {
			next = view->room_to;
		} else {
			more = true;
		}
		view->room_lift = next;
		wlr_scene_node_set_position(&view->scene_tree->node, view->scene_tree->node.x,
			(int)lround(view->scene_tree->node.y + was - next));
		view_sync_shadow(view);
		view_send_position(view);
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* ---- what a thing can ask of the desk ------------------------------------
 *
 * (thing.h.) Things are in files of their own and know the desk by these. */

struct wl_display *desk_display(const struct desk_server *server)
{
	return server->wl_display;
}

struct wlr_seat *desk_seat(const struct desk_server *server)
{
	return server->seat;
}

struct wlr_cursor *desk_cursor(const struct desk_server *server)
{
	return server->cursor;
}

struct wlr_scene *desk_scene(const struct desk_server *server)
{
	return server->scene;
}

struct wlr_scene_tree *desk_windows(const struct desk_server *server)
{
	return server->layer_win;
}

int desk_width(const struct desk_server *server)
{
	return server->desk_w;
}

int desk_height(const struct desk_server *server)
{
	return server->desk_h;
}

struct wlr_box desk_screen_box_at(struct desk_server *server, double x, double y)
{
	struct wlr_box box = { 0, 0, server->desk_w, server->desk_h };
	struct wlr_output *out = screen_at(server, x, y);
	if (out && wlr_output_layout_get(server->output_layout, out)) {
		wlr_output_layout_get_box(server->output_layout, out, &box);
	}
	return box;
}

bool desk_at_front(struct desk_server *server, double x, double y)
{
	struct wlr_output *out = wlr_output_layout_output_at(server->output_layout, x, y);
	if (!out) {
		return false;
	}
	struct wlr_box sb;
	wlr_output_layout_get_box(server->output_layout, out, &sb);
	/* the desk's own front, as far up as the drawer's handle; another screen's, its edge */
	int zone = out == first_output(server)
		? desk_drawer_pull_h(server->desk_w, server->desk_h) + 6 : HALF_EDGE + 6;
	return y >= sb.y + sb.height - zone
		&& !wlr_output_layout_output_at(server->output_layout, x, sb.y + sb.height);
}

bool desk_covered(const struct desk_server *server)
{
	return locked(server) || shelf_up(server) || console_up(server) || post_up(server);
}

void desk_take_keys(struct desk_server *server)
{
	struct desk_toplevel *was = view_of_surface(server, server->seat->keyboard_state.focused_surface);
	if (was) {
		view_set_activated(was, false);
		view_repaint_chrome(was);
	}
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

/* Copied: some text, for whoever pastes it. */
struct text_source {
	struct wlr_data_source base;
	char *text;
};

static void text_source_send(struct wlr_data_source *source, const char *mime, int32_t fd)
{
	(void)mime;
	struct text_source *s = wl_container_of(source, s, base);
	ssize_t n = write(fd, s->text, strlen(s->text));
	(void)n;
	close(fd);
}

static void text_source_destroy(struct wlr_data_source *source)
{
	struct text_source *s = wl_container_of(source, s, base);
	free(s->text);
	free(s);
}

static const struct wlr_data_source_impl text_source_impl = {
	.send = text_source_send,
	.destroy = text_source_destroy,
};

void desk_copy_text(struct desk_server *server, const char *text)
{
	struct text_source *s = calloc(1, sizeof *s);
	if (!s) {
		return;
	}
	wlr_data_source_init(&s->base, &text_source_impl);
	s->text = strdup(text);
	if (!s->text) {
		wlr_data_source_destroy(&s->base);
		return;
	}
	static const char *const MIMES[] = { "text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING",
		"TEXT" };
	for (size_t i = 0; i < sizeof MIMES / sizeof MIMES[0]; i++) {
		char **p = wl_array_add(&s->base.mime_types, sizeof *p);
		if (p) {
			*p = strdup(MIMES[i]);
		}
	}
	wlr_seat_set_selection(server->seat, &s->base, wl_display_next_serial(server->wl_display));
}

/* Text on its way from whoever has copied it. */
static struct {
	struct desk_server *server;
	void (*got)(struct desk_server *server, const char *text);
	int fd;
	struct wl_event_source *source;
	char text[4096];
	size_t len;
} g_paste;

static void paste_end(void)
{
	if (g_paste.source) {
		wl_event_source_remove(g_paste.source);
		g_paste.source = NULL;
	}
	if (g_paste.fd > 0) {
		close(g_paste.fd);
		g_paste.fd = 0;
	}
}

static int paste_read(int fd, uint32_t mask, void *data)
{
	(void)mask;
	(void)data;
	size_t room = sizeof g_paste.text - 1 - g_paste.len;
	ssize_t n = room > 0 ? read(fd, g_paste.text + g_paste.len, room) : 0;
	if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
		return 0;
	}
	if (n > 0) {
		g_paste.len += (size_t)n;
		return 0;
	}
	g_paste.text[g_paste.len] = '\0';
	paste_end();
	g_paste.got(g_paste.server, g_paste.text);
	return 0;
}

void desk_paste_text(struct desk_server *server, void (*got)(struct desk_server *server, const char *text))
{
	struct wlr_data_source *src = server->seat->selection_source;
	if (!src) {
		return;
	}
	static const char *const WANT[] = { "text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING",
		"TEXT" };
	const char *mime = NULL;
	for (size_t i = 0; i < sizeof WANT / sizeof WANT[0] && !mime; i++) {
		char **p;
		wl_array_for_each(p, &src->mime_types) {
			if (!strcmp(*p, WANT[i])) {
				mime = WANT[i];
				break;
			}
		}
	}
	int fds[2];
	if (!mime || pipe(fds) != 0) {
		return;
	}
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	paste_end();
	g_paste.server = server;
	g_paste.got = got;
	g_paste.len = 0;
	g_paste.fd = fds[0];
	g_paste.source = wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display),
		fds[0], WL_EVENT_READABLE, paste_read, NULL);
	wlr_data_source_send(src, mime, fds[1]);
}

struct desk_toplevel *view_next(struct desk_server *server, struct desk_toplevel *view)
{
	struct wl_list *at = view ? view->link.next : server->toplevels.next;
	if (at == &server->toplevels) {
		return NULL;
	}
	struct desk_toplevel *next = wl_container_of(at, next, link);
	return next;
}

struct desk_server *view_server(const struct desk_toplevel *view)
{
	return view->server;
}

int view_w(const struct desk_toplevel *view)
{
	return view->outer_w;
}

int view_h(const struct desk_toplevel *view)
{
	return view->outer_h;
}

struct wlr_scene_tree *view_tree(const struct desk_toplevel *view)
{
	return view->scene_tree;
}

void view_lay_under(struct desk_toplevel *view, struct wlr_scene_node *node)
{
	wlr_scene_node_place_below(node, view->chrome ? &view->chrome->node : &view->surface_tree->node);
}

bool view_is(const struct desk_toplevel *view, const struct desk_thing *thing)
{
	return view->thing == thing;
}

void *view_thing_data(struct desk_toplevel *view, const struct desk_thing *thing, size_t size)
{
	if (view->thing_data_of != thing) {
		free(view->thing_data);
		view->thing_data = calloc(1, size);
		if (!view->thing_data) {
			wlr_log(WLR_ERROR, "out of memory for the %s", thing->name);
			abort();
		}
		view->thing_data_of = thing;
	}
	return view->thing_data;
}

void view_redraw(struct desk_toplevel *view)
{
	view->chrome_key = 0;
	view_repaint_chrome(view);
}

bool view_on_desk(const struct desk_toplevel *view)
{
	return view->scene_tree && !view->unmanaged;
}

const char *view_wayland_id(const struct desk_toplevel *view)
{
	return view->xdg_toplevel ? view->xdg_toplevel->app_id : NULL;
}

bool view_has_parent(const struct desk_toplevel *view)
{
	return view->xdg_toplevel && view->xdg_toplevel->parent;
}

const char *view_program_id(const struct desk_toplevel *view)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		return view->xsurface->class;
	}
#endif
	return view_wayland_id(view);
}

void view_bring_back(struct desk_toplevel *view)
{
	if (view->sheet.phase != SHEET_IDLE) {
		view_fold_toggle(view);
	}
	focus_toplevel(view);
}

static void onscreen_leds(struct wlr_keyboard *keyboard, uint32_t leds)
{
	(void)keyboard;
	(void)leds;
}

static const struct wlr_keyboard_impl onscreen_impl = { .name = "desk-keyboard", .led_update = onscreen_leds };
static struct wlr_keyboard g_onscreen;
static bool g_onscreen_made;

struct wlr_keyboard *desk_onscreen_keyboard(struct desk_server *server)
{
	if (!g_onscreen_made) {
		g_onscreen_made = true;
		wlr_keyboard_init(&g_onscreen, &onscreen_impl, "the keyboard on the screen");
		server_new_keyboard(server, &g_onscreen.base);
		seat_says_what_it_has(server);
	}
	return &g_onscreen;
}

struct wlr_output_layout *desk_layout(const struct desk_server *server)
{
	return server->output_layout;
}

struct wlr_scene_tree *desk_over_all(const struct desk_server *server)
{
	return server->layer_keys;
}

bool desk_door_shut(const struct desk_server *server)
{
	return locked(server);
}

bool desk_type_text(struct desk_server *server, const char *text)
{
	struct desk_ti *t = ti_of_focus(server);
	if (!t) {
		return false;
	}
	ti_commit_text(text, t);
	return true;
}

/* A character for an X program, which has no key for it: Xwayland is asked to put it on a key that
 * nothing uses, as programs that type for you do on X, and that key is what is pressed. Its own
 * connection, made the first time. */
#ifdef DESK_HAVE_XWAYLAND
static xcb_connection_t *g_xc;
static xcb_keycode_t g_spare;

static bool x_spare_ready(struct desk_server *server)
{
	if (g_xc && !xcb_connection_has_error(g_xc)) {
		return g_spare != 0;
	}
	if (g_xc) {
		xcb_disconnect(g_xc);
		g_xc = NULL;
	}
	g_spare = 0;
	if (!server->xwayland || !server->xwayland->display_name) {
		return false;
	}
	g_xc = xcb_connect(server->xwayland->display_name, NULL);
	if (xcb_connection_has_error(g_xc)) {
		return false;
	}
	const xcb_setup_t *setup = xcb_get_setup(g_xc);
	int lo = setup->min_keycode, hi = setup->max_keycode;
	xcb_get_keyboard_mapping_reply_t *map = xcb_get_keyboard_mapping_reply(g_xc,
		xcb_get_keyboard_mapping(g_xc, (xcb_keycode_t)lo, (uint8_t)(hi - lo + 1)), NULL);
	if (!map) {
		return false;
	}
	xcb_keysym_t *syms = xcb_get_keyboard_mapping_keysyms(map);
	int per = map->keysyms_per_keycode;
	/* (from the top: the keys at the end of the keymap are those no keyboard has) */
	for (int code = hi; code >= lo && !g_spare; code--) {
		bool none = true;
		for (int i = 0; i < per; i++) {
			none = none && syms[(code - lo) * per + i] == 0;
		}
		if (none) {
			g_spare = (xcb_keycode_t)code;
		}
	}
	free(map);
	return g_spare != 0;
}

static void x_spare_set(xcb_keysym_t sym)
{
	xcb_keysym_t two[2] = { sym, sym };
	xcb_change_keyboard_mapping(g_xc, 1, g_spare, 2, two);
	/* (and it is done before the key comes: the round trip) */
	free(xcb_get_input_focus_reply(g_xc, xcb_get_input_focus(g_xc), NULL));
}
#endif

bool desk_typing_to_x(struct desk_server *server)
{
#ifdef DESK_HAVE_XWAYLAND
	struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
	return focus && wlr_xwayland_surface_try_from_wlr_surface(focus);
#else
	(void)server;
	return false;
#endif
}

int desk_x_key_for(struct desk_server *server, uint32_t cp)
{
#ifdef DESK_HAVE_XWAYLAND
	if (!x_spare_ready(server)) {
		return 0;
	}
	x_spare_set(cp < 0x100 ? cp : 0x01000000u | cp);
	return g_spare - 8;
#else
	(void)server;
	(void)cp;
	return 0;
#endif
}

void desk_x_key_done(struct desk_server *server)
{
	(void)server;
#ifdef DESK_HAVE_XWAYLAND
	if (g_xc && g_spare && !xcb_connection_has_error(g_xc)) {
		x_spare_set(0);
	}
#endif
}

void desk_run(const char *program)
{
	desk_run_with(program, NULL);
}

void desk_run_with(const char *program, const char *const *words)
{
	char *argv[16];
	int n = 1;
	for (; words && words[n - 1] && n < 15; n++) {
		argv[n] = (char *)words[n - 1];
	}
	argv[n] = NULL;
	/* (the one that came with this desk, as built or installed beside it, before any other) */
	char exe[1024];
	ssize_t k = readlink("/proc/self/exe", exe, sizeof exe - 1);
	exe[k > 0 ? k : 0] = '\0';
	char *slash = strrchr(exe, '/');
	if (slash && !strchr(program, '/') && (size_t)(slash + 1 - exe) + strlen(program) < sizeof exe) {
		strcpy(slash + 1, program);
		if (access(exe, X_OK) == 0) {
			argv[0] = exe;
			spawn_argv(argv);
			return;
		}
	}
	argv[0] = (char *)program;
	spawn_argv(argv);
}

bool desk_busy(const struct desk_server *server)
{
	return desk_covered(server) || server->drawer_t > 0.004 || server->sw_on || server->sw_shown
		|| server->snip_on;
}

bool desk_window_under(struct desk_server *server)
{
	return window_under(server);
}

struct wlr_scene_tree *desk_on_desk(const struct desk_server *server)
{
	return server->layer_mat;
}

void desk_hand_shape(struct desk_server *server, const char *name)
{
	cursor_image(server, name);
}

void desk_overlays_changed(struct desk_server *server)
{
	overlay_update(server, false);
	server_schedule_frames(server);
}

bool desk_fingers_lately(const struct desk_server *server)
{
	return server->hand_unseen || server->pen_lately;
}

bool desk_hand_finger_on(const struct desk_server *server, int32_t besides)
{
	for (int i = 0; i < DESK_FINGERS; i++) {
		const struct desk_finger *f = &server->fingers[i];
		if (f->on && f->id != besides && f->goes == FINGER_HAND) {
			return true;
		}
	}
	return false;
}

bool desk_pen_writing(struct desk_server *server)
{
	struct desk_toplevel *view = view_of_surface(server, server->seat->keyboard_state.focused_surface);
	const struct desk_thing *thing = view ? view_thing(view) : NULL;
	return thing && thing->pen_writes && desk_pen_lately(server);
}

bool desk_pen_lately(const struct desk_server *server)
{
	return server->pen_lately;
}

double desk_pen_pressure(const struct desk_server *server)
{
	return server->pen_pressure;
}

bool desk_pen_there(const struct desk_server *server)
{
	const struct desk_tablet *t;
	wl_list_for_each(t, &server->tablets, link) {
#ifdef DESK_HAVE_LIBINPUT
		/* (the window of a nested desk says it has one, whatever the machine has) */
		if (!wlr_input_device_is_libinput(&t->wlr_tablet->base)) {
			continue;
		}
#endif
		return true;
	}
	return false;
}

struct wlr_output *desk_finger_screen(struct desk_server *server)
{
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output == server->finger_screen && !o->off) {
			return o->wlr_output;
		}
	}
	return first_output(server);
}

bool desk_typing_keyboard(const struct desk_server *server)
{
	if (server->tablet_mode) {
		return false;
	}
	struct desk_keyboard *keyboard;
	wl_list_for_each(keyboard, &server->keyboards, link) {
		struct wlr_input_device *device = &keyboard->wlr_keyboard->base;
		if (keyboard->wlr_keyboard == &g_onscreen) {
			continue;
		}
#ifdef DESK_HAVE_LIBINPUT
		/* (a power button and the volume keys are a keyboard too, to the machine) */
		if (wlr_input_device_is_libinput(device)
			&& libinput_device_keyboard_has_key(wlr_libinput_get_device_handle(device), KEY_A) != 1) {
			continue;
		}
#else
		(void)device;
#endif
		return true;
	}
	return false;
}

bool desk_touch_there(const struct desk_server *server)
{
	const struct desk_touch_device *t;
	wl_list_for_each(t, &server->touch_devices, link) {
#ifdef DESK_HAVE_LIBINPUT
		/* (the window of a nested desk says it has one, whatever the machine has) */
		if (!wlr_input_device_is_libinput(t->device)) {
			continue;
		}
#endif
		return true;
	}
	return false;
}

int desk_keys_setting(const struct desk_server *server)
{
	return server->conf.onscreen_keyboard;
}

void desk_keys_room(struct desk_server *server, int top)
{
	server->room_top = top;
	room_update(server);
}

void view_begin_move(struct desk_toplevel *view)
{
	begin_interactive(view, DESK_CUR_MOVE, 0);
}

void view_begin_resize(struct desk_toplevel *view, unsigned edges)
{
	begin_interactive(view, DESK_CUR_RESIZE, edges);
}

void view_pass_press(struct desk_toplevel *view, struct wlr_pointer_button_event *event)
{
	struct desk_server *server = view->server;
	focus_toplevel(view);
	if (pen_down_on_program(server, event)) {
		return;
	}
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
	if (server->seat->pointer_state.button_count == 1) {
		pointer_hold(server);
	}
}

void drawer_thing_present(struct desk_server *server, const struct desk_thing *thing, bool present)
{
	for (int i = 0; i < server->drw_n; i++) {
		if (server->drw[i].kind == DRAWER_THING && server->drw[i].thing == thing) {
			server->drw[i].present = present;
		}
	}
	drawer_layout(server, false);
}

static void things_say(struct desk_server *server)
{
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->say) {
			(*t)->say(server);
		}
	}
}

/* A button pressed over the paper: a window's corner, the edge of its
 * frame or its heading -- to be turned up, pulled or moved -- or the
 * program in it, which is focused and handed the click. */
static void paper_press(struct desk_server *server, struct wlr_pointer_button_event *event)
{
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct desk_toplevel *toplevel = view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	const struct desk_thing *thing = toplevel ? view_thing(toplevel) : NULL;
	if (thing && thing->press) {
		thing->press(toplevel, surface, event);
		return;
	}
	if (toplevel && view_framed(toplevel)) {
		focus_toplevel(toplevel);
		double lx = server->cursor->x - toplevel->scene_tree->node.x;
		double ly = server->cursor->y - toplevel->scene_tree->node.y;
		/* the turned-down corner, and what is printed on its back */
		double cx = lx - (toplevel->outer_w - toplevel->curl_box);
		double rest, open;
		curl_sizes(toplevel, &rest, &open);
		double zone = rest * 2.6, dx = toplevel->outer_w - lx, past = curl_past(toplevel);
		bool on_fold = !toplevel->curl_near && dx >= -past && dx <= zone && ly >= -past && ly <= zone
			&& (!toplevel->own_frame || mount_on_fold(toplevel, dx, ly));
		if (toplevel->curl && toplevel->curl->node.enabled
			&& (on_fold || desk_curl_contains(toplevel->curl_box, toplevel->curl_s, cx, ly))) {
			if (!toplevel->curl_near) {
				/* down: this click only turns it up */
				if (event->button == BTN_LEFT) {
					curl_turn_up(toplevel);
				}
				return;
			}
			int item = desk_curl_hit(toplevel->curl_box, toplevel->curl_s, open, cx, ly);
			if (item >= 0 && event->button == BTN_LEFT && toplevel->curl_act < 0) {
				curl_press(toplevel, item);
			}
			return;
		}
		/* on the program itself, laid on our paper: the program's (its own
		 * heading, its own buttons) */
		if (!(toplevel->own_frame && surface)) {
			int edges = desk_frame_resize_edges(lx, ly, toplevel->outer_w,
				toplevel->outer_h);
			if (edges == WLR_EDGE_BOTTOM && event->button == BTN_LEFT
				&& view_peek_able(toplevel)) {
				/* the bottom edge, down on the drawer's pull: a click, or a
				 * pull -- which, the hand will say (peek_press_motion) */
				server->grabbed = toplevel;
				server->cursor_mode = DESK_CUR_PEEK;
				server->move_from_x = server->cursor->x;
				server->move_from_y = server->cursor->y;
				return;
			}
			if (edges) {
				begin_interactive(toplevel, DESK_CUR_RESIZE, (uint32_t)edges);
				return;
			}
			/* (what is left of a thing, off the window in it, is something to pick it
			 * up by) */
			if (thing ? !surface : desk_frame_title_hit(lx, ly, toplevel->outer_w, toplevel->outer_h)) {
				begin_interactive(toplevel, DESK_CUR_MOVE, 0);
				return;
			}
		}
	} else if (toplevel) {
		/* no frame to catch: the click is the client's, and so is moving it */
		focus_toplevel(toplevel);
	}
	if (pen_down_on_program(server, event)) {
		return;
	}
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
	if (server->seat->pointer_state.button_count == 1) {
		pointer_hold(server);
	}
}

/* A wheel: a mouse's, or a finger turning over what can be turned (fingers). */
static void pointer_axis(struct desk_server *server, struct wlr_pointer_axis_event *event)
{
	if (!input_seen(server, "the wheel")) {
		return;
	}
	desk_sound_awake();
	if (server->snip_on) {
		snip_wheel(server, event);
		return;
	}
	if (locked(server) || server->sw_shown) {
		return;
	}
	/* beside the desk, only paper: the program under the pointer's */
	if (pointer_on_side(server)) {
		if (!paper_moved(server)) {
			wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
				event->delta, event->delta_discrete, event->source, event->relative_direction);
		}
		return;
	}
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->wheel && (*t)->wheel(server, event)) {
			return;
		}
	}
	/* Scrolling over an open binder turns the block over. A wheel notch
	 * (DESK_NOTCH) buys one sheet; a device that sends a stream of small
	 * ones banks them until it has a notch's worth. */
	if (console_up(server)) {
		/* A wheel over a knob turns it. It is the one gesture on the set
		 * that is not a hand on the control itself, and it is allowed
		 * because turning a knob is what a wheel is. */
		if (server->cc_t < 0.9995
			|| event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL) {
			return;
		}
		int which = desk_console_hit_lid(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y, server->cc_guard);
		if (which != DESK_CC_VOL && which != DESK_CC_BRIGHT
			&& !(which == DESK_CC_SCREEN && server->cc_screen_ok)) {
			return;
		}
		double d = event->delta;
		if (d == 0.0 && event->delta_discrete != 0) {
			d = (double)event->delta_discrete / 120.0 * 15.0;
		}
		if (d == 0.0) {
			return;
		}
		double *v = which == DESK_CC_VOL ? &server->cc_vol
			: which == DESK_CC_SCREEN ? &server->cc_screen : &server->cc_bright;
		double was = *v;
		*v = clampf_d(*v - d / DESK_NOTCH * 0.04, 0.0, 1.0);
		if (fabs(*v - was) < 1e-6) {
			return;
		}
		if (which == DESK_CC_VOL) {
			desk_meters_set_volume(*v);
		} else if (which == DESK_CC_SCREEN) {
			screen_apply(server);
		} else {
			console_dim_apply(server);
		}
		paint_console(server);
		server_schedule_frames(server);
		return;
	}
	if (shelf_up(server) && memo_up(server)) {
		/* a sheet a notch */
		if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
			double d = event->delta;
			if (d == 0.0 && event->delta_discrete != 0) {
				d = (double)event->delta_discrete / 120.0 * 15.0;
			}
			memo_scroll(server, d);
		}
		return;
	}
	if (shelf_up(server) && cal_up(server)) {
		/* a page a notch: down on, up back -- or on the ruler, the time */
		if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL || !cal_settled(server)) {
			return;
		}
		double d = event->delta;
		if (d == 0.0 && event->delta_discrete != 0) {
			d = (double)event->delta_discrete / 120.0 * 15.0;
		}
		int rarg = 0;
		if (agd_up(server) && agd_hit_at(server, server->cursor->x, server->cursor->y, &rarg)
				== DESK_AGENDA_HIT_RULER) {
			server->agd_scroll += d;
			while (server->agd_scroll >= DESK_NOTCH) {
				server->agd_scroll -= DESK_NOTCH;
				agd_ruler_roll(server, 1);
			}
			while (server->agd_scroll <= -DESK_NOTCH) {
				server->agd_scroll += DESK_NOTCH;
				agd_ruler_roll(server, -1);
			}
			return;
		}
		server->cal_scroll += d;
		while (server->cal_scroll >= DESK_NOTCH) {
			server->cal_scroll -= DESK_NOTCH;
			cal_turn_page(server, 1);
		}
		while (server->cal_scroll <= -DESK_NOTCH) {
			server->cal_scroll += DESK_NOTCH;
			cal_turn_page(server, -1);
		}
		return;
	}
	if (shelf_up(server) && server->shelf_binder >= 0) {
		if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL) {
			return;
		}
		double d = event->delta;
		if (d == 0.0 && event->delta_discrete != 0) {
			d = (double)event->delta_discrete / 120.0 * 15.0;
		}
		/* A book's leaf is a spread, a whole two pages of the index: a finger (on a touchpad, or
		 * on the screen, which comes here as one) goes further for one than for a sheet of a
		 * binder, and turns one a stroke; a wheel, a leaf a notch, three at most at a time
		 * (owner 2026-11-10: 本の感度が強い). */
		bool book = server->shelf_binder == DESK_SHELF_BOOK;
		bool finger = event->source == WL_POINTER_AXIS_SOURCE_FINGER
			|| event->source == WL_POINTER_AXIS_SOURCE_CONTINUOUS;
		double notch = book && finger ? DESK_NOTCH * 3.0 : DESK_NOTCH;
		int burst_most = book ? (finger ? 1 : 3) : 5;
		server->binder_scroll += d;
		int step = 0;
		while (server->binder_scroll >= notch) {
			step++;
			server->binder_scroll -= notch;
		}
		while (server->binder_scroll <= -notch) {
			step--;
			server->binder_scroll += notch;
		}
		if (!step) {
			return;
		}
		/* One flick of a trackpad arrives as two dozen notches, and every
		 * one of them buying a sheet tore through the whole block and left
		 * it sitting on the last one -- which is what "it does not turn"
		 * looked like. Rate limiting cannot tell the difference, because
		 * the compositor paces those notches out one per frame anyway.
		 *
		 * So it is counted per gesture instead: notches that keep arriving
		 * are the same push of the hand, and a push turns a few sheets, not
		 * the whole block. A quarter of a second of quiet ends the gesture,
		 * so deliberate notch-by-notch scrolling is never capped. */
		double now = real_sec();
		if (now - server->binder_axis_at > 0.25) {
			server->binder_burst = 0;
		}
		server->binder_axis_at = now;
		if (server->binder_burst >= burst_most) {
			server->binder_scroll = 0.0;
			return;
		}
		server->binder_burst++;
		binder_riffle(server, step > 0 ? 1 : -1);
		return;
	}
	if (shelf_up(server)) {
		return;
	}
	if (post_up(server)) {
		/* Over the open box, the wheel turns the letters in it over, one
		 * a notch, the way it turns a binder's sheets. */
		if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL || server->pb_t < 0.9995) {
			return;
		}
		double d = event->delta;
		if (d == 0.0 && event->delta_discrete != 0) {
			d = (double)event->delta_discrete / 120.0 * 15.0;
		}
		server->pb_scroll += d;
		int step = 0;
		while (server->pb_scroll >= DESK_NOTCH) {
			step++;
			server->pb_scroll -= DESK_NOTCH;
		}
		while (server->pb_scroll <= -DESK_NOTCH) {
			step--;
			server->pb_scroll += DESK_NOTCH;
		}
		if (step) {
			pb_riffle(server, step > 0 ? 1 : -1);
		}
		return;
	}
	/* the clock on the desk, when no window is over it */
	if (!post_up(server) && card_axis(server, event)) {
		return;
	}
	if (!post_up(server) && !window_under(server)
		&& (clock_axis(server, event) || slips_axis(server, event))) {
		return;
	}
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta,
		event->delta_discrete, event->source, event->relative_direction);
}
