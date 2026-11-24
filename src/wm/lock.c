/* Part of wm.c: the screen lock.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static double lock_ease(double t)
{
	t = clampf_d(t, 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

struct lock_args {
	struct desk_lock_look look;
};

static void lock_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct lock_args *a = data;
	desk_draw_lock(cr, w, h, &a->look);
}

static void lock_look(const struct desk_server *server, struct desk_lock_look *lk)
{
	memset(lk, 0, sizeof *lk);
	double now = monotonic_sec();
	lk->name = server->lock_name;
	lk->picture = server->lock_picture;
	lk->typed = server->lock_typed;
	lk->caret = server->lock_caret_on && server->lock_phase == LOCK_SHUT;
	lk->checking = server->lock_phase == LOCK_CHECK;
	lk->caps = server->lock_caps;
	lk->now_s = now;
	lk->steady = 1.0 - g_bounce;
	/* where the moves under way have got to: the eye, the key and the door
	 * are the painter's sums (desk_lock_pose), the ones its films of them
	 * are made with */
	struct desk_lock_times tm;
	memset(&tm, 0, sizeof tm);
	tm.walking = server->lock_walk;
	tm.walk_t = now - server->lock_at;
	tm.checking = server->lock_check_at > 0.0;
	tm.check_t = now - server->lock_check_at;
	tm.lean0 = server->lock_lean0;
	tm.opening = server->lock_phase == LOCK_OPEN;
	tm.open_t = now - server->lock_open_at;
	tm.failing = server->lock_phase != LOCK_OPEN && server->lock_fail_at > 0.0
		&& !server->lock_refused && now >= server->lock_fail_at;
	tm.fail_t = now - server->lock_fail_at;
	tm.leaving = server->lock_leaving;
	tm.leave_t = now - server->lock_leave_at;
	tm.inside = server->lock_inside != NULL;
	desk_lock_pose(&tm, lk);
	if (server->lock_leaving) {
		lk->inside = server->lock_inside;
		lk->caret = false;
		lk->typed = 0;
	}
	lk->inside_view = server->lock_from == LOCK_FROM_CONSOLE ? DESK_VIEW_CONSOLE
		: DESK_VIEW_SHELF;
	/* what it is like out there: the hour, the weather, the day */
	desk_occasion_mood(&lk->mood);
	if (server->lock_phase == LOCK_OPEN) {
		lk->inside = server->lock_inside;
		lk->checking = false;
	}
}

/* How often what moves at the door is drawn: thirty times a second, and
 * fifteen on Low; at Ultra every frame. In 2D it does not move at all
 * (paint_lock_fx). */
static double lock_fx_period(const struct desk_server *server)
{
	(void)server;
	enum desk_gfx g = desk_gfx_level();
	return g == DESK_GFX_ULTRA ? 0.0 : g <= DESK_GFX_LOW ? 1.0 / 15.0 : 1.0 / 30.0;
}

/* what moves out at the door, on its own sheet over it */
static void paint_lock_fx(struct desk_server *server, const struct desk_lock_look *lk,
	double op)
{
	int w = server->desk_w, h = server->desk_h;
	if (!desk_lock_fx_wanted(&lk->mood) || lk->push >= 0.999 || server->lock_film
		|| gfx_flat()) {
		if (server->lock_fx) {
			wlr_scene_node_set_enabled(&server->lock_fx->node, false);
		}
		return;
	}
	int ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	struct desk_buf *buf = buf_create(ow, oh);
	cairo_save(buf->cr);
	cairo_scale(buf->cr, (double)ow / w, (double)oh / h);
	desk_draw_lock_fx(buf->cr, w, h, lk);
	cairo_restore(buf->cr);
	scene_put(&server->lock_fx, server->layer_lock, buf, 0, 0, NULL);
	if (server->lock_fx) {
		wlr_scene_buffer_set_dest_size(server->lock_fx, w, h);
		server->lock_fx->point_accepts_input = shadow_accepts;
		wlr_scene_node_raise_to_top(&server->lock_fx->node);
		wlr_scene_buffer_set_opacity(server->lock_fx, (float)op);
		wlr_scene_node_set_enabled(&server->lock_fx->node, true);
	}
	server->lock_fx_at = real_sec();
}

static void paint_lock(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (!locked(server) || w < 8 || h < 8) {
		return;
	}
	if (server->lock_film) {
		/* turning to the bookcase first: the door is not down yet, but
		 * nothing behind it has the keys any more */
		wlr_scene_node_set_enabled(&server->layer_lock->node, false);
		return;
	}
	/* black over every output, under the door, so that a second screen
	 * shows nothing of what is behind it either */
	struct wlr_box all;
	wlr_output_layout_get_box(server->output_layout, NULL, &all);
	if (!server->lock_cover) {
		const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		server->lock_cover = wlr_scene_rect_create(server->layer_lock, 1, 1, black);
	}
	wlr_scene_rect_set_size(server->lock_cover, all.width > w ? all.width : w,
		all.height > h ? all.height : h);
	wlr_scene_node_set_position(&server->lock_cover->node, all.x < 0 ? all.x : 0,
		all.y < 0 ? all.y : 0);
	struct lock_args a;
	lock_look(server, &a.look);
	int ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	if (desk_lock_filmed(w, h, &a.look) && (double)ow * oh > LOCK_FILM_PX) {
		/* a frame of a film made ahead, no finer than it was made: laid
		 * at no more than LOCK_FILM_PX and scaled up with the rest of the
		 * screen, not laid at four times that on a 4K screen */
		double f = sqrt(LOCK_FILM_PX / ((double)ow * oh));
		ow = (int)(ow * f);
		oh = (int)(oh * f);
	}
	struct desk_buf *buf = buf_create(ow, oh);
	cairo_save(buf->cr);
	cairo_scale(buf->cr, (double)ow / w, (double)oh / h);
	lock_draw(buf->cr, w, h, &a);
	cairo_restore(buf->cr);
	scene_put(&server->lock_node, server->layer_lock, buf, 0, 0, NULL);
	if (server->lock_node) {
		wlr_scene_buffer_set_dest_size(server->lock_node, w, h);
		wlr_scene_node_raise_to_top(&server->lock_node->node);
	}
	/* coming down, it is seen through; going, it fades off where you were
	 * when there is no going in */
	double now = monotonic_sec(), op = 1.0;
	if (server->lock_fade_in) {
		op = clampf_d((now - server->lock_at) / LOCK_FADE_S, 0.0, 1.0);
	}
	if (server->lock_phase == LOCK_OPEN && !server->lock_inside) {
		op = 1.0 - clampf_d((now - server->lock_open_at - DESK_LOCK_SWING1 + 0.15) / LOCK_FADE_S,
			0.0, 1.0);
	}
	if (server->lock_node) {
		wlr_scene_buffer_set_opacity(server->lock_node, (float)op);
	}
	paint_lock_fx(server, &a.look, op);
	wlr_scene_node_set_enabled(&server->lock_cover->node, op >= 0.999);
	wlr_scene_node_set_enabled(&server->layer_lock->node, true);
}

static int lock_caret_cb(void *data)
{
	struct desk_server *server = data;
	/* dark, there is nobody to blink at: it starts again on waking */
	if (server->lock_phase != LOCK_SHUT || server->dark) {
		return 0;
	}
	server->lock_caret_on = !server->lock_caret_on;
	paint_lock(server);
	server_schedule_frames(server);
	wl_event_source_timer_update(server->lock_timer, LOCK_CARET_MS);
	return 0;
}

/* zeroed in a way the compiler may not leave out */
static void lock_zero(void *p, size_t n)
{
	volatile unsigned char *q = p;
	while (n--) {
		*q++ = 0;
	}
}

static void lock_wipe(struct desk_server *server)
{
	lock_zero(server->lock_pass, sizeof server->lock_pass);
	server->lock_len = 0;
}

/* What the room looks like from in front of the bookcase, and from in
 * front of the Control Panel: the pictures the door opens on to, and is
 * walked out of. */
static cairo_surface_t *lock_snap_shelf(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h, ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ow, oh);
	cairo_t *c = cairo_create(s);
	cairo_scale(c, (double)ow / w, (double)oh / h);
	shelf_dress_today();
	desk_draw_shelf(c, w, h, -1, 0.0, server->shelf_binder);
	desk_draw_shelf_gadgets(c, w, h, time(NULL));
	cairo_destroy(c);
	return s;
}

static cairo_surface_t *lock_snap_console(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h, ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ow, oh);
	cairo_t *c = cairo_create(s);
	cairo_scale(c, (double)ow / w, (double)oh / h);
	struct desk_console_state st;
	console_state(server, &st);
	desk_draw_console(c, w, h, &st, -1);
	cairo_destroy(c);
	return s;
}

static void snip_stop(struct desk_server *server);
static void rec_end(struct desk_server *server);

/* whose door it is: the name on the board, and the picture */
static void lock_who(struct desk_server *server)
{
	if (!server->lock_name[0]) {
		char login[64];
		desk_lock_user(login, sizeof login, server->lock_name, sizeof server->lock_name);
	}
	if (!server->lock_picture && !server->lock_picture_tried) {
		server->lock_picture_tried = true;
		server->lock_picture = desk_lock_picture();
	}
}

/* The room to go into when let in: where you went out of it if you did --
 * the desk by way of the bookcase, the bookcase, the Control Panel -- and
 * otherwise, the bookcase if its camera can sit you down at the desk from
 * there, or where you already were. */
static int lock_open_from(const struct desk_server *server)
{
	int from = server->lock_from;
	if (from == LOCK_FROM_ELSEWHERE) {
		from = !shelf_up(server) && !console_up(server) && !post_up(server)
			&& server->drawer_t < 0.004 ? LOCK_FROM_DESK
			: shelf_up(server) && server->shelf_t > 0.999 ? LOCK_FROM_SHELF
			: console_up(server) && server->cc_t > 0.999 ? LOCK_FROM_CONSOLE
			: LOCK_FROM_ELSEWHERE;
	}
	return from;
}

/* and which of the room's pictures it opens on: DESK_VIEW_*, or -1 for
 * none (the door swings in and fades, on where you were) */
static int lock_open_view(const struct desk_server *server, int from)
{
	if (!desk_scene_views_ready(server->desk_w, server->desk_h)) {
		return -1;
	}
	return from == LOCK_FROM_DESK || from == LOCK_FROM_SHELF ? DESK_VIEW_SHELF
		: from == LOCK_FROM_CONSOLE && console_up(server) ? DESK_VIEW_CONSOLE : -1;
}

/* The door's moves, filmed ahead (desk_lock_films) so that they are not
 * traced as they go, coarse: unlocked, the ways out to it -- the walk up,
 * and out of the room from the bookcase or, while it is up, the Control
 * Panel; locked, and standing at it, the ways back in -- leaning in to the
 * key, told no, let in and through to where you were. Asked again every
 * few seconds, which costs nothing while the door is the same door: the
 * painter makes them again when it is not (the hour, the weather, the
 * screen, the level). Not while a move is under way: its film is in use. */
#define LOCK_FILMS_AFTER_S 60.0   /* after the lamp is up, before the door is filmed ahead */

static void lock_films(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (desk_gfx_level() < DESK_GFX_MEDIUM || !server->icons_shown || w < 8 || h < 8) {
		if (server->lock_films_asked != -1) {
			desk_lock_films(w, h, NULL, 0);
			server->lock_films_asked = -1;
		}
		return;
	}
	/* Not in the first minute after the lamp is up, unless the door is wanted before that (it is
	 * traced as it goes meanwhile): every core for three or four seconds, while the rest of the
	 * room is still being made (2026-11-12) */
	if (!locked(server) && (server->boot_on || server->lamp_up_at <= 0.0
			|| monotonic_sec() - server->lamp_up_at < LOCK_FILMS_AFTER_S)) {
		return;
	}
	if (locked(server) && (server->lock_walk || server->lock_leaving || server->lock_film
			|| server->lock_phase != LOCK_SHUT || server->lock_check_at > 0.0
			|| server->lock_fail_at > 0.0)) {
		return;
	}
	bool views = desk_scene_views_ready(w, h);
	unsigned moves;
	int view = -1;   /* the room a film opens on */
	if (!locked(server)) {
		moves = 1u << DESK_LOCK_MOVE_WALK;
		if (views) {
			moves |= 1u << DESK_LOCK_MOVE_OUT;
			view = console_up(server) ? DESK_VIEW_CONSOLE : DESK_VIEW_SHELF;
		}
	} else {
		moves = (1u << DESK_LOCK_MOVE_LEAN) | (1u << DESK_LOCK_MOVE_FAIL)
			| (1u << DESK_LOCK_MOVE_OPEN);
		view = lock_open_view(server, lock_open_from(server));
	}
	int asked = (locked(server) ? 8 : 0) + view + 1;
	double now = monotonic_sec();
	if (asked == server->lock_films_asked && now - server->lock_films_at < 5.0) {
		return;
	}
	server->lock_films_at = now;
	server->lock_films_asked = asked;
	lock_who(server);
	struct desk_lock_look lk;
	memset(&lk, 0, sizeof lk);
	lk.name = server->lock_name;
	lk.picture = server->lock_picture;
	lk.steady = 1.0 - g_bounce;
	lk.inside_view = view;
	desk_occasion_mood(&lk.mood);
	if (desk_lock_films(w, h, &lk, moves) && view >= 0) {
		/* the room it opens on, as it is now: wanted only when a film of
		 * it is to be made */
		lk.inside = view == DESK_VIEW_CONSOLE ? lock_snap_console(server)
			: lock_snap_shelf(server);
		desk_lock_films(w, h, &lk, moves);
		cairo_surface_destroy(lk.inside);
	}
}

/* Locked. how: at once (logind: the machine may be going to sleep); walking
 * up to the door from a step away; turning to the bookcase first and then
 * going out (lock_tick); or straight out of the room, which inside shows
 * as it is now. from says where you were, to come back to. */
static void lock_begin(struct desk_server *server, int how, cairo_surface_t *inside, int from)
{
	if (locked(server)) {
		if (inside) {
			cairo_surface_destroy(inside);
		}
		return;
	}
	wlr_log(WLR_INFO, "lock: locked");
	/* nothing half done left behind the door: a film being made ends
	 * there too */
	snip_stop(server);
	rec_end(server);
	switcher_close(server);
	double now = monotonic_sec();
	server->lock_phase = LOCK_SHUT;
	server->lock_at = now;
	server->lock_fade_in = how == LOCK_COME_WALK;
	server->lock_walk = how == LOCK_COME_WALK;
	server->lock_film = how == LOCK_COME_FILM;
	server->lock_leaving = how == LOCK_COME_OUT;
	server->lock_leave_at = now;
	server->lock_from = from;
	if (server->lock_inside) {
		cairo_surface_destroy(server->lock_inside);
	}
	server->lock_inside = inside;
	server->lock_fail_at = 0.0;
	server->lock_check_at = 0.0;
	server->lock_lean0 = 0.0;
	server->lock_refused = false;
	server->lock_typed = 0;
	server->lock_caret_on = true;
	lock_wipe(server);
	lock_who(server);
	/* nobody behind the door has the keys or the pointer any more */
	reset_cursor_mode(server);
	server->cc_grab = -1;
	wlr_seat_keyboard_notify_clear_focus(server->seat);
	wlr_seat_pointer_notify_clear_focus(server->seat);
	cursor_image(server, "default");
	server->lock_hand = false;
	if (!server->lock_timer) {
		server->lock_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), lock_caret_cb, server);
	}
	if (server->lock_timer) {
		wl_event_source_timer_update(server->lock_timer, LOCK_CARET_MS);
	}
	if (how == LOCK_COME_NOW || how == LOCK_COME_WALK) {
		desk_sound_play(DESK_SND_LOCK_CLUNK, 0.0, 0.5, 0.0);
	}
	desk_lock_hint(true);
	paint_lock(server);
	server_schedule_frames(server);
	/* the door, lit with nobody at it, goes dark sooner than the desk */
	idle_rearm(server);
}

/* Out of the room: the door swings to, the key goes back round. */
static void lock_go_out(struct desk_server *server, cairo_surface_t *inside)
{
	double now = monotonic_sec();
	if (server->lock_inside) {
		cairo_surface_destroy(server->lock_inside);
	}
	server->lock_inside = inside;
	server->lock_film = false;
	server->lock_leaving = true;
	server->lock_leave_at = now;
	desk_sound_play_in(DESK_SND_POST_CLOSE, 0.1, 0.8, 0.0, real_secs(DESK_LOCK_OUT_SHUT1 - 0.06));
	desk_sound_play_in(DESK_SND_LOCK_CLUNK, 0.3, 0.9, 0.0,
		real_secs(DESK_LOCK_OUT_TURN + DESK_LOCK_TURN_S * 0.85));
}

/* Super+L, or the LOCK button: lock, the way you let yourself in run
 * backwards -- from the desk, turn to the bookcase and go out of the room
 * by the front door; from in front of the bookcase or the Control Panel,
 * go out from there. Anywhere else, the door comes down over it and you
 * walk up to it. */
static void lock_request(struct desk_server *server)
{
	if (locked(server)) {
		return;
	}
	if (desk_gfx_level() <= DESK_GFX_LOW) {
		/* Low and 2D: no walk out to it, traced as it goes -- the door,
		 * at once */
		lock_begin(server, LOCK_COME_NOW, NULL, LOCK_FROM_ELSEWHERE);
		return;
	}
	bool views = desk_scene_views_ready(server->desk_w, server->desk_h);
	if (views && console_up(server) && !server->cc_moving && server->cc_t > 0.999
		&& !console_dying(server)) {
		cairo_surface_t *in = lock_snap_console(server);
		/* going out: the bolt's clunk comes when the key turns
		 * (lock_go_out), not once more now */
		lock_begin(server, LOCK_COME_OUT, NULL, LOCK_FROM_CONSOLE);
		lock_go_out(server, in);
		paint_lock(server);
		return;
	}
	if (views && shelf_up(server) && !server->shelf_moving && server->shelf_t > 0.999) {
		cairo_surface_t *in = lock_snap_shelf(server);
		lock_begin(server, LOCK_COME_OUT, NULL, LOCK_FROM_SHELF);
		lock_go_out(server, in);
		paint_lock(server);
		return;
	}
	if (views && !shelf_up(server) && !console_up(server) && !post_up(server)
		&& server->drawer_t < 0.004) {
		shelf_toggle(server);
		if (server->shelf_moving || server->shelf_t > 0.999) {
			lock_begin(server, LOCK_COME_FILM, NULL, LOCK_FROM_DESK);
			return;
		}
	}
	lock_begin(server, LOCK_COME_WALK, NULL, LOCK_FROM_ELSEWHERE);
}

/* The door is gone: you are in. */
static void lock_finish(struct desk_server *server)
{
	bool into_shelf = server->lock_inside != NULL && server->lock_from_desk;
	server->lock_phase = LOCK_NONE;
	server->lock_film = false;
	server->lock_leaving = false;
	lock_wipe(server);
	if (server->lock_inside) {
		cairo_surface_destroy(server->lock_inside);
		server->lock_inside = NULL;
	}
	if (into_shelf) {
		/* standing in the room, looking at the bookcase, as the last of
		 * the door showed it: and now sit down at the desk */
		server->shelf_from = server->shelf_to = server->shelf_t = 1.0;
		server->shelf_moving = 0;
		paint_shelf(server);
	}
	wlr_scene_node_set_enabled(&server->layer_lock->node, false);
	if (server->lock_fx) {
		wlr_scene_node_set_enabled(&server->lock_fx->node, false);
	}
	desk_lock_forget();
	desk_lock_hint(false);
	if (into_shelf) {
		shelf_toggle(server);
	}
	/* let in on a day kept: the clock says so */
	show_for_today(server);
	if (!wl_list_empty(&server->toplevels)) {
		struct desk_toplevel *top = wl_container_of(server->toplevels.next, top, link);
		focus_toplevel(top);
	}
	server_schedule_frames(server);
	wlr_log(WLR_INFO, "lock: in");
}

/* Let in: the key goes round, you stand back as the door swings in, and
 * through it into the room -- once the key has been seen tried. */
static void lock_open(struct desk_server *server)
{
	if (!locked(server) || server->lock_phase == LOCK_OPEN) {
		return;
	}
	double now = monotonic_sec();
	server->lock_phase = LOCK_OPEN;
	things_text_wanted(server, false);
	server->lock_refused = false;
	server->lock_fail_at = 0.0;
	server->lock_open_at = now;
	if (server->lock_check_at > 0.0 && server->lock_check_at + LOCK_TRIED_S > now) {
		server->lock_open_at = server->lock_check_at + LOCK_TRIED_S;
	}
	double at = server->lock_open_at;
	lock_wipe(server);
	int from = lock_open_from(server);
	int view = lock_open_view(server, from);
	server->lock_film = false;
	server->lock_leaving = false;
	server->lock_from = from;
	server->lock_from_desk = from == LOCK_FROM_DESK;
	if (server->lock_inside) {
		cairo_surface_destroy(server->lock_inside);
		server->lock_inside = NULL;
	}
	if (view == DESK_VIEW_SHELF) {
		server->lock_inside = lock_snap_shelf(server);
	} else if (view == DESK_VIEW_CONSOLE) {
		server->lock_inside = lock_snap_console(server);
	}
	desk_sound_play_in(DESK_SND_LOCK_CLUNK, 0.3, 0.9, 0.0,
		real_secs(at - now + DESK_LOCK_TURN_S * 0.85));
	desk_sound_play_in(DESK_SND_POST_OPEN, 0.1, 0.8, 0.0, real_secs(at - now + DESK_LOCK_SWING0));
	paint_lock(server);
	server_schedule_frames(server);
}

static void lock_checked(bool ok, void *data)
{
	struct desk_server *server = data;
	if (server->lock_phase != LOCK_CHECK) {
		return;
	}
	if (ok) {
		wlr_log(WLR_INFO, "lock: the password is right");
		lock_open(server);
		return;
	}
	wlr_log(WLR_INFO, "lock: the password is wrong");
	/* told at once, perhaps; shown once the key has been seen tried
	 * (lock_tick) */
	double now = monotonic_sec();
	server->lock_refused = true;
	server->lock_fail_at = now;
	if (server->lock_check_at + LOCK_TRIED_S > now) {
		server->lock_fail_at = server->lock_check_at + LOCK_TRIED_S;
	}
	paint_lock(server);
	server_schedule_frames(server);
}

/* The key will not go round: back to waiting, leaning back out. */
static void lock_refuse(struct desk_server *server)
{
	server->lock_refused = false;
	server->lock_phase = LOCK_SHUT;
	server->lock_typed = 0;
	server->lock_caret_on = true;
	desk_sound_play(DESK_SND_TOGGLE, 0.3, 0.8, 0.6);
	desk_sound_play_in(DESK_SND_KEY_IN, 0.3, 0.5, 0.8, real_secs(0.08));
	if (server->lock_timer) {
		wl_event_source_timer_update(server->lock_timer, LOCK_CARET_MS);
	}
}

/* Return, or the key clicked: lean in to it, try it, and ask. */
static void lock_submit(struct desk_server *server)
{
	if (server->lock_phase != LOCK_SHUT || server->lock_film || server->lock_leaving) {
		return;
	}
	double now = monotonic_sec();
	struct desk_lock_look lk;
	lock_look(server, &lk);
	server->lock_lean0 = clampf_d(lk.lean, 0.0, 1.0);
	server->lock_phase = LOCK_CHECK;
	server->lock_check_at = now;
	if (server->lock_hand) {
		server->lock_hand = false;
		cursor_image(server, "default");
	}
	server->lock_fail_at = 0.0;
	server->lock_refused = false;
	desk_sound_play_in(DESK_SND_KEY_IN, 0.3, 0.55, 0.0, real_secs(DESK_LOCK_LEAN_S * 0.75));
	if (!desk_lock_check(wl_display_get_event_loop(server->wl_display),
			server->lock_pass, lock_checked, server)) {
		lock_wipe(server);
		lock_checked(false, server);
		return;
	}
	lock_wipe(server);
	paint_lock(server);
	server_schedule_frames(server);
}

static void lock_key(struct desk_server *server, struct xkb_state *xkb, uint32_t keycode,
	uint32_t sym, uint32_t mods, bool released)
{
	bool caps = (mods & WLR_MODIFIER_CAPS) != 0;
	if (caps != server->lock_caps) {
		server->lock_caps = caps;
		paint_lock(server);
		server_schedule_frames(server);
	}
	if (released || server->lock_phase != LOCK_SHUT || server->lock_film
		|| server->lock_leaving) {
		return;   /* still going out of the room: the key is not in your hand */
	}
	char utf8[8] = "";
	xkb_state_key_get_utf8(xkb, keycode + 8, utf8, sizeof utf8);
	switch (sym) {
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		lock_submit(server);
		return;
	case XKB_KEY_BackSpace:
		if (server->lock_len > 0) {
			server->lock_len = utf8_prev(server->lock_pass, server->lock_len);
			server->lock_pass[server->lock_len] = '\0';
			server->lock_typed = server->lock_typed > 0 ? server->lock_typed - 1 : 0;
		}
		break;
	case XKB_KEY_Escape:
		lock_wipe(server);
		server->lock_typed = 0;
		break;
	default:
		if ((mods & WLR_MODIFIER_CTRL) && (sym == XKB_KEY_u || sym == XKB_KEY_U)) {
			lock_wipe(server);
			server->lock_typed = 0;
			break;
		}
		if ((unsigned char)utf8[0] < 0x20 || utf8[0] == 0x7f
			|| (mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))) {
			return;
		}
		size_t k = strlen(utf8);
		if (server->lock_len + (int)k >= (int)sizeof server->lock_pass - 1) {
			return;
		}
		memcpy(server->lock_pass + server->lock_len, utf8, k);
		server->lock_len += (int)k;
		server->lock_pass[server->lock_len] = '\0';
		server->lock_typed++;
		desk_sound_play(DESK_SND_KEY_PRESS, 0.0, 0.25, 1.3);
		break;
	}
	server->lock_caret_on = true;
	if (server->lock_timer) {
		wl_event_source_timer_update(server->lock_timer, LOCK_CARET_MS);
	}
	paint_lock(server);
	server_schedule_frames(server);
}

/* Once a frame while something about the door is moving. */
static void lock_tick(struct desk_server *server)
{
	if (!locked(server)) {
		return;
	}
	double now = monotonic_sec();
	bool moving = false;
	if (server->lock_fade_in) {
		if (now - server->lock_at >= LOCK_FADE_S) {
			server->lock_fade_in = false;
		}
		moving = true;
	}
	if (server->lock_walk) {
		if (now - server->lock_at >= DESK_LOCK_WALK_S) {
			server->lock_walk = false;
		}
		moving = true;
	}
	if (server->lock_film) {
		if (server->shelf_t > 0.999 && !server->shelf_moving) {
			/* in front of the bookcase: now out of the room */
			lock_go_out(server, lock_snap_shelf(server));
		} else if (now - server->lock_at > LOCK_FILM_MAX_S) {
			/* it never got there: the door comes down over it instead */
			server->lock_film = false;
			server->lock_from = LOCK_FROM_ELSEWHERE;
			server->lock_walk = server->lock_fade_in = true;
			server->lock_at = now;
		}
		moving = true;
	}
	if (server->lock_leaving) {
		if (now - server->lock_leave_at >= DESK_LOCK_OUT_END) {
			server->lock_leaving = false;
			server->lock_caret_on = true;
			if (server->lock_inside) {
				cairo_surface_destroy(server->lock_inside);
				server->lock_inside = NULL;
			}
		}
		moving = true;
	}
	if (server->lock_refused && now >= server->lock_fail_at) {
		lock_refuse(server);
	}
	if (server->lock_fail_at > 0.0 && !server->lock_refused) {
		if (now - server->lock_fail_at >= DESK_LOCK_FAIL_S) {
			/* stood back from the door again, as it was */
			server->lock_fail_at = 0.0;
			server->lock_check_at = 0.0;
		}
		moving = true;
	}
	if (server->lock_phase == LOCK_CHECK) {
		moving = true;   /* the light running along the slot */
	}
	if (desk_lock_settling()) {
		moving = true;   /* the door, finely, as soon as it is done */
	}
	if (server->lock_phase == LOCK_OPEN) {
		double end = server->lock_inside ? DESK_LOCK_PUSH1 : DESK_LOCK_SWING1 + 0.15 + LOCK_FADE_S;
		if (now - server->lock_open_at >= end) {
			lock_finish(server);
			return;
		}
		moving = true;
	}
	if (moving) {
		paint_lock(server);
		server_schedule_frames(server);
	} else if (server->lock_phase != LOCK_NONE) {
		/* only the rain, the snow, the lights: their sheet, thirty
		 * times a second (lock_fx_period), the door left as it is */
		struct desk_lock_look lk;
		lock_look(server, &lk);
		if (desk_lock_fx_wanted(&lk.mood) && !gfx_flat()) {
			if (real_sec() - server->lock_fx_at >= lock_fx_period(server)) {
				double op = server->lock_node ? server->lock_node->opacity : 1.0;
				paint_lock_fx(server, &lk, op);
			}
			server_schedule_frames(server);
		}
	}
}

static void lock_cb(void *data)
{
	/* the machine may be about to sleep: down at once, no fading */
	lock_begin(data, LOCK_COME_NOW, NULL, LOCK_FROM_ELSEWHERE);
}

static void unlock_cb(void *data)
{
	struct desk_server *server = data;
	if (locked(server)) {
		lock_open(server);
	}
}

/* ---- the screen going dark ---------------------------------------------
 *
 * Nobody has touched a key, the pointer or the screen for a while
 * (shell.conf screen_off; ten minutes unless it says otherwise): the front
 * door comes down at once -- unless lock_on_screen_off says not to -- and
 * the screen goes dark. Anything touched brings it back, and does nothing
 * else: a click on the black is not a click on whatever was under it, and
 * the key that woke it is not typed at the door. A program playing a film
 * holds it up while its window is out to be seen (idle-inhibit); programs
 * that do something of their own after a while, swayidle, are told when
 * anybody last touched anything (ext-idle-notify); and wlopm turns the
 * screens off and on through the same machinery (wlr-output-power-
 * management). DESK_IDLE_TEST_S gives the wait in seconds, for trying it.
 *
 * Dark is the screen turned off: the output disabled, no frames, nothing
 * drawn. Nested, the screen is a window on another desktop, which is not
 * ours to turn off -- the X11 backend would take the window away, and with
 * it the pointer that is to wake it -- so it is covered black, shown once,
 * and nothing more is drawn into it until somebody comes back. */

#define IDLE_AT_DOOR_MS 60000   /* the door, lit, with nobody at it */
#define WAKE_BLIND_S 0.8        /* woken: a press before this is not for anything */
