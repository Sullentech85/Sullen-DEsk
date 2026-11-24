/* Part of draw.c: the ALL APPS book.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

int desk_book_matches(const char *query, int *out, int max)
{
	char q[256];
	book_trim(query ? query : "", q, sizeof q);
	const struct desk_app_list *l = desk_apps_acquire();
	int n = book_matches_in(l, q, out, max);
	desk_apps_release(l);
	return n;
}

/* The open book, flat, on a screen w by h. */
struct book_geom {
	double bx, by, bw, bh;        /* the covers */
	double px0, py0, px1, py1;    /* the pages, both, outer edge to outer edge */
	double gx;                    /* the gutter */
	double row;                   /* a line of the index */
	double l_top, r_top, bottom;  /* where the lines start on the two pages, and stop */
	double tab_h;                 /* a tab of the thumb index */
	int l_rows, r_rows;
};

static void book_geom(int w, int h, struct book_geom *g)
{
	g->bw = fmin(w * 0.80, h * 1.42);
	g->bh = g->bw * 0.62;
	g->bx = floor((w - g->bw) * 0.5);
	g->by = floor(h * 0.965 - g->bh);
	g->px0 = g->bx + g->bw * 0.022;
	g->px1 = g->bx + g->bw * 0.978;
	g->py0 = g->by + g->bh * 0.030;
	g->py1 = g->by + g->bh * 0.962;
	g->gx = g->bx + g->bw * 0.5;
	g->row = g->bh * 0.064;
	g->l_top = g->py0 + g->bh * 0.255;
	g->r_top = g->py0 + g->bh * 0.075;
	g->bottom = g->py1 - g->bh * 0.070;
	g->l_rows = (int)floor((g->bottom - g->l_top) / g->row);
	g->r_rows = (int)floor((g->bottom - g->r_top) / g->row);
	g->tab_h = (g->py1 - g->py0 - g->bh * 0.06) / DESK_BOOK_TABS;
}

/* The index laid out: for each line of it its spread, page and row; and
 * where the letters begin, each a row of its own when nothing is written
 * (an initial heads them). Kept until something about it changes. */
struct book_index {
	int w, h;
	unsigned gen;
	char query[256];
	int n;
	int app[BOOK_MAX], spread[BOOK_MAX], side[BOOK_MAX], row[BOOK_MAX];
	int nh;
	int h_letter[DESK_BOOK_TABS], h_spread[DESK_BOOK_TABS], h_side[DESK_BOOK_TABS];
	int h_row[DESK_BOOK_TABS];
	int spreads;
	int tab_spread[DESK_BOOK_TABS];
};

static const struct book_index *book_index(int w, int h, const char *query)
{
	static struct book_index bi;
	static bool ok;
	char q[256];
	book_trim(query ? query : "", q, sizeof q);
	const struct desk_app_list *l = desk_apps_acquire();
	unsigned gen = l->generation;
	if (ok && bi.w == w && bi.h == h && bi.gen == gen && strcmp(bi.query, q) == 0) {
		desk_apps_release(l);
		return &bi;
	}
	struct book_geom g;
	book_geom(w, h, &g);
	memset(&bi, 0, sizeof bi);
	bi.w = w;
	bi.h = h;
	bi.gen = gen;
	snprintf(bi.query, sizeof bi.query, "%s", q);
	bi.n = book_matches_in(l, q, bi.app, BOOK_MAX);
	bi.n = bi.n > BOOK_MAX ? BOOK_MAX : bi.n;
	for (int t = 0; t < DESK_BOOK_TABS; t++) {
		bi.tab_spread[t] = -1;
	}
	int spread = 0, side = 0, row = 0, letter = -1;
	for (int i = 0; i < bi.n; i++) {
		int lt = book_letter(&l->apps[bi.app[i]]);
		bool head = !q[0] && lt != letter;
		/* an initial does not stand at the foot of a page on its own */
		int need = head ? 2 : 1;
		int rows = side ? g.r_rows : g.l_rows;
		if (row + need > rows) {
			row = 0;
			side = !side;
			if (!side) {
				spread++;
			}
		}
		if (head && bi.nh < DESK_BOOK_TABS) {
			bi.h_letter[bi.nh] = lt;
			bi.h_spread[bi.nh] = spread;
			bi.h_side[bi.nh] = side;
			bi.h_row[bi.nh] = row;
			bi.nh++;
			row++;
			letter = lt;
		}
		if (bi.tab_spread[lt] < 0) {
			bi.tab_spread[lt] = spread;
		}
		bi.spread[i] = spread;
		bi.side[i] = side;
		bi.row[i] = row;
		row++;
	}
	bi.spreads = spread + 1;
	desk_apps_release(l);
	ok = true;
	return &bi;
}

int desk_book_spreads(int w, int h, const char *query)
{
	return book_index(w, h, query)->spreads;
}

int desk_book_spread_of(int w, int h, const char *query, int i)
{
	const struct book_index *bi = book_index(w, h, query);
	return i >= 0 && i < bi->n ? bi->spread[i] : -1;
}

int desk_book_tab_spread(int w, int h, const char *query, int tab)
{
	const struct book_index *bi = book_index(w, h, query);
	return tab >= 0 && tab < DESK_BOOK_TABS ? bi->tab_spread[tab] : -1;
}

/* where a row of a page is: its left, its top, its width */
static void book_row_rect(const struct book_geom *g, int side, int row, double r[4])
{
	double x0 = side ? g->gx + g->bw * 0.040 : g->px0 + g->bw * 0.040;
	double x1 = side ? g->px1 - g->bw * 0.060 : g->gx - g->bw * 0.034;
	r[0] = x0;
	r[1] = (side ? g->r_top : g->l_top) + g->row * row;
	r[2] = x1 - x0;
	r[3] = g->row;
}

/* the line to write on, at the head of a left-hand page */
static void book_find_rect(const struct book_geom *g, double r[4])
{
	r[0] = g->px0 + g->bw * 0.040;
	r[1] = g->py0 + g->bh * 0.118;
	r[2] = g->gx - g->bw * 0.034 - r[0];
	r[3] = g->bh * 0.070;
}

static void book_tab_rect(const struct book_geom *g, int t, double r[4])
{
	r[0] = g->px1 - g->bw * 0.024;
	r[1] = g->py0 + g->bh * 0.030 + g->tab_h * t;
	r[2] = g->bw * 0.030;
	r[3] = g->tab_h * 0.92;
}

static const char *const BOOK_TAB_NAME[DESK_BOOK_TABS] = {
	"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q",
	"R", "S", "T", "U", "V", "W", "X", "Y", "Z", "#",
};

/* A book face for what is only ever in Latin letters -- the head, the
 * initials, the tabs, the folios -- and the reader's own for names, which
 * may be in any script: one family has to cover all of a string, and a
 * serif chosen for Latin need not have a single kana. */
static void book_font_as(cairo_t *cr, const char *family, double size, bool bold, bool italic)
{
	cairo_select_font_face(cr, family, italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL,
		bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, size);
}

static void book_font(cairo_t *cr, double size, bool bold, bool italic)
{
	book_font_as(cr, desk_font_family(DESK_FONT_DISPLAY, NULL), size, bold, italic);
}

/* Names in any script, printed: in a face that has Japanese as well as
 * Latin letters -- or, for one in a script that face has not got, in one
 * that has (fonts.h). text is what is to be set, or NULL. */
static void book_font_any(cairo_t *cr, const char *text, double size, bool bold, bool italic)
{
	book_font_as(cr, desk_font_family_wide(DESK_FONT_PRINT, text), size, bold, italic);
}

/* text at x, y cut to width w with an ellipsis; how wide it came out */
static double book_text(cairo_t *cr, const char *s, double x, double y, double w)
{
	cairo_text_extents_t e;
	cairo_text_extents(cr, s, &e);
	if (e.x_advance <= w) {
		cairo_move_to(cr, x, y);
		cairo_show_text(cr, s);
		return e.x_advance;
	}
	char buf[256];
	snprintf(buf, sizeof buf, "%s", s);
	size_t n = strlen(buf);
	cairo_text_extents_t el;
	cairo_text_extents(cr, "\xe2\x80\xa6", &el);
	while (n > 0) {
		do {
			n--;
		} while (n > 0 && ((unsigned char)buf[n] & 0xc0) == 0x80);
		buf[n] = '\0';
		cairo_text_extents(cr, buf, &e);
		if (e.x_advance + el.x_advance <= w) {
			break;
		}
	}
	snprintf(buf + n, sizeof buf - n, "\xe2\x80\xa6");
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, buf);
	cairo_text_extents(cr, buf, &e);
	return e.x_advance;
}

/* The favourites' flag of a line of the index: in the margin before it, an
 * arrow of the same mustard film as on the binders' sheets, pointing at the
 * line (binder_flag); x, y, w, h. */
static void book_flag_rect(const double r[4], double f[4])
{
	double rh = r[3];
	f[0] = r[0] - rh * 0.78;
	f[1] = r[1] + rh * 0.30;
	f[2] = rh * 0.60;
	f[3] = rh * 0.34;
}

static void book_flag(cairo_t *cr, const double r[4], bool stuck, bool hot)
{
	double f[4];
	book_flag_rect(r, f);
	double x = f[0], y = f[1], fw = f[2], fh = f[3], tip = fh * 0.55;
	cairo_save(cr);
	cairo_move_to(cr, x, y);
	cairo_line_to(cr, x + fw - tip, y);
	cairo_line_to(cr, x + fw, y + fh * 0.5);
	cairo_line_to(cr, x + fw - tip, y + fh);
	cairo_line_to(cr, x, y + fh);
	cairo_close_path(cr);
	if (!stuck) {
		cairo_set_source_rgba(cr, 0.93, 0.72, 0.18, hot ? 0.40 : 0.12);
		cairo_fill_preserve(cr);
		static const double DASH[] = { 2.4, 2.0 };
		cairo_set_dash(cr, DASH, hot ? 0 : 2, 0.0);
		cairo_set_line_width(cr, 0.9);
		cairo_set_source_rgba(cr, 0.42, 0.33, 0.20, hot ? 0.75 : 0.45);
		cairo_stroke(cr);
		cairo_restore(cr);
		return;
	}
	cairo_pattern_t *g = cairo_pattern_create_linear(x, 0, x + fw, 0);
	cairo_pattern_add_color_stop_rgba(g, 0.0, hot ? 1.0 : 0.96, 0.76, 0.22, 0.90);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.90, 0.66, 0.14, 0.78);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 0.7);
	cairo_set_source_rgba(cr, 0.46, 0.32, 0.04, 0.55);
	cairo_stroke(cr);
	cairo_move_to(cr, x + fh * 0.2, y + 0.8);
	cairo_line_to(cr, x + fw - tip, y + 0.8);
	cairo_set_line_width(cr, 0.7);
	cairo_set_source_rgba(cr, 1.0, 0.96, 0.80, 0.45);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* One line of the index: its favourites' flag, if it has one (or, the
 * hand on the line, where one goes), the program's mark, its name and what
 * it is, a leader of dots and the shelf it is on. */
static void book_entry(cairo_t *cr, const struct book_geom *g, const double r[4],
	const struct desk_app *a, const struct desk_app_list *l, bool hot, bool chosen,
	bool flag_hot)
{
	(void)g;
	double x = r[0], y = r[1], w = r[2], rh = r[3];
	if (a->fav || hot) {
		book_flag(cr, r, a->fav, flag_hot);
	}
	if (hot || chosen) {
		/* gone over with a highlighter */
		cairo_save(cr);
		cairo_rectangle(cr, x - rh * 0.15, y + rh * 0.10, w + rh * 0.3, rh * 0.82);
		cairo_set_source_rgba(cr, 0.99, 0.88, 0.36, chosen ? 0.50 : 0.34);
		cairo_fill(cr);
		cairo_restore(cr);
	}
	double is = rh * 0.70, ix = x, iy = y + (rh - is) * 0.5;
	cairo_surface_t *ic = desk_app_icon(a);
	if (ic) {
		double iw = cairo_image_surface_get_width(ic), ih = cairo_image_surface_get_height(ic);
		double k = is / fmax(iw, ih);
		cairo_save(cr);
		cairo_translate(cr, ix + (is - iw * k) * 0.5, iy + (is - ih * k) * 0.5);
		cairo_scale(cr, k, k);
		cairo_set_source_surface(cr, ic, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		/* a printed initial in a ring, for one with no mark of its own */
		cairo_save(cr);
		cairo_arc(cr, ix + is * 0.5, iy + is * 0.5, is * 0.44, 0, 2 * M_PI);
		cairo_set_source_rgba(cr, 0.42, 0.10, 0.08, 0.85);
		cairo_set_line_width(cr, fmax(is * 0.05, 0.8));
		cairo_stroke(cr);
		book_font_any(cr, a->initial, is * 0.50, true, false);
		cairo_text_extents_t e;
		cairo_text_extents(cr, a->initial, &e);
		cairo_move_to(cr, ix + is * 0.5 - e.x_bearing - e.width * 0.5, iy + is * 0.5 + e.height * 0.42);
		cairo_show_text(cr, a->initial);
		cairo_restore(cr);
	}
	double tx = ix + is + rh * 0.30;
	double shelf_w = w * 0.20;
	double base = y + rh * 0.50;
	cairo_set_source_rgb(cr, 0.13, 0.11, 0.09);
	book_font_any(cr, a->name, rh * 0.36, true, false);
	double room = w - (tx - x) - shelf_w - rh * 0.4;
	double nw = book_text(cr, a->name, tx, base, room);
	if (strcmp(a->name, a->name_c) != 0 && nw < room - rh) {
		/* the name it is filed under, when it is shown by another */
		char c[160];
		snprintf(c, sizeof c, "  %s", a->name_c);
		cairo_set_source_rgba(cr, 0.40, 0.35, 0.29, 0.90);
		book_font_any(cr, c, rh * 0.26, false, true);
		nw += book_text(cr, c, tx + nw, base, room - nw);
	}
	if (a->generic[0]) {
		cairo_set_source_rgba(cr, 0.32, 0.28, 0.23, 0.95);
		book_font_any(cr, a->generic, rh * 0.27, false, true);
		book_text(cr, a->generic, tx, y + rh * 0.84, w - (tx - x) - shelf_w - rh * 0.4);
	}
	/* the leader, and where it is filed */
	const char *sh = shelf_label(&l->shelf[a->shelf]);
	book_font(cr, rh * 0.24, false, false);
	cairo_text_extents_t se;
	cairo_text_extents(cr, sh, &se);
	double sx = x + w - se.x_advance;
	cairo_set_source_rgba(cr, 0.45, 0.12, 0.09, 0.90);
	cairo_move_to(cr, sx, base);
	cairo_show_text(cr, sh);
	cairo_set_source_rgba(cr, 0.30, 0.26, 0.21, 0.55);
	double dot = rh * 0.16;
	for (double dx = tx + nw + rh * 0.3; dx < sx - rh * 0.3; dx += dot) {
		cairo_arc(cr, dx, base - rh * 0.02, fmax(rh * 0.018, 0.5), 0, 2 * M_PI);
		cairo_fill(cr);
	}
}

/* paper, a page of it: x0 to x1, the gutter on the side `gutter` (-1 left,
 * 1 right): the shade where it goes down into the spine, the light where it
 * curves up out of it */
static void book_paper(cairo_t *cr, const struct book_geom *g, double x0, double x1, int gutter)
{
	cairo_save(cr);
	cairo_rectangle(cr, x0, g->py0, x1 - x0, g->py1 - g->py0);
	cairo_clip(cr);
	/* the grain's corner on a whole pixel: a fraction off one, and every
	 * pixel of the page is resampled through the best filter there is,
	 * which was most of what drawing a page cost */
	double tx = x0, ty = g->py0;
	cairo_user_to_device(cr, &tx, &ty);
	tx = floor(tx + 0.5);
	ty = floor(ty + 0.5);
	cairo_device_to_user(cr, &tx, &ty);
	cairo_translate(cr, tx, ty);
	fill_tile(cr, paper_tile(), 1.0, 1.0);
	cairo_restore(cr);
	double gxx = gutter < 0 ? x0 : x1, dir = gutter < 0 ? 1.0 : -1.0;
	cairo_pattern_t *gr = cairo_pattern_create_linear(gxx, 0, gxx + dir * (x1 - x0) * 0.16, 0);
	cairo_pattern_add_color_stop_rgba(gr, 0.0, 0.10, 0.07, 0.04, 0.42);
	cairo_pattern_add_color_stop_rgba(gr, 0.35, 0.10, 0.07, 0.04, 0.10);
	cairo_pattern_add_color_stop_rgba(gr, 1.0, 0.10, 0.07, 0.04, 0.0);
	cairo_rectangle(cr, x0, g->py0, x1 - x0, g->py1 - g->py0);
	cairo_set_source(cr, gr);
	cairo_fill(cr);
	cairo_pattern_destroy(gr);
	/* the outer edge, lighter where the leaves curl a little up */
	double ox = gutter < 0 ? x1 : x0;
	cairo_pattern_t *eg = cairo_pattern_create_linear(ox, 0, ox - dir * (x1 - x0) * 0.06, 0);
	cairo_pattern_add_color_stop_rgba(eg, 0.0, 0.10, 0.07, 0.04, 0.12);
	cairo_pattern_add_color_stop_rgba(eg, 1.0, 0.10, 0.07, 0.04, 0.0);
	cairo_rectangle(cr, x0, g->py0, x1 - x0, g->py1 - g->py0);
	cairo_set_source(cr, eg);
	cairo_fill(cr);
	cairo_pattern_destroy(eg);
}

/* one page of spread `spread` (side 0 left, 1 right), drawn where it lies */
static void book_plate(cairo_t *cr, const struct book_geom *g, const struct desk_app *a,
	const struct desk_app_list *l, int pick, bool by_key);

static void book_page(cairo_t *cr, int w, int h, const struct book_geom *g,
	const struct desk_book_look *lk, int spread, int side)
{
	const struct book_index *bi = book_index(w, h, lk->query);
	const struct desk_app_list *l = desk_apps_acquire();
	double x0 = side ? g->gx : g->px0, x1 = side ? g->px1 : g->gx;
	book_paper(cr, g, x0, x1, side ? -1 : 1);
	bool empty = spread < 0 || spread >= bi->spreads;
	if (!side) {
		/* the head of the page: what this is, and the line to write on */
		double hx = g->px0 + g->bw * 0.040, hw = g->gx - g->bw * 0.034 - hx;
		cal_spaced(cr, TR("ALL APPS"), hx, g->py0 + g->bh * 0.068, g->bh * 0.040,
			g->bh * 0.010, true, 0.55, 0.11, 0.08);
		book_font(cr, g->bh * 0.022, false, true);
		cairo_set_source_rgba(cr, 0.32, 0.28, 0.23, 0.95);
		cairo_move_to(cr, hx, g->py0 + g->bh * 0.100);
		cairo_show_text(cr, TR("every program on this machine, A to Z"));
		double fr[4];
		book_find_rect(g, fr);
		/* the glass, and the rule to write on */
		double gr = fr[3] * 0.26, gcx = fr[0] + gr * 1.1, gcy = fr[1] + fr[3] * 0.50;
		cairo_set_source_rgba(cr, 0.30, 0.26, 0.21, 0.85);
		cairo_set_line_width(cr, fmax(gr * 0.22, 1.0));
		cairo_new_path(cr);
		cairo_arc(cr, gcx, gcy - gr * 0.15, gr * 0.72, 0, 2 * M_PI);
		cairo_stroke(cr);
		cairo_move_to(cr, gcx + gr * 0.50, gcy + gr * 0.36);
		cairo_line_to(cr, gcx + gr * 1.10, gcy + gr * 0.96);
		cairo_stroke(cr);
		double lx = fr[0] + gr * 2.8;
		cairo_set_line_width(cr, fmax(g->bh * 0.0018, 0.8));
		cairo_move_to(cr, lx, fr[1] + fr[3] * 0.80);
		cairo_line_to(cr, fr[0] + fr[2], fr[1] + fr[3] * 0.80);
		cairo_stroke(cr);
		/* what is written on it, the input method's part underlined */
		const char *q = lk->query ? lk->query : "", *pre = lk->preedit ? lk->preedit : "";
		int caret = lk->caret < 0 ? 0 : (lk->caret > (int)strlen(q) ? (int)strlen(q) : lk->caret);
		char shown[512];
		snprintf(shown, sizeof shown, "%.*s%s%s", caret, q, pre, q + caret);
		double ts = fr[3] * 0.52, base = fr[1] + fr[3] * 0.70;
		book_font(cr, ts, false, true);
		if (!shown[0]) {
			cairo_set_source_rgba(cr, 0.40, 0.36, 0.30, 0.55);
			cairo_move_to(cr, lx + ts * 0.3, base);
			cairo_show_text(cr, TR("write a name, or what it is for\xe2\x80\xa6"));
		}
		book_font_any(cr, shown, ts, false, false);
		if (shown[0]) {
			cairo_set_source_rgb(cr, 0.16, 0.15, 0.17);
			cairo_move_to(cr, lx + ts * 0.3, base);
			cairo_show_text(cr, shown);
		}
		char part[512];
		if (pre[0]) {
			snprintf(part, sizeof part, "%.*s", caret, q);
			cairo_text_extents_t a, b;
			cairo_text_extents(cr, part, &a);
			snprintf(part, sizeof part, "%.*s%s", caret, q, pre);
			cairo_text_extents(cr, part, &b);
			cairo_set_line_width(cr, fmax(ts * 0.06, 0.8));
			cairo_move_to(cr, lx + ts * 0.3 + a.x_advance, base + ts * 0.18);
			cairo_line_to(cr, lx + ts * 0.3 + b.x_advance, base + ts * 0.18);
			cairo_stroke(cr);
		}
		/* the caret */
		int cb = caret + (pre[0] ? (lk->preedit_caret < 0 ? 0 : lk->preedit_caret) : 0);
		snprintf(part, sizeof part, "%.*s", cb, shown);
		cairo_text_extents_t ce;
		cairo_text_extents(cr, part, &ce);
		cairo_rectangle(cr, lx + ts * 0.3 + (shown[0] ? ce.x_advance : 0.0), base - ts * 0.86,
			fmax(ts * 0.07, 1.0), ts * 1.06);
		cairo_set_source_rgb(cr, 0.10, 0.10, 0.12);
		cairo_fill(cr);
		/* how many there are of what it is the index of */
		char cnt[64];
		if (lk->query && lk->query[0]) {
			snprintf(cnt, sizeof cnt, TR("%d FOUND"), bi->n);
		} else {
			snprintf(cnt, sizeof cnt, TR("%d PROGRAMS"), bi->n);
		}
		cal_spaced(cr, cnt, hx, g->py0 + g->bh * 0.226, g->bh * 0.020, g->bh * 0.003, true,
			0.45, 0.40, 0.33);
		(void)hw;
	} else if (!empty) {
		/* the running head: the letters this spread runs from and to */
		int first = -1, last = -1;
		for (int i = 0; i < bi->n; i++) {
			if (bi->spread[i] == spread) {
				int lt = book_letter(&l->apps[bi->app[i]]);
				first = first < 0 ? lt : first;
				last = lt;
			}
		}
		if (first >= 0 && !(lk->query && lk->query[0])) {
			char run[32];
			if (first == last) {
				snprintf(run, sizeof run, "%s", BOOK_TAB_NAME[first]);
			} else {
				snprintf(run, sizeof run, "%s \xe2\x80\x93 %s", BOOK_TAB_NAME[first],
					BOOK_TAB_NAME[last]);
			}
			double rw = cal_spaced_w(cr, run, g->bh * 0.026, g->bh * 0.006, true);
			cal_spaced(cr, run, (g->gx + g->px1 - rw) * 0.5 - g->bw * 0.012,
				g->py0 + g->bh * 0.050, g->bh * 0.026, g->bh * 0.006, true, 0.55, 0.11, 0.08);
		}
	}
	if (!empty) {
		/* the initials */
		for (int k = 0; k < bi->nh; k++) {
			if (bi->h_spread[k] != spread || bi->h_side[k] != side) {
				continue;
			}
			double r[4];
			book_row_rect(g, side, bi->h_row[k], r);
			book_font(cr, r[3] * 0.62, true, false);
			cairo_set_source_rgb(cr, 0.55, 0.11, 0.08);
			cairo_move_to(cr, r[0], r[1] + r[3] * 0.78);
			cairo_show_text(cr, BOOK_TAB_NAME[bi->h_letter[k]]);
			cairo_set_source_rgba(cr, 0.55, 0.11, 0.08, 0.45);
			cairo_set_line_width(cr, fmax(r[3] * 0.02, 0.7));
			cairo_move_to(cr, r[0] + r[3] * 0.62, r[1] + r[3] * 0.56);
			cairo_line_to(cr, r[0] + r[2], r[1] + r[3] * 0.56);
			cairo_stroke(cr);
		}
		for (int i = 0; i < bi->n; i++) {
			if (bi->spread[i] != spread || bi->side[i] != side) {
				continue;
			}
			double r[4];
			book_row_rect(g, side, bi->row[i], r);
			book_entry(cr, g, r, &l->apps[bi->app[i]], l, lk->hover == i, lk->sel == i,
				lk->hover == i && lk->hover_flag);
		}
		if (side) {
			/* nothing of the index on this page: the one chosen, at large */
			bool any = false;
			for (int i = 0; i < bi->n && !any; i++) {
				any = bi->spread[i] == spread && bi->side[i] == 1;
			}
			int pick = lk->hover >= 0 ? lk->hover : lk->sel;
			if (!any && pick >= 0 && pick < bi->n && bi->spread[pick] == spread
				&& bi->app[pick] < l->n) {
				book_plate(cr, g, &l->apps[bi->app[pick]], l, pick, lk->hover < 0);
			}
		}
		if (bi->n == 0 && !side) {
			book_font(cr, g->bh * 0.030, false, true);
			cairo_set_source_rgba(cr, 0.35, 0.31, 0.26, 0.9);
			cairo_move_to(cr, g->px0 + g->bw * 0.040, g->l_top + g->row * 0.8);
			cairo_show_text(cr, TR("Nothing answers to that."));
			cairo_move_to(cr, g->px0 + g->bw * 0.040, g->l_top + g->row * 1.6);
			cairo_show_text(cr, TR("Fewer letters, or what it is for?"));
		}
	}
	/* the folio */
	char folio[16];
	snprintf(folio, sizeof folio, "%d", (spread < 0 ? 0 : spread) * 2 + side + 1);
	book_font(cr, g->bh * 0.024, false, false);
	cairo_text_extents_t fe;
	cairo_text_extents(cr, folio, &fe);
	cairo_set_source_rgba(cr, 0.40, 0.35, 0.29, 0.9);
	cairo_move_to(cr, side ? g->px1 - g->bw * 0.060 - fe.x_advance : g->px0 + g->bw * 0.040,
		g->py1 - g->bh * 0.030);
	cairo_show_text(cr, folio);
	desk_apps_release(l);
}

/* the words of s, centred on cx, broken to lines no wider than w; at most
 * nmax lines, the last cut short with an ellipsis */
static void book_wrap_centred(cairo_t *cr, const char *s, double cx, double y, double w,
	double lh, int nmax)
{
	char line[256] = "";
	const char *p = s;
	for (int ln = 0; ln < nmax && *p; ln++) {
		line[0] = '\0';
		size_t used = 0;
		while (*p) {
			const char *e = p;
			while (*e && *e != ' ') {
				e++;
			}
			char trial[256];
			int tw = snprintf(trial, sizeof trial, "%s%s%.*s", line, used ? " " : "",
				(int)(e - p), p);
			/* what does not fit in the buffer does not fit on the line */
			if (used && (tw < 0 || (size_t)tw >= sizeof trial)) {
				break;
			}
			cairo_text_extents_t te;
			cairo_text_extents(cr, trial, &te);
			if (used && te.x_advance > w) {
				break;
			}
			snprintf(line, sizeof line, "%s", trial);
			used++;
			p = *e ? e + 1 : e;
		}
		if (!used) {
			break;
		}
		if (ln == nmax - 1 && *p) {
			size_t n = strlen(line);
			if (n + 4 < sizeof line) {
				memcpy(line + n, "\xe2\x80\xa6", 4);
			}
		}
		cairo_text_extents_t te;
		cairo_text_extents(cr, line, &te);
		double lw = fmin(te.x_advance, w);
		book_text(cr, line, cx - lw * 0.5, y + lh * ln, w);
	}
}

/* The right-hand page, when the index has nothing on it: the one chosen in
 * it, at large -- its mark, its names, where it is filed and what else it
 * answers to, as a plate tipped in, ruled round in the red the index is
 * printed in. */
static void book_plate(cairo_t *cr, const struct book_geom *g, const struct desk_app *a,
	const struct desk_app_list *l, int pick, bool by_key)
{
	(void)pick;
	const double R = 0.55, G = 0.11, B = 0.08;
	double fx0 = g->gx + g->bw * 0.045, fx1 = g->px1 - g->bw * 0.060;
	double fy0 = g->py0 + g->bh * 0.090, fy1 = g->py1 - g->bh * 0.095;
	double cx = (fx0 + fx1) * 0.5, fw = fx1 - fx0;
	/* the rules: a heavy one and a hair inside it, a lozenge at each corner */
	cairo_save(cr);
	cairo_set_source_rgba(cr, R, G, B, 0.80);
	cairo_set_line_width(cr, fmax(g->bh * 0.0030, 1.0));
	cairo_rectangle(cr, fx0, fy0, fx1 - fx0, fy1 - fy0);
	cairo_stroke(cr);
	double in = g->bh * 0.010;
	cairo_set_line_width(cr, fmax(g->bh * 0.0012, 0.6));
	cairo_rectangle(cr, fx0 + in, fy0 + in, fx1 - fx0 - 2 * in, fy1 - fy0 - 2 * in);
	cairo_stroke(cr);
	for (int k = 0; k < 4; k++) {
		double x = (k & 1) ? fx1 : fx0, y = (k & 2) ? fy1 : fy0, d = g->bh * 0.011;
		cairo_move_to(cr, x, y - d);
		cairo_line_to(cr, x + d, y);
		cairo_line_to(cr, x, y + d);
		cairo_line_to(cr, x - d, y);
		cairo_close_path(cr);
		cairo_fill(cr);
	}
	/* its number in the binder it is filed in, as its sheet there has it */
	const struct desk_shelf_info *si = &l->shelf[a->shelf];
	char code[48], key[4] = { 0 };
	for (int i = 0; i < 3 && si->key[i]; i++) {
		key[i] = (char)toupper((unsigned char)si->key[i]);
	}
	snprintf(code, sizeof code, "%s \xc2\xb7 %03d", key, (int)(a - l->apps) - si->first + 1);
	double cs = g->bh * 0.019, ct = g->bh * 0.006;
	double cw = cal_spaced_w(cr, code, cs, ct, true);
	cal_spaced(cr, code, cx - cw * 0.5, fy0 + g->bh * 0.052, cs, ct, true, R, G, B);
	/* the mark, set down on the page with its shadow under it */
	double is = g->bh * 0.165, iy = fy0 + g->bh * 0.080;
	cairo_surface_t *ic = desk_app_icon(a);
	if (ic) {
		double iw = cairo_image_surface_get_width(ic), ih = cairo_image_surface_get_height(ic);
		double k = is / fmax(iw, ih);
		for (int sh = 2; sh >= 0; sh--) {
			cairo_save(cr);
			double o = is * (0.012 + 0.012 * sh);
			cairo_translate(cr, cx - iw * k * 0.5 + o * 0.6, iy + (is - ih * k) * 0.5 + o);
			cairo_scale(cr, k, k);
			if (sh) {
				cairo_set_source_rgba(cr, 0.16, 0.10, 0.04, 0.10);
				cairo_mask_surface(cr, ic, 0, 0);
			}
			cairo_restore(cr);
		}
		cairo_save(cr);
		cairo_translate(cr, cx - iw * k * 0.5, iy + (is - ih * k) * 0.5);
		cairo_scale(cr, k, k);
		cairo_set_source_surface(cr, ic, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		cairo_new_path(cr);
		cairo_arc(cr, cx, iy + is * 0.5, is * 0.44, 0, 2 * M_PI);
		cairo_set_source_rgba(cr, R, G, B, 0.85);
		cairo_set_line_width(cr, fmax(is * 0.035, 1.0));
		cairo_stroke(cr);
		book_font_any(cr, a->initial, is * 0.46, true, false);
		cairo_text_extents_t e;
		cairo_text_extents(cr, a->initial, &e);
		cairo_move_to(cr, cx - e.x_bearing - e.width * 0.5, iy + is * 0.5 + e.height * 0.42);
		cairo_show_text(cr, a->initial);
	}
	/* its names */
	double y = iy + is + g->bh * 0.070, tw = fw * 0.84;
	cairo_set_source_rgb(cr, 0.13, 0.11, 0.09);
	book_font_any(cr, a->name, g->bh * 0.046, true, false);
	cairo_text_extents_t e;
	cairo_text_extents(cr, a->name, &e);
	book_text(cr, a->name, cx - fmin(e.x_advance, tw) * 0.5, y, tw);
	if (strcmp(a->name, a->name_c) != 0) {
		y += g->bh * 0.036;
		cairo_set_source_rgba(cr, 0.40, 0.35, 0.29, 0.95);
		book_font_any(cr, a->name_c, g->bh * 0.024, false, true);
		cairo_text_extents(cr, a->name_c, &e);
		book_text(cr, a->name_c, cx - fmin(e.x_advance, tw) * 0.5, y, tw);
	}
	if (a->generic[0]) {
		y += g->bh * 0.038;
		cairo_set_source_rgba(cr, 0.30, 0.26, 0.21, 0.95);
		book_font_any(cr, a->generic, g->bh * 0.027, false, true);
		cairo_text_extents(cr, a->generic, &e);
		book_text(cr, a->generic, cx - fmin(e.x_advance, tw) * 0.5, y, tw);
	}
	/* a rule with a lozenge on it */
	y += g->bh * 0.036;
	cairo_set_source_rgba(cr, R, G, B, 0.65);
	cairo_set_line_width(cr, fmax(g->bh * 0.0012, 0.6));
	double rl = fw * 0.16, d = g->bh * 0.006;
	cairo_move_to(cr, cx - rl, y);
	cairo_line_to(cr, cx - d * 2.2, y);
	cairo_move_to(cr, cx + d * 2.2, y);
	cairo_line_to(cr, cx + rl, y);
	cairo_stroke(cr);
	cairo_move_to(cr, cx, y - d);
	cairo_line_to(cr, cx + d, y);
	cairo_line_to(cr, cx, y + d);
	cairo_line_to(cr, cx - d, y);
	cairo_close_path(cr);
	cairo_fill(cr);
	/* where it is filed */
	y += g->bh * 0.048;
	double ls = g->bh * 0.015, lt = g->bh * 0.005;
	double lw = cal_spaced_w(cr, TR("FILED IN"), ls, lt, true);
	cal_spaced(cr, TR("FILED IN"), cx - lw * 0.5, y, ls, lt, true, 0.45, 0.40, 0.33);
	y += g->bh * 0.034;
	char filed[96];
	snprintf(filed, sizeof filed, "%s", shelf_label(si));
	cairo_set_source_rgb(cr, R, G, B);
	book_font_any(cr, filed, g->bh * 0.026, false, false);
	cairo_text_extents(cr, filed, &e);
	book_text(cr, filed, cx - fmin(e.x_advance, tw) * 0.5, y, tw);
	/* and what else it answers to */
	if (a->keywords[0]) {
		/* a list with ';' between, as the spec has it, or ',' as some write it */
		char kw[320];
		size_t o = 0;
		bool sep = false;
		for (const char *p = a->keywords; *p && o + 3 < sizeof kw; p++) {
			if (*p == ';' || *p == ',') {
				sep = o > 0;
			} else if (*p == ' ' && (sep || o == 0)) {
				continue;
			} else {
				if (sep) {
					kw[o++] = ',';
					kw[o++] = ' ';
					sep = false;
				}
				kw[o++] = *p;
			}
		}
		kw[o] = '\0';
		y += g->bh * 0.052;
		lw = cal_spaced_w(cr, TR("ALSO FOUND BY"), ls, lt, true);
		cal_spaced(cr, TR("ALSO FOUND BY"), cx - lw * 0.5, y, ls, lt, true, 0.45, 0.40, 0.33);
		y += g->bh * 0.032;
		cairo_set_source_rgba(cr, 0.30, 0.26, 0.21, 0.95);
		book_font_any(cr, kw, g->bh * 0.022, false, true);
		book_wrap_centred(cr, kw, cx, y, tw, g->bh * 0.030, 2);
	}
	/* how to have it */
	const char *how = by_key ? TR("PRESS ENTER TO OPEN") : TR("CLICK ITS LINE TO OPEN");
	double hs = g->bh * 0.017, ht = g->bh * 0.006;
	double hw = cal_spaced_w(cr, how, hs, ht, true);
	cal_spaced(cr, how, cx - hw * 0.5, fy1 - g->bh * 0.036, hs, ht, true, R, G, B);
	cairo_restore(cr);
}

/* the thumb index, cut into the fore edge of the right-hand page */
static void book_tabs(cairo_t *cr, int w, int h, const struct book_geom *g,
	const struct desk_book_look *lk, int spread)
{
	const struct book_index *bi = book_index(w, h, lk->query);
	bool writing = lk->query && lk->query[0];
	/* the letter the spread open now begins at */
	int here = -1;
	for (int t = 0; t < DESK_BOOK_TABS; t++) {
		if (bi->tab_spread[t] >= 0 && bi->tab_spread[t] <= spread) {
			here = t;
		}
	}
	for (int t = 0; t < DESK_BOOK_TABS; t++) {
		double r[4];
		book_tab_rect(g, t, r);
		bool has = bi->tab_spread[t] >= 0;
		bool hot = lk->hover_tab == t && has && !writing;
		/* the scoop cut out of the leaves, and the dark tab in it */
		cairo_save(cr);
		double x = r[0] - (hot ? r[2] * 0.25 : 0.0);
		desk_rounded(cr, x, r[1], r[2] + g->bw * 0.012, r[3], r[3] * 0.30);
		double k = has && !writing ? (t == here ? 1.0 : 0.80) : 0.45;
		cairo_set_source_rgb(cr, 0.08 * k + 0.02, 0.10 * k + 0.02, 0.18 * k + 0.03);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
		cairo_set_line_width(cr, 0.6);
		cairo_stroke(cr);
		double ts = fmin(r[3] * 0.70, r[2] * 0.62);
		book_font(cr, ts, true, false);
		cairo_text_extents_t e;
		cairo_text_extents(cr, BOOK_TAB_NAME[t], &e);
		if (t == here && !writing) {
			cairo_set_source_rgb(cr, 0.98, 0.82, 0.42);
		} else {
			cairo_set_source_rgba(cr, 0.86, 0.78, 0.60, has && !writing ? 0.92 : 0.30);
		}
		cairo_move_to(cr, x + r[2] * 0.5 - e.x_bearing - e.width * 0.5,
			r[1] + r[3] * 0.5 + e.height * 0.5);
		cairo_show_text(cr, BOOK_TAB_NAME[t]);
		cairo_restore(cr);
	}
}

/* the leaves under each page, at its foot and its outer edge */
static void book_block(cairo_t *cr, const struct book_geom *g)
{
	for (int i = 6; i >= 1; i--) {
		double d = i * g->bh * 0.0032;
		double c = 0.86 - 0.025 * i;
		cairo_set_source_rgb(cr, c, c * 0.97, c * 0.90);
		cairo_rectangle(cr, g->px0 - d * 0.6, g->py0 + d, g->px1 - g->px0 + d * 1.2,
			g->py1 - g->py0);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.30, 0.26, 0.21, 0.25);
		cairo_set_line_width(cr, 0.5);
		cairo_move_to(cr, g->px0 - d * 0.6, g->py1 + d);
		cairo_line_to(cr, g->px1 + d * 0.6, g->py1 + d);
		cairo_stroke(cr);
	}
}

/* A leaf going over the spine, at angle th (0 lying on the right, pi on
 * the left): the right page of `from` on its front, the left page of the
 * next on its back, seen from over the middle of the book. */
/* Where the leaf going over stands at th (0 lying on the right page, pi on
 * the left), in the book's flat drawing: the quad it covers (q, its own
 * corners in order), the shadow it throws on the page under it (sh), and
 * whether what shows is its back. False while it is still edge on or flat. */
static bool book_leaf_geom(const struct book_geom *g, double th, double q[4][2],
	double sh[4][2], bool *back)
{
	double pw = g->px1 - g->gx, ph = g->py1 - g->py0;
	*back = th > M_PI * 0.5;
	/* the free edge, round over the spine and up towards you */
	double ex = g->gx, ey = g->py0 + ph * 0.5, f = g->bh * 2.4;
	double fx = g->gx + pw * cos(th), fz = -pw * sin(th);
	double kk = f / (f + fz);
	double fy0 = ey + (g->py0 - ey) * kk, fy1 = ey + (g->py1 - ey) * kk;
	double sx = ex + (fx - ex) * kk;
	if (!*back) {
		q[0][0] = g->gx; q[0][1] = g->py0;
		q[1][0] = sx;    q[1][1] = fy0;
		q[2][0] = sx;    q[2][1] = fy1;
		q[3][0] = g->gx; q[3][1] = g->py1;
	} else {
		q[0][0] = sx;    q[0][1] = fy0;
		q[1][0] = g->gx; q[1][1] = g->py0;
		q[2][0] = g->gx; q[2][1] = g->py1;
		q[3][0] = sx;    q[3][1] = fy1;
	}
	sh[0][0] = g->gx;                           sh[0][1] = g->py0;
	sh[1][0] = g->gx + (sx - g->gx) * 1.08;     sh[1][1] = g->py0 + ph * 0.02;
	sh[2][0] = g->gx + (sx - g->gx) * 1.08;     sh[2][1] = g->py1 + ph * 0.02;
	sh[3][0] = g->gx;                           sh[3][1] = g->py1;
	return fabs(sx - g->gx) > 1.0;
}

static uint64_t key_mix(uint64_t h, const void *p, size_t n);

/* The page on the leaf going over -- its front (the right page of spread
 * from) or its back (the left page of the next) -- printed at k pixels to
 * the unit, and kept: the same picture for every frame of the turn, where
 * it was a page of the index set again each frame. Two kept, the front and
 * the back; only the thread that draws the shelf comes here. */
static cairo_surface_t *book_leaf_page(int w, int h, const struct book_geom *g,
	const struct desk_book_look *lk, int from, bool back, double k)
{
	static struct {
		cairo_surface_t *s;
		uint64_t key;
	} kept[2];
	const struct desk_app_list *apps = desk_apps_acquire();
	unsigned gen = apps->generation;
	desk_apps_release(apps);
	uint64_t key = 14695981039346656037ull;
	const char *q = lk->query ? lk->query : "", *pre = lk->preedit ? lk->preedit : "";
	int lang = (int)desk_lang_get();
	key = (key ^ (uint64_t)from) * 1099511628211ull;
	key = (key ^ (uint64_t)back) * 1099511628211ull;
	key = key_mix(key, q, strlen(q));
	key = key_mix(key, pre, strlen(pre));
	int ints[9] = { w, h, (int)lround(k * 4096.0), lk->caret, lk->preedit_caret, lk->hover,
		lk->sel, (int)gen, lang };
	key = key_mix(key, ints, sizeof ints);
	key = key_mix(key, &lk->ime, sizeof lk->ime);
	int slot = back ? 1 : 0;
	if (kept[slot].s && kept[slot].key == key) {
		return kept[slot].s;
	}
	double pw = g->px1 - g->gx, ph = g->py1 - g->py0;
	int iw = (int)ceil(pw * k), ih = (int)ceil(ph * k);
	if (iw < 2 || ih < 2) {
		return NULL;
	}
	if (kept[slot].s) {
		cairo_surface_destroy(kept[slot].s);
	}
	kept[slot].s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
	kept[slot].key = key;
	cairo_t *c = cairo_create(kept[slot].s);
	cairo_scale(c, k, k);
	/* the page drawn where it would lie, moved to the picture's corner */
	if (back) {
		cairo_translate(c, -g->px0, -g->py0);
		book_page(c, w, h, g, lk, from + 1, 0);
	} else {
		cairo_translate(c, -g->gx, -g->py0);
		book_page(c, w, h, g, lk, from, 1);
	}
	cairo_destroy(c);
	cairo_surface_flush(kept[slot].s);
	return kept[slot].s;
}

static void book_leaf(cairo_t *cr, int w, int h, const struct book_geom *g,
	const struct desk_book_look *lk, int from, double th)
{
	double q[4][2], sh[4][2];
	bool back;
	if (!book_leaf_geom(g, th, q, sh, &back)) {
		return;
	}
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double k = fmax(hypot(kx, ky), 0.25);
	cairo_surface_t *img = book_leaf_page(w, h, g, lk, from, back, k);
	if (!img) {
		return;
	}
	int iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
	struct hmat map;
	if (!hmat_rect_to_quad(0.0, 0.0, iw / k, ih / k, q, &map)) {
		return;
	}
	/* the shadow it throws on the page under it */
	double s = sin(th);
	cairo_save(cr);
	cairo_move_to(cr, sh[0][0], sh[0][1]);
	for (int i = 1; i < 4; i++) {
		cairo_line_to(cr, sh[i][0], sh[i][1]);
	}
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.05, 0.03, 0.02, 0.20 * s);
	cairo_fill(cr);
	cairo_restore(cr);
	warp_paint(cr, img, 0.0, 0.0, k, &map, 1.0);
	/* in its own shade as it stands up off the page */
	cairo_save(cr);
	cairo_move_to(cr, q[0][0], q[0][1]);
	for (int i = 1; i < 4; i++) {
		cairo_line_to(cr, q[i][0], q[i][1]);
	}
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.06, 0.04, 0.02, 0.08 * s);
	cairo_fill(cr);
	cairo_restore(cr);
}

/* navy morocco: the leather, dyed */
static void book_leather(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_save(cr);
	leather_fill(cr, x, y, w, h, r);
	desk_rounded(cr, x, y, w, h, r);
	cairo_set_operator(cr, CAIRO_OPERATOR_HSL_COLOR);
	cairo_set_source_rgb(cr, 0.17, 0.21, 0.33);
	cairo_fill(cr);
	cairo_restore(cr);
}

/* The book open, flat: its shadow on the desk, the covers (navy morocco,
 * turned in), and the pages and all on them -- in layers, so the book
 * being opened can lay each where it is at the time. NOLEAF: the pages
 * without the leaf going over, which is laid on by itself (book_overlay). */
enum { BOOK_FLAT_SHADOW = 1, BOOK_FLAT_COVERS = 2, BOOK_FLAT_PAGES = 4, BOOK_FLAT_ALL = 7,
	BOOK_FLAT_NOLEAF = 8 };

/* the ribbon, down from the head of the spine and out over the foot */
static void book_ribbon(cairo_t *cr, const struct book_geom *g)
{
	cairo_save(cr);
	double rx = g->gx + g->bw * 0.012, rw = g->bw * 0.010;
	cairo_move_to(cr, rx, g->py0 - g->bh * 0.01);
	cairo_curve_to(cr, rx + rw * 0.8, g->py0 + g->bh * 0.4, rx - rw * 0.5, g->py1 - g->bh * 0.2,
		rx + rw * 2.0, g->py1 + g->bh * 0.045);
	cairo_line_to(cr, rx + rw * 2.6, g->py1 + g->bh * 0.030);
	cairo_line_to(cr, rx + rw * 3.0, g->py1 + g->bh * 0.050);
	cairo_curve_to(cr, rx + rw * 0.5, g->py1 - g->bh * 0.2, rx + rw * 1.8, g->py0 + g->bh * 0.4,
		rx + rw, g->py0 - g->bh * 0.01);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 0.58, 0.10, 0.10);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
	cairo_set_line_width(cr, 0.7);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* the input method's candidates, on a slip under the line to write on */
static void book_cands(cairo_t *cr, const struct book_geom *g, const struct desk_book_look *lk)
{
	if (!lk->cands || lk->ncand <= 0) {
		return;
	}
	double fr[4];
	book_find_rect(g, fr);
	int ps = lk->cand_page > 0 ? lk->cand_page : 9;
	int cur = clampf(lk->cand_cursor, 0, lk->ncand - 1);
	int first = cur / ps * ps, last = first + ps < lk->ncand ? first + ps : lk->ncand;
	double row = g->row * 0.62, sw = fr[2] * 0.55, sh = row * (last - first) + row * 0.3;
	double sx = fr[0] + fr[2] * 0.12, sy = fr[1] + fr[3] + row * 0.2;
	cairo_save(cr);
	cairo_rectangle(cr, sx + 2, sy + 3, sw, sh);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.20);
	cairo_fill(cr);
	cairo_rectangle(cr, sx, sy, sw, sh);
	cairo_set_source_rgb(cr, 0.99, 0.98, 0.96);
	cairo_fill(cr);
	book_font_any(cr, NULL, row * 0.62, false, false);
	for (int i = first; i < last; i++) {
		double ry = sy + row * 0.15 + row * (i - first);
		if (i == cur) {
			cairo_rectangle(cr, sx + 2, ry, sw - 4, row);
			cairo_set_source_rgba(cr, 0.99, 0.86, 0.40, 0.7);
			cairo_fill(cr);
		}
		cairo_set_source_rgb(cr, 0.15, 0.13, 0.11);
		cairo_move_to(cr, sx + row * 0.4, ry + row * 0.74);
		cairo_show_text(cr, lk->cands[i]);
	}
	cairo_restore(cr);
}

static void book_draw_flat_parts(cairo_t *cr, int w, int h, const struct desk_book_look *lk,
	int parts)
{
	struct book_geom g;
	book_geom(w, h, &g);
	const struct book_index *bi = book_index(w, h, lk->query);
	double page = clampf(lk->page, 0.0, fmax(bi->spreads - 1, 0));
	int n = (int)floor(page);
	double frac = page - n;
	if (parts & BOOK_FLAT_SHADOW) {
		for (int i = 0; i < 8; i++) {
			double s = g.bw * (0.004 + 0.004 * i);
			desk_rounded(cr, g.bx - s * 0.5 + g.bw * 0.006, g.by - s * 0.3 + g.bh * 0.020,
				g.bw + s, g.bh + s, g.bw * 0.012 + s);
			cairo_set_source_rgba(cr, 0, 0, 0, 0.05);
			cairo_fill(cr);
		}
	}
	if (parts & BOOK_FLAT_COVERS) {
		book_leather(cr, g.bx, g.by, g.bw, g.bh, g.bw * 0.012);
	}
	if (!(parts & BOOK_FLAT_PAGES)) {
		return;
	}
	book_block(cr, &g);
	/* the pages: the left of this spread, the right of the next if a leaf
	 * is going over, then the leaf */
	book_page(cr, w, h, &g, lk, n, 0);
	book_page(cr, w, h, &g, lk, frac > 0.0005 ? n + 1 : n, 1);
	/* the spine's crease */
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.10, 0.07, 0.04, 0.40);
	cairo_set_line_width(cr, fmax(g.bh * 0.002, 0.8));
	cairo_move_to(cr, g.gx, g.py0);
	cairo_line_to(cr, g.gx, g.py1);
	cairo_stroke(cr);
	cairo_restore(cr);
	book_tabs(cr, w, h, &g, lk, frac > 0.5 ? n + 1 : n);
	if (frac > 0.0005 && !(parts & BOOK_FLAT_NOLEAF)) {
		double t = frac * frac * (3.0 - 2.0 * frac);
		book_leaf(cr, w, h, &g, lk, n, M_PI * t);
	}
	book_ribbon(cr, &g);
	book_cands(cr, &g, lk);
}

/* ---- held up ---- */

#define BOOK_HELD_PITCH 0.12

static void book_turn3(int w, int h, struct turn3 *t)
{
	struct book_geom g;
	book_geom(w, h, &g);
	t->px = g.bx + g.bw * 0.5;
	t->py = g.by + g.bh * 0.5;
	t->yaw = 0.0;
	t->pitch = BOOK_HELD_PITCH;
	t->ex = w * 0.5;
	t->ey = h * 0.45;
	t->f = h * 1.6;
}

/* all that is drawn of it flat: the covers, their shadow, the ribbon */
static void book_flat_box(int w, int h, double box[4])
{
	struct book_geom g;
	book_geom(w, h, &g);
	double m = g.bw * 0.045;
	box[0] = g.bx - m;
	box[1] = g.by - m;
	box[2] = g.bw + m * 2.0;
	box[3] = g.bh + m * 2.0;
}

/* ---- the book, taken off the shelf and opened (op_draw) ---- */

/* The front board, outside: the same morocco, tooled in gold -- a double
 * rule round it with a flower in each corner, the title, what it is, the
 * glass, A to Z and how many. W by H. */
static void book_cover_face(cairo_t *cr, double W, double H, double r, int count)
{
	book_leather(cr, 0.0, 0.0, W, H, r);
	cairo_save(cr);
	/* blind: a line pressed in, near the edge */
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);
	cairo_set_line_width(cr, fmax(W * 0.003, 0.8));
	cairo_rectangle(cr, W * 0.040, W * 0.040, W - W * 0.080, H - W * 0.080);
	cairo_stroke(cr);
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
	const double m[2] = { W * 0.075, W * 0.100 }, lw[2] = { W * 0.007, W * 0.0028 };
	for (int i = 0; i < 2; i++) {
		cairo_new_path(cr);
		cairo_rectangle(cr, m[i], m[i], W - m[i] * 2.0, H - m[i] * 2.0);
		cairo_rectangle(cr, m[i] + lw[i], m[i] + lw[i], W - (m[i] + lw[i]) * 2.0,
			H - (m[i] + lw[i]) * 2.0);
		gold_fill(cr, 0.0, H);
	}
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
	double fi = W * 0.135;
	gold_fleuron(cr, fi, fi, W * 0.026);
	gold_fleuron(cr, W - fi, fi, W * 0.026);
	gold_fleuron(cr, fi, H - fi, W * 0.026);
	gold_fleuron(cr, W - fi, H - fi, W * 0.026);
	/* the title, and a rule under it with a lozenge on it */
	gold_text(cr, TR("ALL APPS"), W * 0.5, H * 0.300, H * 0.068, H * 0.014, true);
	double ry = H * 0.345, rh = H * 0.0022, d = H * 0.008;
	cairo_new_path(cr);
	cairo_rectangle(cr, W * 0.30, ry - rh, W * 0.20 - d * 2.0, rh * 2.0);
	cairo_rectangle(cr, W * 0.50 + d * 2.0, ry - rh, W * 0.20 - d * 2.0, rh * 2.0);
	cairo_move_to(cr, W * 0.5, ry - d);
	cairo_line_to(cr, W * 0.5 + d, ry);
	cairo_line_to(cr, W * 0.5, ry + d);
	cairo_line_to(cr, W * 0.5 - d, ry);
	cairo_close_path(cr);
	gold_fill(cr, ry - d, ry + d);
	gold_text(cr, TR("AN INDEX OF"), W * 0.5, H * 0.410, H * 0.028, H * 0.006, true);
	gold_text(cr, TR("EVERY PROGRAM"), W * 0.5, H * 0.448, H * 0.028, H * 0.006, true);
	/* the glass */
	double gx = W * 0.49, gy = H * 0.565, gr = W * 0.085;
	cairo_new_path(cr);
	cairo_arc(cr, gx, gy, gr, 0, 2 * M_PI);
	cairo_new_sub_path(cr);
	cairo_arc_negative(cr, gx, gy, gr * 0.74, 2 * M_PI, 0);
	gold_fill(cr, gy - gr, gy + gr);
	cairo_save(cr);
	cairo_translate(cr, gx, gy);
	cairo_rotate(cr, M_PI * 0.25);
	cairo_new_path(cr);
	desk_rounded(cr, gr * 0.98, -gr * 0.15, gr * 0.95, gr * 0.30, gr * 0.12);
	gold_fill(cr, -gr * 0.2, gr * 0.2);
	cairo_restore(cr);
	gold_text(cr, "A\xe2\x80\x93Z", W * 0.5, H * 0.760, H * 0.050, 0.0, true);
	char n[32];
	snprintf(n, sizeof n, TR("%d PROGRAMS"), count);
	gold_text(cr, n, W * 0.5, H * 0.836, H * 0.026, H * 0.006, true);
	cairo_restore(cr);
}

/* What it is drawn from, kept while the same book is being opened or put
 * back: the halves of the open book, the front board's tooling
 * and the spine's, in strips round the round of it. */
struct book_pics {
	uint64_t key;                     /* of the pages as they are */
	uint64_t key_fixed;               /* of all else: the size, how many there are */
	double k;
	struct book_pic left, board, pages;
	cairo_surface_t *cover;
	cairo_surface_t *spine[OP_SPINE_N];
};
static struct book_pics g_bpics;

/* the pictures of the pages, or (all) of everything */
static void book_pics_drop(bool all_of_it)
{
	struct book_pic *all[] = { &g_bpics.left, &g_bpics.pages, &g_bpics.board };
	for (size_t i = 0; i < (all_of_it ? 3u : 2u); i++) {
		if (all[i]->s) {
			cairo_surface_destroy(all[i]->s);
			all[i]->s = NULL;
		}
	}
	g_bpics.key = 0;
	if (!all_of_it) {
		return;
	}
	g_bpics.key_fixed = 0;
	if (g_bpics.cover) {
		cairo_surface_destroy(g_bpics.cover);
		g_bpics.cover = NULL;
	}
	for (int i = 0; i < OP_SPINE_N; i++) {
		if (g_bpics.spine[i]) {
			cairo_surface_destroy(g_bpics.spine[i]);
			g_bpics.spine[i] = NULL;
		}
	}
	g_bpics.key = 0;
}

struct book_pic_arg {
	int w, h, parts;
	const struct desk_book_look *lk;
};

static void book_pic_draw(cairo_t *cr, void *arg)
{
	const struct book_pic_arg *a = arg;
	book_draw_flat_parts(cr, a->w, a->h, a->lk, a->parts);
}

/* The layers parts of the flat book that lie in x0, y0, pw by ph, at k
 * pixels a unit, on the pixels the book held is drawn on (its picture
 * starts at the corner of book_flat_box). */
static struct book_pic book_pic(int w, int h, const struct desk_book_look *lk, double k,
	double x0, double y0, double pw, double ph, int parts)
{
	double box[4];
	book_flat_box(w, h, box);
	struct book_pic_arg a = { w, h, parts, lk };
	return op_pic(k, box[0], box[1], x0, y0, pw, ph, book_pic_draw, &a);
}

static int book_count(void)
{
	const struct desk_app_list *l = desk_apps_acquire();
	int count = 0;
	for (int i = 0; i < DESK_SHELF_COUNT; i++) {
		count += l->shelf[i].count;
	}
	desk_apps_release(l);
	return count;
}

/* the book as it is when it is shut: nothing under the pointer, no leaf
 * going over, nothing half written */
static void book_pics_look(const struct desk_book_look *lk, struct desk_book_look *lo)
{
	*lo = *lk;
	lo->page = lround(lk->page);
	lo->hover = -1;
	lo->hover_tab = -1;
	lo->preedit = "";
	lo->ncand = 0;
	lo->cands = NULL;
	lo->open_t = 1.0;
}

/* what the pictures are of: the book's size, the programs in it and how
 * many; and (lo) the pages it is open at */
static uint64_t book_pics_key(int w, int h, const struct desk_book_look *lo, double k,
	int count)
{
	const struct desk_app_list *l = desk_apps_acquire();
	unsigned gen = l->generation;
	desk_apps_release(l);
	uint64_t key = 14695981039346656037ull;
	int vals[5] = { w, h, (int)lround(k * 1000.0), (int)gen, count };
	for (size_t i = 0; i < sizeof vals; i++) {
		key = (key ^ ((const unsigned char *)vals)[i]) * 1099511628211ull;
	}
	if (!lo) {
		return key | 1;
	}
	key = (key ^ (unsigned)lround(lo->page)) * 1099511628211ull;
	for (const char *c = lo->query ? lo->query : ""; *c; c++) {
		key = (key ^ (unsigned char)*c) * 1099511628211ull;
	}
	key = (key ^ (unsigned)(lo->sel + 1)) * 1099511628211ull;
	return key | 1;
}

/* Print the pictures for the book as lk has it: all that are missing, or
 * (one) just the next of them. True when they are all there. */
static bool book_pics_step(int w, int h, const struct desk_book_look *lk, double k, bool one)
{
	struct desk_book_look lo;
	book_pics_look(lk, &lo);
	int count = book_count();
	uint64_t fixed = book_pics_key(w, h, NULL, k, count);
	uint64_t key = book_pics_key(w, h, &lo, k, count);
	if (g_bpics.key_fixed != fixed) {
		book_pics_drop(true);
		g_bpics.key_fixed = fixed;
		g_bpics.k = k;
	}
	if (g_bpics.key != key) {
		book_pics_drop(false);
		g_bpics.key = key;
	}
	struct lamp_px lamp_was = lamp_view(w, h);
	struct book_geom g;
	book_geom(w, h, &g);
	bool done = false;
	for (int step = 0; !done; step++) {
		if (!g_bpics.left.s) {
			g_bpics.left = book_pic(w, h, &lo, k, g.bx, g.by, g.gx - g.bx, g.bh,
				BOOK_FLAT_COVERS | BOOK_FLAT_PAGES);
		} else if (!g_bpics.board.s) {
			g_bpics.board = book_pic(w, h, &lo, k, g.gx, g.by, g.bx + g.bw - g.gx, g.bh,
				BOOK_FLAT_COVERS);
		} else if (!g_bpics.pages.s) {
			/* the pages, the tabs in their fore edge and the ribbon hanging
			 * out over the foot */
			g_bpics.pages = book_pic(w, h, &lo, k, g.gx, g.by, g.bx + g.bw - g.gx, g.bh * 1.04,
				BOOK_FLAT_PAGES);
		} else if (!g_bpics.cover) {
			/* the front board, outside */
			double cw = g.bw * 0.5;
			int iw = (int)ceil(cw * k), ih = (int)ceil(g.bh * k);
			g_bpics.cover = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
			cairo_t *c = cairo_create(g_bpics.cover);
			cairo_scale(c, k, k);
			book_cover_face(c, cw, g.bh, g.bw * 0.012, count);
			cairo_destroy(c);
			cairo_surface_flush(g_bpics.cover);
		} else if (!g_bpics.spine[0]) {
			/* the spine's tooling, in strips across it (its back on the left) */
			double ts = g.bh * (2.0 * RM_BK_HW) / RM_BK_H;
			double y1f = 1.0 - BK_SPINE_Y1 / RM_BK_H;
			double span = (BK_SPINE_Y1 - BK_SPINE_Y0) / RM_BK_H;
			double sw = ts / OP_SPINE_N;
			for (int i = 0; i < OP_SPINE_N; i++) {
				int sw_px = (int)ceil(sw * k), sh_px = (int)ceil(g.bh * k);
				g_bpics.spine[i] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw_px, sh_px);
				cairo_t *sc = cairo_create(g_bpics.spine[i]);
				cairo_scale(sc, k, k);
				cairo_translate(sc, -sw * i + ts * 0.05, g.bh * y1f);
				book_spine_face(sc, ts * 0.90, g.bh * span, count);
				cairo_destroy(sc);
				cairo_surface_flush(g_bpics.spine[i]);
			}
		} else {
			done = true;
			break;
		}
		if (one && step == 0) {
			break;
		}
	}
	g_light = lamp_was;
	return done || (g_bpics.left.s && g_bpics.board.s && g_bpics.pages.s && g_bpics.cover
		&& g_bpics.spine[0]);
}

static const struct book_pics *book_pics_for(int w, int h, const struct desk_book_look *lk,
	double k)
{
	book_pics_step(w, h, lk, k, false);
	return &g_bpics;
}

/* The pictures for the book as it is taken down, printed one a call ahead
 * of it: false once there is nothing left to print. */
bool desk_book_warm(int w, int h)
{
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	struct desk_book_look lk;
	memset(&lk, 0, sizeof lk);
	lk.query = "";
	lk.preedit = "";
	lk.sel = -1;
	return !book_pics_step(w, h, &lk, (double)ow / w, true);
}

/* Its shadow on the desk, as the flat book has it (book_draw_flat_parts),
 * laid where the book open will be: the rounded rectangles drawn straight
 * through the turn rather than as a picture warped, since a picture of a
 * shadow is a million pixels of almost nothing to blend. */
static void book_an_shadow(cairo_t *cr, const struct op_kind *K, const struct book_pose *P,
	double fade)
{
	double xs = P->WR / (K->bx + K->bw - K->gx), cy = K->by + K->bh * 0.5;
	double bx = K->bx, by = K->by, bw = K->bw, bh = K->bh;
	cairo_save(cr);
	for (int i = 0; i < 8; i++) {
		double sp = bw * (0.004 + 0.004 * i);
		double x = bx - sp * 0.5 + bw * 0.006, y = by - sp * 0.3 + bh * 0.020;
		double rw = bw + sp, rh = bh + sp, r = fmin(bw * 0.012 + sp, fmin(rw, rh) * 0.5);
		const double cx[4] = { x + rw - r, x + rw - r, x + r, x + r };
		const double cyy[4] = { y + r, y + rh - r, y + rh - r, y + r };
		cairo_new_path(cr);
		for (int c = 0; c < 4; c++) {
			for (int k = 0; k <= 6; k++) {
				double a = (c - 1) * M_PI * 0.5 + M_PI * 0.5 * k / 6.0;
				double fx = cx[c] + r * cos(a), fy = cyy[c] + r * sin(a);
				const double p[3] = { (fx - K->gx) * xs, fy - cy, 0.0 };
				double sx, sy;
				bp_screen(P, p, &sx, &sy);
				if (c == 0 && k == 0) {
					cairo_move_to(cr, sx, sy);
				} else {
					cairo_line_to(cr, sx, sy);
				}
			}
		}
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.05 * fade);
		cairo_fill(cr);
	}
	cairo_restore(cr);
}

/* The book as a thing to be taken down and opened: navy morocco and
 * gilt edges, and as thick and deep to its height as it stands on the
 * shelf; pics, if not NULL, what it is drawn from. */
static void book_kind(int w, int h, const struct book_pics *pics, struct op_kind *K)
{
	memset(K, 0, sizeof *K);
	struct book_geom g;
	book_geom(w, h, &g);
	K->bx = g.bx;
	K->by = g.by;
	K->bw = g.bw;
	K->bh = g.bh;
	K->gx = g.gx;
	book_turn3(w, h, &K->held);
	K->b = g.bw * 0.010;
	K->fore_in = g.bx + g.bw - g.px1;
	K->yt = g.py0 - (g.by + g.bh * 0.5);
	K->yb = g.py1 - (g.by + g.bh * 0.5);
	K->ts = g.bh * (2.0 * RM_BK_HW) / RM_BK_H;
	K->rb = K->ts * 0.20;
	K->W0 = g.bh * RM_BK_D / RM_BK_H;
	op_shelf_spine(w, h, RM_BK_X, RM_BK_HW, RM_BK_H, RM_BK_ZC + RM_BK_D * 0.5, &K->scx,
		&K->scy, &K->ssh);
	K->s1 = 0.80;
	K->leather[0] = 0.13;
	K->leather[1] = 0.16;
	K->leather[2] = 0.27;
	K->edge[0] = 0.86;
	K->edge[1] = 0.66;
	K->edge[2] = 0.30;
	K->lit_faces = true;
	K->shadow_fn = book_an_shadow;
	if (pics) {
		K->k = pics->k;
		K->left = &pics->left;
		K->board = &pics->board;
		K->pages = &pics->pages;
		K->cover = pics->cover;
		K->spine = pics->spine;
	}
}

static void book_anim_draw(cairo_t *cr, int w, int h, const struct desk_book_look *lk,
	double u)
{
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double k = fmax(hypot(kx, ky), 0.25);
	const struct book_pics *pc = book_pics_for(w, h, lk, k);
	struct op_kind K;
	book_kind(w, h, pc, &K);
	op_draw(cr, &K, u);
}

/* The board's edges, round the book held open. */
static void book_board_edges(cairo_t *cr, int w, int h)
{
	struct op_kind K;
	book_kind(w, h, NULL, &K);
	op_board_edges(cr, &K);
}

/* The book's shadow and its covers, flat, over box at k pixels to the
 * unit: they do not change as it is written in and turned, so kept, and
 * only the pages drawn again over them. */
static cairo_surface_t *book_base(int w, int h, const struct desk_book_look *lk,
	const double box[4], double k, int iw, int ih)
{
	static cairo_surface_t *base;
	static int base_w, base_h, base_iw, base_ih;
	static double base_k;
	if (!base || base_w != w || base_h != h || base_k != k || base_iw != iw || base_ih != ih) {
		if (base) {
			cairo_surface_destroy(base);
		}
		base = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
		cairo_t *bc = cairo_create(base);
		cairo_scale(bc, k, k);
		cairo_translate(bc, -box[0], -box[1]);
		book_draw_flat_parts(bc, w, h, lk, BOOK_FLAT_SHADOW | BOOK_FLAT_COVERS);
		cairo_destroy(bc);
		cairo_surface_flush(base);
		base_w = w;
		base_h = h;
		base_k = k;
		base_iw = iw;
		base_ih = ih;
	}
	return base;
}

/* The book held open: the spread, with the leaf going over in it -- or,
 * leaf false, without it, the leaf being laid on over it by itself
 * (desk_draw_book_leaf). */
static void book_draw(cairo_t *cr, int w, int h, const struct desk_book_look *lk, bool leaf)
{
	double e = clampf(lk->open_t, 0.0, 1.0);
	if (e <= 0.002) {
		return;
	}
	struct lamp_px lamp_was = lamp_view(w, h);
	if (e < 0.999) {
		/* coming off the shelf and opening, or going back */
		book_anim_draw(cr, w, h, lk, e);
		g_light = lamp_was;
		return;
	}
	double box[4];
	book_flat_box(w, h, box);
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double k = fmax(hypot(kx, ky), 0.25);
	int iw = (int)ceil(box[2] * k), ih = (int)ceil(box[3] * k);
	if (iw >= 2 && ih >= 2) {
		cairo_surface_t *base = book_base(w, h, lk, box, k, iw, ih);
		cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
		cairo_t *c = cairo_create(img);
		cairo_set_source_surface(c, base, 0, 0);
		cairo_set_operator(c, CAIRO_OPERATOR_SOURCE);
		cairo_paint(c);
		cairo_set_operator(c, CAIRO_OPERATOR_OVER);
		cairo_scale(c, k, k);
		cairo_translate(c, -box[0], -box[1]);
		book_draw_flat_parts(c, w, h, lk, BOOK_FLAT_PAGES | (leaf ? 0 : BOOK_FLAT_NOLEAF));
		cairo_destroy(c);
		struct turn3 t;
		book_turn3(w, h, &t);
		struct book_geom g;
		book_geom(w, h, &g);
		book_board_edges(cr, w, h);
		struct hmat map;
		if (turn3_hmat(&t, box[0], box[1], box[2], box[3], &map)) {
			warp_paint(cr, img, box[0], box[1], k, &map, 1.0);
		}
		held_face_light(cr, &t, g.bx, g.by, g.bw, g.bh, 0.06);
		cairo_surface_destroy(img);
	}
	g_light = lamp_was;
}

void desk_draw_book(cairo_t *cr, int w, int h, const struct desk_book_look *lk)
{
	book_draw(cr, w, h, lk, true);
}

void desk_draw_book_still(cairo_t *cr, int w, int h, const struct desk_book_look *lk)
{
	book_draw(cr, w, h, lk, false);
}

/* The leaf going over in the book held open: its page (n, the spread it
 * is going over from) and how far round it has come (th, 0 to pi); false
 * when none is. */
static bool book_turning(int w, int h, const struct desk_book_look *lk, struct book_geom *g,
	int *n, double *th)
{
	if (clampf(lk->open_t, 0.0, 1.0) < 0.999) {
		return false;
	}
	book_geom(w, h, g);
	const struct book_index *bi = book_index(w, h, lk->query);
	double page = clampf(lk->page, 0.0, fmax(bi->spreads - 1, 0));
	*n = (int)floor(page);
	double frac = page - *n;
	if (frac <= 0.0005) {
		return false;
	}
	*th = M_PI * frac * frac * (3.0 - 2.0 * frac);
	return true;
}

/* what of the book's flat drawing the leaf covers going over, with its
 * shadow on the page under it, and the margin their soft edges want */
static bool book_overlay_rect(const struct book_geom *g, double th, double r[4])
{
	double q[4][2], sh[4][2];
	bool back;
	if (!book_leaf_geom(g, th, q, sh, &back)) {
		return false;
	}
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 4; i++) {
		x0 = fmin(x0, fmin(q[i][0], sh[i][0]));
		y0 = fmin(y0, fmin(q[i][1], sh[i][1]));
		x1 = fmax(x1, fmax(q[i][0], sh[i][0]));
		y1 = fmax(y1, fmax(q[i][1], sh[i][1]));
	}
	r[0] = floor(x0) - 3.0;
	r[1] = floor(y0) - 3.0;
	r[2] = ceil(x1 - x0) + 6.0;
	r[3] = ceil(y1 - y0) + 6.0;
	return true;
}

/* The leaf going over, by itself, over the book drawn still: what of the
 * flat drawing it changes -- its shadow on the page under it, the leaf, its
 * shade -- and what lies on it there (the ribbon, the candidates' slip),
 * drawn flat onto clear, laid into the view by the same turn as the rest,
 * and the light on the face over the leaf. Turning a leaf drew both pages
 * of the index and laid the whole book into the view a pixel at a time on
 * every frame: 45 ms a frame. */
void desk_draw_book_leaf(cairo_t *cr, int w, int h, const struct desk_book_look *lk)
{
	struct book_geom g;
	int n;
	double th;
	if (!book_turning(w, h, lk, &g, &n, &th)) {
		return;
	}
	double q[4][2], sh[4][2];
	bool back;
	if (!book_leaf_geom(&g, th, q, sh, &back)) {
		return;
	}
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double k = fmax(hypot(kx, ky), 0.25);
	struct turn3 t;
	book_turn3(w, h, &t);
	double box[4];
	book_flat_box(w, h, box);
	struct hmat held;
	if (!turn3_hmat(&t, box[0], box[1], box[2], box[3], &held)) {
		return;
	}
	struct lamp_px lamp_was = lamp_view(w, h);
	/* the leaf's corners, its shadow's, on the screen */
	double sq[4][2], ss[4][2];
	for (int i = 0; i < 4; i++) {
		turn3_point(&t, q[i][0], q[i][1], &sq[i][0], &sq[i][1], NULL);
		turn3_point(&t, sh[i][0], sh[i][1], &ss[i][0], &ss[i][1], NULL);
	}
	double s = sin(th);
	/* the shadow it throws on the page under it */
	cairo_save(cr);
	cairo_move_to(cr, ss[0][0], ss[0][1]);
	for (int i = 1; i < 4; i++) {
		cairo_line_to(cr, ss[i][0], ss[i][1]);
	}
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.05, 0.03, 0.02, 0.20 * s);
	cairo_fill(cr);
	cairo_restore(cr);
	/* the leaf, its page laid straight onto the screen: the turn of the leaf
	 * and the turn of the book one map, so the page is resampled once */
	cairo_surface_t *img = book_leaf_page(w, h, &g, lk, n, back, k);
	struct hmat onto, both;
	if (img && hmat_rect_to_quad(0.0, 0.0, cairo_image_surface_get_width(img) / k,
			cairo_image_surface_get_height(img) / k, q, &onto)) {
		hmat_then(&onto, &held, &both);
		warp_paint(cr, img, 0.0, 0.0, k, &both, 1.0);
	}
	/* in its own shade as it stands up off the page */
	cairo_save(cr);
	cairo_move_to(cr, sq[0][0], sq[0][1]);
	for (int i = 1; i < 4; i++) {
		cairo_line_to(cr, sq[i][0], sq[i][1]);
	}
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.06, 0.04, 0.02, 0.08 * s);
	cairo_fill(cr);
	cairo_restore(cr);
	/* What lies on it there, drawn after it: the ribbon down the spine, and
	 * the candidates' slip -- flat, where they cross the leaf, and laid in
	 * by the book's turn. */
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 4; i++) {
		x0 = fmin(x0, q[i][0]);
		y0 = fmin(y0, q[i][1]);
		x1 = fmax(x1, q[i][0]);
		y1 = fmax(y1, q[i][1]);
	}
	double rib0 = g.gx, rib1 = g.gx + g.bw * 0.045;
	bool cands = lk->cands && lk->ncand > 0;
	if (!cands) {
		x0 = fmax(x0, rib0);
		x1 = fmin(x1, rib1);
	}
	double r[4] = { floor(x0) - 2.0, floor(y0) - 2.0, ceil(x1 - x0) + 4.0, ceil(y1 - y0) + 4.0 };
	int iw = (int)ceil(r[2] * k), ih = (int)ceil(r[3] * k);
	if (x1 > x0 && iw >= 2 && ih >= 2) {
		cairo_surface_t *top = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
		cairo_t *c = cairo_create(top);
		cairo_scale(c, k, k);
		cairo_translate(c, -r[0], -r[1]);
		cairo_move_to(c, q[0][0], q[0][1]);
		for (int i = 1; i < 4; i++) {
			cairo_line_to(c, q[i][0], q[i][1]);
		}
		cairo_close_path(c);
		cairo_clip(c);
		book_ribbon(c, &g);
		book_cands(c, &g, lk);
		cairo_destroy(c);
		warp_paint(cr, top, r[0], r[1], k, &held, 1.0);
		cairo_surface_destroy(top);
	}
	/* and the lamps' light on the face, over the leaf and what is on it
	 * (the book under it has its own) */
	cairo_save(cr);
	cairo_move_to(cr, sq[0][0], sq[0][1]);
	for (int i = 1; i < 4; i++) {
		cairo_line_to(cr, sq[i][0], sq[i][1]);
	}
	cairo_close_path(cr);
	cairo_clip(cr);
	held_face_light(cr, &t, g.bx, g.by, g.bw, g.bh, 0.06);
	cairo_restore(cr);
	g_light = lamp_was;
}

bool desk_book_leaf_box(int w, int h, const struct desk_book_look *lk, double out[4])
{
	struct book_geom g;
	int n;
	double th, r[4];
	if (!book_turning(w, h, lk, &g, &n, &th) || !book_overlay_rect(&g, th, r)) {
		return false;
	}
	struct turn3 t;
	book_turn3(w, h, &t);
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 4; i++) {
		double sx, sy;
		turn3_point(&t, (i & 1) ? r[0] + r[2] : r[0], (i & 2) ? r[1] + r[3] : r[1], &sx, &sy,
			NULL);
		x0 = fmin(x0, sx);
		y0 = fmin(y0, sy);
		x1 = fmax(x1, sx);
		y1 = fmax(y1, sy);
	}
	out[0] = fmax(floor(x0) - 2.0, 0.0);
	out[1] = fmax(floor(y0) - 2.0, 0.0);
	out[2] = fmin(ceil(x1) + 2.0, (double)w) - out[0];
	out[3] = fmin(ceil(y1) + 2.0, (double)h) - out[1];
	return out[2] > 0.0 && out[3] > 0.0;
}

/* What the hand on the book changes, in the book's flat drawing: part 0
 * the line of the index it is on, gone over with the highlighter, or the
 * tab of the thumb index it is on, drawn out; part 1 the plate that line
 * puts on a right-hand page with nothing of the index on it. False when
 * the hand changes nothing there. */
static bool book_hot_rect(int w, int h, const struct desk_book_look *lk, int part, double r[4])
{
	if (clampf(lk->open_t, 0.0, 1.0) < 0.999) {
		return false;
	}
	struct book_geom g;
	book_geom(w, h, &g);
	const struct book_index *bi = book_index(w, h, lk->query);
	double page = clampf(lk->page, 0.0, fmax(bi->spreads - 1, 0));
	int left = (int)floor(page), right = page - left > 0.0005 ? left + 1 : left;
	int i = lk->hover;
	bool line = i >= 0 && i < bi->n && bi->spread[i] == (bi->side[i] ? right : left);
	/* the soft edges, and a pixel or two over */
	double pad = 2.0;
	if (part == 0 && line) {
		double rr[4];
		book_row_rect(&g, bi->side[i], bi->row[i], rr);
		double rh = rr[3];
		/* the highlighter's stroke (book_entry), and the flag in the margin */
		r[0] = rr[0] - rh * 0.80 - pad;
		r[1] = rr[1] + rh * 0.10 - pad;
		r[2] = rr[2] + rh * 0.95 + pad * 2.0;
		r[3] = rh * 0.82 + pad * 2.0;
		return true;
	}
	if (part == 0) {
		int t = lk->hover_tab;
		if (t < 0 || t >= DESK_BOOK_TABS || bi->tab_spread[t] < 0 || (lk->query && lk->query[0])) {
			return false;
		}
		double tr[4];
		book_tab_rect(&g, t, tr);
		/* where it lies, and drawn out a quarter of its width (book_tabs) */
		r[0] = tr[0] - tr[2] * 0.25 - pad;
		r[1] = tr[1] - pad;
		r[2] = tr[2] * 1.25 + g.bw * 0.012 + pad * 2.0;
		r[3] = tr[3] + pad * 2.0;
		return true;
	}
	if (part != 1 || !line || bi->spread[i] != right) {
		return false;
	}
	for (int j = 0; j < bi->n; j++) {
		if (bi->spread[j] == right && bi->side[j] == 1) {
			return false;
		}
	}
	/* the plate's frame (book_plate), its lozenges and its heavy rule */
	double fx0 = g.gx + g.bw * 0.045, fx1 = g.px1 - g.bw * 0.060;
	double fy0 = g.py0 + g.bh * 0.090, fy1 = g.py1 - g.bh * 0.095;
	double m = g.bh * 0.016 + pad;
	r[0] = fx0 - m;
	r[1] = fy0 - m;
	r[2] = fx1 - fx0 + m * 2.0;
	r[3] = fy1 - fy0 + m * 2.0;
	return true;
}

/* a rectangle of the book's flat drawing, as it lands in the view */
static void book_view_bounds(const struct turn3 *t, const double r[4], double out[4])
{
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 4; i++) {
		double sx, sy;
		turn3_point(t, (i & 1) ? r[0] + r[2] : r[0], (i & 2) ? r[1] + r[3] : r[1], &sx, &sy, NULL);
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

bool desk_book_hot_box(int w, int h, const struct desk_book_look *lk, int part, double out[4])
{
	double r[4], v[4];
	if (!book_hot_rect(w, h, lk, part, r)) {
		return false;
	}
	struct turn3 t;
	book_turn3(w, h, &t);
	book_view_bounds(&t, r, v);
	out[0] = fmax(floor(v[0]) - 2.0, 0.0);
	out[1] = fmax(floor(v[1]) - 2.0, 0.0);
	out[2] = fmin(ceil(v[0] + v[2]) + 2.0, (double)w) - out[0];
	out[3] = fmin(ceil(v[1] + v[3]) + 2.0, (double)h) - out[1];
	return out[2] > 0.0 && out[3] > 0.0;
}

/* The patch: the book's flat drawing over that rectangle and a few pixels
 * round it, on the still's own pixels -- its covers from the same kept
 * picture, its pages drawn again as lk has them -- laid into the view by
 * the same turn as the still, with the lamps' light over it as the still
 * has, and cut to the rectangle. Where nothing changed it is the still to
 * the pixel, so it lies on it without a seam. Going over the index drew
 * the whole book again for every line the pointer crossed, 25 ms and more
 * each; a line of it is a few.
 *
 * Drawn into a picture the size of the still's, clipped to the patch, and
 * not into one the size of the patch: cairo puts a word where its start
 * rounds to on a 256th of a pixel, then to the nearest quarter, and a
 * word that starts on the boundary goes either way with the last bit of
 * the arithmetic -- which moving the origin changes. The same transform
 * as the still's sets every word where the still has it. The picture's
 * memory is only touched where the clip lets it be. */
void desk_draw_book_hot(cairo_t *cr, int w, int h, const struct desk_book_look *lk, int part)
{
	double r[4];
	if (!book_hot_rect(w, h, lk, part, r)) {
		return;
	}
	double box[4];
	book_flat_box(w, h, box);
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double k = fmax(hypot(kx, ky), 0.25);
	int iw = (int)ceil(box[2] * k), ih = (int)ceil(box[3] * k);
	/* the still's pixels it covers, and three more each way for the turn
	 * to read from */
	int i0 = (int)floor((r[0] - box[0]) * k) - 3, j0 = (int)floor((r[1] - box[1]) * k) - 3;
	int i1 = (int)ceil((r[0] + r[2] - box[0]) * k) + 3;
	int j1 = (int)ceil((r[1] + r[3] - box[1]) * k) + 3;
	i0 = i0 < 0 ? 0 : i0;
	j0 = j0 < 0 ? 0 : j0;
	i1 = i1 > iw ? iw : i1;
	j1 = j1 > ih ? ih : j1;
	if (iw < 2 || ih < 2 || i1 - i0 < 2 || j1 - j0 < 2) {
		return;
	}
	struct turn3 t;
	book_turn3(w, h, &t);
	struct hmat map;
	if (!turn3_hmat(&t, box[0], box[1], box[2], box[3], &map)) {
		return;
	}
	struct lamp_px lamp_was = lamp_view(w, h);
	cairo_surface_t *base = book_base(w, h, lk, box, k, iw, ih);
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
	cairo_t *c = cairo_create(img);
	cairo_rectangle(c, i0, j0, i1 - i0, j1 - j0);
	cairo_clip(c);
	cairo_set_source_surface(c, base, 0, 0);
	cairo_set_operator(c, CAIRO_OPERATOR_SOURCE);
	cairo_paint(c);
	cairo_set_operator(c, CAIRO_OPERATOR_OVER);
	cairo_scale(c, k, k);
	cairo_translate(c, -box[0], -box[1]);
	book_draw_flat_parts(c, w, h, lk, BOOK_FLAT_PAGES | BOOK_FLAT_NOLEAF);
	cairo_destroy(c);
	/* all of the view the patch can reach: the turn writes straight to the
	 * pixels, and the cut below clears the rest of it */
	double pr[4] = { box[0] + i0 / k, box[1] + j0 / k, (i1 - i0) / k, (j1 - j0) / k }, v[4];
	book_view_bounds(&t, pr, v);
	cairo_save(cr);
	cairo_rectangle(cr, floor(v[0]) - 3.0, floor(v[1]) - 3.0, ceil(v[2]) + 7.0, ceil(v[3]) + 7.0);
	cairo_clip(cr);
	warp_paint(cr, img, box[0], box[1], k, &map, 1.0);
	struct book_geom g;
	book_geom(w, h, &g);
	held_face_light(cr, &t, g.bx, g.by, g.bw, g.bh, 0.06);
	cairo_set_operator(cr, CAIRO_OPERATOR_DEST_IN);
	for (int i = 0; i < 4; i++) {
		static const int ord[4] = { 0, 1, 3, 2 };
		double sx, sy;
		turn3_point(&t, (ord[i] & 1) ? r[0] + r[2] : r[0], (ord[i] & 2) ? r[1] + r[3] : r[1], &sx,
			&sy, NULL);
		if (i) {
			cairo_line_to(cr, sx, sy);
		} else {
			cairo_move_to(cr, sx, sy);
		}
	}
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_surface_destroy(img);
	g_light = lamp_was;
}

void desk_book_box(int w, int h, const struct desk_book_look *lk, double out[4])
{
	if (lk && lk->open_t < 0.999) {
		/* on its way from the shelf, or back */
		out[0] = 0.0;
		out[1] = 0.0;
		out[2] = w;
		out[3] = h;
		return;
	}
	double box[4];
	book_flat_box(w, h, box);
	struct turn3 t;
	book_turn3(w, h, &t);
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 8; i++) {
		double x = (i & 1) ? box[0] + box[2] : box[0], y = (i & 2) ? box[1] + box[3] : box[1];
		double sx, sy;
		/* and a leaf standing up over the spine, towards you */
		turn3_project(&t, x, y, (i & 4) ? -box[2] * 0.25 : 0.02 * box[2], &sx, &sy, NULL);
		x0 = fmin(x0, sx);
		y0 = fmin(y0, sy);
		x1 = fmax(x1, sx);
		y1 = fmax(y1, sy);
	}
	out[0] = fmax(floor(x0) - 2.0, 0.0);
	out[1] = fmax(floor(y0) - 2.0, 0.0);
	out[2] = fmin(ceil(x1) + 2.0, (double)w) - out[0];
	out[3] = fmin(ceil(y1) + 2.0, (double)h) - out[1];
}

static void book_unturn(int w, int h, double *x, double *y)
{
	struct turn3 t;
	book_turn3(w, h, &t);
	struct book_geom g;
	book_geom(w, h, &g);
	struct hmat map, inv;
	if (turn3_hmat(&t, g.bx, g.by, g.bw, g.bh, &map) && hmat_invert(&map, &inv)) {
		hmat_apply(&inv, *x, *y, x, y);
	}
}

int desk_book_hit(int w, int h, const struct desk_book_look *lk, double x, double y,
	int *arg)
{
	*arg = -1;
	book_unturn(w, h, &x, &y);
	struct book_geom g;
	book_geom(w, h, &g);
	if (x < g.bx || x > g.bx + g.bw || y < g.by || y > g.by + g.bh) {
		return DESK_BOOK_HIT_NONE;
	}
	const struct book_index *bi = book_index(w, h, lk->query);
	int spread = (int)lround(clampf(lk->page, 0.0, fmax(bi->spreads - 1, 0)));
	for (int t = 0; t < DESK_BOOK_TABS; t++) {
		double r[4];
		book_tab_rect(&g, t, r);
		/* the slips between the tabs count to the nearer one: a finger
		 * that comes down on one has meant a tab all the same */
		double slip = (g.tab_h - r[3]) * 0.5;
		if (x >= r[0] - g.bw * 0.004 && x <= r[0] + r[2] + g.bw * 0.012 && y >= r[1] - slip
			&& y <= r[1] + r[3] + slip) {
			*arg = t;
			return DESK_BOOK_HIT_TAB;
		}
	}
	double fr[4];
	book_find_rect(&g, fr);
	if (x >= fr[0] && x <= fr[0] + fr[2] && y >= fr[1] && y <= fr[1] + fr[3]) {
		return DESK_BOOK_HIT_FIND;
	}
	for (int i = 0; i < bi->n; i++) {
		if (bi->spread[i] != spread) {
			continue;
		}
		double r[4], f[4];
		book_row_rect(&g, bi->side[i], bi->row[i], r);
		book_flag_rect(r, f);
		if (x >= f[0] - 3.0 && x < r[0] - r[3] * 0.08 && y >= r[1] && y <= r[1] + r[3]) {
			*arg = i;
			return DESK_BOOK_HIT_FLAG;
		}
		if (x >= r[0] && x <= r[0] + r[2] && y >= r[1] && y <= r[1] + r[3]) {
			*arg = i;
			return DESK_BOOK_HIT_ENTRY;
		}
	}
	double cz = g.bw * 0.07;
	if (y > g.py1 - cz) {
		if (x < g.px0 + cz) {
			return DESK_BOOK_HIT_PREV;
		}
		if (x > g.px1 - cz) {
			return DESK_BOOK_HIT_NEXT;
		}
	}
	return DESK_BOOK_HIT_BOOK;
}

bool desk_book_entry_pose(int w, int h, const struct desk_book_look *lk, int i,
	double *cx, double *cy, double *ew, double *eh, double *angle)
{
	const struct book_index *bi = book_index(w, h, lk->query);
	if (i < 0 || i >= bi->n) {
		return false;
	}
	struct book_geom g;
	book_geom(w, h, &g);
	double r[4];
	book_row_rect(&g, bi->side[i], bi->row[i], r);
	struct turn3 t;
	book_turn3(w, h, &t);
	double q[4][2];
	for (int k = 0; k < 4; k++) {
		turn3_point(&t, r[0] + ((k == 1 || k == 2) ? r[2] : 0.0), r[1] + (k >= 2 ? r[3] : 0.0),
			&q[k][0], &q[k][1], NULL);
	}
	*cx = (q[0][0] + q[1][0] + q[2][0] + q[3][0]) * 0.25;
	*cy = (q[0][1] + q[1][1] + q[2][1] + q[3][1]) * 0.25;
	*ew = hypot(q[1][0] - q[0][0], q[1][1] - q[0][1]);
	*eh = hypot(q[3][0] - q[0][0], q[3][1] - q[0][1]);
	*angle = atan2(q[1][1] - q[0][1], q[1][0] - q[0][0]);
	return bi->spread[i] == (int)lround(lk->page);
}

bool desk_book_find_box(int w, int h, const struct desk_book_look *lk, double box[4])
{
	(void)lk;
	struct book_geom g;
	book_geom(w, h, &g);
	double fr[4];
	book_find_rect(&g, fr);
	struct turn3 t;
	book_turn3(w, h, &t);
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int k = 0; k < 4; k++) {
		double sx, sy;
		turn3_point(&t, fr[0] + ((k & 1) ? fr[2] : 0.0), fr[1] + ((k & 2) ? fr[3] : 0.0), &sx,
			&sy, NULL);
		x0 = fmin(x0, sx);
		y0 = fmin(y0, sy);
		x1 = fmax(x1, sx);
		y1 = fmax(y1, sy);
	}
	box[0] = x0;
	box[1] = y0;
	box[2] = x1 - x0;
	box[3] = y1 - y0;
	return true;
}

cairo_surface_t *desk_app_sheet_surface(const char *id, double w, double h,
	const char *code)
{
	const struct desk_app_list *apps = desk_apps_acquire();
	const struct desk_app *app = desk_apps_find(apps, id);
	cairo_surface_t *s = NULL;
	if (app && w >= 8 && h >= 8) {
		s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)w, (int)h);
		cairo_t *cr = cairo_create(s);
		desk_draw_app_sheet(cr, w, h, app->name, app->generic, code,
			desk_app_icon(app), app->initial, app->seed,
			0.20 + 0.70 * hash21(app->seed, 2.2));
		cairo_destroy(cr);
		cairo_surface_flush(s);
	}
	desk_apps_release(apps);
	return s;
}

/* ---- a sheet taken out of a binder ---------------------------------
 *
 * Taken off the ring and held, carried down to the desk as you turn back
 * to it, and put down where the program's window will be; then the paper
 * comes out from behind it, the way paper does when a sheet is made
 * bigger, until it is the size of the window, and the print of the
 * catalogue goes off it. What is left is a blank sheet with the program's
 * name at its head -- the window, waiting for the program to write on it. */

/* The sheet in the hand, its middle at (cx, cy) on the screen, turned by
 * angle, `height` (0 lying on the desk .. 1 held up) off the desk: the
 * higher, the further its shadow falls and the softer it is. */
void desk_draw_taken_sheet(cairo_t *cr, cairo_surface_t *face, double cx, double cy,
	double w, double h, double angle, double height)
{
	if (!face || w < 1.0 || h < 1.0) {
		return;
	}
	double fw = cairo_image_surface_get_width(face), fh = cairo_image_surface_get_height(face);
	height = clampf(height, 0.0, 1.0);
	/* the shadow: the one a sheet lying on the desk has, carried away
	 * from the lamp and thinned out the higher the sheet is held */
	double ox, oy;
	double sx = cx, sy = cy;
	cairo_user_to_device(cr, &sx, &sy);
	shadow_dir(sx, sy, 70.0 * height, &ox, &oy);
	cairo_device_to_user_distance(cr, &ox, &oy);
	cairo_save(cr);
	cairo_push_group(cr);
	cairo_translate(cr, cx + ox, cy + oy);
	cairo_rotate(cr, angle);
	double grow = 1.0 + 0.05 * height;
	desk_draw_window_shadow(cr, -w * 0.5 * grow, -h * 0.5 * grow, w * grow, h * grow,
		DESK_RADIUS);
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, 1.0 - 0.45 * height);
	cairo_restore(cr);
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, angle);
	cairo_translate(cr, -w * 0.5, -h * 0.5);
	cairo_scale(cr, w / fw, h / fh);
	cairo_set_source_surface(cr, face, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* The blank sheet the window will be, made once: w by h, the program's
 * name at its head. */
cairo_surface_t *desk_window_base_sheet(double w, double h, const char *title)
{
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		(int)ceil(w), (int)ceil(h));
	cairo_t *cr = cairo_create(s);
	desk_draw_window_frame(cr, 0, 0, w, h, title, false, 0, false);
	cairo_destroy(cr);
	cairo_surface_flush(s);
	return s;
}

/* That sheet on the desk, w by h in its own coordinates, `open` from 0 --
 * only the sheet that was put down, lw by lh, in the middle, the
 * catalogue's print (face) still on it -- to 1, all of it, the paper that
 * came out from behind still creased where it was folded. */
void desk_draw_window_base(cairo_t *cr, cairo_surface_t *blank, cairo_surface_t *face,
	double w, double h, double lw, double lh, double open, unsigned seed)
{
	double e = clampf(open, 0.0, 1.0);
	e = e * e * (3.0 - 2.0 * e);
	double lx0 = (w - lw) * 0.5, ly0 = (h - lh) * 0.5;
	double x0 = lx0 * (1.0 - e), y0 = ly0 * (1.0 - e);
	double cw = w - 2.0 * x0, ch = h - 2.0 * y0;
	desk_draw_window_shadow(cr, x0, y0, cw, ch, DESK_RADIUS);
	if (blank) {
		cairo_save(cr);
		desk_rounded_path(cr, x0, y0, cw, ch, DESK_RADIUS);
		cairo_clip(cr);
		cairo_set_source_surface(cr, blank, 0, 0);
		cairo_paint(cr);
		/* the creases in what came out, and the turn of the paper at the
		 * edges while they are still coming */
		cairo_translate(cr, x0, y0);
		unsigned edges = e < 0.999 ? 15u : 0u;
		desk_draw_sizing(cr, cw, ch, lx0 - x0, ly0 - y0, lx0 + lw - x0, ly0 + lh - y0,
			edges, (1.0 - e) * 0.8, seed);
		cairo_restore(cr);
	}
	double fa = 1.0 - clampf(open / 0.55, 0.0, 1.0);
	if (face && fa > 0.0) {
		double fw = cairo_image_surface_get_width(face);
		double fh = cairo_image_surface_get_height(face);
		cairo_save(cr);
		cairo_translate(cr, lx0, ly0);
		cairo_scale(cr, lw / fw, lh / fh);
		cairo_set_source_surface(cr, face, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint_with_alpha(cr, fa * fa);
		cairo_restore(cr);
	}
}

/* The sheet coming down off the shelf and onto the desk, opening out into
 * the size of the window it is about to become. It leaves the frame just as
 * the client's own first frame arrives, so the paper is what you watch and
 * the handover is not something you see. */
void desk_draw_sheet_flight(cairo_t *cr, cairo_surface_t *sheet,
	double x0, double y0, double w0, double h0,
	double x1, double y1, double w1, double h1, double t)
{
	if (!sheet) {
		return;
	}
	double e = clampf(t, 0.0, 1.0);
	/* thrown, so it goes out fast and settles */
	double p = 1.0 - pow(1.0 - e, 2.6);
	double x = x0 + (x1 - x0) * p;
	double y = y0 + (y1 - y0) * p;
	double w = w0 + (w1 - w0) * p;
	double h = h0 + (h1 - h0) * p;
	/* a sheet let go of turns a little before it lies down */
	double rot = (1.0 - p) * 0.11;
	/* and rises before it falls */
	y -= sin(p * M_PI) * (h1 * 0.10);

	int sw = cairo_image_surface_get_width(sheet);
	int sh = cairo_image_surface_get_height(sheet);
	if (sw < 1 || sh < 1) {
		return;
	}
	cairo_save(cr);
	desk_set_paint_origin(0, 0);
	contact_shadow(cr, x + w * 0.06, y + h * 0.92, w * 0.88, h * 0.16,
		0.34 * (0.35 + 0.65 * p));
	cairo_translate(cr, x + w * 0.5, y + h * 0.5);
	cairo_rotate(cr, rot);
	cairo_translate(cr, -w * 0.5, -h * 0.5);
	cairo_scale(cr, w / sw, h / sh);
	cairo_set_source_surface(cr, sheet, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	/* the printing goes off it as it opens out: what lands is the window */
	cairo_paint_with_alpha(cr, e > 0.72 ? (1.0 - e) / 0.28 : 1.0);
	cairo_restore(cr);
}

/* ---- samples of what the desk may be made of ------------------------
 *
 * For Shell Settings' fan of them: a strip cut from the top of the desk as
 * it would be in that wood (timber_at as the top has it, desk_wood_at's
 * stain, its years of use), lit as the lamp lights the desk and tone-mapped
 * as the room is, so that a sample is the colour of the desk it makes. */

/* The lamp on the desk, and the room seen in the lacquer over it, as the
 * trace has them there: found by matching samples to the desk traced in each
 * wood (the v0.2 page's chips, cut from it). */
#define SAMPLE_LIGHT 1.30
#define SAMPLE_SHEEN 0.070
#define SAMPLES_KEPT 24
#define GRAINS_KEPT (DESK_WOOD_COUNT * 2)

static struct {
	int wood, stain, w, h;
	cairo_surface_t *s;
} g_samples[SAMPLES_KEPT];
static int g_samples_next;

/* What a sample is before its stain and the light, pixel by pixel: the
 * wood's colour over the four looks at it -- three numbers. The timber is
 * the slow part (about 2 ms to a thousand pixels), so a sample in another
 * stain is only these put together again. */
static struct {
	int wood, w, h;
	float *px;
} g_grains[GRAINS_KEPT];
static int g_grains_next;

static const float *sample_grain(int wood, int w, int h)
{
	for (int i = 0; i < GRAINS_KEPT; i++) {
		if (g_grains[i].px && g_grains[i].wood == wood && g_grains[i].w == w && g_grains[i].h == h) {
			return g_grains[i].px;
		}
	}
	float *px = malloc(sizeof *px * 3 * (size_t)w * (size_t)h);
	if (!px) {
		return NULL;
	}
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w; i++) {
			/* four looks a pixel, as the room is traced: the latewood is a
			 * line finer than one */
			vec3 sum = v3(0.0, 0.0, 0.0);
			for (int ss = 0; ss < 4; ss++) {
				double x = (i + SS_OFF[ss][0]) / w, y = (j + SS_OFF[ss][1]) / h;
				/* a strip of the desk's top, off to its right, back to front: 4.3
				 * units of one board across, and along it as much as the sample
				 * is long, as the desk is on the screen made smaller (the v0.2
				 * page's chips, cut from the desk traced at 1440 across) */
				double u = 11.0 + x * 4.3, v = -3.5 + y * 3.92 * h / w;
				struct mtl m;
				memset(&m, 0, sizeof m);
				timber_at(&DESK_WOODS[wood], v3(u, 0.0, v), u, v, &m, 5.4, 1.0);
				sum = vadd(sum, m.albedo);
			}
			float *o = px + 3 * ((size_t)j * (size_t)w + (size_t)i);
			o[0] = (float)(sum.x * 0.25);
			o[1] = (float)(sum.y * 0.25);
			o[2] = (float)(sum.z * 0.25);
		}
	}
	int k = g_grains_next;
	g_grains_next = (g_grains_next + 1) % GRAINS_KEPT;
	free(g_grains[k].px);
	g_grains[k].wood = wood;
	g_grains[k].w = w;
	g_grains[k].h = h;
	g_grains[k].px = px;
	return px;
}

cairo_surface_t *desk_wood_sample(int wood, int stain, int w, int h)
{
	wood = wood < 0 || wood >= DESK_WOOD_COUNT ? DESK_WOOD_WALNUT : wood;
	stain = desk_stain_offered(wood, stain) ? stain : DESK_STAIN_NATURAL;
	w = w < 4 ? 4 : w > 512 ? 512 : w;
	h = h < 4 ? 4 : h > 1024 ? 1024 : h;
	for (int i = 0; i < SAMPLES_KEPT; i++) {
		if (g_samples[i].s && g_samples[i].wood == wood && g_samples[i].stain == stain
			&& g_samples[i].w == w && g_samples[i].h == h) {
			return cairo_surface_reference(g_samples[i].s);
		}
	}
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	const float *grain = sample_grain(wood, w, h);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS || !grain) {
		return s;
	}
	cairo_surface_flush(s);
	unsigned char *px = cairo_image_surface_get_data(s);
	int stride = cairo_image_surface_get_stride(s);
	for (int j = 0; j < h; j++) {
		uint32_t *row = (uint32_t *)(px + (size_t)j * (size_t)stride);
		/* the lamp, a little less toward the far end; the room in the lacquer */
		double y = (j + 0.5) / h, fall = 1.06 - 0.12 * y;
		vec3 sheen = v3(SAMPLE_SHEEN, SAMPLE_SHEEN * 0.92, SAMPLE_SHEEN * 0.62);
		for (int i = 0; i < w; i++) {
			const float *g = grain + 3 * ((size_t)j * (size_t)w + (size_t)i);
			vec3 a = stained(v3(g[0], g[1], g[2]), stain);
			vec3 c = tonemap(vadd(vmul(a, SAMPLE_LIGHT * fall), sheen));
			uint32_t r = (uint32_t)lround(clampf(c.x, 0.0, 1.0) * 255.0);
			uint32_t gg = (uint32_t)lround(clampf(c.y, 0.0, 1.0) * 255.0);
			uint32_t b = (uint32_t)lround(clampf(c.z, 0.0, 1.0) * 255.0);
			row[i] = 0xff000000u | r << 16 | gg << 8 | b;
		}
	}
	cairo_surface_mark_dirty(s);
	int k = g_samples_next;
	g_samples_next = (g_samples_next + 1) % SAMPLES_KEPT;
	if (g_samples[k].s) {
		cairo_surface_destroy(g_samples[k].s);
	}
	g_samples[k].wood = wood;
	g_samples[k].stain = stain;
	g_samples[k].w = w;
	g_samples[k].h = h;
	g_samples[k].s = cairo_surface_reference(s);
	return s;
}

/* ---- Shell Settings ------------------------------------------------
 *
 * The shell's own settings are a form printed on a sheet of the shell's
 * paper (desk-settings, a program like any other, which asks for our
 * frame and leaves its own surface clear where there is no ink): small
 * capitals over a rule for each part, a box ticked in pencil for each
 * choice, what it means set under it, and where it is kept at the foot.
 * What is set in another program has a row too, with "Open ›" written
 * after it in pencil, the way one form points you to another. On a narrow
 * sheet a label takes two lines, a choice's words as many as they need,
 * and what is pencilled after a label goes under it. */

static int wrap_lines(cairo_t *cr, const char *s, double w, char out[][256], int max);

#define FORM_M 26.0
#define FORM_TOP 6.0
#define FORM_LABEL_LH 20.0
#define FORM_NOTE_LH 17.0
#define FORM_NOTE_MAX 10
#define FORM_OPT_GAP 26.0
#define FORM_OPT_LH 28.0
#define FORM_HAND_LH 18.0
#define FORM_ROWS 64

/* Where everything on the form goes, w wide: each row's top and height;
 * its label's lines; a choice's words, or an action's pencilled word (in
 * lines of its own, when it has to go under the label), each where it
 * starts, how wide and on which baseline; and its note's lines and the
 * baseline of the first. */
struct form_layout {
	double y[FORM_ROWS], h[FORM_ROWS];
	int nlab[FORM_ROWS];
	char label[FORM_ROWS][2][256];
	int nopt[FORM_ROWS];
	double opt_x[FORM_ROWS][DESK_FORM_OPTS], opt_w[FORM_ROWS][DESK_FORM_OPTS],
		opt_y[FORM_ROWS][DESK_FORM_OPTS];
	int nhand[FORM_ROWS];
	char hand[FORM_ROWS][2][256];
	int nl[FORM_ROWS], nwarn[FORM_ROWS];
	char note[FORM_ROWS][FORM_NOTE_MAX][256];
	double note_x[FORM_ROWS], note_y[FORM_ROWS];
	/* a fan of samples: where it turns, and its plate (x, y, w) */
	double fan_x[FORM_ROWS], fan_y[FORM_ROWS], plate[FORM_ROWS][3];
	double foot_y, total;
};
