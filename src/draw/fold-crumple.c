/* Part of draw.c: seven segment digits; folding a sheet of paper and crumpling it.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- seven segment digits, for the desk clock ------------------- */

/* ================================================================
 * Folding a sheet of paper.
 *
 * Folded by hand, not by a machine: the crease never lands on the middle,
 * so the under layer shows a lip along one edge; the crease itself wanders
 * and is not quite level; the burnished highlight along it breaks up; and
 * the free edge of the flap sits a little proud of the stack. A fold that
 * is exactly halfway, dead straight and perfectly flush reads as printed,
 * not as folded.
 * ================================================================ */

/* Where this fold went wrong, deterministically per sheet and per fold. */
static double fold_bias(unsigned seed, int idx)
{
	return (hash21(seed * 3.77 + idx * 11.3 + 1.0, 2.91) - 0.5) * 0.090;
}

/* A smooth wander along a crease, in pixels. */
static double fold_wob(unsigned seed, int idx, double t, double amp)
{
	double s = seed * 5.13 + idx * 17.7;
	double f = t * 3.0;
	double i = floor(f), u = f - i;
	u = u * u * (3.0 - 2.0 * u);
	double a = hash21(s + i, 7.3);
	double b = hash21(s + i + 1.0, 7.3);
	return ((a + (b - a) * u) - 0.5) * 2.0 * amp;
}

static void paint_paper_back(cairo_t *cr, double x, double y, double w, double h)
{
	if (w < 1 || h < 1) {
		return;
	}
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_clip(cr);
	cairo_pattern_t *pat = tile_pattern(cr, paper_tile(), 1.0, 1.0, CAIRO_FILTER_GOOD);
	cairo_set_source(cr, pat);
	cairo_paint(cr);
	cairo_pattern_destroy(pat);
	cairo_set_source_rgba(cr, 0.78, 0.70, 0.54, 0.28);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* Stroke a line that wanders, with the ink breaking up along it. */
static void wobble_line(cairo_t *cr, double x0, double y0, double x1, double y1,
	unsigned seed, int idx, double amp, double width, double r, double g, double b, double a)
{
	const int seg = 12;
	double dx = x1 - x0, dy = y1 - y0;
	double len = sqrt(dx * dx + dy * dy);
	if (len < 1.0) {
		return;
	}
	double nx = -dy / len, ny = dx / len;
	cairo_set_line_width(cr, width);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int i = 0; i < seg; i++) {
		double t0 = (double)i / seg, t1 = (double)(i + 1) / seg;
		double w0 = fold_wob(seed, idx, t0, amp), w1 = fold_wob(seed, idx, t1, amp);
		double fade = 0.45 + hash21(seed * 2.3 + idx * 5.1 + i, 3.7) * 0.75;
		cairo_set_source_rgba(cr, r, g, b, clampf(a * fade, 0.0, 1.0));
		cairo_move_to(cr, x0 + dx * t0 + nx * w0, y0 + dy * t0 + ny * w0);
		cairo_line_to(cr, x0 + dx * t1 + nx * w1, y0 + dy * t1 + ny * w1);
		cairo_stroke(cr);
	}
}

/* The cut edge of a sheet, which is never flush with the stack under it. */
static void free_edge_h(cairo_t *cr, double y, double w, unsigned seed, int idx)
{
	wobble_line(cr, 0, y, w, y, seed, idx, 1.2, 2.6, 0.97, 0.94, 0.86, 0.95);
	wobble_line(cr, 0, y + 1.4, w, y + 1.4, seed, idx, 1.2, 1.1, 0.40, 0.30, 0.17, 0.42);
}

static void free_edge_v(cairo_t *cr, double x, double h, unsigned seed, int idx)
{
	wobble_line(cr, x, 0, x, h, seed, idx, 1.2, 2.6, 0.97, 0.94, 0.86, 0.95);
	wobble_line(cr, x + 1.4, 0, x + 1.4, h, seed, idx, 1.2, 1.1, 0.40, 0.30, 0.17, 0.42);
}

/* ---- the folding itself ---------------------------------------------
 *
 * A fold takes the part of the stack beyond the crease -- the bottom part
 * when the crease runs across, the right part when it runs down -- and
 * turns it over onto the rest. It is paper, not a plate on a hinge: the
 * part near the crease goes over first and the free end trails and then
 * catches up, the lifted part comes up towards you and so looks bigger,
 * it is lit by the lamp according to which way it faces, it throws its
 * shadow on the paper under it, and once it is past upright what you see
 * of it is its back.
 *
 * The stack is followed from above and from below as it goes. From above
 * is what you see; from below never changes but by cropping -- the part
 * that stays keeps its underside -- so it is the sheet's own back, cut
 * down. That is what lets something be printed on the back where it will
 * end up outside: what the sheet is, for the packet on the desk. */

enum { FOLD_ACROSS = 0, FOLD_DOWN = 1 };
#define FOLD_MAX 4
#define FOLD_STRIPS 30
#define FOLD_DONE 0.86

struct fold_step {
	int axis;        /* FOLD_ACROSS: the bottom part comes up. FOLD_DOWN: the right part comes left */
	double len;      /* the stack's length along the fold before it */
	double crease;   /* how far from the top, or the left, the crease runs */
};

struct desk_fold {
	cairo_surface_t *face;    /* the sheet printed side up, as it lay */
	cairo_surface_t *back;    /* its back, and what is printed on it */
	int w, h;
	unsigned seed;
	int n;
	struct fold_step step[FOLD_MAX];
	int sw[FOLD_MAX + 1], sh[FOLD_MAX + 1];
	cairo_surface_t *top[FOLD_MAX + 1];   /* the stack from above after k folds */
	double tilt;
	bool book;                /* a notebook: it shuts, it does not fold */
	cairo_surface_t *cover;   /* and it shut, the size of face, its name on it */
	/* Folded by a hand coming down it: the top part comes down, the first thing it does, and
	 * what lies under stays where it was. All of this is the bottom coming up on the sheet
	 * upside down, and drawn upside down. */
	bool by_hand;
};

/* A notebook, by t: its cover down, and then the size it lies at */
#define BOOK_SHUT 0.46

/* Which fold is under way at t, and how far through it. */
static void fold_at(const struct desk_fold *f, double t, int *k, double *p)
{
	if (f->book) {
		*k = t >= BOOK_SHUT ? 1 : 0;
		*p = *k ? 0.0 : t / BOOK_SHUT;
		return;
	}
	double u = clampf(t / FOLD_DONE, 0.0, 1.0) * f->n;
	int kk = (int)floor(u);
	if (kk >= f->n) {
		*k = f->n;
		*p = 0.0;
		return;
	}
	*k = kk;
	*p = u - kk;
}

/* The lamp, as the fold sees it from (x, y) in cr's terms: along the
 * fold, across it, and up. */
static void fold_light(cairo_t *cr, int axis, double x, double y, double *l_al, double *l_ac,
	double *l_z)
{
	double lx, ly, lz;
	lamp_toward_user(cr, x, y, &lx, &ly, &lz);
	*l_al = axis == FOLD_ACROSS ? ly : lx;
	*l_ac = axis == FOLD_ACROSS ? lx : ly;
	*l_z = lz;
}

/* Lay down the rectangle of src that lies between along0 and along1 (in
 * the stack's coordinates, full width across) so that its along0 edge goes
 * to d0 and its along1 edge to d1, and across it is scaled by s about cac.
 * d1 < d0 turns it over. */
static void fold_blit(cairo_t *cr, cairo_surface_t *src, int axis, double along0, double along1,
	double d0, double d1, double s, double cac, double across_len)
{
	double sc = (d1 - d0) / (along1 - along0);
	double lo = fmin(d0, d1) - 0.45, hi = fmax(d0, d1) + 0.45;
	double a0 = cac - cac * s, a1 = cac + (across_len - cac) * s;
	cairo_save(cr);
	if (axis == FOLD_ACROSS) {
		cairo_rectangle(cr, a0, lo, a1 - a0, hi - lo);
		cairo_clip(cr);
		cairo_translate(cr, cac * (1.0 - s), d0 - along0 * sc);
		cairo_scale(cr, s, sc);
	} else {
		cairo_rectangle(cr, lo, a0, hi - lo, a1 - a0);
		cairo_clip(cr);
		cairo_translate(cr, d0 - along0 * sc, cac * (1.0 - s));
		cairo_scale(cr, sc, s);
	}
	cairo_set_source_surface(cr, src, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
}

static void fold_band(cairo_t *cr, int axis, double at, double depth, double across_len,
	double r, double g, double b, double a0, double a1)
{
	/* a band of shade or light, from strength a0 at `at` to a1 `depth` on */
	cairo_pattern_t *pat = axis == FOLD_ACROSS
		? cairo_pattern_create_linear(0, at, 0, at + depth)
		: cairo_pattern_create_linear(at, 0, at + depth, 0);
	cairo_pattern_add_color_stop_rgba(pat, 0.0, r, g, b, a0);
	cairo_pattern_add_color_stop_rgba(pat, 1.0, r, g, b, a1);
	double lo = fmin(at, at + depth), len = fabs(depth);
	if (axis == FOLD_ACROSS) {
		cairo_rectangle(cr, 0, lo, across_len, len);
	} else {
		cairo_rectangle(cr, lo, 0, len, across_len);
	}
	cairo_set_source(cr, pat);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
}

/* The part beyond the crease of the stack before fold k, turned p of the
 * way over, and everything that goes with it. `from_above` is that stack
 * seen from above. */
static void fold_flap(cairo_t *cr, const struct desk_fold *f, int k, double p,
	cairo_surface_t *from_above)
{
	const struct fold_step *st = &f->step[k];
	int axis = st->axis;
	double across_len = axis == FOLD_ACROSS ? f->sw[k] : f->sh[k];
	double len = st->len, c = st->crease, L = len - c;
	if (L < 1.0) {
		return;
	}
	double e = p * p * (3.0 - 2.0 * p);
	double turn = M_PI * e;
	/* the free end trails while it is moving, and catches up as it lands */
	double lag = 0.30 * sin(M_PI * e);
	/* a camera well above the stack, looking down on the middle of it */
	double D = 2.8 * fmax(f->w, f->h);
	double cal = len * 0.5, cac = across_len * 0.5;

	double al[FOLD_STRIPS + 1], z[FOLD_STRIPS + 1], ph[FOLD_STRIPS];
	al[0] = c;
	z[0] = 0.0;
	double ds = L / FOLD_STRIPS;
	for (int i = 0; i < FOLD_STRIPS; i++) {
		double u = (i + 0.5) / FOLD_STRIPS;
		ph[i] = turn * (1.0 - lag * u * u);
		al[i + 1] = al[i] + cos(ph[i]) * ds;
		z[i + 1] = z[i] + sin(ph[i]) * ds;
	}
	double l_al, l_ac, l_z;
	fold_light(cr, axis, axis == FOLD_ACROSS ? cac : cal, axis == FOLD_ACROSS ? cal : cac,
		&l_al, &l_ac, &l_z);

	/* its shadow, on what lies under it: each strip thrown away from the
	 * lamp by its height, fainter and softer the higher it is */
	if (p > 0.0 && p < 1.0) {
		for (int i = 0; i < FOLD_STRIPS; i++) {
			double zm = (z[i] + z[i + 1]) * 0.5;
			if (zm < 0.5) {
				continue;
			}
			/* a lamp well above: the shadow does not run the length of the
			 * desk, however high the flap stands */
			double zs = fmin(zm, L * 0.45);
			double sh_al = -zs * l_al / l_z, sh_ac = -zs * l_ac / l_z;
			double a = 0.085 * clampf(1.0 - zm / (L * 1.3), 0.2, 1.0);
			double lo = fmin(al[i], al[i + 1]) + sh_al, hi = fmax(al[i], al[i + 1]) + sh_al;
			double grow = 1.5 + zm * 0.06;
			cairo_set_source_rgba(cr, 0.10, 0.07, 0.04, a);
			if (axis == FOLD_ACROSS) {
				cairo_rectangle(cr, sh_ac - grow, lo - grow, across_len + grow * 2,
					hi - lo + grow * 2);
			} else {
				cairo_rectangle(cr, lo - grow, sh_ac - grow, hi - lo + grow * 2,
					across_len + grow * 2);
			}
			cairo_fill(cr);
		}
	}

	/* the strips, lowest first */
	int order[FOLD_STRIPS];
	for (int i = 0; i < FOLD_STRIPS; i++) {
		order[i] = i;
	}
	for (int i = 1; i < FOLD_STRIPS; i++) {
		int o = order[i], j = i - 1;
		double zo = z[o] + z[o + 1];
		while (j >= 0 && z[order[j]] + z[order[j] + 1] > zo) {
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = o;
	}
	double flat = 0.55 + 0.45 * l_z;
	for (int n = 0; n < FOLD_STRIPS; n++) {
		int i = order[n];
		double zm = (z[i] + z[i + 1]) * 0.5;
		double s = D / (D - zm);
		double d0 = cal + (al[i] - cal) * D / (D - z[i]);
		double d1 = cal + (al[i + 1] - cal) * D / (D - z[i + 1]);
		bool up = cos(ph[i]) >= 0.0;
		double s0 = c + L * i / FOLD_STRIPS, s1 = c + L * (i + 1) / FOLD_STRIPS;
		fold_blit(cr, up ? from_above : f->back, axis, s0, s1, d0, d1, s, cac, across_len);

		/* lit by the side that faces up */
		double n_al = -sin(ph[i]), n_z = cos(ph[i]);
		if (!up) {
			n_al = -n_al;
			n_z = -n_z;
		}
		double lit = (0.55 + 0.45 * fmax(0.0, n_al * l_al + n_z * l_z)) / flat;
		double lo = fmin(d0, d1) - 0.45, hi = fmax(d0, d1) + 0.45;
		double a0 = cac - cac * s, a1 = cac + (across_len - cac) * s;
		if (fabs(lit - 1.0) > 0.004) {
			if (lit < 1.0) {
				cairo_set_source_rgba(cr, 0.06, 0.04, 0.02, clampf((1.0 - lit) * 0.95, 0, 0.6));
			} else {
				cairo_set_source_rgba(cr, 1.0, 0.98, 0.93, clampf((lit - 1.0) * 0.9, 0, 0.3));
			}
			if (axis == FOLD_ACROSS) {
				cairo_rectangle(cr, a0, lo, a1 - a0, hi - lo);
			} else {
				cairo_rectangle(cr, lo, a0, hi - lo, a1 - a0);
			}
			cairo_fill(cr);
		}
	}

	/* the cut edge at the end of the flap catches the light */
	if (p > 0.02) {
		double dz = z[FOLD_STRIPS];
		double de = cal + (al[FOLD_STRIPS] - cal) * D / (D - dz);
		double s = D / (D - dz);
		double a0 = cac - cac * s, a1 = cac + (across_len - cac) * s;
		cairo_set_line_width(cr, 1.3);
		cairo_set_source_rgba(cr, 1.0, 0.98, 0.93, 0.55);
		if (axis == FOLD_ACROSS) {
			cairo_move_to(cr, a0, de);
			cairo_line_to(cr, a1, de);
		} else {
			cairo_move_to(cr, de, a0);
			cairo_line_to(cr, de, a1);
		}
		cairo_stroke(cr);
	}
}

/* The stack after k folds, from above, worked out once. */
static cairo_surface_t *fold_stage(struct desk_fold *f, int k)
{
	if (f->top[k]) {
		return f->top[k];
	}
	cairo_surface_t *prev = fold_stage(f, k - 1);
	const struct fold_step *st = &f->step[k - 1];
	int axis = st->axis;
	int w = f->sw[k], h = f->sh[k];
	double across_len = axis == FOLD_ACROSS ? w : h;
	double c = st->crease, len = st->len;
	cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(dst);
	/* the part that stays where it was */
	cairo_set_source_surface(cr, prev, 0, 0);
	cairo_paint(cr);
	/* where the flap's cut edge lands short of the crease's twin, the edge
	 * of it lies on what is under it and shades it */
	double edge = 2.0 * c - len;
	if (edge > 1.0) {
		fold_band(cr, axis, edge, -7.0, across_len, 0.10, 0.07, 0.03, 0.30, 0.0);
	}
	/* the flap, landed: turned all the way over, back up */
	fold_flap(cr, f, k - 1, 1.0, prev);
	/* its cut edge, a hair proud of the stack; and the fold itself, which
	 * is not a line but a rounded edge of paper, lit on top and going into
	 * shade as it turns under */
	if (axis == FOLD_ACROSS) {
		free_edge_h(cr, fmax(edge, 0.6), w, f->seed, k * 3 + 1);
	} else {
		free_edge_v(cr, fmax(edge, 0.6), h, f->seed, k * 3 + 1);
	}
	fold_band(cr, axis, c - 7.0, 4.0, across_len, 1.0, 0.98, 0.93, 0.0, 0.20);
	fold_band(cr, axis, c - 3.0, 3.0, across_len, 0.16, 0.11, 0.06, 0.06, 0.34);
	cairo_destroy(cr);
	f->top[k] = dst;
	return dst;
}

/* What the sheet is, printed where it will end up on the outside of the
 * packet, the right way up once it is folded. */
static void fold_label(cairo_t *cr, double rw, double rh, cairo_surface_t *icon,
	const char *name, const char *initial)
{
	double pad = fmin(rw, rh) * 0.10;
	double fs = clampf(rh * 0.125, 9.0, 19.0);
	double name_h = fs * 1.55;
	double icon_s = fmin(rh - pad * 2.0 - name_h, rw * 0.48);
	double top = pad + (rh - pad * 2.0 - name_h - fmax(icon_s, 0.0)) * 0.42;
	if (icon_s >= 10.0) {
		double ix = (rw - icon_s) * 0.5;
		if (icon) {
			/* an icon of any shape, whole and in the middle of its square */
			double iw = cairo_image_surface_get_width(icon);
			double ih = cairo_image_surface_get_height(icon);
			double k = icon_s / fmax(iw, ih);
			cairo_save(cr);
			cairo_translate(cr, ix + (icon_s - iw * k) * 0.5, top + (icon_s - ih * k) * 0.5);
			cairo_scale(cr, k, k);
			cairo_set_source_surface(cr, icon, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			cairo_paint_with_alpha(cr, 0.93);
			cairo_restore(cr);
		} else {
			/* no icon: its letter, pressed into the paper */
			cairo_set_source_rgba(cr, 0.22, 0.17, 0.12, 0.14);
			desk_rounded_path(cr, ix, top, icon_s, icon_s, icon_s * 0.18);
			cairo_fill(cr);
			double gs = icon_s * 0.62;
			double gw = word_width(cr, initial, gs);
			print_word(cr, initial, ix + (icon_s - gw) * 0.5, top + icon_s * 0.74, gs,
				0.24, 0.18, 0.12, 0.85);
		}
	} else {
		icon_s = 0.0;
	}
	/* and its name under it, set to fit */
	double size = fs;
	double ww = word_width(cr, name, size);
	double room = rw - pad * 1.6;
	if (ww > room) {
		size *= fmax(room / fmax(ww, 1.0), 0.62);
		ww = word_width(cr, name, size);
	}
	double base = top + icon_s + fs * 1.25;
	cairo_save(cr);
	cairo_rectangle(cr, pad * 0.5, base - fs * 1.2, rw - pad, fs * 1.6);
	cairo_clip(cr);
	print_word(cr, name, (rw - fmin(ww, room)) * 0.5, base, size, 0.20, 0.15, 0.10, 0.88);
	cairo_restore(cr);
}

static struct desk_fold *fold_new(cairo_surface_t *face, unsigned seed, double packet_w,
	cairo_surface_t *icon, const char *name, bool by_hand)
{
	struct desk_fold *f = calloc(1, sizeof *f);
	if (!f) {
		return NULL;
	}
	f->by_hand = by_hand;
	f->face = cairo_surface_reference(face);
	f->w = cairo_image_surface_get_width(face);
	f->h = cairo_image_surface_get_height(face);
	f->seed = seed;
	f->top[0] = cairo_surface_reference(face);
	f->sw[0] = f->w;
	f->sh[0] = f->h;

	/* Fold whichever way is furthest from a packet the size of a
	 * postcard, until it is one: a small sheet takes one fold, a large
	 * one four. Never exactly in half. */
	double tw = fmax(packet_w, 40.0), th = tw * 0.68;
	double cw = f->w, ch = f->h;
	while (f->n < FOLD_MAX) {
		double rx = cw / tw, ry = ch / th;
		if (fmax(rx, ry) < 1.45 && f->n > 0) {
			break;
		}
		int axis = ry >= rx || (by_hand && f->n == 0) ? FOLD_ACROSS : FOLD_DOWN;
		double len = axis == FOLD_ACROSS ? ch : cw;
		double crease = floor(len * (0.5 + fabs(fold_bias(seed, f->n))));
		f->step[f->n] = (struct fold_step){ axis, len, crease };
		if (axis == FOLD_ACROSS) {
			ch = crease;
		} else {
			cw = crease;
		}
		f->n++;
		f->sw[f->n] = (int)cw;
		f->sh[f->n] = (int)ch;
	}
	f->tilt = (hash21(seed * 4.9 + 2.0, 6.1) - 0.5) * 0.075;

	/* The back: the same paper, the printing on the front showing through
	 * faintly, and -- on the part that finishes up outside -- what it is. */
	f->back = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, f->w, f->h);
	cairo_t *cr = cairo_create(f->back);
	paint_paper_back(cr, 0, 0, f->w, f->h);
	cairo_set_source_surface(cr, face, 0, 0);
	cairo_paint_with_alpha(cr, 0.028);
	const struct fold_step *last = &f->step[f->n - 1];
	double rw = last->axis == FOLD_ACROSS ? f->sw[f->n - 1] : f->sh[f->n - 1];
	double rh = last->len - last->crease;
	char initial[8];
	desk_initial_of(name && *name ? name : "?", initial, sizeof initial);
	cairo_save(cr);
	if (last->axis == FOLD_ACROSS) {
		/* it lands upside down: print it that way (and by hand, it is drawn upside down) */
		if (by_hand) {
			cairo_translate(cr, 0, last->crease);
		} else {
			cairo_translate(cr, 0, last->len);
			cairo_scale(cr, 1, -1);
		}
		fold_label(cr, rw, rh, icon, name ? name : "", initial);
	} else {
		/* it lands mirrored, and the right way up is along its length */
		cairo_translate(cr, last->len, by_hand ? rw : 0.0);
		cairo_scale(cr, -1, by_hand ? -1 : 1);
		fold_label(cr, rh, rw, icon, name ? name : "", initial);
	}
	cairo_restore(cr);
	cairo_destroy(cr);
	return f;
}

struct desk_fold *desk_fold_new(cairo_surface_t *face, unsigned seed, double packet_w,
	cairo_surface_t *icon, const char *name)
{
	return fold_new(face, seed, packet_w, icon, name, false);
}

struct desk_fold *desk_fold_new_by_hand(cairo_surface_t *face, unsigned seed, double packet_w,
	cairo_surface_t *icon, const char *name)
{
	int w = cairo_image_surface_get_width(face), h = cairo_image_surface_get_height(face);
	cairo_surface_t *over = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(over);
	cairo_translate(cr, 0, h);
	cairo_scale(cr, 1, -1);
	cairo_set_source_surface(cr, face, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(over);
	struct desk_fold *f = fold_new(over, seed, packet_w, icon, name, true);
	cairo_surface_destroy(over);
	return f;
}

double desk_fold_hand_reach(const struct desk_fold *f)
{
	if (f->book) {
		return f->h * 0.6;
	}
	return 2.0 * (f->step[0].len - f->step[0].crease);
}

double desk_fold_hand_t(const struct desk_fold *f, double frac)
{
	frac = clampf(frac, 0.0, 1.0);
	if (f->book) {
		return frac * BOOK_SHUT;
	}
	/* the end of a flap turning over is only ever seen going across: how far over it has
	 * turned, for how far across it has come; and how far through the fold that is, as it is
	 * eased (fold_flap) */
	double e = acos(1.0 - 2.0 * frac) / M_PI;
	double p = 0.5 - sin(asin(1.0 - 2.0 * e) / 3.0);
	return clampf(p, 0.0, 1.0) * FOLD_DONE / f->n;
}

int desk_fold_hand_y(const struct desk_fold *f, double t)
{
	if (!f->by_hand) {
		return 0;
	}
	int k;
	double p;
	fold_at(f, clampf(t, 0.0, 1.0), &k, &p);
	return f->h - f->sh[k];
}

/* A notebook's fold: face is the picture of it open, wire and all (notebook.h),
 * and what it comes to is the notebook shut, its name on its label. */
struct desk_fold *desk_fold_new_book(cairo_surface_t *face, unsigned seed, double packet_w,
	const char *name)
{
	struct desk_fold *f = calloc(1, sizeof *f);
	if (!f) {
		return NULL;
	}
	f->book = true;
	f->n = 1;
	f->seed = seed;
	f->face = cairo_surface_reference(face);
	f->w = cairo_image_surface_get_width(face);
	f->h = cairo_image_surface_get_height(face);
	f->top[0] = cairo_surface_reference(face);
	f->sw[0] = f->w;
	f->sh[0] = f->h;
	f->sw[1] = (int)ceil(packet_w);
	f->sh[1] = (int)ceil(packet_w * desk_notebook_shut_aspect());
	f->top[1] = desk_notebook_shut_named(name, f->sw[1], f->sh[1]);
	f->cover = desk_notebook_shut_named(name, f->w, f->h);
	return f;
}

/* Between shut and lying where it lies: 0 .. 1. */
static double book_going(double t)
{
	double p = clampf((t - BOOK_SHUT) / (FOLD_DONE - BOOK_SHUT), 0.0, 1.0);
	return p * p * (3.0 - 2.0 * p);
}

void desk_fold_free(struct desk_fold *f)
{
	if (!f) {
		return;
	}
	for (int i = 0; i <= FOLD_MAX; i++) {
		if (f->top[i]) {
			cairo_surface_destroy(f->top[i]);
		}
	}
	cairo_surface_destroy(f->back);
	cairo_surface_destroy(f->face);
	cairo_surface_destroy(f->cover);
	free(f);
}

int desk_fold_stages(const struct desk_fold *f)
{
	return f ? f->n : 0;
}

int desk_fold_stage(const struct desk_fold *f, double t)
{
	int k;
	double p;
	if (!f) {
		return 0;
	}
	fold_at(f, clampf(t, 0.0, 1.0), &k, &p);
	return k;
}

double desk_fold_done(const struct desk_fold *f)
{
	return f && f->book ? BOOK_SHUT : FOLD_DONE;
}

void desk_fold_extent(struct desk_fold *f, double t, int *w, int *h, int *margin)
{
	if (f->book) {
		double p = book_going(t);
		*w = (int)ceil(f->w + (f->sw[1] - f->w) * p);
		*h = (int)ceil(f->h + (f->sh[1] - f->h) * p);
		/* its cover stands up over the desk to the left of it */
		*margin = t < BOOK_SHUT ? (int)ceil(0.40 * f->w) + 24 : 24;
		return;
	}
	int k;
	double p;
	fold_at(f, t, &k, &p);
	*w = f->sw[k];
	*h = f->sh[k];
	/* a flap coming up towards you is seen larger, so reaches a little
	 * outside the stack; and it throws its shadow */
	*margin = k < f->n ? (int)ceil(0.14 * fmax(*w, *h)) + 24 : 24;
}

void desk_fold_draw(cairo_t *cr, struct desk_fold *f, double t)
{
	t = clampf(t, 0.0, 1.0);
	if (f->book) {
		if (t < BOOK_SHUT) {
			desk_notebook_shutting(cr, f->face, f->cover, t / BOOK_SHUT);
			return;
		}
		double p = book_going(t);
		double w = f->w + (f->sw[1] - f->w) * p, h = f->h + (f->sh[1] - f->h) * p;
		cairo_save(cr);
		cairo_scale(cr, w / f->w, h / f->h);
		cairo_set_source_surface(cr, f->cover, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);
		/* and as it gets there, the picture of it made for that size */
		double a = clampf((p - 0.70) / 0.30, 0.0, 1.0);
		if (a > 0.0) {
			cairo_save(cr);
			cairo_scale(cr, w / f->sw[1], h / f->sh[1]);
			cairo_set_source_surface(cr, f->top[1], 0, 0);
			cairo_paint_with_alpha(cr, a);
			cairo_restore(cr);
		}
		return;
	}
	int k;
	double p;
	fold_at(f, t, &k, &p);
	cairo_surface_t *st = fold_stage(f, k);
	int w = f->sw[k], h = f->sh[k];

	/* lying where it was dropped, not square to the desk -- once it is
	 * folded, not while it is still the sheet it was */
	double tilt = f->tilt * clampf((t - 0.55) / 0.45, 0.0, 1.0);
	cairo_save(cr);
	if (f->by_hand) {
		cairo_translate(cr, 0, h);
		cairo_scale(cr, 1, -1);
	}
	cairo_translate(cr, w * 0.5, h * 0.5);
	cairo_rotate(cr, tilt);
	cairo_translate(cr, -w * 0.5, -h * 0.5);

	/* The stack's shadow on the desk. Once it is a packet it is thick
	 * enough to throw a firmer one. */
	int base_w = w, base_h = h;
	if (k < f->n && f->step[k].axis == FOLD_ACROSS) {
		base_h = (int)f->step[k].crease;
	} else if (k < f->n) {
		base_w = (int)f->step[k].crease;
	}
	double ox, oy;
	shadow_offset(cr, base_w * 0.5, base_h * 0.5, 6.0 + 3.0 * k, &ox, &oy);
	for (int i = 8; i >= 1; i--) {
		double g = i / 8.0;
		cairo_set_source_rgba(cr, 0.10, 0.07, 0.03, (0.030 + 0.006 * k) * (1.0 - g));
		cairo_rectangle(cr, ox * g - g * 3.0, oy * g - g * 1.5,
			base_w + g * 6.0, base_h + g * 4.0);
		cairo_fill(cr);
	}

	if (k >= f->n) {
		/* The packet. The leaves under the top one never stack flush, and
		 * the stack is not flat: it rises to the folded edges and sags a
		 * little between. */
		int layers = 1 << (f->n < 3 ? f->n : 3);
		for (int i = layers; i >= 1; i--) {
			double jx = (hash21(f->seed * 7.1 + i, 1.9) - 0.5) * 2.2;
			double jy = (hash21(f->seed * 7.1 + i, 8.3) - 0.5) * 2.2;
			double off = i * 0.75;
			cairo_set_source_rgb(cr, 0.86 - i * 0.02, 0.80 - i * 0.02, 0.67 - i * 0.02);
			cairo_rectangle(cr, off * 0.4 + jx, off + jy, w, h);
			cairo_fill(cr);
		}
		cairo_set_source_surface(cr, st, 0, 0);
		cairo_paint(cr);
		cairo_save(cr);
		cairo_rectangle(cr, 0, 0, w, h);
		cairo_clip(cr);
		cairo_pattern_t *puff = cairo_pattern_create_radial(w * 0.5, h * 0.46, fmin(w, h) * 0.1,
			w * 0.5, h * 0.5, fmax(w, h) * 0.72);
		cairo_pattern_add_color_stop_rgba(puff, 0.0, 1.0, 0.99, 0.95, 0.07);
		cairo_pattern_add_color_stop_rgba(puff, 0.6, 1.0, 0.99, 0.95, 0.0);
		cairo_pattern_add_color_stop_rgba(puff, 1.0, 0.10, 0.07, 0.03, 0.10);
		cairo_set_source(cr, puff);
		cairo_paint(cr);
		cairo_pattern_destroy(puff);
		cairo_restore(cr);
	} else {
		/* part way through fold k: the part that stays, and the flap */
		const struct fold_step *fs = &f->step[k];
		double across_len = fs->axis == FOLD_ACROSS ? w : h;
		cairo_save(cr);
		if (fs->axis == FOLD_ACROSS) {
			cairo_rectangle(cr, 0, 0, w, fs->crease);
		} else {
			cairo_rectangle(cr, 0, 0, fs->crease, h);
		}
		cairo_clip(cr);
		cairo_set_source_surface(cr, st, 0, 0);
		cairo_paint(cr);
		/* the crease coming in as the paper goes over it */
		fold_band(cr, fs->axis, fs->crease - 3.0, 3.0, across_len,
			0.14, 0.09, 0.04, 0.0, 0.34 * p);
		cairo_restore(cr);
		fold_flap(cr, f, k, p, st);
	}
	cairo_restore(cr);
}

/* The packet on its own -- no shadow, square to the page -- for laying in
 * the drawer, which turns it and gives it a shadow of its own there. */
cairo_surface_t *desk_fold_packet(struct desk_fold *f)
{
	if (!f) {
		return NULL;
	}
	if (f->book) {
		return cairo_surface_reference(f->top[1]);
	}
	cairo_surface_t *st = fold_stage(f, f->n);
	int w = f->sw[f->n], h = f->sh[f->n];
	/* the leaves under the top one show past it by a few pixels */
	cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w + 6, h + 9);
	cairo_t *cr = cairo_create(out);
	cairo_translate(cr, 2.0, 1.0);
	int layers = 1 << (f->n < 3 ? f->n : 3);
	for (int i = layers; i >= 1; i--) {
		double jx = (hash21(f->seed * 7.1 + i, 1.9) - 0.5) * 2.2;
		double jy = (hash21(f->seed * 7.1 + i, 8.3) - 0.5) * 2.2;
		double off = i * 0.75;
		cairo_set_source_rgb(cr, 0.86 - i * 0.02, 0.80 - i * 0.02, 0.67 - i * 0.02);
		cairo_rectangle(cr, off * 0.4 + jx, off + jy, w, h);
		cairo_fill(cr);
	}
	cairo_save(cr);
	if (f->by_hand) {
		cairo_translate(cr, 0, h);
		cairo_scale(cr, 1, -1);
	}
	cairo_set_source_surface(cr, st, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);
	cairo_pattern_t *puff = cairo_pattern_create_radial(w * 0.5, h * 0.46, fmin(w, h) * 0.1,
		w * 0.5, h * 0.5, fmax(w, h) * 0.72);
	cairo_pattern_add_color_stop_rgba(puff, 0.0, 1.0, 0.99, 0.95, 0.07);
	cairo_pattern_add_color_stop_rgba(puff, 0.6, 1.0, 0.99, 0.95, 0.0);
	cairo_pattern_add_color_stop_rgba(puff, 1.0, 0.10, 0.07, 0.03, 0.10);
	cairo_set_source(cr, puff);
	cairo_paint(cr);
	cairo_pattern_destroy(puff);
	cairo_destroy(cr);
	cairo_surface_flush(out);
	return out;
}

/* The old calls, for the preview: fold a picture there and then. */
void desk_paper_fold_size(cairo_surface_t *src, double t, unsigned seed, int *ow, int *oh)
{
	struct desk_fold *f = desk_fold_new(src, seed,
		fmax(cairo_image_surface_get_width(src) * 0.30, 90.0), NULL, NULL);
	int m;
	desk_fold_extent(f, t, ow, oh, &m);
	desk_fold_free(f);
}

void desk_draw_paper_fold(cairo_t *cr, cairo_surface_t *src, double t, unsigned seed)
{
	struct desk_fold *f = desk_fold_new(src, seed,
		fmax(cairo_image_surface_get_width(src) * 0.30, 90.0), NULL, NULL);
	desk_fold_draw(cr, f, t);
	desk_fold_free(f);
}

/* ================================================================
 * Crumpling a sheet.
 *
 * Thrown away, a sheet is screwed up into a ball first. The sheet is cut
 * into triangles, and every corner of every triangle has somewhere to go
 * in the ball: the edges are gathered first -- that is where the hands
 * are -- the middle last, and on the way everything buckles. A facet that
 * has turned over shows the back of the paper; each catches the lamp its
 * own way; and the folds between them are creases. Where the pieces land
 * in the ball has nothing to do with where they were, which is why a ball
 * of paper shows scraps of everything that was printed on it.
 * ================================================================ */

#define CR_COLS 12
#define CR_ROWS 9
#define CR_V ((CR_COLS + 1) * (CR_ROWS + 1))
#define CR_T (CR_COLS * CR_ROWS * 2)

struct cr_vtx {
	double x0, y0;      /* on the flat sheet */
	double x1, y1;      /* in the ball */
	double wx, wy;      /* which way it buckles on the way */
	double delay;
};

struct desk_crumple {
	cairo_surface_t *face;   /* the sheet, at `scale` of its size */
	cairo_surface_t *back;
	double scale;
	int w, h, cols, rows;
	struct cr_vtx v[CR_V];
	int tri[CR_T][3];
	double depth[CR_T], tone[CR_T];
	double cx, cy, R;
	cairo_surface_t *ball;   /* finished, for throwing */
	double ball_x, ball_y;   /* where its top left is, from the ball's centre */
};

struct desk_crumple *desk_crumple_new(cairo_surface_t *sheet, unsigned seed)
{
	struct desk_crumple *c = calloc(1, sizeof *c);
	if (!c) {
		return NULL;
	}
	c->w = cairo_image_surface_get_width(sheet);
	c->h = cairo_image_surface_get_height(sheet);
	/* kept at full size: the first moments are the sheet as it lay, and a
	 * picture drawn at its own size is drawn fastest (see the compositor,
	 * which draws the later moments smaller) */
	c->scale = 1.0;
	int fw = (int)ceil(c->w * c->scale), fh = (int)ceil(c->h * c->scale);
	c->face = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, fw, fh);
	cairo_t *cr = cairo_create(c->face);
	cairo_scale(cr, c->scale, c->scale);
	cairo_set_source_surface(cr, sheet, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	c->back = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, fw, fh);
	cr = cairo_create(c->back);
	paint_paper_back(cr, 0, 0, fw, fh);
	cairo_destroy(cr);

	/* a facet every 56 px -- at Medium every 70, at Low every 84, less than
	 * half as many: each
	 * is the picture laid on at its own slant, and redrawing two hundred of
	 * them was a frame and a half of an old machine's time, thirty times a
	 * second */
	enum desk_gfx gl = desk_gfx_level();
	double cell = gl <= DESK_GFX_LOW ? 84.0 : gl == DESK_GFX_MEDIUM ? 70.0 : 56.0;
	c->cols = (int)clampf(round(c->w / cell), 5, CR_COLS);
	c->rows = (int)clampf(round(c->h / cell), 4, CR_ROWS);
	c->cx = c->w * 0.5;
	c->cy = c->h * 0.5;
	/* a sheet screwed up tight: a sheet of A4 makes a ball the size of an
	 * orange */
	c->R = clampf(0.15 * sqrt((double)c->w * c->h), 30.0, 150.0);
	double twist = (hash21(seed * 1.7, 3.1) - 0.5) * 1.4;
	for (int j = 0; j <= c->rows; j++) {
		for (int i = 0; i <= c->cols; i++) {
			struct cr_vtx *v = &c->v[j * (c->cols + 1) + i];
			double nx = (double)i / c->cols * 2.0 - 1.0;
			double ny = (double)j / c->rows * 2.0 - 1.0;
			v->x0 = (nx + 1.0) * 0.5 * c->w;
			v->y0 = (ny + 1.0) * 0.5 * c->h;
			/* the square of the sheet into the round of the ball, lumpy */
			double dx = nx * sqrt(1.0 - ny * ny * 0.5);
			double dy = ny * sqrt(1.0 - nx * nx * 0.5);
			double r = sqrt(dx * dx + dy * dy);
			double a = atan2(dy, dx) + twist * (1.0 - r);
			double k = seed * 0.37 + i * 3.1 + j * 7.7;
			double lump = 0.84 + 0.20 * hash21(floor(a * 2.4 + 9.0) + seed, 5.3);
			double jx = (hash21(k, 1.1) - 0.5) * 0.34, jy = (hash21(k, 2.2) - 0.5) * 0.34;
			v->x1 = c->cx + c->R * (r * lump * cos(a) + jx);
			v->y1 = c->cy + c->R * (r * lump * sin(a) + jy);
			double ba = hash21(k, 3.3) * 2.0 * M_PI;
			v->wx = cos(ba) * c->R * 0.30;
			v->wy = sin(ba) * c->R * 0.30;
			double edge = fmax(fabs(nx), fabs(ny));
			v->delay = 0.30 * (1.0 - edge) + hash21(k, 4.4) * 0.07;
		}
	}
	int t = 0;
	for (int j = 0; j < c->rows; j++) {
		for (int i = 0; i < c->cols; i++) {
			int a = j * (c->cols + 1) + i, b = a + 1, d = a + c->cols + 1, e = d + 1;
			if (hash21(seed * 0.11 + i * 1.3 + j * 5.9, 6.6) < 0.5) {
				c->tri[t][0] = a; c->tri[t][1] = b; c->tri[t][2] = e; t++;
				c->tri[t][0] = a; c->tri[t][1] = e; c->tri[t][2] = d; t++;
			} else {
				c->tri[t][0] = a; c->tri[t][1] = b; c->tri[t][2] = d; t++;
				c->tri[t][0] = b; c->tri[t][1] = e; c->tri[t][2] = d; t++;
			}
		}
	}
	for (int k = 0; k < t; k++) {
		c->depth[k] = hash21(seed * 0.73 + k * 1.9, 7.7);
		c->tone[k] = hash21(seed * 0.29 + k * 2.3, 8.8) * 2.0 - 1.0;
	}
	return c;
}

void desk_crumple_free(struct desk_crumple *c)
{
	if (!c) {
		return;
	}
	cairo_surface_destroy(c->face);
	cairo_surface_destroy(c->back);
	if (c->ball) {
		cairo_surface_destroy(c->ball);
	}
	free(c);
}

static double cr_ease(double x)
{
	x = clampf(x, 0.0, 1.0);
	return x * x * (3.0 - 2.0 * x);
}

/* Where corner v has got to at u. */
static void cr_at(const struct desk_crumple *c, int vi, double u, double *x, double *y, double *q)
{
	const struct cr_vtx *v = &c->v[vi];
	double qq = cr_ease((u - v->delay) / (1.0 - v->delay));
	double buckle = sin(M_PI * qq) * (1.0 - 0.4 * qq);
	*x = v->x0 + (v->x1 - v->x0) * qq + v->wx * buckle;
	*y = v->y0 + (v->y1 - v->y0) * qq + v->wy * buckle;
	*q = qq;
}

void desk_crumple_extent(struct desk_crumple *c, double u, double *x0, double *y0,
	double *x1, double *y1)
{
	*x0 = *y0 = 1e9;
	*x1 = *y1 = -1e9;
	int nv = (c->cols + 1) * (c->rows + 1);
	for (int i = 0; i < nv; i++) {
		double x, y, q;
		cr_at(c, i, u, &x, &y, &q);
		*x0 = fmin(*x0, x);
		*y0 = fmin(*y0, y);
		*x1 = fmax(*x1, x);
		*y1 = fmax(*y1, y);
	}
	/* and the shadow under it */
	*x0 -= 6.0;
	*y0 -= 6.0;
	*x1 += 16.0;
	*y1 += 20.0;
}

static void cr_draw(cairo_t *cr, struct desk_crumple *c, double u, bool shadow)
{
	u = clampf(u, 0.0, 1.0);
	int nv = (c->cols + 1) * (c->rows + 1), nt = c->cols * c->rows * 2;
	double px[CR_V], py[CR_V], pq[CR_V];
	for (int i = 0; i < nv; i++) {
		cr_at(c, i, u, &px[i], &py[i], &pq[i]);
	}
	/* its shadow: the sheet's, at first, gathering into the ball's */
	double sx0 = 1e9, sy0 = 1e9, sx1 = -1e9, sy1 = -1e9;
	for (int i = 0; i < nv; i++) {
		sx0 = fmin(sx0, px[i]);
		sy0 = fmin(sy0, py[i]);
		sx1 = fmax(sx1, px[i]);
		sy1 = fmax(sy1, py[i]);
	}
	for (int k = 6; shadow && k >= 1; k--) {
		double g = k / 6.0;
		cairo_set_source_rgba(cr, 0.10, 0.07, 0.03, 0.05 * (1.0 - g));
		cairo_save(cr);
		cairo_translate(cr, (sx0 + sx1) * 0.5 + 4.0 + 5.0 * u, (sy0 + sy1) * 0.5 + 6.0 + 7.0 * u);
		cairo_scale(cr, (sx1 - sx0) * 0.5 + g * 7.0, (sy1 - sy0) * 0.5 * (0.9 + 0.1 * u) + g * 7.0);
		cairo_arc(cr, 0, 0, 1.0, 0, 2 * M_PI);
		cairo_restore(cr);
		cairo_fill(cr);
	}

	/* the facets, in the order the crumpling has piled them -- drawn as a
	 * group, so the shading below lands on paper and nowhere else */
	cairo_push_group(cr);
	int order[CR_T];
	double key[CR_T];
	for (int k = 0; k < nt; k++) {
		double q = (pq[c->tri[k][0]] + pq[c->tri[k][1]] + pq[c->tri[k][2]]) / 3.0;
		key[k] = c->depth[k] * q;
		order[k] = k;
	}
	for (int i = 1; i < nt; i++) {
		int o = order[i], j = i - 1;
		while (j >= 0 && key[order[j]] > key[o]) {
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = o;
	}
	/* one pattern for each side of the paper, moved for each facet: a facet
	 * is a triangle filled straight from the picture, not a picture pasted
	 * through a triangular hole, which costs a mask a time */
	cairo_pattern_t *pat[2];
	pat[0] = cairo_pattern_create_for_surface(c->face);
	pat[1] = cairo_pattern_create_for_surface(c->back);
	for (int i = 0; i < 2; i++) {
		cairo_pattern_set_filter(pat[i], CAIRO_FILTER_BILINEAR);
		cairo_pattern_set_extend(pat[i], CAIRO_EXTEND_PAD);
	}
	cairo_matrix_t to_pixels;
	cairo_matrix_init_scale(&to_pixels, c->scale, c->scale);
	for (int n = 0; n < nt; n++) {
		int k = order[n];
		int a = c->tri[k][0], b = c->tri[k][1], d = c->tri[k][2];
		double x0 = c->v[a].x0, y0 = c->v[a].y0;
		double dx1 = c->v[b].x0 - x0, dy1 = c->v[b].y0 - y0;
		double dx2 = c->v[d].x0 - x0, dy2 = c->v[d].y0 - y0;
		double X0 = px[a], Y0 = py[a];
		double dX1 = px[b] - X0, dY1 = py[b] - Y0, dX2 = px[d] - X0, dY2 = py[d] - Y0;
		double det = dx1 * dy2 - dx2 * dy1;
		double ddet = dX1 * dY2 - dX2 * dY1;
		if (fabs(det) < 1e-9 || fabs(ddet) < 0.02) {
			continue;   /* seen edge on */
		}
		/* the sheet's triangle onto where it has got to */
		cairo_matrix_t m;
		m.xx = (dX1 * dy2 - dX2 * dy1) / det;
		m.xy = (dX2 * dx1 - dX1 * dx2) / det;
		m.yx = (dY1 * dy2 - dY2 * dy1) / det;
		m.yy = (dY2 * dx1 - dY1 * dx2) / det;
		m.x0 = X0 - m.xx * x0 - m.xy * y0;
		m.y0 = Y0 - m.yx * x0 - m.yy * y0;
		if (cairo_matrix_invert(&m) != CAIRO_STATUS_SUCCESS) {
			continue;
		}
		cairo_matrix_t pm;
		cairo_matrix_multiply(&pm, &m, &to_pixels);
		bool flipped = (det > 0) != (ddet > 0);
		cairo_pattern_t *src = pat[flipped ? 1 : 0];
		cairo_pattern_set_matrix(src, &pm);
		double q = (pq[a] + pq[b] + pq[d]) / 3.0;
		/* a hair larger than itself, so the seams do not show */
		double gx = (px[a] + px[b] + px[d]) / 3.0, gy = (py[a] + py[b] + py[d]) / 3.0;
		int vv[3] = { a, b, d };
		for (int i = 0; i < 3; i++) {
			double ex = px[vv[i]] - gx, ey = py[vv[i]] - gy;
			double el = sqrt(ex * ex + ey * ey) + 1e-6;
			double X = px[vv[i]] + ex / el * 0.6, Y = py[vv[i]] + ey / el * 0.6;
			if (i == 0) {
				cairo_move_to(cr, X, Y);
			} else {
				cairo_line_to(cr, X, Y);
			}
		}
		cairo_close_path(cr);
		cairo_set_source(cr, src);
		/* each facet faces the lamp its own way */
		double tone = c->tone[k] * q;
		if (fabs(tone) < 0.02) {
			cairo_fill(cr);
		} else {
			cairo_fill_preserve(cr);
			if (tone < 0.0) {
				cairo_set_source_rgba(cr, 0.07, 0.05, 0.03, -tone * 0.38);
			} else {
				cairo_set_source_rgba(cr, 1.0, 0.98, 0.93, tone * 0.24);
			}
			cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);
			cairo_fill(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		}
		/* and the creases between them */
		if (q > 0.05) {
			cairo_set_line_width(cr, 0.9);
			cairo_set_source_rgba(cr, 0.18, 0.13, 0.08, 0.22 * q);
			cairo_move_to(cr, px[a], py[a]);
			cairo_line_to(cr, px[b], py[b]);
			cairo_line_to(cr, px[d], py[d]);
			cairo_close_path(cr);
			cairo_stroke(cr);
		}
	}
	cairo_pattern_destroy(pat[0]);
	cairo_pattern_destroy(pat[1]);
	/* round, by the end: lit on the lamp's side, in its own shade on the other */
	if (u > 0.35) {
		double k = cr_ease((u - 0.35) / 0.65);
		cairo_save(cr);
		cairo_pattern_t *g = cairo_pattern_create_radial(c->cx - c->R * 0.35, c->cy - c->R * 0.45,
			c->R * 0.1, c->cx, c->cy, c->R * 1.15);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 0.99, 0.95, 0.16 * k);
		cairo_pattern_add_color_stop_rgba(g, 0.55, 1.0, 0.99, 0.95, 0.0);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 0.06, 0.04, 0.02, 0.34 * k);
		cairo_set_source(cr, g);
		cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);
		cairo_paint(cr);
		cairo_pattern_destroy(g);
		cairo_restore(cr);
	}
	cairo_pop_group_to_source(cr);
	cairo_paint(cr);
}

void desk_crumple_draw(cairo_t *cr, struct desk_crumple *c, double u)
{
	cr_draw(cr, c, u, true);
}

void desk_crumple_ball(cairo_t *cr, struct desk_crumple *c, double x, double y,
	double scale, double angle, double lift)
{
	if (!c->ball) {
		/* the ball alone, once: in the air it throws its own shadow */
		double x0, y0, x1, y1;
		desk_crumple_extent(c, 1.0, &x0, &y0, &x1, &y1);
		x0 += 4.0;
		y0 += 4.0;
		x1 -= 14.0;
		y1 -= 18.0;
		int bw = (int)ceil(x1 - x0), bh = (int)ceil(y1 - y0);
		c->ball = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bw, bh);
		cairo_t *b = cairo_create(c->ball);
		cairo_translate(b, -x0, -y0);
		cr_draw(b, c, 1.0, false);
		cairo_destroy(b);
		c->ball_x = x0 - c->cx;
		c->ball_y = y0 - c->cy;
	}
	/* its shadow on the desk, left behind as it rises */
	double r = c->R * scale;
	for (int k = 6; k >= 1; k--) {
		double g = k / 6.0;
		cairo_set_source_rgba(cr, 0.10, 0.07, 0.03, 0.06 * (1.0 - g) / (1.0 + lift * 0.012));
		cairo_save(cr);
		cairo_translate(cr, x + lift * 0.30 + 6.0, y + lift * 0.55 + 8.0);
		cairo_scale(cr, r * 0.95 + g * (6.0 + lift * 0.08), r * 0.85 + g * (6.0 + lift * 0.08));
		cairo_arc(cr, 0, 0, 1.0, 0, 2 * M_PI);
		cairo_restore(cr);
		cairo_fill(cr);
	}
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_rotate(cr, angle);
	cairo_scale(cr, scale, scale);
	cairo_set_source_surface(cr, c->ball, c->ball_x, c->ball_y);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
}

double desk_crumple_radius(const struct desk_crumple *c)
{
	return c->R;
}
