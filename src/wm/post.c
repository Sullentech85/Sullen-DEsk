/* Part of wm.c: the post box and its letters.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* ================================================================
 * The post box
 *
 * It stands at the back right, and from the desk all you see of it is its
 * cap: the slot, the nameplate, and the corner of an envelope when there
 * is something in it. Clicking takes you round to the front, where the
 * door has fallen open and the letters are standing in it.
 *
 * Taking one out is not opening a panel. The envelope comes out of the
 * box, the paper comes out of the envelope, and the paper ends up on the
 * desk -- where it stays, because a letter you have read is still a piece
 * of paper on your desk until you do something with it.
 * ================================================================ */

/* Where the k-th letter taken out of the post lies on a desk w by h: a
 * sheet of letter paper, so many points by so many, in the middle of the
 * desk and a little further along each time. */
static void paper_place(struct desk_paper *pp, int w, int h, int k)
{
	double pt = desk_px_per_pt(w);
	pp->w = (int)lround(511.0 * pt);
	pp->h = (int)lround(410.0 * pt);
	pp->x = (int)lround(w * 0.5 - pp->w * 0.5 + k * 37.0 * pt);
	pp->y = (int)lround(h * 0.4825 - pp->h * 0.5 + k * 25.0 * pt);
}

/* The letters on the desk laid out again, each where its place in the pile
 * puts it -- after one has gone from under the others, so that none ends up
 * exactly on top of another. */
static void papers_place(struct desk_server *server, int w, int h)
{
	for (int i = 0; i < server->pb_papers; i++) {
		struct desk_paper *pp = &server->pb_paper[i];
		paper_place(pp, w, h, i);
		if (pp->placed) {
			/* where it was put, on a desk of another size the same part
			 * of the way across */
			pp->x = (int)lround(pp->fx * w);
			pp->y = (int)lround(pp->fy * h);
		}
		if (pp->node) {
			wlr_scene_buffer_set_dest_size(pp->node, pp->w, pp->h);
			wlr_scene_node_set_position(&pp->node->node, pp->x, pp->y);
		}
	}
}

static void post_land(struct desk_server *server);

static bool post_up(const struct desk_server *server)
{
	return server->pb_t > 0.0005 || server->pb_moving || server->pb_fly_on;
}

#ifdef DESK_HAVE_POST
static int post_count(const struct desk_note **out)
{
	return desk_post_letters(out);
}
static int post_unread(void)
{
	return desk_post_unread();
}
#else
static int post_count(const struct desk_note **out)
{
	*out = NULL;
	return 0;
}
static int post_unread(void)
{
	return 0;
}
#endif

static void pb_view_draw(cairo_t *cr, int w, int h, void *data)
{
	struct desk_server *server = data;
	if (server->pb_t > 0.70) {
		const struct desk_note *notes = NULL;
		int n = post_count(&notes);
		struct desk_post_curl pc = { server->pbc_idx, server->pbc_s, server->pbc_lift,
			server->pbc_hot };
		desk_draw_post(cr, w, h, notes, n, server->pb_hover, 1.0, server->pb_pos, &pc);
	}
	if (server->pb_fly_on) {
		double u = clampf_d((monotonic_sec() - server->pb_fly_start)
			/ server->pb_fly_dur, 0, 1);
		/* to where it will lie on the desk */
		struct desk_paper at;
		/* where it will lie: the last place, when the pile is full and the
		 * oldest is to go from under it (post_land) */
		int cap = (int)(sizeof server->pb_paper / sizeof server->pb_paper[0]);
		paper_place(&at, w, h, server->pb_papers < cap ? server->pb_papers : cap - 1);
		desk_draw_sheet_flight(cr, server->pb_fly,
			server->pb_fly_x, server->pb_fly_y, server->pb_fly_w, server->pb_fly_h,
			at.x, at.y, at.w, at.h, u);
	}
}

/* The settled view of the box, moved with the camera at the end of a move. */
static void pb_follow(struct desk_server *server, int dx, int dy)
{
	if (server->pb_sprite.node) {
		wlr_scene_node_set_position(&server->pb_sprite.node->node,
			server->pb_sprite.x + dx, server->pb_sprite.y + dy);
	}
	if (server->pb_base) {
		wlr_scene_node_set_position(&server->pb_base->node, dx, dy);
	}
}

static void paint_post_body(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	if (!post_up(server)) {
		if (server->pb_sprite.node) {
			wlr_scene_node_set_enabled(&server->pb_sprite.node->node, false);
		}
		if (server->pb_base) {
			wlr_scene_node_set_enabled(&server->pb_base->node, false);
		}
		if (server->film_a[DESK_VIEW_POST]) {
			wlr_scene_node_set_enabled(&server->film_a[DESK_VIEW_POST]->node, false);
			wlr_scene_node_set_enabled(&server->film_b[DESK_VIEW_POST]->node, false);
		}
		return;
	}
	film_ensure(server, DESK_VIEW_POST, server->layer_post);

	unsigned pb_room = desk_view_room(w, h, DESK_VIEW_POST);
	if (!server->pb_base || server->pb_base_w != w || server->pb_base_h != h
		|| server->pb_base_room != pb_room) {
		struct desk_buf *bb = buf_create(w, h);
		desk_draw_post(bb->cr, w, h, NULL, 0, -1, 0.0, 0.0, NULL);
		scene_put(&server->pb_base, server->layer_post, bb, 0, 0, NULL);
		server->pb_base_w = w;
		server->pb_base_h = h;
		server->pb_base_room = pb_room;
		view_bases_under(server);
	}

	/* The letters standing in the box change when the post does, or the
	 * hand moves to another one -- not on the frames of the camera coming
	 * round to them, which only fade them in. So they are drawn again when
	 * the key says so and only faded otherwise; a letter in the air is
	 * somewhere new every frame. */
	bool have_view = server->pb_t > 0.70 || server->pb_fly_on;
	if (have_view) {
		const struct desk_note *notes = NULL;
		int n = post_count(&notes);
		bool settled = server->pb_t > 0.70;
		uint64_t key = key_add(14695981039346656037ull, &settled, sizeof settled);
		key = key_add(key, &n, sizeof n);
		key = key_add(key, &server->pb_hover, sizeof server->pb_hover);
		key = key_add(key, &server->pb_pos, sizeof server->pb_pos);
		int corner[3] = { server->pbc_idx, (int)lround(server->pbc_s * 4.0), server->pbc_hot };
		key = key_add(key, corner, sizeof corner);
		for (int i = 0; i < n; i++) {
			key = key_add(key, &notes[i].id, sizeof notes[i].id);
			key = key_add(key, &notes[i].read, sizeof notes[i].read);
			key = key_add(key, &notes[i].at, sizeof notes[i].at);
			key = key_add(key, &notes[i].urgency, sizeof notes[i].urgency);
			key = key_add(key, notes[i].app, strnlen(notes[i].app, sizeof notes[i].app));
			key = key_add(key, notes[i].summary,
				strnlen(notes[i].summary, sizeof notes[i].summary));
			key = key_add(key, notes[i].body, strnlen(notes[i].body, sizeof notes[i].body));
		}
		if (server->pb_fly_on) {
			double at = monotonic_sec();
			key = key_add(key, &at, sizeof at);
		}
		key = key_add(key, &pb_room, sizeof pb_room);
		bool force = server->pb_sprite_w != w || server->pb_sprite_h != h;
		server->pb_sprite_w = w;
		server->pb_sprite_h = h;
		if (force || !server->pb_sprite.drawn || server->pb_sprite.key != key) {
			server->pb_sprite.key = key;
			/* the letters' own corner of the view, except while one is on
			 * its way to the desk */
			double box[4] = { 0.0, 0.0, (double)w, (double)h };
			if (!server->pb_fly_on && !desk_post_letters_area(w, h, box)) {
				box[0] = box[1] = 0.0;
				box[2] = w;
				box[3] = h;
			}
			sprite_paint(server, &server->pb_sprite, server->layer_post, box,
				pb_view_draw, server);
		}
	}
	if (server->pb_sprite.node) {
		wlr_scene_node_set_enabled(&server->pb_sprite.node->node, have_view);
	}
	wlr_scene_node_set_enabled(&server->pb_base->node, server->pb_t > 0.70);

	double t = server->pb_t;
	if (!server->film_a[DESK_VIEW_POST] || (!server->pb_moving && t >= 0.9995)) {
		film_hide(server, DESK_VIEW_POST);
		pb_follow(server, 0, 0);
		if (server->pb_sprite.node) {
			wlr_scene_buffer_set_opacity(server->pb_sprite.node, 1.0f);
		}
		wlr_scene_buffer_set_opacity(server->pb_base, 1.0f);
		return;
	}
	double over;
	int dx, dy;
	film_play(server, DESK_VIEW_POST, t, &over, &dx, &dy);
	pb_follow(server, dx, dy);
	double a = server->pb_fly_on ? 1.0 : over;
	if (server->pb_sprite.node) {
		wlr_scene_buffer_set_opacity(server->pb_sprite.node, (float)(have_view ? a : 0.0));
		wlr_scene_node_set_enabled(&server->pb_sprite.node->node,
			have_view && (over > 0.001 || server->pb_fly_on));
	}
	wlr_scene_buffer_set_opacity(server->pb_base, (float)over);
	wlr_scene_node_set_enabled(&server->pb_base->node, over > 0.001);
}

static void paint_post(struct desk_server *server)
{
	prof_begin(PROF_POST);
	paint_post_body(server);
	prof_end();
}

/* Turn the letters in the box over, a letter a step, as a binder's sheets
 * are turned: the one turned to stands up, the ones before it go over
 * forward and the ones after lean back. */
static void pb_turn_to(struct desk_server *server, double to);

static void pb_riffle(struct desk_server *server, int step)
{
	double to = server->pb_pos_to + step;
	/* not further ahead of where the letters have got to than a couple */
	if (to > server->pb_pos + 2.5) {
		to = floor(server->pb_pos + 2.5);
	}
	if (to < server->pb_pos - 2.5) {
		to = ceil(server->pb_pos - 2.5);
	}
	pb_turn_to(server, to);
}

/* The letters to a place among them. */
static void pb_turn_to(struct desk_server *server, double to)
{
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	if (to > n - 1) {
		to = n - 1;
	}
	if (to < 0) {
		to = 0;
	}
	if (to == server->pb_pos_to && !server->pb_pos_moving) {
		return;
	}
	server->pb_pos_to = to;
	if (!server->pb_pos_moving) {
		server->pb_pos_at = monotonic_sec();
		server->pb_pos_vel = 0.0;
	}
	server->pb_pos_moving = 1;
	/* a corner that was up goes down as its letter goes by */
	server->pbc_near = false;
	server->pbc_hover_at = 0.0;
	server->pbc_hot = -1;
	paint_post(server);
	server_schedule_frames(server);
}

/* The letters follow where they have been turned to on the binder's own
 * spring: over, a shade too far, and back. */
static void pb_pos_tick(struct desk_server *server)
{
	if (!server->pb_pos_moving) {
		return;
	}
	const double W = 24.0, Z = 0.72;
	double now = monotonic_sec();
	double dt = clampf_d(now - server->pb_pos_at, 0.0, 0.05);
	server->pb_pos_at = now;
	int k = (int)ceil(dt / (1.0 / 480.0));
	double h = k > 0 ? dt / k : 0.0;
	for (int i = 0; i < k; i++) {
		double acc = W * W * (server->pb_pos_to - server->pb_pos)
			- 2.0 * Z * W * server->pb_pos_vel;
		server->pb_pos_vel += acc * h;
		server->pb_pos += server->pb_pos_vel * h;
	}
	int letter = (int)lround(server->pb_pos);
	if (letter != server->snd_letter) {
		desk_sound_play(DESK_SND_LETTER_FLICK, 0.4, 1.0, 0.0);
		server->snd_letter = letter;
	}
	if (gfx_flat()) {
		server->pb_pos = server->pb_pos_to;
		server->pb_pos_vel = 0.0;
	}
	if (fabs(server->pb_pos_to - server->pb_pos) < 0.002 && fabs(server->pb_pos_vel) < 0.02) {
		server->pb_pos = server->pb_pos_to;
		server->pb_pos_vel = 0.0;
		server->pb_pos_moving = 0;
	}
	/* what is under the hand has moved */
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	server->pb_hover = desk_post_hit(server->desk_w, server->desk_h, n, server->pb_pos,
		server->cursor->x, server->cursor->y);
	paint_post(server);
	if (server->pb_pos_moving) {
		server_schedule_frames(server);
	}
}

/* ---- a letter's corner -------------------------------------------
 *
 * The same corner a sheet on the desk has, on the letter that is standing
 * up to be read: the hand resting on its fold stirs it, held there or
 * clicked it turns up, and what is printed on its back is pressed there.
 * Clicking the corner of a letter further back turns the letters to it,
 * and its corner comes up as it arrives. */

static void post_take(struct desk_server *server, int idx);

static void pbc_reset(struct desk_server *server)
{
	server->pbc_idx = -1;
	server->pbc_id = 0;
	server->pbc_s = 0.0;
	server->pbc_v = 0.0;
	server->pbc_at = 0.0;
	server->pbc_lift = 0.0;
	server->pbc_near = false;
	server->pbc_hover_at = 0.0;
	server->pbc_hot = -1;
	server->pbc_act = -1;
}

/* the size of a letter's face, and its corner's */
static bool pbc_sizes(struct desk_server *server, double *fw, double *fh, double *rest,
	double *open)
{
	double fx, fy;
	*fw = *fh = 0.0;
	desk_post_on_letter(server->desk_w, server->desk_h, -1, 0, 0.0, 0.0, 0.0,
		&fx, &fy, fw, fh);
	if (*fw < 8.0) {
		return false;
	}
	desk_post_curl_sizes(*fw, *fh, rest, open);
	return true;
}

/* Where the pointer is on letter i's face, if it is on it. */
static bool pbc_on(struct desk_server *server, int i, int n, double *fx, double *fy,
	double *fw, double *fh)
{
	return desk_post_on_letter(server->desk_w, server->desk_h, i, n, server->pb_pos,
		server->cursor->x, server->cursor->y, fx, fy, fw, fh);
}

static void pbc_track(struct desk_server *server)
{
	if (server->pbc_act >= 0 || server->pb_pos_moving) {
		return;
	}
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	int sel = (int)lround(server->pb_pos);
	bool near = server->pbc_near, on_fold = false;
	int hot = -1;
	double fx, fy, fw, fh, rest, open;
	if (sel >= 0 && sel < n && pbc_on(server, sel, n, &fx, &fy, &fw, &fh)) {
		desk_post_curl_sizes(fw, fh, &rest, &open);
		double dx = fw - fx;
		double zone = rest * 2.6;
		on_fold = dx >= -8.0 && dx <= zone && fy >= -8.0 && fy <= zone;
		if (near) {
			near = dx >= -8.0 && dx <= open * 1.30 && fy >= -8.0 && fy <= open * 1.30;
		}
		if (near) {
			hot = desk_post_curl_hit(&notes[sel], fw, fh, server->pbc_s, fx, fy);
		}
	} else {
		near = false;
	}
	if (near || on_fold) {
		if (server->pbc_idx != sel) {
			server->pbc_idx = sel;
			server->pbc_id = notes[sel].id;
			server->pbc_at = 0.0;
		}
	}
	if (!near && on_fold) {
		if (server->pbc_hover_at <= 0.0) {
			server->pbc_hover_at = real_sec();
		}
	} else {
		server->pbc_hover_at = 0.0;
	}
	if (near != server->pbc_near || hot != server->pbc_hot || on_fold) {
		server->pbc_near = near;
		server->pbc_hot = hot;
		paint_post(server);
		server_schedule_frames(server);
	}
}

/* A click at the post box: on the back of a turned-up corner, on a
 * corner's fold, or neither (false). */
static bool pbc_press(struct desk_server *server)
{
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	if (server->pbc_act >= 0) {
		return true;
	}
	int idx = server->pbc_idx;
	if (server->pbc_near && server->pbc_hot >= 0 && idx >= 0 && idx < n) {
		server->pbc_act = server->pbc_hot;
		desk_sound_play(DESK_SND_CORNER_TAP, 0.2, 1.0, 0.0);
		server->pbc_act_at = monotonic_sec();
		if (server->pbc_act == DESK_LETTER_MARK) {
#ifdef DESK_HAVE_POST
			desk_post_set_read(notes[idx].id, !notes[idx].read);
#endif
		}
		server_schedule_frames(server);
		return true;
	}
	/* the fold of the letter standing up, or of one further back, whose
	 * head is all that shows: the front-most letter under the hand */
	int sel = (int)lround(server->pb_pos);
	for (int i = sel; i >= 0 && i < n && i < sel + 5; i++) {
		double fx, fy, fw, fh, rest, open;
		if (!pbc_on(server, i, n, &fx, &fy, &fw, &fh)) {
			continue;
		}
		desk_post_curl_sizes(fw, fh, &rest, &open);
		double zone = rest * 2.6;
		if (!(fw - fx <= zone && fy <= zone)) {
			return false;
		}
		if (i != sel) {
			pb_riffle(server, i - sel);
		}
		server->pbc_idx = i;
		server->pbc_id = notes[i].id;
		desk_sound_play(DESK_SND_CORNER_LIFT, 0.2, 1.0, 0.0);
		server->pbc_near = true;
		server->pbc_hover_at = 0.0;
		server->pbc_hot = -1;
		server_schedule_frames(server);
		return true;
	}
	return false;
}

/* Once a frame: the corner goes where it is going, on the spring a
 * sheet's corner has, and what was pressed on it is done. */
static void pbc_tick(struct desk_server *server)
{
	if (server->pbc_idx < 0) {
		return;
	}
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	int idx = server->pbc_idx;
	double fw, fh, rest, open;
	if (!post_up(server) || idx >= n || notes[idx].id != server->pbc_id
		|| !pbc_sizes(server, &fw, &fh, &rest, &open)) {
		/* the box was shut, or the letter went */
		pbc_reset(server);
		paint_post(server);
		return;
	}
	double now = monotonic_sec();
	bool more = false;
	if (!server->pbc_near && server->pbc_hover_at > 0.0) {
		if (real_sec() - server->pbc_hover_at >= 0.5 && server->conf.corner_hover) {
			desk_sound_play(DESK_SND_CORNER_LIFT, 0.2, 1.0, 0.0);
			server->pbc_near = true;
			server->pbc_hover_at = 0.0;
		} else {
			more = true;
		}
	}
	double to = server->pbc_near ? open : (server->pbc_hover_at > 0.0 ? rest * 1.7 : rest);
	if (server->pbc_act >= 0) {
		double e = now - server->pbc_act_at;
		unsigned id = notes[idx].id;
		switch (server->pbc_act) {
		case DESK_LETTER_READ:
			/* laid down again, and the letter comes out */
			to = rest;
			if (e > 0.14) {
				pbc_reset(server);
				post_take(server, idx);
				return;
			}
			break;
		case DESK_LETTER_MARK:
			/* a nod of the corner: it is done, and the words have changed */
			to = open * 0.86;
			if (e > 0.16) {
				server->pbc_act = -1;
			}
			break;
		case DESK_LETTER_ACTION:
			to = open * 1.25;
			if (e > 0.18) {
				const char *key = desk_post_letter_action(&notes[idx], NULL);
				bool own = notes[idx].own;
				pbc_reset(server);
				if (key && own) {
					own_letter_action(server, id, key);
				} else if (key) {
#ifdef DESK_HAVE_POST
					desk_post_invoke(id, key);
#endif
				}
				paint_post(server);
				return;
			}
			break;
		default:
			/* peeled right back, and the letter is thrown away */
			to = open * 1.9;
			if (e > 0.22) {
				pbc_reset(server);
#ifdef DESK_HAVE_POST
				desk_post_dismiss(id);
#endif
				(void)id;
				paint_post(server);
				return;
			}
			break;
		}
		more = true;
	}
	double dt = server->pbc_at > 0.0 ? clampf_d(now - server->pbc_at, 0.0, 0.05) : 0.0;
	server->pbc_at = now;
	if (server->pbc_s <= 0.0) {
		server->pbc_s = rest;
	}
	const double k = 240.0, c = 2.0 * 0.60 * 15.49;
	double acc = k * (to - server->pbc_s) - c * server->pbc_v;
	server->pbc_v += acc * dt;
	server->pbc_s += server->pbc_v * dt;
	if (gfx_flat()) {
		server->pbc_s = to;
		server->pbc_v = 0.0;
	}
	if (fabs(to - server->pbc_s) < 0.15 && fabs(server->pbc_v) < 2.0) {
		server->pbc_s = to;
		server->pbc_v = 0.0;
	} else {
		more = true;
	}
	server->pbc_lift = clampf_d((server->pbc_s - rest) / fmax(open - rest, 1.0), 0.0, 1.0);
	/* the words move with the flap, under a pointer that may not */
	if (server->pbc_near && server->pbc_act < 0) {
		double fx, fy, w2, h2;
		server->pbc_hot = pbc_on(server, idx, n, &fx, &fy, &w2, &h2)
			? desk_post_curl_hit(&notes[idx], fw, fh, server->pbc_s, fx, fy) : -1;
	}
	paint_post(server);
	if (more) {
		server_schedule_frames(server);
	} else if (!server->pbc_near && server->pbc_hover_at <= 0.0 && server->pbc_act < 0
		&& fabs(server->pbc_s - rest) < 0.2) {
		/* down again: nobody's corner */
		server->pbc_idx = -1;
		paint_post(server);
	}
}

/* The paper the flight was carrying comes to rest on the desk. It stays
 * there: a letter you have read is still a piece of paper on your desk
 * until you do something with it, and putting it away is a thing you do,
 * not a thing that happens to you. */
static void post_land(struct desk_server *server)
{
	server->pb_fly_on = 0;
	if (!server->pb_fly) {
		return;
	}
	int w = server->desk_w, h = server->desk_h;
	if (server->pb_papers >= (int)(sizeof server->pb_paper / sizeof server->pb_paper[0])) {
		/* the desk only takes so much before the bottom one goes away */
		struct desk_paper *old = &server->pb_paper[0];
		if (old->node) {
			wlr_scene_node_destroy(&old->node->node);
		}
		if (old->sheet) {
			cairo_surface_destroy(old->sheet);
		}
		memmove(&server->pb_paper[0], &server->pb_paper[1],
			(size_t)(server->pb_papers - 1) * sizeof server->pb_paper[0]);
		server->pb_papers--;
		/* (the one in the hand is one further down now, or gone) */
		if (server->pp_held > 0) {
			server->pp_held--;
		}
		papers_place(server, w, h);
	}
	struct desk_paper *pp = &server->pb_paper[server->pb_papers++];
	memset(pp, 0, sizeof *pp);
	pp->sheet = server->pb_fly;
	pp->id = server->pb_fly_id;
	server->pb_fly = NULL;

	/* a sheet of letter paper, in points, laid down in the middle of the
	 * desk a little further along each time, and never square */
	int k = server->pb_papers - 1;
	paper_place(pp, w, h, k);

	struct desk_buf *buf = buf_create(pp->w, pp->h);
	desk_set_paint_origin(pp->x, pp->y);
	desk_draw_window_shadow(buf->cr, 0, 0, pp->w, pp->h, 6.0);
	cairo_set_source_surface(buf->cr, pp->sheet, 0, 0);
	cairo_paint(buf->cr);
	desk_set_paint_origin(0, 0);
	scene_put(&pp->node, server->layer_win, buf, pp->x, pp->y, NULL);
	wlr_scene_node_set_enabled(&pp->node->node, true);
	snd_at(server, snd_sheet_down(server, pp->x + pp->w * 0.5, pp->y + pp->h * 0.5),
		pp->x + pp->w * 0.5, 0.8);
	paint_post(server);
	server_schedule_frames(server);
}

/* Put one away: it is thrown out, and the sender is told so. */
static void post_paper_away(struct desk_server *server, int i)
{
	if (i < 0 || i >= server->pb_papers) {
		return;
	}
	struct desk_paper *pp = &server->pb_paper[i];
	snd_at(server, DESK_SND_PAPER_CRUMPLE, pp->x + pp->w * 0.5, 0.7);
	if (pp->node) {
		wlr_scene_node_destroy(&pp->node->node);
	}
	if (pp->sheet) {
		cairo_surface_destroy(pp->sheet);
	}
#ifdef DESK_HAVE_POST
	desk_post_dismiss(pp->id);
#endif
	memmove(&server->pb_paper[i], &server->pb_paper[i + 1],
		(size_t)(server->pb_papers - i - 1) * sizeof server->pb_paper[0]);
	server->pb_papers--;
	if (server->pp_held == i + 1) {
		server->pp_held = 0;
	} else if (server->pp_held > i + 1) {
		server->pp_held--;
	}
	papers_place(server, server->desk_w, server->desk_h);
	server_schedule_frames(server);
}

static bool paper_release(struct desk_server *server);

/* The letter in the hand follows it, once it has moved more than a
 * twitch: picked up, it comes over everything on the desk. */
static bool paper_drag(struct desk_server *server)
{
	if (server->pp_held <= 0 || server->pp_held > server->pb_papers) {
		return false;
	}
	/* the door shut, or the bookcase, the set or the post box come up,
	 * with it in the hand: it is put down where it is */
	if (locked(server) || shelf_up(server) || console_up(server) || post_up(server)) {
		if (server->pp_moved) {
			paper_release(server);
		}
		server->pp_held = 0;
		return false;
	}
	struct desk_paper *pp = &server->pb_paper[server->pp_held - 1];
	double cx = server->cursor->x, cy = server->cursor->y;
	if (!server->pp_moved) {
		double twitch = 5.0 * desk_px_per_pt(server->desk_w);
		if (hypot(cx - server->pp_px, cy - server->pp_py) < twitch) {
			return true;
		}
		server->pp_moved = true;
		if (pp->node) {
			wlr_scene_node_raise_to_top(&pp->node->node);
			views_above_raise(server);
		}
		snd_at(server, DESK_SND_PAPER_SWING, cx, 0.35);
	}
	/* kept on the desk: at least a corner of it in reach */
	int w = server->desk_w, h = server->desk_h;
	double x = fmin(fmax(cx - server->pp_hx, 32.0 - pp->w), w - 32.0);
	double y = fmin(fmax(cy - server->pp_hy, 0.0), h - 32.0);
	pp->x = (int)lround(x);
	pp->y = (int)lround(y);
	if (pp->node) {
		wlr_scene_node_set_position(&pp->node->node, pp->x, pp->y);
	}
	server_schedule_frames(server);
	return true;
}

/* Let go of the letter in the hand: put down where it is, or -- clicked
 * and not moved -- put away. True if there was one. */
static bool paper_release(struct desk_server *server)
{
	if (server->pp_held <= 0) {
		return false;
	}
	int i = server->pp_held - 1;
	server->pp_held = 0;
	if (i >= server->pb_papers) {
		return true;
	}
	if (!server->pp_moved) {
		post_paper_away(server, i);
		return true;
	}
	struct desk_paper *pp = &server->pb_paper[i];
	pp->placed = true;
	pp->fx = server->desk_w > 0 ? (double)pp->x / server->desk_w : 0.0;
	pp->fy = server->desk_h > 0 ? (double)pp->y / server->desk_h : 0.0;
	snd_at(server, snd_sheet_down(server, pp->x + pp->w * 0.5, pp->y + pp->h * 0.5),
		pp->x + pp->w * 0.5, 0.6);
	server_schedule_frames(server);
	return true;
}

/* The letter lying on the desk that is the uppermost thing under the
 * pointer, or -1: a window over it is the window's. The letters lie among
 * the windows, in their layer, so that a window can lie over one -- which
 * is why a click is asked about them before it is taken to be a window's. */
static int post_paper_under(struct desk_server *server)
{
	double sx, sy;
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
		server->cursor->x, server->cursor->y, &sx, &sy);
	for (int i = server->pb_papers - 1; node && i >= 0; i--) {
		if (server->pb_paper[i].node && node == &server->pb_paper[i].node->node) {
			return i;
		}
	}
	return -1;
}

/* Take a letter out of the box: the envelope comes out, the paper comes
 * out of the envelope, and it goes down onto the desk with you. */
static void post_take(struct desk_server *server, int idx)
{
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	if (idx < 0 || idx >= n) {
		return;
	}
	const struct desk_note *note = &notes[idx];
	int w = server->desk_w, h = server->desk_h;
	double bx, by, bw, bh;
	if (!desk_post_letter_box(w, h, idx, n, server->pb_pos, &bx, &by, &bw, &bh)) {
		return;
	}
	snd_at(server, DESK_SND_LETTER_TAKE, bx + bw * 0.5, 1.0);
	if (server->pb_fly) {
		cairo_surface_destroy(server->pb_fly);
	}
	/* the size it will lie on the desk at (paper_place) */
	double pt = desk_px_per_pt(w);
	server->pb_fly = desk_letter_sheet(note, lround(511.0 * pt), lround(410.0 * pt));
	server->pb_fly_x = bx;
	server->pb_fly_y = by;
	server->pb_fly_w = bw;
	server->pb_fly_h = bh;
	server->pb_fly_id = note->id;
	server->pb_fly_start = monotonic_sec();
	server->pb_fly_dur = 0.82;
	server->pb_fly_on = server->pb_fly != NULL;
#ifdef DESK_HAVE_POST
	desk_post_mark_read(note->id);
#endif
	wlr_log(WLR_INFO, "post: taking out \"%s\" from %s", note->summary, note->app);
	/* and you come back to the desk to read it */
	if (server->pb_t > 0.5) {
		post_toggle(server);
	}
}

static void post_toggle(struct desk_server *server)
{
	/* whatever was pulled out from under the Control Panel goes back */
	card_gone(server);
	if (server->pb_t < 0.5
		&& !desk_scene_view_ready(server->desk_w, server->desk_h, DESK_VIEW_POST)) {
		return;
	}
	double now = monotonic_sec();
	double cur = server->pb_t;
	server->pb_from = cur;
	server->pb_to = cur > 0.5 ? 0.0 : 1.0;
	server->pb_start = now;
	server->pb_hover = -1;
	/* it opens at the front letter, with every corner down */
	server->pb_pos = server->pb_pos_to = server->pb_pos_vel = 0.0;
	server->pb_pos_moving = 0;
	server->pb_scroll = 0.0;
	server->snd_letter = 0;
	pbc_reset(server);
	double span = fabs(server->pb_to - cur);
	if (span < 0.30) {
		span = 0.30;
	}
	server->pb_dur = 0.86 * span;
	server->pb_moving = 1;
	desk_sound_play(server->pb_to > 0.5 ? DESK_SND_POST_OPEN : DESK_SND_POST_CLOSE,
		0.55, 1.0, 0.0);
	if (server->pb_to > 0.5 && server->drawer_t > 0.004) {
		drawer_toggle(server);
	}
	server_schedule_frames(server);
	paint_post(server);
}

static void post_tick(struct desk_server *server)
{
	if (server->look_held == DESK_VIEW_POST + 1) {
		paint_post(server);
	} else if (server->look_free == DESK_VIEW_POST + 1
		&& !look_free_step(server, &server->pb_t, server->pb_to)) {
		paint_post(server);
		server_schedule_frames(server);
	} else if (server->pb_moving) {
		if (server->look_free == DESK_VIEW_POST + 1) {
			server->look_free = 0;
			server->pb_start = monotonic_sec() - server->pb_dur;
		}
		double u = clampf_d((monotonic_sec() - server->pb_start) / server->pb_dur, 0, 1);
		double e = look_ease(u);
		server->pb_t = server->pb_from + (server->pb_to - server->pb_from) * e;
		if (u >= 1.0) {
			server->pb_t = server->pb_to;
			server->pb_moving = 0;
		}
		paint_post(server);
		server_schedule_frames(server);
	}
	if (!server->pb_fly_on) {
		return;
	}
	double u = clampf_d((monotonic_sec() - server->pb_fly_start)
		/ server->pb_fly_dur, 0, 1);
	if (u >= 1.0) {
		post_land(server);
		return;
	}
	paint_post(server);
	server_schedule_frames(server);
}

/* The drawer on a finger, or let go of by one: its runs going round as fast as it goes, out or in,
 * as a sheet slid is (2026-11-20, the owner: 引き出しの動きと効果音があっていない -- the run was played
 * once as the finger took it, whatever the finger then did with it). The two runs' own speeds, as
 * drawer.dsp's speed (1 is 1200 points a second), and how quickly what is heard follows. */
#define DRUN_SLOW (0.20 * 1200.0)
#define DRUN_QUICK (0.75 * 1200.0)
#define DRUN_TAU 0.05

static double drawer_lift_at(const struct desk_server *server, double t);

static void drawer_snd_end(struct desk_server *server, double fade)
{
	for (int k = 0; k < 2; k++) {
		desk_sound_stop(server->snd_drun[k], fade);
		server->snd_drun[k] = 0;
	}
	server->drun_of = -1;
	server->drun_v = 0.0;
	server->drun_at = 0.0;
	if (server->drun_timer) {
		wl_event_source_timer_update(server->drun_timer, 0);
	}
}

static void drawer_snd_set(struct desk_server *server)
{
	double v = fmin(server->drun_v, 1.5 * 1200.0);
	/* as loud as drawer.dsp makes the run for the speed, from whichever is nearer */
	double a = pow(v / 1200.0, 0.8);
	double w = clampf_d(log(fmax(v, 1.0) / DRUN_SLOW) / log(DRUN_QUICK / DRUN_SLOW), 0, 1);
	double g[2] = { (1.0 - w) * a / pow(DRUN_SLOW / 1200.0, 0.8),
		w * a / pow(DRUN_QUICK / 1200.0, 0.8) };
	int of = server->drun_out ? DESK_SND_DRAWER_RUN_OUT_SLOW : DESK_SND_DRAWER_RUN_IN_SLOW;
	if (of != server->drun_of) {
		for (int k = 0; k < 2; k++) {
			desk_sound_stop(server->snd_drun[k], 0.06);
			server->snd_drun[k] = 0;
		}
		server->drun_of = of;
	}
	for (int k = 0; k < 2; k++) {
		if (!server->snd_drun[k] && g[k] > 0.002) {
			/* (in at nothing, and up to where it is: no click) */
			server->snd_drun[k] = desk_sound_loop((enum desk_sound)(of + k), 0.0, 0.0);
		}
		desk_sound_set(server->snd_drun[k], 0.0, g[k]);
	}
}

/* the drawer moved: how fast, and which way */
static void drawer_snd_motion(struct desk_server *server)
{
	double now = monotonic_sec();
	double lift = drawer_lift_at(server, server->drawer_t);
	if (server->drun_at > 0.0) {
		double dt = now - server->drun_at;
		if (dt < 1e-4) {
			return;   /* (two at once: the next one has it) */
		}
		double d = (lift - server->drun_lift) / desk_px_per_pt(server->desk_w);
		if (fabs(d) > 0.5) {
			server->drun_out = d > 0.0;
		}
		server->drun_v += (fmin(fabs(d) / dt, 3000.0) - server->drun_v)
			* (1.0 - exp(-dt / DRUN_TAU));
	}
	server->drun_lift = lift;
	server->drun_at = now;
	drawer_snd_set(server);
	if (server->drun_timer) {
		wl_event_source_timer_update(server->drun_timer, 30);
	}
}

static int drun_timer_cb(void *data)
{
	struct desk_server *server = data;
	double idle = monotonic_sec() - server->drun_at;
	bool going = server->drawer_moving && (server->drawer_held || server->drawer_free);
	if (!going || server->drun_at <= 0.0 || idle > 0.4) {
		drawer_snd_end(server, 0.08);
		return 0;
	}
	if (idle > 0.025) {
		/* the finger has stopped, or nearly: dying away */
		server->drun_v *= exp(-0.03 / DRUN_TAU);
		drawer_snd_set(server);
	}
	wl_event_source_timer_update(server->drun_timer, 30);
	return 0;
}

/* Out, or home, from where it has got to. */
static void drawer_go(struct desk_server *server, double to)
{
	/* whatever was pulled out from under the Control Panel goes back */
	card_gone(server);
	double now = monotonic_sec();
	double cur = server->drawer_t;
	server->drawer_held = false;
	server->drawer_from = cur;
	server->drawer_to = to;
	if (server->drawer_to < 0.5) {
		server->drw_hover = -1;
	}
	server->drw_dirty = true;
	server->drawer_start = now;
	double span = fabs(server->drawer_to - cur);
	if (span < 0.28) {
		span = 0.28;
	}
	server->drawer_dur = 0.54 * span;
	server->drawer_moving = 1;
	/* along the runners, until it is at its stop (the stop, if it was on its way to one, is not
	 * reached now): the run is made for the whole way, and a shorter one is cut where it gets
	 * there, dying away as the stop is heard */
	desk_sound_stop(server->snd_drawer, 0.04);
	drawer_snd_end(server, 0.04);
	snd_forget(&server->snd_drawer_stop);
	server->snd_drawer = desk_sound_play(server->drawer_to > 0.5 ? DESK_SND_DRAWER_OUT
		: DESK_SND_DRAWER_IN, 0.0, 0.55 + 0.45 * fmin(span, 1.0), 0.0);
	desk_sound_stop_after(server->snd_drawer,
		real_secs(server->drawer_dur * drawer_ease_hit()) + SND_SEEN, 0.10);
	server_schedule_frames(server);
	drawer_compose(server);
	fullscreen_layer_order(server);
}

static void drawer_toggle(struct desk_server *server)
{
	/* pushed back partway, it goes back from where it has got to */
	drawer_go(server, server->drawer_t > 0.5 ? 0.0 : 1.0);
}

static void snip_begin(struct desk_server *server, bool tape);

/* How far up the desk is, with the drawer so far out. */
static double drawer_lift_at(const struct desk_server *server, double t)
{
	int ox0, oy0, ox1, oy1;
	double c0, c1;
	if (!desk_desk_place(server->desk_w, server->desk_h, 0.0, &ox0, &oy0, &c0)
		|| !desk_desk_place(server->desk_w, server->desk_h, t, &ox1, &oy1, &c1)) {
		return 0.0;
	}
	return fabs((double)oy1 - oy0);
}

/* And how far out, with the desk so far up. */
static double drawer_t_of_lift(const struct desk_server *server, double lift)
{
	double lo = 0.0, hi = 1.0;
	for (int i = 0; i < 14; i++) {
		double mid = (lo + hi) * 0.5;
		if (drawer_lift_at(server, mid) < lift) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	return (lo + hi) * 0.5;
}

/* What a drawer let go of does. It runs on, as fast as the finger had it going; what is in it pulls
 * it on to the end it was going to, against the runners; and at that end it meets the stop, and
 * comes back off it a little. In points a second, and a second. */
#define DRAWER_PULL_PT 2600.0
#define DRAWER_RUNNERS 2.2
#define DRAWER_COMES_BACK 0.24
#define DRAWER_COMES_BACK_MOST_PT 280.0
#define DRAWER_FASTEST_PT 2400.0

static void drawer_free_step(struct desk_server *server)
{
	double full = drawer_lift_at(server, 1.0);
	double now = monotonic_sec();
	double dt = clampf_d(now - server->drawer_at, 0.0, 0.05);
	server->drawer_at = now;
	bool out = server->drawer_to > 0.5;
	double pull = (out ? DRAWER_PULL_PT : -DRAWER_PULL_PT) * desk_ui_scale();
	double stop_at = out ? full : 0.0;
	int n = (int)ceil(dt * 480.0);
	double h = n > 0 ? dt / n : 0.0;
	bool there = false;
	for (int i = 0; i < n && !there; i++) {
		server->drawer_v += (pull - DRAWER_RUNNERS * server->drawer_v) * h;
		server->drawer_x += server->drawer_v * h;
		if (out ? server->drawer_x >= stop_at : server->drawer_x <= stop_at) {
			double hit = fabs(server->drawer_v);
			server->drawer_x = stop_at;
			/* (it is a heavy thing on runners: a hard shove does not throw it back further) */
			server->drawer_v = clampf_d(-server->drawer_v * DRAWER_COMES_BACK,
				-DRAWER_COMES_BACK_MOST_PT * desk_ui_scale(),
				DRAWER_COMES_BACK_MOST_PT * desk_ui_scale());
			if (!server->snd_drawer_stop) {
				/* at its stop, or home: as hard as it got there */
				desk_sound_stop(server->snd_drawer, 0.03);
				server->snd_drawer = 0;
				drawer_snd_end(server, 0.03);
				server->snd_drawer_stop = desk_sound_play(out ? DESK_SND_DRAWER_STOP_OUT
					: DESK_SND_DRAWER_STOP_IN, 0.0, fmin(0.35 + hit / 1400.0, 1.0), 0.0);
			}
			/* too slow to come back off it: stopped */
			there = hit < 60.0 * desk_ui_scale();
		}
		/* (the other end, going the wrong way: it stops at that too) */
		if (server->drawer_x < 0.0 || server->drawer_x > full) {
			server->drawer_x = clampf_d(server->drawer_x, 0.0, full);
			server->drawer_v = 0.0;
		}
	}
	if (there) {
		server->drawer_free = false;
		server->drawer_x = stop_at;
	}
	server->drawer_t = drawer_t_of_lift(server, server->drawer_x);
	/* (on its way: its runs as fast as it goes; once at its stop, what it does coming back off
	 * it is in the stop) */
	if (!server->snd_drawer_stop) {
		drawer_snd_motion(server);
	}
}

static void drawer_tick(struct desk_server *server)
{
	if (!server->drawer_moving) {
		return;
	}
	/* (on a finger, it is where the finger has put it) */
	if (server->drawer_held) {
		server->drw_dirty = true;
		drawer_compose(server);
		room_layout(server);
		overlay_place(server);
		return;
	}
	if (server->drawer_free) {
		drawer_free_step(server);
		room_layout(server);
		overlay_place(server);
		if (server->drawer_free) {
			server_schedule_frames(server);
			return;
		}
		/* (there: as it comes to its stop) */
		server->drawer_start = monotonic_sec() - server->drawer_dur;
	}
	double u = clampf_d((monotonic_sec() - server->drawer_start) / server->drawer_dur, 0, 1);
	server->drawer_t = server->drawer_from
		+ (server->drawer_to - server->drawer_from) * drawer_ease(u);
	/* at its stop, or home: the first time it gets there (the run has been cut to end there) */
	snd_end(&server->snd_drawer_stop, server->drawer_start,
		server->drawer_dur * drawer_ease_hit(),
		server->drawer_to > 0.5 ? DESK_SND_DRAWER_STOP_OUT : DESK_SND_DRAWER_STOP_IN,
		0.0, 1.0, 0.0);
	if (u >= 1.0) {
		server->drawer_t = server->drawer_to;
		server->drawer_moving = 0;
		server->snd_drawer = 0;
		/* home: a game on the whole screen is in front of it again */
		fullscreen_layer_order(server);
		/* and the camera taken out of it put up to the eye */
		if (server->snip_from_drawer && server->drawer_to < 0.5) {
			server->snip_from_drawer = false;
			snip_begin(server, false);
		}
	}
	/* Both sheets are baked, so a frame of the animation is two node
	 * moves -- and what lies on the desk moves with it rather than being
	 * drawn again. */
	room_layout(server);
	overlay_place(server);
	if (server->drawer_moving) {
		server_schedule_frames(server);
	}
}

static void sheet_begin_unfold(struct desk_server *server, struct desk_sheet *sheet)
{
	if (sheet->phase == SHEET_IDLE) {
		return;
	}
	sheet->passed = false;
	/* in the drawer: out of it, from where it lies there; in the hand:
	 * let go of first */
	if (sheet->in_drawer) {
		int i = drawer_index_of(server, sheet);
		if (i >= 0) {
			drawer_lift_sheet(server, i);
		}
	}
	drawer_forget(server, sheet);
	sheet->to_drawer = false;   /* turned back before it got there */
	double now = monotonic_sec();
	if (sheet->phase == SHEET_FOLDING) {
		double u = clampf_d((now - sheet->start) / sheet->dur, 0, 1);
		/* turned back: the presses and the landing asked for ahead will
		 * not come; it opens from the stage it has really got to */
		sheet_snd_forget(sheet);
		sheet->snd_k = desk_fold_stage(sheet->fold, u);
		sheet->phase = SHEET_UNFOLDING;
		sheet->dur = 0.95;
		sheet->start = now - (1.0 - u) * sheet->dur;
		server_schedule_frames(server);
		return;
	}
	if (sheet->phase != SHEET_PARKED) {
		return;
	}
	sheet->phase = SHEET_UNFOLDING;
	sheet->dur = 0.95;
	sheet->start = now;
	sheet_snd_forget(sheet);
	sheet->snd_k = desk_fold_stages(sheet->fold);
	/* picked up off the desk to be opened out */
	snd_at(server, DESK_SND_CORNER_LIFT, sheet->hit_x + sheet->hit_w * 0.5, 1.4);
	server_schedule_frames(server);
	sheet_tick_one(server, sheet);
}

static void drawer_remove(struct desk_server *server, int i, struct drawer_entry *out);
static void drawer_layout(struct desk_server *server, bool now);
static void things_say(struct desk_server *server);
