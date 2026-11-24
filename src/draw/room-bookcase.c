/* Part of draw.c: the bookcase, as a shape in the room.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- the bookcase ---------------------------------------------- */

/* Eight box files standing in one row across both bays, four either side of
 * the centre stile. One row of eight reads as a set of eight things you can
 * take down; two stacked groups of four read as a filing system. At the
 * left end of the row a ninth, slimmer -- as thick as the book at the
 * other end -- in mustard buckram: the favourites (DESK_SHELF_FAV, owner
 * 2026-11-10). It leans as the first of the eight does, against it. The
 * table is by place in the row; shelf_row_thing says which thing that is. */
struct shelf_binder {
	double x;        /* centre of the spine */
	double hw;       /* half the spine width */
	double h;        /* how tall it stands off the board */
	double lean;     /* radians about its own foot, + leans right */
	double label_y;  /* centre of the label pocket, above the foot */
	int mid;
};

#define SHELF_ROW (DESK_SHELF_COUNT + 1)

static const struct shelf_binder SHELF_BINDERS[SHELF_ROW] = {
	{ -13.20, 1.65, 9.40, -0.055, 6.10, MID_BINDER_A },
	{  -9.60, 1.65, 9.05,  0.000, 5.90, MID_BINDER_B },
	{  -6.00, 1.65, 9.55,  0.000, 6.20, MID_BINDER_A },
	{  -2.40, 1.65, 9.20,  0.000, 6.00, MID_BINDER_B },
	{   2.40, 1.65, 9.45,  0.000, 6.15, MID_BINDER_A },
	{   6.00, 1.65, 9.10,  0.000, 5.95, MID_BINDER_B },
	{   9.60, 1.65, 9.50,  0.000, 6.20, MID_BINDER_A },
	{  13.20, 1.65, 9.25,  0.000, 6.00, MID_BINDER_B },
	/* its side against the first's, as the book's is against the last's */
	{ -16.07, 1.10, 9.30, -0.055, 6.05, MID_BINDER_FAV },
};

/* the thing on the shelf at place r in the row, and back */
static int shelf_row_thing(int r)
{
	return r < DESK_SHELF_COUNT ? r : DESK_SHELF_FAV;
}

static const struct shelf_binder *shelf_binder_of(int thing)
{
	return thing >= 0 && thing < DESK_SHELF_COUNT ? &SHELF_BINDERS[thing]
		: thing == DESK_SHELF_FAV ? &SHELF_BINDERS[DESK_SHELF_COUNT] : NULL;
}

#define RM_BN_DEPTH 6.40
#define RM_BN_ZF (RM_SH_ZF - 0.55)               /* the spine, facing out */
#define RM_BN_ZC (RM_BN_ZF - RM_BN_DEPTH * 0.5)
/* The binders stand on the UPPER board. The bay under them is left clear
 * on purpose: the control panel and the post box are built into the back
 * edge of the desk, and from up here they stand in front of that bay. They
 * are meant to -- a bookcase behind the things on your desk is the whole
 * point of looking up at it -- but they must not stand in front of the one
 * row you came up here to read. */
#define RM_BN_FOOT (RM_SH_BAY_T + RM_SH_BOARD)

/* Where one binder is, with its lean already applied, as a box a ray can be
 * tested against before any of it is evaluated. */
static void binder_bound(int i, vec3 *c, vec3 *e)
{
	const struct shelf_binder *b = &SHELF_BINDERS[i];
	double sway = fabs(sin(b->lean)) * b->h;
	*c = v3(b->x, RM_BN_FOOT + b->h * 0.5, RM_BN_ZC);
	*e = v3(b->hw + sway * 0.5 + 0.20, b->h * 0.5 + 0.20, RM_BN_DEPTH * 0.5 + 0.20);
}

static hit3 shelf_binder_map(vec3 p, int i)
{
	const struct shelf_binder *b = &SHELF_BINDERS[i];
	hit3 h = { 1e9, 0 };
	/* stand it on its foot and let it lean from there */
	vec3 q = vsub(p, v3(b->x, RM_BN_FOOT, RM_BN_ZC));
	if (b->lean != 0.0) {
		q = rot_z(q, -b->lean);
	}
	q = vsub(q, v3(0, b->h * 0.5, 0));

	/* the boards, with the spine bowed a little the way a full one is */
	double bow = 0.055 * (1.0 - fabs(q.y) / fmax(b->h * 0.5, 0.1));
	double body = sd_round_box(q, v3(b->hw + bow, b->h * 0.5, RM_BN_DEPTH * 0.5), 0.16);

	/* the label pocket, recessed into the spine */
	vec3 lp = vsub(q, v3(0.0, b->label_y - b->h * 0.5, RM_BN_DEPTH * 0.5));
	double pocket = sd_round_box(lp, v3(b->hw * 0.72, 1.55, 0.13), 0.08);
	body = fmax(body, -pocket);
	h = hmin3(h, (hit3){ body, b->mid });
	h = hmin3(h, (hit3){ sd_round_box(vsub(lp, v3(0, 0, -0.10)),
		v3(b->hw * 0.72, 1.55, 0.05), 0.06), MID_BINDER_LABEL });

	/* the block of paper, just visible at the head */
	h = hmin3(h, (hit3){ sd_box(vsub(q, v3(0, b->h * 0.5 - 0.30, -0.35)),
		v3(b->hw - 0.22, 0.26, RM_BN_DEPTH * 0.5 - 0.55)), MID_FOLDER_PAPER });
	return h;
}

/* ---- the book of every program, at the end of the row ----
 *
 * Past the last binder stands a book: every program on the machine in one
 * volume, an index to look a program up in. It is not a box file but a
 * bound book, thick, in navy morocco, its spine rounded and crossed by
 * raised bands, its head gilt; what is tooled on the spine is printed
 * over it in the frame, as the binders' labels are. The case is always at
 * least wide enough for it (shelf_set_width). */
#define RM_BK_HW 1.05          /* half its thickness */
#define RM_BK_H 9.10           /* how tall it stands */
#define RM_BK_D (RM_BN_DEPTH * 0.94)
#define RM_BK_X0 14.97         /* its side against the last binder */
#define RM_BK_GAP 0.30         /* and the room left past it, at least */
#define RM_BK_X (RM_BK_X0 + RM_BK_HW)
#define RM_BK_ZC (RM_BN_ZF - RM_BK_D * 0.5)
#define RM_BK_BANDS 5

/* how far up the spine band k is, from the foot */
static double book_band_y(int k)
{
	return RM_BK_H * (0.20 + 0.16 * k);
}

static void book_bound(vec3 *c, vec3 *e)
{
	*c = v3(RM_BK_X, RM_BN_FOOT + RM_BK_H * 0.5, RM_BK_ZC);
	*e = v3(RM_BK_HW + 0.25, RM_BK_H * 0.5 + 0.25, RM_BK_D * 0.5 + 0.40);
}

static hit3 shelf_book_map(vec3 p)
{
	hit3 h = { 1e9, 0 };
	vec3 q = vsub(p, v3(RM_BK_X, RM_BN_FOOT + RM_BK_H * 0.5, RM_BK_ZC));
	double hz = RM_BK_D * 0.5;
	/* the spine rounded out across its width */
	double ax = clampf(q.x / RM_BK_HW, -1.0, 1.0);
	double bulge = 0.12 * (1.0 - ax * ax);
	double body = sd_round_box(vsub(q, v3(0.0, 0.0, bulge * 0.5)),
		v3(RM_BK_HW, RM_BK_H * 0.5, hz + bulge * 0.5), 0.14);
	/* the head of the leaves, sunk between the boards behind the spine */
	double head = sd_box(vsub(q, v3(0.0, RM_BK_H * 0.5, -0.30)),
		v3(RM_BK_HW - 0.10, 0.13, hz - 0.15));
	body = fmax(body, -head);
	h = hmin3(h, (hit3){ body, MID_BOOK_LEATHER });
	h = hmin3(h, (hit3){ sd_box(vsub(q, v3(0.0, RM_BK_H * 0.5 - 0.20, -0.30)),
		v3(RM_BK_HW - 0.12, 0.07, hz - 0.20)), MID_BOOK_GILT });
	/* the raised bands across the spine */
	for (int k = 0; k < RM_BK_BANDS; k++) {
		double y = book_band_y(k) - RM_BK_H * 0.5;
		h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(0.0, y, hz + bulge - 0.02)),
			v3(RM_BK_HW - 0.03, 0.065, 0.09), 0.06), MID_BOOK_LEATHER });
	}
	return h;
}

/* The books: the ones that fell over at the end of the binder row when
 * somebody pulled a binder out and did not catch them. */
static hit3 shelf_one_book(vec3 p, double x, double foot, double z, double ht,
	double hw, double lean, int mid)
{
	vec3 q = vsub(p, v3(x, foot, z));
	if (lean != 0.0) {
		q = rot_z(q, -lean);
	}
	q = vsub(q, v3(0, ht * 0.5, 0));
	return (hit3){ sd_round_box(q, v3(hw, ht * 0.5, RM_BN_DEPTH * 0.44), 0.10), mid };
}

static hit3 shelf_books(vec3 p)
{
	hit3 h = { 1e9, 0 };

	/* the book of every program, at the end of the row */
	{
		vec3 c, e;
		book_bound(&c, &e);
		double db = sd_box(vsub(p, c), e);
		h = hmin3(h, db > DRAWER_NEAR ? (hit3){ db, 0 } : shelf_book_map(p));
	}
	/* and past it, the ones that fell over, where there is room */
	double x0 = RM_BK_X0 + 2.0 * RM_BK_HW + 0.15;
	double run = g_rm_sh_hx - RM_SH_SIDE - x0;
	if (run > 2.0) {
		for (int i = 0; i < 3; i++) {
			double t = (i + 0.7) / 3.4;
			h = hmin3(h, shelf_one_book(p, x0 + run * t, RM_BN_FOOT,
				RM_BN_ZC + 0.40, 6.6 + room_seed(i, 1) * 2.4, 0.62,
				0.16 + 0.10 * room_seed(i, 2), i & 1 ? MID_BINDER_B : MID_BINDER_A));
		}
	}

	/* The bay under them stays clear. It used to carry two runs of older
	 * volumes; the binders moved up into where those were, and putting
	 * them back down here would only be putting something behind the
	 * control panel and the post box for them to hide. */
	return h;
}

static hit3 map_shelf(vec3 p)
{
	hit3 h = { 1e9, 0 };
	const double zc = (RM_SH_ZF + RM_SH_ZB) * 0.5;
	const double zh = (RM_SH_ZF - RM_SH_ZB) * 0.5;

	/* one bound around the whole case: a ray that is nowhere near it pays
	 * for four box distances and nothing else */
	double gross = sd_box(vsub(p, v3(0.0, (RM_SH_CORN_Y + RM_FLOOR) * 0.5, zc)),
		v3(g_rm_sh_hx + RM_SH_CORN_P, (RM_SH_CORN_Y - RM_FLOOR) * 0.5, zh + RM_SH_CORN_P));
	if (gross > DRAWER_NEAR) {
		return (hit3){ gross, 0 };
	}

	/* the plinth, set back so the case has a toe */
	h = hmin3(h, (hit3){ sd_box(vsub(p,
		v3(0.0, (RM_SH_PLINTH + RM_FLOOR) * 0.5, zc - RM_SH_PLINTH_Z)),
		v3(g_rm_sh_hx - 0.30, (RM_SH_PLINTH - RM_FLOOR) * 0.5, zh - RM_SH_PLINTH_Z)),
		MID_SHELF_CASE });
	/* two sides */
	for (int sgn = 0; sgn < 2; sgn++) {
		double sx = (sgn ? 1.0 : -1.0) * (g_rm_sh_hx - RM_SH_SIDE * 0.5);
		h = hmin3(h, (hit3){ sd_box(vsub(p, v3(sx, (RM_SH_TOP + RM_SH_PLINTH) * 0.5, zc)),
			v3(RM_SH_SIDE * 0.5, (RM_SH_TOP - RM_SH_PLINTH) * 0.5, zh)), MID_SHELF_CASE });
	}
	/* the centre stile: a metre and three quarters of shelf board would sag,
	 * and a real case has one. Stopped short of the front so it reads as a
	 * divider rather than a second front edge. */
	h = hmin3(h, (hit3){ sd_box(vsub(p, v3(0.0, (RM_SH_TOP + RM_SH_PLINTH) * 0.5, zc)),
		v3(RM_SH_SIDE * 0.5, (RM_SH_TOP - RM_SH_PLINTH) * 0.5, zh - 0.30)), MID_SHELF_CASE });
	/* the back, which is the cheap ply a real case has */
	h = hmin3(h, (hit3){ sd_box(vsub(p,
		v3(0.0, (RM_SH_TOP + RM_SH_PLINTH) * 0.5, RM_SH_ZB + RM_SH_BACKT * 0.5)),
		v3(g_rm_sh_hx - RM_SH_SIDE, (RM_SH_TOP - RM_SH_PLINTH) * 0.5, RM_SH_BACKT * 0.5)),
		MID_SHELF_BACK });
	/* the boards, a hair shy of the front so the sides stand proud */
	const double board_top[4] = {
		RM_SH_PLINTH, RM_SH_BAY_B, RM_SH_BAY_T + RM_SH_BOARD, RM_SH_TOP,
	};
	for (int i = 0; i < 4; i++) {
		h = hmin3(h, (hit3){ sd_box(vsub(p,
			v3(0.0, board_top[i] - RM_SH_BOARD * 0.5, zc + 0.12)),
			v3(g_rm_sh_hx - RM_SH_SIDE, RM_SH_BOARD * 0.5, zh - 0.12)), MID_SHELF_BOARD });
	}
	/* a moulded cap, proud on the front and both ends */
	h = hmin3(h, (hit3){ sd_round_box(vsub(p,
		v3(0.0, (RM_SH_CORN_Y + RM_SH_TOP) * 0.5, zc + RM_SH_CORN_P * 0.5)),
		v3(g_rm_sh_hx + RM_SH_CORN_P, (RM_SH_CORN_Y - RM_SH_TOP) * 0.5,
			zh + RM_SH_CORN_P * 0.5), 0.16), MID_SHELF_CASE });

	/* the two shelf lamps: a brass tube in a shade, screwed up under the
	 * board they light from */
	for (int i = 0; i < 2; i++) {
		double lx = i ? 8.2 : -8.2;
		vec3 q = vsub(p, v3(lx, SHELF_LAMP_Y + 0.30, RM_SH_ZF - 0.15));
		double shade = sd_round_box(q, v3(2.30, 0.26, 0.40), 0.16);
		shade = fmax(shade, -sd_round_box(vsub(q, v3(0, -0.24, 0)),
			v3(2.10, 0.22, 0.26), 0.10));
		h = hmin3(h, (hit3){ shade, MID_CC_BRASS });
		h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(0, -0.22, 0)),
			v3(1.95, 0.10, 0.17), 0.09), MID_CC_LENS_AMBER });
		/* the bracket back to the board it is screwed to */
		h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(0, 0.30, -0.55)),
			v3(0.26, 0.20, 0.55), 0.06), MID_CC_BRASS });
	}

	for (int i = 0; i < SHELF_ROW; i++) {
		vec3 c, e;
		binder_bound(i, &c, &e);
		double db = sd_box(vsub(p, c), e);
		if (db > DRAWER_NEAR) {
			h = hmin3(h, (hit3){ db, 0 });
			continue;
		}
		h = hmin3(h, shelf_binder_map(p, i));
	}
	h = hmin3(h, shelf_books(p));
	return h;
}

/* Which half of the room a surface belongs to. The scene is baked twice --
 * once with the drawer slot empty, once with nothing but the drawer -- so
 * that sliding one sheet over the other opens it in real time.
 *
 * Asked as a question about the material, not about a box: the objects
 * standing on the felt are taller than the box they stand in, and a box
 * test cut their tops off the drawer sheet, leaving holes through to the
 * desk behind. */
static int room_is_drawer(int id)
{
	switch (id) {
	case MID_ROOM_FLOOR:
	case MID_ROOM_WALL:
	case MID_ROOM_SKIRT:
	case MID_DESK_TOP:
	case MID_DESK_BODY:
	case MID_GADGET_BODY:
	case MID_GADGET_GLASS:
	case MID_GADGET_LEATHER:
	case MID_GADGET_DIAL:
	case MID_GADGET_PAPER:
		return 0;
	default:
		return 1;
	}
}

/* Declared up with the gadgets, which need to know whether the bookcase is
 * in this layer before they decide whether they exist. */

static hit3 map_room(vec3 p)
{
	hit3 h = { 1e9, 0 };

	if (g_room_layer != 2) {
		/* The wall stands behind the bookcase when there is one, and behind
		 * the desk when there is not. Moving it for every layer would change
		 * what the clock's glass reflects, and the desk sheets have to come
		 * back bit-identical. */
		double wall_z = (g_room_layer >= 3) ? (RM_SH_ZB - 0.50) : RM_WALL_Z;
		h = hmin3(h, (hit3){ p.y - RM_FLOOR, MID_ROOM_FLOOR });
		h = hmin3(h, (hit3){ p.z - wall_z, MID_ROOM_WALL });
		h = hmin3(h, (hit3){ sd_box(vsub(p, v3(0, RM_FLOOR + 1.10, wall_z + 0.32)),
			v3(90.0, 1.10, 0.32)), MID_ROOM_SKIRT });
		if (g_room_layer == 3) {
			h = hmin3(h, map_shelf(p));
		}
		/* The console and the post box are part of the desk, so they are
		 * in every view of it -- in the desk's own sheet as pictures laid
		 * over it (room_cases_paint), shadows and all. */
		if (g_room_layer != 1) {
			h = hmin3(h, map_console(p));
			h = hmin3(h, map_post(p));
		}
	}

	/* the top board, bullnosed along the front */
	h = hmin3(h, (hit3){ sd_round_box(vsub(p, v3(0, -RM_TOP_T * 0.5, (RM_ZN + RM_ZF) * 0.5)),
		v3(RM_HX, RM_TOP_T * 0.5, (RM_ZN - RM_ZF) * 0.5), 0.16), MID_DESK_TOP });

	if (g_room_layer != 2) {
		/* end panels and a back rail, so the underside is a carcass */
		for (int s = 0; s < 2; s++) {
			double sx = s ? (RM_HX - 0.85) : -(RM_HX - 0.85);
			h = hmin3(h, (hit3){ sd_box(vsub(p, v3(sx, -9.5, (RM_ZN + RM_ZF) * 0.5 + 0.5)),
				v3(0.52, 9.0, (RM_ZN - RM_ZF) * 0.5 - 0.6)), MID_DESK_BODY });
		}
		h = hmin3(h, (hit3){ sd_box(vsub(p, v3(0, -2.6, RM_ZF + 0.6)),
			v3(RM_HX - 0.6, 2.1, 0.38)), MID_DESK_BODY });
		/* the rails the drawer runs between */
		h = hmin3(h, (hit3){ sd_box(vsub(p, v3(0, -0.70, RM_ZN - 0.34)),
			v3(RM_HX - 0.6, 0.28, 0.34)), MID_DESK_BODY });
		h = hmin3(h, (hit3){ sd_box(vsub(p, v3(0, -4.10, RM_ZN - 0.34)),
			v3(RM_HX - 0.6, 0.28, 0.34)), MID_DESK_BODY });
		h = hmin3(h, room_gadgets(p));
	}

	if (g_room_layer == 1 || g_room_layer >= 4) {
		return h;
	}

	/* the drawer: in the drawer sheet (2) an empty one -- what is in it is
	 * drawn and laid in afterwards, wherever it has been put
	 * (desk_drawer_paint_items) */
	vec3 dp = vsub(p, v3(0, 0, room_pull()));
	double cy = (RM_DR_TOP + RM_DR_BOT) * 0.5;
	double chy = (RM_DR_TOP - RM_DR_BOT) * 0.5;
	h = hmin3(h, (hit3){ sd_round_box(vsub(dp, v3(0, cy, RM_DR_FACE - 0.26)),
		v3(RM_DR_HX, chy, 0.26), 0.10), MID_DRAWER_FACE });

	double in_hx = RM_DR_HX - 0.42;
	double in_zc = (RM_DR_FACE - 0.52 + RM_DR_BACK) * 0.5;
	double in_zh = (RM_DR_FACE - 0.52 - RM_DR_BACK) * 0.5;
	double felt_y = RM_DR_BOT + 0.22
		- (g_room_layer == 2 ? 0.0 : felt_dip(vsub(dp, v3(0, 0, RM_ITEM_Z))));
	h = hmin3(h, (hit3){ sd_box(vsub(dp, v3(0, felt_y, in_zc)),
		v3(in_hx, 0.22, in_zh)), MID_FELT });
	for (int s = 0; s < 2; s++) {
		double sx = s ? in_hx : -in_hx;
		h = hmin3(h, (hit3){ sd_box(vsub(dp, v3(sx, cy + 0.16, in_zc)),
			v3(0.22, chy - 0.16, in_zh)), MID_DRAWER_WOOD });
	}
	h = hmin3(h, (hit3){ sd_box(vsub(dp, v3(0, cy + 0.16, RM_DR_BACK - 0.22)),
		v3(in_hx, chy - 0.16, 0.22)), MID_DRAWER_WOOD });

	/* brass cup pull */
	vec3 hp = vsub(dp, v3(0, cy - 0.08, RM_DR_FACE + 0.02));
	double plate = sd_round_box(hp, v3(1.95, 0.42, 0.10), 0.09);
	double cup = sd_round_box(vsub(hp, v3(0, -0.30, 0.16)), v3(1.62, 0.34, 0.24), 0.16);
	cup = fmax(cup, -sd_round_box(vsub(hp, v3(0, -0.46, 0.40)), v3(1.44, 0.34, 0.26), 0.14));
	h = hmin3(h, (hit3){ fmin(plate, cup), MID_BRASS });

	if (g_room_layer != 2) {
		h = hmin3(h, room_items(dp));
	}
	return h;
}

static hit3 scene_map(vec3 p, int kind)
{
	switch (kind) {
	case DESK_ICON_TERM:
		return map_term(p);
	case DESK_ICON_GLOBE:
		return map_globe(p);
	case DESK_ICON_FOLDER:
		return map_folder(p);
	case DESK_ICON_NOTES:
		return map_notes(p);
	case DESK_ICON_GEAR:
		return map_gear(p);
	case DRAWER_KIND:
		return map_drawer(p);
	case ROOM_KIND:
		return map_room(p);
	default:
		return (hit3){ 1e9, 0 };
	}
}

static int bound_hit(vec3 ro, vec3 rd, int kind, double *t0, double *t1)
{
	if (kind == DRAWER_KIND || kind == ROOM_KIND) {
		*t0 = 0.0;
		*t1 = 260.0;
		return 1;
	}
	double b = vdot(ro, rd);
	double c = vdot(ro, ro) - SCENE_BOUND * SCENE_BOUND;
	double disc = b * b - c;
	if (disc < 0.0) {
		return 0;
	}
	double s = sqrt(disc);
	double e0 = -b - s, e1 = -b + s;
	if (e1 < 0.0) {
		return 0;
	}
	*t0 = fmax(e0, 0.0);
	*t1 = e1;
	return 1;
}

static vec3 scene_normal(vec3 p, int kind)
{
	const double e = (kind == ROOM_KIND || kind == DRAWER_KIND) ? 0.0035 : 0.0012;
	vec3 k1 = v3(1, -1, -1), k2 = v3(-1, -1, 1), k3 = v3(-1, 1, -1), k4 = v3(1, 1, 1);
	return vnorm(vadd(vadd(
		vmul(k1, scene_map(vadd(p, vmul(k1, e)), kind).d),
		vmul(k2, scene_map(vadd(p, vmul(k2, e)), kind).d)),
		vadd(
		vmul(k3, scene_map(vadd(p, vmul(k3, e)), kind).d),
		vmul(k4, scene_map(vadd(p, vmul(k4, e)), kind).d))));
}

/* Mean curvature from the same four taps. Positive on a convex edge, which
 * is exactly where paint rubs off and metal polishes bright. */
static double scene_curvature(vec3 p, int kind)
{
	const double e = (kind == ROOM_KIND || kind == DRAWER_KIND) ? 0.030 : 0.010;
	vec3 k1 = v3(1, -1, -1), k2 = v3(-1, -1, 1), k3 = v3(-1, 1, -1), k4 = v3(1, 1, 1);
	double s = scene_map(vadd(p, vmul(k1, e)), kind).d
		+ scene_map(vadd(p, vmul(k2, e)), kind).d
		+ scene_map(vadd(p, vmul(k3, e)), kind).d
		+ scene_map(vadd(p, vmul(k4, e)), kind).d;
	return clampf(-s / (e * 4.0) * 0.5, -1.0, 1.0);
}

/* Penumbra tracking soft shadow, width tied to the lamp's angular size. */
static double scene_shadow(vec3 ro, vec3 rd, int kind, double ang)
{
	double tmin, tmax;
	if (!bound_hit(ro, rd, kind, &tmin, &tmax)) {
		return 1.0;
	}
	int big = (kind == ROOM_KIND || kind == DRAWER_KIND);
	if (big) {
		tmax = 40.0;
	}
	double k = 1.0 / fmax(ang, 0.01);
	double t = fmax(tmin, big ? 0.05 : 0.015), res = 1.0, ph = 1e9;
	int steps = big ? 40 : 28;
	for (int i = 0; i < steps && t < tmax; i++) {
		double d = scene_map(vadd(ro, vmul(rd, t)), kind).d;
		if (d < (big ? 0.004 : 0.0008)) {
			return 0.0;
		}
		double y = d * d / (2.0 * ph);
		double dd = sqrt(fmax(d * d - y * y, 0.0));
		res = fmin(res, k * dd / fmax(0.0001, t - y));
		ph = d;
		t += clampf(d, big ? 0.05 : 0.010, big ? 1.40 : 0.22);
	}
	res = clampf(res, 0.0, 1.0);
	return res * res * (3.0 - 2.0 * res);
}

static double scene_ao(vec3 p, vec3 n, int kind)
{
	double scale = (kind == ROOM_KIND || kind == DRAWER_KIND) ? 2.2 : 1.0;
	double occ = 0.0, sca = 1.0;
	for (int i = 1; i <= 5; i++) {
		double dist = 0.022 * i * i * scale;
		double d = scene_map(vadd(p, vmul(n, dist)), kind).d;
		occ += (dist - d) * sca / scale;
		sca *= 0.72;
	}
	return clampf(1.0 - occ * 1.70, 0.05, 1.0);
}

static double continent(vec3 n)
{
	double lon = atan2(n.x, n.z);
	double lat = n.y;
	double d = 0.0;
	d = fmax(d, 0.80 - sqrt((lon + 0.55) * (lon + 0.55) * 1.7 + (lat - 0.06) * (lat - 0.06) * 3.5));
	d = fmax(d, 0.52 - sqrt((lon + 0.32) * (lon + 0.32) * 4.2 + (lat - 0.58) * (lat - 0.58) * 6.4));
	d = fmax(d, 0.74 - sqrt((lon - 1.18) * (lon - 1.18) * 1.25 + (lat - 0.40) * (lat - 0.40) * 3.1));
	d = fmax(d, 0.70 - sqrt((lon + 1.88) * (lon + 1.88) * 1.55 + (lat - 0.46) * (lat - 0.46) * 2.5));
	d = fmax(d, 0.60 - sqrt((lon + 1.38) * (lon + 1.38) * 3.6 + (lat + 0.28) * (lat + 0.28) * 2.15));
	d = fmax(d, 0.48 - sqrt((lon - 2.15) * (lon - 2.15) * 4.6 + (lat + 0.42) * (lat + 0.42) * 8.2));
	d += (room_fbm(lon * 3.1 + 2.0, lat * 3.4) - 0.5) * 0.24;
	return d;
}

/* Measured extent of an object, by walking a grid over its own space. Used
 * to stand it on the felt and to size its bounding sphere. */
static void icon_bounds(int kind, vec3 *lo, vec3 *hi)
{
	const int n = 60;
	const double ext = 1.20;
	double step = 2.0 * ext / n;
	*lo = v3(1e9, 1e9, 1e9);
	*hi = v3(-1e9, -1e9, -1e9);
	for (int i = 0; i < n; i++) {
		double x = -ext + (i + 0.5) * step;
		for (int j = 0; j < n; j++) {
			double y = -ext + (j + 0.5) * step;
			for (int k = 0; k < n; k++) {
				double z = -ext + (k + 0.5) * step;
				if (scene_map(v3(x, y, z), kind).d >= step * 0.87) {
					continue;
				}
				if (x < lo->x) lo->x = x;
				if (y < lo->y) lo->y = y;
				if (z < lo->z) lo->z = z;
				if (x > hi->x) hi->x = x;
				if (y > hi->y) hi->y = y;
				if (z > hi->z) hi->z = z;
			}
		}
	}
	if (lo->x > hi->x) {
		*lo = v3(-0.5, -0.5, -0.5);
		*hi = v3(0.5, 0.5, 0.5);
	}
}

struct mtl {
	vec3 albedo;
	double rough;
	double metal;
	vec3 emit;
	double coat;       /* clear lacquer on top, 0 for none */
	double coat_rough;
	double bump_freq;  /* procedural micro relief */
	double bump_amp;
	double dust;       /* how readily dust settles on upward faces */
	double edge;       /* how hard the convex edges have been rubbed, 1 = as built */
};

/* A timber: its colours, and how its grain is made. Walnut is the room's;
 * the desk may be of another (desk_set_wood), and the bookcase and the
 * console stay walnut, being other furniture. */
struct wood {
	vec3 light, dark;    /* earlywood and latewood */
	double tone[4];      /* how each goes with the board: light's least and span, dark's */
	vec3 fleck;          /* added where a medullary ray is cut */
	double fleck_w;      /* rays to the unit across the grain: walnut's are fine */
	double fleck_p;      /* and how few of them show: the higher, the fewer */
	vec3 sap;            /* the odd streak of sapwood */
	double pore;         /* how dark the open pores are */
	double rate;         /* rings to the unit near the pith */
	double late;         /* how narrow the latewood band is */
	vec3 faded;          /* what the sun leaves of it */
	double dust;         /* how much the dust that settles on it shows: on black, less */
};

#define WOOD_WALNUT_INIT { { 0.234, 0.114, 0.049 }, { 0.052, 0.021, 0.009 }, \
	{ 0.80, 0.44, 0.82, 0.38 }, { 0.14, 0.11, 0.07 }, 46.0, 7.0, { 0.265, 0.190, 0.124 }, 0.30, \
	7.2, 2.7, { 0.208, 0.156, 0.112 }, 1.0 }
static const struct wood WOOD_WALNUT = WOOD_WALNUT_INIT;

/* Timber, in three dimensions. Rings are the distance from a pith running
 * down the length of the board, so the figure opens into cathedrals where
 * the saw cut across the growth and closes into stripes where it did not --
 * which is the thing that separates real timber from a brown gradient. */
static void timber_at(const struct wood *wd, vec3 q, double u, double v, struct mtl *m, double plank,
	double use)
{
	double board = plank > 0.0 ? floor(u / plank) : 0.0;
	double bu = plank > 0.0 ? u - board * plank : u;
	double jitter = room_seed((int)board, 3);

	/* the pith sits off to one side of each board and wanders a little */
	double pith = plank * (0.24 + jitter * 0.52);
	double wob = room_fbm(v * 0.09 + board * 3.7, 4.2) - 0.5;
	double r = fabs(bu - pith + wob * plank * 0.30);

	/* Rings. Evenly spaced rings are a corduroy, and a corduroy is the one
	 * thing no tree has ever produced: some years were good and some were
	 * not, so the spacing swells and tightens as you go out from the pith,
	 * and the line of each ring wanders along the length of the board. */
	double season = room_fbm2(r * 0.52 + board * 5.3, 2.7) - 0.5;
	double rate = (wd->rate + jitter * 2.4) * (1.0 + season * 0.85);
	double ring = r * rate
		+ room_fbm(v * 0.16 + board * 2.1, r * 0.8) * 3.4
		+ room_fbm2(v * 0.62 + board * 9.1, r * 1.9) * 1.25;
	/* and latewood is a narrow hard band, not half of a sine */
	double g = 0.5 + 0.5 * sin(ring * 2.0 * M_PI);
	g = pow(g, wd->late);

	/* cathedral: where the ring spacing widens the grain flares */
	double cath = smoothstepf(0.0, plank * 0.18, r);
	g = vmix(v3(g, 0, 0), v3(g * 0.55 + 0.28, 0, 0), 1.0 - cath).x;

	/* No two boards came off the same part of the log. */
	double btone = room_seed((int)board, 4);
	vec3 light = vmul(wd->light, wd->tone[0] + btone * wd->tone[1]);
	vec3 dark = vmul(wd->dark, wd->tone[2] + btone * wd->tone[3]);
	m->albedo = vmix(light, dark, clampf(g, 0, 1));

	/* medullary fleck and the odd streak of sapwood */
	double fleck = pow(room_noise(u * wd->fleck_w, v * 7.0), wd->fleck_p);
	m->albedo = vadd(m->albedo, vmul(wd->fleck, fleck));
	double sap = smoothstepf(0.62, 0.86, room_fbm(u * 0.5 + 31.0, v * 0.11));
	m->albedo = vmix(m->albedo, wd->sap, sap * 0.35);

	/* open pores: short dashes that run with the grain, not with the ring */
	double pore = pow(room_noise(u * 130.0, v * 6.5), 4.0);
	m->albedo = vmul(m->albedo, 1.0 - pore * wd->pore);

	if (plank > 0.0) {
		double seam = fabs(bu - plank * 0.5);
		seam = smoothstepf(plank * 0.5, plank * 0.5 - 0.055, seam);
		m->albedo = vmul(m->albedo, 1.0 - seam * 0.55);
	}

	m->rough = 0.26 + g * 0.13 + pore * 0.22;
	/* Satin, and unevenly so. A full gloss coat over the whole top put one
	 * broad white sheen across everything, which is what a piece of
	 * furniture looks like on the day it leaves the shop and never again. */
	m->coat = 0.26 + room_fbm2(u * 0.42 + 8.0, v * 0.42) * 0.14;
	m->coat_rough = 0.19 + room_fbm(u * 0.8, v * 0.8) * 0.085;
	m->bump_freq = 26.0;
	m->bump_amp = 0.030;
	m->dust = wd->dust;

	if (use <= 0.0) {
		return;
	}

	/* ---- and then somebody used it for twenty years ---- */

	double touch = clampf(use * (0.30 + 0.85 * room_touch(q)), 0.0, 1.0);

	/* Sun. The dye goes out of walnut before anything else does, and it
	 * goes out unevenly, because things stood on it for a while and then
	 * got moved. */
	double bleach = room_sun(q) * (0.55 + 0.45 * use);
	vec3 faded = vmix(m->albedo, wd->faded, 0.62);
	m->albedo = vmix(m->albedo, faded, bleach * 0.40);

	/* Scratches, lying both ways: things dragged across the boards and
	 * things dragged along them. A scratch is the finish opened up, so it
	 * is paler and duller than the surface either side of it. */
	double sc = scratch_at(u, v, 1.90, 13.0, 0.026, 1.0) * 0.85
		+ scratch_at(v, u, 3.40, 17.0, 0.034, 6.0) * 0.62
		+ scratch_at(u, v, 0.52, 8.0, 0.010, 13.0) * 0.48;
	sc = clampf(sc, 0.0, 1.0);
	sc = clampf(sc * (0.35 + touch * 1.35), 0.0, 1.0);

	/* Knocks: through the finish and into the timber, so they are darker
	 * than the surface, not lighter. */
	double nick = nick_at(u, v, 1.5, 3.0) * clampf(0.30 + touch, 0.0, 1.0);

	/* Varnish. It was even once. Where a sleeve has been going back and
	 * forth since, it is thin, and where the scratches are it is cut. */
	double worn = clampf(touch * 1.05 + sc * 0.90, 0.0, 1.0);
	m->coat = clampf(m->coat - 0.42 * worn, 0.04, 1.0);
	m->coat_rough = clampf(m->coat_rough + worn * 0.30 + sc * 0.14, 0.02, 1.0);
	m->rough = clampf(m->rough + worn * 0.18 + sc * 0.20, 0.05, 1.0);

	/* A cut through varnish goes white: the finish is crazed along it and
	 * the timber under it has never been polished. A knock goes the other
	 * way, because it is dirt driven into open grain. */
	m->albedo = vmix(m->albedo, vmul(vadd(m->albedo, v3(0.055, 0.044, 0.034)), 1.55),
		sc * 0.62);
	m->albedo = vmix(m->albedo, vmul(m->albedo, 0.40), nick * 0.85);
	m->rough = clampf(m->rough + nick * 0.30, 0.05, 1.0);
	m->coat = clampf(m->coat * (1.0 - nick * 0.80), 0.0, 1.0);

	/* the bullnose along the front, and every arris: rubbed back to bare */
	m->edge = 1.0 + use * 1.5;

	/* Grime. Not dust -- dust settles and this is rubbed in: the grey that
	 * collects wherever a cloth has never quite reached, which is the
	 * opposite of where the hand goes. */
	double grime = (1.0 - touch) * use * 0.55
		* clampf(room_fbm2(q.x * 0.34 + 11.0, q.z * 0.34 + 2.0) * 1.5 - 0.30, 0.0, 1.0);
	m->albedo = vmix(m->albedo, v3(0.062, 0.050, 0.042), grime * 0.26);
	m->rough = clampf(m->rough + grime * 0.16, 0.05, 1.0);
}

static void walnut_at(vec3 q, double u, double v, struct mtl *m, double plank, double use)
{
	timber_at(&WOOD_WALNUT, q, u, v, m, plank, use);
}
