/* Part of draw.c: what is in the drawer, and the room it is in.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ================================================================
 * What is in the drawer, and the room it is in.
 * ================================================================ */

static void icon_bounds(int kind, vec3 *lo, vec3 *hi);
static hit3 scene_map(vec3 p, int kind);

/* The five objects, laid out in a row on the felt. yaw turns an object on
 * the felt; pitch leans it back so that a camera looking straight down sees
 * its face rather than its lid. */
struct drawer_item {
	int kind;
	double x, z;
	double yaw, pitch;
	double scale;
};

/* Where they lie in the drawer in the traced scene. The drawer sheet is
 * traced empty, so nothing on screen shows these: what lies in the drawer
 * is drawn (tool_*, desk_drawer_paint_items). */
static const struct drawer_item DRAWER_ITEMS[DESK_DOCK_COUNT] = {
	{ DESK_ICON_TERM,   -8.90, -0.10,  0.30, 0.88, 1.66 },
	{ DESK_ICON_GLOBE,  -4.45,  0.10, -0.26, 0.52, 1.53 },
	{ DESK_ICON_FOLDER,  0.00, -0.12,  0.18, 1.08, 1.61 },
	{ DESK_ICON_NOTES,   4.45,  0.08, -0.32, 1.16, 1.56 },
	{ DESK_ICON_GEAR,    8.90, -0.08,  0.26, 1.46, 1.46 },
};

/* Measured bounds of each object and the lift that puts its feet on the
 * felt, plus a bounding sphere in drawer space. Deriving them from the
 * geometry means moving an object cannot leave it hovering or buried, and
 * cannot leave a stale sphere punching a hole in the felt beside it. */
static vec3 g_item_lo[DESK_DOCK_COUNT], g_item_hi[DESK_DOCK_COUNT];
static double g_item_y[DESK_DOCK_COUNT];
static vec3 g_item_c[DESK_DOCK_COUNT];   /* centre of the rotated bound */
static vec3 g_item_e[DESK_DOCK_COUNT];   /* its half extents */
static int g_items_built;

/* The bound is a box, not a sphere, and it is only ever reported as a
 * distance when it is further away than anything that samples around a
 * surface can reach. Report it closer than that and ambient occlusion reads
 * the empty box as solid, and every object sits in a neat rectangular pit. */
#define DRAWER_CULL 0.08
#define DRAWER_NEAR 1.60

static vec3 item_to_local(vec3 p, int i)
{
	const struct drawer_item *it = &DRAWER_ITEMS[i];
	vec3 q = vsub(p, v3(it->x, g_item_y[i], it->z));
	q = rot_y(q, it->yaw);
	q = rot_x(q, it->pitch);
	return vmul(q, 1.0 / it->scale);
}

static int icon_cache_dir(char *buf, size_t n);
static unsigned icon_build_tag(void);

/* What the five things measure, kept on the disk with the room they are
 * in (room-items-<tag>.bin): measuring them is a million points of the
 * scene, a third of a second here and more than a second on an old
 * machine, at every login, for the same ten numbers each time. */
#define ITEMS_MAGIC 0x42494b44u   /* "DKIB" */

static bool items_cache_path(char *out, size_t n)
{
	char dir[512];
	return icon_cache_dir(dir, sizeof dir)
		&& (size_t)snprintf(out, n, "%s/room-items-%u.bin", dir, icon_build_tag()) < n;
}

static bool items_cache_read(void)
{
	char path[768];
	if (!items_cache_path(path, sizeof path)) {
		return false;
	}
	FILE *f = fopen(path, "rbe");
	if (!f) {
		return false;
	}
	unsigned head[2];
	double v[DESK_DOCK_COUNT][6];
	bool ok = fread(head, sizeof head, 1, f) == 1 && head[0] == ITEMS_MAGIC
		&& head[1] == DESK_DOCK_COUNT && fread(v, sizeof v, 1, f) == 1;
	fclose(f);
	for (int i = 0; ok && i < DESK_DOCK_COUNT; i++) {
		g_item_lo[i] = v3(v[i][0], v[i][1], v[i][2]);
		g_item_hi[i] = v3(v[i][3], v[i][4], v[i][5]);
	}
	return ok;
}

static void items_cache_write(void)
{
	char path[768], tmp[800];
	if (!items_cache_path(path, sizeof path)
		|| (size_t)snprintf(tmp, sizeof tmp, "%s.%d", path, (int)getpid()) >= sizeof tmp) {
		return;
	}
	unsigned head[2] = { ITEMS_MAGIC, DESK_DOCK_COUNT };
	double v[DESK_DOCK_COUNT][6];
	for (int i = 0; i < DESK_DOCK_COUNT; i++) {
		v[i][0] = g_item_lo[i].x;
		v[i][1] = g_item_lo[i].y;
		v[i][2] = g_item_lo[i].z;
		v[i][3] = g_item_hi[i].x;
		v[i][4] = g_item_hi[i].y;
		v[i][5] = g_item_hi[i].z;
	}
	FILE *f = fopen(tmp, "wbe");
	if (!f) {
		return;
	}
	bool ok = fwrite(head, sizeof head, 1, f) == 1 && fwrite(v, sizeof v, 1, f) == 1;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
	}
}

static void drawer_items_build(void)
{
	if (g_items_built) {
		return;
	}
	bool kept = items_cache_read();
	if (!kept) {
		for (int i = 0; i < DESK_DOCK_COUNT; i++) {
			icon_bounds(DRAWER_ITEMS[i].kind, &g_item_lo[i], &g_item_hi[i]);
		}
		items_cache_write();
	}
	for (int i = 0; i < DESK_DOCK_COUNT; i++) {
		const struct drawer_item *it = &DRAWER_ITEMS[i];
		/* the rotated, scaled extent, so the lift and the sphere are both
		 * measured on the object as it actually stands */
		vec3 lo = v3(1e9, 1e9, 1e9), hi = v3(-1e9, -1e9, -1e9);
		for (int k = 0; k < 8; k++) {
			vec3 l = v3((k & 1) ? g_item_hi[i].x : g_item_lo[i].x,
				(k & 2) ? g_item_hi[i].y : g_item_lo[i].y,
				(k & 4) ? g_item_hi[i].z : g_item_lo[i].z);
			vec3 q = rot_y(rot_x(vmul(l, it->scale), -it->pitch), -it->yaw);
			lo = v3(fmin(lo.x, q.x), fmin(lo.y, q.y), fmin(lo.z, q.z));
			hi = v3(fmax(hi.x, q.x), fmax(hi.y, q.y), fmax(hi.z, q.z));
		}
		g_item_y[i] = -lo.y;
		vec3 c = vmul(vadd(lo, hi), 0.5);
		g_item_c[i] = vadd(c, v3(it->x, g_item_y[i], it->z));
		g_item_e[i] = vadd(vmul(vsub(hi, lo), 0.5),
			v3(DRAWER_CULL, DRAWER_CULL, DRAWER_CULL));
	}
	g_items_built = 1;
}

/* Which object owns a point, if any. One shared render is picked apart with
 * this, both for hit testing and for the hover treatment. */
static int drawer_item_at(vec3 p, double *out_d)
{
	int best = -1;
	double bd = 1e9;
	for (int i = 0; i < DESK_DOCK_COUNT; i++) {
		double d = sd_box(vsub(p, g_item_c[i]), g_item_e[i]);
		if (d < 0.0 && d < bd) {
			bd = d;
			best = i;
		}
	}
	if (out_d) {
		*out_d = bd;
	}
	return best;
}

/* Materials are authored in each object's own space. Evaluating wood grain
 * or a printed globe gore at a world coordinate smears it across the scene,
 * so a point on an object is carried back into the space it was modelled
 * in. Only object materials are remapped: the desk and the felt are authored
 * in world space, and remapping those puts ghost grain on the desk top. */
static vec3 scene_local(vec3 p, int kind, int id)
{
	if (kind != DRAWER_KIND && kind != ROOM_KIND) {
		return p;
	}
	if (id <= 0 || id > MID_GEAR) {
		return p;
	}
	vec3 q = p;
	if (kind == ROOM_KIND) {
		q = vsub(p, v3(0, RM_ITEM_Y, RM_ITEM_Z + room_pull()));
	}
	int i = drawer_item_at(q, NULL);
	return i >= 0 ? item_to_local(q, i) : p;
}

static hit3 drawer_items_map(vec3 q)
{
	hit3 h = { 1e9, 0 };
	for (int i = 0; i < DESK_DOCK_COUNT; i++) {
		double db = sd_box(vsub(q, g_item_c[i]), g_item_e[i]);
		if (db > DRAWER_NEAR) {
			/* a conservative bound: safe to march on, and no material */
			h = hmin3(h, (hit3){ db, 0 });
			continue;
		}
		hit3 o = scene_map(item_to_local(q, i), DRAWER_ITEMS[i].kind);
		o.d *= DRAWER_ITEMS[i].scale;
		h = hmin3(h, o);
	}
	return h;
}

/* Felt gives under anything standing on it. A shallow dip under each object
 * is most of what stops the row reading as decals on a green mat. */
static double felt_dip(vec3 q)
{
	double dip = 0.0;
	for (int i = 0; i < DESK_DOCK_COUNT; i++) {
		double dx = fabs(q.x - g_item_c[i].x) - g_item_e[i].x * 0.42;
		double dz = fabs(q.z - g_item_c[i].z) - g_item_e[i].z * 0.42;
		double d = fmax(fmax(dx, 0.0), fmax(dz, 0.0));
		dip = fmax(dip, (1.0 - smoothstepf(0.0, 0.70, d)) * 0.030);
	}
	return dip;
}

/* The drawer on its own, in its own space: the box, the felt and what is
 * standing on it. The room renders the same objects in room coordinates. */
static hit3 map_drawer(vec3 p)
{
	hit3 h = { 1e9, 0 };
	const double hx = RM_DR_HX - 0.42;
	const double cy = (RM_DR_TOP + RM_DR_BOT) * 0.5;
	const double chy = (RM_DR_TOP - RM_DR_BOT) * 0.5;
	const double zc = (RM_DR_FACE - 0.52 + RM_DR_BACK) * 0.5;
	const double zh = (RM_DR_FACE - 0.52 - RM_DR_BACK) * 0.5;

	double felt_y = RM_DR_BOT + 0.22 - felt_dip(p);
	h = hmin3(h, (hit3){ sd_box(vsub(p, v3(0, felt_y, zc)), v3(hx, 0.22, zh)), MID_FELT });
	for (int s = 0; s < 2; s++) {
		double sx = s ? hx : -hx;
		h = hmin3(h, (hit3){ sd_box(vsub(p, v3(sx, cy + 0.16, zc)),
			v3(0.22, chy - 0.16, zh)), MID_DRAWER_WOOD });
	}
	h = hmin3(h, (hit3){ sd_box(vsub(p, v3(0, cy + 0.16, RM_DR_BACK - 0.22)),
		v3(hx, chy - 0.16, 0.22)), MID_DRAWER_WOOD });
	h = hmin3(h, (hit3){ sd_round_box(vsub(p, v3(0, cy, RM_DR_FACE - 0.26)),
		v3(RM_DR_HX, chy, 0.26), 0.10), MID_DRAWER_FACE });

	h = hmin3(h, drawer_items_map(vsub(p, v3(0, RM_ITEM_Y, RM_ITEM_Z))));
	return h;
}

/* The things standing on the desk top. The status bar used to float over
 * the desktop; here it is three small instruments sitting on the wood at
 * the back, with the diary, the calendar block and the pad beside them. */
/* Which bake is running: 0 the whole room, 1 the room with the drawer taken
 * out, 2 the drawer by itself, 3 the bookcase, 4 the control panel, 5 the
 * post box. The desk is still traced in mode 2 as an occluder, but marked
 * so the resolve drops it. */
static int g_room_layer;

/* Two kinds of thing. The instruments along the back of the desk lie flat
 * and are read from straight above: the signal, the clock and the cell,
 * which is the row you want in the corner of your eye while you work.
 *
 * The rest -- the memo block, the clock you actually look at, the calendar
 * -- are stood up on the bookcase shelf on little plinths, leaning back
 * the way anything stood up on a shelf leans. They are not things you need
 * in the corner of your eye; they are things you look up at. */
struct room_gadget {
	double x, z;       /* centre on whatever it stands on */
	double hx, hz;     /* half across, and half deep (flat) or half deep (upright) */
	double h;          /* how far it stands off that surface */
	double inset;      /* how far the face is recessed */
	int round;
	int body, face;    /* materials */
	double face_w, face_h;   /* pixel size of the face cairo draws */
	double foot;       /* the surface it stands on; 0 is the desk top */
	double tilt;       /* leaning back, radians; 0 for the flat ones */
	double yaw;        /* and not quite square on, because a hand put it there */
	double plinth;     /* the stand under it, 0 for none */
	int kind;          /* GADGET_BLOCK, or GADGET_TENT: the calendar, the memo pad */
};

enum { GADGET_BLOCK = 0, GADGET_TENT = 1 };

/* The desk calendar is a tent: two leather-covered boards leaning together,
 * the month's pages on the front one, hung from a twin-loop wire round the
 * ridge, and turned over it to the back. In the front board's frame: y up
 * its face from the shelf, z out of it; g->h is the board's length. */
#define CAL_T     0.06    /* half a board's thickness */
#define CAL_PZ    0.10    /* the pages, stacked on the front board */
#define CAL_PY0   0.14    /* the foot of the pages, up the board */
#define CAL_PTOP  0.06    /* how far below the ridge their top edge is */
#define CAL_PIN   0.12    /* and in from the board's sides */
#define CAL_TB    0.40    /* the back board's lean */
#define CAL_RY    0.20    /* the wire: how far below the ridge it goes round */
#define CAL_RR    0.26    /* the radius of its loops */
#define CAL_RW    0.030   /* the wire's own */
#define CAL_RP    0.56    /* a pair of loops every so far along */
#define CAL_RT    0.075   /* the two of a pair, this far either side */
#define CAL_RM    0.30    /* and as many pairs as leave this much clear at the ends */
#define CAL_TAB   0.42    /* how far the sections' tabs stand out past the pages */

/* How many pairs of loops a tent's wire has either side of the middle one:
 * as many as its pages are wide for (four on the calendar, three on the
 * narrower memo pad). room.glsl works it out the same way. */
static double tent_rn(double hx)
{
	return floor((hx - CAL_PIN - CAL_RT - CAL_RM) / CAL_RP);
}

static bool gadget_upright(const struct room_gadget *g)
{
	return g->tilt != 0.0 || g->foot != 0.0;
}

/* A flat gadget's z is how far in it sits from the line along which the
 * top of the screen meets its top face -- so that how much of it is in view
 * is the same on every shape of screen: the clock stands as far back as
 * leaves the top edge of its display just in view -- and an upright one's
 * how far in from the front edge of the shelf board. Both lines move with
 * the shape of the screen: on a wide one the desk is shallower, and a
 * clock put down at a fixed z ended up behind the wall. */
static double gadget_z(const struct room_gadget *g)
{
	return gadget_upright(g) ? RM_SH_ZF + g->z
		: RM_ZF + (RM_FOV_X_V * 0.5 / g_rm_aspect) * g->h + g->z;
}

/* The gadget's own frame: u across it, v up its face, n out of its face. */
static vec3 gadget_to_world(const struct room_gadget *g, double u, double v, double n)
{
	vec3 l = v3(u, v, n);
	if (g->tilt != 0.0) {
		l = rot_x(l, -g->tilt);
	}
	return vadd(l, v3(g->x, g->foot + g->plinth, gadget_z(g)));
}

static vec3 gadget_to_local(const struct room_gadget *g, vec3 p)
{
	vec3 q = vsub(p, v3(g->x, g->foot + g->plinth, gadget_z(g)));
	return g->tilt != 0.0 ? rot_x(q, g->tilt) : q;
}

static double tent_tab(const struct room_gadget *g);

/* The four corners of the face cairo prints into, in the order quad_matrix
 * wants them: top left, top right, bottom left, bottom right. */
static void gadget_face_quad(const struct room_gadget *g, vec3 out[4])
{
	if (g->kind == GADGET_TENT) {
		/* the top page, all of it, and the tabs standing out past its
		 * right edge (the calendar's): the punched strip along its top
		 * edge is printed too, wire and all, so that it is one sheet of
		 * paper and not a print with a strip of the room's paper above it */
		double px = g->hx - CAL_PIN;
		double y0 = CAL_PY0, y1 = g->h - CAL_PTOP;
		for (int k = 0; k < 4; k++) {
			out[k] = gadget_to_world(g, (k & 1) ? px + tent_tab(g) : -px,
				(k & 2) ? y0 : y1, CAL_PZ + 0.004);
		}
		return;
	}
	double fx = g->hx - g->inset;
	if (gadget_upright(g)) {
		double fy = g->h * 0.5 - g->inset;
		for (int k = 0; k < 4; k++) {
			out[k] = gadget_to_world(g, (k & 1) ? fx : -fx,
				g->h * 0.5 + ((k & 2) ? -fy : fy), g->hz - 0.10);
		}
		return;
	}
	double fz = g->hz - g->inset;
	for (int k = 0; k < 4; k++) {
		out[k] = v3(g->x + ((k & 1) ? fx : -fx), g->h - 0.10,
			gadget_z(g) + ((k & 2) ? fz : -fz));
	}
}

/* The back left corner belongs to the control panel now -- to the whole of
 * it, once it is out -- so the row of small instruments shifts right and
 * the leather clock crosses to the far side, into the bay the panel used
 * to sit in. Nothing here is in the machine's way at either end of its
 * travel. */
#define RM_GD_SHELF_Z (-2.15)   /* in from the front edge of the board */
#define RM_GD_TILT 0.155

static const struct room_gadget ROOM_GADGETS[DESK_GADGET_COUNT] = {
	/* the tray: flat along the back of the desk. Only the clock now --
	 * the signal box moved on to the console's nameplate, the battery came
	 * off altogether -- and it has grown sideways into the room they left,
	 * a block of four keys beside its display, with its front edge where
	 * it always was so the windows keep the room they had. */
	{   0.50, 0.68, 5.16, 0.93, 0.95, 0.20, 0, MID_GADGET_BODY, MID_GADGET_GLASS,
		492.0, 72.0, 0.0, 0.0, 0.0, 0.0, GADGET_BLOCK },
	/* and the three stood up on the shelf, on their own plinths */
	{   4.60, RM_GD_SHELF_Z, 2.35, 0.80, 4.70, 0.44, 1, MID_GADGET_LEATHER,
		MID_GADGET_DIAL, 220.0, 220.0, RM_SH_BAY_B, RM_GD_TILT, -0.030, 0.62,
		GADGET_BLOCK },
	/* the calendar: a tent on a plinth as deep as it is, the plinth's
	 * front edge level with the others'. The page is 5.66 by 4.50, and its
	 * tabs stand out 0.42 past it; its face is printed at that shape. */
	{  -4.40, -1.45, 2.95, 0.20, 4.70, 0.0, 0, MID_GADGET_LEATHER,
		MID_GADGET_PAPER, 354.5, 262.4, RM_SH_BAY_B, 0.27, 0.038, 0.45,
		GADGET_TENT },
	/* the memo pad: the calendar's make, narrower and taller, ruled
	 * sheets on its board in place of the months -- no tabs. The sheet is
	 * 4.36 by 4.90, its face printed at that shape. */
	{ -11.40, -1.45, 2.30, 0.20, 5.10, 0.0, 0, MID_GADGET_LEATHER,
		MID_GADGET_PAPER, 254.0, 285.4, RM_SH_BAY_B, 0.27, -0.045, 0.45,
		GADGET_TENT },
};

/* how far a tent's face goes on past its pages: the calendar's tabs */
static double tent_tab(const struct room_gadget *g)
{
	return g == &ROOM_GADGETS[DESK_GADGET_CALENDAR] ? CAL_TAB : 0.0;
}

/* The desk calendar (and the memo pad, made the same way): the front board
 * and the pages on it, the back board hung from the ridge and leaning the
 * other way down to the shelf, and the wire round the top of the front
 * board in pairs of loops. room.glsl's cal_tent is this, line for line. */
static hit3 cal_tent(const struct room_gadget *g, vec3 p, hit3 h)
{
	double H = g->h;
	vec3 l = gadget_to_local(g, p);
	h = hmin3(h, (hit3){ sd_round_box(vsub(l, v3(0.0, H * 0.5, -CAL_T)),
		v3(g->hx, H * 0.5, CAL_T), 0.03), g->body });
	double py1 = H - CAL_PTOP;
	h = hmin3(h, (hit3){ sd_round_box(vsub(l, v3(0.0, (CAL_PY0 + py1) * 0.5, CAL_PZ * 0.5)),
		v3(g->hx - CAL_PIN, (py1 - CAL_PY0) * 0.5, CAL_PZ * 0.5), 0.015), g->face });
	vec3 ridge = gadget_to_world(g, 0.0, H, -2.0 * CAL_T);
	double lb = (ridge.y - (g->foot + g->plinth)) / cos(CAL_TB);
	vec3 b = rot_x(vsub(p, ridge), -CAL_TB);
	h = hmin3(h, (hit3){ sd_round_box(vsub(b, v3(0.0, -lb * 0.5, -CAL_T)),
		v3(g->hx, lb * 0.5, CAL_T), 0.03), g->body });
	double rn = tent_rn(g->hx);
	double k = clampf(floor(l.x / CAL_RP + 0.5), -rn, rn);
	double xr = fabs(l.x - k * CAL_RP) - CAL_RT;
	double ry = l.y - (H - CAL_RY), rz = l.z - (CAL_PZ - 2.0 * CAL_T) * 0.5;
	double rr = sqrt(ry * ry + rz * rz) - CAL_RR;
	double ring = sqrt(rr * rr + xr * xr) - CAL_RW;
	return hmin3(h, (hit3){ ring, MID_CC_STEEL });
}

/* How deep the tent stands, front edge to the foot of the back board. */
static double cal_depth(const struct room_gadget *g)
{
	return g->h * (sin(g->tilt) + cos(g->tilt) * tan(CAL_TB));
}

static hit3 room_gadgets(vec3 p)
{
	hit3 h = { 1e9, 0 };
	for (int i = 0; i < DESK_GADGET_COUNT; i++) {
		const struct room_gadget *g = &ROOM_GADGETS[i];
		bool up = gadget_upright(g);
		/* The ones on the shelf exist only where the shelf does. In the
		 * views that do not trace the bookcase they would be three things
		 * hanging in the air behind the desk. */
		if (up && g_room_layer != 3) {
			continue;
		}
		/* The clock on the desk is a picture, rendered once and laid over
		 * the desk's sheet (room_cases_paint), shadow and all. It is traced
		 * only where the camera moves: in the moves, and the views they end
		 * in. */
		if (i == DESK_GADGET_CLOCK && g_room_layer == 1) {
			continue;
		}
		double base = g->foot + g->plinth;
		double gz = gadget_z(g);
		vec3 c = up ? v3(g->x, base + g->h * 0.5, gz) : v3(g->x, g->h * 0.5, gz);
		vec3 e = up ? v3(g->hx, g->h * 0.5 + g->plinth, g->hz + g->h * 0.5 * sin(g->tilt))
			: v3(g->hx, g->h * 0.5, g->hz);
		if (g->kind == GADGET_TENT) {
			double dep = cal_depth(g), ht = g->h * cos(g->tilt);
			c = v3(g->x, g->foot + (g->plinth + ht) * 0.5, gz - dep * 0.5);
			e = v3(g->hx + 0.34, (g->plinth + ht) * 0.5 + 0.30, dep * 0.5 + 0.50);
		}
		/* skip the whole thing when the ray is nowhere near it */
		double db = sd_box(vsub(p, c), vadd(e, v3(0.40, 0.40, 0.40)));
		if (db > DRAWER_NEAR) {
			h = hmin3(h, (hit3){ db, 0 });
			continue;
		}
		if (g->kind == GADGET_TENT) {
			/* its stand, as deep as the tent */
			double dep = cal_depth(g), pz = gz - dep * 0.5;
			h = hmin3(h, (hit3){ sd_round_box(vsub(p, v3(g->x, g->foot + g->plinth * 0.30, pz)),
				v3(g->hx + 0.34, g->plinth * 0.30, dep * 0.5 + 0.46), 0.06), MID_SHELF_CASE });
			h = hmin3(h, (hit3){ sd_round_box(vsub(p, v3(g->x, g->foot + g->plinth * 0.72, pz)),
				v3(g->hx + 0.14, g->plinth * 0.30, dep * 0.5 + 0.26), 0.05), MID_SHELF_CASE });
			h = cal_tent(g, p, h);
			continue;
		}

		if (up) {
			/* the stand: a stepped plinth, the same timber as the case */
			h = hmin3(h, (hit3){ sd_round_box(vsub(p,
				v3(g->x, g->foot + g->plinth * 0.30, gz)),
				v3(g->hx + 0.34, g->plinth * 0.30, g->hz + 0.46), 0.06),
				MID_SHELF_CASE });
			h = hmin3(h, (hit3){ sd_round_box(vsub(p,
				v3(g->x, g->foot + g->plinth * 0.72, gz)),
				v3(g->hx + 0.14, g->plinth * 0.30, g->hz + 0.26), 0.05),
				MID_SHELF_CASE });

			vec3 l = gadget_to_local(g, p);
			vec3 lc = vsub(l, v3(0.0, g->h * 0.5, 0.0));
			double body = sd_round_box(lc, v3(g->hx, g->h * 0.5, g->hz),
				g->round ? fmin(g->hx, g->h * 0.5) * 0.30 : 0.14);
			vec3 fe = v3(g->hx - g->inset, g->h * 0.5 - g->inset, 0.10);
			double well = sd_round_box(vsub(lc, v3(0, 0, g->hz)), fe, 0.05);
			body = fmax(body, -well);
			h = hmin3(h, (hit3){ body, g->body });
			h = hmin3(h, (hit3){ sd_round_box(vsub(lc, v3(0, 0, g->hz - 0.12)),
				v3(fe.x, fe.y, 0.05), 0.04), g->face });
			continue;
		}

		double body = sd_round_box(vsub(p, c), e, g->round ? fmin(g->hx, g->hz) * 0.45 : 0.14);
		/* the recessed face, cut into the top */
		vec3 fc = v3(g->x, g->h, gz);
		vec3 fe = v3(g->hx - g->inset, 0.10, g->hz - g->inset);
		double well = sd_round_box(vsub(p, fc), fe, 0.05);
		body = fmax(body, -well);
		h = hmin3(h, (hit3){ body, g->body });
		h = hmin3(h, (hit3){ sd_round_box(vsub(p, v3(g->x, g->h - 0.12, gz)),
			v3(fe.x, 0.05, fe.z), 0.04), g->face });
	}
	return h;
}

static hit3 room_items(vec3 p)
{
	vec3 q = vsub(p, v3(0, RM_ITEM_Y, RM_ITEM_Z));
	return drawer_items_map(q);
}
