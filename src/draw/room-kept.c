/* Part of draw.c: the room as it is made, kept and handed over.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

void desk_room_gpu_prefer(const char *node)
{
#ifdef DESK_HAVE_GPU
	desk_gpu_prefer(node);
#else
	(void)node;
#endif
}

/* At the end of the session (desk.h): the warm-up told to stop and waited
 * for. It is not left running into exit(), which takes the GPU's driver
 * down -- Mesa's and LLVM's destructors -- under a trace still going on
 * it, and the process crashes on its way out. It stops at the next window
 * of the sheet on the GPU, or row on the CPU, and what it had of the room
 * is neither put up nor kept. */
void desk_room_stop(void)
{
	atomic_store(&g_room_stop, 1);
	if (!g_room_warm_live) {
		return;
	}
	pthread_mutex_lock(&g_room_lock);
	bool busy = g_room_warming != 0;
	pthread_mutex_unlock(&g_room_lock);
	double t0 = room_now();
	pthread_join(g_room_warm_th, NULL);
	g_room_warm_live = false;
	if (busy) {
		room_log("the warm-up stopped part way for the end of the session, in %.0f ms",
			(room_now() - t0) * 1000.0);
	}
}

/* The room to draw the desk from.
 *
 * Not necessarily the room for this exact size. A window that has just been
 * resized wants a room nobody has traced yet, and the honest answer -- no
 * room -- meant a bare plank that answered no clicks at all: not the post,
 * not the console, not the drawer, not a dock icon. For a size nobody had
 * opened before that was a minute and a half of a dead desk.
 *
 * So the last room stays up until its replacement lands. The desk is fitted
 * to the frame by height and has a third of its width to spare, so a room
 * traced for a slightly different shape of screen is a slightly different
 * crop and nothing more. Everything is measured off the sheet that is
 * actually drawn, so the pointer still lands where it looks. */
static struct room_render *room_for(int screen_w, int screen_h)
{
	int cw, ch, hq;
	room_reap();
	room_canon(screen_w, screen_h, &cw, &ch, &hq);
	struct room_render *rr = room_peek(cw, ch, hq);
	return rr ? rr : room_any();
}

/* for a view -- the bookcase, the console, the post box -- which is not
 * there yet on the first start of a size until its sheet is traced */
static struct room_render *room_for_views(int screen_w, int screen_h, unsigned views)
{
	int cw, ch, hq;
	room_reap();
	room_canon(screen_w, screen_h, &cw, &ch, &hq);
	struct room_render *rr = room_peek_views(cw, ch, hq, views);
	return rr ? rr : room_any_views(views);
}

static struct room_render *room_for_view(int screen_w, int screen_h, int which)
{
	if (which < 0 || which >= DESK_VIEW_COUNT) {
		return NULL;
	}
	return room_for_views(screen_w, screen_h, 1u << which);
}

/* and for what needs all three */
static struct room_render *room_for_whole(int screen_w, int screen_h)
{
	return room_for_views(screen_w, screen_h, ROOM_VIEWS_ALL);
}

/* ---- desk-gputest --room ------------------------------------------
 *
 * Every kind of sheet, on the GPU and on the CPU, side by side. Not the
 * spot check a bake does -- all of it: how many samples differ in coverage
 * or owner, how far the colours are apart, and then the same after the
 * resolve, in the eight bits that end up on screen. With a directory, the
 * two sheets and their difference go there to be looked at. This is the
 * tool for a machine the GPU path has not been seen on. */

#ifdef DESK_HAVE_GPU
static void selftest_png(const char *dir, const char *name, const char *what,
	cairo_surface_t *s)
{
	if (!dir || !s) {
		return;
	}
	char path[1024];
	snprintf(path, sizeof path, "%s/%s-%s.png", dir, name, what);
	cairo_status_t st = cairo_surface_write_to_png(s, path);
	if (st != CAIRO_STATUS_SUCCESS) {
		printf("could not write %s: %s\n", path, cairo_status_to_string(st));
	}
}

/* |a - b| per channel, eight times over so that one level shows */
static cairo_surface_t *selftest_diff(cairo_surface_t *a, cairo_surface_t *b,
	int *over2, int *worst)
{
	int w = cairo_image_surface_get_width(a), h = cairo_image_surface_get_height(a);
	cairo_surface_t *d = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
	cairo_surface_flush(a);
	cairo_surface_flush(b);
	const unsigned char *pa = cairo_image_surface_get_data(a);
	const unsigned char *pb = cairo_image_surface_get_data(b);
	unsigned char *pd = cairo_image_surface_get_data(d);
	int sa = cairo_image_surface_get_stride(a), sb = cairo_image_surface_get_stride(b);
	int sd = cairo_image_surface_get_stride(d);
	*over2 = 0;
	*worst = 0;
	for (int y = 0; y < h; y++) {
		const uint32_t *ra = (const uint32_t *)(pa + (size_t)y * sa);
		const uint32_t *rb = (const uint32_t *)(pb + (size_t)y * sb);
		uint32_t *rd = (uint32_t *)(pd + (size_t)y * sd);
		for (int x = 0; x < w; x++) {
			int m = 0;
			uint32_t out = 0;
			for (int c = 0; c < 32; c += 8) {
				int da = abs((int)((ra[x] >> c) & 255) - (int)((rb[x] >> c) & 255));
				m = da > m ? da : m;
				if (c < 24) {
					int v = da * 8 > 255 ? 255 : da * 8;
					out |= (uint32_t)v << c;
				}
			}
			rd[x] = out;
			*over2 += m > 2;
			*worst = m > *worst ? m : *worst;
		}
	}
	cairo_surface_mark_dirty(d);
	return d;
}

/* ---- the faces pictured, and tried against the trace ----------------
 *
 * desk-gputest --faces-bake makes a view's pack; --faces lays the view
 * and the move to it from the faces, next to the same traced. */

/* Where along the normal through w the face's own surface is: marched in
 * from just outside it. A start inside something else is a texel that
 * something stands on, which nothing will ever see. */
static bool rf_find(const struct rface *f, vec3 w, vec3 *hp, int *id)
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

/* How finely a face has to be pictured, and how much of it: as many texels
 * to the unit along each edge as the cameras it is seen through put pixels
 * there (and a fifth more), and only the part of it any of them sees. */
static void rf_size(struct rface *f, const struct room_cam *cams, const int *camw, int ncam,
	bool whole)
{
	double lu = vlen(f->u), lv = vlen(f->v);
	vec3 du = vmul(f->u, 1.0 / lu), dv = vmul(f->v, 1.0 / lv);
	double a0 = 1.0, a1 = 0.0, b0 = 1.0, b1 = 0.0, du_px = 0.0, dv_px = 0.0;
	const int N = 48;
	for (int j = 0; j <= N; j++) {
		for (int i = 0; i <= N; i++) {
			double a = (double)i / N, b = (double)j / N;
			vec3 w = vadd(f->o, vadd(vmul(f->u, a), vmul(f->v, b)));
			for (int c = 0; c < ncam; c++) {
				double pu, pv, qu, qv, ru, rv;
				if (vdot(vsub(w, cams[c].eye), f->n) >= 0.0
					|| !room_project(&cams[c], w, &pu, &pv)
					|| pu < -0.02 || pu > 1.02 || pv < -0.02 || pv > 1.02) {
					continue;
				}
				a0 = fmin(a0, a);
				a1 = fmax(a1, a);
				b0 = fmin(b0, b);
				b1 = fmax(b1, b);
				double camh = camw[c] * cams[c].fov_y / cams[c].fov_x;
				if (room_project(&cams[c], vadd(w, vmul(du, 0.01)), &qu, &qv)) {
					du_px = fmax(du_px, hypot((qu - pu) * camw[c], (qv - pv) * camh) / 0.01);
				}
				if (room_project(&cams[c], vadd(w, vmul(dv, 0.01)), &ru, &rv)) {
					dv_px = fmax(dv_px, hypot((ru - pu) * camw[c], (rv - pv) * camh) / 0.01);
				}
			}
		}
	}
	if (a1 < a0 || b1 < b0) {
		f->lv = 0;   /* nobody sees it */
		return;
	}
	if (whole) {
		a0 = b0 = 0.0;
		a1 = b1 = 1.0;
	}
	/* a grid step either way, for what fell between the points looked at */
	a0 = fmax(0.0, a0 - 1.0 / N);
	a1 = fmin(1.0, a1 + 1.0 / N);
	b0 = fmax(0.0, b0 - 1.0 / N);
	b1 = fmin(1.0, b1 + 1.0 / N);
	f->o = vadd(f->o, vadd(vmul(f->u, a0), vmul(f->v, b0)));
	f->u = vmul(f->u, a1 - a0);
	f->v = vmul(f->v, b1 - b0);
	double tu = fmin(du_px * 1.2 * f->fine, f->tpu_max);
	double tv = fmin(dv_px * 1.2 * f->fine, f->tpu_max);
	f->tw[0] = (int)ceil(vlen(f->u) * fmax(tu, 1.0)) + 1;
	f->th[0] = (int)ceil(vlen(f->v) * fmax(tv, 1.0)) + 1;
	f->lv = 1;
}

struct rf_bake_arg {
	struct rface *f;
	vec3 eye;
	float *tex, *key;   /* linear, premultiplied, the first level */
	unsigned char *tall;   /* or NULL: what stands tall, a texel each */
	float *height;         /* or NULL: how far off the face, a texel each */
};

static void rf_bake_row(void *p, int y)
{
	struct rf_bake_arg *a = p;
	struct rface *f = a->f;
	static const double J[4][2] = {
		{ 0.25, 0.25 }, { 0.75, 0.25 }, { 0.25, 0.75 }, { 0.75, 0.75 },
	};
	int tw = f->tw[0], th = f->th[0];
	for (int x = 0; x < tw; x++) {
		double r = 0, g = 0, b = 0, cov = 0, kr = 0, kg = 0, kb = 0;
		int own = 0;
		bool owned = false, tall = false;
		double hsum = 0.0;
		for (int k = 0; k < 4; k++) {
			double s = (x + J[k][0]) / tw, t = (y + J[k][1]) / th;
			vec3 w = vadd(f->o, vadd(vmul(f->u, s), vmul(f->v, t)));
			vec3 hp;
			int id;
			if (!rf_find(f, w, &hp, &id)) {
				continue;
			}
			double hgt = vdot(vsub(hp, w), f->n);
			hsum += hgt;
			if (f->relief && hgt > f->tall) {
				tall = true;
			}
			vec3 n = scene_normal(hp, ROOM_KIND);
			vec3 rd = vnorm(vsub(hp, a->eye));
			vec3 col = shade_surface(hp, n, rd, ROOM_KIND, id, 0.0, 0);
			vec3 lit = shade_surface(hp, n, rd, ROOM_KIND, id, 1.0, 0);
			r += col.x;
			g += col.y;
			b += col.z;
			kr += lit.x - col.x;
			kg += lit.y - col.y;
			kb += lit.z - col.z;
			cov += 1.0;
			if (!owned) {
				own = room_owner(hp, id);
				owned = true;
			}
		}
		float *o = a->tex + ((size_t)y * tw + x) * 4;
		o[0] = (float)(r * 0.25);
		o[1] = (float)(g * 0.25);
		o[2] = (float)(b * 0.25);
		o[3] = (float)(cov * 0.25);
		float *k = a->key + ((size_t)y * tw + x) * 4;
		k[0] = (float)(kr * 0.25);
		k[1] = (float)(kg * 0.25);
		k[2] = (float)(kb * 0.25);
		k[3] = (float)(cov * 0.25);
		f->own[(size_t)y * tw + x] = (unsigned char)own;
		if (a->tall) {
			a->tall[(size_t)y * tw + x] = tall;
		}
		if (a->height) {
			a->height[(size_t)y * tw + x] = cov > 0.0 ? (float)(hsum / cov) : 0.0f;
		}
	}
}

/* What stands tall off a face, and round it as far as it could be seen
 * across the face from the side, marked RF_OWN_TALL: traced, not laid. */
static void rf_mark_tall(struct rface *f, const unsigned char *tall)
{
	int tw = f->tw[0], th = f->th[0];
	for (int y = 0; y < th; y++) {
		for (int x = 0; x < tw; x++) {
			if (!tall[(size_t)y * tw + x]) {
				continue;
			}
			double reach = RF_TALL_REACH * (f->height ? f->height[(size_t)y * tw + x] : f->tall);
			int rx = (int)ceil(reach * tw / vlen(f->u));
			int ry = (int)ceil(reach * th / vlen(f->v));
			for (int yy = y - ry < 0 ? 0 : y - ry; yy <= y + ry && yy < th; yy++) {
				for (int xx = x - rx < 0 ? 0 : x - rx; xx <= x + rx && xx < tw; xx++) {
					double dx = (double)(xx - x) / (rx + 1e-9), dy = (double)(yy - y) / (ry + 1e-9);
					if (dx * dx + dy * dy <= 1.0) {
						f->own[(size_t)yy * tw + xx] = RF_OWN_TALL;
					}
				}
			}
		}
	}
}

/* One of a face's pictures, from linear: the scale that fits its
 * brightest texel, and the first level. */
static uint32_t *rf_pack(const float *t, size_t n, float *scale)
{
	double top = 1e-6;
	for (size_t i = 0; i < n; i++) {
		if (t[i * 4 + 3] > 1e-6f) {
			for (int c = 0; c < 3; c++) {
				top = fmax(top, t[i * 4 + c] / t[i * 4 + 3]);
			}
		}
	}
	*scale = (float)(1.0 / top);
	uint32_t *d = malloc(n * sizeof(uint32_t));
	if (d) {
		for (size_t i = 0; i < n; i++) {
			d[i] = rf_enc(t + i * 4, *scale);
		}
	}
	return d;
}

static bool rf_seen_from(const struct rface *f, const struct room_cam *c)
{
	for (int j = 0; j <= 8; j++) {
		for (int i = 0; i <= 8; i++) {
			vec3 w = vadd(f->o, vadd(vmul(f->u, i / 8.0), vmul(f->v, j / 8.0)));
			double pu, pv;
			if (vdot(vsub(w, c->eye), f->n) < 0.0 && room_project(c, w, &pu, &pv)
				&& pu > -0.02 && pu < 1.02 && pv > -0.02 && pv < 1.02) {
				return true;
			}
		}
	}
	return false;
}

/* Picture every face that the cameras see, lit as it is seen from eye --
 * and a face that the first of them sees as well (with more than one, the
 * start of the move), as it is seen from there too. */
static long rf_bake(struct rface_set *s, const struct room_cam *cams, const int *camw,
	int ncam, vec3 eye, bool whole)
{
	rf_lin_init();
	s->eye1 = eye;
	s->eye0 = ncam > 1 ? cams[1].eye : eye;
	long texels = 0;
	for (int i = 0; i < s->n; i++) {
		struct rface *f = &s->f[i];
		if (f->trace || f->clear) {
			continue;
		}
		rf_size(f, cams, camw, ncam, whole);
		if (!f->lv) {
			continue;
		}
		size_t n = (size_t)f->tw[0] * f->th[0];
		bool two = ncam > 1 && f->two && rf_seen_from(f, &cams[1]);
		float *lin[RF_PICS] = { NULL, NULL, NULL, NULL };
		bool ok = true;
		for (int k = 0; k < (two ? RF_PICS : RF_REST0); k++) {
			lin[k] = calloc(n * 4, sizeof(float));
			ok = ok && lin[k];
		}
		f->own = calloc(n, 1);
		unsigned char *tall = f->relief ? calloc(n, 1) : NULL;
		f->height = f->relief ? calloc(n, sizeof(float)) : NULL;
		if (ok && f->own) {
			struct rf_bake_arg a = { f, eye, lin[RF_REST], lin[RF_LAMP], tall, f->height };
			g_rf_flat = g_rf_unshadowed = true;
			rf_run(rf_bake_row, &a, f->th[0]);
			if (two) {
				struct rf_bake_arg a0 = { f, s->eye0, lin[RF_REST0], lin[RF_LAMP0], NULL, NULL };
				rf_run(rf_bake_row, &a0, f->th[0]);
				texels += (long)n;
			}
			g_rf_flat = g_rf_unshadowed = false;
			if (tall) {
				rf_mark_tall(f, tall);
			}
			if (f->height) {
				f->hmin = f->hmax = 0.0f;
				for (size_t k = 0; k < n; k++) {
					/* the tall are traced: the field stops at f->tall */
					f->height[k] = fminf(f->height[k], (float)f->tall);
					if (lin[RF_REST][k * 4 + 3] > 0.0f) {
						f->hmin = fminf(f->hmin, f->height[k]);
						f->hmax = fmaxf(f->hmax, f->height[k]);
					}
				}
				/* Where nothing was found the face is open, as deep as it
				 * goes: a ray followed down past the edge of a rounded block
				 * goes on to what it meets beyond, and is not stopped at the
				 * plane it was looked for from. */
				for (size_t k = 0; k < n; k++) {
					if (lin[RF_REST][k * 4 + 3] <= 0.0f) {
						f->height[k] = f->hmin;
					}
				}
			}
			for (int k = 0; k < RF_PICS; k++) {
				if (lin[k]) {
					f->pic[k][0] = rf_pack(lin[k], n, &f->scale[k]);
					ok = ok && f->pic[k][0];
				}
			}
		}
		for (int k = 0; k < RF_PICS; k++) {
			free(lin[k]);
		}
		free(tall);
		if (!ok || !f->own) {
			for (int k = 0; k < RF_PICS; k++) {
				free(f->pic[k][0]);
				f->pic[k][0] = NULL;
			}
			free(f->own);
			f->own = NULL;
			f->lv = 0;
			continue;
		}
		/* each picture from the level it starts at */
		rf_dims(f);
		for (int k = 0; k < RF_PICS; k++) {
			if (!f->pic[k][0] || f->base[k] == 0) {
				continue;
			}
			uint32_t *t = f->pic[k][0];
			f->pic[k][0] = NULL;
			for (int l = 1; l <= f->base[k] && t; l++) {
				uint32_t *h = rf_half(t, f->tw[l - 1], f->th[l - 1], f->tw[l], f->th[l]);
				free(t);
				t = h;
			}
			f->pic[k][f->base[k]] = t;
		}
		rf_mips(f);
		texels += (long)n;
	}
	return texels;
}

/* How far apart two sheets are once the grain is taken out of both -- a
 * five by five box over each -- in pixels more than two levels apart, and
 * the mean of the largest channel's difference. */
static int rf_soft_diff(cairo_surface_t *a, cairo_surface_t *b, double *mean)
{
	int w = cairo_image_surface_get_width(a), h = cairo_image_surface_get_height(a);
	cairo_surface_flush(a);
	cairo_surface_flush(b);
	const unsigned char *pa = cairo_image_surface_get_data(a);
	const unsigned char *pb = cairo_image_surface_get_data(b);
	int sa = cairo_image_surface_get_stride(a), sb = cairo_image_surface_get_stride(b);
	int over = 0;
	double sum = 0.0;
	for (int y = 2; y < h - 2; y++) {
		for (int x = 2; x < w - 2; x++) {
			int m = 0;
			for (int c = 0; c < 24; c += 8) {
				int ta = 0, tb = 0;
				for (int dy = -2; dy <= 2; dy++) {
					const uint32_t *ra = (const uint32_t *)(pa + (size_t)(y + dy) * sa);
					const uint32_t *rb = (const uint32_t *)(pb + (size_t)(y + dy) * sb);
					for (int dx = -2; dx <= 2; dx++) {
						ta += (ra[x + dx] >> c) & 255;
						tb += (rb[x + dx] >> c) & 255;
					}
				}
				int d = abs(ta - tb) / 25;
				m = d > m ? d : m;
			}
			over += m > 2;
			sum += m;
		}
	}
	*mean = sum / ((double)(w - 4) * (h - 4));
	return over;
}

/* A picture of a face as a PNG, every step-th texel of it: cairo's, so
 * multiplied by its alpha. Into dir/faces, to see what they weigh. */
static void rf_dump_one(const char *path, const uint32_t *t, int w, int h, int step)
{
	int ow = (w + step - 1) / step, oh = (h + step - 1) / step;
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ow, oh);
	unsigned char *d = cairo_image_surface_get_data(s);
	int st = cairo_image_surface_get_stride(s);
	for (int y = 0; y < oh; y++) {
		uint32_t *row = (uint32_t *)(d + (size_t)y * st);
		for (int x = 0; x < ow; x++) {
			uint32_t p = t[(size_t)(y * step) * w + x * step], a = p >> 24, q = a << 24;
			for (int c = 0; c < 24; c += 8) {
				q |= ((((p >> c) & 255) * a + 127) / 255) << c;
			}
			row[x] = q;
		}
	}
	cairo_surface_mark_dirty(s);
	cairo_surface_write_to_png(s, path);
	cairo_surface_destroy(s);
}

static void rf_dump(const struct rface_set *fs, const char *dir)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/faces", dir);
	mkdir(path, 0755);
	for (int i = 0; i < fs->n; i++) {
		const struct rface *f = &fs->f[i];
		if (!f->lv || f->trace) {
			continue;
		}
		static const char *const KIND[RF_PICS] = { "rest", "lamp", "rest0", "lamp0" };
		for (int k = 0; k < RF_PICS; k++) {
			if (rf_has(f, k)) {
				int b = f->base[k];
				snprintf(path, sizeof path, "%s/faces/%02d-%s-%s.png", dir, i, f->what,
					KIND[k]);
				rf_dump_one(path, f->pic[k][b], f->tw[b], f->th[b], 1);
			}
		}
	}
}

struct rf_buf {
	unsigned char *d;
	size_t n, cap;
};

static cairo_status_t rf_buf_write(void *closure, const unsigned char *data, unsigned int len)
{
	struct rf_buf *b = closure;
	if (b->n + len > b->cap) {
		size_t cap = (b->n + len) * 2;
		unsigned char *d = realloc(b->d, cap);
		if (!d) {
			return CAIRO_STATUS_WRITE_ERROR;
		}
		b->d = d;
		b->cap = cap;
	}
	memcpy(b->d + b->n, data, len);
	b->n += len;
	return CAIRO_STATUS_SUCCESS;
}

static bool rf_write(const struct rface_set *s, const char *path)
{
	FILE *fp = fopen(path, "wb");
	if (!fp) {
		return false;
	}
	int32_t n = 0;
	for (int i = 0; i < s->n; i++) {
		n += s->f[i].lv > 0 && !s->f[i].trace;
	}
	fwrite("DSKFACE4", 1, 8, fp);
	fwrite(&n, sizeof n, 1, fp);
	struct rf_buf *png = calloc((size_t)s->n * (RF_PICS + 2), sizeof *png);
	bool ok = png != NULL;
	for (int i = 0; ok && i < s->n; i++) {
		const struct rface *f = &s->f[i];
		if (!f->lv || f->trace) {
			continue;
		}
		struct rf_rec r;
		memset(&r, 0, sizeof r);
		snprintf(r.what, sizeof r.what, "%s", f->what);
		r.nth = rf_nth(s, i);
		r.tw = f->tw[0];
		r.th = f->th[0];
		r.turn = (float)f->turn;
		const vec3 *V[6] = { &f->o, &f->u, &f->v, &f->n, &f->at, &f->lamp };
		double *D[6] = { r.o, r.u, r.v, r.n, r.at, r.lamp };
		for (int k = 0; k < 6; k++) {
			D[k][0] = V[k]->x;
			D[k][1] = V[k]->y;
			D[k][2] = V[k]->z;
		}
		for (int k = 0; k <= RF_PICS + 1; k++) {
			struct rf_buf *b = &png[(size_t)i * (RF_PICS + 2) + k];
			cairo_surface_t *img;
			if (k == RF_PICS + 1) {
				/* the heights, between hmin and hmax in 255 steps */
				if (!f->height || f->hmax - f->hmin < 1e-4f) {
					continue;
				}
				r.hmin = f->hmin;
				r.hmax = f->hmax;
				img = cairo_image_surface_create(CAIRO_FORMAT_A8, r.tw, r.th);
				unsigned char *d = cairo_image_surface_get_data(img);
				int st = cairo_image_surface_get_stride(img);
				for (int y = 0; y < r.th; y++) {
					for (int x = 0; x < r.tw; x++) {
						double hgt = (f->height[(size_t)y * r.tw + x] - f->hmin)
							/ (f->hmax - f->hmin);
						d[(size_t)y * st + x] = (unsigned char)(clampf(hgt, 0, 1) * 255 + 0.5);
					}
				}
			} else if (k < RF_PICS) {
				if (!rf_has(f, k)) {
					continue;
				}
				int bl = f->base[k], pw = f->tw[bl], ph = f->th[bl];
				const uint32_t *t = f->pic[k][bl];
				r.scale[k] = f->scale[k];
				r.base[k] = (uint8_t)bl;
				img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
				unsigned char *d = cairo_image_surface_get_data(img);
				int st = cairo_image_surface_get_stride(img);
				for (int y = 0; y < ph; y++) {
					uint32_t *row = (uint32_t *)(d + (size_t)y * st);
					for (int x = 0; x < pw; x++) {
						uint32_t p = t[(size_t)y * pw + x], a = p >> 24;
						uint32_t q = a << 24;
						for (int c = 0; c < 24; c += 8) {
							q |= ((((p >> c) & 255) * a + 127) / 255) << c;
						}
						row[x] = q;
					}
				}
			} else {
				img = cairo_image_surface_create(CAIRO_FORMAT_A8, r.tw, r.th);
				unsigned char *d = cairo_image_surface_get_data(img);
				int st = cairo_image_surface_get_stride(img);
				for (int y = 0; y < r.th; y++) {
					memcpy(d + (size_t)y * st, f->own + (size_t)y * r.tw, (size_t)r.tw);
				}
			}
			cairo_surface_mark_dirty(img);
			ok = ok && cairo_surface_write_to_png_stream(img, rf_buf_write, b)
				== CAIRO_STATUS_SUCCESS;
			cairo_surface_destroy(img);
			r.len[k] = (uint32_t)b->n;
		}
		fwrite(&r, sizeof r, 1, fp);
	}
	for (int i = 0; ok && i < s->n * (RF_PICS + 2); i++) {
		if (png[i].n) {
			fwrite(png[i].d, 1, png[i].n, fp);
		}
	}
	for (int i = 0; png && i < s->n * (RF_PICS + 2); i++) {
		free(png[i].d);
	}
	free(png);
	ok = !ferror(fp) && ok;
	return fclose(fp) == 0 && ok;
}


/* The move up to the bookcase, frame by frame at a quarter of the size,
 * traced and laid; a sheet of each into dir. */
static void rf_film_test(struct rface_set *fs, int w, int h, const char *dir, int layer)
{
	int fw = w / 4, fh = h / 4, fhw = fw * ICON_SS, fhh = fh * ICON_SS;
	size_t n = (size_t)fhw * fhh;
	float *ht = calloc(n * 4, sizeof(float)), *hl = calloc(n * 4, sizeof(float));
	unsigned char *it = calloc(n, 1), *il = calloc(n, 1), *need = calloc(n, 1);
	cairo_surface_t *sheet_t = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, fw * 6, fh * 3);
	cairo_surface_t *sheet_l = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, fw * 6, fh * 3);
	cairo_t *ct_ = cairo_create(sheet_t), *cl_ = cairo_create(sheet_l);
	double aspect = (double)w / (double)h;
	double tt = 0, tl = 0, tr = 0;
	printf("\nthe move, %d frames of %d x %d:\n", ROOM_TILT_FRAMES, fw, fh);
	printf("  frame  traced    laid    rest  rest %%   soft>2 %%   mean\n");
	for (int k = 0; ht && hl && it && il && need && k < ROOM_TILT_FRAMES; k++) {
		struct room_cam cam;
		double u = ROOM_TILT_U0 + (ROOM_TILT_U1 - ROOM_TILT_U0) * k / (ROOM_TILT_FRAMES - 1);
		int which = layer == 4 ? DESK_VIEW_CONSOLE : layer == 5 ? DESK_VIEW_POST
			: DESK_VIEW_SHELF;
		room_cam_lerp(aspect, u, which, &cam);
		room_film_pose(which, u);
		rf_pose(fs);
		struct room_ctx c1 = { 0 };
		c1.hi = ht;
		c1.idhi = it;
		c1.hw = fhw;
		c1.hh = fhh;
		c1.layer = layer;
		c1.cam = cam;
		double t0 = room_now();
		room_trace_cpu(&c1);
		double a = room_now() - t0;
		struct room_ctx c2 = c1;
		c2.hi = hl;
		c2.idhi = il;
		t0 = room_now();
		rf_lay_sheet(fs, &c2, need);
		double b = room_now() - t0;
		size_t needed = 0;
		for (size_t i = 0; i < n; i++) {
			needed += need[i];
		}
		c2.need = need;
		t0 = room_now();
		room_trace_cpu(&c2);
		double c = room_now() - t0;
		cairo_surface_t *s1 = room_resolve(ht, it, fw, fh, NULL);
		cairo_surface_t *s2 = room_resolve(hl, il, fw, fh, NULL);
		double mean = 0.0;
		int over = rf_soft_diff(s2, s1, &mean);
		printf("  %5d %7.2f %7.2f %7.2f %7.1f %10.2f %6.2f\n", k, a, b, c,
			100.0 * needed / n, 100.0 * over / ((double)fw * fh), mean);
		tt += a;
		tl += b;
		tr += c;
		cairo_set_source_surface(ct_, s1, (k % 6) * fw, (k / 6) * fh);
		cairo_paint(ct_);
		cairo_set_source_surface(cl_, s2, (k % 6) * fw, (k / 6) * fh);
		cairo_paint(cl_);
		cairo_surface_destroy(s1);
		cairo_surface_destroy(s2);
	}
	printf("  all   %7.2f %7.2f %7.2f\n", tt, tl, tr);
	cairo_destroy(ct_);
	cairo_destroy(cl_);
	selftest_png(dir, "film", "trace", sheet_t);
	selftest_png(dir, "film", "both", sheet_l);
	cairo_surface_destroy(sheet_t);
	cairo_surface_destroy(sheet_l);
	free(ht);
	free(hl);
	free(it);
	free(il);
	free(need);
}

/* The cameras a view's face set is pictured for, in the room as the
 * globals have it: the view at w wide, and the move to it at a quarter of
 * that. cams has room for 1 + ROOM_TILT_FRAMES. */
static int rf_view_cams(int which, double aspect, int w, struct room_cam *cams, int *camw)
{
	camw[0] = w;
	int n = 1;
	for (int k = 0; k < ROOM_TILT_FRAMES; k++) {
		room_cam_lerp(aspect, ROOM_TILT_U0 + (ROOM_TILT_U1 - ROOM_TILT_U0) * k
			/ (ROOM_TILT_FRAMES - 1), which, &cams[n]);
		camw[n++] = w / 4;
	}
	/* room_cam_lerp left the desk's camera's room */
	rf_view_cam(which, aspect, &cams[0]);
	return n;
}

/* The desk's own view's pack: the faces of its sheet (layer 1) and of its
 * drawer's (2), pictured straight down in the room of the widest desk it is
 * for at its deepest, a 2560 point screen of 16 by 10 -- through a camera
 * w wide, which at 2560 has the top at its cap (rf_plan). DESK_FACES_BAKE_AT
 * as for the others. Under g_render_lock. */
static bool rf_bake_plan(const char *out, int w)
{
	double aspect = 1.6, hx = 28.44, at_a = 0.0, at_h = 0.0;
	const char *bat = getenv("DESK_FACES_BAKE_AT");
	if (bat && sscanf(bat, "%lf:%lf", &at_a, &at_h) == 2 && at_a > 0.2) {
		aspect = at_a;
		hx = fmax(at_h, RM_HX_MIN);
	}
	struct rface_set *one = calloc(1, sizeof *one), *two = calloc(1, sizeof *two);
	bool ok = one && two;
	if (ok) {
		double open0 = g_room_open;
		g_room_open = 1.0;
		g_rm_hx = hx;
		struct room_cam cam;
		room_cam_build(aspect, &cam);
		printf("the desk: %.2f to 1, %.1f wide, %.1f deep, seen %d wide\n", aspect,
			2.0 * hx, RM_ZN - RM_ZF, w);
		for (int layer = 1; layer <= 2; layer++) {
			struct rface_set *s = layer == 1 ? one : two;
			g_room_layer = layer;
			rf_layer(s, layer);
			double t0 = room_now();
			long t = rf_bake(s, &cam, &w, 1, cam.eye, true);
			printf("  its %s: %ld texels in %.1f s\n", layer == 1 ? "sheet" : "drawer", t,
				room_now() - t0);
		}
		/* the drawer's after the desk's, every one, in the order they
		 * come (rf_nth) */
		for (int i = 0; i < two->n && one->n < RF_MAX; i++) {
			one->f[one->n++] = two->f[i];
			memset(&two->f[i], 0, sizeof two->f[i]);
		}
		ok = rf_write(one, out);
		printf("%s %s\n", ok ? "wrote" : "could not write", out);
		g_room_open = open0;
	}
	if (one) {
		rf_free(one);
	}
	if (two) {
		rf_free(two);
	}
	free(one);
	free(two);
	struct room_cam dc;
	room_cam_build(1.6, &dc);
	return ok;
}

bool desk_room_faces_bake(const char *out, int w, int layer)
{
	/* the pictures that ship are of the desk as it was made, always (desk_relight) */
	desk_set_wood(DESK_WOOD_WALNUT, DESK_STAIN_NATURAL);
	wood_bake_begin();
	const int which = layer == 4 ? DESK_VIEW_CONSOLE : layer == 5 ? DESK_VIEW_POST
		: DESK_VIEW_SHELF;
	if (layer == 1 || layer == 2) {
		pthread_mutex_lock(&g_render_lock);
		drawer_items_build();
		sh_build();
		bool ok = rf_bake_plan(out, w);
		pthread_mutex_unlock(&g_render_lock);
		return ok;
	}
	pthread_mutex_lock(&g_render_lock);
	drawer_items_build();
	sh_build();
	double open0 = g_room_open, cc0 = g_cc_out, pb0 = g_pb_open;
	g_room_open = 0.0;
	g_cc_out = layer == 4 ? 1.0 : 0.0;
	g_pb_open = layer == 5 ? 1.0 : 0.0;
	g_room_layer = layer;

	/* the case as wide as any screen makes it, of those it is pictured
	 * for: 5:4 to 21:9, and desks up to a 2560 point screen's */
	static const double ASPECT[] = { 1.25, 4.0 / 3.0, 1.5, 1.6, 16.0 / 9.0, 2.0, 2.37 };
	static const double HX[] = { 16.0, 21.33, 28.44 };
	double best = 0.0, ba = 1.6, bh = 16.0;
	/* DESK_FACES_BAKE_AT=aspect:hx -- everything pictured in that one
	 * room, for trying the pack against the faces pictured in place */
	double at_a = 0.0, at_h = 0.0;
	const char *bat = getenv("DESK_FACES_BAKE_AT");
	bool one = bat && sscanf(bat, "%lf:%lf", &at_a, &at_h) == 2 && at_a > 0.2;
	for (size_t i = 0; !one && i < sizeof ASPECT / sizeof ASPECT[0]; i++) {
		for (size_t j = 0; j < sizeof HX / sizeof HX[0]; j++) {
			struct room_cam c;
			g_rm_hx = HX[j];
			room_cam_shelf(ASPECT[i], &c);
			if (g_rm_sh_hx > best) {
				best = g_rm_sh_hx;
				ba = ASPECT[i];
				bh = HX[j];
			}
		}
	}
	struct room_cam cams[1 + ROOM_TILT_FRAMES];
	int camw[1 + ROOM_TILT_FRAMES];
	struct rface_set *fs = calloc(1, sizeof *fs), *desk = calloc(1, sizeof *desk);
	bool ok = fs && desk;
	if (one) {
		ba = at_a;
		bh = fmax(at_h, RM_HX_MIN);
	}
	if (ok) {
		g_rm_hx = bh;
		int nc = rf_view_cams(which, ba, w, cams, camw);
		printf("the room: %.2f to 1, desk %.1f wide: case %.1f wide\n", ba, 2.0 * bh,
			2.0 * g_rm_sh_hx);
		rf_layer(fs, layer);
		int di = -1;
		for (int i = 0; i < fs->n; i++) {
			if (!strcmp(fs->f[i].what, "desk")) {
				fs->f[i].trace = true;
				di = i;
			}
			/* the desk, the wall and the floor are the bookcase's pack's,
			 * in every view, and so are the things on its board; the
			 * console and the post box their own -- and the clock on the
			 * desk every view's own, its glass showing the room as it is
			 * seen from there */
			const char *w8 = fs->f[i].what;
			bool room = (!strncmp(w8, "desk", 4) || !strncmp(w8, "wall", 4)
				|| !strncmp(w8, "floor", 5) || !strncmp(w8, "skirting", 8)
				|| (!strncmp(w8, "gd-", 3) && strcmp(w8, "gd-tray")))
				&& !strstr(w8, "-bare");
			if ((layer != 3 && room) || (layer != 4 && !strncmp(w8, "cc-", 3))
				|| (layer != 5 && !strncmp(w8, "pb-", 3))) {
				fs->f[i].trace = true;
			}
		}
		double t0 = room_now();
		long t1 = rf_bake(fs, cams, camw, nc, cams[0].eye, true);
		printf("  %ld texels in %.1f s\n", t1, room_now() - t0);

		/* and the post box's door shut, as the bookcase and the console and
		 * the start of the move to it see its outside: open, nothing does.
		 * Lit as the bookcase sees it, from where it is most looked at. */
		if (layer == 5) {
			struct room_cam sc[2 + ROOM_TILT_FRAMES];
			int scw[2 + ROOM_TILT_FRAMES];
			int sn = rf_view_cams(which, ba, w, sc, scw);
			room_cam_shelf(ba, &sc[sn]);
			scw[sn++] = w;
			g_pb_open = 0.0;
			rf_layer(desk, layer);
			int k = -1;
			for (int i = 0; i < desk->n; i++) {
				desk->f[i].trace = strcmp(desk->f[i].what, "pb-door") != 0;
				k = desk->f[i].trace ? k : i;
			}
			t0 = room_now();
			long t3 = rf_bake(desk, sc, scw, sn, sc[sn - 1].eye, true);
			printf("  its door shut: %ld texels in %.1f s\n", t3, room_now() - t0);
			for (int i = 0; k >= 0 && i < fs->n; i++) {
				if (!strcmp(fs->f[i].what, "pb-door")) {
					struct rface open = fs->f[i];
					fs->f[i] = desk->f[k];
					desk->f[k] = open;   /* freed with the rest */
					break;
				}
			}
			g_pb_open = 1.0;
		}
		/* and the desk as wide and as deep as any of them */
		if (layer != 3) {
			di = -1;
			goto written;
		}
		g_rm_hx = one ? bh : 28.44;
		nc = rf_view_cams(which, one ? ba : 1.6, w, cams, camw);
		printf("the desk: %.1f wide, %.1f deep\n", 2.0 * g_rm_hx, RM_ZN - RM_ZF);
		rf_layer(desk, layer);
		for (int i = 0; i < desk->n; i++) {
			desk->f[i].trace = strcmp(desk->f[i].what, "desk") != 0;
		}
		t0 = room_now();
		long t2 = rf_bake(desk, cams, camw, nc, cams[0].eye, true);
		printf("  %ld texels in %.1f s\n", t2, room_now() - t0);
		for (int i = 0; di >= 0 && i < desk->n; i++) {
			if (!desk->f[i].trace) {
				fs->f[di] = desk->f[i];
				memset(&desk->f[i], 0, sizeof desk->f[i]);
			}
		}
	written:
		ok = rf_write(fs, out);
		printf("%s %s\n", ok ? "wrote" : "could not write", out);
	}
	if (fs) {
		rf_free(fs);
	}
	if (desk) {
		rf_free(desk);
	}
	free(fs);
	free(desk);
	struct room_cam dc;
	room_cam_build(1.6, &dc);
	g_room_open = open0;
	g_cc_out = cc0;
	g_pb_open = pb0;
	pthread_mutex_unlock(&g_render_lock);
	return ok;
}

bool desk_room_faces_test(int w, int h, const char *dir)
{
	wood_bake_begin();
	pthread_mutex_lock(&g_render_lock);
	drawer_items_build();
	sh_build();
	double open0 = g_room_open, cc0 = g_cc_out, pb0 = g_pb_open;
	struct room_q bq0 = g_bq;
	if (dir && *dir && mkdir(dir, 0755) != 0 && errno != EEXIST) {
		printf("cannot make %s: %s\n", dir, strerror(errno));
		dir = NULL;
	}
	/* DESK_FACES_SS=1: a sample a pixel, as at 2D (and 3, Ultra's nine) */
	const char *ssv = getenv("DESK_FACES_SS");
	if (ssv && atoi(ssv) >= 1 && atoi(ssv) <= 3) {
		g_bq.ss = atoi(ssv);
	}
	/* DESK_FACES_EDGE=2: two of a traced edge's four, as at Low */
	const char *edv = getenv("DESK_FACES_EDGE");
	if (edv) {
		g_bq.edge_traced = atoi(edv);
	}
	/* DESK_FACES_LAYER=4: the console's view and the move to it instead;
	 * 1 and 2, the desk's own sheet and its drawer's */
	const char *lay = getenv("DESK_FACES_LAYER");
	const int layer = lay && atoi(lay) >= 1 && atoi(lay) <= 5 ? atoi(lay) : 3;
	const int which = layer <= 2 ? RF_VIEW_DESK : layer == 4 ? DESK_VIEW_CONSOLE
		: layer == 5 ? DESK_VIEW_POST : DESK_VIEW_SHELF;
	g_room_open = layer <= 3 ? 1.0 : 0.0;
	g_cc_out = layer == 4 ? 1.0 : 0.0;
	g_pb_open = layer == 5 ? 1.0 : 0.0;
	g_room_layer = layer;

	/* DESK_FACES_FROM=WxH[:hx]: the faces pictured in the room of that
	 * screen and that desk, and laid in this one -- whether one set of
	 * pictures will do for rooms of other shapes */
	struct rface_set *src = NULL;
	long src_texels = 0;
	double t_src = 0.0;
	const char *from = getenv("DESK_FACES_FROM");
	int fw = 0, fh = 0;
	double fhx = RM_HX_MIN;
	if (from && sscanf(from, "%dx%d:%lf", &fw, &fh, &fhx) >= 2 && fw >= 16 && fh >= 16) {
		g_rm_hx = fmax(fhx, RM_HX_MIN);
		struct room_cam fcam;
		rf_view_cam(which, (double)fw / fh, &fcam);
		src = calloc(1, sizeof *src);
		if (src) {
			rf_layer(src, layer);
			double t0 = room_now();
			src_texels = rf_bake(src, &fcam, &fw, 1, fcam.eye, true);
			t_src = room_now() - t0;
			printf("pictured in the room of %d x %d, desk %.1f wide, case %.1f\n",
				fw, fh, 2.0 * g_rm_hx, 2.0 * g_rm_sh_hx);
		}
	}

	const char *thx = getenv("DESK_TEST_HX");
	g_rm_hx = thx && *thx ? fmax(atof(thx), RM_HX_MIN) : RM_HX_MIN;
	double aspect = (double)w / (double)h;
	struct room_cam cam;
	rf_view_cam(which, aspect, &cam);
	/* DESK_FACES_FILM: the move up to the bookcase as well, frame by frame,
	 * with the drawer shut as it is for the move */
	bool film = getenv("DESK_FACES_FILM") != NULL;
	if (film) {
		g_room_open = 0.0;
	}
	int hw = w * ICON_SS, hh = h * ICON_SS;
	size_t n = (size_t)hw * hh;
	printf("the bookcase, %d x %d (%d x %d samples), desk %.1f wide, case %.1f\n\n",
		w, h, hw, hh, 2.0 * g_rm_hx, 2.0 * g_rm_sh_hx);

	float *ht = calloc(n * 4, sizeof(float)), *hl = calloc(n * 4, sizeof(float));
	unsigned char *it = calloc(n, 1), *il = calloc(n, 1), *need = calloc(n, 1);
	struct rface_set *fs = calloc(1, sizeof *fs);
	bool ok = ht && hl && it && il && need && fs;
	if (ok) {
		struct room_ctx ct = { 0 };
		ct.hi = ht;
		ct.idhi = it;
		ct.hw = hw;
		ct.hh = hh;
		ct.layer = layer;
		ct.cam = cam;
		double t0 = room_now();
		room_trace_cpu(&ct);
		double t_trace = room_now() - t0;

		rf_layer(fs, layer);
		int own_n = fs->n;
		t0 = room_now();
		long texels;
		const char *packp = getenv("DESK_FACES_PACK");
		struct rface_set *pack = NULL;
		if (packp) {
			FILE *fp = fopen(packp, "rb");
			unsigned char *buf = NULL;
			long len = 0;
			if (fp && fseek(fp, 0, SEEK_END) == 0 && (len = ftell(fp)) > 0
				&& fseek(fp, 0, SEEK_SET) == 0 && (buf = malloc((size_t)len))
				&& fread(buf, 1, (size_t)len, fp) == (size_t)len) {
				pack = rf_read(buf, (size_t)len);
			}
			if (fp) {
				fclose(fp);
			}
			free(buf);
			printf("the pack %s: %s, %ld bytes\n", packp, pack ? "read" : "NOT read", len);
		}
		/* DESK_FACES_PACK2, _PACK3: the other packs the view lays from */
		struct rface_set *more[2] = { NULL, NULL };
		for (int q = 0; pack && q < 2; q++) {
			const char *mp = getenv(q ? "DESK_FACES_PACK3" : "DESK_FACES_PACK2");
			FILE *fp = mp ? fopen(mp, "rb") : NULL;
			unsigned char *buf = NULL;
			long len = 0;
			if (fp && fseek(fp, 0, SEEK_END) == 0 && (len = ftell(fp)) > 0
				&& fseek(fp, 0, SEEK_SET) == 0 && (buf = malloc((size_t)len))
				&& fread(buf, 1, (size_t)len, fp) == (size_t)len) {
				more[q] = rf_read(buf, (size_t)len);
			}
			if (fp) {
				fclose(fp);
			}
			free(buf);
			if (mp) {
				printf("the pack %s: %s\n", mp, more[q] ? "read" : "NOT read");
			}
		}
		if (pack) {
			const struct rface_set *packs[3] = { pack, more[0], more[1] };
			rf_link(fs, packs, 3);
			struct room_cam plan;
			if (which != RF_VIEW_DESK) {
				room_cam_lerp(aspect, ROOM_TILT_U0, which, &plan);
			}
			rf_view_cam(which, aspect, &cam);
			fs->eye0 = which != RF_VIEW_DESK ? plan.eye : cam.eye;
			fs->eye1 = cam.eye;
			texels = 0;
		} else if (src) {
			/* the same faces, in the same order, of the other room */
			for (int i = 0; i < own_n; i++) {
				if (i < src->n && !strcmp(src->f[i].what, fs->f[i].what)) {
					fs->f[i].src = &src->f[i];
				} else {
					fs->f[i].trace = true;
				}
			}
			texels = src_texels;
		} else {
			struct room_cam cams[1 + ROOM_TILT_FRAMES];
			int camw[1 + ROOM_TILT_FRAMES];
			int ncam = 1;
			cams[0] = cam;
			camw[0] = w;
			for (int k = 0; film && k < ROOM_TILT_FRAMES; k++) {
				room_cam_lerp(aspect, ROOM_TILT_U0 + (ROOM_TILT_U1 - ROOM_TILT_U0) * k
					/ (ROOM_TILT_FRAMES - 1), which, &cams[ncam]);
				camw[ncam++] = w / 4;
			}
			texels = rf_bake(fs, cams, camw, ncam, cam.eye, false);
		}
		double t_bake = src ? t_src : room_now() - t0;
		const char *ltpu = getenv("DESK_FACES_LTPU");
		double lt = ltpu ? atof(ltpu) : 4.0;
		double t_light = 0.0;
		long lights = 0;
		if (lt > 0.0) {
			t0 = room_now();
			if (pack) {
				long nn = 0, mm = 0;
				rf_room_light(fs, layer, &nn, &mm);
				lights = nn + mm;
			} else {
				lights = rf_light(fs, lt);
			}
			t_light = room_now() - t0;
		}

		/* DESK_FACES_PROBE=x,y: what the ray through that pixel meets, and
		 * which faces it goes through */
		const char *probe = getenv("DESK_FACES_PROBE");
		int px_, py_;
		if (probe && sscanf(probe, "%d,%d", &px_, &py_) == 2) {
			double pu = (px_ + 0.5) / w, pv = (py_ + 0.5) / h;
			vec3 ro, rd;
			room_ray(&cam, pu, pv, &ro, &rd);
			double tt = 0.02;
			for (int i = 0; i < 400 && tt < 300.0; i++) {
				hit3 hh = scene_map(vadd(ro, vmul(rd, tt)), ROOM_KIND);
				if (hh.d < 0.002 && hh.id > 0) {
					vec3 hp = vadd(ro, vmul(rd, tt));
					printf("probe %d,%d: material %d at (%.2f %.2f %.2f), t %.2f\n", px_, py_,
						hh.id, hp.x, hp.y, hp.z, tt);
					break;
				}
				tt += fmax(hh.d, 0.002);
			}
			for (int i = 0; i < fs->n; i++) {
				double a, b, t;
				if (rf_hit(&fs->f[i], &cam, pu, pv, &a, &b, &t) && a >= 0 && a <= 1 && b >= 0
					&& b <= 1) {
					printf("  through %d %s at t %.2f (a %.2f b %.2f)%s", i, fs->f[i].what, t,
						a, b, fs->f[i].trace ? " traced" : "");
					const struct rface *tf = fs->f[i].src;
					if (tf && tf->lv) {
						double ta = a, tb = b;
						rf_move(&fs->f[i], &ta, &tb);
						float q[4];
						rf_sample(tf, RF_REST, ta, tb, 0.0, q);
						int tx = (int)(ta * tf->tw[0]), ty = (int)(tb * tf->th[0]);
						tx = tx < 0 ? 0 : tx >= tf->tw[0] ? tf->tw[0] - 1 : tx;
						ty = ty < 0 ? 0 : ty >= tf->th[0] ? tf->th[0] - 1 : ty;
						printf(" -> (%.3f %.3f) alpha %.2f own %d", ta, tb, q[3],
							tf->own[(size_t)ty * tf->tw[0] + tx]);
					}
					printf("\n");
				}
			}
		}
		struct room_ctx cl = ct;
		cl.hi = hl;
		cl.idhi = il;
		t0 = room_now();
		rf_lay_sheet(fs, &cl, need);
		double t_lay = room_now() - t0;
		size_t needed = 0;
		for (size_t i = 0; i < n; i++) {
			needed += need[i];
		}
		cairo_surface_t *s_laid = room_resolve(hl, il, w, h, NULL);

		/* and what no face covers, traced */
		cl.need = need;
		t0 = room_now();
		room_trace_cpu(&cl);
		double t_rest = room_now() - t0;

		size_t own = 0;
		for (size_t i = 0; i < n; i++) {
			own += it[i] != il[i];
		}
		cairo_surface_t *s_trace = room_resolve(ht, it, w, h, NULL);
		cairo_surface_t *s_both = room_resolve(hl, il, w, h, NULL);
		int over2 = 0, maxd = 0;
		cairo_surface_t *s_diff = selftest_diff(s_both, s_trace, &over2, &maxd);
		double mean5 = 0.0;
		int over5 = rf_soft_diff(s_both, s_trace, &mean5);
		cairo_surface_t *s_need = cairo_image_surface_create(CAIRO_FORMAT_A8, hw, hh);
		unsigned char *nd = cairo_image_surface_get_data(s_need);
		int ns = cairo_image_surface_get_stride(s_need);
		for (int y = 0; y < hh; y++) {
			for (int x = 0; x < hw; x++) {
				nd[(size_t)y * ns + x] = need[(size_t)y * hw + x] ? 255 : 0;
			}
		}
		cairo_surface_mark_dirty(s_need);

		int nf = 0;
		for (int i = 0; i < fs->n; i++) {
			nf += fs->f[i].lv > 0 || (fs->f[i].src && fs->f[i].src->lv > 0);
		}
		if (getenv("DESK_FACES_DEBUG")) {
			for (int i = 0; i < fs->n; i++) {
				const struct rface *f = &fs->f[i], *tf = f->src ? f->src : f;
				if (f->trace && !f->src) {
					continue;
				}
				/* how much of it starts inside something: a face turned
				 * the wrong way round is all of it */
				int in = 0;
				for (int k = 0; k < 81; k++) {
					vec3 q = vadd(f->o, vadd(vmul(f->u, (k % 9 + 0.5) / 9.0),
						vmul(f->v, (k / 9 + 0.5) / 9.0)));
					in += scene_map(vadd(q, vmul(f->n, f->out > 0.0 ? f->out : 0.05)),
						ROOM_KIND).d < 0.0;
				}
				printf("  %2d %-9s %s lv %d %5d x %-5d missed %ld  inside %d%%\n", i, f->what,
					f->src ? "from" : "own ", tf->lv, tf->tw[0], tf->th[0],
					atomic_load(&g_rf_missed[i]), in * 100 / 81);
			}
		}
		printf("traced, all of it:   %7.2f s\n", t_trace);
		printf("faces pictured:      %7.2f s  (%d of %d seen, %.1f M texels)\n",
			t_bake, nf, fs->n, texels / 1e6);
		printf("the lamp worked out: %7.2f s  (%ld points, %.0f to the unit)\n", t_light,
			lights, lt);
		printf("laid:                %7.2f s\n", t_lay);
		printf("the rest traced:     %7.2f s  (%.1f %% of the samples)\n", t_rest,
			100.0 * needed / n);
		printf("laid and the rest:   %7.2f s  (%.0f %% of the trace)\n", t_lay + t_rest,
			100.0 * (t_lay + t_rest) / fmax(t_trace, 1e-6));
		printf("owners that differ:  %.3f %% of the samples\n", 100.0 * own / n);
		printf("pixels > 2 apart:    %.3f %%, the most %d\n",
			100.0 * over2 / ((double)w * h), maxd);
		printf("softened 5x5:        %.3f %% > 2 apart, %.2f on average\n",
			100.0 * over5 / ((double)w * h), mean5);
		selftest_png(dir, "shelf", "trace", s_trace);
		selftest_png(dir, "shelf", "laid", s_laid);
		selftest_png(dir, "shelf", "both", s_both);
		selftest_png(dir, "shelf", "diff", s_diff);
		selftest_png(dir, "shelf", "need", s_need);
		cairo_surface_destroy(s_trace);
		cairo_surface_destroy(s_laid);
		cairo_surface_destroy(s_both);
		cairo_surface_destroy(s_diff);
		cairo_surface_destroy(s_need);
		if (film) {
			rf_film_test(fs, w, h, dir, layer);
		}
		if (pack) {
			rf_free(pack);
			free(pack);
		}
		for (int q = 0; q < 2; q++) {
			if (more[q]) {
				rf_free(more[q]);
				free(more[q]);
			}
		}
		if (getenv("DESK_FACES_DUMP") && dir) {
			rf_dump(fs, dir);
		}
		rf_free(fs);
	}
	if (src) {
		rf_free(src);
		free(src);
	}
	free(ht);
	free(hl);
	free(it);
	free(il);
	free(need);
	free(fs);
	/* the desk camera owns the aspect-derived geometry; put it back */
	struct room_cam desk;
	room_cam_build(aspect, &desk);
	g_room_open = open0;
	g_cc_out = cc0;
	g_pb_open = pb0;
	g_bq = bq0;
	pthread_mutex_unlock(&g_render_lock);
	return ok;
}

bool desk_room_selftest(int w, int h, const char *dir)
{
	wood_bake_begin();
	pthread_mutex_lock(&g_render_lock);
	drawer_items_build();
	sh_build();
	double open0 = g_room_open, cc0 = g_cc_out, pb0 = g_pb_open;
	/* the directory the pictures go to, made if it is not there yet */
	if (dir && *dir && mkdir(dir, 0755) != 0 && errno != EEXIST) {
		printf("cannot make %s: %s\n", dir, strerror(errno));
		dir = NULL;
	}
	/* straight at the driver: no verdict from before and no trial child,
	 * because this is the trial -- and a crash here is the answer */
	g_rgpu.dead = false;
	g_rgpu.trial = true;
	struct desk_gpu *g = room_gpu();
	g_rgpu.trial = false;
	if (!g) {
		pthread_mutex_unlock(&g_render_lock);
		return false;
	}
	/* the desk it is of: as designed, or as wide as DESK_TEST_HX says,
	 * for the corners that move out with a wider one */
	const char *thx = getenv("DESK_TEST_HX");
	g_rm_hx = thx && *thx ? fmax(atof(thx), RM_HX_MIN) : RM_HX_MIN;
	printf("device  : %s\n", desk_gpu_name(g));
	printf("sheet   : %d x %d (%d x %d samples), desk %.1f wide\n\n", w, h,
		w * ICON_SS, h * ICON_SS, 2.0 * g_rm_hx);
	printf("%-8s %7s %7s  %6s %6s  %8s %8s  %6s %4s\n", "", "gpu s", "cpu s",
		"cov %", "own %", "col >4%", "worst", "px>2 %", "max");

	static const struct {
		const char *name;
		int layer, cam;
		double open, cc, pb, lid;
	} SHEETS[] = {
		{ "desk", 1, 0, 1.0, 0.0, 0.0, 1.0 },
		{ "drawer", 2, 0, 1.0, 0.0, 0.0, 1.0 },
		{ "shelf", 3, 1, 1.0, 0.0, 0.0, 1.0 },
		{ "console", 4, 2, 0.0, 1.0, 0.0, 1.0 },
		/* the console's other sheet, its lid up (room_bare_make) */
		{ "lid-up", 4, 2, 0.0, 1.0, 0.0, 2.0 },
		{ "post", 5, 3, 0.0, 0.0, 1.0, 1.0 },
	};
	bool all = true;
	int hw = w * ICON_SS, hh = h * ICON_SS;
	size_t n = (size_t)hw * hh;
	float *hg = malloc(n * 4 * sizeof(float)), *hc = malloc(n * 4 * sizeof(float));
	unsigned char *ig = malloc(n), *ic = malloc(n);
	for (size_t k = 0; hg && hc && ig && ic && k < sizeof SHEETS / sizeof SHEETS[0]; k++) {
		struct room_cam cam;
		double aspect = (double)w / (double)h;
		switch (SHEETS[k].cam) {
		case 0: room_cam_build(aspect, &cam); break;
		case 1: room_cam_shelf(aspect, &cam); break;
		case 2: room_cam_console(aspect, &cam); break;
		default: room_cam_post(aspect, &cam); break;
		}
		g_room_open = SHEETS[k].open;
		g_cc_out = SHEETS[k].cc;
		g_pb_open = SHEETS[k].pb;
		g_cc_lid = SHEETS[k].lid;
		g_room_layer = SHEETS[k].layer;

		struct room_ctx cg = { 0 }, cc = { 0 };
		cg.hi = hg;
		cg.idhi = ig;
		cc.hi = hc;
		cc.idhi = ic;
		cg.hw = cc.hw = hw;
		cg.hh = cc.hh = hh;
		cg.layer = cc.layer = SHEETS[k].layer;
		cg.cam = cc.cam = cam;

		long counted = 0;
		room_gpu_uniforms(g, &cg);
		double t0 = room_now();
		bool drew = room_gpu_tiles(g, &cg, &counted);
		double tg = room_now() - t0;
		t0 = room_now();
		room_trace_cpu(&cc);
		double tc = room_now() - t0;
		if (!drew) {
			printf("%-8s the GPU failed a draw\n", SHEETS[k].name);
			all = false;
			break;
		}

		size_t cov = 0, own = 0, col = 0;
		double worst = 0.0;
		for (size_t i = 0; i < n; i++) {
			const float *a = hg + i * 4, *b = hc + i * 4;
			if ((a[3] >= 0.5f) != (b[3] >= 0.5f)) {
				cov++;
				continue;
			}
			own += ig[i] != ic[i];
			for (int c = 0; c < 3; c++) {
				double d = fabs((double)a[c] - b[c]);
				if (!isfinite(a[c])) {
					d = 1e9;
				}
				if (d > 0.01 + 0.04 * fmax(fabs(a[c]), fabs(b[c]))) {
					col++;
					break;
				}
				worst = fmax(worst, d / fmax(fabs(b[c]), 0.05));
			}
		}
		cairo_surface_t *sg = room_resolve(hg, ig, w, h, NULL);
		cairo_surface_t *sc = room_resolve(hc, ic, w, h, NULL);
		int over2 = 0, maxd = 0;
		cairo_surface_t *sd = selftest_diff(sg, sc, &over2, &maxd);
		double pc = 100.0 / (double)n, pp = 100.0 / ((double)w * h);
		printf("%-8s %7.2f %7.2f  %6.3f %6.3f  %8.3f %8.3f  %6.3f %4d\n",
			SHEETS[k].name, tg, tc, cov * pc, own * pc, col * pc, worst,
			over2 * pp, maxd);
		/* The same test the bake's spot check makes, over every sample
		 * rather than two hundred of them: a tenth disagreeing is a GPU
		 * that is drawing something else. */
		if ((cov + own + col) * 10 > n) {
			all = false;
		}
		selftest_png(dir, SHEETS[k].name, "gpu", sg);
		selftest_png(dir, SHEETS[k].name, "cpu", sc);
		selftest_png(dir, SHEETS[k].name, "diff", sd);
		cairo_surface_destroy(sg);
		cairo_surface_destroy(sc);
		cairo_surface_destroy(sd);
	}
	free(hg);
	free(hc);
	free(ig);
	free(ic);
	room_gpu_end();
	g_room_open = open0;
	g_cc_out = cc0;
	g_pb_open = pb0;
	g_cc_lid = 1.0;
	pthread_mutex_unlock(&g_render_lock);
	printf("\ncov/own: samples that hit something else, or belong to something\n"
		"else. col: samples more than 4%% apart. worst: the largest relative\n"
		"difference among the rest. px>2: pixels more than two levels apart\n"
		"on screen, and max, the most any pixel is.\n");
	if (dir && *dir) {
		printf("\nThe sheets are in %s: <sheet>-gpu.png, <sheet>-cpu.png, and\n"
			"<sheet>-diff.png, the difference eight times over.\n", dir);
	}
	printf("\n%s\n", all ? "The GPU draws the room the C does."
		: "The GPU does NOT draw the room the C does.");
	return all;
}

/* desk-gputest --tour: the room from any camera, a frame a line -- for a film of the room
 * itself, the camera going round it rather than between its four views. Each line:
 *     name  eye.x eye.y eye.z  at.x at.y at.z  fov  cc_out pb_open cc_lid drawer_open
 * in the room's own units (the desk top at y 0, its near edge at z RM_ZN, a unit 45 points),
 * fov the frame's width at a distance of one. The whole room is in the bookcase's layer;
 * the pictures and lettering laid over the views (the clock's figures, the labels) are not,
 * nor is the lamp's coming up at login. A frame is <dir>/<name>.png, w by h. */
static void tour_cam(vec3 eye, vec3 at, double fov, double aspect, struct room_cam *c)
{
	memset(c, 0, sizeof *c);
	c->eye = eye;
	c->ww = vnorm(vsub(at, eye));
	vec3 up = fabs(c->ww.y) > 0.999 ? v3(0.0, 0.0, -1.0) : v3(0.0, 1.0, 0.0);
	c->uu = vnorm(vcross(c->ww, up));
	c->vv = vcross(c->uu, c->ww);
	c->fov_x = fov;
	c->fov_y = fov / fmax(aspect, 0.2);
	c->pitch = asin(fmin(fmax(-c->ww.y, -1.0), 1.0));
	c->z0 = RM_ZF;
	c->z1 = RM_ZN;
}

bool desk_room_tour(const char *path, const char *dir, int w, int h)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		printf("cannot read %s: %s\n", path, strerror(errno));
		return false;
	}
	if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
		printf("cannot make %s: %s\n", dir, strerror(errno));
		fclose(f);
		return false;
	}
	wood_bake_begin();
	pthread_mutex_lock(&g_render_lock);
	drawer_items_build();
	sh_build();
	double open0 = g_room_open, cc0 = g_cc_out, pb0 = g_pb_open;
	struct room_q bq0 = g_bq;
	const char *ss = getenv("DESK_TOUR_SS");
	g_bq.ss = ss && *ss ? (int)fmax(1.0, fmin(4.0, atof(ss))) : 3;
	g_rgpu.dead = false;
	g_rgpu.trial = true;
	struct desk_gpu *g = room_gpu();
	g_rgpu.trial = false;
	if (!g) {
		g_bq = bq0;
		pthread_mutex_unlock(&g_render_lock);
		fclose(f);
		printf("no GPU to trace with\n");
		return false;
	}
	const char *thx = getenv("DESK_TEST_HX");
	g_rm_hx = thx && *thx ? fmax(atof(thx), RM_HX_MIN) : RM_HX_MIN;
	/* the four views' cameras, which set the room out for this shape (the bookcase's
	 * width, where the console stands), and say where things are to aim a path at */
	double aspect = (double)w / (double)h;
	struct room_cam vc[4];
	room_cam_build(aspect, &vc[0]);
	room_cam_shelf(aspect, &vc[1]);
	room_cam_console(aspect, &vc[2]);
	room_cam_post(aspect, &vc[3]);
	static const char *const VN[4] = { "desk", "shelf", "console", "post" };
	for (int i = 0; i < 4; i++) {
		printf("view %-8s eye %8.3f %8.3f %8.3f  looking %6.3f %6.3f %6.3f  fov %.3f\n", VN[i],
			vc[i].eye.x, vc[i].eye.y, vc[i].eye.z, vc[i].ww.x, vc[i].ww.y, vc[i].ww.z, vc[i].fov_x);
	}
	printf("room    desk half width %.2f, top z %.2f..%.2f, floor y %.1f, bookcase z %.2f..%.2f "
		"(cornice y %.1f, half width %.2f)\n", RM_HX, RM_ZF, RM_ZN, RM_FLOOR, RM_SH_ZB, RM_SH_ZF,
		RM_SH_CORN_Y, g_rm_sh_hx);
	printf("        console x %.2f..%.2f z %.2f..%.2f, post box at %.2f, %.2f, lamp %.2f %.2f %.2f\n",
		RM_CC_X0, RM_CC_X1, RM_CC_ZB, RM_CC_PZ0, RM_PB_CX, RM_PB_CZ,
		ROOM_LAMP_POS0.x * ROOM_LAMP_K, ROOM_LAMP_POS0.y * ROOM_LAMP_K,
		RM_ZN + (ROOM_LAMP_POS0.z - RM_ZN) * ROOM_LAMP_K);
	fflush(stdout);
	int hw = w * ICON_SS, hh = h * ICON_SS;
	size_t n = (size_t)hw * hh;
	float *hi = malloc(n * 4 * sizeof(float));
	unsigned char *idhi = malloc(n);
	bool ok = hi && idhi;
	char line[512];
	int frames = 0;
	double t_all = room_now();
	while (ok && fgets(line, sizeof line, f)) {
		char name[128];
		double ex, ey, ez, tx, ty, tz, fov, cc, pb, lid, open;
		if (line[0] == '#' || sscanf(line, "%127s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf", name,
				&ex, &ey, &ez, &tx, &ty, &tz, &fov, &cc, &pb, &lid, &open) != 12) {
			continue;
		}
		struct room_cam cam;
		tour_cam(v3(ex, ey, ez), v3(tx, ty, tz), fov, aspect, &cam);
		g_room_open = open;
		g_cc_out = cc;
		g_pb_open = pb;
		g_cc_lid = lid;
		g_room_layer = 3;
		struct room_ctx ctx = { 0 };
		memset(hi, 0, n * 4 * sizeof(float));
		memset(idhi, 0, n);
		ctx.hi = hi;
		ctx.idhi = idhi;
		ctx.hw = hw;
		ctx.hh = hh;
		ctx.layer = 3;
		ctx.cam = cam;
		long counted = 0;
		double t0 = room_now();
		room_gpu_uniforms(g, &ctx);
		if (!room_gpu_tiles(g, &ctx, &counted)) {
			printf("%s: the GPU failed a draw\n", name);
			ok = false;
			break;
		}
		cairo_surface_t *img = room_resolve(hi, idhi, w, h, NULL);
		char out[1024];
		snprintf(out, sizeof out, "%s/%s.png", dir, name);
		cairo_surface_write_to_png(img, out);
		cairo_surface_destroy(img);
		frames++;
		printf("%s %.2f s\n", name, room_now() - t0);
		fflush(stdout);
	}
	fclose(f);
	free(hi);
	free(idhi);
	room_gpu_end();
	g_room_open = open0;
	g_cc_out = cc0;
	g_pb_open = pb0;
	g_cc_lid = 1.0;
	g_bq = bq0;
	pthread_mutex_unlock(&g_render_lock);
	printf("%d frames in %.1f s\n", frames, room_now() - t_all);
	return ok;
}
#else
bool desk_room_selftest(int w, int h, const char *dir)
{
	(void)w;
	(void)h;
	(void)dir;
	printf("built without the GPU path\n");
	return false;
}

bool desk_room_faces_test(int w, int h, const char *dir)
{
	(void)w;
	(void)h;
	(void)dir;
	printf("built without the GPU path\n");
	return false;
}

bool desk_room_faces_bake(const char *out, int w, int layer)
{
	(void)out;
	(void)w;
	(void)layer;
	printf("built without the GPU path\n");
	return false;
}

bool desk_room_tour(const char *path, const char *dir, int w, int h)
{
	(void)path;
	(void)dir;
	(void)w;
	(void)h;
	printf("built without the GPU path\n");
	return false;
}
#endif
