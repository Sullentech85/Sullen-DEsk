/* Part of wm.c: windows spread over the desk and Alt+Tab.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* The windows' own layers, hidden while their pictures are spread. */
static void spread_hide_windows(struct desk_server *server, bool hide)
{
	struct wlr_scene_tree *l[] = {
		server->layer_folded, server->layer_win, server->layer_full, server->layer_xover,
	};
	for (size_t i = 0; i < sizeof l / sizeof l[0]; i++) {
		wlr_scene_node_set_enabled(&l[i]->node, !hide);
	}
}

/* Where a window lies on the screen: an open one, its sheet; a folded one,
 * the picture shrunk into where the packet lies, or, in the drawer, below
 * the front of the desk. */
static void spread_home(struct desk_server *server, struct desk_toplevel *view,
	struct sw_paper *p, bool open)
{
	struct desk_sheet *s = &view->sheet;
	if (open || s->phase == SHEET_IDLE || !s->tree || !s->tree->node.parent) {
		int lx = 0, ly = 0;
		if (view->scene_tree) {
			wlr_scene_node_coords(&view->scene_tree->node, &lx, &ly);
		}
		p->hx = lx;
		p->hy = ly;
		p->hw = view->outer_w > 1 ? view->outer_w : p->iw;
		p->hh = view->outer_h > 1 ? view->outer_h : p->ih;
		return;
	}
	double bx, by, bw, bh;
	if (s->in_drawer) {
		bw = server->desk_w * 0.08;
		bh = bw;
		bx = server->desk_w * 0.5 - bw * 0.5;
		by = server->desk_h;
	} else {
		struct wlr_scene_node *in = s->tree->node.parent ? &s->tree->node.parent->node : NULL;
		bx = s->hit_x + (in ? in->x : 0);
		by = s->hit_y + (in ? in->y : 0);
		bw = s->hit_w;
		bh = s->hit_h;
	}
	double k = fmin(bw / fmax(p->iw, 1), bh / fmax(p->ih, 1));
	p->hw = p->iw * k;
	p->hh = p->ih * k;
	p->hx = bx + (bw - p->hw) * 0.5;
	p->hy = by + (bh - p->hh) * 0.5;
}

/* Where each goes in the spread, over box (a screen): in rows, as many
 * across as lets them be biggest, none made bigger than its sheet (a
 * folded one's, opened: iw x ih); the rows
 * taken top to bottom as they lie, and each row left to right. */
static void spread_lay(struct desk_server *server, struct wlr_box box)
{
	int n = server->sw_n;
	double pt = desk_px_per_pt(box.width);
	double gap = 28.0 * pt;   /* between them */
	double ax = box.x + 40.0 * pt, ay = box.y + 96.0 * pt;
	double aw = fmax(box.width - 80.0 * pt, 120.0), ah = fmax(box.height - 136.0 * pt, 120.0);
	int best_c = 1;
	double best = -1.0, cw = aw, ch = ah;
	for (int c = 1; c <= n; c++) {
		int r = (n + c - 1) / c;
		double w = (aw - gap * (c - 1)) / c, h = (ah - gap * (r - 1)) / r;
		if (w < 8.0 || h < 8.0) {
			continue;
		}
		double area = 0.0;
		for (int i = 0; i < n; i++) {
			const struct sw_paper *p = &server->sw_paper[i];
			double s = fmin(1.0, fmin(w / p->iw, h / p->ih));
			area += s * s * p->iw * p->ih;
		}
		if (area > best * 1.0001) {
			best = area;
			best_c = c;
			cw = w;
			ch = h;
		}
	}
	int c = best_c, r = (n + c - 1) / c;
	int order[SWITCHER_MAX];
	for (int i = 0; i < n; i++) {
		order[i] = i;
	}
	/* top to bottom by their middles, and each row left to right */
	for (int i = 1; i < n; i++) {
		int v = order[i], j = i;
		const struct sw_paper *pv = &server->sw_paper[v];
		while (j > 0) {
			const struct sw_paper *pj = &server->sw_paper[order[j - 1]];
			if (pj->hy + pj->hh * 0.5 <= pv->hy + pv->hh * 0.5) {
				break;
			}
			order[j] = order[j - 1];
			j--;
		}
		order[j] = v;
	}
	double rows_h[SWITCHER_MAX];
	double total = gap * (r - 1);
	int first = 0;
	for (int row = 0; row < r; row++) {
		int m = n / r + (row < n % r ? 1 : 0);
		for (int i = first + 1; i < first + m; i++) {
			int v = order[i], j = i;
			while (j > first && server->sw_paper[order[j - 1]].hx > server->sw_paper[v].hx) {
				order[j] = order[j - 1];
				j--;
			}
			order[j] = v;
		}
		double rh = 0.0;
		for (int i = first; i < first + m; i++) {
			struct sw_paper *p = &server->sw_paper[order[i]];
			double s = fmin(1.0, fmin(cw / p->iw, ch / p->ih));
			p->sw = fmax(p->iw * s, 1.0);
			p->sh = fmax(p->ih * s, 1.0);
			rh = fmax(rh, p->sh);
		}
		rows_h[row] = rh;
		total += rh;
		first += m;
	}
	double y = ay + fmax((ah - total) * 0.5, 0.0);
	first = 0;
	for (int row = 0; row < r; row++) {
		int m = n / r + (row < n % r ? 1 : 0);
		double rw = gap * (m - 1);
		for (int i = first; i < first + m; i++) {
			rw += server->sw_paper[order[i]].sw;
		}
		double x = ax + (aw - rw) * 0.5;
		for (int i = first; i < first + m; i++) {
			struct sw_paper *p = &server->sw_paper[order[i]];
			p->sx = x;
			p->sy = y + (rows_h[row] - p->sh) * 0.5;
			x += p->sw + gap;
		}
		y += rows_h[row] + gap;
		first += m;
	}
}

/* A picture made into a buffer of ours, at w x h. */
static struct wlr_buffer *spread_picture(cairo_surface_t *src, int w, int h)
{
	struct desk_buf *buf = buf_create(w, h);
	double sw = cairo_image_surface_get_width(src), sh = cairo_image_surface_get_height(src);
	cairo_scale(buf->cr, w / fmax(sw, 1.0), h / fmax(sh, 1.0));
	cairo_set_source_surface(buf->cr, src, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(buf->cr), CAIRO_FILTER_GOOD);
	cairo_paint(buf->cr);
	cairo_surface_flush(buf->surface);
	return &buf->base;
}

/* The pictures one over another as the sheets lie: the folded under the
 * open, the open as they are stacked, and top (if any) over all. */
static void spread_restack(struct desk_server *server, struct desk_toplevel *top)
{
	for (int i = 0; i < server->sw_n; i++) {
		struct desk_toplevel *v = server->sw_view[i];
		if (server->sw_paper[i].tree && v->sheet.phase != SHEET_IDLE && v != top) {
			wlr_scene_node_raise_to_top(&server->sw_paper[i].tree->node);
		}
	}
	struct desk_toplevel *v;
	wl_list_for_each_reverse(v, &server->toplevels, link) {
		for (int i = 0; i < server->sw_n; i++) {
			if (server->sw_view[i] == v && v->sheet.phase == SHEET_IDLE && v != top
				&& server->sw_paper[i].tree) {
				wlr_scene_node_raise_to_top(&server->sw_paper[i].tree->node);
			}
		}
	}
	for (int i = 0; i < server->sw_n; i++) {
		if (top && server->sw_view[i] == top && server->sw_paper[i].tree) {
			wlr_scene_node_raise_to_top(&server->sw_paper[i].tree->node);
		}
	}
}

/* The one it is on is another: lifted, and the rest dimmed, as it goes
 * (switcher_tick). */
static void switcher_mark(struct desk_server *server)
{
	if (server->sw_shown) {
		server_schedule_frames(server);
	}
}

/* Spread them out: the pictures made, laid out, and set going. */
static void switcher_spread(struct desk_server *server)
{
	if (server->sw_shown || server->sw_n < 1) {
		return;
	}
	double t0 = real_sec();
	struct wlr_output *out = screen_at(server, server->cursor->x, server->cursor->y);
	struct wlr_box box = { 0, 0, server->desk_w, server->desk_h };
	if (out) {
		wlr_output_layout_get_box(server->output_layout, out, &box);
	}
	for (int i = 0; i < server->sw_n; i++) {
		struct desk_toplevel *view = server->sw_view[i];
		struct sw_paper *p = &server->sw_paper[i];
		memset(p, 0, sizeof *p);
		cairo_surface_t *src = view->sheet.phase != SHEET_IDLE && view->sheet.src
			? cairo_surface_reference(view->sheet.src) : view_snapshot(view);
		p->iw = cairo_image_surface_get_width(src);
		p->ih = cairo_image_surface_get_height(src);
		spread_home(server, view, p, false);
		p->full = spread_picture(src, p->iw, p->ih);
		p->src = src;
	}
	spread_lay(server, box);
	server->sw_shown = true;
	server->sw_back = false;
	server->sw_k = server->sw_k0 = 0.0;
	server->sw_at = server->sw_ticked = monotonic_sec();
	struct wlr_box all;
	wlr_output_layout_get_box(server->output_layout, NULL, &all);
	static const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	server->sw_veil = wlr_scene_rect_create(server->layer_switcher,
		all.width > 0 ? all.width : server->desk_w, all.height > 0 ? all.height : server->desk_h,
		clear);
	if (server->sw_veil) {
		wlr_scene_node_set_position(&server->sw_veil->node, all.x, all.y);
	}
	int pad = desk_shadow_sprite_pad();
	for (int i = 0; i < server->sw_n; i++) {
		struct sw_paper *p = &server->sw_paper[i];
		/* drawn again at the size it will lie at, picked up */
		int w = (int)ceil(p->sw * 1.06), h = (int)ceil(p->sh * 1.06);
		p->small = spread_picture(p->src, w, h);
		cairo_surface_destroy(p->src);
		p->src = NULL;
		p->tree = wlr_scene_tree_create(server->layer_switcher);
		if (!p->tree) {
			continue;
		}
		if (!gfx_flat()) {
			double side, amount;
			desk_paper_lift(view_title(server->sw_view[i]), &side, &amount);
			p->shw = (int)ceil(p->sw);
			p->shh = (int)ceil(p->sh);
			struct desk_buf *sb = buf_create(p->shw + pad * 2, p->shh + pad * 2);
			desk_draw_shadow_sprite(sb->cr, p->shw, p->shh, 7.0, side, amount);
			scene_put(&p->shadow, p->tree, sb, 0, 0, NULL);
			p->shadow->point_accepts_input = shadow_accepts;
		}
		p->img = scene_new(p->tree, p->full);
		if (p->img) {
			p->img->point_accepts_input = shadow_accepts;
		}
		/* its label, on it near the foot, the same size however big the
		 * sheet is (and under the dark the ones not chosen have) */
		char name[128], initial[8];
		cairo_surface_t *icon = view_app(server->sw_view[i], name, sizeof name);
		desk_initial_of(name, initial, sizeof initial);
		double pt = desk_px_per_pt(box.width);
		double most = fmax(p->sw - 20.0 * pt, 40.0 * pt);
		desk_switcher_tag_size(name, pt, most, &p->tag_w, &p->tag_h, &p->tag_pad);
		struct desk_buf *tb = buf_create(p->tag_w, p->tag_h);
		desk_draw_switcher_tag(tb->cr, icon, initial, name, pt, most,
			(unsigned)((uintptr_t)server->sw_view[i] >> 4) * 2654435761u);
		scene_put(&p->tag, p->tree, tb, 0, 0, NULL);
		p->tag->point_accepts_input = shadow_accepts;
		p->dim = wlr_scene_rect_create(p->tree, 1, 1, clear);
	}
	spread_restack(server, NULL);
	spread_hide_windows(server, true);
	/* nothing under the hand sees it meanwhile, and a game lets go of it */
	wlr_seat_pointer_clear_focus(server->seat);
	cursor_image(server, "default");
	desk_constraint_refresh(server);
	switcher_mark(server);
	wlr_log(WLR_INFO, "switcher: %d windows spread in %.1f ms", server->sw_n,
		(real_sec() - t0) * 1000.0);
	server_schedule_frames(server);
}

static int switcher_show_cb(void *data)
{
	struct desk_server *server = data;
	if (server->sw_on && !server->sw_shown) {
		switcher_spread(server);
	}
	return 0;
}

/* All of it gone at once, and the windows there again. */
static void switcher_close(struct desk_server *server)
{
	for (int i = 0; i < server->sw_n; i++) {
		sw_paper_free(&server->sw_paper[i]);
	}
	if (server->sw_shown) {
		spread_hide_windows(server, false);
	}
	if (server->sw_veil) {
		wlr_scene_node_destroy(&server->sw_veil->node);
		server->sw_veil = NULL;
	}
	server->sw_on = false;
	server->sw_shown = false;
	server->sw_back = false;
	server->sw_n = 0;
	server->sw_sel = 0;
	server->sw_go = NULL;
	if (server->sw_timer) {
		wl_event_source_timer_update(server->sw_timer, 0);
	}
	server_schedule_frames(server);
}

/* All back where they lie: the windows there again, and a folded one gone
 * to opened where its picture has been put down. */
static void switcher_done(struct desk_server *server)
{
	struct desk_toplevel *go = server->sw_go;
	switcher_close(server);
	if (go && go->sheet.phase != SHEET_IDLE) {
		struct desk_toplevel *current = view_of_surface(server,
			server->seat->keyboard_state.focused_surface);
		sheet_open_now(server, &go->sheet);
		switch_to(server, current, go);
	}
}

/* Alt let go of, Enter, a click (go), or Esc (do not). */
static void switcher_finish(struct desk_server *server, bool go)
{
	if (!server->sw_on) {
		return;
	}
	struct desk_toplevel *next = go && server->sw_sel >= 0 && server->sw_sel < server->sw_n
		? server->sw_view[server->sw_sel] : NULL;
	struct desk_toplevel *current = view_of_surface(server,
		server->seat->keyboard_state.focused_surface);
	if (!server->sw_shown) {
		switcher_close(server);
		if (next) {
			switch_to(server, current, next);
		}
		return;
	}
	server->sw_on = false;
	if (server->sw_timer) {
		wl_event_source_timer_update(server->sw_timer, 0);
	}
	/* an open one has the keys at once; a folded one once it is down */
	server->sw_go = next;
	if (next && next->sheet.phase == SHEET_IDLE) {
		switch_to(server, current, next);
	}
	/* back to where they lie now (one gone to, from the whole screen, back
	 * on its sheet), as they will lie, the one gone to over all */
	for (int i = 0; i < server->sw_n; i++) {
		struct desk_toplevel *v = server->sw_view[i];
		if (v == next || v->sheet.phase == SHEET_IDLE) {
			spread_home(server, v, &server->sw_paper[i], true);
		}
	}
	spread_restack(server, next);
	server->sw_back = true;
	server->sw_k0 = server->sw_k;
	server->sw_at = monotonic_sec();
	server_schedule_frames(server);
}

/* A frame of them going out or coming back, and of the one it is on being
 * picked up and the others put down. */
static void switcher_tick(struct desk_server *server)
{
	if (!server->sw_shown) {
		return;
	}
	double now = monotonic_sec();
	double u = clampf_d((now - server->sw_at) / SPREAD_S, 0.0, 1.0);
	double e = 1.0 - (1.0 - u) * (1.0 - u) * (1.0 - u);
	double k = server->sw_back ? server->sw_k0 * (1.0 - e) : e;
	if (server->sw_held) {
		k = clampf_d(server->sw_hand, 0.0, 1.0);
		u = 1.0;
	}
	server->sw_k = k;
	double dt = clampf_d(now - server->sw_ticked, 0.0, 0.05);
	server->sw_ticked = now;
	bool more = u < 1.0 || server->sw_held;
	int pad = desk_shadow_sprite_pad();
	for (int i = 0; i < server->sw_n; i++) {
		struct sw_paper *p = &server->sw_paper[i];
		double want = !server->sw_back && i == server->sw_sel ? 1.0 : 0.0;
		p->lift += (want - p->lift) * clampf_d(dt * 16.0, 0.0, 1.0);
		if (fabs(want - p->lift) < 0.004) {
			p->lift = want;
		} else {
			more = true;
		}
		double g = 1.0 + 0.06 * p->lift * k;
		double w = (p->hw + (p->sw - p->hw) * k) * g;
		double h = (p->hh + (p->sh - p->hh) * k) * g;
		double x = p->hx + (p->sx - p->hx) * k - (g - 1.0) * w * 0.5 / g;
		double y = p->hy + (p->sy - p->hy) * k - (g - 1.0) * h * 0.5 / g;
		p->x = x;
		p->y = y;
		p->w = w;
		p->h = h;
		if (!p->tree || !p->img) {
			continue;
		}
		/* lying in the spread, the picture drawn at its size; moving, whole */
		bool small = k >= 1.0 && !server->sw_back;
		if (small != p->small_on) {
			scene_swap(p->img, small ? p->small : p->full);
			p->small_on = small;
		}
		wlr_scene_node_set_position(&p->img->node, (int)lround(x), (int)lround(y));
		wlr_scene_buffer_set_dest_size(p->img, (int)lround(fmax(w, 1.0)),
			(int)lround(fmax(h, 1.0)));
		if (p->shadow) {
			double f = w / fmax(p->shw, 1.0), fy = h / fmax(p->shh, 1.0);
			double ox, oy;
			desk_shadow_offset(x + w * 0.5, y + h * 0.5, 44.0 + 70.0 * p->lift, &ox, &oy);
			wlr_scene_node_set_position(&p->shadow->node, (int)lround(x - pad * f + ox),
				(int)lround(y - pad * fy + oy));
			wlr_scene_buffer_set_dest_size(p->shadow,
				(int)lround(fmax((p->shw + pad * 2) * f, 1.0)),
				(int)lround(fmax((p->shh + pad * 2) * fy, 1.0)));
			wlr_scene_buffer_set_opacity(p->shadow, (float)(1.0 - 0.22 * p->lift));
		}
		if (p->tag) {
			double pt = desk_px_per_pt(server->desk_w);
			wlr_scene_node_set_position(&p->tag->node,
				(int)lround(x + w * 0.5 - p->tag_w * 0.5),
				(int)lround(y + h - 12.0 * pt - (p->tag_h - p->tag_pad)));
			wlr_scene_buffer_set_opacity(p->tag, (float)k);
		}
		if (p->dim) {
			const float c[4] = { 0.0f, 0.0f, 0.0f, (float)(0.24 * k * (1.0 - p->lift)) };
			wlr_scene_rect_set_color(p->dim, c);
			wlr_scene_rect_set_size(p->dim, (int)lround(fmax(w, 1.0)), (int)lround(fmax(h, 1.0)));
			wlr_scene_node_set_position(&p->dim->node, (int)lround(x), (int)lround(y));
		}
	}
	if (server->sw_veil) {
		const float c[4] = { 0.02f * (float)k, 0.015f * (float)k, 0.01f * (float)k,
			(float)(0.46 * k) };
		wlr_scene_rect_set_color(server->sw_veil, c);
	}
	/* the one it is on over the rest, once they are well apart */
	if (!server->sw_back && k > 0.6 && server->sw_sel >= 0 && server->sw_sel < server->sw_n
		&& server->sw_paper[server->sw_sel].tree) {
		struct wlr_scene_node *n = &server->sw_paper[server->sw_sel].tree->node;
		struct wlr_scene_node *last = wl_container_of(server->layer_switcher->children.prev,
			last, link);
		if (last != n) {
			wlr_scene_node_raise_to_top(n);
		}
	}
	if (more) {
		server_schedule_frames(server);
	} else if (server->sw_back) {
		switcher_done(server);
	}
}

/* Alt+Tab (dir 1) or Alt+Shift+Tab (-1): taken up, the first time; round
 * them, after. */
static void switcher_step(struct desk_server *server, int dir)
{
	if (!server->sw_on) {
		if (locked(server) || console_dying(server)) {
			return;
		}
		/* the last ones still going back: back, and on from there */
		if (server->sw_shown) {
			switcher_done(server);
		}
		struct desk_toplevel *current = view_of_surface(server,
			server->seat->keyboard_state.focused_surface);
		int n = 0;
		/* the one with the keys first, then as they were last used */
		if (current && current->scene_tree && !current->unmanaged) {
			server->sw_view[n++] = current;
		}
		struct desk_toplevel *v;
		wl_list_for_each(v, &server->toplevels, link) {
			if (n < SWITCHER_MAX && v != current && v->scene_tree && !v->unmanaged
				&& view_takes_keys(v)) {
				server->sw_view[n++] = v;
			}
		}
		if (n < 2) {
			/* nothing to choose between: as Super+Tab is */
			desk_switch_application(server);
			return;
		}
		server->sw_n = n;
		server->sw_on = true;
		server->sw_shown = false;
		/* with nobody having the keys, the first press is the first of them */
		server->sw_sel = current ? 0 : -1;
		if (!server->sw_timer) {
			server->sw_timer = wl_event_loop_add_timer(
				wl_display_get_event_loop(server->wl_display), switcher_show_cb, server);
		}
		if (server->sw_timer) {
			wl_event_source_timer_update(server->sw_timer, SWITCHER_SHOW_MS);
		}
		int m = server->sw_n;
		server->sw_sel = ((server->sw_sel + dir) % m + m) % m;
		return;
	}
	int n = server->sw_n;
	server->sw_sel = ((server->sw_sel + dir) % n + n) % n;
	if (!server->sw_shown) {
		/* gone round before they were spread: spread now */
		switcher_spread(server);
	} else {
		switcher_mark(server);
	}
}

/* An arrow: the nearest one that way from the one it is on, as they lie in
 * the spread. */
static void switcher_toward(struct desk_server *server, int dx, int dy)
{
	if (!server->sw_shown) {
		switcher_spread(server);
	}
	if (server->sw_sel < 0 || server->sw_sel >= server->sw_n) {
		return;
	}
	const struct sw_paper *a = &server->sw_paper[server->sw_sel];
	double ax = a->sx + a->sw * 0.5, ay = a->sy + a->sh * 0.5;
	int best = -1;
	double best_d = 0.0;
	for (int i = 0; i < server->sw_n; i++) {
		const struct sw_paper *b = &server->sw_paper[i];
		double ddx = b->sx + b->sw * 0.5 - ax, ddy = b->sy + b->sh * 0.5 - ay;
		double along = ddx * dx + ddy * dy;
		double across = fabs(ddx * dy) + fabs(ddy * dx);
		if (i == server->sw_sel || along < 1.0) {
			continue;
		}
		double d = along + across * 2.0;
		if (best < 0 || d < best_d) {
			best = i;
			best_d = d;
		}
	}
	if (best >= 0) {
		server->sw_sel = best;
		switcher_mark(server);
	}
}

/* A key while they are taken up: round them, go, or put them down. Not
 * Alt's own letting go: that is the modifiers' (keyboard_handle_modifiers). */
static bool switcher_key(struct desk_server *server, xkb_keysym_t sym, uint32_t mods, bool down)
{
	switch (sym) {
	case XKB_KEY_Tab:
	case XKB_KEY_ISO_Left_Tab:
		if (down) {
			switcher_step(server, (mods & WLR_MODIFIER_SHIFT) || sym == XKB_KEY_ISO_Left_Tab
				? -1 : 1);
		}
		return true;
	case XKB_KEY_Left:
	case XKB_KEY_Right:
	case XKB_KEY_Up:
	case XKB_KEY_Down:
		if (down) {
			switcher_toward(server, sym == XKB_KEY_Left ? -1 : sym == XKB_KEY_Right ? 1 : 0,
				sym == XKB_KEY_Up ? -1 : sym == XKB_KEY_Down ? 1 : 0);
		}
		return true;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
	case XKB_KEY_Escape:
		if (down) {
			switcher_finish(server, sym != XKB_KEY_Escape);
		}
		return true;
	default:
		return false;
	}
}

/* Which of them the hand is over, the one it is on first; -1, the desk. */
static int switcher_at(struct desk_server *server, double x, double y)
{
	int sel = server->sw_sel;
	if (sel >= 0 && sel < server->sw_n) {
		const struct sw_paper *p = &server->sw_paper[sel];
		if (x >= p->x && x < p->x + p->w && y >= p->y && y < p->y + p->h) {
			return sel;
		}
	}
	for (int i = server->sw_n - 1; i >= 0; i--) {
		const struct sw_paper *p = &server->sw_paper[i];
		if (x >= p->x && x < p->x + p->w && y >= p->y && y < p->y + p->h) {
			return i;
		}
	}
	return -1;
}

/* The hand moved while they are spread: on to the one it is over. */
static void switcher_motion(struct desk_server *server)
{
	wlr_seat_pointer_clear_focus(server->seat);
	if (!server->sw_on || server->sw_back) {
		return;
	}
	int i = switcher_at(server, server->cursor->x, server->cursor->y);
	if (i >= 0 && i != server->sw_sel) {
		server->sw_sel = i;
		switcher_mark(server);
	}
}

/* A click while they are spread: on one, to it; on the desk, put them back
 * as they were. Its letting go goes nowhere (sw_eat). */
static void switcher_button(struct desk_server *server, uint32_t button, bool down)
{
	if (!down || !server->sw_on || server->sw_back) {
		return;
	}
	server->sw_eat = button;
	int i = switcher_at(server, server->cursor->x, server->cursor->y);
	if (i >= 0 && button == BTN_LEFT) {
		server->sw_sel = i;
		switcher_finish(server, true);
	} else {
		switcher_finish(server, false);
	}
}

/* A window gone while they are taken up: out of them. */
static void switcher_forget(struct desk_server *server, struct desk_toplevel *view)
{
	if (!server->sw_on && !server->sw_shown) {
		return;
	}
	if (server->sw_go == view) {
		server->sw_go = NULL;
	}
	for (int i = 0; i < server->sw_n; i++) {
		if (server->sw_view[i] != view) {
			continue;
		}
		sw_paper_free(&server->sw_paper[i]);
		for (int j = i; j + 1 < server->sw_n; j++) {
			server->sw_view[j] = server->sw_view[j + 1];
			server->sw_paper[j] = server->sw_paper[j + 1];
		}
		memset(&server->sw_paper[server->sw_n - 1], 0, sizeof server->sw_paper[0]);
		server->sw_n--;
		if (server->sw_sel > i || server->sw_sel >= server->sw_n) {
			server->sw_sel--;
		}
		break;
	}
	if (server->sw_n < 1) {
		switcher_close(server);
		return;
	}
	if (server->sw_sel < 0) {
		server->sw_sel = 0;
	}
	if (!server->sw_back) {
		switcher_mark(server);
	}
}

static bool handle_keybinding(struct desk_server *server, xkb_keysym_t sym, uint32_t mods)
{
	/* Once the lamp starts going out there is nothing to press, nothing to
	 * turn away to, and no key that takes it back. That is what the key
	 * and the glass were for. */
	if (console_dying(server)) {
		return true;
	}
	if ((mods & (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)) && sym == XKB_KEY_Escape) {
		end_begin(server, END_LOG_OUT);
		return true;
	}
	/* Super+P, as Windows+P: the screens the next way round, as DISPLAY on the Control Panel's
	 * lid -- to be had with nothing on a screen you can see to find the lid by */
	if ((mods & WLR_MODIFIER_LOGO) && (sym == XKB_KEY_p || sym == XKB_KEY_P)) {
		snd_at(server, DESK_SND_KEY_PRESS, server->cursor->x, 0.5);
		screens_next(server);
		if (console_up(server)) {
			console_live(server, true);
			paint_console(server);
		}
		server_schedule_frames(server);
		return true;
	}
	/* Super+V, as Windows+V: the clipboard out of the drawer onto the desk, or back in
	 * (2026-11-12, the owner: クリップボードがしまえない、ショートカットがない（SUPER+V）) */
	if ((mods & WLR_MODIFIER_LOGO) && (sym == XKB_KEY_v || sym == XKB_KEY_V)) {
		if (!locked(server) && !desk_covered(server)) {
			for (const struct desk_thing *const *t = desk_things; *t; t++) {
				if (strcmp((*t)->name, "clipboard") || !(*t)->in_drawer
					|| ((*t)->offered && !(*t)->offered(server))) {
					continue;
				}
				if ((*t)->in_drawer(server)) {
					wlr_log(WLR_INFO, "Super+V: taking the clipboard out of the drawer");
					(*t)->take_out(server);
				} else if ((*t)->put_away) {
					(*t)->put_away(server);
				}
				server_schedule_frames(server);
			}
		}
		return true;
	}
	/* Alt+Tab should be a compositor shortcut even when a game has focus:
	 * the windows held up over the desk to choose from */
	if ((mods & WLR_MODIFIER_ALT) && (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab)) {
		switcher_step(server, (mods & WLR_MODIFIER_SHIFT) || sym == XKB_KEY_ISO_Left_Tab
			? -1 : 1);
		return true;
	}
	if (card_keys(server, sym)) {
		return true;
	}
	if (looks_keys(server, sym)) {
		return true;
	}
	if (post_up(server)) {
		if (sym == XKB_KEY_Escape) {
			post_toggle(server);
			return true;
		}
	}
	if (console_up(server)) {
		if (sym == XKB_KEY_Escape) {
			console_toggle(server);
			return true;
		}
	}
	if (shelf_up(server) && cal_up(server)) {
		/* the calendar in your hand: put back, or turned a page */
		if (sym == XKB_KEY_Escape) {
			cal_close(server);
			return true;
		}
		if (sym == XKB_KEY_Left || sym == XKB_KEY_Up || sym == XKB_KEY_Page_Up) {
			cal_turn_page(server, -1);
			return true;
		}
		if (sym == XKB_KEY_Right || sym == XKB_KEY_Down || sym == XKB_KEY_Page_Down) {
			cal_turn_page(server, 1);
			return true;
		}
		/* and the sections by their initials */
		int v = sym == XKB_KEY_y ? DESK_CAL_YEAR : sym == XKB_KEY_m ? DESK_CAL_MONTH
			: sym == XKB_KEY_w ? DESK_CAL_WEEK : sym == XKB_KEY_d ? DESK_CAL_DAY : -1;
		if (v >= 0) {
			cal_switch(server, v);
			return true;
		}
	}
	if (shelf_up(server)) {
		if (sym == XKB_KEY_Escape) {
			/* the binder goes back on the shelf before you sit down */
			if (server->shelf_binder >= 0) {
				binder_close(server);
			} else {
				shelf_toggle(server);
			}
			return true;
		}
		if (server->shelf_binder >= 0) {
			int fan = desk_binder_fan();
			if (sym == XKB_KEY_Right || sym == XKB_KEY_Down) {
				binder_riffle(server, 1);
				return true;
			}
			if (sym == XKB_KEY_Left || sym == XKB_KEY_Up) {
				binder_riffle(server, -1);
				return true;
			}
			if (sym == XKB_KEY_Page_Down) {
				binder_riffle(server, fan);
				return true;
			}
			if (sym == XKB_KEY_Page_Up) {
				binder_riffle(server, -fan);
				return true;
			}
		}
	}
	if (mods & WLR_MODIFIER_LOGO) {
		switch (sym) {
		case XKB_KEY_Return:
			terminal_open();
			return true;
		case XKB_KEY_s:
		case XKB_KEY_S:
			/* Super+Shift+S, as other desks have it: Print's camera */
			if (mods & WLR_MODIFIER_SHIFT) {
				snip_begin(server, false);
				return true;
			}
			break;
		case XKB_KEY_l:
		case XKB_KEY_L:
			lock_request(server);
			return true;
		case XKB_KEY_q:
		case XKB_KEY_Q:
			for (const struct desk_thing *const *t = desk_things; *t; t++) {
				if ((*t)->has_keys && (*t)->has_keys(server)) {
					(*t)->put_away(server);
					return true;
				}
			}
			if (!wl_list_empty(&server->toplevels)) {
				struct desk_toplevel *view = wl_container_of(server->toplevels.next, view, link);
				view_close(view);
			}
			return true;
		case XKB_KEY_Tab:
			desk_switch_application(server);
			return true;
		case XKB_KEY_f:
		case XKB_KEY_F:
			/* Super+F gives games without a fullscreen setting one anyway. */
			if (!shelf_up(server) && !console_up(server) && !post_up(server)) {
				struct desk_toplevel *v = view_of_surface(server,
					server->seat->keyboard_state.focused_surface);
				if (!v && !wl_list_empty(&server->toplevels)) {
					v = wl_container_of(server->toplevels.next, v, link);
				}
				if (v && v->scene_tree && v->sheet.phase == SHEET_IDLE && !v->unmanaged) {
					view_set_fullscreen(v, !v->fullscreen);
				}
			}
			return true;
		case XKB_KEY_Left:
		case XKB_KEY_Right:
			/* the sheet in front on the left or right half of its screen */
			if (!shelf_up(server) && !console_up(server) && !post_up(server)) {
				struct desk_toplevel *v = view_of_surface(server,
					server->seat->keyboard_state.focused_surface);
				if (!v && !wl_list_empty(&server->toplevels)) {
					v = wl_container_of(server->toplevels.next, v, link);
				}
				if (v && v->scene_tree) {
					view_lay_half(v, sym == XKB_KEY_Left ? -1 : 1, view_screen(v));
				}
			}
			return true;
		case XKB_KEY_m:
		case XKB_KEY_M:
			if (!wl_list_empty(&server->toplevels)) {
				struct desk_toplevel *view = view_of_surface(server,
					server->seat->keyboard_state.focused_surface);
				if (!view) {
					view = wl_container_of(server->toplevels.next, view, link);
				}
				if (view->fullscreen) {
					view_set_fullscreen(view, false);
				}
				view_fold_toggle(view);
			}
			return true;
		case XKB_KEY_d:
		case XKB_KEY_D:
			if (!shelf_up(server)) {
				drawer_toggle(server);
			}
			return true;
		case XKB_KEY_a:
		case XKB_KEY_A:
			if (!console_up(server) && !post_up(server)) {
				shelf_toggle(server);
			}
			return true;
		case XKB_KEY_c:
		case XKB_KEY_C:
			if (!shelf_up(server) && !post_up(server)) {
				console_toggle(server);
			}
			return true;
		case XKB_KEY_n:
		case XKB_KEY_N:
			if (!shelf_up(server) && !console_up(server)) {
				post_toggle(server);
			}
			return true;
		default:
			break;
		}
	}
	if ((mods & WLR_MODIFIER_ALT) && sym == XKB_KEY_F4) {
		if (!wl_list_empty(&server->toplevels)) {
			struct desk_toplevel *view = wl_container_of(server->toplevels.next, view, link);
			view_close(view);
		}
		return true;
	}
	if ((mods & WLR_MODIFIER_ALT) && sym == XKB_KEY_F1) {
		desk_switch_application(server);
		return true;
	}
	return false;
}

/* ---- the screen lock: the front door ----------------------------------
 *
 * Super+L, or logind (loginctl lock-session, the machine going to sleep):
 * the desk is behind a front door (desk_draw_lock). Nothing behind it
 * gets a key or the pointer -- no window, no shortcut but the VT keys and
 * a laptop's brightness keys -- until the password is right (PAM, on a
 * thread, lock.c). Then the lever turns, the door swings in on the room,
 * and you go in: the room is the view of the bookcase from inside, and
 * once it fills the screen the door is gone and you sit down at the desk,
 * the bookcase's own camera move (shelf_toggle). Locked from somewhere
 * that move does not start from (the Control Panel, the post box, the
 * drawer out, a first run with no bookcase yet), the door fades away on
 * where you were instead. */

enum { LOCK_NONE = 0, LOCK_SHUT, LOCK_CHECK, LOCK_OPEN };

/* How the moves go -- the walk up, leaning in, the key, the door, the way
 * through and the way out -- is desk.h's (DESK_LOCK_*), shared with the
 * painter, which makes the poses from them (desk_lock_pose) and films the
 * moves ahead (desk_lock_films). Here, only what is the compositor's own. */
#define LOCK_FADE_S 0.35     /* coming down over the desk */
#define LOCK_CARET_MS 550
#define LOCK_TRIED_S 0.70    /* after Return, by when the key has been tried */
#define LOCK_FILM_MAX_S 3.0  /* how long the turn to the bookcase may take */
#define LOCK_FILM_PX 2.1e6   /* the most pixels the door is laid at from a film */

enum { LOCK_FROM_ELSEWHERE = 0, LOCK_FROM_DESK, LOCK_FROM_SHELF, LOCK_FROM_CONSOLE };
enum { LOCK_COME_NOW = 0, LOCK_COME_WALK, LOCK_COME_FILM, LOCK_COME_OUT };

static bool locked(const struct desk_server *server)
{
	return server->lock_phase != LOCK_NONE;
}
