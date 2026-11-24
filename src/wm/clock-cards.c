/* Part of wm.c: the clock, the tray's slips and cards.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* Position the two room sheets for the current drawer position. Both are
 * baked; this only moves and scales nodes, so it is free per frame. */
static void room_layout(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	struct desk_room_sheets sh;
	if (w < 8 || h < 8 || !desk_room_sheets(w, h, &sh)) {
		return;
	}
	double zoom, pan, t;
	desk_room_view(server->drawer_t, &sh, &zoom, &pan, &t);
	int dw = (int)(w * zoom + 0.5), dh = (int)(h * zoom + 0.5);
	int ox = (w - dw) / 2, oy = (int)((h - dh) / 2 + pan * h);
	double scale = (double)dh / sh.h;

	/* The desk keeps its size and slides up out of the drawer's way. */
	int lx = server->desk_dx, ly = server->desk_dy;
	if (server->wallpaper) {
		desk_screen_lay(server, server->wallpaper,
			(struct wlr_fbox){ 0.0, 0.0, (double)sh.w, (double)sh.h }, ox, oy, dw, dh, lx, ly);
	}
	if (server->drawer_sheet) {
		double dy, clip;
		desk_room_drawer_offset(h, server->drawer_t, scale, oy,
			sh.dr_v0, sh.dr_v1, sh.h, &dy, &clip);
		/* Everything above the line is still inside the desk. */
		double ny = oy + dy;
		double cut = clampf_d((clip - ny) / fmax(scale, 1e-6), 0.0, (double)sh.h);
		struct wlr_fbox src = { 0.0, cut, (double)sh.w, (double)sh.h - cut };
		if (src.height < 1.0 || server->drawer_t <= 0.004) {
			wlr_scene_node_set_enabled(&server->drawer_sheet->node, false);
		} else {
			wlr_scene_node_set_enabled(&server->drawer_sheet->node, true);
			desk_screen_lay(server, server->drawer_sheet, src, ox,
				(int)(ny + cut * scale + 0.5), dw, (int)(src.height * scale + 0.5), lx, ly);
		}
		/* what lies in it goes with it, cut at the same line */
		if (server->drw_node) {
			const double *b = server->drw_box;
			double top = ny + b[1] * scale;
			double dcut = clampf_d((clip - top) / fmax(scale, 1e-6), 0.0, b[3]);
			if (server->drawer_t <= 0.004 || b[3] - dcut < 1.0) {
				wlr_scene_node_set_enabled(&server->drw_node->node, false);
			} else {
				double r = server->drw_res > 0.0 ? server->drw_res : 1.0;
				struct wlr_fbox isrc = { 0.0, dcut * r, b[2] * r, (b[3] - dcut) * r };
				wlr_scene_node_set_enabled(&server->drw_node->node, true);
				desk_screen_lay(server, server->drw_node, isrc,
					(int)(ox + b[0] * scale + 0.5), (int)(top + dcut * scale + 0.5),
					(int)(b[2] * scale + 0.5), (int)((b[3] - dcut) * scale + 0.5), lx, ly);
			}
		}
	}
}

/* Where everything over the desk goes for the drawer where it is now:
 * the sprites on the desk follow it by however far it has moved since they
 * were drawn -- a whole number of pixels, because the desk only ever
 * slides, so a moved sprite is exactly the sprite drawn there -- and the
 * shade is a flat colour for the GPU, not a picture to upload. */
static void overlay_place(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	int ox = 0, oy = 0;
	double clip = 0.0;
	if (!server->ovl_under || !desk_desk_place(w, h, server->drawer_t, &ox, &oy, &clip)) {
		return;
	}
	int dx = ox - server->ovl_ox, dy = oy - server->ovl_oy;
	for (int i = 0; i < DESK_OVL_COUNT; i++) {
		struct desk_sprite *sp = &server->ovl[i];
		if (!sp->node) {
			continue;
		}
		bool moves = desk_overlay_on_desk(i);
		wlr_scene_node_set_position(&sp->node->node,
			sp->x + (moves ? dx : 0), sp->y + (moves ? dy : 0));
	}
	float rgb[3];
	double a = desk_desk_shade(server->drawer_t, rgb);
	/* A rectangle ends on a whole pixel and the line it stands for does
	 * not, so the row the line falls in is a rectangle of its own, shaded
	 * by how much of it is above the line -- which is what cairo's edge
	 * did when this was painted. */
	double line = fmin(clip, (double)h);
	int full = line > 0.0 ? (int)floor(line) : 0;
	double part = line > 0.0 ? line - full : 0.0;
	bool on = a > 0.0 && line > 0.0;
	if (on && full > 0) {
		const float color[4] = { rgb[0] * (float)a, rgb[1] * (float)a,
			rgb[2] * (float)a, (float)a };
		wlr_scene_rect_set_color(server->shade, color);
		wlr_scene_rect_set_size(server->shade, w, full);
	}
	wlr_scene_node_set_enabled(&server->shade->node, on && full > 0);
	if (on && part > 0.002) {
		float ea = (float)(a * part);
		const float color[4] = { rgb[0] * ea, rgb[1] * ea, rgb[2] * ea, ea };
		wlr_scene_rect_set_color(server->shade_edge, color);
		wlr_scene_rect_set_size(server->shade_edge, w, 1);
		wlr_scene_node_set_position(&server->shade_edge->node, 0, full);
	}
	wlr_scene_node_set_enabled(&server->shade_edge->node, on && part > 0.002);
}

struct ovl_args {
	int which;
	time_t now;
	int unread;
};

static void ovl_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct ovl_args *a = data;
	desk_draw_overlay(cr, a->which, w, h, a->now, a->unread);
}

/* One thing over the desk, drawn again and cropped to what it covers. */
static void sprite_draw(struct desk_server *server, int i, time_t now, int unread)
{
	struct ovl_args a = { i, now, unread };
	double box[4];
	bool boxed = desk_overlay_box(i, server->desk_w, server->desk_h, box);
	sprite_paint(server, &server->ovl[i],
		desk_overlay_shaded(i) ? server->ovl_under : server->ovl_over,
		boxed ? box : NULL, ovl_draw, &a);
}

/* Everything over the desk that needs drawing again, drawn again. force
 * is for a new room or a new size, when every sprite is stale. */
static void overlay_update(struct desk_server *server, bool force)
{
	int w = server->desk_w, h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	if (!server->ovl_under) {
		server->ovl_under = wlr_scene_tree_create(server->layer_desk);
		server->layer_mat = wlr_scene_tree_create(server->layer_desk);
		const float none[4] = { 0, 0, 0, 0 };
		server->shade = wlr_scene_rect_create(server->layer_desk, 0, 0, none);
		wlr_scene_node_set_enabled(&server->shade->node, false);
		server->shade_edge = wlr_scene_rect_create(server->layer_desk, 0, 0, none);
		wlr_scene_node_set_enabled(&server->shade_edge->node, false);
		server->ovl_over = wlr_scene_tree_create(server->layer_desk);
	}
	int ox = 0, oy = 0;
	double clip;
	if (!desk_desk_place(w, h, 0.0, &ox, &oy, &clip)) {
		/* no room yet: nothing is placed, and the next paint forces */
		for (int i = 0; i < DESK_OVL_COUNT; i++) {
			server->ovl[i].drawn = false;
			if (server->ovl[i].node) {
				wlr_scene_node_set_enabled(&server->ovl[i].node->node, false);
			}
		}
		server->ovl_placed = false;
		return;
	}
	/* stale if the desk has moved, or if it is another room: a bake that
	 * has just landed, or the last one kept up while the next is made */
	struct desk_room_sheets sh;
	const void *room = desk_room_sheets(w, h, &sh) ? (const void *)sh.background : NULL;
	if (ox != server->ovl_ox || oy != server->ovl_oy || !server->ovl_placed
		|| room != server->ovl_room) {
		force = true;
	}
	server->ovl_room = room;
	server->ovl_ox = ox;
	server->ovl_oy = oy;
	server->ovl_placed = true;

	time_t now = time(NULL);
	int unread = post_unread();
	for (int i = 0; i < DESK_OVL_COUNT; i++) {
		uint64_t key = desk_overlay_key(i, now, unread);
		if (!force && server->ovl[i].drawn && key == server->ovl[i].key) {
			continue;
		}
		server->ovl[i].key = key;
		sprite_draw(server, i, now, unread);
	}
	/* the things on the desk are what the rest over it lies on, and the mat
	 * is what they lie on -- a clear one over what is under it -- whenever
	 * their pictures were first put up */
	static const int UNDER[] = { DESK_OVL_CASES, DESK_OVL_MAT_SHEET, DESK_OVL_MAT };
	for (size_t i = 0; i < sizeof UNDER / sizeof UNDER[0]; i++) {
		if (server->ovl[UNDER[i]].node) {
			wlr_scene_node_lower_to_bottom(&server->ovl[UNDER[i]].node->node);
		}
	}
	overlay_place(server);
}

static void desktop_paint_body(struct desk_server *server)
{
	int w = server->desk_w;
	int h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	/* the lamp what is drawn over the room is lit by: the room's own, as
	 * the room for this screen has it (the shadows follow on the next
	 * frame, paper_tick) */
	desk_light_desk(w, h);
	struct desk_room_sheets sh;
	if (desk_room_sheets(w, h, &sh)) {
		/* The room is one traced scene, delivered as two sheets. */
		struct desk_buf *bg = buf_create(sh.w, sh.h);
		cairo_set_source_surface(bg->cr, sh.background, 0, 0);
		cairo_paint(bg->cr);
		scene_put(&server->wallpaper, server->layer_bg, bg, 0, 0, NULL);

		struct desk_buf *dr = buf_create(sh.w, sh.h);
		cairo_set_source_surface(dr->cr, sh.drawer, 0, 0);
		cairo_paint(dr->cr);
		scene_put(&server->drawer_sheet, server->layer_drawer, dr, 0, 0, NULL);
		room_layout(server);
		/* and what lies in it, over it: a new room is a new felt */
		server->drw_dirty = true;
		drawer_compose(server);
	} else if (!desk_room_shown(w, h)) {
		/* No room at all yet, not even another size's: the bare boards,
		 * under the dark of the lamp not yet lit, drawn a quarter of the
		 * screen and stretched. Drawn whole they were the slowest thing on
		 * the way up -- 0.3 s here, three on a machine of 2010 -- for a
		 * picture that a room read back from the disk replaces a moment
		 * later. */
		int pw = w / 4 > 16 ? w / 4 : 16, ph = h / 4 > 16 ? h / 4 : 16;
		struct desk_buf *wp = buf_create(pw, ph);
		desk_draw_room_placeholder(wp->cr, pw, ph);
		scene_put(&server->wallpaper, server->layer_bg, wp, 0, 0, NULL);
		scene_source(server->wallpaper, NULL);
		wlr_scene_buffer_set_dest_size(server->wallpaper, w, h);
	} else {
		struct desk_buf *wp = buf_create(w, h);
		desk_draw_room(wp->cr, w, h, server->drawer_t);
		scene_put(&server->wallpaper, server->layer_bg, wp, 0, 0, NULL);
		scene_source(server->wallpaper, NULL);
		wlr_scene_buffer_set_dest_size(server->wallpaper, w, h);
	}

	/* The gadgets live on the desk; only what glows behind their glass is
	 * drawn over the bake -- and only what has changed, which after a new
	 * room or a new size is all of it. This is called four times a second
	 * while a room is baking, and the sprites have no business being drawn
	 * again for that. */
	overlay_update(server, false);

	console_dim_apply(server);
	/* and the bare desk beside it, cut from this room */
	side_desks_paint(server, false);
}

static void desktop_paint(struct desk_server *server)
{
	prof_begin(PROF_DESK);
	desktop_paint_body(server);
	prof_end();
	server->wait_room = desk_room_shown(server->desk_w, server->desk_h);
	server->wait_w = server->desk_w;
	server->wait_h = server->desk_h;
	server->wait_drawer = server->drawer_t;
}

/* whether the desk painted while the room is traced would come out any
 * different now (wait_*) */
static bool desktop_waiting_stale(struct desk_server *server)
{
	return server->wait_w != server->desk_w || server->wait_h != server->desk_h
		|| server->wait_drawer != server->drawer_t
		|| server->wait_room != desk_room_shown(server->desk_w, server->desk_h);
}

static void desktop_tick_body(struct desk_server *server)
{
	int w = server->desk_w;
	int h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	/* The room is baked; only the glowing faces change with the clock, and
	 * only the ones whose faces have actually changed are drawn. */
	overlay_update(server, false);
}

static void desktop_tick(struct desk_server *server)
{
	prof_begin(PROF_DESK);
	desktop_tick_body(server);
	prof_end();
}

#ifdef DESK_HAVE_POST
/* Something arrived, or something was taken out. Redraw whatever of the
 * post box is showing. */
static void post_changed(void *data)
{
	struct desk_server *server = data;
	const struct desk_note *notes;
	int n = desk_post_letters(&notes);
	wlr_log(WLR_INFO, "post: %d letter%s, %d unread%s", n, n == 1 ? "" : "s",
		desk_post_unread(), n ? "" : " (box empty)");
	/* one that has not been here before: heard coming through the slot */
	unsigned newest = 0;
	for (int i = 0; i < n; i++) {
		newest = notes[i].id > newest ? notes[i].id : newest;
	}
	if (server->post_seen_set && newest > server->post_seen && server->conf.letter_sound) {
		desk_sound_play(DESK_SND_LETTER_ARRIVE, 0.55, 1.0, 0.0);
	}
	if (!server->post_seen_set || newest > server->post_seen) {
		server->post_seen = newest;
	}
	server->post_seen_set = true;
	/* With the box open, the letters standing in it are what is in it
	 * now: never turned past the last of them, the hand over whichever is
	 * under it now, and drawn again. */
	if (post_up(server)) {
		const struct desk_note *in = NULL;
		int k = post_count(&in);
		double top = k > 0 ? k - 1 : 0;
		if (server->pb_pos_to > top) {
			server->pb_pos_to = top;
		}
		if (server->pb_pos > top) {
			server->pb_pos = top;
			server->snd_letter = (int)lround(server->pb_pos);
		}
		server->pb_hover = server->pb_t >= 0.9995
			? desk_post_hit(server->desk_w, server->desk_h, k, server->pb_pos,
				server->cursor->x, server->cursor->y)
			: -1;
		paint_post(server);
	}
	desktop_tick(server);
	server_schedule_frames(server);
}
#endif

/* The desk is a different size now. Everything that was built for the old
 * one goes, the room starts baking again for the new one, and until it
 * lands the desk paints flat -- the same as a first run, because that is
 * what it is. */
static void desk_relayout(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	wlr_log(WLR_INFO, "desk is %dx%d now; rebuilding", w, h);
	book_kept_forget(server);

	/* the traced moves, which are one strip of frames at one size */
	for (int i = 0; i < DESK_VIEW_COUNT; i++) {
		if (server->film_a[i]) {
			wlr_scene_node_destroy(&server->film_a[i]->node);
			server->film_a[i] = NULL;
		}
		if (server->film_b[i]) {
			wlr_scene_node_destroy(&server->film_b[i]->node);
			server->film_b[i] = NULL;
		}
		server->film_frames[i] = 0;
	}
	/* the traced sheets behind the live overlays: their own size guards
	 * will notice, but drop them here so nothing is drawn at the old size
	 * even once */
	if (server->cc_base) {
		wlr_scene_node_destroy(&server->cc_base->node);
		server->cc_base = NULL;
		server->cc_base_w = server->cc_base_h = 0;
	}
	if (server->pb_base) {
		wlr_scene_node_destroy(&server->pb_base->node);
		server->pb_base = NULL;
		server->pb_base_w = server->pb_base_h = 0;
	}

	/* the letters lying on the desk move with the desk */
	papers_place(server, w, h);
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->screen_changed) {
			(*t)->screen_changed(server);
		}
	}

	server->icons_shown = false;
	desk_scene_warm(w, h, true);
	desktop_paint(server);
	paint_shelf(server);
	paint_console(server);
	paint_post(server);
	windows_keep_off_tray(server);
	server_schedule_frames(server);
}

static int resize_cb(void *data)
{
	struct desk_server *server = data;
	if (server->resize_w == server->desk_w && server->resize_h == server->desk_h) {
		return 0;
	}
	server->desk_w = server->resize_w;
	server->desk_h = server->resize_h;
	g_motion_fine = desk_px_per_pt(server->desk_w);
	desk_relayout(server);
	return 0;
}

/* Called for every size the backend offers. Only the one it settles on
 * ever reaches the ray tracer. */
static void desk_resize_to(struct desk_server *server, int w, int h)
{
	if (w < 8 || h < 8) {
		return;
	}
	server->resize_w = w;
	server->resize_h = h;
	if (w == server->desk_w && h == server->desk_h) {
		return;
	}
	if (server->resize_timer) {
		wl_event_source_timer_update(server->resize_timer, 350);
	}
}

/* What the first look at a view would otherwise stop to make: the case
 * with its labels printed, the console's traced sheet, the post box's, and
 * the three films of the camera moves. A game loads its assets behind the
 * title screen; this makes them on the idle ticks after the room is up,
 * one a tick so that no single tick is the long one, and leaves them
 * hidden until they are looked at. Each view's as soon as that view is
 * there: on a machine that traces on its processor the bookcase is
 * minutes ahead of the other two. */
static void views_prewarm(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (w != server->warm_w || h != server->warm_h) {
		server->warm_w = w;
		server->warm_h = h;
		server->warm_step = 0;
	}
	if (!server->icons_shown || shelf_up(server) || console_up(server) || post_up(server)) {
		return;
	}
	if (server->warm_step > 7) {
		/* The films are traced after the views (draw.c), and may come long
		 * after the steps below went past them: each is taken up on the
		 * tick after it comes, not on the first move to its view. */
		struct wlr_scene_tree *const layer[DESK_VIEW_COUNT] = {
			[DESK_VIEW_SHELF] = server->layer_shelf,
			[DESK_VIEW_CONSOLE] = server->layer_console,
			[DESK_VIEW_POST] = server->layer_post,
		};
		for (int v = 0; v < DESK_VIEW_COUNT; v++) {
			if (!server->film_a[v]) {
				film_ensure(server, v, layer[v]);
				if (server->film_a[v]) {
					return;   /* one a tick */
				}
			}
		}
		return;
	}
	/* a view at a time, the bookcase first, each waiting for its view to
	 * be there; and each view's film before its sheet, the order a first
	 * look makes them in, which is the order they stack in */
	static const signed char NEEDS[8] = { DESK_VIEW_SHELF, DESK_VIEW_SHELF, DESK_VIEW_SHELF,
		DESK_VIEW_CONSOLE, DESK_VIEW_CONSOLE, DESK_VIEW_POST, DESK_VIEW_POST, -1 };
	int need = NEEDS[server->warm_step < 0 ? 0 : server->warm_step];
	if (need >= 0 && !desk_scene_view_ready(w, h, need)) {
		return;
	}
	switch (server->warm_step) {
	case 0:
		shelf_dress_today();
		if (!desk_shelf_prewarm(w, h)) {
			return;
		}
		break;
	case 1:
		film_ensure(server, DESK_VIEW_SHELF, server->layer_shelf);
		break;
	case 2: {
		/* and the case itself, as the shelf view will first put it up */
		if (!server->sh_live) {
			server->sh_live = wlr_scene_tree_create(server->layer_shelf);
			for (int k = 0; k < SH_LAYERS; k++) {
				server->sh_layer[k] = wlr_scene_tree_create(server->sh_live);
			}
			wlr_scene_node_set_enabled(&server->sh_live->node, false);
		}
		const struct desk_app_list *l = desk_apps_acquire();
		unsigned gen = l->generation;
		desk_apps_release(l);
		struct sh_args a = { server, -1, -1, -1, 0, 0.0, NULL, time(NULL) };
		unsigned dc = shelf_dress_today();
		uint64_t key = key_add(14695981039346656037ull, &gen, sizeof gen);
		key = key_add(key, &a.taken, sizeof a.taken);
		key = key_add(key, &dc, sizeof dc);
		double full[4] = { 0.0, 0.0, (double)w, (double)h };
		unsigned room = desk_view_room(w, h, DESK_VIEW_SHELF);
		bool force = server->sh_room != room;
		server->sh_w = w;
		server->sh_h = h;
		server->sh_room = room;
		sh_put(server, SH_CASE, key, force, full, sh_case_draw, &a);
		break;
	}
	case 3:
		film_ensure(server, DESK_VIEW_CONSOLE, server->layer_console);
		break;
	case 4: {
		/* noted with its room, as paint_console_body notes it, or the
		 * first look at the set draws it all over again */
		unsigned room = desk_console_room(w, h);
		if (!server->cc_base || server->cc_base_w != w || server->cc_base_h != h
			|| server->cc_base_room != room) {
			struct desk_buf *bb = buf_create(w, h);
			desk_draw_console_base(bb->cr, w, h);
			scene_put(&server->cc_base, server->layer_console, bb, 0, 0, NULL);
			wlr_scene_node_set_enabled(&server->cc_base->node, false);
			server->cc_base_w = w;
			server->cc_base_h = h;
			server->cc_base_room = room;
			view_bases_under(server);
		}
		break;
	}
	case 5:
		film_ensure(server, DESK_VIEW_POST, server->layer_post);
		break;
	case 6:
		unsigned room = desk_view_room(w, h, DESK_VIEW_POST);
		if (!server->pb_base || server->pb_base_w != w || server->pb_base_h != h
			|| server->pb_base_room != room) {
			struct desk_buf *bb = buf_create(w, h);
			desk_draw_post(bb->cr, w, h, NULL, 0, -1, 0.0, 0.0, NULL);
			scene_put(&server->pb_base, server->layer_post, bb, 0, 0, NULL);
			wlr_scene_node_set_enabled(&server->pb_base->node, false);
			server->pb_base_w = w;
			server->pb_base_h = h;
			server->pb_base_room = room;
			view_bases_under(server);
		}
		break;
	default:
		break;
	}
	server->warm_step++;
}

/* ---- the clock ------------------------------------------------------
 *
 * Four keys beside its display, CLOCK and DAY over MEDIA and PC. A
 * key shows its page, and it stays up until a key is pressed: its own
 * again, or CLOCK, goes back to the time (owner 11/10: not of itself). The
 * keys light as a hand comes near, and the one under it more. */

static void clock_repaint(struct desk_server *server)
{
	struct desk_sprite *sp = &server->ovl[DESK_OVL_CLOCK];
	if (!server->ovl_placed) {
		return;
	}
	time_t now = time(NULL);
	uint64_t key = desk_clock_key(now);
	if (sp->drawn && sp->key == key) {
		return;
	}
	sp->key = key;
	sprite_draw(server, DESK_OVL_CLOCK, now, post_unread());
	overlay_place(server);
}

/* Everything the face shows, handed to the painter, and painted if it
 * comes out different. */
static void clock_publish(struct desk_server *server)
{
	struct desk_clock_state st;
	memset(&st, 0, sizeof st);
	st.mode = server->clk.mode;
	st.wake = server->clk.wake;
	for (int i = 0; i < DESK_CLOCK_KEYS; i++) {
		st.glow[i] = server->clk.glow[i];
	}
	st.pressed = server->clk.pressed;
	struct desk_meters m;
	memset(&m, 0, sizeof m);
	desk_meters_read_volume(&m);
	st.have_volume = m.have_volume;
	st.volume = m.volume > 1.0 ? 1.0 : m.volume;
	st.muted = m.muted;
	st.supply = server->clk.supply;
	st.pc = server->clk.pc;
	st.pc_rates = server->clk.pc_reads >= 2;
	st.avg_w = server->clk.n_all > 0 ? server->clk.sum_all / server->clk.n_all : -1.0;
	st.hour_wh = -1.0;
	if (server->clk.hist_n > 0) {
		double sum = 0.0;
		for (int i = 0; i < server->clk.hist_n; i++) {
			sum += server->clk.hist[i];
		}
		/* a second at a time: watt seconds, in watt hours */
		st.hour_wh = sum / 3600.0;
	}
	st.now = monotonic_sec();
	st.boot = server->boot_on ? server->boot_p : server->relight == RELIGHT_DARK ? server->relight_p
		: -1.0;
	st.boot_stage = server->boot_on ? server->boot_stage : 0;   /* (another wood: the desk) */
	st.media_hot = server->clk.media_hot;
	st.media_down = server->clk.media_down;
	st.camera = desk_camera_state();
	st.mic = desk_mic_state();
	st.rec = server->rec ? server->rec_shown : -1;
	st.show = server->show;
	st.show_t = server->show ? real_sec() - server->show_t0 : 0.0;
	{
		time_t ot = desk_occasion_now();
		struct tm otm;
		localtime_r(&ot, &otm);
		st.year = otm.tm_year + 1900;
	}
#ifdef DESK_HAVE_POST
	/* the input method is shown in the tray (its own item), not here */
	st.ime[0] = '\0';
	desk_media_read(&st.media, st.now);
#endif
	desk_clock_set(&st);
	clock_repaint(server);
	/* and back when it will next come out different by itself */
	if (server->clk.timer) {
		int ms = desk_clock_next_ms();
		wl_event_source_timer_update(server->clk.timer, ms < 0 ? 0 : ms > 0 ? ms : 1);
	}
}

static int clock_timer_cb(void *data)
{
	clock_publish(data);
	return 0;
}

static void clock_changed_cb(void *data)
{
	clock_publish(data);
}

/* the weather has come in: the door out in it */
static void occasion_changed_cb(void *data)
{
	struct desk_server *server = data;
	if (locked(server)) {
		paint_lock(server);
		server_schedule_frames(server);
	}
}

static void clock_animate(struct desk_server *server)
{
	if (!server->clk.animating) {
		server->clk.animating = true;
		server->clk.anim_at = monotonic_sec();
	}
	server_schedule_frames(server);
}

/* Once a frame while the light is still changing: the keys brighten
 * quickly and fade a little slower, the way a lamp does. */
static void clock_tick_frame(struct desk_server *server)
{
	if (!server->clk.animating) {
		return;
	}
	double now = monotonic_sec();
	double dt = clampf_d(now - server->clk.anim_at, 0.0, 0.05);
	server->clk.anim_at = now;
	bool moving = false;
	double *v = &server->clk.wake, to = server->clk.wake_to;
	double k = gfx_flat() ? 1.0 : 1.0 - exp(-dt / (to > *v ? 0.07 : 0.22));
	*v += (to - *v) * k;
	if (fabs(to - *v) < 0.01) {
		*v = to;
	} else {
		moving = true;
	}
	for (int i = 0; i < DESK_CLOCK_KEYS; i++) {
		double target = server->clk.hot == i ? 1.0 : 0.0;
		double *g = &server->clk.glow[i];
		double kg = gfx_flat() ? 1.0 : 1.0 - exp(-dt / (target > *g ? 0.05 : 0.18));
		*g += (target - *g) * kg;
		if (fabs(target - *g) < 0.01) {
			*g = target;
		} else {
			moving = true;
		}
	}
	clock_publish(server);
	if (moving) {
		server_schedule_frames(server);
	} else {
		server->clk.animating = false;
	}
}

static void clock_set_mode(struct desk_server *server, int mode)
{
	if (server->clk.mode == mode) {
		return;
	}
	server->clk.mode = mode;
	if (mode == DESK_CLOCK_PC) {
		/* the counters now, for the next look's rates to go from */
		desk_meters_read(&server->clk.pc);
		server->clk.pc_reads = 1;
	}
	clock_publish(server);
}

static void clock_set_volume(struct desk_server *server, double v)
{
	v = clampf_d(v, 0.0, 1.0);
	if (desk_meters_set_volume(v)) {
		/* the console's knob is the same mixer */
		server->cc_vol = v;
		clock_publish(server);
	}
}

/* Where the pointer is, for the clock: how lit its keys are, and which one
 * is under it. True when it is over the clock. */
static bool clock_track(struct desk_server *server, bool can_touch)
{
	struct desk_clock_pick pk;
	bool have = can_touch && desk_clock_pick(server->desk_w, server->desk_h,
		server->cursor->x, server->cursor->y, server->drawer_t, &pk);
	if (!have) {
		memset(&pk, 0, sizeof pk);
		pk.key = -1;
		pk.button = -1;
	}
	if (pk.near != server->clk.near) {
		server->clk.near = pk.near;
		server->clk.wake_to = pk.near ? 1.0 : 0.0;
		clock_animate(server);
	}
	if (pk.key != server->clk.hot) {
		server->clk.hot = pk.key;
		clock_animate(server);
	}
	int button = pk.button == DESK_MEDIA_RAISE ? -1 : pk.button;
	if (button != server->clk.media_hot) {
		server->clk.media_hot = button;
		clock_publish(server);
	}
	return pk.on;
}

static void rec_end(struct desk_server *server);

static void clock_press(struct desk_server *server, uint32_t button, bool *taken)
{
	*taken = false;
	struct desk_clock_pick pk;
	if (!desk_clock_pick(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
			server->drawer_t, &pk) || !pk.on) {
		return;
	}
	*taken = true;
	/* a film being made: its time is on the face, and the face stops it */
	if (server->rec && pk.key < 0 && server->clk.mode == DESK_CLOCK_TIME && button == BTN_LEFT) {
		rec_end(server);
		return;
	}
	if (pk.key >= 0) {
		if (button == BTN_LEFT) {
			server->clk.pressed = pk.key;
			snd_at(server, DESK_SND_KEY_PRESS, server->cursor->x, 1.0);
			clock_publish(server);
		}
		return;
	}
	if (server->clk.mode == DESK_CLOCK_MEDIA && button == BTN_LEFT) {
		if (pk.button >= 0) {
			/* it acts when let go over itself, as a key does */
			snd_at(server, DESK_SND_KEY_PRESS, server->cursor->x, 0.8);
			server->clk.media_down = pk.button;
			clock_publish(server);
		} else if (pk.speaker) {
			if (desk_meters_set_muted(2)) {
				clock_publish(server);
			}
		} else if (pk.bar) {
			server->clk.vol_drag = true;
			clock_set_volume(server, pk.bar_value);
		}
	}
}

/* A button let go: a key held and let go over itself turns its page up,
 * or back to the time if its page is up already. */
static bool clock_release(struct desk_server *server)
{
	if (server->clk.vol_drag) {
		server->clk.vol_drag = false;
		return true;
	}
	if (server->clk.media_down >= 0) {
		int which = server->clk.media_down;
		server->clk.media_down = -1;
		snd_at(server, DESK_SND_KEY_RELEASE, server->cursor->x, 0.8);
		struct desk_clock_pick pk;
		if (desk_clock_pick(server->desk_w, server->desk_h, server->cursor->x,
				server->cursor->y, server->drawer_t, &pk) && pk.button == which) {
#ifdef DESK_HAVE_POST
			desk_media_command(which);
#endif
		}
		clock_publish(server);
		return true;
	}
	if (server->clk.pressed < 0) {
		return false;
	}
	int key = server->clk.pressed;
	server->clk.pressed = -1;
	snd_at(server, DESK_SND_KEY_RELEASE, server->cursor->x, 1.0);
	struct desk_clock_pick pk;
	if (desk_clock_pick(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
			server->drawer_t, &pk) && pk.key == key) {
		int page = desk_clock_key_mode(key);
		clock_set_mode(server, server->clk.mode == page ? DESK_CLOCK_TIME : page);
	}
	clock_publish(server);
	return true;
}

static bool clock_drag(struct desk_server *server)
{
	if (!server->clk.vol_drag) {
		return false;
	}
	double v;
	if (desk_clock_bar_value(server->desk_w, server->desk_h, server->cursor->x,
			server->cursor->y, server->drawer_t, &v)) {
		clock_set_volume(server, v);
	}
	return true;
}

/* A wheel over the clock: the volume, on the time and media pages. */
static bool clock_axis(struct desk_server *server, const struct wlr_pointer_axis_event *event)
{
	struct desk_clock_pick pk;
	if (!desk_clock_pick(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
			server->drawer_t, &pk) || !pk.on) {
		return false;
	}
	double d = event->delta;
	if (d == 0.0 && event->delta_discrete != 0) {
		d = (double)event->delta_discrete / 120.0 * 15.0;
	}
	if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL
		|| (server->clk.mode != DESK_CLOCK_TIME && server->clk.mode != DESK_CLOCK_MEDIA)) {
		return true;
	}
	server->clk.vol_scroll += d;
	int notches = 0;
	while (server->clk.vol_scroll >= 15.0) {
		notches--;
		server->clk.vol_scroll -= 15.0;
	}
	while (server->clk.vol_scroll <= -15.0) {
		notches++;
		server->clk.vol_scroll += 15.0;
	}
	if (notches) {
		struct desk_meters m;
		memset(&m, 0, sizeof m);
		desk_meters_read_volume(&m);
		if (m.have_volume) {
			clock_set_volume(server, round((m.volume + notches * 0.02) * 50.0) / 50.0);
		}
	}
	return true;
}

/* Once a tick: what the machine is drawing, the input method, the tray
 * and what is playing. The tick comes ten times a second while the shelf is up; the
 * record of what was drawn is a second at a time all the same. */
static void clock_tick(struct desk_server *server)
{
	struct desk_supply *s = &server->clk.supply;
	/* a look a second, taken on a thread of its own: each new one is a
	 * second of the hour's draw */
	unsigned looks = desk_supply_latest(s);
	if (looks != server->clk.supply_seen) {
		server->clk.supply_seen = looks;
		if (s->draw_w >= 0.0) {
			server->clk.hist[server->clk.hist_at] = (float)s->draw_w;
			server->clk.hist_at = (server->clk.hist_at + 1) % 3600;
			if (server->clk.hist_n < 3600) {
				server->clk.hist_n++;
			}
			server->clk.sum_all += s->draw_w;
			server->clk.n_all++;
		}
		if (server->clk.mode == DESK_CLOCK_PC) {
			desk_meters_read(&server->clk.pc);
			server->clk.pc_reads++;
		}
	}
#ifdef DESK_HAVE_POST
	desk_ime_tick();
	desk_tray_tick();
	desk_media_tick();
#endif
	things_say(server);
	desk_occasion_tick();
	/* programs installed or taken away while the desk is up: read in
	 * again, but not under the hand of somebody at the bookcase */
	char why[600];
	if (!shelf_up(server) && desk_apps_look(why, sizeof why)) {
		wlr_log(WLR_INFO, "programs: %s; reading them again", why);
	}
	show_midnight_check(server);
	clock_publish(server);
}

/* ---- the tray: slips under the Control Panel -------------------------
 *
 * Each thing a program has put in the tray is a slip tucked under the
 * front of the Control Panel, and the one under the pointer slides out a
 * little to meet it. A click is the program's own (Activate: most bring
 * their window up), the middle button its second thing; the right one
 * pulls the slip out, and the card it is stuck on comes out with it, with
 * the program's menu on it. A program that is nothing but a menu has its
 * card come out whichever button it is. */

#ifdef DESK_HAVE_POST

/* the one thing on the card of a program that has no menu of its own */
#define CARD_ACTIVATE INT32_MIN

static double *slip_lift(struct desk_server *server, unsigned id, bool make)
{
	for (int i = 0; i < server->slp.n_lift; i++) {
		if (server->slp.lift_id[i] == id) {
			return &server->slp.lift[i];
		}
	}
	if (!make) {
		return NULL;
	}
	/* a free place, or the place of one that is back where it lies */
	int at = server->slp.n_lift < DESK_SLIPS_MAX ? server->slp.n_lift++ : -1;
	for (int i = 0; at < 0 && i < server->slp.n_lift; i++) {
		if (server->slp.lift[i] <= 0.0) {
			at = i;
		}
	}
	if (at < 0) {
		return NULL;
	}
	server->slp.lift_id[at] = id;
	server->slp.lift[at] = 0.0;
	return &server->slp.lift[at];
}

/* How a slip happens to lie and what paper it is: its Id's, so it lies the
 * same way every time the program puts it there. */
static unsigned slip_seed(const struct desk_tray_item *it)
{
	unsigned h = 2166136261u;
	for (const char *p = it->name_id; *p; p++) {
		h = (h ^ (unsigned char)*p) * 16777619u;
	}
	return h;
}

static int slip_index(unsigned id)
{
	const struct desk_tray_item *items = NULL;
	int n = desk_tray_items(&items);
	for (int i = 0; i < n && i < DESK_SLIPS_MAX; i++) {
		if (items[i].id == id) {
			return i;
		}
	}
	return -1;
}

static void card_paint(struct desk_server *server);

/* The slips as the tray has them, handed to the painter, and painted if
 * they come out different. */
/* The first line of s, as much of it as fits, cut between characters: a
 * slip has room for a line, and a character cut in half is a string cairo
 * refuses -- and refuses everything drawn after it on the same card. */
static void utf8_line(char *out, size_t n, const char *s)
{
	size_t i = 0;
	if (n == 0) {
		return;
	}
	while (s && s[i] && s[i] != '\n' && s[i] != '\r' && i + 1 < n) {
		i++;
	}
	if (s && s[i] && s[i] != '\n' && s[i] != '\r') {
		while (i > 0 && ((unsigned char)s[i] & 0xc0) == 0x80) {
			i--;
		}
	}
	if (s) {
		memcpy(out, s, i);
	}
	out[i] = '\0';
	while (i > 0 && out[i - 1] == ' ') {
		out[--i] = '\0';
	}
}

static void slips_publish(struct desk_server *server)
{
	const struct desk_tray_item *items = NULL;
	int n = desk_tray_items(&items);
	struct desk_slips_state st;
	memset(&st, 0, sizeof st);
	st.hot = st.pulled = -1;
	for (int i = 0; i < n && st.n < DESK_SLIPS_MAX; i++) {
		struct desk_slip *sl = &st.slips[st.n];
		sl->icon = items[i].icon;
		utf8_line(sl->title, sizeof sl->title, items[i].title);
		sl->seed = slip_seed(&items[i]);
		sl->attention = items[i].attention;
		sl->passive = items[i].passive;
		const double *lift = slip_lift(server, items[i].id, false);
		sl->lift = lift ? *lift : 0.0;
		if (items[i].id == server->slp.hot) {
			st.hot = st.n;
		}
		if (items[i].id == server->slp.card) {
			st.pulled = st.n;
		}
		st.n++;
	}
	/* never slid further than there are slips to show */
	int fit = desk_slips_fit(server->desk_w, server->desk_h);
	double most = fit > 0 && st.n > fit ? (double)(st.n - fit) : 0.0;
	server->slp.scroll_to = clampf_d(server->slp.scroll_to, 0.0, most);
	server->slp.scroll = clampf_d(server->slp.scroll, 0.0, most);
	st.scroll = server->slp.scroll;
	/* the tray's band is deeper with slips in it than without */
	static int had = -1;
	bool changed_band = had >= 0 && (had > 0) != (st.n > 0);
	had = st.n;
	desk_slips_set(&st);
	if (changed_band) {
		windows_keep_off_tray(server);
	}
	struct desk_sprite *sp = &server->ovl[DESK_OVL_SLIPS];
	if (server->ovl_placed) {
		uint64_t key = desk_slips_key();
		if (!sp->drawn || sp->key != key) {
			sp->key = key;
			sprite_draw(server, DESK_OVL_SLIPS, time(NULL), post_unread());
			overlay_place(server);
		}
	}
	card_paint(server);
}

/* What is on the card now: the entries of whichever submenu it has been
 * taken down to -- or, for a program with no menu of its own, the one
 * thing any of them can be asked to do. */
static bool card_build(struct desk_server *server, struct desk_card *c)
{
	memset(c, 0, sizeof *c);
	c->hot = -1;
	c->slip = slip_index(server->slp.card);
	if (c->slip < 0) {
		return false;
	}
	const struct desk_tray_item *items = NULL;
	desk_tray_items(&items);
	utf8_line(c->title, sizeof c->title, items[c->slip].title);
	int depth = server->slp.depth;
	if (depth > 0) {
		c->back = true;
		utf8_line(c->title, sizeof c->title, server->slp.path_label[depth - 1]);
	}
	const struct desk_tray_menu_entry *e = NULL;
	int state = DESK_TRAY_MENU_NONE;
	int n = desk_tray_menu(depth > 0 ? server->slp.path[depth - 1] : 0, &e, &state);
	if (state == DESK_TRAY_MENU_LOADING) {
		c->loading = true;
	} else if (state == DESK_TRAY_MENU_NONE) {
		c->n = 1;
		c->e[0].id = CARD_ACTIVATE;
		snprintf(c->e[0].label, sizeof c->e[0].label, "%s", TR("Open"));
		c->e[0].enabled = true;
		c->e[0].state = -1;
	} else {
		for (int i = 0; i < n && c->n < DESK_CARD_MAX; i++) {
			struct desk_card_entry *ce = &c->e[c->n++];
			ce->id = e[i].id;
			memcpy(ce->label, e[i].label, sizeof ce->label);
			ce->enabled = e[i].enabled;
			ce->separator = e[i].separator;
			ce->submenu = e[i].submenu;
			ce->toggle = e[i].toggle;
			ce->state = e[i].state;
			ce->icon = e[i].icon;
		}
	}
	c->hot = server->slp.hot_entry < c->n ? server->slp.hot_entry : -1;
	c->top = server->slp.top < c->n ? server->slp.top : 0;
	return true;
}

static uint64_t card_key(const struct desk_server *server, const struct desk_card *c)
{
	uint64_t h = key_add(14695981039346656037ull, &c->slip, sizeof c->slip);
	int top[8] = { c->n, c->hot, c->top, c->back, c->loading,
		(int)lround(server->slp.pull * 2000.0), server->desk_w, server->desk_h };
	h = key_add(h, top, sizeof top);
	h = key_add(h, c->title, strlen(c->title));
	for (int i = 0; i < c->n; i++) {
		const struct desk_card_entry *e = &c->e[i];
		int f[6] = { e->id, e->enabled, e->separator, e->submenu, e->toggle, e->state };
		h = key_add(h, f, sizeof f);
		h = key_add(h, e->label, strlen(e->label));
		uintptr_t pi = (uintptr_t)e->icon;
		h = key_add(h, &pi, sizeof pi);
	}
	uint64_t slips = desk_slips_key();
	h = key_add(h, &slips, sizeof slips);
	double t = server->drawer_t;
	return key_add(h, &t, sizeof t);
}

/* The card as it is now, where it is now. */
static void card_paint(struct desk_server *server)
{
	if (!server->slp.card) {
		if (server->slp.node) {
			wlr_scene_node_set_enabled(&server->slp.node->node, false);
		}
		return;
	}
	struct desk_card c;
	struct desk_card_place pl;
	if (!card_build(server, &c) || !desk_card_place(server->desk_w, server->desk_h,
			server->drawer_t, &c, &pl)) {
		card_gone(server);
		return;
	}
	uint64_t key = card_key(server, &c);
	if (server->slp.node && server->slp.node->node.enabled && server->slp.key == key) {
		return;
	}
	server->slp.key = key;
	double box[4];
	desk_card_box(&pl, server->slp.pull, box);
	struct desk_buf *buf = buf_create((int)box[2], (int)box[3]);
	cairo_translate(buf->cr, -box[0], -box[1]);
	desk_draw_card(buf->cr, server->desk_w, server->desk_h, server->drawer_t, &c, &pl,
		server->slp.pull);
	/* over the sheets on the desk: it has been pulled out to be read */
	scene_put(&server->slp.node, server->layer_parked, buf, (int)box[0], (int)box[1], NULL);
	server->slp.node->point_accepts_input = shadow_accepts;
	wlr_scene_node_raise_to_top(&server->slp.node->node);
	wlr_scene_node_set_enabled(&server->slp.node->node, true);
}

static void card_pull(struct desk_server *server, double to)
{
	desk_sound_play(DESK_SND_CARD_SLIDE, -0.45, to > 0.5 ? 1.0 : 0.7, to > 0.5 ? 0.0 : 1.2);
	server->slp.pull_from = server->slp.pull;
	server->slp.pull_to = to;
	server->slp.pull_start = monotonic_sec();
	server->slp.pulling = true;
	server_schedule_frames(server);
}

static void card_open(struct desk_server *server, unsigned id)
{
	if (server->slp.card == id) {
		if (server->slp.pull_to < 0.5) {
			card_pull(server, 1.0);
		}
		return;
	}
	card_gone(server);
	server->slp.card = id;
	server->slp.depth = 0;
	server->slp.hot_entry = -1;
	server->slp.top = 0;
	server->slp.card_scroll = 0.0;
	server->slp.pull = 0.0;
	desk_tray_menu_open(id);
	card_pull(server, 1.0);
	slips_publish(server);
}

/* Slid back in under the machine; what the program is told is told when
 * it is out of sight, so what is on it does not change on the way. */
static void card_put_back(struct desk_server *server)
{
	if (server->slp.card && server->slp.pull_to > 0.5) {
		card_pull(server, 0.0);
	}
}

/* Gone at once: the view is changing, or the program has gone. */
static void card_gone(struct desk_server *server)
{
	if (!server->slp.card) {
		return;
	}
	desk_tray_menu_close();
	server->slp.card = 0;
	server->slp.pull = server->slp.pull_to = 0.0;
	server->slp.pulling = false;
	if (server->slp.node) {
		wlr_scene_node_set_enabled(&server->slp.node->node, false);
	}
	slips_publish(server);
}

static bool card_out(const struct desk_server *server)
{
	return server->slp.card && server->slp.pull_to > 0.5;
}

/* Where the card is under the pointer: an entry, -2 its head, -1 the card
 * but nothing on it, -3 not the card. */
static int card_under(struct desk_server *server, struct desk_card *c)
{
	struct desk_card_place pl;
	if (!card_build(server, c) || !desk_card_place(server->desk_w, server->desk_h,
			server->drawer_t, c, &pl)) {
		return -3;
	}
	return desk_card_hit(c, &pl, server->cursor->x, server->cursor->y);
}

static void card_hot(struct desk_server *server, int hot)
{
	if (hot != server->slp.hot_entry) {
		server->slp.hot_entry = hot;
		card_paint(server);
	}
}

/* An entry chosen: a submenu is gone down into, anything else is done and
 * the card put back. */
static void card_choose(struct desk_server *server, const struct desk_card *c, int i)
{
	if (i == -2) {
		if (server->slp.depth > 0) {
			server->slp.depth--;
			server->slp.hot_entry = -1;
			server->slp.top = 0;
			card_paint(server);
		} else {
			/* the program's own name: it comes, as it does for a click */
			desk_tray_click(server->slp.card, 0, (int)server->cursor->x,
				(int)server->cursor->y);
			card_put_back(server);
		}
		return;
	}
	if (i < 0 || i >= c->n || !c->e[i].enabled || c->e[i].separator) {
		return;
	}
	const struct desk_card_entry *e = &c->e[i];
	if (e->id == CARD_ACTIVATE) {
		desk_tray_click(server->slp.card, 0, (int)server->cursor->x, (int)server->cursor->y);
		card_put_back(server);
		return;
	}
	if (e->submenu && server->slp.depth < (int)(sizeof server->slp.path / sizeof server->slp.path[0])) {
		int d = server->slp.depth++;
		server->slp.path[d] = e->id;
		snprintf(server->slp.path_label[d], sizeof server->slp.path_label[d], "%s", e->label);
		server->slp.hot_entry = -1;
		server->slp.top = 0;
		desk_tray_menu_show(e->id);
		card_paint(server);
		return;
	}
	desk_tray_menu_click(e->id);
	card_put_back(server);
}

/* Once a frame while something moves: the slips sliding out to the
 * pointer and back, and a card on its way out or in. */
static void slips_tick_frame(struct desk_server *server)
{
	double now = monotonic_sec();
	bool moved = false, more = false;
	if (server->slp.animating) {
		double dt = clampf_d(now - server->slp.anim_at, 0.0, 0.05);
		server->slp.anim_at = now;
		for (int i = 0; i < server->slp.n_lift; i++) {
			double to = server->slp.lift_id[i] == server->slp.hot ? 1.0 : 0.0;
			double *v = &server->slp.lift[i];
			double k = gfx_flat() ? 1.0 : 1.0 - exp(-dt / (to > *v ? 0.055 : 0.11));
			*v += (to - *v) * k;
			if (fabs(to - *v) < 0.01) {
				*v = to;
			} else {
				more = true;
			}
		}
		if (fabs(server->slp.scroll_to - server->slp.scroll) > 0.0) {
			double k = gfx_flat() ? 1.0 : 1.0 - exp(-dt / 0.07);
			server->slp.scroll += (server->slp.scroll_to - server->slp.scroll) * k;
			if (fabs(server->slp.scroll_to - server->slp.scroll) < 0.004) {
				server->slp.scroll = server->slp.scroll_to;
			} else {
				more = true;
			}
		}
		moved = true;
		server->slp.animating = more;
	}
	if (server->slp.pulling) {
		bool out = server->slp.pull_to > server->slp.pull_from;
		double u = clampf_d((now - server->slp.pull_start) / (out ? 0.34 : 0.24), 0.0, 1.0);
		server->slp.pull = server->slp.pull_from
			+ (server->slp.pull_to - server->slp.pull_from) * drawer_ease(u);
		if (u >= 1.0) {
			server->slp.pull = server->slp.pull_to;
			server->slp.pulling = false;
			if (server->slp.pull_to <= 0.0) {
				card_gone(server);
			}
		} else {
			more = true;
		}
		moved = true;
	}
	if (moved) {
		slips_publish(server);
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* A slip pulled down by a finger (2026-11-12, the owner: 右上の紙の部分（システムトレイ）も引っ張って
 * 出てくるように): its card comes out under the finger, as far as the finger goes, and let go it
 * goes the rest of the way out, or back in, as the finger was going. */
static bool hand_card_begin(struct desk_server *server, double x, double y, double fy)
{
	int i = desk_slip_at(server->desk_w, server->desk_h, x, y, server->drawer_t);
	const struct desk_tray_item *items = NULL;
	if (i < 0 || i >= desk_tray_items(&items)) {
		return false;
	}
	card_open(server, items[i].id);
	/* the finger has it, not the slide */
	server->slp.pulling = false;
	server->card_y0 = fy;
	server->card_pull0 = server->slp.pull;
	return true;
}

static double card_travel(struct desk_server *server)
{
	struct desk_card c;
	struct desk_card_place pl;
	if (!card_build(server, &c) || !desk_card_place(server->desk_w, server->desk_h,
			server->drawer_t, &c, &pl) || pl.travel < 1.0) {
		return 0.0;
	}
	return pl.travel;
}

static void hand_card_move(struct desk_server *server, double fy)
{
	double travel = card_travel(server);
	if (travel <= 0.0 || !server->slp.card) {
		return;
	}
	server->slp.pull = clampf_d(server->card_pull0 + (fy - server->card_y0) / travel, 0.0, 1.0);
	server->slp.pull_to = 1.0;
	slips_publish(server);
	server_schedule_frames(server);
}

static void hand_card_lift(struct desk_server *server, double vy)
{
	double travel = card_travel(server);
	if (!server->slp.card) {
		return;
	}
	double seen = server->slp.pull + (travel > 0.0 ? vy * 0.16 / travel : 0.0);
	card_pull(server, seen > 0.4 ? 1.0 : 0.0);
}

/* Where the pointer is, for the slips. True when it is on one. */
static bool slips_track(struct desk_server *server, bool can_touch)
{
	unsigned hot = 0;
	int i = can_touch ? desk_slip_at(server->desk_w, server->desk_h, server->cursor->x,
		server->cursor->y, server->drawer_t) : -1;
	if (i >= 0) {
		const struct desk_tray_item *items = NULL;
		if (i < desk_tray_items(&items)) {
			hot = items[i].id;
		}
	}
	if (hot != server->slp.hot) {
		server->slp.hot = hot;
		if (hot) {
			slip_lift(server, hot, true);
		}
		if (!server->slp.animating) {
			server->slp.animating = true;
			server->slp.anim_at = monotonic_sec();
		}
		server_schedule_frames(server);
	}
	return hot != 0;
}

static void slips_press(struct desk_server *server, uint32_t button, bool *taken)
{
	*taken = false;
	int i = desk_slip_at(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
		server->drawer_t);
	const struct desk_tray_item *items = NULL;
	if (i < 0 || i >= desk_tray_items(&items)) {
		return;
	}
	*taken = true;
	const struct desk_tray_item *it = &items[i];
	if (button == BTN_RIGHT || (button == BTN_LEFT && it->is_menu)) {
		card_open(server, it->id);
	} else if (button == BTN_LEFT || button == BTN_MIDDLE) {
		desk_tray_click(it->id, button == BTN_MIDDLE ? 1 : 0, (int)server->cursor->x,
			(int)server->cursor->y);
	}
}

/* A button pressed with the card out: it is the card's, whatever it is
 * over. Off the card it goes back in -- unless it is another slip's card
 * being asked for. */
static bool card_press(struct desk_server *server, uint32_t button)
{
	if (!card_out(server)) {
		return false;
	}
	struct desk_card c;
	int hit = card_under(server, &c);
	if (hit == -3) {
		bool slip;
		card_put_back(server);
		slips_press(server, button, &slip);
		return true;
	}
	if (button == BTN_LEFT) {
		card_choose(server, &c, hit);
	}
	return true;
}

/* The pointer over the card: the entry under it. True when it is the
 * card's, and nothing under it is to hear of it. */
static bool card_motion(struct desk_server *server)
{
	if (!card_out(server)) {
		return false;
	}
	struct desk_card c;
	int hit = card_under(server, &c);
	card_hot(server, hit >= 0 || hit == -2 ? hit : -1);
	return hit != -3;
}

static bool card_axis(struct desk_server *server, const struct wlr_pointer_axis_event *event)
{
	if (!card_out(server)) {
		return false;
	}
	struct desk_card c;
	struct desk_card_place pl;
	if (card_under(server, &c) == -3) {
		return false;
	}
	if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL
		|| !desk_card_place(server->desk_w, server->desk_h, server->drawer_t, &c, &pl)) {
		return true;
	}
	/* by the notch, as everything else the wheel turns: a trackpad's flick
	 * comes as dozens of small events */
	double d = event->delta;
	if (d == 0.0 && event->delta_discrete != 0) {
		d = (double)event->delta_discrete / 120.0 * DESK_NOTCH;
	}
	server->slp.card_scroll += d;
	int step = 0;
	while (server->slp.card_scroll >= DESK_NOTCH) {
		step++;
		server->slp.card_scroll -= DESK_NOTCH;
	}
	while (server->slp.card_scroll <= -DESK_NOTCH) {
		step--;
		server->slp.card_scroll += DESK_NOTCH;
	}
	if (!step) {
		return true;
	}
	int top = server->slp.top + step;
	if (top > c.n - pl.rows) {
		top = c.n - pl.rows;
	}
	if (top < 0) {
		top = 0;
	}
	if (top != server->slp.top) {
		server->slp.top = top;
		card_paint(server);
	}
	return true;
}

static bool slips_axis(struct desk_server *server, const struct wlr_pointer_axis_event *event)
{
	int i = desk_slip_at(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
		server->drawer_t);
	const struct desk_tray_item *items = NULL;
	int n = desk_tray_items(&items);
	if (i < 0 || i >= n) {
		return false;
	}
	int steps = event->delta_discrete ? event->delta_discrete
		: (int)lround(event->delta / 15.0 * 120.0);
	/* More slips than there is room for, and no card out: the wheel slides
	 * the row along, a slip a notch. Only then -- with all of them in
	 * sight, it is the program's, as it always was. */
	int fit = desk_slips_fit(server->desk_w, server->desk_h);
	if (n > DESK_SLIPS_MAX) {
		n = DESK_SLIPS_MAX;
	}
	if (fit > 0 && n > fit && !card_out(server)) {
		server->slp.wheel += steps;
		int notches = server->slp.wheel / 120;
		server->slp.wheel -= notches * 120;
		if (notches) {
			double to = clampf_d(round(server->slp.scroll_to) + notches, 0.0, (double)(n - fit));
			if (to != server->slp.scroll_to) {
				server->slp.scroll_to = to;
				if (!server->slp.animating) {
					server->slp.anim_at = monotonic_sec();
					server->slp.animating = true;
				}
				/* the one that was under the pointer goes with the row */
				server->slp.hot = 0;
				server_schedule_frames(server);
			}
		}
		return true;
	}
	if (steps) {
		desk_tray_scroll(items[i].id, steps,
			event->orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL);
	}
	return true;
}

/* The keys, with the card out: up and down it, into a submenu and back
 * out, choose, and put it back. */
static bool card_keys(struct desk_server *server, xkb_keysym_t sym)
{
	if (!card_out(server)) {
		return false;
	}
	struct desk_card c;
	struct desk_card_place pl;
	if (!card_build(server, &c) || !desk_card_place(server->desk_w, server->desk_h,
			server->drawer_t, &c, &pl)) {
		return false;
	}
	switch (sym) {
	case XKB_KEY_Escape:
		card_put_back(server);
		return true;
	case XKB_KEY_Left:
	case XKB_KEY_BackSpace:
		if (server->slp.depth > 0) {
			card_choose(server, &c, -2);
		}
		return true;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
	case XKB_KEY_space:
	case XKB_KEY_Right:
		if (c.hot >= 0 && (sym != XKB_KEY_Right || c.e[c.hot].submenu)) {
			card_choose(server, &c, c.hot);
		}
		return true;
	case XKB_KEY_Up:
	case XKB_KEY_Down: {
		int step = sym == XKB_KEY_Down ? 1 : -1;
		int i = c.hot;
		for (int n = 0; n < c.n; n++) {
			i = i < 0 ? (step > 0 ? 0 : c.n - 1) : (i + step + c.n) % c.n;
			if (!c.e[i].separator && c.e[i].enabled) {
				break;
			}
		}
		if (i < 0 || i >= c.n) {
			return true;
		}
		/* scrolled so it is in sight */
		if (i < server->slp.top) {
			server->slp.top = i;
		} else if (i >= server->slp.top + pl.rows) {
			server->slp.top = i - pl.rows + 1;
		}
		server->slp.hot_entry = -1;
		card_hot(server, i);
		return true;
	}
	default:
		return false;
	}
}

static void slips_bus_changed(void *data)
{
	struct desk_server *server = data;
	clock_publish(server);
	slips_publish(server);
}

static void media_changed(void *data)
{
	clock_publish(data);
	things_say(data);
}

#else

static void slips_tick_frame(struct desk_server *server)
{
	(void)server;
}

static bool hand_card_begin(struct desk_server *server, double x, double y, double fy)
{
	(void)server;
	(void)x;
	(void)y;
	(void)fy;
	return false;
}

static void hand_card_move(struct desk_server *server, double fy)
{
	(void)server;
	(void)fy;
}

static void hand_card_lift(struct desk_server *server, double vy)
{
	(void)server;
	(void)vy;
}

static bool slips_track(struct desk_server *server, bool can_touch)
{
	(void)server;
	(void)can_touch;
	return false;
}

static void slips_press(struct desk_server *server, uint32_t button, bool *taken)
{
	(void)server;
	(void)button;
	*taken = false;
}

static bool card_out(const struct desk_server *server)
{
	(void)server;
	return false;
}

static bool card_press(struct desk_server *server, uint32_t button)
{
	(void)server;
	(void)button;
	return false;
}

static bool card_motion(struct desk_server *server)
{
	(void)server;
	return false;
}

static bool card_axis(struct desk_server *server, const struct wlr_pointer_axis_event *event)
{
	(void)server;
	(void)event;
	return false;
}

static bool slips_axis(struct desk_server *server, const struct wlr_pointer_axis_event *event)
{
	(void)server;
	(void)event;
	return false;
}

static bool card_keys(struct desk_server *server, xkb_keysym_t sym)
{
	(void)server;
	(void)sym;
	return false;
}

static void card_gone(struct desk_server *server)
{
	(void)server;
}

#endif

/* ---- drawn on the CPU --------------------------------------------------
 *
 * A virtual machine without 3D, a GPU without its driver: the screen is put
 * together by the processor -- pixman, or GL that is a rasterizer running on
 * it (llvmpipe and the like) -- and everything the desk draws is drawn by the
 * processor too. Then it draws in 2D Fallback unless the user has chosen a
 * level of their own (gfx_wanted), says so in a letter once in a while, and
 * tells Shell Settings, which says it beside Graphics. The way things are
 * drawn is the same either way: 2D is the desk with its effects off, not a
 * second desk drawn some other way. DESK_ON_CPU=1 (or 0) says outright. */
