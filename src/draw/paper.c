/* Part of draw.c: paper that has been in the room a while, and window chrome.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ================================================================
 * Paper that has been in the room a while
 *
 * White paper is paper that arrived this morning. Everything else has gone
 * over -- first at the edges, where the air gets at it, then in blotches
 * from whatever it was stacked against, and in spots wherever a spore
 * landed. It is not flat either: a sheet that has been handled has a soft
 * crease in it and a corner that has stopped lying down.
 *
 * years: 0 for something printed today, 1 for something filed in 1974.
 * ================================================================ */

static void paper_age(cairo_t *cr, double w, double h, unsigned seed, double years)
{
	years = clampf(years, 0.0, 1.0);
	if (years < 0.002 || w < 4.0 || h < 4.0) {
		return;
	}
	double d = fmin(w, h), big = fmax(w, h);
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);

	/* the general yellowing, heavier towards the edges */
	cairo_pattern_t *ed = cairo_pattern_create_radial(w * 0.5, h * 0.5, d * 0.16,
		w * 0.5, h * 0.5, big * 0.76);
	cairo_pattern_add_color_stop_rgba(ed, 0.00, 0.780, 0.672, 0.432, 0.085 * years);
	cairo_pattern_add_color_stop_rgba(ed, 0.62, 0.712, 0.580, 0.336, 0.190 * years);
	cairo_pattern_add_color_stop_rgba(ed, 1.00, 0.584, 0.438, 0.222, 0.430 * years);
	cairo_set_source(cr, ed);
	cairo_paint(cr);
	cairo_pattern_destroy(ed);

	/* blotches: it spent years pressed against other paper */
	for (int i = 0; i < 5; i++) {
		double bx = hash21(seed + i * 3.7, 1.31) * w;
		double by = hash21(seed + i * 5.1, 8.93) * h;
		double br = d * (0.20 + hash21(seed + i * 2.3, 2.21) * 0.46);
		cairo_pattern_t *b = cairo_pattern_create_radial(bx, by, 0, bx, by, br);
		cairo_pattern_add_color_stop_rgba(b, 0.0, 0.652, 0.516, 0.282,
			0.11 * years * (0.4 + hash21(seed + i, 6.6) * 0.9));
		cairo_pattern_add_color_stop_rgba(b, 1.0, 0.652, 0.516, 0.282, 0.0);
		cairo_set_source(cr, b);
		/* only where it reaches: a gradient painted over the whole sheet
		 * is worked out for every pixel of it */
		cairo_rectangle(cr, bx - br, by - br, br * 2.0, br * 2.0);
		cairo_fill(cr);
		cairo_pattern_destroy(b);
	}

	/* foxing: small rust spots, and not very many of them */
	int spots = (int)(6.0 + years * 10.0);
	for (int i = 0; i < spots; i++) {
		double fx = hash21(seed * 1.7 + i * 9.13, 3.77) * w;
		double fy = hash21(seed * 2.3 + i * 4.41, 7.19) * h;
		double fr = d * (0.006 + hash21(seed + i * 1.9, 5.5) * 0.016);
		cairo_pattern_t *f = cairo_pattern_create_radial(fx, fy, 0, fx, fy, fr);
		cairo_pattern_add_color_stop_rgba(f, 0.0, 0.460, 0.318, 0.176, 0.40 * years);
		cairo_pattern_add_color_stop_rgba(f, 0.5, 0.480, 0.352, 0.200, 0.22 * years);
		cairo_pattern_add_color_stop_rgba(f, 1.0, 0.500, 0.380, 0.220, 0.0);
		cairo_set_source(cr, f);
		cairo_rectangle(cr, fx - fr, fy - fr, fr * 2.0, fr * 2.0);
		cairo_fill(cr);
		cairo_pattern_destroy(f);
	}
	cairo_restore(cr);
}

/* The same, worked out at a third of the size and laid on stretched: it
 * is all soft gradients, which look the same either way, and a window's
 * sheet is large and drawn again whenever it changes size. */
/* desk_paper_age_hold: the size held, and the age last worked out for it */
static _Thread_local int g_age_hold_w, g_age_hold_h;
static _Thread_local struct {
	cairo_surface_t *s;
	int w, h;
	unsigned seed;
	double years;
} g_age_held;

void desk_paper_age_hold(int w, int h)
{
	/* (the picture is kept when let go of: the next frame of the glide
	 * holds the same size again, and a third of a sheet is small) */
	g_age_hold_w = w > 0 && h > 0 ? w : 0;
	g_age_hold_h = w > 0 && h > 0 ? h : 0;
}

static void paper_age_coarse(cairo_t *cr, double w, double h, unsigned seed, double years)
{
	const double k = 3.0;
	/* worked out for the size held, if one is (everything on it is placed
	 * and sized in proportion to the sheet, so stretched it is the same
	 * paper, give or take the round of a spot) */
	double aw = g_age_hold_w ? g_age_hold_w : w, ah = g_age_hold_w ? g_age_hold_h : h;
	int sw = (int)ceil(aw / k) + 1, sh = (int)ceil(ah / k) + 1;
	if (sw < 8 || sh < 8 || (int)ceil(w / k) + 1 < 8 || (int)ceil(h / k) + 1 < 8) {
		paper_age(cr, w, h, seed, years);
		return;
	}
	cairo_surface_t *s = NULL;
	if (g_age_hold_w && g_age_held.s && g_age_held.w == g_age_hold_w && g_age_held.h == g_age_hold_h
		&& g_age_held.seed == seed && g_age_held.years == years) {
		s = cairo_surface_reference(g_age_held.s);
	} else {
		s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
		cairo_t *c = cairo_create(s);
		cairo_scale(c, 1.0 / k, 1.0 / k);
		paper_age(c, aw, ah, seed, years);
		cairo_destroy(c);
		if (g_age_hold_w) {
			if (g_age_held.s) {
				cairo_surface_destroy(g_age_held.s);
			}
			g_age_held.s = cairo_surface_reference(s);
			g_age_held.w = g_age_hold_w;
			g_age_held.h = g_age_hold_h;
			g_age_held.seed = seed;
			g_age_held.years = years;
		}
	}
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);
	cairo_scale(cr, k * w / aw, k * h / ah);
	cairo_set_source_surface(cr, s, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_surface_destroy(s);
}

/* A typewriter ribbon that has been through the machine too many times.
 * The letters come out unevenly across the line, and in places they barely
 * come out at all -- so the paper colour is washed back over them in
 * patches rather than the ink being drawn at a lower alpha, which only
 * makes it grey and even. */
static void ink_skip(cairo_t *cr, double x, double y, double w, double h,
	unsigned seed, double amount, double pr, double pg, double pb)
{
	if (amount < 0.004 || w < 2.0 || h < 1.0) {
		return;
	}
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_clip(cr);
	for (int i = 0; i < 14; i++) {
		double px = x + hash21(seed + i * 3.13, 2.71) * w;
		double py = y + hash21(seed + i * 5.91, 9.43) * h;
		double r = h * (0.16 + hash21(seed + i * 1.7, 4.41) * 0.62);
		cairo_pattern_t *g = cairo_pattern_create_radial(px, py, 0, px, py, r);
		cairo_pattern_add_color_stop_rgba(g, 0.0, pr, pg, pb,
			amount * (0.35 + hash21(seed + i, 8.1) * 0.9));
		cairo_pattern_add_color_stop_rgba(g, 1.0, pr, pg, pb, 0.0);
		cairo_set_source(cr, g);
		cairo_paint(cr);
		cairo_pattern_destroy(g);
	}
	/* and it ran dry towards one end of the line */
	int lean = hash21(seed, 31.1) > 0.5;
	cairo_pattern_t *dry = cairo_pattern_create_linear(lean ? x : x + w, y,
		lean ? x + w : x, y);
	cairo_pattern_add_color_stop_rgba(dry, 0.0, pr, pg, pb, 0.0);
	cairo_pattern_add_color_stop_rgba(dry, 1.0, pr, pg, pb, amount * 0.85);
	cairo_set_source(cr, dry);
	cairo_paint(cr);
	cairo_pattern_destroy(dry);
	cairo_restore(cr);
}

/* A crease. Not a fold -- a crease is what is left after a fold has been
 * pressed out again: one side of the line catches the light and the other
 * is in its own shadow, and the line is never straight. */
static void paper_crease(cairo_t *cr, double w, double h, unsigned seed, double a)
{
	if (a < 0.004 || w < 6.0 || h < 6.0) {
		return;
	}
	int across = hash21(seed, 11.3) > 0.45;
	double t = 0.26 + hash21(seed, 13.7) * 0.48;
	double lw = fmax(1.0, fmin(w, h) * 0.006);
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);
	cairo_set_line_width(cr, lw);
	for (int side = 0; side < 2; side++) {
		double off = side ? lw * 0.9 : -lw * 0.9;
		if (side) {
			cairo_set_source_rgba(cr, 0.28, 0.23, 0.17, a * 0.85);
		} else {
			cairo_set_source_rgba(cr, 1.0, 0.98, 0.93, a * 0.95);
		}
		int n = 9;
		for (int i = 0; i <= n; i++) {
			double u = (double)i / n;
			double wob = (hash21(seed + i * 2.7, side * 3.1 + 19.0) - 0.5)
				* fmin(w, h) * 0.012;
			double x = across ? u * w : t * w + wob + off;
			double y = across ? t * h + wob + off : u * h;
			if (i) {
				cairo_line_to(cr, x, y);
			} else {
				cairo_move_to(cr, x, y);
			}
		}
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

/* A sheet is a solid, not a decal. The inside of its edge is a hair darker
 * than the middle because the light has to get past the thickness of it,
 * and one corner has stopped lying down. */
static void paper_edge(cairo_t *cr, double w, double h, unsigned seed, double curl)
{
	double d = fmin(w, h);
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);

	/* Thickness, as a band of shade running in from each edge. A stroked
	 * rectangle here reads as a printed border, which is the one thing a
	 * sheet of paper does not have. */
	double b = fmax(2.0, d * 0.045);
	struct { double x0, y0, x1, y1; } side[4] = {
		{ 0, 0, b, 0 }, { w, 0, w - b, 0 }, { 0, 0, 0, b }, { 0, h, 0, h - b },
	};
	for (int i = 0; i < 4; i++) {
		cairo_pattern_t *g = cairo_pattern_create_linear(
			side[i].x0, side[i].y0, side[i].x1, side[i].y1);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 0.268, 0.222, 0.166, 0.20);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 0.268, 0.222, 0.166, 0.0);
		cairo_set_source(cr, g);
		cairo_paint(cr);
		cairo_pattern_destroy(g);
	}

	if (curl > 0.01) {
		/* A corner that has stopped lying down. What you see is the BACK
		 * of the sheet, which is duller than the front and is lit from the
		 * wrong side -- plus the shadow the flap throws on the sheet. */
		int c = (int)(hash21(seed, 17.9) * 4.0) & 3;
		double sz = d * (0.10 + hash21(seed, 23.1) * 0.12) * curl;
		double cx = (c & 1) ? w : 0.0, cy = (c & 2) ? h : 0.0;
		double sx = (c & 1) ? -1.0 : 1.0, sy = (c & 2) ? -1.0 : 1.0;

		cairo_save(cr);
		cairo_translate(cr, sx * sz * 0.16, sy * sz * 0.16);
		cairo_move_to(cr, cx, cy + sy * sz);
		cairo_line_to(cr, cx + sx * sz, cy);
		cairo_line_to(cr, cx, cy);
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0.10, 0.082, 0.060, 0.34);
		cairo_fill(cr);
		cairo_restore(cr);

		cairo_move_to(cr, cx, cy + sy * sz);
		cairo_line_to(cr, cx + sx * sz, cy);
		cairo_line_to(cr, cx, cy);
		cairo_close_path(cr);
		cairo_pattern_t *fl = cairo_pattern_create_linear(
			cx + sx * sz * 0.5, cy + sy * sz * 0.5, cx, cy);
		cairo_pattern_add_color_stop_rgba(fl, 0.0, 0.632, 0.586, 0.496, 0.92);
		cairo_pattern_add_color_stop_rgba(fl, 1.0, 0.836, 0.802, 0.716, 0.92);
		cairo_set_source(cr, fl);
		cairo_fill(cr);
		cairo_pattern_destroy(fl);

		/* the fold itself, where the fibres have gone */
		cairo_set_line_width(cr, fmax(1.0, d * 0.006));
		cairo_set_source_rgba(cr, 0.94, 0.92, 0.86, 0.55);
		cairo_move_to(cr, cx, cy + sy * sz);
		cairo_line_to(cr, cx + sx * sz, cy);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

static void fill_paper(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_save(cr);
	desk_rounded(cr, x, y, w, h, r);
	cairo_clip(cr);
	fill_tile(cr, paper_tile(), 1.0, 1.0);
	cairo_restore(cr);
}

static void leather_fill(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_save(cr);
	desk_rounded(cr, x, y, w, h, r);
	cairo_clip(cr);
	cairo_set_source_rgb(cr, 0.226, 0.118, 0.066);
	cairo_paint(cr);
	cairo_pattern_t *g = cairo_pattern_create_linear(x, y, x, y + h);
	cairo_pattern_add_color_stop_rgba(g, 0.00, 1, 0.92, 0.80, 0.16);
	cairo_pattern_add_color_stop_rgba(g, 0.45, 0, 0, 0, 0.00);
	cairo_pattern_add_color_stop_rgba(g, 1.00, 0, 0, 0, 0.26);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	overlay_grain(cr, x, y, w, h, 1.4);
	cairo_restore(cr);
}

static void stitch_rect(cairo_t *cr, double x, double y, double w, double h, double inset)
{
	cairo_save(cr);
	cairo_set_line_width(cr, 1.4);
	cairo_set_source_rgba(cr, 0.92, 0.85, 0.66, 0.60);
	double dash[2] = { 5.0, 4.5 };
	cairo_set_dash(cr, dash, 2, 0);
	desk_rounded(cr, x + inset, y + inset, w - inset * 2, h - inset * 2, 6.0);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* A shallow well, for anything recessed into a panel. */
static void draw_gloss_bar(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_save(cr);
	desk_rounded(cr, x, y, w, h, r);
	cairo_clip(cr);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y, 0, y + h * 0.52);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 1, 1, 1, 0.42);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 1, 1, 1, 0.06);
	cairo_set_source(cr, g);
	cairo_rectangle(cr, x, y, w, h * 0.52);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
}

/* A blob of shade right under something, so it does not float. */
static void contact_shadow(cairo_t *cr, double x, double y, double w, double h, double a)
{
	if (!gfx_shadows()) {
		return;
	}
	double ox, oy;
	shadow_offset(cr, x + w * 0.5, y + h * 0.5, 10.0, &ox, &oy);
	cairo_save(cr);
	cairo_translate(cr, x + w * 0.5 + ox, y + h * 0.5 + oy);
	cairo_scale(cr, w * 0.55, h * 0.55);
	cairo_pattern_t *g = cairo_pattern_create_radial(0, 0, 0.2, 0, 0, 1.0);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0.06, 0.04, 0.03, a);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.06, 0.04, 0.03, 0.0);
	cairo_set_source(cr, g);
	cairo_arc(cr, 0, 0, 1.0, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
}

/* ---- window chrome --------------------------------------------- */

void desk_draw_window_shadow(cairo_t *cr, double x, double y, double w, double h, double r)
{
	if (!gfx_shadows()) {
		return;
	}
	double ox, oy;
	shadow_offset(cr, x + w * 0.5, y + h * 0.5, 34.0, &ox, &oy);
	cairo_save(cr);
	for (int k = 16; k >= 1; k--) {
		double t = k / 16.0;
		double grow = 30.0 * t;
		cairo_set_source_rgba(cr, 0.06, 0.04, 0.03, 0.045 * (1.0 - t));
		desk_rounded(cr, x - grow + ox, y - grow + oy, w + grow * 2, h + grow * 2, r + grow);
		cairo_fill(cr);
	}
	cairo_restore(cr);
}

bool desk_frame_title_hit(double lx, double ly, double w, double h)
{
	(void)h;
	/* the whole of the top margin: a sheet has no buttons in it, and its
	 * corner is seen to before this is asked */
	return ly >= 0 && ly < DESK_TITLE_H && lx >= 0 && lx <= w;
}

/* wlroots' WLR_EDGE_ bits, without dragging the header in here. */
int desk_frame_resize_edges(double lx, double ly, double w, double h)
{
	/* a little way in, and a little way out past the edge of the paper:
	 * a sheet's edge is a line, and a hand does not land on a line */
	const double g = 9.0, out = 6.0;
	int e = 0;
	if (lx < -out || ly < -out || lx > w + out || ly > h + out) {
		return 0;
	}
	if (ly > h - g) {
		e |= 2;          /* bottom */
	} else if (ly < g) {
		e |= 1;          /* top */
	}
	if (lx < g) {
		e |= 4;          /* left */
	} else if (lx > w - g) {
		e |= 8;          /* right */
	}
	/* the title bar is for dragging, not for resizing from the side */
	if (ly >= g && ly < DESK_TITLE_H && (e & (4 | 8)) == 0) {
		return 0;
	}
	return e;
}

/* Two windows with the same scratches in the same places are two prints of
 * one window. Their name is the only thing that tells them apart. */
static unsigned title_seed(const char *s)
{
	unsigned h = 2166136261u;
	for (const char *p = s ? s : "-"; *p; p++) {
		h = (h ^ (unsigned char)*p) * 16777619u;
	}
	return h;
}

/* Which corner of a sheet is off the desk, and by how much.
 *
 * Paper does not lie flat. A sheet that has been anywhere has one corner
 * that will not go down, and which corner it is belongs to the sheet --
 * so it comes off the title, the same seed the foxing and the wear come
 * off, and the shadow and the sheet agree about it without anybody having
 * to carry it between them.
 *
 * Always a bottom corner: the top right one is turned down on purpose --
 * it is where the sheet's instructions are (desk_draw_curl) -- and a
 * sheet lifting at both ends of one edge is a sheet about to blow away. */
void desk_paper_lift(const char *title, double *side, double *amount)
{
	unsigned s = title_seed(title);
	*side = hash21(s * 0.0131 + 1.7, 7.7) < 0.5 ? -1.0 : 1.0;
	*amount = 0.38 + hash21(s * 0.0291 + 4.3, 3.1) * 0.62;
}

/* The paper a sheet is cut from, before anything is printed on it: its
 * tone, the age it has picked up, and the corner that will not lie down.
 * Which sheet it is -- its title -- decides the stains and the corner, so a
 * sheet opened out to the whole desk is the same paper, only more of it. */
static void sheet_paper(cairo_t *cr, double x, double y, double w, double h, const char *title)
{
	double r = DESK_RADIUS;
	fill_paper(cr, x, y, w, h, r);
	cairo_save(cr);
	desk_rounded(cr, x, y, w, h, r);
	cairo_clip(cr);
	cairo_translate(cr, x, y);
	paper_age_coarse(cr, w, h, title_seed(title), 0.30);
	cairo_restore(cr);

	/* The corner that will not lie down.
	 *
	 * Only the margin of the sheet shows -- the client has the middle --
	 * and the margin is where a curl shows anyway. Along the bottom and
	 * along one side it rises into the lamp and goes light, there is a
	 * crease at the foot of the rise, and the cut edge itself catches. */
	{
		double side, amount;
		desk_paper_lift(title, &side, &amount);
		double run = fmin(w, h) * 0.46 * amount;
		double cx = side < 0.0 ? x : x + w;
		double cy = y + h;
		cairo_save(cr);
		desk_rounded(cr, x, y, w, h, r);
		cairo_clip(cr);

		/* Which way the rise faces: up and in, away from the corner. Turned
		 * towards the lamp it goes light; turned away, into its own shade. */
		double rise = 0.45, gx = side < 0.0 ? -0.70710678 : 0.70710678, gy = 0.70710678;
		double nn = sqrt(1.0 + rise * rise);
		double llx, lly, llz;
		lamp_toward_user(cr, cx, cy, &llx, &lly, &llz);
		double lit = (-gx * rise * llx - gy * rise * lly + llz) / nn / fmax(llz, 0.2);
		double up = clampf((lit - 1.0) / 0.20, -1.0, 1.0);
		cairo_pattern_t *g = cairo_pattern_create_radial(cx, cy, run * 0.10,
			cx, cy, run);
		if (up >= 0.0) {
			double k = 0.35 + 0.65 * up;
			cairo_pattern_add_color_stop_rgba(g, 0.00, 1.000, 0.992, 0.960,
				0.50 * amount * k);
			cairo_pattern_add_color_stop_rgba(g, 0.42, 1.000, 0.988, 0.944,
				0.17 * amount * k);
		} else {
			cairo_pattern_add_color_stop_rgba(g, 0.00, 0.420, 0.360, 0.280,
				0.30 * amount * -up);
			cairo_pattern_add_color_stop_rgba(g, 0.42, 0.480, 0.420, 0.340,
				0.12 * amount * -up);
		}
		cairo_pattern_add_color_stop_rgba(g, 0.74, 0.560, 0.500, 0.420,
			0.085 * amount);
		cairo_pattern_add_color_stop_rgba(g, 1.00, 0.560, 0.500, 0.420, 0.0);
		cairo_set_source(cr, g);
		cairo_rectangle(cr, cx - run, cy - run, run * 2.0, run * 2.0);
		cairo_fill(cr);
		cairo_pattern_destroy(g);

		/* the crease: where the flat of the sheet stops and the rise
		 * begins, which on paper is a line and not a gradient */
		cairo_set_line_width(cr, fmax(1.0, run * 0.016));
		cairo_set_source_rgba(cr, 0.404, 0.356, 0.290, 0.20 * amount);
		cairo_move_to(cr, cx - side * run * 0.86, cy);
		cairo_curve_to(cr,
			cx - side * run * 0.50, cy - run * 0.18,
			cx - side * run * 0.18, cy - run * 0.50,
			cx, cy - run * 0.86);
		cairo_stroke(cr);
		cairo_set_source_rgba(cr, 1.0, 0.99, 0.96, 0.17 * amount);
		cairo_move_to(cr, cx - side * run * 0.86, cy - 1.2);
		cairo_curve_to(cr,
			cx - side * run * 0.50, cy - run * 0.18 - 1.2,
			cx - side * run * 0.18, cy - run * 0.50 - 1.2,
			cx - side * 1.2, cy - run * 0.86);
		cairo_stroke(cr);
		cairo_restore(cr);
	}

}

/* the bezel around the whole thing */
static void sheet_edge(cairo_t *cr, double x, double y, double w, double h)
{
	cairo_save(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.20, 0.18, 0.16, 0.55);
	desk_rounded(cr, x + 0.5, y + 0.5, w - 1, h - 1, DESK_RADIUS);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void desk_draw_window_frame(cairo_t *cr, double x, double y, double w, double h,
	const char *title, bool focused, int hover_btn, bool with_shadow)
{
	double r = DESK_RADIUS;
	if (with_shadow) {
		desk_draw_window_shadow(cr, x, y, w, h, r);
	}
	sheet_paper(cr, x, y, w, h, title);

	/* No bar across the top: the top of the sheet is paper like the rest,
	 * with what it is printed along it as a heading, and a rule under the
	 * heading where the page proper begins. The margin above the rule is
	 * what you pick the sheet up by. The one being written on has its
	 * heading in fresh ink; the rest have gone a little grey. */
	cairo_save(cr);
	desk_rounded(cr, x, y, w, h, r);
	cairo_clip(cr);
	double ink = focused ? 1.0 : 0.0;
	double rule_y = y + DESK_TITLE_H - 7.0;
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.30, 0.24, 0.17, 0.30 + 0.22 * ink);
	cairo_move_to(cr, x + 16.0, rule_y + 0.5);
	cairo_line_to(cr, x + w - 16.0, rule_y + 0.5);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 0.30, 0.24, 0.17, 0.16 + 0.12 * ink);
	cairo_move_to(cr, x + 16.0, rule_y + 3.5);
	cairo_line_to(cr, x + w - 16.0, rule_y + 3.5);
	cairo_stroke(cr);
	if (title) {
		/* clear of the turned-down corner at the right, and centred on
		 * what is left */
		double room = w - 32.0 - 60.0;
		show_text_fit_as(cr, DESK_FONT_PRINT, title, x + 16.0 + room * 0.5,
			y + DESK_TITLE_H * 0.5 + 3.0,
			14.0, true, 0.20, 0.16, 0.12, 0.52 + 0.40 * ink, 0, room);
	}
	(void)hover_btn;
	cairo_restore(cr);

	sheet_edge(cr, x, y, w, h);
}

/* A program that draws its own frame and buttons, laid on the shell's
 * paper anyway: the sheet with a margin of paper round the program, and
 * nothing printed on it -- the program has a heading of its own. */
void desk_draw_window_mount(cairo_t *cr, double x, double y, double w, double h,
	const char *title)
{
	sheet_paper(cr, x, y, w, h, title);
	sheet_edge(cr, x, y, w, h);
}

/* ---- the corner of a sheet ---------------------------------------
 *
 * There are no buttons on a sheet of paper. What there is, is its corner:
 * the top right one of every sheet is turned down a little, the way the
 * corner of a page you mean to come back to is, and what can be done with
 * the sheet is printed on the back of it. Come near and the corner lifts
 * off the page far enough to read -- and to press what it says.
 *
 * All of it is drawn in a square box whose top right is the sheet's own
 * top right corner. Turned down by s, the fold runs from (B - s, 0) to
 * (B, s); the flap is the triangle below and left of it, its tip at
 * (B - s, s); the triangle above and right of it is where the paper was,
 * and shows whatever is under it -- the desk, another sheet -- which the
 * compositor lays in before the flap goes over (desk_paint_room_at). */

void desk_curl_sizes(int screen_w, int screen_h, double *rest, double *open)
{
	/* in points, like everything else laid over the desk */
	(void)screen_h;
	double k = desk_px_per_pt(screen_w);
	*rest = fmax(19.0 * k, 12.0);
	*open = fmax(151.0 * k, 120.0);
}

static const char *const CURL_WORDS[DESK_CURL_ITEMS] = { "Fold", "Unfold", "Discard" };

/* Where instruction i is printed, measured from the tip, which is where the
 * back of the flap is widest: the last line sits nearest the tip, and each
 * line up has less room than the one below it. */
static void curl_item_at(double B, double s, double open, int i, int n,
	double *x, double *base, double *fs, double *room)
{
	*fs = clampf(open * 0.084, 11.0, 21.0);
	double lh = *fs * 2.0;
	double pad = *fs * 1.05;
	int row = n - 1 - i;
	*x = B - s + pad;
	*base = s - pad - row * lh;
	/* across the flap at the height of the top of the letters */
	*room = (*base - *fs) - pad * 0.6;
}

/* A mark for each instruction, drawn with the pen that printed the words,
 * the size of a capital: a sheet with its corner folded in, a sheet opened
 * out to its edges, and a sheet screwed up. */
/* the marks a letter's corner has besides a sheet's */
enum { CURL_G_READ = 10, CURL_G_MARK, CURL_G_UNMARK, CURL_G_ACTION, CURL_G_DRAWER };

static void curl_glyph(cairo_t *cr, int i, double x, double y, double z)
{
	cairo_save(cr);
	cairo_set_line_width(cr, fmax(1.0, z * 0.11));
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	switch (i) {
	case DESK_CURL_FOLD:
		cairo_move_to(cr, x, y);
		cairo_line_to(cr, x + z * 0.55, y);
		cairo_line_to(cr, x + z * 0.80, y + z * 0.25);
		cairo_line_to(cr, x + z * 0.80, y + z);
		cairo_line_to(cr, x, y + z);
		cairo_close_path(cr);
		cairo_move_to(cr, x + z * 0.55, y);
		cairo_line_to(cr, x + z * 0.55, y + z * 0.25);
		cairo_line_to(cr, x + z * 0.80, y + z * 0.25);
		cairo_stroke(cr);
		break;
	case DESK_CURL_UNFOLD:
		cairo_rectangle(cr, x + z * 0.18, y + z * 0.18, z * 0.64, z * 0.64);
		cairo_stroke(cr);
		for (int k = 0; k < 4; k++) {
			double sx = (k & 1) ? 1.0 : -1.0, sy = (k & 2) ? 1.0 : -1.0;
			double cx = x + z * 0.5 + sx * z * 0.50, cy = y + z * 0.5 + sy * z * 0.50;
			cairo_move_to(cr, cx - sx * z * 0.22, cy);
			cairo_line_to(cr, cx, cy);
			cairo_line_to(cr, cx, cy - sy * z * 0.22);
			cairo_stroke(cr);
		}
		break;
	case CURL_G_READ:
		/* a sheet coming up out of its envelope */
		cairo_rectangle(cr, x, y + z * 0.45, z, z * 0.55);
		cairo_move_to(cr, x, y + z * 0.45);
		cairo_line_to(cr, x + z * 0.5, y + z * 0.78);
		cairo_line_to(cr, x + z, y + z * 0.45);
		cairo_stroke(cr);
		cairo_move_to(cr, x + z * 0.18, y + z * 0.45);
		cairo_line_to(cr, x + z * 0.18, y);
		cairo_line_to(cr, x + z * 0.82, y);
		cairo_line_to(cr, x + z * 0.82, y + z * 0.45);
		cairo_move_to(cr, x + z * 0.32, y + z * 0.16);
		cairo_line_to(cr, x + z * 0.68, y + z * 0.16);
		cairo_stroke(cr);
		break;
	case CURL_G_MARK:
		/* a tick, or a ring for one that has been read and is to be
		 * looked at again */
		cairo_move_to(cr, x + z * 0.08, y + z * 0.55);
		cairo_line_to(cr, x + z * 0.38, y + z * 0.88);
		cairo_line_to(cr, x + z * 0.95, y + z * 0.12);
		cairo_stroke(cr);
		break;
	case CURL_G_UNMARK:
		cairo_arc(cr, x + z * 0.5, y + z * 0.5, z * 0.40, 0, 2 * M_PI);
		cairo_stroke(cr);
		cairo_arc(cr, x + z * 0.5, y + z * 0.5, z * 0.13, 0, 2 * M_PI);
		cairo_fill(cr);
		break;
	case CURL_G_DRAWER:
		/* a drawer, pulled out a little, and its pull */
		cairo_rectangle(cr, x, y + z * 0.30, z, z * 0.62);
		cairo_move_to(cr, x + z * 0.10, y + z * 0.30);
		cairo_line_to(cr, x + z * 0.18, y + z * 0.08);
		cairo_line_to(cr, x + z * 0.82, y + z * 0.08);
		cairo_line_to(cr, x + z * 0.90, y + z * 0.30);
		cairo_move_to(cr, x + z * 0.36, y + z * 0.60);
		cairo_line_to(cr, x + z * 0.64, y + z * 0.60);
		cairo_stroke(cr);
		break;
	case CURL_G_ACTION:
		/* out of the box, to the program that sent it */
		cairo_move_to(cr, x + z * 0.55, y + z * 0.12);
		cairo_line_to(cr, x + z * 0.12, y + z * 0.12);
		cairo_line_to(cr, x + z * 0.12, y + z * 0.88);
		cairo_line_to(cr, x + z * 0.88, y + z * 0.88);
		cairo_line_to(cr, x + z * 0.88, y + z * 0.45);
		cairo_move_to(cr, x + z * 0.45, y + z * 0.55);
		cairo_line_to(cr, x + z * 0.95, y + z * 0.05);
		cairo_move_to(cr, x + z * 0.62, y + z * 0.05);
		cairo_line_to(cr, x + z * 0.95, y + z * 0.05);
		cairo_line_to(cr, x + z * 0.95, y + z * 0.38);
		cairo_stroke(cr);
		break;
	default: {
		/* a ball of paper: a lumpy ring and the creases across it */
		double cx = x + z * 0.5, cy = y + z * 0.54, r = z * 0.44;
		for (int k = 0; k <= 9; k++) {
			double a = k * 2.0 * M_PI / 9.0;
			double rr = r * (0.86 + 0.14 * ((k * 7) % 3) / 2.0);
			if (k == 0) {
				cairo_move_to(cr, cx + cos(a) * rr, cy + sin(a) * rr);
			} else {
				cairo_line_to(cr, cx + cos(a) * rr, cy + sin(a) * rr);
			}
		}
		cairo_close_path(cr);
		cairo_move_to(cr, cx - r * 0.55, cy - r * 0.20);
		cairo_line_to(cr, cx + r * 0.10, cy + r * 0.10);
		cairo_line_to(cr, cx + r * 0.45, cy - r * 0.40);
		cairo_move_to(cr, cx - r * 0.20, cy + r * 0.60);
		cairo_line_to(cr, cx + r * 0.10, cy + r * 0.10);
		cairo_stroke(cr);
		break;
	}
	}
	cairo_restore(cr);
}

static void print_word(cairo_t *cr, const char *s, double x, double y, double size,
	double r, double g, double b, double a)
{
	/* a window's name may be an X title in Latin-1, which cairo will not
	 * take: it would stop drawing on cr altogether */
	char clean[1024];
	s = utf8_clean(s, clean, sizeof clean);
	cairo_save(cr);
	desk_font_select(cr, DESK_FONT_PRINT, s, true, false);
	cairo_set_font_size(cr, size);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, s);
	cairo_restore(cr);
}

static double word_width(cairo_t *cr, const char *s, double size)
{
	char clean[1024];
	s = utf8_clean(s, clean, sizeof clean);
	cairo_save(cr);
	desk_font_select(cr, DESK_FONT_PRINT, s, true, false);
	cairo_set_font_size(cr, size);
	cairo_text_extents_t e;
	cairo_text_extents(cr, s, &e);
	cairo_restore(cr);
	return e.x_advance;
}

void desk_paint_room_at(cairo_t *cr, int screen_w, int screen_h, double drawer_open)
{
	struct desk_room_sheets sh;
	if (!desk_room_sheets(screen_w, screen_h, &sh) || !sh.background) {
		return;
	}
	/* exactly where the compositor lays the room: see room_layout */
	double zoom, pan, t;
	desk_room_view(drawer_open, &sh, &zoom, &pan, &t);
	int dw = (int)(screen_w * zoom + 0.5), dh = (int)(screen_h * zoom + 0.5);
	int ox = (screen_w - dw) / 2, oy = (int)((screen_h - dh) / 2 + pan * screen_h);
	cairo_save(cr);
	cairo_translate(cr, ox, oy);
	cairo_scale(cr, (double)dw / sh.w, (double)dh / sh.h);
	cairo_set_source_surface(cr, sh.background, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* The corner, with n instructions on its back: word i next to mark
 * glyph[i], in red where warn[i]. */
static void curl_draw(cairo_t *cr, double B, double s, double open, double lift, int hot,
	unsigned seed, int n, const char *const *words, const int *glyphs, const bool *warns)
{
	if (s < 0.5) {
		return;
	}
	double ax = B - s, ay = 0.0;     /* where the fold meets the top edge */
	double bx = B, by = s;           /* and the right edge */
	double tx = B - s, ty = s;       /* the tip, turned down */
	/* a lifted flap does not lie flat: its free edges bow away from the
	 * page, more the further it is off it */
	double bow = s * (0.035 + 0.075 * lift);

	/* Its shadow, on the page and on the desk under where it was: thrown
	 * away from the lamp, further and softer the higher the flap is off the
	 * paper -- the tip most, the fold that holds it hardly at all. */
	double d = 2.0 + lift * s * 0.075;
	double llx, lly, llz;
	lamp_toward_user(cr, B - s * 0.5, s * 0.5, &llx, &lly, &llz);
	double vx = -llx / fmax(llz, 0.2), vy = -lly / fmax(llz, 0.2);
	double vn = fmax(sqrt(vx * vx + vy * vy), 1e-6);
	cairo_save(cr);
	for (int k = 6; k >= 1; k--) {
		double g = k / 6.0;
		double spread = 1.5 + g * (3.0 + lift * 9.0);
		/* each corner out along the shadow by its height, and the blur
		 * out the same way */
		double ea = d * 0.35 + spread * 0.3, eb = d * 0.45 + spread * 0.3;
		double et = d * 1.25 + spread;
		cairo_set_source_rgba(cr, 0.10, 0.07, 0.04, (0.07 + 0.05 * lift) * (1.0 - g * 0.8));
		cairo_move_to(cr, ax + vx * ea - vy / vn * spread * 0.2, ay + vy * ea + vx / vn * spread * 0.2);
		cairo_line_to(cr, bx + vx * eb + vy / vn * spread * 0.2, by + vy * eb - vx / vn * spread * 0.2);
		cairo_line_to(cr, tx + vx * et, ty + vy * et);
		cairo_close_path(cr);
		cairo_fill(cr);
	}
	cairo_restore(cr);

	/* the back of the sheet: the same paper, uncoated side, catching the
	 * lamp where it bends at the fold and going into its own shade towards
	 * the tip */
	cairo_save(cr);
	cairo_move_to(cr, ax, ay);
	cairo_line_to(cr, bx, by);
	cairo_curve_to(cr, bx - s * 0.30, by + bow * 0.55, tx + s * 0.30, ty + bow, tx, ty);
	cairo_curve_to(cr, tx - bow, ty - s * 0.30, ax - bow * 0.55, ay + s * 0.30, ax, ay);
	cairo_close_path(cr);
	cairo_clip_preserve(cr);
	cairo_pattern_t *g = cairo_pattern_create_linear(B - s * 0.5, s * 0.5, tx, ty);
	cairo_pattern_add_color_stop_rgb(g, 0.00, 0.968, 0.950, 0.906);
	cairo_pattern_add_color_stop_rgb(g, 0.20, 0.930, 0.905, 0.850);
	cairo_pattern_add_color_stop_rgb(g, 1.00, 0.820 - 0.05 * lift, 0.790 - 0.05 * lift,
		0.720 - 0.05 * lift);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	overlay_grain(cr, tx - bow, 0, s + bow, s + bow, 0.7);
	/* a little of what is printed on the front shows through, backwards */
	cairo_set_source_rgba(cr, 0.35, 0.30, 0.24, 0.035);
	for (int k = 0; k < 3; k++) {
		double yy = s * (0.30 + k * 0.22) + hash21(seed + k, 3.3) * 4.0;
		cairo_rectangle(cr, B - s * 0.92 + yy * 0.2, yy, s * 0.45, 1.2);
		cairo_fill(cr);
	}

	/* What is printed on the back, once there is room to read it. It is
	 * printed there, so it goes where the flap goes. */
	double show = clampf((s / fmax(open, 1.0) - 0.62) / 0.30, 0.0, 1.0);
	if (show > 0.0) {
		for (int i = 0; i < n; i++) {
			double x, base, fs, room;
			curl_item_at(B, s, open, i, n, &x, &base, &fs, &room);
			const char *word = words[i];
			bool warn = warns[i];
			double ir = warn ? 0.55 : 0.24, ig = warn ? 0.14 : 0.20, ib = warn ? 0.10 : 0.15;
			double a = show * (hot == i ? 0.98 : 0.80);
			double z = fs * 0.92;
			cairo_set_source_rgba(cr, ir, ig, ib, a);
			curl_glyph(cr, glyphs[i], x, base - z * 0.92, z);
			double wx = x + z * 1.45;
			double fsz = fs;
			double ww = word_width(cr, word, fsz);
			if (wx + ww > x + room) {
				fsz *= fmax((x + room - wx) / fmax(ww, 1.0), 0.6);
			}
			print_word(cr, word, wx, base, fsz, ir, ig, ib, a);
			if (hot == i) {
				/* underlined in pencil, the way you mark what you mean */
				double uw = word_width(cr, word, fsz);
				cairo_set_line_width(cr, 1.3);
				cairo_set_source_rgba(cr, 0.28, 0.26, 0.24, 0.70 * show);
				cairo_move_to(cr, wx - 1.0, base + fs * 0.28);
				cairo_curve_to(cr, wx + uw * 0.3, base + fs * 0.36,
					wx + uw * 0.7, base + fs * 0.20, wx + uw + 2.0, base + fs * 0.30);
				cairo_stroke(cr);
			}
		}
	}
	cairo_restore(cr);

	/* the bend along the fold catches the light; just past it, where the
	 * sheet goes on flat, is the crease */
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_width(cr, 1.6);
	cairo_set_source_rgba(cr, 1.0, 0.99, 0.95, 0.70);
	cairo_move_to(cr, ax + 1.0, ay + 0.2);
	cairo_line_to(cr, bx - 0.2, by - 1.0);
	cairo_stroke(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.30, 0.25, 0.18, 0.28);
	cairo_move_to(cr, ax + 2.0, ay);
	cairo_line_to(cr, bx, by - 2.0);
	cairo_stroke(cr);
	/* and the cut edges of the flap */
	cairo_set_source_rgba(cr, 0.28, 0.23, 0.17, 0.45);
	cairo_move_to(cr, bx, by);
	cairo_curve_to(cr, bx - s * 0.30, by + bow * 0.55, tx + s * 0.30, ty + bow, tx, ty);
	cairo_curve_to(cr, tx - bow, ty - s * 0.30, ax - bow * 0.55, ay + s * 0.30, ax, ay);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void desk_draw_curl(cairo_t *cr, double B, double s, double open, double lift,
	int hot, bool maximized, const char *title)
{
	const char *words[DESK_CURL_ITEMS];
	int glyphs[DESK_CURL_ITEMS];
	bool warns[DESK_CURL_ITEMS];
	for (int i = 0; i < DESK_CURL_ITEMS; i++) {
		words[i] = TR(CURL_WORDS[i]);
		glyphs[i] = i;
		warns[i] = i == DESK_CURL_DISCARD;
	}
	if (maximized) {
		words[DESK_CURL_UNFOLD] = TR("Refold");
	}
	curl_draw(cr, B, s, open, lift, hot, title_seed(title), DESK_CURL_ITEMS, words, glyphs,
		warns);
}

static int curl_hit_n(double B, double s, double open, int n, double lx, double ly)
{
	if (s < open * 0.85) {
		return -1;
	}
	for (int i = 0; i < n; i++) {
		double x, base, fs, room;
		curl_item_at(B, s, open, i, n, &x, &base, &fs, &room);
		double lh = fs * 2.0;
		if (lx >= x - fs * 0.4 && lx <= x + room && ly >= base - lh * 0.72
			&& ly <= base + lh * 0.28) {
			return i;
		}
	}
	return -1;
}

int desk_curl_hit(double B, double s, double open, double lx, double ly)
{
	return curl_hit_n(B, s, open, DESK_CURL_ITEMS, lx, ly);
}

bool desk_curl_contains(double B, double s, double lx, double ly)
{
	/* the flap and where it came from: the square the fold cuts across */
	return lx >= B - s - 2.0 && lx <= B && ly >= 0.0 && ly <= s + 2.0;
}

/* ---- a sheet being made bigger or smaller -------------------------
 *
 * There is more of every sheet than shows: the rest is folded under it at
 * the edges. Pull an edge out and paper comes out from behind -- round the
 * edge, which is a fold, a lip of paper curving under -- and the paper that
 * has come out still has the creases it was folded with. Push the edge in
 * and what was there goes back under the same way. Once the program has
 * written on the new paper, the creases are under what it wrote. */

void desk_draw_sizing(cairo_t *cr, double pw, double ph,
	double cx0, double cy0, double cx1, double cy1, unsigned edges, double lip,
	unsigned seed)
{
	cairo_save(cr);
	desk_rounded_path(cr, 0, 0, pw, ph, DESK_RADIUS);
	cairo_clip(cr);
	/* the creases in paper that has come out from under: parallel to the
	 * edge it came out at, a panel's width apart, counted from where the
	 * sheet ended */
	double panel = 84.0;
	for (int side = 0; side < 4; side++) {
		/* 0 right, 1 left, 2 bottom, 3 top */
		double from, to;
		bool vertical = side < 2;
		if (side == 0) {
			from = cx1;
			to = pw;
		} else if (side == 1) {
			from = cx0;
			to = 0;
		} else if (side == 2) {
			from = cy1;
			to = ph;
		} else {
			from = cy0;
			to = 0;
		}
		double span = fabs(to - from);
		if (span < 6.0) {
			continue;
		}
		/* Where the paper came out from under, the fold it was folded on
		 * stands up as a ridge while it is being pulled, and goes down
		 * again, not quite flat, when it is let go. */
		desk_draw_crease_relief(cr, vertical, from, 0, vertical ? ph : pw,
			0.55 + 0.95 * lip);
		double dir = to > from ? 1.0 : -1.0;
		int n = 0;
		for (double d = 0.0; d <= span + 0.5; d += panel, n++) {
			double at = from + dir * d;
			/* a mountain fold and then a valley, turn about */
			bool mountain = (n & 1) == 0;
			double jitter = (hash21(seed * 0.31 + side * 7.0 + n, 4.2) - 0.5) * 2.0;
			double a = 0.20 * (n == 0 ? 1.3 : 1.0);
			cairo_set_line_width(cr, 1.0);
			for (int k = 0; k < 2; k++) {
				double off = (k == 0 ? -0.6 : 0.6) * (mountain ? 1.0 : -1.0);
				if (k == 0) {
					cairo_set_source_rgba(cr, 0.28, 0.22, 0.15, a);
				} else {
					cairo_set_source_rgba(cr, 1.0, 0.98, 0.93, a * 1.4);
				}
				if (vertical) {
					cairo_move_to(cr, at + off, 0);
					cairo_line_to(cr, at + off + jitter, ph);
				} else {
					cairo_move_to(cr, 0, at + off);
					cairo_line_to(cr, pw, at + off + jitter);
				}
				cairo_stroke(cr);
			}
			/* each panel a little off the flat, so the creases have a
			 * slope either side */
			if (d + panel <= span + panel * 0.5) {
				double w0 = fmin(panel, span - d);
				cairo_pattern_t *g;
				if (vertical) {
					g = cairo_pattern_create_linear(at, 0, at + dir * w0, 0);
				} else {
					g = cairo_pattern_create_linear(0, at, 0, at + dir * w0);
				}
				double tone = mountain ? 0.060 : -0.050;
				if (tone > 0) {
					cairo_pattern_add_color_stop_rgba(g, 0.0, 1, 0.98, 0.93, tone);
					cairo_pattern_add_color_stop_rgba(g, 1.0, 0.1, 0.07, 0.04, tone);
				} else {
					cairo_pattern_add_color_stop_rgba(g, 0.0, 0.1, 0.07, 0.04, -tone);
					cairo_pattern_add_color_stop_rgba(g, 1.0, 1, 0.98, 0.93, -tone);
				}
				if (vertical) {
					cairo_rectangle(cr, fmin(at, at + dir * w0), 0, w0, ph);
				} else {
					cairo_rectangle(cr, 0, fmin(at, at + dir * w0), pw, w0);
				}
				cairo_set_source(cr, g);
				cairo_fill(cr);
				cairo_pattern_destroy(g);
			}
		}
	}
	cairo_restore(cr);

	/* the edges being moved: the paper turns under there, and the harder
	 * it is being pulled, the rounder the turn */
	if (lip <= 0.01) {
		return;
	}
	double lw = 10.0 + 24.0 * lip;
	for (int side = 0; side < 4; side++) {
		/* WLR_EDGE_: top 1, bottom 2, left 4, right 8 */
		static const unsigned bit[4] = { 8, 4, 2, 1 };
		if (!(edges & bit[side])) {
			continue;
		}
		cairo_save(cr);
		/* the turn is on the paper, which has round corners */
		cairo_new_path(cr);
		cairo_rectangle(cr, -40, -40, pw + 80, ph + 80);
		cairo_clip(cr);
		/* turned so the edge is at x = 0 with the paper to its left */
		if (side == 0) {
			cairo_translate(cr, pw, 0);
		} else if (side == 1) {
			cairo_translate(cr, 0, ph);
			cairo_rotate(cr, M_PI);
		} else if (side == 2) {
			cairo_translate(cr, pw, ph);
			cairo_rotate(cr, M_PI * 0.5);
		} else {
			cairo_rotate(cr, -M_PI * 0.5);
		}
		double len = side < 2 ? ph : pw;
		/* the shadow of the curl on the desk, just outside */
		double sw = 5.0 + 10.0 * lip;
		cairo_pattern_t *sh = cairo_pattern_create_linear(0, 0, sw, 0);
		cairo_pattern_add_color_stop_rgba(sh, 0.0, 0.08, 0.05, 0.03, 0.42 * lip);
		cairo_pattern_add_color_stop_rgba(sh, 1.0, 0.08, 0.05, 0.03, 0.0);
		cairo_rectangle(cr, 0, DESK_RADIUS, sw, len - 2 * DESK_RADIUS);
		cairo_set_source(cr, sh);
		cairo_fill(cr);
		cairo_pattern_destroy(sh);
		cairo_restore(cr);
		cairo_save(cr);
		desk_rounded_path(cr, 0, 0, pw, ph, DESK_RADIUS);
		cairo_clip(cr);
		if (side == 0) {
			cairo_translate(cr, pw, 0);
		} else if (side == 1) {
			cairo_translate(cr, 0, ph);
			cairo_rotate(cr, M_PI);
		} else if (side == 2) {
			cairo_translate(cr, pw, ph);
			cairo_rotate(cr, M_PI * 0.5);
		} else {
			cairo_rotate(cr, -M_PI * 0.5);
		}
		/* the turn itself: lit where it starts to bend towards the lamp,
		 * going into its own shade as it turns away and under */
		cairo_pattern_t *g = cairo_pattern_create_linear(-lw, 0, 0, 0);
		cairo_pattern_add_color_stop_rgba(g, 0.00, 1.0, 0.99, 0.95, 0.0);
		cairo_pattern_add_color_stop_rgba(g, 0.32, 1.0, 0.99, 0.95, 0.50 * lip);
		cairo_pattern_add_color_stop_rgba(g, 0.66, 0.42, 0.35, 0.26, 0.20 * lip);
		cairo_pattern_add_color_stop_rgba(g, 1.00, 0.12, 0.08, 0.05, 0.80 * lip);
		cairo_rectangle(cr, -lw, 2, lw, len - 4);
		cairo_set_source(cr, g);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
		/* where the flat of the sheet gives way to the turn */
		cairo_set_line_width(cr, 1.0);
		cairo_set_source_rgba(cr, 0.30, 0.24, 0.17, 0.38 * lip);
		cairo_move_to(cr, -lw + 0.5, 4);
		cairo_line_to(cr, -lw + 0.5, len - 4);
		cairo_stroke(cr);
		cairo_restore(cr);
	}
}

/* ---- a panel unfolded out from behind a sheet, or folded back under it --
 *
 * The extra paper of a sheet opened out to the desk has been folded under
 * it -- a mountain fold, the panel behind. Unfolding it on a desk, the
 * crease comes up off the desk: the edge of the sheet lifts, turning up
 * towards you on a curve, and from under the ridge that makes the panel
 * swings out and down, its far edge coming to the desk and sliding out
 * along it; and when all of it is out, the ridge goes down again and the
 * two lie flat. Seen from above, and not from very far above: the ridge,
 * being nearer, is larger, and what is printed across the fold kinks at
 * it; the two faces take the lamp differently; and what is off the desk
 * throws its shadow on it. Folded back, the same in reverse.
 *
 * Everything here is in the edge's own terms: `a` across the fold, from
 * the crease outwards, `t` along it, `z` up off the desk. */

static void ue_xy(const struct desk_unfold_edge *e, double a, double t, double *x, double *y)
{
	switch (e->side) {
	case 0:
		*x = e->crease + a;
		*y = t;
		break;
	case 1:
		*x = e->crease - a;
		*y = t;
		break;
	case 2:
		*x = t;
		*y = e->crease + a;
		break;
	default:
		*x = t;
		*y = e->crease - a;
		break;
	}
}

static void ue_proj(const struct desk_unfold_edge *e, double a, double t, double z,
	double *x, double *y)
{
	double X, Y;
	ue_xy(e, a, t, &X, &Y);
	double k = e->dist / (e->dist - z);
	*x = e->vx + (X - e->vx) * k;
	*y = e->vy + (Y - e->vy) * k;
}

/* The lamp: a lamp, not the sun -- the room's, over the near right of the
 * desk, and near enough that which way a face turns towards it depends on
 * where the face is. In the edge's terms (which are the screen's). */
static void ue_lamp_at(const struct desk_unfold_edge *e, double *la, double *lt, double *lz)
{
	double X = g_light.x, Y = g_light.y;
	*lz = g_light.h;
	switch (e->side) {
	case 0:
		*la = X - e->crease;
		*lt = Y;
		break;
	case 1:
		*la = e->crease - X;
		*lt = Y;
		break;
	case 2:
		*la = Y - e->crease;
		*lt = X;
		break;
	default:
		*la = e->crease - Y;
		*lt = X;
		break;
	}
}

/* How much light a face at slope th (rising outwards) at a, t, z gets,
 * against lying flat there. */
static double ue_lit(const struct desk_unfold_edge *e, double a, double t, double z, double th)
{
	double la, lt, lz;
	ue_lamp_at(e, &la, &lt, &lz);
	double dx = la - a, dy = lt - t, dz = lz - z;
	double n = sqrt(dx * dx + dy * dy + dz * dz);
	dx /= n;
	dz /= n;
	double d = fmax(0.0, -sin(th) * dx + cos(th) * dz);
	double lit = (0.30 + 0.70 * d) / (0.30 + 0.70 * dz);
	/* rather more than the eye would give it: it is the one thing that
	 * says which way a face is turned */
	return lit < 1.0 ? lit * lit : lit;
}

/* The shadow of something off the desk falls away from the lamp: for a
 * point at a, t, z, where on the desk. */
static void ue_shadow_at(const struct desk_unfold_edge *e, double a, double t, double z,
	double *sa, double *st)
{
	double la, lt, lz;
	ue_lamp_at(e, &la, &lt, &lz);
	double k = z / fmax(lz - z, 1.0);
	*sa = a + (a - la) * k;
	*st = t + (t - lt) * k;
}

/* The affine map taking three points to three others. */
static bool affine3(cairo_matrix_t *m, const double s[3][2], const double d[3][2])
{
	double sx1 = s[1][0] - s[0][0], sy1 = s[1][1] - s[0][1];
	double sx2 = s[2][0] - s[0][0], sy2 = s[2][1] - s[0][1];
	double dx1 = d[1][0] - d[0][0], dy1 = d[1][1] - d[0][1];
	double dx2 = d[2][0] - d[0][0], dy2 = d[2][1] - d[0][1];
	double det = sx1 * sy2 - sx2 * sy1;
	if (fabs(det) < 1e-9) {
		return false;
	}
	m->xx = (dx1 * sy2 - dx2 * sy1) / det;
	m->xy = (dx2 * sx1 - dx1 * sx2) / det;
	m->yx = (dy1 * sy2 - dy2 * sy1) / det;
	m->yy = (dy2 * sx1 - dy1 * sx2) / det;
	m->x0 = d[0][0] - m->xx * s[0][0] - m->xy * s[0][1];
	m->y0 = d[0][1] - m->yx * s[0][0] - m->yy * s[0][1];
	return true;
}

#define UE_BANDS 40
#define UE_LIFT 1.30       /* how far the edge turns up, at most: ~74 degrees */
#define UE_OUT0 0.36       /* when the panel starts to come out from under */
#define UE_OUT1 0.86       /* and when all of it is out */

struct ue_shape {
	double sa[UE_BANDS + 1], sz[UE_BANDS + 1];   /* the sheet's edge, lifting */
	int np;
	double pa[UE_BANDS + 1], pz[UE_BANDS + 1];   /* the panel, as much as shows */
	double ps[UE_BANDS + 1];                      /* how far along the panel each is */
	double vis;
	bool contact;       /* the panel meets the desk, and the rest is under it */
};

static void ue_shape_at(const struct desk_unfold_edge *e, double u, struct ue_shape *sh)
{
	u = clampf(u, 0.0, 1.0);
	double alpha = UE_LIFT * pow(sin(M_PI * u), 0.85);
	/* the edge of the sheet: flat where it leaves the rest, turning up on
	 * a curve to the crease */
	double a = -e->bend, z = 0.0;
	sh->sa[0] = a;
	sh->sz[0] = z;
	const int sub = 4;
	double ds = e->bend / (UE_BANDS * sub);
	for (int i = 0; i < UE_BANDS; i++) {
		for (int k = 0; k < sub; k++) {
			double x = (i * sub + k + 0.5) / (double)(UE_BANDS * sub);
			double th = alpha * x * x * (3.0 - 2.0 * x);
			a += cos(th) * ds;
			z += sin(th) * ds;
		}
		sh->sa[i + 1] = a;
		sh->sz[i + 1] = z;
	}
	double ac = sh->sa[UE_BANDS], zc = sh->sz[UE_BANDS];

	/* the panel, from under the ridge: how far out along the desk it has
	 * got, and at what slope, meeting the desk where it comes out */
	sh->np = 0;
	sh->vis = 0.0;
	sh->contact = false;
	if (u <= UE_OUT0 || e->len < 1.0) {
		return;
	}
	double x = clampf((u - UE_OUT0) / (UE_OUT1 - UE_OUT0), 0.0, 1.0);
	double out = e->len * x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
	double reach = sqrt(out * out + zc * zc);
	double gamma, vis;
	if (reach < e->len) {
		gamma = -atan2(zc, fmax(out, 1e-3));
		vis = reach;
		sh->contact = true;
	} else {
		gamma = -asin(clampf(zc / e->len, 0.0, 1.0));
		vis = e->len;
	}
	if (vis < 0.5) {
		return;
	}
	sh->vis = vis;
	/* Not a board: paper stands a little proud of the straight line from
	 * the ridge to where it lands, the way a sheet does that is being
	 * opened out and not yet pressed flat. */
	double bow = vis * 0.09 * clampf(-gamma / 0.5, 0.0, 1.0);
	double ca = cos(gamma), sa = sin(gamma);
	sh->np = UE_BANDS + 1;
	for (int i = 0; i <= UE_BANDS; i++) {
		double s2 = vis * i / UE_BANDS;
		double b = bow * sin(M_PI * i / UE_BANDS);
		sh->ps[i] = s2;
		sh->pa[i] = ac + s2 * ca - sa * b;
		sh->pz[i] = fmax(zc + s2 * sa + ca * b, 0.0);
	}
}

/* One band of paper: the flat rectangle from src_a0 to src_a1 (across) by
 * t0..t1 (along), wherever it is now -- its near and far edges at a0,z0 and
 * a1,z1 -- printed with pat (placed at img_x, img_y), and shaded for the way
 * it faces the lamp. Drawn without smoothing its edges: the bands tile the
 * paper exactly, and the outline of the whole is smoothed once, after. */
static void ue_band(cairo_t *cr, const struct desk_unfold_edge *e, cairo_pattern_t *pat,
	double img_x, double img_y, double src_a0, double src_a1,
	double a0, double z0, double a1, double z1, double lit)
{
	/* In pieces along the fold as well as across it: a piece is laid on
	 * with a flat map, and the nearer and farther edges of a band are not
	 * quite the same size, so a band the length of the desk would drift
	 * off its neighbours towards its far end. */
	int nseg = (int)ceil((e->t1 - e->t0) / 160.0);
	nseg = nseg < 1 ? 1 : nseg;
	for (int k = 0; k < nseg; k++) {
		double ta = e->t0 + (e->t1 - e->t0) * k / nseg;
		double tb = e->t0 + (e->t1 - e->t0) * (k + 1) / nseg;
		double p00[2], p01[2], p10[2], p11[2];
		ue_proj(e, a0, ta, z0, &p00[0], &p00[1]);
		ue_proj(e, a0, tb, z0, &p01[0], &p01[1]);
		ue_proj(e, a1, ta, z1, &p10[0], &p10[1]);
		ue_proj(e, a1, tb, z1, &p11[0], &p11[1]);
		double area = fabs((p10[0] - p00[0]) * (p01[1] - p00[1])
			- (p10[1] - p00[1]) * (p01[0] - p00[0]));
		if (area < 0.02) {
			return;   /* seen edge on */
		}
		double s[3][2], d[3][2];
		ue_xy(e, src_a0, ta, &s[0][0], &s[0][1]);
		ue_xy(e, src_a1, ta, &s[1][0], &s[1][1]);
		ue_xy(e, src_a0, tb, &s[2][0], &s[2][1]);
		for (int i = 0; i < 3; i++) {
			s[i][0] -= img_x;
			s[i][1] -= img_y;
		}
		d[0][0] = p00[0];
		d[0][1] = p00[1];
		d[1][0] = p10[0];
		d[1][1] = p10[1];
		d[2][0] = p01[0];
		d[2][1] = p01[1];
		cairo_matrix_t m;
		if (!affine3(&m, s, d) || cairo_matrix_invert(&m) != CAIRO_STATUS_SUCCESS) {
			continue;
		}
		cairo_new_path(cr);
		cairo_move_to(cr, p00[0], p00[1]);
		cairo_line_to(cr, p10[0], p10[1]);
		cairo_line_to(cr, p11[0], p11[1]);
		cairo_line_to(cr, p01[0], p01[1]);
		cairo_close_path(cr);
		cairo_pattern_set_matrix(pat, &m);
		cairo_set_source(cr, pat);
		cairo_fill_preserve(cr);
		if (lit < 0.995) {
			cairo_set_source_rgba(cr, 0.07, 0.05, 0.03, clampf((1.0 - lit) * 1.1, 0.0, 0.75));
		} else if (lit > 1.005) {
			cairo_set_source_rgba(cr, 1.0, 0.985, 0.94, clampf((lit - 1.0) * 1.3, 0.0, 0.40));
		} else {
			cairo_new_path(cr);
			continue;
		}
		cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);
		cairo_fill(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	}
}

void desk_unfold_edge_extent(const struct desk_unfold_edge *e, double u,
	double *x0, double *y0, double *x1, double *y1)
{
	struct ue_shape sh;
	ue_shape_at(e, u, &sh);
	*x0 = *y0 = 1e9;
	*x1 = *y1 = -1e9;
	for (int part = 0; part < 2; part++) {
		int n = part ? sh.np : UE_BANDS + 1;
		for (int i = 0; i < n; i++) {
			double a = part ? sh.pa[i] : sh.sa[i], z = part ? sh.pz[i] : sh.sz[i];
			for (int k = 0; k < 2; k++) {
				double t = k ? e->t1 : e->t0;
				double x, y, sa, st;
				ue_proj(e, a, t, z, &x, &y);
				*x0 = fmin(*x0, x);
				*y0 = fmin(*y0, y);
				*x1 = fmax(*x1, x);
				*y1 = fmax(*y1, y);
				/* and where its shadow falls */
				ue_shadow_at(e, a, t, z, &sa, &st);
				ue_proj(e, sa, st, 0.0, &x, &y);
				*x0 = fmin(*x0, x);
				*y0 = fmin(*y0, y);
				*x1 = fmax(*x1, x);
				*y1 = fmax(*y1, y);
			}
		}
	}
	*x0 -= 30.0;
	*y0 -= 30.0;
	*x1 += 30.0;
	*y1 += 30.0;
}

/* The outline of the sheet's edge and the panel together, along t0 and
 * back along t1, at height (or on the desk, for its shadow). */
static void ue_outline(cairo_t *cr, const struct desk_unfold_edge *e, const struct ue_shape *sh,
	bool shadow)
{
	cairo_new_path(cr);
	bool first = true;
	for (int pass = 0; pass < 2; pass++) {
		double t = pass ? e->t1 : e->t0;
		int total = (UE_BANDS + 1) + (sh->np > 1 ? sh->np - 1 : 0);
		for (int j = 0; j < total; j++) {
			int k = pass ? total - 1 - j : j;
			double a, z;
			if (k <= UE_BANDS) {
				a = sh->sa[k];
				z = sh->sz[k];
			} else {
				a = sh->pa[k - UE_BANDS];
				z = sh->pz[k - UE_BANDS];
			}
			double x, y;
			if (shadow) {
				double sa, st;
				ue_shadow_at(e, a, t, z, &sa, &st);
				ue_proj(e, sa, st, 0.0, &x, &y);
			} else {
				ue_proj(e, a, t, z, &x, &y);
			}
			if (first) {
				cairo_move_to(cr, x, y);
				first = false;
			} else {
				cairo_line_to(cr, x, y);
			}
		}
	}
	cairo_close_path(cr);
}

void desk_draw_unfold_edge(cairo_t *cr, const struct desk_unfold_edge *e, double u)
{
	struct ue_shape sh;
	ue_shape_at(e, u, &sh);
	double zc = sh.sz[UE_BANDS];
	double tm = (e->t0 + e->t1) * 0.5;

	/* The shadow of what is off the desk, on the desk: thrown away from
	 * the lamp by its height, and softer the higher it is. */
	if (zc > 0.5 || sh.np > 0) {
		cairo_save(cr);
		/* Only where the paper has left the desk. The soft edge of a
		 * shadow drawn across the line where the flat part begins darkens
		 * the flat part in a stripe -- and at that line the paper is on
		 * the desk and throws nothing. */
		double big = 1e5, lim = e->crease;
		switch (e->side) {
		case 0:
			cairo_rectangle(cr, lim - e->bend, -big, big * 2, big * 2);
			break;
		case 1:
			cairo_rectangle(cr, lim + e->bend - big * 2, -big, big * 2, big * 2);
			break;
		case 2:
			cairo_rectangle(cr, -big, lim - e->bend, big * 2, big * 2);
			break;
		default:
			cairo_rectangle(cr, -big, lim + e->bend - big * 2, big * 2, big * 2);
			break;
		}
		cairo_clip(cr);
		ue_outline(cr, e, &sh, true);
		double soft = 3.0 + zc * 0.14;
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		for (int k = 3; k >= 1; k--) {
			cairo_set_source_rgba(cr, 0.09, 0.06, 0.03, 0.05);
			cairo_set_line_width(cr, soft * k * 1.3);
			cairo_stroke_preserve(cr);
		}
		cairo_set_source_rgba(cr, 0.09, 0.06, 0.03,
			0.20 + 0.10 * clampf(1.0 - zc / 140.0, 0.0, 1.0));
		cairo_fill(cr);
		cairo_restore(cr);
	}

	cairo_pattern_t *sheet = cairo_pattern_create_for_surface(e->sheet);
	cairo_pattern_set_filter(sheet, CAIRO_FILTER_BILINEAR);
	cairo_pattern_t *panel = sheet;
	if (e->panel && e->panel != e->sheet) {
		panel = cairo_pattern_create_for_surface(e->panel);
		cairo_pattern_set_filter(panel, CAIRO_FILTER_BILINEAR);
	}
	cairo_push_group(cr);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	/* the sheet's edge, turning up to the crease */
	for (int i = 0; i < UE_BANDS; i++) {
		double th = atan2(sh.sz[i + 1] - sh.sz[i], sh.sa[i + 1] - sh.sa[i]);
		double src0 = -e->bend + e->bend * i / UE_BANDS;
		double src1 = -e->bend + e->bend * (i + 1) / UE_BANDS;
		double am = (sh.sa[i] + sh.sa[i + 1]) * 0.5, zm = (sh.sz[i] + sh.sz[i + 1]) * 0.5;
		ue_band(cr, e, sheet, e->sheet_x, e->sheet_y, src0, src1,
			sh.sa[i], sh.sz[i], sh.sa[i + 1], sh.sz[i + 1], ue_lit(e, am, tm, zm, th));
	}
	/* the panel, as much of it as is out from under -- darker where it
	 * goes down to the desk and the desk is close over it */
	for (int i = 0; i + 1 < sh.np; i++) {
		double th = atan2(sh.pz[i + 1] - sh.pz[i], sh.pa[i + 1] - sh.pa[i]);
		double am = (sh.pa[i] + sh.pa[i + 1]) * 0.5, zm = (sh.pz[i] + sh.pz[i + 1]) * 0.5;
		double lit = ue_lit(e, am, tm, zm, th);
		if (sh.contact) {
			double to_desk = sh.vis - (sh.ps[i] + sh.ps[i + 1]) * 0.5;
			lit *= 1.0 - 0.30 * (1.0 - clampf(to_desk / 30.0, 0.0, 1.0));
		}
		ue_band(cr, e, panel, e->panel ? e->panel_x : e->sheet_x,
			e->panel ? e->panel_y : e->sheet_y, sh.ps[i], sh.ps[i + 1],
			sh.pa[i], sh.pz[i], sh.pa[i + 1], sh.pz[i + 1], lit);
	}
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_pattern_t *paper = cairo_pop_group(cr);
	if (panel != sheet) {
		cairo_pattern_destroy(panel);
	}
	cairo_pattern_destroy(sheet);
	ue_outline(cr, e, &sh, false);
	cairo_set_source(cr, paper);
	cairo_fill(cr);
	cairo_pattern_destroy(paper);

	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	/* where the panel comes to the desk, and goes on under it */
	if (sh.np > 0 && sh.contact) {
		int last = sh.np - 1;
		double x0, y0, x1, y1;
		ue_proj(e, sh.pa[last], e->t0, 0.0, &x0, &y0);
		ue_proj(e, sh.pa[last], e->t1, 0.0, &x1, &y1);
		cairo_set_source_rgba(cr, 0.10, 0.07, 0.04, 0.35);
		cairo_set_line_width(cr, 2.2);
		cairo_move_to(cr, x0, y0);
		cairo_line_to(cr, x1, y1);
		cairo_stroke(cr);
	}
	/* the crease, a ridge, catching the lamp along its top */
	if (zc > 1.0 && sh.np > 1) {
		double x0, y0, x1, y1;
		ue_proj(e, sh.sa[UE_BANDS], e->t0, zc, &x0, &y0);
		ue_proj(e, sh.sa[UE_BANDS], e->t1, zc, &x1, &y1);
		double k = clampf(zc / 40.0, 0.0, 1.0);
		cairo_set_source_rgba(cr, 1.0, 0.99, 0.95, 0.60 * k);
		cairo_set_line_width(cr, 1.8);
		cairo_move_to(cr, x0, y0);
		cairo_line_to(cr, x1, y1);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

void desk_draw_blank_sheet(cairo_t *cr, double x, double y, double w, double h,
	const char *title)
{
	sheet_paper(cr, x, y, w, h, title);
	sheet_edge(cr, x, y, w, h);
}

/* A crease pressed out flat again: a ridge a few pixels high, lit on the
 * side that faces the lamp and in its own shade on the other. `vertical`:
 * the crease runs up the sheet at x = at; otherwise across it at y = at. */
void desk_draw_crease_relief(cairo_t *cr, bool vertical, double at, double from, double to,
	double strength)
{
	if (strength < 0.01) {
		return;
	}
	double w = 16.0;
	/* the slope of the ridge turned towards the lamp is the lit one */
	double llx, lly, llz;
	double mid = (from + to) * 0.5;
	if (vertical) {
		lamp_toward_user(cr, at, mid, &llx, &lly, &llz);
	} else {
		lamp_toward_user(cr, mid, at, &llx, &lly, &llz);
	}
	double lit_side = (vertical ? llx : lly) >= 0.0 ? 1.0 : -1.0;
	cairo_save(cr);
	for (int k = 0; k < 2; k++) {
		double dir = k ? 1.0 : -1.0;
		bool lit = dir == lit_side;
		cairo_pattern_t *g = vertical
			? cairo_pattern_create_linear(at, 0, at + dir * w, 0)
			: cairo_pattern_create_linear(0, at, 0, at + dir * w);
		double a = strength * (lit ? 0.16 : 0.14);
		if (lit) {
			cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 0.99, 0.95, a * 1.4);
			cairo_pattern_add_color_stop_rgba(g, 1.0, 1.0, 0.99, 0.95, 0.0);
		} else {
			cairo_pattern_add_color_stop_rgba(g, 0.0, 0.10, 0.07, 0.04, a);
			cairo_pattern_add_color_stop_rgba(g, 1.0, 0.10, 0.07, 0.04, 0.0);
		}
		if (vertical) {
			cairo_rectangle(cr, fmin(at, at + dir * w), from, w, to - from);
		} else {
			cairo_rectangle(cr, from, fmin(at, at + dir * w), to - from, w);
		}
		cairo_set_source(cr, g);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 1.0, 0.99, 0.95, 0.45 * strength);
	if (vertical) {
		cairo_move_to(cr, at, from);
		cairo_line_to(cr, at, to);
	} else {
		cairo_move_to(cr, from, at);
		cairo_line_to(cr, to, at);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* The creases left in a sheet opened out to pw x ph from the box x0,y0 -
 * x1,y1 it had been folded down to: the sides came out first, as tall as
 * the sheet was then, and the ends after them, right across. Where the
 * sheet already reached the edge of the paper nothing was folded. */
void desk_draw_unfolded_creases(cairo_t *cr, double pw, double ph,
	double x0, double y0, double x1, double y1)
{
	const double none = 6.0;
	cairo_save(cr);
	desk_rounded(cr, 0, 0, pw, ph, DESK_RADIUS);
	cairo_clip(cr);
	if (x0 > none) {
		desk_draw_crease_relief(cr, true, x0, y0, y1, 1.0);
	}
	if (x1 < pw - none) {
		desk_draw_crease_relief(cr, true, x1, y0, y1, 1.0);
	}
	if (y0 > none) {
		desk_draw_crease_relief(cr, false, y0, 0, pw, 1.0);
	}
	if (y1 < ph - none) {
		desk_draw_crease_relief(cr, false, y1, 0, pw, 1.0);
	}
	cairo_restore(cr);
}

/* ---- the settings window's contents ---------------------------- */

static void ios_switch(cairo_t *cr, double x, double y, double w, double h, bool on)
{
	double r = h * 0.5;
	cairo_save(cr);
	desk_rounded(cr, x, y, w, h, r);
	cairo_clip(cr);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y, 0, y + h);
	if (on) {
		cairo_pattern_add_color_stop_rgb(g, 0.0, 0.32, 0.66, 0.28);
		cairo_pattern_add_color_stop_rgb(g, 1.0, 0.52, 0.84, 0.38);
	} else {
		cairo_pattern_add_color_stop_rgb(g, 0.0, 0.78, 0.78, 0.80);
		cairo_pattern_add_color_stop_rgb(g, 1.0, 0.93, 0.93, 0.95);
	}
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
	cairo_rectangle(cr, x, y, w, h * 0.34);
	cairo_fill(cr);
	cairo_restore(cr);

	double kx = on ? x + w - h : x;
	cairo_set_source_rgba(cr, 0, 0, 0, 0.26);
	cairo_arc(cr, kx + r, y + r + 1.0, r - 1.0, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_t *k = cairo_pattern_create_linear(0, y, 0, y + h);
	cairo_pattern_add_color_stop_rgb(k, 0.0, 1.00, 1.00, 1.00);
	cairo_pattern_add_color_stop_rgb(k, 1.0, 0.86, 0.86, 0.88);
	cairo_set_source(cr, k);
	cairo_arc(cr, kx + r, y + r, r - 1.0, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(k);

	cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
	cairo_set_line_width(cr, 1.0);
	desk_rounded(cr, x + 0.5, y + 0.5, w - 1, h - 1, r);
	cairo_stroke(cr);
}

static void group_table(cairo_t *cr, double x, double y, double w, double h)
{
	cairo_save(cr);
	desk_rounded(cr, x, y, w, h, 9.0);
	cairo_clip(cr);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_paint(cr);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y, 0, y + 12);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0, 0, 0, 0.07);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0, 0, 0, 0.0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
	cairo_set_source_rgba(cr, 0.60, 0.58, 0.55, 0.75);
	cairo_set_line_width(cr, 1.0);
	desk_rounded(cr, x + 0.5, y + 0.5, w - 1, h - 1, 9.0);
	cairo_stroke(cr);
}

static void chevron(cairo_t *cr, double x, double y, double s)
{
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.58, 0.58, 0.60, 1.0);
	cairo_set_line_width(cr, 2.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(cr, x, y - s);
	cairo_line_to(cr, x + s * 0.72, y);
	cairo_line_to(cr, x, y + s);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void desk_draw_settings_body(cairo_t *cr, double x, double y, double w, double h)
{
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_clip(cr);

	cairo_pattern_t *bg = cairo_pattern_create_linear(0, y, 0, y + h);
	cairo_pattern_add_color_stop_rgb(bg, 0.0, 0.867, 0.875, 0.886);
	cairo_pattern_add_color_stop_rgb(bg, 1.0, 0.812, 0.820, 0.836);
	cairo_set_source(cr, bg);
	cairo_paint(cr);
	cairo_pattern_destroy(bg);

	static const char *rows[] = { "Wi-Fi", "Bluetooth", "Do Not Disturb", "Brightness" };
	static const bool on[] = { true, false, true, false };
	double pad = 14.0;
	double rowh = 42.0;
	double gy = y + 16.0;

	/* the strip at the top is the desk showing through a cut-out */
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, 10.0);
	cairo_clip(cr);
	fill_wood(cr, x, y, w, 10.0);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
	cairo_paint(cr);
	cairo_restore(cr);

	show_text(cr, "GENERAL", x + pad + 6, gy + 10, 11.0, true, 0.42, 0.42, 0.45, 1.0, -1);
	gy += 20.0;
	double gh = rowh * 4;
	group_table(cr, x + pad, gy, w - pad * 2, gh);
	for (int i = 0; i < 4; i++) {
		double ry = gy + i * rowh;
		if (i) {
			cairo_set_source_rgba(cr, 0.72, 0.70, 0.68, 0.9);
			cairo_rectangle(cr, x + pad + 14, ry, w - pad * 2 - 14, 1);
			cairo_fill(cr);
		}
		show_text(cr, rows[i], x + pad + 14, ry + rowh * 0.5 + 5, 14.0,
			false, 0.16, 0.16, 0.18, 1.0, -1);
		if (i < 3) {
			ios_switch(cr, x + w - pad - 66, ry + rowh * 0.5 - 14, 52, 28, on[i]);
		} else {
			chevron(cr, x + w - pad - 22, ry + rowh * 0.5, 5.0);
			show_text(cr, "72%", x + w - pad - 34, ry + rowh * 0.5 + 5, 13.0,
				false, 0.46, 0.46, 0.50, 1.0, 1);
		}
	}
	gy += gh + 22.0;

	show_text(cr, "ABOUT", x + pad + 6, gy + 10, 11.0, true, 0.42, 0.42, 0.45, 1.0, -1);
	gy += 20.0;
	group_table(cr, x + pad, gy, w - pad * 2, rowh);
	show_text(cr, "Version", x + pad + 14, gy + rowh * 0.5 + 5, 14.0,
		false, 0.16, 0.16, 0.18, 1.0, -1);
	show_text(cr, DESK_RELEASE, x + w - pad - 16, gy + rowh * 0.5 + 5, 14.0,
		false, 0.46, 0.46, 0.50, 1.0, 1);

	cairo_restore(cr);
}
