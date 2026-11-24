/* Part of draw.c: the plate to the shelf, the desk mat and the papers on it.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- the plate that sends you to the shelf --------------------- */

/* A small engraved plate screwed to the back of the desk, in the corner to
 * the right of the post box: the third of the plates along the tray, lying
 * flat like the other two -- CONTROL PANEL, POST, ALL APPS -- and like them
 * pointing up, which is the way each of them sends you. It used to stand
 * on its side down the right edge, twice as far down the screen as
 * anything else on the tray, where every window was in its way. */
void desk_allapps_box(int w, int h, double *x, double *y, double *pw, double *ph)
{
	/* In points, as the post box beside it is: a plate sized off the
	 * screen was a postage stamp next to the box on one screen and a
	 * signboard on the next. The screw spacing and the lettering follow
	 * the plate. */
	(void)h;
	double t = fmax(32.4 * desk_px_per_pt(w), 20.0);
	double len = t * 4.20;
	double margin = t * 0.40;
	/* between the post box and the edge of the screen, and smaller rather
	 * than on top of the box where the corner is tight */
	struct tray_extent te;
	double left = tray_measure(w, h, &te) ? te.post_x1 + margin : w * 0.905;
	double room = w - margin - left;
	if (len > room) {
		double k = fmax(room, t * 2.6) / len;
		t *= k;
		len *= k;
		margin = t * 0.40;
	}
	*pw = len;
	*ph = t;
	*x = w - margin - len;
	/* level with the POST plate on the box's cap: where the middle of it
	 * really is, once the room is traced */
	double mid = h * 0.034;
	struct room_xf xf;
	if (room_xform(w, h, 0.0, &xf)) {
		double u, v;
		vec3 c = pb_cap_to_world(0.0, RM_PB_CAP_T * 0.5,
			(RM_PB_HZ - 0.10 + RM_PB_SLOT_S + RM_PB_SLOT_HS + 0.30) * 0.5);
		if (room_project(&xf.rr->cam, c, &u, &v)) {
			mid = xf.oy + v * xf.rr->h * xf.scale;
		}
	}
	*y = mid - t * 0.5;
	if (*y < margin * 0.5) {
		*y = margin * 0.5;
	}
}

bool desk_allapps_hit(int w, int h, double px, double py)
{
	double x, y, pw, ph;
	desk_allapps_box(w, h, &x, &y, &pw, &ph);
	/* generous up and down, less so sideways: the post box is next door */
	double pad = ph * 0.40;
	double side = ph * 0.20;
	return px >= x - side && px <= x + pw + side
		&& py >= y - pad && py <= y + ph + pad;
}

static void allapps_plate(cairo_t *cr, int w, int h, bool hot)
{
	double x, y, pw, ph;
	desk_allapps_box(w, h, &x, &y, &pw, &ph);

	cairo_save(cr);
	desk_set_paint_origin(0, 0);
	/* it stands a couple of millimetres off the wood */
	contact_shadow(cr, x, y + ph * 0.52, pw, ph * 0.96, 0.30);

	/* drawn as the plate was made: L long and T tall */
	cairo_translate(cr, x, y);
	double L = pw, T = ph;
	double r = T * 0.16;

	cairo_save(cr);
	desk_rounded_path(cr, 0, 0, L, T, r);
	cairo_clip(cr);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, T);
	double k = hot ? 1.13 : 1.0;
	cairo_pattern_add_color_stop_rgb(g, 0.00, 0.694 * k, 0.588 * k, 0.362 * k);
	cairo_pattern_add_color_stop_rgb(g, 0.34, 0.560 * k, 0.454 * k, 0.246 * k);
	cairo_pattern_add_color_stop_rgb(g, 0.52, 0.470 * k, 0.372 * k, 0.190 * k);
	cairo_pattern_add_color_stop_rgb(g, 1.00, 0.620 * k, 0.512 * k, 0.300 * k);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	/* brushed along its length, the way a rolled plate is */
	cairo_set_line_width(cr, 1.0);
	for (double bx = 0; bx < L; bx += 2.0) {
		cairo_set_source_rgba(cr, 1, 0.96, 0.86, hash21(bx * 0.41, 2.7) * 0.085);
		cairo_move_to(cr, bx + 0.5, 0);
		cairo_line_to(cr, bx + 0.5, T);
		cairo_stroke(cr);
	}
	overlay_grain(cr, 0, 0, L, T, 0.9);
	cairo_restore(cr);

	/* the bevel */
	cairo_set_line_width(cr, 1.2);
	cairo_set_source_rgba(cr, 1.0, 0.94, 0.80, 0.42);
	desk_rounded_path(cr, 0.6, 0.6, L - 1.2, T - 1.2, r);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 0.14, 0.09, 0.03, 0.55);
	desk_rounded_path(cr, 0, 0, L, T, r);
	cairo_stroke(cr);

	/* two screws, neither of them driven home straight */
	for (int i = 0; i < 2; i++) {
		double sx = i ? L - T * 0.30 : T * 0.30;
		double sy = T * 0.5;
		double sr = T * 0.113;
		cairo_set_source_rgba(cr, 0.12, 0.08, 0.03, 0.42);
		cairo_arc(cr, sx, sy + sr * 0.22, sr, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_pattern_t *sg = cairo_pattern_create_linear(sx - sr, sy - sr,
			sx + sr, sy + sr);
		cairo_pattern_add_color_stop_rgb(sg, 0.0, 0.80, 0.70, 0.46);
		cairo_pattern_add_color_stop_rgb(sg, 1.0, 0.44, 0.35, 0.18);
		cairo_set_source(cr, sg);
		cairo_arc(cr, sx, sy, sr, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_pattern_destroy(sg);
		double a = (hash21(i * 7.3, 1.9) - 0.5) * 1.6;
		cairo_save(cr);
		cairo_translate(cr, sx, sy);
		cairo_rotate(cr, a);
		cairo_set_line_width(cr, fmax(sr * 0.30, 1.0));
		cairo_set_source_rgba(cr, 0.16, 0.11, 0.04, 0.80);
		cairo_move_to(cr, -sr * 0.74, 0);
		cairo_line_to(cr, sr * 0.74, 0);
		cairo_stroke(cr);
		cairo_restore(cr);
	}

	/* The engraving: cut in, so a dark line with the light catching below.
	 * The words, then the arrow pointing up the way it sends you, as the
	 * other two plates have it. */
	double as = T * 0.24;
	double ax = L - T * 0.60 - as * 1.1;
	double ay = T * 0.50;
	double tx = T * 0.60;
	double ty = T * 0.66;
	double fs = T * 0.40;
	double tw = ax - as * 1.2 - tx;
	show_text_fit_as(cr, DESK_FONT_ENGRAVE, TR("ALL APPS"), tx, ty + 1.0, fs, true, 1.0,
		0.94, 0.78, 0.34, -1, tw);
	show_text_fit_as(cr, DESK_FONT_ENGRAVE, TR("ALL APPS"), tx, ty, fs, true, 0.16, 0.11,
		0.04, 0.88, -1, tw);

	/* and the arrow, drawn rather than typed so it is cut the same way */
	for (int pass = 0; pass < 2; pass++) {
		cairo_save(cr);
		cairo_translate(cr, 0, pass ? 0.0 : 1.0);
		if (pass) {
			cairo_set_source_rgba(cr, 0.16, 0.11, 0.04, 0.88);
		} else {
			cairo_set_source_rgba(cr, 1.0, 0.94, 0.78, 0.34);
		}
		cairo_set_line_width(cr, fmax(as * 0.30, 1.4));
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		/* up the desk */
		cairo_move_to(cr, ax, ay + as);
		cairo_line_to(cr, ax, ay - as);
		cairo_stroke(cr);
		cairo_move_to(cr, ax - as * 0.66, ay - as * 0.34);
		cairo_line_to(cr, ax, ay - as);
		cairo_line_to(cr, ax + as * 0.66, ay - as * 0.34);
		cairo_stroke(cr);
		cairo_restore(cr);
	}
	cairo_restore(cr);
}

/* How much of the lamp reaches a point on the desk, against what reaches
 * the middle of it. The gadget faces are painted by cairo at a fixed
 * brightness and laid over the bake, so without this the notepad in the
 * dark corner is as bright as the clock under the lamp, and both read as
 * stickers rather than as things inside the room. */
static double room_lamp_at(vec3 p)
{
	struct key_light k = key_at(p, ROOM_KIND);
	struct key_light mid = key_at(v3(0.0, 1.0, -2.0), ROOM_KIND);
	return clampf(k.irr / fmax(mid.irr, 1e-6), 0.0, 1.6);
}

/* One gadget's face, laid onto the quad its own frame gives it. The flat
 * ones on the desk and the upright ones on the shelf differ only in where
 * that quad is and which camera is looking at it. */
static void gadget_face_paint(cairo_t *cr, const struct room_gadget *g, int i,
	const struct room_cam *cam, double ox, double oy, double sw, double sh,
	time_t now, double lit)
{
	vec3 q[4];
	gadget_face_quad(g, q);
	cairo_matrix_t mt;
	if (!quad_matrix(cam, q, g->face_w, g->face_h, ox, oy, sw, sh, &mt)) {
		return;
	}
	struct face_print fp;
	if (!face_begin(&fp, cr, &mt, g->face_w, g->face_h)) {
		return;
	}
	cairo_t *fc = fp.fc;
	switch (i) {
	case DESK_GADGET_CLOCK:
		gadget_clock(fc, g->face_w, g->face_h, now);
		break;
	case DESK_GADGET_DESKCLOCK:
		gadget_deskclock(fc, g->face_w, g->face_h, now);
		break;
	case DESK_GADGET_CALENDAR:
		gadget_calendar(fc, g->face_w, g->face_h, now);
		break;
	default:
		gadget_memo(fc, g->face_w, g->face_h);
		break;
	}
	face_end(&fp, cr, clampf(lit, 0.0, 1.0));
	if (g->kind == GADGET_TENT) {
		return;   /* paper: no glass over it */
	}

	/* the glass over it catches the light */
	cairo_save(cr);
	cairo_transform(cr, &fp.mt);
	cairo_pattern_t *gl = cairo_pattern_create_linear(0, 0,
		g->face_w * 0.7, g->face_h);
	cairo_pattern_add_color_stop_rgba(gl, 0.00, 1, 1, 1, 0.14);
	cairo_pattern_add_color_stop_rgba(gl, 0.42, 1, 1, 1, 0.035);
	cairo_pattern_add_color_stop_rgba(gl, 1.00, 1, 1, 1, 0.00);
	cairo_set_source(cr, gl);
	cairo_rectangle(cr, 0, 0, g->face_w, g->face_h);
	cairo_fill(cr);
	cairo_pattern_destroy(gl);
	cairo_restore(cr);
}

/* The three stood up on the bookcase shelf, through the bookcase's own
 * camera. They are not on the desk any more, so nothing in the desk view
 * draws them. */
void desk_draw_shelf_gadgets(cairo_t *cr, int screen_w, int screen_h, time_t now)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!shelf_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return;
	}
	for (int i = 0; i < DESK_GADGET_COUNT; i++) {
		const struct room_gadget *g = &ROOM_GADGETS[i];
		if (!gadget_upright(g)) {
			continue;
		}
		gadget_face_paint(cr, g, i, &rr->shelf_cam, ox, oy,
			rr->w * scale, rr->h * scale, now, 0.92);
	}
}

/* ---- the shelf view, as sprites ------------------------------------
 *
 * The case itself changes when a binder is taken down and at no other
 * time. A binder the hand is over slides out of the row over a few dozen
 * frames, and all that changes is the rectangle it slides in -- so that
 * rectangle is drawn alone, by the same painter clipped to it, and laid
 * over the case: the case is opaque, so the result is what painting the
 * whole view would have been, to the pixel. The three things on the lower
 * board are sprites of their own. */

/* Where one binder can be, anywhere in its slide: its slot, and its
 * furthest-out copy with the shadow it throws, in screen pixels. */
bool desk_shelf_hover_box(int w, int h, int hover, double box[4])
{
	struct room_render *rr;
	double ox, oy, scale;
	if (hover < 0 || hover >= DESK_SHELF_THINGS
		|| !shelf_xform(w, h, &rr, &ox, &oy, &scale) || !rr->shelf_ids) {
		return false;
	}
	static unsigned rects_room;
	static int rects[DESK_SHELF_THINGS][4];
	static bool rects_ok[DESK_SHELF_THINGS];
	if (rects_room != rr->serial) {
		memset(rects_ok, 0, sizeof rects_ok);
		rects_room = rr->serial;
	}
	if (!rects_ok[hover]) {
		int x0 = rr->w, y0 = rr->h, x1 = -1, y1 = -1;
		for (int y = 0; y < rr->h; y++) {
			const unsigned char *row = rr->shelf_ids + (size_t)y * rr->w;
			for (int x = 0; x < rr->w; x++) {
				if (row[x] == hover + 1) {
					x0 = x < x0 ? x : x0;
					x1 = x > x1 ? x : x1;
					y0 = y < y0 ? y : y0;
					y1 = y;
				}
			}
		}
		if (x1 < 0) {
			return false;
		}
		rects[hover][0] = x0;
		rects[hover][1] = y0;
		rects[hover][2] = x1 - x0 + 1;
		rects[hover][3] = y1 - y0 + 1;
		rects_ok[hover] = true;
	}
	double bx0 = rects[hover][0], by0 = rects[hover][1];
	double bw = rects[hover][2], bh = rects[hover][3];
	/* desk_draw_shelf at hover_t 1: scaled by k about its centre, dropped
	 * by the lift, and its shadow a hair up and left of that */
	double k = 1.075, lift = bh * 0.085;
	double cx = bx0 + bw * 0.5, cy = by0 + bh * 0.5;
	double sx0 = cx + (bx0 - bw * 0.010 - cx) * k, sy0 = cy + lift + (by0 - bh * 0.030 - cy) * k;
	double px0 = cx + (bx0 - cx) * k, py0 = cy + lift + (by0 - cy) * k;
	double px1 = cx + (bx0 + bw - cx) * k, py1 = cy + lift + (by0 + bh - cy) * k;
	double x0 = fmin(fmin(bx0, sx0), px0), y0 = fmin(fmin(by0, sy0), py0);
	double x1 = fmax(bx0 + bw, px1), y1 = fmax(by0 + bh, py1);
	box[0] = floor(ox + x0 * scale) - 2.0;
	box[1] = floor(oy + y0 * scale) - 2.0;
	box[2] = ceil(ox + x1 * scale) + 2.0 - box[0];
	box[3] = ceil(oy + y1 * scale) + 2.0 - box[1];
	return true;
}

static int shelf_gadget_ok(int i)
{
	return i >= 0 && i < DESK_GADGET_COUNT && gadget_upright(&ROOM_GADGETS[i]);
}

bool desk_shelf_gadget_box(int w, int h, int i, double box[4])
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!shelf_gadget_ok(i) || !shelf_xform(w, h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	const struct room_gadget *g = &ROOM_GADGETS[i];
	vec3 q[4];
	gadget_face_quad(g, q);
	cairo_matrix_t mt;
	if (!quad_matrix(&rr->shelf_cam, q, g->face_w, g->face_h, ox, oy,
			rr->w * scale, rr->h * scale, &mt)) {
		return false;
	}
	double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
	for (int k = 0; k < 4; k++) {
		double x = (k & 1) ? g->face_w : 0.0, y = (k & 2) ? g->face_h : 0.0;
		cairo_matrix_transform_point(&mt, &x, &y);
		x0 = fmin(x0, x);
		y0 = fmin(y0, y);
		x1 = fmax(x1, x);
		y1 = fmax(y1, y);
	}
	box[0] = floor(x0) - 2.0;
	box[1] = floor(y0) - 2.0;
	box[2] = ceil(x1) + 2.0 - box[0];
	box[3] = ceil(y1) + 2.0 - box[1];
	return true;
}

void desk_draw_shelf_gadget(cairo_t *cr, int w, int h, int i, time_t now)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!shelf_gadget_ok(i) || !shelf_xform(w, h, &rr, &ox, &oy, &scale)) {
		return;
	}
	gadget_face_paint(cr, &ROOM_GADGETS[i], i, &rr->shelf_cam, ox, oy,
		rr->w * scale, rr->h * scale, now, 0.92);
}

uint64_t desk_shelf_gadget_key(int i, time_t now)
{
	if (i == DESK_GADGET_DESKCLOCK) {
		/* the seconds hand is thrown at each mark and rings about it, so
		 * the face is new every time it is looked at */
		struct timespec t;
		clock_gettime(CLOCK_MONOTONIC, &t);
		return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec + 1u;
	}
	struct tm tmv;
	localtime_r(&now, &tmv);
	uint64_t open_at = i == DESK_GADGET_CALENDAR
		? (uint64_t)g_cal_view + 4u * (uint64_t)desk_agenda_generation()
		: i == DESK_GADGET_NOTEPAD ? (uint64_t)desk_memo_generation() : 0u;
	return (((uint64_t)(tmv.tm_year + 1900) * 400u + (unsigned)tmv.tm_yday) * 8u
		+ (unsigned)i + 1u) * 8u + open_at;
}

/* One of the instruments lying on the desk, printed on the quad its glass
 * makes in the sheet. */
static void desk_gadget_face(cairo_t *cr, const struct room_xf *xf, int i, time_t now)
{
	const struct room_gadget *g = &ROOM_GADGETS[i];
	struct room_render *rr = xf->rr;
	/* Put it in the room's light rather than the studio's. Dimming by
	 * multiplying into the face itself turns its transparent parts
	 * opaque, because multiply still unions the alpha -- so the clock
	 * came back as a white card. Letting the face through at less than
	 * full strength dims it towards the body underneath, which is
	 * already lit correctly -- the clock's, a picture of it laid under
	 * this one (DESK_OVL_CASES). */
	double lit = room_lamp_at(v3(g->x, g->h, gadget_z(g)));
	gadget_face_paint(cr, g, i, &rr->cam, xf->ox, xf->oy,
		rr->w * xf->scale, rr->h * xf->scale, now,
		clampf(0.42 + 0.58 * lit, 0.38, 1.0));
}

void desk_draw_gadgets(cairo_t *cr, int w, int h, time_t now, double drawer_open)
{
	struct room_xf xf;
	if (!room_xform(w, h, drawer_open, &xf)) {
		/* Whoever asks for the room first starts it baking. This one used
		 * to be the exception, and on the path the compositor actually
		 * takes it is the only caller that runs every frame -- so the desk
		 * came up with the gadgets' faces blank and stayed that way. */
		desk_scene_warm(w, h, true);
		return;
	}
	for (int i = 0; i < DESK_GADGET_COUNT; i++) {
		if (!gadget_upright(&ROOM_GADGETS[i])) {
			desk_gadget_face(cr, &xf, i, now);   /* the rest live on the shelf */
		}
	}

	allapps_plate(cr, w, h, false);

	/* With the drawer out, the desk is not what you are looking at. It goes
	 * down the way a home screen does behind the multitasking tray -- which
	 * is also what keeps the felt reading as the lit thing in the frame.
	 * Everything below the line is the drawer, and it stays bright. */
	float rgb[3];
	double shade = desk_desk_shade(drawer_open, rgb);
	if (shade > 0.0 && xf.clip > 0.0) {
		cairo_save(cr);
		cairo_rectangle(cr, 0, 0, w, fmin(xf.clip, (double)h));
		cairo_clip(cr);
		cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], shade);
		cairo_paint(cr);
		cairo_restore(cr);
	}
}

/* ---- the desk mat -------------------------------------------------
 *
 * A sprite of its own (DESK_OVL_MAT) over the baked room and under everything
 * else, so a new mat is a redraw and not a new room. */

static const struct {
	const char *name;
	int kind;
	float rgb[3];
} MAT_PRESETS[] = {
	{ "green", DESK_MAT_COLOR, { 0.12f, 0.29f, 0.20f } },
	{ "navy", DESK_MAT_COLOR, { 0.10f, 0.16f, 0.30f } },
	{ "burgundy", DESK_MAT_COLOR, { 0.36f, 0.09f, 0.13f } },
	{ "charcoal", DESK_MAT_COLOR, { 0.16f, 0.16f, 0.17f } },
	{ "cream", DESK_MAT_COLOR, { 0.80f, 0.75f, 0.62f } },
	/* green morocco, as a writing desk's; and office linoleum, grey-green */
	{ "leather", DESK_MAT_LEATHER, { 0.20f, 0.36f, 0.24f } },
	{ "linoleum", DESK_MAT_LINOLEUM, { 0.50f, 0.55f, 0.46f } },
};

bool desk_mat_preset(const char *name, struct desk_mat *out)
{
	if (!name || !out) {
		return false;
	}
	for (size_t i = 0; i < sizeof MAT_PRESETS / sizeof MAT_PRESETS[0]; i++) {
		if (!strcasecmp(name, MAT_PRESETS[i].name)) {
			out->kind = MAT_PRESETS[i].kind;
			memcpy(out->rgb, MAT_PRESETS[i].rgb, sizeof out->rgb);
			return true;
		}
	}
	if (name[0] == '#' && strlen(name) == 7) {
		for (int i = 1; i < 7; i++) {
			if (!isxdigit((unsigned char)name[i])) {
				return false;
			}
		}
		unsigned v = (unsigned)strtoul(name + 1, NULL, 16);
		out->kind = DESK_MAT_COLOR;
		out->rgb[0] = (float)((v >> 16) & 0xff) / 255.0f;
		out->rgb[1] = (float)((v >> 8) & 0xff) / 255.0f;
		out->rgb[2] = (float)(v & 0xff) / 255.0f;
		return true;
	}
	return false;
}

static pthread_mutex_t g_mat_lock = PTHREAD_MUTEX_INITIALIZER;
static struct desk_mat g_mat = { .size = DESK_MAT_MEDIUM };
static uint64_t g_mat_gen = 1;          /* changes when what is shown does */
static uint64_t g_sheet_gen = 1;        /* and when a clear sheet itself would look otherwise */
static cairo_surface_t *g_mat_pic;      /* the photo read for g_mat_pic_path */
static char g_mat_pic_path[512];        /* ...even when it could not be read: not tried twice */
static bool g_mat_busy;                 /* the thread reading it is running */

/* The path is whatever shell.conf says and its file may have been sent, so
 * only a regular file of picture size is read, and a PNG only if its header
 * says a picture's worth of pixels. A photo is shrunk to MAT_PIC_MAX. */
#define MAT_PIC_MAX 2400
#define MAT_FILE_MAX (64L << 20)
#define MAT_PIC_PIXELS (48L << 20)
/* A PNG is read in the compositor only up to this many pixels (16 MB once
 * decoded); a larger one goes to the converter, a process of its own. */
#define MAT_PNG_DIRECT (4L << 20)

static cairo_surface_t *mat_pic_shrunk(cairo_surface_t *s)
{
	int pw = cairo_image_surface_get_width(s), ph = cairo_image_surface_get_height(s);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS || pw < 1 || ph < 1) {
		cairo_surface_destroy(s);
		return NULL;
	}
	if (pw <= MAT_PIC_MAX && ph <= MAT_PIC_MAX) {
		return s;
	}
	double k = (double)MAT_PIC_MAX / (pw > ph ? pw : ph);
	cairo_surface_t *d = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)ceil(pw * k),
		(int)ceil(ph * k));
	if (cairo_surface_status(d) == CAIRO_STATUS_SUCCESS) {
		cairo_t *cr = cairo_create(d);
		cairo_scale(cr, k, k);
		cairo_set_source_surface(cr, s, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
		cairo_paint(cr);
		cairo_destroy(cr);
	} else {
		cairo_surface_destroy(d);
		d = NULL;
	}
	cairo_surface_destroy(s);
	return d;
}

/* The pixels a PNG says it has in its first 24 bytes, head: 0 if it is not one. */
static unsigned long mat_png_pixels(const unsigned char head[24])
{
	if (memcmp(head, "\x89PNG\r\n\x1a\n", 8)) {
		return 0;
	}
	unsigned long pw = (unsigned long)head[16] << 24 | (unsigned long)head[17] << 16
		| (unsigned long)head[18] << 8 | head[19];
	unsigned long ph = (unsigned long)head[20] << 24 | (unsigned long)head[21] << 16
		| (unsigned long)head[22] << 8 | head[23];
	return pw > 65535 || ph > 65535 ? 0 : pw * ph;
}

/* A PNG whose first 24 bytes are head, read through fn, if it has no more than
 * max pixels: cairo allocates for the whole picture the moment it starts. */
static cairo_surface_t *mat_png_decode(const unsigned char head[24], long max,
	cairo_read_func_t fn, void *closure)
{
	unsigned long px = mat_png_pixels(head);
	if (px == 0 || px > (unsigned long)max) {
		return NULL;
	}
	cairo_surface_t *s = cairo_image_surface_create_from_png_stream(fn, closure);
	return s ? mat_pic_shrunk(s) : NULL;
}

static cairo_status_t mat_fd_read(void *closure, unsigned char *data, unsigned int length)
{
	while (length > 0) {
		ssize_t r = read(*(int *)closure, data, length);
		if (r < 0 && errno == EINTR) {
			continue;
		}
		if (r <= 0) {
			return CAIRO_STATUS_READ_ERROR;
		}
		data += r;
		length -= (unsigned int)r;
	}
	return CAIRO_STATUS_SUCCESS;
}

struct mat_mem {
	const unsigned char *p;
	size_t n, at;
};

static cairo_status_t mat_mem_read(void *closure, unsigned char *data, unsigned int length)
{
	struct mat_mem *m = closure;
	if (length > m->n - m->at) {
		return CAIRO_STATUS_READ_ERROR;
	}
	memcpy(data, m->p + m->at, length);
	m->at += length;
	return CAIRO_STATUS_SUCCESS;
}

/* A PNG file, if it is small enough to read here (else *big says it is for the
 * converter). Opened without waiting, since a named pipe would never end, and
 * read from that one descriptor, so what was checked is what is read. */
static cairo_surface_t *mat_png_file(const char *path, bool *big)
{
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		return NULL;
	}
	struct stat st;
	unsigned char head[24];
	cairo_surface_t *s = NULL;
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 24
		&& st.st_size <= MAT_FILE_MAX && pread(fd, head, sizeof head, 0) == (ssize_t)sizeof head) {
		unsigned long px = mat_png_pixels(head);
		*big = px > MAT_PNG_DIRECT && px <= MAT_PIC_PIXELS;
		s = mat_png_decode(head, MAT_PNG_DIRECT, mat_fd_read, &fd);
	}
	close(fd);
	return s;
}

/* What argv prints to stdout, up to MAT_FILE_MAX, and whether it exited 0.
 * Its input and its words go nowhere. Nothing is kept on disk. */
static unsigned char *mat_run(char *const argv[], size_t *len)
{
	int fd[2];
	if (pipe(fd) != 0) {
		return NULL;
	}
	fcntl(fd[0], F_SETFD, FD_CLOEXEC);
	fcntl(fd[1], F_SETFD, FD_CLOEXEC);   /* the child has its stdout, the dup of it */
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO);
	posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	pid_t pid;
	int err = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	close(fd[1]);
	if (err != 0) {
		close(fd[0]);
		return NULL;
	}
	unsigned char *buf = malloc(MAT_FILE_MAX);
	size_t got = 0;
	ssize_t r = 0;
	while (buf && got < MAT_FILE_MAX && (r = read(fd[0], buf + got, MAT_FILE_MAX - got)) != 0) {
		if (r < 0 && errno == EINTR) {
			continue;
		}
		if (r < 0) {
			break;
		}
		got += (size_t)r;
	}
	bool whole = r == 0;   /* all of it, not cut short by the limit or an error */
	close(fd[0]);
	int status = 0;
	pid_t w;
	while ((w = waitpid(pid, &status, 0)) < 0 && errno == EINTR) {
		;
	}
	if (!buf || !whole || w != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		free(buf);
		return NULL;
	}
	*len = got;
	return buf;
}

/* The photo at path (absolute): a small PNG as it is. Anything else, a large
 * PNG too, is made into a PNG of the size wanted by gdk-pixbuf's thumbnailer,
 * which only decodes pictures, or else by
 * ImageMagick, which goes by what is inside a file and runs what that asks
 * for, so it is given only a name it knows and told the decoder to use. */
static cairo_surface_t *mat_pic_read(const char *path)
{
	const char *dot = strrchr(path, '.');
	struct stat st;
	if (path[0] != '/' || strlen(path) > 400 || !dot) {
		return NULL;
	}
	if (!strcasecmp(dot, ".png")) {
		bool big = false;
		cairo_surface_t *png = mat_png_file(path, &big);
		if (png || !big) {
			return png;
		}
	}
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > MAT_FILE_MAX) {
		return NULL;
	}
	static const char *const CODERS[][2] = { { ".png", "png" }, { ".jpg", "jpeg" }, { ".jpeg", "jpeg" },
		{ ".webp", "webp" }, { ".gif", "gif" }, { ".bmp", "bmp" }, { ".tif", "tiff" },
		{ ".tiff", "tiff" } };
	const char *coder = NULL;
	for (size_t i = 0; i < sizeof CODERS / sizeof CODERS[0]; i++) {
		coder = !strcasecmp(dot, CODERS[i][0]) ? CODERS[i][1] : coder;
	}
	char size[16], area[16], in[460];
	snprintf(size, sizeof size, "%d", MAT_PIC_MAX);
	snprintf(area, sizeof area, "%ldMP", MAT_PIC_PIXELS >> 20);
	snprintf(in, sizeof in, "%s:%s[0]", coder ? coder : "", path);
	char *const pixbuf[] = { "/usr/bin/gdk-pixbuf-thumbnailer", "-s", size, (char *)path,
		"/dev/stdout", NULL };
	char *const magick[] = { "/usr/bin/magick", "-limit", "memory", "512MiB", "-limit", "map",
		"512MiB", "-limit", "disk", "0", "-limit", "area", area, in, "-auto-orient",
		"-resize", size, "PNG:-", NULL };
	bool have_pixbuf = access(pixbuf[0], X_OK) == 0;
	if (!have_pixbuf && (!coder || access(magick[0], X_OK) != 0)) {
		return NULL;
	}
	/* a minute at most: timeout(1) in front of either */
	char *argv[24] = { "timeout", "30" };
	char *const *tool = have_pixbuf ? pixbuf : magick;
	for (int i = 0; tool[i] && i < 21; i++) {
		argv[i + 2] = tool[i];
	}
	size_t n = 0;
	unsigned char *png = mat_run(argv, &n);
	if (!png) {
		return NULL;
	}
	struct mat_mem m = { png, n, 0 };
	cairo_surface_t *s = n > 24 ? mat_png_decode(png, (long)MAT_PIC_MAX * MAT_PIC_MAX,
		mat_mem_read, &m) : NULL;
	free(png);
	return s;
}

/* The picture a mat lays all over it, or "": a photo mat's, and a clear
 * mat's when it has one picture under all of it. */
static const char *mat_photo_of(const struct desk_mat *m)
{
	bool whole = m->kind == DESK_MAT_PHOTO
		|| (m->kind == DESK_MAT_CLEAR && m->under == DESK_MAT_UNDER_WHOLE);
	return whole ? m->photo : "";
}

/* Reads the photo shell.conf names, and then the next if that changed meanwhile. */
static void *mat_pic_thread(void *arg)
{
	(void)arg;
	for (;;) {
		char want[512];
		pthread_mutex_lock(&g_mat_lock);
		snprintf(want, sizeof want, "%s", mat_photo_of(&g_mat));
		if (!want[0] || !strcmp(want, g_mat_pic_path)) {
			g_mat_busy = false;
			pthread_mutex_unlock(&g_mat_lock);
			return NULL;
		}
		pthread_mutex_unlock(&g_mat_lock);
		cairo_surface_t *s = mat_pic_read(want);
		if (!s) {
			fprintf(stderr, "desk: mat: cannot read %s\n", want);
		}
		pthread_mutex_lock(&g_mat_lock);
		if (g_mat_pic) {
			cairo_surface_destroy(g_mat_pic);
		}
		g_mat_pic = s;
		snprintf(g_mat_pic_path, sizeof g_mat_pic_path, "%s", want);
		g_mat_gen++;
		pthread_mutex_unlock(&g_mat_lock);
	}
}

/* With g_mat_busy just set. */
static void mat_pic_start(void)
{
	pthread_t th;
	if (pthread_create(&th, NULL, mat_pic_thread, NULL) == 0) {
		pthread_detach(th);
		return;
	}
	pthread_mutex_lock(&g_mat_lock);
	g_mat_busy = false;
	pthread_mutex_unlock(&g_mat_lock);
}

bool desk_set_mat(const struct desk_mat *m)
{
	if (!m) {
		return false;
	}
	pthread_mutex_lock(&g_mat_lock);
	bool same = g_mat.kind == m->kind && g_mat.size == m->size
		&& !memcmp(g_mat.rgb, m->rgb, sizeof g_mat.rgb)
		&& (m->kind != DESK_MAT_CLEAR || g_mat.under == m->under)
		&& !strcmp(mat_photo_of(&g_mat), mat_photo_of(m));
	bool photo = mat_photo_of(m)[0] != '\0';
	/* one that could not be read is tried again when the settings are applied again */
	bool start = photo && !g_mat_busy
		&& (same ? !g_mat_pic && !strcmp(g_mat_pic_path, m->photo)
			: strcmp(m->photo, g_mat_pic_path) != 0);
	if (!same) {
		g_mat = *m;
		g_mat.photo[sizeof g_mat.photo - 1] = '\0';
		g_mat_gen++;
		g_sheet_gen++;
	}
	if (!photo && g_mat_pic) {
		cairo_surface_destroy(g_mat_pic);
		g_mat_pic = NULL;
		g_mat_pic_path[0] = '\0';
	}
	if (start) {
		g_mat_pic_path[0] = '\0';
		g_mat_busy = true;
	}
	pthread_mutex_unlock(&g_mat_lock);
	if (start) {
		mat_pic_start();
	}
	return !same;
}

/* How far the mat lies unrolled (desk_mat_unroll): 1, flat. */
static double g_unroll = 1.0;

static uint64_t mat_key(void)
{
	pthread_mutex_lock(&g_mat_lock);
	uint64_t k = g_mat_gen ^ (uint64_t)lround(g_unroll * 4096.0) << 44;
	pthread_mutex_unlock(&g_mat_lock);
	return k;
}

/* Where the mat lies (x, y, w, h): centred, near the front edge, and never up
 * to the console, clock and post box. Its size is in points, as the windows'
 * are, so a larger screen shows more desk, not a larger mat: the sizes are
 * a few places on the way from the least (0) to all the open desk (1), and
 * it is never more than that. */
#define MAT_OPEN_W 0.92   /* the open desk, as a share of the screen */
#define MAT_OPEN_H 0.82
static void mat_rect(int w, int h, double size, double r[4])
{
	static const double AT[] = { 0.0, DESK_MAT_SMALL, DESK_MAT_MEDIUM, DESK_MAT_LARGE };
	static const double PW[] = { 480.0, 600.0, 900.0, 1160.0 };   /* points across, and */
	static const double PH[] = { 320.0, 380.0, 530.0, 650.0 };    /* deep */
	size = isfinite(size) ? clampf(size, 0.0, 1.0) : DESK_MAT_MEDIUM;
	double k = desk_px_per_pt(w), ow = w * MAT_OPEN_W, oh = h * MAT_OPEN_H, mw, mh;
	if (size >= AT[3]) {
		double t = (size - AT[3]) / (1.0 - AT[3]);
		mw = PW[3] * k + (ow - PW[3] * k) * t;
		mh = PH[3] * k + (oh - PH[3] * k) * t;
	} else {
		int i = size < AT[1] ? 0 : size < AT[2] ? 1 : 2;
		double t = (size - AT[i]) / (AT[i + 1] - AT[i]);
		mw = (PW[i] + (PW[i + 1] - PW[i]) * t) * k;
		mh = (PH[i] + (PH[i + 1] - PH[i]) * t) * k;
	}
	r[2] = fmin(mw, ow);
	r[3] = fmin(mh, oh);
	r[0] = (w - r[2]) * 0.5;
	r[1] = h * 0.96 - r[3];
}

/* How big a mat lies: a clear one is all the open desk, whatever it says. */
static double mat_size_of(const struct desk_mat *m)
{
	return m->kind == DESK_MAT_CLEAR ? DESK_MAT_FULL : m->size;
}

/* The mat's lift off the boards, in pixels: a thick felt blotter. */
static double mat_lift(int w)
{
	return fmax(2.0, 4.0 * desk_px_per_pt(w));
}

static void under_box(int w, int h, const struct desk_mat *m, double box[4]);

/* it and the shadow it throws, or false when there is no mat */
static bool mat_box(int w, int h, double box[4])
{
	pthread_mutex_lock(&g_mat_lock);
	struct desk_mat m = g_mat;
	pthread_mutex_unlock(&g_mat_lock);
	int kind = m.kind;
	double size = mat_size_of(&m);
	if (kind == DESK_MAT_NONE) {
		return false;
	}
	if (kind == DESK_MAT_CLEAR) {
		under_box(w, h, &m, box);
		return true;
	}
	double r[4], pad = 32.0 * desk_px_per_pt(w);
	mat_rect(w, h, size, r);
	box[0] = floor(r[0] - pad);
	box[1] = floor(r[1] - pad);
	box[2] = ceil(r[0] + r[2] + pad) - box[0];
	box[3] = ceil(r[1] + r[3] + pad) - box[1];
	return true;
}

bool desk_mat_size_parse(const char *s, double *out)
{
	if (!s || !out) {
		return false;
	}
	if (!strcasecmp(s, "small")) {
		*out = DESK_MAT_SMALL;
	} else if (!strcasecmp(s, "medium")) {
		*out = DESK_MAT_MEDIUM;
	} else if (!strcasecmp(s, "large")) {
		*out = DESK_MAT_LARGE;
	} else if (!strcasecmp(s, "full")) {
		*out = DESK_MAT_FULL;
	} else {
		char *end = NULL;
		double v = strtod(s, &end);
		if (end == s || strcmp(end, "%") || !isfinite(v) || v < 0.0 || v > 100.0) {
			return false;
		}
		*out = v / 100.0;
	}
	return true;
}

/* How much of the lamp reaches each part of the desk, from the baked room
 * drawn small, so that the grain averages out and the pool of the lamp, the
 * dark toward the corners and the shadows at the back are left; against its
 * brightest. The mat is darkened by it, as the boards are. Made again only
 * for another room or size, and only from the compositor's thread (or
 * desk-preview's), so it needs no lock. */
#define MAT_LIGHT_W 16
#define MAT_LIGHT_H 10
static cairo_surface_t *g_mat_light;
static unsigned g_mat_light_room;
static int g_mat_light_w, g_mat_light_h;

static cairo_surface_t *mat_light(int w, int h)
{
	unsigned room = desk_room_shown(w, h);
	if (!room) {
		return NULL;   /* no room yet: evenly lit, and drawn again when there is */
	}
	if (g_mat_light && g_mat_light_room == room && g_mat_light_w == w && g_mat_light_h == h) {
		return g_mat_light;
	}
	/* a quarter of the size is plenty to average from, and a sixteenth of the pixels */
	cairo_surface_t *full = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (w + 3) / 4,
		(h + 3) / 4);
	cairo_t *fc = cairo_create(full);
	cairo_scale(fc, 0.25, 0.25);
	desk_draw_room(fc, w, h, 0.0);
	cairo_destroy(fc);
	cairo_surface_t *small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, MAT_LIGHT_W,
		MAT_LIGHT_H);
	cairo_t *sc = cairo_create(small);
	cairo_scale(sc, (double)MAT_LIGHT_W / ((w + 3) / 4), (double)MAT_LIGHT_H / ((h + 3) / 4));
	cairo_set_source_surface(sc, full, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(sc), CAIRO_FILTER_GOOD);
	cairo_paint(sc);
	cairo_destroy(sc);
	cairo_surface_destroy(full);
	cairo_surface_flush(small);

	double lum[MAT_LIGHT_W * MAT_LIGHT_H], ref = 0.02;
	const unsigned char *px = cairo_image_surface_get_data(small);
	int stride = cairo_image_surface_get_stride(small);
	for (int y = 0; y < MAT_LIGHT_H; y++) {
		const uint32_t *row = (const uint32_t *)(px + (size_t)y * stride);
		for (int x = 0; x < MAT_LIGHT_W; x++) {
			uint32_t c = row[x];
			lum[y * MAT_LIGHT_W + x] = (0.30 * ((c >> 16) & 0xff) + 0.59 * ((c >> 8) & 0xff)
				+ 0.11 * (c & 0xff)) / 255.0;
			ref = fmax(ref, lum[y * MAT_LIGHT_W + x]);
		}
	}
	cairo_surface_t *map = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, MAT_LIGHT_W,
		MAT_LIGHT_H);
	unsigned char *mp = cairo_image_surface_get_data(map);
	int ms = cairo_image_surface_get_stride(map);
	for (int y = 0; y < MAT_LIGHT_H; y++) {
		uint32_t *row = (uint32_t *)(mp + (size_t)y * ms);
		for (int x = 0; x < MAT_LIGHT_W; x++) {
			/* a felt is lit more evenly than boards are: not all the way */
			double f = clampf(lum[y * MAT_LIGHT_W + x] / ref, 0.0, 1.0);
			f = 1.0 - 0.85 * (1.0 - f);
			uint32_t g = (uint32_t)lround(f * 255.0);
			row[x] = 0xff000000u | (g << 16) | (g << 8) | g;
		}
	}
	cairo_surface_mark_dirty(map);
	cairo_surface_destroy(small);
	if (g_mat_light) {
		cairo_surface_destroy(g_mat_light);
	}
	g_mat_light = map;
	g_mat_light_room = room;
	g_mat_light_w = w;
	g_mat_light_h = h;
	return map;
}

/* what the thread of the stitching is: light on a dark mat, dark on a
 * light one */
static void mat_thread(const float rgb[3], double out[3])
{
	double lum = 0.30 * rgb[0] + 0.59 * rgb[1] + 0.11 * rgb[2];
	if (lum < 0.45) {
		out[0] = 0.86;
		out[1] = 0.76;
		out[2] = 0.54;
	} else {
		out[0] = 0.30;
		out[1] = 0.21;
		out[2] = 0.12;
	}
}

static void clear_under_paint(cairo_t *cr, int w, int h, const struct desk_mat *m, cairo_surface_t *pic);

/* The mat lying flat. */
static void mat_paint_flat(cairo_t *cr, int w, int h)
{
	struct desk_mat m;
	cairo_surface_t *pic = NULL;
	pthread_mutex_lock(&g_mat_lock);
	m = g_mat;
	if (mat_photo_of(&m)[0] && g_mat_pic && !strcmp(g_mat_pic_path, m.photo)) {
		pic = cairo_surface_reference(g_mat_pic);
	}
	pthread_mutex_unlock(&g_mat_lock);
	if (m.kind == DESK_MAT_NONE) {
		return;
	}
	if (m.kind == DESK_MAT_CLEAR) {
		clear_under_paint(cr, w, h, &m, pic);
		if (pic) {
			cairo_surface_destroy(pic);
		}
		return;
	}
	double r[4], k = desk_px_per_pt(w);
	mat_rect(w, h, m.size, r);
	/* felt's corners well rounded; leather's turned over a board, less;
	 * linoleum cut from the roll */
	double x = r[0], y = r[1], mw = r[2], mh = r[3];
	double rad = (m.kind == DESK_MAT_LEATHER ? 7.0 : m.kind == DESK_MAT_LINOLEUM ? 2.5 : 12.0) * k;
	double lift = mat_lift(w);

	/* Its shadow, as the windows' are: each edge leans away from the lamp by what
	 * a mat this thick throws there. Hard where it touches, soft beyond. */
	double lx, rx, ty, by, along;
	shadow_dir(x, y + mh * 0.5, lift, &lx, &along);
	shadow_dir(x + mw, y + mh * 0.5, lift, &rx, &along);
	shadow_dir(x + mw * 0.5, y, lift, &along, &ty);
	shadow_dir(x + mw * 0.5, y + mh, lift, &along, &by);
	const int cast = 10;
	for (int i = cast; i >= 1; i--) {
		double g = 3.0 * lift * i / cast;
		desk_rounded_path(cr, x + fmin(lx, 0.0) - g, y + fmin(ty, 0.0) - g,
			mw + fmax(rx, 0.0) - fmin(lx, 0.0) + 2 * g,
			mh + fmax(by, 0.0) - fmin(ty, 0.0) + 2 * g, rad + g);
		cairo_set_source_rgba(cr, 0.04, 0.025, 0.012, 0.030);
		cairo_fill(cr);
	}
	const int touch = 6;
	for (int i = touch; i >= 1; i--) {
		double g = 1.1 * lift * i / touch;
		desk_rounded_path(cr, x + lx * 0.5 - g, y + ty * 0.5 - g,
			mw + (rx - lx) * 0.5 + 2 * g, mh + (by - ty) * 0.5 + 2 * g, rad + g);
		cairo_set_source_rgba(cr, 0.03, 0.018, 0.008, 0.085);
		cairo_fill(cr);
	}

	cairo_save(cr);
	desk_rounded_path(cr, x, y, mw, mh, rad);
	cairo_clip(cr);
	if (pic) {
		/* cut to fill it, from the middle of the picture */
		double pw = cairo_image_surface_get_width(pic), ph = cairo_image_surface_get_height(pic);
		double k = fmax(mw / pw, mh / ph);
		cairo_save(cr);
		cairo_translate(cr, x + (mw - pw * k) * 0.5, y + (mh - ph * k) * 0.5);
		cairo_scale(cr, k, k);
		cairo_set_source_surface(cr, pic, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		cairo_set_source_rgb(cr, m.rgb[0], m.rgb[1], m.rgb[2]);
		cairo_paint(cr);
		if (m.kind == DESK_MAT_LEATHER) {
			/* its pebbles, at the size they are; rubbed paler in the middle,
			 * where the hands and the paper go; and a little shine */
			cairo_pattern_t *pat = tile_pattern(cr, pebble_tile(), 0.5 * k, 0.5 * k, CAIRO_FILTER_GOOD);
			cairo_set_source(cr, pat);
			cairo_paint(cr);
			cairo_pattern_destroy(pat);
			cairo_pattern_t *rub = cairo_pattern_create_radial(x + mw * 0.5, y + mh * 0.62, 0.0,
				x + mw * 0.5, y + mh * 0.62, fmax(mw, mh) * 0.55);
			cairo_pattern_add_color_stop_rgba(rub, 0.0, 0.80, 0.88, 0.72, 0.16);
			cairo_pattern_add_color_stop_rgba(rub, 1.0, 0.80, 0.88, 0.72, 0.0);
			cairo_set_source(cr, rub);
			cairo_paint(cr);
			cairo_pattern_destroy(rub);
			overlay_grain(cr, x, y, mw, mh, 0.6);
		} else if (m.kind == DESK_MAT_LINOLEUM) {
			/* marbled along the roll, and gritty, matt */
			cairo_pattern_t *pat = tile_pattern(cr, marble_tile(), 0.7 * k, 0.7 * k, CAIRO_FILTER_GOOD);
			cairo_set_source(cr, pat);
			cairo_paint(cr);
			cairo_pattern_destroy(pat);
			overlay_grain(cr, x, y, mw, mh, 0.8);
		} else {
			/* felt: a nap you can see, and nothing shiny */
			overlay_grain(cr, x, y, mw, mh, 1.6);
		}
	}
	/* the lamp on it: the baked room's own light, over the whole mat */
	cairo_surface_t *lit = mat_light(w, h);
	if (lit) {
		cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
		cairo_set_source_surface(cr, lit, 0, 0);
		cairo_pattern_t *lp = cairo_get_source(cr);
		cairo_pattern_set_filter(lp, CAIRO_FILTER_BILINEAR);
		cairo_pattern_set_extend(lp, CAIRO_EXTEND_PAD);
		cairo_matrix_t mt;
		cairo_matrix_init_scale(&mt, (double)MAT_LIGHT_W / w, (double)MAT_LIGHT_H / h);
		cairo_pattern_set_matrix(lp, &mt);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	}
	cairo_restore(cr);

	/* its edge, rounded over: bright where it faces the lamp, dark away
	 * from it */
	double tx2 = 0.0, ty2 = 0.0, tz2 = 0.0;
	lamp_toward(x + mw * 0.5, y + mh * 0.5, &tx2, &ty2, &tz2);
	double hn = hypot(tx2, ty2);
	double dxl = hn > 1e-6 ? tx2 / hn : -0.6, dyl = hn > 1e-6 ? ty2 / hn : -0.8;
	double R = 0.5 * hypot(mw, mh);
	cairo_pattern_t *edge = cairo_pattern_create_linear(x + mw * 0.5 + dxl * R,
		y + mh * 0.5 + dyl * R, x + mw * 0.5 - dxl * R, y + mh * 0.5 - dyl * R);
	cairo_pattern_add_color_stop_rgba(edge, 0.0, 1.0, 0.94, 0.80, 0.30);
	cairo_pattern_add_color_stop_rgba(edge, 0.5, 1.0, 0.94, 0.80, 0.0);
	cairo_pattern_add_color_stop_rgba(edge, 1.0, 0.0, 0.0, 0.0, 0.45);
	desk_rounded_path(cr, x + 0.5, y + 0.5, mw - 1.0, mh - 1.0, rad);
	cairo_set_source(cr, edge);
	cairo_set_line_width(cr, fmax(1.0, 2.0 * k));
	cairo_stroke(cr);
	cairo_pattern_destroy(edge);

	if (m.kind == DESK_MAT_LEATHER && !pic) {
		/* tooled round in gold, a little in from the edge, and a blind line
		 * pressed inside it */
		double in = 11.0 * k, ir = fmax(rad - in * 0.5, 2.0);
		desk_rounded_path(cr, x + in + 4.0 * k, y + in + 4.0 * k, mw - 2 * in - 8.0 * k,
			mh - 2 * in - 8.0 * k, ir);
		cairo_set_source_rgba(cr, 0.0, 0.02, 0.0, 0.30);
		cairo_set_line_width(cr, fmax(1.0, 1.2 * k));
		cairo_stroke(cr);
		desk_rounded_path(cr, x + in, y + in + 0.8, mw - 2 * in, mh - 2 * in, ir);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
		cairo_set_line_width(cr, fmax(1.4, 2.0 * k));
		cairo_stroke(cr);
		desk_rounded_path(cr, x + in, y + in, mw - 2 * in, mh - 2 * in, ir);
		cairo_set_source_rgb(cr, 0.78, 0.60, 0.27);
		cairo_set_line_width(cr, fmax(1.2, 1.7 * k));
		cairo_stroke(cr);
		desk_rounded_path(cr, x + in - 0.3, y + in - 0.4, mw - 2 * in, mh - 2 * in, ir);
		cairo_set_source_rgba(cr, 1.0, 0.92, 0.66, 0.45);
		cairo_set_line_width(cr, fmax(0.6, 0.6 * k));
		cairo_stroke(cr);
		if (pic) {
			cairo_surface_destroy(pic);
		}
		return;
	}
	if (m.kind == DESK_MAT_LINOLEUM) {
		/* cut square from the roll: nothing round its edge but the edge */
		if (pic) {
			cairo_surface_destroy(pic);
		}
		return;
	}
	/* and stitched round, a little in from the edge: each stitch with the
	 * shadow of the thread on the cloth under it */
	double thread[3];
	mat_thread(m.rgb, thread);
	double in = 14.0 * k, sw = fmax(1.4, 2.7 * k);
	double dash[2] = { 9.5 * k, 6.0 * k };
	desk_rounded_path(cr, x + in, y + in, mw - 2 * in, mh - 2 * in, fmax(rad * 0.55, 2.0));
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_dash(cr, dash, 2, 0.0);
	cairo_save(cr);
	cairo_translate(cr, 0.5, 0.9);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.38);
	cairo_set_line_width(cr, sw + 0.5);
	cairo_stroke_preserve(cr);
	cairo_restore(cr);
	cairo_set_source_rgb(cr, thread[0], thread[1], thread[2]);
	cairo_set_line_width(cr, sw);
	cairo_stroke_preserve(cr);
	cairo_save(cr);
	cairo_translate(cr, -0.25, -0.4);
	cairo_set_source_rgba(cr, 1.0, 0.97, 0.88, 0.28);
	cairo_set_line_width(cr, sw * 0.4);
	cairo_stroke(cr);
	cairo_restore(cr);
	cairo_set_dash(cr, NULL, 0, 0.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	if (pic) {
		cairo_surface_destroy(pic);
	}
}

/* ---- the mat rolled out, and up -----------------------------------
 *
 * A mat is laid as one is on a real desk: set down rolled up at the back of
 * it and unrolled toward you; and taken away rolled up again (wm.c drives
 * it, desk_mat_unroll). What lies flat is the mat as it will lie, drawn once
 * and kept while it rolls; the roll is the rest of it wound round, face out,
 * lit from the lamp behind it and throwing its shadow forward. Felt, leather,
 * linoleum and a photo mat roll; a clear sheet is laid at once. */

#define MAT_THICK_PT 2.6      /* as thick as a mat is, wound */
#define MAT_CORE_PT 5.0       /* and what it is wound round to start with */

static cairo_surface_t *g_flat_mat;   /* the mat flat, while it rolls: for this look, size, room */
static uint64_t g_flat_gen;
static int g_flat_w, g_flat_h;
static unsigned g_flat_room;

bool desk_mat_rolls(const struct desk_mat *m)
{
	return m && (m->kind == DESK_MAT_COLOR || m->kind == DESK_MAT_LEATHER
		|| m->kind == DESK_MAT_LINOLEUM || m->kind == DESK_MAT_PHOTO);
}

void desk_mat_unroll(double t)
{
	t = isfinite(t) ? clampf(t, 0.0, 1.0) : 1.0;
	pthread_mutex_lock(&g_mat_lock);
	g_unroll = t;
	pthread_mutex_unlock(&g_mat_lock);
	if (t >= 1.0 && g_flat_mat) {
		cairo_surface_destroy(g_flat_mat);
		g_flat_mat = NULL;
	}
}

static void mat_paint(cairo_t *cr, int w, int h)
{
	pthread_mutex_lock(&g_mat_lock);
	double t = g_unroll;
	struct desk_mat m = g_mat;
	uint64_t gen = g_mat_gen;
	pthread_mutex_unlock(&g_mat_lock);
	if (t >= 0.9995 || !desk_mat_rolls(&m)) {
		mat_paint_flat(cr, w, h);
		return;
	}
	unsigned room = desk_room_shown(w, h);
	if (!g_flat_mat || g_flat_gen != gen || g_flat_w != w || g_flat_h != h || g_flat_room != room) {
		if (g_flat_mat) {
			cairo_surface_destroy(g_flat_mat);
		}
		g_flat_mat = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		cairo_t *fc = cairo_create(g_flat_mat);
		mat_paint_flat(fc, w, h);
		cairo_destroy(fc);
		g_flat_gen = gen;
		g_flat_w = w;
		g_flat_h = h;
		g_flat_room = room;
	}
	double r[4], k = desk_px_per_pt(w);
	mat_rect(w, h, m.size, r);
	double x = r[0], y = r[1], mw = r[2], mh = r[3];
	/* laid this far from the back; the rest wound round as thick as it is,
	 * the roll resting on the desk where the laid part ends */
	double laid = mh * t, core = MAT_CORE_PT * k;
	double R = sqrt((mh - laid) * MAT_THICK_PT * k / M_PI + core * core);
	double c = y + laid;
	/* (set down, or taken up, in the first moment) */
	double seen = clampf(t / 0.03, 0.0, 1.0);
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, c);
	cairo_clip(cr);
	cairo_set_source_surface(cr, g_flat_mat, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
	/* its shadow on the desk in front of it */
	cairo_pattern_t *sh = cairo_pattern_create_linear(0.0, c, 0.0, c + R * 1.9);
	cairo_pattern_add_color_stop_rgba(sh, 0.0, 0.03, 0.018, 0.008, 0.46 * seen);
	cairo_pattern_add_color_stop_rgba(sh, 1.0, 0.03, 0.018, 0.008, 0.0);
	cairo_rectangle(cr, x - 2.0 * k, c, mw + 6.0 * k, R * 1.9);
	cairo_set_source(cr, sh);
	cairo_fill(cr);
	cairo_pattern_destroy(sh);
	/* the roll: from the back of it over the top to the front, the mat that
	 * is still to come, round it */
	cairo_save(cr);
	cairo_rectangle(cr, x, c - R, mw, 2.0 * R);
	cairo_clip(cr);
	const int N = 28;
	for (int i = 0; i < N; i++) {
		double a0 = -M_PI * 0.5 + M_PI * i / N, a1 = -M_PI * 0.5 + M_PI * (i + 1) / N;
		double y0 = c + R * sin(a0), y1 = c + R * sin(a1);
		double s0 = c + R * (a0 + M_PI), s1 = c + R * (a1 + M_PI);
		/* (past its front edge, round again from its back: what is wound) */
		s0 = y + fmod(s0 - y, mh);
		s1 = s0 + R * (a1 - a0);
		cairo_save(cr);
		cairo_rectangle(cr, x, y0, mw, y1 - y0 + 0.6);
		cairo_clip(cr);
		cairo_translate(cr, 0.0, y0);
		cairo_scale(cr, 1.0, (y1 - y0) / (s1 - s0));
		cairo_translate(cr, 0.0, -s0);
		cairo_set_source_surface(cr, g_flat_mat, 0, 0);
		cairo_paint_with_alpha(cr, seen);
		cairo_restore(cr);
	}
	/* lit as it turns: the lamp behind it and above, so its back and top
	 * take the light, and its front, under, falls into shade */
	cairo_pattern_t *lit = cairo_pattern_create_linear(0.0, c - R, 0.0, c + R);
	for (int j = 0; j <= 8; j++) {
		double a = -M_PI * 0.5 + M_PI * j / 8.0;
		double off = (sin(a) + 1.0) * 0.5;
		double l = cos(a + 0.45);
		if (l > 0.55) {
			cairo_pattern_add_color_stop_rgba(lit, off, 1.0, 0.95, 0.85, 0.24 * (l - 0.55) / 0.45 * seen);
		} else {
			cairo_pattern_add_color_stop_rgba(lit, off, 0.0, 0.0, 0.0, 0.62 * (0.55 - l) / 1.55 * seen);
		}
	}
	cairo_rectangle(cr, x, c - R, mw, 2.0 * R);
	cairo_set_source(cr, lit);
	cairo_fill(cr);
	cairo_pattern_destroy(lit);
	/* its ends, the turns showing as lines */
	cairo_set_line_width(cr, fmax(1.0, 1.2 * k));
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35 * seen);
	for (int e = 0; e < 2; e++) {
		double ex = e ? x + mw - 0.6 : x + 0.6;
		cairo_move_to(cr, ex, c - R);
		cairo_line_to(cr, ex, c + R);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* ---- the clear mat, and what is slipped under it ----------------------
 *
 * A sheet of clear vinyl over all the open desk, as an office desk has under
 * its glass: the boards are seen through it a shade darker, the lamp lies in
 * it, and its edge, as thick as the sheet, catches the light. Under it lies
 * one picture over all of it, or prints, each where it was laid and turned
 * as it was. They are moved with the mat lifted (thing-mat.c); laid down,
 * nothing moves them. Like the mat, it is all one sprite (DESK_OVL_MAT),
 * drawn again only when what is under it, or the room, has changed. */

#define CLEAR_RADIUS_PT 6.0    /* its corners, rounded */
#define CLEAR_EDGE_PT 2.4      /* the bevel round its edge: the sheet is about this thick */
#define CLEAR_LIFT_PT 1.2      /* lying on the desk, it stands this far off it at its edge */
#define CLEAR_LIFTED_PT 12.0   /* and lifted, to put something under it */
#define PAPER_LIFT_PT 0.7      /* a print pressed flat under it */
#define PAPER_BORDER 0.045     /* a print's white margin, against its shorter side */
#define PAPER_READ_MAX 2048    /* a print is read no bigger than this across */

/* What each paper's print is as read: kept beside the papers, under g_mat_lock. */
struct mat_print {
	char name[96];           /* what was read: the paper's name, once it has been */
	cairo_surface_t *pic;    /* NULL: not yet, or it could not be (failed) */
	int px;                  /* the longer side it was read at */
	bool failed;
};
static struct desk_mat_paper g_papers[DESK_MAT_PAPERS_MAX];
static struct mat_print g_prints[DESK_MAT_PAPERS_MAX];
static int g_npapers;
static bool g_papers_busy;       /* the thread reading prints is running */
static double g_papers_k;        /* pixels to the point where the mat was last painted (0: not yet) */
static int g_mat_corner = -1;    /* the corner the sheet is turned up at, or -1 */
static double g_mat_cut;         /* and how far, in points along each edge */
static bool g_mat_lifted;        /* lifted off what is under it */
static int g_mat_held = -1;
static struct stat g_papers_st;  /* the papers' file as it was last read, or written */
static bool g_papers_known;      /* whether it has been looked for at all */

void desk_mat_prints_dir(char *out, size_t n)
{
	const char *data = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	if (data && *data) {
		snprintf(out, n, "%s/desk/mat", data);
	} else if (home && *home) {
		snprintf(out, n, "%s/.local/share/desk/mat", home);
	} else {
		out[0] = '\0';
	}
}

void desk_mat_papers_file(char *out, size_t n)
{
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg) {
		snprintf(out, n, "%s/desk/desk-mat", cfg);
	} else if (home && *home) {
		snprintf(out, n, "%s/.config/desk/desk-mat", home);
	} else {
		out[0] = '\0';
	}
}

/* A print's name as the file may say it: a file in the prints' folder, and
 * only there. */
static bool paper_name_ok(const char *s)
{
	size_t n = strlen(s);
	if (n == 0 || n >= sizeof g_papers[0].name || s[0] == '.') {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == '/' || c < 0x20 || c == 0x7f) {
			return false;
		}
	}
	return true;
}

/* How big a print is, from its PNG's head; 3 by 2, as a photograph is,
 * until it can be read. */
static void paper_size_read(struct desk_mat_paper *p)
{
	char dir[400], path[512];
	p->pw = 3;
	p->ph = 2;
	desk_mat_prints_dir(dir, sizeof dir);
	if (!dir[0] || !paper_name_ok(p->name)) {
		return;
	}
	snprintf(path, sizeof path, "%s/%s", dir, p->name);
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		return;
	}
	struct stat st;
	unsigned char head[24];
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)
		&& pread(fd, head, sizeof head, 0) == (ssize_t)sizeof head && mat_png_pixels(head) > 0) {
		p->pw = head[16] << 24 | head[17] << 16 | head[18] << 8 | head[19];
		p->ph = head[20] << 24 | head[21] << 16 | head[22] << 8 | head[23];
	}
	close(fd);
}

/* A paper as the file and the hand may give it: on the mat, of a size a
 * print can be. */
static void paper_tidy(struct desk_mat_paper *p)
{
	p->x = isfinite(p->x) ? clampf(p->x, -4000.0, 4000.0) : 0.0;
	p->y = isfinite(p->y) ? clampf(p->y, -4000.0, 4000.0) : 0.0;
	p->w = isfinite(p->w) ? clampf(p->w, DESK_MAT_PAPER_MIN_PT, DESK_MAT_PAPER_MAX_PT) : 240.0;
	p->turn = isfinite(p->turn) ? fmod(p->turn, 360.0) : 0.0;
	if (p->pw < 1 || p->ph < 1) {
		p->pw = 3;
		p->ph = 2;
	}
}

/* Where a paper lies: its middle, and half its width and height before it is
 * turned, on a screen k pixels to the point with the mat at r. */
static void paper_place_in(const double r[4], double k, const struct desk_mat_paper *p, double *cx,
	double *cy, double *hw, double *hh)
{
	*cx = r[0] + r[2] * 0.5 + p->x * k;
	*cy = r[1] + r[3] * 0.5 + p->y * k;
	*hw = p->w * k * 0.5;
	*hh = *hw * (double)p->ph / (double)p->pw;
}

bool desk_mat_paper_place(int w, int h, const struct desk_mat_paper *p, double *cx, double *cy,
	double *hw, double *hh)
{
	if (!p || w < 8 || h < 8) {
		return false;
	}
	double r[4];
	mat_rect(w, h, DESK_MAT_FULL, r);
	paper_place_in(r, desk_px_per_pt(w), p, cx, cy, hw, hh);
	return true;
}

bool desk_mat_rect(int w, int h, double r[4])
{
	pthread_mutex_lock(&g_mat_lock);
	int kind = g_mat.kind;
	double size = mat_size_of(&g_mat);
	pthread_mutex_unlock(&g_mat_lock);
	if (kind == DESK_MAT_NONE || w < 8 || h < 8) {
		return false;
	}
	mat_rect(w, h, size, r);
	return true;
}

int desk_mat_at(int w, int h, double x, double y)
{
	pthread_mutex_lock(&g_mat_lock);
	int kind = g_mat.kind;
	double size = mat_size_of(&g_mat), t = g_unroll;
	pthread_mutex_unlock(&g_mat_lock);
	if (kind == DESK_MAT_NONE || w < 8 || h < 8) {
		return DESK_MAT_NONE;
	}
	double r[4];
	mat_rect(w, h, size, r);
	/* (one being unrolled lies from the back as far as it has come) */
	double far = kind == DESK_MAT_CLEAR ? r[3] : r[3] * t;
	return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + far ? kind : DESK_MAT_NONE;
}

int desk_mat_paper_at(int w, int h, double x, double y)
{
	struct desk_mat_paper p[DESK_MAT_PAPERS_MAX];
	int n = desk_mat_papers(p, DESK_MAT_PAPERS_MAX);
	for (int i = n - 1; i >= 0; i--) {
		double cx, cy, hw, hh;
		if (!desk_mat_paper_place(w, h, &p[i], &cx, &cy, &hw, &hh)) {
			return -1;
		}
		/* into the paper's own terms, turned back */
		double t = p[i].turn * M_PI / 180.0, dx = x - cx, dy = y - cy;
		double u = dx * cos(t) + dy * sin(t), v = -dx * sin(t) + dy * cos(t);
		if (fabs(u) <= hw && fabs(v) <= hh) {
			return i;
		}
	}
	return -1;
}

int desk_mat_papers(struct desk_mat_paper *out, int max)
{
	pthread_mutex_lock(&g_mat_lock);
	int n = g_mat.kind == DESK_MAT_CLEAR && g_mat.under == DESK_MAT_UNDER_FREE ? g_npapers : 0;
	n = n < max ? n : max;
	memcpy(out, g_papers, sizeof *out * (size_t)n);
	pthread_mutex_unlock(&g_mat_lock);
	return n;
}

/* The papers lie like this now, the prints already read kept for them. */
static void papers_put(const struct desk_mat_paper *p, int n)
{
	struct mat_print had[DESK_MAT_PAPERS_MAX];
	bool kept[DESK_MAT_PAPERS_MAX] = { false };
	n = n < 0 ? 0 : n > DESK_MAT_PAPERS_MAX ? DESK_MAT_PAPERS_MAX : n;
	pthread_mutex_lock(&g_mat_lock);
	int was = g_npapers;
	memcpy(had, g_prints, sizeof had);
	for (int i = 0; i < n; i++) {
		g_papers[i] = p[i];
		paper_tidy(&g_papers[i]);
		memset(&g_prints[i], 0, sizeof g_prints[i]);
		for (int j = 0; j < was; j++) {
			if (!kept[j] && !strcmp(had[j].name, p[i].name)) {
				g_prints[i] = had[j];
				kept[j] = true;
				break;
			}
		}
	}
	for (int j = 0; j < was; j++) {
		if (!kept[j] && had[j].pic) {
			cairo_surface_destroy(had[j].pic);
		}
	}
	for (int i = n; i < DESK_MAT_PAPERS_MAX; i++) {
		memset(&g_prints[i], 0, sizeof g_prints[i]);
	}
	g_npapers = n;
	if (g_mat_held >= n) {
		g_mat_held = -1;
	}
	g_mat_gen++;
	pthread_mutex_unlock(&g_mat_lock);
}

/* Written whole, to a file beside it and then over it, so that a desk-mat
 * reading it never sees half of it. */
static void papers_write(const struct desk_mat_paper *p, int n)
{
	char path[512], tmp[540];
	desk_mat_papers_file(path, sizeof path);
	if (!path[0]) {
		return;
	}
	char *slash = strrchr(path, '/');
	if (slash) {
		*slash = '\0';
		mkdir(path, 0700);
		*slash = '/';
	}
	snprintf(tmp, sizeof tmp, "%s.%d.tmp", path, (int)getpid());
	FILE *f = fopen(tmp, "we");
	if (!f) {
		fprintf(stderr, "desk: mat: cannot write %s: %s\n", tmp, strerror(errno));
		return;
	}
	fprintf(f, "# The pictures under the clear mat, the bottom one first (desk-mat, docs/CONFIGURATION.md):\n"
		"# paper = PRINT X Y WIDTH TURN -- its middle X, Y points from the mat's, WIDTH points\n"
		"# across, turned TURN degrees clockwise. The prints are in $XDG_DATA_HOME/desk/mat.\n");
	for (int i = 0; i < n; i++) {
		fprintf(f, "paper = %s %.1f %.1f %.1f %.1f\n", p[i].name, p[i].x, p[i].y, p[i].w, p[i].turn);
	}
	bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		fprintf(stderr, "desk: mat: cannot write %s: %s\n", path, strerror(errno));
		unlink(tmp);
		return;
	}
	struct stat st;
	if (stat(path, &st) == 0) {
		pthread_mutex_lock(&g_mat_lock);
		g_papers_st = st;
		g_papers_known = true;
		pthread_mutex_unlock(&g_mat_lock);
	}
}

void desk_mat_papers_set(const struct desk_mat_paper *p, int n, bool save)
{
	papers_put(p, n);
	if (save) {
		papers_write(p, n);
	}
}

bool desk_mat_papers_poll(void)
{
	char path[512];
	desk_mat_papers_file(path, sizeof path);
	if (!path[0]) {
		return false;
	}
	struct stat st;
	bool there = stat(path, &st) == 0 && S_ISREG(st.st_mode);
	pthread_mutex_lock(&g_mat_lock);
	bool known = g_papers_known, had = g_npapers > 0;
	bool same = known && there && st.st_mtim.tv_sec == g_papers_st.st_mtim.tv_sec
		&& st.st_mtim.tv_nsec == g_papers_st.st_mtim.tv_nsec && st.st_size == g_papers_st.st_size
		&& st.st_ino == g_papers_st.st_ino;
	if (!there) {
		memset(&g_papers_st, 0, sizeof g_papers_st);
	}
	g_papers_known = true;
	pthread_mutex_unlock(&g_mat_lock);
	if (same || (!there && known && !had) || (!there && !known)) {
		return false;
	}
	struct desk_mat_paper p[DESK_MAT_PAPERS_MAX] = { 0 };
	int n = 0;
	FILE *f = there ? fopen(path, "re") : NULL;
	if (f) {
		char line[512];
		while (n < DESK_MAT_PAPERS_MAX && fgets(line, sizeof line, f)) {
			struct desk_mat_paper q = { 0 };
			if (sscanf(line, " paper = %95s %lf %lf %lf %lf", q.name, &q.x, &q.y, &q.w, &q.turn) == 5
				&& paper_name_ok(q.name)) {
				paper_size_read(&q);
				paper_tidy(&q);
				p[n++] = q;
			}
		}
		fclose(f);
	}
	papers_put(p, n);
	pthread_mutex_lock(&g_mat_lock);
	if (there) {
		g_papers_st = st;
	}
	pthread_mutex_unlock(&g_mat_lock);
	return true;
}

/* The longer side a paper's print is wanted at, on the screen last painted:
 * a little more than it is seen at, so that a paper made a little larger
 * is not read again for it. */
static int paper_want_px(const struct desk_mat_paper *p)
{
	double side = p->w * fmax(1.0, (double)p->ph / (double)p->pw);
	int px = (int)ceil(side * (g_papers_k > 0.0 ? g_papers_k : 1.0) * 1.15);
	return px < 64 ? 64 : px > PAPER_READ_MAX ? PAPER_READ_MAX : px;
}

/* A paper whose print is not read, or was read much too small or too large
 * for how it is seen now: -1, none. */
static int papers_next(void)
{
	if (g_papers_k <= 0.0 || g_mat.kind != DESK_MAT_CLEAR || g_mat.under != DESK_MAT_UNDER_FREE) {
		return -1;
	}
	for (int i = 0; i < g_npapers; i++) {
		const struct mat_print *q = &g_prints[i];
		if (q->failed) {
			continue;
		}
		int want = paper_want_px(&g_papers[i]);
		int own = g_papers[i].pw > g_papers[i].ph ? g_papers[i].pw : g_papers[i].ph;
		int most = own > 8 && own < want ? own : want;
		if (!q->pic || q->px < most * 0.85 || (q->px > want * 2.5 && q->px > 256)) {
			return i;
		}
	}
	return -1;
}

/* s, made no larger than px on its longer side */
static cairo_surface_t *paper_shrunk(cairo_surface_t *s, int px)
{
	int pw = cairo_image_surface_get_width(s), ph = cairo_image_surface_get_height(s);
	if (pw <= px && ph <= px) {
		return s;
	}
	double k = (double)px / (pw > ph ? pw : ph);
	cairo_surface_t *d = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)ceil(pw * k),
		(int)ceil(ph * k));
	if (cairo_surface_status(d) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(d);
		return s;
	}
	cairo_t *cr = cairo_create(d);
	cairo_scale(cr, k, k);
	cairo_set_source_surface(cr, s, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(s);
	return d;
}

/* Reads the prints the papers want, one after another, until none does. */
static void *papers_thread(void *arg)
{
	(void)arg;
	for (;;) {
		char name[96], dir[400], path[512];
		pthread_mutex_lock(&g_mat_lock);
		int i = papers_next();
		if (i < 0) {
			g_papers_busy = false;
			pthread_mutex_unlock(&g_mat_lock);
			return NULL;
		}
		snprintf(name, sizeof name, "%s", g_papers[i].name);
		int want = paper_want_px(&g_papers[i]);
		pthread_mutex_unlock(&g_mat_lock);

		desk_mat_prints_dir(dir, sizeof dir);
		snprintf(path, sizeof path, "%s/%s", dir, name);
		/* the prints are desk-mat's PNGs, read as a photo is (mat_pic_read) */
		cairo_surface_t *s = dir[0] && paper_name_ok(name) ? mat_pic_read(path) : NULL;
		s = s ? paper_shrunk(s, want) : NULL;
		if (!s) {
			fprintf(stderr, "desk: mat: cannot read the print %s\n", path);
		}
		pthread_mutex_lock(&g_mat_lock);
		/* to every paper of that print, wherever the hand has put it meanwhile */
		for (int j = 0; j < g_npapers; j++) {
			struct mat_print *q = &g_prints[j];
			if (strcmp(g_papers[j].name, name)) {
				continue;
			}
			snprintf(q->name, sizeof q->name, "%s", name);
			if (q->pic) {
				cairo_surface_destroy(q->pic);
				q->pic = NULL;
			}
			if (s) {
				q->pic = cairo_surface_reference(s);
				int sw = cairo_image_surface_get_width(s), sh = cairo_image_surface_get_height(s);
				q->px = sw > sh ? sw : sh;
				if (g_papers[j].pw == 3 && g_papers[j].ph == 2) {
					g_papers[j].pw = sw;   /* a print whose head said nothing */
					g_papers[j].ph = sh;
				}
			} else {
				q->failed = true;
			}
		}
		g_mat_gen++;
		pthread_mutex_unlock(&g_mat_lock);
		if (s) {
			cairo_surface_destroy(s);
		}
	}
}

/* With g_mat_lock held: the thread started if a print wants reading. */
static bool papers_wake_locked(void)
{
	if (g_papers_busy || papers_next() < 0) {
		return false;
	}
	g_papers_busy = true;
	return true;
}

static void papers_start(void)
{
	pthread_t th;
	if (pthread_create(&th, NULL, papers_thread, NULL) == 0) {
		pthread_detach(th);
		return;
	}
	pthread_mutex_lock(&g_mat_lock);
	g_papers_busy = false;
	pthread_mutex_unlock(&g_mat_lock);
}

void desk_mat_lift(int corner, double cut, bool lifted, int held)
{
	corner = corner < 0 || corner > 3 ? -1 : corner;
	cut = corner < 0 || !isfinite(cut) ? 0.0 : clampf(cut, 0.0, 400.0);
	pthread_mutex_lock(&g_mat_lock);
	if (corner != g_mat_corner || cut != g_mat_cut || lifted != g_mat_lifted) {
		g_mat_corner = corner;
		g_mat_cut = cut;
		g_mat_lifted = lifted;
		g_sheet_gen++;
	}
	if (held != g_mat_held) {
		g_mat_held = held;
		g_mat_gen++;   /* what is under it, without what is in the hand */
	}
	pthread_mutex_unlock(&g_mat_lock);
}

bool desk_mat_clear(int *under)
{
	pthread_mutex_lock(&g_mat_lock);
	bool clear = g_mat.kind == DESK_MAT_CLEAR;
	if (under) {
		*under = g_mat.under;
	}
	pthread_mutex_unlock(&g_mat_lock);
	return clear;
}

/* A corner of the sheet r, as desk_mat_lift has them, and the ways along
 * its two edges from it: toward the corner before it, and after it, going
 * round clockwise on the screen. */
static void sheet_corner(const double r[4], int c, double *px, double *py, double a[2], double b[2])
{
	static const int CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
	int prev = (c + 3) % 4, next = (c + 1) % 4;
	*px = r[0] + r[2] * CX[c];
	*py = r[1] + r[3] * CY[c];
	a[0] = CX[prev] - CX[c];
	a[1] = CY[prev] - CY[c];
	b[0] = CX[next] - CX[c];
	b[1] = CY[next] - CY[c];
}

int desk_mat_corner_at(int w, int h, double x, double y, double reach)
{
	if (!desk_mat_clear(NULL) || w < 8 || h < 8) {
		return -1;
	}
	double r[4], k = desk_px_per_pt(w);
	mat_rect(w, h, DESK_MAT_FULL, r);
	for (int c = 0; c < 4; c++) {
		double px, py, a[2], b[2];
		sheet_corner(r, c, &px, &py, a, b);
		/* on the sheet, within reach of its corner along both edges */
		double u = (x - px) * a[0] + (y - py) * a[1], v = (x - px) * b[0] + (y - py) * b[1];
		if (u >= -4.0 * k && v >= -4.0 * k && u + v <= reach * k) {
			return c;
		}
	}
	return -1;
}

/* The prints read now, on this thread, for a screen w across: for a picture
 * of the desk that is taken once (desk-preview), not for the compositor. */
void desk_mat_settle(int w)
{
	pthread_mutex_lock(&g_mat_lock);
	g_papers_k = desk_px_per_pt(w);
	bool start = papers_wake_locked();
	pthread_mutex_unlock(&g_mat_lock);
	if (start) {
		papers_thread(NULL);
	}
	for (int i = 0; i < 400; i++) {
		pthread_mutex_lock(&g_mat_lock);
		bool busy = g_papers_busy;
		pthread_mutex_unlock(&g_mat_lock);
		if (!busy) {
			break;
		}
		nanosleep(&(struct timespec){ 0, 20000000 }, NULL);
	}
}

/* One print as it lies under the sheet: its shadow from the lamp, as far off
 * the desk as it is (pressed flat, or held), the white of the paper, and the
 * picture in it. In screen terms; not lit, the caller lights it. */
static void paper_paint(cairo_t *cr, double k, const struct desk_mat_paper *p, cairo_surface_t *pic,
	double cx, double cy, double hw, double hh, double lift)
{
	double t = p->turn * M_PI / 180.0, sx, sy;
	shadow_dir(cx, cy, lift, &sx, &sy);
	/* the shadow: tight while it lies flat, soft and far once it is lifted */
	int layers = lift > 4.0 * k ? 8 : 3;
	for (int i = layers; i >= 1; i--) {
		double g = (0.35 * k + lift * 0.45) * i / layers;
		cairo_save(cr);
		cairo_translate(cr, cx + sx, cy + sy);
		cairo_rotate(cr, t);
		desk_rounded_path(cr, -hw - g, -hh - g, 2 * (hw + g), 2 * (hh + g), 1.0 + g);
		cairo_set_source_rgba(cr, 0.03, 0.02, 0.01, lift > 4.0 * k ? 0.045 : 0.10);
		cairo_fill(cr);
		cairo_restore(cr);
	}
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, t);
	/* the print: a little warm, its edge a cut edge */
	cairo_rectangle(cr, -hw, -hh, 2 * hw, 2 * hh);
	cairo_set_source_rgb(cr, 0.955, 0.945, 0.915);
	cairo_fill(cr);
	double b = PAPER_BORDER * 2.0 * fmin(hw, hh);
	double iw = 2 * (hw - b), ih = 2 * (hh - b);
	if (pic && iw > 1.0 && ih > 1.0) {
		double pw = cairo_image_surface_get_width(pic), ph = cairo_image_surface_get_height(pic);
		double s = fmax(iw / pw, ih / ph);
		cairo_save(cr);
		cairo_rectangle(cr, -hw + b, -hh + b, iw, ih);
		cairo_clip(cr);
		cairo_translate(cr, -pw * s * 0.5, -ph * s * 0.5);
		cairo_scale(cr, s, s);
		cairo_set_source_surface(cr, pic, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);
	}
	/* the paper's own tooth, over the white and the picture alike */
	overlay_grain(cr, -hw, -hh, 2 * hw, 2 * hh, 0.55);
	cairo_rectangle(cr, -hw + 0.5, -hh + 0.5, 2 * hw - 1.0, 2 * hh - 1.0);
	cairo_set_source_rgba(cr, 0.35, 0.30, 0.24, 0.30);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* The lamp of the room on what was laid in under, which has alpha where it
 * is: the boards beside it are lit already, in the room. */
static void mat_light_over(cairo_t *cr, int w, int h, cairo_pattern_t *under)
{
	cairo_surface_t *lit = mat_light(w, h);
	if (!lit) {
		return;
	}
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
	cairo_set_source_surface(cr, lit, 0, 0);
	cairo_pattern_t *lp = cairo_get_source(cr);
	cairo_pattern_set_filter(lp, CAIRO_FILTER_BILINEAR);
	cairo_pattern_set_extend(lp, CAIRO_EXTEND_PAD);
	cairo_matrix_t mt;
	cairo_matrix_init_scale(&mt, (double)MAT_LIGHT_W / w, (double)MAT_LIGHT_H / h);
	cairo_pattern_set_matrix(lp, &mt);
	cairo_mask(cr, under);
	cairo_restore(cr);
}

/* top laid over bottom, both colours with alpha (not premultiplied): what
 * one paint of the two together is */
static void rgba_over(const double top[4], const double bottom[4], double out[4])
{
	double a = top[3] + bottom[3] * (1.0 - top[3]);
	for (int i = 0; i < 3; i++) {
		out[i] = a > 1e-9 ? (top[i] * top[3] + bottom[i] * bottom[3] * (1.0 - top[3])) / a : 0.0;
	}
	out[3] = a;
}

/* The sheet's outline, its corners rounded, but corner c (or none, -1) cut
 * off along the fold of it turned up, cut pixels along each edge. */
static void sheet_path(cairo_t *cr, const double r[4], double rad, int c, double cut)
{
	cut = c < 0 ? 0.0 : fmin(cut, fmin(r[2], r[3]) * 0.5);
	cairo_new_sub_path(cr);
	for (int i = 0; i < 4; i++) {
		double px, py, a[2], b[2];
		sheet_corner(r, i, &px, &py, a, b);
		if (i == c && cut > rad) {
			cairo_line_to(cr, px + a[0] * cut, py + a[1] * cut);
			cairo_line_to(cr, px + b[0] * cut, py + b[1] * cut);
		} else {
			cairo_line_to(cr, px + a[0] * rad, py + a[1] * rad);
			cairo_curve_to(cr, px + a[0] * rad * 0.45, py + a[1] * rad * 0.45,
				px + b[0] * rad * 0.45, py + b[1] * rad * 0.45, px + b[0] * rad, py + b[1] * rad);
		}
	}
	cairo_close_path(cr);
}

/* The sheet over what is under it: it, its edge and the lamp in it; turned
 * up at corner c by cut pixels (or not, -1). */
static void clear_sheet(cairo_t *cr, int w, int h, const double r[4], double k, bool lifted, int c,
	double cut)
{
	double x = r[0], y = r[1], mw = r[2], mh = r[3], rad = CLEAR_RADIUS_PT * k;
	double lift = (lifted ? CLEAR_LIFTED_PT : CLEAR_LIFT_PT) * k;

	/* Only its edge throws a shadow: a line of it, leaning away from the lamp,
	 * soft and further off with the sheet lifted. */
	double lx, rx, ty, by, along;
	shadow_dir(x, y + mh * 0.5, lift, &lx, &along);
	shadow_dir(x + mw, y + mh * 0.5, lift, &rx, &along);
	shadow_dir(x + mw * 0.5, y, lift, &along, &ty);
	shadow_dir(x + mw * 0.5, y + mh, lift, &along, &by);
	int soft = lifted ? 6 : 2;
	for (int i = soft; i >= 1; i--) {
		double g = (lifted ? 2.6 : 0.8) * k * i / soft;
		double sr[4] = { x + lx - g * 0.5, y + ty - g * 0.5, mw + rx - lx + g, mh + by - ty + g };
		sheet_path(cr, sr, rad + g, c, cut);
		cairo_set_source_rgba(cr, 0.02, 0.015, 0.008, lifted ? 0.035 : 0.10);
		cairo_set_line_width(cr, fmax(1.0, (lifted ? 2.4 : 1.4) * k));
		cairo_stroke(cr);
	}

	cairo_save(cr);
	sheet_path(cr, r, rad, c, cut);
	cairo_clip(cr);
	/* Through it everything is a shade darker, a little toward the green of
	 * the edge of glass, and a little less sharp for the room it shows back,
	 * lighter toward the lamp; less so held up off it. One paint of it all. */
	double ex = w * 0.5, ey = h * 0.5;
	double big = fmax(mw, mh), a = lifted ? 1.2 : 1.0;
	double tint[4], near_lamp[4];
	const double dark[4] = { 0.02, 0.05, 0.045, lifted ? 0.04 : 0.07 };
	const double haze[4] = { 0.80, 0.86, 0.85, lifted ? 0.015 : 0.03 };
	const double room[4] = { 1.0, 0.97, 0.90, 0.07 * a };
	rgba_over(haze, dark, tint);
	rgba_over(room, tint, near_lamp);
	double tx0 = 0.0, ty0 = 0.0, tz0 = 0.0;
	lamp_toward(x + mw * 0.5, y + mh * 0.5, &tx0, &ty0, &tz0);
	double hn0 = hypot(tx0, ty0);
	double ux = hn0 > 1e-6 ? tx0 / hn0 : 0.6, uy = hn0 > 1e-6 ? ty0 / hn0 : 0.8;
	double R0 = 0.5 * hypot(mw, mh);
	cairo_pattern_t *veil = cairo_pattern_create_linear(x + mw * 0.5 + ux * R0, y + mh * 0.5 + uy * R0,
		x + mw * 0.5 - ux * R0, y + mh * 0.5 - uy * R0);
	cairo_pattern_add_color_stop_rgba(veil, 0.0, near_lamp[0], near_lamp[1], near_lamp[2], near_lamp[3]);
	cairo_pattern_add_color_stop_rgba(veil, 0.6, tint[0], tint[1], tint[2], tint[3]);
	cairo_set_source(cr, veil);
	cairo_paint(cr);
	cairo_pattern_destroy(veil);
	/* and the lamp itself in it, seen where the sheet lies between the eye,
	 * over the middle of the screen, and the lamp's image under the desk:
	 * most of the way toward the point under the lamp. Its shade a bright
	 * patch, softened by the vinyl's own faint grain, in a wider glow. */
	double px = ex + (g_light.x - g_paint_ox - ex) * 0.74, py = ey + (g_light.y - g_paint_oy - ey) * 0.74;
	cairo_save(cr);
	cairo_translate(cr, px, py);
	cairo_scale(cr, 1.0, 0.78);
	cairo_rectangle(cr, -big * 0.34, -big * 0.34, big * 0.68, big * 0.68);
	cairo_pattern_t *glow = cairo_pattern_create_radial(0, 0, 0, 0, 0, big * 0.34);
	cairo_pattern_add_color_stop_rgba(glow, 0.00, 1.0, 0.98, 0.92, 0.40 * a);
	cairo_pattern_add_color_stop_rgba(glow, 0.13, 1.0, 0.97, 0.90, 0.33 * a);
	cairo_pattern_add_color_stop_rgba(glow, 0.21, 1.0, 0.96, 0.88, 0.10 * a);
	cairo_pattern_add_color_stop_rgba(glow, 0.50, 1.0, 0.95, 0.86, 0.025 * a);
	cairo_pattern_add_color_stop_rgba(glow, 1.00, 1.0, 0.95, 0.86, 0.0);
	cairo_set_source(cr, glow);
	cairo_fill(cr);
	cairo_pattern_destroy(glow);
	cairo_restore(cr);
	/* the fine scratches a sheet gets, seen only in the lamp: the same each
	 * time, from the size of the desk */
	uint32_t seed = 0x9e3779b9u ^ (uint32_t)(mw * 7 + mh * 13);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int i = 0; i < 46; i++) {
		seed = seed * 1664525u + 1013904223u;
		double sx0 = x + (seed >> 8) % 10000 / 10000.0 * mw;
		seed = seed * 1664525u + 1013904223u;
		double sy0 = y + (seed >> 8) % 10000 / 10000.0 * mh;
		seed = seed * 1664525u + 1013904223u;
		double ang = (seed >> 8) % 10000 / 10000.0 * M_PI;
		seed = seed * 1664525u + 1013904223u;
		double len = (6.0 + (seed >> 8) % 10000 / 10000.0 * 46.0) * k;
		double d = hypot(sx0 - px, (sy0 - py) / 0.78) / (big * 0.30);
		double near = clampf(1.0 - d, 0.0, 1.0);
		if (near <= 0.02) {
			continue;
		}
		cairo_move_to(cr, sx0, sy0);
		cairo_line_to(cr, sx0 + cos(ang) * len, sy0 + sin(ang) * len);
		cairo_set_source_rgba(cr, 1.0, 0.98, 0.92, 0.22 * near * near);
		cairo_set_line_width(cr, fmax(0.6, 0.55 * k));
		cairo_stroke(cr);
	}
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	/* its edge, polished and as thick as it is: a band just in from the edge
	 * where what is under it is bent and darker, light along the side facing
	 * the lamp and dark along the other */
	double band = CLEAR_EDGE_PT * k;
	sheet_path(cr, r, rad, c, cut);
	cairo_set_source_rgba(cr, 0.04, 0.07, 0.065, 0.16);
	cairo_set_line_width(cr, band * 2.0);
	cairo_stroke(cr);
	cairo_restore(cr);

	double tx = 0.0, tyy = 0.0, tz = 0.0;
	lamp_toward(x + mw * 0.5, y + mh * 0.5, &tx, &tyy, &tz);
	double hn = hypot(tx, tyy);
	double dxl = hn > 1e-6 ? tx / hn : -0.6, dyl = hn > 1e-6 ? tyy / hn : -0.8;
	double R = 0.5 * hypot(mw, mh);
	cairo_pattern_t *edge = cairo_pattern_create_linear(x + mw * 0.5 + dxl * R, y + mh * 0.5 + dyl * R,
		x + mw * 0.5 - dxl * R, y + mh * 0.5 - dyl * R);
	cairo_pattern_add_color_stop_rgba(edge, 0.0, 1.0, 0.98, 0.92, lifted ? 0.85 : 0.70);
	cairo_pattern_add_color_stop_rgba(edge, 0.5, 1.0, 0.98, 0.92, 0.22);
	cairo_pattern_add_color_stop_rgba(edge, 1.0, 0.0, 0.0, 0.0, 0.35);
	double er[4] = { x + 0.75, y + 0.75, mw - 1.5, mh - 1.5 };
	sheet_path(cr, er, rad, c, cut);
	cairo_set_source(cr, edge);
	cairo_set_line_width(cr, fmax(1.5, 1.4 * k));
	cairo_stroke(cr);
	cairo_pattern_destroy(edge);
}

/* What is under the clear sheet (DESK_OVL_MAT): the prints, or the one
 * picture all over it. The sheet is a sprite of its own over it
 * (DESK_OVL_MAT_SHEET), so that moving a print does not draw the sheet, nor
 * lifting the sheet what is under it. */
static void clear_under_paint(cairo_t *cr, int w, int h, const struct desk_mat *m, cairo_surface_t *pic)
{
	double r[4], k = desk_px_per_pt(w);
	mat_rect(w, h, DESK_MAT_FULL, r);
	struct desk_mat_paper papers[DESK_MAT_PAPERS_MAX];
	cairo_surface_t *pics[DESK_MAT_PAPERS_MAX];
	int n = 0;
	pthread_mutex_lock(&g_mat_lock);
	g_papers_k = k;
	int held = g_mat_held;
	if (m->under == DESK_MAT_UNDER_FREE) {
		n = g_npapers;
		for (int i = 0; i < n; i++) {
			papers[i] = g_papers[i];
			pics[i] = g_prints[i].pic ? cairo_surface_reference(g_prints[i].pic) : NULL;
		}
	}
	bool start = papers_wake_locked();
	pthread_mutex_unlock(&g_mat_lock);
	if (start) {
		papers_start();
	}

	/* what is under the sheet, lit where it lies */
	double rad = CLEAR_RADIUS_PT * k;
	cairo_save(cr);
	desk_rounded_path(cr, r[0], r[1], r[2], r[3], rad);
	cairo_clip(cr);
	cairo_push_group(cr);
	if (pic) {
		/* one picture all over it, cut to fill it from the middle */
		double pw = cairo_image_surface_get_width(pic), ph = cairo_image_surface_get_height(pic);
		double s = fmax(r[2] / pw, r[3] / ph);
		cairo_save(cr);
		cairo_translate(cr, r[0] + (r[2] - pw * s) * 0.5, r[1] + (r[3] - ph * s) * 0.5);
		cairo_scale(cr, s, s);
		cairo_set_source_surface(cr, pic, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
		cairo_paint(cr);
		cairo_restore(cr);
	}
	for (int i = 0; i < n; i++) {
		if (i == held || !pics[i]) {
			continue;   /* in the hand; or not read yet, and laid when it is */
		}
		double cx, cy, hw, hh;
		paper_place_in(r, k, &papers[i], &cx, &cy, &hw, &hh);
		paper_paint(cr, k, &papers[i], pics[i], cx, cy, hw, hh, PAPER_LIFT_PT * k);
	}
	cairo_pattern_t *under = cairo_pop_group(cr);
	cairo_set_source(cr, under);
	cairo_paint(cr);
	mat_light_over(cr, w, h, under);
	cairo_pattern_destroy(under);
	cairo_restore(cr);
	for (int i = 0; i < n; i++) {
		if (pics[i]) {
			cairo_surface_destroy(pics[i]);
		}
	}
}

/* The clear sheet by itself (DESK_OVL_MAT_SHEET), as it lies or is lifted. */
static void clear_sheet_paint(cairo_t *cr, int w, int h)
{
	pthread_mutex_lock(&g_mat_lock);
	bool clear = g_mat.kind == DESK_MAT_CLEAR, lifted = g_mat_lifted;
	int corner = g_mat_corner;
	double cut = g_mat_cut;
	pthread_mutex_unlock(&g_mat_lock);
	if (!clear) {
		return;
	}
	double r[4], k = desk_px_per_pt(w);
	mat_rect(w, h, DESK_MAT_FULL, r);
	clear_sheet(cr, w, h, r, k, lifted, corner, cut * k);
}

static uint64_t sheet_key(void)
{
	pthread_mutex_lock(&g_mat_lock);
	uint64_t k = g_sheet_gen;
	pthread_mutex_unlock(&g_mat_lock);
	return k;
}

/* Where the sheet draws (x, y, w, h): all of it and its shadow; nothing,
 * a box of nothing, when there is no clear mat. */
static bool sheet_box(int w, int h, double box[4])
{
	box[0] = box[1] = box[2] = box[3] = 0.0;
	if (!desk_mat_clear(NULL)) {
		return true;
	}
	double r[4], k = desk_px_per_pt(w), pad = (CLEAR_LIFTED_PT * 1.6 + 8.0) * k;
	mat_rect(w, h, DESK_MAT_FULL, r);
	box[0] = floor(r[0] - pad);
	box[1] = floor(r[1] - pad);
	box[2] = ceil(r[0] + r[2] + pad) - box[0];
	box[3] = ceil(r[1] + r[3] + pad) - box[1];
	return true;
}

/* Where what is under a clear mat draws: the picture all over it, all of
 * it; the prints, from the first to the last of them and their shadows;
 * none, a box of nothing. */
static void under_box(int w, int h, const struct desk_mat *m, double box[4])
{
	double r[4], k = desk_px_per_pt(w);
	mat_rect(w, h, DESK_MAT_FULL, r);
	box[0] = box[1] = box[2] = box[3] = 0.0;
	if (m->under == DESK_MAT_UNDER_WHOLE) {
		if (m->photo[0]) {
			memcpy(box, r, sizeof r);
		}
		return;
	}
	double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
	pthread_mutex_lock(&g_mat_lock);
	for (int i = 0; i < g_npapers; i++) {
		if (i == g_mat_held) {
			continue;
		}
		double cx, cy, hw, hh, t = g_papers[i].turn * M_PI / 180.0;
		paper_place_in(r, k, &g_papers[i], &cx, &cy, &hw, &hh);
		double ex = hw * fabs(cos(t)) + hh * fabs(sin(t)), ey = hw * fabs(sin(t)) + hh * fabs(cos(t));
		double pad = 4.0 * k + 2.0;
		x0 = fmin(x0, cx - ex - pad);
		y0 = fmin(y0, cy - ey - pad);
		x1 = fmax(x1, cx + ex + pad);
		y1 = fmax(y1, cy + ey + pad);
	}
	pthread_mutex_unlock(&g_mat_lock);
	x0 = fmax(x0, r[0]);
	y0 = fmax(y0, r[1]);
	x1 = fmin(x1, r[0] + r[2]);
	y1 = fmin(y1, r[1] + r[3]);
	if (x1 > x0 && y1 > y0) {
		box[0] = floor(x0);
		box[1] = floor(y0);
		box[2] = ceil(x1) - box[0];
		box[3] = ceil(y1) - box[1];
	}
}

cairo_surface_t *desk_mat_paper_picture(int w, int h, const struct desk_mat_paper *p, double lift,
	int *ox, int *oy)
{
	if (!p || w < 8 || h < 8) {
		return NULL;
	}
	cairo_surface_t *pic = NULL;
	pthread_mutex_lock(&g_mat_lock);
	for (int i = 0; i < g_npapers && !pic; i++) {
		if (g_prints[i].pic && !strcmp(g_papers[i].name, p->name)) {
			pic = cairo_surface_reference(g_prints[i].pic);
		}
	}
	pthread_mutex_unlock(&g_mat_lock);
	if (!pic) {
		return NULL;
	}
	double r[4], k = desk_px_per_pt(w), cx, cy, hw, hh;
	mat_rect(w, h, DESK_MAT_FULL, r);
	paper_place_in(r, k, p, &cx, &cy, &hw, &hh);
	lift *= k;
	/* as far round it as it can reach turned, and its shadow past that */
	double reach = hypot(hw, hh), pad = lift * 1.6 + 8.0 * k;
	int x0 = (int)floor(cx - reach - pad), y0 = (int)floor(cy - reach - pad);
	int sw = (int)ceil(2 * (reach + pad)) + 2, sh = sw;
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
	cairo_t *cr = cairo_create(s);
	cairo_translate(cr, -x0, -y0);
	cairo_push_group(cr);
	paper_paint(cr, k, p, pic, cx, cy, hw, hh, lift);
	cairo_pattern_t *it = cairo_pop_group(cr);
	cairo_set_source(cr, it);
	cairo_paint(cr);
	mat_light_over(cr, w, h, it);
	cairo_pattern_destroy(it);
	cairo_destroy(cr);
	cairo_surface_destroy(pic);
	*ox = x0;
	*oy = y0;
	return s;
}

/* ---- the corner turned up, and the envelope of prints -------------------
 *
 * The clear mat is lifted by a corner, as one is: the hand turns it up, and
 * holds it up while what is under it is moved. The corner is its own
 * picture over the sheet, which leaves that corner out of its own (a
 * clear sheet's corner turned over shows the desk where it was). By it lies
 * the envelope the prints came in: more are taken out of it, and one put
 * back in it is gone from under the mat. */

#define ENVELOPE_W_PT 200.0
#define ENVELOPE_H_PT 138.0

/* A corner raised a little off the desk, where it lies: not turned over,
 * only stood up at its tip, h pixels, and coming down to lie flat at reach
 * pixels along each edge -- its edges catching the light, and the shadow
 * they throw. Nothing of the sheet has to be drawn again for it. */
static cairo_surface_t *corner_raised(double px, double py, const double a[2], const double b[2],
	double reach, double h, double k, int *ox, int *oy)
{
	double ax = px + a[0] * reach, ay = py + a[1] * reach, bx = px + b[0] * reach, by = py + b[1] * reach;
	double sx, sy;
	shadow_dir(px, py, h, &sx, &sy);
	double pad = 10.0 * k;
	double x0 = floor(fmin(fmin(ax, bx), fmin(px, px + sx)) - pad);
	double y0 = floor(fmin(fmin(ay, by), fmin(py, py + sy)) - pad);
	double x1 = ceil(fmax(fmax(ax, bx), fmax(px, px + sx)) + pad);
	double y1 = ceil(fmax(fmax(ay, by), fmax(py, py + sy)) + pad);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)(x1 - x0), (int)(y1 - y0));
	cairo_t *cr = cairo_create(s);
	cairo_translate(cr, -x0, -y0);
	/* the shadow: between the edges where they lie and where their shadow
	 * falls, nothing at the far ends, most at the corner */
	double sn = fmax(hypot(sx, sy), 1.0);
	for (int i = 5; i >= 1; i--) {
		double g = 0.9 * k * i;
		cairo_move_to(cr, ax, ay);
		cairo_line_to(cr, px, py);
		cairo_line_to(cr, bx, by);
		cairo_line_to(cr, px + sx * (1.0 + g / sn), py + sy * (1.0 + g / sn));
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0.02, 0.015, 0.008, 0.075);
		cairo_fill(cr);
	}
	/* the edges, up off the desk: in the light, most where they are highest */
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int e = 0; e < 2; e++) {
		double ex = e ? bx : ax, ey = e ? by : ay;
		cairo_pattern_t *lit = cairo_pattern_create_linear(ex, ey, px, py);
		cairo_pattern_add_color_stop_rgba(lit, 0.0, 1.0, 0.98, 0.92, 0.0);
		cairo_pattern_add_color_stop_rgba(lit, 1.0, 1.0, 0.98, 0.92, 0.95);
		cairo_move_to(cr, ex, ey);
		cairo_line_to(cr, px, py);
		cairo_set_source(cr, lit);
		cairo_set_line_width(cr, fmax(1.6, 2.2 * k));
		cairo_stroke(cr);
		cairo_pattern_destroy(lit);
	}
	cairo_destroy(cr);
	*ox = (int)x0;
	*oy = (int)y0;
	return s;
}

cairo_surface_t *desk_mat_curl_picture(int w, int h, int corner, double cut, bool folded, int *ox,
	int *oy)
{
	if (corner < 0 || corner > 3 || w < 8 || h < 8 || !(cut > 0.0)) {
		return NULL;
	}
	double r[4], k = desk_px_per_pt(w);
	mat_rect(w, h, DESK_MAT_FULL, r);
	double c = fmin(cut * k, fmin(r[2], r[3]) * 0.5);
	double px, py, a[2], b[2];
	sheet_corner(r, corner, &px, &py, a, b);
	if (!folded) {
		return corner_raised(px, py, a, b, c * 2.8, c * 0.75, k, ox, oy);
	}
	/* The fold runs from A on one edge to B on the other. Turned over it, the
	 * corner would lie flat at T over the sheet; held up, it stands off the
	 * sheet and is seen short of that, its tip highest. */
	double ax = px + a[0] * c, ay = py + a[1] * c, bx = px + b[0] * c, by = py + b[1] * c;
	double mx = (ax + bx) * 0.5, my = (ay + by) * 0.5;
	double tx = mx + (ax + bx - px - mx) * 0.80, ty = my + (ay + by - py - my) * 0.80;
	double sx, sy;
	shadow_dir(tx, ty, c * 0.42, &sx, &sy);
	double pad = 14.0 * k;
	double x0 = floor(fmin(fmin(ax, bx), fmin(fmin(tx, tx + sx), px)) - pad);
	double y0 = floor(fmin(fmin(ay, by), fmin(fmin(ty, ty + sy), py)) - pad);
	double x1 = ceil(fmax(fmax(ax, bx), fmax(fmax(tx, tx + sx), px)) + pad);
	double y1 = ceil(fmax(fmax(ay, by), fmax(fmax(ty, ty + sy), py)) + pad);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)(x1 - x0), (int)(y1 - y0));
	cairo_t *cr = cairo_create(s);
	cairo_translate(cr, -x0, -y0);
	/* the bend of it bows out between the fold and the tip */
	double bow = c * 0.10;
	double nx = tx - mx, ny = ty - my, nn = hypot(nx, ny);
	nx = nn > 1e-6 ? nx / nn : 0.0;
	ny = nn > 1e-6 ? ny / nn : 0.0;

	/* Its shadow on what is under it: along the fold, where it leaves the
	 * sheet, and further out to its tip. */
	for (int i = 6; i >= 1; i--) {
		double g = 1.6 * k * i;
		cairo_move_to(cr, ax, ay);
		cairo_line_to(cr, tx + sx + nx * g, ty + sy + ny * g);
		cairo_line_to(cr, bx, by);
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0.02, 0.015, 0.008, 0.035);
		cairo_fill(cr);
	}

	/* The corner itself: clear, so only where it bends toward the lamp, and
	 * its cut edges, are seen; a little haze between. */
	cairo_move_to(cr, ax, ay);
	cairo_curve_to(cr, ax + (tx - ax) * 0.5 + nx * bow, ay + (ty - ay) * 0.5 + ny * bow,
		tx - nx * bow * 0.3, ty - ny * bow * 0.3, tx, ty);
	cairo_curve_to(cr, tx - nx * bow * 0.3, ty - ny * bow * 0.3,
		bx + (tx - bx) * 0.5 + nx * bow, by + (ty - by) * 0.5 + ny * bow, bx, by);
	cairo_close_path(cr);
	cairo_pattern_t *haze = cairo_pattern_create_linear(mx, my, tx, ty);
	cairo_pattern_add_color_stop_rgba(haze, 0.0, 1.0, 0.98, 0.93, 0.30);
	cairo_pattern_add_color_stop_rgba(haze, 0.25, 1.0, 0.98, 0.93, 0.10);
	cairo_pattern_add_color_stop_rgba(haze, 1.0, 0.85, 0.92, 0.90, 0.16);
	cairo_set_source(cr, haze);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(haze);
	cairo_set_source_rgba(cr, 1.0, 0.98, 0.92, 0.70);
	cairo_set_line_width(cr, fmax(1.2, 1.3 * k));
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_stroke(cr);
	/* the fold: the bend catches the lamp in a line along it, dark beside */
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, ax + nx * 1.5 * k, ay + ny * 1.5 * k);
	cairo_line_to(cr, bx + nx * 1.5 * k, by + ny * 1.5 * k);
	cairo_set_source_rgba(cr, 1.0, 0.98, 0.92, 0.55);
	cairo_set_line_width(cr, 2.6 * k);
	cairo_stroke(cr);
	cairo_move_to(cr, ax - nx * 0.8 * k, ay - ny * 0.8 * k);
	cairo_line_to(cr, bx - nx * 0.8 * k, by - ny * 0.8 * k);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.16);
	cairo_set_line_width(cr, 1.0 * k);
	cairo_stroke(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	cairo_destroy(cr);
	*ox = (int)x0;
	*oy = (int)y0;
	return s;
}

/* Where the envelope lies, by the mat lifted at corner c, turned up cut
 * pixels: its middle, its size and how it is turned. Past the fold, along
 * the edge across the desk from the corner. */
static void envelope_place(const double r[4], double k, int c, double cut, double *cx, double *cy,
	double *ew, double *eh, double *turn)
{
	double px, py, a[2], b[2];
	sheet_corner(r, c, &px, &py, a, b);
	*ew = ENVELOPE_W_PT * k;
	*eh = ENVELOPE_H_PT * k;
	double hx = c == 0 || c == 3 ? 1.0 : -1.0, vy = c == 0 || c == 1 ? 1.0 : -1.0;
	*cx = px + hx * (cut + 30.0 * k + *ew * 0.5);
	*cy = py + vy * (26.0 * k + *eh * 0.5);
	*turn = (c == 0 || c == 2 ? -4.0 : 4.0) * M_PI / 180.0;
}

bool desk_mat_envelope_box(int w, int h, int corner, double cut, double box[4])
{
	if (corner < 0 || corner > 3 || w < 8 || h < 8) {
		return false;
	}
	double r[4], k = desk_px_per_pt(w), cx, cy, ew, eh, t;
	mat_rect(w, h, DESK_MAT_FULL, r);
	envelope_place(r, k, corner, cut * k, &cx, &cy, &ew, &eh, &t);
	double hw = 0.5 * (ew * fabs(cos(t)) + eh * fabs(sin(t)));
	double hh = 0.5 * (ew * fabs(sin(t)) + eh * fabs(cos(t)));
	box[0] = cx - hw;
	box[1] = cy - hh;
	box[2] = 2 * hw;
	box[3] = 2 * hh;
	return true;
}

/* A photographer's envelope, of brown paper, the prints in it; open, its
 * flap turned up behind and the prints' tops out of its mouth. */
static void envelope_paint(cairo_t *cr, double cx, double cy, double ew, double eh, double turn,
	bool open, double k)
{
	double sx, sy;
	shadow_dir(cx, cy, 1.6 * k, &sx, &sy);
	cairo_save(cr);
	cairo_translate(cr, cx + sx, cy + sy);
	cairo_rotate(cr, turn);
	for (int i = 3; i >= 1; i--) {
		double g = 0.9 * k * i;
		desk_rounded_path(cr, -ew * 0.5 - g, -eh * 0.5 - g, ew + 2 * g, eh + 2 * g, 3.0 * k + g);
		cairo_set_source_rgba(cr, 0.03, 0.02, 0.01, 0.08);
		cairo_fill(cr);
	}
	cairo_restore(cr);

	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, turn);
	double x = -ew * 0.5, y = -eh * 0.5;
	if (open) {
		/* three prints, a little fanned, their tops out of it */
		static const double INK[3][3] = { { 0.36, 0.52, 0.66 }, { 0.64, 0.50, 0.32 },
			{ 0.40, 0.57, 0.37 } };
		for (int i = 0; i < 3; i++) {
			double pw = ew * 0.82, ph = eh * 0.74;
			cairo_save(cr);
			cairo_translate(cr, (i - 1) * 3.0 * k, y + ph * 0.30 - i * 2.5 * k);
			cairo_rotate(cr, (i - 1) * 0.05);
			cairo_rectangle(cr, -pw * 0.5, -ph * 0.5, pw, ph);
			cairo_set_source_rgb(cr, 0.955, 0.945, 0.915);
			cairo_fill_preserve(cr);
			cairo_set_source_rgba(cr, 0.35, 0.30, 0.24, 0.35);
			cairo_set_line_width(cr, 1.0);
			cairo_stroke(cr);
			double b = ph * 0.06;
			cairo_rectangle(cr, -pw * 0.5 + b, -ph * 0.5 + b, pw - 2 * b, ph - 2 * b);
			cairo_set_source_rgb(cr, INK[i][0], INK[i][1], INK[i][2]);
			cairo_fill(cr);
			cairo_restore(cr);
		}
		/* the flap, turned up behind the mouth: its inside, paler */
		cairo_move_to(cr, x + 2.0 * k, y);
		cairo_line_to(cr, x + ew - 2.0 * k, y);
		cairo_line_to(cr, x + ew * 0.5, y - eh * 0.30);
		cairo_close_path(cr);
		cairo_set_source_rgb(cr, 0.85, 0.74, 0.55);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.30, 0.20, 0.10, 0.25);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	}
	/* the paper of it */
	desk_rounded_path(cr, x, y, ew, eh, 2.5 * k);
	cairo_set_source_rgb(cr, 0.79, 0.65, 0.46);
	cairo_fill(cr);
	overlay_grain(cr, x, y, ew, eh, 1.1);
	/* where its sides are folded in and glued, from its foot */
	cairo_move_to(cr, x + 2.0 * k, y + eh - 2.0 * k);
	cairo_line_to(cr, x + ew * 0.5, y + eh * 0.52);
	cairo_line_to(cr, x + ew - 2.0 * k, y + eh - 2.0 * k);
	cairo_set_source_rgba(cr, 0.25, 0.16, 0.08, 0.16);
	cairo_set_line_width(cr, 1.2 * k);
	cairo_stroke(cr);
	if (!open) {
		/* the flap, shut down over its mouth */
		cairo_move_to(cr, x, y + 1.0 * k);
		cairo_line_to(cr, x + ew, y + 1.0 * k);
		cairo_line_to(cr, x + ew * 0.5, y + eh * 0.42);
		cairo_close_path(cr);
		cairo_set_source_rgb(cr, 0.75, 0.61, 0.42);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.25, 0.16, 0.08, 0.30);
		cairo_set_line_width(cr, 1.0 * k);
		cairo_stroke(cr);
	}
	/* and on it, in the laboratory's red, what is in it */
	show_text_fit_as(cr, DESK_FONT_ENGRAVE, TR("PHOTOS"), 0.0, y + eh * 0.80, 18.0 * k, true, 0.64, 0.16,
		0.12, 0.92, 0, ew * 0.80);
	cairo_restore(cr);
}

cairo_surface_t *desk_mat_envelope_picture(int w, int h, int corner, double cut, bool open, int *ox,
	int *oy)
{
	double box[4];
	if (!desk_mat_envelope_box(w, h, corner, cut, box)) {
		return NULL;
	}
	double r[4], k = desk_px_per_pt(w), cx, cy, ew, eh, t;
	mat_rect(w, h, DESK_MAT_FULL, r);
	envelope_place(r, k, corner, cut * k, &cx, &cy, &ew, &eh, &t);
	/* room above it for the prints and the flap open, and round it for its shadow */
	double pad = 8.0 * k, up = open ? eh * 0.40 : 0.0;
	int x0 = (int)floor(box[0] - pad), y0 = (int)floor(box[1] - pad - up);
	int sw = (int)ceil(box[2] + 2 * pad), sh = (int)ceil(box[3] + 2 * pad + up);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
	cairo_t *cr = cairo_create(s);
	cairo_translate(cr, -x0, -y0);
	cairo_push_group(cr);
	envelope_paint(cr, cx, cy, ew, eh, t, open, k);
	cairo_pattern_t *it = cairo_pop_group(cr);
	cairo_set_source(cr, it);
	cairo_paint(cr);
	mat_light_over(cr, w, h, it);
	cairo_pattern_destroy(it);
	cairo_destroy(cr);
	*ox = x0;
	*oy = y0;
	return s;
}

/* ---- what lies over the desk, a sprite at a time ----------------
 *
 * The compositor used to repaint everything drawn over the baked desk --
 * the tray's faces, the plates, the shade the drawer throws -- as one
 * full-screen picture, once a second and on every frame of the drawer,
 * and upload all of it each time, for a clock that changes once a minute.
 * A game keeps its HUD as sprites and redraws one only when what it shows
 * has changed; so does this. Each element here is drawn on its own, as it
 * sits with the drawer shut, and comes with a key that changes exactly
 * when drawing it again would come out different. What the drawer does to
 * the desk is a move, and the shade is a flat colour the GPU lays down. */

uint64_t desk_overlay_key(int which, time_t now, int unread)
{
	switch (which) {
	case DESK_OVL_MAT:
		return mat_key();
	case DESK_OVL_MAT_SHEET:
		return sheet_key();
	case DESK_OVL_CLOCK:
		return desk_clock_key(now);
	case DESK_OVL_POST:
		return 1u + (uint64_t)(unread > 0 ? unread : 0);
	case DESK_OVL_SLIPS:
		return desk_slips_key();
	default:
		return 1u;   /* the plates: they never change */
	}
}

bool desk_overlay_on_desk(int which)
{
	return which != DESK_OVL_ALLAPPS;   /* that one is screwed to the screen */
}

bool desk_overlay_shaded(int which)
{
	/* the plates at the back are not, and never were */
	return which != DESK_OVL_CONSOLE && which != DESK_OVL_POST;
}

void desk_draw_overlay(cairo_t *cr, int which, int w, int h, time_t now, int unread)
{
	struct room_xf xf;
	switch (which) {
	case DESK_OVL_MAT:
		mat_paint(cr, w, h);
		break;
	case DESK_OVL_MAT_SHEET:
		clear_sheet_paint(cr, w, h);
		break;
	case DESK_OVL_CASES:
		if (room_xform(w, h, 0.0, &xf)) {
			room_cases_paint(cr, &xf);
		}
		break;
	case DESK_OVL_CLOCK:
		if (room_xform(w, h, 0.0, &xf)) {
			desk_gadget_face(cr, &xf, DESK_GADGET_CLOCK, now);
		} else {
			desk_scene_warm(w, h, true);
		}
		break;
	case DESK_OVL_ALLAPPS:
		allapps_plate(cr, w, h, false);
		break;
	case DESK_OVL_CONSOLE:
		console_idle(cr, w, h, 0.0);
		break;
	case DESK_OVL_POST:
		desk_draw_post_idle(cr, w, h, 0.0, unread);
		break;
	case DESK_OVL_SLIPS:
		slips_paint(cr, w, h);
		break;
	default:
		break;
	}
}

bool desk_desk_place(int w, int h, double open, int *ox, int *oy, double *clip)
{
	struct room_xf xf;
	if (!room_xform(w, h, open, &xf)) {
		return false;
	}
	*ox = (int)xf.ox;
	*oy = (int)xf.oy;
	*clip = xf.clip;
	return true;
}

double desk_desk_shade(double open, float rgb[3])
{
	/* With the drawer out, the desk is not what you are looking at. It goes
	 * down the way a home screen does behind the multitasking tray. */
	rgb[0] = 0.020f;
	rgb[1] = 0.014f;
	rgb[2] = 0.008f;
	double e = clampf(open, 0, 1);
	e = e * e * (3.0 - 2.0 * e);
	return e > 0.002 ? 0.42 * e : 0.0;
}
