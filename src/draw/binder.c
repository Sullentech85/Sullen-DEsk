/* Part of draw.c: the binder, open on the desk.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- the binder, open on the desk ------------------------------ */

/* Taken down off the shelf and laid open on the desk in front of it. The
 * sheets hang from one corner ring and fan downward under their own weight,
 * because three rings lock a stack and a locked stack shows you nothing.
 * The front one is readable whole; the ones behind it show the running foot
 * they were given for exactly this. */
#define BINDER_FAN 9

struct binder_geom {
	double bx, by, bw, bh;      /* the open binder */
	double sx, sy, sw, sh;      /* the front sheet */
	double rx, ry;              /* the ring */
};

static void binder_geom(int w, int h, struct binder_geom *g)
{
	/* Held open against the desk rather than lying on it: the desk is only
	 * the bottom fifth of this view, and a binder small enough to lie there
	 * would not be readable. Its lower edge rests on the wood. */
	double fh = h * 0.470;
	double fw = fh / 1.38;
	if (fw > w * 0.245) {
		fw = w * 0.245;
		fh = fw * 1.38;
	}
	g->sw = fw;
	g->sh = fh;
	g->bw = fw * 1.94;
	g->bh = fh * 1.13;
	g->bx = w * 0.5 - g->bw * 0.5;
	g->by = h * 0.968 - g->bh;
	/* the sheets hang on the right leaf; the left one carries the slip */
	g->sx = w * 0.5 + fw * 0.055;
	g->sy = g->by + fh * 0.075;
	g->rx = g->sx + fw * 0.052;
	g->ry = g->sy + fh * 0.077;
}

/* How far round the ring a sheet has swung. The first few fall a long way
 * and the rest crowd, the way a block of paper actually hangs. Takes a
 * fractional place, so turning the block over is a movement rather than a
 * jump: at -1 a sheet has been carried up past the front and is on its way
 * off the ring. */
static double binder_angle(double i)
{
	if (i < 0.0) {
		/* A sheet being turned goes up and over the ring, not just a few
		 * degrees clear of the front. That arc is the whole of what reads
		 * as turning a page rather than as a list scrolling. */
		double t = clampf(-i, 0.0, 1.0);
		return -1.05 * t * t * (3.0 - 2.0 * t);
	}
	return 0.62 * (1.0 - exp(-i / 3.1));
}

/* How far the sheets have swung down off the stack they were squared up
 * in while the binder was shut, 0 to 1 (as it is opened, op_draw), for
 * the drawing in hand. */
static double g_binder_settle = 1.0;

/* Sheet j of the fan, settle x of the way: the front one goes first and
 * each behind it a little after; each starts slowly and gathers way as it
 * falls, swings a little past where it hangs and comes back, as paper let
 * go of does. */
static double binder_settle(int j, double x)
{
	const double d = 0.06;
	double t = clampf(x * (1.0 + d * (BINDER_FAN - 1)) - d * (j > 0 ? j : 0), 0.0, 1.0);
	double f = clampf(t / 0.78, 0.0, 1.0);
	double fall = f * f * (3.0 - 2.0 * f);
	double past = t > 0.55 ? 0.05 * sin(M_PI * (t - 0.55) / 0.45) : 0.0;
	return fall + past;
}

/* How much of a sheet is left as it goes over. */
static double binder_fade(double place)
{
	if (place >= 0.0) {
		return 1.0;
	}
	return 1.0 - smoothstepf(0.45, 1.0, -place);
}

int desk_binder_fan(void)
{
	return BINDER_FAN;
}

/* Which sheet is under the pointer. Walks front to back, so the one you can
 * see the most of wins, and inverts each sheet's own rotation rather than
 * guessing from a bounding box. */
static void binder_unturn(int w, int h, double *x, double *y);
static void binder_turn3(int w, int h, struct turn3 *t);

int desk_binder_hit(int w, int h, int count, double first, double px, double py)
{
	struct binder_geom g;
	binder_geom(w, h, &g);
	binder_unturn(w, h, &px, &py);
	int base = (int)floor(first);
	double frac = first - base;
	/* front to back, because that is the order they are drawn in reverse */
	for (int j = -2; j < BINDER_FAN; j++) {
		int idx = base + j;
		if (idx < 0 || idx >= count) {
			continue;
		}
		double place = (double)j - frac;
		if (binder_fade(place) < 0.45) {
			continue;   /* on its way over: not a thing you can take */
		}
		double a = binder_angle(place);
		double dx = px - g.rx, dy = py - g.ry;
		double c = cos(-a), s = sin(-a);
		double lx = dx * c - dy * s + (g.rx - g.sx);
		double ly = dx * s + dy * c + (g.ry - g.sy);
		if (lx >= 0 && lx <= g.sw && ly >= 0 && ly <= g.sh) {
			return idx;
		}
	}
	return -1;
}

/* The catalogue mark a sheet is printed with: the shelf's first three
 * letters and the sheet's place in the binder. */
static void binder_code(char *buf, size_t n, const char *key, int idx)
{
	char k[4] = { 0 };
	for (int i = 0; i < 3 && key[i]; i++) {
		k[i] = (char)toupper((unsigned char)key[i]);
	}
	snprintf(buf, n, "%s\xc2\xb7%03d", k, idx + 1);
}

/* ---- the flag on a binder sheet: the favourites ----
 *
 * A sheet whose program is in the favourites has a flag stuck on it, as a
 * page of a catalogue somebody means to find again has: a strip of mustard
 * film, the favourites binder's colour, along the right edge a little
 * below the corner, most of it standing out past the paper (owner
 * 2026-11-10: 紙に付箋を貼る). The sheet under the hand shows faintly where
 * one would go, and more so with the hand on it; pressed there, a flag is
 * stuck on, or peeled off (wm.c). In the sheet's own pixels. */
#define BINDER_FLAG_Y 0.165     /* its top, down the sheet */
#define BINDER_FLAG_H 0.052     /* how wide the strip is */
#define BINDER_FLAG_IN 0.080    /* how much of it is on the paper */
#define BINDER_FLAG_OUT 0.090   /* and how much stands out past it */

/* stuck: on the sheet; hot: the hand on it; dim: the shade the sheet is in */
static void binder_flag(cairo_t *cr, double sw, double sh, bool stuck, bool hot, double dim)
{
	double y = sh * BINDER_FLAG_Y, fh = sh * BINDER_FLAG_H;
	double x0 = sw * (1.0 - BINDER_FLAG_IN), x1 = sw * (1.0 + BINDER_FLAG_OUT);
	double r = fh * 0.16, k = 1.0 - dim;
	cairo_save(cr);
	if (!stuck) {
		/* where one would go: its outline, and a breath of its colour */
		desk_rounded_path(cr, x0, y, x1 - x0, fh, r);
		cairo_set_source_rgba(cr, 0.93, 0.72, 0.18, hot ? 0.38 : 0.10);
		cairo_fill_preserve(cr);
		static const double DASH[] = { 3.0, 2.5 };
		cairo_set_dash(cr, DASH, hot ? 0 : 2, 0.0);
		cairo_set_line_width(cr, 1.1);
		cairo_set_source_rgba(cr, 0.42, 0.33, 0.20, hot ? 0.75 : 0.45);
		cairo_stroke(cr);
		cairo_restore(cr);
		return;
	}
	/* stuck, and lifted a hair at its free end under the hand */
	double lift = hot ? fh * 0.10 : 0.0;
	/* its shadow, past the paper, on whatever is behind */
	desk_rounded_path(cr, sw + 1.5, y + 2.0 + lift, x1 - sw - 1.0, fh, r);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.16 * k);
	cairo_fill(cr);
	/* on the paper the film lets the paper through; past it, it is its
	 * own colour, held up to the light */
	desk_rounded_path(cr, x0, y, sw - x0 + r, fh, r * 0.6);
	cairo_set_source_rgba(cr, 0.93 * k, 0.70 * k, 0.16 * k, 0.52);
	cairo_fill(cr);
	desk_rounded_path(cr, sw - 0.5, y - lift * 0.4, x1 - sw + 0.5, fh, r);
	cairo_pattern_t *g = cairo_pattern_create_linear(sw, 0, x1, 0);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0.90 * k, 0.66 * k, 0.13 * k, 0.92);
	cairo_pattern_add_color_stop_rgba(g, 1.0, (hot ? 1.0 : 0.97) * k, 0.78 * k, 0.24 * k, 0.92);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 0.8);
	cairo_set_source_rgba(cr, 0.46 * k, 0.32 * k, 0.04 * k, 0.55);
	cairo_stroke(cr);
	/* the light along its top edge */
	cairo_move_to(cr, x0 + r, y + 0.9);
	cairo_line_to(cr, x1 - r, y + 0.9 - lift * 0.4);
	cairo_set_line_width(cr, 0.9);
	cairo_set_source_rgba(cr, 1.0, 0.96, 0.80, 0.40 * k);
	cairo_stroke(cr);
	cairo_restore(cr);
}

bool desk_binder_flag_hit(int w, int h, double lx, double ly)
{
	struct binder_geom g;
	binder_geom(w, h, &g);
	double y = g.sh * BINDER_FLAG_Y, fh = g.sh * BINDER_FLAG_H;
	return lx >= g.sw * (1.0 - BINDER_FLAG_IN) - 6.0 && lx <= g.sw * (1.0 + BINDER_FLAG_OUT) + 2.0
		&& ly >= y - 6.0 && ly <= y + fh + 6.0;
}

/* ---- the corner of a binder sheet ----
 *
 * The sheet under the hand has its top right corner turned down, as every
 * sheet on the desk and every letter in the post box has; on its back: open
 * the program, and put it in the drawer (or take it out again). */

static void binder_curl_sizes(const struct binder_geom *g, double *rest, double *open)
{
	*rest = fmax(g->sw * 0.045, 9.0);
	*open = fmin(g->sw * 0.62, g->sh * 0.62);
}

void desk_binder_curl_sizes(int w, int h, double *rest, double *open)
{
	struct binder_geom g;
	binder_geom(w, h, &g);
	binder_curl_sizes(&g, rest, open);
}

static int binder_corner_items(bool in_drawer, const char *words[2], int glyphs[2],
	bool warns[2])
{
	words[DESK_BINDER_OPEN] = TR("Open");
	glyphs[DESK_BINDER_OPEN] = CURL_G_ACTION;
	words[DESK_BINDER_DRAWER] = in_drawer ? TR("Out of the drawer") : TR("Into the drawer");
	glyphs[DESK_BINDER_DRAWER] = CURL_G_DRAWER;
	warns[0] = warns[1] = false;
	return 2;
}

int desk_binder_curl_hit(int w, int h, double s, bool in_drawer, double lx, double ly)
{
	struct binder_geom g;
	binder_geom(w, h, &g);
	double rest, open;
	binder_curl_sizes(&g, &rest, &open);
	const char *words[2];
	int glyphs[2];
	bool warns[2];
	int n = binder_corner_items(in_drawer, words, glyphs, warns);
	return curl_hit_n(g.sw, s, open, n, lx, ly);
}

/* Into the frame of one sheet of the fan: swung `a` round the ring, and
 * lifted a little off the others if it is the one under the pointer. */
static void binder_sheet_frame(cairo_t *cr, const struct binder_geom *g, double a, bool hot)
{
	cairo_translate(cr, g->rx, g->ry);
	cairo_rotate(cr, a);
	cairo_translate(cr, -(g->rx - g->sx) + (hot ? -g->sw * 0.035 : 0.0),
		-(g->ry - g->sy) + (hot ? -g->sh * 0.022 : 0.0));
}

/* Where a point on the screen is on sheet idx of the fan (as it is drawn:
 * lifted, if it is the one under the hand), in the sheet's own pixels from
 * its top left; and the sheet's size. */
void desk_binder_local(int w, int h, double first, int idx, bool hot, double px, double py,
	double *lx, double *ly, double *sw, double *sh)
{
	struct binder_geom g;
	binder_geom(w, h, &g);
	binder_unturn(w, h, &px, &py);
	double a = binder_angle((double)idx - first);
	double dx = px - g.rx, dy = py - g.ry;
	double c = cos(-a), sn = sin(-a);
	*lx = dx * c - dy * sn + (g.rx - g.sx) + (hot ? g.sw * 0.035 : 0.0);
	*ly = dx * sn + dy * c + (g.ry - g.sy) + (hot ? g.sh * 0.022 : 0.0);
	*sw = g.sw;
	*sh = g.sh;
}

/* Where a finger is round the binder's ring, as the binder is held (2026-11-12): the angle,
 * the way the sheets swing (binder_angle: a sheet turned over goes up and round, -1.05), and how
 * far from the ring. False if the binder is not there to be measured. */
bool desk_binder_finger_at(int w, int h, double px, double py, double *angle, double *r)
{
	struct binder_geom g;
	binder_geom(w, h, &g);
	if (g.sw <= 0.0 || g.sh <= 0.0) {
		return false;
	}
	binder_unturn(w, h, &px, &py);
	*angle = atan2(py - g.ry, px - g.rx);
	*r = hypot(px - g.rx, py - g.ry);
	return true;
}

/* A sheet of the fan as it hangs: its middle on the screen, its size, and
 * how far round the ring it has swung (hot: lifted, as the one reached
 * for is). For taking it off the ring. */
void desk_binder_sheet_pose(int w, int h, double place, bool hot, double *cx, double *cy,
	double *sw, double *sh, double *angle)
{
	struct binder_geom g;
	binder_geom(w, h, &g);
	double a = binder_angle(place);
	double lx = g.sw * 0.5 - (g.rx - g.sx) + (hot ? -g.sw * 0.035 : 0.0);
	double ly = g.sh * 0.5 - (g.ry - g.sy) + (hot ? -g.sh * 0.022 : 0.0);
	double mx = g.rx + lx * cos(a) - ly * sin(a);
	double my = g.ry + lx * sin(a) + ly * cos(a);
	/* and as the binder is held: its corners turned, as near a turned
	 * rectangle as they make */
	struct turn3 t;
	binder_turn3(w, h, &t);
	double q[4][2];
	for (int i = 0; i < 4; i++) {
		double u = (i == 1 || i == 2) ? g.sw * 0.5 : -g.sw * 0.5;
		double v = i >= 2 ? g.sh * 0.5 : -g.sh * 0.5;
		turn3_point(&t, mx + u * cos(a) - v * sin(a), my + u * sin(a) + v * cos(a),
			&q[i][0], &q[i][1], NULL);
	}
	*cx = (q[0][0] + q[1][0] + q[2][0] + q[3][0]) * 0.25;
	*cy = (q[0][1] + q[1][1] + q[2][1] + q[3][1]) * 0.25;
	*sw = (hypot(q[1][0] - q[0][0], q[1][1] - q[0][1])
		+ hypot(q[2][0] - q[3][0], q[2][1] - q[3][1])) * 0.5;
	*sh = (hypot(q[3][0] - q[0][0], q[3][1] - q[0][1])
		+ hypot(q[2][0] - q[1][0], q[2][1] - q[1][1])) * 0.5;
	*angle = atan2(q[1][1] - q[0][1] + q[2][1] - q[3][1], q[1][0] - q[0][0] + q[2][0] - q[3][0]);
}

/* The square box, in whole device pixels, that a sheet of the fan is sure
 * to cover: inside the box its turned outline cuts from the sheet, clear
 * of the round corners and of the hole it hangs by. False if there is no
 * such box worth having. */
static bool binder_cover(cairo_t *cr, const struct binder_geom *g, double a, bool hot,
	double box[4])
{
	double px[4] = { 0.0, g->sw, g->sw, 0.0 }, py[4] = { 0.0, 0.0, g->sh, g->sh };
	double hx = g->sw * 0.052, hy = g->sh * 0.077, hr = g->sw * 0.026;
	cairo_save(cr);
	binder_sheet_frame(cr, g, a, hot);
	for (int k = 0; k < 4; k++) {
		cairo_user_to_device(cr, &px[k], &py[k]);
	}
	cairo_user_to_device(cr, &hx, &hy);
	cairo_user_to_device_distance(cr, &hr, &(double){ 0.0 });
	cairo_restore(cr);
	/* for a turned rectangle, between the second and third of its
	 * corners each way is inside it */
	for (int i = 1; i < 4; i++) {
		for (int k = i; k > 0 && px[k] < px[k - 1]; k--) {
			double t = px[k]; px[k] = px[k - 1]; px[k - 1] = t;
		}
		for (int k = i; k > 0 && py[k] < py[k - 1]; k--) {
			double t = py[k]; py[k] = py[k - 1]; py[k - 1] = t;
		}
	}
	const double in = 3.0;   /* the corners are rounded that far */
	double x0 = ceil(px[1] + in), x1 = floor(px[2] - in);
	double y0 = ceil(py[1] + in), y1 = floor(py[2] - in);
	/* the hole is near the top: start below it */
	double r = fabs(hr) + 2.0;
	if (hx + r > x0 && hx - r < x1 && hy + r > y0) {
		y0 = fmax(y0, ceil(hy + r));
	}
	if (x1 - x0 < 16.0 || y1 - y0 < 16.0) {
		return false;
	}
	box[0] = x0;
	box[1] = y0;
	box[2] = x1;
	box[3] = y1;
	return true;
}

/* Clip to everything but that box, as four boxes round it. */
static void binder_clip_outside(cairo_t *cr, const double box[4])
{
	const double far = 32000.0;
	cairo_save(cr);
	cairo_identity_matrix(cr);
	cairo_new_path(cr);
	cairo_rectangle(cr, -far, -far, box[0] + far, far * 2.0);
	cairo_rectangle(cr, box[2], -far, far - box[2], far * 2.0);
	cairo_rectangle(cr, box[0], -far, box[2] - box[0], box[1] + far);
	cairo_rectangle(cr, box[0], box[3], box[2] - box[0], far - box[3]);
	cairo_restore(cr);
	cairo_clip(cr);
}

/* A sheet in the binder, printed once and kept.
 *
 * Printed straight onto the fan at its angle, a sheet cost 18-22 ms: every
 * texture and gradient on it is fetched through the rotation, which is five
 * times what the same sheet costs printed square. The fan is eleven sheets
 * and the whole binder is drawn again for every frame of opening it,
 * turning a sheet and moving over one, so a full binder ran at six frames
 * a second. Printed square once, into a surface of its own, a sheet costs
 * that 4 ms the first time it is seen and about 1 ms each frame after, to
 * lay it on the fan turned.
 *
 * Kept by the app list it came from, where in it, its catalogue mark and
 * its size in pixels. Sixteen is the fan and a few either side of it, for
 * turning back and forth. Drawn from the compositor's thread only. */
#define BINDER_KEEP 16

static struct binder_kept {
	cairo_surface_t *s;
	unsigned gen;
	int app;
	int pw, ph;
	char code[32];
	unsigned long used;
} binder_kept[BINDER_KEEP];
static unsigned long binder_kept_clock;

static cairo_surface_t *binder_sheet(const struct desk_app_list *apps, int app,
	const char *code, double sw, double sh, double scale)
{
	int pw = (int)ceil(sw * scale), ph = (int)ceil(sh * scale);
	if (pw < 8 || ph < 8) {
		return NULL;
	}
	int slot = -1;
	for (int i = 0; i < BINDER_KEEP; i++) {
		struct binder_kept *k = &binder_kept[i];
		if (k->s && k->gen == apps->scan_generation && k->app == app && k->pw == pw
			&& k->ph == ph && strcmp(k->code, code) == 0) {
			k->used = ++binder_kept_clock;
			return k->s;
		}
		/* an empty place, or else the one least lately wanted */
		if (slot < 0 || (binder_kept[slot].s && (!k->s || k->used < binder_kept[slot].used))) {
			slot = i;
		}
	}
	struct binder_kept *k = &binder_kept[slot];
	if (k->s) {
		cairo_surface_destroy(k->s);
	}
	const struct desk_app *a = &apps->apps[app];
	k->s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
	cairo_t *cr = cairo_create(k->s);
	cairo_scale(cr, (double)pw / sw, (double)ph / sh);
	desk_draw_app_sheet(cr, sw, sh, a->name, a->generic, code, desk_app_icon(a),
		a->initial, a->seed, 0.20 + 0.70 * hash21(a->seed, 2.2));
	cairo_destroy(cr);
	cairo_surface_flush(k->s);
	k->gen = apps->scan_generation;
	k->app = app;
	k->pw = pw;
	k->ph = ph;
	snprintf(k->code, sizeof k->code, "%s", code);
	k->used = ++binder_kept_clock;
	return k->s;
}

/* The boards and what on them never changes while it is open: its shadow
 * on the desk, the cloth, the hinge, the stitching and the slip in the left
 * board's pocket. Printed once, like the sheets. */
/* The colour of binder shelf's boards: what its spine is on the shelf --
 * the oxblood calf, the green buckram or the favourites' mustard
 * (shelf_binder_map) -- as it looks under the lamps it is held up to,
 * which are brighter than the room's. */
static void binder_dye(int shelf, double rgb[3])
{
	const struct shelf_binder *sb = shelf_binder_of(shelf);
	int mid = sb ? sb->mid : MID_BINDER_A;
	static const double DYE[3][3] = {
		{ 0.34, 0.12, 0.095 }, { 0.13, 0.19, 0.13 }, { 0.55, 0.39, 0.12 },
	};
	int k = mid == MID_BINDER_FAV ? 2 : mid == MID_BINDER_B ? 1 : 0;
	rgb[0] = DYE[k][0];
	rgb[1] = DYE[k][1];
	rgb[2] = DYE[k][2];
}

static void binder_boards_paint(cairo_t *cr, const struct binder_geom *g,
	const struct desk_app_list *apps, int shelf)
{
	const struct desk_shelf_info *si = &apps->shelf[shelf];
	/* the shadow it throws on the desk */
	contact_shadow(cr, g->bx + g->bw * 0.10, g->by + g->bh * 0.90,
		g->bw * 0.80, g->bh * 0.22, 0.42);

	/* the boards, dyed the colour of the spine it came off */
	double dye[3];
	binder_dye(shelf, dye);
	double br = dye[0], bg = dye[1], bb = dye[2];
	cairo_save(cr);
	desk_rounded_path(cr, g->bx, g->by, g->bw, g->bh, g->bw * 0.012);
	cairo_clip(cr);
	cairo_set_source_rgb(cr, br, bg, bb);
	cairo_paint(cr);
	cairo_pattern_t *lg = cairo_pattern_create_linear(g->bx, g->by, g->bx, g->by + g->bh);
	cairo_pattern_add_color_stop_rgba(lg, 0.00, 1, 0.94, 0.86, 0.15);
	cairo_pattern_add_color_stop_rgba(lg, 0.42, 0, 0, 0, 0.00);
	cairo_pattern_add_color_stop_rgba(lg, 1.00, 0, 0, 0, 0.30);
	cairo_set_source(cr, lg);
	cairo_paint(cr);
	cairo_pattern_destroy(lg);
	overlay_grain(cr, g->bx, g->by, g->bw, g->bh, 1.6);
	cairo_restore(cr);
	/* the hinge crease down the middle, and the stitching round the edge */
	wobble_line(cr, g->bx + g->bw * 0.475, g->by, g->bx + g->bw * 0.475, g->by + g->bh,
		(unsigned)shelf * 7 + 3, 1, 1.4, 2.0, 0.02, 0.01, 0.01, 0.42);
	stitch_rect(cr, g->bx, g->by, g->bw, g->bh, g->bw * 0.020);

	/* the contents slip in the left board's pocket */
	char head[160];
	snprintf(head, sizeof head, "%s", shelf_label(si));
	show_text_fit_as(cr, DESK_FONT_PRINT, head, g->bx + g->bw * 0.225, g->by + g->bh * 0.26,
		g->bh * 0.058, true, 0.92, 0.88, 0.80, 0.80, 0, g->bw * 0.38);
	char cnt[64];
	snprintf(cnt, sizeof cnt, "%d", si->count);
	show_text_as(cr, DESK_FONT_PRINT, cnt, g->bx + g->bw * 0.225, g->by + g->bh * 0.44,
		g->bh * 0.12, true, 0.92, 0.88, 0.80, 0.55, 0);
}

static struct {
	cairo_surface_t *s;
	unsigned gen;
	int shelf, pw, ph;
} binder_kept_boards;

/* The printed boards, and where their top left goes. */
static cairo_surface_t *binder_boards(const struct desk_app_list *apps, int shelf,
	const struct binder_geom *g, double scale, double *ox, double *oy)
{
	/* the boards, and their shadow below them and a little either side */
	*ox = g->bx - 16.0;
	*oy = g->by - 16.0;
	int pw = (int)ceil((g->bw + 32.0) * scale);
	int ph = (int)ceil((g->bh * 1.14 + 32.0) * scale);
	if (pw < 8 || ph < 8) {
		return NULL;
	}
	if (binder_kept_boards.s && binder_kept_boards.gen == apps->generation
		&& binder_kept_boards.shelf == shelf && binder_kept_boards.pw == pw
		&& binder_kept_boards.ph == ph) {
		return binder_kept_boards.s;
	}
	if (binder_kept_boards.s) {
		cairo_surface_destroy(binder_kept_boards.s);
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
	cairo_t *cr = cairo_create(img);
	cairo_scale(cr, scale, scale);
	cairo_translate(cr, -*ox, -*oy);
	/* its shadow falls the way it would where the boards will be */
	double pox = g_paint_ox, poy = g_paint_oy;
	desk_set_paint_origin(*ox * scale, *oy * scale);
	binder_boards_paint(cr, g, apps, shelf);
	desk_set_paint_origin(pox, poy);
	cairo_destroy(cr);
	cairo_surface_flush(img);
	binder_kept_boards.s = img;
	binder_kept_boards.gen = apps->generation;
	binder_kept_boards.shelf = shelf;
	binder_kept_boards.pw = pw;
	binder_kept_boards.ph = ph;
	return img;
}

/* Print ahead -- up to `budget` of them -- the sheets of the fan at
 * `first`, and the two either side of it that a turn brings in next: so
 * the first frame of taking a binder down is not the one that prints
 * eleven, nor the first frame of a turn the one that prints another.
 * True while some are still to do. */
bool desk_binder_warm(int w, int h, int shelf, double first, int budget)
{
	if (!desk_shelf_is_binder(shelf) || w < 8 || h < 8) {
		return false;
	}
	const struct desk_app_list *apps = desk_apps_acquire();
	const struct desk_shelf_info *si = &apps->shelf[shelf];
	struct binder_geom g;
	binder_geom(w, h, &g);
	/* at the scale the compositor's overlay is drawn at */
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	double scale = fmax((double)ow / w, 0.25);
	int pw = (int)ceil(g.sw * scale), ph = (int)ceil(g.sh * scale);
	bool more = false;
	int base = (int)floor(first);
	for (int n = 0; n < BINDER_FAN + 4; n++) {
		/* the fan first, front to back, then what comes next either way */
		int idx = n < BINDER_FAN ? base + n
			: (n - BINDER_FAN) % 2 == 0 ? base + BINDER_FAN + (n - BINDER_FAN) / 2
			: base - 1 - (n - BINDER_FAN) / 2;
		if (idx < 0 || idx >= si->count) {
			continue;
		}
		char code[32];
		binder_code(code, sizeof code, si->key, idx);
		bool have = false;
		for (int i = 0; i < BINDER_KEEP && !have; i++) {
			const struct binder_kept *k = &binder_kept[i];
			have = k->s && k->gen == apps->scan_generation
				&& k->app == desk_shelf_app_index(apps, shelf, idx)
				&& k->pw == pw && k->ph == ph && strcmp(k->code, code) == 0;
		}
		if (have) {
			continue;
		}
		if (budget <= 0) {
			more = true;
			break;
		}
		binder_sheet(apps, desk_shelf_app_index(apps, shelf, idx), code, g.sw, g.sh, scale);
		budget--;
	}
	desk_apps_release(apps);
	return more;
}

void desk_binder_forget(void)
{
	for (int i = 0; i < BINDER_KEEP; i++) {
		if (binder_kept[i].s) {
			cairo_surface_destroy(binder_kept[i].s);
		}
		binder_kept[i] = (struct binder_kept){ 0 };
	}
	if (binder_kept_boards.s) {
		cairo_surface_destroy(binder_kept_boards.s);
		binder_kept_boards.s = NULL;
	}
}

/* An open binder's layers: the shadow it throws, its boards (with the
 * slip in the pocket), and its sheets on the ring. */
enum { BINDER_FLAT_SHADOW = 1, BINDER_FLAT_BOARDS = 2, BINDER_FLAT_SHEETS = 4,
	BINDER_FLAT_ALL = 7 };

static void binder_draw_flat(cairo_t *cr, int w, int h, int shelf, double first,
	int sel, double open_t, const struct desk_binder_corner *corner, int taken, int parts)
{
	if (!desk_shelf_is_binder(shelf)) {
		return;
	}
	double e = clampf(open_t, 0.0, 1.0);
	e = e * e * (3.0 - 2.0 * e);
	if (e <= 0.002) {
		return;
	}
	/* held up in front of the bookcase, in its light */
	struct lamp_px lamp_was = lamp_view(w, h);
	double pox = g_paint_ox, poy = g_paint_oy;   /* put back on the way out */
	const struct desk_app_list *apps = desk_apps_acquire();
	const struct desk_shelf_info *si = &apps->shelf[shelf];
	struct binder_geom g;
	binder_geom(w, h, &g);
	/* the lamp is fixed to the screen, and this may be being painted into
	 * a buffer that starts somewhere else on it */
	double sx0 = 0.0, sy0 = 0.0;
	cairo_user_to_device(cr, &sx0, &sy0);
	desk_set_paint_origin(-sx0, -sy0);

	/* it comes down off the shelf, so it arrives from up the frame */
	cairo_save(cr);
	cairo_translate(cr, 0, (1.0 - e) * -h * 0.42);
	cairo_push_group(cr);

	/* the boards, from the copy printed when it was taken down */
	double ux = 1.0, uy = 0.0;
	cairo_user_to_device_distance(cr, &ux, &uy);
	double scale = fmax(hypot(ux, uy), 0.25);
	double ox, oy;
	cairo_surface_t *boards = (parts & (BINDER_FLAT_SHADOW | BINDER_FLAT_BOARDS))
		? binder_boards(apps, shelf, &g, scale, &ox, &oy) : NULL;
	if (boards) {
		cairo_save(cr);
		if (!(parts & BINDER_FLAT_SHADOW)) {
			desk_rounded_path(cr, g.bx, g.by, g.bw, g.bh, g.bw * 0.012);
			cairo_clip(cr);
		} else if (!(parts & BINDER_FLAT_BOARDS)) {
			cairo_rectangle(cr, g.bx - g.bw, g.by - g.bh, g.bw * 3.0, g.bh * 3.0);
			desk_rounded_path(cr, g.bx, g.by, g.bw, g.bh, g.bw * 0.012);
			cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
			cairo_clip(cr);
		}
		cairo_translate(cr, ox, oy);
		cairo_scale(cr, 1.0 / scale, 1.0 / scale);
		cairo_set_source_surface(cr, boards, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	}
	int base = (int)floor(first);
	double frac = first - base;
	if (!(parts & BINDER_FLAT_SHEETS)) {
		if ((parts & BINDER_FLAT_BOARDS) && si->count > BINDER_FAN) {
			char more[64];
			int last = base + BINDER_FAN < si->count ? base + BINDER_FAN : si->count;
			snprintf(more, sizeof more, "%d\xe2\x80\x93%d", base + 1, last);
			show_text(cr, more, g.bx + g.bw * 0.225, g.by + g.bh * 0.56,
				g.bh * 0.048, false, 0.92, 0.88, 0.80, 0.45, 0);
		}
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, e);
		cairo_restore(cr);
		desk_apps_release(apps);
		g_light = lamp_was;
		desk_set_paint_origin(pox, poy);
		return;
	}
	if ((parts & BINDER_FLAT_BOARDS) && si->count > BINDER_FAN) {
		char more[64];
		int last = base + BINDER_FAN < si->count ? base + BINDER_FAN : si->count;
		snprintf(more, sizeof more, "%d\xe2\x80\x93%d", base + 1, last);
		show_text(cr, more, g.bx + g.bw * 0.225, g.by + g.bh * 0.56,
			g.bh * 0.048, false, 0.92, 0.88, 0.80, 0.45, 0);
	}

	/* Most of every sheet in the fan is under the one in front of it all,
	 * which is opaque paper: the ones behind are laid on only outside the
	 * largest square box -- in whole pixels -- that sheet covers. A box is
	 * what cairo clips to cheaply; the sheet's own outline is not. */
	int front = BINDER_FAN;
	for (int j = -2; j < BINDER_FAN; j++) {
		int idx = base + j;
		if (idx >= 0 && idx < si->count && idx != taken
			&& binder_fade((double)j - frac) >= 0.999) {
			front = j;
			break;
		}
	}
	/* the corner of the one under the hand, turned down: what is behind
	 * it shows where it was, so nothing is hidden behind a front sheet
	 * that has one */
	bool cornered = corner && corner->idx >= 0 && corner->idx == sel && corner->s >= 0.5;
	double hide[4];
	bool hiding = front < BINDER_FAN && !(cornered && base + front == sel)
		&& binder_cover(cr, &g, binder_angle((double)front - frac), base + front == sel, hide);

	/* Back to front, each swung further round the ring. The one at -1 is
	 * the sheet you have just turned past, still on its way up and off. */
	for (int j = BINDER_FAN - 1; j >= -2; j--) {
		int idx = base + j;
		if (idx < 0 || idx >= si->count || idx == taken) {
			continue;   /* (the one taken out is in the hand) */
		}
		double place = (double)j - frac;
		double fade = binder_fade(place);
		if (fade <= 0.004) {
			continue;
		}
		double a = binder_angle(place);
		if (g_binder_settle < 1.0 && place >= 0.0) {
			a *= binder_settle(j, g_binder_settle);
		}
		bool hot = idx == sel;
		char code[32];
		binder_code(code, sizeof code, si->key, idx);
		int ai = desk_shelf_app_index(apps, shelf, idx);
		cairo_surface_t *sheet = binder_sheet(apps, ai, code, g.sw, g.sh, scale);
		cairo_save(cr);
		if (hiding && j > front) {
			binder_clip_outside(cr, hide);
		}
		if (fade < 0.999) {
			/* a group only the size of this sheet, not of the screen */
			double m = g.sw * 0.08, r = hypot(g.sw, g.sh) + m;
			cairo_rectangle(cr, g.rx - r, g.ry - r, r * 2.0, r * 2.0);
			cairo_clip(cr);
			cairo_push_group(cr);
		}
		binder_sheet_frame(cr, &g, a, hot);
		/* the sheet's own shadow on the one behind it */
		contact_shadow(cr, g.sw * 0.06, g.sh * 0.92, g.sw * 0.88, g.sh * 0.10,
			0.26 + (hot ? 0.10 : 0.0));
		double cs = hot && cornered ? clampf(corner->s, 0.0, g.sh * 0.98) : 0.0;
		if (sheet) {
			cairo_save(cr);
			if (cs > 0.0) {
				/* the paper the corner was, turned over onto the sheet */
				cairo_move_to(cr, -1.0, -1.0);
				cairo_line_to(cr, g.sw - cs, -1.0);
				cairo_line_to(cr, g.sw + 1.0, cs);
				cairo_line_to(cr, g.sw + 1.0, g.sh + 1.0);
				cairo_line_to(cr, -1.0, g.sh + 1.0);
				cairo_close_path(cr);
				cairo_clip(cr);
			}
			cairo_scale(cr, g.sw / cairo_image_surface_get_width(sheet),
				g.sh / cairo_image_surface_get_height(sheet));
			cairo_set_source_surface(cr, sheet, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
			cairo_paint(cr);
			cairo_restore(cr);
		}
		if (cs > 0.0) {
			double rest, open;
			binder_curl_sizes(&g, &rest, &open);
			const char *words[2];
			int glyphs[2];
			bool warns[2];
			int n = binder_corner_items(corner->in_drawer, words, glyphs, warns);
			curl_draw(cr, g.sw, cs, open, corner->lift, corner->hot,
				apps->apps[ai].seed, n, words, glyphs, warns);
		}
		/* its flag, stuck on; or on the one under the hand, where one goes */
		double shade = hot ? 0.0 : 0.05 + 0.030 * fmax((double)j - frac, 0.0);
		if (ai >= 0 && (apps->apps[ai].fav || (hot && corner && corner->hot < 0))) {
			binder_flag(cr, g.sw, g.sh, apps->apps[ai].fav, hot && corner && corner->flag_hot,
				fmin(shade * 2.2, 0.6));
		}
		if (!hot) {
			/* the ones behind sit in the front one's shade */
			cairo_set_source_rgba(cr, 0.06, 0.04, 0.02, shade);
			cairo_rectangle(cr, 0, 0, g.sw, g.sh);
			cairo_fill(cr);
		}
		if (fade < 0.999) {
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr, fade);
		}
		cairo_restore(cr);
	}

	/* the ring they all hang from */
	double rr = g.sw * 0.052;
	cairo_save(cr);
	cairo_set_line_width(cr, rr * 0.42);
	cairo_set_source_rgba(cr, 0.20, 0.16, 0.10, 0.45);
	cairo_arc(cr, g.rx, g.ry + rr * 0.18, rr, 0, 2 * M_PI);
	cairo_stroke(cr);
	cairo_pattern_t *mg = cairo_pattern_create_linear(g.rx - rr, g.ry - rr,
		g.rx + rr, g.ry + rr);
	cairo_pattern_add_color_stop_rgb(mg, 0.0, 0.92, 0.88, 0.78);
	cairo_pattern_add_color_stop_rgb(mg, 0.5, 0.58, 0.54, 0.48);
	cairo_pattern_add_color_stop_rgb(mg, 1.0, 0.82, 0.78, 0.70);
	cairo_set_source(cr, mg);
	cairo_arc(cr, g.rx, g.ry, rr, 0, 2 * M_PI);
	cairo_stroke(cr);
	cairo_pattern_destroy(mg);
	cairo_restore(cr);

	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, e);
	cairo_restore(cr);
	desk_apps_release(apps);
	g_light = lamp_was;
	desk_set_paint_origin(pox, poy);
}

/* All the open binder can cover with its sheets fanned from `first`: the
 * boards and their shadow, and every sheet swung round the ring with its
 * own shadow and the lift of the one under the pointer. The compositor
 * makes a buffer this size rather than one the size of the screen. */
static void binder_box_flat(int w, int h, double first, double box[4])
{
	struct binder_geom g;
	binder_geom(w, h, &g);
	double x0 = g.bx - 14.0, y0 = g.by - 14.0;
	double x1 = g.bx + g.bw + 14.0, y1 = g.by + g.bh * 1.14 + 14.0;
	int base = (int)floor(first);
	double frac = first - base;
	double m = g.sw * 0.08 + 14.0;
	for (int j = -2; j < BINDER_FAN; j++) {
		double place = (double)j - frac;
		if (binder_fade(place) <= 0.004) {
			continue;
		}
		double a = binder_angle(place), c = cos(a), sn = sin(a);
		for (int k = 0; k < 4; k++) {
			double lx = g.sx - g.rx + (k & 1 ? g.sw : 0.0);
			double ly = g.sy - g.ry + (k & 2 ? g.sh : 0.0);
			double x = g.rx + lx * c - ly * sn, y = g.ry + lx * sn + ly * c;
			x0 = fmin(x0, x - m);
			y0 = fmin(y0, y - m);
			x1 = fmax(x1, x + m);
			y1 = fmax(y1, y + m);
		}
	}
	x0 = fmax(x0, 0.0);
	y0 = fmax(y0, 0.0);
	x1 = fmin(x1, (double)w);
	y1 = fmin(y1, (double)h);
	box[0] = x0;
	box[1] = y0;
	box[2] = fmax(x1 - x0, 0.0);
	box[3] = fmax(y1 - y0, 0.0);
}

/* ---- the binder held up ----
 *
 * As the calendar and the binder of notes are: the binder open, its sheets
 * fanned, drawn flat and laid into the view a little turned and leaning
 * back, the edges of its boards showing where they come round to the eye. */

#define BINDER_HELD_YAW 0.16
#define BINDER_HELD_PITCH 0.14

static void binder_turn3(int w, int h, struct turn3 *t)
{
	struct binder_geom g;
	binder_geom(w, h, &g);
	t->px = g.bx + g.bw * 0.5;
	t->py = g.by + g.bh * 0.5;
	t->yaw = BINDER_HELD_YAW;
	t->pitch = BINDER_HELD_PITCH;
	t->ex = w * 0.5;
	t->ey = h * 0.5;
	t->f = h * 1.6;
}

/* a point on the screen, back onto the binder's face */
static void binder_unturn(int w, int h, double *x, double *y)
{
	struct turn3 t;
	binder_turn3(w, h, &t);
	struct binder_geom g;
	binder_geom(w, h, &g);
	struct hmat map, inv;
	if (turn3_hmat(&t, g.bx, g.by, g.bw, g.bh, &map) && hmat_invert(&map, &inv)) {
		hmat_apply(&inv, *x, *y, x, y);
	}
}

/* In the lamps' light as it is held: whatever is drawn of it, a little
 * darker towards its foot (over the drawing, not round it). */
static void binder_darken(cairo_t *cr, int w, int h)
{
	struct turn3 t;
	binder_turn3(w, h, &t);
	struct binder_geom g;
	binder_geom(w, h, &g);
	double lit = turn3_lit(&t, 0.0, 0.0, -1.0, HELD_LIGHT_X, HELD_LIGHT_Y, HELD_LIGHT_Z);
	double lit0 = fmax(-HELD_LIGHT_Z / sqrt(HELD_LIGHT_X * HELD_LIGHT_X
		+ HELD_LIGHT_Y * HELD_LIGHT_Y + HELD_LIGHT_Z * HELD_LIGHT_Z), 1e-3);
	double dark = clampf(1.0 - lit / lit0, 0.0, 0.6) * 0.35;
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);
	cairo_pattern_t *gr = cairo_pattern_create_linear(0, g.by, 0, g.by + g.bh * 1.15);
	cairo_pattern_add_color_stop_rgba(gr, 0.0, 0.0, 0.0, 0.0, dark);
	cairo_pattern_add_color_stop_rgba(gr, 1.0, 0.0, 0.0, 0.0, dark + 0.08);
	cairo_set_source(cr, gr);
	cairo_paint(cr);
	cairo_pattern_destroy(gr);
	cairo_restore(cr);
}

/* ---- a binder, taken off the shelf and opened (op_draw) ----
 *
 * Its front board outside is the boards' cloth, dyed as its spine, with
 * the stitching round it and the name of what is in it pressed in; its
 * spine carries the label in its pocket, as on the shelf; the block is the
 * sheets, their edges plain paper. The left of it held open (the slip in
 * the pocket) rides on the inside of the front board, the right's board
 * under the sheets and the ring. */

static void binder_cover_face(cairo_t *cr, double W, double H, double r,
	const struct desk_shelf_info *si, unsigned seed)
{
	double dye[3];
	binder_dye((int)seed, dye);
	cairo_save(cr);
	desk_rounded_path(cr, 0.0, 0.0, W, H, r);
	cairo_clip(cr);
	cairo_set_source_rgb(cr, dye[0], dye[1], dye[2]);
	cairo_paint(cr);
	cairo_pattern_t *lg = cairo_pattern_create_linear(0, 0, 0, H);
	cairo_pattern_add_color_stop_rgba(lg, 0.00, 1, 0.94, 0.86, 0.15);
	cairo_pattern_add_color_stop_rgba(lg, 0.42, 0, 0, 0, 0.00);
	cairo_pattern_add_color_stop_rgba(lg, 1.00, 0, 0, 0, 0.30);
	cairo_set_source(cr, lg);
	cairo_paint(cr);
	cairo_pattern_destroy(lg);
	overlay_grain(cr, 0.0, 0.0, W, H, 1.6);
	cairo_restore(cr);
	stitch_rect(cr, 0.0, 0.0, W, H, W * 0.040);
	/* what is in it, pressed into the cloth: a shade darker, and a hair
	 * of light along the bottom of the letters where the edge catches it */
	const char *name = shelf_label(si);
	show_text_fit_as(cr, DESK_FONT_DISPLAY, name, W * 0.5, H * 0.215 + 1.2, H * 0.064, true,
		1.0, 0.95, 0.85, 0.22, 0, W * 0.70);
	show_text_fit_as(cr, DESK_FONT_DISPLAY, name, W * 0.5, H * 0.215, H * 0.064, true,
		0.0, 0.0, 0.0, 0.50, 0, W * 0.70);
}

/* The spine of binder i, ts across and H high: its label in the pocket
 * where it is on the shelf, the rest the cloth (drawn round it). */
static void binder_spine_face(cairo_t *cr, int i, double ts, double H,
	const struct desk_shelf_info *si)
{
	const struct shelf_binder *b = shelf_binder_of(i);
	double k = H / b->h;
	double cy = H - b->label_y * k;
	/* the pocket, and the label in it */
	double px = b->hw * 0.72 * k, py = 1.55 * k;
	cairo_save(cr);
	desk_rounded_path(cr, ts * 0.5 - px, cy - py, px * 2.0, py * 2.0, px * 0.12);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.38);
	cairo_fill(cr);
	const double LW = binder_label_w(i), LH = 280.0;
	double lx = b->hw * 0.66 * k, ly = 1.42 * k;
	cairo_translate(cr, ts * 0.5 - lx, cy - ly);
	cairo_scale(cr, lx * 2.0 / LW, ly * 2.0 / LH);
	cairo_rectangle(cr, 0, 0, LW, LH);
	cairo_clip(cr);
	binder_label_face(cr, LW, LH, shelf_label(si), si->count,
		(unsigned)(i * 37 + 11), i == DESK_SHELF_FAV);
	cairo_restore(cr);
}

/* What a binder is drawn from as it is taken down, kept as the book's
 * pictures are: those of its sheets as they are fanned, and all else. */
struct binder_pics {
	uint64_t key, key_fixed;
	double k;
	struct book_pic left, board, pages, shadow;
	cairo_surface_t *cover;
	cairo_surface_t *spine[OP_SPINE_N];
};
static struct binder_pics g_npics;

static void binder_pics_drop(bool all_of_it)
{
	op_pic_drop(&g_npics.pages);
	/* the left board has the range of the fan ("19-27") printed on it */
	op_pic_drop(&g_npics.left);
	g_npics.key = 0;
	if (!all_of_it) {
		return;
	}
	op_pic_drop(&g_npics.board);
	op_pic_drop(&g_npics.shadow);
	if (g_npics.cover) {
		cairo_surface_destroy(g_npics.cover);
		g_npics.cover = NULL;
	}
	for (int i = 0; i < OP_SPINE_N; i++) {
		if (g_npics.spine[i]) {
			cairo_surface_destroy(g_npics.spine[i]);
			g_npics.spine[i] = NULL;
		}
	}
	g_npics.key_fixed = 0;
}

struct binder_pic_arg {
	int w, h, shelf, parts;
	double first;
};

static void binder_pic_draw(cairo_t *cr, void *arg)
{
	const struct binder_pic_arg *a = arg;
	binder_draw_flat(cr, a->w, a->h, a->shelf, a->first, -1, 1.0, NULL, -1, a->parts);
	binder_darken(cr, a->w, a->h);
}

/* The sheets as the binder is opened: squared up in a stack on the right
 * board while it is shut, and let go as the front board comes up, to
 * swing down off the ring into their fan (binder_settle) by the time it
 * lies open -- printed again each frame they move, and once they hang as
 * they do held open, the kept picture of them. */
static const struct book_pic *binder_pages_at(const struct op_kind *K, const struct book_pose *P)
{
	double x = clampf((P->o - 0.38) / 0.62, 0.0, 1.0);
	if (x >= 1.0) {
		return &g_npics.pages;
	}
	static struct book_pic pic;
	static double pic_x = -1.0, pic_k, pic_first;
	static int pic_w, pic_h, pic_shelf = -1;
	int w = (int)lround(K->held.ex * 2.0), h = (int)lround(K->held.ey * 2.0);
	if (pic.s && pic_x == x && pic_k == K->k && pic_w == w && pic_h == h
		&& pic_shelf == K->shelf && pic_first == K->first) {
		return &pic;
	}
	op_pic_drop(&pic);
	double box[4];
	binder_box_flat(w, h, K->first, box);
	struct binder_pic_arg a = { w, h, K->shelf, BINDER_FLAT_SHEETS, K->first };
	g_binder_settle = x;
	pic = op_pic(K->k, box[0], box[1], box[0], box[1], box[2], box[3], binder_pic_draw, &a);
	g_binder_settle = 1.0;
	pic_x = x;
	pic_k = K->k;
	pic_w = w;
	pic_h = h;
	pic_shelf = K->shelf;
	pic_first = K->first;
	return &pic;
}

/* Binder shelf as a thing to be taken down and opened: the boards dyed
 * as its spine, as thick and deep to its height as it stands on the
 * shelf; pics, if not NULL, what it is drawn from. */
static void binder_kind(int w, int h, int shelf, const struct binder_pics *pics,
	struct op_kind *K)
{
	memset(K, 0, sizeof *K);
	struct binder_geom g;
	binder_geom(w, h, &g);
	const struct shelf_binder *sb = shelf_binder_of(shelf);
	K->bx = g.bx;
	K->by = g.by;
	K->bw = g.bw;
	K->bh = g.bh;
	K->gx = g.bx + g.bw * 0.475;
	binder_turn3(w, h, &K->held);
	K->b = g.bw * 0.012;
	K->fore_in = g.bw * 0.020;
	K->yt = -g.bh * 0.5 + g.bh * 0.030;
	K->yb = g.bh * 0.5 - g.bh * 0.030;
	K->ts = g.bh * (2.0 * sb->hw) / sb->h;
	K->rb = K->ts * 0.08;
	K->W0 = g.bh * RM_BN_DEPTH / sb->h;
	op_shelf_spine(w, h, sb->x, sb->hw, sb->h, RM_BN_ZF, &K->scx, &K->scy, &K->ssh);
	K->s1 = 0.80;
	binder_dye(shelf, K->leather);
	K->edge[0] = 0.93;
	K->edge[1] = 0.91;
	K->edge[2] = 0.86;
	K->lit_faces = false;
	K->shelf = shelf;
	if (pics) {
		K->k = pics->k;
		K->left = &pics->left;
		K->board = &pics->board;
		K->pages = &pics->pages;
		K->shadow = &pics->shadow;
		K->cover = pics->cover;
		K->spine = pics->spine;
	}
}

/* Print the pictures for binder shelf fanned from first: all that are
 * missing, or (one) just the next. True when they are all there. */
static bool binder_pics_step(int w, int h, int shelf, double first, double k, bool one)
{
	const struct desk_app_list *apps = desk_apps_acquire();
	unsigned gen = apps->generation;
	const struct desk_shelf_info si = apps->shelf[shelf];
	desk_apps_release(apps);
	uint64_t fixed = 14695981039346656037ull;
	int vals[5] = { w, h, (int)lround(k * 1000.0), (int)gen, shelf };
	for (size_t i = 0; i < sizeof vals; i++) {
		fixed = (fixed ^ ((const unsigned char *)vals)[i]) * 1099511628211ull;
	}
	fixed |= 1;
	uint64_t key = ((fixed ^ (uint64_t)lround(first * 1000.0)) * 1099511628211ull) | 1;
	if (g_npics.key_fixed != fixed) {
		binder_pics_drop(true);
		g_npics.key_fixed = fixed;
		g_npics.k = k;
	}
	if (g_npics.key != key) {
		binder_pics_drop(false);
		g_npics.key = key;
	}
	struct lamp_px lamp_was = lamp_view(w, h);
	struct binder_geom g;
	binder_geom(w, h, &g);
	double gx = g.bx + g.bw * 0.475;
	double box[4];
	binder_box_flat(w, h, first, box);
	struct binder_pic_arg a = { w, h, shelf, BINDER_FLAT_BOARDS, first };
	bool done = false;
	for (int step = 0; !done; step++) {
		if (!g_npics.left.s) {
			a.parts = BINDER_FLAT_BOARDS;
			g_npics.left = op_pic(k, box[0], box[1], g.bx, g.by, gx - g.bx, g.bh,
				binder_pic_draw, &a);
		} else if (!g_npics.board.s) {
			a.parts = BINDER_FLAT_BOARDS;
			g_npics.board = op_pic(k, box[0], box[1], gx, g.by, g.bx + g.bw - gx, g.bh,
				binder_pic_draw, &a);
		} else if (!g_npics.shadow.s) {
			a.parts = BINDER_FLAT_SHADOW;
			g_npics.shadow = op_pic(k, box[0], box[1], g.bx - 16.0, g.by - 16.0,
				g.bw + 32.0, g.bh * 1.14 + 32.0, binder_pic_draw, &a);
		} else if (!g_npics.pages.s) {
			a.parts = BINDER_FLAT_SHEETS;
			g_npics.pages = op_pic(k, box[0], box[1], box[0], box[1], box[2], box[3],
				binder_pic_draw, &a);
		} else if (!g_npics.cover) {
			double cw = g.bx + g.bw - gx;
			g_npics.cover = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
				(int)ceil(cw * k), (int)ceil(g.bh * k));
			cairo_t *c = cairo_create(g_npics.cover);
			cairo_scale(c, k, k);
			binder_cover_face(c, cw, g.bh, g.bw * 0.012, &si, (unsigned)shelf);
			cairo_destroy(c);
			cairo_surface_flush(g_npics.cover);
		} else if (!g_npics.spine[0]) {
			const struct shelf_binder *sb = shelf_binder_of(shelf);
			double ts = g.bh * (2.0 * sb->hw) / sb->h, sw = ts / OP_SPINE_N;
			for (int i = 0; i < OP_SPINE_N; i++) {
				g_npics.spine[i] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
					(int)ceil(sw * k), (int)ceil(g.bh * k));
				cairo_t *sc = cairo_create(g_npics.spine[i]);
				cairo_scale(sc, k, k);
				cairo_translate(sc, -sw * i, 0.0);
				binder_spine_face(sc, shelf, ts, g.bh, &si);
				cairo_destroy(sc);
				cairo_surface_flush(g_npics.spine[i]);
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
	return done || (g_npics.left.s && g_npics.board.s && g_npics.shadow.s && g_npics.pages.s
		&& g_npics.cover && g_npics.spine[0]);
}

static const struct binder_pics *binder_pics_for(int w, int h, int shelf, double first,
	double k)
{
	binder_pics_step(w, h, shelf, first, k, false);
	return &g_npics;
}

/* The pictures for binder shelf as it is taken down, printed one a call
 * ahead of it: true while there is more to print. */
bool desk_binder_anim_warm(int w, int h, int shelf)
{
	if (!desk_shelf_is_binder(shelf) || w < 8 || h < 8) {
		return false;
	}
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	return !binder_pics_step(w, h, shelf, 0.0, (double)ow / w, true);
}

void desk_draw_binder(cairo_t *cr, int w, int h, int shelf, double first,
	int sel, double open_t, const struct desk_binder_corner *corner, int taken)
{
	if (!desk_shelf_is_binder(shelf) || open_t <= 0.002) {
		return;
	}
	if (open_t < 0.999) {
		/* coming off the shelf and opening, or going back */
		struct lamp_px lamp_was = lamp_view(w, h);
		double kx = 1.0, ky = 0.0;
		cairo_user_to_device_distance(cr, &kx, &ky);
		double k = fmax(hypot(kx, ky), 0.25);
		const struct binder_pics *pc = binder_pics_for(w, h, shelf, first, k);
		struct op_kind K;
		binder_kind(w, h, shelf, pc, &K);
		K.first = first;
		K.pages_at = binder_pages_at;
		op_draw(cr, &K, open_t);
		g_light = lamp_was;
		return;
	}
	double box[4];
	binder_box_flat(w, h, first, box);
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
	binder_draw_flat(c, w, h, shelf, first, sel, 1.0, corner, taken, BINDER_FLAT_ALL);
	binder_darken(c, w, h);
	cairo_destroy(c);

	struct turn3 t;
	binder_turn3(w, h, &t);
	struct op_kind K;
	binder_kind(w, h, shelf, NULL, &K);
	op_board_edges(cr, &K);
	struct hmat map;
	if (turn3_hmat(&t, box[0], box[1], box[2], box[3], &map)) {
		warp_paint(cr, img, box[0], box[1], k, &map, 1.0);
	}
	cairo_surface_destroy(img);
}

void desk_binder_box(int w, int h, double first, double open_t, double box[4])
{
	if (open_t < 0.999) {
		/* on its way from the shelf, or back */
		box[0] = 0.0;
		box[1] = 0.0;
		box[2] = w;
		box[3] = h;
		return;
	}
	double fb[4];
	binder_box_flat(w, h, first, fb);
	struct turn3 t;
	binder_turn3(w, h, &t);
	struct binder_geom g;
	binder_geom(w, h, &g);
	double tb = g.bw * 0.012;
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 8; i++) {
		double x = (i & 1) ? fb[0] + fb[2] : fb[0], y = (i & 2) ? fb[1] + fb[3] : fb[1];
		double sx, sy;
		turn3_project(&t, x, y, (i & 4) ? tb : 0.0, &sx, &sy, NULL);
		x0 = fmin(x0, sx);
		y0 = fmin(y0, sy);
		x1 = fmax(x1, sx);
		y1 = fmax(y1, sy);
	}
	x0 = fmax(floor(x0) - 2.0, 0.0);
	y0 = fmax(floor(y0) - 2.0, 0.0);
	x1 = fmin(ceil(x1) + 2.0, (double)w);
	y1 = fmin(ceil(y1) + 2.0, (double)h);
	box[0] = x0;
	box[1] = y0;
	box[2] = fmax(x1 - x0, 0.0);
	box[3] = fmax(y1 - y0, 0.0);
}

/* ---- the book of every program, open -----------------------------------
 *
 * Taken down off the shelf it lies open on the desk in front of the case,
 * held a little up off it as the binders are. It is an index: every
 * program on the machine, A to Z by the name it was given, a line to each
 * -- its mark, its name, what it is, and a leader of dots out to the shelf
 * it is filed on -- under a red initial where the letter changes. Down the
 * fore edge of the right-hand page, the thumb index: a tab cut for each
 * letter, the ones with nothing under them blind. At the head of every
 * left-hand page is a line to write on: what is written there, the index
 * is of only what answers to it -- by its name, in any language it has
 * one in, by what it is, by the words it is known by -- best first. A
 * ribbon to keep the place. A leaf turns over the spine, as a leaf does. */

#define BOOK_MAX 1024

static double cal_spaced_w(cairo_t *cr, const char *s, double size, double track, bool bold);
static double cal_spaced(cairo_t *cr, const char *s, double x, double y, double size,
	double track, bool bold, double r, double g, double b);

static int book_lc(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* where needle is in hay, letters of either case alike; -1 if not */
static int book_find(const char *hay, const char *needle)
{
	size_t n = strlen(needle);
	if (!n) {
		return 0;
	}
	for (size_t i = 0; hay[i]; i++) {
		size_t k = 0;
		while (k < n && hay[i + k] && book_lc((unsigned char)hay[i + k])
			== book_lc((unsigned char)needle[k])) {
			k++;
		}
		if (k == n) {
			return (int)i;
		}
	}
	return -1;
}

/* how well an app answers to q: 0 its name starts with it, 1 a word of its
 * name does, 2 its name has it, 3 what it is or the words it is known by
 * have it; -1 not at all */
static int book_rank(const struct desk_app *a, const char *q)
{
	const char *names[2] = { a->name, a->name_c };
	int best = -1;
	for (int i = 0; i < 2; i++) {
		int at = book_find(names[i], q);
		if (at < 0) {
			continue;
		}
		int r = at == 0 ? 0 : (names[i][at - 1] == ' ' || names[i][at - 1] == '-') ? 1 : 2;
		best = best < 0 || r < best ? r : best;
	}
	if (best >= 0) {
		return best;
	}
	if (book_find(a->generic, q) >= 0 || book_find(a->keywords, q) >= 0
		|| book_find(a->id, q) >= 0) {
		return 3;
	}
	return -1;
}

/* the letter of the thumb index an app is under: 0 to 25, 26 for the rest */
static int book_letter(const struct desk_app *a)
{
	int c = book_lc((unsigned char)a->name_c[0]);
	return c >= 'a' && c <= 'z' ? c - 'a' : 26;
}

static int book_cmp_name(const struct desk_app *a, const struct desk_app *b)
{
	int la = book_letter(a), lb = book_letter(b);
	if (la != lb) {
		return la < lb ? -1 : 1;
	}
	for (int i = 0;; i++) {
		int x = book_lc((unsigned char)a->name_c[i]), y = book_lc((unsigned char)b->name_c[i]);
		if (x != y || !x) {
			return x < y ? -1 : (x > y ? 1 : 0);
		}
	}
}

static void book_trim(const char *in, char *out, size_t n)
{
	while (*in == ' ') {
		in++;
	}
	snprintf(out, n, "%s", in);
	size_t k = strlen(out);
	while (k > 0 && out[k - 1] == ' ') {
		out[--k] = '\0';
	}
}

/* The programs answering to q (trimmed already) in the list l, best first:
 * their places in l->apps, at most max of them into out; how many there are.
 * On a list the caller holds, so the places are places in that list. */
static int book_matches_in(const struct desk_app_list *l, const char *q, int *out, int max)
{
	static int idx[BOOK_MAX], rank[BOOK_MAX];
	int n = 0;
	for (int i = 0; i < l->n && n < BOOK_MAX; i++) {
		const struct desk_app *a = &l->apps[i];
		if (a->shelf >= DESK_SHELF_COUNT) {
			continue;
		}
		int r = q[0] ? book_rank(a, q) : 0;
		if (r < 0) {
			continue;
		}
		/* in order as it goes in: best first, then by name */
		int j = n;
		while (j > 0 && (rank[j - 1] > r || (rank[j - 1] == r
			&& book_cmp_name(&l->apps[idx[j - 1]], a) > 0))) {
			idx[j] = idx[j - 1];
			rank[j] = rank[j - 1];
			j--;
		}
		idx[j] = i;
		rank[j] = r;
		n++;
	}
	for (int i = 0; i < n && i < max; i++) {
		out[i] = idx[i];
	}
	return n;
}
