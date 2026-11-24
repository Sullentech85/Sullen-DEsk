/* Part of draw.c: flat things: procedural tiles and the surfaces things are printed on.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ================================================================
 * Flat things: window chrome, the surfaces the gadget faces are printed
 * on, and the paper that folds. Everything here is cairo, because it is
 * either text or it has to be redrawn every frame.
 * ================================================================ */

static void desk_rounded_path(cairo_t *cr, double x, double y, double w, double h, double r)
{
	if (r > w * 0.5) {
		r = w * 0.5;
	}
	if (r > h * 0.5) {
		r = h * 0.5;
	}
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
	cairo_close_path(cr);
}

static void desk_rounded(cairo_t *cr, double x, double y, double w, double h, double r)
{
	desk_rounded_path(cr, x, y, w, h, r);
}

/* align: -1 left, 0 centred on x, 1 right; set as role has it (fonts.h) */
/* s as cairo will take it. A byte that is not part of a whole UTF-8
 * character is left out: cairo refuses such a string, and with it
 * everything drawn after it on the same cairo_t -- a tray item's tooltip
 * cut short in the middle of a character once left its whole menu card
 * blank. A string that is fine is handed back as it is. */
static const char *utf8_clean(const char *s, char *buf, size_t n)
{
	if (!s) {
		return "";
	}
	const unsigned char *p = (const unsigned char *)s;
	size_t o = 0;
	bool bad = false;
	while (*p) {
		int len = *p < 0x80 ? 1 : (*p >= 0xc2 && *p <= 0xdf) ? 2 : (*p >= 0xe0 && *p <= 0xef) ? 3
			: (*p >= 0xf0 && *p <= 0xf4) ? 4 : 0;
		bool ok = len > 0;
		for (int k = 1; ok && k < len; k++) {
			ok = (p[k] & 0xc0) == 0x80;
		}
		if (ok && len == 3) {
			ok = !(p[0] == 0xe0 && p[1] < 0xa0) && !(p[0] == 0xed && p[1] > 0x9f);
		} else if (ok && len == 4) {
			ok = !(p[0] == 0xf0 && p[1] < 0x90) && !(p[0] == 0xf4 && p[1] > 0x8f);
		}
		if (!ok) {
			bad = true;
			p++;
			continue;
		}
		if (o + (size_t)len < n) {
			memcpy(buf + o, p, (size_t)len);
			o += (size_t)len;
		}
		p += len;
	}
	if (!bad) {
		return s;
	}
	buf[o < n ? o : n - 1] = '\0';
	return buf;
}

static void show_text_as(cairo_t *cr, enum desk_font role, const char *s, double x, double y,
	double size, bool bold, double r, double g, double b, double a, int align)
{
	char clean[1024];
	s = utf8_clean(s, clean, sizeof clean);
	cairo_save(cr);
	desk_font_select(cr, role, s, bold, false);
	cairo_set_font_size(cr, size);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, s, &ext);
	double tx = x;
	if (align == 0) {
		tx = x - ext.width * 0.5 - ext.x_bearing;
	} else if (align > 0) {
		tx = x - ext.width - ext.x_bearing;
	}
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_move_to(cr, tx, y);
	cairo_show_text(cr, s);
	cairo_restore(cr);
}

static void show_text(cairo_t *cr, const char *s, double x, double y, double size,
	bool bold, double r, double g, double b, double a, int align)
{
	show_text_as(cr, DESK_FONT_UI, s, x, y, size, bold, r, g, b, a, align);
}

/* The same, shrunk until it fits. A catalogue sheet is set to the width of
 * its paper: the compositor it was named after had no such discipline, and
 * "GNU Image Manipulation Program" ran off the edge. */
static void show_text_fit_to(cairo_t *cr, enum desk_font role, const char *s, double x,
	double y, double size, bool bold, double r, double g, double b, double a, int align,
	double max_w, double least)
{
	char clean[1024];
	s = utf8_clean(s, clean, sizeof clean);
	cairo_save(cr);
	desk_font_select(cr, role, s, bold, false);
	double use = size;
	for (int i = 0; i < 24; i++) {
		cairo_set_font_size(cr, use);
		cairo_text_extents_t ext;
		cairo_text_extents(cr, s, &ext);
		if (ext.width <= max_w || use < size * least) {
			break;
		}
		use *= 0.94;
	}
	cairo_restore(cr);
	show_text_as(cr, role, s, x, y, use, bold, r, g, b, a, align);
}

/* (no smaller than about half its size: what does not fit then runs over) */
static void show_text_fit_as(cairo_t *cr, enum desk_font role, const char *s, double x,
	double y, double size, bool bold, double r, double g, double b, double a, int align,
	double max_w)
{
	show_text_fit_to(cr, role, s, x, y, size, bold, r, g, b, a, align, max_w, 0.55);
}

static void show_text_fit(cairo_t *cr, const char *s, double x, double y, double size,
	bool bold, double r, double g, double b, double a, int align, double max_w)
{
	show_text_fit_as(cr, DESK_FONT_UI, s, x, y, size, bold, r, g, b, a, align, max_w);
}

/* The clock's hours, 0..23 or 1..12 with AM and PM (Shell Settings). */
static bool g_clock_12h;

void desk_set_clock_12h(bool on)
{
	g_clock_12h = on;
}

/* A time of day as the desk prints it: 14:05, or 2:05 PM. */
static void fmt_hm(char *out, size_t n, const struct tm *t)
{
	if (g_clock_12h) {
		int h = t->tm_hour % 12;
		snprintf(out, n, "%d:%02d %s", h ? h : 12, t->tm_min, t->tm_hour < 12 ? "AM" : "PM");
	} else {
		snprintf(out, n, "%02d:%02d", t->tm_hour, t->tm_min);
	}
}

/* ---- the lamp, in screen coordinates ---------------------------
 *
 * There is one lamp over the desk, the room's (ROOM_LAMP_POS): it stands
 * at the near right, well up. What is drawn over the traced room -- the
 * paper and its folds, creases and curled corners, and the shadows under
 * all of it -- is lit by that same lamp, or it looks pasted on: so the
 * lamp is kept here as the point of the desk under it on the screen and
 * its height over that in pixels. A thing off the desk throws its shadow
 * away from that point, the longer the farther from it it is, and turns
 * its lit side towards it. (It was a lamp off the top of the screen, as a
 * flat desktop has its light, and every shadow fell down the screen while
 * the room's fell up it.)
 *
 * The bookcase, the console and the post box are seen from in front, lit
 * from above: each sets that lamp while it paints and puts the desk's
 * back after. */
struct lamp_px {
	double x, y;          /* the point under it, on the screen */
	double h;             /* how high it is over that */
	double kmin, kmax;    /* the shortest and longest shadow, against the height */
};
static struct lamp_px g_desk_lamp = { 1080.0, 733.0, 660.0, 0.10, 1.40 };
static struct lamp_px g_light = { 1080.0, 733.0, 660.0, 0.10, 1.40 };
static double g_paint_ox, g_paint_oy;

static struct lamp_px lamp_view(int screen_w, int screen_h)
{
	(void)screen_w;
	struct lamp_px was = g_light;
	/* above the middle of the view and in front of it */
	g_light = (struct lamp_px){ screen_w * 0.5, -screen_h * 0.34, 1100.0, 0.18, 1.15 };
	return was;
}

/* Toward the lamp from (sx, sy) on the screen: x right, y down the screen,
 * z up off the desk; a unit vector. */
static void lamp_toward(double sx, double sy, double *lx, double *ly, double *lz)
{
	double dx = g_light.x - (sx + g_paint_ox), dy = g_light.y - (sy + g_paint_oy);
	double dz = g_light.h;
	double n = sqrt(dx * dx + dy * dy + dz * dz);
	*lx = dx / n;
	*ly = dy / n;
	*lz = dz / n;
}

/* The same, from (x, y) in cr's terms and in them: the lamp is fixed to
 * the screen, and a sheet being painted may be turned. */
static void lamp_toward_user(cairo_t *cr, double x, double y, double *lx, double *ly,
	double *lz)
{
	double dx = x, dy = y;
	cairo_user_to_device(cr, &dx, &dy);
	double ax, ay, az;
	lamp_toward(dx, dy, &ax, &ay, &az);
	/* the part along the desk, turned into cr's terms; the part up stays */
	double hx = ax, hy = ay;
	cairo_device_to_user_distance(cr, &hx, &hy);
	double hn = sqrt(hx * hx + hy * hy), an = sqrt(ax * ax + ay * ay);
	if (hn > 1e-9) {
		hx *= an / hn;
		hy *= an / hn;
	}
	*lx = hx;
	*ly = hy;
	*lz = az;
}

void desk_set_paint_origin(double x, double y)
{
	g_paint_ox = x;
	g_paint_oy = y;
}

/* Which way a shadow leans, for something at (sx, sy) on screen standing
 * `lift` above the desk: away from the point under the lamp, hardly at all
 * right under it, longer the farther off it is -- and so, across the desk,
 * fanning out from it. */
static void shadow_dir(double sx, double sy, double lift, double *ox, double *oy)
{
	double x = sx + g_paint_ox, y = sy + g_paint_oy;
	double dx = x - g_light.x, dy = y - g_light.y;
	double len = sqrt(dx * dx + dy * dy);
	if (len < 1e-6) {
		*ox = 0.0;
		*oy = 0.0;
		return;
	}
	double k = lift * clampf(len / g_light.h, g_light.kmin, g_light.kmax);
	*ox = dx / len * k;
	*oy = dy / len * k;
}

/* The same, for a caller that is painting through a transform: the lamp is
 * fixed to the screen, so the point has to be taken out to device space and
 * the answer brought back. */
static void shadow_offset(cairo_t *cr, double x, double y, double lift,
	double *ox, double *oy)
{
	double dx = x, dy = y;
	cairo_user_to_device(cr, &dx, &dy);
	double sx, sy;
	shadow_dir(dx, dy, lift, &sx, &sy);
	cairo_device_to_user_distance(cr, &sx, &sy);
	*ox = sx;
	*oy = sy;
}

void desk_shadow_offset(double screen_x, double screen_y, double lift,
	double *ox, double *oy)
{
	shadow_dir(screen_x, screen_y, lift, ox, oy);
}

int desk_shadow_sprite_pad(void)
{
	return DESK_SHADOW;
}

void desk_draw_shadow_sprite(cairo_t *cr, double w, double h, double r,
	double side, double amount)
{
	if (!gfx_shadows()) {
		return;
	}
	int pad = DESK_SHADOW;
	double lift = clampf(amount, 0.0, 1.0);
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	/* three stacked blurs, widest and faintest first */
	const double spread[3] = { 1.00, 0.52, 0.22 };
	const double alpha[3] = { 0.13, 0.17, 0.26 };
	for (int i = 0; i < 3; i++) {
		double s = pad * spread[i];
		int steps = 14;
		for (int k = steps; k >= 1; k--) {
			double t = (double)k / steps;
			double grow = s * t;
			/* The corner that is up has air under it, so its shadow runs
			 * out further and softer; the corners still on the desk are
			 * touching it, and a shadow at a contact is a hard thin line.
			 * This is the whole difference between a sheet of paper and a
			 * rectangle with a blur behind it. */
			double gl = grow * (1.0 + lift * (side < 0.0 ? 0.85 : -0.42));
			double gr = grow * (1.0 + lift * (side > 0.0 ? 0.85 : -0.42));
			double gt = grow * (1.0 - lift * 0.50);
			double gb = grow * (1.0 + lift * 0.70);
			cairo_set_source_rgba(cr, 0.06, 0.04, 0.03,
				alpha[i] * (1.0 - t) * (1.0 - t) / steps * 4.0);
			desk_rounded(cr, pad - gl, pad - gt,
				w + gl + gr, h + gt + gb, r + grow);
			cairo_fill(cr);
		}
	}
	cairo_restore(cr);
}

/* ---- seamless procedural tiles --------------------------------- */

/* Periodic value noise, so a tile can repeat without a visible grid. */
static double noise2p(double x, double y, double px, double py)
{
	double xi = floor(x), yi = floor(y);
	double xf = x - xi, yf = y - yi;
	double u = xf * xf * (3.0 - 2.0 * xf);
	double v = yf * yf * (3.0 - 2.0 * yf);
	double x0 = fmod(fmod(xi, px) + px, px), x1 = fmod(x0 + 1.0, px);
	double y0 = fmod(fmod(yi, py) + py, py), y1 = fmod(y0 + 1.0, py);
	double a = hash21(x0, y0);
	double b = hash21(x1, y0);
	double c = hash21(x0, y1);
	double d = hash21(x1, y1);
	double top = a + (b - a) * u;
	double bot = c + (d - c) * u;
	return top + (bot - top) * v;
}

static double fbm_p(double x, double y, double px, double py)
{
	double sum = 0.0, amp = 0.5, f = 1.0;
	for (int i = 0; i < 5; i++) {
		sum += amp * noise2p(x * f, y * f, px * f, py * f);
		amp *= 0.5;
		f *= 2.0;
	}
	return sum;
}

static uint32_t pack_rgb(double r, double g, double b)
{
	uint32_t R = (uint32_t)(clampf(r, 0, 1) * 255.0 + 0.5);
	uint32_t G = (uint32_t)(clampf(g, 0, 1) * 255.0 + 0.5);
	uint32_t B = (uint32_t)(clampf(b, 0, 1) * 255.0 + 0.5);
	return 0xff000000u | (R << 16) | (G << 8) | B;
}

/* Walnut for the flat path, in tile coordinates. Same idea as the traced
 * material: rings around a pith, cathedrals where the cut crossed them. */
static void wood_at(double u, double v, double pu, double pv, double *rr, double *gg, double *bb)
{
	double plank = pu / 3.0;
	double board = floor(u / plank);
	double bu = u - board * plank;
	double jit = hash21(board * 7.3, 11.1);
	double pith = plank * (0.24 + jit * 0.52);
	double wob = fbm_p(v * 0.09 + board * 3.7, 4.2, pv, pv) - 0.5;
	double r = fabs(bu - pith + wob * plank * 0.30);
	double ring = r * (7.2 + jit * 2.4) + fbm_p(v * 0.16 + board * 2.1, r * 0.8, pv, pv) * 3.4;
	double g = pow(0.5 + 0.5 * sin(ring * 2.0 * M_PI), 1.9);

	double lr = 0.238, lg = 0.138, lb = 0.076;
	double dr = 0.088, dg = 0.044, db = 0.024;
	double cr = lr + (dr - lr) * g;
	double cg = lg + (dg - lg) * g;
	double cb = lb + (db - lb) * g;

	double fleck = pow(noise2p(u * 46.0, v * 7.0, pu * 46.0, pv * 7.0), 7.0);
	cr += 0.14 * fleck;
	cg += 0.11 * fleck;
	cb += 0.07 * fleck;
	double pore = pow(noise2p(u * 130.0, v * 6.5, pu * 130.0, pv * 6.5), 4.0);
	cr *= 1.0 - pore * 0.30;
	cg *= 1.0 - pore * 0.30;
	cb *= 1.0 - pore * 0.30;
	double seam = fabs(bu - plank * 0.5);
	seam = smoothstepf(plank * 0.5, plank * 0.5 - 0.055, seam);
	cr *= 1.0 - seam * 0.55;
	cg *= 1.0 - seam * 0.55;
	cb *= 1.0 - seam * 0.55;

	/* a flat tile has no lighting of its own, so lift it into the range the
	 * traced desk sits in */
	*rr = pow(cr, 1.0 / 2.2) * 1.04;
	*gg = pow(cg, 1.0 / 2.2) * 1.04;
	*bb = pow(cb, 1.0 / 2.2) * 1.04;
}

static cairo_surface_t *g_wood_tile;
static pthread_once_t g_wood_tile_once = PTHREAD_ONCE_INIT;

static void wood_tile_make(void)
{
	cairo_surface_t *tile;
	const int n = 384;
	tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, n, n);
	unsigned char *data = cairo_image_surface_get_data(tile);
	int stride = cairo_image_surface_get_stride(tile);
	for (int y = 0; y < n; y++) {
		uint32_t *row = (uint32_t *)(data + y * stride);
		for (int x = 0; x < n; x++) {
			double r, g, b;
			wood_at(x * 24.0 / n, y * 24.0 / n, 24.0, 24.0, &r, &g, &b);
			row[x] = pack_rgb(r, g, b);
		}
	}
	cairo_surface_mark_dirty(tile);
	g_wood_tile = tile;
}

static cairo_surface_t *wood_tile(void)
{
	pthread_once(&g_wood_tile_once, wood_tile_make);
	return g_wood_tile;
}

static cairo_surface_t *g_paper_tile;
static pthread_once_t g_paper_tile_once = PTHREAD_ONCE_INIT;

static void paper_tile_make(void)
{
	cairo_surface_t *tile;
	const int n = 256;
	tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, n, n);
	unsigned char *data = cairo_image_surface_get_data(tile);
	int stride = cairo_image_surface_get_stride(tile);
	for (int y = 0; y < n; y++) {
		uint32_t *row = (uint32_t *)(data + y * stride);
		for (int x = 0; x < n; x++) {
			double u = x * 20.0 / n, v = y * 20.0 / n;
			double fib = fbm_p(u * 5.0, v * 2.0, 100.0, 40.0);
			double sp = pow(noise2p(u * 30.0, v * 30.0, 600.0, 600.0), 6.0);
			double k = 0.965 + fib * 0.055 - sp * 0.10;
			row[x] = pack_rgb(0.947 * k, 0.933 * k, 0.902 * k);
		}
	}
	cairo_surface_mark_dirty(tile);
	g_paper_tile = tile;
}

static cairo_surface_t *paper_tile(void)
{
	pthread_once(&g_paper_tile_once, paper_tile_make);
	return g_paper_tile;
}

/* Film grain, so nothing in the frame is perfectly clean. */
static cairo_surface_t *g_grain_tile;
static pthread_once_t g_grain_tile_once = PTHREAD_ONCE_INIT;

static void grain_tile_make(void)
{
	cairo_surface_t *tile;
	const int n = 128;
	tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, n, n);
	unsigned char *data = cairo_image_surface_get_data(tile);
	int stride = cairo_image_surface_get_stride(tile);
	for (int y = 0; y < n; y++) {
		uint32_t *row = (uint32_t *)(data + y * stride);
		for (int x = 0; x < n; x++) {
			double g = hash21(x * 1.7 + 0.5, y * 3.1 + 0.5);
			uint32_t a = (uint32_t)(fabs(g - 0.5) * 2.0 * 44.0);
			uint32_t c = g > 0.5 ? 255u : 0u;
			row[x] = (a << 24) | ((c * a / 255) << 16) | ((c * a / 255) << 8) | (c * a / 255);
		}
	}
	cairo_surface_mark_dirty(tile);
	g_grain_tile = tile;
}

static cairo_surface_t *grain_tile(void)
{
	pthread_once(&g_grain_tile_once, grain_tile_make);
	return g_grain_tile;
}

/* Value noise on a lattice of px by py cells that wraps, for a tile that
 * repeats without a seam: 0..1 at (x, y), in cells. */
static double wrap_noise(double x, double y, int px, int py, double seed)
{
	double fx = floor(x), fy = floor(y), tx = x - fx, ty = y - fy;
	int ix = ((int)fx % px + px) % px, iy = ((int)fy % py + py) % py;
	int jx = (ix + 1) % px, jy = (iy + 1) % py;
	double a = hash21(ix + seed, iy), b = hash21(jx + seed, iy);
	double c = hash21(ix + seed, jy), d = hash21(jx + seed, jy);
	tx = tx * tx * (3.0 - 2.0 * tx);
	ty = ty * ty * (3.0 - 2.0 * ty);
	return a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty;
}

/* What a tile of light and shade over a colour is, as grain_tile has it:
 * premultiplied white for lighter, black for darker, by l (-1..1). */
static uint32_t tile_shade(double l, double up, double down)
{
	double a = l > 0.0 ? l * up : -l * down;
	uint32_t al = (uint32_t)lround(clampf(a, 0.0, 1.0) * 255.0);
	uint32_t c = l > 0.0 ? al : 0u;
	return al << 24 | c << 16 | c << 8 | c;
}

/* Morocco: leather boarded until its grain stands up in pebbles, a crease
 * round each -- cells, the creases where two are nearly as near, the pebbles
 * lit from the lamp's side. A tile that repeats. */
static cairo_surface_t *g_pebble_tile;
static pthread_once_t g_pebble_tile_once = PTHREAD_ONCE_INIT;

static void pebble_tile_make(void)
{
	const int n = 192, cells = 24;
	double cs = (double)n / cells;
	cairo_surface_t *tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, n, n);
	unsigned char *data = cairo_image_surface_get_data(tile);
	int stride = cairo_image_surface_get_stride(tile);
	for (int y = 0; y < n; y++) {
		uint32_t *row = (uint32_t *)(data + y * stride);
		for (int x = 0; x < n; x++) {
			double px = (x + 0.5) / cs, py = (y + 0.5) / cs;
			int cx = (int)floor(px), cy = (int)floor(py);
			double f1 = 9.0, f2 = 9.0, dx1 = 0.0, dy1 = 0.0;
			for (int j = -1; j <= 1; j++) {
				for (int i = -1; i <= 1; i++) {
					int gx = ((cx + i) % cells + cells) % cells, gy = ((cy + j) % cells + cells) % cells;
					double fx = cx + i + 0.15 + 0.7 * hash21(gx * 1.3, gy * 2.1);
					double fy = cy + j + 0.15 + 0.7 * hash21(gx * 3.7 + 9.0, gy * 1.9);
					double d = hypot(px - fx, py - fy);
					if (d < f1) {
						f2 = f1;
						f1 = d;
						dx1 = px - fx;
						dy1 = py - fy;
					} else if (d < f2) {
						f2 = d;
					}
				}
			}
			/* the crease, and the pebble's top catching the lamp up to the left */
			double crease = 1.0 - clampf((f2 - f1) / 0.22, 0.0, 1.0);
			double lit = f1 > 1e-6 ? -(dx1 + dy1) / (f1 * 1.414) : 0.0;
			double l = -crease * crease * 0.9 + lit * (1.0 - crease) * 0.35 * clampf(f1 / 0.5, 0.0, 1.0);
			row[x] = tile_shade(l, 0.30, 0.42);
		}
	}
	cairo_surface_mark_dirty(tile);
	g_pebble_tile = tile;
}

static cairo_surface_t *pebble_tile(void)
{
	pthread_once(&g_pebble_tile_once, pebble_tile_make);
	return g_pebble_tile;
}

/* Linoleum: linseed and cork pressed with a marbling drawn through it along
 * the roll, and a fine grit. A tile that repeats. */
static cairo_surface_t *g_marble_tile;
static pthread_once_t g_marble_tile_once = PTHREAD_ONCE_INIT;

static void marble_tile_make(void)
{
	const int n = 256;
	cairo_surface_t *tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, n, n);
	unsigned char *data = cairo_image_surface_get_data(tile);
	int stride = cairo_image_surface_get_stride(tile);
	for (int y = 0; y < n; y++) {
		uint32_t *row = (uint32_t *)(data + y * stride);
		for (int x = 0; x < n; x++) {
			double u = (double)x / n, v = (double)y / n;
			/* the marbling: stretched along x, wandering, and lifted where it folds */
			double warp = wrap_noise(u * 8.0, v * 8.0, 8, 8, 3.0) * 1.4
				+ wrap_noise(u * 16.0, v * 16.0, 16, 16, 5.0) * 0.5;
			double m = wrap_noise(u * 6.0 + warp, v * 40.0, 6, 40, 11.0) * 0.6
				+ wrap_noise(u * 12.0 + warp * 2.0, v * 80.0, 12, 80, 17.0) * 0.4;
			double streak = clampf((m - 0.5) * 2.0, -1.0, 1.0);
			/* and the cork in it, a fleck here and there */
			double fleck = hash21(x * 0.37 + 2.0, y * 0.53 + 7.0);
			double grit = hash21(x * 1.13 + 0.5, y * 2.71 + 0.5) - 0.5;
			double l = streak * 0.40 + grit * 0.28 + (fleck > 0.985 ? 0.6 : fleck < 0.012 ? -0.6 : 0.0);
			row[x] = tile_shade(l, 0.22, 0.26);
		}
	}
	cairo_surface_mark_dirty(tile);
	g_marble_tile = tile;
}

static cairo_surface_t *marble_tile(void)
{
	pthread_once(&g_marble_tile_once, marble_tile_make);
	return g_marble_tile;
}

/* The tiles made ahead, on a thread of their own (desk.h). Each is made
 * once, by whichever asks first, and a painter asking while one is being
 * made waits only for the rest of it. */
static void *tiles_warm(void *arg)
{
	(void)arg;
	thread_to_the_back();
	paper_tile();
	grain_tile();
	wood_tile();
	return NULL;
}

void desk_warm_tiles(void)
{
	static bool asked;
	if (asked) {
		return;
	}
	asked = true;
	pthread_t th;
	pthread_attr_t at;
	if (pthread_attr_init(&at) != 0) {
		return;
	}
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&th, &at, tiles_warm, NULL) != 0) {
		/* made when first asked for, then, as they always were */
	}
	pthread_attr_destroy(&at);
}

/* A tile made again at the size it is laid at, through filter: kept, a few
 * of them, for whoever lays the same tile at the same size next. Each holds
 * the tile it was made from, so that no other surface can come to stand at
 * its address while it is here and be taken for it. */
#define TILE_SIZED_MAX 16
static struct {
	cairo_surface_t *tile, *sized;
	int w, h;
	cairo_filter_t filter;
} g_tile_sized[TILE_SIZED_MAX];
static int g_tile_sized_next;
static pthread_mutex_t g_tile_sized_lock = PTHREAD_MUTEX_INITIALIZER;

/* tile at w by h, a reference the caller lets go of */
static cairo_surface_t *tile_sized(cairo_surface_t *tile, int w, int h, cairo_filter_t filter)
{
	pthread_mutex_lock(&g_tile_sized_lock);
	for (int i = 0; i < TILE_SIZED_MAX; i++) {
		if (g_tile_sized[i].tile == tile && g_tile_sized[i].w == w && g_tile_sized[i].h == h
			&& g_tile_sized[i].filter == filter) {
			cairo_surface_t *s = cairo_surface_reference(g_tile_sized[i].sized);
			pthread_mutex_unlock(&g_tile_sized_lock);
			return s;
		}
	}
	pthread_mutex_unlock(&g_tile_sized_lock);
	int tw = cairo_image_surface_get_width(tile), th = cairo_image_surface_get_height(tile);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *c = cairo_create(s);
	cairo_scale(c, (double)w / tw, (double)h / th);
	cairo_pattern_t *pat = cairo_pattern_create_for_surface(tile);
	/* round the edge it wraps over, as it is laid */
	cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
	cairo_pattern_set_filter(pat, filter);
	cairo_set_source(c, pat);
	cairo_set_operator(c, CAIRO_OPERATOR_SOURCE);
	cairo_paint(c);
	cairo_pattern_destroy(pat);
	cairo_destroy(c);
	cairo_surface_flush(s);
	pthread_mutex_lock(&g_tile_sized_lock);
	int k = g_tile_sized_next;
	g_tile_sized_next = (k + 1) % TILE_SIZED_MAX;
	if (g_tile_sized[k].sized) {
		cairo_surface_destroy(g_tile_sized[k].sized);
		cairo_surface_destroy(g_tile_sized[k].tile);
	}
	g_tile_sized[k].tile = cairo_surface_reference(tile);
	g_tile_sized[k].sized = cairo_surface_reference(s);
	g_tile_sized[k].w = w;
	g_tile_sized[k].h = h;
	g_tile_sized[k].filter = filter;
	pthread_mutex_unlock(&g_tile_sized_lock);
	return s;
}

/* A tile to lay across cr, repeated: a pixel of it sx by sy units, taken
 * through filter.
 *
 * At its own pixels the tile is copied; at any other size the good and the
 * best filters look at a square of the tile's pixels for every one laid. A
 * page of paper on a 4K screen -- the overlay is drawn at 2048 and so the
 * page at half its size, as it is on any screen at the levels that draw
 * the overlay smaller -- was 123 ms that way, where at its own size it is
 * a twentieth of one: every letter written in the book, both its pages.
 * Smaller, then, the tile is made again at the size it is laid at, through
 * filter, once, and that laid as near its own pixels as whole pixels come:
 * bilinear, which costs nothing there, and on average comes within two
 * levels in 255 of what the filter gave. Larger it is laid as it was. */
static cairo_pattern_t *tile_pattern(cairo_t *cr, cairo_surface_t *tile, double sx, double sy,
	cairo_filter_t filter)
{
	/* pixels of the target to a pixel of the tile, across and down */
	double ax = sx, ay = 0.0, bx = 0.0, by = sy;
	cairo_user_to_device_distance(cr, &ax, &ay);
	cairo_user_to_device_distance(cr, &bx, &by);
	double kx = hypot(ax, ay), ky = hypot(bx, by);
	double mx = 1.0 / sx, my = 1.0 / sy;
	cairo_surface_t *src = NULL;
	bool own = fabs(kx - 1.0) < 1e-6 && fabs(ky - 1.0) < 1e-6;
	if (!own && kx > 0.0 && ky > 0.0 && kx < 1.0 + 1e-6 && ky < 1.0 + 1e-6) {
		int tw = cairo_image_surface_get_width(tile), th = cairo_image_surface_get_height(tile);
		int w = (int)lround(tw * fmin(kx, 1.0)), h = (int)lround(th * fmin(ky, 1.0));
		w = w < 1 ? 1 : w;
		h = h < 1 ? 1 : h;
		src = tile_sized(tile, w, h, filter);
		mx *= (double)w / tw;
		my *= (double)h / th;
		filter = CAIRO_FILTER_BILINEAR;
	}
	cairo_pattern_t *pat = cairo_pattern_create_for_surface(src ? src : tile);
	if (src) {
		cairo_surface_destroy(src);   /* the pattern holds it */
	}
	cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
	cairo_pattern_set_filter(pat, filter);
	if (mx != 1.0 || my != 1.0) {
		cairo_matrix_t m;
		cairo_matrix_init_scale(&m, mx, my);
		cairo_pattern_set_matrix(pat, &m);
	}
	return pat;
}

static void overlay_grain(cairo_t *cr, double x, double y, double w, double h, double a)
{
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_clip(cr);
	cairo_pattern_t *pat = tile_pattern(cr, grain_tile(), 1.0, 1.0, CAIRO_FILTER_GOOD);
	cairo_set_source(cr, pat);
	cairo_paint_with_alpha(cr, a * 0.35);
	cairo_pattern_destroy(pat);
	cairo_restore(cr);
}

static void fill_tile(cairo_t *cr, cairo_surface_t *tile, double sx, double sy)
{
	cairo_pattern_t *pat = tile_pattern(cr, tile, sx, sy, CAIRO_FILTER_BEST);
	cairo_set_source(cr, pat);
	cairo_paint(cr);
	cairo_pattern_destroy(pat);
}

static void fill_wood(cairo_t *cr, double x, double y, double w, double h)
{
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_clip(cr);
	fill_tile(cr, wood_tile(), 1.0, 1.0);
	cairo_restore(cr);
}

static void fill_wood_scaled(cairo_t *cr, double x, double y, double w, double h,
	double sx, double sy)
{
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_clip(cr);
	fill_tile(cr, wood_tile(), sx, sy);
	cairo_restore(cr);
}
