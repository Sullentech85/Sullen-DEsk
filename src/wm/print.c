/* Part of wm.c: Print: the camera, films and screenshots.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* Whether one of Print's nodes has been drawn: asked of its picture's
 * size, kept beside it -- not of b->buffer, which a GLES2 screen lets go
 * of once the picture is on the GPU (and the camera coming up was taken
 * for not drawn, and not shown, from its second frame on). */
static bool snip_has_pic(struct wlr_scene_buffer *b)
{
	int w, h;
	return b && scene_pic_size(b, &w, &h);
}

static void snip_rect(struct wlr_scene_rect *r, int x, int y, int w, int h)
{
	wlr_scene_node_set_enabled(&r->node, w > 0 && h > 0);
	if (w > 0 && h > 0) {
		wlr_scene_node_set_position(&r->node, x, y);
		wlr_scene_rect_set_size(r, w, h);
	}
}

/* A sprite of Print's that is stretched (the penumbra, the glass's edge):
 * shown from the middles of its end pixels, so that stretched it begins
 * and ends exactly as drawn, with nothing from past its edges in it. */
static void snip_sprite_ends(struct wlr_scene_buffer *n, int w, int h)
{
	struct wlr_fbox src = { w > 1 ? 0.5 : 0.0, h > 1 ? 0.5 : 0.0, w > 1 ? w - 1.0 : w,
		h > 1 ? h - 1.0 : h };
	scene_source(n, &src);
}

/* All the screens, as they are laid out. */
static struct wlr_box snip_bounds(struct desk_server *server)
{
	struct wlr_box b;
	wlr_output_layout_get_box(server->output_layout, NULL, &b);
	if (wlr_box_empty(&b)) {
		b = (struct wlr_box){ 0, 0, server->desk_w, server->desk_h };
	}
	return b;
}

/* Where a screen's viewfinder shows its picture, the camera at the eye: a
 * little in from the screen's edges, the dark round it the eyecup's and
 * the display in the dark under it -- as big as that leaves room for, the
 * screen's own shape. */
static struct wlr_box snip_view_box(struct wlr_box b)
{
	double pt = desk_px_per_pt(b.width);
	double top = SNIP_VIEW_TOP * pt, h = b.height - (SNIP_VIEW_TOP + SNIP_VIEW_FOOT) * pt;
	if (h < b.height * 0.6) {
		h = b.height * 0.6;   /* (a screen too small for all of it) */
		top = b.height * 0.1;
	}
	double w = b.width * h / b.height;
	return (struct wlr_box){ b.x + (int)lround((b.width - w) * 0.5), b.y + (int)lround(top),
		(int)lround(w), (int)lround(h) };
}

/* The still of the screen (x, y) is on: NULL off them all. */
static struct snip_still *snip_still_at(struct desk_server *server, double x, double y)
{
	for (int i = 0; i < server->snip_still_n; i++) {
		if (wlr_box_contains_point(&server->snip_still[i].box, x, y)) {
			return &server->snip_still[i];
		}
	}
	return NULL;
}

/* From where the hand is on a screen -- over its viewfinder's picture, or
 * in the dark round it -- to the point of the screen it is over: in the
 * dark, the nearest edge. Where the screen has no still, the screen. */
static void snip_to_picture(const struct snip_still *st, double x, double y, double *px,
	double *py)
{
	if (!st || st->view.width <= 0 || st->view.height <= 0) {
		*px = x;
		*py = y;
		return;
	}
	*px = st->box.x + clampf_d((x - st->view.x) / st->view.width, 0.0, 1.0) * st->box.width;
	*py = st->box.y + clampf_d((y - st->view.y) / st->view.height, 0.0, 1.0) * st->box.height;
}

/* And back: a part of a screen, where its viewfinder shows it. */
static struct wlr_box snip_to_view(const struct snip_still *st, struct wlr_box a)
{
	if (!st || st->box.width <= 0 || st->box.height <= 0) {
		return a;
	}
	double sx = (double)st->view.width / st->box.width;
	double sy = (double)st->view.height / st->box.height;
	int x0 = st->view.x + (int)lround((a.x - st->box.x) * sx);
	int y0 = st->view.y + (int)lround((a.y - st->box.y) * sy);
	int x1 = st->view.x + (int)lround((a.x + a.width - st->box.x) * sx);
	int y1 = st->view.y + (int)lround((a.y + a.height - st->box.y) * sy);
	return (struct wlr_box){ x0, y0, x1 - x0, y1 - y0 };
}

/* What is lit, to be taken: the area the hand has drawn, inside the
 * screen it was begun on -- or, the dial set to a window or a screen,
 * that one. Of the screens as they are (and their stills). */
static struct wlr_box snip_area(struct desk_server *server)
{
	if (server->snip_mode != 0) {
		return server->snip_target;
	}
	struct wlr_box b = snip_bounds(server);
	if (server->snip_drag >= 0 && server->snip_drag < server->snip_still_n) {
		b = server->snip_still[server->snip_drag].box;
	}
	int x0 = server->snip_x0, x1 = server->snip_x1;
	int y0 = server->snip_y0, y1 = server->snip_y1;
	int l = x0 < x1 ? x0 : x1, r = x0 < x1 ? x1 : x0;
	int t = y0 < y1 ? y0 : y1, d = y0 < y1 ? y1 : y0;
	l = l < b.x ? b.x : l;
	t = t < b.y ? b.y : t;
	r = r > b.x + b.width ? b.x + b.width : r;
	d = d > b.y + b.height ? b.y + b.height : d;
	return (struct wlr_box){ l, t, r > l ? r - l : 0, d > t ? d - t : 0 };
}

/* The still what is lit is seen in: the one it was drawn out on, or the
 * one its middle is on. */
static struct snip_still *snip_area_still(struct desk_server *server, struct wlr_box a)
{
	if (server->snip_mode == 0 && server->snip_drag >= 0
		&& server->snip_drag < server->snip_still_n) {
		return &server->snip_still[server->snip_drag];
	}
	return snip_still_at(server, a.x + a.width * 0.5, a.y + a.height * 0.5);
}

/* And where it is seen: in that viewfinder (as much of it as is in it). */
static struct wlr_box snip_area_view(struct desk_server *server)
{
	struct wlr_box a = snip_area(server);
	struct snip_still *st = snip_area_still(server, a);
	struct wlr_box v = snip_to_view(st, a), in;
	if (st && st->finder) {
		if (!wlr_box_intersection(&in, &v, &st->view)) {
			in = (struct wlr_box){ v.x, v.y, 0, 0 };
		}
		v = in;
	}
	return v;
}

/* The shade round the picture: the light gathered on what is being drawn
 * out, as a lamp's on what it is turned to, and falling off round it
 * over SNIP_PEN_PT into the shade -- four rects where it is deepest, and
 * the penumbra's edges and corners (desk_draw_finder_penumbra), drawn
 * once and stretched. */
static void snip_shade_lay(struct desk_server *server, struct wlr_box a, double pt)
{
	struct wlr_box b = snip_bounds(server);
	int p = (int)lround(SNIP_PEN_PT * pt);
	int ar = a.x + a.width, ad = a.y + a.height;
	int bx = b.x, by = b.y, br = b.x + b.width, bd = b.y + b.height;
	snip_rect(server->snip_dark[0], bx, by, b.width, a.y - p - by);
	snip_rect(server->snip_dark[1], bx, ad + p, b.width, bd - ad - p);
	snip_rect(server->snip_dark[2], bx, a.y - p, a.x - p - bx, a.height + 2 * p);
	snip_rect(server->snip_dark[3], ar + p, a.y - p, br - ar - p, a.height + 2 * p);
	const struct wlr_box pen[8] = {
		{ a.x, a.y - p, a.width, p }, { ar, a.y, p, a.height },
		{ a.x, ad, a.width, p }, { a.x - p, a.y, p, a.height },
		{ a.x - p, a.y - p, p, p }, { ar, a.y - p, p, p },
		{ ar, ad, p, p }, { a.x - p, ad, p, p },
	};
	for (int i = 0; i < 8; i++) {
		struct wlr_scene_buffer *n = server->snip_pen[i];
		bool on = pen[i].width > 0 && pen[i].height > 0;
		wlr_scene_node_set_enabled(&n->node, on);
		if (on) {
			wlr_scene_buffer_set_dest_size(n, pen[i].width, pen[i].height);
			wlr_scene_node_set_position(&n->node, pen[i].x, pen[i].y);
		}
	}
}

/* Whether a is something, all inside b. */
static bool snip_inside(struct wlr_box a, struct wlr_box b)
{
	return a.width > 0 && a.height > 0 && a.x >= b.x && a.y >= b.y
		&& a.x + a.width <= b.x + b.width && a.y + a.height <= b.y + b.height;
}

/* The glass round the picture, its edge as Liquid Glass's: the band of
 * the screen just outside it drawn out across it (from the screen's still,
 * by where the band is shown from), the glass's own edge catching the
 * light (desk_draw_finder_bevel), and the lamp in it -- above the screen,
 * a little left of its middle: along the top edge where the lamp is over
 * it, down the side that faces it, and a little, thrown back, along the
 * bottom -- so that the light on it moves as the picture is drawn out
 * and moved about. */
static void snip_glass_lay(struct desk_server *server, struct snip_still *st, struct wlr_box a,
	double pt)
{
	int g = (int)lround(SNIP_GLASS_PT * pt);
	int ar = a.x + a.width, ad = a.y + a.height;
	double cx = a.x + a.width * 0.5, cy = a.y + a.height * 0.5;
	const struct wlr_box band[8] = {
		{ a.x, a.y - g, a.width, g }, { ar, a.y, g, a.height },
		{ a.x, ad, a.width, g }, { a.x - g, a.y, g, a.height },
		{ a.x - g, a.y - g, g, g }, { ar, a.y - g, g, g }, { ar, ad, g, g }, { a.x - g, ad, g, g },
	};
	double q = g * 0.35;   /* what the glass shows: a third of its width, drawn out */
	const struct wlr_fbox from[4] = {
		{ a.x, a.y - q, a.width, q }, { ar, a.y, q, a.height },
		{ a.x, ad, a.width, q }, { a.x - q, a.y, q, a.height },
	};
	if (st && (!st->finder || st->view.width <= 0 || st->view.height <= 0)) {
		st = NULL;
	}
	/* (none of it out in the viewfinder's dark: where what is lit runs to
	 * the picture's edge, the viewfinder's own glass is there) */
	struct wlr_box in = st ? st->view : snip_bounds(server);
	for (int i = 0; i < 4; i++) {
		bool on = snip_inside(band[i], in);
		struct wlr_scene_buffer *gl = server->snip_glass[i];
		if (st && on) {
			if (gl->buffer != &st->buf->base) {
				scene_swap(gl, &st->buf->base);
			}
			/* (the band as the viewfinder shows it, from the still) */
			double sx = st->buf->base.width / (double)st->view.width;
			double sy = st->buf->base.height / (double)st->view.height;
			struct wlr_fbox src = { (from[i].x - st->view.x) * sx, (from[i].y - st->view.y) * sy,
				from[i].width * sx, from[i].height * sy };
			scene_source(gl, &src);
			wlr_scene_buffer_set_dest_size(gl, band[i].width, band[i].height);
			wlr_scene_node_set_position(&gl->node, band[i].x, band[i].y);
		}
		wlr_scene_node_set_enabled(&gl->node, st && on);
	}
	for (int i = 0; i < 8; i++) {
		struct wlr_scene_buffer *bv = server->snip_bevel[i];
		bool on = snip_inside(band[i], in);
		wlr_scene_node_set_enabled(&bv->node, on);
		if (on) {
			wlr_scene_buffer_set_dest_size(bv, band[i].width, band[i].height);
			wlr_scene_node_set_position(&bv->node, band[i].x, band[i].y);
		}
	}
	struct wlr_box ob = st ? st->view : snip_bounds(server);
	double lx = ob.x + ob.width * 0.42, ly = ob.y - ob.height * 0.6;
	double lh = fmin(a.width * 0.45, 180.0 * pt), lv = fmin(a.height * 0.45, 180.0 * pt);
	double th = g * 1.1;
	double top = clampf_d((a.y - ly) / fmax(hypot(cx - lx, a.y - ly), 1.0), 0.0, 1.0);
	double left = clampf_d((a.x - lx) / fmax(hypot(a.x - lx, cy - ly), 1.0), 0.0, 1.0);
	double right = clampf_d((lx - ar) / fmax(hypot(lx - ar, cy - ly), 1.0), 0.0, 1.0);
	double gx = a.width > lh ? clampf_d(lx, a.x + lh * 0.5, ar - lh * 0.5) : cx;
	double gy = a.height > lv ? clampf_d(ly, a.y + lv * 0.5, ad - lv * 0.5) : cy;
	const double glint[4][5] = {
		{ gx, a.y - g * 0.62, lh, th, top },
		{ ar + g * 0.62, gy, th, lv, 0.15 + 0.85 * right },
		{ gx, ad + g * 0.62, lh * 0.6, th * 0.8, 0.30 * top },
		{ a.x - g * 0.62, gy, th, lv, 0.15 + 0.85 * left },
	};
	for (int i = 0; i < 4; i++) {
		struct wlr_scene_buffer *n = server->snip_glint[i];
		int w = (int)lround(glint[i][2]), h = (int)lround(glint[i][3]);
		struct wlr_box at = { (int)lround(glint[i][0] - w * 0.5), (int)lround(glint[i][1] - h * 0.5),
			w, h };
		bool on = w > 1 && h > 1 && snip_inside(at, in);
		wlr_scene_node_set_enabled(&n->node, on);
		if (on) {
			wlr_scene_buffer_set_dest_size(n, w, h);
			wlr_scene_node_set_position(&n->node, (int)lround(glint[i][0] - w * 0.5),
				(int)lround(glint[i][1] - h * 0.5));
		}
		server->snip_glint_a[i] = glint[i][4];
	}
}

/* The viewfinder's edge on one screen: its picture bent as thick glass
 * bends what is under its edge (desk_draw_finder_rim), as four strips
 * round it -- top, bottom, left, right -- deep enough for the rounded
 * corners. Worked out on a thread of its own (it is the most of a big
 * screen's moment, and is not wanted till the camera is near the eye),
 * the still held for it meanwhile; laid when it is back, if the camera is
 * still up. Not on a screen turned round. */
struct snip_rim_job {
	int fd;
	unsigned gen;
	int index;
	struct desk_buf *src;   /* the still, held */
	struct wlr_box part[4];
	cairo_surface_t *strip[4];
	double band, round;
};

static void *snip_rim_run(void *data)
{
	struct snip_rim_job *job = data;
	struct desk_buf *src = job->src;
	const unsigned char *pix = cairo_image_surface_get_data(src->surface);
	int sstride = cairo_image_surface_get_stride(src->surface);
	for (int i = 0; i < 4; i++) {
		cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
			job->part[i].width, job->part[i].height);
		if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
			cairo_surface_flush(img);
			desk_draw_finder_rim(pix, src->base.width, src->base.height, sstride,
				cairo_image_surface_get_data(img), job->part[i].x, job->part[i].y,
				job->part[i].width, job->part[i].height, cairo_image_surface_get_stride(img),
				job->band, job->round);
			cairo_surface_mark_dirty(img);
		}
		job->strip[i] = img;
	}
	if (write(job->fd, &job, sizeof job) != (ssize_t)sizeof job) {
		/* (nobody to hand it back to: the desk going) */
	}
	return NULL;
}

static void snip_rim_make(struct desk_server *server, int index)
{
	struct snip_still *st = &server->snip_still[index];
	int w = st->buf->base.width, h = st->buf->base.height;
	double pt = desk_px_per_pt(st->box.width);
	double band = SNIP_RIM_PT * pt, round = SNIP_ROUND_PT * pt;
	int t = (int)ceil(fmax(band, round));
	if (st->output->transform != WL_OUTPUT_TRANSFORM_NORMAL || t * 2 >= h || t * 2 >= w
		|| server->snip_rim_fd[1] < 0) {
		return;
	}
	struct snip_rim_job *job = calloc(1, sizeof *job);
	if (!job) {
		return;
	}
	*job = (struct snip_rim_job){
		.fd = server->snip_rim_fd[1], .gen = server->snip_gen, .index = index,
		.src = st->buf, .band = band, .round = round,
		.part = { { 0, 0, w, t }, { 0, h - t, w, t }, { 0, t, t, h - 2 * t },
			{ w - t, t, t, h - 2 * t } },
	};
	cairo_surface_flush(st->buf->surface);
	wlr_buffer_lock(&st->buf->base);
	pthread_attr_t at;
	pthread_t th;
	bool started = false;
	if (pthread_attr_init(&at) == 0) {
		pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
		started = pthread_create(&th, &at, snip_rim_run, job) == 0;
		pthread_attr_destroy(&at);
	}
	if (!started) {
		snip_rim_run(job);
	}
}

static void snip_tint(struct desk_server *server, double k);
static void snip_finder_lay(struct desk_server *server);

/* A glass edge back from its thread. */
static int snip_rim_landed(int fd, uint32_t mask, void *data)
{
	(void)mask;
	struct desk_server *server = data;
	struct snip_rim_job *job;
	while (read(fd, &job, sizeof job) == (ssize_t)sizeof job) {
		struct snip_still *st = job->index < server->snip_still_n
			? &server->snip_still[job->index] : NULL;
		bool keep = server->snip_on && job->gen == server->snip_gen && st && st->buf == job->src;
		for (int i = 0; i < 4; i++) {
			cairo_surface_t *img = job->strip[i];
			if (keep && img && cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
				struct desk_buf *b = buf_create(job->part[i].width, job->part[i].height);
				cairo_set_operator(b->cr, CAIRO_OPERATOR_SOURCE);
				cairo_set_source_surface(b->cr, img, 0, 0);
				cairo_paint(b->cr);
				cairo_set_operator(b->cr, CAIRO_OPERATOR_OVER);
				cairo_surface_flush(b->surface);
				st->rim[i] = scene_new(server->snip_rims, &b->base);
				wlr_buffer_drop(&b->base);
				st->rim_part[i] = job->part[i];
				st->rim[i]->point_accepts_input = shadow_accepts;
			}
			if (img) {
				cairo_surface_destroy(img);
			}
		}
		wlr_buffer_unlock(&job->src->base);
		free(job);
		if (keep) {
			snip_finder_lay(server);
			snip_tint(server, server->snip_k);
			server_schedule_frames(server);
		}
	}
	return 0;
}

/* ---- the screens, still ----
 *
 * Print takes its picture of the screens as they were when it was pressed:
 * each screen's next frame is read back as it goes out (output_commit), as
 * a screen recorder's is -- the pointer off them for it, and nothing of
 * Print's own on them yet -- and laid where the screen is, under the rest
 * of what Print shows (snip_stills), till the camera is put down. The
 * windows are told nothing meanwhile: a menu, a tooltip, stays as it was.
 * What is taken is cut from them (snip_cut) and written out on a thread
 * of its own, for desk-screenshot to file (snip_save). A screen that
 * gives no picture in SNIP_FREEZE_MS is shown as it is, and an area that
 * is not all in the pictures is taken by grim, as before. */

static void snip_frozen(struct desk_server *server);

static void snip_still_take(struct desk_server *server, struct wlr_output *wo,
	struct wlr_buffer *buffer)
{
	if (!server->snip_freezing || server->snip_still_n >= SNIP_SCREENS) {
		return;
	}
	struct wlr_box box;
	wlr_output_layout_get_box(server->output_layout, wo, &box);
	struct wlr_texture *tex = wlr_box_empty(&box) ? NULL
		: wlr_texture_from_buffer(server->renderer, buffer);
	if (!tex) {
		return;
	}
	int w = (int)tex->width, h = (int)tex->height;
	struct desk_buf *buf = buf_create(w, h);
	cairo_surface_flush(buf->surface);
	unsigned char *data = cairo_image_surface_get_data(buf->surface);
	int stride = cairo_image_surface_get_stride(buf->surface);
	bool ok = wlr_texture_read_pixels(tex, &(struct wlr_texture_read_pixels_options){
		.data = data,
		.format = DRM_FORMAT_ARGB8888,
		.stride = (uint32_t)stride,
	});
	wlr_texture_destroy(tex);
	if (!ok) {
		wlr_log(WLR_ERROR, "print: the picture of %s could not be read", wo->name);
		wlr_buffer_drop(&buf->base);
		return;
	}
	/* nothing is behind a screen: opaque, whatever its frame's fourth
	 * byte was */
	for (int y = 0; y < h; y++) {
		uint32_t *row = (uint32_t *)(void *)(data + (size_t)y * (size_t)stride);
		for (int x = 0; x < w; x++) {
			row[x] |= 0xff000000u;
		}
	}
	cairo_surface_mark_dirty(buf->surface);
	struct snip_still *st = &server->snip_still[server->snip_still_n++];
	st->output = wo;
	st->box = box;
	st->buf = buf;
	wlr_buffer_lock(&buf->base);   /* ours, to cut from, as well as the scene's */
	st->node = scene_new(server->snip_stills, &buf->base);
	/* and its viewfinder: the dark over it, the picture in that -- as yet
	 * just where the screen is, and the dark not there */
	static const float none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	st->dark = wlr_scene_rect_create(server->snip_backs, box.width, box.height, none);
	wlr_scene_node_set_position(&st->dark->node, box.x, box.y);
	st->finder = scene_new(server->snip_finders, &buf->base);
	wlr_buffer_drop(&buf->base);
	st->view = snip_view_box(box);
	struct wlr_scene_buffer *both[2] = { st->node, st->finder };
	for (int i = 0; i < 2; i++) {
		wlr_scene_buffer_set_transform(both[i], wo->transform);
		wlr_scene_buffer_set_dest_size(both[i], box.width, box.height);
		wlr_scene_node_set_position(&both[i]->node, box.x, box.y);
		both[i]->point_accepts_input = shadow_accepts;
	}
}

/* A screen's frame has gone out with the picture asked of it in it. */
static void snip_still_landed(struct desk_server *server)
{
	if (server->snip_freezing && --server->snip_still_wanted <= 0) {
		snip_frozen(server);
	}
}

static int snip_freeze_cb(void *data)
{
	struct desk_server *server = data;
	if (server->snip_freezing) {
		wlr_log(WLR_INFO, "print: %d screens gave no picture in time",
			server->snip_still_wanted);
		snip_frozen(server);
	}
	return 0;
}

/* Ask every screen that is showing for its next frame, whole: drawn
 * though nothing on it has changed, and without the pointer. */
static void snip_freeze(struct desk_server *server)
{
	server->snip_freezing = true;
	server->snip_still_n = 0;
	server->snip_still_wanted = 0;
	server->snip_freeze_at = real_sec();
	wlr_cursor_unset_image(server->cursor);
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		struct wlr_scene_output *so = wlr_scene_get_scene_output(server->scene, o->wlr_output);
		if (!o->wlr_output->enabled || !so || o->off || o->dark
			|| server->snip_still_wanted >= SNIP_SCREENS) {
			continue;
		}
		o->snip_want = true;
		server->snip_still_wanted++;
		wlr_damage_ring_add_whole(&so->damage_ring);
		wlr_output_schedule_frame(o->wlr_output);
	}
	if (server->snip_still_wanted == 0) {
		snip_frozen(server);
		return;
	}
	if (!server->snip_freeze_timer) {
		server->snip_freeze_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), snip_freeze_cb, server);
	}
	if (server->snip_freeze_timer) {
		wl_event_source_timer_update(server->snip_freeze_timer, SNIP_FREEZE_MS);
	}
}

static void snip_stills_drop(struct desk_server *server)
{
	for (int i = 0; i < server->snip_still_n; i++) {
		struct snip_still *st = &server->snip_still[i];
		if (st->node) {
			wlr_scene_node_destroy(&st->node->node);
		}
		if (st->dark) {
			wlr_scene_node_destroy(&st->dark->node);
		}
		if (st->finder) {
			wlr_scene_node_destroy(&st->finder->node);
		}
		for (int k = 0; k < 4; k++) {
			if (st->rim[k]) {
				wlr_scene_node_destroy(&st->rim[k]->node);
			}
		}
		if (st->buf) {
			wlr_buffer_unlock(&st->buf->base);
		}
		*st = (struct snip_still){ 0 };
	}
	server->snip_still_n = 0;
	server->snip_still_wanted = 0;
	server->snip_freezing = false;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		o->snip_want = false;
	}
	if (server->snip_freeze_timer) {
		wl_event_source_timer_update(server->snip_freeze_timer, 0);
	}
}

/* The area as it was when Print was pressed, cut from the screens'
 * pictures; NULL where they have not all of it. */
static cairo_surface_t *snip_cut(struct desk_server *server, struct wlr_box a)
{
	long need = (long)a.width * a.height, got = 0;
	for (int i = 0; i < server->snip_still_n; i++) {
		struct snip_still *st = &server->snip_still[i];
		struct wlr_box in;
		if (st->output->transform != WL_OUTPUT_TRANSFORM_NORMAL) {
			return NULL;   /* a screen turned round: grim knows which way */
		}
		if (wlr_box_intersection(&in, &st->box, &a)) {
			got += (long)in.width * in.height;
		}
	}
	if (need <= 0 || got < need) {
		return NULL;
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_RGB24, a.width, a.height);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(img);
		return NULL;
	}
	cairo_t *cr = cairo_create(img);
	for (int i = 0; i < server->snip_still_n; i++) {
		struct snip_still *st = &server->snip_still[i];
		cairo_save(cr);
		cairo_translate(cr, st->box.x - a.x, st->box.y - a.y);
		cairo_scale(cr, (double)st->box.width / st->buf->base.width,
			(double)st->box.height / st->buf->base.height);
		cairo_set_source_surface(cr, st->buf->surface, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	}
	cairo_destroy(cr);
	return img;
}

/* A picture being written out: on a thread of its own (a big one is a
 * second's work), then said back over snip_save_fd. */
struct snip_save {
	int fd;
	cairo_surface_t *img;
	char path[PATH_MAX];
	bool ok;
};

static void *snip_save_run(void *data)
{
	struct snip_save *job = data;
	job->ok = cairo_surface_write_to_png(job->img, job->path) == CAIRO_STATUS_SUCCESS;
	cairo_surface_destroy(job->img);
	job->img = NULL;
	if (write(job->fd, &job, sizeof job) != (ssize_t)sizeof job) {
		free(job);   /* nobody to hand it to */
	}
	return NULL;
}

/* Written: to desk-screenshot, to be filed, copied and posted. */
static int snip_saved(int fd, uint32_t mask, void *data)
{
	(void)mask;
	(void)data;
	struct snip_save *job;
	while (read(fd, &job, sizeof job) == (ssize_t)sizeof job) {
		if (job->ok) {
			wlr_log(WLR_INFO, "screenshot: desk-screenshot --file %s", job->path);
			char *const argv[] = { (char *)"desk-screenshot", (char *)"--file", job->path,
				NULL };
			spawn_argv(argv);
		} else {
			wlr_log(WLR_ERROR, "print: %s could not be written", job->path);
			unlink(job->path);
		}
		free(job);
	}
	return 0;
}

static void snip_save(struct desk_server *server, cairo_surface_t *img)
{
	struct snip_save *job = calloc(1, sizeof *job);
	if (!job || server->snip_save_fd[1] < 0) {
		free(job);
		cairo_surface_destroy(img);
		return;
	}
	const char *dir = getenv("XDG_RUNTIME_DIR");
	snprintf(job->path, sizeof job->path, "%s/desk-print-%d-%u.png",
		dir && *dir ? dir : "/tmp", (int)getpid(), ++server->snip_saves);
	job->fd = server->snip_save_fd[1];
	job->img = img;
	pthread_attr_t at;
	pthread_t th;
	bool started = false;
	if (pthread_attr_init(&at) == 0) {
		pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
		started = pthread_create(&th, &at, snip_save_run, job) == 0;
		pthread_attr_destroy(&at);
	}
	if (!started) {
		snip_save_run(job);
	}
}

/* points on the screen the area begins on */
static double snip_pt(struct desk_server *server, struct wlr_box a)
{
	struct wlr_output *out = screen_at(server, a.x, a.y);
	return desk_px_per_pt(out ? out->width : server->desk_w);
}

/* The viewfinders, as far as the camera has come up. On the screen it is
 * held up to, the picture as the eyepiece shows it: in the eyepiece's
 * glass, as big as the camera is near the eye (snip_hero_lay), with the
 * dark in the eyepiece round it (snip_hole), and the screen round the
 * camera going dark as it nears the eye (snip_dim). On the others, the
 * picture drawing in from the screen's edges and the dark coming over it
 * as the camera gets there (k). The glass edges go with the pictures. */
static void snip_finder_lay(struct desk_server *server)
{
	double k = server->snip_k, e = k * k * (3.0 - 2.0 * k);
	for (int i = 0; i < server->snip_still_n; i++) {
		struct snip_still *st = &server->snip_still[i];
		if (!st->finder || !st->dark) {
			continue;
		}
		struct wlr_box b = st->box, v = st->view;
		double fx, fy, fw, fh, dark;
		if (server->snip_eye_r > 0.0 && wlr_box_equal(&b, &server->snip_screen)) {
			fw = v.width * server->snip_eye_r;
			fh = v.height * server->snip_eye_r;
			fx = server->snip_eye_x - fw * 0.5;
			fy = server->snip_eye_y - fh * 0.5;
			dark = server->snip_dim;
		} else {
			fx = b.x + (v.x - b.x) * e;
			fy = b.y + (v.y - b.y) * e;
			fw = b.width + (v.width - b.width) * e;
			fh = b.height + (v.height - b.height) * e;
			dark = k;
		}
		int x = (int)lround(fx), y = (int)lround(fy);
		int w = (int)lround(fx + fw) - x, h = (int)lround(fy + fh) - y;
		wlr_scene_node_set_enabled(&st->finder->node, w > 0 && h > 0);
		if (w > 0 && h > 0) {
			wlr_scene_buffer_set_dest_size(st->finder, w, h);
			wlr_scene_node_set_position(&st->finder->node, x, y);
		}
		const float c[4] = { 0.012f * (float)dark, 0.010f * (float)dark, 0.008f * (float)dark,
			(float)dark };
		wlr_scene_rect_set_color(st->dark, c);
		wlr_scene_node_set_enabled(&st->dark->node, dark > 0.002);
		double sx = fw / st->buf->base.width, sy = fh / st->buf->base.height;
		for (int r = 0; r < 4; r++) {
			struct wlr_scene_buffer *n = st->rim[r];
			if (!n) {
				continue;
			}
			struct wlr_box pr = st->rim_part[r];
			int rx = (int)lround(fx + pr.x * sx), ry = (int)lround(fy + pr.y * sy);
			int rw = (int)lround(fx + (pr.x + pr.width) * sx) - rx;
			int rh = (int)lround(fy + (pr.y + pr.height) * sy) - ry;
			wlr_scene_node_set_enabled(&n->node, rw > 0 && rh > 0);
			if (rw > 0 && rh > 0) {
				wlr_scene_buffer_set_dest_size(n, rw, rh);
				wlr_scene_node_set_position(&n->node, rx, ry);
			}
		}
	}
	/* the dark in the eyepiece, behind the picture in it: its glass, and a
	 * little into the rubber round it */
	bool hole = server->snip_eye_on && server->snip_eye_s > 0.0;
	if (hole) {
		double ex, ey, ew, eh, m = 4.0;
		desk_finder_camera_eyepiece(&ex, &ey, &ew, &eh);
		double es = server->snip_eye_s;
		int x = (int)lround(server->snip_eye_x - (ew * 0.5 + m) * es);
		int y = (int)lround(server->snip_eye_y - (eh * 0.5 + m) * es);
		snip_rect(server->snip_hole, x, y, (int)lround((ew + 2.0 * m) * es),
			(int)lround((eh + 2.0 * m) * es));
	} else {
		snip_rect(server->snip_hole, 0, 0, 0, 0);
	}
}

/* How far up to the eye the camera is (k): the viewfinder's glass edge,
 * the circle and the display, from nothing (0) to all there (1); and with
 * it, how much the light has gathered on the picture (snip_shade): the
 * shade round it, and the glass round it. */
static void snip_tint(struct desk_server *server, double k)
{
	float d[4];
	double lit = k * server->snip_shade, sh = SNIP_SHADE * lit;
	const float warm[3] = { 0.070f, 0.056f, 0.040f };
	for (int i = 0; i < 4; i++) {
		d[i] = i < 3 ? warm[i] * (float)sh : (float)sh;
	}
	for (int i = 0; i < 4; i++) {
		wlr_scene_rect_set_color(server->snip_dark[i], d);
		wlr_scene_buffer_set_opacity(server->snip_glass[i], (float)lit);
		wlr_scene_buffer_set_opacity(server->snip_glint[i],
			(float)(lit * server->snip_glint_a[i]));
	}
	for (int i = 0; i < 8; i++) {
		wlr_scene_buffer_set_opacity(server->snip_bevel[i], (float)lit);
	}
	for (int i = 0; i < 8; i++) {
		wlr_scene_buffer_set_opacity(server->snip_pen[i], (float)sh);
	}
	for (int i = 0; i < server->snip_still_n; i++) {
		for (int r = 0; r < 4; r++) {
			if (server->snip_still[i].rim[r]) {
				wlr_scene_buffer_set_opacity(server->snip_still[i].rim[r], (float)k);
			}
		}
	}
	if (server->snip_info) {
		wlr_scene_buffer_set_opacity(server->snip_info, (float)k);
	}
	if (server->snip_ring) {
		wlr_scene_buffer_set_opacity(server->snip_ring, (float)k);
	}
}

/* The display, in the dark under the picture of the screen the camera is
 * held up to; and the circle in the middle of what is lit (where there is
 * room for it to be no more than a mark there). Drawn again only when
 * what it says changes, and once a frame at most (snip_tick): a pointer
 * can move a thousand times a second. */
static void snip_display(struct desk_server *server)
{
	struct wlr_box sc = server->snip_screen;
	struct snip_still *st = snip_still_at(server, sc.x + sc.width * 0.5, sc.y + sc.height * 0.5);
	struct wlr_box v = st && st->finder ? st->view : snip_view_box(sc);
	double pt = desk_px_per_pt(sc.width);
	struct wlr_box a = snip_area(server);
	struct desk_finder_line l = {
		.mode = server->snip_mode,
		.ready = server->snip_lit && a.width >= 3 && a.height >= 3,
		.w = a.width,
		.h = a.height,
		.timer = server->snip_timer_set == 1 ? 2 : server->snip_timer_set == 2 ? 10 : 0,
		.frame = server->snip_cam.count,
		.tape = server->snip_tape,
		.rec = server->rec != NULL,
	};
	if (!l.ready) {
		l.w = l.h = 0;
	}
	const struct desk_finder_line *was = &server->snip_line;
	bool same = l.mode == was->mode && l.ready == was->ready && l.w == was->w && l.h == was->h
		&& l.timer == was->timer && l.frame == was->frame && l.tape == was->tape
		&& l.rec == was->rec && l.secs == was->secs;
	if (!snip_has_pic(server->snip_info) || !same || v.width != server->snip_line_w
		|| pt != server->snip_line_pt) {
		int bh = desk_finder_line_height(pt);
		struct desk_buf *buf = buf_create(v.width, bh);
		desk_draw_finder_line(buf->cr, v.width, &l, pt);
		scene_put(&server->snip_info, server->layer_snip, buf, 0, 0, NULL);
		server->snip_info->point_accepts_input = shadow_accepts;
		wlr_scene_buffer_set_opacity(server->snip_info, (float)server->snip_k);
		server->snip_line = l;
		server->snip_line_w = v.width;
		server->snip_line_pt = pt;
		server->snip_info_bh = bh;
	}
	/* in the middle of the dark under the picture */
	int foot = sc.y + sc.height - (v.y + v.height);
	wlr_scene_node_set_position(&server->snip_info->node, v.x,
		v.y + v.height + (foot - server->snip_info_bh) / 2);
	wlr_scene_node_set_enabled(&server->snip_info->node, true);
	int rs = desk_finder_ring_size(pt);
	if (!snip_has_pic(server->snip_ring) || pt != server->snip_ring_pt) {
		struct desk_buf *buf = buf_create(rs, rs);
		desk_draw_finder_ring(buf->cr, pt);
		scene_put(&server->snip_ring, server->layer_snip, buf, 0, 0, NULL);
		server->snip_ring->point_accepts_input = shadow_accepts;
		wlr_scene_buffer_set_opacity(server->snip_ring, (float)server->snip_k);
		server->snip_ring_pt = pt;
	}
	struct wlr_box av = snip_area_view(server);
	bool room = server->snip_lit && av.width >= rs * 3 && av.height >= rs * 3;
	wlr_scene_node_set_enabled(&server->snip_ring->node, room);
	if (room) {
		wlr_scene_node_set_position(&server->snip_ring->node,
			av.x + av.width / 2 - rs / 2, av.y + av.height / 2 - rs / 2);
	}
}

static void snip_paint(struct desk_server *server)
{
	server->snip_hand_away = false;
	/* the display and the circle, on the next frame */
	server->snip_dirty = true;
	if (!server->snip_lit) {
		/* (the shade and the glass stay where they were, the light
		 * spreading again over all of it: snip_tick) */
		server_schedule_frames(server);
		return;
	}
	struct wlr_box pa = snip_area(server), a = snip_area_view(server);
	double pt = snip_pt(server, pa);
	/* the camera in the hand out of the way of an area drawn out over it */
	if (server->snip_held && snip_has_pic(server->snip_hand)) {
		struct wlr_box hand = snip_hand_box(server, 12.0 * pt), in;
		server->snip_hand_away = wlr_box_intersection(&in, &hand, &a);
	}
	/* the light on it, and the glass round it -- all outside it, so that
	 * none of it is in the picture */
	snip_shade_lay(server, a, pt);
	snip_glass_lay(server, snip_area_still(server, pa), a, pt);
	snip_tint(server, server->snip_k);
	server_schedule_frames(server);
}

static bool snip_cam_same(const struct desk_finder_camera *a, const struct desk_finder_camera *b)
{
	return a->dial == b->dial && a->timer == b->timer && a->power == b->power
		&& a->press == b->press && a->wind == b->wind && a->lamp == b->lamp
		&& a->count == b->count && a->tape == b->tape && a->see_through == b->see_through;
}

/* The camera that comes up to the eye, drawn for the screen it is held up
 * to as it is set -- once, and again only when that changes. */
static void snip_hero_make(struct desk_server *server)
{
	double s = SNIP_HERO * server->snip_screen.width / DESK_FINDER_CAMERA_W;
	struct desk_finder_camera c = server->snip_cam;
	c.see_through = true;
	if (snip_has_pic(server->snip_hero) && server->snip_hero_s == s
		&& snip_cam_same(&c, &server->snip_hero_drawn)) {
		return;
	}
	struct desk_buf *b = buf_create((int)ceil(DESK_FINDER_CAMERA_W * s),
		(int)ceil(DESK_FINDER_CAMERA_H * s));
	desk_draw_finder_camera(b->cr, s, &c);
	scene_put(&server->snip_hero, server->layer_snip, b, 0, 0, NULL);
	server->snip_hero->point_accepts_input = shadow_accepts;
	wlr_scene_node_set_enabled(&server->snip_hero->node, false);
	server->snip_hero_s = s;
	server->snip_hero_drawn = c;
}

/* The camera on its way: rise, from under the screen's foot (0) to held up
 * in front of it (1); zoom, from held there (0) to at the eye (1), its
 * eyepiece -- clear, the viewfinder seen through it -- over the picture
 * the viewfinder shows (snip_finder_lay); and how much of it is seen.
 * Where its eyepiece is, as it is, for the viewfinder to be laid in. */
static void snip_hero_lay(struct desk_server *server, double rise, double zoom, double seen)
{
	struct wlr_scene_buffer *n = server->snip_hero;
	double s = server->snip_hero_s;
	server->snip_eye_on = false;
	if (!snip_has_pic(n) || s <= 0.0 || gfx_flat()) {
		/* (in 2D, not drawn: the viewfinder is there at once) */
		server->snip_eye_r = 0.0;
		if (n) {
			wlr_scene_node_set_enabled(&n->node, false);
		}
		return;
	}
	struct wlr_box sc = server->snip_screen;
	struct snip_still *st = snip_still_at(server, sc.x + sc.width * 0.5, sc.y + sc.height * 0.5);
	struct wlr_box v = st && st->finder ? st->view : sc;
	double ex, ey, ew, eh;
	desk_finder_camera_eyepiece(&ex, &ey, &ew, &eh);
	double ecx = ex + ew * 0.5, ecy = ey + eh * 0.5;
	double most = fmax(v.width / (ew * s), v.height / (eh * s)) * 1.06;
	double g = (0.9 + 0.1 * rise) * pow(most, zoom);
	/* where the eyepiece is: under the foot of the screen, the camera's
	 * top just out of sight; held up, a little under the middle; at the
	 * eye, over the middle of the viewfinder's picture */
	double below = sc.y + sc.height + ecy * s * g, held = sc.y + sc.height * 0.56;
	double py = below + (held - below) * rise + (v.y + v.height * 0.5 - held) * zoom;
	double px = sc.x + sc.width * 0.5 + (v.x + v.width * 0.5 - (sc.x + sc.width * 0.5)) * zoom;
	server->snip_eye_x = px;
	server->snip_eye_y = py;
	server->snip_eye_s = s * g;
	server->snip_eye_r = g / most;
	bool on = seen > 0.004;
	server->snip_eye_on = on;
	wlr_scene_node_set_enabled(&n->node, on);
	if (!on) {
		return;
	}
	wlr_scene_buffer_set_dest_size(n, (int)lround(DESK_FINDER_CAMERA_W * s * g),
		(int)lround(DESK_FINDER_CAMERA_H * s * g));
	wlr_scene_node_set_position(&n->node, (int)lround(px - ecx * s * g),
		(int)lround(py - ecy * s * g));
	wlr_scene_buffer_set_opacity(n, (float)seen);
	wlr_scene_node_raise_to_top(&n->node);
}

static double snip_smooth(double e0, double e1, double x)
{
	double u = clampf_d((x - e0) / (e1 - e0), 0.0, 1.0);
	return u * u * (3.0 - 2.0 * u);
}

/* The flash going off as a picture is taken: a (on the screens) lit white
 * all at once, and the rest of the screens a little, as a room is; dying
 * away over SNIP_FLASH_S (snip_light_tick), over everything Print shows.
 * The tube's pop with the shutter, and it charging again after. */
static void snip_light_fire(struct desk_server *server, struct wlr_box a)
{
	struct wlr_box b = snip_bounds(server);
	snip_rect(server->snip_glow, b.x, b.y, b.width, b.height);
	snip_rect(server->snip_light, a.x, a.y, a.width, a.height);
	wlr_scene_node_raise_to_top(&server->snip_glow->node);
	wlr_scene_node_raise_to_top(&server->snip_light->node);
	server->snip_light_at = monotonic_sec();
	snd_at(server, DESK_SND_FLASH, a.x + a.width * 0.5, 0.8);
	server_schedule_frames(server);
}

/* And as it dies away: in at once, out slowly, as a flash is seen. */
static void snip_light_tick(struct desk_server *server)
{
	if (server->snip_light_at <= 0.0) {
		return;
	}
	double u = (monotonic_sec() - server->snip_light_at) / SNIP_FLASH_S;
	if (u >= 1.0) {
		server->snip_light_at = 0.0;
		wlr_scene_node_set_enabled(&server->snip_light->node, false);
		wlr_scene_node_set_enabled(&server->snip_glow->node, false);
		return;
	}
	double k = u < 0.04 ? u / 0.04 : pow(1.0 - (u - 0.04) / 0.96, 2.4);
	float l = (float)(0.92 * k), g = (float)(0.30 * k);
	const float lc[4] = { l, l, l * 0.97f, l }, gc[4] = { g, g * 0.98f, g * 0.94f, g };
	wlr_scene_rect_set_color(server->snip_light, lc);
	wlr_scene_rect_set_color(server->snip_glow, gc);
	server_schedule_frames(server);
}

/* The camera's parts where they are now -- the dial turning to where it is
 * set, the slide sliding, the power lever going over, the shutter button
 * down and up again; whether any is still on its way. */
static bool snip_cam_now(struct desk_server *server, double now)
{
	struct desk_finder_camera *c = &server->snip_cam;
	double u = clampf_d((now - server->snip_dial_at) / SNIP_TURN_S, 0.0, 1.0);
	c->dial = server->snip_dial_from + (server->snip_mode - server->snip_dial_from)
		* snip_smooth(0.0, 1.0, u);
	bool moving = u < 1.0;
	u = clampf_d((now - server->snip_slide_at) / SNIP_TURN_S, 0.0, 1.0);
	c->timer = server->snip_slide_from + (server->snip_timer_set - server->snip_slide_from)
		* snip_smooth(0.0, 1.0, u);
	moving = moving || u < 1.0;
	if (server->snip_power_at > 0.0) {
		u = clampf_d((now - server->snip_power_at) / SNIP_TURN_S, 0.0, 1.0);
		c->power = 1.0 - snip_smooth(0.0, 1.0, u);
		moving = moving || u < 1.0;
	} else {
		c->power = 1.0;
	}
	c->press = 0.0;
	if (server->snip_press_at > 0.0) {
		u = clampf_d((now - server->snip_press_at) / 0.22, 0.0, 1.0);
		c->press = u < 0.3 ? u / 0.3 : 1.0 - (u - 0.3) / 0.7;
		moving = moving || u < 1.0;
	}
	double tape = server->snip_tape ? 1.0 : 0.0;
	c->tape = tape;
	if (server->snip_tape_at > 0.0) {
		u = clampf_d((now - server->snip_tape_at) / SNIP_TAPE_S, 0.0, 1.0);
		c->tape = server->snip_tape_from + (tape - server->snip_tape_from) * u;
		moving = moving || u < 1.0;
	}
	return moving;
}

/* The tape put in the camera (in), or taken out: on its way from wherever
 * it has got to, from `at` (the desk's time) -- heard going in as it
 * reaches the slot, or the eject's at once. */
static void snip_tape_set(struct desk_server *server, bool in, double at)
{
	double now = monotonic_sec();
	snip_cam_now(server, now);
	double from = server->snip_cam.tape;
	server->snip_tape_from = from;
	server->snip_tape_at = at;
	server->snip_tape = in;
	server->snip_dirty = true;   /* (the display says so) */
	double x = server->snip_screen.x + server->snip_screen.width * 0.5
		- DESK_FINDER_CAMERA_W * 0.22 * (server->snip_hand_s > 0.0 ? server->snip_hand_s : 0.8);
	if (in) {
		double reach = from < DESK_FINDER_TAPE_REACH
			? (DESK_FINDER_TAPE_REACH - from) / (1.0 - from) * SNIP_TAPE_S : 0.0;
		snd_in(server, DESK_SND_TAPE_IN, x, 0.8, 0.0, at - now + reach);
	} else {
		snd_in(server, DESK_SND_TAPE_OUT, x, 0.8, 0.0, at - now);
	}
	server_schedule_frames(server);
}

/* The camera in the hand: drawn as it is set, for the screen it is held up
 * to, again only when that changes; and laid at the screen's foot, as far
 * up as it is. */
static void snip_hand_lay(struct desk_server *server)
{
	struct wlr_box sc = server->snip_screen;
	double s = SNIP_HAND_PT * desk_px_per_pt(sc.width) / DESK_FINDER_CAMERA_W;
	struct desk_finder_camera c = server->snip_cam;
	c.see_through = false;
	if (!snip_has_pic(server->snip_hand) || server->snip_hand_s != s
		|| !snip_cam_same(&c, &server->snip_hand_drawn)) {
		struct desk_buf *b = buf_create((int)ceil(DESK_FINDER_CAMERA_W * s),
			(int)ceil(DESK_FINDER_CAMERA_H * s));
		desk_draw_finder_camera(b->cr, s, &c);
		scene_put(&server->snip_hand, server->layer_snip, b, 0, 0, NULL);
		server->snip_hand_s = s;
		server->snip_hand_drawn = c;
	}
	double k = server->snip_hand_k;
	bool on = k > 0.004;
	wlr_scene_node_set_enabled(&server->snip_hand->node, on);
	if (on) {
		double up = SNIP_HAND_SHOW * s, e = 1.0 - pow(1.0 - k, 3.0);
		wlr_scene_node_set_position(&server->snip_hand->node,
			(int)lround(sc.x + sc.width * 0.5 - DESK_FINDER_CAMERA_W * s * 0.5),
			(int)lround(sc.y + sc.height - up * e));
	}
}

/* Where the camera in the hand is, up, on the screens -- and m round it. */
static struct wlr_box snip_hand_box(struct desk_server *server, double m)
{
	double hs = server->snip_hand_s;
	return (struct wlr_box){
		(int)lround(server->snip_screen.x + server->snip_screen.width * 0.5
			- DESK_FINDER_CAMERA_W * hs * 0.5 - m),
		(int)lround(server->snip_screen.y + server->snip_screen.height - SNIP_HAND_SHOW * hs - m),
		(int)lround(DESK_FINDER_CAMERA_W * hs + 2.0 * m),
		(int)lround(SNIP_HAND_SHOW * hs + 2.0 * m),
	};
}

/* What of the camera in the hand is at (x, y) on the screens. */
static enum desk_finder_part snip_hand_part(struct desk_server *server, double x, double y)
{
	struct wlr_scene_buffer *h = server->snip_hand;
	if (!h->node.enabled || server->snip_hand_k < 0.9 || server->snip_hand_s <= 0.0) {
		return DESK_FINDER_NOTHING;
	}
	return desk_finder_camera_at((x - h->node.x) / server->snip_hand_s,
		(y - h->node.y) / server->snip_hand_s);
}

static void snip_done(struct desk_server *server);

/* On a frame: the camera coming up to the eye, or going down; the light
 * gathering on the picture (or spreading again); and the display. */
static void snip_tick(struct desk_server *server)
{
	snip_light_tick(server);
	/* (nor while the self-timer's picture is taken: no camera up, just
	 * the flash over it) */
	if (!server->snip_on || server->snip_freezing || server->snip_flash) {
		return;
	}
	double now = monotonic_sec();
	double dt = clampf_d(now - server->snip_ticked, 0.0, 0.05);
	server->snip_ticked = now;
	bool more = false;
	if (server->snip_down_at > 0.0) {
		/* down: from the eye, back out to held in front, and down out of
		 * sight -- the viewfinder gone as it leaves the eye */
		double d = clampf_d((now - server->snip_down_at) / SNIP_DOWN_S, 0.0, 1.0);
		double back = clampf_d(d / 0.55, 0.0, 1.0), fall = clampf_d((d - 0.55) / 0.45, 0.0, 1.0);
		double zoom = pow(1.0 - back, 3.0);
		snip_hero_lay(server, 1.0 - fall * fall * fall, zoom, snip_smooth(0.0, 0.25, d));
		server->snip_dim = snip_smooth(0.05, 0.70, zoom);
		server->snip_k = 1.0 - snip_smooth(0.0, 0.35, d);
		more = true;
		if (d >= 1.0) {
			snip_done(server);
			return;
		}
	} else if (server->snip_k < 1.0 || (server->snip_hero && server->snip_hero->node.enabled)) {
		double u = clampf_d((now - server->snip_at) / SNIP_UP_S, 0.0, 1.0);
		double rise = clampf_d(u / SNIP_RISE, 0.0, 1.0);
		double zoom = clampf_d((u - SNIP_RISE) / (1.0 - SNIP_RISE), 0.0, 1.0);
		zoom = pow(zoom, 1.6);   /* faster the nearer the eye */
		/* the camera seen till its eyecup is all round the eye, the
		 * screen round it gone dark well before that */
		snip_hero_lay(server, 1.0 - pow(1.0 - rise, 3.0), zoom,
			1.0 - snip_smooth(0.80, 1.0, zoom));
		server->snip_dim = snip_smooth(0.05, 0.70, zoom);
		server->snip_k = snip_smooth(0.45, 1.0, zoom);
		more = true;
	}
	double want = server->snip_lit ? 1.0 : 0.0;
	if (server->snip_shade != want) {
		server->snip_shade += (want - server->snip_shade)
			* clampf_d(dt * 3.0 / SNIP_SHADE_S, 0.0, 1.0);
		if (fabs(want - server->snip_shade) < 0.004) {
			server->snip_shade = want;
		}
		more = true;
	}
	/* the camera in the hand: up once the other is at the eye, out of the
	 * way of an area drawn out over it, down as it all goes down -- and
	 * its parts on their way */
	double up = clampf_d((now - server->snip_at) / SNIP_UP_S, 0.0, 1.0);
	double hand = server->snip_down_at <= 0.0 && up >= 0.85 && !server->snip_hand_away
		? 1.0 : 0.0;
	if (server->snip_hand_k != hand) {
		double step = dt * (hand > server->snip_hand_k ? 4.5 : 9.0);
		server->snip_hand_k = hand > server->snip_hand_k
			? fmin(hand, server->snip_hand_k + step) : fmax(hand, server->snip_hand_k - step);
		more = true;
	}
	if (snip_cam_now(server, now)) {
		more = true;
	}
	snip_hand_lay(server);
	if (more) {
		snip_finder_lay(server);
		snip_tint(server, server->snip_k);
		server_schedule_frames(server);
	}
	if (server->snip_dirty && !server->snip_shot) {
		server->snip_dirty = false;
		snip_display(server);
	}
}

static void snip_begin(struct desk_server *server, bool tape)
{
	if (server->snip_counting) {
		snip_count_stop(server);
		return;
	}
	if (server->snip_on || locked(server)) {
		return;
	}
	/* a film being made: the camera is not put up, the film stops */
	if (server->rec) {
		rec_end(server);
		return;
	}
	/* (Shift+Print: a tape, put in once the camera is in the hand) */
	server->snip_load = tape && !server->snip_tape;
	server->snip_on = true;
	server->snip_held = false;
	server->snip_shot = false;
	server->snip_lit = false;
	server->snip_k = 0.0;
	server->snip_gen++;
	snip_freeze(server);
}

/* The screens' pictures in (or as many as came): the camera up. */
static void snip_frozen(struct desk_server *server)
{
	if (!server->snip_on || !server->snip_freezing) {
		return;
	}
	server->snip_freezing = false;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		o->snip_want = false;
	}
	if (server->snip_freeze_timer) {
		wl_event_source_timer_update(server->snip_freeze_timer, 0);
	}
	wlr_log(WLR_INFO, "print: the screens still (%d) in %.1f ms", server->snip_still_n,
		(real_sec() - server->snip_freeze_at) * 1000.0);
	if (server->snip_late) {
		/* the self-timer gone off: taken, heard, and the flash over it */
		server->snip_late = false;
		struct wlr_box a = server->snip_take;
		cairo_surface_t *cut = snip_cut(server, a);
		if (cut) {
			snip_save(server, cut);
		}
		server->snip_grim = cut ? (struct wlr_box){ 0 } : a;
		server->snip_cam.count = server->snip_cam.count % 99 + 1;
		snd_at(server, DESK_SND_SHUTTER, a.x + a.width * 0.5, 1.0);
		wlr_scene_node_set_enabled(&server->layer_snip->node, true);
		snip_light_fire(server, a);
		server->snip_shot = server->snip_flash = true;
		cursor_image(server, "default");
		snip_shut_after(server, real_secs(SNIP_FLASH_S) * 1000.0);
		server_schedule_frames(server);
		return;
	}
	for (int i = 0; i < server->snip_still_n; i++) {
		snip_rim_make(server, i);
	}
	double r0;
	struct wlr_output *out = screen_at(server, server->cursor->x, server->cursor->y);
	server->snip_screen = snip_bounds(server);
	if (out) {
		wlr_output_layout_get_box(server->output_layout, out, &server->snip_screen);
	}
	/* (in 2D, where everything gets where it is going at once, the camera
	 * is at the eye at once too: not drawn, nor shown for a frame) */
	if (!gfx_flat()) {
		r0 = real_sec();
		snip_hero_make(server);
		wlr_log(WLR_INFO, "print: the camera in %.1f ms", (real_sec() - r0) * 1000.0);
	}
	server->snip_at = server->snip_ticked = monotonic_sec();
	server->snip_down_at = 0.0;
	server->snip_k = 0.0;
	server->snip_dim = 0.0;
	server->snip_shade = 0.0;
	server->snip_hand_k = 0.0;
	server->snip_hand_away = false;
	server->snip_drag = -1;
	server->snip_target = (struct wlr_box){ 0 };
	server->snip_power_at = server->snip_press_at = 0.0;
	server->snip_dial_from = server->snip_mode;
	server->snip_slide_from = server->snip_timer_set;
	server->snip_dial_at = server->snip_slide_at = 0.0;
	server->snip_tape_at = 0.0;
	server->snip_cursor = "crosshair";
	snip_cam_now(server, server->snip_at);
	if (server->snip_load) {
		server->snip_load = false;
		snip_tape_set(server, true, server->snip_at + SNIP_UP_S);
	}
	snip_hero_lay(server, 0.0, 0.0, 1.0);
	snip_finder_lay(server);
	snip_tint(server, 0.0);
	snd_at(server, DESK_SND_FELT_PICK, server->snip_screen.x + server->snip_screen.width * 0.5,
		0.7);
	/* now, not before: a game's hold on the pointer let go, and the
	 * windows told the pointer has gone */
	desk_constraint_refresh(server);
	wlr_seat_pointer_clear_focus(server->seat);
	wlr_scene_node_set_enabled(&server->layer_snip->node, true);
	cursor_image(server, "crosshair");
	snip_paint(server);
}

/* The camera down: Print's all gone, the screens as they are again --
 * and grim given what the stills had not all of. */
static void snip_done(struct desk_server *server)
{
	struct wlr_box a = server->snip_grim;
	snip_stills_drop(server);
	server->snip_on = false;
	server->snip_held = false;
	server->snip_lit = false;
	server->snip_shot = false;
	server->snip_dirty = false;
	server->snip_down_at = 0.0;
	if (server->snip_hand) {
		wlr_scene_node_set_enabled(&server->snip_hand->node, false);
	}
	/* (and the rest of it out of sight, for the next time the layer is
	 * shown: the self-timer's black, a moment) */
	server->snip_shade = 0.0;
	server->snip_k = 0.0;
	snip_tint(server, 0.0);
	if (server->snip_info) {
		wlr_scene_node_set_enabled(&server->snip_info->node, false);
	}
	if (server->snip_ring) {
		wlr_scene_node_set_enabled(&server->snip_ring->node, false);
	}
	server->snip_grim = (struct wlr_box){ 0 };
	if (server->snip_timer) {
		wl_event_source_timer_update(server->snip_timer, 0);
	}
	snip_rect(server->snip_shut, 0, 0, 0, 0);
	snip_rect(server->snip_hole, 0, 0, 0, 0);
	snip_rect(server->snip_light, 0, 0, 0, 0);
	snip_rect(server->snip_glow, 0, 0, 0, 0);
	server->snip_light_at = 0.0;
	if (server->snip_hero) {
		wlr_scene_node_set_enabled(&server->snip_hero->node, false);
	}
	server->snip_eye_on = false;
	server->snip_eye_r = 0.0;
	server->snip_dim = 0.0;
	server->snip_drag = -1;
	wlr_scene_node_set_enabled(&server->layer_snip->node, false);
	cursor_image(server, "default");
	server_schedule_frames(server);
	/* a film begun: its first frame, Print all gone from the screen */
	if (server->rec) {
		server->rec_owed = true;
		rec_timer_next(server);
	}
	if (a.width > 0 && a.height > 0) {
		char cmd[160];
		/* (a moment first, for Print's to be off the screen) */
		snprintf(cmd, sizeof cmd, "sleep 0.18; desk-screenshot --geometry '%d,%d %dx%d'",
			a.x, a.y, a.width, a.height);
		wlr_log(WLR_INFO, "screenshot: %s", cmd);
		spawn(cmd);
	}
}

/* Put down (take: with the picture the shutter took), or given up: what
 * was taken cut from the stills and written out now, and the camera taken
 * down from the eye (snip_tick) -- at once if it never came up. */
static void snip_end(struct desk_server *server, bool take)
{
	if (!server->snip_on || server->snip_down_at > 0.0) {
		return;
	}
	struct wlr_box a = server->snip_take;
	take = take && server->snip_shot && a.width >= 3 && a.height >= 3;
	cairo_surface_t *cut = take ? snip_cut(server, a) : NULL;
	if (cut) {
		snip_save(server, cut);
	}
	server->snip_grim = take && !cut ? a : (struct wlr_box){ 0 };
	server->snip_held = false;
	server->snip_lit = false;
	snip_rect(server->snip_shut, 0, 0, 0, 0);
	/* (the display, its red dot lit, stays as the camera goes down with
	 * a film begun) */
	if (server->snip_info && !server->rec) {
		wlr_scene_node_set_enabled(&server->snip_info->node, false);
	}
	if (server->snip_ring) {
		wlr_scene_node_set_enabled(&server->snip_ring->node, false);
	}
	if (server->snip_freezing || !server->snip_hero) {
		snip_done(server);
		return;
	}
	server->snip_down_at = monotonic_sec();
	server->snip_ticked = server->snip_down_at;
	server_schedule_frames(server);
}

/* Print gone at once, nothing taken: the door closing on it, a screen it
 * was on taken away. */
static void snip_stop(struct desk_server *server)
{
	snip_count_stop(server);
	if (server->snip_on) {
		server->snip_grim = (struct wlr_box){ 0 };
		server->snip_late = server->snip_flash = false;
		snip_done(server);
	}
}

static int snip_shut_cb(void *data)
{
	struct desk_server *server = data;
	if (server->snip_flash) {
		server->snip_flash = false;
		snip_done(server);
	} else {
		snip_end(server, true);
	}
	return 0;
}

static void snip_shut_after(struct desk_server *server, double ms)
{
	if (!server->snip_timer) {
		server->snip_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), snip_shut_cb, server);
	}
	if (server->snip_timer) {
		wl_event_source_timer_update(server->snip_timer, ms < 1.0 ? 1 : (int)lround(ms));
	} else {
		snip_shut_cb(server);
	}
}

/* ---- the film: Print with a tape in it ------------------------------
 *
 * A tape in the camera (snip_tape: put in at its slot on the camera in the
 * hand, or with Shift+Print), the shutter records instead of taking a
 * picture: what is lit -- the area drawn out, the window, the screen --
 * from when the camera is down until Print is pressed again or the clock's
 * face is (rec_end). As a camcorder on its stand records what is in front
 * of it: the windows are worked meanwhile, and one moved out of what is
 * being recorded is out of the film. The screen's frames are read as they
 * go out (rec_output_commit), not drawn again for the film: no more often
 * than REC_FPS, only when what is recorded has changed (and once more
 * after the last change, if that came too soon after the frame before),
 * the pointer drawn into them meanwhile (as the screen's own, not the
 * card's) and no program's frames sent straight to the screen past the
 * drawing. desk-record makes the film of them, and of the desk's sound
 * (rec.c). The clock on the back of the desk says it is running, and for
 * how long, in place of the time (clock_publish, rec_secs). */

static void rec_end(struct desk_server *server);

/* How long the film has run, whole seconds; -1 if there is none. */
static int rec_secs(struct desk_server *server)
{
	if (!server->rec) {
		return -1;
	}
	double t = real_sec() - server->rec_due;
	return t > 0.0 ? (int)floor(t) : 0;
}

/* The screen the film was of, as it was before: the pointer the card's
 * again, and a program's frames free to go straight to it. */
static void rec_let_go(struct desk_server *server)
{
	if (server->rec_output) {
		wlr_output_lock_software_cursors(server->rec_output, false);
		wlr_output_lock_attach_render(server->rec_output, false);
	}
	server->rec_output = NULL;
}

/* A box on the screens, in the pixels of wo's frames (as wlr_scene lays
 * them out: scaled, and turned as the screen is). */
static struct wlr_box rec_to_frame(struct wlr_output *wo, struct wlr_box ob, struct wlr_box a)
{
	double sc = wo->scale > 0.0f ? wo->scale : 1.0;
	struct wlr_box local = {
		(int)lround((a.x - ob.x) * sc), (int)lround((a.y - ob.y) * sc),
		(int)lround(a.width * sc), (int)lround(a.height * sc),
	};
	int tw, th;
	wlr_output_transformed_resolution(wo, &tw, &th);
	struct wlr_box out;
	wlr_box_transform(&out, &local, wlr_output_transform_invert(wo->transform), tw, th);
	return out;
}

static int rec_timer_cb(void *data);

/* The timer set for what is due next: the film's first frame, a frame owed,
 * the clock's next second. */
static void rec_timer_next(struct desk_server *server)
{
	if (!server->rec) {
		return;
	}
	if (!server->rec_timer) {
		server->rec_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), rec_timer_cb, server);
		if (!server->rec_timer) {
			return;
		}
	}
	double now = real_sec(), next;
	if (now < server->rec_due) {
		next = server->rec_due;
	} else {
		next = server->rec_due + floor(now - server->rec_due) + 1.0;
		if (server->rec_owed) {
			next = fmin(next, server->rec_read_at + 1.0 / REC_FPS);
		}
	}
	int ms = (int)ceil((next - now) * 1000.0);
	wl_event_source_timer_update(server->rec_timer, ms < 1 ? 1 : ms);
}

/* On its time: whether desk-record is still taking the frames, and the
 * screen still the size it was; the clock's seconds; a frame owed. */
static int rec_timer_cb(void *data)
{
	struct desk_server *server = data;
	if (!server->rec) {
		return 0;
	}
	struct wlr_output *wo = server->rec_output;
	struct wlr_box r = server->rec_src;
	if (!desk_rec_running(server->rec)) {
		wlr_log(WLR_ERROR, "print: desk-record is not taking the film's frames");
		rec_end(server);
		return 0;
	}
	if (r.x < 0 || r.y < 0 || r.x + r.width > wo->width || r.y + r.height > wo->height) {
		wlr_log(WLR_INFO, "print: the screen being recorded is not the size it was");
		rec_end(server);
		return 0;
	}
	int secs = rec_secs(server);
	if (secs != server->rec_shown) {
		server->rec_shown = secs;
		clock_publish(server);
	}
	if (!server->snip_on && real_sec() >= server->rec_due && server->rec_owed) {
		wlr_output_schedule_frame(wo);
	}
	rec_timer_next(server);
	return 0;
}

/* The film armed: a (on the screens -- as much of it as is on the screen
 * its middle is on) recorded from `after` seconds from now, or once Print
 * is all gone if that is later; desk-record started now, to be ready by
 * then. False if it cannot be: too small, off the screens, no recorder. */
static bool rec_arm(struct desk_server *server, struct wlr_box a, double after)
{
	if (server->rec) {
		return false;
	}
	struct wlr_output *wo = screen_at(server, a.x + a.width * 0.5, a.y + a.height * 0.5);
	struct wlr_box ob, in;
	if (!wo) {
		return false;
	}
	wlr_output_layout_get_box(server->output_layout, wo, &ob);
	if (wlr_box_empty(&ob) || !wlr_box_intersection(&in, &a, &ob)) {
		return false;
	}
	struct wlr_box src = rec_to_frame(wo, ob, in);
	/* (an even number of pixels each way: a film's colour is a pixel's
	 * worth to every four) */
	src.width &= ~1;
	src.height &= ~1;
	if (src.width < 16 || src.height < 16) {
		return false;
	}
	double wait = fmax(after, REC_READY_S);
	struct timespec wall;
	clock_gettime(CLOCK_REALTIME, &wall);
	double t0 = (double)wall.tv_sec + (double)wall.tv_nsec / 1e9 + wait;
	server->rec = desk_rec_start(src.width, src.height, t0, wo->transform,
		desk_lang_code(desk_lang_get()), false);
	if (!server->rec) {
		wlr_log(WLR_ERROR, "print: the film could not be begun");
		return false;
	}
	server->rec_output = wo;
	server->rec_box = in;
	server->rec_src = src;
	server->rec_due = real_sec() + wait;
	server->rec_read_at = 0.0;
	server->rec_owed = true;
	server->rec_shown = 0;
	/* the pointer in the frames; and no program's frames straight to the
	 * screen, past them */
	wlr_output_lock_software_cursors(wo, true);
	wlr_output_lock_attach_render(wo, true);
	wlr_log(WLR_INFO, "print: recording %dx%d of %s, from %.2f s", src.width, src.height,
		wo->name, wait);
	clock_publish(server);
	rec_timer_next(server);
	return true;
}

/* The film stopped: Print again, the clock's face pressed, the door
 * locked, its screen gone, the desk going -- or desk-record gone. */
static void rec_end(struct desk_server *server)
{
	if (!server->rec) {
		return;
	}
	double ran = real_sec() - server->rec_due;
	desk_rec_stop(server->rec);
	server->rec = NULL;
	wlr_log(WLR_INFO, "print: the film stopped, %.1f s", ran > 0.0 ? ran : 0.0);
	if (server->rec_timer) {
		wl_event_source_timer_update(server->rec_timer, 0);
	}
	rec_let_go(server);
	if (server->quitting) {
		return;
	}
	server->rec_shown = -1;
	clock_publish(server);
	struct wlr_box b = server->rec_box;
	snd_at(server, DESK_SND_REC_STOP, b.x + b.width * 0.5, 0.8);
}

/* Whether what is recorded has changed in this frame. */
static bool rec_touched(struct desk_server *server, struct wlr_scene_output *so)
{
	if (so->output->needs_frame) {
		return true;
	}
	struct wlr_box r = server->rec_src;
	pixman_box32_t box = { r.x, r.y, r.x + r.width, r.y + r.height };
	return pixman_region32_contains_rectangle(desk_scene_output_pending_damage(so), &box)
		!= PIXMAN_REGION_OUT;
}

/* What is recorded, read out of the frame into one of the recorder's
 * pictures -- if one is free: if not, read a frame later (rec_owed). */
static void rec_read(struct desk_server *server, struct wlr_buffer *buffer, double now)
{
	struct wlr_box r = server->rec_src;
	server->rec_read_at = now;
	server->rec_owed = false;
	if (r.x < 0 || r.y < 0 || r.x + r.width > buffer->width || r.y + r.height > buffer->height) {
		return;   /* (the screen changed size: rec_timer_cb ends it) */
	}
	uint8_t *frame = desk_rec_frame(server->rec);
	if (!frame) {
		server->rec_owed = true;
		rec_timer_next(server);
		return;
	}
	struct wlr_texture *tex = wlr_texture_from_buffer(server->renderer, buffer);
	bool ok = tex && wlr_texture_read_pixels(tex, &(struct wlr_texture_read_pixels_options){
		.data = frame,
		.format = DRM_FORMAT_ARGB8888,
		.stride = (uint32_t)r.width * 4,
		.src_box = r,
	});
	if (tex) {
		wlr_texture_destroy(tex);
	}
	if (!ok) {
		static bool said;
		if (!said) {
			said = true;
			wlr_log(WLR_ERROR, "print: a frame of %s could not be read for the film",
				server->rec_output->name);
		}
	}
	desk_rec_frame_done(server->rec, frame, ok);
}

/* A frame of the screen being recorded, drawn as wlr_scene_output_commit
 * would draw it -- not at all, if nothing on the screen has changed -- and
 * what is recorded read out of it before it goes out. */
static void rec_output_commit(struct desk_server *server, struct wlr_scene_output *so)
{
	struct wlr_output *wo = so->output;
	double now = real_sec();
	bool live = server->rec && !server->snip_on && now >= server->rec_due;
	/* (what wlr_scene_output_commit asks itself: whether anything on the
	 * screen has changed -- which wlroots 0.18 keeps in a field it calls
	 * its own, pending_commit_damage) */
	bool changed = wo->needs_frame || pixman_region32_not_empty(desk_scene_output_pending_damage(so));
	bool read = false;
	if (live && (server->rec_owed || (changed && rec_touched(server, so)))) {
		if (now >= server->rec_read_at + 1.0 / REC_FPS - 0.002) {
			read = true;
		} else {
			server->rec_owed = true;
			rec_timer_next(server);
		}
	}
	if (!changed && !read) {
		return;
	}
	struct wlr_output_state st;
	wlr_output_state_init(&st);
	if (wlr_scene_output_build_state(so, &st, NULL)) {
		if (st.buffer && read) {
			rec_read(server, st.buffer, now);
		}
		wlr_output_commit_state(wo, &st);
	}
	wlr_output_state_finish(&st);
}

/* The shutter pressed with a tape in: what is lit recorded, from when the
 * camera is down -- desk-record started now, to be ready by then -- and
 * the red dot lit in the display meanwhile, as the camera goes down. */
static void snip_film(struct desk_server *server, struct wlr_box a)
{
	double down = server->snip_hero && !server->snip_freezing ? real_secs(SNIP_DOWN_S) : 0.0;
	if (!rec_arm(server, a, down)) {
		snip_end(server, false);
		return;
	}
	server->snip_take = a;
	server->snip_shot = true;
	snd_at(server, DESK_SND_REC_START, a.x + a.width * 0.5, 0.8);
	snip_display(server);
	snip_end(server, false);
}

/* ---- the self-timer ----
 *
 * Set, the shutter does not go off at once: the camera is put down there
 * and then and the screens are the hand's again -- for a menu to be opened,
 * a pointer put over something -- while the self-timer counts down beside
 * what it is to take, its lamp blinking and beeping once a second, four
 * times a second for the last two, as a camera's does. Then the picture is
 * taken of the screens as they are (snip_freeze once more, the count out
 * of sight for it), the shutter heard and the flash over what was taken. Print again, or the door closing, and it is given up. */

static void snip_freeze(struct desk_server *server);

static void snip_count_show(struct desk_server *server, int left, bool lit)
{
	struct wlr_box a = server->snip_take, b = snip_bounds(server);
	double pt = snip_pt(server, a);
	int bw, bh;
	desk_finder_count_size(pt, &bw, &bh);
	struct desk_buf *buf = buf_create(bw, bh);
	desk_draw_finder_count(buf->cr, left, lit, pt);
	scene_put(&server->snip_count_node, server->layer_count, buf, 0, 0, NULL);
	server->snip_count_node->point_accepts_input = shadow_accepts;
	/* over its top right corner, outside it; inside, where there is no room */
	int gap = (int)lround(8.0 * pt);
	int x = a.x + a.width - bw, y = a.y - gap - bh;
	if (y < b.y) {
		y = a.y + gap;
		x = a.x + a.width - bw - gap;
	}
	wlr_scene_node_set_position(&server->snip_count_node->node, x < b.x ? b.x : x, y);
	wlr_scene_node_set_enabled(&server->snip_count_node->node, true);
	server_schedule_frames(server);
}

static void snip_count_stop(struct desk_server *server)
{
	if (!server->snip_counting) {
		return;
	}
	server->snip_counting = false;
	if (server->snip_count_timer) {
		wl_event_source_timer_update(server->snip_count_timer, 0);
	}
	if (server->snip_count_node) {
		wlr_scene_node_set_enabled(&server->snip_count_node->node, false);
	}
	server_schedule_frames(server);
}

static int snip_count_cb(void *data)
{
	struct desk_server *server = data;
	if (!server->snip_counting) {
		return 0;
	}
	double left = server->snip_count_end - real_sec(), slow = g_time_slow;
	if (left <= 0.02 * slow) {
		/* off: the count out of sight, and the screens taken as they are
		 * -- or, a tape in, recorded from now */
		snip_count_stop(server);
		if (server->snip_tape) {
			struct wlr_box a = server->snip_take;
			if (rec_arm(server, a, 0.0)) {
				snd_at(server, DESK_SND_REC_START, a.x + a.width * 0.5, 0.8);
			}
			return 0;
		}
		server->snip_late = true;
		server->snip_on = true;
		server->snip_held = server->snip_lit = server->snip_shot = false;
		snip_freeze(server);
		return 0;
	}
	double beat = left > 2.0 * slow ? 1.0 * slow : 0.25 * slow;
	server->snip_count_lit = !server->snip_count_lit;
	if (server->snip_count_lit) {
		struct wlr_box a = server->snip_take;
		int h = snd_at(server, DESK_SND_BEEP, a.x + a.width * 0.5, 0.30);
		desk_sound_stop_after(h, 0.07, 0.01);
	}
	snip_count_show(server, (int)ceil(left / slow), server->snip_count_lit);
	double half = beat * 0.5;
	wl_event_source_timer_update(server->snip_count_timer,
		(int)lround(fmax(fmin(half, left), 0.02) * 1000.0));
	return 0;
}

/* The shutter pressed with the self-timer set: a for it to take. */
static void snip_count_start(struct desk_server *server, struct wlr_box a)
{
	server->snip_take = a;
	server->snip_grim = (struct wlr_box){ 0 };
	snip_done(server);
	server->snip_counting = true;
	server->snip_count_lit = false;
	server->snip_count_end = real_sec()
		+ (server->snip_timer_set == 1 ? 2.0 : 10.0) * g_time_slow;
	if (!server->snip_count_timer) {
		server->snip_count_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), snip_count_cb, server);
	}
	if (!server->snip_count_timer) {
		server->snip_counting = false;
		return;
	}
	wlr_log(WLR_INFO, "print: the self-timer, %d s", server->snip_timer_set == 1 ? 2 : 10);
	snip_count_cb(server);
}

/* Let go over a picture: the shutter, and the flash going off over what
 * is taken -- the display out with it, as a camera's is -- and as long as
 * the mirror is up (SNIP_SHUT_MS, whatever the motion is set to) nothing
 * more is done with it; then the camera put down, the flash dying away as
 * it goes, and the picture taken. */
static void snip_shoot(struct desk_server *server, struct wlr_box a)
{
	if (server->snip_timer_set > 0) {
		snip_count_start(server, a);
		return;
	}
	if (server->snip_tape) {
		snip_film(server, a);
		return;
	}
	server->snip_take = a;
	server->snip_shot = true;
	server->snip_cam.count = server->snip_cam.count % 99 + 1;
	snip_light_fire(server, snip_area_view(server));
	if (server->snip_info) {
		wlr_scene_node_set_enabled(&server->snip_info->node, false);
	}
	if (server->snip_ring) {
		wlr_scene_node_set_enabled(&server->snip_ring->node, false);
	}
	snd_at(server, DESK_SND_SHUTTER, a.x + a.width * 0.5, 1.0);
	snip_shut_after(server, SNIP_SHUT_MS * g_time_slow);
	server_schedule_frames(server);
}

/* The window under (x, y) on the screens, open, paper and all -- as
 * Alt+Print takes it. */
static bool snip_window_at(struct desk_server *server, double x, double y, struct wlr_box *box)
{
	struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		int lx, ly;
		if (!v->scene_tree || !v->scene_tree->node.enabled || v->sheet.phase != SHEET_IDLE
			|| v->outer_w <= 0 || v->outer_h <= 0
			|| !wlr_scene_node_coords(&v->scene_tree->node, &lx, &ly)) {
			continue;
		}
		struct wlr_box b = { lx, ly, v->outer_w, v->outer_h };
		if (wlr_box_contains_point(&b, x, y)) {
			*box = b;
			return true;
		}
	}
	return false;
}

/* The screen under (x, y). */
static bool snip_screen_at(struct desk_server *server, double x, double y, struct wlr_box *box)
{
	struct wlr_output *out = screen_at(server, x, y);
	if (!out) {
		return false;
	}
	wlr_output_layout_get_box(server->output_layout, out, box);
	return !wlr_box_empty(box);
}

static void snip_set_cursor(struct desk_server *server, const char *name)
{
	if (!server->snip_cursor || strcmp(server->snip_cursor, name) != 0) {
		server->snip_cursor = name;
		cursor_image(server, name);
	}
}

/* The hand moving while the camera is up: an area being drawn out, or --
 * the dial set to a window or a screen -- the one under it lit; the
 * pointer a hand over the camera's parts. */
static void snip_motion(struct desk_server *server)
{
	if (server->snip_shot || server->snip_down_at > 0.0 || server->snip_freezing) {
		return;
	}
	double x = server->cursor->x, y = server->cursor->y, px, py;
	if (server->snip_held) {
		/* (on the picture it was begun on, out to its edges) */
		struct snip_still *st = server->snip_drag >= 0 && server->snip_drag < server->snip_still_n
			? &server->snip_still[server->snip_drag] : NULL;
		snip_to_picture(st, x, y, &px, &py);
		server->snip_x1 = (int)lround(px);
		server->snip_y1 = (int)lround(py);
		snip_paint(server);
		return;
	}
	enum desk_finder_part part = snip_hand_part(server, x, y);
	snip_set_cursor(server, part == DESK_FINDER_NOTHING ? "crosshair"
		: part == DESK_FINDER_BODY ? "default" : "pointer");
	if (server->snip_mode == 0) {
		return;
	}
	struct wlr_box b = { 0 };
	snip_to_picture(snip_still_at(server, x, y), x, y, &px, &py);
	bool lit = part == DESK_FINDER_NOTHING && (server->snip_mode == 1
		? snip_window_at(server, px, py, &b) : snip_screen_at(server, px, py, &b));
	if (lit != server->snip_lit || !wlr_box_equal(&b, &server->snip_target)) {
		server->snip_lit = lit;
		server->snip_target = lit ? b : (struct wlr_box){ 0 };
		snip_paint(server);
	}
}

/* The dial turned a step or more (round past its ends, wrap), a detent's
 * tick for each. */
static void snip_turn(struct desk_server *server, int by, bool wrap)
{
	int m = server->snip_mode + by;
	m = wrap ? (m % 3 + 3) % 3 : (m < 0 ? 0 : m > 2 ? 2 : m);
	if (m == server->snip_mode) {
		return;
	}
	double now = monotonic_sec();
	snip_cam_now(server, now);
	server->snip_dial_from = server->snip_cam.dial;
	server->snip_dial_at = now;
	double dx, dy;
	desk_finder_camera_dial(&dx, &dy);
	double x = server->snip_hand->node.x + dx * server->snip_hand_s;
	int steps = abs(m - server->snip_mode);
	for (int i = 0; i < steps; i++) {
		snd_in(server, DESK_SND_KNOB_TICK, x, 0.9, 0.0, SNIP_TURN_S * (i + 0.5) / steps);
	}
	server->snip_mode = m;
	server->snip_lit = false;
	server->snip_target = (struct wlr_box){ 0 };
	snip_paint(server);
	snip_motion(server);
	server_schedule_frames(server);
}

/* What the shutter button takes, pressed by itself: an area -- nothing
 * drawn out -- all the screen it is held up to; the window in front; or
 * that screen. */
static struct wlr_box snip_shutter_box(struct desk_server *server)
{
	struct wlr_box b = server->snip_screen;
	if (server->snip_mode == 1 && !wl_list_empty(&server->toplevels)) {
		struct desk_toplevel *v;
		wl_list_for_each(v, &server->toplevels, link) {
			int lx, ly;
			if (v->scene_tree && v->scene_tree->node.enabled && v->sheet.phase == SHEET_IDLE
				&& v->outer_w > 0 && v->outer_h > 0
				&& wlr_scene_node_coords(&v->scene_tree->node, &lx, &ly)) {
				b = (struct wlr_box){ lx, ly, v->outer_w, v->outer_h };
				break;
			}
		}
	}
	return b;
}

/* A part of the camera in the hand, worked. */
static void snip_work(struct desk_server *server, enum desk_finder_part part)
{
	double now = monotonic_sec();
	double x = server->cursor->x;
	switch (part) {
	case DESK_FINDER_DIAL:
		snip_turn(server, 1, true);
		break;
	case DESK_FINDER_TIMER:
		snip_cam_now(server, now);
		server->snip_slide_from = server->snip_cam.timer;
		server->snip_slide_at = now;
		server->snip_timer_set = (server->snip_timer_set + 1) % 3;
		server->snip_dirty = true;   /* (the display says so) */
		snd_at(server, DESK_SND_TOGGLE, x, 0.7);
		break;
	case DESK_FINDER_POWER:
		/* off: the camera put down */
		server->snip_power_at = now;
		snd_at(server, DESK_SND_TOGGLE, x, 0.8);
		snip_end(server, false);
		break;
	case DESK_FINDER_TAPE:
		/* a tape put in, or the one in it thrown out */
		snip_tape_set(server, !server->snip_tape, now);
		break;
	case DESK_FINDER_SHUTTER: {
		struct wlr_box b = snip_shutter_box(server);
		server->snip_press_at = now;
		if (server->snip_mode == 0) {
			struct snip_still *st = snip_still_at(server, b.x + b.width * 0.5,
				b.y + b.height * 0.5);
			server->snip_drag = st ? (int)(st - server->snip_still) : -1;
			server->snip_x0 = b.x;
			server->snip_y0 = b.y;
			server->snip_x1 = b.x + b.width;
			server->snip_y1 = b.y + b.height;
		} else {
			server->snip_target = b;
		}
		server->snip_lit = true;
		snip_paint(server);
		snip_shoot(server, b);
		break;
	}
	default:
		break;
	}
	server_schedule_frames(server);
}

/* A button while the camera is up. */
static void snip_button(struct desk_server *server, uint32_t button, bool down)
{
	if (server->snip_shot || server->snip_down_at > 0.0) {
		return;
	}
	double x = server->cursor->x, y = server->cursor->y;
	if (button == BTN_RIGHT && down) {
		snip_end(server, false);
	} else if (button == BTN_LEFT && down && !server->snip_held) {
		enum desk_finder_part part = snip_hand_part(server, x, y);
		if (part != DESK_FINDER_NOTHING) {
			snip_work(server, part);
		} else if (server->snip_mode == 0) {
			/* begun on the picture of the screen the hand is on */
			struct snip_still *st = snip_still_at(server, x, y);
			double px, py;
			snip_to_picture(st, x, y, &px, &py);
			server->snip_drag = st ? (int)(st - server->snip_still) : -1;
			server->snip_x0 = server->snip_x1 = (int)lround(px);
			server->snip_y0 = server->snip_y1 = (int)lround(py);
			server->snip_held = true;
			server->snip_lit = true;
			snip_paint(server);
		} else if (server->snip_lit) {
			snip_shoot(server, server->snip_target);
		}
	} else if (button == BTN_LEFT && !down && server->snip_held) {
		struct snip_still *st = server->snip_drag >= 0 && server->snip_drag < server->snip_still_n
			? &server->snip_still[server->snip_drag] : NULL;
		double px, py;
		snip_to_picture(st, x, y, &px, &py);
		server->snip_x1 = (int)lround(px);
		server->snip_y1 = (int)lround(py);
		struct wlr_box a = snip_area(server);
		if (a.width >= 3 && a.height >= 3) {
			snip_paint(server);
			snip_shoot(server, a);
		} else {
			snip_end(server, false);
		}
	}
}

/* The wheel while the camera is up: over the dial, it turns it. */
static void snip_wheel(struct desk_server *server, const struct wlr_pointer_axis_event *ev)
{
	if (server->snip_shot || server->snip_down_at > 0.0
		|| snip_hand_part(server, server->cursor->x, server->cursor->y) != DESK_FINDER_DIAL) {
		return;
	}
	int by = ev->delta_discrete != 0 ? (ev->delta_discrete > 0 ? 1 : -1)
		: (ev->delta > 0.0 ? 1 : ev->delta < 0.0 ? -1 : 0);
	if (by) {
		snip_turn(server, by, false);
	}
}

/* A picture of the screen -- or of the sheet in front, paper and all --
 * with grim, by desk-screenshot (which files it and posts a letter). */
static void screenshot_take(struct desk_server *server, bool window)
{
	char cmd[160] = "desk-screenshot";
	if (window && !wl_list_empty(&server->toplevels)) {
		struct desk_toplevel *v = wl_container_of(server->toplevels.next, v, link);
		if (v->scene_tree && v->scene_tree->node.enabled && v->outer_w > 0 && v->outer_h > 0) {
			snprintf(cmd, sizeof cmd, "desk-screenshot --geometry '%d,%d %dx%d'",
				v->scene_tree->node.x, v->scene_tree->node.y, v->outer_w, v->outer_h);
		}
	}
	wlr_log(WLR_INFO, "screenshot: %s", cmd);
	spawn(cmd);
}

/* A program asks for one of its windows to be brought to the front -- with
 * a token it got from whatever started it, or from itself. A window folded
 * away is unfolded for it, as clicking its packet would. */
static void xdg_request_activate(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, request_activate);
	const struct wlr_xdg_activation_v1_request_activate_event *ev = data;
	struct desk_toplevel *view = view_of_surface(server, ev->surface);
	if (!view || !view->scene_tree || server->lock_phase != 0) {
		return;
	}
	if (view->sheet.phase != SHEET_IDLE) {
		sheet_begin_unfold(server, &view->sheet);
	}
	focus_toplevel(view);
	server_schedule_frames(server);
}

static bool ti_mods_in(struct desk_server *server, const struct wlr_keyboard_modifiers *mods);

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
	struct desk_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	(void)data;
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	/* Alt let go of with the windows held up: to the one it is on */
	if (keyboard->server->sw_on
		&& !(wlr_keyboard_get_modifiers(keyboard->wlr_keyboard) & WLR_MODIFIER_ALT)) {
		switcher_finish(keyboard->server, true);
	}
	if (ti_mods_in(keyboard->server, &keyboard->wlr_keyboard->modifiers)) {
		return;   /* behind a key the input method is answering for */
	}
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

/* An input method's own window, by its name -- a Wayland app_id, an X
 * window's class: IBus's panel (ibus-ui-gtk3: the candidates, the
 * switcher), Mozc's renderer (its candidates where they are not IBus's),
 * fcitx's candidates. */
static bool ime_window_name(const char *name)
{
	static const char *const names[] = { "ibus-ui-gtk3", "mozc_renderer", "fcitx" };
	for (size_t i = 0; name && i < sizeof names / sizeof *names; i++) {
		if (strcasecmp(name, names[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* ---- typing into programs' windows, through the input method --------
 *
 * A program on Wayland that wants an input method says so with text-input
 * (GTK, Qt), and the compositor is the go-between: the keys go to the input
 * method first -- IBus, by way of the same input context DEsk types its
 * own notes through (ime.c) -- and only what it does not keep goes on to
 * the window, in order; what it writes comes back to the window as the
 * text being written (preedit) and then the text written (commit). The
 * compositor knows where the window is, so the input method is told where
 * on the screen the cursor is, and Mozc's candidates come up under it --
 * a program talking to IBus directly can only say where in its window,
 * and they came up in the corner of the screen. And the input method's
 * mode, あ or A as 半角/全角 goes, comes back with it, for the clock. */

struct desk_ti {
	struct desk_server *server;
	struct wlr_text_input_v3 *ti;
	struct wl_list link;
	struct wl_listener enable, commit, disable, destroy;
};

enum { TIK_IDLE = 0, TIK_ASKED, TIK_ASKED_UP, TIK_DOWN, TIK_TAKEN };
