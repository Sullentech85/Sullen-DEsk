/* Part of wm.c: the drawer and the desk mat.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* Has the surface said it is to have our frame, by KDE's protocol?
 *
 * GTK 3 does not speak xdg-decoration. It says whose frame a window is to
 * have by KDE's older server-decoration protocol, which is offered with
 * ours as what a window gets unless it says otherwise: a plain GTK 3
 * window -- Xfce's and MATE's programs, their dialogs -- then draws no
 * title bar of its own and comes onto our paper with its heading, like a
 * program that asks by xdg-decoration. A window with a title bar of its
 * own making (a header bar, its buttons in it) says it frames itself, and
 * keeps its frame. What a window says may change after it is on the desk,
 * and its frame is decided again when it does (kde_frame_again). */
static bool kde_server_frame(struct desk_server *server, struct wlr_surface *surface)
{
	struct desk_kde_deco *k;
	wl_list_for_each(k, &server->kde_decos, link) {
		if (k->deco->surface == surface) {
			return k->deco->mode == WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
		}
	}
	return false;
}

/* Does the program draw its own frame? */
static bool view_draws_own_frame(const struct desk_toplevel *view)
{
	if (view->xdg_toplevel) {
		return view->deco == NULL
			&& !kde_server_frame(view->server, view->xdg_toplevel->base->surface);
	}
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		return view->xsurface->decorations != WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
	}
#endif
	return false;
}

static void view_frame_decide(struct desk_toplevel *view, bool *bare, bool *own_frame)
{
	if (view->thing_want && view->thing_want->frame_always) {
		*bare = *own_frame = false;
		return;
	}
	bool own = view_draws_own_frame(view);
	bool splash = false;
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		splash = xview_is_splash(view->server, view->xsurface);
	}
#endif
	*bare = splash || (own && !view_wants_paper(view->server, view));
	*own_frame = own && !*bare;
}

/* Told it is tiled on every side, a program that draws its own frame
 * leaves out its shadow and its round corners: on paper, the paper is its
 * edge. */
static void view_tell_tiled(struct desk_toplevel *view)
{
	if (view->xdg_toplevel && view->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_tiled(view->xdg_toplevel,
			view->own_frame || (view->thing_want && view->thing_want->frame_always)
			? WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT : WLR_EDGE_NONE);
	}
}

/* As it comes onto the desk: decided, nothing to move yet. */
static void view_frame_init(struct desk_toplevel *view)
{
	view->thing = view->thing_want = thing_claiming(view, false);
	view_frame_decide(view, &view->bare, &view->own_frame);
	view_tell_tiled(view);
}

/* Decided again -- it has asked for our frame, or stopped; the shell's
 * settings have changed -- with the window proper left where it is on the
 * desk and the paper made again round it. */
static void view_frame_update(struct desk_toplevel *view)
{
	bool bare, own_frame;
	view_frame_decide(view, &bare, &own_frame);
	if (bare == view->bare && own_frame == view->own_frame && view->thing_want == view->thing) {
		return;
	}
	int l0, t0, r0, b0, l1, t1, r1, b1;
	view_insets(view, &l0, &t0, &r0, &b0);
	view->bare = bare;
	view->own_frame = own_frame;
	view->thing = view->thing_want;
	view_insets(view, &l1, &t1, &r1, &b1);
	view_tell_tiled(view);
	if (!view->scene_tree) {
		return;
	}
	wlr_scene_node_set_position(&view->scene_tree->node,
		view->scene_tree->node.x + l0 - l1, view->scene_tree->node.y + t0 - t1);
	view->chrome_key = 0;
	view_repaint_chrome(view);
	view_sync_shadow(view);
	view_send_position(view);
}

static void windows_keep_off_tray(struct desk_server *server);

static double ui_scale_for(struct desk_server *server, struct wlr_output *out);
static void overlay_update(struct desk_server *server, bool force);

/* ---- the mat laid, and taken away ----
 *
 * A mat (felt, leather, linoleum, a photo) is set down rolled up at the back
 * of the desk and unrolled toward you; taken away, it is rolled up first; and
 * for another, the one is rolled up and the other unrolled (draw.c
 * desk_mat_unroll). A clear sheet, a mat only a different size, the first
 * one at login, in 2D or behind the door: laid at once. */

enum { MAT_ROLL_NONE, MAT_ROLL_UP, MAT_ROLL_OUT };
#define MAT_UNROLL_S 1.3
#define MAT_ROLLUP_S 0.8

static bool gfx_flat(void);
static bool locked(const struct desk_server *server);

/* the same mat but for its size, or the same */
static bool mat_same_look(const struct desk_mat *a, const struct desk_mat *b)
{
	return a->kind == b->kind && !memcmp(a->rgb, b->rgb, sizeof a->rgb) && !strcmp(a->photo, b->photo)
		&& a->under == b->under;
}

static bool mat_lay(struct desk_server *server, const struct desk_mat *m, bool roll)
{
	struct desk_mat was = server->mat_roll == MAT_ROLL_UP ? server->mat_next : server->mat_want;
	if (server->mat_roll && mat_same_look(&was, m) && was.size == m->size) {
		return false;   /* (shell.conf read again for something else: let it go on) */
	}
	server->mat_want = *m;
	bool look = !mat_same_look(&was, m);
	if (!roll || !look || gfx_flat() || locked(server) || server->desk_w < 8
		|| (!desk_mat_rolls(&was) && !desk_mat_rolls(m))) {
		/* at once: and whatever was on its way, there */
		server->mat_roll = MAT_ROLL_NONE;
		desk_mat_unroll(1.0);
		return desk_set_mat(m);
	}
	if (server->mat_roll == MAT_ROLL_UP) {
		server->mat_next = *m;   /* still going up: this one comes after it */
		return false;
	}
	if (desk_mat_rolls(&was) && was.kind != DESK_MAT_NONE) {
		/* the one there rolled up, from as far out as it is */
		double now = monotonic_sec();
		server->mat_roll_from = server->mat_roll == MAT_ROLL_OUT
			? fmin(1.0, (now - server->mat_roll_t0) / MAT_UNROLL_S) : 1.0;
		server->mat_roll_from = 1.0 - pow(1.0 - server->mat_roll_from, 3.0);
		server->mat_roll = MAT_ROLL_UP;
		server->mat_roll_t0 = now;
		server->mat_next = *m;
		snd_at(server, DESK_SND_SHEET_TAKE, server->desk_w * 0.5, 0.6);
		wlr_log(WLR_INFO, "the mat rolled up");
		server_schedule_frames(server);
		return false;
	}
	/* nothing to roll up: this one set down rolled, and unrolled */
	bool changed = desk_set_mat(m);
	if (desk_mat_rolls(m)) {
		desk_mat_unroll(0.0);
		server->mat_roll = MAT_ROLL_OUT;
		server->mat_roll_t0 = monotonic_sec();
		snd_at(server, DESK_SND_SHEET_TAKE, server->desk_w * 0.5, 0.5);
		wlr_log(WLR_INFO, "the mat unrolled");
		server_schedule_frames(server);
	}
	return changed;
}

static void overlay_update(struct desk_server *server, bool force);

/* Once a frame while one is on its way. */
static void mat_roll_tick(struct desk_server *server)
{
	if (!server->mat_roll) {
		return;
	}
	double q = (monotonic_sec() - server->mat_roll_t0)
		/ (server->mat_roll == MAT_ROLL_UP ? MAT_ROLLUP_S : MAT_UNROLL_S);
	if (locked(server) || gfx_flat()) {
		q = 1.0;   /* behind the door, or in 2D: done */
	}
	if (server->mat_roll == MAT_ROLL_UP) {
		double e = clampf_d(q, 0.0, 1.0);
		desk_mat_unroll(server->mat_roll_from * (1.0 - e * e));
		if (q >= 1.0) {
			struct desk_mat next = server->mat_next;
			desk_set_mat(&next);
			if (desk_mat_rolls(&next) && next.kind != DESK_MAT_NONE && !locked(server) && !gfx_flat()) {
				desk_mat_unroll(0.0);
				server->mat_roll = MAT_ROLL_OUT;
				server->mat_roll_t0 = monotonic_sec();
				wlr_log(WLR_INFO, "the mat unrolled");
			} else {
				desk_mat_unroll(1.0);
				server->mat_roll = MAT_ROLL_NONE;
			}
		}
	} else {
		/* quick, and slowing as it lies down */
		double e = 1.0 - pow(1.0 - clampf_d(q, 0.0, 1.0), 3.0);
		desk_mat_unroll(e);
		if (q >= 1.0) {
			desk_mat_unroll(1.0);
			server->mat_roll = MAT_ROLL_NONE;
			snd_at(server, DESK_SND_PAPER_SETTLE, server->desk_w * 0.5, 0.6);
		}
	}
	overlay_update(server, false);
	server_schedule_frames(server);
}

/* The mat shell.conf asks for, laid (rolled, if roll and it is a change):
 * true when that is not what lay on the desk. */
static bool mat_apply(struct desk_server *server, const struct shell_conf *c, bool roll)
{
	struct desk_mat m = { .size = c->desk_mat_size };
	if (!strcasecmp(c->desk_mat, "clear")) {
		/* a clear sheet, and under it the prints, or one picture all over it */
		m.kind = DESK_MAT_CLEAR;
		m.under = c->desk_mat_under;
		snprintf(m.photo, sizeof m.photo, "%s", c->desk_mat_photo);
		if (m.under == DESK_MAT_UNDER_WHOLE && !m.photo[0]) {
			wlr_log(WLR_ERROR, "desk_mat_under is whole, but desk_mat_photo names no picture");
		}
	} else if (!strcasecmp(c->desk_mat, "photo") && c->desk_mat_photo[0]) {
		desk_mat_preset("charcoal", &m);   /* the cloth until the picture is read */
		m.kind = DESK_MAT_PHOTO;
		snprintf(m.photo, sizeof m.photo, "%s", c->desk_mat_photo);
	} else if (!desk_mat_preset(c->desk_mat, &m)) {
		m.kind = DESK_MAT_NONE;
		if (!strcasecmp(c->desk_mat, "photo")) {
			wlr_log(WLR_ERROR, "desk_mat is photo, but desk_mat_photo names no picture");
		}
	}
	return mat_lay(server, &m, roll);
}

/* How much of a print lying at (x, y) points from the middle of the mat r
 * (k pixels to the point) is out of sight: under one of the n windows
 * (x, y, w, h on the screen), or off the screen w by h -- and, by a third
 * as much, under the m prints in `over` that came in with it. 0 to 1. */
static double mat_paper_hidden(const struct desk_mat_paper *q, double x, double y, const double r[4],
	double k, const struct wlr_box *win, int n, const struct desk_mat_paper *over, int m, int w,
	int h)
{
	double t = q->turn * 3.14159265358979 / 180.0, hw = q->w * 0.5, hh = hw * (double)q->ph / q->pw;
	double cx = r[0] + r[2] * 0.5 + x * k, cy = r[1] + r[3] * 0.5 + y * k;
	double hidden = 0.0;
	for (int i = 0; i < 5; i++) {
		for (int j = 0; j < 5; j++) {
			double u = (i - 2) / 2.2 * hw * k, v = (j - 2) / 2.2 * hh * k;
			double px = cx + u * cos(t) - v * sin(t), py = cy + u * sin(t) + v * cos(t);
			bool out = px < 0.0 || py < 0.0 || px >= w || py >= h;
			for (int b = 0; b < n && !out; b++) {
				out = px >= win[b].x && py >= win[b].y && px < win[b].x + win[b].width
					&& py < win[b].y + win[b].height;
			}
			double part = out ? 1.0 : 0.0;
			for (int o = 0; o < m && part == 0.0; o++) {
				double ot = over[o].turn * 3.14159265358979 / 180.0, dx = px - (r[0] + r[2] * 0.5 + over[o].x * k),
					dy = py - (r[1] + r[3] * 0.5 + over[o].y * k);
				double ou = dx * cos(ot) + dy * sin(ot), ov = -dx * sin(ot) + dy * cos(ot);
				double ohw = over[o].w * 0.5 * k, ohh = ohw * (double)over[o].ph / over[o].pw;
				part = fabs(ou) <= ohw && fabs(ov) <= ohh ? 1.0 / 3.0 : 0.0;
			}
			hidden += part;
		}
	}
	return hidden / 25.0;
}

/* Prints that have just come under the mat (not among the n_had names
 * there were) are laid where they can be seen: desk-mat lays them near the
 * middle, which is where the window they were chosen in is likely to be. A
 * print mostly under a window goes to the nearest place on the mat that is
 * clear of the windows, as one would slip it under the glass where there is
 * room. True if one has been moved. */
static bool mat_papers_in_sight(struct desk_server *server, struct desk_mat_paper *p, int n,
	char (*had)[96], int n_had)
{
	int w = server->desk_w, h = server->desk_h;
	double r[4];
	if (!desk_mat_rect(w, h, r)) {
		return false;
	}
	double k = desk_px_per_pt(w);
	struct wlr_box win[64];
	int nwin = 0;
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		int lx, ly;
		if (nwin < 64 && view->scene_tree && !view->unmanaged
			&& wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly)) {
			win[nwin++] = (struct wlr_box){ lx, ly, view->outer_w, view->outer_h };
		}
	}
	struct desk_mat_paper came[DESK_MAT_PAPERS_MAX];
	int ncame = 0;
	bool moved = false;
	for (int i = 0; i < n; i++) {
		bool fresh = p[i].pw > 0 && p[i].ph > 0;
		for (int j = 0; j < n_had && fresh; j++) {
			fresh = strcmp(had[j], p[i].name) != 0;
		}
		if (!fresh) {
			continue;
		}
		struct desk_mat_paper *q = &p[i];
		double was = mat_paper_hidden(q, q->x, q->y, r, k, win, nwin, came, ncame, w, h);
		if (was > 0.25) {
			/* all of it under the sheet wherever it goes (as thing-mat.c keeps
			 * a print carried), the nearest of the places least hidden */
			double t = q->turn * 3.14159265358979 / 180.0, hw = q->w * 0.5, hh = hw * (double)q->ph / q->pw;
			double ex = hw * fabs(cos(t)) + hh * fabs(sin(t)), ey = hw * fabs(sin(t)) + hh * fabs(cos(t));
			double mx = fmax(0.0, r[2] * 0.5 / k - ex - 6.0), my = fmax(0.0, r[3] * 0.5 / k - ey - 6.0);
			double best = was + 1.0, bx = q->x, by = q->y;
			for (double y = -my; y <= my + 0.01; y += fmax(my / 24.0, 8.0)) {
				for (double x = -mx; x <= mx + 0.01; x += fmax(mx / 40.0, 8.0)) {
					double score = mat_paper_hidden(q, x, y, r, k, win, nwin, came, ncame, w, h)
						+ hypot(x - q->x, y - q->y) / (r[2] / k) * 0.2;
					if (score < best) {
						best = score;
						bx = x;
						by = y;
					}
				}
			}
			double now = mat_paper_hidden(q, bx, by, r, k, win, nwin, came, ncame, w, h);
			if (now < was - 0.05) {
				wlr_log(WLR_INFO, "a print come under the mat where it can be seen: %s at %.0f, %.0f "
					"(%.0f%% hidden where it was laid, %.0f%% there)", q->name, bx, by, was * 100.0,
					now * 100.0);
				q->x = bx;
				q->y = by;
				moved = true;
			}
		}
		came[ncame++] = *q;
	}
	if (ncame > 0) {
		/* slipped in under the sheet */
		snd_at(server, DESK_SND_PAPER_SETTLE, r[0] + r[2] * 0.5 + came[0].x * k, 0.6);
	}
	return moved;
}

/* The prints under the clear mat, as desk-mat or another desk left them in
 * their file: drawn again when they have changed. Those there when the desk
 * first reads it stay where they lie; one that comes after is laid in sight
 * (mat_papers_in_sight). */
static void mat_papers_poll(struct desk_server *server)
{
	static char had[DESK_MAT_PAPERS_MAX][96];
	static int n_had = -1;   /* -1: not looked at yet */
	bool changed = desk_mat_papers_poll();
	if (!changed && n_had >= 0) {
		return;
	}
	struct desk_mat_paper p[DESK_MAT_PAPERS_MAX];
	int n = desk_mat_papers(p, DESK_MAT_PAPERS_MAX);
	if (n_had >= 0 && mat_papers_in_sight(server, p, n, had, n_had)) {
		desk_mat_papers_set(p, n, true);
	}
	for (int i = 0; i < n; i++) {
		memcpy(had[i], p[i].name, sizeof had[i]);
		had[i][sizeof had[i] - 1] = '\0';
	}
	n_had = n;
	if (changed) {
		overlay_update(server, false);
		server_schedule_frames(server);
	}
}

static void shell_apply_all(struct desk_server *server)
{
	motion_set(motion_of(&server->conf));
	g_bounce = server->conf.bounce;
	desk_set_clock_12h(server->conf.clock_12h);
	mat_apply(server, &server->conf, true);
	if (desk_set_binder_ink(server->conf.binder_ink)) {
		/* the labels typed again: the bookcase's pictures with them */
		pictures_forget(server);
		server_schedule_frames(server);
	}
	overlay_update(server, true);
	/* a new size for things: the room is traced again for it, as for a
	 * new screen */
	struct wlr_output *out = first_output(server);
	if (out && server->desk_w > 8) {
		double scale = ui_scale_for(server, out);
		if (fabs(scale - desk_ui_scale()) > 1e-3) {
			wlr_log(WLR_INFO, "a point is %.2f pixels now", scale);
			desk_set_ui_scale(scale);
			g_motion_fine = desk_px_per_pt(server->desk_w);
			desk_relayout(server);
			/* which the display tools show as the screen's scale */
			om_publish(server);
		}
	}
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->scene_tree && !view->unmanaged) {
			view_frame_update(view);
		}
	}
	/* a sheet that has grown a margin may now be over the tray */
	windows_keep_off_tray(server);
	input_apply(server);
	server_schedule_frames(server);
}

/* On a program laid on paper, is (dx from the right edge, ly from the
 * top) on the paper by the corner, or on the fold itself -- rather than on
 * the program, whose own buttons are just under it? */
static bool mount_on_fold(const struct desk_toplevel *view, double dx, double ly)
{
	return ly < DESK_MOUNT_TOP || dx < DESK_MOUNT || dx + ly <= view->curl_s + 4.0;
}

/* What a window is, for printing on the back of it: the application it
 * belongs to, found by what the window says of itself, and its icon -- or,
 * if no application owns up to it, the best name the window has. */
static cairo_surface_t *view_app(const struct desk_toplevel *view, char *name, size_t n)
{
	const char *app_id = NULL, *cls = NULL, *inst = NULL;
	if (view->xdg_toplevel) {
		app_id = view->xdg_toplevel->app_id;
	}
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		cls = view->xsurface->class;
		inst = view->xsurface->instance;
	}
#endif
	cairo_surface_t *icon = NULL;
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = view_app_entry(l, view, app_id, cls, inst);
	/* a script's window is called after its file (thing.py), and one that
	 * calls itself python3 says nothing: its title does better */
	char stem[128];
	if (desk_app_is_interpreter(app_id)) {
		app_id = NULL;
	} else if (desk_app_script_stem(app_id, stem, sizeof stem)) {
		app_id = stem;
	}
	if (desk_app_is_interpreter(cls)) {
		cls = NULL;
	}
	if (app) {
		snprintf(name, n, "%s", app->name);
		icon = desk_app_icon(app);
	} else if (app_id && *app_id) {
		/* org.example.Thing is Thing */
		const char *dot = strrchr(app_id, '.');
		snprintf(name, n, "%s", dot && dot[1] ? dot + 1 : app_id);
	} else if (cls && *cls) {
		char cstem[128];
		snprintf(name, n, "%s", desk_app_script_stem(cls, cstem, sizeof cstem) ? cstem : cls);
	} else {
		snprintf(name, n, "%s", view_title(view));
	}
	desk_apps_release(l);
	return icon;
}

/* A thing as it lies, what is in it and all, pad past its edge. */
static cairo_surface_t *view_thing_picture(struct desk_toplevel *view)
{
	const struct desk_thing *thing = view_thing(view);
	int w = view->outer_w, h = view->outer_h, pad = thing->pad;
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		w + pad * 2, h + pad * 2);
	cairo_t *cr = cairo_create(surf);
	thing->paint(cr, view, pad, pad, w, h, view_title(view), true, true);
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	struct wlr_box geo;
	view_geometry(view, &geo);
	cairo_rectangle(cr, pad + l, pad + t, w - l - r, h - t - b);
	cairo_clip(cr);
	paint_client(cr, view_surface(view), pad + l - geo.x, pad + t - geo.y);
	cairo_destroy(cr);
	cairo_surface_flush(surf);
	return surf;
}

cairo_surface_t *view_picture(struct desk_toplevel *view)
{
	return view_thing(view) ? view_thing_picture(view) : view_snapshot(view);
}

static void view_fold_toggle(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (view_refuses(view, THING_NO_FOLD)) {
		return;
	}
	if (view->sheet.phase != SHEET_IDLE) {
		sheet_begin_unfold(server, &view->sheet);
		return;
	}
	const struct desk_thing *thing = view_thing(view);
	bool book = thing && thing->shuts;
	cairo_surface_t *src = book ? view_thing_picture(view) : view_snapshot(view);
	char name[128];
	cairo_surface_t *icon = view_app(view, name, sizeof name);
	bool had_keys = server->seat->keyboard_state.focused_surface == view_surface(view);
	view->sheet.book = book;
	if (book) {
		/* on its label, what is in it */
		snprintf(name, sizeof name, "%s", view_title(view));
		char *mark = strstr(name, "  \xe2\x80\xa2");
		if (mark) {
			*mark = '\0';
		}
	}
	int pad = book ? thing->pad : 0;
	sheet_begin_fold(server, &view->sheet, src,
		view->scene_tree->node.x - pad, view->scene_tree->node.y - pad,
		view->scene_tree, view, icon, name, view_screen(view));
	/* a sheet folded away is not the one being written on any more */
	if (had_keys) {
		focus_next(server);
	}
}

/* ---- what is in the drawer ---------------------------------------
 *
 * Whatever has been put there, in a row along the felt: programs, the
 * screwdriver, sheets folded away. A hand on one of them does what it is
 * for -- runs the program, opens Shell Settings, opens the sheet back out on
 * the desk -- and the drawer goes back in. Picked up and carried, a thing
 * goes elsewhere in the row; carried out over the desk, a program is taken
 * out of the drawer and a sheet is laid down there. A sheet folded on the
 * desk is carried into the drawer the same way, and the drawer comes out
 * for it if it is carried to the drawer's pull. The programs and the
 * screwdriver, in their order, are kept ($XDG_CONFIG_HOME/desk/drawer); the sheets go
 * when their windows do. */

static void drawer_path(char *out, size_t n)
{
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg) {
		snprintf(out, n, "%s/desk/drawer", cfg);
	} else if (home && *home) {
		snprintf(out, n, "%s/.config/desk/drawer", home);
	} else {
		out[0] = '\0';
	}
}

static void drawer_save(struct desk_server *server)
{
	char path[512];
	drawer_path(path, sizeof path);
	if (!path[0]) {
		return;
	}
	conf_dirs_for(path);
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		wlr_log(WLR_ERROR, "cannot keep what is in the drawer in %s", tmp);
		return;
	}
	fprintf(f, "# DEsk's drawer (2). What is in it, in order: \"thing <terminal | browser |\n"
		"# files | editor>\" (DEsk's standard program for it, whichever that is),\n"
		"# \"app <desktop file id>\", \"panel\" (the screwdriver: Shell Settings),\n"
		"# \"camera\" (Print's camera), \"calculator\", \"keyboard\" or \"notebook\".\n");
	for (int i = 0; i < server->drw_n; i++) {
		const struct drawer_entry *e = &server->drw[i];
		if (e->kind == DRAWER_APP) {
			fprintf(f, "app %s\n", e->id);
		} else if (e->kind == DRAWER_STD) {
			fprintf(f, "thing %s\n", desk_std_name(e->std));
		} else if (e->kind == DRAWER_PANEL) {
			fprintf(f, "panel\n");
		} else if (e->kind == DRAWER_CAMERA) {
			fprintf(f, "camera\n");
		} else if (e->kind == DRAWER_THING) {
			fprintf(f, "%s\n", e->thing->drawer_word);
		}
	}
	bool ok = fflush(f) == 0 && !ferror(f);
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		wlr_log(WLR_ERROR, "cannot keep what is in the drawer in %s", path);
		unlink(tmp);
		return;
	}
	server->drw_default = false;
}

/* Print's camera is always in the drawer, as the screwdriver is: put in
 * before the screwdriver if it is not there -- a drawer kept before there
 * was a camera. */
static void drawer_camera_ensure(struct desk_server *server)
{
	int at = server->drw_n;
	for (int i = 0; i < server->drw_n; i++) {
		if (server->drw[i].kind == DRAWER_CAMERA) {
			return;
		}
		if (server->drw[i].kind == DRAWER_PANEL && at == server->drw_n) {
			at = i;
		}
	}
	if (server->drw_n >= DRAWER_MAX) {
		return;
	}
	memmove(&server->drw[at + 1], &server->drw[at],
		(size_t)(server->drw_n - at) * sizeof server->drw[0]);
	server->drw[at] = (struct drawer_entry){ .kind = DRAWER_CAMERA, .present = true,
		.fit = 1.0, .fit_to = 1.0 };
	server->drw_n++;
}

/* And so is the calculator, as it is when it is not on the desk: before the
 * camera, if it is not there -- a drawer kept before there was one. */
/* Everything kept in the drawer has its place there, before the camera if it had none. */
static void drawer_things_ensure(struct desk_server *server)
{
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if (!(*t)->drawer_word || ((*t)->offered && !(*t)->offered(server))) {
			continue;
		}
		int at = server->drw_n;
		bool there = false;
		for (int i = 0; i < server->drw_n && !there; i++) {
			there = server->drw[i].kind == DRAWER_THING && server->drw[i].thing == *t;
			if (server->drw[i].kind == DRAWER_CAMERA && at == server->drw_n) {
				at = i;
			}
		}
		if (there || server->drw_n >= DRAWER_MAX) {
			continue;
		}
		memmove(&server->drw[at + 1], &server->drw[at],
			(size_t)(server->drw_n - at) * sizeof server->drw[0]);
		server->drw[at] = (struct drawer_entry){ .kind = DRAWER_THING, .thing = *t, .present = true,
			.fit = 1.0, .fit_to = 1.0 };
		server->drw_n++;
	}
}

/* The thing the drawer's file calls this. ("journal": the notebook, before there was one.) */
static const struct desk_thing *thing_of_drawer_word(const char *word)
{
	if (!strcmp(word, "journal")) {
		word = "notebook";
	}
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->drawer_word && !strcmp((*t)->drawer_word, word)) {
			return *t;
		}
	}
	return NULL;
}

static void drawer_load(struct desk_server *server)
{
	server->drw_n = 0;
	server->drw_default = true;
	server->drw_gen = UINT_MAX;   /* matched to no list yet */
	server->drw_gap = -1;
	server->drw_hover = -1;
	server->drw_press = -1;
	server->drw_carry_from = -1;
	char path[512];
	drawer_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return;
	}
	server->drw_default = false;
	/* one kept before there were standard programs: its first program of
	 * each kind was what that thing in the drawer opened, so that is what
	 * it is turned into (drawer_match) */
	server->drw_migrate = true;
	char line[256];
	while (fgets(line, sizeof line, f) && server->drw_n < DRAWER_MAX) {
		/* (DeskWM and [DE]sk: the names DEsk went by when it wrote some
		 * of these) */
		if (!strncmp(line, "# DEsk's drawer (2)", 19) || !strncmp(line, "# [DE]sk's drawer (2)", 21)
			|| !strncmp(line, "# DeskWM's drawer (2)", 21)) {
			server->drw_migrate = false;
		}
		char *s = line;
		while (*s == ' ' || *s == '\t') {
			s++;
		}
		size_t len = strlen(s);
		while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' '
				|| s[len - 1] == '\t')) {
			s[--len] = '\0';
		}
		struct drawer_entry e = { 0 };
		if (strcmp(s, "panel") == 0) {
			e.kind = DRAWER_PANEL;
			e.present = true;
		} else if (strcmp(s, "camera") == 0) {
			e.kind = DRAWER_CAMERA;
			e.present = true;
		} else if (thing_of_drawer_word(s)) {
			e.kind = DRAWER_THING;
			e.thing = thing_of_drawer_word(s);
			e.present = true;
		} else if (strncmp(s, "thing", 5) == 0 && (s[5] == ' ' || s[5] == '\t')) {
			const char *name = s + 6;
			while (*name == ' ' || *name == '\t') {
				name++;
			}
			e.std = desk_std_parse(name);
			if (e.std < 0) {
				continue;
			}
			e.kind = DRAWER_STD;
		} else if (strncmp(s, "app", 3) == 0 && (s[3] == ' ' || s[3] == '\t')) {
			const char *id = s + 4;
			while (*id == ' ' || *id == '\t') {
				id++;
			}
			if (!*id) {
				continue;
			}
			e.kind = DRAWER_APP;
			snprintf(e.id, sizeof e.id, "%s", id);
		} else {
			continue;
		}
		e.fit = e.fit_to = 1.0;
		server->drw[server->drw_n++] = e;
	}
	fclose(f);
	drawer_camera_ensure(server);
	drawer_things_ensure(server);
}

static int drawer_shown(const struct desk_server *server)
{
	int m = 0;
	for (int i = 0; i < server->drw_n; i++) {
		m += server->drw[i].present;
	}
	return m;
}

/* Where each goes along the row, the place held open for something
 * carried skipped; now, or by sliding there. */
static void drawer_layout(struct desk_server *server, bool now)
{
	int shown = drawer_shown(server);
	int gap = server->drw_gap > shown ? shown : server->drw_gap;
	int m = shown + (gap >= 0 ? 1 : 0);
	double fit = desk_drawer_fit(m);
	int k = 0;
	for (int i = 0; i < server->drw_n; i++) {
		struct drawer_entry *e = &server->drw[i];
		if (!e->present) {
			continue;
		}
		int slot = gap >= 0 && k >= gap ? k + 1 : k;
		e->x_to = desk_drawer_slot_x(slot, m);
		e->fit_to = fit;
		if (now) {
			e->x = e->x_to;
			e->fit = e->fit_to;
		}
		k++;
	}
	server->drw_dirty = true;
	if (!now && !server->drw_sliding) {
		server->drw_sliding = true;
		server->drw_slid_at = monotonic_sec();
		server_schedule_frames(server);
	}
}

/* Matched again to the programs there are, when that list is new: which
 * are installed, and -- if nobody has filled the drawer yet -- what it
 * comes with, one of each of the four things there are programs for, and
 * the panel. */
static void drawer_match(struct desk_server *server)
{
	const struct desk_app_list *l = desk_apps_acquire();
	if (l->generation == server->drw_gen && !(server->drw_default && l->n > 0)) {
		desk_apps_release(l);
		return;
	}
	server->drw_gen = l->generation;
	/* (what is offered may hang on what programs there are: the notebook on the standard editor) */
	if (!server->drw_default) {
		drawer_things_ensure(server);
	}
	if (server->drw_default && l->n > 0) {
		struct drawer_entry keep[DRAWER_MAX];
		int nk = 0;
		for (int i = 0; i < server->drw_n; i++) {
			if (server->drw[i].kind == DRAWER_SHEET) {
				keep[nk++] = server->drw[i];
			}
		}
		server->drw_n = 0;
		/* DEsk's standard programs, one a thing */
		for (int m = 0; m < DESK_STD_COUNT; m++) {
			server->drw[server->drw_n++] = (struct drawer_entry){ .kind = DRAWER_STD,
				.std = m, .fit = 1.0, .fit_to = 1.0 };
		}
		for (const struct desk_thing *const *t = desk_things; *t; t++) {
			if ((*t)->drawer_word && (!(*t)->offered || (*t)->offered(server))) {
				server->drw[server->drw_n++] = (struct drawer_entry){ .kind = DRAWER_THING, .thing = *t,
					.present = (*t)->in_drawer(server), .fit = 1.0, .fit_to = 1.0 };
			}
		}
		server->drw[server->drw_n++] = (struct drawer_entry){ .kind = DRAWER_CAMERA,
			.present = true, .fit = 1.0, .fit_to = 1.0 };
		server->drw[server->drw_n++] = (struct drawer_entry){ .kind = DRAWER_PANEL,
			.present = true, .fit = 1.0, .fit_to = 1.0 };
		for (int i = 0; i < nk && server->drw_n < DRAWER_MAX; i++) {
			server->drw[server->drw_n++] = keep[i];
		}
		/* it is what it comes with until somebody changes it: not kept */
		server->drw_default = false;
	}
	if (server->drw_migrate && l->n > 0) {
		server->drw_migrate = false;
		for (int m = 0; m < DESK_STD_COUNT; m++) {
			for (int i = 0; i < server->drw_n; i++) {
				struct drawer_entry *e = &server->drw[i];
				const struct desk_app *app = e->kind == DRAWER_APP
					? desk_apps_find(l, e->id) : NULL;
				if (app && app->model == m) {
					wlr_log(WLR_INFO, "the drawer's %s is the standard one now, not %s",
						desk_std_name(m), e->id);
					e->kind = DRAWER_STD;
					e->std = m;
					e->id[0] = '\0';
					break;
				}
			}
		}
		drawer_save(server);
	}
	for (int i = 0; i < server->drw_n; i++) {
		struct drawer_entry *e = &server->drw[i];
		if (e->kind == DRAWER_APP) {
			e->present = desk_apps_find(l, e->id) != NULL;
		} else if (e->kind == DRAWER_STD) {
			e->present = desk_apps_for_std(l, e->std) != NULL;
		}
	}
	desk_apps_release(l);
	drawer_layout(server, true);
}

/* ---- what a program in the drawer looks like -------------------------
 *
 * Any program kept there can be given a look of its own (looks.h): its icon
 * lying there by itself, one of the things the drawer has always had, one of
 * the desk's, or a look anyone has made (docs/LOOKS.md). Chosen on the card of
 * looks (below), and
 * kept by what the drawer's file calls the program
 * ($XDG_CONFIG_HOME/desk/drawer-looks), so that it looks the same taken out
 * and put back. */

static bool drawer_key(const struct drawer_entry *e, char *out, size_t n)
{
	if (e->kind == DRAWER_APP) {
		snprintf(out, n, "app %s", e->id);
	} else if (e->kind == DRAWER_STD) {
		snprintf(out, n, "thing %s", desk_std_name(e->std));
	} else {
		out[0] = '\0';
	}
	return out[0] != '\0';
}

static const char *drawer_look_of(const struct desk_server *server, const struct drawer_entry *e)
{
	char key[128];
	if (!drawer_key(e, key, sizeof key)) {
		return NULL;
	}
	for (int i = 0; i < server->drw_looks_n; i++) {
		if (!strcmp(server->drw_looks[i].key, key)) {
			return server->drw_looks[i].look;
		}
	}
	return NULL;
}

static void drawer_looks_path(char *out, size_t n)
{
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg) {
		snprintf(out, n, "%s/desk/drawer-looks", cfg);
	} else if (home && *home) {
		snprintf(out, n, "%s/.config/desk/drawer-looks", home);
	} else {
		out[0] = '\0';
	}
}

static void drawer_looks_load(struct desk_server *server)
{
	server->drw_looks_n = 0;
	char path[512];
	drawer_looks_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return;
	}
	char line[300];
	while (fgets(line, sizeof line, f) && server->drw_looks_n < DRAWER_LOOKS_MOST) {
		line[strcspn(line, "\r\n")] = '\0';
		/* (the program last: what the look is called may have spaces in it, a picture's name) */
		char *sp = NULL;
		for (char *q = line; (q = strchr(q, ' ')); q++) {
			if (!strncmp(q, " app ", 5) || !strncmp(q, " thing ", 7)) {
				sp = q;
			}
		}
		if (line[0] == '#' || !sp || sp == line || (size_t)(sp - line) >= sizeof server->drw_looks[0].look
			|| strlen(sp + 1) >= sizeof server->drw_looks[0].key) {
			continue;
		}
		struct drawer_look *dl = &server->drw_looks[server->drw_looks_n++];
		snprintf(dl->look, sizeof dl->look, "%.*s", (int)(sp - line), line);
		snprintf(dl->key, sizeof dl->key, "%s", sp + 1);
	}
	fclose(f);
}

static void drawer_looks_save(struct desk_server *server)
{
	char path[512];
	drawer_looks_path(path, sizeof path);
	if (!path[0]) {
		return;
	}
	conf_dirs_for(path);
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		wlr_log(WLR_ERROR, "cannot keep what the drawer's programs look like in %s", tmp);
		return;
	}
	fprintf(f, "# DEsk's drawer: what a program kept in it looks like, when it is not as it would be.\n"
		"# \"<look> <what the drawer's file calls it>\": the look one of the desk's (card, icon,\n"
		"# terminal, compass, folder, notepad, screwdriver, calculator, typewriter, notebook, camera),\n"
		"# a look's folder's name (radio, paintbox, gamepad, headphones are the desk's own), or\n"
		"# \"own/<folder or file>\", one in $XDG_DATA_HOME/desk/looks (docs/LOOKS.md).\n");
	for (int i = 0; i < server->drw_looks_n; i++) {
		fprintf(f, "%s %s\n", server->drw_looks[i].look, server->drw_looks[i].key);
	}
	bool ok = fflush(f) == 0 && !ferror(f);
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		wlr_log(WLR_ERROR, "cannot keep what the drawer's programs look like in %s", path);
		unlink(tmp);
	}
}

/* It looks like this from now on: NULL, as it would. */
static void drawer_look_set(struct desk_server *server, const struct drawer_entry *e, const char *look)
{
	char key[128];
	if (!drawer_key(e, key, sizeof key)) {
		return;
	}
	int at = -1;
	for (int i = 0; i < server->drw_looks_n && at < 0; i++) {
		if (!strcmp(server->drw_looks[i].key, key)) {
			at = i;
		}
	}
	if (!look) {
		if (at >= 0) {
			memmove(&server->drw_looks[at], &server->drw_looks[at + 1],
				(size_t)(server->drw_looks_n - at - 1) * sizeof server->drw_looks[0]);
			server->drw_looks_n--;
		}
	} else {
		if (at < 0) {
			if (server->drw_looks_n >= DRAWER_LOOKS_MOST) {
				return;
			}
			at = server->drw_looks_n++;
			snprintf(server->drw_looks[at].key, sizeof server->drw_looks[at].key, "%s", key);
		}
		snprintf(server->drw_looks[at].look, sizeof server->drw_looks[at].look, "%s", look);
	}
	drawer_looks_save(server);
	server->drw_dirty = true;
}

/* What it looks like, as one it lies as: the program's name and how it happens to lie stay. */
static void drawer_look_apply(const struct desk_app_list *l, const struct drawer_entry *e,
	const struct desk_look *look, struct desk_drawer_item *it)
{
	it->icon = NULL;
	it->lying = NULL;
	it->picture = NULL;
	switch (look->kind) {
	case DESK_LOOKS_CARD: {
		const struct desk_app *app = e->kind == DRAWER_APP ? desk_apps_find(l, e->id)
			: e->kind == DRAWER_STD ? desk_apps_for_std(l, e->std) : NULL;
		it->look = DESK_LOOK_CARD;
		it->icon = app ? desk_app_icon(app) : NULL;
		break;
	}
	case DESK_LOOKS_TOOL:
		it->look = look->tool;
		break;
	case DESK_LOOKS_THING: {
		const struct desk_thing *t = thing_of_drawer_word(look->thing);
		struct desk_drawer_item ti;
		memset(&ti, 0, sizeof ti);
		if (t && t->drawer_item) {
			t->drawer_item(&ti);
		}
		it->look = ti.lying ? DESK_LOOK_THING : DESK_LOOK_CARD;
		it->lying = ti.lying;
		it->thick = ti.thick;
		break;
	}
	case DESK_LOOKS_NOTEBOOK:
		it->look = DESK_LOOK_NOTEBOOK;
		break;
	case DESK_LOOKS_CAMERA:
		it->look = DESK_LOOK_CAMERA;
		break;
	case DESK_LOOKS_ICON: {
		/* its mark, lying there as a thing, printed on nothing */
		const struct desk_app *app = e->kind == DRAWER_APP ? desk_apps_find(l, e->id)
			: e->kind == DRAWER_STD ? desk_apps_for_std(l, e->std) : NULL;
		it->picture = app ? desk_app_icon(app) : NULL;
		it->along = DRAWER_ICON_ALONG;
		it->thick = DRAWER_ICON_THICK;
		it->look = it->picture ? DESK_LOOK_PICTURE : DESK_LOOK_CARD;
		if (!it->picture) {
			it->icon = NULL;
		}
		break;
	}
	case DESK_LOOKS_PICTURE:
		it->picture = desk_look_picture(look);
		it->along = look->along;
		it->thick = look->thick;
		it->look = it->picture ? DESK_LOOK_PICTURE : DESK_LOOK_CARD;
		break;
	}
}

/* What it looks like as it would, in the words of looks.h. */
static const char *drawer_look_usual(const struct desk_drawer_item *it)
{
	static const char *const TOOL[DESK_DOCK_COUNT] = { "terminal", "compass", "folder", "notepad",
		"screwdriver" };
	if (it->look >= 0 && it->look < DESK_DOCK_COUNT) {
		return TOOL[it->look];
	}
	return it->look == DESK_LOOK_NOTEBOOK ? "notebook" : "card";
}

/* What entry i looks like lying there. The first program of a kind is the
 * thing itself; another of the same kind is a card, so the two can be told
 * apart. */
static void drawer_item_usual(const struct desk_server *server, const struct desk_app_list *l,
	const struct drawer_entry *e, struct desk_drawer_item *it)
{
	memset(it, 0, sizeof *it);
	switch (e->kind) {
	case DRAWER_APP: {
		const struct desk_app *app = desk_apps_find(l, e->id);
		it->look = DESK_LOOK_CARD;
		if (!app) {
			snprintf(it->name, sizeof it->name, "%s", e->id);
			desk_initial_of(e->id, it->initial, sizeof it->initial);
			break;
		}
		bool first = app->model >= 0 && app->model < DESK_ICON_GEAR;
		/* the thing is the standard one, if that is in the drawer: this is
		 * a card */
		for (int i = 0; first && i < server->drw_n; i++) {
			const struct drawer_entry *o = &server->drw[i];
			first = !(o->kind == DRAWER_STD && o->present && o->std == app->model);
		}
		for (int i = 0; first && i < server->drw_n && &server->drw[i] != e; i++) {
			const struct drawer_entry *o = &server->drw[i];
			if (o->kind == DRAWER_APP && o->present) {
				const struct desk_app *oa = desk_apps_find(l, o->id);
				first = !(oa && oa->model == app->model);
			}
		}
		if (first) {
			it->look = app->model == DESK_ICON_NOTES && !strcmp(app->id, "desk-notepad.desktop")
				? DESK_LOOK_NOTEBOOK : app->model;
		} else {
			it->icon = desk_app_icon(app);
		}
		snprintf(it->name, sizeof it->name, "%s", app->name);
		snprintf(it->initial, sizeof it->initial, "%s", app->initial);
		it->seed = app->seed;
		break;
	}
	case DRAWER_STD: {
		const struct desk_app *app = desk_apps_for_std(l, e->std);
		it->look = e->std;
		if (app && e->std == DESK_ICON_NOTES && !strcmp(app->id, "desk-notepad.desktop")) {
			it->look = DESK_LOOK_NOTEBOOK;
		}
		if (app) {
			snprintf(it->name, sizeof it->name, "%s", app->name);
			snprintf(it->initial, sizeof it->initial, "%s", app->initial);
			it->seed = app->seed;
		} else {
			snprintf(it->name, sizeof it->name, "%s", desk_std_name(e->std));
			it->seed = (unsigned)e->std * 13u + 5u;
		}
		break;
	}
	case DRAWER_PANEL:
		it->look = DESK_ICON_GEAR;
		snprintf(it->name, sizeof it->name, "%s", TR("Shell Settings"));
		it->seed = 7;
		break;
	case DRAWER_CAMERA:
		it->look = DESK_LOOK_CAMERA;
		snprintf(it->name, sizeof it->name, "%s", TR("Camera"));
		it->seed = 23;
		break;
	case DRAWER_THING:
		e->thing->drawer_item(it);
		break;
	case DRAWER_SHEET:
		it->look = DESK_LOOK_PACKET;
		it->packet = e->packet;
		if (e->sheet) {
			snprintf(it->name, sizeof it->name, "%s", e->sheet->name);
			it->seed = e->sheet->seed;
		}
		break;
	}
}

static void drawer_item_of(const struct desk_server *server, const struct desk_app_list *l,
	const struct drawer_entry *e, struct desk_drawer_item *it)
{
	drawer_item_usual(server, l, e, it);
	const struct desk_look *look = desk_look_find(drawer_look_of(server, e));
	if (look) {
		drawer_look_apply(l, e, look, it);
	}
}

/* Draw what lies in the drawer again, if it has changed: one buffer the
 * size of the felt, laid over the drawer sheet and moved with it. */
static void drawer_compose(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	struct desk_room_sheets sh;
	if (w < 8 || h < 8 || !desk_room_sheets(w, h, &sh)
		|| !desk_drawer_felt_box(w, h, server->drw_box)) {
		if (server->drw_node) {
			wlr_scene_node_set_enabled(&server->drw_node->node, false);
		}
		return;
	}
	/* nothing of it shows with the drawer in: it is drawn as it comes out */
	if (server->drawer_t <= 0.004 && !(server->drawer_moving && server->drawer_to > 0.5)) {
		return;
	}
	drawer_match(server);
	if (!server->drw_dirty && server->drw_node) {
		return;
	}
	server->drw_dirty = false;
	prof_begin(PROF_DOCK);
	struct desk_drawer_item items[DRAWER_MAX];
	double xs[DRAWER_MAX], fits[DRAWER_MAX];
	int n = 0, hover = -1;
	const struct desk_app_list *l = desk_apps_acquire();
	for (int i = 0; i < server->drw_n; i++) {
		const struct drawer_entry *e = &server->drw[i];
		if (!e->present) {
			continue;
		}
		if (i == server->drw_hover) {
			hover = n;
		}
		drawer_item_of(server, l, e, &items[n]);
		xs[n] = e->x;
		fits[n] = e->fit;
		n++;
	}
	/* as fine as the sheet is shown: magnified to the screen, and the
	 * screen's own pixels finer than its points */
	double zoom, pan, t;
	desk_room_view(server->drawer_t, &sh, &zoom, &pan, &t);
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	double res = clampf_d((double)(int)(h * zoom + 0.5) / sh.h * ow / w, 1.0, 4.0);
	server->drw_res = res;
	struct desk_buf *buf = buf_create((int)ceil(server->drw_box[2] * res),
		(int)ceil(server->drw_box[3] * res));
	cairo_save(buf->cr);
	cairo_scale(buf->cr, res, res);
	cairo_translate(buf->cr, -server->drw_box[0], -server->drw_box[1]);
	desk_drawer_paint_items(buf->cr, w, h, items, n, xs, fits, hover);
	cairo_restore(buf->cr);
	desk_apps_release(l);
	scene_put(&server->drw_node, server->layer_drawer, buf, 0, 0,
		(void *)(uintptr_t)DESK_ROLE_DOCK);
	wlr_scene_node_raise_to_top(&server->drw_node->node);
	prof_end();
	room_layout(server);
}

static void drawer_hover_to(struct desk_server *server, int hover)
{
	if (hover == server->drw_hover) {
		return;
	}
	server->drw_hover = hover;
	server->drw_dirty = true;
	drawer_compose(server);
}

/* Which entry is under a point, or -1: by where they lie now, mid-slide
 * or not. */
static int drawer_entry_at(struct desk_server *server, double px, double py)
{
	double x;
	bool row;
	if (server->drawer_t < 0.35
		|| !desk_drawer_at(server->desk_w, server->desk_h, server->drawer_t, px, py, &x, &row)
		|| !row) {
		return -1;
	}
	double half = desk_drawer_pitch(drawer_shown(server)) * 0.5;
	int best = -1;
	for (int i = 0; i < server->drw_n; i++) {
		const struct drawer_entry *e = &server->drw[i];
		double d = fabs(x - e->x);
		if (e->present && d < half) {
			half = d;
			best = i;
		}
	}
	return best;
}

/* The place in the row a thing carried to x along it would go. */
static int drawer_gap_for(struct desk_server *server, double x)
{
	int shown = drawer_shown(server);
	int m = shown + 1;
	int k = (int)lround(x / desk_drawer_pitch(m) + (m - 1) * 0.5);
	return k < 0 ? 0 : (k > shown ? shown : k);
}

/* Put e in the row at place gap (counting only what is shown). */
static void drawer_insert(struct desk_server *server, const struct drawer_entry *e, int gap)
{
	if (server->drw_n >= DRAWER_MAX) {
		return;
	}
	int at = server->drw_n, k = 0;
	for (int i = 0; i < server->drw_n; i++) {
		if (!server->drw[i].present) {
			continue;
		}
		if (k == gap) {
			at = i;
			break;
		}
		k++;
	}
	memmove(&server->drw[at + 1], &server->drw[at],
		(size_t)(server->drw_n - at) * sizeof server->drw[0]);
	server->drw[at] = *e;
	server->drw_n++;
}

static void drawer_remove(struct desk_server *server, int i, struct drawer_entry *out)
{
	if (out) {
		*out = server->drw[i];
	} else if (server->drw[i].packet) {
		cairo_surface_destroy(server->drw[i].packet);
	}
	memmove(&server->drw[i], &server->drw[i + 1],
		(size_t)(server->drw_n - i - 1) * sizeof server->drw[0]);
	server->drw_n--;
	if (server->drw_hover == i) {
		server->drw_hover = -1;
	} else if (server->drw_hover > i) {
		server->drw_hover--;
	}
	/* and the one being pressed, which a window closing just now may
	 * have taken out from under the hand */
	if (server->drw_press == i) {
		server->drw_press = -1;
	} else if (server->drw_press > i) {
		server->drw_press--;
	}
}

static bool drawer_has_app(const struct desk_server *server, const char *id)
{
	for (int i = 0; i < server->drw_n; i++) {
		if (server->drw[i].kind == DRAWER_APP && strcmp(server->drw[i].id, id) == 0) {
			return true;
		}
	}
	const struct drawer_entry *c = &server->drw_carry;
	return server->drw_carrying && c->kind == DRAWER_APP && strcmp(c->id, id) == 0;
}

/* A program put in the drawer, after the programs already there -- or
 * taken out of it. Kept. */
static void drawer_keep_app(struct desk_server *server, const char *id, bool keep)
{
	/* what it comes with goes in first, or it would replace this later */
	drawer_match(server);
	if (keep) {
		if (drawer_has_app(server, id) || server->drw_n >= DRAWER_MAX) {
			return;
		}
		int at = 0;
		for (int i = 0; i < server->drw_n; i++) {
			if (server->drw[i].kind == DRAWER_APP || server->drw[i].kind == DRAWER_STD) {
				at = i + 1;
			}
		}
		struct drawer_entry e = { .kind = DRAWER_APP, .present = true, .fit = 1.0,
			.fit_to = 1.0 };
		snprintf(e.id, sizeof e.id, "%s", id);
		memmove(&server->drw[at + 1], &server->drw[at],
			(size_t)(server->drw_n - at) * sizeof server->drw[0]);
		server->drw[at] = e;
		server->drw_n++;
		wlr_log(WLR_INFO, "%s is put in the drawer", id);
	} else {
		for (int i = 0; i < server->drw_n; i++) {
			if (server->drw[i].kind == DRAWER_APP && strcmp(server->drw[i].id, id) == 0) {
				drawer_remove(server, i, NULL);
				wlr_log(WLR_INFO, "%s is taken out of the drawer", id);
				break;
			}
		}
	}
	drawer_layout(server, true);
	drawer_save(server);
}

/* In or out, from wherever it has got to. */
static void drawer_move_to(struct desk_server *server, double to)
{
	bool out = server->drawer_moving ? server->drawer_to > 0.5 : server->drawer_t > 0.5;
	if (out != (to > 0.5)) {
		drawer_toggle(server);
	}
}

static bool window_under(struct desk_server *server);

/* The packet that lies on the desk, not in the drawer, under the pointer:
 * one lying under a window is not, there, the window is. */
static struct desk_sheet *parked_sheet_at(struct desk_server *server, double px, double py)
{
	double x = px - server->layer_parked->node.x, y = py - server->layer_parked->node.y;
	bool covered = window_under(server);
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		struct desk_sheet *s = &view->sheet;
		if (!s->in_drawer && sheet_hit(s, x, y)
			&& !(covered && s->tree && s->tree->node.parent == server->layer_folded)) {
			return s;
		}
	}
	return NULL;
}

/* Sheet entry i out of the drawer, lying where it lay in it -- or, the
 * drawer shut, where it would have been put down on the desk -- to be
 * opened out from there. */
static void drawer_lift_sheet(struct desk_server *server, int i)
{
	struct desk_sheet *s = server->drw[i].sheet;
	double sx = 0.0, sy = 0.0;
	bool there = server->drawer_t > 0.05 && desk_drawer_point(server->desk_w,
		server->desk_h, server->drawer_t, server->drw[i].x, &sx, &sy);
	drawer_remove(server, i, NULL);
	drawer_layout(server, false);
	if (!s) {
		return;
	}
	s->in_drawer = false;
	s->slot = sheet_alloc_slot(server, first_output(server), s);
	int ow, oh, pad;
	desk_fold_extent(s->fold, 1.0, &ow, &oh, &pad);
	if (there) {
		s->x1 = sx - server->layer_parked->node.x - ow * 0.5;
		s->y1 = sy - server->layer_parked->node.y - oh * 0.5;
	} else {
		sheet_slot_xy(server, first_output(server), s->slot, ow, oh, &s->x1, &s->y1);
	}
	sheet_present(server, s, 1.0, s->x1, s->y1);
}

static int drawer_index_of(struct desk_server *server, const struct desk_sheet *sheet)
{
	for (int i = 0; i < server->drw_n; i++) {
		if (server->drw[i].kind == DRAWER_SHEET && server->drw[i].sheet == sheet) {
			return i;
		}
	}
	return -1;
}

/* A sheet folded straight into the drawer: at the end of the row. (The
 * drawer full, it is put down on the desk as any other.) */
static void drawer_put_in(struct desk_server *server, struct desk_sheet *sheet)
{
	if (server->drw_n >= DRAWER_MAX || drawer_index_of(server, sheet) >= 0 || !sheet->fold) {
		int ow, oh, pad;
		desk_fold_extent(sheet->fold, 1.0, &ow, &oh, &pad);
		sheet->slot = sheet_alloc_slot(server, first_output(server), sheet);
		sheet_slot_xy(server, first_output(server), sheet->slot, ow, oh,
			&sheet->x1, &sheet->y1);
		sheet_present(server, sheet, 1.0, sheet->x1, sheet->y1);
		return;
	}
	struct drawer_entry e = { .kind = DRAWER_SHEET, .sheet = sheet, .present = true,
		.fit = 1.0, .fit_to = 1.0 };
	e.packet = desk_fold_packet(sheet->fold);
	server->drw[server->drw_n++] = e;
	sheet->in_drawer = true;
	sheet->slot = -1;
	if (sheet->tree) {
		wlr_scene_node_set_enabled(&sheet->tree->node, false);
	}
	drawer_layout(server, true);
	drawer_compose(server);
}

/* Take hold of entry i, or of a packet on the desk: carried from here on,
 * under the hand, over everything. */
static void drawer_pick_up(struct desk_server *server)
{
	struct drawer_entry *c = &server->drw_carry;
	server->drw_gx = server->drw_gy = 0.5;
	server->drw_sprung = false;
	if (server->drw_press >= 0) {
		/* the place it leaves is held open, so nothing moves yet */
		int i = server->drw_press, k = 0;
		for (int j = 0; j < i; j++) {
			k += server->drw[j].present;
		}
		drawer_remove(server, i, c);
		server->drw_carry_from = k;
		server->drw_gap = k;
		server->drw_over = true;
		drawer_layout(server, false);
	} else {
		struct desk_sheet *s = server->drw_press_sheet;
		*c = (struct drawer_entry){ .kind = DRAWER_SHEET, .sheet = s, .present = true,
			.fit = 1.0, .fit_to = 1.0 };
		server->drw_carry_from = -1;
		server->drw_gap = -1;
		server->drw_over = false;
		/* held where the hand took hold of it */
		double lx = server->drw_px - server->layer_parked->node.x;
		double ly = server->drw_py - server->layer_parked->node.y;
		server->drw_gx = s->hit_w > 0 ? (lx - s->hit_x) / s->hit_w : 0.5;
		server->drw_gy = s->hit_h > 0 ? (ly - s->hit_y) / s->hit_h : 0.5;
	}
	/* off the felt; or a packet lifted off the desk */
	if (c->kind != DRAWER_SHEET || server->drw_carry_from >= 0) {
		desk_sound_play(DESK_SND_FELT_PICK, snd_pan(server, server->drw_px), 1.0, 0.0);
	} else {
		snd_at(server, DESK_SND_CORNER_LIFT, server->drw_px, 1.3);
	}
	server->drw_press = -1;
	server->drw_press_sheet = NULL;
	server->drw_carrying = true;
	server->drw_hover = -1;
	if (c->kind == DRAWER_SHEET && c->sheet && !c->packet) {
		c->packet = desk_fold_packet(c->sheet->fold);
	}
	/* the picture of it, held up */
	const struct desk_app_list *l = desk_apps_acquire();
	struct desk_drawer_item it;
	drawer_item_of(server, l, c, &it);
	double ax = 0.0, ay = 0.0;
	cairo_surface_t *pic = desk_drawer_item_picture(server->desk_w, server->desk_h, &it,
		&ax, &ay);
	desk_apps_release(l);
	if (pic) {
		int pw = cairo_image_surface_get_width(pic), ph = cairo_image_surface_get_height(pic);
		struct desk_buf *b = buf_create(pw, ph);
		cairo_set_operator(b->cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(b->cr, pic, 0, 0);
		cairo_paint(b->cr);
		cairo_surface_destroy(pic);
		scene_put(&server->drw_carry_node, server->layer_drag, b, 0, 0, NULL);
		wlr_scene_buffer_set_opacity(server->drw_carry_node, 1.0f);
		server->drw_carry_ax = ax;
		server->drw_carry_ay = ay;
	}
	server->drw_poof_at = 0.0;
	server->drw_dirty = true;
	drawer_compose(server);
}

/* The carried thing under the hand: over the drawer, as it will lie in it;
 * a packet over the desk, as the packet it is. */
static void drawer_carry_show(struct desk_server *server)
{
	double cx = server->cursor->x, cy = server->cursor->y;
	struct drawer_entry *c = &server->drw_carry;
	struct desk_sheet *s = c->kind == DRAWER_SHEET ? c->sheet : NULL;
	bool packet = s && !server->drw_over && s->fold;
	if (packet) {
		int ow, oh, pad;
		desk_fold_extent(s->fold, 1.0, &ow, &oh, &pad);
		double x = cx - server->drw_gx * ow, y = cy - server->drw_gy * oh;
		if (!s->tree || s->tree->node.parent != server->layer_drag || !s->tree->node.enabled) {
			sheet_present(server, s, 1.0, x, y);
			wlr_scene_node_reparent(&s->tree->node, server->layer_drag);
		}
		wlr_scene_node_set_enabled(&s->tree->node, true);
		wlr_scene_node_set_position(&s->tree->node, (int)(x - pad), (int)(y - pad));
	} else if (s && s->tree) {
		wlr_scene_node_set_enabled(&s->tree->node, false);
	}
	if (server->drw_carry_node) {
		wlr_scene_node_set_enabled(&server->drw_carry_node->node, !packet);
		wlr_scene_node_set_position(&server->drw_carry_node->node,
			(int)(cx - server->drw_carry_ax), (int)(cy - server->drw_carry_ay));
		/* a program carried off over the desk is on its way out of it */
		bool leaving = !server->drw_over
			&& (c->kind == DRAWER_APP || c->kind == DRAWER_STD);
		wlr_scene_buffer_set_opacity(server->drw_carry_node, leaving ? 0.55f : 1.0f);
	}
}

static void drawer_carry_move(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	double cx = server->cursor->x, cy = server->cursor->y;
	struct drawer_entry *c = &server->drw_carry;
	/* a packet carried to the shut drawer's pull: it comes out for it
	 * (not for one only being moved about the desk with the right button) */
	if (c->kind == DRAWER_SHEET && !server->drw_sprung && server->drw_button != BTN_RIGHT
		&& !pointer_on_side(server) && cy >= h - desk_drawer_pull_h(w, h) - 6
		&& !(server->drawer_moving ? server->drawer_to > 0.5 : server->drawer_t > 0.5)) {
		server->drw_sprung = true;
		drawer_move_to(server, 1.0);
	}
	double x = 0.0;
	bool row;
	bool over = desk_drawer_at(w, h, server->drawer_t, cx, cy, &x, &row);
	int gap = -1;
	if (over) {
		gap = drawer_gap_for(server, x);
	} else if (c->kind == DRAWER_PANEL || c->kind == DRAWER_CAMERA || c->kind == DRAWER_THING) {
		/* the screwdriver, the camera and what is kept here cannot be carried
		 * out: their places stay open for them */
		gap = server->drw_carry_from;
	}
	server->drw_over = over;
	if (gap != server->drw_gap) {
		server->drw_gap = gap;
		drawer_layout(server, false);
	}
	drawer_carry_show(server);
}

/* Let go of what is carried. */
static void drawer_drop(struct desk_server *server)
{
	struct drawer_entry c = server->drw_carry;
	int w = server->desk_w, h = server->desk_h;
	double cx = server->cursor->x, cy = server->cursor->y;
	double x = 0.0;
	bool row;
	bool over = server->drw_over
		&& desk_drawer_at(w, h, server->drawer_t, cx, cy, &x, &row);
	int gap = server->drw_gap;
	server->drw_carrying = false;
	server->drw_carry = (struct drawer_entry){ 0 };
	server->drw_gap = -1;
	bool keep = false;
	struct desk_sheet *s = c.kind == DRAWER_SHEET ? c.sheet : NULL;
	/* the drawer full, a packet off the desk does not go in: it is put down
	 * on the desk where it was let go, as any other */
	if (over && gap >= 0 && (!s || server->drw_n < DRAWER_MAX)) {
		/* into the drawer, where it was let go, and along to its place */
		snd_at(server, DESK_SND_FELT_DROP, cx, 1.0);
		c.x = x;
		drawer_insert(server, &c, gap);
		keep = c.kind != DRAWER_SHEET;
		if (s) {
			s->in_drawer = true;
			s->slot = -1;
			if (s->tree) {
				wlr_scene_node_set_enabled(&s->tree->node, false);
				wlr_scene_node_reparent(&s->tree->node, server->layer_parked);
			}
		}
		if (server->drw_carry_node) {
			wlr_scene_node_set_enabled(&server->drw_carry_node->node, false);
		}
	} else if (c.kind == DRAWER_PANEL || c.kind == DRAWER_CAMERA || c.kind == DRAWER_THING) {
		/* back where it was */
		snd_at(server, DESK_SND_FELT_DROP, cx, 0.7);
		drawer_insert(server, &c, server->drw_carry_from >= 0 ? server->drw_carry_from : 0);
		if (server->drw_carry_node) {
			wlr_scene_node_set_enabled(&server->drw_carry_node->node, false);
		}
	} else if (c.kind == DRAWER_APP || c.kind == DRAWER_STD) {
		/* taken out of the drawer: it fades from the hand */
		server->drw_poof_at = monotonic_sec();
		keep = true;
		server_schedule_frames(server);
	} else if (s) {
		/* laid down where it was let go -- on the desk, or on the screen
		 * beside it the hand is over -- inside that screen's edges */
		if (c.packet) {
			cairo_surface_destroy(c.packet);
		}
		if (server->drw_carry_node) {
			wlr_scene_node_set_enabled(&server->drw_carry_node->node, false);
		}
		int ow, oh, pad;
		desk_fold_extent(s->fold, 1.0, &ow, &oh, &pad);
		struct wlr_output *out = screen_at(server, cx, cy);
		struct wlr_output *was = s->in_drawer ? NULL : sheet_screen(server, s);
		struct wlr_box wa = screen_area(server, out);
		double lx = cx - server->drw_gx * ow - server->layer_parked->node.x;
		double ly = cy - server->drw_gy * oh - server->layer_parked->node.y;
		lx = clampf_d(lx, wa.x, fmax(wa.x, wa.x + wa.width - ow));
		ly = clampf_d(ly, wa.y, fmax(wa.y, wa.y + wa.height - oh));
		s->in_drawer = false;
		/* a place along the front of the screen it is on now */
		if (s->slot < 0 || was != out) {
			s->slot = sheet_alloc_slot(server, out, s);
		}
		if (s->tree) {
			wlr_scene_node_reparent(&s->tree->node, server->layer_parked);
		}
		s->phase = SHEET_PARKED;
		s->x1 = lx;
		s->y1 = ly;
		sheet_present(server, s, 1.0, lx, ly);
		snd_at(server, DESK_SND_PAPER_SETTLE, cx, 1.0);
	}
	if (keep) {
		drawer_save(server);
	}
	drawer_layout(server, false);
	drawer_compose(server);
	server_schedule_frames(server);
}

/* A sheet is gone (its window has), or is being opened out by some other
 * way than the drawer: out of the drawer and out of the hand. */
static void drawer_forget(struct desk_server *server, struct desk_sheet *sheet)
{
	if (server->drw_press_sheet == sheet) {
		server->drw_press_sheet = NULL;
	}
	if (server->drw_carrying && server->drw_carry.sheet == sheet) {
		if (server->drw_carry.packet) {
			cairo_surface_destroy(server->drw_carry.packet);
		}
		server->drw_carry = (struct drawer_entry){ 0 };
		server->drw_carrying = false;
		server->drw_gap = -1;
		if (server->drw_carry_node) {
			wlr_scene_node_set_enabled(&server->drw_carry_node->node, false);
		}
		if (sheet->tree) {
			wlr_scene_node_reparent(&sheet->tree->node, server->layer_parked);
		}
		drawer_layout(server, false);
	}
	int i = drawer_index_of(server, sheet);
	if (i >= 0) {
		drawer_remove(server, i, NULL);
		drawer_layout(server, false);
	}
	sheet->in_drawer = false;
}

/* What a hand on entry i is for. */
static void drawer_activate(struct desk_server *server, int i)
{
	struct drawer_entry *e = &server->drw[i];
	snd_at(server, DESK_SND_FELT_PICK, server->cursor->x, 1.0);
	switch (e->kind) {
	case DRAWER_APP: {
		const struct desk_app_list *l = desk_apps_acquire();
		const struct desk_app *app = desk_apps_find(l, e->id);
		if (app) {
			wlr_log(WLR_INFO, "taking %s out of the drawer", app->id);
			desk_app_launch(app);
		}
		desk_apps_release(l);
		break;
	}
	case DRAWER_STD: {
		const struct desk_app_list *l = desk_apps_acquire();
		const struct desk_app *app = desk_apps_for_std(l, e->std);
		if (app) {
			wlr_log(WLR_INFO, "taking the %s, %s, out of the drawer", desk_std_name(e->std),
				app->id);
			desk_app_launch(app);
		}
		desk_apps_release(l);
		break;
	}
	case DRAWER_PANEL:
		/* the screwdriver: Shell Settings, whose window comes on the desk */
		wlr_log(WLR_INFO, "taking the screwdriver, Shell Settings, out of the drawer");
		settings_open();
		break;
	case DRAWER_CAMERA:
		/* the camera: put up to the eye once the drawer is in, so that
		 * what it sees is the desk, not the drawer */
		wlr_log(WLR_INFO, "taking the camera out of the drawer");
		server->snip_from_drawer = true;
		drawer_move_to(server, 0.0);
		return;
	case DRAWER_THING:
		/* onto the desk as the drawer goes in */
		wlr_log(WLR_INFO, "taking the %s out of the drawer", e->thing->name);
		e->thing->take_out(server);
		drawer_move_to(server, 0.0);
		return;
	case DRAWER_SHEET: {
		struct desk_sheet *s = e->sheet;
		drawer_lift_sheet(server, i);
		if (s) {
			sheet_begin_unfold(server, s);
		}
		break;
	}
	}
	/* and the drawer goes back in: what came out of it is on the desk --
	 * unless it is to be left out (Shell Settings) */
	if (server->conf.drawer_closes) {
		drawer_move_to(server, 0.0);
	}
}

static void looks_open(struct desk_server *server, int i);

/* A hand put down: on something in the drawer, or on a packet lying on the
 * desk. Nothing happens until it is lifted or moved. The right button is
 * for moving a folded sheet: anywhere on the desk, or into the drawer if it
 * is out, and a right click that does not move it does nothing; on a
 * program, it asks what the program is to look like (the card of looks). */
static bool drawer_press(struct desk_server *server, uint32_t button)
{
	if (server->drw_carrying || server->drw_press >= 0 || server->drw_press_sheet) {
		return true;   /* already holding something */
	}
	/* (a screen beside the desk's is in reach whatever the desk's is
	 * turned to) */
	if (!pointer_on_side(server) && (post_up(server) || console_up(server) || shelf_up(server))) {
		return false;
	}
	bool right = button == BTN_RIGHT;
	double cx = server->cursor->x, cy = server->cursor->y;
	if (server->drawer_t > 0.004) {
		int i = drawer_entry_at(server, cx, cy);
		if (i >= 0 && (!right || server->drw[i].kind == DRAWER_SHEET || server->drw[i].kind == DRAWER_APP
				|| server->drw[i].kind == DRAWER_STD)) {
			server->drw_press = i;
			server->drw_px = cx;
			server->drw_py = cy;
			server->drw_button = button;
			return true;
		}
		if (cy >= drawer_line(server)) {
			return false;
		}
	}
	struct desk_sheet *s = parked_sheet_at(server, cx, cy);
	if (!s) {
		return false;
	}
	if (s->phase != SHEET_PARKED) {
		/* on its way down or up: it goes back the way it came */
		if (!right) {
			sheet_begin_unfold(server, s);
		}
		return true;
	}
	server->drw_press_sheet = s;
	server->drw_px = cx;
	server->drw_py = cy;
	server->drw_button = button;
	return true;
}

static bool drawer_motion(struct desk_server *server)
{
	if (server->drw_carrying) {
		drawer_carry_move(server);
		return true;
	}
	if (server->drw_press < 0 && !server->drw_press_sheet) {
		return false;
	}
	/* a little way is still a click: a hand is not a pin */
	if (hypot(server->cursor->x - server->drw_px, server->cursor->y - server->drw_py) < 6.0) {
		return true;
	}
	if (server->drw_button == BTN_RIGHT && server->drw_press >= 0
		&& server->drw[server->drw_press].kind != DRAWER_SHEET) {
		/* (a program held with the other button is asked about, not moved) */
		return true;
	}
	drawer_pick_up(server);
	drawer_carry_move(server);
	return true;
}

static bool drawer_release(struct desk_server *server)
{
	bool right = server->drw_button == BTN_RIGHT;
	server->drw_button = 0;
	if (server->drw_carrying) {
		drawer_drop(server);
		cursor_image(server, "default");
		return true;
	}
	if (right && server->drw_press >= 0 && server->drw_press < server->drw_n
		&& server->drw[server->drw_press].kind != DRAWER_SHEET) {
		/* the other button on a program: what is it to look like? */
		int i = server->drw_press;
		server->drw_press = -1;
		looks_open(server, i);
		return true;
	}
	if (right && (server->drw_press >= 0 || server->drw_press_sheet)) {
		/* moved nowhere: a right click on a sheet is not a use of it */
		server->drw_press = -1;
		server->drw_press_sheet = NULL;
		return true;
	}
	if (server->drw_press >= 0) {
		int i = server->drw_press;
		server->drw_press = -1;
		if (i < server->drw_n) {
			drawer_activate(server, i);
		}
		return true;
	}
	if (server->drw_press_sheet) {
		struct desk_sheet *s = server->drw_press_sheet;
		server->drw_press_sheet = NULL;
		sheet_begin_unfold(server, s);
		drawer_move_to(server, 0.0);
		return true;
	}
	return false;
}

/* ---- the card of looks ------------------------------------------------
 *
 * The other button on a program in the drawer (a finger held on it) brings up
 * a card over it from a catalogue, of what it could look like lying there
 * (draw.c's desk_looks_card_paint): touched, it looks like that, and the card
 * goes. The last on it chooses a picture from anywhere (desk-look pick). A hand
 * anywhere else, the drawer going in or Escape, and the card goes as it was. */

#define LOOKS_UP_S 0.16
#define LOOKS_DOWN_S 0.12
#define LOOKS_RISE_PT 14.0

static void looks_close(struct desk_server *server);
