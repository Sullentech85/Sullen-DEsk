/* Part of draw.c: the front door: light and what things are made of.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ---- light, and what is in its way ---- */

static bool lk_in_doorway(vec3 p)
{
	return p.z < LK_CZ - 1e-5 && fabs(p.x) < LK_OW + 1e-4 && p.y < LK_OH + 1e-4;
}

/* From inside the doorway the light has to come out through it: where the
 * ray to it crosses the front of the casing, how far inside the opening. */
static double lk_tunnel(vec3 p, vec3 l, double dist, double rad)
{
	if (p.z >= LK_CZ || l.z <= 1e-6) {
		return 1.0;
	}
	double s = (LK_CZ - p.z) / l.z;
	if (s >= dist) {
		return 1.0;
	}
	vec3 c = vadd(p, vmul(l, s));
	double m = fmin(fmin(LK_OW - fabs(c.x), LK_OH - c.y), c.y + 0.5);
	double w = rad * s / fmax(dist - s, 0.02) + 0.0006;
	return smoothstepf(-w, w, m);
}

/* the shadow of one thing on the door: march its distance along the ray,
 * the nearest it comes for how far along, which is the penumbra */
static double lk_soft(const struct lk_scene *sc, int j, vec3 o, vec3 d, double tmax, double k)
{
	vec3 c;
	double r;
	lk_occ_bound(j, &c, &r);
	vec3 oc = vsub(c, o);
	double tc = vdot(oc, d);
	double d2 = vdot(oc, oc) - tc * tc;
	double reach = r + fmax(tc, 0.0) / k;
	bool inside = vdot(oc, oc) < r * r;
	if (!inside && (tc < 0.0 || d2 > reach * reach)) {
		return 1.0;
	}
	double t = fmax(0.0016, tc - r - 0.01), t1 = fmin(tmax, tc + r + 0.01);
	double res = 1.0;
	for (int i = 0; i < 36 && t < t1; i++) {
		double hh = lk_occ_sd(sc, j, vadd(o, vmul(d, t)));
		if (hh < 0.00008) {
			return 0.0;
		}
		res = fmin(res, k * hh / t);
		t += clampf(hh, 0.0006, 0.024);
	}
	res = clampf(res, 0.0, 1.0);
	return res * res * (3.0 - 2.0 * res);
}

static int lk_self_occ(const struct lk_hit *h)
{
	switch (h->id) {
	case LK_SIGN:
	case LK_SIGN_EDGE:
		return LK_OC_SIGN;
	case LK_PLATE:
		return LK_OC_PLATE;
	default:
		return -1;
	}
}

static double lk_shadow(const struct lk_scene *sc, vec3 p, vec3 n, vec3 l, double dist,
	double rad, int light, const struct lk_hit *h)
{
	double s = 1.0;
	if (lk_in_doorway(p)) {
		s = lk_tunnel(p, l, dist, rad);
		if (s <= 0.002) {
			return 0.0;
		}
	}
	vec3 pp = vadd(p, vmul(n, 0.0007));
	/* the casing over the wall beside it, the lantern over the wall under it */
	if (h->id == LK_WALL_ID || h->id == LK_CASING || h->id == LK_STEP) {
		double t;
		int f;
		for (int i = 0; i < 3; i++) {
			if (!(h->id == LK_CASING && h->sub == i)
				&& lk_box(pp, l, LK_CASE[i][0], LK_CASE[i][1], dist, &t, &f)) {
				return 0.0;
			}
		}
		if (light != 1 && lk_box(pp, l, LK_LANTERN[1].lo, LK_LANTERN[1].hi, dist, &t, &f)) {
			return 0.0;
		}
	}
	if (h->id == LK_LGLASS || h->id == LK_IRON || h->id == LK_PICTURE || h->id == LK_INSIDE
		|| h->id == LK_ROOMIN) {
		return s;
	}
	vec3 o = lk_to_door(sc, pp), d = lk_door_dir_in(sc, l);
	double k = dist / fmax(rad, 1e-3);
	int self = lk_self_occ(h);
	for (int j = 0; j < LK_OC_N && s > 0.002; j++) {
		if (j != self) {
			s *= lk_soft(sc, j, o, d, dist, k);
		}
	}
	/* the lantern is beside the door and low: the dressing's shadows from
	 * it are long, faint and dear to find; the lamp overhead's and the
	 * sun's are what it is seen by */
	if (sc->ndeco && s > 0.002 && light != 1) {
		s *= lkd_shadow(sc, pp, l, dist, k, h->id == LK_DECO ? h->face : -1);
	}
	return s;
}

/* how shut in a point on or near the door is by what stands on it */
static double lk_ao(const struct lk_scene *sc, vec3 p, vec3 n)
{
	vec3 o = lk_to_door(sc, p), d = lk_door_dir_in(sc, n);
	static const double D[4] = { 0.004, 0.012, 0.026, 0.050 };
	static const double W[4] = { 0.30, 0.26, 0.22, 0.16 };
	double ao = 1.0;
	for (int j = 0; j < LK_OC_N; j++) {
		vec3 c;
		double r;
		lk_occ_bound(j, &c, &r);
		if (vlen(vsub(o, c)) > r + 0.06) {
			continue;
		}
		double occ = 0.0;
		for (int i = 0; i < 4; i++) {
			double hh = lk_occ_sd(sc, j, vadd(o, vmul(d, D[i])));
			occ += clampf((D[i] - hh) / D[i], 0.0, 1.0) * W[i];
		}
		ao *= 1.0 - clampf(occ, 0.0, 1.0) * 0.85;
	}
	return ao;
}

/* GGX, its lobe widened by the size of the light so a big lamp makes a
 * broad soft highlight rather than a pinpoint, and dimmed to match */
static double lk_ggx(vec3 n, vec3 v, vec3 l, double rough, double grow)
{
	double a0 = fmax(rough * rough, 0.002);
	double a = fmin(1.0, a0 + grow);
	vec3 hv = vnorm(vadd(v, l));
	double noh = fmax(vdot(n, hv), 0.0), nov = fmax(vdot(n, v), 1e-4), nol = fmax(vdot(n, l), 1e-4);
	double a2 = a * a;
	double dd = noh * noh * (a2 - 1.0) + 1.0;
	double D = a2 / (M_PI * dd * dd) * lk_sq(a0 / a);
	double k = lk_sq(rough + 1.0) / 8.0;
	double G = nov / (nov * (1.0 - k) + k) * nol / (nol * (1.0 - k) + k);
	return D * G / (4.0 * nov * nol);
}

/* all round: the street, the step the lamp lights, the sky (the rig's);
 * as a surface that rough sees it, the horizon not a line in it */
static vec3 lk_env(const struct lk_scene *sc, vec3 r, double rough)
{
	const struct lk_rig *g = &sc->rig;
	vec3 sky = vmix(g->sky_lo, g->sky_hi, smoothstepf(0.0, 0.8, r.y));
	vec3 gnd = vmix(g->gnd_hi, g->gnd_lo, smoothstepf(0.0, -0.6, r.y));
	double b = 0.03 + 0.55 * clampf(rough, 0.0, 1.0);
	vec3 c = vmix(gnd, sky, smoothstepf(-b, b, r.y));
	if (g->porch > 0.0) {
		double lamp = fmax(vdot(r, vnorm(v3(0.12, 0.70, 0.70))), 0.0);
		c = vadd(c, vmul(v3(1.0, 0.80, 0.55), 0.55 * pow(lamp, 10.0) * g->porch));
		double step = fmax(vdot(r, vnorm(v3(0.0, -0.55, 0.84))), 0.0);
		c = vadd(c, vmul(v3(0.30, 0.23, 0.15), 0.30 * step * step * g->porch));
	}
	return c;
}

/* The porch's eave, out of sight overhead -- 2.45 m up, 0.95 m out from
 * the wall -- which keeps a high sun off the door: how much of the sun a
 * point still has, softly at the eave's shadow's edge. */
#define LK_EAVE_Y 2.45
#define LK_EAVE_Z 0.95
static double lk_eave(vec3 p, vec3 l)
{
	if (l.y <= 1e-4 || p.y >= LK_EAVE_Y || p.z >= LK_EAVE_Z) {
		return 1.0;
	}
	double t = (LK_EAVE_Y - p.y) / l.y;
	double z = p.z + l.z * t, x = p.x + l.x * t;
	if (fabs(x) > 2.6) {
		return 1.0;
	}
	/* soft: the eave is never seen, and its shadow is not to read as a
	 * line drawn across the house */
	double w = 0.12 * t + 0.10;
	return smoothstepf(LK_EAVE_Z - w, LK_EAVE_Z + w, z);
}

struct lk_mat {
	vec3 albedo, emit, n;
	double rough, metal, coat, ao;
	int sub;
	bool unlit, disp;
};

static vec3 lk_shade(const struct lk_scene *sc, vec3 p, vec3 v, const struct lk_mat *m,
	const struct lk_hit *h)
{
	if (m->unlit) {
		return m->emit;
	}
	vec3 n = m->n;
	vec3 diff = vmul(m->albedo, 1.0 - m->metal);
	vec3 f0 = vmix(v3(0.04, 0.04, 0.04), m->albedo, m->metal);
	vec3 col = m->emit;
	double nov = fmax(vdot(n, v), 1e-4);
	for (int i = 0; i < 2; i++) {
		const struct lk_light *L = &LK_LIGHTS[i];
		double level = i == 0 ? sc->rig.porch : sc->rig.lantern;
		if (level <= 0.002 || (i == 1 && (h->id == LK_LGLASS || h->id == LK_IRON))) {
			continue;
		}
		vec3 lv = vsub(L->pos, p);
		double d2 = vdot(lv, lv), d = sqrt(d2);
		vec3 l = vmul(lv, 1.0 / d);
		double nol = vdot(n, l);
		if (nol <= 0.0 || vdot(h->n, l) <= -0.05) {
			continue;
		}
		double sh = lk_shadow(sc, p, h->n, l, d, L->r, i, h);
		if (sh <= 0.002) {
			continue;
		}
		vec3 E = vmul(L->col, level * sh * nol / d2);
		vec3 hv = vnorm(vadd(v, l));
		double fw = pow(1.0 - fmax(vdot(v, hv), 0.0), 5.0);
		vec3 F = vadd(f0, vmul(vsub(v3(1, 1, 1), f0), fw));
		double s = lk_ggx(n, v, l, m->rough, L->r / d);
		col = vadd(col, vmul3(vadd(vmul(diff, 1.0 / M_PI), vmul(F, s)), E));
		if (m->coat > 0.0) {
			double cs = lk_ggx(n, v, l, 0.10, L->r / d) * (0.04 + 0.96 * fw) * m->coat;
			col = vadd(col, vmul(E, cs));
		}
	}
	/* the sun, out and round this side of the house */
	if (sc->rig.sun_col.x > 0.0) {
		vec3 l = sc->rig.sun;
		double nol = vdot(n, l);
		if (nol > 0.0 && vdot(h->n, l) > -0.05) {
			double sh = lk_eave(p, l);
			if (sh > 0.002) {
				sh *= lk_shadow(sc, p, h->n, l, 60.0, 60.0 * sc->rig.sun_soft, 2, h);
			}
			if (sh > 0.002) {
				vec3 E = vmul(sc->rig.sun_col, sh * nol);
				vec3 hv = vnorm(vadd(v, l));
				double fw = pow(1.0 - fmax(vdot(v, hv), 0.0), 5.0);
				vec3 F = vadd(f0, vmul(vsub(v3(1, 1, 1), f0), fw));
				double s = lk_ggx(n, v, l, m->rough, sc->rig.sun_soft);
				col = vadd(col, vmul3(vadd(vmul(diff, 1.0 / M_PI), vmul(F, s)), E));
				if (m->coat > 0.0) {
					double cs = lk_ggx(n, v, l, 0.10, sc->rig.sun_soft) * (0.04 + 0.96 * fw)
						* m->coat;
					col = vadd(col, vmul(E, cs));
				}
			}
		}
	}
	/* the moon, over the roofs to the left */
	if (sc->rig.moon_col.z > 0.0) {
		vec3 l = vnorm(LK_MOON_DIR);
		double nol = vdot(n, l);
		if (nol > 0.0 && vdot(h->n, l) > -0.05) {
			double sh = lk_in_doorway(p) ? lk_tunnel(p, l, 1e3, 1e3 * 0.012) : 1.0;
			col = vadd(col, vmul3(vmul(diff, 1.0 / M_PI), vmul(sc->rig.moon_col, nol * sh)));
		}
	}
	/* the room's light, out through the doorway once it is open */
	if (sc->open > 0.0 && p.z > -LK_WALL) {
		vec3 lp = v3(0.0, 1.25, -0.80);
		vec3 lv = vsub(lp, p);
		double d2 = vdot(lv, lv), d = sqrt(d2);
		vec3 l = vmul(lv, 1.0 / d);
		double nol = vdot(n, l);
		if (nol > 0.0) {
			double sh = 1.0;
			if (p.z > -LK_WALL) {
				double s0 = (0.0 - p.z) / fmin(l.z, -1e-6);
				vec3 c = vadd(p, vmul(l, fmax(s0, 0.0)));
				sh = smoothstepf(-0.02, 0.02, fmin(LK_OW - fabs(c.x), LK_OH - c.y));
			}
			vec3 E = vmul(v3(0.95, 0.70, 0.42), 0.55 * sc->open * sh * nol / fmax(d2, 0.15));
			col = vadd(col, vmul3(vmul(diff, 1.0 / M_PI), E));
		}
	}
	/* the night all round, less where it is shut in */
	vec3 amb = vmix(sc->rig.amb_lo, sc->rig.amb_hi, 0.5 + 0.5 * n.y);
	col = vadd(col, vmul3(diff, vmul(amb, m->ao)));
	double ea, eb;
	env_brdf(nov, m->rough, &ea, &eb);
	vec3 r = reflect3(vmul(v, -1.0), n);
	vec3 env = lk_env(sc, r, m->rough);
	col = vadd(col, vmul3(env, vmul(vadd(vmul(f0, ea), v3(eb, eb, eb)), m->ao)));
	if (m->coat > 0.0) {
		double ca, cb;
		env_brdf(nov, 0.10, &ca, &cb);
		col = vadd(col, vmul(env, (0.04 * ca + cb) * m->coat * m->ao));
	}
	return col;
}

/* ---- what things are made of ---- */

static float g_lk_lin[256];

/* bilinear, u and v 0..1 across the picture; linear light unless disp */
static vec3 lk_tex_at(const struct lk_tex *t, double u, double v, bool disp, double *alpha)
{
	if (!t->px) {
		*alpha = 0.0;
		return v3(0, 0, 0);
	}
	double x = u * t->w - 0.5, y = v * t->h - 0.5;
	int x0 = (int)floor(x), y0 = (int)floor(y);
	double fx = x - x0, fy = y - y0;
	vec3 c = v3(0, 0, 0);
	double al = 0.0;
	for (int j = 0; j < 2; j++) {
		for (int i = 0; i < 2; i++) {
			int xi = x0 + i, yi = y0 + j;
			xi = xi < 0 ? 0 : xi >= t->w ? t->w - 1 : xi;
			yi = yi < 0 ? 0 : yi >= t->h ? t->h - 1 : yi;
			double wgt = (i ? fx : 1.0 - fx) * (j ? fy : 1.0 - fy);
			if (t->a8) {
				al += wgt * t->px[(size_t)yi * t->stride + xi] / 255.0;
				continue;
			}
			uint32_t px = *(const uint32_t *)(t->px + (size_t)yi * t->stride + (size_t)xi * 4);
			unsigned a = px >> 24;
			if (a == 0) {
				continue;
			}
			unsigned r = ((px >> 16) & 0xff) * 255 / a, g = ((px >> 8) & 0xff) * 255 / a;
			unsigned b = (px & 0xff) * 255 / a;
			r = r > 255 ? 255 : r;
			g = g > 255 ? 255 : g;
			b = b > 255 ? 255 : b;
			if (disp) {
				c = vadd(c, vmul(v3(r / 255.0, g / 255.0, b / 255.0), wgt * a / 255.0));
			} else {
				c = vadd(c, vmul(v3(g_lk_lin[r], g_lk_lin[g], g_lk_lin[b]), wgt * a / 255.0));
			}
			al += wgt * a / 255.0;
		}
	}
	*alpha = al;
	return al > 1e-6 && !t->a8 ? vmul(c, 1.0 / al) : c;
}

/* Walnut, cheaply: growth rings across the grain (u, metres), their line
 * wandering slowly along it (v), broad figure over them and open pores in
 * them; piece says which board, so that no two are cut alike. */
static vec3 lk_wood(double u, double v, int piece, double *rough)
{
	double s = room_seed(piece, 11);
	double x = u + (room_noise(v * 2.2 + s * 31.0, u * 6.0 + s * 5.0) - 0.5) * 0.034
		+ (room_noise(v * 11.0 + s * 3.0, u * 40.0) - 0.5) * 0.006;
	double ring = x * (95.0 + s * 45.0) + room_noise(x * 22.0 + s * 4.0, v * 0.9 + s * 9.0) * 3.2;
	double g = 0.5 + 0.5 * sin(ring * 2.0 * M_PI);
	g = g * g * g;
	double fig = room_noise(x * 13.0 + s * 3.0, v * 1.1 + s);
	double tone = 0.88 + s * 0.24;
	vec3 c = vmix(vmul(v3(0.215, 0.112, 0.054), tone), vmul(v3(0.082, 0.038, 0.017), tone),
		clampf(g * 0.62 + fig * 0.42, 0.0, 1.0));
	double pore = room_noise(u * 640.0 + s * 17.0, v * 30.0);
	pore = pore * pore * pore * pore;
	c = vmul(c, 1.0 - pore * 0.28);
	*rough = 0.34 + g * 0.10 + pore * 0.16;
	return c;
}

static double lk_rect_d(double x, double y, double x0, double y0, double x1, double y1, int *edge)
{
	double d = x - x0;
	int e = 0;
	if (x1 - x < d) {
		d = x1 - x;
		e = 1;
	}
	if (y - y0 < d) {
		d = y - y0;
		e = 2;
	}
	if (y1 - y < d) {
		d = y1 - y;
		e = 3;
	}
	*edge = e;
	return d;
}

/* The face of the door as relief: how far each point stands proud of the
 * stiles and rails (or sinks below them), what it is, and the nearest
 * edge of what it is in, which way its grain runs. */
enum { LK_DF_FRAME, LK_DF_PANEL, LK_DF_MOULD, LK_DF_GLASS, LK_DF_BEAD, LK_DF_MUNTIN };

static double lk_relief(double x, double y, int *what, int *edge, double *dist)
{
	int e;
	*what = LK_DF_FRAME;
	*edge = -1;
	*dist = 1.0;
	double d = lk_rect_d(x, y, LK_STILE, LK_GLASS0, LK_DW - LK_STILE, LK_GLASS1, &e);
	if (d >= 0.0) {
		*edge = e;
		*dist = d;
		if (d < 0.008) {
			*what = LK_DF_BEAD;
			return 0.0032 * sin(M_PI * d / 0.008);
		}
		if (d < 0.016) {
			*what = LK_DF_BEAD;
			double s = (d - 0.008) / 0.008;
			return -0.016 + 0.016 * sqrt(fmax(0.0, 1.0 - s * s));
		}
		double dv = fabs(x - LK_DW * 0.5), dh = fabs(y - LK_MUNTIN);
		double dm = fmin(dv, dh);
		if (dm < 0.013) {
			*what = LK_DF_MUNTIN;
			*edge = dv < dh ? 0 : 2;
			return -0.016 + 0.015 * sqrt(fmax(0.0, 1.0 - lk_sq(dm / 0.013)));
		}
		*what = LK_DF_GLASS;
		*dist = fmin(d, dm);
		return -0.016;
	}
	for (int i = 0; i < 2; i++) {
		double x0 = i ? LK_DW * 0.5 + LK_MID : LK_STILE;
		double x1 = i ? LK_DW - LK_STILE : LK_DW * 0.5 - LK_MID;
		d = lk_rect_d(x, y, x0, LK_PANEL0, x1, LK_RAIL0, &e);
		if (d < 0.0) {
			continue;
		}
		*edge = e;
		*dist = d;
		if (d < 0.010) {
			*what = LK_DF_MOULD;
			return -0.010 + sqrt(fmax(0.0, 0.0001 - d * d));
		}
		*what = LK_DF_PANEL;
		if (d < 0.016) {
			return -0.010;
		}
		if (d < 0.060) {
			return -0.010 + 0.007 * (d - 0.016) / 0.044;
		}
		if (d < 0.064) {
			return -0.003 + 0.003 * smoothstepf(0.060, 0.064, d);
		}
		return 0.0;
	}
	return 0.0;
}

static void lk_door_face(const struct lk_scene *sc, vec3 q, struct lk_mat *m)
{
	double x = q.x, y = q.y;
	int what, edge;
	double dist;
	double h0 = lk_relief(x, y, &what, &edge, &dist);
	const double e = 0.0006;
	int w2, e2;
	double d2;
	double hx = lk_relief(x + e, y, &w2, &e2, &d2), hy = lk_relief(x, y + e, &w2, &e2, &d2);
	vec3 nl = vnorm(v3(-(hx - h0) / e, -(hy - h0) / e, 1.0));
	m->n = lk_door_dir_out(sc, nl);
	m->sub = what;
	/* which board, and which way its grain goes */
	bool along_y;
	int piece;
	switch (what) {
	case LK_DF_PANEL:
		along_y = true;
		piece = x < LK_DW * 0.5 ? 7 : 8;
		break;
	case LK_DF_MOULD:
	case LK_DF_BEAD:
		along_y = edge < 2;
		piece = 20 + edge;
		break;
	case LK_DF_MUNTIN:
		along_y = edge == 0;
		piece = 12 + edge;
		break;
	default:
		if (x < LK_STILE || x > LK_DW - LK_STILE || (fabs(x - LK_DW * 0.5) < LK_MID + 0.001
			&& y > LK_PANEL0 && y < LK_RAIL0)) {
			along_y = true;
			piece = x < LK_STILE ? 1 : x > LK_DW - LK_STILE ? 2 : 6;
		} else {
			along_y = false;
			piece = y > LK_GLASS1 ? 3 : y > LK_PANEL0 ? 4 : 5;
		}
		break;
	}
	double rough;
	m->albedo = along_y ? lk_wood(x + piece * 0.37, y, piece, &rough)
		: lk_wood(y + piece * 0.37, x, piece, &rough);
	m->rough = rough;
	m->coat = 0.55;
	m->metal = 0.0;
	/* shut in: the grooves, and round the frame while it is shut */
	double ao = 1.0;
	if (what == LK_DF_PANEL && dist < 0.016) {
		ao = 0.60;
	} else if (what == LK_DF_PANEL && dist < 0.060) {
		ao = 0.60 + 0.40 * smoothstepf(0.016, 0.060, dist);
	} else if (what == LK_DF_MOULD) {
		ao = 0.70 + 0.30 * (1.0 - dist / 0.010);
	}
	double rim = fmin(fmin(x, LK_DW - x), LK_DY + LK_DH - y);
	ao *= 1.0 - 0.55 * exp(-rim / 0.030) * (1.0 - sc->open);
	if (what == LK_DF_GLASS) {
		/* frosted, and the room's light coming through it */
		double fr = room_noise(x * 380.0, y * 380.0) * 0.6 + room_noise(x * 90.0, y * 90.0) * 0.4;
		double lamp = exp(-(lk_sq(x - 0.30) + lk_sq(y - 1.50) * 0.6) / 0.10);
		vec3 warm = vmul(v3(1.00, 0.56, 0.22), (0.16 + 0.34 * lamp) * (0.70 + 0.55 * fr));
		double shut = smoothstepf(0.0, 0.035, dist);
		m->emit = vmul(warm, sc->lamp * sc->rig.glass * (0.35 + 0.65 * shut));
		m->albedo = v3(0.015, 0.012, 0.010);
		m->rough = 0.16 + fr * 0.10;
		m->coat = 0.0;
		ao *= 0.55 + 0.45 * shut;
	}
	m->ao = ao;
}

static double lk_brick_h(double x, double y, int *id, double *edge)
{
	const double H = 0.075, L = 0.225, J = 0.0095;
	double row = floor(y / H);
	double off = ((long)row & 1) ? L * 0.5 : 0.0;
	double col = floor((x + off) / L);
	double bx = x + off - col * L, by = y - row * H;
	double e = fmin(fmin(bx - J * 0.5, L - J * 0.5 - bx), fmin(by - J * 0.5, H - J * 0.5 - by));
	e += (room_noise(x * 150.0, y * 150.0) - 0.5) * 0.0026;
	*id = (int)col * 73 + (int)row * 1009;
	*edge = e;
	if (e < 0.0) {
		return -0.0068 - room_noise(x * 320.0, y * 320.0) * 0.0014;
	}
	return -0.0042 * (1.0 - smoothstepf(0.0, 0.0065, e))
		+ (room_noise(x * 64.0 + *id, y * 64.0) - 0.5) * 0.0012;
}

static void lk_brick(vec3 p, struct lk_mat *m)
{
	int id, i2;
	double edge, e2;
	const double e = 0.0007;
	double h0 = lk_brick_h(p.x, p.y, &id, &edge);
	double hx = lk_brick_h(p.x + e, p.y, &i2, &e2), hy = lk_brick_h(p.x, p.y + e, &i2, &e2);
	m->n = vnorm(v3(-(hx - h0) / e, -(hy - h0) / e, 1.0));
	if (edge < 0.0) {
		m->albedo = vmul(v3(0.27, 0.255, 0.235), 0.80 + 0.35 * room_noise(p.x * 200.0, p.y * 200.0));
		m->rough = 0.95;
		m->sub = 1;
		m->ao = 0.70;
	} else {
		double s0 = room_seed(id, 21), s1 = room_seed(id, 22);
		vec3 c = vmix(v3(0.25, 0.085, 0.050), v3(0.40, 0.165, 0.085), s0);
		if (s1 > 0.84) {
			c = vmul(c, 0.55);
		}
		c = vmul(c, 0.78 + 0.40 * room_noise(p.x * 48.0 + id, p.y * 48.0));
		double speck = room_noise(p.x * 260.0, p.y * 260.0);
		c = vmul(c, 1.0 - 0.45 * smoothstepf(0.78, 0.95, speck));
		m->albedo = c;
		m->rough = 0.82;
		m->sub = 0;
		m->ao = 0.85 + 0.15 * smoothstepf(0.0, 0.010, edge);
	}
	/* the casing standing off the wall beside it */
	double dx = fabs(p.x) - (LK_OW + LK_CW), dy = p.y - (LK_OH + LK_CW);
	double dc = dx > 0.0 && dy > 0.0 ? hypot(dx, dy) : fmax(dx, dy);
	if (dc > 0.0) {
		m->ao *= 1.0 - 0.40 * exp(-dc / 0.022);
	}
}

static double lk_casing_h(double t)
{
	double h = -0.006 * (1.0 - smoothstepf(0.0, 0.07, t));
	h += 0.0028 * exp(-lk_sq((t - 0.15) / 0.035));
	h -= 0.0020 * exp(-lk_sq((t - 0.25) / 0.020));
	h += 0.0060 * smoothstepf(0.80, 0.86, t);
	h -= 0.0090 * lk_sq(smoothstepf(0.93, 1.0, t));
	return h;
}

static void lk_casing(const struct lk_hit *h, vec3 p, struct lk_mat *m)
{
	m->albedo = vmul(v3(0.60, 0.555, 0.48), 0.95 + 0.06 * room_noise(p.x * 18.0 + p.y * 2.0,
		p.y * 260.0 + p.x * 9.0));
	m->rough = 0.42;
	m->coat = 0.25;
	m->ao = 1.0;
	m->n = h->n;
	if (h->face != 5) {
		if (h->face >> 1 == 0 && fabs(p.x) < LK_OW + 0.001) {
			/* the side facing into the doorway: the jamb, coming forward */
			m->ao = 1.0 - 0.45 * exp(-fabs(p.z - LK_DZ) / 0.015);
		}
		return;
	}
	double tx = (fabs(p.x) - LK_OW) / LK_CW, ty = (p.y - LK_OH) / LK_CW;
	bool side = tx > ty;
	double t = side ? tx : ty;
	const double e = 0.004;
	double dh = (lk_casing_h(t + e) - lk_casing_h(t - e)) / (2.0 * e) / LK_CW;
	m->n = side ? vnorm(v3(-dh * (p.x < 0.0 ? -1.0 : 1.0), 0.0, 1.0)) : vnorm(v3(0.0, -dh, 1.0));
	m->ao = 0.80 + 0.20 * smoothstepf(0.0, 0.25, t);
	m->sub = t < 0.3 ? 1 : 0;
}

static void lk_sign_face(const struct lk_scene *sc, const struct lk_hit *h, struct lk_mat *m)
{
	double u = (h->q.x + LK_SIGN_W * 0.5) / LK_SIGN_W, v = 1.0 - h->q.y / LK_SIGN_H;
	double a, ga;
	m->albedo = lk_tex_at(&sc->sign, u, v, false, &a);
	if (a < 0.5) {
		m->albedo = v3(0.20, 0.11, 0.05);
	}
	lk_tex_at(&sc->gold, u, v, false, &ga);
	/* the gilt stands up off the board a little */
	double du = 1.5 / fmax(sc->gold.w, 1), dv = 1.5 / fmax(sc->gold.h, 1);
	double gx, gy;
	lk_tex_at(&sc->gold, u + du, v, false, &gx);
	lk_tex_at(&sc->gold, u, v + dv, false, &gy);
	double sx = (gx - ga) / (du * LK_SIGN_W) * 0.0006, sy = (gy - ga) / (dv * LK_SIGN_H) * 0.0006;
	vec3 nl = vnorm(v3(-sx, sy, 1.0));
	m->n = lk_door_dir_out(sc, lk_sign_dir_out(nl));
	m->albedo = vmix(m->albedo, v3(0.98, 0.74, 0.34), ga);
	m->metal = ga;
	m->rough = 0.42 - 0.20 * ga;
	m->coat = 0.45 * (1.0 - ga);
	m->ao = 1.0;
	m->sub = ga > 0.5 ? 1 : 0;
}

static void lk_plate_face(const struct lk_scene *sc, const struct lk_hit *h, struct lk_mat *m)
{
	double x = h->q.x, y = h->q.y;
	m->metal = 1.0;
	m->albedo = vmul(v3(0.93, 0.70, 0.36), 0.90 + 0.10 * room_noise(x * 18.0, y * 1400.0));
	m->rough = 0.30 + 0.10 * room_noise(x * 6.0, y * 900.0);
	m->ao = 1.0;
	vec3 nl = lk_face_n(h->face);
	if (h->face == 5) {
		/* a bevel round the edge */
		double be = fmin(fmin(x - LK_PL_X0, LK_PL_X1 - x), fmin(y - LK_PL_Y0, LK_PL_Y1 - y));
		if (be < 0.0022) {
			double s = 1.0 - be / 0.0022;
			double ex = x - LK_PL_X0 < LK_PL_X1 - x ? -1.0 : 1.0, ey = y - LK_PL_Y0 < LK_PL_Y1 - y ? -1.0 : 1.0;
			bool xs = fmin(x - LK_PL_X0, LK_PL_X1 - x) < fmin(y - LK_PL_Y0, LK_PL_Y1 - y);
			nl = vnorm(v3(xs ? ex * s : 0.0, xs ? 0.0 : ey * s, 1.0));
			m->sub = 2;
		}
		if (x > LK_WIN_X0 && x < LK_WIN_X1 && y > LK_WIN_Y0 && y < LK_WIN_Y1) {
			/* the slot: black glass, its edge going down into it */
			double we = fmin(fmin(x - LK_WIN_X0, LK_WIN_X1 - x), fmin(y - LK_WIN_Y0, LK_WIN_Y1 - y));
			m->metal = 0.0;
			m->albedo = v3(0.010, 0.008, 0.007);
			m->rough = 0.07;
			m->coat = 0.6;
			m->ao = 0.45 + 0.55 * smoothstepf(0.0, 0.006, we);
			m->sub = 3;
			if (we < 0.0016) {
				bool xs = fmin(x - LK_WIN_X0, LK_WIN_X1 - x) < fmin(y - LK_WIN_Y0, LK_WIN_Y1 - y);
				double ex = x - LK_WIN_X0 < LK_WIN_X1 - x ? 1.0 : -1.0;
				double ey = y - LK_WIN_Y0 < LK_WIN_Y1 - y ? 1.0 : -1.0;
				nl = vnorm(v3(xs ? ex : 0.0, xs ? 0.0 : ey, 0.9));
				m->metal = 1.0;
				m->albedo = v3(0.80, 0.60, 0.30);
				m->rough = 0.35;
				m->coat = 0.0;
			}
		} else {
			double u = (x - LK_PL_X0) / (LK_PL_X1 - LK_PL_X0), v = (LK_PL_Y1 - y) / (LK_PL_Y1 - LK_PL_Y0);
			double ea;
			lk_tex_at(&sc->plate, u, v, false, &ea);
			if (ea > 0.02) {
				m->albedo = vmix(m->albedo, v3(0.16, 0.10, 0.045), ea * 0.85);
				m->rough = fmin(1.0, m->rough + ea * 0.35);
				m->sub = 1;
			}
		}
	}
	m->n = lk_door_dir_out(sc, nl);
}

static vec3 lk_room_colour(const struct lk_scene *sc, vec3 p, vec3 n, int kind, int *sub);

/* The weather on what a ray met, outside. Rain blown in on the wall, the
 * step shining and in puddles, the iron of the lantern wet; the door,
 * under the eave, dry. Snow on whatever faces up -- the lantern's roof,
 * the top of the board, the ledge every course of brick makes over the
 * mortar under it. */
static void lk_weather_on(const struct lk_scene *sc, const struct lk_hit *h, vec3 p,
	struct lk_mat *m)
{
	const struct lk_rig *g = &sc->rig;
	if (g->wet > 0.0) {
		double w = 0.0;
		switch (h->id) {
		case LK_WALL_ID:
			w = g->wet * (0.50 + 0.50 * smoothstepf(1.9, 0.5, p.y))
				* (0.70 + 0.30 * room_noise(p.x * 6.0, p.y * 2.5));
			break;
		case LK_STEP:
			w = p.z > 0.0 ? g->wet : 0.0;
			break;
		case LK_CASING:
			w = g->wet * 0.30;
			break;
		case LK_IRON:
		case LK_LGLASS:
			w = g->wet * 0.80;
			break;
		default:
			break;
		}
		if (w > 0.0) {
			m->albedo = vmul(m->albedo, 1.0 - 0.40 * w);
			m->rough += (0.16 - m->rough) * w * 0.85;
			m->coat = fmax(m->coat, 0.50 * w);
			if (h->id == LK_STEP) {
				double pd = smoothstepf(0.46, 0.60, room_noise(p.x * 3.0 + 7.0, p.z * 3.0));
				m->rough += (0.03 - m->rough) * pd;
				m->albedo = vmul(m->albedo, 1.0 - 0.45 * pd);
				m->coat = fmax(m->coat, pd);
				m->n = vnorm(vmix(m->n, v3(0, 1, 0), pd));
			}
		}
	}
	if (g->snow > 0.0) {
		/* on the wall, only the very lip of each course */
		double ny = h->id == LK_WALL_ID ? (m->n.y - 0.22) * 4.0 : h->n.y;
		double up = smoothstepf(0.40, 0.80, ny);
		if (up > 0.0) {
			double drift = room_noise(p.x * 23.0 + p.z * 11.0, p.z * 23.0 + p.y * 9.0);
			double cover = up * smoothstepf(0.15, 0.45, g->snow * (0.55 + 0.6 * drift));
			if (cover > 0.0) {
				m->albedo = vmix(m->albedo, v3(0.86, 0.88, 0.93), cover);
				m->rough += (0.80 - m->rough) * cover;
				m->metal *= 1.0 - cover;
				m->coat *= 1.0 - cover;
				m->emit = vmul(m->emit, 1.0 - cover);
				m->n = vnorm(vmix(m->n, v3(0, 1, 0), cover * 0.6));
			}
		}
	}
}

/* ---- dressed for the day: what it is made of ---- */

static const vec3 LKD_BULB_COL[LKG_N] = {
	{ 1.00, 0.10, 0.05 }, { 1.00, 0.50, 0.07 }, { 0.10, 0.78, 0.16 }, { 0.12, 0.30, 1.00 },
	{ 1.00, 0.78, 0.50 },
};
static const vec3 LKB_COL[5] = {
	{ 0.62, 0.025, 0.035 }, { 0.80, 0.50, 0.09 }, { 0.035, 0.16, 0.60 }, { 0.80, 0.21, 0.40 },
	{ 0.80, 0.80, 0.82 },
};
/* Halloween's lights, orange and purple in turn */
static const vec3 LKH_BULB_COL[2] = { { 1.00, 0.10, 0.00 }, { 0.20, 0.02, 1.00 } };
/* the leaves of the fall: crimson, scarlet, orange, amber, gold, brown */
static const vec3 LKL_COL[6] = {
	{ 0.36, 0.024, 0.020 }, { 0.56, 0.060, 0.018 }, { 0.70, 0.18, 0.020 },
	{ 0.68, 0.33, 0.032 }, { 0.60, 0.46, 0.065 }, { 0.20, 0.075, 0.024 },
};
/* the pumpkins: the jack-o'-lantern, then orange, a white one, a golden one */
static const vec3 LKP_COL[4] = {
	{ 0.78, 0.24, 0.018 }, { 0.72, 0.22, 0.016 }, { 0.70, 0.64, 0.48 }, { 0.78, 0.42, 0.030 },
};
/* what the light inside the jack-o'-lantern is */
static const vec3 LKJ_LIGHT = { 1.00, 0.55, 0.14 };

/* the colour of the light k along a run of the garland */
static vec3 lkd_bulb_col(unsigned decor, int run, int k)
{
	if ((decor & DESK_DECOR_HALLOWEEN) && !(decor & DESK_DECOR_XMAS)) {
		return LKH_BULB_COL[(k + run) & 1];
	}
	return LKD_BULB_COL[(k + run * 2) % LKG_N];
}

/* A pumpkin's skin: darker and greener down the grooves, streaked up the
 * ribs, freckled; green still round the stalk. */
static void lkd_pumpkin_paint(struct lk_mat *m, const struct lkd_pumpkin *pk, vec3 base, vec3 q)
{
	vec3 pp = lkd_pumpkin_local(pk, q);
	double ang = lkd_atan2(pp.z, pp.x);
	double groove = 1.0 - fabs(sin(ang * pk->ribs * 0.5));
	groove *= groove;
	vec3 col = vmix(base, vmul3(base, v3(0.62, 0.58, 0.36)), 0.65 * groove);
	col = vmul(col, 0.86 + 0.24 * room_noise(ang * 30.0, pp.y / pk->ry * 8.0));
	double crown = pp.y > 0.0 ? exp(-(pp.x * pp.x + pp.z * pp.z) / (pk->rx * pk->rx) * 6.0) : 0.0;
	col = vmix(col, v3(0.20, 0.18, 0.05), 0.45 * crown);
	col = vmix(col, vmul(col, 1.25), 0.6 * smoothstepf(0.78, 0.84, lkd_noise3(vmul(q, 620.0))));
	m->albedo = col;
	m->rough = 0.40;
	m->coat = 0.28;
	m->ao *= 1.0 - 0.30 * groove;
}

static void lkd_stem_paint(struct lk_mat *m, vec3 q)
{
	m->albedo = vmix(v3(0.11, 0.095, 0.045), v3(0.26, 0.21, 0.10), lkd_noise3(vmul(q, 900.0)));
	m->rough = 0.72;
}

/* Inside a jack-o'-lantern (DM_GLOW), lit by the light standing on its
 * floor; or the cut edge of its wall (DM_FLESH), lit through from inside. */
static void lkd_jack_lit(struct lk_mat *m, const struct lkd_pumpkin *pk, vec3 q, int mat)
{
	vec3 pp = lkd_pumpkin_local(pk, q);
	if (mat == DM_GLOW) {
		double dl = vlen(vsub(pp, v3(0.0, -0.030, 0.0)));
		m->emit = vmul(LKJ_LIGHT, 0.45 + 1.1 / (1.0 + dl * dl / (0.035 * 0.035)));
		m->albedo = v3(0.05, 0.03, 0.01);
		m->rough = 0.6;
		return;
	}
	double depth = clampf(-lkd_pumpkin_skin(pp, LKJ_RX, LKJ_RY, LKJ_RIBS) / LKJ_SHELL, 0.0, 1.0);
	m->albedo = v3(0.80, 0.56, 0.20);
	m->emit = vmul(LKJ_LIGHT, 0.08 + 0.80 * depth * depth);
	m->rough = 0.55;
}

/* A leaf, as shading: its colour blotched and greener toward the stalk,
 * the veins paler, the edge dried brown and a dark spot here and there;
 * the back of it paler and duller. */
static void lkd_leaf_skin(struct lk_mat *m, const struct lkd_leaf *l, vec3 q)
{
	vec3 p = lkd_leaf_local(l, q);
	double R = l->r, vein, edge, from;
	if (l->kind == LKL_MAPLE) {
		/* the veins from the point out along each lobe */
		double r = sqrt(p.x * p.x + p.y * p.y), th = lkd_atan2(p.y, p.x);
		double n = clampf(round(th / 0.95), -2.0, 2.0);
		double off = r * sin(th - n * 0.95), w = 0.00045 + 0.00055 * (1.0 - clampf(r / R, 0.0, 1.0));
		vein = cos(th - n * 0.95) > 0.0 ? exp(-off * off / (w * w)) : 0.0;
		double e = R * lkd_maple_edge(th, true);
		edge = clampf((e - r) / (0.15 * R), 0.0, 1.0);
		from = clampf(r / fmax(e, 1e-4), 0.0, 1.0);
	} else {
		/* the midrib, and the veins off it each way toward the tip */
		double xx = p.x - R, w = 0.0004 + 0.0005 * (1.0 - fabs(xx) / R);
		double sv = (p.x - fabs(p.y) * 1.4) / (0.30 * R);
		double sf = (sv - floor(sv + 0.5)) * 0.30 * R;
		vein = fmax(exp(-p.y * p.y / (w * w)), 0.7 * exp(-sf * sf / (0.36 * w * w)));
		double rv = 0.5 * R * (0.40 + 1.0 / 0.40), dv = rv - 0.40 * R;
		double half = sqrt(fmax(rv * rv - xx * xx, 0.0)) - dv;
		edge = clampf((half - fabs(p.y)) / (0.12 * R), 0.0, 1.0);
		from = clampf(p.x / (2.0 * R), 0.0, 1.0);
	}
	vec3 col = LKL_COL[l->col];
	double blot = lkd_noise3(vadd(vmul(q, 60.0), v3(1.3, 4.1, 2.7)));
	col = vmul(col, 0.80 + 0.30 * blot + 0.10 * lkd_noise3(vmul(q, 210.0)));
	if (l->col != 5) {
		col = vmix(col, v3(0.42, 0.36, 0.05), 0.30 * (1.0 - from) * (1.0 - from));
	}
	col = vmix(col, vadd(vmul(col, 1.15), v3(0.06, 0.05, 0.01)), 0.55 * vein);
	col = vmix(col, v3(0.12, 0.050, 0.020), 0.38 * (1.0 - edge));
	double spot = lkd_noise3(vadd(vmul(q, 330.0), v3(7.7, 0.0, 3.3)));
	col = vmix(col, v3(0.045, 0.028, 0.015), 0.70 * smoothstepf(0.86, 0.90, spot));
	bool back = p.z < 0.0;
	if (back) {
		double luma = 0.3 * col.x + 0.6 * col.y + 0.1 * col.z;
		col = vmul(vmix(col, v3(luma, luma, luma), 0.25), 1.15);
	}
	m->albedo = col;
	m->rough = back ? 0.70 : 0.50;
	m->coat = back ? 0.0 : 0.12;
}

/* Needles, as shading. Each sprig lays its needles its own way across the
 * surface; their rows tilt the normal this way and that, dark down in the
 * crowd of them, the new growth lighter at the tips. q: the point in the
 * group's frame; tf, bf: two ways along the surface there, and tw, bw the
 * same in the world; lump: how far out it stands. */
static void lkd_needles(struct lk_mat *m, vec3 q, vec3 tf, vec3 bf, vec3 tw, vec3 bw,
	double lump, double dark)
{
	double ang = lkd_noise3(vmul(q, 30.0)) * 9.4;
	double c = cos(ang), s = sin(ang);
	vec3 dirf = vadd(vmul(tf, c), vmul(bf, s)), crossf = vadd(vmul(tf, -s), vmul(bf, c));
	vec3 dirw = vadd(vmul(tw, c), vmul(bw, s)), crossw = vadd(vmul(tw, -s), vmul(bw, c));
	double u = vdot(q, dirf), v = vdot(q, crossf);
	double row = sin(v * 1500.0 + 9.0 * lkd_noise3(vmul(q, 140.0)));
	double brk = lkd_noise3(v3(u * 160.0, v * 1100.0, 0.37));
	m->n = vnorm(vadd(m->n, vadd(vmul(crossw, 0.55 * row), vmul(dirw, 0.35 * (brk - 0.5)))));
	double tone = lkd_noise3(vadd(vmul(q, 55.0), v3(2.2, 7.1, 4.4)));
	vec3 col = vmix(v3(0.010, 0.030, 0.014), v3(0.040, 0.095, 0.038), tone);
	double tip = smoothstepf(0.55, 0.95, lump) * smoothstepf(0.1, 0.9, brk);
	col = vmix(col, v3(0.085, 0.150, 0.050), tip * 0.75);
	col = vmul(col, 0.70 + 0.30 * (0.5 + 0.5 * row));
	m->albedo = vmul(col, dark);
	m->rough = 0.60;
	m->coat = 0.06;
	m->ao *= 0.40 + 0.60 * smoothstepf(0.1, 0.9, lump);
}

/* a little noise of three numbers, for a surface that glitters */
static vec3 lkd_jitter(vec3 q, double scale)
{
	int x = (int)floor(q.x * scale), y = (int)floor(q.y * scale), z = (int)floor(q.z * scale);
	return v3(room_hash(x * 3 + z, y) - 0.5, room_hash(y * 5 + x, z + 7) - 0.5,
		room_hash(z * 7 + y, x + 13) - 0.5);
}

static void lkd_material(const struct lk_scene *sc, const struct lk_hit *h, vec3 p,
	struct lk_mat *m)
{
	const struct lkd_group *g = &sc->deco[h->face];
	vec3 q = h->q;
	int mat, part;
	(void)p;
	m->ao = lkd_ao(sc, p, h->n);
	m->rough = 0.5;
	switch (h->sub) {
	case DM_NEEDLE:
		if (g->kind == LKD_WREATH) {
			double a, b;
			lkd_ring(q, &a, &b);
			vec3 tf = v3(-sin(a), cos(a), 0.0);
			vec3 bf = v3(-sin(b) * cos(a), -sin(b) * sin(a), cos(b));
			lkd_needles(m, q, tf, bf, lk_door_dir_out(sc, tf), lk_door_dir_out(sc, bf),
				lkd_fir(q), 1.0);
		} else {
			int run;
			double s;
			vec3 off;
			lkd_run(q, &run, &s, &off);
			vec3 t = run == 1 ? v3(1, 0, 0) : v3(0, 1, 0);
			vec3 bt = vnorm(vcross(t, vlen(off) > 1e-6 ? off : v3(0, 0, 1)));
			lkd_needles(m, q, t, bt, t, bt, lkd_fir(q), 1.0);
		}
		break;
	case DM_BERRY:
		/* holly's red, or bittersweet's orange */
		m->albedo = sc->wreath == LKD_AUTUMN ? v3(0.74, 0.20, 0.022) : v3(0.60, 0.022, 0.032);
		m->rough = 0.10;
		m->coat = 1.0;
		break;
	case DM_CONE: {
		double sc1 = sin(q.x * 820.0 + q.y * 410.0) * sin(q.y * 820.0 - q.x * 410.0);
		m->albedo = vmul(v3(0.21, 0.11, 0.050), 0.55 + 0.45 * fabs(sc1));
		m->rough = 0.78;
		m->ao *= 0.75 + 0.25 * fabs(sc1);
		break;
	}
	case DM_BOW:
		if (sc->wreath == LKD_TINSEL) {
			m->albedo = v3(0.86, 0.64, 0.30);
			m->metal = 0.65;
			m->rough = 0.30;
		} else {
			m->albedo = v3(0.50, 0.020, 0.030);
			m->rough = 0.34;
			m->coat = 0.55;
		}
		break;
	case DM_BULB: {
		lkd_sd(sc, g, q, &mat, &part);
		int run = part < 0 ? 0 : part % 3, k = part < 0 ? 0 : part / 3;
		vec3 col = lkd_bulb_col(sc->rig.decor, run, k);
		vec3 c = lkd_bulb_at(run, k);
		if (q.z - c.z < -0.0045) {
			/* its socket, in the green -- or black, among the leaves */
			m->albedo = sc->wreath == LKD_AUTUMN ? v3(0.012, 0.011, 0.010) : v3(0.020, 0.045, 0.020);
			m->rough = 0.4;
			break;
		}
		m->emit = vmul(col, 1.9);
		m->albedo = vmul(col, 0.05);
		m->rough = 0.08;
		m->coat = 0.9;
		break;
	}
	case DM_TINSEL:
		m->n = vnorm(vadd(m->n, vmul(lkd_jitter(q, 380.0), 1.6)));
		m->albedo = v3(0.95, 0.70, 0.27);
		m->metal = 1.0;
		m->rough = 0.16;
		break;
	case DM_BAUBLE: {
		double a, b, ak;
		lkd_ring(q, &a, &b);
		int k = lkd_slot(a, &ak);
		m->albedo = (k & 1) ? v3(0.90, 0.91, 0.93) : v3(0.95, 0.72, 0.32);
		m->metal = 1.0;
		m->rough = 0.06;
		break;
	}
	case DM_LEAF: {
		lkd_sd(sc, g, q, &mat, &part);
		int n = g->kind == LKD_WREATH ? LKA_N * LKA_ROWS : (2 * LKV_N0 + LKV_N1) * LKV_ROWS;
		if (part < 0 || part >= n) {
			m->albedo = LKL_COL[2];
			break;
		}
		lkd_leaf_skin(m, g->kind == LKD_WREATH ? &g_lka[part] : &g_lkv[part], q);
		break;
	}
	case DM_VINE:
		/* grapevine, grey-brown and stringy */
		m->albedo = vmix(v3(0.050, 0.036, 0.026), v3(0.14, 0.10, 0.068),
			lkd_noise3(vmul(q, 420.0)));
		m->rough = 0.82;
		break;
	case DM_PUMPKIN: {
		lkd_sd(sc, g, q, &mat, &part);
		int i = part < 0 || part > 3 ? 0 : part;
		lkd_pumpkin_paint(m, &g_lkp[i], LKP_COL[i], q);
		break;
	}
	case DM_STEM:
		lkd_stem_paint(m, q);
		break;
	case DM_GLOW:
	case DM_FLESH:
		lkd_jack_lit(m, &g_lkp[0], q, h->sub);
		break;
	case DM_BALLOON: {
		lkd_balloons(q, &mat, &part);
		int i = part < 0 ? 0 : part;
		m->albedo = LKB_COL[i];
		m->rough = 0.17;
		m->coat = 0.95;
		if (i == 1) {
			m->metal = 0.55;
		}
		if (q.y < LKB[i].c.y - LKB[i].r * 1.12) {
			m->rough = 0.45;
			m->coat = 0.2;
		}
		break;
	}
	default:
		m->albedo = v3(0.3, 0.3, 0.3);
		break;
	}
}

/* ---- Halloween on the bookcase ----
 *
 * On the day, the lower board of the bookcase has a jack-o'-lantern on it
 * too, right of the clock, with two little pumpkins and a few leaves come
 * down. They are not in the bake -- a room is traced once and kept, and
 * tracing it again for a day would be a long wait at a login -- but traced
 * onto the picture of the case once it is painted (shelf_base_ensure),
 * through the case's own camera, under the room's own lights: the lamp on
 * the desk, the pair under the upper board, the light all round. Where
 * they stand the board is the bake's, with what they do to it worked out
 * against what it had: their shadows, how they shut it in, and the light
 * out of the face. In metres, from the board's front edge: x across from
 * the middle of the case, y up off the board, z out. */
static struct {
	struct lkd_pumpkin pk[3];   /* the jack-o'-lantern, then two little ones */
	struct lkd_leaf leaf[3];
	vec3 bc[6];                 /* a sphere round each of them */
	double br[6];
} g_shf;
static const vec3 SHF_COL[3] = { { 0.78, 0.24, 0.018 }, { 0.75, 0.30, 0.020 }, { 0.70, 0.64, 0.48 } };
#define SHF_X0 0.278        /* where they make a difference: from right of the clock */
#define SHF_X1 0.640
#define SHF_Z0 (-0.270)     /* what stands there, from short of the back */
#define SHF_TOP 0.130       /* up to the jack-o'-lantern's stalk */
#define SHF_BACK ((RM_SH_BACKT - RM_SH_D) * LK_RM_UNIT)   /* the case's back, inside */
#define SHF_UP 0.200        /* as high up it as their shadows go */

static vec3 shf_from_room(vec3 p)
{
	return v3(p.x * LK_RM_UNIT, (p.y - RM_SH_BAY_B) * LK_RM_UNIT, (p.z - RM_SH_ZF) * LK_RM_UNIT);
}

static vec3 shf_to_room(vec3 q)
{
	return v3(q.x / LK_RM_UNIT, q.y / LK_RM_UNIT + RM_SH_BAY_B, q.z / LK_RM_UNIT + RM_SH_ZF);
}

/* the jack-o'-lantern looking round toward the eye, and a little up */
static void shf_make(const struct room_cam *cam)
{
	vec3 c = v3(0.392, LKJ_RY * 0.97, -0.100);
	vec3 h = vsub(shf_from_room(cam->eye), c);
	h.y = 0.0;
	h = vnorm(h);
	lkd_pumpkin_put(&g_shf.pk[0], c, vsub(v3(0.0, 1.0, 0.0), vmul(h, 0.22)), h, LKJ_RX, LKJ_RY,
		LKJ_RIBS);
	lkd_pumpkin_put(&g_shf.pk[1], v3(0.497, 0.031 * 0.97, -0.052), v3(0.08, 1.0, 0.05),
		v3(0.0, 0.0, 1.0), 0.040, 0.031, 8);
	lkd_pumpkin_put(&g_shf.pk[2], v3(0.322, 0.028 * 0.97, -0.050), v3(-0.06, 1.0, 0.03),
		v3(0.0, 0.0, 1.0), 0.035, 0.028, 8);
	/* fallen, curled up a little at the edges and the tip */
	static const struct { double x, z, a, r, fold, curl; int kind, col; } L[3] = {
		{ 0.448, -0.024, 2.6, 0.040, 0.18, 0.22, LKL_MAPLE, 1 },
		{ 0.358, -0.010, -0.5, 0.036, 0.12, 0.30, LKL_MAPLE, 3 },
		{ 0.300, -0.185, 0.4, 0.030, 0.10, 0.20, LKL_PLAIN, 4 },
	};
	for (int i = 0; i < 3; i++) {
		struct lkd_leaf *l = &g_shf.leaf[i];
		bool maple = L[i].kind == LKL_MAPLE;
		l->c = v3(L[i].x, 0.0012, L[i].z);
		l->u = v3(cos(L[i].a), 0.0, sin(L[i].a));
		l->w = v3(0.0, 1.0, 0.0);
		l->v = vcross(l->w, l->u);
		l->r = L[i].r;
		l->fold = L[i].fold;
		l->curl = L[i].curl;
		l->bow = 0.08;
		l->kind = L[i].kind;
		l->col = L[i].col;
		l->m = vadd(l->c, vmul(l->u, maple ? 0.40 * l->r : l->r));
		l->mr = (maple ? 0.84 : 1.04) * l->r + 0.002;
		g_shf.bc[3 + i] = l->m;
		g_shf.br[3 + i] = l->mr;
	}
	for (int i = 0; i < 3; i++) {
		g_shf.bc[i] = g_shf.pk[i].c;
		g_shf.br[i] = g_shf.pk[i].rx * 1.45;
	}
}

/* whether the way from p toward l (as far as dist) passes within reach of
 * any of them */
static bool shf_near_way(vec3 p, vec3 l, double dist, double reach)
{
	for (int i = 0; i < 6; i++) {
		vec3 w = vsub(g_shf.bc[i], p);
		double s = clampf(vdot(w, l), 0.0, dist);
		vec3 off = vsub(w, vmul(l, s));
		if (vdot(off, off) < lk_sq(g_shf.br[i] + reach)) {
			return true;
		}
	}
	return false;
}

/* for a shadow, or how shut in something is: the pumpkins as plain
 * shapes, the leaves without their teeth (set by whoever asks, on its own
 * thread) */
static _Thread_local bool g_shf_rough;

/* what is on the board, and which: part 0..2 the pumpkins, 3..5 the leaves */
static double shf_sd(vec3 q, int *mat, int *part)
{
	double d = 1e9;
	*mat = DM_PUMPKIN;
	*part = 0;
	for (int i = 0; i < 3; i++) {
		const struct lkd_pumpkin *pk = &g_shf.pk[i];
		double far = vlen(vsub(q, pk->c)) - pk->rx * 1.45;
		int m = DM_PUMPKIN;
		double e = far > 0.02 ? far
			: g_shf_rough ? lkd_ellipsoid(lkd_pumpkin_local(pk, q), v3(pk->rx, pk->ry, pk->rx))
			: i == 0 ? lkd_jack_body(lkd_pumpkin_local(pk, q), &m) : lkd_pumpkin_sd(pk, q, &m);
		if (e < d) {
			d = e;
			*mat = m;
			*part = i;
		}
	}
	for (int i = 0; i < 3; i++) {
		const struct lkd_leaf *l = &g_shf.leaf[i];
		if (vlen(vsub(q, l->m)) - l->mr < d) {
			double e = lkd_leaf_sd(l, q, d);
			if (e < d) {
				d = e;
				*mat = DM_LEAF;
				*part = 3 + i;
			}
		}
	}
	return d;
}

static vec3 shf_normal(vec3 q)
{
	const double e = 0.00025;
	int m, pt;
	vec3 k1 = v3(1, -1, -1), k2 = v3(-1, -1, 1), k3 = v3(-1, 1, -1), k4 = v3(1, 1, 1);
	return vnorm(vadd(vadd(vmul(k1, shf_sd(vadd(q, vmul(k1, e)), &m, &pt)),
		vmul(k2, shf_sd(vadd(q, vmul(k2, e)), &m, &pt))),
		vadd(vmul(k3, shf_sd(vadd(q, vmul(k3, e)), &m, &pt)),
		vmul(k4, shf_sd(vadd(q, vmul(k4, e)), &m, &pt)))));
}

/* how much of a light toward l (as far as dist) they let through to p, softly */
static double shf_soft(vec3 p, vec3 l, double dist, double k)
{
	double t0, t1;
	/* nothing near the way, or so far along it the penumbra cannot reach */
	if (!shf_near_way(p, l, dist, fmin(dist / k, 0.03)) || !lkd_slab(p, l, v3(SHF_X0 - 0.02, 0.0, SHF_Z0 - 0.02), v3(SHF_X1 + 0.02, SHF_TOP + 0.01, 0.02),
			dist, &t0, &t1)) {
		return 1.0;
	}
	double res = 1.0, t = fmax(t0, 0.002);
	g_shf_rough = g_lkd_rough = true;
	for (int n = 0; n < 48 && t < t1; n++) {
		int m, pt;
		double h = shf_sd(vadd(p, vmul(l, t)), &m, &pt);
		if (h < 0.0002) {
			res = 0.0;
			break;
		}
		res = fmin(res, k * h / t);
		t += clampf(h, 0.0015, 0.04);
	}
	g_shf_rough = g_lkd_rough = false;
	res = clampf(res, 0.0, 1.0);
	return res * res * (3.0 - 2.0 * res);
}

/* how shut in p is, by them and the board under them */
static double shf_ao(vec3 p, vec3 n)
{
	static const double D[4] = { 0.006, 0.016, 0.032, 0.060 };
	static const double W[4] = { 0.30, 0.26, 0.22, 0.16 };
	/* none of them in reach: open (the board never shuts in the board) */
	bool near = false;
	for (int i = 0; i < 6 && !near; i++) {
		near = vlen(vsub(p, g_shf.bc[i])) - g_shf.br[i] < 0.065;
	}
	if (!near) {
		return 1.0;
	}
	double occ = 0.0;
	g_shf_rough = g_lkd_rough = true;
	for (int j = 0; j < 4; j++) {
		int m, pt;
		vec3 q = vadd(p, vmul(n, D[j]));
		double h = fmin(shf_sd(q, &m, &pt), fmax(q.y, 0.0) + 0.02);
		occ += clampf((D[j] - h) / D[j], 0.0, 1.0) * W[j];
	}
	g_shf_rough = g_lkd_rough = false;
	return 1.0 - clampf(occ, 0.0, 1.0) * 0.80;
}

static bool shf_trace(vec3 o, vec3 d, double tmax, double *th, int *mat, int *part)
{
	/* only between the first sphere round one of them it goes into and the
	 * last it comes out of, if it goes into any */
	double t = 1e9, t1 = -1.0;
	for (int i = 0; i < 6; i++) {
		vec3 w = vsub(g_shf.bc[i], o);
		double s = vdot(w, d), h2 = vdot(w, w) - s * s, r2 = g_shf.br[i] * g_shf.br[i];
		if (h2 < r2) {
			double half = sqrt(r2 - h2);
			t = fmin(t, s - half);
			t1 = fmax(t1, s + half);
		}
	}
	t = fmax(t, 0.0);
	t1 = fmin(t1, tmax);
	for (int n = 0; n < 200 && t < t1; n++) {
		double e = shf_sd(vadd(o, vmul(d, t)), mat, part);
		if (e < 0.00012) {
			*th = t;
			return true;
		}
		t += fmax(e * 0.85, 0.00008);
	}
	return false;
}

/* one of them, as the room's lights have it (shade_surface, but for the
 * room's own shadows and how it shuts things in, which the bake has and
 * these are clear of) */
static vec3 shf_shade(vec3 q, vec3 d, int mat, int part)
{
	struct lk_mat m;
	memset(&m, 0, sizeof m);
	m.ao = 1.0;
	m.n = shf_normal(q);
	if (part >= 3) {
		lkd_leaf_skin(&m, &g_shf.leaf[part - 3], q);
	} else if (mat == DM_GLOW || mat == DM_FLESH) {
		lkd_jack_lit(&m, &g_shf.pk[0], q, mat);
	} else if (mat == DM_STEM) {
		lkd_stem_paint(&m, q);
	} else {
		lkd_pumpkin_paint(&m, &g_shf.pk[part], SHF_COL[part], q);
	}
	vec3 pu = shf_to_room(q), v = vmul(d, -1.0), n = m.n;
	struct key_light kl = key_at(pu, ROOM_KIND);
	double nol = fmax(vdot(n, kl.dir), 0.0), nov = fmax(vdot(n, v), 1e-4);
	double ao = m.ao * shf_ao(q, n);
	double sh = nol > 0.0 ? shf_soft(vadd(q, vmul(n, 0.0008)), kl.dir, 0.5, 1.0 / fmax(kl.ang, 0.02))
		: 0.0;
	vec3 diff = vmul(m.albedo, 1.0 - m.metal);
	vec3 col = vmul(vmul3(diff, vmul(LIT_KEY_COL, kl.irr)), nol * sh / M_PI);
	col = vadd(col, vmul(vmul3(diff, sh_irradiance(n)), ao / M_PI));
	col = vadd(col, vmul(vmul3(diff, shelf_lamp_at(pu, n)), ao / M_PI));
	double ea, eb;
	env_brdf(nov, m.rough, &ea, &eb);
	vec3 f0 = vmix(v3(0.04, 0.04, 0.04), m.albedo, m.metal);
	vec3 r = reflect3(d, n);
	vec3 refl = vadd(env_prefiltered(r, m.rough), vmul(env_lamp(r, m.rough, &kl), sh));
	col = vadd(col, vmul(vmul3(refl, vadd(vmul(f0, ea), v3(eb, eb, eb))), ao));
	if (m.coat > 0.0) {
		double fr = 0.04 + 0.96 * pow(1.0 - nov, 5.0), ca, cb;
		col = vmul(col, 1.0 - fr * m.coat);
		env_brdf(nov, 0.10, &ca, &cb);
		vec3 cr = vadd(env_prefiltered(r, 0.10), vmul(env_lamp(r, 0.10, &kl), sh));
		col = vadd(col, vmul(cr, (0.04 * ca + cb) * m.coat * ao));
	}
	/* lit as bright as it is at the door, whose exposure is not the room's */
	col = vadd(col, vmul(m.emit, LK_EXPOSURE / ICON_EXPOSURE));
	return tonemap(vmul(col, ICON_EXPOSURE));
}

/* What they do to the case at b, facing n (the board, or the back): of
 * all the light that reached it before, how much still does, and what the
 * light out of the face adds. */
static vec3 shf_board(vec3 b, vec3 n)
{
	vec3 pu = shf_to_room(b);
	struct key_light kl = key_at(pu, ROOM_KIND);
	vec3 e = vmul(LIT_KEY_COL, kl.irr * fmax(vdot(n, kl.dir), 0.0));
	vec3 all = e, left = vmul(e, shf_soft(b, kl.dir, 0.5, 1.0 / fmax(kl.ang, 0.02)));
	e = sh_irradiance(n);
	all = vadd(all, e);
	left = vadd(left, vmul(e, shf_ao(b, n)));
	/* the pair under the upper board, as shelf_lamp_at, one at a time */
	for (int i = 0; i < 2; i++) {
		vec3 dl = vsub(v3(i ? 8.2 : -8.2, SHELF_LAMP_Y, RM_SH_ZF - 0.15), pu);
		double dist = fmax(vlen(dl), 0.70);
		vec3 l = vmul(dl, 1.0 / dist);
		double cone = smoothstepf(-0.05, 0.62, vdot(vmul(l, -1.0), vnorm(SHELF_LAMP_AIM)));
		e = vmul(SHELF_LAMP_COL, SHELF_LAMP_IRR * cone * fmax(vdot(n, l), 0.0) / (dist * dist));
		all = vadd(all, e);
		left = vadd(left, vmul(e, shf_soft(b, l, dist * LK_RM_UNIT, 14.0)));
	}
	/* the face's light, on the board just in front of it */
	const struct lkd_pumpkin *pk = &g_shf.pk[0];
	vec3 f = vadd(pk->c, vmul(pk->z, LKJ_RX * 0.9));
	vec3 to = vsub(b, f);
	double dist = vlen(to);
	double out = fmax(vdot(pk->z, to) / dist, 0.0), on = fmax(-vdot(n, to) / dist, 0.0);
	vec3 glow = vmul(LKJ_LIGHT, 0.0030 * out * out * on / (dist * dist + 0.0004)
		* smoothstepf(0.10, 0.03, dist));
	return v3((left.x + glow.x) / fmax(all.x, 1e-6), (left.y + glow.y) / fmax(all.y, 1e-6),
		(left.z + glow.z) / fmax(all.z, 1e-6));
}

/* the tone curve backwards: a pixel of the bake to the light it was */
static double shf_unmap(double c)
{
	double y = clampf(pow(c, 2.2), 0.0, 0.99);
	double A = 2.43 * y - 2.51, B = 0.59 * y - 0.03, C = 0.14 * y;
	return (-B - sqrt(fmax(B * B - 4.0 * A * C, 0.0))) / (2.0 * A);
}

/* One ray through the picture at sx, sy (its pixels): what it shows there,
 * against the bake's pixel (base, 0..1). 0: the bake's own, 1: the board,
 * 2 and on: the thing (2 + its part). The post box on the desk stands
 * between the eye and the right of the board: what is behind it is not
 * seen. */
static int shf_sample(const struct room_render *rr, double sx, double sy, vec3 base, vec3 *out)
{
	vec3 ro, rd;
	room_ray(&rr->shelf_cam, sx / rr->w, sy / rr->h, &ro, &rd);
	vec3 o = shf_from_room(ro);
	double tb = rd.y < -1e-6 ? -o.y / rd.y : 1e9, t, t1;
	if (lkd_slab(ro, rd, v3(RM_PB_CX - RM_PB_HX - 0.12, -0.05, RM_PB_CZ - RM_PB_HZ - 0.70),
			v3(RM_PB_CX + RM_PB_HX + 0.12, RM_PB_SHOULDER + RM_PB_CAP_T + 0.50, RM_PB_CZ + RM_PB_HZ + 0.70),
			tb / LK_RM_UNIT, &t, &t1)) {
		return 0;
	}
	/* what is behind them: the board, or further in the case's back */
	double tp = rd.z < -1e-6 ? (SHF_BACK - o.z) / rd.z : 1e9;
	bool back = tp < tb;
	tb = fmin(tb, tp);
	int mat, part;
	if (shf_trace(o, rd, tb, &t, &mat, &part)) {
		*out = shf_shade(vadd(o, vmul(rd, t)), rd, mat, part);
		return 2 + part;
	}
	vec3 b = vadd(o, vmul(rd, tb));
	if (tb >= 1e9 || b.x < SHF_X0 || b.x > SHF_X1 || b.z > 0.0 || b.y > SHF_UP) {
		return 0;
	}
	vec3 k = back ? shf_board(v3(b.x, b.y, SHF_BACK), v3(0.0, 0.0, 1.0))
		: shf_board(v3(b.x, 0.0, b.z), v3(0.0, 1.0, 0.0));
	if (fabs(k.x - 1.0) + fabs(k.y - 1.0) + fabs(k.z - 1.0) < 0.004) {
		return 0;
	}
	*out = tonemap(v3(shf_unmap(base.x) * k.x, shf_unmap(base.y) * k.y, shf_unmap(base.z) * k.z));
	return 1;
}

/* the part of the picture they make a difference to, a row at a time on
 * as many threads as the door's tracer has (lk_run) */
struct shf_job {
	const struct room_render *rr;
	const unsigned char *px;
	int stride, x0, y0, bw, bh;
	vec3 *col;
	unsigned char *what;   /* shf_sample's, at each pixel's middle */
	unsigned char *ss;     /* 1: the pixel taken again, four times over */
	int pass, next;
	pthread_mutex_t lock;
};

static void shf_row(struct shf_job *j, int y)
{
	for (int x = 0; x < j->bw; x++) {
		int i = y * j->bw + x;
		const unsigned char *p = j->px + (size_t)(j->y0 + y) * j->stride + (size_t)(j->x0 + x) * 4;
		vec3 base = v3(p[2] / 255.0, p[1] / 255.0, p[0] / 255.0);
		double sx = j->x0 + x, sy = j->y0 + y;
		if (j->pass == 0) {
			j->what[i] = (unsigned char)shf_sample(j->rr, sx + 0.5, sy + 0.5, base, &j->col[i]);
			if (!j->what[i]) {
				j->col[i] = base;
			}
			continue;
		}
		/* where what it shows is not what the pixels round it show, and
		 * one side of that is one of them: four samples instead of one */
		int a = j->what[i];
		bool edge = false;
		for (int k = 0; k < 4 && !edge; k++) {
			int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
			int b = nx >= 0 && ny >= 0 && nx < j->bw && ny < j->bh ? j->what[ny * j->bw + nx] : a;
			edge = a != b && (a | b) > 1;
		}
		if (!edge) {
			continue;
		}
		vec3 sum = v3(0, 0, 0);
		for (int k = 0; k < 4; k++) {
			vec3 c = base;
			shf_sample(j->rr, sx + SS_OFF[k][0], sy + SS_OFF[k][1], base, &c);
			sum = vadd(sum, c);
		}
		j->col[i] = vmul(sum, 0.25);
		j->ss[i] = 1;
	}
}

static void *shf_worker(void *arg)
{
	struct shf_job *j = arg;
	for (;;) {
		pthread_mutex_lock(&j->lock);
		int y = j->next++;
		pthread_mutex_unlock(&j->lock);
		if (y >= j->bh) {
			break;
		}
		shf_row(j, y);
	}
	return NULL;
}

static void shf_run(struct shf_job *j)
{
	j->next = 0;
	int n = bake_threads();
	pthread_t th[8];
	int started = 0;
	for (int i = 0; i + 1 < n; i++) {
		if (pthread_create(&th[started], NULL, shf_worker, j) == 0) {
			started++;
		}
	}
	shf_worker(j);
	for (int i = 0; i < started; i++) {
		pthread_join(th[i], NULL);
	}
}

static void shelf_fall_paint(const struct room_render *rr, cairo_surface_t *s)
{
	shf_make(&rr->shelf_cam);
	cairo_surface_flush(s);
	unsigned char *px = cairo_image_surface_get_data(s);
	int stride = cairo_image_surface_get_stride(s), W = rr->w, H = rr->h;
	double x0 = W, y0 = H, x1 = 0.0, y1 = 0.0;
	for (int k = 0; k < 8; k++) {
		vec3 c = shf_to_room(v3(k & 1 ? SHF_X1 : SHF_X0, k & 2 ? SHF_UP : 0.0, k & 4 ? 0.0 : SHF_BACK));
		double u, v;
		if (!px || !room_project(&rr->shelf_cam, c, &u, &v)) {
			return;
		}
		x0 = fmin(x0, u * W);
		x1 = fmax(x1, u * W);
		y0 = fmin(y0, v * H);
		y1 = fmax(y1, v * H);
	}
	struct shf_job j = { .rr = rr, .px = px, .stride = stride };
	j.x0 = (int)clampf(floor(x0) - 2.0, 0.0, W - 1);
	j.y0 = (int)clampf(floor(y0) - 2.0, 0.0, H - 1);
	j.bw = (int)clampf(ceil(x1) + 2.0, 0.0, W - 1) - j.x0 + 1;
	j.bh = (int)clampf(ceil(y1) + 2.0, 0.0, H - 1) - j.y0 + 1;
	size_t n = (size_t)j.bw * j.bh;
	j.col = malloc(n * sizeof *j.col);
	j.what = malloc(n);
	j.ss = calloc(n, 1);
	if (j.col && j.what && j.ss) {
		pthread_mutex_init(&j.lock, NULL);
		for (j.pass = 0; j.pass < 2; j.pass++) {
			shf_run(&j);
		}
		pthread_mutex_destroy(&j.lock);
		for (int y = 0; y < j.bh; y++) {
			for (int x = 0; x < j.bw; x++) {
				size_t i = (size_t)y * j.bw + x;
				if (!j.what[i] && !j.ss[i]) {
					continue;
				}
				/* dithered, as the trace's own pixels are */
				int X = j.x0 + x, Y = j.y0 + y;
				double dz = room_hash(X, Y + 0x7777) - 0.5;
				unsigned char *p = px + (size_t)Y * stride + (size_t)X * 4;
				p[2] = (unsigned char)clampf(j.col[i].x * 255.0 + dz + 0.5, 0.0, 255.0);
				p[1] = (unsigned char)clampf(j.col[i].y * 255.0 + dz + 0.5, 0.0, 255.0);
				p[0] = (unsigned char)clampf(j.col[i].z * 255.0 + dz + 0.5, 0.0, 255.0);
			}
		}
	}
	free(j.col);
	free(j.what);
	free(j.ss);
	cairo_surface_mark_dirty(s);
}

static void lk_material(const struct lk_scene *sc, const struct lk_hit *h, vec3 p, struct lk_mat *m)
{
	memset(m, 0, sizeof *m);
	m->n = h->n;
	m->ao = 1.0;
	m->rough = 0.5;
	switch (h->id) {
	case LK_WALL_ID:
		lk_brick(p, m);
		break;
	case LK_CASING:
		lk_casing(h, p, m);
		break;
	case LK_JAMB:
		m->albedo = v3(0.56, 0.52, 0.45);
		m->rough = 0.50;
		m->coat = 0.15;
		if (p.z > LK_DZ) {
			m->ao = 1.0 - 0.50 * exp(-(p.z - LK_DZ) / 0.014) * (1.0 - sc->open);
		} else {
			/* behind the door, in the wall: plaster */
			m->albedo = v3(0.50, 0.46, 0.40);
			m->rough = 0.90;
			m->coat = 0.0;
		}
		break;
	case LK_STEP: {
		double g = room_noise(p.x * 30.0, p.z * 30.0) * 0.6 + room_noise(p.x * 160.0, p.z * 160.0) * 0.4;
		m->albedo = vmul(v3(0.30, 0.285, 0.265), 0.75 + 0.40 * g);
		m->rough = 0.80;
		if (p.z < 0.0) {
			/* the sill: oak, worn */
			m->albedo = vmul(v3(0.34, 0.22, 0.12), 0.85 + 0.25 * room_noise(p.x * 3.0, p.z * 120.0));
			m->rough = 0.55;
		}
		break;
	}
	case LK_INSIDE: {
		/* the room round the picture of it, the picture's colours carried
		 * on along the walls, darker toward the door */
		double fall = 0.45 + 0.55 * smoothstepf(-LK_WALL, LK_ROOM_Z, p.z);
		m->emit = vmul(sc->edge[h->sub], fall);
		m->unlit = true;
		m->disp = true;
		break;
	}
	case LK_ROOMIN:
		m->emit = lk_room_colour(sc, h->q, h->n, h->sub, &m->sub);
		m->unlit = true;
		m->disp = true;
		break;
	case LK_PICTURE: {
		double u = (p.x - sc->pic_x0) / sc->pic_w, v = 1.0 - (p.y - sc->pic_y0) / sc->pic_h;
		double a;
		if (sc->pic.px && u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0) {
			m->emit = lk_tex_at(&sc->pic, u, v, true, &a);
			m->disp = true;
		} else {
			m->emit = vmul(vadd(vadd(sc->edge[0], sc->edge[1]), vadd(sc->edge[2], sc->edge[4])), 0.25);
			m->disp = true;
		}
		m->unlit = true;
		break;
	}
	case LK_DOOR:
		lk_door_face(sc, h->q, m);
		break;
	case LK_DOOR_EDGE: {
		double rough;
		m->albedo = vmul(lk_wood(h->q.z * 3.0, h->q.y, 30 + h->face, &rough), 0.85);
		m->rough = rough;
		m->coat = 0.3;
		break;
	}
	case LK_SIGN:
		lk_sign_face(sc, h, m);
		break;
	case LK_SIGN_EDGE: {
		double rough;
		m->albedo = vmul(lk_wood(h->q.z * 4.0 + h->q.x, h->q.y + h->q.x, 40 + h->face, &rough),
			1.30);
		m->rough = rough;
		m->coat = 0.2;
		break;
	}
	case LK_PLATE:
		lk_plate_face(sc, h, m);
		break;
	case LK_BRASS:
	case LK_KEY: {
		vec3 q = h->q;
		double nz = room_noise(q.x * 900.0 + q.z * 600.0, q.y * 900.0);
		m->metal = 1.0;
		m->albedo = h->id == LK_KEY ? v3(0.98, 0.79, 0.44) : v3(0.92, 0.70, 0.37);
		m->albedo = vmul(m->albedo, 0.92 + 0.10 * nz);
		m->rough = (h->id == LK_KEY ? 0.17 : 0.22) + 0.08 * nz;
		m->sub = h->id == LK_KEY ? 1 : 0;
		break;
	}
	case LK_LGLASS: {
		/* the lantern: glass in an iron frame, the flame inside it */
		vec3 lo = LK_LANTERN[0].lo, hi = LK_LANTERN[0].hi;
		int ax = h->face >> 1;
		double a = ax == 0 ? p.z : p.x, b = ax == 1 ? p.z : p.y;
		double a0 = ax == 0 ? lo.z : lo.x, a1 = ax == 0 ? hi.z : hi.x;
		double b0 = ax == 1 ? lo.z : lo.y, b1 = ax == 1 ? hi.z : hi.y;
		double fe = fmin(fmin(a - a0, a1 - a), fmin(b - b0, b1 - b));
		double mid = fabs(a - (a0 + a1) * 0.5);
		if (fe < 0.0075 || (ax != 1 && mid < 0.0022)) {
			m->albedo = v3(0.035, 0.032, 0.030);
			m->metal = 0.6;
			m->rough = 0.38;
			m->sub = 1;
			break;
		}
		vec3 bulb = LK_LIGHTS[1].pos;
		double ba = ax == 0 ? bulb.z : bulb.x, bb = ax == 1 ? bulb.z : bulb.y;
		double r2 = lk_sq(a - ba) + lk_sq(b - bb);
		m->emit = vmul(vadd(vmul(v3(1.0, 0.50, 0.17), 0.24),
			vmul(v3(1.0, 0.76, 0.45), 1.5 * exp(-r2 / 0.0008))), sc->rig.lantern);
		m->albedo = v3(0.02, 0.02, 0.02);
		m->rough = 0.10;
		break;
	}
	case LK_IRON:
		m->albedo = v3(0.035, 0.032, 0.030);
		m->metal = 0.6;
		m->rough = 0.40;
		break;
	case LK_DECO:
		lkd_material(sc, h, p, m);
		break;
	default:
		m->unlit = true;
		m->emit = v3(0.004, 0.005, 0.009);
		break;
	}
	if (h->id >= LK_DOOR && h->id <= LK_KEY) {
		m->ao *= lk_ao(sc, p, h->n);
	}
	if (sc->ndeco && !m->unlit && h->id != LK_DECO) {
		m->ao *= lkd_ao(sc, p, h->n);
	}
	bool lit_or_aloft = h->id == LK_DECO && (h->sub == DM_BULB || h->sub == DM_BALLOON
		|| h->sub == DM_GLOW || h->sub == DM_FLESH);
	if (!m->unlit && !lit_or_aloft && (sc->rig.wet > 0.0 || sc->rig.snow > 0.0) && p.z > -0.01) {
		lk_weather_on(sc, h, p, m);
	}
}

/* What a point in the room looks like: the picture of it, from the first
 * camera that saw it (in its frame, facing it, nothing in between), or
 * else made here -- walnut, the walls' colour, under the room's lamp. */
static vec3 lk_room_colour(const struct lk_scene *sc, vec3 p, vec3 n, int kind, int *sub)
{
	const struct lk_room *rm = &sc->room;
	for (int k = 0; k < 3; k++) {
		const struct lk_proj *pj = &rm->proj[k];
		double u, v;
		if (!pj->on || !room_project(&pj->cam, p, &u, &v) || u < 0.0 || u > 1.0 || v < 0.0
			|| v > 1.0) {
			continue;
		}
		vec3 d = vsub(p, pj->cam.eye);
		double dist = vlen(d);
		if (dist < 1e-6 || vdot(n, d) > 0.0) {
			continue;
		}
		int kk;
		vec3 nn;
		double th = lk_room_trace(rm, pj->cam.eye, vmul(d, 1.0 / dist), dist + 1.0, &kk, &nn);
		if (th < dist - 0.10) {
			continue;   /* something stood between it and the camera */
		}
		double a;
		double px = pj->x0 + u * pj->sw, py = pj->y0 + v * pj->sh;
		*sub = k;
		return lk_tex_at(&pj->tex, px / pj->tex.w, py / pj->tex.h, true, &a);
	}
	*sub = 2 + kind;
	/* made: walnut for the desk and the case, the walls in the colour they
	 * have at the edges of the picture, the lamp over the desk */
	vec3 c;
	double rough;
	switch (kind) {
	case LK_RM_DESK:
	case LK_RM_LEG: {
		bool across = fabs(n.z) > 0.5;
		vec3 a = across ? lk_wood(p.x * rm->s * 0.6, p.y * rm->s, 50 + kind, &rough)
			: lk_wood(p.z * rm->s * 0.6, p.y * rm->s, 52 + kind, &rough);
		c = vmul(a, 1.35);
		if (kind == LK_RM_DESK && n.z > 0.5 && p.y < -RM_TOP_T - 0.2) {
			/* the drawer, in the apron: a dark line round it, a brass pull */
			double dx = RM_DR_HX - fabs(p.x), dy = fmin(p.y - (RM_DR_BOT - 0.10), (-RM_TOP_T - 0.15) - p.y);
			if (dx > 0.0 && dy > 0.0 && fmin(dx, dy) < 0.12) {
				c = vmul(c, 0.35);
			}
			if (fabs(p.x) < 1.9 && fabs(p.y - (RM_DR_BOT + RM_DR_TOP) * 0.5) < 0.40) {
				c = v3(0.55, 0.40, 0.16);
			}
		}
		break;
	}
	case LK_RM_CASE:
		c = vmul(lk_wood(p.z * rm->s * 0.6 + p.x * rm->s, p.y * rm->s, 60, &rough), 1.1);
		break;
	case LK_RM_POST:
		c = v3(0.30, 0.040, 0.025);
		break;
	case LK_RM_CONSOLE:
		c = v3(0.050, 0.046, 0.042);
		break;
	default:
		if (n.y > 0.5) {
			/* the floor: boards running away from the door */
			double bx = p.x * rm->s / 0.14;
			int board = (int)floor(bx);
			c = vmul(lk_wood(bx * 0.14, p.z * rm->s + board * 1.7, 70 + board, &rough), 1.3);
			double seam = fabs(bx - floor(bx) - 0.5);
			c = vmul(c, 1.0 - 0.45 * smoothstepf(0.47, 0.5, seam));
		} else if (n.y < -0.5) {
			c = rm->ceil;
		} else {
			/* the wall, as the picture has it beside the bookcase; along its
			 * foot a skirting board, its top edge catching the lamp */
			c = rm->wall;
			double up = p.y - rm->fy;
			if (up < 4.2) {
				c = up > 3.8 ? v3(0.30, 0.19, 0.11) : v3(0.17, 0.105, 0.06);
			}
			return vmul(c, 0.86 + 0.14 * smoothstepf(-10.0, 20.0, p.y)
				- 0.10 * smoothstepf(30.0, 60.0, fabs(p.x)));
		}
		break;
	}
	/* the lamp is over the desk; lower down and further off, darker */
	vec3 lamp = v3(0.0, 34.0, (rm->desk_z0 + rm->desk_z1) * 0.5);
	vec3 lv = vsub(lamp, p);
	double ld = vlen(lv);
	double lit = fmax(vdot(n, vmul(lv, 1.0 / ld)), 0.0);
	double fall = 1.0 / (1.0 + lk_sq(ld / 38.0));
	double k = 0.30 + 0.95 * lit * fall;
	/* shut in: down at the floor, under the desk */
	if (p.y < -RM_TOP_T && fabs(p.x) < rm->desk_hx && p.z > rm->desk_z0 && p.z < rm->desk_z1) {
		k *= 0.45;
	}
	k *= 0.70 + 0.30 * smoothstepf(0.0, 6.0, p.y - rm->fy);
	if (kind != LK_RM_SHELL || n.y > 0.5) {
		/* wood is made in light; shown, it wants the curve the pictures went through */
		return tonemap(vmul(c, k * 0.9));
	}
	return vmul(c, 0.75 + 0.25 * k);
}

/* one sample: what the ray meets, lit, as it is to be shown */
static vec3 lk_sample(const struct lk_scene *sc, vec3 ro, vec3 rd, int *aa)
{
	struct lk_hit h;
	lk_intersect(sc, ro, rd, &h);
	vec3 p = vadd(ro, vmul(rd, h.t));
	struct lk_mat m;
	lk_material(sc, &h, p, &m);
	*aa = h.id * 8 + (m.sub & 7);
	if (m.disp) {
		return m.emit;
	}
	vec3 v = vmul(rd, -1.0);
	if (vdot(m.n, v) < 0.0) {
		m.n = vnorm(vsub(m.n, vmul(v, vdot(m.n, v) * 1.02)));
	}
	vec3 c = lk_shade(sc, p, v, &m, &h);
	if (sc->rig.haze_k > 0.0) {
		c = vmix(c, sc->rig.haze, 1.0 - exp(-h.t * sc->rig.haze_k));
	}
	return tonemap(vmul(c, sc->rig.exposure));
}
