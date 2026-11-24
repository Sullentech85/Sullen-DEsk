/* Part of wm.c: the scene's layers, shadows, pictures and what a view is made of.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

struct desk_buf *buf_create(int w, int h)
{
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
	struct desk_buf *buffer = calloc(1, sizeof(*buffer));
	if (!buffer) {
		/* nothing more can be drawn: said, rather than a fall unexplained */
		wlr_log(WLR_ERROR, "out of memory for a %dx%d picture", w, h);
		abort();
	}
	wlr_buffer_init(&buffer->base, &cairo_buffer_impl, w, h);
	buffer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	buffer->cr = cairo_create(buffer->surface);
	cairo_set_operator(buffer->cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(buffer->cr);
	cairo_set_operator(buffer->cr, CAIRO_OPERATOR_OVER);
	return buffer;
}

static void prof_upload(int w, int h);

/* len, shortened if it must be so that start + len, as the machine adds
 * it, does not pass end */
static double span_inside(double start, double len, double end)
{
	len = fmin(len, end - start);
	while (len > 0.0 && start + len > end) {
		len = nextafter(len, 0.0);
	}
	return len;
}

/* What a node's picture measures, kept beside the node. Once a picture is
 * on the GPU the scene lets go of it -- b->buffer is NULL from then on, on
 * a GLES2 screen, though never on a pixman one, which reads the picture
 * every time -- so its size is written down as it is handed over
 * (scene_new, scene_swap) and read back by whatever lays a box over it.
 * Read off b->buffer instead, a box went on being laid for the size of the
 * screen over a picture a third of it, and wlroots stopped the desk.
 *
 * And the picture itself, for the few whose pixels are read back later
 * (scene_hold): a sheet's shadow, paper and corner, which what is under
 * another sheet's turned-up corner is drawn from (curl_underlay). Let go
 * of, the GPU screen showed the bare desk there (#10). */
struct pic_size {
	struct wlr_addon addon;
	int w, h;
	bool hold;
	struct wlr_buffer *held;
};

static void pic_size_destroy(struct wlr_addon *addon)
{
	struct pic_size *p = wl_container_of(addon, p, addon);
	if (p->held) {
		wlr_buffer_unlock(p->held);
	}
	wlr_addon_finish(addon);
	free(p);
}

/* p holding on to buffer, and no longer to the one before */
static void pic_hold_swap(struct pic_size *p, struct wlr_buffer *buffer)
{
	if (p->held == buffer) {
		return;
	}
	if (buffer) {
		wlr_buffer_lock(buffer);
	}
	if (p->held) {
		wlr_buffer_unlock(p->held);
	}
	p->held = buffer;
}

static const struct wlr_addon_interface PIC_SIZE = {
	.name = "desk-picture-size",
	.destroy = pic_size_destroy,
};

static void pic_size_keep(struct wlr_scene_buffer *b, struct wlr_buffer *buffer)
{
	if (!buffer) {
		return;
	}
	struct wlr_addon *a = wlr_addon_find(&b->node.addons, b, &PIC_SIZE);
	struct pic_size *p;
	if (a) {
		p = wl_container_of(a, p, addon);
	} else {
		p = calloc(1, sizeof *p);
		if (!p) {
			return;
		}
		wlr_addon_init(&p->addon, &b->node.addons, b, &PIC_SIZE);
	}
	p->w = buffer->width;
	p->h = buffer->height;
	if (p->hold) {
		pic_hold_swap(p, buffer);
	}
}

/* b's pictures kept by us as well, from this one on, to be read back
 * (scene_pic_buffer) after the scene has let go of them. */
static void scene_hold(struct wlr_scene_buffer *b)
{
	if (!b || !b->buffer) {
		return;
	}
	pic_size_keep(b, b->buffer);
	struct wlr_addon *a = wlr_addon_find(&b->node.addons, b, &PIC_SIZE);
	if (a) {
		struct pic_size *p = wl_container_of(a, p, addon);
		p->hold = true;
		pic_hold_swap(p, b->buffer);
	}
}

/* b's picture, from the scene or held by us (scene_hold); NULL if neither. */
static struct wlr_buffer *scene_pic_buffer(struct wlr_scene_buffer *b)
{
	if (b->buffer) {
		return b->buffer;
	}
	struct wlr_addon *a = wlr_addon_find(&b->node.addons, b, &PIC_SIZE);
	if (!a) {
		return NULL;
	}
	struct pic_size *p = wl_container_of(a, p, addon);
	return p->held;
}

/* The size of b's picture, whether or not the scene still holds it; false
 * if it has none. */
static bool scene_pic_size(struct wlr_scene_buffer *b, int *w, int *h)
{
	if (b->buffer) {
		*w = b->buffer->width;
		*h = b->buffer->height;
		return true;
	}
	struct wlr_addon *a = wlr_addon_find(&b->node.addons, b, &PIC_SIZE);
	if (!a) {
		return false;
	}
	struct pic_size *p = wl_container_of(a, p, addon);
	*w = p->w;
	*h = p->h;
	return true;
}

/* Show src of b's picture (NULL: all of it). Every source box goes through
 * here and is kept inside the picture to the last bit: wlroots will not
 * draw one that reaches past its picture's edge by any amount, and a box
 * worked out in floating point -- a scaled picture cut at a screen's edge
 * -- can come to 160.00000000000003 of a picture 160 wide. */
void scene_source(struct wlr_scene_buffer *b, const struct wlr_fbox *src)
{
	int pw, ph;
	if (!src || wlr_fbox_empty(src) || !scene_pic_size(b, &pw, &ph)) {
		wlr_scene_buffer_set_source_box(b, src);
		return;
	}
	struct wlr_fbox box = *src;
	if (!isfinite(box.x) || !isfinite(box.y) || !isfinite(box.width)
		|| !isfinite(box.height)) {
		wlr_scene_buffer_set_source_box(b, NULL);
		return;
	}
	if (box.x < 0.0) {
		box.width += box.x;
		box.x = 0.0;
	}
	if (box.y < 0.0) {
		box.height += box.y;
		box.y = 0.0;
	}
	box.width = span_inside(box.x, box.width, pw);
	box.height = span_inside(box.y, box.height, ph);
	wlr_scene_buffer_set_source_box(b, &box);
}

/* A new picture for b; the box the last one was shown through is kept
 * inside it until whoever lays b gives it its own. */
static void scene_swap(struct wlr_scene_buffer *b, struct wlr_buffer *buffer)
{
	wlr_scene_buffer_set_buffer(b, buffer);
	pic_size_keep(b, buffer);
	if (!wlr_fbox_empty(&b->src_box)) {
		struct wlr_fbox keep = b->src_box;
		scene_source(b, &keep);
	}
}

/* A node for a picture of ours, its size kept (pic_size_keep). */
static struct wlr_scene_buffer *scene_new(struct wlr_scene_tree *parent,
	struct wlr_buffer *buffer)
{
	struct wlr_scene_buffer *b = wlr_scene_buffer_create(parent, buffer);
	if (b) {
		pic_size_keep(b, buffer);
	}
	return b;
}

void scene_put(struct wlr_scene_buffer **slot, struct wlr_scene_tree *parent,
	struct desk_buf *buffer, int x, int y, void *data)
{
	cairo_surface_flush(buffer->surface);
	prof_upload(buffer->base.width, buffer->base.height);
	if (*slot == NULL) {
		*slot = scene_new(parent, &buffer->base);
	} else {
		scene_swap(*slot, &buffer->base);
	}
	wlr_scene_node_set_position(&(*slot)->node, x, y);
	(*slot)->node.data = data;
	wlr_buffer_drop(&buffer->base);
}

double monotonic_sec(void);
static double clampf_d(double v, double lo, double hi);

/* ---- what a frame costs: DESK_PROFILE=1 ------------------------------
 *
 * A frame-time readout, the kind a game keeps in the corner of the screen,
 * written to the log instead. Every two seconds that anything happened in,
 * one line: how many frames went out and how long the longest gap between
 * two of them was while something was moving, what a frame cost on this
 * thread -- the ticks and painters, then wlroots rendering the scene --
 * and, painter by painter, how often it ran, for how long, and how many
 * bytes of pixels it handed the scene graph to upload. Painters nest, and
 * their times are inclusive. Off, it costs a branch. */
enum prof_what {
	PROF_DESK, PROF_DOCK, PROF_SHELF, PROF_CONSOLE, PROF_POST, PROF_CHROME,
	PROF_SHEET, PROF_SHADOW, PROF_FILM, PROF_THROW, PROF_OTHER, PROF_N,
};
static const char *const PROF_NAME[PROF_N] = {
	"desk", "dock", "shelf", "console", "post", "chrome",
	"sheet", "shadow", "film", "throw", "other",
};
static struct {
	bool on;
	double since;
	int frames, late;
	double frame_ms, frame_max, commit_ms, commit_max, gap_max, last_frame;
	double asked;       /* when a frame was first asked for and not yet made */
	int stack[8];
	double t0[8];
	int depth;
	int calls[PROF_N];
	double ms[PROF_N], bytes[PROF_N];
	int bufs[PROF_N];
} g_prof;

/* DESK_PROFILE_FRAMES=FILE: the same, a frame at a time, for a script to
 * read -- one line a frame: when it began (CLOCK_MONOTONIC), how long since
 * the frame before and since it was asked for, what the ticks cost and
 * what wlroots rendering it cost (on the wall clock and in this thread's
 * own CPU), what this thread spent between the last frame and this one,
 * the pixels handed to the scene since, and the painters that ran since.
 * Kept apart from g_prof, which is cleared at every report. */
static struct {
	FILE *f;
	double last_f0, last_cpu;
	double ms[PROF_N];
	double bytes;
	int bufs;
} g_trace;

static double thread_cpu_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* what the profile measures is what the machine spends: real time, not the
 * desk's own (monotonic_sec, which runs fast in 2D and slow with DESK_SLOW) */
static double real_sec(void);

static void prof_begin(int what)
{
	if (!g_prof.on || g_prof.depth >= 8) {
		return;
	}
	g_prof.stack[g_prof.depth] = what;
	g_prof.t0[g_prof.depth] = real_sec();
	g_prof.depth++;
}

static void prof_end(void)
{
	if (!g_prof.on || g_prof.depth <= 0) {
		return;
	}
	g_prof.depth--;
	int w = g_prof.stack[g_prof.depth];
	double ms = (real_sec() - g_prof.t0[g_prof.depth]) * 1000.0;
	g_prof.calls[w]++;
	g_prof.ms[w] += ms;
	g_trace.ms[w] += ms;
}

/* pixels handed to the scene graph, which it uploads whole */
static void prof_upload(int w, int h)
{
	if (!g_prof.on) {
		return;
	}
	int what = g_prof.depth > 0 ? g_prof.stack[g_prof.depth - 1] : PROF_OTHER;
	g_prof.bytes[what] += (double)w * h * 4.0;
	g_prof.bufs[what]++;
	g_trace.bytes += (double)w * h * 4.0;
	g_trace.bufs++;
}

/* a frame's line (DESK_PROFILE_FRAMES): f0, f1, f2 the frame's start, the
 * end of its ticks and the end of its rendering, c0..c2 the thread's CPU
 * time at each, wait how long since it was asked for (or -1) */
static void trace_frame(double f0, double f1, double f2, double c0, double c1, double c2,
	double wait)
{
	if (!g_trace.f) {
		return;
	}
	fprintf(g_trace.f, "%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.0f,%d,",
		f0, g_trace.last_f0 > 0.0 ? (f0 - g_trace.last_f0) * 1000.0 : 0.0,
		wait >= 0.0 ? wait * 1000.0 : -1.0, (f1 - f0) * 1000.0, (c1 - c0) * 1000.0,
		(f2 - f1) * 1000.0, (c2 - c1) * 1000.0,
		g_trace.last_cpu > 0.0 ? (c0 - g_trace.last_cpu) * 1000.0 : 0.0,
		g_trace.bytes / 1024.0, g_trace.bufs);
	for (int i = 0; i < PROF_N; i++) {
		if (g_trace.ms[i] >= 0.05) {
			fprintf(g_trace.f, "%s:%.2f;", PROF_NAME[i], g_trace.ms[i]);
		}
		g_trace.ms[i] = 0.0;
	}
	fputc('\n', g_trace.f);
	g_trace.bytes = 0.0;
	g_trace.bufs = 0;
	g_trace.last_f0 = f0;
	g_trace.last_cpu = c2;
}

static void prof_report(double now)
{
	if (!g_prof.on) {
		return;
	}
	if (g_trace.f) {
		fflush(g_trace.f);
	}
	if (g_prof.since == 0.0) {
		g_prof.since = now;
		return;
	}
	double span = now - g_prof.since;
	if (span < 2.0) {
		return;
	}
	bool any = g_prof.frames > 0;
	for (int i = 0; i < PROF_N; i++) {
		any = any || g_prof.calls[i] > 0 || g_prof.bufs[i] > 0;
	}
	if (any) {
		char line[1024];
		int n = snprintf(line, sizeof line,
			"profile %.1fs: %d frames, slowest to arrive %.0f ms (%d late); "
			"frame %.2f ms avg %.1f max, of which render %.2f avg %.1f max |",
			span, g_prof.frames, g_prof.gap_max * 1000.0, g_prof.late,
			g_prof.frames ? g_prof.frame_ms / g_prof.frames : 0.0, g_prof.frame_max,
			g_prof.frames ? g_prof.commit_ms / g_prof.frames : 0.0, g_prof.commit_max);
		for (int i = 0; i < PROF_N && n > 0 && (size_t)n < sizeof line; i++) {
			if (g_prof.calls[i] == 0 && g_prof.bufs[i] == 0) {
				continue;
			}
			n += snprintf(line + n, sizeof line - (size_t)n, " %s %dx %.1f ms %.1f MB/%d",
				PROF_NAME[i], g_prof.calls[i], g_prof.ms[i],
				g_prof.bytes[i] / 1048576.0, g_prof.bufs[i]);
		}
		wlr_log(WLR_INFO, "%s", line);
	}
	int depth = g_prof.depth;
	int stack[8];
	double t0[8];
	memcpy(stack, g_prof.stack, sizeof stack);
	memcpy(t0, g_prof.t0, sizeof t0);
	double last = g_prof.last_frame, asked = g_prof.asked;
	memset(&g_prof, 0, sizeof g_prof);
	g_prof.on = true;
	g_prof.since = now;
	g_prof.last_frame = last;
	g_prof.asked = asked;
	g_prof.depth = depth;
	memcpy(g_prof.stack, stack, sizeof stack);
	memcpy(g_prof.t0, t0, sizeof t0);
}

void server_schedule_frames(struct desk_server *server);
static void settings_open(void);
/* more than one screen (see there) */
struct wlr_output *first_output(struct desk_server *server);
static struct wlr_output *screen_at(struct desk_server *server, double x, double y);
static bool screen_is_side(struct desk_server *server, const struct wlr_output *out);
struct wlr_box screen_area(struct desk_server *server, struct wlr_output *out);
static bool pointer_on_side(struct desk_server *server);

static void things_text_wanted(struct desk_server *server, bool wanted);
static void room_update(struct desk_server *server);

/* The pointer's picture: none while fingers are at work. */
static void cursor_image(struct desk_server *server, const char *name)
{
	if (!server->hand_unseen) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, name);
	}
}
struct wlr_output *view_screen(struct desk_toplevel *view);
static void desk_screen_lay(struct desk_server *server, struct wlr_scene_buffer *b,
	struct wlr_fbox src, double x, double y, double dw, double dh, int lx, int ly);

bool shadow_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	(void)buffer;
	(void)sx;
	(void)sy;
	/* a shadow is not a surface: clicks go through it */
	return false;
}

static void shadow_sync_body(struct desk_shadow *sh, struct wlr_scene_tree *tree,
	int w, int h, int screen_x, int screen_y, bool held)
{
	if (!tree || w < 8 || h < 8) {
		return;
	}
	int pad = desk_shadow_sprite_pad();
	/* A sheet being resized has its shadow stretched rather than drawn
	 * again every frame: it is all blur, and nobody can tell -- until it
	 * is stretched a long way from the size it was drawn for, when the
	 * blur at its edge goes hard or thin; then it is drawn again. At
	 * Ultra it is drawn again as it goes. */
	bool far = sh->node && (desk_gfx_level() == DESK_GFX_ULTRA || w > sh->w * 1.25
		|| w * 1.25 < sh->w || h > sh->h * 1.25 || h * 1.25 < sh->h);
	if (!sh->node || ((sh->w != w || sh->h != h) && (!sh->stretch || far))) {
		struct desk_buf *buffer = buf_create(w + pad * 2, h + pad * 2);
		desk_draw_shadow_sprite(buffer->cr, w, h, 7.0, sh->side, sh->amount);
		scene_put(&sh->node, tree, buffer, 0, 0, NULL);
		scene_hold(sh->node);   /* read back under a corner (curl_underlay) */
		sh->w = w;
		sh->h = h;
		sh->node->point_accepts_input = shadow_accepts;
		wlr_scene_node_lower_to_bottom(&sh->node->node);
	}

	double now = monotonic_sec();
	double dt = sh->at > 0.0 ? clampf_d(now - sh->at, 0.0, 0.05) : 0.0;
	sh->at = now;
	if (sh->settled) {
		/* what the sheet did since we last looked, taken out of the
		 * shadow: for that instant the paper is bent between them */
		sh->lx -= (screen_x - sh->px) * 0.62;
		sh->ly -= (screen_y - sh->py) * 0.62;
		sh->lx = clampf_d(sh->lx, -34.0, 34.0);
		sh->ly = clampf_d(sh->ly, -34.0, 34.0);
	}
	sh->px = screen_x;
	sh->py = screen_y;
	sh->settled = 1;

	/* and it flattens out again -- underdamped, so it goes a shade past
	 * and comes back, which is what a sheet of paper does */
	const double K = 210.0, C = 2.0 * 14.49 * 0.52;
	if (dt > 0.0) {
		sh->vx += (-K * sh->lx - C * sh->vx) * dt;
		sh->vy += (-K * sh->ly - C * sh->vy) * dt;
		sh->lx += sh->vx * dt;
		sh->ly += sh->vy * dt;
		double want = held ? 1.0 : 0.0;
		sh->held += (want - sh->held) * clampf_d(dt * 9.0, 0.0, 1.0);
	}

	double ox, oy;
	/* held, the sheet is off the desk, so its shadow goes further out,
	 * spreads, and goes soft */
	desk_shadow_offset(screen_x + w * 0.5, screen_y + h * 0.5,
		(sh->low ? 9.0 : 44.0) + 38.0 * sh->held, &ox, &oy);
	double grow = 1.0 + 0.045 * sh->held;
	int dw = (int)((w + pad * 2) * grow), dh = (int)((h + pad * 2) * grow);
	wlr_scene_buffer_set_dest_size(sh->node, dw, dh);
	wlr_scene_buffer_set_opacity(sh->node, (float)(1.0 - 0.22 * sh->held));
	wlr_scene_node_set_position(&sh->node->node,
		(int)(-pad + ox + sh->lx - (dw - (w + pad * 2)) * 0.5),
		(int)(-pad + oy + sh->ly - (dh - (h + pad * 2)) * 0.5));
}

static void shadow_sync(struct desk_shadow *sh, struct wlr_scene_tree *tree,
	int w, int h, int screen_x, int screen_y, bool held)
{
	prof_begin(PROF_SHADOW);
	shadow_sync_body(sh, tree, w, h, screen_x, screen_y, held);
	prof_end();
}

static bool shadow_restless(const struct desk_shadow *sh)
{
	return fabs(sh->lx) > 0.12 || fabs(sh->ly) > 0.12
		|| fabs(sh->vx) > 1.0 || fabs(sh->vy) > 1.0 || sh->held > 0.004;
}

/* ---- a window, whichever protocol it came by ------------------------ */

static struct wlr_surface *view_surface(const struct desk_toplevel *view)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		return view->xsurface->surface;
	}
#endif
	return view->xdg_toplevel ? view->xdg_toplevel->base->surface : NULL;
}

const char *view_title(const struct desk_toplevel *view)
{
	const char *t = NULL;
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		t = view->xsurface->title ? view->xsurface->title : view->xsurface->class;
	}
#endif
	if (view->xdg_toplevel) {
		t = view->xdg_toplevel->title;
	}
	return t ? t : TR("Window");
}

/* The thing that claims this window (thing.h), if it can be drawn. Its title having
 * changed, only those that go by that are asked. */
static const struct desk_thing *thing_claiming(const struct desk_toplevel *view, bool retitled)
{
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->claims && (!retitled || (*t)->by_title) && (*t)->claims(view)
			&& (!(*t)->ready || (*t)->ready())) {
			return *t;
		}
	}
	return NULL;
}

/* The thing it will be, for what it is to be told as it comes: that it can be drawn
 * is asked when it is here. */
static const struct desk_thing *thing_coming(const struct desk_toplevel *view)
{
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->claims && (*t)->claims(view)) {
			return *t;
		}
	}
	return NULL;
}

/* The thing a program's windows will be, by its desktop file. */
static const struct desk_thing *thing_of_program(const char *desktop)
{
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->desktop && !strcmp((*t)->desktop, desktop) && (!(*t)->ready || (*t)->ready())) {
			return *t;
		}
	}
	return NULL;
}

const struct desk_thing *view_thing(const struct desk_toplevel *view)
{
	return !view->bare && !view->own_frame && !view->fullscreen ? view->thing : NULL;
}

/* Does what it is not do this? */
static bool view_refuses(const struct desk_toplevel *view, unsigned no)
{
	const struct desk_thing *thing = view_thing(view);
	return thing && (thing->no & no);
}

/* How far in from the edge of the sheet the client starts. */
static void view_insets(const struct desk_toplevel *view, int *l, int *t, int *r, int *b)
{
	if (view->bare || view->fullscreen) {
		*l = *t = *r = *b = 0;
		return;
	}
	const struct desk_thing *thing = view_thing(view);
	if (thing) {
		*l = thing->inset_l;
		*t = thing->inset_t;
		*r = thing->inset_r;
		*b = thing->inset_b;
		return;
	}
	if (view->own_frame) {
		/* paper round a program with a heading of its own */
		*l = *r = *b = DESK_MOUNT;
		*t = DESK_MOUNT_TOP;
		return;
	}
	*l = DESK_BORDER;
	*t = DESK_TITLE_H;
	*r = DESK_BORDER;
	*b = DESK_BORDER;
}

static bool view_framed(const struct desk_toplevel *view)
{
	return !view->bare && !view->fullscreen;
}

/* Made another size on purpose -- opened out, the whole screen, a half,
 * its edge pulled: a lifted sheet's bottom edge is not put back down. */
static void view_peek_forget(struct desk_toplevel *view)
{
	view->peek_inner_h = 0;
	view->peek_to_h = 0;
}

/* What of a window is trimmed to the paper: its own surfaces, and not the
 * menus it has open. The scene's node for an xdg surface has the surfaces'
 * tree first, and its menus after; an X window's node is its surfaces'. */
static struct wlr_scene_node *view_content_node(struct desk_toplevel *view)
{
	if (!view->surface_tree) {
		return NULL;
	}
	if (view->xdg_toplevel) {
		if (wl_list_empty(&view->surface_tree->children)) {
			return NULL;
		}
		struct wlr_scene_node *first;
		first = wl_container_of(view->surface_tree->children.next, first, link);
		return first;
	}
	return &view->surface_tree->node;
}

static void view_geometry(const struct desk_toplevel *view, struct wlr_box *geo);

/* Its trim at rest: none -- except a program laid on our paper that draws
 * a shadow round itself all the same (it was told it is tiled, and did
 * not listen), which is trimmed to the window proper, the paper casting
 * the shadow. */
static void view_clip_rest(struct desk_toplevel *view, const struct wlr_box *geo)
{
	struct wlr_scene_node *content = view_content_node(view);
	if (!content) {
		return;
	}
	struct wlr_surface *s = view->xdg_toplevel ? view->xdg_toplevel->base->surface : NULL;
	bool margin = s && geo->width > 0 && geo->height > 0 && (geo->x > 0 || geo->y > 0
		|| s->current.width > geo->width || s->current.height > geo->height);
	if (view->own_frame && !view->bare && !view->fullscreen && margin) {
		struct wlr_box clip = *geo;
		wlr_scene_subsurface_tree_set_clip(content, &clip);
	} else {
		wlr_scene_subsurface_tree_set_clip(content, NULL);
	}
}

/* The client's own idea of its window, in its surface's coordinates: an
 * xdg client may draw a margin of its own round it. */
static void view_geometry(const struct desk_toplevel *view, struct wlr_box *geo)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		*geo = (struct wlr_box){ 0, 0, view->xsurface->width, view->xsurface->height };
		return;
	}
#endif
	*geo = (struct wlr_box){ 0 };
	if (view->xdg_toplevel) {
		wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, geo);
	}
}

/* An X client keeps its own idea of where its window is on the screen, and
 * places its menus by it; tell it whenever the sheet moves. A Wayland client
 * is never told and never needs to be. */
void view_send_position(struct desk_toplevel *view)
{
#ifdef DESK_HAVE_XWAYLAND
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (!xs || !view->scene_tree || view->unmanaged) {
		return;
	}
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	wlr_xwayland_surface_configure(xs, (int16_t)(view->scene_tree->node.x + l),
		(int16_t)(view->scene_tree->node.y + t), xs->width, xs->height);
#else
	(void)view;
#endif
}

/* Ask for a new size, the sheet already being where it is to go. */
static void view_set_size(struct desk_toplevel *view, int w, int h)
{
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
#ifdef DESK_HAVE_XWAYLAND
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (xs) {
		int l = 0, t = 0, r, b;
		view_insets(view, &l, &t, &r, &b);
		int x = view->scene_tree ? view->scene_tree->node.x + l : xs->x;
		int y = view->scene_tree ? view->scene_tree->node.y + t : xs->y;
		wlr_xwayland_surface_configure(xs, (int16_t)x, (int16_t)y,
			(uint16_t)(w > 32767 ? 32767 : w), (uint16_t)(h > 32767 ? 32767 : h));
		return;
	}
#endif
	wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
}

static void view_set_maximized(struct desk_toplevel *view, bool on)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		wlr_xwayland_surface_set_maximized(view->xsurface, on);
		return;
	}
#endif
	wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, on);
}

static void view_set_activated(struct desk_toplevel *view, bool on)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		wlr_xwayland_surface_activate(view->xsurface, on);
		if (on) {
			wlr_xwayland_surface_restack(view->xsurface, NULL, XCB_STACK_MODE_ABOVE);
		}
		return;
	}
#endif
	wlr_xdg_toplevel_set_activated(view->xdg_toplevel, on);
}

/* Does it take the keyboard at all? An X window can say it never does. */
static bool view_takes_keys(const struct desk_toplevel *view)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		return wlr_xwayland_icccm_input_model(view->xsurface) != WLR_ICCCM_INPUT_MODEL_NONE;
	}
#endif
	(void)view;
	return true;
}

void view_close(struct desk_toplevel *view)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		wlr_xwayland_surface_close(view->xsurface);
		return;
	}
#endif
	wlr_xdg_toplevel_send_close(view->xdg_toplevel);
}

static bool gfx_flat(void);

static void view_sync_shadow(struct desk_toplevel *view)
{
	if (view->fullscreen || (view->own_shadow && !view->own_frame) || gfx_flat()
		|| view_refuses(view, THING_NO_SHADOW)) {
		/* nothing under it to fall on -- or one of its own already (laid on our paper,
		 * the paper casts ours) */
		if (view->shadow.node) {
			wlr_scene_node_set_enabled(&view->shadow.node->node, false);
		}
		return;
	}
	if (view->shadow.node) {
		wlr_scene_node_set_enabled(&view->shadow.node->node, true);
	}
	const struct desk_thing *thing = view_thing(view);
	if (view->shadow.amount <= 0.0) {
		desk_paper_lift(view_title(view), &view->shadow.side, &view->shadow.amount);
		if (thing && thing->shadow_amount > 0.0) {
			view->shadow.side = thing->shadow_side;
			view->shadow.amount = thing->shadow_amount;
		}
	}
	bool held = view->server->grabbed == view
		&& view->server->cursor_mode == DESK_CUR_MOVE;
	view->shadow.stretch = view->sizing || view->ua_on;
	view->shadow.low = thing && thing->shadow_low;
	shadow_sync(&view->shadow, view->scene_tree, view->outer_w, view->outer_h,
		view->scene_tree->node.x, view->scene_tree->node.y, held);
}

/* Every sheet on the desk, once a frame, so the ones still settling do. */
static void paper_tick(struct desk_server *server)
{
	struct desk_toplevel *view;
	bool more = false;
	wl_list_for_each(view, &server->toplevels, link) {
		if (!view->scene_tree || !view->scene_tree->node.enabled) {
			continue;
		}
		view_sync_shadow(view);
		more = more || shadow_restless(&view->shadow);
	}
	if (more) {
		server_schedule_frames(server);
	}
}

void focus_toplevel(struct desk_toplevel *toplevel);
void views_above_raise(struct desk_server *server);

/* DESK_SLOW=n: everything that moves takes n times as long, to be
 * watched (or photographed) a frame at a time. */
static double g_time_slow = 1.0;    /* DESK_SLOW */
static double g_time_motion = 1.0;  /* Shell Settings' motion */
static double g_time_real0, g_time_virt0;   /* where the rate last changed */
static bool g_time_rebased;

static double real_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* when this process began, for the log's account of how long the way up
 * took: the first frame, the desk, the lamp */
static double g_start_real;

double monotonic_sec(void)
{
	double t = real_sec();
	double k = g_time_slow * g_time_motion;
	if (!g_time_rebased) {
		return k == 1.0 ? t : t / k;
	}
	return g_time_virt0 + (t - g_time_real0) / k;
}

/* The clock the fingers are timed by: the wall's -- how fast a hand moves is
 * not the desk's to stretch -- except under DESK_SLOW, where a hand filmed
 * slowly has to throw as far as it would have: its speeds are then per
 * second of the desk's time, as everything they are handed on to is. */
static double hand_sec(void)
{
	return real_sec() / g_time_slow;
}

/* How many seconds on the clock a span of the desk's own time is. */
static double real_secs(double virt)
{
	return virt * g_time_slow * g_time_motion;
}

/* How long things take, as the settings have it: their motion, and in 2D
 * a hundredth of it -- everything that moves gets where it is going within
 * a frame, every animation as good as off, without anything that waits on
 * one waiting for ever (and never 0: it is divided by). */
#define GFX_2D_MOTION 0.01
static double motion_of(const struct shell_conf *c)
{
	return c->motion * (desk_gfx_level() == DESK_GFX_2D ? GFX_2D_MOTION : 1.0);
}

/* in 2D: what eases or springs to where it is going is put there at once */
static bool gfx_flat(void)
{
	return desk_gfx_level() == DESK_GFX_2D;
}

/* Everything that moves to take k times as long from now on, carrying on
 * from where it has got to. */
static void motion_set(double k)
{
	if (k == g_time_motion) {
		return;
	}
	double now = monotonic_sec();
	g_time_real0 = real_sec();
	g_time_virt0 = now;
	g_time_rebased = true;
	g_time_motion = k;
}

static double clampf_d(double v, double lo, double hi)
{
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static double smoothstep01(double a, double b, double x)
{
	double t = clampf_d((x - a) / (b - a > 1e-9 ? b - a : 1e-9), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

/* A drawer, which is a different thing from a head: no wind-up, because
 * you have hold of it, and a knock at the end, because it arrives against
 * a stop and the things inside it keep going for a moment. */
#define DRAWER_EASE_Z 0.80   /* (art/sound/bake.py makes the drawer's sounds for these) */
#define DRAWER_EASE_W 12.0
static double drawer_ease(double u)
{
	const double Z = DRAWER_EASE_Z, W = DRAWER_EASE_W;
	u = clampf_d(u, 0.0, 1.0);
	double wd = W * sqrt(1.0 - Z * Z);
	return 1.0 - exp(-Z * W * u) * (cos(wd * u) + (Z * W / wd) * sin(wd * u));
}

/* How far through drawer_ease the drawer is first where it was going -- at its stop, or home:
 * a third of the way through (0.347); the rest is its coming back off it and settling. That is
 * where the stop is heard (2026-11-20, the owner: 引き出しの動きと効果音があっていない -- it was
 * heard at the end, a third of a second after the drawer had got there). */
static double drawer_ease_hit(void)
{
	double wd = DRAWER_EASE_W * sqrt(1.0 - DRAWER_EASE_Z * DRAWER_EASE_Z);
	return (3.14159265358979 - atan(wd / (DRAWER_EASE_Z * DRAWER_EASE_W))) / wd;
}

/* ---- how the camera moves ----------------------------------------
 *
 * The way the drawer does, which was right from the start: it sets off at
 * once, travels, goes a little past where it was going and comes back to
 * it. A spring, underdamped enough to be seen going past -- three per
 * cent, which the film has frames for (ROOM_TILT_U1) -- and settled well
 * before its time is up. From zero it is 60 per cent of the way at a fifth
 * of the time, over the top at half and back by nine tenths.
 *
 * It used to wind up first, leaning back a hair before it went. While the
 * move was shown as a dissolve nobody could see that, nor the arrival --
 * the view at either end was a still -- and what was left was a machine
 * going at one speed. Shown as a camera moving, a lean back reads as a
 * hesitation, and the arrival is the part that says it is a head.
 */
/* camera_bounce: 1 as above; less, more damped (0.4: under half a per cent
 * past); 0, not past at all -- critically damped, a head that simply
 * arrives -- for anyone the swing makes feel unwell */
static double g_bounce = 1.0;

static double look_ease(double u)
{
	const double W = 8.5;
	if (u >= 1.0) {
		return 1.0;
	}
	u = clampf_d(u, 0.0, 1.0);
	if (g_bounce <= 0.0) {
		return 1.0 - exp(-W * u) * (1.0 + W * u);
	}
	double Z = 0.74 + (1.0 - fmin(g_bounce, 1.0)) * 0.20;
	double wd = W * sqrt(1.0 - Z * Z);
	return 1.0 - exp(-Z * W * u) * (cos(wd * u) + (Z * W / wd) * sin(wd * u));
}

static double ease_d(double t)
{
	t = clampf_d(t, 0, 1);
	return t * t * (3.0 - 2.0 * t);
}

/* ---- what the desk sounds like (sound.c) ----
 *
 * Every sound is placed where the thing that makes it is: a little left or
 * right with it on the screen, never all the way to one side. */
