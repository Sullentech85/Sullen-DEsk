/* Part of wm.c: starting up, relighting the room and Wood Maintenance.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static void render_check(struct desk_server *server)
{
	char name[128] = "";
	bool cpu = false;
	if (wlr_renderer_is_pixman(server->renderer)) {
		cpu = true;
		snprintf(name, sizeof name, "pixman");
	}
#ifdef DESK_HAVE_GPU
	else if (wlr_renderer_is_gles2(server->renderer)) {
		struct wlr_egl *egl = wlr_gles2_renderer_get_egl(server->renderer);
		if (egl && desk_gl_renderer_of(wlr_egl_get_display(egl), wlr_egl_get_context(egl),
				name, sizeof name)) {
			cpu = desk_gl_name_software(name);
			server->render_own = desk_gl_name_own(name);
		}
	}
#endif
	const char *say = getenv("DESK_ON_CPU");
	if (say && *say) {
		cpu = strcmp(say, "0") != 0;
	}
	server->on_cpu = cpu;
	snprintf(server->render_name, sizeof server->render_name, "%s", name);
	if (cpu) {
		wlr_log(WLR_INFO, "the screen is drawn on the CPU (%s): the desk draws in %s",
			name[0] ? name : "?", gfx_wanted(server) == DESK_GFX_2D
			? "2D Fallback" : "the level chosen for it");
	} else if (name[0]) {
		wlr_log(WLR_INFO, "the screen is drawn by %s", name);
	}
}

/* For Shell Settings: $XDG_RUNTIME_DIR/<the Wayland display>.render, read
 * by settings.c render_read. */
static char g_render_file[512];

static void render_publish(struct desk_server *server, const char *socket)
{
	const char *run = getenv("XDG_RUNTIME_DIR");
	if (!run || !*run || !socket || !*socket || strchr(socket, '/')
		|| (size_t)snprintf(g_render_file, sizeof g_render_file, "%s/%s.render", run,
			socket) >= sizeof g_render_file) {
		g_render_file[0] = '\0';
		return;
	}
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", g_render_file);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		g_render_file[0] = '\0';
		return;
	}
	static const char *const LEVEL[DESK_GFX_COUNT] = { "2d", "low", "medium", "high", "ultra" };
	fprintf(f, "# how the desk's screen is drawn, for Shell Settings\n"
		"on_cpu = %s\nrenderer = %s\ngraphics_unchosen = %s\n", server->on_cpu ? "yes" : "no",
		server->render_name, LEVEL[gfx_unchosen(server)]);
	bool ok = fclose(f) == 0;
	if (!ok || rename(tmp, g_render_file) != 0) {
		unlink(tmp);
		g_render_file[0] = '\0';
	}
}

static void render_unpublish(void)
{
	if (g_render_file[0]) {
		unlink(g_render_file);
		g_render_file[0] = '\0';
	}
}

/* Shell Settings, from its desktop file, or by name, or from beside this. */
static void settings_open(void)
{
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = desk_apps_find(l, "desk-settings.desktop");
	if (app) {
		desk_app_launch(app);
	}
	desk_apps_release(l);
	if (app) {
		return;
	}
	if (on_path("desk-settings")) {
		spawn("desk-settings");
		return;
	}
	char exe[512];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	exe[n > 0 ? n : 0] = '\0';
	char *slash = strrchr(exe, '/');
	if (slash && (size_t)(slash - exe) + sizeof "/desk-settings" < sizeof exe) {
		memcpy(slash, "/desk-settings", sizeof "/desk-settings");
		if (access(exe, X_OK) == 0) {
			char *const argv[] = { exe, NULL };
			spawn_argv(argv);
		}
	}
}

/* A letter to say the screen is drawn on the CPU: at the end of a login,
 * when the lamp is up and the desk is there to read it on -- and not every
 * login: once, again a week on if it still is, and again if what draws it
 * has changed ($XDG_STATE_HOME/desk/cpu-letter). */
static void cpu_letter(struct desk_server *server)
{
	if (!server->on_cpu) {
		return;
	}
	char path[512];
	const char *st = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
	if (st && *st == '/') {
		snprintf(path, sizeof path, "%s/desk/cpu-letter", st);
	} else if (home && *home) {
		snprintf(path, sizeof path, "%s/.local/state/desk/cpu-letter", home);
	} else {
		path[0] = '\0';
	}
	time_t now = time(NULL);
	if (path[0]) {
		FILE *f = fopen(path, "re");
		if (f) {
			long long when = 0;
			char was[128] = "";
			if (fscanf(f, "%lld %127[^\n]", &when, was) >= 1 && now - (time_t)when < 7 * 86400
				&& now >= (time_t)when && !strcmp(was, server->render_name)) {
				fclose(f);
				return;
			}
			fclose(f);
		}
	}
#ifdef DESK_HAVE_POST
	struct desk_note note = { 0 };
	snprintf(note.summary, sizeof note.summary, "%s", TR("Running on the CPU"));
	snprintf(note.body, sizeof note.body, "%s", desk_gfx_level() == DESK_GFX_2D
		? TR("The screen is being drawn by the processor, not a graphics card, so the desk "
			"draws in 2D Fallback. Check the graphics drivers -- or, in a virtual "
			"machine, its settings (3D acceleration).")
		: TR("The screen is being drawn by the processor, not a graphics card. Check the "
			"graphics drivers -- or, in a virtual machine, its settings (3D "
			"acceleration)."));
	snprintf(note.desktop, sizeof note.desktop, "desk-settings");
	note.n_actions = 1;
	snprintf(note.action_key[0], sizeof note.action_key[0], "desk-settings");
	snprintf(note.action_label[0], sizeof note.action_label[0], "%s", TR("Shell Settings"));
	desk_post_own(&note);
	wlr_log(WLR_INFO, "a letter: the screen is drawn on the CPU");
#endif
	if (path[0]) {
		conf_dirs_for(path);
		FILE *f = fopen(path, "we");
		if (f) {
			fprintf(f, "%lld %s\n", (long long)now, server->render_name);
			fclose(f);
		}
	}
}

/* What a letter of the desk's own offers to do (desk_post_own), done here;
 * and you turn back to the desk, where what it opened comes. */
static void own_letter_action(struct desk_server *server, unsigned id, const char *key)
{
	if (!strcmp(key, "desk-settings")) {
		settings_open();
	}
#ifdef DESK_HAVE_POST
	desk_post_dismiss(id);
#else
	(void)id;
#endif
	if (post_up(server)) {
		post_toggle(server);
	}
}

/* ---- the lamp coming up at login --------------------------------------
 *
 * The room starts dark, the way a room is before the light is put on. The
 * clock on the back of the desk, which lights itself, counts in red how
 * far the room has been made -- the desk's two sheets, then the bookcase,
 * the console and the post box, then the pictures made ahead of the first
 * look at each -- and the lamp comes up with the desk: the pool under it
 * first and warm, the corners last, over boot_light_s(). It does not wait
 * for the rest: they go on being made with the light on, the clock still
 * counting them, and at the end it says READY for a moment and goes back
 * to being a clock. Nothing waits on any of it: the desk answers the whole
 * time.
 *
 * The lamp used to take 3.6 s however ready the room was, and on a machine
 * slow to make the views stay dark up to twelve more after the desk was
 * there: every login waited on it, the room on the disk or not. */

#define BOOT_LIGHT_S 1.0     /* the lamp comes up over this, once the desk is there */
#define BOOT_RATE 1.6        /* and the count goes no faster than this a second */
#define BOOT_HOLD_S 0.8      /* READY, before the clock is a clock again */
#define BOOT_WARM_S 0.12     /* the pictures made ahead, one this often */

/* At Low, half as long: a full-screen change every frame is what an old
 * machine can least afford */
static double boot_light_s(void)
{
	return desk_gfx_level() <= DESK_GFX_LOW ? BOOT_LIGHT_S * 0.5 : BOOT_LIGHT_S;
}

/* The room has finished baking, or been read back: put it up in place of
 * the holding gradient the first frames were drawn with -- and now the tray
 * is where it really is, not where it was guessed to be. */
static void room_landed(struct desk_server *server)
{
	if (!server->icons_shown) {
		wlr_log(WLR_INFO, "the desk is up (%.1f s)", real_sec() - g_start_real);
		/* what the first window's paper is cut from, made meanwhile */
		desk_warm_tiles();
	}
	server->icons_shown = true;
	desktop_paint(server);
	windows_keep_off_tray(server);
	server_schedule_frames(server);
}

/* The letter left in the middle of the desk while its room is made for the
 * first time (desk_scene_first_run): over the dark, where the clock counts,
 * until the lamp is up -- then on the desk, under the windows -- and gone
 * when the room is all there. Not something to click: the desk under it
 * answers as if it were not there. Drawn once, at the screen's own
 * resolution, not the overlay's: at 2D the overlay is the room's 640 wide,
 * and a letter that small is hard to read. */
static void first_letter_tick(struct desk_server *server, double light)
{
	struct desk_sprite *sp = &server->first_letter;
	int w = server->desk_w, h = server->desk_h;
	bool want = server->boot_on && desk_scene_first_run() && w >= 64 && h >= 64;
	if (!want) {
		if (server->first_letter_up && sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, false);
			wlr_log(WLR_INFO, "the first start's letter is put away");
		}
		server->first_letter_up = false;
		return;
	}
	double box[4];
	uint64_t key = ((uint64_t)w << 32) ^ (uint64_t)h ^ ((uint64_t)desk_lang_get() << 60);
	if ((!sp->drawn || sp->key != key) && desk_first_letter_box(w, h, box)) {
		sp->key = key;
		int bx = (int)box[0], by = (int)box[1], bw = (int)box[2], bh = (int)box[3];
		struct desk_buf *b = buf_create(bw, bh);
		cairo_save(b->cr);
		cairo_translate(b->cr, -bx, -by);
		desk_draw_first_letter(b->cr, w, h);
		cairo_restore(b->cr);
		struct wlr_scene_tree *at = sp->node ? sp->node->node.parent : server->layer_top;
		scene_put(&sp->node, at, b, bx, by, NULL);
		wlr_scene_buffer_set_dest_size(sp->node, bw, bh);
		sp->node->point_accepts_input = shadow_accepts;
		sp->drawn = true;
		if (!server->first_letter_up) {
			wlr_log(WLR_INFO, "a first start: a letter in the middle of the desk says so");
		}
	}
	if (!sp->node || !sp->drawn) {
		return;
	}
	server->first_letter_up = true;
	/* over the dark while there is dark; on the desk once the lamp is up */
	struct wlr_scene_tree *in = light < 0.999 ? server->layer_top : server->layer_desk;
	if (sp->node->node.parent != in) {
		wlr_scene_node_reparent(&sp->node->node, in);
	}
	wlr_scene_node_set_enabled(&sp->node->node, true);
	wlr_scene_node_raise_to_top(&sp->node->node);
}

static void boot_end(struct desk_server *server)
{
	server->boot_on = false;
	first_letter_tick(server, 1.0);
	if (server->boot_night) {
		wlr_scene_node_set_enabled(&server->boot_night->node, false);
	}
	if (server->boot_glow.node) {
		wlr_scene_node_set_enabled(&server->boot_glow.node->node, false);
	}
	clock_publish(server);
	server_schedule_frames(server);
	wlr_log(WLR_INFO, "the lamp is up (%.1f s, %.1f s since the start)",
		real_sec() - server->boot_real0, real_sec() - g_start_real);
	/* nobody waits for what is made from now on: half the machine for it (desk_bake_calm) */
	desk_bake_calm(true);
	server->lamp_up_at = monotonic_sec();
	/* a day kept: and now the clock says so */
	show_for_today(server);
	/* and on the CPU, a letter to say so */
	cpu_letter(server);
}

struct boot_glow_args {
	int w, h;
};

static void boot_glow_draw(cairo_t *cr, int w, int h, void *data)
{
	(void)data;
	desk_draw_clock_glow(cr, w, h);
}

/* The dark over everything, as the lamp is (L: 1 for none of it), and
 * what of the clock shines through it; face changes whenever the clock
 * would come out different. */
static void night_paint(struct desk_server *server, double L, uint64_t face)
{
	int w = server->desk_w, h = server->desk_h;
	bool dark = L < 0.999;
	side_desks_light(server, dark ? L : 1.0);
	if (dark) {
		/* all gradient: a quarter of the screen, stretched */
		int nw = w / 4 > 16 ? w / 4 : 16, nh = h / 4 > 16 ? h / 4 : 16;
		int q = (int)lround(L * 1000.0);
		if (!server->boot_night || q != server->boot_night_q || nw != server->boot_night_w
			|| nh != server->boot_night_h) {
			struct desk_buf *buf = buf_create(nw, nh);
			cairo_save(buf->cr);
			cairo_scale(buf->cr, (double)nw / w, (double)nh / h);
			desk_draw_night(buf->cr, w, h, L);
			cairo_restore(buf->cr);
			scene_put(&server->boot_night, server->layer_top, buf, 0, 0, NULL);
			if (server->boot_night) {
				/* it is the dark in the room, not something you can click */
				server->boot_night->point_accepts_input = shadow_accepts;
			}
			server->boot_night_q = q;
			server->boot_night_w = nw;
			server->boot_night_h = nh;
		}
		if (server->boot_night) {
			wlr_scene_buffer_set_dest_size(server->boot_night, w, h);
			wlr_scene_node_set_enabled(&server->boot_night->node, true);
			wlr_scene_node_raise_to_top(&server->boot_night->node);
		}
	} else if (server->boot_night) {
		wlr_scene_node_set_enabled(&server->boot_night->node, false);
	}
	/* and what of the clock shines through the dark */
	double box[4];
	struct desk_sprite *sp = &server->boot_glow;
	if (dark && desk_clock_glow_box(w, h, box)) {
		uint64_t key = face ^ ((uint64_t)w << 16) ^ (uint64_t)h;
		if (!sp->drawn || sp->key != key) {
			sp->key = key;
			struct boot_glow_args a = { w, h };
			sprite_paint(server, sp, server->layer_top, box, boot_glow_draw, &a);
		}
		if (sp->node) {
			double a = desk_night_alpha_at(w, h, L, box[0] + box[2] * 0.5,
				box[1] + box[3] * 0.5);
			wlr_scene_buffer_set_opacity(sp->node, (float)clampf_d(a, 0.0, 1.0));
			wlr_scene_node_set_enabled(&sp->node->node, true);
			wlr_scene_node_raise_to_top(&sp->node->node);
		}
	} else if (sp->node) {
		wlr_scene_node_set_enabled(&sp->node->node, false);
	}
}

/* The dark, and the clock over it, as the lamp is now. */
static void boot_paint(struct desk_server *server)
{
	/* the clock, counting */
	int shown = (int)floor(server->boot_p * 100.0 + 1e-6) * 8 + server->boot_stage;
	if (shown != server->boot_shown) {
		server->boot_shown = shown;
		clock_publish(server);
	}
	night_paint(server, server->boot_light, (uint64_t)shown << 32);
}

/* the lamp brought down for a show, and the clock through it */
static void show_paint_night(struct desk_server *server, double L)
{
	night_paint(server, L, desk_clock_key(time(NULL)) | 1u);
}

/* ---- another wood: the lamp down, the room traced in the dark, the eyes
 * opening on it ----------------------------------------------------------
 *
 * Shell Settings' "What the desk is made of" or "Stain" changed: the lamp
 * goes down slowly over the room as it is (the dark coming in from the
 * corners to the pool under it, as the lamp comes up at login the other way);
 * in the dark the room is traced again in the new wood, the clock counting
 * how far ("DESK" and the blocks, as at login); and once it is there, the
 * dark goes as closed eyes open on it -- the lids parting from a line across
 * the middle, a blink, and wide. In 2D, at the door, or with the lamp down
 * for something else, it is changed at once, as before. */

#define RELIGHT_DOWN_S 1.6   /* the lamp going down */
#define RELIGHT_HOLD_S 0.4   /* the dark a moment at least, and the count full a moment */
#define RELIGHT_OPEN_S 2.2   /* the eyes opening */

static double lock_ease(double t);

/* How far the eyes are open, q of the way through their opening: shut a
 * moment, half open, a blink, and wide. */
static double lids_open_at(double q)
{
	static const double K[][2] = { { 0.0, 0.0 }, { 0.10, 0.0 }, { 0.34, 0.42 }, { 0.48, 0.14 },
		{ 1.0, 1.0 } };
	int n = (int)(sizeof K / sizeof K[0]);
	for (int i = 1; i < n; i++) {
		if (q <= K[i][0]) {
			double f = lock_ease((q - K[i - 1][0]) / (K[i][0] - K[i - 1][0]));
			return K[i - 1][1] + (K[i][1] - K[i - 1][1]) * f;
		}
	}
	return 1.0;
}

/* The lids over everything, open that far (1, none): all of it soft, so a
 * quarter of the screen, stretched. Not something to click. */
static void lids_paint(struct desk_server *server, double open)
{
	int w = server->desk_w, h = server->desk_h;
	if (open >= 0.999 || w < 8 || h < 8) {
		if (server->lids) {
			wlr_scene_node_set_enabled(&server->lids->node, false);
		}
		return;
	}
	int nw = w / 4 > 16 ? w / 4 : 16, nh = h / 4 > 16 ? h / 4 : 16;
	struct desk_buf *buf = buf_create(nw, nh);
	cairo_save(buf->cr);
	cairo_scale(buf->cr, (double)nw / w, (double)nh / h);
	desk_draw_lids(buf->cr, w, h, open);
	cairo_restore(buf->cr);
	scene_put(&server->lids, server->layer_top, buf, 0, 0, NULL);
	if (server->lids) {
		server->lids->point_accepts_input = shadow_accepts;
		wlr_scene_buffer_set_dest_size(server->lids, w, h);
		wlr_scene_node_set_enabled(&server->lids->node, true);
		wlr_scene_node_raise_to_top(&server->lids->node);
	}
}

/* All of it over: the lamp up, the lids gone, the clock telling the time. */
static void relight_end(struct desk_server *server)
{
	if (!server->relight) {
		return;
	}
	if (server->relight == RELIGHT_OPEN) {
		wlr_log(WLR_INFO, "the eyes are open on the desk's new wood (%.1f s since it was chosen)",
			real_sec() - server->relight_chosen);
	}
	server->relight = RELIGHT_NONE;
	lids_paint(server, 1.0);
	night_paint(server, 1.0, 0);
	clock_publish(server);
	server_schedule_frames(server);
}

/* In the dark from now, the room being traced again in the wood as it is. */
static void relight_dark(struct desk_server *server)
{
	wood_apply(server);
	gfx_switch(server);
	server->relight = RELIGHT_DARK;
	server->relight_t0 = monotonic_sec();
	server->relight_p = 0.0;
	server->relight_shown = -1;
	server->relight_ready_at = 0.0;
	server->relight_counting = false;
	lids_paint(server, 1.0);
	night_paint(server, 0.0, 2u);
	clock_publish(server);
	server_schedule_frames(server);
}

static void relight_begin(struct desk_server *server)
{
	if (server->relight == RELIGHT_DOWN) {
		return;   /* on its way down: traced in whatever it is by the time it is dark */
	}
	if (server->relight == RELIGHT_DARK || server->relight == RELIGHT_OPEN) {
		/* in the dark (or the eyes opening on the last): shut again, traced again */
		relight_dark(server);
		return;
	}
	if (gfx_flat() || locked(server) || server->boot_on || server->show || server->desk_w < 8
		|| server->desk_h < 8) {
		wood_apply(server);
		gfx_switch(server);
		return;
	}
	server->relight = RELIGHT_DOWN;
	server->relight_t0 = monotonic_sec();
	server->relight_chosen = real_sec();
	/* somebody is waiting in the dark for it: all the machine has (desk_bake_calm) */
	desk_bake_calm(false);
	wlr_log(WLR_INFO, "another wood chosen: the lamp goes down, and the room is traced in the dark");
	server_schedule_frames(server);
}

static void relight_tick(struct desk_server *server)
{
	if (!server->relight) {
		return;
	}
	int w = server->desk_w, h = server->desk_h;
	double now = monotonic_sec(), t = now - server->relight_t0;
	if (locked(server) || server->boot_on || server->show || w < 8 || h < 8) {
		/* the door shut on it, or the lamp wanted for something else: the rest at once */
		if (server->relight == RELIGHT_DOWN) {
			wood_apply(server);
			gfx_switch(server);
		}
		relight_end(server);
		return;
	}
	if (server->relight == RELIGHT_DOWN) {
		double q = clampf_d(t / RELIGHT_DOWN_S, 0.0, 1.0);
		night_paint(server, 1.0 - lock_ease(q), desk_clock_key(time(NULL)) | 1u);
		if (q >= 1.0) {
			relight_dark(server);
		}
		server_schedule_frames(server);
	} else if (server->relight == RELIGHT_DARK) {
		/* the tick comes back for it (tick_cb): nothing moves but the count */
		bool ready = desk_scene_ready(w, h);
		if (ready && !server->icons_shown) {
			room_landed(server);
		}
		/* the tracer's count is the last trace's (or the views') until this
		 * one has begun; meanwhile it creeps, a little */
		double traced = desk_scene_progress();
		server->relight_counting = server->relight_counting || traced < 0.95;
		double real = ready ? 1.0 : fmin(0.99, fmax(server->relight_counting ? traced : 0.0,
			0.30 * (1.0 - exp(-t / 5.0))));
		server->relight_p = fmax(server->relight_p, real);
		int shown = (int)floor(server->relight_p * 100.0 + 1e-6);
		if (shown != server->relight_shown) {
			server->relight_shown = shown;
			clock_publish(server);
		}
		night_paint(server, 0.0, ((uint64_t)shown << 32) | 2u);
		if (!ready) {
			server->relight_ready_at = 0.0;
		} else if (server->relight_ready_at <= 0.0) {
			server->relight_ready_at = now;
		}
		if (ready && t >= RELIGHT_HOLD_S && now - server->relight_ready_at >= RELIGHT_HOLD_S) {
			wlr_log(WLR_INFO, "the room is traced in the new wood (%.1f s in the dark): the eyes open",
				t);
			desk_bake_calm(true);
			server->relight = RELIGHT_OPEN;
			server->relight_t0 = now;
			lids_paint(server, 0.0);
			night_paint(server, 1.0, 0);
			clock_publish(server);
			server_schedule_frames(server);
		}
	} else if (server->relight == RELIGHT_OPEN) {
		double q = t / RELIGHT_OPEN_S;
		if (q >= 1.0) {
			relight_end(server);
			return;
		}
		lids_paint(server, lids_open_at(q));
		server_schedule_frames(server);
	}
}

static void boot_tick(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (!server->boot_on || w < 8 || h < 8) {
		return;
	}
	double now = monotonic_sec();
	if (server->boot_t0 <= 0.0) {
		server->boot_t0 = now;
		server->boot_at = now;
		server->boot_real0 = real_sec();
	}
	double dt = clampf_d(now - server->boot_at, 0.0, 0.1);
	server->boot_at = now;
	/* how far the room really is -- and whether there is any desk to light
	 * meanwhile: a first look traced small (room_preview), on a machine
	 * that traces on its processor */
	bool desk = desk_scene_ready(w, h);
	bool shown = desk || desk_room_shown(w, h) != 0;
	if (desk && !server->icons_shown) {
		/* up the moment it is there, not on the next idle tick: the bare
		 * boards would show through the dark */
		room_landed(server);
	}
	bool views = desk && desk_scene_views_ready(w, h);
	/* each view as it comes, for the log: on a machine that traces on its
	 * processor the bookcase is minutes ahead of the post box */
	for (int v = 0; desk && v < DESK_VIEW_COUNT; v++) {
		static const char *const NAME[DESK_VIEW_COUNT] = { "the bookcase",
			"the Control Panel", "the post box" };
		if (!(server->views_logged & (1u << v)) && desk_scene_view_ready(w, h, v)) {
			server->views_logged |= 1u << v;
			wlr_log(WLR_INFO, "%s is up (%.1f s since the start)", NAME[v],
				real_sec() - g_start_real);
		}
	}
	int view = -1;
	/* the desk's own count says nothing while a GPU is still being got
	 * ready to trace it -- seconds, sometimes -- so it creeps meanwhile, a
	 * little and never past a third of its share, rather than sit at 0 */
	double dp = desk ? 1.0 : fmax(desk_scene_progress(),
		0.30 * (1.0 - exp(-(now - server->boot_t0) / 5.0)));
	double vp = views ? 1.0 : desk_scene_views_progress(&view);
	double wp = !views ? 0.0 : server->warm_step > 7 ? 1.0 : server->warm_step / 8.0;
	double real = 0.45 * dp + 0.40 * vp + 0.15 * wp;
	(void)view;
	if (desk && server->boot_desk_at <= 0.0) {
		server->boot_desk_at = real_sec();
	}
	/* the pictures made ahead of a first look, quicker than the idle tick
	 * makes them while there is a count waiting on them -- each view's as
	 * it comes (views_prewarm waits for the one it needs) */
	if (desk && server->warm_step <= 7 && now - server->boot_warm_at >= BOOT_WARM_S) {
		server->boot_warm_at = now;
		views_prewarm(server);
	}
	/* the count: toward how far it really is, no quicker than the lamp may
	 * come up, and no jumps */
	double ramp = server->boot_lamp ? (now - server->boot_t0) / boot_light_s() : 1.0;
	double target = fmin(real, ramp);
	if (target > server->boot_p) {
		server->boot_p = fmin(target, server->boot_p + dt * BOOT_RATE);
	}
	/* the lamp, as far as the count -- and all the way once the desk is
	 * there, whatever is still being made */
	if (shown) {
		server->boot_floor = fmin(1.0, server->boot_floor + dt / boot_light_s());
	}
	/* what the clock says is being made: as far as it has counted, which is
	 * never further than the work has really got -- the desk, the three
	 * views a third of their share each, the lamp, and ready */
	double p = server->boot_p;
	server->boot_stage = p < 0.45 ? 0 : p < 0.583 ? 1 : p < 0.717 ? 2 : p < 0.85 ? 3
		: p < 0.999 ? 4 : 5;
	/* With no desk yet at all (its first run at this size) the room stays
	 * dark whatever the lamp is set to, and the clock -- where it will
	 * stand -- is what says how far along it is. */
	double L = server->boot_lamp || !desk ? desk_boot_light(server->boot_p) : 1.0;
	L = fmax(L, server->boot_floor);
	server->boot_light = L;
	if (L >= 0.999 && !server->boot_lit) {
		server->boot_lit = true;
		wlr_log(WLR_INFO, "the light is on (%.1f s since the start)", real_sec() - g_start_real);
	}
	if (server->boot_p >= 0.999 && real >= 0.999) {
		if (server->boot_done_at <= 0.0) {
			server->boot_done_at = now;
		}
		if (now - server->boot_done_at >= BOOT_HOLD_S) {
			boot_end(server);
			return;
		}
	}
	boot_paint(server);
	first_letter_tick(server, L);
	/* Frame after frame only while the lamp is coming up. Waiting on the
	 * room, nothing moves but the count, which the tick comes back for
	 * (tick_cb): a frame every frame meant the dark over the whole screen
	 * put together sixty times a second, on the CPU the room was being
	 * read back or traced with -- on an old machine, most of it. */
	if (shown && L < 0.999) {
		server_schedule_frames(server);
	}
}

/* ---- Wood Maintenance: termites -----------------------------------------
 *
 * Shell Settings' Wood Maintenance, off unless chosen: the desk left alone a
 * while (termites_wait), termites come out onto it -- one, a few, or a lot
 * of them -- walk to the wood, or to the edge of a sheet or a packet lying
 * on it, and nibble there in short bursts, and move on. The pointer moves,
 * and they run off the screen. What they leave is only a picture: a worn
 * spot and crumbs in the wood, under the paper and moved with it, kept
 * between visits and sessions until Restore Desk ($XDG_STATE_HOME/desk/
 * wood-damage-v1.txt, as fractions of the screen); a bitten edge on a sheet
 * or packet, which is on that sheet -- moves with it, goes with it -- and
 * is not kept. Nothing comes out at the door, in the dark, over the whole
 * screen, while something holds the screen awake, or with the desk away
 * (the bookcase, the Control Panel, the post box, the drawer out).
 * (barenakedchicken's Wood Maintenance, PR #14 and the AiO v0.7 --
 * dentarthurdent on Discord. There the marks were one picture over
 * everything, and stayed where the sheets had been.) */

#define TERMITES_MS 66          /* how often they move, while they are out */
#define TM_PI 3.14159265358979
#define NIBBLES_PER_SHEET 24

static char nibble_tag;         /* a mark on a sheet, by its node's data */

static uint32_t tm_rand(uint32_t *state)
{
	uint32_t x = *state ? *state : 0x6d2b79f5u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static double tm_unit(uint32_t *state)
{
	return (double)(tm_rand(state) & 0x00ffffffu) / 16777216.0;
}

static double tm_pt(const struct desk_server *server)
{
	return desk_px_per_pt(server->desk_w);
}

static bool wood_scars_path(char *out, size_t n, bool make)
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
		for (char *p = dir + 1; *p; p++) {
			if (*p == '/') {
				*p = '\0';
				mkdir(dir, 0700);
				*p = '/';
			}
		}
		mkdir(dir, 0700);
	}
	return (size_t)snprintf(out, n, "%s/wood-damage-v1.txt", dir) < n;
}

/* Kept: written whole, to a new file put in the old one's place. Not kept,
 * or none: no file. */
static void wood_scars_save(struct desk_server *server)
{
	server->scars_unsaved = false;
	char path[600];
	if (!wood_scars_path(path, sizeof path, server->scars_n > 0 && server->conf.termites_keep)) {
		return;
	}
	if (!server->conf.termites_keep || server->scars_n == 0 || server->desk_w < 100
		|| server->desk_h < 100) {
		unlink(path);
		return;
	}
	char tmp[640];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		return;
	}
	bool ok = fprintf(f, "DESK_WOOD_DAMAGE_V1\n") > 0;
	for (int i = 0; i < server->scars_n && ok; i++) {
		const struct wood_scar *m = &server->scars[i];
		/* (as the AiO kept them: edge 4 and not paper, the wood) */
		ok = fprintf(f, "%.6f %.6f %.4f %u 4 0\n", m->x / server->desk_w,
			m->y / server->desk_h, m->bite, m->seed) > 0;
	}
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
	}
}

/* Read once the desk has a size. The AiO's marks on paper, kept where the
 * sheets were, are left out. */
static void wood_scars_load(struct desk_server *server)
{
	if (server->scars_loaded || server->desk_w < 100 || server->desk_h < 100) {
		return;
	}
	server->scars_loaded = true;
	server->scars_dirty = true;
	server->scars_w = server->desk_w;
	server->scars_h = server->desk_h;
	char path[600];
	if (!server->conf.termites_keep || !wood_scars_path(path, sizeof path, false)) {
		return;
	}
	FILE *f = fopen(path, "re");
	if (!f) {
		return;
	}
	char line[256];
	if (fgets(line, sizeof line, f) && !strcmp(line, "DESK_WOOD_DAMAGE_V1\n")) {
		while (server->scars_n < WOOD_SCARS_MAX && fgets(line, sizeof line, f)) {
			double x, y, bite;
			unsigned seed;
			int edge, paper;
			if (sscanf(line, "%lf %lf %lf %u %d %d", &x, &y, &bite, &seed, &edge, &paper) != 6
				|| !(x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0 && bite >= 0.035
					&& bite <= 1.0) || paper) {
				continue;
			}
			server->scars[server->scars_n++] = (struct wood_scar){ x * server->desk_w,
				y * server->desk_h, bite, seed };
		}
	}
	fclose(f);
}

/* A worn spot in the wood and the crumbs round it, at x, y. */
static void nibble_wood(cairo_t *cr, double x, double y, double bite, uint32_t seed, double pt)
{
	cairo_set_source_rgba(cr, 0.17, 0.08, 0.025, 0.23 * bite);
	cairo_arc(cr, x, y, (2.0 + 5.0 * bite) * pt, 0, 2.0 * TM_PI);
	cairo_fill(cr);
	for (int j = 0; j < 5; j++) {
		uint32_t s = seed ^ ((uint32_t)(j + 1) * 0x9e3779b9u);
		double dx = (tm_unit(&s) - 0.5) * 21.0 * pt, dy = (tm_unit(&s) - 0.5) * 16.0 * pt;
		cairo_set_source_rgba(cr, 0.78, 0.57, 0.28, 0.25 + 0.32 * bite);
		cairo_arc(cr, x + dx, y + dy, (0.35 + 0.6 * tm_unit(&s)) * bite * pt, 0, 2.0 * TM_PI);
		cairo_fill(cr);
	}
}

/* A bite out of the edge of a sheet, at x, y on that edge (0 top, 1 right,
 * 2 bottom, 3 left): the wood showing through, and a torn fibre. */
static void nibble_paper(cairo_t *cr, double x, double y, double bite, int edge, double pt)
{
	double r = (1.4 + 9.0 * bite) * pt;
	cairo_save(cr);
	cairo_translate(cr, x, y);
	/* drawn for the left edge, turned for the others */
	cairo_rotate(cr, edge == 0 ? TM_PI * 0.5 : edge == 1 ? TM_PI : edge == 2 ? -TM_PI * 0.5 : 0.0);
	cairo_set_source_rgba(cr, 0.24, 0.13, 0.067, 0.91);
	cairo_move_to(cr, 0, -r * 0.75);
	cairo_line_to(cr, r * 0.40, -r * 0.35);
	cairo_line_to(cr, r * 0.92, -r * 0.06);
	cairo_line_to(cr, r * 0.75, r * 0.47);
	cairo_line_to(cr, r * 0.15, r * 0.63);
	cairo_line_to(cr, 0, r * 0.75);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.82, 0.63, 0.32, 0.55);
	cairo_set_line_width(cr, 0.8 * pt);
	cairo_move_to(cr, r * 0.10, r * 0.70);
	cairo_line_to(cr, r * 0.60, r * 0.45);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* The marks in the wood drawn again -- only when one is new or deeper, the
 * screen has changed size (they keep their place on it), or Restore. */
static void wood_scars_paint(struct desk_server *server)
{
	wood_scars_load(server);
	if (!server->layer_scars || !server->scars_loaded) {
		return;
	}
	if (server->scars_w != server->desk_w || server->scars_h != server->desk_h) {
		if (server->scars_w > 0 && server->scars_h > 0 && server->desk_w > 0) {
			for (int i = 0; i < server->scars_n; i++) {
				server->scars[i].x *= (double)server->desk_w / server->scars_w;
				server->scars[i].y *= (double)server->desk_h / server->scars_h;
			}
		}
		server->scars_w = server->desk_w;
		server->scars_h = server->desk_h;
		server->scars_dirty = true;
	}
	if (!server->scars_dirty) {
		return;
	}
	server->scars_dirty = false;
	if (server->scars_n == 0) {
		if (server->scars_node) {
			wlr_scene_node_destroy(&server->scars_node->node);
			server->scars_node = NULL;
		}
		server_schedule_frames(server);
		return;
	}
	/* drawn no wider than 1152: worn spots and crumbs, not print */
	double k = fmin(1.0, 1152.0 / server->desk_w);
	struct desk_buf *b = buf_create((int)ceil(server->desk_w * k), (int)ceil(server->desk_h * k));
	cairo_scale(b->cr, k, k);
	double pt = tm_pt(server);
	for (int i = 0; i < server->scars_n; i++) {
		const struct wood_scar *m = &server->scars[i];
		nibble_wood(b->cr, m->x, m->y, m->bite, m->seed, pt);
	}
	scene_put(&server->scars_node, server->layer_scars, b, 0, 0, NULL);
	wlr_scene_buffer_set_dest_size(server->scars_node, server->desk_w, server->desk_h);
	server->scars_node->point_accepts_input = shadow_accepts;
	server_schedule_frames(server);
}

/* The mark a termite is making, as deep as it is now: in the wood, or on
 * its sheet (made there the first time). */
static void termite_mark(struct desk_server *server, struct termite *a)
{
	if (a->bite < 0.035) {
		return;
	}
	if (!a->view) {
		if (a->scar < 0) {
			a->scar = server->scars_n < WOOD_SCARS_MAX ? server->scars_n++
				: server->scars_next++ % WOOD_SCARS_MAX;
		}
		server->scars[a->scar] = (struct wood_scar){ a->tx, a->ty, a->bite, a->seed };
		server->scars_dirty = true;
		server->scars_unsaved = true;
		return;
	}
	struct wlr_scene_tree *on = a->packet ? a->view->sheet.tree : a->view->scene_tree;
	if (!on) {
		return;
	}
	double pt = tm_pt(server);
	int m = (int)ceil(32.0 * pt);
	struct desk_buf *b = buf_create(m, m);
	nibble_paper(b->cr, m * 0.5, m * 0.5, a->bite, a->edge, pt);
	if (!a->mark) {
		a->view->nibbles++;
	}
	scene_put(&a->mark, on, b, (int)lround(a->tx - on->node.x - m * 0.5),
		(int)lround(a->ty - on->node.y - m * 0.5), &nibble_tag);
	a->mark->point_accepts_input = shadow_accepts;
	wlr_scene_node_raise_to_top(&a->mark->node);
}

/* Sheets and packets lying open to a termite: their outer boxes on the
 * screen, and whose. */
struct tm_snack {
	double x, y, w, h;
	struct desk_toplevel *view;
	bool packet;
};

static int termite_snacks(struct desk_server *server, struct tm_snack *out, int cap)
{
	int n = 0;
	struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		if (n >= cap || v->nibbles >= NIBBLES_PER_SHEET || v->unmanaged) {
			continue;
		}
		const struct desk_sheet *s = &v->sheet;
		if (server->conf.termites_packets && s->phase == SHEET_PARKED && !s->in_drawer
			&& s->tree && s->tree->node.enabled && s->hit_w >= 34 && s->hit_h >= 20) {
			out[n++] = (struct tm_snack){ s->hit_x, s->hit_y, s->hit_w, s->hit_h, v, true };
		} else if (server->conf.termites_windows && s->phase == SHEET_IDLE && v->scene_tree
			&& v->scene_tree->node.enabled && !v->fullscreen && v->outer_w >= 95
			&& v->outer_h >= 70) {
			out[n++] = (struct tm_snack){ v->scene_tree->node.x, v->scene_tree->node.y,
				v->outer_w, v->outer_h, v, false };
		}
	}
	return n;
}

/* Somewhere new to nibble: an edge of a sheet, mostly, if any is open to
 * them, else the wood -- the desk's own, between the tray and the drawer. */
static void termite_pick(struct desk_server *server, struct termite *a,
	const struct tm_snack *snacks, int count)
{
	termite_mark(server, a);
	a->view = NULL;
	a->packet = false;
	a->mark = NULL;
	a->scar = -1;
	a->bite = 0.0;
	a->bursts = 0;
	a->seed = tm_rand(&a->rng);
	if (count > 0 && tm_unit(&a->rng) < 0.72) {
		const struct tm_snack *s = &snacks[tm_rand(&a->rng) % (unsigned)count];
		a->view = s->view;
		a->packet = s->packet;
		a->edge = (int)(tm_rand(&a->rng) % 4u);
		double f = 0.12 + 0.76 * tm_unit(&a->rng);
		switch (a->edge) {
		case 0: a->tx = s->x + s->w * f; a->ty = s->y + 3.0; break;
		case 1: a->tx = s->x + s->w - 3.0; a->ty = s->y + s->h * f; break;
		case 2: a->tx = s->x + s->w * f; a->ty = s->y + s->h - 3.0; break;
		default: a->tx = s->x + 3.0; a->ty = s->y + s->h * f; break;
		}
	} else {
		struct wlr_box wa = work_area(server);
		a->edge = 4;
		a->tx = wa.x + wa.width * (0.08 + 0.84 * tm_unit(&a->rng));
		a->ty = wa.y + wa.height * (0.45 + 0.50 * tm_unit(&a->rng));
	}
	a->tx = clampf_d(a->tx, 12.0, server->desk_w - 12.0);
	a->ty = clampf_d(a->ty, 12.0, server->desk_h - 12.0);
	a->stage = 0;
}

/* Its sheet gone, or folded, opened, put away: its mark there is that
 * sheet's (and gone with it); it goes and finds somewhere else. */
static void termite_lost(struct desk_server *server, struct termite *a)
{
	a->view = NULL;
	a->mark = NULL;
	a->bite = 0.0;
	termite_pick(server, a, NULL, 0);
}

static bool termite_target_there(const struct desk_server *server, const struct termite *a)
{
	if (!a->view) {
		return true;
	}
	const struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		if (v == a->view) {
			return a->packet ? v->sheet.phase == SHEET_PARKED && v->sheet.tree
				&& !v->sheet.in_drawer
				: v->sheet.phase == SHEET_IDLE && v->scene_tree && !v->fullscreen;
		}
	}
	return false;
}

/* A sheet going: no termite left holding on to it. */
static void termites_forget_view(struct desk_server *server, struct desk_toplevel *view)
{
	for (int i = 0; i < server->tm_n; i++) {
		if (server->tm[i].view == view) {
			termite_lost(server, &server->tm[i]);
		}
	}
}

/* WALK, INSPECT, CHEW, PAUSE: it stops at the place rather than going round
 * it, and nibbles in short bursts, its mark there deeper after each. */
static void termite_step(struct desk_server *server, struct termite *a, double now, double dt,
	const struct tm_snack *snacks, int count)
{
	switch (a->stage) {
	case 0: {
		double dx = a->tx - a->x, dy = a->ty - a->y, d = hypot(dx, dy);
		if (d <= 2.6) {
			a->x = a->tx;
			a->y = a->ty;
			a->stage = 1;
			a->at = now + 0.15 + 0.48 * tm_unit(&a->rng);
			break;
		}
		double move = fmin(d, dt * a->pace);
		double side = (tm_unit(&a->rng) - 0.5) * fmin(1.5, move * 0.10);
		a->x += move * dx / d - side * dy / d;
		a->y += move * dy / d + side * dx / d;
		a->heading = atan2(dy, dx);
		a->legs += move * 0.16;
		break;
	}
	case 1:
		if (now >= a->at) {
			a->stage = 2;
			a->at = now + 0.30 + 0.70 * tm_unit(&a->rng);
		}
		break;
	case 2:
		a->bite = fmin(1.0, a->bite + dt * (0.09 + 0.05 * tm_unit(&a->rng)));
		a->legs += dt * 0.45;
		if (now >= a->at) {
			a->bursts++;
			termite_mark(server, a);
			a->stage = 3;
			a->at = now + 0.20 + 0.85 * tm_unit(&a->rng);
		}
		break;
	default:
		if (now < a->at) {
			break;
		}
		if (a->bursts >= 3 && tm_unit(&a->rng) < 0.42) {
			termite_pick(server, a, snacks, count);
		} else if (tm_unit(&a->rng) < 0.52) {
			/* along the edge a little, and a new mark there */
			double drift = (tm_unit(&a->rng) - 0.5) * 27.0;
			a->mark = NULL;
			a->scar = -1;
			a->bite = 0.0;
			a->seed = tm_rand(&a->rng);
			if (a->edge == 0 || a->edge == 2 || a->edge == 4) {
				a->tx = clampf_d(a->tx + drift, 12.0, server->desk_w - 12.0);
			} else {
				a->ty = clampf_d(a->ty + drift, 12.0, server->desk_h - 12.0);
			}
			a->stage = 0;
		} else {
			a->stage = 2;
			a->at = now + 0.30 + 0.80 * tm_unit(&a->rng);
		}
		break;
	}
}

/* The termite itself, small: its legs going as it walks. */
static void termite_draw(cairo_t *cr, double x, double y, double a, double t, double size)
{
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_rotate(cr, a);
	cairo_scale(cr, size, size);
	cairo_set_source_rgba(cr, 0.08, 0.05, 0.025, 0.30);
	cairo_save(cr);
	cairo_translate(cr, 0.7, 1.15);
	cairo_scale(cr, 1.65, 0.72);
	cairo_arc(cr, 0, 0, 1.0, 0, 2.0 * TM_PI);
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_set_line_width(cr, 0.22);
	cairo_set_source_rgb(cr, 0.52, 0.31, 0.13);
	for (int side = -1; side <= 1; side += 2) {
		for (int leg = 0; leg < 3; leg++) {
			double bx = -1.0 + leg * 0.95;
			double step = sin(t * 10.0 + leg * 2.2 + side * 1.2) * 0.38;
			cairo_move_to(cr, bx, side * 0.33);
			cairo_line_to(cr, bx + step - 0.15, side * 1.35);
			cairo_line_to(cr, bx + step + 0.45, side * 1.65);
		}
	}
	cairo_stroke(cr);
	cairo_set_source_rgb(cr, 0.73, 0.49, 0.22);
	cairo_save(cr);
	cairo_scale(cr, 1.42, 0.83);
	cairo_arc(cr, -0.55, 0, 1.05, 0, 2.0 * TM_PI);
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_set_source_rgb(cr, 0.90, 0.67, 0.34);
	cairo_arc(cr, 0.55, 0, 0.76, 0, 2.0 * TM_PI);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.63, 0.39, 0.16);
	cairo_arc(cr, 1.43, 0, 0.56, 0, 2.0 * TM_PI);
	cairo_fill(cr);
	cairo_set_line_width(cr, 0.17);
	for (int s = -1; s <= 1; s += 2) {
		cairo_move_to(cr, 1.8, s * 0.2);
		cairo_curve_to(cr, 2.0, s * 0.8, 2.6, s * 1.25, 2.8, s * 1.1);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

static void termite_show(struct desk_server *server, struct termite *a, int i, bool feeding)
{
	double pt = tm_pt(server);
	double size = (2.05 + (i % 4) * 0.29) * pt;
	int m = (int)ceil(size * 7.0);
	double x = a->x, y = a->y, heading = a->heading;
	if (feeding) {
		/* nervous nibbles, facing into the sheet */
		x += (tm_unit(&a->rng) - 0.5) * 0.9;
		y += (tm_unit(&a->rng) - 0.5) * 0.9;
		if (a->view) {
			heading = a->edge == 0 ? TM_PI * 0.5 : a->edge == 1 ? TM_PI
				: a->edge == 2 ? -TM_PI * 0.5 : 0.0;
		}
	}
	struct desk_buf *b = buf_create(m, m);
	termite_draw(b->cr, m * 0.5, m * 0.5, heading, a->legs, size);
	scene_put(&a->node, server->layer_bugs, b, (int)lround(x - m * 0.5), (int)lround(y - m * 0.5),
		NULL);
	a->node->point_accepts_input = shadow_accepts;
}

/* Gone: the termites off the screen, what they made there finished, and
 * the wood's marks kept -- or, not kept, cleaned up. */
static void termites_away(struct desk_server *server)
{
	for (int i = 0; i < server->tm_n; i++) {
		struct termite *a = &server->tm[i];
		termite_mark(server, a);
		if (a->node) {
			wlr_scene_node_destroy(&a->node->node);
			a->node = NULL;
		}
	}
	bool were = server->tm_n > 0 || server->tm_on;
	server->tm_n = 0;
	server->tm_on = false;
	server->tm_preview = false;
	server->tm_preview_at = 0.0;
	server->tm_flee_until = 0.0;
	if (server->tm_timer) {
		wl_event_source_timer_update(server->tm_timer, 0);
	}
	if (were && !server->conf.termites_keep) {
		server->scars_n = 0;
		server->scars_next = 0;
		server->scars_dirty = true;
	}
	if (server->scars_unsaved || (were && !server->conf.termites_keep)) {
		wood_scars_save(server);
	}
	wood_scars_paint(server);
	server_schedule_frames(server);
}

/* Restore Desk: no marks anywhere, in the wood or on the paper. */
static void termites_restore(struct desk_server *server)
{
	termites_away(server);
	server->scars_n = 0;
	server->scars_next = 0;
	server->scars_dirty = true;
	wood_scars_save(server);
	wood_scars_paint(server);
	struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		struct wlr_scene_tree *trees[2] = { v->scene_tree, v->sheet.tree };
		for (int t = 0; t < 2; t++) {
			if (!trees[t]) {
				continue;
			}
			struct wlr_scene_node *child, *tmp;
			wl_list_for_each_safe(child, tmp, &trees[t]->children, link) {
				if (child->data == &nibble_tag) {
					wlr_scene_node_destroy(child);
				}
			}
		}
		v->nibbles = 0;
	}
	wlr_log(WLR_INFO, "termites: the desk restored");
}

/* Out, or not: the desk left long enough (or the preview), and nothing in
 * the way of them. */
static bool termites_wanted(struct desk_server *server, double now)
{
	if ((!server->conf.termites && !server->tm_preview) || !server->layer_bugs || server->dark
		|| locked(server) || server->idle_held || shelf_up(server) || console_up(server)
		|| post_up(server) || paper_moved(server) || server->desk_w < 100
		|| server->desk_h < 100 || server->sw_on || server->sw_shown || server->snip_on) {
		return false;
	}
	struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		if (v->fullscreen && v->scene_tree && v->scene_tree->node.enabled) {
			return false;
		}
	}
	return server->tm_preview || now - server->idle_at >= server->conf.termites_wait;
}

static int termites_cb(void *data);

/* Once a second (tick_cb), and while they are out, every TERMITES_MS. */
static void termites_tick(struct desk_server *server)
{
	wood_scars_paint(server);
	double now = real_sec();
	bool fleeing = server->tm_flee_until > 0.0;
	if (!fleeing && !termites_wanted(server, now)) {
		if (server->tm_on) {
			termites_away(server);
		}
		return;
	}
	if (!server->tm_timer) {
		server->tm_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), termites_cb, server);
	}
	double dt = clampf_d(now - server->tm_last, 0.0, 0.20);
	server->tm_last = now;
	if (fleeing) {
		/* off the screen, away from the hand, and gone */
		if (now >= server->tm_flee_until) {
			termites_away(server);
			return;
		}
		for (int i = 0; i < server->tm_n; i++) {
			struct termite *a = &server->tm[i];
			double dx = a->x - server->cursor->x, dy = a->y - server->cursor->y;
			if (hypot(dx, dy) < 5.0) {
				dx = (i & 1) ? 1.0 : -1.0;
				dy = 0.5;
			}
			a->heading = atan2(dy, dx);
			double speed = (210.0 + 47.0 * (i % 4)) * tm_pt(server);
			a->x += cos(a->heading) * speed * dt;
			a->y += sin(a->heading) * speed * dt;
			a->legs += speed * dt * 0.17;
			termite_show(server, a, i, false);
			if (a->node) {
				wlr_scene_buffer_set_opacity(a->node,
					(float)clampf_d((server->tm_flee_until - now) / 0.30, 0.0, 1.0));
			}
		}
	} else {
		/* a new visit: the termites come one after another, a few seconds
		 * apart; a preview all at once */
		double session = server->tm_preview ? -server->tm_preview_at : server->idle_at;
		if (!server->tm_on || session != server->tm_session) {
			bool preview = server->tm_preview;
			double asked = server->tm_preview_at;
			if (server->tm_on) {
				termites_away(server);
			}
			server->tm_preview = preview;
			server->tm_preview_at = asked;
			server->tm_on = true;
			server->tm_session = session;
			server->tm_level_seen = server->conf.termites_level;
			server->tm_seed = (uint32_t)(now * 1000.0) ^ 0x5bd1e995u;
		}
		int cap = server->conf.termites_level == 0 ? 1 : server->conf.termites_level == 2
			? TERMITES_MAX : 6;
		double age = server->tm_preview ? 1e9 : now - server->idle_at - server->conf.termites_wait;
		int wanted = cap == 1 ? 1 : 1 + (int)floor(age / 2.4);
		if (wanted > cap || server->tm_level_seen != server->conf.termites_level) {
			wanted = cap;
		}
		server->tm_level_seen = server->conf.termites_level;
		while (server->tm_n > wanted) {
			struct termite *a = &server->tm[--server->tm_n];
			termite_mark(server, a);
			if (a->node) {
				wlr_scene_node_destroy(&a->node->node);
				a->node = NULL;
			}
		}
		struct tm_snack snacks[24];
		int n = termite_snacks(server, snacks, 24);
		double pt = tm_pt(server);
		while (server->tm_n < wanted) {
			struct termite *a = &server->tm[server->tm_n++];
			*a = (struct termite){ .rng = tm_rand(&server->tm_seed), .scar = -1 };
			termite_pick(server, a, snacks, n);
			double angle = tm_unit(&a->rng) * 2.0 * TM_PI;
			double far = (38.0 + 95.0 * tm_unit(&a->rng)) * pt;
			a->x = clampf_d(a->tx + cos(angle) * far, 5.0, server->desk_w - 5.0);
			a->y = clampf_d(a->ty + sin(angle) * far, 5.0, server->desk_h - 5.0);
			a->pace = (35.0 + 40.0 * tm_unit(&a->rng)) * pt;
			a->heading = atan2(a->ty - a->y, a->tx - a->x);
		}
		for (int i = 0; i < server->tm_n; i++) {
			struct termite *a = &server->tm[i];
			if (!termite_target_there(server, a)) {
				termite_lost(server, a);
			}
			termite_step(server, a, now, dt, snacks, n);
			termite_show(server, a, i, a->stage == 2 || a->stage == 3);
			wlr_scene_buffer_set_opacity(a->node, 1.0f);
		}
	}
	wood_scars_paint(server);
	wl_event_source_timer_update(server->tm_timer, TERMITES_MS);
	server_schedule_frames(server);
}

static int termites_cb(void *data)
{
	termites_tick(data);
	return 0;
}

/* The pointer moved: whatever is out runs off (once; more moving does not
 * start it again). */
static void termites_disturb(struct desk_server *server)
{
	if (!server->tm_on || server->tm_flee_until > 0.0) {
		return;
	}
	server->tm_flee_until = real_sec() + 1.15;
	server->tm_preview = false;
	server->tm_preview_at = 0.0;
	termites_tick(server);
}

/* Shell Settings' Restore Desk and Preview, and pictures for the clear mat,
 * asked for in a file of their own beside the desk's socket -- one word,
 * read once and taken away. */
static void termites_requests(struct desk_server *server)
{
	const char *run = getenv("XDG_RUNTIME_DIR"), *sock = getenv("WAYLAND_DISPLAY");
	if (!run || !sock || strchr(sock, '/')) {
		return;
	}
	char path[512];
	snprintf(path, sizeof path, "%s/%s.termites", run, sock);
	FILE *f = fopen(path, "re");
	if (!f) {
		return;
	}
	char word[32] = "";
	if (!fgets(word, sizeof word, f)) {
		word[0] = '\0';
	}
	fclose(f);
	unlink(path);
	if (!strncmp(word, "restore", 7)) {
		termites_restore(server);
	} else if (!strncmp(word, "preview", 7)) {
		wlr_log(WLR_INFO, "termites: a preview");
		server->tm_preview_at = real_sec();
		server->tm_preview = true;
		termites_tick(server);
	} else if (!strncmp(word, "mat-pick", 8) || !strncmp(word, "mat-whole", 9)) {
		/* desk-mat chooses them, makes prints of them and lays them under the
		 * mat; the desk sees them come in its file (mat_papers_poll) */
		const char *words[] = { "pick", "--lang", desk_lang_code(desk_lang_get()),
			word[4] == 'w' ? "--whole" : NULL, NULL };
		desk_run_with("desk-mat", words);
	}
}

/* One of the round's chores, timed: the screen stands still while it is
 * done, so a round that is long says in the log which chore it was. */
static void tick_chore(struct desk_server *server, const char *what,
	void (*chore)(struct desk_server *))
{
	double t0 = monotonic_sec();
	chore(server);
	double ms = (monotonic_sec() - t0) * 1000.0;
	if (ms > server->tick_worst_ms) {
		server->tick_worst_ms = ms;
		server->tick_worst = what;
	}
}

static int tick_cb(void *data)
{
	struct desk_server *server = data;
	prof_report(real_sec());
	/* the lamp's count, while nothing else is moving (boot_tick, relight_tick) */
	if (server->boot_on || server->relight) {
		server_schedule_frames(server);
	}
	/* The room is ray traced on a worker. Check first, so that the tick it
	 * lands on is the tick that puts it up rather than the one after. */
	if (!server->icons_shown && desk_scene_ready(server->desk_w, server->desk_h)) {
		room_landed(server);
	} else if (!server->icons_shown) {
		/* Still tracing. Ask again every tick: a resize that landed while
		 * the tracer was busy is turned away, and nothing else would ever
		 * come back for it -- the desk would stay a bare plank for good. */
		desk_scene_warm(server->desk_w, server->desk_h, true);
		/* How far along it is is the clock's to say (desktop_tick); the
		 * desk under it is the same picture until there is a room to
		 * draw, or another (wait_*). */
		if (desktop_waiting_stale(server)) {
			desktop_paint(server);
			server_schedule_frames(server);
		}
	}
	double round_at = monotonic_sec();
	server->tick_worst = NULL;
	server->tick_worst_ms = 0.0;
	tick_chore(server, "the gadgets' faces", desktop_tick);
	tick_chore(server, "the views made ahead", views_prewarm);
	tick_chore(server, "the door's films", lock_films);
	tick_chore(server, "the clock", clock_tick);
	tick_chore(server, "the blank sheets", base_timeouts);
	tick_chore(server, "the settings", shell_conf_poll);
	tick_chore(server, "the mat's pictures", mat_papers_poll);
	tick_chore(server, "the termites", termites_requests);
	tick_chore(server, "the termites", termites_tick);
	/* The clock and the calendar live on the bookcase shelf now, and the
	 * shelf is only repainted when something about it changes -- and while
	 * you are looking at it, ten times a second. The seconds hand is
	 * thrown at each mark and rings about it before it settles, and at one
	 * frame a second there is no throw and no ring, only a picture of a
	 * clock that differs from the last picture of a clock. */
	bool shelf = shelf_up(server);
	if (shelf) {
		double t0 = monotonic_sec();
		paint_shelf(server);
		server_schedule_frames(server);
		/* the book's stills for the next turn, while it lies still */
		book_ahead(server);
		double ms = (monotonic_sec() - t0) * 1000.0;
		if (ms > server->tick_worst_ms) {
			server->tick_worst_ms = ms;
			server->tick_worst = "the bookcase's shelf";
		}
	}
	/* A round of a tenth of a second is a hitch anybody can see; it is
	 * put in the log (once a minute at most), and a desk-report shows it. */
	double round_ms = (monotonic_sec() - round_at) * 1000.0;
	if (round_ms >= 100.0 && real_sec() - server->tick_said_at >= 60.0) {
		server->tick_said_at = real_sec();
		wlr_log(WLR_INFO, "the once-a-second round took %.0f ms, the screen standing still "
			"meanwhile; the longest of it was %s (%.0f ms)", round_ms,
			server->tick_worst ? server->tick_worst : "nothing in particular",
			server->tick_worst_ms);
	}
	if (server->tick) {
		int ms = 1000;
		if (!server->icons_shown || server->boot_on) {
			ms = 400;   /* asking after the room, while it is being made */
		} else if (shelf) {
			ms = 100;
		}
		wl_event_source_timer_update(server->tick, ms);
	}
	return 0;
}

static void begin_interactive(struct desk_toplevel *toplevel, enum desk_cursor_mode mode, uint32_t edges)
{
	struct desk_server *server = toplevel->server;
	if (!toplevel->scene_tree || toplevel->fullscreen) {
		return;
	}
	if (toplevel->sz_glide) {
		/* taken in hand on its way onto a half: there at once */
		view_sizing_stop(toplevel);
	}
	server->grabbed = toplevel;
	server->cursor_mode = mode;
	if (mode == DESK_CUR_MOVE) {
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
		server->move_from_x = server->cursor->x;
		server->move_from_y = server->cursor->y;
	} else {
		/* pulled to a size of its own: no longer a half of anything, nor
		 * lifted off the pull */
		toplevel->half = 0;
		view_peek_forget(toplevel);
		struct wlr_box geo;
		int l, t, r, b;
		view_geometry(toplevel, &geo);
		view_insets(toplevel, &l, &t, &r, &b);
		/* the window proper lies at the insets: its surface is placed so */
		double border_x = (toplevel->scene_tree->node.x + l) +
			((edges & WLR_EDGE_RIGHT) ? geo.width : 0);
		double border_y = (toplevel->scene_tree->node.y + t) +
			((edges & WLR_EDGE_BOTTOM) ? geo.height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;
		server->grab_geobox = geo;
		server->grab_geobox.x = toplevel->scene_tree->node.x + l;
		server->grab_geobox.y = toplevel->scene_tree->node.y + t;
		server->resize_edges = edges;
		cursor_for_edges(server, edges);
	}
}

static struct desk_toplevel *view_at(struct desk_server *server, double lx, double ly,
	struct wlr_surface **surface, double *sx, double *sy)
{
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (scene_surface) {
		*surface = scene_surface->surface;
	} else {
		*surface = NULL;
	}
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	if (tree && tree->node.data && (uintptr_t)tree->node.data != DESK_ROLE_DOCK &&
		(uintptr_t)tree->node.data != DESK_ROLE_SHEET) {
		return tree->node.data;
	}
	return NULL;
}

/* Is it a window under the pointer -- a frame, a client, an X menu --
 * rather than the desk? The desk and everything on it lie under the
 * windows, and a click on a sheet of paper lying over the post box is a
 * click on the paper. */
static bool pointer_over(struct desk_server *server, struct wlr_scene_tree *tree)
{
	if (!tree) {
		return false;
	}
	double sx, sy;
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
		server->cursor->x, server->cursor->y, &sx, &sy);
	for (; node; node = node->parent ? &node->parent->node : NULL) {
		if (node == &tree->node) {
			return true;
		}
	}
	return false;
}

static bool window_under(struct desk_server *server)
{
	return pointer_over(server, server->layer_win) || pointer_over(server, server->layer_full)
		|| pointer_over(server, server->layer_xover);
}

/* Go from the window with the keys (current, NULL for none) to next. A
 * game may occupy the fullscreen layer above the whole desk: it goes back
 * to its sheet before the next app is raised, or the new window would
 * remain hidden under the game. A sheet folded away -- lying on the desk,
 * or put in the drawer -- is opened out again where it was, and has the
 * keys from the start, so that a next press goes on past it and what is
 * typed meanwhile goes to it. */
static void switch_to(struct desk_server *server, struct desk_toplevel *current,
	struct desk_toplevel *next)
{
	if (current && current->fullscreen && current != next) {
		view_set_fullscreen(current, false);
	}
	/* one an earlier press set opening out, and still opening, is gone
	 * past: open, it leaves the keys with this one */
	struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		if (v != next && v->sheet.phase == SHEET_UNFOLDING) {
			v->sheet.passed = true;
		}
	}
	if (next->sheet.phase != SHEET_IDLE) {
		sheet_begin_unfold(server, &next->sheet);
	}
	focus_toplevel(next);
}

/* Super+Tab, Alt+F1 -- and Alt+Tab with nothing else to go to: straight on
 * to the window used longest ago, so that pressing it again and again goes
 * round them all. Folded ones are among them. (After dentarthurdent's
 * folded-switch, in the AiO v0.7.) */
static void desk_switch_application(struct desk_server *server)
{
	struct desk_toplevel *current = view_of_surface(server,
		server->seat->keyboard_state.focused_surface);
	struct desk_toplevel *next;
	if (!current) {
		/* nobody has the keys: the one in front is where it starts --
		 * the open one in front, a folded one being one to go to */
		wl_list_for_each(next, &server->toplevels, link) {
			if (next->sheet.phase == SHEET_IDLE && next->scene_tree) {
				current = next;
				break;
			}
		}
	}
	wl_list_for_each_reverse(next, &server->toplevels, link) {
		if (next != current && next->scene_tree && !next->unmanaged
			&& view_takes_keys(next)) {
			switch_to(server, current, next);
			return;
		}
	}
	/* nothing else to go to: a game alone on the whole screen goes back on
	 * its sheet, and the desk is there */
	if (current && current->fullscreen) {
		view_set_fullscreen(current, false);
	}
}

/* ---- Alt+Tab: the windows spread out over the desk ---------------------
 *
 * Alt+Tab, held: every window -- the folded ones too -- spread out over
 * the desk, the way you would push the papers on a desk apart to see them
 * all: each slides from where it lies to a place of its own, smaller as it
 * must be to fit, near where it was (the top ones along the top, the left
 * ones on the left), and the desk goes dark under them. The one it would go
 * to is picked up off the rest, and its name is typed on a slip under it.
 * Tab and Shift+Tab go round them in the order they were last used; the
 * arrows go to the next one that way; the hand picks the one it is over.
 * Letting go of Alt, Enter or a click goes to it, and they all slide back
 * where they lie, the one gone to on top; Esc, or a click on the desk, puts
 * them back as they were. A quick Alt+Tab is the window used before this
 * one, nothing spread at all. (After dentarthurdent's switcher-popup, in
 * the AiO v0.7, which was a list in a box.)
 *
 * What slides is a picture of each sheet as it was (view_snapshot; for a
 * folded one, the picture it was folded from), whole while it moves and
 * drawn again at its size once it is down, over the windows' own layers,
 * which are hidden meanwhile. */

#define SWITCHER_SHOW_MS 140    /* Alt held this long: they are spread */
#define SPREAD_S 0.30           /* out, or back */

static void desk_constraint_refresh(struct desk_server *server);

static void sw_paper_free(struct sw_paper *p)
{
	if (p->tree) {
		wlr_scene_node_destroy(&p->tree->node);
	}
	if (p->full) {
		wlr_buffer_drop(p->full);
	}
	if (p->small) {
		wlr_buffer_drop(p->small);
	}
	if (p->src) {
		cairo_surface_destroy(p->src);
	}
	memset(p, 0, sizeof *p);
}
