/* Part of wm.c: looks for the drawer, focus and the layout of screens.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* desk-look, which chooses a picture from anywhere and makes a look of it: the one beside this desk,
 * as built or installed with it, else the one on the PATH. Is it there? */
static bool looks_picker(char *out, size_t n)
{
	char exe[1024], buf[1024];
	ssize_t k = readlink("/proc/self/exe", exe, sizeof exe - 1);
	exe[k > 0 ? k : 0] = '\0';
	char *slash = strrchr(exe, '/');
	if (slash && (size_t)(slash - exe) + 12 < sizeof exe) {
		strcpy(slash + 1, "desk-look");
		if (access(exe, X_OK) == 0) {
			if (out) {
				snprintf(out, n, "%s", exe);
			}
			return true;
		}
	}
	const char *path = getenv("PATH");
	for (const char *p = path && *path ? path : "/usr/local/bin:/usr/bin:/bin"; *p;) {
		const char *e = strchr(p, ':');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		if (len > 0 && len + 11 < sizeof buf) {
			memcpy(buf, p, len);
			strcpy(buf + len, "/desk-look");
			if (access(buf, X_OK) == 0) {
				if (out) {
					snprintf(out, n, "%s", buf);
				}
				return true;
			}
		}
		p = e ? e + 1 : p + len;
	}
	return false;
}

static void looks_pick_done(struct desk_server *server)
{
	if (server->lk_pick_src) {
		wl_event_source_remove(server->lk_pick_src);
		server->lk_pick_src = NULL;
	}
	if (server->lk_pick_fd >= 0) {
		close(server->lk_pick_fd);
		server->lk_pick_fd = -1;
	}
}

/* What desk-look says as it goes: the look it made, when it has gone, given to the program it was
 * for, and the drawer out to show it. */
static int looks_pick_said(int fd, uint32_t mask, void *data)
{
	struct desk_server *server = data;
	if (mask & WL_EVENT_READABLE) {
		size_t room = sizeof server->lk_pick_said - 1 - server->lk_pick_len;
		ssize_t got = room ? read(fd, server->lk_pick_said + server->lk_pick_len, room) : 0;
		if (got > 0) {
			server->lk_pick_len += (size_t)got;
			return 0;
		}
		if (got < 0 && errno == EINTR) {
			return 0;
		}
	} else if (!(mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR))) {
		return 0;
	}
	server->lk_pick_said[server->lk_pick_len] = '\0';
	server->lk_pick_said[strcspn(server->lk_pick_said, "\n")] = '\0';
	looks_pick_done(server);
	const char *id = server->lk_pick_said;
	if (strncmp(id, "own/", 4) || !desk_look_find(id)) {
		return 0;
	}
	for (int i = 0; i < server->drw_n; i++) {
		char key[128];
		if (drawer_key(&server->drw[i], key, sizeof key) && !strcmp(key, server->lk_pick_key)) {
			drawer_look_set(server, &server->drw[i], id);
			wlr_log(WLR_INFO, "a look chosen for %s: %s", key, id);
			drawer_move_to(server, 1.0);
			drawer_compose(server);
			break;
		}
	}
	return 0;
}

/* The one more on the card: desk-look asked to choose a picture for this program, from anywhere.
 * The drawer goes in for it, and comes out again with the program looking like it. */
static void looks_pick(struct desk_server *server)
{
	char prog[1024];
	if (server->lk_pick_src || server->lk_entry >= server->drw_n || !looks_picker(prog, sizeof prog)
		|| !drawer_key(&server->drw[server->lk_entry], server->lk_pick_key, sizeof server->lk_pick_key)) {
		return;
	}
	const struct desk_app_list *l = desk_apps_acquire();
	struct desk_drawer_item base;
	drawer_item_usual(server, l, &server->drw[server->lk_entry], &base);
	desk_apps_release(l);
	int fds[2];
	if (pipe(fds) != 0) {
		return;
	}
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	const char *lang = desk_lang_code(desk_lang_get());
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		if (fork() == 0) {
			dup2(fds[1], STDOUT_FILENO);
			desk_close_from(3);
			execl(prog, "desk-look", "pick", "--for", base.name, "--lang", lang, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	close(fds[1]);
	if (pid < 0) {
		close(fds[0]);
		return;
	}
	while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
		;
	}
	server->lk_pick_fd = fds[0];
	server->lk_pick_len = 0;
	server->lk_pick_src = wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display), fds[0],
		WL_EVENT_READABLE, looks_pick_said, server);
	looks_close(server);
	drawer_move_to(server, 0.0);
}

static void looks_paint(struct desk_server *server)
{
	if (!server->lk_open || server->lk_entry >= server->drw_n) {
		return;
	}
	/* (each as it lies now: a picture of one's own may have been changed since) */
	const struct desk_app_list *l = desk_apps_acquire();
	const struct drawer_entry *e = &server->drw[server->lk_entry];
	struct desk_drawer_item base;
	drawer_item_usual(server, l, e, &base);
	for (int i = 0; i < server->lk_n; i++) {
		const struct desk_look *look = desk_look_find(server->lk_ids[i]);
		server->lk_items[i] = base;
		if (look) {
			drawer_look_apply(l, e, look, &server->lk_items[i]);
		}
	}
	desk_apps_release(l);
	char dir[400], hint[600];
	desk_looks_own_dir(dir, sizeof dir);
	const char *home = getenv("HOME");
	size_t hl = home ? strlen(home) : 0;
	if (hl && !strncmp(dir, home, hl) && dir[hl] == '/') {
		/* (where it is, as a person would say it) */
		memmove(dir + 1, dir + hl, strlen(dir + hl) + 1);
		dir[0] = '~';
	}
	snprintf(hint, sizeof hint, "%s  %s", TR("Your own: PNG pictures, seen from above, in"), dir);
	const char *names[DRAWER_LOOKS_ON_CARD];
	for (int i = 0; i < server->lk_n; i++) {
		names[i] = server->lk_names[i];
	}
	struct desk_buf *buf = buf_create(server->lk_w, server->lk_h);
	desk_looks_card_paint(buf->cr, server->lk_items, names, server->lk_n,
		server->lk_more ? TR("Choose a picture…") : NULL, server->lk_cols, server->lk_pt, base.name, hint,
		server->lk_chosen, server->lk_hover);
	scene_put(&server->lk_node, server->layer_top, buf, server->lk_x, server->lk_y, NULL);
	wlr_scene_node_raise_to_top(&server->lk_node->node);
	server_schedule_frames(server);
}

static void looks_close(struct desk_server *server)
{
	if (!server->lk_open) {
		return;
	}
	server->lk_open = false;
	server->lk_going = true;
	server->lk_at = monotonic_sec();
	server->lk_press = -1;
	drawer_hover_to(server, -1);
	server_schedule_frames(server);
}

/* Up over entry i, if it is a program's. */
static void looks_open(struct desk_server *server, int i)
{
	if (i < 0 || i >= server->drw_n || (server->drw[i].kind != DRAWER_APP && server->drw[i].kind != DRAWER_STD)) {
		return;
	}
	const struct drawer_entry *e = &server->drw[i];
	const struct desk_look *all;
	int n = desk_looks_all(&all);
	n = n < DRAWER_LOOKS_ON_CARD ? n : DRAWER_LOOKS_ON_CARD;
	const struct desk_app_list *l = desk_apps_acquire();
	struct desk_drawer_item base;
	drawer_item_usual(server, l, e, &base);
	const char *now = drawer_look_of(server, e);
	if (!now || !desk_look_find(now)) {
		now = drawer_look_usual(&base);
	}
	server->lk_chosen = -1;
	for (int k = 0; k < n; k++) {
		snprintf(server->lk_ids[k], sizeof server->lk_ids[k], "%s", all[k].id);
		snprintf(server->lk_names[k], sizeof server->lk_names[k], "%s", all[k].name);
		if (!strcmp(all[k].id, now)) {
			server->lk_chosen = k;
		}
	}
	desk_apps_release(l);
	server->lk_n = n;
	server->lk_entry = i;
	server->lk_hover = -1;
	server->lk_press = -1;
	server->lk_pt = desk_px_per_pt(server->desk_w);
	server->lk_more = looks_picker(NULL, 0);
	int cells = n + (server->lk_more ? 1 : 0);
	/* over it, as near as the screen has room for: as many across as keep it above the drawer */
	double sx = server->desk_w * 0.5, sy = server->desk_h * 0.75;
	desk_drawer_point(server->desk_w, server->desk_h, server->drawer_t, e->x, &sx, &sy);
	double pt = server->lk_pt;
	int rows = (int)floor(((sy - 66.0 * pt) / pt - 94.0) / 100.0);
	rows = rows < 1 ? 1 : rows;
	int most = (int)floor((server->desk_w / pt - 56.0) / 108.0);
	server->lk_cols = (cells + rows - 1) / rows;
	server->lk_cols = server->lk_cols < 5 ? 5 : server->lk_cols;
	server->lk_cols = server->lk_cols > most && most >= 5 ? most : server->lk_cols;
	desk_looks_card_size(cells, server->lk_cols, pt, &server->lk_w, &server->lk_h);
	int x = (int)lround(sx - server->lk_w * 0.5);
	int y = (int)lround(sy - server->lk_h - 58.0 * server->lk_pt);
	int edge = (int)lround(8.0 * server->lk_pt);
	x = x > server->desk_w - server->lk_w - edge ? server->desk_w - server->lk_w - edge : x;
	x = x < edge ? edge : x;
	y = y < edge ? edge : y;
	server->lk_x = x;
	server->lk_y = y;
	wlr_log(WLR_INFO, "the card of looks, for %s: %d of them%s, %d across, at %d,%d, %dx%d", base.name, n,
		server->lk_more ? " and a picture to choose" : "", server->lk_cols, x, y, server->lk_w, server->lk_h);
	server->lk_open = true;
	server->lk_going = false;
	server->lk_at = monotonic_sec();
	drawer_hover_to(server, i);
	desk_sound_play(DESK_SND_CARD_SLIDE, (sx / fmax(server->desk_w, 1) - 0.5) * 1.6, 0.6, 0.4);
	looks_paint(server);
}

/* Coming up out of the drawer, or going back down. */
static void looks_tick(struct desk_server *server)
{
	if (!server->lk_node || (!server->lk_open && !server->lk_going)) {
		return;
	}
	if (server->lk_open && (server->drawer_to < 0.5 || server->lk_entry >= server->drw_n)) {
		/* the drawer going in: the card with it */
		looks_close(server);
	}
	double t = (monotonic_sec() - server->lk_at) / (server->lk_open ? LOOKS_UP_S : LOOKS_DOWN_S);
	t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
	double shown = server->lk_open ? 1.0 - (1.0 - t) * (1.0 - t) : 1.0 - t;
	if (!server->lk_open && t >= 1.0) {
		server->lk_going = false;
		wlr_scene_node_set_enabled(&server->lk_node->node, false);
		return;
	}
	wlr_scene_node_set_enabled(&server->lk_node->node, true);
	wlr_scene_buffer_set_opacity(server->lk_node, (float)shown);
	wlr_scene_node_set_position(&server->lk_node->node, server->lk_x,
		server->lk_y + (int)lround((1.0 - shown) * LOOKS_RISE_PT * server->lk_pt));
	if (t < 1.0) {
		server_schedule_frames(server);
	}
}

static int looks_at(struct desk_server *server)
{
	double x = server->cursor->x - server->lk_x, y = server->cursor->y - server->lk_y;
	if (x < 0 || y < 0 || x >= server->lk_w || y >= server->lk_h) {
		return -2;
	}
	return desk_looks_card_hit(server->lk_n + (server->lk_more ? 1 : 0), server->lk_cols, server->lk_pt, x, y);
}

/* The hand over it: true while it is up. */
static bool looks_motion(struct desk_server *server)
{
	if (!server->lk_open) {
		return false;
	}
	int at = looks_at(server);
	at = at < 0 ? -1 : at;
	if (at != server->lk_hover) {
		server->lk_hover = at;
		looks_paint(server);
	}
	return true;
}

/* A hand put down, or let go, while it is up: on one of them, it looks like that; anywhere
 * else, the card goes. True if it was for the card. */
static bool looks_button(struct desk_server *server, uint32_t button, bool down)
{
	if (down) {
		if (!server->lk_open) {
			return false;
		}
		server->lk_button = button;
		int at = looks_at(server);
		server->lk_press = button == BTN_LEFT ? at : -1;
		if (at == -2) {
			looks_close(server);
		}
		return true;
	}
	if (!server->lk_button || button != server->lk_button) {
		return false;
	}
	server->lk_button = 0;
	int at = server->lk_open ? looks_at(server) : -1;
	if (at >= 0 && at == server->lk_press && at == server->lk_n && server->lk_more) {
		looks_pick(server);
		server->lk_press = -1;
		return true;
	}
	if (at >= 0 && at == server->lk_press && server->lk_entry < server->drw_n) {
		struct drawer_entry *e = &server->drw[server->lk_entry];
		const struct desk_app_list *l = desk_apps_acquire();
		struct desk_drawer_item base;
		drawer_item_usual(server, l, e, &base);
		desk_apps_release(l);
		const char *id = server->lk_ids[at];
		drawer_look_set(server, e, strcmp(id, drawer_look_usual(&base)) ? id : NULL);
		server->lk_chosen = at;
		looks_paint(server);
		double sx = 0.0, sy = 0.0;
		desk_drawer_point(server->desk_w, server->desk_h, server->drawer_t, e->x, &sx, &sy);
		desk_sound_play(DESK_SND_FELT_DROP, (sx / fmax(server->desk_w, 1) - 0.5) * 1.6, 0.8, 0.0);
		looks_close(server);
		drawer_compose(server);
	}
	server->lk_press = -1;
	return true;
}

static bool looks_keys(struct desk_server *server, xkb_keysym_t sym)
{
	if (!server->lk_open || sym != XKB_KEY_Escape) {
		return false;
	}
	looks_close(server);
	return true;
}

/* Once a frame: along the row to where they go, and a program taken out
 * fading from the hand. */
static void drawer_slide_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	if (server->drw_poof_at > 0.0 && server->drw_carry_node) {
		double u = (now - server->drw_poof_at) / 0.22;
		if (u >= 1.0) {
			server->drw_poof_at = 0.0;
			wlr_scene_node_set_enabled(&server->drw_carry_node->node, false);
		} else {
			wlr_scene_buffer_set_opacity(server->drw_carry_node, (float)(0.55 * (1.0 - u)));
			server_schedule_frames(server);
		}
	}
	if (!server->drw_sliding) {
		return;
	}
	double dt = clampf_d(now - server->drw_slid_at, 0.0, 0.1);
	server->drw_slid_at = now;
	double k = gfx_flat() ? 1.0 : 1.0 - exp(-dt / 0.055);
	bool more = false;
	for (int i = 0; i < server->drw_n; i++) {
		struct drawer_entry *e = &server->drw[i];
		e->x += (e->x_to - e->x) * k;
		e->fit += (e->fit_to - e->fit) * k;
		if (fabs(e->x_to - e->x) > 0.003 || fabs(e->fit_to - e->fit) > 0.002) {
			more = true;
		} else {
			e->x = e->x_to;
			e->fit = e->fit_to;
		}
	}
	server->drw_sliding = more;
	server->drw_dirty = true;
	drawer_compose(server);
	if (more) {
		server_schedule_frames(server);
	}
}

/* Which of our windows a surface is the client of, if any. */
static struct desk_toplevel *view_of_surface(struct desk_server *server,
	struct wlr_surface *surface)
{
	if (!surface) {
		return NULL;
	}
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view_surface(view) == surface) {
			return view;
		}
	}
	return NULL;
}

static void reset_cursor_mode(struct desk_server *server)
{
	if (server->cursor_mode == DESK_CUR_MOVE) {
		slide_end(server);
	}
	server->cursor_mode = DESK_CUR_PASSTHROUGH;
	server->grabbed = NULL;
}

/* A video taken out of its page to float over everything -- Firefox's
 * and Chromium's picture-in-picture -- is known by its window's title
 * alone, as the browsers name it in DEsk's languages (compared without
 * case, spaces or hyphens): Wayland has no "keep this in front". */
static const char *const PIP_TITLES[] = {
	"pictureinpicture",          /* Firefox, Chromium */
	"ピクチャーインピクチャー",
	"画中画", "子母畫面",
	"pip모드", "화면속화면",
	"imagenenimagen", "imagendentrodeimagen", "imagensuperpuesta",
	"incrustationvidéo", "imagedansl’image", "imagedansl'image",
	"bildimbild",
};

bool view_is_pip(const struct desk_toplevel *view)
{
	const char *t = view_title(view);
	char norm[96];
	size_t k = 0;
	for (const char *p = t; p && *p && k + 1 < sizeof norm; p++) {
		if (*p != ' ' && *p != '-') {
			norm[k++] = (char)tolower((unsigned char)*p);
		}
	}
	norm[k] = '\0';
	for (size_t i = 0; i < sizeof PIP_TITLES / sizeof PIP_TITLES[0]; i++) {
		if (strcmp(norm, PIP_TITLES[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* What is kept in front goes back in front, after anything is put over
 * the windows -- the last of them to have been used on top. */
void views_above_raise(struct desk_server *server)
{
	struct desk_toplevel *v;
	wl_list_for_each_reverse(v, &server->toplevels, link) {
		if (v->above && v->scene_tree && v->scene_tree->node.parent == server->layer_win) {
			wlr_scene_node_raise_to_top(&v->scene_tree->node);
		}
	}
}

/* Is it (still) one to keep in front? Asked when it comes, and whenever
 * its title changes. */
static void view_check_above(struct desk_toplevel *view)
{
	bool above = view_is_pip(view);
	const struct desk_thing *was = view->thing_want;
	if (!was || was->by_title) {
		const struct desk_thing *want = thing_claiming(view, true);
		if (want != was) {
			view->thing_want = want;
			if (was && was->forget) {
				was->forget(view);
			}
			view_frame_update(view);
		}
	}
	if (above != view->above) {
		view->above = above;
		views_above_raise(view->server);
	}
	const struct desk_thing *thing = view->thing_want;
	if (thing && thing->put_down && !view->thing_placed && view->scene_tree
		&& view->scene_tree->node.enabled && view->outer_w > 0) {
		view->thing_placed = true;
		thing->put_down(view);
	}
}

void focus_toplevel(struct desk_toplevel *toplevel)
{
	if (toplevel == NULL) {
		return;
	}
	struct desk_server *server = toplevel->server;
	if (server->lock_phase != 0) {
		return;   /* nothing gets the keys while the door is shut */
	}
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = view_surface(toplevel);
	if (!surface || !toplevel->scene_tree) {
		return;
	}
	/* to the front first: a click on the one that has the keys already
	 * still brings it up over what was put down on it */
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	views_above_raise(server);
	if (!view_takes_keys(toplevel) || prev == surface) {
		/* raised; the keys stay where they were, or are here already */
		return;
	}
	struct desk_toplevel *was = view_of_surface(server, prev);
	if (was) {
		view_set_activated(was, false);
	}
	view_set_activated(toplevel, true);
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->keys_gone) {
			(*t)->keys_gone(server);
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
			keyboard->num_keycodes, &keyboard->modifiers);
	} else {
		wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
	}
	/* the title bar says which one has the keys */
	if (was) {
		view_repaint_chrome(was);
	}
	view_repaint_chrome(toplevel);
}

/* The window on top that can have the keys, when the one that had them
 * goes: or nobody, if nothing is left. A sheet folded away is not it. */
void focus_next(struct desk_server *server)
{
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->sheet.phase == SHEET_IDLE && view->scene_tree && view_takes_keys(view)) {
			focus_toplevel(view);
			return;
		}
	}
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

/* The frame's buffer reaches this far past the paper: only far enough
 * for a hand reaching for the edge. (Its shadow is a node of its own.) */
#define CHROME_PAD 8

/* (a thing can go further past its edge than a sheet's does) */
static int view_chrome_pad(const struct desk_toplevel *view)
{
	const struct desk_thing *thing = view_thing(view);
	return thing ? thing->pad : CHROME_PAD;
}

static bool chrome_accepts(struct wlr_scene_buffer *buffer, int sx, int sy)
{
	struct desk_toplevel *view = buffer->node.data;
	if (!view) {
		return false;
	}
	int pad = view_chrome_pad(view);
	double lx = sx - pad;
	double ly = sy - pad;
	const struct desk_thing *thing = view_thing(view);
	if (thing && thing->solid) {
		return thing->solid(view, lx, ly);
	}
	if (thing) {
		/* all of it but the window, and a little past its edge */
		if (lx < -6 || ly < -6 || lx > view->outer_w + 6 || ly > view->outer_h + 6) {
			return false;
		}
		return !(lx >= thing->inset_l && lx <= view->outer_w - thing->inset_r
			&& ly >= thing->inset_t && ly <= view->outer_h - thing->inset_b);
	}
	/* a little past the edge of the paper, where a hand reaches for it */
	if (lx < -6 || ly < -6 || lx > view->outer_w + 6 || ly > view->outer_h + 6) {
		return false;
	}
	if (lx >= DESK_BORDER && lx <= view->outer_w - DESK_BORDER &&
		ly >= DESK_TITLE_H && ly <= view->outer_h - DESK_BORDER) {
		return false;
	}
	return true;
}

static bool chrome_accepts_f(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	return chrome_accepts(buffer, (int)*sx, (int)*sy);
}

static void view_paint_chrome_body(struct desk_toplevel *view, int inner_w, int inner_h)
{
	if (!view->scene_tree || !view->surface_tree) {
		return;
	}
	bool framed = view_framed(view);
	if (framed) {
		if (inner_w < 80) {
			inner_w = 80;
		}
		if (inner_h < 60) {
			inner_h = 60;
		}
	}
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	const char *title = view_title(view);
	struct wlr_surface *surface = view_surface(view);
	bool focused = view->server->seat->keyboard_state.focused_surface == surface;
	/* Where the window proper starts in the client's surface: a client
	 * that draws its own shadow draws it in a margin round the window,
	 * and the sheet is the window, not the margin. */
	struct wlr_box geo;
	view_geometry(view, &geo);
	if (geo.width <= 0 || geo.height <= 0) {
		geo = (struct wlr_box){ 0, 0, inner_w, inner_h };
	}
	view->own_shadow = view->xdg_toplevel && surface
		&& (geo.x > 0 || geo.y > 0 || surface->current.width > geo.width
			|| surface->current.height > geo.height);

	/* Drawn again only if what it shows has changed. A client commits a
	 * frame for every keystroke and every frame of a video, and the frame
	 * round it is the same every time. */
	/* gliding onto a half or off it: drawn smaller and stretched, as what
	 * moves is, and whole again where it comes to rest */
	double k = view->sz_glide ? motion_res() : 1.0;
	uint64_t key = 14695981039346656037ull;
	int what[11] = { inner_w, inner_h, focused, framed, l, geo.x, geo.y, view->own_shadow,
		view->own_frame, view->thing && view->thing->look ? view->thing->look(view) : 0,
		(int)(k * 1000.0) };
	key = key_add(key, what, sizeof what);
	key = key_add(key, &view->thing, sizeof view->thing);
	key = key_add(key, title, strlen(title));
	if (view->chrome_key == key && (view->chrome || !framed)) {
		return;
	}
	view->chrome_key = key;
	view->outer_w = inner_w + l + r;
	view->outer_h = inner_h + t + b;
	/* The window proper at (l, t). The scene's node for an xdg surface
	 * already has its origin at the window geometry's corner, so the
	 * client's margin is not taken off again here: it was, and a program
	 * that draws its own shadow sat that far up and to the left of where
	 * its sheet was. (An X window has no margin.) */
	wlr_scene_node_set_position(&view->surface_tree->node, l, t);
	if (!view->sizing) {
		view_clip_rest(view, &geo);
	}
	if (!framed) {
		if (view->chrome) {
			wlr_scene_node_set_enabled(&view->chrome->node, false);
		}
		if (view->thing && view->thing->hidden) {
			view->thing->hidden(view);
		}
		view_sync_shadow(view);
		return;
	}
	int pad = view_chrome_pad(view);
	int bw = view->outer_w + pad * 2;
	int bh = view->outer_h + pad * 2;
	int pw = (int)ceil(bw * k), ph = (int)ceil(bh * k);
	struct desk_buf *buffer = buf_create(pw, ph);
	if (pw != bw || ph != bh) {
		cairo_scale(buffer->cr, (double)pw / bw, (double)ph / bh);
	}
	const struct desk_thing *thing = view_thing(view);
	if (view->sz_glide) {
		/* its paper's age as it will be where it is going, stretched */
		desk_paper_age_hold(view->sz_to.width + l + r, view->sz_to.height + t + b);
	}
	if (thing) {
		thing->paint(buffer->cr, view, pad, pad, view->outer_w, view->outer_h, title, focused, false);
	} else if (view->own_frame) {
		desk_draw_window_mount(buffer->cr, pad, pad, view->outer_w, view->outer_h, title);
	} else {
		desk_draw_window_frame(buffer->cr, pad, pad,
			view->outer_w, view->outer_h, title, focused, 0, false);
	}
	desk_paper_age_hold(0, 0);
	scene_put(&view->chrome, view->scene_tree, buffer, -pad, -pad, view);
	wlr_scene_buffer_set_dest_size(view->chrome, bw, bh);
	scene_hold(view->chrome);   /* read back under a corner (curl_underlay) */
	/* The frame is a whole sheet of paper, the client lies on it: under
	 * the client, then, not over it -- it is made at the first commit that
	 * has a size, which is long after the client's own node, and a new
	 * node goes on top. */
	wlr_scene_node_place_below(&view->chrome->node, &view->surface_tree->node);
	wlr_scene_node_set_enabled(&view->chrome->node, true);
	view->chrome->point_accepts_input = chrome_accepts_f;
	view_sync_shadow(view);
	if (view->thing && view->thing->painted) {
		view->thing->painted(view);
	}
}

static void view_paint_chrome(struct desk_toplevel *view, int inner_w, int inner_h)
{
	prof_begin(PROF_CHROME);
	view_paint_chrome_body(view, inner_w, inner_h);
	prof_end();
}

/* The frame again at the size the client has now: after a change of
 * focus, of title, or of whether there is to be a frame at all. */
static void view_sizing_place(struct desk_toplevel *view);

static void view_repaint_chrome(struct desk_toplevel *view)
{
	/* opening out or folding back: the frame and the program's drawing
	 * are away until the end, and view_unfold_finish paints the frame
	 * again once ua_on is clear */
	if (view->ua_on) {
		return;
	}
	struct wlr_surface *s = view_surface(view);
	if (!s || !view->scene_tree) {
		return;
	}
	/* the window proper: for a Wayland client its geometry, which leaves
	 * out any margin it draws a shadow in */
	int iw = s->current.width, ih = s->current.height;
	if (view->xdg_toplevel) {
		struct wlr_box geo;
		view_geometry(view, &geo);
		if (geo.width > 0 && geo.height > 0) {
			iw = geo.width;
			ih = geo.height;
		}
	}
	if (view->sizing) {
		/* the paper, not the program: it is where the pointer took it */
		iw = view->sz_x1 - view->sz_x0;
		ih = view->sz_y1 - view->sz_y0;
	}
	if (iw > 0 && ih > 0) {
		view_paint_chrome(view, iw, ih);
	}
	if (view->sizing) {
		view_sizing_place(view);
	}
}

/* The screen the desk is on: the one at the corner the desk is drawn from
 * -- the first there was, or the one a display tool put there -- else the
 * newest that is on. One turned off from a display tool is not where
 * anything is. */
struct wlr_output *first_output(struct desk_server *server)
{
	if (wl_list_empty(&server->outputs)) {
		return NULL;
	}
	struct wlr_output *at = wlr_output_layout_output_at(server->output_layout, 0.0, 0.0);
	struct desk_output *o;
	if (at) {
		/* (one showing the desk's, laid on it, is not the desk's: the one under it is) */
		bool mirror = false;
		wl_list_for_each(o, &server->outputs, link) {
			mirror = mirror || (o->wlr_output == at && o->mirror);
		}
		if (!mirror) {
			return at;
		}
		wl_list_for_each(o, &server->outputs, link) {
			struct wlr_box b;
			wlr_output_layout_get_box(server->output_layout, o->wlr_output, &b);
			if (!o->off && !o->mirror && !wlr_box_empty(&b)
				&& wlr_box_contains_point(&b, 0.0, 0.0)) {
				return o->wlr_output;
			}
		}
	}
	wl_list_for_each(o, &server->outputs, link) {
		if (!o->off && !o->mirror) {
			return o->wlr_output;
		}
	}
	o = wl_container_of(server->outputs.next, o, link);
	return o->wlr_output;
}

/* Where windows may lie: all of the screen but the tray along the back of
 * the desk -- console, clock, post box, the shelf's plate -- which
 * windows keep off the way they keep off any panel, and the shut drawer's
 * pull along the front. A window with the whole screen is the exception. */
struct wlr_box work_area(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	struct wlr_output *out = first_output(server);
	if ((w < 8 || h < 8) && out) {
		w = out->width;
		h = out->height;
	}
	int top = desk_tray_bottom(w, h);
	int bottom = desk_drawer_pull_h(w, h);
	/* a screen too short for that still gets somewhere to put a window */
	if (h - top - bottom < 160) {
		top = h / 5;
		bottom = 0;
	}
	return (struct wlr_box){ 0, top, w, h - top - bottom };
}

/* ---- more than one screen -------------------------------------------
 *
 * The desk is on one screen: the one at the corner the desk is drawn from
 * (first_output). Any other screen is more desk beside it -- the same
 * boards, with nothing standing on them and no drawer (side_desks_paint)
 * -- for windows to be laid on. There, where a window may lie is all of
 * that screen; one opened out is as big as that screen, and one given the
 * whole screen has that one; a new window comes onto the screen the
 * pointer is on. Nothing the desk's own screen turns to -- the drawer, the
 * bookcase, the Control Panel, the post box -- moves what lies on the
 * others, or takes them out of reach; and a screen that goes puts what was
 * on it on one that is there. */

/* Is out a screen beside the desk's, rather than the desk's own? */
static bool screen_is_side(struct desk_server *server, const struct wlr_output *out)
{
	if (!out || out == first_output(server)) {
		return false;
	}
	/* (one showing the desk's own is the desk again, not more desk) */
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output == out) {
			return !o->mirror;
		}
	}
	return true;
}

/* The screen a point is on, or the nearest one to it. */
static struct wlr_output *screen_at(struct desk_server *server, double x, double y)
{
	struct wlr_output *out = wlr_output_layout_output_at(server->output_layout, x, y);
	if (!out) {
		double cx, cy;
		wlr_output_layout_closest_point(server->output_layout, NULL, x, y, &cx, &cy);
		out = wlr_output_layout_output_at(server->output_layout, cx, cy);
	}
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output == out && o->mirror) {
			return first_output(server);
		}
	}
	return out ? out : first_output(server);
}

/* Is there a screen beside the desk's? */
static bool screens_beside(struct desk_server *server)
{
	struct wlr_output *desk = first_output(server);
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output != desk && !o->off && !o->mirror
			&& wlr_output_layout_get(server->output_layout, o->wlr_output)) {
			return true;
		}
	}
	return false;
}

/* Something of the desk's own screen drawn bigger than that screen -- the
 * room, the drawer, a camera move's film, the bookcase stepped up to --
 * laid over x, y, dw by dh in its layer (which is moved by lx, ly), src
 * being all of its picture: cut, when there is a screen beside the desk's,
 * to what of it is on the desk's screen, so that what is beside it is not
 * drawn over. */
static void desk_screen_lay(struct desk_server *server, struct wlr_scene_buffer *b,
	struct wlr_fbox src, double x, double y, double dw, double dh, int lx, int ly)
{
	if (screens_beside(server) && dw >= 1.0 && dh >= 1.0) {
		double sx0 = -lx, sy0 = -ly;
		double sx1 = sx0 + server->desk_w, sy1 = sy0 + server->desk_h;
		double x0 = fmax(x, sx0), y0 = fmax(y, sy0);
		double x1 = fmin(x + dw, sx1), y1 = fmin(y + dh, sy1);
		if (x1 - x0 >= 1.0 && y1 - y0 >= 1.0 && (x0 > x || y0 > y || x1 < x + dw
				|| y1 < y + dh)) {
			double kx = src.width / dw, ky = src.height / dh;
			struct wlr_fbox cut = { src.x + (x0 - x) * kx, src.y + (y0 - y) * ky,
				(x1 - x0) * kx, (y1 - y0) * ky };
			scene_source(b, &cut);
			wlr_scene_buffer_set_dest_size(b, (int)lround(x1 - x0), (int)lround(y1 - y0));
			wlr_scene_node_set_position(&b->node, (int)lround(x0), (int)lround(y0));
			return;
		}
	}
	scene_source(b, &src);
	wlr_scene_buffer_set_dest_size(b, (int)lround(dw), (int)lround(dh));
	wlr_scene_node_set_position(&b->node, (int)lround(x), (int)lround(y));
}

/* Is the pointer on a screen beside the desk's? */
static bool pointer_on_side(struct desk_server *server)
{
	struct wlr_output *at = wlr_output_layout_output_at(server->output_layout,
		server->cursor->x, server->cursor->y);
	return screen_is_side(server, at);
}

/* Where windows may lie on a screen, in the paper's coordinates (where a
 * window's node is put, in layer_win): on the desk's, the work area, which
 * goes wherever the desk goes; on one beside it, all of it, wherever the
 * drawer and the camera have moved the paper to (desk_follow). */
struct wlr_box screen_area(struct desk_server *server, struct wlr_output *out)
{
	struct wlr_box box = { 0 };
	if (screen_is_side(server, out)) {
		wlr_output_layout_get_box(server->output_layout, out, &box);
		box.x -= server->desk_dx;
		box.y -= server->desk_dy + server->win_dy;
	}
	return wlr_box_empty(&box) ? work_area(server) : box;
}

/* How far the drawer and the camera have moved the paper on the desk off
 * its place (desk_follow): nothing to reach for until it is back. */
static bool paper_moved(const struct desk_server *server)
{
	return server->desk_dx != 0 || server->desk_dy != 0 || server->win_dy != 0;
}

/* The bare desk on each screen beside the desk's own (layer_side), cut
 * from the desk's room (desk_draw_side_desk): drawn for where that screen
 * is, how big, the level and the room -- again only when one of those has
 * changed, or whatever (force) -- as finely as the desk's own overlay is
 * drawn at the level, and as light as the lamp is up. A screen that is
 * not beside the desk any more loses its. */
static void side_desks_paint(struct desk_server *server, bool force)
{
	if (server->quitting) {
		return;
	}
	struct wlr_output *desk = first_output(server);
	int dw = server->desk_w, dh = server->desk_h;
	unsigned room = dw >= 8 && dh >= 8 ? desk_room_shown(dw, dh) : 0;
	int level = (int)desk_gfx_level();
	bool drew = false;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		struct wlr_box box = { 0 };
		if (o->wlr_output != desk && !o->off && !o->mirror && o->wlr_output->enabled) {
			wlr_output_layout_get_box(server->output_layout, o->wlr_output, &box);
		}
		if (wlr_box_empty(&box) || dw < 8 || dh < 8) {
			if (o->side) {
				wlr_scene_node_destroy(&o->side->node);
				o->side = NULL;
				drew = true;
			}
			continue;
		}
		if (!force && o->side && o->side_room == room && o->side_level == level
			&& wlr_box_equal(&box, &o->side_box)) {
			continue;
		}
		int ow, oh;
		desk_ambient_size(dw, dh, &ow, &oh);
		double k = clampf_d((double)ow / dw, 0.25, 1.0);
		int bw = (int)ceil(box.width * k), bh = (int)ceil(box.height * k);
		struct desk_buf *buf = buf_create(bw, bh);
		cairo_scale(buf->cr, (double)bw / box.width, (double)bh / box.height);
		desk_draw_side_desk(buf->cr, box.x, box.y, box.width, box.height, dw, dh);
		scene_put(&o->side, server->layer_side, buf, box.x, box.y, NULL);
		if (o->side) {
			wlr_scene_buffer_set_dest_size(o->side, box.width, box.height);
			wlr_scene_buffer_set_opacity(o->side, (float)server->side_light);
		}
		o->side_box = box;
		o->side_room = room;
		o->side_level = level;
		drew = true;
	}
	if (drew) {
		server_schedule_frames(server);
	}
}

/* The lamp, as far up as it is (L, 0..1), on the bare desk beside the
 * desk's screen: dark at a login, lit as the desk is. */
static void side_desks_light(struct desk_server *server, double L)
{
	L = clampf_d(L, 0.0, 1.0);
	if (fabs(L - server->side_light) < 1e-4) {
		return;
	}
	server->side_light = L;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->side) {
			wlr_scene_buffer_set_opacity(o->side, (float)L);
		}
	}
}

struct wlr_output *view_screen(struct desk_toplevel *view);
static void view_middle(struct desk_toplevel *view, double *x, double *y);
static struct wlr_box max_area(struct desk_server *server, struct wlr_output *out);
static void view_set_fullscreen(struct desk_toplevel *view, bool on);

/* A window whose middle is on no screen now -- its screen taken away, or
 * turned off -- is put on the nearest one that is there, all of it on it
 * where it fits; one that had all of its screen, or was opened out to it,
 * has the new one's. */
static void windows_onto_screens(struct desk_server *server)
{
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (!view->scene_tree || view->unmanaged) {
			continue;
		}
		struct wlr_scene_node *node = &view->scene_tree->node;
		double mx, my;
		view_middle(view, &mx, &my);
		if (wlr_output_layout_output_at(server->output_layout, mx, my)) {
			continue;
		}
		struct wlr_output *out = screen_at(server, mx, my);
		if (!out) {
			continue;
		}
		wlr_log(WLR_INFO, "a window off every screen comes onto %s: %s", out->name,
			view_title(view));
		if (view->fullscreen) {
			/* off the screen it had, and as it was before it took it */
			view_set_fullscreen(view, false);
			view_middle(view, &mx, &my);
			if (wlr_output_layout_output_at(server->output_layout, mx, my)) {
				continue;
			}
		}
		/* the nearest place on that screen, all of it on it where it fits
		 * -- in the paper's coordinates, which the area is in */
		double cx, cy;
		wlr_output_layout_closest_point(server->output_layout, out, mx, my, &cx, &cy);
		cx -= server->desk_dx;
		cy -= server->desk_dy + server->win_dy;
		struct wlr_box a = screen_area(server, out);
		int l, t, r, b;
		view_insets(view, &l, &t, &r, &b);
		/* one opened out is put back, later, at its own size */
		int w = view->maximized ? view->saved_inner_w + l + r : view->outer_w;
		int h = view->maximized ? view->saved_inner_h + t + b : view->outer_h;
		int x = (int)(cx - w * 0.5), y = (int)(cy - h * 0.5);
		x = x + w > a.x + a.width ? a.x + a.width - w : x;
		y = y + h > a.y + a.height ? a.y + a.height - h : y;
		x = x < a.x ? a.x : x;
		y = y < a.y ? a.y : y;
		if (view->maximized) {
			/* opened out to the screen it is on now, and to be put back
			 * there, on that screen too */
			struct wlr_box ma = max_area(server, out);
			view->saved_x = x;
			view->saved_y = y;
			wlr_scene_node_set_position(node, ma.x, ma.y);
			view_set_size(view, ma.width - l - r, ma.height - t - b);
		} else {
			wlr_scene_node_set_position(node, x, y);
		}
		/* folded, it opens out to where the window is now */
		if (view->sheet.phase != SHEET_IDLE) {
			view->sheet.x0 = node->x;
			view->sheet.y0 = node->y;
		}
		view_sync_shadow(view);
		view_send_position(view);
	}
	/* A sheet folded on a screen that has gone: along the front of the
	 * nearest one that is there. */
	wl_list_for_each(view, &server->toplevels, link) {
		struct desk_sheet *s = &view->sheet;
		if ((s->phase != SHEET_PARKED && s->phase != SHEET_FOLDING) || s->in_drawer
			|| s->to_drawer || !s->fold
			|| (s->tree && s->tree->node.parent == server->layer_drag)) {
			continue;
		}
		int ow, oh, pad;
		desk_fold_extent(s->fold, 1.0, &ow, &oh, &pad);
		double mx = s->x1 + ow * 0.5 + server->desk_dx;
		double my = s->y1 + oh * 0.5 + server->desk_dy + server->win_dy;
		if (wlr_output_layout_output_at(server->output_layout, mx, my)) {
			continue;
		}
		struct wlr_output *out = screen_at(server, mx, my);
		if (!out) {
			continue;
		}
		s->slot = sheet_alloc_slot(server, out, s);
		sheet_slot_xy(server, out, s->slot, ow, oh, &s->x1, &s->y1);
		if (s->phase == SHEET_PARKED) {
			sheet_present(server, s, 1.0, s->x1, s->y1);
		}
	}
}
