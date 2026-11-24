/* Part of draw.c: the front door: what is drawn over the trace.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- what is drawn over the trace ---- */

/* the door's face about door point q, as it lands on the screen: metres on
 * it, x to the right and y down, to the screen */
static bool lk_face_matrix(const struct lk_scene *sc, const struct lk_cam *cam, vec3 q,
	cairo_matrix_t *m)
{
	const double e = 0.005;
	double x0, y0, x1, y1, x2, y2;
	if (!lk_project(cam, lk_from_door(sc, q), &x0, &y0)
		|| !lk_project(cam, lk_from_door(sc, vadd(q, v3(e, 0, 0))), &x1, &y1)
		|| !lk_project(cam, lk_from_door(sc, vadd(q, v3(0, -e, 0))), &x2, &y2)) {
		return false;
	}
	cairo_matrix_init(m, (x1 - x0) / e, (y1 - y0) / e, (x2 - x0) / e, (y2 - y0) / e, x0, y0);
	return true;
}

/* the cords the board hangs by, from its eyelets up to the nail, and their
 * shadows on the door under the porch lamp */
static void lk_cords(cairo_t *cr, const struct lk_scene *sc, const struct lk_cam *cam)
{
	vec3 nail = v3(LK_SIGN_X, LK_NAIL_Y - 0.0015, LK_DT + 0.0040);
	vec3 lamp = lk_to_door(sc, LK_LIGHTS[0].pos);
	/* the board, as it is on the screen: the shadows are behind it */
	double bq[4][2];
	bool board = true;
	for (int i = 0; i < 4; i++) {
		vec3 c = v3((i == 1 || i == 2 ? 0.5 : -0.5) * LK_SIGN_W, i >= 2 ? 0.0 : LK_SIGN_H, LK_SIGN_T);
		board = board && lk_project(cam, lk_from_door(sc, lk_from_sign(c)), &bq[i][0], &bq[i][1]);
	}
	for (int pass = 0; pass < 2; pass++) {
		cairo_save(cr);
		if (pass == 0 && board) {
			cairo_rectangle(cr, -1e5, -1e5, 2e5, 2e5);
			cairo_move_to(cr, bq[0][0], bq[0][1]);
			for (int i = 1; i < 4; i++) {
				cairo_line_to(cr, bq[i][0], bq[i][1]);
			}
			cairo_close_path(cr);
			cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
			cairo_clip(cr);
			cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
		}
		for (int s = -1; s <= 1; s += 2) {
			vec3 a = lk_from_sign(v3(s * LK_CORD_X, LK_SIGN_H - 0.022, LK_SIGN_T));
			vec3 pts[2] = { a, nail };
			double sx[2], sy[2];
			bool ok = true;
			for (int i = 0; i < 2; i++) {
				vec3 p = pts[i];
				if (pass == 0) {
					/* along the light, onto the face of the door */
					vec3 d = vsub(p, lamp);
					double t = (LK_DT - p.z) / (fabs(d.z) > 1e-6 ? d.z : -1e-6);
					p = vadd(p, vmul(d, fmax(t, 0.0)));
				}
				ok = ok && lk_project(cam, lk_from_door(sc, p), &sx[i], &sy[i]);
			}
			if (!ok) {
				continue;
			}
			double depth = vlen(vsub(lk_from_door(sc, nail), cam->eye));
			double lw = 0.0030 * cam->f / fmax(depth, 0.05);
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
			cairo_move_to(cr, sx[0], sy[0]);
			cairo_line_to(cr, sx[1], sy[1]);
			if (pass == 0) {
				cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);
				cairo_set_line_width(cr, lw * 2.2);
				cairo_stroke(cr);
				continue;
			}
			cairo_set_source_rgb(cr, 0.16, 0.11, 0.06);
			cairo_set_line_width(cr, lw * 1.25);
			cairo_stroke_preserve(cr);
			cairo_set_source_rgb(cr, 0.62, 0.48, 0.30);
			cairo_set_line_width(cr, lw * 0.55);
			cairo_stroke(cr);
		}
		cairo_restore(cr);
	}
}

/* The slot: a light for each letter in, the caret after them; a light
 * running along them while it is checked; red, and what was wrong, when
 * told no. Under the plate, what it has to say. */
static void lk_slot(cairo_t *cr, const struct lk_scene *sc, const struct lk_cam *cam,
	const struct desk_lock_look *lk, double fade)
{
	double cx = (LK_WIN_X0 + LK_WIN_X1) * 0.5, cy = (LK_WIN_Y0 + LK_WIN_Y1) * 0.5;
	cairo_matrix_t m;
	if (!lk_face_matrix(sc, cam, v3(cx, cy, LK_DT + LK_PL_T - 0.002), &m)) {
		return;
	}
	double ww = LK_WIN_X1 - LK_WIN_X0 - 0.003, wh = LK_WIN_Y1 - LK_WIN_Y0 - 0.003;
	cairo_save(cr);
	cairo_transform(cr, &m);
	cairo_rectangle(cr, -ww * 0.5, -wh * 0.5, ww, wh);
	cairo_clip(cr);
	double fail = lk->fail > 0.0 && lk->fail < 1.0 ? lk->fail : 0.0;
	if (fail > 0.0) {
		double a = fade * (1.0 - pow(fail, 4.0));
		double shake = sin(fail * 40.0) * (1.0 - fail) * 0.003;
		cairo_pattern_t *g = cairo_pattern_create_radial(0, 0, 0.0, 0, 0, ww * 0.55);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 0.20, 0.08, 0.30 * a);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 1.0, 0.20, 0.08, 0.05 * a);
		cairo_set_source(cr, g);
		cairo_paint(cr);
		cairo_pattern_destroy(g);
		show_text_as(cr, DESK_FONT_PRINT, TR("That is not it"), shake, wh * 0.22, wh * 0.52,
			true, 1.0, 0.42, 0.26, a, 0);
		cairo_restore(cr);
		return;
	}
	enum { N = 14 };
	double pitch = (ww - 0.016) / N, r = 0.0052;
	int lit = lk->typed > N ? N : lk->typed;
	double x0 = -ww * 0.5 + 0.008;
	for (int i = 0; i < N; i++) {
		double x = x0 + pitch * (i + 0.5);
		double a = 0.0;
		if (lk->checking) {
			double ph = fmod(lk->now_s * 1.6, 1.0) * N;
			double d = fabs(i - ph);
			a = (i < lit ? 0.45 : 0.10) + 0.55 * exp(-d * d * 0.5);
		} else if (i < lit) {
			a = 1.0;
		}
		cairo_arc(cr, x, 0, r, 0, 2 * M_PI);
		cairo_set_source_rgba(cr, 0.45, 0.16, 0.06, 0.22 * fade);
		cairo_fill(cr);
		if (a > 0.01) {
			a *= fade;
			cairo_pattern_t *g = cairo_pattern_create_radial(x, 0, 0, x, 0, r * 2.8);
			cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 0.96, 0.82, a);
			cairo_pattern_add_color_stop_rgba(g, 0.30, 1.0, 0.62, 0.20, a * 0.95);
			cairo_pattern_add_color_stop_rgba(g, 1.0, 1.0, 0.45, 0.10, 0.0);
			cairo_set_source(cr, g);
			cairo_arc(cr, x, 0, r * 2.8, 0, 2 * M_PI);
			cairo_fill(cr);
			cairo_pattern_destroy(g);
		}
	}
	if (lk->caret && !lk->checking) {
		double x = lk->typed >= N ? x0 + pitch * N - 0.002 : x0 + pitch * (lit + 0.5) - r - 0.0012;
		if (lk->typed == 0) {
			x = x0 + 0.002;
		}
		cairo_pattern_t *g = cairo_pattern_create_linear(x - 0.004, 0, x + 0.004, 0);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 0.55, 0.15, 0.0);
		cairo_pattern_add_color_stop_rgba(g, 0.5, 1.0, 0.80, 0.45, 0.95 * fade);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 1.0, 0.55, 0.15, 0.0);
		cairo_set_source(cr, g);
		cairo_rectangle(cr, x - 0.004, -wh * 0.36, 0.008, wh * 0.72);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
		cairo_set_source_rgba(cr, 1.0, 0.92, 0.75, 0.95 * fade);
		cairo_rectangle(cr, x - 0.0006, -wh * 0.32, 0.0012, wh * 0.64);
		cairo_fill(cr);
	}
	cairo_restore(cr);
	const char *say = lk->checking ? TR("Checking\xe2\x80\xa6") : lk->caps ? TR("Caps Lock is on") : NULL;
	if (say && lk_face_matrix(sc, cam, v3(cx, LK_PL_Y0 - 0.026, LK_DT), &m)) {
		cairo_save(cr);
		cairo_transform(cr, &m);
		show_text_as(cr, DESK_FONT_PRINT, say, 0.0008, 0.0010, 0.022, true, 0.0, 0.0, 0.0,
			0.60 * fade, 0);
		show_text_as(cr, DESK_FONT_PRINT, say, 0.0, 0.0, 0.022, true, 1.0, 0.86, 0.62,
			0.90 * fade, 0);
		cairo_restore(cr);
	}
}

/* the colour along each edge of the picture of the room: the floor, the
 * ceiling, the left wall, (the far wall), the right */
static void lk_edges(cairo_surface_t *s, vec3 edge[5])
{
	for (int i = 0; i < 5; i++) {
		edge[i] = v3(0.10, 0.07, 0.05);
	}
	if (!s || cairo_image_surface_get_format(s) != CAIRO_FORMAT_ARGB32) {
		return;
	}
	cairo_surface_flush(s);
	const unsigned char *px = cairo_image_surface_get_data(s);
	int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
	int stride = cairo_image_surface_get_stride(s);
	int bx = w / 40 + 1, by = h / 40 + 1;
	static const int SIDE[5] = { 3, 2, 0, -1, 1 };   /* bottom, top, left, -, right */
	for (int i = 0; i < 5; i++) {
		if (SIDE[i] < 0) {
			continue;
		}
		int x0 = SIDE[i] == 1 ? w - bx : 0, x1 = SIDE[i] == 0 ? bx : w;
		int y0 = SIDE[i] == 3 ? h - by : 0, y1 = SIDE[i] == 2 ? by : h;
		double r = 0, g = 0, b = 0;
		long n = 0;
		for (int y = y0; y < y1; y += 2) {
			const uint32_t *row = (const uint32_t *)(const void *)(px + (size_t)y * stride);
			for (int x = x0; x < x1; x += 2) {
				r += (row[x] >> 16) & 0xff;
				g += (row[x] >> 8) & 0xff;
				b += row[x] & 0xff;
				n++;
			}
		}
		if (n > 0) {
			edge[i] = v3(r / n / 255.0, g / n / 255.0, b / n / 255.0);
		}
	}
	edge[3] = vmul(vadd(vadd(edge[0], edge[1]), vadd(edge[2], edge[4])), 0.25);
}

/* ---- what moves out there ----
 *
 * Rain, snow and lightning, the lights in the garland twinkling, the
 * balloons' strings: drawn on a sheet of their own over the door, every
 * frame while there is any of it, so that the door -- traced, and kept --
 * is never traced again for a raindrop. Where the eye is, it is where the
 * door was last drawn from. */
static struct {
	bool have;
	struct lk_cam cam;
	int w, h;
} g_lk_view;

void desk_draw_lock(cairo_t *cr, int w, int h, const struct desk_lock_look *lk)
{
	/* as fine as it is drawn (k), and as fine as the door is for this
	 * screen (kn: what its board and its films are made for, whatever size
	 * the compositor lays a frame of a film at) */
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double k = clampf(hypot(kx, ky), 0.25, 3.0), kd = k, kn = lk_scale_for(w, h);
	if (desk_gfx_level() == DESK_GFX_2D) {
		k = fmax(k * 0.5, 0.25);   /* traced at half the screen's size, once */
		kn = fmax(kn * 0.5, 0.25);
	}
	if (lk->push >= 0.999 && lk->inside) {
		/* through: the room, as the compositor will show it */
		double iw = cairo_image_surface_get_width(lk->inside);
		double ih = cairo_image_surface_get_height(lk->inside);
		cairo_save(cr);
		cairo_scale(cr, w / iw, h / ih);
		cairo_set_source_surface(cr, lk->inside, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
		return;
	}
	lk_prepare(w, h, kn, lk->name ? lk->name : "", lk->picture,
		lk->mood.set && (lk->mood.decor & DESK_DECOR_BIRTHDAY));
	struct lk_scene sc;
	lk_scene_for(&sc, w, h, lk);
	lk_tex_of(&sc.sign, g_lock.sign);
	lk_tex_of(&sc.gold, g_lock.gold);
	lk_tex_of(&sc.plate, g_lock.plate);
	if (lk->inside) {
		lk_tex_of(&sc.pic, lk->inside);
		if (lk->open > 0.0 || lk->push > 0.0) {
			lk_edges(lk->inside, sc.edge);
		}
	}
	/* through the doorway, the room itself, built */
	struct lk_cam end, cam;
	bool room = lk->inside && (lk->open > 0.0 || lk->push > 0.0)
		&& lk_room_build(&sc.room, w, h, lk->inside_view, lk->inside, sc.edge, &end);
	lk_camera_to(w, h, lk, room ? &end : NULL, &cam);
	/* filmed ahead, or traced as it goes */
	bool coming;
	if (!lkf_paint(cr, w, h, kn, kd, lk, &sc, &cam, &coming)) {
		struct lk_frame *f = lk_frame_for(&sc, &cam, w, h, k, lk->inside, coming);
		if (f->img) {
			double iw = cairo_image_surface_get_width(f->img);
			double ih = cairo_image_surface_get_height(f->img);
			cairo_save(cr);
			cairo_scale(cr, w / iw, h / ih);
			cairo_set_source_surface(cr, f->img, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
			cairo_paint(cr);
			cairo_restore(cr);
		}
	}
	lk_cords(cr, &sc, &cam);
	g_lk_view.have = true;
	g_lk_view.cam = cam;
	g_lk_view.w = w;
	g_lk_view.h = h;
	double fade = 1.0 - clampf(lk->open * 5.0, 0.0, 1.0);
	if (fade > 0.0) {
		lk_slot(cr, &sc, &cam, lk, fade);
	}
}

bool desk_lock_key_at(int w, int h, double x, double y)
{
	struct desk_lock_look rest;
	memset(&rest, 0, sizeof rest);
	struct lk_cam cam;
	lk_camera(w, h, &rest, &cam);
	struct lk_scene sc;
	lk_scene_for(&sc, w, h, &rest);
	double kx, ky;
	if (!lk_project(&cam, lk_from_door(&sc, v3(LK_KEY_X, LK_KEY_Y, LK_DT + 0.05)), &kx, &ky)) {
		return false;
	}
	double r = 0.065 * cam.f / (cam.eye.z - LK_DZ);
	return hypot(x - kx, y - ky) < r;
}

bool desk_lock_fx_wanted(const struct desk_door_mood *m)
{
	return m && m->set && (m->weather == DESK_WX_RAIN || m->weather == DESK_WX_SNOW
		|| m->weather == DESK_WX_STORM
		|| (m->decor & (DESK_DECOR_XMAS | DESK_DECOR_NEWYEAR | DESK_DECOR_BIRTHDAY
			| DESK_DECOR_HALLOWEEN)));
}

static double lkfx_hash(int i, int salt)
{
	return room_hash(i * 31 + salt, salt * 7 + 1013);
}

/* a light's glow, its own breath: each slow and out of step */
static void lkfx_bulbs(cairo_t *cr, const struct lk_cam *cam, unsigned decor, double t, double a)
{
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
	for (int run = 0; run < 3; run++) {
		double len = run == 1 ? 2.0 * LKG_X : LKG_TOP;
		double s0 = run == 1 ? LKG_TOP : 0.0;
		int k0 = (int)ceil((s0 - 0.05) / LKG_PITCH), k1 = (int)floor((s0 + len - 0.05) / LKG_PITCH);
		for (int k = k0 < 0 ? 0 : k0; k <= k1; k++) {
			vec3 c = lkd_bulb_at(run, k);
			c.z += 0.014;
			double x, y;
			if (!lk_project(cam, c, &x, &y)) {
				continue;
			}
			double depth = vlen(vsub(c, cam->eye));
			double r = 0.032 * cam->f / fmax(depth, 0.05);
			if (x < -r || y < -r || x > g_lk_view.w + r || y > g_lk_view.h + r) {
				continue;
			}
			vec3 col = lkd_bulb_col(decor, run, k);
			double h1 = lkfx_hash(k, run + 1), h2 = lkfx_hash(k, run + 11);
			double breath = 0.5 + 0.5 * sin(t * (0.7 + 0.9 * h1) + h2 * 6.283);
			double twinkle = pow(breath, 3.0);
			double g = a * (0.30 + 0.70 * twinkle);
			cairo_pattern_t *p = cairo_pattern_create_radial(x, y, 0.0, x, y, r);
			cairo_pattern_add_color_stop_rgba(p, 0.0, col.x, col.y, col.z, 0.55 * g);
			cairo_pattern_add_color_stop_rgba(p, 0.25, col.x, col.y, col.z, 0.22 * g);
			cairo_pattern_add_color_stop_rgba(p, 1.0, col.x, col.y, col.z, 0.0);
			cairo_set_source(cr, p);
			cairo_arc(cr, x, y, r, 0, 2.0 * M_PI);
			cairo_fill(cr);
			cairo_pattern_destroy(p);
		}
	}
	cairo_restore(cr);
}

/* the jack-o'-lantern's light, never quite steady, glowing out of its face
 * -- on the door, swung in as far as it is (radians), so facing away
 * as it goes; brightest at night */
static void lkfx_jack(cairo_t *cr, const struct lk_cam *cam, double door, double t, double a,
	double night)
{
	const struct lkd_pumpkin *pk = &g_lkp[0];
	vec3 f = vadd(pk->c, vadd(vmul(pk->y, -0.004), vmul(pk->z, 0.058)));
	double cd = cos(door), sd = sin(door);
	vec3 c = v3(f.x * cd + f.z * sd + LK_HX, f.y, -f.x * sd + f.z * cd + LK_HZ);
	vec3 out = v3(pk->z.x * cd + pk->z.z * sd, pk->z.y, -pk->z.x * sd + pk->z.z * cd);
	double facing = vdot(out, vnorm(vsub(cam->eye, c)));
	double x, y;
	if (facing <= 0.0 || !lk_project(cam, c, &x, &y)) {
		return;
	}
	double r = 0.085 * cam->f / fmax(vlen(vsub(c, cam->eye)), 0.05);
	double flick = 0.80 + 0.10 * sin(t * 7.9 + 0.3) * sin(t * 3.1) + 0.06 * sin(t * 13.7 + 1.1)
		+ 0.04 * sin(t * 21.3 + 2.0);
	double g = a * flick * smoothstepf(0.0, 0.5, facing) * (0.35 + 0.65 * night);
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
	cairo_pattern_t *p = cairo_pattern_create_radial(x, y, 0.0, x, y, r);
	cairo_pattern_add_color_stop_rgba(p, 0.0, LKJ_LIGHT.x, LKJ_LIGHT.y, LKJ_LIGHT.z, 0.30 * g);
	cairo_pattern_add_color_stop_rgba(p, 0.35, LKJ_LIGHT.x, LKJ_LIGHT.y, LKJ_LIGHT.z, 0.11 * g);
	cairo_pattern_add_color_stop_rgba(p, 1.0, LKJ_LIGHT.x, LKJ_LIGHT.y, LKJ_LIGHT.z, 0.0);
	cairo_set_source(cr, p);
	cairo_arc(cr, x, y, r, 0, 2.0 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(p);
	cairo_restore(cr);
}

/* the balloons' strings, down to the nail in the casing, swaying a little;
 * the ribbon's curled ends hanging from the knot there */
static void lkfx_strings(cairo_t *cr, const struct lk_cam *cam, double t, double a, double lit)
{
	vec3 tie = v3(LKB_TIE_X, LKB_TIE_Y, LKB_TIE_Z);
	double tx, ty;
	if (!lk_project(cam, tie, &tx, &ty)) {
		return;
	}
	double depth = vlen(vsub(tie, cam->eye));
	double lw = 0.0022 * cam->f / fmax(depth, 0.05);
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	double v = 0.45 + 0.55 * lit;
	for (int i = 0; i < 5; i++) {
		vec3 k = lkd_knot(i);
		vec3 mid = vmix(k, tie, 0.5);
		mid.x += 0.010 * sin(t * (0.55 + 0.13 * i) + i * 1.7);
		mid.z += 0.012 * sin(t * (0.43 + 0.11 * i) + i * 2.9);
		double kx, ky, mx, my;
		if (!lk_project(cam, k, &kx, &ky) || !lk_project(cam, mid, &mx, &my)) {
			continue;
		}
		cairo_move_to(cr, kx, ky);
		cairo_curve_to(cr, mx, my, mx, my, tx, ty);
		cairo_set_source_rgba(cr, 0.05, 0.03, 0.02, 0.35 * a);
		cairo_set_line_width(cr, lw * 1.8);
		cairo_stroke_preserve(cr);
		vec3 c = LKB_COL[i];
		cairo_set_source_rgba(cr, (0.55 + 0.45 * c.x) * v, (0.55 + 0.45 * c.y) * v,
			(0.55 + 0.45 * c.z) * v, 0.95 * a);
		cairo_set_line_width(cr, lw);
		cairo_stroke(cr);
	}
	/* the ends of the ribbon, curled, hanging from the knot */
	double px = cam->f / fmax(depth, 0.05);
	for (int i = 0; i < 3; i++) {
		vec3 c = LKB_COL[i * 2 % 5];
		cairo_move_to(cr, tx, ty);
		double ph = t * (0.8 + 0.2 * i) + i;
		for (int j = 1; j <= 28; j++) {
			double u = j / 28.0;
			double cx = tx + (sin(u * 18.0 + i * 2.0) * 0.010 + (i - 1) * 0.018 * u
				+ 0.002 * sin(ph)) * px;
			double cy = ty + u * (0.11 + 0.02 * i) * px;
			cairo_line_to(cr, cx, cy);
		}
		cairo_set_source_rgba(cr, (0.45 + 0.55 * c.x) * v, (0.45 + 0.55 * c.y) * v,
			(0.45 + 0.55 * c.z) * v, 0.9 * a);
		cairo_set_line_width(cr, lw * 1.3);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

/* Rain: streaks, the near ones long and quick and faint, the far ones
 * short; lit warm by the porch lamp at night, grey by day. */
static void lkfx_rain(cairo_t *cr, int w, int h, double t, double heavy, double a, bool night)
{
	int n = (int)(70 + 300 * heavy);
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	double wind = 0.10 + 0.08 * heavy;
	for (int band = 0; band < 3; band++) {
		double z = (band + 0.5) / 3.0;
		cairo_new_path(cr);
		for (int i = band; i < n; i += 3) {
			double h1 = lkfx_hash(i, 3), h2 = lkfx_hash(i, 5), h3 = lkfx_hash(i, 7);
			double speed = 1.5 + 1.6 * z + 0.4 * h3;
			double len = h * (0.035 + 0.075 * z) * (0.8 + 0.4 * h1);
			double y = fmod(h2 + t * speed * (0.9 + 0.2 * h1), 1.0) * (h + len * 2.0) - len;
			double x = h1 * w * 1.25 - w * 0.15 + wind * y;
			cairo_move_to(cr, x, y);
			cairo_line_to(cr, x + wind * len, y + len);
		}
		double al = (0.05 + 0.14 * z) * a * (0.6 + 0.6 * heavy);
		if (night) {
			cairo_set_source_rgba(cr, 1.0, 0.86, 0.66, al * 1.3);
		} else {
			cairo_set_source_rgba(cr, 0.86, 0.89, 0.95, al);
		}
		cairo_set_line_width(cr, (0.6 + 1.4 * z) * h / 900.0);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

/* Snow: flakes drifting down and to and fro, the near ones big and soft. */
static void lkfx_snow(cairo_t *cr, int w, int h, double t, double heavy, double a)
{
	int n = (int)(60 + 220 * heavy);
	double k = h / 900.0;
	cairo_save(cr);
	for (int band = 0; band < 4; band++) {
		double z = (band + 0.5) / 4.0;
		double r = (1.1 + 4.2 * z * z) * k;
		cairo_new_path(cr);
		for (int i = band; i < n; i += 4) {
			double h1 = lkfx_hash(i, 21), h2 = lkfx_hash(i, 23), h3 = lkfx_hash(i, 29);
			double speed = 0.045 + 0.075 * z + 0.02 * h3;
			double y = fmod(h2 + t * speed, 1.0) * (h + 20.0 * k) - 10.0 * k;
			double x = h1 * w + sin(t * (0.4 + 0.5 * h3) + h1 * 40.0) * (8.0 + 22.0 * z) * k
				+ t * 6.0 * k * (0.5 + z);
			x = fmod(x, (double)w + 40.0 * k);
			cairo_new_sub_path(cr);
			cairo_arc(cr, x, y, r, 0, 2.0 * M_PI);
		}
		double al = (0.50 + 0.35 * z) * a;
		if (band == 3) {
			/* out of focus, near the eye */
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, al * 0.35);
			cairo_fill(cr);
		} else {
			cairo_set_source_rgba(cr, 0.97, 0.98, 1.0, al);
			cairo_fill(cr);
		}
	}
	cairo_restore(cr);
}

/* lightning, now and then: a flash, and another after it */
static double lkfx_flash(double t)
{
	double period = 11.0;
	double seg = floor(t / period);
	double at = seg * period + lkfx_hash((int)seg, 41) * (period - 1.0);
	double d = t - at;
	if (d < 0.0 || d > 0.6) {
		return 0.0;
	}
	double f = exp(-d * 22.0) + 0.7 * exp(-lk_sq((d - 0.16) / 0.035));
	return lkfx_hash((int)seg, 43) < 0.75 ? f : 0.0;
}

/* ---- the New Year's party, out at the door ----
 *
 * Wherever the house is, the New Year at its door is a party: a string of
 * pennants across the top of the door saying so, in gold, black and
 * silver, curled streamers hanging from the ends of it beside the casing,
 * confetti coming down. Drawn over the door with the rest of what moves (the pennants
 * stir), their shadows thrown on the door by the lamp or the sun. */

/* the light on something out there, as a colour to multiply by */
static vec3 lkfx_light(const struct lk_rig *g)
{
	double porch = clampf(g->porch, 0.0, 1.0);
	vec3 day = v3(0.95, 0.95, 0.95);
	vec3 night = v3(0.95, 0.82, 0.64);
	return vmix(day, night, porch);
}

/* where a point's shadow falls on the face of the door and the wall, from
 * the lamp at night or the sun by day; false for no shadow */
static bool lkfx_shadow_at(const struct lk_rig *g, vec3 p, vec3 *s, double *a)
{
	const double face = LK_DT;
	if (g->porch > 0.1) {
		vec3 lamp = LK_LIGHTS[0].pos;
		double dz = p.z - lamp.z;
		if (fabs(dz) < 1e-4) {
			return false;
		}
		double k = (face - p.z) / dz;
		*s = vadd(p, vmul(vsub(p, lamp), k));
		*a = 0.30 * g->porch;
		return true;
	}
	if (g->sun_col.x > 0.0 && g->sun.z > 0.08) {
		double k = (p.z - face) / g->sun.z;
		*s = vsub(p, vmul(g->sun, k));
		*a = 0.30 * clampf(g->sun_col.x / 2.5, 0.0, 1.0);
		return true;
	}
	return false;
}

/* the pennants' string: from nails at the tops of the casing, sagging */
#define LKN_N 14
static vec3 lkfx_string_at(double u, double t)
{
	/* low enough to be read from the step: the eye there sees no higher
	 * than about 1.92 m up the door */
	const double x0 = -0.64, x1 = 0.64, y = 1.918, z = 0.065;
	double sag = 0.030 + 0.003 * sin(t * 0.7);
	return v3(x0 + (x1 - x0) * u, y - sag * 4.0 * u * (1.0 - u), z + 0.02 * 4.0 * u * (1.0 - u));
}

/* a five-pointed star, r across, about x, y */
static void lkfx_star(cairo_t *cr, double x, double y, double r)
{
	for (int k = 0; k < 10; k++) {
		double a = -M_PI * 0.5 + k * M_PI / 5.0, rr = (k & 1) ? r * 0.42 : r;
		if (k == 0) {
			cairo_move_to(cr, x + cos(a) * rr, y + sin(a) * rr);
		} else {
			cairo_line_to(cr, x + cos(a) * rr, y + sin(a) * rr);
		}
	}
	cairo_close_path(cr);
}

static void lkfx_pennants(cairo_t *cr, const struct lk_cam *cam, const struct lk_rig *g, double t,
	double a)
{
	static const char WORDS[LKN_N + 1] = "HAPPY NEW YEAR";
	static const double COL[3][3] = { { 0.95, 0.75, 0.30 }, { 0.10, 0.09, 0.09 },
		{ 0.84, 0.86, 0.90 } };
	vec3 L = lkfx_light(g);
	struct { vec3 tl, tr, ap; } f[LKN_N];
	for (int i = 0; i < LKN_N; i++) {
		double u0 = 0.045 + i * (0.91 / LKN_N), u1 = u0 + 0.91 / LKN_N * 0.92;
		f[i].tl = lkfx_string_at(u0, t);
		f[i].tr = lkfx_string_at(u1, t);
		vec3 mid = vmix(f[i].tl, f[i].tr, 0.5);
		f[i].ap = vadd(mid, v3(0.005 * sin(t * 1.7 + i * 1.3), -0.074, 0.004 * sin(t * 1.3 + i)));
	}
	cairo_save(cr);
	/* the shadows first: on the door, behind */
	for (int i = 0; i < LKN_N; i++) {
		vec3 q[3] = { f[i].tl, f[i].tr, f[i].ap }, s;
		double sa = 0.0, x, y;
		bool ok = true;
		for (int k = 0; k < 3 && ok; k++) {
			ok = lkfx_shadow_at(g, q[k], &s, &sa) && lk_project(cam, s, &x, &y);
			if (ok) {
				if (k == 0) {
					cairo_move_to(cr, x, y);
				} else {
					cairo_line_to(cr, x, y);
				}
			}
		}
		if (ok) {
			cairo_close_path(cr);
			cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, sa * a);
			cairo_fill(cr);
		} else {
			cairo_new_path(cr);
		}
	}
	/* the string */
	for (int k = 0; k <= 40; k++) {
		double x, y;
		if (lk_project(cam, lkfx_string_at(k / 40.0, t), &x, &y)) {
			if (k == 0) {
				cairo_move_to(cr, x, y);
			} else {
				cairo_line_to(cr, x, y);
			}
		}
	}
	double depth = vlen(vsub(lkfx_string_at(0.5, t), cam->eye));
	double px = cam->f / fmax(depth, 0.05);   /* screen pixels to a metre, there */
	cairo_set_source_rgba(cr, 0.92 * L.x, 0.86 * L.y, 0.70 * L.z, 0.9 * a);
	cairo_set_line_width(cr, fmax(0.0022 * px, 1.0));
	cairo_stroke(cr);
	/* the pennants, each with its letter */
	for (int i = 0; i < LKN_N; i++) {
		double x[3], y[3];
		vec3 q[3] = { f[i].tl, f[i].tr, f[i].ap };
		bool ok = true;
		for (int k = 0; k < 3; k++) {
			ok = ok && lk_project(cam, q[k], &x[k], &y[k]);
		}
		if (!ok) {
			continue;
		}
		const double *c = COL[i % 3];
		cairo_move_to(cr, x[0], y[0]);
		cairo_line_to(cr, x[1], y[1]);
		cairo_line_to(cr, x[2], y[2]);
		cairo_close_path(cr);
		/* foil: a sheen across it */
		cairo_pattern_t *sh = cairo_pattern_create_linear(x[0], y[0], x[2], y[2]);
		cairo_pattern_add_color_stop_rgba(sh, 0.0, fmin(1.0, c[0] * L.x * 1.15),
			fmin(1.0, c[1] * L.y * 1.15), fmin(1.0, c[2] * L.z * 1.15), a);
		cairo_pattern_add_color_stop_rgba(sh, 1.0, c[0] * L.x * 0.72, c[1] * L.y * 0.72,
			c[2] * L.z * 0.72, a);
		cairo_set_source(cr, sh);
		cairo_fill(cr);
		cairo_pattern_destroy(sh);
		/* the letter, on the pennant: its own frame, metres across and down */
		double w = vlen(vsub(f[i].tr, f[i].tl)), h = vlen(vsub(f[i].ap, vmix(f[i].tl, f[i].tr, 0.5)));
		double mx = (x[0] + x[1]) * 0.5, my = (y[0] + y[1]) * 0.5;
		cairo_matrix_t mt;
		cairo_matrix_init(&mt, (x[1] - x[0]) / w, (y[1] - y[0]) / w, (x[2] - mx) / h, (y[2] - my) / h,
			mx, my);
		cairo_save(cr);
		cairo_transform(cr, &mt);
		const double *ink = i % 3 == 1 ? COL[0] : COL[1];
		if (WORDS[i] == ' ') {
			lkfx_star(cr, 0.0, h * 0.36, 0.014);
			cairo_set_source_rgba(cr, ink[0] * L.x, ink[1] * L.y, ink[2] * L.z, 0.95 * a);
			cairo_fill(cr);
		} else {
			char ch[2] = { WORDS[i], '\0' };
			show_text_as(cr, DESK_FONT_ENGRAVE, ch, 0.0, h * 0.52, 0.034, true,
				ink[0] * L.x, ink[1] * L.y, ink[2] * L.z, 0.95 * a, 0);
		}
		cairo_restore(cr);
	}
	cairo_restore(cr);
}

/* Streamers: paper curled tight, hung from the ends of the string down
 * the casing, turning to and from the eye as they curl, stirring. */
static void lkfx_streamers(cairo_t *cr, const struct lk_cam *cam, const struct lk_rig *g,
	double t, double a)
{
	static const double COL[4][3] = { { 0.95, 0.74, 0.28 }, { 0.82, 0.84, 0.88 },
		{ 0.86, 0.18, 0.52 }, { 0.12, 0.66, 0.72 } };
	vec3 L = lkfx_light(g);
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	for (int i = 0; i < 6; i++) {
		double side = i < 3 ? -1.0 : 1.0;
		/* out past the casing, over the brick */
		vec3 top = v3(side * (0.635 + 0.022 * (i % 3)), 1.915 - 0.012 * (i % 3), 0.050 + 0.01 * (i % 3));
		double len = 0.42 + 0.17 * ((i * 5) % 3) / 2.0;
		const double *c = COL[(i + (i / 3)) % 4];
		vec3 prev = top;
		double px0 = 0, py0 = 0;
		bool have = lk_project(cam, prev, &px0, &py0);
		int n = 70;
		for (int k = 1; k <= n; k++) {
			double s = len * k / n, u = s / len;
			double ph = s / 0.034 * 2.0 * M_PI + i * 1.7;
			double R = 0.020 * (1.0 - 0.45 * u);
			double sway = 0.016 * u * u * sin(t * (0.9 + 0.2 * i) + i * 2.1);
			vec3 p = v3(top.x + R * cos(ph) + sway, top.y - s, top.z + R * sin(ph) + 0.004);
			double x, y;
			if (!lk_project(cam, p, &x, &y)) {
				have = false;
				continue;
			}
			if (have) {
				double depth = vlen(vsub(p, cam->eye));
				double px = cam->f / fmax(depth, 0.05);
				/* broad where it faces the eye, a line where it is edge on */
				double face = fabs(cos(ph * 0.5));
				double lw = fmax(0.0085 * px * (0.25 + 0.75 * face), 0.6);
				double shade = 0.55 + 0.45 * (0.5 + 0.5 * sin(ph));
				cairo_move_to(cr, px0, py0);
				cairo_line_to(cr, x, y);
				cairo_set_source_rgba(cr, c[0] * L.x * shade, c[1] * L.y * shade, c[2] * L.z * shade,
					a);
				cairo_set_line_width(cr, lw);
				cairo_stroke(cr);
			}
			px0 = x;
			py0 = y;
			have = true;
			prev = p;
		}
	}
	cairo_restore(cr);
}

/* Confetti, coming down past the door: little squares of foil and paper,
 * turning as they fall, catching the light when they face it. */
static void lkfx_confetti(cairo_t *cr, int w, int h, const struct lk_rig *g, double t, double a)
{
	static const double COL[6][3] = { { 0.96, 0.76, 0.30 }, { 0.85, 0.87, 0.92 },
		{ 0.92, 0.22, 0.30 }, { 0.20, 0.50, 0.95 }, { 0.30, 0.82, 0.42 }, { 0.95, 0.45, 0.75 } };
	vec3 L = lkfx_light(g);
	double k = h / 900.0;
	cairo_save(cr);
	for (int i = 0; i < 56; i++) {
		double h1 = lkfx_hash(i, 51), h2 = lkfx_hash(i, 53), h3 = lkfx_hash(i, 57);
		double speed = 0.035 + 0.035 * h3;
		double y = fmod(h2 + t * speed, 1.0) * (h + 40.0 * k) - 20.0 * k;
		double x = h1 * w + sin(t * (0.6 + 0.8 * h3) + i) * 18.0 * k;
		double spin = t * (2.0 + 3.0 * h1) + h2 * 6.283;
		double face = cos(spin);
		double sz = (5.0 + 4.0 * h3) * k;
		const double *c = COL[i % 6];
		double glint = pow(fabs(face), 8.0) * (i % 6 < 2 ? 0.6 : 0.25);
		cairo_save(cr);
		cairo_translate(cr, x, y);
		cairo_rotate(cr, h2 * 6.283 + t * 0.8 * (h1 - 0.5));
		cairo_scale(cr, 1.0, fmax(fabs(face), 0.12));
		cairo_rectangle(cr, -sz * 0.5, -sz * 0.35, sz, sz * 0.7);
		cairo_set_source_rgba(cr, fmin(1.0, c[0] * L.x + glint), fmin(1.0, c[1] * L.y + glint),
			fmin(1.0, c[2] * L.z + glint), 0.92 * a);
		cairo_fill(cr);
		cairo_restore(cr);
	}
	cairo_restore(cr);
}

void desk_draw_lock_fx(cairo_t *cr, int w, int h, const struct desk_lock_look *lk)
{
	const struct desk_door_mood *m = &lk->mood;
	if (!desk_lock_fx_wanted(m) || !g_lk_view.have || g_lk_view.w != w || g_lk_view.h != h) {
		return;
	}
	double t = lk->now_s;
	/* gone in through the door, nothing out there is to be seen any more */
	double a = 1.0 - smoothstepf(0.20, 0.70, lk->push);
	if (a <= 0.0) {
		return;
	}
	struct lk_rig rig;
	lk_rig_for(m, &rig);
	bool night = rig.porch > 0.5;
	const struct lk_cam *cam = &g_lk_view.cam;
	/* what the door is dressed in, as lkd_dress has it */
	bool fall = (m->decor & DESK_DECOR_HALLOWEEN) && !(m->decor & (DESK_DECOR_XMAS | DESK_DECOR_NEWYEAR));
	if ((m->decor & DESK_DECOR_XMAS) || fall) {
		lkfx_bulbs(cr, cam, m->decor, t, a);
	}
	if (fall) {
		pthread_once(&g_lkd_fall_once, lkd_fall_make);
		lkfx_jack(cr, cam, clampf(lk->open, 0.0, 1.0) * LK_SWING, t, a, rig.porch);
	}
	if (m->decor & DESK_DECOR_BIRTHDAY) {
		lkfx_strings(cr, cam, t, a, night ? 0.75 : 1.0);
	}
	if (m->decor & DESK_DECOR_NEWYEAR) {
		lkfx_streamers(cr, cam, &rig, t, a);
		lkfx_pennants(cr, cam, &rig, t, a);
		lkfx_confetti(cr, w, h, &rig, t, a);
	}
	double heavy = clampf(m->heavy, 0.0, 1.0);
	if (m->weather == DESK_WX_RAIN || m->weather == DESK_WX_STORM) {
		lkfx_rain(cr, w, h, t, m->weather == DESK_WX_STORM ? 1.0 : heavy, a, night);
	} else if (m->weather == DESK_WX_SNOW) {
		lkfx_snow(cr, w, h, t, heavy, a);
	}
	if (m->weather == DESK_WX_STORM) {
		double f = lkfx_flash(t);
		if (f > 0.002) {
			cairo_save(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
			cairo_set_source_rgba(cr, 0.70, 0.78, 1.0, 0.30 * f * a);
			cairo_paint(cr);
			cairo_restore(cr);
		}
	}
}
