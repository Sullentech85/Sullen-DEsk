/* Part of draw.c: painting the whole room, the things standing on the desk, and Print's camera.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- painting the whole room ----------------------------------- */

/* The bare boards, before there is any room: walnut and the felt along the
 * front, in gradients only. They lie under the dark of a lamp not yet lit,
 * and the grain of the wood is not worth what it cost here -- the wood and
 * felt tiles made from noise, pixel by pixel, the first time anything asks
 * for them, which at login was three seconds on a machine of 2010 before
 * the first frame. */
/* linear light to the screen's (sRGB) */
static double to_srgb(double c)
{
	c = clampf(c, 0.0, 1.0);
	return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

static void room_placeholder(cairo_t *cr, int w, int h)
{
	/* the boards, in the desk's own wood: walnut's, as they always were */
	double mid[3] = { 0.410, 0.255, 0.140 };
	if (desk_look() != 0) {
		const struct wood *wd0 = &DESK_WOODS[atomic_load(&g_desk_wood)];
		vec3 a = stained(vmix(wd0->light, wd0->dark, 0.35), atomic_load(&g_desk_stain));
		/* (walnut's middle comes out as the colour above: the same way, the rest) */
		mid[0] = to_srgb(a.x) * 0.79;
		mid[1] = to_srgb(a.y) * 0.79;
		mid[2] = to_srgb(a.z) * 0.79;
	}
	cairo_pattern_t *wd = cairo_pattern_create_linear(0, 0, w * 0.35, h);
	cairo_pattern_add_color_stop_rgb(wd, 0.0, mid[0] * 0.805, mid[1] * 0.784, mid[2] * 0.786);
	cairo_pattern_add_color_stop_rgb(wd, 0.5, mid[0], mid[1], mid[2]);
	cairo_pattern_add_color_stop_rgb(wd, 1.0, mid[0] * 0.732, mid[1] * 0.706, mid[2] * 0.679);
	cairo_set_source(cr, wd);
	cairo_paint(cr);
	cairo_pattern_destroy(wd);
	cairo_set_source_rgb(cr, 0.045, 0.105, 0.062);
	cairo_rectangle(cr, 0, h * 0.94, w, h * 0.06);
	cairo_fill(cr);
	cairo_pattern_t *sh = cairo_pattern_create_linear(0, h * 0.94, 0, h);
	cairo_pattern_add_color_stop_rgba(sh, 0.0, 0, 0, 0, 0.70);
	cairo_pattern_add_color_stop_rgba(sh, 1.0, 0, 0, 0, 0.20);
	cairo_set_source(cr, sh);
	cairo_rectangle(cr, 0, h * 0.94, w, h * 0.06);
	cairo_fill(cr);
	cairo_pattern_destroy(sh);
	/* How far along it is is the clock's to say, lit up in the dark where it
	 * will stand (clk_face_matrix, before there is a room): nothing is left
	 * lying on the bare boards in the meantime. */
	cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, h);
	cairo_pattern_add_color_stop_rgba(g, 0.00, 0.00, 0.00, 0.00, 0.55);
	cairo_pattern_add_color_stop_rgba(g, 0.45, 0.00, 0.00, 0.00, 0.10);
	cairo_pattern_add_color_stop_rgba(g, 1.00, 0.00, 0.00, 0.00, 0.45);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_pattern_t *r = cairo_pattern_create_radial(w * 0.5, h * 0.36, h * 0.04,
		w * 0.5, h * 0.42, h * 0.95);
	cairo_pattern_add_color_stop_rgba(r, 0.0, 1.0, 0.93, 0.74, 0.26);
	cairo_pattern_add_color_stop_rgba(r, 1.0, 0, 0, 0, 0);
	cairo_set_source(cr, r);
	cairo_paint(cr);
	cairo_pattern_destroy(r);
}

void desk_draw_room_placeholder(cairo_t *cr, int w, int h)
{
	room_placeholder(cr, w, h);
}

void desk_draw_room(cairo_t *cr, int w, int h, double drawer_open)
{
	struct room_xf xf;
	if (!room_xform(w, h, drawer_open, &xf)) {
		desk_scene_warm(w, h, true);
		room_placeholder(cr, w, h);
		return;
	}
	struct room_render *rr = xf.rr;

	cairo_save(cr);
	cairo_translate(cr, xf.ox, xf.oy);
	cairo_scale(cr, xf.scale, xf.scale);
	cairo_set_source_surface(cr, rr->background, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	cairo_save(cr);
	cairo_rectangle(cr, 0, xf.clip, w, h - xf.clip);
	cairo_clip(cr);
	cairo_translate(cr, xf.ox, xf.ny);
	cairo_scale(cr, xf.scale, xf.scale);
	cairo_set_source_surface(cr, rr->drawer, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	/* what a drawer nobody has filled comes with: the five things, in a
	 * row (the compositor lays in whatever has been put there) */
	if (drawer_open > 0.004) {
		struct desk_drawer_item items[DESK_DOCK_COUNT];
		double xs[DESK_DOCK_COUNT], fits[DESK_DOCK_COUNT];
		for (int i = 0; i < DESK_DOCK_COUNT; i++) {
			items[i] = (struct desk_drawer_item){ .look = i, .seed = (unsigned)i };
			xs[i] = desk_drawer_slot_x(i, DESK_DOCK_COUNT);
			fits[i] = desk_drawer_fit(DESK_DOCK_COUNT);
		}
		desk_drawer_paint_items(cr, w, h, items, DESK_DOCK_COUNT, xs, fits, -1);
	}
	cairo_restore(cr);

	overlay_grain(cr, 0, 0, w, h, 0.55);
}

/* ---- more desk, on a screen beside the desk's own ---------------------
 *
 * The same boards with nothing standing on them and no drawer in them: a
 * band of the desk's own top, from under what stands along its back to
 * above the drawer's pull, laid again and again over the other screen,
 * each copy the mirror of the one beside it -- so the boards run on from
 * where the desk's screen leaves off, with no seam, at the size they are
 * on the desk. Lit less the further it is from the desk the lamp stands
 * on. Before there is any room, the bare boards' brown. */
void desk_draw_side_desk(cairo_t *cr, int x, int y, int w, int h, int desk_w, int desk_h)
{
	struct room_xf xf;
	bool room = desk_w >= 64 && desk_h >= 64 && room_xform(desk_w, desk_h, 0.0, &xf);
	double y0 = 0.0, y1 = 0.0;
	if (room) {
		y0 = desk_tray_bottom(desk_w, desk_h) + desk_h * 0.03;
		y1 = desk_h - desk_drawer_pull_h(desk_w, desk_h) - desk_h * 0.02;
		if (y1 - y0 < 48.0) {
			y0 = desk_h * 0.25;
			y1 = desk_h * 0.80;
		}
	}
	cairo_surface_t *band = NULL;
	if (room) {
		int bw = desk_w, bh = (int)(y1 - y0);
		band = cairo_image_surface_create(CAIRO_FORMAT_RGB24, bw, bh);
		cairo_t *bc = cairo_create(band);
		cairo_translate(bc, xf.ox, xf.oy - y0);
		cairo_scale(bc, xf.scale, xf.scale);
		cairo_set_source_surface(bc, xf.rr->background, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(bc), CAIRO_FILTER_BILINEAR);
		cairo_paint(bc);
		cairo_destroy(bc);
		if (cairo_surface_status(band) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(band);
			band = NULL;
		}
	}
	cairo_save(cr);
	if (band) {
		/* the desk's own coordinates across: the band's left edge is the
		 * desk screen's, so a screen to its right takes the boards up from
		 * its right edge; and along them a band on, where the boards come
		 * the other way round -- so that what lies on them there, a ring a
		 * cup left, is not just across from where it is on the desk */
		cairo_pattern_t *pat = cairo_pattern_create_for_surface(band);
		cairo_pattern_set_extend(pat, CAIRO_EXTEND_REFLECT);
		cairo_pattern_set_filter(pat, CAIRO_FILTER_GOOD);
		cairo_matrix_t m;
		cairo_matrix_init_translate(&m, x, y - y0 + (y1 - y0));
		cairo_pattern_set_matrix(pat, &m);
		cairo_set_source(cr, pat);
		cairo_paint(cr);
		cairo_pattern_destroy(pat);
		cairo_surface_destroy(band);
	} else {
		cairo_pattern_t *wd = cairo_pattern_create_linear(0, 0, w * 0.35, h);
		cairo_pattern_add_color_stop_rgb(wd, 0.0, 0.330, 0.200, 0.110);
		cairo_pattern_add_color_stop_rgb(wd, 0.5, 0.410, 0.255, 0.140);
		cairo_pattern_add_color_stop_rgb(wd, 1.0, 0.300, 0.180, 0.095);
		cairo_set_source(cr, wd);
		cairo_paint(cr);
		cairo_pattern_destroy(wd);
	}
	/* further from the lamp: from the edge nearest the desk's screen, where
	 * it is nearly as light, to the far edge, a good deal darker */
	double cx = desk_w * 0.5, cy = desk_h * 0.5;
	/* the point of it nearest the middle of the desk's screen, and the one
	 * furthest from that, across it */
	double nx = clampf(cx, x, x + w) - x, ny = clampf(cy, y, y + h) - y;
	double fx = x <= cx && cx <= x + w ? nx : fabs(x - cx) > fabs(x + w - cx) ? 0.0 : w;
	double fy = y <= cy && cy <= y + h ? ny : fabs(y - cy) > fabs(y + h - cy) ? 0.0 : h;
	if (hypot(fx - nx, fy - ny) > 1.0) {
		cairo_pattern_t *dim = cairo_pattern_create_linear(nx, ny, fx, fy);
		cairo_pattern_add_color_stop_rgba(dim, 0.0, 0.0, 0.0, 0.0, 0.10);
		cairo_pattern_add_color_stop_rgba(dim, 1.0, 0.0, 0.0, 0.0, 0.42);
		cairo_set_source(cr, dim);
		cairo_paint(cr);
		cairo_pattern_destroy(dim);
	} else {
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.20);
		cairo_paint(cr);
	}
	cairo_restore(cr);
}

unsigned desk_room_shown(int screen_w, int screen_h)
{
	struct room_render *rr = room_for(screen_w, screen_h);
	return rr ? rr->serial : 0;
}

unsigned desk_console_room(int w, int h)
{
	struct room_render *rr = room_for_view(w, h, DESK_VIEW_CONSOLE);
	return rr && rr->console ? rr->serial : 0;
}

unsigned desk_view_room(int w, int h, int view)
{
	struct room_render *rr = room_for_view(w, h, view);
	return rr ? rr->serial : 0;
}

void desk_scene_warm(int screen_w, int screen_h, bool async)
{
	desk_light_desk(screen_w, screen_h);
	room_warm(screen_w, screen_h, async);
}

/* The room's lamp over the desk seen from straight above with the drawer
 * shut: where on the screen the point under it is, and its height, from
 * the camera the room was traced through -- or, before there is a room,
 * from the desk filling the screen, which is what that camera does. */
void desk_light_desk(int screen_w, int screen_h)
{
	struct room_xf xf;
	struct lamp_px l = g_desk_lamp;
	bool got = false;
	if (screen_w >= 8 && screen_h >= 8 && room_xform(screen_w, screen_h, 0.0, &xf)) {
		struct room_render *rr = xf.rr;
		vec3 lp = ROOM_LAMP_POS;
		double u0, v0, u1, v1;
		if (room_project(&rr->cam, v3(lp.x, 0.0, lp.z), &u0, &v0)
			&& room_project(&rr->cam, v3(lp.x + 1.0, 0.0, lp.z), &u1, &v1)) {
			double sw = rr->w * xf.scale, sh = rr->h * xf.scale;
			l.x = xf.ox + u0 * sw;
			l.y = xf.oy + v0 * sh;
			l.h = lp.y * (u1 - u0) * sw;
			got = l.h > 1.0;
		}
	}
	if (!got && screen_w >= 8 && screen_h >= 8) {
		vec3 lp = ROOM_LAMP_POS;
		double ppu = screen_w / (2.0 * RM_HX);
		l.x = screen_w * 0.5 + lp.x * ppu;
		l.y = screen_h * (lp.z - RM_ZF) / (RM_ZN - RM_ZF);
		l.h = lp.y * ppu;
	}
	/* a little shorter than the lamp's own would be: a sheet held an inch
	 * up should not throw a shadow a hand long */
	l.h /= 0.85;
	l.kmin = 0.10;
	l.kmax = 1.40;
	g_desk_lamp = l;
	g_light = l;
}

/* ---- the lamp coming up at login ----------------------------------
 *
 * An incandescent lamp on a dimmer: black, then the pool under the lamp
 * opening first and warm while the filament is, the light reaching the
 * corners last. All of it is one radial gradient about the point under
 * the lamp, from how dark its middle is (a0) to how dark the corners are
 * (a1) at radius r, over a warm wash. */
struct night {
	double lx, ly, r, a0, a1, warm;
};

static void night_of(int w, int h, double light, struct night *n)
{
	double L = clampf(light, 0.0, 1.0);
	n->lx = clampf(g_desk_lamp.x, 0.0, w);
	n->ly = clampf(g_desk_lamp.y, 0.0, h);
	n->r = hypot(w, h) * (0.30 + 0.80 * L);
	n->a0 = 0.985 * pow(1.0 - L, 1.7);
	n->a1 = 0.985 * pow(1.0 - L, 0.75);
	n->warm = 0.09 * sin(M_PI * L) * (1.0 - 0.4 * L);
}

void desk_draw_night(cairo_t *cr, int w, int h, double light)
{
	struct night n;
	night_of(w, h, light, &n);
	cairo_save(cr);
	if (n.warm > 0.002) {
		cairo_set_source_rgba(cr, 1.0, 0.56, 0.22, n.warm);
		cairo_paint(cr);
	}
	cairo_pattern_t *g = cairo_pattern_create_radial(n.lx, n.ly, 0.0, n.lx, n.ly, n.r);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0.030, 0.018, 0.010, n.a0);
	cairo_pattern_add_color_stop_rgba(g, 0.5, 0.030, 0.018, 0.010, n.a0 * 0.35 + n.a1 * 0.65);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.030, 0.018, 0.010, n.a1);
	cairo_pattern_set_extend(g, CAIRO_EXTEND_PAD);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
}

double desk_boot_light(double progress)
{
	double t = clampf((progress - 0.02) / 0.98, 0.0, 1.0);
	return pow(t * t * (3.0 - 2.0 * t), 1.2);
}

double desk_night_alpha_at(int w, int h, double light, double x, double y)
{
	struct night n;
	night_of(w, h, light, &n);
	double t = clampf(hypot(x - n.lx, y - n.ly) / fmax(n.r, 1.0), 0.0, 1.0);
	double mid = n.a0 * 0.35 + n.a1 * 0.65;
	return t < 0.5 ? n.a0 + (mid - n.a0) * t * 2.0 : mid + (n.a1 - mid) * (t - 0.5) * 2.0;
}

void desk_draw_lids(cairo_t *cr, int w, int h, double open)
{
	if (w < 8 || h < 8 || open >= 1.0) {
		return;
	}
	open = clampf(open, 0.0, 1.0);
	/* made a quarter the size, stretched: it is all soft */
	int sw = w / 4 > 8 ? w / 4 : 8, sh = h / 4 > 8 ? h / 4 : 8;
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(img);
		return;
	}
	cairo_surface_flush(img);
	unsigned char *px = cairo_image_surface_get_data(img);
	int stride = cairo_image_surface_get_stride(img);
	double cx = w * 0.5, cy = h * 0.5;
	/* how far each lid's edge is drawn back from the middle line: slowly
	 * at first (half open is a slit), over the corners of the screen wide
	 * open, and shut with all of its soft edge over the line */
	double soft = h * (0.03 + 0.10 * open);
	double up = open * open * h * 1.35 - soft, down = open * open * h * 1.5 - soft;
	for (int j = 0; j < sh; j++) {
		uint32_t *row = (uint32_t *)(px + (size_t)j * (size_t)stride);
		double y = (j + 0.5) * h / sh;
		for (int i = 0; i < sw; i++) {
			double x = (i + 0.5) * w / sw;
			double u = (x - cx) / (w * 0.75), arch = 1.0 - u * u;
			/* past the edge of the lid it is in (above the middle the upper
			 * one, below it the lower), in pixels: less than 0 is open */
			double d = y < cy ? (cy - y) - up * arch : (y - cy) - down * arch * (0.85 + 0.15 * arch);
			double a = clampf((d + soft) / (2.0 * soft), 0.0, 1.0);
			a = a * a * (3.0 - 2.0 * a);
			/* the light coming through near the edge: deep red, then dark */
			double glow = exp(-fmax(d, 0.0) / (soft * 2.5 + 1.0)) * (0.35 + 0.65 * open);
			double r = 0.012 + 0.10 * glow, g = 0.006 + 0.025 * glow, b = 0.004 + 0.012 * glow;
			uint32_t A = (uint32_t)lround(a * 255.0);
			uint32_t R = (uint32_t)lround(r * a * 255.0), G = (uint32_t)lround(g * a * 255.0),
				B = (uint32_t)lround(b * a * 255.0);
			row[i] = A << 24 | R << 16 | G << 8 | B;
		}
	}
	cairo_surface_mark_dirty(img);
	cairo_save(cr);
	cairo_scale(cr, (double)w / sw, (double)h / sh);
	cairo_set_source_surface(cr, img, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_surface_destroy(img);
}

/* Whether the room for THIS size is traced. Not the same question as
 * whether there is anything to draw -- room_for() will hand back the last
 * room rather than nothing -- so this stays exact: it is what tells the
 * compositor to keep asking for the trace it actually wants. */
bool desk_scene_ready(int screen_w, int screen_h)
{
	int cw, ch, hq;
	room_canon(screen_w, screen_h, &cw, &ch, &hq);
	return room_peek(cw, ch, hq) != NULL;
}

/* The desk goes up as soon as its own two sheets are traced. The bookcase,
 * the console and the post box come after it, so for a minute or so on the
 * first run at a size there is a desk to work at but nothing to turn to:
 * all three (desk_scene_views_ready), or one of them, which is there the
 * moment its own sheet is (desk_scene_view_ready). */
bool desk_scene_views_ready(int screen_w, int screen_h)
{
	return room_for_whole(screen_w, screen_h) != NULL;
}

bool desk_scene_view_ready(int screen_w, int screen_h, int which)
{
	return room_for_view(screen_w, screen_h, which) != NULL;
}

/* What resolution to draw the things that are laid over the room in screen
 * coordinates -- the pool of light under the pointer, the lamps and needles
 * on the console, the letters in the post box.
 *
 * Not the screen's, above a point. The room behind them is traced at 1440
 * and magnified, so past there a finer overlay buys a sharp lamp on a soft
 * desk; what it costs is real. On a 3600 wide screen the console's live
 * half was seventeen milliseconds and it is repainted twelve times a
 * second, which is a fifth of a core to watch a needle move. Drawn at 2048
 * it is six -- still finer than the desk it sits on.
 *
 * Except at Ultra, which is drawn at its full size, moving or not: there
 * the screen's own pixels, up to a 4K screen's worth. Magnified to a 4K
 * screen from 2048, the book's letters, the calendar's figures and the
 * clock's were soft enough to be reported, over a room a GPU traces 2560
 * wide; at the screen's own width a 4K screen takes up to some 500 MB
 * more (the pictures of the book, the binders, the console...) and a page
 * of the book turns at two and a half times the cost. Past a 4K screen's
 * worth of pixels (5K, 8K) they are drawn at that many and magnified the
 * rest of the way; an ultrawide, wider but of fewer pixels, at its own. */
#define DESK_OVERLAY_W 2048
#define DESK_OVERLAY_ULTRA_PX (3840.0 * 2160.0)

/* the screen's size, or no wider than cap */
static void overlay_capped(int screen_w, int screen_h, int cap, int *ow, int *oh)
{
	if (screen_w <= cap) {
		*ow = screen_w;
		*oh = screen_h;
		return;
	}
	*ow = cap;
	*oh = (int)((double)screen_h * cap / screen_w + 0.5);
	if (*oh < 8) {
		*oh = 8;
	}
}

void desk_overlay_size(int screen_w, int screen_h, int *ow, int *oh)
{
	if (screen_w < 8 || screen_h < 8) {
		*ow = screen_w;
		*oh = screen_h;
		return;
	}
	if (desk_gfx_level() == DESK_GFX_ULTRA) {
		double px = (double)screen_w * screen_h;
		if (px <= DESK_OVERLAY_ULTRA_PX) {
			*ow = screen_w;
			*oh = screen_h;
			return;
		}
		double f = sqrt(DESK_OVERLAY_ULTRA_PX / px);
		*ow = (int)(screen_w * f + 0.5);
		*oh = (int)(screen_h * f + 0.5);
		return;
	}
	/* Not tied to the room at the levels that trace it smaller (Low 960,
	 * Medium 1280, 2D 640), as it was, to spare the console's tube on a
	 * machine of 2010: on a 1920 screen at 2D the clock's buttons and the
	 * calendar's figures came out three times magnified and could not be
	 * read. What is read is drawn at the screen's pixels at every level;
	 * what may be soft -- the room, and what only sets the scene over it
	 * (desk_ambient_size) -- is as small as the level says. On a machine of
	 * 2010 at 2D on a 1920 screen it costs the book most (a letter written
	 * in it waited up to 0.36 s rather than 0.03, a page turned in 0.14
	 * rather than 0.04) and 130 MB; the clock, the calendar, the memo pad,
	 * the console and the post box cost about what they did. */
	overlay_capped(screen_w, screen_h, DESK_OVERLAY_W, ow, oh);
}

/* What is a scene rather than something read -- the door, traced as the
 * room is, and the pictures of the room it opens on to; the rain, the snow
 * and the lights at it; the confetti at the New Year; the desk going on
 * across another screen -- at the size the overlay had before: no finer
 * than the room is traced at the levels that trace it smaller. At the
 * screen's width on a 1920 screen at Low, a machine of 2010 was still
 * tracing the door finely ten seconds after it came down, drawing it
 * again every frame meanwhile, and showed the rain at 19 frames a second
 * rather than 46. */
void desk_ambient_size(int screen_w, int screen_h, int *ow, int *oh)
{
	int rq = ROOM_Q[desk_gfx_level()].cap;
	if (screen_w < 8 || screen_h < 8 || rq <= 0) {
		desk_overlay_size(screen_w, screen_h, ow, oh);
		return;
	}
	overlay_capped(screen_w, screen_h, rq < DESK_OVERLAY_W ? rq : DESK_OVERLAY_W, ow, oh);
}

double desk_scene_progress(void)
{
	if (atomic_load_explicit(&g_previewing, memory_order_relaxed)) {
		return 0.0;
	}
	if (atomic_load_explicit(&g_rest_view, memory_order_relaxed) >= 0) {
		return 1.0;   /* past the desk: the counters are the views' now */
	}
	long goal = atomic_load_explicit(&g_bake_goal, memory_order_relaxed);
	if (goal <= 0) {
		return 0.0;
	}
	long done = atomic_load_explicit(&g_bake_rows, memory_order_relaxed);
	return clampf((double)done / (double)goal, 0.0, 1.0);
}

double desk_scene_views_progress(int *view)
{
	int v = atomic_load_explicit(&g_rest_view, memory_order_relaxed);
	long total = atomic_load_explicit(&g_rest_total_w, memory_order_relaxed);
	if (view) {
		*view = v;
	}
	if (v < 0 || total <= 0) {
		return 0.0;
	}
	long goal = atomic_load_explicit(&g_bake_goal, memory_order_relaxed);
	long rows = atomic_load_explicit(&g_bake_rows, memory_order_relaxed);
	double part = goal > 0 ? clampf((double)rows / (double)goal, 0.0, 1.0) : 0.0;
	double done = (double)atomic_load_explicit(&g_rest_done_w, memory_order_relaxed)
		+ part * (double)atomic_load_explicit(&g_rest_part_w, memory_order_relaxed);
	return clampf(done / (double)total, 0.0, 1.0);
}

/* ---- the things standing on the desk --------------------------- */

/* ---- the clock on the back of the desk --------------------------
 *
 * A red LED display in a dark case, and under it four keys with what they
 * do printed on them. The display is drawn as the hardware would be: every
 * segment of every digit is there, unlit, behind the glass, and the lit
 * ones glow; letters are fourteen segment characters, small print is a
 * five by seven dot matrix, and a tray item's icon is lit through its own
 * shape. Everything here is in the face's own pixels, CLK_W by CLK_H,
 * which ROOM_GADGETS[DESK_GADGET_CLOCK] lays over the case. */

#define CLK_W 492.0
#define CLK_H 72.0
/* the display window */
#define CLK_WX 8.0
#define CLK_WY 6.0
#define CLK_WW 356.0
#define CLK_WH 64.0
/* the keys: two columns of two, to the right of the display */
#define CLK_KX 376.0
#define CLK_KY CLK_WY
#define CLK_KW 54.0
#define CLK_KH 29.0
#define CLK_KGX 4.0
#define CLK_KGY 6.0

static struct desk_clock_state g_clk;
static const char *const CLK_KEY_NAME[DESK_CLOCK_KEYS] = { "clock|CLOCK", "clock|DAY",
	"clock|MEDIA", "clock|PC" };

static double clk_key_x(int i)
{
	return CLK_KX + (i & 1) * (CLK_KW + CLK_KGX);
}

static double clk_key_y(int i)
{
	return CLK_KY + (i >> 1) * (CLK_KH + CLK_KGY);
}

void desk_clock_set(const struct desk_clock_state *st)
{
	g_clk = *st;
}

/* ---- the light ---- */

/* the colour they light: the clock's red, unless what is being drawn with
 * them is something else's (the viewfinder's green) */
static double g_led_rgb[3] = { 1.00, 0.22, 0.11 };

static void led_on(cairo_t *cr, double a)
{
	cairo_set_source_rgba(cr, g_led_rgb[0], g_led_rgb[1], g_led_rgb[2], a);
}

/* drawing only what shines -- the clock in the dark: the unlit segments
 * are not seen at all */
static bool g_led_lit_only;

static void led_ghost(cairo_t *cr)
{
	cairo_set_source_rgba(cr, 0.60, 0.10, 0.06, g_led_lit_only ? 0.0 : 0.13);
}

/* A lit shape, filled from the current path: a soft halo round it first,
 * because an LED behind smoked glass is brighter than its edges. */
static void led_fill_path(cairo_t *cr, double a)
{
	cairo_path_t *path = cairo_copy_path(cr);
	cairo_save(cr);
	led_on(cr, 0.16 * a);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_width(cr, 3.2);
	cairo_stroke_preserve(cr);
	cairo_set_line_width(cr, 1.6);
	led_on(cr, 0.22 * a);
	cairo_stroke(cr);
	cairo_restore(cr);
	cairo_append_path(cr, path);
	cairo_path_destroy(path);
	led_on(cr, a);
	cairo_fill(cr);
}

/* ---- segments ---- */

struct seg_box {
	double x, y, w, h;
	double slant;     /* how far the top leans right of the bottom */
};

static void seg_pt(const struct seg_box *b, double u, double v, double *x, double *y)
{
	*x = b->x + u + b->slant * (1.0 - v / b->h);
	*y = b->y + v;
}

/* One segment from (u1,v1) to (u2,v2), pointed at both ends, t thick. */
static void seg_path(cairo_t *cr, const struct seg_box *b, double u1, double v1, double u2,
	double v2, double t, double inset)
{
	double dx = u2 - u1, dy = v2 - v1;
	double len = hypot(dx, dy);
	if (len < 1e-6) {
		return;
	}
	dx /= len;
	dy /= len;
	u1 += dx * inset;
	v1 += dy * inset;
	u2 -= dx * inset;
	v2 -= dy * inset;
	double nx = -dy * t * 0.5, ny = dx * t * 0.5;
	double hx = dx * t * 0.5, hy = dy * t * 0.5;
	double pu[6] = { u1, u1 + hx + nx, u2 - hx + nx, u2, u2 - hx - nx, u1 + hx - nx };
	double pv[6] = { v1, v1 + hy + ny, v2 - hy + ny, v2, v2 - hy - ny, v1 + hy - ny };
	for (int k = 0; k < 6; k++) {
		double x, y;
		seg_pt(b, pu[k], pv[k], &x, &y);
		if (k == 0) {
			cairo_move_to(cr, x, y);
		} else {
			cairo_line_to(cr, x, y);
		}
	}
	cairo_close_path(cr);
}

/* The fourteen segments, in the order hardware fonts number them:
 * A B C D E F G1 G2, then the diagonals and the middle uprights
 * H (top left) J (top) K (top right) L (bottom right) M (bottom) N
 * (bottom left). Seven segment digits are the first seven with G whole. */
static void seg14_one(cairo_t *cr, const struct seg_box *b, int s, double t)
{
	double w = b->w, h = b->h, m = h * 0.5, c = w * 0.5;
	/* The outline runs through the middle of the strokes, so neighbours
	 * meet point to point with a hair between; the middle bars and the
	 * spokes meet at the centre the same way. */
	double e = t * 0.5, gap = t * 0.18;
	double dt = t * 0.80;
	switch (s) {
	case 0: seg_path(cr, b, e, e, w - e, e, t, gap); break;
	case 1: seg_path(cr, b, w - e, e, w - e, m, t, gap); break;
	case 2: seg_path(cr, b, w - e, m, w - e, h - e, t, gap); break;
	case 3: seg_path(cr, b, e, h - e, w - e, h - e, t, gap); break;
	case 4: seg_path(cr, b, e, m, e, h - e, t, gap); break;
	case 5: seg_path(cr, b, e, e, e, m, t, gap); break;
	case 6: seg_path(cr, b, e, m, c, m, t, gap); break;
	case 7: seg_path(cr, b, c, m, w - e, m, t, gap); break;
	case 8: seg_path(cr, b, e + t * 0.55, e + t * 0.55, c, m, dt, t * 0.55); break;
	case 9: seg_path(cr, b, c, e, c, m, dt, gap + t * 0.35); break;
	case 10: seg_path(cr, b, w - e - t * 0.55, e + t * 0.55, c, m, dt, t * 0.55); break;
	case 11: seg_path(cr, b, c, m, w - e - t * 0.55, h - e - t * 0.55, dt, t * 0.55); break;
	case 12: seg_path(cr, b, c, m, c, h - e, dt, gap + t * 0.35); break;
	case 13: seg_path(cr, b, c, m, e + t * 0.55, h - e - t * 0.55, dt, t * 0.55); break;
	default: break;
	}
}

static unsigned seg14_code(int ch)
{
	if (ch >= 'a' && ch <= 'z') {
		ch -= 32;
	}
	switch (ch) {
	case '0': return 0x243F;
	case '1': return 0x0006;
	case '2': return 0x00DB;
	case '3': return 0x008F;
	case '4': return 0x00E6;
	case '5': return 0x00ED;
	case '6': return 0x00FD;
	case '7': return 0x0007;
	case '8': return 0x00FF;
	case '9': return 0x00EF;
	case 'A': return 0x00F7;
	case 'B': return 0x128F;
	case 'C': return 0x0039;
	case 'D': return 0x120F;
	case 'E': return 0x0079;
	case 'F': return 0x0071;
	case 'G': return 0x00BD;
	case 'H': return 0x00F6;
	case 'I': return 0x1209;
	case 'J': return 0x001E;
	case 'K': return 0x0C70;
	case 'L': return 0x0038;
	case 'M': return 0x0536;
	case 'N': return 0x0936;
	case 'O': return 0x003F;
	case 'P': return 0x00F3;
	case 'Q': return 0x083F;
	case 'R': return 0x08F3;
	case 'S': return 0x00ED;
	case 'T': return 0x1201;
	case 'U': return 0x003E;
	case 'V': return 0x2430;
	case 'W': return 0x2836;
	case 'X': return 0x2D00;
	case 'Y': return 0x1500;
	case 'Z': return 0x2409;
	case '/': return 0x2400;
	case '-': return 0x00C0;
	default: return 0;
	}
}

/* A character on a fourteen segment position, lit segments over dim ones. */
static void seg14_char(cairo_t *cr, const struct seg_box *b, int ch, double lit)
{
	double t = b->w * 0.17;
	unsigned code = seg14_code(ch);
	led_ghost(cr);
	for (int s = 0; s < 14; s++) {
		if (!(code & (1u << s))) {
			seg14_one(cr, b, s, t);
		}
	}
	cairo_fill(cr);
	if (!code) {
		return;
	}
	for (int s = 0; s < 14; s++) {
		if (code & (1u << s)) {
			seg14_one(cr, b, s, t);
		}
	}
	led_fill_path(cr, lit);
}

/* A seven segment digit (or -1 for a blank, -2 for a dash). */
static void seg7_digit(cairo_t *cr, const struct seg_box *b, int d, double lit)
{
	static const unsigned char SEG7[10] = {
		0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
	};
	unsigned code = d >= 0 && d <= 9 ? SEG7[d] : d == -2 ? 0x40 : 0;
	double t = b->w * 0.19;
	double w = b->w, h = b->h, m = h * 0.5;
	for (int pass = 0; pass < 2; pass++) {
		bool any = false;
		for (int s = 0; s < 7; s++) {
			bool on = (code >> s) & 1;
			if (on != (pass == 1)) {
				continue;
			}
			any = true;
			if (s == 6) {
				seg_path(cr, b, t * 0.5, m, w - t * 0.5, m, t, t * 0.18);
			} else {
				seg14_one(cr, b, s, t);
			}
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

/* A string of seven segment digits: '0'-'9', ' ', '-', and '.' as the
 * decimal point on the digit before it. Returns the width it took. */
static double seg7_text(cairo_t *cr, const char *s, double x, double y, double w, double h,
	double gap, double lit)
{
	double x0 = x;
	for (const char *p = s; *p; p++) {
		if (*p == '.') {
			continue;
		}
		struct seg_box b = { x, y, w, h, h * 0.10 };
		int d = *p >= '0' && *p <= '9' ? *p - '0' : *p == '-' ? -2 : -1;
		seg7_digit(cr, &b, d, lit);
		/* the point after it */
		cairo_arc(cr, x + w + gap * 0.5 - h * 0.02, y + h - w * 0.10, w * 0.10, 0, 2 * M_PI);
		if (p[1] == '.') {
			led_fill_path(cr, lit);
		} else {
			led_ghost(cr);
			cairo_fill(cr);
		}
		x += w + gap;
	}
	return x - x0 - gap;
}

static double seg14_text(cairo_t *cr, const char *s, double x, double y, double w, double h,
	double gap, double lit)
{
	double x0 = x;
	for (const char *p = s; *p; p++) {
		struct seg_box b = { x, y, w, h, h * 0.10 };
		seg14_char(cr, &b, (unsigned char)*p, lit);
		x += w + gap;
	}
	return x - x0 - gap;
}

/* ---- Print: the viewfinder ------------------------------------------
 *
 * Print is a look through a camera at the screen (wm.c, snip_*). In the
 * dark under the picture, the viewfinder's display, where a reflex camera
 * has it: green figures and marks that show only where they are lit. At
 * its left, the picture of what the dial is set to, the focus light --
 * lit when the shutter would take something -- and how big that is, w by
 * h pixels; at its right, the self-timer's mark and seconds when it is
 * set, and the frame the film is at. In the middle of what is lit, the
 * focusing screen's circle. In points (pt pixels each); the display is
 * laid out in the clock's own sizes, a digit FD_H high, and scaled. */

#define FD_H 36.0       /* a digit: high */
#define FD_W 20.0       /* wide */
#define FD_GAP 6.0      /* between two */
#define FD_PAD 24.0     /* in from the ends of the self-timer's count */
#define FD_LIGHT 13.0   /* the focus light, across */
#define FD_BY 14.0      /* the x between the two, across */
#define FL_H (FD_H + 20.0)   /* the display, high */
#define FL_END 12.0     /* in from the picture's edges */
#define FL_MARK 34.0    /* a mark in it, across */

static double finder_scale(double pt)
{
	return 12.0 * pt / FD_H;   /* a digit 12 points high */
}

static double finder_line_scale(double pt)
{
	return 14.0 * pt / FD_H;   /* and in the display, 14 */
}

static int finder_digits(int v)
{
	int n = 1;
	while (v >= 10) {
		v /= 10;
		n++;
	}
	return n;
}

/* A lit line along the current path, w across in the units it is drawn
 * in: its halo first, as for a lit shape. */
static void led_stroke_path(cairo_t *cr, double w, double a)
{
	cairo_save(cr);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	led_on(cr, 0.16 * a);
	cairo_set_line_width(cr, w + 3.2);
	cairo_stroke_preserve(cr);
	led_on(cr, 0.22 * a);
	cairo_set_line_width(cr, w + 1.6);
	cairo_stroke_preserve(cr);
	led_on(cr, a);
	cairo_set_line_width(cr, w);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* The pictures on the camera's dial, and in the display: about (0, 0), a
 * unit across -- the corners of an area drawn out (0), a sheet of paper,
 * its corner turned down (1), a screen on its foot (2). A path only. */
static void finder_mark_path(cairo_t *cr, int i)
{
	cairo_new_path(cr);
	if (i == 0) {
		const double c[4][2] = { { -0.5, -0.36 }, { 0.5, -0.36 }, { 0.5, 0.36 }, { -0.5, 0.36 } };
		for (int k = 0; k < 4; k++) {
			double sx = k == 0 || k == 3 ? 1.0 : -1.0, sy = k < 2 ? 1.0 : -1.0;
			cairo_move_to(cr, c[k][0] + sx * 0.30, c[k][1]);
			cairo_line_to(cr, c[k][0], c[k][1]);
			cairo_line_to(cr, c[k][0], c[k][1] + sy * 0.24);
		}
	} else if (i == 1) {
		cairo_move_to(cr, -0.38, -0.44);
		cairo_line_to(cr, 0.20, -0.44);
		cairo_line_to(cr, 0.38, -0.26);
		cairo_line_to(cr, 0.38, 0.44);
		cairo_line_to(cr, -0.38, 0.44);
		cairo_close_path(cr);
		cairo_move_to(cr, 0.20, -0.44);
		cairo_line_to(cr, 0.20, -0.26);
		cairo_line_to(cr, 0.38, -0.26);
		cairo_move_to(cr, -0.22, -0.10);
		cairo_line_to(cr, 0.22, -0.10);
		cairo_move_to(cr, -0.22, 0.12);
		cairo_line_to(cr, 0.22, 0.12);
	} else {
		cairo_rectangle(cr, -0.50, -0.42, 1.0, 0.64);
		cairo_move_to(cr, 0.0, 0.22);
		cairo_line_to(cr, 0.0, 0.40);
		cairo_move_to(cr, -0.22, 0.42);
		cairo_line_to(cr, 0.22, 0.42);
	}
}

/* the self-timer's mark, likewise: a stopwatch */
static void finder_timer_path(cairo_t *cr)
{
	cairo_new_path(cr);
	cairo_new_sub_path(cr);
	cairo_arc(cr, 0.0, 0.08, 0.38, 0.0, 2.0 * M_PI);
	cairo_move_to(cr, 0.0, -0.30);
	cairo_line_to(cr, 0.0, -0.46);
	cairo_move_to(cr, -0.13, -0.46);
	cairo_line_to(cr, 0.13, -0.46);
	cairo_move_to(cr, 0.0, 0.08);
	cairo_line_to(cr, 0.17, -0.10);
}

/* and the film's: a frame of it, the sprocket holes over and under it */
static void finder_film_path(cairo_t *cr)
{
	cairo_new_path(cr);
	cairo_rectangle(cr, -0.50, -0.38, 1.0, 0.76);
	cairo_rectangle(cr, -0.28, -0.17, 0.56, 0.34);
	for (int k = 0; k < 2; k++) {
		double x = k ? 0.16 : -0.16;
		cairo_move_to(cr, x - 0.05, -0.28);
		cairo_line_to(cr, x + 0.05, -0.28);
		cairo_move_to(cr, x - 0.05, 0.28);
		cairo_line_to(cr, x + 0.05, 0.28);
	}
}

/* and a tape's: a cassette, its two reels and, along its foot, the tape */
static void finder_tape_path(cairo_t *cr)
{
	cairo_new_path(cr);
	cairo_rectangle(cr, -0.50, -0.36, 1.0, 0.72);
	for (int k = 0; k < 2; k++) {
		cairo_new_sub_path(cr);
		cairo_arc(cr, k ? 0.22 : -0.22, -0.06, 0.12, 0.0, 2.0 * M_PI);
	}
	cairo_move_to(cr, -0.32, 0.36);
	cairo_line_to(cr, -0.24, 0.20);
	cairo_line_to(cr, 0.24, 0.20);
	cairo_line_to(cr, 0.32, 0.36);
}

/* How long a tape has run, as a camcorder shows it: minutes and seconds,
 * hours before them past the hour, a colon between -- two lit dots. From
 * x (or, measure, just how wide). */
#define FD_COLON 12.0
static double finder_time(cairo_t *cr, int secs, double x, double y, bool measure)
{
	int v = secs < 0 ? 0 : secs > 359999 ? 359999 : secs;
	char g[3][4];
	int n = 0;
	if (v >= 3600) {
		snprintf(g[n++], sizeof g[0], "%d", v / 3600);
		snprintf(g[n++], sizeof g[0], "%02d", v / 60 % 60);
	} else {
		snprintf(g[n++], sizeof g[0], "%d", v / 60);
	}
	snprintf(g[n++], sizeof g[0], "%02d", v % 60);
	double x0 = x;
	for (int i = 0; i < n; i++) {
		if (i > 0) {
			if (!measure) {
				for (int k = 0; k < 2; k++) {
					/* (leaning with the digits) */
					double dy = FD_H * (k ? 0.70 : 0.32);
					cairo_arc(cr, x + FD_COLON * 0.5 + (FD_H - dy) * 0.10 - 1.0, y + dy,
						FD_W * 0.12, 0.0, 2.0 * M_PI);
					led_fill_path(cr, 1.0);
				}
			}
			x += FD_COLON;
		}
		int digits = (int)strlen(g[i]);
		if (!measure) {
			seg7_text(cr, g[i], x, y, FD_W, FD_H, FD_GAP, 1.0);
		}
		x += digits * (FD_W + FD_GAP) - FD_GAP;
		if (i + 1 < n) {
			x += FD_GAP;
		}
	}
	return x - x0;
}

/* a red light, lit: the dot a camera running shows */
static void finder_rec_dot(cairo_t *cr, double cx, double cy, double r)
{
	double was[3] = { g_led_rgb[0], g_led_rgb[1], g_led_rgb[2] };
	g_led_rgb[0] = 1.00;
	g_led_rgb[1] = 0.16;
	g_led_rgb[2] = 0.10;
	cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
	led_fill_path(cr, 1.0);
	g_led_rgb[0] = was[0];
	g_led_rgb[1] = was[1];
	g_led_rgb[2] = was[2];
}

/* one of the marks at (x, y) in the display, lit */
static void finder_line_mark(cairo_t *cr, void (*path)(cairo_t *), int i, double x, double y)
{
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_scale(cr, FL_MARK, FL_MARK);
	if (path) {
		path(cr);
	} else {
		finder_mark_path(cr, i);
	}
	cairo_restore(cr);
	led_stroke_path(cr, 3.4, 1.0);
}

int desk_finder_line_height(double pt)
{
	return (int)ceil(FL_H * finder_line_scale(pt));
}

void desk_draw_finder_line(cairo_t *cr, int width, const struct desk_finder_line *l, double pt)
{
	double s = finder_line_scale(pt);
	double uw = width / s, y = 10.0, my = y + FD_H * 0.5;
	char num[16];
	cairo_save(cr);
	cairo_scale(cr, s, s);
	double was[3] = { g_led_rgb[0], g_led_rgb[1], g_led_rgb[2] };
	g_led_rgb[0] = 0.56;
	g_led_rgb[1] = 1.00;
	g_led_rgb[2] = 0.32;
	g_led_lit_only = true;
	/* at the left: what the dial is at; the focus light and how big */
	double x = FL_END;
	finder_line_mark(cr, NULL, l->mode, x + FL_MARK * 0.5, my);
	x += FL_MARK + 22.0;
	if (l->ready) {
		cairo_arc(cr, x + FD_LIGHT * 0.5, my, FD_LIGHT * 0.5, 0, 2 * M_PI);
		led_fill_path(cr, 1.0);
		x += FD_LIGHT + 22.0;
		snprintf(num, sizeof num, "%d", l->w);
		x += seg7_text(cr, num, x, y, FD_W, FD_H, FD_GAP, 1.0) + 12.0;
		/* by: a fourteen segment X, half as high */
		struct seg_box by = { x, y + FD_H * 0.25, FD_BY, FD_H * 0.5, FD_H * 0.05 };
		seg14_char(cr, &by, 'X', 1.0);
		x += FD_BY + 12.0;
		snprintf(num, sizeof num, "%d", l->h);
		seg7_text(cr, num, x, y, FD_W, FD_H, FD_GAP, 1.0);
	}
	/* at the right, from its end: the frame -- or the tape's time, and
	 * the red dot before it running -- and the self-timer before that */
	if (l->tape) {
		x = uw - FL_END - finder_time(cr, l->secs, 0.0, y, true);
		finder_time(cr, l->secs, x, y, false);
		if (l->rec) {
			x -= 14.0 + FD_LIGHT;
			finder_rec_dot(cr, x + FD_LIGHT * 0.5, my, FD_LIGHT * 0.5);
		}
		x -= 14.0 + FL_MARK;
		finder_line_mark(cr, finder_tape_path, 0, x + FL_MARK * 0.5, my);
	} else {
		x = uw - FL_END - (finder_digits(l->frame) * (FD_W + FD_GAP) - FD_GAP);
		snprintf(num, sizeof num, "%d", l->frame);
		seg7_text(cr, num, x, y, FD_W, FD_H, FD_GAP, 1.0);
		x -= 14.0 + FL_MARK;
		finder_line_mark(cr, finder_film_path, 0, x + FL_MARK * 0.5, my);
	}
	if (l->timer > 0) {
		x -= 40.0 + (finder_digits(l->timer) * (FD_W + FD_GAP) - FD_GAP);
		snprintf(num, sizeof num, "%d", l->timer);
		seg7_text(cr, num, x, y, FD_W, FD_H, FD_GAP, 1.0);
		x -= 14.0 + FL_MARK;
		finder_line_mark(cr, finder_timer_path, 0, x + FL_MARK * 0.5, my);
	}
	g_led_lit_only = false;
	g_led_rgb[0] = was[0];
	g_led_rgb[1] = was[1];
	g_led_rgb[2] = was[2];
	cairo_restore(cr);
}

/* the shade's depth at t (0 by the lit area .. 1 a penumbra away): a
 * step down out of the light at its edge, as a lamp's beam has, and then
 * on into the dark */
static double finder_shade(double t)
{
	double u = clampf(t, 0.0, 1.0);
	return 0.32 + 0.68 * u * u * (3.0 - 2.0 * u);
}

void desk_draw_finder_penumbra(cairo_t *cr, int n, int part)
{
	cairo_surface_t *s = cairo_get_target(cr);
	cairo_surface_flush(s);
	unsigned char *data = cairo_image_surface_get_data(s);
	int stride = cairo_image_surface_get_stride(s);
	int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
	if (!data || n < 1) {
		return;
	}
	/* a warm black, as the dark between the lamp's light and the room */
	const double r = 0.070, g = 0.056, bl = 0.040;
	for (int y = 0; y < h; y++) {
		uint32_t *row = (uint32_t *)(void *)(data + (size_t)y * (size_t)stride);
		for (int x = 0; x < w; x++) {
			double fx = n > 1 ? (double)x / (n - 1) : 0.5, fy = n > 1 ? (double)y / (n - 1) : 0.5;
			double t;
			switch (part) {
			case 0: t = 1.0 - fy; break;                  /* over the top edge */
			case 1: t = fx; break;                        /* right of it */
			case 2: t = fy; break;                        /* under it */
			case 3: t = 1.0 - fx; break;                  /* left of it */
			case 4: t = hypot(1.0 - fx, 1.0 - fy); break; /* the corners: top left */
			case 5: t = hypot(fx, 1.0 - fy); break;       /* top right */
			case 6: t = hypot(fx, fy); break;             /* bottom right */
			default: t = hypot(1.0 - fx, fy); break;      /* bottom left */
			}
			double a = finder_shade(t);
			row[x] = (unsigned)lround(a * 255.0) << 24 | (unsigned)lround(r * a * 255.0) << 16
				| (unsigned)lround(g * a * 255.0) << 8 | (unsigned)lround(bl * a * 255.0);
		}
	}
	cairo_surface_mark_dirty(s);
}

void desk_draw_finder_bevel(cairo_t *cr, int n, int part)
{
	cairo_surface_t *s = cairo_get_target(cr);
	cairo_surface_flush(s);
	unsigned char *data = cairo_image_surface_get_data(s);
	int stride = cairo_image_surface_get_stride(s);
	int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
	if (!data || n < 2) {
		return;
	}
	for (int y = 0; y < h; y++) {
		uint32_t *row = (uint32_t *)(void *)(data + (size_t)y * (size_t)stride);
		for (int x = 0; x < w; x++) {
			/* t: 0 at the edge of the picture, 1 at the glass's own; round
			 * the corners, out from the picture's corner, and nothing
			 * past the glass's rounded edge */
			double fx = (double)x / (n - 1), fy = (double)y / (n - 1), t;
			switch (part) {
			case 0: t = 1.0 - fy; break;
			case 1: t = fx; break;
			case 2: t = fy; break;
			case 3: t = 1.0 - fx; break;
			case 4: t = hypot(1.0 - fx, 1.0 - fy); break;
			case 5: t = hypot(fx, 1.0 - fy); break;
			case 6: t = hypot(fx, fy); break;
			default: t = hypot(1.0 - fx, fy); break;
			}
			double cut = clampf((1.0 - t) * (n - 1) + 0.5, 0.0, 1.0);
			/* clear glass: hardly a breath of white in it, its outer edge
			 * a bright line, and where it meets the picture a fine dark
			 * one */
			double white = (0.03 + 0.08 * t * t * t
				+ (t > 0.80 ? (t - 0.80) / 0.20 * 0.55 : 0.0)) * cut;
			double dark = t < 0.14 ? (1.0 - t / 0.14) * 0.40 : 0.0;
			/* the dark line over the white */
			double a = dark + white * (1.0 - dark);
			unsigned ai = (unsigned)lround(clampf(a, 0.0, 1.0) * 255.0);
			unsigned vi = (unsigned)lround(clampf(white * (1.0 - dark), 0.0, 1.0) * 255.0);
			row[x] = ai << 24 | vi << 16 | vi << 8 | vi;
		}
	}
	cairo_surface_mark_dirty(s);
}

void desk_draw_finder_glint(cairo_t *cr, int w, int h)
{
	cairo_surface_t *s = cairo_get_target(cr);
	cairo_surface_flush(s);
	unsigned char *data = cairo_image_surface_get_data(s);
	int stride = cairo_image_surface_get_stride(s);
	if (!data || w < 2 || h < 2) {
		return;
	}
	bool across = w >= h;   /* lying along x, or along y */
	for (int y = 0; y < h; y++) {
		uint32_t *row = (uint32_t *)(void *)(data + (size_t)y * (size_t)stride);
		for (int x = 0; x < w; x++) {
			double u = ((across ? x : y) + 0.5) / (across ? w : h) * 2.0 - 1.0;
			double v = ((across ? y : x) + 0.5) / (across ? h : w) * 2.0 - 1.0;
			/* long and soft along the edge, thin across it, hottest in the
			 * middle */
			double along = cos(fmin(fabs(u), 1.0) * M_PI * 0.5);
			double a = along * along * exp(-v * v * 5.0) * (0.7 + 0.3 * along);
			unsigned ai = (unsigned)lround(clampf(a, 0.0, 1.0) * 255.0);
			row[x] = ai << 24 | ai << 16 | ai << 8 | ai;
		}
	}
	cairo_surface_mark_dirty(s);
}

/* one channel of src at (x, y), between its pixels */
static double finder_tap(const unsigned char *src, int sw, int sh, int stride, double x,
	double y, int shift)
{
	x = clampf(x - 0.5, 0.0, sw - 1.001);
	y = clampf(y - 0.5, 0.0, sh - 1.001);
	int x0 = (int)x, y0 = (int)y;
	double fx = x - x0, fy = y - y0;
	const uint32_t *r0 = (const uint32_t *)(const void *)(src + (size_t)y0 * (size_t)stride);
	const uint32_t *r1 = (const uint32_t *)(const void *)(src + (size_t)(y0 + 1) * (size_t)stride);
	double a = (r0[x0] >> shift) & 0xff, b = (r0[x0 + 1] >> shift) & 0xff;
	double c = (r1[x0] >> shift) & 0xff, d = (r1[x0 + 1] >> shift) & 0xff;
	return ((a + (b - a) * fx) * (1.0 - fy) + (c + (d - c) * fx) * fy) / 255.0;
}

void desk_draw_finder_rim(const unsigned char *src, int sw, int sh, int sstride,
	unsigned char *dst, int dx, int dy, int dw, int dh, int dstride, double b, double r)
{
	/* the window: a rounded rectangle the size of the screen */
	double cx = sw * 0.5, cy = sh * 0.5;
	r = fmin(r, fmin(cx, cy));
	b = fmin(b, r);
	/* the lamp, above and a little to the left, and in front */
	const double lx = -0.22, ly = -0.80, lz = 0.56;
	const double hx = lx, hy = ly, hz = lz + 1.0, hl = sqrt(hx * hx + hy * hy + hz * hz);
	for (int y = 0; y < dh; y++) {
		uint32_t *row = (uint32_t *)(void *)(dst + (size_t)y * (size_t)dstride);
		for (int x = 0; x < dw; x++) {
			double px = dx + x + 0.5, py = dy + y + 0.5;
			double qx = fabs(px - cx) - (cx - r), qy = fabs(py - cy) - (cy - r);
			double ox = fmax(qx, 0.0), oy = fmax(qy, 0.0);
			double sd = hypot(ox, oy) + fmin(fmax(qx, qy), 0.0) - r;
			double d = -sd;   /* in from the window's edge */
			if (d >= b) {
				row[x] = 0;
				continue;
			}
			/* which way is out, here */
			double nx, ny;
			if (qx > 0.0 && qy > 0.0) {
				double l = hypot(ox, oy);
				nx = ox / l;
				ny = oy / l;
			} else if (qx > qy) {
				nx = 1.0;
				ny = 0.0;
			} else {
				nx = 0.0;
				ny = 1.0;
			}
			nx = px < cx ? -nx : nx;
			ny = py < cy ? -ny : ny;
			double dd = fmax(d, 0.0);
			double u = 1.0 - dd / b;   /* 1 at the edge, 0 where the glass is flat */
			/* the picture under it, bent in: what is a little way in seen
			 * at the edge, drawn out -- red a little further than blue */
			double bend = b * 0.40 * u * u;
			double rr = finder_tap(src, sw, sh, sstride, px - nx * bend * 1.07,
				py - ny * bend * 1.07, 16);
			double gg = finder_tap(src, sw, sh, sstride, px - nx * bend, py - ny * bend, 8);
			double bb = finder_tap(src, sw, sh, sstride, px - nx * bend * 0.93,
				py - ny * bend * 0.93, 0);
			/* the glass's face turning away towards its edge, lit by the
			 * lamp: a highlight where it faces it, and a little shade
			 * where it does not */
			double slope = pow(u, 1.6) * 0.92;
			double gx = nx * slope, gy = ny * slope, gz = sqrt(fmax(1.0 - slope * slope, 0.0));
			double lit = gx * lx + gy * ly + gz * lz;
			double spec = pow(fmax((gx * hx + gy * hy + gz * hz) / hl, 0.0), 60.0) * 0.55;
			double shade = 1.0 + (lit - lz) * 0.35;
			/* and its very edge, catching the light (or not) as a line */
			double edge = 0.0;
			if (dd < 2.0) {
				double face = fmax(nx * lx + ny * ly, 0.0);   /* out towards the lamp */
				edge = (1.0 - dd / 2.0) * (0.18 + 0.32 * face);
			}
			double fres = pow(u, 3.0) * 0.10;
			double add = spec + edge + fres;
			rr = clampf(rr * shade + add, 0.0, 1.0);
			gg = clampf(gg * shade + add, 0.0, 1.0);
			bb = clampf(bb * shade + add, 0.0, 1.0);
			/* past the edge, the eyecup: black rubber, the edge of the
			 * window smoothed into it over a pixel */
			if (sd > -0.5) {
				double k = clampf(sd + 0.5, 0.0, 1.0);
				double cup = 0.035 * (1.0 - fmin(sd / (r * 0.6), 1.0) * 0.6);
				rr = rr * (1.0 - k) + cup * k;
				gg = gg * (1.0 - k) + cup * 0.95 * k;
				bb = bb * (1.0 - k) + cup * 0.90 * k;
			}
			row[x] = 0xff000000u | (unsigned)lround(rr * 255.0) << 16
				| (unsigned)lround(gg * 255.0) << 8 | (unsigned)lround(bb * 255.0);
		}
	}
}

int desk_finder_ring_size(double pt)
{
	return (int)ceil(52.0 * pt);
}

void desk_draw_finder_ring(cairo_t *cr, double pt)
{
	double c = desk_finder_ring_size(pt) * 0.5;
	/* the spot, a fine circle; in it the split image, a smaller one cut
	 * across the middle -- each a light line over a soft dark one, to be
	 * seen on whatever is under it */
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 0) {
			cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);
			cairo_set_line_width(cr, 2.6 * pt);
		} else {
			cairo_set_source_rgba(cr, 1.0, 0.99, 0.95, 0.80);
			cairo_set_line_width(cr, fmax(1.0 * pt, 1.0));
		}
		cairo_new_sub_path(cr);
		cairo_arc(cr, c, c, 23.0 * pt, 0, 2 * M_PI);
		cairo_new_sub_path(cr);
		cairo_arc(cr, c, c, 8.5 * pt, 0, 2 * M_PI);
		cairo_move_to(cr, c - 8.5 * pt, c);
		cairo_line_to(cr, c + 8.5 * pt, c);
		cairo_stroke(cr);
	}
}

/* The self-timer counting down, beside what it is to take: on the
 * viewfinder's black, its red lamp (lit or not, as it blinks) and the
 * seconds left in red figures. */
void desk_finder_count_size(double pt, int *bw, int *bh)
{
	double s = finder_scale(pt);
	*bw = (int)ceil((FD_PAD + 26.0 + 16.0 + 2.0 * (FD_W + FD_GAP) - FD_GAP + FD_PAD) * s);
	*bh = (int)ceil((FD_H + 30.0) * s);
}

/* the lamp on the viewfinder's black: a red bead, glowing when lit */
static void finder_lamp(cairo_t *cr, double cx, double cy, bool lit)
{
	if (lit) {
		cairo_pattern_t *g = cairo_pattern_create_radial(cx, cy, 0.0, cx, cy, 26.0);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 0.25, 0.12, 0.55);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 1.0, 0.20, 0.10, 0.0);
		cairo_set_source(cr, g);
		cairo_arc(cr, cx, cy, 26.0, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
	cairo_arc(cr, cx, cy, 11.0, 0, 2 * M_PI);
	cairo_pattern_t *g = cairo_pattern_create_radial(cx - 3.0, cy - 3.5, 0.5, cx, cy, 11.0);
	if (lit) {
		cairo_pattern_add_color_stop_rgb(g, 0.0, 1.0, 0.85, 0.75);
		cairo_pattern_add_color_stop_rgb(g, 0.5, 1.0, 0.25, 0.12);
		cairo_pattern_add_color_stop_rgb(g, 1.0, 0.75, 0.08, 0.05);
	} else {
		cairo_pattern_add_color_stop_rgb(g, 0.0, 0.70, 0.40, 0.35);
		cairo_pattern_add_color_stop_rgb(g, 0.3, 0.38, 0.06, 0.04);
		cairo_pattern_add_color_stop_rgb(g, 1.0, 0.18, 0.03, 0.02);
	}
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

void desk_draw_finder_count(cairo_t *cr, int left, bool lit, double pt)
{
	double s = finder_scale(pt);
	double uw = FD_PAD + 26.0 + 16.0 + 2.0 * (FD_W + FD_GAP) - FD_GAP + FD_PAD, uh = FD_H + 30.0;
	cairo_save(cr);
	cairo_scale(cr, s, s);
	desk_rounded_path(cr, 0, 0, uw, uh, 12.0);
	cairo_set_source_rgba(cr, 0.008, 0.010, 0.008, 0.82);
	cairo_fill(cr);
	finder_lamp(cr, FD_PAD + 13.0, uh * 0.5, lit);
	char num[8];
	snprintf(num, sizeof num, "%2d", left < 0 ? 0 : left > 99 ? 99 : left);
	g_led_lit_only = true;
	seg7_text(cr, num, FD_PAD + 26.0 + 16.0, 15.0, FD_W, FD_H, FD_GAP, 1.0);
	g_led_lit_only = false;
	cairo_restore(cr);
}

/* ---- Print: the camera ------------------------------------------------
 *
 * A 35 mm reflex camera, black, its top plate satin chrome, seen from
 * behind and a little above, as it is held up: rendered in Blender
 * (art/camera/camera.py) into one picture, camera.png, with DEsk's data
 * (desk_art_find), and where its pieces are in it (camera-map.h, built
 * in). Here it is laid together as
 * it is set -- the camera whole, and over it what has moved: the mode
 * dial's knurling as it turns, and the pictures on the dial's top turning
 * with it; the power lever, the shutter button, the self-timer's knob and
 * its shadow wherever the slide is; the frame counter's figure under its
 * glass; the eyepiece seen through. The picture is read once (on a thread
 * of the desk's, before it is wanted: desk_finder_camera_load), and the
 * camera whole is kept scaled for the two sizes it is shown at, the hand's
 * and the one coming up to the eye, so that what moves is all that is
 * drawn again. In the camera's own units, DESK_FINDER_CAMERA_W across. */

#include "camera-map.h"
#include "camera-top.h"

#define CAM_INSET 8     /* a piece laid over the camera, this far in from its edges */

static pthread_once_t g_cam_once = PTHREAD_ONCE_INIT;
static cairo_surface_t *g_cam;   /* camera.png, read (NULL: it could not be) */

struct cam_read {
	const unsigned char *at;
	size_t left;
};

static cairo_status_t cam_read(void *closure, unsigned char *data, unsigned int length)
{
	struct cam_read *r = closure;
	if (length > r->left) {
		return CAIRO_STATUS_READ_ERROR;
	}
	memcpy(data, r->at, length);
	r->at += length;
	r->left -= length;
	return CAIRO_STATUS_SUCCESS;
}

/* One of the camera's pictures, read; NULL, and said why, when it could
 * not be. */
static cairo_surface_t *cam_picture(const char *file)
{
	char path[RF_DIR_LEN + 64];
	if (!desk_art_find("camera", file, path, sizeof path)) {
		fprintf(stderr, "desk: the camera's picture is not there (%s)\n", path);
		return NULL;
	}
	cairo_surface_t *img = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "desk: the camera's picture would not read (%s: %s)\n", path,
			cairo_status_to_string(cairo_surface_status(img)));
		cairo_surface_destroy(img);
		return NULL;
	}
	return img;
}

static void cam_load(void)
{
	g_cam = cam_picture("camera.png");
}

void desk_finder_camera_load(void)
{
	pthread_once(&g_cam_once, cam_load);
}

/* And the camera lying in the drawer (DESK_LOOK_CAMERA): its own picture,
 * read the first time it is laid there; NULL if it could not be. */
static pthread_once_t g_cam_top_once = PTHREAD_ONCE_INIT;
static cairo_surface_t *g_cam_top;

static void cam_top_load(void)
{
	g_cam_top = cam_picture("camera-top.png");
}

static cairo_surface_t *cam_top_lying(double ppu, double turn, double *ax, double *ay)
{
	pthread_once(&g_cam_top_once, cam_top_load);
	cairo_surface_t *pic = g_cam_top;
	if (!pic) {
		return NULL;
	}
	/* as it was rendered lying there, its body as wide as the terminal
	 * and a little more */
	double pw = cairo_image_surface_get_width(pic);
	double ph = cairo_image_surface_get_height(pic);
	double k = 2.42 * ppu / CAMERA_TOP_BODY_PX;
	double diag = hypot(pw, ph) * k + 4.0;
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		(int)ceil(diag), (int)ceil(diag));
	cairo_t *cr = cairo_create(img);
	cairo_translate(cr, diag * 0.5, diag * 0.5);
	cairo_rotate(cr, turn);
	cairo_scale(cr, k, k);
	cairo_translate(cr, -CAMERA_TOP_MID_X, -CAMERA_TOP_MID_Y);
	cairo_set_source_surface(cr, pic, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(img);
	*ax = *ay = diag * 0.5;
	return img;
}

static void *cam_load_run(void *arg)
{
	(void)arg;
	thread_to_the_back();
	desk_finder_camera_load();
	return NULL;
}

void desk_finder_camera_preload(void)
{
	pthread_attr_t at;
	pthread_t th;
	if (pthread_attr_init(&at) != 0) {
		return;
	}
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&th, &at, cam_load_run, NULL) != 0) {
		/* (read when it is first wanted, then) */
	}
	pthread_attr_destroy(&at);
}

/* One piece of camera.png, laid on the camera at s pixels to the unit:
 * over what is there, or (instead) in place of it -- all but its edges,
 * which are as the camera under them is, there for the scaling to read. */
static void cam_piece(cairo_t *cr, double s, const struct camera_piece *p, double dx,
	double dy, bool instead)
{
	cairo_surface_t *sub = cairo_surface_create_for_rectangle(g_cam, p->ax, p->ay, p->w, p->h);
	cairo_pattern_t *pat = cairo_pattern_create_for_surface(sub);
	cairo_pattern_set_extend(pat, CAIRO_EXTEND_PAD);
	cairo_pattern_set_filter(pat, CAIRO_FILTER_GOOD);
	cairo_save(cr);
	cairo_scale(cr, s / CAMERA_PX, s / CAMERA_PX);
	cairo_translate(cr, p->x + dx, p->y + dy);
	int in = instead ? CAM_INSET : 0;
	cairo_rectangle(cr, in, in, p->w - 2 * in, p->h - 2 * in);
	if (instead) {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	}
	cairo_set_source(cr, pat);
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_pattern_destroy(pat);
	cairo_surface_destroy(sub);
}

/* The camera whole, at s pixels to the unit: scaled once for each of the
 * last two sizes asked for. */
static cairo_surface_t *cam_whole(double s)
{
	static struct {
		double s;
		cairo_surface_t *img;
	} kept[2];
	for (int i = 0; i < 2; i++) {
		if (kept[i].img && kept[i].s == s) {
			return kept[i].img;
		}
	}
	int w = (int)ceil(DESK_FINDER_CAMERA_W * s), h = (int)ceil(DESK_FINDER_CAMERA_H * s);
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w > 0 ? w : 1,
		h > 0 ? h : 1);
	cairo_t *cr = cairo_create(img);
	cam_piece(cr, s, &CAMERA_BASE, 0.0, 0.0, false);
	cairo_destroy(cr);
	if (kept[1].img) {
		cairo_surface_destroy(kept[1].img);
	}
	kept[1] = kept[0];
	kept[0].s = s;
	kept[0].img = img;
	return img;
}

/* The tape on its way, t of it from out of sight (0) to in its slot (1):
 * brought up under the slot's mouth, slowing as it gets there, then
 * pushed in -- how far under the mouth it is (+), or in past it (-), in
 * the camera's units. Back the same way, coming out. */
#define CAM_TAPE_RISE 44.0   /* from this far under the mouth, out of sight */

static double cam_tape_dy(double t)
{
	if (t <= DESK_FINDER_TAPE_REACH) {
		double u = 1.0 - t / DESK_FINDER_TAPE_REACH;
		return CAM_TAPE_RISE * u * u;
	}
	double u = (t - DESK_FINDER_TAPE_REACH) / (1.0 - DESK_FINDER_TAPE_REACH);
	return -CAMERA_TAPE_PUSH * u * u * (3.0 - 2.0 * u);
}

/* The pictures on the dial's top -- painted, a little worn -- turned with
 * it: the one it is set to at the front, by the index mark. */
static void cam_dial_marks(cairo_t *cr, double s, double dial)
{
	double sq = CAMERA_DIAL_RY / CAMERA_DIAL_RX;
	cairo_save(cr);
	cairo_scale(cr, s, s);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
	for (int i = 0; i < 3; i++) {
		double a = (i - dial) * CAMERA_DIAL_STEP;
		cairo_save(cr);
		cairo_translate(cr, CAMERA_DIAL_X + sin(a) * CAMERA_DIAL_MARK_R,
			CAMERA_DIAL_Y + cos(a) * CAMERA_DIAL_MARK_R * sq);
		cairo_scale(cr, 1.0, sq);
		cairo_rotate(cr, -a);
		cairo_scale(cr, CAMERA_DIAL_MARK, CAMERA_DIAL_MARK);
		cairo_set_line_width(cr, 0.10);
		finder_mark_path(cr, i);
		cairo_set_source_rgba(cr, 0.90, 0.90, 0.86, 0.88);
		cairo_stroke(cr);
		cairo_restore(cr);
	}
	cairo_restore(cr);
}

/* The figure in the frame counter's window: printed on the disc under it,
 * as flat as the top plate is seen. */
static void cam_count(cairo_t *cr, double s, int count)
{
	char num[8];
	snprintf(num, sizeof num, "%d", count < 0 ? 0 : count % 100);
	double sq = CAMERA_COUNT_RY / CAMERA_COUNT_RX;
	cairo_save(cr);
	cairo_scale(cr, s, s);
	cairo_translate(cr, CAMERA_COUNT_X, CAMERA_COUNT_Y);
	cairo_scale(cr, 1.0, sq);
	cairo_arc(cr, 0.0, 0.0, CAMERA_COUNT_RX, 0.0, 2.0 * M_PI);
	cairo_clip(cr);
	desk_font_select(cr, DESK_FONT_ENGRAVE, num, true, false);
	cairo_set_font_size(cr, CAMERA_COUNT_RX * 0.95);
	cairo_text_extents_t te;
	cairo_text_extents(cr, num, &te);
	cairo_move_to(cr, -te.x_bearing - te.width * 0.5, -te.y_bearing - te.height * 0.5);
	cairo_set_source_rgba(cr, 0.93, 0.92, 0.88, 0.92);
	cairo_show_text(cr, num);
	cairo_restore(cr);
}

void desk_draw_finder_camera(cairo_t *cr, double s, const struct desk_finder_camera *c)
{
	desk_finder_camera_load();
	if (!g_cam || s <= 0.0) {
		return;
	}
	/* laid together on its own, the pieces in place of what they replace,
	 * and then over whatever it is drawn on */
	cairo_save(cr);
	cairo_rectangle(cr, 0.0, 0.0, DESK_FINDER_CAMERA_W * s, DESK_FINDER_CAMERA_H * s);
	cairo_clip(cr);
	cairo_push_group(cr);
	cairo_set_source_surface(cr, cam_whole(s), 0.0, 0.0);
	cairo_paint(cr);
	/* the dial: its knurling as far round a knurl as it has turned, and
	 * its pictures */
	double turn = c->dial * CAMERA_DIAL_STEP * CAMERA_KNURLS / (2.0 * M_PI);
	int q = (int)lround((turn - floor(turn)) * 4.0) % 4;
	if (q > 0) {
		cam_piece(cr, s, &CAMERA_KNURL[q - 1], 0.0, 0.0, true);
	}
	cam_dial_marks(cr, s, c->dial);
	/* the shutter button down, or the power lever on its way over */
	int down = (int)lround(clampf(c->press, 0.0, 1.0) * 3.0);
	int on = (int)lround(clampf(c->power, 0.0, 1.0) * 4.0);
	if (down > 0) {
		cam_piece(cr, s, &CAMERA_PRESS[down - 1], 0.0, 0.0, true);
	} else if (on < 4) {
		cam_piece(cr, s, &CAMERA_POWER[on], 0.0, 0.0, true);
	}
	/* the self-timer's knob where the slide is */
	cam_piece(cr, s, &CAMERA_KNOB, (c->timer - 1.0) * CAMERA_KNOB_STEP * CAMERA_PX, 0.0,
		false);
	/* the frame counter: its figure, its glass over it */
	cam_count(cr, s, c->count);
	cam_piece(cr, s, &CAMERA_DOME, 0.0, 0.0, false);
	/* the tape: all the way in, as it was rendered there, its shadow and
	 * all; or on its way -- what of it is in the slot out of sight over
	 * the slot's edge, the slot's flap pushed in ahead of it */
	if (c->tape >= 1.0) {
		cam_piece(cr, s, &CAMERA_TAPE_IN, 0.0, 0.0, true);
	} else if (c->tape > 0.0) {
		double dy = cam_tape_dy(c->tape);
		if (dy < 0.0) {
			cam_piece(cr, s, &CAMERA_SLOT, 0.0, 0.0, true);
		}
		cairo_save(cr);
		cairo_rectangle(cr, 0.0, CAMERA_TAPE_CLIP * s, DESK_FINDER_CAMERA_W * s,
			DESK_FINDER_CAMERA_H * s);
		cairo_clip(cr);
		cam_piece(cr, s, &CAMERA_TAPE, 0.0, dy * CAMERA_PX, false);
		cairo_restore(cr);
	}
	if (c->see_through) {
		cam_piece(cr, s, &CAMERA_SEE, 0.0, 0.0, true);
	}
	cairo_pop_group_to_source(cr);
	cairo_paint(cr);
	cairo_restore(cr);
}

enum desk_finder_part desk_finder_camera_at(double x, double y)
{
	/* the dial, its top and its drum */
	double my = (CAMERA_DIAL_Y + CAMERA_DIAL_FOOT) * 0.5;
	double dx = (x - CAMERA_DIAL_X) / (CAMERA_DIAL_RX + 4.0);
	double dy = (y - my) / ((CAMERA_DIAL_FOOT - CAMERA_DIAL_Y) * 0.5 + CAMERA_DIAL_RY + 4.0);
	if (dx * dx + dy * dy <= 1.0) {
		return DESK_FINDER_DIAL;
	}
	/* the self-timer's slide, and its marks over it */
	if (x >= CAMERA_KNOB_X - 12.0 && x <= CAMERA_KNOB_X + 2.0 * CAMERA_KNOB_STEP + 12.0
		&& y >= CAMERA_KNOB_Y - 20.0 && y <= CAMERA_KNOB_Y + 10.0) {
		return DESK_FINDER_TIMER;
	}
	/* the shutter button, and round it the power lever */
	dx = (x - CAMERA_SHUT_X) / (CAMERA_SHUT_R + 2.0);
	dy = (y - (CAMERA_SHUT_Y + 4.0)) / 10.0;
	if (dx * dx + dy * dy <= 1.0) {
		return DESK_FINDER_SHUTTER;
	}
	dx = (x - CAMERA_SHUT_X) / CAMERA_COLLAR_R;
	dy = (y - CAMERA_COLLAR_Y) / 17.0;
	if (dx * dx + dy * dy <= 1.0) {
		return DESK_FINDER_POWER;
	}
	/* the tape's slot, the tape in it, its eject mark */
	if (x >= CAMERA_SLOT_X0 && x <= CAMERA_SLOT_X1 && y >= CAMERA_SLOT_Y0 && y <= CAMERA_SLOT_Y1) {
		return DESK_FINDER_TAPE;
	}
	if (x >= CAMERA_LEFT && x <= CAMERA_RIGHT && y >= CAMERA_PLATE_FAR - 4.0 && y <= 360.0) {
		return DESK_FINDER_BODY;
	}
	if (x >= 228.0 && x <= 372.0 && y >= CAMERA_ROOF && y < CAMERA_PLATE_FAR) {
		return DESK_FINDER_BODY;
	}
	return DESK_FINDER_NOTHING;
}

void desk_finder_camera_dial(double *x, double *y)
{
	*x = CAMERA_DIAL_X;
	*y = CAMERA_DIAL_Y;
}

void desk_finder_camera_eyepiece(double *x, double *y, double *w, double *h)
{
	*x = CAMERA_EYE_X;
	*y = CAMERA_EYE_Y;
	*w = CAMERA_EYE_W;
	*h = CAMERA_EYE_H;
}
