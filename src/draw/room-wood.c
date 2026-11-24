/* Part of draw.c: what the desk is made of: woods, stains and the icons' pictures.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- what the desk is made of (desk_set_wood) ---- */

/* The timbers it may be, as the boards of a desk come: the same lacquer on
 * each, so that what differs is the wood. (Leather and linoleum lie on it,
 * as mats: desk_mat_preset.) */
static const struct wood DESK_WOODS[DESK_WOOD_COUNT] = {
	[DESK_WOOD_WALNUT] = WOOD_WALNUT_INIT,
	/* white oak: wide, open rings; coarse pores; the rays cut as flecks */
	[DESK_WOOD_OAK] = { { 0.50, 0.31, 0.13 }, { 0.25, 0.135, 0.048 }, { 0.84, 0.30, 0.86, 0.28 },
		{ 0.12, 0.09, 0.05 }, 18.0, 5.0, { 0.56, 0.42, 0.24 }, 0.45, 5.0, 2.0,
		{ 0.50, 0.38, 0.22 }, 1.0 },
	/* cherry: fine and close, red, and redder in the sun */
	[DESK_WOOD_CHERRY] = { { 0.44, 0.15, 0.062 }, { 0.20, 0.052, 0.020 },
		{ 0.84, 0.32, 0.84, 0.30 }, { 0.07, 0.04, 0.02 }, 40.0, 8.0, { 0.52, 0.32, 0.17 }, 0.12,
		8.5, 2.4, { 0.40, 0.14, 0.055 }, 0.9 },
	/* hard maple: pale, its rings hardly there, its pores closed */
	[DESK_WOOD_MAPLE] = { { 0.74, 0.56, 0.32 }, { 0.58, 0.40, 0.19 }, { 0.88, 0.24, 0.88, 0.22 },
		{ 0.05, 0.045, 0.03 }, 60.0, 9.0, { 0.78, 0.62, 0.38 }, 0.06, 9.5, 3.4,
		{ 0.72, 0.58, 0.36 }, 1.0 },
	/* ebony as furniture is made of it (Macassar): near black, striped through
	 * with dark brown, the stripes wandering along the board */
	[DESK_WOOD_EBONY] = { { 0.120, 0.074, 0.042 }, { 0.011, 0.008, 0.006 },
		{ 0.85, 0.30, 0.85, 0.30 }, { 0.016, 0.012, 0.009 }, 46.0, 7.0, { 0.090, 0.062, 0.040 }, 0.25,
		2.6, 1.15, { 0.110, 0.080, 0.056 }, 0.30 },
};

static _Atomic int g_desk_wood = DESK_WOOD_WALNUT, g_desk_stain = DESK_STAIN_NATURAL;
/* While the faces' pictures are put right for this room (rf_fix), the desk
 * is as it was made, as it is in them: what is put right is the light, and
 * the wood is put in afterwards (desk_relight). */
static _Thread_local bool g_desk_as_made;
/* What the desk is made of in the room being traced: what was wanted as the
 * trace began (wood_bake_begin), so that the GPU and the C, and every sheet
 * of one room, are of one wood whatever is chosen meanwhile -- a desk of
 * another wood chosen halfway through had the GPU's sheet of one wood
 * checked against the C's of the other, and the GPU put down for it. */
static _Atomic int g_wood_bake = DESK_WOOD_WALNUT, g_stain_bake = DESK_STAIN_NATURAL;

static void wood_bake_begin(void)
{
	atomic_store(&g_wood_bake, atomic_load(&g_desk_wood));
	atomic_store(&g_stain_bake, atomic_load(&g_desk_stain));
}

/* desk_look's number for the room being traced */
static unsigned bake_look(void)
{
	return (unsigned)atomic_load(&g_wood_bake) * 8u + (unsigned)atomic_load(&g_stain_bake);
}

static const char *const WOOD_NAMES[DESK_WOOD_COUNT] = { "walnut", "oak", "cherry", "maple",
	"ebony" };
static const char *const STAIN_NAMES[DESK_STAIN_COUNT] = { "natural", "honey", "brown", "dark",
	"grey" };

bool desk_wood_parse(const char *s, int *wood)
{
	for (int i = 0; s && i < DESK_WOOD_COUNT; i++) {
		if (!strcasecmp(s, WOOD_NAMES[i])) {
			*wood = i;
			return true;
		}
	}
	return false;
}

bool desk_stain_parse(const char *s, int *stain)
{
	for (int i = 0; s && i < DESK_STAIN_COUNT; i++) {
		if (!strcasecmp(s, STAIN_NAMES[i])) {
			*stain = i;
			return true;
		}
	}
	/* (the other spelling) */
	if (s && !strcasecmp(s, "gray")) {
		*stain = DESK_STAIN_GREY;
		return true;
	}
	return false;
}

bool desk_stain_offered(int wood, int stain)
{
	if (stain < 0 || stain >= DESK_STAIN_COUNT) {
		return false;
	}
	return wood != DESK_WOOD_EBONY || (stain != DESK_STAIN_BROWN && stain != DESK_STAIN_DARK);
}

bool desk_set_wood(int wood, int stain)
{
	wood = wood < 0 || wood >= DESK_WOOD_COUNT ? DESK_WOOD_WALNUT : wood;
	stain = desk_stain_offered(wood, stain) ? stain : DESK_STAIN_NATURAL;
	int was_wood = atomic_exchange(&g_desk_wood, wood);
	int was_stain = atomic_exchange(&g_desk_stain, stain);
	return was_wood != wood || was_stain != stain;
}

/* What the desk is made of, as a number for the room traced of it: 0 for
 * walnut as it was made, so that a room traced before there was a choice is
 * still that room. */
static unsigned desk_look(void)
{
	return (unsigned)atomic_load(&g_desk_wood) * 8u + (unsigned)atomic_load(&g_desk_stain);
}

/* A stain over the wood: through it, the grain as it was. */
static vec3 stained(vec3 a, int stain)
{
	switch (stain) {
	case DESK_STAIN_HONEY:
		return vmul3(a, v3(1.16, 0.90, 0.42));
	case DESK_STAIN_BROWN:
		return vmul3(a, v3(0.60, 0.48, 0.38));
	case DESK_STAIN_DARK:
		return vmul3(a, v3(0.28, 0.23, 0.20));
	case DESK_STAIN_GREY: {
		/* a grey wash: the colour drawn out of it, the figure left */
		double lum = a.x * 0.30 + a.y * 0.59 + a.z * 0.11;
		return vmix(a, vmul(v3(0.93, 0.96, 1.0), lum * 1.18), 0.72);
	}
	default:
		return a;
	}
}

/* The desk's own wood, cut as a part of it is, under its stain. */
static void desk_wood_at(vec3 q, double u, double v, struct mtl *m, double plank, double use)
{
	if (g_desk_as_made) {
		timber_at(&WOOD_WALNUT, q, u, v, m, plank, use);
		return;
	}
	int wood = atomic_load_explicit(&g_wood_bake, memory_order_relaxed);
	timber_at(&DESK_WOODS[wood], q, u, v, m, plank, use);
	m->albedo = stained(m->albedo, atomic_load_explicit(&g_stain_bake, memory_order_relaxed));
}

/* Somebody has been putting a cup down on this desk for years and not
 * always on a mat. What is left is a ring, because the edge of the puddle
 * is what dried last and darkest -- and it is never a whole ring, because
 * it got wiped at while it was still wet. */
static void desk_stains(vec3 w, struct mtl *m)
{
	static const struct { double x, z, r, a; } RING[3] = {
		{ -2.10,  3.40, 1.30, 1.00 },
		{  5.70,  0.90, 1.14, 0.72 },
		{ -6.90, -1.10, 1.46, 0.46 },
	};
	for (int i = 0; i < 3; i++) {
		double dx = w.x - RING[i].x, dz = w.z - RING[i].z;
		double d = sqrt(dx * dx + dz * dz);
		double edge = smoothstepf(0.17, 0.02, fabs(d - RING[i].r));
		double inside = smoothstepf(RING[i].r, RING[i].r - 0.55, d);
		double a = atan2(dz, dx);
		double keep = smoothstepf(0.28, 0.64,
			room_fbm2(cos(a) * 1.7 + i * 7.3, sin(a) * 1.7 + i * 2.9));
		double ring = edge * keep * RING[i].a;
		m->albedo = vmix(m->albedo, vmul(m->albedo, 0.58), ring * 0.60);
		m->albedo = vmix(m->albedo, vmul(m->albedo, 1.12),
			inside * keep * RING[i].a * 0.14);
		m->rough = clampf(m->rough + ring * 0.26, 0.05, 1.0);
		m->coat = clampf(m->coat * (1.0 - ring * 0.60), 0.0, 1.0);
	}
	/* and one spill nobody got to in time */
	double sx = (w.x - 8.60) / 2.30, sz = (w.z + 4.10) / 1.70;
	double sd = sqrt(sx * sx + sz * sz) * (0.75 + room_fbm2(w.x * 0.8 + 3.0, w.z * 0.8) * 0.6);
	double spill = smoothstepf(1.05, 0.45, sd);
	m->albedo = vmix(m->albedo, vmul(m->albedo, 0.68), spill * 0.40);
	m->coat = clampf(m->coat * (1.0 - spill * 0.50), 0.0, 1.0);
	m->rough = clampf(m->rough + spill * 0.14, 0.05, 1.0);
}

static void icon_material(int kind, int id, vec3 p, struct mtl *m)
{
	m->albedo = v3(0.5, 0.5, 0.5);
	m->rough = 0.45;
	m->metal = 0.0;
	m->edge = 1.0;
	m->emit = v3(0, 0, 0);
	m->coat = 0.0;
	m->coat_rough = 0.08;
	m->bump_freq = 0.0;
	m->bump_amp = 0.0;
	m->dust = 0.0;

	vec3 w = p;                              /* world space, for the room */
	p = scene_local(p, kind, id);            /* object space, for the objects */
	double grain = room_fbm(p.x * 6.2 + p.z * 1.4, p.y * 5.1);
	switch (id) {
	case MID_CRT_BODY: {
		/* Beige ABS that has been sitting in daylight for twenty years:
		 * yellower low down where the case is thickest, pebbled surface. */
		vec3 q = rot_y(rot_x(p, -0.32), 0.38);
		double age = smoothstepf(0.34, -0.30, q.y);
		vec3 fresh = v3(0.575, 0.552, 0.485);
		vec3 yellowed = v3(0.585, 0.508, 0.352);
		m->albedo = vmix(fresh, yellowed, age * 0.85);
		m->albedo = vmul(m->albedo, 0.94 + grain * 0.12);
		/* moulding seam where the two shell halves meet */
		double seam = smoothstepf(0.010, 0.0, fabs(q.y + 0.145));
		m->albedo = vmul(m->albedo, 1.0 - seam * 0.30);
		m->rough = 0.46 + grain * 0.10 + seam * 0.10;
		m->bump_freq = 190.0;
		m->bump_amp = 0.030;
		m->dust = 1.0;
		break;
	}
	case MID_CRT_BEZEL:
		m->albedo = v3(0.055, 0.055, 0.062);
		m->rough = 0.34;
		m->bump_freq = 240.0;
		m->bump_amp = 0.022;
		m->dust = 0.8;
		break;
	case MID_CRT_SCREEN: {
		/* Phosphor behind curved glass. The glass is what sells it, so it
		 * gets a mirror coat that actually reflects the room. */
		vec3 q = rot_y(rot_x(p, -0.32), 0.38);
		double u = clampf((q.x + 0.32) / 0.64, 0, 1);
		double v = clampf((0.40 - q.y) / 0.46, 0, 1);
		double scan = 0.42 + 0.58 * (0.5 + 0.5 * sin(v * 92.0));
		double vig = smoothstepf(0.0, 0.16, u) * smoothstepf(1.0, 0.84, u)
			* smoothstepf(0.0, 0.14, v) * smoothstepf(1.0, 0.86, v);
		vig = 0.34 + 0.66 * vig;
		m->albedo = v3(0.020, 0.055, 0.026);
		m->rough = 0.045;
		m->coat = 0.0;
		vec3 phos = v3(0.10, 0.78, 0.20);
		double lum = 0.30 * scan;
		if (u > 0.13 && u < 0.29 && v > 0.33 && v < 0.63) {
			lum = 2.30;
		}
		if (u > 0.31 && u < 0.80 && v > 0.55 && v < 0.655) {
			lum = 1.55;
		}
		if (u > 0.13 && u < 0.62 && v > 0.20 && v < 0.29) {
			lum = 1.20;
		}
		m->emit = vmul(phos, lum * scan * vig);
		break;
	}
	case MID_CRT_KEYBED:
		m->albedo = v3(0.115, 0.110, 0.104);
		m->rough = 0.58;
		m->bump_freq = 210.0;
		m->bump_amp = 0.030;
		m->dust = 1.0;
		break;
	case MID_CRT_KEY: {
		/* Per-key tint variation, the way a real keyboard never has two
		 * caps that match exactly. */
		vec3 q = rot_y(rot_x(p, -0.32), 0.38);
		vec3 k = vsub(q, v3(0.00, -0.32, 0.18));
		double cx = floor(k.x / 0.092 + 0.5);
		double cz = floor(k.z / 0.082 + 0.5);
		double tint = room_seed((int)cx * 64 + (int)cz, 5);
		m->albedo = vmul(v3(0.735, 0.715, 0.660), 0.92 + tint * 0.16);
		m->rough = 0.40 + tint * 0.10;
		m->bump_freq = 260.0;
		m->bump_amp = 0.018;
		m->dust = 0.5;
		break;
	}
	case MID_CRT_LED:
		m->albedo = v3(0.10, 0.45, 0.16);
		m->rough = 0.08;
		m->emit = v3(0.30, 3.20, 0.60);
		break;
	case MID_GLOBE: {
		vec3 q = rot_y(rot_x(p, -0.18), 0.22);
		vec3 gn = vnorm(vsub(q, v3(0, 0.12, 0)));
		double land = continent(gn);
		if (gn.y > 0.80 || gn.y < -0.74) {
			m->albedo = v3(0.88, 0.91, 0.95);
			m->rough = 0.42;
		} else if (land > 0.18) {
			double desert = clampf(land * 0.4 + room_fbm(gn.x * 4, gn.z * 4) * 0.5, 0, 1);
			m->albedo = vmix(v3(0.135, 0.320, 0.098), v3(0.520, 0.390, 0.155), desert * 0.60);
			m->rough = 0.60;
			m->bump_freq = 46.0;
			m->bump_amp = 0.055;
		} else {
			double shallow = clampf(0.55 + gn.y * 0.2 + land * 1.4, 0, 1);
			m->albedo = vmix(v3(0.018, 0.070, 0.220), v3(0.075, 0.330, 0.520), shallow);
			m->rough = 0.30;
		}
		/* printed paper gores under varnish: faint seams every 30 degrees */
		double lon = atan2(gn.x, gn.z);
		double gore = fabs(fmod(lon + 20.0 * M_PI, M_PI / 6.0) - M_PI / 12.0);
		m->albedo = vmul(m->albedo, 1.0 - smoothstepf(0.018, 0.0, gore) * 0.16);
		m->coat = 1.0;
		m->coat_rough = 0.055;
		m->dust = 0.35;
		break;
	}
	case MID_GLOBE_RING: {
		/* Lacquered brass, worn bright along the edge you grab. */
		double brush = room_noise(p.x * 120.0 + p.y * 18.0, p.z * 120.0);
		m->albedo = vmul(v3(0.86, 0.66, 0.28), 0.90 + brush * 0.18);
		m->rough = 0.155 + brush * 0.10;
		m->metal = 1.0;
		m->dust = 0.25;
		break;
	}
	case MID_GLOBE_STAND: {
		/* Turned walnut: rings run around the lathe axis. */
		vec3 q = rot_y(rot_x(p, -0.18), 0.22);
		double r = sqrt(q.x * q.x + q.z * q.z);
		double rings = 0.5 + 0.5 * sin(r * 130.0 + room_fbm(r * 9.0, q.y * 6.0) * 5.0);
		m->albedo = vmix(v3(0.105, 0.048, 0.022), v3(0.215, 0.110, 0.052), rings * 0.7 + grain * 0.3);
		m->rough = 0.34 + rings * 0.10;
		m->coat = 0.6;
		m->coat_rough = 0.16;
		m->bump_freq = 120.0;
		m->bump_amp = 0.022;
		m->dust = 0.8;
		break;
	}
	case MID_FOLDER: {
		/* Manila card: long fibres, a soft sheen, scuffed corners. */
		double fib = room_fbm(p.x * 150.0, p.y * 24.0);
		m->albedo = vmul(v3(0.760, 0.580, 0.245), 0.93 + fib * 0.14);
		m->rough = 0.62 + fib * 0.12;
		m->bump_freq = 330.0;
		m->bump_amp = 0.026;
		m->dust = 0.9;
		break;
	}
	case MID_FOLDER_PAPER: {
		double fib = room_fbm(p.x * 170.0, p.y * 28.0);
		m->albedo = vmul(v3(0.855, 0.838, 0.785), 0.96 + fib * 0.08);
		m->rough = 0.70;
		m->bump_freq = 360.0;
		m->bump_amp = 0.020;
		m->dust = 0.7;
		break;
	}
	case MID_NOTES: {
		vec3 q = rot_y(rot_x(p, -0.40), 0.28);
		double fib = room_fbm(q.x * 160.0, q.y * 26.0);
		m->albedo = vmul(v3(0.900, 0.815, 0.400), 0.95 + fib * 0.10);
		if (q.x < -0.20) {
			m->albedo = v3(0.640, 0.105, 0.075);
		} else if (fabs(fmod(q.y + 10.0, 0.078) - 0.039) < 0.010 && q.y < 0.38) {
			m->albedo = vmix(m->albedo, v3(0.30, 0.44, 0.66), 0.80);
		}
		m->rough = 0.66;
		m->bump_freq = 340.0;
		m->bump_amp = 0.024;
		m->dust = 0.8;
		break;
	}
	case MID_NOTES_BIND:
		m->albedo = v3(0.480, 0.070, 0.048);
		m->rough = 0.44;
		m->bump_freq = 200.0;
		m->bump_amp = 0.030;
		m->dust = 0.6;
		break;
	case MID_GEAR: {
		/* Machined brass: concentric turning marks, scratches, and the
		 * tarnish that collects in the roots of the teeth. */
		vec3 q = rot_x(rot_y(p, 0.18), -0.22);
		double r = sqrt(q.x * q.x + q.y * q.y);
		double turn = 0.5 + 0.5 * sin(r * 520.0);
		double scratch = pow(room_noise(q.x * 34.0, q.y * 11.0 + q.z * 5.0), 9.0);
		double tarnish = clampf(room_fbm(q.x * 7.0, q.y * 7.0) * 1.3 - 0.35, 0.0, 1.0);
		m->albedo = vmix(v3(0.880, 0.662, 0.245), v3(0.395, 0.300, 0.150), tarnish * 0.55);
		m->albedo = vadd(m->albedo, vmul(v3(1, 1, 1), scratch * 0.10));
		m->rough = 0.185 + turn * 0.075 + tarnish * 0.24 - scratch * 0.10;
		m->rough = clampf(m->rough, 0.05, 0.85);
		m->metal = 1.0;
		m->bump_freq = 430.0;
		m->bump_amp = 0.016;
		m->dust = 0.45;
		break;
	}

	/* ---- the drawer and the room ---- */
	case MID_FELT: {
		/* Baize: short fibres lying every which way, dusty in the corners,
		 * and a little lighter where something has been standing on it. */
		double fib = room_fbm(w.x * 34.0, w.z * 34.0);
		double nap = room_fbm(w.x * 7.0 + 13.0, w.z * 7.0);
		m->albedo = vmix(v3(0.036, 0.082, 0.048), v3(0.072, 0.134, 0.078), fib);
		m->albedo = vmul(m->albedo, 0.86 + nap * 0.30);
		/* Baize goes grey where the nap has been rubbed off it and dusty
		 * where it has not been touched at all, and the two are never in
		 * the same place. */
		double bare = clampf(room_fbm2(w.x * 0.95 + 21.0, w.z * 0.95) * 1.6 - 0.42,
			0.0, 1.0);
		m->albedo = vmix(m->albedo, v3(0.062, 0.078, 0.058), bare * 0.55);
		m->rough = 0.94 - bare * 0.14;
		m->bump_freq = 52.0;
		m->bump_amp = 0.075;
		m->dust = 0.4;
		break;
	}
	case MID_DRAWER_WOOD: {
		/* Inside faces are unfinished: same timber, no polish. */
		double vert = fabs(w.x) > RM_DR_HX - 1.0;
		/* never polished, and everything in here slides about */
		desk_wood_at(w, vert ? w.z : w.x, vert ? w.y * 3.0 : w.z, m, 0.0, 0.60);
		m->coat = 0.0;
		m->albedo = vmul(m->albedo, 0.78);
		m->rough = clampf(m->rough + 0.22, 0.2, 1.0);
		break;
	}
	case MID_DRAWER_FACE:
		/* the one board in the room that gets pulled every day */
		desk_wood_at(w, w.x, w.y * 2.6, m, 0.0, 1.10);
		break;
	case MID_DESK_TOP:
		/* the top is made of boards running front to back */
		desk_wood_at(w, w.x, w.z, m, 5.4, 1.00);
		desk_stains(w, m);
		break;
	case MID_DESK_BODY:
		/* the ends and the back rail: knees and shoes, not hands */
		desk_wood_at(w, w.z, w.y, m, 0.0, 0.50);
		m->albedo = vmul(m->albedo, 0.72);
		m->coat *= 0.55;
		break;
	case MID_BRASS: {
		/* What makes brass read as brass rather than as gold paint is that
		 * it is never one colour. It goes brown and slightly green wherever
		 * nothing rubs it, and comes back to yellow under a thumb -- so the
		 * bright part of a pull is the shape of the hand that uses it. */
		double brush = room_noise(w.x * 90.0, w.y * 14.0 + w.z * 6.0);
		double rub = clampf(0.22 + room_touch(w) * 1.5, 0.0, 1.0);
		double tarn = clampf(room_fbm2(w.x * 2.3 + 4.0, (w.y * 1.7 + w.z) * 2.3) * 1.55 - 0.32,
			0.0, 1.0);
		tarn = clampf(tarn * (1.0 - rub * 0.78) + 0.12 * (1.0 - rub), 0.0, 1.0);
		m->albedo = vmix(v3(0.862, 0.660, 0.296), v3(0.318, 0.272, 0.158), tarn * 0.80);
		m->albedo = vmul(m->albedo, 0.88 + brush * 0.22);
		/* the marks a cloth leaves, all lying the one way */
		double sc = scratch_at(w.x, w.y * 0.7 + w.z * 0.7, 0.30, 7.0, 0.006, 2.0);
		m->albedo = vmix(m->albedo, v3(0.930, 0.818, 0.560), sc * 0.34);
		m->rough = clampf(0.185 + brush * 0.10 + tarn * 0.40 - sc * 0.07, 0.04, 1.0);
		m->metal = 1.0;
		m->dust = 0.3;
		break;
	}
	case MID_ROOM_FLOOR: {
		/* oak boards, seen at a distance and mostly in shadow */
		double board = floor(w.x / 3.6);
		double bu = w.x - board * 3.6;
		double j = room_seed((int)board, 6);
		double g = 0.5 + 0.5 * sin((bu * 2.4 + room_fbm(w.z * 0.22 + board, bu) * 4.0) * 2.0 * M_PI);
		m->albedo = vmix(v3(0.168, 0.118, 0.072), v3(0.098, 0.066, 0.040), g * (0.6 + j * 0.4));
		double seam = smoothstepf(1.80, 1.74, fabs(bu - 1.8));
		m->albedo = vmul(m->albedo, 1.0 - seam * 0.6);
		m->rough = 0.42 + g * 0.18;
		m->coat = 0.35;
		m->coat_rough = 0.22;
		m->bump_freq = 14.0;
		m->bump_amp = 0.030;
		m->dust = 0.9;
		break;
	}
	case MID_ROOM_WALL: {
		double t = room_fbm(w.x * 1.6, w.y * 1.6);
		m->albedo = vmul(v3(0.300, 0.286, 0.262), 0.92 + t * 0.16);
		m->rough = 0.88;
		m->bump_freq = 18.0;
		m->bump_amp = 0.045;
		break;
	}
	case MID_ROOM_SKIRT:
		m->albedo = v3(0.300, 0.284, 0.268);
		m->rough = 0.52;
		m->coat = 0.4;
		m->coat_rough = 0.20;
		break;
	case MID_GADGET_BODY: {
		/* Bakelite: nearly black, faintly marbled, with a hard shell. */
		double t = room_fbm(w.x * 9.0, w.z * 9.0 + w.y * 4.0);
		m->albedo = vmix(v3(0.036, 0.030, 0.028), v3(0.082, 0.062, 0.050), t * 0.8);
		/* Bakelite comes out of the mould with a shine on it and loses it
		 * to forty years of sleeves and dusters. What is left is a dull
		 * shell with the arrises rubbed back to brown. */
		double sc = scratch_at(w.x, w.z, 0.26, 6.0, 0.006, 31.0);
		m->albedo = vmix(m->albedo, vmul(m->albedo, 2.10), clampf(sc, 0.0, 1.0) * 0.30);
		m->rough = 0.26 + t * 0.10 + sc * 0.25;
		m->coat = 0.34;
		m->coat_rough = 0.185;
		m->bump_freq = 140.0;
		m->bump_amp = 0.020;
		m->dust = 0.9;
		m->edge = 2.3;
		break;
	}
	case MID_GADGET_LEATHER: {
		double t = room_fbm(w.x * 26.0, w.z * 26.0);
		double cell = pow(room_noise(w.x * 52.0, w.z * 52.0), 2.2);
		m->albedo = vmix(v3(0.128, 0.060, 0.034), v3(0.226, 0.118, 0.066), t);
		m->albedo = vmul(m->albedo, 0.90 + cell * 0.22);
		/* Leather does not wear evenly: it goes pale and smooth where a
		 * hand goes and stays dark in the grain where one does not. */
		double rub = clampf(0.25 + room_touch(w) * 1.4, 0.0, 1.0);
		double bald = clampf(room_fbm2(w.x * 1.4 + 6.0, w.z * 1.4) * 1.5 - 0.30, 0.0, 1.0)
			* rub;
		m->albedo = vmix(m->albedo, vmul(m->albedo, 1.62), bald * 0.42);
		m->rough = 0.54 + t * 0.20 - bald * 0.16;
		m->coat = 0.30 + bald * 0.20;
		m->coat_rough = 0.28 - bald * 0.10;
		m->bump_freq = 60.0;
		m->bump_amp = 0.090 * (1.0 - bald * 0.45);
		m->dust = 0.9;
		m->edge = 2.2;
		break;
	}
	case MID_GADGET_GLASS:
		/* The face itself is painted by cairo and composited on top; the
		 * scene only needs the glass over it. */
		m->albedo = v3(0.030, 0.030, 0.032);
		m->rough = 0.035;
		m->coat = 1.0;
		m->coat_rough = 0.030;
		m->dust = 0.25;
		break;
	case MID_GADGET_DIAL:
		m->albedo = v3(0.060, 0.058, 0.054);
		m->rough = 0.10;
		m->coat = 1.0;
		m->coat_rough = 0.045;
		m->dust = 0.3;
		break;
	case MID_SHELF_CASE:
		/* obviously the same timber as the desk, waxed rather than
		 * lacquered, with the grain running up the stiles */
		walnut_at(w, w.y, w.x, m, 0.0, 0.55);
		m->albedo = vmul(m->albedo, 0.86);
		m->coat *= 0.66;
		break;
	case MID_SHELF_BOARD:
		/* eight binders dragged on and off these for years */
		walnut_at(w, w.x, w.z, m, 6.2, 0.70);
		m->albedo = vmul(m->albedo, 0.80);
		m->coat *= 0.48;
		break;
	case MID_SHELF_BACK: {
		double t = room_fbm(w.x * 3.0, w.y * 3.0);
		m->albedo = vmul(v3(0.086, 0.052, 0.032), 0.86 + t * 0.30);
		m->rough = 0.78;
		m->bump_freq = 34.0;
		m->bump_amp = 0.040;
		m->dust = 0.6;
		break;
	}
	case MID_BINDER_A: {
		/* oxblood calf, rubbed along the head and the fore edge */
		double t = room_fbm(w.x * 22.0, w.y * 22.0);
		double cell = pow(room_noise(w.x * 48.0, w.y * 48.0), 2.4);
		m->albedo = vmix(v3(0.176, 0.048, 0.042), v3(0.286, 0.098, 0.074), t);
		m->albedo = vmul(m->albedo, 0.90 + cell * 0.20);
		/* The spine has been facing the room for years and the head takes
		 * the worst of it. Dye goes out of calf unevenly, so it goes
		 * patchy rather than paler all over. */
		double head = smoothstepf(RM_BN_FOOT + 3.6, RM_BN_FOOT + 7.4, w.y);
		double fade = clampf(head * (0.24 + room_fbm2(w.x * 1.9 + 3.0, w.y * 0.8) * 1.05),
			0.0, 1.0);
		m->albedo = vmix(m->albedo, v3(0.208, 0.132, 0.112), fade * 0.30);
		/* and the marks of being pulled out by the head, every time */
		double sc = scratch_at(w.y, w.x, 0.50, 5.0, 0.011, 4.0) * 0.85
			+ scratch_at(w.x, w.y, 0.28, 4.0, 0.008, 9.0) * 0.60;
		m->albedo = vmix(m->albedo, vmul(m->albedo, 1.80), clampf(sc, 0.0, 1.0) * 0.32);
		m->rough = 0.50 + t * 0.22 + fade * 0.16;
		m->coat = 0.26 * (1.0 - fade * 0.65);
		m->coat_rough = 0.30 + fade * 0.22;
		m->bump_freq = 56.0;
		m->bump_amp = 0.085;
		m->dust = 1.0;
		/* the corners of a box file are the most rubbed thing in the room */
		m->edge = 2.6;
		break;
	}
	case MID_BINDER_B: {
		/* dark green buckram: a woven cloth, so a coarser tooth and no shine */
		double t = room_fbm(w.x * 26.0, w.y * 26.0);
		double weave = 0.5 + 0.5 * sin(w.x * 120.0) * sin(w.y * 120.0);
		m->albedo = vmix(v3(0.044, 0.082, 0.056), v3(0.086, 0.140, 0.092), t);
		m->albedo = vmul(m->albedo, 0.92 + weave * 0.14);
		/* Cloth fades further than leather does, and it goes grey doing
		 * it, and it collects the dust of the room in its weave. */
		double head = smoothstepf(RM_BN_FOOT + 3.6, RM_BN_FOOT + 7.4, w.y);
		double fade = clampf(head * (0.28 + room_fbm2(w.x * 1.7 + 8.0, w.y * 0.8) * 1.0),
			0.0, 1.0);
		m->albedo = vmix(m->albedo, v3(0.108, 0.118, 0.098), fade * 0.34);
		double sc = scratch_at(w.y, w.x, 0.46, 5.0, 0.010, 14.0) * 0.8;
		m->albedo = vmix(m->albedo, vmul(m->albedo, 1.55), clampf(sc, 0.0, 1.0) * 0.30);
		m->rough = 0.82 + fade * 0.10;
		m->bump_freq = 78.0;
		m->bump_amp = 0.070;
		m->dust = 1.0;
		m->edge = 2.4;
		break;
	}
	case MID_BINDER_FAV: {
		/* the favourites: mustard buckram, the same cloth as the green but
		 * newer to the shelf, so the head has gone less far towards grey */
		double t = room_fbm(w.x * 26.0, w.y * 26.0);
		double weave = 0.5 + 0.5 * sin(w.x * 120.0) * sin(w.y * 120.0);
		m->albedo = vmix(v3(0.300, 0.156, 0.022), v3(0.420, 0.220, 0.036), t);
		m->albedo = vmul(m->albedo, 0.92 + weave * 0.14);
		double head = smoothstepf(RM_BN_FOOT + 3.6, RM_BN_FOOT + 7.4, w.y);
		double fade = clampf(head * (0.20 + room_fbm2(w.x * 1.7 + 4.0, w.y * 0.8) * 0.9),
			0.0, 1.0);
		m->albedo = vmix(m->albedo, v3(0.300, 0.240, 0.160), fade * 0.30);
		double sc = scratch_at(w.y, w.x, 0.46, 5.0, 0.010, 27.0) * 0.8;
		m->albedo = vmix(m->albedo, vmul(m->albedo, 1.30), clampf(sc, 0.0, 1.0) * 0.30);
		m->rough = 0.82 + fade * 0.10;
		m->bump_freq = 78.0;
		m->bump_amp = 0.070;
		m->dust = 1.0;
		m->edge = 2.4;
		break;
	}
	case MID_BOOK_LEATHER: {
		/* Navy morocco: the grain pebbled, polished by the hands that took
		 * it down, the head gone a little grey where the light falls. */
		double t = room_fbm(w.x * 30.0, w.y * 30.0);
		double peb = pow(room_noise(w.x * 70.0, w.y * 70.0), 1.8);
		m->albedo = vmix(v3(0.030, 0.042, 0.086), v3(0.058, 0.078, 0.150), t);
		m->albedo = vmul(m->albedo, 0.86 + peb * 0.26);
		double head = smoothstepf(RM_BN_FOOT + 5.0, RM_BN_FOOT + 9.0, w.y);
		double fade = clampf(head * (0.20 + room_fbm2(w.x * 2.3 + 5.0, w.y * 0.9) * 0.9),
			0.0, 1.0);
		m->albedo = vmix(m->albedo, v3(0.090, 0.098, 0.130), fade * 0.30);
		double sc = scratch_at(w.y, w.x, 0.40, 5.0, 0.009, 21.0) * 0.7;
		m->albedo = vmix(m->albedo, vmul(m->albedo, 1.70), clampf(sc, 0.0, 1.0) * 0.28);
		m->rough = 0.44 + t * 0.18 + fade * 0.12;
		m->coat = 0.34 * (1.0 - fade * 0.5);
		m->coat_rough = 0.26 + fade * 0.20;
		m->bump_freq = 90.0;
		m->bump_amp = 0.070;
		m->dust = 1.0;
		m->edge = 2.2;
		break;
	}
	case MID_BOOK_GILT: {
		/* the head of the leaves, gilt: gold laid on and burnished flat,
		 * the leaves showing through it as fine lines */
		double leaf = 0.5 + 0.5 * sin(w.x * 260.0);
		double tarn = clampf(room_fbm2(w.x * 6.0 + 2.0, w.z * 6.0) * 1.3 - 0.30, 0.0, 1.0);
		m->albedo = vmix(v3(0.900, 0.700, 0.300), v3(0.560, 0.420, 0.190), tarn * 0.45);
		m->albedo = vmul(m->albedo, 0.88 + leaf * 0.16);
		m->rough = 0.22 + tarn * 0.20 + leaf * 0.05;
		m->metal = 1.0;
		m->dust = 0.6;
		break;
	}
	case MID_BINDER_LABEL: {
		/* The card in the pocket, which cairo prints over in the frame.
		 * It was white once. Paper goes over first at the edges and then
		 * in blotches, and it picks up foxing wherever a spore landed. */
		double fib = room_fbm(w.x * 44.0, w.y * 44.0);
		double age = clampf(room_fbm2(w.x * 2.1 + 17.0, w.y * 2.1) * 1.5 - 0.22, 0.0, 1.0);
		double fox = nick_at(w.x, w.y, 9.0, 11.0);
		m->albedo = vmul(v3(0.762, 0.716, 0.612), 0.94 + fib * 0.12);
		m->albedo = vmix(m->albedo, v3(0.560, 0.484, 0.348), age * 0.42);
		m->albedo = vmix(m->albedo, v3(0.402, 0.300, 0.196), fox * 0.55);
		m->rough = 0.74 + age * 0.12;
		m->bump_freq = 100.0;
		m->bump_amp = 0.032;
		m->dust = 1.0;
		break;
	}
	case MID_CC_STEEL:
	case MID_CC_PANEL: {
		/* Crinkle-finish painted steel, the black that lab gear and
		 * aircraft panels were always finished in: a hard shell over a
		 * surface that is not quite flat. */
		double t = room_fbm(w.x * 30.0, (w.y + w.z) * 30.0);
		double crin = pow(room_noise(w.x * 90.0, (w.y - w.z) * 90.0), 1.6);
		m->albedo = vmix(v3(0.026, 0.027, 0.030), v3(0.062, 0.063, 0.069), t);
		m->albedo = vmul(m->albedo, 0.88 + crin * 0.26);
		/* Crinkle is a texture you put on a panel so it will NOT shine:
		 * a gloss coat over it turned the whole face into a grey mirror of
		 * the room, which is how black paint came out looking like bare
		 * aluminium. */
		m->rough = (id == MID_CC_PANEL) ? 0.78 : 0.68;
		m->coat = 0.12;
		m->coat_rough = 0.40;
		m->bump_freq = 170.0;
		m->bump_amp = 0.090;
		m->dust = 0.65;
		break;
	}
	case MID_CC_BRASS: {
		/* Instrument brass: turned, lacquered once, handled since. The set
		 * stands at the back of the desk where a sleeve never goes, so its
		 * fittings have gone further over than the drawer pull has -- but
		 * the bezels somebody leans in to read stay bright. */
		double brush = room_noise(w.x * 130.0, (w.y + w.z) * 22.0);
		double tarn = clampf(room_fbm2(w.x * 4.2 + 9.0, (w.y + w.z) * 4.2) * 1.45 - 0.22,
			0.0, 1.0);
		double lac = smoothstepf(1.4, 4.6, w.y);   /* higher up, less handled */
		tarn = clampf(tarn * (0.55 + lac * 0.70), 0.0, 1.0);
		m->albedo = vmix(v3(0.800, 0.620, 0.266), v3(0.352, 0.286, 0.150), tarn * 0.62);
		m->albedo = vmul(m->albedo, 0.90 + brush * 0.18);
		double sc = scratch_at(w.x, w.y * 0.8 + w.z * 0.8, 0.22, 6.0, 0.0045, 5.0);
		m->albedo = vmix(m->albedo, v3(0.916, 0.812, 0.570), sc * 0.30);
		/* turned and lacquered, not polished: a mirror finish on anything
		 * this size just went white under the lamp */
		m->rough = clampf(0.245 + brush * 0.13 + tarn * 0.30 - sc * 0.06, 0.05, 1.0);
		m->metal = 1.0;
		m->dust = 0.35;
		break;
	}
	case MID_CC_BAKELITE: {
		/* Moulded phenolic: nearly black, faintly brown where it is thin,
		 * with the mould's own grain in it. */
		double t = room_fbm(w.x * 44.0, (w.y + w.z) * 44.0);
		m->albedo = vmix(v3(0.019, 0.015, 0.013), v3(0.047, 0.034, 0.026), t * 0.9);
		/* Moulded, not polished. A gloss on it put the desk lamp across
		 * the whole flat top of the knob, and black phenolic came out the
		 * colour of milky coffee. */
		m->rough = 0.74 + t * 0.10;
		m->coat = 0.09;
		m->coat_rough = 0.38;
		m->bump_freq = 190.0;
		m->bump_amp = 0.030;
		m->dust = 0.7;
		break;
	}
	case MID_CC_GLASS:
		/* The face is drawn by cairo behind it; this is only the window. */
		m->albedo = v3(0.018, 0.019, 0.021);
		m->rough = 0.028;
		m->coat = 1.0;
		m->coat_rough = 0.022;
		m->dust = 0.30;
		break;
	case MID_CC_CHEEK:
		/* you take hold of the set by its cheeks to move it */
		walnut_at(w, w.z, w.y, m, 0.0, 0.75);
		m->albedo = vmul(m->albedo, 0.92);
		m->coat *= 0.84;
		break;
	case MID_POST_RED: {
		/* Cast iron under coach enamel, repainted more than once and not
		 * always well. Enamel does not wear thin the way a lacquer does:
		 * it chips, and what is under it is iron. */
		double t = room_fbm(w.x * 6.0, (w.y + w.z) * 6.0);
		double orange = room_fbm2(w.x * 1.3 + 12.0, w.y * 1.3);
		m->albedo = vmix(v3(0.232, 0.024, 0.016), v3(0.318, 0.048, 0.026), t * 0.85);
		m->albedo = vmix(m->albedo, v3(0.186, 0.038, 0.024), orange * 0.32);
		double chip = nick_at(w.x, w.y + w.z, 2.6, 41.0);
		m->albedo = vmix(m->albedo, v3(0.088, 0.064, 0.052), chip * 0.85);
		double sc = scratch_at(w.y, w.x, 0.34, 6.0, 0.007, 27.0);
		m->albedo = vmix(m->albedo, vmul(m->albedo, 1.70), clampf(sc, 0.0, 1.0) * 0.30);
		m->rough = 0.34 + t * 0.14 + chip * 0.42;
		m->coat = 0.46 * (1.0 - chip * 0.9);
		m->coat_rough = 0.12 + t * 0.09 + chip * 0.30;
		/* orange peel: coach enamel was brushed on, and it shows */
		m->bump_freq = 34.0;
		m->bump_amp = 0.055;
		m->dust = 1.0;
		/* every arris on it has been knocked at some point */
		m->edge = 2.4;
		break;
	}
	case MID_POST_DARK: {
		double t = room_fbm(w.x * 14.0, (w.y + w.z) * 14.0);
		m->albedo = vmix(v3(0.036, 0.026, 0.022), v3(0.074, 0.054, 0.044), t);
		m->rough = 0.62 + t * 0.14;
		m->coat = 0.14;
		m->coat_rough = 0.34;
		m->bump_freq = 120.0;
		m->bump_amp = 0.040;
		m->dust = 1.0;
		m->edge = 2.0;
		break;
	}
	case MID_CC_LENS_AMBER:
	case MID_CC_LENS_GREEN:
	case MID_CC_LENS_BLUE:
	case MID_CC_LENS_RED:
	case MID_CC_LENS_WHITE: {
		/* Cast glass lenses. They are lit by the compositor over the top,
		 * so the bake only has to make them read as glass with a filament
		 * sitting cold behind them. The white one is opal: milk glass. */
		vec3 tint = id == MID_CC_LENS_AMBER ? v3(0.620, 0.310, 0.060)
			: id == MID_CC_LENS_GREEN ? v3(0.110, 0.420, 0.160)
			: id == MID_CC_LENS_RED ? v3(0.640, 0.098, 0.062)
			: id == MID_CC_LENS_WHITE ? v3(0.720, 0.700, 0.640)
			: v3(0.110, 0.230, 0.560);
		m->albedo = vmul(tint, 0.34);
		m->rough = 0.075;
		m->coat = 1.0;
		m->coat_rough = 0.035;
		m->emit = vmul(tint, 0.10);
		break;
	}
	case MID_GADGET_PAPER: {
		double fib = room_fbm(w.x * 40.0, w.z * 40.0);
		double age = clampf(room_fbm2(w.x * 1.7 + 5.0, w.y * 1.7) * 1.5 - 0.25, 0.0, 1.0);
		double fox = nick_at(w.x, w.y, 7.0, 23.0);
		m->albedo = vmul(v3(0.742, 0.706, 0.618), 0.94 + fib * 0.12);
		m->albedo = vmix(m->albedo, v3(0.590, 0.520, 0.382), age * 0.38);
		m->albedo = vmix(m->albedo, v3(0.430, 0.330, 0.216), fox * 0.50);
		m->rough = 0.76;
		m->bump_freq = 90.0;
		m->bump_amp = 0.030;
		m->dust = 1.0;
		break;
	}
	default:
		break;
	}
}

/* Bend the normal with a procedural height field. Without this every
 * surface is geometrically perfect, which is the single loudest tell that
 * something was rendered rather than photographed. */
static vec3 perturb_normal(vec3 n, vec3 p, double freq, double amp)
{
	if (amp <= 0.0 || freq <= 0.0) {
		return n;
	}
	const double e = 0.35 / freq;
	double b0 = room_fbm(p.x * freq, p.y * freq + p.z * freq * 0.63);
	double bx = room_fbm((p.x + e) * freq, p.y * freq + p.z * freq * 0.63);
	double by = room_fbm(p.x * freq, (p.y + e) * freq + p.z * freq * 0.63);
	double bz = room_fbm(p.x * freq, p.y * freq + (p.z + e) * freq * 0.63);
	vec3 g = v3((bx - b0) / e, (by - b0) / e, (bz - b0) / e);
	g = vsub(g, vmul(n, vdot(g, n)));
	double gl = vlen(g);
	if (gl < 1e-9) {
		return n;
	}
	return vnorm(vsub(n, vmul(g, amp / fmax(gl, 1.0))));
}

static vec3 reflect3(vec3 i, vec3 n)
{
	return vsub(i, vmul(n, 2.0 * vdot(i, n)));
}

static vec3 shade_surface(vec3 p, vec3 n_geo, vec3 rd, int kind, int id,
	double key_boost, int depth);

/* Ambient specular: a real reflection ray when the surface is smooth enough
 * to show one, otherwise the prefiltered environment plus the lamp. */
static vec3 spec_ambient(vec3 p, vec3 n, vec3 v, double rough, int kind,
	double key_boost, double shadow, const struct key_light *key, int depth)
{
	vec3 r = reflect3(vmul(v, -1.0), n);
	vec3 sky = vadd(env_prefiltered(r, rough),
		vmul(env_lamp(r, rough, key), shadow * key_boost));
	if (depth > 0 || rough > 0.38) {
		return sky;
	}
	vec3 ro = vadd(p, vmul(n, 0.004));
	double tmin, tmax;
	if (!bound_hit(ro, r, kind, &tmin, &tmax)) {
		return sky;
	}
	int big = (kind == ROOM_KIND || kind == DRAWER_KIND);
	if (big) {
		tmax = 30.0;
	}
	double t = fmax(tmin, big ? 0.02 : 0.004);
	for (int i = 0; i < 40 && t < tmax; i++) {
		hit3 h = scene_map(vadd(ro, vmul(r, t)), kind);
		if (h.d < (big ? 0.004 : 0.0012)) {
			if (h.id <= 0) {
				break;
			}
			vec3 hp = vadd(ro, vmul(r, t));
			vec3 hn = scene_normal(hp, kind);
			vec3 c = shade_surface(hp, hn, r, kind, h.id, key_boost, depth + 1);
			/* the object blocks the lamp along this ray, so only the bounce */
			return vmix(c, sky, clampf(rough * 2.4, 0.0, 1.0));
		}
		t += fmax(h.d, big ? 0.004 : 0.0012);
	}
	return sky;
}

static vec3 shade_surface(vec3 p, vec3 n_geo, vec3 rd, int kind, int id,
	double key_boost, int depth)
{
	struct mtl m;
	icon_material(kind, id, p, &m);

	vec3 n = perturb_normal(n_geo, scene_local(p, kind, id), m.bump_freq, m.bump_amp);

	/* Convex edges wear: paint thins and brightens, metal polishes, and the
	 * roughness drops because the burr has been rubbed off. */
	if (m.dust > 0.0 || m.metal > 0.5) {
		double curv = scene_curvature(p, kind);
		double wear = clampf(curv * 1.4 * m.edge, 0.0, 1.0);
		if (m.metal > 0.5) {
			m.rough = fmax(0.045, m.rough - wear * 0.11);
			m.albedo = vmix(m.albedo, v3(0.95, 0.80, 0.42), wear * 0.45);
		} else {
			m.albedo = vmix(m.albedo, vmul(m.albedo, 1.30), wear * 0.55);
			m.rough = clampf(m.rough - wear * 0.14, 0.05, 1.0);
		}
		/* and dust settles in the concave parts that face up */
		double up = clampf(n.y, 0.0, 1.0);
		double settle = m.dust * up * up * clampf(-curv * 0.8 + 0.42, 0.0, 1.0);
		m.albedo = vmix(m.albedo, v3(0.42, 0.40, 0.37), settle * 0.16);
		m.rough = clampf(m.rough + settle * 0.22, 0.03, 1.0);
	}

	vec3 v = vnorm(vmul(rd, -1.0));
	struct key_light kl = key_at(p, kind);
	vec3 l = kl.dir;
	double nol = fmax(vdot(n, l), 0.0);
	double nov = fmax(vdot(n, v), 1e-4);

	double ao = (depth > 0) ? 0.85 : scene_ao(p, n, kind);
	double sh = 1.0;
	if (nol > 0.0 && !g_rf_unshadowed) {
		double bias = (kind == ROOM_KIND || kind == DRAWER_KIND) ? 0.040 : 0.010;
		sh = (depth > 0) ? 1.0 : scene_shadow(vadd(p, vmul(n_geo, bias)), l, kind, kl.ang);
	}

	vec3 f0 = vmix(v3(0.04, 0.04, 0.04), m.albedo, m.metal);
	vec3 diff_albedo = vmul(m.albedo, 1.0 - m.metal);
	vec3 key = vmul(LIT_KEY_COL, kl.irr * key_boost);

	/* direct lamp, diffuse only: the specular half of the lamp arrives
	 * through env_lamp inside spec_ambient, so it is not counted twice */
	vec3 col = vmul(vmul3(diff_albedo, key), nol * sh / M_PI);

	/* ambient diffuse from the room */
	col = vadd(col, vmul(vmul3(diff_albedo, sh_irradiance(n)), ao / M_PI));

	/* and the pair under the bookcase's upper board */
	if (kind == ROOM_KIND) {
		col = vadd(col, vmul(vmul3(diff_albedo, shelf_lamp_at(p, n)), ao / M_PI));
	}

	/* ambient specular, split sum */
	double ea, eb;
	env_brdf(nov, m.rough, &ea, &eb);
	vec3 dfg = vadd(vmul(f0, ea), v3(eb, eb, eb));
	/* specular occlusion: a rough cavity should not mirror the whole room */
	double so = clampf(pow(nov + ao, exp2(-16.0 * m.rough - 1.0)) - 1.0 + ao, 0.0, 1.0);
	vec3 refl = spec_ambient(p, n, v, m.rough, kind, key_boost, sh, &kl, depth);
	col = vadd(col, vmul(vmul3(refl, dfg), so));

	/* single scattering GGX loses energy on rough metal; put it back */
	if (m.metal > 0.05) {
		vec3 comp = vmul3(f0, vmul(vsub(v3(1, 1, 1), v3(ea + eb, ea + eb, ea + eb)),
			1.0 / fmax(ea + eb, 1e-3)));
		col = vadd(col, vmul(vmul3(vmul3(refl, dfg), comp), so * m.metal));
	}

	/* clear lacquer on top of the base layer */
	if (m.coat > 0.0) {
		vec3 cf0 = v3(0.04, 0.04, 0.04);
		double cft = pow(1.0 - nov, 5.0);
		double cfres = 0.04 + 0.96 * cft;
		col = vmul(col, 1.0 - cfres * m.coat);
		double ca, cb;
		env_brdf(nov, m.coat_rough, &ca, &cb);
		vec3 cdfg = vadd(vmul(cf0, ca), v3(cb, cb, cb));
		vec3 crefl = spec_ambient(p, n, v, m.coat_rough, kind, key_boost, sh, &kl, depth);
		col = vadd(col, vmul(vmul3(crefl, cdfg), m.coat * so));
	}

	col = vadd(col, m.emit);
	return col;
}
