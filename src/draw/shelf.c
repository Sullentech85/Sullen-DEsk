/* Part of draw.c: the hover treatment, and the shelf as the compositor sees it.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- the hover treatment --------------------------------------- */

/* The film itself, so the compositor can hang it on two scene nodes and
 * pick frames with a source box. Painting a full screen buffer in cairo for
 * every frame of the move was what made it judder; this way a frame of the
 * tilt is two rectangles and an opacity. */
bool desk_tilt_film(int screen_w, int screen_h, int which, cairo_surface_t **strip,
	int *frames, int *fw, int *fh)
{
	struct room_render *rr = room_for_view(screen_w, screen_h, which);
	if (which < 0 || which >= DESK_VIEW_COUNT || !rr || rr->film_div <= 0) {
		return false;
	}
	/* traced after the views, and put in while they are up: until it is
	 * there, the move is a cut */
	cairo_surface_t *s = room_film_get(rr, which);
	if (!s) {
		return false;
	}
	*strip = s;
	*frames = ROOM_TILT_FRAMES;
	*fw = rr->w / rr->film_div;
	*fh = rr->h / rr->film_div;
	return true;
}

/* ---- playing a move's film -------------------------------------
 *
 * A keyframe is a picture from a camera a little way along the move from
 * where the camera is now. Laid over the screen as it came and cross-faded
 * with the next, the move read as a dissolve -- and the slow parts of it,
 * setting off and settling, as nothing at all, so what was left looked like
 * a machine going at one speed. Moved and scaled so that the room in it is
 * where the room is in the view at t, two keyframes read as the camera
 * itself, at whatever speed it is going and past the end and back.
 *
 * The view at t: the camera partway between the desk's and the one it is
 * going to, and, at the start, zoomed in the way the desk view is on the
 * desk top -- the desk view is the same camera, cropped -- which lets go as
 * the camera lifts. Where things are in it is found by sending a grid of
 * rays into a stand-in for the room: the desk top, and a wall across the
 * back where the thing being looked at stands. A picture only moves and
 * scales, so near enough is exact enough. */

/* The camera partway, without touching the tracer's globals: it may be
 * busy tracing on a worker. */
static void film_cam_at(const struct room_cam *plan, const struct room_cam *dst, double e,
	struct room_cam *c)
{
	double phi = dst->pitch * e;
	*c = *plan;
	c->pitch = phi;
	c->eye = vmix(plan->eye, dst->eye, e);
	c->ww = v3(0.0, -cos(phi), -sin(phi));
	c->uu = v3(1, 0, 0);
	c->vv = vcross(c->uu, c->ww);
}

static const struct room_cam *film_dst_cam(const struct room_render *rr, int which)
{
	return which == DESK_VIEW_CONSOLE ? &rr->console_cam
		: which == DESK_VIEW_POST ? &rr->post_cam : &rr->shelf_cam;
}

/* The stand-in's back wall: where the thing looked at stands. */
static double film_back_z(int which)
{
	return which == DESK_VIEW_CONSOLE ? (RM_CC_PZ0 + RM_CC_ZB) * 0.5
		: which == DESK_VIEW_POST ? RM_PB_CZ : RM_SH_ZF;
}

/* How far the desk view's crop has let go at t. */
static double film_crop_gone(double t)
{
	return smoothstepf(0.0, 0.55, t);
}

/* Fit x' = a + b x each way, taking the points of `from` to where the same
 * room is at t, as a box on the screen. */
static bool film_fit(const struct room_render *rr, int which, const struct room_cam *from,
	double t, int screen_w, int screen_h, double box[4])
{
	struct room_cam at;
	film_cam_at(&rr->cam, film_dst_cam(rr, which), clampf(t, ROOM_TILT_U0, ROOM_TILT_U1), &at);
	double gone = film_crop_gone(t);
	double z = rr->shut_zoom + (1.0 - rr->shut_zoom) * gone;
	double pan = rr->shut_pan * (1.0 - gone);
	double zb = film_back_z(which);
	double sx = 0, sxx = 0, sxX = 0, sX = 0, sy = 0, syy = 0, syY = 0, sY = 0;
	int n = 0;
	for (int gy = 0; gy < 7; gy++) {
		for (int gx = 0; gx < 9; gx++) {
			double u = 0.06 + 0.88 * gx / 8.0, v = 0.06 + 0.88 * gy / 6.0;
			vec3 ro, rd;
			room_ray(&at, u, v, &ro, &rd);
			/* the desk top, or the wall behind it */
			vec3 p;
			bool hit = false;
			if (rd.y < -1e-4) {
				double k = -ro.y / rd.y;
				p = vadd(ro, vmul(rd, k));
				hit = p.z >= zb;
			}
			if (!hit) {
				if (rd.z > -1e-4) {
					continue;
				}
				p = vadd(ro, vmul(rd, (zb - ro.z) / rd.z));
			}
			double fu, fv;
			if (!room_project(from, p, &fu, &fv)) {
				continue;
			}
			/* where it is on the screen at t */
			double X = (1.0 - z) * 0.5 + u * z;
			double Y = (1.0 - z) * 0.5 + pan + v * z;
			sx += fu;
			sxx += fu * fu;
			sxX += fu * X;
			sX += X;
			sy += fv;
			syy += fv * fv;
			syY += fv * Y;
			sY += Y;
			n++;
		}
	}
	double dx = n * sxx - sx * sx, dy = n * syy - sy * sy;
	if (n < 6 || fabs(dx) < 1e-9 || fabs(dy) < 1e-9) {
		return false;
	}
	double bx = (n * sxX - sx * sX) / dx, ax = (sX - bx * sx) / n;
	double by = (n * syY - sy * sY) / dy, ay = (sY - by * sy) / n;
	if (bx < 0.2 || bx > 5.0 || by < 0.2 || by > 5.0) {
		return false;
	}
	box[0] = ax * screen_w;
	box[1] = ay * screen_h;
	box[2] = bx * screen_w;
	box[3] = by * screen_h;
	return true;
}

bool desk_tilt_place(int screen_w, int screen_h, int which, int frame, double t,
	double box[4])
{
	struct room_render *rr = room_for_view(screen_w, screen_h, which);
	if (which < 0 || which >= DESK_VIEW_COUNT || !rr || frame < 0
		|| frame >= ROOM_TILT_FRAMES) {
		return false;
	}
	struct room_cam key;
	film_cam_at(&rr->cam, film_dst_cam(rr, which),
		ROOM_TILT_U0 + (ROOM_TILT_U1 - ROOM_TILT_U0) * (double)frame
			/ (double)(ROOM_TILT_FRAMES - 1), &key);
	if (!film_fit(rr, which, &key, t, screen_w, screen_h, box)) {
		return false;
	}
	/* It has to cover the screen: whatever showed round a picture laid a
	 * little short of an edge would be the desk underneath. Stretched to
	 * the edge instead, by a pixel or three. */
	double x1 = box[0] + box[2], y1 = box[1] + box[3];
	box[0] = fmin(box[0], 0.0);
	box[1] = fmin(box[1], 0.0);
	box[2] = fmax(x1, (double)screen_w) - box[0];
	box[3] = fmax(y1, (double)screen_h) - box[1];
	return true;
}

void desk_tilt_offset(int screen_w, int screen_h, int which, double t, double *dx, double *dy)
{
	*dx = *dy = 0.0;
	struct room_render *rr = room_for_view(screen_w, screen_h, which);
	if (which < 0 || which >= DESK_VIEW_COUNT || !rr) {
		return;
	}
	double box[4];
	if (!film_fit(rr, which, film_dst_cam(rr, which), t, screen_w, screen_h, box)) {
		return;
	}
	/* where the middle of the settled view goes */
	*dx = box[0] + box[2] * 0.5 - screen_w * 0.5;
	*dy = box[1] + box[3] * 0.5 - screen_h * 0.5;
}

void desk_tilt_desk_offset(int screen_w, int screen_h, int which, double t,
	double *dx, double *dy)
{
	*dx = *dy = 0.0;
	struct room_render *rr = room_for_view(screen_w, screen_h, which);
	if (which < 0 || which >= DESK_VIEW_COUNT || !rr) {
		return;
	}
	double box[4];
	if (!film_fit(rr, which, &rr->cam, t, screen_w, screen_h, box)) {
		return;
	}
	/* against the middle of the desk as the desk view puts it */
	*dx = box[0] + box[2] * 0.5 - screen_w * 0.5;
	*dy = box[1] + box[3] * 0.5 - screen_h * (0.5 + rr->shut_pan);
}

/* Where in the film a given position in the move is. t runs 0 at the desk
 * to 1 at the shelf; the caller draws frame i and then frame i+1 over it at
 * alpha mix. */
void desk_tilt_at(double t, int frames, int *frame, double *mix)
{
	double e = clampf(t, ROOM_TILT_U0, ROOM_TILT_U1);
	double f = (e - ROOM_TILT_U0) / (ROOM_TILT_U1 - ROOM_TILT_U0)
		* (double)(frames - 1);
	int i = (int)f;
	if (i < 0) {
		i = 0;
	}
	if (i > frames - 2) {
		i = frames - 2;
		f = (double)(frames - 1);
	}
	*frame = i;
	*mix = clampf(f - (double)i, 0.0, 1.0);
}

/* ---- the shelf, as the compositor sees it ---------------------- */

/* The four corners of one binder's label pocket, in world space, in the
 * order top left, top right, bottom left, bottom right as the label is read. */
static void binder_label_quad(int i, vec3 out[4])
{
	const struct shelf_binder *b = shelf_binder_of(i);
	double lx = b->hw * 0.66, ly = 1.42;
	double lz = RM_BN_DEPTH * 0.5 + 0.02;
	for (int k = 0; k < 4; k++) {
		double sx = (k & 1) ? lx : -lx;
		double sy = (k & 2) ? -ly : ly;
		vec3 q = v3(sx, b->label_y - b->h * 0.5 + sy, lz);
		if (b->lean != 0.0) {
			q = rot_z(q, b->lean);
		}
		out[k] = vadd(q, v3(b->x, RM_BN_FOOT + b->h * 0.5, RM_BN_ZC));
	}
}

/* Fit the affine that lays a rectangle onto a projected quad. A planar
 * rectangle seen through a perspective camera is really a trapezium, but
 * over a label 90 pixels wide the difference is under a pixel, and cairo
 * has nothing but affines anyway. */
static int quad_matrix(const struct room_cam *cam, const vec3 q[4],
	double lw, double lh, double ox, double oy, double sw, double sh,
	cairo_matrix_t *m)
{
	double px[4], py[4];
	for (int k = 0; k < 4; k++) {
		double u, v;
		if (!room_project(cam, q[k], &u, &v)) {
			return 0;
		}
		px[k] = ox + u * sw;
		py[k] = oy + v * sh;
	}
	cairo_matrix_init(m,
		(px[1] - px[0]) / lw, (py[1] - py[0]) / lw,
		(px[2] - px[0]) / lh, (py[2] - py[0]) / lh,
		px[0], py[0]);
	return 1;
}

/* ---- a face, printed at the pixels it lands on ----
 *
 * A face is authored at some size -- the clock's is 492 by 76 -- and laid
 * on the desk through the matrix quad_matrix gives. Drawn at its authored
 * size and then resampled, it is as soft as the ratio between the two: a
 * clock magnified 1.26 times to fit a 1918 wide screen has smeared dots.
 * So the face is drawn at the size it will have on the target, and laid
 * down one pixel to a pixel.
 *
 * Where the matrix only scales and moves -- everything on the desk seen
 * from straight above -- the size is rounded to whole pixels and the
 * corner put on a pixel: the face moves by less than half a pixel, and
 * comes out of the copy exactly as it went in. Where it turns or shears
 * (the shelf, seen from the side) it is drawn at the larger of the two
 * scales and resampled as before.
 *
 *	struct face_print f;
 *	if (face_begin(&f, cr, &mt, fw, fh)) {
 *		draw into f.fc in the face's own units, 0..fw by 0..fh
 *		face_end(&f, cr, alpha);      // leaves cr as it found it
 *	}
 * f.mt is the matrix to draw anything else over the face with. */
struct face_print {
	cairo_surface_t *img;
	cairo_t *fc;
	cairo_matrix_t mt;     /* face units to the target's user space, as adjusted */
	double kx, ky;         /* image pixels per face unit */
	bool exact;            /* one image pixel to one device pixel */
};

static bool face_begin(struct face_print *f, cairo_t *cr, const cairo_matrix_t *mt,
	double fw, double fh)
{
	cairo_matrix_t ctm, tot, inv;
	cairo_get_matrix(cr, &ctm);
	cairo_matrix_multiply(&tot, mt, &ctm);
	inv = ctm;
	bool invertible = cairo_matrix_invert(&inv) == CAIRO_STATUS_SUCCESS;
	double sx = hypot(tot.xx, tot.yx), sy = hypot(tot.xy, tot.yy);
	/* only scaled and moved: no turn, no shear, nothing mirrored, and the
	 * same scale both ways (a thing seen from straight above is) */
	bool upright = invertible && fabs(tot.yx) <= 1e-6 * sx && fabs(tot.xy) <= 1e-6 * sy
		&& tot.xx > 0.0 && tot.yy > 0.0 && fabs(sx - sy) <= 1e-4 * sx
		&& fabs(ctm.yx) <= 1e-9 && fabs(ctm.xy) <= 1e-9;
	/* one scale for the image, the larger of the two where they differ */
	double k = fmax(sx, sy);
	if (k < 0.5 || k > 4.0) {
		k = clampf(k, 0.5, 4.0);
		upright = false;
	}
	int pw = (int)ceil(fw * k), ph = (int)ceil(fh * k);
	if (pw > 4096 || ph > 4096) {
		k *= 4096.0 / (double)(pw > ph ? pw : ph);
		pw = (int)ceil(fw * k);
		ph = (int)ceil(fh * k);
		upright = false;
	}
	f->mt = *mt;
	f->exact = false;
	if (upright) {
		/* the image is authored at the scale it lands at, and its corner is put
		 * on a pixel (it moves by less than half of one): copied 1:1 it comes
		 * out exactly as it went in */
		cairo_matrix_t t2 = tot;
		t2.xx = t2.yy = k;
		t2.x0 = round(tot.x0);
		t2.y0 = round(tot.y0);
		cairo_matrix_multiply(&f->mt, &t2, &inv);
		f->exact = true;
	}
	f->kx = f->ky = k;
	f->img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw < 8 ? 8 : pw, ph < 8 ? 8 : ph);
	f->fc = cairo_create(f->img);
	cairo_scale(f->fc, k, k);
	return true;
}

static void face_end(struct face_print *f, cairo_t *cr, double alpha)
{
	cairo_destroy(f->fc);
	cairo_surface_flush(f->img);
	cairo_save(cr);
	cairo_transform(cr, &f->mt);
	cairo_scale(cr, 1.0 / f->kx, 1.0 / f->ky);
	cairo_set_source_surface(cr, f->img, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr),
		f->exact ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_GOOD);
	cairo_paint_with_alpha(cr, alpha);
	cairo_restore(cr);
	cairo_surface_destroy(f->img);
}

/* What is typed on a binder's spine: what is in it, and how much. */
/* What a binder is called, in the desk's language. */
static const char *shelf_label(const struct desk_shelf_info *si)
{
	static const char *const KEYS[] = { "shelf|Development", "shelf|Internet", "shelf|Office",
		"shelf|Graphics", "shelf|Media", "shelf|Games", "shelf|Utilities", "shelf|System",
		"shelf|Favorites" };
	for (size_t i = 0; si->label_en && i < sizeof KEYS / sizeof KEYS[0]; i++) {
		if (!strcmp(KEYS[i] + strlen("shelf|"), si->label_en)) {
			return TR(KEYS[i]);
		}
	}
	return si->label_en;
}

/* How dark the binders' labels are typed (shell.conf binder_ink): 0 with
 * the tired ribbon they were designed with, 1 a fresh one pressed hard.
 * A pale label on a dim shelf is hard to read, so the default is already
 * a little past the tired ribbon. */
static double g_binder_ink = 0.4;

bool desk_set_binder_ink(double depth)
{
	depth = clampf(depth, 0.0, 1.0);
	if (fabs(depth - g_binder_ink) < 1e-6) {
		return false;
	}
	g_binder_ink = depth;
	/* the labels are printed into the case, which is kept: print it again */
	desk_draw_forget_words();
	return true;
}

static void lkfx_star(cairo_t *cr, double x, double y, double r);

/* The card in binder thing's pocket is as wide as the pocket is: the
 * favourites' is narrower, and typed on as narrow. */
static double binder_label_w(int thing)
{
	return 132.0 * shelf_binder_of(thing)->hw / 1.65;
}

static void binder_label_face(cairo_t *cr, double w, double h,
	const char *name, int count, unsigned seed, bool star)
{
	/* This card was typed once and has been in the pocket ever since. It
	 * is not white, the ribbon was tired, somebody's thumb has been on the
	 * bottom corner every time the binder came down, and one corner has
	 * stopped lying flat. */
	double years = 0.62 + hash21(seed, 41.3) * 0.34;
	double ink = g_binder_ink;
	cairo_save(cr);
	cairo_matrix_t card;
	cairo_get_matrix(cr, &card);
	cairo_set_source_rgb(cr, 0.874, 0.840, 0.758);
	cairo_paint(cr);
	paper_age(cr, w, h, seed + 71, years);

	/* typed, so it is off square to the card it was typed on */
	cairo_translate(cr, w * 0.5, h * 0.5);
	cairo_rotate(cr, (hash21(seed, 3.7) - 0.5) * 0.030);
	cairo_translate(cr, -w * 0.5, -h * 0.5);

	cairo_set_source_rgba(cr, 0.24, 0.18, 0.12, 0.52 + 0.30 * ink);
	cairo_set_line_width(cr, 1.2);
	cairo_rectangle(cr, w * 0.10, h * 0.09, w * 0.80, h * 0.82);
	cairo_stroke(cr);

	if (!star) {
		/* typed smaller for a long name (Développement): the card is no wider */
		show_text_fit_to(cr, DESK_FONT_TYPE, name, w * 0.5, h * 0.42, h * 0.125, true,
			0.16 - 0.11 * ink, 0.12 - 0.08 * ink, 0.09 - 0.06 * ink, 0.95 + 0.05 * ink, 0,
			w * 0.72, 0.35);
	}

	wobble_line(cr, w * 0.20, h * 0.60, w * 0.80, h * 0.60, seed, 3, 0.5, 1.0,
		0.30, 0.22, 0.15, 0.55 + 0.30 * ink);

	char buf[16];
	snprintf(buf, sizeof buf, "%d", count);
	show_text_as(cr, DESK_FONT_TYPE, buf, w * 0.5, h * 0.80, h * 0.155, true,
		0.42 - 0.10 * ink, 0.14 - 0.06 * ink, 0.10 - 0.05 * ink, 0.92 + 0.08 * ink, 0);

	cairo_identity_matrix(cr);

	/* the ribbon, over everything that was typed with it -- missing less of
	 * it the fresher the ribbon is */
	double fresh = 1.0 - 0.9 * ink;
	ink_skip(cr, w * 0.10, h * 0.16, w * 0.80, h * 0.44, seed + 3,
		(0.30 + years * 0.26) * fresh, 0.876, 0.842, 0.762);
	ink_skip(cr, w * 0.30, h * 0.66, w * 0.40, h * 0.24, seed + 9,
		(0.20 + years * 0.18) * fresh, 0.876, 0.842, 0.762);

	if (star) {
		/* The favourites: a star stamped where the name would be typed --
		 * stamped, not typed, so the ribbon has nothing to do with it --
		 * in the red of a stamp pad, not quite straight. Its card is too
		 * narrow for お気に入り or Favoriten to be read from the desk;
		 * the name is on its board, inside. */
		cairo_save(cr);
		cairo_set_matrix(cr, &card);
		cairo_translate(cr, w * 0.5, h * 0.36);
		cairo_rotate(cr, (hash21(seed, 8.1) - 0.5) * 0.30);
		lkfx_star(cr, 0.0, 0.0, fmin(w * 0.34, h * 0.16));
		cairo_set_source_rgba(cr, 0.66, 0.11, 0.08, 0.86);
		cairo_fill(cr);
		cairo_restore(cr);
	}

	/* the thumb that takes this binder down, always in the same place */
	double tx = hash21(seed, 19.7) > 0.5 ? w * 0.26 : w * 0.74;
	cairo_pattern_t *th = cairo_pattern_create_radial(tx, h * 0.90, 0,
		tx, h * 0.90, w * 0.40);
	cairo_pattern_add_color_stop_rgba(th, 0.0, 0.284, 0.226, 0.156, 0.26 * years);
	cairo_pattern_add_color_stop_rgba(th, 1.0, 0.284, 0.226, 0.156, 0.0);
	cairo_set_source(cr, th);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_fill(cr);
	cairo_pattern_destroy(th);

	paper_crease(cr, w, h, seed + 17, 0.07);
	paper_edge(cr, w, h, seed + 5, 0.75 + years * 0.25);
	overlay_grain(cr, 0, 0, w, h, 1.2);
	cairo_restore(cr);
}

/* Where the shelf sheet lands on screen. It is baked at the sheet's own
 * resolution and shown whole, so this is only the scale. */
static int shelf_xform(int screen_w, int screen_h, struct room_render **out_rr,
	double *ox, double *oy, double *scale)
{
	struct room_render *rr = room_for_view(screen_w, screen_h, DESK_VIEW_SHELF);
	if (!rr || !rr->shelf) {
		return 0;
	}
	double sx = (double)screen_w / rr->w, sy = (double)screen_h / rr->h;
	*scale = sx < sy ? sy : sx;
	*ox = (screen_w - rr->w * *scale) * 0.5;
	*oy = (screen_h - rr->h * *scale) * 0.5;
	*out_rr = rr;
	return 1;
}

int desk_shelf_hit(int screen_w, int screen_h, double px, double py)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!shelf_xform(screen_w, screen_h, &rr, &ox, &oy, &scale) || !rr->shelf_ids) {
		return -1;
	}
	int sx = (int)((px - ox) / scale + 0.5);
	int sy = (int)((py - oy) / scale + 0.5);
	if (sx < 0 || sy < 0 || sx >= rr->w || sy >= rr->h) {
		return -1;
	}
	int id = rr->shelf_ids[(size_t)sy * rr->w + sx];
	return id > 0 && id <= DESK_SHELF_THINGS ? id - 1 : -1;
}

/* The bookcase with its labels on, painted once and kept: a binder coming
 * forward under the pointer is then a blit and a transform rather than a
 * repaint of the whole case. */
static cairo_surface_t *g_shelf_base;
static unsigned g_shelf_base_room;   /* rr->serial it was painted for; 0 none */
static unsigned g_shelf_base_gen;
/* what the day puts on the lower board (DESK_DECOR_HALLOWEEN, or 0), and
 * what it was when the case was painted */
static unsigned g_shelf_decor, g_shelf_base_decor;

static void shelf_fall_paint(const struct room_render *rr, cairo_surface_t *s);

void desk_shelf_dress(unsigned decor)
{
	g_shelf_decor = decor & DESK_DECOR_HALLOWEEN;
}

/* ---- the book's spine, tooled ----
 *
 * Gold on navy morocco: a double rule either side of every band, a red
 * lettering-piece between the top two bands with ALL APPS on it, under it
 * a glass to look things up with, then A-Z, then how many programs are in
 * it, and a flower at the head and the tail. Laid on over the bake, like
 * the binders' labels, so the count is always the count. */

/* the part of the spine it is printed on: across it, and from just over
 * its foot to just under its head, on the round of it */
#define BK_SPINE_Y0 0.10
#define BK_SPINE_Y1 (RM_BK_H - 0.22)

static void book_spine_quad(vec3 out[4])
{
	double hx = RM_BK_HW * 0.90, z = RM_BK_ZC + RM_BK_D * 0.5 + 0.11;
	for (int k = 0; k < 4; k++) {
		out[k] = v3(RM_BK_X + ((k & 1) ? hx : -hx),
			RM_BN_FOOT + ((k & 2) ? BK_SPINE_Y0 : BK_SPINE_Y1), z);
	}
}

static int cal_letter(const char *s, char out[8]);

/* gold leaf: a path filled with the shine of it, its edge pressed in */
static void gold_fill(cairo_t *cr, double y0, double y1)
{
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y0, 0, y1);
	cairo_pattern_add_color_stop_rgb(g, 0.00, 0.98, 0.86, 0.52);
	cairo_pattern_add_color_stop_rgb(g, 0.45, 0.80, 0.60, 0.24);
	cairo_pattern_add_color_stop_rgb(g, 0.55, 0.70, 0.50, 0.18);
	cairo_pattern_add_color_stop_rgb(g, 1.00, 0.92, 0.74, 0.36);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_set_source_rgba(cr, 0.20, 0.12, 0.03, 0.45);
	cairo_set_line_width(cr, 0.6);
	cairo_stroke(cr);
}

static void gold_text(cairo_t *cr, const char *t, double cx, double base, double size,
	double track, bool bold)
{
	cairo_save(cr);
	desk_font_select(cr, DESK_FONT_DISPLAY, t, bold, false);
	cairo_set_font_size(cr, size);
	double w = 0.0;
	char ch[8];
	for (const char *p = t; *p;) {
		p += cal_letter(p, ch);
		cairo_text_extents_t e;
		cairo_text_extents(cr, ch, &e);
		w += e.x_advance + (*p ? track : 0.0);
	}
	double x = cx - w * 0.5;
	cairo_new_path(cr);
	for (const char *p = t; *p;) {
		p += cal_letter(p, ch);
		cairo_text_extents_t e;
		cairo_text_extents(cr, ch, &e);
		cairo_move_to(cr, x, base);
		cairo_text_path(cr, ch);
		x += e.x_advance + track;
	}
	gold_fill(cr, base - size * 0.8, base);
	cairo_restore(cr);
}

/* a small flower of four leaves and a dot */
static void gold_fleuron(cairo_t *cr, double cx, double cy, double r)
{
	cairo_new_path(cr);
	for (int i = 0; i < 4; i++) {
		double a = M_PI * 0.5 * i;
		double tx = cx + cos(a) * r, ty = cy + sin(a) * r;
		double nx = -sin(a) * r * 0.32, ny = cos(a) * r * 0.32;
		cairo_move_to(cr, cx, cy);
		cairo_curve_to(cr, cx + (tx - cx) * 0.4 + nx, cy + (ty - cy) * 0.4 + ny,
			tx + nx * 0.5, ty + ny * 0.5, tx, ty);
		cairo_curve_to(cr, tx - nx * 0.5, ty - ny * 0.5,
			cx + (tx - cx) * 0.4 - nx, cy + (ty - cy) * 0.4 - ny, cx, cy);
	}
	cairo_new_sub_path(cr);
	cairo_arc(cr, cx, cy, r * 0.16, 0, 2 * M_PI);
	gold_fill(cr, cy - r, cy + r);
}

static void book_spine_face(cairo_t *cr, double W, double H, int count)
{
	/* where something up the spine is, down the face */
	double span = BK_SPINE_Y1 - BK_SPINE_Y0;
	#define BK_FY(y) ((BK_SPINE_Y1 - (y)) / span * H)
	double bh = 0.065 * 2.0 / span * H;   /* a band, its height on the face */
	/* the double rules either side of each band */
	for (int k = 0; k < RM_BK_BANDS; k++) {
		double yc = BK_FY(book_band_y(k));
		for (int sgn = -1; sgn <= 1; sgn += 2) {
			for (int r = 0; r < 2; r++) {
				double y = yc + sgn * (bh * 0.75 + r * H * 0.009);
				cairo_rectangle(cr, W * 0.06, y - H * 0.0018, W * 0.88, H * 0.0036);
				gold_fill(cr, y - 2, y + 2);
			}
		}
	}
	/* the lettering-piece: red morocco, between the top two bands */
	double l0 = BK_FY(book_band_y(4)) + bh * 1.6, l1 = BK_FY(book_band_y(3)) - bh * 1.6;
	cairo_save(cr);
	desk_rounded(cr, W * 0.10, l0, W * 0.80, l1 - l0, W * 0.04);
	cairo_pattern_t *red = cairo_pattern_create_linear(0, l0, 0, l1);
	cairo_pattern_add_color_stop_rgb(red, 0.0, 0.50, 0.10, 0.08);
	cairo_pattern_add_color_stop_rgb(red, 1.0, 0.36, 0.06, 0.05);
	cairo_set_source(cr, red);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(red);
	cairo_set_line_width(cr, H * 0.004);
	cairo_set_source_rgba(cr, 0.90, 0.72, 0.34, 0.85);
	cairo_stroke(cr);
	cairo_restore(cr);
	double lm = (l0 + l1) * 0.5, ls = (l1 - l0);
	gold_text(cr, TR("spine|ALL"), W * 0.5, lm - ls * 0.06, ls * 0.30, ls * 0.04, true);
	gold_text(cr, TR("spine|APPS"), W * 0.5, lm + ls * 0.30, ls * 0.30, ls * 0.02, true);
	/* the glass */
	{
		double y0 = BK_FY(book_band_y(3)), y1 = BK_FY(book_band_y(2));
		double cy = y0 + (y1 - y0) * 0.42, r = W * 0.20;
		cairo_new_path(cr);
		cairo_arc(cr, W * 0.46, cy - r * 0.2, r, 0, 2 * M_PI);
		cairo_new_sub_path(cr);
		cairo_arc_negative(cr, W * 0.46, cy - r * 0.2, r * 0.70, 2 * M_PI, 0);
		gold_fill(cr, cy - r, cy + r);
		cairo_save(cr);
		cairo_translate(cr, W * 0.46, cy - r * 0.2);
		cairo_rotate(cr, M_PI * 0.25);
		desk_rounded(cr, r * 0.95, -r * 0.16, r * 0.95, r * 0.32, r * 0.12);
		gold_fill(cr, -r * 0.2, r * 0.2);
		cairo_restore(cr);
		gold_text(cr, TR("spine|SEARCH"), W * 0.5, y0 + (y1 - y0) * 0.86, (y1 - y0) * 0.12,
			(y1 - y0) * 0.02, false);
	}
	/* A to Z */
	{
		double y0 = BK_FY(book_band_y(2)), y1 = BK_FY(book_band_y(1));
		gold_text(cr, "A\xe2\x80\x93Z", W * 0.5, y0 + (y1 - y0) * 0.58, (y1 - y0) * 0.36,
			0.0, true);
		gold_text(cr, TR("spine|INDEX"), W * 0.5, y0 + (y1 - y0) * 0.84, (y1 - y0) * 0.12,
			(y1 - y0) * 0.02, false);
	}
	/* how many */
	{
		double y0 = BK_FY(book_band_y(1)), y1 = BK_FY(book_band_y(0));
		char n[16];
		snprintf(n, sizeof n, "%d", count);
		gold_text(cr, n, W * 0.5, y0 + (y1 - y0) * 0.55, (y1 - y0) * 0.30, 0.0, true);
		gold_text(cr, TR("spine|PROGRAMS"), W * 0.5, y0 + (y1 - y0) * 0.80, (y1 - y0) * 0.10,
			(y1 - y0) * 0.01, false);
	}
	/* the head and the tail */
	gold_fleuron(cr, W * 0.5, BK_FY(book_band_y(4)) * 0.50, W * 0.17);
	gold_fleuron(cr, W * 0.5, (BK_FY(book_band_y(0)) + H) * 0.5, W * 0.17);
	#undef BK_FY
}

/* Painted in the sheet's own coordinates, not the screen's: the id buffer
 * is in sheet space, so a binder cut out of this carries its printed label
 * with it. */
static void shelf_base_paint(cairo_t *cr, struct room_render *rr)
{
	double ox = 0.0, oy = 0.0, scale = 1.0;
	(void)scale;
	cairo_set_source_surface(cr, rr->shelf, 0, 0);
	/* as the desk and the post box do -- left on the default, this one
	 * sheet took cairo's slow resampling path */
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);

	/* the labels are printed over the bake rather than in it, so that
	 * installing a package changes what the spine says without re-baking a
	 * bookcase */
	const struct desk_app_list *apps = desk_apps_acquire();
	const double LH = 280.0;
	cairo_surface_t *face = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		132, (int)LH);
	cairo_t *fc = cairo_create(face);
	for (int r = 0; r < SHELF_ROW; r++) {
		int i = shelf_row_thing(r);
		double LW = binder_label_w(i);
		vec3 q[4];
		binder_label_quad(i, q);
		cairo_matrix_t m;
		if (!quad_matrix(&rr->shelf_cam, q, LW, LH, ox, oy,
			rr->w, rr->h, &m)) {
			continue;
		}
		cairo_save(fc);
		cairo_set_operator(fc, CAIRO_OPERATOR_CLEAR);
		cairo_paint(fc);
		cairo_restore(fc);
		cairo_save(fc);
		cairo_rectangle(fc, 0, 0, LW, LH);
		cairo_clip(fc);
		binder_label_face(fc, LW, LH, shelf_label(&apps->shelf[i]), apps->shelf[i].count,
			(unsigned)(i * 37 + 11), i == DESK_SHELF_FAV);
		cairo_restore(fc);
		cairo_surface_flush(face);

		cairo_save(cr);
		cairo_transform(cr, &m);
		cairo_set_source_surface(cr, face, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint_with_alpha(cr, 0.97);
		cairo_restore(cr);
	}
	cairo_destroy(fc);
	cairo_surface_destroy(face);

	/* and the book at the end of the row, its spine tooled in gold */
	{
		vec3 q[4];
		book_spine_quad(q);
		const double SW = 120.0, SH = 120.0 * (BK_SPINE_Y1 - BK_SPINE_Y0) / (RM_BK_HW * 1.80);
		cairo_matrix_t m;
		if (quad_matrix(&rr->shelf_cam, q, SW, SH, ox, oy, rr->w, rr->h, &m)) {
			int count = 0;
			for (int i = 0; i < DESK_SHELF_COUNT; i++) {
				count += apps->shelf[i].count;
			}
			cairo_surface_t *sp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
				(int)SW, (int)ceil(SH));
			cairo_t *sc = cairo_create(sp);
			book_spine_face(sc, SW, SH, count);
			cairo_destroy(sc);
			cairo_surface_flush(sp);
			cairo_save(cr);
			cairo_transform(cr, &m);
			cairo_set_source_surface(cr, sp, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			cairo_paint(cr);
			cairo_restore(cr);
			cairo_surface_destroy(sp);
		}
	}
	desk_apps_release(apps);
}

/* One binder's own pixels, cut out of the painted case. Kept, because the
 * hover animates and scanning a megapixel of ids for a bounding box on
 * every frame of it is real work for an answer that never changes. */
struct shelf_cut_entry {
	cairo_surface_t *surf;
	int x0, y0, w, h;
};
static struct shelf_cut_entry g_cuts[DESK_SHELF_THINGS];
static unsigned g_cuts_room;         /* rr->serial they were cut from; 0 none */
static unsigned g_cuts_gen;

static void shelf_cuts_drop(void)
{
	for (int i = 0; i < DESK_SHELF_THINGS; i++) {
		if (g_cuts[i].surf) {
			cairo_surface_destroy(g_cuts[i].surf);
			g_cuts[i].surf = NULL;
		}
	}
}

static cairo_surface_t *shelf_cut(struct room_render *rr, cairo_surface_t *base,
	int which, int *bx0, int *by0, int *bw, int *bh)
{
	if (g_cuts_room == rr->serial && g_cuts_gen == g_shelf_base_gen && g_cuts[which].surf) {
		*bx0 = g_cuts[which].x0;
		*by0 = g_cuts[which].y0;
		*bw = g_cuts[which].w;
		*bh = g_cuts[which].h;
		return g_cuts[which].surf;
	}
	if (g_cuts_room != rr->serial || g_cuts_gen != g_shelf_base_gen) {
		shelf_cuts_drop();
		g_cuts_room = rr->serial;
		g_cuts_gen = g_shelf_base_gen;
	}
	int x0 = rr->w, y0 = rr->h, x1 = -1, y1 = -1;
	for (int y = 0; y < rr->h; y++) {
		for (int x = 0; x < rr->w; x++) {
			if (rr->shelf_ids[(size_t)y * rr->w + x] != which + 1) {
				continue;
			}
			if (x < x0) {
				x0 = x;
			}
			if (x > x1) {
				x1 = x;
			}
			if (y < y0) {
				y0 = y;
			}
			if (y > y1) {
				y1 = y;
			}
		}
	}
	if (x1 < 0) {
		return NULL;
	}
	int w = x1 - x0 + 1, h = y1 - y0 + 1;
	cairo_surface_t *mask = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
	unsigned char *md = cairo_image_surface_get_data(mask);
	int ms = cairo_image_surface_get_stride(mask);
	for (int y = 0; y < h; y++) {
		unsigned char *row = md + (size_t)y * ms;
		for (int x = 0; x < w; x++) {
			if (rr->shelf_ids[(size_t)(y + y0) * rr->w + (x + x0)] == which + 1) {
				row[x] = 255;
			}
		}
	}
	cairo_surface_mark_dirty(mask);
	cairo_surface_t *cut = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cc = cairo_create(cut);
	cairo_set_source_surface(cc, base, -x0, -y0);
	cairo_mask_surface(cc, mask, 0, 0);
	cairo_destroy(cc);
	cairo_surface_destroy(mask);
	cairo_surface_flush(cut);
	g_cuts[which].surf = cut;
	g_cuts[which].x0 = x0;
	g_cuts[which].y0 = y0;
	g_cuts[which].w = w;
	g_cuts[which].h = h;
	*bx0 = x0;
	*by0 = y0;
	*bw = w;
	*bh = h;
	return cut;
}

/* The case with every label printed on it, for this room and this list of
 * applications. Printing eight spines is most of a tenth of a second, so it
 * is done once, and ahead of time when it can be (desk_shelf_prewarm). */
static void shelf_base_ensure(struct room_render *rr)
{
	unsigned gen = 0;
	{
		const struct desk_app_list *a = desk_apps_acquire();
		gen = a->generation;
		desk_apps_release(a);
	}
	if (!g_shelf_base || g_shelf_base_room != rr->serial || g_shelf_base_gen != gen
		|| g_shelf_base_decor != g_shelf_decor) {
		if (g_shelf_base) {
			cairo_surface_destroy(g_shelf_base);
		}
		g_shelf_base = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rr->w, rr->h);
		cairo_t *bc = cairo_create(g_shelf_base);
		shelf_base_paint(bc, rr);
		cairo_destroy(bc);
		cairo_surface_flush(g_shelf_base);
		if (g_shelf_decor & DESK_DECOR_HALLOWEEN) {
			shelf_fall_paint(rr, g_shelf_base);
		}
		g_shelf_base_room = rr->serial;
		g_shelf_base_gen = gen;
		g_shelf_base_decor = g_shelf_decor;
		shelf_cuts_drop();
	}
}

bool desk_shelf_prewarm(int screen_w, int screen_h)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!shelf_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	shelf_base_ensure(rr);
	return true;
}

void desk_draw_shelf(cairo_t *cr, int screen_w, int screen_h, int hover,
	double hover_t, int taken)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!shelf_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		desk_scene_warm(screen_w, screen_h, true);
		return;
	}
	struct lamp_px lamp_was = lamp_view(screen_w, screen_h);

	shelf_base_ensure(rr);
	cairo_save(cr);
	cairo_translate(cr, ox, oy);
	cairo_scale(cr, scale, scale);
	cairo_set_source_surface(cr, g_shelf_base, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	/* The one you have taken down is not on the shelf. Its own pixels go to
	 * the dark of the empty slot, which is what makes the case a place you
	 * took something from rather than a picture of a case. */
	if (taken >= 0 && taken < DESK_SHELF_THINGS && rr->shelf_ids) {
		cairo_save(cr);
		cairo_translate(cr, ox, oy);
		cairo_scale(cr, scale, scale);
		cairo_surface_t *slot = cairo_image_surface_create(CAIRO_FORMAT_A8, rr->w, rr->h);
		unsigned char *sd = cairo_image_surface_get_data(slot);
		int ss = cairo_image_surface_get_stride(slot);
		for (int y = 0; y < rr->h; y++) {
			unsigned char *row = sd + (size_t)y * ss;
			for (int x = 0; x < rr->w; x++) {
				if (rr->shelf_ids[(size_t)y * rr->w + x] == taken + 1) {
					row[x] = 255;
				}
			}
		}
		cairo_surface_mark_dirty(slot);
		cairo_set_source_rgba(cr, 0.030, 0.020, 0.014, 0.93);
		cairo_mask_surface(cr, slot, 0, 0);
		cairo_surface_destroy(slot);
		cairo_restore(cr);
	}

	/* The one under the hand comes out of the row towards you, the way you
	 * pull a book half off a shelf before deciding to take it. Its own
	 * pixels are cut out of the case, the slot behind them goes dark, and
	 * the cut is drawn back bigger and lower -- which is what coming
	 * forward looks like to a camera that is above and in front. */
	double ht = clampf(hover_t, 0.0, 1.0);
	if (hover >= 0 && hover < DESK_SHELF_THINGS && rr->shelf_ids && ht > 0.004) {
		int bx0, by0, bw, bh;
		cairo_surface_t *cut = shelf_cut(rr, g_shelf_base, hover, &bx0, &by0, &bw, &bh);
		if (cut) {
			cairo_save(cr);
			cairo_translate(cr, ox, oy);
			cairo_scale(cr, scale, scale);

			/* the slot it is leaving */
			cairo_save(cr);
			cairo_set_source_surface(cr, cut, bx0, by0);
			cairo_paint(cr);
			cairo_set_source_rgba(cr, 0.035, 0.024, 0.016, 0.88 * ht);
			cairo_mask_surface(cr, cut, bx0, by0);
			cairo_restore(cr);

			double lift = bh * 0.085 * ht;
			double k = 1.0 + 0.075 * ht;
			double cx = bx0 + bw * 0.5, cy = by0 + bh * 0.5;
			cairo_save(cr);
			cairo_translate(cr, cx, cy + lift);
			cairo_scale(cr, k, k);
			cairo_translate(cr, -cx, -cy);
			/* what it throws back into the slot */
			cairo_set_source_rgba(cr, 0.02, 0.014, 0.008, 0.40 * ht);
			cairo_mask_surface(cr, cut, bx0 - bw * 0.010, by0 - bh * 0.030);
			cairo_set_source_surface(cr, cut, bx0, by0);
			cairo_paint(cr);
			/* and the light it catches now it is clear of its neighbours */
			cairo_set_source_rgba(cr, 1.00, 0.92, 0.74, 0.12 * ht);
			cairo_mask_surface(cr, cut, bx0, by0);
			cairo_restore(cr);
			cairo_restore(cr);
		}
	}

	overlay_grain(cr, 0, 0, screen_w, screen_h, 0.55);
	g_light = lamp_was;
}

/* ---- things held up in front of the eye ------------------------------
 *
 * What is taken off the shelf to be looked at close -- the calendar, the
 * binder of notes -- is a thing in the room, not a picture pasted over it:
 * held a little turned, the side going away smaller than the side coming
 * towards you, its edges showing their thickness, lit more where it faces
 * the light. It is drawn flat first, as a picture of its face, and that
 * picture is laid into the view through a perspective: a plane turned
 * about a point of it (yaw: its right side going away; pitch: its top
 * going away) and seen by an eye f pixels in front of the screen over
 * (ex, ey). A plane seen so is a projective map of its picture
 * (struct hmat), which goes both ways: the picture onto the screen, and
 * the pointer on the screen back into the picture to find what it is on. */

struct hmat {
	double m[9];   /* (x, y, 1) -> (X, Y, W), row by row */
};

static void hmat_apply(const struct hmat *h, double x, double y, double *ox, double *oy)
{
	const double *m = h->m;
	double w = m[6] * x + m[7] * y + m[8];
	if (fabs(w) < 1e-12) {
		w = w < 0.0 ? -1e-12 : 1e-12;
	}
	*ox = (m[0] * x + m[1] * y + m[2]) / w;
	*oy = (m[3] * x + m[4] * y + m[5]) / w;
}

/* a then b: the map that does b to what a has done */
static void hmat_then(const struct hmat *a, const struct hmat *b, struct hmat *out)
{
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			out->m[r * 3 + c] = b->m[r * 3] * a->m[c] + b->m[r * 3 + 1] * a->m[3 + c]
				+ b->m[r * 3 + 2] * a->m[6 + c];
		}
	}
}

static bool hmat_invert(const struct hmat *a, struct hmat *out)
{
	const double *m = a->m;
	double c0 = m[4] * m[8] - m[5] * m[7];
	double c1 = m[5] * m[6] - m[3] * m[8];
	double c2 = m[3] * m[7] - m[4] * m[6];
	double det = m[0] * c0 + m[1] * c1 + m[2] * c2;
	if (fabs(det) < 1e-18) {
		return false;
	}
	double k = 1.0 / det;
	double *o = out->m;
	o[0] = c0 * k;
	o[1] = (m[2] * m[7] - m[1] * m[8]) * k;
	o[2] = (m[1] * m[5] - m[2] * m[4]) * k;
	o[3] = c1 * k;
	o[4] = (m[0] * m[8] - m[2] * m[6]) * k;
	o[5] = (m[2] * m[3] - m[0] * m[5]) * k;
	o[6] = c2 * k;
	o[7] = (m[1] * m[6] - m[0] * m[7]) * k;
	o[8] = (m[0] * m[4] - m[1] * m[3]) * k;
	return true;
}

/* The rectangle x0, y0, w by h onto the four corners q (top left, top
 * right, bottom right, bottom left): Heckbert's square to quadrilateral,
 * after the rectangle onto the unit square. */
static bool hmat_rect_to_quad(double x0, double y0, double w, double h, const double q[4][2],
	struct hmat *out)
{
	if (fabs(w) < 1e-9 || fabs(h) < 1e-9) {
		return false;
	}
	double sx = q[0][0] - q[1][0] + q[2][0] - q[3][0];
	double sy = q[0][1] - q[1][1] + q[2][1] - q[3][1];
	double a, b, c, d, e, f, g, hh;
	if (fabs(sx) < 1e-9 && fabs(sy) < 1e-9) {
		a = q[1][0] - q[0][0];
		b = q[2][0] - q[1][0];
		c = q[0][0];
		d = q[1][1] - q[0][1];
		e = q[2][1] - q[1][1];
		f = q[0][1];
		g = hh = 0.0;
	} else {
		double dx1 = q[1][0] - q[2][0], dx2 = q[3][0] - q[2][0];
		double dy1 = q[1][1] - q[2][1], dy2 = q[3][1] - q[2][1];
		double del = dx1 * dy2 - dx2 * dy1;
		if (fabs(del) < 1e-12) {
			return false;
		}
		g = (sx * dy2 - sy * dx2) / del;
		hh = (dx1 * sy - dy1 * sx) / del;
		a = q[1][0] - q[0][0] + g * q[1][0];
		b = q[3][0] - q[0][0] + hh * q[3][0];
		c = q[0][0];
		d = q[1][1] - q[0][1] + g * q[1][1];
		e = q[3][1] - q[0][1] + hh * q[3][1];
		f = q[0][1];
	}
	/* (u, v) = ((x - x0) / w, (y - y0) / h) first */
	double iw = 1.0 / w, ih = 1.0 / h;
	struct hmat r = { { a * iw, b * ih, c - a * x0 * iw - b * y0 * ih,
		d * iw, e * ih, f - d * x0 * iw - e * y0 * ih,
		g * iw, hh * ih, 1.0 - g * x0 * iw - hh * y0 * ih } };
	*out = r;
	return true;
}

/* A turn of a flat thing in front of the eye, as above. */
struct turn3 {
	double px, py;        /* the point it turns about, on the screen */
	double yaw, pitch;
	double ex, ey, f;     /* the eye */
};

/* A point of the thing's own frame -- x, y as in the flat drawing, z
 * behind its face (away from you) -- turned with it: about the turn's
 * point, top going away by pitch, then right side going away by yaw.
 * Relative to that point; a direction turns the same way. */
static void turn3_rot(const struct turn3 *t, double x, double y, double z, double *ox,
	double *oy, double *oz)
{
	double cp = cos(t->pitch), sp = sin(t->pitch);
	double y1 = y * cp + z * sp, z1 = -y * sp + z * cp;
	double cy = cos(t->yaw), sy = sin(t->yaw);
	*ox = x * cy - z1 * sy;
	*oy = y1;
	*oz = x * sy + z1 * cy;
}

/* Where the point x, y, z of it lands on the screen, and how far off it
 * is (depth, against the plane of the screen; > 0 going away). */
static void turn3_project(const struct turn3 *t, double x, double y, double z, double *sx,
	double *sy, double *depth)
{
	double X, Y, Z;
	turn3_rot(t, x - t->px, y - t->py, z, &X, &Y, &Z);
	double k = t->f / fmax(t->f + Z, 1.0);
	*sx = t->ex + (t->px + X - t->ex) * k;
	*sy = t->ey + (t->py + Y - t->ey) * k;
	if (depth) {
		*depth = Z;
	}
}

static void turn3_point(const struct turn3 *t, double x, double y, double *sx, double *sy,
	double *depth)
{
	turn3_project(t, x, y, 0.0, sx, sy, depth);
}

/* Whether a face of it at x, y, z with outward normal n (in its own
 * frame) is turned towards the eye. */
static bool turn3_facing(const struct turn3 *t, double x, double y, double z, double nx,
	double ny, double nz)
{
	double X, Y, Z, NX, NY, NZ;
	turn3_rot(t, x - t->px, y - t->py, z, &X, &Y, &Z);
	turn3_rot(t, nx, ny, nz, &NX, &NY, &NZ);
	double ex = t->ex - (t->px + X), ey = t->ey - (t->py + Y), ez = -t->f - Z;
	return NX * ex + NY * ey + NZ * ez > 0.0;
}

/* How much light a face of it with outward normal n (its own frame) takes
 * from a light coming from the direction l (the eye's frame: x right, y
 * down, z away), against the face turned square to the light: 0 to 1. */
static double turn3_lit(const struct turn3 *t, double nx, double ny, double nz, double lx,
	double ly, double lz)
{
	double NX, NY, NZ;
	turn3_rot(t, nx, ny, nz, &NX, &NY, &NZ);
	double ln = sqrt(lx * lx + ly * ly + lz * lz);
	return fmax(0.0, (NX * lx + NY * ly + NZ * lz) / fmax(ln, 1e-9));
}

/* The flat drawing's rectangle x, y, w by h, turned: its picture on the
 * screen as a projective map. */
static bool turn3_hmat(const struct turn3 *t, double x, double y, double w, double h,
	struct hmat *out)
{
	double q[4][2];
	turn3_point(t, x, y, &q[0][0], &q[0][1], NULL);
	turn3_point(t, x + w, y, &q[1][0], &q[1][1], NULL);
	turn3_point(t, x + w, y + h, &q[2][0], &q[2][1], NULL);
	turn3_point(t, x, y + h, &q[3][0], &q[3][1], NULL);
	return hmat_rect_to_quad(x, y, w, h, q, out);
}

/* Two premultiplied pixels mixed, f/256 of the way from a to b: the red
 * and blue together in one word, the alpha and green in another. */
static inline uint32_t px_mix(uint32_t a, uint32_t b, unsigned f)
{
	unsigned g = 256 - f;
	uint32_t rb = (((a & 0xff00ffu) * g + (b & 0xff00ffu) * f) >> 8) & 0xff00ffu;
	uint32_t ag = (((a >> 8) & 0xff00ffu) * g + ((b >> 8) & 0xff00ffu) * f) & 0xff00ff00u;
	return rb | ag;
}

/* a premultiplied pixel times k/256 */
static inline uint32_t px_scale(uint32_t a, unsigned k)
{
	uint32_t rb = (((a & 0xff00ffu) * k) >> 8) & 0xff00ffu;
	uint32_t ag = (((a >> 8) & 0xff00ffu) * k) & 0xff00ff00u;
	return rb | ag;
}

/* One pixel of the target, looking back to (U, V) in the picture, in
 * 16.16: taken there bilinearly, faded by ka/256, and laid over what is
 * there. Past the picture's edge it is clear. */
static inline void warp_pixel(int32_t U, int32_t V, const unsigned char *sp, int ss, int iw,
	int ih, unsigned ka, uint32_t *dst)
{
	/* u, v at or past -1 in front, iw, ih behind: nothing of it there */
	if (U <= -65536 || V <= -65536 || U >= (int32_t)iw << 16 || V >= (int32_t)ih << 16) {
		return;
	}
	int iu = U >> 16, iv = V >> 16;   /* floors: the shift is arithmetic */
	unsigned fu = ((uint32_t)U >> 8) & 0xffu, fv = ((uint32_t)V >> 8) & 0xffu;
	uint32_t t[4];
	if (iu >= 0 && iv >= 0 && iu + 1 < iw && iv + 1 < ih) {
		const uint32_t *r0 = (const uint32_t *)(sp + (size_t)iv * ss) + iu;
		const uint32_t *r1 = (const uint32_t *)((const unsigned char *)r0 + ss);
		t[0] = r0[0];
		t[1] = r0[1];
		t[2] = r1[0];
		t[3] = r1[1];
	} else {
		for (int k = 0; k < 4; k++) {
			int cx = iu + (k & 1), cy2 = iv + (k >> 1);
			t[k] = cx >= 0 && cy2 >= 0 && cx < iw && cy2 < ih
				? ((const uint32_t *)(sp + (size_t)cy2 * ss))[cx] : 0;
		}
	}
	if (!(t[0] | t[1] | t[2] | t[3])) {
		return;
	}
	uint32_t out = px_mix(px_mix(t[0], t[1], fu), px_mix(t[2], t[3], fu), fv);
	if (ka < 256) {
		out = px_scale(out, ka);
	}
	unsigned sa = out >> 24;
	if (sa == 0) {
		return;
	}
	uint32_t d = *dst;
	if (sa >= 255 || !d) {
		*dst = out;
		return;
	}
	/* over what is there: out + d (255 - sa) / 255, in pairs */
	unsigned ia = 255 - sa;
	uint32_t rb = (d & 0xff00ffu) * ia + 0x800080u;
	rb = ((rb + ((rb >> 8) & 0xff00ffu)) >> 8) & 0xff00ffu;
	uint32_t ag = ((d >> 8) & 0xff00ffu) * ia + 0x800080u;
	ag = (ag + ((ag >> 8) & 0xff00ffu)) & 0xff00ff00u;
	*dst = out + (rb | ag);
}

/* how many pixels apart warp_paint works out exactly where it looks */
#define WARP_SPAN 16

/* Pixels xs to xe of a row, each worked out exactly: for the span the eye's
 * own plane goes through, or one that looks back very far. m is the map
 * from the target back to the picture; bu, bv, bw the row's part of it. */
static void warp_run_exact(const double *m, double bu, double bv, double bw, int xs, int xe,
	const unsigned char *sp, int ss, int iw, int ih, unsigned ka, uint32_t *row)
{
	for (int x = xs; x < xe; x++) {
		double X = x + 0.5;
		double nw = m[6] * X + bw;
		if (nw <= 1e-12) {
			continue;
		}
		double u = (m[0] * X + bu) / nw - 0.5, v = (m[3] * X + bv) / nw - 0.5;
		if (u <= -1.0 || v <= -1.0 || u >= iw || v >= ih) {
			continue;
		}
		warp_pixel((int32_t)lrint(u * 65536.0), (int32_t)lrint(v * 65536.0), sp, ss, iw, ih,
			ka, &row[x]);
	}
}

/* Lay picture src -- its pixel (0, 0) at x0, y0 of the flat drawing, kx
 * pixels to its unit -- into cr's target through map (flat drawing to
 * cr's user space), faded by alpha. Each pixel of the target looks back
 * through the map into the picture and takes it there, bilinearly; past
 * its edge the picture is clear, which gives its edges their softness. */
static void warp_paint(cairo_t *cr, cairo_surface_t *src, double x0, double y0, double kx,
	const struct hmat *map, double alpha)
{
	cairo_surface_t *dst = cairo_get_target(cr);
	if (cairo_surface_get_type(dst) != CAIRO_SURFACE_TYPE_IMAGE
		|| cairo_image_surface_get_format(dst) != CAIRO_FORMAT_ARGB32
		|| cairo_image_surface_get_format(src) != CAIRO_FORMAT_ARGB32 || alpha <= 0.0) {
		return;
	}
	int iw = cairo_image_surface_get_width(src), ih = cairo_image_surface_get_height(src);
	int dw = cairo_image_surface_get_width(dst), dh = cairo_image_surface_get_height(dst);
	/* picture pixels -> flat drawing -> user space -> device: one map */
	cairo_matrix_t u2d;
	cairo_get_matrix(cr, &u2d);
	double dxo, dyo;
	cairo_surface_get_device_offset(dst, &dxo, &dyo);
	struct hmat pic = { { 1.0 / kx, 0.0, x0, 0.0, 1.0 / kx, y0, 0.0, 0.0, 1.0 } };
	struct hmat dev = { { u2d.xx, u2d.xy, u2d.x0 + dxo, u2d.yx, u2d.yy, u2d.y0 + dyo,
		0.0, 0.0, 1.0 } };
	struct hmat t1, all;
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			t1.m[r * 3 + c] = map->m[r * 3] * pic.m[c] + map->m[r * 3 + 1] * pic.m[3 + c]
				+ map->m[r * 3 + 2] * pic.m[6 + c];
		}
	}
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			all.m[r * 3 + c] = dev.m[r * 3] * t1.m[c] + dev.m[r * 3 + 1] * t1.m[3 + c]
				+ dev.m[r * 3 + 2] * t1.m[6 + c];
		}
	}
	struct hmat inv;
	if (!hmat_invert(&all, &inv)) {
		return;
	}
	/* where the picture lands, to the pixel */
	double bx0 = 1e30, by0 = 1e30, bx1 = -1e30, by1 = -1e30;
	const double cu[4] = { 0, iw, iw, 0 }, cv[4] = { 0, 0, ih, ih };
	for (int i = 0; i < 4; i++) {
		double X, Y;
		hmat_apply(&all, cu[i], cv[i], &X, &Y);
		bx0 = fmin(bx0, X);
		by0 = fmin(by0, Y);
		bx1 = fmax(bx1, X);
		by1 = fmax(by1, Y);
	}
	int x_0 = (int)floor(bx0) - 1, y_0 = (int)floor(by0) - 1;
	int x_1 = (int)ceil(bx1) + 1, y_1 = (int)ceil(by1) + 1;
	x_0 = x_0 < 0 ? 0 : x_0;
	y_0 = y_0 < 0 ? 0 : y_0;
	x_1 = x_1 > dw ? dw : x_1;
	y_1 = y_1 > dh ? dh : y_1;
	if (x_1 <= x_0 || y_1 <= y_0) {
		return;
	}
	cairo_surface_flush(dst);
	cairo_surface_flush(src);
	const unsigned char *sp = cairo_image_surface_get_data(src);
	unsigned char *dp = cairo_image_surface_get_data(dst);
	int ss = cairo_image_surface_get_stride(src), ds = cairo_image_surface_get_stride(dst);
	unsigned ka = (unsigned)lround(clampf(alpha, 0.0, 1.0) * 256.0);
	const double *m = inv.m;
	/* DESK_WARP_EXACT: every pixel worked out on its own, as it was, to
	 * hold the stepped one against */
	bool exact = getenv("DESK_WARP_EXACT") != NULL;
	for (int y = y_0; y < y_1; y++) {
		uint32_t *row = (uint32_t *)(dp + (size_t)y * ds);
		double Y = y + 0.5;
		/* Along a row each of u, v and w goes in a straight line, so where
		 * the picture is on it -- u and v inside it, w ahead of the eye --
		 * is one run of the row: found first, so the corners of the box it
		 * is in cost nothing. Each bound is a X + b > 0. */
		double bu = m[1] * Y + m[2], bv = m[4] * Y + m[5], bw = m[7] * Y + m[8];
		const double lin[5][2] = {
			{ m[0] + 0.5 * m[6], bu + 0.5 * bw },                    /* u > -1 */
			{ (iw + 0.5) * m[6] - m[0], (iw + 0.5) * bw - bu },      /* u < iw */
			{ m[3] + 0.5 * m[6], bv + 0.5 * bw },                    /* v > -1 */
			{ (ih + 0.5) * m[6] - m[3], (ih + 0.5) * bw - bv },      /* v < ih */
			{ m[6], bw - 1e-12 },                                    /* w > 0 */
		};
		double lo = x_0 + 0.5, hi = x_1 - 0.5;
		for (int i = 0; i < 5 && lo <= hi; i++) {
			double A = lin[i][0], B = lin[i][1];
			if (fabs(A) < 1e-15) {
				if (B <= 0.0) {
					hi = lo - 1.0;
				}
			} else if (A > 0.0) {
				lo = fmax(lo, -B / A);
			} else {
				hi = fmin(hi, -B / A);
			}
		}
		if (lo > hi) {
			continue;
		}
		/* a pixel to spare either side: the tests in the loop are exact */
		int xa = (int)floor(lo - 0.5) - 1, xb = (int)ceil(hi - 0.5) + 2;
		xa = xa < x_0 ? x_0 : xa;
		xb = xb > x_1 ? x_1 : xb;
		if (exact) {
			warp_run_exact(m, bu, bv, bw, xa, xb, sp, ss, iw, ih, ka, row);
			continue;
		}
		/* Where each pixel looks back to is exact only every WARP_SPAN
		 * pixels, and stepped in a straight line between, in 16.16: the
		 * divide and the two floors a pixel were most of what a turned
		 * book cost (a leaf going over was 45 ms a frame), and a homography
		 * as gentle as a thing held in the hand bends less over sixteen
		 * pixels than a ten thousandth of one. */
		for (int xs = xa; xs < xb; xs += WARP_SPAN) {
			int xe = xs + WARP_SPAN < xb ? xs + WARP_SPAN : xb;
			double X0 = xs + 0.5, X1 = xe + 0.5;
			double w0 = m[6] * X0 + bw, w1 = m[6] * X1 + bw;
			if (w0 <= 1e-12 || w1 <= 1e-12) {
				/* the eye's own plane crosses the span: the slow way */
				warp_run_exact(m, bu, bv, bw, xs, xe, sp, ss, iw, ih, ka, row);
				continue;
			}
			double u0 = (m[0] * X0 + bu) / w0 - 0.5, v0 = (m[3] * X0 + bv) / w0 - 0.5;
			double u1 = (m[0] * X1 + bu) / w1 - 0.5, v1 = (m[3] * X1 + bv) / w1 - 0.5;
			if (fabs(u0) > 30000.0 || fabs(v0) > 30000.0 || fabs(u1) > 30000.0
				|| fabs(v1) > 30000.0) {
				warp_run_exact(m, bu, bv, bw, xs, xe, sp, ss, iw, ih, ka, row);
				continue;
			}
			int n = xe - xs;
			int32_t U = (int32_t)lrint(u0 * 65536.0), V = (int32_t)lrint(v0 * 65536.0);
			int32_t dU = (int32_t)lrint((u1 - u0) * 65536.0 / n);
			int32_t dV = (int32_t)lrint((v1 - v0) * 65536.0 / n);
			for (int x = xs; x < xe; x++, U += dU, V += dV) {
				warp_pixel(U, V, sp, ss, iw, ih, ka, &row[x]);
			}
		}
	}
	cairo_surface_mark_dirty_rectangle(dst, x_0, y_0, x_1 - x_0, y_1 - y_0);
}

/* from the face out towards the lamps: up, a little right, towards you */
#define HELD_LIGHT_X 0.18
#define HELD_LIGHT_Y (-0.82)
#define HELD_LIGHT_Z (-0.54)

static void held_poly(cairo_t *cr, const struct turn3 *t, const double (*p)[3], int n)
{
	for (int i = 0; i < n; i++) {
		double sx, sy;
		turn3_project(t, p[i][0], p[i][1], p[i][2], &sx, &sy, NULL);
		if (i == 0) {
			cairo_move_to(cr, sx, sy);
		} else {
			cairo_line_to(cr, sx, sy);
		}
	}
	cairo_close_path(cr);
}

/* a face of the leather, lit as it turns: base colour times the light */
static void held_leather_face(cairo_t *cr, const struct turn3 *t, double nx, double ny,
	double nz, double base)
{
	double lit = turn3_lit(t, nx, ny, nz, HELD_LIGHT_X, HELD_LIGHT_Y, HELD_LIGHT_Z);
	double k = base * (0.30 + 0.70 * lit);
	cairo_set_source_rgb(cr, 0.36 * k, 0.22 * k, 0.13 * k);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
	cairo_set_line_width(cr, 0.6);
	cairo_stroke(cr);
}

/* The edges of a board x, y, w by h and tb thick behind its face, where
 * they are turned to the eye: leather, lit as they face. */
static void held_board_edges(cairo_t *cr, const struct turn3 *t, double x, double y, double w,
	double h, double tb)
{
	const double ex[4][2][2] = {
		{ { x, y }, { x, y + h } }, { { x + w, y }, { x + w, y + h } },
		{ { x, y }, { x + w, y } }, { { x, y + h }, { x + w, y + h } },
	};
	const double en[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
	for (int e = 0; e < 4; e++) {
		double mx = (ex[e][0][0] + ex[e][1][0]) * 0.5, my = (ex[e][0][1] + ex[e][1][1]) * 0.5;
		if (!turn3_facing(t, mx, my, tb * 0.5, en[e][0], en[e][1], 0.0)) {
			continue;
		}
		double q[4][3] = { { ex[e][0][0], ex[e][0][1], 0.0 }, { ex[e][1][0], ex[e][1][1], 0.0 },
			{ ex[e][1][0], ex[e][1][1], tb }, { ex[e][0][0], ex[e][0][1], tb } };
		held_poly(cr, t, q, 4);
		held_leather_face(cr, t, en[e][0], en[e][1], 0.0, 1.20);
	}
}

/* A face held up, x, y, w by h of its flat drawing, in the lamps' light:
 * darker or lighter as it is turned from square to them, and its foot a
 * little darker than its head. */
static void held_face_light(cairo_t *cr, const struct turn3 *t, double x, double y, double w,
	double h, double foot)
{
	double lit = turn3_lit(t, 0.0, 0.0, -1.0, HELD_LIGHT_X, HELD_LIGHT_Y, HELD_LIGHT_Z);
	double lit0 = fmax(-HELD_LIGHT_Z / sqrt(HELD_LIGHT_X * HELD_LIGHT_X
		+ HELD_LIGHT_Y * HELD_LIGHT_Y + HELD_LIGHT_Z * HELD_LIGHT_Z), 1e-3);
	double dark = clampf(1.0 - lit / lit0, -0.6, 0.6);
	double q[4][2];
	turn3_point(t, x, y, &q[0][0], &q[0][1], NULL);
	turn3_point(t, x + w, y, &q[1][0], &q[1][1], NULL);
	turn3_point(t, x + w, y + h, &q[2][0], &q[2][1], NULL);
	turn3_point(t, x, y + h, &q[3][0], &q[3][1], NULL);
	cairo_save(cr);
	cairo_move_to(cr, q[0][0], q[0][1]);
	for (int i = 1; i < 4; i++) {
		cairo_line_to(cr, q[i][0], q[i][1]);
	}
	cairo_close_path(cr);
	cairo_pattern_t *gr = cairo_pattern_create_linear((q[0][0] + q[1][0]) * 0.5,
		(q[0][1] + q[1][1]) * 0.5, (q[2][0] + q[3][0]) * 0.5, (q[2][1] + q[3][1]) * 0.5);
	double a0 = fmax(0.0, dark) * 0.35, a1 = fmax(0.0, dark) * 0.35 + foot;
	cairo_pattern_add_color_stop_rgba(gr, 0.0, 0.0, 0.0, 0.0, a0);
	cairo_pattern_add_color_stop_rgba(gr, 1.0, 0.0, 0.0, 0.0, a1);
	cairo_set_source(cr, gr);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(gr);
	if (dark < 0.0) {
		cairo_set_source_rgba(cr, 1.0, 0.97, 0.90, -dark * 0.20);
		cairo_fill(cr);
	}
	cairo_new_path(cr);
	cairo_restore(cr);
}

/* ---- taken off the shelf and opened ----
 *
 * What is taken down off the bookcase -- the book of every program at the
 * end of the row, or one of the binders -- comes off the shelf as it stood
 * there, spine first, and is brought round in front of you: it turns on
 * its own upright until its front board faces you, tipping back as it is
 * held open, and the board swings over on the spine with the inside of it
 * coming round, the paper under it settling flat as it opens and slides
 * across to lie as it is held. Put back, the same backwards.
 *
 * It is a solid: the back board, the block of paper on it, the round of
 * the spine with its label or its tooling, and the front board hinged at
 * the spine -- each face drawn where it is turned to the eye, the nearest
 * part last. Its pictures are the ones it is held open with, cut at the
 * hinge and printed on the same pixels: the left of it rides on the inside
 * of the front board, the right's boards on the back board and what lies
 * on them on the block. So its last frame is the thing as it is held, and
 * nothing jumps when it is drawn the held way instead. */

/* where u (0 on the shelf, 1 open) is in each part of it */
#define OP_TAKE0 0.00
#define OP_TAKE1 0.36
#define OP_TURN0 0.20
#define OP_TURN1 0.54
#define OP_OPEN0 0.52
#define OP_OPEN1 1.00
#define OP_SPINE_N 4

static double op_ease(double a, double b, double u)
{
	double x = clampf((u - a) / (b - a), 0.0, 1.0);
	return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

/* Its own frame: x out from the hinge to the fore edge, y down from the
 * middle of its height, z behind the paper as it lies open. */
struct book_pose {
	struct turn3 t;
	double s;                 /* its size against held open */
	double pvx, pvy, pvz;     /* the point of it that is at t.px, t.py */
	double WL, WR, H, T, b;   /* the front and back boards' widths, the height;
	                           * the block's thickness; a board's */
	double Wp, yt, yb;        /* the block: out to Wp, from yt down to yb */
	double ts, rb;            /* the spine: across, and how far it rounds out */
	double phi;               /* the front board: 0 shut, pi lying open on the left */
	double o;                 /* how far open, 0 to 1 */
};

/* a picture of part of the flat drawing it is held with, and what part */
struct book_pic {
	cairo_surface_t *s;
	double x0, y0, pw, ph;
};

/* What is taken down and opened: how it lies held open, how it stood on
 * the shelf, and the pictures it is drawn from. */
struct op_kind {
	double bx, by, bw, bh;    /* its boards held open, in the flat drawing */
	double gx;                /* the hinge between them */
	struct turn3 held;        /* how it is held open */
	double b;                 /* a board's thickness */
	double fore_in;           /* the block of paper, in from the boards' fore edge */
	double yt, yb;            /* and its head and foot, from the middle of the height */
	double ts, rb, W0;        /* on the shelf: the spine across, its round, a board's depth */
	double scx, scy, ssh;     /* where its spine was on the screen, and how high */
	double s1;                /* held shut, its size against open */
	double leather[3];        /* the boards' edges and the spine, in full light */
	double edge[3];           /* the block's edges: gilt, or the paper */
	bool lit_faces;           /* the lamps laid over its faces (else they are in its pictures) */
	/* what lies on the block as it is so far opened, if that changes (a
	 * binder's sheets swing down off the stack): else pages */
	const struct book_pic *(*pages_at)(const struct op_kind *K, const struct book_pose *P);
	int shelf;
	double first;
	/* Its pictures: the left of it held open (on the inside of the front
	 * board), the right's boards (the back board's inside) and what lies
	 * on them (on the block); its shadow, if it is a picture; the front
	 * board outside, and the spine in strips round its round. */
	double k;
	const struct book_pic *left, *board, *pages, *shadow;
	cairo_surface_t *cover;
	cairo_surface_t *const *spine;
	/* its shadow drawn straight, if it is not a picture */
	void (*shadow_fn)(cairo_t *cr, const struct op_kind *K, const struct book_pose *P,
		double fade);
};

/* The middle and height on the screen of the front of a spine on the
 * shelf -- at x, hw either side of it, ht high, its face at z -- pulled
 * out of the row as the pointer had it. */
static void op_shelf_spine(int w, int h, double x, double hw, double ht, double z,
	double *cx, double *cy, double *sh)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (shelf_xform(w, h, &rr, &ox, &oy, &scale)) {
		const vec3 p[4] = {
			v3(x - hw, RM_BN_FOOT + ht, z), v3(x + hw, RM_BN_FOOT + ht, z),
			v3(x + hw, RM_BN_FOOT, z), v3(x - hw, RM_BN_FOOT, z),
		};
		double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
		bool ok = true;
		for (int k = 0; k < 4 && ok; k++) {
			double u, v;
			ok = room_project(&rr->shelf_cam, p[k], &u, &v);
			x0 = fmin(x0, u * rr->w);
			x1 = fmax(x1, u * rr->w);
			y0 = fmin(y0, v * rr->h);
			y1 = fmax(y1, v * rr->h);
		}
		if (ok && y1 > y0) {
			/* as desk_shelf_hover_box has it out of the row */
			double bh = y1 - y0;
			*cx = ox + (x0 + x1) * 0.5 * scale;
			*cy = oy + ((y0 + y1) * 0.5 + bh * 0.085) * scale;
			*sh = bh * 1.075 * scale;
			return;
		}
	}
	*cx = w * 0.5;
	*cy = h * 0.30;
	*sh = h * 0.28;
}

static void op_pose_at(const struct op_kind *K, double u, struct book_pose *P)
{
	double a = op_ease(OP_TAKE0, OP_TAKE1, u);
	double r = op_ease(OP_TURN0, OP_TURN1, u);
	double o = op_ease(OP_OPEN0, OP_OPEN1, u);
	P->H = K->bh;
	P->b = K->b;
	P->ts = K->ts;
	P->rb = K->rb;
	/* Shut, both boards as deep as on the shelf, then as wide as the wider
	 * of them held open; opening, each to its own. */
	double WLo = K->gx - K->bx, WRo = K->bx + K->bw - K->gx, W1 = fmax(WLo, WRo);
	double Wc = K->W0 + (W1 - K->W0) * r;
	P->WL = Wc + (WLo - W1) * o;
	P->WR = Wc + (WRo - W1) * o;
	P->Wp = P->WR - K->fore_in;
	P->yt = K->yt;
	P->yb = K->yb;
	/* the paper settles flat once the board is over */
	P->T = (K->ts - 2.0 * K->b) * (1.0 - op_ease(0.40, 1.0, o));
	P->phi = M_PI * o;
	P->o = o;
	/* turned about its own middle, shut; about the point it is held by, open */
	double cy = K->by + K->bh * 0.5;
	P->pvx = Wc * 0.5 * (1.0 - o) + (K->held.px - K->gx) * o;
	P->pvy = (K->held.py - cy) * o;
	P->pvz = -P->T * 0.5 * (1.0 - o);
	/* Where it starts: so big, and so placed, that its spine is where it
	 * was on the shelf -- the spine is nearer you than the middle it turns
	 * about by d, and so that much bigger. The eye, as far off as when it
	 * is held -- but further while the board stands up off it: up on its
	 * hinge it comes half its width towards you, and from that near it
	 * would fill the screen. */
	const struct turn3 *held = &K->held;
	double f = held->f * (1.0 + 1.2 * (1.0 - o)), f0 = held->f * 2.2;
	double d = K->rb + K->W0 * 0.5;
	double s0 = K->ssh * f0 / (P->H * f0 + K->ssh * d);
	double k0 = f0 / fmax(f0 - s0 * d, 1.0);
	double p0x = held->ex + (K->scx - held->ex) / k0, p0y = held->ey + (K->scy - held->ey) / k0;
	P->s = s0 + (K->s1 - s0) * a + (1.0 - K->s1) * o;
	P->t = *held;
	P->t.f = f;
	P->t.px = p0x + (held->px - p0x) * a;
	P->t.py = p0y + (held->py - p0y) * a;
	P->t.yaw = held->yaw + M_PI * 0.5 * (1.0 - r);
	P->t.pitch = held->pitch * r;
}

/* a point of it into the turn's frame, and onto the screen */
static void bp_frame(const struct book_pose *P, double x, double y, double z, double out[3])
{
	out[0] = P->t.px + P->s * (x - P->pvx);
	out[1] = P->t.py + P->s * (y - P->pvy);
	out[2] = P->s * (z - P->pvz);
}

static void bp_screen(const struct book_pose *P, const double p[3], double *sx, double *sy)
{
	double q[3];
	bp_frame(P, p[0], p[1], p[2], q);
	turn3_project(&P->t, q[0], q[1], q[2], sx, sy, NULL);
}

static bool bp_facing(const struct book_pose *P, const double (*q)[3], const double n[3])
{
	double c[3] = { 0, 0, 0 };
	for (int i = 0; i < 4; i++) {
		for (int k = 0; k < 3; k++) {
			c[k] += q[i][k] * 0.25;
		}
	}
	double f[3];
	bp_frame(P, c[0], c[1], c[2], f);
	return turn3_facing(&P->t, f[0], f[1], f[2], n[0], n[1], n[2]);
}

/* how far a point of it is from the eye */
static double bp_dist(const struct book_pose *P, double x, double y, double z)
{
	double f[3], X, Y, Z;
	bp_frame(P, x, y, z, f);
	turn3_rot(&P->t, f[0] - P->t.px, f[1] - P->t.py, f[2], &X, &Y, &Z);
	double dx = P->t.px + X - P->t.ex, dy = P->t.py + Y - P->t.ey, dz = Z + P->t.f;
	return dx * dx + dy * dy + dz * dz;
}

static void bp_quad(cairo_t *cr, const struct book_pose *P, const double (*q)[3])
{
	cairo_new_path(cr);
	for (int i = 0; i < 4; i++) {
		double sx, sy;
		bp_screen(P, q[i], &sx, &sy);
		if (i == 0) {
			cairo_move_to(cr, sx, sy);
		} else {
			cairo_line_to(cr, sx, sy);
		}
	}
	cairo_close_path(cr);
}

static double bp_lit(const struct book_pose *P, const double n[3])
{
	return turn3_lit(&P->t, n[0], n[1], n[2], HELD_LIGHT_X, HELD_LIGHT_Y, HELD_LIGHT_Z);
}

/* a face of the boards, in the light it is turned to */
static void bp_leather(cairo_t *cr, const struct book_pose *P, const double (*q)[3],
	const double n[3], double base, const double col[3])
{
	double k = base * (0.30 + 0.70 * bp_lit(P, n));
	bp_quad(cr, P, q);
	cairo_set_source_rgb(cr, fmin(col[0] * k, 1.0), fmin(col[1] * k, 1.0),
		fmin(col[2] * k, 1.0));
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
	cairo_set_line_width(cr, 0.6);
	cairo_stroke(cr);
}

/* An edge of the block, gilt or the paper: in the light, and the leaves
 * in it as lines along it. q runs along the edge from q[0] to q[1], and
 * across the leaves from q[1] to q[2]. */
static void bp_block_edge(cairo_t *cr, const struct book_pose *P, const double (*q)[3],
	const double n[3], const double col[3])
{
	double k = 0.40 + 0.85 * bp_lit(P, n);
	bp_quad(cr, P, q);
	cairo_set_source_rgb(cr, fmin(col[0] * k, 1.0), fmin(col[1] * k, 1.0),
		fmin(col[2] * k, 1.0));
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.30, 0.18, 0.04, 0.22);
	cairo_set_line_width(cr, 0.5);
	for (int i = 1; i < 7; i++) {
		double t = i / 7.0, a[3], b[3];
		for (int c = 0; c < 3; c++) {
			a[c] = q[0][c] + (q[3][c] - q[0][c]) * t;
			b[c] = q[1][c] + (q[2][c] - q[1][c]) * t;
		}
		double ax, ay, bx, by;
		bp_screen(P, a, &ax, &ay);
		bp_screen(P, b, &bx, &by);
		cairo_move_to(cr, ax, ay);
		cairo_line_to(cr, bx, by);
	}
	cairo_stroke(cr);
}

/* picture img (k pixels to the unit, pw by ph of them) laid on the face q:
 * its top left corner on q[0], top right on q[1], and round */
static void bp_warp(cairo_t *cr, const struct book_pose *P, cairo_surface_t *img, double k,
	double pw, double ph, const double (*q)[3], double alpha)
{
	if (!img) {
		return;
	}
	double sq[4][2];
	for (int i = 0; i < 4; i++) {
		bp_screen(P, q[i], &sq[i][0], &sq[i][1]);
	}
	struct hmat map;
	if (hmat_rect_to_quad(0.0, 0.0, pw, ph, (const double (*)[2])sq, &map)) {
		warp_paint(cr, img, 0.0, 0.0, k, &map, alpha);
	}
}

/* A picture of the flat drawing laid on the plane z, as the right of it
 * lies (x out from the hinge) or, flap, on the inside of the front board
 * at angle phi (u out from the hinge). Widths go as the board's against
 * held open, while it is still changing shape. */
static void bp_warp_flat(cairo_t *cr, const struct book_pose *P, const struct book_pic *pic,
	double k, double gx, double cy, double xs, double z, bool flap, double alpha)
{
	if (!pic || !pic->s) {
		return;
	}
	double fx[4] = { pic->x0, pic->x0 + pic->pw, pic->x0 + pic->pw, pic->x0 };
	double fy[4] = { pic->y0, pic->y0, pic->y0 + pic->ph, pic->y0 + pic->ph };
	double q[4][3];
	double c = cos(P->phi), s = sin(P->phi);
	for (int i = 0; i < 4; i++) {
		if (flap) {
			double u = (gx - fx[i]) * xs;
			q[i][0] = u * c;
			q[i][1] = fy[i] - cy;
			q[i][2] = -P->T - u * s;
		} else {
			q[i][0] = (fx[i] - gx) * xs;
			q[i][1] = fy[i] - cy;
			q[i][2] = z;
		}
	}
	bp_warp(cr, P, pic->s, k, pic->pw, pic->ph, (const double (*)[3])q, alpha);
}

/* the lamps on a face, as held_face_light has them on a thing held */
static void bp_face_light(cairo_t *cr, const struct book_pose *P, const double (*q)[3],
	const double n[3], double foot)
{
	double lit = bp_lit(P, n);
	double lit0 = fmax(-HELD_LIGHT_Z / sqrt(HELD_LIGHT_X * HELD_LIGHT_X
		+ HELD_LIGHT_Y * HELD_LIGHT_Y + HELD_LIGHT_Z * HELD_LIGHT_Z), 1e-3);
	double dark = clampf(1.0 - lit / lit0, -0.6, 0.6);
	double s[4][2];
	for (int i = 0; i < 4; i++) {
		bp_screen(P, q[i], &s[i][0], &s[i][1]);
	}
	cairo_save(cr);
	cairo_new_path(cr);
	cairo_move_to(cr, s[0][0], s[0][1]);
	for (int i = 1; i < 4; i++) {
		cairo_line_to(cr, s[i][0], s[i][1]);
	}
	cairo_close_path(cr);
	cairo_pattern_t *gr = cairo_pattern_create_linear((s[0][0] + s[1][0]) * 0.5,
		(s[0][1] + s[1][1]) * 0.5, (s[2][0] + s[3][0]) * 0.5, (s[2][1] + s[3][1]) * 0.5);
	double a0 = fmax(0.0, dark) * 0.35, a1 = fmax(0.0, dark) * 0.35 + foot;
	cairo_pattern_add_color_stop_rgba(gr, 0.0, 0.0, 0.0, 0.0, a0);
	cairo_pattern_add_color_stop_rgba(gr, 1.0, 0.0, 0.0, 0.0, a1);
	cairo_set_source(cr, gr);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(gr);
	if (dark < 0.0) {
		cairo_set_source_rgba(cr, 1.0, 0.97, 0.90, -dark * 0.20);
		cairo_fill(cr);
	}
	cairo_new_path(cr);
	cairo_restore(cr);
}

/* Part of a flat drawing -- x0, y0, pw by ph of it, at k pixels a unit --
 * printed on the pixels the thing held is drawn on, whose picture starts
 * at grid_x, grid_y: so that laid down where it is held it comes out the
 * same to the pixel. */
static struct book_pic op_pic(double k, double grid_x, double grid_y, double x0, double y0,
	double pw, double ph, void (*draw)(cairo_t *cr, void *arg), void *arg)
{
	struct book_pic p = { NULL, 0, 0, 0, 0 };
	p.x0 = grid_x + floor((x0 - grid_x) * k) / k;
	p.y0 = grid_y + floor((y0 - grid_y) * k) / k;
	int iw = (int)ceil((x0 + pw - p.x0) * k), ih = (int)ceil((y0 + ph - p.y0) * k);
	if (iw < 1 || ih < 1) {
		return p;
	}
	p.pw = iw / k;
	p.ph = ih / k;
	p.s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
	cairo_t *c = cairo_create(p.s);
	cairo_scale(c, k, k);
	cairo_translate(c, -p.x0, -p.y0);
	cairo_rectangle(c, x0, y0, pw, ph);
	cairo_clip(c);
	draw(c, arg);
	cairo_destroy(c);
	cairo_surface_flush(p.s);
	return p;
}

static void op_pic_drop(struct book_pic *p)
{
	if (p->s) {
		cairo_surface_destroy(p->s);
		p->s = NULL;
	}
}

/* The back board and the block of paper on it. */
static void op_right(cairo_t *cr, const struct op_kind *K, const struct book_pose *P)
{
	double WRo = K->bx + K->bw - K->gx;
	double xs = P->WR / WRo, cy = K->by + K->bh * 0.5;
	double W = P->WR, b = P->b, T = P->T, y0 = -P->H * 0.5, y1 = P->H * 0.5;
	/* the board: its back, its edges, its inside */
	{
		const double q[4][3] = { { 0, y0, b }, { 0, y1, b }, { W, y1, b }, { W, y0, b } };
		const double n[3] = { 0, 0, 1 };
		if (bp_facing(P, q, n)) {
			bp_leather(cr, P, q, n, 0.95, K->leather);
		}
	}
	{
		const double q[4][3] = { { W, y0, 0 }, { W, y1, 0 }, { W, y1, b }, { W, y0, b } };
		const double n[3] = { 1, 0, 0 };
		if (bp_facing(P, q, n)) {
			bp_leather(cr, P, q, n, 1.15, K->leather);
		}
	}
	for (int e = 0; e < 2; e++) {
		double y = e ? y1 : y0;
		const double q[4][3] = { { 0, y, 0 }, { W, y, 0 }, { W, y, b }, { 0, y, b } };
		const double n[3] = { 0, e ? 1.0 : -1.0, 0 };
		if (bp_facing(P, q, n)) {
			bp_leather(cr, P, q, n, 1.15, K->leather);
		}
	}
	const double qi[4][3] = { { 0, y0, 0 }, { W, y0, 0 }, { W, y1, 0 }, { 0, y1, 0 } };
	const double ni[3] = { 0, 0, -1 };
	bool inside = bp_facing(P, qi, ni);
	if (inside) {
		bp_warp_flat(cr, P, K->board, K->k, K->gx, cy, xs, 0.0, false, 1.0);
	}
	/* the block: its edges, and what lies on top */
	if (T > 0.5) {
		double xp = P->Wp, yt = P->yt, yb = P->yb;
		const double qf[4][3] = { { xp, yt, -T }, { xp, yb, -T }, { xp, yb, 0 }, { xp, yt, 0 } };
		const double nf[3] = { 1, 0, 0 };
		if (bp_facing(P, qf, nf)) {
			bp_block_edge(cr, P, qf, nf, K->edge);
		}
		for (int e = 0; e < 2; e++) {
			double y = e ? yb : yt;
			const double q[4][3] = { { 0, y, -T }, { xp, y, -T }, { xp, y, 0 }, { 0, y, 0 } };
			const double n[3] = { 0, e ? 1.0 : -1.0, 0 };
			if (bp_facing(P, q, n)) {
				bp_block_edge(cr, P, q, n, K->edge);
			}
		}
	}
	if (P->o > 0.0005) {
		const double q[4][3] = { { 0, y0, -T }, { W, y0, -T }, { W, y1, -T }, { 0, y1, -T } };
		const double n[3] = { 0, 0, -1 };
		if (bp_facing(P, q, n)) {
			const struct book_pic *pg = K->pages_at ? K->pages_at(K, P) : K->pages;
			bp_warp_flat(cr, P, pg, K->k, K->gx, cy, xs, -T, false, 1.0);
			/* in the shade of the board going over it */
			double s = sin(P->phi);
			if (P->phi < M_PI * 0.5 && s > 0.01) {
				double x1 = fmin(W, W * cos(P->phi) + W * 0.20 * s);
				const double qs[4][3] = { { 0, y0, -T }, { x1, y0, -T }, { x1, y1, -T },
					{ 0, y1, -T } };
				bp_quad(cr, P, qs);
				cairo_set_source_rgba(cr, 0.05, 0.03, 0.02, 0.22 * s);
				cairo_fill(cr);
			}
		}
	}
	if (inside && K->lit_faces) {
		bp_face_light(cr, P, qi, ni, 0.06);
	}
}

/* The round of the spine, what is on it on it. */
static void op_spine(cairo_t *cr, const struct op_kind *K, const struct book_pose *P)
{
	double y0 = -P->H * 0.5, y1 = P->H * 0.5;
	double R = P->T * 0.5 + P->b, zc = -P->T * 0.5, rb = P->rb;
	double sw = P->ts / OP_SPINE_N;
	for (int i = 0; i < OP_SPINE_N; i++) {
		double ta = M_PI * (1.0 - (double)i / OP_SPINE_N);
		double tb = M_PI * (1.0 - (double)(i + 1) / OP_SPINE_N);
		double xa = -rb * sin(ta), za = zc - R * cos(ta);
		double xb = -rb * sin(tb), zb = zc - R * cos(tb);
		const double q[4][3] = { { xa, y0, za }, { xb, y0, zb }, { xb, y1, zb }, { xa, y1, za } };
		double tm = (ta + tb) * 0.5;
		double n[3] = { -sin(tm) / fmax(rb, 1e-3), 0.0, -cos(tm) / fmax(R, 1e-3) };
		double nl = sqrt(n[0] * n[0] + n[2] * n[2]);
		n[0] /= nl;
		n[2] /= nl;
		if (!bp_facing(P, q, n)) {
			continue;
		}
		bp_leather(cr, P, q, n, 1.05, K->leather);
		if (K->spine) {
			bp_warp(cr, P, K->spine[i], K->k, sw, P->H, q, 1.0);
		}
	}
}

/* The front board on its hinge: shut on the block, over, or open. */
static void op_flap(cairo_t *cr, const struct op_kind *K, const struct book_pose *P)
{
	double WLo = K->gx - K->bx, WRo = K->bx + K->bw - K->gx;
	double W = P->WL, b = P->b, y0 = -P->H * 0.5, y1 = P->H * 0.5;
	double xs = P->WL / WLo;
	double c = cos(P->phi), s = sin(P->phi);
	double no[3] = { -s, 0.0, -c }, ni[3] = { s, 0.0, c }, nd[3] = { c, 0.0, -s };
	/* a point u out from the hinge, y down, on its inside or outside */
	#define FLAP(u, y, out, o) do { \
		(o)[0] = (u) * c + ((out) ? no[0] * b : 0.0); \
		(o)[1] = (y); \
		(o)[2] = -P->T - (u) * s + ((out) ? no[2] * b : 0.0); \
	} while (0)
	double q[4][3];
	/* its edges */
	FLAP(W, y0, 0, q[0]);
	FLAP(W, y1, 0, q[1]);
	FLAP(W, y1, 1, q[2]);
	FLAP(W, y0, 1, q[3]);
	if (bp_facing(P, (const double (*)[3])q, nd)) {
		bp_leather(cr, P, (const double (*)[3])q, nd, 1.15, K->leather);
	}
	for (int e = 0; e < 2; e++) {
		double y = e ? y1 : y0;
		const double n[3] = { 0, e ? 1.0 : -1.0, 0 };
		FLAP(0, y, 0, q[0]);
		FLAP(W, y, 0, q[1]);
		FLAP(W, y, 1, q[2]);
		FLAP(0, y, 1, q[3]);
		if (bp_facing(P, (const double (*)[3])q, n)) {
			bp_leather(cr, P, (const double (*)[3])q, n, 1.15, K->leather);
		}
	}
	/* its outside; or its inside, with the left of it held open on it */
	FLAP(0, y0, 1, q[0]);
	FLAP(W, y0, 1, q[1]);
	FLAP(W, y1, 1, q[2]);
	FLAP(0, y1, 1, q[3]);
	if (bp_facing(P, (const double (*)[3])q, no)) {
		bp_warp(cr, P, K->cover, K->k, fmax(WLo, WRo), P->H, (const double (*)[3])q, 1.0);
		bp_face_light(cr, P, (const double (*)[3])q, no, 0.06);
	}
	FLAP(W, y0, 0, q[0]);
	FLAP(0, y0, 0, q[1]);
	FLAP(0, y1, 0, q[2]);
	FLAP(W, y1, 0, q[3]);
	if (bp_facing(P, (const double (*)[3])q, ni)) {
		bp_warp_flat(cr, P, K->left, K->k, K->gx, K->by + K->bh * 0.5, xs, 0.0, true, 1.0);
		if (K->lit_faces) {
			bp_face_light(cr, P, (const double (*)[3])q, ni, 0.06);
		}
	}
	#undef FLAP
}

/* It all, u of the way from the shelf to held open. */
static void op_draw(cairo_t *cr, const struct op_kind *K, double u)
{
	struct book_pose P;
	op_pose_at(K, u, &P);
	/* its shadow, coming as it comes down open */
	if (P.o > 0.55) {
		double fade = op_ease(0.55, 1.0, P.o);
		if (K->shadow && K->shadow->s) {
			bp_warp_flat(cr, &P, K->shadow, K->k, K->gx, K->by + K->bh * 0.5,
				P.WR / (K->bx + K->bw - K->gx), 0.0, false, fade);
		} else if (K->shadow_fn) {
			K->shadow_fn(cr, K, &P, fade);
		}
	}
	/* the three parts of it, furthest first */
	double fc[3];
	{
		double um = P.WL * 0.5, c = cos(P.phi), s = sin(P.phi);
		fc[0] = um * c - s * P.b * 0.5;
		fc[1] = 0.0;
		fc[2] = -P.T - um * s - c * P.b * 0.5;
	}
	double d[3] = {
		bp_dist(&P, P.WR * 0.5, 0.0, -P.T * 0.5 + P.b * 0.5),
		bp_dist(&P, -P.rb * 0.6, 0.0, -P.T * 0.5),
		bp_dist(&P, fc[0], fc[1], fc[2]),
	};
	int order[3] = { 0, 1, 2 };
	for (int i = 0; i < 3; i++) {
		for (int j = i + 1; j < 3; j++) {
			if (d[order[j]] > d[order[i]]) {
				int t = order[i];
				order[i] = order[j];
				order[j] = t;
			}
		}
	}
	if (P.phi > M_PI * 0.5) {
		/* over and going down open, the front board is under whatever
		 * lies across from the right -- a binder's sheets fan over it */
		order[0] = 2;
		order[1] = 1;
		order[2] = 0;
	}
	for (int i = 0; i < 3; i++) {
		switch (order[i]) {
		case 0:
			op_right(cr, K, &P);
			break;
		case 1:
			/* once the board is over, the spine is under it */
			if (P.phi < M_PI * 0.5) {
				op_spine(cr, K, &P);
			}
			break;
		default:
			op_flap(cr, K, &P);
			break;
		}
	}
	/* up on the shelf it is in the room's dimmer light, and comes into
	 * the lamps' as it comes to you: over what is drawn of it and nothing
	 * else (what it is drawn on is clear but for it) */
	double dim = 0.32 * (1.0 - op_ease(OP_TAKE0, OP_TAKE1, u));
	if (dim > 0.004) {
		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);
		cairo_set_source_rgba(cr, 0.02, 0.02, 0.04, dim);
		cairo_paint(cr);
		cairo_restore(cr);
	}
}

/* The boards' edges, round it held open. */
static void op_board_edges(cairo_t *cr, const struct op_kind *K)
{
	struct book_pose P;
	op_pose_at(K, 1.0, &P);
	double L = P.WL, R = P.WR, b = P.b, y0 = -P.H * 0.5, y1 = P.H * 0.5;
	const double qe[4][4][3] = {
		{ { -L, y0, 0 }, { -L, y1, 0 }, { -L, y1, b }, { -L, y0, b } },
		{ { R, y0, 0 }, { R, y1, 0 }, { R, y1, b }, { R, y0, b } },
		{ { -L, y0, 0 }, { R, y0, 0 }, { R, y0, b }, { -L, y0, b } },
		{ { -L, y1, 0 }, { R, y1, 0 }, { R, y1, b }, { -L, y1, b } },
	};
	const double ne[4][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 } };
	for (int e = 0; e < 4; e++) {
		if (bp_facing(&P, qe[e], ne[e])) {
			bp_leather(cr, &P, qe[e], ne[e], 1.15, K->leather);
		}
	}
}
