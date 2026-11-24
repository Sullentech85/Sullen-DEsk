/* Part of wm.c: the desk's sounds, sprites and the sheets of the bookcase.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static double snd_pan(const struct desk_server *server, double x)
{
	double w = server->desk_w > 0 ? server->desk_w : 1.0;
	return clampf_d(x / w * 2.0 - 1.0, -1.0, 1.0) * 0.7;
}

int snd_at(const struct desk_server *server, enum desk_sound s, double x,
	double gain)
{
	return desk_sound_play(s, snd_pan(server, x), gain, 0.0);
}

/* Keeping the sound with the picture.
 *
 * What is played now is heard some while later -- the sound server's
 * buffers and the card's, from a few milliseconds to a tenth of a second
 * and more in a virtual machine -- and what is drawn now is seen a frame
 * later. A sound for something the user did (a click) can only follow it;
 * but where a motion is going to end is known while it is on its way, so
 * the sound for its end -- the drawer at its stop, a flap pressed down, a
 * ball of paper landing -- is asked for that much ahead, for the moment
 * the end is seen, and the sound server puts it out early by as long as
 * its way to the speakers takes. */
#define SND_SEEN 0.012   /* drawn, and seen: about a frame */

/* how far ahead, in the desk's time: the way to the speakers, and a frame
 * more, since the tick that notices is a frame apart */
static double snd_lead(void)
{
	return (desk_sound_latency() + 0.025) / (g_time_slow * g_time_motion);
}

/* s, to be heard as what it goes with is seen, `in` of the desk's time
 * from now */
static int snd_in(const struct desk_server *server, enum desk_sound s, double x,
	double gain, double rate, double in)
{
	return desk_sound_play_in(s, snd_pan(server, x), gain, rate,
		real_secs(fmax(in, 0.0)) + SND_SEEN);
}

/* A sound for something that takes `virt` of the desk's time, beginning
 * `in` from now: played as long as it does -- and cut to end as it is seen
 * to, even if it is heard late -- and faster (higher) for a quicker one
 * than designed. */
static int snd_for_in(const struct desk_server *server, enum desk_sound s, double x,
	double gain, double designed, double virt, double in)
{
	in = fmax(in, 0.0);
	double secs = real_secs(virt);
	int h = desk_sound_play_in(s, snd_pan(server, x), gain,
		clampf_d(designed / fmax(secs, 0.05), 0.6, 1.8), real_secs(in) + SND_SEEN);
	desk_sound_stop_after(h, real_secs(in + virt) + SND_SEEN, 0.04);
	return h;
}

static void snd_for(const struct desk_server *server, enum desk_sound s, double x,
	double gain, double designed, double virt)
{
	snd_for_in(server, s, x, gain, designed, virt, 0.0);
}

/* The sound for the end of a motion that began at `start` (the desk's
 * time) and lasts `dur`: asked for once, as soon as the end is near
 * enough. *h is 0 until it has been; whoever starts the motion again
 * forgets it (snd_forget), so that a sound for an end that is not
 * reached after all is not heard. */
static void snd_end(int *h, double start, double dur, enum desk_sound s, double pan,
	double gain, double rate)
{
	if (*h) {
		return;
	}
	double left = start + dur - monotonic_sec();
	if (left <= snd_lead()) {
		*h = desk_sound_play_in(s, pan, gain, rate, real_secs(fmax(left, 0.0)) + SND_SEEN);
		*h = *h ? *h : -1;   /* asked, even if there was nothing to play to */
	}
}

static void snd_forget(int *h)
{
	desk_sound_cancel(*h);
	*h = 0;
}

/* What a sheet at (x, y) is on, as it sounds: 0 the bare desk, 1 a soft
 * mat (leather, linoleum, felt, a photo printed on cloth), 2 the clear one. */
static int snd_under(const struct desk_server *server, double x, double y)
{
	int k = desk_mat_at(server->desk_w, server->desk_h, x, y);
	return k == DESK_MAT_CLEAR ? 2 : k != DESK_MAT_NONE ? 1 : 0;
}

#define SND_NONE DESK_SND_COUNT   /* nothing: what is played for it is nothing */

/* A sheet put down there: on the clear mat, smooth plastic, it settles
 * without a sound (2026-11-15, by ear: "無音でも良い"). */
static enum desk_sound snd_sheet_down(const struct desk_server *server, double x, double y)
{
	static const enum desk_sound on[] = { DESK_SND_SHEET_DOWN, DESK_SND_SHEET_DOWN_SOFT, SND_NONE };
	return on[snd_under(server, x, y)];
}

/* ---- a window slid ----
 *
 * Taken hold of and moved, a window is a sheet drawn across the desk under
 * the hand: heard for as long as it moves, louder and brighter the faster
 * it goes, of what it is going over. Two takes go round under it, one made
 * going slowly and one quickly (art/sound), and the hand's speed fades
 * between them. Once the hand stops they die away, and are let go of a
 * little after (the timer); and when the window is. */
#define SLIDE_SLOW 450.0    /* points a second: what the two were made going */
#define SLIDE_QUICK 1700.0
#define SLIDE_TAU 0.06      /* the speed heard is the hand's over about this long */

static void slide_sound(struct desk_server *server)
{
	static const enum desk_sound slow[] = { DESK_SND_SLIDE_WOOD_SLOW, DESK_SND_SLIDE_SOFT_SLOW,
		DESK_SND_SLIDE_CLEAR_SLOW };   /* (each followed by its quick one) */
	double v = fmin(server->slide_v, 2.0 * SLIDE_QUICK);
	/* as loud as sheet_slide.dsp makes it for the speed, from whichever is nearer */
	double a = pow(v / 1500.0, 0.75);
	double w = clampf_d(log(fmax(v, 1.0) / SLIDE_SLOW) / log(SLIDE_QUICK / SLIDE_SLOW), 0, 1);
	double g[2] = { (1.0 - w) * a / pow(SLIDE_SLOW / 1500.0, 0.75),
		w * a / pow(SLIDE_QUICK / 1500.0, 0.75) };
	int of = (int)slow[snd_under(server, server->slide_x, server->slide_y)];
	if (of != server->slide_of) {
		for (int k = 0; k < 2; k++) {
			desk_sound_stop(server->snd_slide[k], 0.08);
			server->snd_slide[k] = 0;
		}
		server->slide_of = of;
	}
	double pan = snd_pan(server, server->slide_x);
	for (int k = 0; k < 2; k++) {
		if (!server->snd_slide[k] && g[k] > 0.002) {
			/* (in at nothing, and up to where it is: no click) */
			server->snd_slide[k] = desk_sound_loop((enum desk_sound)(of + k), pan, 0.0);
		}
		desk_sound_set(server->snd_slide[k], pan, g[k]);
	}
}

static void slide_end(struct desk_server *server)
{
	for (int k = 0; k < 2; k++) {
		desk_sound_stop(server->snd_slide[k], 0.08);
		server->snd_slide[k] = 0;
	}
	server->slide_of = -1;
	server->slide_v = 0.0;
	server->slide_at = 0.0;
	if (server->slide_timer) {
		wl_event_source_timer_update(server->slide_timer, 0);
	}
}

/* the window moved to where the hand is */
static void slide_motion(struct desk_server *server)
{
	double now = monotonic_sec(), x = server->cursor->x, y = server->cursor->y;
	if (server->slide_at > 0.0) {
		double dt = now - server->slide_at;
		if (dt < 1e-4) {
			return;   /* (two at once: the next one has it) */
		}
		double v = hypot(x - server->slide_x, y - server->slide_y)
			/ desk_px_per_pt(server->desk_w) / dt;
		server->slide_v += (fmin(v, 4.0 * SLIDE_QUICK) - server->slide_v)
			* (1.0 - exp(-dt / SLIDE_TAU));
	}
	server->slide_x = x;
	server->slide_y = y;
	server->slide_at = now;
	slide_sound(server);
	if (server->slide_timer) {
		wl_event_source_timer_update(server->slide_timer, 30);
	}
}

static int slide_timer_cb(void *data)
{
	struct desk_server *server = data;
	double idle = monotonic_sec() - server->slide_at;
	if (server->cursor_mode != DESK_CUR_MOVE || server->slide_at <= 0.0 || idle > 0.4) {
		slide_end(server);
		return 0;
	}
	if (idle > 0.025) {
		/* the hand has stopped, or nearly: dying away */
		server->slide_v *= exp(-0.03 / SLIDE_TAU);
		slide_sound(server);
	}
	wl_event_source_timer_update(server->slide_timer, 30);
	return 0;
}

/* sounds of a sheet asked for ahead: kept, to be forgotten if it turns */
static void sheet_snd(struct desk_sheet *sheet, int h)
{
	int i = sheet->snd_due_i++ % 6;
	sheet->snd_due[i] = h;
}

static void sheet_snd_forget(struct desk_sheet *sheet)
{
	for (int i = 0; i < 6; i++) {
		desk_sound_cancel(sheet->snd_due[i]);
		sheet->snd_due[i] = 0;
	}
	sheet->snd_end = false;
}

void server_schedule_frames(struct desk_server *server)
{
	if (g_prof.on && g_prof.asked == 0.0) {
		g_prof.asked = real_sec();
	}
	struct desk_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

/* The screen a sheet lies on, or is on its way to: the one the middle of
 * where it comes to rest is on (x1, y1, in the paper's coordinates). */
static struct wlr_output *sheet_screen(struct desk_server *server, const struct desk_sheet *s)
{
	int ow = (int)s->hit_w, oh = (int)s->hit_h, pad;
	if (s->fold) {
		desk_fold_extent(s->fold, 1.0, &ow, &oh, &pad);
	}
	return screen_at(server, s->x1 + ow * 0.5 + server->desk_dx,
		s->y1 + oh * 0.5 + server->desk_dy + server->win_dy);
}

/* A place along the front of screen out for a sheet to be put down in:
 * the first not held by another (not self) lying on that screen. */
static int sheet_alloc_slot(struct desk_server *server, struct wlr_output *out,
	const struct desk_sheet *self)
{
	bool used[12] = {0};
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->sheet.phase != SHEET_IDLE && !view->sheet.in_drawer
			&& &view->sheet != self && view->sheet.slot >= 0 && view->sheet.slot < 12
			&& sheet_screen(server, &view->sheet) == out) {
			used[view->sheet.slot] = true;
		}
	}
	for (int i = 0; i < 12; i++) {
		if (!used[i]) {
			return i;
		}
	}
	return 0;
}

struct wlr_box work_area(struct desk_server *server);

/* Where a folded sheet is put down: along the front of the desk -- or of
 * the screen beside it, out, it was folded on -- from the left, a row at a
 * time -- the next row further back, when one is full. */
static void sheet_slot_xy(struct desk_server *server, struct wlr_output *out, int slot,
	int pw, int ph, double *x, double *y)
{
	struct wlr_box wa = screen_area(server, out);
	int per_row = (wa.width - 48) / (pw + 18);
	if (per_row < 1) {
		per_row = 1;
	}
	int col = slot % per_row, row = slot / per_row;
	*x = wa.x + 36 + col * (pw + 18);
	*y = wa.y + wa.height - ph - 18 - row * (ph + 16);
}

/* How finely a thing in motion is drawn: whole at High and above, three
 * quarters across at Medium, half at Low, stretched back to its size by the
 * scene. A sheet folding is on screen for half a second and is mostly a
 * blur to the eye; drawn at half its size it costs a quarter to paint and
 * to hand over -- on an old machine, the difference between thirty frames
 * a second and sixty. What comes to rest is drawn whole again. */
static double g_motion_fine = 1.0;   /* pixels to a point on the desk's screen (motion_res) */

static double motion_res(void)
{
	enum desk_gfx g = desk_gfx_level();
	if (g >= DESK_GFX_ULTRA) {
		return 1.0;   /* (2026-10-10: Ultra is drawn whole, moving or not) */
	}
	double k = g == DESK_GFX_HIGH ? 1.0 : g == DESK_GFX_MEDIUM ? 0.75 : 0.5;
	/* On a fine screen -- more than a pixel and a half to a point, a Retina one, a Surface's 2736
	 * by 1824 -- what moves needs no more than a point and a quarter's worth of pixels to look as
	 * sharp as an ordinary screen's whole, and that is all of it there is time for: a Surface Pro
	 * at High drew a binder turning at five times an ordinary screen's pixels (2026-11-12). */
	if (g_motion_fine > 1.5) {
		k = fmin(k, fmax(0.4, 1.25 * k / g_motion_fine));
	}
	return k;
}

static void sheet_present_body(struct desk_server *server, struct desk_sheet *sheet, double t, double x, double y)
{
	if (!sheet->src || !sheet->fold) {
		return;
	}
	int ow, oh, pad;
	desk_fold_extent(sheet->fold, t, &ow, &oh, &pad);
	int fw = ow + pad * 2, fh = oh + pad * 2;
	/* in motion, drawn smaller and stretched; at rest, whole */
	double k = t > 0.0 && t < 1.0 ? motion_res() : 1.0;
	int bw = (int)ceil(fw * k), bh = (int)ceil(fh * k);
	struct desk_buf *buffer = buf_create(bw, bh);
	if (bw != fw || bh != fh) {
		cairo_scale(buffer->cr, (double)bw / fw, (double)bh / fh);
	}
	cairo_translate(buffer->cr, pad, pad);
	/* the sheet is painted off-screen, so tell the shadow where it lands */
	desk_set_paint_origin(x - pad, y - pad);
	desk_fold_draw(buffer->cr, sheet->fold, t);
	desk_set_paint_origin(0, 0);
	/* lying folded it is under the windows; folding, opening out, over
	 * them, as it is held up off the desk */
	struct wlr_scene_tree *in = sheet->phase == SHEET_PARKED ? server->layer_folded
		: server->layer_parked;
	if (!sheet->tree) {
		sheet->tree = wlr_scene_tree_create(in);
		sheet->tree->node.data = (void *)(uintptr_t)DESK_ROLE_SHEET;
	} else if (sheet->tree->node.parent != in) {
		wlr_scene_node_reparent(&sheet->tree->node, in);
	}
	wlr_scene_node_set_enabled(&sheet->tree->node, true);
	scene_put(&sheet->buf, sheet->tree, buffer, 0, 0, (void *)(uintptr_t)DESK_ROLE_SHEET);
	wlr_scene_buffer_set_dest_size(sheet->buf, fw, fh);
	wlr_scene_node_set_position(&sheet->tree->node, (int)(x - pad), (int)(y - pad));
	wlr_scene_node_raise_to_top(&sheet->tree->node);
	sheet->hit_x = x;
	sheet->hit_y = y;
	sheet->hit_w = ow;
	sheet->hit_h = oh;
}

static void sheet_present(struct desk_server *server, struct desk_sheet *sheet, double t, double x, double y)
{
	prof_begin(PROF_SHEET);
	sheet_present_body(server, sheet, t, x, y);
	prof_end();
}

static void drawer_put_in(struct desk_server *server, struct desk_sheet *sheet);

/* Open: the window back where it was, the folded paper gone, and the keys
 * to it -- unless it was gone past while it opened. */
static void sheet_opened(struct desk_sheet *sheet)
{
	sheet->phase = SHEET_IDLE;
	if (sheet->tree) {
		wlr_scene_node_set_enabled(&sheet->tree->node, false);
	}
	if (sheet->live) {
		wlr_scene_node_set_enabled(&sheet->live->node, true);
		wlr_scene_node_raise_to_top(&sheet->live->node);
	}
	if (sheet->src) {
		cairo_surface_destroy(sheet->src);
		sheet->src = NULL;
	}
	desk_fold_free(sheet->fold);
	sheet->fold = NULL;
	sheet->by_hand = false;
	if (sheet->hit && !sheet->passed) {
		focus_toplevel(sheet->hit);
	}
	sheet->passed = false;
}

/* A folding sheet let go of falls on the way it was going: of all the way, a second, a second */
#define SHEET_FALLS 2.6

static void sheet_tick_one(struct desk_server *server, struct desk_sheet *sheet)
{
	if (sheet->phase != SHEET_FOLDING && sheet->phase != SHEET_UNFOLDING) {
		return;
	}
	if (sheet->hand_held || sheet->hand_free) {
		/* as far as the fingers have it; or let go of, falling the way it was going */
		double now = monotonic_sec();
		if (sheet->hand_free) {
			double dt = clampf_d(now - sheet->hand_at, 0.0, 0.05);
			sheet->hand_at = now;
			sheet->hand_v += (sheet->phase == SHEET_FOLDING ? SHEET_FALLS : -SHEET_FALLS) * dt;
			sheet->hand_t = clampf_d(sheet->hand_t + sheet->hand_v * dt, 0.0, 1.0);
			if (sheet->hand_t <= 0.0 || sheet->hand_t >= 1.0) {
				sheet->hand_free = false;
			}
		}
		double held = sheet->phase == SHEET_FOLDING ? sheet->hand_t : 1.0 - sheet->hand_t;
		sheet->start = now - held * sheet->dur;
	}
	double u = (monotonic_sec() - sheet->start) / sheet->dur;
	u = clampf_d(u, 0, 1);
	double t = (sheet->phase == SHEET_FOLDING) ? u : (1.0 - u);
	double travel = ease_d(clampf_d((t - 0.55) / 0.45, 0, 1));
	double x = sheet->x0 + (sheet->x1 - sheet->x0) * travel;
	double y = sheet->y0 + (sheet->y1 - sheet->y0) * travel;
	if (sheet->by_hand && sheet->fold) {
		/* (what has been folded down on stays where it was) */
		y += desk_fold_hand_y(sheet->fold, t) * (1.0 - travel);
	}
	sheet_present(server, sheet, t, x, y);
	if (sheet->fold) {
		/* A flap landing is pressed down, the next goes over; opening
		 * out, each crease lets go and its flap lifts away -- each asked
		 * for ahead, for the moment it is seen (snd_lead): the stage the
		 * sheet will be at by then, and when it got there. */
		int n = desk_fold_stages(sheet->fold);
		double now = monotonic_sec();
		double ua = sheet->hand_held ? u
			: clampf_d((now + snd_lead() - sheet->start) / sheet->dur, 0, 1);
		int k = desk_fold_stage(sheet->fold, sheet->phase == SHEET_FOLDING ? ua : 1.0 - ua);
		double done = desk_fold_done(sheet->fold);
		double stage = sheet->dur * done / (n > 0 ? n : 1);
		double mid = x + sheet->hit_w * 0.5;
		if (sheet->phase == SHEET_FOLDING) {
			for (int j = sheet->snd_k + 1; j <= k && n > 0; j++) {
				/* the j-th flap down, j/n of the way to done */
				double in = sheet->start + sheet->dur * done * j / n - now;
				sheet_snd(sheet, snd_in(server, DESK_SND_PAPER_PRESS, mid, 1.0, 0.0, in));
				if (j < n) {
					sheet_snd(sheet, snd_for_in(server, DESK_SND_PAPER_SWING, mid, 0.9,
						0.40, stage, in));
				}
			}
			if (ua >= 1.0 && !sheet->snd_end) {
				/* down where it is to lie, or into the drawer */
				double in = sheet->start + sheet->dur - now;
				sheet->snd_end = true;
				sheet_snd(sheet, sheet->to_drawer
					? snd_in(server, DESK_SND_FELT_DROP, server->desk_w * 0.5, 0.6, 0.0, in)
					: snd_in(server, DESK_SND_PAPER_SETTLE, sheet->x1 + sheet->hit_w * 0.5,
						1.0, 0.0, in));
			}
		} else {
			for (int j = sheet->snd_k; j > k && n > 0; j--) {
				/* the j-th crease letting go, as the sheet opens back
				 * past it */
				double in = sheet->start + sheet->dur * (1.0 - done * j / n) - now;
				sheet_snd(sheet, snd_in(server, DESK_SND_PAPER_UNPRESS, mid, 1.0, 0.0, in));
				sheet_snd(sheet, snd_for_in(server, DESK_SND_PAPER_SWING, mid, 0.8, 0.40,
					stage, in));
			}
		}
		sheet->snd_k = k;
	}
	if (u >= 1.0 && !sheet->hand_held) {
		sheet->hand_free = false;
		if (sheet->phase == SHEET_FOLDING) {
			sheet->phase = SHEET_PARKED;
			sheet_present(server, sheet, 1.0, sheet->x1, sheet->y1);
			/* (heard as it lands: asked for ahead, above) */
			if (sheet->to_drawer) {
				sheet->to_drawer = false;
				drawer_put_in(server, sheet);
			}
		} else {
			sheet_opened(sheet);
		}
	}
}

static void folds_tick(struct desk_server *server)
{
	bool busy = false;
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->sheet.phase == SHEET_FOLDING || view->sheet.phase == SHEET_UNFOLDING) {
			sheet_tick_one(server, &view->sheet);
			busy = true;
		}
	}
	if (busy) {
		server_schedule_frames(server);
	}
}

static void room_layout(struct desk_server *server);
static void desktop_tick(struct desk_server *server);
static void overlay_place(struct desk_server *server);
static void desk_resize_to(struct desk_server *server, int w, int h);

/* Draw one sprite again. draw paints in screen coordinates, exactly as it
 * would into a buffer the size of the screen; box, when the caller knows
 * where the drawing lands, is that, and the sprite is made that size and
 * drawn straight into. Without a box it is drawn into a screen-sized
 * scratch and cropped to what it covers -- the right thing for something
 * that changes rarely and whose extent is easier to find than to say.
 * Above the overlay size the sprite is drawn smaller and blown back up by
 * the scene graph, the same as every other overlay. */
typedef void (*sprite_fn)(cairo_t *cr, int w, int h, void *data);

/* A sprite's picture for a box it is known to land in, drawn and not put
 * anywhere: where it goes on the screen (x, y), how big it is shown there
 * (dw, dh), and the overlay's pixel its first pixel is (ox, oy). NULL for
 * a box with nothing in it. */
static struct desk_buf *sprite_picture(struct desk_server *server, const double *box,
	sprite_fn draw, void *data, int *x, int *y, int *dw, int *dh, int *ox, int *oy)
{
	int w = server->desk_w, h = server->desk_h;
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	double fx = (double)ow / w, fy = (double)oh / h;
	if (server->sprite_shrink > 0.0 && server->sprite_shrink < 1.0) {
		/* in motion: smaller, and blown back up by the scene */
		fx *= server->sprite_shrink;
		fy *= server->sprite_shrink;
		ow = (int)ceil(ow * server->sprite_shrink);
		oh = (int)ceil(oh * server->sprite_shrink);
	}
	int x0 = (int)floor(box[0] * fx);
	int y0 = (int)floor(box[1] * fy);
	int x1 = (int)ceil((box[0] + box[2]) * fx);
	int y1 = (int)ceil((box[1] + box[3]) * fy);
	x0 = x0 < 0 ? 0 : x0;
	y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > ow ? ow : x1;
	y1 = y1 > oh ? oh : y1;
	int bw = x1 - x0, bh = y1 - y0;
	if (bw < 1 || bh < 1) {
		return NULL;
	}
	struct desk_buf *b = buf_create(bw, bh);
	cairo_save(b->cr);
	cairo_translate(b->cr, -x0, -y0);
	cairo_scale(b->cr, fx, fy);
	draw(b->cr, w, h, data);
	cairo_restore(b->cr);
	*x = (int)floor(x0 / fx);
	*y = (int)floor(y0 / fy);
	*dw = (int)ceil(bw / fx);
	*dh = (int)ceil(bh / fy);
	*ox = x0;
	*oy = y0;
	return b;
}

static void sprite_paint(struct desk_server *server, struct desk_sprite *sp,
	struct wlr_scene_tree *parent, const double *box, sprite_fn draw, void *data)
{
	int w = server->desk_w, h = server->desk_h;
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	double fx = (double)ow / w, fy = (double)oh / h;
	int x0, y0, bw, bh;
	struct desk_buf *b = NULL;
	if (box) {
		b = sprite_picture(server, box, draw, data, &sp->x, &sp->y, &sp->dw, &sp->dh, &sp->ox,
			&sp->oy);
		if (!b) {
			if (sp->node) {
				wlr_scene_node_set_enabled(&sp->node->node, false);
			}
			sp->drawn = false;
			return;
		}
		sp->bw = b->base.width;
		sp->bh = b->base.height;
		scene_put(&sp->node, parent, b, sp->x, sp->y, NULL);
		wlr_scene_buffer_set_dest_size(sp->node, sp->dw, sp->dh);
		wlr_scene_node_set_enabled(&sp->node->node, true);
		sp->drawn = true;
		return;
	} else {
		cairo_surface_t *scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ow, oh);
		cairo_t *cr = cairo_create(scratch);
		cairo_scale(cr, fx, fy);
		draw(cr, w, h, data);
		cairo_destroy(cr);
		cairo_surface_flush(scratch);
		const unsigned char *px = cairo_image_surface_get_data(scratch);
		int stride = cairo_image_surface_get_stride(scratch);
		int ax0 = ow, ay0 = oh, ax1 = -1, ay1 = -1;
		for (int y = 0; y < oh; y++) {
			const uint32_t *row = (const uint32_t *)(px + (size_t)y * stride);
			for (int x = 0; x < ow; x++) {
				if (row[x] >> 24) {
					ax0 = x < ax0 ? x : ax0;
					ax1 = x > ax1 ? x : ax1;
					ay0 = y < ay0 ? y : ay0;
					ay1 = y;
				}
			}
		}
		if (ax1 < 0) {
			/* nothing to show yet, or at all */
			if (sp->node) {
				wlr_scene_node_set_enabled(&sp->node->node, false);
			}
			sp->drawn = false;
			cairo_surface_destroy(scratch);
			return;
		}
		x0 = ax0;
		y0 = ay0;
		bw = ax1 - ax0 + 1;
		bh = ay1 - ay0 + 1;
		b = buf_create(bw, bh);
		cairo_set_operator(b->cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(b->cr, scratch, -x0, -y0);
		cairo_paint(b->cr);
		cairo_set_operator(b->cr, CAIRO_OPERATOR_OVER);
		cairo_surface_destroy(scratch);
	}
	sp->x = (int)floor(x0 / fx);
	sp->y = (int)floor(y0 / fy);
	sp->dw = (int)ceil(bw / fx);
	sp->dh = (int)ceil(bh / fy);
	sp->ox = x0;
	sp->oy = y0;
	sp->bw = bw;
	sp->bh = bh;
	scene_put(&sp->node, parent, b, sp->x, sp->y, NULL);
	/* every time: at 1:1 too, since wlroots keeps the last size given
	 * across new buffers, and the overlay can come back to 1:1 */
	wlr_scene_buffer_set_dest_size(sp->node, sp->dw, sp->dh);
	wlr_scene_node_set_enabled(&sp->node->node, true);
	sp->drawn = true;
}

/* A sprite drawn on another's pixels: its picture a window of the other's
 * -- the overlay's same pixels, drawn the same way -- and shown through a
 * source box that puts each of its pixels where the other shows the same
 * one. Two sprites each placed by itself agree to within a pixel where
 * the overlay is drawn smaller than the screen (Low, a wide screen), which
 * is nothing for a thing laid over another and a seam for a patch of it:
 * the hand on the book is a patch of the book. The box is clipped to the
 * picture under it. */
static void sprite_paint_on(struct desk_server *server, struct desk_sprite *sp,
	struct wlr_scene_tree *parent, const struct desk_sprite *under, const double *box,
	sprite_fn draw, void *data)
{
	int w = server->desk_w, h = server->desk_h;
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	double fx = (double)ow / w, fy = (double)oh / h;
	int x0 = (int)floor(box[0] * fx), y0 = (int)floor(box[1] * fy);
	int x1 = (int)ceil((box[0] + box[2]) * fx), y1 = (int)ceil((box[1] + box[3]) * fy);
	x0 = x0 < under->ox ? under->ox : x0;
	y0 = y0 < under->oy ? under->oy : y0;
	x1 = x1 > under->ox + under->bw ? under->ox + under->bw : x1;
	y1 = y1 > under->oy + under->bh ? under->oy + under->bh : y1;
	/* the picture under it shows the overlay's pixel p at
	 * under->x + (p - under->ox) * sx on the screen */
	double sx = under->bw > 0 ? (double)under->dw / under->bw : 1.0;
	double sy = under->bh > 0 ? (double)under->dh / under->bh : 1.0;
	double lx0 = under->x + (x0 - under->ox) * sx, lx1 = under->x + (x1 - under->ox) * sx;
	double ly0 = under->y + (y0 - under->oy) * sy, ly1 = under->y + (y1 - under->oy) * sy;
	/* shown on whole pixels of the screen, inside the picture */
	int X = (int)ceil(lx0 - 1e-6), X1 = (int)floor(lx1 + 1e-6);
	int Y = (int)ceil(ly0 - 1e-6), Y1 = (int)floor(ly1 + 1e-6);
	if (!under->drawn || x1 <= x0 || y1 <= y0 || X1 <= X || Y1 <= Y) {
		if (sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, false);
		}
		sp->drawn = false;
		return;
	}
	struct desk_buf *b = buf_create(x1 - x0, y1 - y0);
	cairo_save(b->cr);
	cairo_translate(b->cr, -x0, -y0);
	cairo_scale(b->cr, fx, fy);
	draw(b->cr, w, h, data);
	cairo_restore(b->cr);
	struct wlr_fbox src = { (X - lx0) / sx, (Y - ly0) / sy, (X1 - X) / sx, (Y1 - Y) / sy };
	scene_put(&sp->node, parent, b, X, Y, NULL);
	scene_source(sp->node, &src);
	wlr_scene_buffer_set_dest_size(sp->node, X1 - X, Y1 - Y);
	wlr_scene_node_set_enabled(&sp->node->node, true);
	sp->x = X;
	sp->y = Y;
	sp->dw = X1 - X;
	sp->dh = Y1 - Y;
	sp->ox = x0;
	sp->oy = y0;
	sp->bw = x1 - x0;
	sp->bh = y1 - y0;
	sp->drawn = true;
}

static void drawer_compose(struct desk_server *server);

/* Is the bookcase in the frame at all? */
static bool shelf_up(const struct desk_server *server)
{
	return server->shelf_t > 0.0005 || server->shelf_moving;
}

/* The move is a flipbook on two scene nodes; the settled view -- the shelf
 * with its printed labels, or the console with its needles -- is one cairo
 * buffer repainted only when it changes. */
static void film_ensure_body(struct desk_server *server, int which,
	struct wlr_scene_tree *tree)
{
	if (server->film_a[which]) {
		return;
	}
	cairo_surface_t *strip;
	int frames, fw, fh;
	if (!desk_tilt_film(server->desk_w, server->desk_h, which,
			&strip, &frames, &fw, &fh)) {
		return;
	}
	int sw = cairo_image_surface_get_width(strip);
	int sh = cairo_image_surface_get_height(strip);
	struct desk_buf *buf = buf_create(sw, sh);
	cairo_set_source_surface(buf->cr, strip, 0, 0);
	cairo_paint(buf->cr);
	cairo_surface_flush(buf->surface);
	prof_upload(sw, sh);
	server->film_frames[which] = frames;
	server->film_fw[which] = fw;
	server->film_fh[which] = fh;
	server->film_a[which] = scene_new(tree, &buf->base);
	server->film_b[which] = scene_new(tree, &buf->base);
	wlr_buffer_drop(&buf->base);
	wlr_scene_node_set_enabled(&server->film_a[which]->node, false);
	wlr_scene_node_set_enabled(&server->film_b[which]->node, false);
	/* Under everything else in the view's layer, the later keyframe over
	 * the earlier: the settled view fades in over the film (film_play).
	 * A film is traced after its view, so it can come after the view's own
	 * nodes were made, which would have put it on top of them. */
	wlr_scene_node_lower_to_bottom(&server->film_b[which]->node);
	wlr_scene_node_lower_to_bottom(&server->film_a[which]->node);
}

static void film_ensure(struct desk_server *server, int which,
	struct wlr_scene_tree *tree)
{
	if (server->film_a[which]) {
		return;
	}
	prof_begin(PROF_FILM);
	film_ensure_body(server, which, tree);
	prof_end();
}

/* One keyframe, laid where it stands in for the view at t. */
static void film_place(struct desk_server *server, int which,
	struct wlr_scene_buffer *node, int frame, double t, double opacity)
{
	struct wlr_fbox src = {
		0.0, (double)frame * server->film_fh[which],
		(double)server->film_fw[which], (double)server->film_fh[which],
	};
	double box[4];
	if (desk_tilt_place(server->desk_w, server->desk_h, which, frame, t, box)) {
		int x0 = (int)floor(box[0]), y0 = (int)floor(box[1]);
		int x1 = (int)ceil(box[0] + box[2]), y1 = (int)ceil(box[1] + box[3]);
		desk_screen_lay(server, node, src, x0, y0, x1 - x0, y1 - y0, 0, 0);
	} else {
		desk_screen_lay(server, node, src, 0, 0, server->desk_w, server->desk_h, 0, 0);
	}
	wlr_scene_buffer_set_opacity(node, (float)opacity);
	wlr_scene_node_set_enabled(&node->node, true);
}

static void film_hide(struct desk_server *server, int which)
{
	if (server->film_a[which]) {
		wlr_scene_node_set_enabled(&server->film_a[which]->node, false);
		wlr_scene_node_set_enabled(&server->film_b[which]->node, false);
	}
}

/* A move's film at t. The keyframes either side of it are laid where they
 * stand in for the view there, the later fading in over the earlier; from
 * nine tenths of the way the settled view fades in over them (`over`),
 * moved to where the view at t has it (dx, dy) -- which, as the move goes
 * past the end and comes back, is what shows it doing so. The film stays
 * under the settled view until the move is over, so that what shows round
 * the edge of it, moved, is the room and not the desk. */
static void film_play(struct desk_server *server, int which, double t,
	double *over, int *dx, int *dy)
{
	double in = clampf_d(t / 0.10, 0.0, 1.0);
	*over = clampf_d((t - 0.90) / 0.0995, 0.0, 1.0);
	int frame;
	double mix;
	desk_tilt_at(t, server->film_frames[which], &frame, &mix);
	if (server->soft_render && desk_gfx_level() <= DESK_GFX_LOW) {
		/* Put together on the processor, at Low: the nearer keyframe
		 * alone, not the two faded one over the other. Each is the whole
		 * screen scaled, which pixman does by the pixel; the two were most
		 * of what a move cost a machine with no GPU to do it. */
		film_place(server, which, server->film_a[which], mix > 0.5 ? frame + 1 : frame, t, in);
		wlr_scene_node_set_enabled(&server->film_b[which]->node, false);
	} else {
		film_place(server, which, server->film_a[which], frame, t, in);
		film_place(server, which, server->film_b[which], frame + 1, t, in * mix);
	}
	*dx = *dy = 0;
	if (*over > 0.0) {
		double fx, fy;
		desk_tilt_offset(server->desk_w, server->desk_h, which, t, &fx, &fy);
		*dx = (int)lround(fx);
		*dy = (int)lround(fy);
	}
}

/* a key out of whatever a layer is drawn from */
static uint64_t key_add(uint64_t h, const void *p, size_t n)
{
	const unsigned char *c = p;
	for (size_t i = 0; i < n; i++) {
		h = (h ^ c[i]) * 1099511628211ull;
	}
	return h;
}

struct sh_args {
	struct desk_server *server;
	int hover, taken, gadget, part;
	double ht;
	const double *box;
	time_t now;
};

/* What today puts on the bookcase -- on the 31st of October, things on
 * its lower board -- told to the painter, and returned for the keys of
 * what is painted from it. */
static unsigned shelf_dress_today(void)
{
	unsigned dc = desk_occasion_today() & DESK_OCC_HALLOWEEN ? DESK_DECOR_HALLOWEEN : 0;
	desk_shelf_dress(dc);
	return dc;
}

static void sh_case_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	desk_draw_shelf(cr, w, h, -1, 0.0, a->taken);
}

/* The same painter, clipped to the rectangle the binder slides in: the
 * case under it is opaque, so this is the whole view there, to the pixel. */
static void sh_hover_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	cairo_rectangle(cr, a->box[0], a->box[1], a->box[2], a->box[3]);
	cairo_clip(cr);
	desk_draw_shelf(cr, w, h, a->hover, a->ht, a->taken);
}

static void sh_gadget_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	desk_draw_shelf_gadget(cr, w, h, a->gadget, a->now);
}

/* The binder as it lies open. Coming down off the shelf it is the same
 * picture moved and faded (sh_binder_follow), not a new one every frame. */
static bool bnc_in_drawer(struct desk_server *server, int idx);

static void book_look(const struct desk_server *server, struct desk_book_look *lk);
