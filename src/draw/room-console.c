/* Part of draw.c: the control console, as a shape in the room.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- the control console -------------------------------------- */

/* A valve set, a lab rack and an aircraft panel in one, in crinkle-black
 * steel with brass fittings and walnut cheeks. Everything on it is
 * something you push, pull, turn or throw.
 *
 * It is built on two sloped planes -- the operating panel you reach across
 * and the steeper tower behind it that carries the instruments -- so every
 * face is defined by its own frame rather than by guessing at coordinates
 * in the room.
 *
 * It lives in the far left corner, and it lives THERE rather than on the
 * desk: a set this size sitting out in the open owns the desk, and the desk
 * belongs to the paper in the middle of it. So it rides in and out of the
 * back edge on a slide. Stowed, the desk's own far edge is what hides it --
 * the same trick that lets a six foot bookcase stand behind the desk
 * without touching the plan view by a pixel -- and all that is left above
 * the line is the nameplate ledge, the pilot lamps and the front edge of
 * the panel. Pulled out, the whole machine is on the desk. */
/* It grew a column at its left end for the screen's own light (a laptop's
 * backlight, on a fader under the lamp's). The nameplate and its ledge are
 * at the very left end, under that column (RM_CC_PLATE_X0), so the rest
 * of the front is the tray's. */
/* (each where it is on the desk as designed, _0, less how far the corner
 * has moved out) */
#define RM_CC_X0_0 (-15.20)
#define RM_CC_X1_0 (-5.10)
#define RM_CC_PLATE_X0_0 (-15.05)
#define RM_CC_X0 (RM_CC_X0_0 - RM_HX_GROW)
#define RM_CC_X1 (RM_CC_X1_0 - RM_HX_GROW)
#define RM_CC_PLATE_X0 (RM_CC_PLATE_X0_0 - RM_HX_GROW)
#define RM_CC_CHEEK 0.72
#define RM_CC_PLINTH 0.50
/* the plinth runs out in front of the panel as a ledge, because a plate you
 * have to read needs a face that is square to the camera and the panel is
 * not: seen from above, a legend on that slope is thirteen pixels deep.
 * It runs out only as far across as the plate on it needs; past that the
 * plinth stops under the panel's lip, and what lies on the desk there --
 * the tray's slips -- is tucked in under it. */
#define RM_CC_LEDGE 0.95
/* and the plate is cut as short as its legend can be engraved -- narrow
 * letters, the screws close in -- because every bit of plinth past it is
 * room for another slip */
#define RM_CC_LEDGE_X1_0 (-11.50)
#define RM_CC_LEDGE_X1 (RM_CC_LEDGE_X1_0 - RM_HX_GROW)
/* where the plinth's front is, past the ledge: a hair behind the lip, so
 * the case throws a line of shadow under itself */
#define RM_CC_PLINTH_Z0 (RM_CC_PZ0 - 0.05)

/* the operating panel: front edge to back edge */
#define RM_CC_PY0 0.95
#define RM_CC_PZ0 (-5.70)
#define RM_CC_PY1 3.30
#define RM_CC_PZ1 (-8.20)
/* the tower, steeper, carrying the instruments -- and since 2026-11-10 the knob and the faders
 * too, the top half of a panel in four (the meters at its left, what is turned and slid at its
 * right), so it is longer than it was (2.33 up its slope; now 2.90) at the same slope */
#define RM_CC_TY0 3.30
#define RM_CC_TZ0 (-8.20)
#define RM_CC_TY1 5.918
#define RM_CC_TZ1 (-9.447)
#define RM_CC_ZB (-10.60)   /* the back of the case */
/* The front of the carcass, the body under both faces: behind the tower's back where the
 * carcass's top meets it (0.72 of the way up), since the tower leans back off the fold and a
 * body as far forward as the fold stood out through the tower's face as a ledge. Not cut
 * along the tower instead: SVGA3D runs a dummy for the room's shader that one more max()
 * makes. */
#define RM_CC_BODY_Z0 (-8.95)
/* Both faces are boxes this thick, so their skin -- the plane every
 * fitting has to stand on -- is here and not at the frame's origin. */
#define RM_CC_SKIN 0.17

/* Where the slide has got to: 0 stowed, 1 all the way out. Everything in
 * here is authored in the position it sits in when it is out, and this is
 * the one shift that puts it back. */
static double g_cc_out = 1.0;

/* How far back it has to go for only its front to clear the desk's far
 * edge. Derived rather than authored, because that edge moves with the
 * shape of the screen: on a four by three desk it is four units deeper
 * than on a sixteen by ten one, and a fixed travel would leave the machine
 * sitting out in the open on one and buried on the other.
 *
 * The point that is meant to land exactly on the line is a fifth of the way
 * up the panel -- just above the lamps.
 *
 * A pure function of the aspect, and not of the aspect GLOBAL. That global
 * is whatever the last camera anybody built left behind, and the compositor
 * bakes a second room at the backend's own size before its window has
 * settled -- so at drawing time it was reading a four by three travel and
 * projecting the nameplate four hundred pixels off the top of the screen.
 * Anything drawn over a baked sheet has to use the sheet's own shape. */
static double cc_stow_for(double aspect, double hx)
{
	const double k = 0.15;
	double a = fmax(aspect, 0.2);
	double y = RM_CC_PY0 + (RM_CC_PY1 - RM_CC_PY0) * k;
	double z = RM_CC_PZ0 + (RM_CC_PZ1 - RM_CC_PZ0) * k;
	double zf = RM_ZN - 2.0 * hx / a;
	return z - (zf + (RM_FOV_X_V * 0.5 / a) * y);
}

/* (for the desk the room is of) */
static double cc_stow_at(double aspect)
{
	return cc_stow_for(aspect, RM_HX);
}

/* How far forward it comes when it is out. The desk is shallower on a
 * wider screen -- at two to one its far edge is five units nearer than at
 * sixteen by ten -- and the machine, pulled out to where it was authored,
 * had its whole instrument tower through the far edge, where the edge cuts
 * everything away, and the tube and the meters drawn over the wall behind
 * it. So it comes out as far as its back needs to clear the edge, and no
 * further: on a desk deep enough, not at all. */
static double cc_forward_at(double aspect)
{
	double a = fmax(aspect, 0.2);
	double zf = RM_ZN - 2.0 * RM_HX / a;
	return fmax(0.0, zf + 0.35 - RM_CC_ZB);
}

/* The z offset the whole machine carries at a given point in the slide:
 * stowed, far enough back for only its front to show; out, clear of the
 * far edge. */
static double cc_dz(double out, double aspect)
{
	double o = clampf(out, 0.0, 1.0);
	return o * cc_forward_at(aspect) - (1.0 - o) * cc_stow_at(aspect);
}

struct cc_face {
	vec3 o;        /* centre of the face */
	vec3 ax;       /* across it */
	vec3 as;       /* up it, towards the back */
	vec3 an;       /* out of it */
	double half;   /* half its length along as */
};

static void cc_face_build(double y0, double z0, double y1, double z1, struct cc_face *f)
{
	double dy = y1 - y0, dz = z1 - z0;
	double len = sqrt(dy * dy + dz * dz);
	f->o = v3((RM_CC_X0 + RM_CC_X1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5);
	f->ax = v3(1, 0, 0);
	f->as = v3(0, dy / len, dz / len);
	f->an = v3(0, -f->as.z, f->as.y);
	f->half = len * 0.5;
}

static vec3 cc_to_face(const struct cc_face *f, vec3 p)
{
	vec3 q = vsub(p, f->o);
	return v3(vdot(q, f->ax), vdot(q, f->an), vdot(q, f->as));
}

static vec3 cc_to_world(const struct cc_face *f, double u, double n, double s)
{
	return vadd(f->o, vadd(vmul(f->ax, u), vadd(vmul(f->an, n), vmul(f->as, s))));
}

/* The same point in the room's coordinates rather than the machine's, for
 * whatever position the slide is in. */
static vec3 cc_world(const struct cc_face *f, double u, double n, double s,
	double out, double aspect)
{
	return vadd(cc_to_world(f, u, n, s), v3(0.0, 0.0, cc_dz(out, aspect)));
}

enum {
	CC_WIFI = 0, CC_BT, CC_VOL, CC_BRIGHT, CC_CRT, CC_BATT, CC_NET,
	CC_PW_OFF, CC_PW_BOOT, CC_PW_OUT, CC_PW_KEY,
	CC_SCREEN,
	CC_LOCK,
	/* (2026-11-10) SLEEP beside LOCK, the screens put to sleep; SUSPEND under the lid with the
	 * three, the machine put to sleep; the four on the lid; and the lid itself, last of all,
	 * so that a point on one of the four is that one's rather than the lid's */
	CC_SLEEP, CC_PW_SUSPEND,
	CC_AIR, CC_SAVE, CC_NIGHT, CC_DISPLAY,
	CC_LID,
	CC_COUNT,
};

/* What a control is on: the face; under the lid, which is all you can reach of it when the
 * lid is up; or the lid, and what is on it, which is the whole of it when the lid is down. */
enum { CC_ON_FACE = 0, CC_ON_UNDER, CC_ON_LID };

/* Where each control sits on its face: which face, then across and up it. */
struct cc_control {
	int tower;       /* 0 operating panel, 1 instrument tower */
	double u, s;     /* across the face, up the face */
	double hu, hs;   /* how much of it you can reach for */
	double fu, fs;   /* the printed face, which fits inside its bezel */
	int round;       /* the face is clipped to a circle */
	int on;          /* CC_ON_* */
};

/* The panel in four (owner 2026-11-10: 左上、右上、左下、右下): up on the tower, the
 * meters at the left -- the tube, and the battery's and the line's one over the other -- and
 * at the right what is turned and slid -- the volume, and the lamp's and the screen's faders
 * one over the other; down on the panel, at the left the two switches with LOCK and SLEEP
 * under them, and at the right the walnut lid, with AIRPLANE, POWER SAVE, NIGHT and DISPLAY on
 * it, SHUT DOWN, RESTART, LOG OUT and SUSPEND under it, and the key beside it.
 *
 * The desk's far edge cuts the machine off when it is stowed, and a cut through the middle of a
 * fitting leaves half of it standing on the fascia -- half the volume knob's brass skirt was
 * sitting up there like a rising sun. So the row of LOCK and SLEEP is down at the lip, all of it
 * in front of that cut on a screen of any shape (it is what the desk sees of the machine, with
 * the pilot lenses), and everything else -- the lid's free edge, and the caps on it, which stand
 * furthest out -- is behind it, from five by four to the widest (the cut comes up the panel as
 * the screen gets squarer). */
static const struct cc_control CC[CC_COUNT] = {
	{ 0, -3.05,  0.74, 0.66, 0.60, 0.74, 1.00, 0, CC_ON_FACE },   /* wifi lever */
	{ 0, -1.15,  0.74, 0.62, 0.56, 0.70, 0.96, 0, CC_ON_FACE },   /* bluetooth toggle */
	{ 1,  1.40,  0.00, 0.90, 0.90, 0.95, 1.18, 0, CC_ON_FACE },   /* volume knob */
	{ 1,  3.32,  0.66, 0.92, 0.36, 0.94, 0.56, 0, CC_ON_FACE },   /* the lamp's fader */
	{ 1, -2.95,  0.00, 1.25, 1.25, 1.06, 1.06, 1, CC_ON_FACE },   /* the tube */
	{ 1, -0.66,  0.70, 0.82, 0.60, 0.64, 0.42, 0, CC_ON_FACE },   /* battery meter */
	{ 1, -0.66, -0.70, 0.82, 0.60, 0.64, 0.42, 0, CC_ON_FACE },   /* network meter */
	{ 0,  1.155, 1.07, 0.50, 0.50, 0.52, 0.52, 0, CC_ON_UNDER },  /* SHUT DOWN */
	{ 0,  2.625, 1.07, 0.50, 0.50, 0.52, 0.52, 0, CC_ON_UNDER },  /* RESTART */
	{ 0,  1.155, -0.80, 0.50, 0.50, 0.52, 0.52, 0, CC_ON_UNDER },  /* LOG OUT */
	{ 0,  3.86,  0.80, 0.44, 0.78, 0.44, 0.78, 0, CC_ON_FACE },   /* the lock, and the key under it */
	/* under the lamp's fader, the screen's: the backlight of a laptop's panel, or of nothing,
	 * which it says */
	{ 1,  3.32, -0.66, 0.92, 0.36, 0.94, 0.56, 0, CC_ON_FACE },
	/* and down at the left, out in the open because it ends nothing, the one that locks the
	 * screen: a button like the others, blue */
	{ 0, -3.05, -1.00, 0.46, 0.40, 0.50, 0.42, 0, CC_ON_FACE },
	{ 0, -1.15, -1.00, 0.46, 0.40, 0.50, 0.42, 0, CC_ON_FACE },   /* SLEEP */
	{ 0,  2.625, -0.80, 0.50, 0.50, 0.52, 0.52, 0, CC_ON_UNDER },  /* SUSPEND */
	{ 0,  1.155, 1.07, 0.50, 0.50, 0.52, 0.52, 0, CC_ON_LID },    /* AIRPLANE */
	{ 0,  2.625, 1.07, 0.50, 0.50, 0.52, 0.52, 0, CC_ON_LID },    /* POWER SAVE */
	{ 0,  1.155, -0.80, 0.50, 0.50, 0.52, 0.52, 0, CC_ON_LID },    /* NIGHT */
	{ 0,  2.625, -0.80, 0.50, 0.50, 0.52, 0.52, 0, CC_ON_LID },    /* DISPLAY */
	{ 0,  1.89,  0.135, 1.47, 1.435, 1.47, 1.435, 0, CC_ON_LID }, /* the lid */
};

/* ---- the guarded set at the bottom right of the operating panel ----
 *
 * The four that end the session or put the machine to sleep, the lock that lets you at them,
 * and the walnut lid over them -- which carries the four that are pressed every day, so they
 * are under the hand with it down. Laid out here because the tracer, the picker and the cairo
 * half all have to agree about where a button is to within nothing.
 *
 * LOCK and SLEEP are the size the three under the glass were. The lid runs the height of the
 * quarter, from just behind the pilot lenses to the hinge, so the eight in and on it are bigger
 * than those two, two by two (2026-11-10, the owner: the buttons on the lid were small). The key
 * lies upright under its lock, so the lock's column is only as wide as the lock, and the lid
 * has the rest (2026-11-11, the owner: the key upright, and the room it gave to the buttons). From
 * the desk, with the machine stowed, the lid's front and the two at the front of it show, as
 * LOCK and SLEEP do, and nothing under it. */
#define CC_PW_R      0.285   /* the cap */
#define CC_PW_RING   0.395   /* the brass ring round it, which carries the word */
#define CC_PW_DOME   0.50    /* the sphere the cap's dome is cut from */
#define CC_LB_R      0.325   /* the eight in and on the lid: their caps */
#define CC_LB_RING   0.45    /* and rings */
#define CC_LB_DOME   0.637   /* (a dome as high as the small one's, on a wider cap) */
#define CC_PW_LOCK_U 3.86    /* the barrel, in the column between the lid and the cheek */
#define CC_PW_KEY_S  1.16    /* and up the panel: the top row's */
#define CC_PW_KEY_REST 0.80  /* where the key's tip lies when it is not in the lock: under the
                              * barrel, upright, its bow towards the lip */
#define CC_PW_LAMP_S (-0.16) /* the set's own lamp, under the key: lit with the key home */
/* the lid: across, up (its free edge, and its back edge before the hinge), and how far its
 * underside and its top stand off the panel, shut; where its hinge is; how far it swings */
#define CC_LID_U0    0.42
#define CC_LID_U1    3.36
#define CC_LID_S0  (-1.30)   /* (short of the pilot lenses, which from the desk it hid) */
#define CC_LID_S1    1.57
#define CC_LID_N0    0.40
#define CC_LID_N1    0.54
#define CC_LID_HINGE_S 1.64
#define CC_LID_SWING 1.92    /* 110 degrees: past upright, leaning back towards the tower */
/* the brass strip that parts the left half of the panel from the right, on both faces */
#define CC_PART_U    0.30

/* What each button's cap is cast in, by control (0: it is not a button). */
static int cc_btn_mid(int i)
{
	switch (i) {
	case CC_LOCK: case CC_PW_SUSPEND: case CC_NIGHT:
		return MID_CC_LENS_BLUE;
	case CC_SLEEP: case CC_DISPLAY:
		return MID_CC_LENS_WHITE;
	case CC_PW_OFF:
		return MID_CC_LENS_RED;
	case CC_PW_BOOT: case CC_AIR:
		return MID_CC_LENS_AMBER;
	case CC_PW_OUT: case CC_SAVE:
		return MID_CC_LENS_GREEN;
	default:
		return 0;
	}
}

/* The one on the lid where one under it is, for one under it; any other, itself. */
static int cc_twin(int i)
{
	if (CC[i].on != CC_ON_UNDER) {
		return i;
	}
	for (int k = 0; k < CC_COUNT; k++) {
		if (CC[k].on == CC_ON_LID && cc_btn_mid(k) && CC[k].u == CC[i].u && CC[k].s == CC[i].s) {
			return k;
		}
	}
	return i;
}

/* (a point in a control's box is that control's from the face up to here: the eight in and on
 * the lid are told apart by how far off the panel the point is) */
static double cc_owner_n0(int i)
{
	return CC[i].on == CC_ON_LID ? CC_LID_N0 - 0.02 : -0.15;
}

static double cc_owner_n1(int i)
{
	return CC[i].on == CC_ON_UNDER ? CC_LID_N0 - 0.02 : 1.15;
}

/* How the lid is traced over the set: 1 shut, in every sheet but one; 2 up on its hinge, all
 * the way (CC_LID_SWING), in the console's own view's second sheet (rr->console_bare), which is
 * what is under it and the lid's underside and its shadow; 0 not at all. As it swings the lid
 * is laid over from those two (cc_lid_*). */
static double g_cc_lid = 1.0;

static void cc_faces(struct cc_face *panel, struct cc_face *tower)
{
	cc_face_build(RM_CC_PY0, RM_CC_PZ0, RM_CC_PY1, RM_CC_PZ1, panel);
	cc_face_build(RM_CC_TY0, RM_CC_TZ0, RM_CC_TY1, RM_CC_TZ1, tower);
}

/* The four corners of the lid's top, at a given point in its swing. It is hinged along its
 * back edge, so opening it lifts the free edge off the panel and up towards the tower. In the
 * order cc_quad has them: its back left, back right, front left, front right. */
static void cc_lid_corners(double open, double n, double slide, double aspect, vec3 out[4])
{
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	double th = clampf(open, 0.0, 1.0) * CC_LID_SWING;
	double hn = (CC_LID_N0 + CC_LID_N1) * 0.5;
	/* each corner turned about the hinge: (s, n) from it, round by th, the free edge (down the
	 * panel from the hinge) coming up off it. (It went round the other way, into the panel, which
	 * from the front looks much the same until the lid is half way.) */
	const double U[4] = { CC_LID_U0, CC_LID_U1, CC_LID_U0, CC_LID_U1 };
	const double S[4] = { CC_LID_S1, CC_LID_S1, CC_LID_S0, CC_LID_S0 };
	for (int k = 0; k < 4; k++) {
		double ds = S[k] - CC_LID_HINGE_S, dn = n - hn;
		double rs = ds * cos(th) + dn * sin(th), rn = dn * cos(th) - ds * sin(th);
		out[k] = cc_world(&panel, U[k], hn + rn, CC_LID_HINGE_S + rs, slide, aspect);
	}
}

/* The four corners of a control's face, for cairo to print into. The four on the lid are
 * printed as high over the panel as the lid lifts them: laid at the panel's own height, their
 * words and icons were half a unit under the caps, and the view's perspective slid them off
 * the caps, towards the middle of the view (2026-11-11, the owner: 描画ズレ). */
static void cc_quad(int i, double slide, double aspect, vec3 out[4])
{
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	const struct cc_face *f = CC[i].tower ? &tower : &panel;
	double n = CC[i].tower ? 0.05 : 0.03;
	if (CC[i].on == CC_ON_LID && i != CC_LID) {
		n += CC_LID_N1 - RM_CC_SKIN;
	}
	for (int k = 0; k < 4; k++) {
		double du = (k & 1) ? CC[i].fu : -CC[i].fu;
		double ds = (k & 2) ? -CC[i].fs : CC[i].fs;
		out[k] = cc_world(f, CC[i].u + du, n, CC[i].s + ds, slide, aspect);
	}
}

/* The ledge across the front of the plinth: a horizontal face, which is the
 * only kind you can read a legend off in a plan view. Corner order matches
 * cc_quad's, so the same affine fit works on it. */
static void cc_plate_quad(double slide, double aspect, vec3 out[4])
{
	double cx = (RM_CC_PLATE_X0 + RM_CC_LEDGE_X1) * 0.5;
	double hx = (RM_CC_LEDGE_X1 - RM_CC_PLATE_X0) * 0.5 - 0.10;
	double z0 = RM_CC_PZ0 + RM_CC_LEDGE - 0.10;   /* front */
	double z1 = RM_CC_PZ0 - 0.06;                 /* under the panel's lip */
	double y = RM_CC_PLINTH + 0.012;
	double dz = cc_dz(slide, aspect);
	for (int k = 0; k < 4; k++) {
		out[k] = v3(cx + ((k & 1) ? hx : -hx), y,
			((k & 2) ? z0 : z1) + dz);
	}
}

/* How big to print a control's face before laying it onto the quad. It is
 * the quad's own shape at a fixed number of pixels to the world unit, so
 * nothing printed on a face is stretched, and anything drawn at a world
 * measurement lands on the hardware under it. */
#define CC_FACE_PX 142.0

/* At Low, which traces the set 960 wide rather than 1440, two thirds of
 * that (at Medium, 1280 wide, five sixths): a finer face there sharpens the print on a softer panel, at a cost
 * that is by the pixel -- and the tube's face is printed six times a
 * second (ten milliseconds a time on a machine of 2010, at the full size).
 * Everything that reads a face back goes by its size, so nothing else
 * changes with it. */
static double cc_face_px(void)
{
	enum desk_gfx g = desk_gfx_level();
	return g <= DESK_GFX_LOW ? CC_FACE_PX * 2.0 / 3.0
		: g == DESK_GFX_MEDIUM ? CC_FACE_PX * 5.0 / 6.0 : CC_FACE_PX;
}

static void cc_face_size(int i, double *fw, double *fh)
{
	*fw = ceil(2.0 * CC[i].fu * cc_face_px());
	*fh = ceil(2.0 * CC[i].fs * cc_face_px());
}

/* A button, about q (in its face's coordinates, from its middle): a brass ring standing off the
 * face at base, the skirt the cap sits in, and the cap -- a shallow dome, a big sphere (dome)
 * with all but its crown cut away rather than a small one sitting on the panel like an egg --
 * in cast glass, mat. Its crown stands 0.20 off base whatever its width: a wider cap is cut
 * from a bigger sphere. The ring is wide, because the word for what the button does is cut
 * round the bottom of it. */
static hit3 cc_button(vec3 q, double ring, double cap, double dome, double base, int mat)
{
	hit3 h = { sd_cyl_y(v3(q.x, q.y - (base + 0.045), q.z), ring, 0.045), MID_CC_BRASS };
	h = hmin3(h, (hit3){ sd_cyl_y(v3(q.x, q.y - (base + 0.075), q.z), cap + 0.045, 0.075),
		MID_CC_BRASS });
	double d = fmax(sd_sphere(v3(q.x, q.y - (base + 0.20 - dome), q.z), dome),
		sd_cyl_y(v3(q.x, q.y - (base + 0.05), q.z), cap, 0.16));
	return hmin3(h, (hit3){ d, mat });
}

static hit3 map_console(vec3 p)
{
	hit3 h = { 1e9, 0 };
	/* Everything below is in the machine's own coordinates -- where it sits
	 * when it is all the way out. This one line is the whole slide. */
	p = vsub(p, v3(0.0, 0.0, cc_dz(g_cc_out, g_rm_aspect)));
	const double cx = (RM_CC_X0 + RM_CC_X1) * 0.5;
	const double chx = (RM_CC_X1 - RM_CC_X0) * 0.5;
	const double inner = chx - RM_CC_CHEEK;

	double gross = sd_box(vsub(p, v3(cx, RM_CC_TY1 * 0.5, (RM_CC_PZ0 + RM_CC_ZB) * 0.5)),
		v3(chx + 0.35, RM_CC_TY1 * 0.5 + 0.55, (RM_CC_PZ0 - RM_CC_ZB) * 0.5 + 0.35));
	if (gross > DRAWER_NEAR) {
		return (hit3){ gross, 0 };
	}

	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	vec3 pf = cc_to_face(&panel, p);
	vec3 tf = cc_to_face(&tower, p);

	/* The plinth, set back at the sides and at the front so the case
	 * throws a shadow line under itself, and run out at the front as the
	 * nameplate ledge as far across as the plate goes. */
	double plz0 = RM_CC_PLINTH_Z0, plz1 = RM_CC_ZB + 0.30;
	h = hmin3(h, (hit3){ sd_round_box(vsub(p,
		v3(cx, RM_CC_PLINTH * 0.5, (plz0 + plz1) * 0.5)),
		v3(chx - 0.10, RM_CC_PLINTH * 0.5, (plz0 - plz1) * 0.5), 0.06),
		MID_CC_STEEL });
	{
		double lx0 = RM_CC_PLATE_X0 + 0.10, lx1 = RM_CC_LEDGE_X1;
		double lz0 = RM_CC_PZ0 + RM_CC_LEDGE, lz1 = RM_CC_PLINTH_Z0 - 0.20;
		h = hmin3(h, (hit3){ sd_round_box(vsub(p,
			v3((lx0 + lx1) * 0.5, RM_CC_PLINTH * 0.5, (lz0 + lz1) * 0.5)),
			v3((lx1 - lx0) * 0.5, RM_CC_PLINTH * 0.5, (lz0 - lz1) * 0.5), 0.06),
			MID_CC_STEEL });
	}

	/* the carcass: a box under both faces, so the thing has a body (RM_CC_BODY_Z0) */
	h = hmin3(h, (hit3){ sd_round_box(vsub(p,
		v3(cx, (RM_CC_PLINTH + RM_CC_TY1 * 0.72) * 0.5, (RM_CC_BODY_Z0 + RM_CC_ZB) * 0.5)),
		v3(inner, (RM_CC_TY1 * 0.72 - RM_CC_PLINTH) * 0.5, (RM_CC_BODY_Z0 - RM_CC_ZB) * 0.5),
		0.10), MID_CC_STEEL });

	/* the two faces */
	h = hmin3(h, (hit3){ sd_round_box(pf, v3(inner, 0.17, panel.half), 0.06),
		MID_CC_PANEL });
	h = hmin3(h, (hit3){ sd_round_box(tf, v3(inner, 0.17, tower.half), 0.06),
		MID_CC_PANEL });

	/* walnut cheeks at each end, carried up the whole height */
	for (int sgn = 0; sgn < 2; sgn++) {
		double sx = sgn ? RM_CC_X1 - RM_CC_CHEEK * 0.5 : RM_CC_X0 + RM_CC_CHEEK * 0.5;
		vec3 q = vsub(p, v3(sx, 0, 0));
		/* the cheek follows the profile: tall at the back, low at the front */
		double prof = fmax(sd_round_box(vsub(q, v3(0,
			(RM_CC_PLINTH + RM_CC_TY1) * 0.5, (RM_CC_PZ1 + RM_CC_ZB) * 0.5)),
			v3(RM_CC_CHEEK * 0.5, (RM_CC_TY1 - RM_CC_PLINTH) * 0.5,
				(RM_CC_PZ1 - RM_CC_ZB) * 0.5), 0.10),
			-1e9);
		double front = sd_round_box(vsub(q, v3(0,
			(RM_CC_PLINTH + RM_CC_PY1) * 0.5, (RM_CC_PZ0 + RM_CC_PZ1) * 0.5)),
			v3(RM_CC_CHEEK * 0.5, (RM_CC_PY1 - RM_CC_PLINTH) * 0.5,
				(RM_CC_PZ1 - RM_CC_PZ0) * 0.5 * -1.0 + 0.0), 0.10);
		h = hmin3(h, (hit3){ fmin(prof, front), MID_CC_CHEEK });
	}

	/* ---- the instruments on the tower ---- */

	/* the tube, in a turned brass bezel. In face coordinates y is the face
	 * normal, which is already the axis sd_torus and sd_cyl_y turn about. */
	{
		vec3 q = vsub(tf, v3(CC[CC_CRT].u, 0, CC[CC_CRT].s));
		h = hmin3(h, (hit3){ sd_torus(v3(q.x, q.y - (RM_CC_SKIN + 0.02), q.z),
			1.18, 0.17), MID_CC_BRASS });
		h = hmin3(h, (hit3){ sd_cyl_y(v3(q.x, q.y - (RM_CC_SKIN + 0.02), q.z),
			1.14, 0.09), MID_CC_GLASS });
	}
	/* two meters under glass in brass frames */
	for (int m = 0; m < 2; m++) {
		int idx = m ? CC_NET : CC_BATT;
		vec3 q = vsub(tf, v3(CC[idx].u, 0, CC[idx].s));
		h = hmin3(h, (hit3){ sd_round_box(v3(q.x, q.y - (RM_CC_SKIN + 0.06), q.z),
			v3(CC[idx].hu * 0.92, 0.10, CC[idx].hs * 0.88), 0.05), MID_CC_BRASS });
		h = hmin3(h, (hit3){ sd_round_box(v3(q.x, q.y - (RM_CC_SKIN + 0.05), q.z),
			v3(CC[idx].hu * 0.78, 0.07, CC[idx].hs * 0.74), 0.03), MID_CC_GLASS });
	}

	/* the brass that parts the panel in four: a strip up each face between its left half and
	 * its right, and a rod along the fold between the panel and the tower. The panel's strip
	 * stops short of the row along the lip, behind the cut the desk's far edge makes. */
	h = hmin3(h, (hit3){ sd_round_box(v3(tf.x - CC_PART_U, tf.y - (RM_CC_SKIN + 0.015), tf.z),
		v3(0.035, 0.015, tower.half - 0.10), 0.015), MID_CC_BRASS });
	h = hmin3(h, (hit3){ sd_round_box(v3(pf.x - CC_PART_U, pf.y - (RM_CC_SKIN + 0.015),
		pf.z - (panel.half - 0.06 - 0.45) * 0.5),
		v3(0.035, 0.015, (panel.half - 0.06 + 0.45) * 0.5), 0.015), MID_CC_BRASS });
	h = hmin3(h, (hit3){ sd_round_box(v3(pf.x, pf.y - (RM_CC_SKIN + 0.03),
		pf.z - (panel.half - 0.04)), v3(inner - 0.06, 0.03, 0.03), 0.03), MID_CC_BRASS });

	/* the knob, up on the tower: fluted bakelite on a brass skirt, with a pointer */
	{
		vec3 q = vsub(tf, v3(CC[CC_VOL].u, 0, CC[CC_VOL].s));
		h = hmin3(h, (hit3){ sd_cyl_y(v3(q.x, q.y - (RM_CC_SKIN + 0.035), q.z),
			0.88, 0.035), MID_CC_BRASS });
		/* A fine knurl, not a deep one: a radial ripple this size breaks
		 * the distance bound the marcher relies on, and the knob came back
		 * looking chewed round the rim.
		 *
		 * And the bakelite stops well inside the brass, because the brass
		 * is a scale and a scale needs somewhere to be. */
		double ang = atan2(q.z, q.x);
		double flute = 0.500 + 0.010 * pow(fmax(0.0, cos(ang * 18.0)), 2.0);
		/* A radius that varies with the angle is not a distance field: the
		 * true distance to a rippled rim is shorter than a plain cylinder
		 * reports, by about sqrt(1 + (A k / R)^2), so the marcher steps
		 * past it and the rim comes back chewed. Hand it a lower bound and
		 * it stops doing that. Shrinking the knob made this worse, because
		 * the same ripple on a smaller radius is a steeper one. */
		/* (and low: standing 0.47 off its skirt, the view's slant moved its top half a
		 * knob off its foot -- 2026-11-11, the owner: 回す物の高さを下げましょう) */
		h = hmin3(h, (hit3){ 0.85 * sd_cyl_y(v3(q.x, q.y - (RM_CC_SKIN + 0.19), q.z),
			flute, 0.12), MID_CC_BAKELITE });
		/* the pointer is drawn on, because it turns */
	}

	/* the two faders, the lamp's and the screen's, up on the tower beside it: a milled handle
	 * in a long slot */
	for (int fd = 0; fd < 2; fd++) {
		int idx = fd ? CC_SCREEN : CC_BRIGHT;
		vec3 q = vsub(tf, v3(CC[idx].u, 0, CC[idx].s));
		double plate = sd_round_box(v3(q.x, q.y - (RM_CC_SKIN + 0.04), q.z),
			v3(CC[idx].hu * 0.95, 0.04, 0.30), 0.04);
		plate = fmax(plate, -sd_box(v3(q.x, q.y - (RM_CC_SKIN + 0.14), q.z),
			v3(CC[idx].hu * 0.82, 0.14, 0.11)));
		h = hmin3(h, (hit3){ plate, MID_CC_BRASS });
		/* the handle slides, so it is drawn rather than baked */
	}

	/* ---- what you reach for on the panel ---- */

	/* two switches, each on its brass plate with its pilot lamp above */
	for (int sw = 0; sw < 2; sw++) {
		vec3 q = vsub(pf, v3(CC[sw].u, 0, CC[sw].s));
		/* a sub-panel, not a brass plate: a hand-sized sheet of brass up
		 * against the desk lamp turned into a mirror and blew out */
		h = hmin3(h, (hit3){ sd_round_box(v3(q.x, q.y - (RM_CC_SKIN + 0.03), q.z),
			v3(CC[sw].hu * 0.82, 0.03, CC[sw].hs * 0.82), 0.03), MID_CC_PANEL });
		/* The handle itself is not baked: it has to fall when you throw
		 * it, and a baked one cannot. The hinge boss is here; the lever is
		 * drawn over it. */
		h = hmin3(h, (hit3){ sd_cyl_y(v3(q.x, q.y - (RM_CC_SKIN + 0.11), q.z),
			0.14, 0.06), MID_CC_STEEL });
		vec3 lamp = v3(q.x, q.y - (RM_CC_SKIN + 0.05), q.z - CC[sw].hs * 0.86);
		h = hmin3(h, (hit3){ sd_cyl_y(lamp, 0.21, 0.05), MID_CC_BRASS });
		h = hmin3(h, (hit3){ sd_sphere(v3(lamp.x, lamp.y - 0.09, lamp.z), 0.155),
			sw ? MID_CC_LENS_BLUE : MID_CC_LENS_AMBER });
	}

	/* The buttons -- LOCK and SLEEP at the lip, the four under the lid, the four on it where it
	 * is traced at all -- as one: the one nearest across the panel, exactly, and for the rest
	 * no nearer than the next nearest's ring. Each lies inside its own ring, which is less than
	 * half the way to the next, so wherever a button is the one nearest is that one; and the
	 * shader carries one button rather than ten (SVGA3D silently runs nothing for a program as
	 * long as ten made it). */
	{
		int best = -1;
		double d1 = 1e9, d2 = 1e9, r2 = CC_PW_RING;
		for (int i = 0; i < CC_COUNT; i++) {
			/* The four on the lid are where the four under it are: those four stand for both,
			 * and the nearest of them is its twin on the lid while the lid is traced. Asking
			 * which of the eight the lid leaves to be seen, button by button, made the
			 * shader one SVGA3D runs a dummy for. */
			if (!cc_btn_mid(i) || CC[i].on == CC_ON_LID) {
				continue;
			}
			double d = hypot(pf.x - CC[i].u, pf.z - CC[i].s);
			double ring = CC[i].on == CC_ON_FACE ? CC_PW_RING : CC_LB_RING;
			if (d < d1) {
				d2 = d1;
				r2 = best >= 0 ? (CC[best].on == CC_ON_FACE ? CC_PW_RING : CC_LB_RING) : r2;
				d1 = d;
				best = i;
			} else if (d < d2) {
				d2 = d;
				r2 = ring;
			}
		}
		if (best >= 0) {
			/* (shut: the four on it; lifted, the lid is up off them, and the four under it
			 * are what is there) */
			int b = CC[best].on == CC_ON_UNDER && fabs(g_cc_lid - 1.0) < 0.5 ? cc_twin(best)
				: best;
			bool big = CC[b].on == CC_ON_FACE;
			h = hmin3(h, cc_button(vsub(pf, v3(CC[b].u, 0, CC[b].s)),
				big ? CC_PW_RING : CC_LB_RING, big ? CC_PW_R : CC_LB_R,
				big ? CC_PW_DOME : CC_LB_DOME,
				CC[b].on == CC_ON_LID ? CC_LID_N1 : RM_CC_SKIN, cc_btn_mid(b)));
			h = hmin3(h, (hit3){ d2 - r2, 0 });
		}
	}

	/* ---- the guarded set, at the bottom right ----
	 *
	 * A plate of its own, four buttons that end the session or put the machine to sleep, a
	 * lock, and a walnut lid over the four on a brass hinge -- with four of its own on top,
	 * that anybody may press. The key is not baked: it moves. Nor is the lid, in the
	 * console's own sheet, where it swings. */
	{
		/* the sub-plate the set is mounted on: a little wider than the lid, and short of the
		 * pilot lenses in front */
		h = hmin3(h, (hit3){ sd_round_box(
			v3(pf.x - (CC_LID_U0 + CC_LID_U1) * 0.5, pf.y - (RM_CC_SKIN + 0.035),
				pf.z - (CC_LID_S0 + CC_LID_HINGE_S + 0.04) * 0.5),
			v3((CC_LID_U1 - CC_LID_U0) * 0.5 + 0.04, 0.035,
				(CC_LID_HINGE_S - CC_LID_S0 + 0.08) * 0.5), 0.05), MID_CC_PANEL });

		/* the lock: a brass barrel with a slot across it */
		{
			vec3 q = vsub(pf, v3(CC_PW_LOCK_U, 0, CC_PW_KEY_S));
			double barrel = sd_cyl_y(v3(q.x, q.y - (RM_CC_SKIN + 0.055), q.z),
				0.235, 0.055);
			barrel = fmax(barrel, -sd_box(
				v3(q.x, q.y - (RM_CC_SKIN + 0.105), q.z),
				v3(0.032, 0.055, 0.145)));
			h = hmin3(h, (hit3){ barrel, MID_CC_BRASS });
		}
		/* and under it the set's own lamp, green, lit while the key is home */
		{
			vec3 lamp = vsub(pf, v3(CC_PW_LOCK_U, RM_CC_SKIN + 0.05, CC_PW_LAMP_S));
			h = hmin3(h, (hit3){ sd_cyl_y(lamp, 0.21, 0.05), MID_CC_BRASS });
			h = hmin3(h, (hit3){ sd_sphere(v3(lamp.x, lamp.y - 0.09, lamp.z), 0.155),
				MID_CC_LENS_GREEN });
		}

		/* the hinge: a brass knuckle the width of the lid, on two brackets */
		double hn = (CC_LID_N0 + CC_LID_N1) * 0.5;
		h = hmin3(h, (hit3){ sd_round_box(v3(pf.x - (CC_LID_U0 + CC_LID_U1) * 0.5, pf.y - hn,
			pf.z - CC_LID_HINGE_S), v3((CC_LID_U1 - CC_LID_U0) * 0.5 - 0.04, 0.055, 0.055),
			0.055), MID_CC_BRASS });
		for (int sgn = 0; sgn < 2; sgn++) {
			double u = sgn ? CC_LID_U1 - 0.14 : CC_LID_U0 + 0.14;
			h = hmin3(h, (hit3){ sd_round_box(v3(pf.x - u, pf.y - (RM_CC_SKIN + hn) * 0.5,
				pf.z - CC_LID_HINGE_S), v3(0.065, (hn - RM_CC_SKIN) * 0.5, 0.05), 0.03),
				MID_CC_BRASS });
		}

		/* the lid, where it is traced at all: a walnut board, and a brass lip along its free
		 * edge to lift it by, between the two in front (the four on it are with the other
		 * buttons, above) -- shut, or up on its hinge, as the console's other sheet has it
		 * (g_cc_lid 2): the point turned back about the hinge onto the lid as it lies shut */
		if (g_cc_lid > 0.5) {
			double lc = g_cc_lid > 1.5 ? cos(CC_LID_SWING) : 1.0;
			double lsn = g_cc_lid > 1.5 ? sin(CC_LID_SWING) : 0.0;
			double ds = pf.z - CC_LID_HINGE_S, dn = pf.y - hn;
			double ls = (CC_LID_S0 + CC_LID_S1) * 0.5;
			vec3 q = v3(pf.x - (CC_LID_U0 + CC_LID_U1) * 0.5, ds * lsn + dn * lc,
				CC_LID_HINGE_S + ds * lc - dn * lsn - ls);
			h = hmin3(h, (hit3){ sd_round_box(q,
				v3((CC_LID_U1 - CC_LID_U0) * 0.5, (CC_LID_N1 - CC_LID_N0) * 0.5,
					(CC_LID_S1 - CC_LID_S0) * 0.5), 0.035), MID_CC_CHEEK });
			h = hmin3(h, (hit3){ sd_round_box(v3(q.x, q.y - (CC_LID_N1 - hn - 0.015),
				q.z - (CC_LID_S0 - ls) - 0.07), v3(0.25, 0.03, 0.045), 0.02), MID_CC_BRASS });
		}
	}

	/* a row of small pilot lenses along the front edge of the panel. Pushed
	 * right down to the lip, because the guarded set went in above them and
	 * these are what the desk sees of the machine. The six are at one height,
	 * one way up the panel and evenly across it, so the nearest across it is
	 * the nearest of all, and that one is enough: the shader carries one lens,
	 * not six (it is that close to the length SVGA3D runs a dummy for). */
	{
		double u0 = -inner + 0.70, pitch = 2.0 * (inner - 0.70) / 5.0;
		double k = fmin(fmax(floor((pf.x - u0) / pitch + 0.5), 0.0), 5.0);
		int i = (int)k;
		double u = u0 + k * pitch;
		vec3 q = vsub(pf, v3(u, 0, -panel.half + 0.136));
		h = hmin3(h, (hit3){ sd_cyl_y(v3(q.x, q.y - (RM_CC_SKIN + 0.035), q.z),
			0.13, 0.035), MID_CC_BRASS });
		h = hmin3(h, (hit3){ sd_sphere(v3(q.x, q.y - (RM_CC_SKIN + 0.095), q.z),
			0.095), (i & 1) ? MID_CC_LENS_GREEN : MID_CC_LENS_AMBER });
	}

	/* Nothing of it exists behind the desk's own far edge. Without this the
	 * stowed machine hangs over the void behind the desk and runs through
	 * the wall -- both invisible from straight above, but the moment the
	 * camera starts to tilt they are not. Cut there instead, and what the
	 * machine does is come out of the back edge of the desk. */
	double edge = (RM_ZF + 0.02) - (p.z + cc_dz(g_cc_out, g_rm_aspect));
	h.d = fmax(h.d, edge);
	return h;
}

/* Which control a point belongs to, for picking and for lighting it up. */
static int console_owner(vec3 p)
{
	struct cc_face panel, tower;
	p = vsub(p, v3(0.0, 0.0, cc_dz(g_cc_out, g_rm_aspect)));
	cc_faces(&panel, &tower);
	for (int i = 0; i < CC_COUNT; i++) {
		vec3 q = cc_to_face(CC[i].tower ? &tower : &panel, p);
		if (CC[i].on == CC_ON_LID && g_cc_lid > 1.5) {
			/* the lid up on its hinge: the four on it turned away, and a point of the lid the
			 * lid's where it would be, shut (map_console) */
			if (i != CC_LID) {
				continue;
			}
			double hn = (CC_LID_N0 + CC_LID_N1) * 0.5;
			double ds = q.z - CC_LID_HINGE_S, dn = q.y - hn;
			double lc = cos(CC_LID_SWING), lsn = sin(CC_LID_SWING);
			q = v3(q.x, hn + ds * lsn + dn * lc, CC_LID_HINGE_S + ds * lc - dn * lsn);
		}
		double n0 = cc_owner_n0(i), n1 = cc_owner_n1(i);
		vec3 d = v3(q.x - CC[i].u, q.y - (n0 + n1) * 0.5, q.z - CC[i].s);
		if (sd_box(d, v3(CC[i].hu, (n1 - n0) * 0.5, CC[i].hs)) < 0.0) {
			return i + 1;
		}
	}
	return 0;
}

/* ---- the post box ----------------------------------------------
 *
 * It stands on the desk at the back right, and from the desk all you see
 * of it is its cap: the sloped lid, the brass mouth of the slot, and --
 * when there is something in it -- the corner of an envelope that has not
 * gone all the way in. To read the post you have to turn and look at it,
 * which is the whole point of putting it in a box rather than throwing
 * the words across the middle of the desk.
 *
 * Cast iron under a coat of enamel, brass where hands and letters go. */
#define RM_PB_X0 9.60          /* where it stands on the desk as designed */
#define RM_PB_CX (RM_PB_X0 + RM_HX_GROW)
#define RM_PB_HX 1.90
#define RM_PB_HZ 0.80
#define RM_PB_PLINTH 0.34     /* the foot it stands on */
#define RM_PB_SHOULDER 4.05   /* where the body stops and the cap starts */
#define RM_PB_CAP_T 0.62      /* how thick the cap is */
#define RM_PB_TILT 0.185      /* the cap falls this much towards the front */

/* Where the slot in the lid was, in the cap's frame: across it, and how far
 * back. The lid has no slot now -- from the desk it is past the top of the
 * screen -- but the nameplate is still set out from where it was. */
#define RM_PB_SLOT_HU 1.28
#define RM_PB_SLOT_HS 0.17
#define RM_PB_SLOT_S (-0.34)

/* The slot the post goes in: across the head of the door, which is the
 * face of the box the desk looks at. FS_Y is its middle, up from the
 * door's hinge; FR the brass frame round it; the hood over it comes out
 * HOOD_OUT and slopes down by HOOD_TILT. */
#define RM_PB_DOOR_Y0 (RM_PB_PLINTH + 0.16)
#define RM_PB_FS_Y 2.75
#define RM_PB_FS_HU 1.25
#define RM_PB_FS_HH 0.11
#define RM_PB_FS_FR 0.09
#define RM_PB_HOOD_OUT 0.18
#define RM_PB_HOOD_TILT 0.30

/* Where the box stands: as far back as leaves the lettering on its
 * nameplate in view, and no further -- the slot behind it is past the top
 * of the screen (what shows that there is post will be something else).
 *
 * The line kept in view is TOP_F of the way down the plate, just over the
 * tops of the letters: TOP_N above the cap's top face and TOP_S along it
 * from the cap's middle, which puts it TOP_Y up and TOP_DZ behind the
 * box's own z. The top of the screen, at that height, is the sight line
 * room_hidden_z gives -- and it moves with the shape of the screen, which
 * is why the box does too: a fixed z showed the plate on one screen and
 * buried it on the next. TOP_GAP is a hair of margin over the letters. */
#define RM_PB_PLATE_S0 (RM_PB_SLOT_S + RM_PB_SLOT_HS + 0.30)
#define RM_PB_PLATE_S1 (RM_PB_HZ - 0.10)
#define RM_PB_TOP_F 0.30
#define RM_PB_TOP_S (RM_PB_PLATE_S0 + (RM_PB_PLATE_S1 - RM_PB_PLATE_S0) * RM_PB_TOP_F)
#define RM_PB_TOP_N (RM_PB_CAP_T * 0.5 + 0.012)
#define RM_PB_TOP_Y (RM_PB_SHOULDER + RM_PB_CAP_T * 0.5 \
	+ RM_PB_TOP_N * cos(RM_PB_TILT) - RM_PB_TOP_S * sin(RM_PB_TILT))
#define RM_PB_TOP_DZ (-(RM_PB_TOP_N * sin(RM_PB_TILT) + RM_PB_TOP_S * cos(RM_PB_TILT)))
#define RM_PB_TOP_GAP 0.02
#define RM_PB_CZ (RM_ZF + RM_PB_TOP_DZ + RM_PB_TOP_GAP \
	+ (RM_FOV_X_V * 0.5 / g_rm_aspect) * RM_PB_TOP_Y)

/* The cap's own frame: its middle, and the tilt it is set at. Everything
 * on the lid -- the slot, its brass mouth, the nameplate -- is placed in
 * here rather than guessed at in room coordinates. */
static vec3 pb_to_cap(vec3 p)
{
	vec3 q = vsub(p, v3(RM_PB_CX, RM_PB_SHOULDER + RM_PB_CAP_T * 0.5, RM_PB_CZ));
	return rot_x(q, -RM_PB_TILT);
}

static vec3 pb_cap_to_world(double u, double n, double s)
{
	vec3 q = rot_x(v3(u, n, s), RM_PB_TILT);
	return vadd(q, v3(RM_PB_CX, RM_PB_SHOULDER + RM_PB_CAP_T * 0.5, RM_PB_CZ));
}

/* How far the door has fallen open. Shut on the desk, open in the box's
 * own view, and the film in between is the door coming down. */
static double g_pb_open;

/* How many letters the opened box shows standing up (post_card_at);
 * the rest are down in it. */
#define RM_PB_FAN 5

static hit3 map_post(vec3 p)
{
	hit3 h = { 1e9, 0 };
	double gross = sd_box(vsub(p, v3(RM_PB_CX, (RM_PB_SHOULDER + RM_PB_CAP_T) * 0.5,
		RM_PB_CZ)), v3(RM_PB_HX + 0.30, (RM_PB_SHOULDER + RM_PB_CAP_T) * 0.5 + 0.35,
		RM_PB_HZ + 0.55));
	if (gross > DRAWER_NEAR) {
		return (hit3){ gross, 0 };
	}

	/* the foot, set out a little so the box has something to stand on */
	h = hmin3(h, (hit3){ sd_round_box(vsub(p,
		v3(RM_PB_CX, RM_PB_PLINTH * 0.5, RM_PB_CZ)),
		v3(RM_PB_HX + 0.16, RM_PB_PLINTH * 0.5, RM_PB_HZ + 0.16), 0.07),
		MID_POST_DARK });

	/* The body: a shell, not a solid. It has to be hollow because the
	 * whole point of the thing is that there is something inside it. */
	double body_y0 = RM_PB_PLINTH, body_y1 = RM_PB_SHOULDER;
	double mouth_y0 = body_y0 + 0.14, mouth_y1 = body_y1 - 0.26;
	{
		double shell = sd_round_box(vsub(p,
			v3(RM_PB_CX, (body_y0 + body_y1) * 0.5, RM_PB_CZ)),
			v3(RM_PB_HX, (body_y1 - body_y0) * 0.5, RM_PB_HZ), 0.16);
		double cav = sd_round_box(vsub(p,
			v3(RM_PB_CX, (mouth_y0 + mouth_y1) * 0.5, RM_PB_CZ + 0.08)),
			v3(RM_PB_HX - 0.26, (mouth_y1 - mouth_y0) * 0.5, RM_PB_HZ - 0.18), 0.10);
		double mouth = sd_box(vsub(p,
			v3(RM_PB_CX, (mouth_y0 + mouth_y1) * 0.5, RM_PB_CZ + RM_PB_HZ)),
			v3(RM_PB_HX - 0.26, (mouth_y1 - mouth_y0) * 0.5, 0.50));
		h = hmin3(h, (hit3){ fmax(shell, -fmin(cav, mouth)), MID_POST_RED });
		/* a dark liner at the back of the cavity, so a cream envelope has
		 * something to stand against */
		h = hmin3(h, (hit3){ sd_round_box(vsub(p,
			v3(RM_PB_CX, (mouth_y0 + mouth_y1) * 0.5, RM_PB_CZ - RM_PB_HZ + 0.30)),
			v3(RM_PB_HX - 0.30, (mouth_y1 - mouth_y0) * 0.5 - 0.04, 0.05), 0.04),
			MID_POST_DARK });
	}

	/* The door, hinged along its bottom edge. Shut it is the front of the
	 * box; open it has fallen forward and the letters are standing there.
	 * It is not baked in one position because it has to be in both. */
	{
		double hy = body_y0 + 0.16, hz = RM_PB_CZ + RM_PB_HZ;
		vec3 q = rot_x(vsub(p, v3(RM_PB_CX, hy, hz)), -g_pb_open * 1.78);
		double dh = mouth_y1 + 0.16 - hy;
		vec3 c = vsub(q, v3(0.0, dh * 0.5, 0.0));
		/* with the slot the post goes in cut through its head, as the slot
		 * of a letterbox at the gate is: from the desk, the front is the
		 * face of the box you can see */
		double door = sd_round_box(c, v3(RM_PB_HX - 0.06, dh * 0.5, 0.10), 0.07);
		double slot = sd_box(vsub(q, v3(0.0, RM_PB_FS_Y, 0.0)),
			v3(RM_PB_FS_HU, RM_PB_FS_HH, 0.30));
		h = hmin3(h, (hit3){ fmax(door, -slot), MID_POST_RED });
		/* its brass frame, standing just proud of the door */
		{
			vec3 f = vsub(q, v3(0.0, RM_PB_FS_Y, 0.12));
			double outer = sd_round_box(f, v3(RM_PB_FS_HU + RM_PB_FS_FR,
				RM_PB_FS_HH + RM_PB_FS_FR, 0.035), 0.03);
			double inner = sd_box(f, v3(RM_PB_FS_HU, RM_PB_FS_HH, 0.30));
			h = hmin3(h, (hit3){ fmax(outer, -inner), MID_CC_BRASS });
		}
		/* and the hood over it, sloping down and out, to keep the rain off
		 * what has been posted: from above, a strip of brass */
		{
			vec3 hq = rot_x(vsub(q, v3(0.0, RM_PB_FS_Y + RM_PB_FS_HH + RM_PB_FS_FR - 0.01,
				0.14)), -RM_PB_HOOD_TILT);
			h = hmin3(h, (hit3){ sd_round_box(vsub(hq, v3(0.0, 0.0, RM_PB_HOOD_OUT * 0.5)),
				v3(RM_PB_FS_HU + RM_PB_FS_FR + 0.04, 0.022, RM_PB_HOOD_OUT * 0.5), 0.015),
				MID_CC_BRASS });
		}
		/* the keyhole */
		h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(0, dh * 0.42, 0.07)),
			v3(0.24, 0.32, 0.05), 0.09), MID_CC_BRASS });
	}

	/* the cap, tilted so the weather runs off the front of it */
	vec3 cp = pb_to_cap(p);
	double cap = sd_round_box(cp, v3(RM_PB_HX + 0.14, RM_PB_CAP_T * 0.5,
		RM_PB_HZ + 0.14), 0.13);
	h = hmin3(h, (hit3){ cap, MID_POST_RED });

	/* Standing this far back it overhangs the desk by a finger's width.
	 * Nothing past the top board's own edge exists, so it does not hang
	 * over the void behind the desk -- the same cut the control panel
	 * comes out of. */
	h.d = fmax(h.d, (RM_ZF + 0.02) - p.z);
	return h;
}
