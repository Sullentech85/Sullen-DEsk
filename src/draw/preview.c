/* Part of draw.c: the whole desktop, for the preview tool.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- the whole desktop, for the preview tool ------------------- */

void desk_draw_desktop(cairo_t *cr, int w, int h, time_t now, bool with_demo_window,
	double drawer_open)
{
	desk_light_desk(w, h);
	desk_draw_room(cr, w, h, drawer_open);
	/* the mat lies on the boards, under everything that stands or lies on
	 * it (the compositor lays it as its own sprite, DESK_OVL_MAT) */
	{
		int ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0;
		double clip0 = 0.0, clip1 = 0.0;
		cairo_save(cr);
		if (desk_desk_place(w, h, 0.0, &ox0, &oy0, &clip0)
			&& desk_desk_place(w, h, drawer_open, &ox1, &oy1, &clip1)) {
			cairo_translate(cr, ox1 - ox0, oy1 - oy0);
		}
		desk_draw_overlay(cr, DESK_OVL_MAT, w, h, now, 0);
		desk_draw_overlay(cr, DESK_OVL_MAT_SHEET, w, h, now, 0);
		cairo_restore(cr);
	}
	struct room_xf xf;
	if (room_xform(w, h, drawer_open, &xf)) {
		room_cases_paint(cr, &xf);
	}
	desk_draw_gadgets(cr, w, h, now, drawer_open);
	desk_draw_console_idle(cr, w, h, drawer_open);
	desk_draw_post_idle(cr, w, h, drawer_open, 2);

	if (!with_demo_window) {
		return;
	}

	double ww = clampf(w * 0.42, 520, 640);
	double hh = clampf(h * 0.52, 380, 460);
	double x = w * 0.20, y = h * 0.14;
	desk_draw_window_frame(cr, x, y, ww, hh, TR("Settings"), true, 0, true);
	desk_draw_settings_body(cr, x + DESK_BORDER, y + DESK_TITLE_H,
		ww - DESK_BORDER * 2, hh - DESK_TITLE_H - DESK_BORDER);

	/* a letter, part way through being folded */
	cairo_surface_t *letter = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 300, 400);
	cairo_t *lcr = cairo_create(letter);
	desk_draw_window_frame(lcr, 0, 0, 300, 400, TR("Letter"), true, 0, false);
	cairo_save(lcr);
	cairo_rectangle(lcr, DESK_BORDER, DESK_TITLE_H,
		300 - DESK_BORDER * 2, 400 - DESK_TITLE_H - DESK_BORDER);
	cairo_clip(lcr);
	cairo_set_source_rgb(lcr, 0.58, 0.22, 0.20);
	cairo_rectangle(lcr, DESK_BORDER + 28, DESK_TITLE_H, 1.6,
		400 - DESK_TITLE_H - DESK_BORDER);
	cairo_fill(lcr);
	cairo_set_source_rgb(lcr, 0.62, 0.74, 0.86);
	cairo_set_line_width(lcr, 1.0);
	for (double ly = DESK_TITLE_H + 22; ly < 390; ly += 22) {
		cairo_move_to(lcr, 20, ly);
		cairo_line_to(lcr, 280, ly);
		cairo_stroke(lcr);
	}
	show_text(lcr, TR("Fold me like a letter."), 42, DESK_TITLE_H + 48,
		14, false, 0.18, 0.12, 0.08, 1, -1);
	cairo_restore(lcr);
	cairo_destroy(lcr);

	const double stages[] = { 0.16, 0.38, 0.62, 1.0 };
	double px = x + ww + 40.0;
	for (int i = 0; i < 4; i++) {
		int fw, fh;
		desk_paper_fold_size(letter, stages[i], (unsigned)(i + 1), &fw, &fh);
		if (px + fw > w - 30.0) {
			break;
		}
		double fy = y + hh - fh - 10.0;
		contact_shadow(cr, px, fy + fh * 0.86, fw, fh * 0.28, 0.32);
		cairo_save(cr);
		cairo_translate(cr, px, fy);
		desk_set_paint_origin(px, fy);
		desk_draw_paper_fold(cr, letter, stages[i], (unsigned)(i + 1));
		desk_set_paint_origin(0, 0);
		cairo_restore(cr);
		px += fw + 26;
	}
	cairo_surface_destroy(letter);
}

int desk_write_png(const char *path, int w, int h, bool with_demo_window)
{
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(surf);
	desk_draw_desktop(cr, w, h, time(NULL), with_demo_window, 1.0);
	cairo_status_t st = cairo_surface_write_to_png(surf, path);
	cairo_destroy(cr);
	cairo_surface_destroy(surf);
	return st == CAIRO_STATUS_SUCCESS ? 0 : 1;
}

/* ---- words ----
 *
 * The language changed (i18n.h): every picture kept with words printed in
 * it goes, to be printed again. Most are keyed on the generation of the
 * programs' list, which a change of language reads again; these are let
 * go as well, so that nothing has to be right about that. */
void desk_draw_forget_words(void)
{
	if (g_shelf_base) {
		cairo_surface_destroy(g_shelf_base);
		g_shelf_base = NULL;
	}
	g_shelf_base_room = 0;
	shelf_cuts_drop();
	g_cuts_room = 0;
	book_pics_drop(true);
	binder_pics_drop(true);
}

/* ---- the screen lock: the front door -------------------------------
 *
 * Locked, you are on the step of your own front door at night, close
 * enough to touch it: walnut, a frosted pane lit from the room, the name
 * board hung on it from a nail -- whose picture, whose name -- and at hand
 * height a brass plate with the slot the password goes into, the key in
 * its lock beside it. Let in, you lean in to the key and turn it, the door
 * swings in on the room and you walk through: the room comes up to fill
 * the view as the view of the bookcase from inside, which is where the
 * compositor takes over and sits you down at the desk. Told no, the key
 * will not go round.
 *
 * It is traced, like the room, but as a small scene of its own -- in
 * metres, with its own lights: a porch lamp overhead, a lantern on the
 * wall, the night -- simple enough to be traced again every frame while
 * the eye moves, at whatever resolution keeps up (the way a game holds its
 * frame rate), and finely, and kept, wherever the eye comes to rest. What
 * changes as you type is drawn over it: the lights in the slot, and the
 * cords the board hangs by. */

/* the scene: x to the right, y up from the step, z out of the wall toward
 * you; the eye stands a little over a metre back */
#define LK_OW 0.480         /* the opening: half its width */
#define LK_OH 2.100         /* its height */
#define LK_CW 0.110         /* the casing round it */
#define LK_CZ 0.028         /* how far the casing stands off the wall */
#define LK_WALL 0.240       /* the wall's thickness */
#define LK_DW 0.950         /* the door */
#define LK_DH 2.088
#define LK_DT 0.045
#define LK_DY 0.006         /* the gap under it */
#define LK_DZ (-0.055)      /* its face, shut */
#define LK_HX (-0.475)      /* its hinges, at the left and on the inside: it opens in */
#define LK_HZ (LK_DZ - LK_DT)
#define LK_SWING 1.45       /* how far it goes */

/* on the door, from its hinge edge and the step; its face is at z = LK_DT */
#define LK_STILE 0.125
#define LK_MID 0.020        /* half the stile between the lower panels */
#define LK_PANEL0 0.240
#define LK_RAIL0 0.960      /* the lock rail, up to the glass */
#define LK_GLASS0 1.360
#define LK_GLASS1 1.890
#define LK_MUNTIN 1.630

#define LK_SIGN_X 0.475     /* the board: hung from a nail, its back resting */
#define LK_SIGN_Y 1.505     /* on the door along its bottom edge, its top */
#define LK_SIGN_W 0.440     /* leaning out */
#define LK_SIGN_H 0.300
#define LK_SIGN_T 0.018
#define LK_SIGN_TILT 0.070
#define LK_CORD_X 0.180     /* where its cords go in, from its middle */
#define LK_NAIL_Y 1.935

#define LK_PL_X0 0.225      /* the brass plate */
#define LK_PL_X1 0.690
#define LK_PL_Y0 1.098
#define LK_PL_Y1 1.226
#define LK_PL_T 0.006
#define LK_WIN_X0 0.246     /* the slot in it */
#define LK_WIN_X1 0.669
#define LK_WIN_Y0 1.112
#define LK_WIN_Y1 1.176

#define LK_KEY_X 0.800      /* the lock, the key in it */
#define LK_KEY_Y 1.162
#define LK_KNOB_X 0.812
#define LK_KNOB_Y 0.985

#define LK_ROOM_Z (-2.70)   /* the far wall inside, the picture of the room on it */
#define LK_EXPOSURE 1.15

enum {
	LK_NONE = 0, LK_WALL_ID, LK_CASING, LK_JAMB, LK_STEP, LK_INSIDE, LK_PICTURE, LK_ROOMIN,
	LK_DOOR, LK_DOOR_EDGE, LK_SIGN, LK_SIGN_EDGE, LK_PLATE, LK_BRASS, LK_KEY,
	LK_LGLASS, LK_IRON, LK_DECO,
};

/* what on the door throws a shadow, softly */
enum { LK_OC_SIGN, LK_OC_PLATE, LK_OC_LOCK, LK_OC_KNOB, LK_OC_NAIL, LK_OC_N };

struct lk_tex {
	const unsigned char *px;    /* a cairo ARGB32 or A8 picture */
	int w, h, stride;
	bool a8;
};

/* ---- the room inside ----
 *
 * Through the door is the room the desk is in -- not a picture of it on a
 * far wall. It is built here as the room tracer has it, in that tracer's
 * own units (the desk top at y 0, the floor 19 below it: a unit is about
 * 3.85 cm) and the same axes: the desk on its legs, the bookcase behind
 * it, the post box on the desk, the Control Panel when it is out, the
 * walls -- boxes, enough to stand in. On to them the pictures the
 * compositor already has are thrown back from the cameras that took them,
 * each only on to what its camera could see: the view the walk ends on
 * (the bookcase, or the Control Panel) and the desk from straight above.
 * What neither saw -- the near face of the desk, the floor round it, the
 * side walls, the ceiling -- is made here in the room's own colours. So the
 * room has depth as you walk into it, and the walk ends exactly on the
 * picture. */
#define LK_RM_UNIT 0.0385       /* metres to one of the room's units */
#define LK_RM_BEHIND 0.90       /* the front wall, behind where the walk ends, metres */

struct lk_proj {
	struct room_cam cam;
	struct lk_tex tex;
	double x0, y0, sw, sh;      /* where the camera's frame lands on the picture, pixels */
	bool on;
};

enum { LK_RM_SHELL = 0, LK_RM_DESK, LK_RM_LEG, LK_RM_CASE, LK_RM_POST, LK_RM_CONSOLE };

struct lk_room {
	bool on;
	vec3 t;                     /* the room's origin, in the street's metres */
	double s;
	double wx, fy, cy, bz, fz;  /* the walls: x within +-wx, floor, ceiling, back, front */
	struct { vec3 lo, hi; int kind; } box[12];
	int nbox;
	double desk_hx, desk_z0, desk_z1;
	struct lk_proj proj[3];     /* the view the walk ends on; the bookcase as traced (if that
	                             * is not it); the desk from above */
	vec3 wall, ceil;            /* the colours of what no picture saw */
	cairo_surface_t *keep[2];   /* the room's own pictures in there (not ours) */
};

/* ---- the weather, and the time of day ----
 *
 * The scene was made for one night. What it is like out there now comes in
 * as a rig: how far up each lamp is (they are on a sensor: on at dusk,
 * off by day, kept on through a dark wet afternoon), the sun -- which way
 * it is, its colour, how big for the softness of its shadows; an eave over
 * the step keeps the high summer sun off the door -- the light all round
 * and the sky the brass and the glass show, how wet things are, how much
 * snow lies on what faces up, and a haze. A mood not set is the night the
 * scene was made for, to the number. */
struct lk_rig {
	double porch, lantern;     /* the porch lamp and the lantern, 0..1 */
	double glass;              /* the room's light through the pane, against as made */
	vec3 sun, sun_col;         /* toward the sun; its light (nothing for none) */
	double sun_soft;           /* its size: the half angle, radians */
	vec3 moon_col;
	vec3 amb_lo, amb_hi;       /* the light all round, from below and from above */
	vec3 sky_hi, sky_lo;       /* reflected: the sky overhead and at the horizon */
	vec3 gnd_hi, gnd_lo;       /* and the street, near and far below */
	double wet, snow;
	vec3 haze;                 /* the haze, in linear light, and how thick, a metre */
	double haze_k;
	double exposure;
	unsigned decor;
	int year;
	unsigned key;              /* what a picture of it depends on, to keep it by */
};

/* ---- dressed for the day: what there is of it ----
 *
 * Christmas, the New Year and a birthday each put something up at the
 * front door, and all of it is in the scene: traced like the rest, lit by
 * the same lamps and sun, throwing its shadows on the door and the wall.
 * A wreath round the name board -- fir with berries and cones and a red bow
 * at Christmas, tinsel and baubles for the New Year's party, the leaves of
 * the fall and a jack-o'-lantern at Halloween -- lights in a garland up the
 * casing, a bunch of balloons. Each is a group of distances in a box of
 * its own, marched only where a ray is inside the box; the wreath's is in
 * the door's frame, so it swings in with the door. */
enum { LKD_WREATH = 0, LKD_GARLAND, LKD_BALLOONS };
enum { LKD_FIR = 1, LKD_TINSEL, LKD_AUTUMN };   /* the wreath it is */
enum {
	DM_NEEDLE = 0, DM_BERRY, DM_CONE, DM_BOW, DM_BULB, DM_TINSEL, DM_BAUBLE, DM_BALLOON,
	DM_LEAF, DM_VINE, DM_PUMPKIN, DM_STEM, DM_GLOW, DM_FLESH,
};

struct lkd_group {
	int kind;
	bool door;         /* in the door's frame */
	vec3 lo, hi;       /* its box, in that frame */
};
#define LKD_MAX 4

struct lk_scene {
	struct lk_room room;
	struct lk_rig rig;
	struct lkd_group deco[LKD_MAX];   /* what the door is dressed in */
	int ndeco, wreath;
	bool rough;                /* a frame on the way somewhere: the dressing in its big shapes */
	double door;               /* how far the door has swung in, radians */
	double cd, sd;             /* and its cosine and sine */
	double key;                /* how far the key is turned, radians */
	double lamp;               /* how lit the room behind the glass is */
	double open;               /* the doorway: how much of the room's light is out */
	double room_hx, room_y0, room_y1;      /* the room inside */
	double pic_x0, pic_y0, pic_w, pic_h;   /* the picture of it on its far wall */
	struct lk_tex sign, gold, plate, pic;
	vec3 edge[5];             /* inside, by wall: the colour the picture has at that edge */
	double post;              /* how much of the lens there is: none once in the room,
	                           * whose picture already went through one */
};

struct lk_light {
	vec3 pos, col;
	double r;
};

/* the porch lamp, out of sight overhead, and the lantern on the wall */
static const struct lk_light LK_LIGHTS[2] = {
	{ { 0.15, 2.32, 0.95 }, { 3.20, 2.50, 1.70 }, 0.100 },
	{ { 0.735, 1.615, 0.125 }, { 0.34, 0.21, 0.09 }, 0.030 },
};
static const vec3 LK_MOON_DIR = { -0.50, 0.52, 0.69 };
static const vec3 LK_MOON_COL = { 0.040, 0.056, 0.105 };

/* the lantern: glass, its iron, the arm it is on */
static const struct {
	vec3 lo, hi;
	int id;
} LK_LANTERN[6] = {
	{ { 0.673, 1.515, 0.062 }, { 0.797, 1.715, 0.188 }, LK_LGLASS },
	{ { 0.663, 1.715, 0.052 }, { 0.807, 1.742, 0.198 }, LK_IRON },
	{ { 0.703, 1.742, 0.093 }, { 0.767, 1.782, 0.157 }, LK_IRON },
	{ { 0.667, 1.497, 0.056 }, { 0.803, 1.515, 0.194 }, LK_IRON },
	{ { 0.721, 1.457, 0.000 }, { 0.749, 1.497, 0.140 }, LK_IRON },
	{ { 0.697, 1.400, 0.000 }, { 0.773, 1.560, 0.012 }, LK_IRON },
};

/* the casing: left, right, head */
static const vec3 LK_CASE[3][2] = {
	{ { -LK_OW - LK_CW, 0.0, 0.0 }, { -LK_OW, LK_OH + LK_CW, LK_CZ } },
	{ { LK_OW, 0.0, 0.0 }, { LK_OW + LK_CW, LK_OH + LK_CW, LK_CZ } },
	{ { -LK_OW - LK_CW, LK_OH, 0.0 }, { LK_OW + LK_CW, LK_OH + LK_CW, LK_CZ } },
};
