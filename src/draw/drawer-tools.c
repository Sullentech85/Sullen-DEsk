/* Part of draw.c: an application's sheet, and the room as the compositor sees it: the drawer and its tools.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ================================================================
 * One application, printed on a sheet of binder paper.
 *
 * Not an icon in a cell. A sheet has a grain, a punched hole with the tear
 * it picked up being turned, printing that went through the press a
 * fraction off square, and a running foot that is the only thing you can
 * read once it is fanned behind another. The things that stop this reading
 * as a card in a grid are all in the last third of this function.
 * ================================================================ */

/* The six inks a catalogue like this would have been printed in. */
static const double SHEET_INK[6][3] = {
	{ 0.13, 0.12, 0.12 },   /* process black */
	{ 0.58, 0.16, 0.13 },   /* warm red */
	{ 0.16, 0.26, 0.44 },   /* mid blue */
	{ 0.16, 0.34, 0.22 },   /* green */
	{ 0.56, 0.40, 0.14 },   /* ochre */
	{ 0.32, 0.18, 0.36 },   /* violet */
};

/* The picture, printed rather than composited: struck once solid, once
 * faintly off register, and then broken up by the tooth of the paper. */
static void sheet_mark(cairo_t *cr, cairo_surface_t *icon, const char *initial,
	double x, double y, double s, unsigned seed)
{
	double dx = (hash21(seed * 1.9, 3.1) - 0.5) * s * 0.022;
	double dy = (hash21(seed * 1.9, 9.7) - 0.5) * s * 0.022;
	const double *ink = SHEET_INK[seed % 6];

	if (icon) {
		int iw = cairo_image_surface_get_width(icon);
		int ih = cairo_image_surface_get_height(icon);
		if (iw > 0 && ih > 0) {
			double k = s / (double)(iw > ih ? iw : ih);
			for (int pass = 0; pass < 2; pass++) {
				cairo_save(cr);
				cairo_translate(cr, x + (s - iw * k) * 0.5 + (pass ? dx : 0.0),
					y + (s - ih * k) * 0.5 + (pass ? dy : 0.0));
				cairo_scale(cr, k, k);
				cairo_set_source_surface(cr, icon, 0, 0);
				cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
				cairo_paint_with_alpha(cr, pass ? 0.13 : 0.93);
				cairo_restore(cr);
			}
			/* knock the gloss off it: a wash of the paper's own colour */
			cairo_save(cr);
			cairo_rectangle(cr, x, y, s, s);
			cairo_clip(cr);
			cairo_set_source_rgba(cr, 0.95, 0.93, 0.89, 0.13);
			cairo_paint(cr);
			overlay_grain(cr, x, y, s, s, 1.5);
			cairo_restore(cr);
			return;
		}
	}

	/* Nothing to print from, so it gets a letterpress initial: one ink, a
	 * ruled block round it, struck twice. */
	for (int pass = 0; pass < 2; pass++) {
		cairo_save(cr);
		cairo_translate(cr, pass ? dx : 0.0, pass ? dy : 0.0);
		double a = pass ? 0.12 : 1.0;
		cairo_set_source_rgba(cr, ink[0], ink[1], ink[2], a);
		cairo_set_line_width(cr, s * 0.035);
		desk_rounded_path(cr, x + s * 0.10, y + s * 0.10, s * 0.80, s * 0.80, s * 0.06);
		cairo_stroke(cr);
		show_text_as(cr, DESK_FONT_PRINT, initial && *initial ? initial : "?", x + s * 0.5,
			y + s * 0.68,
			s * 0.56, true, ink[0], ink[1], ink[2], a, 0);
		cairo_restore(cr);
	}
	cairo_save(cr);
	cairo_rectangle(cr, x, y, s, s);
	cairo_clip(cr);
	overlay_grain(cr, x, y, s, s, 1.8);
	cairo_restore(cr);
}

void desk_draw_app_sheet(cairo_t *cr, double w, double h,
	const char *name, const char *generic, const char *code,
	cairo_surface_t *icon, const char *initial, unsigned seed, double wear)
{
	if (w < 8 || h < 8) {
		return;
	}
	wear = clampf(wear, 0.0, 1.0);

	/* ---- the stock ---- */
	fill_paper(cr, 0, 0, w, h, 2.5);
	cairo_save(cr);
	desk_rounded_path(cr, 0, 0, w, h, 2.5);
	cairo_clip(cr);
	/* binder paper is thinner than the card a window is made of -- and it
	 * has been in that binder since before anybody here started */
	cairo_set_source_rgba(cr, 1, 1, 1, 0.025);
	cairo_paint(cr);
	paper_age(cr, w, h, seed, 0.54 + 0.40 * hash21(seed, 27.7));

	/* ---- the printing ---- */
	cairo_save(cr);
	/* every sheet went through the press differently */
	cairo_translate(cr, (hash21(seed, 1.7) - 0.5) * w * 0.014,
		(hash21(seed, 2.9) - 0.5) * h * 0.010);
	/* and none of them came out square to the paper. This one line is what
	 * stops a page of these reading as a lattice. */
	cairo_translate(cr, w * 0.5, h * 0.5);
	cairo_rotate(cr, (hash21(seed, 4.3) - 0.5) * 0.010);
	cairo_translate(cr, -w * 0.5, -h * 0.5);

	const double *ink = SHEET_INK[seed % 6];
	/* the catalogue mark goes in the outer corner: the inner one is the
	 * hole's, and a number printed over a hole is a number you cannot read */
	if (code && *code) {
		show_text_as(cr, DESK_FONT_PRINT, code, w * 0.90, h * 0.092, w * 0.036, true,
			ink[0], ink[1], ink[2], 0.78, 1);
	}

	sheet_mark(cr, icon, initial, w * 0.275, h * 0.345 - w * 0.225, w * 0.45, seed);

	/* the name, with the press running short of ink across it */
	cairo_save(cr);
	cairo_rectangle(cr, 0, h * 0.60, w, h * 0.09);
	cairo_clip(cr);
	show_text_fit_as(cr, DESK_FONT_PRINT, name ? name : "", w * 0.5, h * 0.655, w * 0.078, true,
		0.14, 0.13, 0.12, 1.0, 0, w * 0.80);
	cairo_pattern_t *dry = cairo_pattern_create_linear(
		hash21(seed, 5.1) > 0.5 ? 0 : w, h * 0.60, hash21(seed, 5.1) > 0.5 ? w : 0, h * 0.60);
	cairo_pattern_add_color_stop_rgba(dry, 0.0, 0.95, 0.93, 0.89, 0.00);
	cairo_pattern_add_color_stop_rgba(dry, 1.0, 0.95, 0.93, 0.89, 0.16);
	cairo_set_source(cr, dry);
	cairo_paint(cr);
	cairo_pattern_destroy(dry);
	cairo_restore(cr);

	wobble_line(cr, w * 0.16, h * 0.688, w * 0.84, h * 0.688, seed, 7, 0.6, 1.0,
		ink[0], ink[1], ink[2], 0.55);

	if (generic && *generic) {
		show_text_fit_as(cr, DESK_FONT_PRINT, generic, w * 0.5, h * 0.742, w * 0.042, false,
			0.42, 0.40, 0.38, 0.95, 0, w * 0.76);
	}

	/* The running foot. The bottom margin is the only part of a sheet that
	 * stays out when it is fanned behind another, so this is the line you
	 * actually read in the binder. */
	show_text_fit_as(cr, DESK_FONT_PRINT, name ? name : "", w * 0.08, h * 0.945, w * 0.038, false,
		0.30, 0.28, 0.26, 0.92, -1, w * 0.62);
	if (code && *code) {
		show_text_as(cr, DESK_FONT_PRINT, code, w * 0.92, h * 0.945, w * 0.038, false,
			0.30, 0.28, 0.26, 0.72, 1);
	}
	cairo_restore(cr);

	/* ---- what handling did to it ---- */

	/* thumbed along the fore edge */
	cairo_pattern_t *th = cairo_pattern_create_linear(w - w * 0.045, 0, w, 0);
	cairo_pattern_add_color_stop_rgba(th, 0.0, 0.12, 0.09, 0.05, 0.00);
	cairo_pattern_add_color_stop_rgba(th, 1.0, 0.12, 0.09, 0.05, 0.05 + wear * 0.05);
	cairo_set_source(cr, th);
	cairo_paint(cr);
	cairo_pattern_destroy(th);

	/* somebody's thumb, at a corner this sheet always gets picked up by */
	if (wear > 0.25) {
		double sx = hash21(seed, 6.3) > 0.5 ? w * 0.74 : w * 0.10;
		contact_shadow(cr, sx, h * (0.80 + hash21(seed, 7.7) * 0.10),
			w * 0.22, h * 0.10, 0.05 * wear);
	}
	/* it has been out of the binder and back in again */
	paper_crease(cr, w, h, seed + 5, 0.055 + wear * 0.065);
	paper_edge(cr, w, h, seed, 0.55 + wear * 0.45);
	cairo_restore(cr);

	/* the cut edges, which are never flush with the sheet beneath */
	cairo_save(cr);
	desk_rounded_path(cr, 0, 0, w, h, 2.5);
	cairo_clip(cr);
	free_edge_v(cr, w - 1.6, h, seed, 3);
	free_edge_h(cr, h - 1.6, w, seed, 4);
	cairo_restore(cr);

	/* ---- the hole it hangs by ---- */
	double hx = w * 0.052, hy = h * 0.077, hr = w * 0.026;
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_arc(cr, hx, hy, hr, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_set_source_rgba(cr, 0.34, 0.24, 0.13, 0.50);
	cairo_set_line_width(cr, 1.0);
	cairo_arc(cr, hx, hy, hr + 0.5, 0, 2 * M_PI);
	cairo_stroke(cr);
	/* and the tear it got the day somebody pulled it off the ring */
	if (wear > 0.45) {
		double t = (wear - 0.45) / 0.55;
		wobble_line(cr, hx + hr * 0.7, hy - hr * 0.5, hx + hr * 2.6, hy - hr * 1.5,
			seed, 11, 1.8, 1.3, 0.30, 0.22, 0.12, 0.35 + t * 0.35);
	}

	/* ---- and the corner it has been turned down at ---- */
	if (wear > 0.62) {
		double e = w * 0.055 + hash21(seed * 3.3, 5.5) * w * 0.03;
		cairo_save(cr);
		cairo_translate(cr, w, h);
		cairo_scale(cr, -1.0, -1.0);
		cairo_move_to(cr, 0, e * 1.15);
		cairo_line_to(cr, e * 1.05, 0);
		cairo_line_to(cr, e * 0.18, -0.6);
		cairo_close_path(cr);
		cairo_clip_preserve(cr);
		paint_paper_back(cr, -2, -2, e * 2.0 + 4, e * 2.0 + 4);
		cairo_pattern_t *dg = cairo_pattern_create_linear(0, 0, e, e);
		cairo_pattern_add_color_stop_rgba(dg, 0.0, 0.10, 0.06, 0.02, 0.30);
		cairo_pattern_add_color_stop_rgba(dg, 1.0, 1.00, 0.97, 0.90, 0.10);
		cairo_set_source(cr, dg);
		cairo_fill(cr);
		cairo_pattern_destroy(dg);
		cairo_set_source_rgba(cr, 0.34, 0.24, 0.13, 0.55);
		cairo_set_line_width(cr, 1.0);
		cairo_move_to(cr, 0, e * 1.15);
		cairo_line_to(cr, e * 1.05, 0);
		cairo_stroke(cr);
		cairo_restore(cr);
	}
}

/* ================================================================
 * The room, as the compositor sees it.
 * ================================================================ */

/* How far the desk has to move to clear the drawer, as a fraction of the
 * screen. The drawer band is measured in sheet units and the sheet is drawn
 * at shut_zoom, so this is all it takes. */
static double room_lift(const struct desk_room_sheets *sh)
{
	return (sh->dr_v1 - sh->dr_v0) * sh->shut_zoom;
}

void desk_room_view(double open, const struct desk_room_sheets *sheets,
	double *zoom, double *pan, double *slide_t)
{
	/* Already eased by whoever is pulling it, and allowed a little past
	 * the stop: a drawer that arrives dead still has never been opened by
	 * a hand. Easing it again here bent the curve twice and flattened the
	 * knock at the end back out of it. */
	double e = clampf(open, -0.06, 1.06);
	/* The desk keeps its size: it slides up by the height of the drawer to
	 * get out of its way, and no further. Zooming out to keep both in frame
	 * shrank the desk, and windows laid over it slid out from under the
	 * pointer. */
	*zoom = sheets->shut_zoom;
	*pan = sheets->shut_pan - room_lift(sheets) * e;
	*slide_t = e;
}

void desk_room_drawer_offset(int screen_h, double open, double scale,
	double oy, double v0, double v1, double sheet_h, double *dy, double *clip_y)
{
	double e = clampf(open, -0.06, 1.06);
	double band = (v1 - v0) * sheet_h * scale;
	/* The drawer rises from below the bottom edge, its own height, in step
	 * with the desk moving up the same distance. */
	double top = screen_h - band * e;
	*clip_y = top;
	*dy = top - (oy + v0 * sheet_h * scale);
}

bool desk_room_sheets(int screen_w, int screen_h, struct desk_room_sheets *out)
{
	struct room_render *rr = room_for(screen_w, screen_h);
	if (!desk_scene_ready(screen_w, screen_h)) {
		/* Nobody asks for the room before they want to draw it, so this is
		 * where the bake starts. On a worker: the first frame has to be on
		 * screen in seconds and a cold trace takes most of a minute. */
		desk_scene_warm(screen_w, screen_h, true);
	}
	if (!rr || !rr->background || !rr->drawer) {
		return false;
	}
	out->background = rr->background;
	out->drawer = rr->drawer;
	out->shelf = room_for_view(screen_w, screen_h, DESK_VIEW_SHELF) == rr ? rr->shelf : NULL;
	out->w = rr->w;
	out->h = rr->h;
	out->slide = rr->slide;
	out->edge = rr->edge;
	out->shut_zoom = rr->shut_zoom;
	out->shut_pan = rr->shut_pan;
	out->dr_v0 = rr->dr_v0;
	out->dr_v1 = rr->dr_v1;
	return true;
}

/* The same arithmetic the compositor does when it places the two sheets.
 * Hit testing and the hover overlay have to agree with it exactly, or the
 * pointer picks an object a few pixels from the one under it. */
struct room_xf {
	struct room_render *rr;
	double scale;
	double ox, oy;     /* where the background sheet lands */
	double ny;         /* where the drawer sheet lands */
	double clip;       /* nothing of the drawer shows above this */
};

static int room_xform(int screen_w, int screen_h, double open, struct room_xf *xf)
{
	struct room_render *rr = room_for(screen_w, screen_h);
	if (!rr) {
		return 0;
	}
	struct desk_room_sheets sh = {
		rr->background, rr->drawer, NULL, rr->w, rr->h, rr->slide, rr->edge,
		rr->shut_zoom, rr->shut_pan, rr->dr_v0, rr->dr_v1,
	};
	double zoom, pan, t;
	desk_room_view(open, &sh, &zoom, &pan, &t);
	int dh = (int)(screen_h * zoom + 0.5);
	/* Where the sheet's top lands. Fixed here, before the sheet is allowed
	 * to grow, because what is along the top edge -- the console's ledge,
	 * the post box, the lamps -- is the part you have to be able to reach. */
	int oy = (int)((screen_h - dh) / 2 + pan * screen_h);

	/* A room traced for another shape of screen is narrower than this one,
	 * and the floor showed along both edges. Grow it until it covers, and
	 * spend the extra off the bottom, which is cropped anyway. A room
	 * traced for this screen has nothing to correct: cover is 1. */
	double cover = ((double)screen_w / screen_h) / ((double)rr->w / rr->h);
	if (cover > 1.0) {
		dh = (int)(dh * cover + 0.5);
	}
	/* Centred on the width the sheet actually covers, which is the screen's
	 * only when the sheet was traced for this exact shape of screen. It was
	 * centred on screen_w * zoom, and a sheet of another shape then sat off
	 * to one side -- far enough to uncover the floor along one edge. */
	int dw = (int)((double)rr->w * dh / rr->h + 0.5);
	xf->rr = rr;
	/* rounded exactly the way the compositor rounds it, so a pointer lands
	 * on the pixel it is over and not the one beside it */
	xf->ox = (screen_w - dw) / 2;
	xf->oy = oy;
	xf->scale = (double)dh / rr->h;
	double dy, clip;
	desk_room_drawer_offset(screen_h, open, xf->scale, xf->oy,
		rr->dr_v0, rr->dr_v1, rr->h, &dy, &clip);
	xf->ny = xf->oy + dy;
	xf->clip = clip;
	return 1;
}

/* ---- what is in the drawer ---------------------------------------
 *
 * The drawer sheet is traced empty, and what is in the drawer is drawn over
 * it: whatever has been put there, in the order it was put -- one of the
 * five small things it comes with (below), a card with a program's mark on
 * it for a program that has none of its own, or a sheet folded away -- laid
 * in a row along the
 * felt, the row closing up as it fills, each thing with the shadow the
 * drawer's lamp gives it. A camera looking straight down at it makes moving
 * a thing along the row the next thing to moving the picture of it. */

#define DRAWER_PITCH 3.05          /* room units from one thing to the next, at most */
#define DRAWER_FELT_Y (RM_DR_BOT + 0.44)

/* ---- the five things a drawer comes with ---------------------------
 *
 * Small things you would keep in a desk drawer, lying flat on the felt and
 * seen from straight above: a pocket computer with a prompt on its screen
 * (the terminal), a brass pocket compass (the browser), a manila folder
 * with papers in it (the files), a notepad with a pencil across it (the
 * editor) and a screwdriver (Shell Settings). They used to be traced
 * models -- a little CRT, a globe -- that stood up off the felt and read
 * as icons dropped into it. These lie low, in the drawer's own light: the
 * lamp up behind the desk to the right, a shadow under each as long as the
 * thing is thick, and the felt's own brightness where it lies.
 *
 * Everything is drawn in room units about the thing's middle. */

/* toward the lamp on the screen (y down), and how much of it is up */
#define TOOL_LAMP_X 0.52
#define TOOL_LAMP_Y (-0.854)
#define TOOL_LAMP_UP 0.80

/* toward the lamp, across the table, in the thing's own turned frame */
struct tool_light {
	double x, y;
};

static struct tool_light tool_light_turned(const struct tool_light *l, double a)
{
	double c = cos(a), s = sin(a);
	return (struct tool_light){ c * l->x + s * l->y, -s * l->x + c * l->y };
}

/* The light on something round lying along x, at s across it (-1 its edge
 * at -y, 1 at +y), seen from straight above: how lit, and its shine. */
static double tool_lit(double s, const struct tool_light *l, double *spec)
{
	double up = TOOL_LAMP_UP, across = sqrt(1.0 - up * up);
	double nz = sqrt(fmax(0.0, 1.0 - s * s));
	double d = s * l->y * across + nz * up;
	/* halfway between the lamp and the eye, which is straight above */
	double hy = l->y * across, hz = up + 1.0;
	double nh = (s * hy + nz * hz) / hypot(hy, hz);
	*spec = pow(fmax(nh, 0.0), 40.0);
	return fmax(d, 0.0);
}

/* Across something round of radius r lying along x, centred on y = y0. */
static cairo_pattern_t *tool_round(double y0, double r, const struct tool_light *l,
	double cr0, double cg, double cb, double shine)
{
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y0 - r, 0, y0 + r);
	for (int k = 0; k <= 12; k++) {
		double s = -1.0 + k / 6.0;
		double spec;
		double lit = 0.30 + 0.80 * tool_lit(s, l, &spec);
		double sp = shine * spec;
		cairo_pattern_add_color_stop_rgb(g, k / 12.0, fmin(cr0 * lit + sp, 1.0),
			fmin(cg * lit + sp, 1.0), fmin(cb * lit + sp, 1.0));
	}
	return g;
}

/* Across something flat: lighter toward the lamp, by `by` either way. */
static cairo_pattern_t *tool_flat(double hw, double hh, const struct tool_light *l,
	double cr0, double cg, double cb, double by)
{
	cairo_pattern_t *g = cairo_pattern_create_linear(l->x * hw, l->y * hh, -l->x * hw, -l->y * hh);
	cairo_pattern_add_color_stop_rgb(g, 0.0, fmin(cr0 * (1.0 + by), 1.0),
		fmin(cg * (1.0 + by), 1.0), fmin(cb * (1.0 + by), 1.0));
	cairo_pattern_add_color_stop_rgb(g, 1.0, cr0 * (1.0 - by), cg * (1.0 - by), cb * (1.0 - by));
	return g;
}

/* The edges of a flat box: the ones facing the lamp catch it, the others
 * are in their own shade. */
static void tool_edges(cairo_t *cr, double x, double y, double w, double h, double r,
	const struct tool_light *l, double px, double hi, double lo)
{
	const double nx[4] = { 0, 1, 0, -1 }, ny[4] = { -1, 0, 1, 0 };
	cairo_save(cr);
	cairo_set_line_width(cr, 1.2 * px);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int e = 0; e < 4; e++) {
		double f = nx[e] * l->x + ny[e] * l->y;
		double in = 0.7 * px;
		double ax = 0, ay = 0, bx = 0, by = 0;
		switch (e) {
		case 0: ax = x + r; ay = y + in; bx = x + w - r; by = y + in; break;
		case 1: ax = x + w - in; ay = y + r; bx = x + w - in; by = y + h - r; break;
		case 2: ax = x + r; ay = y + h - in; bx = x + w - r; by = y + h - in; break;
		default: ax = x + in; ay = y + r; bx = x + in; by = y + h - r; break;
		}
		if (f > 0.0) {
			cairo_set_source_rgba(cr, 1.0, 0.97, 0.88, hi * f);
		} else {
			cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, lo * -f);
		}
		cairo_move_to(cr, ax, ay);
		cairo_line_to(cr, bx, by);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

static void tool_text(cairo_t *cr, const char *s, double x, double y, double size,
	enum desk_font role, bool bold, double r, double g, double b, double a)
{
	cairo_save(cr);
	desk_font_select(cr, role, s, bold, false);
	cairo_set_font_size(cr, size);
	cairo_text_extents_t e;
	cairo_text_extents(cr, s, &e);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_move_to(cr, x - e.x_bearing - e.width * 0.5, y - e.y_bearing - e.height * 0.5);
	cairo_show_text(cr, s);
	cairo_restore(cr);
}

/* A pocket computer: dark moulded case, a brushed plate with a screen that
 * has a prompt waiting on it, and rows of little keys. */
static void tool_term(cairo_t *cr, double px, const struct tool_light *l, unsigned seed)
{
	const double W = 2.30, H = 1.16, R = 0.10;
	double x0 = -W * 0.5, y0 = -H * 0.5;
	(void)seed;
	desk_rounded_path(cr, x0, y0, W, H, R);
	cairo_pattern_t *g = tool_flat(W * 0.5, H * 0.5, l, 0.20, 0.20, 0.21, 0.22);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	tool_edges(cr, x0, y0, W, H, R, l, px, 0.30, 0.35);

	/* the plate, brushed along its length */
	double fx = x0 + 0.09, fy = y0 + 0.08, fw = W - 0.18, fh = 0.50;
	desk_rounded_path(cr, fx, fy, fw, fh, 0.03);
	g = cairo_pattern_create_linear(0, fy, 0, fy + fh);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.66, 0.65, 0.61);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.50, 0.49, 0.46);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_save(cr);
	cairo_clip(cr);
	cairo_set_line_width(cr, 0.8 * px);
	for (int i = 0; i < 46; i++) {
		double yy = fy + fh * hash21(i * 1.7, 3.1);
		double a = 0.03 + 0.06 * hash21(i * 2.3, 5.9);
		if (i & 1) {
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, a);
		} else {
			cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, a);
		}
		cairo_move_to(cr, fx, yy);
		cairo_line_to(cr, fx + fw, yy);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
	tool_edges(cr, fx, fy, fw, fh, 0.03, l, px, 0.35, 0.30);

	/* the screen, in its bezel, with a prompt on it */
	double sx = fx + 0.07, sy = fy + 0.05, sw = 1.42, sh = fh - 0.10;
	desk_rounded_path(cr, sx, sy, sw, sh, 0.03);
	cairo_set_source_rgb(cr, 0.07, 0.07, 0.07);
	cairo_fill(cr);
	double gx = sx + 0.035, gy = sy + 0.035, gw = sw - 0.07, gh = sh - 0.07;
	cairo_rectangle(cr, gx, gy, gw, gh);
	g = cairo_pattern_create_radial(gx + gw * 0.35, gy + gh * 0.5, 0.0,
		gx + gw * 0.35, gy + gh * 0.5, gw * 0.7);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.05, 0.14, 0.08);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.02, 0.05, 0.03);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_save(cr);
	cairo_rectangle(cr, gx, gy, gw, gh);
	cairo_clip(cr);
	desk_font_select(cr, DESK_FONT_SCREEN, NULL, true, false);
	cairo_set_font_size(cr, gh * 0.82);
	cairo_font_extents_t fe;
	cairo_font_extents(cr, &fe);
	double base = gy + gh * 0.5 + (fe.ascent - fe.descent) * 0.5;
	/* the glow first, then the light itself */
	for (int pass = 0; pass < 2; pass++) {
		cairo_set_source_rgba(cr, 0.35, 0.95, 0.50, pass ? 0.95 : 0.25);
		cairo_move_to(cr, gx + 0.06, base);
		if (!pass) {
			cairo_set_line_width(cr, 2.2 * px);
			cairo_text_path(cr, "$");
			cairo_stroke(cr);
		} else {
			cairo_show_text(cr, "$");
		}
	}
	cairo_text_extents_t te;
	cairo_text_extents(cr, "$ ", &te);
	cairo_set_source_rgba(cr, 0.35, 0.95, 0.50, 0.85);
	cairo_rectangle(cr, gx + 0.06 + te.x_advance, base - fe.ascent * 0.72, fe.max_x_advance * 0.8,
		fe.ascent * 0.80);
	cairo_fill(cr);
	/* and the glass over it */
	g = cairo_pattern_create_linear(gx, gy, gx + gw * 0.6, gy + gh);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 1.0, 1.0, 0.10);
	cairo_pattern_add_color_stop_rgba(g, 0.5, 1.0, 1.0, 1.0, 0.02);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 1.0, 1.0, 1.0, 0.0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);

	/* a lamp and a speaker grille at the end of the plate */
	double lx = sx + sw + 0.13, ly = fy + fh * 0.5;
	g = cairo_pattern_create_radial(lx, ly, 0.0, lx, ly, 0.07);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 0.45, 0.30, 0.9);
	cairo_pattern_add_color_stop_rgba(g, 0.45, 0.85, 0.16, 0.10, 0.9);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.85, 0.16, 0.10, 0.0);
	cairo_set_source(cr, g);
	cairo_arc(cr, lx, ly, 0.07, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 0.03);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int i = 0; i < 5; i++) {
		double gx2 = lx + 0.14 + i * 0.07;
		cairo_set_source_rgba(cr, 0.10, 0.10, 0.10, 0.75);
		cairo_move_to(cr, gx2, fy + 0.10);
		cairo_line_to(cr, gx2, fy + fh - 0.10);
		cairo_stroke(cr);
	}

	/* the keys */
	double kx = x0 + 0.11, ky = fy + fh + 0.07;
	double kw_all = W - 0.22, kh_all = y0 + H - 0.07 - ky;
	const int cols = 12, rows = 3;
	double gap = 0.035;
	double kw = (kw_all - gap * (cols - 1)) / cols, kh = (kh_all - gap * (rows - 1)) / rows;
	for (int r = 0; r < rows; r++) {
		for (int c = 0; c < cols; c++) {
			double x = kx + c * (kw + gap), y = ky + r * (kh + gap);
			double w = kw, h = kh;
			double kr = 0.30, kg = 0.30, kb = 0.31;
			if (r == 2 && c >= 3 && c <= 7) {
				if (c != 3) {
					continue;
				}
				w = kw * 5 + gap * 4;   /* the space bar */
			}
			if (c == cols - 1 && r >= 1) {
				if (r == 2) {
					continue;
				}
				h = kh * 2 + gap;       /* ENTER, two rows tall */
				kr = 0.82;
				kg = 0.47;
				kb = 0.18;
			}
			if (r == 2 && c == 0) {
				kr = kg = kb = 0.58;
			}
			desk_rounded_path(cr, x, y, w, h, 0.02);
			cairo_pattern_t *kgp = tool_flat(w * 0.5, h * 0.5, l, kr, kg, kb, 0.18);
			cairo_matrix_t m;
			cairo_matrix_init_translate(&m, -(x + w * 0.5), -(y + h * 0.5));
			cairo_pattern_set_matrix(kgp, &m);
			cairo_set_source(cr, kgp);
			cairo_fill(cr);
			cairo_pattern_destroy(kgp);
			tool_edges(cr, x, y, w, h, 0.02, l, px, 0.35, 0.45);
			if (w < kw * 1.5 && kr < 0.5) {
				cairo_set_source_rgba(cr, 0.85, 0.85, 0.80, 0.45);
				cairo_arc(cr, x + w * 0.5, y + h * 0.45, fmin(w, h) * 0.13, 0, 2 * M_PI);
				cairo_fill(cr);
			}
		}
	}
}

/* A pocket compass: a brass case on a bail, a printed card with its rose,
 * and a needle that points north whichever way the case is lying. */
static void tool_compass(cairo_t *cr, double px, const struct tool_light *l, double needle)
{
	const double R = 0.90;
	/* the bail it hangs by */
	cairo_pattern_t *g = tool_flat(0.2, 0.2, l, 0.70, 0.55, 0.27, 0.35);
	cairo_matrix_t m;
	cairo_matrix_init_translate(&m, 0.0, R + 0.17);
	cairo_pattern_set_matrix(g, &m);
	cairo_set_source(cr, g);
	desk_rounded_path(cr, -0.09, -R - 0.08, 0.18, 0.14, 0.04);
	cairo_fill(cr);
	cairo_set_line_width(cr, 0.055);
	cairo_arc(cr, 0, -R - 0.19, 0.12, 0, 2 * M_PI);
	cairo_stroke(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 0.9 * px);
	cairo_set_source_rgba(cr, 1.0, 0.95, 0.80, 0.35);
	cairo_arc(cr, 0, -R - 0.19, 0.12, -M_PI * 0.75, -M_PI * 0.05);
	cairo_stroke(cr);

	/* the case, and the inside of its rim the other way round */
	g = tool_flat(R, R, l, 0.68, 0.52, 0.25, 0.32);
	cairo_set_source(cr, g);
	cairo_arc(cr, 0, 0, R, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	g = tool_flat(R, R, l, 0.62, 0.47, 0.22, -0.30);
	cairo_set_source(cr, g);
	cairo_arc(cr, 0, 0, R * 0.92, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	g = tool_flat(R, R, l, 0.70, 0.55, 0.27, 0.18);
	cairo_set_source(cr, g);
	cairo_arc(cr, 0, 0, R * 0.885, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* the ring of degrees engraved in the bezel */
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	for (int i = 0; i < 72; i++) {
		double a = i * M_PI / 36.0;
		double r0 = (i % 6 == 0) ? R * 0.79 : R * 0.82;
		cairo_set_line_width(cr, (i % 6 == 0 ? 1.2 : 0.8) * px);
		cairo_set_source_rgba(cr, 0.20, 0.13, 0.05, 0.60);
		cairo_move_to(cr, cos(a) * r0, sin(a) * r0);
		cairo_line_to(cr, cos(a) * R * 0.865, sin(a) * R * 0.865);
		cairo_stroke(cr);
	}

	/* the card */
	double rc = R * 0.78;
	g = cairo_pattern_create_radial(-l->x * rc * 0.2, -l->y * rc * 0.2, 0.0, 0, 0, rc);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.92, 0.88, 0.77);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.82, 0.77, 0.64);
	cairo_set_source(cr, g);
	cairo_arc(cr, 0, 0, rc, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 0.9 * px);
	cairo_set_source_rgba(cr, 0.25, 0.20, 0.14, 0.45);
	cairo_arc(cr, 0, 0, rc * 0.62, 0, 2 * M_PI);
	cairo_stroke(cr);
	/* the rose: the four quarters long, the four between short, each
	 * point half in light and half in shade */
	for (int pass = 0; pass < 2; pass++) {
		for (int k = pass; k < 8; k += 2) {
			bool major = (k % 2) == 0;
			double a = k * M_PI / 4.0 - M_PI * 0.5;
			double len = major ? rc * 0.74 : rc * 0.46;
			double hw = major ? rc * 0.11 : rc * 0.085;
			double ca = cos(a), sa = sin(a);
			double tx = ca * len, ty = sa * len;
			double qx = -sa * hw, qy = ca * hw;
			bool north = k == 0;
			for (int half = 0; half < 2; half++) {
				double sgn = half ? 1.0 : -1.0;
				cairo_move_to(cr, 0, 0);
				cairo_line_to(cr, tx, ty);
				cairo_line_to(cr, qx * sgn, qy * sgn);
				cairo_close_path(cr);
				if (north) {
					cairo_set_source_rgb(cr, half ? 0.78 : 0.56, half ? 0.26 : 0.13,
						half ? 0.20 : 0.10);
				} else if (major) {
					cairo_set_source_rgb(cr, half ? 0.66 : 0.20, half ? 0.63 : 0.21,
						half ? 0.57 : 0.25);
				} else {
					cairo_set_source_rgb(cr, half ? 0.72 : 0.38, half ? 0.69 : 0.36,
						half ? 0.62 : 0.34);
				}
				cairo_fill(cr);
			}
		}
	}
	const char *nesw[4] = { "N", "E", "S", "W" };
	for (int k = 0; k < 4; k++) {
		double a = k * M_PI * 0.5 - M_PI * 0.5;
		double rr = rc * 0.86;
		cairo_save(cr);
		cairo_translate(cr, cos(a) * rr, sin(a) * rr);
		cairo_rotate(cr, a + M_PI * 0.5);
		tool_text(cr, nesw[k], 0, 0, rc * 0.21, DESK_FONT_DISPLAY, true,
			k == 0 ? 0.55 : 0.18, k == 0 ? 0.12 : 0.15, k == 0 ? 0.09 : 0.12, 0.92);
		cairo_restore(cr);
	}

	/* the needle */
	cairo_save(cr);
	cairo_rotate(cr, needle);
	double nl = rc * 0.80, nw = rc * 0.075;
	struct tool_light nlit = tool_light_turned(l, needle);
	for (int end = 0; end < 2; end++) {
		double dir = end ? 1.0 : -1.0;   /* -y is north */
		for (int half = 0; half < 2; half++) {
			double side = half ? 1.0 : -1.0;
			cairo_move_to(cr, 0, dir * nl);
			cairo_line_to(cr, side * nw, 0);
			cairo_line_to(cr, 0, 0);
			cairo_close_path(cr);
			double lit = 0.75 + 0.35 * side * nlit.x;
			if (end == 0) {
				cairo_set_source_rgb(cr, fmin(0.78 * lit, 1.0), 0.15 * lit, 0.11 * lit);
			} else {
				cairo_set_source_rgb(cr, 0.62 * lit, 0.64 * lit, 0.67 * lit);
			}
			cairo_fill(cr);
		}
	}
	cairo_restore(cr);
	g = tool_flat(rc * 0.08, rc * 0.08, l, 0.78, 0.62, 0.30, 0.40);
	cairo_set_source(cr, g);
	cairo_arc(cr, 0, 0, rc * 0.08, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);

	/* the glass: the rim's shadow on the card, and the lamp in it */
	g = cairo_pattern_create_radial(0, 0, rc * 0.80, 0, 0, rc);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0.0, 0.0, 0.0, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.10, 0.06, 0.02, 0.40);
	cairo_set_source(cr, g);
	cairo_arc(cr, 0, 0, rc, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_save(cr);
	cairo_arc(cr, 0, 0, rc * 0.96, 0, 2 * M_PI);
	cairo_clip(cr);
	cairo_new_path(cr);
	cairo_arc(cr, 0, 0, rc * 0.96, 0, 2 * M_PI);
	cairo_arc_negative(cr, -l->x * rc * 0.22, -l->y * rc * 0.22, rc * 0.96, 2 * M_PI, 0);
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
	g = cairo_pattern_create_linear(l->x * rc, l->y * rc, 0, 0);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 1.0, 0.96, 0.28);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 1.0, 1.0, 0.96, 0.0);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
}

/* A manila folder with papers in it, a paper clip over its edge. */
static void tool_folder(cairo_t *cr, double px, const struct tool_light *l, unsigned seed)
{
	const double W = 2.00, H = 1.42;
	double x0 = -W * 0.5, y0 = -H * 0.5;
	/* the back, with its tab */
	cairo_move_to(cr, x0, y0 + 0.16);
	cairo_line_to(cr, x0 + 0.10, y0 + 0.16);
	cairo_line_to(cr, x0 + 0.17, y0);
	cairo_line_to(cr, x0 + 0.79, y0);
	cairo_line_to(cr, x0 + 0.86, y0 + 0.16);
	cairo_line_to(cr, x0 + W, y0 + 0.16);
	cairo_line_to(cr, x0 + W, y0 + H);
	cairo_line_to(cr, x0, y0 + H);
	cairo_close_path(cr);
	cairo_pattern_t *g = tool_flat(W * 0.5, H * 0.5, l, 0.72, 0.58, 0.36, 0.10);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 0.9 * px);
	cairo_set_source_rgba(cr, 0.28, 0.19, 0.08, 0.40);
	cairo_stroke(cr);
	/* its label */
	cairo_rectangle(cr, x0 + 0.24, y0 + 0.035, 0.48, 0.10);
	cairo_set_source_rgb(cr, 0.90, 0.87, 0.78);
	cairo_fill(cr);
	cairo_set_line_width(cr, 1.0 * px);
	cairo_set_source_rgba(cr, 0.25, 0.20, 0.15, 0.55);
	cairo_move_to(cr, x0 + 0.29, y0 + 0.085);
	cairo_line_to(cr, x0 + 0.29 + 0.18 + 0.12 * hash21(seed % 97u, 1.0), y0 + 0.085);
	cairo_stroke(cr);

	/* the papers in it: the tops of two sheets, and their lines */
	for (int i = 0; i < 2; i++) {
		double top = y0 + 0.21 + i * 0.045;
		double left = x0 + 0.10 + i * 0.07;
		double right = x0 + W + 0.03 - i * 0.05;
		cairo_rectangle(cr, left, top, right - left, y0 + H - 0.05 - top);
		double t = 0.93 - i * 0.025;
		cairo_set_source_rgb(cr, t, t - 0.01, t - 0.06);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.30, 0.25, 0.18, 0.35);
		cairo_set_line_width(cr, 0.8 * px);
		cairo_stroke(cr);
		cairo_set_source_rgba(cr, 0.30, 0.30, 0.35, 0.45);
		cairo_set_line_width(cr, 1.0 * px);
		double ll = 0.35 + 0.5 * hash21((seed % 97u) + i, 4.2);
		cairo_move_to(cr, left + 0.08, top + 0.045);
		cairo_line_to(cr, left + 0.08 + ll, top + 0.045);
		cairo_stroke(cr);
	}
	/* the front's shadow on them, and the front, a little lower than the
	 * back so the tab and the papers show */
	double fy = y0 + 0.31;
	cairo_rectangle(cr, x0, fy - 0.07, W + 0.03, 0.07);
	g = cairo_pattern_create_linear(0, fy - 0.07, 0, fy);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0.10, 0.06, 0.02, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.10, 0.06, 0.02, 0.30);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_rectangle(cr, x0, fy, W, y0 + H - fy);
	g = tool_flat(W * 0.5, H * 0.5, l, 0.79, 0.65, 0.42, 0.12);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	tool_edges(cr, x0, fy, W, y0 + H - fy, 0.0, l, px, 0.45, 0.30);
	/* where it folds, along the bottom */
	cairo_set_line_width(cr, 1.0 * px);
	cairo_set_source_rgba(cr, 1.0, 0.95, 0.82, 0.22);
	cairo_move_to(cr, x0 + 0.02, y0 + H - 0.075);
	cairo_line_to(cr, x0 + W - 0.02, y0 + H - 0.075);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 0.25, 0.16, 0.06, 0.22);
	cairo_move_to(cr, x0 + 0.02, y0 + H - 0.06);
	cairo_line_to(cr, x0 + W - 0.02, y0 + H - 0.06);
	cairo_stroke(cr);

	/* a paper clip over the edge, holding them in */
	cairo_save(cr);
	cairo_translate(cr, x0 + 1.52, fy - 0.10);
	cairo_rotate(cr, 0.06);
	for (int pass = 0; pass < 3; pass++) {
		cairo_save(cr);
		if (pass == 0) {
			/* its shadow */
			cairo_translate(cr, -l->x * 0.025, -l->y * 0.025);
			cairo_set_source_rgba(cr, 0.05, 0.03, 0.01, 0.35);
			cairo_set_line_width(cr, 0.032);
		} else if (pass == 1) {
			cairo_set_source_rgb(cr, 0.58, 0.59, 0.61);
			cairo_set_line_width(cr, 0.026);
		} else {
			cairo_translate(cr, l->x * 0.005, l->y * 0.005);
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.55);
			cairo_set_line_width(cr, 0.009);
		}
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_move_to(cr, 0.03, 0.14);
		cairo_line_to(cr, 0.03, -0.06);
		cairo_arc_negative(cr, 0.0, -0.06, 0.03, 0.0, M_PI);
		cairo_line_to(cr, -0.03, 0.24);
		cairo_arc_negative(cr, 0.015, 0.24, 0.045, M_PI, 0.0);
		cairo_line_to(cr, 0.06, -0.10);
		cairo_arc_negative(cr, 0.0, -0.10, 0.06, 0.0, M_PI);
		cairo_line_to(cr, -0.06, 0.16);
		cairo_stroke(cr);
		cairo_restore(cr);
	}
	cairo_restore(cr);
}

/* A pencil lying along x, its point at +x: rubber, ferrule, the painted
 * six-sided body, the sharpened wood and the lead. */
static void tool_pencil(cairo_t *cr, double px, const struct tool_light *l, double len)
{
	const double r = 0.085;
	double e0 = -len * 0.5, e1 = e0 + 0.12, f1 = e1 + 0.16, w0 = len * 0.5 - 0.34, t0 = len * 0.5 - 0.08;
	/* rubber */
	cairo_pattern_t *g = tool_round(0, r, l, 0.86, 0.50, 0.48, 0.10);
	cairo_set_source(cr, g);
	desk_rounded_path(cr, e0, -r * 0.96, e1 - e0 + 0.02, r * 1.92, r * 0.5);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* ferrule */
	g = tool_round(0, r * 1.04, l, 0.74, 0.71, 0.64, 0.55);
	cairo_set_source(cr, g);
	cairo_rectangle(cr, e1, -r * 1.04, f1 - e1, r * 2.08);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 0.9 * px);
	for (int i = 1; i < 4; i++) {
		double x = e1 + (f1 - e1) * i / 4.0;
		cairo_set_source_rgba(cr, 0.20, 0.18, 0.14, 0.45);
		cairo_move_to(cr, x, -r * 1.04);
		cairo_line_to(cr, x, r * 1.04);
		cairo_stroke(cr);
	}
	/* the body: three of its six faces, flat-lit */
	double up = TOOL_LAMP_UP, across = sqrt(1.0 - up * up);
	const double fy[4] = { -r, -r / 3.0, r / 3.0, r };
	const double fn[3] = { -0.866, 0.0, 0.866 };
	for (int f = 0; f < 3; f++) {
		double nz = f == 1 ? 1.0 : 0.5;
		double d = fmax(fn[f] * l->y * across + nz * up, 0.0);
		double lit = 0.34 + 0.76 * d;
		cairo_set_source_rgb(cr, fmin(0.95 * lit, 1.0), fmin(0.74 * lit, 1.0), 0.16 * lit);
		cairo_rectangle(cr, f1, fy[f], w0 - f1 + 0.01, fy[f + 1] - fy[f]);
		cairo_fill(cr);
	}
	cairo_set_line_width(cr, 0.8 * px);
	cairo_set_source_rgba(cr, 0.35, 0.22, 0.02, 0.35);
	for (int k = 1; k < 3; k++) {
		cairo_move_to(cr, f1, fy[k]);
		cairo_line_to(cr, w0, fy[k]);
		cairo_stroke(cr);
	}
	/* the sharpened wood, scalloped where the paint was cut */
	cairo_move_to(cr, w0, -r);
	cairo_curve_to(cr, w0 + 0.04, -r * 0.6, w0 - 0.02, -r * 0.2, w0 + 0.03, 0.0);
	cairo_curve_to(cr, w0 - 0.02, r * 0.2, w0 + 0.04, r * 0.6, w0, r);
	cairo_line_to(cr, t0, r * 0.26);
	cairo_line_to(cr, t0, -r * 0.26);
	cairo_close_path(cr);
	g = tool_round(0, r, l, 0.88, 0.72, 0.50, 0.05);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* the lead */
	cairo_move_to(cr, t0, -r * 0.26);
	cairo_line_to(cr, len * 0.5, 0.0);
	cairo_line_to(cr, t0, r * 0.26);
	cairo_close_path(cr);
	g = tool_round(0, r * 0.26, l, 0.26, 0.26, 0.28, 0.35);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

/* A reporter's notepad, written on, with its pencil lying across it. */
static void tool_notes(cairo_t *cr, double px, const struct tool_light *l, unsigned seed)
{
	const double W = 1.30, H = 1.76;
	double x0 = -W * 0.5 - 0.14, y0 = -H * 0.5;
	/* the leaves under the top one, showing at its edges */
	for (int i = 3; i >= 1; i--) {
		double t = 0.84 - i * 0.03;
		cairo_set_source_rgb(cr, t, t - 0.03, t - 0.10);
		cairo_rectangle(cr, x0 + 0.010 * i, y0 + 0.18 + 0.016 * i, W, H - 0.18);
		cairo_fill(cr);
	}
	/* the top sheet, ruled, with a margin */
	cairo_rectangle(cr, x0, y0 + 0.18, W, H - 0.18);
	cairo_pattern_t *g = tool_flat(W * 0.5, H * 0.5, l, 0.94, 0.92, 0.84, 0.05);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, fmax(0.9 * px, 0.008));
	int nl = 0;
	for (double y = y0 + 0.42; y < y0 + H - 0.06; y += 0.125, nl++) {
		cairo_set_source_rgba(cr, 0.52, 0.64, 0.82, 0.55);
		cairo_move_to(cr, x0 + 0.02, y);
		cairo_line_to(cr, x0 + W - 0.02, y);
		cairo_stroke(cr);
	}
	cairo_set_source_rgba(cr, 0.80, 0.32, 0.30, 0.50);
	cairo_move_to(cr, x0 + 0.22, y0 + 0.20);
	cairo_line_to(cr, x0 + 0.22, y0 + H - 0.02);
	cairo_stroke(cr);
	/* a few lines of writing */
	cairo_set_line_width(cr, fmax(1.0 * px, 0.010));
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_source_rgba(cr, 0.14, 0.18, 0.40, 0.78);
	for (int k = 0; k < 4 && k < nl; k++) {
		double y = y0 + 0.42 + k * 0.125 - 0.022;
		double xa = x0 + 0.28;
		double xb = xa + (W - 0.36) * (0.45 + 0.50 * hash21((seed % 211u) + k * 3.0, 9.1));
		bool pen = false;
		for (double x = xa; x < xb; x += 0.012) {
			double word = hash21(floor((x - xa) / 0.16) + k * 17.0, 2.0 + (seed % 7u));
			if (word < 0.18) {
				pen = false;   /* between words */
				continue;
			}
			double yy = y - fabs(sin((x - xa) * 55.0 + k)) * 0.035
				+ sin((x - xa) * 23.0) * 0.006;
			if (!pen) {
				cairo_move_to(cr, x, yy);
				pen = true;
			} else {
				cairo_line_to(cr, x, yy);
			}
		}
		cairo_stroke(cr);
	}
	/* the binding along the top: cloth tape over the leaves, two staples */
	cairo_rectangle(cr, x0 - 0.01, y0, W + 0.02, 0.22);
	g = tool_flat(W * 0.5, 0.11, l, 0.44, 0.15, 0.11, 0.15);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 0.7 * px);
	for (int i = 0; i < 9; i++) {
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.10);
		cairo_move_to(cr, x0, y0 + 0.02 + i * 0.022);
		cairo_line_to(cr, x0 + W, y0 + 0.02 + i * 0.022);
		cairo_stroke(cr);
	}
	tool_edges(cr, x0 - 0.01, y0, W + 0.02, 0.22, 0.0, l, px, 0.35, 0.35);
	for (int s = 0; s < 2; s++) {
		double sx = s ? x0 + W - 0.32 : x0 + 0.22;
		cairo_rectangle(cr, sx, y0 + 0.095, 0.12, 0.028);
		cairo_set_source_rgb(cr, 0.74, 0.74, 0.76);
		cairo_fill(cr);
	}
	tool_edges(cr, x0, y0 + 0.18, W, H - 0.18, 0.0, l, px, 0.30, 0.25);

	/* the pencil, across its right-hand side, and its shadow on the page */
	cairo_save(cr);
	cairo_translate(cr, 0.34, 0.16);
	cairo_rotate(cr, -1.08);
	struct tool_light pl = tool_light_turned(l, -1.08);
	const double len = 1.98, r = 0.085;
	for (int k = 2; k >= 1; k--) {
		double o = 0.035 * k;
		cairo_set_source_rgba(cr, 0.10, 0.07, 0.03, 0.10 + 0.10 * (2 - k));
		desk_rounded_path(cr, -len * 0.5 - pl.x * o, -r - pl.y * o, len, r * 2.0, r);
		cairo_fill(cr);
	}
	tool_pencil(cr, px, &pl, len);
	cairo_restore(cr);
}

/* A screwdriver: a turned wooden handle, a brass ferrule, the steel. */
static void tool_driver(cairo_t *cr, double px, const struct tool_light *l, unsigned seed)
{
	const double hr = 0.15;
	double h0 = -1.22, h1 = -0.18, f1 = -0.06, s1 = 1.10, tip = 1.22;
	/* the handle, rounded at its end, a little narrower at the ferrule */
	cairo_move_to(cr, h0 + hr, -hr);
	cairo_curve_to(cr, h0 + 0.5, -hr * 1.04, h1 - 0.2, -hr * 0.98, h1, -hr * 0.84);
	cairo_line_to(cr, h1, hr * 0.84);
	cairo_curve_to(cr, h1 - 0.2, hr * 0.98, h0 + 0.5, hr * 1.04, h0 + hr, hr);
	cairo_arc(cr, h0 + hr, 0.0, hr, M_PI * 0.5, M_PI * 1.5);
	cairo_close_path(cr);
	cairo_pattern_t *g = tool_round(0, hr, l, 0.62, 0.33, 0.14, 0.35);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_save(cr);
	cairo_clip(cr);
	/* its grain, and the flutes turned into it */
	cairo_set_line_width(cr, 0.8 * px);
	for (int i = 0; i < 9; i++) {
		double y = -hr + hr * 2.0 * hash21(i * 3.1 + (seed % 13u), 7.3);
		cairo_set_source_rgba(cr, 0.18, 0.08, 0.02, 0.10 + 0.08 * hash21(i, 1.1));
		cairo_move_to(cr, h0, y);
		cairo_curve_to(cr, h0 + 0.35, y + 0.02, h0 + 0.7, y - 0.02, h1, y + 0.01);
		cairo_stroke(cr);
	}
	cairo_set_line_width(cr, 1.4 * px);
	for (int k = -1; k <= 1; k += 2) {
		cairo_set_source_rgba(cr, 0.12, 0.05, 0.01, 0.30);
		cairo_move_to(cr, h0 + 0.18, k * hr * 0.42);
		cairo_line_to(cr, h1 - 0.05, k * hr * 0.40);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
	/* the ferrule */
	g = tool_round(0, 0.10, l, 0.78, 0.62, 0.30, 0.55);
	cairo_set_source(cr, g);
	cairo_rectangle(cr, h1 - 0.01, -0.10, f1 - h1 + 0.01, 0.20);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* the shaft and the blade */
	g = tool_round(0, 0.040, l, 0.66, 0.68, 0.71, 0.85);
	cairo_set_source(cr, g);
	cairo_rectangle(cr, f1, -0.040, s1 - f1, 0.080);
	cairo_fill(cr);
	cairo_move_to(cr, s1, -0.040);
	cairo_line_to(cr, tip - 0.02, -0.058);
	cairo_line_to(cr, tip, -0.052);
	cairo_line_to(cr, tip, 0.052);
	cairo_line_to(cr, tip - 0.02, 0.058);
	cairo_line_to(cr, s1, 0.040);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 0.8 * px);
	cairo_set_source_rgba(cr, 0.15, 0.15, 0.16, 0.55);
	cairo_move_to(cr, s1 + 0.02, 0.0);
	cairo_line_to(cr, tip, 0.0);
	cairo_stroke(cr);
}

/* How thick each lies, in room units: what its shadow is reckoned from. */
static const double TOOL_HEIGHT[DESK_DOCK_COUNT] = { 0.15, 0.24, 0.05, 0.13, 0.28 };

/* Its box, unturned, about its middle: x0, y0, x1, y1. */
static const double TOOL_BOX[DESK_DOCK_COUNT][4] = {
	{ -1.16, -0.59, 1.16, 0.59 },
	{ -0.92, -1.33, 0.92, 0.92 },
	{ -1.01, -0.72, 1.05, 0.72 },
	{ -0.81, -0.90, 0.88, 1.06 },
	{ -1.23, -0.16, 1.23, 0.16 },
};

/* How it happens to lie: turned a little, the same way every time for the
 * same seed. The compass, being round, lies any way at all. */
static double tool_turn(int look, unsigned seed)
{
	static const double base[DESK_DOCK_COUNT] = { -0.05, 0.0, -0.04, 0.05, -0.50 };
	double j = hash21((double)(seed % 977u), 7.7) - 0.5;
	if (look == DESK_ICON_GLOBE) {
		return j * 1.6;
	}
	return base[look] + j * 0.12;
}

/* One of them as a picture at ppu pixels to the unit, and its middle. */
static cairo_surface_t *tool_image(int look, unsigned seed, double ppu, double *ax, double *ay)
{
	double a = tool_turn(look, seed);
	double c = cos(a), s = sin(a);
	const double *b = TOOL_BOX[look];
	double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
	for (int k = 0; k < 4; k++) {
		double x = k & 1 ? b[2] : b[0], y = k & 2 ? b[3] : b[1];
		double rx = x * c - y * s, ry = x * s + y * c;
		minx = fmin(minx, rx);
		maxx = fmax(maxx, rx);
		miny = fmin(miny, ry);
		maxy = fmax(maxy, ry);
	}
	double m = 0.04 + 2.0 / ppu;
	int w = (int)ceil((maxx - minx + 2.0 * m) * ppu);
	int h = (int)ceil((maxy - miny + 2.0 * m) * ppu);
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w > 1 ? w : 1,
		h > 1 ? h : 1);
	cairo_t *cr = cairo_create(img);
	*ax = (-minx + m) * ppu;
	*ay = (-miny + m) * ppu;
	cairo_translate(cr, *ax, *ay);
	cairo_scale(cr, ppu, ppu);
	cairo_rotate(cr, a);
	struct tool_light l0 = { TOOL_LAMP_X, TOOL_LAMP_Y };
	struct tool_light l = tool_light_turned(&l0, a);
	double px = 1.0 / ppu;
	switch (look) {
	case DESK_ICON_TERM:
		tool_term(cr, px, &l, seed);
		break;
	case DESK_ICON_GLOBE:
		/* north a little to the left of straight up the screen */
		tool_compass(cr, px, &l, -0.14 - a);
		break;
	case DESK_ICON_FOLDER:
		tool_folder(cr, px, &l, seed);
		break;
	case DESK_ICON_NOTES:
		tool_notes(cr, px, &l, seed);
		break;
	default:
		tool_driver(cr, px, &l, seed);
		break;
	}
	cairo_destroy(cr);
	/* the same grain as everything else in the room, on it alone */
	cr = cairo_create(img);
	cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);
	overlay_grain(cr, 0, 0, w, h, 0.45);
	cairo_destroy(cr);
	cairo_surface_flush(img);
	return img;
}

/* Kept, at the size the drawer wants them: a slide along the row draws them
 * every frame, and they are smaller or larger copies of this. */
struct tool_kept {
	int look;
	unsigned seed;
	double ppu;
	cairo_surface_t *s;
	double ax, ay;
	unsigned used;
};
static struct tool_kept g_tool_kept[12];
static unsigned g_tool_clock;

static cairo_surface_t *tool_image_kept(int look, unsigned seed, double ppu, double *ax,
	double *ay)
{
	struct tool_kept *slot = &g_tool_kept[0];
	for (size_t i = 0; i < sizeof g_tool_kept / sizeof g_tool_kept[0]; i++) {
		struct tool_kept *k = &g_tool_kept[i];
		if (k->s && k->look == look && k->seed == seed && fabs(k->ppu - ppu) < 0.01) {
			k->used = ++g_tool_clock;
			*ax = k->ax;
			*ay = k->ay;
			return cairo_surface_reference(k->s);
		}
		if (!k->s || k->used < slot->used) {
			slot = k;
		}
	}
	if (slot->s) {
		cairo_surface_destroy(slot->s);
	}
	slot->s = tool_image(look, seed, ppu, &slot->ax, &slot->ay);
	slot->look = look;
	slot->seed = seed;
	slot->ppu = ppu;
	slot->used = ++g_tool_clock;
	*ax = slot->ax;
	*ay = slot->ay;
	return cairo_surface_reference(slot->s);
}

/* The row. Things are laid along the felt at the depth the five were
 * traced at, a pitch apart, the row centred and closing up as it fills;
 * past what fits at full size, they are drawn smaller. x is in room units
 * across the drawer, 0 its middle. */
static double drawer_pitch(int m)
{
	double room = 2.0 * (RM_DR_HX - 0.42 - 0.40);
	return m > 0 ? fmin(DRAWER_PITCH, room / m) : DRAWER_PITCH;
}

double desk_drawer_pitch(int m)
{
	return drawer_pitch(m);
}

double desk_drawer_slot_x(int k, int m)
{
	return (k - (m - 1) * 0.5) * drawer_pitch(m);
}

double desk_drawer_fit(int m)
{
	return fmin(1.0, drawer_pitch(m) / DRAWER_PITCH * 1.08);
}

/* A point on the row, the drawer out, in the sheet's pixels; and how many
 * of them a room unit is there. */
static bool drawer_felt_px(const struct room_render *rr, double x, double *sx, double *sy,
	double *ppu)
{
	double u0, v0, u1, v1;
	double z = RM_ITEM_Z + RM_DR_PULL;
	if (!room_project(&rr->cam, v3(x, DRAWER_FELT_Y, z), &u0, &v0)
		|| !room_project(&rr->cam, v3(x + 1.0, DRAWER_FELT_Y, z), &u1, &v1)) {
		return false;
	}
	*sx = u0 * rr->w;
	*sy = v0 * rr->h;
	*ppu = fabs(u1 - u0) * rr->w;
	return *ppu > 1e-6;
}

/* The felt inside the drawer's walls, in the sheet's pixels: nothing laid
 * in it goes up them or over the face. */
static bool drawer_felt_rect(const struct room_render *rr, double box[4])
{
	double in_hx = RM_DR_HX - 0.42 - 0.22;
	double zb = RM_DR_BACK + RM_DR_PULL, zf = RM_DR_FACE - 0.52 + RM_DR_PULL;
	double u[4], v[4];
	for (int k = 0; k < 4; k++) {
		vec3 p = v3(k & 1 ? in_hx : -in_hx, DRAWER_FELT_Y, k & 2 ? zf : zb);
		if (!room_project(&rr->cam, p, &u[k], &v[k])) {
			return false;
		}
	}
	/* the narrower of the two ends: the walls lean in towards the back */
	double x0 = fmax(u[0], u[2]) * rr->w, x1 = fmin(u[1], u[3]) * rr->w;
	double y0 = fmin(v[0], v[1]) * rr->h, y1 = fmax(v[2], v[3]) * rr->h;
	box[0] = floor(x0);
	box[1] = floor(y0);
	box[2] = ceil(x1) - box[0];
	box[3] = ceil(y1) - box[1];
	return box[2] > 1.0 && box[3] > 1.0;
}

bool desk_drawer_felt_box(int screen_w, int screen_h, double box[4])
{
	struct room_xf xf;
	return room_xform(screen_w, screen_h, 1.0, &xf) && drawer_felt_rect(xf.rr, box);
}

/* A shadow on the felt the shape of what casts it: its alpha, softened
 * by going down to a fraction of its size and back, and laid down where
 * the lamp puts it. */
static void drawer_shadow(cairo_t *cr, cairo_surface_t *src, double x, double y, double sw,
	double sh, double dx, double dy, double alpha, double blur, double ks)
{
	if (!gfx_shadows()) {
		return;
	}
	/* sw, sh, x, y in cr's units; src has ks pixels to each of them */
	int qw = (int)ceil(sw / blur) + 4, qh = (int)ceil(sh / blur) + 4;
	cairo_surface_t *m = cairo_image_surface_create(CAIRO_FORMAT_A8, qw, qh);
	cairo_t *mc = cairo_create(m);
	cairo_translate(mc, 2.0, 2.0);
	cairo_scale(mc, 1.0 / (blur * ks), 1.0 / (blur * ks));
	cairo_set_source_surface(mc, src, 0, 0);
	cairo_paint(mc);
	cairo_destroy(mc);
	cairo_surface_flush(m);
	cairo_save(cr);
	cairo_translate(cr, x + dx - 2.0 * blur, y + dy - 2.0 * blur);
	cairo_scale(cr, blur, blur);
	cairo_set_source_rgba(cr, 0.02, 0.025, 0.015, alpha);
	cairo_pattern_t *pat = cairo_pattern_create_for_surface(m);
	cairo_pattern_set_filter(pat, CAIRO_FILTER_GOOD);
	cairo_mask(cr, pat);
	cairo_pattern_destroy(pat);
	cairo_restore(cr);
	cairo_surface_destroy(m);
}

/* Where the room's lamp throws the shadow of a point `up` room units over
 * the felt at x along the row: how far off it, in pixels. The lamp is high
 * and behind, so a thing lying flat throws a short shadow toward you and
 * away from the lamp's side. */
static void drawer_shadow_dir(double x, double up, double ppu, double *dx, double *dy)
{
	vec3 lp = ROOM_LAMP_POS;
	double ddx = x - lp.x, ddz = (RM_ITEM_Z + RM_DR_PULL) - lp.z;
	double high = fmax(lp.y - DRAWER_FELT_Y, 1.0);
	*dx = ddx / high * up * ppu;
	*dy = ddz / high * up * ppu;
}

/* How thick a thing lies, which is how long its shadow is. */
static double drawer_item_height(const struct desk_drawer_item *it)
{
	if (it->look >= 0 && it->look < DESK_DOCK_COUNT) {
		return TOOL_HEIGHT[it->look];
	}
	return it->look == DESK_LOOK_CAMERA ? 0.32 : it->look == DESK_LOOK_THING ? it->thick
		: it->look == DESK_LOOK_PICTURE ? it->thick
		: it->look == DESK_LOOK_NOTEBOOK ? 0.18
		: it->look == DESK_LOOK_PACKET ? 0.12 : 0.03;
}

/* Print's camera lying in the drawer, ppu pixels to the room's unit, turned
 * so, and its middle in the picture (see the camera, below); NULL if its
 * picture could not be read. */
static cairo_surface_t *cam_top_lying(double ppu, double turn, double *ax, double *ay);

/* How lit the felt is at (x, y) on the drawer sheet, against its middle:
 * the drawer's lamp falls off toward its ends, and what lies there is in the
 * same light. */
static double drawer_felt_lum(const struct room_render *rr, double x, double y)
{
	cairo_surface_t *s = rr->drawer;
	if (!s) {
		return 0.0;
	}
	cairo_surface_flush(s);
	const unsigned char *px = cairo_image_surface_get_data(s);
	int stride = cairo_image_surface_get_stride(s);
	int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
	double sum = 0.0;
	int n = 0;
	for (int dy = -4; dy <= 4; dy++) {
		for (int dx = -4; dx <= 4; dx++) {
			int xx = (int)x + dx * 2, yy = (int)y + dy * 2;
			if (xx < 0 || yy < 0 || xx >= w || yy >= h) {
				continue;
			}
			uint32_t p = ((const uint32_t *)(px + (size_t)yy * stride))[xx];
			if ((p >> 24) == 0) {
				continue;
			}
			sum += 0.2126 * ((p >> 16) & 255) + 0.7152 * ((p >> 8) & 255) + 0.0722 * (p & 255);
			n++;
		}
	}
	return n ? sum / n : 0.0;
}

static double drawer_light_at(struct room_render *rr, double sx, double sy)
{
	if (rr->felt_mid <= 0.0) {
		double mx, my, ppu;
		if (!drawer_felt_px(rr, 0.0, &mx, &my, &ppu)) {
			return 1.0;
		}
		rr->felt_mid = fmax(drawer_felt_lum(rr, mx, my), 1.0);
	}
	double l = drawer_felt_lum(rr, sx, sy);
	return l > 0.0 ? clampf(l / rr->felt_mid, 0.55, 1.0) : 1.0;
}

/* What one thing looks like lying in the drawer, as a picture with an
 * anchor (where its middle is), at ppu pixels to a room unit. */
static cairo_surface_t *drawer_item_image(struct room_render *rr, const struct desk_drawer_item *it,
	double ppu, double dev, double *ax, double *ay, bool exact)
{
	if (it->look >= 0 && it->look < DESK_DOCK_COUNT) {
		if (exact) {
			return tool_image(it->look, it->seed, ppu, ax, ay);
		}
		/* drawn once at the most the drawer asks for -- standing up off
		 * the felt, as finely as the screen shows it (dev of its pixels
		 * to one of the sheet's) -- and made smaller from that */
		double sppu, sx, sy;
		if (!drawer_felt_px(rr, 0.0, &sx, &sy, &sppu)) {
			return NULL;
		}
		double base = ceil(sppu * 1.06 * fmax(dev, 1.0) * 4.0) / 4.0;
		double kax, kay;
		cairo_surface_t *src = tool_image_kept(it->look, it->seed, base, &kax, &kay);
		double k = ppu / base;
		if (fabs(k - 1.0) < 1e-3) {
			*ax = kax;
			*ay = kay;
			return src;
		}
		int w = (int)ceil(cairo_image_surface_get_width(src) * k);
		int h = (int)ceil(cairo_image_surface_get_height(src) * k);
		cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w > 1 ? w : 1,
			h > 1 ? h : 1);
		cairo_t *cr = cairo_create(img);
		cairo_scale(cr, k, k);
		cairo_set_source_surface(cr, src, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_destroy(cr);
		cairo_surface_flush(img);
		cairo_surface_destroy(src);
		*ax = kax * k;
		*ay = kay * k;
		return img;
	}
	double turn = (hash21((double)(it->seed % 977u), 4.1) - 0.5) * 0.14;
	if (it->look == DESK_LOOK_CAMERA) {
		cairo_surface_t *img = cam_top_lying(ppu, turn, ax, ay);
		if (img) {
			return img;
		}
	}
	if (it->look == DESK_LOOK_THING && it->lying) {
		cairo_surface_t *img = it->lying(ppu, turn, ax, ay);
		if (img) {
			return img;
		}
	}
	if (it->look == DESK_LOOK_NOTEBOOK) {
		cairo_surface_t *img = desk_notebook_top_lying(ppu, turn, ax, ay);
		if (img) {
			return img;
		}
	}
	if (it->look == DESK_LOOK_PICTURE && it->picture && it->along > 0.0) {
		/* a picture to look like: as long as it is, turned as it happens to lie */
		double pw = cairo_image_surface_get_width(it->picture);
		double ph = cairo_image_surface_get_height(it->picture);
		double k = it->along * ppu / fmax(pw, ph);
		double diag = hypot(pw, ph) * k + 4.0;
		cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
			(int)ceil(diag), (int)ceil(diag));
		cairo_t *cr = cairo_create(img);
		cairo_translate(cr, diag * 0.5, diag * 0.5);
		cairo_rotate(cr, turn);
		cairo_scale(cr, k, k);
		cairo_translate(cr, -pw * 0.5, -ph * 0.5);
		cairo_set_source_surface(cr, it->picture, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_destroy(cr);
		cairo_surface_flush(img);
		*ax = *ay = diag * 0.5;
		return img;
	}
	if (it->look == DESK_LOOK_PACKET && it->packet) {
		/* a sheet folded away: as it was folded, smaller */
		double pw = cairo_image_surface_get_width(it->packet);
		double ph = cairo_image_surface_get_height(it->packet);
		double k = 2.05 * ppu / fmax(pw, ph);
		double w = pw * k, h = ph * k;
		double diag = hypot(w, h) + 4.0;
		cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
			(int)ceil(diag), (int)ceil(diag));
		cairo_t *cr = cairo_create(img);
		cairo_translate(cr, diag * 0.5, diag * 0.5);
		cairo_rotate(cr, turn);
		cairo_translate(cr, -w * 0.5, -h * 0.5);
		cairo_scale(cr, k, k);
		cairo_set_source_surface(cr, it->packet, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_destroy(cr);
		cairo_surface_flush(img);
		*ax = *ay = diag * 0.5;
		return img;
	}
	/* a card with the program's mark on it, and its name under that */
	double cw = 1.95 * ppu, ch = 1.42 * ppu;
	double diag = hypot(cw, ch) + 4.0;
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		(int)ceil(diag), (int)ceil(diag));
	cairo_t *cr = cairo_create(img);
	cairo_translate(cr, diag * 0.5, diag * 0.5);
	cairo_rotate(cr, turn);
	cairo_translate(cr, -cw * 0.5, -ch * 0.5);
	fill_paper(cr, 0, 0, cw, ch, ch * 0.05);
	cairo_save(cr);
	desk_rounded_path(cr, 0, 0, cw, ch, ch * 0.05);
	cairo_clip(cr);
	paper_age(cr, cw, ch, it->seed, 0.25);
	double is = ch * 0.56, ix = (cw - is) * 0.5, iy = ch * 0.09;
	if (it->icon) {
		double iw = cairo_image_surface_get_width(it->icon);
		double ih = cairo_image_surface_get_height(it->icon);
		double k = is / fmax(iw, ih);
		cairo_save(cr);
		cairo_translate(cr, ix + (is - iw * k) * 0.5, iy + (is - ih * k) * 0.5);
		cairo_scale(cr, k, k);
		cairo_set_source_surface(cr, it->icon, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		cairo_set_source_rgba(cr, 0.22, 0.17, 0.13, 0.16);
		desk_rounded_path(cr, ix, iy, is, is, is * 0.18);
		cairo_fill(cr);
		show_text_as(cr, DESK_FONT_ENGRAVE, it->initial[0] ? it->initial : "?",
			ix + is * 0.5, iy + is * 0.76,
			is * 0.70, true, 0.26, 0.19, 0.14, 0.88, 0);
	}
	show_text_fit_as(cr, DESK_FONT_ENGRAVE, it->name, cw * 0.5, ch * 0.88, ch * 0.13, true,
		0.17, 0.14, 0.11, 0.92,
		0, cw * 0.88);
	cairo_restore(cr);
	cairo_destroy(cr);
	cairo_surface_flush(img);
	*ax = *ay = diag * 0.5;
	return img;
}

/* One thing, its shadow first, its middle at (x, y) on the felt; lifted
 * (hover) it stands a little proud of the felt, its shadow goes wider, and
 * what it is is written on the felt under it. */
static void drawer_lay(cairo_t *cr, struct room_render *rr, const struct desk_drawer_item *it,
	double x, double y, double ppu, double wx, bool lifted)
{
	double ax, ay;
	double k = lifted ? 1.06 : 1.0;
	/* Drawn as fine as it will be shown: the sheet it lies on is the
	 * room's, and the room is magnified to the screen (and the screen may
	 * be a fine one). Drawn at the sheet's own size it was a picture of
	 * the icon blown up -- soft, until it was picked up and drawn again
	 * at the screen's. */
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double ks = clampf(hypot(kx, ky), 1.0, 4.0);
	cairo_surface_t *img = drawer_item_image(rr, it, ppu * k * ks, ks, &ax, &ay, false);
	if (!img) {
		return;
	}
	double iw = cairo_image_surface_get_width(img) / ks, ih = cairo_image_surface_get_height(img) / ks;
	ax /= ks;
	ay /= ks;
	/* Two shadows: where it touches the felt, tight and dark, and the
	 * lamp's, as long as it is thick and softer the higher it stands. */
	double hgt = drawer_item_height(it) + (lifted ? 0.30 : 0.0);
	double dx, dy;
	drawer_shadow_dir(wx, hgt, ppu, &dx, &dy);
	double up = lifted ? 0.10 * ppu : 0.0;
	if (!lifted) {
		drawer_shadow(cr, img, x - ax, y - ay, iw, ih, dx * 0.25, dy * 0.25, 0.50,
			fmax(1.3, 0.025 * ppu), ks);
	}
	drawer_shadow(cr, img, x - ax, y - ay, iw, ih, dx, dy, lifted ? 0.30 : 0.34,
		fmax(2.0, (0.04 + hgt * 0.30) * ppu), ks);
	cairo_save(cr);
	cairo_translate(cr, x - ax, y - ay - up);
	cairo_scale(cr, 1.0 / ks, 1.0 / ks);
	cairo_set_source_surface(cr, img, 0, 0);
	cairo_paint(cr);
	/* and in the felt's light where it lies */
	double light = drawer_light_at(rr, x, y);
	if (light < 0.985) {
		cairo_set_source_rgba(cr, 0.02, 0.02, 0.01, 1.0 - light);
		cairo_mask_surface(cr, img, 0, 0);
	}
	cairo_restore(cr);
	cairo_surface_destroy(img);
	if (lifted && it->name[0]) {
		/* chalked on the felt, the way a tailor marks cloth */
		double fs = fmax(0.36 * ppu, 9.0);
		double ww = word_width(cr, it->name, fs);
		double room = DRAWER_PITCH * ppu * 1.6;
		if (ww > room) {
			fs *= fmax(room / fmax(ww, 1.0), 0.62);
			ww = word_width(cr, it->name, fs);
		}
		print_word(cr, it->name, x - ww * 0.5, y + 1.18 * ppu, fs, 0.93, 0.91, 0.84, 0.62);
	}
}

void desk_drawer_paint_items(cairo_t *cr, int screen_w, int screen_h,
	const struct desk_drawer_item *items, int n, const double *x, const double *fit,
	int hover)
{
	struct room_xf xf;
	double felt[4];
	if (!room_xform(screen_w, screen_h, 1.0, &xf) || !drawer_felt_rect(xf.rr, felt)) {
		return;
	}
	struct room_render *rr = xf.rr;
	cairo_save(cr);
	cairo_rectangle(cr, felt[0], felt[1], felt[2], felt[3]);
	cairo_clip(cr);
	/* the one lifted last, so it is over its neighbours */
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < n; i++) {
			if ((i == hover) != (pass == 1)) {
				continue;
			}
			double sx, sy, ppu;
			if (!drawer_felt_px(rr, x[i], &sx, &sy, &ppu)) {
				continue;
			}
			drawer_lay(cr, rr, &items[i], sx, sy, ppu * fit[i], x[i], i == hover);
		}
	}
	cairo_restore(cr);
}

bool desk_drawer_at(int screen_w, int screen_h, double open, double px, double py,
	double *x, bool *row)
{
	struct room_xf xf;
	double felt[4];
	*row = false;
	if (open < 0.35 || !room_xform(screen_w, screen_h, open, &xf)
		|| !drawer_felt_rect(xf.rr, felt) || py < xf.clip) {
		return false;
	}
	double sx, sy, ppu;
	if (!drawer_felt_px(xf.rr, 0.0, &sx, &sy, &ppu)) {
		return false;
	}
	double s = xf.scale;
	*x = (px - (xf.ox + sx * s)) / (ppu * s);
	double fx0 = xf.ox + felt[0] * s, fx1 = xf.ox + (felt[0] + felt[2]) * s;
	if (px < fx0 || px > fx1) {
		return false;
	}
	double cy = xf.ny + sy * s;
	*row = fabs(py - cy) <= 1.25 * ppu * s;
	return true;
}

bool desk_drawer_point(int screen_w, int screen_h, double open, double x, double *sx,
	double *sy)
{
	struct room_xf xf;
	double fx, fy, ppu;
	if (!room_xform(screen_w, screen_h, open, &xf) || !drawer_felt_px(xf.rr, x, &fx, &fy, &ppu)) {
		return false;
	}
	*sx = xf.ox + fx * xf.scale;
	*sy = xf.ny + fy * xf.scale;
	return true;
}

cairo_surface_t *desk_drawer_item_picture(int screen_w, int screen_h,
	const struct desk_drawer_item *it, double *ax, double *ay)
{
	struct room_xf xf;
	if (!room_xform(screen_w, screen_h, 1.0, &xf)) {
		return NULL;
	}
	double sx, sy, ppu;
	if (!drawer_felt_px(xf.rr, 0.0, &sx, &sy, &ppu)) {
		return NULL;
	}
	/* held up, nearer the eye, and its shadow on whatever is under it */
	double sppu = ppu * xf.scale;
	double iax, iay;
	cairo_surface_t *img = drawer_item_image(xf.rr, it, sppu * 1.10, 1.0, &iax, &iay, true);
	if (!img) {
		return NULL;
	}
	double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
	double dx, dy;
	drawer_shadow_dir(0.0, drawer_item_height(it) + 1.2, sppu, &dx, &dy);
	double blur = 7.0, m = blur * 3.0;
	double pl = m + fmax(-dx, 0.0), pt = m + fmax(-dy, 0.0);
	int w = (int)ceil(iw + pl + m + fmax(dx, 0.0)), h = (int)ceil(ih + pt + m + fmax(dy, 0.0));
	cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(out);
	drawer_shadow(cr, img, pl, pt, iw, ih, dx, dy, 0.30, blur, 1.0);
	cairo_set_source_surface(cr, img, pl, pt);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(out);
	cairo_surface_destroy(img);
	*ax = iax + pl;
	*ay = iay + pt;
	return out;
}

/* ---- the card of looks ---------------------------------------------
 *
 * Up out of the drawer over a program kept there, when it is asked for with
 * the other button (wm.c): a card from a catalogue, the program's name at its
 * head, and on it each thing the program could look like lying in the drawer,
 * as it would lie there and named under it, the one it looks like now ringed
 * in pencil. At its foot, where the person's own pictures go. In points. */

#define LOOKS_CELL_W 108.0
#define LOOKS_CELL_H 100.0
#define LOOKS_PIC_H 66.0
#define LOOKS_MARGIN 20.0
#define LOOKS_HEAD 54.0
#define LOOKS_FOOT 40.0

static void looks_grid(int n, int want, int *cols, int *rows)
{
	want = want > 0 ? want : 5;
	*cols = n < want ? (n > 0 ? n : 1) : want;
	*rows = (n + *cols - 1) / *cols;
	*rows = *rows > 0 ? *rows : 1;
}

void desk_looks_card_size(int n, int cols, double pt, int *w, int *h)
{
	int rows;
	looks_grid(n, cols, &cols, &rows);
	*w = (int)ceil((LOOKS_MARGIN * 2.0 + cols * LOOKS_CELL_W) * pt);
	*h = (int)ceil((LOOKS_HEAD + rows * LOOKS_CELL_H + LOOKS_FOOT) * pt);
}

static void looks_cell(int i, int n, int cols, double pt, double *x, double *y)
{
	int rows;
	looks_grid(n, cols, &cols, &rows);
	*x = (LOOKS_MARGIN + (i % cols) * LOOKS_CELL_W) * pt;
	*y = (LOOKS_HEAD + (i / cols) * LOOKS_CELL_H) * pt;
}

int desk_looks_card_hit(int n, int cols, double pt, double x, double y)
{
	for (int i = 0; i < n; i++) {
		double cx, cy;
		looks_cell(i, n, cols, pt, &cx, &cy);
		if (x >= cx && x < cx + LOOKS_CELL_W * pt && y >= cy && y < cy + LOOKS_CELL_H * pt) {
			return i;
		}
	}
	return -1;
}

/* How far into a picture what is in it goes, from each side (its alpha). */
static void alpha_box(cairo_surface_t *img, double *x0, double *y0, double *x1, double *y1)
{
	cairo_surface_flush(img);
	int w = cairo_image_surface_get_width(img), h = cairo_image_surface_get_height(img);
	int stride = cairo_image_surface_get_stride(img);
	const unsigned char *px = cairo_image_surface_get_data(img);
	int l = w, t = h, r = -1, b = -1;
	for (int y = 0; y < h; y++) {
		const uint32_t *row = (const uint32_t *)(px + (size_t)y * stride);
		for (int x = 0; x < w; x++) {
			if ((row[x] >> 24) > 24) {
				l = x < l ? x : l;
				r = x > r ? x : r;
				t = y < t ? y : t;
				b = y > b ? y : b;
			}
		}
	}
	if (r < 0) {
		*x0 = *y0 = 0.0;
		*x1 = w;
		*y1 = h;
		return;
	}
	*x0 = l;
	*y0 = t;
	*x1 = r + 1;
	*y1 = b + 1;
}

/* A pencil's ring round a word or a thing: once round and a little past, not quite true. */
static void pencil_ring(cairo_t *cr, double cx, double cy, double rx, double ry, unsigned seed)
{
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	double a0 = -2.2 + hash21((double)seed, 1.7) * 0.6;
	int steps = 64;
	for (int k = 0; k < steps; k++) {
		double t0 = (double)k / steps, t1 = (double)(k + 1) / steps;
		double a = a0 + t0 * 2.0 * M_PI * 1.08, b = a0 + t1 * 2.0 * M_PI * 1.08;
		/* wandering out as it goes round, and lighter where it was pressed less */
		double w0 = 1.0 + 0.05 * t0 + 0.02 * sin(a * 3.0 + seed);
		double w1 = 1.0 + 0.05 * t1 + 0.02 * sin(b * 3.0 + seed);
		cairo_set_line_width(cr, 1.6 + 0.6 * sin(M_PI * t0));
		cairo_set_source_rgba(cr, 0.24, 0.24, 0.26, 0.55 + 0.25 * sin(M_PI * t0));
		cairo_move_to(cr, cx + cos(a) * rx * w0, cy + sin(a) * ry * w0);
		cairo_line_to(cr, cx + cos(b) * rx * w1, cy + sin(b) * ry * w1);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

/* The one more on the card: a picture to be chosen from anywhere, a frame pencilled in with nothing
 * in it yet. */
static void looks_more(cairo_t *cr, double cx, double cy, double w, double h, double pt, bool hot)
{
	cairo_save(cr);
	double a = hot ? 0.85 : 0.62;
	cairo_set_source_rgba(cr, 0.26, 0.24, 0.22, a);
	cairo_set_line_width(cr, 1.3 * pt);
	double dash[2] = { 4.0 * pt, 3.0 * pt };
	cairo_set_dash(cr, dash, 2, 0.0);
	desk_rounded_path(cr, cx - w * 0.5, cy - h * 0.5, w, h, 4.0 * pt);
	cairo_stroke(cr);
	cairo_set_dash(cr, NULL, 0, 0.0);
	/* a little picture in it: hills and a sun */
	double s = fmin(w, h) * 0.30;
	cairo_set_line_width(cr, 1.5 * pt);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(cr, cx - s, cy + s * 0.55);
	cairo_line_to(cr, cx - s * 0.35, cy - s * 0.15);
	cairo_line_to(cr, cx + s * 0.05, cy + s * 0.25);
	cairo_line_to(cr, cx + s * 0.45, cy - s * 0.05);
	cairo_line_to(cr, cx + s, cy + s * 0.55);
	cairo_stroke(cr);
	cairo_new_sub_path(cr);
	cairo_arc(cr, cx + s * 0.45, cy - s * 0.55, s * 0.18, 0.0, 2.0 * M_PI);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void desk_looks_card_paint(cairo_t *cr, const struct desk_drawer_item *items, const char *const *names,
	int n, const char *more, int cols, double pt, const char *title, const char *hint, int chosen, int hover)
{
	int W, H, cells = n + (more ? 1 : 0);
	desk_looks_card_size(cells, cols, pt, &W, &H);
	double r = 5.0 * pt;
	/* its shadow on what is under it, and the card */
	cairo_save(cr);
	for (int k = 6; k >= 1; k--) {
		desk_rounded_path(cr, 1.5 * pt * k * 0.5, 3.0 * pt + k * 0.6 * pt, W - pt * k, H - 2.0 * pt, r + k);
		cairo_set_source_rgba(cr, 0.05, 0.03, 0.02, 0.035);
		cairo_fill(cr);
	}
	cairo_restore(cr);
	double cw = W - 6.0 * pt, ch = H - 8.0 * pt;
	cairo_save(cr);
	cairo_translate(cr, 3.0 * pt, 2.0 * pt);
	fill_paper(cr, 0, 0, cw, ch, r);
	cairo_save(cr);
	desk_rounded_path(cr, 0, 0, cw, ch, r);
	cairo_clip(cr);
	paper_age(cr, cw, ch, 41u, 0.15);
	cairo_restore(cr);
	/* the head: the program's name, and under it the card's own red rule */
	show_text_fit_as(cr, DESK_FONT_ENGRAVE, title, LOOKS_MARGIN * pt, 32.0 * pt, 19.0 * pt, true,
		0.20, 0.15, 0.11, 0.92, -1, cw - LOOKS_MARGIN * 2.0 * pt);
	cairo_set_source_rgba(cr, 0.80, 0.30, 0.28, 0.55);
	cairo_set_line_width(cr, 1.1 * pt);
	cairo_move_to(cr, 8.0 * pt, 44.0 * pt);
	cairo_line_to(cr, cw - 8.0 * pt, 44.0 * pt);
	cairo_stroke(cr);
	cairo_restore(cr);
	/* each look, lying on it */
	for (int i = 0; i < cells; i++) {
		double x, y;
		looks_cell(i, cells, cols, pt, &x, &y);
		x += 3.0 * pt;
		y += 2.0 * pt;
		if (i == n) {
			looks_more(cr, x + LOOKS_CELL_W * pt * 0.5, y + (8.0 + LOOKS_PIC_H * 0.5) * pt, (LOOKS_CELL_W - 30.0) * pt,
				(LOOKS_PIC_H - 6.0) * pt, pt, i == hover);
			show_text_fit_as(cr, DESK_FONT_PRINT, more, x + LOOKS_CELL_W * pt * 0.5, y + (LOOKS_PIC_H + 28.0) * pt,
				11.5 * pt, false, 0.18, 0.15, 0.12, i == hover ? 0.98 : 0.80, 0, (LOOKS_CELL_W - 10.0) * pt);
			continue;
		}
		double pw = (LOOKS_CELL_W - 18.0) * pt, ph = LOOKS_PIC_H * pt;
		double ax, ay;
		cairo_surface_t *img = drawer_item_image(NULL, &items[i], 30.0 * pt, 1.0, &ax, &ay, true);
		if (img) {
			double bx0, by0, bx1, by1;
			alpha_box(img, &bx0, &by0, &bx1, &by1);
			double k = fmin(1.0, fmin(pw / (bx1 - bx0), ph / (by1 - by0)));
			if (i == hover) {
				k *= 1.06;
			}
			double ox = x + LOOKS_CELL_W * pt * 0.5 - (bx0 + bx1) * 0.5 * k;
			double oy = y + 8.0 * pt + ph * 0.5 - (by0 + by1) * 0.5 * k - (i == hover ? 2.0 * pt : 0.0);
			int iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
			cairo_save(cr);
			cairo_translate(cr, ox, oy);
			cairo_scale(cr, k, k);
			double up = i == hover ? 0.5 : 0.2;
			drawer_shadow(cr, img, 0, 0, iw, ih, 6.0 * up / k * pt, 8.0 * up / k * pt, 0.32,
				fmax(1.5, 3.0 * pt / k), 1.0);
			cairo_set_source_surface(cr, img, 0, 0);
			cairo_paint(cr);
			cairo_restore(cr);
			cairo_surface_destroy(img);
		}
		double nx = x + LOOKS_CELL_W * pt * 0.5, ny = y + (LOOKS_PIC_H + 28.0) * pt;
		show_text_fit_as(cr, DESK_FONT_PRINT, names[i], nx, ny, 11.5 * pt, false,
			0.18, 0.15, 0.12, i == hover ? 0.98 : 0.80, 0, (LOOKS_CELL_W - 10.0) * pt);
		if (i == chosen) {
			/* round the thing, clear of its name */
			pencil_ring(cr, nx, y + (8.0 + LOOKS_PIC_H * 0.5) * pt, (LOOKS_CELL_W * 0.5 - 7.0) * pt,
				(LOOKS_PIC_H * 0.5 + 5.0) * pt, (unsigned)i * 7u + 3u);
		}
	}
	/* and where the person's own go */
	if (hint && *hint) {
		show_text_fit_as(cr, DESK_FONT_PRINT, hint, W * 0.5, H - 20.0 * pt, 10.5 * pt, false,
			0.30, 0.25, 0.20, 0.70, 0, W - LOOKS_MARGIN * 2.0 * pt);
	}
}

/* ---- Alt+Tab: what is on the windows spread out ----------------------
 *
 * Where the windows are spread out over the desk (wm.c): on each, a label
 * with its program's mark and name printed on it, with its shadow, in
 * points (pt pixels each), no wider than max_w. */

static double switcher_text_w(enum desk_font role, const char *s, double size)
{
	static cairo_surface_t *scratch;
	static cairo_t *mcr;
	if (!mcr) {
		scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4);
		mcr = cairo_create(scratch);
	}
	desk_font_select(mcr, role, s, false, false);
	cairo_set_font_size(mcr, size);
	cairo_text_extents_t ext;
	cairo_text_extents(mcr, s, &ext);
	return ext.x_advance;
}

static void switcher_tag_lay(const char *name, double pt, double max_w, double *fs,
	double *tw, double *th, double *pad)
{
	*fs = 13.0 * pt;
	/* the mark, then the name: 8 in, 22 the mark, 7 to the name, 11 out */
	*tw = fmax(fmin(48.0 * pt + switcher_text_w(DESK_FONT_PRINT, name, *fs), max_w),
		40.0 * pt);
	*th = 32.0 * pt;
	*pad = ceil(9.0 * pt);
}

void desk_switcher_tag_size(const char *name, double pt, double max_w, int *w, int *h,
	int *pad)
{
	double fs, tw, th, p;
	switcher_tag_lay(name, pt, max_w, &fs, &tw, &th, &p);
	*w = (int)ceil(tw + p * 2.0);
	*h = (int)ceil(th + p * 2.0);
	*pad = (int)p;
}

void desk_draw_switcher_tag(cairo_t *cr, cairo_surface_t *icon, const char *initial,
	const char *name, double pt, double max_w, unsigned seed)
{
	double fs, tw, th, pad;
	switcher_tag_lay(name, pt, max_w, &fs, &tw, &th, &pad);
	cairo_surface_t *tag = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		(int)ceil(tw), (int)ceil(th));
	cairo_t *tc = cairo_create(tag);
	double r = 3.0 * pt;
	fill_paper(tc, 0, 0, tw, th, r);
	cairo_save(tc);
	desk_rounded_path(tc, 0, 0, tw, th, r);
	cairo_clip(tc);
	paper_age(tc, tw, th, seed, 0.15);
	cairo_restore(tc);
	/* a label: a fine rule printed round inside its edge */
	desk_rounded_path(tc, 2.5 * pt, 2.5 * pt, tw - 5.0 * pt, th - 5.0 * pt, r * 0.6);
	cairo_set_source_rgba(tc, 0.30, 0.24, 0.18, 0.30);
	cairo_set_line_width(tc, fmax(0.6 * pt, 0.6));
	cairo_stroke(tc);
	double ms = 22.0 * pt;
	sheet_mark(tc, icon, initial, 8.0 * pt, (th - ms) * 0.5, ms, seed);
	double nx = 37.0 * pt;
	show_text_fit_as(tc, DESK_FONT_PRINT, name, nx, th * 0.5 + fs * 0.36, fs, false,
		0.17, 0.14, 0.11, 0.92, -1, tw - nx - 9.0 * pt);
	cairo_destroy(tc);
	cairo_surface_flush(tag);
	drawer_shadow(cr, tag, pad, pad, tw, th, 0.0, 2.0 * pt, 0.32, fmax(2.0, 2.5 * pt), 1.0);
	cairo_set_source_surface(cr, tag, pad, pad);
	cairo_paint(cr);
	cairo_surface_destroy(tag);
}

bool desk_drawer_handle_hit(int screen_w, int screen_h, double px, double py, double open)
{
	struct room_xf xf;
	if (!room_xform(screen_w, screen_h, open, &xf)) {
		return false;
	}
	struct room_render *rr = xf.rr;
	double u0, u1, v;
	if (!room_project(&rr->cam, v3(-RM_DR_HX, RM_DR_TOP, RM_DR_FACE + RM_DR_PULL), &u0, &v)
		|| !room_project(&rr->cam, v3(RM_DR_HX, RM_DR_TOP, RM_DR_FACE + RM_DR_PULL), &u1, &v)) {
		return false;
	}
	double x0 = xf.ox + fmin(u0, u1) * rr->w * xf.scale;
	double x1 = xf.ox + fmax(u0, u1) * rr->w * xf.scale;
	if (px < x0 - 12.0 || px > x1 + 12.0) {
		return false;
	}

	/* the brass pull, wherever the drawer has got to */
	double uu, vv;
	if (room_project(&rr->cam, v3(0, RM_DR_TOP - 1.10, RM_DR_FACE + RM_DR_PULL + 0.30),
		&uu, &vv)) {
		double cy = xf.ny + vv * rr->h * xf.scale;
		if (py >= xf.clip && py >= cy - 24.0 && py <= cy + 24.0) {
			return true;
		}
	}
	/* and the strip along the front of the desk, which is where a hand goes
	 * for a drawer that is shut and has no handle showing at all */
	return py >= screen_h - fmax(screen_h * 0.035, 16.0);
}
