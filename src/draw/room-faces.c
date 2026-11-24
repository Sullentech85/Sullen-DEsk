/* Part of draw.c: the room laid from pictures of its faces.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

static void rf_add(struct rface_set *s, const char *what, vec3 o, vec3 u, vec3 v,
	double out, double in)
{
	if (s->n >= RF_MAX) {
		return;
	}
	struct rface *f = &s->f[s->n++];
	memset(f, 0, sizeof *f);
	f->o = o;
	f->u = u;
	f->v = v;
	f->n = vnorm(vcross(u, v));
	f->out = out;
	f->in = in;
	f->what = what;
	f->at = g_rf_at;
	f->tpu_max = g_rf_tpu;
	f->fine = g_rf_fine;
	f->lfine = g_rf_lfine;
	f->two = g_rf_two;
	f->live = g_rf_live;
	f->relief = g_rf_relief;
	f->tall = g_rf_tall;
	f->group = g_rf_group;
	f->cut = g_rf_cut;
	f->clear = g_rf_clear;
	f->pose = g_rf_group == RF_G_CONSOLE ? cc_dz(g_cc_out, g_rm_aspect) : 0.0;
	f->turn = g_rf_group == RF_G_DOOR ? clampf(g_pb_open, 0.0, 1.0) * RF_DOOR_SWING : 0.0;
	f->lamp = ROOM_LAMP_POS;
	f->base[RF_LAMP] = g_rf_lamp_base;
	f->base[RF_REST0] = f->base[RF_LAMP0] = 2;
}

/* The faces of a box that are in `sides`, turned by `turn` about the z
 * axis through `pivot` -- the way the binders lean (rot_z, as map_shelf
 * turns them the other way to stand them up). */
static void rf_box(struct rface_set *s, const char *what, vec3 c, vec3 e, unsigned sides,
	vec3 pivot, double turn, double out, double in)
{
	const struct {
		unsigned bit;
		vec3 o, u, v;
	} F[6] = {
		{ RF_PZ, { -e.x, -e.y, e.z }, { 2 * e.x, 0, 0 }, { 0, 2 * e.y, 0 } },
		{ RF_NZ, { e.x, -e.y, -e.z }, { -2 * e.x, 0, 0 }, { 0, 2 * e.y, 0 } },
		{ RF_PY, { -e.x, e.y, e.z }, { 2 * e.x, 0, 0 }, { 0, 0, -2 * e.z } },
		{ RF_NY, { -e.x, -e.y, -e.z }, { 2 * e.x, 0, 0 }, { 0, 0, 2 * e.z } },
		{ RF_PX, { e.x, -e.y, e.z }, { 0, 0, -2 * e.z }, { 0, 2 * e.y, 0 } },
		{ RF_NX, { -e.x, -e.y, -e.z }, { 0, 0, 2 * e.z }, { 0, 2 * e.y, 0 } },
	};
	for (int i = 0; i < 6; i++) {
		if (!(sides & F[i].bit)) {
			continue;
		}
		vec3 o = vadd(c, F[i].o), u = F[i].u, v = F[i].v;
		if (turn != 0.0) {
			o = vadd(pivot, rot_z(vsub(o, pivot), turn));
			u = rot_z(u, turn);
			v = rot_z(v, turn);
		}
		rf_add(s, what, o, u, v, out, in);
	}
}

/* The bookcase's view, face by face: what map_shelf and map_room put in
 * layer 3, as far as it is flat. The things on the desk and the board, the
 * console and the post box are faces of their own (rf_gadgets, rf_console,
 * rf_post). */
static void rf_room(struct rface_set *s, int layer)
{
	const double hx = g_rm_sh_hx;
	const double wall_z = RM_SH_ZB - 0.50;
	const vec3 o0 = v3(0, 0, 0);

	/* the wall and the floor the case stands against, which move back and
	 * forth with it with the shape of the screen -- the wall stands there
	 * in every view but the desk's own, case or no case */
	const double wh = RM_SH_CORN_Y + 12.0 - RM_FLOOR;
	g_rf_at = v3(0, 0, RM_SH_ZF);
	g_rf_tpu = 8.0;   /* paint and boards a long way off: smooth light */
	g_rf_two = false;
	g_rf_live = true;
	/* behind the case and over it, and the floor in front of it -- and
	 * in the views with no case, where it would be: bare, and pictured in
	 * those views' own packs */
	bool bare = layer != 3;
	rf_add(s, bare ? "wall-bare" : "wall", v3(-hx, RM_FLOOR, wall_z), v3(2.0 * hx, 0, 0),
		v3(0, wh, 0), 0.05, 0.05);
	rf_add(s, "floor", v3(-hx - 16.0, RM_FLOOR, RM_ZN + 24.0), v3(2.0 * (hx + 16.0), 0, 0),
		v3(0, 0, -(RM_ZN + 24.0 - RM_SH_ZF)), 0.05, 0.05);
	if (bare) {
		rf_add(s, "floor-bare", v3(-hx, RM_FLOOR, RM_SH_ZF), v3(2.0 * hx, 0, 0),
			v3(0, 0, -(RM_SH_ZF - wall_z)), 0.05, 0.05);
	}
	rf_box(s, bare ? "skirting-bare" : "skirting", v3(0, RM_FLOOR + 1.10, wall_z + 0.32),
		v3(hx, 1.10, 0.32), RF_PZ | RF_PY, o0, 0.0, 0.05, 0.05);
	/* and either side of it, each with its side: pictured beside the
	 * widest case, what a narrower one leaves bare was under it */
	for (int sgn = -1; sgn <= 1; sgn += 2) {
		double x0 = sgn < 0 ? -hx - 16.0 : hx;
		g_rf_at = v3(sgn * hx, 0, RM_SH_ZF);
		rf_add(s, "wall-side", v3(x0, RM_FLOOR, wall_z), v3(16.0, 0, 0), v3(0, wh, 0),
			0.05, 0.05);
		rf_add(s, "floor-side", v3(x0, RM_FLOOR, RM_SH_ZF), v3(16.0, 0, 0),
			v3(0, 0, -(RM_SH_ZF - wall_z)), 0.05, 0.05);
		rf_box(s, "skirting-side", v3(x0 + 8.0, RM_FLOOR + 1.10, wall_z + 0.32),
			v3(8.0, 1.10, 0.32), RF_PZ | RF_PY, o0, 0.0, 0.05, 0.05);
	}
	g_rf_tpu = RF_TPU_MAX;
	g_rf_live = false;
}

/* the bookcase itself */
static void rf_case(struct rface_set *s)
{
	const double zc = (RM_SH_ZF + RM_SH_ZB) * 0.5, zh = (RM_SH_ZF - RM_SH_ZB) * 0.5;
	const double hx = g_rm_sh_hx;
	const vec3 o0 = v3(0, 0, 0);

	g_rf_at = v3(0, 0, RM_SH_ZF);
	rf_box(s, "plinth", v3(0.0, (RM_SH_PLINTH + RM_FLOOR) * 0.5, zc - RM_SH_PLINTH_Z),
		v3(hx - 0.30, (RM_SH_PLINTH - RM_FLOOR) * 0.5, zh - RM_SH_PLINTH_Z),
		RF_PZ, o0, 0.0, 0.05, 0.05);
	for (int sgn = 0; sgn < 2; sgn++) {
		double sx = (sgn ? 1.0 : -1.0) * (hx - RM_SH_SIDE * 0.5);
		g_rf_at = v3(sx, 0, RM_SH_ZF);
		rf_box(s, "side", v3(sx, (RM_SH_TOP + RM_SH_PLINTH) * 0.5, zc),
			v3(RM_SH_SIDE * 0.5, (RM_SH_TOP - RM_SH_PLINTH) * 0.5, zh),
			RF_PZ | (sgn ? RF_NX : RF_PX), o0, 0.0, 0.05, 0.05);
	}
	g_rf_at = v3(0, 0, RM_SH_ZF);
	rf_box(s, "stile", v3(0.0, (RM_SH_TOP + RM_SH_PLINTH) * 0.5, zc),
		v3(RM_SH_SIDE * 0.5, (RM_SH_TOP - RM_SH_PLINTH) * 0.5, zh - 0.30),
		RF_PZ | RF_PX | RF_NX, o0, 0.0, 0.05, 0.05);
	rf_box(s, "back", v3(0.0, (RM_SH_TOP + RM_SH_PLINTH) * 0.5, RM_SH_ZB + RM_SH_BACKT * 0.5),
		v3(hx - RM_SH_SIDE, (RM_SH_TOP - RM_SH_PLINTH) * 0.5, RM_SH_BACKT * 0.5),
		RF_PZ, o0, 0.0, 0.05, 0.05);
	const double board_top[4] = {
		RM_SH_PLINTH, RM_SH_BAY_B, RM_SH_BAY_T + RM_SH_BOARD, RM_SH_TOP,
	};
	for (int i = 0; i < 4; i++) {
		rf_box(s, "board", v3(0.0, board_top[i] - RM_SH_BOARD * 0.5, zc + 0.12),
			v3(hx - RM_SH_SIDE, RM_SH_BOARD * 0.5, zh - 0.12),
			RF_PZ | RF_PY | RF_NY, o0, 0.0, 0.05, 0.05);
	}
	rf_box(s, "cornice", v3(0.0, (RM_SH_CORN_Y + RM_SH_TOP) * 0.5, zc + RM_SH_CORN_P * 0.5),
		v3(hx + RM_SH_CORN_P, (RM_SH_CORN_Y - RM_SH_TOP) * 0.5, zh + RM_SH_CORN_P * 0.5),
		RF_PZ | RF_PY | RF_NY, o0, 0.0, 0.10, 0.25);
	for (int i = 0; i < 2; i++) {
		rf_box(s, "lamp", v3(i ? 8.2 : -8.2, SHELF_LAMP_Y + 0.30, RM_SH_ZF - 0.15),
			v3(2.30, 0.26, 0.40), RF_PZ | RF_PX | RF_NX, o0, 0.0, 0.10, 0.30);
	}

	for (int i = 0; i < SHELF_ROW; i++) {
		const struct shelf_binder *b = &SHELF_BINDERS[i];
		g_rf_at = v3(b->x, RM_BN_FOOT, RM_BN_ZC);
		rf_box(s, "binder", v3(b->x, RM_BN_FOOT + b->h * 0.5, RM_BN_ZC),
			v3(b->hw + 0.055, b->h * 0.5, RM_BN_DEPTH * 0.5),
			RF_PZ | RF_PY | RF_PX | RF_NX, v3(b->x, RM_BN_FOOT, RM_BN_ZC), b->lean,
			0.10, 0.40);
	}
	g_rf_at = v3(RM_BK_X, RM_BN_FOOT, RM_BK_ZC);
	rf_box(s, "book", v3(RM_BK_X, RM_BN_FOOT + RM_BK_H * 0.5, RM_BK_ZC + 0.06),
		v3(RM_BK_HW, RM_BK_H * 0.5, RM_BK_D * 0.5 + 0.06),
		RF_PZ | RF_PY | RF_PX | RF_NX, o0, 0.0, 0.15, 0.40);
	double x0 = RM_BK_X0 + 2.0 * RM_BK_HW + 0.15;
	double run = hx - RM_SH_SIDE - x0;
	if (run > 2.0) {
		for (int i = 0; i < 3; i++) {
			double t = (i + 0.7) / 3.4;
			double bx = x0 + run * t, ht = 6.6 + room_seed(i, 1) * 2.4;
			double lean = 0.16 + 0.10 * room_seed(i, 2);
			vec3 foot = v3(bx, RM_BN_FOOT, RM_BN_ZC + 0.40);
			g_rf_at = foot;
			rf_box(s, "fallen", vadd(foot, v3(0, ht * 0.5, 0)),
				v3(0.62, ht * 0.5, RM_BN_DEPTH * 0.44),
				RF_PZ | RF_PY | RF_PX | RF_NX, foot, lean, 0.10, 0.30);
		}
	}

}

/* The desk's top, from the start of the move as well as from the view: in
 * two, at a line across it that stays put whatever the shape of the room,
 * so that the half a view looks down on can be pictured finely and the
 * near half, seen only in the move, coarsely. */
static void rf_desk(struct rface_set *s)
{
	g_rf_at = v3(0, 0, 0);
	g_rf_two = true;
	g_rf_live = true;
	g_rf_lamp_base = 0;
	rf_add(s, "desk", v3(-RM_HX, 0.0, RF_DESK_SPLIT), v3(2.0 * RM_HX, 0, 0),
		v3(0, 0, RM_ZF - RF_DESK_SPLIT), 0.05, 0.10);
	rf_add(s, "desk-near", v3(-RM_HX, 0.0, RM_ZN), v3(2.0 * RM_HX, 0, 0),
		v3(0, 0, RF_DESK_SPLIT - RM_ZN), 0.05, 0.10);
	g_rf_two = false;
	g_rf_live = false;
	g_rf_lamp_base = 1;
}

/* The faces of a box that are in `sides`, turned by `turn` about the x
 * axis through `pivot` (rot_x) -- the post box's cap and its door. */
static void rf_box_rx(struct rface_set *s, const char *what, vec3 c, vec3 e, unsigned sides,
	vec3 pivot, double turn, double out, double in)
{
	int from = s->n;
	rf_box(s, what, c, e, sides, pivot, 0.0, out, in);
	for (int i = from; i < s->n; i++) {
		struct rface *f = &s->f[i];
		f->o = vadd(pivot, rot_x(vsub(f->o, pivot), turn));
		f->u = rot_x(f->u, turn);
		f->v = rot_x(f->v, turn);
		f->n = rot_x(f->n, turn);
	}
}

/* The post box, face by face, its door where g_pb_open has it: the foot,
 * the body round the mouth, inside it the floor, the walls and the liner
 * the letters stand against, the cap, and the door -- what stands off the
 * door's face, the slot's brass and its hood and the keyhole, followed
 * down its heights (rf_hit_relief). */
static void rf_post(struct rface_set *s)
{
	const double cx = RM_PB_CX, cz = RM_PB_CZ;
	const double y0 = RM_PB_PLINTH, y1 = RM_PB_SHOULDER;
	const double my0 = y0 + 0.14, my1 = y1 - 0.26;
	const vec3 o0 = v3(0, 0, 0);

	g_rf_at = v3(cx, 0, cz);
	g_rf_cut = true;
	/* every face of it followed down its heights: the lacquer's rounded
	 * edges, laid on the face's plane, met each other's in a crease */
	g_rf_relief = true;
	rf_box(s, "pb-foot", v3(cx, RM_PB_PLINTH * 0.5, cz),
		v3(RM_PB_HX + 0.16, RM_PB_PLINTH * 0.5, RM_PB_HZ + 0.16),
		RF_PZ | RF_PY | RF_PX | RF_NX, o0, 0.0, 0.10, 0.20);
	rf_box(s, "pb-body", v3(cx, (y0 + y1) * 0.5, cz), v3(RM_PB_HX, (y1 - y0) * 0.5, RM_PB_HZ),
		RF_PZ | RF_PX | RF_NX, o0, 0.0, 0.10, 0.25);
	/* inside: the floor, the two walls, and the liner at the back */
	double ihx = RM_PB_HX - 0.26, izc = cz + 0.08, ihz = RM_PB_HZ - 0.18;
	rf_add(s, "pb-floor", v3(cx - ihx, my0, izc + ihz), v3(2.0 * ihx, 0, 0),
		v3(0, 0, -2.0 * ihz), 0.10, 0.20);
	/* (facing in: the left one +x, the right one -x) */
	rf_add(s, "pb-wall", v3(cx - ihx, my0, izc + ihz), v3(0, 0, -2.0 * ihz),
		v3(0, my1 - my0, 0), 0.10, 0.20);
	rf_add(s, "pb-wall", v3(cx + ihx, my0, izc - ihz), v3(0, 0, 2.0 * ihz),
		v3(0, my1 - my0, 0), 0.10, 0.20);
	rf_box(s, "pb-liner", v3(cx, (my0 + my1) * 0.5, cz - RM_PB_HZ + 0.30),
		v3(RM_PB_HX - 0.30, (my1 - my0) * 0.5 - 0.04, 0.05), RF_PZ, o0, 0.0, 0.08, 0.15);
	/* the cap, tilted so the weather runs off the front of it */
	vec3 cc = v3(cx, RM_PB_SHOULDER + RM_PB_CAP_T * 0.5, cz);
	rf_box_rx(s, "pb-cap", cc, v3(RM_PB_HX + 0.14, RM_PB_CAP_T * 0.5, RM_PB_HZ + 0.14),
		RF_PZ | RF_PY | RF_PX | RF_NX, cc, RM_PB_TILT, 0.10, 0.25);
	/* the door, on its hinge along the bottom of the mouth */
	vec3 hinge = v3(cx, y0 + 0.16, cz + RM_PB_HZ);
	double dh = my1 + 0.16 - hinge.y;
	double turn = clampf(g_pb_open, 0.0, 1.0) * RF_DOOR_SWING;
	g_rf_at = hinge;
	g_rf_group = RF_G_DOOR;
	rf_box_rx(s, "pb-door", vadd(hinge, v3(0, dh * 0.5, 0)), v3(RM_PB_HX - 0.06, dh * 0.5, 0.10),
		RF_PZ, hinge, turn, 0.45, 0.25);
	rf_box_rx(s, "pb-door-in", vadd(hinge, v3(0, dh * 0.5, 0)), v3(RM_PB_HX - 0.06, dh * 0.5, 0.10),
		RF_NZ | RF_PY | RF_PX | RF_NX, hinge, turn, 0.10, 0.20);
	g_rf_relief = false;
	g_rf_group = RF_G_NONE;
	g_rf_cut = false;
}

/* The things on the desk and on the bookcase's board, face by face, as
 * room_gadgets makes them: the clock along the back of the desk, in every
 * view but the desk's own (there it is a picture laid over the sheet), and
 * in the bookcase's the three on its board, each on its stepped stand --
 * the leather clock, a rounded block leaning back, and the calendar and
 * the memo pad, tents. The rounding of the leather, the well the dial is
 * set in, the pages and the wire round the ridge are followed down their
 * heights (rf_hit_relief). Through a tent's open ends the inside of its
 * back board shows, and the stand under it. */
static void rf_gadgets(struct rface_set *s, int layer)
{
	const vec3 o0 = v3(0, 0, 0);
	g_rf_relief = true;
	/* small, and full of edges a texel's blur takes the line off */
	g_rf_fine = 2.0;
	for (int i = 0; i < DESK_GADGET_COUNT; i++) {
		const struct room_gadget *g = &ROOM_GADGETS[i];
		bool up = gadget_upright(g);
		if ((up && layer != 3) || (i == DESK_GADGET_CLOCK && layer == 1)) {
			continue;
		}
		double gz = gadget_z(g), base = g->foot + g->plinth;
		if (!up) {
			/* a box, the well of its glass let into the top */
			g_rf_at = v3(g->x, 0, gz);
			g_rf_two = true;
			rf_box(s, "gd-tray", v3(g->x, g->h * 0.5, gz), v3(g->hx, g->h * 0.5, g->hz),
				RF_PY | RF_PZ | RF_PX | RF_NX, o0, 0.0, 0.05, 0.30);
			g_rf_two = false;
			continue;
		}
		bool tent = g->kind == GADGET_TENT;
		g_rf_at = v3(g->x, g->foot, gz);
		/* the stand, both its steps */
		double sd = tent ? cal_depth(g) * 0.5 : g->hz;
		rf_box(s, "gd-stand", v3(g->x, g->foot + g->plinth * 0.51, tent ? gz - sd : gz),
			v3(g->hx + 0.34, g->plinth * 0.51, sd + 0.46), RF_PZ | RF_PY | RF_PX | RF_NX,
			o0, 0.0, 0.06, 0.34);
		if (!tent) {
			/* the block: its face as deep as the leather is rounded off
			 * round it, its top and its sides no deeper than the well the
			 * dial is in starts -- what is under that rim, looked down at,
			 * is not there */
			vec3 b = v3(g->x, base, gz), c = vadd(b, v3(0, g->h * 0.5, 0));
			vec3 e = v3(g->hx, g->h * 0.5, g->hz);
			rf_box_rx(s, "gd-block", c, e, RF_PZ, b, -g->tilt, 0.05, g->hz);
			rf_box_rx(s, "gd-block", c, e, RF_PY | RF_PX | RF_NX, b, -g->tilt, 0.05,
				g->inset - 0.04);
			continue;
		}
		/* the front board from the face of its pages, off which the wire
		 * round the ridge stands no further than RF_TALL */
		double H = g->h, hx = g->hx + 0.01;
		vec3 o = gadget_to_world(g, -hx, -0.01, CAL_PZ);
		rf_add(s, "gd-tent", o, vsub(gadget_to_world(g, hx, -0.01, CAL_PZ), o),
			vsub(gadget_to_world(g, -hx, H + 0.12, CAL_PZ), o), 0.20, 0.24);
		/* the inside of the back board, hung from the ridge */
		vec3 ridge = gadget_to_world(g, 0.0, H, -2.0 * CAL_T);
		double lb = (ridge.y - base) / cos(CAL_TB);
		g_rf_relief = false;
		rf_add(s, "gd-tent-back", vadd(ridge, rot_x(v3(-hx, -lb, 0.0), CAL_TB)),
			v3(2.0 * hx, 0, 0), rot_x(v3(0, lb, 0), CAL_TB), 0.05, 0.05);
		/* the open ends, where the boards' edges are (the ridge, looked
		 * along from the bookcase's height, is the front board's face's) */
		double y1 = base + (H + 0.12) * cos(g->tilt) + 0.30 * sin(g->tilt);
		double z1 = gz + 0.15, z0 = gz - cal_depth(g) - 0.15;
		rf_box(s, "gd-tent-end", v3(g->x, (base + y1) * 0.5, (z0 + z1) * 0.5),
			v3(hx, (y1 - base) * 0.5, (z1 - z0) * 0.5), RF_PX | RF_NX, o0, 0.0, 0.04, 0.14);
		g_rf_relief = true;
	}
	g_rf_relief = false;
	g_rf_fine = 1.0;
}

/* The bookcase's view: the room, the case, the desk. */
static void rf_shelf(struct rface_set *s)
{
	rf_room(s, 3);
	rf_case(s);
	rf_desk(s);
}

/* The console, face by face, where the slide has it (g_cc_out): what
 * map_console makes as far as it is flat -- its two faces, its plinth and
 * ledge, the walnut cheeks and the top of the body between them. What
 * stands up off the faces, the knob and the lamps and the buttons, is in
 * their height fields; what of it is seen from the side is traced. */
static void rf_console(struct rface_set *s)
{
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	const double dz = cc_dz(g_cc_out, g_rm_aspect);
	const vec3 slide = v3(0, 0, dz);
	const double cx = (RM_CC_X0 + RM_CC_X1) * 0.5, chx = (RM_CC_X1 - RM_CC_X0) * 0.5;
	const double inner = chx - RM_CC_CHEEK;
	const vec3 o0 = v3(0, 0, 0);

	g_rf_at = v3(cx, 0, dz);
	g_rf_group = RF_G_CONSOLE;
	g_rf_cut = true;
	g_rf_relief = true;
	const struct cc_face *F[2] = { &panel, &tower };
	static const char *const NAME[2] = { "cc-panel", "cc-tower" };
	g_rf_lamp_base = 0;   /* the glint of the lamp in each lens */
	g_rf_lfine = 3.0;     /* and the knob's shadow across its skirt */
	g_rf_two = true;      /* the brass's sheen as the move starts */
	for (int i = 0; i < 2; i++) {
		/* The tower's knob stands 0.31 off it, and is laid. The panel's
		 * buttons and lamps stand no more than 0.30 off it and are laid
		 * too; the lid, 0.37 off it, is traced, because the console's
		 * sheet without it is laid from these same faces, and what is
		 * under a laid lid is nowhere in them. */
		g_rf_tall = i ? 0.60 : 0.30;
		/* the tower's a little past its top and bottom edges: the tube's
		 * bezel stands out over both */
		double over = i ? 0.22 : 0.0;
		vec3 c = vadd(vadd(F[i]->o, vmul(F[i]->an, 0.17)), slide);
		vec3 o = vsub(vsub(c, vmul(F[i]->ax, inner)), vmul(F[i]->as, F[i]->half + over));
		rf_add(s, NAME[i], o, vmul(F[i]->ax, 2.0 * inner),
			vmul(F[i]->as, 2.0 * (F[i]->half + over)), 0.60, 0.25);
	}
	g_rf_tall = RF_TALL;
	g_rf_lamp_base = 1;
	g_rf_lfine = 1.0;
	g_rf_two = false;
	/* and the tower's top edge, the thickness of it, which its face meets
	 * from the side */
	rf_add(s, "cc-tower-top", vadd(vsub(vsub(vadd(tower.o, vmul(tower.as, tower.half)),
		vmul(tower.an, 0.17)), vmul(tower.ax, inner)), slide), vmul(tower.an, 0.34),
		vmul(tower.ax, 2.0 * inner), 0.10, 0.10);
	/* (and the rest of it, the walnut's rounded edges, as well) */

	/* the top of the body behind the tower, between the cheeks */
	rf_box(s, "cc-body", vadd(v3(cx, (RM_CC_PLINTH + RM_CC_TY1 * 0.72) * 0.5,
		(RM_CC_BODY_Z0 + RM_CC_ZB) * 0.5), slide),
		v3(inner, (RM_CC_TY1 * 0.72 - RM_CC_PLINTH) * 0.5, (RM_CC_BODY_Z0 - RM_CC_ZB) * 0.5),
		RF_PY, o0, 0.0, 0.10, 0.20);
	/* the plinth, and the ledge the nameplate is on */
	double plz0 = RM_CC_PLINTH_Z0, plz1 = RM_CC_ZB + 0.30;
	rf_box(s, "cc-plinth", vadd(v3(cx, RM_CC_PLINTH * 0.5, (plz0 + plz1) * 0.5), slide),
		v3(chx - 0.10, RM_CC_PLINTH * 0.5, (plz0 - plz1) * 0.5),
		RF_PZ | RF_PY | RF_PX | RF_NX, o0, 0.0, 0.10, 0.20);
	double lx0 = RM_CC_PLATE_X0 + 0.10, lx1 = RM_CC_LEDGE_X1;
	double lz0 = RM_CC_PZ0 + RM_CC_LEDGE, lz1 = RM_CC_PLINTH_Z0 - 0.20;
	rf_box(s, "cc-ledge", vadd(v3((lx0 + lx1) * 0.5, RM_CC_PLINTH * 0.5, (lz0 + lz1) * 0.5),
		slide), v3((lx1 - lx0) * 0.5, RM_CC_PLINTH * 0.5, (lz0 - lz1) * 0.5),
		RF_PZ | RF_PY | RF_PX | RF_NX, o0, 0.0, 0.10, 0.20);
	/* the cheeks: tall at the back, low at the front, both sides of each
	 * (the inner one stands up over the faces) */
	for (int sgn = 0; sgn < 2; sgn++) {
		double sx = sgn ? RM_CC_X1 - RM_CC_CHEEK * 0.5 : RM_CC_X0 + RM_CC_CHEEK * 0.5;
		rf_box(s, "cc-cheek", vadd(v3(sx, (RM_CC_PLINTH + RM_CC_TY1) * 0.5,
			(RM_CC_PZ0 + RM_CC_ZB) * 0.5), slide),
			v3(RM_CC_CHEEK * 0.5, (RM_CC_TY1 - RM_CC_PLINTH) * 0.5, (RM_CC_PZ0 - RM_CC_ZB) * 0.5),
			RF_PX | RF_NX, o0, 0.0, 0.10, 0.25);
		rf_box(s, "cc-cheek-lo", vadd(v3(sx, (RM_CC_PLINTH + RM_CC_PY1) * 0.5,
			(RM_CC_PZ0 + RM_CC_PZ1) * 0.5), slide),
			v3(RM_CC_CHEEK * 0.5, (RM_CC_PY1 - RM_CC_PLINTH) * 0.5, (RM_CC_PZ0 - RM_CC_PZ1) * 0.5),
			RF_PY | RF_PZ, o0, 0.0, 0.10, 0.25);
		rf_box(s, "cc-cheek-hi", vadd(v3(sx, (RM_CC_PY1 + RM_CC_TY1) * 0.5,
			(RM_CC_PZ1 + RM_CC_ZB) * 0.5), slide),
			v3(RM_CC_CHEEK * 0.5, (RM_CC_TY1 - RM_CC_PY1) * 0.5, (RM_CC_PZ1 - RM_CC_ZB) * 0.5),
			RF_PY | RF_PZ, o0, 0.0, 0.10, 0.25);
	}
	g_rf_relief = false;
	g_rf_group = RF_G_NONE;
	g_rf_cut = false;
}

/* Each face that moves, where what moves it has it now: the console's,
 * along the slide; the post box's door, on its hinge. Its pictures and its
 * light go with it. */
static void rf_pose(struct rface_set *s)
{
	double dz = cc_dz(g_cc_out, g_rm_aspect);
	double turn = clampf(g_pb_open, 0.0, 1.0) * RF_DOOR_SWING;
	for (int i = 0; i < s->n; i++) {
		struct rface *f = &s->f[i];
		if (f->group == RF_G_DOOR && f->turn != turn) {
			/* swung on the hinge, which is its at */
			double d = turn - f->turn;
			f->o = vadd(f->at, rot_x(vsub(f->o, f->at), d));
			f->u = rot_x(f->u, d);
			f->v = rot_x(f->v, d);
			f->n = rot_x(f->n, d);
			f->turn = turn;
			continue;
		}
		if (f->group != RF_G_CONSOLE || f->pose == dz) {
			continue;
		}
		vec3 d = v3(0, 0, dz - f->pose);
		f->o = vadd(f->o, d);
		f->at = vadd(f->at, d);
		f->pose = dz;
	}
}

/* The desk's own view, from straight above. In its sheet (layer 1) the
 * top, its rounded edges and corners faces of their own that go in and out
 * with the edge or corner they are, the floor round the desk, and the wall
 * and its skirting past the far corners. In the drawer's (2) the drawer
 * all the way out -- its felt, the insides of its sides and of its front
 * and the tops of them -- with the desk's top over the back of it and,
 * under everything, the floor clear: in that sheet nothing else is drawn.
 * Its brass pull, a sliver in front, is traced. */
static void rf_plan(struct rface_set *s, int layer)
{
	const double hx = RM_HX, zf = RM_ZF, wall_z = RM_WALL_Z, e = 0.25;
	const vec3 o0 = v3(0, 0, 0);
	g_rf_two = false;
	if (layer == 2) {
		g_rf_at = o0;
		g_rf_clear = true;
		rf_add(s, "pl-desk-clear", v3(-hx, 0.0, RM_ZN), v3(2.0 * hx, 0, 0),
			v3(0, 0, -(RM_ZN - zf)), 0.0, 0.0);
		rf_add(s, "pl-under", v3(-80.0, RM_DR_BOT - 0.50, 60.0), v3(160.0, 0, 0),
			v3(0, 0, -160.0), 0.0, 0.0);
		g_rf_clear = false;
		/* the drawer, out */
		const double pull = RM_DR_PULL, top = RM_DR_TOP;
		const double felt = RM_DR_BOT + 0.44, ix = RM_DR_HX - 0.42 - 0.22;
		const double zb = RM_DR_BACK + pull, zi = RM_DR_FACE - 0.52 + pull;
		const double zo = RM_DR_FACE + pull;
		rf_add(s, "dr-felt", v3(-ix, felt, zi), v3(2.0 * ix, 0, 0), v3(0, 0, -(zi - zb)),
			0.05, 0.10);
		rf_add(s, "dr-side", v3(-ix, felt, zi), v3(0, 0, -(zi - zb)), v3(0, top - felt, 0),
			0.05, 0.10);
		rf_add(s, "dr-side", v3(ix, felt, zb), v3(0, 0, zi - zb), v3(0, top - felt, 0),
			0.05, 0.10);
		rf_add(s, "dr-side-top", v3(-ix - 0.44, top, zi), v3(0.44, 0, 0), v3(0, 0, -(zi - zb)),
			0.05, 0.12);
		rf_add(s, "dr-side-top", v3(ix, top, zi), v3(0.44, 0, 0), v3(0, 0, -(zi - zb)),
			0.05, 0.12);
		rf_add(s, "dr-front", v3(ix, felt, zi), v3(-2.0 * ix, 0, 0), v3(0, top - felt, 0),
			0.05, 0.10);
		rf_add(s, "dr-front-top", v3(-RM_DR_HX, top, zo), v3(2.0 * RM_DR_HX, 0, 0),
			v3(0, 0, -(zo - zi)), 0.05, 0.14);
		int from = s->n;
		rf_box(s, "dr-pull", v3(0, (RM_DR_TOP + RM_DR_BOT) * 0.5 - 0.08, zo + 0.30),
			v3(2.10, 0.55, 0.40), RF_PX | RF_NX | RF_PY | RF_NY | RF_PZ | RF_NZ, o0, 0.0,
			0.0, 0.0);
		for (int i = from; i < s->n; i++) {
			s->f[i].trace = true;
		}
		return;
	}
	g_rf_live = true;
	/* the top, less its edges, finely: it is what fills the screen */
	g_rf_at = o0;
	g_rf_lamp_base = 0;
	g_rf_tpu = 40.0;
	rf_add(s, "pl-desk", v3(-hx + e, 0.0, RM_ZN - e), v3(2.0 * (hx - e), 0, 0),
		v3(0, 0, -(RM_ZN - zf - 2.0 * e)), 0.05, 0.20);
	/* the edges, along the near and far ones and up the two sides, and
	 * the corners, each where its edge or corner is */
	g_rf_at = v3(0, 0, RM_ZN);
	rf_add(s, "pl-edge-near", v3(-hx + e, 0.0, RM_ZN), v3(2.0 * (hx - e), 0, 0),
		v3(0, 0, -e), 0.05, 0.20);
	g_rf_at = v3(0, 0, zf);
	rf_add(s, "pl-edge-far", v3(-hx + e, 0.0, zf + e), v3(2.0 * (hx - e), 0, 0),
		v3(0, 0, -e), 0.05, 0.20);
	for (int sgn = -1; sgn <= 1; sgn += 2) {
		double x0 = sgn < 0 ? -hx : hx - e;
		g_rf_at = v3(sgn * hx, 0, 0);
		rf_add(s, "pl-edge-side", v3(x0, 0.0, RM_ZN - e), v3(e, 0, 0),
			v3(0, 0, -(RM_ZN - zf - 2.0 * e)), 0.05, 0.20);
		g_rf_at = v3(sgn * hx, 0, RM_ZN);
		rf_add(s, "pl-corner-near", v3(x0, 0.0, RM_ZN), v3(e, 0, 0), v3(0, 0, -e), 0.05, 0.20);
		g_rf_at = v3(sgn * hx, 0, zf);
		rf_add(s, "pl-corner-far", v3(x0, 0.0, zf + e), v3(e, 0, 0), v3(0, 0, -e), 0.05, 0.20);
	}
	g_rf_lamp_base = 1;
	/* the floor and the wall, a long way down and seen past the desk only
	 * once the drawer is out */
	g_rf_tpu = 10.0;
	g_rf_lfine = 0.6;
	g_rf_at = o0;
	rf_add(s, "pl-floor", v3(-48.0, RM_FLOOR, 24.0), v3(96.0, 0, 0),
		v3(0, 0, -(24.0 - wall_z)), 0.05, 0.05);
	g_rf_at = v3(0, 0, wall_z);
	rf_add(s, "pl-wall", v3(-48.0, RM_FLOOR, wall_z), v3(96.0, 0, 0), v3(0, -RM_FLOOR - 1.0, 0),
		0.05, 0.05);
	rf_box(s, "pl-skirting", v3(0, RM_FLOOR + 1.10, wall_z + 0.32), v3(48.0, 1.10, 0.32),
		RF_PZ | RF_PY, o0, 0.0, 0.05, 0.05);
	g_rf_tpu = RF_TPU_MAX;
	g_rf_lfine = 1.0;
	g_rf_live = false;
}

/* What layer is laid from, as the globals have the room. */
static void rf_layer(struct rface_set *s, int layer)
{
	if (layer <= 2) {
		rf_plan(s, layer);
		return;
	}
	/* the console and the post box stand on the desk in every view, each
	 * as its own view has it when you are not looking at it: stowed, shut */
	double cc = g_cc_out, pb = g_pb_open;
	if (layer != 4) {
		g_cc_out = 0.0;
	}
	if (layer != 5) {
		g_pb_open = 0.0;
	}
	if (layer == 3) {
		rf_shelf(s);
	} else {
		rf_room(s, layer);
		rf_desk(s);
	}
	rf_console(s);
	rf_post(s);
	rf_gadgets(s, layer);
	g_cc_out = cc;
	g_pb_open = pb;
}

/* Run row(arg, y) for every y below rows, on this machine's cores. */
struct rf_job {
	void (*row)(void *arg, int y);
	void *arg;
	int rows;
	_Atomic int next;
};

static void *rf_worker(void *p)
{
	struct rf_job *j = p;
	for (;;) {
		int y = atomic_fetch_add(&j->next, 1);
		if (y >= j->rows) {
			break;
		}
		j->row(j->arg, y);
	}
	return NULL;
}

static void rf_run(void (*row)(void *, int), void *arg, int rows)
{
	struct rf_job j = { row, arg, rows, 0 };
	int n = bake_threads();
	pthread_t th[8];
	int started = 0;
	for (int i = 0; i + 1 < n; i++) {
		if (pthread_create(&th[started], NULL, rf_worker, &j) == 0) {
			started++;
		}
	}
	rf_worker(&j);
	for (int i = 0; i < started; i++) {
		pthread_join(th[i], NULL);
	}
}

/* 8 bits to linear light, before the scale */
static float g_rf_lin[256];

static void rf_lin_init(void)
{
	if (g_rf_lin[255] == 0.0f) {
		for (int i = 0; i < 256; i++) {
			g_rf_lin[i] = (float)pow(i / 255.0, 2.2);
		}
	}
}

/* linear, premultiplied (r g b a) to a texel, at scale */
static uint32_t rf_enc(const float *p, double scale)
{
	if (p[3] <= 1e-6f) {
		return 0;
	}
	double a = clampf(p[3], 0.0, 1.0);
	uint32_t px = (uint32_t)(a * 255.0 + 0.5) << 24;
	for (int c = 0; c < 3; c++) {
		double v = pow(clampf(p[c] / p[3] * scale, 0.0, 1.0), 1.0 / 2.2);
		px |= (uint32_t)(v * 255.0 + 0.5) << (16 - 8 * c);
	}
	return px;
}

/* a texel to linear, premultiplied, before the scale */
static void rf_dec(uint32_t px, float *o)
{
	float a = (float)(px >> 24) / 255.0f;
	o[0] = g_rf_lin[(px >> 16) & 255] * a;
	o[1] = g_rf_lin[(px >> 8) & 255] * a;
	o[2] = g_rf_lin[px & 255] * a;
	o[3] = a;
}

/* The smaller pictures of a face, each a quarter of the one before. */
static uint32_t *rf_half(const uint32_t *s, int pw, int ph, int w, int h)
{
	uint32_t *d = malloc((size_t)w * h * sizeof(uint32_t));
	if (!d) {
		return NULL;
	}
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int x0 = 2 * x, y0 = 2 * y;
			int x1 = x0 + 1 < pw ? x0 + 1 : x0, y1 = y0 + 1 < ph ? y0 + 1 : y0;
			float sum[4] = { 0, 0, 0, 0 }, q[4];
			const uint32_t k[4] = {
				s[(size_t)y0 * pw + x0], s[(size_t)y0 * pw + x1],
				s[(size_t)y1 * pw + x0], s[(size_t)y1 * pw + x1],
			};
			for (int i = 0; i < 4; i++) {
				rf_dec(k[i], q);
				for (int c = 0; c < 4; c++) {
					sum[c] += q[c] * 0.25f;
				}
			}
			d[(size_t)y * w + x] = rf_enc(sum, 1.0);
		}
	}
	return d;
}

/* The sizes of a face's levels, from its first. */
static void rf_dims(struct rface *f)
{
	int lv = 1;
	while (lv < RF_LEVELS && (f->tw[lv - 1] > 1 || f->th[lv - 1] > 1)) {
		f->tw[lv] = (f->tw[lv - 1] + 1) / 2;
		f->th[lv] = (f->th[lv - 1] + 1) / 2;
		lv++;
	}
	f->lv = lv;
}

/* whether a face has picture k */
static bool rf_has(const struct rface *f, int k)
{
	return f->base[k] < RF_LEVELS && f->pic[k][f->base[k]];
}

/* Each picture's levels below the one it starts at. */
static void rf_mips(struct rface *f)
{
	rf_dims(f);
	int lv = f->lv;
	for (int k = 0; k < RF_PICS; k++) {
		int b = f->base[k];
		if (b >= lv || !f->pic[k][b]) {
			continue;
		}
		for (int l = b + 1; l < lv; l++) {
			f->pic[k][l] = rf_half(f->pic[k][l - 1], f->tw[l - 1], f->th[l - 1],
				f->tw[l], f->th[l]);
			if (!f->pic[k][l]) {
				f->lv = lv = l;
				break;
			}
		}
	}
}

static void relit_forget(const struct rface_set *s);

static void rf_free(struct rface_set *s)
{
	relit_forget(s);
	for (int i = 0; i < s->n; i++) {
		for (int k = 0; k < RF_PICS; k++) {
			for (int l = 0; l < RF_LEVELS; l++) {
				free(s->f[i].pic[k][l]);
			}
		}
		free(s->f[i].own);
		free(s->f[i].height);
		free(s->f[i].light);
		free(s->f[i].fix[0]);
		free(s->f[i].fix[1]);
		free(s->f[i].light_in);
		free(s->f[i].fix_in[0]);
		free(s->f[i].fix_in[1]);
		free(s->f[i].seen);
	}
}

/* A face's picture at (a, b) of it, at level l, between the four nearest
 * texels; premultiplied. */
static void rf_fetch_from(const struct rface *f, int pic, int l, double a, double b,
	float out[4])
{
	int w = f->tw[l], h = f->th[l];
	double x = a * w - 0.5, y = b * h - 0.5;
	int x0 = (int)floor(x), y0 = (int)floor(y);
	double fx = x - x0, fy = y - y0;
	int x1 = x0 + 1, y1 = y0 + 1;
	x0 = x0 < 0 ? 0 : x0 >= w ? w - 1 : x0;
	x1 = x1 < 0 ? 0 : x1 >= w ? w - 1 : x1;
	y0 = y0 < 0 ? 0 : y0 >= h ? h - 1 : y0;
	y1 = y1 < 0 ? 0 : y1 >= h ? h - 1 : y1;
	const uint32_t *t = f->pic[pic][l];
	float p00[4], p10[4], p01[4], p11[4];
	rf_dec(t[(size_t)y0 * w + x0], p00);
	rf_dec(t[(size_t)y0 * w + x1], p10);
	rf_dec(t[(size_t)y1 * w + x0], p01);
	rf_dec(t[(size_t)y1 * w + x1], p11);
	/* every level is at the picture's scale */
	float k = 1.0f / f->scale[pic];
	for (int c = 0; c < 4; c++) {
		double top = p00[c] + (p10[c] - p00[c]) * fx;
		double bot = p01[c] + (p11[c] - p01[c]) * fx;
		out[c] = (float)(top + (bot - top) * fy) * (c < 3 ? k : 1.0f);
	}
}

struct rf_lay_arg {
	const struct rface_set *s;
	struct room_ctx *ctx;
	float *z;
	unsigned char *need;
	int (*box)[4];     /* each face's samples, x0 y0 x1 y1 */
	unsigned char *traced;   /* a row's worth each: the nearest is traced */
	double along;            /* how far the camera is from eye0 to eye1 */
	double out;              /* how far out what moves is (s->moves) */
	/* or, nothing lit, at each sample what won it: its face and one, and
	 * where on it (rf_mark_views) */
	float *mark;
	/* only the samples set here laid, the others left as they are; and
	 * which face won each, its index and one -- 0: none, or traced. Either
	 * may be NULL (rf_sparse) */
	const unsigned char *only;
	unsigned short *won_by;
	/* a sample stands for its whole pixel: laid in the middle of it, as
	 * its picture is filtered; off the middle, a grain as fine as the
	 * desk's came out of a sample a pixel speckled */
	bool whole;
};

/* Where the ray through (u, v) meets a face's plane: (a, b) on it and how
 * far along the ray; false when it does not, or from behind. */
static bool rf_hit(const struct rface *f, const struct room_cam *c, double u, double v,
	double *a, double *b, double *t)
{
	vec3 rd = vadd(c->ww, vadd(vmul(c->uu, (u - 0.5) * c->fov_x),
		vmul(c->vv, (0.5 - v) * c->fov_y)));
	double den = vdot(rd, f->n);
	if (den > -1e-9) {
		return false;
	}
	*t = vdot(vsub(f->o, c->eye), f->n) / den;
	if (*t <= 0.0) {
		return false;
	}
	vec3 d = vsub(vadd(c->eye, vmul(rd, *t)), f->o);
	*a = vdot(d, f->u) / vdot(f->u, f->u);
	*b = vdot(d, f->v) / vdot(f->v, f->v);
	return true;
}

/* how many samples each face could not find in its picture (the trial) */
static _Atomic long g_rf_missed[RF_MAX];

/* (a, b) on f, as (a, b) on the face its picture is of: the same place on
 * the same thing, which in that room stood elsewhere */
static void rf_move(const struct rface *f, double *a, double *b)
{
	const struct rface *t = f->src;
	vec3 p = vadd(f->o, vadd(vmul(f->u, *a), vmul(f->v, *b)));
	vec3 d;
	if (f->turn != t->turn) {
		/* into the thing's own frame, and out again as it stood */
		vec3 l = rot_x(vsub(p, f->at), -f->turn);
		d = vsub(vadd(t->at, rot_x(l, t->turn)), t->o);
	} else {
		d = vsub(vadd(p, vsub(t->at, f->at)), t->o);
	}
	*a = vdot(d, t->u) / vdot(t->u, t->u);
	*b = vdot(d, t->v) / vdot(t->v, t->v);
}

/* A face's picture (pic: tex or key) at (a, b), lod texels to a sample:
 * between the two nearest levels. */
static void rf_sample(const struct rface *f, int pic, double a, double b, double lod,
	float px[4])
{
	int base = f->base[pic] < f->lv ? f->base[pic] : f->lv - 1;
	if (lod <= base || f->lv == 1) {
		rf_fetch_from(f, pic, base, a, b, px);
		return;
	}
	int l0 = (int)floor(lod);
	double fr = lod - l0;
	if (l0 >= f->lv - 1) {
		rf_fetch_from(f, pic, f->lv - 1, a, b, px);
		return;
	}
	float q[4];
	rf_fetch_from(f, pic, l0, a, b, px);
	rf_fetch_from(f, pic, l0 + 1, a, b, q);
	for (int k = 0; k < 4; k++) {
		px[k] += (float)((q[k] - px[k]) * fr);
	}
}

/* How much of the room's lamp reaches p, on a face facing n, as a part of
 * LIT_KEY_IRRADIANCE: its pool, and whatever stands in the way. */
static double rf_lamp(vec3 p, vec3 n)
{
	struct key_light kl = key_at(p, ROOM_KIND);
	double l = kl.irr / LIT_KEY_IRRADIANCE;
	if (l > 1e-4 && vdot(n, kl.dir) > 0.0) {
		l *= scene_shadow(vadd(p, vmul(n, 0.040)), kl.dir, ROOM_KIND, kl.ang);
	}
	return l;
}

/* Where along the normal through w the face's own surface is: marched in
 * from just outside it. A start inside something else is a place that
 * something stands on, which nothing will ever see. */
static bool rf_surface(const struct rface *f, vec3 w, vec3 *hp, int *id)
{
	vec3 p = vadd(w, vmul(f->n, f->out));
	hit3 h0 = scene_map(p, ROOM_KIND);
	if (h0.d < 0.0 && h0.id > 0) {
		return false;
	}
	double t = 0.0, tmax = f->out + f->in;
	for (int i = 0; i < 96 && t <= tmax; i++) {
		vec3 q = vsub(p, vmul(f->n, t));
		hit3 h = scene_map(q, ROOM_KIND);
		if (h.d < 0.0015 && h.id > 0) {
			*hp = q;
			*id = h.id;
			return true;
		}
		t += fmax(h.d, 0.0008);
	}
	return false;
}

static double rf_light_at(const struct rface *f, double a, double b);

/* Light points no further than this from one at a place that is seen are
 * worked out in full (rf_mark_views): the four round a sample, and those
 * the put right's points need, which are a third further apart. */
#define RF_SEEN_LIGHT 4
#define RF_SEEN_FIX 3

/* whether f's light point nearest (a, b) is within reach of a seen one */
static bool rf_seen_near(const struct rface *f, double a, double b, int reach)
{
	if (!f->seen) {
		return true;
	}
	int x = (int)lround(clampf(a, 0.0, 1.0) * (f->lw - 1));
	int y = (int)lround(clampf(b, 0.0, 1.0) * (f->lh - 1));
	return f->seen[(size_t)y * f->lw + x] <= reach;
}

/* f's put-right factor at (a, b) of it, from one of its grids */
static void rf_fix_from(const struct rface *f, const float *g, double a, double b,
	double out[3])
{
	double x = clampf(a, 0.0, 1.0) * (f->fw - 1), y = clampf(b, 0.0, 1.0) * (f->fh - 1);
	int x0 = (int)x, y0 = (int)y;
	int x1 = x0 + 1 < f->fw ? x0 + 1 : x0, y1 = y0 + 1 < f->fh ? y0 + 1 : y0;
	double fx = x - x0, fy = y - y0;
	for (int c = 0; c < 3; c++) {
		double p00 = g[(y0 * f->fw + x0) * 3 + c], p10 = g[(y0 * f->fw + x1) * 3 + c];
		double p01 = g[(y1 * f->fw + x0) * 3 + c], p11 = g[(y1 * f->fw + x1) * 3 + c];
		double top = p00 + (p10 - p00) * fx, bot = p01 + (p11 - p01) * fx;
		out[c] = top + (bot - top) * fy;
	}
}

/* f's put-right factor at (a, b) of it, as seen from one end of the move */
static void rf_fix_at(const struct rface *f, int e, double a, double b, double out[3])
{
	rf_fix_from(f, f->fix[e], a, b, out);
}

struct rf_fix_arg {
	struct rface *f;
	vec3 eye[2];
	int eyes;
	float *is[2], *was[2];   /* a point each: r g b and whether it counts */
};

static void rf_fix_row(void *p, int y)
{
	struct rf_fix_arg *A = p;
	struct rface *f = A->f;
	const struct rface *tf = f->src;
	/* The picture at the very point the room is shaded at, not at the
	 * spacing of the points: the grain is in both, and cancels only where
	 * both are of the same place -- and so the desk as it was made, as it
	 * is in the picture, whatever it is made of now */
	const double lod = 0.0;
	g_desk_as_made = true;
	for (int x = 0; x < f->fw; x++) {
		double a = (double)x / (f->fw - 1), b = (double)y / (f->fh - 1);
		if (!rf_seen_near(f, a, b, RF_SEEN_FIX)) {
			continue;   /* nobody sees near it: it weighs nothing */
		}
		vec3 w = vadd(f->o, vadd(vmul(f->u, a), vmul(f->v, b)));
		vec3 hp;
		int id;
		bool there = rf_surface(f, w, &hp, &id);
		vec3 n = there ? scene_normal(hp, ROOM_KIND) : f->n;
		double ta = a, tb = b;
		rf_move(f, &ta, &tb);
		for (int e = 0; e < A->eyes; e++) {
			float *o = A->is[e] + ((size_t)y * f->fw + x) * 4;
			float *q = A->was[e] + ((size_t)y * f->fw + x) * 4;
			o[0] = o[1] = o[2] = o[3] = 0.0f;
			q[0] = q[1] = q[2] = q[3] = 0.0f;
			if (!there || ta < 0.0 || ta > 1.0 || tb < 0.0 || tb > 1.0) {
				continue;
			}
			/* what the pictures and the lamp worked out for this room
			 * make of the place, and what this room's light does */
			float px[4], kp[4];
			rf_sample(tf, e ? RF_REST0 : RF_REST, ta, tb, lod, px);
			rf_sample(tf, e ? RF_LAMP0 : RF_LAMP, ta, tb, lod, kp);
			if (px[3] < 0.5f) {
				continue;
			}
			double lamp = f->light ? rf_light_at(f, a, b) : 0.0;
			double ik = kp[3] > 1e-6f ? lamp / kp[3] : 0.0;
			vec3 rd = vnorm(vsub(hp, A->eye[e]));
			vec3 now = shade_surface(hp, n, rd, ROOM_KIND, id, 1.0, 0);
			double was[3] = { px[0] / px[3] + kp[0] * ik, px[1] / px[3] + kp[1] * ik,
				px[2] / px[3] + kp[2] * ik };
			double is[3] = { now.x, now.y, now.z };
			for (int c = 0; c < 3; c++) {
				o[c] = (float)is[c];
				q[c] = (float)was[c];
			}
			o[3] = q[3] = 1.0f;
		}
	}
	g_desk_as_made = false;
}

/* A box of 2r + 1 each way over w by h points of r g b and a weight, the
 * weight too, in place; twice. */
static void rf_soften(float *g, int w, int h, int r)
{
	float *t = malloc((size_t)w * h * 4 * sizeof(float));
	if (!t || r < 1) {
		free(t);
		return;
	}
	for (int pass = 0; pass < 2; pass++) {
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				float sum[4] = { 0, 0, 0, 0 };
				for (int k = -r; k <= r; k++) {
					int xx = x + k < 0 ? 0 : x + k >= w ? w - 1 : x + k;
					const float *p = g + ((size_t)y * w + xx) * 4;
					for (int c = 0; c < 4; c++) {
						sum[c] += p[c] * p[3];
					}
				}
				float *o = t + ((size_t)y * w + x) * 4;
				for (int c = 0; c < 3; c++) {
					o[c] = sum[3] > 1e-6f ? sum[c] / sum[3] : 0.0f;
				}
				o[3] = sum[3] / (2 * r + 1);
			}
		}
		for (int x = 0; x < w; x++) {
			for (int y = 0; y < h; y++) {
				float sum[4] = { 0, 0, 0, 0 };
				for (int k = -r; k <= r; k++) {
					int yy = y + k < 0 ? 0 : y + k >= h ? h - 1 : y + k;
					const float *p = t + ((size_t)yy * w + x) * 4;
					for (int c = 0; c < 4; c++) {
						sum[c] += p[c] * p[3];
					}
				}
				float *o = g + ((size_t)y * w + x) * 4;
				for (int c = 0; c < 3; c++) {
					o[c] = sum[3] > 1e-6f ? sum[c] / sum[3] : 0.0f;
				}
				o[3] = sum[3] / (2 * r + 1);
			}
		}
	}
	free(t);
}

/* The faces whose surroundings change with the room, put right for this
 * one, tpu points to the unit. Returns how many points were worked out. */
static long rf_fix(struct rface_set *s, double tpu)
{
	long n = 0;
	for (int i = 0; i < s->n; i++) {
		struct rface *f = &s->f[i];
		if (!f->live || f->trace || !f->src) {
			continue;
		}
		int eyes = rf_has(f->src, RF_REST0) ? 2 : 1;
		double d = tpu * f->lfine;
		f->fw = (int)ceil(vlen(f->u) * d) + 1;
		f->fh = (int)ceil(vlen(f->v) * d) + 1;
		f->fw = f->fw < 2 ? 2 : f->fw;
		f->fh = f->fh < 2 ? 2 : f->fh;
		bool ok = true;
		for (int e = 0; e < eyes; e++) {
			f->fix[e] = malloc((size_t)f->fw * f->fh * 3 * sizeof(float));
			ok = ok && f->fix[e];
		}
		if (!ok) {
			free(f->fix[0]);
			free(f->fix[1]);
			f->fix[0] = f->fix[1] = NULL;
			continue;
		}
		size_t np = (size_t)f->fw * f->fh;
		struct rf_fix_arg a = { f, { s->eye1, s->eye0 }, eyes, { NULL, NULL }, { NULL, NULL } };
		for (int e = 0; e < eyes; e++) {
			a.is[e] = calloc(np * 4, sizeof(float));
			a.was[e] = calloc(np * 4, sizeof(float));
			ok = ok && a.is[e] && a.was[e];
		}
		if (ok) {
			rf_run(rf_fix_row, &a, f->fh);
			/* Both softened over a unit or so before one is divided by the
			 * other: the grain is in both, a point of it at a time in the
			 * room's light and smoothed in a picture that starts coarse;
			 * what is wanted is how the light differs. */
			int r = (int)ceil(d * 0.6);
			for (int e = 0; e < eyes; e++) {
				rf_soften(a.is[e], f->fw, f->fh, r);
				rf_soften(a.was[e], f->fw, f->fh, r);
				for (size_t k = 0; k < np; k++) {
					const float *o = a.is[e] + k * 4, *q = a.was[e] + k * 4;
					for (int c = 0; c < 3; c++) {
						f->fix[e][k * 3 + c] = o[3] > 1e-3f && q[3] > 1e-3f
							? (float)clampf((o[c] / o[3] + 0.002) / (q[c] / q[3] + 0.002),
								0.2, 5.0)
							: 1.0f;
					}
				}
			}
		} else {
			for (size_t k = 0; k < np * 3; k++) {
				f->fix[0][k] = 1.0f;
				if (f->fix[1]) {
					f->fix[1][k] = 1.0f;
				}
			}
		}
		for (int e = 0; e < eyes; e++) {
			free(a.is[e]);
			free(a.was[e]);
		}
		for (int y = 0; y < f->fh; y++) {
			for (int x = 0; x < f->fw; x++) {
				n += eyes * rf_seen_near(f, (double)x / (f->fw - 1), (double)y / (f->fh - 1),
					RF_SEEN_FIX);
			}
		}
	}
	return n;
}

/* The lamp on f at (a, b) of it, from the points either side: as it is
 * now, or with what moves all the way in. */
static double rf_light_from(const struct rface *f, const float *l, double a, double b)
{
	double x = clampf(a, 0.0, 1.0) * (f->lw - 1), y = clampf(b, 0.0, 1.0) * (f->lh - 1);
	int x0 = (int)x, y0 = (int)y;
	int x1 = x0 + 1 < f->lw ? x0 + 1 : x0, y1 = y0 + 1 < f->lh ? y0 + 1 : y0;
	double fx = x - x0, fy = y - y0;
	double top = l[y0 * f->lw + x0] + (l[y0 * f->lw + x1] - l[y0 * f->lw + x0]) * fx;
	double bot = l[y1 * f->lw + x0] + (l[y1 * f->lw + x1] - l[y1 * f->lw + x0]) * fx;
	return top + (bot - top) * fy;
}

static double rf_light_at(const struct rface *f, double a, double b)
{
	return rf_light_from(f, f->light, a, b);
}

static double rf_height_at(const struct rface *f, double a, double b);

static void rf_light_row(void *p, int y)
{
	struct rface *f = p;
	const struct rface *hf = f->src ? f->src : f;
	for (int x = 0; x < f->lw; x++) {
		double a = (double)x / (f->lw - 1), b = (double)y / (f->lh - 1);
		vec3 w = vadd(f->o, vadd(vmul(f->u, a), vmul(f->v, b)));
		if (f->seen && f->seen[y * f->lw + x] > RF_SEEN_LIGHT) {
			/* nobody sees near it: the lamp's pool, nothing in the way */
			f->light[y * f->lw + x] = (float)(key_at(w, ROOM_KIND).irr / LIT_KEY_IRRADIANCE);
			continue;
		}
		if (hf->height) {
			/* on what stands up off the face there, not under it */
			double ta = a, tb = b;
			if (f->src) {
				rf_move(f, &ta, &tb);
			}
			w = vadd(w, vmul(f->n, rf_height_at(hf, ta, tb)));
		}
		double l = rf_lamp(w, f->n);
		if (f->src && l > 0.0) {
			/* The picture has the lamp at the slant it fell at in the room
			 * it was pictured in, where it hung elsewhere: this room's
			 * slant instead. */
			vec3 then = vnorm(vsub(f->src->lamp, vadd(w, vsub(f->src->at, f->at))));
			vec3 now = key_at(w, ROOM_KIND).dir;
			double was = vdot(f->src->n, then), is = vdot(f->n, now);
			l *= clampf(fmax(is, 0.0) / fmax(was, 0.05), 0.0, 4.0);
		}
		f->light[y * f->lw + x] = (float)l;
	}
}

/* f's light points at tpu to the unit (times its own lfine): lw by lh */
static void rf_light_size(struct rface *f, double tpu)
{
	double d = tpu * f->lfine;
	f->lw = (int)ceil(vlen(f->u) * d) + 1;
	f->lh = (int)ceil(vlen(f->v) * d) + 1;
	f->lw = f->lw < 2 ? 2 : f->lw;
	f->lh = f->lh < 2 ? 2 : f->lh;
}

/* The lamp on every face that is laid, tpu points to the unit, for the
 * room as it is now. Returns how many points were worked out in full. */
static long rf_light(struct rface_set *s, double tpu)
{
	long n = 0;
	for (int i = 0; i < s->n; i++) {
		struct rface *f = &s->f[i];
		if (f->trace || (!f->lv && !f->src)) {
			continue;
		}
		int lw = f->lw, lh = f->lh;
		rf_light_size(f, tpu);
		if (f->seen && (f->lw != lw || f->lh != lh)) {
			free(f->seen);   /* marked on other points */
			f->seen = NULL;
		}
		f->light = malloc((size_t)f->lw * f->lh * sizeof(float));
		if (!f->light) {
			continue;
		}
		rf_run(rf_light_row, f, f->lh);
		for (int k = 0; k < f->lw * f->lh; k++) {
			n += !f->seen || f->seen[k] <= RF_SEEN_LIGHT;
		}
	}
	return n;
}

/* f's height field at (a, b) of its picture's face */
static double rf_height_at(const struct rface *f, double a, double b)
{
	int w = f->tw[0], h = f->th[0];
	double x = clampf(a, 0.0, 1.0) * w - 0.5, y = clampf(b, 0.0, 1.0) * h - 0.5;
	int x0 = (int)floor(x), y0 = (int)floor(y);
	double fx = x - x0, fy = y - y0;
	int x1 = x0 + 1 >= w ? w - 1 : x0 + 1, y1 = y0 + 1 >= h ? h - 1 : y0 + 1;
	x0 = x0 < 0 ? 0 : x0;
	y0 = y0 < 0 ? 0 : y0;
	const float *t = f->height;
	double top = t[(size_t)y0 * w + x0] + (t[(size_t)y0 * w + x1] - t[(size_t)y0 * w + x0]) * fx;
	double bot = t[(size_t)y1 * w + x0] + (t[(size_t)y1 * w + x1] - t[(size_t)y1 * w + x0]) * fx;
	return top + (bot - top) * fy;
}

/* The ray through (u, v) followed down through f's height field, from the
 * plane of its highest point to that of its lowest, a few steps and then
 * between the two either side: where it meets it, (a, b) on f and how far
 * along. False when it does not, or from behind. */
static bool rf_hit_relief(const struct rface *f, const struct room_cam *c, double u, double v,
	double *a, double *b, double *t, bool *wall)
{
	const struct rface *tf = f->src ? f->src : f;
	vec3 rd = vadd(c->ww, vadd(vmul(c->uu, (u - 0.5) * c->fov_x),
		vmul(c->vv, (0.5 - v) * c->fov_y)));
	double den = vdot(rd, f->n);
	if (den > -1e-9) {
		return false;
	}
	/* from a hair above the highest point to a hair below the lowest: a
	 * floor that is all at the lowest, followed down to exactly there, was
	 * missed as often as met */
	double base = vdot(vsub(f->o, c->eye), f->n);
	double t0 = (base + tf->hmax + 0.002) / den, t1 = (base + tf->hmin - 0.002) / den;
	if (t1 <= 0.0) {
		return false;
	}
	t0 = fmax(t0, 1e-4);
	/* a step for every 0.04 of the depth it goes down, at least ten: the
	 * leather of the clock on the board is rounded off 0.7 deep */
	const int N = 10 + (int)fmin((tf->hmax - tf->hmin) / 0.04, 30.0);
	double pt = t0, pd = 0.0;
	for (int i = 0; i <= N; i++) {
		double tt = t0 + (t1 - t0) * i / N;
		vec3 d = vsub(vadd(c->eye, vmul(rd, tt)), f->o);
		double fa = vdot(d, f->u) / vdot(f->u, f->u), fb = vdot(d, f->v) / vdot(f->v, f->v);
		double ta = fa, tb = fb;
		if (f->src) {
			rf_move(f, &ta, &tb);
		}
		double over = vdot(d, f->n) - rf_height_at(tf, ta, tb);
		if (over <= 0.0) {
			double th = i == 0 ? tt : pt + (tt - pt) * pd / fmax(pd - over, 1e-9);
			vec3 e = vsub(vadd(c->eye, vmul(rd, th)), f->o);
			*a = vdot(e, f->u) / vdot(f->u, f->u);
			*b = vdot(e, f->v) / vdot(f->v, f->v);
			*t = th;
			/* how steep the field is where it was met: met from the side,
			 * what stands up off the face is a wall of texels each the
			 * colour of the top of it, and what is under an overhang (the
			 * dial under the rim of its well, looked down at) is not there */
			double ha = *a, hb = *b;
			if (f->src) {
				rf_move(f, &ha, &hb);
			}
			double da = 1.0 / tf->tw[0], db = 1.0 / tf->th[0];
			double ga = (rf_height_at(tf, ha + da, hb) - rf_height_at(tf, ha - da, hb))
				/ (2.0 * da * vlen(tf->u));
			double gb = (rf_height_at(tf, ha, hb + db) - rf_height_at(tf, ha, hb - db))
				/ (2.0 * db * vlen(tf->v));
			*wall = ga * ga + gb * gb > RF_WALL * RF_WALL;
			return true;
		}
		pt = tt;
		pd = over;
	}
	return false;
}

/* whether something stands up off f: its heights are marched (rf_hit_relief) */
static bool rf_relief(const struct rface *f)
{
	const struct rface *hf = f->src ? f->src : f;
	return hf->height && hf->hmax - hf->hmin > 0.004;
}

struct rf_won {
	const struct rface *f, *tf;
	double fa, fb;     /* where on f */
	double a, b, lod;
	vec3 p;
	float px[4];
};

/* ---- the desk laid from its faces, made of another wood ----
 *
 * The faces' pictures are of the desk as it was made, walnut: lit, with the
 * grain in them. Laid for a desk of another wood (desk_set_wood), what each
 * sample shows of the walnut is taken out and the other put in. What a
 * surface sends back is the light that falls on it times its colour, and
 * what its lacquer gives back, which is the light's colour whatever the
 * wood under it (shade_surface): with the walnut's colour known where the
 * sample is, the two are told apart across its three channels, and put
 * together again with the other's. */

/* The desk's own material a face shows, by its name: MID_DESK_TOP; -1 for
 * the drawer's, which is found where each sample is; 0 for none. */
static int rf_desk_mid(const char *what)
{
	if (!what) {
		return 0;
	}
	if (!strcmp(what, "desk") || !strcmp(what, "desk-near") || !strcmp(what, "pl-desk")
		|| !strncmp(what, "pl-edge", 7) || !strncmp(what, "pl-corner", 9)) {
		return MID_DESK_TOP;
	}
	if (!strncmp(what, "dr-", 3) && strcmp(what, "dr-felt") && strcmp(what, "dr-pull")) {
		return -1;
	}
	return 0;
}

/* The colour of a part of the desk at p, its face turned to n: as it was
 * made (walnut, no stain), or as it is now. As icon_material and
 * shade_surface have it, on a flat face. */
static vec3 desk_albedo_at(vec3 p, vec3 n, int mid, bool as_made)
{
	struct mtl m = { .albedo = { 0.5, 0.5, 0.5 }, .rough = 0.45, .edge = 1.0, .coat_rough = 0.08 };
	const struct wood *made = &WOOD_WALNUT;
	switch (mid) {
	case MID_DESK_TOP:
		if (as_made) {
			timber_at(made, p, p.x, p.z, &m, 5.4, 1.00);
		} else {
			desk_wood_at(p, p.x, p.z, &m, 5.4, 1.00);
		}
		desk_stains(p, &m);
		break;
	case MID_DRAWER_FACE:
		if (as_made) {
			timber_at(made, p, p.x, p.y * 2.6, &m, 0.0, 1.10);
		} else {
			desk_wood_at(p, p.x, p.y * 2.6, &m, 0.0, 1.10);
		}
		break;
	case MID_DRAWER_WOOD: {
		bool vert = fabs(p.x) > RM_DR_HX - 1.0;
		if (as_made) {
			timber_at(made, p, vert ? p.z : p.x, vert ? p.y * 3.0 : p.z, &m, 0.0, 0.60);
		} else {
			desk_wood_at(p, vert ? p.z : p.x, vert ? p.y * 3.0 : p.z, &m, 0.0, 0.60);
		}
		m.albedo = vmul(m.albedo, 0.78);
		break;
	}
	default:
		return m.albedo;
	}
	/* the dust that settles on what faces up (shade_surface, on the flat) */
	double up = clampf(n.y, 0.0, 1.0);
	m.albedo = vmix(m.albedo, v3(0.42, 0.40, 0.37), m.dust * up * up * 0.42 * 0.16);
	return m.albedo;
}

/* c, a channel at a time, as l (the light's colour) times (d a + s): how
 * much of it is the surface's colour a, and how much the light's. */
static void lit_split(const double c[3], const double l[3], const double a[3], double *d, double *s)
{
	double yv[3], ma = (a[0] + a[1] + a[2]) / 3.0, my = 0.0;
	for (int i = 0; i < 3; i++) {
		yv[i] = l[i] > 1e-6 ? c[i] / l[i] : 0.0;
		my += yv[i] / 3.0;
	}
	double sxy = 0.0, sxx = 0.0, ay = 0.0, aa = 0.0;
	for (int i = 0; i < 3; i++) {
		sxy += (a[i] - ma) * (yv[i] - my);
		sxx += (a[i] - ma) * (a[i] - ma);
		ay += a[i] * yv[i];
		aa += a[i] * a[i];
	}
	double dd = sxx > 1e-9 ? sxy / sxx : (aa > 1e-12 ? ay / aa : 0.0);
	double ss = my - dd * ma;
	if (ss < 0.0) {
		ss = 0.0;
		dd = aa > 1e-12 ? ay / aa : 0.0;
	}
	if (dd < 0.0) {
		dd = 0.0;
		ss = my;
	}
	*d = dd;
	*s = ss;
}

/* The light on the desk as it was made, a cell at a time: what falls on
 * each part of it from the room and from the lamp, and what its lacquer
 * gives back of each, worked out from the face's pictures against the
 * walnut's own colour there -- both taken over all of the cell, since the
 * light changes slowly over a desk and the grain does not: a sample's
 * colour taken against the walnut's at one point of it read a dark band
 * of the grain as a deal more light (and a desk of another wood came out
 * a fifth too bright). Made once for each picture, whatever wood the desk
 * is to be. */
#define RELIT_CELLS 4.0   /* cells to the unit */
#define RELIT_SUB 4       /* and points a side of each, where it is looked at */
struct relit_map {
	const struct rface *f;   /* the face whose pictures these are of */
	float *m;                /* 4 a cell: the room's d and s, the lamp's; d < 0, none */
	int w, h;
};
static struct relit_map g_relit[32];
static int g_nrelit;
static pthread_mutex_t g_relit_lock = PTHREAD_MUTEX_INITIALIZER;

/* The desk's own material at p, on a face showing mid (rf_desk_mid): the
 * drawer's, which of it is there. 0, none. */
static int desk_mid_at(vec3 p, int mid)
{
	if (mid >= 0) {
		return mid;
	}
	int id = scene_map(p, ROOM_KIND).id;
	return id == MID_DRAWER_FACE || id == MID_DRAWER_WOOD ? id : 0;
}

static struct relit_map relit_make(const struct rface *f, int mid)
{
	struct relit_map rm = { f, NULL, 0, 0 };
	double ul = vlen(f->u), vl = vlen(f->v);
	rm.w = (int)fmax(1.0, ceil(ul * RELIT_CELLS));
	rm.h = (int)fmax(1.0, ceil(vl * RELIT_CELLS));
	rm.m = malloc(sizeof(float) * 4 * (size_t)rm.w * (size_t)rm.h);
	if (!rm.m) {
		return rm;
	}
	/* the pictures looked at as finely as the points are apart */
	double tpu = f->tw[0] / fmax(ul, 1e-6);
	double lod = log2(fmax(tpu / (RELIT_CELLS * RELIT_SUB), 1.0));
	for (int j = 0; j < rm.h; j++) {
		for (int i = 0; i < rm.w; i++) {
			double cr[3] = { 0 }, ck[3] = { 0 }, ca[3] = { 0 };
			int n = 0;
			vec3 mid_p = v3(0, 0, 0);
			for (int q = 0; q < RELIT_SUB * RELIT_SUB; q++) {
				double a = (i + (q % RELIT_SUB + 0.5) / RELIT_SUB) / rm.w;
				double b = (j + (q / RELIT_SUB + 0.5) / RELIT_SUB) / rm.h;
				vec3 p = vadd(f->o, vadd(vmul(f->u, a), vmul(f->v, b)));
				int m = desk_mid_at(p, mid);
				if (!m || (f->height && rf_height_at(f, a, b) > 0.004)) {
					continue;   /* not the desk's own wood here, or something lies on it */
				}
				float px[4], kx[4];
				rf_sample(f, RF_REST, a, b, lod, px);
				rf_sample(f, RF_LAMP, a, b, lod, kx);
				if (px[3] < 0.5f || kx[3] < 1e-6f) {
					continue;
				}
				vec3 al = desk_albedo_at(p, f->n, m, true);
				for (int k = 0; k < 3; k++) {
					cr[k] += px[k] / px[3];
					ck[k] += kx[k] / kx[3];
				}
				ca[0] += al.x;
				ca[1] += al.y;
				ca[2] += al.z;
				mid_p = vadd(mid_p, p);
				n++;
			}
			float *o = rm.m + 4 * ((size_t)j * rm.w + i);
			if (n == 0) {
				o[0] = o[1] = o[2] = o[3] = -1.0f;
				continue;
			}
			for (int k = 0; k < 3; k++) {
				cr[k] /= n;
				ck[k] /= n;
				ca[k] /= n;
			}
			mid_p = vmul(mid_p, 1.0 / n);
			vec3 amb = vadd(sh_irradiance(f->n), shelf_lamp_at(mid_p, f->n));
			const double la[3] = { amb.x, amb.y, amb.z };
			const double lk[3] = { LIT_KEY_COL.x, LIT_KEY_COL.y, LIT_KEY_COL.z };
			double d, sp;
			lit_split(cr, la, ca, &d, &sp);
			o[0] = (float)d;
			o[1] = (float)sp;
			lit_split(ck, lk, ca, &d, &sp);
			o[2] = (float)d;
			o[3] = (float)sp;
		}
	}
	return rm;
}

/* The cells of a set's faces, gone with them: another set may come where
 * they were. */
static void relit_forget(const struct rface_set *s)
{
	pthread_mutex_lock(&g_relit_lock);
	for (int i = 0; i < g_nrelit;) {
		if (g_relit[i].f >= s->f && g_relit[i].f < s->f + RF_MAX) {
			free(g_relit[i].m);
			g_relit[i] = g_relit[--g_nrelit];
		} else {
			i++;
		}
	}
	pthread_mutex_unlock(&g_relit_lock);
}

/* The cells of face f's pictures, made the first time they are wanted. */
static const struct relit_map *relit_of(const struct rface *f, int mid)
{
	pthread_mutex_lock(&g_relit_lock);
	for (int i = 0; i < g_nrelit; i++) {
		if (g_relit[i].f == f) {
			pthread_mutex_unlock(&g_relit_lock);
			return g_relit[i].m ? &g_relit[i] : NULL;
		}
	}
	const struct relit_map *out = NULL;
	if (g_nrelit < (int)(sizeof g_relit / sizeof g_relit[0])) {
		g_relit[g_nrelit] = relit_make(f, mid);
		out = g_relit[g_nrelit].m ? &g_relit[g_nrelit] : NULL;
		g_nrelit++;
	}
	pthread_mutex_unlock(&g_relit_lock);
	return out;
}

/* The light at a, b on the face: the four cells round it, those that have
 * any. False if none of them does. */
static bool relit_at(const struct relit_map *rm, double a, double b, double out[4])
{
	double x = a * rm->w - 0.5, y = b * rm->h - 0.5;
	int x0 = (int)floor(x), y0 = (int)floor(y);
	double fx = x - x0, fy = y - y0, wsum = 0.0;
	out[0] = out[1] = out[2] = out[3] = 0.0;
	for (int k = 0; k < 4; k++) {
		int cx = x0 + (k & 1), cy = y0 + (k >> 1);
		cx = cx < 0 ? 0 : cx >= rm->w ? rm->w - 1 : cx;
		cy = cy < 0 ? 0 : cy >= rm->h ? rm->h - 1 : cy;
		const float *c = rm->m + 4 * ((size_t)cy * rm->w + cx);
		if (c[0] < 0.0f) {
			continue;
		}
		double wt = ((k & 1) ? fx : 1.0 - fx) * ((k >> 1) ? fy : 1.0 - fy) + 1e-6;
		for (int q = 0; q < 4; q++) {
			out[q] += c[q] * wt;
		}
		wsum += wt;
	}
	if (wsum <= 0.0) {
		return false;
	}
	for (int q = 0; q < 4; q++) {
		out[q] /= wsum;
	}
	return true;
}

/* A sample of the desk at p, at a, b on its picture's face, as the desk is
 * now: the light of the cells round it on the colour of the wood it is now
 * made of, there. False if the cells say nothing of it. */
static bool desk_relight(const struct rface *pf, double a, double b, vec3 p, vec3 n, int mid,
	double rest[3], double key[3])
{
	const struct relit_map *rm = relit_of(pf, rf_desk_mid(pf->what));
	double l4[4];
	if (!rm || !relit_at(rm, a, b, l4)) {
		return false;
	}
	vec3 a1 = desk_albedo_at(p, n, mid, false);
	vec3 amb = vadd(sh_irradiance(n), shelf_lamp_at(p, n));
	const double now[3] = { a1.x, a1.y, a1.z };
	const double la[3] = { amb.x, amb.y, amb.z };
	const double lk[3] = { LIT_KEY_COL.x, LIT_KEY_COL.y, LIT_KEY_COL.z };
	for (int i = 0; i < 3; i++) {
		rest[i] = la[i] * (l4[0] * now[i] + l4[1]);
		key[i] = lk[i] * (l4[2] * now[i] + l4[3]);
	}
	return true;
}

static void rf_lay_row(void *p, int y)
{
	struct rf_lay_arg *A = p;
	struct room_ctx *ctx = A->ctx;
	const struct room_cam *c = &ctx->cam;
	int hw = ctx->hw, hh = ctx->hh;
	const unsigned char *only = A->only ? A->only + (size_t)y * hw : NULL;
	/* a desk not as it was made: its samples relit (desk_relight) */
	bool other_desk = bake_look() != 0;
	/* the console's sheet with its lid up (room_bare_make) */
	bool lid_up = ctx->layer == 4 && g_cc_lid > 1.5;
	if (only && !memchr(only, 1, (size_t)hw)) {
		return;
	}
	float *z = A->z + (size_t)y * hw;
	unsigned char *traced = A->traced + (size_t)y * hw;
	struct rf_won *won = calloc((size_t)hw, sizeof *won);
	float *zw = malloc((size_t)hw * sizeof *zw);   /* the nearest wall met */
	if (!won || !zw) {
		free(won);
		free(zw);
		for (int x = 0; x < hw; x++) {
			if (!only || only[x]) {
				A->need[(size_t)y * hw + x] = 1;
			}
		}
		return;
	}
	for (int x = 0; x < hw; x++) {
		z[x] = zw[x] = INFINITY;
		traced[x] = 0;
	}
	for (int i = 0; i < A->s->n; i++) {
		const struct rface *f = &A->s->f[i];
		const int *bx = A->box[i];
		if ((!f->lv && !f->trace && !f->src && !f->clear) || y < bx[1] || y > bx[3]) {
			continue;
		}
		bool relief = rf_relief(f);
		for (int x = bx[0]; x <= bx[2]; x++) {
			if (only && !only[x]) {
				continue;
			}
			int s = (y * hw + x) & 3;
			double u = (x + (A->whole ? 0.5 : SS_OFF[s][0])) / hw;
			double v = (y + (A->whole ? 0.5 : SS_OFF[s][1])) / hh;
			double a, b, t;
			bool wall = false;
			bool hit = relief ? rf_hit_relief(f, c, u, v, &a, &b, &t, &wall)
				: rf_hit(f, c, u, v, &a, &b, &t);
			if (!hit || a < 0.0 || a > 1.0 || b < 0.0 || b > 1.0 || t >= z[x]) {
				continue;
			}
			if (f->cut && vadd(f->o, vadd(vmul(f->u, a), vmul(f->v, b))).z < RM_ZF + 0.02) {
				/* past the desk's far edge: it is not there (map_console,
				 * map_post), and what is behind it shows */
				continue;
			}
			if (wall) {
				zw[x] = fminf(zw[x], (float)t);
				continue;
			}
			if (f->trace || f->clear) {
				z[x] = (float)t;
				traced[x] = f->trace;
				won[x].f = NULL;
				continue;
			}
			vec3 at = vadd(f->o, vadd(vmul(f->u, a), vmul(f->v, b)));
			double fa = a, fb = b;
			/* how many texels a sample spans, from where the next sample
			 * along and the next one down fall on the face's plane, and
			 * this one: met there too, not up on the relief, which a
			 * slanting look moves dozens of texels along -- the brass ring
			 * round the tube came out blurred */
			double ax, bx2, ay, by, pa = a, pb = b, tt;
			double lod = 0.0;
			bool near = (!relief || rf_hit(f, c, u, v, &pa, &pb, &tt))
				&& rf_hit(f, c, u + 1.0 / hw, v, &ax, &bx2, &tt)
				&& rf_hit(f, c, u, v + 1.0 / hh, &ay, &by, &tt);
			/* the picture, which may be another room's face of the same
			 * thing, standing somewhere else */
			const struct rface *tf = f;
			if (f->src) {
				tf = f->src;
				rf_move(f, &a, &b);
				rf_move(f, &pa, &pb);
				rf_move(f, &ax, &bx2);
				rf_move(f, &ay, &by);
				if (a < 0.0 || a > 1.0 || b < 0.0 || b > 1.0 || !tf->lv) {
					z[x] = (float)t;
					traced[x] = 1;
					atomic_fetch_add(&g_rf_missed[i], 1);
					continue;
				}
			}
			if (near) {
				double lx = hypot((ax - pa) * tf->tw[0], (bx2 - pb) * tf->th[0]);
				double ly = hypot((ay - pa) * tf->tw[0], (by - pb) * tf->th[0]);
				lod = log2(fmax(fmax(lx, ly), 1e-6));
			}
			float px[4];
			rf_sample(tf, RF_REST, a, b, lod, px);
			if (px[3] < 0.5f) {
				continue;
			}
			{
				int tx = (int)(a * tf->tw[0]), ty = (int)(b * tf->th[0]);
				tx = tx >= tf->tw[0] ? tf->tw[0] - 1 : tx;
				ty = ty >= tf->th[0] ? tf->th[0] - 1 : ty;
				if (tf->own[(size_t)ty * tf->tw[0] + tx] == RF_OWN_TALL) {
					/* something stands up off it here: traced */
					z[x] = (float)t;
					traced[x] = 1;
					continue;
				}
			}
			z[x] = (float)t;
			traced[x] = 0;
			won[x] = (struct rf_won){ f, tf, fa, fb, a, b, lod, at,
				{ px[0], px[1], px[2], px[3] } };
		}
	}
	/* and lit, once each, by what won */
	for (int x = 0; x < hw; x++) {
		if (only && !only[x]) {
			continue;
		}
		size_t k = (size_t)y * hw + x;
		bool need = !isfinite(z[x]) || traced[x] || zw[x] + RF_WALL_GAP < z[x];
		if (!need && lid_up) {
			/* the Control Panel's lid, up on its hinge, in front of what was laid */
			int s = (y * hw + x) & 3;
			need = cc_lid_ray(c, (x + (A->whole ? 0.5 : SS_OFF[s][0])) / hw,
				(y + (A->whole ? 0.5 : SS_OFF[s][1])) / hh) < z[x];
		}
		A->need[k] = need;
		if (A->won_by) {
			A->won_by[k] = need || !won[x].f ? 0 : (unsigned short)(won[x].f - A->s->f + 1);
		}
		if (A->mark) {
			float *m = A->mark + k * 3;
			m[0] = need || !won[x].f ? 0.0f : (float)(won[x].f - A->s->f + 1);
			m[1] = (float)won[x].fa;
			m[2] = (float)won[x].fb;
			continue;
		}
		float *o = ctx->hi + k * 4;
		if (need || !won[x].f) {
			/* to be traced, or clear */
			o[0] = o[1] = o[2] = o[3] = 0.0f;
			ctx->idhi[k] = 0;
			continue;
		}
		const struct rf_won *w = &won[x];
		float key[4];
		rf_sample(w->tf, RF_LAMP, w->a, w->b, w->lod, key);
		double lamp = w->f->light ? rf_light_at(w->f, w->fa, w->fb) : rf_lamp(w->p, w->f->n);
		if (w->f->light_in && A->out < 1.0) {
			double in = rf_light_from(w->f, w->f->light_in, w->fa, w->fb);
			lamp = in + (lamp - in) * A->out;
		}
		double ia = 1.0 / w->px[3], ik = key[3] > 1e-6f ? 1.0 / key[3] : 0.0;
		double col[3], fix[3] = { 1.0, 1.0, 1.0 };
		if (w->f->fix[0]) {
			rf_fix_at(w->f, 0, w->fa, w->fb, fix);
			if (w->f->fix_in[0] && A->out < 1.0) {
				double in[3];
				rf_fix_from(w->f, w->f->fix_in[0], w->fa, w->fb, in);
				for (int q = 0; q < 3; q++) {
					fix[q] = in[q] + (fix[q] - in[q]) * A->out;
				}
			}
		}
		double rest[3], keyv[3];
		for (int q = 0; q < 3; q++) {
			rest[q] = w->px[q] * ia;
			keyv[q] = key[q] * ik;
		}
		int mid = other_desk ? rf_desk_mid(w->f->what) : 0;
		if (mid && w->tf->height && rf_height_at(w->tf, w->a, w->b) > 0.004) {
			mid = 0;   /* something lying on it there, not the desk */
		}
		if (mid < 0) {
			/* the drawer's: which of it this is, where it is */
			mid = desk_mid_at(w->p, mid);
		}
		if (mid && !desk_relight(w->tf, w->a, w->b, w->p, w->f->n, mid, rest, keyv)) {
			mid = 0;
		}
		for (int q = 0; q < 3; q++) {
			col[q] = (rest[q] + keyv[q] * lamp) * fix[q];
		}
		if (rf_has(w->tf, RF_REST0) && A->along < 1.0) {
			/* nearer the start of the move: the sheen as seen from there */
			float px0[4], key0[4];
			rf_sample(w->tf, RF_REST0, w->a, w->b, w->lod, px0);
			rf_sample(w->tf, RF_LAMP0, w->a, w->b, w->lod, key0);
			double ia0 = px0[3] > 1e-6f ? 1.0 / px0[3] : 0.0;
			double ik0 = key0[3] > 1e-6f ? 1.0 / key0[3] : 0.0;
			double fix0[3] = { 1.0, 1.0, 1.0 };
			if (w->f->fix[1]) {
				rf_fix_at(w->f, 1, w->fa, w->fb, fix0);
				if (w->f->fix_in[1] && A->out < 1.0) {
					double in[3];
					rf_fix_from(w->f, w->f->fix_in[1], w->fa, w->fb, in);
					for (int q = 0; q < 3; q++) {
						fix0[q] = in[q] + (fix0[q] - in[q]) * A->out;
					}
				}
			}
			double rest0[3], keyv0[3];
			for (int q = 0; q < 3; q++) {
				rest0[q] = px0[q] * ia0;
				keyv0[q] = key0[q] * ik0;
			}
			if (mid) {
				/* (the lamp seen from where the move starts: its own pictures' light) */
				desk_relight(w->tf, w->a, w->b, w->p, w->f->n, mid, rest0, keyv0);
			}
			for (int q = 0; q < 3; q++) {
				double c0 = (rest0[q] + keyv0[q] * lamp) * fix0[q];
				col[q] = c0 + (col[q] - c0) * A->along;
			}
		}
		o[0] = (float)col[0];
		o[1] = (float)col[1];
		o[2] = (float)col[2];
		o[3] = 1.0f;
		const struct rface *tf = w->tf;
		int tx = (int)(w->a * tf->tw[0]), ty = (int)(w->b * tf->th[0]);
		tx = tx >= tf->tw[0] ? tf->tw[0] - 1 : tx;
		ty = ty >= tf->th[0] ? tf->th[0] - 1 : ty;
		ctx->idhi[k] = w->f->src && !w->f->own_ok ? 0 : tf->own[(size_t)ty * tf->tw[0] + tx];
	}
	free(won);
	free(zw);
}

/* Lay every face through ctx's camera into its samples; what none of them
 * covers is marked in need, for the tracer. With mark, nothing is lit:
 * what won each sample goes there instead. With only, only the samples it
 * sets are laid; with won_by, which face won each goes there too. whole:
 * a sample a pixel. */
static void rf_lay_into(const struct rface_set *s, struct room_ctx *ctx, unsigned char *need,
	float *mark, const unsigned char *only, unsigned short *won_by, bool whole)
{
	int hw = ctx->hw, hh = ctx->hh;
	float *z = malloc((size_t)hw * hh * sizeof(float));
	unsigned char *traced = malloc((size_t)hw * hh);
	int (*box)[4] = calloc((size_t)s->n, sizeof *box);
	if (!z || !box || !traced) {
		free(z);
		free(box);
		free(traced);
		memset(need, 1, (size_t)hw * hh);
		return;
	}
	for (int i = 0; i < s->n; i++) {
		const struct rface *f = &s->f[i];
		const struct rface *hf = f->src ? f->src : f;
		double up = hf->height ? hf->hmax : 0.0, down = hf->height ? hf->hmin : 0.0;
		double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
		bool all = true;
		for (int k = 0; k < 8; k++) {
			vec3 w = vadd(f->o, vadd(vmul(f->u, k & 1), vmul(f->v, (k >> 1) & 1)));
			w = vadd(w, vmul(f->n, (k >> 2) ? up : down));
			double pu, pv;
			if (!room_project(&ctx->cam, w, &pu, &pv)) {
				all = false;
				break;
			}
			x0 = fmin(x0, pu * hw);
			x1 = fmax(x1, pu * hw);
			y0 = fmin(y0, pv * hh);
			y1 = fmax(y1, pv * hh);
		}
		if (!all) {
			x0 = y0 = 0;
			x1 = hw;
			y1 = hh;
		}
		box[i][0] = (int)fmax(0.0, floor(x0) - 1);
		box[i][1] = (int)fmax(0.0, floor(y0) - 1);
		box[i][2] = (int)fmin(hw - 1.0, ceil(x1) + 1);
		box[i][3] = (int)fmin(hh - 1.0, ceil(y1) + 1);
	}
	vec3 d = vsub(s->eye1, s->eye0);
	double dd = vdot(d, d);
	double along = dd > 1e-9 ? clampf(vdot(vsub(ctx->cam.eye, s->eye0), d) / dd, 0.0, 1.0)
		: 1.0;
	if (getenv("DESK_FACES_ALONG")) {
		along = atof(getenv("DESK_FACES_ALONG"));
	}
	if (getenv("DESK_FACES_DEBUG") && !mark) {
		printf("    along %.3f\n", along);
	}
	struct rf_lay_arg a = { s, ctx, z, need, box, traced, along,
		s->moves == 4 ? clampf(g_cc_out, 0.0, 1.0)
		: s->moves == 5 ? clampf(g_pb_open, 0.0, 1.0) : 1.0, mark, only, won_by, whole };
	rf_run(rf_lay_row, &a, hh);
	free(z);
	free(box);
	free(traced);
}

static void rf_lay(const struct rface_set *s, struct room_ctx *ctx, unsigned char *need)
{
	rf_lay_into(s, ctx, need, NULL, NULL, NULL, ICON_SS == 1);
}

/* ---- the faces as they are shipped ----
 *
 * One file a view: "DSKFACE4", how many faces, and for each a record --
 * what it is and which of those it is, where it and the room's lamp were
 * in the room it was pictured in, how far it was turned on a hinge, its
 * size, the level each picture starts at and the scale of each, how far
 * what stands off it goes -- and after the records, each face's pictures
 * as PNGs (cairo's, so multiplied by their alpha), its owners as an A8 PNG
 * and its heights as another, a length of 0 for one it has not.
 * Native byte order: it is made on the machine it is built on. */

struct rf_rec {
	char what[16];
	int32_t nth, tw, th;
	uint8_t base[RF_PICS];
	double o[3], u[3], v[3], n[3], at[3];
	float scale[RF_PICS];
	uint32_t len[RF_PICS + 2];   /* the pictures, the owners, the heights */
	double lamp[3];
	float hmin, hmax;
	float turn, spare;
};

struct rf_src {
	const unsigned char *at;
	size_t left;
};

static cairo_status_t rf_src_read(void *closure, unsigned char *data, unsigned int len)
{
	struct rf_src *r = closure;
	if (len > r->left) {
		return CAIRO_STATUS_READ_ERROR;
	}
	memcpy(data, r->at, len);
	r->at += len;
	r->left -= len;
	return CAIRO_STATUS_SUCCESS;
}

/* which of the faces called what this one is, in the order they come */
static int rf_nth(const struct rface_set *s, int i)
{
	int k = 0;
	for (int j = 0; j < i; j++) {
		k += !strcmp(s->f[j].what, s->f[i].what);
	}
	return k;
}

/* A pack back into faces, their smaller pictures made; NULL when it is not
 * one. */
static struct rface_set *rf_read(const unsigned char *data, size_t len)
{
	int32_t n;
	if (len < 12 || memcmp(data, "DSKFACE4", 8)) {
		return NULL;
	}
	memcpy(&n, data + 8, sizeof n);
	size_t recs = 12 + (size_t)n * sizeof(struct rf_rec);
	if (n < 0 || n > RF_MAX || recs > len) {
		return NULL;
	}
	struct rface_set *s = calloc(1, sizeof *s);
	if (!s) {
		return NULL;
	}
	rf_lin_init();
	size_t off = recs;
	bool ok = true;
	for (int i = 0; ok && i < n; i++) {
		struct rf_rec r;
		memcpy(&r, data + 12 + (size_t)i * sizeof r, sizeof r);
		struct rface *f = &s->f[s->n++];
		memcpy(s->names[i], r.what, sizeof s->names[i]);
		s->names[i][15] = 0;
		f->what = s->names[i];
		f->nth = r.nth;
		f->o = v3(r.o[0], r.o[1], r.o[2]);
		f->u = v3(r.u[0], r.u[1], r.u[2]);
		f->v = v3(r.v[0], r.v[1], r.v[2]);
		f->n = v3(r.n[0], r.n[1], r.n[2]);
		f->at = v3(r.at[0], r.at[1], r.at[2]);
		f->lamp = v3(r.lamp[0], r.lamp[1], r.lamp[2]);
		f->hmin = r.hmin;
		f->hmax = r.hmax;
		f->turn = r.turn;
		f->tw[0] = r.tw;
		f->th[0] = r.th;
		rf_dims(f);
		for (int k = 0; ok && k <= RF_PICS + 1; k++) {
			if (!r.len[k]) {
				continue;
			}
			int bl = k < RF_PICS ? r.base[k] : 0;
			if (bl >= f->lv) {
				ok = false;
				break;
			}
			int pw = f->tw[bl], ph = f->th[bl];
			if (off + r.len[k] > len) {
				ok = false;
				break;
			}
			struct rf_src src = { data + off, r.len[k] };
			off += r.len[k];
			cairo_surface_t *img = cairo_image_surface_create_from_png_stream(rf_src_read, &src);
			if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS
				|| cairo_image_surface_get_width(img) != pw
				|| cairo_image_surface_get_height(img) != ph) {
				cairo_surface_destroy(img);
				ok = false;
				break;
			}
			cairo_surface_flush(img);
			const unsigned char *d = cairo_image_surface_get_data(img);
			int st = cairo_image_surface_get_stride(img);
			size_t m = (size_t)pw * ph;
			if (k < RF_PICS) {
				f->scale[k] = r.scale[k];
				f->base[k] = bl;
				uint32_t *t = f->pic[k][bl] = malloc(m * sizeof(uint32_t));
				for (int y = 0; t && y < ph; y++) {
					const uint32_t *row = (const uint32_t *)(d + (size_t)y * st);
					for (int x = 0; x < pw; x++) {
						uint32_t p = row[x], a = p >> 24, q = a << 24;
						for (int c = 0; c < 24 && a; c += 8) {
							uint32_t v = (((p >> c) & 255) * 255 + a / 2) / a;
							q |= (v > 255 ? 255 : v) << c;
						}
						t[(size_t)y * pw + x] = q;
					}
				}
				ok = t != NULL;
			} else if (k == RF_PICS + 1) {
				bool a8 = cairo_image_surface_get_format(img) == CAIRO_FORMAT_A8;
				f->height = malloc(m * sizeof(float));
				for (int y = 0; f->height && y < ph; y++) {
					const unsigned char *row = d + (size_t)y * st;
					for (int x = 0; x < pw; x++) {
						int q = a8 ? row[x] : (int)(((const uint32_t *)row)[x] & 255);
						f->height[(size_t)y * pw + x] = f->hmin + (f->hmax - f->hmin) * q / 255.0f;
					}
				}
				ok = f->height != NULL;
			} else {
				/* written as A8, which comes back as grey in RGB24 */
				bool a8 = cairo_image_surface_get_format(img) == CAIRO_FORMAT_A8;
				f->own = malloc(m);
				for (int y = 0; f->own && y < ph; y++) {
					const unsigned char *row = d + (size_t)y * st;
					for (int x = 0; x < pw; x++) {
						f->own[(size_t)y * pw + x] = a8 ? row[x]
							: (unsigned char)(((const uint32_t *)row)[x] & 255);
					}
				}
				ok = f->own != NULL;
			}
			cairo_surface_destroy(img);
		}
		if (ok && (!rf_has(f, RF_REST) || !rf_has(f, RF_LAMP) || !f->own)) {
			ok = false;
		}
		if (ok) {
			rf_mips(f);
		}
	}
	if (!ok) {
		rf_free(s);
		free(s);
		return NULL;
	}
	return s;
}

/* Each face of s that is laid, to its picture in the packs (any of which
 * may be NULL), the first that has it: the face of the same name that came
 * as many times before it. One none has is traced. */
static void rf_link(struct rface_set *s, const struct rface_set *const *packs, int npacks)
{
	for (int i = 0; i < s->n; i++) {
		struct rface *f = &s->f[i];
		if (f->trace || f->clear) {
			continue;
		}
		int k = rf_nth(s, i);
		f->src = NULL;
		for (int p = 0; p < npacks && !f->src; p++) {
			const struct rface_set *q = packs[p];
			for (int j = 0; q && j < q->n && !f->src; j++) {
				if (q->f[j].nth == k && !strcmp(q->f[j].what, f->what)) {
					f->src = &q->f[j];
					/* what can be picked is the view's own pack's to say */
					f->own_ok = p == 0;
				}
			}
		}
		if (!f->src) {
			f->trace = true;
		}
	}
}

/* Supersamples to a sheet: coverage, the vote for who owns each pixel, the
 * glow, and the tonemap. */
static cairo_surface_t *room_resolve(const float *hi, const unsigned char *idhi,
	int w, int h, unsigned char *ids)
{
	int hw = w * ICON_SS;
	float *lin = calloc((size_t)w * h * 4, sizeof(float));
	float *glow = calloc((size_t)w * h * 3, sizeof(float));
	float *tmp = calloc((size_t)w * h * 3, sizeof(float));
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (!lin || !glow || !tmp) {
		free(lin);
		free(glow);
		free(tmp);
		return img;
	}

	const double inv = 1.0 / (ICON_SS * ICON_SS);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			double r = 0, g = 0, b = 0, a = 0;
			int votes[ROOM_ID_MAX] = { 0 };
			for (int sy = 0; sy < ICON_SS; sy++) {
				for (int sx = 0; sx < ICON_SS; sx++) {
					size_t o = ((size_t)(y * ICON_SS + sy)) * hw + (x * ICON_SS + sx);
					const float *sp = hi + o * 4;
					r += sp[0];
					g += sp[1];
					b += sp[2];
					a += sp[3];
					votes[idhi[o] < ROOM_ID_MAX ? idhi[o] : 0]++;
				}
			}
			float *d = lin + ((size_t)y * w + x) * 4;
			/* radiance is per covered sample, so divide by coverage before
			 * the tonemap; premultiplying twice crushes soft edges */
			double cov = fmax(a, 1e-6);
			d[0] = (float)(r / cov * ICON_EXPOSURE);
			d[1] = (float)(g / cov * ICON_EXPOSURE);
			d[2] = (float)(b / cov * ICON_EXPOSURE);
			d[3] = (float)(a * inv);
			if (ids) {
				int best = 0;
				for (int i = 1; i < ROOM_ID_MAX; i++) {
					if (votes[i] > votes[best]) {
						best = i;
					}
				}
				ids[(size_t)y * w + x] = (unsigned char)best;
			}
		}
	}

	for (int i = 0; i < w * h; i++) {
		for (int c = 0; c < 3; c++) {
			glow[i * 3 + c] = (float)fmax(lin[i * 4 + c] - 1.15, 0.0) * lin[i * 4 + 3];
		}
	}
	if (g_bq.glow) {
		blur_pass_rect(glow, tmp, w, h, 1);
		blur_pass_rect(glow, tmp, w, h, 2);
		blur_pass_rect(glow, tmp, w, h, 4);
		blur_pass_rect(glow, tmp, w, h, 8);
	} else {
		memset(glow, 0, (size_t)w * h * 3 * sizeof(float));
	}

	unsigned char *data = cairo_image_surface_get_data(img);
	int stride = cairo_image_surface_get_stride(img);
	for (int y = 0; y < h; y++) {
		uint32_t *out = (uint32_t *)(data + y * stride);
		for (int x = 0; x < w; x++) {
			size_t i = (size_t)y * w + x;
			vec3 c = v3(lin[i * 4 + 0], lin[i * 4 + 1], lin[i * 4 + 2]);
			c = vadd(c, vmul(v3(glow[i * 3 + 0], glow[i * 3 + 1], glow[i * 3 + 2]), 0.48));
			c = tonemap(c);
			double dth = (((x * 7 + y * 13) & 15) / 15.0 - 0.5) / 255.0;
			double al = clampf(lin[i * 4 + 3], 0.0, 1.0);
			uint32_t A = (uint32_t)(al * 255.0 + 0.5);
			uint32_t R = (uint32_t)(clampf(c.x + dth, 0, 1) * al * 255.0 + 0.5);
			uint32_t G = (uint32_t)(clampf(c.y + dth, 0, 1) * al * 255.0 + 0.5);
			uint32_t B = (uint32_t)(clampf(c.z + dth, 0, 1) * al * 255.0 + 0.5);
			out[x] = (A << 24) | (R << 16) | (G << 8) | B;
		}
	}
	cairo_surface_mark_dirty(img);
	free(lin);
	free(glow);
	free(tmp);
	return img;
}

/* Whether the sheet room_bake made last came off a GPU; and whether the
 * next has to come off the CPU whatever the GPU would do (a sheet that has
 * to meet one the CPU made, to the pixel). Only the tracing thread. */
static bool g_room_last_gpu;
static bool g_room_force_cpu;

static bool rf_compose(int layer, struct room_ctx *ctx);
static bool g_room_last_faces;

/* One sheet. layer 1 is the room with the drawer taken out, layer 2 the
 * drawer alone on transparent ground. ids may be NULL. */
static void surface_tent(cairo_surface_t *s);
/* On the processor the first desk at a size is a minute and more, and on a
 * machine of 2010 several. So, when there is no room at all yet: a third of
 * its size at one sample a pixel first, a thirty-sixth of the work, up at
 * once and soft -- the desk to work at, and the lamp up over it, while the
 * real one is traced behind it. Published at its own size, so nothing
 * takes it for the room this screen asked for (desk_scene_ready stays
 * false, and the compositor goes on waiting for that); never kept
 * (room_preview). This is while it is being traced. */
static _Atomic int g_previewing;

static cairo_surface_t *room_bake(int layer, int w, int h, const struct room_cam *cam,
	unsigned char *ids)
{
	if (room_stopping()) {
		/* the session is ending (desk_room_stop): a blank sheet, at once,
		 * for a room that is neither put up nor kept */
		return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	}
	int hw = w * ICON_SS, hh = h * ICON_SS;
	float *hi = calloc((size_t)hw * hh * 4, sizeof(float));
	unsigned char *idhi = calloc((size_t)hw * hh, 1);
	if (!hi || !idhi) {
		free(hi);
		free(idhi);
		return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	}

	struct room_ctx ctx;
	memset(&ctx, 0, sizeof ctx);
	ctx.hi = hi;
	ctx.idhi = idhi;
	ctx.hw = hw;
	ctx.hh = hh;
	ctx.layer = layer;
	ctx.cam = *cam;

	g_room_layer = layer;
	g_room_last_gpu = false;
	/* laid from its faces where there are any and it would be the CPU
	 * tracing it, and only the rest traced */
	g_room_last_faces = rf_compose(layer, &ctx);
#ifdef DESK_HAVE_GPU
	if (!g_room_last_faces) {
		g_room_last_gpu = !g_room_force_cpu && room_trace_gpu(&ctx);
	}
#endif
	if (!g_room_last_gpu && !g_room_last_faces) {
		room_trace_cpu(&ctx);
	}
	if (room_stopping()) {
		/* stopped part way through: not worth resolving */
		free(hi);
		free(idhi);
		return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	}
	cairo_surface_t *img = room_resolve(hi, idhi, w, h, ids);
	free(hi);
	free(idhi);
	if (ICON_SS == 1 && g_bq.tag == 'f' && !atomic_load(&g_previewing) && !g_room_last_faces) {
		/* a sample a pixel: the grain of the wood beats against it; a
		 * tent over the neighbours takes the beat out (laid from faces,
		 * the grain is out of pictures that are already smooth) */
		surface_tent(img);
	}
	return img;
}

/* Where the desk sits inside the baked frame, and how far the drawer runs.
 * The band the drawer occupies is scanned out of its own alpha rather than
 * computed from the box: the brass pull sticks out past the face, and half
 * the animation was empty when the number came from the geometry. */
static void room_metrics(struct room_render *rr)
{
	const struct room_cam *c = &rr->cam;
	double u, v;

	rr->shut_zoom = 1.0;
	rr->shut_pan = 0.0;
	rr->edge = rr->h * 0.5;
	rr->slide = 0.0;

	/* the desk's own band in frame units */
	double v_far = 0.5, v_near = 0.5;
	if (room_project(c, v3(0, 0, RM_ZF), &u, &v)) {
		v_far = v;
	}
	if (room_project(c, v3(0, 0, RM_ZN), &u, &v)) {
		v_near = v;
	}
	double span = fmax(v_near - v_far, 1e-6);
	rr->shut_zoom = 1.0 / span;
	rr->shut_pan = rr->shut_zoom * (0.5 - (v_far + v_near) * 0.5);
	rr->edge = v_near * rr->h;

	double z_shut = RM_DR_FACE;
	if (room_project(c, v3(0, RM_DR_TOP, z_shut), &u, &v)) {
		double v0 = v;
		if (room_project(c, v3(0, RM_DR_TOP, z_shut + RM_DR_PULL), &u, &v)) {
			rr->slide = (v - v0) * rr->h;
		}
	}

	/* scan the drawer sheet for the rows that actually have something in */
	rr->dr_v0 = 0.0;
	rr->dr_v1 = 1.0;
	if (!rr->drawer) {
		return;
	}
	cairo_surface_flush(rr->drawer);
	const unsigned char *data = cairo_image_surface_get_data(rr->drawer);
	int stride = cairo_image_surface_get_stride(rr->drawer);
	int first = -1, last = -1;
	for (int y = 0; y < rr->h; y++) {
		const uint32_t *row = (const uint32_t *)(data + (size_t)y * stride);
		int any = 0;
		for (int x = 0; x < rr->w; x++) {
			if ((row[x] >> 24) > 8) {
				any = 1;
				break;
			}
		}
		if (any) {
			if (first < 0) {
				first = y;
			}
			last = y;
		}
	}
	if (first >= 0) {
		rr->dr_v0 = (double)first / rr->h;
		rr->dr_v1 = (double)(last + 1) / rr->h;
	}
}
