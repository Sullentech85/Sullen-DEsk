/* Part of wm.c: the desk calendar.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static bool cal_up(const struct desk_server *server)
{
	return server->cal_t > 0.0005 || server->cal_moving;
}

/* close up and still: what the page is for */
static bool cal_settled(const struct desk_server *server)
{
	return server->cal_to > 0.5 && !server->cal_moving;
}

struct cal_args {
	struct desk_cal_look look;
	time_t now;
};

static void cal_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct cal_args *a = data;
	desk_draw_calendar(cr, w, h, &a->look, a->now);
}

static bool cal_switching(const struct desk_server *server)
{
	return server->cal_to_view != server->cal_view;
}

static double cal_switch_t(const struct desk_server *server)
{
	if (!cal_switching(server)) {
		return 0.0;
	}
	return clampf_d((monotonic_sec() - server->cal_switch_start) / server->cal_switch_dur, 0, 1);
}

static double cal_turn_t(const struct desk_server *server)
{
	if (!server->cal_turn) {
		return 0.0;
	}
	return clampf_d((monotonic_sec() - server->cal_turn_start) / server->cal_turn_dur, 0, 1);
}

/* Where the calendar close up is on the screen: a point of it at x, y is
 * at x * xf[0] + xf[1], y * xf[0] + xf[2] -- in the middle of the view,
 * or gone aside for the binder of notes, as far as that has got. */
static void cal_aside(const struct desk_server *server, double xf[3])
{
	xf[0] = 1.0;
	xf[1] = xf[2] = 0.0;
	double t = clampf_d(server->agd_t, 0.0, 1.0);
	if (t <= 0.0) {
		return;
	}
	double b[4], cal[3];
	desk_agenda_layout(server->desk_w, server->desk_h, b, cal);
	xf[0] = 1.0 + (cal[0] - 1.0) * t;
	xf[1] = cal[1] * t;
	xf[2] = cal[2] * t;
}

/* a point on the screen, as a point of the calendar close up */
static void cal_unaside(const struct desk_server *server, double *x, double *y)
{
	double xf[3];
	cal_aside(server, xf);
	*x = (*x - xf[1]) / xf[0];
	*y = (*y - xf[2]) / xf[0];
}

/* The step in towards something on the lower board, t of the way: the
 * pictures of the bookcase (sharp, soft) scaled about zx, zy, where it
 * stood, the soft one coming over the sharp. How much nearer it is. */
static double step_in_place(struct desk_server *server, double t, double zx, double zy,
	struct wlr_scene_buffer *sharp, struct wlr_scene_buffer *soft)
{
	int w = server->desk_w, h = server->desk_h;
	double z = 1.0 + CAL_STEP_IN * t;
	struct wlr_scene_buffer *bk[2] = { sharp, soft };
	for (int i = 0; i < 2; i++) {
		if (!bk[i]) {
			continue;
		}
		int pw = w, ph = h;
		scene_pic_size(bk[i], &pw, &ph);
		struct wlr_fbox all = { 0.0, 0.0, (double)pw, (double)ph };
		desk_screen_lay(server, bk[i], all, (int)lround(zx - zx * z), (int)lround(zy - zy * z),
			(int)ceil(w * z), (int)ceil(h * z), 0, 0);
		wlr_scene_node_set_enabled(&bk[i]->node, t > 0.001);
	}
	if (soft) {
		wlr_scene_buffer_set_opacity(soft, (float)t);
	}
	return z;
}

/* Lay the picture where cal_t has got to: its page over the face of the
 * calendar on the shelf at 0, in the middle of the view at 1 -- or aside
 * for the binder. */
static void cal_place(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	double t = clampf_d(server->cal_t, 0.0, 1.0);
	/* the step in: the bookcase scaled about where the calendar stood */
	double fb[4];
	bool have_fb = desk_shelf_gadget_box(w, h, DESK_GADGET_CALENDAR, fb);
	double zx = have_fb ? fb[0] + fb[2] * 0.5 : w * 0.5;
	double zy = have_fb ? fb[1] + fb[3] * 0.5 : h * 0.5;
	double z = step_in_place(server, t, zx, zy, server->cal_sharp, server->cal_soft);
	struct desk_sprite *sp = &server->cal_sp;
	if (!sp->node || !sp->drawn) {
		return;
	}
	double pg[4], box[4], xf[3];
	desk_calendar_rect(w, h, pg, box);
	cal_aside(server, xf);
	double k = xf[0], dx = xf[1], dy = xf[2];
	if (t < 0.9995 && have_fb) {
		/* from the face on the stepped-in shelf -- the page and its tabs;
		 * the box has two pixels to spare all round */
		double fx = zx + (fb[0] + 2.0 - zx) * z, fy = zy + (fb[1] + 2.0 - zy) * z;
		double fw = (fb[2] - 4.0) * z, cw = pg[2] * desk_calendar_face_ratio();
		double tx = pg[0] * xf[0] + xf[1], ty = pg[1] * xf[0] + xf[2];
		double rx = fx + (tx - fx) * t, ry = fy + (ty - fy) * t;
		double rw = fw + (cw * xf[0] - fw) * t;
		k = rw / cw;
		dx = rx - pg[0] * k;
		dy = ry - pg[1] * k;
	}
	wlr_scene_node_set_position(&sp->node->node, (int)lround(sp->x * k + dx),
		(int)lround(sp->y * k + dy));
	int dw = (int)lround(server->cal_nat_w * k), dh = (int)lround(server->cal_nat_h * k);
	wlr_scene_buffer_set_dest_size(sp->node, dw > 1 ? dw : 1, dh > 1 ? dh : 1);
	/* it lifts off the one on the shelf rather than being pasted over it */
	wlr_scene_buffer_set_opacity(sp->node, (float)clampf_d(t * 5.0, 0.0, 1.0));
}

static void cal_look_now(const struct desk_server *server, struct desk_cal_look *lk)
{
	memset(lk, 0, sizeof *lk);
	lk->view = server->cal_view;
	lk->year = server->cal_year;
	lk->month = server->cal_month;
	lk->day = server->cal_day;
	lk->turn = server->cal_turn;
	lk->turn_t = cal_turn_t(server);
	lk->lift = server->cal_lift;
	lk->lift_t = server->cal_lift_t;
	lk->to_view = server->cal_to_view;
	lk->switch_t = cal_switch_t(server);
	lk->hover_tab = server->cal_hover_tab;
	/* Held up, a little turned and leaning back, so that it is seen to be
	 * a tent of boards and not a picture of one: turned more on its own,
	 * where nothing else shows its depth, and less once the binder is
	 * beside it -- off to the side of the eye, its inner side shows
	 * anyway. Coming down off the shelf it turns as it comes. The eye is
	 * over the middle of the screen, wherever the calendar has gone. */
	double up = clampf_d(server->cal_t, 0.0, 1.0);
	up = up * up * (3.0 - 2.0 * up);
	double aside = clampf_d(server->agd_t, 0.0, 1.0);
	lk->yaw = (CAL_HELD_YAW - (CAL_HELD_YAW - CAL_ASIDE_YAW) * aside) * up;
	lk->pitch = CAL_HELD_PITCH * up;
	double xf[3];
	cal_aside(server, xf);
	lk->eye_x = (server->desk_w * 0.5 - xf[1]) / xf[0];
	lk->eye_y = (server->desk_h * 0.5 - xf[2]) / xf[0];
	lk->eye_f = server->desk_h * 1.6 / xf[0];
	/* the notes off the page just now: in the air, being written on in
	 * the binder, being peeled off */
	int n = 0;
	for (int i = 0; i < AGD_FLIGHTS && n < DESK_CAL_HIDE; i++) {
		if (server->agd_fly[i].mode != AGD_FLY_NONE) {
			lk->hide[n++] = server->agd_fly[i].s.id;
		}
	}
	if (server->agd_edit > 0 && n < DESK_CAL_HIDE) {
		lk->hide[n++] = server->agd_edit;
	}
	if (server->agd_gone_id && n < DESK_CAL_HIDE) {
		lk->hide[n++] = server->agd_gone_id;
	}
}

/* The page the calendar will be at once what is going over has gone:
 * where a note sent to it now will be found. */
static void cal_look_settled(const struct desk_server *server, struct desk_cal_look *lk)
{
	cal_look_now(server, lk);
	lk->view = server->cal_to_view;
	lk->to_view = lk->view;
	lk->switch_t = 0.0;
	lk->lift = 0;
	lk->lift_t = 0.0;
	if (server->cal_turn) {
		desk_calendar_step(server->cal_view, server->cal_turn, &lk->year, &lk->month, &lk->day);
	}
	if (server->cal_more) {
		desk_calendar_step(server->cal_view, server->cal_more, &lk->year, &lk->month,
			&lk->day);
	}
	lk->turn = 0;
	lk->turn_t = 0.0;
}

static void agd_fly_raise(struct desk_server *server);

static void paint_calendar(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (!cal_up(server) || !shelf_up(server) || w < 8 || h < 8) {
		if (server->cal_tree) {
			wlr_scene_node_set_enabled(&server->cal_tree->node, false);
		}
		return;
	}
	if (!server->cal_tree) {
		server->cal_tree = wlr_scene_tree_create(server->layer_shelf);
	}
	wlr_scene_node_set_enabled(&server->cal_tree->node, true);
	wlr_scene_node_raise_to_top(&server->cal_tree->node);

	struct cal_args a;
	cal_look_now(server, &a.look);
	a.now = time(NULL);
	struct tm tmv;
	localtime_r(&a.now, &tmv);
	uint64_t key = 14695981039346656037ull;
	key = key_add(key, &a.look, sizeof a.look);
	unsigned gen = desk_agenda_generation();
	key = key_add(key, &gen, sizeof gen);
	key = key_add(key, &tmv.tm_yday, sizeof tmv.tm_yday);
	key = key_add(key, &w, sizeof w);
	key = key_add(key, &h, sizeof h);
	struct desk_sprite *sp = &server->cal_sp;
	if (!sp->drawn || sp->key != key) {
		double box[4];
		desk_calendar_extent(w, h, &a.look, box);
		sp->key = key;
		sprite_paint(server, sp, server->cal_tree, box, cal_draw, &a);
		if (sp->node) {
			wlr_scene_node_raise_to_top(&sp->node->node);
		}
		int bw, bh;
		if (sp->node && scene_pic_size(sp->node, &bw, &bh)) {
			int ow, oh;
			desk_overlay_size(w, h, &ow, &oh);
			server->cal_nat_w = (double)bw * w / ow;
			server->cal_nat_h = (double)bh * h / oh;
		}
	}
	cal_place(server);
	agd_fly_raise(server);
}

static double cal_pan(struct desk_server *server)
{
	double fb[4];
	if (!desk_shelf_gadget_box(server->desk_w, server->desk_h, DESK_GADGET_CALENDAR, fb)) {
		return -0.2;
	}
	return snd_pan(server, fb[0] + fb[2] * 0.5);
}

static void cal_move(struct desk_server *server, double to, double dur)
{
	server->cal_from = server->cal_t;
	server->cal_to = to;
	server->cal_start = monotonic_sec();
	server->cal_dur = dur * fmax(fabs(to - server->cal_t), 0.35);
	server->cal_moving = 1;
	server_schedule_frames(server);
	paint_calendar(server);
}

/* A box blur of an ARGB picture, r pixels each way, along and then down
 * (premultiplied, so every channel alike). */
static void cal_box_blur(uint32_t *px, int w, int h, int stride, int r)
{
	uint32_t *line = malloc(sizeof(uint32_t) * (size_t)(w > h ? w : h));
	if (!line) {
		return;
	}
	for (int pass = 0; pass < 2; pass++) {
		int n = pass ? h : w, m = pass ? w : h;
		for (int j = 0; j < m; j++) {
			for (int i = 0; i < n; i++) {
				line[i] = pass ? px[(size_t)i * stride + j] : px[(size_t)j * stride + i];
			}
			for (int i = 0; i < n; i++) {
				unsigned sum[4] = { 0, 0, 0, 0 }, c = 0;
				for (int k = i - r; k <= i + r; k++) {
					uint32_t v = line[k < 0 ? 0 : (k >= n ? n - 1 : k)];
					for (int b = 0; b < 4; b++) {
						sum[b] += (v >> (8 * b)) & 0xffu;
					}
					c++;
				}
				uint32_t o = 0;
				for (int b = 0; b < 4; b++) {
					o |= ((sum[b] + c / 2) / c) << (8 * b);
				}
				if (pass) {
					px[(size_t)i * stride + j] = o;
				} else {
					px[(size_t)j * stride + i] = o;
				}
			}
		}
	}
	free(line);
}

/* The bookcase as it stands, to step in towards: as drawn, and small and
 * blurred (the scene graph stretches it back, which softens it further),
 * put in tree under whatever else is in it. */
static void shelf_snapshot(struct desk_server *server, struct wlr_scene_tree *tree,
	struct wlr_scene_buffer **sharp_slot, struct wlr_scene_buffer **soft_slot)
{
	int w = server->desk_w, h = server->desk_h;
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	time_t now = time(NULL);
	/* The sharp one is only seen while the step in moves -- the soft one is over it by the end --
	 * so it is drawn as finely as things in motion are (motion_res): on a Surface Pro's screen
	 * at High the whole bookcase drawn again at 2048 wide was a stop of 25 ms and more on every
	 * opening of the memo or the calendar (2026-11-12, the owner: 卓上メモやカレンダーが依然と
	 * して重い). */
	double fine = desk_gfx_level() > DESK_GFX_LOW ? motion_res() : 1.0;
	int fw = (int)ceil(ow * fine), fh = (int)ceil(oh * fine);
	struct desk_buf *sharp = buf_create(fw, fh);
	cairo_save(sharp->cr);
	cairo_scale(sharp->cr, (double)fw / w, (double)fh / h);
	shelf_dress_today();
	desk_draw_shelf(sharp->cr, w, h, -1, 0.0, server->shelf_binder);
	desk_draw_shelf_gadgets(sharp->cr, w, h, now);
	cairo_restore(sharp->cr);
	cairo_surface_flush(sharp->surface);
	if (desk_gfx_level() <= DESK_GFX_LOW) {
		/* Low and 2D: the bookcase as it is, not softened behind */
		if (*soft_slot) {
			wlr_scene_node_destroy(&(*soft_slot)->node);
			*soft_slot = NULL;
		}
		scene_put(sharp_slot, tree, sharp, 0, 0, NULL);
		wlr_scene_node_lower_to_bottom(&(*sharp_slot)->node);
		return;
	}

	const int down = 3;
	int sw = ow / down > 1 ? ow / down : 1, sh = oh / down > 1 ? oh / down : 1;
	struct desk_buf *soft = buf_create(sw, sh);
	cairo_save(soft->cr);
	cairo_scale(soft->cr, (double)sw / fw, (double)sh / fh);
	cairo_set_source_surface(soft->cr, sharp->surface, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(soft->cr), CAIRO_FILTER_GOOD);
	cairo_paint(soft->cr);
	cairo_restore(soft->cr);
	cairo_surface_flush(soft->surface);
	cal_box_blur((uint32_t *)cairo_image_surface_get_data(soft->surface), sw, sh,
		cairo_image_surface_get_stride(soft->surface) / 4, 1);
	cairo_surface_mark_dirty(soft->surface);

	scene_put(sharp_slot, tree, sharp, 0, 0, NULL);
	scene_put(soft_slot, tree, soft, 0, 0, NULL);
	/* under the thing in your hand */
	wlr_scene_node_lower_to_bottom(&(*soft_slot)->node);
	wlr_scene_node_lower_to_bottom(&(*sharp_slot)->node);
}

static void cal_snapshot(struct desk_server *server)
{
	if (!server->cal_tree) {
		server->cal_tree = wlr_scene_tree_create(server->layer_shelf);
	}
	shelf_snapshot(server, server->cal_tree, &server->cal_sharp, &server->cal_soft);
}

static void cal_open(struct desk_server *server)
{
	if (cal_up(server) && server->cal_to > 0.5) {
		return;
	}
	/* it comes down at today's page, open at the section it was left at */
	if (!cal_up(server)) {
		time_t now = time(NULL);
		struct tm tmv;
		localtime_r(&now, &tmv);
		server->cal_year = tmv.tm_year + 1900;
		server->cal_month = tmv.tm_mon;
		server->cal_day = tmv.tm_mday;
		server->cal_to_view = server->cal_view;
		server->cal_hover_tab = -1;
		server->cal_turn = 0;
		server->cal_more = 0;
		server->cal_zone = 0;
		server->cal_lift = 0;
		server->cal_lift_t = 0.0;
		server->cal_sp.drawn = false;
		cal_snapshot(server);
	}
	/* the binder the pointer was on goes back into the row */
	hover_set(server, -1);
	desk_sound_play(DESK_SND_SHEET_TAKE, cal_pan(server), 0.8, 1.1);
	cal_move(server, 1.0, CAL_OPEN_S);
}

static void agd_close(struct desk_server *server);
static void agd_drop(struct desk_server *server);
static void agd_fly_clear(struct desk_server *server);

static void cal_close(struct desk_server *server)
{
	if (!cal_up(server) || server->cal_to < 0.5) {
		return;
	}
	agd_close(server);
	/* what is in the air is down at once: the calendar is going back */
	agd_fly_clear(server);
	paint_calendar(server);
	server->cal_zone = 0;
	double dur = CAL_SHUT_S * fmax(server->cal_t, 0.35);
	/* stood back on the board as it gets there */
	desk_sound_play_in(DESK_SND_SHEET_DOWN, cal_pan(server), 0.6, 0.0,
		real_secs(dur) + SND_SEEN);
	cal_move(server, 0.0, CAL_SHUT_S);
}

/* Off the screen at once: the view it was in is going. */
static void cal_drop(struct desk_server *server)
{
	agd_drop(server);
	agd_fly_clear(server);
	server->cal_t = 0.0;
	server->cal_to = 0.0;
	server->cal_moving = 0;
	server->cal_turn = 0;
	server->cal_more = 0;
	server->cal_zone = 0;
	server->cal_lift_t = 0.0;
	server->cal_hover_tab = -1;
	if (cal_switching(server)) {
		server->cal_view = server->cal_to_view;
		desk_calendar_set_view(server->cal_view);
	}
	if (server->cal_tree) {
		wlr_scene_node_set_enabled(&server->cal_tree->node, false);
	}
}

/* Turn a page: +1 on to the next of the section (year, month, week or
 * day), -1 back to the last. Asked again while one is going over, the next
 * goes as soon as it is down, quicker. */
static void cal_turn_page(struct desk_server *server, int dir)
{
	if (!cal_settled(server) || cal_switching(server)) {
		return;
	}
	if (server->cal_turn) {
		server->cal_more = clampf_d(server->cal_more + dir, -4, 4);
		return;
	}
	server->cal_turn = dir;
	server->cal_turn_start = monotonic_sec();
	server->cal_turn_dur = server->cal_more ? CAL_TURN_S * 0.62 : CAL_TURN_S;
	/* the lifted edge is the page going over now */
	server->cal_lift_t = 0.0;
	server->cal_lift = 0;
	double rate = server->cal_turn_dur < CAL_TURN_S ? 1.25 : 1.0;
	desk_sound_play(DESK_SND_PAGE_TURN, cal_pan(server) * 0.3, 1.0, rate);
	server_schedule_frames(server);
	paint_calendar(server);
}

/* Go to section v by its tab: the sections in front of it go over the
 * wire to the back together, or those behind it come back over. */
static void cal_switch(struct desk_server *server, int v)
{
	if (!cal_settled(server) || cal_switching(server) || server->cal_turn
		|| v < 0 || v >= DESK_CAL_VIEWS || v == server->cal_view) {
		return;
	}
	int n = abs(v - server->cal_view);
	server->cal_to_view = v;
	server->cal_switch_start = monotonic_sec();
	server->cal_switch_dur = CAL_SWITCH_S + 0.07 * (n - 1);
	server->cal_lift_t = 0.0;
	server->cal_lift = 0;
	server->cal_zone = 0;
	/* a thumb run along the edges, and the bundle going over */
	desk_sound_play(DESK_SND_LETTER_FLICK, cal_pan(server) * 0.3, 0.8 + 0.15 * n, 1.0);
	desk_sound_play(DESK_SND_PAGE_TURN, cal_pan(server) * 0.3, 1.1, 0.80 - 0.05 * n);
	server_schedule_frames(server);
	paint_calendar(server);
}

static void cal_tick(struct desk_server *server)
{
	if (!cal_up(server)) {
		return;
	}
	double now = monotonic_sec();
	bool changed = false;
	if (server->cal_moving) {
		double u = clampf_d((now - server->cal_start) / server->cal_dur, 0, 1);
		/* up quickly and settling; back down evenly */
		double e = server->cal_to > 0.5 ? 1.0 - pow(1.0 - u, 3.0) : u * u * (3.0 - 2.0 * u);
		server->cal_t = server->cal_from + (server->cal_to - server->cal_from) * e;
		if (u >= 1.0) {
			server->cal_t = server->cal_to;
			server->cal_moving = 0;
		}
		changed = true;
	}
	if (cal_switching(server)) {
		if (cal_switch_t(server) >= 1.0) {
			server->cal_view = server->cal_to_view;
			/* the calendar on the shelf is open there too, now */
			desk_calendar_set_view(server->cal_view);
			paint_shelf(server);
		}
		changed = true;
	}
	if (server->cal_turn) {
		if (cal_turn_t(server) >= 1.0) {
			desk_calendar_step(server->cal_view, server->cal_turn, &server->cal_year,
				&server->cal_month, &server->cal_day);
			server->cal_turn = 0;
			if (server->cal_more) {
				int dir = server->cal_more > 0 ? 1 : -1;
				server->cal_more -= dir;
				cal_turn_page(server, dir);
			}
		}
		changed = true;
	}
	/* the edge under the pointer up, the other down first */
	int want = server->cal_zone == 1 ? -1 : (server->cal_zone == 2 ? 1 : 0);
	if (server->cal_turn || cal_switching(server) || !cal_settled(server)) {
		want = 0;
	}
	double dt = server->cal_lift_at > 0.0 ? now - server->cal_lift_at : 0.0;
	server->cal_lift_at = now;
	double step = fmin(dt, 0.05) / CAL_LIFT_S;
	if (want != 0 && server->cal_lift == want) {
		if (server->cal_lift_t < 1.0) {
			server->cal_lift_t = fmin(1.0, server->cal_lift_t + step);
			changed = true;
		}
	} else if (server->cal_lift_t > 0.0) {
		server->cal_lift_t = fmax(0.0, server->cal_lift_t - step);
		changed = true;
	} else if (server->cal_lift != want) {
		server->cal_lift = want;
		changed = true;
	}
	if (server->cal_t <= 0.0005 && !server->cal_moving) {
		cal_drop(server);
		return;
	}
	if (changed) {
		paint_calendar(server);
		server_schedule_frames(server);
	}
}

#ifndef DESK_HAVE_POST
/* Built without the bus, there is no input method to type through: the
 * binder types the keys itself. */
bool desk_ime_typing_begin(const struct desk_ime_typing *t)
{
	(void)t;
	return false;
}

void desk_ime_typing_end(void)
{
}

void *desk_ime_typing_owner(void)
{
	return NULL;
}

bool desk_ime_typing_key(uint32_t keysym, uint32_t keycode, uint32_t mods, bool released)
{
	(void)keysym;
	(void)keycode;
	(void)mods;
	(void)released;
	return false;
}

void desk_ime_typing_reset(void)
{
}

void desk_ime_typing_cursor(int x, int y, int w, int h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}
#endif

/* ---- the memo pad, close up ------------------------------------------
 *
 * Clicked on the shelf, it is taken down as the calendar is -- the step
 * in towards the bookcase, the pad coming up to the middle of the view a
 * little turned in the hand -- open at the sheet it was left at, the
 * caret at the end of what is written there. What is typed is written on
 * it (through the input method, as the binder of notes and the book take
 * it). The wheel, Page Up and Page Down turn the sheets over the wire;
 * turning on past the last one written on gives a clean one, as Ctrl+N
 * does. A click on the sheet puts the caret there; off the pad, Escape or
 * the other button put it back on the shelf. What is written is kept a
 * moment after the last change, and whenever a sheet is turned or the pad
 * put back (memo.c). */

#define MEMO_OPEN_S 0.42
#define MEMO_SHUT_S 0.34
#define MEMO_TURN_S 0.46
#define MEMO_HELD_YAW 0.18   /* held to be written on: turned less than the calendar */
#define MEMO_HELD_PITCH 0.10
#define MEMO_KEEP_MS 1000    /* kept this long after the last change */
#define DESK_NOTCH 15.0    /* a wheel's notch, in the units scrolling comes in */

static void memo_close(struct desk_server *server);
