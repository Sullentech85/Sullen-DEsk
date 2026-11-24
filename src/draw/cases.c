/* Part of draw.c: what is under the pointer, and the cases of the things on the desk.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- what is under the pointer ---- */

/* The face on the screen, as the desk has it with the drawer where it is:
 * face pixels to screen pixels. */
/* Where the desk's frame will land on a screen before any room for it has
 * been made -- what room_metrics and room_xform work out, on the camera
 * alone, which needs no tracing: so that the clock can count the room
 * being made from where it will stand. */
static bool room_frame_guess(int sw, int sh, struct room_cam *cam, double *ox, double *oy,
	double *fw, double *fh)
{
	if (sw < 8 || sh < 8) {
		return false;
	}
	int cw, ch, hq;
	room_canon(sw, sh, &cw, &ch, &hq);
	double aspect = (double)cw / ch, hx = hq * 0.5;
	room_cam_plan_at(aspect, hx, cam);
	double u, v_far = 0.5, v_near = 0.5;
	room_project(cam, v3(0, 0, RM_ZN - 2.0 * hx / aspect), &u, &v_far);
	room_project(cam, v3(0, 0, RM_ZN), &u, &v_near);
	double zoom = 1.0 / fmax(v_near - v_far, 1e-6);
	double pan = zoom * (0.5 - (v_far + v_near) * 0.5);
	int dh = (int)(sh * zoom + 0.5);
	int y0 = (int)((sh - dh) / 2 + pan * sh);
	double cover = ((double)sw / sh) / aspect;
	if (cover > 1.0) {
		dh = (int)(dh * cover + 0.5);
	}
	int dw = (int)((double)cw * dh / ch + 0.5);
	*ox = (sw - dw) / 2;
	*oy = y0;
	*fw = (double)cw * dh / ch;
	*fh = dh;
	return true;
}

static bool clk_face_matrix(int w, int h, double drawer_open, cairo_matrix_t *m)
{
	struct room_xf xf;
	const struct room_gadget *g = &ROOM_GADGETS[DESK_GADGET_CLOCK];
	vec3 q[4];
	if (!room_xform(w, h, drawer_open, &xf)) {
		/* no room yet: where it will be when there is one */
		struct room_cam cam;
		double ox, oy, fw, fh;
		if (!room_frame_guess(w, h, &cam, &ox, &oy, &fw, &fh)) {
			return false;
		}
		gadget_face_quad(g, q);
		return quad_matrix(&cam, q, g->face_w, g->face_h, ox, oy, fw, fh, m) != 0;
	}
	gadget_face_quad(g, q);
	return quad_matrix(&xf.rr->cam, q, g->face_w, g->face_h, xf.ox, xf.oy,
		xf.rr->w * xf.scale, xf.rr->h * xf.scale, m) != 0;
}

/* ---- the things on the desk, rendered -----------------------------
 *
 * The clock's case, the post box and the Control Panel, stowed, are not
 * traced with the desk: each is a picture rendered once, in Blender
 * (art/case.py, with art/clock/clock.py, art/post/post.py and
 * art/console/console.py), the same thing in the same light through the
 * same camera, with what the tracer has no time for -- Bakelite marbled
 * right through, enamel chipped to the iron, brass tarnished where nobody
 * touches it, walnut with a grain, the light the desk throws back into each
 * -- and its shadow on the desk with it. They are laid over the desk's sheet, and what is printed
 * on them is printed over them. Only the moves, and the views they end in,
 * trace them still.
 *
 * How much of a thing's front and side shows, how the lamp falls on it and
 * where its shadow goes follow the shape of the screen, and -- for the two
 * at the back corners, which move out with the corners of a wider desk --
 * the width of the desk; so each is rendered for a dozen shapes, and for some widths,
 * and the ones either side of this screen are mixed. Each picture is laid
 * a piece at a time, a piece to a plane of the thing -- its top, its front,
 * its side, the desk at its foot -- each plane through where four of its
 * corners were seen when it was rendered (through the camera of the room it
 * was rendered for, room_cam_plan_at) and are seen now: a plane seen from
 * one place is the same plane seen from another, through a homography.
 * Cairo lays a picture through a matrix, not a homography, so each piece
 * is laid in cells, small enough that one matrix is within a tenth of a
 * pixel of it. A piece has the part of the frame its outline covers, less
 * what the pieces before it have, in whole pixels -- a pixel half one
 * piece's and half the next's would let the desk under it show through --
 * and the last, the desk, has the rest. The room is traced as wide as the
 * machine affords, and the pictures go to the screen's own pixels, laid
 * once for where they land and kept.
 *
 * Each thing's pictures are PNGs one after another in a file of DEsk's data
 * (art/<thing>/<thing>-case.bin, installed in share/desk/<thing>), where
 * each is is in its .h, built in; read out of it a picture at a time, the
 * ones this shape of screen wants. */

#define ROOM_CASE_PIECES 12

struct room_case_bake {
	double aspect, hx;      /* the shape of screen, and the desk's half width */
	unsigned off, len;      /* its picture, a PNG, in the .bin */
	int w, h;
	double u0, v0, u1, v1;  /* where that is in the room's frame */
};

#include "clock-case.h"
#include "post-case.h"
#include "console-case.h"

/* A piece of a thing's picture: the plane it is laid by -- four corners on
 * it -- and the outline of the part of the frame it has: its corners in
 * order round it, or a hull round them; none for the rest, which is the
 * last piece's. */
struct case_piece {
	vec3 q[4];
	int n;
	bool hull;
	vec3 p[8];
};

struct room_case {
	const struct room_case_bake *bake;
	int n_aspect, n_hx;     /* in rows of n_aspect shapes, a row to a desk */
	const char *kind, *file, *what;   /* where its pictures are, and what it is */
	bool gone;              /* they are not there, or would not read: not looked for again */
	/* its pieces, for a screen this shape over a desk this wide */
	int (*pieces)(double aspect, double hx, struct case_piece *pc);
	/* each picture, read out of the .bin when it is wanted and let go when
	 * another shape of screen wants others; and the pictures as last laid,
	 * in the target's pixels, and what for */
	cairo_surface_t **pic;
	cairo_surface_t *img;
	int x, y;
	double key[14];
};

static void case_quad(struct case_piece *pc, vec3 a, vec3 b, vec3 c, vec3 d)
{
	pc->q[0] = a;
	pc->q[1] = b;
	pc->q[2] = c;
	pc->q[3] = d;
}

/* The clock: its top, its front, the desk at its foot. Where it stands is
 * gadget_z's, for this screen rather than the room the globals were last
 * left at. */
static int clock_case_pieces(double aspect, double hx, struct case_piece *pc)
{
	const struct room_gadget *g = &ROOM_GADGETS[DESK_GADGET_CLOCK];
	double a = fmax(aspect, 0.2);
	double gz = RM_ZN - 2.0 * hx / a + (RM_FOV_X_V * 0.5 / a) * g->h + g->z;
	double x0 = g->x - g->hx, x1 = g->x + g->hx, zb = gz - g->hz, zf = gz + g->hz;
	pc[0] = (struct case_piece){ .n = 4,
		.p = { v3(x0, g->h, zb), v3(x1, g->h, zb), v3(x1, g->h, zf), v3(x0, g->h, zf) } };
	memcpy(pc[0].q, pc[0].p, sizeof pc[0].q);
	pc[1] = (struct case_piece){ .n = 4,
		.p = { v3(x0, g->h, zf), v3(x1, g->h, zf), v3(x1, 0.0, zf), v3(x0, 0.0, zf) } };
	memcpy(pc[1].q, pc[1].p, sizeof pc[1].q);
	pc[2] = (struct case_piece){ .n = 0 };
	case_quad(&pc[2], v3(x0 - 2.0, 0.0, zb), v3(x1 + 2.0, 0.0, zb),
		v3(x1 + 2.0, 0.0, zf + 2.0), v3(x0 - 2.0, 0.0, zf + 2.0));
	return 3;
}

/* Where the post box stands for a screen this shape over a desk this wide:
 * RM_PB_CX and RM_PB_CZ, without the room's globals. */
static void pb_place_at(double aspect, double hx, double *cx, double *cz)
{
	double a = fmax(aspect, 0.2);
	*cx = RM_PB_X0 + (hx - RM_HX_MIN);
	*cz = RM_ZN - 2.0 * hx / a + RM_PB_TOP_DZ + RM_PB_TOP_GAP
		+ (RM_FOV_X_V * 0.5 / a) * RM_PB_TOP_Y;
}

/* pb_cap_to_world, for a box standing at cx, cz */
static vec3 pb_cap_at(double cx, double cz, double u, double n, double s)
{
	vec3 q = rot_x(v3(u, n, s), RM_PB_TILT);
	return vadd(q, v3(cx, RM_PB_SHOULDER + RM_PB_CAP_T * 0.5, cz));
}

/* The post box: its cap; the door's face; its left side, which the eye, to
 * the left of it, sees; and the desk. */
static int post_case_pieces(double aspect, double hx, struct case_piece *pc)
{
	double cx, cz;
	pb_place_at(aspect, hx, &cx, &cz);
	const double ux = RM_PB_HX + 0.14, un = RM_PB_CAP_T * 0.5, us = RM_PB_HZ + 0.14;
	double xl = cx - RM_PB_HX, xr = cx + RM_PB_HX;
	double zf = cz + RM_PB_HZ, zd = zf + 0.10, zb = cz - RM_PB_HZ;
	double y0 = RM_PB_PLINTH, y1 = RM_PB_SHOULDER;
	/* the cap: laid by its top, and the whole of it a hull round its
	 * corners */
	pc[0] = (struct case_piece){ .n = 8, .hull = true };
	case_quad(&pc[0], pb_cap_at(cx, cz, -ux, un, -us), pb_cap_at(cx, cz, ux, un, -us),
		pb_cap_at(cx, cz, ux, un, us), pb_cap_at(cx, cz, -ux, un, us));
	for (int k = 0; k < 8; k++) {
		pc[0].p[k] = pb_cap_at(cx, cz, (k & 1) ? ux : -ux, (k & 2) ? un : -un,
			(k & 4) ? us : -us);
	}
	/* the front, laid by the door's face */
	pc[1] = (struct case_piece){ .n = 4,
		.p = { v3(xl, y1, zf), v3(xr, y1, zf), v3(xr, y0, zf), v3(xl, y0, zf) } };
	case_quad(&pc[1], v3(xl, y1, zd), v3(xr, y1, zd), v3(xr, y0, zd), v3(xl, y0, zd));
	/* the side */
	pc[2] = (struct case_piece){ .n = 4,
		.p = { v3(xl, y0, zf), v3(xl, y1, zf), v3(xl, y1, zb), v3(xl, y0, zb) } };
	memcpy(pc[2].q, pc[2].p, sizeof pc[2].q);
	/* the desk */
	pc[3] = (struct case_piece){ .n = 0 };
	case_quad(&pc[3], v3(xl - 2.0, 0.0, zb), v3(xr + 2.0, 0.0, zb),
		v3(xr + 2.0, 0.0, zf + 2.0), v3(xl - 2.0, 0.0, zf + 2.0));
	return 4;
}

/* A point on the Control Panel's operating panel, in its frame (across it,
 * out of it, up it), for a set from x0 to x1 carried dz: cc_world, without
 * the room's globals. */
static vec3 cc_panel_at(double x0, double x1, double dz, double u, double n, double s)
{
	struct cc_face f;
	cc_face_build(RM_CC_PY0, RM_CC_PZ0, RM_CC_PY1, RM_CC_PZ1, &f);
	f.o.x = (x0 + x1) * 0.5;
	return vadd(cc_to_world(&f, u, n, s), v3(0.0, 0.0, dz));
}

/* The Control Panel, stowed: its cheeks -- the right one's side, which the
 * eye, to the right of it, sees; both fronts and both tops; and the inside
 * of the left one, over the panel -- its panel, the ledge in front and the
 * plinth, and the desk. */
static int console_case_pieces(double aspect, double hx, struct case_piece *pc)
{
	double g = hx - RM_HX_MIN;
	double x0 = RM_CC_X0_0 - g, x1 = RM_CC_X1_0 - g, dz = -cc_stow_for(aspect, hx);
	double xc0 = x0 + RM_CC_CHEEK, xc1 = x1 - RM_CC_CHEEK;
	double inner = (x1 - x0) * 0.5 - RM_CC_CHEEK;
	double zf = RM_CC_PZ0 + dz, zb = RM_CC_PZ1 + dz;
	double y0 = RM_CC_PLINTH, y1 = RM_CC_PY1;
	struct cc_face f;
	cc_face_build(RM_CC_PY0, RM_CC_PZ0, RM_CC_PY1, RM_CC_PZ1, &f);
	/* the cheeks: the right one's side, its front and top, the left one's
	 * front and top */
	const vec3 face[5][4] = {
		{ v3(x1, y0, zf), v3(x1, y1, zf), v3(x1, y1, zb), v3(x1, y0, zb) },
		{ v3(xc1, y0, zf), v3(x1, y0, zf), v3(x1, y1, zf), v3(xc1, y1, zf) },
		{ v3(xc1, y1, zf), v3(x1, y1, zf), v3(x1, y1, zb), v3(xc1, y1, zb) },
		{ v3(x0, y0, zf), v3(xc0, y0, zf), v3(xc0, y1, zf), v3(x0, y1, zf) },
		{ v3(x0, y1, zf), v3(xc0, y1, zf), v3(xc0, y1, zb), v3(x0, y1, zb) },
	};
	for (int k = 0; k < 5; k++) {
		pc[k] = (struct case_piece){ .n = 4 };
		memcpy(pc[k].q, face[k], sizeof face[k]);
		memcpy(pc[k].p, face[k], sizeof face[k]);
	}
	/* the left one's inside, as much of it as stands over the panel: from
	 * where the panel's skin meets its front to where it meets its top */
	double sf = (RM_CC_PZ0 - f.o.z - f.an.z * RM_CC_SKIN) / f.as.z;
	double st = (y1 - f.o.y - f.an.y * RM_CC_SKIN) / f.as.y;
	pc[5] = (struct case_piece){ .n = 3, .p = { cc_panel_at(x0, x1, dz, -inner, RM_CC_SKIN, sf),
		v3(xc0, y1, zf), cc_panel_at(x0, x1, dz, -inner, RM_CC_SKIN, st) } };
	case_quad(&pc[5], v3(xc0, y0, zf), v3(xc0, y1, zf), v3(xc0, y1, zb), v3(xc0, y0, zb));
	/* the panel, its skin from lip to top */
	pc[6] = (struct case_piece){ .n = 4 };
	case_quad(&pc[6], cc_panel_at(x0, x1, dz, -inner, RM_CC_SKIN, -f.half),
		cc_panel_at(x0, x1, dz, inner, RM_CC_SKIN, -f.half),
		cc_panel_at(x0, x1, dz, inner, RM_CC_SKIN, f.half),
		cc_panel_at(x0, x1, dz, -inner, RM_CC_SKIN, f.half));
	memcpy(pc[6].p, pc[6].q, sizeof pc[6].q);
	/* the ledge, and the plinth: each laid by its top, a hull round its
	 * corners */
	double lx0 = RM_CC_PLATE_X0_0 - g + 0.10, lx1 = RM_CC_LEDGE_X1_0 - g;
	double lz0 = RM_CC_PZ0 + RM_CC_LEDGE + dz, lz1 = RM_CC_PLINTH_Z0 - 0.20 + dz;
	double px0 = x0 + 0.10, px1 = x1 - 0.10;
	double pz0 = RM_CC_PLINTH_Z0 + dz, pz1 = RM_CC_ZB + 0.30 + dz;
	pc[7] = (struct case_piece){ .n = 8, .hull = true };
	pc[8] = (struct case_piece){ .n = 8, .hull = true };
	case_quad(&pc[7], v3(lx0, y0, lz1), v3(lx1, y0, lz1), v3(lx1, y0, lz0), v3(lx0, y0, lz0));
	case_quad(&pc[8], v3(px0, y0, pz1), v3(px1, y0, pz1), v3(px1, y0, pz0), v3(px0, y0, pz0));
	for (int k = 0; k < 8; k++) {
		double y = (k & 2) ? y0 : 0.0;
		pc[7].p[k] = v3((k & 1) ? lx1 : lx0, y, (k & 4) ? lz1 : lz0);
		pc[8].p[k] = v3((k & 1) ? px1 : px0, y, (k & 4) ? pz1 : pz0);
	}
	/* and the desk */
	pc[9] = (struct case_piece){ .n = 0 };
	case_quad(&pc[9], v3(x0 - 3.0, 0.0, zb), v3(x1 + 2.0, 0.0, zb),
		v3(x1 + 2.0, 0.0, lz0 + 2.0), v3(x0 - 3.0, 0.0, lz0 + 2.0));
	return 10;
}

static cairo_surface_t *g_clock_case_pic[CLOCK_CASE_ASPECTS * CLOCK_CASE_HXS];
static cairo_surface_t *g_post_case_pic[POST_CASE_ASPECTS * POST_CASE_HXS];
static cairo_surface_t *g_console_case_pic[CONSOLE_CASE_ASPECTS * CONSOLE_CASE_HXS];

static struct room_case g_cases[] = {
	{ CLOCK_CASE, CLOCK_CASE_ASPECTS, CLOCK_CASE_HXS, "clock", "clock-case.bin",
		"the clock's", false, clock_case_pieces, g_clock_case_pic, NULL, 0, 0, { 0 } },
	{ POST_CASE, POST_CASE_ASPECTS, POST_CASE_HXS, "post", "post-case.bin",
		"the post box's", false, post_case_pieces, g_post_case_pic, NULL, 0, 0, { 0 } },
	{ CONSOLE_CASE, CONSOLE_CASE_ASPECTS, CONSOLE_CASE_HXS, "console", "console-case.bin",
		"the console's", false, console_case_pieces, g_console_case_pic, NULL, 0, 0, { 0 } },
};
#define ROOM_CASES ((int)(sizeof g_cases / sizeof g_cases[0]))
static pthread_mutex_t g_case_lock = PTHREAD_MUTEX_INITIALIZER;

/* How the pictures of one thing are laid: the ones mixed and how much of
 * each; for each piece of each, the homography from the target's pixels to
 * the picture's; each piece's outline in the target's pixels, and the
 * pixels each piece and all of it cover. */
struct case_plan {
	int n, which[4];
	double weight[4];
	int pieces;
	double to_pic[4][ROOM_CASE_PIECES][9];
	int outn[ROOM_CASE_PIECES];
	double out[ROOM_CASE_PIECES][8][2];
	int box[ROOM_CASE_PIECES][4];
	int x0, y0, x1, y1;
};

/* Along one row of what a thing was rendered for -- the shapes, or the
 * desks, every `stride`th -- where v is: the one at or below it, and how
 * far on to the next, as a ratio (1.5 is as far from 1.25 as 1.8 from
 * 1.5); not at all, nor all the way, when it is next to one. */
static int case_step(const struct room_case_bake *b, int n, int stride, bool desk, double v,
	double *t)
{
	int i = 0;
	while (i + 1 < n && (desk ? b[(i + 1) * stride].hx : b[(i + 1) * stride].aspect) <= v) {
		i++;
	}
	*t = 0.0;
	double lo = desk ? b[i * stride].hx : b[i * stride].aspect;
	if (i + 1 < n && v > lo) {
		double hi = desk ? b[(i + 1) * stride].hx : b[(i + 1) * stride].aspect;
		*t = log(v / lo) / log(hi / lo);
	}
	*t = *t < 0.01 ? 0.0 : *t > 0.99 ? 1.0 : *t;
	return i;
}

/* The pictures a screen this shape over a desk this wide is mixed from:
 * up to the four round it, by how far between them it is each way. */
static int case_pick(const struct room_case *o, double aspect, double hx, int which[4],
	double weight[4])
{
	double ta, th;
	int i = case_step(o->bake, o->n_aspect, 1, false, aspect, &ta);
	int j = case_step(o->bake, o->n_hx, o->n_aspect, true, hx, &th);
	int n = 0;
	for (int dj = 0; dj < 2; dj++) {
		for (int di = 0; di < 2; di++) {
			double w = (di ? ta : 1.0 - ta) * (dj ? th : 1.0 - th);
			if (w > 0.0) {
				which[n] = (j + dj) * o->n_aspect + i + di;
				weight[n++] = w;
			}
		}
	}
	return n;
}

/* The convex hull of n points (at most eight), round in order, in place;
 * how many are on it. */
static int case_hull(double p[8][2], int n)
{
	for (int i = 1; i < n; i++) {
		for (int j = i; j > 0 && (p[j][0] < p[j - 1][0]
				|| (!(p[j][0] > p[j - 1][0]) && p[j][1] < p[j - 1][1])); j--) {
			double t0 = p[j][0], t1 = p[j][1];
			p[j][0] = p[j - 1][0];
			p[j][1] = p[j - 1][1];
			p[j - 1][0] = t0;
			p[j - 1][1] = t1;
		}
	}
	if (n < 3) {
		return n;
	}
	double h[16][2];
	int k = 0;
	for (int pass = 0; pass < 2; pass++) {
		int start = k;
		for (int c = 0; c < n; c++) {
			const double *q = p[pass ? n - 1 - c : c];
			while (k >= start + 2 && (h[k - 1][0] - h[k - 2][0]) * (q[1] - h[k - 2][1])
				- (h[k - 1][1] - h[k - 2][1]) * (q[0] - h[k - 2][0]) <= 0.0) {
				k--;
			}
			h[k][0] = q[0];
			h[k][1] = q[1];
			k++;
		}
		k--;   /* each run's last is the next one's first */
	}
	for (int i = 0; i < k; i++) {
		p[i][0] = h[i][0];
		p[i][1] = h[i][1];
	}
	return k;
}

/* 3x3, row by row: c = a b */
static void m3_mul(const double a[9], const double b[9], double c[9])
{
	double r[9];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
		}
	}
	memcpy(c, r, sizeof r);
}

/* its inverse, up to a scale (which a homography does not mind); false
 * when there is none */
static bool m3_inv(const double m[9], double r[9])
{
	r[0] = m[4] * m[8] - m[5] * m[7];
	r[1] = m[2] * m[7] - m[1] * m[8];
	r[2] = m[1] * m[5] - m[2] * m[4];
	r[3] = m[5] * m[6] - m[3] * m[8];
	r[4] = m[0] * m[8] - m[2] * m[6];
	r[5] = m[2] * m[3] - m[0] * m[5];
	r[6] = m[3] * m[7] - m[4] * m[6];
	r[7] = m[1] * m[6] - m[0] * m[7];
	r[8] = m[0] * m[4] - m[1] * m[3];
	double det = m[0] * r[0] + m[1] * r[3] + m[2] * r[6];
	return fabs(det) > 1e-300;
}

/* The homography from the unit square to the quad q (corners round in
 * order from the one at 0, 0). */
static bool m3_square(const double q[4][2], double m[9])
{
	double sx = q[0][0] - q[1][0] + q[2][0] - q[3][0];
	double sy = q[0][1] - q[1][1] + q[2][1] - q[3][1];
	double dx1 = q[1][0] - q[2][0], dx2 = q[3][0] - q[2][0];
	double dy1 = q[1][1] - q[2][1], dy2 = q[3][1] - q[2][1];
	double den = dx1 * dy2 - dx2 * dy1;
	if (fabs(den) < 1e-18) {
		return false;
	}
	double g = (sx * dy2 - dx2 * sy) / den, h = (dx1 * sy - sx * dy1) / den;
	m[0] = q[1][0] - q[0][0] + g * q[1][0];
	m[1] = q[3][0] - q[0][0] + h * q[3][0];
	m[2] = q[0][0];
	m[3] = q[1][1] - q[0][1] + g * q[1][1];
	m[4] = q[3][1] - q[0][1] + h * q[3][1];
	m[5] = q[0][1];
	m[6] = g;
	m[7] = h;
	m[8] = 1.0;
	return true;
}

static bool case_project4(const struct room_cam *c, const vec3 p[4], double q[4][2])
{
	for (int i = 0; i < 4; i++) {
		if (!room_project(c, p[i], &q[i][0], &q[i][1])) {
			return false;
		}
	}
	return true;
}

static bool case_plan(const struct room_case *o, const struct room_render *rr, double ox,
	double oy, double sw, double sh, const cairo_matrix_t *ctm, struct case_plan *p,
	double key[14])
{
	const struct room_cam *c = &rr->cam;
	if (c->pitch != 0.0 || c->fov_y <= 0.0) {
		return false;
	}
	double a = c->fov_x / c->fov_y;
	/* the desk's half width, from where the frame has its far edge */
	double hx = (RM_ZN - c->z0) * a * 0.5;
	struct case_piece pc[ROOM_CASE_PIECES];
	int np = o->pieces(a, hx, pc);
	p->pieces = np;
	p->x0 = p->y0 = INT_MAX;
	p->x1 = p->y1 = INT_MIN;
	/* this frame to the target's pixels */
	const double to_dev[9] = {
		ctm->xx * sw, ctm->xy * sh, ctm->xx * ox + ctm->xy * oy + ctm->x0,
		ctm->yx * sw, ctm->yy * sh, ctm->yx * ox + ctm->yy * oy + ctm->y0,
		0.0, 0.0, 1.0,
	};
	double now[ROOM_CASE_PIECES][9];
	for (int k = 0; k < np; k++) {
		/* the outline, in the target's pixels */
		double pts[8][2] = { { 0.0 } };
		for (int i = 0; i < pc[k].n; i++) {
			double u, v;
			if (!room_project(c, pc[k].p[i], &u, &v)) {
				return false;
			}
			pts[i][0] = ox + u * sw;
			pts[i][1] = oy + v * sh;
			cairo_matrix_transform_point(ctm, &pts[i][0], &pts[i][1]);
		}
		p->outn[k] = pc[k].hull ? case_hull(pts, pc[k].n) : pc[k].n;
		memcpy(p->out[k], pts, sizeof pts);
		/* and the plane, as it is seen now */
		double q[4][2];
		if (!case_project4(c, pc[k].q, q) || !m3_square(q, now[k])) {
			return false;
		}
	}

	p->n = case_pick(o, a, hx, p->which, p->weight);
	for (int k = 0; k < np; k++) {
		p->box[k][0] = p->box[k][1] = INT_MAX;
		p->box[k][2] = p->box[k][3] = INT_MIN;
	}
	for (int s = 0; s < p->n; s++) {
		const struct room_case_bake *b = &o->bake[p->which[s]];
		/* the room it was rendered in, and where its picture is there */
		struct room_cam bc;
		room_cam_plan_at(b->aspect, b->hx, &bc);
		struct case_piece then[ROOM_CASE_PIECES];
		if (o->pieces(b->aspect, b->hx, then) != np) {
			return false;
		}
		const double from_pic[9] = {
			(b->u1 - b->u0) / b->w, 0.0, b->u0,
			0.0, (b->v1 - b->v0) / b->h, b->v0,
			0.0, 0.0, 1.0,
		};
		for (int k = 0; k < np; k++) {
			double q[4][2], sq[9], inv[9], m[9];
			if (!case_project4(&bc, then[k].q, q) || !m3_square(q, sq) || !m3_inv(sq, inv)) {
				return false;
			}
			/* its pixels to its frame, to the square, to this frame, to
			 * the target's pixels; and back */
			m3_mul(inv, from_pic, m);
			m3_mul(now[k], m, m);
			m3_mul(to_dev, m, m);
			/* (an inverse up to a scale can be up to a negative one: the
			 * picture is in front, so its w is positive) */
			if (m[6] * b->w * 0.5 + m[7] * b->h * 0.5 + m[8] < 0.0) {
				for (int i = 0; i < 9; i++) {
					m[i] = -m[i];
				}
			}
			if (!m3_inv(m, p->to_pic[s][k])) {
				return false;
			}
			/* what of the target it can reach: the picture, laid this
			 * way, and no further than its outline */
			double bx0 = INFINITY, by0 = INFINITY, bx1 = -INFINITY, by1 = -INFINITY;
			for (int c4 = 0; c4 < 4; c4++) {
				double x = (c4 & 1) ? b->w : 0.0, y = (c4 & 2) ? b->h : 0.0;
				double w = m[6] * x + m[7] * y + m[8];
				if (w <= 0.0) {
					return false;
				}
				double dx = (m[0] * x + m[1] * y + m[2]) / w;
				double dy = (m[3] * x + m[4] * y + m[5]) / w;
				bx0 = fmin(bx0, dx);
				by0 = fmin(by0, dy);
				bx1 = fmax(bx1, dx);
				by1 = fmax(by1, dy);
			}
			if (p->outn[k] >= 3) {
				double ox0 = INFINITY, oy0 = INFINITY, ox1 = -INFINITY, oy1 = -INFINITY;
				for (int i = 0; i < p->outn[k]; i++) {
					ox0 = fmin(ox0, p->out[k][i][0]);
					oy0 = fmin(oy0, p->out[k][i][1]);
					ox1 = fmax(ox1, p->out[k][i][0]);
					oy1 = fmax(oy1, p->out[k][i][1]);
				}
				bx0 = fmax(bx0, ox0);
				by0 = fmax(by0, oy0);
				bx1 = fmin(bx1, ox1);
				by1 = fmin(by1, oy1);
			}
			if (bx1 <= bx0 || by1 <= by0) {
				continue;
			}
			int *bk = p->box[k];
			bk[0] = (int)floor(bx0) - 1 < bk[0] ? (int)floor(bx0) - 1 : bk[0];
			bk[1] = (int)floor(by0) - 1 < bk[1] ? (int)floor(by0) - 1 : bk[1];
			bk[2] = (int)ceil(bx1) + 1 > bk[2] ? (int)ceil(bx1) + 1 : bk[2];
			bk[3] = (int)ceil(by1) + 1 > bk[3] ? (int)ceil(by1) + 1 : bk[3];
		}
	}
	for (int k = 0; k < np; k++) {
		if (p->box[k][2] > p->box[k][0] && p->box[k][3] > p->box[k][1]) {
			p->x0 = p->box[k][0] < p->x0 ? p->box[k][0] : p->x0;
			p->y0 = p->box[k][1] < p->y0 ? p->box[k][1] : p->y0;
			p->x1 = p->box[k][2] > p->x1 ? p->box[k][2] : p->x1;
			p->y1 = p->box[k][3] > p->y1 ? p->box[k][3] : p->y1;
		}
	}
	if (p->n == 0 || p->x1 <= p->x0 || p->y1 <= p->y0) {
		return false;
	}
	if (key) {
		double k14[14] = { ctm->xx, ctm->yx, ctm->xy, ctm->yy, ctm->x0, ctm->y0,
			ox, oy, sw, sh, a, hx, (double)rr->w, (double)rr->h };
		memcpy(key, k14, sizeof k14);
	}
	return true;
}

/* The pictures the plan mixes, read out of the .bin, and the others let
 * go. */
static bool case_pics(struct room_case *o, const int *which, int n)
{
	int count = o->n_aspect * o->n_hx;
	for (int i = 0; i < count; i++) {
		bool want = false;
		for (int s = 0; s < n; s++) {
			want |= which[s] == i;
		}
		if (!want && o->pic[i]) {
			cairo_surface_destroy(o->pic[i]);
			o->pic[i] = NULL;
		}
	}
	if (o->gone) {
		return false;
	}
	bool all = true;
	int fd = -1;
	for (int s = 0; s < n && all; s++) {
		const struct room_case_bake *b = &o->bake[which[s]];
		if (o->pic[which[s]]) {
			continue;
		}
		if (fd < 0) {
			char path[RF_DIR_LEN + 64];
			if (!desk_art_find(o->kind, o->file, path, sizeof path)
				|| (fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) {
				room_log("%s pictures are not there (%s)", o->what, path);
				o->gone = true;
				return false;
			}
		}
		unsigned char *png = malloc(b->len);
		struct cam_read r = { png, b->len };
		cairo_surface_t *pic = NULL;
		if (png && pread(fd, png, b->len, (off_t)b->off) == (ssize_t)b->len) {
			pic = cairo_image_surface_create_from_png_stream(cam_read, &r);
		}
		free(png);
		if (!pic || cairo_surface_status(pic) != CAIRO_STATUS_SUCCESS
			|| cairo_image_surface_get_width(pic) != b->w
			|| cairo_image_surface_get_height(pic) != b->h) {
			room_log("%s pictures would not read (%s, the one at %u)", o->what, o->file,
				b->off);
			o->gone = true;
			if (pic) {
				cairo_surface_destroy(pic);
			}
			all = false;
			continue;
		}
		o->pic[which[s]] = pic;
	}
	if (fd >= 0) {
		close(fd);
	}
	return all;
}

/* The pictures a screen this shape over a desk this wide wants, read ahead
 * -- by the thread making its room, before there is a desk to draw -- so
 * that the desk's first frame does not wait for them to be read. */
static void room_cases_ready(double aspect, double hx)
{
	pthread_mutex_lock(&g_case_lock);
	for (int i = 0; i < ROOM_CASES; i++) {
		int which[4];
		double weight[4];
		int n = case_pick(&g_cases[i], fmax(aspect, 0.2), hx, which, weight);
		case_pics(&g_cases[i], which, n);
	}
	pthread_mutex_unlock(&g_case_lock);
}

static void case_outline(cairo_t *cr, const struct case_plan *p, int k)
{
	cairo_move_to(cr, p->out[k][0][0], p->out[k][0][1]);
	for (int i = 1; i < p->outn[k]; i++) {
		cairo_line_to(cr, p->out[k][i][0], p->out[k][i][1]);
	}
	cairo_close_path(cr);
}

/* where the target's pixel x, y is in the picture, through h */
static void case_hmap(const double h[9], double x, double y, double *u, double *v)
{
	double w = h[6] * x + h[7] * y + h[8];
	*u = (h[0] * x + h[1] * y + h[2]) / w;
	*v = (h[3] * x + h[4] * y + h[5]) / w;
}

/* The picture into the cell x0, y0 - x1, y1 of the target, through h: by
 * the one matrix that is h at three of its corners, where that is within a
 * tenth of a pixel of h at the fourth and the middle -- else the cell in
 * two, and each the same way. */
static void case_cells(cairo_t *ic, cairo_surface_t *pic, const double h[9], double alpha,
	int x0, int y0, int x1, int y1, int depth)
{
	double ua, va, ub, vb, uc, vc, ud, vd, um, vm;
	case_hmap(h, x0, y0, &ua, &va);
	case_hmap(h, x1, y0, &ub, &vb);
	case_hmap(h, x0, y1, &uc, &vc);
	case_hmap(h, x1, y1, &ud, &vd);
	case_hmap(h, (x0 + x1) * 0.5, (y0 + y1) * 0.5, &um, &vm);
	double w = x1 - x0, hh = y1 - y0;
	/* how far off the matrix is, in the picture's pixels, and those in the
	 * target's */
	double off = fmax(hypot(ub + uc - ua - ud, vb + vc - va - vd),
		hypot((ub + uc) * 0.5 - um, (vb + vc) * 0.5 - vm));
	double grain = fmax(hypot(ub - ua, vb - va) / w, hypot(uc - ua, vc - va) / hh);
	if (off > 0.1 * grain && depth < 10 && (w > 2.0 || hh > 2.0)) {
		if (w >= hh) {
			int xm = (x0 + x1) / 2;
			case_cells(ic, pic, h, alpha, x0, y0, xm, y1, depth + 1);
			case_cells(ic, pic, h, alpha, xm, y0, x1, y1, depth + 1);
		} else {
			int ym = (y0 + y1) / 2;
			case_cells(ic, pic, h, alpha, x0, y0, x1, ym, depth + 1);
			case_cells(ic, pic, h, alpha, x0, ym, x1, y1, depth + 1);
		}
		return;
	}
	cairo_matrix_t m;
	double xx = (ub - ua) / w, yx = (vb - va) / w, xy = (uc - ua) / hh, yy = (vc - va) / hh;
	cairo_matrix_init(&m, xx, yx, xy, yy, ua - xx * x0 - xy * y0, va - yx * x0 - yy * y0);
	cairo_pattern_t *pat = cairo_pattern_create_for_surface(pic);
	cairo_pattern_set_matrix(pat, &m);
	cairo_pattern_set_filter(pat, CAIRO_FILTER_GOOD);
	cairo_save(ic);
	cairo_rectangle(ic, x0, y0, w, hh);
	cairo_clip(ic);
	cairo_set_source(ic, pat);
	cairo_paint_with_alpha(ic, alpha);
	cairo_restore(ic);
	cairo_pattern_destroy(pat);
}

/* The picture laid for this plan: piece by piece, each picture mixed in by
 * its weight -- premultiplied, the mix is a sum. */
static cairo_surface_t *case_lay(struct room_case *o, const struct case_plan *p)
{
	int w = p->x1 - p->x0, h = p->y1 - p->y0;
	if (w > 8192 || h > 8192 || !case_pics(o, p->which, p->n)) {
		return NULL;
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *ic = cairo_create(img);
	cairo_set_operator(ic, CAIRO_OPERATOR_ADD);
	cairo_set_antialias(ic, CAIRO_ANTIALIAS_NONE);
	/* in the target's pixels */
	cairo_translate(ic, -p->x0, -p->y0);
	for (int k = 0; k < p->pieces; k++) {
		bool rest = k == p->pieces - 1;
		const int *bk = rest ? (const int[4]){ p->x0, p->y0, p->x1, p->y1 } : p->box[k];
		if ((p->outn[k] < 3 && !rest) || bk[2] <= bk[0] || bk[3] <= bk[1]) {
			continue;
		}
		cairo_save(ic);
		/* its outline, less those before it; the last, all but theirs */
		cairo_set_fill_rule(ic, CAIRO_FILL_RULE_EVEN_ODD);
		for (int j = 0; j < k; j++) {
			if (p->outn[j] >= 3) {
				cairo_rectangle(ic, p->x0 - 1, p->y0 - 1, w + 2, h + 2);
				case_outline(ic, p, j);
				cairo_clip(ic);
			}
		}
		if (!rest) {
			case_outline(ic, p, k);
			cairo_clip(ic);
		}
		for (int s = 0; s < p->n; s++) {
			case_cells(ic, o->pic[p->which[s]], p->to_pic[s][k], p->weight[s],
				bk[0], bk[1], bk[2], bk[3], 0);
		}
		cairo_restore(ic);
	}
	cairo_destroy(ic);
	cairo_surface_flush(img);
	return img;
}

static void case_paint(struct room_case *o, cairo_t *cr, const struct room_xf *xf)
{
	cairo_matrix_t ctm;
	cairo_get_matrix(cr, &ctm);
	struct case_plan p;
	double key[14];
	if (!case_plan(o, xf->rr, xf->ox, xf->oy, xf->rr->w * xf->scale,
			xf->rr->h * xf->scale, &ctm, &p, key)) {
		return;
	}
	pthread_mutex_lock(&g_case_lock);
	if (!o->img || memcmp(key, o->key, sizeof key) != 0) {
		if (o->img) {
			cairo_surface_destroy(o->img);
		}
		o->img = case_lay(o, &p);
		o->x = p.x0;
		o->y = p.y0;
		memcpy(o->key, key, sizeof key);
	}
	if (o->img) {
		cairo_save(cr);
		cairo_identity_matrix(cr);
		cairo_set_source_surface(cr, o->img, o->x, o->y);
		cairo_paint(cr);
		cairo_restore(cr);
	}
	pthread_mutex_unlock(&g_case_lock);
}

/* All of them, on the desk as xf has it. */
static void room_cases_paint(cairo_t *cr, const struct room_xf *xf)
{
	for (int i = 0; i < ROOM_CASES; i++) {
		case_paint(&g_cases[i], cr, xf);
	}
}

/* What of a w x h screen they cover, the drawer shut. */
static bool room_cases_box(int w, int h, double box[4])
{
	struct room_xf xf;
	if (!room_xform(w, h, 0.0, &xf)) {
		return false;
	}
	cairo_matrix_t one;
	cairo_matrix_init_identity(&one);
	double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
	for (int i = 0; i < ROOM_CASES; i++) {
		struct case_plan p;
		if (case_plan(&g_cases[i], xf.rr, xf.ox, xf.oy, xf.rr->w * xf.scale,
				xf.rr->h * xf.scale, &one, &p, NULL)) {
			x0 = fmin(x0, p.x0 - 1.0);
			y0 = fmin(y0, p.y0 - 1.0);
			x1 = fmax(x1, p.x1 + 1.0);
			y1 = fmax(y1, p.y1 + 1.0);
		}
	}
	if (x1 <= x0 || y1 <= y0) {
		return false;
	}
	box[0] = x0;
	box[1] = y0;
	box[2] = x1 - x0;
	box[3] = y1 - y0;
	return true;
}

static bool clk_face_point(int w, int h, double px, double py, double drawer_open,
	double *fx, double *fy)
{
	cairo_matrix_t m;
	if (!clk_face_matrix(w, h, drawer_open, &m) || cairo_matrix_invert(&m) != CAIRO_STATUS_SUCCESS) {
		return false;
	}
	*fx = px;
	*fy = py;
	cairo_matrix_transform_point(&m, fx, fy);
	return true;
}

bool desk_clock_bar_value(int w, int h, double px, double py, double drawer_open,
	double *value)
{
	double fx, fy;
	if (!clk_face_point(w, h, px, py, drawer_open, &fx, &fy)) {
		return false;
	}
	/* the MEDIA page's volume: a block is lit once the pointer is past
	 * half of it */
	double v = (fx - CLK_WX - MED_VX) / MED_VW + 0.5 / MED_VN;
	*value = floor(fmin(fmax(v, 0.0), 1.0) * MED_VN) / MED_VN;
	return true;
}

bool desk_clock_pick(int w, int h, double px, double py, double drawer_open,
	struct desk_clock_pick *out)
{
	memset(out, 0, sizeof *out);
	out->key = -1;
	out->button = -1;
	double fx, fy;
	if (!clk_face_point(w, h, px, py, drawer_open, &fx, &fy)) {
		return false;
	}
	/* close enough that a hand is plainly on its way to it */
	out->near = fx > -64.0 && fx < CLK_W + 64.0 && fy > -30.0 && fy < CLK_H + 64.0;
	out->on = fx >= -3.0 && fx <= CLK_W + 3.0 && fy >= -3.0 && fy <= CLK_H + 4.0;
	if (!out->on) {
		return true;
	}
	if (fx >= CLK_KX - 6.0) {
		/* the block of keys: the gaps belong to the nearer key */
		for (int i = 0; i < DESK_CLOCK_KEYS; i++) {
			double kx = clk_key_x(i), ky = clk_key_y(i);
			if (fx >= kx - CLK_KGX * 0.5 - (i & 1 ? 0.0 : 6.0)
				&& fx < kx + CLK_KW + CLK_KGX * 0.5 + (i & 1 ? 6.0 : 0.0)
				&& fy >= ky - (i >> 1 ? CLK_KGY * 0.5 : 3.0)
				&& fy < ky + CLK_KH + (i >> 1 ? 4.0 : CLK_KGY * 0.5)) {
				out->key = i;
			}
		}
		return true;
	}
	if (g_clk.mode != DESK_CLOCK_MEDIA) {
		return true;
	}
	double rx = fx - CLK_WX, ry = fy - CLK_WY;
	if (ry >= MED_RY - 4.0) {
		/* the row of things to press */
		for (int i = 0; i < 3; i++) {
			double bx = MED_BX + i * (MED_BW + MED_BGAP);
			if (rx >= bx - MED_BGAP * 0.5 && rx < bx + MED_BW + MED_BGAP * 0.5) {
				out->button = i;
			}
		}
		if (g_clk.have_volume && rx >= MED_SX - 6.0 && rx < MED_VX - 3.0) {
			out->speaker = true;
		} else if (g_clk.have_volume && rx >= MED_VX - 3.0) {
			out->bar = true;
			desk_clock_bar_value(w, h, px, py, drawer_open, &out->bar_value);
		}
	} else if (ry < MED_PY && g_clk.media.have && g_clk.media.can_raise) {
		/* the title: the player itself, brought up */
		out->button = DESK_MEDIA_RAISE;
	}
	return true;
}

/* The clock's face, and the light that spills from its keys. */
static bool clk_face_box(int w, int h, double box[4])
{
	cairo_matrix_t m;
	if (!clk_face_matrix(w, h, 0.0, &m)) {
		return false;
	}
	double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
	for (int k = 0; k < 4; k++) {
		double x = (k & 1) ? CLK_W : 0.0, y = (k & 2) ? CLK_H : 0.0;
		cairo_matrix_transform_point(&m, &x, &y);
		x0 = fmin(x0, x);
		y0 = fmin(y0, y);
		x1 = fmax(x1, x);
		y1 = fmax(y1, y);
	}
	/* and the light that spills from the keys */
	box[0] = floor(x0) - 12.0;
	box[1] = floor(y0) - 12.0;
	box[2] = ceil(x1) - box[0] + 12.0;
	box[3] = ceil(y1) - box[1] + 12.0;
	return true;
}

static bool mat_box(int w, int h, double box[4]);
static bool sheet_box(int w, int h, double box[4]);

bool desk_overlay_box(int which, int w, int h, double box[4])
{
	if (which == DESK_OVL_MAT) {
		return mat_box(w, h, box);
	}
	if (which == DESK_OVL_MAT_SHEET) {
		return sheet_box(w, h, box);
	}
	if (which == DESK_OVL_SLIPS) {
		return slips_box(w, h, box);
	}
	if (which == DESK_OVL_CASES) {
		return room_cases_box(w, h, box);
	}
	return which == DESK_OVL_CLOCK && clk_face_box(w, h, box);
}

bool desk_clock_glow_box(int w, int h, double box[4])
{
	if (!clk_face_box(w, h, box)) {
		return false;
	}
	/* and the red it throws round it */
	double m = box[3] * 0.8;
	box[0] = floor(box[0] - m);
	box[1] = floor(box[1] - m);
	box[2] = ceil(box[2] + 2.0 * m);
	box[3] = ceil(box[3] + 2.0 * m);
	return true;
}

bool desk_draw_poppers(cairo_t *cr, int w, int h, double t)
{
	static const double COL[6][3] = { { 0.96, 0.76, 0.30 }, { 0.85, 0.87, 0.92 },
		{ 0.92, 0.22, 0.30 }, { 0.20, 0.50, 0.95 }, { 0.30, 0.82, 0.42 }, { 0.95, 0.45, 0.75 } };
	const double end = 7.5;
	if (t > end || w < 8 || h < 8) {
		return false;
	}
	double k = h / 900.0, fade = clampf((end - t) / 1.6, 0.0, 1.0);
	const double drag = 2.6, fall = 0.16 * h;
	for (int p = 0; p < 2; p++) {
		/* from the left corner up and in, then from the right */
		double side = p ? -1.0 : 1.0, t0 = p ? 0.32 : 0.10;
		double x0 = p ? w * 0.97 : w * 0.03, y0 = h + 6.0;
		for (int i = 0; i < 130; i++) {
			double h1 = hash21(i * 1.31 + p * 7.0, 2.7), h2 = hash21(i * 2.17 + p, 5.3);
			double h3 = hash21(i * 0.73 + p * 3.0, 9.1), h4 = hash21(i * 3.91 + p, 1.9);
			double tau = t - (t0 + 0.05 * h1);
			if (tau < 0.0) {
				continue;
			}
			double ang = 0.10 + 0.62 * h2, v = (1.6 + 1.2 * h3) * h;
			double vx = side * sin(ang) * v, vy = -cos(ang) * v;
			double e = exp(-drag * tau);
			double x = x0 + vx / drag * (1.0 - e)
				+ sin(tau * (2.0 + 2.5 * h4) + h1 * 6.283) * 0.02 * w * fmin(tau / 1.2, 1.0);
			double y = y0 + fall * tau + (vy - fall) / drag * (1.0 - e);
			if (y > h + 20.0 || x < -20.0 || x > w + 20.0) {
				continue;
			}
			double spin = tau * (4.0 + 6.0 * h1) + h2 * 6.283;
			double face = cos(spin);
			double sz = (7.0 + 6.0 * h4) * k;
			const double *c = COL[(i + p) % 6];
			double glint = pow(fabs(face), 8.0) * ((i + p) % 6 < 2 ? 0.55 : 0.2);
			cairo_save(cr);
			cairo_translate(cr, x, y);
			cairo_rotate(cr, h3 * 6.283 + tau * (h1 - 0.5) * 2.0);
			cairo_scale(cr, 1.0, fmax(fabs(face), 0.12));
			cairo_rectangle(cr, -sz * 0.5, -sz * 0.33, sz, sz * 0.66);
			cairo_set_source_rgba(cr, fmin(1.0, c[0] + glint), fmin(1.0, c[1] + glint),
				fmin(1.0, c[2] + glint), 0.95 * fade);
			cairo_fill(cr);
			cairo_restore(cr);
		}
	}
	return true;
}

void desk_draw_clock_glow(cairo_t *cr, int w, int h)
{
	cairo_matrix_t m;
	if (!clk_face_matrix(w, h, 0.0, &m)) {
		return;
	}
	cairo_save(cr);
	cairo_transform(cr, &m);
	/* the red light the display throws on its case and on the desk */
	double cx = CLK_WX + CLK_WW * 0.5, cy = CLK_WY + CLK_WH * 0.5;
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_scale(cr, 1.0, 0.42);
	cairo_pattern_t *g = cairo_pattern_create_radial(0, 0, CLK_WW * 0.12, 0, 0, CLK_WW * 0.95);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 0.16, 0.07, 0.16);
	cairo_pattern_add_color_stop_rgba(g, 0.5, 1.0, 0.14, 0.06, 0.05);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 1.0, 0.12, 0.05, 0.0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
	/* and the segments that are lit, as bright as in daylight */
	desk_rounded_path(cr, CLK_WX, CLK_WY, CLK_WW, CLK_WH, 4.0);
	cairo_clip(cr);
	g_led_lit_only = true;
	if (g_clk.boot >= 0.0) {
		clk_page_boot(cr, 1.0);
	} else if (g_clk.show) {
		clk_page_show(cr, 1.0);
	}
	g_led_lit_only = false;
	cairo_restore(cr);
}

static void gadget_deskclock(cairo_t *cr, double w, double h, time_t now)
{
	double r = fmin(w, h) * 0.5 - 4.0;
	double cx = w * 0.5, cy = h * 0.5;
	cairo_set_source_rgb(cr, 0.944, 0.930, 0.894);
	cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.20, 0.17, 0.14, 0.45);
	cairo_set_line_width(cr, 2.0);
	cairo_arc(cr, cx, cy, r - 1.0, 0, 2 * M_PI);
	cairo_stroke(cr);

	for (int i = 0; i < 60; i++) {
		double a = i * M_PI / 30.0;
		double inner = (i % 5 == 0) ? r * 0.80 : r * 0.88;
		cairo_set_line_width(cr, (i % 5 == 0) ? 2.4 : 1.0);
		cairo_set_source_rgba(cr, 0.18, 0.16, 0.14, (i % 5 == 0) ? 0.9 : 0.5);
		cairo_move_to(cr, cx + sin(a) * inner, cy - cos(a) * inner);
		cairo_line_to(cr, cx + sin(a) * r * 0.94, cy - cos(a) * r * 0.94);
		cairo_stroke(cr);
	}
	for (int i = 1; i <= 12; i++) {
		double a = i * M_PI / 6.0;
		char buf[4];
		snprintf(buf, sizeof buf, "%d", i);
		show_text_as(cr, DESK_FONT_ENGRAVE, buf, cx + sin(a) * r * 0.66,
			cy - cos(a) * r * 0.66 + r * 0.09,
			r * 0.24, true, 0.16, 0.14, 0.12, 1.0, 0);
	}

	struct tm tmv;
	localtime_r(&now, &tmv);
	double hr = (tmv.tm_hour % 12) + tmv.tm_min / 60.0;
	double mn = tmv.tm_min + tmv.tm_sec / 60.0;
	struct {
		double ang, len, wid;
	} hands[2] = {
		{ hr * M_PI / 6.0, r * 0.52, 5.0 },
		{ mn * M_PI / 30.0, r * 0.76, 3.4 },
	};
	for (int i = 0; i < 2; i++) {
		double a = hands[i].ang;
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
		cairo_set_line_width(cr, hands[i].wid);
		cairo_move_to(cr, cx - sin(a) * r * 0.12 + 1.5, cy + cos(a) * r * 0.12 + 1.5);
		cairo_line_to(cr, cx + sin(a) * hands[i].len + 1.5, cy - cos(a) * hands[i].len + 1.5);
		cairo_stroke(cr);
		cairo_set_source_rgb(cr, 0.12, 0.11, 0.10);
		cairo_move_to(cr, cx - sin(a) * r * 0.12, cy + cos(a) * r * 0.12);
		cairo_line_to(cr, cx + sin(a) * hands[i].len, cy - cos(a) * hands[i].len);
		cairo_stroke(cr);
	}
	/* A seconds hand does not arrive at a mark, it is thrown at one: it
	 * gets there in about a tenth of a second and rings about it before it
	 * settles. On a clock nobody is looking at, that ring is the whole
	 * difference between a clock and a picture of one. */
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	double k = clampf(ts.tv_nsec / 1e9 / 0.105, 0.0, 1.0);
	const double SZ = 0.42, SW = 13.0;
	double swd = SW * sqrt(1.0 - SZ * SZ);
	double thrown = 1.0 - exp(-SZ * SW * k)
		* (cos(swd * k) + (SZ * SW / swd) * sin(swd * k));
	double sa = ((double)tmv.tm_sec - 1.0 + thrown) * M_PI / 30.0;
	cairo_set_source_rgb(cr, 0.68, 0.16, 0.12);
	cairo_set_line_width(cr, 1.6);
	cairo_move_to(cr, cx - sin(sa) * r * 0.18, cy + cos(sa) * r * 0.18);
	cairo_line_to(cr, cx + sin(sa) * r * 0.84, cy - cos(sa) * r * 0.84);
	cairo_stroke(cr);
	cairo_arc(cr, cx, cy, 3.2, 0, 2 * M_PI);
	cairo_fill(cr);
}

/* ---- the desk calendar --------------------------------------------
 *
 * Printed the way a calendar that stands on a desk is, in four sections
 * one behind the other, each with its tab standing out at the right edge
 * of the pages: the year on one page, a month to a page, a week to a page,
 * a day to a page. A month's page: the number of the month large, its
 * name and the year beside it, the month before and after small in the
 * corner, and a grid of boxes with the date in the corner of each and room
 * to write. Sundays red and Saturdays blue, as the ones sold here are;
 * today ringed in red pen. The punched strip along the top is printed with
 * the page -- and on the shelf the wire through it too; close up it is
 * drawn over. */

static const char *const CAL_TAB_NAME[DESK_CAL_VIEWS] = { "tab|YEAR", "tab|MONTH", "tab|WEEK",
	"tab|DAY" };

/* A month's name in the desk's language: whole ("OCTOBER", "10月") or
 * short ("OCT", the same in Japanese) -- i18n.c's. (buf: unused.) */
static const char *cal_month_word(int m, bool full, char *buf, size_t n)
{
	(void)buf;
	(void)n;
	return desk_month_name(m, full ? DESK_NAME_WHOLE : DESK_NAME_SHORT);
}

/* A weekday's name: whole (2: "THURSDAY", "木曜日"), short (1: "THU",
 * "木") or a letter (0: "T", "木"). (buf: unused.) */
static const char *cal_day_word(int wd, int form, char *buf, size_t n)
{
	(void)buf;
	(void)n;
	return desk_day_name(wd, form == 2 ? DESK_NAME_WHOLE : form == 1 ? DESK_NAME_SHORT
		: DESK_NAME_LETTER);
}

/* A year's word after its number, in a language that has one: 年, 년. */
static const char *cal_year_word(void)
{
	enum desk_lang l = desk_lang_get();
	return l == DESK_LANG_JA || l == DESK_LANG_ZH ? "\xe5\xb9\xb4"   /* 年 */
		: l == DESK_LANG_KO ? "\xeb\x85\x84" : NULL;            /* 년 */
}

/* "OCTOBER 2026", "2026年10月", "2026년 10월" */
static void cal_month_year(char *out, size_t n, int m, int yr)
{
	char mb[16];
	const char *year = cal_year_word();
	if (year) {
		snprintf(out, n, "%d%s%s%s", yr, year, desk_lang_get() == DESK_LANG_KO ? " " : "",
			cal_month_word(m, true, mb, sizeof mb));
	} else {
		snprintf(out, n, "%s %d", cal_month_word(m, true, mb, sizeof mb), yr);
	}
}

/* the section the calendar on the shelf is open at */
static int g_cal_view = DESK_CAL_MONTH;
