/* Part of draw.c: the desk calendar.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

void desk_calendar_set_view(int view)
{
	if (view >= 0 && view < DESK_CAL_VIEWS) {
		g_cal_view = view;
	}
}

static int cal_days(int y, int m)
{
	static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
	return m == 1 && leap ? 29 : d[m];
}

/* the day of the week the 1st falls on, 0 for Sunday (Sakamoto's) */
static int cal_first(int y, int m)
{
	static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	int yy = m < 2 ? y - 1 : y;
	return (yy + yy / 4 - yy / 100 + yy / 400 + t[m] + 1) % 7;
}

static void cal_step(int *y, int *m, int by)
{
	int k = *y * 12 + *m + by;
	*y = k / 12;
	*m = k % 12;
}

/* Days since 1970-01-01, and back (Hinnant's); month 0 to 11. */
static long cal_daynum(int y, int m, int d)
{
	int mm = m + 1;
	y -= mm <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153u * (unsigned)(mm > 2 ? mm - 3 : mm + 9) + 2u) / 5u + (unsigned)d - 1u;
	unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
	return era * 146097 + (long)doe - 719468;
}

static void cal_civil(long z, int *y, int *m, int *d)
{
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
	long yy = (long)yoe + era * 400;
	unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
	unsigned mp = (5u * doy + 2u) / 153u;
	unsigned dd = doy - (153u * mp + 2u) / 5u + 1u;
	unsigned mm = mp < 10u ? mp + 3u : mp - 9u;
	*y = (int)(yy + (mm <= 2u));
	*m = (int)mm - 1;
	*d = (int)dd;
}

/* 0 for Sunday */
static int cal_wday(long z)
{
	long w = (z + 4) % 7;
	return (int)(w < 0 ? w + 7 : w);
}

void desk_calendar_step(int view, int dir, int *y, int *m, int *d)
{
	if (view == DESK_CAL_WEEK || view == DESK_CAL_DAY) {
		long z = cal_daynum(*y, *m, *d) + (view == DESK_CAL_WEEK ? 7 : 1) * dir;
		cal_civil(z, y, m, d);
		return;
	}
	if (view == DESK_CAL_YEAR) {
		*y += dir;
	} else {
		cal_step(y, m, dir);
	}
	int n = cal_days(*y, *m);
	*d = *d > n ? n : (*d < 1 ? 1 : *d);
}

static bool cal_is_today(const struct tm *today, int y, int m, int d)
{
	return today && today->tm_year + 1900 == y && today->tm_mon == m && today->tm_mday == d;
}

static void cal_ink(int wd, double *r, double *g, double *b)
{
	if (wd == 0) {
		*r = 0.74, *g = 0.15, *b = 0.12;
	} else if (wd == 6) {
		*r = 0.17, *g = 0.33, *b = 0.62;
	} else {
		*r = 0.20, *g = 0.19, *b = 0.18;
	}
}

/* A tent's page as the shelf has it: its shape, width over height, and
 * where the wire goes through it -- across, 0 to 1 of its width, and down
 * from its top edge, 0 to 1 of its height. Close up it is the same page. */
static double tent_page_w(const struct room_gadget *g)
{
	return 2.0 * (g->hx - CAL_PIN);
}

static double tent_page_h(const struct room_gadget *g)
{
	return g->h - CAL_PTOP - CAL_PY0;
}

/* the loops of its wire, two to a pair */
static int tent_loops(const struct room_gadget *g)
{
	return (2 * (int)tent_rn(g->hx) + 1) * 2;
}

static double tent_loop_x(const struct room_gadget *g, int i)
{
	int k = i / 2 - (int)tent_rn(g->hx);
	return 0.5 + (k * CAL_RP + ((i & 1) ? CAL_RT : -CAL_RT)) / tent_page_w(g);
}

static double tent_hole_y(const struct room_gadget *g)
{
	double zc = CAL_PZ - (CAL_PZ - 2.0 * CAL_T) * 0.5;
	return (CAL_RY + sqrt(CAL_RR * CAL_RR - zc * zc) - CAL_PTOP) / tent_page_h(g);
}

static const struct room_gadget *cal_gadget(void)
{
	return &ROOM_GADGETS[DESK_GADGET_CALENDAR];
}

static double cal_page_w(void)
{
	return tent_page_w(cal_gadget());
}

static double cal_page_h(void)
{
	return tent_page_h(cal_gadget());
}

double desk_calendar_face_ratio(void)
{
	return (cal_page_w() + CAL_TAB) / cal_page_w();
}

#define CAL_LOOPS tent_loops(cal_gadget())

static double cal_loop_x(int i)
{
	return tent_loop_x(cal_gadget(), i);
}

static double cal_hole_y(void)
{
	return tent_hole_y(cal_gadget());
}

/* The next letter of s into ch, a whole UTF-8 sequence of it (a dash or a
 * middle dot is more than one byte, and half of one is no string at all:
 * cairo stops drawing anything after it). How many bytes it took. */
static int cal_letter(const char *s, char ch[8])
{
	unsigned char c = (unsigned char)s[0];
	int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
	int k = 0;
	while (k < n && s[k]) {
		ch[k] = s[k];
		k++;
	}
	ch[k] = 0;
	return k > 0 ? k : 1;
}

/* letters set apart, as a month's name is on a calendar: how wide, and
 * set at x, y (where it ends) */
static double cal_spaced_w(cairo_t *cr, const char *s, double size, double track, bool bold)
{
	cairo_save(cr);
	desk_font_select(cr, DESK_FONT_ENGRAVE, s, bold, false);
	cairo_set_font_size(cr, size);
	double x = 0.0;
	char ch[8];
	for (const char *p = s; *p;) {
		p += cal_letter(p, ch);
		cairo_text_extents_t e;
		cairo_text_extents(cr, ch, &e);
		x += e.x_advance + (*p ? track : 0.0);
	}
	cairo_restore(cr);
	return x;
}

static double cal_spaced(cairo_t *cr, const char *s, double x, double y, double size,
	double track, bool bold, double r, double g, double b)
{
	cairo_save(cr);
	desk_font_select(cr, DESK_FONT_ENGRAVE, s, bold, false);
	cairo_set_font_size(cr, size);
	cairo_set_source_rgb(cr, r, g, b);
	char ch[8];
	for (const char *p = s; *p;) {
		p += cal_letter(p, ch);
		cairo_text_extents_t e;
		cairo_text_extents(cr, ch, &e);
		cairo_move_to(cr, x, y);
		cairo_show_text(cr, ch);
		x += e.x_advance + track;
	}
	cairo_restore(cr);
	return x - track;
}

/* A ring drawn round something with a red pen: not quite an ellipse, and
 * the end goes on a little past where it began. */
static void cal_ring(cairo_t *cr, double cx, double cy, double rx, double ry, double lw)
{
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_width(cr, lw);
	cairo_set_source_rgba(cr, 0.80, 0.12, 0.10, 0.86);
	double a0 = -2.1, sweep = 2.0 * M_PI + 0.55;
	for (int i = 0; i <= 64; i++) {
		double u = (double)i / 64.0, a = a0 + sweep * u;
		double k = 1.0 + 0.05 * sin(3.0 * a + 1.3) + 0.07 * u;
		double x = cx + cos(a) * rx * k, y = cy + sin(a) * ry * k - 0.10 * ry * u;
		if (i == 0) {
			cairo_move_to(cr, x, y);
		} else {
			cairo_line_to(cr, x, y);
		}
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* The sheet itself: card, the punched strip, and on the shelf the wire's
 * front half through it. */
static void cal_paper(cairo_t *cr, double w, double h, bool wire)
{
	fill_tile(cr, paper_tile(), 1.0, 1.0);
	cairo_set_source_rgba(cr, 1.0, 1.0, 0.99, 0.30);
	cairo_paint(cr);
	double hy = cal_hole_y() * h;
	double hw = fmax(w * 0.011, 1.2), hh = fmax(h * 0.026, 1.6);
	for (int i = 0; i < CAL_LOOPS; i++) {
		double x = cal_loop_x(i) * w;
		desk_rounded(cr, x - hw * 0.5, hy - hh * 0.5, hw, hh, hw * 0.5);
		cairo_set_source_rgb(cr, 0.13, 0.075, 0.045);
		cairo_fill(cr);
	}
	if (!wire) {
		return;
	}
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int i = 0; i < CAL_LOOPS; i++) {
		double x = cal_loop_x(i) * w;
		cairo_set_line_width(cr, fmax(w * 0.0095, 1.4));
		cairo_set_source_rgb(cr, 0.05, 0.05, 0.055);
		cairo_move_to(cr, x, hy);
		cairo_line_to(cr, x, -h * 0.05);
		cairo_stroke(cr);
		cairo_set_line_width(cr, fmax(w * 0.0030, 0.6));
		cairo_set_source_rgba(cr, 0.50, 0.50, 0.54, 0.75);
		cairo_move_to(cr, x - w * 0.0022, hy - hh * 0.2);
		cairo_line_to(cr, x - w * 0.0022, -h * 0.05);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

/* The head every page has: large figures, a word in red beside them and a
 * line in grey under it, gap (of the width) between. Where the head ends
 * down the page; the figures' box, for a ring round them. */
static double cal_head(cairo_t *cr, double w, double h, const char *big, const char *red,
	const char *grey, bool close, double gap, double box[4])
{
	double top = h * 0.120;
	double numsz = h * (close ? 0.180 : 0.205);
	cairo_save(cr);
	desk_font_select(cr, DESK_FONT_ENGRAVE, big, true, false);
	cairo_set_font_size(cr, numsz);
	cairo_text_extents_t ne;
	cairo_text_extents(cr, big, &ne);
	double nx = w * 0.050, base = top + numsz * 0.84;
	cairo_set_source_rgb(cr, 0.16, 0.15, 0.14);
	cairo_move_to(cr, nx - ne.x_bearing, base);
	cairo_show_text(cr, big);
	cairo_restore(cr);
	if (box) {
		box[0] = nx;
		box[1] = base + ne.y_bearing;
		box[2] = ne.width;
		box[3] = ne.height;
	}
	double tx = nx + ne.width + w * gap;
	cal_spaced(cr, red, tx, top + numsz * 0.40, h * (close ? 0.050 : 0.062),
		h * (close ? 0.011 : 0.008), true, 0.74, 0.15, 0.12);
	cal_spaced(cr, grey, tx, top + numsz * 0.82, h * (close ? 0.040 : 0.050),
		h * (close ? 0.007 : 0.005), false, 0.44, 0.41, 0.37);
	return top + numsz * 1.06 + h * 0.012;
}

/* ---- notes stuck on the pages --------------------------------------
 *
 * Sticky notes, the colours the pads come in: on a month's page small
 * flags along the foot of a day's box, on a week's down a day's column,
 * on a day's page at their hour and among the things to do; on the year's
 * a dot under the day. */

static const double STICK_RGB[DESK_STICK_COLOURS][3] = {
	{ 0.99, 0.88, 0.40 },   /* canary */
	{ 0.98, 0.68, 0.74 },   /* rose */
	{ 0.62, 0.82, 0.96 },   /* sky */
	{ 0.73, 0.89, 0.58 },   /* lime */
	{ 0.99, 0.74, 0.45 },   /* tangerine */
	{ 0.80, 0.72, 0.94 },   /* lilac */
};

static void stick_ink(int c, double k, double *r, double *g, double *b)
{
	c = c < 0 || c >= DESK_STICK_COLOURS ? 0 : c;
	*r = STICK_RGB[c][0] * k;
	*g = STICK_RGB[c][1] * k;
	*b = STICK_RGB[c][2] * k;
}

void desk_stick_colour(int c, double *r, double *g, double *b)
{
	stick_ink(c, 1.0, r, g, b);
}

/* Into a note's own frame: w by h at x, y as it would lie square, turned
 * by rot about its middle. */
static void stick_frame(cairo_t *cr, double x, double y, double w, double h, double rot)
{
	cairo_translate(cr, x + w * 0.5, y + h * 0.5);
	cairo_rotate(cr, rot);
	cairo_translate(cr, -w * 0.5, -h * 0.5);
}

/* A sticky note at 0, 0 in its frame, w by h: the colour of its pad, the
 * gummed band along its top a shade deeper, the rest of it a little off
 * the paper -- its shadow widening towards the foot, the more for lift
 * (0 to 1, a note being peeled). alpha, to fade it. */
static void stick_paper(cairo_t *cr, double w, double h, int colour, double lift, double alpha)
{
	double r, g, b;
	stick_ink(colour, 1.0, &r, &g, &b);
	double u = fmin(w, h);
	for (int i = 0; i < 3; i++) {
		double sp = u * (0.03 + 0.035 * i) * (1.0 + lift * 2.0);
		cairo_move_to(cr, 0.4, 0.8);
		cairo_line_to(cr, w + 0.4, 0.8);
		cairo_line_to(cr, w + sp * 0.8, h + sp);
		cairo_line_to(cr, -sp * 0.3, h + sp);
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.065 * alpha);
		cairo_fill(cr);
	}
	cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 0, h);
	double gum = fmin(0.24, u * 0.30 / h);
	cairo_pattern_add_color_stop_rgba(pat, 0.0, r * 0.95, g * 0.95, b * 0.95, alpha);
	cairo_pattern_add_color_stop_rgba(pat, gum, r * 0.95, g * 0.95, b * 0.95, alpha);
	cairo_pattern_add_color_stop_rgba(pat, gum + 0.01, r, g, b, alpha);
	cairo_pattern_add_color_stop_rgba(pat, 1.0, r * (0.97 - 0.05 * lift), g * (0.97 - 0.05 * lift),
		b * (0.97 - 0.05 * lift), alpha);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_set_source(cr, pat);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(pat);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.10 * alpha);
	cairo_set_line_width(cr, fmax(u * 0.012, 0.5));
	cairo_stroke(cr);
}

static void stick_font(cairo_t *cr, double size, bool bold)
{
	desk_font_select(cr, DESK_FONT_HAND, NULL, bold, false);
	cairo_set_font_size(cr, size);
}

static double stick_adv(cairo_t *cr, const char *ch)
{
	cairo_text_extents_t e;
	cairo_text_extents(cr, ch, &e);
	return e.x_advance;
}

/* Where the lines of s break, w wide in the font set: the start and end of
 * each in bytes, up to max of them; how many. At a space where there is
 * one, anywhere in a run without (Japanese has none). */
static int stick_wrap(cairo_t *cr, const char *s, double w, int *start, int *end, int max)
{
	int n = 0, ls = 0, i = 0, space = -1;
	double x = 0.0;
	while (s[i] && n < max) {
		char ch[8];
		int len = cal_letter(s + i, ch);
		double cw = stick_adv(cr, ch);
		if (x + cw > w && i > ls) {
			int br = space > ls ? space : i;
			start[n] = ls;
			end[n] = br;
			n++;
			ls = space > ls ? space + 1 : i;
			space = -1;
			x = 0.0;
			for (int k = ls; k < i;) {
				char c2[8];
				int l2 = cal_letter(s + k, c2);
				x += stick_adv(cr, c2);
				k += l2;
			}
			continue;
		}
		if (ch[0] == ' ') {
			space = i;
		}
		x += cw;
		i += len;
	}
	if (n < max && (i > ls || n == 0)) {
		start[n] = ls;
		end[n] = i;
		n++;
	}
	return n;
}

/* s on lines w wide from x, y down (baselines line apart), max of them,
 * the last cut short with an ellipsis if all of it does not go. */
static void stick_lines(cairo_t *cr, const char *s, double x, double y, double w, double line,
	int max)
{
	int st[8], en[8];
	max = max > 8 ? 8 : max;
	int n = stick_wrap(cr, s, w, st, en, max);
	bool cut = n > 0 && s[en[n - 1]] != '\0' && !(s[en[n - 1]] == ' ' && !s[en[n - 1] + 1]);
	for (int i = 0; i < n; i++) {
		char buf[256];
		int len = en[i] - st[i];
		len = len > 240 ? 240 : len;
		memcpy(buf, s + st[i], (size_t)len);
		buf[len] = '\0';
		if (cut && i == n - 1) {
			/* room for the ellipsis */
			double ew = stick_adv(cr, "\xe2\x80\xa6");
			while (len > 0) {
				cairo_text_extents_t e;
				cairo_text_extents(cr, buf, &e);
				if (e.x_advance + ew <= w) {
					break;
				}
				do {
					len--;
				} while (len > 0 && ((unsigned char)buf[len] & 0xc0) == 0x80);
				buf[len] = '\0';
			}
			snprintf(buf + len, sizeof buf - (size_t)len, "\xe2\x80\xa6");
		}
		cairo_move_to(cr, x, y + line * i);
		cairo_show_text(cr, buf);
	}
}

/* "10:00" for a plan at a time; span: "10:00–11:30", if it has an end */
static void stick_time(const struct desk_stick *s, char *out, int n, bool span)
{
	desk_agenda_time_text(s->minute, span ? s->span : 0, out, n);
}

/* a note's own small turn, the same every time it is drawn */
static double stick_rot(int id)
{
	return (((unsigned)id * 2654435761u >> 24) % 9 - 4.0) * 0.0045;
}

/* Looking for notes rather than drawing them (desk_calendar_note_pose,
 * desk_calendar_note_at): a page drawn into nothing with this on notes
 * down where each note on it went, and where the day asked about is. */
#define PROBE_NOTES 64
static struct {
	bool on;
	int y, m, d;
	bool have_day;
	struct desk_note_pose day;
	int n;
	int id[PROBE_NOTES];
	struct desk_note_pose pose[PROBE_NOTES];
} g_probe;

/* the notes off the page just now (desk_cal_look's hide): their places
 * are kept, empty */
static int g_cal_hide[DESK_CAL_HIDE];

static bool stick_away(int id)
{
	for (int i = 0; id && i < DESK_CAL_HIDE; i++) {
		if (g_cal_hide[i] == id) {
			return true;
		}
	}
	return false;
}

static void stick_seen(int id, double x, double y, double w, double h, double rot, int style,
	double font)
{
	if (g_probe.on && g_probe.n < PROBE_NOTES) {
		g_probe.id[g_probe.n] = id;
		g_probe.pose[g_probe.n] = (struct desk_note_pose){ x, y, w, h, rot, font, style };
		g_probe.n++;
	}
}

/* where day y-m-d is on the page, for a note of it with no room to be
 * seen: a spot size across at cx, cy */
static void day_seen(int y, int m, int d, double cx, double cy, double size)
{
	if (g_probe.on && !g_probe.have_day && y == g_probe.y && m == g_probe.m && d == g_probe.d) {
		g_probe.have_day = true;
		g_probe.day = (struct desk_note_pose){ cx - size * 0.5, cy - size * 0.5, size, size,
			0.0, 0.0, DESK_NOTE_DOT };
	}
}

/* the box of a thing to do, size across with its foot at y, ticked in red
 * when it is done */
static void stick_box(cairo_t *cr, const struct desk_stick *s, double x, double y, double size,
	double alpha)
{
	cairo_set_source_rgba(cr, 0.20, 0.18, 0.15, alpha * (s->done ? 0.55 : 1.0));
	cairo_set_line_width(cr, fmax(size * 0.10, 0.6));
	cairo_rectangle(cr, x, y - size, size, size);
	cairo_stroke(cr);
	if (s->done) {
		cairo_set_source_rgba(cr, 0.75, 0.12, 0.10, alpha * 0.9);
		cairo_set_line_width(cr, fmax(size * 0.16, 0.8));
		cairo_move_to(cr, x + size * 0.15, y - size * 0.48);
		cairo_line_to(cr, x + size * 0.42, y - size * 0.15);
		cairo_line_to(cr, x + size * 1.10, y - size * 1.20);
		cairo_stroke(cr);
	}
}

/* What is written on a flag on a month's day, w by h in its frame: its
 * time, if it has one, the box of a thing to do, and as much of the
 * writing as goes on one line. */
static void stick_flag_ink(cairo_t *cr, const struct desk_stick *s, double w, double h)
{
	double sz = h * 0.66, tx = h * 0.22, base = h * 0.74;
	double ink = s->done ? 0.55 : 1.0;
	if (s->kind == DESK_STICK_TODO) {
		double bs = sz * 0.78;
		stick_box(cr, s, tx, base, bs, 1.0);
		tx += bs + sz * 0.30;
	}
	char tm[24];
	stick_time(s, tm, sizeof tm, false);
	cairo_set_source_rgba(cr, 0.20, 0.18, 0.15, ink);
	if (tm[0]) {
		stick_font(cr, sz * 0.92, true);
		cairo_move_to(cr, tx, base);
		cairo_show_text(cr, tm);
		tx += stick_adv(cr, tm) + sz * 0.30;
	}
	stick_font(cr, sz, false);
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w - h * 0.12, h);
	cairo_clip(cr);
	stick_lines(cr, s->text, tx, base, w - tx - h * 0.20, h, 1);
	cairo_restore(cr);
}

/* A plan or a thing to do as a flag on a month's day. */
static void stick_flag(cairo_t *cr, const struct desk_stick *s, double x, double y, double w,
	double h)
{
	double rot = stick_rot(s->id) * 0.6;
	stick_seen(s->id, x, y, w, h, rot, DESK_NOTE_FLAG, h);
	if (stick_away(s->id)) {
		return;
	}
	cairo_save(cr);
	stick_frame(cr, x, y, w, h, rot);
	stick_paper(cr, w, h, s->colour, 0.0, s->done ? 0.75 : 1.0);
	stick_flag_ink(cr, s, w, h);
	cairo_restore(cr);
}

/* What is written on a note on a week's or a day's page, w by h in its
 * frame, sz the size of its writing: the time small over it -- until when,
 * too, if there is room -- the writing on as many lines as there is room
 * for; a thing to do with its box, struck through when it is done. */
static void stick_note_ink(cairo_t *cr, const struct desk_stick *s, double w, double h,
	double sz)
{
	double pad = sz * 0.45, tx = pad, ty = pad + sz * 0.92;
	double ink = s->done ? 0.55 : 1.0;
	if (s->kind == DESK_STICK_TODO) {
		double bs = sz * 0.82;
		stick_box(cr, s, pad, ty + bs * 0.14, bs, 1.0);
		tx += bs + sz * 0.45;
	}
	cairo_set_source_rgba(cr, 0.20, 0.18, 0.15, ink);
	char tm[24];
	stick_time(s, tm, sizeof tm, true);
	int lines = (int)floor((h - pad * 2.0) / (sz * 1.25));
	lines = lines < 1 ? 1 : lines;
	if (tm[0]) {
		stick_font(cr, sz * 0.85, true);
		if (stick_adv(cr, tm) > w - tx - pad) {
			/* no room for its end */
			stick_time(s, tm, sizeof tm, false);
		}
		cairo_move_to(cr, tx, ty);
		cairo_show_text(cr, tm);
		if (lines > 1) {
			ty += sz * 1.20;
			lines--;
		} else {
			tx += stick_adv(cr, tm) + sz * 0.35;
		}
	}
	stick_font(cr, sz, false);
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);
	stick_lines(cr, s->text, tx, ty, w - tx - pad, sz * 1.25, lines);
	if (s->done) {
		/* struck through */
		cairo_set_line_width(cr, fmax(sz * 0.07, 0.6));
		cairo_move_to(cr, tx, ty - sz * 0.32);
		cairo_line_to(cr, w - pad, ty - sz * 0.32);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

/* A note on a week's or a day's page. */
static void stick_note(cairo_t *cr, const struct desk_stick *s, double x, double y, double w,
	double h, double sz)
{
	double rot = stick_rot(s->id);
	stick_seen(s->id, x, y, w, h, rot, DESK_NOTE_CARD, sz);
	if (stick_away(s->id)) {
		return;
	}
	cairo_save(cr);
	stick_frame(cr, x, y, w, h, rot);
	stick_paper(cr, w, h, s->colour, 0.0, s->done ? 0.75 : 1.0);
	stick_note_ink(cr, s, w, h, sz);
	cairo_restore(cr);
}

/* What is written on a note in the binder, w by h in its frame, font the
 * size of its writing: as the note being written there is laid out (the
 * binder's agd_edit_draw), so that one taken off the page to go to the
 * calendar starts out looking the same. */
static void stick_binder_ink(cairo_t *cr, const struct desk_stick *s, double w, double h,
	double font)
{
	double pad = font * 0.55, line = font * 1.32;
	double x = pad, y = pad + font * 0.95;
	if (s->kind == DESK_STICK_TODO) {
		stick_box(cr, s, x, y + font * 0.12, font * 0.85, 1.0);
		x += font * 1.30;
	}
	cairo_set_source_rgba(cr, 0.17, 0.15, 0.12, s->done ? 0.55 : 1.0);
	char tm[24];
	stick_time(s, tm, sizeof tm, true);
	double top = 0.0;
	if (tm[0]) {
		stick_font(cr, font * 0.80, true);
		cairo_move_to(cr, x, y - font * 0.10);
		cairo_show_text(cr, tm);
		y += line * 0.80;
		top = line * 0.80;
	}
	int lines = (int)floor((h - pad * 2.0 - top) / line + 0.02);
	lines = lines < 1 ? 1 : (lines > 3 ? 3 : lines);
	stick_font(cr, font, false);
	stick_lines(cr, s->text, x, y, w - pad - x, line, lines);
}

/* the drawing on a note, as it is where it is */
static void stick_ink_as(cairo_t *cr, const struct desk_stick *s,
	const struct desk_note_pose *p)
{
	switch (p->style) {
	case DESK_NOTE_BINDER:
		stick_binder_ink(cr, s, p->w, p->h, p->font);
		break;
	case DESK_NOTE_FLAG:
		stick_flag_ink(cr, s, p->w, p->h);
		break;
	case DESK_NOTE_CARD:
		stick_note_ink(cr, s, p->w, p->h, p->font);
		break;
	default:
		break;
	}
}

/* In the air the note keeps to its own proportions: the drawing it had at
 * either end is scaled as a whole to fit what the paper is now, cut off
 * at its edges, the one faded into the other. */
void desk_draw_note_between(cairo_t *cr, const struct desk_stick *s,
	const struct desk_note_pose *at, const struct desk_note_pose *from,
	const struct desk_note_pose *to, double look, double lift, double alpha)
{
	if (at->w < 0.5 || at->h < 0.5 || alpha <= 0.0) {
		return;
	}
	cairo_save(cr);
	stick_frame(cr, at->x, at->y, at->w, at->h, at->rot);
	stick_paper(cr, at->w, at->h, s->colour, lift, alpha * (s->done ? 0.8 : 1.0));
	cairo_rectangle(cr, 0, 0, at->w, at->h);
	cairo_clip(cr);
	look = clampf(look, 0.0, 1.0);
	const struct desk_note_pose *p[2] = { from, to };
	double a[2] = { 1.0 - look, look };
	for (int i = 0; i < 2; i++) {
		if (a[i] * alpha < 0.02 || p[i]->style == DESK_NOTE_DOT || p[i]->w < 0.5
			|| p[i]->h < 0.5) {
			continue;
		}
		double k = fmin(at->w / p[i]->w, at->h / p[i]->h);
		cairo_push_group(cr);
		cairo_scale(cr, k, k);
		stick_ink_as(cr, s, p[i]);
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, a[i] * alpha);
	}
	cairo_restore(cr);
}

/* The notes on a day, plans first and then things to do, up to max of
 * them; how many there are in all. */
static int stick_day(int y, int m, int d, struct desk_stick *out, int max)
{
	int np = desk_agenda_day(y, m, d, DESK_STICK_PLAN, out, max);
	int got = np < max ? np : max;
	int nt = desk_agenda_day(y, m, d, DESK_STICK_TODO, out + got, max - got);
	return np + nt;
}

/* A month in small: its name, perhaps the days' initials, the dates, today
 * ringed -- for the year's page, and the corners of the others. row: a
 * week to shade (the one the page is about), -1 for none; ring: a day to
 * ring rather than today, 0 for today. */
static void cal_mini(cairo_t *cr, double x, double y, double w, double h, int yr, int m,
	const struct tm *today, int row, int ring, bool full, bool initials)
{
	char t[16], mb[16];
	snprintf(t, sizeof t, "%s", cal_month_word(m, full, mb, sizeof mb));
	bool now = today && today->tm_year + 1900 == yr && today->tm_mon == m;
	double tr = now ? 0.74 : 0.36, tg = now ? 0.15 : 0.33, tb = now ? 0.12 : 0.30;
	cal_spaced(cr, t, x, y + h * 0.13, h * 0.125, h * 0.025, true, tr, tg, tb);
	double gy = y + h * 0.20;
	double cw = w / 7.0;
	if (initials) {
		for (int c = 0; c < 7; c++) {
			double r, g, b;
			cal_ink(c, &r, &g, &b);
			char db[16];
			show_text_as(cr, DESK_FONT_ENGRAVE, cal_day_word(c, 0, db, sizeof db),
				x + cw * (c + 0.5), gy + h * 0.075, h * 0.075, true,
				r, g, b, 0.9, 0);
		}
		gy += h * 0.105;
	}
	double rh = (y + h - gy) / 6.0;
	int f = cal_first(yr, m), n = cal_days(yr, m);
	unsigned char marks[32];
	desk_agenda_marks(yr, m, marks);
	if (row >= 0) {
		cairo_rectangle(cr, x - cw * 0.08, gy + rh * row + rh * 0.08, w + cw * 0.16, rh * 0.94);
		cairo_set_source_rgba(cr, 0.86, 0.66, 0.30, 0.26);
		cairo_fill(cr);
	}
	for (int d = 1; d <= n; d++) {
		int idx = f + d - 1;
		double r, g, b;
		cal_ink(idx % 7, &r, &g, &b);
		char num[12];
		snprintf(num, sizeof num, "%d", d);
		double cx = x + cw * (idx % 7 + 0.5), by = gy + rh * (idx / 7 + 0.80);
		double sz = rh * 0.72;
		show_text_as(cr, DESK_FONT_ENGRAVE, num, cx, by, sz, false, r, g, b, 0.88, 0);
		if (full) {
			/* the year's page: a note of the day goes to its spot */
			day_seen(yr, m, d, cx, by + rh * 0.13, fmax(rh * 0.30, 2.0));
		}
		if (marks[d]) {
			/* something planned that day */
			double sr, sg, sb;
			stick_ink(marks[d] - 1, 0.80, &sr, &sg, &sb);
			cairo_arc(cr, cx, by + rh * 0.13, fmax(rh * 0.10, 0.6), 0, 2.0 * M_PI);
			cairo_set_source_rgb(cr, sr, sg, sb);
			cairo_fill(cr);
		}
		if (ring ? d == ring : cal_is_today(today, yr, m, d)) {
			cal_ring(cr, cx, by - sz * 0.36, cw * 0.50, rh * 0.54, fmax(rh * 0.075, 0.7));
		}
	}
}

/* ---- the four kinds of page ---- */

static void cal_page_year(cairo_t *cr, double w, double h, int yr, int m,
	const struct tm *today, bool close)
{
	char big[12], grey[24];
	snprintf(big, sizeof big, "%d", yr);
	snprintf(grey, sizeof grey, TR("%d DAYS"), cal_days(yr, 1) == 29 ? 366 : 365);
	double gy = cal_head(cr, w, h, big, TR("head|YEAR"), grey, close, 0.024, NULL);
	double gx = w * 0.045, gw = w * 0.910, gb = h * 0.962;
	double cw = gw / 4.0, ch = (gb - gy) / 3.0;
	for (int k = 0; k < 12; k++) {
		double x = gx + cw * (k % 4), y = gy + ch * (k / 4);
		if (k == m) {
			/* the month the calendar was at */
			desk_rounded(cr, x + cw * 0.02, y + ch * 0.01, cw * 0.96, ch * 0.97, cw * 0.04);
			cairo_set_source_rgba(cr, 0.84, 0.74, 0.56, 0.20);
			cairo_fill(cr);
		}
		cal_mini(cr, x + cw * 0.08, y + ch * 0.05, cw * 0.84, ch * 0.88, yr, k, today, -1, 0,
			close, close);
	}
}

static void cal_page_month(cairo_t *cr, double w, double h, int yr, int m,
	const struct tm *today, bool close)
{
	char num[12], year[12];
	snprintf(num, sizeof num, "%d", m + 1);
	snprintf(year, sizeof year, "%d", yr);
	/* "10 OCTOBER 2026" -- and in Japanese "10 月 2026年" */
	char mb[16];
	const char *red = cal_month_word(m, true, mb, sizeof mb);
	if (cal_year_word()) {
		red = desk_lang_get() == DESK_LANG_KO ? "\xec\x9b\x94" : "\xe6\x9c\x88";   /* 월, 月 */
		snprintf(year, sizeof year, "%d%s", yr, cal_year_word());
	}
	double gy = cal_head(cr, w, h, num, red, year, close, 0.024, NULL);
	if (close) {
		int py = yr, pm = m, ny = yr, nm = m;
		cal_step(&py, &pm, -1);
		cal_step(&ny, &nm, 1);
		double mw = w * 0.150, gap = w * 0.030, mh = h * 0.184;
		cal_mini(cr, w * 0.955 - mw, h * 0.116, mw, mh, ny, nm, today, -1, 0, false, false);
		cal_mini(cr, w * 0.955 - 2.0 * mw - gap, h * 0.116, mw, mh, py, pm, today, -1, 0,
			false, false);
	}
	double gx = w * 0.045, gw = w * 0.910;
	double wh = h * (close ? 0.052 : 0.070);
	double gb = h * 0.962;
	int f = cal_first(yr, m), n = cal_days(yr, m);
	int rows = (f + n + 6) / 7;
	double cw = gw / 7.0, rh = (gb - gy - wh) / rows;
	for (int c = 0; c < 7; c++) {
		double r, g, b;
		cal_ink(c, &r, &g, &b);
		char db[16];
		show_text_as(cr, DESK_FONT_ENGRAVE, cal_day_word(c, close ? 1 : 0, db, sizeof db),
			gx + cw * (c + 0.5),
			gy + wh * 0.70, wh * (close ? 0.50 : 0.74), true, r, g, b, 1.0, 0);
	}
	cairo_set_source_rgba(cr, 0.34, 0.31, 0.27, 0.60);
	cairo_set_line_width(cr, fmax(h * 0.0026, 0.8));
	cairo_move_to(cr, gx, gy + wh);
	cairo_line_to(cr, gx + gw, gy + wh);
	cairo_stroke(cr);
	if (close) {
		cairo_set_source_rgba(cr, 0.52, 0.47, 0.41, 0.30);
		cairo_set_line_width(cr, fmax(h * 0.0016, 0.6));
		for (int r = 1; r <= rows; r++) {
			cairo_move_to(cr, gx, gy + wh + rh * r);
			cairo_line_to(cr, gx + gw, gy + wh + rh * r);
		}
		for (int c = 1; c < 7; c++) {
			cairo_move_to(cr, gx + cw * c, gy + wh);
			cairo_line_to(cr, gx + cw * c, gb);
		}
		cairo_stroke(cr);
	}
	int py = yr, pm = m;
	cal_step(&py, &pm, -1);
	int pn = cal_days(py, pm);
	unsigned char marks[32];
	desk_agenda_marks(yr, m, marks);
	for (int cell = 0; cell < rows * 7; cell++) {
		int col = cell % 7, row = cell / 7;
		int d = cell - f + 1;
		bool in = d >= 1 && d <= n;
		if (!in && !close) {
			continue;
		}
		int shown = in ? d : (d < 1 ? pn + d : d - n);
		char t[12];
		snprintf(t, sizeof t, "%d", shown);
		double r, g, b;
		cal_ink(col, &r, &g, &b);
		double x0 = gx + cw * col, y0 = gy + wh + rh * row;
		double sz = close ? rh * 0.33 : rh * 0.62;
		double tx0 = close ? x0 + cw * 0.09 : x0 + cw * 0.5;
		double ty0 = close ? y0 + rh * 0.42 : y0 + rh * 0.74;
		show_text_as(cr, DESK_FONT_ENGRAVE, t, tx0, ty0, sz, true, r, g, b, in ? 1.0 : 0.28,
			close ? -1 : 0);
		if (in && close) {
			/* where the ones with no room go: in with the count of them */
			day_seen(yr, m, d, x0 + cw * 0.86, y0 + rh * 0.30, rh * 0.16);
		}
		if (in && close && marks[d]) {
			struct desk_stick st[8];
			int k = stick_day(yr, m, d, st, 8);
			int show = k > 2 ? 2 : k;
			for (int i = 0; i < show; i++) {
				stick_flag(cr, &st[i], x0 + cw * 0.06, y0 + rh * (0.50 + 0.245 * i), cw * 0.88,
					rh * 0.215);
			}
			if (k > show) {
				char more[12];
				snprintf(more, sizeof more, "+%d", k - show);
				show_text_as(cr, DESK_FONT_ENGRAVE, more, x0 + cw * 0.93, y0 + rh * 0.36,
					rh * 0.20, true,
					0.45, 0.42, 0.37, 1.0, 1);
			}
		} else if (in && marks[d]) {
			double sr, sg, sb;
			stick_ink(marks[d] - 1, 0.85, &sr, &sg, &sb);
			cairo_rectangle(cr, x0 + cw * 0.22, y0 + rh * 0.86, cw * 0.56, fmax(rh * 0.10, 0.8));
			cairo_set_source_rgb(cr, sr, sg, sb);
			cairo_fill(cr);
		}
		if (in && cal_is_today(today, yr, m, d)) {
			double len = (double)strlen(t);
			if (close) {
				cal_ring(cr, tx0 + sz * 0.29 * len, ty0 - sz * 0.36,
					sz * (0.30 * len + 0.38), sz * 0.66, sz * 0.10);
			} else {
				cal_ring(cr, tx0, ty0 - sz * 0.36, fmin(cw * 0.46, sz * (0.30 * len + 0.40)),
					fmin(rh * 0.50, sz * 0.66), fmax(sz * 0.11, 0.9));
			}
		}
	}
}

/* the days a week's page is about: "OCT 4 – 10, 2026", "OCT 28 – NOV 3,
 * 2026", "DEC 28, 2025 – JAN 3, 2026"; in Chinese, Japanese and Korean the
 * year first, "2026年10月4日 – 10日", and in Spanish, French and German the
 * day, "4 – 10 OCT 2026" (in German "4. – 10. OKT 2026") */
static void cal_week_range(char *out, size_t n, int ya, int ma, int da, int yb, int mb, int db)
{
	char m1[16], m2[16];
	const char *a = cal_month_word(ma, false, m1, sizeof m1);
	const char *b = cal_month_word(mb, false, m2, sizeof m2);
	enum desk_lang l = desk_lang_get();
	const char *year = cal_year_word();
	if (year) {
		/* 年 and 日, or 년 and 일 spaced as Korean is */
		bool ko = l == DESK_LANG_KO;
		const char *day = ko ? "\xec\x9d\xbc" : "\xe6\x97\xa5";
		const char *sp = ko ? " " : "";
		char from[64];
		snprintf(from, sizeof from, "%d%s%s%s%s%d%s", ya, year, sp, a, sp, da, day);
		if (ya != yb) {
			snprintf(out, n, "%s \xe2\x80\x93 %d%s%s%s%s%d%s", from, yb, year, sp, b, sp, db,
				day);
		} else if (ma != mb) {
			snprintf(out, n, "%s \xe2\x80\x93 %s%s%d%s", from, b, sp, db, day);
		} else {
			snprintf(out, n, "%s \xe2\x80\x93 %d%s", from, db, day);
		}
		return;
	}
	if (l == DESK_LANG_ES || l == DESK_LANG_FR || l == DESK_LANG_DE) {
		const char *dot = l == DESK_LANG_DE ? "." : "";
		if (ya != yb) {
			snprintf(out, n, "%d%s %s %d \xe2\x80\x93 %d%s %s %d", da, dot, a, ya, db, dot, b,
				yb);
		} else if (ma != mb) {
			snprintf(out, n, "%d%s %s \xe2\x80\x93 %d%s %s %d", da, dot, a, db, dot, b, yb);
		} else {
			snprintf(out, n, "%d%s \xe2\x80\x93 %d%s %s %d", da, dot, db, dot, b, yb);
		}
		return;
	}
	if (ya != yb) {
		snprintf(out, n, "%s %d, %d \xe2\x80\x93 %s %d, %d", a, da, ya, b, db, yb);
	} else if (ma != mb) {
		snprintf(out, n, "%s %d \xe2\x80\x93 %s %d, %d", a, da, b, db, yb);
	} else {
		snprintf(out, n, "%s %d \xe2\x80\x93 %d, %d", a, da, db, yb);
	}
}

static void cal_page_week(cairo_t *cr, double w, double h, int yr, int m, int d,
	const struct tm *today, bool close)
{
	long z = cal_daynum(yr, m, d), z0 = z - cal_wday(z);
	int ya, ma, da, yb, mb, db;
	cal_civil(z0, &ya, &ma, &da);
	cal_civil(z0 + 6, &yb, &mb, &db);
	/* counted the way a calendar that starts its weeks on a Sunday counts
	 * them: the week with the 1st of January in it is the first */
	int wk = (int)((z0 - cal_daynum(yb, 0, 1) + cal_first(yb, 0)) / 7) + 1;
	char big[12], grey[128];
	snprintf(big, sizeof big, "%d", wk);
	cal_week_range(grey, sizeof grey, ya, ma, da, yb, mb, db);
	double gy = cal_head(cr, w, h, big, TR("head|WEEK"), grey, close, 0.024, NULL);
	if (close) {
		/* the month it falls in, the week shaded */
		double mw = w * 0.150, mh = h * 0.184;
		cal_mini(cr, w * 0.955 - mw, h * 0.116, mw, mh, yr, m, today,
			(cal_first(yr, m) + d - 1) / 7, 0, false, false);
	}
	double gx = w * 0.045, gw = w * 0.910, gb = h * 0.962;
	double cw = gw / 7.0;
	double hb = (gb - gy) * (close ? 0.27 : 0.42);
	cairo_set_source_rgba(cr, 0.34, 0.31, 0.27, 0.60);
	cairo_set_line_width(cr, fmax(h * 0.0026, 0.8));
	cairo_move_to(cr, gx, gy + hb);
	cairo_line_to(cr, gx + gw, gy + hb);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 0.52, 0.47, 0.41, 0.34);
	cairo_set_line_width(cr, fmax(h * 0.0016, 0.6));
	for (int c = 1; c < 7; c++) {
		cairo_move_to(cr, gx + cw * c, gy);
		cairo_line_to(cr, gx + cw * c, gb);
	}
	cairo_stroke(cr);
	for (int c = 0; c < 7; c++) {
		int yy, mm, dd;
		cal_civil(z0 + c, &yy, &mm, &dd);
		double r, g, b;
		cal_ink(c, &r, &g, &b);
		double cx = gx + cw * (c + 0.5);
		char dw[16];
		show_text_as(cr, DESK_FONT_ENGRAVE, cal_day_word(c, close ? 1 : 0, dw, sizeof dw), cx,
			gy + hb * 0.26,
			hb * (close ? 0.17 : 0.30), true, r, g, b, 1.0, 0);
		char num[12];
		snprintf(num, sizeof num, "%d", dd);
		double sz = hb * (close ? 0.44 : 0.50), by = gy + hb * (close ? 0.78 : 0.90);
		show_text_as(cr, DESK_FONT_ENGRAVE, num, cx, by, sz, true, r, g, b, 1.0, 0);
		if (close && (dd == 1 || c == 0)) {
			char mw[16];
			show_text_as(cr, DESK_FONT_ENGRAVE, cal_month_word(mm, false, mw, sizeof mw), cx,
				gy + hb * 0.95, hb * 0.11, true,
				0.44, 0.41, 0.37, 1.0, 0);
		}
		if (cal_is_today(today, yy, mm, dd)) {
			double len = (double)strlen(num);
			cal_ring(cr, cx, by - sz * 0.36, fmin(cw * 0.44, sz * (0.30 * len + 0.40)),
				sz * 0.64, fmax(sz * 0.08, 0.9));
		}
		if (close) {
			/* lines to write on */
			cairo_set_source_rgba(cr, 0.55, 0.50, 0.44, 0.30);
			cairo_set_line_width(cr, fmax(h * 0.0012, 0.5));
			for (int k = 1; k <= 6; k++) {
				double y = gy + hb + (gb - gy - hb) * k / 7.0;
				cairo_move_to(cr, gx + cw * c + cw * 0.10, y);
				cairo_line_to(cr, gx + cw * c + cw * 0.90, y);
			}
			cairo_stroke(cr);
		}
		/* and what is stuck there, plans and then things to do */
		struct desk_stick st[8];
		int k = stick_day(yy, mm, dd, st, 8);
		double area = gb - gy - hb, top = gy + hb + area * 0.03;
		if (close) {
			double nh = area / 4.4;
			int fit = (int)floor((gb - top) / (nh * 1.06));
			int show = k > fit ? fit - 1 : k;
			show = show < 0 ? 0 : (show > 8 ? 8 : show);
			for (int i = 0; i < show; i++) {
				stick_note(cr, &st[i], gx + cw * c + cw * 0.06, top + nh * 1.06 * i, cw * 0.88,
					nh, nh * 0.24);
			}
			day_seen(yy, mm, dd, cx, top + nh * 1.06 * show + nh * 0.32, nh * 0.30);
			if (k > show) {
				char more[12];
				snprintf(more, sizeof more, "+%d", k - show);
				show_text_as(cr, DESK_FONT_ENGRAVE, more, cx,
					top + nh * 1.06 * show + nh * 0.45, nh * 0.30, true,
					0.45, 0.42, 0.37, 1.0, 0);
			}
		} else {
			for (int i = 0; i < k && i < 4; i++) {
				double sr, sg, sb;
				stick_ink(st[i].colour, st[i].done ? 0.65 : 0.85, &sr, &sg, &sb);
				cairo_rectangle(cr, gx + cw * c + cw * 0.12, top + area * 0.18 * i, cw * 0.76,
					area * 0.13);
				cairo_set_source_rgb(cr, sr, sg, sb);
				cairo_fill(cr);
			}
		}
	}
}

static void cal_page_day(cairo_t *cr, double w, double h, int yr, int m, int d,
	const struct tm *today, bool close)
{
	long z = cal_daynum(yr, m, d);
	int wd = cal_wday(z);
	char big[12], grey[32];
	snprintf(big, sizeof big, "%d", d);
	cal_month_year(grey, sizeof grey, m, yr);
	double nb[4];
	bool now = cal_is_today(today, yr, m, d);
	/* today ringed, the ring given room beside the figures */
	char dname[32];
	double gy = cal_head(cr, w, h, big, cal_day_word(wd, 2, dname, sizeof dname), grey, close,
		now ? 0.052 : 0.024, nb);
	if (now) {
		cal_ring(cr, nb[0] + nb[2] * 0.5, nb[1] + nb[3] * 0.52, nb[2] * 0.5 + h * 0.026,
			nb[3] * 0.5 + h * 0.024, fmax(h * 0.009, 1.0));
	}
	int ylen = cal_days(yr, 1) == 29 ? 366 : 365;
	int yday = (int)(z - cal_daynum(yr, 0, 1)) + 1;
	if (close) {
		double mw = w * 0.150, mh = h * 0.184;
		cal_mini(cr, w * 0.955 - mw, h * 0.116, mw, mh, yr, m, today, -1, d, false, false);
		/* beside the month and the year (cal_head), which a ringed day and a
		 * long month push over towards it: the days to go left off where
		 * they would run into it, and the day of the year too */
		double grey_x1 = nb[0] + nb[2] + w * (now ? 0.052 : 0.024)
			+ cal_spaced_w(cr, grey, h * 0.040, h * 0.007, false);
		char left[40];
		for (int k = 0; k < 2; k++) {
			if (k == 0) {
				snprintf(left, sizeof left, TR("DAY %d  \xc2\xb7  %d TO GO"), yday,
					ylen - yday);
			} else {
				snprintf(left, sizeof left, TR("DAY %d"), yday);
			}
			double lw = cal_spaced_w(cr, left, h * 0.030, h * 0.004, false);
			double lx = w * 0.955 - mw - w * 0.03 - lw;
			if (lx >= grey_x1 + w * 0.02) {
				cal_spaced(cr, left, lx, h * 0.290, h * 0.030, h * 0.004, false, 0.50, 0.47,
					0.42);
				break;
			}
		}
	}
	double gx = w * 0.045, gw = w * 0.910, gb = h * 0.962;
	/* the hours, eight to eight, down the left; notes and things to do on
	 * the right */
	int hours = 13;
	double rh = (gb - gy) / hours;
	double lx1 = gx + gw * (close ? 0.58 : 0.62);
	for (int i = 0; i < hours; i++) {
		double y = gy + rh * (i + 1);
		cairo_set_source_rgba(cr, 0.55, 0.50, 0.44, 0.42);
		cairo_set_line_width(cr, fmax(h * 0.0014, 0.5));
		cairo_move_to(cr, gx, y);
		cairo_line_to(cr, lx1, y);
		cairo_stroke(cr);
		char hr[8];
		snprintf(hr, sizeof hr, "%d", 8 + i);
		show_text_as(cr, DESK_FONT_ENGRAVE, hr, gx + gw * 0.045, gy + rh * (i + 0.72),
			rh * (close ? 0.50 : 0.66),
			true, 0.48, 0.45, 0.40, 1.0, 1);
		if (close) {
			double dash[2] = { h * 0.004, h * 0.006 };
			cairo_set_dash(cr, dash, 2, 0);
			cairo_set_source_rgba(cr, 0.55, 0.50, 0.44, 0.26);
			cairo_move_to(cr, gx + gw * 0.065, y - rh * 0.5);
			cairo_line_to(cr, lx1, y - rh * 0.5);
			cairo_stroke(cr);
			cairo_set_dash(cr, NULL, 0, 0);
		}
	}
	double rx0 = gx + gw * (close ? 0.63 : 0.68), rx1 = gx + gw;
	if (close) {
		cal_spaced(cr, TR("NOTES"), rx0, gy + rh * 0.70, rh * 0.42, rh * 0.10, true,
			0.48, 0.45, 0.40);
	}
	struct desk_stick plan[16], todo[8];
	int np = desk_agenda_day(yr, m, d, DESK_STICK_PLAN, plan, 16);
	int nt = desk_agenda_day(yr, m, d, DESK_STICK_TODO, todo, 8);
	np = np > 16 ? 16 : np;
	nt = nt > 8 ? 8 : nt;
	cairo_set_source_rgba(cr, 0.55, 0.50, 0.44, 0.42);
	cairo_set_line_width(cr, fmax(h * 0.0014, 0.5));
	for (int i = 1; i <= 6; i++) {
		cairo_move_to(cr, rx0, gy + rh * i);
		cairo_line_to(cr, rx1, gy + rh * i);
	}
	cairo_stroke(cr);
	if (close) {
		cal_spaced(cr, TR("TO DO"), rx0, gy + rh * 7.70, rh * 0.42, rh * 0.10, true,
			0.48, 0.45, 0.40);
	}
	/* The plans: those at a time down the hours, from their start for as
	 * long as they last -- one with no end given filling its hour --
	 * side by side where they run into one another; those for the day
	 * down the notes. Before eight they start at the top, after eight at
	 * night at the foot. */
	double hx0 = gx + gw * 0.075, hw = lx1 - hx0 - gw * 0.01;
	double a0[16], a1[16];
	int col[16], ncol[16];
	int run = -1, run_cols = 0;
	double run_end = -1.0, colend[16];
	for (int i = 0; i < np; i++) {
		const struct desk_stick *s = &plan[i];
		col[i] = -1;
		ncol[i] = 1;
		if (s->minute < 0) {
			continue;
		}
		double t0, t1;
		if (s->span > 0) {
			t0 = s->minute / 60.0 - 8.0;
			t1 = t0 + s->span / 60.0;
		} else {
			t0 = floor(s->minute / 60.0) - 8.0;
			t1 = t0 + 1.0;
		}
		t0 = clampf(t0, 0.0, hours - 0.6);
		t1 = clampf(t1, t0 + 0.6, (double)hours);
		a0[i] = t0;
		a1[i] = t1;
		/* plans come in order of their start: a new run of them where
		 * this one starts after all the run so far have ended */
		if (t0 >= run_end - 1e-6) {
			for (int j = run; j >= 0 && j < i; j++) {
				if (col[j] >= 0) {
					ncol[j] = run_cols;
				}
			}
			run = i;
			run_cols = 0;
			run_end = t1;
		}
		int c = 0;
		while (c < run_cols && colend[c] > t0 + 1e-6) {
			c++;
		}
		if (c == run_cols && run_cols < 16) {
			run_cols++;
		}
		c = c > 15 ? 15 : c;
		colend[c] = t1;
		col[i] = c;
		run_end = fmax(run_end, t1);
	}
	for (int j = run; j >= 0 && j < np; j++) {
		if (col[j] >= 0) {
			ncol[j] = run_cols;
		}
	}
	int untimed = 0;
	for (int i = 0; i < np; i++) {
		const struct desk_stick *s = &plan[i];
		if (col[i] >= 0) {
			double w1 = hw / ncol[i];
			double y0 = gy + rh * a0[i], y1 = gy + rh * a1[i];
			if (close) {
				stick_note(cr, s, hx0 + w1 * col[i], y0 + rh * 0.08, w1 - rh * 0.12,
					y1 - y0 - rh * 0.16, rh * 0.40);
			} else {
				double sr, sg, sb;
				stick_ink(s->colour, 0.85, &sr, &sg, &sb);
				cairo_rectangle(cr, hx0 + w1 * col[i], y0 + rh * 0.15, w1 * 0.9,
					y1 - y0 - rh * 0.30);
				cairo_set_source_rgb(cr, sr, sg, sb);
				cairo_fill(cr);
			}
		} else if (untimed < 6) {
			if (close) {
				stick_note(cr, s, rx0, gy + rh * (untimed + 1) - rh * 0.88, rx1 - rx0,
					rh * 0.82, rh * 0.40);
			} else {
				double sr, sg, sb;
				stick_ink(s->colour, 0.85, &sr, &sg, &sb);
				cairo_rectangle(cr, rx0, gy + rh * untimed + rh * 0.15, (rx1 - rx0) * 0.9,
					rh * 0.70);
				cairo_set_source_rgb(cr, sr, sg, sb);
				cairo_fill(cr);
			}
			untimed++;
		}
	}
	/* those with no room: in by NOTES */
	day_seen(yr, m, d, rx0 + rh * 2.6, gy + rh * 0.40, rh * 0.30);
	for (int i = 8; i < hours; i++) {
		double y = gy + rh * (i + 1);
		double bs = rh * 0.42;
		if (i - 8 < nt) {
			/* a thing to do stuck in its place */
			const struct desk_stick *s = &todo[i - 8];
			if (close) {
				stick_note(cr, s, rx0, y - rh * 0.90, rx1 - rx0, rh * 0.84, rh * 0.40);
			} else {
				double sr, sg, sb;
				stick_ink(s->colour, s->done ? 0.65 : 0.85, &sr, &sg, &sb);
				cairo_rectangle(cr, rx0, y - rh * 0.85, (rx1 - rx0) * 0.9, rh * 0.70);
				cairo_set_source_rgb(cr, sr, sg, sb);
				cairo_fill(cr);
			}
			continue;
		}
		cairo_set_source_rgba(cr, 0.40, 0.37, 0.33, 0.70);
		cairo_set_line_width(cr, fmax(h * 0.0018, 0.6));
		cairo_rectangle(cr, rx0, y - rh * 0.25 - bs, bs, bs);
		cairo_stroke(cr);
		cairo_set_source_rgba(cr, 0.55, 0.50, 0.44, 0.42);
		cairo_set_line_width(cr, fmax(h * 0.0014, 0.5));
		cairo_move_to(cr, rx0 + bs * 1.6, y);
		cairo_line_to(cr, rx1, y);
		cairo_stroke(cr);
	}
}

/* The page of section view for the day y-m-d, w by h. close: as it is seen
 * close up -- more of it, finer; on the shelf a plainer page that still
 * reads as one at a few dozen pixels. wire: the wire through its holes,
 * for the shelf, where the page is printed over the traced one. */
static void cal_page_paint(cairo_t *cr, double w, double h, int view, int y, int m, int d,
	const struct tm *today, bool close, bool wire)
{
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);
	cal_paper(cr, w, h, wire);
	switch (view) {
	case DESK_CAL_YEAR:
		cal_page_year(cr, w, h, y, m, today, close);
		break;
	case DESK_CAL_WEEK:
		cal_page_week(cr, w, h, y, m, d, today, close);
		break;
	case DESK_CAL_DAY:
		cal_page_day(cr, w, h, y, m, d, today, close);
		break;
	default:
		cal_page_month(cr, w, h, y, m, today, close);
		break;
	}
	cairo_restore(cr);
}

/* ---- the tabs ----
 *
 * Card, a colour to each section, standing out from the right edge of
 * every page of it at a height of its own, its name set sideways along it.
 * A tab is on a page, and goes where the page goes: drawn here on a page
 * hung from the wire at angle a (see the calendar close up, below), so the
 * tabs of a page going over go over with it, and those of the sections
 * turned to the back stand out past the edge of the board behind. */

#define CAL_TAB_Y0 0.13     /* the first tab's top, down the page */
#define CAL_TAB_H 0.185     /* each tab's length */
#define CAL_TAB_STEP 0.21   /* and from one to the next */
#define CAL_EYE 0.30
#define CAL_REST (2.0 * M_PI - CAL_TB)   /* turned over: lying on the back board */

static void cal_tab_ink(int v, double *r, double *g, double *b)
{
	static const double c[DESK_CAL_VIEWS][3] = {
		{ 0.60, 0.21, 0.17 },   /* the year: brick */
		{ 0.21, 0.31, 0.49 },   /* the months: navy */
		{ 0.25, 0.43, 0.31 },   /* the weeks: green */
		{ 0.76, 0.57, 0.21 },   /* the days: ochre */
	};
	*r = c[v][0];
	*g = c[v][1];
	*b = c[v][2];
}

/* Tab v on a page cx across its middle, pw by ph, hung from ay at angle a;
 * tw how far a tab stands out; shade, how much darker it is for being
 * under a page or behind the board; out, how much further it is drawn out
 * (the pointer on it). */
static void cal_tab(cairo_t *cr, int v, double cx, double ay, double pw, double ph, double tw,
	double a, double shade, double out)
{
	double s = cos(a - CAL_EYE), bulge = 0.10 * sin(a);
	bool face = cos(a + CAL_EYE) > 0.0;
	double u0 = CAL_TAB_Y0 + v * CAL_TAB_STEP, u1 = u0 + CAL_TAB_H;
	double k0 = 1.0 + bulge * u0, k1 = 1.0 + bulge * u1;
	double e0 = cx + pw * 0.5 * k0, e1 = cx + pw * 0.5 * k1;
	double y0 = ay + s * ph * u0, y1 = ay + s * ph * u1;
	if (fabs(y1 - y0) < 1.0) {
		return;
	}
	double t0 = tw * (1.0 + out) * k0, t1 = tw * (1.0 + out) * k1;
	double ov = tw * 0.35;   /* the part of it under the page's edge */
	double sg = y1 > y0 ? 1.0 : -1.0;
	double rad = fmin(fabs(y1 - y0) * 0.16, tw * 0.30);
	double r, g, b;
	cal_tab_ink(v, &r, &g, &b);
	cairo_save(cr);
	cairo_new_path(cr);
	cairo_move_to(cr, e0 - ov, y0);
	cairo_line_to(cr, e0 + t0 - rad, y0);
	cairo_curve_to(cr, e0 + t0 - rad * 0.45, y0, e0 + t0, y0 + rad * 0.45 * sg,
		e0 + t0, y0 + rad * sg);
	cairo_line_to(cr, e1 + t1, y1 - rad * sg);
	cairo_curve_to(cr, e1 + t1, y1 - rad * 0.45 * sg, e1 + t1 - rad * 0.45, y1,
		e1 + t1 - rad, y1);
	cairo_line_to(cr, e1 - ov, y1);
	cairo_close_path(cr);
	cairo_path_t *path = cairo_copy_path(cr);

	/* its shadow, down and right of it */
	cairo_save(cr);
	cairo_translate(cr, tw * 0.05, tw * 0.07);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.22 * (1.0 - fmin(shade, 0.5)));
	cairo_fill(cr);
	cairo_restore(cr);

	cairo_new_path(cr);
	cairo_append_path(cr, path);
	double lit = (face ? 1.0 : 0.80) * (1.0 + 0.30 * out);
	cairo_pattern_t *pat = cairo_pattern_create_linear(e0, 0, e0 + t0, 0);
	cairo_pattern_add_color_stop_rgb(pat, 0.0, r * lit * 0.86, g * lit * 0.86, b * lit * 0.86);
	cairo_pattern_add_color_stop_rgb(pat, 0.6, r * lit, g * lit, b * lit);
	cairo_pattern_add_color_stop_rgb(pat, 1.0, r * lit * 1.08, g * lit * 1.08, b * lit * 1.08);
	cairo_set_source(cr, pat);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(pat);
	double dark = clampf((face ? 0.18 * fabs(sin(a)) : 0.28) + shade, 0.0, 0.85);
	if (dark > 0.002) {
		cairo_set_source_rgba(cr, 0, 0, 0, dark);
		cairo_fill_preserve(cr);
	}
	cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
	cairo_set_line_width(cr, fmax(tw * 0.025, 0.6));
	cairo_stroke(cr);
	cairo_path_destroy(path);

	/* its name, sideways along it, reading down */
	if (face && fabs(s) > 0.30) {
		double sz = tw * 0.40, track = sz * 0.22;
		double len = cal_spaced_w(cr, TR(CAL_TAB_NAME[v]), sz, track, true);
		/* a long name (SEMAINE) set smaller, as it would be to fit */
		double room = fabs(y1 - y0) / fabs(s) * 0.84;
		if (len > room) {
			sz *= room / len;
			track *= room / len;
			len = room;
		}
		double mx = 0.5 * ((e0 + t0 * 0.5) + (e1 + t1 * 0.5));
		cairo_translate(cr, mx, 0.5 * (y0 + y1));
		cairo_scale(cr, 1.0, s);
		cairo_rotate(cr, M_PI / 2.0);
		double ink = 1.0 - dark;
		cal_spaced(cr, TR(CAL_TAB_NAME[v]), -len * 0.5, sz * 0.36, sz, track, true,
			0.97 * ink, 0.94 * ink, 0.86 * ink);
	}
	cairo_restore(cr);
}

static void gadget_calendar(cairo_t *cr, double w, double h, time_t now)
{
	struct tm tmv;
	localtime_r(&now, &tmv);
	double pw = w / desk_calendar_face_ratio(), tw = w - pw;
	int view = g_cal_view;
	/* the tabs of the sections behind and under, then the page over them,
	 * then its own tab */
	for (int v = 0; v < view; v++) {
		cal_tab(cr, v, pw * 0.5, 0.0, pw, h, tw, CAL_REST, 0.22, 0.0);
	}
	for (int v = DESK_CAL_VIEWS - 1; v > view; v--) {
		cal_tab(cr, v, pw * 0.5, 0.0, pw, h, tw, 0.0, 0.10, 0.0);
	}
	cal_page_paint(cr, pw, h, view, tmv.tm_year + 1900, tmv.tm_mon, tmv.tm_mday, &tmv,
		false, true);
	cal_tab(cr, view, pw * 0.5, 0.0, pw, h, tw, 0.0, 0.0, 0.0);
}

/* ---- the calendar close up ----------------------------------------
 *
 * Taken off the shelf and looked at: the same pages, face on and large,
 * on the leather board, the wire across the top -- turned over the wire a
 * page at a time, or a section at a time by its tab, the pages between
 * going over together in a bundle. A page turns about the wire. a, its
 * angle, is 0 hanging down in front, a quarter turn sticking out at you,
 * half a turn straight up, and on round behind to rest on the back board
 * (CAL_REST); the eye is a little above (CAL_EYE), so a page lying out flat
 * is still seen, and the nearer edge of one coming up is a little wider. */

void desk_calendar_rect(int w, int h, double page[4], double box[4])
{
	double asp = cal_page_w() / cal_page_h();
	double ph = fmin(h * 0.58, w * 0.62 / asp);
	double pw = ph * asp;
	double tw = pw * CAL_TAB / cal_page_w();
	/* the page and its tabs together in the middle */
	page[0] = floor((w - pw - tw) * 0.5);
	page[1] = floor(h * 0.53 - ph * 0.5);
	page[2] = ceil(pw);
	page[3] = ceil(ph);
	/* and everything that is drawn: the board, the tabs drawn out and the
	 * shadow, and above, as high as a page turning over the wire goes */
	double pad = pw * 0.07;
	box[0] = floor(page[0] - pad);
	box[1] = 0.0;
	box[2] = ceil(pw + tw * 1.35 + 2.0 * pad);
	box[3] = ceil(page[1] + ph * 1.10 + pad);
}

/* the board behind the pages, round them */
static void cal_board(const double pg[4], double b[4])
{
	b[0] = pg[0] - pg[2] * 0.034;
	b[1] = pg[1] - pg[3] * 0.050;
	b[2] = pg[2] * 1.068;
	b[3] = pg[3] * 1.112;
}

/* the head of a page close up ends this far down it (cal_head, close) */
#define CAL_HEAD_END (0.120 + 0.180 * 1.06 + 0.012)

int desk_calendar_hit(int w, int h, const struct desk_cal_look *lk, double x, double y,
	int *oy, int *om, int *od)
{
	double pg[4], box[4], b[4];
	desk_calendar_rect(w, h, pg, box);
	cal_board(pg, b);
	double tw = pg[2] * CAL_TAB / cal_page_w();
	int view = lk->view;
	/* the tabs first: they stand out past the board, those of the
	 * sections turned to the back drawn up shorter */
	double ex = pg[0] + pg[2];
	if (x >= ex - tw * 0.10 && x <= ex + tw * 1.35) {
		double back = cos(CAL_REST - CAL_EYE);
		for (int v = 0; v < DESK_CAL_VIEWS; v++) {
			double k = v < view ? back : 1.0;
			double u0 = CAL_TAB_Y0 + v * CAL_TAB_STEP, u1 = u0 + CAL_TAB_H;
			if (y >= pg[1] + pg[3] * u0 * k && y <= pg[1] + pg[3] * u1 * k) {
				return DESK_CAL_HIT_TAB + v;
			}
		}
	}
	double m = pg[2] * 0.02;
	if (x < b[0] - m || x > b[0] + b[2] + m || y < b[1] - m || y > b[1] + b[3] + m) {
		return 0;
	}
	/* the head of the page and over it, back; its foot, on */
	double u = (x - pg[0]) / pg[2], v = (y - pg[1]) / pg[3];
	double head = CAL_HEAD_END, gx = 0.045, gw = 0.910, gb = 0.962;
	if (v < head) {
		return DESK_CAL_HIT_PREV;
	}
	if (v > gb) {
		return DESK_CAL_HIT_NEXT;
	}
	if (u < gx || u > gx + gw) {
		return DESK_CAL_HIT_PAGE;
	}
	int yr = lk->year, mo = lk->month;
	long z = cal_daynum(yr, mo, lk->day);
	switch (view) {
	case DESK_CAL_MONTH: {
		double wh = 0.052;
		int f = cal_first(yr, mo), n = cal_days(yr, mo), rows = (f + n + 6) / 7;
		double cw = gw / 7.0, rh = (gb - head - wh) / rows;
		int col = (int)floor((u - gx) / cw), row = (int)floor((v - head - wh) / rh);
		if (v < head + wh || col < 0 || col > 6 || row < 0 || row >= rows) {
			return DESK_CAL_HIT_PAGE;
		}
		cal_civil(cal_daynum(yr, mo, 1) + row * 7 + col - f, oy, om, od);
		return DESK_CAL_HIT_DATE;
	}
	case DESK_CAL_WEEK: {
		int col = (int)floor((u - gx) / (gw / 7.0));
		col = col < 0 ? 0 : (col > 6 ? 6 : col);
		cal_civil(z - cal_wday(z) + col, oy, om, od);
		return DESK_CAL_HIT_DATE;
	}
	case DESK_CAL_DAY: {
		double rh = (gb - head) / 13.0;
		*oy = yr;
		*om = mo;
		*od = lk->day;
		return u > gx + gw * 0.63 && v > head + rh * 7.0 ? DESK_CAL_HIT_TODO
			: DESK_CAL_HIT_DATE;
	}
	default: {
		/* the year: a month's name, or one of its days */
		double cw = gw / 4.0, ch = (gb - head) / 3.0;
		int col = (int)floor((u - gx) / cw), row = (int)floor((v - head) / ch);
		if (col < 0 || col > 3 || row < 0 || row > 2) {
			return DESK_CAL_HIT_PAGE;
		}
		int k = row * 4 + col;
		double mx = gx + cw * col + cw * 0.08, my = head + ch * row + ch * 0.05;
		double mw = cw * 0.84, mh = ch * 0.88;
		*oy = yr;
		*om = k;
		*od = 1;
		double gy2 = my + mh * 0.305, rh2 = (my + mh - gy2) / 6.0, cw2 = mw / 7.0;
		int c2 = (int)floor((u - mx) / cw2), r2 = (int)floor((v - gy2) / rh2);
		int f = cal_first(yr, k), n = cal_days(yr, k);
		int d = r2 * 7 + c2 - f + 1;
		if (v >= gy2 && c2 >= 0 && c2 < 7 && r2 >= 0 && r2 < 6 && d >= 1 && d <= n) {
			*od = d;
			return DESK_CAL_HIT_DATE;
		}
		return DESK_CAL_HIT_MONTH;
	}
	}
}

int desk_calendar_hour_at(int w, int h, const struct desk_cal_look *lk, double x,
	double y)
{
	if (lk->view != DESK_CAL_DAY) {
		return -1;
	}
	double pg[4], box[4];
	desk_calendar_rect(w, h, pg, box);
	double u = (x - pg[0]) / pg[2], v = (y - pg[1]) / pg[3];
	double head = CAL_HEAD_END, gx = 0.045, gw = 0.910, gb = 0.962;
	if (u < gx || u > gx + gw * 0.58 || v < head || v >= gb) {
		return -1;
	}
	return 8 + (int)floor((v - head) / ((gb - head) / 13.0));
}

bool desk_shelf_calendar_hit(int w, int h, double x, double y)
{
	double b[4];
	if (!desk_shelf_gadget_box(w, h, DESK_GADGET_CALENDAR, b)) {
		return false;
	}
	double mx = b[2] * 0.06, my = b[3] * 0.10;
	return x >= b[0] - mx && x <= b[0] + b[2] + mx && y >= b[1] - my && y <= b[1] + b[3] + my;
}

/* A page: of which section, about which day. */
struct cal_pg {
	int view, y, m, d;
};

/* Pages as pictures, at the size they are drawn close up: the one showing
 * and those either side of it, for as long as nothing about them changes.
 * What a page shows of the day it is about: a year's page the year (and
 * the month the calendar was at), a month's the month, a week's the week. */
static cairo_surface_t *cal_page_image(const struct cal_pg *p, int w, int h,
	const struct tm *today)
{
	enum { N = 6 };
	static struct {
		long key;
		int w, h, yday, year, lang;
		unsigned used, gen;
		int hide[DESK_CAL_HIDE];
		cairo_surface_t *img;
	} cache[N];
	static unsigned clock;
	long z = cal_daynum(p->y, p->m, p->d);
	long key;
	switch (p->view) {
	case DESK_CAL_YEAR:
		key = (long)p->y * 12 + p->m;
		break;
	case DESK_CAL_MONTH:
		key = (long)p->y * 12 + p->m;
		break;
	case DESK_CAL_WEEK:
		key = z - cal_wday(z);
		break;
	default:
		key = z;
		break;
	}
	key = key * DESK_CAL_VIEWS + p->view;
	for (int i = 0; i < N; i++) {
		if (cache[i].img && cache[i].key == key && cache[i].w == w && cache[i].h == h
			&& cache[i].yday == today->tm_yday && cache[i].year == today->tm_year
			&& cache[i].lang == (int)desk_lang_get()
			&& cache[i].gen == desk_agenda_generation()
			&& memcmp(cache[i].hide, g_cal_hide, sizeof g_cal_hide) == 0) {
			cache[i].used = ++clock;
			return cache[i].img;
		}
	}
	/* an empty place, or the one looked at longest ago */
	int old = 0;
	for (int i = 0; i < N; i++) {
		if (!cache[i].img) {
			old = i;
			break;
		}
		if (cache[i].used < cache[old].used) {
			old = i;
		}
	}
	if (cache[old].img) {
		cairo_surface_destroy(cache[old].img);
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *c = cairo_create(img);
	cal_page_paint(c, w, h, p->view, p->y, p->m, p->d, today, true, false);
	cairo_destroy(c);
	cairo_surface_flush(img);
	cache[old].img = img;
	cache[old].key = key;
	cache[old].w = w;
	cache[old].h = h;
	cache[old].yday = today->tm_yday;
	cache[old].year = today->tm_year;
	cache[old].lang = (int)desk_lang_get();
	cache[old].gen = desk_agenda_generation();
	memcpy(cache[old].hide, g_cal_hide, sizeof g_cal_hide);
	cache[old].used = ++clock;
	return img;
}

/* A page at angle a, hung from the wire at ay. In strips, each as wide as
 * that part of the page stands near: a picture can only be turned and
 * stretched, and a page coming up at you is wider at its free edge. */
static void cal_turning(cairo_t *cr, cairo_surface_t *img, double px, double ay,
	double pw, double ph, double a)
{
	double s = cos(a - CAL_EYE);           /* its length on screen, down from the wire */
	bool face = cos(a + CAL_EYE) > 0.0;    /* the printed side towards you */
	double bulge = 0.10 * sin(a);
	double cx = px + pw * 0.5;
	if (fabs(s) * ph < 0.75) {
		/* edge on: a line */
		cairo_set_source_rgba(cr, 0.55, 0.52, 0.47, 0.9);
		cairo_rectangle(cr, px, ay - 0.5, pw, 1.0);
		cairo_fill(cr);
		return;
	}
	int iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
	const int N = 40;
	double dir = s > 0.0 ? 1.0 : -1.0;
	double behind = a > M_PI ? 0.15 : 0.0;
	for (int i = 0; i < N; i++) {
		double u0 = (double)i / N, u1 = (double)(i + 1) / N;
		double y0 = ay + s * ph * u0, y1 = ay + s * ph * u1;
		double k0 = 1.0 + bulge * u0, k1 = 1.0 + bulge * u1, km = 0.5 * (k0 + k1);
		cairo_save(cr);
		/* a hair of overlap, or the strips show as lines */
		double o = i + 1 < N ? 0.6 * dir : 0.0;
		cairo_move_to(cr, cx - pw * 0.5 * k0, y0);
		cairo_line_to(cr, cx + pw * 0.5 * k0, y0);
		cairo_line_to(cr, cx + pw * 0.5 * k1, y1 + o);
		cairo_line_to(cr, cx - pw * 0.5 * k1, y1 + o);
		cairo_close_path(cr);
		cairo_clip(cr);
		double sy = (y1 - y0) / ((u1 - u0) * ih);
		if (face) {
			cairo_translate(cr, cx - pw * 0.5 * km, y0 - u0 * ih * sy);
			cairo_scale(cr, pw * km / iw, sy);
			cairo_set_source_surface(cr, img, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			cairo_paint(cr);
		} else {
			/* the back of it: plain, the page showing through faintly */
			cairo_set_source_rgb(cr, 0.930, 0.918, 0.890);
			cairo_paint(cr);
			cairo_translate(cr, cx + pw * 0.5 * km, y0 - u0 * ih * sy);
			cairo_scale(cr, -pw * km / iw, sy);
			cairo_set_source_surface(cr, img, 0, 0);
			cairo_paint_with_alpha(cr, 0.07);
		}
		/* and in the light as it stands: darker as it turns away from the
		 * lamp, the back darker still */
		double shade = (face ? 0.20 * fabs(sin(a)) * u1 : 0.10 + 0.14 * fabs(sin(a))) + behind;
		cairo_identity_matrix(cr);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, shade);
		cairo_paint(cr);
		cairo_restore(cr);
	}
}

/* One of the sheets in a bundle, its print not seen: plain paper, the line
 * of its free edge showing against the next. */
static void cal_plain(cairo_t *cr, double px, double ay, double pw, double ph, double a)
{
	double s = cos(a - CAL_EYE);
	if (fabs(s) * ph < 0.75) {
		return;
	}
	bool face = cos(a + CAL_EYE) > 0.0;
	double cx = px + pw * 0.5, k1 = 1.0 + 0.10 * sin(a), ye = ay + s * ph;
	cairo_save(cr);
	cairo_move_to(cr, cx - pw * 0.5, ay);
	cairo_line_to(cr, cx + pw * 0.5, ay);
	cairo_line_to(cr, cx + pw * 0.5 * k1, ye);
	cairo_line_to(cr, cx - pw * 0.5 * k1, ye);
	cairo_close_path(cr);
	if (face) {
		cairo_set_source_rgb(cr, 0.950, 0.942, 0.920);
	} else {
		cairo_set_source_rgb(cr, 0.928, 0.916, 0.888);
	}
	cairo_fill_preserve(cr);
	double shade = (face ? 0.16 * fabs(sin(a)) : 0.10 + 0.14 * fabs(sin(a)))
		+ (a > M_PI ? 0.15 : 0.0);
	cairo_set_source_rgba(cr, 0, 0, 0, shade);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.32, 0.29, 0.25, 0.55);
	cairo_set_line_width(cr, 1.0);
	cairo_move_to(cr, cx - pw * 0.5 * k1, ye);
	cairo_line_to(cr, cx + pw * 0.5 * k1, ye);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* A sheet in the air: its angle, its picture (NULL: plain), the tab it
 * carries (-1: none). */
struct cal_fly {
	double a;
	cairo_surface_t *img;
	int tab;
};

static void cal_fly_draw(cairo_t *cr, const struct cal_fly *f, double px, double ay,
	double pw, double ph, double tw, int hover)
{
	if (f->img) {
		cal_turning(cr, f->img, px, ay, pw, ph, f->a);
	} else {
		cal_plain(cr, px, ay, pw, ph, f->a);
	}
	if (f->tab >= 0) {
		cal_tab(cr, f->tab, px + pw * 0.5, ay, pw, ph, tw, f->a, f->a > M_PI ? 0.22 : 0.0,
			f->tab == hover ? 0.22 : 0.0);
	}
}

/* the sheets in the air, in the order they are drawn: the farthest from
 * the eye first */
static void cal_fly_order(const struct cal_fly *f, int n, int *idx)
{
	for (int i = 0; i < n; i++) {
		idx[i] = i;
	}
	for (int i = 1; i < n; i++) {
		int k = idx[i], j = i - 1;
		while (j >= 0 && sin(f[idx[j]].a) > sin(f[k].a)) {
			idx[j + 1] = idx[j];
			j--;
		}
		idx[j + 1] = k;
	}
}

static void cal_draw_flat(cairo_t *cr, int w, int h, const struct desk_cal_look *lk,
	time_t now)
{
	double pg[4], box[4], bd[4];
	desk_calendar_rect(w, h, pg, box);
	cal_board(pg, bd);
	double px = pg[0], py = pg[1], pw = pg[2], ph = pg[3];
	double cx = px + pw * 0.5, tw = pw * CAL_TAB / cal_page_w();
	struct tm tmv;
	localtime_r(&now, &tmv);
	int iw = (int)pw, ih = (int)ph;
	int view = clampf(lk->view, 0, DESK_CAL_VIEWS - 1);
	int hover = lk->hover_tab;
	/* the notes not on the page just now */
	memcpy(g_cal_hide, lk->hide, sizeof g_cal_hide);

	/* What lies flat, what is in the air and at what angle -- and which
	 * sections' tabs stand out from behind the board, which from under the
	 * page, and which is the flat page's own. */
	struct cal_pg cur = { view, lk->year, lk->month, lk->day };
	struct cal_pg flat = cur;
	struct cal_fly fly[24];
	int nf = 0;
	int back_below = view, under_above = view, own = view;
	double t = clampf(lk->turn_t, 0.0, 1.0);
	t = t * t * (3.0 - 2.0 * t);
	if (lk->to_view != view && lk->to_view >= 0 && lk->to_view < DESK_CAL_VIEWS) {
		/* by a tab: the sections between going over together */
		int to = lk->to_view, n = abs(to - view);
		bool fwd = to > view;
		double st = clampf(lk->switch_t, 0.0, 1.0);
		st = st * st * (3.0 - 2.0 * st);
		double a = fwd ? CAL_REST * st : CAL_REST * (1.0 - st);
		double spread = (0.20 + 0.10 * n) * sin(M_PI * st);
		int sheets = 3 + 5 * n;
		struct cal_pg top = fwd ? cur : (struct cal_pg){ to, cur.y, cur.m, cur.d };
		if (fwd) {
			flat = (struct cal_pg){ to, cur.y, cur.m, cur.d };
			back_below = view;
			under_above = to;
			own = to;
		} else {
			back_below = to;
			under_above = view;
			own = view;
		}
		/* fanned a little apart as they go, the printed one outermost:
		 * going over it leads, coming back it lands last */
		for (int i = 0; i < sheets; i++) {
			fly[nf].a = a + spread * (0.5 - (double)i / (sheets - 1));
			fly[nf].img = i == 0 ? cal_page_image(&top, iw, ih, &tmv) : NULL;
			fly[nf].tab = -1;
			nf++;
		}
		/* each section's tab on a sheet of its own, the first on top */
		int first = fwd ? view : to;
		for (int j = 0; j < n; j++) {
			fly[j * (sheets - 1) / n].tab = first + j;
		}
	} else if (lk->turn) {
		struct cal_pg other = cur;
		desk_calendar_step(view, lk->turn, &other.y, &other.m, &other.d);
		if (lk->turn > 0) {
			flat = other;
			fly[nf++] = (struct cal_fly){ CAL_REST * t, cal_page_image(&cur, iw, ih, &tmv), view };
		} else {
			fly[nf++] = (struct cal_fly){ CAL_REST * (1.0 - t),
				cal_page_image(&other, iw, ih, &tmv), view };
		}
	} else if (lk->lift > 0 && lk->lift_t > 0.0) {
		/* a finger under its foot */
		struct cal_pg other = cur;
		desk_calendar_step(view, 1, &other.y, &other.m, &other.d);
		flat = other;
		fly[nf++] = (struct cal_fly){ 0.52 * lk->lift_t, cal_page_image(&cur, iw, ih, &tmv), view };
	} else if (lk->lift < 0 && lk->lift_t > 0.0) {
		/* the last one, drawn up a little behind the wire */
		struct cal_pg other = cur;
		desk_calendar_step(view, -1, &other.y, &other.m, &other.d);
		fly[nf++] = (struct cal_fly){ CAL_REST - (CAL_REST - 4.905) * lk->lift_t,
			cal_page_image(&other, iw, ih, &tmv), view };
	}
	int order[24];
	cal_fly_order(fly, nf, order);

	/* its shadow on whatever is behind it */
	cairo_save(cr);
	for (int i = 0; i < 10; i++) {
		double g = i * pw * 0.006;
		desk_rounded(cr, bd[0] - g + pw * 0.010, bd[1] - g + ph * 0.030,
			bd[2] + 2.0 * g, bd[3] + 2.0 * g, pw * 0.012 + g);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.055);
		cairo_fill(cr);
	}
	cairo_restore(cr);

	/* behind the board: the tabs of the sections turned to the back, and
	 * whatever in the air has gone over */
	for (int v = 0; v < back_below; v++) {
		cal_tab(cr, v, cx, py, pw, ph, tw, CAL_REST, 0.22, v == hover ? 0.22 : 0.0);
	}
	for (int i = 0; i < nf; i++) {
		if (fly[order[i]].a > M_PI) {
			cal_fly_draw(cr, &fly[order[i]], px, py, pw, ph, tw, hover);
		}
	}

	/* the board; the tabs of the sections under this one; the pages at
	 * the foot of the stack, and the top one */
	leather_fill(cr, bd[0], bd[1], bd[2], bd[3], pw * 0.012);
	for (int v = DESK_CAL_VIEWS - 1; v > under_above; v--) {
		cal_tab(cr, v, cx, py, pw, ph, tw, 0.0, 0.10, v == hover ? 0.22 : 0.0);
	}
	for (int i = 5; i >= 1; i--) {
		double k = ph / 420.0;
		cairo_rectangle(cr, px + 0.5, py + ph - k + i * 1.2 * k, pw - 1.0, 1.6 * k);
		cairo_set_source_rgb(cr, 0.90 - 0.035 * i, 0.89 - 0.035 * i, 0.86 - 0.035 * i);
		cairo_fill(cr);
	}
	cairo_save(cr);
	cairo_translate(cr, px, py);
	cairo_scale(cr, pw / iw, ph / ih);
	cairo_set_source_surface(cr, cal_page_image(&flat, iw, ih, &tmv), 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
	cal_tab(cr, own, cx, py, pw, ph, tw, 0.0, 0.0, own == hover ? 0.22 : 0.0);

	/* in front: what is in the air and not yet over, and the shadow the
	 * nearest of it throws on the page under it, along its free edge */
	double lowest = -1.0;
	for (int i = 0; i < nf; i++) {
		if (fly[i].a <= M_PI && (lowest < 0.0 || fly[i].a < lowest)) {
			lowest = fly[i].a;
		}
	}
	if (lowest >= 0.0) {
		double s = cos(lowest - CAL_EYE), sa = sin(lowest);
		double ye = py + fmax(s, 0.0) * ph;
		double band = ph * (0.02 + 0.10 * sa);
		cairo_pattern_t *g = cairo_pattern_create_linear(0, ye, 0, ye + band);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 0, 0, 0, 0.30 * sa);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 0, 0, 0, 0.0);
		cairo_set_source(cr, g);
		cairo_rectangle(cr, px, ye, pw, band);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
	for (int i = 0; i < nf; i++) {
		if (fly[order[i]].a <= M_PI) {
			cal_fly_draw(cr, &fly[order[i]], px, py, pw, ph, tw, hover);
		}
	}

	/* the wire, over all of it: the front half of each loop, from its hole
	 * up over the top of the board */
	double hy = py + cal_hole_y() * ph;
	double top = py - ph * (CAL_RR - CAL_RY + CAL_PTOP) / cal_page_h();
	double lw = pw * (2.0 * CAL_RW + 0.010) / cal_page_w();
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int i = 0; i < CAL_LOOPS; i++) {
		double x = px + cal_loop_x(i) * pw;
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
	memset(g_cal_hide, 0, sizeof g_cal_hide);
}

/* ---- the calendar held up ----
 *
 * The flat drawing is its face: the board with the pages on it, the tabs,
 * the wire. Held up it is a tent of two boards seen a little turned: the
 * face laid into the view through the turn, and along the side that comes
 * into view the edges of the stack of pages and of the front board, the
 * back board's edge going away from the ridge to its foot, and between
 * them the dark inside of the tent. Lit from above and in front, by the
 * lamps over the bays. */

static void cal_turn3(int w, int h, const struct desk_cal_look *lk, struct turn3 *t)
{
	double pg[4], box[4];
	desk_calendar_rect(w, h, pg, box);
	t->px = pg[0] + pg[2] * 0.5;
	t->py = pg[1] + pg[3] * 0.5;
	t->yaw = lk->yaw;
	t->pitch = lk->pitch;
	if (lk->eye_f > 0.0) {
		t->ex = lk->eye_x;
		t->ey = lk->eye_y;
		t->f = lk->eye_f;
	} else {
		t->ex = w * 0.5;
		t->ey = h * 0.5;
		t->f = h * 1.6;
	}
}

static bool cal_held(const struct desk_cal_look *lk)
{
	return fabs(lk->yaw) > 1e-4 || fabs(lk->pitch) > 1e-4;
}

/* the tent in the close up's pixels: the board, how thick the stack of
 * pages and the board are, how long a board is and the angle between the
 * two at the ridge */
struct cal_tent_px {
	double bd[4], pg[4];
	double pz, bt, len, ang;
};

static void cal_tent_px(int w, int h, struct cal_tent_px *k)
{
	double box[4];
	desk_calendar_rect(w, h, k->pg, box);
	cal_board(k->pg, k->bd);
	const struct room_gadget *g = &ROOM_GADGETS[DESK_GADGET_CALENDAR];
	double ppu = k->pg[3] / (g->h - CAL_PTOP - CAL_PY0);
	k->pz = CAL_PZ * ppu;
	k->bt = 2.0 * CAL_T * ppu;
	k->len = k->bd[3];
	k->ang = g->tilt + CAL_TB;
}

/* What shows of its sides, before the face goes over them. */
static void cal_sides(cairo_t *cr, const struct turn3 *t, const struct cal_tent_px *k)
{
	double top = k->bd[1], bot = k->bd[1] + k->bd[3];
	double zb = k->pz, zb2 = k->pz + k->bt;
	/* the back board, from the ridge down and away to its foot */
	double fy = top + k->len * cos(k->ang), fz = zb2 + k->len * sin(k->ang);
	for (int side = 0; side < 2; side++) {
		double x = side ? k->bd[0] + k->bd[2] : k->bd[0];
		double nx = side ? 1.0 : -1.0;
		if (!turn3_facing(t, x, (top + bot) * 0.5, zb2, nx, 0.0, 0.0)) {
			continue;
		}
		/* the dark inside of the tent */
		double in[3][3] = { { x, top, zb2 }, { x, bot, zb2 }, { x, fy, fz } };
		held_poly(cr, t, in, 3);
		cairo_set_source_rgba(cr, 0.05, 0.035, 0.025, 0.92);
		cairo_fill(cr);
		/* the back board's edge, going away */
		double ca = cos(k->ang), sa = sin(k->ang);
		double bb[4][3] = { { x, top, zb2 }, { x, fy, fz },
			{ x, fy - sa * k->bt, fz + ca * k->bt }, { x, top - sa * k->bt, zb2 + ca * k->bt } };
		held_poly(cr, t, bb, 4);
		held_leather_face(cr, t, nx, 0.0, 0.0, 1.15);
		/* the front board's edge */
		double fb[4][3] = { { x, top, zb }, { x, bot, zb }, { x, bot, zb2 }, { x, top, zb2 } };
		held_poly(cr, t, fb, 4);
		held_leather_face(cr, t, nx, 0.0, 0.0, 1.25);
		/* and the edges of the pages, a little in from it */
		double px = side ? k->pg[0] + k->pg[2] : k->pg[0];
		double pt = k->pg[1], pb = k->pg[1] + k->pg[3];
		double pe[4][3] = { { px, pt, 0.0 }, { px, pb, 0.0 }, { px, pb, zb }, { px, pt, zb } };
		held_poly(cr, t, pe, 4);
		double lit = turn3_lit(t, nx, 0.0, 0.0, HELD_LIGHT_X, HELD_LIGHT_Y, HELD_LIGHT_Z);
		double c = 0.62 + 0.30 * lit;
		cairo_set_source_rgb(cr, c, c * 0.98, c * 0.93);
		cairo_fill(cr);
		/* the leaves, as lines along the edge */
		cairo_set_source_rgba(cr, 0.30, 0.27, 0.22, 0.35);
		cairo_set_line_width(cr, 0.5);
		for (int i = 1; i < 6; i++) {
			double z = zb * i / 6.0;
			double ax, ay, bx, by;
			turn3_project(t, px, pt, z, &ax, &ay, NULL);
			turn3_project(t, px, pb, z, &bx, &by, NULL);
			cairo_move_to(cr, ax, ay);
			cairo_line_to(cr, bx, by);
		}
		cairo_stroke(cr);
	}
	/* the foot of the stack of pages, if it is seen from above */
	if (turn3_facing(t, k->pg[0] + k->pg[2] * 0.5, k->pg[1] + k->pg[3], zb * 0.5, 0.0, 1.0,
			0.0)) {
		double pb = k->pg[1] + k->pg[3];
		double fe[4][3] = { { k->pg[0], pb, 0.0 }, { k->pg[0] + k->pg[2], pb, 0.0 },
			{ k->pg[0] + k->pg[2], pb, zb }, { k->pg[0], pb, zb } };
		held_poly(cr, t, fe, 4);
		cairo_set_source_rgb(cr, 0.80, 0.78, 0.73);
		cairo_fill(cr);
	}
}

void desk_draw_calendar(cairo_t *cr, int w, int h, const struct desk_cal_look *lk,
	time_t now)
{
	if (!cal_held(lk)) {
		cal_draw_flat(cr, w, h, lk, now);
		return;
	}
	double pg[4], box[4];
	desk_calendar_rect(w, h, pg, box);
	/* the face, flat, at the size cr draws at */
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
	struct desk_cal_look flat = *lk;
	flat.yaw = flat.pitch = 0.0;
	cal_draw_flat(c, w, h, &flat, now);
	cairo_destroy(c);

	struct turn3 t;
	cal_turn3(w, h, lk, &t);
	struct cal_tent_px tp;
	cal_tent_px(w, h, &tp);
	cal_sides(cr, &t, &tp);
	struct hmat map;
	if (turn3_hmat(&t, box[0], box[1], box[2], box[3], &map)) {
		warp_paint(cr, img, box[0], box[1], k, &map, 1.0);
	}
	cairo_surface_destroy(img);

	/* the face in the light: as square to it as it is turned, and the
	 * lamps above, so the foot of it a little darker than the head */
	held_face_light(cr, &t, tp.bd[0], tp.bd[1], tp.bd[2], tp.bd[3], 0.10);
}

void desk_calendar_extent(int w, int h, const struct desk_cal_look *lk, double out[4])
{
	double pg[4], box[4];
	desk_calendar_rect(w, h, pg, box);
	if (!cal_held(lk)) {
		memcpy(out, box, sizeof box);
		return;
	}
	struct turn3 t;
	cal_turn3(w, h, lk, &t);
	struct cal_tent_px tp;
	cal_tent_px(w, h, &tp);
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	/* the face, and the tent behind it as deep as it goes */
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

void desk_calendar_turn_point(int w, int h, const struct desk_cal_look *lk, double *x,
	double *y)
{
	if (!cal_held(lk)) {
		return;
	}
	struct turn3 t;
	cal_turn3(w, h, lk, &t);
	turn3_point(&t, *x, *y, x, y, NULL);
}

void desk_calendar_unturn(int w, int h, const struct desk_cal_look *lk, double *x,
	double *y)
{
	if (!cal_held(lk)) {
		return;
	}
	struct turn3 t;
	cal_turn3(w, h, lk, &t);
	double pg[4], box[4];
	desk_calendar_rect(w, h, pg, box);
	struct hmat map, inv;
	if (turn3_hmat(&t, box[0], box[1], box[2], box[3], &map) && hmat_invert(&map, &inv)) {
		hmat_apply(&inv, *x, *y, x, y);
	}
}

/* ---- where the notes are on the page close up ---- */

static cairo_t *agd_measure(void);

/* Draw the page lk is at into nothing, noting down where the notes go and
 * where day y-m-d is (pg: where the page is close up). */
static void cal_probe(int w, int h, const struct desk_cal_look *lk, int y, int m, int d,
	double pg[4])
{
	double box[4];
	desk_calendar_rect(w, h, pg, box);
	memset(&g_probe, 0, sizeof g_probe);
	g_probe.on = true;
	g_probe.y = y;
	g_probe.m = m;
	g_probe.d = d;
	memcpy(g_cal_hide, lk->hide, sizeof g_cal_hide);
	time_t now = time(NULL);
	struct tm tmv;
	localtime_r(&now, &tmv);
	cairo_t *cr = agd_measure();
	cairo_save(cr);
	cal_page_paint(cr, (int)pg[2], (int)pg[3], clampf(lk->view, 0, DESK_CAL_VIEWS - 1),
		lk->year, lk->month, lk->day, &tmv, true, false);
	cairo_restore(cr);
	g_probe.on = false;
	memset(g_cal_hide, 0, sizeof g_cal_hide);
}

bool desk_calendar_note_pose(int w, int h, const struct desk_cal_look *lk,
	const struct desk_stick *s, struct desk_note_pose *pose)
{
	double pg[4];
	cal_probe(w, h, lk, s->y, s->m, s->d, pg);
	bool found = false;
	for (int i = 0; i < g_probe.n; i++) {
		if (g_probe.id[i] == s->id) {
			*pose = g_probe.pose[i];
			found = true;
			break;
		}
	}
	if (!found) {
		if (!g_probe.have_day) {
			return false;
		}
		*pose = g_probe.day;
	}
	pose->x += pg[0];
	pose->y += pg[1];
	return true;
}

int desk_calendar_note_at(int w, int h, const struct desk_cal_look *lk, double x, double y)
{
	double pg[4];
	cal_probe(w, h, lk, -1, -1, -1, pg);
	/* the one drawn last is on top */
	for (int i = g_probe.n - 1; i >= 0; i--) {
		const struct desk_note_pose *p = &g_probe.pose[i];
		bool away = false;
		for (int k = 0; k < DESK_CAL_HIDE; k++) {
			away |= lk->hide[k] == g_probe.id[i];
		}
		if (away) {
			continue;
		}
		/* into the note's own frame */
		double dx = x - (pg[0] + p->x + p->w * 0.5), dy = y - (pg[1] + p->y + p->h * 0.5);
		double c = cos(-p->rot), sn = sin(-p->rot);
		double u = dx * c - dy * sn, v = dx * sn + dy * c;
		if (fabs(u) <= p->w * 0.5 && fabs(v) <= p->h * 0.5) {
			return g_probe.id[i];
		}
	}
	return 0;
}

bool desk_calendar_page_has(int view, int y, int m, int d, int yy, int mm, int dd)
{
	switch (view) {
	case DESK_CAL_YEAR:
		return y == yy;
	case DESK_CAL_MONTH:
		return y == yy && m == mm;
	case DESK_CAL_WEEK: {
		long a = cal_daynum(y, m, d), b = cal_daynum(yy, mm, dd);
		return a - cal_wday(a) == b - cal_wday(b);
	}
	default:
		return y == yy && m == mm && d == dd;
	}
}

/* ---- the binder of notes ------------------------------------------
 *
 * A day on the calendar clicked, the calendar moves over and a binder
 * comes in beside it: a planner of the kind with its rings down the
 * spine, open at that day. Inside its front cover, pads of sticky notes
 * in six colours; on the page, the day's plans and the things to do that
 * day, written down in pencil as they are stuck on the calendar. A note
 * is peeled off the pad picked and stuck in the space where it is to go,
 * written on, and taken over to the calendar; a line clicked, the note
 * comes back to be written again; with the other button, it comes off. */
