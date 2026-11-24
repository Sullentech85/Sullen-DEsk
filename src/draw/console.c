/* Part of draw.c: the console, as the compositor sees it.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- the console, as the compositor sees it -------------------- */

/* The instruments are printed over the bake, the way the binder labels
 * are: needles move, lamps light and the tube draws, and none of that can
 * be traced once and kept. The lever, the knob's pointer and the fader's
 * handle are drawn here too, because they have to move and a baked one
 * cannot. */

static int console_xform(int screen_w, int screen_h, struct room_render **out_rr,
	double *ox, double *oy, double *scale)
{
	struct room_render *rr = room_for_view(screen_w, screen_h, DESK_VIEW_CONSOLE);
	if (!rr || !rr->console) {
		return 0;
	}
	double sx = (double)screen_w / rr->w, sy = (double)screen_h / rr->h;
	*scale = sx < sy ? sy : sx;
	*ox = (screen_w - rr->w * *scale) * 0.5;
	*oy = (screen_h - rr->h * *scale) * 0.5;
	*out_rr = rr;
	return 1;
}

int desk_console_hit(int screen_w, int screen_h, double px, double py)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!console_xform(screen_w, screen_h, &rr, &ox, &oy, &scale) || !rr->console_ids) {
		return -1;
	}
	int sx = (int)((px - ox) / scale + 0.5);
	int sy = (int)((py - oy) / scale + 0.5);
	if (sx < 0 || sy < 0 || sx >= rr->w || sy >= rr->h) {
		return -1;
	}
	int id = rr->console_ids[(size_t)sy * rr->w + sx];
	return id > 0 && id <= CC_COUNT ? id - 1 : -1;
}

/* Is a sheet pixel under where the lid lies shut? (its box in the sheet, as cut by its ids) */
static bool cc_lid_footprint(struct room_render *rr, int x, int y);
static bool cc_lid_points(const struct room_cam *cam, double open, double n, double aspect,
	double ox, double oy, double sw, double sh, double x[4], double y[4]);

int desk_console_hit_lid(int screen_w, int screen_h, double px, double py, double open)
{
	if (open <= 0.0005) {
		return desk_console_hit(screen_w, screen_h, px, py);
	}
	struct room_render *rr;
	double ox, oy, scale;
	if (!console_xform(screen_w, screen_h, &rr, &ox, &oy, &scale) || !rr->console_ids) {
		return -1;
	}
	int sx = (int)((px - ox) / scale + 0.5);
	int sy = (int)((py - oy) / scale + 0.5);
	if (sx < 0 || sy < 0 || sx >= rr->w || sy >= rr->h) {
		return -1;
	}
	bool under = cc_lid_footprint(rr, sx, sy);
	if (under && open < 0.999) {
		return -1;   /* the lid on its way over it */
	}
	cairo_surface_t *bare;
	const unsigned char *bare_ids;
	const unsigned char *ids = under && room_bare_get(rr, &bare, &bare_ids) ? bare_ids
		: rr->console_ids;
	int id = ids[(size_t)sy * rr->w + sx];
	if (id == CC_LID + 1 || (id > 0 && id <= CC_COUNT && CC[id - 1].on == CC_ON_LID)) {
		return -1;   /* (up, the lid is not where it lay) */
	}
	return id > 0 && id <= CC_COUNT ? id - 1 : -1;
}

bool desk_console_lid_ready(int screen_w, int screen_h)
{
	struct room_render *rr;
	double ox, oy, scale;
	cairo_surface_t *bare;
	const unsigned char *ids;
	return console_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)
		&& room_bare_get(rr, &bare, &ids);
}

/* The names the compositor uses have to be the ones the renderer uses. */
_Static_assert((int)DESK_CC_COUNT == (int)CC_COUNT
	&& (int)DESK_CC_WIFI == (int)CC_WIFI
	&& (int)DESK_CC_BT == (int)CC_BT
	&& (int)DESK_CC_VOL == (int)CC_VOL
	&& (int)DESK_CC_BRIGHT == (int)CC_BRIGHT
	&& (int)DESK_CC_PW_KEY == (int)CC_PW_KEY
	&& (int)DESK_CC_SCREEN == (int)CC_SCREEN
	&& (int)DESK_CC_LOCK == (int)CC_LOCK
	&& (int)DESK_CC_CRT == (int)CC_CRT && (int)DESK_CC_BATT == (int)CC_BATT
	&& (int)DESK_CC_NET == (int)CC_NET && (int)DESK_CC_PW_OFF == (int)CC_PW_OFF
	&& (int)DESK_CC_PW_BOOT == (int)CC_PW_BOOT && (int)DESK_CC_PW_OUT == (int)CC_PW_OUT
	&& (int)DESK_CC_SLEEP == (int)CC_SLEEP && (int)DESK_CC_PW_SUSPEND == (int)CC_PW_SUSPEND
	&& (int)DESK_CC_AIR == (int)CC_AIR && (int)DESK_CC_SAVE == (int)CC_SAVE
	&& (int)DESK_CC_NIGHT == (int)CC_NIGHT && (int)DESK_CC_DISPLAY == (int)CC_DISPLAY
	&& (int)DESK_CC_LID == (int)CC_LID,
	"console controls out of step");

/* Inside a convex quad, testing every edge for the same turn. The corners
 * come out of cc_quad in binary order, so they go round as 0,1,3,2. */
static bool cc_in_quad(const double qx[4], const double qy[4], double x, double y)
{
	static const int o[4] = { 0, 1, 3, 2 };
	int sign = 0;
	for (int k = 0; k < 4; k++) {
		int a = o[k], b = o[(k + 1) & 3];
		double c = (qx[b] - qx[a]) * (y - qy[a]) - (qy[b] - qy[a]) * (x - qx[a]);
		int s = c > 0.0 ? 1 : (c < 0.0 ? -1 : 0);
		if (!s) {
			continue;
		}
		if (!sign) {
			sign = s;
		} else if (sign != s) {
			return false;
		}
	}
	return true;
}

/* Is the pointer on the glass? It is not in the traced sheet -- it swings,
 * so it is drawn rather than baked -- and clicking a sheet of glass is the
 * whole of how you get it out of the way. */
bool desk_console_guard_hit(int screen_w, int screen_h, double px, double py,
	double open)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!console_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	/* its top, and its free edge's thickness -- up, the edge is most of what shows */
	double tx[4], ty[4], bx[4], by[4];
	if (!cc_lid_points(&rr->console_cam, open, CC_LID_N1, aspect, ox, oy, rr->w * scale,
		rr->h * scale, tx, ty) || !cc_lid_points(&rr->console_cam, open, CC_LID_N0, aspect, ox,
		oy, rr->w * scale, rr->h * scale, bx, by)) {
		return false;
	}
	/* past upright, the underside is what faces this way */
	double ex[4] = { tx[2], tx[3], bx[2], bx[3] }, ey[4] = { ty[2], ty[3], by[2], by[3] };
	return cc_in_quad(tx, ty, px, py) || cc_in_quad(bx, by, px, py)
		|| cc_in_quad(ex, ey, px, py);
}

/* A point down the middle of the lid's top on the screen, opened that far (0 shut, 1 all the way):
 * along 0 is its end at the hinge, 1 the middle of its free edge. What a finger lifting the lid
 * holds on to (2026-11-12, by its edge; 2026-11-20, wherever the finger was put down on it). */
bool desk_console_lid_point(int screen_w, int screen_h, double open, double along, double *x,
	double *y)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!console_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	vec3 q[4];
	cc_lid_corners(open, CC_LID_N1, 1.0, aspect, q);
	vec3 hinge = vmul(vadd(q[0], q[1]), 0.5), edge = vmul(vadd(q[2], q[3]), 0.5);
	double u, v;
	if (!room_project(&rr->console_cam, vmix(hinge, edge, along), &u, &v)) {
		return false;
	}
	*x = ox + u * rr->w * scale;
	*y = oy + v * rr->h * scale;
	return true;
}

/* The machine as screen quads through whichever camera: the operating
 * panel, the instrument tower, and the nameplate ledge across the front --
 * which is the only one of the three you can see at all when it is stowed,
 * and so the only one worth clicking. Returns how many came out in front
 * of the lens. */
enum { CC_QUADS = 3 };
static int cc_desk_quads(struct room_render *rr, const struct room_cam *cam,
	double ox, double oy, double sw, double sh, double slide,
	double qx[CC_QUADS][4], double qy[CC_QUADS][4])
{
	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	struct cc_face f[2];
	cc_faces(&f[0], &f[1]);
	double hx = (RM_CC_X1 - RM_CC_X0) * 0.5 - RM_CC_CHEEK * 0.5;
	vec3 corner[CC_QUADS][4];
	for (int i = 0; i < 2; i++) {
		for (int k = 0; k < 4; k++) {
			corner[i][k] = cc_world(&f[i], (k & 1) ? hx : -hx, 0.05,
				(k & 2) ? -f[i].half : f[i].half, slide, aspect);
		}
	}
	cc_plate_quad(slide, aspect, corner[2]);

	int n = 0;
	for (int i = 0; i < CC_QUADS; i++) {
		bool ok = true;
		for (int k = 0; k < 4; k++) {
			double u, v;
			if (!room_project(cam, corner[i][k], &u, &v)) {
				ok = false;
				break;
			}
			qx[i][k] = ox + u * sw;
			qy[i][k] = oy + v * sh;
		}
		if (ok) {
			n++;
		} else {
			for (int k = 0; k < 4; k++) {
				qx[i][k] = qy[i][k] = 0.0;
			}
		}
	}
	return n;
}

/* Where the whole case lands on screen, as a box. */
static void cc_screen_box(struct room_render *rr, const struct room_cam *cam,
	double ox, double oy, double sw, double sh,
	double *x0, double *y0, double *x1, double *y1)
{
	double qx[CC_QUADS][4], qy[CC_QUADS][4];
	*x0 = *y0 = 1e9;
	*x1 = *y1 = -1e9;
	if (!cc_desk_quads(rr, cam, ox, oy, sw, sh, 1.0, qx, qy)) {
		*x0 = *y0 = 0.0;
		*x1 = sw;
		*y1 = sh;
		return;
	}
	for (int i = 0; i < CC_QUADS; i++) {
		for (int k = 0; k < 4; k++) {
			*x0 = fmin(*x0, qx[i][k]);
			*y0 = fmin(*y0, qy[i][k]);
			*x1 = fmax(*x1, qx[i][k]);
			*y1 = fmax(*y1, qy[i][k]);
		}
	}
	/* the cheeks, the plinth and every lamp's bloom stand outside the faces */
	double pad = fmax(*x1 - *x0, *y1 - *y0) * 0.10;
	*x0 -= pad;
	*y0 -= pad;
	*x1 += pad;
	*y1 += pad;
}

bool desk_console_desk_hit(int screen_w, int screen_h, double px, double py,
	double drawer_open)
{
	struct room_xf xf;
	if (!room_xform(screen_w, screen_h, drawer_open, &xf)) {
		return false;
	}
	double qx[CC_QUADS][4], qy[CC_QUADS][4];
	/* stowed: this is the desk view, and on the desk it is always stowed */
	if (!cc_desk_quads(xf.rr, &xf.rr->cam, xf.ox, xf.oy,
		xf.rr->w * xf.scale, xf.rr->h * xf.scale, 0.0, qx, qy)) {
		return false;
	}
	for (int i = 0; i < CC_QUADS; i++) {
		if (cc_in_quad(qx[i], qy[i], px, py)) {
			return true;
		}
	}
	return false;
}

/* Is the pointer on the machine at all, as you are looking at it? Reaching
 * for a knob and missing it by four pixels used to put you back at the
 * desk; the only thing that sends you back is clicking off the machine. */
/* The machines on the desk are in the foreground of the bookcase view --
 * they are built into the back edge and that edge is what you are looking
 * over. So they are reachable from up there too: click one and you come
 * back down to the desk and turn to it. */
bool desk_console_shelf_hit(int screen_w, int screen_h, double px, double py)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!shelf_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	double qx[CC_QUADS][4], qy[CC_QUADS][4];
	if (!cc_desk_quads(rr, &rr->shelf_cam, ox, oy,
		rr->w * scale, rr->h * scale, 0.0, qx, qy)) {
		return false;
	}
	for (int i = 0; i < CC_QUADS; i++) {
		if (cc_in_quad(qx[i], qy[i], px, py)) {
			return true;
		}
	}
	return false;
}

bool desk_console_view_hit(int screen_w, int screen_h, double px, double py)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!console_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	double x0, y0, x1, y1;
	cc_screen_box(rr, &rr->console_cam, ox, oy,
		rr->w * scale, rr->h * scale, &x0, &y0, &x1, &y1);
	return px >= x0 && px <= x1 && py >= y0 && py <= y1;
}

/* Where the hand on a control has put it. Only the two you take hold of --
 * the knob you turn and the fader you push -- have a position to read; a
 * lever is thrown, not aimed. */
bool desk_console_value_at(int screen_w, int screen_h, int idx,
	double px, double py, double *out)
{
	if (idx != CC_VOL && idx != CC_BRIGHT && idx != CC_SCREEN) {
		return false;
	}
	struct room_render *rr;
	double ox, oy, scale;
	if (!console_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	double fw, fh;
	cc_face_size(idx, &fw, &fh);
	/* where the hand is, on the top of the bakelite or of the fader's plate: the face is
	 * pasted onto those (cc_paste) */
	struct cc_deck d[CC_DECKS];
	double ground = 0.0;
	if (cc_decks(idx, d, &ground) < 1) {
		return false;
	}
	double lx, ly;
	if (!cc_face_point(&rr->console_cam, aspect, ox, oy, rr->w * scale, rr->h * scale, idx,
		fw, fh, d[0].top, px, py, &lx, &ly)) {
		return false;
	}
	if (idx == CC_BRIGHT || idx == CC_SCREEN) {
		/* the slot is as long as the trace cut it, not some fraction of
		 * the printed face */
		double sx = fw / (2.0 * CC[idx].fu);
		double run = CC[idx].hu * 0.82 * sx;
		*out = clampf((lx - (fw * 0.5 - run)) / (2.0 * run), 0.0, 1.0);
		return true;
	}
	/* the knob: wherever the hand is round it is where the pointer goes,
	 * over the same sweep the engraved scale covers */
	double a = atan2(ly - fh * 0.5, lx - fw * 0.5);
	if (a > 0.80) {
		a -= 2.0 * M_PI;
	}
	*out = clampf((a + M_PI * 0.5 + 2.30) / 4.60, 0.0, 1.0);
	return true;
}

/* A lamp: a bloom over its lens, which is where the bake put it. */
static void cc_lamp(cairo_t *cr, double x, double y, double r,
	double lr, double lg, double lb, double on)
{
	if (on <= 0.004) {
		return;
	}
	cairo_pattern_t *g = cairo_pattern_create_radial(x, y, r * 0.12, x, y, r * 2.8);
	cairo_pattern_add_color_stop_rgba(g, 0.00, 1.0, 1.0, 0.96, 0.92 * on);
	cairo_pattern_add_color_stop_rgba(g, 0.18, lr, lg, lb, 0.90 * on);
	cairo_pattern_add_color_stop_rgba(g, 0.46, lr, lg, lb, 0.30 * on);
	cairo_pattern_add_color_stop_rgba(g, 1.00, lr, lg, lb, 0.0);
	cairo_set_source(cr, g);
	cairo_arc(cr, x, y, r * 2.8, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

/* A needle on a scale, swinging through a quarter turn. */
static void cc_needle(cairo_t *cr, double cx, double cy, double len,
	double t, double r, double g, double b)
{
	double a = (-0.62 + 1.24 * clampf(t, 0.0, 1.0)) - M_PI * 0.5;
	double tipx = cx + cos(a) * len, tipy = cy + sin(a) * len;
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
	cairo_set_line_width(cr, len * 0.070);
	cairo_move_to(cr, cx + 1.2, cy + 1.6);
	cairo_line_to(cr, tipx + 1.2, tipy + 1.6);
	cairo_stroke(cr);
	cairo_set_source_rgb(cr, r, g, b);
	cairo_set_line_width(cr, len * 0.055);
	cairo_move_to(cr, cx, cy);
	cairo_line_to(cr, tipx, tipy);
	cairo_stroke(cr);
	cairo_set_source_rgb(cr, 0.10, 0.09, 0.08);
	cairo_arc(cr, cx, cy, len * 0.10, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_restore(cr);
}

static void cc_scale_arc(cairo_t *cr, double cx, double cy, double rad, int ticks)
{
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.18, 0.16, 0.13, 0.85);
	for (int i = 0; i <= ticks; i++) {
		double t = (double)i / ticks;
		double a = (-0.62 + 1.24 * t) - M_PI * 0.5;
		double inner = (i % 5 == 0) ? rad * 0.84 : rad * 0.92;
		cairo_set_line_width(cr, (i % 5 == 0) ? 2.0 : 1.0);
		cairo_move_to(cr, cx + cos(a) * inner, cy + sin(a) * inner);
		cairo_line_to(cr, cx + cos(a) * rad, cy + sin(a) * rad);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

/* The faces, each drawn into its own little surface and laid onto the quad
 * the bake left for it. */
/* ---- the tube ---------------------------------------------------
 *
 * What makes a cathode ray tube read as one is not that it is green. It is
 * that the picture is drawn by a beam:
 *
 *   the phosphor is brightest where the beam dwells and dimmest where it is
 *   travelling fast, so a flat part of a trace is bright and a steep part
 *   is thin and faint;
 *   it goes on glowing after the beam has left, so what was drawn a moment
 *   ago is still there, fading;
 *   a hot spot on the phosphor bleeds into its neighbours, so every bright
 *   thing has a halo around it and the core of it goes past green to white;
 *   the raster is combed into scan lines;
 *   and the glass is a bottle, so the whole picture bows outward.
 *
 * None of that is expensive. All of it is the difference between a tube and
 * a green rectangle with a graph in it.
 * ================================================================ */

#define CRT_BOW 0.10

/* The bottle. Everything drawn on the phosphor goes through here first. */
static void crt_warp(double w, double h, double *x, double *y)
{
	double cx = w * 0.5, cy = h * 0.5;
	double nx = (*x - cx) / cx, ny = (*y - cy) / cy;
	double k = 1.0 + CRT_BOW * (nx * nx + ny * ny);
	*x = cx + nx * k * cx;
	*y = cy + ny * k * cy;
}

/* Characters on a phosphor are not printed, they bloom. Four passes at a
 * hair's offset make the halo a blur would, and cairo has no blur. */
static void crt_text(cairo_t *cr, const char *s, double x, double y,
	double size, double lit, double w, double h)
{
	static const double off[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
	crt_warp(w, h, &x, &y);
	for (int i = 0; i < 4; i++) {
		double d = size * 0.10;
		show_text_as(cr, DESK_FONT_SCREEN, s, x + off[i][0] * d, y + off[i][1] * d, size, true,
			0.26, 0.92, 0.34, 0.20 * lit, 0);
	}
	show_text_as(cr, DESK_FONT_SCREEN, s, x, y, size, true, 0.76, 1.0, 0.80, 0.95 * lit, 0);
}

/* What of the tube's face does not move with the clock: the cathode's
 * glow and the graticule, drawn once at full light and laid on at the
 * light there is (the hum and the flicker scale them, nothing else does);
 * the comb of the raster; and the vignette with the room in the glass.
 * Drawn once a size. They were most of what a face cost -- four gradients
 * across the whole of it and a hundred strokes -- and the face is drawn
 * as often as the tube moves. Only the compositor's thread draws it. */
#define CRT_GRAT_A 0.22   /* the graticule at its brightest */
static struct {
	cairo_surface_t *glow, *grat, *comb, *over;
	double w, h;
} g_crt;

static void crt_cache(double w, double h)
{
	if (g_crt.glow && g_crt.w == w && g_crt.h == h) {
		return;
	}
	cairo_surface_t **all[4] = { &g_crt.glow, &g_crt.grat, &g_crt.comb, &g_crt.over };
	for (int i = 0; i < 4; i++) {
		if (*all[i]) {
			cairo_surface_destroy(*all[i]);
		}
		*all[i] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)ceil(w), (int)ceil(h));
	}
	g_crt.w = w;
	g_crt.h = h;
	double cx = w * 0.5, cy = h * 0.5;

	/* the cathode glow: the tube is lit even where nothing is drawn on it */
	cairo_t *c = cairo_create(g_crt.glow);
	cairo_pattern_t *hz = cairo_pattern_create_radial(cx, cy * 1.02, 0, cx, cy, h * 0.64);
	cairo_pattern_add_color_stop_rgba(hz, 0.00, 0.086, 0.300, 0.140, 0.62);
	cairo_pattern_add_color_stop_rgba(hz, 0.55, 0.050, 0.180, 0.082, 0.34);
	cairo_pattern_add_color_stop_rgba(hz, 1.00, 0.020, 0.070, 0.034, 0.0);
	cairo_set_source(c, hz);
	cairo_paint(c);
	cairo_pattern_destroy(hz);
	cairo_destroy(c);

	/* the graticule, etched on the inside of the glass. Cool white rather
	 * than phosphor green: it is scratched into the glass in front of the
	 * picture, not part of it. */
	c = cairo_create(g_crt.grat);
	cairo_set_line_width(c, fmax(0.7, h * 0.0035));
	cairo_set_source_rgba(c, 0.52, 0.74, 0.58, CRT_GRAT_A);
	for (int i = 1; i < 8; i++) {
		for (int axis = 0; axis < 2; axis++) {
			for (int k = 0; k <= 10; k++) {
				double t = (double)k / 10.0;
				double x = axis ? t * w : w * i / 8.0;
				double y = axis ? h * i / 8.0 : t * h;
				crt_warp(w, h, &x, &y);
				if (k) {
					cairo_line_to(c, x, y);
				} else {
					cairo_move_to(c, x, y);
				}
			}
			cairo_stroke(c);
		}
	}
	cairo_destroy(c);

	/* The raster, combed into lines, and every other field a shade darker,
	 * which is what interlace looks like when it is not quite in step.
	 * Coarse enough to survive: the face is printed larger than the quad
	 * it is laid onto, and a comb finer than the scale factor just averages
	 * itself away into a flat wash. Two passes, not eighty strokes: the odd
	 * rows and the even ones. */
	c = cairo_create(g_crt.comb);
	double pitch = fmax(3.0, h * 0.0215);
	cairo_set_line_width(c, pitch * 0.48);
	for (int field = 0; field < 2; field++) {
		int row = 0;
		for (double y = pitch * 0.5; y < h; y += pitch, row++) {
			if ((row & 1) != field) {
				continue;
			}
			cairo_move_to(c, 0, y);
			cairo_line_to(c, w, y);
		}
		cairo_set_source_rgba(c, 0.0, 0.01, 0.0, field ? 0.44 : 0.32);
		cairo_stroke(c);
	}
	cairo_destroy(c);

	/* the phosphor is brightest in the middle, and the tube is round; and
	 * the room, in the curve of the glass */
	c = cairo_create(g_crt.over);
	cairo_pattern_t *vig = cairo_pattern_create_radial(cx, cy, h * 0.08, cx, cy, h * 0.74);
	cairo_pattern_add_color_stop_rgba(vig, 0.0, 0, 0, 0, 0.0);
	cairo_pattern_add_color_stop_rgba(vig, 0.62, 0, 0, 0, 0.22);
	cairo_pattern_add_color_stop_rgba(vig, 1.0, 0, 0, 0, 0.80);
	cairo_set_source(c, vig);
	cairo_paint(c);
	cairo_pattern_destroy(vig);
	cairo_pattern_t *gl = cairo_pattern_create_linear(0, 0, w * 0.62, h * 0.86);
	cairo_pattern_add_color_stop_rgba(gl, 0.00, 1, 1, 1, 0.055);
	cairo_pattern_add_color_stop_rgba(gl, 0.30, 1, 1, 1, 0.014);
	cairo_pattern_add_color_stop_rgba(gl, 0.62, 1, 1, 1, 0.0);
	cairo_set_source(c, gl);
	cairo_paint(c);
	cairo_pattern_destroy(gl);
	cairo_destroy(c);
	for (int i = 0; i < 4; i++) {
		cairo_surface_flush(*all[i]);
	}
}

/* crt_text, made smaller where it would run wider than max_w */
static void crt_text_fit(cairo_t *cr, const char *s, double x, double y,
	double size, double max_w, double lit, double w, double h)
{
	static const double off[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
	crt_warp(w, h, &x, &y);
	for (int i = 0; i < 4; i++) {
		double d = size * 0.10;
		show_text_fit_as(cr, DESK_FONT_SCREEN, s, x + off[i][0] * d, y + off[i][1] * d, size,
			true, 0.26, 0.92, 0.34, 0.20 * lit, 0, max_w);
	}
	show_text_fit_as(cr, DESK_FONT_SCREEN, s, x, y, size, true, 0.76, 1.0, 0.80, 0.95 * lit, 0,
		max_w);
}

/* a screen, drawn by the beam: its edges through the glass's curve, soft and then the core */
static void crt_screen(cairo_t *cr, double x0, double y0, double x1, double y1, double lit,
	double w, double h)
{
	const double X[5] = { x0, x1, x1, x0, x0 }, Y[5] = { y0, y0, y1, y1, y0 };
	for (int pass = 0; pass < 2; pass++) {
		for (int e = 0; e < 4; e++) {
			for (int k = 0; k <= 6; k++) {
				double t = k / 6.0;
				double px = X[e] + (X[e + 1] - X[e]) * t, py = Y[e] + (Y[e + 1] - Y[e]) * t;
				crt_warp(w, h, &px, &py);
				if (e == 0 && k == 0) {
					cairo_move_to(cr, px, py);
				} else {
					cairo_line_to(cr, px, py);
				}
			}
		}
		cairo_close_path(cr);
		cairo_set_line_width(cr, pass ? h * 0.012 : h * 0.040);
		if (pass) {
			cairo_set_source_rgba(cr, 0.82, 1.00, 0.86, 0.90 * lit);
		} else {
			cairo_set_source_rgba(cr, 0.30, 0.95, 0.40, 0.20 * lit);
		}
		cairo_stroke(cr);
	}
}

/* What DISPLAY has just made of the screens, on the tube a moment over the trace: SCREENS, the
 * two drawn as they are used -- side by side, the same on both, the one or the other alone --
 * and the word for it. */
static void crt_screens_word(cairo_t *cr, double w, double h, int said, double word,
	double lit)
{
	static const char *const WORD[] = { "EXTEND", "DUPLICATE", "THIS SCREEN", "OTHER SCREEN",
		"ONE SCREEN", "NOT POSSIBLE" };
	if (word <= 0.0 || said < 0 || said > DESK_SCREENS_REFUSED) {
		return;
	}
	double a = lit * word;
	/* the trace let die down under it */
	cairo_set_source_rgba(cr, 0.010, 0.020, 0.014, 0.86 * word);
	cairo_paint(cr);
	crt_text_fit(cr, TR("SCREENS"), w * 0.50, h * 0.255, h * 0.085, w * 0.62, a * 0.80, w, h);
	double sw = w * 0.20, sh = h * 0.15, cy = h * 0.475;
	switch (said) {
	case DESK_SCREENS_ONE:
		crt_screen(cr, w * 0.5 - sw * 0.5, cy - sh * 0.5, w * 0.5 + sw * 0.5, cy + sh * 0.5,
			a, w, h);
		break;
	case DESK_SCREENS_MIRROR: {
		double g = w * 0.10;
		crt_screen(cr, w * 0.5 - g * 0.5 - sw, cy - sh * 0.5, w * 0.5 - g * 0.5, cy + sh * 0.5,
			a, w, h);
		crt_screen(cr, w * 0.5 + g * 0.5, cy - sh * 0.5, w * 0.5 + g * 0.5 + sw, cy + sh * 0.5,
			a, w, h);
		/* the same on both: an equals sign between them */
		for (int k = 0; k < 2; k++) {
			double y = cy + (k ? 1.0 : -1.0) * sh * 0.16;
			crt_screen(cr, w * 0.5 - g * 0.22, y, w * 0.5 + g * 0.22, y, a * 0.9, w, h);
		}
		break;
	}
	default: {
		/* two side by side: both lit (the desk going on), or one alone */
		bool none = said == DESK_SCREENS_REFUSED;
		double l = said == DESK_SCREENS_OTHER || none ? 0.22 : 1.0;
		double r = said == DESK_SCREENS_THIS || none ? 0.22 : 1.0;
		double g = said == DESK_SCREENS_EXTEND ? 0.0 : w * 0.04;
		crt_screen(cr, w * 0.5 - g * 0.5 - sw, cy - sh * 0.5, w * 0.5 - g * 0.5, cy + sh * 0.5,
			a * l, w, h);
		crt_screen(cr, w * 0.5 + g * 0.5, cy - sh * 0.5, w * 0.5 + g * 0.5 + sw, cy + sh * 0.5,
			a * r, w, h);
		break;
	}
	}
	crt_text_fit(cr, TR(WORD[said]), w * 0.50, h * 0.775, h * 0.120, w * 0.78, a, w, h);
}

static void cc_face_crt(cairo_t *cr, double w, double h,
	const struct desk_meters *m, double warm, double clock,
	const double *trace, int ntrace, int said, double word)
{
	cairo_set_source_rgb(cr, 0.010, 0.020, 0.014);
	cairo_paint(cr);
	if (warm <= 0.02) {
		return;
	}
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);

	/* Mains hum and a gun that was never quite regulated: the whole raster
	 * breathes. Constant brightness is the one thing a tube never had. */
	double fl = 0.93 + 0.045 * sin(clock * 11.3) + 0.025 * sin(clock * 37.1);
	double lit = clampf(warm, 0.0, 1.0) * fl;

	/* the cathode glow and the graticule, as bright as the tube is now */
	crt_cache(w, h);
	cairo_set_source_surface(cr, g_crt.glow, 0, 0);
	cairo_paint_with_alpha(cr, lit);
	cairo_set_source_surface(cr, g_crt.grat, 0, 0);
	cairo_paint_with_alpha(cr, (0.13 + 0.09 * lit) / CRT_GRAT_A);

	/* ---- the trace ----
	 *
	 * The beam sweeps left to right and the left of the screen is what it
	 * drew longest ago, so that is what has faded furthest. */
	double x0 = w * 0.100, x1 = w * 0.900;
	double base = h * 0.760;
	/* it strikes as a flat line and opens out as the gun comes up */
	double amp = h * 0.520 * smoothstepf(0.30, 0.82, warm);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	/* The two soft passes are one path apiece: they are broad enough that
	 * the beam's speed does not show in them, and the fade towards the
	 * older end of the sweep is a gradient along the screen rather than a
	 * stroke per segment. Fifty strokes instead of a hundred and forty. */
	for (int pass = 0; pass < 2; pass++) {
		double lw = pass ? h * 0.032 : h * 0.090;
		double a = pass ? 0.36 : 0.115;
		double r = pass ? 0.36 : 0.20, g = pass ? 1.00 : 0.88;
		double b = pass ? 0.44 : 0.30;
		cairo_set_line_width(cr, lw);
		for (int i = 0; i < ntrace; i++) {
			double t = (double)i / (ntrace - 1);
			double px = x0 + (x1 - x0) * t;
			double py = base - amp * clampf(trace[i], 0.0, 1.0);
			crt_warp(w, h, &px, &py);
			if (i) {
				cairo_line_to(cr, px, py);
			} else {
				cairo_move_to(cr, px, py);
			}
		}
		cairo_pattern_t *fade = cairo_pattern_create_linear(x0, 0, x1, 0);
		cairo_pattern_add_color_stop_rgba(fade, 0.0, r, g, b, a * 0.26 * lit);
		cairo_pattern_add_color_stop_rgba(fade, 0.6, r, g, b, a * 0.56 * lit);
		cairo_pattern_add_color_stop_rgba(fade, 1.0, r, g, b, a * lit);
		cairo_set_source(cr, fade);
		cairo_stroke(cr);
		cairo_pattern_destroy(fade);
	}

	/* The core is drawn a segment at a time, because this is where the
	 * beam's speed lives: a steep flank is the same charge spread over
	 * more phosphor, so it comes out thin and faint, and a flat run comes
	 * out bright. That one thing is most of what says cathode ray. */
	cairo_set_line_width(cr, h * 0.0115);
	for (int i = 1; i < ntrace; i++) {
		double t0 = (double)(i - 1) / (ntrace - 1);
		double t1 = (double)i / (ntrace - 1);
		double ax = x0 + (x1 - x0) * t0;
		double ay = base - amp * clampf(trace[i - 1], 0.0, 1.0);
		double bx = x0 + (x1 - x0) * t1;
		double by = base - amp * clampf(trace[i], 0.0, 1.0);
		crt_warp(w, h, &ax, &ay);
		crt_warp(w, h, &bx, &by);
		double age = 0.26 + 0.74 * t1 * t1;
		double dy = fabs(by - ay) / fmax(bx - ax, 1e-6);
		double dwell = 1.0 / sqrt(1.0 + dy * dy * 2.6);
		cairo_set_source_rgba(cr, 0.82, 1.00, 0.86,
			0.96 * age * (0.34 + 0.66 * dwell) * lit);
		cairo_move_to(cr, ax, ay);
		cairo_line_to(cr, bx, by);
		cairo_stroke(cr);
	}

	/* where the beam is now */
	{
		double hx = x1, hy = base - amp * clampf(trace[ntrace - 1], 0.0, 1.0);
		crt_warp(w, h, &hx, &hy);
		double rr = h * 0.105;
		cairo_pattern_t *sp = cairo_pattern_create_radial(hx, hy, 0, hx, hy, rr);
		cairo_pattern_add_color_stop_rgba(sp, 0.00, 0.94, 1.00, 0.94, 0.95 * lit);
		cairo_pattern_add_color_stop_rgba(sp, 0.22, 0.46, 1.00, 0.52, 0.42 * lit);
		cairo_pattern_add_color_stop_rgba(sp, 1.00, 0.20, 0.86, 0.28, 0.0);
		cairo_set_source(cr, sp);
		cairo_arc(cr, hx, hy, rr, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_pattern_destroy(sp);
	}

	/* ---- what it is a trace of ---- */
	char buf[64];
	snprintf(buf, sizeof buf, "CPU %2d%%", (int)(m->cpu * 100.0 + 0.5));
	crt_text(cr, buf, w * 0.50, h * 0.232, h * 0.115, lit, w, h);
	snprintf(buf, sizeof buf, "MEM %2d%%", (int)(m->mem * 100.0 + 0.5));
	crt_text(cr, buf, w * 0.335, h * 0.845, h * 0.090, lit * 0.88, w, h);
	if (m->temp_c > -100.0) {
		snprintf(buf, sizeof buf, "%2d\xc2\xb0""C", (int)(m->temp_c + 0.5));
	} else {
		snprintf(buf, sizeof buf, "-- \xc2\xb0""C");
	}
	crt_text(cr, buf, w * 0.672, h * 0.845, h * 0.090, lit * 0.88, w, h);

	/* and over it a moment, what DISPLAY has made of the screens */
	crt_screens_word(cr, w, h, said, word, lit);

	/* the raster */
	cairo_set_source_surface(cr, g_crt.comb, 0, 0);
	cairo_paint(cr);

	/* the hum bar, drifting up the screen the way an unsynced one does */
	double hb = fmod(clock * 0.13, 1.6) - 0.30;
	cairo_pattern_t *bar = cairo_pattern_create_linear(0, (hb - 0.18) * h,
		0, (hb + 0.18) * h);
	cairo_pattern_add_color_stop_rgba(bar, 0.0, 0.70, 1.0, 0.76, 0.0);
	cairo_pattern_add_color_stop_rgba(bar, 0.5, 0.70, 1.0, 0.76, 0.030 * lit);
	cairo_pattern_add_color_stop_rgba(bar, 1.0, 0.70, 1.0, 0.76, 0.0);
	cairo_set_source(cr, bar);
	cairo_paint(cr);
	cairo_pattern_destroy(bar);

	/* the vignette, and the room in the curve of the glass */
	cairo_set_source_surface(cr, g_crt.over, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* The printed card behind a meter, which is the same card every frame:
 * the paper, its age, the scale and the legend. Generating it was most of
 * what a meter cost to draw -- seven radial gradients of foxing and yellow
 * across the whole face, twelve times a second, for a needle. So it is
 * baked once per size and legend, the way a game bakes a texture, and
 * only what moves is drawn over it. The layers go on in the same order as
 * ever, so the face comes out the same to the pixel. */
typedef void (*cc_card_fn)(cairo_t *cr, double w, double h, const void *arg);

static void cc_card(cairo_t *cr, int slot, double w, double h, uint64_t sig,
	cc_card_fn fn, const void *arg)
{
	static struct {
		cairo_surface_t *s;
		double w, h;
		uint64_t sig;
		int lang;           /* the words on it are the language's */
	} cache[4];
	int lang = (int)desk_lang_get();
	if (!cache[slot].s || cache[slot].w != w || cache[slot].h != h
		|| cache[slot].sig != sig || cache[slot].lang != lang) {
		if (cache[slot].s) {
			cairo_surface_destroy(cache[slot].s);
		}
		cairo_surface_t *c = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
			(int)w, (int)h);
		cairo_t *cc = cairo_create(c);
		fn(cc, w, h, arg);
		cairo_destroy(cc);
		cairo_surface_flush(c);
		cache[slot].s = c;
		cache[slot].w = w;
		cache[slot].h = h;
		cache[slot].sig = sig;
		cache[slot].lang = lang;
	}
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, cache[slot].s, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
}

static void cc_battery_card(cairo_t *cr, double w, double h, const void *arg)
{
	const struct desk_meters *m = arg;
	/* The card behind a meter is printed card, and it has been behind that
	 * glass since the set was built. */
	cairo_set_source_rgb(cr, 0.852, 0.818, 0.732);
	cairo_paint(cr);
	paper_age(cr, w, h, 401, 0.62);
	double cx = w * 0.5, cy = h * 1.06, rad = h * 0.78;
	cc_scale_arc(cr, cx, cy, rad, 20);
	show_text_as(cr, DESK_FONT_ENGRAVE, TR("BATTERY"), w * 0.5, h * 0.30, h * 0.16, true,
		0.20, 0.17, 0.13, 0.9, 0);
	if (m->have_battery) {
		show_text_as(cr, DESK_FONT_ENGRAVE, "0", w * 0.16, h * 0.62, h * 0.12, false, 0.24,
			0.20, 0.16, 0.8, 0);
		show_text_as(cr, DESK_FONT_ENGRAVE, TR("FULL"), w * 0.86, h * 0.62, h * 0.12, false,
			0.24, 0.20, 0.16, 0.8, 0);
	} else {
		/* there is no cell in this machine, and a meter that reads a number
		 * nobody measured is worse than one that reads LINE */
		show_text_as(cr, DESK_FONT_ENGRAVE, TR("LINE"), w * 0.5, h * 0.66, h * 0.20, true,
			0.62, 0.16, 0.12, 0.92, 0);
	}
}

static void cc_face_battery(cairo_t *cr, double w, double h,
	const struct desk_meters *m, double warm)
{
	cc_card(cr, 0, w, h, m->have_battery ? 1u : 2u, cc_battery_card, m);
	double cx = w * 0.5, cy = h * 1.06, rad = h * 0.78;
	if (m->have_battery) {
		cc_needle(cr, cx, cy, rad * 0.86, m->battery * warm, 0.16, 0.14, 0.12);
	} else {
		cc_needle(cr, cx, cy, rad * 0.86, warm, 0.62, 0.16, 0.12);
	}
	ink_skip(cr, w * 0.10, h * 0.14, w * 0.80, h * 0.24, 401, 0.34,
		0.852, 0.818, 0.732);
	overlay_grain(cr, 0, 0, w, h, 0.8);
}

static void cc_net_card(cairo_t *cr, double w, double h, const void *arg)
{
	const struct desk_meters *m = arg;
	cairo_set_source_rgb(cr, 0.846, 0.812, 0.726);
	cairo_paint(cr);
	paper_age(cr, w, h, 733, 0.58);
	double cx = w * 0.5, cy = h * 1.00, rad = h * 0.70;
	cc_scale_arc(cr, cx, cy, rad, 20);
	show_text_fit_as(cr, DESK_FONT_ENGRAVE, m->net_if[0] ? m->net_if : TR("NO LINE"),
		w * 0.5, h * 0.26,
		h * 0.150, true, 0.20, 0.17, 0.13, 0.9, 0, w * 0.82);
}

static void cc_face_net(cairo_t *cr, double w, double h,
	const struct desk_meters *m, double warm)
{
	uint64_t sig = 1469598103934665603ull;
	for (const char *c = m->net_if; *c && c < m->net_if + sizeof m->net_if; c++) {
		sig = (sig ^ (unsigned char)*c) * 1099511628211ull;
	}
	cc_card(cr, 1, w, h, sig, cc_net_card, m);
	double cx = w * 0.5, cy = h * 1.00, rad = h * 0.70;
	cc_needle(cr, cx, cy, rad * 0.86, m->net_rate * warm, 0.16, 0.14, 0.12);

	/* the two lamps a set like this always has */
	double ly = h * 0.52;
	bool up = m->net_up && m->net_routed;
	cairo_set_source_rgba(cr, 0.10, 0.09, 0.08, 0.55);
	cairo_arc(cr, w * 0.20, ly, h * 0.085, 0, 2 * M_PI);
	cairo_arc(cr, w * 0.80, ly, h * 0.085, 0, 2 * M_PI);
	cairo_fill(cr);
	cc_lamp(cr, w * 0.20, ly, h * 0.075, 0.26, 1.0, 0.36, up ? warm : 0.0);
	cc_lamp(cr, w * 0.80, ly, h * 0.075, 1.0, 0.22, 0.16, up ? 0.0 : warm);
	ink_skip(cr, w * 0.10, h * 0.12, w * 0.80, h * 0.22, 733, 0.30,
		0.846, 0.812, 0.726);
	overlay_grain(cr, 0, 0, w, h, 0.8);
}

/* The knob's dial: the pointer turns, the flutes underneath do not, which
 * at sixteen flutes is not a thing anybody can see. */
/* A legend stamped under a control, the way every panel of this kind names
 * what it does. Drawn at a world offset, so it lands under the hardware
 * rather than under whatever fraction of the face it used to be. */
static void cc_legend(cairo_t *cr, double w, double h, double sy, double below,
	const char *s, double warm)
{
	show_text_as(cr, DESK_FONT_ENGRAVE, s, w * 0.5, h * 0.5 + below * sy, 0.155 * sy, true,
		0.84, 0.79, 0.66, 0.30 + 0.48 * warm, 0);
}

/* The knob: fluted bakelite 0.68 across on a brass skirt 0.88 across. Both
 * numbers come out of the trace, so the pointer sits on the bakelite and
 * the scale on the brass instead of somewhere near them. */
static void cc_face_knob(cairo_t *cr, double w, double h, double sx, double sy,
	double v, double warm)
{
	double cx = w * 0.5, cy = h * 0.5;
	double rad = 0.500 * sx;          /* the bakelite, as the trace cut it */
	double val = clampf(v, 0.0, 1.0);
	cairo_save(cr);

	/* ---- the scale, engraved into the brass skirt ----
	 * Eleven divisions, numbered at every other one. A knob with a scale
	 * on it is a knob you can come back to; one with only a pointer is a
	 * knob you have to listen to. */
	cairo_set_source_rgba(cr, 0.13, 0.09, 0.04, 0.85);
	for (int i = 0; i <= 10; i++) {
		double t = (double)i / 10.0;
		double a = (-2.30 + 4.60 * t) - M_PI * 0.5;
		double inner = (i % 2 == 0) ? 0.645 * sx : 0.672 * sx;
		cairo_set_line_width(cr, (i % 2 == 0) ? 0.026 * sx : 0.014 * sx);
		cairo_move_to(cr, cx + cos(a) * inner, cy + sin(a) * inner);
		cairo_line_to(cr, cx + cos(a) * 0.722 * sx, cy + sin(a) * 0.722 * sx);
		cairo_stroke(cr);
	}
	for (int i = 0; i <= 5; i++) {
		double t = (double)i / 5.0;
		double a = (-2.30 + 4.60 * t) - M_PI * 0.5;
		double nx = cx + cos(a) * 0.808 * sx;
		/* show_text sets a baseline, and a number on a dial wants its
		 * middle on the radius */
		double ny = cy + sin(a) * 0.808 * sx + 0.046 * sx;
		char lab[4];
		snprintf(lab, sizeof lab, "%d", i * 2);
		/* cut in, so the lamp catches the lower lip of the cut */
		show_text_as(cr, DESK_FONT_ENGRAVE, lab, nx + 0.009 * sx, ny + 0.011 * sx,
			0.126 * sx, true,
			0.90, 0.82, 0.60, 0.34 * (0.35 + 0.65 * warm), 0);
		show_text_as(cr, DESK_FONT_ENGRAVE, lab, nx, ny, 0.126 * sx, true, 0.10, 0.068,
			0.028, 0.90, 0);
	}

	/* ---- where it is set ----
	 * The moulded pointer says which way the knob is turned; the red index
	 * riding just off its rim says what that comes to on the scale. They
	 * are the same angle, so together they read as one mark that starts on
	 * the bakelite and ends in an arrowhead at the numbers. */
	double a = (-2.30 + 4.60 * val) - M_PI * 0.5;
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, a);            /* +x now points at the value */
	double r0 = 0.520 * sx, r1 = 0.640 * sx, hw = 0.068 * sx;
	cairo_move_to(cr, r1 + 0.013 * sx, 0.015 * sx);
	cairo_line_to(cr, r0 + 0.013 * sx, -hw + 0.015 * sx);
	cairo_line_to(cr, r0 + 0.013 * sx, hw + 0.015 * sx);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.08, 0.016, 0.010, 0.48);
	cairo_fill(cr);

	cairo_move_to(cr, r1, 0);
	cairo_line_to(cr, r0, -hw);
	cairo_line_to(cr, r0, hw);
	cairo_close_path(cr);
	cairo_pattern_t *rd = cairo_pattern_create_linear(r0, -hw, r0, hw);
	cairo_pattern_add_color_stop_rgb(rd, 0.00, 0.930, 0.286, 0.180);
	cairo_pattern_add_color_stop_rgb(rd, 0.45, 0.760, 0.098, 0.062);
	cairo_pattern_add_color_stop_rgb(rd, 1.00, 0.512, 0.046, 0.032);
	cairo_set_source(cr, rd);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(rd);
	cairo_set_source_rgba(cr, 0.14, 0.02, 0.01, 0.55);
	cairo_set_line_width(cr, 0.010 * sx);
	cairo_stroke(cr);
	cairo_restore(cr);

	/* the pointer, cut into the bakelite and catching the light */
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	/* minus, not plus: with the other sign the pointer read the scale
	 * backwards -- turned up, it pointed at zero */
	cairo_rotate(cr, a - M_PI * 0.5);
	cairo_set_source_rgba(cr, 0.02, 0.02, 0.02, 0.85);
	cairo_rectangle(cr, -rad * 0.105, rad * 0.06, rad * 0.21, rad * 0.86);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.96, 0.91, 0.74, 0.94 * (0.35 + 0.65 * warm));
	cairo_rectangle(cr, -rad * 0.080, rad * 0.09, rad * 0.16, rad * 0.80);
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_restore(cr);
	cc_legend(cr, w, h, sy, 1.04, TR("VOLUME"), warm);
}

/* The lever, drawn over the hinge boss at the centre of its plate so it can
 * fall. hu and hs are the control's own reach, in world units. */
static void cc_face_switch(cairo_t *cr, double w, double h, double sx, double sy,
	double hu, double hs, bool on, bool toggle,
	double lamp_on, double lr, double lg, double lb, const char *legend)
{
	double cx = w * 0.5, cy = h * 0.5;
	(void)toggle;
	cairo_save(cr);
	/* The words it falls between go under the pivot, not over it: up there
	 * they were behind the lever when it was thrown, which is the one
	 * place a legend must never be. */
	show_text_as(cr, DESK_FONT_ENGRAVE, TR("ON"), cx - hu * 0.58 * sx, cy + hs * 0.46 * sy,
		0.145 * sy, true,
		0.88, 0.84, 0.72, on ? 0.95 : 0.40, 0);
	show_text_as(cr, DESK_FONT_ENGRAVE, TR("OFF"), cx + hu * 0.58 * sx, cy + hs * 0.46 * sy,
		0.145 * sy, true,
		0.88, 0.84, 0.72, on ? 0.40 : 0.95, 0);

	/* (the lever stands out of the boss, and is drawn as a thing in the
	 * room over this face: cc_lever3) */
	cairo_restore(cr);

	/* the pilot lamp, where the trace put its lens */
	cc_lamp(cr, cx, cy - hs * 0.86 * sy, 0.155 * sy, lr, lg, lb, lamp_on);
	cc_legend(cr, w, h, sy, hs * 0.82 + 0.20, legend, lamp_on * 0.5 + 0.5);
}

/* A fader: a milled handle in a slot 0.82 of the plate long and 0.11
 * high, which is what the trace cut into the brass. One with nothing to
 * turn (ok false: the screen's, on a machine with no backlight) has no
 * scale, and says so where the scale would be. */
static void cc_face_fader(cairo_t *cr, double w, double h, double sx, double sy,
	double hu, double v, double warm, const char *legend, bool ok)
{
	double cx = w * 0.5, cy = h * 0.5;
	double run = hu * 0.82 * sx;
	cairo_save(cr);
	if (ok) {
		/* the scale above the slot */
		cairo_set_source_rgba(cr, 0.11, 0.08, 0.03, 0.78);
		for (int i = 0; i <= 10; i++) {
			double x = cx - run + 2.0 * run * i / 10.0;
			double len = (i % 5 == 0) ? 0.15 * sy : 0.09 * sy;
			cairo_set_line_width(cr, (i % 5 == 0) ? 0.024 * sx : 0.013 * sx);
			cairo_move_to(cr, x, cy - 0.27 * sy);
			cairo_line_to(cr, x, cy - 0.27 * sy + len);
			cairo_stroke(cr);
		}
	} else {
		/* as long as the scale would have been, at most, and standing where its ticks would
		 * have (higher, and up on the tower it ran off the top of the plate) */
		show_text_fit_as(cr, DESK_FONT_ENGRAVE, TR("NO BACKLIGHT"), cx, cy - 0.12 * sy,
			0.105 * sy, true, 0.13, 0.09, 0.04, 0.70, 0, 2.0 * run);
	}
	/* (the handle stands up out of the slot: cc_fader3) */
	(void)v;
	cairo_restore(cr);
	cc_legend(cr, w, h, sy, 0.47, legend, ok ? warm : warm * 0.55);
}

/* The tube's trace: a short history of the processor, kept here because
 * nothing else has a reason to remember it. */
#define CC_TRACE 48
static double g_cc_trace[CC_TRACE];
static double g_cc_trace_at;

static void cc_trace_step(double cpu, double now)
{
	if (now - g_cc_trace_at < 0.09) {
		return;
	}
	g_cc_trace_at = now;
	for (int i = 0; i < CC_TRACE - 1; i++) {
		g_cc_trace[i] = g_cc_trace[i + 1];
	}
	/* a little noise on the beam, because a tube is not a spreadsheet */
	g_cc_trace[CC_TRACE - 1] = clampf(cpu + (hash21(now * 31.7, 5.5) - 0.5) * 0.020,
		0.0, 1.0);
}

/* The set is two things: a sheet that was traced once and never changes,
 * and the instruments, which change every frame there is. They are drawn
 * apart so the compositor can hang the sheet on a node of its own and stop
 * copying a screenful of baked steel twelve times a second. */

/* ---- the guarded set ---------------------------------------------
 *
 * Three buttons that end the session, a lock that lets you at them, and a
 * sheet of glass over the lot.
 *
 * The brass, the caps and the barrel are traced with the rest of the
 * machine. What is here is everything that moves or lights: the word cut
 * round each bezel, the filament behind each cap, the key, and the glass
 * -- which cannot be baked, because a baked one is shut for ever. */

/* A word cut round the bottom of a bezel. Set letter by letter along the
 * arc, each one turned to stand on it, because a straight line of text
 * across a ring reads as a sticker rather than as engraving. */
static void arc_text(cairo_t *fc, const char *text, double cx, double cy,
	double r, double size, double r_, double g_, double b_, double a)
{
	if (!text[0]) {
		return;
	}
	cairo_save(fc);
	desk_font_select(fc, DESK_FONT_ENGRAVE, text, true, false);
	cairo_set_font_size(fc, size);
	/* how much of the circle the whole word takes, at this radius -- a
	 * letter at a time, and a whole one (a Japanese letter is three bytes,
	 * and a third of one is nothing cairo will draw) */
	double total = 0.0;
	char c[8];
	for (const char *p = text; *p;) {
		p += cal_letter(p, c);
		cairo_text_extents_t e;
		cairo_text_extents(fc, c, &e);
		total += e.x_advance;
	}
	/* Down the angles, not up them: y runs down the face, so going the
	 * other way sets the word right to left and it came out mirrored. */
	double span = total / fmax(r, 1.0);
	double ang = M_PI * 0.5 + span * 0.5;
	cairo_set_source_rgba(fc, r_, g_, b_, a);
	for (const char *p = text; *p;) {
		p += cal_letter(p, c);
		cairo_text_extents_t e;
		cairo_text_extents(fc, c, &e);
		double step = e.x_advance / fmax(r, 1.0);
		double a0 = ang - step * 0.5;
		cairo_save(fc);
		cairo_translate(fc, cx + cos(a0) * r, cy + sin(a0) * r);
		cairo_rotate(fc, a0 - M_PI * 0.5);
		cairo_move_to(fc, -e.x_advance * 0.5 - e.x_bearing, 0.0);
		cairo_show_text(fc, c);
		cairo_restore(fc);
		ang -= step;
	}
	cairo_restore(fc);
}

/* A button's face. The cap is already there in the bake, cold; this is the
 * word round it and the filament behind it. Its ring and cap, in the
 * room's units. */
/* What a button does, as a sign etched into its cap (2026-11-10, the owner: lightly): a
 * path in a box two across, about (x, y), r to half of it. */
enum {
	CC_ICON_NONE = 0, CC_ICON_POWER, CC_ICON_RESTART, CC_ICON_LOGOUT, CC_ICON_SUSPEND,
	CC_ICON_LOCK, CC_ICON_SLEEP, CC_ICON_AIR, CC_ICON_SAVE, CC_ICON_NIGHT, CC_ICON_DISPLAY,
};

static void cc_icon_path(cairo_t *cr, int icon, double x, double y, double r)
{
#define IX(a) (x + (a) * r)
#define IY(b) (y + (b) * r)
	switch (icon) {
	case CC_ICON_POWER:
		/* the ring broken at the top, and the stroke down into it */
		cairo_new_sub_path(cr);
		cairo_arc(cr, x, y + 0.06 * r, 0.78 * r, -M_PI * 0.5 + 0.62, M_PI * 1.5 - 0.62);
		cairo_move_to(cr, IX(0.0), IY(-0.98));
		cairo_line_to(cr, IX(0.0), IY(-0.10));
		break;
	case CC_ICON_RESTART:
		/* round once and back to the start, with its head */
		cairo_new_sub_path(cr);
		cairo_arc(cr, x, y, 0.76 * r, -M_PI * 0.30, M_PI * 1.38);
		cairo_move_to(cr, IX(0.22), IY(-0.98));
		cairo_line_to(cr, IX(0.62), IY(-0.62));
		cairo_line_to(cr, IX(0.10), IY(-0.40));
		break;
	case CC_ICON_LOGOUT:
		/* out of the door */
		cairo_move_to(cr, IX(-0.05), IY(-0.82));
		cairo_line_to(cr, IX(-0.80), IY(-0.82));
		cairo_line_to(cr, IX(-0.80), IY(0.82));
		cairo_line_to(cr, IX(-0.05), IY(0.82));
		cairo_move_to(cr, IX(-0.38), IY(0.0));
		cairo_line_to(cr, IX(0.90), IY(0.0));
		cairo_move_to(cr, IX(0.52), IY(-0.36));
		cairo_line_to(cr, IX(0.90), IY(0.0));
		cairo_line_to(cr, IX(0.52), IY(0.36));
		break;
	case CC_ICON_SUSPEND:
		/* the crescent moon (the sign for sleep on every machine's button): round the outside
		 * from where the two circles cross to where they cross again, and back inside */
		cairo_new_sub_path(cr);
		cairo_arc(cr, x, y, 0.80 * r, 0.2472, 4.5900);
		cairo_arc_negative(cr, x + 0.34 * r, y - 0.30 * r, 0.66 * r, -2.2958, -5.4334);
		cairo_close_path(cr);
		break;
	case CC_ICON_LOCK:
		/* a padlock: the shackle over the body */
		cairo_new_sub_path(cr);
		cairo_arc(cr, x, y - 0.24 * r, 0.46 * r, M_PI, 2.0 * M_PI);
		cairo_line_to(cr, IX(0.46), IY(-0.06));
		cairo_move_to(cr, IX(-0.46), IY(-0.24));
		cairo_line_to(cr, IX(-0.46), IY(-0.06));
		cairo_rectangle(cr, IX(-0.74), IY(-0.06), 1.48 * r, 0.96 * r);
		cairo_move_to(cr, IX(0.0), IY(0.24));
		cairo_line_to(cr, IX(0.0), IY(0.56));
		break;
	case CC_ICON_SLEEP:
		/* a screen gone to sleep */
		cairo_rectangle(cr, IX(-0.86), IY(-0.66), 1.72 * r, 1.12 * r);
		cairo_move_to(cr, IX(0.0), IY(0.46));
		cairo_line_to(cr, IX(0.0), IY(0.74));
		cairo_move_to(cr, IX(-0.38), IY(0.76));
		cairo_line_to(cr, IX(0.38), IY(0.76));
		cairo_move_to(cr, IX(-0.26), IY(-0.34));
		cairo_line_to(cr, IX(0.26), IY(-0.34));
		cairo_line_to(cr, IX(-0.26), IY(0.14));
		cairo_line_to(cr, IX(0.26), IY(0.14));
		break;
	case CC_ICON_AIR:
		/* an aeroplane from above, nose up */
		cairo_move_to(cr, IX(0.0), IY(-0.96));
		cairo_line_to(cr, IX(0.0), IY(0.84));
		cairo_move_to(cr, IX(-0.92), IY(0.16));
		cairo_line_to(cr, IX(0.0), IY(-0.26));
		cairo_line_to(cr, IX(0.92), IY(0.16));
		cairo_move_to(cr, IX(-0.38), IY(0.88));
		cairo_line_to(cr, IX(0.0), IY(0.64));
		cairo_line_to(cr, IX(0.38), IY(0.88));
		break;
	case CC_ICON_SAVE:
		/* a leaf, and its vein */
		cairo_move_to(cr, IX(-0.72), IY(0.72));
		cairo_curve_to(cr, IX(-0.92), IY(-0.30), IX(-0.10), IY(-0.92), IX(0.78), IY(-0.78));
		cairo_curve_to(cr, IX(0.92), IY(0.10), IX(0.30), IY(0.92), IX(-0.72), IY(0.72));
		cairo_close_path(cr);
		cairo_move_to(cr, IX(-0.86), IY(0.86));
		cairo_line_to(cr, IX(0.30), IY(-0.30));
		break;
	case CC_ICON_NIGHT:
		/* the sun going down: the evening's warm light */
		cairo_move_to(cr, IX(-0.94), IY(0.40));
		cairo_line_to(cr, IX(0.94), IY(0.40));
		cairo_new_sub_path(cr);
		cairo_arc(cr, x, y + 0.40 * r, 0.46 * r, M_PI, 2.0 * M_PI);
		for (int k = 0; k < 5; k++) {
			double a = M_PI + M_PI * (k + 0.5) / 5.0;
			cairo_move_to(cr, x + cos(a) * 0.64 * r, y + 0.40 * r + sin(a) * 0.64 * r);
			cairo_line_to(cr, x + cos(a) * 0.90 * r, y + 0.40 * r + sin(a) * 0.90 * r);
		}
		break;
	case CC_ICON_DISPLAY:
		/* two screens, one in front of the other */
		cairo_move_to(cr, IX(-0.20), IY(-0.40));
		cairo_line_to(cr, IX(-0.20), IY(-0.80));
		cairo_line_to(cr, IX(0.92), IY(-0.80));
		cairo_line_to(cr, IX(0.92), IY(0.04));
		cairo_line_to(cr, IX(0.24), IY(0.04));
		cairo_rectangle(cr, IX(-0.92), IY(-0.40), 1.16 * r, 0.80 * r);
		cairo_move_to(cr, IX(-0.34), IY(0.40));
		cairo_line_to(cr, IX(-0.34), IY(0.66));
		cairo_move_to(cr, IX(-0.62), IY(0.68));
		cairo_line_to(cr, IX(-0.06), IY(0.68));
		break;
	default:
		break;
	}
#undef IX
#undef IY
}

/* Etched lightly into the cap's glass: unlit, a groove a little darker than the glass with the
 * room's light along its lower lip; lit, the etching takes the filament's light and is the
 * brightest thing on the cap. */
static void cc_icon_etch(cairo_t *fc, int icon, double cx, double cy, double cap, double k,
	double lr, double lg, double lb)
{
	if (icon == CC_ICON_NONE) {
		return;
	}
	double r = cap * 0.50, lw = fmax(0.9, cap * 0.075);
	cairo_save(fc);
	cairo_set_line_cap(fc, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(fc, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_width(fc, lw);
	cc_icon_path(fc, icon, cx + lw * 0.45, cy + lw * 0.45, r);
	cairo_set_source_rgba(fc, 1.0, 0.96, 0.86, 0.20 * (1.0 - k));
	cairo_stroke(fc);
	cc_icon_path(fc, icon, cx, cy, r);
	cairo_set_source_rgba(fc, 0.05, 0.04, 0.03, 0.34 * (1.0 - 0.6 * k));
	cairo_stroke(fc);
	if (k > 0.004) {
		cairo_set_line_width(fc, lw * 0.85);
		cc_icon_path(fc, icon, cx, cy, r);
		cairo_set_source_rgba(fc, fmin(1.0, 0.45 + lr * 0.6), fmin(1.0, 0.45 + lg * 0.6),
			fmin(1.0, 0.45 + lb * 0.6), 0.62 * k);
		cairo_stroke(fc);
	}
	cairo_restore(fc);
}

static void cc_face_power_button(cairo_t *fc, double fw, double fh,
	double sx, double sy, double ring_w, double cap_w, const char *word, double lamp,
	double lr, double lg, double lb, double pressed, int icon)
{
	(void)sy;   /* the faces are scaled alike, so across does for up */
	double cx = fw * 0.5, cy = fh * 0.5;
	double ring = ring_w * sx;
	double cap = cap_w * sx;
	double k = clampf(lamp, 0.0, 1.0);

	/* Pressed, the cap goes down into the ring: a hair smaller and a
	 * shade darker round its rim, which is all you see of a button
	 * travelling two millimetres. */
	if (pressed > 0.004) {
		cairo_set_source_rgba(fc, 0.04, 0.03, 0.02, 0.34 * pressed);
		cairo_arc(fc, cx, cy, cap * 1.02, 0, 2 * M_PI);
		cairo_arc_negative(fc, cx, cy, cap * (1.0 - 0.07 * pressed), 2 * M_PI, 0);
		cairo_fill(fc);
	}

	if (k > 0.004) {
		/* the filament, seen through cast glass: a hot core, the whole cap
		 * carrying the colour, and a wash of it on the brass around */
		cairo_pattern_t *g = cairo_pattern_create_radial(cx, cy - cap * 0.18,
			cap * 0.05, cx, cy, cap * 1.92);
		cairo_pattern_add_color_stop_rgba(g, 0.00, 1.0, 0.98, 0.94, 0.95 * k);
		cairo_pattern_add_color_stop_rgba(g, 0.15, lr, lg, lb, 0.96 * k);
		cairo_pattern_add_color_stop_rgba(g, 0.48, lr, lg, lb, 0.66 * k);
		cairo_pattern_add_color_stop_rgba(g, 0.66, lr, lg, lb, 0.16 * k);
		cairo_pattern_add_color_stop_rgba(g, 1.00, lr, lg, lb, 0.0);
		cairo_set_source(fc, g);
		cairo_arc(fc, cx, cy, cap * 1.92, 0, 2 * M_PI);
		cairo_fill(fc);
		cairo_pattern_destroy(g);
	}

	/* what it does, etched into the cap, pressed down into the ring with it */
	cc_icon_etch(fc, icon, cx, cy, cap * (1.0 - 0.05 * clampf(pressed, 0.0, 1.0)), k, lr, lg, lb);

	/* The word, cut into the bezel, over the light rather than under it.
	 * Under it the lamp washed the engraving off its own bezel, which is
	 * the one time you want to be able to read which button this is. */
	double tr = ring - (ring - cap) * 0.42;
	double ts = (ring - cap) * 1.10;
	arc_text(fc, word, cx, cy + fmax(1.0, ts * 0.10), tr, ts,
		1.00, 0.94, 0.74, 0.34 + 0.34 * k);
	arc_text(fc, word, cx, cy, tr, ts, 0.070, 0.050, 0.022, 0.94);
}

static void cc_face_power_key(cairo_t *fc, double fw, double fh,
	double sx, double sy, double turn)
{
	(void)fw;
	(void)fh;
	(void)turn;
	/* where the barrel lands in this face, at the top of it: the key lies below */
	double lock_x = (CC_PW_LOCK_U - CC[CC_PW_KEY].u + CC[CC_PW_KEY].fu) * sx;
	double cy = (CC[CC_PW_KEY].s + CC[CC_PW_KEY].fs - CC_PW_KEY_S) * sy;

	/* POWER, cut into the plate over the barrel */
	double ts = 0.125 * sy;
	show_text_as(fc, DESK_FONT_ENGRAVE, TR("POWER"), lock_x + 1.0, cy - 0.255 * sy + 1.0, ts,
		true,
		0.94, 0.86, 0.62, 0.26, 0);
	show_text_as(fc, DESK_FONT_ENGRAVE, TR("POWER"), lock_x, cy - 0.255 * sy, ts, true,
		0.090, 0.066, 0.030, 0.86, 0);

	/* the two positions, scored into the brass under the barrel */
	cairo_save(fc);
	cairo_set_line_width(fc, 0.016 * sx);
	cairo_set_source_rgba(fc, 0.110, 0.084, 0.040, 0.70);
	cairo_arc(fc, lock_x, cy, 0.300 * sx, M_PI * 0.16, M_PI * 0.84);
	cairo_stroke(fc);
	for (int i = 0; i < 2; i++) {
		double a = i ? M_PI * 0.84 : M_PI * 0.16;
		cairo_move_to(fc, lock_x + cos(a) * 0.255 * sx, cy + sin(a) * 0.255 * sy);
		cairo_line_to(fc, lock_x + cos(a) * 0.345 * sx, cy + sin(a) * 0.345 * sy);
		cairo_stroke(fc);
	}
	cairo_restore(fc);

	/* (the key is a thing in the room, drawn over this face: cc_key3) */
}

/* ---- what stands up off the panel ----------------------------------
 *
 * The levers of the two switches and the key are not printed on the
 * panel. A lever stands out of its boss at an angle, thrown one way or the
 * other; the key lies on the panel beside its lock until it is taken up,
 * stood in the lock, pushed home and turned, and then stands out of the
 * brass with its bow. They are drawn as things in the room: their points
 * through the console's own camera, each face lit by the room's lamp as it
 * faces it and shining towards the eye, their shadows thrown on the panel
 * along the lamp's light -- over the face printed under them, which has
 * only the words and the lamps. */

struct cc_view {
	const struct room_cam *cam;
	double ox, oy, sw, sh;
	struct cc_face panel;
	double aspect;
};

static vec3 cc_view_at(const struct cc_view *v, double u, double n, double s)
{
	return cc_world(&v->panel, u, n, s, 1.0, v->aspect);
}

/* a direction given across, out of and up the panel, in the room */
static vec3 cc_view_dir(const struct cc_view *v, double u, double n, double s)
{
	return vadd(vmul(v->panel.ax, u), vadd(vmul(v->panel.an, n), vmul(v->panel.as, s)));
}

static bool cc_view_pt(const struct cc_view *v, vec3 p, double *x, double *y)
{
	double u, w;
	if (!room_project(v->cam, p, &u, &w)) {
		return false;
	}
	*x = v->ox + u * v->sw;
	*y = v->oy + w * v->sh;
	return true;
}

static bool cc_view_poly(cairo_t *cr, const struct cc_view *v, const vec3 *p, int n)
{
	cairo_new_path(cr);
	for (int i = 0; i < n; i++) {
		double x, y;
		if (!cc_view_pt(v, p[i], &x, &y)) {
			cairo_new_path(cr);
			return false;
		}
		if (i == 0) {
			cairo_move_to(cr, x, y);
		} else {
			cairo_line_to(cr, x, y);
		}
	}
	cairo_close_path(cr);
	return true;
}

/* the outline round points in the room, as they land on the screen */
static bool cc_view_hull(cairo_t *cr, const struct cc_view *v, const vec3 *p, int n)
{
	double x[16], y[16];
	int m = 0;
	for (int i = 0; i < n && m < 16; i++) {
		if (cc_view_pt(v, p[i], &x[m], &y[m])) {
			m++;
		}
	}
	if (m < 3) {
		return false;
	}
	/* gift wrapping: few points */
	int start = 0;
	for (int i = 1; i < m; i++) {
		if (x[i] < x[start] || (x[i] == x[start] && y[i] < y[start])) {
			start = i;
		}
	}
	cairo_new_path(cr);
	int cur = start, guard = 0;
	do {
		if (cur == start) {
			cairo_move_to(cr, x[cur], y[cur]);
		} else {
			cairo_line_to(cr, x[cur], y[cur]);
		}
		int next = (cur + 1) % m;
		for (int i = 0; i < m; i++) {
			double cx = (x[next] - x[cur]) * (y[i] - y[cur]) - (y[next] - y[cur]) * (x[i] - x[cur]);
			if (cx < 0.0) {
				next = i;
			}
		}
		cur = next;
	} while (cur != start && ++guard < m);
	cairo_close_path(cr);
	return true;
}

static bool cc_view_facing(const struct cc_view *v, vec3 p, vec3 nrm)
{
	return vdot(nrm, vsub(v->cam->eye, p)) > 0.0;
}

/* how a face at p turned to nrm takes the lamp, and what it shines back
 * towards the eye */
static double cc_view_lit(const struct cc_view *v, vec3 p, vec3 nrm, double *shine)
{
	struct key_light k = key_at(p, ROOM_KIND);
	vec3 nn = vnorm(nrm);
	double d = fmax(0.0, vdot(nn, k.dir));
	vec3 e = vnorm(vsub(v->cam->eye, p));
	vec3 hv = vnorm(vadd(e, k.dir));
	*shine = d > 0.0 ? pow(fmax(0.0, vdot(nn, hv)), 28.0) : 0.0;
	return d;
}

static void cc_view_fill(cairo_t *cr, const struct cc_view *v, vec3 p, vec3 nrm,
	const double rgb[3], double gloss)
{
	double shine, d = cc_view_lit(v, p, nrm, &shine);
	double k = 0.32 + 0.80 * d;
	cairo_set_source_rgb(cr, fmin(1.0, rgb[0] * k + gloss * shine),
		fmin(1.0, rgb[1] * k + gloss * shine), fmin(1.0, rgb[2] * k + gloss * shine * 0.9));
	cairo_fill(cr);
}

/* where p's shadow falls on the panel, at n off its face, along the light */
static vec3 cc_view_shadow(const struct cc_view *v, vec3 p, double at)
{
	struct key_light k = key_at(p, ROOM_KIND);
	vec3 q = cc_to_face(&v->panel, vsub(p, v3(0.0, 0.0, cc_dz(1.0, v->aspect))));
	double ln = fmax(vdot(k.dir, v->panel.an), 0.08);
	return vsub(p, vmul(k.dir, (q.y - at) / ln));
}

/* A round rod from a to b, r0 thick at a and r1 at b, lit across its
 * curve; rgb its colour. */
static void cc_view_rod(cairo_t *cr, const struct cc_view *v, vec3 a, vec3 b, double r0,
	double r1, const double rgb[3], double gloss)
{
	double ax, ay, bx, by;
	if (!cc_view_pt(v, a, &ax, &ay) || !cc_view_pt(v, b, &bx, &by)) {
		return;
	}
	vec3 d = vnorm(vsub(b, a));
	vec3 mid = vmul(vadd(a, b), 0.5);
	/* the side of it facing the eye, and across it */
	vec3 e = vsub(v->cam->eye, mid);
	vec3 f = vnorm(vsub(e, vmul(d, vdot(e, d))));
	vec3 x = vnorm(vcross(d, f));
	double sx0, sy0, sx1, sy1;
	if (!cc_view_pt(v, vadd(mid, vmul(x, -1.0)), &sx0, &sy0)
		|| !cc_view_pt(v, vadd(mid, x), &sx1, &sy1)) {
		return;
	}
	/* across it on the screen, per unit of the room */
	double px = (sx1 - sx0) * 0.5, py = (sy1 - sy0) * 0.5;
	cairo_save(cr);
	cairo_move_to(cr, ax - px * r0, ay - py * r0);
	cairo_line_to(cr, bx - px * r1, by - py * r1);
	cairo_line_to(cr, bx + px * r1, by + py * r1);
	cairo_line_to(cr, ax + px * r0, ay + py * r0);
	cairo_close_path(cr);
	double r = (r0 + r1) * 0.5;
	double mx = (ax + bx) * 0.5, my = (ay + by) * 0.5;
	cairo_pattern_t *g = cairo_pattern_create_linear(mx - px * r, my - py * r,
		mx + px * r, my + py * r);
	for (int i = 0; i <= 8; i++) {
		double s = -1.0 + 2.0 * i / 8.0, c = sqrt(fmax(0.0, 1.0 - s * s));
		vec3 nrm = vadd(vmul(f, c), vmul(x, s));
		double shine, lit = cc_view_lit(v, mid, nrm, &shine);
		double k = (0.30 + 0.80 * lit) * (0.55 + 0.45 * c);
		cairo_pattern_add_color_stop_rgb(g, i / 8.0, fmin(1.0, rgb[0] * k + gloss * shine),
			fmin(1.0, rgb[1] * k + gloss * shine), fmin(1.0, rgb[2] * k + gloss * shine));
	}
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
}

/* A ball at c, r across, lit where it faces the lamp. */
static void cc_view_ball(cairo_t *cr, const struct cc_view *v, vec3 c, double r,
	const double rgb[3], double gloss)
{
	double cx, cy, ex, ey;
	if (!cc_view_pt(v, c, &cx, &cy) || !cc_view_pt(v, vadd(c, vmul(v->cam->uu, r)), &ex, &ey)) {
		return;
	}
	double rp = hypot(ex - cx, ey - cy);
	struct key_light k = key_at(c, ROOM_KIND);
	double lx, ly;
	if (!cc_view_pt(v, vadd(c, vmul(k.dir, r)), &lx, &ly)) {
		lx = cx;
		ly = cy;
	}
	double hx = cx + (lx - cx) * 0.55, hy = cy + (ly - cy) * 0.55;
	cairo_pattern_t *g = cairo_pattern_create_radial(hx, hy, rp * 0.05, cx, cy, rp);
	cairo_pattern_add_color_stop_rgb(g, 0.0, fmin(1.0, rgb[0] + gloss), fmin(1.0, rgb[1] + gloss),
		fmin(1.0, rgb[2] + gloss));
	cairo_pattern_add_color_stop_rgb(g, 0.45, rgb[0] * 0.95, rgb[1] * 0.95, rgb[2] * 0.95);
	cairo_pattern_add_color_stop_rgb(g, 1.0, rgb[0] * 0.30, rgb[1] * 0.30, rgb[2] * 0.30);
	cairo_set_source(cr, g);
	cairo_arc(cr, cx, cy, rp, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

/* a soft dark band along the shadow of a to b on the panel */
static void cc_view_rod_shadow(cairo_t *cr, const struct cc_view *v, vec3 a, vec3 b, double r,
	double at, double alpha)
{
	vec3 sa = cc_view_shadow(v, a, at), sb = cc_view_shadow(v, b, at);
	double ax, ay, bx, by, rx, ry;
	if (!cc_view_pt(v, sa, &ax, &ay) || !cc_view_pt(v, sb, &bx, &by)
		|| !cc_view_pt(v, vadd(sb, vmul(v->panel.ax, r)), &rx, &ry)) {
		return;
	}
	double rp = hypot(rx - bx, ry - by);
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int i = 3; i >= 1; i--) {
		cairo_set_source_rgba(cr, 0.02, 0.015, 0.01, alpha / 3.0);
		cairo_set_line_width(cr, rp * 2.0 * (1.0 + 0.55 * i));
		cairo_move_to(cr, ax, ay);
		cairo_line_to(cr, bx, by);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

/* A switch's lever, standing out of its boss, thrown towards ON (across to
 * the left) or OFF. The wireless one is a bakelite bat, the Bluetooth one a
 * chromed toggle with a ball on its end. */
static void cc_lever3(cairo_t *cr, const struct cc_view *v, int i, bool on, bool toggle)
{
	static const double STEEL[3] = { 0.78, 0.78, 0.80 };
	static const double BAKELITE[3] = { 0.13, 0.11, 0.10 };
	double u0 = CC[i].u, s0 = CC[i].s, n0 = RM_CC_SKIN + 0.16;
	double th = 0.72, dir = on ? -1.0 : 1.0;
	double L = toggle ? 0.44 : 0.50;
	vec3 a = cc_view_at(v, u0, n0, s0);
	vec3 b = cc_view_at(v, u0 + dir * L * sin(th), n0 + L * cos(th), s0);
	double at = RM_CC_SKIN + 0.06;
	cc_view_rod_shadow(cr, v, a, b, toggle ? 0.05 : 0.08, at, 0.55);
	if (toggle) {
		cc_view_rod(cr, v, a, b, 0.045, 0.034, STEEL, 0.9);
		cc_view_ball(cr, v, b, 0.072, STEEL, 0.35);
	} else {
		/* a bat: wider towards its end, the end rounded */
		cc_view_rod(cr, v, a, b, 0.060, 0.095, BAKELITE, 0.5);
		cc_view_ball(cr, v, b, 0.095, BAKELITE, 0.18);
	}
}

/* The fader's handle: a block of bakelite standing up out of its slot at
 * v of the way along, milled across its top for a thumb. */
static void cc_fader3(cairo_t *cr, const struct cc_view *v, int i, double val, double warm)
{
	static const double BAKELITE[3] = { 0.15, 0.13, 0.12 };
	double run = CC[i].hu * 0.82;
	double u = CC[i].u - run + 2.0 * run * clampf(val, 0.0, 1.0);
	double s0 = CC[i].s, nb = RM_CC_SKIN + 0.04, nt = nb + 0.24;
	double hu = 0.085, hs = 0.23;
	vec3 c[8];
	for (int k = 0; k < 8; k++) {
		c[k] = cc_view_at(v, u + ((k & 1) ? hu : -hu), (k & 4) ? nt : nb,
			s0 + ((k & 2) ? hs : -hs));
	}
	/* its shadow on the plate: the top, thrown along the light */
	vec3 sh[4] = { cc_view_shadow(v, c[4], nb), cc_view_shadow(v, c[5], nb),
		cc_view_shadow(v, c[7], nb), cc_view_shadow(v, c[6], nb) };
	vec3 all[8] = { c[0], c[1], c[2], c[3], sh[0], sh[1], sh[2], sh[3] };
	cairo_save(cr);
	if (cc_view_hull(cr, v, all, 8)) {
		cairo_set_source_rgba(cr, 0.02, 0.015, 0.01, 0.45);
		cairo_fill(cr);
	}
	const int F[5][4] = { { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 },
		{ 1, 3, 7, 5 } };
	vec3 an = v->panel.an, ax = v->panel.ax, as = v->panel.as;
	vec3 Nf[5] = { an, vmul(as, -1.0), as, vmul(ax, -1.0), ax };
	for (int f = 0; f < 5; f++) {
		vec3 q[4] = { c[F[f][0]], c[F[f][1]], c[F[f][2]], c[F[f][3]] };
		vec3 m = vmul(vadd(vadd(q[0], q[1]), vadd(q[2], q[3])), 0.25);
		if (!cc_view_facing(v, m, Nf[f]) || !cc_view_poly(cr, v, q, 4)) {
			continue;
		}
		cairo_path_t *path = cairo_copy_path(cr);
		cc_view_fill(cr, v, m, Nf[f], BAKELITE, f == 0 ? 0.25 : 0.12);
		cairo_append_path(cr, path);
		cairo_path_destroy(path);
		cairo_set_line_width(cr, 0.7);
		cairo_stroke(cr);
	}
	/* the milling across its top */
	cairo_set_source_rgba(cr, 0.76, 0.72, 0.62, 0.45 * (0.4 + 0.6 * clampf(warm, 0.0, 1.0)));
	for (int g = 0; g < 4; g++) {
		double s1 = s0 - hs * 0.62 + g * hs * 0.38, s2 = s1 + hs * 0.10;
		vec3 q[4] = { cc_view_at(v, u - hu * 0.72, nt + 0.002, s1),
			cc_view_at(v, u + hu * 0.72, nt + 0.002, s1),
			cc_view_at(v, u + hu * 0.72, nt + 0.002, s2),
			cc_view_at(v, u - hu * 0.72, nt + 0.002, s2) };
		if (cc_view_poly(cr, v, q, 4)) {
			cairo_fill(cr);
		}
	}
	cairo_restore(cr);
}

/* The key: t 0 lying on the panel under the lock, upright, its blade towards it; taken up,
 * a quarter turned in the fingers to the slot, and stood in the lock; pushed home; turned a
 * quarter round. */
static void cc_key3(cairo_t *cr, const struct cc_view *v, double t)
{
	static const double BRASS[3] = { 0.82, 0.66, 0.33 };
	const double Lb = 0.36, bw = 0.052, kt = 0.040, br = 0.190, bh = 0.060, bt = 0.075;
	double bs = CC_PW_KEY_S;
	double top = RM_CC_SKIN + 0.11;            /* the barrel's face */
	t = clampf(t, 0.0, 1.0);
	double up = smoothstepf(0.00, 0.40, t);
	double in = smoothstepf(0.40, 0.58, t);
	double turn = smoothstepf(0.62, 1.00, t) * M_PI * 0.5;
	/* its tip: under the lock, then over the slot, then in it */
	double ts = CC_PW_KEY_REST + (bs - CC_PW_KEY_REST) * up;
	double tn = (RM_CC_SKIN + kt * 0.5) + (top - RM_CC_SKIN - kt * 0.5) * up
		+ 0.24 * sin(M_PI * up) - in * Lb * 0.78;
	double phi = up * M_PI * 0.5, twist = up * M_PI * 0.5;
	/* along it from bow to tip, across its blade, and through it: lying, the blade flat and
	 * across the panel; stood up, across the slot as it was before */
	vec3 A = cc_view_dir(v, 0.0, -sin(phi), cos(phi));
	vec3 P1 = cc_view_dir(v, 1.0, 0.0, 0.0), P2 = cc_view_dir(v, 0.0, cos(phi), sin(phi));
	vec3 W0 = vadd(vmul(P1, cos(twist)), vmul(P2, sin(twist)));
	vec3 T0 = vsub(vmul(P2, cos(twist)), vmul(P1, sin(twist)));
	vec3 W = vadd(vmul(W0, cos(turn)), vmul(T0, sin(turn)));
	vec3 T = vsub(vmul(T0, cos(turn)), vmul(W0, sin(turn)));
	vec3 tip = cc_view_at(v, CC_PW_LOCK_U, tn, ts);
	vec3 root = vsub(tip, vmul(A, Lb));
	/* what of the blade is out of the barrel */
	double rootn = tn + Lb * sin(phi);
	double out = Lb;
	if (tn < top && rootn > tn) {
		out = Lb * clampf((rootn - top) / (rootn - tn), 0.0, 1.0);
	}
	vec3 end = vadd(root, vmul(A, out));
	vec3 C = vsub(root, vmul(A, br * 0.80));
	enum { N = 56 };

	/* Once it is in, the plug it is in turns with it: its face over the
	 * barrel's, the slot across it the way the blade is. */
	if (in > 0.0) {
		vec3 pc = cc_view_at(v, CC_PW_LOCK_U, top + 0.002, bs);
		vec3 ax = cc_view_dir(v, 1.0, 0.0, 0.0), as = cc_view_dir(v, 0.0, 0.0, 1.0);
		vec3 ring[N];
		for (int i = 0; i < N; i++) {
			double b = 2.0 * M_PI * i / N;
			ring[i] = vadd(pc, vadd(vmul(ax, 0.150 * cos(b)), vmul(as, 0.150 * sin(b))));
		}
		cairo_save(cr);
		if (cc_view_poly(cr, v, ring, N)) {
			cc_view_fill(cr, v, pc, v->panel.an, BRASS, 0.25);
		}
		vec3 wd = vadd(vmul(as, cos(turn)), vmul(ax, -sin(turn)));
		vec3 cd = vadd(vmul(as, sin(turn)), vmul(ax, cos(turn)));
		vec3 slot[4] = {
			vadd(pc, vadd(vmul(wd, 0.140), vmul(cd, 0.030))),
			vadd(pc, vadd(vmul(wd, -0.140), vmul(cd, 0.030))),
			vadd(pc, vadd(vmul(wd, -0.140), vmul(cd, -0.030))),
			vadd(pc, vadd(vmul(wd, 0.140), vmul(cd, -0.030))),
		};
		if (cc_view_poly(cr, v, slot, 4)) {
			cairo_set_source_rgba(cr, 0.08, 0.06, 0.03, 0.95);
			cairo_fill(cr);
		}
		cairo_restore(cr);
	}

	/* its shadow on the panel: the bow and what is out of the blade */
	{
		double at = RM_CC_SKIN;
		vec3 pts[N];
		for (int i = 0; i < N; i++) {
			double b = 2.0 * M_PI * i / N;
			pts[i] = cc_view_shadow(v, vadd(C, vadd(vmul(A, br * cos(b)), vmul(W, br * sin(b)))), at);
		}
		cairo_save(cr);
		if (cc_view_poly(cr, v, pts, N)) {
			cairo_set_source_rgba(cr, 0.02, 0.015, 0.01, 0.48);
			cairo_fill(cr);
		}
		cc_view_rod_shadow(cr, v, root, end, bw, at, 0.45);
		cairo_restore(cr);
	}

	/* the blade and the bow, the one further from the eye first */
	double db = vlen(vsub(v->cam->eye, vmul(vadd(root, end), 0.5)));
	double dc = vlen(vsub(v->cam->eye, C));
	for (int pass = 0; pass < 2; pass++) {
		bool blade = (pass == 0) == (db > dc);
		if (blade && out > 0.004) {
			/* a box: its faces turned to the eye */
			vec3 c[8];
			for (int k = 0; k < 8; k++) {
				vec3 p = (k & 1) ? end : root;
				p = vadd(p, vmul(W, (k & 2) ? bw : -bw));
				c[k] = vadd(p, vmul(T, (k & 4) ? kt * 0.5 : -kt * 0.5));
			}
			const int F[6][4] = { { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
				{ 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 } };
			vec3 Nf[6] = { vmul(T, -1.0), T, vmul(W, -1.0), W, vmul(A, -1.0), A };
			for (int f = 0; f < 6; f++) {
				vec3 q[4] = { c[F[f][0]], c[F[f][1]], c[F[f][2]], c[F[f][3]] };
				vec3 m = vmul(vadd(vadd(q[0], q[1]), vadd(q[2], q[3])), 0.25);
				if (!cc_view_facing(v, m, Nf[f]) || !cc_view_poly(cr, v, q, 4)) {
					continue;
				}
				cc_view_fill(cr, v, m, Nf[f], BRASS, 0.55);
			}
			/* the wards, cut out of one edge, while it lies there */
			if (up < 0.5 && cc_view_facing(v, root, T)) {
				cairo_set_source_rgba(cr, 0.10, 0.08, 0.04, 0.75 * (1.0 - up * 2.0));
				for (int w = 0; w < 3; w++) {
					double a0 = Lb * (0.18 + w * 0.26), a1 = a0 + Lb * 0.10;
					if (a1 > out) {
						break;
					}
					vec3 q[4] = {
						vadd(vadd(root, vmul(A, a0)), vadd(vmul(W, -bw), vmul(T, kt * 0.5))),
						vadd(vadd(root, vmul(A, a1)), vadd(vmul(W, -bw), vmul(T, kt * 0.5))),
						vadd(vadd(root, vmul(A, a1)), vadd(vmul(W, -bw * 0.1), vmul(T, kt * 0.5))),
						vadd(vadd(root, vmul(A, a0)), vadd(vmul(W, -bw * 0.1), vmul(T, kt * 0.5))),
					};
					if (cc_view_poly(cr, v, q, 4)) {
						cairo_fill(cr);
					}
				}
			}
		} else if (!blade) {
			/* the bow: a disc, its rim, the hole through it */
			vec3 rim[2][N];
			for (int i = 0; i < N; i++) {
				double b = 2.0 * M_PI * i / N;
				vec3 r = vadd(C, vadd(vmul(A, br * cos(b)), vmul(W, br * sin(b))));
				rim[0][i] = vsub(r, vmul(T, bt * 0.5));
				rim[1][i] = vadd(r, vmul(T, bt * 0.5));
			}
			for (int i = 0; i < N; i++) {
				int j = (i + 1) % N;
				double b = 2.0 * M_PI * (i + 0.5) / N;
				vec3 nrm = vadd(vmul(A, cos(b)), vmul(W, sin(b)));
				vec3 q[4] = { rim[0][i], rim[0][j], rim[1][j], rim[1][i] };
				vec3 m = vmul(vadd(vadd(q[0], q[1]), vadd(q[2], q[3])), 0.25);
				if (cc_view_facing(v, m, nrm) && cc_view_poly(cr, v, q, 4)) {
					/* the same colour round its edge too, so that the
					 * strips of the rim do not show as teeth */
					cairo_path_t *path = cairo_copy_path(cr);
					cc_view_fill(cr, v, m, nrm, BRASS, 0.30);
					cairo_append_path(cr, path);
					cairo_path_destroy(path);
					cairo_set_line_width(cr, 0.8);
					cairo_stroke(cr);
				}
			}
			for (int side = 0; side < 2; side++) {
				vec3 nrm = side ? T : vmul(T, -1.0);
				vec3 fc = vadd(C, vmul(nrm, bt * 0.5));
				if (!cc_view_facing(v, fc, nrm) || !cc_view_poly(cr, v, rim[side], N)) {
					continue;
				}
				cc_view_fill(cr, v, fc, nrm, BRASS, 0.6);
				vec3 hole[N];
				for (int i = 0; i < N; i++) {
					double b = 2.0 * M_PI * i / N;
					hole[i] = vadd(fc, vadd(vmul(A, bh * cos(b)), vmul(W, bh * sin(b))));
				}
				if (cc_view_poly(cr, v, hole, N)) {
					cairo_set_source_rgba(cr, 0.10, 0.07, 0.03, 0.92);
					cairo_fill(cr);
				}
			}
		}
	}
}

/* The glass over the three.
 *
 * Not baked and not a face: it swings, so it is four corners projected and
 * filled. Colourless, so everything it is made of is edges -- the bright
 * line down its thickness, the one band of the room it reflects, and the
 * shadow it drops on the plate when it is up. */
static void cc_nameplate(cairo_t *cr, const struct room_cam *cam,
	double ox, double oy, double sw, double sh, double slide, double aspect,
	double lit);

/* How far control i's light has come up as the set warms up (warm 0 to 1): one after another,
 * each over a quarter of the warming -- in twelve steps, which is as smooth as a lamp coming up
 * looks, and what a piece is drawn again for (desk_console_piece_key): every frame of it, a
 * piece at a time, was what the set's first second and a half cost (2026-11-12). */
static double cc_lit(double warm, int i)
{
	const double LAMP_W = 0.26;
	double lit = clampf((warm - i * (1.0 - LAMP_W) / (CC_COUNT - 1)) / LAMP_W, 0.0, 1.0);
	return round(lit * 12.0) / 12.0;
}

/* One control's face, printed on the quad it has on the panel. */
static void cc_control_paint(cairo_t *cr, struct room_render *rr, double ox, double oy,
	double scale, double aspect, int i, const struct desk_console_state *st,
	int hover, const struct desk_meters *mp, double warm)
{
	struct desk_meters m = *mp;
	double lit = cc_lit(warm, i);
	vec3 q[4];
	cc_quad(i, 1.0, aspect, q);
	cairo_matrix_t mt;
	double fw = 0, fh = 0;
	cc_face_size(i, &fw, &fh);
	if (!quad_matrix(&rr->console_cam, q, fw, fh, ox, oy,
		rr->w * scale, rr->h * scale, &mt)) {
		return;
	}
	/* pixels to the world unit, so anything drawn at a measurement
	 * off the trace lands on the hardware it belongs to */
	double sx = fw / (2.0 * CC[i].fu), sy = fh / (2.0 * CC[i].fs);
	cairo_surface_t *face = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		(int)fw, (int)fh);
	cairo_t *fc = cairo_create(face);
	if (CC[i].round) {
		/* a tube is round, and a rectangle of phosphor over a round
		 * bezel is the one thing that would give the whole set away */
		cairo_arc(fc, fw * 0.5, fh * 0.5, fmin(fw, fh) * 0.5, 0, 2 * M_PI);
		cairo_clip(fc);
	}
	switch (i) {
	case CC_CRT:
		cc_face_crt(fc, fw, fh, &m, lit, st ? st->clock : 0.0,
			g_cc_trace, CC_TRACE, st ? st->screens_said : -1, st ? st->screens_word : 0.0);
		break;
	case CC_BATT:
		cc_face_battery(fc, fw, fh, &m, lit);
		break;
	case CC_NET:
		cc_face_net(fc, fw, fh, &m, lit);
		break;
	case CC_VOL:
		cc_face_knob(fc, fw, fh, sx, sy, st ? st->volume : 0.5, lit);
		break;
	case CC_BRIGHT:
		cc_face_fader(fc, fw, fh, sx, sy, CC[i].hu,
			st ? st->brightness : 1.0, lit, TR("LAMP"), true);
		break;
	case CC_SCREEN:
		cc_face_fader(fc, fw, fh, sx, sy, CC[i].hu,
			st ? st->screen : 1.0, lit, TR("SCREEN"), st && st->screen_ok);
		break;
	case CC_WIFI:
		cc_face_switch(fc, fw, fh, sx, sy, CC[i].hu, CC[i].hs,
			st && st->wifi_on, false,
			(st && st->wifi_on) ? lit : 0.0, 1.0, 0.64, 0.18, TR("WIRELESS"));
		break;
	case CC_PW_OFF:
	case CC_PW_BOOT:
	case CC_PW_OUT:
	case CC_PW_SUSPEND: {
		static const char *WORD[4] = { "SHUT DOWN", "RESTART", "LOG OUT", "SUSPEND" };
		static const double COL[4][3] = {
			{ 1.00, 0.24, 0.15 },
			{ 1.00, 0.72, 0.17 },
			{ 0.34, 1.00, 0.46 },
			{ 0.36, 0.62, 1.00 },
		};
		int k = i == CC_PW_SUSPEND ? 3 : i - CC_PW_OFF;
		/* Nothing here is alight until the key is home. The filaments
		 * come up with the last of the turn, not with the warm-up:
		 * this part of the machine is off, not merely unattended. */
		double key = st ? clampf(st->key, 0.0, 1.0) : 0.0;
		double lamp = smoothstepf(0.66, 1.00, key) * lit;
		double push = 0.0;
		if (st && st->pressed >= 0) {
			double d = clampf(st->dying, 0.0, 1.0);
			if (st->pressed == i) {
				push = 1.0;
				/* the one that was pushed is the last thing alight */
				lamp *= pow(1.0 - d, 1.45);
			} else {
				lamp *= 1.0 - smoothstepf(0.0, 0.30, d);
			}
		}
		if (st && st->push_which == i) {
			push = fmax(push, clampf(st->push, 0.0, 1.0));
		}
		static const int ICON[4] = { CC_ICON_POWER, CC_ICON_RESTART, CC_ICON_LOGOUT,
			CC_ICON_SUSPEND };
		cc_face_power_button(fc, fw, fh, sx, sy, CC_LB_RING, CC_LB_R, TR(WORD[k]), lamp,
			COL[k][0], COL[k][1], COL[k][2], push, ICON[k]);
		break;
	}
	case CC_PW_KEY:
		cc_face_power_key(fc, fw, fh, sx, sy, st ? st->key : 0.0);
		break;
	case CC_LOCK:
		/* always ready, a low blue light, brighter pushed */
		cc_face_power_button(fc, fw, fh, sx, sy, CC_PW_RING, CC_PW_R, TR("LOCK"),
			lit * (0.55 + 0.45 * (st ? clampf(st->lock_push, 0.0, 1.0) : 0.0)),
			0.36, 0.62, 1.00, st ? st->lock_push : 0.0, CC_ICON_LOCK);
		break;
	case CC_SLEEP: {
		/* the same, in opal: a low white light, brighter pushed */
		double push = st && st->push_which == i ? clampf(st->push, 0.0, 1.0) : 0.0;
		cc_face_power_button(fc, fw, fh, sx, sy, CC_PW_RING, CC_PW_R, TR("SLEEP"),
			lit * (0.45 + 0.55 * push), 1.00, 0.95, 0.84, push, CC_ICON_SLEEP);
		break;
	}
	case CC_AIR:
	case CC_SAVE:
	case CC_NIGHT:
	case CC_DISPLAY: {
		/* on the lid: lit while what it puts on is on -- DISPLAY, which is always one way or
		 * another, always lit, the colour of the way. A machine that cannot do it has it dark
		 * (DISPLAY, with only the one screen). */
		static const char *WORD[4] = { "AIRPLANE", "POWER SAVE", "NIGHT", "DISPLAY" };
		static const double COL[4][3] = {
			{ 1.00, 0.62, 0.18 },
			{ 0.34, 1.00, 0.46 },
			{ 0.40, 0.56, 1.00 },
			{ 1.00, 0.95, 0.84 },
		};
		int k = i - CC_AIR;
		double push = st && st->push_which == i ? clampf(st->push, 0.0, 1.0) : 0.0;
		bool on = st && (i == CC_AIR ? st->air_on : i == CC_SAVE ? st->save_on
			: i == CC_NIGHT ? st->night_on : false);
		bool can = !st || i == CC_AIR
			|| (i == CC_SAVE ? st->save_ok : i == CC_NIGHT ? st->night_ok : st->screens_ok);
		double lamp = !can ? 0.0 : i == CC_DISPLAY ? lit * (0.55 + 0.45 * push)
			: on ? lit : lit * 0.10 * push;
		/* DISPLAY lit the colour of how the screens are used: white the desk going on onto
		 * them, blue the same on all, amber this one only, green the other only */
		static const double WAY[DESK_SCREENS_MODES][3] = {
			{ 1.00, 0.95, 0.84 },
			{ 0.40, 0.62, 1.00 },
			{ 1.00, 0.62, 0.18 },
			{ 0.34, 1.00, 0.46 },
		};
		const double *c = COL[k];
		if (i == CC_DISPLAY && st && st->screens_mode >= 0
			&& st->screens_mode < DESK_SCREENS_MODES) {
			c = WAY[st->screens_mode];
		}
		static const int ICON[4] = { CC_ICON_AIR, CC_ICON_SAVE, CC_ICON_NIGHT,
			CC_ICON_DISPLAY };
		cc_face_power_button(fc, fw, fh, sx, sy, CC_LB_RING, CC_LB_R, TR(WORD[k]), lamp,
			c[0], c[1], c[2], push, ICON[k]);
		break;
	}
	case CC_LID:
		break;
	default:
		cc_face_switch(fc, fw, fh, sx, sy, CC[i].hu, CC[i].hs,
			st && st->bt_on, true,
			(st && st->bt_on) ? lit : 0.0, 0.32, 0.56, 1.0, TR("BLUETOOTH"));
		break;
	}
	if (hover == i) {
		/* The one under the hand catches the light. A flat wash over
		 * the face put a grey rectangle on the panel, which is a thing
		 * no panel has; light falls in a pool. */
		cairo_pattern_t *g = cairo_pattern_create_radial(
			fw * 0.5, fh * 0.5, 0.0,
			fw * 0.5, fh * 0.5, fmax(fw, fh) * 0.62);
		cairo_pattern_add_color_stop_rgba(g, 0.00, 1.0, 0.95, 0.80, 0.17);
		cairo_pattern_add_color_stop_rgba(g, 0.55, 1.0, 0.94, 0.78, 0.07);
		cairo_pattern_add_color_stop_rgba(g, 1.00, 1.0, 0.94, 0.78, 0.0);
		cairo_set_source(fc, g);
		cairo_paint(fc);
		cairo_pattern_destroy(g);
	}
	cairo_destroy(fc);
	cairo_surface_flush(face);

	if (cc_paste(cr, &rr->console_cam, aspect, ox, oy, rr->w * scale, rr->h * scale, i, face,
		0.97)) {
		cairo_surface_destroy(face);
		goto stands;
	}
	cairo_save(cr);
	cairo_transform(cr, &mt);
	cairo_set_source_surface(cr, face, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint_with_alpha(cr, 0.97);
	/* glass over the instruments up on the tower, the tube's round -- and none over the knob
	 * and the faders beside them, where a sheen the shape of the face was a grey box */
	if (i == CC_CRT || i == CC_BATT || i == CC_NET) {
		cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, fw * 0.7, fh);
		cairo_pattern_add_color_stop_rgba(g, 0.00, 1, 1, 1, 0.17);
		cairo_pattern_add_color_stop_rgba(g, 0.45, 1, 1, 1, 0.03);
		cairo_pattern_add_color_stop_rgba(g, 1.00, 1, 1, 1, 0.00);
		cairo_set_source(cr, g);
		if (CC[i].round) {
			cairo_arc(cr, fw * 0.5, fh * 0.5, fmin(fw, fh) * 0.5, 0, 2 * M_PI);
		} else {
			cairo_rectangle(cr, 0, 0, fw, fh);
		}
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
	cairo_restore(cr);
	cairo_surface_destroy(face);

stands:
	/* and what stands up off it */
	if (i == CC_WIFI || i == CC_BT || i == CC_PW_KEY || i == CC_BRIGHT || i == CC_SCREEN) {
		struct cc_face panel, tower;
		cc_faces(&panel, &tower);
		struct cc_view v = { &rr->console_cam, ox, oy, rr->w * scale, rr->h * scale,
			CC[i].tower ? tower : panel, aspect };
		if (i == CC_PW_KEY) {
			cc_key3(cr, &v, st ? st->key : 0.0);
		} else if (i == CC_BRIGHT) {
			cc_fader3(cr, &v, i, st ? st->brightness : 1.0, lit);
		} else if (i == CC_SCREEN) {
			/* with nothing to turn it stays at the top of its slot */
			cc_fader3(cr, &v, i, st && st->screen_ok ? st->screen : 1.0, lit);
		} else {
			cc_lever3(cr, &v, i, i == CC_WIFI ? (st && st->wifi_on) : (st && st->bt_on),
				i == CC_BT);
		}
	}
}

/* The wireless aerial: a motor-driven telescopic one, the kind a car of
 * the eighties put up when the radio went on, standing in a chrome ferrule
 * at the top right of the tower, over the lamp's fader. ext is 0 down
 * and 1 all the way up: the thinnest section comes out first and each
 * thicker one is drawn out after it. Most of it goes up out of the view;
 * what shows is the foot of it at work. */
#define CC_ANT_U 4.10
#define CC_ANT_S 1.22
static void cc_antenna(cairo_t *cr, struct room_render *rr, double ox, double oy, double scale,
	double aspect, double ext)
{
	static const double CHROME[3] = { 0.80, 0.81, 0.84 };
	static const double DARK[3] = { 0.10, 0.095, 0.09 };
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	struct cc_view v = { &rr->console_cam, ox, oy, rr->w * scale, rr->h * scale, panel,
		aspect };
	vec3 up = v3(0, 1, 0);
	vec3 foot = cc_world(&tower, CC_ANT_U, RM_CC_SKIN + 0.02, CC_ANT_S, 1.0, aspect);
	/* the boss it stands in, and the ferrule */
	cc_view_rod(cr, &v, foot, vadd(foot, vmul(up, 0.10)), 0.165, 0.165, DARK, 0.35);
	vec3 base = vadd(foot, vmul(up, 0.34));
	cc_view_rod(cr, &v, vadd(foot, vmul(up, 0.08)), base, 0.110, 0.092, CHROME, 0.9);
	enum { N = 5 };
	static const double R[N] = { 0.074, 0.063, 0.052, 0.042, 0.033 };
	const double SEG = 1.10;
	double E = clampf(ext, 0.0, 1.0) * N * SEG;
	double lip = 0.092;
	for (int k = 0; k < N; k++) {
		double l = clampf(E - (N - 1 - k) * SEG, 0.0, SEG);
		if (l <= 1e-4) {
			continue;
		}
		vec3 top = vadd(base, vmul(up, l));
		cc_view_rod(cr, &v, base, top, R[k], R[k], CHROME, 0.9);
		/* the lip of the section it comes out of */
		cc_view_rod(cr, &v, vadd(base, vmul(up, -0.02)), vadd(base, vmul(up, 0.025)),
			lip, lip, CHROME, 0.6);
		lip = R[k] + 0.012;
		base = top;
	}
	cc_view_ball(cr, &v, vadd(base, vmul(up, 0.040)), 0.062, CHROME, 0.5);
}

/* The row of lenses along the front edge, coming up one after another. */
static void cc_lenses_paint(cairo_t *cr, struct room_render *rr, double ox, double oy,
	double scale, double warm, int screen_h, const struct desk_console_state *st)
{
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	double inner = (RM_CC_X1 - RM_CC_X0) * 0.5 - RM_CC_CHEEK;
	for (int i = 0; i < 6; i++) {
		double u = -inner + 0.70 + i * (2.0 * (inner - 0.70) / 5.0);
		/* where the machine is when it is out, like everything else on it */
		vec3 p = cc_world(&panel, u, 0.17, -panel.half + 0.136, 1.0, aspect);
		double pu, pv;
		if (!room_project(&rr->console_cam, p, &pu, &pv)) {
			continue;
		}
		double lit = clampf((warm - i * 0.11) / 0.22, 0.0, 1.0);
		double x = ox + pu * rr->w * scale, y = oy + pv * rr->h * scale;
		if (i & 1) {
			cc_lamp(cr, x, y, screen_h * 0.011, 0.30, 1.0, 0.40, lit * 0.85);
		} else {
			cc_lamp(cr, x, y, screen_h * 0.011, 1.0, 0.60, 0.18, lit * 0.85);
		}
	}
	/* and the guarded set's own, under the key's barrel: green, lit with the key home, and
	 * out with the set when one of the four is pushed */
	double key = st ? smoothstepf(0.66, 1.0, clampf(st->key, 0.0, 1.0)) : 0.0;
	double dying = st ? clampf(st->dying, 0.0, 1.0) : 0.0;
	vec3 lp = cc_world(&panel, CC_PW_LOCK_U, RM_CC_SKIN + 0.20, CC_PW_LAMP_S, 1.0, aspect);
	double pu, pv;
	if (key > 0.004 && room_project(&rr->console_cam, lp, &pu, &pv)) {
		cc_lamp(cr, ox + pu * rr->w * scale, oy + pv * rr->h * scale, screen_h * 0.014,
			0.30, 1.0, 0.40, key * (1.0 - smoothstepf(0.0, 0.30, dying)) * clampf(warm, 0, 1));
	}
}

static uint64_t key_mix(uint64_t h, const void *p, size_t n)
{
	const unsigned char *c = p;
	for (size_t i = 0; i < n; i++) {
		h = (h ^ c[i]) * 1099511628211ull;
	}
	return h;
}
#define KEY_ADD(h, v) do { __typeof__(v) key_v_ = (v); \
	(h) = key_mix((h), &key_v_, sizeof key_v_); } while (0)

/* ---- the lid, as it swings ----
 *
 * Shut, the lid is in the traced sheet like everything else on the set, and nothing is drawn
 * for it. Lifted, what is under it comes from the console's sheet traced with no lid
 * (rr->console_bare), and the lid is its own picture out of the first sheet -- what of it is
 * the lid's, by the ids, with the faces of the four on it printed on -- laid over that on the
 * plane it has got to on its hinge, darker as it turns from the lamp, with its free edge's
 * thickness and the shadow it throws. */

static struct {
	unsigned serial;            /* the room its box is of */
	int x0, y0, x1, y1;         /* where in the sheet the lid lies shut, as its ids have it */
	bool ok;
	cairo_surface_t *pic;       /* the lid out of the sheet, the box's size */
	uint64_t key;               /* what is printed on it */
} g_lid;

static bool cc_lid_id(int id)
{
	return id > 0 && id <= CC_COUNT && CC[id - 1].on == CC_ON_LID;
}

static bool cc_lid_box(struct room_render *rr)
{
	if (g_lid.serial == rr->serial) {
		return g_lid.ok;
	}
	if (g_lid.pic) {
		cairo_surface_destroy(g_lid.pic);
		g_lid.pic = NULL;
	}
	g_lid.serial = rr->serial;
	g_lid.ok = false;
	int x0 = rr->w, y0 = rr->h, x1 = -1, y1 = -1;
	for (int y = 0; rr->console_ids && y < rr->h; y++) {
		const unsigned char *row = rr->console_ids + (size_t)y * rr->w;
		for (int x = 0; x < rr->w; x++) {
			if (cc_lid_id(row[x])) {
				x0 = x < x0 ? x : x0;
				x1 = x > x1 ? x : x1;
				y0 = y < y0 ? y : y0;
				y1 = y > y1 ? y : y1;
			}
		}
	}
	if (x1 < x0 || y1 < y0) {
		return false;
	}
	/* a couple of pixels round it, for the edge the filter softened */
	g_lid.x0 = x0 > 2 ? x0 - 2 : 0;
	g_lid.y0 = y0 > 2 ? y0 - 2 : 0;
	g_lid.x1 = x1 + 3 < rr->w ? x1 + 3 : rr->w;
	g_lid.y1 = y1 + 3 < rr->h ? y1 + 3 : rr->h;
	g_lid.ok = true;
	return true;
}

static bool cc_lid_footprint(struct room_render *rr, int x, int y)
{
	return cc_lid_box(rr) && x >= g_lid.x0 && x < g_lid.x1 && y >= g_lid.y0 && y < g_lid.y1;
}

/* The lid as it lies shut, out of the sheet, with what is on it printed on. */
static cairo_surface_t *cc_lid_pic(struct room_render *rr, double aspect,
	const struct desk_console_state *st, const struct desk_meters *m, double warm)
{
	if (!cc_lid_box(rr)) {
		return NULL;
	}
	uint64_t key = 14695981039346656037ull;
	KEY_ADD(key, rr->serial);
	for (int i = CC_AIR; i <= CC_DISPLAY; i++) {
		KEY_ADD(key, desk_console_piece_key(i, st, -1, m));
	}
	if (g_lid.pic && g_lid.key == key) {
		return g_lid.pic;
	}
	if (g_lid.pic) {
		cairo_surface_destroy(g_lid.pic);
	}
	int w = g_lid.x1 - g_lid.x0, h = g_lid.y1 - g_lid.y0;
	cairo_surface_t *mask = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
	unsigned char *md = cairo_image_surface_get_data(mask);
	int ms = cairo_image_surface_get_stride(mask);
	for (int y = 0; y < h; y++) {
		const unsigned char *row = rr->console_ids + (size_t)(y + g_lid.y0) * rr->w + g_lid.x0;
		for (int x = 0; x < w; x++) {
			md[(size_t)y * ms + x] = cc_lid_id(row[x]) ? 255 : 0;
		}
	}
	cairo_surface_mark_dirty(mask);
	g_lid.pic = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	g_lid.key = key;
	cairo_t *cr = cairo_create(g_lid.pic);
	cairo_set_source_surface(cr, rr->console, -g_lid.x0, -g_lid.y0);
	cairo_mask_surface(cr, mask, 0, 0);
	/* and the faces of the four on it, as they are printed on the panel */
	cairo_push_group(cr);
	for (int i = CC_AIR; i <= CC_DISPLAY; i++) {
		cc_control_paint(cr, rr, -g_lid.x0, -g_lid.y0, 1.0, aspect, i, st, -1, m, warm);
	}
	cairo_pop_group_to_source(cr);
	cairo_mask_surface(cr, mask, 0, 0);
	cairo_destroy(cr);
	cairo_surface_destroy(mask);
	cairo_surface_flush(g_lid.pic);
	return g_lid.pic;
}

/* (where a point of the lid lands: in the sheet's pixels shut, on the screen swung) */
static bool cc_lid_points(const struct room_cam *cam, double open, double n, double aspect,
	double ox, double oy, double sw, double sh, double x[4], double y[4])
{
	vec3 q[4];
	cc_lid_corners(open, n, 1.0, aspect, q);
	for (int k = 0; k < 4; k++) {
		double u, v;
		if (!room_project(cam, q[k], &u, &v)) {
			return false;
		}
		x[k] = ox + u * sw;
		y[k] = oy + v * sh;
	}
	return true;
}

/* the affine map that takes three points to three others */
static bool cc_affine3(const double px[3], const double py[3], const double qx[3],
	const double qy[3], cairo_matrix_t *m)
{
	double ux = px[1] - px[0], uy = py[1] - py[0], vx = px[2] - px[0], vy = py[2] - py[0];
	double det = ux * vy - uy * vx;
	if (fabs(det) < 1e-9) {
		return false;
	}
	double ax = qx[1] - qx[0], ay = qy[1] - qy[0], bx = qx[2] - qx[0], by = qy[2] - qy[0];
	/* [a b] [u v]^-1 */
	double xx = (ax * vy - bx * uy) / det, xy = (bx * ux - ax * vx) / det;
	double yx = (ay * vy - by * uy) / det, yy = (by * ux - ay * vx) / det;
	cairo_matrix_init(m, xx, yx, xy, yy, qx[0] - (xx * px[0] + xy * py[0]),
		qy[0] - (yx * px[0] + yy * py[0]));
	return true;
}

/* What the console's sheet with the lid up (rr->console_bare) has that the first has not, out
 * of it once a room: what the lid was over -- the four under it --, the lid all the way up, its
 * underside and its free edge, and the shadow it throws. The shadow is a veil as dark as the
 * second sheet is darker than the first, over whatever is there: the lid is a sprite of its own,
 * over the others, and cannot multiply them. */
static struct {
	unsigned serial;
	bool ok;
	int fx0, fy0, fx1, fy1;
	cairo_surface_t *foot;
	int lx0, ly0, lx1, ly1;
	cairo_surface_t *lid;
	int sx0, sy0, sx1, sy1;
	cairo_surface_t *shade;
} g_lidup;

static void cc_lid_up_drop(void)
{
	cairo_surface_t **s[3] = { &g_lidup.foot, &g_lidup.lid, &g_lidup.shade };
	for (int i = 0; i < 3; i++) {
		if (*s[i]) {
			cairo_surface_destroy(*s[i]);
			*s[i] = NULL;
		}
	}
	g_lidup.ok = false;
}

static double cc_px_lum(const uint32_t *row, int x)
{
	uint32_t c = row[x];
	return 0.30 * ((c >> 16) & 255) + 0.59 * ((c >> 8) & 255) + 0.11 * (c & 255);
}

/* Is a point of the second sheet inside the lid's outline, all the way up: its top, its underside
 * and its four edges, the corners projected (tx/ty at CC_LID_N1, bx/by at CC_LID_N0). */
static bool cc_in_lid_up(const double tx[4], const double ty[4], const double bx[4],
	const double by[4], double x, double y)
{
	static const int SIDE[4][2] = { { 0, 1 }, { 2, 3 }, { 0, 2 }, { 1, 3 } };
	if (cc_in_quad(tx, ty, x, y) || cc_in_quad(bx, by, x, y)) {
		return true;
	}
	for (int k = 0; k < 4; k++) {
		int a = SIDE[k][0], b = SIDE[k][1];
		double qx[4] = { tx[a], tx[b], bx[a], bx[b] }, qy[4] = { ty[a], ty[b], by[a], by[b] };
		if (cc_in_quad(qx, qy, x, y)) {
			return true;
		}
	}
	return false;
}

static bool cc_lid_up(struct room_render *rr)
{
	cairo_surface_t *up;
	const unsigned char *up_ids;
	if (!rr->console || !rr->console_ids || !room_bare_get(rr, &up, &up_ids)
		|| !cc_lid_box(rr)) {
		return false;
	}
	if (g_lidup.serial == rr->serial && g_lidup.ok) {
		return true;
	}
	cc_lid_up_drop();
	g_lidup.serial = rr->serial;
	int w = rr->w, h = rr->h;
	cairo_surface_flush(rr->console);
	cairo_surface_flush(up);
	if (cairo_image_surface_get_width(up) != w || cairo_image_surface_get_height(up) != h) {
		return false;
	}
	const unsigned char *bd = cairo_image_surface_get_data(rr->console);
	const unsigned char *ud = cairo_image_surface_get_data(up);
	int bs = cairo_image_surface_get_stride(rr->console), us = cairo_image_surface_get_stride(up);
	const unsigned char *ids = rr->console_ids;
	const int LID = CC_LID + 1;

	/* what it was over: where the lid lay shut, two pixels round, and not the lid up */
	int fx0 = g_lid.x0, fy0 = g_lid.y0, fx1 = g_lid.x1, fy1 = g_lid.y1;
	int fw = fx1 - fx0, fh = fy1 - fy0;
	unsigned char *mask = calloc((size_t)fw * fh, 1);
	if (!mask) {
		return false;
	}
	for (int y = fy0; y < fy1; y++) {
		for (int x = fx0; x < fx1; x++) {
			if (!cc_lid_id(ids[(size_t)y * w + x])) {
				continue;
			}
			for (int yy = y - 2; yy <= y + 2; yy++) {
				for (int xx = x - 2; xx <= x + 2; xx++) {
					if (yy >= fy0 && yy < fy1 && xx >= fx0 && xx < fx1
						&& up_ids[(size_t)yy * w + xx] != LID) {
						mask[(size_t)(yy - fy0) * fw + (xx - fx0)] = 1;
					}
				}
			}
		}
	}
	g_lidup.foot = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, fw, fh);
	unsigned char *fd = cairo_image_surface_get_data(g_lidup.foot);
	int fs = cairo_image_surface_get_stride(g_lidup.foot);
	for (int y = 0; y < fh; y++) {
		const uint32_t *src = (const uint32_t *)(ud + (size_t)(y + fy0) * us) + fx0;
		uint32_t *dst = (uint32_t *)(fd + (size_t)y * fs);
		for (int x = 0; x < fw; x++) {
			dst[x] = mask[(size_t)y * fw + x] ? src[x] : 0u;
		}
	}
	cairo_surface_mark_dirty(g_lidup.foot);
	g_lidup.fx0 = fx0;
	g_lidup.fy0 = fy0;
	g_lidup.fx1 = fx1;
	g_lidup.fy1 = fy1;

	/* The lid up: what the second sheet's owners say is the lid, and everything inside its
	 * outline besides. The end grain of its free edge lies on the very edge of the box that says
	 * what is the lid, and near its foot the knob's and the fader's boxes, asked before the
	 * lid's, reach out over it: a strip along its top and lines across its foot were left out of
	 * it, and the knob showed through them under the lid's shadow (2026-11-20, the owner:
	 * 開けたときに上のほうが描画切れ). The sheet itself has the lid there; it is only which
	 * pixels are the lid's that was wrong. */
	unsigned char *in = calloc((size_t)w * h, 1);
	if (!in) {
		free(mask);
		return false;
	}
	double aspect = (double)w / (double)(h > 0 ? h : 1);
	double tx[4], ty[4], bx[4], by[4];
	if (cc_lid_points(&rr->console_cam, 1.0, CC_LID_N1, aspect, 0.0, 0.0, w, h, tx, ty)
		&& cc_lid_points(&rr->console_cam, 1.0, CC_LID_N0, aspect, 0.0, 0.0, w, h, bx, by)) {
		double hx0 = 1e9, hy0 = 1e9, hx1 = -1e9, hy1 = -1e9;
		for (int k = 0; k < 4; k++) {
			hx0 = fmin(hx0, fmin(tx[k], bx[k]));
			hy0 = fmin(hy0, fmin(ty[k], by[k]));
			hx1 = fmax(hx1, fmax(tx[k], bx[k]));
			hy1 = fmax(hy1, fmax(ty[k], by[k]));
		}
		int ix0 = (int)fmax(0.0, floor(hx0)), iy0 = (int)fmax(0.0, floor(hy0));
		int ix1 = (int)fmin((double)w, ceil(hx1)), iy1 = (int)fmin((double)h, ceil(hy1));
		for (int y = iy0; y < iy1; y++) {
			for (int x = ix0; x < ix1; x++) {
				if (cc_in_lid_up(tx, ty, bx, by, x + 0.5, y + 0.5)) {
					in[(size_t)y * w + x] = 1;
				}
			}
		}
	}
	int lx0 = w, ly0 = h, lx1 = -1, ly1 = -1;
	for (int y = 0; y < h; y++) {
		const unsigned char *row = up_ids + (size_t)y * w;
		unsigned char *inr = in + (size_t)y * w;
		for (int x = 0; x < w; x++) {
			if (row[x] == LID) {
				inr[x] = 1;
			}
			if (inr[x]) {
				lx0 = x < lx0 ? x : lx0;
				lx1 = x > lx1 ? x : lx1;
				ly0 = y < ly0 ? y : ly0;
				ly1 = y > ly1 ? y : ly1;
			}
		}
	}
	if (lx1 < lx0) {
		free(in);
		free(mask);
		cc_lid_up_drop();
		return false;
	}
	lx1++;
	ly1++;
	g_lidup.lid = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, lx1 - lx0, ly1 - ly0);
	unsigned char *ld = cairo_image_surface_get_data(g_lidup.lid);
	int ls = cairo_image_surface_get_stride(g_lidup.lid);
	for (int y = ly0; y < ly1; y++) {
		const uint32_t *src = (const uint32_t *)(ud + (size_t)y * us);
		uint32_t *dst = (uint32_t *)(ld + (size_t)(y - ly0) * ls);
		for (int x = lx0; x < lx1; x++) {
			dst[x - lx0] = in[(size_t)y * w + x] ? src[x] : 0u;
		}
	}
	cairo_surface_mark_dirty(g_lidup.lid);
	g_lidup.lx0 = lx0;
	g_lidup.ly0 = ly0;
	g_lidup.lx1 = lx1;
	g_lidup.ly1 = ly1;

	/* its shadow: round both, as far again as the lid up is big, wherever the same thing is
	 * darker in the second sheet */
	int ux0 = lx0 < fx0 ? lx0 : fx0, uy0 = ly0 < fy0 ? ly0 : fy0;
	int ux1 = lx1 > fx1 ? lx1 : fx1, uy1 = ly1 > fy1 ? ly1 : fy1;
	int mx = (ux1 - ux0) / 2, my = (uy1 - uy0) / 2;
	int sx0 = ux0 - mx > 0 ? ux0 - mx : 0, sy0 = uy0 - my > 0 ? uy0 - my : 0;
	int sx1 = ux1 + mx < w ? ux1 + mx : w, sy1 = uy1 + my < h ? uy1 + my : h;
	int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
	unsigned char *veil = calloc((size_t)(sx1 - sx0) * (sy1 - sy0), 1);
	if (!veil) {
		free(in);
		free(mask);
		return false;
	}
	for (int y = sy0; y < sy1; y++) {
		const uint32_t *br = (const uint32_t *)(bd + (size_t)y * bs);
		const uint32_t *ur = (const uint32_t *)(ud + (size_t)y * us);
		for (int x = sx0; x < sx1; x++) {
			size_t k = (size_t)y * w + x;
			if (ids[k] != up_ids[k] || in[k]
				|| (x >= fx0 && x < fx1 && y >= fy0 && y < fy1
					&& mask[(size_t)(y - fy0) * fw + (x - fx0)])) {
				continue;
			}
			double lb = cc_px_lum(br, x), lu = cc_px_lum(ur, x);
			if (lb < 6.0 || lu > lb * 0.985) {
				continue;
			}
			double a = clampf(1.0 - lu / lb, 0.0, 0.92);
			veil[(size_t)(y - sy0) * (sx1 - sx0) + (x - sx0)] = (unsigned char)lround(a * 255.0);
			bx0 = x < bx0 ? x : bx0;
			bx1 = x > bx1 ? x : bx1;
			by0 = y < by0 ? y : by0;
			by1 = y > by1 ? y : by1;
		}
	}
	free(mask);
	free(in);
	if (bx1 >= bx0) {
		bx1++;
		by1++;
		g_lidup.shade = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bx1 - bx0, by1 - by0);
		unsigned char *sd = cairo_image_surface_get_data(g_lidup.shade);
		int ss = cairo_image_surface_get_stride(g_lidup.shade);
		for (int y = by0; y < by1; y++) {
			uint32_t *dst = (uint32_t *)(sd + (size_t)(y - by0) * ss);
			for (int x = bx0; x < bx1; x++) {
				unsigned a = veil[(size_t)(y - sy0) * (sx1 - sx0) + (x - sx0)];
				dst[x - bx0] = (uint32_t)a << 24;   /* black, premultiplied */
			}
		}
		cairo_surface_mark_dirty(g_lidup.shade);
		g_lidup.sx0 = bx0;
		g_lidup.sy0 = by0;
		g_lidup.sx1 = bx1;
		g_lidup.sy1 = by1;
	}
	free(veil);
	g_lidup.ok = true;
	return true;
}

/* (which way round a quad's corners go, in binary order: its top seen from in front or
 * from behind) */
static double cc_quad_turn(const double x[4], const double y[4])
{
	return (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
}

static void cc_lid_layer(cairo_t *cr, double ox, double oy, double scale, cairo_surface_t *s,
	int x, int y, double alpha)
{
	if (!s || alpha <= 0.0) {
		return;
	}
	cairo_save(cr);
	cairo_translate(cr, ox, oy);
	cairo_scale(cr, scale, scale);
	cairo_set_source_surface(cr, s, x, y);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint_with_alpha(cr, alpha);
	cairo_restore(cr);
}

static void cc_lid_paint(cairo_t *cr, struct room_render *rr, double ox, double oy,
	double scale, double aspect, const struct desk_console_state *st, int hover,
	const struct desk_meters *m, double warm)
{
	double open = st ? clampf(st->guard, 0.0, 1.0) : 0.0;
	if (open <= 0.0005 || !cc_lid_up(rr)) {
		return;
	}
	double sw = rr->w * scale, sh = rr->h * scale;
	const struct room_cam *cam = &rr->console_cam;
	static const int UNDER[4] = { CC_PW_OFF, CC_PW_BOOT, CC_PW_OUT, CC_PW_SUSPEND };

	/* what is under it, where it lay, and the faces of the four there */
	cc_lid_layer(cr, ox, oy, scale, g_lidup.foot, g_lidup.fx0, g_lidup.fy0, 1.0);
	for (int k = 0; k < 4; k++) {
		cc_control_paint(cr, rr, ox, oy, scale, aspect, UNDER[k], st, hover, m, warm);
	}
	if (open >= 0.999) {
		/* all the way up: as the second sheet has it, its shadow and all */
		cc_lid_layer(cr, ox, oy, scale, g_lidup.shade, g_lidup.sx0, g_lidup.sy0, 1.0);
		cc_lid_layer(cr, ox, oy, scale, g_lidup.lid, g_lidup.lx0, g_lidup.ly0, 1.0);
		return;
	}

	double th = open * CC_LID_SWING;
	/* the shade it stands over: against the hinge, as it comes up between the lamp and
	 * what is under it; and, near the top, its shadow as the second sheet has it */
	{
		double tx[4], ty[4];
		if (cc_lid_points(cam, 0.0, RM_CC_SKIN + 0.04, aspect, ox, oy, sw, sh, tx, ty)) {
			cairo_save(cr);
			cairo_move_to(cr, tx[0], ty[0]);
			cairo_line_to(cr, tx[1], ty[1]);
			cairo_line_to(cr, tx[3], ty[3]);
			cairo_line_to(cr, tx[2], ty[2]);
			cairo_close_path(cr);
			cairo_clip(cr);
			double a = fmax(sin(th), 0.0) * (1.0 - open);
			cairo_pattern_t *g = cairo_pattern_create_linear((tx[0] + tx[1]) * 0.5,
				(ty[0] + ty[1]) * 0.5, (tx[2] + tx[3]) * 0.5, (ty[2] + ty[3]) * 0.5);
			cairo_pattern_add_color_stop_rgba(g, 0.0, 0.02, 0.015, 0.01, 0.30 * a);
			cairo_pattern_add_color_stop_rgba(g, 0.55, 0.02, 0.015, 0.01, 0.06 * a);
			cairo_pattern_add_color_stop_rgba(g, 1.0, 0.02, 0.015, 0.01, 0.0);
			cairo_set_source(cr, g);
			cairo_paint(cr);
			cairo_pattern_destroy(g);
			cairo_restore(cr);
		}
		double near = clampf((open - 0.55) / 0.45, 0.0, 1.0);
		cc_lid_layer(cr, ox, oy, scale, g_lidup.shade, g_lidup.sx0, g_lidup.sy0, near * near);
	}

	/* the lid: its top from the first sheet while it is the top that faces this way, and past
	 * upright its underside from the second, laid where its corners have got to */
	double px[4], py[4], qx[4], qy[4];
	if (!cc_lid_points(cam, 0.0, CC_LID_N1, aspect, -g_lid.x0, -g_lid.y0, rr->w, rr->h, px, py)
		|| !cc_lid_points(cam, open, CC_LID_N1, aspect, ox, oy, sw, sh, qx, qy)) {
		return;
	}
	bool top = cc_quad_turn(px, py) * cc_quad_turn(qx, qy) > 0.0;
	if (!top) {
		double ax[4], ay[4], bx[4], by[4];
		cairo_matrix_t mt;
		if (cc_lid_points(cam, 1.0, CC_LID_N0, aspect, -g_lidup.lx0, -g_lidup.ly0, rr->w,
			rr->h, ax, ay) && cc_lid_points(cam, open, CC_LID_N0, aspect, ox, oy, sw, sh, bx,
			by) && cc_affine3(ax, ay, bx, by, &mt)) {
			cairo_save(cr);
			cairo_transform(cr, &mt);
			cairo_set_source_surface(cr, g_lidup.lid, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
			cairo_paint(cr);
			cairo_restore(cr);
		}
		return;
	}
	cairo_surface_t *pic = cc_lid_pic(rr, aspect, st, m, warm);
	cairo_matrix_t mt;
	if (!pic || !cc_affine3(px, py, qx, qy, &mt)) {
		return;
	}
	cairo_save(cr);
	cairo_transform(cr, &mt);
	cairo_set_source_surface(cr, pic, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	/* turned from the lamp, and the room's light off it, as it comes up */
	cairo_set_source_rgba(cr, 0.03, 0.02, 0.012, 0.42 * sin(th) * sin(th));
	cairo_mask_surface(cr, pic, 0, 0);
	cairo_restore(cr);

	/* its free edge, which you look at the end grain of as it comes up, and the brass lip */
	double bx[4], by[4];
	if (th > 0.05 && cc_lid_points(cam, open, CC_LID_N0, aspect, ox, oy, sw, sh, bx, by)) {
		cairo_save(cr);
		cairo_move_to(cr, qx[2], qy[2]);
		cairo_line_to(cr, qx[3], qy[3]);
		cairo_line_to(cr, bx[3], by[3]);
		cairo_line_to(cr, bx[2], by[2]);
		cairo_close_path(cr);
		cairo_pattern_t *g = cairo_pattern_create_linear((qx[2] + qx[3]) * 0.5,
			(qy[2] + qy[3]) * 0.5, (bx[2] + bx[3]) * 0.5, (by[2] + by[3]) * 0.5);
		cairo_pattern_add_color_stop_rgb(g, 0.0, 0.40, 0.25, 0.14);
		cairo_pattern_add_color_stop_rgb(g, 1.0, 0.20, 0.12, 0.065);
		cairo_set_source(cr, g);
		cairo_fill_preserve(cr);
		cairo_pattern_destroy(g);
		cairo_set_line_width(cr, fmax(1.0, sh * 0.0012));
		cairo_set_source_rgba(cr, 0.06, 0.035, 0.015, 0.75);
		cairo_stroke(cr);
		/* the lip, along the top of it */
		double lx0 = qx[2] + (qx[3] - qx[2]) * 0.40, ly0 = qy[2] + (qy[3] - qy[2]) * 0.40;
		double lx1 = qx[2] + (qx[3] - qx[2]) * 0.60, ly1 = qy[2] + (qy[3] - qy[2]) * 0.60;
		cairo_move_to(cr, lx0, ly0);
		cairo_line_to(cr, lx1, ly1);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_width(cr, fmax(1.6, sh * 0.0042));
		cairo_set_source_rgb(cr, 0.80, 0.64, 0.34);
		cairo_stroke(cr);
		cairo_restore(cr);
	}
}

static void cc_lid_box_screen(struct room_render *rr, double ox, double oy, double scale,
	double aspect, double box[4])
{
	double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
	if (cc_lid_box(rr)) {
		x0 = ox + g_lid.x0 * scale;
		y0 = oy + g_lid.y0 * scale;
		x1 = ox + g_lid.x1 * scale;
		y1 = oy + g_lid.y1 * scale;
	}
	/* and the shadow it throws, up, as the console's other sheet has it */
	if (cc_lid_up(rr) && g_lidup.shade) {
		x0 = fmin(x0, ox + g_lidup.sx0 * scale);
		y0 = fmin(y0, oy + g_lidup.sy0 * scale);
		x1 = fmax(x1, ox + g_lidup.sx1 * scale);
		y1 = fmax(y1, oy + g_lidup.sy1 * scale);
	}
	/* and wherever it swings to: its top and its underside, all the way up */
	for (int t = 0; t <= 8; t++) {
		for (int nn = 0; nn < 2; nn++) {
			double x[4], y[4];
			if (!cc_lid_points(&rr->console_cam, t / 8.0, nn ? CC_LID_N1 : CC_LID_N0, aspect, ox,
				oy, rr->w * scale, rr->h * scale, x, y)) {
				continue;
			}
			for (int k = 0; k < 4; k++) {
				x0 = fmin(x0, x[k]);
				y0 = fmin(y0, y[k]);
				x1 = fmax(x1, x[k]);
				y1 = fmax(y1, y[k]);
			}
		}
	}
	box[0] = floor(x0) - 4.0;
	box[1] = floor(y0) - 4.0;
	box[2] = ceil(x1) + 4.0 - box[0];
	box[3] = ceil(y1) + 4.0 - box[1];
}

/* One piece of the set's live half: a control, the nameplate, the row of
 * lenses, or the lid as it swings. The compositor keeps each as a sprite of its own
 * and redraws it only when its key moves; the preview paints them all. */
static void console_piece_paint(cairo_t *cr, int screen_h, struct room_render *rr,
	double ox, double oy, double scale, double aspect, int p,
	const struct desk_console_state *st, int hover, const struct desk_meters *m,
	double warm)
{
	if (p < CC_COUNT) {
		/* what is under the lid is drawn with it, as it is up (cc_lid_paint), and what is on
		 * it, as it swings; the lid itself is in the sheet */
		double open = st ? st->guard : 0.0;
		if (p == CC_LID || CC[p].on == CC_ON_UNDER || (CC[p].on == CC_ON_LID && open > 0.0005)) {
			return;
		}
		cc_control_paint(cr, rr, ox, oy, scale, aspect, p, st, hover, m, warm);
	} else if (p == DESK_CCP_PLATE) {
		cc_nameplate(cr, &rr->console_cam, ox, oy, rr->w * scale, rr->h * scale,
			1.0, aspect, 1.0);
	} else if (p == DESK_CCP_LENSES) {
		cc_lenses_paint(cr, rr, ox, oy, scale, warm, screen_h, st);
	} else if (p == DESK_CCP_GUARD) {
		cc_lid_paint(cr, rr, ox, oy, scale, aspect, st, hover, m, warm);
	} else if (p == DESK_CCP_ANTENNA) {
		cc_antenna(cr, rr, ox, oy, scale, aspect, st ? st->antenna : 1.0);
	}
}

static void console_paint(cairo_t *cr, int screen_w, int screen_h,
	const struct desk_console_state *st, int hover, bool live)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!console_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		desk_scene_warm(screen_w, screen_h, true);
		return;
	}
	struct lamp_px lamp_was = lamp_view(screen_w, screen_h);

	double gx0, gy0, gx1, gy1;
	cc_screen_box(rr, &rr->console_cam, ox, oy, rr->w * scale, rr->h * scale,
		&gx0, &gy0, &gx1, &gy1);
	cairo_save(cr);
	cairo_translate(cr, ox, oy);
	cairo_scale(cr, scale, scale);
	cairo_set_source_surface(cr, rr->console, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_restore(cr);
	/* Grain belongs to the sheet, not to the needles: over the live half
	 * it was a tiled paint across the machine on every frame. */
	overlay_grain(cr, gx0, gy0, gx1 - gx0, gy1 - gy0, 0.55);
	if (!live) {
		g_light = lamp_was;
		return;
	}

	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	struct desk_meters m;
	desk_meters_read(&m);
	double warm = clampf(st ? st->warm : 1.0, 0.0, 1.0);
	cc_trace_step(m.cpu, st ? st->clock : 0.0);
	for (int p = 0; p < DESK_CCP_COUNT; p++) {
		console_piece_paint(cr, screen_h, rr, ox, oy, scale, aspect, p, st, hover, &m, warm);
	}
	g_light = lamp_was;
}
