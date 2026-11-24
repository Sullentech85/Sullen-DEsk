/* Part of draw.c: the agenda and the memo pad.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

void desk_agenda_layout(int w, int h, double binder[4], double cal[3])
{
	double pg[4], box[4];
	desk_calendar_rect(w, h, pg, box);
	double pw = pg[2], ph = pg[3];
	double tw = pw * CAL_TAB / cal_page_w();
	/* the calendar from the left edge of its board to its tabs drawn out;
	 * the binder a little taller than its page, open */
	double cwid = pw * 1.034 + tw * 1.30;
	double bh = ph * 1.14, bw = bh * 1.20;
	double gap = w * 0.022;
	double k = fmin(1.0, (w * 0.96 - gap) / (cwid + bw));
	double x0 = floor((w - (cwid + bw) * k - gap) * 0.5);
	double cy = pg[1] + ph * 0.5;
	double npx = x0 + pw * 0.034 * k, npy = cy - ph * 0.5 * k;
	cal[0] = k;
	cal[1] = npx - pg[0] * k;
	cal[2] = npy - pg[1] * k;
	binder[0] = floor(x0 + cwid * k + gap);
	binder[1] = floor(cy - bh * 0.5 * k);
	binder[2] = ceil(bw * k);
	binder[3] = ceil(bh * k);
}

#define AGD_NOTES 12

struct agd_note {
	struct desk_stick s;
	double r[4];
	int lines;
};

struct agd_geom {
	double W, H;
	double lx0, lx1, ly0, ly1;
	double pad[DESK_STICK_COLOURS][4];
	double plate[4];
	double ring_y[6];
	double px0, py0, px1, py1;
	double note_x, note_w, font, line, pad_in, row;
	double plans_y, todos_y;
	struct agd_note plan[AGD_NOTES], todo[AGD_NOTES];
	int nplan, ntodo, more_plan, more_todo;
	double slot_plan[4], slot_todo[4];
	double edit_r[4];
	int edit_lines, edit_kind;
	bool editing;
	/* the ruler across the foot of the binder, and where its hours run
	 * from and to along it */
	double ruler[4], rx0, rx1;
	/* what the note being written shows: the text with the input
	 * method's part in it, where that part is, and the caret */
	char shown[512];
	int pre0, pre1, caret;
};

/* how much of the note's width the writing has */
static double agd_text_w(const struct agd_geom *g, int kind)
{
	double w = g->note_w - g->pad_in * 2.0;
	return kind == DESK_STICK_TODO ? w - g->font * 1.30 : w;
}

static int agd_lines(cairo_t *cr, const struct agd_geom *g, const char *text, int kind, int max)
{
	int st[8], en[8];
	stick_font(cr, g->font, false);
	int n = stick_wrap(cr, text, agd_text_w(g, kind), st, en, max);
	return n < 1 ? 1 : n;
}

static double agd_note_h(const struct agd_geom *g, int lines, bool timed)
{
	return g->pad_in * 2.0 + g->line * lines + (timed ? g->line * 0.80 : 0.0);
}

/* The notes of one kind stuck on the calendar, written down the page in
 * pencil a line to each, and the space after them for a new one -- or the
 * note being written, in its place among them (a new one at the end). */
static void agd_list(cairo_t *cr, struct agd_geom *g, const struct desk_agenda_look *lk,
	int kind, double top, double bottom, struct agd_note *out, int *shown, int *more,
	double slot[4])
{
	struct desk_stick st[AGD_NOTES * 2];
	/* how many there are, and the first of them to lay out */
	int total = desk_agenda_day(lk->year, lk->month, lk->day, kind, st, AGD_NOTES * 2);
	int n = total > AGD_NOTES * 2 ? AGD_NOTES * 2 : total;
	bool here = (kind == DESK_STICK_PLAN && lk->edit == DESK_AGENDA_NEW_PLAN)
		|| (kind == DESK_STICK_TODO && lk->edit == DESK_AGENDA_NEW_TODO);
	for (int i = 0; i < n; i++) {
		here |= st[i].id == lk->edit;
	}
	int elines = agd_lines(cr, g, g->shown, kind, 3);
	double eh = agd_note_h(g, elines, kind == DESK_STICK_PLAN && lk->when >= 0);
	double gap = g->font * 0.35;
	double y = top;
	bool placed = false;
	*shown = 0;
	*more = 0;
	slot[2] = slot[3] = 0.0;
	for (int i = 0; i < n; i++) {
		bool ed = st[i].id == lk->edit;
		double nh = ed ? eh + gap * 2.0 : g->row;
		if (*shown >= AGD_NOTES
			|| y + nh > bottom - (here && !placed && !ed ? eh + gap * 2.0 : 0.0)) {
			*more = total - i;
			break;
		}
		struct agd_note *o = &out[(*shown)++];
		o->s = st[i];
		o->r[0] = g->note_x;
		o->r[1] = ed ? y + gap : y;
		o->r[2] = g->note_w;
		o->r[3] = ed ? eh : nh;
		o->lines = ed ? elines : 1;
		if (ed) {
			placed = true;
		}
		y += nh;
	}
	if (here) {
		/* where it is among them, or after them */
		g->edit_lines = elines;
		g->edit_kind = kind;
		g->edit_r[0] = g->note_x;
		g->edit_r[1] = fmin(y + gap, bottom - eh);
		g->edit_r[2] = g->note_w;
		g->edit_r[3] = eh;
		for (int i = 0; i < *shown; i++) {
			if (out[i].s.id == lk->edit) {
				memcpy(g->edit_r, out[i].r, sizeof out[i].r);
			}
		}
		g->editing = true;
		return;
	}
	double sh = g->pad_in * 2.0 + g->line;
	y += g->font * 0.25;
	if (y + sh <= bottom) {
		slot[0] = g->note_x;
		slot[1] = y;
		slot[2] = g->note_w;
		slot[3] = sh;
	}
}

/* the ruler across the foot of the binder, and where its hours run from
 * and to along it */
static void agd_ruler_place(double W, double H, double r[4], double *rx0, double *rx1)
{
	r[0] = W * 0.030;
	r[1] = H * 0.850;
	r[2] = W * 0.940;
	r[3] = H * 0.110;
	*rx0 = r[0] + W * 0.105;
	*rx1 = r[0] + r[2] - W * 0.028;
}

static void agd_geometry(cairo_t *cr, double W, double H, const struct desk_agenda_look *lk,
	struct agd_geom *g)
{
	memset(g, 0, sizeof *g);
	g->W = W;
	g->H = H;
	g->lx0 = W * 0.035;
	g->lx1 = W * 0.355;
	g->ly0 = H * 0.035;
	g->ly1 = H * 0.965;
	double ps = fmin((g->lx1 - g->lx0) * 0.36, H * 0.16);
	for (int c = 0; c < DESK_STICK_COLOURS; c++) {
		double cx = g->lx0 + (g->lx1 - g->lx0) * (c % 2 ? 0.71 : 0.29);
		double cy = H * (0.30 + 0.205 * (c / 2));
		g->pad[c][0] = cx - ps * 0.5;
		g->pad[c][1] = cy - ps * 0.5;
		g->pad[c][2] = ps;
		g->pad[c][3] = ps;
	}
	g->plate[0] = W * 0.360;
	g->plate[1] = H * 0.10;
	g->plate[2] = W * 0.030;
	g->plate[3] = H * 0.80;
	static const double ry[6] = { 0.17, 0.26, 0.35, 0.65, 0.74, 0.83 };
	for (int i = 0; i < 6; i++) {
		g->ring_y[i] = H * ry[i];
	}
	g->px0 = W * 0.395;
	g->py0 = H * 0.035;
	g->px1 = W * 0.968;
	g->py1 = H * 0.965;
	g->font = H * 0.027;
	g->line = g->font * 1.32;
	g->pad_in = g->font * 0.55;
	g->row = g->line * 1.10;
	agd_ruler_place(W, H, g->ruler, &g->rx0, &g->rx1);
	g->note_x = g->px0 + (g->px1 - g->px0) * 0.11;
	g->note_w = (g->px1 - g->px0) * 0.82;
	g->plans_y = H * 0.245;
	g->todos_y = H * 0.640;
	/* the text being written, with the input method's part in it */
	const char *t = lk->text ? lk->text : "";
	const char *pre = lk->preedit ? lk->preedit : "";
	int caret = lk->caret < 0 ? 0 : lk->caret;
	caret = caret > (int)strlen(t) ? (int)strlen(t) : caret;
	snprintf(g->shown, sizeof g->shown, "%.*s%s%s", caret, t, pre, t + caret);
	g->pre0 = caret;
	g->pre1 = caret + (int)strlen(pre);
	g->caret = pre[0] ? caret + (lk->preedit_caret < 0 ? 0 : lk->preedit_caret) : caret;
	agd_list(cr, g, lk, DESK_STICK_PLAN, H * 0.258, H * 0.600, g->plan, &g->nplan,
		&g->more_plan, g->slot_plan);
	agd_list(cr, g, lk, DESK_STICK_TODO, H * 0.654, H * 0.918, g->todo, &g->ntodo,
		&g->more_todo, g->slot_todo);
}

/* a context to measure text in, for hit testing outside of any drawing */
static cairo_t *agd_measure(void)
{
	static cairo_t *cr;
	if (!cr) {
		cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
		cr = cairo_create(s);
		cairo_surface_destroy(s);
	}
	return cr;
}

/* A pad of sticky notes: the sheets under the top one at its edges; the
 * top one's corner turned up on the pad picked, ready to be peeled. */
static void agd_pad(cairo_t *cr, const double r[4], int colour, bool picked, bool hover)
{
	double x = r[0], y = r[1], s = r[2];
	double cr_, cg, cb;
	if (hover) {
		x -= s * 0.012;
		y -= s * 0.02;
	}
	/* its shadow on the leather */
	for (int i = 0; i < 3; i++) {
		double g = s * (0.02 + 0.025 * i);
		cairo_rectangle(cr, x - g * 0.3, y + g * 0.4, s + g * 1.2, s + g * 1.4);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.10);
		cairo_fill(cr);
	}
	/* the block of sheets under */
	for (int i = 6; i >= 1; i--) {
		stick_ink(colour, 0.80 + 0.02 * (6 - i), &cr_, &cg, &cb);
		cairo_rectangle(cr, x + i * s * 0.004, y + i * s * 0.009, s, s);
		cairo_set_source_rgb(cr, cr_, cg, cb);
		cairo_fill(cr);
	}
	cairo_save(cr);
	cairo_translate(cr, x, y);
	if (picked) {
		/* the top sheet, its foot coming up off the block */
		double c = s * 0.30;
		cairo_move_to(cr, 0, 0);
		cairo_line_to(cr, s, 0);
		cairo_line_to(cr, s, s - c);
		cairo_line_to(cr, s - c, s);
		cairo_line_to(cr, 0, s);
		cairo_close_path(cr);
		cairo_save(cr);
		cairo_clip(cr);
		stick_paper(cr, s, s, colour, 0.0, 1.0);
		cairo_restore(cr);
		/* the turned-up corner: its underside, and its shadow */
		cairo_move_to(cr, s, s - c);
		cairo_line_to(cr, s - c * 0.92, s - c * 0.92);
		cairo_line_to(cr, s - c, s);
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
		cairo_fill(cr);
		cairo_move_to(cr, s, s - c);
		cairo_line_to(cr, s - c * 0.80, s - c * 1.10);
		cairo_line_to(cr, s - c, s);
		cairo_close_path(cr);
		stick_ink(colour, 1.06, &cr_, &cg, &cb);
		cairo_set_source_rgb(cr, fmin(cr_, 1.0), fmin(cg, 1.0), fmin(cb, 1.0));
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.15);
		cairo_set_line_width(cr, 0.8);
		cairo_stroke(cr);
	} else {
		stick_paper(cr, s, s, colour, 0.0, 1.0);
	}
	cairo_restore(cr);
}

/* A note stuck on the calendar, written down on the page in pencil: a
 * scrap of its colour, the box of a thing to do, its time, what it says;
 * struck through, when it is done. gone: being rubbed out (0 to 1). */
static void agd_entry(cairo_t *cr, const struct agd_geom *g, const struct agd_note *n,
	bool hover, double gone)
{
	const struct desk_stick *s = &n->s;
	double alpha = 1.0 - clampf(gone, 0.0, 1.0);
	if (alpha <= 0.0) {
		return;
	}
	double x = n->r[0], y = n->r[1], w = n->r[2], h = n->r[3];
	cairo_save(cr);
	if (hover) {
		double r, gg, b;
		stick_ink(s->colour, 1.0, &r, &gg, &b);
		desk_rounded(cr, x - g->font * 0.25, y + h * 0.04, w + g->font * 0.5, h * 0.92,
			g->font * 0.2);
		cairo_set_source_rgba(cr, r, gg, b, 0.32 * alpha);
		cairo_fill(cr);
	}
	double cs = g->font * 0.80, base = y + h * 0.5 + g->font * 0.34;
	cairo_save(cr);
	stick_frame(cr, x, y + (h - cs) * 0.5, cs, cs, stick_rot(s->id) * 3.0);
	stick_paper(cr, cs, cs, s->colour, 0.0, alpha);
	cairo_restore(cr);
	double tx = x + cs + g->font * 0.50;
	if (s->kind == DESK_STICK_TODO) {
		stick_box(cr, s, tx, base + g->font * 0.06, g->font * 0.78, alpha);
		tx += g->font * 1.15;
	}
	/* in pencil */
	cairo_set_source_rgba(cr, 0.26, 0.26, 0.29, alpha * (s->done ? 0.50 : 0.92));
	char tm[24];
	stick_time(s, tm, sizeof tm, true);
	if (tm[0]) {
		stick_font(cr, g->font * 0.80, true);
		cairo_move_to(cr, tx, base);
		cairo_show_text(cr, tm);
		tx += stick_adv(cr, tm) + g->font * 0.45;
	}
	stick_font(cr, g->font * 0.95, false);
	double tw = x + w - tx;
	if (tw > g->font) {
		stick_lines(cr, s->text, tx, base, tw, g->line, 1);
	}
	if (s->done) {
		double ex = fmin(x + w, tx + stick_adv(cr, s->text));
		cairo_set_line_width(cr, fmax(g->font * 0.06, 0.6));
		cairo_move_to(cr, tx, base - g->font * 0.30);
		cairo_line_to(cr, ex, base - g->font * 0.30);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

static double agd_ruler_x(const struct agd_geom *g, int minute)
{
	return g->rx0 + (g->rx1 - g->rx0) * minute / (24.0 * 60.0);
}

/* where the ruler lies across the foot of the binder, as far as it has
 * come in (it rises the last little way as it fades in): its top, and how
 * far in it has come, eased */
static double agd_ruler_y(const struct agd_geom *g, const struct desk_agenda_look *lk,
	double *e)
{
	double t = clampf(lk->ruler_t, 0.0, 1.0);
	*e = 1.0 - pow(1.0 - t, 3.0);
	return g->ruler[1] + (1.0 - *e) * g->H * 0.04;
}

/* Where the pointer is on the ruler, a pencil mark and a tag over it saying
 * when that is -- drawn, or with cr NULL only measured: out, the part of
 * the binder's flat drawing they cover (with what their edges and shadow
 * need). False if there is none. It moves with every quarter hour the
 * pointer crosses, so the compositor lays it on by itself (desk.h). */
static bool agd_ruler_mark(cairo_t *cr, const struct agd_geom *g,
	const struct desk_agenda_look *lk, double out[4])
{
	if (lk->ruler_mark < 0 || lk->ruler_mark > 1440 || lk->ruler_t <= 0.0) {
		return false;
	}
	double e;
	double y = agd_ruler_y(g, lk, &e);
	double w = g->ruler[2], h = g->ruler[3], tyh = h * 0.40;
	double mx = agd_ruler_x(g, lk->ruler_mark);
	char tm[24];
	desk_agenda_time_text(lk->ruler_mark % 1440, 0, tm, sizeof tm);
	if (lk->ruler_mark == 1440) {
		snprintf(tm, sizeof tm, "24:00");
	}
	cairo_t *mc = cr;
	cairo_surface_t *scratch = NULL;
	if (!mc) {
		scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
		mc = cairo_create(scratch);
	}
	stick_font(mc, g->font * 0.78, true);
	cairo_text_extents_t te;
	cairo_text_extents(mc, tm, &te);
	double tw = te.x_advance + g->font * 0.7, th = g->font * 1.25;
	double tx = clampf(mx - tw * 0.5, g->ruler[0], g->ruler[0] + w - tw);
	double tyy = y - th - h * 0.08;
	if (!cr) {
		cairo_destroy(mc);
		cairo_surface_destroy(scratch);
		double x0 = fmin(mx - 0.8, tx), x1 = fmax(mx + 0.8, tx + tw + 1.0);
		double y0 = tyy, y1 = fmax(y - h * 0.05 + tyh + h * 0.12, tyy + th + 1.5);
		if (out) {
			out[0] = floor(x0) - 3.0;
			out[1] = floor(y0) - 3.0;
			out[2] = ceil(x1 - x0) + 6.0;
			out[3] = ceil(y1 - y0) + 6.0;
		}
		return true;
	}
	/* the pencil mark */
	cairo_set_source_rgba(cr, 0.18, 0.18, 0.22, 0.9);
	cairo_rectangle(cr, mx - 0.8, y - h * 0.05, 1.6, tyh + h * 0.12);
	cairo_fill(cr);
	/* and the tag */
	cairo_rectangle(cr, tx + 1.0, tyy + 1.5, tw, th);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
	cairo_fill(cr);
	cairo_rectangle(cr, tx, tyy, tw, th);
	cairo_set_source_rgb(cr, 0.99, 0.98, 0.95);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.18, 0.18, 0.22);
	cairo_move_to(cr, tx + g->font * 0.35, tyy + th * 0.72);
	cairo_show_text(cr, tm);
	return true;
}

/* A clear plastic ruler marked in hours, laid across the foot of the
 * binder while a plan is being written: the time it is at, or from and
 * until, gone over with a highlighter the colour of the note; at its end,
 * ALL DAY, for no time; where the pointer is, a pencil mark and a tag
 * saying when that is. */
static void agd_ruler(cairo_t *cr, const struct agd_geom *g, const struct desk_agenda_look *lk)
{
	if (lk->ruler_t <= 0.0) {
		return;
	}
	double e;
	double H = g->H, x = g->ruler[0], w = g->ruler[2], h = g->ruler[3];
	double y = agd_ruler_y(g, lk, &e);
	cairo_save(cr);
	cairo_push_group(cr);
	/* its shadow on the binder */
	for (int i = 0; i < 4; i++) {
		double sp = h * (0.03 + 0.04 * i);
		desk_rounded(cr, x - sp * 0.2, y + sp * 0.6, w + sp * 0.4, h + sp * 0.2, h * 0.08 + sp);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.05);
		cairo_fill(cr);
	}
	/* the plastic: the binder seen through it, lighter and a little green */
	cairo_pattern_t *pl = cairo_pattern_create_linear(0, y, 0, y + h);
	cairo_pattern_add_color_stop_rgba(pl, 0.0, 0.96, 0.99, 0.98, 0.80);
	cairo_pattern_add_color_stop_rgba(pl, 1.0, 0.86, 0.93, 0.92, 0.72);
	desk_rounded(cr, x, y, w, h, h * 0.08);
	cairo_set_source(cr, pl);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(pl);
	cairo_set_source_rgba(cr, 0.30, 0.42, 0.42, 0.55);
	cairo_set_line_width(cr, fmax(H * 0.0016, 0.7));
	cairo_stroke(cr);
	cairo_move_to(cr, x + h * 0.10, y + 1.2);
	cairo_line_to(cr, x + w - h * 0.10, y + 1.2);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	double ty0 = y + h * 0.05, tyh = h * 0.40;
	if (lk->when >= 0) {
		/* the highlighter, under the marks: past midnight it goes on
		 * from the other end */
		double r, gg, b;
		stick_ink(lk->edit_colour, 0.92, &r, &gg, &b);
		double a0 = agd_ruler_x(g, lk->when);
		if (lk->span > 0) {
			int end = lk->when + lk->span;
			cairo_rectangle(cr, a0, ty0, agd_ruler_x(g, end > 1440 ? 1440 : end) - a0, tyh);
			if (end > 1440) {
				cairo_rectangle(cr, g->rx0, ty0, agd_ruler_x(g, end - 1440) - g->rx0, tyh);
			}
		} else {
			cairo_rectangle(cr, a0 - g->font * 0.14, ty0, g->font * 0.28, tyh);
		}
		cairo_set_source_rgba(cr, r, gg, b, 0.85);
		cairo_fill(cr);
	}
	/* the marks: hours, half hours, quarters */
	cairo_set_source_rgba(cr, 0.14, 0.18, 0.20, 0.85);
	for (int q = 0; q <= 96; q++) {
		double mx = agd_ruler_x(g, q * 15);
		double len = q % 4 == 0 ? tyh : (q % 2 == 0 ? tyh * 0.62 : tyh * 0.36);
		cairo_rectangle(cr, mx - 0.5, y + h * 0.02, 1.0, len + h * 0.03);
	}
	cairo_fill(cr);
	double step = (g->rx1 - g->rx0) / 24.0;
	int every = step > h * 0.30 ? 1 : 2;
	for (int hr = 0; hr <= 24; hr += every) {
		char num[12];
		snprintf(num, sizeof num, "%d", hr);
		show_text_as(cr, DESK_FONT_PRINT, num, agd_ruler_x(g, hr * 60), y + h * 0.69,
			h * 0.20, true,
			0.14, 0.18, 0.20, 0.9, 0);
	}
	/* no time at all */
	double ax = x + (g->rx0 - x) * 0.47, ay = y + h * 0.50;
	double asz = h * 0.15, aw = cal_spaced_w(cr, TR("ALL DAY"), asz, asz * 0.12, true);
	bool all = lk->when < 0, hot = lk->hover == DESK_AGENDA_HIT_ALLDAY;
	cal_spaced(cr, TR("ALL DAY"), ax - aw * 0.5, ay + asz * 0.35, asz, asz * 0.12, true,
		all ? 0.74 : 0.30, all ? 0.15 : 0.36, all ? 0.12 : 0.38);
	if (all || hot) {
		cairo_save(cr);
		if (!all) {
			cairo_push_group(cr);
		}
		cal_ring(cr, ax, ay, aw * 0.62, h * 0.24, fmax(h * 0.030, 0.9));
		if (!all) {
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr, 0.35);
		}
		cairo_restore(cr);
	}
	/* what it is for, along its foot */
	stick_font(cr, h * 0.14, false);
	cairo_set_source_rgba(cr, 0.24, 0.30, 0.31, 0.85);
	cairo_move_to(cr, g->rx0, y + h * 0.92);
	cairo_show_text(cr, TR("click or drag along it for the time"));
	const char *keys = TR("Enter: stick it on the calendar    Esc: stop");
	cairo_text_extents_t ke;
	cairo_text_extents(cr, keys, &ke);
	cairo_move_to(cr, g->rx1 - ke.x_advance, y + h * 0.92);
	cairo_show_text(cr, keys);
	agd_ruler_mark(cr, g, lk, NULL);
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, e);
	cairo_restore(cr);
}

/* The note being written: what is on it so far, the input method's part
 * of it underlined, and the caret. */
static void agd_edit_draw(cairo_t *cr, const struct agd_geom *g, const struct desk_agenda_look *lk,
	int kind, const double r[4], double lift, double turn)
{
	double w = r[2], h = r[3];
	cairo_save(cr);
	stick_frame(cr, r[0], r[1], w, h, turn);
	stick_paper(cr, w, h, lk->edit_colour, 0.35 + lift, 1.0);
	double x = g->pad_in, y = g->pad_in + g->font * 0.95;
	if (kind == DESK_STICK_TODO) {
		double bs = g->font * 0.85;
		cairo_set_source_rgba(cr, 0.20, 0.18, 0.15, 0.8);
		cairo_set_line_width(cr, fmax(g->font * 0.08, 0.7));
		cairo_rectangle(cr, x, y - bs * 0.86, bs, bs);
		cairo_stroke(cr);
		x += g->font * 1.30;
	}
	if (kind == DESK_STICK_PLAN && lk->when >= 0) {
		/* the time it is at, from the ruler or written first */
		char tm[24];
		desk_agenda_time_text(lk->when, lk->span, tm, sizeof tm);
		stick_font(cr, g->font * 0.80, true);
		cairo_set_source_rgb(cr, 0.17, 0.15, 0.12);
		cairo_move_to(cr, x, y - g->font * 0.10);
		cairo_show_text(cr, tm);
		y += g->line * 0.80;
	}
	stick_font(cr, g->font, false);
	int st[8], en[8];
	int n = stick_wrap(cr, g->shown, agd_text_w(g, kind), st, en, 3);
	int len = (int)strlen(g->shown);
	if (n == 0 || (len == 0 && n == 1)) {
		n = 1;
		st[0] = en[0] = 0;
	}
	for (int i = 0; i < n; i++) {
		double ly = y + g->line * i;
		char buf[512];
		int l = en[i] - st[i];
		memcpy(buf, g->shown + st[i], (size_t)l);
		buf[l] = '\0';
		cairo_set_source_rgb(cr, 0.17, 0.15, 0.12);
		cairo_move_to(cr, x, ly);
		cairo_show_text(cr, buf);
		/* the input method's part, underlined */
		int a = g->pre0 > st[i] ? g->pre0 : st[i], b = g->pre1 < en[i] ? g->pre1 : en[i];
		if (b > a) {
			char pre[512];
			snprintf(pre, sizeof pre, "%.*s", a - st[i], g->shown + st[i]);
			double xa = x + stick_adv(cr, pre);
			snprintf(pre, sizeof pre, "%.*s", b - st[i], g->shown + st[i]);
			double xb = x + stick_adv(cr, pre);
			cairo_set_line_width(cr, fmax(g->font * 0.06, 0.8));
			cairo_move_to(cr, xa, ly + g->font * 0.18);
			cairo_line_to(cr, xb, ly + g->font * 0.18);
			cairo_stroke(cr);
		}
		/* the caret, on the line it is at the end or in the middle of */
		bool here = g->caret >= st[i] && (g->caret < en[i] || (g->caret == en[i]
			&& (i == n - 1 || st[i + 1] > en[i])));
		if (here) {
			char pre[512];
			snprintf(pre, sizeof pre, "%.*s", g->caret - st[i], g->shown + st[i]);
			double xc = x + stick_adv(cr, pre);
			cairo_set_source_rgb(cr, 0.10, 0.10, 0.12);
			cairo_rectangle(cr, xc, ly - g->font * 0.88, fmax(g->font * 0.07, 1.0), g->font * 1.08);
			cairo_fill(cr);
		}
	}
	cairo_restore(cr);
}

/* The input method's candidates, on a slip under the note (over it, if
 * there is no room under). */
static void agd_cands(cairo_t *cr, const struct agd_geom *g, const struct desk_agenda_look *lk,
	const double er[4])
{
	if (!lk->cands || lk->ncand <= 0) {
		return;
	}
	int ps = lk->cand_page > 0 ? lk->cand_page : 9;
	int cur = lk->cand_cursor < 0 ? 0 : (lk->cand_cursor >= lk->ncand ? lk->ncand - 1 : lk->cand_cursor);
	int first = cur / ps * ps, last = first + ps < lk->ncand ? first + ps : lk->ncand;
	double row = g->font * 1.40, sw = g->note_w * 0.66;
	double sh = row * (last - first) + g->font * 0.5;
	double sx = er[0] + g->note_w * 0.08, sy = er[1] + er[3] + g->font * 0.25;
	if (sy + sh > g->H * 0.99) {
		sy = er[1] - sh - g->font * 0.25;
	}
	cairo_save(cr);
	for (int i = 0; i < 3; i++) {
		double s2 = g->font * (0.10 + 0.12 * i);
		cairo_rectangle(cr, sx - s2 * 0.2, sy + s2 * 0.5, sw + s2 * 0.8, sh + s2);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.08);
		cairo_fill(cr);
	}
	cairo_rectangle(cr, sx, sy, sw, sh);
	cairo_set_source_rgb(cr, 0.985, 0.980, 0.965);
	cairo_fill(cr);
	for (int i = first; i < last; i++) {
		double ry = sy + g->font * 0.25 + row * (i - first);
		if (i == cur) {
			cairo_rectangle(cr, sx + g->font * 0.2, ry, sw - g->font * 0.4, row);
			cairo_set_source_rgba(cr, 0.98, 0.86, 0.40, 0.70);
			cairo_fill(cr);
		}
		char num[12];
		snprintf(num, sizeof num, "%d", i - first + 1);
		stick_font(cr, g->font * 0.70, true);
		cairo_set_source_rgb(cr, 0.55, 0.52, 0.47);
		cairo_move_to(cr, sx + g->font * 0.5, ry + row * 0.70);
		cairo_show_text(cr, num);
		stick_font(cr, g->font, false);
		cairo_set_source_rgb(cr, 0.15, 0.13, 0.11);
		cairo_save(cr);
		cairo_rectangle(cr, sx, ry, sw - g->font * 0.3, row);
		cairo_clip(cr);
		cairo_move_to(cr, sx + g->font * 1.4, ry + row * 0.72);
		cairo_show_text(cr, lk->cands[i]);
		cairo_restore(cr);
	}
	cairo_restore(cr);
}

/* a space on the page for a new note: a pencilled outline */
static void agd_slot(cairo_t *cr, const struct agd_geom *g, const double r[4], int colour,
	bool hover)
{
	if (r[2] <= 0.0) {
		return;
	}
	cairo_save(cr);
	if (hover) {
		double cr_, cg, cb;
		stick_ink(colour, 1.0, &cr_, &cg, &cb);
		cairo_rectangle(cr, r[0], r[1], r[2], r[3]);
		cairo_set_source_rgba(cr, cr_, cg, cb, 0.35);
		cairo_fill(cr);
	}
	double dash[2] = { g->font * 0.30, g->font * 0.22 };
	cairo_set_dash(cr, dash, 2, 0);
	cairo_set_line_width(cr, fmax(g->font * 0.05, 0.7));
	cairo_set_source_rgba(cr, 0.40, 0.37, 0.33, hover ? 0.80 : 0.45);
	cairo_rectangle(cr, r[0] + 0.5, r[1] + 0.5, r[2] - 1.0, r[3] - 1.0);
	cairo_stroke(cr);
	cairo_restore(cr);
	stick_font(cr, g->font * 0.80, false);
	cairo_set_source_rgba(cr, 0.40, 0.37, 0.33, hover ? 0.85 : 0.55);
	cairo_move_to(cr, r[0] + g->pad_in, r[1] + r[3] * 0.5 + g->font * 0.30);
	cairo_show_text(cr, TR("+  stick a note here"));
}

static void agd_draw_flat(cairo_t *cr, double W, double H, const struct desk_agenda_look *lk,
	bool rings)
{
	struct agd_geom g;
	agd_geometry(cr, W, H, lk, &g);
	time_t now = time(NULL);
	struct tm tmv;
	localtime_r(&now, &tmv);

	/* its shadow, the cover, and the inside of the front cover */
	cairo_save(cr);
	for (int i = 0; i < 8; i++) {
		double s = W * (0.004 + 0.004 * i);
		desk_rounded(cr, -s * 0.5 + W * 0.008, -s * 0.3 + H * 0.016, W + s, H + s, W * 0.02 + s);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.05);
		cairo_fill(cr);
	}
	cairo_restore(cr);
	leather_fill(cr, 0, 0, W, H, W * 0.02);
	cairo_save(cr);
	desk_rounded(cr, g.lx0, g.ly0, g.lx1 - g.lx0, g.ly1 - g.ly0, W * 0.012);
	cairo_set_source_rgba(cr, 1.0, 0.92, 0.82, 0.06);
	cairo_fill(cr);
	cairo_restore(cr);
	stitch_rect(cr, g.lx0, g.ly0, g.lx1 - g.lx0, g.ly1 - g.ly0, W * 0.010);
	double lw = cal_spaced_w(cr, TR("STICKY NOTES"), H * 0.024, H * 0.006, true);
	cal_spaced(cr, TR("STICKY NOTES"), (g.lx0 + g.lx1 - lw) * 0.5, H * 0.135, H * 0.024, H * 0.006,
		true, 0.80, 0.66, 0.42);
	for (int c = 0; c < DESK_STICK_COLOURS; c++) {
		agd_pad(cr, g.pad[c], c, c == lk->colour,
			lk->hover == DESK_AGENDA_HIT_PAD && lk->hover_arg == c);
	}
	/* (under the ruler, when it is out) */
	if (lk->ruler_t < 0.3) {
		const char *pick = TR("pick a colour");
		double pkw = cal_spaced_w(cr, pick, H * 0.022, H * 0.002, false);
		cal_spaced(cr, pick, (g.lx0 + g.lx1 - pkw) * 0.5, H * 0.905, H * 0.022, H * 0.002, false,
			0.80, 0.66, 0.42);
	}

	/* the page, its shadow on the cover first */
	cairo_save(cr);
	cairo_rectangle(cr, g.px0 + W * 0.004, g.py0 + H * 0.008, g.px1 - g.px0, g.py1 - g.py0);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
	cairo_fill(cr);
	cairo_rectangle(cr, g.px0, g.py0, g.px1 - g.px0, g.py1 - g.py0);
	cairo_clip(cr);
	cairo_translate(cr, g.px0, g.py0);
	fill_tile(cr, paper_tile(), 1.0, 1.0);
	cairo_restore(cr);
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.40, 0.55, 0.72, 0.16);
	cairo_set_line_width(cr, fmax(H * 0.0012, 0.5));
	for (double ly = H * 0.215; ly < g.py1 - H * 0.02; ly += g.line * 1.05) {
		cairo_move_to(cr, g.px0 + (g.px1 - g.px0) * 0.08, ly);
		cairo_line_to(cr, g.px1 - (g.px1 - g.px0) * 0.03, ly);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
	/* the holes it hangs by, and the rings through them from the plate */
	double hx = g.px0 + W * 0.024;
	for (int i = 0; i < 6; i++) {
		cairo_arc(cr, hx, g.ring_y[i], H * 0.011, 0, 2.0 * M_PI);
		cairo_set_source_rgb(cr, 0.14, 0.08, 0.05);
		cairo_fill(cr);
	}
	cairo_save(cr);
	cairo_pattern_t *pl = cairo_pattern_create_linear(g.plate[0], 0, g.plate[0] + g.plate[2], 0);
	cairo_pattern_add_color_stop_rgb(pl, 0.0, 0.42, 0.42, 0.44);
	cairo_pattern_add_color_stop_rgb(pl, 0.45, 0.86, 0.86, 0.88);
	cairo_pattern_add_color_stop_rgb(pl, 1.0, 0.50, 0.50, 0.52);
	desk_rounded(cr, g.plate[0], g.plate[1], g.plate[2], g.plate[3], g.plate[2] * 0.4);
	cairo_set_source(cr, pl);
	cairo_fill(cr);
	cairo_pattern_destroy(pl);
	for (int i = 0; i < 6 && rings; i++) {
		double y = g.ring_y[i], th = H * 0.016;
		double x0 = g.plate[0] + g.plate[2] * 0.3, x1 = hx + H * 0.004;
		cairo_pattern_t *rg = cairo_pattern_create_linear(0, y - th * 0.5, 0, y + th * 0.5);
		cairo_pattern_add_color_stop_rgb(rg, 0.0, 0.95, 0.95, 0.97);
		cairo_pattern_add_color_stop_rgb(rg, 0.5, 0.70, 0.70, 0.73);
		cairo_pattern_add_color_stop_rgb(rg, 1.0, 0.36, 0.36, 0.38);
		desk_rounded(cr, x0, y - th * 0.5, x1 - x0, th, th * 0.5);
		cairo_set_source(cr, rg);
		cairo_fill_preserve(cr);
		cairo_pattern_destroy(rg);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
		cairo_set_line_width(cr, 0.8);
		cairo_stroke(cr);
	}
	cairo_restore(cr);

	/* the day */
	long z = cal_daynum(lk->year, lk->month, lk->day);
	char num[12], my[32];
	snprintf(num, sizeof num, "%d", lk->day);
	cal_month_year(my, sizeof my, lk->month, lk->year);
	cairo_save(cr);
	stick_font(cr, H * 0.085, true);
	cairo_text_extents_t ne;
	cairo_text_extents(cr, num, &ne);
	cairo_set_source_rgb(cr, 0.16, 0.15, 0.14);
	cairo_move_to(cr, g.note_x - ne.x_bearing, H * 0.150);
	cairo_show_text(cr, num);
	cairo_restore(cr);
	double tx = g.note_x + ne.width + W * 0.020;
	int wd = cal_wday(z);
	double r_, g_, b_;
	cal_ink(wd, &r_, &g_, &b_);
	char dname[32];
	cal_spaced(cr, cal_day_word(wd, 2, dname, sizeof dname), tx, H * 0.108, H * 0.030,
		H * 0.007, true,
		wd == 6 ? r_ : 0.74, wd == 6 ? g_ : 0.15, wd == 6 ? b_ : 0.12);
	cal_spaced(cr, my, tx, H * 0.146, H * 0.024, H * 0.005, false, 0.44, 0.41, 0.37);
	if (cal_is_today(&tmv, lk->year, lk->month, lk->day)) {
		/* stamped */
		cairo_save(cr);
		cairo_translate(cr, g.px1 - W * 0.085, H * 0.110);
		cairo_rotate(cr, -0.14);
		double sw = cal_spaced_w(cr, TR("TODAY"), H * 0.026, H * 0.005, true);
		cairo_set_source_rgba(cr, 0.78, 0.14, 0.12, 0.75);
		cairo_set_line_width(cr, fmax(H * 0.003, 1.0));
		cairo_rectangle(cr, -sw * 0.5 - H * 0.010, -H * 0.028, sw + H * 0.020, H * 0.038);
		cairo_stroke(cr);
		cal_spaced(cr, TR("TODAY"), -sw * 0.5, 0.0, H * 0.026, H * 0.005, true, 0.78, 0.14, 0.12);
		cairo_restore(cr);
	}
	cairo_set_source_rgba(cr, 0.34, 0.31, 0.27, 0.55);
	cairo_set_line_width(cr, fmax(H * 0.002, 0.7));
	cairo_move_to(cr, g.note_x, H * 0.182);
	cairo_line_to(cr, g.px1 - (g.px1 - g.px0) * 0.04, H * 0.182);
	cairo_stroke(cr);

	/* plans, and things to do */
	cal_spaced(cr, TR("PLANS"), g.note_x, g.plans_y, g.font * 0.78, g.font * 0.20, true,
		0.48, 0.45, 0.40);
	cal_spaced(cr, TR("TO DO"), g.note_x, g.todos_y, g.font * 0.78, g.font * 0.20, true,
		0.48, 0.45, 0.40);
	for (int k = 0; k < 2; k++) {
		const struct agd_note *list = k ? g.todo : g.plan;
		int n = k ? g.ntodo : g.nplan, more = k ? g.more_todo : g.more_plan;
		for (int i = 0; i < n; i++) {
			const struct agd_note *nt = &list[i];
			if (nt->s.id == lk->edit) {
				continue;
			}
			agd_entry(cr, &g, nt,
				(lk->hover == DESK_AGENDA_HIT_NOTE || lk->hover == DESK_AGENDA_HIT_CHECK)
				&& lk->hover_arg == nt->s.id,
				nt->s.id == lk->gone_id ? lk->gone_t : 0.0);
		}
		if (more > 0) {
			char m[24];
			snprintf(m, sizeof m, TR("+%d more"), more);
			show_text_as(cr, DESK_FONT_PRINT, m, g.note_x + g.note_w,
				(k ? g.todos_y : g.plans_y), g.font * 0.75,
				true, 0.45, 0.42, 0.37, 1.0, 1);
		}
		agd_slot(cr, &g, k ? g.slot_todo : g.slot_plan, lk->colour,
			lk->hover == (k ? DESK_AGENDA_HIT_SLOT_TODO : DESK_AGENDA_HIT_SLOT_PLAN));
	}

	/* the ruler for its time, while a plan is written */
	agd_ruler(cr, &g, lk);

	/* the note being written, or on its way from the pad -- unless it is
	 * still in the air on its way from the calendar */
	if (g.editing && !lk->edit_away) {
		int kind = g.edit_kind;
		double t = clampf(lk->peel_t, 0.0, 1.0);
		if (lk->edit < 0 && t < 1.0) {
			double e = 1.0 - pow(1.0 - t, 3.0);
			const double *p = g.pad[lk->edit_colour < 0 ? 0 : lk->edit_colour % DESK_STICK_COLOURS];
			double r[4];
			for (int i = 0; i < 4; i++) {
				r[i] = p[i] + (g.edit_r[i] - p[i]) * e;
			}
			struct agd_geom g2 = g;
			g2.shown[0] = '\0';
			g2.pre0 = g2.pre1 = g2.caret = 0;
			agd_edit_draw(cr, &g2, lk, kind, r, 1.0 - e, -0.20 * (1.0 - e));
		} else {
			agd_edit_draw(cr, &g, lk, kind, g.edit_r, 0.0, 0.0);
			agd_cands(cr, &g, lk, g.edit_r);
		}
	}

	/* what to do next, at the foot of the page (under the ruler, when it
	 * is out, which says it there) */
	double ht = 1.0 - clampf(lk->ruler_t * 3.0, 0.0, 1.0);
	if (ht > 0.0) {
		const char *hint = g.editing
			? TR("Enter: stick it on the calendar   Esc: stop")
			: TR("type to write a plan   click a line: change   right-click: peel off");
		stick_font(cr, H * 0.019, false);
		cairo_set_source_rgba(cr, 0.45, 0.42, 0.37, 0.85 * ht);
		cairo_move_to(cr, g.note_x, H * 0.952);
		cairo_show_text(cr, hint);
	}
}

/* ---- the binder held up ----
 *
 * As the calendar is: its open face drawn flat and laid into the view
 * through the turn it is held at; round it, the edges of its boards and of
 * the stack of pages on the back one, where they come into view; and the
 * rings standing up out of it, arches from the plate over to the holes in
 * the pages, which seen square on are the bars they look like flat. */

static bool agd_held(const struct desk_agenda_look *lk)
{
	return lk->eye_f > 0.0 && (fabs(lk->yaw) > 1e-4 || fabs(lk->pitch) > 1e-4);
}

static void agd_turn3(double W, double H, const struct desk_agenda_look *lk, struct turn3 *t)
{
	t->px = W * 0.5;
	t->py = H * 0.5;
	t->yaw = lk->yaw;
	t->pitch = lk->pitch;
	t->ex = lk->eye_x;
	t->ey = lk->eye_y;
	t->f = lk->eye_f > 0.0 ? lk->eye_f : H * 2.0;
}

/* the drop shadow drawn round it reaches this far out */
#define AGD_SHADOW_M(W) ((W) * 0.040)

/* what shows of its edges, before its face goes over them */
static void agd_sides(cairo_t *cr, const struct turn3 *t, const struct agd_geom *g)
{
	held_board_edges(cr, t, 0.0, 0.0, g->W, g->H, g->W * 0.011);
	/* (the stack of pages on the back board stands up off it towards
	 * you, and from where the eye is beside the binder none of its own
	 * edges is turned to it: what shows is its shadow on the board,
	 * which is in the flat drawing) */
}

/* the rings: arches up out of the plate and over into the holes */
static void agd_rings3(cairo_t *cr, const struct turn3 *t, const struct agd_geom *g)
{
	double H = g->H, W = g->W;
	double hx = g->px0 + W * 0.024;
	double x0 = g->plate[0] + g->plate[2] * 0.3, x1 = hx + H * 0.004;
	double xm = (x0 + x1) * 0.5, R = (x1 - x0) * 0.5, th = H * 0.016;
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	for (int i = 0; i < 6; i++) {
		double y = g->ring_y[i];
		for (int pass = 0; pass < 3; pass++) {
			for (int k = 0; k <= 24; k++) {
				double a = M_PI * k / 24.0;
				/* the highlight runs along the top of the wire */
				double dy = pass == 2 ? -th * 0.18 : 0.0;
				double sx, sy;
				turn3_project(t, xm - R * cos(a), y + dy, -R * sin(a) * 0.85, &sx, &sy, NULL);
				if (k == 0) {
					cairo_move_to(cr, sx, sy);
				} else {
					cairo_line_to(cr, sx, sy);
				}
			}
			if (pass == 0) {
				cairo_set_source_rgba(cr, 0.10, 0.10, 0.12, 0.85);
				cairo_set_line_width(cr, th * 1.15);
			} else if (pass == 1) {
				cairo_set_source_rgb(cr, 0.66, 0.66, 0.70);
				cairo_set_line_width(cr, th * 0.90);
			} else {
				cairo_set_source_rgba(cr, 0.97, 0.97, 0.99, 0.85);
				cairo_set_line_width(cr, th * 0.30);
			}
			cairo_stroke(cr);
		}
	}
	cairo_restore(cr);
}

void desk_draw_agenda(cairo_t *cr, double W, double H, const struct desk_agenda_look *lk)
{
	if (!agd_held(lk)) {
		agd_draw_flat(cr, W, H, lk, true);
		return;
	}
	double m = AGD_SHADOW_M(W);
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double k = fmax(hypot(kx, ky), 0.25);
	int iw = (int)ceil((W + m * 2.0) * k), ih = (int)ceil((H + m * 2.0) * k);
	if (iw < 2 || ih < 2) {
		return;
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
	cairo_t *c = cairo_create(img);
	cairo_scale(c, k, k);
	cairo_translate(c, m, m);
	agd_draw_flat(c, W, H, lk, false);
	cairo_destroy(c);

	struct agd_geom g;
	agd_geometry(agd_measure(), W, H, lk, &g);
	struct turn3 t;
	agd_turn3(W, H, lk, &t);
	agd_sides(cr, &t, &g);
	struct hmat map;
	if (turn3_hmat(&t, -m, -m, W + m * 2.0, H + m * 2.0, &map)) {
		warp_paint(cr, img, -m, -m, k, &map, 1.0);
	}
	cairo_surface_destroy(img);

	/* in the lamps' light, as square to it as it is turned; the foot a
	 * little darker than the head */
	held_face_light(cr, &t, 0.0, 0.0, W, H, 0.09);
	agd_rings3(cr, &t, &g);
}

void desk_draw_agenda_mark(cairo_t *cr, double W, double H, const struct desk_agenda_look *lk)
{
	struct agd_geom g;
	agd_geometry(agd_measure(), W, H, lk, &g);
	double r[4], e;
	if (!agd_ruler_mark(NULL, &g, lk, r)) {
		return;
	}
	agd_ruler_y(&g, lk, &e);
	if (!agd_held(lk)) {
		cairo_save(cr);
		cairo_push_group(cr);
		agd_ruler_mark(cr, &g, lk, NULL);
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, e);
		cairo_restore(cr);
		return;
	}
	/* drawn flat, and laid in through the turn the binder is held at --
	 * the same map as its face (desk_draw_agenda) */
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double k = fmax(hypot(kx, ky), 0.25);
	int iw = (int)ceil(r[2] * k), ih = (int)ceil(r[3] * k);
	if (iw < 2 || ih < 2) {
		return;
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
	cairo_t *c = cairo_create(img);
	cairo_scale(c, k, k);
	cairo_translate(c, -r[0], -r[1]);
	agd_ruler_mark(c, &g, lk, NULL);
	cairo_destroy(c);
	struct turn3 t;
	agd_turn3(W, H, lk, &t);
	double m = AGD_SHADOW_M(W);
	struct hmat map;
	if (turn3_hmat(&t, -m, -m, W + m * 2.0, H + m * 2.0, &map)) {
		warp_paint(cr, img, r[0], r[1], k, &map, e);
	}
	cairo_surface_destroy(img);
}

bool desk_agenda_mark_extent(double W, double H, const struct desk_agenda_look *lk,
	double box[4])
{
	struct agd_geom g;
	agd_geometry(agd_measure(), W, H, lk, &g);
	double r[4];
	if (!agd_ruler_mark(NULL, &g, lk, r)) {
		return false;
	}
	if (!agd_held(lk)) {
		memcpy(box, r, sizeof r);
		return true;
	}
	struct turn3 t;
	agd_turn3(W, H, lk, &t);
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 4; i++) {
		double sx, sy;
		turn3_project(&t, (i & 1) ? r[0] + r[2] : r[0], (i & 2) ? r[1] + r[3] : r[1], 0.0,
			&sx, &sy, NULL);
		x0 = fmin(x0, sx);
		y0 = fmin(y0, sy);
		x1 = fmax(x1, sx);
		y1 = fmax(y1, sy);
	}
	box[0] = floor(x0) - 2.0;
	box[1] = floor(y0) - 2.0;
	box[2] = ceil(x1 - x0) + 4.0;
	box[3] = ceil(y1 - y0) + 4.0;
	return true;
}

void desk_agenda_extent(double W, double H, const struct desk_agenda_look *lk, double box[4])
{
	double m = AGD_SHADOW_M(W);
	if (!agd_held(lk)) {
		box[0] = -m;
		box[1] = -m;
		box[2] = W + m * 2.0;
		box[3] = H + m * 2.0;
		return;
	}
	struct turn3 t;
	agd_turn3(W, H, lk, &t);
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 8; i++) {
		double x = (i & 1) ? W + m : -m, y = (i & 2) ? H + m : -m;
		double z = (i & 4) ? W * 0.03 : -H * 0.06;
		double sx, sy;
		turn3_project(&t, x, y, z, &sx, &sy, NULL);
		x0 = fmin(x0, sx);
		y0 = fmin(y0, sy);
		x1 = fmax(x1, sx);
		y1 = fmax(y1, sy);
	}
	box[0] = floor(x0) - 2.0;
	box[1] = floor(y0) - 2.0;
	box[2] = ceil(x1 - x0) + 4.0;
	box[3] = ceil(y1 - y0) + 4.0;
}

void desk_agenda_turn_point(double W, double H, const struct desk_agenda_look *lk, double *x,
	double *y)
{
	if (!agd_held(lk)) {
		return;
	}
	struct turn3 t;
	agd_turn3(W, H, lk, &t);
	turn3_point(&t, *x, *y, x, y, NULL);
}

void desk_agenda_unturn(double W, double H, const struct desk_agenda_look *lk, double *x,
	double *y)
{
	if (!agd_held(lk)) {
		return;
	}
	struct turn3 t;
	agd_turn3(W, H, lk, &t);
	struct hmat map, inv;
	if (turn3_hmat(&t, 0.0, 0.0, W, H, &map) && hmat_invert(&map, &inv)) {
		hmat_apply(&inv, *x, *y, x, y);
	}
}

bool desk_agenda_note_pose(double W, double H, const struct desk_agenda_look *lk,
	struct desk_note_pose *pose)
{
	struct agd_geom g;
	agd_geometry(agd_measure(), W, H, lk, &g);
	if (!g.editing) {
		return false;
	}
	*pose = (struct desk_note_pose){ g.edit_r[0], g.edit_r[1], g.edit_r[2], g.edit_r[3], 0.0,
		g.font, DESK_NOTE_BINDER };
	return true;
}

int desk_agenda_ruler_minute(double W, double H, double x)
{
	double r[4], rx0, rx1;
	agd_ruler_place(W, H, r, &rx0, &rx1);
	int q = (int)lround(clampf((x - rx0) / (rx1 - rx0), 0.0, 1.0) * 96.0);
	return q * 15;
}

bool desk_agenda_writing(double W, double H, const struct desk_agenda_look *lk, double r[4])
{
	struct agd_geom g;
	agd_geometry(agd_measure(), W, H, lk, &g);
	if (!g.editing) {
		return false;
	}
	memcpy(r, g.edit_r, sizeof g.edit_r);
	return true;
}

int desk_agenda_hit(double W, double H, const struct desk_agenda_look *lk, double x,
	double y, int *arg)
{
	if (x < 0.0 || y < 0.0 || x > W || y > H) {
		return DESK_AGENDA_HIT_NONE;
	}
	struct agd_geom g;
	agd_geometry(agd_measure(), W, H, lk, &g);
	if (lk->ruler_t > 0.5 && x >= g.ruler[0] && x <= g.ruler[0] + g.ruler[2]
		&& y >= g.ruler[1] && y <= g.ruler[1] + g.ruler[3]) {
		if (x < g.rx0 - W * 0.012) {
			*arg = 0;
			return DESK_AGENDA_HIT_ALLDAY;
		}
		*arg = desk_agenda_ruler_minute(W, H, x);
		return DESK_AGENDA_HIT_RULER;
	}
	for (int c = 0; c < DESK_STICK_COLOURS; c++) {
		const double *p = g.pad[c];
		if (x >= p[0] && x <= p[0] + p[2] * 1.03 && y >= p[1] && y <= p[1] + p[3] * 1.05) {
			*arg = c;
			return DESK_AGENDA_HIT_PAD;
		}
	}
	if (g.editing && !lk->edit_away && x >= g.edit_r[0] && x <= g.edit_r[0] + g.edit_r[2]
		&& y >= g.edit_r[1]
		&& y <= g.edit_r[1] + g.edit_r[3]) {
		*arg = lk->edit;
		return DESK_AGENDA_HIT_NOTE;
	}
	for (int k = 0; k < 2; k++) {
		const struct agd_note *list = k ? g.todo : g.plan;
		int n = k ? g.ntodo : g.nplan;
		for (int i = 0; i < n; i++) {
			const double *r = list[i].r;
			if (list[i].s.id == lk->gone_id || list[i].s.id == lk->edit) {
				continue;
			}
			if (x >= r[0] && x <= r[0] + r[2] && y >= r[1] && y <= r[1] + r[3]) {
				*arg = list[i].s.id;
				/* a thing to do's box: past the scrap of colour */
				return k && x < r[0] + g.font * 2.35 ? DESK_AGENDA_HIT_CHECK
					: DESK_AGENDA_HIT_NOTE;
			}
		}
		const double *s = k ? g.slot_todo : g.slot_plan;
		if (s[2] > 0.0 && x >= s[0] && x <= s[0] + s[2] && y >= s[1] && y <= s[1] + s[3]) {
			*arg = 0;
			return k ? DESK_AGENDA_HIT_SLOT_TODO : DESK_AGENDA_HIT_SLOT_PLAN;
		}
	}
	*arg = 0;
	return DESK_AGENDA_HIT_BINDER;
}

/* ---- the memo pad ----------------------------------------------------
 *
 * A desk memo: a pad of ruled sheets on a board, the wire through their
 * tops, stood up as a tent beside the calendar -- the same make of thing,
 * with sheets to write on where the calendar has its months. On the shelf
 * its top sheet shows what is written on it; taken down (as the calendar
 * is) it is held up close and written on, and turned over the wire a
 * sheet at a time. */

static const struct room_gadget *memo_gadget(void)
{
	return &ROOM_GADGETS[DESK_GADGET_NOTEPAD];
}

double desk_memo_face_ratio(void)
{
	const struct room_gadget *g = memo_gadget();
	return (tent_page_w(g) + tent_tab(g)) / tent_page_w(g);
}

/* where the ruled lines are on a sheet h high -- the first, and from one to
 * the next -- and the margin down its left, of its width */
#define MEMO_RULE0 0.215
#define MEMO_PITCH 0.062
#define MEMO_MARGIN 0.11
#define MEMO_LINES (DESK_MEMO_TEXT + 8)

static int memo_rows(void)
{
	return (int)floor((0.975 - MEMO_RULE0) / MEMO_PITCH) + 1;
}

/* What is written, laid on the ruled lines: where each line of it starts
 * and ends in the string, the line the caret is on and how far along, the
 * input method's part (on one line, pre_line, from pre_x0 to pre_x1), and
 * the first line on the sheet -- the caret kept on it, the lines above
 * going off the top as the sheet fills. */
struct memo_lay {
	int n, first;
	int start[MEMO_LINES], end[MEMO_LINES];
	int caret_line;
	double caret_x;
	int pre_line;
	double pre_x0, pre_x1;
};

static double memo_width_of(cairo_t *cr, const char *s, int from, int to)
{
	char part[DESK_MEMO_TEXT + 256];
	snprintf(part, sizeof part, "%.*s", to - from, s + from);
	cairo_text_extents_t e;
	cairo_text_extents(cr, part, &e);
	return e.x_advance;
}

/* Break s into lines no wider than width -- at its newlines, at a space
 * where there is one, and anywhere in writing that has none. caret < 0:
 * no caret, and the sheet read from its top. */
static void memo_layout(cairo_t *cr, const char *s, double width, int caret, int pre0, int pre1,
	int rows, struct memo_lay *L)
{
	L->n = 0;
	L->first = 0;
	L->caret_line = 0;
	L->caret_x = 0.0;
	L->pre_line = -1;
	int len = (int)strlen(s);
	int ls = 0, last_space = -1;
	double x = 0.0;
	for (int i = 0; i <= len && L->n < MEMO_LINES - 1;) {
		if (i == len || s[i] == '\n') {
			L->start[L->n] = ls;
			L->end[L->n] = i;
			L->n++;
			if (i == len) {
				break;
			}
			i++;
			ls = i;
			x = 0.0;
			last_space = -1;
			continue;
		}
		char ch[8];
		int k = cal_letter(s + i, ch);
		cairo_text_extents_t e;
		cairo_text_extents(cr, ch, &e);
		if (x + e.x_advance > width && i > ls) {
			int cut = last_space >= ls ? last_space + 1 : i;
			L->start[L->n] = ls;
			L->end[L->n] = cut;
			L->n++;
			ls = cut;
			last_space = -1;
			/* what went over with it to the next line */
			x = memo_width_of(cr, s, ls, i);
			continue;
		}
		if (ch[0] == ' ') {
			last_space = i;
		}
		x += e.x_advance;
		i += k;
	}
	if (L->n == 0) {
		L->start[0] = L->end[0] = 0;
		L->n = 1;
	}
	if (caret >= 0) {
		for (int l = 0; l < L->n; l++) {
			if (L->start[l] <= caret) {
				L->caret_line = l;
			}
		}
		int cl = L->caret_line;
		int c = caret < L->end[cl] ? caret : L->end[cl];
		L->caret_x = memo_width_of(cr, s, L->start[cl], c);
		L->first = cl >= rows ? cl - rows + 1 : 0;
	}
	if (pre1 > pre0) {
		for (int l = 0; l < L->n; l++) {
			if (L->start[l] <= pre0 && pre0 <= L->end[l]) {
				L->pre_line = l;
				break;
			}
		}
		if (L->pre_line >= 0) {
			int pl = L->pre_line, e1 = pre1 < L->end[pl] ? pre1 : L->end[pl];
			L->pre_x0 = memo_width_of(cr, s, L->start[pl], pre0);
			L->pre_x1 = memo_width_of(cr, s, L->start[pl], e1);
		}
	}
}

/* The text as it is on the sheet: what is written with the input
 * method's part put in at the caret; and where the caret is in that. */
static int memo_shown(const char *text, int caret, const char *pre, int pre_caret,
	char out[DESK_MEMO_TEXT + 256], int *pre0, int *pre1)
{
	const char *t = text ? text : "", *p = pre ? pre : "";
	int tl = (int)strlen(t), pl = (int)strlen(p);
	int c = caret < 0 ? tl : (caret > tl ? tl : caret);
	snprintf(out, DESK_MEMO_TEXT + 256, "%.*s%s%s", c, t, p, t + c);
	*pre0 = c;
	*pre1 = c + pl;
	if (caret < 0) {
		return -1;
	}
	return pl ? c + (pre_caret < 0 ? pl : (pre_caret > pl ? pl : pre_caret)) : c;
}

/* Set the face for the writing on a sheet h high into cr. */
static void memo_ink_face(cairo_t *cr, const char *shown, double h)
{
	desk_font_select(cr, DESK_FONT_HAND, shown, false, false);
	cairo_set_font_size(cr, h * MEMO_PITCH * 0.64);
}

/* A sheet, w by h: the paper, the holes for the wire, the head, the rules
 * and the margin, and what is written on it. caret < 0: none shown. */
static void memo_sheet(cairo_t *cr, double w, double h, const char *text, int caret,
	const char *pre, int pre_caret, int number, int of, bool close)
{
	const struct room_gadget *g = memo_gadget();
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);
	/* the room's paper, a little warmer: a memo pad's cream */
	fill_tile(cr, paper_tile(), 1.0, 1.0);
	cairo_set_source_rgba(cr, 0.99, 0.94, 0.74, 0.26);
	cairo_paint(cr);
	/* the holes the wire goes through -- and on the shelf, where the
	 * traced wire is too fine to tell, the wire's front half through them
	 * printed, as the calendar has it */
	double hy = tent_hole_y(g) * h;
	double hw = fmax(w * 0.013, 1.2), hh = fmax(h * 0.024, 1.6);
	for (int i = 0; i < tent_loops(g); i++) {
		double x = tent_loop_x(g, i) * w;
		desk_rounded(cr, x - hw * 0.5, hy - hh * 0.5, hw, hh, hw * 0.5);
		cairo_set_source_rgb(cr, 0.13, 0.075, 0.045);
		cairo_fill(cr);
		if (close) {
			continue;
		}
		cairo_save(cr);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_width(cr, fmax(w * 0.012, 1.4));
		cairo_set_source_rgb(cr, 0.05, 0.05, 0.055);
		cairo_move_to(cr, x, hy);
		cairo_line_to(cr, x, -h * 0.05);
		cairo_stroke(cr);
		cairo_set_line_width(cr, fmax(w * 0.0038, 0.6));
		cairo_set_source_rgba(cr, 0.50, 0.50, 0.54, 0.75);
		cairo_move_to(cr, x - w * 0.0028, hy - hh * 0.2);
		cairo_line_to(cr, x - w * 0.0028, -h * 0.05);
		cairo_stroke(cr);
		cairo_restore(cr);
	}
	/* the head, printed on every sheet of the pad: what it is, and which
	 * sheet this is */
	double pitch = h * MEMO_PITCH;
	double hs = h * 0.036, by = h * (MEMO_RULE0 - MEMO_PITCH) - hs * 0.45;
	cal_spaced(cr, TR("MEMO"), w * MEMO_MARGIN, by, hs, hs * 0.32, true, 0.66, 0.20, 0.16);
	char no[32];
	if (of > 1) {
		snprintf(no, sizeof no, "No. %d / %d", number, of);
	} else {
		snprintf(no, sizeof no, "No. %d", number);
	}
	double nw = cal_spaced_w(cr, no, hs * 0.82, hs * 0.08, false);
	cal_spaced(cr, no, w * 0.95 - nw, by, hs * 0.82, hs * 0.08, false, 0.50, 0.42, 0.34);
	double ly = h * (MEMO_RULE0 - MEMO_PITCH) + pitch * 0.12;
	cairo_set_source_rgba(cr, 0.70, 0.22, 0.18, 0.70);
	cairo_set_line_width(cr, fmax(h * 0.0030, 0.7));
	cairo_move_to(cr, w * 0.035, ly);
	cairo_line_to(cr, w * 0.965, ly);
	cairo_stroke(cr);
	/* the rules, and the margin down the left */
	int rows = memo_rows();
	cairo_set_source_rgba(cr, 0.42, 0.58, 0.76, 0.55);
	cairo_set_line_width(cr, fmax(h * 0.0018, 0.5));
	for (int r = 0; r < rows; r++) {
		double y = h * MEMO_RULE0 + pitch * r;
		cairo_move_to(cr, w * 0.035, y);
		cairo_line_to(cr, w * 0.965, y);
	}
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 0.82, 0.32, 0.26, 0.55);
	cairo_move_to(cr, w * (MEMO_MARGIN - 0.025), ly);
	cairo_line_to(cr, w * (MEMO_MARGIN - 0.025), h);
	cairo_stroke(cr);

	/* what is written, in a blue-black ink, sitting on the rules */
	char shown[DESK_MEMO_TEXT + 256];
	int pre0, pre1;
	int pc = memo_shown(text, caret, pre, pre_caret, shown, &pre0, &pre1);
	memo_ink_face(cr, shown, h);
	double tx = w * MEMO_MARGIN, width = w * 0.95 - tx;
	static struct memo_lay L;
	memo_layout(cr, shown, width, pc, pre0, pre1, rows, &L);
	cairo_set_source_rgba(cr, 0.10, 0.15, 0.40, 0.94);
	for (int l = L.first; l < L.n && l - L.first < rows; l++) {
		double y = h * MEMO_RULE0 + pitch * (l - L.first) - pitch * 0.18;
		char line[DESK_MEMO_TEXT + 256];
		snprintf(line, sizeof line, "%.*s", L.end[l] - L.start[l], shown + L.start[l]);
		cairo_move_to(cr, tx, y);
		cairo_show_text(cr, line);
	}
	/* the input method's part, underlined as it is being written */
	if (L.pre_line >= L.first && L.pre_line - L.first < rows && L.pre_x1 > L.pre_x0) {
		double y = h * MEMO_RULE0 + pitch * (L.pre_line - L.first) - pitch * 0.07;
		cairo_set_line_width(cr, fmax(pitch * 0.045, 0.8));
		cairo_move_to(cr, tx + L.pre_x0, y);
		cairo_line_to(cr, tx + L.pre_x1, y);
		cairo_stroke(cr);
	}
	if (pc >= 0 && close) {
		double y = h * MEMO_RULE0 + pitch * (L.caret_line - L.first);
		cairo_rectangle(cr, tx + L.caret_x, y - pitch * 0.86, fmax(pitch * 0.055, 1.0),
			pitch * 0.76);
		cairo_set_source_rgb(cr, 0.08, 0.08, 0.12);
		cairo_fill(cr);
	}
	if (close && !shown[0]) {
		cairo_set_source_rgba(cr, 0.40, 0.36, 0.30, 0.42);
		cairo_move_to(cr, tx + pitch * 0.30, h * MEMO_RULE0 - pitch * 0.18);
		cairo_show_text(cr, TR("write here\xe2\x80\xa6"));
	}
	cairo_restore(cr);
}

/* On the shelf: the sheet on top, as it was left. */
static void gadget_memo(cairo_t *cr, double w, double h)
{
	int top = desk_memo_top();
	memo_sheet(cr, w / desk_memo_face_ratio(), h, desk_memo_sheet(top), -1, NULL, 0,
		top + 1, desk_memo_sheets(), false);
}

/* Close up: the sheet in the middle of the view, a little below its
 * middle; box, everything drawn -- the board and its shadow, and above,
 * as high as a sheet going over the wire goes. */
void desk_memo_rect(int w, int h, double page[4], double box[4])
{
	const struct room_gadget *g = memo_gadget();
	double asp = tent_page_w(g) / tent_page_h(g);
	double ph = fmin(h * 0.64, w * 0.60 / asp);
	double pw = ph * asp;
	page[0] = floor((w - pw) * 0.5);
	page[1] = floor(h * 0.54 - ph * 0.5);
	page[2] = ceil(pw);
	page[3] = ceil(ph);
	double pad = pw * 0.10;
	box[0] = floor(page[0] - pad);
	box[1] = 0.0;
	box[2] = ceil(pw + 2.0 * pad);
	box[3] = ceil(page[1] + ph * 1.12 + pad);
}

/* the board behind the sheets, round them, in the close up's pixels */
static void memo_board(const double pg[4], double b[4])
{
	const struct room_gadget *g = memo_gadget();
	double ppu = pg[3] / tent_page_h(g);
	b[0] = pg[0] - CAL_PIN * ppu;
	b[1] = pg[1] - CAL_PTOP * ppu;
	b[2] = 2.0 * g->hx * ppu;
	b[3] = g->h * ppu;
}

/* a sheet as a picture, for going over the wire */
static cairo_surface_t *memo_sheet_image(int w, int h, const char *text, int number, int of)
{
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w > 1 ? w : 1,
		h > 1 ? h : 1);
	cairo_t *cr = cairo_create(img);
	memo_sheet(cr, w, h, text, -1, NULL, 0, number, of, true);
	cairo_destroy(cr);
	return img;
}

/* where the caret is on the flat close up: x, y, w, h */
static void memo_caret_flat(int w, int h, const struct desk_memo_look *lk, double out[4])
{
	double pg[4], box[4];
	desk_memo_rect(w, h, pg, box);
	double ph = pg[3], pw = pg[2], pitch = ph * MEMO_PITCH;
	char shown[DESK_MEMO_TEXT + 256];
	int pre0, pre1;
	int pc = memo_shown(lk->text, lk->caret < 0 ? 0 : lk->caret, lk->preedit,
		lk->preedit_caret, shown, &pre0, &pre1);
	cairo_t *cr = agd_measure();
	cairo_save(cr);
	memo_ink_face(cr, shown, ph);
	static struct memo_lay L;
	double tx = pw * MEMO_MARGIN;
	memo_layout(cr, shown, pw * 0.95 - tx, pc, pre0, pre1, memo_rows(), &L);
	cairo_restore(cr);
	out[0] = pg[0] + tx + L.caret_x;
	out[1] = pg[1] + ph * MEMO_RULE0 + pitch * (L.caret_line - L.first) - pitch * 0.86;
	out[2] = fmax(pitch * 0.055, 1.0);
	out[3] = pitch * 0.76;
}

static void memo_draw_flat(cairo_t *cr, int w, int h, const struct desk_memo_look *lk)
{
	const struct room_gadget *g = memo_gadget();
	double pg[4], box[4], bd[4];
	desk_memo_rect(w, h, pg, box);
	memo_board(pg, bd);
	double px = pg[0], py = pg[1], pw = pg[2], ph = pg[3];
	int of = lk->sheets > 0 ? lk->sheets : 1;
	/* a sheet going over: on to the next, the one under it flat; or back
	 * over the wire, coming down onto this one */
	double t = clampf(lk->turn_t, 0.0, 1.0);
	t = t * t * (3.0 - 2.0 * t);
	struct cal_fly fly = { 0.0, NULL, -1 };
	bool turning = lk->turn != 0 && lk->other;
	const char *flat_text = lk->text;
	int flat_no = lk->sheet + 1, flat_caret = lk->caret;
	if (turning && lk->turn > 0) {
		fly.a = CAL_REST * t;
		fly.img = memo_sheet_image((int)pw, (int)ph, lk->text, lk->sheet + 1, of);
		flat_text = lk->other;
		flat_no = lk->other_sheet + 1;
		flat_caret = -1;
	} else if (turning) {
		fly.a = CAL_REST * (1.0 - t);
		fly.img = memo_sheet_image((int)pw, (int)ph, lk->other, lk->other_sheet + 1, of);
		flat_caret = -1;
	}

	/* its shadow on whatever is behind it */
	cairo_save(cr);
	for (int i = 0; i < 10; i++) {
		double gr = i * pw * 0.006;
		desk_rounded(cr, bd[0] - gr + pw * 0.010, bd[1] - gr + ph * 0.030,
			bd[2] + 2.0 * gr, bd[3] + 2.0 * gr, pw * 0.012 + gr);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.055);
		cairo_fill(cr);
	}
	cairo_restore(cr);
	if (fly.img && fly.a > M_PI) {
		cal_fly_draw(cr, &fly, px, py, pw, ph, 0.0, -1);
	}
	/* the board, the edges of the sheets under the top one at its foot,
	 * and the top one */
	leather_fill(cr, bd[0], bd[1], bd[2], bd[3], pw * 0.012);
	int under = of - flat_no;
	under = under < 3 ? 3 : (under > 7 ? 7 : under);
	for (int i = under; i >= 1; i--) {
		double k = ph / 420.0;
		cairo_rectangle(cr, px + 0.5, py + ph - k + i * 1.1 * k, pw - 1.0, 1.5 * k);
		cairo_set_source_rgb(cr, 0.93 - 0.035 * i, 0.91 - 0.035 * i, 0.83 - 0.035 * i);
		cairo_fill(cr);
	}
	cairo_save(cr);
	cairo_translate(cr, px, py);
	memo_sheet(cr, pw, ph, flat_text, flat_caret, flat_caret >= 0 ? lk->preedit : NULL,
		lk->preedit_caret, flat_no, of, true);
	cairo_restore(cr);
	if (fly.img && fly.a <= M_PI) {
		/* the shadow it throws on the sheet under it, along its free edge */
		double s = cos(fly.a - CAL_EYE), sa = sin(fly.a);
		double ye = py + fmax(s, 0.0) * ph, band = ph * (0.02 + 0.10 * sa);
		cairo_pattern_t *sh = cairo_pattern_create_linear(0, ye, 0, ye + band);
		cairo_pattern_add_color_stop_rgba(sh, 0.0, 0, 0, 0, 0.30 * sa);
		cairo_pattern_add_color_stop_rgba(sh, 1.0, 0, 0, 0, 0.0);
		cairo_set_source(cr, sh);
		cairo_rectangle(cr, px, ye, pw, band);
		cairo_fill(cr);
		cairo_pattern_destroy(sh);
		cal_fly_draw(cr, &fly, px, py, pw, ph, 0.0, -1);
	}
	if (fly.img) {
		cairo_surface_destroy(fly.img);
	}

	/* the wire, over all of it: the front half of each loop, from its hole
	 * up over the top of the board */
	double hy = py + tent_hole_y(g) * ph;
	double top = py - ph * (CAL_RR - CAL_RY + CAL_PTOP) / tent_page_h(g);
	double lw = pw * (2.0 * CAL_RW + 0.010) / tent_page_w(g);
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int i = 0; i < tent_loops(g); i++) {
		double x = px + tent_loop_x(g, i) * pw;
		cairo_set_line_width(cr, lw * 1.15);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
		cairo_move_to(cr, x + lw * 0.55, hy + lw * 0.65);
		cairo_line_to(cr, x + lw * 0.55, top + lw * 0.65);
		cairo_stroke(cr);
		cairo_set_line_width(cr, lw);
		cairo_set_source_rgb(cr, 0.065, 0.065, 0.075);
		cairo_move_to(cr, x, hy);
		cairo_line_to(cr, x, top);
		cairo_stroke(cr);
		cairo_set_line_width(cr, lw * 0.26);
		cairo_set_source_rgba(cr, 0.62, 0.62, 0.66, 0.70);
		cairo_move_to(cr, x - lw * 0.20, hy - lw * 0.2);
		cairo_line_to(cr, x - lw * 0.20, top);
		cairo_stroke(cr);
	}
	cairo_restore(cr);

	/* the input method's candidates, on a slip by the caret: below it, or
	 * above if there is no room on the board below */
	if (lk->cands && lk->ncand > 0 && !turning) {
		double cb[4];
		memo_caret_flat(w, h, lk, cb);
		double row = ph * 0.052, sw = pw * 0.40, sh = row * lk->ncand + row * 0.30;
		double sx = fmax(bd[0], fmin(cb[0] - row * 0.3, bd[0] + bd[2] - sw));
		double sy = cb[1] + cb[3] + row * 0.30;
		if (sy + sh > bd[1] + bd[3]) {
			sy = cb[1] - row * 0.30 - sh;
		}
		cairo_save(cr);
		cairo_rectangle(cr, sx + 2, sy + 3, sw, sh);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
		cairo_fill(cr);
		cairo_rectangle(cr, sx, sy, sw, sh);
		cairo_set_source_rgb(cr, 0.99, 0.98, 0.95);
		cairo_fill(cr);
		for (int i = 0; i < lk->ncand; i++) {
			double ry = sy + row * 0.15 + row * i;
			if (i == lk->cand_cursor) {
				cairo_rectangle(cr, sx + 2, ry, sw - 4, row);
				cairo_set_source_rgba(cr, 0.99, 0.84, 0.38, 0.75);
				cairo_fill(cr);
			}
			char label[112];
			snprintf(label, sizeof label, "%d  %s", i + 1, lk->cands[i]);
			desk_font_select(cr, DESK_FONT_UI, label, false, false);
			cairo_set_font_size(cr, row * 0.60);
			cairo_set_source_rgb(cr, 0.15, 0.13, 0.11);
			cairo_move_to(cr, sx + row * 0.40, ry + row * 0.74);
			cairo_show_text(cr, label);
		}
		cairo_restore(cr);
	}
}

static void memo_turn3(int w, int h, const struct desk_memo_look *lk, struct turn3 *t)
{
	double pg[4], box[4];
	desk_memo_rect(w, h, pg, box);
	t->px = pg[0] + pg[2] * 0.5;
	t->py = pg[1] + pg[3] * 0.5;
	t->yaw = lk->yaw;
	t->pitch = lk->pitch;
	t->ex = lk->eye_f > 0.0 ? lk->eye_x : w * 0.5;
	t->ey = lk->eye_f > 0.0 ? lk->eye_y : h * 0.5;
	t->f = lk->eye_f > 0.0 ? lk->eye_f : h * 1.6;
}

static bool memo_held(const struct desk_memo_look *lk)
{
	return fabs(lk->yaw) > 1e-4 || fabs(lk->pitch) > 1e-4;
}

static void memo_tent_px(int w, int h, struct cal_tent_px *k)
{
	const struct room_gadget *g = memo_gadget();
	double box[4];
	desk_memo_rect(w, h, k->pg, box);
	memo_board(k->pg, k->bd);
	double ppu = k->pg[3] / tent_page_h(g);
	k->pz = CAL_PZ * ppu;
	k->bt = 2.0 * CAL_T * ppu;
	k->len = k->bd[3];
	k->ang = g->tilt + CAL_TB;
}

void desk_draw_memo(cairo_t *cr, int w, int h, const struct desk_memo_look *lk)
{
	if (!memo_held(lk)) {
		memo_draw_flat(cr, w, h, lk);
		return;
	}
	double pg[4], box[4];
	desk_memo_rect(w, h, pg, box);
	/* the face, flat, at the size cr draws at; then turned, as the
	 * calendar is held */
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double k = fmax(hypot(kx, ky), 0.25);
	int iw = (int)ceil(box[2] * k), ih = (int)ceil(box[3] * k);
	if (iw < 2 || ih < 2) {
		return;
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
	cairo_t *c = cairo_create(img);
	cairo_scale(c, k, k);
	cairo_translate(c, -box[0], -box[1]);
	memo_draw_flat(c, w, h, lk);
	cairo_destroy(c);
	struct turn3 t;
	memo_turn3(w, h, lk, &t);
	struct cal_tent_px tp;
	memo_tent_px(w, h, &tp);
	cal_sides(cr, &t, &tp);
	struct hmat map;
	if (turn3_hmat(&t, box[0], box[1], box[2], box[3], &map)) {
		warp_paint(cr, img, box[0], box[1], k, &map, 1.0);
	}
	cairo_surface_destroy(img);
	held_face_light(cr, &t, tp.bd[0], tp.bd[1], tp.bd[2], tp.bd[3], 0.10);
}

void desk_memo_extent(int w, int h, const struct desk_memo_look *lk, double out[4])
{
	double pg[4], box[4];
	desk_memo_rect(w, h, pg, box);
	if (!memo_held(lk)) {
		memcpy(out, box, sizeof box);
		return;
	}
	struct turn3 t;
	memo_turn3(w, h, lk, &t);
	struct cal_tent_px tp;
	memo_tent_px(w, h, &tp);
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	double depth = tp.pz + tp.bt + tp.len * sin(tp.ang) + tp.bt;
	for (int i = 0; i < 8; i++) {
		double x = (i & 1) ? box[0] + box[2] : box[0];
		double y = (i & 2) ? box[1] + box[3] : box[1];
		double z = (i & 4) ? depth : 0.0;
		double sx, sy;
		turn3_project(&t, x, y, z, &sx, &sy, NULL);
		x0 = fmin(x0, sx);
		y0 = fmin(y0, sy);
		x1 = fmax(x1, sx);
		y1 = fmax(y1, sy);
	}
	out[0] = floor(x0) - 2.0;
	out[1] = floor(fmax(y0, 0.0)) - 2.0;
	out[2] = ceil(x1 - x0) + 4.0;
	out[3] = ceil(y1 - fmax(y0, 0.0)) + 4.0;
}

void desk_memo_unturn(int w, int h, const struct desk_memo_look *lk, double *x, double *y)
{
	if (!memo_held(lk)) {
		return;
	}
	struct turn3 t;
	memo_turn3(w, h, lk, &t);
	double pg[4], box[4];
	desk_memo_rect(w, h, pg, box);
	struct hmat map, inv;
	if (turn3_hmat(&t, box[0], box[1], box[2], box[3], &map) && hmat_invert(&map, &inv)) {
		hmat_apply(&inv, *x, *y, x, y);
	}
}

void desk_memo_caret_box(int w, int h, const struct desk_memo_look *lk, double out[4])
{
	double cb[4];
	memo_caret_flat(w, h, lk, cb);
	if (!memo_held(lk)) {
		memcpy(out, cb, sizeof cb);
		return;
	}
	struct turn3 t;
	memo_turn3(w, h, lk, &t);
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 4; i++) {
		double sx, sy;
		turn3_point(&t, cb[0] + ((i & 1) ? cb[2] : 0.0), cb[1] + ((i & 2) ? cb[3] : 0.0),
			&sx, &sy, NULL);
		x0 = fmin(x0, sx);
		y0 = fmin(y0, sy);
		x1 = fmax(x1, sx);
		y1 = fmax(y1, sy);
	}
	out[0] = x0;
	out[1] = y0;
	out[2] = x1 - x0;
	out[3] = y1 - y0;
}

/* The place between two letters of line l nearest x along it (x from the
 * line's start), in cr's face; at the end of a line broken at a space,
 * before the space -- or the caret would be at the start of the next. */
static int memo_nearest(cairo_t *cr, const char *t, const struct memo_lay *L, int l, double x)
{
	int best = L->start[l];
	double bx = 1e30, at = 0.0;
	char ch[8];
	for (int i = L->start[l];;) {
		double d = fabs(at - x);
		if (d < bx) {
			bx = d;
			best = i;
		}
		if (i >= L->end[l] || t[i] == '\n') {
			break;
		}
		int k = cal_letter(t + i, ch);
		cairo_text_extents_t e;
		cairo_text_extents(cr, ch, &e);
		at += e.x_advance;
		i += k;
	}
	if (best == L->end[l] && l + 1 < L->n && L->start[l + 1] == best && best > L->start[l]
		&& t[best - 1] == ' ') {
		best--;
	}
	return best;
}

int desk_memo_hit(int w, int h, const struct desk_memo_look *lk, double x, double y)
{
	double pg[4], box[4], bd[4];
	desk_memo_rect(w, h, pg, box);
	memo_board(pg, bd);
	double m = pg[2] * 0.02;
	if (x < bd[0] - m || x > bd[0] + bd[2] + m || y < bd[1] - m || y > bd[1] + bd[3] + m) {
		return -1;
	}
	/* the line the point is on, as the sheet is now, and the letter on it
	 * nearest the point */
	double ph = pg[3], pw = pg[2], pitch = ph * MEMO_PITCH;
	const char *t = lk->text ? lk->text : "";
	cairo_t *cr = agd_measure();
	cairo_save(cr);
	memo_ink_face(cr, t, ph);
	static struct memo_lay L;
	double tx = pw * MEMO_MARGIN;
	int rows = memo_rows();
	memo_layout(cr, t, pw * 0.95 - tx, lk->caret < 0 ? 0 : lk->caret, 0, 0, rows, &L);
	int l = L.first + (int)floor((y - pg[1] - ph * MEMO_RULE0) / pitch) + 1;
	int last = L.first + rows - 1 < L.n - 1 ? L.first + rows - 1 : L.n - 1;
	l = l < L.first ? L.first : (l > last ? last : l);
	int best = memo_nearest(cr, t, &L, l, x - pg[0] - tx);
	cairo_restore(cr);
	return best;
}

int desk_memo_caret_step(int w, int h, const struct desk_memo_look *lk, int dir)
{
	double pg[4], box[4];
	desk_memo_rect(w, h, pg, box);
	const char *t = lk->text ? lk->text : "";
	int len = (int)strlen(t);
	int c = lk->caret < 0 ? len : (lk->caret > len ? len : lk->caret);
	cairo_t *cr = agd_measure();
	cairo_save(cr);
	memo_ink_face(cr, t, pg[3]);
	static struct memo_lay L;
	double tx = pg[2] * MEMO_MARGIN;
	memo_layout(cr, t, pg[2] * 0.95 - tx, c, 0, 0, memo_rows(), &L);
	int l = L.caret_line + (dir < 0 ? -1 : 1);
	int to = l < 0 ? 0 : l >= L.n ? len : memo_nearest(cr, t, &L, l, L.caret_x);
	cairo_restore(cr);
	return to;
}

bool desk_shelf_memo_hit(int w, int h, double x, double y)
{
	double b[4];
	if (!desk_shelf_gadget_box(w, h, DESK_GADGET_NOTEPAD, b)) {
		return false;
	}
	double mx = b[2] * 0.06, my = b[3] * 0.10;
	return x >= b[0] - mx && x <= b[0] + b[2] + mx && y >= b[1] - my && y <= b[1] + b[3] + my;
}

/* ---- the tray ---------------------------------------------------
 *
 * What stands along the back edge of the desk -- the console's ledge, the
 * clock, the post box, the plate that sends you to the
 * shelf -- is the machine's tray, and windows keep off it the way they keep
 * off any panel. Where it ends is wherever the camera puts the back of the
 * desk for this shape of screen, so it is measured, not assumed. */

struct tray_extent {
	double bottom;     /* the lowest point of anything on it */
	double post_x1;    /* and where the post box ends, to the right */
};

static bool tray_measure(int w, int h, struct tray_extent *te)
{
	struct room_xf xf;
	if (!room_xform(w, h, 0.0, &xf)) {
		return false;
	}
	struct room_render *rr = xf.rr;
	double sw = rr->w * xf.scale, sh = rr->h * xf.scale;
	double bottom = 0.0, post_x1 = 0.0;

	double cqx[CC_QUADS][4], cqy[CC_QUADS][4];
	if (cc_desk_quads(rr, &rr->cam, xf.ox, xf.oy, sw, sh, 0.0, cqx, cqy)) {
		for (int i = 0; i < CC_QUADS; i++) {
			for (int k = 0; k < 4; k++) {
				bottom = fmax(bottom, cqy[i][k]);
			}
		}
	}
	double pqx[2][4], pqy[2][4];
	if (pb_quads(rr, &rr->cam, xf.ox, xf.oy, sw, sh, pqx, pqy)) {
		for (int i = 0; i < 2; i++) {
			for (int k = 0; k < 4; k++) {
				bottom = fmax(bottom, pqy[i][k]);
				post_x1 = fmax(post_x1, pqx[i][k]);
			}
		}
	}
	/* the clock: the whole of its case, not only the face */
	for (int i = 0; i < DESK_GADGET_COUNT; i++) {
		const struct room_gadget *g = &ROOM_GADGETS[i];
		if (gadget_upright(g)) {
			continue;
		}
		for (int k = 0; k < 8; k++) {
			vec3 p = v3(g->x + ((k & 1) ? g->hx : -g->hx), (k & 2) ? g->h : 0.0,
				gadget_z(g) + ((k & 4) ? g->hz : -g->hz));
			double u, v;
			if (room_project(&rr->cam, p, &u, &v)) {
				bottom = fmax(bottom, xf.oy + v * sh);
			}
		}
	}
	/* and the slips tucked under the machine, as far out as they come --
	 * when there are any */
	struct slip_room sr;
	if (g_slips.n > 0 && slip_room(w, h, 0.0, &sr)) {
		bottom = fmax(bottom,
			sr.yp + (SLIP_OUT + 0.05 + SLIP_ATTENTION) * sr.s);
	}
	if (bottom <= 0.0) {
		return false;
	}
	te->bottom = bottom;
	te->post_x1 = post_x1;
	return true;
}

int desk_tray_bottom(int w, int h)
{
	struct tray_extent te;
	/* before the room is traced, about where it will be */
	double bottom = tray_measure(w, h, &te) ? te.bottom : h * 0.140;
	/* and clear of it by the line of shadow along the foot of each thing */
	return (int)ceil(bottom + 3.0);
}

int desk_drawer_pull_h(int w, int h)
{
	(void)w;
	/* the strip desk_drawer_handle_hit takes a shut drawer's pull to be */
	return (int)ceil(fmax(h * 0.035, 16.0));
}
