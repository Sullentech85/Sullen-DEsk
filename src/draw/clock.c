/* Part of draw.c: the clock: its dot matrix, pages and keys.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- the dot matrix ---- */

/* Five columns of seven, bit 0 at the top: the character generator every
 * small LED and LCD sign has had since the seventies. Upper case only. */
static const unsigned char *dots_glyph(int ch)
{
	static const struct {
		char c;
		unsigned char col[5];
	} FONT[] = {
		{ '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } }, { '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } },
		{ '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } }, { '3', { 0x21, 0x41, 0x45, 0x4B, 0x31 } },
		{ '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } }, { '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
		{ '6', { 0x3C, 0x4A, 0x49, 0x49, 0x30 } }, { '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
		{ '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } }, { '9', { 0x06, 0x49, 0x49, 0x29, 0x1E } },
		{ 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } }, { 'B', { 0x7F, 0x49, 0x49, 0x49, 0x36 } },
		{ 'C', { 0x3E, 0x41, 0x41, 0x41, 0x22 } }, { 'D', { 0x7F, 0x41, 0x41, 0x22, 0x1C } },
		{ 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } }, { 'F', { 0x7F, 0x09, 0x09, 0x09, 0x01 } },
		{ 'G', { 0x3E, 0x41, 0x49, 0x49, 0x7A } }, { 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
		{ 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } }, { 'J', { 0x20, 0x40, 0x41, 0x3F, 0x01 } },
		{ 'K', { 0x7F, 0x08, 0x14, 0x22, 0x41 } }, { 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
		{ 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } }, { 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } },
		{ 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } }, { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
		{ 'Q', { 0x3E, 0x41, 0x51, 0x21, 0x5E } }, { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
		{ 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } }, { 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } },
		{ 'U', { 0x3F, 0x40, 0x40, 0x40, 0x3F } }, { 'V', { 0x1F, 0x20, 0x40, 0x20, 0x1F } },
		{ 'W', { 0x3F, 0x40, 0x38, 0x40, 0x3F } }, { 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } },
		{ 'Y', { 0x07, 0x08, 0x70, 0x08, 0x07 } }, { 'Z', { 0x61, 0x51, 0x49, 0x45, 0x43 } },
		{ '.', { 0x00, 0x60, 0x60, 0x00, 0x00 } }, { ':', { 0x00, 0x36, 0x36, 0x00, 0x00 } },
		{ '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } }, { '/', { 0x20, 0x10, 0x08, 0x04, 0x02 } },
		{ '%', { 0x23, 0x13, 0x08, 0x64, 0x62 } }, { '+', { 0x08, 0x08, 0x3E, 0x08, 0x08 } },
		{ '(', { 0x00, 0x1C, 0x22, 0x41, 0x00 } }, { ')', { 0x00, 0x41, 0x22, 0x1C, 0x00 } },
		{ '!', { 0x00, 0x00, 0x5F, 0x00, 0x00 } }, { '?', { 0x02, 0x01, 0x51, 0x09, 0x06 } },
		{ '=', { 0x14, 0x14, 0x14, 0x14, 0x14 } }, { '_', { 0x40, 0x40, 0x40, 0x40, 0x40 } },
		{ '<', { 0x08, 0x14, 0x22, 0x41, 0x00 } }, { '>', { 0x00, 0x41, 0x22, 0x14, 0x08 } },
		{ '\'', { 0x00, 0x05, 0x03, 0x00, 0x00 } }, { ',', { 0x00, 0x50, 0x30, 0x00, 0x00 } },
		{ '&', { 0x36, 0x49, 0x55, 0x22, 0x50 } }, { '#', { 0x14, 0x7F, 0x14, 0x7F, 0x14 } },
		{ '"', { 0x00, 0x07, 0x00, 0x07, 0x00 } }, { ';', { 0x00, 0x56, 0x36, 0x00, 0x00 } },
		{ '[', { 0x00, 0x7F, 0x41, 0x41, 0x00 } }, { ']', { 0x00, 0x41, 0x41, 0x7F, 0x00 } },
		{ '*', { 0x08, 0x2A, 0x1C, 0x2A, 0x08 } }, { '@', { 0x32, 0x49, 0x79, 0x41, 0x3E } },
		{ '$', { 0x24, 0x2A, 0x7F, 0x2A, 0x12 } }, { '|', { 0x00, 0x00, 0x7F, 0x00, 0x00 } },
		{ '~', { 0x02, 0x01, 0x02, 0x04, 0x02 } }, { '^', { 0x04, 0x02, 0x01, 0x02, 0x04 } },
		{ '{', { 0x00, 0x08, 0x36, 0x41, 0x00 } }, { '}', { 0x00, 0x41, 0x36, 0x08, 0x00 } },
		{ '\\', { 0x02, 0x04, 0x08, 0x10, 0x20 } },
	};
	if (ch >= 'a' && ch <= 'z') {
		ch -= 32;
	}
	for (size_t i = 0; i < sizeof FONT / sizeof *FONT; i++) {
		if (FONT[i].c == ch) {
			return FONT[i].col;
		}
	}
	return NULL;
}

static bool dots_can(const char *s)
{
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (*p != ' ' && !dots_glyph(*p)) {
			return false;
		}
	}
	return true;
}

/* Text as the matrix can have it: accented letters as their plain ones,
 * a printer's quotes and dashes as a typewriter's, full width letters as
 * narrow ones. False when something is left that it has no character for
 * -- a Japanese title, say -- and then out is not to be used. */
static bool dots_fold(const char *s, char *out, size_t n)
{
	/* U+00C0 to U+00FF; '?' where it takes two letters */
	static const char LATIN1[] =
		"AAAAAA?CEEEEIIIIDNOOOOOXOUUUUY??aaaaaa?ceeeeiiiidnooooo/ouuuuy?y";
	size_t k = 0;
	const unsigned char *p = (const unsigned char *)s;
	if (!n) {
		return false;
	}
	while (*p) {
		uint32_t c;
		if (p[0] < 0x80) {
			c = p[0];
			p += 1;
		} else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
			c = (uint32_t)(p[0] & 0x1F) << 6 | (p[1] & 0x3F);
			p += 2;
		} else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
			c = (uint32_t)(p[0] & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F);
			p += 3;
		} else {
			return false;
		}
		const char *rep = NULL;
		char one[2] = { 0, 0 };
		if (c < 0x80) {
			one[0] = (char)c;
		} else if (c >= 0xFF01 && c <= 0xFF5E) {
			one[0] = (char)(c - 0xFEE0);
		} else if (c == 0x3000 || c == 0xA0) {
			one[0] = ' ';
		} else if (c == 0x2018 || c == 0x2019 || c == 0x2032) {
			one[0] = '\'';
		} else if (c == 0x201C || c == 0x201D || c == 0x2033) {
			one[0] = '"';
		} else if ((c >= 0x2010 && c <= 0x2015) || c == 0x2212) {
			one[0] = '-';
		} else if (c == 0x2026) {
			rep = "...";
		} else if (c == 0xB7 || c == 0x2022) {
			one[0] = '.';
		} else if (c >= 0xC0 && c <= 0xFF) {
			one[0] = LATIN1[c - 0xC0];
			if (one[0] == '?') {
				rep = c == 0xDF ? "SS" : (c == 0xDE || c == 0xFE) ? "TH" : "AE";
				one[0] = 0;
			}
		} else {
			return false;
		}
		if (!rep) {
			if (one[0] != ' ' && !dots_glyph((unsigned char)one[0])) {
				return false;
			}
			rep = one;
		}
		for (const char *r = rep; *r; r++) {
			if (k + 1 >= n) {
				out[k] = '\0';
				return true;
			}
			out[k++] = *r;
		}
	}
	out[k] = '\0';
	return true;
}

static double dots_width(const char *s, double pitch)
{
	size_t n = strlen(s);
	return n ? (n * 6.0 - 1.0) * pitch : 0.0;
}

/* ---- print, kept ----
 *
 * Dot matrix print is a great many small shapes, each with its halo, and
 * the MEDIA page is drawn afresh sixteen times a second while a title runs
 * along it. So a thing once drawn is kept as a picture, made at the very
 * fraction of a pixel it was drawn at: laid down again a whole number of
 * pixels away it comes out exactly as drawing it would -- OVER onto what
 * is there is OVER onto nothing, then OVER onto what is there. Only where
 * the face is drawn a pixel to a unit, as the clock's is; anywhere else it
 * is simply drawn. */

typedef void (*kept_fn)(cairo_t *cr, const void *arg);

/* what a string is, for a key: all of it, however long */
static unsigned long long kept_hash(const char *s)
{
	unsigned long long h = 14695981039346656037ull;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		h = (h ^ *p) * 1099511628211ull;
	}
	return h;
}

#define KEPT_N 48
#define KEPT_M 3              /* pixels of margin for the halos */
static struct kept {
	char key[300];
	cairo_surface_t *img;
	int px, py;               /* its top left, from the pixel the origin is in */
	unsigned stamp;
} g_kept[KEPT_N];
static unsigned g_kept_stamp;

/* fn draws about its own origin, which is at (x, y), inside x0 .. x0 + w
 * by y0 .. y0 + h of it; key names everything that decides how it looks.
 *
 * The picture is made in device pixels, whatever the scale the target is
 * drawn at, and laid down on a pixel: a face printed one pixel to a pixel
 * (face_print) draws through a plain scale, so that is the case that
 * matters. Under a turn or a shear it is drawn afresh, as before. */
static void kept_draw(cairo_t *cr, const char *key, double x, double y, double x0, double y0,
	double w, double h, kept_fn fn, const void *arg)
{
	cairo_matrix_t m;
	cairo_get_matrix(cr, &m);
	double sc = m.xx;
	if (fabs(m.xy) > 1e-9 || fabs(m.yx) > 1e-9 || fabs(m.yy - m.xx) > 1e-6 * fabs(m.xx)
		|| sc < 0.05 || sc > 16.0 || w <= 0.0 || h <= 0.0
		|| w * sc > 4096.0 || h * sc > 512.0) {
		cairo_save(cr);
		cairo_translate(cr, x, y);
		fn(cr, arg);
		cairo_restore(cr);
		return;
	}
	double dx = x * sc + m.x0, dy = y * sc + m.y0;
	double ix = floor(dx), iy = floor(dy);
	/* to a sixty-fourth of a pixel: the same place always comes out the
	 * same, and nobody can see the rest */
	double fx = round((dx - ix) * 64.0) / 64.0, fy = round((dy - iy) * 64.0) / 64.0;
	if (fx >= 1.0) {
		ix += 1.0;
		fx = 0.0;
	}
	if (fy >= 1.0) {
		iy += 1.0;
		fy = 0.0;
	}
	int px = (int)floor(x0 * sc) - KEPT_M, py = (int)floor(y0 * sc) - KEPT_M;
	int pw = (int)ceil((x0 + w) * sc) + 1 + KEPT_M - px;
	int ph = (int)ceil((y0 + h) * sc) + 1 + KEPT_M - py;
	char full[300];
	snprintf(full, sizeof full, "%s|%.5f|%g|%g|%d|%d|%d|%d", key, sc, fx, fy, px, py, pw, ph);
	struct kept *k = NULL, *old = &g_kept[0];
	for (int i = 0; i < KEPT_N && !k; i++) {
		if (g_kept[i].img && strcmp(g_kept[i].key, full) == 0) {
			k = &g_kept[i];
		} else if (!g_kept[i].img || (old->img && g_kept[i].stamp < old->stamp)) {
			old = &g_kept[i];
		}
	}
	if (!k) {
		k = old;
		if (k->img) {
			cairo_surface_destroy(k->img);
		}
		k->img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
		snprintf(k->key, sizeof k->key, "%s", full);
		k->px = px;
		k->py = py;
		cairo_t *kc = cairo_create(k->img);
		cairo_translate(kc, fx - px, fy - py);
		cairo_scale(kc, sc, sc);
		fn(kc, arg);
		cairo_destroy(kc);
		cairo_surface_flush(k->img);
	}
	k->stamp = ++g_kept_stamp;
	cairo_save(cr);
	cairo_identity_matrix(cr);
	cairo_set_source_surface(cr, k->img, ix + k->px, iy + k->py);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* How big a dot is, as a part of the pitch between dots. Round dots with
 * a gap round each are what a dot matrix looks like, and they need a pitch
 * of a couple of pixels to be told apart from one another; at less they
 * are a grey smear. So the smaller the pitch comes out on the screen, the
 * nearer the dots are let to touch -- till they run into strokes, which
 * at that size is what the eye reads a dot matrix as anyway. */
static double dot_radius(cairo_t *cr, double pitch)
{
	cairo_matrix_t m;
	cairo_get_matrix(cr, &m);
	double dev = pitch * hypot(m.xx, m.yx);
	return pitch * clampf(0.42 + 0.14 * (2.4 - dev) / 1.2, 0.42, 0.56);
}

/* Dot matrix text drawn afresh, its top left at (x, y). */
static void dots_draw(cairo_t *cr, const char *s, double x, double y, double pitch, double lit)
{
	double r = dot_radius(cr, pitch);
	for (int pass = 0; pass < 2; pass++) {
		double cx = x;
		for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
			const unsigned char *g = dots_glyph(*p);
			for (int c = 0; c < 5; c++) {
				for (int rw = 0; rw < 7; rw++) {
					bool on = g && ((g[c] >> rw) & 1);
					if (on != (pass == 1)) {
						continue;
					}
					double dx = cx + c * pitch + pitch * 0.5, dy = y + rw * pitch + pitch * 0.5;
					cairo_new_sub_path(cr);
					cairo_arc(cr, dx, dy, r, 0, 2 * M_PI);
				}
			}
			cx += pitch * 6.0;
		}
		if (pass == 0) {
			led_ghost(cr);
			cairo_fill(cr);
		} else {
			led_fill_path(cr, lit);
		}
	}
}

struct dots_arg {
	const char *s;
	double pitch, lit;
};

static void dots_kept(cairo_t *cr, const void *arg)
{
	const struct dots_arg *a = arg;
	dots_draw(cr, a->s, 0.0, 0.0, a->pitch, a->lit);
}

/* Dot matrix text; align -1 left, 0 centre, 1 right of x. Returns its width. */
static double dots_text(cairo_t *cr, const char *s, double x, double y, double pitch, int align,
	double lit)
{
	double wd = dots_width(s, pitch);
	if (align == 0) {
		x -= wd * 0.5;
	} else if (align > 0) {
		x -= wd;
	}
	if (!s[0]) {
		return 0.0;
	}
	char key[200];
	snprintf(key, sizeof key, "dots|%.4f|%.4f|%016llx|%.60s", pitch, lit, kept_hash(s), s);
	struct dots_arg a = { s, pitch, lit };
	kept_draw(cr, key, x, y, 0.0, 0.0, wd, 7.0 * pitch, dots_kept, &a);
	return wd;
}

/* Print from a font, lit: its baseline starts at the origin. */
struct font_arg {
	const char *s;
	double fs, lit;
};

static void font_kept(cairo_t *cr, const void *arg)
{
	const struct font_arg *a = arg;
	desk_font_select(cr, DESK_FONT_ENGRAVE, a->s, true, false);
	cairo_set_font_size(cr, a->fs);
	cairo_move_to(cr, 0.0, 0.0);
	cairo_text_path(cr, a->s);
	led_fill_path(cr, a->lit);
}

/* Anything the matrix has no characters for -- a tray item's Japanese
 * title -- in the display's own light, from a font. */
static double led_text(cairo_t *cr, const char *s, double x, double y, double size, int align,
	double lit, double max_w)
{
	if (dots_can(s)) {
		double pitch = size / 7.0;
		double wd = dots_width(s, pitch);
		if (wd > max_w && wd > 0) {
			pitch *= max_w / wd;
		}
		return dots_text(cr, s, x, y, pitch, align, lit);
	}
	cairo_save(cr);
	desk_font_select(cr, DESK_FONT_ENGRAVE, s, true, false);
	double fs = size * 1.25;
	cairo_set_font_size(cr, fs);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, s, &ext);
	if (ext.x_advance > max_w && ext.x_advance > 0) {
		fs *= max_w / ext.x_advance;
		cairo_set_font_size(cr, fs);
		cairo_text_extents(cr, s, &ext);
	}
	double tx = align == 0 ? x - ext.x_advance * 0.5 : align > 0 ? x - ext.x_advance : x;
	double ty = y + size * 0.5 - (ext.y_bearing + ext.height * 0.5);
	char key[200];
	snprintf(key, sizeof key, "font|%.4f|%.4f|%016llx|%.60s", fs, lit, kept_hash(s), s);
	struct font_arg a = { s, fs, lit };
	kept_draw(cr, key, tx, ty, ext.x_bearing, ext.y_bearing, ext.width, ext.height, font_kept, &a);
	cairo_restore(cr);
	return ext.x_advance;
}

/* ---- a tray icon, lit through its own shape ---- */

/* ---- the pages ---- */

static void clk_speaker(cairo_t *cr, double x, double y, double s, bool muted, double lit)
{
	/* a box and a cone, and the sound, or a cross */
	cairo_rectangle(cr, x, y + s * 0.34, s * 0.22, s * 0.32);
	cairo_move_to(cr, x + s * 0.22, y + s * 0.34);
	cairo_line_to(cr, x + s * 0.50, y + s * 0.10);
	cairo_line_to(cr, x + s * 0.50, y + s * 0.90);
	cairo_line_to(cr, x + s * 0.22, y + s * 0.66);
	cairo_close_path(cr);
	led_fill_path(cr, lit);
	cairo_save(cr);
	cairo_set_line_width(cr, s * 0.08);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	if (muted) {
		cairo_move_to(cr, x + s * 0.64, y + s * 0.32);
		cairo_line_to(cr, x + s * 0.94, y + s * 0.68);
		cairo_move_to(cr, x + s * 0.94, y + s * 0.32);
		cairo_line_to(cr, x + s * 0.64, y + s * 0.68);
	} else {
		cairo_arc(cr, x + s * 0.50, y + s * 0.50, s * 0.22, -0.9, 0.9);
		cairo_new_sub_path(cr);
		cairo_arc(cr, x + s * 0.50, y + s * 0.50, s * 0.40, -0.9, 0.9);
	}
	cairo_path_t *p = cairo_copy_path(cr);
	led_on(cr, 0.25 * lit);
	cairo_set_line_width(cr, s * 0.16);
	cairo_stroke(cr);
	cairo_append_path(cr, p);
	cairo_path_destroy(p);
	led_on(cr, lit);
	cairo_set_line_width(cr, s * 0.08);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* A little bar of n blocks, lit to v. */
static void clk_blocks(cairo_t *cr, double x, double y, double w, double h, int n, double v,
	double lit, bool grow)
{
	double gap = w / n * 0.28, bw = (w - gap * (n - 1)) / n;
	int on = (int)lround(v * n);
	for (int pass = 0; pass < 2; pass++) {
		bool any = false;
		for (int i = 0; i < n; i++) {
			bool lit_one = i < on;
			if (lit_one != (pass == 1)) {
				continue;
			}
			double bh = grow ? h * (0.34 + 0.66 * (i + 1) / n) : h;
			cairo_rectangle(cr, x + i * (bw + gap), y + h - bh, bw, bh);
			any = true;
		}
		if (!any) {
			continue;
		}
		if (pass == 0) {
			led_ghost(cr);
			cairo_fill(cr);
		} else {
			led_fill_path(cr, lit);
		}
	}
}

/* A battery on its side, filled as far as it is, with a bolt through it
 * while it charges; nearly flat and running down, it blinks. */
static void clk_battery(cairo_t *cr, double bx, double by, double bw, double bh,
	const struct desk_supply *s, const struct tm *tmv, double lit)
{
	double v = fmin(fmax(s->battery, 0.0), 1.0);
	if (s->discharging && v < 0.10 && desk_gfx_level() != DESK_GFX_2D && (tmv->tm_sec & 1)) {
		lit *= 0.25;
	}
	cairo_save(cr);
	desk_rounded_path(cr, bx, by, bw, bh, 2.0);
	cairo_rectangle(cr, bx + bw, by + bh * 0.3, 2.5, bh * 0.4);
	led_on(cr, lit * 0.9);
	cairo_set_line_width(cr, 1.3);
	cairo_stroke(cr);
	cairo_restore(cr);
	cairo_new_path(cr);
	if (v > 0.0) {
		cairo_rectangle(cr, bx + 2.5, by + 2.5, (bw - 5.0) * v, bh - 5.0);
		led_fill_path(cr, lit);
	}
	if (s->charging) {
		cairo_move_to(cr, bx + bw * 0.58, by + 1.5);
		cairo_line_to(cr, bx + bw * 0.36, by + bh * 0.56);
		cairo_line_to(cr, bx + bw * 0.52, by + bh * 0.56);
		cairo_line_to(cr, bx + bw * 0.44, by + bh - 1.5);
		cairo_line_to(cr, bx + bw * 0.66, by + bh * 0.42);
		cairo_line_to(cr, bx + bw * 0.50, by + bh * 0.42);
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0.05, 0.01, 0.01, 0.9);
		cairo_fill_preserve(cr);
		cairo_set_line_width(cr, 0.8);
		led_on(cr, lit * 0.8);
		cairo_stroke(cr);
	}
}

/* The screen's light: a sun, r to the tips of its rays. */
static void clk_sun(cairo_t *cr, double cx, double cy, double r, double lit)
{
	cairo_new_path(cr);
	cairo_arc(cr, cx, cy, r * 0.42, 0, 2 * M_PI);
	led_fill_path(cr, lit);
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int k = 0; k < 8; k++) {
		double a = k * M_PI / 4.0;
		cairo_move_to(cr, cx + cos(a) * r * 0.66, cy + sin(a) * r * 0.66);
		cairo_line_to(cr, cx + cos(a) * r, cy + sin(a) * r);
	}
	cairo_path_t *p = cairo_copy_path(cr);
	led_on(cr, 0.25 * lit);
	cairo_set_line_width(cr, 2.6);
	cairo_stroke(cr);
	cairo_append_path(cr, p);
	cairo_path_destroy(p);
	led_on(cr, lit);
	cairo_set_line_width(cr, 1.3);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* A camera, the shape every machine puts on its lamp for one: a body and
 * the lens to its right, h high from its middle cy. Lit while one is in
 * use, and the ghost of the segment otherwise. */
static void clk_camera(cairo_t *cr, double x, double cy, double h, bool on, double lit)
{
	double bw = h * 1.15, r = h * 0.18;
	cairo_new_path(cr);
	desk_rounded_path(cr, x, cy - h * 0.5, bw, h, r);
	cairo_move_to(cr, x + bw + h * 0.10, cy - h * 0.16);
	cairo_line_to(cr, x + bw + h * 0.62, cy - h * 0.46);
	cairo_line_to(cr, x + bw + h * 0.62, cy + h * 0.46);
	cairo_line_to(cr, x + bw + h * 0.10, cy + h * 0.16);
	cairo_close_path(cr);
	if (on) {
		led_fill_path(cr, lit);
	} else {
		led_ghost(cr);
		cairo_fill(cr);
	}
}

/* A microphone, h high from its middle cy round mx: the capsule, the
 * fork it sits in, and the stand. Lit like the camera. */
static void clk_mic(cairo_t *cr, double mx, double cy, double h, bool on, double lit)
{
	double top = cy - h * 0.5, cw = h * 0.40;
	cairo_save(cr);
	cairo_new_path(cr);
	desk_rounded_path(cr, mx - cw * 0.5, top, cw, h * 0.60, cw * 0.5);
	if (on) {
		led_fill_path(cr, lit);
	} else {
		led_ghost(cr);
		cairo_fill(cr);
	}
	cairo_new_path(cr);
	cairo_arc_negative(cr, mx, top + h * 0.36, h * 0.34, M_PI, 0.0);
	cairo_move_to(cr, mx, top + h * 0.70);
	cairo_line_to(cr, mx, top + h * 0.90);
	cairo_move_to(cr, mx - h * 0.22, top + h * 0.95);
	cairo_line_to(cr, mx + h * 0.22, top + h * 0.95);
	cairo_set_line_width(cr, h * 0.11);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	if (on) {
		cairo_path_t *p = cairo_copy_path(cr);
		led_on(cr, 0.25 * lit);
		cairo_set_line_width(cr, h * 0.22);
		cairo_stroke(cr);
		cairo_append_path(cr, p);
		cairo_path_destroy(p);
		led_on(cr, lit);
		cairo_set_line_width(cr, h * 0.11);
	} else {
		led_ghost(cr);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

static void clk_page_time(cairo_t *cr, const struct tm *tmv, double lit)
{
	double x = CLK_WX, y = CLK_WY;
	char hhmm[8];
	int hour = tmv->tm_hour;
	if (g_clock_12h) {
		hour = hour % 12 ? hour % 12 : 12;
	}
	snprintf(hhmm, sizeof hhmm, "%02d%02d", hour, tmv->tm_min);
	double dh = 44.0, dw = 23.0;
	double tx = x + 12.0, ty = y + (CLK_WH - dh) * 0.5;
	struct seg_box b;
	for (int i = 0; i < 4; i++) {
		double cx = tx + i * (dw + 7.0) + (i >= 2 ? 11.0 : 0.0);
		b = (struct seg_box){ cx, ty, dw, dh, dh * 0.10 };
		/* a twelve-hour clock has no leading nought: that digit is dark */
		int d = hhmm[i] - '0';
		if (g_clock_12h && i == 0 && d == 0) {
			d = -1;
		}
		seg7_digit(cr, &b, d, lit);
	}
	if (g_clock_12h) {
		/* the little lamp a twelve-hour clock has for the afternoon */
		double px = tx + 4 * (dw + 7.0) + 11.0;
		dots_text(cr, tmv->tm_hour < 12 ? "AM" : "PM", px, ty + 1.0, 1.1, -1,
			lit * (tmv->tm_hour < 12 ? 0.55 : 1.0));
	}
	/* the colon, which beats -- in 2D, which has no seconds, it stays lit */
	bool flat = desk_gfx_level() == DESK_GFX_2D;
	bool beat = flat || (tmv->tm_sec & 1) == 0;
	double cx = tx + 2 * (dw + 7.0) - 1.5 + dh * 0.05;
	for (int k = 0; k < 2; k++) {
		cairo_arc(cr, cx, ty + dh * (k ? 0.70 : 0.30), 2.6, 0, 2 * M_PI);
		if (beat) {
			led_fill_path(cr, lit);
		} else {
			led_ghost(cr);
			cairo_fill(cr);
		}
	}

	/* Beside the minutes, small, in two rows: the lamps that say a camera
	 * and a microphone are in use (there only when there is one, and lit
	 * only while it is), and under them the seconds, on the minutes' foot.
	 * Past AM and PM, on a twelve-hour clock, which are up there too. */
	double bx0 = x + 150.0 + (g_clock_12h ? 16.0 : 0.0), bw0 = 36.0;
	if (g_clk.camera >= 0) {
		clk_camera(cr, bx0, ty + 8.0, 10.0, g_clk.camera > 0, lit);
	}
	if (g_clk.mic >= 0) {
		clk_mic(cr, bx0 + bw0 - 5.0, ty + 8.0, 12.0, g_clk.mic > 0, lit);
	}
	if (!flat) {
		double sh = 18.0, sw = 9.5, sx = bx0 + bw0 - (2.0 * sw + 3.5);
		for (int i = 0; i < 2; i++) {
			struct seg_box sb = { sx + i * (sw + 3.5), ty + dh - sh, sw, sh, sh * 0.10 };
			seg7_digit(cr, &sb, i ? tmv->tm_sec % 10 : tmv->tm_sec / 10, lit);
		}
	}

	/* To the right of those: the input method and the volume, on the
	 * time's own line -- or, on a machine with a battery or a screen whose
	 * light it sets, at the top, with those on a line of their own under. */
	const struct desk_supply *su = &g_clk.supply;
	bool second = su->have_battery || su->have_backlight;
	double rx = bx0 + bw0 + 14.0, cy = y + (second ? 20.0 : CLK_WH * 0.5);
	double row_x = rx;
	if (g_clk.ime[0]) {
		cairo_save(cr);
		desk_rounded_path(cr, rx, cy - 11.0, 26.0, 22.0, 3.0);
		led_on(cr, 0.55 * lit);
		cairo_set_line_width(cr, 1.2);
		cairo_stroke(cr);
		cairo_restore(cr);
		led_text(cr, g_clk.ime, rx + 13.0, cy - 7.5, 15.0, 0, lit, 21.0);
	}
	double vx = rx + (g_clk.ime[0] ? 38.0 : 0.0);
	if (g_clk.have_volume) {
		/* the blocks take what is left before the number */
		double bw = fmax(x + CLK_WW - 8.0 - 46.0 - (vx + 24.0), 30.0);
		clk_speaker(cr, vx, cy - 9.0, 18.0, g_clk.muted, lit);
		clk_blocks(cr, vx + 24.0, cy - 9.0, bw, 18.0, 10, g_clk.muted ? 0.0 : g_clk.volume,
			lit, true);
		char v[8];
		snprintf(v, sizeof v, "%d", (int)lround(g_clk.volume * 100.0));
		dots_text(cr, g_clk.muted ? "MUTE" : v, x + CLK_WW - 8.0, cy - 5.6, 1.6, 1, lit);
	}
	if (!second) {
		return;
	}
	/* the battery and the screen's light, one after the other */
	double by = y + 46.0, bx = row_x;
	if (su->have_battery) {
		clk_battery(cr, bx + 0.5, by - 6.5, 22.0, 13.0, su, tmv, lit);
		char pct[8];
		snprintf(pct, sizeof pct, "%d%%", (int)lround(su->battery * 100.0));
		bx += 32.0 + dots_text(cr, pct, bx + 32.0, by - 5.6, 1.6, -1, lit) + 18.0;
	}
	if (su->have_backlight) {
		clk_sun(cr, bx + 8.0, by, 8.0, lit);
		char pct[8];
		snprintf(pct, sizeof pct, "%d%%", (int)lround(su->backlight * 100.0));
		dots_text(cr, pct, bx + 22.0, by - 5.6, 1.6, -1, lit);
	}
}

static void clk_page_day(cairo_t *cr, const struct tm *tmv, double lit)
{
	/* in the letters fourteen segments have: no accents */
	static const char *const MON[4][12] = {
		{ "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" },
		{ "ENE", "FEB", "MAR", "ABR", "MAY", "JUN", "JUL", "AGO", "SEP", "OCT", "NOV", "DIC" },
		{ "JAN", "FEV", "MAR", "AVR", "MAI", "JUN", "JUL", "AOU", "SEP", "OCT", "NOV", "DEC" },
		{ "JAN", "FEB", "MAR", "APR", "MAI", "JUN", "JUL", "AUG", "SEP", "OKT", "NOV", "DEZ" },
	};
	char s[32];
	int m = tmv->tm_mon % 12, d = tmv->tm_mday, y = tmv->tm_year + 1900;
	enum desk_lang l = desk_lang_get();
	/* as a clock sold there has it: the year first in Chinese and Korean,
	 * the day first in Spanish, French and German, the month first in
	 * English (and Japanese, as this one always has) */
	if (l == DESK_LANG_ZH || l == DESK_LANG_KO) {
		snprintf(s, sizeof s, "%04d/%02d/%02d", y, m + 1, d);
	} else if (l == DESK_LANG_ES || l == DESK_LANG_FR || l == DESK_LANG_DE) {
		snprintf(s, sizeof s, "%02d/%s/%04d", d,
			MON[l == DESK_LANG_ES ? 1 : l == DESK_LANG_FR ? 2 : 3][m], y);
	} else {
		snprintf(s, sizeof s, "%s/%02d/%04d", MON[0][m], d, y);
	}
	double ch = 42.0, cw = 24.5, gap = 6.2;
	double total = strlen(s) * cw + (strlen(s) - 1) * gap;
	seg14_text(cr, s, CLK_WX + (CLK_WW - total) * 0.5 - ch * 0.05, CLK_WY + (CLK_WH - ch) * 0.5,
		cw, ch, gap, lit);
}

/* ---- MEDIA: what is playing ----
 *
 * A hi-fi's display: the title along a dot matrix the width of the glass,
 * running along it when it is too long to fit, the artist and the time
 * under it, a rule for how far through the track it is, and a row of
 * marks to press -- back, play or pause, on -- with the volume at the end.
 * Paused, the time blinks. Everything here is from the display's top left. */
#define MED_TX 10.0          /* the title's matrix, this wide */
#define MED_TW 336.0
#define MED_TY 6.0
#define MED_TP 2.0           /* its pitch */
#define MED_COLS 168         /* MED_TW / MED_TP */
#define MED_GAP 24           /* columns between the end of a running title and its start */
#define MED_SPEED 16.0       /* columns a second */
#define MED_HOLD 2.5         /* seconds it stands still at its start */
#define MED_AY 25.0          /* the artist and the time */
#define MED_AP 1.3
#define MED_PY 38.0          /* how far through: a rule of blocks */
#define MED_PH 3.0
#define MED_RY 44.0          /* the buttons and the volume, in a row this high */
#define MED_RH 16.0
#define MED_BX 10.0          /* three buttons */
#define MED_BW 28.0
#define MED_BGAP 4.0
#define MED_SX 232.0         /* the speaker */
#define MED_VX 250.0         /* the volume, in blocks */
#define MED_VW 62.0
#define MED_VN 10

/* What the title row says, folded for the matrix if it can be. */
static void med_title(char *out, size_t n, bool *dots)
{
	const struct desk_media *md = &g_clk.media;
	const char *t = !md->have ? "NOTHING PLAYING"
		: md->title[0] ? md->title : md->player[0] ? md->player : "NO TITLE";
	*dots = dots_fold(t, out, n);
	if (!*dots) {
		snprintf(out, n, "%s", t);
	}
}

static void med_font(cairo_t *cr)
{
	desk_font_select(cr, DESK_FONT_UI, NULL, true, false);
	cairo_set_font_size(cr, 13.0);
}

/* How wide the title is, in the matrix's columns. */
static int med_title_cols(const char *t, bool dots)
{
	if (dots) {
		return (int)strlen(t) * 6 - 1;
	}
	/* from a font: measured once a title */
	static char last[160];
	static int last_cols;
	if (strcmp(t, last) == 0) {
		return last_cols;
	}
	cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
	cairo_t *cr = cairo_create(sf);
	med_font(cr);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, t, &ext);
	cairo_destroy(cr);
	cairo_surface_destroy(sf);
	snprintf(last, sizeof last, "%s", t);
	last_cols = (int)ceil(ext.x_advance / MED_TP);
	return last_cols;
}

/* How far a title of cols columns has run along, now; and in how many
 * seconds it moves on (< 0: it stands). */
static int med_scroll(int cols, double *next)
{
	*next = -1.0;
	if (cols <= MED_COLS) {
		return 0;
	}
	int loop = cols + MED_GAP;
	double t = fmax(g_clk.now - g_clk.media.since, 0.0);
	double period = MED_HOLD + loop / MED_SPEED;
	double ph = fmod(t, period);
	if (ph < MED_HOLD) {
		*next = MED_HOLD - ph;
		return 0;
	}
	double c = (ph - MED_HOLD) * MED_SPEED;
	*next = (floor(c) + 1.0 - c) / MED_SPEED;
	return (int)floor(c) % loop;
}

/* The title row: a dot matrix the width of the glass, every dot there
 * whether lit or not, and the title's columns moved along it -- a whole
 * column, a whole number of pixels, at a time, so the lit columns are one
 * picture kept and laid down where they have got to. */
static void med_grid(cairo_t *cr, const void *arg)
{
	(void)arg;
	double r = dot_radius(cr, MED_TP);
	for (int c = 0; c < MED_COLS; c++) {
		for (int rw = 0; rw < 7; rw++) {
			cairo_new_sub_path(cr);
			cairo_arc(cr, (c + 0.5) * MED_TP, (rw + 0.5) * MED_TP, r, 0, 2 * M_PI);
		}
	}
	led_ghost(cr);
	cairo_fill(cr);
}

struct med_arg {
	const char *t;
	double lit;
};

/* the title's lit dots, from its first column */
static void med_lit(cairo_t *cr, const void *arg)
{
	const struct med_arg *a = arg;
	double r = dot_radius(cr, MED_TP);
	int tcols = (int)strlen(a->t) * 6 - 1;
	cairo_new_path(cr);
	for (int c = 0; c < tcols; c++) {
		const unsigned char *g = c % 6 < 5 ? dots_glyph((unsigned char)a->t[c / 6]) : NULL;
		for (int rw = 0; g && rw < 7; rw++) {
			if ((g[c % 6] >> rw) & 1) {
				cairo_new_sub_path(cr);
				cairo_arc(cr, (c + 0.5) * MED_TP, (rw + 0.5) * MED_TP, r, 0, 2 * M_PI);
			}
		}
	}
	led_fill_path(cr, a->lit);
}

/* the title from a font, its baseline's start at the origin */
static void med_lit_font(cairo_t *cr, const void *arg)
{
	const struct med_arg *a = arg;
	med_font(cr);
	cairo_new_path(cr);
	cairo_move_to(cr, 0.0, 0.0);
	cairo_text_path(cr, a->t);
	led_fill_path(cr, a->lit);
}

static void med_title_row(cairo_t *cr, const char *t, bool dots, double x, double y, int off,
	bool centre, double lit)
{
	int tcols = med_title_cols(t, dots);
	bool run = tcols > MED_COLS;
	int loop = tcols + MED_GAP;
	int start = centre && !run ? (MED_COLS - tcols) / 2 : 0;
	if (dots) {
		kept_draw(cr, "medgrid", x, y, 0.0, 0.0, MED_TW, 7.0 * MED_TP, med_grid, NULL);
	}
	cairo_save(cr);
	cairo_rectangle(cr, x, y - 3.0, MED_TW, 7.0 * MED_TP + 6.0);
	cairo_clip(cr);
	char key[200];
	snprintf(key, sizeof key, "medtitle|%d|%.4f|%016llx|%.60s", dots, lit, kept_hash(t), t);
	struct med_arg a = { t, lit };
	double ty = 0.0, x0 = 0.0, y0 = 0.0, h = 7.0 * MED_TP, w = tcols * MED_TP;
	if (!dots) {
		/* the font's box, centred on the row */
		cairo_save(cr);
		med_font(cr);
		cairo_text_extents_t ext;
		cairo_text_extents(cr, t, &ext);
		cairo_restore(cr);
		ty = 7.0 * MED_TP * 0.5 - (ext.y_bearing + ext.height * 0.5);
		x0 = ext.x_bearing;
		y0 = ext.y_bearing;
		h = ext.height;
		w = ext.width;
	}
	/* The title moves a whole number of device pixels at a time, so that
	 * it lands on the same fraction of a pixel every time and is one kept
	 * picture, not a new one for each step. */
	cairo_matrix_t cm;
	cairo_get_matrix(cr, &cm);
	double sc = cm.xx > 0.05 ? cm.xx : 1.0;
	for (int k = 0; k < (run ? 2 : 1); k++) {
		double cx = x + (start + k * loop - off) * MED_TP;
		double snapped = x + round((cx - x) * sc) / sc;
		kept_draw(cr, key, snapped, y + ty, x0, y0, w, h, dots ? med_lit : med_lit_font, &a);
	}
	cairo_restore(cr);
}

/* Small print cut to fit, with two points where it was cut; from a font
 * where the matrix cannot. Returns its width. */
static double med_line(cairo_t *cr, const char *s, double x, double y, double pitch, int align,
	double max_w, double lit)
{
	char f[160];
	if (!s[0]) {
		return 0.0;
	}
	if (!dots_fold(s, f, sizeof f)) {
		return led_text(cr, s, x, y, pitch * 7.0, align, lit, max_w);
	}
	size_t fit = (size_t)floor((max_w + pitch) / (6.0 * pitch));
	if (strlen(f) > fit && fit > 3) {
		f[fit - 2] = '.';
		f[fit - 1] = '.';
		f[fit] = '\0';
	}
	return dots_text(cr, f, x, y, pitch, align, lit);
}

static void med_clock(char *out, size_t n, double sec)
{
	int t = (int)floor(fmax(sec, 0.0));
	if (t >= 3600) {
		snprintf(out, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
	} else {
		snprintf(out, n, "%d:%02d", t / 60, t % 60);
	}
}

/* One of the marks to press, in a cell MED_BW by MED_RH about (cx, cy):
 * lit when it can be pressed, framed under the hand. */
static void med_button(cairo_t *cr, int which, double cx, double cy, double lit)
{
	const struct desk_media *md = &g_clk.media;
	bool can = md->have && (which == DESK_MEDIA_PREV ? md->can_prev
		: which == DESK_MEDIA_NEXT ? md->can_next
		: md->playing ? md->can_pause : md->can_play);
	bool hot = g_clk.media_hot == which && md->have, down = g_clk.media_down == which;
	if (hot || down) {
		cairo_save(cr);
		desk_rounded_path(cr, cx - MED_BW * 0.5 + 1.0, cy - MED_RH * 0.5, MED_BW - 2.0, MED_RH,
			3.0);
		if (down) {
			led_on(cr, 0.16 * lit);
			cairo_fill_preserve(cr);
		}
		led_on(cr, (down ? 0.75 : 0.45) * lit);
		cairo_set_line_width(cr, 1.1);
		cairo_stroke(cr);
		cairo_restore(cr);
	}
	cairo_new_path(cr);
	double h = 5.0;
	switch (which) {
	case DESK_MEDIA_PREV:
	case DESK_MEDIA_NEXT: {
		/* two arrows against a bar, facing the way it goes */
		double d = which == DESK_MEDIA_NEXT ? 1.0 : -1.0;
		cairo_rectangle(cr, d > 0 ? cx + 5.4 : cx - 7.4, cy - h, 2.0, 2.0 * h);
		for (int k = 0; k < 2; k++) {
			double tip = cx + d * (5.4 - k * 6.0), back = tip - d * 6.0;
			cairo_move_to(cr, tip, cy);
			cairo_line_to(cr, back, cy - h);
			cairo_line_to(cr, back, cy + h);
			cairo_close_path(cr);
		}
		break;
	}
	default:
		if (md->playing) {
			cairo_rectangle(cr, cx - 4.6, cy - h, 3.4, 2.0 * h);
			cairo_rectangle(cr, cx + 1.2, cy - h, 3.4, 2.0 * h);
		} else {
			cairo_move_to(cr, cx - 3.8, cy - h - 0.5);
			cairo_line_to(cr, cx + 5.2, cy);
			cairo_line_to(cr, cx - 3.8, cy + h + 0.5);
			cairo_close_path(cr);
		}
		break;
	}
	if (can) {
		led_fill_path(cr, down ? lit : lit * 0.95);
	} else {
		led_ghost(cr);
		cairo_fill(cr);
	}
}

static void clk_page_media(cairo_t *cr, double lit)
{
	double x = CLK_WX, y = CLK_WY;
	const struct desk_media *md = &g_clk.media;
	/* the title */
	char t[160];
	bool dots;
	med_title(t, sizeof t, &dots);
	double next;
	int off = med_scroll(med_title_cols(t, dots), &next);
	med_title_row(cr, t, dots, x + MED_TX, y + MED_TY, off, !md->have, md->have ? lit : lit * 0.8);
	/* the time, right; the artist in what is left of the line */
	double tw = 0.0;
	if (md->have) {
		char a[24] = "", b[24] = "", line[56];
		if (md->stopped) {
			snprintf(line, sizeof line, "STOPPED");
		} else {
			if (md->position >= 0.0) {
				med_clock(a, sizeof a, md->position);
			}
			if (md->length > 0.0) {
				med_clock(b, sizeof b, md->length);
			}
			/* paused, the time blinks: its places stay, unlit */
			if (!md->playing && fmod(g_clk.now, 1.0) >= 0.55) {
				memset(a, ' ', strlen(a));
			}
			snprintf(line, sizeof line, "%s%s%s", a, a[0] && b[0] ? " / " : "", b);
		}
		tw = dots_text(cr, line, x + MED_TX + MED_TW, y + MED_AY, MED_AP, 1, lit);
		med_line(cr, md->artist, x + MED_TX, y + MED_AY, MED_AP, -1, MED_TW - tw - 14.0,
			lit * 0.9);
	}
	/* how far through */
	double frac = md->have && md->length > 0.0 && md->position >= 0.0
		? fmin(md->position / md->length, 1.0) : 0.0;
	clk_blocks(cr, x + MED_TX, y + MED_PY, MED_TW, MED_PH, 56, frac, lit, false);
	/* the marks to press */
	double cy = y + MED_RY + MED_RH * 0.5;
	for (int i = 0; i < 3; i++) {
		med_button(cr, i, x + MED_BX + i * (MED_BW + MED_BGAP) + MED_BW * 0.5, cy, lit);
	}
	/* whose it is, small, between them and the volume */
	if (md->have && md->player[0]) {
		double l = MED_BX + 3.0 * (MED_BW + MED_BGAP) + 6.0, r = MED_SX - 10.0;
		med_line(cr, md->player, x + (l + r) * 0.5, cy - 4.2, 1.2, 0, r - l, lit * 0.6);
	}
	/* and the volume */
	if (g_clk.have_volume) {
		clk_speaker(cr, x + MED_SX, cy - 7.0, 14.0, g_clk.muted, lit);
		clk_blocks(cr, x + MED_VX, cy - 7.0, MED_VW, 14.0, MED_VN,
			g_clk.muted ? 0.0 : g_clk.volume, lit, true);
		char v[8];
		snprintf(v, sizeof v, "%d", (int)lround(g_clk.volume * 100.0));
		dots_text(cr, g_clk.muted ? "MUTE" : v, x + MED_TX + MED_TW, cy - 4.2, 1.2, 1, lit);
	} else {
		dots_text(cr, "NO MIXER", x + MED_TX + MED_TW, cy - 4.2, 1.2, 1, lit * 0.6);
	}
}

/* Watts as the display writes them: 18.2, 186, 1.20k. */
static void clk_watts(char *out, size_t n, double w)
{
	if (w < 0.0) {
		snprintf(out, n, "--.-");
	} else if (w < 100.0) {
		snprintf(out, n, "%.1f", w);
	} else if (w < 1000.0) {
		snprintf(out, n, "%.0f", w);
	} else {
		snprintf(out, n, "%.2f", w / 1000.0);
	}
}

/* A rate as the display writes it: up to three figures, and what of. */
static void clk_rate(char *out, size_t n, const char **unit, double bps)
{
	static const char *const UNIT[] = { "B/S", "KB/S", "MB/S", "GB/S" };
	int u = 0;
	while (bps >= 999.5 && u < 3) {
		bps /= 1000.0;
		u++;
	}
	*unit = UNIT[u];
	if (bps < 0.0) {
		snprintf(out, n, "---");
	} else if (bps < 9.95 && u > 0) {
		snprintf(out, n, "%.1f", bps);
	} else {
		snprintf(out, n, "%.0f", bps);
	}
}

/* A column of the PC page: its name over it, and a figure under that with
 * what it is of after it. */
static void clk_column(cairo_t *cr, double x, double y, const char *label, const char *value,
	const char *unit, double lit)
{
	dots_text(cr, label, x, y + 5.0, 1.15, -1, lit * 0.8);
	double wd = seg7_text(cr, value, x, y + 15.0, 10.0, 20.0, 3.0, lit);
	dots_text(cr, unit, x + wd + 4.0, y + 27.0, 1.2, -1, lit * 0.9);
}

/* One way of the line: an arrow, down or up, and how fast. */
static void clk_net_way(cairo_t *cr, double x, double y, bool in, double bps, double lit)
{
	if (in) {
		cairo_move_to(cr, x, y + 3.0);
		cairo_line_to(cr, x + 6.0, y + 3.0);
		cairo_line_to(cr, x + 3.0, y + 8.5);
	} else {
		cairo_move_to(cr, x, y + 8.5);
		cairo_line_to(cr, x + 6.0, y + 8.5);
		cairo_line_to(cr, x + 3.0, y + 3.0);
	}
	cairo_close_path(cr);
	led_fill_path(cr, lit * 0.9);
	char v[8];
	const char *unit;
	clk_rate(v, sizeof v, &unit, bps);
	double wd = seg7_text(cr, v, x + 10.0, y, 7.0, 11.0, 2.0, lit);
	dots_text(cr, unit, x + 10.0 + wd + 4.0, y + 4.0, 1.0, -1, lit * 0.85);
}

/* The machine, in five columns: the battery (or the wall), what it draws,
 * how busy the processor is, how full the memory is, and the line, down
 * and up. A rate needs two reads; until there are two it is dashes. */
#define PC_LINE 40.0   /* the bars, or a first line of small print */
#define PC_FOOT 50.0   /* the small print under each */
static void clk_page_pc(cairo_t *cr, double lit)
{
	static const double COL[] = { 8.0, 74.0, 146.0, 210.0, 278.0, 348.0 };
	double x = CLK_WX, y = CLK_WY;
	const struct desk_supply *s = &g_clk.supply;
	const struct desk_meters *m = &g_clk.pc;
	char v[16], t[32];
	/* the rules between them */
	for (int i = 1; i < 5; i++) {
		double rx = x + COL[i] - 4.0;
		cairo_rectangle(cr, rx - 0.5, y + 7.0, 1.0, CLK_WH - 14.0);
	}
	led_ghost(cr);
	cairo_fill(cr);

	/* the battery, or the wall */
	double cx = x + COL[0], cw = COL[1] - COL[0] - 8.0;
	if (s->have_battery) {
		snprintf(v, sizeof v, "%3d", (int)lround(s->battery * 100.0));
		clk_column(cr, cx, y, "BATT", v, "%", lit);
		clk_blocks(cr, cx, y + PC_LINE, cw, 5.0, 8, fmin(fmax(s->battery, 0.0), 1.0), lit,
			false);
		char hm[24] = "";
		if (s->hours >= 0.0 && s->hours < 100.0) {
			int mins = (int)lround(s->hours * 60.0);
			snprintf(hm, sizeof hm, " %d:%02d", mins / 60, mins % 60);
		}
		snprintf(t, sizeof t, "%s%s", s->charging ? "CHG" : s->discharging ? "BAT" : s->full
			? "FULL" : "AC", s->charging || s->discharging ? hm : "");
	} else {
		dots_text(cr, "BATT", cx, y + 5.0, 1.15, -1, lit * 0.8);
		seg14_text(cr, "AC", cx, y + 15.0, 11.0, 20.0, 3.0, lit);
		dots_text(cr, "NONE", cx, y + PC_LINE, 1.0, -1, lit * 0.6);
		/* off the wall, what went in the last hour, where it is measured */
		char wh[16];
		clk_watts(wh, sizeof wh, g_clk.hour_wh);
		snprintf(t, sizeof t, "1H %sWH", wh);
		if (g_clk.hour_wh < 0.0) {
			t[0] = '\0';
		}
	}
	dots_text(cr, t, cx, y + PC_FOOT, 1.0, -1, lit * 0.85);

	/* what it draws */
	cx = x + COL[1];
	clk_watts(v, sizeof v, s->draw_w);
	clk_column(cr, cx, y, "POWER", v, s->draw_w >= 1000.0 ? "KW" : "W", lit);
	if (g_clk.avg_w >= 0.0) {
		snprintf(t, sizeof t, "AVG %.1fW", g_clk.avg_w);
		dots_text(cr, t, cx, y + PC_LINE, 1.0, -1, lit * 0.8);
	}
	if (s->draw_w >= 0.0) {
		snprintf(t, sizeof t, "BY %s", s->source);
	} else {
		snprintf(t, sizeof t, "UNMEASURED");
	}
	dots_text(cr, t, cx, y + PC_FOOT, 1.0, -1, lit * 0.7);

	/* the processor */
	cx = x + COL[2];
	cw = COL[3] - COL[2] - 8.0;
	if (g_clk.pc_rates) {
		snprintf(v, sizeof v, "%3d", (int)lround(m->cpu * 100.0));
	} else {
		snprintf(v, sizeof v, "---");
	}
	clk_column(cr, cx, y, "CPU", v, "%", lit);
	clk_blocks(cr, cx, y + PC_LINE, cw, 5.0, 8, g_clk.pc_rates ? m->cpu : 0.0, lit, false);
	if (m->temp_c > -100.0) {
		snprintf(t, sizeof t, "TEMP %dC", (int)lround(m->temp_c));
	} else {
		snprintf(t, sizeof t, "LOAD %.2f", m->load);
	}
	dots_text(cr, t, cx, y + PC_FOOT, 1.0, -1, lit * 0.8);

	/* the memory */
	cx = x + COL[3];
	cw = COL[4] - COL[3] - 8.0;
	snprintf(v, sizeof v, "%3d", (int)lround(m->mem * 100.0));
	clk_column(cr, cx, y, "RAM", v, "%", lit);
	clk_blocks(cr, cx, y + PC_LINE, cw, 5.0, 8, m->mem, lit, false);
	if (m->mem_total_gb > 0.0) {
		int dp = m->mem_total_gb < 100.0 ? 1 : 0;
		snprintf(t, sizeof t, "%.*f/%.*fG", dp, m->mem_gb, dp, m->mem_total_gb);
		dots_text(cr, t, cx, y + PC_FOOT, 1.0, -1, lit * 0.8);
	}

	/* the line: down, up, and what it goes over */
	cx = x + COL[4];
	dots_text(cr, "NET", cx, y + 5.0, 1.15, -1, lit * 0.8);
	bool rates = g_clk.pc_rates && m->net_if[0];
	clk_net_way(cr, cx, y + 15.0, true, rates ? m->net_in : -1.0, lit);
	clk_net_way(cr, cx, y + 29.0, false, rates ? m->net_out : -1.0, lit);
	if (!m->net_if[0]) {
		snprintf(t, sizeof t, "NO LINE");
	} else if (!m->net_up) {
		snprintf(t, sizeof t, "DOWN");
	} else if (m->net_wireless) {
		snprintf(t, sizeof t, "WIFI %d%%", (int)lround(m->net_signal * 100.0));
	} else {
		snprintf(t, sizeof t, "CABLE");
	}
	dots_text(cr, t, cx, y + PC_FOOT, 1.0, -1, lit * 0.8);
}

/* ---- the keys ---- */

static void clk_key(cairo_t *cr, int i)
{
	double kx = clk_key_x(i), ky = clk_key_y(i), kw = CLK_KW, kh = CLK_KH;
	bool down = g_clk.pressed == i;
	bool active = g_clk.mode == desk_clock_key_mode(i);
	double glow = fmin(fmax(g_clk.glow[i], 0.0), 1.0);
	double wake = fmin(fmax(g_clk.wake, 0.0), 1.0);
	/* the one under the hand stands up to meet it; held, it goes down */
	double dy = down ? 1.4 : -0.9 * glow;
	/* the hole it stands in, and its shadow */
	cairo_save(cr);
	desk_rounded_path(cr, kx - 1.5, ky - 1.0, kw + 3.0, kh + 3.0, 4.0);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
	cairo_fill(cr);
	/* light spilling out round it as a hand comes near */
	if (wake > 0.01) {
		cairo_pattern_t *sp = cairo_pattern_create_radial(kx + kw * 0.5, ky + kh * 0.5, kh * 0.4,
			kx + kw * 0.5, ky + kh * 0.5, kw * 0.62);
		cairo_pattern_add_color_stop_rgba(sp, 0.0, 1.0, 0.60, 0.28, 0.20 * wake + 0.30 * glow);
		cairo_pattern_add_color_stop_rgba(sp, 1.0, 1.0, 0.62, 0.30, 0.0);
		cairo_set_source(cr, sp);
		cairo_arc(cr, kx + kw * 0.5, ky + kh * 0.5, kw * 0.62, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_pattern_destroy(sp);
	}
	/* the cap: dark, a little rounded, lit along its top edge */
	desk_rounded_path(cr, kx, ky + dy, kw, kh - 1.0, 3.5);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, ky + dy, 0, ky + dy + kh);
	double lift = down ? 0.0 : 0.035 * glow;
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.25 + lift, 0.24 + lift, 0.23 + lift);
	cairo_pattern_add_color_stop_rgb(g, 0.45, 0.16 + lift, 0.155 + lift, 0.15 + lift);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.10, 0.095, 0.09);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.6);
	cairo_stroke(cr);
	if (!down) {
		cairo_move_to(cr, kx + 4.0, ky + 1.0);
		cairo_line_to(cr, kx + kw - 4.0, ky + 1.0);
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.16 + 0.10 * glow);
		cairo_stroke(cr);
	}
	/* the lamp in it: lit when its page is up */
	double lx = kx + 8.0, ly = ky + dy + (kh - 1.0) * 0.5;
	cairo_arc(cr, lx, ly, 2.4, 0, 2 * M_PI);
	if (active) {
		cairo_pattern_t *lg = cairo_pattern_create_radial(lx, ly, 0.5, lx, ly, 6.0);
		cairo_pattern_add_color_stop_rgba(lg, 0.0, 1.0, 0.35, 0.18, 0.9);
		cairo_pattern_add_color_stop_rgba(lg, 1.0, 1.0, 0.25, 0.12, 0.0);
		cairo_set_source(cr, lg);
		cairo_paint_with_alpha(cr, 0.6);
		cairo_pattern_destroy(lg);
		cairo_arc(cr, lx, ly, 2.4, 0, 2 * M_PI);
		cairo_set_source_rgb(cr, 1.0, 0.36, 0.20);
	} else {
		cairo_set_source_rgba(cr, 0.28, 0.06, 0.04, 0.9);
	}
	cairo_fill(cr);
	/* and what it does, printed on it -- lit from behind as a hand nears:
	 * grey print on a dark key at rest, amber light through it awake */
	double ink = 0.50 + 0.28 * wake + 0.22 * glow;
	double warm = fmin(wake * 0.75 + glow * 0.45, 1.0);
	const char *label = TR(CLK_KEY_NAME[i]);
	desk_font_select(cr, DESK_FONT_ENGRAVE, label, true, false);
	cairo_set_font_size(cr, 9.0);
	cairo_text_extents_t ext;
	/* letter spaced, as a key is printed */
	double spacing = 0.5, tw = 0.0;
	char one[8];
	/* a whole letter at a time: a Japanese one is three bytes */
	for (const char *p = label; *p;) {
		p += cal_letter(p, one);
		cairo_text_extents(cr, one, &ext);
		tw += ext.x_advance + (*p ? spacing : 0.0);
	}
	double tx = kx + 12.0 + (kw - 14.0 - tw) * 0.5, ty = ky + dy + (kh - 1.0) * 0.5 + 3.3;
	if (warm > 0.02) {
		double gx = tx;
		for (const char *p = label; *p;) {
			p += cal_letter(p, one);
			cairo_move_to(cr, gx, ty);
			cairo_text_path(cr, one);
			cairo_text_extents(cr, one, &ext);
			gx += ext.x_advance + spacing;
		}
		cairo_set_source_rgba(cr, 1.0, 0.62, 0.30, 0.42 * warm);
		cairo_set_line_width(cr, 2.8);
		cairo_stroke(cr);
	}
	double gx = tx;
	for (const char *p = label; *p;) {
		p += cal_letter(p, one);
		cairo_move_to(cr, gx, ty);
		cairo_text_path(cr, one);
		cairo_text_extents(cr, one, &ext);
		gx += ext.x_advance + spacing;
	}
	cairo_set_source_rgba(cr, 0.84 + 0.16 * warm, 0.83 - 0.07 * warm, 0.80 - 0.34 * warm, ink);
	cairo_fill(cr);
	cairo_restore(cr);
}

/* The room being made at login: what of it, a row of blocks filling, and
 * how far, large -- lit in the dark before the lamp is. */
static const char *const CLK_BOOT_WORD[6] = { "DESK", "SHELF", "PANEL", "POST", "LAMP",
	"READY" };

static void clk_page_boot(cairo_t *cr, double lit)
{
	double x = CLK_WX, y = CLK_WY;
	double p = clampf(g_clk.boot, 0.0, 1.0);
	int st = g_clk.boot_stage < 0 ? 0 : (g_clk.boot_stage > 5 ? 5 : g_clk.boot_stage);
	seg14_text(cr, CLK_BOOT_WORD[st], x + 14.0, y + 8.0, 12.0, 20.0, 3.0, lit);
	clk_blocks(cr, x + 14.0, y + 39.0, 180.0, 13.0, 24, p, lit, false);
	char pct[8];
	snprintf(pct, sizeof pct, "%3d", (int)floor(p * 100.0 + 1e-6));
	double wd = seg7_text(cr, pct, x + 212.0, y + (CLK_WH - 44.0) * 0.5, 23.0, 44.0, 7.0, lit);
	dots_text(cr, "%", x + 212.0 + wd + 6.0, y + 41.0, 2.0, -1, lit);
}

/* A day kept: what it is, running along the glass as a sign does, and
 * under it a row of blocks doing the rest of the show -- a metronome's
 * arm swinging to the birthday tune, stars twinkling at Christmas, a burst
 * spreading at each firework. */
static void clk_page_show(cairo_t *cr, double lit)
{
	double x = CLK_WX, y = CLK_WY, t = g_clk.show_t;
	char words[48];
	switch (g_clk.show) {
	case DESK_SHOW_BIRTHDAY:
		snprintf(words, sizeof words, "HAPPY BIRTHDAY");
		break;
	case DESK_SHOW_XMAS:
		snprintf(words, sizeof words, "MERRY CHRISTMAS");
		break;
	default:
		snprintf(words, sizeof words, "HAPPY NEW YEAR %d", g_clk.year);
		break;
	}
	const double cw = 18.0, ch = 30.0, gap = 5.5;
	double tw = strlen(words) * (cw + gap) - gap;
	/* in from the right, across and out to the left, and round again */
	double run = CLK_WW + tw + 40.0;
	double tx = x + CLK_WW - fmod(t * 118.0, run);
	seg14_text(cr, words, tx, y + 6.0, cw, ch, gap, lit);
	if (tx + tw + 40.0 < x + CLK_WW) {
		seg14_text(cr, words, tx + run, y + 6.0, cw, ch, gap, lit);
	}
	/* the row of blocks */
	enum { N = 25 };
	double bx = x + 12.0, by = y + 45.0, bw = (CLK_WW - 24.0) / N, bh = 9.0;
	double v[N];
	for (int i = 0; i < N; i++) {
		double c = (i + 0.5) / N;
		switch (g_clk.show) {
		case DESK_SHOW_BIRTHDAY: {
			/* the arm, from side to side a beat at a time, and its trail */
			double arm = 0.5 + 0.48 * cos(M_PI * t / DESK_SHOW_BEAT);
			double d = fabs(c - arm) * N;
			v[i] = clampf(1.0 - d / 1.6, 0.0, 1.0);
			break;
		}
		case DESK_SHOW_XMAS: {
			double ph = hash21(i * 1.7, 3.1) * 6.283, sp = 1.3 + 2.1 * hash21(i * 2.3, 7.7);
			double tw2 = 0.5 + 0.5 * sin(t * sp + ph);
			v[i] = tw2 * tw2 * tw2;
			break;
		}
		default: {
			/* a burst opening out from the middle, every couple of seconds */
			double age = fmod(t, 2.2);
			double r = age * 0.42, d = fabs(c - 0.5);
			v[i] = clampf(1.0 - fabs(d - r) * 9.0, 0.0, 1.0) * exp(-age * 1.2);
			break;
		}
		}
	}
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < N; i++) {
			if ((v[i] > 0.05) != (pass == 1)) {
				continue;
			}
			cairo_rectangle(cr, bx + i * bw + bw * 0.14, by, bw * 0.72, bh);
			if (pass == 0) {
				led_ghost(cr);
				cairo_fill(cr);
			} else {
				led_fill_path(cr, lit * v[i]);
			}
		}
	}
}

/* Print's camera recording, in place of the time, as a recorder's own
 * display shows it: the red dot that says it is running, the tape's mark,
 * how long it has run in the time's own big figures; and the time, small,
 * at the right. */
static void clk_page_rec(cairo_t *cr, const struct tm *tmv, double lit)
{
	double x = CLK_WX, y = CLK_WY, dh = 44.0, ty = y + (CLK_WH - dh) * 0.5;
	finder_rec_dot(cr, x + 20.0, y + CLK_WH * 0.5, 8.0);
	cairo_save(cr);
	cairo_translate(cr, x + 52.0, y + CLK_WH * 0.5);
	cairo_scale(cr, 30.0, 30.0);
	finder_tape_path(cr);
	cairo_restore(cr);
	led_stroke_path(cr, 1.8, lit);
	cairo_save(cr);
	cairo_translate(cr, x + 84.0, ty);
	cairo_scale(cr, dh / FD_H, dh / FD_H);
	finder_time(cr, g_clk.rec, 0.0, 0.0, false);
	cairo_restore(cr);
	char hhmm[8];
	int hour = tmv->tm_hour;
	if (g_clock_12h) {
		hour = hour % 12 ? hour % 12 : 12;
	}
	snprintf(hhmm, sizeof hhmm, "%2d%02d", hour, tmv->tm_min);
	double sh = 18.0, sw = 9.5, sx = x + CLK_WW - 12.0 - 4.0 * (sw + 3.0) - 6.0;
	for (int i = 0; i < 4; i++) {
		struct seg_box sb = { sx + i * (sw + 3.0) + (i >= 2 ? 6.0 : 0.0), ty + dh - sh, sw, sh,
			sh * 0.10 };
		seg7_digit(cr, &sb, hhmm[i] == ' ' ? -1 : hhmm[i] - '0', lit * 0.8);
	}
	for (int k = 0; k < 2; k++) {
		cairo_arc(cr, sx + 2.0 * (sw + 3.0) + 1.5, ty + dh - sh * (k ? 0.30 : 0.70), 1.2, 0,
			2.0 * M_PI);
		led_fill_path(cr, lit * 0.8);
	}
}

static void gadget_clock(cairo_t *cr, double w, double h, time_t now)
{
	struct tm tmv;
	localtime_r(&now, &tmv);
	cairo_save(cr);
	cairo_scale(cr, w / CLK_W, h / CLK_H);

	/* the window: smoked glass over the dark, the segments behind it */
	double x = CLK_WX, y = CLK_WY;
	cairo_save(cr);
	desk_rounded_path(cr, x - 2.0, y - 2.0, CLK_WW + 4.0, CLK_WH + 4.0, 5.0);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.7);
	cairo_fill(cr);
	desk_rounded_path(cr, x, y, CLK_WW, CLK_WH, 4.0);
	cairo_set_source_rgb(cr, 0.055, 0.018, 0.016);
	cairo_fill_preserve(cr);
	cairo_clip(cr);
	double lit = 1.0;
	switch (g_clk.boot >= 0.0 ? -1 : g_clk.show ? -2 : g_clk.mode) {
	case -1:
		clk_page_boot(cr, lit);
		break;
	case -2:
		clk_page_show(cr, lit);
		break;
	case DESK_CLOCK_DAY:
		clk_page_day(cr, &tmv, lit);
		break;
	case DESK_CLOCK_MEDIA:
		clk_page_media(cr, lit);
		break;
	case DESK_CLOCK_PC:
		clk_page_pc(cr, lit);
		break;
	default:
		if (g_clk.rec >= 0) {
			clk_page_rec(cr, &tmv, lit);
		} else {
			clk_page_time(cr, &tmv, lit);
		}
		break;
	}
	cairo_restore(cr);
	draw_gloss_bar(cr, x, y, CLK_WW, CLK_WH, 4.0);

	for (int i = 0; i < DESK_CLOCK_KEYS; i++) {
		clk_key(cr, i);
	}
	cairo_restore(cr);
}

uint64_t desk_clock_key(time_t now)
{
	uint64_t h = 14695981039346656037ull;
#define CLK_MIX(p, n) do { const unsigned char *c_ = (const unsigned char *)(p); \
	for (size_t k_ = 0; k_ < (n); k_++) { h = (h ^ c_[k_]) * 1099511628211ull; } } while (0)
	struct tm tmv;
	localtime_r(&now, &tmv);
	/* the date changes at midnight (the DAY page, and the TIME page below);
	 * the rest of the time only the TIME page shows, so the other pages are
	 * not drawn again every second */
	int t[2] = { tmv.tm_year, tmv.tm_yday };
	CLK_MIX(t, sizeof t);
	CLK_MIX(&g_clk.mode, sizeof g_clk.mode);
	CLK_MIX(&g_clock_12h, sizeof g_clock_12h);
	int boot[2] = { g_clk.boot >= 0.0 ? (int)floor(g_clk.boot * 100.0 + 1e-6) : -1,
		g_clk.boot_stage };
	CLK_MIX(boot, sizeof boot);
	int show[3] = { g_clk.show, g_clk.show ? (int)floor(g_clk.show_t * 30.0) : 0, g_clk.year };
	CLK_MIX(show, sizeof show);
	int q[DESK_CLOCK_KEYS + 2];
	for (int i = 0; i < DESK_CLOCK_KEYS; i++) {
		q[i] = (int)lround(g_clk.glow[i] * 40.0);
	}
	q[DESK_CLOCK_KEYS] = (int)lround(g_clk.wake * 40.0);
	q[DESK_CLOCK_KEYS + 1] = g_clk.pressed;
	CLK_MIX(q, sizeof q);
	int v[3] = { g_clk.have_volume, (int)lround(g_clk.volume * 100.0), g_clk.muted };
	CLK_MIX(v, sizeof v);
	CLK_MIX(g_clk.ime, strlen(g_clk.ime));
	if (g_clk.mode == DESK_CLOCK_TIME) {
		/* the hour and minute, the seconds (the colon's beat, a flat
		 * battery's blink), and the camera and microphone lamps */
		/* in 2D no seconds: drawn again once a minute, not every second */
		int sec = desk_gfx_level() == DESK_GFX_2D ? 0 : tmv.tm_sec;
		int hms[5] = { tmv.tm_hour * 60 + tmv.tm_min, sec, g_clk.camera, g_clk.mic, g_clk.rec };
		CLK_MIX(hms, sizeof hms);
		const struct desk_supply *s = &g_clk.supply;
		int b[5] = { s->have_battery, (int)lround(s->battery * 100.0), s->charging,
			s->have_backlight, (int)lround(s->backlight * 100.0) };
		CLK_MIX(b, sizeof b);
	}
	if (g_clk.mode == DESK_CLOCK_MEDIA) {
		const struct desk_media *md = &g_clk.media;
		char title[160];
		bool dots;
		med_title(title, sizeof title, &dots);
		double next;
		int m[12] = { md->have, md->playing, md->stopped,
			md->position >= 0.0 ? (int)floor(md->position) : -1,
			md->length > 0.0 ? (int)floor(md->length) : -1,
			md->can_prev | md->can_next << 1 | md->can_play << 2 | md->can_pause << 3,
			g_clk.media_hot, g_clk.media_down,
			med_scroll(med_title_cols(title, dots), &next),
			!md->playing && fmod(g_clk.now, 1.0) >= 0.55, dots, 0 };
		CLK_MIX(m, sizeof m);
		CLK_MIX(title, strlen(title));
		CLK_MIX(md->artist, strlen(md->artist));
		CLK_MIX(md->player, strlen(md->player));
	}
	if (g_clk.mode == DESK_CLOCK_PC) {
		const struct desk_supply *s = &g_clk.supply;
		int p[9] = { s->have_battery, (int)lround(s->battery * 1000.0), s->charging,
			s->discharging, s->full, (int)lround(s->hours * 60.0),
			(int)lround(s->draw_w * 10.0), (int)lround(g_clk.avg_w * 10.0),
			(int)lround(g_clk.hour_wh * 10.0) };
		CLK_MIX(p, sizeof p);
		CLK_MIX(s->source, strlen(s->source));
		/* as written: a rate that moves in its fourth figure draws nothing */
		const struct desk_meters *m = &g_clk.pc;
		char t[96];
		snprintf(t, sizeof t, "%d|%.0f|%.0f|%.1f|%.1f|%.0f|%.2f|%s|%d|%d|%.0f",
			g_clk.pc_rates, m->cpu * 100.0, m->mem * 100.0, m->mem_gb, m->mem_total_gb,
			m->temp_c, m->load, m->net_if, m->net_up, m->net_wireless, m->net_signal * 100.0);
		CLK_MIX(t, strlen(t));
		for (int k = 0; k < 2; k++) {
			const char *unit;
			clk_rate(t, sizeof t, &unit, k ? m->net_out : m->net_in);
			CLK_MIX(t, strlen(t));
			CLK_MIX(unit, strlen(unit));
		}
	}
#undef CLK_MIX
	return h | 1u;
}

int desk_clock_next_ms(void)
{
	if (g_clk.show) {
		return 33;   /* the sign running along, the blocks */
	}
	if (g_clk.mode != DESK_CLOCK_MEDIA || !g_clk.media.have) {
		return -1;
	}
	const struct desk_media *md = &g_clk.media;
	double soon = -1.0;
	/* a title running along */
	char t[160];
	bool dots;
	med_title(t, sizeof t, &dots);
	med_scroll(med_title_cols(t, dots), &soon);
	/* the next second of the track's time, or the blink while paused */
	double tick = -1.0;
	if (md->playing && md->position >= 0.0) {
		tick = 1.0 - fmod(md->position, 1.0);
	} else if (!md->stopped && md->position >= 0.0) {
		double ph = fmod(g_clk.now, 1.0);
		tick = ph < 0.55 ? 0.55 - ph : 1.0 - ph;
	}
	if (tick >= 0.0 && (soon < 0.0 || tick < soon)) {
		soon = tick;
	}
	return soon < 0.0 ? -1 : (int)ceil(soon * 1000.0) + 2;
}
