/* Part of draw.c: the room: its measures, its lamp and what years of use leave behind.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ================================================================
 * The room.
 *
 * Everything that is not a client window is one ray traced scene: floor,
 * wall, skirting, the desk, its drawer, what is in the drawer and the few
 * things standing on the top. One camera, one lamp, one set of shadows.
 *
 * Painting each surface flat and then hand-shading it towards looking solid
 * is what made the desktop read as a poster. A scene does not need
 * persuading: the top is a board and the drawer is a box, and they behave
 * like one because they are one.
 *
 * The camera looks straight down, as the flat desktop it replaced did, so
 * the desk is a rectangle and windows laid over it still line up with the
 * pointer. Units are the ones the drawer objects were authored in.
 *
 * All of it, down to shade_surface, has a twin in room.glsl that traces it
 * on a GPU. This is the reference and that is the port: change one, change
 * the other, and `desk-gputest --room` says whether they still agree.
 * Named numbers go across by themselves (room_glsl_build); the arithmetic
 * does not.
 * ================================================================ */

/* Half the width of the desk top. It is not fixed: the desk is as wide as
 * the screen is in points, so that what stands on it -- the console, the
 * clock, the post box -- is the same size on every screen at the same
 * density, and a bigger screen shows more desk rather than bigger things.
 * A Mac is built the same way: a window is so many points wide, and a
 * Retina display spends two pixels on each one rather than making it
 * twice the size. RM_HX_MIN is the desk the room was designed on, and the
 * narrowest it gets: below that the things on it would not fit, and the
 * whole room shrinks instead. See room_canon. */
#define RM_HX_MIN 16.0
/* (This and the other numbers of the room's shape -- g_rm_zf, g_rm_aspect,
 * g_rm_sh_hx -- are set by the thread making a room, and read by the
 * compositor's as it lays things over the room on the screen: atomic, so
 * that is a read of a number, not a race.) */
static _Atomic double g_rm_hx = RM_HX_MIN;
#define RM_HX g_rm_hx
/* the things at the two back corners move out with the corners */
#define RM_HX_GROW (g_rm_hx - RM_HX_MIN)
#define RM_ZN 7.2           /* near edge of the top */
#define RM_TOP_T 0.55       /* thickness of the top board */
#define RM_FLOOR (-19.0)

/* The far edge moves with the screen's aspect so the desk always fills the
 * frame exactly. Everything else about the desk is fixed. */
static _Atomic double g_rm_zf = -12.80;
#define RM_ZF g_rm_zf

/* The plan camera's image plane width at unit distance. Declared here as
 * well as at the camera because the bookcase's setback is derived from it. */
#define RM_FOV_X_V 0.78

static _Atomic double g_rm_aspect = 1.60;

/* The wall stands behind whatever the desk turned out to be. Pinned, it
 * ended up in front of the far edge on a four by three screen, where the
 * desk is deep, and a band of wall showed along the top. */
#define RM_WALL_Z (g_rm_zf - 1.60)

static void room_set_aspect(double aspect)
{
	double a = fmax(aspect, 0.2);
	g_rm_aspect = a;
	g_rm_zf = RM_ZN - 2.0 * RM_HX / a;
}

#define RM_DR_HX 13.2       /* half width of the drawer box */
#define RM_DR_TOP (-1.05)
#define RM_DR_BOT (-3.65)
#define RM_DR_FACE 8.05     /* z of the drawer face when shut */
#define RM_DR_BACK 2.20
#define RM_DR_PULL 3.30     /* how far it travels when opened: a hand's
                             * pull, not the whole of it -- the back of it
                             * stays in the desk */

/* Where the drawer objects sit: on the felt, in the middle of the box. */
#define RM_ITEM_Y (RM_DR_BOT + 0.30)
/* in the middle of what shows of the felt with the drawer out: from the
 * desk's front edge (and a little more, that the edge hides from a camera
 * over the middle of the desk) to the front of the drawer */
#define RM_ITEM_Z ((RM_ZN - RM_DR_PULL + 0.60 + RM_DR_FACE - 0.52) * 0.5)

/* ---- the bookcase against the back wall ------------------------
 *
 * Nothing behind the desk's far edge can be seen from straight above: the
 * ray that grazes the far edge of the top board is the ray at the top of
 * the frame, so anything past it is either behind the board or already off
 * the picture. That is what lets a six foot bookcase stand back there
 * without touching the desk view by a pixel.
 *
 * The line it has to clear is z = RM_ZF + (RM_FOV_X / 2 / aspect) * y, taken
 * at the case's lowest and nearest point, its plinth. The gap has to grow on
 * narrower screens, which is the same direction room_set_aspect() already
 * pushes the desk's far edge. */
static double room_hidden_z(double y)
{
	return g_rm_zf + (RM_FOV_X_V * 0.5 / g_rm_aspect) * y;
}

#define RM_SH_PLINTH_Z 0.60    /* the toe kick, set back from the front */
#define RM_SH_CLEAR    1.10    /* margin below the sight line */
#define RM_SH_D        8.40    /* outer depth of the case */
#define RM_SH_ZF (room_hidden_z(RM_FLOOR) + RM_SH_PLINTH_Z - RM_SH_CLEAR)
#define RM_SH_ZB (RM_SH_ZF - RM_SH_D)

#define RM_SH_SIDE     1.10    /* sides and the centre stile */
#define RM_SH_BACKT    0.55    /* the back panel */
#define RM_SH_BOARD    0.80    /* a shelf board */
#define RM_SH_PLINTH (-15.40)  /* top of the plinth */
#define RM_SH_BAY_B  (-2.60)   /* the board the binders stand on */
#define RM_SH_BAY_T    8.40    /* underside of the board above them */
#define RM_SH_TOP     20.20    /* top of the carcass */
#define RM_SH_CORN_Y  22.60    /* top of the cornice */
#define RM_SH_CORN_P   0.90    /* how far the cornice stands proud */

/* Sized to the frame once the camera is solved, the way the desk's depth is. */
static _Atomic double g_rm_sh_hx = 23.00;

/* ================================================================
 * Miniature ray traced objects (the things inside the drawer).
 *
 * The rig is a desk lamp shining down from above and in front, inside a
 * walnut drawer lined with green felt. That means three things have to be
 * true or the objects read as plastic toys: the lamp is an area light, so
 * highlights are ovals and shadows have soft edges; everything else in the
 * room still emits light, so surfaces facing away from the lamp pick up
 * walnut brown from the sides and felt green from below; and polished
 * things reflect that room rather than showing one white dot.
 * ================================================================ */

static double smoothstepf(double a, double b, double x)
{
	double t = clampf((x - a) / (b - a + 1e-9), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

/* ================================================================
 * What years of use leave behind
 *
 * None of this is geometry. A scratch is a line where the finish is thinner
 * and rougher than it is a millimetre either side; a worn edge is where the
 * finish has gone altogether and the timber under it has been handled; a
 * tarnish is what collects where nobody's cuff ever reaches.
 *
 * What separates wear from noise is that wear is not everywhere. Noise is
 * uniform, and a surface with uniform damage on it reads as a texture map,
 * which is exactly the thing that makes a render look like an asset. So
 * every field here is multiplied by where a hand actually goes.
 * ================================================================ */

/* Two octaves. The wear fields are all low frequency and there are a lot of
 * them, and five octaves of value noise for a blotch is money thrown away. */
static double room_fbm2(double x, double y)
{
	return room_noise(x, y) * 0.65 + room_noise(x * 2.17, y * 2.17) * 0.35;
}

/* A family of scratches.
 *
 * The obvious constructions both fail. A high power of ridged noise keeps
 * the crest only -- a line of zero width, which is invisible. A contour
 * band of a noise field has a width that goes as one over the gradient, so
 * the same mark is sub-pixel where the field is steep and a fat blur where
 * it is flat, and it reads as water staining rather than as a scratch.
 *
 * So the lines are laid out at a spacing, bent by low frequency noise so
 * they are not parallel, and given a half width measured in world units --
 * which is the only number that matters, because a scratch is a scratch by
 * being thin. Most of them are then rubbed out, and the ones that survive
 * fade in and out along their length.
 *
 * spacing: world units between lines. aniso: how much longer than they are
 * apart. halfw: world half width of one mark. Marks run along v. */
static double scratch_at(double u, double v, double spacing, double aniso,
	double halfw, double seed)
{
	double y = v / (spacing * aniso) + seed * 53.1;
	double warp = (room_fbm2(u / (spacing * 6.0) + seed * 3.1, y * 1.7) - 0.5)
		* spacing * 1.6;
	double x = (u + warp) / spacing + seed * 17.3;
	double d = fabs(fract(x) - 0.5) * spacing;   /* to the nearest line */
	double line = smoothstepf(halfw, halfw * 0.22, d);
	/* A surface scratched evenly all over is a texture map; a surface
	 * scratched in four places has been used. */
	double alive = room_noise(floor(x) * 1.73 + seed * 7.7, y * 6.0);
	return line * smoothstepf(0.50, 0.79, alive);
}

/* Knocks: isolated, rare, and nothing in between. A dent is a thing that
 * happened once, not a distribution. */
static double nick_at(double u, double v, double freq, double seed)
{
	double n = room_noise(u * freq + seed * 91.7, v * freq + seed * 13.3);
	return smoothstepf(0.930, 0.997, n);
}

/* Where the room actually gets handled. The near edge of the desk, where
 * the forearms are; a working patch in front of the chair, which is not
 * centred because nobody sits centred; and less and less of both as you go
 * back towards the wall. Everything in the room is in the same coordinates,
 * so the same field does for all of it. */
static double room_touch(vec3 w)
{
	double front = smoothstepf(-6.0, 7.4, w.z);
	double dx = (w.x - 1.8) / 9.5, dz = (w.z - 1.4) / 6.2;
	double seat = exp(-(dx * dx + dz * dz));
	double patchy = room_fbm2(w.x * 0.19 + 3.3, w.z * 0.19 + 7.1);
	return clampf((front * 0.58 + seat * 0.62) * (0.40 + patchy * 1.25), 0.0, 1.0);
}

/* The window is off to the front left, and has been for years. Walnut loses
 * its dye to that: it goes lighter, greyer and a little more orange, most
 * on the side that faces it and least where something has always stood. */
static double room_sun(vec3 w)
{
	double side = smoothstepf(9.0, -13.0, w.x);
	double reach = smoothstepf(-11.0, 4.0, w.z);
	double uneven = room_fbm2(w.x * 0.11 + 21.0, w.z * 0.11 + 4.0);
	return clampf(side * reach * (0.30 + uneven * 1.05), 0.0, 1.0);
}

/* The lamp: direction, colour, radiance, and the angular radius that makes
 * it an area light instead of a point. */
static const vec3 LIT_KEY_DIR = { 0.05, 0.90, 0.43 };
static const vec3 LIT_KEY_COL = { 1.00, 0.89, 0.69 };
#define LIT_KEY_IRRADIANCE 5.6   /* E on a surface square to the lamp */
#define LIT_KEY_ANGLE 0.16       /* angular radius of the softbox, radians */

/* Daylight leaking across the desk from the front left, and a cold rim off
 * the back wall that separates silhouettes from the felt. */
static const vec3 LIT_FILL_DIR = { -0.66, 0.30, 0.69 };
static const vec3 LIT_FILL_COL = { 0.36, 0.45, 0.60 };
static const vec3 LIT_RIM_DIR = { -0.10, 0.16, -0.98 };
static const vec3 LIT_RIM_COL = { 0.96, 0.90, 0.80 };

/* The two composite scenes. Object kinds 0..4 are the drawer objects on
 * their own; these two are whole rooms and get their own lamp rig. */
#define DRAWER_KIND 9
#define ROOM_KIND 10

/* Everything the objects can see that is not the lamp itself. */
static vec3 env_ambient(vec3 d)
{
	/* felt floor -> walnut side walls -> the lit room above */
	vec3 c = vmix(v3(0.026, 0.044, 0.024), v3(0.150, 0.098, 0.052),
		smoothstepf(-0.80, -0.04, d.y));
	/* The ceiling is a ceiling with one lamp under it, not a white dome.
	 * A bright even sky was most of what made the room read as a studio:
	 * it filled every shadow and left nothing for the lamp to do. */
	c = vmix(c, v3(0.128, 0.116, 0.106), smoothstepf(0.02, 0.88, d.y));
	double k = fmax(vdot(d, vnorm(LIT_KEY_DIR)), 0.0);
	c = vadd(c, vmul(LIT_KEY_COL, 0.30 * k * k * k));
	double f = fmax(vdot(d, vnorm(LIT_FILL_DIR)), 0.0);
	c = vadd(c, vmul(LIT_FILL_COL, 0.24 * pow(f, 2.5)));
	double r = fmax(vdot(d, vnorm(LIT_RIM_DIR)), 0.0);
	c = vadd(c, vmul(LIT_RIM_COL, 0.17 * pow(r, 7.0)));
	return c;
}

/* Diffuse ambient comes from a 9 term spherical harmonic projection of
 * env_ambient, built once. Evaluating it is a handful of multiplies, and
 * unlike a constant ambient it still knows which way is up. */
static vec3 g_sh[9];
static int g_sh_ready;

static void sh_build(void)
{
	if (g_sh_ready) {
		return;
	}
	const int n = 4096;
	for (int i = 0; i < 9; i++) {
		g_sh[i] = v3(0, 0, 0);
	}
	for (int i = 0; i < n; i++) {
		/* Hammersley over the sphere */
		double u1 = (i + 0.5) / n;
		unsigned int b = (unsigned int)i;
		b = (b << 16) | (b >> 16);
		b = ((b & 0x55555555u) << 1) | ((b & 0xAAAAAAAAu) >> 1);
		b = ((b & 0x33333333u) << 2) | ((b & 0xCCCCCCCCu) >> 2);
		b = ((b & 0x0F0F0F0Fu) << 4) | ((b & 0xF0F0F0F0u) >> 4);
		b = ((b & 0x00FF00FFu) << 8) | ((b & 0xFF00FF00u) >> 8);
		double u2 = b * 2.3283064365386963e-10;
		double z = 1.0 - 2.0 * u1;
		double r = sqrt(fmax(0.0, 1.0 - z * z));
		double phi = 2.0 * M_PI * u2;
		vec3 d = v3(r * cos(phi), r * sin(phi), z);
		vec3 e = env_ambient(d);
		double y[9];
		y[0] = 0.282095;
		y[1] = 0.488603 * d.y;
		y[2] = 0.488603 * d.z;
		y[3] = 0.488603 * d.x;
		y[4] = 1.092548 * d.x * d.y;
		y[5] = 1.092548 * d.y * d.z;
		y[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
		y[7] = 1.092548 * d.x * d.z;
		y[8] = 0.546274 * (d.x * d.x - d.y * d.y);
		double w = 4.0 * M_PI / n;
		for (int j = 0; j < 9; j++) {
			g_sh[j] = vadd(g_sh[j], vmul(e, y[j] * w));
		}
	}
	g_sh_ready = 1;
}

/* Ramamoorthi and Hanrahan's irradiance reconstruction. */
static vec3 sh_irradiance(vec3 n)
{
	const double c1 = 0.429043, c2 = 0.511664, c3 = 0.743125;
	const double c4 = 0.886227, c5 = 0.247708;
	vec3 e = vmul(g_sh[0], c4);
	e = vsub(e, vmul(g_sh[6], c5));
	e = vadd(e, vmul(g_sh[6], c3 * n.z * n.z));
	e = vadd(e, vmul(g_sh[1], 2.0 * c2 * n.y));
	e = vadd(e, vmul(g_sh[2], 2.0 * c2 * n.z));
	e = vadd(e, vmul(g_sh[3], 2.0 * c2 * n.x));
	e = vadd(e, vmul(g_sh[4], 2.0 * c1 * n.x * n.y));
	e = vadd(e, vmul(g_sh[5], 2.0 * c1 * n.y * n.z));
	e = vadd(e, vmul(g_sh[7], 2.0 * c1 * n.x * n.z));
	e = vadd(e, vmul(g_sh[8], c1 * (n.x * n.x - n.y * n.y)));
	return v3(fmax(e.x, 0.0), fmax(e.y, 0.0), fmax(e.z, 0.0));
}

/* Stand in for a prefiltered radiance cube: sharp at mirror roughness,
 * fading into the diffuse irradiance as the surface gets rough. */
static vec3 env_prefiltered(vec3 d, double rough)
{
	double t = clampf(rough * rough * 1.6, 0.0, 1.0);
	if (t >= 0.999) {
		return vmul(sh_irradiance(d), 1.0 / M_PI);
	}
	vec3 sharp = env_ambient(d);
	if (t <= 0.001) {
		return sharp;
	}
	return vmix(sharp, vmul(sh_irradiance(d), 1.0 / M_PI), t);
}

/* How far the drawer has come out, in world z. Set before a trace. */
static double g_room_open = 1.0;

static double room_pull(void)
{
	double e = clampf(g_room_open, 0, 1);
	return e * e * (3.0 - 2.0 * e) * RM_DR_PULL;
}

/* Where the lamp actually hangs. A single direction would light the far
 * corner of a box thirteen units wide exactly like the near one, and
 * parallel shadows across a scene that size read as a diagram. A position
 * gives falloff and diverging shadows, which is most of what "somebody
 * switched the light on" looks like.
 *
 * In the plan view the room lamp has to sit above AND forward of the desk.
 * Directly overhead it lands in the mirror direction of every horizontal
 * surface at once and washes the whole top out. */
static const vec3 LAMP_POS = { 1.30, 7.60, 5.40 };
#define LAMP_REF_DIST 8.4
#define LAMP_RADIUS 1.30

/* The room's lamp is a lamp on the desk, not a softbox over a set. It
 * stands off to the near left, it is close, and it has a shade -- so there
 * is a pool of light on the near left of the top that falls away towards
 * the far right corner, and the wall behind gets what is left over.
 *
 * The shade is what does most of the work. Distance falloff alone still
 * lights everything from the same direction at a similar brightness, which
 * reads as an evenly lit room; a cone reads as one lamp somebody switched
 * on. */
/* It stands on the NEAR RIGHT. The pool then falls on the middle of the
 * desk, where the paper is, and the far left corner -- where the control
 * panel is built into the back edge -- is the dimmest part of the room.
 * A large machine sitting in the brightest corner is the thing the desk is
 * about, and the desk is not about it. */
/* Where it stands on the desk as designed. A wider desk -- a bigger screen
 * in points -- has the lamp moved out and up with it, about the middle of
 * the near edge, and its brightness and size with it: the same pool on
 * more desk, and the machines in the corners lit as they were designed. */
static const vec3 ROOM_LAMP_POS0 = { 8.0, 12.5, 3.5 };
static const vec3 ROOM_LAMP_AIM = { -0.16, -0.97, -0.18 };
#define ROOM_LAMP_REF0 16.4
#define ROOM_LAMP_RADIUS0 2.30
#define ROOM_LAMP_K (RM_HX / RM_HX_MIN)
#define ROOM_LAMP_POS vadd(v3(0.0, 0.0, RM_ZN), \
	vmul(vsub(ROOM_LAMP_POS0, v3(0.0, 0.0, RM_ZN)), ROOM_LAMP_K))
#define ROOM_LAMP_REF (ROOM_LAMP_REF0 * ROOM_LAMP_K)
#define ROOM_LAMP_RADIUS (ROOM_LAMP_RADIUS0 * ROOM_LAMP_K)
#define ROOM_LAMP_CONE_IN 0.22    /* cos where the shade has cut it off */
#define ROOM_LAMP_CONE_OUT 0.80   /* cos where it is still the full beam */

struct key_light {
	vec3 dir;        /* from the surface towards the lamp */
	double irr;      /* irradiance on a surface square to it */
	double ang;      /* angular radius seen from the surface */
};

/* For pictures of the room's faces taken apart by light (the faces
 * trial): the room's lamp as bright everywhere as on a surface square to
 * it, and nothing in its way. Set only while nothing else is shading. */
static bool g_rf_flat, g_rf_unshadowed;

static struct key_light key_at(vec3 p, int kind)
{
	struct key_light k;
	vec3 lp;
	double ref, rad;
	if (kind == DRAWER_KIND) {
		lp = LAMP_POS;
		ref = LAMP_REF_DIST;
		rad = LAMP_RADIUS;
	} else if (kind == ROOM_KIND) {
		lp = ROOM_LAMP_POS;
		ref = ROOM_LAMP_REF;
		rad = ROOM_LAMP_RADIUS;
	} else {
		k.dir = vnorm(LIT_KEY_DIR);
		k.irr = LIT_KEY_IRRADIANCE;
		k.ang = LIT_KEY_ANGLE;
		return k;
	}
	vec3 d = vsub(lp, p);
	double dist = fmax(vlen(d), 0.75);
	k.dir = vmul(d, 1.0 / dist);
	k.irr = LIT_KEY_IRRADIANCE * (ref * ref) / (dist * dist);
	k.ang = atan(rad / dist);
	if (kind == ROOM_KIND) {
		/* inside the shade's beam, or out at the edge of the pool */
		double ax = vdot(vmul(k.dir, -1.0), vnorm(ROOM_LAMP_AIM));
		k.irr *= smoothstepf(ROOM_LAMP_CONE_IN, ROOM_LAMP_CONE_OUT, ax);
		if (g_rf_flat) {
			k.irr = LIT_KEY_IRRADIANCE;
		}
	}
	return k;
}

/* Two small lamps clipped under the upper board, one to a bay, the way a
 * shelf of things worth looking at always has one. The room's own lamp is
 * on the desk and a long way in front of the case, so what stands on that
 * shelf was readable only because the eye is forgiving.
 *
 * They cast no shadows. There is one lamp in this room that traces shadow
 * rays and this is not it -- these are here to make what is under them
 * legible, and a second shadow ray per sample would put forty per cent on
 * every bake for a thing nobody would be able to point at. */
static const vec3 SHELF_LAMP_COL = { 1.00, 0.86, 0.62 };
static const vec3 SHELF_LAMP_AIM = { 0.0, -0.94, 0.34 };
#define SHELF_LAMP_Y 7.85
#define SHELF_LAMP_IRR 26.0

static vec3 shelf_lamp_at(vec3 p, vec3 n)
{
	vec3 sum = v3(0, 0, 0);
	for (int i = 0; i < 2; i++) {
		vec3 lp = v3(i ? 8.2 : -8.2, SHELF_LAMP_Y, RM_SH_ZF - 0.15);
		vec3 d = vsub(lp, p);
		double dist = fmax(vlen(d), 0.70);
		vec3 l = vmul(d, 1.0 / dist);
		double nol = fmax(vdot(n, l), 0.0);
		if (nol <= 0.0) {
			continue;
		}
		double ax = vdot(vmul(l, -1.0), vnorm(SHELF_LAMP_AIM));
		double cone = smoothstepf(-0.05, 0.62, ax);
		sum = vadd(sum, vmul(SHELF_LAMP_COL,
			SHELF_LAMP_IRR * cone * nol / (dist * dist)));
	}
	return sum;
}

/* Radiance of the lamp disc as a mirror would see it. A disc of angular
 * radius t and radiance L puts E = L * pi * sin^2(t) onto a facing surface,
 * so pinning E fixes L. Keeping the two consistent is what lets a mirror and
 * a matte surface sit next to each other and look lit by the same lamp. */
static double lamp_radiance(const struct key_light *k)
{
	double s = sin(k->ang);
	return k->irr / (M_PI * s * s);
}

/* The lamp as seen through a reflection lobe of the given roughness: the
 * disc widens and dims, conserving flux, so a rough surface shows a broad
 * soft sheen and a polished one shows the source itself. Deliberately absent
 * from env_ambient, which feeds the diffuse SH and must not count it twice. */
static vec3 env_lamp(vec3 d, double rough, const struct key_light *k)
{
	double th = sqrt(k->ang * k->ang + rough * rough * 2.1);
	double ca = acos(clampf(vdot(d, k->dir), -1.0, 1.0));
	double t = smoothstepf(th * 2.40, th * 0.18, ca);
	t = t * t;
	if (t <= 0.0) {
		return v3(0, 0, 0);
	}
	double kk = fmin((k->ang * k->ang) / (th * th), 1.0);
	return vmul(LIT_KEY_COL, lamp_radiance(k) * kk * t);
}

/* Karis' analytic fit to the split sum DFG term. */
static void env_brdf(double nov, double rough, double *a, double *b)
{
	double rx = rough * -1.00 + 1.00;
	double ry = rough * -0.0275 + 0.0425;
	double rz = rough * -0.572 + 1.04;
	double rw = rough * 0.022 - 0.04;
	double a004 = fmin(rx * rx, exp2(-9.28 * nov)) * rx + ry;
	*a = a004 * -1.04 + rz;
	*b = a004 * 1.04 + rw;
}

static double sd_sphere(vec3 p, double r)
{
	return vlen(p) - r;
}

static double sd_box(vec3 p, vec3 b)
{
	vec3 q = v3(fabs(p.x) - b.x, fabs(p.y) - b.y, fabs(p.z) - b.z);
	double ox = fmax(q.x, 0.0), oy = fmax(q.y, 0.0), oz = fmax(q.z, 0.0);
	return fmin(fmax(q.x, fmax(q.y, q.z)), 0.0) + sqrt(ox * ox + oy * oy + oz * oz);
}

static double sd_round_box(vec3 p, vec3 b, double r)
{
	return sd_box(p, v3(fmax(b.x - r, 0), fmax(b.y - r, 0), fmax(b.z - r, 0))) - r;
}

static double sd_cyl_y(vec3 p, double r, double h)
{
	double d = sqrt(p.x * p.x + p.z * p.z) - r;
	double dy = fabs(p.y) - h;
	double ax = fmax(d, 0.0), ay = fmax(dy, 0.0);
	return fmin(fmax(d, dy), 0.0) + sqrt(ax * ax + ay * ay);
}

static double sd_torus(vec3 p, double r, double t)
{
	double q = sqrt(p.x * p.x + p.z * p.z) - r;
	return sqrt(q * q + p.y * p.y) - t;
}

static double sd_capped_cone(vec3 p, double r1, double r2, double h)
{
	vec3 q = v3(sqrt(p.x * p.x + p.z * p.z), p.y, 0);
	vec3 k1 = v3(r2, h, 0);
	vec3 k2 = v3(r2 - r1, 2.0 * h, 0);
	vec3 ca = v3(q.x - fmin(q.x, (q.y < 0.0) ? r1 : r2), fabs(q.y) - h, 0);
	double k2l = vlen(k2) + 1e-8;
	double t = clampf(vdot(vsub(k1, q), k2) / (k2l * k2l), 0, 1);
	vec3 cb = vsub(q, vadd(k1, vmul(k2, -t)));
	double s = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;
	return s * sqrt(fmin(vdot(ca, ca), vdot(cb, cb)));
}

static double sd_cyl_z(vec3 p, double r, double h)
{
	double d = sqrt(p.x * p.x + p.y * p.y) - r;
	double dz = fabs(p.z) - h;
	double ax = fmax(d, 0.0), ay = fmax(dz, 0.0);
	return fmin(fmax(d, dz), 0.0) + sqrt(ax * ax + ay * ay);
}

/* Involute-ish teeth with a chamfer, so the rim catches a highlight the way
 * a real machined gear does instead of reading as a cog-shaped blob. */
static double sd_gear_z(vec3 p, double r, double h, int n)
{
	double ang = atan2(p.y, p.x);
	double tooth = pow(fmax(0.0, cos(ang * n)), 3.0);
	double rad = r * (0.74 + 0.32 * tooth);
	double d = sd_cyl_z(p, rad, h);
	double chamfer = (sqrt(p.x * p.x + p.y * p.y) - rad * 0.86) * 0.42 + fabs(p.z) - h * 1.06;
	d = fmax(d, chamfer);
	d = fmax(d, -sd_cyl_z(p, r * 0.17, h + 0.2));
	d = fmin(d, sd_cyl_z(p, r * 0.27, h * 0.45));
	/* lightening holes */
	double ha = atan2(p.y, p.x) + M_PI;
	double sector = 2.0 * M_PI / 5.0;
	double hb = ha - sector * floor(ha / sector) - sector * 0.5;
	double hr = sqrt(p.x * p.x + p.y * p.y);
	vec3 hp = v3(hb * r * 0.52, hr - r * 0.46, p.z);
	if (r > 0.3) {
		d = fmax(d, -sd_cyl_z(hp, r * 0.115, h + 0.2));
	}
	return d;
}

typedef struct {
	double d;
	int id;
} hit3;

static hit3 hmin3(hit3 a, hit3 b)
{
	return a.d < b.d ? a : b;
}

enum {
	MID_CRT_BODY = 1,
	MID_CRT_BEZEL,
	MID_CRT_SCREEN,
	MID_CRT_KEYBED,
	MID_CRT_KEY,
	MID_CRT_LED,
	MID_GLOBE,
	MID_GLOBE_RING,
	MID_GLOBE_STAND,
	MID_FOLDER,
	MID_FOLDER_PAPER,
	MID_NOTES,
	MID_NOTES_BIND,
	MID_GEAR,
};

/* Materials belonging to the drawer and the room around it. */
enum {
	MID_FELT = 40,
	MID_DRAWER_WOOD,
	MID_ROOM_FLOOR = 70,
	MID_ROOM_WALL,
	MID_ROOM_SKIRT,
	MID_DESK_TOP,
	MID_DESK_BODY,
	MID_DRAWER_FACE,
	MID_BRASS,
	MID_GADGET_BODY,
	MID_GADGET_GLASS,
	MID_GADGET_LEATHER,
	MID_GADGET_DIAL,
	MID_GADGET_PAPER,
	MID_SHELF_CASE,
	MID_SHELF_BOARD,
	MID_SHELF_BACK,
	MID_BINDER_A,
	MID_BINDER_B,
	MID_BINDER_LABEL,
	MID_CC_STEEL,
	MID_CC_PANEL,
	MID_CC_BRASS,
	MID_CC_BAKELITE,
	MID_CC_GLASS,
	MID_CC_CHEEK,
	MID_CC_LENS_AMBER,
	MID_CC_LENS_GREEN,
	MID_CC_LENS_BLUE,
	MID_CC_LENS_RED,
	MID_POST_RED,
	MID_POST_DARK,
	MID_BOOK_LEATHER,
	MID_BOOK_GILT,
	MID_BINDER_FAV,
	MID_CC_LENS_WHITE,
};

static hit3 map_term(vec3 p)
{
	hit3 h = { 1e9, 0 };
	vec3 q = rot_y(rot_x(p, -0.32), 0.38);
	h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(0.00, 0.12, -0.06)), v3(0.50, 0.38, 0.30), 0.06), MID_CRT_BODY });
	vec3 qs = vsub(q, v3(0.00, 0.18, 0.22));
	qs.z *= 1.25;
	h = hmin3(h, (hit3){ sd_round_box(qs, v3(0.36, 0.24, 0.055), 0.04), MID_CRT_SCREEN });
	h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(0.00, 0.18, 0.18)), v3(0.40, 0.28, 0.035), 0.02), MID_CRT_BEZEL });
	h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(0.00, -0.38, 0.20)), v3(0.52, 0.05, 0.24), 0.03), MID_CRT_KEYBED });
	vec3 k = vsub(q, v3(0.00, -0.32, 0.18));
	double cell = 0.092;
	double kx = k.x - cell * floor(k.x / cell + 0.5);
	double kz = k.z - 0.082 * floor(k.z / 0.082 + 0.5);
	/* dished keycaps rather than flat slabs */
	double keys = sd_round_box(v3(kx, k.y, kz), v3(0.032, 0.022, 0.030), 0.008);
	keys = fmax(keys, -sd_sphere(v3(kx, k.y - 0.086, kz), 0.068));
	keys = fmax(keys, sd_box(k, v3(0.42, 0.08, 0.19)));
	h = hmin3(h, (hit3){ keys, MID_CRT_KEY });
	h = hmin3(h, (hit3){ sd_sphere(vsub(q, v3(0.32, -0.18, 0.22)), 0.026), MID_CRT_LED });
	return h;
}

static hit3 map_globe(vec3 p)
{
	hit3 h = { 1e9, 0 };
	vec3 q = rot_y(rot_x(p, -0.18), 0.22);
	vec3 c = v3(0.00, 0.12, 0.00);
	h = hmin3(h, (hit3){ sd_sphere(vsub(q, c), 0.46), MID_GLOBE });
	h = hmin3(h, (hit3){ sd_torus(vsub(q, c), 0.472, 0.016), MID_GLOBE_RING });
	vec3 ax = rot_z(vsub(q, c), 0.40);
	h = hmin3(h, (hit3){ sd_cyl_y(ax, 0.012, 0.48), MID_GLOBE_RING });
	h = hmin3(h, (hit3){ sd_cyl_y(vsub(q, v3(0, -0.28, 0)), 0.042, 0.14), MID_GLOBE_STAND });
	h = hmin3(h, (hit3){ sd_capped_cone(vsub(q, v3(0, -0.48, 0)), 0.30, 0.16, 0.045), MID_GLOBE_STAND });
	h = hmin3(h, (hit3){ sd_cyl_y(vsub(q, v3(0, -0.52, 0)), 0.32, 0.018), MID_GLOBE_STAND });
	return h;
}

/* Paper never lies flat. A low frequency ripple across a sheet costs one
 * sine and is the difference between a stack of card and a stack of tiles. */
static vec3 warp_sheet(vec3 q, double amp, double freq)
{
	double w = sin(q.x * freq) * 0.6 + sin(q.y * freq * 0.7 + 1.3) * 0.4;
	return v3(q.x, q.y, q.z - w * amp);
}

static hit3 map_folder(vec3 p)
{
	hit3 h = { 1e9, 0 };
	vec3 q = rot_y(rot_x(p, -0.42), 0.52);
	h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(0.00, -0.02, -0.05)), v3(0.48, 0.34, 0.032), 0.016), MID_FOLDER });
	vec3 front = rot_x(vsub(q, v3(0.00, -0.06, 0.08)), 0.18);
	h = hmin3(h, (hit3){ sd_round_box(front, v3(0.48, 0.30, 0.028), 0.016), MID_FOLDER });
	h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(-0.22, 0.34, -0.03)), v3(0.18, 0.08, 0.02), 0.012), MID_FOLDER });
	h = hmin3(h, (hit3){ sd_box(warp_sheet(vsub(q, v3(0.04, 0.08, -0.01)), 0.012, 6.0),
		v3(0.40, 0.30, 0.02)), MID_FOLDER_PAPER });
	h = hmin3(h, (hit3){ sd_box(warp_sheet(vsub(q, v3(0.02, 0.04, 0.02)), 0.010, 7.5),
		v3(0.36, 0.26, 0.012)), MID_FOLDER_PAPER });
	return h;
}

static hit3 map_notes(vec3 p)
{
	hit3 h = { 1e9, 0 };
	vec3 q = rot_y(rot_x(p, -0.40), 0.28);
	h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(0.00, -0.02, 0.00)), v3(0.38, 0.48, 0.07), 0.018), MID_NOTES });
	h = hmin3(h, (hit3){ sd_round_box(vsub(q, v3(0.00, 0.46, 0.00)), v3(0.38, 0.05, 0.08), 0.016), MID_NOTES_BIND });
	h = hmin3(h, (hit3){ sd_round_box(warp_sheet(vsub(q, v3(0.04, -0.05, -0.05)), 0.008, 5.0),
		v3(0.36, 0.45, 0.022), 0.012), MID_FOLDER_PAPER });
	return h;
}

static hit3 map_gear(vec3 p)
{
	hit3 h = { 1e9, 0 };
	vec3 q = rot_y(p, 0.18);
	q = rot_x(q, -0.22);
	h = hmin3(h, (hit3){ sd_gear_z(vsub(q, v3(-0.06, 0.08, 0.00)), 0.40, 0.07, 12), MID_GEAR });
	h = hmin3(h, (hit3){ sd_gear_z(vsub(q, v3(0.36, -0.28, 0.05)), 0.23, 0.055, 8), MID_GEAR });
	return h;
}

/* Every object fits inside this sphere in its own space. Rays that miss it
 * cost nothing, which is most of the frame, and rays that hit it start at
 * the entry point instead of crawling through empty space. */
#define SCENE_BOUND 1.25
