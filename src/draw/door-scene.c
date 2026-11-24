/* Part of draw.c: the front door: where things are and what a ray meets.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

static vec3 lk_mix3(vec3 night, vec3 dusk, vec3 day, double tw, double dd)
{
	return vmix(vmix(night, dusk, tw), day, dd);
}

/* how much of the sky the weather covers */
static double cloud_of(int wx)
{
	return wx == DESK_WX_CLEAR ? 0.0 : wx == DESK_WX_CLOUDS ? 0.6 : 1.0;
}

/* the rig for a mood (see struct lk_rig) */
static void lk_rig_for(const struct desk_door_mood *m, struct lk_rig *r)
{
	memset(r, 0, sizeof *r);
	r->porch = r->lantern = r->glass = 1.0;
	r->moon_col = LK_MOON_COL;
	r->amb_lo = v3(0.018, 0.015, 0.012);
	r->amb_hi = v3(0.026, 0.032, 0.050);
	r->sky_lo = v3(0.022, 0.028, 0.046);
	r->sky_hi = v3(0.006, 0.009, 0.020);
	r->gnd_hi = v3(0.050, 0.040, 0.030);
	r->gnd_lo = v3(0.020, 0.016, 0.012);
	r->exposure = LK_EXPOSURE;
	r->sun_soft = 0.0093;
	r->key = 1;
	if (!m || !m->set) {
		return;
	}
	r->decor = m->decor;
	r->year = m->year;
	/* the sun to a degree and a half, and six degrees round: the picture
	 * is traced again only as often as that changes */
	double el = round(m->sun_el / 1.5) * 1.5, az = round(m->sun_az / 6.0) * 6.0;
	int wx = m->weather < 0 || m->weather >= DESK_WX_N ? DESK_WX_CLEAR : m->weather;
	double heavy = clampf(m->heavy, 0.0, 1.0);
	bool dull = wx >= DESK_WX_OVERCAST;
	bool wet_sky = wx == DESK_WX_RAIN || wx == DESK_WX_STORM;
	/* how far through the evening (tw: night to dusk; dd: dusk to day) */
	double tw = smoothstepf(-13.0, -3.0, el), dd = smoothstepf(-3.0, 12.0, el);
	double day = smoothstepf(-7.0, 9.0, el);
	/* the sun low: everything it reaches warm */
	double low = smoothstepf(-5.0, 1.0, el) * (1.0 - smoothstepf(4.0, 22.0, el));
	/* the lamps: on a sensor, which a dull day keeps on longer */
	double on = 1.0 - smoothstepf(dull ? 3.0 : -2.5, dull ? 14.0 : 5.0, el);
	r->porch = r->lantern = on;
	r->glass = 1.0 - 0.50 * day;
	/* the sun, through whatever is in the way */
	double through = wx == DESK_WX_CLEAR ? 1.0 : wx == DESK_WX_CLOUDS ? 0.55 : 0.0;
	double up = smoothstepf(-1.0, 2.5, el) * through;
	if (up > 0.0) {
		double ea = el * M_PI / 180.0, aa = az * M_PI / 180.0;
		r->sun = vnorm(v3(sin(aa) * cos(ea), sin(fmax(ea, 0.03)), -cos(aa) * cos(ea)));
		vec3 c = vmix(v3(1.0, 0.40, 0.14), v3(1.0, 0.91, 0.80), smoothstepf(1.0, 30.0, el));
		r->sun_col = vmul(c, 2.9 * up * (0.30 + 0.70 * smoothstepf(0.0, 25.0, el)));
		r->sun_soft = wx == DESK_WX_CLOUDS ? 0.035 : 0.0093;
	}
	double moon = 0.35 + 0.65 * clampf(m->moon, 0.0, 1.0);
	r->moon_col = vmul(LK_MOON_COL, moon * (1.0 - day) * (1.0 - cloud_of(wx)));
	/* the light all round: the night, the blue of the evening, the day --
	 * a grey one, under cloud; darker for rain, lighter off snow */
	vec3 day_hi = v3(0.36, 0.44, 0.60), day_lo = v3(0.22, 0.19, 0.15);
	if (dull) {
		double k = wx == DESK_WX_STORM ? 0.42 : wx == DESK_WX_RAIN ? 0.68 - 0.18 * heavy
			: wx == DESK_WX_SNOW ? 0.92 : 0.85;
		day_hi = vmul(v3(0.48, 0.50, 0.54), k);
		day_lo = vmul(wx == DESK_WX_SNOW ? v3(0.46, 0.48, 0.52) : v3(0.27, 0.26, 0.25), k);
	} else if (wx == DESK_WX_CLOUDS) {
		day_hi = v3(0.42, 0.46, 0.55);
		day_lo = v3(0.24, 0.21, 0.18);
	}
	vec3 dusk_hi = v3(0.090, 0.100, 0.190), dusk_lo = v3(0.060, 0.045, 0.040);
	vec3 night_hi = r->amb_hi, night_lo = r->amb_lo;
	if (dull) {
		/* low cloud over a town at night: the street lights on it */
		night_hi = v3(0.036, 0.032, 0.032);
		dusk_hi = v3(0.075, 0.072, 0.085);
	}
	r->amb_hi = lk_mix3(night_hi, dusk_hi, day_hi, tw, dd);
	r->amb_lo = lk_mix3(night_lo, dusk_lo, day_lo, tw, dd);
	r->amb_lo = vadd(r->amb_lo, vmul(v3(0.10, 0.045, 0.015), low * through));
	/* the sky in the brass and the glass */
	vec3 dsky_hi = v3(0.24, 0.40, 0.78), dsky_lo = v3(0.70, 0.76, 0.86);
	vec3 dgnd_hi = v3(0.24, 0.22, 0.19), dgnd_lo = v3(0.12, 0.11, 0.10);
	if (dull) {
		vec3 g = vmul(v3(0.62, 0.64, 0.68), wx == DESK_WX_STORM ? 0.40
			: wet_sky ? 0.70 : 0.95);
		dsky_hi = vmul(g, 0.85);
		dsky_lo = g;
		if (wx == DESK_WX_SNOW) {
			dgnd_hi = v3(0.70, 0.72, 0.76);
			dgnd_lo = v3(0.50, 0.52, 0.56);
		}
	}
	vec3 esky_hi = v3(0.10, 0.11, 0.24), esky_lo = v3(0.85, 0.42, 0.20);
	if (dull) {
		esky_hi = v3(0.08, 0.08, 0.10);
		esky_lo = v3(0.16, 0.13, 0.12);
	}
	r->sky_hi = lk_mix3(r->sky_hi, esky_hi, dsky_hi, tw, dd);
	r->sky_lo = lk_mix3(r->sky_lo, esky_lo, dsky_lo, tw, dd);
	r->gnd_hi = lk_mix3(r->gnd_hi, vmul(r->gnd_hi, 2.0), dgnd_hi, tw, dd);
	r->gnd_lo = lk_mix3(r->gnd_lo, vmul(r->gnd_lo, 2.0), dgnd_lo, tw, dd);
	/* on things */
	r->wet = wet_sky ? 0.55 + 0.45 * heavy : wx == DESK_WX_FOG ? 0.20 : 0.0;
	r->snow = wx == DESK_WX_SNOW ? 0.40 + 0.60 * heavy : 0.0;
	r->haze_k = wx == DESK_WX_FOG ? 0.30 : wx == DESK_WX_STORM ? 0.16
		: wx == DESK_WX_RAIN ? 0.05 + 0.10 * heavy : wx == DESK_WX_SNOW ? 0.07 + 0.14 * heavy
		: 0.0;
	r->haze = wx == DESK_WX_FOG ? vmul(vadd(r->amb_hi, r->amb_lo), 0.75) : vmul(r->amb_hi, 0.75);
	r->exposure = LK_EXPOSURE * (1.0 - 0.18 * day)
		* (1.0 - 0.18 * day * (wx == DESK_WX_SNOW || wx == DESK_WX_FOG));
	/* The key changes only where the rig does: below 13 degrees under the
	 * horizon the night is the same night (tw, dd, day, low, on and up are
	 * all at their ends), and which way the sun is counts only while it
	 * is out -- or the door, and its films, were traced again every few
	 * minutes all night for nothing. */
	unsigned k = 2166136261u;
	int q[8] = { (int)lround(fmax(el, -13.5) / 1.5), up > 0.0 ? (int)lround(az / 6.0) : 0, wx,
		(int)lround(heavy * 4.0), (int)m->decor, 0, (int)lround(moon * 4.0), m->year };
	for (int i = 0; i < 8; i++) {
		k = (k ^ (unsigned)q[i]) * 16777619u;
	}
	r->key = k | 2u;
}

static double lk_sq(double v)
{
	return v * v;
}

static double lk_smin(double a, double b, double k)
{
	double h = clampf(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
	return b + (a - b) * h - k * h * (1.0 - h);
}

/* ---- where things are ---- */

static vec3 lk_to_door(const struct lk_scene *sc, vec3 p)
{
	vec3 q = v3(p.x - LK_HX, p.y, p.z - LK_HZ);
	return v3(q.x * sc->cd - q.z * sc->sd, q.y, q.x * sc->sd + q.z * sc->cd);
}

static vec3 lk_door_dir_in(const struct lk_scene *sc, vec3 d)
{
	return v3(d.x * sc->cd - d.z * sc->sd, d.y, d.x * sc->sd + d.z * sc->cd);
}

static vec3 lk_door_dir_out(const struct lk_scene *sc, vec3 d)
{
	return v3(d.x * sc->cd + d.z * sc->sd, d.y, -d.x * sc->sd + d.z * sc->cd);
}

static vec3 lk_from_door(const struct lk_scene *sc, vec3 q)
{
	vec3 p = lk_door_dir_out(sc, q);
	return v3(p.x + LK_HX, p.y, p.z + LK_HZ);
}

/* the board's own frame: across it, up it, out of it */
static vec3 lk_to_sign(vec3 q)
{
	const double c = cos(LK_SIGN_TILT), s = sin(LK_SIGN_TILT);
	vec3 o = v3(q.x - LK_SIGN_X, q.y - LK_SIGN_Y, q.z - LK_DT - 0.0004);
	return v3(o.x, o.y * c + o.z * s, -o.y * s + o.z * c);
}

static vec3 lk_sign_dir_in(vec3 d)
{
	const double c = cos(LK_SIGN_TILT), s = sin(LK_SIGN_TILT);
	return v3(d.x, d.y * c + d.z * s, -d.y * s + d.z * c);
}

static vec3 lk_sign_dir_out(vec3 d)
{
	const double c = cos(LK_SIGN_TILT), s = sin(LK_SIGN_TILT);
	return v3(d.x, d.y * c - d.z * s, d.y * s + d.z * c);
}

static vec3 lk_from_sign(vec3 s)
{
	vec3 d = lk_sign_dir_out(s);
	return v3(d.x + LK_SIGN_X, d.y + LK_SIGN_Y, d.z + LK_DT + 0.0004);
}

/* Where the ray first comes to the box from outside it, and by which face
 * (axis * 2, plus 1 for the far side of the axis). */
static bool lk_box(vec3 ro, vec3 rd, vec3 lo, vec3 hi, double tmax, double *t, int *face)
{
	const double o[3] = { ro.x, ro.y, ro.z }, d[3] = { rd.x, rd.y, rd.z };
	const double a[3] = { lo.x, lo.y, lo.z }, b[3] = { hi.x, hi.y, hi.z };
	double t0 = -1e30, t1 = tmax;
	int f = -1;
	for (int i = 0; i < 3; i++) {
		if (fabs(d[i]) < 1e-12) {
			if (o[i] < a[i] || o[i] > b[i]) {
				return false;
			}
			continue;
		}
		double inv = 1.0 / d[i];
		double ta = (a[i] - o[i]) * inv, tb = (b[i] - o[i]) * inv;
		int fa = i * 2, fb = i * 2 + 1;
		if (ta > tb) {
			double s = ta;
			ta = tb;
			tb = s;
			int g = fa;
			fa = fb;
			fb = g;
		}
		if (ta > t0) {
			t0 = ta;
			f = fa;
		}
		if (tb < t1) {
			t1 = tb;
		}
		if (t0 > t1) {
			return false;
		}
	}
	if (f < 0 || t0 < 1e-7) {
		return false;
	}
	*t = t0;
	*face = f;
	return true;
}

static vec3 lk_face_n(int face)
{
	double s = (face & 1) ? 1.0 : -1.0;
	switch (face >> 1) {
	case 0:
		return v3(s, 0, 0);
	case 1:
		return v3(0, s, 0);
	default:
		return v3(0, 0, s);
	}
}

/* ---- the brass, as distances ---- */

/* the lock: a low rosette, the plug in it, and the key in that, turned */
static double lk_sd_lock(double key, vec3 q, int *id)
{
	vec3 p = vsub(q, v3(LK_KEY_X, LK_KEY_Y, LK_DT));
	double ros = sd_cyl_z(vsub(p, v3(0, 0, 0.0035)), 0.0270, 0.0012) - 0.0024;
	double plug = sd_cyl_z(vsub(p, v3(0, 0, 0.0074)), 0.0110, 0.0010) - 0.0007;
	double lock = fmin(ros, plug);
	vec3 k = rot_z(p, key);
	double shank = sd_round_box(vsub(k, v3(0, 0, 0.032)), v3(0.0026, 0.0062, 0.023), 0.0012);
	double collar = sd_cyl_z(vsub(k, v3(0, 0, 0.0480)), 0.0064, 0.0022) - 0.0011;
	/* the bow: a flat ring in the plane of the blade, a clover cut in it */
	vec3 b = vsub(k, v3(0, 0, 0.0830));
	double d2 = sqrt(b.y * b.y + b.z * b.z) - 0.0315;
	for (int i = 0; i < 3; i++) {
		double a = i * (2.0 * M_PI / 3.0);
		double hz = b.z - 0.0138 * cos(a), hy = b.y - 0.0138 * sin(a);
		d2 = fmax(d2, 0.0086 - sqrt(hy * hy + hz * hz));
	}
	const double ra = 0.0013, hx = 0.0028;
	double wa = d2 + ra, wb = fabs(b.x) - (hx - ra);
	double bow = fmin(fmax(wa, wb), 0.0) + sqrt(lk_sq(fmax(wa, 0.0)) + lk_sq(fmax(wb, 0.0))) - ra;
	double keyd = lk_smin(lk_smin(shank, collar, 0.002), bow, 0.003);
	*id = keyd < lock ? LK_KEY : LK_BRASS;
	return fmin(keyd, lock);
}

static double lk_sd_knob(vec3 q, int *id)
{
	vec3 p = vsub(q, v3(LK_KNOB_X, LK_KNOB_Y, LK_DT));
	double ros = sd_cyl_z(vsub(p, v3(0, 0, 0.0035)), 0.0245, 0.0012) - 0.0025;
	double neck = sd_cyl_z(vsub(p, v3(0, 0, 0.0230)), 0.0082, 0.0190);
	vec3 b = vsub(p, v3(0, 0, 0.0600));
	b.z *= 1.30;
	double ball = (vlen(b) - 0.0285) / 1.30;
	*id = LK_BRASS;
	return fmin(ros, lk_smin(neck, ball, 0.008));
}

static double lk_sd_nail(vec3 q, int *id)
{
	vec3 p = vsub(q, v3(LK_SIGN_X, LK_NAIL_Y, LK_DT));
	double head = sd_cyl_z(vsub(p, v3(0, 0, 0.0076)), 0.0042, 0.0006) - 0.0009;
	double shank = sd_cyl_z(vsub(p, v3(0, 0, 0.0036)), 0.0016, 0.0036);
	*id = LK_BRASS;
	return fmin(head, shank);
}

static double lk_occ_sd(const struct lk_scene *sc, int j, vec3 q)
{
	int id;
	switch (j) {
	case LK_OC_SIGN: {
		vec3 s = lk_to_sign(q);
		return sd_box(vsub(s, v3(0, LK_SIGN_H * 0.5, LK_SIGN_T * 0.5)),
			v3(LK_SIGN_W * 0.5, LK_SIGN_H * 0.5, LK_SIGN_T * 0.5));
	}
	case LK_OC_PLATE:
		return sd_box(vsub(q, v3((LK_PL_X0 + LK_PL_X1) * 0.5, (LK_PL_Y0 + LK_PL_Y1) * 0.5,
			LK_DT + LK_PL_T * 0.5)), v3((LK_PL_X1 - LK_PL_X0) * 0.5,
			(LK_PL_Y1 - LK_PL_Y0) * 0.5, LK_PL_T * 0.5));
	case LK_OC_LOCK:
		return lk_sd_lock(sc->key, q, &id);
	case LK_OC_KNOB:
		return lk_sd_knob(q, &id);
	default:
		return lk_sd_nail(q, &id);
	}
}

static void lk_occ_bound(int j, vec3 *c, double *r)
{
	switch (j) {
	case LK_OC_SIGN:
		*c = lk_from_sign(v3(0, LK_SIGN_H * 0.5, LK_SIGN_T * 0.5));
		*r = 0.245;
		break;
	case LK_OC_PLATE:
		*c = v3((LK_PL_X0 + LK_PL_X1) * 0.5, (LK_PL_Y0 + LK_PL_Y1) * 0.5, LK_DT + 0.003);
		*r = 0.202;
		break;
	case LK_OC_LOCK:
		*c = v3(LK_KEY_X, LK_KEY_Y, LK_DT + 0.057);
		*r = 0.072;
		break;
	case LK_OC_KNOB:
		*c = v3(LK_KNOB_X, LK_KNOB_Y, LK_DT + 0.042);
		*r = 0.056;
		break;
	default:
		*c = v3(LK_SIGN_X, LK_NAIL_Y, LK_DT + 0.005);
		*r = 0.009;
		break;
	}
}

/* ---- finding what a ray meets ---- */

struct lk_hit {
	double t;
	int id, face, sub;
	vec3 n;      /* the surface's normal, in the world */
	vec3 q;      /* the point in the frame of what it is on: the door's, the board's */
};

static vec3 lk_group_normal(const struct lk_scene *sc, int j, vec3 p)
{
	const double e = 0.00022;
	vec3 k1 = v3(1, -1, -1), k2 = v3(-1, -1, 1), k3 = v3(-1, 1, -1), k4 = v3(1, 1, 1);
	return vnorm(vadd(vadd(vmul(k1, lk_occ_sd(sc, j, vadd(p, vmul(k1, e)))),
		vmul(k2, lk_occ_sd(sc, j, vadd(p, vmul(k2, e))))),
		vadd(vmul(k3, lk_occ_sd(sc, j, vadd(p, vmul(k3, e)))),
		vmul(k4, lk_occ_sd(sc, j, vadd(p, vmul(k4, e)))))));
}

/* the brass things, by marching their distances inside their bounds */
static void lk_march(const struct lk_scene *sc, int j, vec3 o, vec3 d, struct lk_hit *h)
{
	vec3 c;
	double r;
	lk_occ_bound(j, &c, &r);
	vec3 oc = vsub(o, c);
	double b = vdot(oc, d), cc = vdot(oc, oc) - r * r;
	double disc = b * b - cc;
	if (disc <= 0.0) {
		return;
	}
	double s = sqrt(disc);
	double t = fmax(-b - s, 0.0), t1 = fmin(-b + s, h->t);
	for (int i = 0; i < 90 && t < t1; i++) {
		vec3 p = vadd(o, vmul(d, t));
		int id = LK_BRASS;
		double dist = j == LK_OC_LOCK ? lk_sd_lock(sc->key, p, &id)
			: j == LK_OC_KNOB ? lk_sd_knob(p, &id) : lk_sd_nail(p, &id);
		if (dist < 0.00010) {
			h->t = t;
			h->id = id;
			h->face = j;
			h->q = p;
			h->n = lk_door_dir_out(sc, lk_group_normal(sc, j, p));
			return;
		}
		t += dist * 0.92;
	}
}

/* ---- dressed for the day: the shapes ---- */

/* the wreath: a ring round the board's middle, standing off the door */
#define LKD_CX LK_SIGN_X
#define LKD_CY (LK_SIGN_Y + LK_SIGN_H * 0.5)
#define LKD_R 0.355
#define LKD_Z (LK_DT + 0.050)
#define LKD_KNOT_A (M_PI * 1.27)   /* the bow, or the orange: low on the left */
#define LKD_SLOTS 12               /* what is tucked in round it */

/* an ellipsoid, near enough (Quilez) */
static double lkd_ellipsoid(vec3 p, vec3 r)
{
	double k0 = vlen(v3(p.x / r.x, p.y / r.y, p.z / r.z));
	double k1 = vlen(v3(p.x / (r.x * r.x), p.y / (r.y * r.y), p.z / (r.z * r.z)));
	return k1 > 1e-12 ? k0 * (k0 - 1.0) / k1 : -fmin(r.x, fmin(r.y, r.z));
}

/* p turned by a about z, round c */
static vec3 lkd_turn(vec3 p, vec3 c, double a)
{
	vec3 d = vsub(p, c);
	double s = sin(a), k = cos(a);
	return v3(d.x * k + d.y * s, -d.x * s + d.y * k, d.z);
}

/* how far from the ring's centre line, and where on it: round the ring
 * (a, from the right, anticlockwise) and round the tube (b, 0 outward) */
static double lkd_ring(vec3 q, double *a, double *b)
{
	double x = q.x - LKD_CX, y = q.y - LKD_CY, z = q.z - LKD_Z;
	double c = sqrt(x * x + y * y) - LKD_R;
	*a = atan2(y, x);
	*b = atan2(z, c);
	return sqrt(c * c + z * z);
}

/* the same distance, when where on it is not wanted (no atan2s) */
static double lkd_ring_r(vec3 q)
{
	double x = q.x - LKD_CX, y = q.y - LKD_CY, z = q.z - LKD_Z;
	double c = sqrt(x * x + y * y) - LKD_R;
	return sqrt(c * c + z * z);
}

static vec3 lkd_ring_at(double a, double b, double r)
{
	double rr = LKD_R + r * cos(b);
	return v3(LKD_CX + cos(a) * rr, LKD_CY + sin(a) * rr, LKD_Z + r * sin(b));
}

/* value noise in three dimensions, 0..1, on the room's lattice hash */
static double lkd_hash3(int x, int y, int z)
{
	return room_hash(x + z * 7919, y - z * 1543);
}

static double lkd_noise3(vec3 p)
{
	double xi = floor(p.x), yi = floor(p.y), zi = floor(p.z);
	double xf = p.x - xi, yf = p.y - yi, zf = p.z - zi;
	double u = xf * xf * (3.0 - 2.0 * xf), v = yf * yf * (3.0 - 2.0 * yf);
	double w = zf * zf * (3.0 - 2.0 * zf);
	int x = (int)xi, y = (int)yi, z = (int)zi;
	double a = lkd_hash3(x, y, z) + (lkd_hash3(x + 1, y, z) - lkd_hash3(x, y, z)) * u;
	double b = lkd_hash3(x, y + 1, z) + (lkd_hash3(x + 1, y + 1, z) - lkd_hash3(x, y + 1, z)) * u;
	double c = lkd_hash3(x, y, z + 1) + (lkd_hash3(x + 1, y, z + 1) - lkd_hash3(x, y, z + 1)) * u;
	double d = lkd_hash3(x, y + 1, z + 1)
		+ (lkd_hash3(x + 1, y + 1, z + 1) - lkd_hash3(x, y + 1, z + 1)) * u;
	double ab = a + (b - a) * v, cd = c + (d - c) * v;
	return ab + (cd - ab) * w;
}

/* the fir's sprigs, as lumps: big ones a sprig across, and the clumps of
 * needles on them -- 0..1, about a point (metres) */
static double lkd_fir(vec3 q)
{
	return 0.62 * lkd_noise3(vmul(q, 30.0)) + 0.38 * lkd_noise3(vadd(vmul(q, 85.0), v3(5.1, 1.7, 3.3)));
}

/* g_lkd_rough: the fir only to its big lumps -- for a shadow, or how shut
 * in something is, which never see the needles' clumps. Set per call by
 * whoever asks (it is the trace's, on each worker's own stack of calls). */
static _Thread_local bool g_lkd_rough;

static double lkd_fir_for(vec3 q)
{
	return g_lkd_rough ? 0.62 * lkd_noise3(vmul(q, 30.0)) + 0.19 : lkd_fir(q);
}

/* the same, as it was round the ring (for what has no point to go by) */
static double lkd_lumps(double a, double b)
{
	return 0.55 * (0.5 + 0.5 * sin(23.0 * a + 1.9 * sin(2.0 * b + 5.0 * a)))
		+ 0.45 * (0.5 + 0.5 * sin(37.0 * a - 3.0 * b + 1.3));
}

/* the slot nearest a point round the ring, and its angle; -1 for the
 * stretch where the knot is */
static int lkd_slot(double a, double *ak)
{
	double step = 2.0 * M_PI / (LKD_SLOTS + 1);
	double rel = (a - LKD_KNOT_A) / step;
	rel -= floor(rel / (LKD_SLOTS + 1)) * (LKD_SLOTS + 1);
	int k = (int)lround(rel) - 1;
	if (k < 0 || k >= LKD_SLOTS) {
		return -1;
	}
	*ak = LKD_KNOT_A + (k + 1) * step;
	return k;
}

/* the bow, low on the left of the wreath: two loops, the knot, two tails
 * with their ends cut in a V */
static double lkd_bow(vec3 q, int *mat)
{
	vec3 c = vadd(lkd_ring_at(LKD_KNOT_A, 1.0, 0.040), v3(0.0, 0.0, 0.012));
	vec3 l = vsub(q, c);
	*mat = DM_BOW;
	if (vdot(l, l) > 0.13 * 0.13) {
		return vlen(l) - 0.12;
	}
	double d = lkd_ellipsoid(l, v3(0.020, 0.017, 0.015));
	for (int s = -1; s <= 1; s += 2) {
		vec3 lp = lkd_turn(q, vadd(c, v3(s * 0.039, 0.011, -0.004)), -s * 0.42);
		d = fmin(d, lkd_ellipsoid(lp, v3(0.043, 0.021, 0.013)));
		vec3 tp = lkd_turn(q, vadd(c, v3(s * 0.022, -0.058, -0.007)), s * 0.26);
		double tail = sd_box(tp, v3(0.0125, 0.052, 0.0022));
		tail = fmax(tail, (-0.052 + 0.011 - fabs(tp.x)) - tp.y);
		d = fmin(d, tail);
	}
	*mat = DM_BOW;
	return d;
}

/* a cone tucked in at ak round the ring, lying along it, r out from the
 * middle of the ring's tube */
static double lkd_cone(vec3 q, double ak, double r)
{
	vec3 c = lkd_ring_at(ak, 0.75, r);
	vec3 u = vsub(q, c);
	double along = -u.x * sin(ak) + u.y * cos(ak), across = u.x * cos(ak) + u.y * sin(ak);
	return lkd_ellipsoid(v3(along, across, u.z), v3(0.031, 0.020, 0.018));
}

/* berries, three to a sprig, at ak round the ring: b round its tube, r out */
static double lkd_berries(vec3 q, double ak, double b, double r)
{
	vec3 c = lkd_ring_at(ak, b, r);
	static const double O[3][3] = { { 0.0, 0.012, 0.003 }, { -0.011, -0.006, 0.0 },
		{ 0.011, -0.007, 0.002 } };
	double d = 1e9;
	for (int i = 0; i < 3; i++) {
		d = fmin(d, vlen(vsub(q, vadd(c, v3(O[i][0], O[i][1], O[i][2])))) - 0.0115);
	}
	return d;
}

/* the wreath, in the door's frame */
static double lkd_wreath(int kind, vec3 q, int *mat)
{
	double a, b;
	double r = lkd_ring(q, &a, &b);
	double d;
	double lump = r > 0.12 ? 1.0 : kind == LKD_TINSEL ? lkd_lumps(a, b) : lkd_fir_for(q);
	d = r - (kind == LKD_TINSEL ? 0.040 + 0.006 * lump : 0.034 + 0.030 * lump);
	*mat = kind == LKD_TINSEL ? DM_TINSEL : DM_NEEDLE;
	double ak;
	/* far from the ring, as near as anything tucked in it could be */
	int k = d >= 0.09 ? -1 : lkd_slot(a, &ak);
	if (d >= 0.09) {
		d -= 0.04;
	}
	if (k >= 0) {
		double e;
		if (kind == LKD_TINSEL) {
			e = vlen(vsub(q, lkd_ring_at(ak, 0.95, 0.052))) - 0.024;
			if (e < d) {
				d = e;
				*mat = DM_BAUBLE;
			}
		} else if (k % 3 == 1) {
			e = lkd_cone(q, ak, 0.050);
			if (e < d) {
				d = e;
				*mat = DM_CONE;
			}
		} else {
			e = lkd_berries(q, ak, 1.05, 0.052);
			if (e < d) {
				d = e;
				*mat = DM_BERRY;
			}
		}
	}
	int m2 = DM_BOW;
	double e = lkd_bow(q, &m2);
	if (e < d) {
		d = e;
		*mat = m2;
	}
	return d;
}

/* The garland up the casing and over the head of it, the lights wound in
 * it every so far, pointing out, each side of it in turn: the run (0 left,
 * 1 head, 2 right), how far along it, and which light. */
#define LKG_X (LK_OW + LK_CW * 0.5)
#define LKG_TOP (LK_OH + LK_CW * 0.5)
#define LKG_Z (LK_CZ + 0.030)
#define LKG_PITCH 0.105
#define LKG_N 5                   /* the colours, in turn */

static void lkd_run(vec3 p, int *run, double *s, vec3 *off)
{
	double sl = clampf(p.y, 0.0, LKG_TOP), sh = clampf(p.x, -LKG_X, LKG_X);
	vec3 ol = v3(p.x + LKG_X, p.y - sl, p.z - LKG_Z);
	vec3 or = v3(p.x - LKG_X, p.y - sl, p.z - LKG_Z);
	vec3 oh = v3(p.x - sh, p.y - LKG_TOP, p.z - LKG_Z);
	double dl = vdot(ol, ol), dr = vdot(or, or), dh = vdot(oh, oh);
	if (dl <= dr && dl <= dh) {
		*run = 0;
		*s = sl;
		*off = ol;
	} else if (dr <= dh) {
		*run = 2;
		*s = sl;
		*off = or;
	} else {
		*run = 1;
		/* round from the left side, so the lights go on in order */
		*s = LKG_TOP + (sh + LKG_X);
		*off = oh;
	}
}

/* the light k along a run, where it is (the run's own frame: across, along,
 * out) */
static vec3 lkd_bulb_at(int run, int k)
{
	double s = 0.05 + k * LKG_PITCH, side = (k & 1) ? 1.0 : -1.0;
	if (run == 1) {
		return v3(s - LKG_TOP - LKG_X, LKG_TOP + side * 0.021, LKG_Z + 0.024);
	}
	double x = run == 0 ? -LKG_X : LKG_X;
	return v3(x + side * 0.021, s, LKG_Z + 0.024);
}

/* the lights wound in a garland near s along run, against d */
static double lkd_lights(vec3 p, int run, double s, double d, int *mat, int *bulb)
{
	int k = (int)lround((s - 0.05) / LKG_PITCH);
	for (int j = k - 1; j <= k + 1; j++) {
		if (j < 0) {
			continue;
		}
		vec3 c = lkd_bulb_at(run, j);
		c.z += 0.006;
		vec3 u = vsub(p, c);
		double e = lkd_ellipsoid(u, v3(0.0105, 0.0105, 0.0150));
		if (e < d) {
			d = e;
			*mat = DM_BULB;
			*bulb = j * 3 + run;
		}
	}
	return d;
}

static double lkd_garland(vec3 p, int *mat, int *bulb)
{
	int run;
	double s;
	vec3 off;
	lkd_run(p, &run, &s, &off);
	double r = vlen(off);
	if (r > 0.12) {
		*mat = DM_NEEDLE;
		*bulb = -1;
		return r - 0.06;
	}
	double d = r - (0.020 + 0.026 * lkd_fir_for(p));
	*mat = DM_NEEDLE;
	*bulb = -1;
	return lkd_lights(p, run, s, d, mat, bulb);
}

/* The balloons, a bunch of them tied below the lantern, floating up past
 * the door: where, how big, what colour. */
static const struct { vec3 c; double r; } LKB[5] = {
	{ { 0.40, 1.62, 0.24 }, 0.100 },
	{ { 0.56, 1.74, 0.18 }, 0.095 },
	{ { 0.32, 1.82, 0.30 }, 0.092 },
	{ { 0.50, 1.92, 0.30 }, 0.090 },
	{ { 0.58, 1.48, 0.34 }, 0.085 },
};
/* where their strings are tied: round a nail in the casing */
#define LKB_TIE_X (LK_OW + LK_CW * 0.45)
#define LKB_TIE_Y 1.20
#define LKB_TIE_Z (LK_CZ + 0.004)

/* a balloon's knot: the foot of its string */
static vec3 lkd_knot(int i)
{
	return v3(LKB[i].c.x, LKB[i].c.y - LKB[i].r * 1.16 - 0.014, LKB[i].c.z);
}

/* the balloon a shadow is being found for, which is not in its own way */
static _Thread_local int g_lkd_not_balloon = -1;

static double lkd_balloons(vec3 p, int *mat, int *which)
{
	double d = 1e9;
	*mat = DM_BALLOON;
	*which = 0;
	for (int i = 0; i < 5; i++) {
		if (i == g_lkd_not_balloon) {
			continue;
		}
		vec3 u = vsub(p, LKB[i].c);
		double r = LKB[i].r;
		if (vdot(u, u) > lk_sq(r * 1.5)) {
			double e = vlen(u) - r * 1.3;
			if (e < d) {
				d = e;
				*which = i;
			}
			continue;
		}
		/* full above, drawn in toward the knot */
		double pinch = 1.0 - 0.16 * smoothstepf(0.1, -1.0, u.y / r);
		double e = lkd_ellipsoid(u, v3(r * pinch, r * 1.16, r * pinch));
		vec3 k = vsub(p, vadd(lkd_knot(i), v3(0, 0.006, 0)));
		e = fmin(e, sd_capped_cone(k, 0.0045, 0.0085, 0.008));
		if (e < d) {
			d = e;
			*which = i;
		}
	}
	return d;
}

/* ---- Halloween: the leaves of the fall, little pumpkins, a jack-o'-lantern ----
 *
 * The wreath is leaves on a ring of grapevine -- maples and plainer ones,
 * crimson to gold to brown -- with little pumpkins, cones and bittersweet
 * tucked in it, and where the bow would be, a jack-o'-lantern with a light
 * in it. The garland up the casing is the same leaves on a vine, with
 * orange and purple lights in it. Each leaf is one: thin, folded down its
 * middle, its tip curled; where they all are is in a table, made once, and
 * only the few near a point are asked how far it is to them. */
enum { LKL_MAPLE = 0, LKL_PLAIN };
struct lkd_leaf {
	vec3 c;             /* where it grows from */
	vec3 u, v, w;       /* along it to its tip, across it, out of its face */
	vec3 m;             /* the middle of a sphere round all of it */
	double mr;          /* and how big that is */
	double r;           /* a maple: its point to its tip; a plain one: half its length */
	double fold, curl, bow;
	int kind, col;
};

#define LKA_N 64            /* the wreath's leaves, round it */
#define LKA_ROWS 5          /* and round the vine, from just behind straight out */
#define LKA_B0 (-0.45)      /* round to the inside */
#define LKA_DB 0.90
#define LKA_VINE 0.022      /* the grapevine: how thick, from its middle */
#define LKV_PITCH 0.036     /* the garland's leaves, along it */
#define LKV_ROWS 4
#define LKV_P0 (-1.35)      /* round it, from one side over the front to the other */
#define LKV_DP 0.90
#define LKV_N0 60           /* up a side: LKG_TOP / LKV_PITCH, and one */
#define LKV_N1 30           /* along the head: 2 LKG_X / LKV_PITCH, and one */
#define LKV_VINE 0.011
#define LKJ_RX 0.060        /* the jack-o'-lantern: across, */
#define LKJ_RY 0.049        /* high, */
#define LKJ_RIBS 10
#define LKJ_SHELL 0.0065    /* and how thick its wall is */

static struct lkd_leaf g_lka[LKA_N * LKA_ROWS];
static struct lkd_leaf g_lkv[(2 * LKV_N0 + LKV_N1) * LKV_ROWS];
/* the pumpkins -- 0 the jack-o'-lantern, then the little ones in the
 * wreath -- where, their own across, up (the stalk) and out, and size */
static struct lkd_pumpkin {
	vec3 c, x, y, z;
	double rx, ry;
	int ribs;
} g_lkp[4];
static pthread_once_t g_lkd_fall_once = PTHREAD_ONCE_INIT;

static double lkd_rnd(int id, int salt)
{
	return room_seed(id, 0x4c0 + salt);
}

/* a leaf growing out of something at c: t the way the leaves on it go, n
 * out of it; size, how big they are on it */
static void lkd_leaf_put(struct lkd_leaf *l, int id, vec3 c, vec3 t, vec3 n, double size)
{
	vec3 b = vcross(n, t);
	double turn = (lkd_rnd(id, 1) - 0.5) * 0.70, lift = 0.28 + 0.30 * lkd_rnd(id, 2);
	vec3 u0 = vadd(vmul(t, cos(turn)), vmul(b, sin(turn)));
	l->c = c;
	l->u = vadd(vmul(u0, cos(lift)), vmul(n, sin(lift)));
	l->w = vsub(vmul(n, cos(lift)), vmul(u0, sin(lift)));
	l->v = vcross(l->w, l->u);
	bool maple = lkd_rnd(id, 3) < 0.70;
	l->kind = maple ? LKL_MAPLE : LKL_PLAIN;
	l->r = size * (maple ? 0.036 + 0.012 * lkd_rnd(id, 4) : 0.028 + 0.010 * lkd_rnd(id, 4));
	l->fold = 0.08 + 0.22 * lkd_rnd(id, 5);
	l->curl = -0.12 + 0.30 * lkd_rnd(id, 6);
	l->bow = (lkd_rnd(id, 7) - 0.5) * 0.30;
	/* the maples crimson to gold, the others gold to brown */
	double h = lkd_rnd(id, 8);
	l->col = maple ? (h < 0.22 ? 0 : h < 0.48 ? 1 : h < 0.74 ? 2 : h < 0.90 ? 3 : 4)
		: (h < 0.30 ? 4 : h < 0.55 ? 3 : h < 0.80 ? 5 : 2);
	l->m = vadd(c, vmul(l->u, maple ? 0.40 * l->r : l->r));
	l->mr = (maple ? 0.84 : 1.04) * l->r + 0.002;
}

/* a pumpkin at c, its stalk up, turned to look out */
static void lkd_pumpkin_put(struct lkd_pumpkin *pk, vec3 c, vec3 up, vec3 out, double rx,
	double ry, int ribs)
{
	pk->c = c;
	pk->y = vnorm(up);
	pk->x = vnorm(vcross(pk->y, out));
	pk->z = vcross(pk->x, pk->y);
	pk->rx = rx;
	pk->ry = ry;
	pk->ribs = ribs;
}

static void lkd_fall_make(void)
{
	/* round the wreath, flowing round it anticlockwise */
	for (int i = 0; i < LKA_N; i++) {
		for (int j = 0; j < LKA_ROWS; j++) {
			int id = i * LKA_ROWS + j;
			double a = (i + 0.5 + 0.36 * (lkd_rnd(id, 10) - 0.5)) * (2.0 * M_PI / LKA_N);
			double b = LKA_B0
				+ (j + ((i & 1) ? 0.5 : 0.0) + 0.30 * (lkd_rnd(id, 11) - 0.5)) * LKA_DB;
			vec3 n = v3(cos(b) * cos(a), cos(b) * sin(a), sin(b));
			lkd_leaf_put(&g_lka[id], id, lkd_ring_at(a, b, LKA_VINE - 0.005),
				v3(-sin(a), cos(a), 0.0), n, 1.0);
		}
	}
	/* up each side of the casing, hanging down it */
	for (int side = 0; side < 2; side++) {
		for (int i = 0; i < LKV_N0; i++) {
			for (int j = 0; j < LKV_ROWS; j++) {
				int k = (side * LKV_N0 + i) * LKV_ROWS + j, id = 1000 + k;
				double s = (i + 0.5 + 0.36 * (lkd_rnd(id, 10) - 0.5)) * LKV_PITCH;
				double ph = LKV_P0
					+ (j + ((i & 1) ? 0.5 : 0.0) + 0.30 * (lkd_rnd(id, 11) - 0.5)) * LKV_DP;
				vec3 n = v3(sin(ph), 0.0, cos(ph));
				vec3 c = v3((side ? LKG_X : -LKG_X) + n.x * (LKV_VINE - 0.004), s,
					LKG_Z + n.z * (LKV_VINE - 0.004));
				lkd_leaf_put(&g_lkv[k], id, c, v3(0.0, -1.0, 0.0), n, 0.80);
			}
		}
	}
	/* and along the head, from its middle out to each end */
	for (int i = 0; i < LKV_N1; i++) {
		for (int j = 0; j < LKV_ROWS; j++) {
			int k = (2 * LKV_N0 + i) * LKV_ROWS + j, id = 1000 + k;
			double x = -LKG_X + (i + 0.5 + 0.36 * (lkd_rnd(id, 10) - 0.5)) * LKV_PITCH;
			double ph = LKV_P0
				+ (j + ((i & 1) ? 0.5 : 0.0) + 0.30 * (lkd_rnd(id, 11) - 0.5)) * LKV_DP;
			vec3 n = v3(0.0, sin(ph), cos(ph));
			vec3 c = v3(x, LKG_TOP + n.y * (LKV_VINE - 0.004), LKG_Z + n.z * (LKV_VINE - 0.004));
			lkd_leaf_put(&g_lkv[k], id, c, v3(x < 0.0 ? -1.0 : 1.0, 0.0, 0.0), n, 0.80);
		}
	}
	/* the jack-o'-lantern where the bow would be, looking a little up and
	 * round toward the step; little pumpkins in every fourth slot */
	lkd_pumpkin_put(&g_lkp[0], vadd(lkd_ring_at(LKD_KNOT_A, 0.85, 0.050), v3(0.0, 0.0, 0.016)),
		v3(-0.08, 0.995, -0.10), v3(sin(0.22), 0.0, cos(0.22)), LKJ_RX, LKJ_RY, LKJ_RIBS);
	static const double SIZE[3][2] = { { 0.026, 0.021 }, { 0.024, 0.020 }, { 0.028, 0.022 } };
	for (int i = 0; i < 3; i++) {
		double step = 2.0 * M_PI / (LKD_SLOTS + 1), ak = LKD_KNOT_A + (4 * i + 2) * step;
		vec3 up = v3(cos(ak) * 0.45, 0.55 + 0.30 * sin(ak), 0.70);
		lkd_pumpkin_put(&g_lkp[1 + i], lkd_ring_at(ak, 0.85, 0.052), up, v3(0.0, 0.0, 1.0),
			SIZE[i][0], SIZE[i][1], 8);
	}
}

/* atan2, near enough for a leaf's edge or a pumpkin's ribs (to 1e-5) and
 * a good deal quicker: there are a lot of them */
static double lkd_atan2(double y, double x)
{
	double ax = fabs(x), ay = fabs(y), mx = fmax(ax, ay);
	double a = mx > 0.0 ? fmin(ax, ay) / mx : 0.0, s = a * a;
	double r = ((-0.0464964749 * s + 0.15931422) * s - 0.327622764) * s * a + a;
	r = ay > ax ? 0.5 * M_PI - r : r;
	r = x < 0.0 ? M_PI - r : r;
	return y < 0.0 ? -r : r;
}

static double lkd_len2(double x, double y)
{
	return sqrt(x * x + y * y);
}

/* lkd_ring with the quicker atan2s, for the autumn wreath: it is asked a
 * great deal more, the leaves on it being thick */
static double lkd_ring_quick(vec3 q, double *a, double *b)
{
	double x = q.x - LKD_CX, y = q.y - LKD_CY, z = q.z - LKD_Z;
	double c = sqrt(x * x + y * y) - LKD_R;
	*a = lkd_atan2(y, x);
	*b = lkd_atan2(z, c);
	return sqrt(c * c + z * z);
}

/* a maple leaf's edge: how far out it is from the leaf's point at th
 * (radians round from its tip), its tip 1 -- five lobes, smaller toward
 * the stalk, cut deep between, and teeth on them unless not */
static double lkd_maple_edge(double th, bool teeth)
{
	const double S = 0.95;
	double ph = fabs(th) / S;
	double tri = 1.0 - 2.0 * fabs(ph - (int)(ph + 0.5));
	double env = ph < 1.0 ? 1.0 - 0.12 * ph : ph < 2.0 ? 0.88 - 0.33 * (ph - 1.0)
		: fmax(0.55 - 0.33 * (ph - 2.0), 0.12);
	double e = env * (0.48 + 0.52 * tri * (1.2 - 0.2 * tri));
	if (teeth) {
		double g = ph * 4.0;
		e *= 0.97 + 0.06 * tri * (1.0 - 2.0 * fabs(g - (int)(g + 0.5)));
	}
	return e;
}

/* a point in a leaf's own frame, the leaf's bend taken out of z (and out of
 * y, for a plain one) */
static vec3 lkd_leaf_local(const struct lkd_leaf *l, vec3 q)
{
	vec3 d = vsub(q, l->c);
	double x = vdot(d, l->u), y = vdot(d, l->v), z = vdot(d, l->w), R = l->r;
	double xr = x / R, yr = y / R;
	if (l->kind == LKL_PLAIN) {
		/* curved a little in its plane too */
		double xx = x - R;
		y -= l->bow * (R - xx * xx / R);
		xr = xx / R;
		yr = y / R;
	}
	return v3(x, y, z - R * (l->fold * yr * yr + l->curl * xr * xr));
}

/* a leaf's distance in its own plane: <0 inside its edge */
static double lkd_leaf_edge(const struct lkd_leaf *l, double x, double y, bool teeth)
{
	double R = l->r;
	if (l->kind == LKL_MAPLE) {
		/* round from its point, so how fast the edge goes in and out round
		 * it says how much nearer it may be than it looks */
		double r = sqrt(x * x + y * y);
		double k = (teeth ? 2.7 : 2.1) * R / fmax(r, 0.3 * R);
		return (r - R * lkd_maple_edge(lkd_atan2(y, x), teeth)) / sqrt(1.0 + k * k);
	}
	/* pointed at both ends: two arcs (a vesica, Quilez), along x */
	const double hw = 0.40;
	double rv = 0.5 * R * (hw + 1.0 / hw), dv = rv - hw * R;
	double px = fabs(y), py = fabs(x - R);
	return 0.92 * ((py - R) * dv > px * R ? lkd_len2(px, py - R) : lkd_len2(px + dv, py) - rv);
}

/* a leaf's distance -- or, when it cannot be under best, its distance off
 * its own plane, which is never more and says as much without the edge */
static double lkd_leaf_sd(const struct lkd_leaf *l, vec3 q, double best)
{
	vec3 p = lkd_leaf_local(l, q);
	double dz = 0.85 * (fabs(p.z) - 0.0006);
	if (dz >= best) {
		return dz;
	}
	double d2 = lkd_leaf_edge(l, p.x, p.y, !g_lkd_rough);
	double ox = fmax(d2, 0.0), oz = fmax(dz, 0.0);
	return fmin(fmax(d2, dz), 0.0) + sqrt(ox * ox + oz * oz);
}

/* the nearest of a few leaves to q, if any is nearer than best: those whose
 * spheres are nearest first, until no sphere left could beat it */
static double lkd_leaves_near(const struct lkd_leaf *tab, const int *ids, int n, vec3 q,
	double best, int *which)
{
	double lb[16];
	int order[16], m = 0;
	for (int i = 0; i < n; i++) {
		double b = vlen(vsub(q, tab[ids[i]].m)) - tab[ids[i]].mr;
		if (b >= best) {
			continue;
		}
		int k = m++;
		for (; k > 0 && lb[k - 1] > b; k--) {
			lb[k] = lb[k - 1];
			order[k] = order[k - 1];
		}
		lb[k] = b;
		order[k] = ids[i];
	}
	for (int k = 0; k < m && lb[k] < best; k++) {
		double e = lkd_leaf_sd(&tab[order[k]], q, best);
		if (e < best) {
			best = e;
			*which = order[k];
		}
	}
	return best;
}

/* the wreath's leaves near q: a round the ring, b round the vine (from
 * straight out, -pi/2..3pi/2). They point on round it, so the ones that
 * reach a point grow from before it. */
static double lkd_ring_leaves(vec3 q, double a, double b, double best, int *which)
{
	int ids[12], n = 0;
	int c = (int)floor(a * (LKA_N / (2.0 * M_PI)));
	for (int di = -2; di <= 1; di++) {
		int i = ((c + di) % LKA_N + LKA_N) % LKA_N;
		int jc = (int)lround((b - LKA_B0) / LKA_DB - ((i & 1) ? 0.5 : 0.0));
		for (int j = jc - 1; j <= jc + 1; j++) {
			if (j >= 0 && j < LKA_ROWS) {
				ids[n++] = i * LKA_ROWS + j;
			}
		}
	}
	return lkd_leaves_near(g_lka, ids, n, q, best, which);
}

/* the garland's leaves near p on one run of it (0 left, 1 head, 2 right):
 * down the sides they hang, along the head they point out to its ends, so
 * the ones that reach a point grow from beyond it */
static double lkd_vine_leaves(vec3 p, int run, double best, int *which)
{
	int nn = run == 1 ? LKV_N1 : LKV_N0, ids[16], n = 0;
	int base = run == 0 ? 0 : run == 2 ? LKV_N0 * LKV_ROWS : 2 * LKV_N0 * LKV_ROWS;
	int c = (int)floor((run == 1 ? p.x + LKG_X : p.y) / LKV_PITCH);
	int lo = run == 1 && p.x >= 0.0 ? c - 2 : c - 1;
	for (int i = lo; i <= lo + 3; i++) {
		for (int j = 0; i >= 0 && i < nn && j < LKV_ROWS; j++) {
			ids[n++] = base + i * LKV_ROWS + j;
		}
	}
	return lkd_leaves_near(g_lkv, ids, n, p, best, which);
}

static double lkd_capsule(vec3 p, vec3 a, vec3 b, double ra, double rb)
{
	vec3 pa = vsub(p, a), ba = vsub(b, a);
	double h = clampf(vdot(pa, ba) / vdot(ba, ba), 0.0, 1.0);
	return vlen(vsub(pa, vmul(ba, h))) - (ra + (rb - ra) * h);
}

/* a point in a pumpkin's own frame */
static vec3 lkd_pumpkin_local(const struct lkd_pumpkin *pk, vec3 q)
{
	vec3 d = vsub(q, pk->c);
	return v3(vdot(d, pk->x), vdot(d, pk->y), vdot(d, pk->z));
}

/* its skin, about its middle: ribbed round, drawn in at the top where the
 * stalk is and a little underneath */
static double lkd_pumpkin_skin(vec3 p, double rx, double ry, int ribs)
{
	double g = 1.0 - fabs(sin(lkd_atan2(p.z, p.x) * ribs * 0.5));
	double k = 1.0 - 0.085 * g * g * g;
	double dimple = ry * (p.y > 0.0 ? 0.22 : 0.12) * exp(-(p.x * p.x + p.z * p.z) / (rx * rx) * 10.0);
	vec3 pp = v3(p.x, p.y > 0.0 ? p.y + dimple : p.y - dimple, p.z);
	return 0.8 * lkd_ellipsoid(pp, v3(rx * k, ry, rx * k));
}

static double lkd_stalk(vec3 p, double rx, double ry)
{
	return lkd_capsule(p, v3(0.0, ry * 0.70, 0.0), v3(rx * 0.10, ry * 0.70 + rx * 0.42, -rx * 0.04),
		rx * 0.11, rx * 0.09);
}

static double lkd_pumpkin_sd(const struct lkd_pumpkin *pk, vec3 q, int *mat)
{
	vec3 p = lkd_pumpkin_local(pk, q);
	double d = lkd_pumpkin_skin(p, pk->rx, pk->ry, pk->ribs);
	double st = lkd_stalk(p, pk->rx, pk->ry);
	*mat = st < d ? DM_STEM : DM_PUMPKIN;
	return fmin(d, st);
}

/* a triangle's distance in its plane, <0 inside (Quilez): t its corners */
static double lkd_tri2(double px, double py, const double t[6])
{
	double e0x = t[2] - t[0], e0y = t[3] - t[1], e1x = t[4] - t[2], e1y = t[5] - t[3];
	double e2x = t[0] - t[4], e2y = t[1] - t[5];
	double v0x = px - t[0], v0y = py - t[1], v1x = px - t[2], v1y = py - t[3];
	double v2x = px - t[4], v2y = py - t[5];
	double h0 = clampf((v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y), 0.0, 1.0);
	double h1 = clampf((v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y), 0.0, 1.0);
	double h2 = clampf((v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y), 0.0, 1.0);
	double q0x = v0x - e0x * h0, q0y = v0y - e0y * h0, q1x = v1x - e1x * h1, q1y = v1y - e1y * h1;
	double q2x = v2x - e2x * h2, q2y = v2y - e2y * h2;
	double s = e0x * e2y - e0y * e2x > 0.0 ? 1.0 : -1.0;
	double dd = fmin(fmin(q0x * q0x + q0y * q0y, q1x * q1x + q1y * q1y), q2x * q2x + q2y * q2y);
	double in = fmin(fmin(s * (v0x * e0y - v0y * e0x), s * (v1x * e1y - v1y * e1x)),
		s * (v2x * e2y - v2y * e2x));
	return in > 0.0 ? -sqrt(dd) : sqrt(dd);
}

/* the face cut in the jack-o'-lantern, flat on (x across, y up, from its
 * middle, metres): eyes, a nose, a grin with a tooth down and one up */
static double lkd_face(double x, double y)
{
	static const double EYE[6] = { 0.0105, 0.0060, 0.0315, 0.0040, 0.0215, 0.0245 };
	static const double NOSE[6] = { -0.0062, -0.0095, 0.0062, -0.0095, 0.0, 0.0010 };
	double f = fmin(lkd_tri2(fabs(x), y, EYE), lkd_tri2(x, y, NOSE));
	/* the grin: in a small circle and out of a big one, pointed at its ends */
	double mouth = fmax(lkd_len2(x, y - 0.0070) - 0.0400, 0.2000 - lkd_len2(x, y - 0.1830));
	double t1 = fmax(fabs(x + 0.0110) - 0.0050, -0.0235 - y);
	double t2 = fmax(fabs(x - 0.0130) - 0.0050, y + 0.0215);
	return fmin(f, fmax(mouth, -fmin(t1, t2)));
}

/* a jack-o'-lantern: a pumpkin hollowed out to a wall, the face cut
 * through the front of it -- what is seen through is the inside, lit, and
 * the cut edges of the wall. p in its own frame, metres. */
static double lkd_jack_body(vec3 p, int *mat)
{
	double out = lkd_pumpkin_skin(p, LKJ_RX, LKJ_RY, LKJ_RIBS), in = -(out + LKJ_SHELL);
	double d = fmax(out, in);
	*mat = out >= in ? DM_PUMPKIN : DM_GLOW;
	double cut = -fmax(lkd_face(p.x, p.y), -p.z);
	if (cut > d) {
		d = cut;
		*mat = DM_FLESH;
	}
	double st = lkd_stalk(p, LKJ_RX, LKJ_RY);
	if (st < d) {
		d = st;
		*mat = DM_STEM;
	}
	return d;
}

/* the door's, where the bow would be */
static double lkd_jack(vec3 q, int *mat)
{
	return lkd_jack_body(lkd_pumpkin_local(&g_lkp[0], q), mat);
}

/* the autumn wreath, in the door's frame; part: which leaf or pumpkin */
static double lkd_autumn(vec3 q, int *mat, int *part)
{
	double a, b;
	double r = lkd_ring_quick(q, &a, &b), d;
	*mat = DM_VINE;
	*part = -1;
	if (r > 0.13) {
		/* far from the ring, as near as anything on it could be */
		d = r - 0.095;
	} else {
		/* the grapevine, three strands wound round one another */
		d = r - LKA_VINE * (g_lkd_rough ? 0.95 : 0.90 + 0.10 * sin(3.0 * b - 47.0 * a));
		int which = -1;
		double e = lkd_ring_leaves(q, a, b < -0.5 * M_PI ? b + 2.0 * M_PI : b, d, &which);
		if (which >= 0) {
			d = e;
			*mat = DM_LEAF;
			*part = which;
		}
		double ak;
		int k = lkd_slot(a, &ak), m2;
		if (k >= 0 && k % 4 == 1) {
			e = lkd_pumpkin_sd(&g_lkp[1 + k / 4], q, &m2);
			if (e < d) {
				d = e;
				*mat = m2;
				*part = 1 + k / 4;
			}
		} else if (k >= 0 && k % 4 == 3) {
			e = lkd_cone(q, ak, 0.054);
			if (e < d) {
				d = e;
				*mat = DM_CONE;
			}
		} else if (k >= 0) {
			/* bittersweet */
			e = lkd_berries(q, ak, 1.0, 0.060);
			if (e < d) {
				d = e;
				*mat = DM_BERRY;
			}
		}
	}
	double jr = vlen(vsub(q, g_lkp[0].c));
	int jm = DM_PUMPKIN;
	double e = jr > 0.12 ? jr - 0.092 : lkd_jack(q, &jm);
	if (e < d) {
		d = e;
		*mat = jm;
		*part = 0;
	}
	return d;
}

/* the leaves on a vine up the casing and over its head, the lights in it */
static double lkd_vine(vec3 p, int *mat, int *part)
{
	int run;
	double s;
	vec3 off;
	lkd_run(p, &run, &s, &off);
	double r = vlen(off);
	*mat = DM_VINE;
	*part = -1;
	if (r > 0.10) {
		return r - 0.06;
	}
	double d = r - LKV_VINE;
	int which = -1;
	d = lkd_vine_leaves(p, run, d, &which);
	/* round a top corner, the leaves of the run past it reach too */
	if (run != 1 && p.y > LKG_TOP - 0.09) {
		d = lkd_vine_leaves(p, 1, d, &which);
	} else if (run == 1 && fabs(p.x) > LKG_X - 0.09) {
		d = lkd_vine_leaves(p, p.x < 0.0 ? 0 : 2, d, &which);
	}
	if (which >= 0) {
		*mat = DM_LEAF;
		*part = which;
	}
	return lkd_lights(p, run, s, d, mat, part);
}

/* any group's distance, in its frame */
static double lkd_sd(const struct lk_scene *sc, const struct lkd_group *g, vec3 q, int *mat,
	int *part)
{
	*part = -1;
	bool fall = sc->wreath == LKD_AUTUMN;
	switch (g->kind) {
	case LKD_WREATH:
		return fall ? lkd_autumn(q, mat, part) : lkd_wreath(sc->wreath, q, mat);
	case LKD_GARLAND:
		return fall ? lkd_vine(q, mat, part) : lkd_garland(q, mat, part);
	default:
		return lkd_balloons(q, mat, part);
	}
}

/* A cheap distance that is never more than the real one: to the shape
 * each group is roughly, with room for all that is on it. Marching on this
 * gets a ray to near the thing in a few long strides, and only then is
 * the real one -- the needles, the ornaments -- asked for. */
static double lkd_bound(const struct lk_scene *sc, const struct lkd_group *g, vec3 q)
{
	switch (g->kind) {
	case LKD_WREATH: {
		if (sc->wreath == LKD_AUTUMN) {
			/* the pumpkins stand further out than the bow, the jack-o'-lantern most */
			return fmin(lkd_ring_r(q) - 0.095, vlen(vsub(q, g_lkp[0].c)) - 0.092);
		}
		double d = lkd_ring_r(q) - 0.080;
		vec3 c = vadd(lkd_ring_at(LKD_KNOT_A, 1.0, 0.040), v3(0.0, 0.0, 0.012));
		return fmin(d, vlen(vsub(q, c)) - 0.13);
	}
	case LKD_GARLAND: {
		int run;
		double s;
		vec3 off;
		lkd_run(q, &run, &s, &off);
		return vlen(off) - 0.066;
	}
	case LKD_BALLOONS: {
		double d = 1e9;
		for (int i = 0; i < 5; i++) {
			d = fmin(d, vlen(vsub(q, LKB[i].c)) - LKB[i].r * 1.32);
		}
		return d;
	}
	default:
		return -1.0;
	}
}

static vec3 lkd_normal(const struct lk_scene *sc, const struct lkd_group *g, vec3 q)
{
	const double e = 0.00025;
	int m, pt;
	vec3 k1 = v3(1, -1, -1), k2 = v3(-1, -1, 1), k3 = v3(-1, 1, -1), k4 = v3(1, 1, 1);
	return vnorm(vadd(vadd(vmul(k1, lkd_sd(sc, g, vadd(q, vmul(k1, e)), &m, &pt)),
		vmul(k2, lkd_sd(sc, g, vadd(q, vmul(k2, e)), &m, &pt))),
		vadd(vmul(k3, lkd_sd(sc, g, vadd(q, vmul(k3, e)), &m, &pt)),
		vmul(k4, lkd_sd(sc, g, vadd(q, vmul(k4, e)), &m, &pt)))));
}

/* where a ray is inside a box, if it is: t0..t1 (t0 may be 0) */
static bool lkd_slab(vec3 o, vec3 d, vec3 lo, vec3 hi, double tmax, double *t0, double *t1)
{
	const double oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
	const double a[3] = { lo.x, lo.y, lo.z }, b[3] = { hi.x, hi.y, hi.z };
	double n = 0.0, f = tmax;
	for (int i = 0; i < 3; i++) {
		if (fabs(dd[i]) < 1e-12) {
			if (oo[i] < a[i] || oo[i] > b[i]) {
				return false;
			}
			continue;
		}
		double ta = (a[i] - oo[i]) / dd[i], tb = (b[i] - oo[i]) / dd[i];
		if (ta > tb) {
			double s = ta;
			ta = tb;
			tb = s;
		}
		n = fmax(n, ta);
		f = fmin(f, tb);
		if (n > f) {
			return false;
		}
	}
	*t0 = n;
	*t1 = f;
	return true;
}

/* the ray, through what the door is dressed in (door: the door's frame) */
static void lkd_hit(const struct lk_scene *sc, bool door, vec3 o, vec3 d, struct lk_hit *h)
{
	for (int i = 0; i < sc->ndeco; i++) {
		const struct lkd_group *g = &sc->deco[i];
		bool fall = sc->wreath == LKD_AUTUMN && g->kind != LKD_BALLOONS;
		double t, t1;
		if (g->door != door || !lkd_slab(o, d, g->lo, g->hi, h->t, &t, &t1)) {
			continue;
		}
		g_lkd_rough = sc->rough;
		for (int n = 0; n < 220 && t < t1; n++) {
			vec3 q = vadd(o, vmul(d, t));
			double far = lkd_bound(sc, g, q);
			if (far > 0.002) {
				t += far;
				continue;
			}
			int mat, part;
			double dist = lkd_sd(sc, g, q, &mat, &part);
			if (dist < 0.00012) {
				h->t = t;
				h->id = LK_DECO;
				h->sub = mat;
				h->face = i;
				h->q = q;
				vec3 nn = lkd_normal(sc, g, q);
				h->n = door ? lk_door_dir_out(sc, nn) : nn;
				break;
			}
			/* the leaves' distances are cautious already: longer strides */
			t += fmax(dist * (fall ? (sc->rough ? 0.75 : 0.65) : (sc->rough ? 0.62 : 0.48)), 0.00008);
		}
		g_lkd_rough = false;
	}
}

/* how much of a light the dressing lets through to p, softly (as lk_soft);
 * skip: the group p is on, which shades itself only from a little way off */
static double lkd_shadow(const struct lk_scene *sc, vec3 p, vec3 l, double dist, double k,
	int skip)
{
	double res = 1.0;
	g_lkd_rough = true;
	for (int i = 0; i < sc->ndeco && res > 0.002; i++) {
		const struct lkd_group *g = &sc->deco[i];
		vec3 o = g->door ? lk_to_door(sc, p) : p, d = g->door ? lk_door_dir_in(sc, l) : l;
		double t, t1;
		if (!lkd_slab(o, d, g->lo, g->hi, dist, &t, &t1)) {
			continue;
		}
		/* on itself only whether it is in the way at all: the soft edge
		 * a surface would give itself just off it is stripes -- but a
		 * balloon is shaded by the others in the bunch like anything */
		bool self = i == skip;
		if (self && g->kind == LKD_BALLOONS) {
			int mat, part;
			lkd_balloons(o, &mat, &part);
			g_lkd_not_balloon = part;
			self = false;
		}
		t = fmax(t, self ? 0.012 : 0.0015);
		double ph = 1e20;
		for (int n = 0; n < 48 && t < t1; n++) {
			int mat, part;
			vec3 q = vadd(o, vmul(d, t));
			/* well clear of the cone the penumbra is in: nothing from here */
			double far = lkd_bound(sc, g, q);
			if (far > 0.004 && far * k >= t) {
				t += far;
				ph = 1e20;
				continue;
			}
			double hh = lkd_sd(sc, g, q, &mat, &part);
			if (hh < 0.0001) {
				res = 0.0;
				break;
			}
			if (!self) {
				/* the nearest the ray came, from this distance and the last
				 * (Quilez): no stripes where the steps fall */
				double y = hh * hh / (2.0 * ph);
				double dd = sqrt(fmax(hh * hh - y * y, 0.0));
				res = fmin(res, k * dd / fmax(t - y, 1e-4));
			}
			ph = hh;
			t += clampf(hh * 0.9, 0.0015, 0.05);
		}
		g_lkd_not_balloon = -1;
	}
	g_lkd_rough = false;
	res = clampf(res, 0.0, 1.0);
	return res * res * (3.0 - 2.0 * res);
}

/* how shut in p is by the dressing beside it */
static double lkd_ao(const struct lk_scene *sc, vec3 p, vec3 n)
{
	double ao = 1.0;
	for (int i = 0; i < sc->ndeco; i++) {
		const struct lkd_group *g = &sc->deco[i];
		vec3 o = g->door ? lk_to_door(sc, p) : p, d = g->door ? lk_door_dir_in(sc, n) : n;
		if (o.x < g->lo.x - 0.06 || o.y < g->lo.y - 0.06 || o.z < g->lo.z - 0.06
			|| o.x > g->hi.x + 0.06 || o.y > g->hi.y + 0.06 || o.z > g->hi.z + 0.06) {
			continue;
		}
		static const double D[4] = { 0.006, 0.016, 0.032, 0.060 };
		static const double W[4] = { 0.30, 0.26, 0.22, 0.16 };
		int mat, part;
		g_lkd_rough = true;
		/* nothing of it within reach: nothing to shut it in */
		if (lkd_bound(sc, g, o) > 0.065 || lkd_sd(sc, g, o, &mat, &part) > 0.065) {
			g_lkd_rough = false;
			continue;
		}
		double occ = 0.0;
		for (int j = 0; j < 4; j++) {
			double hh = lkd_sd(sc, g, vadd(o, vmul(d, D[j])), &mat, &part);
			occ += clampf((D[j] - hh) / D[j], 0.0, 1.0) * W[j];
		}
		g_lkd_rough = false;
		ao *= 1.0 - clampf(occ, 0.0, 1.0) * 0.80;
	}
	return ao;
}

/* what the day puts up, for a scene */
static void lkd_dress(struct lk_scene *sc)
{
	unsigned dc = sc->rig.decor;
	sc->ndeco = 0;
	sc->wreath = dc & DESK_DECOR_XMAS ? LKD_FIR : dc & DESK_DECOR_NEWYEAR ? LKD_TINSEL
		: dc & DESK_DECOR_HALLOWEEN ? LKD_AUTUMN : 0;
	bool fall = sc->wreath == LKD_AUTUMN;
	if (fall) {
		pthread_once(&g_lkd_fall_once, lkd_fall_make);
	}
	if (sc->wreath) {
		struct lkd_group *g = &sc->deco[sc->ndeco++];
		g->kind = LKD_WREATH;
		g->door = true;
		double R = LKD_R + (fall ? 0.095 : 0.080);
		g->lo = v3(LKD_CX - R, LKD_CY - R - 0.14, LK_DT);
		g->hi = v3(LKD_CX + R, LKD_CY + R, LKD_Z + (fall ? 0.15 : 0.115));
	}
	if ((dc & DESK_DECOR_XMAS) || fall) {
		struct lkd_group *g = &sc->deco[sc->ndeco++];
		g->kind = LKD_GARLAND;
		g->lo = v3(-LKG_X - 0.07, 0.0, 0.0);
		g->hi = v3(LKG_X + 0.07, LKG_TOP + 0.07, LKG_Z + 0.07);
	}
	if ((dc & DESK_DECOR_BIRTHDAY) && sc->ndeco < LKD_MAX) {
		struct lkd_group *g = &sc->deco[sc->ndeco++];
		g->kind = LKD_BALLOONS;
		g->lo = v3(0.18, 1.33, 0.12);
		g->hi = v3(0.70, 2.05, 0.48);
	}
}

static void lk_door_hit(const struct lk_scene *sc, vec3 ro, vec3 rd, struct lk_hit *h)
{
	vec3 o = lk_to_door(sc, ro), d = lk_door_dir_in(sc, rd);
	double t;
	int f;
	if (lk_box(o, d, v3(0, LK_DY, 0), v3(LK_DW, LK_DY + LK_DH, LK_DT), h->t, &t, &f)) {
		h->t = t;
		h->face = f;
		h->q = vadd(o, vmul(d, t));
		h->id = f == 5 ? LK_DOOR : LK_DOOR_EDGE;
		h->n = lk_door_dir_out(sc, lk_face_n(f));
	}
	vec3 so = lk_to_sign(o), sd = lk_sign_dir_in(d);
	if (lk_box(so, sd, v3(-LK_SIGN_W * 0.5, 0, 0), v3(LK_SIGN_W * 0.5, LK_SIGN_H, LK_SIGN_T),
			h->t, &t, &f)) {
		h->t = t;
		h->face = f;
		h->q = vadd(so, vmul(sd, t));
		h->id = f == 5 ? LK_SIGN : LK_SIGN_EDGE;
		h->n = lk_door_dir_out(sc, lk_sign_dir_out(lk_face_n(f)));
	}
	if (lk_box(o, d, v3(LK_PL_X0, LK_PL_Y0, LK_DT), v3(LK_PL_X1, LK_PL_Y1, LK_DT + LK_PL_T),
			h->t, &t, &f)) {
		h->t = t;
		h->face = f;
		h->q = vadd(o, vmul(d, t));
		h->id = LK_PLATE;
		h->n = lk_door_dir_out(sc, lk_face_n(f));
	}
	lk_march(sc, LK_OC_LOCK, o, d, h);
	lk_march(sc, LK_OC_KNOB, o, d, h);
	lk_march(sc, LK_OC_NAIL, o, d, h);
	if (sc->ndeco) {
		lkd_hit(sc, true, o, d, h);
	}
}

/* the nearest thing in the room along o + d t, from inside it (units) */
static double lk_room_trace(const struct lk_room *rm, vec3 o, vec3 d, double tmax, int *kind,
	vec3 *n)
{
	double best = tmax;
	*kind = -1;
	struct { double t; vec3 n; } c[3];
	c[0].t = d.x > 0.0 ? (rm->wx - o.x) / d.x : d.x < 0.0 ? (-rm->wx - o.x) / d.x : 1e30;
	c[0].n = v3(d.x > 0.0 ? -1 : 1, 0, 0);
	c[1].t = d.y > 0.0 ? (rm->cy - o.y) / d.y : d.y < 0.0 ? (rm->fy - o.y) / d.y : 1e30;
	c[1].n = v3(0, d.y > 0.0 ? -1 : 1, 0);
	c[2].t = d.z > 0.0 ? (rm->fz - o.z) / d.z : d.z < 0.0 ? (rm->bz - o.z) / d.z : 1e30;
	c[2].n = v3(0, 0, d.z > 0.0 ? -1 : 1);
	for (int i = 0; i < 3; i++) {
		if (c[i].t > 0.0 && c[i].t < best) {
			best = c[i].t;
			*kind = LK_RM_SHELL;
			*n = c[i].n;
		}
	}
	for (int i = 0; i < rm->nbox; i++) {
		double t;
		int f;
		if (lk_box(o, d, rm->box[i].lo, rm->box[i].hi, best, &t, &f)) {
			best = t;
			*kind = rm->box[i].kind;
			*n = lk_face_n(f);
		}
	}
	return best;
}

/* into the room: from the doorway, or from where the eye already is */
static void lk_room_hit(const struct lk_scene *sc, vec3 ro, vec3 rd, struct lk_hit *h)
{
	const struct lk_room *rm = &sc->room;
	double t0 = 0.0;
	vec3 o = ro;
	if (ro.z > -LK_WALL) {
		if (rd.z >= 0.0) {
			return;
		}
		t0 = (-LK_WALL - ro.z) / rd.z;
		o = vadd(ro, vmul(rd, t0));
		if (fabs(o.x) > LK_OW || o.y < 0.0 || o.y > LK_OH || t0 >= h->t) {
			return;
		}
	}
	vec3 orr = vmul(vsub(o, rm->t), 1.0 / rm->s);
	int kind;
	vec3 n = v3(0, 0, 1);
	double tr = lk_room_trace(rm, orr, rd, 1e9, &kind, &n);
	if (kind < 0) {
		return;
	}
	double t = t0 + tr * rm->s;
	if (t >= h->t) {
		return;
	}
	h->t = t;
	h->id = LK_ROOMIN;
	h->sub = kind;
	h->n = n;
	h->q = vadd(orr, vmul(rd, tr));
}

static void lk_intersect(const struct lk_scene *sc, vec3 ro, vec3 rd, struct lk_hit *h)
{
	h->t = 1e9;
	h->id = LK_NONE;
	h->face = 0;
	h->sub = 0;
	double t;
	int f;
	/* the wall, the doorway and the casing out of it */
	if (rd.z < 0.0 && ro.z > 0.0) {
		t = -ro.z / rd.z;
		vec3 p = vadd(ro, vmul(rd, t));
		if (p.y >= 0.0 && (fabs(p.x) > LK_OW + LK_CW || p.y > LK_OH + LK_CW)) {
			h->t = t;
			h->id = LK_WALL_ID;
			h->n = v3(0, 0, 1);
			h->q = p;
		}
	}
	/* the step, and the sill through the doorway */
	if (rd.y < 0.0 && ro.y > 0.0) {
		t = -ro.y / rd.y;
		vec3 p = vadd(ro, vmul(rd, t));
		if (t < h->t && (p.z > 0.0 || (fabs(p.x) < LK_OW && p.z > -LK_WALL))) {
			h->t = t;
			h->id = LK_STEP;
			h->n = v3(0, 1, 0);
			h->q = p;
		}
	}
	for (int i = 0; i < 3; i++) {
		if (lk_box(ro, rd, LK_CASE[i][0], LK_CASE[i][1], h->t, &t, &f)) {
			h->t = t;
			h->id = LK_CASING;
			h->face = f;
			h->sub = i;
			h->n = lk_face_n(f);
			h->q = vadd(ro, vmul(rd, t));
		}
	}
	/* the reveal: the jambs and the head, back through the wall */
	for (int s = -1; s <= 1; s += 2) {
		if (rd.x * s > 0.0 && ro.x * s < LK_OW) {
			t = (s * LK_OW - ro.x) / rd.x;
			vec3 p = vadd(ro, vmul(rd, t));
			if (t < h->t && p.z <= 0.0 && p.z >= -LK_WALL && p.y >= 0.0 && p.y <= LK_OH) {
				h->t = t;
				h->id = LK_JAMB;
				h->n = v3(-s, 0, 0);
				h->q = p;
			}
		}
	}
	if (rd.y > 0.0 && ro.y < LK_OH) {
		t = (LK_OH - ro.y) / rd.y;
		vec3 p = vadd(ro, vmul(rd, t));
		if (t < h->t && p.z <= 0.0 && p.z >= -LK_WALL && fabs(p.x) <= LK_OW) {
			h->t = t;
			h->id = LK_JAMB;
			h->n = v3(0, -1, 0);
			h->q = p;
		}
	}
	/* inside: the room, from where the ray comes through the doorway */
	if (sc->room.on) {
		lk_room_hit(sc, ro, rd, h);
	} else if (rd.z < 0.0) {
		const double z0 = -LK_WALL, z1 = LK_ROOM_Z;
		struct lk_cand { double t; vec3 n; int sub; } c[5];
		int nc = 0;
		if (rd.y < 0.0) {
			c[nc++] = (struct lk_cand){ (sc->room_y0 - ro.y) / rd.y, v3(0, 1, 0), 0 };
		}
		if (rd.y > 0.0) {
			c[nc++] = (struct lk_cand){ (sc->room_y1 - ro.y) / rd.y, v3(0, -1, 0), 1 };
		}
		if (rd.x < 0.0) {
			c[nc++] = (struct lk_cand){ (-sc->room_hx - ro.x) / rd.x, v3(1, 0, 0), 2 };
		}
		if (rd.x > 0.0) {
			c[nc++] = (struct lk_cand){ (sc->room_hx - ro.x) / rd.x, v3(-1, 0, 0), 4 };
		}
		c[nc++] = (struct lk_cand){ (z1 - ro.z) / rd.z, v3(0, 0, 1), 3 };
		for (int i = 0; i < nc; i++) {
			if (c[i].t <= 0.0 || c[i].t >= h->t) {
				continue;
			}
			vec3 p = vadd(ro, vmul(rd, c[i].t));
			if (p.z > z0 + 1e-6 || p.z < z1 - 1e-6 || fabs(p.x) > sc->room_hx + 1e-6
				|| p.y < sc->room_y0 - 1e-6 || p.y > sc->room_y1 + 1e-6) {
				continue;
			}
			h->t = c[i].t;
			h->id = c[i].sub == 3 ? LK_PICTURE : LK_INSIDE;
			h->sub = c[i].sub;
			h->n = c[i].n;
			h->q = p;
		}
	}
	for (int i = 0; i < 6; i++) {
		if (lk_box(ro, rd, LK_LANTERN[i].lo, LK_LANTERN[i].hi, h->t, &t, &f)) {
			h->t = t;
			h->id = LK_LANTERN[i].id;
			h->face = f;
			h->sub = i;
			h->n = lk_face_n(f);
			h->q = vadd(ro, vmul(rd, t));
		}
	}
	if (sc->ndeco) {
		lkd_hit(sc, false, ro, rd, h);
	}
	lk_door_hit(sc, ro, rd, h);
}
