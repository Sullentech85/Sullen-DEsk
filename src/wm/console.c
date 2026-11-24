/* Part of wm.c: the Control Panel.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* ================================================================
 * The control console
 *
 * It stands at the back right of the desk and it is always there. Opening
 * it is not a window appearing: it is turning your head to the machine, so
 * the camera goes over to it and the panel fills the frame. Then the set
 * warms up -- the lamps come on one after another, the needles lift off
 * their stops, the tube strikes -- because a valve set does not come on,
 * it comes round.
 * ================================================================ */

/* Is the set in the frame at all? */
static bool console_up(const struct desk_server *server)
{
	return server->cc_t > 0.0005 || server->cc_moving;
}

/* How far the desk is turned down. The machine has no backlight of its own,
 * so the fader is the lamp over the desk. */
static void console_dim_apply(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	double a = (1.0 - clampf_d(server->cc_bright, 0.0, 1.0)) * 0.62;
	/* Leaning into the set, you are looking at a thing that lights itself,
	 * so the room going down barely reaches it -- but it has to reach it a
	 * little, or the fader would feel like it was not connected. */
	a *= 1.0 - 0.72 * clampf_d(server->cc_t, 0.0, 1.0);
	/* and the machine going out takes the room with it, all the way */
	if (server->cc_pressed >= 0) {
		a = fmax(a, smoothstep01(0.10, 1.00, server->cc_dying));
	}
	if (w < 8 || h < 8) {
		return;
	}
	if (!server->cc_dim) {
		/* one flat colour, stretched over the screen by the scene graph.
		 * It used to be a screenful of it: thirty two megabytes of the
		 * same three numbers on a large display. */
		struct desk_buf *buf = buf_create(16, 16);
		cairo_set_source_rgb(buf->cr, 0.004, 0.004, 0.006);
		cairo_paint(buf->cr);
		scene_put(&server->cc_dim, server->layer_top, buf, 0, 0, NULL);
		if (server->cc_dim) {
			/* it is the light in the room, not something you can click */
			server->cc_dim->point_accepts_input = shadow_accepts;
		}
	}
	if (!server->cc_dim) {
		return;
	}
	wlr_scene_buffer_set_dest_size(server->cc_dim, w, h);
	wlr_scene_buffer_set_opacity(server->cc_dim, (float)a);
	wlr_scene_node_set_enabled(&server->cc_dim->node, a > 0.004);
}

static void console_live(struct desk_server *server, bool on);

/* The screen's light, sent to the machine: no more often than it can take
 * it, so a hand running the fader along is one turn now and then and the
 * last one when it stops. */
static int screen_send_cb(void *data)
{
	struct desk_server *server = data;
	if (!desk_backlight_set(server->cc_screen)) {
		static bool said;
		if (!said) {
			said = true;
			wlr_log(WLR_ERROR, "the screen's light would not turn: neither logind nor "
				"the backlight's own file would take it");
		}
	}
	return 0;
}

static void screen_apply(struct desk_server *server)
{
	if (!server->cc_screen_ok) {
		return;
	}
	if (!server->cc_screen_send) {
		server->cc_screen_send = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), screen_send_cb, server);
	}
	if (server->cc_screen_send) {
		wl_event_source_timer_update(server->cc_screen_send, 40);
	} else {
		screen_send_cb(server);
	}
}

/* Where the screen's light is now, as the machine has it -- something else
 * (the keys of a laptop, its power manager) may have turned it. */
static void screen_read(struct desk_server *server)
{
	/* the backlight alone: the rest of the supply is the firmware's, and
	 * slow to answer on some laptops (desk_supply_latest) */
	char name[256];
	double max, now;
	server->cc_screen_ok = desk_backlight_find(name, sizeof name, &max, &now);
	if (server->cc_screen_ok && server->cc_grab != DESK_CC_SCREEN) {
		server->cc_screen = now / max > 1.0 ? 1.0 : now / max;
	}
}

static void paint_console(struct desk_server *server);

/* The brightness keys a laptop has: the screen's light a step -- or, on a
 * machine with no backlight, the lamp over the desk. */
static void screen_step(struct desk_server *server, int dir)
{
	screen_read(server);
	double *v = server->cc_screen_ok ? &server->cc_screen : &server->cc_bright;
	*v = clampf_d(*v + dir * 0.0625, 0.0, 1.0);
	if (server->cc_screen_ok) {
		screen_apply(server);
	} else {
		console_dim_apply(server);
	}
	paint_console(server);
	server_schedule_frames(server);
}

static int cc_live_ms(void);

/* The clock the tube goes by: on at the pace the set is drawn of itself
 * (cc_live_ms), whatever else asks for it to be drawn. A knob being turned
 * asks for a frame sixty times a second, and the tube -- a new face at
 * every tick of its clock -- was drawn again on every one of them, five
 * milliseconds each, for a flicker nobody sees go that fast. A tenth short
 * of the tick, so that every tick moves it on. */
static double cc_clock(void)
{
	double q = cc_live_ms() * 0.0009;
	return floor(monotonic_sec() / q) * q;
}

/* How far down the button that does a thing is: down in a moment, up again after it. */
#define CC_PUSH_DOWN_S 0.09
#define CC_PUSH_UP_S 0.30
static double console_push_depth(const struct desk_server *server)
{
	if (server->cc_push < 0) {
		return 0.0;
	}
	double t = monotonic_sec() - server->cc_push_at;
	return t < CC_PUSH_DOWN_S ? t / CC_PUSH_DOWN_S
		: clampf_d(1.0 - (t - CC_PUSH_DOWN_S) / CC_PUSH_UP_S, 0.0, 1.0);
}

/* ---- the night's warm screen ----
 *
 * NIGHT on the lid: every screen's colours through a warm ramp, as a lamp in the evening has
 * them -- the blue taken down most, the green a little -- by the gamma table each screen has
 * (where it has none, as a window on another desktop has not, there is nothing to do it with,
 * and the button stays dark). Kept, a word in $XDG_STATE_HOME/desk/night, for the next login. */
#define NIGHT_R 1.00
#define NIGHT_G 0.82
#define NIGHT_B 0.62

static bool night_possible(const struct desk_server *server)
{
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (!o->off && wlr_output_get_gamma_size(o->wlr_output) > 0) {
			return true;
		}
	}
	return false;
}

static bool night_path(char *out, size_t n, bool make)
{
	char dir[512];
	const char *st = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
	if (st && *st == '/') {
		snprintf(dir, sizeof dir, "%s/desk", st);
	} else if (home && *home == '/') {
		snprintf(dir, sizeof dir, "%s/.local/state/desk", home);
	} else {
		return false;
	}
	if (make) {
		mkdir(dir, 0700);
	}
	return (size_t)snprintf(out, n, "%s/night", dir) < n;
}

static void night_set(struct desk_server *server, bool on)
{
	server->cc_night = on;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		o->gamma_dirty = true;
	}
	char path[600];
	if (night_path(path, sizeof path, on)) {
		if (on) {
			FILE *f = fopen(path, "we");
			if (f) {
				fputs("on\n", f);
				fclose(f);
			}
		} else {
			unlink(path);
		}
	}
	server_schedule_frames(server);
}

/* The frame, with the screen's gamma table as the night has it: on, the warm ramp; off, none. */
static void night_commit(struct desk_output *output, struct wlr_scene_output *so)
{
	struct wlr_output *wo = output->wlr_output;
	struct wlr_output_state st;
	wlr_output_state_init(&st);
	if (wlr_scene_output_build_state(so, &st, NULL)) {
		size_t n = wlr_output_get_gamma_size(wo);
		uint16_t *lut = n && output->server->cc_night ? malloc(n * 3 * sizeof *lut) : NULL;
		if (lut) {
			/* as strong as Shell Settings has it: the way from white to the warm ramp,
			 * that far (half of it light, twice it the strongest) */
			double k = output->server->conf.night;
			double gr = fmax(0.0, 1.0 - (1.0 - NIGHT_R) * k);
			double gg = fmax(0.0, 1.0 - (1.0 - NIGHT_G) * k);
			double gb = fmax(0.0, 1.0 - (1.0 - NIGHT_B) * k);
			for (size_t i = 0; i < n; i++) {
				double v = n > 1 ? (double)i / (double)(n - 1) : 1.0;
				lut[i] = (uint16_t)lround(v * gr * 65535.0);
				lut[n + i] = (uint16_t)lround(v * gg * 65535.0);
				lut[2 * n + i] = (uint16_t)lround(v * gb * 65535.0);
			}
			wlr_output_state_set_gamma_lut(&st, n, lut, lut + n, lut + 2 * n);
		} else if (n) {
			wlr_output_state_set_gamma_lut(&st, 0, NULL, NULL, NULL);
		}
		if (!wlr_output_commit_state(wo, &st)) {
			wlr_log(WLR_INFO, "%s: the night's colours would not go on", wo->name);
		}
		free(lut);
	}
	wlr_output_state_finish(&st);
	output->gamma_dirty = false;
}

static void console_state(const struct desk_server *server,
	struct desk_console_state *st)
{
	st->wifi_on = server->cc_wifi != 0;
	st->bt_on = server->cc_bt != 0;
	st->volume = server->cc_vol;
	st->brightness = server->cc_bright;
	st->screen = server->cc_screen;
	st->screen_ok = server->cc_screen_ok;
	st->warm = server->cc_warm;
	st->clock = cc_clock();
	st->key = server->cc_key;
	st->guard = server->cc_guard;
	st->pressed = server->cc_pressed;
	st->dying = server->cc_dying;
	st->lock_push = server->cc_lock_push;
	st->antenna = server->cc_ant;
	st->push_which = server->cc_push;
	st->push = console_push_depth(server);
	st->air_on = !server->cc_wifi && !server->cc_bt;
	st->save_on = server->cc_save;
	st->save_ok = server->cc_save_ok;
	st->night_on = server->cc_night;
	st->night_ok = night_possible(server);
	st->screens_mode = server->screens_mode;
	st->screens_ok = screens_count(server) >= 2;
	st->screens_said = server->screens_said;
	/* the tube's word: two seconds, and gone in half another */
	st->screens_word = server->screens_said_at > 0.0
		? clampf_d((2.6 - (monotonic_sec() - server->screens_said_at)) / 0.5, 0.0, 1.0) : 0.0;
}

struct cc_piece_args {
	int piece;
	const struct desk_console_state *st;
	int hover;
	const struct desk_meters *m;
};

static void cc_piece_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct cc_piece_args *a = data;
	desk_draw_console_piece(cr, w, h, a->piece, a->st, a->hover, a->m);
}

/* the camera's fade, across every piece */
static void cc_live_opacity(struct desk_server *server, double a)
{
	for (int p = 0; p < DESK_CCP_COUNT; p++) {
		if (server->cc_sprite[p].node) {
			wlr_scene_buffer_set_opacity(server->cc_sprite[p].node, (float)a);
		}
	}
}

/* A view's traced sheet goes under what is drawn live over it: the
 * console's needles and lamps, the letters in the post box. Those outlive
 * the sheet, which desk_relayout drops for another level or size, and the
 * sheet made again went on top of its layer and hid them until the next
 * login (#12). */
static void view_bases_under(struct desk_server *server)
{
	if (server->cc_base && server->cc_live) {
		wlr_scene_node_place_below(&server->cc_base->node, &server->cc_live->node);
	}
	if (server->pb_base && server->pb_sprite.node) {
		wlr_scene_node_place_below(&server->pb_base->node, &server->pb_sprite.node->node);
	}
}

/* The settled view of the set, moved with the camera at the end of a move. */
static void cc_follow(struct desk_server *server, int dx, int dy)
{
	if (server->cc_live) {
		wlr_scene_node_set_position(&server->cc_live->node, dx, dy);
	}
	if (server->cc_base) {
		wlr_scene_node_set_position(&server->cc_base->node, dx, dy);
	}
}

#define CC_PAINT_FROM 0.30   /* how far into the move to the set its pieces start to be drawn */
#define CC_PAINT_MS 5.0      /* and how long a frame they may take, the rest left to the next */

/* The lid, and the eight drawn with it: the four printed on it, the four under it. */
static bool cc_with_lid(int p)
{
	return p == DESK_CCP_GUARD || p == DESK_CC_PW_OFF || p == DESK_CC_PW_BOOT
		|| p == DESK_CC_PW_OUT || p == DESK_CC_PW_SUSPEND || p == DESK_CC_AIR
		|| p == DESK_CC_SAVE || p == DESK_CC_NIGHT || p == DESK_CC_DISPLAY;
}

/* The meters, which go on by themselves: the tube's trace and words, the battery, the network. */
static bool cc_by_itself(int p)
{
	return p == DESK_CC_CRT || p == DESK_CC_BATT || p == DESK_CC_NET;
}

/* The order the pieces are drawn in when there is not time for all of them in a frame: the lid
 * and its eight first, then everything a hand moves, and the meters last -- they are drawn again
 * on the next tick in any case. The tube, first and drawn again on every tick, could take the
 * whole of a frame's time on a slower machine, and the key, behind it, was not drawn until the
 * lid moved (2026-11-20, the owner: 鍵が蓋が動いたタイミングしか動かない). */
static int cc_paint_order(int k)
{
	static int order[DESK_CCP_COUNT];
	static bool made;
	if (!made) {
		int n = 0;
		for (int pass = 0; pass < 3; pass++) {
			for (int p = 0; p < DESK_CCP_COUNT; p++) {
				int in = cc_with_lid(p) ? 0 : cc_by_itself(p) ? 2 : 1;
				if (in == pass) {
					order[n++] = p;
				}
			}
		}
		made = true;
	}
	return order[k];
}

static void paint_console_body(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	console_dim_apply(server);
	if (!console_up(server)) {
		if (server->cc_live) {
			wlr_scene_node_set_enabled(&server->cc_live->node, false);
		}
		if (server->cc_base) {
			wlr_scene_node_set_enabled(&server->cc_base->node, false);
		}
		if (server->film_a[DESK_VIEW_CONSOLE]) {
			wlr_scene_node_set_enabled(&server->film_a[DESK_VIEW_CONSOLE]->node, false);
			wlr_scene_node_set_enabled(&server->film_b[DESK_VIEW_CONSOLE]->node, false);
		}
		return;
	}
	film_ensure(server, DESK_VIEW_CONSOLE, server->layer_console);

	/* The traced sheet goes up once. Copying a screenful of baked steel
	 * on every frame was most of what the set cost to look at. Once for a
	 * room, too: the screen changed shape, the set is drawn at once from
	 * the room there is, and drawn again when the new size's own room has
	 * been traced -- which puts it somewhere else (DaeTheDruid, #58). */
	unsigned room = desk_console_room(w, h);
	if (!server->cc_base || server->cc_base_w != w || server->cc_base_h != h
		|| server->cc_base_room != room) {
		struct desk_buf *bb = buf_create(w, h);
		desk_draw_console_base(bb->cr, w, h);
		scene_put(&server->cc_base, server->layer_console, bb, 0, 0, NULL);
		server->cc_base_w = w;
		server->cc_base_h = h;
		server->cc_base_room = room;
		view_bases_under(server);
	}

	/* The live half: needles, lamps and the tube, on nothing -- shown once the move has got far
	 * enough for it to show. A sprite a piece, and a piece is drawn only when its key moves: on
	 * most frames that is the tube and nothing else. They are drawn from early in the move, a few
	 * milliseconds of them a frame, so that they are there when it comes to show them: drawn all
	 * at once where they came in, they were a stop of 20 to 85 ms on every opening of the set
	 * (2026-11-12, the owner: コントロールパネル…重い). */
	bool have_view = server->cc_t > 0.70;
	/* (and ahead only on the way to it: on the way back they are not to be seen again) */
	if (have_view || (server->cc_t > CC_PAINT_FROM && server->cc_to > 0.5)) {
		if (!server->cc_live) {
			server->cc_live = wlr_scene_tree_create(server->layer_console);
		}
		struct desk_console_state st;
		console_state(server, &st);
		/* the meters read once a tick of the tube's clock, the same tick
		 * that moves the tube on: frames asked for in between (a knob
		 * being turned) find nothing on it to draw again */
		if (st.clock != server->cc_meters_clock) {
			desk_meters_read(&server->cc_meters);
			server->cc_meters_clock = st.clock;
		}
		struct desk_meters m = server->cc_meters;
		desk_console_advance(&st, &m);
		bool force = server->cc_live_w != w || server->cc_live_h != h
			|| server->cc_live_room != room;
		server->cc_live_w = w;
		server->cc_live_h = h;
		server->cc_live_room = room;
		if (force) {
			/* every piece again, for the new size or room: in the frames to come, as many as
			 * there is time for in each */
			for (int p = 0; p < DESK_CCP_COUNT; p++) {
				server->cc_sprite[p].drawn = false;
			}
		}
		double t0 = real_sec();
		bool more = false;
		/* The lid and the eight that go with it (the four on it, the four under it) all in one
		 * frame, however long it takes, while the lid is up or on its way, or was: drawn a few at
		 * a time, the four had gone from the panel before the lid that carries them was there
		 * (2026-11-20, the owner: 蓋を開けたときの描画がおかしくなる). */
		struct desk_sprite *gsp = &server->cc_sprite[DESK_CCP_GUARD];
		bool lid_live = st.guard > 0.0005 || (gsp->node && gsp->node->node.enabled);
		/* And moving, on a finger or on its own, it is drawn as finely as things in motion are
		 * (motion_res), and whole again where it comes to rest. */
		bool lid_moving = st.guard > 0.0005 && st.guard < 0.999
			&& (server->cc_guard_moving || (server->hand_on && server->hand_work == HAND_LID));
		for (int k = 0; k < DESK_CCP_COUNT; k++) {
			int p = cc_paint_order(k);
			struct desk_sprite *sp = &server->cc_sprite[p];
			bool shrunk = p == DESK_CCP_GUARD && lid_moving;
			uint64_t key = desk_console_piece_key(p, &st, server->cc_hover, &m)
				^ (shrunk ? 0x9e3779b97f4a7c15ull : 0);
			if (sp->drawn && sp->key == key) {
				continue;
			}
			if (!(lid_live && cc_with_lid(p)) && (real_sec() - t0) * 1000.0 > CC_PAINT_MS) {
				/* the rest the next frame */
				more = true;
				break;
			}
			sp->key = key;
			if (p == DESK_CCP_GUARD && st.guard <= 0.0005) {
				/* the lid shut: it is in the sheet, and there is nothing of it to draw over that
				 * (a picture as big as its swing, cleared and handed over for nothing, was 50 ms
				 * of the set's first frame on a Surface Pro) */
				if (sp->node) {
					wlr_scene_node_set_enabled(&sp->node->node, false);
				}
				sp->drawn = true;
				continue;
			}
			double box[4];
			bool known = desk_console_piece_box(w, h, p, box);
			struct cc_piece_args a = { p, &st, server->cc_hover, &m };
			double was = server->sprite_shrink;
			if (shrunk) {
				server->sprite_shrink = motion_res();
			}
			sprite_paint(server, sp, server->cc_live, known ? box : NULL, cc_piece_draw, &a);
			server->sprite_shrink = was;
		}
		if (more) {
			server_schedule_frames(server);
		}
	}
	if (server->cc_live) {
		wlr_scene_node_set_enabled(&server->cc_live->node, have_view);
	}
	wlr_scene_node_set_enabled(&server->cc_base->node, have_view);

	double t = server->cc_t;
	if (!server->film_a[DESK_VIEW_CONSOLE] || (!server->cc_moving && t >= 0.9995)) {
		film_hide(server, DESK_VIEW_CONSOLE);
		cc_follow(server, 0, 0);
		cc_live_opacity(server, 1.0);
		wlr_scene_buffer_set_opacity(server->cc_base, 1.0f);
		return;
	}
	double over;
	int dx, dy;
	film_play(server, DESK_VIEW_CONSOLE, t, &over, &dx, &dy);
	cc_follow(server, dx, dy);
	cc_live_opacity(server, have_view ? over : 0.0);
	if (server->cc_live) {
		wlr_scene_node_set_enabled(&server->cc_live->node, have_view && over > 0.001);
	}
	wlr_scene_buffer_set_opacity(server->cc_base, (float)(have_view ? over : 0.0));
	wlr_scene_node_set_enabled(&server->cc_base->node, have_view && over > 0.001);
}

static void paint_console(struct desk_server *server)
{
	prof_begin(PROF_CONSOLE);
	paint_console_body(server);
	prof_end();
}

static void console_toggle(struct desk_server *server)
{
	/* whatever was pulled out from under the Control Panel goes back */
	card_gone(server);
	if (server->cc_t < 0.5
		&& !desk_scene_view_ready(server->desk_w, server->desk_h, DESK_VIEW_CONSOLE)) {
		return;
	}
	if (console_dying(server)) {
		return;   /* the lamp is going out; nobody is turning away now */
	}
	/* Whichever way this goes, the guarded set is put back as it was
	 * found: lid down, key out. Nobody wants to come back to the desk
	 * and find it standing open. */
	console_power_reset(server);
	double now = monotonic_sec();
	double cur = server->cc_t;
	server->cc_from = cur;
	server->cc_to = cur > 0.5 ? 0.0 : 1.0;
	server->cc_start = now;
	snd_forget(&server->snd_cc_land);
	server->cc_hover = -1;
	server->cc_grab = -1;
	double span = fabs(server->cc_to - cur);
	if (span < 0.30) {
		span = 0.30;
	}
	server->cc_dur = 0.84 * span;
	server->cc_moving = 1;
	console_live(server, false);
	if (server->cc_to > 0.5) {
		/* the screen's light, wherever something else has left it; and the power profile,
		 * wherever another program has */
		screen_read(server);
		int saver = desk_power_saver();
		server->cc_save_ok = saver >= 0;
		server->cc_save = saver > 0;
		/* You push the drawer in before you turn away from the desk, and
		 * the film is traced with it shut. */
		if (server->drawer_t > 0.004) {
			drawer_toggle(server);
		}
		server->cc_warm = 0.0;
		server->cc_warming = 0;
	} else {
		/* turning away leaves it to go cold again -- and switches it off */
		if (server->cc_t > 0.5) {
			desk_sound_play(DESK_SND_SET_OFF, -0.2, 1.0, 0.0);
		}
		server->cc_warming = 0;
	}
	server_schedule_frames(server);
	paint_console(server);
}

/* ---- the guarded set -------------------------------------------
 *
 * Nothing here is a setting, so nothing here is one click. You turn the
 * key, you lift the glass, and then you push. The machine takes its time
 * over each of them on purpose: this is the one part of the desk that ends
 * the session, and it should feel like ending it.
 *
 * The push is the only thing that cannot be taken back. Once the filament
 * starts going out, it goes out. */
#define DESK_CC_KEY_S   1.45   /* the key, seating itself and turning */
#define DESK_CC_GUARD_S 0.42   /* the lid, going up */
#define DESK_CC_ANT_S   1.55   /* the aerial, all the way up or down */
#define DESK_CC_DIE_S   2.80   /* the lamp, going out */

static bool console_dying(const struct desk_server *server)
{
	return server->cc_pressed >= 0;
}

static void console_key_to(struct desk_server *server, double to)
{
	if (console_dying(server)) {
		return;
	}
	/* in, or the bolt drawn back first to take it out */
	desk_sound_play(to > 0.5 ? DESK_SND_CC_KEY_IN : DESK_SND_CC_KEY_BACK, -0.55, 1.0, 0.0);
	server->cc_key_from = server->cc_key;
	server->cc_key_to = to;
	server->cc_key_start = monotonic_sec();
	snd_forget(&server->snd_key_end);
	server->cc_key_dur = DESK_CC_KEY_S * fmax(0.15, fabs(to - server->cc_key));
	server->cc_key_moving = 1;
	console_live(server, true);
}

/* The aerial after the lever: up for wireless on, down for off, from
 * wherever it has got to, the motor heard all the while it runs. */
static void console_aerial_to(struct desk_server *server, double to)
{
	double now = monotonic_sec();
	double span = fabs(to - server->cc_ant);
	if (span < 1e-3) {
		return;
	}
	server->cc_ant_from = server->cc_ant;
	server->cc_ant_to = to;
	server->cc_ant_start = now;
	server->cc_ant_dur = DESK_CC_ANT_S * span;
	server->cc_ant_moving = 1;
	if (server->cc_ant_snd > 0) {
		desk_sound_stop_after(server->cc_ant_snd, 0.0, 0.05);
	}
	server->cc_ant_snd = desk_sound_play(to > 0.5 ? DESK_SND_AERIAL_UP
		: DESK_SND_AERIAL_DOWN, 0.55, 0.7, 0.0);
	if (server->cc_ant_snd > 0 && span < 0.999) {
		desk_sound_stop_after(server->cc_ant_snd, real_secs(server->cc_ant_dur), 0.05);
	}
	console_live(server, true);
	if (server->cc_timer) {
		wl_event_source_timer_update(server->cc_timer, 16);
	}
}

static void console_guard_to(struct desk_server *server, double to)
{
	if (console_dying(server)) {
		return;
	}
	desk_sound_play(to > 0.5 ? DESK_SND_CC_LID_UP : DESK_SND_CC_LID_DOWN, -0.55, 1.0, 0.0);
	server->cc_guard_from = server->cc_guard;
	server->cc_guard_to = to;
	server->cc_guard_start = monotonic_sec();
	snd_forget(&server->snd_guard_end);
	server->cc_guard_dur = DESK_CC_GUARD_S
		* fmax(0.20, fabs(to - server->cc_guard));
	server->cc_guard_moving = 1;
	console_live(server, true);
}

/* Everything back where it was: the lid down, the key out. Turning away
 * from the machine does this, so you never come back to it standing open. */
static void console_power_reset(struct desk_server *server)
{
	server->cc_key = server->cc_key_to = 0.0;
	server->cc_guard = server->cc_guard_to = 0.0;
	server->cc_key_moving = server->cc_guard_moving = 0;
	server->cc_pressed = -1;
	server->cc_dying = 0.0;
}

/* The lamps back on after the machine did not go after all: never a dark
 * screen over a machine that is still running. */
static void console_come_back(struct desk_server *server)
{
	if (!console_dying(server)) {
		return;
	}
	server->cc_pressed = -1;
	server->cc_dying = 0.0;
	console_guard_to(server, 1.0);
	paint_console(server);
	server_schedule_frames(server);
}

/* ---- ending the session ---------------------------------------------
 *
 * Logging out, restarting and powering off all end what is on the desk,
 * so every window is asked to close first, as its own close button would
 * -- a program with work not saved asks about it then -- and the session
 * ends only once the last one has gone. One that is still there after
 * END_WAIT_S (a question about saving, or a program that does not answer)
 * holds it up: the lamps come back on, the desk is turned back to, and that
 * window is put in front. Asked again within END_AGAIN_S of that, it goes
 * ahead once the windows have had their time, whatever is still open. */
enum desk_end { END_LOG_OUT, END_REBOOT, END_POWER_OFF };
#define END_WAIT_S 8.0
#define END_AGAIN_S 60.0

static const char *const END_NAME[] = { "log out", "restart", "power off" };

static void end_finish(void *data)
{
	struct desk_server *server = data;
	int what = server->ending - 1;
	if (what < 0 || server->end_done) {
		return;
	}
	server->end_done = true;
	if (server->end_timer) {
		wl_event_source_timer_update(server->end_timer, 0);
	}
	if (what == END_LOG_OUT) {
		wlr_log(WLR_INFO, "ending: logging out");
		wl_display_terminate(server->wl_display);
		return;
	}
	wlr_log(WLR_INFO, "ending: asking logind to %s", END_NAME[what]);
	if (desk_power_request(what == END_REBOOT ? DESK_POWER_REBOOT : DESK_POWER_OFF)) {
		return;   /* the screen stays dark, because the machine is going */
	}
	/* It said no -- most likely polkit wanting somebody to be at the
	 * keyboard in a way we are not. */
	wlr_log(WLR_ERROR, "ending: logind refused; coming back up");
	server->ending = 0;
	server->end_done = false;
	console_come_back(server);
}

/* The time is up: if the windows have all gone, it ends; else the window
 * still there holds it up -- unless this is the second time of asking. */
static int end_timer_cb(void *data)
{
	struct desk_server *server = data;
	if (!server->ending) {
		return 0;
	}
	int left = wl_list_length(&server->toplevels);
	if (left == 0 || server->end_anyway) {
		if (left) {
			wlr_log(WLR_INFO, "ending: %d window%s still open; going ahead, as asked twice",
				left, left == 1 ? "" : "s");
		}
		end_finish(server);
		return 0;
	}
	wlr_log(WLR_INFO, "ending: %d window%s did not close; staying (ask again to go ahead)",
		left, left == 1 ? "" : "s");
	server->ending = 0;
	server->end_held_at = real_sec();
	console_come_back(server);
	if (console_up(server)) {
		console_toggle(server);
	}
	struct desk_toplevel *view = wl_container_of(server->toplevels.next, view, link);
	focus_toplevel(view);
	server_schedule_frames(server);
	return 0;
}

static void end_begin(struct desk_server *server, int what)
{
	if (server->ending) {
		return;
	}
	server->ending = what + 1;
	server->end_anyway = server->end_held_at > 0.0
		&& real_sec() - server->end_held_at < END_AGAIN_S;
	int n = 0;
	struct desk_toplevel *view, *tmp;
	wl_list_for_each_safe(view, tmp, &server->toplevels, link) {
		view_close(view);
		n++;
	}
	wlr_log(WLR_INFO, "ending (%s): %d window%s asked to close", END_NAME[what], n,
		n == 1 ? "" : "s");
	if (n == 0) {
		end_finish(server);
		return;
	}
	if (!server->end_timer) {
		server->end_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), end_timer_cb, server);
	}
	if (server->end_timer) {
		wl_event_source_timer_update(server->end_timer, (int)(END_WAIT_S * 1000.0));
	}
}

/* A window has gone: the last one, while ending, ends it -- not from inside
 * the window's own going, but just after. */
static void end_check(struct desk_server *server)
{
	if (server->ending && wl_list_empty(&server->toplevels)) {
		wl_event_loop_add_idle(wl_display_get_event_loop(server->wl_display), end_finish,
			server);
	}
}

static void console_power_fire(struct desk_server *server)
{
	int which = server->cc_pressed;
	wlr_log(WLR_INFO, "console: the %s one",
		which == DESK_CC_PW_OUT ? "green" : which == DESK_CC_PW_BOOT ? "amber" : "red");
	end_begin(server, which == DESK_CC_PW_OUT ? END_LOG_OUT
		: which == DESK_CC_PW_BOOT ? END_REBOOT : END_POWER_OFF);
}

/* Once it has landed the set is alive but nothing else is: the tube draws,
 * the needles wander, the lamps finish coming up. That is a repaint twelve
 * times a second, not a frame sixty times a second -- asking for a frame on
 * every vblank and then having nothing to put in it was what the machine
 * was actually spending its time on. */
#define DESK_CC_LIVE_MS 83

/* and less often for less drawing: eight times a second on Medium, six on
 * Low, twice in 2D -- and on Ultra, which keeps nothing back to save the
 * work, thirty: the tube's beam and its hum bar going over smoothly */
static int cc_live_ms(void)
{
	enum desk_gfx g = desk_gfx_level();
	return g == DESK_GFX_2D ? 500 : g == DESK_GFX_LOW ? 166
		: g == DESK_GFX_MEDIUM ? 125 : g == DESK_GFX_ULTRA ? 33 : DESK_CC_LIVE_MS;
}

static int console_live_cb(void *data)
{
	struct desk_server *server = data;
	if (!console_up(server) || server->cc_moving) {
		return 0;   /* the move drives itself; this re-arms when it lands */
	}
	if (server->cc_warming) {
		server->cc_warm = clampf_d(
			(monotonic_sec() - server->cc_warm_start) / 1.60, 0, 1);
		if (server->cc_warm >= 1.0) {
			server->cc_warming = 0;
		}
	}
	double now = monotonic_sec();
	if (server->cc_key_moving) {
		double u = clampf_d((now - server->cc_key_start) / server->cc_key_dur, 0, 1);
		server->cc_key = server->cc_key_from
			+ (server->cc_key_to - server->cc_key_from) * smoothstep01(0, 1, u);
		/* turned: the bolt thrown; or drawn out of the lock */
		bool in = server->cc_key_to > 0.5;
		snd_end(&server->snd_key_end, server->cc_key_start, server->cc_key_dur,
			in ? DESK_SND_CC_KEY_TURN : DESK_SND_CC_KEY_OUT, -0.55, 1.0, 0.0);
		if (u >= 1.0) {
			server->cc_key = server->cc_key_to;
			server->cc_key_moving = 0;
		}
	}
	if (server->cc_ant_moving) {
		/* a motor: the same speed all the way, but for getting going */
		double u = clampf_d((now - server->cc_ant_start) / server->cc_ant_dur, 0, 1);
		double e = u < 0.08 ? u * u / 0.16 : u > 0.92 ? 1.0 - (1.0 - u) * (1.0 - u) / 0.16
			: (u - 0.04) / 0.92;
		server->cc_ant = server->cc_ant_from + (server->cc_ant_to - server->cc_ant_from) * e;
		if (u >= 1.0) {
			server->cc_ant = server->cc_ant_to;
			server->cc_ant_moving = 0;
		}
	}
	if (server->cc_guard_moving) {
		double u = clampf_d((now - server->cc_guard_start)
			/ server->cc_guard_dur, 0, 1);
		server->cc_guard = server->cc_guard_from
			+ (server->cc_guard_to - server->cc_guard_from) * smoothstep01(0, 1, u);
		snd_end(&server->snd_guard_end, server->cc_guard_start, server->cc_guard_dur,
			server->cc_guard_to > 0.5 ? DESK_SND_CC_LID_UP_STOP : DESK_SND_CC_LID_DOWN_STOP,
			-0.55, 1.0, 0.0);
		if (u >= 1.0) {
			server->cc_guard = server->cc_guard_to;
			server->cc_guard_moving = 0;
		}
	}
	if (server->cc_push >= 0 && now - server->cc_push_at > CC_PUSH_DOWN_S + CC_PUSH_UP_S) {
		server->cc_push = -1;
	}
	if (server->cc_then >= 0 && now >= server->cc_then_at) {
		int then = server->cc_then;
		server->cc_then = -1;
		if (then == DESK_CC_SLEEP) {
			wlr_log(WLR_INFO, "console: SLEEP: the screens to sleep");
			screen_go_dark(server);
		} else if (desk_power_request(DESK_POWER_SUSPEND)) {
			wlr_log(WLR_INFO, "console: SUSPEND: asked logind to suspend the machine");
			/* and the set put back, as on turning away from it: what wakes up is not
			 * standing open */
			console_key_to(server, 0.0);
			console_guard_to(server, 0.0);
		} else {
			wlr_log(WLR_ERROR, "console: SUSPEND: logind would not suspend the machine");
			snd_at(server, DESK_SND_KEY_PRESS, server->desk_w * 0.6, 0.4);
		}
	}
	if (console_dying(server)) {
		server->cc_dying = clampf_d((now - server->cc_die_start)
			/ DESK_CC_DIE_S, 0, 1);
		if (server->cc_dying >= 1.0) {
			/* Dark. This is the moment the machine was asked for, and not
			 * one frame before it. */
			paint_console(server);
			console_dim_apply(server);
			server_schedule_frames(server);
			console_power_fire(server);
			if (console_dying(server)) {
				return 0;   /* it is going: nothing more to draw */
			}
			/* refused -- carry on below, which puts the light back */
		}
	}
	paint_console(server);
	console_dim_apply(server);
	server_schedule_frames(server);
	if (server->cc_timer) {
		/* the going-out wants smoother frames than a needle does, and
		 * so does the aerial */
		wl_event_source_timer_update(server->cc_timer,
			console_dying(server) || server->cc_ant_moving || server->cc_push >= 0
			|| server->cc_then >= 0 || server->cc_guard_moving || server->cc_key_moving
			? 16 : cc_live_ms());
	}
	return 0;
}

static void console_live(struct desk_server *server, bool on)
{
	if (server->cc_timer) {
		wl_event_source_timer_update(server->cc_timer, on ? cc_live_ms() : 0);
	}
}

static void console_tick(struct desk_server *server)
{
	if (!server->cc_moving) {
		return;
	}
	if (server->look_held == DESK_VIEW_CONSOLE + 1) {
		paint_console(server);
		return;
	}
	if (server->look_free == DESK_VIEW_CONSOLE + 1) {
		if (!look_free_step(server, &server->cc_t, server->cc_to)) {
			paint_console(server);
			server_schedule_frames(server);
			return;
		}
		server->look_free = 0;
		server->cc_start = monotonic_sec() - server->cc_dur;
	}
	double u = clampf_d((monotonic_sec() - server->cc_start) / server->cc_dur, 0, 1);
	double e = look_ease(u);
	server->cc_t = server->cc_from + (server->cc_to - server->cc_from) * e;
	if (server->cc_to > 0.5) {
		/* the relay as it lands, and the hum swelling */
		snd_end(&server->snd_cc_land, server->cc_start, server->cc_dur,
			DESK_SND_SET_WARM, -0.2, 1.0, 0.0);
	}
	if (u >= 1.0) {
		server->cc_t = server->cc_to;
		server->cc_moving = 0;
		if (server->cc_t > 0.5) {
			/* it has landed: put the filaments on */
			server->cc_warm_start = monotonic_sec();
			server->cc_warming = 1;
			console_live(server, true);
		} else {
			server->cc_warm = 0.0;
			console_live(server, false);
		}
	}
	paint_console(server);
	server_schedule_frames(server);
}

/* Throw, turn or push whatever is under the hand. */
/* How a cap sounds as it is pushed: one that puts something on, or off, as it goes over (AIRPLANE with
 * both its levers dropping, or back), one that only does something (DISPLAY, or one with nothing behind
 * it), SLEEP's and SUSPEND's, smaller. */
static enum desk_sound console_cap_sound(const struct desk_server *server, int which)
{
	switch (which) {
	case DESK_CC_SLEEP:
	case DESK_CC_PW_SUSPEND:
		return DESK_SND_CC_CAP_SLEEP;
	case DESK_CC_AIR:
		return server->cc_wifi || server->cc_bt ? DESK_SND_CC_AIR_ON : DESK_SND_CC_AIR_OFF;
	case DESK_CC_SAVE:
		return !server->cc_save_ok ? DESK_SND_CC_CAP
			: server->cc_save ? DESK_SND_CC_CAP_OFF : DESK_SND_CC_CAP_ON;
	case DESK_CC_NIGHT:
		return !night_possible(server) ? DESK_SND_CC_CAP
			: server->cc_night ? DESK_SND_CC_CAP_OFF : DESK_SND_CC_CAP_ON;
	default:
		return DESK_SND_CC_CAP;
	}
}

/* What the four on the lid, SLEEP and SUSPEND do, as the button goes down: the ones that put
 * something on or off at once, the two that put something to sleep a moment after, so the press
 * is seen and heard first. */
static void console_push(struct desk_server *server, int which)
{
	server->cc_push = which;
	server->cc_push_at = monotonic_sec();
	snd_at(server, console_cap_sound(server, which), server->cursor->x, 0.8);
	switch (which) {
	case DESK_CC_SLEEP:
	case DESK_CC_PW_SUSPEND:
		server->cc_then = which;
		server->cc_then_at = monotonic_sec() + 0.45;
		break;
	case DESK_CC_AIR: {
		/* both off, or both back on */
		bool on = server->cc_wifi || server->cc_bt;
		bool took = desk_meters_set_radio(true, !on);
		took = desk_meters_set_radio(false, !on) || took;
		server->cc_wifi = server->cc_bt = !on;
		console_aerial_to(server, on ? 0.0 : 1.0);
		wlr_log(WLR_INFO, "console: airplane mode %s%s", on ? "on" : "off",
			took ? "" : " (nothing on this machine to switch)");
		break;
	}
	case DESK_CC_SAVE:
		if (!server->cc_save_ok) {
			wlr_log(WLR_INFO, "console: no power-profiles daemon to save power with");
			break;
		}
		if (desk_power_saver_set(!server->cc_save)) {
			server->cc_save = !server->cc_save;
			wlr_log(WLR_INFO, "console: power saving %s", server->cc_save ? "on" : "off");
		} else {
			wlr_log(WLR_INFO, "console: the power-profiles daemon would not change");
		}
		break;
	case DESK_CC_NIGHT:
		if (!night_possible(server)) {
			wlr_log(WLR_INFO, "console: no screen here has colours to warm");
			break;
		}
		night_set(server, !server->cc_night);
		wlr_log(WLR_INFO, "console: the night's warm screen %s", server->cc_night ? "on" : "off");
		break;
	case DESK_CC_DISPLAY:
		/* the next of the four ways round (the other button: a display tool) */
		screens_next(server);
		break;
	default:
		break;
	}
	console_live(server, true);
	paint_console(server);
	server_schedule_frames(server);
}

static void console_press(struct desk_server *server, int which)
{
	if (console_dying(server)) {
		return;   /* it is going out; there is nothing left to press */
	}
	switch (which) {
	case DESK_CC_LOCK:
		/* Pushed in as you go -- and seen pushed in, in the picture of
		 * the set you walk out of -- and out again by the time you are
		 * back. */
		server->cc_lock_push = 1.0;
		desk_sound_play(DESK_SND_POWER_PRESS, -0.55, 0.6, 1.2);
		lock_request(server);
		server->cc_lock_push = 0.0;
		paint_console(server);
		return;
	case DESK_CC_PW_KEY:
		/* It goes in, and then it turns itself. Which is not how a key
		 * works, and is exactly how the key to a thing like this works in
		 * every room it was ever kept in. */
		console_key_to(server, server->cc_key_to > 0.5 ? 0.0 : 1.0);
		if (server->cc_key_to < 0.5) {
			console_guard_to(server, 0.0);
		}
		return;
	case DESK_CC_LID:
		/* the lid lifts with the key home, as the glass did -- and once what is under it has
		 * been traced (desk_console_lid_ready), which is seconds after the set is */
		if (server->cc_key < 0.999 || !desk_console_lid_ready(server->desk_w, server->desk_h)) {
			snd_at(server, DESK_SND_KEY_PRESS, server->cursor->x, 0.35);
			wlr_log(WLR_INFO, "console: the lid is %s", server->cc_key < 0.999
				? "locked: the key is not turned" : "not to be lifted yet");
			return;
		}
		console_guard_to(server, 1.0);
		return;
	case DESK_CC_SLEEP:
	case DESK_CC_AIR:
	case DESK_CC_SAVE:
	case DESK_CC_NIGHT:
	case DESK_CC_DISPLAY:
		console_push(server, which);
		return;
	case DESK_CC_PW_SUSPEND:
		if (server->cc_key < 0.999 || server->cc_guard < 0.999) {
			return;
		}
		console_push(server, which);
		return;
	case DESK_CC_PW_OFF:
	case DESK_CC_PW_BOOT:
	case DESK_CC_PW_OUT:
		if (server->cc_key < 0.999) {
			return;   /* locked: the lid will not lift and neither will it */
		}
		if (server->cc_guard_to < 0.5 || server->cc_guard < 0.999) {
			/* the lid is in the way, and lifting it is how you get at them */
			console_guard_to(server, 1.0);
			return;
		}
		server->cc_pressed = which;
		server->cc_dying = 0.0;
		server->cc_die_start = monotonic_sec();
		/* the button, the relay, and the set going out as long as it takes */
		desk_sound_play(DESK_SND_CC_POWER, -0.55, 1.0, 0.0);
		console_live(server, true);
		paint_console(server);
		server_schedule_frames(server);
		return;
	default:
		break;
	}
	switch (which) {
	case DESK_CC_WIFI:
	case DESK_CC_BT: {
		bool wifi = which == DESK_CC_WIFI;
		int *flag = wifi ? &server->cc_wifi : &server->cc_bt;
		*flag = !*flag;
		snd_at(server, *flag ? DESK_SND_CC_LEVER_UP : DESK_SND_CC_LEVER_DOWN, server->cursor->x, 1.0);
		bool took = desk_meters_set_radio(wifi, *flag != 0);
		if (wifi) {
			console_aerial_to(server, *flag ? 1.0 : 0.0);
		}
		wlr_log(WLR_INFO, "console: %s lever %s%s",
			wifi ? "wifi" : "bluetooth", *flag ? "on" : "off",
			took ? "" : " (nothing on this machine to throw)");
		break;
	}
	case DESK_CC_SCREEN:
		if (!server->cc_screen_ok) {
			/* nothing behind it: it does not move */
			snd_at(server, DESK_SND_KEY_PRESS, server->cursor->x, 0.5);
			return;
		}
		/* fall through */
	case DESK_CC_VOL:
	case DESK_CC_BRIGHT: {
		/* you take hold of it, and it follows the hand until you let go */
		double v;
		if (desk_console_value_at(server->desk_w, server->desk_h, which,
				server->cursor->x, server->cursor->y, &v)) {
			server->cc_grab = which;
			server->snd_knob = (int)floor(v * 24.0);
			snd_at(server, DESK_SND_CC_KNOB, server->cursor->x, 0.8);
			if (which == DESK_CC_VOL) {
				server->cc_vol = v;
				desk_meters_set_volume(v);
			} else if (which == DESK_CC_SCREEN) {
				server->cc_screen = v;
				screen_apply(server);
			} else {
				server->cc_bright = v;
				console_dim_apply(server);
			}
		}
		break;
	}
	default:
		/* an instrument is something you read, not something you work */
		return;
	}
	paint_console(server);
	server_schedule_frames(server);
}

/* The free program that does in full what a control on the set does in
 * brief: the networks behind the meter and the wireless lever, Bluetooth's
 * devices behind its lever, the sound devices behind the volume knob --
 * the same programs, in the same order, as the rows of Shell Settings'
 * SYSTEM part (settings.c), found by their desktop files whether or not
 * those put them on a shelf. The first of them installed; you turn back
 * to the desk, where its window comes. */
static void console_open_settings(struct desk_server *server, int which)
{
	static const char *const NETWORK[] = { "nm-connection-editor.desktop", NULL };
	static const char *const BLUETOOTH[] = { "blueman-manager.desktop", NULL };
	/* pavucontrol's desktop file has had both names */
	static const char *const SOUND[] = { "org.pulseaudio.pavucontrol.desktop",
		"pavucontrol.desktop", "pavucontrol-qt.desktop", "mate-volume-control.desktop",
		NULL };
	/* the screens: as Shell Settings' Displays finds them (settings.c) */
	static const char *const DISPLAYS[] = { "network.cycles.wdisplays.desktop",
		"nwg-displays.desktop", "xfce-display-settings.desktop",
		"mate-display-properties.desktop", NULL };
	const char *const *ids = which == DESK_CC_NET || which == DESK_CC_WIFI ? NETWORK
		: which == DESK_CC_BT ? BLUETOOTH : which == DESK_CC_VOL ? SOUND
		: which == DESK_CC_DISPLAY ? DISPLAYS : NULL;
	if (!ids) {
		return;
	}
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = NULL;
	for (int i = 0; ids[i] && !app; i++) {
		app = desk_apps_find(l, ids[i]);
	}
	if (app) {
		wlr_log(WLR_INFO, "console: %s, for the %s", app->id,
			which == DESK_CC_BT ? "Bluetooth lever" : which == DESK_CC_VOL
				? "volume knob" : which == DESK_CC_DISPLAY ? "screens" : "network");
		desk_app_launch(app);
	} else {
		wlr_log(WLR_INFO, "console: nothing installed to show this in full");
	}
	desk_apps_release(l);
	if (app) {
		snd_at(server, DESK_SND_KEY_PRESS, server->cursor->x, 0.6);
		console_toggle(server);
	}
}

static void console_drag(struct desk_server *server)
{
	if (server->cc_grab < 0) {
		return;
	}
	double v;
	if (!desk_console_value_at(server->desk_w, server->desk_h, server->cc_grab,
			server->cursor->x, server->cursor->y, &v)) {
		return;
	}
	/* the detents going by under the fingers */
	int step = (int)floor(v * 24.0);
	if (step != server->snd_knob) {
		server->snd_knob = step;
		snd_at(server, DESK_SND_CC_KNOB, server->cursor->x, 1.0);
	}
	if (server->cc_grab == DESK_CC_VOL) {
		if (fabs(v - server->cc_vol) < 0.004) {
			return;
		}
		server->cc_vol = v;
		desk_meters_set_volume(v);
	} else if (server->cc_grab == DESK_CC_SCREEN) {
		if (fabs(v - server->cc_screen) < 0.004) {
			return;
		}
		server->cc_screen = v;
		screen_apply(server);
	} else {
		if (fabs(v - server->cc_bright) < 0.004) {
			return;
		}
		server->cc_bright = v;
		console_dim_apply(server);
	}
	paint_console(server);
	server_schedule_frames(server);
}
