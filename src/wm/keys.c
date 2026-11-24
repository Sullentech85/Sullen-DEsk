/* Part of wm.c: typed input, Ctrl+Alt+Del and the keyboard's keys.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static bool ti_same_client(struct wlr_text_input_v3 *ti, struct wlr_surface *s)
{
	return s && ti->resource && wl_resource_get_client(ti->resource)
		== wl_resource_get_client(s->resource);
}

static void ti_forward(struct desk_server *server, uint32_t keycode, uint32_t state)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	uint32_t ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
	wlr_seat_keyboard_notify_key(server->seat, ms, keycode, state);
}

static void ti_commit_text(const char *text, void *user)
{
	struct desk_ti *t = user;
	wlr_text_input_v3_send_preedit_string(t->ti, NULL, 0, 0);
	wlr_text_input_v3_send_commit_string(t->ti, text);
	wlr_text_input_v3_send_done(t->ti);
}

static void ti_preedit(const char *text, int cursor, bool visible, void *user)
{
	struct desk_ti *t = user;
	if (!visible || !text || !*text) {
		wlr_text_input_v3_send_preedit_string(t->ti, NULL, 0, 0);
	} else {
		/* the cursor comes as characters, and goes as bytes */
		int b = 0;
		for (int c = 0; text[b] && c < cursor; c++) {
			b++;
			while ((text[b] & 0xc0) == 0x80) {
				b++;
			}
		}
		wlr_text_input_v3_send_preedit_string(t->ti, text, b, b);
	}
	wlr_text_input_v3_send_done(t->ti);
}

/* what is at the front of the line goes to the window, up to the first key
 * the input method has not answered for yet */
static void ti_flush(struct desk_server *server)
{
	while (server->ti_q_n > 0) {
		struct ti_ev * e = &server->ti_q[server->ti_q_head];
		if (e->waiting) {
			break;
		}
		if (e->mods_only) {
			struct wlr_keyboard_modifiers m = e->mods;
			wlr_seat_keyboard_notify_modifiers(server->seat, &m);
		} else if (e->code < sizeof server->ti_keys) {
			uint8_t *k = &server->ti_keys[e->code];
			if (e->pressed) {
				if (e->taken) {
					*k = TIK_TAKEN;
				} else {
					ti_forward(server, e->code, WL_KEYBOARD_KEY_STATE_PRESSED);
					*k = TIK_DOWN;
				}
			} else {
				/* let go: unless the input method kept it when it went down */
				if (*k != TIK_TAKEN) {
					ti_forward(server, e->code, WL_KEYBOARD_KEY_STATE_RELEASED);
				}
				*k = TIK_IDLE;
			}
		}
		server->ti_q_head = (server->ti_q_head + 1) % 64;
		server->ti_q_n--;
	}
	if (server->ti_timer) {
		wl_event_source_timer_update(server->ti_timer, server->ti_q_n > 0 ? 800 : 0);
	}
}

static void ti_push(struct desk_server *server, bool mods_only, bool pressed, bool waiting,
	uint32_t code, const struct wlr_keyboard_modifiers *mods)
{
	if (server->ti_q_n >= 64) {
		/* a line that long is an input method gone quiet: let it all go */
		for (int i = 0; i < server->ti_q_n; i++) {
			server->ti_q[(server->ti_q_head + i) % 64].waiting = 0;
		}
		ti_flush(server);
	}
	struct ti_ev * e = &server->ti_q[(server->ti_q_head + server->ti_q_n) % 64];
	memset(e, 0, sizeof *e);
	e->mods_only = mods_only;
	e->pressed = pressed;
	e->waiting = waiting;
	e->code = code;
	if (mods) {
		e->mods = *mods;
	}
	server->ti_q_n++;
}

/* the input method has not answered in a long while: the keys go on as
 * they are */
static int ti_timeout(void *data)
{
	struct desk_server *server = data;
	for (int i = 0; i < server->ti_q_n; i++) {
		server->ti_q[(server->ti_q_head + i) % 64].waiting = 0;
	}
	ti_flush(server);
	return 0;
}

static void ti_key(uint32_t keysym, uint32_t keycode, uint32_t mods, bool taken, bool answer,
	void *user)
{
	(void)keysym;
	(void)mods;
	struct desk_ti *t = user;
	struct desk_server *server = t->server;
	if (!answer) {
		/* the input method giving a key back, to be typed as it is */
		ti_push(server, false, true, false, keycode, NULL);
		ti_push(server, false, false, false, keycode, NULL);
		ti_flush(server);
		return;
	}
	for (int i = 0; i < server->ti_q_n; i++) {
		struct ti_ev * e = &server->ti_q[(server->ti_q_head + i) % 64];
		if (e->waiting && !e->mods_only && e->pressed && e->code == keycode) {
			e->waiting = 0;
			e->taken = taken;
			break;
		}
	}
	ti_flush(server);
}

struct ti_find {
	struct wlr_surface *surface;
	int x, y;
	bool found;
};

static void ti_find_buffer(struct wlr_scene_buffer *b, int sx, int sy, void *data)
{
	struct ti_find *f = data;
	struct wlr_scene_surface *ss = f->found ? NULL : wlr_scene_surface_try_from_buffer(b);
	if (ss && ss->surface == f->surface) {
		f->x = sx;
		f->y = sy;
		f->found = true;
	}
}

static void ime_window_place(struct desk_toplevel *view);

/* where the cursor is, on the screen the input method's windows go on */
static void ti_cursor(struct desk_ti *t)
{
	struct desk_server *server = t->server;
	if (desk_ime_typing_owner() != t || !t->ti->focused_surface) {
		return;
	}
	struct ti_find f = { t->ti->focused_surface, 0, 0, false };
	wlr_scene_node_for_each_buffer(&server->scene->tree.node, ti_find_buffer, &f);
	if (!f.found) {
		return;
	}
	struct wlr_box r = t->ti->current.cursor_rectangle;
	if (!(t->ti->active_features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE)) {
		r = (struct wlr_box){ 0, 0, 1, 18 };
	}
	server->ime_at = (struct wlr_box){ f.x + r.x, f.y + r.y, r.width > 0 ? r.width : 1,
		r.height > 0 ? r.height : 18 };
	desk_ime_typing_cursor(server->ime_at.x, server->ime_at.y, server->ime_at.width,
		server->ime_at.height);
	/* and what the input method has up goes with it, as it would on X */
	for (size_t i = 0; i < sizeof server->ime_win / sizeof *server->ime_win; i++) {
		struct desk_toplevel *v = server->ime_win[i];
		if (v && v->scene_tree->node.enabled) {
			ime_window_place(v);
		}
	}
}

/* Something to type in has come up, or gone, as far as a keyboard on the screen goes. */
static void things_told_text_wanted(struct desk_server *server, bool wanted)
{
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->text_wanted) {
			(*t)->text_wanted(server, wanted);
		}
	}
}

static void things_text_wanted(struct desk_server *server, bool wanted)
{
	if (server->text_wanted != wanted) {
		server->text_wanted = wanted;
		things_told_text_wanted(server, wanted);
	}
}

/* Is the window with the keys asking for what is typed? */
static struct desk_ti *ti_of_focus(struct desk_server *server)
{
	struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
	struct desk_ti *t;
	wl_list_for_each(t, &server->text_inputs, link) {
		if (focus && t->ti->current_enabled && t->ti->focused_surface == focus) {
			return t;
		}
	}
	return NULL;
}

static void ti_attach(struct desk_ti *t)
{
	struct desk_server *server = t->server;
	void *owner = desk_ime_typing_owner();
	if (owner == t) {
		return;
	}
	if (owner) {
		return;   /* DEsk's own notes have it */
	}
	const struct desk_ime_typing ty = { ti_commit_text, ti_preedit, NULL, ti_key, t };
	desk_ime_typing_begin(&ty);
	server->ti_ime = t;
	memset(server->ti_keys, 0, sizeof server->ti_keys);
	server->ti_q_n = 0;
	ti_cursor(t);
}

static void ti_detach(struct desk_server *server)
{
	if (server->ti_ime && desk_ime_typing_owner() == server->ti_ime) {
		desk_ime_typing_end();
	}
	server->ti_ime = NULL;
	server->ime_at = (struct wlr_box){ 0 };
	memset(server->ti_keys, 0, sizeof server->ti_keys);
	server->ti_q_n = 0;
	if (server->ti_timer) {
		wl_event_source_timer_update(server->ti_timer, 0);
	}
}

static void ti_on_enable(struct wl_listener *listener, void *data)
{
	(void)data;
	struct desk_ti *t = wl_container_of(listener, t, enable);
	if (t->ti->focused_surface
		&& t->ti->focused_surface == t->server->seat->keyboard_state.focused_surface) {
		ti_attach(t);
	}
}

static void ti_on_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct desk_ti *t = wl_container_of(listener, t, commit);
	if (t->ti->current_enabled && t->ti->focused_surface
		&& t->ti->focused_surface == t->server->seat->keyboard_state.focused_surface) {
		ti_attach(t);
		ti_cursor(t);
		things_text_wanted(t->server, true);
		room_update(t->server);
	}
}

static void ti_on_disable(struct wl_listener *listener, void *data)
{
	(void)data;
	struct desk_ti *t = wl_container_of(listener, t, disable);
	if (t->server->ti_ime == t) {
		ti_detach(t->server);
	}
	things_text_wanted(t->server, false);
}

static void ti_on_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct desk_ti *t = wl_container_of(listener, t, destroy);
	if (t->server->ti_ime == t) {
		ti_detach(t->server);
	}
	wl_list_remove(&t->enable.link);
	wl_list_remove(&t->commit.link);
	wl_list_remove(&t->disable.link);
	wl_list_remove(&t->destroy.link);
	wl_list_remove(&t->link);
	free(t);
}

static void server_new_text_input(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_text_input);
	struct wlr_text_input_v3 *ti = data;
	struct desk_ti *t = calloc(1, sizeof *t);
	if (!t) {
		return;
	}
	t->server = server;
	t->ti = ti;
	t->enable.notify = ti_on_enable;
	wl_signal_add(&ti->events.enable, &t->enable);
	t->commit.notify = ti_on_commit;
	wl_signal_add(&ti->events.commit, &t->commit);
	t->disable.notify = ti_on_disable;
	wl_signal_add(&ti->events.disable, &t->disable);
	t->destroy.notify = ti_on_destroy;
	wl_signal_add(&ti->events.destroy, &t->destroy);
	wl_list_insert(&server->text_inputs, &t->link);
	struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
	if (ti_same_client(ti, focus)) {
		wlr_text_input_v3_send_enter(ti, focus);
	}
}

/* Pointer capture follows keyboard focus too, so switching apps releases a game. */
static void desk_constraint_refresh(struct desk_server *server);

/* The ask for every key in force (server->keys_inhibit): the one of the
 * surface that has the keys, if it has asked and has not been told no. */
static struct wlr_keyboard_shortcuts_inhibitor_v1 *keys_asked(struct desk_server *server)
{
	struct wlr_surface *has = server->seat->keyboard_state.focused_surface;
	if (!server->keys_inhibit || !has) {
		return NULL;
	}
	struct wlr_keyboard_shortcuts_inhibitor_v1 *in;
	wl_list_for_each(in, &server->keys_inhibit->inhibitors, link) {
		if (in->active && in->surface == has && in->seat == server->seat) {
			return in;
		}
	}
	return NULL;
}

/* The keys are with surface now (or someone has just asked): its asks are
 * in force, everyone else's are not. Given back with Super+Esc, an ask is
 * in force again once the keys have been away from it and come back, or
 * when its program asks again. */
static void keys_asked_follow(struct desk_server *server, struct wlr_surface *surface)
{
	if (!server->keys_inhibit) {
		return;
	}
	struct wlr_keyboard_shortcuts_inhibitor_v1 *in;
	wl_list_for_each(in, &server->keys_inhibit->inhibitors, link) {
		bool on = surface && in->surface == surface && in->seat == server->seat;
		if (on && !in->active) {
			wlr_keyboard_shortcuts_inhibitor_v1_activate(in);
			wlr_log(WLR_INFO, "a window has asked for every key, and has them (Super+Esc gives them back)");
		} else if (!on && in->active) {
			wlr_keyboard_shortcuts_inhibitor_v1_deactivate(in);
		}
	}
}

static void server_new_keys_inhibitor(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_keys_inhibitor);
	(void)data;
	keys_asked_follow(server, server->seat->keyboard_state.focused_surface);
}

/* the keyboard has gone to another surface: the text inputs follow it, and
 * so do the asks for every key */
static void seat_kb_focus_change(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, kb_focus_change);
	struct wlr_seat_keyboard_focus_change_event *ev = data;
	desk_constraint_refresh(server);
	keys_asked_follow(server, ev->new_surface);
	if (server->ti_ime && server->ti_ime->ti->focused_surface != ev->new_surface) {
		ti_detach(server);
	}
	struct desk_ti *t;
	wl_list_for_each(t, &server->text_inputs, link) {
		if (t->ti->focused_surface && t->ti->focused_surface != ev->new_surface) {
			wlr_text_input_v3_send_leave(t->ti);
		}
		if (!t->ti->focused_surface && ti_same_client(t->ti, ev->new_surface)) {
			wlr_text_input_v3_send_enter(t->ti, ev->new_surface);
		}
	}
	if (!ti_of_focus(server) && !desk_ime_typing_owner()) {
		things_text_wanted(server, false);
	}
	room_update(server);
}

/* A key for the window in front: to the input method first, if the window
 * is typing through one. True if it is in hand (the window gets it, or
 * not, when the input method has answered). */
static bool ti_key_in(struct desk_server *server, uint32_t code, bool pressed, uint32_t sym,
	uint32_t mods)
{
	struct desk_ti *t = server->ti_ime;
	struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
	if (!t || !t->ti->current_enabled || t->ti->focused_surface != focus) {
		t = NULL;
		struct desk_ti *x;
		wl_list_for_each(x, &server->text_inputs, link) {
			if (x->ti->current_enabled && focus && x->ti->focused_surface == focus) {
				t = x;
			}
		}
		if (!t) {
			return false;
		}
	}
	if (desk_ime_typing_owner() != t) {
		if (desk_ime_typing_owner()) {
			return false;
		}
		ti_attach(t);
	}
	if (code >= sizeof server->ti_keys) {
		return false;
	}
	if (!server->ti_timer) {
		server->ti_timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
			ti_timeout, server);
	}
	if (!pressed) {
		desk_ime_typing_key(sym, code, mods, true);
		ti_push(server, false, false, false, code, NULL);
	} else {
		bool asked = desk_ime_typing_key(sym, code, mods, false);
		ti_push(server, false, true, asked, code, NULL);
	}
	ti_flush(server);
	return true;
}

/* modifiers changing: in line behind any key still with the input method */
static bool ti_mods_in(struct desk_server *server, const struct wlr_keyboard_modifiers *mods)
{
	if (server->ti_q_n == 0) {
		return false;
	}
	ti_push(server, true, false, false, 0, mods);
	ti_flush(server);
	return true;
}

/* ---- Ctrl+Alt+Del: for when something has gone wrong ----
 *
 * Twice running (each within CAD_GAP_S of the last), the desk's own parts
 * are set right again (cad_reset); three times, the session ends, as LOG
 * OUT on the Control Panel ends it: the windows asked to close first, and
 * one that will not holds it up until it is asked again. Counted as they
 * come, at the door too, and never passed on to a window; acted on once no
 * other has come for CAD_GAP_S (the third at once). desk-ctl does the same
 * from a terminal or another VT, by signal (cad_signal). */
#define CAD_GAP_S 0.8

/* Everything the desk itself has open or in hand put away, every picture it
 * keeps drawn again, the desk laid out afresh, its settings read again and
 * the keyboard and the pointer set up again. The windows are left as they
 * are, and a locked desk stays locked: only the door is drawn again. */
static void cad_reset(struct desk_server *server)
{
	wlr_log(WLR_INFO, "the desk set right again");
	show_stop(server, true);
	reset_cursor_mode(server);
	server->cc_grab = -1;
	cal_drop(server);
	memo_drop(server);
	agd_drop(server);
	binder_close(server);
	if (post_up(server)) {
		post_toggle(server);
	}
	if (console_up(server)) {
		console_toggle(server);
	}
	if (shelf_up(server)) {
		shelf_toggle(server);
	}
	drawer_move_to(server, 0.0);
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->scene_tree && !view->unmanaged) {
			view->shadow.w = view->shadow.h = 0;
			view_sync_shadow(view);
		}
	}
	pictures_forget(server);
	desk_lock_forget();
	/* shell.conf read again, as if it had changed */
	server->conf.loaded = false;
	shell_conf_poll(server);
	shell_apply_all(server);
	input_apply(server);
	desk_relayout(server);
	if (locked(server)) {
		paint_lock(server);
	} else if (!wl_list_empty(&server->toplevels)) {
		struct desk_toplevel *top = wl_container_of(server->toplevels.next, top, link);
		focus_toplevel(top);
	}
	desk_sound_play(DESK_SND_PAPER_SETTLE, 0.0, 0.8, 0.0);
	server_schedule_frames(server);
}

static int cad_cb(void *data)
{
	struct desk_server *server = data;
	int n = server->cad_n;
	server->cad_n = 0;
	if (n == 2) {
		wlr_log(WLR_INFO, "ctrl+alt+del twice");
		cad_reset(server);
	}
	return 0;
}

static void cad_press(struct desk_server *server)
{
	double now = real_sec();
	server->cad_n = now - server->cad_at <= CAD_GAP_S ? server->cad_n + 1 : 1;
	server->cad_at = now;
	desk_sound_play(DESK_SND_KEY_PRESS, 0.0, 0.7, 0.85 + 0.15 * server->cad_n);
	if (server->cad_n >= 3) {
		server->cad_n = 0;
		if (server->cad_timer) {
			wl_event_source_timer_update(server->cad_timer, 0);
		}
		wlr_log(WLR_INFO, "ctrl+alt+del three times: the session ends");
		end_begin(server, END_LOG_OUT);
		return;
	}
	if (!server->cad_timer) {
		server->cad_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), cad_cb, server);
	}
	if (server->cad_timer) {
		wl_event_source_timer_update(server->cad_timer, (int)(CAD_GAP_S * 1000.0));
	}
}

/* desk-ctl's signals, USR1 for a reset and USR2 for the end of the session,
 * and TERM, HUP and INT -- logind ending the session for a restart or a
 * power-off, ^C on a desk in a window -- which end the desk the way a log
 * out finally does, at once (the session is going whatever the windows
 * say), so that what is in hand is still kept (cal_drop, memo_drop) and
 * the programs are let go in order. Written down a pipe by the handler --
 * all a handler may safely do -- and acted on by the loop. A handler does
 * not outlive an exec, so the programs started from here get the signals'
 * usual meaning back; nor is any of them blocked, which an exec would hand
 * down. */
static int g_sig_pipe[2] = { -1, -1 };
static pid_t g_sig_pid;

static void sig_note(int sig)
{
	int saved = errno;
	if (getpid() != g_sig_pid) {
		/* a child of the desk's, not yet into its program: the signal is
		 * its own, and does what it would without the handler */
		signal(sig, SIG_DFL);
		raise(sig);
		errno = saved;
		return;
	}
	unsigned char b = (unsigned char)sig;
	ssize_t r = write(g_sig_pipe[1], &b, 1);
	(void)r;
	errno = saved;
}

static int cad_signal(int fd, uint32_t mask, void *data)
{
	(void)mask;
	struct desk_server *server = data;
	unsigned char b;
	while (read(fd, &b, 1) == 1) {
		if (b == SIGUSR1) {
			wlr_log(WLR_INFO, "desk-ctl reset");
			cad_reset(server);
		} else if (b == SIGUSR2) {
			wlr_log(WLR_INFO, "desk-ctl logout: the session ends");
			end_begin(server, END_LOG_OUT);
		} else if ((int)b == SIGRTMIN + 1) {
			/* desk-ctl calculator: the desktop file's way in, from wherever it is opened */
			wlr_log(WLR_INFO, "desk-ctl calculator");
			if (!locked(server)) {
				thing_out(server, thing_named("calculator"));
			}
		} else if (b == SIGTERM || b == SIGHUP || b == SIGINT) {
			wlr_log(WLR_INFO, "%s: the desk ends",
				b == SIGTERM ? "SIGTERM" : b == SIGHUP ? "SIGHUP" : "SIGINT");
			wl_display_terminate(server->wl_display);
		}
	}
	return 0;
}

static void cad_signals(struct desk_server *server)
{
	if (pipe(g_sig_pipe) != 0) {
		return;
	}
	for (int i = 0; i < 2; i++) {
		fcntl(g_sig_pipe[i], F_SETFD, FD_CLOEXEC);
		fcntl(g_sig_pipe[i], F_SETFL, fcntl(g_sig_pipe[i], F_GETFL) | O_NONBLOCK);
	}
	g_sig_pid = getpid();
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = sig_note;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	const int sigs[] = { SIGUSR1, SIGUSR2, SIGTERM, SIGHUP, SIGINT, SIGRTMIN + 1 };
	for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
		/* an ending the desk was started ignoring (nohup, a shell's
		 * background) stays ignored: that was asked for */
		struct sigaction was;
		if (sigs[i] != SIGUSR1 && sigs[i] != SIGUSR2
			&& sigaction(sigs[i], NULL, &was) == 0 && was.sa_handler == SIG_IGN) {
			continue;
		}
		sigaction(sigs[i], &sa, NULL);
	}
	wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display), g_sig_pipe[0],
		WL_EVENT_READABLE, cad_signal, server);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
	struct desk_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct desk_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;
	uint32_t keycode = event->keycode + 8;
	/* a key that wakes the screen, or comes before it can have been seen,
	 * is the waking's, down and up */
	if (!input_seen(server, "a key")) {
		server->wake_key = event->state == WL_KEYBOARD_KEY_STATE_PRESSED ? keycode : 0;
		return;
	}
	if (server->wake_key == keycode && event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		server->wake_key = 0;
		return;
	}
	desk_sound_awake();   /* somebody at the desk: the speakers kept ready */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);
	/* the key's first keysym (NoSymbol for a key without one): what the
	 * lock, the show, the screenshot, the brightness, volume and media keys
	 * and the VTs go by */
	xkb_keysym_t sym0 = nsyms > 0 ? syms[0] : XKB_KEY_NoSymbol;
	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	/* Ctrl+Alt+F1 and so on: another VT, whatever is going on here, the
	 * screen lock included */
	if (sym0 >= XKB_KEY_XF86Switch_VT_1 && sym0 <= XKB_KEY_XF86Switch_VT_12) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && server->session) {
			wlr_session_change_vt(server->session,
				(unsigned)(sym0 - XKB_KEY_XF86Switch_VT_1 + 1));
		}
		return;
	}
	/* Esc stops a show, whatever has the keys -- and its letting go is the
	 * show's too */
	if (sym0 == XKB_KEY_Escape) {
		bool down = event->state == WL_KEYBOARD_KEY_STATE_PRESSED;
		if (server->show && down
			&& !(modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))) {
			show_stop(server, true);
			server->show_esc = true;
			return;
		}
		if (server->show_esc && !down) {
			server->show_esc = false;
			return;
		}
	}
	/* Ctrl+Alt+Del: counted, at the door too, and nobody else's */
	if ((sym0 == XKB_KEY_Delete || sym0 == XKB_KEY_KP_Delete)
		&& (modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
			== (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			cad_press(server);
		}
		return;
	}
	/* the lock takes every key, one with no keysym too: only a laptop's
	 * brightness keys work even at the door */
	if (locked(server) && sym0 != XKB_KEY_XF86MonBrightnessUp
		&& sym0 != XKB_KEY_XF86MonBrightnessDown) {
		lock_key(server, keyboard->wlr_keyboard->xkb_state, event->keycode, sym0, modifiers,
			event->state == WL_KEYBOARD_KEY_STATE_RELEASED);
		return;
	}
	/* A window that asked for every key has them (keys_asked): only the
	 * VTs, Ctrl+Alt+Del, the door and a laptop's brightness keys stay the
	 * desk's -- and Super+Esc, which gives it the rest back. */
	bool down = event->state == WL_KEYBOARD_KEY_STATE_PRESSED;
	if (server->keys_back_key == keycode && !down) {
		server->keys_back_key = 0;
		return;
	}
	struct wlr_keyboard_shortcuts_inhibitor_v1 *asked = server->sw_on || server->snip_on
		? NULL : keys_asked(server);
	if (asked && down && sym0 == XKB_KEY_Escape && (modifiers & WLR_MODIFIER_LOGO)) {
		wlr_keyboard_shortcuts_inhibitor_v1_deactivate(asked);
		server->keys_back_key = keycode;
		wlr_log(WLR_INFO, "Super+Esc: the keys are the desk's again");
		return;
	}
	/* Alt+Tab's windows spread out: Tab, the arrows, Enter and Esc are theirs */
	if (server->sw_on && switcher_key(server, sym0, modifiers,
			event->state == WL_KEYBOARD_KEY_STATE_PRESSED)) {
		return;
	}
	/* the camera up: Esc puts it down, Enter or the space bar is its
	 * shutter button (not once the shutter has gone off), and no other key
	 * goes anywhere meanwhile */
	if (server->snip_on) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && !server->snip_shot
				&& !server->snip_freezing && server->snip_down_at <= 0.0) {
			if (sym0 == XKB_KEY_Escape) {
				snip_end(server, false);
			} else if (sym0 == XKB_KEY_Return || sym0 == XKB_KEY_KP_Enter
				|| sym0 == XKB_KEY_space) {
				snip_work(server, DESK_FINDER_SHUTTER);
			}
		}
		return;
	}
	/* Print takes a picture of an area drawn out, Ctrl+Print of the whole
	 * screen, Alt+Print of the window in front, whatever is in your hand;
	 * Shift+Print puts a tape in the camera, for a film; and a film being
	 * made, any of them stops it */
	if (!asked && (sym0 == XKB_KEY_Print || sym0 == XKB_KEY_Sys_Req)) {
		/* (with Alt held, most keymaps call the key SysRq) */
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			if (server->rec) {
				rec_end(server);
			} else if (modifiers & WLR_MODIFIER_ALT) {
				screenshot_take(server, true);
			} else if (modifiers & WLR_MODIFIER_CTRL) {
				screenshot_take(server, false);
			} else {
				snip_begin(server, (modifiers & WLR_MODIFIER_SHIFT) != 0);
			}
		}
		return;
	}
	/* a laptop's brightness keys work whatever is in your hand */
	if (sym0 == XKB_KEY_XF86MonBrightnessUp || sym0 == XKB_KEY_XF86MonBrightnessDown) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			screen_step(server, sym0 == XKB_KEY_XF86MonBrightnessUp ? 1 : -1);
		}
		return;
	}
	/* The keyboard's sound and player keys: under Wayland only the desk
	 * sees them, so they are the desk's, and do what the clock's volume
	 * and its MEDIA keys do -- the mixer and the player are borrowed. */
	if (!asked && (sym0 == XKB_KEY_XF86AudioRaiseVolume || sym0 == XKB_KEY_XF86AudioLowerVolume
		|| sym0 == XKB_KEY_XF86AudioMute)) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			if (sym0 == XKB_KEY_XF86AudioMute) {
				if (desk_meters_set_muted(2)) {
					clock_publish(server);
				}
			} else {
				struct desk_meters m;
				desk_meters_read_volume(&m);
				bool up = sym0 == XKB_KEY_XF86AudioRaiseVolume;
				if (m.have_volume) {
					/* turning it up is wanting to hear it, as on other
					 * desktops; turning it down leaves it muted */
					if (up && m.muted) {
						desk_meters_set_muted(0);
					}
					clock_set_volume(server, m.volume + (up ? 0.05 : -0.05));
				}
			}
		}
		return;
	}
#ifdef DESK_HAVE_POST
	if (!asked && (sym0 == XKB_KEY_XF86AudioPlay || sym0 == XKB_KEY_XF86AudioPause
		|| sym0 == XKB_KEY_XF86AudioNext || sym0 == XKB_KEY_XF86AudioPrev)) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			desk_media_command(sym0 == XKB_KEY_XF86AudioNext ? DESK_MEDIA_NEXT
				: sym0 == XKB_KEY_XF86AudioPrev ? DESK_MEDIA_PREV : DESK_MEDIA_PLAY_PAUSE);
		}
		return;
	}
#endif
	if (agd_has_keys(server) && shelf_up(server)) {
		/* the binder of notes is in: the keys are its */
		agd_key(server, keyboard->wlr_keyboard->xkb_state, event->keycode,
			sym0, modifiers,
			event->state == WL_KEYBOARD_KEY_STATE_RELEASED);
		return;
	}
	if (book_has_keys(server)) {
		/* the book is open: what is typed is written on its line */
		book_key(server, keyboard->wlr_keyboard->xkb_state, event->keycode,
			sym0, modifiers,
			event->state == WL_KEYBOARD_KEY_STATE_RELEASED);
		return;
	}
	if (memo_has_keys(server)) {
		/* the memo pad in your hand: what is typed is written on it */
		memo_key(server, keyboard->wlr_keyboard->xkb_state, event->keycode,
			sym0, modifiers,
			event->state == WL_KEYBOARD_KEY_STATE_RELEASED);
		return;
	}
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->key && (*t)->key(server, event->keycode, sym0, modifiers,
				event->state == WL_KEYBOARD_KEY_STATE_RELEASED)) {
			return;   /* what is typed is keyed on it */
		}
	}
	if (down && !asked) {
		for (int i = 0; i < nsyms; i++) {
			handled = handle_keybinding(server, syms[i], modifiers);
			if (handled) {
				break;
			}
		}
		/* In a second layout -- Arabic, Russian -- the letters are its own,
		 * and Super+L was Super+lam: a shortcut goes by what the key is in
		 * the first layout, at the level the modifiers held choose there. */
		struct xkb_state *st = keyboard->wlr_keyboard->xkb_state;
		if (!handled && (modifiers & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
			&& xkb_state_serialize_layout(st, XKB_STATE_LAYOUT_EFFECTIVE) != 0) {
			const xkb_keysym_t *first;
			int n = xkb_keymap_key_get_syms_by_level(keyboard->wlr_keyboard->keymap, keycode, 0,
				xkb_state_key_get_level(st, keycode, 0), &first);
			for (int i = 0; i < n && !handled; i++) {
				handled = handle_keybinding(server, first[i], modifiers);
			}
		}
	}
	if (!handled) {
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		if (ti_key_in(server, event->keycode, event->state == WL_KEYBOARD_KEY_STATE_PRESSED,
				sym0, modifiers)) {
			return;   /* the input method's first; the window's when it answers */
		}
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}
