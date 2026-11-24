/* Part of draw.c: rendering the room: the tracer, its threads and the GPU's trial.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ================================================================
 * Rendering the room.
 *
 * One full frame, traced once and cached: the desk does not move and nor
 * does the lamp. What changes at runtime -- windows, the clock's hands, the
 * drawer sliding -- is composited on top of it.
 *
 * It is baked twice. Once with the drawer taken out of the carcass, and
 * once with nothing but the drawer, fully open, on transparent ground.
 * Sliding the second sheet over the first is what lets a trace that takes
 * most of a minute open a drawer at sixty frames a second.
 * ================================================================ */

/* One knob for overall brightness. ACES desaturates hard once values run
 * past about 1.2, so the rig is deliberately exposed below that. */
#define ICON_EXPOSURE 0.62

/* How much the desk draws (desk_set_gfx): the room's trace below, and
 * whether anything throws a shadow. Set from the compositor's thread; the
 * tracer's worker takes the room's part of it once, as a bake starts. */
static _Atomic int g_gfx = DESK_GFX_HIGH;

void desk_set_gfx(enum desk_gfx g)
{
	atomic_store(&g_gfx, g < DESK_GFX_2D ? DESK_GFX_2D : g > DESK_GFX_ULTRA ? DESK_GFX_ULTRA : g);
}

enum desk_gfx desk_gfx_level(void)
{
	return (enum desk_gfx)atomic_load_explicit(&g_gfx, memory_order_relaxed);
}

/* shadows, of windows and of everything drawn: not in 2D */
static bool gfx_shadows(void)
{
	return desk_gfx_level() != DESK_GFX_2D;
}

/* How the room is traced at each level: the widest it is traced (0: as wide
 * as the machine allows, room_cap; -1: wider again, up to 2560, where a GPU
 * traces it), samples a side of each pixel (Ultra's three only up to 1600
 * wide: room_build_now), the films of the moves at 1/n of its size (0: none
 * -- the view is cut to), and the glow round what is brightest. The letter
 * is the level's, in the cache's file names. Last, how many of the samples
 * of a pixel at an edge are traced where the room is laid from faces and
 * no face covers them (0: all; rf_sparse). */
struct room_q {
	int cap, ss, film_div;
	bool glow;
	char tag;
	int edge_traced;
};
static const struct room_q ROOM_Q[DESK_GFX_COUNT] = {
	/* 2D: a sample a pixel, softened (room_bake) -- a quarter of the work of
	 * four, which on a machine drawing on its CPU is the first start in a
	 * quarter of the time, and looks the same stretched to the screen. The
	 * room itself as wide as the screen, though, and laid from its faces
	 * not softened (room_level_cap) */
	[DESK_GFX_2D] = { 640, 1, 0, false, 'f', 0 },
	/* the room as wide as the screen at these two as well, where it is
	 * laid from faces; these widths are where it is traced. Low traces
	 * two of an edge's four, which on a machine of 2010 has the lamp up
	 * about when it was at 960 */
	[DESK_GFX_LOW] = { 960, 2, 6, true, 'l', 2 },
	[DESK_GFX_MEDIUM] = { 1280, 2, 4, true, 'm', 0 },
	[DESK_GFX_HIGH] = { 0, 2, 4, true, 'h', 0 },
	[DESK_GFX_ULTRA] = { -1, 3, 2, true, 'u', 0 },
};
/* the level of the bake under way, from its start to its end (bakes are
 * one at a time, under g_render_lock) */
static struct room_q g_bq = { 0, 2, 4, true, 'h', 0 };
#define ICON_SS (g_bq.ss)
#define ROOM_TILT_DIV (g_bq.film_div)
/* How wide a room is traced. The trace costs in proportion to its area, and
 * the CPU tracer takes a minute and a half at 1440; a screen wider than
 * that has been shown a room magnified to fit, which on a 1x display is a
 * desk visibly softer than the plates and lamps drawn on it at the
 * screen's own resolution. A GPU that is a GPU (not a rasterizer running
 * on the CPU) traces the same room in seconds, so on one the room follows
 * the screen up to ROOM_WIDE_W. Which machine this is gets remembered, in
 * the cache directory, by the first bake a GPU does; DESK_ROOM_W=<px>
 * sets the width outright, whatever the machine. */
#define ROOM_CANON_W 1440
#define ROOM_WIDE_W 1920

/* How many distinct owners a baked sheet can label. The resolve votes on the
 * supersamples covering a pixel, and it indexed a five-entry array with
 * whatever the id buffer held -- which was fine only for as long as nothing
 * but the five drawer objects ever claimed one. */
#define ROOM_ID_MAX 24

/* Rotated grid: four samples that never share an x or a y, which resolves
 * near horizontal and near vertical silhouettes far better than a 2x2 box. */
static const double SS_OFF[4][2] = {
	{ 0.125, 0.375 }, { 0.625, 0.125 }, { 0.375, 0.875 }, { 0.875, 0.625 },
};

/* Filmic curve. Reinhard plus gamma washes the midtones out and is a large
 * part of why the old objects looked like flat vector art. */
static double aces_ch(double x)
{
	const double a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
	return clampf((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

static vec3 tonemap(vec3 c)
{
	c = v3(aces_ch(fmax(c.x, 0.0)), aces_ch(fmax(c.y, 0.0)), aces_ch(fmax(c.z, 0.0)));
	return v3(pow(c.x, 1.0 / 2.2), pow(c.y, 1.0 / 2.2), pow(c.z, 1.0 / 2.2));
}

/* Five tap binomial, run a few times at widening strides. Cheap, and at
 * these sizes indistinguishable from a real gaussian. */
static void blur_pass_rect(float *buf, float *tmp, int w, int h, int stride)
{
	const double k5[5] = { 0.0625, 0.25, 0.375, 0.25, 0.0625 };
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			double a = 0, b = 0, c = 0;
			for (int k = -2; k <= 2; k++) {
				int sx = x + k * stride;
				sx = sx < 0 ? 0 : (sx >= w ? w - 1 : sx);
				const float *s = buf + ((size_t)y * w + sx) * 3;
				a += s[0] * k5[k + 2];
				b += s[1] * k5[k + 2];
				c += s[2] * k5[k + 2];
			}
			float *d = tmp + ((size_t)y * w + x) * 3;
			d[0] = (float)a;
			d[1] = (float)b;
			d[2] = (float)c;
		}
	}
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			double a = 0, b = 0, c = 0;
			for (int k = -2; k <= 2; k++) {
				int sy = y + k * stride;
				sy = sy < 0 ? 0 : (sy >= h ? h - 1 : sy);
				const float *s = tmp + ((size_t)sy * w + x) * 3;
				a += s[0] * k5[k + 2];
				b += s[1] * k5[k + 2];
				c += s[2] * k5[k + 2];
			}
			float *d = buf + ((size_t)y * w + x) * 3;
			d[0] = (float)a;
			d[1] = (float)b;
			d[2] = (float)c;
		}
	}
}

/* ---------------------------------------------------------------- */

/* The cache lives on disk because a cold trace takes most of a minute and
 * a compositor has to be on screen in two seconds. It is keyed on what the
 * room looks like: its shader (DESK_ROOM_GLSL_KEY, a hash of room.glsl made
 * when this was built), which every change to the scene has to reach, and
 * ROOM_LOOK, which a change on the C side alone -- a camera, the resolve,
 * what the sidecar holds -- has to bump.
 *
 * It was keyed on when this file was built. That kept yesterday's desk off
 * the screen while working on it, and made every update anybody installed
 * trace the whole room again at their next login: on a machine with no GPU
 * to trace it, minutes of a dark room, after every update. */
#define ROOM_LOOK 7u   /* 7: the volume knob lower (2026-11-11); 6: the key upright under its lock and the lid wider (2026-11-11); 5: the
                        * console in four, its lid the quarter's height, and its sheet with the lid
                        * up (2026-11-10) */

static unsigned bake_look(void);

static unsigned icon_build_tag(void)
{
	/* (what the desk being traced is made of: walnut as made is 0, the tag it
	 * always had) */
	return DESK_ROOM_GLSL_KEY ^ (ROOM_LOOK * 2654435761u) ^ (bake_look() * 0x9e3779b1u);
}

/* whether this machine's GPU traces the room right: asked again when the
 * shader or the GPU code changes, or the trial itself (ROOM_TRIAL), and
 * kept until then */
#define ROOM_TRIAL 2u
static unsigned gpu_verdict_tag(void)
{
	return DESK_ROOM_GPU_KEY ^ (ROOM_LOOK * 2654435761u) ^ (ROOM_TRIAL * 40503u);
}

static int icon_cache_dir(char *buf, size_t n)
{
	const char *base = getenv("XDG_CACHE_HOME");
	char tmp[512];
	if (base && *base) {
		if ((size_t)snprintf(tmp, sizeof tmp, "%s/desk", base) >= sizeof tmp) {
			return 0;
		}
	} else {
		const char *home = getenv("HOME");
		if (!home || !*home) {
			return 0;
		}
		if ((size_t)snprintf(tmp, sizeof tmp, "%s/.cache/desk", home) >= sizeof tmp) {
			return 0;
		}
	}
	/* the folders above it too: a new account, or a container, may not
	 * have a ~/.cache yet, and without one the room would be traced again
	 * at every start */
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
				return 0;
			}
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
		return 0;
	}
	if ((size_t)snprintf(buf, n, "%s", tmp) >= n) {
		return 0;
	}
	return 1;
}

/* Bakes from older builds are dead weight; drop them on the way past. */
/* everything of a room that is not this one's -- and not the GPU's
 * verdict -- off the disk */
static void icon_cache_sweep(const char *dir, unsigned keep)
{
	char tag[32], gtag[32];
	snprintf(tag, sizeof tag, "-%u.", keep);
	snprintf(gtag, sizeof gtag, "-%u.", gpu_verdict_tag());
	DIR *d = opendir(dir);
	if (!d) {
		return;
	}
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (strncmp(e->d_name, "room-", 5) != 0 && strncmp(e->d_name, "drawer-", 7) != 0) {
			continue;
		}
		/* kept: this build's, the GPU's verdict -- and not a sheet left
		 * half written (room_png_store) by a session cut short */
		if ((strstr(e->d_name, tag) || (!strncmp(e->d_name, "room-gpu-", 9)
			&& strstr(e->d_name, gtag))) && !strstr(e->d_name, ".part")) {
			continue;
		}
		char path[768];
		if ((size_t)snprintf(path, sizeof path, "%s/%s", dir, e->d_name) < sizeof path) {
			unlink(path);
		}
	}
	closedir(d);
}

/* ---------------------------------------------------------------- */

struct room_cam {
	vec3 eye, ww, uu, vv;
	double fov_x, fov_y;
	double pitch;      /* up from straight down, so two cameras can be mixed */
	double z0, z1;     /* the z range the frame covers at the desk top */
};

/* Straight down, as the flat desktop it replaced was. */
#define RM_FOV_X RM_FOV_X_V

/* The bake has to contain the drawer fully out, or there would be nothing
 * to slide. Both sheets are traced through this one camera -- baking the
 * background at open=0 and the drawer at open=1 put them under different
 * projections, and the drawer then slid across the desk at the wrong size. */
/* The plan camera for a desk desk_hx wide, worked out without touching the
 * room's globals: for knowing where things will be before a room for this
 * screen has been made. The same arithmetic as room_cam_build. */
static void room_cam_plan_at(double aspect, double desk_hx, struct room_cam *c)
{
	c->fov_x = RM_FOV_X;
	c->fov_y = RM_FOV_X / fmax(aspect, 0.2);
	double z0 = RM_ZN - 2.0 * desk_hx / fmax(aspect, 0.2);
	double z1 = RM_ZN + RM_DR_PULL + 0.90;
	double hz = (z1 - z0) * 0.5;
	double hx = hz * fmax(aspect, 0.2);
	if (hx < desk_hx) {
		hx = desk_hx;
		hz = hx / fmax(aspect, 0.2);
	}
	c->z0 = (z0 + z1) * 0.5 - hz;
	c->z1 = (z0 + z1) * 0.5 + hz;
	double d = 2.0 * hx / c->fov_x;
	c->pitch = 0.0;
	c->eye = v3(0.0, d, (z0 + z1) * 0.5);
	c->ww = v3(0, -1, 0);
	c->uu = v3(1, 0, 0);
	c->vv = vcross(c->uu, c->ww);
}

static void room_cam_build(double aspect, struct room_cam *c)
{
	room_set_aspect(aspect);
	c->fov_x = RM_FOV_X;
	c->fov_y = RM_FOV_X / fmax(aspect, 0.2);

	/* z the frame must cover: the desk, plus the drawer at full pull */
	double z0 = RM_ZF;
	double z1 = RM_ZN + RM_DR_PULL + 0.90;
	double hz = (z1 - z0) * 0.5;
	double hx = hz * fmax(aspect, 0.2);
	if (hx < RM_HX) {
		hx = RM_HX;
		hz = hx / fmax(aspect, 0.2);
	}
	c->z0 = (z0 + z1) * 0.5 - hz;
	c->z1 = (z0 + z1) * 0.5 + hz;

	double d = 2.0 * hx / c->fov_x;
	c->pitch = 0.0;
	c->eye = v3(0.0, d, (z0 + z1) * 0.5);
	c->ww = v3(0, -1, 0);
	c->uu = v3(1, 0, 0);
	c->vv = vcross(c->uu, c->ww);
}

/* Pitched up from straight down, framed from two points rather than
 * authored: the top front corner of the cornice lands just under the top
 * edge, and a line on the desk top lands just above the bottom one. That
 * way "the desk is still there at the bottom" holds at any aspect instead
 * of holding at the one it was tuned on.
 *
 * A ray landing at frame height v makes angle atan((0.5-v)*fov_y) with the
 * axis, so for a point P on it, Py - Ey = (Pz - Ez) * cot(phi + psi). Two
 * points, two unknowns, closed form. */
#define RM_SH_PITCH 0.9599    /* 55 degrees up from straight down */
#define RM_SH_V_TOP 0.045
#define RM_SH_V_BOT 0.990
#define RM_SH_DESK_Z (g_rm_zf + 0.42 * (RM_ZN - g_rm_zf))

static void shelf_set_width(const struct room_cam *c)
{
	/* the case is as wide as the frame will take at the binder row, less a
	 * margin of wall, so it reads as a wall of shelving rather than a
	 * wardrobe parked behind the desk */
	/* measured at the corner nearest the camera -- the top front of the
	 * cornice -- because with the camera up and forward, the top of the case
	 * projects wider than the binder row and would be the part that is cut */
	vec3 ref = v3(0.0, RM_SH_CORN_Y, RM_SH_ZF + RM_SH_CORN_P);
	double dw = vdot(vsub(ref, c->eye), c->ww);
	g_rm_sh_hx = fmax(12.0, 0.97 * dw * c->fov_x * 0.5 - RM_SH_CORN_P);
	/* and never too narrow for the book at the end of the row: on a
	 * squarer screen its sides go past the frame rather than the book
	 * going */
	g_rm_sh_hx = fmax(g_rm_sh_hx, RM_BK_X0 + 2.0 * RM_BK_HW + RM_BK_GAP + RM_SH_SIDE);
	/* nor for the favourites at the other end, leaning out at the head */
	const struct shelf_binder *fv = &SHELF_BINDERS[DESK_SHELF_COUNT];
	g_rm_sh_hx = fmax(g_rm_sh_hx, -fv->x + fv->hw + fabs(sin(fv->lean)) * fv->h + RM_BK_GAP
		+ RM_SH_SIDE);
}

static void room_cam_shelf(double aspect, struct room_cam *c)
{
	room_set_aspect(aspect);
	c->fov_x = RM_FOV_X;
	c->fov_y = RM_FOV_X / fmax(aspect, 0.2);

	const double phi = RM_SH_PITCH;
	double fy = c->fov_y;
	double yT = RM_SH_CORN_Y, zT = RM_SH_ZF + RM_SH_CORN_P;
	double yB = 0.0, zB = RM_SH_DESK_Z;
	double cT = 1.0 / tan(phi + atan((0.5 - RM_SH_V_TOP) * fy));
	double cB = 1.0 / tan(phi + atan((0.5 - RM_SH_V_BOT) * fy));
	double ez = (zT * cT - zB * cB - (yT - yB)) / (cT - cB);
	double ey = yT - (zT - ez) * cT;
	c->pitch = phi;
	c->eye = v3(0.0, ey, ez);
	c->ww = v3(0.0, -cos(phi), -sin(phi));
	c->uu = v3(1, 0, 0);
	c->vv = vcross(c->uu, c->ww);
	c->z0 = RM_SH_ZB;
	c->z1 = RM_ZN;
	shelf_set_width(c);
}

/* The console is looked at from over the right of the desk, framed the same
 * way the bookcase is: the top of the tower just under the top edge, the
 * front lip of the operating panel just above the bottom one. */
#define RM_CC_PITCH 0.72
#define RM_CC_V_TOP 0.11
#define RM_CC_V_BOT 0.93

static void room_cam_console(double aspect, struct room_cam *c)
{
	room_set_aspect(aspect);
	c->fov_x = RM_FOV_X;
	c->fov_y = RM_FOV_X / fmax(aspect, 0.2);

	const double phi = RM_CC_PITCH;
	double fy = c->fov_y;
	/* where the machine is when it is out, which on a wide screen is
	 * further forward than it was authored */
	double fwd = cc_forward_at(aspect);
	double yT = RM_CC_TY1 + 0.30, zT = RM_CC_TZ1 + fwd;
	double yB = RM_CC_PY0 - 0.35, zB = RM_CC_PZ0 + 0.60 + fwd;
	double cT = 1.0 / tan(phi + atan((0.5 - RM_CC_V_TOP) * fy));
	double cB = 1.0 / tan(phi + atan((0.5 - RM_CC_V_BOT) * fy));
	double ez = (zT * cT - zB * cB - (yT - yB)) / (cT - cB);
	double ey = yT - (zT - ez) * cT;
	c->pitch = phi;
	c->eye = v3((RM_CC_X0 + RM_CC_X1) * 0.5, ey, ez);
	c->ww = v3(0.0, -cos(phi), -sin(phi));
	c->uu = v3(1, 0, 0);
	c->vv = vcross(c->uu, c->ww);
	c->z0 = RM_CC_ZB;
	c->z1 = RM_ZN;
	/* On a narrow screen the machine is wider than the view its height
	 * frames: stand back along the line of sight until both its ends are
	 * in -- the cheeks, at their feet in front and at the tops of their
	 * tall backs, which looking down on them are the nearest of all. */
	static const double YZ[4][2] = {
		{ 0.0, RM_CC_PZ0 + RM_CC_LEDGE }, { RM_CC_PY0, RM_CC_PZ0 },
		{ RM_CC_TY1, RM_CC_PZ1 }, { RM_CC_TY1, RM_CC_TZ1 },
	};
	for (int it = 0; it < 64; it++) {
		double worst = 0.0;
		for (int k = 0; k < 8; k++) {
			vec3 p = v3((k & 1) ? RM_CC_X1 : RM_CC_X0, YZ[k >> 1][0], YZ[k >> 1][1] + fwd);
			vec3 d = vsub(p, c->eye);
			double dw = vdot(d, c->ww);
			if (dw > 1e-3) {
				worst = fmax(worst, fabs(vdot(d, c->uu) / dw) / (c->fov_x * 0.5));
			}
		}
		if (worst <= 0.965) {
			break;
		}
		c->eye = vsub(c->eye, vmul(c->ww, 0.25));
	}
}

/* Round to the front of the post box and a little above it, framed the way
 * the bookcase and the console are: the back of the cap just under the top
 * edge, the front of the foot just above the bottom one. Less pitch than
 * the console, because this is a tall thing and you look AT it rather than
 * down onto it. */
#define RM_PB_PITCH 1.02
#define RM_PB_V_TOP 0.07
#define RM_PB_V_BOT 0.95

static void room_cam_post(double aspect, struct room_cam *c)
{
	room_set_aspect(aspect);
	c->fov_x = RM_FOV_X;
	c->fov_y = RM_FOV_X / fmax(aspect, 0.2);

	const double phi = RM_PB_PITCH;
	double fy = c->fov_y;
	/* the top of the cap just under the top edge, and the front lip of
	 * the fallen door just above the bottom one */
	double yT = RM_PB_SHOULDER + RM_PB_CAP_T + 0.34, zT = RM_PB_CZ - RM_PB_HZ * 0.2;
	double yB = 0.10, zB = RM_PB_CZ + RM_PB_HZ + 1.30;
	double cT = 1.0 / tan(phi + atan((0.5 - RM_PB_V_TOP) * fy));
	double cB = 1.0 / tan(phi + atan((0.5 - RM_PB_V_BOT) * fy));
	double ez = (zT * cT - zB * cB - (yT - yB)) / (cT - cB);
	double ey = yT - (zT - ez) * cT;
	c->pitch = phi;
	c->eye = v3(RM_PB_CX, ey, ez);
	c->ww = v3(0.0, -cos(phi), -sin(phi));
	c->uu = v3(1, 0, 0);
	c->vv = vcross(c->uu, c->ww);
	c->z0 = RM_PB_CZ - RM_PB_HZ - 1.5;
	c->z1 = RM_ZN;
}

static void room_ray(const struct room_cam *c, double u, double v, vec3 *ro, vec3 *rd)
{
	*ro = c->eye;
	*rd = vnorm(vadd(c->ww, vadd(vmul(c->uu, (u - 0.5) * c->fov_x),
		vmul(c->vv, (0.5 - v) * c->fov_y))));
}

static int room_project(const struct room_cam *c, vec3 p, double *u, double *v)
{
	vec3 w = vsub(p, c->eye);
	double dw = vdot(w, c->ww);
	if (dw < 1e-3) {
		return 0;
	}
	*u = 0.5 + (vdot(w, c->uu) / dw) / c->fov_x;
	*v = 0.5 - (vdot(w, c->vv) / dw) / c->fov_y;
	return 1;
}

/* Where a ray from the camera through (u, v) of its frame meets the lid up on its hinge
 * (g_cc_lid 2), in rf_hit's measure of the ray, or INFINITY. The lid up is in no face's
 * picture, so the console's sheet with it up, laid from the faces, traces what is behind it. */
static double cc_lid_ray(const struct room_cam *c, double u, double v)
{
	if (g_cc_lid < 1.5) {
		return INFINITY;
	}
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	vec3 rd = vadd(c->ww, vadd(vmul(c->uu, (u - 0.5) * c->fov_x),
		vmul(c->vv, (0.5 - v) * c->fov_y)));
	/* into the machine's own frame (the slide), the panel's, and back about the hinge onto
	 * the lid as it lies shut (map_console) */
	vec3 o = cc_to_face(&panel, vsub(c->eye, v3(0.0, 0.0, cc_dz(g_cc_out, g_rm_aspect))));
	vec3 d = v3(vdot(rd, panel.ax), vdot(rd, panel.an), vdot(rd, panel.as));
	double hn = (CC_LID_N0 + CC_LID_N1) * 0.5;
	double lc = cos(CC_LID_SWING), lsn = sin(CC_LID_SWING);
	double ods = o.z - CC_LID_HINGE_S, odn = o.y - hn;
	const double lo[3] = { o.x, hn + ods * lsn + odn * lc, CC_LID_HINGE_S + ods * lc - odn * lsn };
	const double ld[3] = { d.x, d.z * lsn + d.y * lc, d.z * lc - d.y * lsn };
	/* its box, and its rounded edges and the lip */
	const double b0[3] = { CC_LID_U0 - 0.06, CC_LID_N0 - 0.06, CC_LID_S0 - 0.06 };
	const double b1[3] = { CC_LID_U1 + 0.06, CC_LID_N1 + 0.08, CC_LID_S1 + 0.06 };
	double t0 = 0.0, t1 = INFINITY;
	for (int k = 0; k < 3; k++) {
		if (fabs(ld[k]) < 1e-12) {
			if (lo[k] < b0[k] || lo[k] > b1[k]) {
				return INFINITY;
			}
			continue;
		}
		double ta = (b0[k] - lo[k]) / ld[k], tb = (b1[k] - lo[k]) / ld[k];
		t0 = fmax(t0, fmin(ta, tb));
		t1 = fmin(t1, fmax(ta, tb));
		if (t0 > t1) {
			return INFINITY;
		}
	}
	return t0;
}

struct room_render {
	int w, h;
	cairo_surface_t *background;
	cairo_surface_t *drawer;
	cairo_surface_t *shelf;       /* the bookcase, through the tilted camera */
	cairo_surface_t *console;     /* the control console, through its own */
	cairo_surface_t *post;        /* the post box, likewise */
	cairo_surface_t *film[DESK_VIEW_COUNT];   /* the moves, traced frame by frame; put
	                               * in after the views are up, so read and
	                               * written under g_room_lock (room_film_get) */
	double felt_mid;              /* how lit the drawer's felt is mid-row (0: not yet looked) */
	unsigned char *shelf_ids;     /* 0 scenery, i+1 binder i */
	unsigned char *console_ids;   /* 0 case, i+1 control i */
	/* the console's view again with no lid over the guarded set, for what is under it as it
	 * swings; traced after the views, before the films, and like them put in under
	 * g_room_lock (room_bare_get) */
	cairo_surface_t *console_bare;
	unsigned char *console_bare_ids;
	unsigned char *post_ids;      /* 0 box, i+1 letter i */
	struct room_cam cam;
	struct room_cam shelf_cam;
	struct room_cam console_cam;
	struct room_cam post_cam;
	double slide, edge;
	double shut_zoom, shut_pan;
	double dr_v0, dr_v1;
	unsigned serial;              /* which room this is, from 1: what is painted from a
	                               * room is kept by this, since a room freed and the
	                               * next one made can come at the same address */
	int film_div;                 /* its films at 1/n of its size; 0: none */
};
static _Atomic unsigned g_room_serial;

/* How far the trace has got, for the card that lies on the desk while the
 * desk is still being made. Counted in rows of the supersampled buffer,
 * because a sheet is ten seconds and nobody waits through a bar with eight
 * positions on it. Only the two desk sheets are counted: they are what the
 * bar is promising, and the rest is traced after the desk is already up. */
static _Atomic long g_bake_rows;
static _Atomic long g_bake_goal;
/* The three views after the desk, a sheet at a time, each weighed by its
 * pixels: what is done of them, the sheet being traced and its weight, and
 * which of the views that is (0 the bookcase, 1 the console, 2 the post
 * box; -1 none yet). For the clock while it counts. The films of the moves
 * come after all three and are not counted: nothing waits on them. */
static _Atomic long g_rest_done_w, g_rest_part_w, g_rest_total_w;
static _Atomic int g_rest_view = -1;
/* whether the screen has asked for another room than the one being made,
 * and a view of the room that is up there to turn to (below, with the lock
 * both are under) */
static bool room_superseded(void);
static void room_views_up(struct room_render *rr, unsigned views);
/* Whether what is being traced is something nobody is waiting for -- the
 * films, after the views are up -- so the GPU it shares with the screen is
 * handed back often (room_gpu_tiles). Only the tracing thread touches it. */
static bool g_room_gentle;

struct room_ctx {
	float *hi;
	unsigned char *idhi;
	int hw, hh;
	int layer;
	struct room_cam cam;
	int next_row;
	pthread_mutex_t lock;
	const unsigned char *need;   /* the samples to trace, or NULL: all */
};

/* Which object a surface belongs to, for the hover mask and for picking.
 * Only the objects themselves: the bound is a box, and attributing the felt
 * inside it to the object put a rectangle of highlight around each one. */
static int room_owner(vec3 p, int id)
{
	if (g_room_layer == 4) {
		return console_owner(p);
	}
	if (g_room_layer == 5) {
		return (id == MID_POST_RED || id == MID_POST_DARK) ? 1 : 0;
	}
	if (g_room_layer >= 3) {
		/* on the shelf, an owner is a binder: which one you would take
		 * down if you reached for this pixel */
		if (id == MID_BOOK_LEATHER || id == MID_BOOK_GILT) {
			return DESK_SHELF_COUNT + 1;
		}
		if (id != MID_BINDER_A && id != MID_BINDER_B && id != MID_BINDER_FAV
			&& id != MID_BINDER_LABEL && id != MID_FOLDER_PAPER) {
			return 0;
		}
		/* the bounds overlap where one leans towards the next (the
		 * favourites and the first of the eight): the nearest of those
		 * the point is in */
		int best = -1;
		double bd = 1e9;
		for (int i = 0; i < SHELF_ROW; i++) {
			vec3 c, e;
			binder_bound(i, &c, &e);
			if (sd_box(vsub(p, c), e) < 0.0) {
				double d = shelf_binder_map(p, i).d;
				if (d < bd) {
					bd = d;
					best = i;
				}
			}
		}
		return best < 0 ? 0 : shelf_row_thing(best) + 1;
	}
	if (id <= 0 || id > MID_GEAR) {
		return 0;
	}
	vec3 ip = vsub(p, v3(0, RM_ITEM_Y, RM_ITEM_Z + room_pull()));
	return drawer_item_at(ip, NULL) + 1;
}

/* One sample of a sheet: x and y are in supersamples, hw by hh of them.
 * Linear RGB and coverage go in out, and the owner comes back. */
static int room_sample(const struct room_cam *cam, int layer, int hw, int hh,
	int x, int y, float out[4])
{
	int s = (y * hw + x) & 3;
	double u = (x + SS_OFF[s][0]) / hw;
	double v = (y + SS_OFF[s][1]) / hh;
	vec3 ro, rd;
	room_ray(cam, u, v, &ro, &rd);

	double cr = 0, cg = 0, cb = 0, ca = 0;
	int owner = 0;
	double t = 0.02;
	for (int i = 0; i < 260 && t < 240.0; i++) {
		vec3 hp = vadd(ro, vmul(rd, t));
		hit3 hit = scene_map(hp, ROOM_KIND);
		double eps = 0.0018 * fmax(t * 0.06, 1.0);
		if (hit.d < eps && hit.id > 0) {
			/* the drawer sheet keeps only the drawer; the desk is
			 * traced with it so that it still occludes, then dropped */
			if (layer == 2 && !room_is_drawer(hit.id)) {
				break;
			}
			vec3 n = scene_normal(hp, ROOM_KIND);
			vec3 col = shade_surface(hp, n, rd, ROOM_KIND, hit.id, 1.0, 0);
			cr = col.x;
			cg = col.y;
			cb = col.z;
			ca = 1.0;
			owner = room_owner(hp, hit.id);
			break;
		}
		t += fmax(hit.d, eps * 0.9);
	}
	out[0] = (float)cr;
	out[1] = (float)cg;
	out[2] = (float)cb;
	out[3] = (float)ca;
	return owner;
}

static void room_row(struct room_ctx *ctx, int y)
{
	float *row = ctx->hi + (size_t)y * ctx->hw * 4;
	unsigned char *idrow = ctx->idhi + (size_t)y * ctx->hw;
	const unsigned char *need = ctx->need ? ctx->need + (size_t)y * ctx->hw : NULL;
	for (int x = 0; x < ctx->hw; x++) {
		if (need && !need[x]) {
			continue;
		}
		idrow[x] = (unsigned char)room_sample(&ctx->cam, ctx->layer,
			ctx->hw, ctx->hh, x, y, row + x * 4);
	}
}

/* The session is ending and the room's warm-up is to stop where it is
 * (desk_room_stop): looked at between rows here, between windows on the
 * GPU, and before anything of a room is put up or kept. */
static _Atomic int g_room_stop;

static bool room_stopping(void)
{
	return atomic_load_explicit(&g_room_stop, memory_order_relaxed) != 0;
}

/* A thread that traces, reads back or makes things ahead runs at the back
 * of the queue (nice 19): on what the desk and its programs leave. On two
 * cores, two threads tracing at the compositor's own priority left it a
 * third of the machine, and the desk was slow to the hand for minutes --
 * just while it was first being tried. Nothing these do is waited on by a
 * frame. On Linux the nice value is the thread's own; only threads made
 * for the purpose call this, never one that might be the compositor's. */
static void thread_to_the_back(void)
{
	(void)setpriority(PRIO_PROCESS, 0, 19);
}

/* How many threads a job made ahead is shared out to. Before the lamp is up, all the machine has
 * (up to eight): somebody is waiting for the room. After, half of them: what is made then -- the
 * other views, the films of the moves, the door -- nobody is waiting for, and every core going
 * flat out for the next twenty seconds made the desk itself slow to the hand on a Surface Pro,
 * a machine with no fan that slows itself down as it warms (2026-11-12, the owner: CPU使用率が高い
 * （起動直後？）). desk_bake_calm says which. */
static _Atomic int g_bake_calm;

void desk_bake_calm(bool calm)
{
	atomic_store(&g_bake_calm, calm ? 1 : 0);
}

static int bake_threads(void)
{
	long nproc = sysconf(_SC_NPROCESSORS_ONLN);
	int n = nproc > 8 ? 8 : nproc > 0 ? (int)nproc : 1;
	if (atomic_load(&g_bake_calm) && n > 1) {
		n = (n + 1) / 2;
	}
	return n;
}

static void *room_worker(void *arg)
{
	struct room_ctx *ctx = arg;
	for (;;) {
		pthread_mutex_lock(&ctx->lock);
		int y = ctx->next_row++;
		pthread_mutex_unlock(&ctx->lock);
		if (y >= ctx->hh || room_stopping()) {
			break;
		}
		room_row(ctx, y);
		atomic_fetch_add_explicit(&g_bake_rows, 1, memory_order_relaxed);
	}
	return NULL;
}

static void *room_worker_thread(void *arg)
{
	thread_to_the_back();
	return room_worker(arg);
}

/* The whole sheet on this machine's cores: the reference, and what
 * everything else falls back to. */
static void room_trace_cpu(struct room_ctx *ctx)
{
	ctx->next_row = 0;
	pthread_mutex_init(&ctx->lock, NULL);
	int nthreads = bake_threads();
	pthread_t th[8];
	int started = 0;
	for (int i = 0; i + 1 < nthreads; i++) {
		if (pthread_create(&th[started], NULL, room_worker_thread, ctx) == 0) {
			started++;
		}
	}
	room_worker(ctx);
	for (int i = 0; i < started; i++) {
		pthread_join(th[i], NULL);
	}
	pthread_mutex_destroy(&ctx->lock);
}

#ifdef DESK_HAVE_GPU
/* ---- the same trace, on a GPU ---------------------------------------
 *
 * room.glsl is this file's tracer, line for line, and this is what feeds
 * it: every named number it has to agree with this file about, written out
 * ahead of its text, and the sheet handed over a window at a time.
 *
 * It is never simply trusted. Every sheet it traces is spot-checked
 * against the C, sample by sample. A layer it disagrees about is traced by
 * the C from then on; a second, and the GPU -- a driver that miscompiled
 * it, a float target that is not what it said -- is put down for the life
 * of the process, and the C traces the room the way it always has. So is
 * one that will not compile it, or fails a draw. None of that is an error
 * anybody has to act on: it is a slower bake, and a line on stderr saying
 * why (in ~/.local/state/desk/session.log, under a login). */

static double room_now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

/* In the child a trial runs in, the pipe back to the parent (below): what
 * it would have logged goes there instead, with how far it has got. */
static int g_trial_fd = -1;

static void room_trial_line(char kind, const char *fmt, va_list ap)
{
	char line[512];
	line[0] = kind;
	line[1] = ' ';
	int n = vsnprintf(line + 2, sizeof line - 3, fmt, ap);
	size_t len = n < 0 ? 2 : (size_t)n + 2 < sizeof line - 1 ? (size_t)n + 2 : sizeof line - 2;
	for (size_t i = 2; i < len; i++) {
		if (line[i] == '\n') {
			line[i] = ' ';
		}
	}
	line[len] = '\n';
	/* one write, under PIPE_BUF: a line arrives whole or not at all */
	ssize_t w = write(g_trial_fd, line, len + 1);
	(void)w;
}

static void room_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (g_trial_fd >= 0) {
		room_trial_line('n', fmt, ap);
		va_end(ap);
		return;
	}
	fputs("desk: room: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/* where the trial's child has got to, for the parent to say if it never
 * comes back */
static void room_trial_stage(const char *fmt, ...)
{
	if (g_trial_fd < 0) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	room_trial_line('s', fmt, ap);
	va_end(ap);
}

struct glsl_buf {
	char *s;
	size_t n, cap;
	bool bad;
};

static void gb_put(struct glsl_buf *b, const char *fmt, ...)
{
	while (!b->bad) {
		size_t room = b->cap - b->n;
		va_list ap;
		va_start(ap, fmt);
		int n = vsnprintf(b->s ? b->s + b->n : NULL, b->s ? room : 0, fmt, ap);
		va_end(ap);
		if (n < 0) {
			b->bad = true;
			return;
		}
		if (b->s && (size_t)n < room) {
			b->n += (size_t)n;
			return;
		}
		size_t cap = b->cap ? b->cap : 16384;
		while (cap < b->n + (size_t)n + 1) {
			cap *= 2;
		}
		char *q = realloc(b->s, cap);
		if (!q) {
			b->bad = true;
			return;
		}
		b->s = q;
		b->cap = cap;
	}
}

/* A float the way GLSL wants one: with a point in it -- GLSL ES will not
 * turn 16 into 16.0 by itself -- and in brackets when it is negative, so
 * that no macro ever lands one after a minus as a "--". The caller has
 * the C locale in force, whatever the process's own is. */
static void gb_float(struct glsl_buf *b, double v)
{
	char t[48];
	snprintf(t, sizeof t, "%.9g", v);
	if (!strpbrk(t, ".eEni")) {
		size_t l = strlen(t);
		if (l + 3 <= sizeof t) {
			memcpy(t + l, ".0", 3);
		}
	}
	gb_put(b, t[0] == '-' ? "(%s)" : "%s", t);
}

static void gb_def(struct glsl_buf *b, const char *name, double v)
{
	gb_put(b, "#define %s ", name);
	gb_float(b, v);
	gb_put(b, "\n");
}

static void gb_defv(struct glsl_buf *b, const char *name, vec3 v)
{
	gb_put(b, "#define %s vec3(", name);
	gb_float(b, v.x);
	gb_put(b, ", ");
	gb_float(b, v.y);
	gb_put(b, ", ");
	gb_float(b, v.z);
	gb_put(b, ")\n");
}

static void gb_farr(struct glsl_buf *b, const char *name, const double *v, int n)
{
	gb_put(b, "const float %s[%d] = float[%d](", name, n, n);
	for (int i = 0; i < n; i++) {
		gb_put(b, i ? ", " : "");
		gb_float(b, v[i]);
	}
	gb_put(b, ");\n");
}

static void gb_iarr(struct glsl_buf *b, const char *name, const int *v, int n)
{
	gb_put(b, "const int %s[%d] = int[%d](", name, n, n);
	for (int i = 0; i < n; i++) {
		gb_put(b, i ? ", %d" : "%d", v[i]);
	}
	gb_put(b, ");\n");
}

static void gb_varr(struct glsl_buf *b, const char *name, const vec3 *v, int n)
{
	gb_put(b, "const vec3 %s[%d] = vec3[%d](", name, n, n);
	for (int i = 0; i < n; i++) {
		gb_put(b, i ? ", vec3(" : "vec3(");
		gb_float(b, v[i].x);
		gb_put(b, ", ");
		gb_float(b, v[i].y);
		gb_put(b, ", ");
		gb_float(b, v[i].z);
		gb_put(b, ")");
	}
	gb_put(b, ");\n");
}

/* Every name room.glsl uses and this file defines. */
#define ROOM_GLSL_INTS(X) \
	X(DESK_DOCK_COUNT) X(DESK_GADGET_COUNT) X(DESK_GADGET_CLOCK) X(DESK_SHELF_COUNT) \
	X(DESK_SHELF_FAV) X(SHELF_ROW) \
	X(DESK_ICON_TERM) X(DESK_ICON_GLOBE) X(DESK_ICON_FOLDER) \
	X(DESK_ICON_NOTES) X(DESK_ICON_GEAR) X(ROOM_SEED_ROW) \
	X(MID_CRT_BODY) X(MID_CRT_BEZEL) X(MID_CRT_SCREEN) X(MID_CRT_KEYBED) \
	X(MID_CRT_KEY) X(MID_CRT_LED) X(MID_GLOBE) X(MID_GLOBE_RING) \
	X(MID_GLOBE_STAND) X(MID_FOLDER) X(MID_FOLDER_PAPER) X(MID_NOTES) \
	X(MID_NOTES_BIND) X(MID_GEAR) X(MID_FELT) X(MID_DRAWER_WOOD) \
	X(MID_ROOM_FLOOR) X(MID_ROOM_WALL) X(MID_ROOM_SKIRT) X(MID_DESK_TOP) \
	X(MID_DESK_BODY) X(MID_DRAWER_FACE) X(MID_BRASS) X(MID_GADGET_BODY) \
	X(MID_GADGET_GLASS) X(MID_GADGET_LEATHER) X(MID_GADGET_DIAL) \
	X(MID_GADGET_PAPER) X(MID_SHELF_CASE) X(MID_SHELF_BOARD) \
	X(MID_SHELF_BACK) X(MID_BINDER_A) X(MID_BINDER_B) X(MID_BINDER_LABEL) \
	X(MID_CC_STEEL) X(MID_CC_PANEL) X(MID_CC_BRASS) X(MID_CC_BAKELITE) \
	X(MID_CC_GLASS) X(MID_CC_CHEEK) X(MID_CC_LENS_AMBER) \
	X(MID_CC_LENS_GREEN) X(MID_CC_LENS_BLUE) X(MID_CC_LENS_RED) \
	X(MID_POST_RED) X(MID_POST_DARK) X(MID_BOOK_LEATHER) X(MID_BOOK_GILT) \
	X(MID_BINDER_FAV) X(MID_CC_LENS_WHITE) \
	X(CC_WIFI) X(CC_BT) X(CC_VOL) X(CC_BRIGHT) X(CC_CRT) X(CC_BATT) \
	X(CC_NET) X(CC_PW_OFF) X(CC_PW_BOOT) X(CC_PW_OUT) X(CC_PW_KEY) X(CC_SCREEN) X(CC_LOCK) \
	X(CC_SLEEP) X(CC_PW_SUSPEND) X(CC_AIR) X(CC_SAVE) X(CC_NIGHT) X(CC_DISPLAY) X(CC_LID) \
	X(CC_COUNT) X(CC_ON_FACE) X(CC_ON_LID) X(CC_ON_UNDER) \
	X(GADGET_TENT)

#define ROOM_GLSL_FLOATS(X) \
	X(RM_HX_MIN) X(RM_ZN) X(RM_TOP_T) X(RM_FLOOR) X(RM_FOV_X_V) \
	X(RM_DR_HX) X(RM_DR_TOP) X(RM_DR_BOT) X(RM_DR_FACE) X(RM_DR_BACK) \
	X(RM_DR_PULL) X(RM_ITEM_Y) X(RM_ITEM_Z) \
	X(RM_SH_PLINTH_Z) X(RM_SH_CLEAR) X(RM_SH_D) X(RM_SH_SIDE) \
	X(RM_SH_BACKT) X(RM_SH_BOARD) X(RM_SH_PLINTH) X(RM_SH_BAY_B) \
	X(RM_SH_BAY_T) X(RM_SH_TOP) X(RM_SH_CORN_Y) X(RM_SH_CORN_P) \
	X(RM_BN_DEPTH) X(RM_BN_FOOT) X(DRAWER_CULL) X(DRAWER_NEAR) \
	X(RM_BK_HW) X(RM_BK_H) X(RM_BK_D) X(RM_BK_X0) X(RM_BK_GAP) \
	X(RM_CC_CHEEK) X(RM_CC_PLINTH) X(RM_CC_LEDGE) \
	X(RM_CC_PLINTH_Z0) \
	X(RM_CC_PY0) X(RM_CC_PZ0) X(RM_CC_PY1) X(RM_CC_PZ1) X(RM_CC_TY0) \
	X(RM_CC_TZ0) X(RM_CC_TY1) X(RM_CC_TZ1) X(RM_CC_ZB) X(RM_CC_BODY_Z0) X(RM_CC_SKIN) \
	X(CC_PW_R) X(CC_PW_RING) X(CC_PW_DOME) X(CC_LB_R) X(CC_LB_RING) X(CC_LB_DOME) \
	X(CC_PW_LOCK_U) X(CC_PW_KEY_S) \
	X(CC_PW_LAMP_S) X(CC_LID_U0) X(CC_LID_U1) X(CC_LID_S0) X(CC_LID_S1) X(CC_LID_N0) \
	X(CC_LID_N1) X(CC_LID_HINGE_S) X(CC_PART_U) \
	X(RM_PB_HX) X(RM_PB_HZ) X(RM_PB_PLINTH) \
	X(RM_PB_SHOULDER) X(RM_PB_CAP_T) X(RM_PB_TILT) X(RM_PB_SLOT_HU) \
	X(RM_PB_SLOT_HS) X(RM_PB_SLOT_S) X(RM_PB_TOP_Y) X(RM_PB_TOP_DZ) \
	X(RM_PB_FS_Y) X(RM_PB_FS_HU) X(RM_PB_FS_HH) X(RM_PB_FS_FR) \
	X(RM_PB_HOOD_OUT) X(RM_PB_HOOD_TILT) \
	X(RM_PB_TOP_GAP) \
	X(SHELF_LAMP_Y) X(SHELF_LAMP_IRR) X(LIT_KEY_IRRADIANCE) \
	X(ROOM_LAMP_REF0) X(ROOM_LAMP_RADIUS0) X(ROOM_LAMP_CONE_IN) \
	X(ROOM_LAMP_CONE_OUT) \
	X(CAL_T) X(CAL_PZ) X(CAL_PY0) X(CAL_PTOP) X(CAL_PIN) X(CAL_TB) \
	X(CAL_RY) X(CAL_RR) X(CAL_RW) X(CAL_RP) X(CAL_RT) X(CAL_RM)

#define ROOM_GLSL_VEC3S(X) \
	X(LIT_KEY_DIR) X(LIT_KEY_COL) X(LIT_FILL_DIR) X(LIT_FILL_COL) \
	X(LIT_RIM_DIR) X(LIT_RIM_COL) X(ROOM_LAMP_POS0) X(ROOM_LAMP_AIM) \
	X(SHELF_LAMP_COL) X(SHELF_LAMP_AIM)

/* The source of one of room.glsl's passes: everything above, then it. */
static void room_glsl_build(struct glsl_buf *b, int pass)
{
	drawer_items_build();
	sh_build();

	/* numbers are written the C way, whatever the process's locale says a
	 * decimal point is; uselocale is this thread's alone */
	locale_t c_loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
	locale_t prev = c_loc ? uselocale(c_loc) : (locale_t)0;

	gb_put(b, "#version 300 es\nprecision highp float;\nprecision highp int;\n"
		"precision highp sampler2D;\n#define ROOM_PASS %d\n", pass);
#define GB_INT(n) gb_put(b, "#define %s %d\n", #n, (int)(n));
#define GB_FLT(n) gb_def(b, #n, (double)(n));
#define GB_V3(n) gb_defv(b, #n, n);
	ROOM_GLSL_INTS(GB_INT)
	ROOM_GLSL_FLOATS(GB_FLT)
	ROOM_GLSL_VEC3S(GB_V3)
#undef GB_INT
#undef GB_FLT
#undef GB_V3

	{
		enum { N = DESK_DOCK_COUNT };
		int kind[N];
		double x[N], z[N], yaw[N], pitch[N], scale[N], y[N];
		for (int i = 0; i < N; i++) {
			kind[i] = DRAWER_ITEMS[i].kind;
			x[i] = DRAWER_ITEMS[i].x;
			z[i] = DRAWER_ITEMS[i].z;
			yaw[i] = DRAWER_ITEMS[i].yaw;
			pitch[i] = DRAWER_ITEMS[i].pitch;
			scale[i] = DRAWER_ITEMS[i].scale;
			y[i] = g_item_y[i];
		}
		gb_iarr(b, "ITEM_KIND", kind, N);
		gb_farr(b, "ITEM_X", x, N);
		gb_farr(b, "ITEM_Z", z, N);
		gb_farr(b, "ITEM_YAW", yaw, N);
		gb_farr(b, "ITEM_PITCH", pitch, N);
		gb_farr(b, "ITEM_SCALE", scale, N);
		gb_farr(b, "ITEM_Y", y, N);
		gb_varr(b, "ITEM_C", g_item_c, N);
		gb_varr(b, "ITEM_E", g_item_e, N);
	}
	{
		enum { N = DESK_GADGET_COUNT };
		double x[N], z[N], hx[N], hz[N], h[N], inset[N], foot[N], tilt[N], plinth[N];
		int round_[N], body[N], face[N], kind[N];
		for (int i = 0; i < N; i++) {
			const struct room_gadget *g = &ROOM_GADGETS[i];
			x[i] = g->x;
			z[i] = g->z;
			hx[i] = g->hx;
			hz[i] = g->hz;
			h[i] = g->h;
			inset[i] = g->inset;
			foot[i] = g->foot;
			tilt[i] = g->tilt;
			plinth[i] = g->plinth;
			round_[i] = g->round;
			body[i] = g->body;
			face[i] = g->face;
			kind[i] = g->kind;
		}
		gb_farr(b, "GD_X", x, N);
		gb_farr(b, "GD_Z", z, N);
		gb_farr(b, "GD_HX", hx, N);
		gb_farr(b, "GD_HZ", hz, N);
		gb_farr(b, "GD_H", h, N);
		gb_farr(b, "GD_INSET", inset, N);
		gb_farr(b, "GD_FOOT", foot, N);
		gb_farr(b, "GD_TILT", tilt, N);
		gb_farr(b, "GD_PLINTH", plinth, N);
		gb_iarr(b, "GD_ROUND", round_, N);
		gb_iarr(b, "GD_BODY", body, N);
		gb_iarr(b, "GD_FACE", face, N);
		gb_iarr(b, "GD_KIND", kind, N);
	}
	{
		enum { N = CC_COUNT };
		int tower[N], on[N];
		double u[N], sv[N], hu[N], hs[N];
		for (int i = 0; i < N; i++) {
			tower[i] = CC[i].tower;
			on[i] = CC[i].on;
			u[i] = CC[i].u;
			sv[i] = CC[i].s;
			hu[i] = CC[i].hu;
			hs[i] = CC[i].hs;
		}
		gb_iarr(b, "CC_TOWER", tower, N);
		gb_iarr(b, "CC_ON", on, N);
		int mid[N];
		for (int i = 0; i < N; i++) {
			mid[i] = cc_btn_mid(i);
		}
		gb_iarr(b, "CC_BTN", mid, N);
		int twin[N];
		for (int i = 0; i < N; i++) {
			twin[i] = cc_twin(i);
		}
		gb_iarr(b, "CC_TWIN", twin, N);
		gb_farr(b, "CC_U", u, N);
		gb_farr(b, "CC_S", sv, N);
		gb_farr(b, "CC_HU", hu, N);
		gb_farr(b, "CC_HS", hs, N);
	}
	{
		enum { N = SHELF_ROW };
		double x[N], hw[N], h[N], lean[N], label_y[N];
		int mid[N];
		for (int i = 0; i < N; i++) {
			const struct shelf_binder *bn = &SHELF_BINDERS[i];
			x[i] = bn->x;
			hw[i] = bn->hw;
			h[i] = bn->h;
			lean[i] = bn->lean;
			label_y[i] = bn->label_y;
			mid[i] = bn->mid;
		}
		gb_farr(b, "BN_X", x, N);
		gb_farr(b, "BN_HW", hw, N);
		gb_farr(b, "BN_H", h, N);
		gb_farr(b, "BN_LEAN", lean, N);
		gb_farr(b, "BN_LABEL_Y", label_y, N);
		gb_iarr(b, "BN_MID", mid, N);
	}
	gb_varr(b, "ROOM_SH", g_sh, 9);
	gb_put(b, "const vec2 SS_OFF[4] = vec2[4](");
	for (int i = 0; i < 4; i++) {
		gb_put(b, i ? ", vec2(" : "vec2(");
		gb_float(b, SS_OFF[i][0]);
		gb_put(b, ", ");
		gb_float(b, SS_OFF[i][1]);
		gb_put(b, ")");
	}
	gb_put(b, ");\n#line 1\n");

	if (c_loc) {
		uselocale(prev);
		freelocale(c_loc);
	}

	/* DESK_ROOM_GLSL: the shader's text from a file rather than the one
	 * built in, so it can be worked on without a rebuild */
	const char *path = getenv("DESK_ROOM_GLSL");
	FILE *f = path && *path ? fopen(path, "rbe") : NULL;
	if (f) {
		char chunk[8192];
		size_t got;
		while ((got = fread(chunk, 1, sizeof chunk - 1, f)) > 0) {
			chunk[got] = '\0';
			gb_put(b, "%s", chunk);
		}
		fclose(f);
		return;
	}
	gb_put(b, "%s", ROOM_GLSL);
}

/* The GPU for one run of room_build_now: opened on the first sheet, on
 * the thread that traces them all, and closed at the end of it. */
enum { ROOM_PASSES = 4 };

static struct {
	struct desk_gpu *g;
	int prog[ROOM_PASSES];
	bool tried;      /* this run */
	bool dead;       /* for the life of the process */
	bool trial;      /* this process IS the trial: see below */
} g_rgpu;

static bool room_trace_gpu(struct room_ctx *ctx);

/* ---- a driver that has not been seen before is tried at arm's length ----
 *
 * A driver that cannot take a program is supposed to say so. Not all of
 * them do: SVGA3D, handed an earlier cut of this shader in one piece,
 * crashed inside glLinkProgram and took the process with it. For the
 * preview that is a failed command; for the compositor it is the session.
 *
 * So the first time a build meets a driver, the programs are built and a
 * corner of the desk is traced and checked in a child: this same
 * executable, started again with DESK_GPU_PROBE naming a pipe, which the
 * constructor at the end of this turns into that and nothing else. A byte
 * down the pipe is its verdict; the pipe closing without one is a crash,
 * and silence for a minute and a half is a hang, which is killed. The
 * pipe and not the exit status: a crash and a clean exit that said nothing
 * look the same from there, and the pipe also tells a hang apart.
 *
 * The verdict is kept in the cache directory, per driver and per shader and
 * GPU code (gpu_verdict_tag) -- room-gpu-<driver>-<tag>.ok or .bad -- so it
 * is asked once. Delete it
 * to ask again. */

static bool room_gpu_verdict_path(struct desk_gpu *g, char *out, size_t n)
{
	char dir[512];
	if (!icon_cache_dir(dir, sizeof dir)) {
		return false;
	}
	unsigned h = 2166136261u;
	for (const char *c = desk_gpu_name(g); *c; c++) {
		h = (h ^ (unsigned char)*c) * 16777619u;
	}
	return (size_t)snprintf(out, n, "%s/room-gpu-%08x-%u", dir, h, gpu_verdict_tag()) < n;
}

/* 1 it passed, 0 it did not, -1 nobody has asked; and for one that did not,
 * why, as the trial said it (room_gpu_remember) */
static int room_gpu_verdict(struct desk_gpu *g, char *why, size_t whysz)
{
	char base[640], path[700];
	if (getenv("DESK_ROOM_GLSL") || !room_gpu_verdict_path(g, base, sizeof base)) {
		return -1;
	}
	snprintf(path, sizeof path, "%s.ok", base);
	if (access(path, F_OK) == 0) {
		return 1;
	}
	snprintf(path, sizeof path, "%s.bad", base);
	FILE *f = fopen(path, "re");
	if (!f) {
		return -1;
	}
	if (whysz > 0) {
		if (!fgets(why, (int)whysz, f)) {
			why[0] = '\0';
		}
		why[strcspn(why, "\n")] = '\0';
	}
	fclose(f);
	return 0;
}

static void room_gpu_remember(struct desk_gpu *g, bool ok, const char *why)
{
	char base[640], path[700];
	if (getenv("DESK_ROOM_GLSL") || !room_gpu_verdict_path(g, base, sizeof base)) {
		return;
	}
	snprintf(path, sizeof path, "%s.%s", base, ok ? "ok" : "bad");
	FILE *f = fopen(path, "we");
	if (f) {
		if (!ok && why && *why) {
			fprintf(f, "%s\n", why);
		}
		fclose(f);
	}
}

/* 1 the child built and traced the room and it agreed, 0 it did not or
 * it died trying, -1 there was no child to ask. node is the render node
 * this process opened its GPU on, if it did: the child is told to try that
 * one first (DESK_GPU_NODE), or it would take the first there is, which on
 * a machine with two GPUs need not be the one its verdict is kept for.
 *
 * The child says, a line at a time, where it has got to ("s ...") and what
 * it would have logged ("n ...": a shader the driver would not build, a
 * draw that failed, how many samples disagreed), and last its verdict
 * ("v1" or "v0"). why is filled with what is worth saying: the reason it
 * failed -- its own words, or that it crashed or went quiet, and where --
 * or, for one that passed, how long it took. It is given two minutes of
 * silence, not a deadline: a driver that is slow to build a program this
 * size is still building it, and one that has stopped says nothing. */
static int room_gpu_trial_run(const char *node, char *why, size_t whysz)
{
	why[0] = '\0';
	int fds[2];
	if (pipe(fds) != 0) {
		return -1;
	}
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	/* dup2 onto itself would leave the close-on-exec flag standing */
	int target = fds[1] == 3 ? 4 : 3;

	size_t n = 0;
	while (environ && environ[n]) {
		n++;
	}
	char **envp = calloc(n + 3, sizeof *envp);
	char probe[40], on[96];
	snprintf(probe, sizeof probe, "DESK_GPU_PROBE=%d", target);
	bool pin = node && *node
		&& (size_t)snprintf(on, sizeof on, "DESK_GPU_NODE=%s", node) < sizeof on;
	posix_spawn_file_actions_t fa;
	bool fa_ok = posix_spawn_file_actions_init(&fa) == 0;
	int rc = -1;
	pid_t pid = -1;
	if (envp && fa_ok) {
		size_t k = 0;
		for (size_t i = 0; i < n; i++) {
			if (strncmp(environ[i], "DESK_GPU_PROBE=", 15) != 0
				&& !(pin && strncmp(environ[i], "DESK_GPU_NODE=", 14) == 0)) {
				envp[k++] = environ[i];
			}
		}
		envp[k++] = probe;
		if (pin) {
			envp[k++] = on;
		}
		posix_spawn_file_actions_adddup2(&fa, fds[1], target);
		posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
		posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
		char *argv[] = { (char *)"desk-gpu-trial", NULL };
		rc = posix_spawn(&pid, "/proc/self/exe", &fa, NULL, argv, envp);
	}
	if (fa_ok) {
		posix_spawn_file_actions_destroy(&fa);
	}
	free(envp);
	close(fds[1]);
	if (rc != 0) {
		close(fds[0]);
		return -1;
	}

	char buf[2048], stage[160] = "starting", said[400] = "";
	size_t have = 0;
	int verdict = -1;
	bool ended = false, quiet = false;
	double t0 = room_now(), heard = t0;
	while (verdict < 0 && !ended) {
		double now = room_now();
		/* or the session is ending, and the child is not waited for */
		if (room_stopping()) {
			break;
		}
		if (now - heard > 120.0 || now - t0 > 900.0) {
			quiet = true;
			break;
		}
		struct pollfd pfd = { fds[0], POLLIN, 0 };
		int pr = poll(&pfd, 1, 100);
		if (pr <= 0) {
			continue;
		}
		ssize_t got = read(fds[0], buf + have, sizeof buf - 1 - have);
		if (got <= 0) {
			ended = got == 0 || errno != EINTR;
			continue;
		}
		heard = room_now();
		have += (size_t)got;
		buf[have] = '\0';
		char *line = buf, *nl;
		while ((nl = strchr(line, '\n')) != NULL) {
			*nl = '\0';
			if (line[0] == 's' && line[1] == ' ') {
				snprintf(stage, sizeof stage, "%s", line + 2);
			} else if (line[0] == 'n' && line[1] == ' ') {
				snprintf(said, sizeof said, "%s", line + 2);
			} else if (line[0] == 'v') {
				verdict = line[1] == '1';
			}
			line = nl + 1;
		}
		have = strlen(line);
		memmove(buf, line, have + 1);
		if (have >= sizeof buf - 1) {
			have = 0;   /* a line longer than any the child writes */
		}
	}
	close(fds[0]);
	double took = room_now() - t0;
	if (verdict == 1) {
		snprintf(why, whysz, "%s; passed in %.1f s", said[0] ? said : "built and drawn", took);
	} else if (verdict == 0) {
		snprintf(why, whysz, "%s", said[0] ? said : "it drew the room wrong");
	} else if (ended) {
		snprintf(why, whysz, "it crashed %s, %.1f s in%s%s", stage, took,
			said[0] ? "; it had said: " : "", said);
	} else if (quiet) {
		snprintf(why, whysz, "no word from it for two minutes, %s, %.0f s in; stopped",
			stage, took);
	}
	if (verdict < 0) {
		kill(pid, SIGKILL);
	}
	/* Reap it, but never wait long for it: it has said what it had to. */
	for (int i = 0; i < 200; i++) {
		pid_t r = waitpid(pid, NULL, WNOHANG);
		if (r == pid || (r < 0 && errno != EINTR)) {
			break;
		}
		struct timespec ts = { 0, 10 * 1000 * 1000 };
		nanosleep(&ts, NULL);
	}
	if (room_stopping() && verdict < 0) {
		return -1;   /* cut short: no verdict either way */
	}
	return verdict == 1 ? 1 : 0;
}

/* What the child does: the programs, and a corner of the desk through
 * them, checked against the C the way every sheet is. */
static bool room_gpu_trial_trace(void)
{
	drawer_items_build();
	sh_build();
	struct room_cam cam;
	room_cam_build(1.6, &cam);
	g_room_open = 1.0;
	g_cc_out = 0.0;
	g_pb_open = 0.0;
	g_room_layer = 1;
	struct room_ctx ctx;
	memset(&ctx, 0, sizeof ctx);
	ctx.hw = 96;
	ctx.hh = 60;
	ctx.layer = 1;
	ctx.cam = cam;
	ctx.hi = calloc((size_t)ctx.hw * ctx.hh * 4, sizeof(float));
	ctx.idhi = calloc((size_t)ctx.hw * ctx.hh, 1);
	bool ok = ctx.hi && ctx.idhi && room_trace_gpu(&ctx);
	free(ctx.hi);
	free(ctx.idhi);
	return ok;
}

__attribute__((constructor)) static void room_gpu_trial_child(void)
{
	const char *v = getenv("DESK_GPU_PROBE");
	if (!v || !*v) {
		return;
	}
	int fd = atoi(v);
	g_rgpu.trial = true;
	g_trial_fd = fd;
	bool ok = room_gpu_trial_trace();
	if (write(fd, ok ? "v1\n" : "v0\n", 3) != 3) {
		_exit(2);
	}
	_exit(ok ? 0 : 1);
}

static void room_gpu_end(void)
{
	if (g_rgpu.g) {
		desk_gpu_close(g_rgpu.g);
		g_rgpu.g = NULL;
	}
	g_rgpu.tried = false;
}

static void room_gpu_kill(const char *why)
{
	room_log("%s; the CPU traces the room from here on", why);
	g_rgpu.dead = true;
	room_gpu_end();
}

static struct desk_gpu *room_gpu(void)
{
	if (g_rgpu.dead) {
		return NULL;
	}
	if (g_rgpu.tried) {
		return g_rgpu.g;
	}
	g_rgpu.tried = true;
	double t0 = room_now();
	room_trial_stage("opening the GPU");
	struct desk_gpu *g = desk_gpu_open();
	if (!g) {
		room_log(getenv("DESK_NO_GPU") ? "DESK_NO_GPU is set; tracing on the CPU"
			: "no GPU with GLES 3.0 and a float target; tracing on the CPU");
		g_rgpu.dead = true;
		return NULL;
	}
	/* the passes carry points on the desk from one to the next, and
	 * sixteen bits of float put them a few centimetres from where they are */
	if (!desk_gpu_float32(g)) {
		room_log("%s has no 32-bit float target; tracing on the CPU", desk_gpu_name(g));
		desk_gpu_close(g);
		g_rgpu.dead = true;
		return NULL;
	}
	if (!g_rgpu.trial) {
		char why[400] = "";
		int verdict = room_gpu_verdict(g, why, sizeof why);
		bool asked = verdict < 0;
		if (asked) {
			verdict = room_gpu_trial_run(desk_gpu_node(g), why, sizeof why);
			if (room_stopping()) {
				/* the trial cut short: no verdict, and no room to trace */
				desk_gpu_close(g);
				return NULL;
			}
			if (verdict >= 0) {
				room_gpu_remember(g, verdict == 1, why);
			} else {
				/* nothing to ask: go ahead in this process, as before */
				verdict = 1;
			}
			if (verdict == 1 && why[0]) {
				room_log("%s passed its trial in a child: %s", desk_gpu_name(g), why);
			}
		}
		if (verdict == 0) {
			room_log("%s failed its trial%s: %s; tracing on the CPU", desk_gpu_name(g),
				asked ? "" : " (in an earlier session)",
				why[0] ? why : "nothing more is known (a trial from before trials said why)");
			desk_gpu_close(g);
			g_rgpu.dead = true;
			return NULL;
		}
	}
	for (int pass = 1; pass <= ROOM_PASSES; pass++) {
		if (room_stopping()) {
			desk_gpu_close(g);
			return NULL;
		}
		room_trial_stage("building program %d of %d (%.1f s in)", pass, ROOM_PASSES,
			room_now() - t0);
		struct glsl_buf b = { 0 };
		room_glsl_build(&b, pass);
		/* the program exactly as the driver is handed it, for a bug
		 * report: DESK_GPU_DUMP=/tmp/room names /tmp/room.1 to .4 */
		const char *dump = getenv("DESK_GPU_DUMP");
		if (dump && *dump && !b.bad) {
			char path[1024];
			snprintf(path, sizeof path, "%s.%d", dump, pass);
			FILE *f = fopen(path, "we");
			if (f) {
				fputs(b.s, f);
				fclose(f);
			}
		}
		char err[2048] = "";
		int prog = b.bad ? -1 : desk_gpu_add_program(g, b.s, err, sizeof err);
		free(b.s);
		if (prog < 0) {
			room_log("%s would not build the room's shader (pass %d): %.1500s",
				desk_gpu_name(g), pass, b.bad ? "out of memory" : err);
			desk_gpu_close(g);
			g_rgpu.dead = true;
			return NULL;
		}
		g_rgpu.prog[pass - 1] = prog;
	}
	room_log("tracing on %s%s%s (ready in %.1f s)", desk_gpu_name(g),
		desk_gpu_node(g)[0] ? ", " : "", desk_gpu_node(g), room_now() - t0);
	g_rgpu.g = g;
	return g;
}

static void room_gpu_uniforms_one(struct desk_gpu *g, const struct room_ctx *ctx)
{
	int frame[2] = { ctx->hw, ctx->hh };
	desk_gpu_set_ivec(g, "u_frame", frame, 2);
	const struct room_cam *c = &ctx->cam;
	const vec3 *v3s[4] = { &c->eye, &c->ww, &c->uu, &c->vv };
	static const char *const NAMES[4] = { "u_eye", "u_ww", "u_uu", "u_vv" };
	for (int i = 0; i < 4; i++) {
		float v[3] = { (float)v3s[i]->x, (float)v3s[i]->y, (float)v3s[i]->z };
		desk_gpu_set(g, NAMES[i], v, 3);
	}
	float fov[2] = { (float)c->fov_x, (float)c->fov_y };
	desk_gpu_set(g, "u_fov", fov, 2);
	desk_gpu_set_int(g, "u_layer", ctx->layer);
	const struct {
		const char *name;
		double v;
	} f[] = {
		{ "u_open", g_room_open }, { "u_cc_out", g_cc_out }, { "u_cc_lid", g_cc_lid },
		{ "u_pb_open", g_pb_open },
		{ "u_zf", g_rm_zf }, { "u_aspect", g_rm_aspect }, { "u_sh_hx", g_rm_sh_hx },
		{ "u_hx", g_rm_hx },
	};
	for (size_t i = 0; i < sizeof f / sizeof f[0]; i++) {
		float v = (float)f[i].v;
		desk_gpu_set(g, f[i].name, &v, 1);
	}
	/* the Control Panel's lid, as this sheet has it: shut, or up on its hinge (map_console) */
	float rot[2] = { (float)(g_cc_lid > 1.5 ? cos(CC_LID_SWING) : 1.0),
		(float)(g_cc_lid > 1.5 ? sin(CC_LID_SWING) : 0.0) };
	desk_gpu_set(g, "u_cc_rot", rot, 2);
	/* what the desk is made of, as the C has it (desk_wood_at) */
	int wood = atomic_load(&g_wood_bake), stain = atomic_load(&g_stain_bake);
	const struct wood *wd = &DESK_WOODS[wood];
	const struct {
		const char *name;
		vec3 v;
	} c3[] = {
		{ "u_wood_light", wd->light }, { "u_wood_dark", wd->dark }, { "u_wood_fleck", wd->fleck },
		{ "u_wood_sap", wd->sap }, { "u_wood_faded", wd->faded },
	};
	for (size_t i = 0; i < sizeof c3 / sizeof c3[0]; i++) {
		float v[3] = { (float)c3[i].v.x, (float)c3[i].v.y, (float)c3[i].v.z };
		desk_gpu_set(g, c3[i].name, v, 3);
	}
	float tone[4] = { (float)wd->tone[0], (float)wd->tone[1], (float)wd->tone[2], (float)wd->tone[3] };
	desk_gpu_set(g, "u_wood_tone", tone, 4);
	float grain[4] = { (float)wd->fleck_w, (float)wd->fleck_p, (float)wd->pore, (float)wd->rate };
	desk_gpu_set(g, "u_wood_grain", grain, 4);
	float late[2] = { (float)wd->late, (float)wd->dust };
	desk_gpu_set(g, "u_wood_late", late, 2);
	desk_gpu_set_int(g, "u_stain", stain);
}

static void room_gpu_uniforms(struct desk_gpu *g, const struct room_ctx *ctx)
{
	for (int i = 0; i < ROOM_PASSES; i++) {
		desk_gpu_use_program(g, g_rgpu.prog[i]);
		room_gpu_uniforms_one(g, ctx);
	}
}

/* One window of the sheet through room.glsl's passes, back as linear RGB
 * and coverage plus twice the owner. */
enum {
	T_HIT0, T_GEO0, T_M0A, T_M0B, T_M0C, T_M0D, T_OCC,
	T_HIT1, T_GEO1, T_M1A, T_M1B, T_M1C, T_M1D, T_OUT,
};

static bool room_gpu_window(struct desk_gpu *g, int x0, int y0, int w, int h, float *out)
{
	static const int A_OUT[] = { T_HIT0, T_GEO0 };
	static const int B0_IN[] = { T_HIT0, T_GEO0 };
	static const int B0_OUT[] = { T_M0A, T_M0B, T_M0C, T_M0D };
	static const int C_IN[] = { T_HIT0, T_GEO0, T_M0A, T_M0C, T_M0D };
	static const int C_OUT[] = { T_OCC, T_HIT1, T_GEO1 };
	static const int B1_IN[] = { T_HIT1, T_GEO1 };
	static const int B1_OUT[] = { T_M1A, T_M1B, T_M1C, T_M1D };
	static const int D_IN[] = { T_HIT0, T_M0A, T_M0B, T_M0C, T_M0D, T_OCC,
		T_HIT1, T_M1A, T_M1B, T_M1C, T_M1D };
	static const int D_OUT[] = { T_OUT };
#define N_(a) (int)(sizeof a / sizeof a[0])
	const int *P = g_rgpu.prog;
	return desk_gpu_pass(g, P[0], NULL, 0, A_OUT, N_(A_OUT), x0, y0, w, h)
		&& desk_gpu_pass(g, P[1], B0_IN, N_(B0_IN), B0_OUT, N_(B0_OUT), x0, y0, w, h)
		&& desk_gpu_pass(g, P[2], C_IN, N_(C_IN), C_OUT, N_(C_OUT), x0, y0, w, h)
		&& desk_gpu_pass(g, P[1], B1_IN, N_(B1_IN), B1_OUT, N_(B1_OUT), x0, y0, w, h)
		&& desk_gpu_pass(g, P[3], D_IN, N_(D_IN), D_OUT, N_(D_OUT), x0, y0, w, h)
		&& desk_gpu_read(g, T_OUT, w, h, out);
#undef N_
}

/* The sheet, a window at a time. A window is one draw, and a draw is what
 * a GPU's watchdog times -- run one long enough and the driver resets the
 * card, or on a VM the host resets the guest's. So windows start small and
 * grow only while each one is quick, and shrink again if one is not.
 *
 * Quick is a tenth of a second while the screen waits on the trace, and a
 * sixtieth when it does not (g_room_gentle): the GPU is very likely the
 * one the screen is drawn with, and a frame of the desk waits behind
 * whatever draw is on it. */
static bool room_gpu_tiles(struct desk_gpu *g, struct room_ctx *ctx, long *counted)
{
	const double quick = g_room_gentle ? 0.006 : 0.025, slow = g_room_gentle ? 0.016 : 0.100;
	int hw = ctx->hw, hh = ctx->hh;
	int lim = desk_gpu_max_size(g);
	int tw = lim < 512 ? lim : 512;
	/* fourteen textures a window high: 256 rows of 512 is 29 MB of them */
	int band_max = lim < 256 ? lim : 256;
	int band = band_max < 32 ? band_max : 32;
	float *tile = malloc((size_t)tw * band_max * 4 * sizeof(float));
	if (!tile) {
		return false;
	}
	for (int y0 = 0; y0 < hh;) {
		int bh = band < hh - y0 ? band : hh - y0;
		double t0 = room_now();
		int tiles = 0;
		for (int x0 = 0; x0 < hw; x0 += tw) {
			int bw = tw < hw - x0 ? tw : hw - x0;
			if (room_stopping() || !room_gpu_window(g, x0, y0, bw, bh, tile)) {
				free(tile);
				return false;
			}
			tiles++;
			for (int r = 0; r < bh; r++) {
				const float *s = tile + (size_t)r * bw * 4;
				float *d = ctx->hi + ((size_t)(y0 + r) * hw + x0) * 4;
				unsigned char *id = ctx->idhi + (size_t)(y0 + r) * hw + x0;
				for (int c = 0; c < bw; c++, s += 4, d += 4) {
					/* alpha is coverage plus twice the owner */
					int cov = s[3] >= 0.5f;
					int own = cov ? (int)((s[3] - 1.0f) * 0.5f + 0.5f) : 0;
					d[0] = s[0];
					d[1] = s[1];
					d[2] = s[2];
					d[3] = (float)cov;
					id[c] = (unsigned char)(own < 0 ? 0 : own > 255 ? 255 : own);
				}
			}
		}
		atomic_fetch_add_explicit(&g_bake_rows, bh, memory_order_relaxed);
		*counted += bh;
		y0 += bh;
		double each = (room_now() - t0) / tiles;
		if (each < quick && band * 2 <= band_max) {
			band *= 2;
		} else if (each > slow && band > 8) {
			band /= 2;
		}
	}
	free(tile);
	return true;
}

/* Whether the GPU drew the room this file would have.
 *
 * A couple of hundred samples spread over the sheet are traced again here,
 * in double, and compared. Float against double does not agree to the bit,
 * and a sample on a silhouette or the edge of a scratch can land either
 * side of it, so what is looked for is not equality but the absence of
 * disagreement: nearly every sample within a few per cent. A driver that
 * gets a program this size wrong does not get it wrong by a few per cent.
 *
 * Anything non-finite that came back is traced again here too, if there is
 * little enough of it; a sheet full of it is a sheet from a broken GPU. */
static bool room_gpu_agrees(struct room_ctx *ctx, char *why, size_t whysz)
{
	int hw = ctx->hw, hh = ctx->hh;
	size_t total = (size_t)hw * hh;
	size_t nonfinite = 0;
	for (size_t i = 0; i < total; i++) {
		float *s = ctx->hi + i * 4;
		if (isfinite(s[0]) && isfinite(s[1]) && isfinite(s[2])) {
			continue;
		}
		if (++nonfinite > total / 1000 + 16) {
			snprintf(why, whysz, "it drew non-finite colours");
			return false;
		}
		int x = (int)(i % (size_t)hw), y = (int)(i / (size_t)hw);
		ctx->idhi[i] = (unsigned char)room_sample(&ctx->cam, ctx->layer, hw, hh, x, y, s);
	}

	enum { GX = 16, GY = 12 };
	int off = 0;
	for (int gy = 0; gy < GY; gy++) {
		for (int gx = 0; gx < GX; gx++) {
			uint32_t r = room_mix((uint32_t)(gx * 131 + gy * 7919 + ctx->layer * 104729));
			int x = (int)((gx + (r & 0xffffu) / 65536.0) / GX * hw);
			int y = (int)((gy + (r >> 16) / 65536.0) / GY * hh);
			x = x < hw ? x : hw - 1;
			y = y < hh ? y : hh - 1;
			float ref[4];
			int own = room_sample(&ctx->cam, ctx->layer, hw, hh, x, y, ref);
			const float *s = ctx->hi + ((size_t)y * hw + x) * 4;
			bool same = (s[3] >= 0.5f) == (ref[3] >= 0.5f)
				&& ctx->idhi[(size_t)y * hw + x] == own;
			for (int c = 0; same && c < 3; c++) {
				float a = fabsf(ref[c]), bb = fabsf(s[c]);
				if (fabsf(s[c] - ref[c]) > 0.01f + 0.04f * (a > bb ? a : bb)) {
					same = false;
				}
			}
			off += !same;
		}
	}
	if (off * 10 > GX * GY) {
		snprintf(why, whysz, "%d of %d samples it drew disagree with the C tracer",
			off, GX * GY);
		return false;
	}
	return true;
}

/* The layers (1 the desk, 2 the drawer, 3 the bookcase, 4 the console, 5
 * the post box) the C did not agree with the GPU about. Such a layer is
 * traced on the processor from then on -- its sheet and its film -- and the
 * GPU kept for the rest: on a driver whose float runs a little its own way,
 * a view full of fine print can fall out of agreement where the desk did
 * not, and putting the GPU down for good on the first such sheet left
 * every view and film after it to the processor, the minutes a first start
 * spent at SHELF. A second layer out of agreement is the GPU's doing, and
 * it is put down; so is a draw that fails. Only the thread that traces
 * touches it (under g_render_lock). */
static unsigned g_rgpu_bad_layers;

static bool room_trace_gpu(struct room_ctx *ctx)
{
	if (g_rgpu_bad_layers & (1u << ctx->layer)) {
		return false;
	}
	struct desk_gpu *g = room_gpu();
	if (!g) {
		return false;
	}
	room_gpu_uniforms(g, ctx);
	long counted = 0;
	char why[160] = "";
	double t0 = room_now();
	room_trial_stage("drawing the corner of the desk");
	bool ok = room_gpu_tiles(g, ctx, &counted);
	room_trial_stage("checking it against the C (drawn in %.1f s)", room_now() - t0);
	if (!ok && room_stopping()) {
		/* not the GPU's doing: the session is ending */
		return false;
	}
	if (!ok) {
		snprintf(why, sizeof why, "%s failed a draw", desk_gpu_name(g));
		atomic_fetch_sub_explicit(&g_bake_rows, counted, memory_order_relaxed);
		room_gpu_kill(why);
		return false;
	}
	if (!room_gpu_agrees(ctx, why, sizeof why)) {
		/* the C will count these rows again as it traces them */
		atomic_fetch_sub_explicit(&g_bake_rows, counted, memory_order_relaxed);
		g_rgpu_bad_layers |= 1u << ctx->layer;
		if (g_rgpu.trial) {
			room_log("%s", why);   /* the trial's child: that is its answer */
		} else if (__builtin_popcount(g_rgpu_bad_layers) >= 2) {
			room_gpu_kill(why);
		} else {
			room_log("%s, in layer %d; that layer is traced on the CPU, the rest still "
				"on the GPU", why, ctx->layer);
		}
		return false;
	}
	return true;
}
#endif

/* ---- the room laid from pictures of its faces ----------------------
 *
 * The views and the films of the moves are traced, and on a machine with
 * no GPU that is minutes: the bookcase took seventeen at Low on one of
 * 2010. Most of what is in them is flat -- the boards and the back of the
 * bookcase, its sides, the binders' spines, the wall, the top of the desk
 * -- and a flat face can be traced once, as a picture of itself, and laid
 * through any camera after that, a sample at a time, for the price of a
 * plane and a lookup. The pictures are made ahead (desk-gputest
 * --faces-bake) and ship with the desk, a pack a view; the room's own
 * lamp, which moves with the width of the desk, is taken out of them and
 * put back for the room of this screen (rf_light). Whatever no face covers
 * -- the things on the desk and the board, the console, the post box --
 * is traced, as it always was. */

#define RF_LEVELS 10
enum { RF_REST, RF_LAMP, RF_REST0, RF_LAMP0, RF_PICS };
#define RF_MAX 256
#define RF_TPU_MAX 72.0   /* texels to the unit, at most */
#define RF_DESK_SPLIT (-2.0)   /* where the desk's top is cut in two, in z */

struct rface {
	vec3 o, u, v, n;   /* a corner, the edges from it, and outward */
	double out, in;    /* how far off its plane its surface may stand */
	int tw[RF_LEVELS], th[RF_LEVELS], lv;
	/* Its pictures (RF_REST ...), a level each, 8 bits a channel: a, how
	 * much of the texel the face is there in, and r g b to a 2.2 power of
	 * linear light times scale, not multiplied by a. Lit by everything but
	 * the room's lamp; what the lamp adds, at LIT_KEY_IRRADIANCE and in
	 * nothing's shadow; and the same two as seen from where the move
	 * starts, for a face seen from there too -- the sheen on it is not
	 * where it is from the end. */
	uint32_t *pic[RF_PICS][RF_LEVELS];
	float scale[RF_PICS];
	/* the finest level each picture has: the lamp's half the size of the
	 * rest, which carries the grain; the start of the move's a quarter,
	 * being seen only in the move's film */
	int base[RF_PICS];
	double tpu_max;          /* texels to the unit, at most */
	double fine;             /* and as many more as the cameras need, times */
	double lfine;            /* its light worked out as finely as the room's
	                          * is, times (rf_light, rf_fix) */
	bool two;                /* pictured from the start of the move too */
	/* A face whose surroundings change with the shape of the room -- the
	 * wall a narrower case leaves bare, the desk the console and the post
	 * box stand on at other places, the lamp's sheen on it seen from
	 * elsewhere -- is put right for this room: on fw by fh points, what
	 * this room's light makes of it over what its pictures and the lamp
	 * worked out for it do, smoothly, from the end of the move and from
	 * its start. */
	bool live;
	bool relief;             /* things stand up off it (g_rf_relief) */
	double tall;             /* and what stands further off it is traced */
	/* and how far, at each texel of the first level, between hmin and
	 * hmax off the face: what a ray is followed down through as it is laid
	 * (rf_hit_relief), so that a raised plate is seen where it stands */
	float *height;
	float hmin, hmax;
	int group;               /* what moves it (RF_G_...), and where that had */
	double pose;             /* it when it was put where it is */
	double turn;             /* how far it is turned about x through at: the
	                          * post box's door, swung on its hinge */
	bool cut;                /* nothing of it past the desk's far edge (the
	                          * console stowed, the back of the post box) */
	bool own_ok;             /* its picture's owners are this view's */
	float *fix[2];
	int fw, fh;
	vec3 lamp;               /* where the room's lamp hung when it was pictured */
	unsigned char *own;      /* whose each texel is, at the first level */
	const char *what;
	bool trace;              /* no picture: what is behind it is traced */
	bool clear;              /* nor anything: what is behind it is not in
	                          * the sheet (the desk's top over the back of
	                          * the drawer, in the drawer's) */
	vec3 at;                 /* where the thing it is part of stands */
	int nth;                 /* in a pack: which face of its name it is */
	/* the room's lamp on it (rf_lamp), on lw by lh points from corner to
	 * corner of it: worked out once for the room of this shape */
	float *light;
	int lw, lh;
	/* how many of those points each is from one at a place a view or a
	 * move to it shows (rf_mark_views): only the near are worked out in
	 * full. NULL, all of them. */
	unsigned char *seen;
	/* and the lamp and the putting right as they are with what moves all
	 * the way in, where the room's light changes as it comes out (the
	 * console's shadow on the desk): mixed by how far out it is */
	float *light_in, *fix_in[2];
	const struct rface *src; /* the picture is that face's, src->at - at off */
};

struct rface_set {
	struct rface f[RF_MAX];
	int n;
	int moves;         /* the faces have their light with what moves in too:
	                    * the layer whose thing moves (4 the console, 5 the
	                    * post box's door), or 0 */
	vec3 eye0, eye1;   /* where the move starts and ends: the pictures with
	                    * the sheen as seen from each */
	char names[RF_MAX][16];   /* a pack's faces' names, which what points at */
};

enum { RF_PX = 1, RF_NX = 2, RF_PY = 4, RF_NY = 8, RF_PZ = 16, RF_NZ = 32 };

/* where the thing the faces being added are part of stands: what moves
 * with it when the room changes shape; how finely they need picturing at
 * most; and whether from the start of the move too */
static vec3 g_rf_at;
static double g_rf_tpu = RF_TPU_MAX;
/* how much finer than the cameras need the faces being added are pictured:
 * the wire round a tent's ridge is two texels thick at the cameras' own */
static double g_rf_fine = 1.0;
/* and their light, how much more coarsely than the room's (the floor round
 * the desk, a long way down, seen only past the drawer) */
static double g_rf_lfine = 1.0;
static bool g_rf_two, g_rf_live, g_rf_cut, g_rf_clear;
/* whether things stand up off the faces being added: what stands more
 * than RF_TALL off one is traced, and round it as far as it could be seen
 * across the face from the side (RF_TALL_REACH times its height) */
static bool g_rf_relief;
/* what moves the faces being added: the console's slide, the post box's
 * door */
enum { RF_G_NONE, RF_G_CONSOLE, RF_G_DOOR };
#define RF_DOOR_SWING 1.78   /* as map_post swings it, all the way open */
static int g_rf_group;
#define RF_TALL 0.18
#define RF_TALL_REACH 1.30   /* times how tall it is */
/* how tall, for the faces being added: the console's controls stand off
 * its faces up to half a unit, and are laid -- what of them is seen from
 * the side is met as a wall (RF_WALL) and traced */
static double g_rf_tall = RF_TALL;
/* A height field met where it is steeper than this is met at a wall of it
 * (rf_hit_relief): what another face shows no further than RF_WALL_GAP
 * behind it is what is there, and if none does, it is traced. */
#define RF_WALL 1.5
#define RF_WALL_GAP 0.10
#define RF_OWN_TALL 255
/* the finest level of the lamp's picture: half the size of the rest's,
 * but for a face whose grain the lamp is what shows (the desk's top) */
static int g_rf_lamp_base = 1;
