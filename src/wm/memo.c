/* Part of wm.c: the memo pad.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static bool memo_up(const struct desk_server *server)
{
	return server->memo_t > 0.0005 || server->memo_moving;
}

static bool memo_settled(const struct desk_server *server)
{
	return server->memo_to > 0.5 && !server->memo_moving;
}

static bool memo_has_keys(const struct desk_server *server)
{
	return shelf_up(server) && memo_up(server) && server->memo_to > 0.5;
}

static double memo_turn_t(const struct desk_server *server)
{
	if (!server->memo_turn) {
		return 0.0;
	}
	return clampf_d((monotonic_sec() - server->memo_turn_start) / MEMO_TURN_S, 0.0, 1.0);
}

static void memo_look(const struct desk_server *server, struct desk_memo_look *lk)
{
	memset(lk, 0, sizeof *lk);
	lk->text = server->memo_text;
	lk->caret = server->memo_caret;
	lk->preedit = server->memo_pre;
	lk->preedit_caret = server->memo_pre_caret;
	lk->cands = server->memo_cand_p;
	lk->ncand = server->memo_ncand;
	lk->cand_cursor = server->memo_cand_cursor;
	lk->sheet = server->memo_sheet;
	lk->sheets = desk_memo_sheets();
	lk->turn = server->memo_turn;
	lk->turn_t = memo_turn_t(server);
	lk->other = server->memo_other;
	lk->other_sheet = server->memo_turn_to;
	/* held up as it comes, a little turned and leaning back, the eye over
	 * the middle of the screen */
	double up = clampf_d(server->memo_t, 0.0, 1.0);
	up = up * up * (3.0 - 2.0 * up);
	lk->yaw = MEMO_HELD_YAW * up;
	lk->pitch = MEMO_HELD_PITCH * up;
	lk->eye_x = server->desk_w * 0.5;
	lk->eye_y = server->desk_h * 0.5;
	lk->eye_f = server->desk_h * 1.6;
}

/* Lay the picture where memo_t has got to: its sheet over the face of the
 * pad on the shelf at 0, in the middle of the view at 1. */
static void memo_place(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	double t = clampf_d(server->memo_t, 0.0, 1.0);
	double fb[4];
	bool have_fb = desk_shelf_gadget_box(w, h, DESK_GADGET_NOTEPAD, fb);
	double zx = have_fb ? fb[0] + fb[2] * 0.5 : w * 0.5;
	double zy = have_fb ? fb[1] + fb[3] * 0.5 : h * 0.5;
	double z = step_in_place(server, t, zx, zy, server->memo_sharp, server->memo_soft);
	struct desk_sprite *sp = &server->memo_sp;
	if (!sp->node || !sp->drawn) {
		return;
	}
	double pg[4], box[4];
	desk_memo_rect(w, h, pg, box);
	double k = 1.0, dx = 0.0, dy = 0.0;
	if (t < 0.9995 && have_fb) {
		/* from the face on the stepped-in shelf (the box has two pixels to
		 * spare all round) */
		double fx = zx + (fb[0] + 2.0 - zx) * z, fy = zy + (fb[1] + 2.0 - zy) * z;
		double fw = (fb[2] - 4.0) * z, cw = pg[2] * desk_memo_face_ratio();
		double rx = fx + (pg[0] - fx) * t, ry = fy + (pg[1] - fy) * t;
		double rw = fw + (cw - fw) * t;
		k = rw / cw;
		dx = rx - pg[0] * k;
		dy = ry - pg[1] * k;
	}
	wlr_scene_node_set_position(&sp->node->node, (int)lround(sp->x * k + dx),
		(int)lround(sp->y * k + dy));
	int dw = (int)lround(server->memo_nat_w * k), dh = (int)lround(server->memo_nat_h * k);
	wlr_scene_buffer_set_dest_size(sp->node, dw > 1 ? dw : 1, dh > 1 ? dh : 1);
	/* it lifts off the one on the shelf rather than being pasted over it */
	wlr_scene_buffer_set_opacity(sp->node, (float)clampf_d(t * 5.0, 0.0, 1.0));
}

struct memo_args {
	struct desk_memo_look look;
};

static void memo_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct memo_args *a = data;
	desk_draw_memo(cr, w, h, &a->look);
}

static void paint_memo(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (!memo_up(server) || !shelf_up(server) || w < 8 || h < 8) {
		if (server->memo_tree) {
			wlr_scene_node_set_enabled(&server->memo_tree->node, false);
		}
		return;
	}
	if (!server->memo_tree) {
		server->memo_tree = wlr_scene_tree_create(server->layer_shelf);
	}
	wlr_scene_node_set_enabled(&server->memo_tree->node, true);
	wlr_scene_node_raise_to_top(&server->memo_tree->node);
	struct memo_args a;
	memo_look(server, &a.look);
	uint64_t key = 14695981039346656037ull;
	key = key_add(key, &a.look, sizeof a.look);
	key = key_add(key, server->memo_text, strlen(server->memo_text));
	key = key_add(key, server->memo_pre, strlen(server->memo_pre) + 1);
	for (int i = 0; i < server->memo_ncand; i++) {
		key = key_add(key, server->memo_cand[i], strlen(server->memo_cand[i]) + 1);
	}
	if (server->memo_turn) {
		key = key_add(key, server->memo_other, strlen(server->memo_other));
	}
	key = key_add(key, &w, sizeof w);
	key = key_add(key, &h, sizeof h);
	struct desk_sprite *sp = &server->memo_sp;
	if (!sp->drawn || sp->key != key) {
		double box[4];
		desk_memo_extent(w, h, &a.look, box);
		sp->key = key;
		sprite_paint(server, sp, server->memo_tree, box, memo_draw, &a);
		if (sp->node) {
			wlr_scene_node_raise_to_top(&sp->node->node);
		}
		int bw, bh;
		if (sp->node && scene_pic_size(sp->node, &bw, &bh)) {
			int ow, oh;
			desk_overlay_size(w, h, &ow, &oh);
			server->memo_nat_w = (double)bw * w / ow;
			server->memo_nat_h = (double)bh * h / oh;
		}
	}
	memo_place(server);
}

static double memo_pan(struct desk_server *server)
{
	double fb[4];
	if (!desk_shelf_gadget_box(server->desk_w, server->desk_h, DESK_GADGET_NOTEPAD, fb)) {
		return -0.5;
	}
	return snd_pan(server, fb[0] + fb[2] * 0.5);
}

/* Tell the input method where the caret is. */
static void memo_tell_cursor(struct desk_server *server)
{
	if (!server->memo_ime || !memo_up(server)) {
		return;
	}
	struct desk_memo_look lk;
	memo_look(server, &lk);
	double b[4];
	desk_memo_caret_box(server->desk_w, server->desk_h, &lk, b);
	int ox = 0, oy = 0;
	host_origin(server, &ox, &oy);
	int c[4] = { ox + (int)lround(b[0]), oy + (int)lround(b[1]), (int)lround(fmax(b[2], 1.0)),
		(int)lround(b[3]) };
	if (memcmp(c, server->memo_cursor, sizeof c) != 0) {
		memcpy(server->memo_cursor, c, sizeof c);
		desk_ime_typing_cursor(c[0], c[1], c[2], c[3]);
	}
}

static void memo_changed(struct desk_server *server)
{
	paint_memo(server);
	memo_tell_cursor(server);
	server_schedule_frames(server);
}

/* What is written on the sheet open, into memo.c (which keeps it on disk). */
static void memo_keep_now(struct desk_server *server)
{
	if (server->memo_keep) {
		wl_event_source_timer_update(server->memo_keep, 0);
	}
	if (!server->memo_dirty) {
		return;
	}
	server->memo_dirty = false;
	desk_memo_set(server->memo_sheet, server->memo_text);
	desk_memo_set_top(server->memo_sheet);
}

static int memo_keep_cb(void *data)
{
	memo_keep_now(data);
	return 0;
}

/* Something was written or rubbed out: kept a moment after the last of it. */
static void memo_written(struct desk_server *server)
{
	server->memo_dirty = true;
	if (!server->memo_keep) {
		server->memo_keep = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), memo_keep_cb, server);
	}
	if (server->memo_keep) {
		wl_event_source_timer_update(server->memo_keep, MEMO_KEEP_MS);
	}
	memo_changed(server);
}

static void memo_clear_ime(struct desk_server *server)
{
	server->memo_pre[0] = '\0';
	server->memo_pre_caret = 0;
	server->memo_ncand = 0;
	server->memo_keyq_n = 0;
}

static void memo_repeat_stop(struct desk_server *server)
{
	server->memo_repeat_sym = 0;
	if (server->memo_repeat) {
		wl_event_source_timer_update(server->memo_repeat, 0);
	}
}

/* Whatever the input method was writing is thrown away (the caret moved
 * by the pointer, a sheet turned). */
static void memo_ime_drop(struct desk_server *server)
{
	if (server->memo_ime && (server->memo_pre[0] || server->memo_ncand)) {
		desk_ime_typing_reset();
	}
	memo_clear_ime(server);
}

static void memo_insert(struct desk_server *server, const char *t)
{
	size_t n = strlen(server->memo_text), k = strlen(t);
	if (!k || n + k >= sizeof server->memo_text) {
		return;   /* the sheet is full */
	}
	int c = server->memo_caret;
	memmove(server->memo_text + c + k, server->memo_text + c, n - (size_t)c + 1);
	memcpy(server->memo_text + c, t, k);
	server->memo_caret = c + (int)k;
}

static void memo_move(struct desk_server *server, double to, double dur)
{
	server->memo_from = server->memo_t;
	server->memo_to = to;
	server->memo_start = monotonic_sec();
	server->memo_dur = dur * fmax(fabs(to - server->memo_t), 0.35);
	server->memo_moving = 1;
	server_schedule_frames(server);
	paint_memo(server);
}

/* Turn to sheet `to`, over the wire: on, past those between, or back.
 * Past the last there is a clean one, unless the last is clean already. */
static void memo_turn_to(struct desk_server *server, int to)
{
	if (!memo_settled(server) || server->memo_turn || to < 0) {
		return;
	}
	memo_keep_now(server);
	if (to >= desk_memo_sheets()) {
		to = desk_memo_add();
	}
	if (to == server->memo_sheet) {
		return;
	}
	memo_ime_drop(server);
	memo_repeat_stop(server);
	server->memo_turn = to > server->memo_sheet ? 1 : -1;
	server->memo_turn_to = to;
	server->memo_turn_start = monotonic_sec();
	snprintf(server->memo_other, sizeof server->memo_other, "%s", desk_memo_sheet(to));
	desk_sound_play(DESK_SND_PAGE_TURN, memo_pan(server) * 0.3, 0.9, 1.1);
	memo_changed(server);
}

static void memo_plain(struct desk_server *server, uint32_t sym, const char *utf8,
	uint32_t mods);

static int memo_repeat_cb(void *data)
{
	struct desk_server *server = data;
	if (!server->memo_repeat_sym || !memo_has_keys(server)) {
		return 0;
	}
	memo_plain(server, server->memo_repeat_sym, "", 0);
	wl_event_source_timer_update(server->memo_repeat, 33);
	return 0;
}

/* a key the input method did not take, or there is no input method: what
 * a key does on paper */
static void memo_plain(struct desk_server *server, uint32_t sym, const char *utf8,
	uint32_t mods)
{
	char *t = server->memo_text;
	int n = (int)strlen(t), c = server->memo_caret;
	bool ctrl = mods & WLR_MODIFIER_CTRL;
	bool rep = false, wrote = false;
	/* (a sheet in the air: nothing is written till it is down) */
	switch (sym) {
	case XKB_KEY_Escape:
		memo_close(server);
		return;
	case XKB_KEY_Page_Up:
		memo_turn_to(server, server->memo_sheet - 1);
		return;
	case XKB_KEY_Page_Down:
		memo_turn_to(server, server->memo_sheet + 1);
		return;
	case XKB_KEY_n:
	case XKB_KEY_N:
		if (ctrl) {
			memo_turn_to(server, desk_memo_sheets());
			return;
		}
		break;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		if (!server->memo_turn) {
			memo_insert(server, "\n");
			wrote = true;
		}
		break;
	case XKB_KEY_BackSpace:
		if (c > 0 && !server->memo_turn) {
			int p = utf8_prev(t, c);
			memmove(t + p, t + c, (size_t)(n - c + 1));
			server->memo_caret = p;
			wrote = true;
		}
		rep = true;
		break;
	case XKB_KEY_Delete:
		if (c < n && !server->memo_turn) {
			int e = utf8_next(t, c);
			memmove(t + c, t + e, (size_t)(n - e + 1));
			wrote = true;
		}
		rep = true;
		break;
	case XKB_KEY_Left:
		server->memo_caret = utf8_prev(t, c);
		rep = true;
		break;
	case XKB_KEY_Right:
		server->memo_caret = utf8_next(t, c);
		rep = true;
		break;
	case XKB_KEY_Up:
	case XKB_KEY_Down: {
		struct desk_memo_look lk;
		memo_look(server, &lk);
		server->memo_caret = desk_memo_caret_step(server->desk_w, server->desk_h, &lk,
			sym == XKB_KEY_Up ? -1 : 1);
		rep = true;
		break;
	}
	case XKB_KEY_Home:
		if (ctrl) {
			server->memo_caret = 0;
		} else {
			while (server->memo_caret > 0 && t[server->memo_caret - 1] != '\n') {
				server->memo_caret = utf8_prev(t, server->memo_caret);
			}
		}
		break;
	case XKB_KEY_End:
		if (ctrl) {
			server->memo_caret = n;
		} else {
			while (server->memo_caret < n && t[server->memo_caret] != '\n') {
				server->memo_caret = utf8_next(t, server->memo_caret);
			}
		}
		break;
	default:
		break;
	}
	if (!wrote && !rep && utf8 && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f
		&& !(mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))
		&& !server->memo_turn && sym != XKB_KEY_Return && sym != XKB_KEY_KP_Enter) {
		memo_insert(server, utf8);
		wrote = true;
	}
	if (rep && server->memo_repeat_sym != sym && sym == server->memo_held_sym && utf8) {
		/* held down, it goes on: a moment's wait, then thirty a second */
		server->memo_repeat_sym = sym;
		if (!server->memo_repeat) {
			server->memo_repeat = wl_event_loop_add_timer(
				wl_display_get_event_loop(server->wl_display), memo_repeat_cb, server);
		}
		if (server->memo_repeat) {
			wl_event_source_timer_update(server->memo_repeat, 450);
		}
	}
	if (wrote) {
		memo_written(server);
	} else {
		memo_changed(server);
	}
}

/* what the input method sends back */

static void memo_ime_commit(const char *text, void *user)
{
	struct desk_server *server = user;
	if (!memo_has_keys(server)) {
		return;
	}
	server->memo_pre[0] = '\0';
	if (!server->memo_turn) {
		memo_insert(server, text);
	}
	memo_written(server);
}

static void memo_ime_preedit(const char *text, int cursor, bool visible, void *user)
{
	struct desk_server *server = user;
	snprintf(server->memo_pre, sizeof server->memo_pre, "%s", visible ? text : "");
	/* its caret is counted in letters */
	int b = 0;
	for (int i = 0; i < cursor && server->memo_pre[b]; i++) {
		b = utf8_next(server->memo_pre, b);
	}
	server->memo_pre_caret = b;
	if (memo_up(server)) {
		memo_changed(server);
	}
}

static void memo_ime_lookup(const char *const *cands, int n, int cursor, int page, bool visible,
	void *user)
{
	struct desk_server *server = user;
	server->memo_ncand = 0;
	if (visible && n > 0) {
		/* the page the picked one is on */
		page = page > 0 ? (page > 16 ? 16 : page) : 9;
		cursor = cursor < 0 ? 0 : (cursor >= n ? n - 1 : cursor);
		int first = cursor / page * page;
		for (int i = first; i < n && server->memo_ncand < page; i++) {
			snprintf(server->memo_cand[server->memo_ncand], sizeof server->memo_cand[0], "%s",
				cands[i]);
			server->memo_cand_p[server->memo_ncand] = server->memo_cand[server->memo_ncand];
			server->memo_ncand++;
		}
		server->memo_cand_cursor = cursor - first;
	}
	if (memo_up(server)) {
		memo_changed(server);
	}
}

static void memo_ime_key(uint32_t sym, uint32_t keycode, uint32_t mods, bool taken, bool answer,
	void *user)
{
	(void)keycode;
	struct desk_server *server = user;
	char utf8[8] = "";
	if (answer) {
		/* the oldest key handed over: what it would type */
		if (server->memo_keyq_n > 0) {
			memcpy(utf8, server->memo_keyq[0].utf8, sizeof utf8);
			server->memo_keyq_n--;
			memmove(&server->memo_keyq[0], &server->memo_keyq[1],
				sizeof server->memo_keyq[0] * (size_t)server->memo_keyq_n);
		}
	} else {
		xkb_keysym_to_utf8(sym, utf8, sizeof utf8);
	}
	if (!taken && memo_has_keys(server)) {
		memo_plain(server, sym, utf8, mods);
	}
}

/* writing on it, from when it is taken down till it is put back */
static void memo_typing_begin(struct desk_server *server)
{
	if (server->memo_typing) {
		return;
	}
	memo_clear_ime(server);
	memset(server->memo_cursor, 0, sizeof server->memo_cursor);
	static const struct desk_ime_typing hooks = { memo_ime_commit, memo_ime_preedit,
		memo_ime_lookup, memo_ime_key, NULL };
	struct desk_ime_typing h = hooks;
	h.user = server;
	server->memo_typing = true;
	server->memo_ime = desk_ime_typing_begin(&h);
	memo_tell_cursor(server);
	things_text_wanted(server, true);
}

static void memo_typing_end(struct desk_server *server)
{
	memo_repeat_stop(server);
	if (!server->memo_typing) {
		return;
	}
	if (desk_ime_typing_owner() == server) {
		desk_ime_typing_end();
	}
	server->memo_typing = false;
	server->memo_ime = false;
	memo_clear_ime(server);
	things_text_wanted(server, false);
}

static void memo_key(struct desk_server *server, struct xkb_state *xkb, uint32_t keycode,
	uint32_t sym, uint32_t mods, bool released)
{
	char utf8[8] = "";
	xkb_state_key_get_utf8(xkb, keycode + 8, utf8, sizeof utf8);
	if (released) {
		if (server->memo_ime) {
			desk_ime_typing_key(sym, keycode, mods, true);
		}
		if (sym == server->memo_held_sym) {
			server->memo_held_sym = 0;
		}
		if (sym == server->memo_repeat_sym) {
			memo_repeat_stop(server);
		}
		return;
	}
	/* the input method may answer for it after it is let go: it repeats
	 * only while it is still held (memo_plain) */
	server->memo_held_sym = sym;
	if (server->memo_ime && desk_ime_typing_key(sym, keycode, mods, false)) {
		if (server->memo_keyq_n < (int)(sizeof server->memo_keyq / sizeof server->memo_keyq[0])) {
			memcpy(server->memo_keyq[server->memo_keyq_n].utf8, utf8, sizeof utf8);
			server->memo_keyq_n++;
		}
		return;
	}
	memo_plain(server, sym, utf8, mods);
}

static void memo_snapshot(struct desk_server *server)
{
	if (!server->memo_tree) {
		server->memo_tree = wlr_scene_tree_create(server->layer_shelf);
	}
	shelf_snapshot(server, server->memo_tree, &server->memo_sharp, &server->memo_soft);
}

static void memo_open(struct desk_server *server)
{
	if (memo_up(server) && server->memo_to > 0.5) {
		return;
	}
	if (!memo_up(server)) {
		/* at the sheet it was left at, the caret after what is on it */
		server->memo_sheet = desk_memo_top();
		snprintf(server->memo_text, sizeof server->memo_text, "%s",
			desk_memo_sheet(server->memo_sheet));
		server->memo_caret = (int)strlen(server->memo_text);
		server->memo_dirty = false;
		server->memo_turn = 0;
		server->memo_scroll = 0.0;
		server->memo_sp.drawn = false;
		server->memo_gen_open = desk_memo_generation();
		memo_snapshot(server);
	}
	hover_set(server, -1);
	memo_typing_begin(server);
	desk_sound_play(DESK_SND_SHEET_TAKE, memo_pan(server), 0.8, 1.15);
	memo_move(server, 1.0, MEMO_OPEN_S);
}

static void memo_close(struct desk_server *server)
{
	if (!memo_up(server) || server->memo_to < 0.5) {
		return;
	}
	memo_typing_end(server);
	if (server->memo_turn) {
		/* the sheet in the air comes down where it was going */
		server->memo_sheet = server->memo_turn_to;
		snprintf(server->memo_text, sizeof server->memo_text, "%s", server->memo_other);
		server->memo_turn = 0;
		desk_memo_set_top(server->memo_sheet);
	}
	memo_keep_now(server);
	/* the bookcase it goes back to has what was written on the pad's face */
	if (desk_memo_generation() != server->memo_gen_open) {
		paint_shelf(server);
		memo_snapshot(server);
	}
	double dur = MEMO_SHUT_S * fmax(server->memo_t, 0.35);
	desk_sound_play_in(DESK_SND_SHEET_DOWN, memo_pan(server), 0.6, 0.0,
		real_secs(dur) + SND_SEEN);
	memo_move(server, 0.0, MEMO_SHUT_S);
}

/* Off the screen at once (kept first): the view it was in is going. */
static void memo_drop(struct desk_server *server)
{
	if (memo_up(server)) {
		if (server->memo_turn) {
			server->memo_sheet = server->memo_turn_to;
			snprintf(server->memo_text, sizeof server->memo_text, "%s", server->memo_other);
			server->memo_turn = 0;
			desk_memo_set_top(server->memo_sheet);
		}
		memo_keep_now(server);
	}
	memo_typing_end(server);
	server->memo_t = 0.0;
	server->memo_to = 0.0;
	server->memo_moving = 0;
	server->memo_turn = 0;
	if (server->memo_tree) {
		wlr_scene_node_set_enabled(&server->memo_tree->node, false);
	}
}

static void memo_tick(struct desk_server *server)
{
	if (!memo_up(server)) {
		return;
	}
	double now = monotonic_sec();
	bool changed = false;
	if (server->memo_moving) {
		double u = clampf_d((now - server->memo_start) / server->memo_dur, 0, 1);
		/* up quickly and settling; back down evenly */
		double e = server->memo_to > 0.5 ? 1.0 - pow(1.0 - u, 3.0) : u * u * (3.0 - 2.0 * u);
		server->memo_t = server->memo_from + (server->memo_to - server->memo_from) * e;
		if (u >= 1.0) {
			server->memo_t = server->memo_to;
			server->memo_moving = 0;
		}
		changed = true;
	}
	if (server->memo_turn) {
		if (memo_turn_t(server) >= 1.0) {
			/* down: open at that sheet now, the caret after what is on it */
			server->memo_sheet = server->memo_turn_to;
			snprintf(server->memo_text, sizeof server->memo_text, "%s", server->memo_other);
			server->memo_caret = (int)strlen(server->memo_text);
			server->memo_turn = 0;
			desk_memo_set_top(server->memo_sheet);
		}
		changed = true;
	}
	if (server->memo_t <= 0.0005 && !server->memo_moving) {
		memo_drop(server);
		return;
	}
	if (changed) {
		paint_memo(server);
		memo_tell_cursor(server);
		server_schedule_frames(server);
	}
}

/* A press while it is in your hand: on the sheet, the caret there; off
 * the pad, or the other button, it goes back on the shelf. */
static void memo_press(struct desk_server *server, uint32_t button)
{
	if (!memo_settled(server)) {
		return;
	}
	if (button != BTN_LEFT) {
		memo_close(server);
		return;
	}
	double x = server->cursor->x, y = server->cursor->y;
	struct desk_memo_look lk;
	memo_look(server, &lk);
	desk_memo_unturn(server->desk_w, server->desk_h, &lk, &x, &y);
	int at = desk_memo_hit(server->desk_w, server->desk_h, &lk, x, y);
	if (at < 0) {
		memo_close(server);
		return;
	}
	if (server->memo_turn) {
		return;
	}
	memo_ime_drop(server);
	server->memo_caret = at;
	memo_changed(server);
}

/* the wheel: a sheet a notch, down on and up back */
static void memo_scroll(struct desk_server *server, double d)
{
	if (!memo_settled(server)) {
		return;
	}
	server->memo_scroll += d;
	while (server->memo_scroll >= DESK_NOTCH) {
		server->memo_scroll -= DESK_NOTCH;
		memo_turn_to(server, server->memo_sheet + 1);
	}
	while (server->memo_scroll <= -DESK_NOTCH) {
		server->memo_scroll += DESK_NOTCH;
		memo_turn_to(server, server->memo_sheet - 1);
	}
}

/* ---- the binder of notes, beside the calendar -------------------------
 *
 * A day clicked on the calendar in your hand, the calendar moves over and
 * a binder comes in from the right, open at that day: pads of sticky notes
 * inside its front cover, the day's plans and things to do on the page.
 * The pad picked gives a new note its colour; a space on the page, or just
 * typing, peels one off and sticks it there to be written on -- in
 * Japanese too, through the input method (ime.c), whose candidates the
 * binder shows itself. A plan's time is set on a ruler laid across the
 * binder, or written at the front. Enter sticks it -- peeled off the
 * binder, it goes over to the calendar and is stuck on its day there, and
 * the binder keeps a note of it in pencil -- Escape stops. While the
 * binder is in, the keys are its. */

#define AGD_HELD_YAW (-0.22)   /* held up: turned a little towards the calendar */
#define AGD_HELD_PITCH 0.08
#define AGD_SWING_YAW 0.85     /* and further, still on its way in */
#define AGD_IN_S 0.46
#define AGD_OUT_S 0.30
#define AGD_PEEL_S 0.34
#define AGD_GONE_S 0.30
