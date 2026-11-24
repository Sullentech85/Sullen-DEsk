/* Part of wm.c: the bookcase: its binders and the ALL APPS book.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* The book as it lies still, a leaf going over or not: the leaf goes on as
 * a layer of its own (SH_LEAF), and what is under it is the same picture
 * from the start of a turn to its middle (where the thumb index moves on)
 * and from there to its end. Its page is put at a quarter or three
 * quarters of the way through, which draws the same. */
static void book_look_still(const struct desk_server *server, struct desk_book_look *lk)
{
	book_look(server, lk);
	/* and without the hand on it, which goes on by itself (SH_BOOKHOT,
	 * SH_BOOKPLATE): the pointer going down the index drew the whole book
	 * again for every line it crossed */
	lk->hover = -1;
	lk->hover_tab = -1;
	lk->hover_flag = false;
	if (lk->open_t < 0.999) {
		return;
	}
	/* Settling, it is the spread it is settling on. The turn goes a shade
	 * past and back, and each time it crossed the page the book under the
	 * leaf was a different spread to draw again; this close, the leaf lies
	 * over all of the page that would have changed. */
	if (fabs(server->binder_pos_to - lk->page) < 0.10) {
		lk->page = server->binder_pos_to;
		return;
	}
	double n = floor(lk->page), frac = lk->page - n;
	lk->page = n + (frac > 0.5 ? 0.75 : frac > 0.0005 ? 0.25 : 0.0);
}

/* ---- the book still, drawn ahead ----------------------------------------
 *
 * A leaf going over draws the book under it again three times: as the turn
 * starts (the next page under the leaf), halfway (the thumb index moves on)
 * and as it settles. Each time it was the whole book laid into the view a
 * pixel at a time, 25 ms and more, and as the first three frames of a turn
 * were those, the leaf was half way over before it was seen to move. So
 * the stills a turn either way will want are drawn ahead while the book
 * lies open and nothing moves, one a tick, and kept with what was drawn on
 * the way; a turn finds them and only hands them over. */

/* the key of the book still for a look, as the shelf keys it */
static uint64_t book_still_key(struct desk_server *server, const struct desk_book_look *still)
{
	const struct desk_app_list *l = desk_apps_acquire();
	unsigned gen = l->generation;
	desk_apps_release(l);
	int taken = server->shelf_binder, tk = server->take.base >= 0 ? server->take.idx : -1;
	int size[2] = { server->desk_w, server->desk_h };
	uint64_t k = 14695981039346656037ull;
	k = key_add(k, &gen, sizeof gen);
	k = key_add(k, &taken, sizeof taken);
	k = key_add(k, &tk, sizeof tk);
	k = key_add(k, &server->binder_t, sizeof server->binder_t);
	k = key_add(k, size, sizeof size);
	k = key_add(k, server->book_q, strlen(server->book_q));
	k = key_add(k, server->book_pre, strlen(server->book_pre));
	for (int i = 0; i < server->book_ncand; i++) {
		k = key_add(k, server->book_cand[i], strlen(server->book_cand[i]));
	}
	return key_add(k, still, sizeof *still);
}

static void book_kept_forget(struct desk_server *server)
{
	for (int i = 0; i < BOOK_AHEAD; i++) {
		struct book_kept *e = &server->book_kept[i];
		if (e->buf) {
			wlr_buffer_unlock(&e->buf->base);
		}
		memset(e, 0, sizeof *e);
	}
}

static struct book_kept *book_kept_find(struct desk_server *server, uint64_t key)
{
	for (int i = 0; i < BOOK_AHEAD; i++) {
		if (server->book_kept[i].buf && server->book_kept[i].key == key) {
			return &server->book_kept[i];
		}
	}
	return NULL;
}

/* buf kept as the still for key, in the slot of the one wanted longest ago */
static void book_kept_keep(struct desk_server *server, uint64_t key, struct desk_buf *buf,
	int x, int y, int dw, int dh, int ox, int oy)
{
	struct book_kept *slot = &server->book_kept[0];
	for (int i = 0; i < BOOK_AHEAD; i++) {
		struct book_kept *e = &server->book_kept[i];
		if (!e->buf) {
			slot = e;
			break;
		}
		if (e->used < slot->used) {
			slot = e;
		}
	}
	if (slot->buf) {
		wlr_buffer_unlock(&slot->buf->base);
	}
	wlr_buffer_lock(&buf->base);
	*slot = (struct book_kept){ buf, key, x, y, dw, dh, ox, oy, monotonic_sec() };
}

static void sh_book_still_draw(cairo_t *cr, int w, int h, void *data)
{
	desk_draw_book_still(cr, w, h, data);
}

/* The book still, for key: handed over if it is kept, else drawn and kept. */
static void book_still_put(struct desk_server *server, uint64_t key, bool force,
	const double *box, struct desk_book_look *still)
{
	struct desk_sprite *sp = &server->sh_sprite[SH_BINDER];
	if (!force && sp->drawn && sp->key == key) {
		if (sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, true);
		}
		return;
	}
	struct book_kept *e = force ? NULL : book_kept_find(server, key);
	struct desk_buf *b;
	int x, y, dw, dh, ox, oy;
	if (e) {
		b = e->buf;
		x = e->x;
		y = e->y;
		dw = e->dw;
		dh = e->dh;
		ox = e->ox;
		oy = e->oy;
		e->used = monotonic_sec();
	} else {
		b = sprite_picture(server, box, sh_book_still_draw, still, &x, &y, &dw, &dh, &ox, &oy);
		if (!b) {
			if (sp->node) {
				wlr_scene_node_set_enabled(&sp->node->node, false);
			}
			sp->drawn = false;
			return;
		}
		book_kept_keep(server, key, b, x, y, dw, dh, ox, oy);
		/* the scene and the kept slot hold it now: drawn, it is let go */
		wlr_buffer_drop(&b->base);
	}
	cairo_surface_flush(b->surface);
	prof_upload(b->base.width, b->base.height);
	if (!sp->node) {
		sp->node = scene_new(server->sh_layer[SH_BINDER], &b->base);
	} else {
		scene_swap(sp->node, &b->base);
	}
	wlr_scene_node_set_position(&sp->node->node, x, y);
	wlr_scene_buffer_set_dest_size(sp->node, dw, dh);
	wlr_scene_node_set_enabled(&sp->node->node, true);
	sp->x = x;
	sp->y = y;
	sp->dw = dw;
	sp->dh = dh;
	sp->ox = ox;
	sp->oy = oy;
	sp->bw = b->base.width;
	sp->bh = b->base.height;
	sp->key = key;
	sp->drawn = true;
}

static int binder_count(struct desk_server *server);

/* One still the next turn either way will want, drawn while the book lies
 * open and nothing moves; false when there was none left to draw. */
static bool book_ahead(struct desk_server *server)
{
	if (!shelf_up(server) || server->shelf_moving || server->shelf_binder != DESK_SHELF_BOOK
		|| server->binder_moving || server->binder_pos_moving || server->binder_t < 0.999
		|| server->binder_putback || server->take.base >= 0) {
		return false;
	}
	int n = (int)lround(server->binder_pos), last = binder_count(server) - 1;
	/* on: under the leaf as it goes, half way, settled; and back */
	static const double AHEAD[] = { 0.25, 0.75, 1.0, -0.25, -0.75, -1.0 };
	for (size_t i = 0; i < sizeof AHEAD / sizeof *AHEAD; i++) {
		double p = n + AHEAD[i];
		if (p < 0.0 || p > last) {
			continue;
		}
		struct desk_book_look lk;
		book_look_still(server, &lk);
		lk.page = p;
		uint64_t key = book_still_key(server, &lk);
		if (book_kept_find(server, key)) {
			continue;
		}
		double box[4];
		desk_book_box(server->desk_w, server->desk_h, &lk, box);
		int x, y, dw, dh, ox, oy;
		struct desk_buf *b = sprite_picture(server, box, sh_book_still_draw, &lk, &x, &y, &dw,
			&dh, &ox, &oy);
		if (b) {
			book_kept_keep(server, key, b, x, y, dw, dh, ox, oy);
			wlr_buffer_drop(&b->base);
		}
		return true;
	}
	return false;
}

/* the leaf of the book going over, by itself */
static void sh_leaf_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	struct desk_book_look lk;
	book_look(a->server, &lk);
	desk_draw_book_leaf(cr, w, h, &lk);
}

/* The book still with the hand put back on it: the look its patches are
 * drawn from. */
static void book_look_hot(const struct desk_server *server, struct desk_book_look *lk)
{
	book_look_still(server, lk);
	lk->hover = server->binder_sel;
	lk->hover_tab = server->book_tab;
	lk->hover_flag = server->book_flag;
}

/* a patch of the book where the hand is on it (part: desk_draw_book_hot) */
static void sh_book_hot_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	struct desk_book_look lk;
	book_look_hot(a->server, &lk);
	desk_draw_book_hot(cr, w, h, &lk, a->part);
}

static void sh_binder_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	struct desk_server *server = a->server;
	if (server->shelf_binder == DESK_SHELF_BOOK) {
		struct desk_book_look lk;
		book_look_still(server, &lk);
		desk_draw_book_still(cr, w, h, &lk);
		return;
	}
	struct desk_binder_corner c = { server->bnc_idx, server->bnc_s, server->bnc_lift,
		server->bnc_hot, bnc_in_drawer(server, server->bnc_idx), server->bnc_flag };
	desk_draw_binder(cr, w, h, server->shelf_binder,
		server->binder_pos, server->binder_sel, clampf_d(server->binder_t, 0.0, 1.0),
		server->bnc_idx >= 0 ? &c : NULL, server->take.base >= 0 ? server->take.idx : -1);
}

/* How far down off the shelf the binder has come: 0 up out of sight, 1
 * lying open -- as it lands, a little past 1 and back. As an opacity, no
 * further than 1. */
static double binder_down(const struct desk_server *server)
{
	return clampf_d(server->binder_t, 0.0, 1.0);
}

/* The book or binder draws its own way off the shelf, the whole of it:
 * here it is only shown, or not. */
static void sh_binder_follow(struct desk_server *server)
{
	struct desk_sprite *sp = &server->sh_sprite[SH_BINDER];
	if (!sp->node || !sp->drawn) {
		return;
	}
	wlr_scene_node_set_position(&sp->node->node, sp->x, sp->y);
	wlr_scene_node_set_enabled(&sp->node->node, binder_down(server) > 0.002);
}

/* One layer of the view: drawn again if its key has moved, shown either way. */
static void sh_put(struct desk_server *server, int k, uint64_t key, bool force,
	const double *box, sprite_fn fn, void *args)
{
	struct desk_sprite *sp = &server->sh_sprite[k];
	if (!force && sp->drawn && sp->key == key) {
		if (sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, true);
		}
		return;
	}
	sp->key = key;
	sprite_paint(server, sp, server->sh_layer[k], box, fn, args);
}

/* The same for a layer that is a patch of the one under it (sprite_paint_on). */
static void sh_put_on(struct desk_server *server, int k, int under, uint64_t key, bool force,
	const double *box, sprite_fn fn, void *args)
{
	struct desk_sprite *sp = &server->sh_sprite[k];
	if (!force && sp->drawn && sp->key == key) {
		if (sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, true);
		}
		return;
	}
	sp->key = key;
	sprite_paint_on(server, sp, server->sh_layer[k], &server->sh_sprite[under], box, fn, args);
}

static void sh_hide(struct desk_server *server, int k)
{
	struct desk_sprite *sp = &server->sh_sprite[k];
	if (sp->node) {
		wlr_scene_node_set_enabled(&sp->node->node, false);
	}
}

static void sh_opacity(struct desk_server *server, double a)
{
	for (int k = 0; k < SH_LAYERS; k++) {
		if (server->sh_sprite[k].node) {
			double o = a;
			wlr_scene_buffer_set_opacity(server->sh_sprite[k].node, (float)o);
		}
	}
}

static void paint_shelf_body(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	if (!shelf_up(server)) {
		if (server->sh_live) {
			wlr_scene_node_set_enabled(&server->sh_live->node, false);
		}
		if (server->film_a[DESK_VIEW_SHELF]) {
			wlr_scene_node_set_enabled(&server->film_a[DESK_VIEW_SHELF]->node, false);
			wlr_scene_node_set_enabled(&server->film_b[DESK_VIEW_SHELF]->node, false);
		}
		return;
	}
	film_ensure(server, DESK_VIEW_SHELF, server->layer_shelf);
	if (!server->sh_live) {
		server->sh_live = wlr_scene_tree_create(server->layer_shelf);
		for (int k = 0; k < SH_LAYERS; k++) {
			server->sh_layer[k] = wlr_scene_tree_create(server->sh_live);
		}
	}
	/* and the room: turned to a new shape, the case is first drawn from
	 * the old shape's room and has to be drawn again when its own lands */
	unsigned room = desk_view_room(w, h, DESK_VIEW_SHELF);
	bool force = server->sh_w != w || server->sh_h != h || server->sh_room != room;
	server->sh_w = w;
	server->sh_h = h;
	server->sh_room = room;

	/* The cairo half: the settled bookcase, the open binder, the sheet in
	 * the air -- and none of it while the move has not got far enough to
	 * show it. Each layer is drawn again only when what it shows has
	 * changed: a binder coming out of the row is its own rectangle, and
	 * the case under it is not touched. */
	bool settled = server->shelf_t > 0.70;
	bool have_view = settled;
	struct sh_args a = { server, -1, -1, -1, 0, 0.0, NULL, time(NULL) };
	if (settled) {
		a.taken = server->shelf_binder;
		a.hover = a.taken >= 0 ? -1
			: (server->shelf_hover >= 0 ? server->shelf_hover : server->hover_from);
		a.ht = a.taken >= 0 ? 0.0 : server->hover_t;
		const struct desk_app_list *l = desk_apps_acquire();
		unsigned gen = l->generation;
		desk_apps_release(l);
		double full[4] = { 0.0, 0.0, (double)w, (double)h };

		unsigned dc = shelf_dress_today();
		uint64_t key = 14695981039346656037ull;
		key = key_add(key, &gen, sizeof gen);
		key = key_add(key, &a.taken, sizeof a.taken);
		key = key_add(key, &dc, sizeof dc);
		sh_put(server, SH_CASE, key, force, full, sh_case_draw, &a);

		double box[4];
		if (a.hover >= 0 && a.ht > 0.004 && desk_shelf_hover_box(w, h, a.hover, box)) {
			uint64_t hk = key_add(key, &a.hover, sizeof a.hover);
			hk = key_add(hk, &a.ht, sizeof a.ht);
			a.box = box;
			sh_put(server, SH_HOVER, hk, force, box, sh_hover_draw, &a);
		} else {
			sh_hide(server, SH_HOVER);
		}

		int slot = SH_GADGET0;
		for (int i = 0; i < DESK_GADGET_COUNT && slot <= SH_GADGET2; i++) {
			double gb[4];
			if (!desk_shelf_gadget_box(w, h, i, gb)) {
				continue;
			}
			a.gadget = i;
			sh_put(server, slot, desk_shelf_gadget_key(i, a.now), force, gb,
				sh_gadget_draw, &a);
			slot++;
		}

		if (a.taken >= 0 && server->binder_putback && server->sh_sprite[SH_BINDER].drawn) {
			/* the book left open as you turn away: as it was, not drawn
			 * again for nobody to look at */
			if (server->sh_sprite[SH_BINDER].node) {
				wlr_scene_node_set_enabled(&server->sh_sprite[SH_BINDER].node->node, true);
			}
			sh_hide(server, SH_LEAF);
		} else if (a.taken >= 0) {
			uint64_t bk = key_add(key, &server->binder_pos, sizeof server->binder_pos);
			bk = key_add(bk, &server->binder_sel, sizeof server->binder_sel);
			/* and its corner, and what is printed on the back of it */
			bool ind = bnc_in_drawer(server, server->bnc_idx);
			bk = key_add(bk, &server->bnc_idx, sizeof server->bnc_idx);
			bk = key_add(bk, &server->bnc_s, sizeof server->bnc_s);
			bk = key_add(bk, &server->bnc_hot, sizeof server->bnc_hot);
			bk = key_add(bk, &server->bnc_flag, sizeof server->bnc_flag);
			bk = key_add(bk, &ind, sizeof ind);
			int tk = server->take.base >= 0 ? server->take.idx : -1;
			bk = key_add(bk, &tk, sizeof tk);
			/* and how far it has come off the shelf */
			bk = key_add(bk, &server->binder_t, sizeof server->binder_t);
			double bb[4];
			uint64_t lkey = 0;
			double lb[4];
			bool leaf = false, book = false;
			struct desk_book_look book_still;
			if (server->shelf_binder == DESK_SHELF_BOOK) {
				/* and what is written in it, and what the pointer is on;
				 * the leaf going over apart, under it the book still
				 * (book_look_still) -- not binder_pos, which moves on
				 * every frame of a turn */
				struct desk_book_look lk;
				book_look(server, &lk);
				book_look_still(server, &book_still);
				bk = book_still_key(server, &book_still);
				lkey = key_add(bk, &lk, sizeof lk);
				desk_book_box(w, h, &book_still, bb);
				leaf = desk_book_leaf_box(w, h, &lk, lb);
				book = true;
			} else {
				desk_binder_box(w, h, server->binder_pos, server->binder_t, bb);
			}
			if (book) {
				/* opening and shutting, drawn smaller as the binder is (2026-11-12: ALL APPS
				 * opening drew 35 ms a frame on a Surface Pro's screen at High) */
				double shrink = server->binder_moving ? motion_res() : 1.0;
				uint64_t sk = shrink < 1.0 ? key_add(bk, &shrink, sizeof shrink) : bk;
				server->sprite_shrink = shrink;
				book_still_put(server, sk, force, bb, &book_still);
				server->sprite_shrink = 1.0;
			} else {
				/* coming off the shelf, turning its sheets, going back: drawn smaller
				 * while it moves, as a sheet folding is (motion_res; whole at High and
				 * above), and whole again at rest (2026-11-11, the owner: the bookcase's
				 * sheets a little heavy -- 30 frames a second at Medium) */
				double shrink = server->binder_moving || server->binder_pos_moving
					? motion_res() : 1.0;
				bk = key_add(bk, &shrink, sizeof shrink);
				server->sprite_shrink = shrink;
				sh_put(server, SH_BINDER, bk, force, bb, sh_binder_draw, &a);
				server->sprite_shrink = 1.0;
			}
			sh_binder_follow(server);
			/* the hand on the book: the line it is on, or the tab, and the
			 * plate that line puts on an empty page -- patches of it */
			struct desk_book_look hot;
			book_look_hot(server, &hot);
			for (int part = 0; part < 2; part++) {
				double hb[4];
				if (book && desk_book_hot_box(w, h, &hot, part, hb)) {
					uint64_t hk = key_add(bk, &part, sizeof part);
					hk = key_add(hk, &hot.hover, sizeof hot.hover);
					hk = key_add(hk, &hot.hover_tab, sizeof hot.hover_tab);
					hk = key_add(hk, &hot.hover_flag, sizeof hot.hover_flag);
					a.part = part;
					sh_put_on(server, SH_BOOKHOT + part, SH_BINDER, hk, force, hb,
						sh_book_hot_draw, &a);
				} else {
					sh_hide(server, SH_BOOKHOT + part);
				}
			}
			if (leaf) {
				/* the leaf going over is only ever moving */
				double shrink = motion_res();
				server->sprite_shrink = shrink;
				sh_put(server, SH_LEAF, key_add(lkey, &shrink, sizeof shrink), force, lb,
					sh_leaf_draw, &a);
				server->sprite_shrink = 1.0;
			} else {
				sh_hide(server, SH_LEAF);
			}
		} else {
			for (int k = SH_BINDER; k <= SH_LEAF; k++) {
				sh_hide(server, k);
			}
		}
	} else {
		for (int k = SH_CASE; k <= SH_LEAF; k++) {
			sh_hide(server, k);
		}
	}
	wlr_scene_node_set_enabled(&server->sh_live->node, have_view);

	double t = server->shelf_t;
	if (!server->film_a[DESK_VIEW_SHELF] || (!server->shelf_moving && t >= 0.9995)) {
		film_hide(server, DESK_VIEW_SHELF);
		wlr_scene_node_set_position(&server->sh_live->node, 0, 0);
		sh_opacity(server, 1.0);
		return;
	}
	/* Hand over to the full resolution sheet at the end of the move, and
	 * fade the whole overlay in at the start so the drawer being pushed
	 * shut is a thing that happens rather than a thing that pops. */
	double over;
	int dx, dy;
	film_play(server, DESK_VIEW_SHELF, t, &over, &dx, &dy);
	wlr_scene_node_set_position(&server->sh_live->node, dx, dy);
	sh_opacity(server, have_view ? over : 0.0);
	wlr_scene_node_set_enabled(&server->sh_live->node, have_view && over > 0.001);
}

static void paint_shelf(struct desk_server *server)
{
	prof_begin(PROF_SHELF);
	paint_shelf_body(server);
	prof_end();
}

static void shelf_toggle(struct desk_server *server);
static void lock_request(struct desk_server *server);
static void lock_films(struct desk_server *server);
static void paint_lock(struct desk_server *server);
static bool locked(const struct desk_server *server);
static void show_for_today(struct desk_server *server);
static void show_stop(struct desk_server *server, bool skipped);
static void show_tick(struct desk_server *server);
static void show_midnight_check(struct desk_server *server);
static void drawer_toggle(struct desk_server *server);
static void fullscreen_layer_order(struct desk_server *server);
static int drawer_lift(struct desk_server *server);
static double drawer_line(struct desk_server *server);
static int drawer_index_of(struct desk_server *server, const struct desk_sheet *sheet);
static void drawer_lift_sheet(struct desk_server *server, int i);
static void drawer_forget(struct desk_server *server, struct desk_sheet *sheet);
static void console_toggle(struct desk_server *server);
static void post_toggle(struct desk_server *server);
static void card_gone(struct desk_server *server);
static bool console_dying(const struct desk_server *server);
static void console_power_reset(struct desk_server *server);
static void console_live(struct desk_server *server, bool on);
static void console_open_settings(struct desk_server *server, int which);
static void screens_next(struct desk_server *server);
static void screens_forget(struct desk_server *server);
static int screens_count(const struct desk_server *server);
static void screen_go_dark(struct desk_server *server);

/* The binder (or the book) back on the shelf, out of the hand: and the
 * favourites' sheets peeled in it leave it now. */
static void hover_set(struct desk_server *server, int which);

static void binder_off_hand(struct desk_server *server)
{
	if (server->shelf_binder == DESK_SHELF_FAV) {
		desk_apps_fav_tidy();
	}
	server->shelf_binder = -1;
	/* back in the row: the one out of it is the one under the pointer now,
	 * not the one it was on when this was taken down -- the pointer has
	 * moved since, with no binder listening (or the screen has turned) */
	if (shelf_up(server)) {
		hover_set(server, desk_shelf_hit(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y));
	}
}

/* How many sheets that binder holds. */
static int binder_count(struct desk_server *server)
{
	if (server->shelf_binder < 0) {
		return 0;
	}
	if (server->shelf_binder == DESK_SHELF_BOOK) {
		/* the book turns a spread at a time */
		return desk_book_spreads(server->desk_w, server->desk_h, server->book_q);
	}
	const struct desk_app_list *l = desk_apps_acquire();
	int n = l->shelf[server->shelf_binder].count;
	desk_apps_release(l);
	return n;
}

/* Taken down, the book or a binder is brought round and opened; put back,
 * the same the other way, a little quicker. */
#define BOOK_OPEN_S 1.25
#define BOOK_CLOSE_S 0.95

static void binder_animate(struct desk_server *server, double to)
{
	double now = monotonic_sec();
	server->binder_from = server->binder_t;
	server->binder_to = to;
	server->binder_start = now;
	/* from wherever it had got to */
	double left = fabs(to - clampf_d(server->binder_t, 0.0, 1.0));
	server->binder_dur = fmax((to > 0.5 ? BOOK_OPEN_S : BOOK_CLOSE_S) * left, 0.05);
	snd_forget(&server->snd_binder_down);
	server->binder_moving = 1;
	server_schedule_frames(server);
	paint_shelf(server);
}

static void bnc_reset(struct desk_server *server);

static void book_begin(struct desk_server *server);
static void book_end(struct desk_server *server);

static void binder_open(struct desk_server *server, int which)
{
	bnc_reset(server);
	desk_sound_play(DESK_SND_BINDER_LIFT, 0.0, 1.0, 0.0);
	server->snd_page = 0;
	server->shelf_binder = which;
	server->binder_pos = 0.0;
	server->binder_pos_to = 0.0;
	server->binder_pos_vel = 0.0;
	server->binder_pos_moving = 0;
	server->binder_scroll = 0.0;
	server->binder_sel = -1;
	server->binder_t = 0.0;
	if (which == DESK_SHELF_BOOK) {
		book_begin(server);
	}
	binder_animate(server, 1.0);
}

static void binder_close(struct desk_server *server)
{
	if (server->shelf_binder < 0) {
		return;
	}
	server->binder_sel = -1;
	bnc_reset(server);
	if (server->shelf_binder == DESK_SHELF_BOOK) {
		book_end(server);
	}
	desk_sound_play(DESK_SND_BINDER_CLOSE, 0.05, 1.0, 0.0);
	binder_animate(server, 0.0);
}

/* Turn the block over. step is in sheets, so a notch of the wheel is one
 * sheet and the paper visibly moves on the ring. */
/* Turn the block to a place picked outright -- a thumb tab, a line chosen
 * with the keys -- and let the spring take it there. */
static void binder_turn_to(struct desk_server *server, double to)
{
	int n = binder_count(server);
	if (n <= 1) {
		return;
	}
	/* You can turn a block right through to its last sheet. Stopping a
	 * fanful short meant a binder of nine or fewer would not turn at all
	 * and a longer one could never reach its end. */
	int last = n - 1;
	if (to > last) {
		to = last;
	}
	if (to < 0) {
		to = 0;
	}
	if (to == server->binder_pos_to && !server->binder_pos_moving) {
		return;
	}
	server->binder_pos_to = to;
	if (!server->binder_pos_moving) {
		server->binder_pos_at = monotonic_sec();
		server->binder_pos_vel = 0.0;
	}
	server->binder_pos_moving = 1;
	server->binder_sel = -1;
	paint_shelf(server);
	server_schedule_frames(server);
}

/* Turn the block over by step sheets: a notch of the wheel is one sheet,
 * so the paper visibly moves on the ring. */
static void binder_riffle(struct desk_server *server, int step)
{
	double cur = server->binder_pos;
	double to = server->binder_pos_to + step;
	/* You cannot shove the block further ahead than the paper has got to.
	 * One flick of a trackpad arrives as two dozen notches inside a
	 * millisecond, and without this it tears straight through to the last
	 * sheet and sits there -- which looks exactly like nothing turning. */
	const double lead = 2.5;
	if (to > cur + lead) {
		to = floor(cur + lead);
	}
	if (to < cur - lead) {
		to = ceil(cur - lead);
	}
	binder_turn_to(server, to);
}

/* The block follows where it has been turned to on a spring, so a sheet
 * goes over, a shade too far, and settles back against the others; and a
 * run of notches is one movement that keeps its way, rather than a start
 * and a stop for each. A sheet takes about a fifth of a second. */
static void binder_pos_tick(struct desk_server *server)
{
	if (!server->binder_pos_moving) {
		return;
	}
	const double W = 24.0, Z = 0.72;
	double now = monotonic_sec();
	double dt = clampf_d(now - server->binder_pos_at, 0.0, 0.05);
	server->binder_pos_at = now;
	/* in small steps, so a slow frame does not throw it */
	int n = (int)ceil(dt / (1.0 / 480.0));
	double h = n > 0 ? dt / n : 0.0;
	for (int i = 0; i < n; i++) {
		double acc = W * W * (server->binder_pos_to - server->binder_pos)
			- 2.0 * Z * W * server->binder_pos_vel;
		server->binder_pos_vel += acc * h;
		server->binder_pos += server->binder_pos_vel * h;
	}
	/* a sheet going over the ring, heard as it passes the top */
	int page = (int)lround(server->binder_pos);
	if (page != server->snd_page) {
		if (now - server->snd_page_at > 0.035) {
			desk_sound_play(DESK_SND_PAGE_TURN, 0.12, 1.0, 0.0);
			server->snd_page_at = now;
		}
		server->snd_page = page;
	}
	if (gfx_flat()) {
		server->binder_pos = server->binder_pos_to;   /* 2D: there at once */
		server->binder_pos_vel = 0.0;
	}
	if (fabs(server->binder_pos_to - server->binder_pos) < 0.002
		&& fabs(server->binder_pos_vel) < 0.02) {
		server->binder_pos = server->binder_pos_to;
		server->binder_pos_vel = 0.0;
		server->binder_pos_moving = 0;
	}
	paint_shelf(server);
	if (server->binder_pos_moving) {
		server_schedule_frames(server);
	}
}

/* The binder under the pointer comes out of the row. */
static void hover_set(struct desk_server *server, int which)
{
	if (which == server->shelf_hover) {
		return;
	}
	server->shelf_hover = which;
	if (which >= 0) {
		server->hover_from = which;
	}
	server->hover_start = monotonic_sec();
	server->hover_moving = 1;
	server_schedule_frames(server);
	paint_shelf(server);
}

static void hover_tick(struct desk_server *server)
{
	if (!server->hover_moving) {
		return;
	}
	double u = clampf_d((monotonic_sec() - server->hover_start) / 0.17, 0, 1);
	double e = u * u * (3.0 - 2.0 * u);
	server->hover_t = server->shelf_hover >= 0 ? e : 1.0 - e;
	if (u >= 1.0) {
		server->hover_t = server->shelf_hover >= 0 ? 1.0 : 0.0;
		server->hover_moving = 0;
		if (server->shelf_hover < 0) {
			server->hover_from = -1;
		}
	}
	paint_shelf(server);
	if (server->hover_moving) {
		server_schedule_frames(server);
	}
}

/* Sheets printed ahead, one a frame: the first ones of the binder under
 * the pointer on the shelf, so taking it down does not stop to print them
 * all at once; and, with one open and at rest, the next ones a turn will
 * bring in either way. */
static void binder_warm_tick(struct desk_server *server)
{
	if (server->shelf_t < 0.70) {
		return;
	}
	int shelf = server->shelf_binder;
	double first = server->binder_pos;
	if (shelf < 0) {
		shelf = server->shelf_hover;
		first = 0.0;
	} else if (server->binder_pos_moving || server->binder_moving) {
		return;
	}
	if (shelf == DESK_SHELF_BOOK && server->shelf_binder < 0) {
		if (desk_book_warm(server->desk_w, server->desk_h)) {
			server_schedule_frames(server);
		}
		return;
	}
	if (desk_shelf_is_binder(shelf)
		&& desk_binder_warm(server->desk_w, server->desk_h, shelf, first, 1)) {
		server_schedule_frames(server);
		return;
	}
	/* and, under the pointer on the shelf, what it is drawn from as it is
	 * taken down and opened */
	if (desk_shelf_is_binder(shelf) && server->shelf_binder < 0
		&& desk_binder_anim_warm(server->desk_w, server->desk_h, shelf)) {
		server_schedule_frames(server);
	}
}

static void binder_tick(struct desk_server *server)
{
	if (!server->binder_moving) {
		return;
	}
	double u = clampf_d((monotonic_sec() - server->binder_start) / server->binder_dur, 0, 1);
	/* in time as it goes: the moves are eased in it (op_draw) */
	double was = server->binder_t;
	server->binder_t = server->binder_from + (server->binder_to - server->binder_from) * u;
	if (was < 0.62 && server->binder_t >= 0.62) {
		/* the board lifting off the pages */
		desk_sound_play(DESK_SND_PAGE_TURN, 0.05, 0.9, 0.85);
	}
	if (server->binder_to > 0.5) {
		/* down on the desk, open */
		snd_end(&server->snd_binder_down, server->binder_start, server->binder_dur,
			DESK_SND_BINDER_OPEN, 0.05, 1.0, 0.0);
	}
	if (u >= 1.0) {
		server->binder_t = server->binder_to;
		server->binder_moving = 0;
		if (server->binder_t <= 0.001) {
			binder_off_hand(server);
			desk_binder_forget();
		}
	}
	paint_shelf(server);
	if (server->binder_moving) {
		server_schedule_frames(server);
	}
}

static int base_new(struct desk_server *server, const struct desk_app *app,
	cairo_surface_t *face, double sw, double sh);

/* The desk's own things that have a line on the shelves as programs do (2026-11-12, the owner:
 * 電卓やノートなどは…本棚からも起動できるように（電卓は通常のアプリとして扱う感じ)): taken
 * from there, the thing comes out of the drawer onto the desk, and no program is started. */
static const struct desk_thing *thing_named(const char *name)
{
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if (!strcmp((*t)->name, name)) {
			return *t;
		}
	}
	return NULL;
}

static const struct desk_thing *app_own_thing(const struct desk_app *app)
{
	if (!strcmp(app->id, "desk-calculator.desktop")) {
		return thing_named("calculator");
	}
	return NULL;
}

/* A thing out of the drawer onto the desk, if it is in there and kept on this machine. */
static bool thing_out(struct desk_server *server, const struct desk_thing *t)
{
	if (!t || !t->take_out || !t->in_drawer || (t->offered && !t->offered(server))
		|| !t->in_drawer(server)) {
		return false;
	}
	wlr_log(WLR_INFO, "taking the %s out of the drawer", t->name);
	t->take_out(server);
	server_schedule_frames(server);
	return true;
}

/* Take the sheet out and go and do what is printed on it: the sheet comes
 * off the ring into the hand and goes down to the desk with you, to where
 * the program's window will be (take_tick). Whether the program is ready
 * by the time it is put down is the program's business. */
static void binder_take(struct desk_server *server, int idx)
{
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_shelf_info *si = &l->shelf[server->shelf_binder];
	bool taken = false;
	const struct desk_app *app = desk_shelf_app(l, server->shelf_binder, idx);
	if (app) {
		wlr_log(WLR_INFO, "taking %s off the %s binder", app->id, si->label_en);

		double cx, cy, sw, sh, a;
		desk_binder_sheet_pose(server->desk_w, server->desk_h,
			(double)idx - server->binder_pos, idx == server->binder_sel,
			&cx, &cy, &sw, &sh, &a);
		char code[32];
		char key[4] = { 0 };
		for (int i = 0; i < 3 && si->key[i]; i++) {
			key[i] = (char)toupper((unsigned char)si->key[i]);
		}
		snprintf(code, sizeof code, "%s\xc2\xb7%03d", key, idx + 1);
		const struct desk_thing *own = app_own_thing(app);
		cairo_surface_t *face = !own && server->take.base < 0 && server->conf.take_sheet
			? desk_app_sheet_surface(app->id, sw, sh, code) : NULL;
		int b = face ? base_new(server, app, face, sw, sh) : -1;
		if (b >= 0) {
			double now = monotonic_sec();
			snd_at(server, DESK_SND_SHEET_TAKE, cx, 1.0);
			server->take.base = b;
			server->take.idx = idx;
			server->take.start = now;
			server->take.turn_at = now + TAKE_TURN;
			server->take.land_at = now + TAKE_TURN + 0.40;
			server->take.turned = false;
			server->take.cx0 = cx;
			server->take.cy0 = cy;
			server->take.w0 = sw;
			server->take.h0 = sh;
			server->take.a0 = a;
			taken = true;
		}
		pid_t sid = own ? (thing_out(server, own), 0) : desk_app_launch(app);
		if (b >= 0) {
			server->bases[b].sid = sid > 0 ? sid : 0;
		}
	}
	desk_apps_release(l);
	/* With a sheet in the hand you turn back to the desk a moment later
	 * (take_tick), without one now; the binder is left open as you go and
	 * put back while you are not looking (shelf_tick), as the book is --
	 * not shut in front of you, a moment you would not see the end of.
	 * Meanwhile it takes no more clicks. */
	server->binder_sel = -1;
	bnc_reset(server);
	server->binder_to = 0.0;
	server->binder_putback = true;
	if (!taken && server->shelf_t > 0.5) {
		shelf_toggle(server);
	}
	paint_shelf(server);
	server_schedule_frames(server);
}

/* ---- the book of every program -------------------------------------
 *
 * Taken down, it is open at its first spread with the caret on the line
 * at the head of the left-hand page: whatever is typed goes there (through
 * the input method, as the binder of notes takes it), and the index is of
 * only what answers to what is written, the first of it chosen -- Enter
 * takes it. Up and down choose along the index, the wheel and Page Up and
 * Down turn a spread, a tab of the thumb index opens it at that letter,
 * Escape rubs out what is written and then puts the book back. A line
 * clicked is taken, as a binder's sheet is: a sheet comes up off the page
 * and is carried down to the desk, where the program's window will be. */

static void book_look(const struct desk_server *server, struct desk_book_look *lk)
{
	memset(lk, 0, sizeof *lk);
	lk->query = server->book_q;
	lk->caret = server->book_caret;
	lk->preedit = server->book_pre;
	lk->preedit_caret = server->book_pre_caret;
	lk->cands = server->book_cand_p;
	lk->ncand = server->book_ncand;
	lk->cand_cursor = server->book_cand_cursor;
	lk->cand_page = server->book_cand_page;
	lk->page = server->binder_pos;
	lk->hover = server->binder_sel;
	lk->sel = server->book_sel;
	lk->hover_tab = server->book_tab;
	lk->open_t = clampf_d(server->binder_t, 0.0, 1.0);
	lk->ime = server->book_ime;
	lk->hover_flag = server->book_flag;
}

static bool host_origin(struct desk_server *server, int *ox, int *oy);

/* Tell the input method where the line being written on is. */
static void book_tell_cursor(struct desk_server *server)
{
	if (!server->book_ime || server->shelf_binder != DESK_SHELF_BOOK) {
		return;
	}
	struct desk_book_look lk;
	book_look(server, &lk);
	double b[4];
	if (!desk_book_find_box(server->desk_w, server->desk_h, &lk, b)) {
		return;
	}
	int ox = 0, oy = 0;
	host_origin(server, &ox, &oy);
	int c[4] = { ox + (int)lround(b[0] + b[2] * 0.12), oy + (int)lround(b[1]),
		(int)lround(b[2] * 0.5), (int)lround(b[3]) };
	if (memcmp(c, server->book_cursor, sizeof c) != 0) {
		memcpy(server->book_cursor, c, sizeof c);
		desk_ime_typing_cursor(c[0], c[1], c[2], c[3]);
	}
}

static void book_changed(struct desk_server *server)
{
	paint_shelf(server);
	book_tell_cursor(server);
	server_schedule_frames(server);
}

/* What is written has changed: the index is a new one, open at its start,
 * the best of it chosen. */
static void book_written(struct desk_server *server)
{
	server->binder_pos = server->binder_pos_to = 0.0;
	server->binder_pos_vel = 0.0;
	server->binder_pos_moving = 0;
	int n = desk_book_matches(server->book_q, NULL, 0);
	server->book_sel = server->book_q[0] && n > 0 ? 0 : -1;
	server->binder_sel = -1;
	book_changed(server);
}

static void book_clear_ime(struct desk_server *server)
{
	server->book_pre[0] = '\0';
	server->book_pre_caret = 0;
	server->book_ncand = 0;
	server->book_keyq_n = 0;
}

static void book_insert(struct desk_server *server, const char *t)
{
	size_t n = strlen(server->book_q), k = strlen(t);
	if (!k || n + k >= sizeof server->book_q - 1) {
		return;
	}
	int c = server->book_caret;
	memmove(server->book_q + c + k, server->book_q + c, n - (size_t)c + 1);
	memcpy(server->book_q + c, t, k);
	server->book_caret = c + (int)k;
}

static int utf8_prev(const char *s, int i);
static int utf8_next(const char *s, int i);
static void book_take(struct desk_server *server, int i);

/* Move the line chosen with the keys d along the index, and open the book
 * where it is. */
static void book_choose(struct desk_server *server, int d)
{
	int n = desk_book_matches(server->book_q, NULL, 0);
	if (n <= 0) {
		return;
	}
	int sel = server->book_sel < 0 ? (d > 0 ? 0 : n - 1) : server->book_sel + d;
	sel = sel < 0 ? 0 : (sel >= n ? n - 1 : sel);
	server->book_sel = sel;
	int sp = desk_book_spread_of(server->desk_w, server->desk_h, server->book_q, sel);
	if (sp >= 0 && sp != (int)lround(server->binder_pos_to)) {
		binder_turn_to(server, sp);
	}
	book_changed(server);
}

/* a key the input method did not take, or there is no input method */
static void book_plain(struct desk_server *server, uint32_t sym, const char *utf8,
	uint32_t mods)
{
	char *q = server->book_q;
	int n = (int)strlen(q), c = server->book_caret;
	switch (sym) {
	case XKB_KEY_Escape:
		if (q[0] || server->book_pre[0]) {
			q[0] = '\0';
			server->book_caret = 0;
			if (server->book_ime) {
				desk_ime_typing_reset();
			}
			book_clear_ime(server);
			book_written(server);
		} else {
			binder_close(server);
		}
		return;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter: {
		int sel = server->book_sel;
		if (sel < 0 && q[0] && desk_book_matches(q, NULL, 0) > 0) {
			sel = 0;
		}
		if (sel >= 0) {
			book_take(server, sel);
		}
		return;
	}
	case XKB_KEY_BackSpace:
		if (c > 0) {
			int p = utf8_prev(q, c);
			memmove(q + p, q + c, (size_t)(n - c + 1));
			server->book_caret = p;
			book_written(server);
		}
		return;
	case XKB_KEY_Delete:
		if (c < n) {
			int e = utf8_next(q, c);
			memmove(q + c, q + e, (size_t)(n - e + 1));
			book_written(server);
		}
		return;
	case XKB_KEY_Left:
		if (q[0]) {
			server->book_caret = utf8_prev(q, c);
			book_changed(server);
		} else {
			binder_riffle(server, -1);
		}
		return;
	case XKB_KEY_Right:
		if (q[0]) {
			server->book_caret = utf8_next(q, c);
			book_changed(server);
		} else {
			binder_riffle(server, 1);
		}
		return;
	case XKB_KEY_Home:
		server->book_caret = 0;
		book_changed(server);
		return;
	case XKB_KEY_End:
		server->book_caret = n;
		book_changed(server);
		return;
	case XKB_KEY_Up:
		book_choose(server, -1);
		return;
	case XKB_KEY_Down:
		book_choose(server, 1);
		return;
	case XKB_KEY_Page_Up:
		binder_riffle(server, -1);
		return;
	case XKB_KEY_Page_Down:
		binder_riffle(server, 1);
		return;
	default:
		if (utf8 && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f
			&& !(mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))) {
			book_insert(server, utf8);
			book_written(server);
		}
		return;
	}
}

/* what the input method sends back */

static void book_ime_commit(const char *text, void *user)
{
	struct desk_server *server = user;
	if (server->shelf_binder != DESK_SHELF_BOOK) {
		return;
	}
	book_insert(server, text);
	server->book_pre[0] = '\0';
	book_written(server);
}

static void book_ime_preedit(const char *text, int cursor, bool visible, void *user)
{
	struct desk_server *server = user;
	snprintf(server->book_pre, sizeof server->book_pre, "%s", visible ? text : "");
	/* its caret is counted in letters */
	int b = 0;
	for (int i = 0; i < cursor && server->book_pre[b]; i++) {
		b = utf8_next(server->book_pre, b);
	}
	server->book_pre_caret = b;
	if (server->shelf_binder == DESK_SHELF_BOOK) {
		book_changed(server);
	}
}

static void book_ime_lookup(const char *const *cands, int n, int cursor, int page, bool visible,
	void *user)
{
	struct desk_server *server = user;
	server->book_ncand = 0;
	if (visible && n > 0) {
		/* the page the picked one is on */
		page = page > 0 ? (page > 16 ? 16 : page) : 9;
		cursor = cursor < 0 ? 0 : (cursor >= n ? n - 1 : cursor);
		int first = cursor / page * page;
		for (int i = first; i < n && server->book_ncand < page; i++) {
			snprintf(server->book_cand[server->book_ncand], sizeof server->book_cand[0], "%s",
				cands[i]);
			server->book_cand_p[server->book_ncand] = server->book_cand[server->book_ncand];
			server->book_ncand++;
		}
		server->book_cand_cursor = cursor - first;
		server->book_cand_page = page;
	}
	if (server->shelf_binder == DESK_SHELF_BOOK) {
		book_changed(server);
	}
}

static void book_ime_key(uint32_t sym, uint32_t keycode, uint32_t mods, bool taken, bool answer,
	void *user)
{
	(void)keycode;
	struct desk_server *server = user;
	char utf8[8] = "";
	if (answer) {
		/* the oldest key handed over: what it would type */
		if (server->book_keyq_n > 0) {
			memcpy(utf8, server->book_keyq[0].utf8, sizeof utf8);
			server->book_keyq_n--;
			memmove(&server->book_keyq[0], &server->book_keyq[1],
				sizeof server->book_keyq[0] * (size_t)server->book_keyq_n);
		}
	} else {
		xkb_keysym_to_utf8(sym, utf8, sizeof utf8);
	}
	if (!taken && server->shelf_binder == DESK_SHELF_BOOK) {
		book_plain(server, sym, utf8, mods);
	}
}

/* in and out, with binder_open and binder_close */

static void book_begin(struct desk_server *server)
{
	server->book_q[0] = '\0';
	server->book_caret = 0;
	book_clear_ime(server);
	server->book_sel = -1;
	server->book_tab = -1;
	memset(server->book_cursor, 0, sizeof server->book_cursor);
	static const struct desk_ime_typing hooks = { book_ime_commit, book_ime_preedit,
		book_ime_lookup, book_ime_key, NULL };
	struct desk_ime_typing h = hooks;
	h.user = server;
	server->book_ime = desk_ime_typing_begin(&h);
	book_tell_cursor(server);
	things_text_wanted(server, true);
}

static void book_end(struct desk_server *server)
{
	/* by who has the typing, not by what beginning it said: it is ours
	 * even when the input method was not there yet to type through */
	if (desk_ime_typing_owner() == server) {
		desk_ime_typing_end();
	}
	server->book_ime = false;
	book_clear_ime(server);
	server->book_tab = -1;
	things_text_wanted(server, false);
}

/* the keys and the pointer, while it is open */

static bool book_has_keys(const struct desk_server *server)
{
	return shelf_up(server) && server->shelf_binder == DESK_SHELF_BOOK
		&& server->binder_to > 0.5;
}

static void book_key(struct desk_server *server, struct xkb_state *xkb, uint32_t keycode,
	uint32_t sym, uint32_t mods, bool released)
{
	char utf8[8] = "";
	xkb_state_key_get_utf8(xkb, keycode + 8, utf8, sizeof utf8);
	if (released) {
		if (server->book_ime) {
			desk_ime_typing_key(sym, keycode, mods, true);
		}
		return;
	}
	if (server->book_ime && desk_ime_typing_key(sym, keycode, mods, false)) {
		if (server->book_keyq_n < (int)(sizeof server->book_keyq / sizeof server->book_keyq[0])) {
			memcpy(server->book_keyq[server->book_keyq_n].utf8, utf8, sizeof utf8);
			server->book_keyq_n++;
		}
		return;
	}
	book_plain(server, sym, utf8, mods);
}

/* Take line i of the index, as a binder's sheet is taken: a sheet comes up
 * off the page at the line's mark and goes down to the desk with you. */
static void book_take(struct desk_server *server, int i)
{
	static int idx[2048];
	int n = desk_book_matches(server->book_q, idx, 2048);
	if (i < 0 || i >= n || i >= 2048) {
		return;
	}
	const struct desk_app_list *l = desk_apps_acquire();
	bool taken = false;
	if (idx[i] < l->n) {
		const struct desk_app *app = &l->apps[idx[i]];
		wlr_log(WLR_INFO, "taking %s out of the book", app->id);
		struct desk_book_look lk;
		book_look(server, &lk);
		double cx, cy, ew, eh, a;
		desk_book_entry_pose(server->desk_w, server->desk_h, &lk, i, &cx, &cy, &ew, &eh, &a);
		/* the sheet a binder would have for it, printed at a binder's size
		 * and starting out as small as the line's mark */
		double sx, sy, sw, sh, sa;
		desk_binder_sheet_pose(server->desk_w, server->desk_h, 0.0, false, &sx, &sy, &sw,
			&sh, &sa);
		/* numbered as its sheet in the binder it is filed in is, and as the
		 * book's plate for it has it */
		const struct desk_shelf_info *si = &l->shelf[app->shelf];
		char code[32];
		char key[4] = { 0 };
		for (int k = 0; k < 3 && si->key[k]; k++) {
			key[k] = (char)toupper((unsigned char)si->key[k]);
		}
		snprintf(code, sizeof code, "%s\xc2\xb7%03d", key, idx[i] - si->first + 1);
		const struct desk_thing *own = app_own_thing(app);
		cairo_surface_t *face = !own && server->take.base < 0 && server->conf.take_sheet
			? desk_app_sheet_surface(app->id, sw, sh, code) : NULL;
		int b = face ? base_new(server, app, face, sw, sh) : -1;
		if (b >= 0) {
			double now = monotonic_sec();
			double w0 = eh * 1.6;
			snd_at(server, DESK_SND_SHEET_TAKE, cx, 1.0);
			server->take.base = b;
			server->take.idx = -1;
			server->take.start = now;
			server->take.turn_at = now + TAKE_TURN;
			server->take.land_at = now + TAKE_TURN + 0.40;
			server->take.turned = false;
			server->take.cx0 = cx - (ew - w0) * 0.5 * cos(a);
			server->take.cy0 = cy - (ew - w0) * 0.5 * sin(a);
			server->take.w0 = w0;
			server->take.h0 = w0 * sh / fmax(sw, 1.0);
			server->take.a0 = a;
			taken = true;
		}
		pid_t sid = own ? (thing_out(server, own), 0) : desk_app_launch(app);
		if (b >= 0) {
			server->bases[b].sid = sid > 0 ? sid : 0;
		}
	}
	desk_apps_release(l);
	/* You turn to the desk with the sheet in your hand: the book is put
	 * back while you are not looking (shelf_tick), not shut in front of you
	 * as you go -- a moment you would not see the end of. Meanwhile it
	 * takes no more keys or clicks. */
	book_end(server);
	server->binder_sel = -1;
	server->binder_to = 0.0;
	server->binder_putback = true;
	if (!taken && server->shelf_t > 0.5) {
		shelf_toggle(server);
	}
	paint_shelf(server);
	server_schedule_frames(server);
}

/* the book or binder left open, back on the shelf at once */
static void binder_put_back(struct desk_server *server)
{
	server->binder_putback = false;
	if (server->shelf_binder < 0) {
		return;
	}
	binder_off_hand(server);
	desk_binder_forget();
	book_kept_forget(server);
	server->binder_t = 0.0;
	server->binder_to = 0.0;
	server->binder_moving = 0;
	server->binder_sel = -1;
	paint_shelf(server);
}

static void book_hover_at(struct desk_server *server, double x, double y)
{
	if (server->binder_to < 0.5) {
		return;
	}
	struct desk_book_look lk;
	book_look(server, &lk);
	int arg = -1;
	int z = server->binder_moving ? DESK_BOOK_HIT_NONE
		: desk_book_hit(server->desk_w, server->desk_h, &lk, x, y, &arg);
	bool flag = z == DESK_BOOK_HIT_FLAG && !server->binder_pos_moving;
	int sel = (z == DESK_BOOK_HIT_ENTRY || flag) && !server->binder_pos_moving ? arg : -1;
	int tab = z == DESK_BOOK_HIT_TAB ? arg : -1;
	if (sel != server->binder_sel || tab != server->book_tab || flag != server->book_flag) {
		server->binder_sel = sel;
		server->book_tab = tab;
		server->book_flag = flag;
		paint_shelf(server);
		server_schedule_frames(server);
	}
}

/* The flag in the margin of line i of the index: stuck on, or peeled off,
 * as on the program's sheet in its binder (bnc_press). */
static void book_flag_toggle(struct desk_server *server, int i)
{
	static int idx[2048];
	int n = desk_book_matches(server->book_q, idx, 2048);
	if (i < 0 || i >= n || i >= 2048) {
		return;
	}
	const struct desk_app_list *l = desk_apps_acquire();
	char id[96] = "";
	bool on = false;
	if (idx[i] >= 0 && idx[i] < l->n) {
		snprintf(id, sizeof id, "%s", l->apps[idx[i]].id);
		on = !l->apps[idx[i]].fav;
	}
	desk_apps_release(l);
	if (!id[0]) {
		return;
	}
	desk_sound_play(on ? DESK_SND_PAPER_PRESS : DESK_SND_CORNER_LIFT, 0.2, 1.0, 0.0);
	wlr_log(WLR_INFO, "favourites: %s %s (the index)", id, on ? "flagged" : "unflagged");
	if (!desk_apps_fav_set(id, on, false)) {
		wlr_log(WLR_ERROR, "favourites: the file could not be written");
	}
	paint_shelf(server);
	server_schedule_frames(server);
}

static void book_press(struct desk_server *server, double x, double y)
{
	if (server->binder_moving || server->binder_to < 0.5) {
		return;
	}
	struct desk_book_look lk;
	book_look(server, &lk);
	int arg = -1;
	int z = desk_book_hit(server->desk_w, server->desk_h, &lk, x, y, &arg);
	switch (z) {
	case DESK_BOOK_HIT_ENTRY:
		book_take(server, arg);
		break;
	case DESK_BOOK_HIT_FLAG:
		book_flag_toggle(server, arg);
		break;
	case DESK_BOOK_HIT_TAB: {
		int sp = desk_book_tab_spread(server->desk_w, server->desk_h, server->book_q, arg);
		if (sp >= 0) {
			binder_turn_to(server, sp);
		}
		break;
	}
	case DESK_BOOK_HIT_PREV:
		binder_riffle(server, -1);
		break;
	case DESK_BOOK_HIT_NEXT:
		binder_riffle(server, 1);
		break;
	case DESK_BOOK_HIT_NONE:
		binder_close(server);
		break;
	default:
		break;
	}
}

/* ---- a binder sheet's corner -------------------------------------
 *
 * The sheet the hand is at has its top right corner turned down, as a
 * sheet on the desk and a letter in the post box have: the hand resting on
 * its fold stirs it, held there or clicked it turns up, and on its back
 * are what can be done with the program printed on the sheet -- open it,
 * or put it in the drawer (take it out, if it is there already). */

static bool drawer_has_app(const struct desk_server *server, const char *id);
static void drawer_keep_app(struct desk_server *server, const char *id, bool keep);

static void bnc_reset(struct desk_server *server)
{
	server->bnc_idx = -1;
	server->bnc_s = 0.0;
	server->bnc_v = 0.0;
	server->bnc_at = 0.0;
	server->bnc_lift = 0.0;
	server->bnc_near = false;
	server->bnc_hover_at = 0.0;
	server->bnc_hot = -1;
	server->bnc_act = -1;
	server->bnc_flag = false;
}

/* The program printed on sheet idx of the open binder. */
static bool binder_app_id(struct desk_server *server, int idx, char *out, size_t n)
{
	bool ok = false;
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = desk_shelf_is_binder(server->shelf_binder)
		? desk_shelf_app(l, server->shelf_binder, idx) : NULL;
	if (app) {
		snprintf(out, n, "%s", app->id);
		ok = true;
	}
	desk_apps_release(l);
	return ok;
}

static bool bnc_in_drawer(struct desk_server *server, int idx)
{
	char id[96];
	return idx >= 0 && binder_app_id(server, idx, id, sizeof id) && drawer_has_app(server, id);
}

/* Where the hand is on the corner of the sheet it is at. */
static void bnc_local(struct desk_server *server, double *dx, double *ly, double *lx)
{
	double sw, sh;
	desk_binder_local(server->desk_w, server->desk_h, server->binder_pos, server->bnc_idx,
		true, server->cursor->x, server->cursor->y, lx, ly, &sw, &sh);
	*dx = sw - *lx;
}

static void bnc_track(struct desk_server *server)
{
	if (server->bnc_act >= 0 || server->binder_pos_moving || server->shelf_binder < 0) {
		return;
	}
	int sel = server->binder_sel;
	double rest, open;
	desk_binder_curl_sizes(server->desk_w, server->desk_h, &rest, &open);
	if (sel >= 0 && sel != server->bnc_idx) {
		bnc_reset(server);
		server->bnc_idx = sel;
		server->bnc_s = rest;
	}
	bool near = false, on_fold = false, flag = false;
	int hot = -1;
	if (sel >= 0) {
		double dx, ly, lx;
		bnc_local(server, &dx, &ly, &lx);
		flag = !server->bnc_near && desk_binder_flag_hit(server->desk_w, server->desk_h, lx, ly);
		double zone = rest * 2.6;
		on_fold = dx >= -8.0 && dx <= zone && ly >= -8.0 && ly <= zone;
		near = server->bnc_near && dx >= -8.0 && dx <= open * 1.30 && ly >= -8.0
			&& ly <= open * 1.30;
		if (near) {
			hot = desk_binder_curl_hit(server->desk_w, server->desk_h, server->bnc_s,
				bnc_in_drawer(server, sel), lx, ly);
		}
	}
	if (!near && on_fold) {
		if (server->bnc_hover_at <= 0.0) {
			server->bnc_hover_at = real_sec();
		}
	} else {
		server->bnc_hover_at = 0.0;
	}
	if (near != server->bnc_near || hot != server->bnc_hot || on_fold
		|| flag != server->bnc_flag) {
		server->bnc_near = near;
		server->bnc_hot = hot;
		server->bnc_flag = flag;
		paint_shelf(server);
		server_schedule_frames(server);
	}
}

/* A click at the open binder: on the back of the turned-up corner, on the
 * corner's fold, or neither (false: the sheet is taken). */
static bool bnc_press(struct desk_server *server)
{
	if (server->bnc_act >= 0) {
		return true;
	}
	int sel = server->binder_sel;
	if (sel < 0 || sel != server->bnc_idx) {
		return false;
	}
	if (server->bnc_flag) {
		/* the favourites' flag: stuck on, or peeled off -- which, in the
		 * favourites binder itself, leaves the sheet in it until it goes
		 * back on the shelf */
		const struct desk_app_list *l = desk_apps_acquire();
		const struct desk_app *app = desk_shelf_app(l, server->shelf_binder, sel);
		char id[96] = "";
		bool on = app && !app->fav;
		if (app) {
			snprintf(id, sizeof id, "%s", app->id);
		}
		desk_apps_release(l);
		if (id[0]) {
			desk_sound_play(on ? DESK_SND_PAPER_PRESS : DESK_SND_CORNER_LIFT, 0.2, 1.0, 0.0);
			wlr_log(WLR_INFO, "favourites: %s %s", id, on ? "flagged" : "unflagged");
			if (!desk_apps_fav_set(id, on, server->shelf_binder == DESK_SHELF_FAV)) {
				wlr_log(WLR_ERROR, "favourites: the file could not be written");
			}
			paint_shelf(server);
			server_schedule_frames(server);
		}
		return true;
	}
	if (server->bnc_near && server->bnc_hot >= 0) {
		server->bnc_act = server->bnc_hot;
		desk_sound_play(DESK_SND_CORNER_TAP, 0.2, 1.0, 0.0);
		server->bnc_act_at = monotonic_sec();
		if (server->bnc_act == DESK_BINDER_DRAWER) {
			char id[96];
			if (binder_app_id(server, sel, id, sizeof id)) {
				drawer_keep_app(server, id, !drawer_has_app(server, id));
			}
		}
		paint_shelf(server);
		server_schedule_frames(server);
		return true;
	}
	double rest, open, dx, ly, lx;
	desk_binder_curl_sizes(server->desk_w, server->desk_h, &rest, &open);
	bnc_local(server, &dx, &ly, &lx);
	if (server->bnc_near) {
		/* on the back of the flap, between the words: nothing */
		return dx >= -8.0 && dx <= open && ly >= -8.0 && ly <= open;
	}
	if (dx >= -8.0 && dx <= rest * 2.6 && ly >= -8.0 && ly <= rest * 2.6) {
		desk_sound_play(DESK_SND_CORNER_LIFT, 0.2, 1.0, 0.0);
		server->bnc_near = true;
		server->bnc_hover_at = 0.0;
		server->bnc_hot = -1;
		server_schedule_frames(server);
		return true;
	}
	return false;
}

/* Once a frame: the corner goes where it is going, on the spring a sheet's
 * corner has, and what was pressed on it is done. */
static void bnc_tick(struct desk_server *server)
{
	if (server->bnc_idx < 0) {
		return;
	}
	if (server->shelf_binder < 0 || !shelf_up(server)) {
		bnc_reset(server);
		return;
	}
	double rest, open;
	desk_binder_curl_sizes(server->desk_w, server->desk_h, &rest, &open);
	double now = monotonic_sec();
	bool more = false;
	if (!server->bnc_near && server->bnc_hover_at > 0.0) {
		if (real_sec() - server->bnc_hover_at >= 0.5 && server->conf.corner_hover) {
			desk_sound_play(DESK_SND_CORNER_LIFT, 0.2, 1.0, 0.0);
			server->bnc_near = true;
			server->bnc_hover_at = 0.0;
		} else {
			more = true;
		}
	}
	double to = server->bnc_near ? open : (server->bnc_hover_at > 0.0 ? rest * 1.7 : rest);
	if (server->bnc_act >= 0) {
		double e = now - server->bnc_act_at;
		if (server->bnc_act == DESK_BINDER_OPEN) {
			/* laid down again, and the sheet is taken */
			to = rest;
			if (e > 0.14) {
				int idx = server->bnc_idx;
				bnc_reset(server);
				binder_take(server, idx);
				return;
			}
		} else {
			/* a nod of the corner: it is done, and the words have changed */
			to = open * 0.86;
			if (e > 0.16) {
				server->bnc_act = -1;
			}
		}
		more = true;
	}
	double s0 = server->bnc_s;
	int hot0 = server->bnc_hot;
	double dt = server->bnc_at > 0.0 ? clampf_d(now - server->bnc_at, 0.0, 0.05) : 0.0;
	server->bnc_at = now;
	if (server->bnc_s <= 0.0) {
		server->bnc_s = rest;
	}
	const double k = 240.0, c = 2.0 * 0.60 * 15.49;
	double acc = k * (to - server->bnc_s) - c * server->bnc_v;
	server->bnc_v += acc * dt;
	server->bnc_s += server->bnc_v * dt;
	if (gfx_flat()) {
		server->bnc_s = to;
		server->bnc_v = 0.0;
	}
	if (fabs(to - server->bnc_s) < 0.15 && fabs(server->bnc_v) < 2.0) {
		server->bnc_s = to;
		server->bnc_v = 0.0;
	} else {
		more = true;
	}
	server->bnc_lift = clampf_d((server->bnc_s - rest) / fmax(open - rest, 1.0), 0.0, 1.0);
	/* the words move with the flap, under a pointer that may not */
	if (server->bnc_near && server->bnc_act < 0 && server->binder_sel == server->bnc_idx) {
		double dx, ly, lx;
		bnc_local(server, &dx, &ly, &lx);
		server->bnc_hot = desk_binder_curl_hit(server->desk_w, server->desk_h,
			server->bnc_s, bnc_in_drawer(server, server->bnc_idx), lx, ly);
	}
	if (server->bnc_s != s0 || server->bnc_hot != hot0) {
		paint_shelf(server);
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* ---- the desk calendar, close up ------------------------------------
 *
 * Clicked on the shelf, you step up to the bookcase and take it: the view
 * closes in a little on where it stood, the bookcase going soft as the eye
 * comes to the calendar in your hand, which comes up to the middle of the
 * view -- the picture drawn close up, laid over the little one on the
 * shelf at first and scaled up from there. The bookcase is a picture too,
 * taken as you reach for it, sharp and blurred: the step in is both
 * scaled about the calendar, and the one fading into the other. All of it
 * moves are the scene graph's, and cost no drawing. A click on the top
 * half of the page brings the last month back over the wire, on the
 * bottom half sends this one over to the next; anywhere else puts it
 * back. The edge the pointer is at stands up a little, the way a page
 * does when a finger is put under it. */

#define CAL_OPEN_S 0.42
#define CAL_SHUT_S 0.34
#define CAL_TURN_S 0.52
#define CAL_LIFT_S 0.14
#define CAL_SWITCH_S 0.56  /* a bundle over, and a little longer the thicker */
#define CAL_STEP_IN 0.09   /* how much nearer the bookcase comes */
#define CAL_HELD_YAW 0.30  /* held up on its own: turned, its right side away */
#define CAL_ASIDE_YAW 0.10 /* and with the binder beside it, turned to it */
#define CAL_HELD_PITCH 0.12 /* leaning back a little */
