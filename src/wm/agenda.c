/* Part of wm.c: the calendar's notebook of plans and to-dos.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static bool agd_up(const struct desk_server *server)
{
	return server->agd_t > 0.0005 || server->agd_moving;
}

/* what the note being written is: a plan or a thing to do */
static int agd_edit_kind(const struct desk_server *server)
{
	if (server->agd_edit == DESK_AGENDA_NEW_TODO) {
		return DESK_STICK_TODO;
	}
	struct desk_stick s;
	if (server->agd_edit > 0 && desk_agenda_get(server->agd_edit, &s)) {
		return s.kind;
	}
	return DESK_STICK_PLAN;
}

/* The time of the plan being written: written at the front of it, or else
 * set on the ruler; -1 for none (and for a thing to do). */
static int agd_time(const struct desk_server *server, int *span)
{
	*span = 0;
	if (!server->agd_edit || agd_edit_kind(server) != DESK_STICK_PLAN) {
		return -1;
	}
	int sp, t = desk_agenda_when(server->agd_text, &sp, NULL);
	if (t >= 0) {
		*span = sp;
		return t;
	}
	*span = server->agd_when >= 0 ? server->agd_span : 0;
	return server->agd_when;
}

static void agd_rect(const struct desk_server *server, double r[4]);

static void agd_look(const struct desk_server *server, struct desk_agenda_look *lk)
{
	memset(lk, 0, sizeof *lk);
	lk->year = server->agd_y;
	lk->month = server->agd_m;
	lk->day = server->agd_d;
	lk->colour = server->agd_colour;
	lk->edit = server->agd_edit;
	lk->edit_colour = server->agd_edit_colour;
	lk->text = server->agd_text;
	lk->caret = server->agd_caret;
	lk->preedit = server->agd_pre;
	lk->preedit_caret = server->agd_pre_caret;
	lk->cands = server->agd_cand_p;
	lk->ncand = server->agd_ncand;
	lk->cand_cursor = server->agd_cand_cursor;
	lk->cand_page = server->agd_cand_page;
	lk->hover = server->agd_hover;
	lk->hover_arg = server->agd_hover_arg;
	lk->peel_t = server->agd_peeling
		? clampf_d((monotonic_sec() - server->agd_peel_start) / AGD_PEEL_S, 0, 1) : 1.0;
	lk->gone_id = server->agd_gone_id;
	lk->gone_t = server->agd_gone_id
		? clampf_d((monotonic_sec() - server->agd_gone_start) / AGD_GONE_S, 0, 1) : 0.0;
	lk->ime = server->agd_ime;
	int span;
	lk->when = agd_time(server, &span);
	lk->span = span;
	if (!server->agd_edit) {
		/* the ruler going away still has on it what it was set at */
		lk->when = server->agd_when;
		lk->span = server->agd_span;
	}
	lk->ruler_t = server->agd_ruler_t;
	lk->ruler_mark = server->agd_mark;
	lk->edit_away = server->agd_edit_away;
	/* Held up beside the calendar, a little turned towards it, as seen
	 * from over the middle of the screen. Coming in from the side it
	 * swings round as it comes, its near edge first, and goes back out the
	 * same way: a thing brought in by hand, not a panel slid across. */
	double r[4], cal[3];
	desk_agenda_layout(server->desk_w, server->desk_h, r, cal);
	double t = clampf_d(server->agd_t, 0.0, 1.2);
	double away = fmax(0.0, 1.0 - t);
	lk->yaw = AGD_HELD_YAW - AGD_SWING_YAW * away * away;
	lk->pitch = AGD_HELD_PITCH;
	double now[4];
	agd_rect(server, now);
	lk->eye_x = server->desk_w * 0.5 - now[0];
	lk->eye_y = server->desk_h * 0.5 - now[1];
	lk->eye_f = server->desk_h * 1.6;
}

struct agd_args {
	struct desk_agenda_look look;
	double rect[4];
};

static void agd_draw(cairo_t *cr, int w, int h, void *data)
{
	(void)w;
	(void)h;
	const struct agd_args *a = data;
	cairo_translate(cr, a->rect[0], a->rect[1]);
	desk_draw_agenda(cr, a->rect[2], a->rect[3], &a->look);
}

static void agd_mark_draw(cairo_t *cr, int w, int h, void *data)
{
	(void)w;
	(void)h;
	const struct agd_args *a = data;
	cairo_translate(cr, a->rect[0], a->rect[1]);
	desk_draw_agenda_mark(cr, a->rect[2], a->rect[3], &a->look);
}

/* how far in it has come, eased: in with a little overshoot, out evenly */
static double agd_in(const struct desk_server *server)
{
	return clampf_d(server->agd_t, 0.0, 1.2);
}

/* where it lies now: in place, or as far to the right as it has to come */
static void agd_rect(const struct desk_server *server, double r[4])
{
	double cal[3];
	desk_agenda_layout(server->desk_w, server->desk_h, r, cal);
	r[0] += (1.0 - agd_in(server)) * (server->desk_w - r[0] + r[2] * 0.08);
}

static void paint_agenda(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	struct desk_sprite *sp = &server->agd_sp, *mk = &server->agd_mark_sp;
	if (!agd_up(server) || !cal_up(server) || !server->cal_tree || w < 8 || h < 8) {
		if (sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, false);
		}
		if (mk->node) {
			wlr_scene_node_set_enabled(&mk->node->node, false);
		}
		return;
	}
	struct agd_args a;
	agd_look(server, &a.look);
	/* The pencil mark where the pointer is on the ruler goes on by itself:
	 * the binder is a whole page drawn and turned into the view a pixel at
	 * a time, and drawn again for every quarter hour the pointer crossed
	 * it was a pointer going along the ruler at a few frames a second. */
	struct agd_args m = a;
	a.look.ruler_mark = -1;
	if (a.look.hover == DESK_AGENDA_HIT_RULER) {
		a.look.hover_arg = 0;
	}
	double cal[3];
	desk_agenda_layout(w, h, a.rect, cal);
	memcpy(m.rect, a.rect, sizeof m.rect);
	/* drawn where it lies when it is in; brought there by moving it */
	uint64_t key = 14695981039346656037ull;
	key = key_add(key, &a.look, sizeof a.look);
	key = key_add(key, server->agd_text, strlen(server->agd_text));
	key = key_add(key, server->agd_pre, strlen(server->agd_pre));
	for (int i = 0; i < server->agd_ncand; i++) {
		key = key_add(key, server->agd_cand[i], strlen(server->agd_cand[i]));
	}
	unsigned gen = desk_agenda_generation();
	key = key_add(key, &gen, sizeof gen);
	key = key_add(key, &w, sizeof w);
	key = key_add(key, &h, sizeof h);
	if (!sp->drawn || sp->key != key) {
		double box[4];
		desk_agenda_extent(a.rect[2], a.rect[3], &a.look, box);
		box[0] += a.rect[0];
		box[1] += a.rect[1];
		sp->key = key;
		sprite_paint(server, sp, server->cal_tree, box, agd_draw, &a);
	}
	if (!sp->node) {
		return;
	}
	wlr_scene_node_raise_to_top(&sp->node->node);
	double r[4];
	agd_rect(server, r);
	int dx = (int)lround(r[0] - a.rect[0]);
	wlr_scene_node_set_position(&sp->node->node, sp->x + dx, sp->y);
	wlr_scene_node_set_enabled(&sp->node->node, server->agd_t > 0.0005);

	double mb[4];
	if (desk_agenda_mark_extent(m.rect[2], m.rect[3], &m.look, mb)) {
		uint64_t mkey = 14695981039346656037ull;
		mkey = key_add(mkey, &m.look, sizeof m.look);
		mkey = key_add(mkey, &w, sizeof w);
		mkey = key_add(mkey, &h, sizeof h);
		if (!mk->drawn || mk->key != mkey) {
			mb[0] += m.rect[0];
			mb[1] += m.rect[1];
			mk->key = mkey;
			sprite_paint(server, mk, server->cal_tree, mb, agd_mark_draw, &m);
		}
		if (mk->node) {
			wlr_scene_node_raise_to_top(&mk->node->node);
			wlr_scene_node_set_position(&mk->node->node, mk->x + dx, mk->y);
			wlr_scene_node_set_enabled(&mk->node->node, server->agd_t > 0.0005);
		}
	} else if (mk->node) {
		wlr_scene_node_set_enabled(&mk->node->node, false);
	}
	agd_fly_raise(server);
}

/* Tell the input method where the note being written is, on the screen
 * its windows go on: this one, or the one DEsk's window is on. */
static void agd_tell_cursor(struct desk_server *server)
{
	if (!server->agd_ime || !server->agd_edit) {
		return;
	}
	struct desk_agenda_look lk;
	agd_look(server, &lk);
	double r[4], n[4];
	agd_rect(server, r);
	if (!desk_agenda_writing(r[2], r[3], &lk, n)) {
		return;
	}
	/* as the binder is held: the box round the note's corners turned */
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 4; i++) {
		double x = n[0] + ((i & 1) ? n[2] : 0.0), y = n[1] + ((i & 2) ? n[3] : 0.0);
		desk_agenda_turn_point(r[2], r[3], &lk, &x, &y);
		x0 = fmin(x0, x);
		y0 = fmin(y0, y);
		x1 = fmax(x1, x);
		y1 = fmax(y1, y);
	}
	int ox = 0, oy = 0;
	host_origin(server, &ox, &oy);
	int c[4] = { ox + (int)lround(r[0] + x0), oy + (int)lround(r[1] + y0),
		(int)lround(x1 - x0), (int)lround(y1 - y0) };
	if (memcmp(c, server->agd_cursor, sizeof c) != 0) {
		memcpy(server->agd_cursor, c, sizeof c);
		desk_ime_typing_cursor(c[0], c[1], c[2], c[3]);
	}
}

static void agd_changed(struct desk_server *server)
{
	paint_agenda(server);
	agd_tell_cursor(server);
	server_schedule_frames(server);
}

/* the notes changed: the calendar's pages, here and on the shelf, too */
static void agd_stuck(struct desk_server *server)
{
	paint_calendar(server);
	paint_shelf(server);
	agd_changed(server);
}

static double agd_pan(const struct desk_server *server)
{
	double r[4], cal[3];
	desk_agenda_layout(server->desk_w, server->desk_h, r, cal);
	return clampf_d(((r[0] + r[2] * 0.5) / fmax(server->desk_w, 1) * 2.0 - 1.0) * 0.7, -0.7, 0.7);
}

/* ---- notes in the air ----
 *
 * Written and stuck, a note is peeled off the binder's page -- its foot
 * first, coming up off the paper -- carried over to the calendar along an
 * arc, a little nearer the eye on the way, and pressed down on its day
 * where the page has room for it: as it goes it takes on the look it will
 * have there (agd_fly_state). Clicked to be written again, it comes back
 * off the calendar the same way; peeled off, it comes away and drops. */

#define AGD_FLY_S 0.82
#define AGD_FLY_OFF_S 0.62
#define AGD_FLY_PEEL 0.22   /* of the way: coming up off the paper */
#define AGD_FLY_LAND 0.80   /* and pressed down from here on */

/* A pose from where the corners of pose p went (top left, top right,
 * bottom right, bottom left): as near a turned rectangle as they make. */
static void agd_pose_from_quad(const double q[4][2], const struct desk_note_pose *p,
	struct desk_note_pose *out)
{
	double cx = (q[0][0] + q[1][0] + q[2][0] + q[3][0]) * 0.25;
	double cy = (q[0][1] + q[1][1] + q[2][1] + q[3][1]) * 0.25;
	double nw = (hypot(q[1][0] - q[0][0], q[1][1] - q[0][1])
		+ hypot(q[2][0] - q[3][0], q[2][1] - q[3][1])) * 0.5;
	double nh = (hypot(q[3][0] - q[0][0], q[3][1] - q[0][1])
		+ hypot(q[2][0] - q[1][0], q[2][1] - q[1][1])) * 0.5;
	*out = *p;
	out->rot = atan2(q[1][1] - q[0][1] + q[2][1] - q[3][1], q[1][0] - q[0][0] + q[2][0] - q[3][0]);
	out->font *= p->w > 1e-6 ? nw / p->w : 1.0;
	out->w = nw;
	out->h = nh;
	out->x = cx - nw * 0.5;
	out->y = cy - nh * 0.5;
}

/* a pose on the binder or the calendar, on the screen as they are now */
static void agd_fly_place(const struct desk_server *server, const struct desk_note_pose *p,
	int on, struct desk_note_pose *out)
{
	*out = *p;
	if (on == AGD_ON_BINDER) {
		/* its corners turned with the binder as it is held, where the
		 * binder is now */
		double r[4];
		agd_rect(server, r);
		struct desk_agenda_look lk;
		agd_look(server, &lk);
		double c = cos(p->rot), sn = sin(p->rot);
		double mx = p->x + p->w * 0.5, my = p->y + p->h * 0.5;
		double q[4][2];
		for (int i = 0; i < 4; i++) {
			double u = (i == 1 || i == 2) ? p->w * 0.5 : -p->w * 0.5;
			double v = i >= 2 ? p->h * 0.5 : -p->h * 0.5;
			double x = mx + u * c - v * sn, y = my + u * sn + v * c;
			desk_agenda_turn_point(r[2], r[3], &lk, &x, &y);
			q[i][0] = x + r[0];
			q[i][1] = y + r[1];
		}
		agd_pose_from_quad(q, p, out);
	} else {
		/* its corners turned with the calendar as it is held, then where
		 * the calendar is: as near a turned rectangle as they make */
		struct desk_cal_look lk;
		cal_look_now(server, &lk);
		double xf[3];
		cal_aside(server, xf);
		double c = cos(p->rot), sn = sin(p->rot);
		double mx = p->x + p->w * 0.5, my = p->y + p->h * 0.5;
		double q[4][2];
		for (int i = 0; i < 4; i++) {
			double u = (i == 1 || i == 2) ? p->w * 0.5 : -p->w * 0.5;
			double v = i >= 2 ? p->h * 0.5 : -p->h * 0.5;
			double x = mx + u * c - v * sn, y = my + u * sn + v * c;
			desk_calendar_turn_point(server->desk_w, server->desk_h, &lk, &x, &y);
			q[i][0] = x * xf[0] + xf[1];
			q[i][1] = y * xf[0] + xf[2];
		}
		agd_pose_from_quad(q, p, out);
	}
}

static double agd_smooth(double u)
{
	u = clampf_d(u, 0.0, 1.0);
	return u * u * (3.0 - 2.0 * u);
}

struct agd_fly_args {
	struct desk_stick s;
	struct desk_note_pose at, a, b;
	double look, lift, alpha;
};

/* Where flight f has got to: its paper (at), how much of the look of
 * where it is going it has (look), how far off the paper it is (lift),
 * how faded (alpha). False once it is over. */
static bool agd_fly_state(const struct desk_server *server, const struct agd_flight *f,
	double now, struct agd_fly_args *o)
{
	struct desk_note_pose a, b;
	agd_fly_place(server, &f->a, f->a_on, &a);
	agd_fly_place(server, &f->b, f->b_on, &b);
	o->s = f->s;
	o->a = a;
	o->b = b;
	double u = clampf_d((now - f->start) / f->dur, 0.0, 1.0);
	/* in the binder a note being written lies a little up off the page */
	double base_a = f->a_on == AGD_ON_BINDER ? 0.35 : 0.0;
	double base_b = f->b_on == AGD_ON_BINDER ? 0.35 : 0.0;
	if (f->mode == AGD_FLY_OFF) {
		/* up off the paper, then dropping away, turning, gone */
		double p = agd_smooth(u / 0.30), q = clampf_d((u - 0.30) / 0.70, 0.0, 1.0);
		o->at = a;
		o->at.y += -a.h * 0.30 * p + server->desk_h * 0.45 * q * q;
		o->at.x += a.w * 0.12 * q;
		o->at.rot += -0.10 * p + 0.80 * q * q;
		o->look = 0.0;
		o->lift = base_a + (1.0 - base_a) * p;
		o->alpha = 1.0 - q * q;
		return u < 1.0;
	}
	double p = agd_smooth(u / AGD_FLY_PEEL);
	double q = agd_smooth((u - AGD_FLY_PEEL) / (AGD_FLY_LAND - AGD_FLY_PEEL));
	double l = agd_smooth((u - AGD_FLY_LAND) / (1.0 - AGD_FLY_LAND));
	/* along an arc over from the middle of the one to the middle of the
	 * other, rising the more the farther it goes */
	double x0 = a.x + a.w * 0.5, y0 = a.y + a.h * 0.5;
	double x2 = b.x + b.w * 0.5, y2 = b.y + b.h * 0.5;
	double x1 = (x0 + x2) * 0.5, y1 = fmin(y0, y2) - fabs(x2 - x0) * 0.22 - server->desk_h * 0.04;
	double mx = (1 - q) * (1 - q) * x0 + 2 * (1 - q) * q * x1 + q * q * x2;
	double my = (1 - q) * (1 - q) * y0 + 2 * (1 - q) * q * y1 + q * q * y2;
	/* lifted off before it goes */
	my -= a.h * 0.20 * p * (1.0 - q);
	double near = 1.0 + 0.10 * sin(3.14159265358979 * q) + 0.05 * (1.0 - l) * (q >= 1.0 ? 1.0 : 0.0);
	double w = (a.w + (b.w - a.w) * q) * near, h = (a.h + (b.h - a.h) * q) * near;
	double side = x2 >= x0 ? 1.0 : -1.0;
	o->at.x = mx - w * 0.5;
	o->at.y = my - h * 0.5;
	o->at.w = w;
	o->at.h = h;
	o->at.rot = a.rot + (b.rot - a.rot) * q - 0.07 * p * (1.0 - q) + 0.12 * side * sin(3.14159265358979 * q);
	o->at.font = 0.0;
	o->at.style = b.style;
	o->look = agd_smooth((q - 0.30) / 0.40);
	o->lift = q < 1.0 ? base_a + (1.0 - base_a) * p : 1.0 + (base_b - 1.0) * l;
	o->alpha = 1.0;
	return u < 1.0;
}

static void agd_fly_draw(cairo_t *cr, int w, int h, void *data)
{
	(void)w;
	(void)h;
	const struct agd_fly_args *f = data;
	desk_draw_note_between(cr, &f->s, &f->at, &f->a, &f->b, f->look, f->lift, f->alpha);
}

/* the notes in the air over the calendar and the binder */
static void agd_fly_raise(struct desk_server *server)
{
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		struct agd_flight *f = &server->agd_fly[i];
		if (f->mode != AGD_FLY_NONE && f->sp.node) {
			wlr_scene_node_raise_to_top(&f->sp.node->node);
		}
	}
}

static void agd_fly_paint(struct desk_server *server, struct agd_flight *f, double now)
{
	struct agd_fly_args a;
	if (!server->cal_tree || f->mode == AGD_FLY_NONE) {
		return;
	}
	agd_fly_state(server, f, now, &a);
	/* all of it, turned, and its shadow */
	double cx = a.at.x + a.at.w * 0.5, cy = a.at.y + a.at.h * 0.5;
	double c = fabs(cos(a.at.rot)), sn = fabs(sin(a.at.rot));
	double hw = (a.at.w * c + a.at.h * sn) * 0.5, hh = (a.at.w * sn + a.at.h * c) * 0.5;
	double m = fmin(a.at.w, a.at.h) * 0.45 + 3.0;
	double box[4] = { cx - hw - m, cy - hh - m, hw * 2.0 + m * 2.0, hh * 2.0 + m * 2.5 };
	sprite_paint(server, &f->sp, server->cal_tree, box, agd_fly_draw, &a);
	if (f->sp.node) {
		wlr_scene_node_raise_to_top(&f->sp.node->node);
	}
}

static void agd_fly_end(struct agd_flight *f)
{
	f->mode = AGD_FLY_NONE;
	if (f->sp.node) {
		wlr_scene_node_set_enabled(&f->sp.node->node, false);
	}
	f->sp.drawn = false;
}

static void agd_fly_clear(struct desk_server *server)
{
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		agd_fly_end(&server->agd_fly[i]);
	}
	server->agd_edit_away = false;
}

static bool agd_flying_to(const struct desk_server *server, int mode)
{
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		if (server->agd_fly[i].mode == mode) {
			return true;
		}
	}
	return false;
}

/* Send note s from a (on a_on) to b (on b_on): one already in the air
 * with it is put down first. */
static void agd_fly_start(struct desk_server *server, int mode, const struct desk_stick *s,
	const struct desk_note_pose *a, int a_on, const struct desk_note_pose *b, int b_on)
{
	struct agd_flight *f = NULL;
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		struct agd_flight *g = &server->agd_fly[i];
		if (g->mode != AGD_FLY_NONE && g->s.id == s->id) {
			if (g->mode == AGD_FLY_TO_BINDER) {
				server->agd_edit_away = false;
			}
			agd_fly_end(g);
		}
		if (g->mode == AGD_FLY_NONE && !f) {
			f = g;
		}
	}
	if (!f) {
		/* the oldest lands at once */
		f = &server->agd_fly[0];
		for (int i = 1; i < AGD_FLIGHTS; i++) {
			if (server->agd_fly[i].start < f->start) {
				f = &server->agd_fly[i];
			}
		}
		/* the note being written, if that was it, is in the binder now */
		if (f->mode == AGD_FLY_TO_BINDER && f->s.id == server->agd_edit) {
			server->agd_edit_away = false;
		}
		agd_fly_end(f);
	}
	f->mode = mode;
	f->s = *s;
	f->a = *a;
	f->b = *b;
	f->a_on = a_on;
	f->b_on = b_on;
	f->start = monotonic_sec();
	f->dur = mode == AGD_FLY_OFF ? AGD_FLY_OFF_S : AGD_FLY_S;
	f->snd = 0;
	desk_sound_play(DESK_SND_CORNER_LIFT, agd_pan(server), mode == AGD_FLY_OFF ? 1.0 : 0.8,
		1.1);
	if (mode == AGD_FLY_OFF) {
		desk_sound_play_in(DESK_SND_PAPER_TOSS, agd_pan(server), 0.35, 1.3,
			real_secs(f->dur * 0.3) + SND_SEEN);
	}
	agd_fly_paint(server, f, f->start);
	server_schedule_frames(server);
}

static void agd_fly_tick(struct desk_server *server, double now)
{
	bool any = false;
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		any |= server->agd_fly[i].mode != AGD_FLY_NONE;
	}
	if (!any) {
		return;
	}
	server_schedule_frames(server);
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		struct agd_flight *f = &server->agd_fly[i];
		if (f->mode == AGD_FLY_NONE) {
			continue;
		}
		if (f->mode != AGD_FLY_OFF) {
			/* pressed down where it lands */
			double pan = f->b_on == AGD_ON_BINDER ? agd_pan(server) : cal_pan(server) * 0.5;
			snd_end(&f->snd, f->start, f->dur * AGD_FLY_LAND, DESK_SND_PAPER_PRESS, pan, 0.9,
				1.25);
		}
		struct agd_fly_args a;
		if (agd_fly_state(server, f, now, &a)) {
			agd_fly_paint(server, f, now);
			continue;
		}
		int mode = f->mode;
		agd_fly_end(f);
		if (mode == AGD_FLY_TO_CAL) {
			/* stuck on its day: on the page now, and the one on the shelf */
			paint_calendar(server);
			paint_shelf(server);
		} else if (mode == AGD_FLY_TO_BINDER) {
			server->agd_edit_away = false;
			paint_agenda(server);
		}
	}
}

/* The note being written, as a pose on the binder: false if none is. */
static bool agd_edit_pose(const struct desk_server *server, struct desk_note_pose *p);

/* Note s, just stuck (or put back as it was), from where it is in the
 * binder over to its day on the calendar -- the calendar turned to it
 * first if it is not on the page. */
static void cal_show(struct desk_server *server, int y, int m, int d);

static void agd_send(struct desk_server *server, const struct desk_stick *s,
	const struct desk_note_pose *from)
{
	cal_show(server, s->y, s->m, s->d);
	struct desk_cal_look lk;
	cal_look_settled(server, &lk);
	struct desk_note_pose to;
	if (!desk_calendar_note_pose(server->desk_w, server->desk_h, &lk, s, &to)) {
		/* its day is not on the page after all: into the middle of it */
		double pg[4], box[4];
		desk_calendar_rect(server->desk_w, server->desk_h, pg, box);
		to = (struct desk_note_pose){ pg[0] + pg[2] * 0.5, pg[1] + pg[3] * 0.5, 2.0, 2.0, 0.0,
			0.0, DESK_NOTE_DOT };
	}
	agd_fly_start(server, AGD_FLY_TO_CAL, s, from, AGD_ON_BINDER, &to, AGD_ON_CAL);
}

/* Turn the calendar to the page with day y-m-d on it: over a page or a
 * few, or, farther than that, opened there. */
static void cal_show(struct desk_server *server, int y, int m, int d)
{
	struct desk_cal_look lk;
	cal_look_settled(server, &lk);
	if (desk_calendar_page_has(lk.view, lk.year, lk.month, lk.day, y, m, d)) {
		if (!server->cal_turn && !server->cal_more) {
			/* on this page already: about that day, for the other
			 * sections to open at */
			server->cal_year = y;
			server->cal_month = m;
			server->cal_day = d;
		}
		return;
	}
	if (cal_settled(server) && !cal_switching(server)) {
		for (int n = 1; n <= 4; n++) {
			for (int dir = -1; dir <= 1; dir += 2) {
				int yy = lk.year, mm = lk.month, dd = lk.day;
				desk_calendar_step(lk.view, dir * n, &yy, &mm, &dd);
				if (!desk_calendar_page_has(lk.view, yy, mm, dd, y, m, d)) {
					continue;
				}
				int left = n;
				if (!server->cal_turn) {
					cal_turn_page(server, dir);
					left--;
				}
				if (abs(server->cal_more + dir * left) <= 4) {
					server->cal_more += dir * left;
					return;
				}
				goto jump;
			}
		}
	}
jump:
	server->cal_turn = 0;
	server->cal_more = 0;
	server->cal_year = y;
	server->cal_month = m;
	server->cal_day = d;
	paint_calendar(server);
	server_schedule_frames(server);
}

/* ---- writing on a note ---- */

static int utf8_prev(const char *s, int i)
{
	while (i > 0) {
		i--;
		if (((unsigned char)s[i] & 0xc0) != 0x80) {
			break;
		}
	}
	return i;
}

static int utf8_next(const char *s, int i)
{
	int n = (int)strlen(s);
	if (i >= n) {
		return n;
	}
	i++;
	while (i < n && ((unsigned char)s[i] & 0xc0) == 0x80) {
		i++;
	}
	return i;
}

static void agd_insert(struct desk_server *server, const char *t)
{
	size_t n = strlen(server->agd_text), k = strlen(t);
	if (n + k >= sizeof server->agd_text - 1) {
		return;
	}
	int c = server->agd_caret;
	memmove(server->agd_text + c + k, server->agd_text + c, n - (size_t)c + 1);
	memcpy(server->agd_text + c, t, k);
	server->agd_caret = c + (int)k;
}

static void agd_clear_ime(struct desk_server *server)
{
	server->agd_pre[0] = '\0';
	server->agd_pre_caret = 0;
	server->agd_ncand = 0;
	server->agd_keyq_n = 0;
}

static void agd_repeat_stop(struct desk_server *server)
{
	server->agd_repeat_sym = 0;
	if (server->agd_repeat) {
		wl_event_source_timer_update(server->agd_repeat, 0);
	}
}

/* The note being written, as a pose on the binder: false if none is. */
static bool agd_edit_pose(const struct desk_server *server, struct desk_note_pose *p)
{
	if (!server->agd_edit) {
		return false;
	}
	struct desk_agenda_look lk;
	agd_look(server, &lk);
	double r[4], cal[3];
	desk_agenda_layout(server->desk_w, server->desk_h, r, cal);
	return desk_agenda_note_pose(r[2], r[3], &lk, p);
}

/* Where note id is in the air just now, as a pose on the binder: false if
 * it is not in the air. */
static bool agd_fly_where(const struct desk_server *server, int id,
	struct desk_note_pose *p)
{
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		const struct agd_flight *f = &server->agd_fly[i];
		struct agd_fly_args a;
		if (f->mode == AGD_FLY_NONE || f->s.id != id) {
			continue;
		}
		agd_fly_state(server, f, monotonic_sec(), &a);
		/* where it is, taken back into the binder's own terms */
		double r[4];
		agd_rect(server, r);
		struct desk_agenda_look lk;
		agd_look(server, &lk);
		double c = cos(a.at.rot), sn = sin(a.at.rot);
		double mx = a.at.x + a.at.w * 0.5, my = a.at.y + a.at.h * 0.5;
		double q[4][2];
		for (int k = 0; k < 4; k++) {
			double u = (k == 1 || k == 2) ? a.at.w * 0.5 : -a.at.w * 0.5;
			double v = k >= 2 ? a.at.h * 0.5 : -a.at.h * 0.5;
			double x = mx + u * c - v * sn - r[0], y = my + u * sn + v * c - r[1];
			desk_agenda_unturn(r[2], r[3], &lk, &x, &y);
			q[k][0] = x;
			q[k][1] = y;
		}
		struct desk_note_pose was = a.at;
		was.font = f->b_on == AGD_ON_BINDER ? f->b.font : f->a.font;
		agd_pose_from_quad(q, &was, p);
		p->style = DESK_NOTE_BINDER;
		return true;
	}
	return false;
}

/* Stop writing: whatever the input method had, the keys held, the ruler. */
static void agd_let_go(struct desk_server *server)
{
	agd_repeat_stop(server);
	desk_ime_typing_reset();
	agd_clear_ime(server);
	server->agd_edit = 0;
	server->agd_peeling = false;
	server->agd_edit_away = false;
	server->agd_drag = false;
	server->agd_mark = -1;
}

/* Put the note down: stuck (keep), or as it was. Either way it is peeled
 * off the binder's page and goes to the calendar: written on, to stick
 * there; an old one put back as it was, back to its place; one with
 * nothing on it is no note, and an old one wiped clean comes off. */
static void agd_finish(struct desk_server *server, bool keep)
{
	if (!server->agd_edit) {
		return;
	}
	if (keep && server->agd_pre[0]) {
		/* what the input method had not yet written goes on as it is */
		agd_insert(server, server->agd_pre);
	}
	int id = server->agd_edit;
	int kind = agd_edit_kind(server);
	int span, when = agd_time(server, &span);
	/* where it is as it is let go: from there it goes */
	struct desk_note_pose from;
	bool have_from = server->agd_edit_away ? agd_fly_where(server, id, &from)
		: agd_edit_pose(server, &from);
	agd_let_go(server);
	server->agd_when = when;
	server->agd_span = span;
	struct desk_stick s = { 0 };
	if (id > 0 && !desk_agenda_get(id, &s)) {
		agd_stuck(server);
		return;
	}
	if (!keep) {
		if (id > 0 && have_from) {
			agd_send(server, &s, &from);
		}
		agd_stuck(server);
		return;
	}
	char *t = server->agd_text;
	while (*t == ' ') {
		t++;
	}
	size_t n = strlen(t);
	while (n > 0 && t[n - 1] == ' ') {
		t[--n] = '\0';
	}
	if (id <= 0) {
		s.y = server->agd_y;
		s.m = server->agd_m;
		s.d = server->agd_d;
		s.kind = kind;
	}
	s.colour = server->agd_edit_colour;
	s.minute = when;
	s.span = when >= 0 ? span : 0;
	const char *rest = t;
	int sp;
	if (kind != DESK_STICK_PLAN || desk_agenda_when(t, &sp, &rest) < 0) {
		rest = t;
	}
	if (!*t) {
		/* nothing written: an old note comes off, a new one never was */
		if (id > 0) {
			desk_agenda_remove(id);
			if (have_from) {
				agd_fly_start(server, AGD_FLY_OFF, &s, &from, AGD_ON_BINDER, &from,
					AGD_ON_BINDER);
			}
		}
		agd_stuck(server);
		return;
	}
	snprintf(s.text, sizeof s.text, "%s", rest);
	int nid = id;
	if (id > 0) {
		desk_agenda_set(&s);
	} else {
		nid = desk_agenda_add(&s);
	}
	struct desk_stick saved;
	if (nid > 0 && desk_agenda_get(nid, &saved) && have_from) {
		agd_send(server, &saved, &from);
	}
	agd_stuck(server);
}

/* Start writing: on note id, or a new one (DESK_AGENDA_NEW_PLAN/TODO)
 * peeled off the pad picked. An old one is taken off the calendar and
 * comes over to the binder to be written on. */
static void agd_begin(struct desk_server *server, int id)
{
	agd_finish(server, true);
	server->agd_text[0] = '\0';
	server->agd_caret = 0;
	agd_clear_ime(server);
	server->agd_when = -1;
	server->agd_span = 0;
	server->agd_mark = -1;
	server->agd_edit_away = false;
	if (id > 0) {
		struct desk_stick s;
		if (!desk_agenda_get(id, &s)) {
			return;
		}
		snprintf(server->agd_text, sizeof server->agd_text, "%s", s.text);
		server->agd_caret = (int)strlen(server->agd_text);
		server->agd_edit_colour = s.colour;
		server->agd_when = s.minute;
		server->agd_span = s.minute >= 0 ? s.span : 0;
		server->agd_peeling = false;
		/* where it is on the calendar, before it is taken off it */
		struct desk_cal_look lk;
		cal_look_now(server, &lk);
		struct desk_note_pose from, to;
		bool on_cal = cal_settled(server) && !server->cal_turn && !cal_switching(server)
			&& desk_calendar_note_pose(server->desk_w, server->desk_h, &lk, &s, &from);
		server->agd_edit = id;
		if (on_cal && agd_edit_pose(server, &to)) {
			server->agd_edit_away = true;
			agd_fly_start(server, AGD_FLY_TO_BINDER, &s, &from, AGD_ON_CAL, &to, AGD_ON_BINDER);
		}
	} else {
		server->agd_edit_colour = server->agd_colour;
		server->agd_peeling = true;
		server->agd_peel_start = monotonic_sec();
		desk_sound_play(DESK_SND_CORNER_LIFT, agd_pan(server), 0.9, 1.15);
		server->agd_edit = id;
	}
	paint_calendar(server);
	agd_changed(server);
}

/* What is written at the front of the note as a time goes, the time being
 * set on the ruler instead. */
static void agd_untype_time(struct desk_server *server)
{
	const char *rest;
	int sp;
	if (desk_agenda_when(server->agd_text, &sp, &rest) < 0) {
		return;
	}
	int cut = (int)(rest - server->agd_text);
	memmove(server->agd_text, server->agd_text + cut, strlen(server->agd_text + cut) + 1);
	server->agd_caret = server->agd_caret > cut ? server->agd_caret - cut : 0;
}

/* The ruler: pressed at minute -- the time from there, or what was set
 * moved there as it was -- and drawn along, from there to where it is. */
static void agd_ruler_press(struct desk_server *server, int minute)
{
	/* what it was at: written, or on the ruler */
	int span, when = agd_time(server, &span);
	agd_untype_time(server);
	server->agd_drag = true;
	server->agd_drag_a = minute;
	server->agd_drag_keep = when >= 0 ? span : 0;
	server->agd_when = minute > 24 * 60 - 15 ? 24 * 60 - 15 : minute;
	server->agd_span = server->agd_drag_keep;
	server->agd_mark = minute;
	desk_sound_play(DESK_SND_PENCIL_TICK, agd_pan(server), 0.35, 1.3);
	agd_changed(server);
}

static void agd_drag_to(struct desk_server *server, double x, double y)
{
	double r[4];
	agd_rect(server, r);
	struct desk_agenda_look lk;
	agd_look(server, &lk);
	double lx = x - r[0], ly = y - r[1];
	desk_agenda_unturn(r[2], r[3], &lk, &lx, &ly);
	int b = desk_agenda_ruler_minute(r[2], r[3], lx), a = server->agd_drag_a;
	int when = server->agd_when, span = server->agd_span;
	if (b != a) {
		when = a < b ? a : b;
		span = abs(b - a);
	} else {
		when = a > 24 * 60 - 15 ? 24 * 60 - 15 : a;
		span = server->agd_drag_keep;
	}
	if (when != server->agd_when || span != server->agd_span || b != server->agd_mark) {
		server->agd_when = when;
		server->agd_span = span;
		server->agd_mark = b;
		agd_changed(server);
	}
}

/* the wheel on the ruler: the time a quarter of an hour on or back */
static void agd_ruler_roll(struct desk_server *server, int dir)
{
	int span;
	int when = agd_time(server, &span);
	agd_untype_time(server);
	if (when < 0) {
		when = server->agd_mark >= 0 ? server->agd_mark : 9 * 60;
		span = 0;
	} else {
		when += 15 * dir;
	}
	server->agd_when = when < 0 ? 0 : (when > 24 * 60 - 15 ? 24 * 60 - 15 : when);
	server->agd_span = span;
	desk_sound_play(DESK_SND_KNOB_TICK, agd_pan(server), 0.30, 1.4);
	agd_changed(server);
}

static void agd_plain(struct desk_server *server, uint32_t sym, const char *utf8, uint32_t mods);

static int agd_repeat_cb(void *data)
{
	struct desk_server *server = data;
	if (!server->agd_repeat_sym || !server->agd_edit) {
		return 0;
	}
	agd_plain(server, server->agd_repeat_sym, "", 0);
	wl_event_source_timer_update(server->agd_repeat, 33);
	return 0;
}

/* a key the input method did not take, or no input method: what a key
 * does on paper */
static void agd_plain(struct desk_server *server, uint32_t sym, const char *utf8, uint32_t mods)
{
	if (!server->agd_edit) {
		return;
	}
	char *t = server->agd_text;
	int n = (int)strlen(t), c = server->agd_caret;
	bool rep = false;
	switch (sym) {
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		agd_finish(server, true);
		return;
	case XKB_KEY_Escape:
		agd_finish(server, false);
		return;
	case XKB_KEY_BackSpace:
		if (c > 0) {
			int p = utf8_prev(t, c);
			memmove(t + p, t + c, (size_t)(n - c + 1));
			server->agd_caret = p;
		}
		rep = true;
		break;
	case XKB_KEY_Delete:
		if (c < n) {
			int q = utf8_next(t, c);
			memmove(t + c, t + q, (size_t)(n - q + 1));
		}
		rep = true;
		break;
	case XKB_KEY_Left:
		server->agd_caret = utf8_prev(t, c);
		rep = true;
		break;
	case XKB_KEY_Right:
		server->agd_caret = utf8_next(t, c);
		rep = true;
		break;
	case XKB_KEY_Home:
		server->agd_caret = 0;
		break;
	case XKB_KEY_End:
		server->agd_caret = n;
		break;
	default:
		if (utf8 && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f
			&& !(mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))) {
			agd_insert(server, utf8);
		}
		break;
	}
	if (rep && server->agd_repeat_sym != sym && sym == server->agd_held_sym && utf8) {
		/* held down, it goes on: a moment's wait, then thirty a second */
		server->agd_repeat_sym = sym;
		if (!server->agd_repeat) {
			server->agd_repeat = wl_event_loop_add_timer(
				wl_display_get_event_loop(server->wl_display), agd_repeat_cb, server);
		}
		if (server->agd_repeat) {
			wl_event_source_timer_update(server->agd_repeat, 450);
		}
	}
	agd_changed(server);
}

/* ---- what the input method sends back ---- */

static void agd_ime_commit(const char *text, void *user)
{
	struct desk_server *server = user;
	if (!server->agd_edit) {
		return;
	}
	agd_insert(server, text);
	server->agd_pre[0] = '\0';
	agd_changed(server);
}

static void agd_ime_preedit(const char *text, int cursor, bool visible, void *user)
{
	struct desk_server *server = user;
	snprintf(server->agd_pre, sizeof server->agd_pre, "%s", visible ? text : "");
	/* its caret is counted in letters */
	int b = 0;
	for (int i = 0; i < cursor && server->agd_pre[b]; i++) {
		b = utf8_next(server->agd_pre, b);
	}
	server->agd_pre_caret = b;
	if (server->agd_edit) {
		agd_changed(server);
	}
}

static void agd_ime_lookup(const char *const *cands, int n, int cursor, int page, bool visible,
	void *user)
{
	struct desk_server *server = user;
	server->agd_ncand = 0;
	if (visible && n > 0) {
		/* the page the picked one is on */
		page = page > 0 ? (page > 16 ? 16 : page) : 9;
		cursor = cursor < 0 ? 0 : (cursor >= n ? n - 1 : cursor);
		int first = cursor / page * page;
		for (int i = first; i < n && server->agd_ncand < page; i++) {
			snprintf(server->agd_cand[server->agd_ncand], sizeof server->agd_cand[0], "%s",
				cands[i]);
			server->agd_cand_p[server->agd_ncand] = server->agd_cand[server->agd_ncand];
			server->agd_ncand++;
		}
		server->agd_cand_cursor = cursor - first;
		server->agd_cand_page = page;
	}
	if (server->agd_edit) {
		agd_changed(server);
	}
}

static void agd_ime_key(uint32_t sym, uint32_t keycode, uint32_t mods, bool taken, bool answer,
	void *user)
{
	(void)keycode;
	struct desk_server *server = user;
	char utf8[8] = "";
	if (answer) {
		/* the oldest key handed over: what it would type */
		if (server->agd_keyq_n > 0) {
			memcpy(utf8, server->agd_keyq[0].utf8, sizeof utf8);
			server->agd_keyq_n--;
			memmove(&server->agd_keyq[0], &server->agd_keyq[1],
				sizeof server->agd_keyq[0] * (size_t)server->agd_keyq_n);
		}
	} else {
		xkb_keysym_to_utf8(sym, utf8, sizeof utf8);
	}
	if (!taken) {
		agd_plain(server, sym, utf8, mods);
	}
}

/* ---- in and out ---- */

static void agd_move(struct desk_server *server, double to, double dur)
{
	server->agd_from = server->agd_t;
	server->agd_to = to;
	server->agd_start = monotonic_sec();
	server->agd_dur = dur;
	server->agd_moving = 1;
	server_schedule_frames(server);
	paint_agenda(server);
}

/* Open at day y-m-d (month 0-11) -- the binder brought in, or turned to it;
 * start: a thing to do there, or a plan, at when (minutes; -1 none). */
enum { AGD_START_NONE, AGD_START_TODO, AGD_START_PLAN };
static void agd_open(struct desk_server *server, int y, int m, int d, int start, int when)
{
	bool in = agd_up(server) && server->agd_to > 0.5;
	if (in && (y != server->agd_y || m != server->agd_m || d != server->agd_d)) {
		agd_finish(server, true);
		desk_sound_play(DESK_SND_PAGE_TURN, agd_pan(server), 0.7, 1.15);
	}
	server->agd_y = y;
	server->agd_m = m;
	server->agd_d = d;
	/* the calendar goes to the day too, once what is in the air is down */
	server->agd_follow = true;
	if (!in) {
		server->agd_hover = 0;
		static const struct desk_ime_typing hooks = { agd_ime_commit, agd_ime_preedit,
			agd_ime_lookup, agd_ime_key, NULL };
		struct desk_ime_typing h = hooks;
		h.user = server;
		server->agd_ime = desk_ime_typing_begin(&h);
		things_text_wanted(server, true);
		server->agd_sp.drawn = false;
		server->agd_mark_sp.drawn = false;
		desk_sound_play(DESK_SND_BINDER_LIFT, agd_pan(server), 0.8, 1.1);
		agd_move(server, 1.0, AGD_IN_S);
	}
	if (start == AGD_START_TODO) {
		agd_begin(server, DESK_AGENDA_NEW_TODO);
	} else if (start == AGD_START_PLAN) {
		agd_begin(server, DESK_AGENDA_NEW_PLAN);
		server->agd_when = when;
		server->agd_span = 0;
	}
	paint_calendar(server);
	agd_changed(server);
}

static void agd_close(struct desk_server *server)
{
	if (!agd_up(server) || server->agd_to < 0.5) {
		return;
	}
	agd_finish(server, true);
	desk_ime_typing_end();
	things_text_wanted(server, false);
	server->agd_ime = false;
	server->agd_hover = 0;
	desk_sound_play(DESK_SND_BINDER_CLOSE, agd_pan(server), 0.7, 1.1);
	agd_move(server, 0.0, AGD_OUT_S);
}

/* gone at once, with the calendar or the shelf */
static void agd_drop(struct desk_server *server)
{
	if (server->agd_edit) {
		agd_finish(server, true);
	}
	if (agd_up(server)) {
		desk_ime_typing_end();
		things_text_wanted(server, false);
	}
	agd_repeat_stop(server);
	server->agd_ime = false;
	server->agd_t = 0.0;
	server->agd_to = 0.0;
	server->agd_moving = 0;
	if (server->agd_gone_id) {
		desk_agenda_remove(server->agd_gone_id);
		server->agd_gone_id = 0;
	}
	server->agd_ruler_t = 0.0;
	server->agd_drag = false;
	server->agd_mark = -1;
	server->agd_follow = false;
	if (server->agd_sp.node) {
		wlr_scene_node_set_enabled(&server->agd_sp.node->node, false);
	}
	if (server->agd_mark_sp.node) {
		wlr_scene_node_set_enabled(&server->agd_mark_sp.node->node, false);
	}
}

/* Peeled off: a note comes away from wherever it is -- the calendar, or
 * the binder where it is being written on -- and drops, and is rubbed out
 * of the binder. */
static void agd_peel_off(struct desk_server *server, int id)
{
	struct desk_stick s;
	if (!desk_agenda_get(id, &s)) {
		return;
	}
	struct desk_note_pose p;
	if (server->agd_edit == id) {
		bool have = server->agd_edit_away ? agd_fly_where(server, id, &p)
			: agd_edit_pose(server, &p);
		agd_let_go(server);
		desk_agenda_remove(id);
		if (have) {
			agd_fly_start(server, AGD_FLY_OFF, &s, &p, AGD_ON_BINDER, &p, AGD_ON_BINDER);
		}
		agd_stuck(server);
		return;
	}
	struct desk_cal_look lk;
	cal_look_now(server, &lk);
	if (cal_settled(server) && !server->cal_turn && !cal_switching(server)
		&& desk_calendar_note_pose(server->desk_w, server->desk_h, &lk, &s, &p)) {
		agd_fly_start(server, AGD_FLY_OFF, &s, &p, AGD_ON_CAL, &p, AGD_ON_CAL);
	}
	if (agd_up(server) && server->agd_to > 0.5 && s.y == server->agd_y && s.m == server->agd_m
		&& s.d == server->agd_d) {
		/* rubbed out of the binder, and gone when it is */
		if (server->agd_gone_id) {
			desk_agenda_remove(server->agd_gone_id);
		}
		server->agd_gone_id = id;
		server->agd_gone_start = monotonic_sec();
	} else {
		desk_agenda_remove(id);
	}
	agd_stuck(server);
}

static void agd_tick(struct desk_server *server)
{
	if (!agd_up(server)) {
		return;
	}
	double now = monotonic_sec();
	bool changed = false;
	if (server->agd_moving) {
		double u = clampf_d((now - server->agd_start) / server->agd_dur, 0, 1);
		double e;
		if (server->agd_to > 0.5) {
			/* in from the right, a hair past its place and back */
			const double c1 = 1.25, c3 = c1 + 1.0;
			e = 1.0 + c3 * pow(u - 1.0, 3.0) + c1 * pow(u - 1.0, 2.0);
		} else {
			e = u * u;
		}
		server->agd_t = server->agd_from + (server->agd_to - server->agd_from) * e;
		if (u >= 1.0) {
			server->agd_t = server->agd_to;
			server->agd_moving = 0;
		}
		changed = true;
	}
	if (server->agd_peeling) {
		if (now - server->agd_peel_start >= AGD_PEEL_S) {
			server->agd_peeling = false;
		}
		changed = true;
	}
	if (server->agd_gone_id) {
		if (now - server->agd_gone_start >= AGD_GONE_S) {
			desk_agenda_remove(server->agd_gone_id);
			server->agd_gone_id = 0;
			agd_stuck(server);
		}
		changed = true;
	}
	/* the ruler out while a plan is being written */
	double want = server->agd_edit && !server->agd_edit_away
		&& agd_edit_kind(server) == DESK_STICK_PLAN ? 1.0 : 0.0;
	double dt = server->agd_ruler_at > 0.0 ? fmin(now - server->agd_ruler_at, 0.05) : 0.0;
	server->agd_ruler_at = now;
	if (server->agd_ruler_t != want) {
		double step = gfx_flat() ? 1.0 : dt / 0.22;
		server->agd_ruler_t = want > server->agd_ruler_t ? fmin(want, server->agd_ruler_t + step)
			: fmax(want, server->agd_ruler_t - step);
		changed = true;
	}
	/* the calendar after the binder's day, once nothing is on its way to it */
	if (server->agd_follow && server->agd_to > 0.5 && !agd_flying_to(server, AGD_FLY_TO_CAL)) {
		server->agd_follow = false;
		cal_show(server, server->agd_y, server->agd_m, server->agd_d);
	}
	if (server->agd_t <= 0.0005 && !server->agd_moving) {
		agd_drop(server);
		paint_calendar(server);
		server_schedule_frames(server);
		return;
	}
	if (changed) {
		paint_agenda(server);
		paint_calendar(server);
		server_schedule_frames(server);
	}
}

/* ---- the keys and the pointer, while it is in ---- */

static bool agd_has_keys(const struct desk_server *server)
{
	return agd_up(server) && server->agd_to > 0.5;
}

static void agd_key(struct desk_server *server, struct xkb_state *xkb, uint32_t keycode,
	uint32_t sym, uint32_t mods, bool released)
{
	char utf8[8] = "";
	xkb_state_key_get_utf8(xkb, keycode + 8, utf8, sizeof utf8);
	if (released) {
		if (server->agd_edit && server->agd_ime) {
			desk_ime_typing_key(sym, keycode, mods, true);
		}
		if (sym == server->agd_held_sym) {
			server->agd_held_sym = 0;
		}
		if (sym == server->agd_repeat_sym) {
			agd_repeat_stop(server);
		}
		return;
	}
	server->agd_held_sym = sym;
	if (!server->agd_edit) {
		/* not writing: Escape puts the binder back, the arrows go along
		 * the days, a letter starts a plan */
		if (sym == XKB_KEY_Escape) {
			agd_close(server);
			return;
		}
		int step = sym == XKB_KEY_Left ? -1 : sym == XKB_KEY_Right ? 1
			: sym == XKB_KEY_Up ? -7 : sym == XKB_KEY_Down ? 7 : 0;
		if (step) {
			int y = server->agd_y, m = server->agd_m, d = server->agd_d;
			desk_calendar_step(DESK_CAL_DAY, step, &y, &m, &d);
			agd_open(server, y, m, d, AGD_START_NONE, -1);
			return;
		}
		bool letter = (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f
			&& !(mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO));
		if (!letter) {
			/* the input method's own keys (kana on and off) still go to it */
			if (server->agd_ime) {
				desk_ime_typing_key(sym, keycode, mods, false);
			}
			return;
		}
		agd_begin(server, DESK_AGENDA_NEW_PLAN);
	}
	if (server->agd_ime && desk_ime_typing_key(sym, keycode, mods, false)) {
		if (server->agd_keyq_n < (int)(sizeof server->agd_keyq / sizeof server->agd_keyq[0])) {
			memcpy(server->agd_keyq[server->agd_keyq_n].utf8, utf8, sizeof utf8);
			server->agd_keyq_n++;
		}
		return;
	}
	agd_plain(server, sym, utf8, mods);
}

/* the pointer at x, y on the screen: on the binder, what of it */
static int agd_hit_at(struct desk_server *server, double x, double y, int *arg)
{
	if (!agd_has_keys(server) || server->agd_moving) {
		return DESK_AGENDA_HIT_NONE;
	}
	double r[4];
	agd_rect(server, r);
	struct desk_agenda_look lk;
	agd_look(server, &lk);
	double lx = x - r[0], ly = y - r[1];
	desk_agenda_unturn(r[2], r[3], &lk, &lx, &ly);
	return desk_agenda_hit(r[2], r[3], &lk, lx, ly, arg);
}

static void agd_hover_at(struct desk_server *server, double x, double y)
{
	int arg = 0;
	int z = agd_hit_at(server, x, y, &arg);
	/* on the ruler, a pencil mark where the pointer is */
	int mark = z == DESK_AGENDA_HIT_RULER ? arg : -1;
	if (z != server->agd_hover || arg != server->agd_hover_arg || mark != server->agd_mark) {
		server->agd_hover = z;
		server->agd_hover_arg = arg;
		server->agd_mark = mark;
		agd_changed(server);
	}
}

/* a click on the binder: true if it was on it */
static bool agd_press(struct desk_server *server, double x, double y, uint32_t button)
{
	int arg = 0;
	int z = agd_hit_at(server, x, y, &arg);
	if (z == DESK_AGENDA_HIT_NONE) {
		return false;
	}
	if (button == BTN_RIGHT) {
		if (z == DESK_AGENDA_HIT_NOTE || z == DESK_AGENDA_HIT_CHECK) {
			if (arg > 0) {
				agd_peel_off(server, arg);
			} else {
				agd_finish(server, false);
			}
		}
		return true;
	}
	if (button != BTN_LEFT) {
		return true;
	}
	switch (z) {
	case DESK_AGENDA_HIT_RULER:
		agd_ruler_press(server, arg);
		break;
	case DESK_AGENDA_HIT_ALLDAY:
		agd_untype_time(server);
		server->agd_when = -1;
		server->agd_span = 0;
		desk_sound_play(DESK_SND_PENCIL_RING, agd_pan(server), 0.35, 1.2);
		agd_changed(server);
		break;
	case DESK_AGENDA_HIT_PAD:
		server->agd_colour = arg;
		if (server->agd_edit) {
			/* the note being written takes the colour too */
			server->agd_edit_colour = arg;
		}
		desk_sound_play(DESK_SND_FELT_PICK, agd_pan(server), 0.6, 1.3);
		agd_changed(server);
		break;
	case DESK_AGENDA_HIT_SLOT_PLAN:
		agd_begin(server, DESK_AGENDA_NEW_PLAN);
		break;
	case DESK_AGENDA_HIT_SLOT_TODO:
		agd_begin(server, DESK_AGENDA_NEW_TODO);
		break;
	case DESK_AGENDA_HIT_NOTE:
		if (arg != server->agd_edit) {
			agd_begin(server, arg);
		}
		break;
	case DESK_AGENDA_HIT_CHECK: {
		struct desk_stick s;
		agd_finish(server, true);
		if (desk_agenda_get(arg, &s)) {
			s.done = !s.done;
			desk_agenda_set(&s);
			desk_sound_play(DESK_SND_PENCIL_TICK, agd_pan(server), 0.9, 0.0);
			agd_stuck(server);
		}
		break;
	}
	default:
		/* the binder, not a note on it: put down what was being written */
		agd_finish(server, true);
		break;
	}
	return true;
}

static void shelf_toggle(struct desk_server *server)
{
	/* whatever was pulled out from under the Control Panel goes back */
	card_gone(server);
	/* and the calendar or the memo pad, if it was in your hand */
	cal_drop(server);
	memo_drop(server);
	/* The desk is traced first and put up on its own; the bookcase, the
	 * console and the post box follow it, each up as its own sheet is.
	 * Until the bookcase lands there is nothing behind the desk to turn
	 * to, so the plate does not answer. Coming back down is always
	 * allowed. */
	if (server->shelf_t < 0.5
		&& !desk_scene_view_ready(server->desk_w, server->desk_h, DESK_VIEW_SHELF)) {
		return;
	}
	double now = monotonic_sec();
	/* turned back partway, it goes back from where it has got to */
	double cur = server->shelf_t;
	server->shelf_from = cur;
	server->shelf_to = cur > 0.5 ? 0.0 : 1.0;
	server->shelf_hover = -1;
	server->hover_from = -1;
	server->hover_t = 0.0;
	server->hover_moving = 0;
	if (server->shelf_to > 0.5 && server->binder_putback) {
		/* back up before it was put away: it is on the shelf */
		binder_put_back(server);
	}
	if (server->shelf_to < 0.5 && server->shelf_binder == DESK_SHELF_BOOK) {
		/* whatever was being written in it is done with */
		book_end(server);
	}
	if (server->shelf_to < 0.5 && !server->binder_putback) {
		binder_off_hand(server);
		desk_binder_forget();
		server->binder_t = 0.0;
		server->binder_moving = 0;
		server->binder_sel = -1;
	}
	server->shelf_start = now;
	double span = fabs(server->shelf_to - cur);
	if (span < 0.30) {
		span = 0.30;
	}
	server->shelf_dur = 0.80 * span;
	server->shelf_moving = 1;
	/* You shut the drawer before you sit up. The film is traced with it
	 * shut, so this is also what makes the first frame match the desk. */
	if (server->shelf_to > 0.5 && server->drawer_t > 0.004) {
		drawer_toggle(server);
	}
	server_schedule_frames(server);
	paint_shelf(server);
}

/* A look let go of by a finger: the same spring as a look set off (look_ease), which sets off at
 * the speed the finger was going, not from rest. True when it has come to rest. */
static bool look_free_step(struct desk_server *server, double *t, double to)
{
	const double W = 10.6;
	double Z = g_bounce <= 0.0 ? 1.0 : 0.74 + (1.0 - fmin(g_bounce, 1.0)) * 0.20;
	double now = monotonic_sec();
	double dt = clampf_d(now - server->look_at, 0.0, 0.05);
	server->look_at = now;
	int n = (int)ceil(dt * 480.0);
	double h = n > 0 ? dt / n : 0.0;
	for (int i = 0; i < n; i++) {
		server->look_v += (W * W * (to - *t) - 2.0 * Z * W * server->look_v) * h;
		*t += server->look_v * h;
	}
	return fabs(to - *t) < 0.002 && fabs(server->look_v) < 0.03;
}

static void shelf_tick(struct desk_server *server)
{
	if (!server->shelf_moving) {
		return;
	}
	if (server->look_held == DESK_VIEW_SHELF + 1) {
		paint_shelf(server);
		return;
	}
	if (server->look_free == DESK_VIEW_SHELF + 1) {
		if (!look_free_step(server, &server->shelf_t, server->shelf_to)) {
			paint_shelf(server);
			server_schedule_frames(server);
			return;
		}
		/* (there: as a look comes to its end) */
		server->look_free = 0;
		server->shelf_start = monotonic_sec() - server->shelf_dur;
	}
	double u = clampf_d((monotonic_sec() - server->shelf_start) / server->shelf_dur, 0, 1);
	double e = look_ease(u);
	server->shelf_t = server->shelf_from + (server->shelf_to - server->shelf_from) * e;
	if (u >= 1.0) {
		server->shelf_t = server->shelf_to;
		server->shelf_moving = 0;
	}
	if (server->binder_putback && server->shelf_t < 0.70) {
		/* out of sight now (paint_shelf_body draws it no lower) */
		binder_put_back(server);
	}
	paint_shelf(server);
	if (server->shelf_moving) {
		server_schedule_frames(server);
		return;
	}
	if (server->shelf_t < 0.5 && server->pending_look) {
		int what = server->pending_look;
		server->pending_look = 0;
		if (what == 1) {
			console_toggle(server);
		} else {
			post_toggle(server);
		}
	}
}
