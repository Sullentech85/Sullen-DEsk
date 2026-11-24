/* Part of draw.c: the front door: the eye, tracing a frame and what is kept.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- the eye ---- */

struct lk_cam {
	vec3 eye;
	double yaw, pitch;   /* yaw: + to the left; pitch: + up */
	double f;            /* pixels, of the w x h it is for */
	double cx, cy;
};

struct lk_pose {
	vec3 eye;
	double yaw, pitch, zoom;
};

/* at the door; leaning in to the key; a step back; inside, looking at the
 * bookcase the way the compositor's own view of it does */
static const struct lk_pose LK_REST = { { -0.04, 1.48, 1.05 }, -0.030, -0.070, 1.00 };
static const struct lk_pose LK_LEAN = { { 0.10, 1.32, 0.38 }, -0.485, -0.346, 1.50 };
static const struct lk_pose LK_BACK = { { 0.10, 1.53, 1.85 }, -0.015, -0.055, 1.00 };
static const struct lk_pose LK_IN = { { 0.00, 1.45, -0.95 }, 0.000, 0.000, 1.00 };

static struct lk_pose lk_pose_mix(struct lk_pose a, struct lk_pose b, double t)
{
	return (struct lk_pose){ vmix(a.eye, b.eye, t), a.yaw + (b.yaw - a.yaw) * t,
		a.pitch + (b.pitch - a.pitch) * t, a.zoom + (b.zoom - a.zoom) * t };
}

/* the field of view: about as much door as a head held a metre off it
 * sees, more if the screen is narrow */
static double lk_tan_v(int w, int h)
{
	return fmax(0.535, 0.60 * h / (double)w);
}

/* Where the eye is: at the door, leaning in, a step back, or through the
 * doorway towards end -- the camera the room's own view was taken with --
 * on a curve that comes through the middle of the doorway at a head's
 * height and only then looks down at the desk and narrows to that view.
 * With no room built, towards the old picture on the far wall. */
static void lk_camera_to(int w, int h, const struct desk_lock_look *lk,
	const struct lk_cam *end, struct lk_cam *c)
{
	double lean = clampf(lk->lean, 0.0, 1.0), back = clampf(lk->back, 0.0, 1.0);
	double push = clampf(lk->push, 0.0, 1.0);
	struct lk_pose p = lk_pose_mix(LK_REST, LK_LEAN, lean);
	p = lk_pose_mix(p, LK_BACK, back);
	double bob = 1.0 - clampf(lk->steady, 0.0, 1.0);
	p.eye.y -= 0.010 * bob * fabs(sin(back * 2.0 * M_PI));
	double f0 = h * 0.5 / lk_tan_v(w, h) * p.zoom;
	if (!end || push <= 0.0) {
		if (push > 0.0) {
			p = lk_pose_mix(p, LK_IN, push);
			p.eye.y -= 0.012 * bob * fabs(sin(push * 3.0 * M_PI));
		}
		c->eye = p.eye;
		c->yaw = p.yaw;
		c->pitch = p.pitch;
		c->f = f0;
		c->cx = w * 0.5;
		c->cy = h * 0.5;
		return;
	}
	double t = push, a = (1.0 - t) * (1.0 - t), b = 2.0 * t * (1.0 - t), e = t * t;
	vec3 mid = v3(0.0, p.eye.y, -LK_WALL - 0.35);
	c->eye = vadd(vadd(vmul(p.eye, a), vmul(mid, b)), vmul(end->eye, e));
	c->eye.y -= 0.012 * bob * fabs(sin(push * 3.0 * M_PI)) * (1.0 - t);
	double look = smoothstepf(0.30, 1.0, t);
	c->yaw = p.yaw + (end->yaw - p.yaw) * smoothstepf(0.0, 0.5, t);
	c->pitch = p.pitch + (end->pitch - p.pitch) * look;
	c->f = exp(log(f0) + (log(end->f) - log(f0)) * smoothstepf(0.15, 1.0, t));
	c->cx = w * 0.5 + (end->cx - w * 0.5) * look;
	c->cy = h * 0.5 + (end->cy - h * 0.5) * look;
}

static void lk_camera(int w, int h, const struct desk_lock_look *lk, struct lk_cam *c)
{
	lk_camera_to(w, h, lk, NULL, c);
}

/* Where the moves under way have the eye, the key and the door. Here and
 * not in the compositor, so that a move filmed ahead (desk_lock_films) is
 * made by the very sums that make it live. */
static double lk_ease(double t)
{
	t = clampf(t, 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

/* how long a move runs until it leaves the eye where it stays */
static double lk_move_len(int move, bool inside)
{
	switch (move) {
	case DESK_LOCK_MOVE_WALK:
		return DESK_LOCK_WALK_S;
	case DESK_LOCK_MOVE_LEAN:
		return DESK_LOCK_TRY_AT + DESK_LOCK_TRY_S;
	case DESK_LOCK_MOVE_FAIL:
		return 0.72;   /* leant back out; the key's shudder is long since nothing */
	case DESK_LOCK_MOVE_OPEN:
		return inside ? DESK_LOCK_PUSH1 : DESK_LOCK_SWING1;
	case DESK_LOCK_MOVE_OUT:
		return DESK_LOCK_OUT_END;
	default:
		return 0.0;
	}
}

void desk_lock_pose(const struct desk_lock_times *tm, struct desk_lock_look *lk)
{
	lk->back = lk->lean = lk->turn = lk->open = lk->push = lk->fail = 0.0;
	if (tm->walking) {
		lk->back = 1.0 - lk_ease(tm->walk_t / DESK_LOCK_WALK_S);
	}
	/* Return: lean in to the key, and try it */
	if (tm->checking) {
		double t = tm->check_t, l0 = tm->lean0;
		lk->lean = l0 + (1.0 - l0) * lk_ease(t / DESK_LOCK_LEAN_S);
		lk->turn = DESK_LOCK_TRY * lk_ease((t - DESK_LOCK_TRY_AT) / DESK_LOCK_TRY_S);
	}
	if (tm->opening) {
		/* it goes round; stand back */
		double t = tm->open_t;
		lk->turn += (1.0 - lk->turn) * lk_ease(t / DESK_LOCK_TURN_S);
		lk->lean *= 1.0 - lk_ease((t - DESK_LOCK_BACK0) / (DESK_LOCK_BACK1 - DESK_LOCK_BACK0));
	} else if (tm->failing) {
		/* it will not go: it is jerked against the wards and comes back,
		 * and you stand back to read why */
		double t = tm->fail_t;
		lk->fail = clampf(t / DESK_LOCK_FAIL_S, 0.0, 0.999);
		lk->turn *= 1.0 - lk_ease(t / 0.30);
		lk->turn += 0.07 * sin(t * 46.0) * exp(-t * 5.5);
		lk->lean *= 1.0 - lk_ease((t - 0.22) / 0.50);
	}
	if (tm->leaving) {
		/* letting yourself in, backwards */
		double t = tm->leave_t;
		lk->push = 1.0 - lk_ease(t / DESK_LOCK_OUT_WALK);
		lk->open = 1.0 - lk_ease((t - DESK_LOCK_OUT_SHUT0)
			/ (DESK_LOCK_OUT_SHUT1 - DESK_LOCK_OUT_SHUT0));
		lk->lean = lk_ease((t - DESK_LOCK_OUT_LEAN) / DESK_LOCK_LEAN_S)
			* (1.0 - lk_ease((t - DESK_LOCK_OUT_BACK) / (DESK_LOCK_OUT_END - DESK_LOCK_OUT_BACK)));
		lk->turn = 1.0 - lk_ease((t - DESK_LOCK_OUT_TURN) / DESK_LOCK_TURN_S);
	}
	if (tm->opening) {
		double t = tm->open_t;
		lk->open = lk_ease((t - DESK_LOCK_SWING0) / (DESK_LOCK_SWING1 - DESK_LOCK_SWING0));
		lk->push = tm->inside ? lk_ease((t - DESK_LOCK_PUSH0) / (DESK_LOCK_PUSH1 - DESK_LOCK_PUSH0))
			: 0.0;
	}
	/* Which move it is, if it is one alone and begun from where it always
	 * begins: Return from standing at the door; let in, or told no, once the
	 * key has been seen tried (the compositor waits for that). Anything
	 * else -- Return while still walking up, or while leaning back from a
	 * no -- is made live. */
	const double tried = DESK_LOCK_TRY_AT + DESK_LOCK_TRY_S;
	bool leant = tm->checking && tm->lean0 == 0.0;
	lk->move = DESK_LOCK_MOVE_NONE;
	lk->move_t = 0.0;
	if (tm->walking) {
		if (!tm->checking && !tm->opening && !tm->failing && !tm->leaving) {
			lk->move = DESK_LOCK_MOVE_WALK;
			lk->move_t = tm->walk_t;
		}
	} else if (tm->leaving) {
		if (!tm->checking && !tm->opening && !tm->failing) {
			lk->move = DESK_LOCK_MOVE_OUT;
			lk->move_t = tm->leave_t;
		}
	} else if (tm->opening) {
		if (leant && tm->open_t < 0.0) {
			lk->move = DESK_LOCK_MOVE_LEAN;   /* waiting for the key to have been tried */
			lk->move_t = tm->check_t;
		} else if (leant && tm->check_t - tm->open_t >= tried) {
			lk->move = DESK_LOCK_MOVE_OPEN;
			lk->move_t = tm->open_t;
		}
	} else if (tm->failing) {
		if (leant && tm->check_t - tm->fail_t >= tried) {
			lk->move = DESK_LOCK_MOVE_FAIL;
			lk->move_t = tm->fail_t;
		}
	} else if (leant) {
		lk->move = DESK_LOCK_MOVE_LEAN;
		lk->move_t = tm->check_t;
	}
}

/* the pose t seconds into a move, begun from where it always begins */
static void lk_move_pose(int move, double t, bool inside, struct desk_lock_look *lk)
{
	struct desk_lock_times tm;
	memset(&tm, 0, sizeof tm);
	tm.inside = inside;
	switch (move) {
	case DESK_LOCK_MOVE_WALK:
		tm.walking = true;
		tm.walk_t = t;
		break;
	case DESK_LOCK_MOVE_LEAN:
		tm.checking = true;
		tm.check_t = t;
		break;
	case DESK_LOCK_MOVE_FAIL:
		tm.checking = tm.failing = true;
		tm.check_t = t + 10.0;
		tm.fail_t = t;
		break;
	case DESK_LOCK_MOVE_OPEN:
		tm.checking = tm.opening = true;
		tm.check_t = t + 10.0;
		tm.open_t = t;
		break;
	case DESK_LOCK_MOVE_OUT:
		tm.leaving = true;
		tm.leave_t = t;
		break;
	default:
		break;
	}
	desk_lock_pose(&tm, lk);
}

static vec3 lk_cam_dir(const struct lk_cam *c, double x, double y)
{
	vec3 d = v3((x - c->cx) / c->f, -(y - c->cy) / c->f, -1.0);
	return vnorm(rot_y(rot_x(d, c->pitch), c->yaw));
}

static bool lk_project(const struct lk_cam *c, vec3 p, double *x, double *y)
{
	vec3 q = rot_x(rot_y(vsub(p, c->eye), -c->yaw), -c->pitch);
	if (q.z > -1e-4) {
		return false;
	}
	*x = c->cx + c->f * q.x / -q.z;
	*y = c->cy - c->f * q.y / -q.z;
	return true;
}

static void lk_scene_for(struct lk_scene *sc, int w, int h, const struct desk_lock_look *lk)
{
	memset(sc, 0, sizeof *sc);
	lk_rig_for(&lk->mood, &sc->rig);
	lkd_dress(sc);
	double open = clampf(lk->open, 0.0, 1.0);
	sc->door = open * LK_SWING;
	sc->cd = cos(sc->door);
	sc->sd = sin(sc->door);
	sc->key = -clampf(lk->turn, -0.5, 1.2) * M_PI * 0.5;
	sc->lamp = 1.0 - 0.35 * open;
	sc->open = open;
	sc->post = smoothstepf(1.0, 0.55, clampf(lk->push, 0.0, 1.0));
	/* inside: the far wall is exactly what the eye sees from where the walk
	 * ends, so the last frame is the picture of the room */
	double tv = lk_tan_v(w, h) / LK_IN.zoom, dz = LK_IN.eye.z - LK_ROOM_Z;
	double hy = dz * tv, hx = hy * w / (double)h;
	sc->pic_x0 = LK_IN.eye.x - hx;
	sc->pic_y0 = LK_IN.eye.y - hy;
	sc->pic_w = 2.0 * hx;
	sc->pic_h = 2.0 * hy;
	sc->room_hx = hx + 0.02;
	sc->room_y0 = fmin(0.0, LK_IN.eye.y - hy - 0.02);
	sc->room_y1 = fmax(2.45, LK_IN.eye.y + hy + 0.02);
}

static void lk_tex_of(struct lk_tex *t, cairo_surface_t *s);

/* The room, from the room tracer's own numbers, put behind the front door;
 * and end, the camera at the end of the walk. False if there is no room
 * up yet, or no picture of where the walk ends. */
static bool lk_room_build(struct lk_room *rm, int w, int h, int view, cairo_surface_t *inside,
	const vec3 edge[5], struct lk_cam *end)
{
	memset(rm, 0, sizeof *rm);
	struct room_render *rr = room_for_whole(w, h);
	if (!rr || !rr->background || !inside || rr->w < 8 || rr->h < 8) {
		return false;
	}
	double aspect = (double)rr->w / (double)rr->h;
	/* the cameras the room was traced through: solving them again here
	 * wrote the tracer's globals (g_rm_aspect, g_rm_zf, g_rm_sh_hx) from
	 * the painting thread while a worker could be tracing another room */
	struct room_cam plan = rr->cam, shelf = rr->shelf_cam;
	struct room_cam cam = view == DESK_VIEW_CONSOLE ? rr->console_cam : shelf;
	rm->s = LK_RM_UNIT;
	/* the front wall stands a little behind where the bookcase is looked at
	 * from, and the door is in the middle of it */
	rm->t = v3(0.0, -rm->s * RM_FLOOR, -LK_WALL - LK_RM_BEHIND - rm->s * shelf.eye.z);
	rm->fy = RM_FLOOR;
	rm->cy = RM_FLOOR + 2.60 / rm->s;
	rm->fz = (-LK_WALL - rm->t.z) / rm->s;
	rm->bz = RM_SH_ZB - 0.30;
	rm->wx = fmax(60.0, g_rm_sh_hx + 25.0);
	rm->desk_hx = RM_HX;
	rm->desk_z0 = RM_ZF;
	rm->desk_z1 = RM_ZN;
	int n = 0;
#define LK_RM_BOX(x0, y0, z0, x1, y1, z1, k) \
	do { rm->box[n].lo = v3((x0), (y0), (z0)); rm->box[n].hi = v3((x1), (y1), (z1)); \
		rm->box[n].kind = (k); n++; } while (0)
	/* the desk: its top, the apron the drawer is in, four legs */
	LK_RM_BOX(-RM_HX, -RM_TOP_T, RM_ZF, RM_HX, 0.0, RM_ZN, LK_RM_DESK);
	double ax = RM_HX - 0.30, az0 = RM_ZF + 0.40, az1 = RM_ZN - 0.25, ay = RM_DR_BOT - 0.15;
	LK_RM_BOX(-ax, ay, az0, ax, -RM_TOP_T, az1, LK_RM_DESK);
	for (int i = 0; i < 4; i++) {
		double lx = (i & 1) ? ax - 1.3 : -ax, lz = (i & 2) ? az1 - 1.3 : az0;
		LK_RM_BOX(lx, RM_FLOOR, lz, lx + 1.3, ay, lz + 1.3, LK_RM_LEG);
	}
	/* the bookcase, and its cornice */
	LK_RM_BOX(-g_rm_sh_hx, RM_FLOOR, RM_SH_ZB, g_rm_sh_hx, RM_SH_TOP, RM_SH_ZF, LK_RM_CASE);
	LK_RM_BOX(-g_rm_sh_hx - RM_SH_CORN_P, RM_SH_TOP, RM_SH_ZB, g_rm_sh_hx + RM_SH_CORN_P,
		RM_SH_CORN_Y, RM_SH_ZF + RM_SH_CORN_P, LK_RM_CASE);
	/* the post box on the desk */
	LK_RM_BOX(RM_PB_CX - RM_PB_HX, 0.0, RM_PB_CZ - RM_PB_HZ, RM_PB_CX + RM_PB_HX,
		RM_PB_SHOULDER + RM_PB_CAP_T, RM_PB_CZ + RM_PB_HZ, LK_RM_POST);
	if (view == DESK_VIEW_CONSOLE) {
		/* the Control Panel, out, as it is looked at */
		double dz = cc_dz(1.0, aspect);
		LK_RM_BOX(RM_CC_X0, 0.0, RM_CC_ZB + dz, RM_CC_X1, RM_CC_TY1, RM_CC_PZ0 + dz,
			LK_RM_CONSOLE);
	}
#undef LK_RM_BOX
	rm->nbox = n;
	/* the pictures: the view the walk ends on, laid over the screen the
	 * way the compositor lays it; the desk from above, as it was traced */
	double sx = (double)w / rr->w, sy = (double)h / rr->h;
	double scale = sx < sy ? sy : sx;
	double ox = (w - rr->w * scale) * 0.5, oy = (h - rr->h * scale) * 0.5;
	struct lk_proj *pj = &rm->proj[0];
	pj->cam = cam;
	lk_tex_of(&pj->tex, inside);
	double kx = (double)pj->tex.w / w, ky = (double)pj->tex.h / h;
	pj->x0 = ox * kx;
	pj->y0 = oy * ky;
	pj->sw = rr->w * scale * kx;
	pj->sh = rr->h * scale * ky;
	pj->on = pj->tex.px != NULL;
	if (view != DESK_VIEW_SHELF && rr->shelf) {
		pj = &rm->proj[1];
		pj->cam = shelf;
		lk_tex_of(&pj->tex, rr->shelf);
		pj->x0 = pj->y0 = 0.0;
		pj->sw = pj->tex.w;
		pj->sh = pj->tex.h;
		pj->on = pj->tex.px != NULL;
		rm->keep[1] = rr->shelf;
	}
	pj = &rm->proj[2];
	pj->cam = plan;
	lk_tex_of(&pj->tex, rr->background);
	pj->x0 = pj->y0 = 0.0;
	pj->sw = pj->tex.w;
	pj->sh = pj->tex.h;
	pj->on = pj->tex.px != NULL;
	rm->keep[0] = rr->background;
	rm->wall = vmul(vadd(edge[2], edge[4]), 0.5);
	rm->ceil = vmul(rm->wall, 0.55);
	/* and the eye that took the first, in the street's metres */
	end->eye = vadd(rm->t, vmul(cam.eye, rm->s));
	end->yaw = 0.0;
	end->pitch = cam.pitch - M_PI * 0.5;
	end->f = rr->w * scale / cam.fov_x;
	end->cx = ox + rr->w * scale * 0.5;
	end->cy = oy + rr->h * scale * 0.5;
	rm->on = rm->proj[0].on;
	return rm->on;
}

/* ---- tracing a frame, on every core ---- */

struct lk_job {
	const struct lk_scene *sc;
	struct lk_cam cam;
	unsigned char *px;
	int stride, W, H;
	unsigned char *aa;
	int pass, next;
	pthread_mutex_t lock;
	/* the fine trace, on threads of its own: at the back of the queue
	 * (thread_to_the_back), and given up at the next row once stop is
	 * set */
	bool back;
	atomic_bool *stop;
};

static uint32_t lk_pack(vec3 c, int x, int y)
{
	double dz = (room_hash(x, y + 0x7777) - 0.5) / 255.0;
	return pack_rgb(c.x + dz, c.y + dz, c.z + dz);
}

static void lk_row(struct lk_job *j, int y)
{
	uint32_t *row = (uint32_t *)(void *)(j->px + (size_t)y * j->stride);
	unsigned char *ar = j->aa + (size_t)y * j->W;
	for (int x = 0; x < j->W; x++) {
		if (j->pass == 0) {
			int a;
			vec3 c = lk_sample(j->sc, j->cam.eye, lk_cam_dir(&j->cam, x + 0.5, y + 0.5), &a);
			ar[x] = (unsigned char)a;
			row[x] = lk_pack(c, x, y);
			continue;
		}
		/* where what the pixel shows changes, four samples instead of one */
		unsigned char a = ar[x];
		bool edge = (x > 0 && ar[x - 1] != a) || (x + 1 < j->W && ar[x + 1] != a)
			|| (y > 0 && ar[x - j->W] != a) || (y + 1 < j->H && ar[x + j->W] != a);
		if (!edge) {
			continue;
		}
		vec3 sum = v3(0, 0, 0);
		for (int s = 0; s < 4; s++) {
			int b;
			sum = vadd(sum, lk_sample(j->sc, j->cam.eye,
				lk_cam_dir(&j->cam, x + SS_OFF[s][0], y + SS_OFF[s][1]), &b));
		}
		row[x] = lk_pack(vmul(sum, 0.25), x, y);
	}
}

/* a fine trace no longer wanted, or the session ending under it */
static bool lk_job_stopped(const struct lk_job *j)
{
	return (j->stop && atomic_load(j->stop)) || (j->back && room_stopping());
}

static void *lk_worker(void *arg)
{
	struct lk_job *j = arg;
	if (j->back) {
		thread_to_the_back();
	}
	for (;;) {
		if (lk_job_stopped(j)) {
			break;
		}
		pthread_mutex_lock(&j->lock);
		int y = j->next++;
		pthread_mutex_unlock(&j->lock);
		if (y >= j->H) {
			break;
		}
		lk_row(j, y);
	}
	return NULL;
}

static void lk_run(struct lk_job *j)
{
	j->next = 0;
	pthread_mutex_init(&j->lock, NULL);
	int n = bake_threads();
	pthread_t th[8];
	int started = 0;
	for (int i = 0; i + 1 < n; i++) {
		if (pthread_create(&th[started], NULL, lk_worker, j) == 0) {
			started++;
		}
	}
	lk_worker(j);
	for (int i = 0; i < started; i++) {
		pthread_join(th[i], NULL);
	}
	pthread_mutex_destroy(&j->lock);
}

/* The glow round what is bright -- the lantern, the glass, the shine on
 * the brass -- and the corners let go dark, the way a lens has it. A
 * frame with a margin (mx, my pixels each side, beyond the screen's edges)
 * has its corners where the screen's are; one at res of the screen's size
 * has its glow as wide as at the screen's. */
static void lk_post(unsigned char *px, int stride, int W, int H, double amount, int mx, int my,
	double res)
{
	if (amount <= 0.0) {
		return;
	}
	int blk = (int)lround(clampf(4.0 * res, 1.0, 4.0));
	int bw = (W + blk - 1) / blk, bh = (H + blk - 1) / blk;
	float *b = calloc((size_t)bw * bh * 3, sizeof *b);
	float *tmp = calloc((size_t)bw * bh * 3, sizeof *tmp);
	if (!b || !tmp) {
		free(b);
		free(tmp);
		return;
	}
	for (int y = 0; y < H; y++) {
		const uint32_t *row = (const uint32_t *)(const void *)(px + (size_t)y * stride);
		float *br = b + (size_t)(y / blk) * bw * 3;
		for (int x = 0; x < W; x++) {
			uint32_t p = row[x];
			double r = ((p >> 16) & 0xff) / 255.0, g = ((p >> 8) & 0xff) / 255.0, bb = (p & 0xff) / 255.0;
			double l = 0.2126 * r + 0.7152 * g + 0.0722 * bb;
			double k = fmax(l - 0.62, 0.0) / 0.38 / (blk * blk);
			if (k > 0.0) {
				float *d = br + (x / blk) * 3;
				d[0] += (float)(r * k);
				d[1] += (float)(g * k);
				d[2] += (float)(bb * k);
			}
		}
	}
	blur_pass_rect(b, tmp, bw, bh, 1);
	blur_pass_rect(b, tmp, bw, bh, 2);
	blur_pass_rect(b, tmp, bw, bh, 4);
	blur_pass_rect(b, tmp, bw, bh, 8);
	int iw = W - 2 * mx, ih = H - 2 * my;
	double ax = 1.0 / (iw * 0.5), ay = 1.0 / (ih * 0.5);
	double aspect = (double)iw / ih;
	for (int y = 0; y < H; y++) {
		uint32_t *row = (uint32_t *)(void *)(px + (size_t)y * stride);
		double fy = (y + 0.5) / blk - 0.5;
		int y0 = (int)floor(fy);
		double ty = fy - y0;
		int ya = y0 < 0 ? 0 : y0 >= bh ? bh - 1 : y0, yb = y0 + 1 >= bh ? bh - 1 : y0 + 1 < 0 ? 0 : y0 + 1;
		double vy = (y - H * 0.5) * ay;
		for (int x = 0; x < W; x++) {
			double fx = (x + 0.5) / blk - 0.5;
			int x0 = (int)floor(fx);
			double tx = fx - x0;
			int xa = x0 < 0 ? 0 : x0 >= bw ? bw - 1 : x0, xb = x0 + 1 >= bw ? bw - 1 : x0 + 1 < 0 ? 0 : x0 + 1;
			const float *p00 = b + ((size_t)ya * bw + xa) * 3, *p10 = b + ((size_t)ya * bw + xb) * 3;
			const float *p01 = b + ((size_t)yb * bw + xa) * 3, *p11 = b + ((size_t)yb * bw + xb) * 3;
			double gl[3];
			for (int c = 0; c < 3; c++) {
				gl[c] = (p00[c] * (1 - tx) + p10[c] * tx) * (1 - ty) + (p01[c] * (1 - tx) + p11[c] * tx) * ty;
			}
			uint32_t p = row[x];
			double r = ((p >> 16) & 0xff) / 255.0, g = ((p >> 8) & 0xff) / 255.0, bb = (p & 0xff) / 255.0;
			double vx = (x - W * 0.5) * ax * fmin(aspect, 1.8) / 1.6;
			double vig = 1.0 - 0.38 * amount * smoothstepf(0.35, 1.35, sqrt(vx * vx + vy * vy));
			r = r * vig + gl[0] * 1.6 * amount;
			g = g * vig + gl[1] * 1.6 * amount;
			bb = bb * vig + gl[2] * 1.6 * amount;
			row[x] = lk_pack(v3(r, g, bb), x, y);
		}
	}
	free(b);
	free(tmp);
}

/* the scene from cam, W x H pixels, the edges done finely or not -- a fine
 * one being the background's (lk_bg_start) or a film's, NULL if it was
 * stopped -- with a margin of mx, my pixels past the screen's edges, at
 * res of the screen's size (lk_post) */
static cairo_surface_t *lk_trace_framed(const struct lk_scene *sc, const struct lk_cam *cam,
	int W, int H, bool fine, atomic_bool *stop, int mx, int my, double res)
{
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
	unsigned char *aa = malloc((size_t)W * H);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS || !aa) {
		free(aa);
		return img;
	}
	cairo_surface_flush(img);
	struct lk_scene *rs = malloc(sizeof *rs);
	if (rs) {
		*rs = *sc;
		rs->rough = !fine;
		sc = rs;
	}
	struct lk_job j = { .sc = sc, .cam = *cam, .px = cairo_image_surface_get_data(img),
		.stride = cairo_image_surface_get_stride(img), .W = W, .H = H, .aa = aa,
		.back = fine, .stop = stop };
	j.pass = 0;
	lk_run(&j);
	/* in 2D: the edges as they come, and no glow */
	bool flat = desk_gfx_level() == DESK_GFX_2D;
	bool stopped = lk_job_stopped(&j);
	if (fine && !flat && !stopped) {
		j.pass = 1;
		lk_run(&j);
		stopped = lk_job_stopped(&j);
	}
	if (!flat && !stopped) {
		lk_post(j.px, j.stride, W, H, sc->post, mx, my, res);
	}
	cairo_surface_mark_dirty(img);
	free(aa);
	free(rs);
	if (stopped) {
		cairo_surface_destroy(img);
		return NULL;
	}
	return img;
}

static cairo_surface_t *lk_trace(const struct lk_scene *sc, const struct lk_cam *cam, int W, int H,
	bool fine, atomic_bool *stop)
{
	return lk_trace_framed(sc, cam, W, H, fine, stop, 0, 0, 1.0);
}

/* ---- the pictures that go on things ---- */

/* the board: a paler wood than the door, a gilt line round it, the picture
 * in a brass ring, the name painted on in the brown of the door; and,
 * apart, where the gold is (it shines, and stands up off the board) */
/* A birthday: a satin sash across the board's top corner, the words on it
 * in gilt. */
static void lk_paint_sash(cairo_t *c, cairo_t *gold, double tw, double th)
{
	double u = th;
	double band = u * 0.115;
	/* along the line from part way along the top to part way down the side */
	double x0 = tw * 0.66, y1 = th * 0.46;
	double ang = atan2(y1, tw - x0);
	double len = hypot(tw - x0, y1) + band * 2.0;
	double mx = (x0 + tw) * 0.5, my = y1 * 0.5;
	for (int pass = 0; pass < 2; pass++) {
		cairo_t *k = pass ? gold : c;
		cairo_save(k);
		cairo_rectangle(k, 0, 0, tw, th);
		cairo_clip(k);
		cairo_translate(k, mx, my);
		cairo_rotate(k, ang);
		if (pass == 0) {
			/* its shadow on the board, then the satin, sheen along it */
			cairo_set_source_rgba(k, 0.05, 0.02, 0.01, 0.35);
			cairo_rectangle(k, -len * 0.5, -band * 0.5 + u * 0.012, len, band);
			cairo_fill(k);
			cairo_pattern_t *g = cairo_pattern_create_linear(0, -band * 0.5, 0, band * 0.5);
			cairo_pattern_add_color_stop_rgb(g, 0.0, 0.52, 0.04, 0.06);
			cairo_pattern_add_color_stop_rgb(g, 0.35, 0.86, 0.16, 0.18);
			cairo_pattern_add_color_stop_rgb(g, 0.55, 0.70, 0.08, 0.11);
			cairo_pattern_add_color_stop_rgb(g, 1.0, 0.42, 0.03, 0.05);
			cairo_set_source(k, g);
			cairo_rectangle(k, -len * 0.5, -band * 0.5, len, band);
			cairo_fill(k);
			cairo_pattern_destroy(g);
		}
		/* gilt edges, and the words */
		cairo_set_source_rgba(k, 1, 1, 1, 1);
		cairo_rectangle(k, -len * 0.5, -band * 0.5 + u * 0.006, len, u * 0.006);
		cairo_rectangle(k, -len * 0.5, band * 0.5 - u * 0.012, len, u * 0.006);
		cairo_fill(k);
		show_text_fit_as(k, DESK_FONT_ENGRAVE, "HAPPY BIRTHDAY", u * 0.02, band * 0.27,
			band * 0.62, true, 1, 1, 1, 1, 0, len - band * 3.2);
		cairo_restore(k);
	}
}

static void lk_paint_sign(cairo_t *c, cairo_t *gold, double tw, double th, const char *name,
	cairo_surface_t *picture, bool birthday)
{
	fill_wood_scaled(c, 0, 0, tw, th, tw / 300.0, tw / 300.0);
	cairo_set_source_rgba(c, 1.0, 0.88, 0.66, 0.50);
	cairo_paint(c);
	cairo_pattern_t *sh = cairo_pattern_create_radial(tw * 0.5, th * 0.45, th * 0.2, tw * 0.5,
		th * 0.45, tw * 0.62);
	cairo_pattern_add_color_stop_rgba(sh, 0.0, 0, 0, 0, 0.0);
	cairo_pattern_add_color_stop_rgba(sh, 1.0, 0.10, 0.05, 0.02, 0.35);
	cairo_set_source(c, sh);
	cairo_paint(c);
	cairo_pattern_destroy(sh);
	double u = th;   /* the board's height, the unit of everything on it */
	/* the gilt line round it */
	cairo_set_line_width(gold, u * 0.012);
	cairo_set_source_rgba(gold, 1, 1, 1, 1);
	desk_rounded(gold, u * 0.045, u * 0.045, tw - u * 0.09, th - u * 0.09, u * 0.03);
	cairo_stroke(gold);
	/* the eyelets the cords go in */
	for (int s = -1; s <= 1; s += 2) {
		double ex = tw * 0.5 + s * LK_CORD_X / LK_SIGN_W * tw, ey = u * 0.075;
		cairo_set_source_rgb(c, 0.06, 0.03, 0.01);
		cairo_arc(c, ex, ey, u * 0.012, 0, 2 * M_PI);
		cairo_fill(c);
		cairo_new_sub_path(gold);
		cairo_arc(gold, ex, ey, u * 0.026, 0, 2 * M_PI);
		cairo_new_sub_path(gold);
		cairo_arc_negative(gold, ex, ey, u * 0.012, 2 * M_PI, 0);
		cairo_fill(gold);
	}
	/* the picture, in a brass ring */
	double pcx = tw * 0.5, pcy = th * 0.41, pr = th * 0.235;
	cairo_save(c);
	cairo_arc(c, pcx, pcy, pr, 0, 2 * M_PI);
	cairo_clip(c);
	if (picture) {
		double iw = cairo_image_surface_get_width(picture);
		double ih = cairo_image_surface_get_height(picture);
		double k = 2.0 * pr / fmin(iw, ih);
		cairo_set_source_rgb(c, 0.86, 0.82, 0.74);
		cairo_paint(c);
		cairo_translate(c, pcx - iw * k * 0.5, pcy - ih * k * 0.5);
		cairo_scale(c, k, k);
		cairo_set_source_surface(c, picture, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(c), CAIRO_FILTER_GOOD);
		cairo_paint(c);
	} else {
		/* no picture: the first letter, as on a sheet with no icon */
		cairo_set_source_rgb(c, 0.80, 0.74, 0.62);
		cairo_paint(c);
		char ini[8] = "";
		cal_letter(name && *name ? name : "?", ini);
		for (char *q = ini; *q; q++) {
			if (*q >= 'a' && *q <= 'z') {
				*q = (char)(*q - 'a' + 'A');
			}
		}
		show_text_as(c, DESK_FONT_DISPLAY, ini, pcx, pcy + pr * 0.36, pr * 1.05, true,
			0.36, 0.26, 0.15, 0.90, 0);
	}
	cairo_restore(c);
	cairo_pattern_t *gs = cairo_pattern_create_linear(pcx - pr, pcy - pr, pcx, pcy);
	cairo_pattern_add_color_stop_rgba(gs, 0.0, 1, 1, 1, 0.18);
	cairo_pattern_add_color_stop_rgba(gs, 1.0, 1, 1, 1, 0.0);
	cairo_set_source(c, gs);
	cairo_arc(c, pcx, pcy, pr, 0, 2 * M_PI);
	cairo_fill(c);
	cairo_pattern_destroy(gs);
	cairo_set_source_rgba(c, 0.05, 0.03, 0.01, 0.5);
	cairo_set_line_width(c, u * 0.010);
	cairo_arc(c, pcx, pcy, pr + u * 0.004, 0, 2 * M_PI);
	cairo_stroke(c);
	cairo_set_source_rgba(gold, 1, 1, 1, 1);
	cairo_set_line_width(gold, u * 0.030);
	cairo_arc(gold, pcx, pcy, pr + u * 0.017, 0, 2 * M_PI);
	cairo_stroke(gold);
	/* the name, in gold, as large as fits */
	const char *nm = name && *name ? name : "";
	double size = u * 0.150;
	cairo_save(c);
	desk_font_select(c, DESK_FONT_DISPLAY, nm, true, false);
	cairo_set_font_size(c, size);
	cairo_text_extents_t te;
	cairo_text_extents(c, nm, &te);
	if (te.x_advance > tw * 0.80) {
		size *= tw * 0.80 / te.x_advance;
	}
	cairo_restore(c);
	double ny = th * 0.865;
	show_text_as(c, DESK_FONT_DISPLAY, nm, pcx, ny, size, true, 0.17, 0.09, 0.04, 0.96, 0);
	if (birthday) {
		lk_paint_sash(c, gold, tw, th);
	}
}

/* the plate: what is cut in it, over the slot -- the word, two screws */
static void lk_paint_plate(cairo_t *c, double tw, double th)
{
	double k = tw / (LK_PL_X1 - LK_PL_X0);
	double band0 = (LK_PL_Y1 - 0.0035) , band1 = LK_WIN_Y1 + 0.0035;
	double cy = ((LK_PL_Y1 - (band0 + band1) * 0.5)) * k;
	double bh = (band0 - band1) * k;
	show_text_as(c, DESK_FONT_ENGRAVE, TR("PASSWORD"), tw * 0.5, cy + bh * 0.30, bh * 0.66,
		true, 1, 1, 1, 1, 0);
	for (int s = 0; s < 2; s++) {
		double sx = s ? tw - 0.012 * k : 0.012 * k;
		cairo_set_source_rgba(c, 1, 1, 1, 0.55);
		cairo_set_line_width(c, 0.0008 * k);
		cairo_arc(c, sx, cy, 0.0036 * k, 0, 2 * M_PI);
		cairo_stroke(c);
		cairo_set_line_width(c, 0.0010 * k);
		cairo_move_to(c, sx - 0.0028 * k, cy - 0.0012 * k);
		cairo_line_to(c, sx + 0.0028 * k, cy + 0.0012 * k);
		cairo_stroke(c);
	}
	(void)th;
}

/* ---- what is kept ---- */

struct lk_frame {
	cairo_surface_t *img;
	struct lk_cam cam;
	double door, key, lamp, post;
	unsigned mood;
	bool fine;
};

static struct {
	int w, h;
	double k;
	char name[128];
	const void *picture;
	char label[64];
	bool birthday;
	cairo_surface_t *sign, *gold, *plate;
	struct lk_frame rest, last;
	int still;
	double scale;      /* of the screen's pixels, while the eye moves (kept) */
} g_lock;

static void lk_bg_collect(bool wait);
static void lk_bg_stop(void);
static void lkf_canvas_free(void);

static void lk_frame_drop(struct lk_frame *f)
{
	if (f->img) {
		cairo_surface_destroy(f->img);
	}
	memset(f, 0, sizeof *f);
}

void desk_lock_forget(void)
{
	lk_bg_stop();
	lk_frame_drop(&g_lock.rest);
	lk_frame_drop(&g_lock.last);
	lkf_canvas_free();
	if (g_lock.sign) {
		cairo_surface_destroy(g_lock.sign);
	}
	if (g_lock.gold) {
		cairo_surface_destroy(g_lock.gold);
	}
	if (g_lock.plate) {
		cairo_surface_destroy(g_lock.plate);
	}
	/* how fine this machine keeps the frame rate at: where the next door
	 * begins */
	double scale = g_lock.scale;
	memset(&g_lock, 0, sizeof g_lock);
	g_lock.scale = scale;
}

static void lk_tex_of(struct lk_tex *t, cairo_surface_t *s)
{
	memset(t, 0, sizeof *t);
	if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
		return;
	}
	cairo_surface_flush(s);
	t->px = cairo_image_surface_get_data(s);
	t->w = cairo_image_surface_get_width(s);
	t->h = cairo_image_surface_get_height(s);
	t->stride = cairo_image_surface_get_stride(s);
	t->a8 = cairo_image_surface_get_format(s) == CAIRO_FORMAT_A8;
}

/* the board and the plate drawn for this screen, about as fine as the
 * eye at the door will see them */
static void lk_prepare(int w, int h, double k, const char *name, cairo_surface_t *picture,
	bool birthday)
{
	const char *label = TR("PASSWORD");
	if (g_lock.sign && g_lock.w == w && g_lock.h == h && fabs(g_lock.k - k) < 1e-6
		&& !strcmp(g_lock.name, name) && g_lock.picture == picture && !strcmp(g_lock.label, label)
		&& g_lock.birthday == birthday) {
		return;
	}
	desk_lock_forget();
	if (g_lk_lin[255] == 0.0f) {
		for (int i = 0; i < 256; i++) {
			g_lk_lin[i] = (float)pow(i / 255.0, 2.2);
		}
	}
	g_lock.w = w;
	g_lock.h = h;
	g_lock.k = k;
	snprintf(g_lock.name, sizeof g_lock.name, "%s", name);
	snprintf(g_lock.label, sizeof g_lock.label, "%s", label);
	g_lock.picture = picture;
	g_lock.birthday = birthday;
	struct desk_lock_look rest;
	memset(&rest, 0, sizeof rest);
	struct lk_cam cam;
	lk_camera(w, h, &rest, &cam);
	double px_per_m = cam.f / (cam.eye.z - LK_DZ) * k;
	int tw = (int)clampf(ceil(LK_SIGN_W * px_per_m * 1.6), 320.0, 1800.0);
	int th = (int)ceil(tw * LK_SIGN_H / LK_SIGN_W);
	g_lock.sign = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
	g_lock.gold = cairo_image_surface_create(CAIRO_FORMAT_A8, tw, th);
	cairo_t *c = cairo_create(g_lock.sign), *gc = cairo_create(g_lock.gold);
	lk_paint_sign(c, gc, tw, th, name, picture, birthday);
	cairo_destroy(c);
	cairo_destroy(gc);
	int pw = (int)clampf(ceil((LK_PL_X1 - LK_PL_X0) * px_per_m * 2.4), 240.0, 1400.0);
	int ph = (int)ceil(pw * (LK_PL_Y1 - LK_PL_Y0) / (LK_PL_X1 - LK_PL_X0));
	g_lock.plate = cairo_image_surface_create(CAIRO_FORMAT_A8, pw, ph);
	c = cairo_create(g_lock.plate);
	lk_paint_plate(c, pw, ph);
	cairo_destroy(c);
}

static bool lk_same(const struct lk_frame *f, const struct lk_cam *c, const struct lk_scene *sc)
{
	return f->img && f->cam.eye.x == c->eye.x && f->cam.eye.y == c->eye.y
		&& f->cam.eye.z == c->eye.z && f->cam.yaw == c->yaw && f->cam.pitch == c->pitch
		&& f->cam.f == c->f && f->door == sc->door && f->key == sc->key && f->lamp == sc->lamp
		&& f->post == sc->post && f->mood == sc->rig.key;
}

static double lk_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* The fine trace of wherever the eye has come to rest is the slow one --
 * most of a second on a big screen -- so it is done on a thread of its
 * own while the frame there is shown coarse, and put in place when it is
 * done. It holds a reference to the picture of the room it may need. It
 * goes at the back of the queue, and when it is no longer wanted -- the
 * door gone, or another size -- it is stopped, not waited out: on an old
 * machine, behind a door dressed for a day, it is many seconds, and the
 * coarse frames that went at its pace and the frame that waited for it
 * stood the door still for that long (seven seconds, Halloween's, at 2010's
 * speed in the lab). */
static struct {
	pthread_t th;
	bool running;
	atomic_bool done, stop;
	struct lk_scene sc;
	struct lk_cam cam;       /* in the w x h the frame is for */
	struct lk_cam px;        /* and in the picture's own pixels */
	int W, H;
	bool rest;
	cairo_surface_t *keep, *keep2[2], *img;
} g_lk_bg;

static void *lk_bg_worker(void *arg)
{
	(void)arg;
	g_lk_bg.img = lk_trace(&g_lk_bg.sc, &g_lk_bg.px, g_lk_bg.W, g_lk_bg.H, true, &g_lk_bg.stop);
	atomic_store(&g_lk_bg.done, true);
	return NULL;
}

static void lk_bg_start(const struct lk_scene *sc, const struct lk_cam *cam, int W, int H, double k,
	bool rest, cairo_surface_t *inside)
{
	if (g_lk_bg.running) {
		return;
	}
	g_lk_bg.sc = *sc;
	g_lk_bg.cam = *cam;
	g_lk_bg.px = *cam;
	g_lk_bg.px.f *= k;
	g_lk_bg.px.cx *= k;
	g_lk_bg.px.cy *= k;
	g_lk_bg.W = W;
	g_lk_bg.H = H;
	g_lk_bg.rest = rest;
	g_lk_bg.img = NULL;
	g_lk_bg.keep = inside ? cairo_surface_reference(inside) : NULL;
	for (int i = 0; i < 2; i++) {
		g_lk_bg.keep2[i] = sc->room.on && sc->room.keep[i]
			? cairo_surface_reference(sc->room.keep[i]) : NULL;
	}
	atomic_store(&g_lk_bg.done, false);
	atomic_store(&g_lk_bg.stop, false);
	if (pthread_create(&g_lk_bg.th, NULL, lk_bg_worker, NULL) == 0) {
		g_lk_bg.running = true;
	} else {
		if (g_lk_bg.keep) {
			cairo_surface_destroy(g_lk_bg.keep);
			g_lk_bg.keep = NULL;
		}
		for (int i = 0; i < 2; i++) {
			if (g_lk_bg.keep2[i]) {
				cairo_surface_destroy(g_lk_bg.keep2[i]);
				g_lk_bg.keep2[i] = NULL;
			}
		}
	}
}

/* if the fine trace is done (or, wait, whether or not), put it where it
 * goes: the frame at the door, or the last one if the eye is still there */
static void lk_bg_collect(bool wait)
{
	if (!g_lk_bg.running || (!wait && !atomic_load(&g_lk_bg.done))) {
		return;
	}
	pthread_join(g_lk_bg.th, NULL);
	g_lk_bg.running = false;
	if (g_lk_bg.keep) {
		cairo_surface_destroy(g_lk_bg.keep);
		g_lk_bg.keep = NULL;
	}
	for (int i = 0; i < 2; i++) {
		if (g_lk_bg.keep2[i]) {
			cairo_surface_destroy(g_lk_bg.keep2[i]);
			g_lk_bg.keep2[i] = NULL;
		}
	}
	cairo_surface_t *img = g_lk_bg.img;
	g_lk_bg.img = NULL;
	if (!img) {
		return;
	}
	struct lk_frame *f = g_lk_bg.rest ? &g_lock.rest
		: lk_same(&g_lock.last, &g_lk_bg.cam, &g_lk_bg.sc) ? &g_lock.last : NULL;
	if (!f || !g_lock.w) {
		cairo_surface_destroy(img);
		return;
	}
	if (f->img) {
		cairo_surface_destroy(f->img);
	}
	f->img = img;
	f->cam = g_lk_bg.cam;
	f->door = g_lk_bg.sc.door;
	f->key = g_lk_bg.sc.key;
	f->lamp = g_lk_bg.sc.lamp;
	f->post = g_lk_bg.sc.post;
	f->mood = g_lk_bg.sc.rig.key;
	f->fine = true;
}

/* the fine trace no longer wanted: stopped at the row it is on */
static void lk_bg_stop(void)
{
	if (g_lk_bg.running) {
		atomic_store(&g_lk_bg.stop, true);
	}
	lk_bg_collect(true);
}

static bool lkf_awaited(void);

bool desk_lock_settling(void)
{
	lk_bg_collect(false);
	return g_lk_bg.running || lkf_awaited();
}

void desk_lock_settle(void)
{
	lk_bg_collect(true);
}

/* The frame for cam: the one kept at the door, the last one if nothing has
 * moved since, or a new one, as fine as keeps the frame rate. Where the eye
 * comes to rest, a fine one is started on the side -- unless one is coming
 * anyway, made ahead (desk_lock_films). */
static struct lk_frame *lk_frame_for(struct lk_scene *sc, const struct lk_cam *cam, int w, int h,
	double k, cairo_surface_t *inside, bool coming)
{
	lk_bg_collect(false);
	struct desk_lock_look rest;
	memset(&rest, 0, sizeof rest);
	struct lk_cam rc;
	lk_camera(w, h, &rest, &rc);
	int W = (int)ceil(w * k), H = (int)ceil(h * k);
	bool at_rest = sc->door == 0.0 && sc->key == 0.0 && cam->eye.x == rc.eye.x
		&& cam->eye.y == rc.eye.y && cam->eye.z == rc.eye.z && cam->yaw == rc.yaw
		&& cam->pitch == rc.pitch && cam->f == rc.f;
	if (at_rest && g_lock.rest.img) {
		if (g_lock.rest.mood != sc->rig.key && !coming) {
			/* the weather, or the hour, has moved on: the door as it was
			 * until it is traced finely as it is now -- no coarse one
			 * in between, the eye has not moved */
			lk_bg_start(sc, cam, W, H, k, true, inside);
		}
		return &g_lock.rest;
	}
	struct lk_frame *f = &g_lock.last;
	if (lk_same(f, cam, sc)) {
		if (!f->fine && ++g_lock.still >= 2 && !coming) {
			lk_bg_start(sc, cam, W, H, k, at_rest, inside);
		}
		return f;
	}
	g_lock.still = 0;
	if (g_lock.scale <= 0.0) {
		/* to begin with, as coarse as it goes, and finer frame by frame
		 * while the frame rate holds (under the door fading in): begun
		 * at a third of a million pixels, the first door on an old
		 * machine stood still for seconds */
		g_lock.scale = 0.20;
	}
	double s = g_lock.scale;
	int fw = (int)ceil(W * s), fh = (int)ceil(H * s);
	struct lk_cam fc = *cam;
	fc.f *= k * s;
	fc.cx *= k * s;
	fc.cy *= k * s;
	double t0 = lk_now();
	cairo_surface_t *img = lk_trace(sc, &fc, fw, fh, false, NULL);
	double dt = lk_now() - t0;
	/* hold the frame rate: about 25 ms a frame -- a good deal coarser at
	 * once after a frame that took far longer, finer a little at a time */
	g_lock.scale = clampf(g_lock.scale * clampf(sqrt(0.025 / fmax(dt, 1e-4)), 0.5, 1.2),
		0.20, 0.75);
	lk_frame_drop(f);
	f->img = img;
	f->cam = *cam;
	f->door = sc->door;
	f->key = sc->key;
	f->lamp = sc->lamp;
	f->post = sc->post;
	f->mood = sc->rig.key;
	f->fine = false;
	if (at_rest && !coming) {
		lk_bg_start(sc, cam, W, H, k, true, inside);
	}
	return f;
}

/* ---- the moves, filmed ahead ----
 *
 * Moving, the door is traced as it goes, as coarse as keeps the frame
 * rate (lk_frame_for): on a big screen a fifth of its size or less, the
 * name on the board a smudge, and fine only once the eye has stopped and a
 * fine trace has been made -- seconds, on a big screen. But the moves are
 * the same every time: the walk up, leaning in to the key, told no, let
 * in and through, out of the room. So, at Medium and up, they are filmed
 * ahead (desk_lock_films), on a thread at the back of the queue, for the
 * door as it is (whose, the weather, the bounce): frames at half the
 * screen's size (no more than LKF_PX pixels, and smaller where the picture
 * goes by faster than LKF_SLOW), with a margin all round; and where a move
 * begins or ends with the eye still -- at the door, leant in to the key --
 * the picture there at the screen's own size (the stills).
 *
 * Each frame keeps a grid of what it saw, in metres -- what is on the door
 * in the door's own frame, what is on the key in the key's -- and is laid
 * by it (lkf_show): each cell of the grid two triangles, each put where
 * its corners now land, the door swung and the key turned as they are
 * now. Near and far, turned and leant, a frame is where the eye would see
 * it, and the frames either side of the moment fade one into the other as
 * the room's own films do, each giving way toward its edge to the other.
 * What the grid cannot carry is what a frame did not see, and what changes
 * besides where things are -- the light as the door opens, a shine. So
 * the frames are as close as that asks (lkf_bake_film): no more than
 * LKF_SPACING pixels of the picture moved where one scale and a shift do
 * not put it back, more where it goes by too fast for the eye to see a
 * double edge, and none shown more than LKF_ZOOM times bigger or smaller
 * than it was made.
 *
 * What is drawn over the door -- the letters, the caret, the light in the
 * slot, the cords, what moves out there -- is drawn as ever. The room the
 * door opens on is a picture, and it changes (the clock on the bookcase,
 * the needles on the Control Panel): the film has it as it was when the
 * film was made, and the last of the walk in is the picture as it is now,
 * the frame before fading into it (and out of it, at the first of the
 * walk out). DESK_LOCK_FILM_DUMP=<dir> writes the frames as they are made,
 * and says how many there are and why (desk-preview --lock-film). */

static void lk_edges(cairo_surface_t *s, vec3 edge[5]);

/* how big the door is drawn for a w x h screen, of the screen: as big as
 * what is a scene rather than something read (desk_ambient_size) */
static double lk_scale_for(int w, int h)
{
	int ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	return clampf((double)ow / w, 0.25, 3.0);
}

#define LKF_PX 550000.0      /* the most pixels a film's frame has, inside its margin */
#define LKF_MARGIN 0.06      /* that margin, each side, of the frame */
#define LKF_SPACING 48.0     /* frames as far apart as that many pixels moved (lkf_moved) */
#define LKF_ZOOM 1.35        /* and no more than that much bigger or smaller */
#define LKF_SLOW 800.0       /* pixels a second the picture goes by at a frame's full size */
#define LKF_MOST 64          /* and no more than that many to a move */
#define LKF_STEPS 240        /* the steps a move's change is counted in */
#define LKF_GX 41            /* the grid of what a frame saw kept with it */
#define LKF_GY 26

/* what is made ahead: the two stills, then a film for each move */
enum { LKF_REST, LKF_HOLD, LKF_STILLS };
#define LKF_JOBS (LKF_STILLS + DESK_LOCK_MOVES)
#define LKF_JOB_OF(move) (LKF_STILLS + (move))

/* everything a picture of the door is made from */
struct lkf_key {
	int w, h, level;
	int view;                 /* the room it opens on (OPEN, OUT): -1 none */
	double k, steady;
	unsigned sign, mood, room;
};

/* where a point a frame saw is: fixed, on the door, on the key */
enum { LKF_FIXED = 0, LKF_DOOR, LKF_KEY };

struct lkf_frame {
	cairo_surface_t *img;     /* NULL: the room's picture, as it is now */
	double p;                 /* how far into its move, by what changes */
	struct lk_cam cam;        /* in its own pixels */
	double door, key;         /* the door's swing and the key's turn it was made at */
	/* at each point of a grid over it, corner to corner, what the ray met:
	 * in metres, or (on the door or the key) in the door's own frame */
	vec3 ref[LKF_GX * LKF_GY];
	unsigned char on[LKF_GX * LKF_GY];
};

struct lkf_film {
	struct lkf_key key;
	double len;
	double prog[LKF_STEPS + 1];
	int n;
	struct lkf_frame *f;
};

/* what the thread making them needs, made on the compositor's */
struct lkf_job {
	bool want;
	struct lkf_key key;
	struct desk_door_mood mood;
	cairo_surface_t *sign, *gold, *plate;
	cairo_surface_t *pic;     /* the room's picture (OPEN, OUT), or NULL */
	bool room_on;
	struct lk_room room;      /* its own pictures referenced */
	struct lk_cam end;
	vec3 edge[5];
};

static struct {
	pthread_mutex_t lock;
	bool running;
	int busy;                 /* the job being made, or -1 */
	atomic_bool stop;         /* and it is no longer wanted */
	struct lkf_job job[LKF_JOBS];
	struct lkf_frame *still[LKF_STILLS];
	struct lkf_key still_key[LKF_STILLS];
	struct lkf_film *film[DESK_LOCK_MOVES];
	bool awaited;             /* a still the door is showing is on its way */
} g_lkf = { .lock = PTHREAD_MUTEX_INITIALIZER, .busy = -1 };

static bool lkf_key_eq(const struct lkf_key *a, const struct lkf_key *b)
{
	return a->w == b->w && a->h == b->h && a->level == b->level && a->view == b->view
		&& fabs(a->k - b->k) < 1e-9 && a->steady == b->steady && a->sign == b->sign
		&& a->mood == b->mood && a->room == b->room;
}

static unsigned lkf_hash(unsigned h, const void *p, size_t n)
{
	const unsigned char *c = p;
	for (size_t i = 0; i < n; i++) {
		h = (h ^ c[i]) * 16777619u;
	}
	return h;
}

/* the board and the plate as lk_prepare draws them: whose, the picture,
 * the word on the plate, a birthday's sash */
static unsigned lkf_sign_key(const char *name, const void *picture, bool birthday)
{
	const char *label = TR("PASSWORD");
	unsigned h = lkf_hash(2166136261u, name, strlen(name) + 1);
	h = lkf_hash(h, &picture, sizeof picture);
	h = lkf_hash(h, label, strlen(label) + 1);
	return lkf_hash(h, &birthday, sizeof birthday);
}

static void lkf_key_of(struct lkf_key *key, int w, int h, double k, const struct desk_lock_look *lk,
	unsigned mood)
{
	memset(key, 0, sizeof *key);
	key->w = w;
	key->h = h;
	key->level = (int)desk_gfx_level();
	key->view = -1;
	key->k = k;
	key->steady = lk->steady;
	key->sign = lkf_sign_key(lk->name ? lk->name : "", lk->picture,
		lk->mood.set && (lk->mood.decor & DESK_DECOR_BIRTHDAY));
	key->mood = mood;
}

/* and for a move, the room it opens on, as it is built */
static void lkf_key_room(struct lkf_key *key, int view, int w, int h)
{
	key->view = view;
	struct room_render *rr = view >= 0 ? room_for_whole(w, h) : NULL;
	key->room = rr ? rr->serial : 0;
}

static void lkf_frame_free(struct lkf_frame *f)
{
	if (f && f->img) {
		cairo_surface_destroy(f->img);
	}
	free(f);
}

static void lkf_film_free(struct lkf_film *film)
{
	if (!film) {
		return;
	}
	for (int i = 0; i < film->n; i++) {
		if (film->f[i].img) {
			cairo_surface_destroy(film->f[i].img);
		}
	}
	free(film->f);
	free(film);
}

static void lkf_job_drop(struct lkf_job *j)
{
	cairo_surface_t *s[5] = { j->sign, j->gold, j->plate, j->pic, NULL };
	for (int i = 0; i < 4; i++) {
		if (s[i]) {
			cairo_surface_destroy(s[i]);
		}
	}
	for (int i = 0; i < 2; i++) {
		if (j->room.keep[i]) {
			cairo_surface_destroy(j->room.keep[i]);
		}
	}
	memset(j, 0, sizeof *j);
}

/* a copy of a job for the thread, every picture in it its own */
static void lkf_job_copy(struct lkf_job *to, const struct lkf_job *from)
{
	*to = *from;
	cairo_surface_t **s[4] = { &to->sign, &to->gold, &to->plate, &to->pic };
	for (int i = 0; i < 4; i++) {
		if (*s[i]) {
			cairo_surface_reference(*s[i]);
		}
	}
	for (int i = 0; i < 2; i++) {
		if (to->room.keep[i]) {
			cairo_surface_reference(to->room.keep[i]);
		}
	}
}


/* how far into the film's move t seconds is, and when it is that far */
static double lkf_prog_at(const struct lkf_film *film, double t)
{
	double u = clampf(t / film->len, 0.0, 1.0) * LKF_STEPS;
	int i = (int)u;
	if (i >= LKF_STEPS) {
		return film->prog[LKF_STEPS];
	}
	return film->prog[i] + (film->prog[i + 1] - film->prog[i]) * (u - i);
}

static double lkf_time_at(const struct lkf_film *film, double p)
{
	int i = 0;
	while (i < LKF_STEPS - 1 && film->prog[i + 1] < p) {
		i++;
	}
	double d = film->prog[i + 1] - film->prog[i];
	double u = i + (d > 1e-12 ? clampf((p - film->prog[i]) / d, 0.0, 1.0) : 0.0);
	return film->len * u / LKF_STEPS;
}

/* What the eye sees at a pose, coarsely: a grid of rays, the points they
 * meet, and whether each is on the door (and swings with it). */
#define LKF_PROBE_X 10
#define LKF_PROBE_Y 7
struct lkf_probe {
	int n;
	vec3 p[LKF_PROBE_X * LKF_PROBE_Y];
	unsigned char on[LKF_PROBE_X * LKF_PROBE_Y];
};

/* what a ray met rides on: the key, the door (what is on it too), or
 * nothing that moves */
static int lkf_on(const struct lk_scene *sc, const struct lk_hit *hit)
{
	if (hit->id == LK_KEY) {
		return LKF_KEY;
	}
	if ((hit->id >= LK_DOOR && hit->id <= LK_BRASS)
		|| (hit->id == LK_DECO && hit->face >= 0 && hit->face < sc->ndeco
		&& sc->deco[hit->face].door)) {
		return LKF_DOOR;
	}
	return LKF_FIXED;
}

/* a point on the key, in the door's frame, turned from key a to key b */
static vec3 lkf_key_turn(vec3 q, double a, double b)
{
	vec3 c = v3(LK_KEY_X, LK_KEY_Y, 0.0);
	return vadd(c, rot_z(vsub(q, c), a - b));
}

/* a point of what was seen in a, where it is in b */
static vec3 lkf_carry(vec3 p, int on, const struct lk_scene *a, const struct lk_scene *b)
{
	if (on == LKF_DOOR) {
		return lk_from_door(b, lk_to_door(a, p));
	}
	if (on == LKF_KEY) {
		return lk_from_door(b, lkf_key_turn(lk_to_door(a, p), a->key, b->key));
	}
	return p;
}

static void lkf_probe(const struct lk_scene *sc, const struct lk_cam *cam, int w, int h,
	struct lkf_probe *pr)
{
	pr->n = 0;
	for (int j = 0; j < LKF_PROBE_Y; j++) {
		for (int i = 0; i < LKF_PROBE_X; i++) {
			vec3 d = lk_cam_dir(cam, (i + 0.5) / LKF_PROBE_X * w, (j + 0.5) / LKF_PROBE_Y * h);
			struct lk_hit hit;
			lk_intersect(sc, cam->eye, d, &hit);
			if (hit.t >= 1e8) {
				continue;
			}
			pr->p[pr->n] = vadd(cam->eye, vmul(d, hit.t));
			pr->on[pr->n] = (unsigned char)lkf_on(sc, &hit);
			pr->n++;
		}
	}
}

/* The grid of what a frame from cam, fw x fh of its pixels, saw of sc:
 * corner to corner, where each ray met something (far off, where it met
 * nothing), and what that rides on. */
static void lkf_grid(struct lkf_frame *f, const struct lk_scene *sc, const struct lk_cam *cam,
	int fw, int fh)
{
	f->door = sc->door;
	f->key = sc->key;
	for (int j = 0; j < LKF_GY; j++) {
		for (int i = 0; i < LKF_GX; i++) {
			int k = j * LKF_GX + i;
			vec3 d = lk_cam_dir(cam, (double)i * fw / (LKF_GX - 1), (double)j * fh / (LKF_GY - 1));
			struct lk_hit hit;
			lk_intersect(sc, cam->eye, d, &hit);
			vec3 p = vadd(cam->eye, vmul(d, hit.t < 1e8 ? hit.t : 60.0));
			int on = hit.t < 1e8 ? lkf_on(sc, &hit) : LKF_FIXED;
			f->ref[k] = on == LKF_FIXED ? p : lk_to_door(sc, p);
			f->on[k] = (unsigned char)on;
		}
	}
}

/* where frame f's point k is, with the door and the key as sc has them */
static vec3 lkf_point(const struct lkf_frame *f, int k, const struct lk_scene *sc)
{
	if (f->on[k] == LKF_DOOR) {
		return lk_from_door(sc, f->ref[k]);
	}
	if (f->on[k] == LKF_KEY) {
		return lk_from_door(sc, lkf_key_turn(f->ref[k], f->key, sc->key));
	}
	return f->ref[k];
}

/* How far the picture moves from pose a to pose b where one scale and a
 * shift do not put it back -- the double edges a fade between frames made
 * there would show -- in the screen's pixels: what a saw, carried to b
 * (swung with the door, if on it), the one in eight that moved most; and
 * in all, scale and shift and the rest (*all), as fast as the eye sees it
 * go. */
static double lkf_moved(const struct lkf_probe *pr, const struct lk_scene *sa,
	const struct lk_cam *ca, const struct lk_scene *sb, const struct lk_cam *cb, int w, int h,
	double *all, double *scale)
{
	*all = 0.0;
	*scale = 1.0;
	double X[LKF_PROBE_X * LKF_PROBE_Y], Y[LKF_PROBE_X * LKF_PROBE_Y];
	double U[LKF_PROBE_X * LKF_PROBE_Y], V[LKF_PROBE_X * LKF_PROBE_Y];
	int n = 0;
	double mx = 0.0, my = 0.0, mu = 0.0, mv = 0.0;
	for (int i = 0; i < pr->n; i++) {
		vec3 q = lkf_carry(pr->p[i], pr->on[i], sa, sb);
		double x, y, u, v;
		if (!lk_project(ca, pr->p[i], &x, &y) || !lk_project(cb, q, &u, &v)
			|| u < -0.25 * w || u > 1.25 * w || v < -0.25 * h || v > 1.25 * h) {
			continue;
		}
		X[n] = x;
		Y[n] = y;
		U[n] = u;
		V[n] = v;
		mx += x;
		my += y;
		mu += u;
		mv += v;
		n++;
	}
	if (n < 3) {
		return 0.0;
	}
	mx /= n;
	my /= n;
	mu /= n;
	mv /= n;
	double num = 0.0, den = 0.0;
	for (int i = 0; i < n; i++) {
		num += (X[i] - mx) * (U[i] - mu) + (Y[i] - my) * (V[i] - mv);
		den += (X[i] - mx) * (X[i] - mx) + (Y[i] - my) * (Y[i] - my);
	}
	double s = den > 1e-9 && num > 0.0 ? num / den : 1.0;
	*scale = s;
	double r[LKF_PROBE_X * LKF_PROBE_Y];
	for (int i = 0; i < n; i++) {
		r[i] = hypot(mu + s * (X[i] - mx) - U[i], mv + s * (Y[i] - my) - V[i]);
		*all += hypot(U[i] - X[i], V[i] - Y[i]) / n;
	}
	/* the one in eight that moved most, not the one ray that grazed something */
	int k = n - 1 - n / 8;
	for (int a = 0; a <= k; a++) {
		int m = a;
		for (int b = a + 1; b < n; b++) {
			if (r[b] < r[m]) {
				m = b;
			}
		}
		double tmp = r[a];
		r[a] = r[m];
		r[m] = tmp;
	}
	return r[k];
}

/* the still a move begins or ends on, or -1 */
static int lkf_first_still(int move)
{
	return move == DESK_LOCK_MOVE_LEAN ? LKF_REST
		: move == DESK_LOCK_MOVE_FAIL || move == DESK_LOCK_MOVE_OPEN ? LKF_HOLD : -1;
}

static int lkf_last_still(int move)
{
	return move == DESK_LOCK_MOVE_LEAN ? LKF_HOLD
		: move == DESK_LOCK_MOVE_WALK || move == DESK_LOCK_MOVE_FAIL
		|| move == DESK_LOCK_MOVE_OUT ? LKF_REST : -1;
}

/* The door as job has it, in the pose of look, as desk_draw_lock puts
 * it together, and the eye there (in the screen's w x h); NULL if there
 * was not the memory, or else to be freed. */
static struct lk_scene *lkf_scene(const struct lkf_job *job, struct desk_lock_look *look,
	struct lk_cam *cam)
{
	const struct lkf_key *key = &job->key;
	struct lk_scene *sc = malloc(sizeof *sc);
	if (!sc) {
		return NULL;
	}
	look->mood = job->mood;
	look->steady = key->steady;
	look->inside = job->pic;
	look->inside_view = key->view;
	lk_scene_for(sc, key->w, key->h, look);
	lk_tex_of(&sc->sign, job->sign);
	lk_tex_of(&sc->gold, job->gold);
	lk_tex_of(&sc->plate, job->plate);
	bool room = false;
	if (job->pic) {
		lk_tex_of(&sc->pic, job->pic);
		if (look->open > 0.0 || look->push > 0.0) {
			memcpy(sc->edge, job->edge, sizeof sc->edge);
			if (job->room_on) {
				sc->room = job->room;
				room = true;
			}
		}
	}
	lk_camera_to(key->w, key->h, look, room ? &job->end : NULL, cam);
	return sc;
}

/* One frame of the door: as job has it, the pose of look, at res of the
 * screen's size, with a margin or not; and what it saw, for laying it
 * where another eye would see the same. False if it was stopped. */
static bool lkf_trace(const struct lkf_job *job, struct desk_lock_look *look, double res,
	bool margin, struct lkf_frame *out)
{
	const struct lkf_key *key = &job->key;
	struct lk_cam cam;
	struct lk_scene *sc = lkf_scene(job, look, &cam);
	if (!sc) {
		return false;
	}
	int W = (int)ceil(key->w * key->k), H = (int)ceil(key->h * key->k);
	int iw = (int)ceil(W * res), ih = (int)ceil(H * res);
	int mx = margin ? (int)ceil(iw * LKF_MARGIN) : 0, my = margin ? (int)ceil(ih * LKF_MARGIN) : 0;
	double s = key->k * res;
	struct lk_cam fc = cam;
	fc.f *= s;
	fc.cx = cam.cx * s + mx;
	fc.cy = cam.cy * s + my;
	cairo_surface_t *img = lk_trace_framed(sc, &fc, iw + 2 * mx, ih + 2 * my, true, &g_lkf.stop,
		mx, my, res);
	if (!img || cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		if (img) {
			cairo_surface_destroy(img);
		}
		free(sc);
		return false;
	}
	memset(out, 0, sizeof *out);
	out->img = img;
	out->cam = fc;
	lkf_grid(out, sc, &fc, iw + 2 * mx, ih + 2 * my);
	free(sc);
	return true;
}

/* half the screen's size, and no more than LKF_PX pixels */
static double lkf_res(const struct lkf_key *key)
{
	double W = ceil(key->w * key->k), H = ceil(key->h * key->k);
	return fmin(0.5, sqrt(LKF_PX / fmax(W * H, 1.0)));
}

static struct lkf_frame *lkf_bake_still(const struct lkf_job *job, int which)
{
	struct desk_lock_look look;
	memset(&look, 0, sizeof look);
	if (which == LKF_HOLD) {
		lk_move_pose(DESK_LOCK_MOVE_LEAN, lk_move_len(DESK_LOCK_MOVE_LEAN, false), false, &look);
	}
	struct lkf_frame *f = calloc(1, sizeof *f);
	if (f && !lkf_trace(job, &look, 1.0, false, f)) {
		free(f);
		return NULL;
	}
	return f;
}

/* A move's film: what it changes counted along it, frames spread evenly
 * by that, the stills it begins and ends on (taken here) and the room's
 * picture as it is then (left NULL) where it goes in or comes out. */
static struct lkf_film *lkf_bake_film(const struct lkf_job *job, int move,
	struct lkf_frame *const still[LKF_STILLS])
{
	bool inside = job->key.view >= 0 && job->pic;
	struct lkf_film *film = calloc(1, sizeof *film);
	if (!film) {
		return NULL;
	}
	film->key = job->key;
	film->len = lk_move_len(move, inside);
	/* how much of the picture moves, along the move, step by step */
	int w = job->key.w, h = job->key.h;
	double speed[LKF_STEPS + 1] = { 0.0 }, moved = 0.0;
	struct lk_scene *sa = NULL;
	struct lk_cam ca;
	for (int i = 0; i <= LKF_STEPS; i++) {
		struct desk_lock_look look;
		memset(&look, 0, sizeof look);
		lk_move_pose(move, film->len * i / LKF_STEPS, inside, &look);
		struct lk_cam cb;
		struct lk_scene *sb = lkf_scene(job, &look, &cb);
		if (!sb || atomic_load(&g_lkf.stop)) {
			free(sa);
			free(sb);
			free(film);
			return NULL;
		}
		if (sa) {
			/* Frames LKF_SPACING pixels of that apart -- or, where the
			 * picture goes by fast, as far apart as it goes in a frame and
			 * a half of the screen's: no eye sees a double edge in what is
			 * a smear going by -- and no frame shown more than LKF_ZOOM
			 * times the size it was made at */
			struct lkf_probe pr;
			lkf_probe(sa, &ca, w, h, &pr);
			double all, scale, r = lkf_moved(&pr, sa, &ca, sb, &cb, w, h, &all, &scale);
			double fast = all / (film->len / LKF_STEPS) / 60.0 * 1.5;
			film->prog[i] = film->prog[i - 1] + r / fmax(LKF_SPACING, fast)
				+ fabs(log(scale)) / log(LKF_ZOOM);
			speed[i] = all / (film->len / LKF_STEPS);
			moved += r / fmax(LKF_SPACING, fast);
		}
		free(sa);
		sa = sb;
		ca = cb;
	}
	free(sa);
	double total = film->prog[LKF_STEPS];
	int n = (int)ceil(total) + 1;
	n = n < 2 ? 2 : n > LKF_MOST ? LKF_MOST : n;
	if (getenv("DESK_LOCK_FILM_DUMP")) {
		double top = 0.0;
		for (int i = 0; i <= LKF_STEPS; i++) {
			top = fmax(top, speed[i]);
		}
		printf("  move %d: %.1f frames' worth (%.1f moving, %.1f growing), %d frames; "
			"fastest %.0f px/s\n", move, total, moved, total - moved, n, top);
	}
	film->f = calloc((size_t)n, sizeof *film->f);
	if (!film->f) {
		free(film);
		return NULL;
	}
	film->n = n;
	double res = lkf_res(&job->key);
	int s0 = lkf_first_still(move), s1 = lkf_last_still(move);
	for (int i = 0; i < n; i++) {
		double p = total * i / (n - 1);
		struct lkf_frame *f = &film->f[i];
		int s = i == 0 ? s0 : i == n - 1 ? s1 : -1;
		if (s >= 0) {
			*f = *still[s];
			cairo_surface_reference(f->img);
		} else if ((i == 0 && move == DESK_LOCK_MOVE_OUT && inside)
			|| (i == n - 1 && move == DESK_LOCK_MOVE_OPEN && inside)) {
			/* the room, as it will be then (NULL), seen from where the walk
			 * ends: what its picture saw */
			struct desk_lock_look look;
			memset(&look, 0, sizeof look);
			lk_move_pose(move, i == 0 ? 0.0 : film->len, inside, &look);
			struct lk_cam cam;
			struct lk_scene *sc = lkf_scene(job, &look, &cam);
			if (!sc) {
				film->n = i;
				lkf_film_free(film);
				return NULL;
			}
			int pw = cairo_image_surface_get_width(job->pic);
			int ph = cairo_image_surface_get_height(job->pic);
			double ks = (double)pw / w;
			cam.f *= ks;
			cam.cx *= ks;
			cam.cy *= ks;
			memset(f, 0, sizeof *f);
			f->cam = cam;
			lkf_grid(f, sc, &cam, pw, ph);
			free(sc);
		} else {
			/* where the picture goes by fast, a smaller frame: what goes by
			 * at more than LKF_SLOW pixels a second is a smear anyway */
			double t = lkf_time_at(film, p);
			double v = speed[(int)lround(clampf(t / film->len, 0.0, 1.0) * LKF_STEPS)];
			struct desk_lock_look look;
			memset(&look, 0, sizeof look);
			lk_move_pose(move, t, inside, &look);
			if (!lkf_trace(job, &look, res * clampf(LKF_SLOW / fmax(v, 1.0), 0.6, 1.0), true, f)) {
				film->n = i;
				lkf_film_free(film);
				return NULL;
			}
		}
		f->p = p;
	}
	return film;
}

/* the next thing wanted and not there, stills first and then the moves
 * in the order they come once the door is down (under the lock) */
static int lkf_next(void)
{
	static const int ORDER[LKF_JOBS] = { LKF_REST, LKF_HOLD,
		LKF_JOB_OF(DESK_LOCK_MOVE_LEAN), LKF_JOB_OF(DESK_LOCK_MOVE_OPEN),
		LKF_JOB_OF(DESK_LOCK_MOVE_FAIL), LKF_JOB_OF(DESK_LOCK_MOVE_OUT),
		LKF_JOB_OF(DESK_LOCK_MOVE_WALK), LKF_JOB_OF(DESK_LOCK_MOVE_NONE) };
	for (int i = 0; i < LKF_JOBS; i++) {
		int j = ORDER[i];
		const struct lkf_job *job = &g_lkf.job[j];
		if (!job->want || !job->sign) {
			continue;
		}
		if (j < LKF_STILLS) {
			if (!g_lkf.still[j] || !lkf_key_eq(&g_lkf.still_key[j], &job->key)) {
				return j;
			}
			continue;
		}
		int move = j - LKF_STILLS;
		if ((move == DESK_LOCK_MOVE_OPEN || move == DESK_LOCK_MOVE_OUT) && job->key.view >= 0
			&& !job->pic) {
			continue;   /* waiting for the room's picture */
		}
		const struct lkf_film *film = g_lkf.film[move];
		if (film && lkf_key_eq(&film->key, &job->key)) {
			continue;
		}
		/* the stills it begins and ends on first */
		int s0 = lkf_first_still(move), s1 = lkf_last_still(move);
		bool ready = true;
		for (int e = 0; e < 2; e++) {
			int s = e ? s1 : s0;
			if (s < 0) {
				continue;
			}
			struct lkf_key sk = job->key;
			sk.view = -1;
			sk.room = 0;
			ready = ready && g_lkf.still[s] && lkf_key_eq(&g_lkf.still_key[s], &sk);
		}
		if (ready) {
			return j;
		}
	}
	return -1;
}

static const char *const LKF_NAME[LKF_JOBS] = { "standing at it", "leant in to the key",
	"", "walking up to it", "leaning in to the key", "told no", "let in", "out of the room" };

static void *lkf_worker(void *arg)
{
	(void)arg;
	thread_to_the_back();
	for (;;) {
		pthread_mutex_lock(&g_lkf.lock);
		int j = room_stopping() ? -1 : lkf_next();
		if (j < 0) {
			g_lkf.running = false;
			g_lkf.busy = -1;
			pthread_mutex_unlock(&g_lkf.lock);
			return NULL;
		}
		struct lkf_job job;
		lkf_job_copy(&job, &g_lkf.job[j]);
		struct lkf_frame *still[LKF_STILLS];
		for (int s = 0; s < LKF_STILLS; s++) {
			/* what a film begins and ends on, held for it */
			still[s] = g_lkf.still[s];
			if (still[s]) {
				cairo_surface_reference(still[s]->img);
			}
		}
		struct lkf_frame held[LKF_STILLS];
		for (int s = 0; s < LKF_STILLS; s++) {
			if (still[s]) {
				held[s] = *still[s];
				still[s] = &held[s];
			}
		}
		g_lkf.busy = j;
		atomic_store(&g_lkf.stop, false);
		pthread_mutex_unlock(&g_lkf.lock);

		double t0 = room_now();
		struct lkf_frame *made = NULL;
		struct lkf_film *film = NULL;
		if (j < LKF_STILLS) {
			made = lkf_bake_still(&job, j);
		} else {
			film = lkf_bake_film(&job, j - LKF_STILLS, still);
		}
		double dt = room_now() - t0;
		for (int s = 0; s < LKF_STILLS; s++) {
			if (still[s]) {
				cairo_surface_destroy(still[s]->img);
			}
		}

		pthread_mutex_lock(&g_lkf.lock);
		const struct lkf_job *now = &g_lkf.job[j];
		bool keep = !atomic_load(&g_lkf.stop) && now->want && lkf_key_eq(&now->key, &job.key);
		if (keep && made) {
			lkf_frame_free(g_lkf.still[j]);
			g_lkf.still[j] = made;
			g_lkf.still_key[j] = job.key;
			made = NULL;
			room_log("the door, %s, made ahead: %dx%d in %.2f s", LKF_NAME[j],
				cairo_image_surface_get_width(g_lkf.still[j]->img),
				cairo_image_surface_get_height(g_lkf.still[j]->img), dt);
		} else if (keep && film) {
			int move = j - LKF_STILLS;
			/* the room's picture and pictures were for making it */
			struct lkf_job *jj = &g_lkf.job[j];
			if (jj->pic) {
				cairo_surface_destroy(jj->pic);
				jj->pic = NULL;
			}
			for (int i = 0; i < 2; i++) {
				if (jj->room.keep[i]) {
					cairo_surface_destroy(jj->room.keep[i]);
				}
			}
			memset(&jj->room, 0, sizeof jj->room);
			jj->room_on = false;
			const char *dump = getenv("DESK_LOCK_FILM_DUMP");
			for (int i = 0; dump && i < film->n; i++) {
				/* desk-preview --lock-film, to look at: the frames as made */
				char fp[1024];
				snprintf(fp, sizeof fp, "%s/%d-%02d.png", dump, move, i);
				if (film->f[i].img) {
					cairo_surface_write_to_png(film->f[i].img, fp);
				}
			}
			lkf_film_free(g_lkf.film[move]);
			g_lkf.film[move] = film;
			film = NULL;
			const struct lkf_frame *mid = &g_lkf.film[move]->f[g_lkf.film[move]->n / 2];
			room_log("the door, %s, filmed ahead: %d frames (%dx%d in the middle) in %.2f s",
				LKF_NAME[j],
				g_lkf.film[move]->n, mid->img ? cairo_image_surface_get_width(mid->img) : 0,
				mid->img ? cairo_image_surface_get_height(mid->img) : 0, dt);
		}
		g_lkf.busy = -1;
		pthread_mutex_unlock(&g_lkf.lock);
		lkf_frame_free(made);
		lkf_film_free(film);
		lkf_job_drop(&job);
	}
}

/* under the lock: what is no longer wanted let go, and the thread started
 * if there is anything to make */
static void lkf_tidy(void)
{
	for (int s = 0; s < LKF_STILLS; s++) {
		const struct lkf_job *job = &g_lkf.job[s];
		if (g_lkf.still[s] && (!job->want || !lkf_key_eq(&g_lkf.still_key[s], &job->key))) {
			lkf_frame_free(g_lkf.still[s]);
			g_lkf.still[s] = NULL;
		}
	}
	for (int m = 1; m < DESK_LOCK_MOVES; m++) {
		const struct lkf_job *job = &g_lkf.job[LKF_JOB_OF(m)];
		if (g_lkf.film[m] && (!job->want || !lkf_key_eq(&g_lkf.film[m]->key, &job->key))) {
			lkf_film_free(g_lkf.film[m]);
			g_lkf.film[m] = NULL;
		}
	}
	if (g_lkf.busy >= 0 && !g_lkf.job[g_lkf.busy].want) {
		atomic_store(&g_lkf.stop, true);
	}
	if (!g_lkf.running && lkf_next() >= 0) {
		pthread_t th;
		if (pthread_create(&th, NULL, lkf_worker, NULL) == 0) {
			pthread_detach(th);
			g_lkf.running = true;
		}
	}
}

unsigned desk_lock_films(int w, int h, const struct desk_lock_look *look, unsigned moves)
{
	/* DESK_LOCK_FILMS=off: traced as they go, as they were */
	const char *off = getenv("DESK_LOCK_FILMS");
	if (!look || w < 8 || h < 8 || desk_gfx_level() < DESK_GFX_MEDIUM
		|| (off && !strcmp(off, "off"))) {
		moves = 0;
	}
	bool want[LKF_JOBS] = { false };
	for (int m = 1; m < DESK_LOCK_MOVES; m++) {
		if (moves & (1u << m)) {
			want[LKF_JOB_OF(m)] = true;
			int s0 = lkf_first_still(m), s1 = lkf_last_still(m);
			if (s0 >= 0) {
				want[s0] = true;
			}
			if (s1 >= 0) {
				want[s1] = true;
			}
		}
	}
	struct lkf_key base;
	memset(&base, 0, sizeof base);
	if (moves) {
		double k = lk_scale_for(w, h);
		/* the board and the plate, drawn as the door will draw them */
		lk_prepare(w, h, k, look->name ? look->name : "", look->picture,
			look->mood.set && (look->mood.decor & DESK_DECOR_BIRTHDAY));
		struct lk_rig rig;
		lk_rig_for(&look->mood, &rig);
		lkf_key_of(&base, w, h, k, look, rig.key);
	}
	unsigned missing = 0;
	pthread_mutex_lock(&g_lkf.lock);
	for (int j = 0; j < LKF_JOBS; j++) {
		struct lkf_job *job = &g_lkf.job[j];
		if (!want[j]) {
			/* the thread making it, if it is, has its own copy */
			if (g_lkf.busy == j) {
				atomic_store(&g_lkf.stop, true);
			}
			lkf_job_drop(job);
			continue;
		}
		int move = j >= LKF_STILLS ? j - LKF_STILLS : DESK_LOCK_MOVE_NONE;
		bool room = move == DESK_LOCK_MOVE_OUT
			|| (move == DESK_LOCK_MOVE_OPEN && look->inside_view >= 0);
		struct lkf_key key = base;
		if (room) {
			lkf_key_room(&key, look->inside_view, w, h);
		}
		bool same = job->want && lkf_key_eq(&job->key, &key);
		if (same && (!room || job->pic)) {
			continue;   /* asked for already, as it is */
		}
		bool made = j < LKF_STILLS ? g_lkf.still[j] && lkf_key_eq(&g_lkf.still_key[j], &key)
			: g_lkf.film[move] && lkf_key_eq(&g_lkf.film[move]->key, &key);
		if (!same) {
			/* another door now (the weather, the screen): what is being
			 * made for the last one is no use */
			if (g_lkf.busy == j) {
				atomic_store(&g_lkf.stop, true);
			}
			lkf_job_drop(job);
		}
		job->want = true;
		job->key = key;
		job->mood = look->mood;
		if (!job->sign) {
			job->sign = cairo_surface_reference(g_lock.sign);
			job->gold = cairo_surface_reference(g_lock.gold);
			job->plate = cairo_surface_reference(g_lock.plate);
		}
		if (room && !made && !job->pic) {
			if (!look->inside) {
				missing |= 1u << move;
				continue;
			}
			job->pic = cairo_surface_reference(look->inside);
			lk_edges(look->inside, job->edge);
			job->room_on = lk_room_build(&job->room, w, h, key.view, look->inside, job->edge,
				&job->end);
			for (int i = 0; i < 2; i++) {
				if (job->room.keep[i]) {
					cairo_surface_reference(job->room.keep[i]);
				}
			}
		}
	}
	lkf_tidy();
	pthread_mutex_unlock(&g_lkf.lock);
	return missing;
}

void desk_lock_films_settle(void)
{
	for (;;) {
		pthread_mutex_lock(&g_lkf.lock);
		bool running = g_lkf.running;
		pthread_mutex_unlock(&g_lkf.lock);
		if (!running) {
			return;
		}
		struct timespec ts = { 0, 20 * 1000 * 1000 };
		nanosleep(&ts, NULL);
	}
}

/* where frame f's grid lands for an eye at cam with the door and the key as
 * sc has them: x, y in the screen's pixels times k; ok where it lands at
 * all (not behind the eye, not so near it that it is thrown off anywhere) */
static void lkf_land(const struct lkf_frame *f, const struct lk_scene *sc, const struct lk_cam *cam,
	double k, int w, int h, double *x, double *y, bool *ok)
{
	for (int i = 0; i < LKF_GX * LKF_GY; i++) {
		vec3 p = lkf_point(f, i, sc);
		vec3 q = rot_x(rot_y(vsub(p, cam->eye), -cam->yaw), -cam->pitch);
		ok[i] = q.z < -0.02;
		if (!ok[i]) {
			continue;
		}
		x[i] = (cam->cx + cam->f * q.x / -q.z) * k;
		y[i] = (cam->cy - cam->f * q.y / -q.z) * k;
		ok[i] = fabs(x[i]) < 8.0 * w * k && fabs(y[i]) < 8.0 * h * k;
	}
}

/* As near as one scale and a shift put frame f's grid where it lands (in
 * the screen's pixels: its pixel x, y at s x + tx, s y + ty): what is laid
 * under the grid, for what of it lands nowhere. */
static void lkf_fit(const struct lkf_frame *f, const double *gx, const double *gy, const bool *ok,
	int fw, int fh, double *s, double *tx, double *ty)
{
	double mx = 0.0, my = 0.0, mu = 0.0, mv = 0.0;
	int n = 0;
	for (int i = 0; i < LKF_GX * LKF_GY; i++) {
		if (ok[i]) {
			mx += (double)(i % LKF_GX) * fw / (LKF_GX - 1);
			my += (double)(i / LKF_GX) * fh / (LKF_GY - 1);
			mu += gx[i];
			mv += gy[i];
			n++;
		}
	}
	if (n >= 3) {
		mx /= n;
		my /= n;
		mu /= n;
		mv /= n;
		double num = 0.0, den = 0.0;
		for (int i = 0; i < LKF_GX * LKF_GY; i++) {
			if (ok[i]) {
				double x = (double)(i % LKF_GX) * fw / (LKF_GX - 1) - mx;
				double y = (double)(i / LKF_GX) * fh / (LKF_GY - 1) - my;
				num += x * (gx[i] - mu) + y * (gy[i] - mv);
				den += x * x + y * y;
			}
		}
		if (den > 1e-9 && num > 0.0) {
			*s = num / den;
			*tx = mu - *s * mx;
			*ty = mv - *s * my;
			return;
		}
	}
	*s = 1.0;
	*tx = *ty = 0.0;
	(void)f;
}

/* ---- laying a frame by its grid ----
 *
 * Each cell of a frame's grid is two triangles, each laid by the one
 * affine map that puts its three corners where they land: what the frame
 * saw, near and far, the door swung and the key turned, put where the eye
 * now sees it. Done here, by the row, on every core: a cairo clip and
 * paint for each of hundreds of triangles was the slower, and its edges,
 * smoothed, showed as seams. */

struct lkf_warp {
	uint32_t *dst;
	int dw, dh, dstride;          /* in pixels */
	const uint32_t *src;
	int sw, sh, sstride;
	const double *x, *y;          /* where each point of the grid lands, in dst */
	const bool *ok;
	double cw, ch;                /* a cell of the grid, in src's pixels */
	int next;
	pthread_mutex_t lock;
};

static inline uint32_t lkf_mix(uint32_t a, uint32_t b, unsigned t)
{
	uint32_t rb = (((a & 0xff00ffu) * (256u - t) + (b & 0xff00ffu) * t) >> 8) & 0xff00ffu;
	uint32_t ag = (((a >> 8) & 0xff00ffu) * (256u - t) + ((b >> 8) & 0xff00ffu) * t) & 0xff00ff00u;
	return rb | ag;
}

static inline uint32_t lkf_fetch(const struct lkf_warp *wp, double x, double y)
{
	x = x < 0.0 ? 0.0 : x > wp->sw - 1 ? wp->sw - 1 : x;
	y = y < 0.0 ? 0.0 : y > wp->sh - 1 ? wp->sh - 1 : y;
	int x0 = (int)x, y0 = (int)y;
	int x1 = x0 + 1 < wp->sw ? x0 + 1 : x0, y1 = y0 + 1 < wp->sh ? y0 + 1 : y0;
	unsigned fx = (unsigned)((x - x0) * 256.0), fy = (unsigned)((y - y0) * 256.0);
	const uint32_t *r0 = wp->src + (size_t)y0 * wp->sstride, *r1 = wp->src + (size_t)y1 * wp->sstride;
	return lkf_mix(lkf_mix(r0[x0], r0[x1], fx), lkf_mix(r1[x0], r1[x1], fx), fy);
}

/* triangle a, b, c of the grid, the rows y0 to y1 of it */
static void lkf_warp_tri(const struct lkf_warp *wp, int a, int b, int c, int y0, int y1)
{
	double ua = wp->x[a], va = wp->y[a], ub = wp->x[b], vb = wp->y[b], uc = wp->x[c], vc = wp->y[c];
	double det = (ub - ua) * (vc - va) - (uc - ua) * (vb - va);
	if (fabs(det) < 1e-6) {
		return;
	}
	double vmin = fmin(va, fmin(vb, vc)), vmax = fmax(va, fmax(vb, vc));
	double umin = fmin(ua, fmin(ub, uc)), umax = fmax(ua, fmax(ub, uc));
	int iy0 = (int)floor(vmin), iy1 = (int)ceil(vmax);
	int ix0 = (int)floor(umin), ix1 = (int)ceil(umax);
	iy0 = iy0 < y0 ? y0 : iy0;
	iy1 = iy1 > y1 ? y1 : iy1;
	ix0 = ix0 < 0 ? 0 : ix0;
	ix1 = ix1 > wp->dw ? wp->dw : ix1;
	if (iy0 >= iy1 || ix0 >= ix1) {
		return;
	}
	/* where each corner was in the frame */
	double xa = (a % LKF_GX) * wp->cw, ya = (a / LKF_GX) * wp->ch;
	double xb = (b % LKF_GX) * wp->cw, yb = (b / LKF_GX) * wp->ch;
	double xc = (c % LKF_GX) * wp->cw, yc = (c / LKF_GX) * wp->ch;
	/* the corners' weights at u, v, and how they go along a row */
	double l1u = (vc - va) / det, l1v = -(uc - ua) / det;
	double l2u = -(vb - va) / det, l2v = (ub - ua) / det;
	const double eps = -1e-7;
	/* how far in from the frame's edge it is laid fully: toward its edge
	 * it gives way to the other frame, not to a step between the two */
	const double feather = 1.0 / fmax(0.04 * wp->sw, 1.0);
	for (int py = iy0; py < iy1; py++) {
		double V = py + 0.5 - va, U = ix0 + 0.5 - ua;
		double l1 = l1u * U + l1v * V, l2 = l2u * U + l2v * V;
		uint32_t *row = wp->dst + (size_t)py * wp->dstride;
		for (int px = ix0; px < ix1; px++, l1 += l1u, l2 += l2u) {
			if (l1 < eps || l2 < eps || 1.0 - l1 - l2 < eps) {
				continue;
			}
			double sx = xa + l1 * (xb - xa) + l2 * (xc - xa);
			double sy = ya + l1 * (yb - ya) + l2 * (yc - ya);
			double in = fmin(fmin(sx, sy), fmin(wp->sw - sx, wp->sh - sy)) * feather;
			if (in <= 0.0) {
				continue;
			}
			unsigned a = in >= 1.0 ? 255u : (unsigned)(in * in * (3.0 - 2.0 * in) * 255.0);
			row[px] = (lkf_fetch(wp, sx - 0.5, sy - 0.5) & 0xffffffu) | (a << 24);
		}
	}
}

static void *lkf_warp_worker(void *arg)
{
	struct lkf_warp *wp = arg;
	const int band = 16;
	for (;;) {
		pthread_mutex_lock(&wp->lock);
		int y0 = wp->next;
		wp->next += band;
		pthread_mutex_unlock(&wp->lock);
		if (y0 >= wp->dh) {
			return NULL;
		}
		int y1 = y0 + band < wp->dh ? y0 + band : wp->dh;
		for (int j = 0; j + 1 < LKF_GY; j++) {
			for (int i = 0; i + 1 < LKF_GX; i++) {
				int a = j * LKF_GX + i, b = a + 1, c = a + LKF_GX + 1, d = a + LKF_GX;
				if (!wp->ok[a] || !wp->ok[b] || !wp->ok[c] || !wp->ok[d]) {
					continue;
				}
				double v0 = fmin(fmin(wp->y[a], wp->y[b]), fmin(wp->y[c], wp->y[d]));
				double v1 = fmax(fmax(wp->y[a], wp->y[b]), fmax(wp->y[c], wp->y[d]));
				if (v1 < y0 || v0 > y1) {
					continue;
				}
				lkf_warp_tri(wp, a, b, c, y0, y1);
				lkf_warp_tri(wp, a, c, d, y0, y1);
			}
		}
	}
}

/* frame f's picture into dst, by its grid landed at x, y */
static void lkf_warp(cairo_surface_t *dst, cairo_surface_t *src, const double *x, const double *y,
	const bool *ok)
{
	cairo_surface_flush(dst);
	cairo_surface_flush(src);
	struct lkf_warp wp = {
		.dst = (uint32_t *)(void *)cairo_image_surface_get_data(dst),
		.dw = cairo_image_surface_get_width(dst), .dh = cairo_image_surface_get_height(dst),
		.dstride = cairo_image_surface_get_stride(dst) / 4,
		.src = (const uint32_t *)(const void *)cairo_image_surface_get_data(src),
		.sw = cairo_image_surface_get_width(src), .sh = cairo_image_surface_get_height(src),
		.sstride = cairo_image_surface_get_stride(src) / 4,
		.x = x, .y = y, .ok = ok,
	};
	wp.cw = (double)wp.sw / (LKF_GX - 1);
	wp.ch = (double)wp.sh / (LKF_GY - 1);
	pthread_mutex_init(&wp.lock, NULL);
	int n = bake_threads();
	pthread_t th[8];
	int started = 0;
	for (int i = 0; i + 1 < n; i++) {
		if (pthread_create(&th[started], NULL, lkf_warp_worker, &wp) == 0) {
			started++;
		}
	}
	lkf_warp_worker(&wp);
	for (int i = 0; i < started; i++) {
		pthread_join(th[i], NULL);
	}
	pthread_mutex_destroy(&wp.lock);
	cairo_surface_mark_dirty(dst);
}

/* The two frames laid, into one: each where it saw, as much as it is to
 * be shown (mix of b) and as fully as it is in from its edge; the colour
 * left unpremultiplied by the warp, made cairo's here. */
struct lkf_merge {
	uint32_t *a;
	const uint32_t *b;      /* NULL: a alone */
	int w, h, stride;
	unsigned t;             /* 0..256, of b */
	int next;
	pthread_mutex_t lock;
};

static void *lkf_merge_worker(void *arg)
{
	struct lkf_merge *m = arg;
	for (;;) {
		pthread_mutex_lock(&m->lock);
		int y0 = m->next;
		m->next += 32;
		pthread_mutex_unlock(&m->lock);
		if (y0 >= m->h) {
			return NULL;
		}
		int y1 = y0 + 32 < m->h ? y0 + 32 : m->h;
		for (int y = y0; y < y1; y++) {
			uint32_t *ra = m->a + (size_t)y * m->stride;
			const uint32_t *rb = m->b ? m->b + (size_t)y * m->stride : NULL;
			for (int x = 0; x < m->w; x++) {
				uint32_t pa = ra[x], pb = rb ? rb[x] : 0;
				unsigned aa = pa >> 24, ab = pb >> 24;
				unsigned wa = (256u - m->t) * aa, wb = rb ? m->t * ab : 0u;
				if (wa + wb == 0) {
					ra[x] = 0;
					continue;
				}
				unsigned al = aa > ab ? aa : ab;
				uint32_t out = 0;
				for (int c = 0; c < 24; c += 8) {
					unsigned v = (((pa >> c) & 0xffu) * wa + ((pb >> c) & 0xffu) * wb) / (wa + wb);
					out |= ((v * al + 127u) / 255u) << c;
				}
				ra[x] = out | (al << 24);
			}
		}
	}
}

static void lkf_merge(cairo_surface_t *a, cairo_surface_t *b, double mix)
{
	struct lkf_merge m = {
		.a = (uint32_t *)(void *)cairo_image_surface_get_data(a),
		.b = b ? (const uint32_t *)(const void *)cairo_image_surface_get_data(b) : NULL,
		.w = cairo_image_surface_get_width(a), .h = cairo_image_surface_get_height(a),
		.stride = cairo_image_surface_get_stride(a) / 4,
		.t = (unsigned)lround(clampf(mix, 0.0, 1.0) * 256.0),
	};
	pthread_mutex_init(&m.lock, NULL);
	int n = bake_threads();
	pthread_t th[8];
	int started = 0;
	for (int i = 0; i + 1 < n; i++) {
		if (pthread_create(&th[started], NULL, lkf_merge_worker, &m) == 0) {
			started++;
		}
	}
	lkf_merge_worker(&m);
	for (int i = 0; i < started; i++) {
		pthread_join(th[i], NULL);
	}
	pthread_mutex_destroy(&m.lock);
	cairo_surface_mark_dirty(a);
}

/* the pictures frames are laid into, kept from one frame to the next:
 * clear (nothing laid there yet) where a frame's grid does not reach */
static cairo_surface_t *g_lkf_canvas[2];

static cairo_surface_t *lkf_canvas(int i, int W, int H)
{
	cairo_surface_t **c = g_lkf_canvas;
	if (c[i] && (cairo_image_surface_get_width(c[i]) != W
		|| cairo_image_surface_get_height(c[i]) != H)) {
		cairo_surface_destroy(c[i]);
		c[i] = NULL;
	}
	if (!c[i]) {
		c[i] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
	}
	cairo_surface_flush(c[i]);
	memset(cairo_image_surface_get_data(c[i]), 0,
		(size_t)cairo_image_surface_get_stride(c[i]) * H);
	cairo_surface_mark_dirty(c[i]);
	return c[i];
}

/* and let go with the rest of the door's pictures (desk_lock_forget) */
static void lkf_canvas_free(void)
{
	for (int i = 0; i < 2; i++) {
		if (g_lkf_canvas[i]) {
			cairo_surface_destroy(g_lkf_canvas[i]);
			g_lkf_canvas[i] = NULL;
		}
	}
}

/* Frames a and b (b over a, mix of it; b NULL for a alone) laid for the
 * eye at cam, the door and key as sc has them, onto cr: each by its grid.
 * What one of them did not see (the wider view of an eye further back
 * than a frame) is the other's alone; what neither saw, the nearer of
 * them scaled and moved as near as one scale and a shift put it. */
static void lkf_show(cairo_t *cr, int w, int h, double k, const struct lk_scene *sc,
	const struct lk_cam *cam, const struct lkf_frame *a, const struct lkf_frame *b, double mix)
{
	int W = (int)lround(w * k), H = (int)lround(h * k);
	double kk = (double)W / w;
	const struct lkf_frame *two[2] = { a, b };
	static double gx[2][LKF_GX * LKF_GY], gy[2][LKF_GX * LKF_GY];
	static bool ok[2][LKF_GX * LKF_GY];
	cairo_surface_t *canvas[2] = { NULL, NULL };
	for (int e = 0; e < (b ? 2 : 1); e++) {
		const struct lkf_frame *f = two[e];
		lkf_land(f, sc, cam, kk, w, h, gx[e], gy[e], ok[e]);
		if (e == (b && mix > 0.5 ? 1 : 0)) {
			/* under it all */
			int fw = cairo_image_surface_get_width(f->img);
			int fh = cairo_image_surface_get_height(f->img);
			double s, tx, ty;
			lkf_fit(f, gx[e], gy[e], ok[e], fw, fh, &s, &tx, &ty);
			cairo_save(cr);
			cairo_scale(cr, 1.0 / kk, 1.0 / kk);
			cairo_translate(cr, tx, ty);
			cairo_scale(cr, s, s);
			cairo_set_source_surface(cr, f->img, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
			cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
			cairo_paint(cr);
			cairo_restore(cr);
		}
		canvas[e] = lkf_canvas(e, W, H);
		lkf_warp(canvas[e], f->img, gx[e], gy[e], ok[e]);
	}
	lkf_merge(canvas[0], canvas[1], b ? mix : 0.0);
	cairo_save(cr);
	cairo_scale(cr, 1.0 / kk, 1.0 / kk);
	cairo_set_source_surface(cr, canvas[0], 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* The door from what was made ahead, if it was: the film of the move
 * under way, or the still the eye has stopped at. coming says that the
 * still the eye is at is on its way (and the door is to be drawn again
 * until it is there). */
/* paint a still, as it is */
static void lkf_still(cairo_t *cr, int w, int h, cairo_surface_t *img)
{
	cairo_save(cr);
	cairo_scale(cr, (double)w / cairo_image_surface_get_width(img),
		(double)h / cairo_image_surface_get_height(img));
	cairo_set_source_surface(cr, img, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
}

/* the film of the move under way, if there is one and the eye is not yet
 * where it ends (under the lock) */
static const struct lkf_film *lkf_film_for(int w, int h, double kn, const struct desk_lock_look *lk,
	unsigned mood)
{
	int move = lk->move;
	if (move <= DESK_LOCK_MOVE_NONE || move >= DESK_LOCK_MOVES || !g_lkf.film[move]) {
		return NULL;
	}
	struct lkf_key key;
	lkf_key_of(&key, w, h, kn, lk, mood);
	if (move == DESK_LOCK_MOVE_OPEN || move == DESK_LOCK_MOVE_OUT) {
		lkf_key_room(&key, lk->inside ? lk->inside_view : -1, w, h);
	}
	const struct lkf_film *film = g_lkf.film[move];
	return lkf_key_eq(&film->key, &key) ? film : NULL;
}

bool desk_lock_filmed(int w, int h, const struct desk_lock_look *lk)
{
	if (desk_gfx_level() < DESK_GFX_MEDIUM || w < 8 || h < 8 || (lk->push >= 0.999 && lk->inside)) {
		return false;
	}
	struct lk_rig rig;
	lk_rig_for(&lk->mood, &rig);
	pthread_mutex_lock(&g_lkf.lock);
	const struct lkf_film *film = lkf_film_for(w, h, lk_scale_for(w, h), lk, rig.key);
	bool on = film && lk->move_t < film->len;
	pthread_mutex_unlock(&g_lkf.lock);
	return on;
}

static bool lkf_paint(cairo_t *cr, int w, int h, double kn, double kd,
	const struct desk_lock_look *lk, const struct lk_scene *sc, const struct lk_cam *cam,
	bool *coming)
{
	*coming = false;
	if (desk_gfx_level() < DESK_GFX_MEDIUM) {
		return false;
	}
	struct lkf_key key;
	lkf_key_of(&key, w, h, kn, lk, sc->rig.key);
	bool painted = false;
	pthread_mutex_lock(&g_lkf.lock);
	const struct lkf_film *film = lkf_film_for(w, h, kn, lk, sc->rig.key);
	if (film && lk->move_t >= film->len && film->f[film->n - 1].img
		&& lkf_last_still(lk->move) >= 0) {
		/* where the move left the eye: the still it ends on, as it is */
		lkf_still(cr, w, h, film->f[film->n - 1].img);
		painted = true;
	} else if (film) {
		double p = lkf_prog_at(film, lk->move_t);
		int i = 0;
		while (i < film->n - 2 && film->f[i + 1].p <= p) {
			i++;
		}
		double span = film->f[i + 1].p - film->f[i].p;
		double mix = span > 1e-12 ? clampf((p - film->f[i].p) / span, 0.0, 1.0) : 1.0;
		struct lkf_frame *two[2] = { NULL, NULL };
		bool ok = true;
		for (int e = 0; e < 2; e++) {
			const struct lkf_frame *f = &film->f[i + e];
			two[e] = malloc(sizeof *two[e]);
			if (!two[e]) {
				ok = false;
				continue;
			}
			*two[e] = *f;
			if (!f->img) {
				/* the room's picture as it is now, where the walk in ends */
				two[e]->img = lk->inside;
				ok = ok && lk->inside;
			}
		}
		if (ok) {
			if (mix < 0.002) {
				lkf_show(cr, w, h, kd, sc, cam, two[0], NULL, 0.0);
			} else if (mix > 0.998) {
				lkf_show(cr, w, h, kd, sc, cam, two[1], NULL, 0.0);
			} else {
				lkf_show(cr, w, h, kd, sc, cam, two[0], two[1], mix);
			}
			painted = true;
		}
		free(two[0]);
		free(two[1]);
	}
	if (!painted) {
		/* stopped at the door, or leant in to the key */
		bool rest = lk->back == 0.0 && lk->lean == 0.0 && lk->turn == 0.0 && lk->open == 0.0
			&& lk->push == 0.0;
		bool hold = lk->back == 0.0 && lk->lean == 1.0 && lk->turn == DESK_LOCK_TRY
			&& lk->open == 0.0 && lk->push == 0.0;
		int s = rest ? LKF_REST : hold ? LKF_HOLD : -1;
		if (s >= 0 && g_lkf.still[s] && lkf_key_eq(&g_lkf.still_key[s], &key)) {
			lkf_still(cr, w, h, g_lkf.still[s]->img);
			painted = true;
		} else if (s >= 0 && g_lkf.job[s].want && lkf_key_eq(&g_lkf.job[s].key, &key)
			&& g_lkf.running) {
			*coming = true;
		}
	}
	g_lkf.awaited = *coming;
	pthread_mutex_unlock(&g_lkf.lock);
	return painted;
}

/* a still the door is waiting on is being made */
static bool lkf_awaited(void)
{
	pthread_mutex_lock(&g_lkf.lock);
	bool w = g_lkf.awaited && g_lkf.running;
	pthread_mutex_unlock(&g_lkf.lock);
	return w;
}
