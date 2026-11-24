/* Part of draw.c: sitting up to look at the shelf, and the views laid from faces.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- sitting up to look at the shelf --------------------------- */

/* The move from the desk to the bookcase is a real camera move: the eye
 * lifts and comes forward while the view swings up off the desk. No affine
 * transform of a plan-view sheet can do that, and a cross fade between the
 * two ends reads as a dissolve rather than as a head coming up -- which is
 * exactly the complaint this feature started with.
 *
 * So it is traced, as a flipbook. The frames are quarter resolution, which
 * is invisible on something that crosses the screen in a third of a second
 * and costs a sixteenth of what a full frame would (ROOM_TILT_DIV, the
 * level's: a half at Ultra, none at all in 2D). */
#define ROOM_TILT_FRAMES 18

/* The film runs a little past both ends of the move.
 *
 * A head that turns to look at something does not stop dead on the thing:
 * it leans back a hair before it goes, arrives a little early, and settles
 * back. Both of those want frames on the other side of where the move
 * finishes, and the cheapest place to find them is the film -- eighteen
 * keyframes spread over slightly more than the move instead of exactly it.
 * It costs nothing: the same eighteen traces, six per cent further apart. */
#define ROOM_TILT_U0 (-0.050)
#define ROOM_TILT_U1   1.060

static void room_cam_lerp(double aspect, double t, int which, struct room_cam *c)
{
	struct room_cam plan, dst;
	/* solve the destination first: it is the one that sizes the bookcase,
	 * which must not change width halfway through a move */
	if (which == DESK_VIEW_CONSOLE) {
		room_cam_console(aspect, &dst);
	} else if (which == DESK_VIEW_POST) {
		room_cam_post(aspect, &dst);
	} else {
		room_cam_shelf(aspect, &dst);
	}
	room_cam_build(aspect, &plan);

	/* Uniform in the camera's own parameter. The easing belongs at
	 * playback, where it can be applied to a fractional frame index; easing
	 * the keyframe spacing as well just bends the curve twice.
	 *
	 * Allowed a little outside the move at both ends, so there is somewhere
	 * for a head to lean back to and somewhere to arrive early at. */
	double e = clampf(t, ROOM_TILT_U0, ROOM_TILT_U1);
	double phi = dst.pitch * e;
	c->fov_x = plan.fov_x;
	c->fov_y = plan.fov_y;
	c->pitch = phi;
	c->eye = vmix(plan.eye, dst.eye, e);
	c->ww = v3(0.0, -cos(phi), -sin(phi));
	c->uu = v3(1, 0, 0);
	c->vv = vcross(c->uu, c->ww);
	c->z0 = plan.z0;
	c->z1 = plan.z1;
}

/* What moves, as it stands at u of the move to a view. The set comes out
 * ahead of the camera rather than with it. It has to be clear of the
 * desk's back edge before the camera has tilted far enough to see along
 * that edge, and it is better theatre besides: the machine arrives, and
 * then you are looking at it. The post box's door comes down as you
 * arrive, not before. */
static void room_film_pose(int which, double u)
{
	if (which == DESK_VIEW_CONSOLE) {
		g_cc_out = smoothstepf(0.02, 0.46, u);
	}
	if (which == DESK_VIEW_POST) {
		g_pb_open = smoothstepf(0.42, 0.96, u);
	}
}

static cairo_surface_t *room_bake_film(int w, int h, int which)
{
	if (ROOM_TILT_DIV <= 0) {
		return NULL;   /* the level cuts to the view */
	}
	int fw = w / ROOM_TILT_DIV, fh = h / ROOM_TILT_DIV;
	if (fw < 16 || fh < 16) {
		return NULL;
	}
	cairo_surface_t *strip = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		fw, fh * ROOM_TILT_FRAMES);
	cairo_t *cr = cairo_create(strip);
	for (int i = 0; i < ROOM_TILT_FRAMES; i++) {
		if (room_stopping() || room_superseded()) {
			break;   /* not wanted now: the caller lets it go */
		}
		struct room_cam cam;
		/* the ends are keyframes too, so there is nothing to jump over at
		 * either end of the move */
		room_cam_lerp((double)w / (double)h,
			ROOM_TILT_U0 + (ROOM_TILT_U1 - ROOM_TILT_U0)
				* (double)i / (double)(ROOM_TILT_FRAMES - 1), which, &cam);
		/* The move is traced in the layer it ends in, so the last frame is
		 * the sheet it hands over to. The drawer is shut throughout: you
		 * push it in before you look away from the desk. */
		g_room_open = 0.0;
		room_film_pose(which, ROOM_TILT_U0 + (ROOM_TILT_U1 - ROOM_TILT_U0)
			* (double)i / (double)(ROOM_TILT_FRAMES - 1));
		int layer = (which == DESK_VIEW_CONSOLE) ? 4
			: (which == DESK_VIEW_POST) ? 5 : 3;
		cairo_surface_t *f = room_bake(layer, fw, fh, &cam, NULL);
		cairo_set_source_surface(cr, f, 0, (double)i * fh);
		cairo_paint(cr);
		cairo_surface_destroy(f);
	}
	cairo_destroy(cr);
	cairo_surface_flush(strip);
	return strip;
}

/* ---- the views laid from faces, where the room is traced on the CPU ---- */

/* Each view's pack of faces, read when it is first wanted and let go when
 * the room is made (rf_release); and the faces of the room of this shape,
 * linked to it and lit. Only the thread making the room touches them.
 * Indexed by layer: 1 the desk (and 2 its drawer, from the same pack), 3
 * the bookcase, 4 the console, 5 the post box. */
#define RF_LAYERS 6
static struct rface_set *g_rf_pack[RF_LAYERS];
static struct rface_set *g_rf_room[RF_LAYERS];
static double g_rf_room_aspect[RF_LAYERS], g_rf_room_hx[RF_LAYERS];
static _Atomic int g_rf_off = -1;    /* DESK_ROOM_FACES=off */
static bool g_rf_bad[RF_LAYERS];   /* a pack that would not read */

/* A view's packs, a desk width each: the console's brass shows the lamp,
 * which moves out and up with a wider desk, so it is pictured for three
 * widths (half widths 16, 21.3 and 28.4 -- screens of 1440, 1920 and 2560
 * points) and the nearest is laid. The bookcase's has the desk, the wall
 * and the floor every view lays, put right for each. Files of DEsk's data
 * (rf_pack_load), not built in: four programs link the drawing, and each
 * carried all thirty megabytes of them. */
#define RF_WIDTHS 3
#define RF_VIEW_DESK (-1)   /* the desk's own view, straight down */
static const struct {
	const char *what;
	int which;
	struct {
		const char *file;
		double hx;
	} at[RF_WIDTHS];
} RF_PACK[RF_LAYERS] = {
	[1] = { "the desk's", RF_VIEW_DESK, { { "plan-faces.bin", 0.0 } } },
	[2] = { "the drawer's", RF_VIEW_DESK, { { "plan-faces.bin", 0.0 } } },
	[3] = { "the bookcase's", DESK_VIEW_SHELF, { { "shelf-faces.bin", 0.0 } } },
	[4] = { "the console's", DESK_VIEW_CONSOLE, {
		{ "console-faces-16.bin", 16.0 },
		{ "console-faces-21.bin", 21.33 },
		{ "console-faces-28.bin", 28.44 } } },
	[5] = { "the post box's", DESK_VIEW_POST, {
		{ "post-faces-16.bin", 16.0 },
		{ "post-faces-21.bin", 21.33 },
		{ "post-faces-28.bin", 28.44 } } },
};
static int g_rf_pack_at[RF_LAYERS];   /* which of a view's packs is read */

/* Where DEsk's pictures of a kind -- "room", the views' faces; "clock",
 * "post", "console", the things on the desk; "camera" -- are looked for,
 * in order: with DEsk's data beside the prefix its binary is in
 * (bin/../share/desk/<kind>); else, run from where it was built, the
 * source's art/<kind> over the build folder -- before any DEsk installed
 * on the machine, whose may be older; else the data of the prefix it was
 * built for. DESK_ART_DIR=<a source's art>: there only (a build made
 * outside the source). How many there are. */
#define RF_DIR_LEN 1100
static int desk_art_dirs(const char *kind, char dirs[3][RF_DIR_LEN])
{
	const char *env = getenv("DESK_ART_DIR");
	if (env && *env) {
		snprintf(dirs[0], RF_DIR_LEN, "%.1000s/%s", env, kind);
		return 1;
	}
	char exe[1024] = "";
	ssize_t k = readlink("/proc/self/exe", exe, sizeof exe - 1);
	exe[k > 0 ? k : 0] = '\0';
	char *slash = strrchr(exe, '/');
	if (slash) {
		*slash = '\0';
	}
	int nd = 0;
	if (exe[0]) {
		snprintf(dirs[nd++], RF_DIR_LEN, "%s/../share/desk/%s", exe, kind);
		snprintf(dirs[nd++], RF_DIR_LEN, "%s/../art/%s", exe, kind);
	}
#ifdef DESK_DATADIR
	snprintf(dirs[nd++], RF_DIR_LEN, "%s/desk/%s", DESK_DATADIR, kind);
#endif
	return nd;
}

/* The first of those that has file, into path; false, and path the
 * first place looked, when none has. */
bool desk_art_find(const char *kind, const char *file, char *path, size_t n)
{
	char dirs[3][RF_DIR_LEN];
	int nd = desk_art_dirs(kind, dirs);
	for (int i = 0; i < nd; i++) {
		if ((size_t)snprintf(path, n, "%s/%s", dirs[i], file) < n && access(path, R_OK) == 0) {
			return true;
		}
	}
	snprintf(path, n, "%s/%s", nd ? dirs[0] : ".", file);
	return false;
}

/* Where the packs are looked for: DESK_ROOM_FACES_DIR if that is set, else
 * where the room's pictures are (desk_art_dirs). */
static int rf_pack_dirs(char dirs[3][RF_DIR_LEN])
{
	const char *env = getenv("DESK_ROOM_FACES_DIR");
	if (env && *env) {
		snprintf(dirs[0], RF_DIR_LEN, "%s", env);
		return 1;
	}
	return desk_art_dirs("room", dirs);
}

/* A pack's file, all of it, into memory (*len bytes; free it), and where it
 * was in where (rf_pack_dirs). NULL when it is not there. */
static unsigned char *rf_pack_load(const char *file, size_t *len, char *where, size_t n)
{
	char dirs[3][RF_DIR_LEN];
	int nd = rf_pack_dirs(dirs);
	for (int i = 0; i < nd; i++) {
		FILE *f = (size_t)snprintf(where, n, "%s/%s", dirs[i], file) < n ? fopen(where, "rbe")
			: NULL;
		if (!f) {
			continue;
		}
		unsigned char *buf = NULL;
		long size = 0;
		if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0
			&& (buf = malloc((size_t)size)) && fread(buf, 1, (size_t)size, f) != (size_t)size) {
			free(buf);
			buf = NULL;
		}
		fclose(f);
		if (buf) {
			*len = (size_t)size;
			return buf;
		}
	}
	snprintf(where, n, "%s", nd ? dirs[0] : "");
	return NULL;
}

/* Whether the packs the desk and the bookcase are laid from are there:
 * looked for once, from any thread (room_level_cap). */
static _Atomic int g_rf_there = -1;
static bool rf_packs_there(void)
{
	int there = atomic_load(&g_rf_there);
	if (there < 0) {
		static const char *const FILE_OF[2] = { "plan-faces.bin", "shelf-faces.bin" };
		char dirs[3][RF_DIR_LEN], path[RF_DIR_LEN + 32];
		int nd = rf_pack_dirs(dirs);
		there = 1;
		for (int k = 0; k < 2; k++) {
			bool found = false;
			for (int i = 0; i < nd && !found; i++) {
				found = (size_t)snprintf(path, sizeof path, "%s/%s", dirs[i], FILE_OF[k])
					< sizeof path && access(path, R_OK) == 0;
			}
			there = there && found;
		}
		atomic_store(&g_rf_there, there);
	}
	return there;
}

static bool room_traces_on_cpu(void);

/* whether faces are not to be laid at all (DESK_ROOM_FACES=off); asked
 * from the compositor's thread too (room_level_cap) */
static bool rf_switched_off(void)
{
	int off = atomic_load(&g_rf_off);
	if (off < 0) {
		const char *e = getenv("DESK_ROOM_FACES");
		off = e && !strcmp(e, "off");
		atomic_store(&g_rf_off, off);
	}
	return off;
}

static bool rf_wanted(void)
{
	return !rf_switched_off() && room_traces_on_cpu();
}

/* the camera of a view */
static void rf_view_cam(int which, double aspect, struct room_cam *c)
{
	if (which == RF_VIEW_DESK) {
		room_cam_build(aspect, c);
		return;
	}
	/* the bookcase's sizes the case, and the wall stands behind where it
	 * would be in every view */
	room_cam_shelf(aspect, c);
	if (which == DESK_VIEW_CONSOLE) {
		room_cam_console(aspect, c);
	} else if (which == DESK_VIEW_POST) {
		room_cam_post(aspect, c);
	}
}

/* How far each of w by h points is from a 0 among them, in points along,
 * down or corner to corner, up to 255: in place, forward and back. */
static void rf_reach(unsigned char *d, int w, int h)
{
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int v = d[y * w + x];
			if (x > 0 && d[y * w + x - 1] + 1 < v) {
				v = d[y * w + x - 1] + 1;
			}
			for (int k = -1; y > 0 && k <= 1; k++) {
				if (x + k >= 0 && x + k < w && d[(y - 1) * w + x + k] + 1 < v) {
					v = d[(y - 1) * w + x + k] + 1;
				}
			}
			d[y * w + x] = (unsigned char)v;
		}
	}
	for (int y = h - 1; y >= 0; y--) {
		for (int x = w - 1; x >= 0; x--) {
			int v = d[y * w + x];
			if (x < w - 1 && d[y * w + x + 1] + 1 < v) {
				v = d[y * w + x + 1] + 1;
			}
			for (int k = -1; y < h - 1 && k <= 1; k++) {
				if (x + k >= 0 && x + k < w && d[(y + 1) * w + x + k] + 1 < v) {
					v = d[(y + 1) * w + x + k] + 1;
				}
			}
			d[y * w + x] = (unsigned char)v;
		}
	}
}

/* Which of each face's light points (tpu to the unit, as rf_light has
 * them) a view and the move to it will look near: the faces laid small
 * through the view's camera and each frame of the move's, with what moves
 * where it is then, and the place each sample finds marked on the face
 * that wins it; then how far every other point is from a marked one
 * (f->seen). What none of them shows -- most of the floor, the wall behind
 * the camera, the desk under the console -- is lit by the lamp's pool
 * alone, without the shadow that is most of the cost. */
#define RF_MARK_W 320   /* samples across, for the view; half for a frame */
static void rf_mark_views(struct rface_set *s, int layer, double tpu)
{
	double aspect = g_rm_aspect;
	int which = RF_PACK[layer].which;
	struct room_cam cams[1 + ROOM_TILT_FRAMES];
	double along[1 + ROOM_TILT_FRAMES];
	int ncam = 1;
	rf_view_cam(which, aspect, &cams[0]);
	for (int i = 0; which != RF_VIEW_DESK && ROOM_TILT_DIV > 0 && i < ROOM_TILT_FRAMES; i++) {
		along[ncam] = ROOM_TILT_U0 + (ROOM_TILT_U1 - ROOM_TILT_U0) * i / (ROOM_TILT_FRAMES - 1);
		room_cam_lerp(aspect, along[ncam], which, &cams[ncam]);
		ncam++;
	}
	for (int i = 0; i < s->n; i++) {
		struct rface *f = &s->f[i];
		free(f->seen);
		f->seen = NULL;
		if (f->trace || (!f->lv && !f->src)) {
			continue;
		}
		rf_light_size(f, tpu);
		f->seen = malloc((size_t)f->lw * f->lh);
		if (f->seen) {
			memset(f->seen, 255, (size_t)f->lw * f->lh);
		}
	}
	int mw = RF_MARK_W, mh = (int)lround(RF_MARK_W / aspect);
	float *mark = malloc((size_t)mw * mh * 3 * sizeof(float));
	unsigned char *need = malloc((size_t)mw * mh);
	double cc = g_cc_out, pb = g_pb_open;
	for (int c = 0; mark && need && c < ncam; c++) {
		int w = c ? mw / 2 : mw, h = c ? mh / 2 : mh;
		if (c == 0) {
			g_cc_out = layer == 4 ? 1.0 : 0.0;
			g_pb_open = layer == 5 ? 1.0 : 0.0;
		} else {
			room_film_pose(which, along[c]);
		}
		rf_pose(s);
		struct room_ctx ctx = { 0 };
		ctx.hw = w;
		ctx.hh = h;
		ctx.cam = cams[c];
		rf_lay_into(s, &ctx, need, mark, NULL, NULL, false);
		for (int k = 0; k < w * h; k++) {
			int i = (int)mark[k * 3] - 1;
			struct rface *f = i >= 0 && i < s->n ? &s->f[i] : NULL;
			if (f && f->seen) {
				int x = (int)lround(clampf(mark[k * 3 + 1], 0.0, 1.0) * (f->lw - 1));
				int y = (int)lround(clampf(mark[k * 3 + 2], 0.0, 1.0) * (f->lh - 1));
				f->seen[(size_t)y * f->lw + x] = 0;
			}
		}
	}
	g_cc_out = cc;
	g_pb_open = pb;
	rf_pose(s);
	for (int i = 0; i < s->n; i++) {
		struct rface *f = &s->f[i];
		if (f->seen && (!mark || !need)) {
			free(f->seen);   /* nothing marked: all of it */
			f->seen = NULL;
		}
		if (f->seen) {
			rf_reach(f->seen, f->lw, f->lh);
		}
	}
	free(mark);
	free(need);
}

/* The room's light on a view's faces, linked to their pack: the lamp, and
 * the faces whose surroundings change put right; for the console's, with
 * it stowed as well as out, mixed as it comes out (its shadow on the desk
 * goes with it) -- near what the view and the move to it show. How many
 * points of each were worked out, into n and m. */
static void rf_room_light(struct rface_set *s, int layer, long *n, long *m)
{
	rf_mark_views(s, layer, 4.0);
	if (layer == 4 || layer == 5) {
		/* the console comes out, the post box's door comes down */
		double *what = layer == 4 ? &g_cc_out : &g_pb_open;
		double keep = *what;
		*what = 0.0;
		rf_pose(s);
		*n += rf_light(s, 4.0);
		*m += rf_fix(s, 3.0);
		for (int i = 0; i < s->n; i++) {
			struct rface *f = &s->f[i];
			f->light_in = f->light;
			f->fix_in[0] = f->fix[0];
			f->fix_in[1] = f->fix[1];
			f->light = f->fix[0] = f->fix[1] = NULL;
		}
		*what = 1.0;
		rf_pose(s);
		s->moves = layer;
		*n += rf_light(s, 4.0);
		*m += rf_fix(s, 3.0);
		*what = keep;
		rf_pose(s);
	} else {
		*n += rf_light(s, 4.0);
		*m += rf_fix(s, 3.0);
	}
	for (int i = 0; i < s->n; i++) {
		free(s->f[i].seen);
		s->f[i].seen = NULL;
	}
}

/* A view's pack, read the first time; NULL when it has none, it is not
 * there or it would not read (and from then on). */
static struct rface_set *rf_pack_get(int layer)
{
	if (layer == 2) {
		layer = 1;   /* the drawer's faces are in the desk's pack */
	}
	if (layer < 0 || layer >= RF_LAYERS || !RF_PACK[layer].at[0].file || g_rf_bad[layer]) {
		return NULL;
	}
	/* the one pictured for the nearest desk */
	int at = 0;
	for (int i = 1; i < RF_WIDTHS && RF_PACK[layer].at[i].file; i++) {
		if (fabs(RF_PACK[layer].at[i].hx - g_rm_hx) < fabs(RF_PACK[layer].at[at].hx - g_rm_hx)) {
			at = i;
		}
	}
	if (g_rf_pack[layer] && g_rf_pack_at[layer] != at) {
		if (g_rf_room[layer]) {
			rf_free(g_rf_room[layer]);
			free(g_rf_room[layer]);
			g_rf_room[layer] = NULL;
		}
		rf_free(g_rf_pack[layer]);
		free(g_rf_pack[layer]);
		g_rf_pack[layer] = NULL;
	}
	if (!g_rf_pack[layer]) {
		double t0 = room_now();
		g_rf_pack_at[layer] = at;
		char where[1200];
		size_t len = 0;
		unsigned char *data = rf_pack_load(RF_PACK[layer].at[at].file, &len, where, sizeof where);
		if (!data) {
			room_log("%s faces are not there (%s, looked for first in %s): it is traced",
				RF_PACK[layer].what, RF_PACK[layer].at[at].file, where);
			g_rf_bad[layer] = true;
			return NULL;
		}
		g_rf_pack[layer] = rf_read(data, len);
		free(data);
		if (!g_rf_pack[layer] || !g_rf_pack[layer]->n) {
			if (g_rf_pack[layer]) {
				rf_free(g_rf_pack[layer]);
				free(g_rf_pack[layer]);
				g_rf_pack[layer] = NULL;
			} else {
				room_log("%s faces would not read (%s): it is traced", RF_PACK[layer].what,
					where);
			}
			g_rf_bad[layer] = true;
			return NULL;
		}
		room_log("%s faces read in %.2f s", RF_PACK[layer].what, room_now() - t0);
	}
	return g_rf_pack[layer];
}

/* A view's faces in the room as the globals have it, linked and lit, and
 * what moves put where it is now; NULL when they are not to be laid. */
static struct rface_set *rf_room_get(int layer)
{
	if (layer < 0 || layer >= RF_LAYERS || !RF_PACK[layer].at[0].file || g_rf_bad[layer]
		|| !rf_wanted()) {
		return NULL;
	}
	if (!rf_pack_get(layer)) {
		return NULL;
	}
	double aspect = g_rm_aspect, hx = g_rm_hx;
	struct rface_set *s = g_rf_room[layer];
	if (!s || g_rf_room_aspect[layer] != aspect || g_rf_room_hx[layer] != hx) {
		if (s) {
			rf_free(s);
			free(s);
			g_rf_room[layer] = NULL;
		}
		s = calloc(1, sizeof *s);
		if (!s) {
			return NULL;
		}
		double t0 = room_now();
		/* where the move to it starts and where it ends, for the sheen; the
		 * view's camera last, which is what sizes the bookcase */
		struct room_cam from, to;
		int which = RF_PACK[layer].which;
		if (which != RF_VIEW_DESK) {
			room_cam_lerp(aspect, ROOM_TILT_U0, which, &from);
		}
		/* the bookcase's camera sizes the case, and the wall stands behind
		 * where it would be in every view */
		rf_view_cam(which, aspect, &to);
		if (which == RF_VIEW_DESK) {
			from = to;   /* it does not move */
		}
		rf_layer(s, layer);
		/* The desk is the same desk in every view: the bookcase's pack has
		 * it, from the start of every move and from up at the bookcase,
		 * and put right for this view's eye like any live face. The
		 * console and the post box are their own packs', for the width of
		 * desk nearest this one. The desk's own view is its own pack's. */
		const struct rface_set *packs[3] = { rf_pack_get(layer), NULL, NULL };
		int np = 1;
		for (int l = 3; l <= 5 && layer >= 3; l++) {
			if (l != layer) {
				packs[np++] = rf_pack_get(l);
			}
		}
		rf_link(s, packs, np);
		s->eye0 = from.eye;
		s->eye1 = to.eye;
		long n = 0, m = 0;
		rf_room_light(s, layer, &n, &m);
		room_log("%s faces for a room %.2f to 1, desk %.1f wide: lit at %ld points, "
			"put right at %ld, in %.2f s", RF_PACK[layer].what, aspect, 2.0 * hx, n, m,
			room_now() - t0);
		g_rf_room[layer] = s;
		g_rf_room_aspect[layer] = aspect;
		g_rf_room_hx[layer] = hx;
	}
	rf_pose(s);
	return s;
}

/* A sheet laid from faces needs its four samples a pixel only where one
 * thing meets another. Inside a face it does not: its pictures are
 * filtered to the pixel already (rf_sample's level), and one sample in the
 * middle is what four would come to. Where faces meet, or what is laid
 * meets what is traced, a sample a pixel is a stair. So the sheet is laid
 * at a sample a pixel first, and what no face covers traced -- at the
 * first of each pixel's own samples, which it keeps; a pixel at an edge
 * is laid again at all its samples, and the rest have their one copied
 * into all of theirs. need: the samples left to trace. False, nothing
 * done, where it is a sample a pixel anyway, or there was not the memory
 * for it (DESK_FACES_FULL=1: every sample laid, as it was).
 *
 * A trace is a hundred times a lay (on a machine of 2010 the console's
 * sides took a fifth of a millisecond a sample), so only an edge that
 * shows is traced again: one where the two differ on screen by more than
 * RF_EDGE levels. And at Low (edge_traced) only on the pixel's diagonal:
 * of its four, two traced, where four are laid. */
#define RF_EDGE 8

struct rf_sparse_arg {
	const struct room_ctx *c;     /* the sheet at a sample a pixel */
	const unsigned char *need;    /* which of its samples were traced */
	const unsigned short *won;    /* and which face won each */
	unsigned char *seen;          /* each pixel as it is on screen, RGBA */
	bool flat[RF_MAX + 1];        /* by face and one: it stands up nowhere */
};

/* a row of the sheet at a sample a pixel about as room_resolve puts it on
 * the screen (the square root for the screen's 2.2, and no glow) */
static void rf_seen_row(void *p, int y)
{
	struct rf_sparse_arg *A = p;
	for (int x = 0; x < A->c->hw; x++) {
		size_t i = (size_t)y * A->c->hw + x;
		const float *s = A->c->hi + i * 4;
		unsigned char *o = A->seen + i * 4;
		double a = clampf(s[3], 0.0, 1.0);
		for (int q = 0; q < 3; q++) {
			double v = s[3] > 1e-6f
				? sqrt(aces_ch(fmax(s[q] * ICON_EXPOSURE / s[3], 0.0))) : 0.0;
			o[q] = (unsigned char)(clampf(v, 0.0, 1.0) * a * 255.0 + 0.5);
		}
		o[3] = (unsigned char)(a * 255.0 + 0.5);
	}
}

/* Whether pixels i and j of the sheet at a sample a pixel meet at an edge:
 * where they belong to different things, always; laid from different
 * faces, always too (a lay is cheap); of the one flat face, never, however
 * they differ -- the grain of the desk is as sharp as its picture, and
 * half the desk was laid again for it. Otherwise, a face with relief or
 * what is traced, where they differ on screen. */
static bool rf_differ(const struct rf_sparse_arg *A, size_t i, size_t j)
{
	const unsigned short *won = A->won;
	if (A->c->idhi[i] != A->c->idhi[j]) {
		return true;
	}
	if (!A->need[i] && !A->need[j]) {
		if (won[i] != won[j]) {
			return true;
		}
		if (won[i] && A->flat[won[i]]) {
			return false;
		}
	}
	for (int q = 0; q < 4; q++) {
		if (abs(A->seen[i * 4 + q] - A->seen[j * 4 + q]) > RF_EDGE) {
			return true;
		}
	}
	return false;
}

static bool rf_sparse(const struct rface_set *s, struct room_ctx *ctx, unsigned char *need)
{
	int ss = ICON_SS;
	if (ss < 2 || getenv("DESK_FACES_FULL")) {
		return false;
	}
	int w = ctx->hw / ss, h = ctx->hh / ss;
	size_t n = (size_t)w * h, nn = (size_t)ctx->hw * ctx->hh;
	struct room_ctx c1;
	memset(&c1, 0, sizeof c1);
	c1.hi = calloc(n * 4, sizeof(float));
	c1.idhi = calloc(n, 1);
	c1.hw = w;
	c1.hh = h;
	c1.layer = ctx->layer;
	c1.cam = ctx->cam;
	unsigned char *need1 = malloc(n), *seen = malloc(n * 4), *edge = calloc(n, 1);
	unsigned char *only = malloc(nn);
	unsigned short *won = calloc(n, sizeof *won);
	bool ok = c1.hi && c1.idhi && need1 && seen && edge && only && won;
	if (ok) {
		rf_lay_into(s, &c1, need1, NULL, NULL, won, true);
		/* what no face covers, traced at each pixel's first sample */
		memset(need, 0, nn);
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				need[(size_t)y * ss * ctx->hw + (size_t)x * ss] = need1[(size_t)y * w + x];
			}
		}
		ctx->need = need;
		atomic_fetch_add(&g_bake_goal, (long)ctx->hh);
		room_trace_cpu(ctx);
		ctx->need = NULL;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				size_t i = (size_t)y * w + x, k = (size_t)y * ss * ctx->hw + (size_t)x * ss;
				if (need1[i]) {
					memcpy(c1.hi + i * 4, ctx->hi + k * 4, 4 * sizeof(float));
					c1.idhi[i] = ctx->idhi[k];
				}
			}
		}
		memset(need, 0, nn);
		struct rf_sparse_arg a = { &c1, need1, won, seen, { false } };
		for (int k = 0; k < s->n; k++) {
			a.flat[k + 1] = !rf_relief(&s->f[k]);
		}
		rf_run(rf_seen_row, &a, h);
		/* each pixel against the one to its right and the three below */
		static const int DX[4] = { 1, -1, 0, 1 }, DY[4] = { 0, 1, 1, 1 };
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				size_t i = (size_t)y * w + x;
				for (int d = 0; d < 4; d++) {
					int qx = x + DX[d], qy = y + DY[d];
					if (qx < 0 || qx >= w || qy >= h) {
						continue;
					}
					size_t j = (size_t)qy * w + qx;
					if (rf_differ(&a, i, j)) {
						edge[i] = edge[j] = 1;
					}
				}
			}
		}
		size_t edges = 0;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				size_t i = (size_t)y * w + x;
				edges += edge[i];
				for (int sy = 0; sy < ss; sy++) {
					size_t k = (size_t)(y * ss + sy) * ctx->hw + (size_t)x * ss;
					for (int sx = 0; sx < ss; sx++, k++) {
						/* the first of a traced one's samples is traced */
						only[k] = edge[i] && !(need1[i] && sy == 0 && sx == 0);
						if (!edge[i]) {
							memcpy(ctx->hi + k * 4, c1.hi + i * 4, 4 * sizeof(float));
							ctx->idhi[k] = c1.idhi[i];
						}
					}
				}
			}
		}
		/* what the lay leaves is to trace */
		rf_lay_into(s, ctx, need, NULL, only, NULL, false);
		bool debug = getenv("DESK_FACES_DEBUG") != NULL;
		size_t t1 = 0, t2 = 0;
		for (size_t i = 0; debug && i < n; i++) {
			t1 += need1[i];
		}
		for (size_t i = 0; debug && i < nn; i++) {
			t2 += need[i] != 0;
		}
		/* At a level that traces two of an edge's four (edge_traced), the
		 * samples of a pixel off its diagonal that no face covers take
		 * the one on the diagonal in their row, once that is traced: the
		 * pixel is the two on its diagonal, as a GPU's two samples are */
		size_t taken = 0;
		if (ss == 2 && g_bq.edge_traced == 2) {
			memset(only, 0, nn);
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					size_t k = (size_t)y * 2 * ctx->hw + (size_t)x * 2;
					size_t off[2] = { k + 1, k + ctx->hw };
					for (int o = 0; o < 2; o++) {
						if (need[off[o]]) {
							need[off[o]] = 0;
							only[off[o]] = 1;
							taken++;
						}
					}
				}
			}
		}
		if (taken) {
			ctx->need = need;
			atomic_fetch_add(&g_bake_goal, (long)ctx->hh);
			room_trace_cpu(ctx);
			ctx->need = NULL;
			memset(need, 0, nn);
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					size_t k = (size_t)y * 2 * ctx->hw + (size_t)x * 2;
					size_t to[2] = { k + 1, k + ctx->hw }, from[2] = { k, k + ctx->hw + 1 };
					for (int o = 0; o < 2; o++) {
						if (only[to[o]]) {
							memcpy(ctx->hi + to[o] * 4, ctx->hi + from[o] * 4,
								4 * sizeof(float));
							ctx->idhi[to[o]] = ctx->idhi[from[o]];
						}
					}
				}
			}
		}
		if (debug) {
			printf("    %d samples a pixel at %.1f %% of the pixels; %zu traced at one, "
				"%zu more to trace, %zu of them taken from the diagonal\n", ss * ss,
				100.0 * edges / n, t1, t2, taken);
		}
	}
	free(c1.hi);
	free(c1.idhi);
	free(need1);
	free(seen);
	free(edge);
	free(only);
	free(won);
	return ok;
}

/* the faces into ctx: what is left to trace goes in need */
static void rf_lay_sheet(const struct rface_set *s, struct room_ctx *ctx, unsigned char *need)
{
	if (!rf_sparse(s, ctx, need)) {
		rf_lay(s, ctx, need);
	}
}

/* A sheet of layer into ctx from the faces, and what they do not cover
 * traced; false, nothing done, when it is not to be laid. */
static bool rf_compose(int layer, struct room_ctx *ctx)
{
	struct rface_set *s = rf_room_get(layer);
	if (!s) {
		return false;
	}
	unsigned char *need = malloc((size_t)ctx->hw * ctx->hh);
	if (!need) {
		return false;
	}
	rf_lay_sheet(s, ctx, need);
	ctx->need = need;
	room_trace_cpu(ctx);
	ctx->need = NULL;
	free(need);
	return true;
}

/* Whether the desk's sheets will be laid from faces: then they are up as
 * soon as a first look at them would have been, and there is none. */
static bool rf_desk_laid(void)
{
	return rf_wanted() && rf_pack_get(1) != NULL;
}

/* The desk's pack and faces let go: its sheets are made, and nothing after
 * them lays from it. */
static void rf_release_desk(void)
{
	for (int i = 1; i <= 2; i++) {
		if (g_rf_room[i]) {
			rf_free(g_rf_room[i]);
			free(g_rf_room[i]);
			g_rf_room[i] = NULL;
		}
	}
	if (g_rf_pack[1]) {
		rf_free(g_rf_pack[1]);
		free(g_rf_pack[1]);
		g_rf_pack[1] = NULL;
	}
}

/* The packs and the room's faces let go: the room is made. */
static void rf_release(void)
{
	for (int i = 0; i < RF_LAYERS; i++) {
		if (g_rf_room[i]) {
			rf_free(g_rf_room[i]);
			free(g_rf_room[i]);
			g_rf_room[i] = NULL;
		}
		if (g_rf_pack[i]) {
			rf_free(g_rf_pack[i]);
			free(g_rf_pack[i]);
			g_rf_pack[i] = NULL;
		}
	}
}

/* The desk itself: the two sheets the plan view is made of, and nothing
 * else. This is a quarter of the trace, and it is the whole of what is on
 * screen when the compositor starts, so it goes up on its own. */
static struct room_render *room_render_desk(int w, int h)
{
	sh_build();
	drawer_items_build();

	struct room_render *rr = calloc(1, sizeof(*rr));
	if (!rr) {
		return NULL;
	}
	rr->w = w;
	rr->h = h;
	rr->serial = ++g_room_serial;
	rr->film_div = ROOM_TILT_DIV;
	rr->shelf_ids = calloc((size_t)w * h, 1);
	rr->console_ids = calloc((size_t)w * h, 1);
	rr->post_ids = calloc((size_t)w * h, 1);

	/* two sheets, ICON_SS rows of samples per output row each */
	atomic_store(&g_bake_rows, 0);
	atomic_store(&g_bake_goal, (long)h * ICON_SS * 2);
	atomic_store(&g_rest_view, -1);
	atomic_store(&g_rest_total_w, 0);

	/* both desk sheets through the one camera, with the drawer fully out */
	room_cam_build((double)w / (double)h, &rr->cam);
	g_room_open = 1.0;
	/* On the desk the set is always stowed: what the desk view shows of it
	 * is the ledge, the lamps and the front edge of the panel. */
	g_cc_out = 0.0;
	rr->background = room_bake(1, w, h, &rr->cam, NULL);
	bool bg_gpu = g_room_last_gpu;
	/* The two sheets slide over each other and have to meet to the pixel,
	 * so they come through the one tracer: a desk the C traced (no GPU, or
	 * one that disagreed with it about the desk) has its drawer traced by
	 * the C too, and a drawer the GPU could not be trusted with sends the
	 * desk the GPU made back through the C. */
	g_room_force_cpu = !bg_gpu;
	rr->drawer = room_bake(2, w, h, &rr->cam, NULL);
	g_room_force_cpu = false;
	if (bg_gpu && !g_room_last_gpu) {
		atomic_fetch_add(&g_bake_goal, (long)h * ICON_SS);
		cairo_surface_destroy(rr->background);
		g_room_force_cpu = true;
		rr->background = room_bake(1, w, h, &rr->cam, NULL);
		g_room_force_cpu = false;
	}
	room_metrics(rr);
	return rr;
}

/* The next of the three views' sheets is about to be traced. */
static void rest_part(int w, int h, int view)
{
	atomic_fetch_add(&g_rest_done_w, atomic_load(&g_rest_part_w));
	atomic_store(&g_bake_rows, 0);
	atomic_store(&g_bake_goal, (long)h * ICON_SS);
	atomic_store(&g_rest_part_w, (long)w * h);
	atomic_store(&g_rest_view, view);
}

/* What traced the sheet room_bake made last, for the log. */
static const char *room_by(void)
{
	return g_room_last_gpu ? "the GPU" : g_room_last_faces ? "faces and the CPU" : "the CPU";
}

/* The three places you can look that are not the desk: their sheets, which
 * are what a view is drawn from and clicked on. None of them is on screen
 * until you ask for it, so this runs after the desk is already up and the
 * machine is already usable, and each view is there to turn to the moment
 * its sheet is: the bookcase -- the programs are on it -- first, and on a
 * machine that traces on its processor that is minutes before the post
 * box. Only the sheets -- the films of the moves to them come after all
 * three (room_render_films). A film is half to four
 * and a half times the trace of the sheet it ends on (eighteen frames, at
 * a sixth to a half of its size), and traced between sheet and sheet it
 * kept the bookcase away, on a first start, for minutes after it could
 * have been there.
 *
 * The drawer and the doors stand for each sheet as they did when a film
 * went between: out for the bookcase, in for the other two. A sheet is the
 * same picture it was, and a room kept on the disk stays good. */
static void room_render_views(struct room_render *rr)
{
	int w = rr->w, h = rr->h;
	double aspect = (double)w / (double)h;
	atomic_store(&g_rest_done_w, 0);
	atomic_store(&g_rest_part_w, 0);
	atomic_store(&g_rest_total_w, 3L * w * h);
	double t[4];

	/* the bookcase through its own camera, tilted up off the desk */
	t[0] = room_now();
	room_cam_shelf(aspect, &rr->shelf_cam);
	g_room_open = 1.0;
	g_cc_out = 0.0;
	g_pb_open = 0.0;
	rest_part(w, h, 0);
	rr->shelf = room_bake(3, w, h, &rr->shelf_cam, rr->shelf_ids);
	const char *by_shelf = room_by();
	if (!room_stopping()) {
		room_views_up(rr, 1u << DESK_VIEW_SHELF);
	}

	/* and the console, which is looked at from over the left of the desk
	 * once it has come out */
	t[1] = room_now();
	room_cam_console(aspect, &rr->console_cam);
	g_room_open = 0.0;
	g_cc_out = 1.0;
	rest_part(w, h, 1);
	rr->console = room_bake(4, w, h, &rr->console_cam, rr->console_ids);
	const char *by_console = room_by();
	g_cc_out = 0.0;
	if (!room_stopping()) {
		room_views_up(rr, 1u << DESK_VIEW_CONSOLE);
	}

	/* and the post box, which is looked at from in front of it with its
	 * door down */
	t[2] = room_now();
	room_cam_post(aspect, &rr->post_cam);
	g_pb_open = 1.0;
	rest_part(w, h, 2);
	rr->post = room_bake(5, w, h, &rr->post_cam, rr->post_ids);
	const char *by_post = room_by();
	g_pb_open = 0.0;
	if (!room_stopping()) {
		room_views_up(rr, 1u << DESK_VIEW_POST);
	}
	atomic_fetch_add(&g_rest_done_w, atomic_load(&g_rest_part_w));
	atomic_store(&g_rest_part_w, 0);
	t[3] = room_now();
	/* the desk camera owns the aspect-derived geometry; put it back --
	 * through a camera of its own: rr->cam is the desk's, which is up and
	 * being drawn from */
	struct room_cam desk;
	room_cam_build(aspect, &desk);
	if (!room_stopping()) {
		room_log("the views, %dx%d: the bookcase in %.1f s on %s, the console in %.1f s on "
			"%s, the post box in %.1f s on %s", w, h, t[1] - t[0], by_shelf,
			t[2] - t[1], by_console, t[3] - t[2], by_post);
	}
}

/* How finely the room follows the shape of the screen: aspect is snapped to
 * a thirty-secondth, which is two per cent at 16:10. */
#define ROOM_CANON_STEPS 32.0

static _Atomic int g_room_cap;     /* 0: not looked yet */
static bool g_room_cap_forced;     /* DESK_ROOM_W said so */

#ifdef DESK_HAVE_GPU
static bool room_wide_path(char *out, size_t n)
{
	char dir[512];
	if (!icon_cache_dir(dir, sizeof dir)) {
		return false;
	}
	return (size_t)snprintf(out, n, "%s/room-wide-%u.ok", dir, icon_build_tag()) < n;
}
#endif

/* The widest room this run traces. */
static int room_cap(void)
{
	int cap = atomic_load_explicit(&g_room_cap, memory_order_relaxed);
	if (cap) {
		return cap;
	}
	cap = ROOM_CANON_W;
	const char *e = getenv("DESK_ROOM_W");
	if (e && *e && atoi(e) >= 320) {
		cap = atoi(e) > 3840 ? 3840 : atoi(e);
		g_room_cap_forced = true;
	}
#ifdef DESK_HAVE_GPU
	else {
		char path[640];
		if (room_wide_path(path, sizeof path) && access(path, F_OK) == 0) {
			cap = ROOM_WIDE_W;
		}
	}
#endif
	atomic_store_explicit(&g_room_cap, cap, memory_order_relaxed);
	return cap;
}

#ifdef DESK_HAVE_GPU
/* A GPU traced a room here: from the next run on, rooms follow the screen. */
static void room_wide_remember(void)
{
	char path[640];
	if (g_room_cap_forced || room_cap() >= ROOM_WIDE_W || !room_wide_path(path, sizeof path)) {
		return;
	}
	FILE *f = fopen(path, "we");
	if (f) {
		fclose(f);
	}
}

/* A wide room was asked for on the strength of an earlier run, and this
 * one has no GPU to trace it with: back to the width the CPU can afford. */
static void room_wide_forget(void)
{
	char path[640];
	if (room_wide_path(path, sizeof path)) {
		unlink(path);
	}
	atomic_store_explicit(&g_room_cap, ROOM_CANON_W, memory_order_relaxed);
}
#endif

/* Points to pixels, as a Mac has them: 1 on an ordinary screen, 2 on a
 * Retina one, and whatever the compositor was told in between. The room
 * was designed at 1440 by 900 points on a desk 32 units wide, so a unit of
 * desk is 45 points: that is what keeps the console, the clock and the
 * post box one size from screen to screen. */
#define ROOM_PT_PER_UNIT 45.0
static double g_ui_scale = 1.0;

void desk_set_ui_scale(double scale)
{
	g_ui_scale = clampf(scale, 0.5, 4.0);
}

double desk_ui_scale(void)
{
	return g_ui_scale;
}

/* Pixels to a point for what is drawn flat over the desk -- a plate, the
 * corner of a sheet -- on a screen screen_w wide: the scale, and less on a
 * screen too narrow for the desk as designed, which shrinks with the room
 * (room_canon). */
double desk_px_per_pt(int screen_w)
{
	double wpt = (double)screen_w / g_ui_scale;
	double fit = 2.0 * RM_HX_MIN * ROOM_PT_PER_UNIT;
	return g_ui_scale * (wpt < fit && wpt > 0.0 ? wpt / fit : 1.0);
}

/* The widest a room is made at level q: ROOM_Q's -- but at 2D, Low and
 * Medium as wide as the machine allows. On a machine tracing on its
 * processor the room is laid from its faces (rf_compose), and a sample a
 * pixel at the screen's own width is less work than Low's four at two
 * thirds of it; 640 stretched to the screen was the softest desk of any
 * level, and Low's 960 next. Four samples are taken only where they show
 * (rf_sparse). A GPU traces it that wide in a moment. With the faces
 * switched off, or not there to lay, it is traced at ROOM_Q's, as it was.
 * (What else is traced at the level's size, the door and the weather,
 * stays at ROOM_Q's: desk_ambient_size.) */
static int room_level_cap(int q)
{
	if (q <= DESK_GFX_MEDIUM && !rf_switched_off() && rf_packs_there()) {
		return 0;
	}
	return ROOM_Q[q].cap;
}

/* The shape of room for a screen: the size to trace it at, and how wide
 * the desk is (hq: its half width in half units). */
static void room_canon_at(int screen_w, int screen_h, int machine, int *cw, int *ch, int *hq);

static void room_canon(int screen_w, int screen_h, int *cw, int *ch, int *hq)
{
	room_canon_at(screen_w, screen_h, room_cap(), cw, ch, hq);
}

/* the same, for a machine that traces rooms `machine` wide (room_cap) */
static void room_canon_at(int screen_w, int screen_h, int machine, int *cw, int *ch, int *hq)
{
	/* as wide as the screen is in points, and not narrower than the desk
	 * it was designed as; in half units, which is two per cent at 1920,
	 * so that a window being dragged wider is not a room a pixel */
	double hx = (double)screen_w / g_ui_scale / (2.0 * ROOM_PT_PER_UNIT);
	*hq = (int)lround(fmax(hx, RM_HX_MIN) * 2.0);
	if (*hq > 400) {
		*hq = 400;
	}
	double aspect = (double)screen_w / (double)(screen_h > 0 ? screen_h : 1);
	aspect = clampf(aspect, 0.30, 4.00);
	/* Snap the SHAPE of the room, not its size in pixels. Rounding the
	 * height alone still made a room per dozen pixels of drag, and each of
	 * those is a minute and a half on the tracer and eleven megabytes on
	 * the disk. A screen at 1440x900 and one at 3600x2260 are the same
	 * shape to within half a per cent and now get the same room.
	 *
	 * The desk is fitted to the frame by height and carries a third of its
	 * width in overscan, so a couple of per cent of error in the shape is
	 * a couple of per cent of different crop and nothing else. */
	aspect = round(aspect * ROOM_CANON_STEPS) / ROOM_CANON_STEPS;

	/* And the size off a short ladder, for the same reason. A step up is
	 * at worst a tenth more tracing; a room per window width was a whole
	 * trace per window width. */
	static const int RUNG[] = { 320, 480, 640, 800, 960, 1120, 1280, ROOM_CANON_W,
		1600, 1760, ROOM_WIDE_W, 2240, 2560, 3200, 3840 };
	int cap = machine;
	int lc = room_level_cap((int)desk_gfx_level());
	if (!g_room_cap_forced) {
		if (lc > 0) {
			cap = cap < lc ? cap : lc;
		} else if (lc < 0 && cap >= ROOM_WIDE_W) {
			cap = 2560;   /* a GPU traces it: as wide as a QHD screen */
		}
	}
	int ww = cap;
	for (size_t i = 0; i < sizeof RUNG / sizeof RUNG[0] && RUNG[i] <= cap; i++) {
		if (screen_w <= RUNG[i]) {
			ww = RUNG[i];
			break;
		}
	}
	int hh = (int)(ww / aspect + 0.5);
	hh = ((hh + 1) / 2) * 2;
	if (hh < 16) {
		hh = 16;
	}
	*cw = ww;
	*ch = hh;
}

static void room_free(struct room_render *rr)
{
	if (!rr) {
		return;
	}
	if (rr->background) {
		cairo_surface_destroy(rr->background);
	}
	if (rr->drawer) {
		cairo_surface_destroy(rr->drawer);
	}
	if (rr->shelf) {
		cairo_surface_destroy(rr->shelf);
	}
	if (rr->post) {
		cairo_surface_destroy(rr->post);
	}
	if (rr->console) {
		cairo_surface_destroy(rr->console);
	}
	for (int i = 0; i < DESK_VIEW_COUNT; i++) {
		if (rr->film[i]) {
			cairo_surface_destroy(rr->film[i]);
		}
	}
	if (rr->console_bare) {
		cairo_surface_destroy(rr->console_bare);
	}
	free(rr->shelf_ids);
	free(rr->console_ids);
	free(rr->console_bare_ids);
	free(rr->post_ids);
	free(rr);
}

static int room_png_ok(cairo_surface_t *s, int w, int h)
{
	return s && cairo_surface_status(s) == CAIRO_STATUS_SUCCESS
		&& cairo_image_surface_get_width(s) == w
		&& cairo_image_surface_get_height(s) == h;
}

static pthread_mutex_t g_render_lock = PTHREAD_MUTEX_INITIALIZER;

/* The sidecar that goes with the baked PNGs: the metrics the compositor
 * needs and the two id buffers. Versioned and length checked, because a
 * stale one from a build that had a different idea of what is in it would
 * otherwise be read as whatever this build expects. */
#define ROOM_META_MAGIC 0x4d574b44u   /* "DKWM" */
#define ROOM_META_VERSION 6u

struct room_meta {
	unsigned magic, version;
	int w, h;
	double slide, edge, shut_zoom, shut_pan, dr_v0, dr_v1;
};

/* every baked sheet, and the sidecar that carries the metrics and the
 * three id buffers */
enum { P_BG = 0, P_DR, P_SH, P_CC, P_PB, P_FM, P_FC, P_FP, P_CB, P_N };

static bool room_cache_paths(int w, int h, int hq, int q, char path[P_N][768],
	char meta[768])
{
	static const char *PART[] = { "bg", "dr", "sh", "cc", "pb", "fm", "fc", "fp", "cb" };
	char dir[512];
	if (!icon_cache_dir(dir, sizeof dir)) {
		return false;
	}
	unsigned tag = icon_build_tag();
	icon_cache_sweep(dir, tag);
	for (int i = 0; i < P_N; i++) {
		if ((size_t)snprintf(path[i], 768, "%s/room-%s-%dx%dd%d%c-%u.png",
			dir, PART[i], w, h, hq, ROOM_Q[q].tag, tag) >= 768) {
			return false;
		}
	}
	return (size_t)snprintf(meta, 768, "%s/room-%dx%dd%d%c-%u.meta", dir, w, h, hq,
		ROOM_Q[q].tag, tag) < 768;
}

/* A room off the disk, in three goes: the desk's part -- its two sheets and
 * the metrics -- the moment it is asked for; the three views and their id
 * buffers once the desk is up, into the same room; and the films of the
 * moves after those. Read whole, the views and films were three quarters
 * of what was decoded before the desk could show: on a slow machine,
 * seconds of dark at every login.
 *
 * The desk's part, or NULL if the room's sheets are not all there on the
 * disk: a desk read back whose views then cannot be is a room traced
 * again. Its films need not be there -- they are kept after the sheets,
 * one by one as they are traced, and one that is not there is traced
 * again (room_render_films). */
static struct room_render *room_cache_load_desk(int w, int h, int hq, int q)
{
	char path[P_N][768], meta[768];
	if (!room_cache_paths(w, h, hq, q, path, meta)) {
		return NULL;
	}
	int div = ROOM_Q[q].film_div;
	for (int i = 0; i < P_FM; i++) {
		if (access(path[i], R_OK) != 0) {
			return NULL;
		}
	}
	struct room_meta m;
	FILE *f = fopen(meta, "rbe");
	if (!f) {
		return NULL;
	}
	bool fine = fread(&m, sizeof m, 1, f) == 1 && m.magic == ROOM_META_MAGIC
		&& m.version == ROOM_META_VERSION && m.w == w && m.h == h;
	fclose(f);
	if (!fine) {
		return NULL;
	}
	cairo_surface_t *bg = cairo_image_surface_create_from_png(path[P_BG]);
	cairo_surface_t *dr = cairo_image_surface_create_from_png(path[P_DR]);
	struct room_render *rr = NULL;
	if (room_png_ok(bg, w, h) && room_png_ok(dr, w, h)) {
		rr = calloc(1, sizeof(*rr));
	}
	if (!rr) {
		cairo_surface_destroy(bg);
		cairo_surface_destroy(dr);
		return NULL;
	}
	rr->w = w;
	rr->h = h;
	rr->serial = ++g_room_serial;
	rr->background = bg;
	rr->drawer = dr;
	rr->film_div = div;
	rr->slide = m.slide;
	rr->edge = m.edge;
	rr->shut_zoom = m.shut_zoom;
	rr->shut_pan = m.shut_pan;
	rr->dr_v0 = m.dr_v0;
	rr->dr_v1 = m.dr_v1;
	sh_build();
	drawer_items_build();
	room_cam_shelf((double)w / (double)h, &rr->shelf_cam);
	room_cam_console((double)w / (double)h, &rr->console_cam);
	room_cam_post((double)w / (double)h, &rr->post_cam);
	room_cam_build((double)w / (double)h, &rr->cam);
	return rr;
}

/* The views, into a room whose desk is up (g_room_views says they are not
 * there yet, and nothing looks at them till it says they are). False if
 * any of them is missing or not what it should be, and then none of it is
 * put in. */
static bool room_cache_load_views(struct room_render *rr, int w, int h, int hq, int q)
{
	char path[P_N][768], meta[768];
	if (!room_cache_paths(w, h, hq, q, path, meta)) {
		return false;
	}
	cairo_surface_t *got[P_N] = { 0 };
	bool ok = true;
	for (int i = P_SH; i < P_FM; i++) {
		got[i] = cairo_image_surface_create_from_png(path[i]);
		ok = room_png_ok(got[i], w, h) && ok;
	}
	size_t n = (size_t)w * h;
	unsigned char *sids = ok ? malloc(n) : NULL;
	unsigned char *cids = sids ? malloc(n) : NULL;
	unsigned char *pids = cids ? malloc(n) : NULL;
	FILE *f = pids ? fopen(meta, "rbe") : NULL;
	ok = ok && f && fseek(f, (long)sizeof(struct room_meta), SEEK_SET) == 0
		&& fread(sids, 1, n, f) == n && fread(cids, 1, n, f) == n
		&& fread(pids, 1, n, f) == n;
	if (f) {
		fclose(f);
	}
	if (!ok) {
		free(pids);
		free(cids);
		free(sids);
		for (int i = 0; i < P_N; i++) {
			if (got[i]) {
				cairo_surface_destroy(got[i]);
			}
		}
		return false;
	}
	rr->shelf = got[P_SH];
	rr->console = got[P_CC];
	rr->post = got[P_PB];
	rr->shelf_ids = sids;
	rr->console_ids = cids;
	rr->post_ids = pids;
	return true;
}

/* A sheet onto the disk, under its own name only once all of it is there:
 * a session that ends halfway through writing one -- the moment a trace is
 * done is when a first start is most likely to be cut short -- leaves no
 * half a picture for the next start to read back. */
static bool room_png_store(cairo_surface_t *s, const char *path)
{
	char part[800];
	if ((size_t)snprintf(part, sizeof part, "%s.part", path) >= sizeof part) {
		return false;
	}
	if (cairo_surface_write_to_png(s, part) != CAIRO_STATUS_SUCCESS || rename(part, path) != 0) {
		unlink(part);
		return false;
	}
	return true;
}

/* Put a freshly traced room on the disk -- its sheets and the sidecar; the
 * films follow as they are traced -- so the next start of this size is
 * seconds rather than minutes. The sidecar goes last: a room is read back
 * only with it, and it is there only once every sheet is. */
static void room_cache_store(struct room_render *rr, int w, int h, int hq, int q)
{
	char path[P_N][768], meta[768];
	if (!rr || !rr->background || !rr->drawer || !rr->shelf || !rr->console || !rr->post
		|| !rr->shelf_ids || !rr->console_ids || !rr->post_ids
		|| !room_cache_paths(w, h, hq, q, path, meta)) {
		return;
	}
	cairo_surface_t *sheet[P_FM] = { rr->background, rr->drawer, rr->shelf, rr->console,
		rr->post };
	for (int i = 0; i < P_FM; i++) {
		if (!room_png_store(sheet[i], path[i])) {
			return;
		}
	}
	char part[800];
	if ((size_t)snprintf(part, sizeof part, "%s.part", meta) >= sizeof part) {
		return;
	}
	FILE *f = fopen(part, "wbe");
	if (!f) {
		return;
	}
	struct room_meta m = {
		ROOM_META_MAGIC, ROOM_META_VERSION, w, h,
		rr->slide, rr->edge, rr->shut_zoom, rr->shut_pan,
		rr->dr_v0, rr->dr_v1,
	};
	size_t n = (size_t)w * h;
	bool ok = fwrite(&m, sizeof m, 1, f) == 1 && fwrite(rr->shelf_ids, 1, n, f) == n
		&& fwrite(rr->console_ids, 1, n, f) == n && fwrite(rr->post_ids, 1, n, f) == n;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(part, meta) != 0) {
		unlink(part);
	}
}

static struct room_render *g_room;
static int g_room_w, g_room_h, g_room_hq;
static int g_room_q = DESK_GFX_HIGH;   /* the level it was traced at */
static unsigned g_room_look;            /* and what the desk was made of (desk_look) */
static unsigned g_room_look_making;     /* what the one being traced is made of */
static int g_room_warming;
/* Which of the views of the room that is up are there to turn to (a bit
 * for each, 1 << DESK_VIEW_*): none while the desk is up and the bookcase,
 * the console and the post box are still on the tracer, and each as its
 * sheet is done. Set under the lock after a view's sheet, ids and camera
 * are written, and read under the lock before they are touched: that
 * pairing is what makes it safe to hand the desk over -- and then each
 * view -- while the worker is still going. */
#define ROOM_VIEWS_ALL ((1u << DESK_VIEW_COUNT) - 1u)
static unsigned g_room_views;
static pthread_mutex_t g_room_lock = PTHREAD_MUTEX_INITIALIZER;

static struct room_render *room_peek(int cw, int ch, int hq)
{
	pthread_mutex_lock(&g_room_lock);
	struct room_render *rr = (g_room && g_room_w == cw && g_room_h == ch && g_room_hq == hq
		&& g_room_q == (int)desk_gfx_level() && g_room_look == desk_look()) ? g_room : NULL;
	pthread_mutex_unlock(&g_room_lock);
	return rr;
}

/* the same, but only once these views (a mask of them) are traced */
static struct room_render *room_peek_views(int cw, int ch, int hq, unsigned views)
{
	pthread_mutex_lock(&g_room_lock);
	struct room_render *rr = (g_room && g_room_w == cw && g_room_h == ch && g_room_hq == hq
		&& g_room_q == (int)desk_gfx_level() && g_room_look == desk_look()
		&& (g_room_views & views) == views) ? g_room : NULL;
	pthread_mutex_unlock(&g_room_lock);
	return rr;
}

static struct room_render *room_peek_whole(int cw, int ch, int hq)
{
	return room_peek_views(cw, ch, hq, ROOM_VIEWS_ALL);
}

/* The room a publish displaced. It is not freed on the worker: the desk is
 * drawn from whatever room is up, so the thread that paints may be halfway
 * through this one's sheets right now. It is freed by room_reap(), from the
 * thread that paints, at a moment when that thread is demonstrably not
 * inside a paint. */
static struct room_render *g_room_retired;

/* A room put up, with none of its views there yet (room_views_up). */
static void room_publish(struct room_render *rr, int cw, int ch, int hq, int q)
{
	struct room_render *spill = NULL;
	pthread_mutex_lock(&g_room_lock);
	struct room_render *old = g_room;
	g_room = rr;
	g_room_w = cw;
	g_room_h = ch;
	g_room_hq = hq;
	g_room_q = q;
	g_room_look = g_room_look_making;
	g_room_views = 0;
	if (old && old != rr) {
		/* two publishes without a paint between them: the one already
		 * waiting has not been drawn from since, so it can go now */
		spill = g_room_retired;
		g_room_retired = old;
	}
	pthread_mutex_unlock(&g_room_lock);
	if (spill) {
		room_free(spill);
	}
}

/* Called from the painting thread before it takes a room, so the room it
 * frees here is one it cannot be holding. */
static void room_reap(void)
{
	pthread_mutex_lock(&g_room_lock);
	struct room_render *dead = g_room_retired;
	g_room_retired = NULL;
	pthread_mutex_unlock(&g_room_lock);
	if (dead) {
		room_free(dead);
	}
}

/* Whatever room is up, whatever size it was baked for. */
static struct room_render *room_any(void)
{
	pthread_mutex_lock(&g_room_lock);
	struct room_render *rr = g_room;
	pthread_mutex_unlock(&g_room_lock);
	return rr;
}

static struct room_render *room_any_views(unsigned views)
{
	pthread_mutex_lock(&g_room_lock);
	struct room_render *rr = (g_room_views & views) == views ? g_room : NULL;
	pthread_mutex_unlock(&g_room_lock);
	return rr;
}

/* These views of rr there to turn to, if rr is still the room that is up. */
static void room_views_up(struct room_render *rr, unsigned views)
{
	pthread_mutex_lock(&g_room_lock);
	if (g_room == rr) {
		g_room_views |= views;
	}
	pthread_mutex_unlock(&g_room_lock);
}

/* A film of a room: under the lock, since one is put into a room that is
 * up, whose views are already being drawn from. */
static cairo_surface_t *room_film_get(struct room_render *rr, int which)
{
	pthread_mutex_lock(&g_room_lock);
	cairo_surface_t *s = rr->film[which];
	pthread_mutex_unlock(&g_room_lock);
	return s;
}

static void room_film_put(struct room_render *rr, int which, cairo_surface_t *s)
{
	pthread_mutex_lock(&g_room_lock);
	rr->film[which] = s;
	pthread_mutex_unlock(&g_room_lock);
}

/* What the screen last asked for (room_warm), and what is being made
 * (room_build_now). When they part -- another size, another level, asked
 * for while this room's films were still to come -- the films are let go
 * between frames, and the room that is wanted is started that much
 * sooner. Under g_room_lock. */
struct room_ask {
	int cw, ch, hq, q;
	unsigned look;   /* what the desk is made of (desk_look) */
};
static struct room_ask g_room_asked, g_room_making;

static bool room_superseded(void)
{
	pthread_mutex_lock(&g_room_lock);
	bool other = memcmp(&g_room_asked, &g_room_making, sizeof g_room_asked) != 0;
	pthread_mutex_unlock(&g_room_lock);
	return other;
}

/* each view's film, by its name in the cache */
static const int ROOM_FILM_PART[DESK_VIEW_COUNT] = {
	[DESK_VIEW_SHELF] = P_FM, [DESK_VIEW_CONSOLE] = P_FC, [DESK_VIEW_POST] = P_FP,
};

/* The films kept on the disk, into a room whose views are up. One that is
 * not there, or not whole, is left to room_render_films. How many came. */
static int room_cache_load_films(struct room_render *rr, int hq, int q)
{
	int w = rr->w, h = rr->h, div = rr->film_div;
	char path[P_N][768], meta[768];
	if (div <= 0 || !room_cache_paths(w, h, hq, q, path, meta)) {
		return 0;
	}
	int n = 0;
	for (int v = 0; v < DESK_VIEW_COUNT; v++) {
		const char *pth = path[ROOM_FILM_PART[v]];
		if (room_film_get(rr, v) || access(pth, R_OK) != 0) {
			continue;
		}
		cairo_surface_t *s = cairo_image_surface_create_from_png(pth);
		if (room_png_ok(s, w / div, (h / div) * ROOM_TILT_FRAMES)) {
			room_film_put(rr, v, s);
			n++;
		} else {
			cairo_surface_destroy(s);
		}
	}
	return n;
}

/* The console's sheet with the lid up (rr->console_bare), and its ids: under the lock, as a
 * film. */
static bool room_bare_get(struct room_render *rr, cairo_surface_t **sheet, const unsigned char **ids)
{
	pthread_mutex_lock(&g_room_lock);
	*sheet = rr->console_bare;
	*ids = rr->console_bare_ids;
	pthread_mutex_unlock(&g_room_lock);
	return *sheet && *ids;
}

static void room_bare_put(struct room_render *rr, cairo_surface_t *sheet, unsigned char *ids)
{
	pthread_mutex_lock(&g_room_lock);
	rr->console_bare = sheet;
	rr->console_bare_ids = ids;
	pthread_mutex_unlock(&g_room_lock);
}

/* where its ids are kept: beside its sheet, .ids for .png */
static bool room_bare_ids_path(const char *png, char *out, size_t n)
{
	size_t len = strlen(png);
	if (len < 4 || len + 1 > n) {
		return false;
	}
	memcpy(out, png, len - 4);
	memcpy(out + len - 4, ".ids", 5);
	return true;
}

/* Off the disk, if it was kept for this room: true if it is in. */
static bool room_bare_load(struct room_render *rr, int hq, int q)
{
	cairo_surface_t *have;
	const unsigned char *have_ids;
	if (room_bare_get(rr, &have, &have_ids)) {
		return true;
	}
	char path[P_N][768], meta[768], ip[780];
	if (!room_cache_paths(rr->w, rr->h, hq, q, path, meta) || access(path[P_CB], R_OK) != 0
		|| !room_bare_ids_path(path[P_CB], ip, sizeof ip)) {
		return false;
	}
	size_t n = (size_t)rr->w * rr->h;
	unsigned char *ids = malloc(n);
	FILE *f = ids ? fopen(ip, "rbe") : NULL;
	bool ok = f && fread(ids, 1, n, f) == n && fgetc(f) == EOF;
	if (f) {
		fclose(f);
	}
	cairo_surface_t *s = ok ? cairo_image_surface_create_from_png(path[P_CB]) : NULL;
	if (!ok || !room_png_ok(s, rr->w, rr->h)) {
		if (s) {
			cairo_surface_destroy(s);
		}
		free(ids);
		return false;
	}
	room_bare_put(rr, s, ids);
	return true;
}

/* Traced, if it was not there, and kept: the console out as its own view has it, the lid up
 * on its hinge -- what is under it, the lid's underside, and the shadow it throws. */
static void room_bare_make(struct room_render *rr, int hq, int q, bool keep)
{
	if (room_bare_load(rr, hq, q)) {
		return;
	}
	int w = rr->w, h = rr->h;
	size_t n = (size_t)w * h;
	unsigned char *ids = calloc(n, 1);
	if (!ids) {
		return;
	}
	struct room_cam cam;
	room_cam_console((double)w / (double)h, &cam);
	double was_open = g_room_open;
	g_room_open = 0.0;
	g_cc_out = 1.0;
	g_cc_lid = 2.0;
	g_room_gentle = true;
	cairo_surface_t *s = room_bake(4, w, h, &cam, ids);
	g_room_gentle = false;
	g_cc_lid = 1.0;
	g_cc_out = 0.0;
	g_room_open = was_open;
	if (!s || room_stopping() || room_superseded()) {
		if (s) {
			cairo_surface_destroy(s);
		}
		free(ids);
		return;
	}
	room_bare_put(rr, s, ids);
	char path[P_N][768], meta[768], ip[780], part[800];
	if (!keep || !room_cache_paths(w, h, hq, q, path, meta)
		|| !room_bare_ids_path(path[P_CB], ip, sizeof ip)
		|| (size_t)snprintf(part, sizeof part, "%s.part", ip) >= sizeof part) {
		return;
	}
	FILE *f = fopen(part, "wbe");
	bool ok = f && fwrite(ids, 1, n, f) == n;
	ok = f && fclose(f) == 0 && ok;
	if (!ok || rename(part, ip) != 0 || !room_png_store(s, path[P_CB])) {
		unlink(part);
	}
}

/* The films of the moves to the three views, once the views are up: each
 * goes into the room, and onto the disk, as it is done -- the bookcase's
 * first, the view most often turned to. Until a view's film is there the
 * move to it is a cut, as at 2D. One already in (read back) is left.
 * took[] is how long each was, 0 for one not traced; false if they were
 * let go part way, for the end of the session or another room. */
static bool room_render_films(struct room_render *rr, int hq, int q, double took[])
{
	int w = rr->w, h = rr->h;
	for (int v = 0; v < DESK_VIEW_COUNT; v++) {
		took[v] = 0.0;
	}
	char path[P_N][768], meta[768];
	bool keep = room_cache_paths(w, h, hq, q, path, meta);
	/* the console with its lid up first, at every level: the lid swings at all of them */
	room_bare_make(rr, hq, q, keep);
	if (room_stopping() || room_superseded()) {
		return false;
	}
	if (rr->film_div <= 0) {
		/* the desk camera owns the aspect-derived geometry; put it back */
		struct room_cam desk;
		room_cam_build((double)w / (double)h, &desk);
		return true;
	}
	bool whole = true;
	for (int v = 0; v < DESK_VIEW_COUNT; v++) {
		if (room_film_get(rr, v)) {
			continue;
		}
		double t0 = room_now();
		/* each film opens its own door as it goes; the others stay shut */
		g_cc_out = 0.0;
		g_pb_open = 0.0;
		g_room_gentle = true;
		cairo_surface_t *f = room_bake_film(w, h, v);
		g_room_gentle = false;
		if (room_stopping() || room_superseded()) {
			if (f) {
				cairo_surface_destroy(f);
			}
			whole = false;
			break;
		}
		took[v] = room_now() - t0;
		if (f) {
			room_film_put(rr, v, f);
			if (keep) {
				room_png_store(f, path[ROOM_FILM_PART[v]]);
			}
		}
	}
	g_cc_out = 0.0;
	g_pb_open = 0.0;
	/* the desk camera owns the aspect-derived geometry; put it back */
	struct room_cam desk;
	room_cam_build((double)w / (double)h, &desk);
	return whole;
}

/* whether the room is about to be traced on the processor (no GPU to do
 * it, or only a software one) */
static bool room_traces_on_cpu(void)
{
#ifdef DESK_HAVE_GPU
	struct desk_gpu *g = room_gpu();
	return !g || desk_gpu_software(g);
#else
	return true;
#endif
}

/* the room the screen wants being traced from nothing, not read back: from
 * when that is known until the desk and the three views are up
 * (desk_scene_first_run) */
static _Atomic int g_room_afresh;

/* A sheet traced at one sample a pixel shows the grain of the wood as a
 * moire once it is stretched: softened, a box of three each way, it is
 * only soft. In place, premultiplied, so the drawer's clear ground stays
 * clear. */
static void surface_soften(cairo_surface_t *s)
{
	cairo_surface_flush(s);
	int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
	int stride = cairo_image_surface_get_stride(s);
	unsigned char *px = cairo_image_surface_get_data(s);
	unsigned char *tmp = malloc((size_t)stride * h);
	if (!px || !tmp || w < 3 || h < 3) {
		free(tmp);
		return;
	}
	for (int pass = 0; pass < 2; pass++) {
		/* pass 0 across, from px into tmp; pass 1 down, from tmp into px */
		const unsigned char *src = pass ? tmp : px;
		unsigned char *dst = pass ? px : tmp;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				for (int c = 0; c < 4; c++) {
					int sum = 0;
					for (int k = -1; k <= 1; k++) {
						int xx = pass ? x : x + k, yy = pass ? y + k : y;
						xx = xx < 0 ? 0 : xx >= w ? w - 1 : xx;
						yy = yy < 0 ? 0 : yy >= h ? h - 1 : yy;
						sum += src[(size_t)yy * stride + xx * 4 + c];
					}
					dst[(size_t)y * stride + x * 4 + c] = (unsigned char)((sum + 1) / 3);
				}
			}
		}
	}
	free(tmp);
	cairo_surface_mark_dirty(s);
}

/* The same over each pixel and its neighbours weighed 1 2 1 across and
 * down: what a sample a pixel misses between them, put back softly. */
static void surface_tent(cairo_surface_t *s)
{
	cairo_surface_flush(s);
	int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
	int stride = cairo_image_surface_get_stride(s);
	unsigned char *px = cairo_image_surface_get_data(s);
	unsigned char *tmp = malloc((size_t)stride * h);
	if (!px || !tmp || w < 3 || h < 3) {
		free(tmp);
		return;
	}
	for (int pass = 0; pass < 2; pass++) {
		const unsigned char *src = pass ? tmp : px;
		unsigned char *dst = pass ? px : tmp;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				int x0 = pass ? x : (x > 0 ? x - 1 : 0), x1 = pass ? x : (x < w - 1 ? x + 1 : x);
				int y0 = pass ? (y > 0 ? y - 1 : 0) : y, y1 = pass ? (y < h - 1 ? y + 1 : y) : y;
				for (int c = 0; c < 4; c++) {
					int sum = src[(size_t)y0 * stride + x0 * 4 + c]
						+ 2 * src[(size_t)y * stride + x * 4 + c]
						+ src[(size_t)y1 * stride + x1 * 4 + c];
					dst[(size_t)y * stride + x * 4 + c] = (unsigned char)((sum + 2) / 4);
				}
			}
		}
	}
	free(tmp);
	cairo_surface_mark_dirty(s);
}

static void room_preview(int cw, int ch, int hq, int q)
{
	int pw = (cw / 3) & ~1, ph = (ch / 3) & ~1;
	if (pw < 64 || ph < 40 || room_any()) {
		return;
	}
	struct room_q full = g_bq;
	g_bq.ss = 1;
	double t0 = room_now();
	/* its rows are not the desk's, and the count on the clock is the
	 * desk's to say (desk_scene_progress) */
	atomic_store(&g_previewing, 1);
	struct room_render *pv = room_render_desk(pw, ph);
	atomic_store(&g_previewing, 0);
	g_bq = full;
	if (!pv) {
		return;
	}
	surface_soften(pv->background);
	surface_soften(pv->drawer);
	if (room_stopping()) {
		room_free(pv);
		return;
	}
	room_publish(pv, pw, ph, hq, q);
	room_log("a first look at the desk, %dx%d, in %.1f s", pw, ph, room_now() - t0);
}

/* The level's trace for a room cw wide. */
static void room_level_set(int q, int cw)
{
	g_bq = ROOM_Q[q];
	if (g_bq.ss > 2 && cw > 1600) {
		/* nine samples a pixel is a buffer of floats nine times the room:
		 * past 1600 wide, four */
		g_bq.ss = 2;
	}
}

static const char *const ROOM_LEVEL_NAME[DESK_GFX_COUNT] = { "2D", "Low", "Medium", "High",
	"Ultra" };

/* The room (cw, ch, hq) at level q, read back or traced; (wcw, wch, whq)
 * is the one it is instead if this turns out to be the first room on a
 * machine whose GPU can follow the screen (room_warm). */
static void room_cases_ready(double aspect, double hx);

static void room_build_now(int cw, int ch, int hq, int wcw, int wch, int whq, int q)
{
	pthread_mutex_lock(&g_render_lock);
	if (room_peek_whole(cw, ch, hq) && g_room_q == q) {
		pthread_mutex_unlock(&g_render_lock);
		return;
	}
	pthread_mutex_lock(&g_room_lock);
	g_room_making = (struct room_ask){ cw, ch, hq, q, desk_look() };
	g_room_look_making = g_room_making.look;
	wood_bake_begin();
	pthread_mutex_unlock(&g_room_lock);
	/* the desk this room is of: everything traced or loaded from here on
	 * is laid out on it; and at the level it was asked for */
	g_rm_hx = hq * 0.5;
	room_level_set(q, cw);
	/* and the pictures of the things on it for a screen this shape over a
	 * desk this wide, before there is a desk to put them on */
	room_cases_ready((double)cw / (double)ch, hq * 0.5);
	double read0 = room_now();
	struct room_render *rr = room_cache_load_desk(cw, ch, hq, q);
	bool traced = false;
#ifdef DESK_HAVE_GPU
	if (!rr && (wcw != cw || wch != ch || whq != hq)) {
		/* The first room this machine traces, on a screen wider than the
		 * processor can afford to follow. Whether there is a GPU to follow
		 * it with is known once one is opened -- which the trace was about
		 * to do anyway -- and a room traced at the processor's width while
		 * that was found out was traced again at the GPU's at the next
		 * start: a second first start, the desk and all. So it is opened
		 * first, and the room traced the width it will stay. */
		struct desk_gpu *g = room_gpu();
		if (g && !desk_gpu_software(g)) {
			room_wide_remember();
			room_log("a GPU to trace with: the room %dx%d, not %dx%d", wcw, wch, cw, ch);
			struct room_ask narrow = { cw, ch, hq, q, g_room_making.look },
				wide = { wcw, wch, whq, q, g_room_making.look };
			pthread_mutex_lock(&g_room_lock);
			atomic_store_explicit(&g_room_cap, ROOM_WIDE_W, memory_order_relaxed);
			/* the room the screen asked for is this one, at its new width */
			if (!memcmp(&g_room_asked, &narrow, sizeof narrow)) {
				g_room_asked = wide;
			}
			g_room_making = wide;
			pthread_mutex_unlock(&g_room_lock);
			cw = wcw;
			ch = wch;
			hq = whq;
			g_rm_hx = hq * 0.5;
			room_level_set(q, cw);
			rr = room_cache_load_desk(cw, ch, hq, q);
		}
	}
	if (!rr && cw > ROOM_CANON_W && !g_room_cap_forced) {
		struct desk_gpu *g = room_gpu();
		if (!g || desk_gpu_software(g)) {
			/* not this machine after all: the caller asks again, and is
			 * told the width the CPU can afford */
			room_log("no GPU to trace a room %d wide with; going back to %d", cw,
				ROOM_CANON_W);
			room_wide_forget();
			room_gpu_end();
			pthread_mutex_lock(&g_room_lock);
			g_room_warming = 0;
			pthread_mutex_unlock(&g_room_lock);
			pthread_mutex_unlock(&g_render_lock);
			return;
		}
	}
#else
	(void)wcw;
	(void)wch;
	(void)whq;
#endif
	/* The quality it is traced at, and kept under: the level's -- except
	 * that Ultra's nine samples a pixel and half-size films are a GPU's to
	 * trace. Where the CPU traces the room (no GPU, or one that failed its
	 * trial), an Ultra room nobody has traced yet is High's, the one High
	 * would trace and read back: on a laptop whose GPU would not, Ultra on
	 * the processor was twelve minutes before the views were all there,
	 * High's is under half that. A GPU that traces it later traces Ultra's
	 * own, which is kept apart from this one. */
	int tq = q;
	if (!rr && q == DESK_GFX_ULTRA && room_traces_on_cpu()) {
		tq = DESK_GFX_HIGH;
		room_level_set(tq, cw);
		rr = room_cache_load_desk(cw, ch, hq, tq);
		room_log("the CPU traces the room here: at High's quality rather than Ultra's%s",
			rr ? " (read back)" : "");
	}
	if (rr) {
		/* the desk at once, the views after it, and their films after
		 * those */
		room_publish(rr, cw, ch, hq, q);
		double read1 = room_now();
		if (room_cache_load_views(rr, cw, ch, hq, tq)) {
			room_views_up(rr, ROOM_VIEWS_ALL);
			double read2 = room_now();
			int films = room_cache_load_films(rr, hq, tq);
			room_log("read back %dx%d: the desk in %.2f s, the views in %.2f s, %d of %d "
				"films in %.2f s", cw, ch, read1 - read0, read2 - read1, films,
				rr->film_div > 0 ? DESK_VIEW_COUNT : 0, room_now() - read2);
		} else {
			/* the views would not come back: the whole room traced again,
			 * the one read back up meanwhile */
			room_log("the views of a %dx%d room would not read back; tracing it again", cw, ch);
			rr = NULL;
		}
	}
	if (!rr) {
		traced = true;
		atomic_store_explicit(&g_room_afresh, 1, memory_order_relaxed);
		if (room_traces_on_cpu() && !rf_desk_laid()) {
			room_preview(cw, ch, hq, q);
		}
		/* The desk first, and on screen the moment it is traced. The three
		 * other views are three quarters of the work and none of them is
		 * being looked at: making the user watch them go by is a minute
		 * and a half of staring at a bare plank. */
		double t0 = room_now();
		rr = room_render_desk(cw, ch);
		rf_release_desk();
		if (room_stopping()) {
			/* stopped part way (desk_room_stop): none of it goes up */
			if (rr) {
				room_free(rr);
			}
			rr = NULL;
		} else if (rr) {
			room_publish(rr, cw, ch, hq, q);
		}
		if (rr) {
			room_log("the desk, %dx%d at %s, %d sample%s a pixel: %.1f s on %s", cw, ch,
				ROOM_LEVEL_NAME[tq], g_bq.ss * g_bq.ss, g_bq.ss > 1 ? "s" : "",
				room_now() - t0, room_by());
			room_render_views(rr);
		}
		/* the desk and its views up: the rest (the films) nobody waits on */
		atomic_store_explicit(&g_room_afresh, 0, memory_order_relaxed);
		/* each view up the moment it was traced (room_render_views), and
		 * all of it kept; what was stopped part way is not */
		if (rr && !room_stopping()) {
			room_cache_store(rr, cw, ch, hq, tq);
		}
	}
	if (rr && !room_stopping()) {
		double took[DESK_VIEW_COUNT];
		double t0 = room_now();
		bool whole = room_render_films(rr, hq, tq, took);
		static const char *const WHOSE[DESK_VIEW_COUNT] = { "the bookcase's",
			"the console's", "the post box's" };
		char each[192] = "";
		size_t at = 0;
		for (int v = 0; v < DESK_VIEW_COUNT && at < sizeof each; v++) {
			if (took[v] > 0.0) {
				int n = snprintf(each + at, sizeof each - at, "%s%s in %.1f s",
					at ? ", " : "", WHOSE[v], took[v]);
				at += n > 0 ? (size_t)n : 0;
			}
		}
		if (each[0] || !whole) {
			room_log("the films of the moves, %dx%d a frame: %s%s; in all %.1f s",
				rr->w / rr->film_div, rr->h / rr->film_div, each[0] ? each : "none",
				whole ? "" : room_stopping() ? " (stopped part way: the session is ending)"
				: " (let go part way: another room is wanted)",
				room_now() - t0);
		}
	}
#ifdef DESK_HAVE_GPU
	/* a GPU that is one traced it: rooms may follow the screen from now on */
	if (traced && g_rgpu.g && !desk_gpu_software(g_rgpu.g) && !room_stopping()) {
		room_wide_remember();
	}
	room_gpu_end();
#else
	(void)traced;
#endif
	atomic_store_explicit(&g_room_afresh, 0, memory_order_relaxed);
	rf_release();
	pthread_mutex_lock(&g_room_lock);
	g_room_warming = 0;
	pthread_mutex_unlock(&g_room_lock);
	pthread_mutex_unlock(&g_render_lock);
}

bool desk_scene_first_run(void)
{
	return atomic_load_explicit(&g_room_afresh, memory_order_relaxed) != 0;
}

struct room_warm_args {
	int cw, ch, hq, wcw, wch, whq, q;
};

static void *room_warm_worker(void *arg)
{
	thread_to_the_back();
	struct room_warm_args *a = arg;
	room_build_now(a->cw, a->ch, a->hq, a->wcw, a->wch, a->whq, a->q);
	free(a);
	return NULL;
}

/* The warm-up's thread, from the thread that starts it and the one that
 * stops it (the compositor's own), while there is one to wait for. */
static pthread_t g_room_warm_th;
static bool g_room_warm_live;

static void room_warm(int screen_w, int screen_h, bool async)
{
	int cw, ch, hq, q = (int)desk_gfx_level();
	/* what is wanted now, whatever is being made: the films of a room
	 * nobody wants any more are let go (room_superseded). Worked out under
	 * the lock, which is what room_build_now widens the machine's rooms
	 * under. */
	pthread_mutex_lock(&g_room_lock);
	room_canon(screen_w, screen_h, &cw, &ch, &hq);
	g_room_asked = (struct room_ask){ cw, ch, hq, q, desk_look() };
	pthread_mutex_unlock(&g_room_lock);
	if (room_peek_whole(cw, ch, hq) || room_stopping()) {
		return;
	}
	/* and the room it is instead if this machine's first room finds a GPU
	 * to follow the screen with (room_build_now) */
	int wcw = cw, wch = ch, whq = hq;
#ifdef DESK_HAVE_GPU
	if (room_cap() < ROOM_WIDE_W && !g_room_cap_forced && room_level_cap(q) <= 0) {
		room_canon_at(screen_w, screen_h, ROOM_WIDE_W, &wcw, &wch, &whq);
	}
#endif
	if (!async) {
		room_build_now(cw, ch, hq, wcw, wch, whq, q);
		return;
	}
	pthread_mutex_lock(&g_room_lock);
	int busy = g_room_warming;
	g_room_warming = 1;
	pthread_mutex_unlock(&g_room_lock);
	if (busy) {
		return;
	}
	struct room_warm_args *a = malloc(sizeof(*a));
	if (!a) {
		/* no worker will run to say it is done: the next warm-up tries */
		pthread_mutex_lock(&g_room_lock);
		g_room_warming = 0;
		pthread_mutex_unlock(&g_room_lock);
		return;
	}
	a->cw = cw;
	a->ch = ch;
	a->hq = hq;
	a->wcw = wcw;
	a->wch = wch;
	a->whq = whq;
	a->q = q;
	/* the last one has said it is done (g_room_warming), and is on its
	 * way out if it has not gone: waited for, which is at once */
	if (g_room_warm_live) {
		pthread_join(g_room_warm_th, NULL);
		g_room_warm_live = false;
	}
	if (pthread_create(&g_room_warm_th, NULL, room_warm_worker, a) == 0) {
		g_room_warm_live = true;
	} else {
		room_warm_worker(a);
	}
}
