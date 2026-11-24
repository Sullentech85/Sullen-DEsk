/* Part of wm.c: a window's paper: its base, taking a sheet, the names of games.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* Open, all at once: what Alt+Tab's spread does with a folded one chosen,
 * whose picture it has already carried to where the window lies. */
static void sheet_open_now(struct desk_server *server, struct desk_sheet *sheet)
{
	if (sheet->phase == SHEET_IDLE) {
		return;
	}
	if (sheet->in_drawer) {
		int i = drawer_index_of(server, sheet);
		if (i >= 0) {
			drawer_remove(server, i, NULL);
			drawer_layout(server, false);
		}
		sheet->in_drawer = false;
	}
	drawer_forget(server, sheet);
	sheet->to_drawer = false;
	sheet_snd_forget(sheet);
	sheet->passed = false;
	sheet_opened(sheet);
	server_schedule_frames(server);
}

static void sheet_begin_fold(struct desk_server *server, struct desk_sheet *sheet,
	cairo_surface_t *src, double x, double y, struct wlr_scene_tree *live, void *hit,
	cairo_surface_t *icon, const char *name, struct wlr_output *out)
{
	if (sheet->phase == SHEET_FOLDING) {
		cairo_surface_destroy(src);
		return;
	}
	if (sheet->phase == SHEET_PARKED || sheet->phase == SHEET_UNFOLDING) {
		cairo_surface_destroy(src);
		sheet_begin_unfold(server, sheet);
		return;
	}
	if (sheet->src) {
		cairo_surface_destroy(sheet->src);
	}
	sheet->src = src;
	sheet->phase = SHEET_FOLDING;
	sheet->dur = 1.10;
	sheet->start = monotonic_sec();
	sheet->x0 = x;
	sheet->y0 = y;
	sheet->live = live;
	sheet->hit = hit;
	sheet->in_drawer = false;
	/* the window's title, as much as fits, cut between characters: cairo
	 * refuses half of one, and everything after it on the same sheet */
	size_t nl = name ? strlen(name) : 0;
	if (nl >= sizeof sheet->name) {
		nl = sizeof sheet->name - 1;
		while (nl > 0 && ((unsigned char)name[nl] & 0xc0) == 0x80) {
			nl--;
		}
	}
	memcpy(sheet->name, name ? name : "", nl);
	sheet->name[nl] = '\0';
	if (sheet->tree && sheet->tree->node.parent != server->layer_parked) {
		wlr_scene_node_reparent(&sheet->tree->node, server->layer_parked);
	}
	sheet->slot = sheet_alloc_slot(server, out, sheet);
	static unsigned sheet_seed;
	sheet->seed = ++sheet_seed;
	/* folded down to about a postcard, whatever size it was */
	double packet = fmax(153.0 * desk_px_per_pt(server->desk_w), 96.0);
	desk_fold_free(sheet->fold);
	sheet->fold = sheet->book ? desk_fold_new_book(src, sheet->seed, packet, name)
		: sheet->by_hand ? desk_fold_new_by_hand(src, sheet->seed, packet, icon, name)
		: desk_fold_new(src, sheet->seed, packet, icon, name);
	int ow, oh, pad;
	desk_fold_extent(sheet->fold, 1.0, &ow, &oh, &pad);
	sheet->to_drawer = server->conf.fold_drawer && server->drw_n < DRAWER_MAX;
	if (sheet->to_drawer) {
		/* down over the front edge of the desk, where the drawer is, and
		 * into it */
		sheet->slot = -1;
		sheet->x1 = server->desk_w * 0.5 - ow * 0.5 - server->layer_parked->node.x;
		sheet->y1 = server->desk_h + 4 - server->layer_parked->node.y;
	} else {
		/* along the front of the screen it was on */
		sheet_slot_xy(server, out, sheet->slot, ow, oh, &sheet->x1, &sheet->y1);
	}
	if (live) {
		wlr_scene_node_set_enabled(&live->node, false);
	}
	/* the first flap goes over */
	sheet_snd_forget(sheet);
	sheet->snd_k = 0;
	{
		int n = desk_fold_stages(sheet->fold);
		snd_for(server, DESK_SND_PAPER_SWING, x + ow * 0.5, 0.9, 0.40,
			sheet->dur * desk_fold_done(sheet->fold) / (n > 0 ? n : 1));
	}
	server_schedule_frames(server);
	sheet_tick_one(server, sheet);
}

static void sheet_cleanup(struct desk_server *server, struct desk_sheet *sheet)
{
	drawer_forget(server, sheet);
	if (sheet->src) {
		cairo_surface_destroy(sheet->src);
		sheet->src = NULL;
	}
	desk_fold_free(sheet->fold);
	sheet->fold = NULL;
	if (sheet->tree) {
		wlr_scene_node_destroy(&sheet->tree->node);
		sheet->tree = NULL;
		sheet->buf = NULL;
	}
	sheet->phase = SHEET_IDLE;
	sheet->live = NULL;
}

static bool sheet_hit(struct desk_sheet *sheet, double x, double y)
{
	if (sheet->phase == SHEET_IDLE || !sheet->src) {
		return false;
	}
	double pad = 26;
	return x >= sheet->hit_x - pad && y >= sheet->hit_y - pad &&
		x <= sheet->hit_x + sheet->hit_w + pad && y <= sheet->hit_y + sheet->hit_h + pad;
}

/* What the client last drew, onto cr at (x, y): out of memory where the
 * client shares memory, and back off the GPU where it hands over GPU
 * buffers -- which is what every X program does, Xwayland drawing with GL. */
static bool paint_client(cairo_t *cr, struct wlr_surface *ws, double x, double y)
{
	if (!ws || !ws->buffer) {
		return false;
	}
	struct wlr_buffer *src = ws->buffer->source ? ws->buffer->source : &ws->buffer->base;
	void *data = NULL;
	uint32_t fmt = 0;
	size_t stride = 0;
	if (wlr_buffer_begin_data_ptr_access(src, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &fmt, &stride)) {
		bool ok = data && (fmt == DRM_FORMAT_ARGB8888 || fmt == DRM_FORMAT_XRGB8888);
		if (ok) {
			/* XRGB's fourth byte is anything at all, not an alpha */
			cairo_surface_t *img = cairo_image_surface_create_for_data(data,
				fmt == DRM_FORMAT_XRGB8888 ? CAIRO_FORMAT_RGB24 : CAIRO_FORMAT_ARGB32,
				src->width, src->height, (int)stride);
			cairo_set_source_surface(cr, img, x, y);
			cairo_paint(cr);
			cairo_surface_destroy(img);
		}
		wlr_buffer_end_data_ptr_access(src);
		if (ok) {
			return true;
		}
	}
	struct wlr_texture *tex = wlr_surface_get_texture(ws);
	if (!tex) {
		return false;
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		(int)tex->width, (int)tex->height);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(img);
		return false;
	}
	cairo_surface_flush(img);
	bool ok = wlr_texture_read_pixels(tex, &(struct wlr_texture_read_pixels_options){
		.data = cairo_image_surface_get_data(img),
		.format = DRM_FORMAT_ARGB8888,
		.stride = (uint32_t)cairo_image_surface_get_stride(img),
	});
	if (ok) {
		cairo_surface_mark_dirty(img);
		cairo_set_source_surface(cr, img, x, y);
		cairo_paint(cr);
	}
	cairo_surface_destroy(img);
	return ok;
}

static void curl_sizes(const struct desk_toplevel *view, double *rest, double *open);

static cairo_surface_t *view_snapshot(struct desk_toplevel *view)
{
	int w = view->outer_w;
	int h = view->outer_h;
	if (w < 40) {
		w = 40;
	}
	if (h < 40) {
		h = 40;
	}
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(surf);
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	const struct desk_thing *thing = view_thing(view);
	if (thing) {
		thing->paint(cr, view, 0, 0, w, h, view_title(view), true, true);
	} else if (view_framed(view) && view->own_frame) {
		desk_draw_window_mount(cr, 0, 0, w, h, view_title(view));
	} else if (view_framed(view)) {
		desk_draw_window_frame(cr, 0, 0, w, h, view_title(view), true, 0, false);
	} else {
		/* nothing of ours to fold: a plain sheet under what the client drew */
		cairo_set_source_rgb(cr, 0.96, 0.95, 0.92);
		cairo_paint(cr);
	}
	struct wlr_box geo;
	view_geometry(view, &geo);
	cairo_save(cr);
	cairo_rectangle(cr, l, t, w - l - r, h - t - b);
	cairo_clip(cr);
	paint_client(cr, view_surface(view), l - geo.x, t - geo.y);
	cairo_restore(cr);
	/* and its corner as it is this moment, turned down or lifted, so what
	 * is folded or thrown away is the sheet as it was left */
	if (view->curl && view->curl->node.enabled && view->curl_s > 0.5) {
		double rest, open;
		curl_sizes(view, &rest, &open);
		double sz = clampf_d(view->curl_s, 0.0, fmin(view->outer_w, view->outer_h) * 0.8);
		double B = view->curl_box;
		double lift = clampf_d((sz - rest) / fmax(open - rest, 1.0), 0.0, 1.3);
		cairo_save(cr);
		cairo_translate(cr, w - B, 0);
		cairo_move_to(cr, B - sz, 0);
		cairo_line_to(cr, B, 0);
		cairo_line_to(cr, B, sz);
		cairo_close_path(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_fill(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		desk_draw_curl(cr, B, sz, open, lift, -1, view->maximized != 0, view_title(view));
		cairo_restore(cr);
	}
	cairo_destroy(cr);
	return surf;
}

void focus_next(struct desk_server *server);

/* ---- a sheet taken out of a binder ---------------------------------
 *
 * Taking a program's sheet out of its binder takes the sheet: it comes off
 * the ring into the hand, the binder goes back up, you turn back to the
 * desk with it, and it is put down where the program's window will be.
 * There the paper comes out from behind it, the way it does when a sheet
 * is made bigger, until it is the window's size, the catalogue's print
 * going off it: a blank sheet with the program's name at its head -- the
 * window, waiting for the program. When the program's window comes, it
 * comes there, onto it (held back, if it is quicker than the paper, until
 * the paper is down and open); a program that never sends one has its
 * sheet screwed up and thrown away after a while. */

static void throw_paper(struct desk_server *server, cairo_surface_t *snap, double x0,
	double y0);

/* Where the next new window goes: down and to the right of the last, round
 * again after six. */
static void cascade_next(struct desk_server *server, int *x, int *y)
{
	int n = server->cascade++;
	struct wlr_output *out = screen_at(server, server->cursor->x, server->cursor->y);
	if (screen_is_side(server, out)) {
		/* on the screen beside the desk's that the pointer is on */
		struct wlr_box a = screen_area(server, out);
		*x = a.x + a.width / 6 + (n % 6) * 28;
		*y = a.y + a.height / 10 + (n % 6) * 24;
		return;
	}
	*x = 320 + (n % 6) * 28;
	*y = work_area(server).y + 8 + (n % 6) * 24;
}

/* The size a new window is offered, within what the screen it will come
 * onto (the pointer's) has room for. A notebook is upright, and the size of
 * its page if it can be. */
static void window_offer(struct desk_server *server, int *ow, int *oh, const struct desk_thing *thing)
{
	struct wlr_box wa = screen_area(server,
		screen_at(server, server->cursor->x, server->cursor->y));
	if (thing && thing->first_size) {
		thing->first_size(&wa, ow, oh);
		return;
	}
	*ow = 680;
	*oh = 430;
	if (*ow > wa.width - DESK_BORDER * 2) {
		*ow = wa.width - DESK_BORDER * 2;
	}
	if (*oh > wa.height - DESK_TITLE_H - DESK_BORDER - 16) {
		*oh = wa.height - DESK_TITLE_H - DESK_BORDER - 16;
	}
}

/* The most of a window there is room for on the screen a new one comes onto (the pointer's), its
 * paper and heading round it. */
static void window_room(struct desk_server *server, int *w, int *h)
{
	struct wlr_box wa = screen_area(server,
		screen_at(server, server->cursor->x, server->cursor->y));
	*w = wa.width - DESK_BORDER * 2;
	*h = wa.height - DESK_TITLE_H - DESK_BORDER - 16;
	*w = *w < 160 ? 160 : *w;
	*h = *h < 120 ? 120 : *h;
}

/* A window coming onto the desk, all of it on the screen (2026-11-12, the owner: 低解像度にも対応
 * できるように): one bigger than the room there is is asked to be that size, and one that would
 * come over an edge -- the cascade starts 320 in -- is put in from it. A thing has rules of its
 * own, and a window opened out or on the whole screen is where it is. */
static void view_fit_on_screen(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (!view->scene_tree || view->thing || view->maximized || view->fullscreen) {
		return;
	}
	struct wlr_output *out = screen_at(server, view->scene_tree->node.x + 1,
		view->scene_tree->node.y + 1);
	struct wlr_box wa = screen_is_side(server, out) ? screen_area(server, out) : work_area(server);
	if (wa.width < 64 || wa.height < 64) {
		return;
	}
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	int ow = view->outer_w, oh = view->outer_h;
	if (ow > wa.width || oh > wa.height) {
		int iw = (ow > wa.width ? wa.width : ow) - l - r;
		int ih = (oh > wa.height ? wa.height : oh) - t - b;
		if (iw >= 160 && ih >= 120) {
			wlr_log(WLR_INFO, "%s: %dx%d is more than the screen has room for: asked for %dx%d",
				view_title(view), ow, oh, iw, ih);
			view_set_size(view, iw, ih);
			ow = iw + l + r;
			oh = ih + t + b;
		}
	}
	int x = view->scene_tree->node.x, y = view->scene_tree->node.y;
	x = x + ow > wa.x + wa.width ? wa.x + wa.width - ow : x;
	y = y + oh > wa.y + wa.height ? wa.y + wa.height - oh : y;
	x = x < wa.x ? wa.x : x;
	y = y < wa.y ? wa.y : y;
	if (x != view->scene_tree->node.x || y != view->scene_tree->node.y) {
		wlr_scene_node_set_position(&view->scene_tree->node, x, y);
		view_sync_shadow(view);
	}
}

/* A thing that is a window -- the notebook, the telephone -- is its maker's
 * to size, but is not left half off a screen that has turned the other way
 * up: moved in from the edges, as it is. */
static void view_shift_on_screen(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (!view->scene_tree || !view->thing || view->maximized || view->fullscreen) {
		return;
	}
	struct wlr_output *out = screen_at(server, view->scene_tree->node.x + 1,
		view->scene_tree->node.y + 1);
	struct wlr_box wa = screen_is_side(server, out) ? screen_area(server, out) : work_area(server);
	if (wa.width < 64 || wa.height < 64) {
		return;
	}
	int left = wa.x + view->thing->open_out_left;
	int x = view->scene_tree->node.x, y = view->scene_tree->node.y;
	x = x + view->outer_w > wa.x + wa.width ? wa.x + wa.width - view->outer_w : x;
	y = y + view->outer_h > wa.y + wa.height ? wa.y + wa.height - view->outer_h : y;
	x = x < left ? left : x;
	y = y < wa.y ? wa.y : y;
	if (x != view->scene_tree->node.x || y != view->scene_tree->node.y) {
		wlr_scene_node_set_position(&view->scene_tree->node, x, y);
		view_sync_shadow(view);
	}
}

static void base_free(struct desk_base *b)
{
	if (b->node) {
		wlr_scene_node_destroy(&b->node->node);
	}
	if (b->face) {
		cairo_surface_destroy(b->face);
	}
	if (b->blank) {
		cairo_surface_destroy(b->blank);
	}
	memset(b, 0, sizeof *b);
}

/* A sheet for app, taken out of its binder (face, sw by sh, is how it was
 * printed there), to be put down where its window will go. */
static int base_new(struct desk_server *server, const struct desk_app *app,
	cairo_surface_t *face, double sw, double sh)
{
	int slot = -1;
	double oldest = 1e30;
	int old = -1;
	for (int i = 0; i < DESK_BASES; i++) {
		struct desk_base *b = &server->bases[i];
		if (b->state == BASE_FREE) {
			slot = i;
			break;
		}
		if (b->state == BASE_WAITING && b->taken_at < oldest) {
			oldest = b->taken_at;
			old = i;
		}
	}
	if (slot < 0 && old >= 0) {
		/* the desk only takes so many sheets waiting for their programs */
		base_free(&server->bases[old]);
		slot = old;
	}
	if (slot < 0) {
		cairo_surface_destroy(face);
		return -1;
	}
	struct desk_base *b = &server->bases[slot];
	int ow, oh;
	/* (the blank of the thing it is to be) */
	const struct desk_thing *thing = thing_of_program(app->id);
	window_offer(server, &ow, &oh, thing);
	b->w = ow + (thing ? thing->inset_l + thing->inset_r : DESK_BORDER * 2);
	b->h = oh + (thing ? thing->inset_t + thing->inset_b : DESK_TITLE_H + DESK_BORDER);
	cascade_next(server, &b->x, &b->y);
	/* inside the screen cascade_next chose: the pointer's */
	struct wlr_box wa = screen_area(server,
		screen_at(server, server->cursor->x, server->cursor->y));
	if (b->x + b->w > wa.x + wa.width) {
		b->x = wa.x + wa.width - b->w;
	}
	if (b->y + b->h > wa.y + wa.height) {
		b->y = wa.y + wa.height - b->h;
	}
	b->x = b->x < wa.x ? wa.x : b->x;
	b->y = b->y < wa.y ? wa.y : b->y;
	/* put down, it is further from the eye than it was in the hand */
	double k = fmin(0.78, fmin(b->w * 0.92 / sw, b->h * 0.92 / sh));
	b->lw = sw * k;
	b->lh = sh * k;
	b->face = face;
	if (thing) {
		b->blank = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, b->w, b->h);
		cairo_t *blank = cairo_create(b->blank);
		thing->paint(blank, NULL, 0, 0, b->w, b->h, app->name, false, true);
		cairo_destroy(blank);
		cairo_surface_flush(b->blank);
	} else {
		b->blank = desk_window_base_sheet(b->w, b->h, app->name);
	}
	snprintf(b->app, sizeof b->app, "%s", app->id);
	snprintf(b->name, sizeof b->name, "%s", app->name);
	b->seed = app->seed;
	b->sid = 0;
	b->view = NULL;
	b->state = BASE_CARRIED;
	b->at = monotonic_sec();
	/* how long it has waited for its program is real time, however fast
	 * things move on the desk */
	b->taken_at = real_sec();
	return slot;
}

/* The sheet on the desk, as far open as it is. */
static void base_paint(struct desk_server *server, struct desk_base *b, double open)
{
	int pad = DESK_SHADOW;
	struct desk_buf *buf = buf_create(b->w + pad * 2, b->h + pad * 2);
	desk_set_paint_origin(b->x - pad, b->y - pad);
	cairo_translate(buf->cr, pad, pad);
	desk_draw_window_base(buf->cr, b->blank, b->face, b->w, b->h, b->lw, b->lh, open,
		b->seed);
	desk_set_paint_origin(0, 0);
	bool fresh = b->node == NULL;
	scene_put(&b->node, server->layer_win, buf, b->x - pad, b->y - pad, NULL);
	if (fresh) {
		/* over the windows already on the desk: it was put down on them */
		wlr_scene_node_raise_to_top(&b->node->node);
		views_above_raise(server);
	}
	wlr_scene_node_set_enabled(&b->node->node, true);
}

/* The window that has come to it is let out, and the sheet goes. */
static void base_hand_over(struct desk_server *server, struct desk_base *b)
{
	struct desk_toplevel *view = b->view;
	if (view && view->scene_tree) {
		wlr_scene_node_set_enabled(&view->scene_tree->node, true);
		wlr_scene_node_raise_to_top(&view->scene_tree->node);
		views_above_raise(server);
	}
	b->state = BASE_GOING;
	b->at = monotonic_sec();
	server_schedule_frames(server);
}

/* The process behind a window, or 0. */
pid_t view_pid(const struct desk_toplevel *view)
{
	pid_t pid = 0;
	if (view->xdg_toplevel && view->xdg_toplevel->resource) {
		wl_client_get_credentials(wl_resource_get_client(view->xdg_toplevel->resource),
			&pid, NULL, NULL);
	}
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		pid = view->xsurface->pid;
	}
#endif
	return pid;
}

/* ---- which Steam game a window is -------------------------------------
 *
 * A game run by Steam often calls its window by its executable (hh.x86_64,
 * or Proton's wine) rather than anything its desktop file says, so it is
 * nobody's: no icon on its folded sheet, no name. Steam starts it with the
 * game's number in its environment, and the desktop file Steam makes for
 * it runs steam://rungameid/ that number: the one is found by the other.
 * Only the window's own process is read, only for those names, and no more
 * than 128 KiB of it. (After dentarthurdent's steam-identity, AiO v0.7.) */

/* SteamAppId=1145360 and the like: the number, if s is one of them. */
static bool steam_game_entry(const char *s, char *out, size_t n)
{
	static const char *const KEYS[] = {
		"SteamAppId=", "SteamGameId=", "SteamOverlayGameId=", "STEAM_COMPAT_APP_ID=",
	};
	for (size_t i = 0; i < sizeof KEYS / sizeof *KEYS; i++) {
		size_t k = strlen(KEYS[i]);
		if (strncmp(s, KEYS[i], k) != 0) {
			continue;
		}
		const char *p = s + k;
		size_t d = 0;
		while (d < 20 && isdigit((unsigned char)p[d])) {
			d++;
		}
		/* (0 is Steam itself, not a game) */
		if (d < 1 || d >= n || p[d] || (d == 1 && p[0] == '0')) {
			return false;
		}
		memcpy(out, p, d);
		out[d] = '\0';
		return true;
	}
	return false;
}

static bool steam_game_of(pid_t pid, char *out, size_t n)
{
	if (pid <= 0) {
		return false;
	}
	char path[64];
	snprintf(path, sizeof path, "/proc/%ld/environ", (long)pid);
	FILE *f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	char entry[1024] = { 0 };
	size_t len = 0;
	bool over = false, found = false;
	for (size_t got = 0; got < 131072 && !found; got++) {
		int c = fgetc(f);
		if (c == EOF) {
			break;
		}
		if (c == 0) {
			if (!over) {
				entry[len] = '\0';
				found = steam_game_entry(entry, out, n);
			}
			len = 0;
			over = false;
		} else if (!over && len + 1 < sizeof entry) {
			entry[len++] = (char)c;
		} else {
			over = true;
		}
	}
	fclose(f);
	return found;
}

/* Does a desktop file's Exec run that game? */
static bool steam_exec_runs(const char *exec, const char *game)
{
	static const char *const ROUTES[] = { "steam://rungameid/", "steam://run/", "-applaunch " };
	size_t n = strlen(game);
	for (size_t i = 0; exec && i < sizeof ROUTES / sizeof *ROUTES; i++) {
		for (const char *at = strstr(exec, ROUTES[i]); at; at = strstr(at + 1, ROUTES[i])) {
			const char *p = at + strlen(ROUTES[i]);
			while (*p == ' ' || *p == '\t') {
				p++;
			}
			if (strncmp(p, game, n) == 0 && !isdigit((unsigned char)p[n])) {
				return true;
			}
		}
	}
	return false;
}

/* The program a window is, by the desktop files: a Steam game by the game
 * its process was started for, else by what the window calls itself. */
static const struct desk_app *view_app_entry(const struct desk_app_list *l,
	const struct desk_toplevel *view, const char *app_id, const char *cls, const char *inst)
{
	char game[24];
	if (l && steam_game_of(view_pid(view), game, sizeof game)) {
		for (int i = 0; i < l->n; i++) {
			if (steam_exec_runs(l->apps[i].exec, game)) {
				return &l->apps[i];
			}
		}
	}
	return desk_apps_match(l, app_id, cls, inst);
}

/* The program a window belongs to, by its desktop file id. */
static bool view_app_id(const struct desk_toplevel *view, char *out, size_t n)
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
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = view_app_entry(l, view, app_id, cls, inst);
	if (app) {
		snprintf(out, n, "%s", app->id);
	}
	desk_apps_release(l);
	return app != NULL;
}

/* The session the process behind a window is in, or 0. */
static pid_t view_session(const struct desk_toplevel *view)
{
	pid_t pid = view_pid(view);
	pid_t sid = pid > 0 ? getsid(pid) : -1;
	return sid > 0 ? sid : 0;
}

/* A window being put on the desk for the first time: is there a sheet
 * waiting for it? The one put down for the program started in the session
 * the window's process is in -- whatever the window calls itself: a
 * program run in a terminal (btop) shows the terminal's window, and some
 * name themselves other than their desktop file does --; else its own
 * program's by name; or -- a window that will not say whose it is -- the
 * one sheet taken out lately, if only one is waiting. */
static struct desk_base *base_claim(struct desk_server *server, struct desk_toplevel *view)
{
	char id[96];
	bool known = view_app_id(view, id, sizeof id);
	pid_t sid = view_session(view);
	struct desk_base *best = NULL, *lone = NULL, *ours = NULL;
	int young = 0;
	for (int i = 0; i < DESK_BASES; i++) {
		struct desk_base *b = &server->bases[i];
		if (b->state == BASE_FREE || b->state == BASE_GOING || b->view) {
			continue;
		}
		if (sid > 0 && b->sid == sid) {
			ours = b;
		}
		if (known && strcmp(b->app, id) == 0 && (!best || b->taken_at < best->taken_at)) {
			best = b;
		}
		if (real_sec() - b->taken_at < 25.0) {
			young++;
			lone = b;
		}
	}
	if (ours) {
		best = ours;
	}
	/* only a window that will not say whose it is: one that does, and is
	 * some other program's, is not what anybody put a sheet down for */
	if (!best && !known && young == 1) {
		best = lone;
	}
	if (best) {
		best->view = view;
		wlr_log(WLR_INFO, "a window for %s comes to the sheet put down for it", best->app);
	}
	return best;
}

static struct desk_base *base_of_view(struct desk_server *server,
	const struct desk_toplevel *view)
{
	for (int i = 0; i < DESK_BASES; i++) {
		if (server->bases[i].state != BASE_FREE && server->bases[i].view == view) {
			return &server->bases[i];
		}
	}
	return NULL;
}

/* After it is mapped and placed: held back while its sheet is still on its
 * way down or opening, otherwise the sheet goes from under it now. */
static void base_after_map(struct desk_server *server, struct desk_toplevel *view)
{
	struct desk_base *b = base_of_view(server, view);
	if (!b || !view->scene_tree) {
		return;
	}
	if (b->state == BASE_CARRIED && view->outer_w > 60 && view->outer_h > 60) {
		/* here before the sheet is down: it goes down where the window is,
		 * and opens out to the window's own size */
		b->x = view->scene_tree->node.x;
		b->y = view->scene_tree->node.y;
		if (b->w != view->outer_w || b->h != view->outer_h) {
			b->w = view->outer_w;
			b->h = view->outer_h;
			cairo_surface_destroy(b->blank);
			b->blank = desk_window_base_sheet(b->w, b->h, b->name);
			double k = fmin(1.0, fmin(b->w * 0.92 / b->lw, b->h * 0.92 / b->lh));
			b->lw *= k;
			b->lh *= k;
		}
	}
	if (b->state == BASE_CARRIED || b->state == BASE_OPENING) {
		wlr_scene_node_set_enabled(&view->scene_tree->node, false);
	} else if (b->state == BASE_WAITING) {
		base_hand_over(server, b);
	}
}

/* A window going: the sheet that was waiting for it waits on (a splash
 * screen comes and goes before the window proper). */
static void base_forget_view(struct desk_server *server, const struct desk_toplevel *view)
{
	struct desk_base *b = base_of_view(server, view);
	if (b) {
		b->view = NULL;
	}
}

/* The sheet in the hand, at u seconds since it was taken. */
struct take_pose {
	double cx, cy, w, h, a, height;
};

static double take_ease_out(double t)
{
	t = clampf_d(t, 0.0, 1.0);
	return 1.0 - pow(1.0 - t, 3.0);
}

static double take_smoother(double t)
{
	t = clampf_d(t, 0.0, 1.0);
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

static void take_pose_at(struct desk_server *server, double u, struct take_pose *p)
{
	struct desk_base *b = &server->bases[server->take.base];
	int h = server->desk_h;
	/* hanging on the ring, then held up off it */
	struct take_pose p0 = { server->take.cx0, server->take.cy0, server->take.w0,
		server->take.h0, server->take.a0, 0.10 };
	struct take_pose p1 = { p0.cx, p0.cy - h * 0.035, p0.w * 1.07, p0.h * 1.07,
		p0.a * 0.35, 0.85 };
	/* over where it goes down, still in the hand; and down */
	double ox = server->layer_win ? server->layer_win->node.x : 0.0;
	double oy = server->layer_win ? server->layer_win->node.y : 0.0;
	double lcx = b->x + b->w * 0.5 + ox, lcy = b->y + b->h * 0.5 + oy;
	double tilt = (b->seed & 1u) ? 0.035 : -0.035;
	struct take_pose p2 = { lcx, lcy - h * 0.012, b->lw * 1.06, b->lh * 1.06, tilt, 0.62 };
	struct take_pose p3 = { lcx, lcy, b->lw, b->lh, 0.0, 0.0 };
	double land = server->take.land_at - server->take.start;
	const struct take_pose *a, *c;
	double t;
	double arc = 0.0;
	if (u < TAKE_LIFT) {
		a = &p0;
		c = &p1;
		t = take_ease_out(u / TAKE_LIFT);
	} else if (u < land) {
		a = &p1;
		c = &p2;
		double q = (u - TAKE_LIFT) / fmax(land - TAKE_LIFT, 1e-3);
		t = take_smoother(q);
		/* carried, not slid: it comes up a little on the way */
		arc = -sin(3.14159265358979 * clampf_d(q, 0.0, 1.0)) * h * 0.05;
	} else {
		a = &p2;
		c = &p3;
		double q = clampf_d((u - land) / TAKE_DOWN, 0.0, 1.0);
		/* let down, slowing as it meets the desk */
		t = 1.0 - (1.0 - q) * (1.0 - q);
	}
	p->cx = a->cx + (c->cx - a->cx) * t;
	p->cy = a->cy + (c->cy - a->cy) * t + arc;
	p->w = a->w + (c->w - a->w) * t;
	p->h = a->h + (c->h - a->h) * t;
	p->a = a->a + (c->a - a->a) * t;
	p->height = a->height + (c->height - a->height) * t;
}

struct take_args {
	struct take_pose p;
	cairo_surface_t *face;
	double box[4];
};

static void take_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct take_args *a = data;
	(void)w;
	(void)h;
	desk_set_paint_origin(floor(a->box[0]), floor(a->box[1]));
	desk_draw_taken_sheet(cr, a->face, a->p.cx, a->p.cy, a->p.w, a->p.h, a->p.a,
		a->p.height);
	desk_set_paint_origin(0, 0);
}

/* Once a frame: the sheet in the hand, and the sheets on the desk. */
static void take_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	if (server->take.base >= 0) {
		struct desk_base *b = &server->bases[server->take.base];
		if (!server->take.turned && now >= server->take.turn_at) {
			/* and back round to the desk with it */
			server->take.turned = true;
			if (server->shelf_t > 0.5 && !(server->shelf_moving && server->shelf_to < 0.5)) {
				shelf_toggle(server);
			}
			/* the move is a spring, there in half its time: the sheet is
			 * over its place as the desk is, and goes down onto it */
			server->take.land_at = now + (server->shelf_moving ? server->shelf_dur * 0.5 : 0.0);
			/* down on the desk, and opening out: when it is put down,
			 * known from here */
			double bx = b->x + b->w * 0.5;
			double in = server->take.land_at + TAKE_DOWN - now;
			snd_in(server, snd_sheet_down(server, bx, b->y + b->h * 0.5), bx, 1.0, 0.0, in);
			snd_for_in(server, DESK_SND_PAPER_UNFOLD, bx, 0.9, 0.46, BASE_OPEN_S, in);
			paint_shelf(server);
		}
		double u = now - server->take.start;
		double land = server->take.land_at - server->take.start;
		if (server->take.turned && u >= land + TAKE_DOWN) {
			/* down: it is on the desk now, not in the hand */
			wlr_log(WLR_INFO, "the sheet for %s is put down, %.2f s after it was taken",
				b->app, u);
			if (server->take.sp.node) {
				wlr_scene_node_set_enabled(&server->take.sp.node->node, false);
			}
			server->take.sp.drawn = false;
			server->take.base = -1;
			server->take.idx = -1;
			b->state = BASE_OPENING;
			b->at = now;
			base_paint(server, b, 0.0);
			/* (heard as it goes down: asked for when you turned) */
		} else {
			struct take_args a;
			take_pose_at(server, u, &a.p);
			a.face = b->face;
			double r = 0.5 * hypot(a.p.w, a.p.h) * 1.06;
			double m = 90.0 + 80.0 * a.p.height;
			a.box[0] = a.p.cx - r - m;
			a.box[1] = a.p.cy - r - m;
			a.box[2] = (r + m) * 2.0;
			a.box[3] = (r + m) * 2.0;
			sprite_paint(server, &server->take.sp, server->layer_top, a.box, take_draw, &a);
		}
		server_schedule_frames(server);
	}
	for (int i = 0; i < DESK_BASES; i++) {
		struct desk_base *b = &server->bases[i];
		if (b->state == BASE_OPENING) {
			double u = (now - b->at) / BASE_OPEN_S;
			base_paint(server, b, fmin(u, 1.0));
			if (u >= 1.0) {
				b->state = BASE_WAITING;
				b->at = now;
				if (b->view) {
					base_hand_over(server, b);
				}
			}
			server_schedule_frames(server);
		} else if (b->state == BASE_GOING) {
			double u = (now - b->at) / BASE_GO_S;
			if (u >= 1.0 || !b->node) {
				base_free(b);
			} else {
				wlr_scene_buffer_set_opacity(b->node, (float)(1.0 - u * u));
				server_schedule_frames(server);
			}
		}
	}
}

/* Whether anything is still running in session sid: a process whose
 * session (the sixth field of /proc/<pid>/stat, after the name in
 * brackets) it is. */
static bool session_alive(pid_t sid)
{
	DIR *d = opendir("/proc");
	if (!d) {
		return true;   /* not to be known: waited out instead */
	}
	bool alive = false;
	struct dirent *e;
	while (!alive && (e = readdir(d))) {
		if (e->d_name[0] < '1' || e->d_name[0] > '9') {
			continue;
		}
		char path[300], line[512];
		snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
		FILE *f = fopen(path, "re");
		if (!f) {
			continue;
		}
		if (fgets(line, sizeof line, f)) {
			const char *p = strrchr(line, ')');
			int session = 0;
			if (p && sscanf(p + 1, " %*c %*d %*d %d", &session) == 1 && session == sid) {
				alive = true;
			}
		}
		fclose(f);
	}
	closedir(d);
	return alive;
}

/* Once a second: a program that has not sent its window has its sheet
 * screwed up and thrown away -- after BASE_WAIT_S, or as soon as nothing it
 * started is running any more (it quit, or handed the work to a program
 * already running, and no window is coming). */
static void base_timeouts(struct desk_server *server)
{
	double now = monotonic_sec(), waited_now = real_sec();
	for (int i = 0; i < DESK_BASES; i++) {
		struct desk_base *b = &server->bases[i];
		if (b->state != BASE_WAITING || b->view) {
			continue;
		}
		double waited = waited_now - b->taken_at;
		bool gone = waited >= BASE_GONE_S && b->sid > 0 && !session_alive(b->sid);
		if (waited < BASE_WAIT_S && !gone) {
			continue;
		}
		wlr_log(WLR_INFO, "no window came for %s%s: its sheet is thrown away", b->app,
			gone ? " (nothing of it is running)" : "");
		if (!server->conf.throw_closed) {
			/* not screwed up: it simply goes */
			b->state = BASE_GOING;
			b->at = now;
			server_schedule_frames(server);
			continue;
		}
		cairo_surface_t *snap = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, b->w, b->h);
		cairo_t *cr = cairo_create(snap);
		cairo_set_source_surface(cr, b->blank, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
		cairo_surface_flush(snap);
		double x = b->x, y = b->y;
		base_free(b);
		throw_paper(server, snap, x, y);
	}
}
