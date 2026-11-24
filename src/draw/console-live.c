/* Part of draw.c: the console's live half, the tray's slips and their cards.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- the live half, a sprite at a time ---- */

void desk_console_advance(const struct desk_console_state *st,
	const struct desk_meters *m)
{
	cc_trace_step(m ? m->cpu : 0.0, st ? st->clock : 0.0);
}

uint64_t desk_console_piece_key(int p, const struct desk_console_state *st,
	int hover, const struct desk_meters *m)
{
	uint64_t h = 14695981039346656037ull;
	KEY_ADD(h, p);
	double warm = clampf(st ? st->warm : 1.0, 0.0, 1.0);
	if (p < CC_COUNT) {
		double lit = cc_lit(warm, p);
		KEY_ADD(h, lit);
		KEY_ADD(h, hover == p);
	}
	switch (p) {
	case CC_CRT:
		/* the beam sweeps with the clock: a new face every frame */
		KEY_ADD(h, st ? st->clock : 0.0);
		KEY_ADD(h, st ? st->screens_said : -1);
		KEY_ADD(h, st ? st->screens_word : 0.0);
		KEY_ADD(h, m->cpu);
		KEY_ADD(h, m->mem);
		KEY_ADD(h, m->temp_c);
		KEY_ADD(h, g_cc_trace_at);
		break;
	case CC_BATT:
		KEY_ADD(h, m->have_battery);
		KEY_ADD(h, m->battery);
		break;
	case CC_NET: {
		KEY_ADD(h, m->net_up);
		KEY_ADD(h, m->net_routed);
		/* A needle a quarter turn long moves a pixel for about a two
		 * hundredth of its swing; traffic that moves it less than that is
		 * not worth a frame. */
		int rate = (int)lround(clampf(m->net_rate, 0.0, 1.0)
			* (desk_gfx_level() <= DESK_GFX_LOW ? 48.0 : 256.0));
		KEY_ADD(h, rate);
		h = key_mix(h, m->net_if, strnlen(m->net_if, sizeof m->net_if));
		break;
	}
	case CC_VOL:
		KEY_ADD(h, st ? st->volume : 0.5);
		break;
	case CC_BRIGHT:
		KEY_ADD(h, st ? st->brightness : 1.0);
		break;
	case CC_SCREEN:
		KEY_ADD(h, st ? st->screen : 1.0);
		KEY_ADD(h, st && st->screen_ok);
		break;
	case CC_WIFI:
		KEY_ADD(h, st && st->wifi_on);
		break;
	case CC_BT:
		KEY_ADD(h, st && st->bt_on);
		break;
	case CC_PW_OFF:
	case CC_PW_BOOT:
	case CC_PW_OUT:
	case CC_PW_SUSPEND:
		KEY_ADD(h, st ? st->key : 0.0);
		KEY_ADD(h, st ? st->pressed : -1);
		KEY_ADD(h, st ? st->dying : 0.0);
		KEY_ADD(h, st && st->push_which == p ? st->push : 0.0);
		break;
	case CC_PW_KEY:
		KEY_ADD(h, st ? st->key : 0.0);
		break;
	case CC_LOCK:
		KEY_ADD(h, st ? st->lock_push : 0.0);
		break;
	case CC_SLEEP:
		KEY_ADD(h, st && st->push_which == p ? st->push : 0.0);
		break;
	case CC_AIR:
	case CC_SAVE:
	case CC_NIGHT:
	case CC_DISPLAY:
		KEY_ADD(h, st && st->push_which == p ? st->push : 0.0);
		KEY_ADD(h, st && st->air_on);
		KEY_ADD(h, st && st->save_on);
		KEY_ADD(h, st && st->night_on);
		KEY_ADD(h, st && st->save_ok);
		KEY_ADD(h, st && st->night_ok);
		KEY_ADD(h, st ? st->screens_mode : 0);
		KEY_ADD(h, st && st->screens_ok);
		/* (up, or on its way, it is printed on the lid's own picture) */
		KEY_ADD(h, st && st->guard > 0.0005);
		break;
	case DESK_CCP_LENSES:
		KEY_ADD(h, warm);
		KEY_ADD(h, st ? st->key : 0.0);
		KEY_ADD(h, st ? st->dying : 0.0);
		break;
	case DESK_CCP_GUARD: {
		KEY_ADD(h, st ? st->guard : 0.0);
		KEY_ADD(h, hover);
		KEY_ADD(h, warm);
		static const int WITH[8] = { CC_PW_OFF, CC_PW_BOOT, CC_PW_OUT, CC_PW_SUSPEND,
			CC_AIR, CC_SAVE, CC_NIGHT, CC_DISPLAY };
		for (int k = 0; k < 8 && st && st->guard > 0.0005; k++) {
			KEY_ADD(h, desk_console_piece_key(WITH[k], st, hover, m));
		}
		break;
	}
	case DESK_CCP_ANTENNA:
		KEY_ADD(h, st ? st->antenna : 1.0);
		break;
	default:
		break;
	}
	return h;
}

/* Where the pieces that are not controls are drawn: the nameplate, the row of lenses with the
 * guarded set's lamp, and the aerial all the way up -- each a box round what they can cover,
 * on the screen and no further. */
static void cc_extra_box(struct room_render *rr, double ox, double oy, double scale, int w, int h,
	int p, double box[4])
{
	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	double sw = rr->w * scale, sh = rr->h * scale;
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
	vec3 pts[24];
	double grow[24];
	int n = 0;
	if (p == DESK_CCP_LENSES) {
		double inner = (RM_CC_X1 - RM_CC_X0) * 0.5 - RM_CC_CHEEK;
		for (int i = 0; i < 6; i++) {
			double u = -inner + 0.70 + i * (2.0 * (inner - 0.70) / 5.0);
			pts[n] = cc_world(&panel, u, 0.17, -panel.half + 0.136, 1.0, aspect);
			grow[n++] = h * 0.011 * 2.8 + 2.0;
		}
		pts[n] = cc_world(&panel, CC_PW_LOCK_U, RM_CC_SKIN + 0.20, CC_PW_LAMP_S, 1.0, aspect);
		grow[n++] = h * 0.014 * 2.8 + 2.0;
	} else if (p == DESK_CCP_PLATE) {
		vec3 q[4];
		cc_plate_quad(1.0, aspect, q);
		for (int k = 0; k < 4; k++) {
			pts[n] = q[k];
			grow[n++] = 12.0;
		}
	} else {
		/* the aerial: up from its foot as far as it goes, and as thick as its boss */
		vec3 foot = cc_world(&tower, CC_ANT_U, RM_CC_SKIN + 0.02, CC_ANT_S, 1.0, aspect);
		double top = 0.34 + 5.0 * 1.10 + 0.11;
		for (int k = 0; k <= 8; k++) {
			pts[n] = vadd(foot, v3(0.0, top * k / 8.0, 0.0));
			grow[n++] = -1.0;   /* (its thickness, measured on the screen below) */
		}
	}
	for (int k = 0; k < n; k++) {
		double u, v;
		if (!room_project(&rr->console_cam, pts[k], &u, &v)) {
			continue;
		}
		double x = ox + u * sw, y = oy + v * sh, g = grow[k];
		if (g < 0.0) {
			double u2, v2;
			g = room_project(&rr->console_cam, vadd(pts[k], v3(0.20, 0.0, 0.0)), &u2, &v2)
				? hypot((u2 - u) * sw, (v2 - v) * sh) + 6.0 : 24.0;
		}
		x0 = fmin(x0, x - g);
		y0 = fmin(y0, y - g);
		x1 = fmax(x1, x + g);
		y1 = fmax(y1, y + g);
	}
	x0 = fmax(x0, 0.0);
	y0 = fmax(y0, 0.0);
	x1 = fmin(x1, (double)w);
	y1 = fmin(y1, (double)h);
	box[0] = floor(x0);
	box[1] = floor(y0);
	box[2] = x1 > x0 ? ceil(x1) - box[0] : 0.0;
	box[3] = y1 > y0 ? ceil(y1) - box[1] : 0.0;
}

bool desk_console_piece_box(int w, int h, int p, double box[4])
{
	struct room_render *rr;
	double ox, oy, scale;
	if (p == DESK_CCP_GUARD && console_xform(w, h, &rr, &ox, &oy, &scale)) {
		cc_lid_box_screen(rr, ox, oy, scale, (double)rr->w / (double)(rr->h > 0 ? rr->h : 1),
			box);
		return box[2] > 0.0 && box[3] > 0.0;
	}
	if ((p == DESK_CCP_PLATE || p == DESK_CCP_LENSES || p == DESK_CCP_ANTENNA)
		&& console_xform(w, h, &rr, &ox, &oy, &scale)) {
		/* (2026-11-12) the nameplate, the lenses' glow and the aerial had no box, and so were
		 * each drawn on a whole screen and looked over for where they had been drawn: on a
		 * Surface Pro's 2736 by 1824 at High, 25 to 40 ms each, the first time the set came up */
		cc_extra_box(rr, ox, oy, scale, w, h, p, box);
		return box[2] > 0.0 && box[3] > 0.0;
	}
	if (p >= CC_COUNT || !console_xform(w, h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	/* a face is laid on by an affine fit to three of its corners, so it
	 * covers the parallelogram they make -- which is the box */
	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	vec3 q[4];
	cc_quad(p, 1.0, aspect, q);
	double fw = 0, fh = 0;
	cc_face_size(p, &fw, &fh);
	cairo_matrix_t mt;
	if (!quad_matrix(&rr->console_cam, q, fw, fh, ox, oy, rr->w * scale, rr->h * scale, &mt)) {
		return false;
	}
	double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
	for (int k = 0; k < 4; k++) {
		double x = (k & 1) ? fw : 0.0, y = (k & 2) ? fh : 0.0;
		cairo_matrix_transform_point(&mt, &x, &y);
		x0 = fmin(x0, x);
		y0 = fmin(y0, y);
		x1 = fmax(x1, x);
		y1 = fmax(y1, y);
	}
	/* (pasted onto its decks, it reaches as far as the top of the tallest) */
	double px0, py0, px1, py1;
	if (cc_paste_bounds(&rr->console_cam, aspect, ox, oy, rr->w * scale, rr->h * scale, p,
		&px0, &py0, &px1, &py1)) {
		x0 = fmin(x0, px0 - 1.0);
		y0 = fmin(y0, py0 - 1.0);
		x1 = fmax(x1, px1 + 1.0);
		y1 = fmax(y1, py1 + 1.0);
	}
	if (p == CC_WIFI || p == CC_BT || p == CC_PW_KEY || p == CC_BRIGHT || p == CC_SCREEN) {
		/* what stands up off the face, wherever it is in its travel,
		 * and its shadow */
		struct cc_face panel, tower;
		cc_faces(&panel, &tower);
		double hu = p == CC_PW_KEY ? CC[p].hu + 0.30 : CC[p].hu, hs = CC[p].hs;
		for (int k = 0; k < 8; k++) {
			vec3 c = cc_world(CC[p].tower ? &tower : &panel, CC[p].u + ((k & 1) ? hu : -hu),
				RM_CC_SKIN + ((k & 4) ? 0.75 : 0.0), CC[p].s + ((k & 2) ? hs : -hs), 1.0,
				aspect);
			double u, v;
			if (room_project(&rr->console_cam, c, &u, &v)) {
				double x = ox + u * rr->w * scale, y = oy + v * rr->h * scale;
				x0 = fmin(x0, x);
				y0 = fmin(y0, y);
				x1 = fmax(x1, x);
				y1 = fmax(y1, y);
			}
		}
	}
	/* and a couple of pixels for the filter's reach */
	box[0] = floor(x0) - 2.0;
	box[1] = floor(y0) - 2.0;
	box[2] = ceil(x1) + 2.0 - box[0];
	box[3] = ceil(y1) + 2.0 - box[1];
	return true;
}

void desk_draw_console_piece(cairo_t *cr, int w, int h, int p,
	const struct desk_console_state *st, int hover, const struct desk_meters *m)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!console_xform(w, h, &rr, &ox, &oy, &scale)) {
		desk_scene_warm(w, h, true);
		return;
	}
	struct lamp_px lamp_was = lamp_view(w, h);
	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	double warm = clampf(st ? st->warm : 1.0, 0.0, 1.0);
	console_piece_paint(cr, h, rr, ox, oy, scale, aspect, p, st, hover, m, warm);
	g_light = lamp_was;
}

void desk_draw_console(cairo_t *cr, int screen_w, int screen_h,
	const struct desk_console_state *st, int hover)
{
	console_paint(cr, screen_w, screen_h, st, hover, true);
}

/* The traced sheet on its own: put it up once and leave it there. */
void desk_draw_console_base(cairo_t *cr, int screen_w, int screen_h)
{
	console_paint(cr, screen_w, screen_h, NULL, -1, false);
}

/* The set standing on the desk with nobody at it. Its pilot lamps are still
 * on -- a machine like this is never off, it is only unattended -- so the
 * corner of the desk has something alive in it before you ever look. */
static double room_lamp_at(vec3 p);

/* The nameplate on the ledge. It is screwed to the machine, not floated
 * over the desk, so it goes on the plinth's own front face and takes that
 * face's perspective -- and it is there whether the machine is stowed or
 * out, because it is the only part of the set that says what the set is.
 * It is only as long as what is engraved on it: the ledge stops where it
 * does, and past it the desk is the desk. */
static void cc_nameplate(cairo_t *cr, const struct room_cam *cam,
	double ox, double oy, double sw, double sh, double slide, double aspect,
	double lit)
{
	vec3 q[4];
	cc_plate_quad(slide, aspect, q);
	cairo_matrix_t mt;
	/* the same pixels to the unit it always had, so the legend is the
	 * size it always was */
	const double PH = 74.0;
	const double PW = ceil((q[1].x - q[0].x) * (620.0 / 8.6));
	if (!quad_matrix(cam, q, PW, PH, ox, oy, sw, sh, &mt)) {
		return;
	}
	cairo_surface_t *face = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		(int)PW, (int)PH);
	cairo_t *fc = cairo_create(face);

	/* rolled brass, screwed down at both ends */
	double px = 9.0, pw = PW - px - 9.0;
	double py = PH * 0.16, ph = PH * 0.66;
	cairo_pattern_t *bg = cairo_pattern_create_linear(px, py, px, py + ph);
	cairo_pattern_add_color_stop_rgb(bg, 0.00, 0.742, 0.590, 0.300);
	cairo_pattern_add_color_stop_rgb(bg, 0.42, 0.576, 0.452, 0.228);
	cairo_pattern_add_color_stop_rgb(bg, 0.58, 0.498, 0.392, 0.200);
	cairo_pattern_add_color_stop_rgb(bg, 1.00, 0.648, 0.516, 0.264);
	cairo_set_source(fc, bg);
	desk_rounded_path(fc, px, py, pw, ph, PH * 0.10);
	cairo_fill(fc);
	cairo_pattern_destroy(bg);

	/* forty years of nothing much happening to it */
	for (int i = 0; i < 5; i++) {
		double bx = px + hash21(91.0 + i * 3.7, 2.1) * pw;
		double by = py + hash21(17.0 + i * 5.3, 8.4) * ph;
		double br = PH * (0.35 + hash21(i * 2.9, 1.7) * 0.85);
		cairo_pattern_t *t = cairo_pattern_create_radial(bx, by, 0, bx, by, br);
		cairo_pattern_add_color_stop_rgba(t, 0.0, 0.262, 0.222, 0.128, 0.30);
		cairo_pattern_add_color_stop_rgba(t, 1.0, 0.262, 0.222, 0.128, 0.0);
		cairo_set_source(fc, t);
		cairo_rectangle(fc, px, py, pw, ph);
		cairo_fill(fc);
		cairo_pattern_destroy(t);
	}

	/* engraved between the screws: cut in, so it is dark with a lit lower
	 * lip -- in a narrow cut, the way a plate that has to be short is */
	const char *legend = TR("CONTROL PANEL \xe2\x86\x91");
	const double narrow = 0.80;
	double s0 = px + PH * 0.15, s1 = px + pw - PH * 0.15;
	double lx = (s0 + s1) * 0.5, room = (s1 - s0 - PH * 0.30) / narrow;
	cairo_save(fc);
	cairo_translate(fc, lx, 0.0);
	cairo_scale(fc, narrow, 1.0);
	show_text_fit_as(fc, DESK_FONT_ENGRAVE, legend, 1.0 / narrow, PH * 0.635 + 1.2, PH * 0.34,
		true, 0.92, 0.84, 0.62, 0.34, 0, room);
	show_text_fit_as(fc, DESK_FONT_ENGRAVE, legend, 0.0, PH * 0.635, PH * 0.34, true,
		0.128, 0.094, 0.044, 0.92, 0, room);
	cairo_restore(fc);

	for (int i = 0; i < 2; i++) {
		double sx = i ? s1 : s0;
		double sy = py + ph * 0.5, sr = PH * 0.10;
		cairo_set_source_rgba(fc, 0.20, 0.16, 0.08, 0.55);
		cairo_arc(fc, sx, sy, sr, 0, 2 * M_PI);
		cairo_fill(fc);
		cairo_set_source_rgba(fc, 0.84, 0.72, 0.44, 0.75);
		cairo_arc(fc, sx, sy - sr * 0.22, sr * 0.72, 0, 2 * M_PI);
		cairo_fill(fc);
		cairo_set_source_rgba(fc, 0.16, 0.12, 0.06, 0.80);
		cairo_set_line_width(fc, sr * 0.34);
		cairo_move_to(fc, sx - sr * 0.62, sy - sr * 0.30 * (i ? -1.0 : 1.0));
		cairo_line_to(fc, sx + sr * 0.62, sy + sr * 0.30 * (i ? -1.0 : 1.0));
		cairo_stroke(fc);
	}
	overlay_grain(fc, 0, 0, PW, PH, 0.9);
	cairo_destroy(fc);
	cairo_surface_flush(face);

	cairo_save(cr);
	cairo_transform(cr, &mt);
	cairo_set_source_surface(cr, face, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint_with_alpha(cr, clampf(lit, 0.0, 1.0));
	cairo_restore(cr);
	cairo_surface_destroy(face);
}

static void console_idle(cairo_t *cr, int screen_w, int screen_h, double drawer_open)
{
	struct room_xf xf;
	if (!room_xform(screen_w, screen_h, drawer_open, &xf)) {
		return;
	}
	struct room_render *rr = xf.rr;
	double sw = rr->w * xf.scale, sh = rr->h * xf.scale;
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	(void)tower;   /* stowed, the tower is behind the desk's far edge */

	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	cc_nameplate(cr, &rr->cam, xf.ox, xf.oy, sw, sh, 0.0, aspect,
		clampf(0.58 + 0.42 * room_lamp_at(v3((RM_CC_PLATE_X0 + RM_CC_LEDGE_X1) * 0.5,
			RM_CC_PLINTH, RM_CC_PZ0 + cc_dz(0.0, aspect))), 0.50, 1.0));

	/* ---- and the row of lenses along the front edge of the panel ----
	 * The set is never off. It is only unattended. */
	double inner = (RM_CC_X1 - RM_CC_X0) * 0.5 - RM_CC_CHEEK;
	for (int i = 0; i < 6; i++) {
		double u = -inner + 0.70 + i * (2.0 * (inner - 0.70) / 5.0);
		vec3 p = cc_world(&panel, u, 0.17, -panel.half + 0.136, 0.0, aspect);
		double pu, pv;
		if (!room_project(&rr->cam, p, &pu, &pv)) {
			continue;
		}
		double x = xf.ox + pu * sw, y = xf.oy + pv * sh;
		if (i & 1) {
			cc_lamp(cr, x, y, 6.75 * desk_px_per_pt(screen_w), 0.30, 1.0, 0.40, 0.52);
		} else {
			cc_lamp(cr, x, y, 6.75 * desk_px_per_pt(screen_w), 1.0, 0.60, 0.18, 0.52);
		}
	}
}

void desk_draw_console_idle(cairo_t *cr, int screen_w, int screen_h,
	double drawer_open)
{
	console_idle(cr, screen_w, screen_h, drawer_open);
}

/* ---- the tray: slips tucked under the Control Panel ---------------
 *
 * Past the end of the nameplate the plinth stops under the panel's lip,
 * and the desk runs right up to it. That is where the tray is: for each
 * thing a program has put there, a slip of paper about the size of a
 * sticky note, tucked in between the plinth and the desk with the end the
 * program's icon is printed on showing. They are drawn where the desk is
 * and nowhere the machine is, which is all it takes to put them under it.
 *
 * Pull one out and it comes out stuck to the end of a card, which had
 * been lying under the machine all along with the program's menu printed
 * on it. The card is drawn by the same rules -- nowhere the machine is --
 * so on its way out it comes from under the plinth and the ledge both. */

#define SLIP_W 0.92          /* across, in the room's units */
#define SLIP_OUT 0.90        /* how much of it shows, as it lies */
#define SLIP_UNDER 1.60      /* and how much more is under the plinth */
#define SLIP_ON_CARD 0.36    /* how far it is stuck on to its card */
#define SLIP_GAP 0.13
#define SLIP_ATTENTION 0.32  /* pushed out, to be seen */
#define SLIP_PASSIVE 0.30    /* pushed in: nothing to say just now */
#define SLIP_LIFT 0.22       /* slid out, under the pointer */

static struct desk_slips_state g_slips;

void desk_slips_set(const struct desk_slips_state *st)
{
	for (int i = 0; i < g_slips.n; i++) {
		if (g_slips.slips[i].icon) {
			cairo_surface_destroy(g_slips.slips[i].icon);
		}
	}
	g_slips = *st;
	if (g_slips.n > DESK_SLIPS_MAX) {
		g_slips.n = DESK_SLIPS_MAX;
	}
	for (int i = 0; i < g_slips.n; i++) {
		if (g_slips.slips[i].icon) {
			cairo_surface_reference(g_slips.slips[i].icon);
		}
	}
}

uint64_t desk_slips_key(void)
{
	uint64_t h = 14695981039346656037ull;
#define SLP_MIX(p, n) do { const unsigned char *c_ = (const unsigned char *)(p); \
	for (size_t k_ = 0; k_ < (n); k_++) { h = (h ^ c_[k_]) * 1099511628211ull; } } while (0)
	int top[4] = { g_slips.n, g_slips.hot, g_slips.pulled, (int)lround(g_slips.scroll * 64.0) };
	SLP_MIX(top, sizeof top);
	for (int i = 0; i < g_slips.n; i++) {
		const struct desk_slip *sl = &g_slips.slips[i];
		uintptr_t pi = (uintptr_t)sl->icon;
		SLP_MIX(&pi, sizeof pi);
		SLP_MIX(sl->title, strlen(sl->title));
		int f[4] = { (int)sl->seed, sl->attention, sl->passive, (int)lround(sl->lift * 48.0) };
		SLP_MIX(f, sizeof f);
	}
#undef SLP_MIX
	return h | 1u;
}

/* Where the machine meets the desk, on the screen. */
struct slip_room {
	double s;              /* screen pixels to the room's unit, on the desk */
	double x0, x1;         /* the plinth's two ends */
	double xl;             /* where the ledge stops */
	double yp, yl;         /* where the plinth's front, and the ledge's, meet the desk */
	double lit;            /* how much of the lamp gets in there */
};

static bool slip_room(int w, int h, double open, struct slip_room *r)
{
	struct room_xf xf;
	if (!room_xform(w, h, open, &xf)) {
		return false;
	}
	struct room_render *rr = xf.rr;
	double aspect = (double)rr->w / (double)(rr->h > 0 ? rr->h : 1);
	double dz = cc_dz(0.0, aspect);
	double sw = rr->w * xf.scale, sh = rr->h * xf.scale;
	vec3 pt[4] = {
		v3(RM_CC_X0 + 0.10, 0.0, RM_CC_PLINTH_Z0 + dz),
		v3(RM_CC_X1 - 0.10, 0.0, RM_CC_PLINTH_Z0 + dz),
		v3(RM_CC_LEDGE_X1, 0.0, RM_CC_PZ0 + RM_CC_LEDGE + dz),
		v3(RM_CC_X0 + 1.10, 0.0, RM_CC_PLINTH_Z0 + dz),
	};
	double x[4], y[4];
	for (int k = 0; k < 4; k++) {
		double u, v;
		if (!room_project(&rr->cam, pt[k], &u, &v)) {
			return false;
		}
		x[k] = xf.ox + u * sw;
		y[k] = xf.oy + v * sh;
	}
	r->x0 = x[0];
	r->x1 = x[1];
	r->xl = x[2];
	r->yp = y[0];
	r->yl = y[2];
	r->s = x[3] - x[0];
	/* put in the room's light the way the clock's face is: this corner
	 * is at the edge of the lamp's pool */
	r->lit = clampf(0.42 + 0.58 * room_lamp_at(v3((RM_CC_LEDGE_X1 + RM_CC_X1) * 0.5, 0.0,
		RM_CC_PLINTH_Z0 + dz + SLIP_OUT * 0.5)), 0.38, 1.0);
	return r->s > 1.0;
}

/* Everything the machine stands on, left out: what is drawn after this
 * shows only where the desk is. */
static void slip_clip(cairo_t *cr, const struct slip_room *r)
{
	const double far = 1e5;
	cairo_new_path(cr);
	cairo_rectangle(cr, -far, -far, r->x0 + far, 2.0 * far);
	cairo_rectangle(cr, r->x1, -far, far, 2.0 * far);
	cairo_rectangle(cr, r->x0, r->yl, r->xl - r->x0, far);
	cairo_rectangle(cr, r->xl, r->yp, r->x1 - r->xl, far);
	cairo_clip(cr);
}

/* How one slip lies: where its end is across, how much of it shows, and
 * how far off square it was pushed in. */
struct slip_lay {
	double cx, out, rot;
	double shown;   /* how much of it is in the row: 0 slid along out of it */
};

/* How many whole slips go side by side between the plate and the end. */
static int slip_fit(const struct slip_room *r)
{
	double w = SLIP_W * r->s, gap = SLIP_GAP * r->s;
	double left = r->xl + gap, right = r->x1 - gap * 0.5;
	return (int)fmax(floor((right - left + gap) / (w + gap)), 1.0);
}

int desk_slips_fit(int w, int h)
{
	struct slip_room r;
	return slip_room(w, h, 0.0, &r) ? slip_fit(&r) : 0;
}

static void slip_layout(const struct slip_room *r, int i, struct slip_lay *L)
{
	double w = SLIP_W * r->s, gap = SLIP_GAP * r->s;
	double left = r->xl + gap;
	/* side by side; when there are more than there is room for, the row
	 * is slid along (slips_paint) */
	double step = w + gap;
	double at = i - (g_slips.n > slip_fit(r) ? g_slips.scroll : 0.0);
	const struct desk_slip *sl = &g_slips.slips[i];
	double sd = sl->seed * 0.0131;
	L->cx = left + w * 0.5 + at * step + (hash21(sd, 7.3) - 0.5) * 0.06 * r->s;
	double end = left + slip_fit(r) * step - gap;
	L->shown = clampf((end + gap * 0.5 - (L->cx - w * 0.5)) / w, 0.0, 1.0)
		* clampf((L->cx + w * 0.5 - (left - gap * 0.5)) / w, 0.0, 1.0);
	L->rot = (hash21(sd, 3.1) - 0.5) * 0.09;
	double out = SLIP_OUT + (hash21(sd, 5.7) - 0.5) * 0.10
		+ (sl->attention ? SLIP_ATTENTION : 0.0) - (sl->passive ? SLIP_PASSIVE : 0.0)
		+ SLIP_LIFT * clampf(sl->lift, 0.0, 1.0);
	L->out = out * r->s;
}

/* Whether an icon is a picture or only a shape. A tray icon is as often
 * as not drawn in white for a dark panel, and white does not print: that
 * kind goes on as the shape it is, in ink. Kept, with a hold on the icon
 * so the answer is never about some other one at the same address. */
struct icon_ink {
	cairo_surface_t *src;
	bool shape;
	unsigned used;
};
static struct icon_ink g_ink[32];
static unsigned g_ink_clock;

static bool icon_is_shape(cairo_surface_t *src)
{
	struct icon_ink *slot = NULL;
	for (size_t i = 0; i < sizeof g_ink / sizeof *g_ink; i++) {
		struct icon_ink *k = &g_ink[i];
		if (k->src == src) {
			k->used = ++g_ink_clock;
			return k->shape;
		}
		if (!slot || !k->src || (slot->src && k->used < slot->used)) {
			if (!slot || slot->src) {
				slot = k;
			}
		}
	}
	bool shape = false;
	if (cairo_image_surface_get_format(src) == CAIRO_FORMAT_ARGB32) {
		cairo_surface_flush(src);
		const unsigned char *px = cairo_image_surface_get_data(src);
		int w = cairo_image_surface_get_width(src), h = cairo_image_surface_get_height(src);
		int stride = cairo_image_surface_get_stride(src);
		double sat = 0.0, wsum = 0.0;
		for (int y = 0; px && y < h; y++) {
			const uint32_t *row = (const uint32_t *)(px + (size_t)y * stride);
			for (int x = 0; x < w; x++) {
				double a = (row[x] >> 24) / 255.0;
				if (a < 0.15) {
					continue;
				}
				double r = ((row[x] >> 16) & 255) / 255.0 / a;
				double g = ((row[x] >> 8) & 255) / 255.0 / a;
				double b = (row[x] & 255) / 255.0 / a;
				sat += a * (fmax(r, fmax(g, b)) - fmin(r, fmin(g, b)));
				wsum += a;
			}
		}
		shape = wsum > 0.0 && sat / wsum < 0.14;
	}
	if (slot) {
		if (slot->src) {
			cairo_surface_destroy(slot->src);
		}
		slot->src = cairo_surface_reference(src);
		slot->shape = shape;
		slot->used = ++g_ink_clock;
	}
	return shape;
}

/* The program's icon printed on paper, size across, centred at x, y. */
static void print_icon(cairo_t *cr, cairo_surface_t *icon, const char *title, double x, double y,
	double size)
{
	if (!icon) {
		char one[8] = { 0 };
		desk_initial_of(title, one, sizeof one);
		show_text_as(cr, DESK_FONT_PRINT, one, x, y + size * 0.36, size * 0.92, true, 0.16,
			0.14, 0.12, 0.86, 0);
		return;
	}
	int iw = cairo_image_surface_get_width(icon), ih = cairo_image_surface_get_height(icon);
	if (iw < 1 || ih < 1) {
		return;
	}
	double sc = size / fmax(iw, ih);
	cairo_save(cr);
	cairo_translate(cr, x - iw * sc * 0.5, y - ih * sc * 0.5);
	cairo_scale(cr, sc, sc);
	if (icon_is_shape(icon)) {
		cairo_set_source_rgba(cr, 0.16, 0.14, 0.12, 0.88);
		cairo_mask_surface(cr, icon, 0, 0);
	} else {
		cairo_set_source_surface(cr, icon, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint_with_alpha(cr, 0.94);
	}
	cairo_restore(cr);
}

/* The paper a slip is cut from: a little different each time. */
static void slip_tone(unsigned seed, double rgb[3])
{
	static const double TONE[4][3] = {
		{ 0.958, 0.944, 0.900 }, { 0.968, 0.948, 0.868 },
		{ 0.944, 0.946, 0.928 }, { 0.962, 0.934, 0.906 },
	};
	const double *t = TONE[seed % 4];
	for (int k = 0; k < 3; k++) {
		rgb[k] = t[k];
	}
}

/* One slip, its end showing from ay down; `under` of it goes back up under
 * whatever it is under -- the plinth, or the card it is stuck on -- and
 * `lit` is how much of the lamp it is in. The caller has left out what
 * hides it. */
static void slip_paint(cairo_t *cr, const struct slip_room *r, const struct desk_slip *sl,
	const struct slip_lay *L, double ay, double under, double lit)
{
	double s = r->s, w = SLIP_W * s, out = L->out;
	cairo_matrix_t screen;
	cairo_get_matrix(cr, &screen);
	cairo_save(cr);
	cairo_translate(cr, L->cx, ay);
	cairo_rotate(cr, L->rot);

	/* what it throws on the desk: not much, lying on it -- but the free
	 * end never lies quite flat */
	for (int k = 3; k >= 1; k--) {
		double g = k * 0.035 * s;
		cairo_set_source_rgba(cr, 0.05, 0.03, 0.02, 0.075);
		desk_rounded_path(cr, -w * 0.5 - g + 0.03 * s, -under, w + 2.0 * g,
			under + out + g + 0.05 * s, 0.04 * s + g);
		cairo_fill(cr);
	}

	double tone[3];
	slip_tone(sl->seed, tone);
	cairo_pattern_t *pg = cairo_pattern_create_linear(0, -under, 0, out);
	cairo_pattern_add_color_stop_rgb(pg, 0.0, tone[0] * 0.95, tone[1] * 0.95, tone[2] * 0.95);
	cairo_pattern_add_color_stop_rgb(pg, 0.8, tone[0], tone[1], tone[2]);
	cairo_pattern_add_color_stop_rgb(pg, 1.0, fmin(tone[0] * 1.02, 1.0),
		fmin(tone[1] * 1.02, 1.0), fmin(tone[2] * 1.02, 1.0));
	desk_rounded_path(cr, -w * 0.5, -under, w, under + out, 0.035 * s);
	cairo_set_source(cr, pg);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(pg);
	cairo_save(cr);
	cairo_clip(cr);
	overlay_grain(cr, -w * 0.5, -under, w, under + out, 0.9);

	/* the icon, printed on the end that shows */
	print_icon(cr, sl->icon, sl->title, 0.0, out - w * 0.50, w * 0.64);
	/* asking to be looked at: somebody has put a red mark on it */
	if (sl->attention) {
		cairo_new_path(cr);
		cairo_set_source_rgba(cr, 0.78, 0.12, 0.09, 0.92);
		cairo_arc(cr, w * 0.30, out - w * 0.84, w * 0.085, 0, 2.0 * M_PI);
		cairo_fill(cr);
	}
	/* the cut end catches the lamp */
	cairo_set_line_width(cr, fmax(0.018 * s, 0.8));
	cairo_set_source_rgba(cr, 1.0, 0.99, 0.95, 0.55);
	cairo_move_to(cr, -w * 0.5 + 0.05 * s, out - 0.012 * s);
	cairo_line_to(cr, w * 0.5 - 0.05 * s, out - 0.012 * s);
	cairo_stroke(cr);
	cairo_restore(cr);

	/* the room's light on it, and the machine's shadow across it where it
	 * goes in -- both in the screen's frame, not the slip's */
	desk_rounded_path(cr, -w * 0.5, -under, w, under + out, 0.035 * s);
	cairo_set_matrix(cr, &screen);
	cairo_set_source_rgba(cr, 0.06, 0.04, 0.02, (1.0 - lit) * 0.92);
	cairo_fill_preserve(cr);
	cairo_pattern_t *sh = cairo_pattern_create_linear(0, r->yp, 0, r->yp + 0.30 * s);
	cairo_pattern_add_color_stop_rgba(sh, 0.0, 0.05, 0.035, 0.02, 0.46);
	cairo_pattern_add_color_stop_rgba(sh, 1.0, 0.05, 0.035, 0.02, 0.0);
	cairo_set_source(cr, sh);
	cairo_fill(cr);
	cairo_pattern_destroy(sh);
	cairo_restore(cr);
}

/* The slips as they lie, the one under the pointer on top of the rest. */
static void slips_paint(cairo_t *cr, int w, int h)
{
	struct slip_room r;
	if (!g_slips.n || !slip_room(w, h, 0.0, &r)) {
		return;
	}
	cairo_save(cr);
	slip_clip(cr, &r);
	/* More than there is room for: the row is slid along, those before go
	 * in under the plate's ledge, and the next one after is only the edge
	 * of it, fading into the shadow at the machine's end -- there is more,
	 * and the wheel slides it in. */
	int fit = slip_fit(&r);
	bool over = g_slips.n > fit;
	double fade0 = r.xl + SLIP_GAP * r.s + fit * (SLIP_W + SLIP_GAP) * r.s - SLIP_GAP * r.s;
	if (over) {
		cairo_rectangle(cr, r.xl, -1e5, r.x1 - r.xl, 2e5);
		cairo_clip(cr);
		cairo_push_group(cr);
	}
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < g_slips.n; i++) {
			if (i == g_slips.pulled || (i == g_slips.hot) != (pass == 1)) {
				continue;
			}
			struct slip_lay L;
			slip_layout(&r, i, &L);
			if (over && (L.cx + SLIP_W * r.s < r.xl || L.cx - SLIP_W * r.s > r.x1)) {
				continue;
			}
			slip_paint(cr, &r, &g_slips.slips[i], &L, r.yp, SLIP_UNDER * r.s, r.lit);
		}
	}
	if (over) {
		cairo_pop_group_to_source(cr);
		cairo_pattern_t *m = cairo_pattern_create_linear(fade0, 0.0, r.x1, 0.0);
		cairo_pattern_add_color_stop_rgba(m, 0.0, 0, 0, 0, 1.0);
		cairo_pattern_add_color_stop_rgba(m, 0.35, 0, 0, 0, 0.55);
		cairo_pattern_add_color_stop_rgba(m, 1.0, 0, 0, 0, 0.0);
		cairo_mask(cr, m);
		cairo_pattern_destroy(m);
	}
	cairo_restore(cr);
}

/* What the slips can cover, lying as far out as they ever do. */
static bool slips_box(int w, int h, double box[4])
{
	struct slip_room r;
	if (!slip_room(w, h, 0.0, &r)) {
		return false;
	}
	double pad = 0.30 * r.s;
	box[0] = floor(r.xl - pad);
	box[1] = floor(r.yp - pad);
	box[2] = ceil(r.x1 + pad) - box[0];
	box[3] = ceil(r.yp + (SLIP_OUT + 0.05 + SLIP_ATTENTION + SLIP_LIFT) * r.s + pad) - box[1];
	return true;
}

int desk_slip_at(int w, int h, double px, double py, double drawer_open)
{
	struct slip_room r;
	if (!g_slips.n || py < 0.0 || !slip_room(w, h, drawer_open, &r) || py < r.yp) {
		return -1;
	}
	/* on top first: the one under the pointer, then right to left */
	for (int k = -1; k < g_slips.n; k++) {
		int i = k < 0 ? g_slips.hot : g_slips.n - 1 - k;
		if (i < 0 || i >= g_slips.n || (k >= 0 && i == g_slips.hot) || i == g_slips.pulled) {
			continue;
		}
		struct slip_lay L;
		slip_layout(&r, i, &L);
		if (L.shown < 0.5) {
			continue;
		}
		double dx = px - L.cx, dy = py - r.yp;
		double c = cos(L.rot), s = sin(L.rot);
		double lx = dx * c + dy * s, ly = -dx * s + dy * c;
		if (fabs(lx) <= SLIP_W * r.s * 0.5 + 1.0 && ly <= L.out + 1.0) {
			return i;
		}
	}
	return -1;
}

/* ---- the card on the end of a slip ---- */

struct card_ink {
	double k;                /* how big this screen draws it */
	double pad, tick, icon, arrow;
};

static void card_ink(const struct slip_room *r, const struct desk_card *c, struct card_ink *ink)
{
	ink->k = r->s / 45.0;
	ink->pad = 11.0 * ink->k;
	ink->tick = 0.0;
	ink->icon = 0.0;
	ink->arrow = 0.0;
	for (int i = 0; i < c->n; i++) {
		if (c->e[i].toggle) {
			ink->tick = 17.0 * ink->k;
		}
		if (c->e[i].icon) {
			ink->icon = 21.0 * ink->k;
		}
		if (c->e[i].submenu) {
			ink->arrow = 16.0 * ink->k;
		}
	}
}

static double text_width(const char *s, double size, bool bold)
{
	char clean[1024];
	s = utf8_clean(s, clean, sizeof clean);
	static cairo_surface_t *scratch;
	static cairo_t *mcr;
	if (!mcr) {
		scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4);
		mcr = cairo_create(scratch);
	}
	desk_font_select(mcr, DESK_FONT_UI, s, bold, false);
	cairo_set_font_size(mcr, size);
	cairo_text_extents_t ext;
	cairo_text_extents(mcr, s, &ext);
	return ext.x_advance;
}

bool desk_card_place(int w, int h, double drawer_open, const struct desk_card *c,
	struct desk_card_place *pl)
{
	struct slip_room r;
	if (c->slip < 0 || c->slip >= g_slips.n || !slip_room(w, h, drawer_open, &r)) {
		return false;
	}
	struct card_ink ink;
	card_ink(&r, c, &ink);
	double k = ink.k;
	pl->head_h = 31.0 * k;
	pl->row_h = 23.0 * k;
	double widest = text_width(c->title, 12.5 * k, true) + 26.0 * k
		+ (c->back ? 12.0 * k : 0.0);
	for (int i = 0; i < c->n; i++) {
		double tw = ink.tick + ink.icon + text_width(c->e[i].label, 12.5 * k, false) + ink.arrow;
		widest = fmax(widest, tw);
	}
	double gap = 0.10 * r.s;
	pl->w = clampf(ceil(widest + ink.pad * 2.0), 172.0 * k, r.x1 - r.x0 - 2.0 * gap);
	struct slip_lay L;
	slip_layout(&r, c->slip, &L);
	pl->x = clampf(L.cx - pl->w * 0.5, r.x0 + gap, r.x1 - gap - pl->w);
	/* all the way out, it is clear of everything it was under */
	pl->y = (pl->x < r.xl ? r.yl : r.yp) + 6.0 * k;
	int want = c->n > 0 ? c->n : 1;
	int room = (int)floor((h - 12.0 * k - pl->y - pl->head_h - ink.pad) / pl->row_h);
	pl->rows = want < room ? want : room;
	if (pl->rows < 1) {
		return false;
	}
	pl->h = pl->head_h + pl->rows * pl->row_h + ink.pad * 0.8;
	/* under the machine its end lies just inside the plinth's front, where
	 * the slip stuck on it comes out */
	pl->travel = pl->y - (r.yp - 2.0 - pl->h);
	pl->tail = (SLIP_OUT + 0.05 + SLIP_ATTENTION + SLIP_LIFT) * r.s;
	return true;
}

void desk_card_box(const struct desk_card_place *pl, double pull, double box[4])
{
	double top = pl->y - (1.0 - clampf(pull, -0.05, 1.05)) * pl->travel;
	double m = pl->row_h * 0.9;
	box[0] = floor(pl->x - m);
	box[1] = floor(top - m);
	box[2] = ceil(pl->w + 2.0 * m);
	/* and the slip on its end, below it */
	box[3] = ceil(pl->h + 2.0 * m + pl->tail);
}

/* A pencil line under something, the way the corner's instructions get one. */
static void card_pencil(cairo_t *cr, double x0, double x1, double y, unsigned seed, double k)
{
	wobble_line(cr, x0, y, x1, y, seed, 5, 0.9 * k, 1.3 * k, 0.30, 0.29, 0.30, 0.62);
}

static void card_tick(cairo_t *cr, const struct desk_card_entry *e, double x, double y,
	double k, double ink)
{
	double b = 10.0 * k;
	cairo_save(cr);
	/* text leaves the pen where it stopped; this is not joined on to it */
	cairo_new_path(cr);
	cairo_set_line_width(cr, 1.1 * k);
	cairo_set_source_rgba(cr, 0.16, 0.14, 0.12, 0.70 * ink);
	if (e->toggle == 2) {
		cairo_arc(cr, x + b * 0.5, y, b * 0.5, 0, 2.0 * M_PI);
		cairo_stroke(cr);
		if (e->state == 1) {
			cairo_arc(cr, x + b * 0.5, y, b * 0.24, 0, 2.0 * M_PI);
			cairo_fill(cr);
		}
	} else {
		cairo_rectangle(cr, x, y - b * 0.5, b, b);
		cairo_stroke(cr);
		if (e->state == 1) {
			/* ticked in pencil, and past the edge of the box, as a tick is */
			cairo_set_source_rgba(cr, 0.22, 0.21, 0.23, 0.90 * ink);
			cairo_set_line_width(cr, 1.7 * k);
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
			cairo_move_to(cr, x + b * 0.12, y - b * 0.02);
			cairo_line_to(cr, x + b * 0.42, y + b * 0.34);
			cairo_line_to(cr, x + b * 1.18, y - b * 0.78);
			cairo_stroke(cr);
		}
	}
	cairo_restore(cr);
}

void desk_draw_card(cairo_t *cr, int w, int h, double drawer_open,
	const struct desk_card *c, const struct desk_card_place *pl, double pull)
{
	struct slip_room r;
	if (c->slip < 0 || c->slip >= g_slips.n || !slip_room(w, h, drawer_open, &r)) {
		return;
	}
	struct card_ink ink;
	card_ink(&r, c, &ink);
	double k = ink.k;
	pull = clampf(pull, -0.05, 1.05);
	double x = pl->x, y = pl->y - (1.0 - pull) * pl->travel, cw = pl->w, ch = pl->h;
	const struct desk_slip *sl = &g_slips.slips[c->slip];
	unsigned seed = sl->seed * 7u + 3u;

	cairo_save(cr);
	slip_clip(cr, &r);

	/* what it throws on the desk, and on anything lying on the desk */
	for (int i = 4; i >= 1; i--) {
		double g = i * 2.2 * k;
		cairo_set_source_rgba(cr, 0.05, 0.03, 0.02, 0.055);
		desk_rounded_path(cr, x - g + 2.0 * k, y - g + 3.5 * k, cw + 2.0 * g, ch + 2.0 * g,
			2.5 * k + g);
		cairo_fill(cr);
	}

	/* the card: heavier than the slip, and it has been under there a while */
	cairo_pattern_t *pg = cairo_pattern_create_linear(x, y, x, y + ch);
	cairo_pattern_add_color_stop_rgb(pg, 0.0, 0.972, 0.962, 0.930);
	cairo_pattern_add_color_stop_rgb(pg, 1.0, 0.944, 0.930, 0.890);
	desk_rounded_path(cr, x, y, cw, ch, 2.5 * k);
	cairo_set_source(cr, pg);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(pg);
	cairo_save(cr);
	cairo_clip(cr);
	cairo_translate(cr, x, y);
	paper_age_coarse(cr, cw, ch, seed, 0.20);
	cairo_restore(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.30, 0.25, 0.18, 0.35);
	desk_rounded_path(cr, x + 0.5, y + 0.5, cw - 1.0, ch - 1.0, 2.5 * k);
	cairo_stroke(cr);

	/* its head: the program, or the way back up out of a submenu */
	double hy = y + pl->head_h * 0.5;
	double tx = x + ink.pad;
	if (c->back) {
		show_text(cr, "\xe2\x80\xb9", tx + 3.0 * k, hy + 5.0 * k, 17.0 * k, true,
			0.16, 0.14, 0.12, 0.85, 0);
		tx += 13.0 * k;
	} else {
		print_icon(cr, sl->icon, sl->title, tx + 8.0 * k, hy, 16.0 * k);
		tx += 23.0 * k;
	}
	show_text_fit(cr, c->title, tx, hy + 4.5 * k, 12.5 * k, true, 0.14, 0.12, 0.10, 0.94, -1,
		x + cw - ink.pad - tx);
	if (c->hot == -2) {
		card_pencil(cr, tx, fmin(tx + text_width(c->title, 12.5 * k, true), x + cw - ink.pad),
			hy + 8.5 * k, seed, k);
	}
	/* ruled off, as a card is */
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.62, 0.20, 0.16, 0.42);
	cairo_move_to(cr, x + ink.pad * 0.6, y + pl->head_h - 3.0 * k + 0.5);
	cairo_line_to(cr, x + cw - ink.pad * 0.6, y + pl->head_h - 3.0 * k + 0.5);
	cairo_stroke(cr);

	/* what is on it */
	double ry = y + pl->head_h;
	if (c->loading || c->n == 0) {
		show_text(cr, c->loading ? "\xe2\x80\xa6" : TR("Nothing in it"), x + cw * 0.5,
			ry + pl->row_h * 0.5 + 4.0 * k, 12.5 * k, false, 0.40, 0.37, 0.34, 0.85, 0);
	}
	for (int j = 0; j < pl->rows && c->top + j < c->n; j++) {
		const struct desk_card_entry *e = &c->e[c->top + j];
		double my = ry + j * pl->row_h + pl->row_h * 0.5;
		cairo_new_path(cr);
		if (e->separator) {
			/* a line of perforations */
			cairo_set_source_rgba(cr, 0.30, 0.25, 0.18, 0.36);
			for (double px = x + ink.pad; px < x + cw - ink.pad; px += 4.0 * k) {
				cairo_arc(cr, px, my, 0.7 * k, 0, 2.0 * M_PI);
				cairo_fill(cr);
			}
			continue;
		}
		double a = e->enabled ? 1.0 : 0.38;
		bool hot = c->hot == c->top + j && e->enabled;
		if (hot) {
			cairo_set_source_rgba(cr, 0.30, 0.29, 0.30, 0.07);
			cairo_rectangle(cr, x + 3.0 * k, my - pl->row_h * 0.46, cw - 6.0 * k, pl->row_h * 0.92);
			cairo_fill(cr);
		}
		double ex = x + ink.pad;
		if (e->toggle) {
			card_tick(cr, e, ex, my, k, a);
		}
		ex += ink.tick;
		if (e->icon) {
			cairo_save(cr);
			cairo_push_group(cr);
			print_icon(cr, e->icon, e->label, ex + 8.0 * k, my, 16.0 * k);
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr, a);
			cairo_restore(cr);
		}
		ex += ink.icon;
		double room = x + cw - ink.pad - ink.arrow - ex;
		show_text_fit(cr, e->label, ex, my + 4.4 * k, 12.5 * k, false,
			0.14, 0.12, 0.10, 0.92 * a, -1, room);
		if (e->submenu) {
			show_text(cr, "\xe2\x80\xba", x + cw - ink.pad - 3.0 * k, my + 5.5 * k, 17.0 * k,
				true, 0.16, 0.14, 0.12, 0.80 * a, 1);
		}
		if (hot) {
			card_pencil(cr, ex, ex + fmin(text_width(e->label, 12.5 * k, false), room),
				my + 7.5 * k, seed + (unsigned)j, k);
		}
	}
	/* more of it than there is room for: an arrow at the end that has more */
	if (c->top > 0) {
		show_text(cr, "\xe2\x96\xb4", x + cw - ink.pad, ry + 8.0 * k, 11.0 * k, false,
			0.30, 0.26, 0.22, 0.70, 1);
	}
	if (c->top + pl->rows < c->n) {
		show_text(cr, "\xe2\x96\xbe", x + cw - ink.pad, y + ch - 4.0 * k, 11.0 * k, false,
			0.30, 0.26, 0.22, 0.70, 1);
	}

	/* the slip it came out on, stuck to its end: in the corner's light
	 * while it is still in the corner, and out of it as it comes out to
	 * be read */
	struct slip_lay L;
	slip_layout(&r, c->slip, &L);
	slip_paint(cr, &r, sl, &L, y + ch + 2.0, SLIP_ON_CARD * r.s,
		r.lit + (1.0 - r.lit) * clampf(pull, 0.0, 1.0));

	/* and while it is still close to the machine, the machine's shadow */
	desk_rounded_path(cr, x, y, cw, ch, 2.5 * k);
	cairo_pattern_t *mg = cairo_pattern_create_linear(0, r.yp, 0, r.yp + 0.30 * r.s);
	cairo_pattern_add_color_stop_rgba(mg, 0.0, 0.05, 0.035, 0.02, 0.40);
	cairo_pattern_add_color_stop_rgba(mg, 1.0, 0.05, 0.035, 0.02, 0.0);
	cairo_set_source(cr, mg);
	cairo_fill(cr);
	cairo_pattern_destroy(mg);
	cairo_restore(cr);
}

int desk_card_hit(const struct desk_card *c, const struct desk_card_place *pl,
	double px, double py)
{
	if (px < pl->x || px > pl->x + pl->w || py < pl->y || py > pl->y + pl->h) {
		return -3;
	}
	if (py < pl->y + pl->head_h) {
		return -2;
	}
	int j = (int)floor((py - pl->y - pl->head_h) / pl->row_h);
	int i = c->top + j;
	if (j < 0 || j >= pl->rows || i >= c->n || c->e[i].separator || !c->e[i].enabled) {
		return -1;
	}
	return i;
}
