/* Part of draw.c: the fanned sheets and the forms of Shell Settings.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

static void form_font(cairo_t *cr, double size, bool bold)
{
	desk_font_select(cr, DESK_FONT_UI, NULL, bold, false);
	cairo_set_font_size(cr, size);
}

static void form_hand(cairo_t *cr, const char *s, double size)
{
	desk_font_select(cr, DESK_FONT_HAND, s, false, false);
	cairo_set_font_size(cr, size);
}

/* ---- a fan of samples (DESK_SWATCH_*) ----
 *
 * What the desk is made of is chosen as at a joiner's: from a fan of
 * samples riveted at one end, each a strip of the desk in that wood (or that
 * stain) with its name stamped in gold along it. The one chosen is taken
 * out of the fan, a little way along its length, and lies over the rest; the
 * one under the hand comes out a little; the name of the one chosen is on a
 * brass plate beside it. As the v0.2 page's fan has them. */

#define FAN_CW 38.0          /* a sample's width */
#define FAN_CH 122.0         /* and length */
#define FAN_PIN 13.0         /* its rivet, up from its end */
#define FAN_LIFT 20.0        /* room left over the fan */
#define FAN_PART (11.0 * M_PI / 180.0)   /* the fan parted either side of the chosen one */
#define FAN_RAISE 1.04       /* the chosen one, up off the rest toward the eye */
#define FAN_PART_HOVER (3.5 * M_PI / 180.0)   /* and of the one under the hand */
#define FAN_PLATE_H 30.0
#define FAN_BOX 30.0         /* the box before the plate, ticked */
#define SIGN_LINE_W 300.0    /* the line an order is signed on */

/* the angle between two samples, n of them */
static double fan_step(int n)
{
	return n > 1 ? fmin(12.5, 63.0 / (n - 1)) * M_PI / 180.0 : 0.0;
}

static double fan_angle(int n, int k)
{
	return (k - (n - 1) * 0.5) * fan_step(n);
}

static void form_tick(cairo_t *cr, double bx, double by);

/* how far either side of its rivet the fan reaches */
static double fan_half_width(int n)
{
	double a = fan_angle(n, n - 1);
	a += FAN_PART + FAN_PART_HOVER;
	return (FAN_CH - FAN_PIN) * FAN_RAISE * sin(a) + FAN_CW * 0.5 * cos(a) + 6.0;
}

/* How a fan lies: its row's samples, the one under the hand, and the one
 * chosen before the chosen one and how far they have changed places (t, 1
 * done). */
struct fan_state {
	const struct desk_form_row *r;
	int n, hover, was;
	double t;
};

/* Is sample k in the fan? Not a stain that is not made for the wood
 * (desk_stain_offered): the fan of colours on ebony is of the three it is
 * made in, closed up. */
static bool fan_has(const struct desk_form_row *r, int k)
{
	return r->swatch != DESK_SWATCH_STAIN || desk_stain_offered(r->swatch_with, k);
}

/* the angle sample k lies at, among those in the fan: fewer of them opened
 * wider, as far as the whole fan would be (but not so far apart that they
 * are not a fan), so that the one beside the one out of it still shows */
static double fan_angle_of(const struct fan_state *fs, int k)
{
	int at = 0, m = 0;
	for (int j = 0; j < fs->n; j++) {
		if (fan_has(fs->r, j)) {
			at += j < k;
			m++;
		}
	}
	if (m == fs->n || m < 2) {
		return fan_angle(m, at);
	}
	double step = fmin(fan_step(fs->n) * (fs->n - 1) / (m - 1), 20.0 * M_PI / 180.0);
	return (at - (m - 1) * 0.5) * step;
}

static struct fan_state fan_state_of(const struct desk_shell_form *f, int i, int n)
{
	bool moving = f->fan_row == i && f->fan_t > 0.0 && f->fan_t < 1.0;
	return (struct fan_state){ &f->rows[i], n, i == f->hover ? f->hover_opt : -1,
		moving ? f->fan_was : -1, moving ? f->fan_t : 1.0 };
}

/* which way sample k is turned for the fan to part at sample c */
static double fan_side(int c, int k)
{
	return c < 0 || k == c ? 0.0 : k < c ? -1.0 : 1.0;
}

/* How far sample k is turned past where it lies: every sample stays on its
 * rivet, as on a real fan, and the fan is parted either side of the one
 * chosen -- closing where the one before it was, opening at the new one --
 * and a little either side of the one under the hand. */
static double fan_swing(const struct fan_state *fs, int k)
{
	double e = 1.0 - pow(1.0 - clampf(fs->t, 0.0, 1.0), 3.0);
	return FAN_PART * (fan_side(fs->r->value, k) * e + fan_side(fs->was, k) * (1.0 - e))
		+ FAN_PART_HOVER * fan_side(fs->hover, k);
}

/* How far up off the rest sample k is, 0 to 1: the one chosen lifted on its
 * rivet as it is turned up, the one before it laid down again. */
static double fan_raise(const struct fan_state *fs, int k)
{
	double e = 1.0 - pow(1.0 - clampf(fs->t, 0.0, 1.0), 3.0);
	return k == fs->r->value ? e : k == fs->was ? 1.0 - e : 0.0;
}

/* A sample's outline, the rivet at 0, 0 and its length up from there:
 * its tip's corners rounded a little, its end round. */
static void fan_chip_path(cairo_t *cr)
{
	double hw = FAN_CW * 0.5, top = -(FAN_CH - FAN_PIN), r = 6.0, end = FAN_PIN - hw;
	cairo_new_path(cr);
	cairo_move_to(cr, -hw, end);
	cairo_line_to(cr, -hw, top + r);
	cairo_arc(cr, -hw + r, top + r, r, M_PI, 1.5 * M_PI);
	cairo_line_to(cr, hw - r, top);
	cairo_arc(cr, hw - r, top + r, r, 1.5 * M_PI, 2.0 * M_PI);
	cairo_line_to(cr, hw, end);
	cairo_arc(cr, 0.0, end, hw, 0.0, M_PI);
	cairo_close_path(cr);
}

/* Is (x, y), from the rivet, on sample k as it lies? */
static bool fan_chip_at(const struct fan_state *fs, int k, double x, double y)
{
	if (!fan_has(fs->r, k)) {
		return false;
	}
	double a = fan_angle_of(fs, k) + fan_swing(fs, k);
	double sc = 1.0 + (FAN_RAISE - 1.0) * fan_raise(fs, k);
	double u = (x * cos(a) + y * sin(a)) / sc, v = (-x * sin(a) + y * cos(a)) / sc;
	double hw = FAN_CW * 0.5, end = FAN_PIN - hw;
	if (fabs(u) <= hw && v >= -(FAN_CH - FAN_PIN) && v <= end) {
		return true;
	}
	return hypot(u, v - end) <= hw;
}

/* Its name, stamped in gold down it from the tip: a word in letters turned
 * to run along it; one in Chinese, Japanese or Korean, a character under
 * another, upright, as such a word is written down a page. A long one is
 * stamped smaller, never cut off. */
static void fan_chip_tag(cairo_t *cr, const char *name)
{
	double hw = FAN_CW * 0.5, top = -(FAN_CH - FAN_PIN), room = FAN_CH - 50.0;
	bool cjk = false;
	for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
		cjk = cjk || *p >= 0xe3;
	}
	char word[96];
	size_t len = 0;
	for (const char *p = name; *p && len + 1 < sizeof word; p++) {
		word[len++] = cjk ? *p : (char)toupper((unsigned char)*p);
	}
	word[len] = '\0';
	cairo_save(cr);
	desk_font_select(cr, DESK_FONT_UI, word, true, false);
	double size = cjk ? 11.0 : 10.0, step = 12.5, space = 1.4, run = 0.0;
	cairo_set_font_size(cr, size);
	char ch[8];
	int count = 0;
	for (const char *p = word; *p;) {
		p += cal_letter(p, ch);
		cairo_text_extents_t e;
		cairo_text_extents(cr, ch, &e);
		run += e.x_advance + space;
		count++;
	}
	double fit = cjk ? (count * step > room ? room / (count * step) : 1.0)
		: (run > room ? room / run : 1.0);
	if (fit < 1.0) {
		size *= fmax(fit, 0.6);
		step *= fit;
		space *= fit;
		cairo_set_font_size(cr, size);
	}
	for (int pass = 0; pass < 2; pass++) {
		cairo_save(cr);
		if (cjk) {
			double y = top + 8.0 + step;
			for (const char *p = word; *p;) {
				p += cal_letter(p, ch);
				cairo_text_extents_t e;
				cairo_text_extents(cr, ch, &e);
				/* the long vowel mark and a dash go down the column, as written so */
				if (!strcmp(ch, "\xe3\x83\xbc") || !strcmp(ch, "\xef\xbc\x8d") || !strcmp(ch, "-")) {
					cairo_save(cr);
					cairo_translate(cr, -hw + 7.0 + size * 0.5 - e.x_advance * 0.0 - 1.0,
						y - step + (step - e.x_advance) * 0.5);
					cairo_rotate(cr, M_PI * 0.5);
					cairo_move_to(cr, 0.0, -size * 0.38 + (pass ? 0.0 : 1.0));
					cairo_text_path(cr, ch);
					cairo_restore(cr);
				} else {
					cairo_move_to(cr, -hw + 7.0 + (size - e.x_advance) * 0.5, y + (pass ? 0.0 : 1.0));
					cairo_text_path(cr, ch);
				}
				y += step;
			}
		} else {
			cairo_translate(cr, -hw + 7.0, top + 10.0);
			cairo_rotate(cr, M_PI * 0.5);
			double x = 0.0;
			for (const char *p = word; *p;) {
				p += cal_letter(p, ch);
				cairo_text_extents_t e;
				cairo_text_extents(cr, ch, &e);
				cairo_move_to(cr, x, pass ? 0.0 : -1.0);
				cairo_text_path(cr, ch);
				x += e.x_advance + space;
			}
		}
		cairo_restore(cr);
		if (pass == 0) {
			cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.62);
		} else {
			cairo_set_source_rgba(cr, 0.965, 0.862, 0.612, 0.92);
		}
		cairo_fill(cr);
	}
	cairo_restore(cr);
}

/* Sample k of row r, out of the fan as far as it is, over the rest: its
 * shadow on what is under it, the desk in that wood, the lamp on its
 * lacquer, the hole it turns on, its name. */
static void fan_chip(cairo_t *cr, const struct fan_state *fs, int k, double fx, double fy)
{
	const struct desk_form_row *r = fs->r;
	double a = fan_angle_of(fs, k) + fan_swing(fs, k);
	double up = fan_raise(fs, k), sc = 1.0 + (FAN_RAISE - 1.0) * up;
	double hw = FAN_CW * 0.5, top = -(FAN_CH - FAN_PIN);
	/* the shadow: down and to the right of it, the lamp being up to the left
	 * -- further off, and softer, from the one lifted up off the rest */
	for (int s = 0; s < 3; s++) {
		double far = 1.0 + 1.6 * up;
		cairo_save(cr);
		cairo_translate(cr, fx + (1.5 + s * 0.8) * far, fy + (3.0 + s * 1.2) * far);
		cairo_rotate(cr, a);
		cairo_scale(cr, sc, sc);
		fan_chip_path(cr);
		cairo_set_source_rgba(cr, 0.10, 0.06, 0.02, s == 0 ? 0.22 + 0.06 * up : 0.07 + 0.05 * up);
		if (s == 0) {
			cairo_fill(cr);
		} else {
			cairo_set_line_width(cr, 2.0 + s * 2.0);
			cairo_stroke(cr);
		}
		cairo_restore(cr);
	}
	cairo_save(cr);
	cairo_translate(cr, fx, fy);
	cairo_rotate(cr, a);
	cairo_scale(cr, sc, sc);
	fan_chip_path(cr);
	cairo_save(cr);
	cairo_clip(cr);
	/* the desk itself, as many of the screen's pixels as it covers */
	double dx = 1.0, dy = 0.0;
	cairo_user_to_device_distance(cr, &dx, &dy);
	double ds = hypot(dx, dy);
	/* in whole pixels to the point, so a sheet that is turning does not ask
	 * for a new picture of them at every step */
	ds = ds < 1.5 ? 1.0 : ds < 2.5 ? 2.0 : 3.0;
	int pw = (int)ceil(FAN_CW * ds), ph = (int)ceil(FAN_CH * ds);
	int wood = r->swatch == DESK_SWATCH_WOOD ? k : r->swatch_with;
	int stain = r->swatch == DESK_SWATCH_WOOD ? r->swatch_with : k;
	cairo_surface_t *pic = desk_wood_sample(wood, stain, pw, ph);
	cairo_save(cr);
	cairo_translate(cr, -hw, top);
	cairo_scale(cr, FAN_CW / pw, FAN_CH / ph);
	cairo_set_source_surface(cr, pic, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_surface_destroy(pic);
	/* the lamp on its lacquer, from the left; its end in its own shadow */
	cairo_pattern_t *g = cairo_pattern_create_linear(-hw, top, hw * 0.9, top + FAN_CW * 0.35);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 1.0, 0.96, 0.88, 0.26);
	cairo_pattern_add_color_stop_rgba(g, 0.38, 1.0, 0.96, 0.88, 0.0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	g = cairo_pattern_create_linear(0.0, FAN_PIN, 0.0, FAN_PIN - FAN_CH * 0.3);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0.0, 0.0, 0.0, 0.26);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.0, 0.0, 0.0, 0.0);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	if (k == r->value) {
		cairo_set_source_rgba(cr, 1.0, 0.97, 0.90, 0.06);
		cairo_paint(cr);
	}
	cairo_restore(cr);
	/* its edge: dark all round, lit along its tip */
	fan_chip_path(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.06, 0.03, 0.01, 0.55);
	cairo_stroke(cr);
	cairo_move_to(cr, -hw + 6.0, top + 1.0);
	cairo_line_to(cr, hw - 6.0, top + 1.0);
	cairo_set_source_rgba(cr, 1.0, 0.94, 0.84, 0.35);
	cairo_stroke(cr);
	if (up > 0.0) {
		/* lifted, its edges catch the lamp: lit down its left, in shade down
		 * its right, as a thing with a thickness to it */
		cairo_set_line_width(cr, 1.4);
		cairo_move_to(cr, -hw + 0.9, top + 6.0);
		cairo_line_to(cr, -hw + 0.9, FAN_PIN - hw);
		cairo_set_source_rgba(cr, 1.0, 0.94, 0.84, 0.32 * up);
		cairo_stroke(cr);
		cairo_move_to(cr, hw - 0.9, top + 6.0);
		cairo_line_to(cr, hw - 0.9, FAN_PIN - hw);
		cairo_set_source_rgba(cr, 0.04, 0.02, 0.0, 0.40 * up);
		cairo_stroke(cr);
	}
	/* the hole it turns on */
	cairo_arc(cr, 0.0, 0.0, 7.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgb(cr, 0.07, 0.04, 0.02);
	cairo_fill(cr);
	if (r->options[k]) {
		fan_chip_tag(cr, r->options[k]);
	}
	cairo_restore(cr);
}

/* The rivet the fan turns on: brass, its slot across it. */
static void fan_rivet(cairo_t *cr, double x, double y)
{
	cairo_save(cr);
	cairo_arc(cr, x + 0.6, y + 2.0, 10.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
	cairo_fill(cr);
	cairo_pattern_t *g = cairo_pattern_create_radial(x - 3.5, y - 3.5, 0.5, x, y, 10.5);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 1.0, 0.957, 0.81);
	cairo_pattern_add_color_stop_rgb(g, 0.55, 0.80, 0.63, 0.29);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.45, 0.32, 0.11);
	cairo_arc(cr, x, y, 10.0, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 0.8);
	cairo_set_source_rgba(cr, 0.30, 0.20, 0.05, 0.7);
	cairo_stroke(cr);
	double a = 32.0 * M_PI / 180.0;
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_width(cr, 2.2);
	cairo_move_to(cr, x - 6.5 * cos(a), y - 6.5 * sin(a));
	cairo_line_to(cr, x + 6.5 * cos(a), y + 6.5 * sin(a));
	cairo_set_source_rgba(cr, 0.23, 0.14, 0.03, 0.85);
	cairo_stroke(cr);
	cairo_move_to(cr, x - 6.5 * cos(a), y - 6.5 * sin(a) + 1.2);
	cairo_line_to(cr, x + 6.5 * cos(a), y + 6.5 * sin(a) + 1.2);
	cairo_set_line_width(cr, 0.8);
	cairo_set_source_rgba(cr, 1.0, 0.95, 0.75, 0.45);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* A brass plate with a name engraved in it, screwed down at its ends. */
static void fan_plate(cairo_t *cr, double x, double y, double w, const char *name)
{
	double h = FAN_PLATE_H;
	cairo_save(cr);
	desk_rounded_path(cr, x + 1.0, y + 2.5, w, h, 4.0);
	cairo_set_source_rgba(cr, 0.12, 0.07, 0.02, 0.30);
	cairo_fill(cr);
	cairo_pattern_t *g = cairo_pattern_create_linear(0.0, y, 0.0, y + h);
	cairo_pattern_add_color_stop_rgb(g, 0.0, 0.99, 0.90, 0.62);
	cairo_pattern_add_color_stop_rgb(g, 0.45, 0.86, 0.69, 0.33);
	cairo_pattern_add_color_stop_rgb(g, 1.0, 0.63, 0.47, 0.19);
	desk_rounded_path(cr, x, y, w, h, 4.0);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.33, 0.22, 0.06, 0.75);
	cairo_stroke(cr);
	cairo_move_to(cr, x + 4.0, y + 1.5);
	cairo_line_to(cr, x + w - 4.0, y + 1.5);
	cairo_set_source_rgba(cr, 1.0, 0.98, 0.86, 0.7);
	cairo_stroke(cr);
	/* the screws */
	for (int e = 0; e < 2; e++) {
		double sx = e ? x + w - 10.0 : x + 10.0, sy = y + h * 0.5;
		cairo_arc(cr, sx, sy, 3.6, 0.0, 2.0 * M_PI);
		cairo_set_source_rgb(cr, 0.55, 0.41, 0.16);
		cairo_fill(cr);
		cairo_arc(cr, sx, sy, 3.6, 0.0, 2.0 * M_PI);
		cairo_set_line_width(cr, 0.7);
		cairo_set_source_rgba(cr, 0.28, 0.18, 0.04, 0.8);
		cairo_stroke(cr);
		cairo_move_to(cr, sx - 2.6, sy + (e ? 0.9 : -0.9));
		cairo_line_to(cr, sx + 2.6, sy + (e ? -0.9 : 0.9));
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	}
	/* the name, cut into it: dark, and the cut's lower lip catching the light */
	bool latin = true;
	for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
		latin = latin && *p < 0xe3;
	}
	char word[96];
	size_t k = 0;
	for (const char *p = name; *p && k + 1 < sizeof word; p++) {
		word[k++] = latin ? (char)toupper((unsigned char)*p) : *p;
	}
	word[k] = '\0';
	desk_font_select(cr, DESK_FONT_UI, word, true, false);
	cairo_set_font_size(cr, latin ? 12.5 : 13.5);
	double space = latin ? 2.4 : 3.0, tw = 0.0;
	char ch[8];
	for (const char *p = word; *p;) {
		p += cal_letter(p, ch);
		cairo_text_extents_t e;
		cairo_text_extents(cr, ch, &e);
		tw += e.x_advance + space;
	}
	tw -= space;
	double scale = tw > w - 34.0 ? (w - 34.0) / tw : 1.0;
	for (int pass = 0; pass < 2; pass++) {
		cairo_save(cr);
		cairo_translate(cr, x + w * 0.5 - tw * scale * 0.5, y + h * 0.5 + 5.0 + (pass ? 0.0 : 1.0));
		cairo_scale(cr, scale, 1.0);
		double cx = 0.0;
		for (const char *p = word; *p;) {
			p += cal_letter(p, ch);
			cairo_text_extents_t e;
			cairo_text_extents(cr, ch, &e);
			cairo_move_to(cr, cx, 0.0);
			cairo_text_path(cr, ch);
			cx += e.x_advance + space;
		}
		cairo_restore(cr);
		if (pass == 0) {
			cairo_set_source_rgba(cr, 1.0, 0.95, 0.74, 0.65);
		} else {
			cairo_set_source_rgba(cr, 0.24, 0.15, 0.03, 0.92);
		}
		cairo_fill(cr);
	}
	cairo_restore(cr);
}

/* A fan, its rivet at fx, fy, and the plate. */
static void fan_draw(cairo_t *cr, const struct fan_state *fs, double fx, double fy,
	const double plate[3])
{
	const struct desk_form_row *r = fs->r;
	for (int k = 0; k < fs->n; k++) {
		if (k != r->value && fan_has(r, k)) {
			fan_chip(cr, fs, k, fx, fy);
		}
	}
	if (r->value >= 0 && r->value < fs->n && fan_has(r, r->value)) {
		fan_chip(cr, fs, r->value, fx, fy);
	}
	fan_rivet(cr, fx, fy);
	if (r->value >= 0 && r->value < fs->n) {
		/* ticked on the order, in the box printed before its plate */
		double bx = plate[0] - FAN_BOX, by = plate[1] + (FAN_PLATE_H - 17.0) * 0.5, bs = 17.0;
		cairo_save(cr);
		cairo_set_line_width(cr, 1.3);
		cairo_set_source_rgba(cr, 0.27, 0.21, 0.15, 0.85);
		cairo_rectangle(cr, bx + 0.5, by + 0.5, bs, bs);
		cairo_stroke(cr);
		cairo_restore(cr);
		form_tick(cr, bx, by);
		fan_plate(cr, plate[0], plate[1], plate[2], r->options[r->value]);
	}
}

/* An action's word, and how it is written: "Open ›" firmly, or what is
 * written where there is nothing to open, smaller. */
static const char *form_action_word(const struct desk_form_row *r, double *size)
{
	bool there = r->value || !r->options[1];
	*size = there ? 17.0 : 14.0;
	return there ? r->options[0] : r->options[1];
}

/* A row's note in lines width wide: what it warns of first (in red, the
 * first nwarn of them), then what it says; six of each at most. */
static void form_wrap_note(cairo_t *cr, const struct desk_form_row *r, double width,
	struct form_layout *L, int i)
{
	form_font(cr, 12.5, false);
	int n = 0;
	if (r->warn && *r->warn) {
		n = wrap_lines(cr, r->warn, width, L->note[i], FORM_NOTE_MAX / 2 + 1);
	}
	L->nwarn[i] = n;
	if (r->note && *r->note && n < FORM_NOTE_MAX) {
		n += wrap_lines(cr, r->note, width, L->note[i] + n, FORM_NOTE_MAX - n);
	}
	L->nl[i] = n;
}

static void form_layout(cairo_t *cr, double w, const struct desk_shell_form *f,
	struct form_layout *L)
{
	double m = FORM_M, tx = m + 30.0, right = w - m;
	double y = FORM_TOP;
	int n = f->n < FORM_ROWS ? f->n : FORM_ROWS;
	cairo_save(cr);
	for (int i = 0; i < n; i++) {
		const struct desk_form_row *r = &f->rows[i];
		L->y[i] = y;
		L->nlab[i] = L->nopt[i] = L->nhand[i] = L->nl[i] = L->nwarn[i] = 0;
		if (r->kind == DESK_FORM_SECTION) {
			/* what the part is about, if it says, under its rule, as wide */
			form_wrap_note(cr, r, right - m, L, i);
			L->note_y[i] = y + 54.0;
			L->h[i] = 44.0 + (L->nl[i] > 0 ? L->nl[i] * FORM_NOTE_LH + 8.0 : 0.0);
			y += L->h[i];
			continue;
		}
		desk_font_select(cr, DESK_FONT_PRINT, r->label, true, false);
		cairo_set_font_size(cr, 15.0);
		L->nlab[i] = wrap_lines(cr, r->label, right - tx, L->label[i], 2);
		/* the label's last line, and the note's first */
		double below = y + 23.0 + (L->nlab[i] > 1 ? L->nlab[i] - 1 : 0) * FORM_LABEL_LH;
		double next = below + 19.0;
		L->note_x[i] = tx;
		if (r->kind == DESK_FORM_CHOICE && r->swatch && r->options) {
			/* the fan under the label; its plate and the note beside it, or
			 * under it if the sheet is too narrow for that */
			int k = 0;
			while (k < DESK_FORM_OPTS && r->options[k]) {
				k++;
			}
			L->nopt[i] = k;
			double half = fan_half_width(k);
			L->fan_x[i] = tx + half;
			L->fan_y[i] = below + 8.0 + FAN_CH - FAN_PIN + FAN_LIFT;
			double bottom = L->fan_y[i] + FAN_PIN + 6.0;
			double px = tx + 2.0 * half + 24.0;
			if (right - px >= 210.0) {
				/* the box it is ticked in, and the plate after it */
				L->plate[i][0] = px + FAN_BOX;
				L->plate[i][1] = below + 34.0;
				L->plate[i][2] = fmin(230.0, right - px - FAN_BOX);
				L->note_x[i] = px;
				next = L->plate[i][1] + FAN_PLATE_H + 26.0;
				form_wrap_note(cr, r, right - px, L, i);
				L->note_y[i] = next;
				double note_end = next + (L->nl[i] > 0 ? (L->nl[i] - 1) * FORM_NOTE_LH + 5.0 : -12.0);
				L->h[i] = fmax(bottom, note_end) - y + 14.0;
			} else {
				L->plate[i][2] = fmin(230.0, right - tx - FAN_BOX);
				L->plate[i][0] = L->fan_x[i] - L->plate[i][2] * 0.5 < tx + FAN_BOX ? tx + FAN_BOX
					: L->fan_x[i] - L->plate[i][2] * 0.5;
				L->plate[i][1] = bottom + 10.0;
				next = L->plate[i][1] + FAN_PLATE_H + 26.0;
				form_wrap_note(cr, r, right - tx, L, i);
				L->note_y[i] = next;
				L->h[i] = next - y + (L->nl[i] > 0 ? (L->nl[i] - 1) * FORM_NOTE_LH + 5.0 : -12.0)
					+ 14.0;
			}
			y += L->h[i];
			continue;
		}
		if (r->kind == DESK_FORM_SIGN) {
			/* the line to sign on, under the label, and what it says under it */
			L->opt_x[i][0] = tx;
			L->opt_w[i][0] = fmin(SIGN_LINE_W, right - tx);
			L->opt_y[i][0] = below + 38.0;
			L->nopt[i] = 1;
			next = below + 38.0 + 32.0;
		} else if ((r->kind == DESK_FORM_CHOICE || r->kind == DESK_FORM_DATE) && r->options) {
			form_font(cr, 14.0, false);
			double x = tx;
			int line = 0;
			for (int k = 0; r->options[k] && k < DESK_FORM_OPTS; k++) {
				cairo_text_extents_t e;
				cairo_text_extents(cr, r->options[k], &e);
				/* words that would run off the sheet start a line of their own */
				if (k > 0 && x + e.x_advance > right) {
					x = tx;
					line++;
				}
				L->opt_x[i][k] = x;
				L->opt_w[i][k] = e.x_advance;
				L->opt_y[i][k] = below + 27.0 + line * FORM_OPT_LH;
				x += e.x_advance + FORM_OPT_GAP;
				L->nopt[i] = k + 1;
			}
			next += FORM_OPT_LH * (line + 1);
		} else if (r->kind == DESK_FORM_ACTION && r->options && r->options[0]) {
			double size;
			const char *word = form_action_word(r, &size);
			form_hand(cr, word, size);
			cairo_text_extents_t e;
			cairo_text_extents(cr, word, &e);
			double lw = word_width(cr, L->label[i][0], 15.0);
			L->nopt[i] = 1;
			if (L->nlab[i] == 1 && tx + lw + 24.0 + e.x_advance <= right) {
				/* after the label, over at the far side */
				L->nhand[i] = 1;
				snprintf(L->hand[i][0], sizeof L->hand[i][0], "%s", word);
				L->opt_x[i][0] = right - e.x_advance;
				L->opt_w[i][0] = e.x_advance;
				L->opt_y[i][0] = below;
			} else {
				/* no room after it: under it, on two lines if it takes them */
				L->nhand[i] = wrap_lines(cr, word, right - tx, L->hand[i], 2);
				L->opt_x[i][0] = tx;
				L->opt_w[i][0] = L->nhand[i] > 1 ? right - tx : e.x_advance;
				L->opt_y[i][0] = below + 22.0;
				next += 22.0 + (L->nhand[i] - 1) * FORM_HAND_LH;
			}
		}
		form_wrap_note(cr, r, right - tx, L, i);
		L->note_y[i] = next;
		/* the note's last line, and a gap under it as before */
		L->h[i] = next - y + (L->nl[i] > 0 ? (L->nl[i] - 1) * FORM_NOTE_LH + 5.0 : -12.0) + 14.0;
		y += L->h[i];
	}
	cairo_restore(cr);
	L->foot_y = y + 10.0;
	L->total = L->foot_y + (f->kept_in && *f->kept_in ? 44.0 : 10.0);
}

/* A scratch context, for measuring when nothing is being drawn. */
static void form_layout_bare(double w, const struct desk_shell_form *f, struct form_layout *L)
{
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *cr = cairo_create(s);
	form_layout(cr, w, f, L);
	cairo_destroy(cr);
	cairo_surface_destroy(s);
}

double desk_shell_form_height(double w, const struct desk_shell_form *f)
{
	struct form_layout *L = calloc(1, sizeof *L);
	if (!L) {
		return 400.0;
	}
	form_layout_bare(w, f, L);
	double t = L->total;
	free(L);
	return t;
}

void desk_shell_form_row_span(double w, const struct desk_shell_form *f, int i, double *top,
	double *height)
{
	*top = 0.0;
	*height = 0.0;
	struct form_layout *L = calloc(1, sizeof *L);
	if (!L) {
		return;
	}
	form_layout_bare(w, f, L);
	if (i >= 0 && i < f->n && i < FORM_ROWS) {
		*top = L->y[i];
		*height = L->h[i];
	}
	free(L);
}

static void form_caps(cairo_t *cr, const char *s, double x, double y, double size)
{
	cairo_save(cr);
	form_font(cr, size, true);
	cairo_set_source_rgba(cr, 0.36, 0.29, 0.21, 0.80);
	char ch[8];
	for (const char *p = s; *p;) {
		p += cal_letter(p, ch);
		cairo_text_extents_t e;
		cairo_text_extents(cr, ch, &e);
		cairo_move_to(cr, x, y);
		cairo_show_text(cr, ch);
		x += e.x_advance + size * 0.22;
	}
	cairo_restore(cr);
}

/* A ring drawn round a word in pencil, the way you mark the one you mean:
 * once round, a little more than once, never quite an ellipse. */
static void form_ring(cairo_t *cr, double x, double y, double w, double h, unsigned seed)
{
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_width(cr, 1.7);
	cairo_set_source_rgba(cr, 0.21, 0.21, 0.23, 0.82);
	double cx = x + w * 0.5, cy = y + h * 0.5, rx = w * 0.5 + 9.0, ry = h * 0.5 + 6.0;
	double a0 = -2.4 + hash21(seed, 1.3) * 0.6;
	for (int k = 0; k <= 64; k++) {
		double t = k / 64.0;
		double a = a0 + t * (2.0 * M_PI + 0.55);
		double wob = 1.0 + 0.045 * sin(a * 3.0 + seed) + 0.06 * t;
		double px = cx + cos(a) * rx * wob, py = cy + sin(a) * ry * wob - 1.0 * t;
		if (k == 0) {
			cairo_move_to(cr, px, py);
		} else {
			cairo_line_to(cr, px, py);
		}
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

static void form_tick(cairo_t *cr, double bx, double by)
{
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_source_rgba(cr, 0.22, 0.22, 0.24, 0.88);
	cairo_set_line_width(cr, 2.3);
	cairo_move_to(cr, bx + 3.0, by + 9.5);
	cairo_curve_to(cr, bx + 5.0, by + 11.0, bx + 6.5, by + 13.5, bx + 7.5, by + 15.5);
	cairo_curve_to(cr, bx + 10.0, by + 8.5, bx + 14.5, by + 2.0, bx + 21.5, by - 4.0);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* The word the hand is over, lightly underlined in pencil: its left end
 * at x, w long, on the line at y. */
static void form_underline(cairo_t *cr, double x, double y, double w)
{
	cairo_save(cr);
	cairo_set_line_width(cr, 1.2);
	cairo_set_source_rgba(cr, 0.25, 0.24, 0.24, 0.55);
	cairo_move_to(cr, x, y);
	cairo_curve_to(cr, x + w * 0.3, y + 1.2, x + w * 0.7, y - 0.6, x + w + 1.0, y + 0.4);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* An order's line to sign on, w long from x on the line at y: printed, with
 * what it is for under it; "Sign here ›" pencilled at its end while there is
 * something ticked to sign for; signed, the name written along it in pencil,
 * as far as t has written it -- a light, quick hand, sloping a little, and a
 * stroke back under it to finish. */
static void form_sign(cairo_t *cr, const struct desk_form_row *r, double x, double y, double w,
	double t, bool hover)
{
	cairo_save(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.30, 0.24, 0.17, 0.55);
	cairo_move_to(cr, x, y + 0.5);
	cairo_line_to(cr, x + w, y + 0.5);
	cairo_stroke(cr);
	cairo_restore(cr);
	form_caps(cr, TR("SIGNATURE"), x, y + 15.0, 8.5);
	if (r->value == 1 && r->options && r->options[1]) {
		cairo_save(cr);
		form_hand(cr, r->options[1], 17.0);
		cairo_text_extents_t e;
		cairo_text_extents(cr, r->options[1], &e);
		cairo_set_source_rgba(cr, 0.21, 0.21, 0.23, 0.88);
		cairo_move_to(cr, x + w - e.x_advance, y - 7.0);
		cairo_show_text(cr, r->options[1]);
		cairo_restore(cr);
		if (hover) {
			form_underline(cr, x + w - e.x_advance, y - 2.0, e.x_advance);
		}
	} else if (r->value == 2 && r->options && r->options[0]) {
		const char *name = r->options[0];
		double e = clampf(t / 0.8, 0.0, 1.0), f = clampf((t - 0.72) / 0.28, 0.0, 1.0);
		e = 1.0 - pow(1.0 - e, 1.6);
		cairo_save(cr);
		form_hand(cr, name, 30.0);
		cairo_text_extents_t te;
		cairo_text_extents(cr, name, &te);
		double sx = x + 14.0, tw = te.x_advance;
		cairo_save(cr);
		cairo_rectangle(cr, sx - 6.0, y - 60.0, (tw + 12.0) * e, 80.0);
		cairo_clip(cr);
		cairo_translate(cr, sx, y - 6.0);
		cairo_rotate(cr, -0.07);
		cairo_transform(cr, &(cairo_matrix_t){ 1.0, 0.0, -0.18, 1.0, 0.0, 0.0 });
		cairo_set_source_rgba(cr, 0.20, 0.20, 0.23, 0.86);
		cairo_move_to(cr, 0.0, 0.0);
		cairo_show_text(cr, name);
		cairo_restore(cr);
		if (f > 0.0) {
			/* and back under it */
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
			cairo_set_line_width(cr, 1.6);
			cairo_set_source_rgba(cr, 0.20, 0.20, 0.23, 0.80);
			double x0 = sx + tw + 4.0, y0 = y - 12.0, x1 = sx - 2.0, y1 = y + 1.0;
			const int n = 24;
			for (int k = 0; k <= (int)(n * f); k++) {
				double u = (double)k / n;
				double px = x0 + (x1 - x0) * u, py = y0 + (y1 - y0) * u + sin(u * M_PI) * 7.0;
				if (k == 0) {
					cairo_move_to(cr, px, py);
				} else {
					cairo_line_to(cr, px, py);
				}
			}
			cairo_stroke(cr);
		}
		cairo_restore(cr);
	}
}

/* A note's lines, from its first baseline down, at x: what it warns of in
 * the red the desk marks things in, the rest in pencil grey. */
static void form_note(cairo_t *cr, const struct form_layout *L, int i, double x, double ink)
{
	cairo_save(cr);
	form_font(cr, 12.5, false);
	for (int k = 0; k < L->nl[i]; k++) {
		if (k < L->nwarn[i]) {
			cairo_set_source_rgba(cr, 0.62, 0.13, 0.10, 0.95 * ink);
		} else {
			cairo_set_source_rgba(cr, 0.32, 0.27, 0.21, 0.88 * ink);
		}
		cairo_move_to(cr, x, L->note_y[i] + k * FORM_NOTE_LH);
		cairo_show_text(cr, L->note[i][k]);
	}
	cairo_restore(cr);
}

void desk_draw_pad_paper(cairo_t *cr)
{
	fill_tile(cr, paper_tile(), 1.0, 1.0);
	cairo_set_source_rgba(cr, 1.0, 1.0, 0.99, 0.30);
	cairo_paint(cr);
}

void desk_draw_shell_form(cairo_t *cr, double w, double h, const struct desk_shell_form *f)
{
	/* nothing under the ink: the paper is the shell's, under this -- unless
	 * the form is on a sheet of its own */
	if (!f->on_paper) {
		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cr);
		cairo_restore(cr);
	}
	struct form_layout *L = calloc(1, sizeof *L);
	if (!L) {
		return;
	}
	form_layout(cr, w, f, L);
	double m = FORM_M, tx = m + 30.0;
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);
	cairo_translate(cr, 0, -f->scroll);
	int n = f->n < FORM_ROWS ? f->n : FORM_ROWS;
	for (int i = 0; i < n; i++) {
		const struct desk_form_row *r = &f->rows[i];
		double y = L->y[i];
		if (y + L->h[i] < f->scroll || y > f->scroll + h) {
			continue;
		}
		if (r->kind == DESK_FORM_SECTION) {
			form_caps(cr, r->label, m, y + 26.0, 10.5);
			cairo_set_line_width(cr, 1.0);
			cairo_set_source_rgba(cr, 0.30, 0.24, 0.17, 0.30);
			cairo_move_to(cr, m, y + 35.5);
			cairo_line_to(cr, w - m, y + 35.5);
			cairo_stroke(cr);
			form_note(cr, L, i, m, 1.0);
			continue;
		}
		/* the row under the hand, washed over a little, the way a page
		 * goes lighter where the lamp is brought to it */
		if (i == f->hover) {
			cairo_set_source_rgba(cr, 1.0, 0.96, 0.84, 0.30);
			desk_rounded_path(cr, m - 10.0, y + 2.0, w - 2.0 * m + 20.0, L->h[i] - 6.0, 6.0);
			cairo_fill(cr);
		}
		if (r->kind == DESK_FORM_CHECK) {
			/* the box, printed, and ticked in pencil */
			double bx = m, by = y + 9.0, bs = 17.0;
			cairo_set_line_width(cr, 1.3);
			cairo_set_source_rgba(cr, 0.27, 0.21, 0.15, 0.85);
			cairo_rectangle(cr, bx + 0.5, by + 0.5, bs, bs);
			cairo_stroke(cr);
			if (r->value) {
				form_tick(cr, bx, by);
			}
		}
		/* a program that is not there to open: its row printed faint */
		double ink = r->kind == DESK_FORM_ACTION && !r->value ? 0.45 : 1.0;
		if (r->kind == DESK_FORM_ACTION && r->icon) {
			/* the program's own icon, where a box would be: whole, in the
			 * middle of its square */
			double iw = cairo_image_surface_get_width(r->icon);
			double ih = cairo_image_surface_get_height(r->icon);
			double is = 24.0, k = iw > 0 && ih > 0 ? is / fmax(iw, ih) : 1.0;
			cairo_save(cr);
			cairo_translate(cr, m - 4.0 + (is - iw * k) * 0.5, y + 6.0 + (is - ih * k) * 0.5);
			cairo_scale(cr, k, k);
			cairo_set_source_surface(cr, r->icon, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			cairo_paint_with_alpha(cr, ink);
			cairo_restore(cr);
		}
		for (int k = 0; k < L->nlab[i]; k++) {
			print_word(cr, L->label[i][k], tx, y + 23.0 + k * FORM_LABEL_LH, 15.0,
				0.18, 0.14, 0.10, 0.94 * ink);
		}
		if (r->kind == DESK_FORM_CHOICE && r->swatch && r->options) {
			struct fan_state fs = fan_state_of(f, i, L->nopt[i]);
			fan_draw(cr, &fs, L->fan_x[i], L->fan_y[i], L->plate[i]);
		} else if (r->kind == DESK_FORM_SIGN) {
			form_sign(cr, r, L->opt_x[i][0], L->opt_y[i][0], L->opt_w[i][0],
				f->sign_t > 0.0 && f->sign_t < 1.0 ? f->sign_t : 1.0, i == f->hover);
		} else if ((r->kind == DESK_FORM_CHOICE || r->kind == DESK_FORM_DATE) && r->options) {
			bool date = r->kind == DESK_FORM_DATE;
			for (int k = 0; k < L->nopt[i]; k++) {
				double ox = L->opt_x[i][k], ow = L->opt_w[i][k], oy = L->opt_y[i][k];
				/* a date: the month and the day written in, firmly; none
				 * ringed when it is none */
				bool chosen = date ? k == 6 && r->value == 0 : k == r->value;
				bool firm = chosen || (date && (k == 1 || k == 4) && r->value != 0);
				cairo_save(cr);
				form_font(cr, 14.0, firm);
				cairo_set_source_rgba(cr, 0.20, 0.16, 0.12, firm ? 0.95 : 0.70);
				cairo_move_to(cr, ox, oy);
				cairo_show_text(cr, r->options[k]);
				cairo_restore(cr);
				if (chosen) {
					form_ring(cr, ox, oy - 13.0, ow, 17.0, (unsigned)(i * 7 + k));
				} else if (i == f->hover && k == f->hover_opt) {
					form_underline(cr, ox, oy + 5.0, ow);
				}
			}
		} else if (r->kind == DESK_FORM_ACTION && L->nopt[i] > 0) {
			double size;
			const char *word = form_action_word(r, &size);
			double ox = L->opt_x[i][0], oy = L->opt_y[i][0];
			cairo_save(cr);
			form_hand(cr, word, size);
			cairo_set_source_rgba(cr, 0.21, 0.21, 0.23, r->value ? 0.88 : 0.55);
			for (int k = 0; k < L->nhand[i]; k++) {
				cairo_move_to(cr, ox, oy + k * FORM_HAND_LH);
				cairo_show_text(cr, L->hand[i][k]);
			}
			cairo_restore(cr);
			/* where the click will take you, underlined while the hand is on
			 * the row */
			if (r->value && i == f->hover) {
				form_underline(cr, ox, oy + 5.0, L->opt_w[i][0]);
			}
		}
		form_note(cr, L, i, L->note_x[i], ink);
	}
	/* where it is kept, at the foot */
	if (f->kept_in && *f->kept_in) {
		cairo_save(cr);
		form_font(cr, 10.5, false);
		char foot[2][256];
		char text[700];
		snprintf(text, sizeof text, TR("Kept in %s. One program the other way about paper: "
			"paper.<program> = yes or no."), f->kept_in);
		int k = wrap_lines(cr, text, w - 2.0 * m, foot, 2);
		cairo_set_source_rgba(cr, 0.36, 0.30, 0.23, 0.70);
		for (int i = 0; i < k; i++) {
			cairo_move_to(cr, m, L->foot_y + 14.0 + i * 14.0);
			cairo_show_text(cr, foot[i]);
		}
		cairo_restore(cr);
	}
	cairo_restore(cr);
	/* more of the form below the fold of the window */
	if (L->total - f->scroll > h + 2.0) {
		form_caps(cr, TR("MORE"), w - m - 46.0, h - 9.0, 9.0);
		cairo_set_source_rgba(cr, 0.36, 0.29, 0.21, 0.80);
		cairo_move_to(cr, w - m - 4.0, h - 15.0);
		cairo_line_to(cr, w - m + 1.0, h - 10.0);
		cairo_line_to(cr, w - m + 6.0, h - 15.0);
		cairo_set_line_width(cr, 1.3);
		cairo_stroke(cr);
	}
	free(L);
}

int desk_shell_form_hit(double w, double h, const struct desk_shell_form *f, double x,
	double y, int *opt)
{
	*opt = -1;
	if (y < 0.0 || y > h) {
		return -1;
	}
	struct form_layout *L = calloc(1, sizeof *L);
	if (!L) {
		return -1;
	}
	form_layout_bare(w, f, L);
	double fy = y + f->scroll;
	int hit = -1;
	int n = f->n < FORM_ROWS ? f->n : FORM_ROWS;
	for (int i = 0; i < n && hit < 0; i++) {
		const struct desk_form_row *r = &f->rows[i];
		if (r->kind == DESK_FORM_SECTION || fy < L->y[i] || fy >= L->y[i] + L->h[i]) {
			continue;
		}
		if (x < FORM_M - 10.0 || x > w - FORM_M + 10.0) {
			break;
		}
		hit = i;
		if (r->kind == DESK_FORM_CHOICE && r->swatch) {
			/* a sample: the one out of the fan first, it lying over the rest,
			 * then the others from the top of the pile down */
			struct fan_state fs = fan_state_of(f, i, L->nopt[i]);
			double dx = x - L->fan_x[i], dy = fy - L->fan_y[i];
			if (r->value >= 0 && r->value < fs.n && fan_chip_at(&fs, r->value, dx, dy)) {
				*opt = r->value;
			}
			for (int k = fs.n - 1; k >= 0 && *opt < 0; k--) {
				if (k != r->value && fan_chip_at(&fs, k, dx, dy)) {
					*opt = k;
				}
			}
			continue;
		}
		if (r->kind == DESK_FORM_SIGN) {
			/* the line, while there is something to sign for */
			double ly = L->opt_y[i][0];
			if (r->value == 1 && fy >= ly - 36.0 && fy <= ly + 14.0 && x >= L->opt_x[i][0] - 10.0
				&& x <= L->opt_x[i][0] + L->opt_w[i][0] + 10.0) {
				*opt = 0;
			}
			continue;
		}
		/* a choice's word, or an action's "Open ›" while there is something
		 * to open */
		bool words = r->kind == DESK_FORM_CHOICE || r->kind == DESK_FORM_DATE
			|| (r->kind == DESK_FORM_ACTION && r->value);
		for (int k = 0; words && k < L->nopt[i]; k++) {
			double oy = L->opt_y[i][k];
			if (fy >= oy - 18.0 && fy <= oy + 10.0 && x >= L->opt_x[i][k] - 10.0
				&& x <= L->opt_x[i][k] + L->opt_w[i][k] + 10.0) {
				*opt = k;
			}
		}
	}
	free(L);
	return hit;
}

/* ---- a face pasted onto what it is printed on ----
 *
 * A face laid on as one flat sheet is right at one height only, and the console's own view
 * looks down at the set from well off to one side of most of it: the knob's pointer, laid at
 * the tower's height, came out beside the bakelite it is cut into, and the words round a
 * button's ring off the ring (2026-11-11, the owner: 描画ズレ, and then 3Dオブジェクトに貼り
 * 付け式のほうが良いかも). So the faces of what stands off the panel are pasted on: each such
 * control is a few decks -- a disc or a block with a flat or a domed top, standing on the next
 * one down -- and each pixel of it is the camera's ray through that pixel, met with the decks
 * from the top down. Where it meets a deck's top, it shows the face at that point across the
 * panel; where it meets a deck's side, nothing (the trace has the side); past them all, the
 * face at the panel's own height. Which pixel of the face each pixel of the screen shows is
 * worked out once and kept (cc_paste_map); a face that changes is then only looked up. */
struct cc_deck {
	double u, s;          /* its middle, across and up the face */
	double r;             /* a disc this wide, or (0) a block hu by hs */
	double hu, hs;
	double top, base;     /* its top, off the face, and where its sides stop */
	double dome;          /* the top is cut from a sphere this big, its crown at top (0 flat) */
	double slot_u, slot_s;  /* a slot through it, so wide and so high (0 none) */
};

#define CC_DECKS 5

/* What a control is, as decks from the top down, and the height of what it stands on (0 decks:
 * it is laid as one sheet, the way the instruments under glass are). */
static int cc_decks(int i, struct cc_deck d[CC_DECKS], double *ground)
{
	const double SK = RM_CC_SKIN;
	double u = CC[i].u, s = CC[i].s;
	if (cc_btn_mid(i)) {
		/* cc_button: the cap's dome, with the etching, and the ring round it, with the word.
		 * Not the skirt the cap sits in, between them: the word runs in over it, and pasted
		 * onto the skirt as well the letters broke in two at its edge. */
		double b = CC[i].on == CC_ON_LID ? CC_LID_N1 : SK;
		bool big = CC[i].on == CC_ON_FACE;
		double ring = big ? CC_PW_RING : CC_LB_RING, cap = big ? CC_PW_R : CC_LB_R;
		d[0] = (struct cc_deck){ u, s, cap, 0, 0, b + 0.20, b + 0.15, big ? CC_PW_DOME
			: CC_LB_DOME, 0, 0 };
		d[1] = (struct cc_deck){ u, s, ring, 0, 0, b + 0.09, b, 0, 0, 0 };
		/* (the four under the lid stand on the plate the set is mounted on) */
		*ground = CC[i].on == CC_ON_LID ? CC_LID_N1 : CC[i].on == CC_ON_UNDER ? SK + 0.07 : SK;
		return 2;
	}
	switch (i) {
	case CC_VOL:
		/* the bakelite on the brass skirt */
		d[0] = (struct cc_deck){ u, s, 0.505, 0, 0, SK + 0.31, SK + 0.07, 0, 0, 0 };
		d[1] = (struct cc_deck){ u, s, 0.88, 0, 0, SK + 0.07, SK, 0, 0, 0 };
		*ground = SK;
		return 2;
	case CC_BRIGHT:
	case CC_SCREEN:
		/* the plate, with the slot the handle runs in */
		d[0] = (struct cc_deck){ u, s, 0, CC[i].hu * 0.95, 0.30, SK + 0.08, SK, 0,
			CC[i].hu * 0.82, 0.11 };
		*ground = SK;
		return 1;
	case CC_WIFI:
	case CC_BT: {
		/* the pilot lamp's lens and its brass cup, the lever's boss, and the sub-panel */
		double ls = s + CC[i].hs * 0.86;
		d[0] = (struct cc_deck){ u, ls, 0.155, 0, 0, SK + 0.295, SK + 0.10, 0.155, 0, 0 };
		d[1] = (struct cc_deck){ u, ls, 0.21, 0, 0, SK + 0.10, SK + 0.06, 0, 0, 0 };
		d[2] = (struct cc_deck){ u, s, 0.14, 0, 0, SK + 0.17, SK + 0.06, 0, 0, 0 };
		d[3] = (struct cc_deck){ u, s, 0, CC[i].hu * 0.82, CC[i].hs * 0.82, SK + 0.06, SK, 0,
			0, 0 };
		*ground = SK;
		return 4;
	}
	case CC_PW_KEY:
		/* the lock's barrel; what is scored round it is on the panel */
		d[0] = (struct cc_deck){ CC_PW_LOCK_U, CC_PW_KEY_S, 0.235, 0, 0, SK + 0.11, SK, 0, 0,
			0 };
		*ground = SK;
		return 1;
	default:
		return 0;
	}
}

/* is (u, s) inside the deck's footprint (and, for one with a slot, not in the slot) */
static bool cc_deck_in(const struct cc_deck *k, double u, double s, bool *slot)
{
	double du = u - k->u, ds = s - k->s;
	*slot = k->slot_u > 0.0 && fabs(du) < k->slot_u && fabs(ds) < k->slot_s;
	return k->r > 0.0 ? du * du + ds * ds <= k->r * k->r
		: fabs(du) <= k->hu && fabs(ds) <= k->hs;
}

/* The ray o + t dir, in the face's own coordinates (x across, y out, z up), met with the decks:
 * true and where it lands, across and up, or false for a side, or nothing. */
static bool cc_decks_ray(const struct cc_deck *d, int n, double ground, vec3 o, vec3 dir,
	double *pu, double *ps)
{
	if (dir.y > -1e-6) {
		return false;
	}
	bool slot;
	for (int k = 0; k < n; k++) {
		if (d[k].dome > 0.0) {
			/* the sphere its top is cut from */
			vec3 c = v3(d[k].u, d[k].top - d[k].dome, d[k].s);
			vec3 oc = vsub(o, c);
			double b = vdot(oc, dir), cc = vdot(oc, oc) - d[k].dome * d[k].dome;
			double disc = b * b - cc * vdot(dir, dir);
			if (disc >= 0.0) {
				double t = (-b - sqrt(disc)) / vdot(dir, dir);
				vec3 p = vadd(o, vmul(dir, t));
				if (t > 0.0 && p.y >= d[k].base && cc_deck_in(&d[k], p.x, p.z, &slot)) {
					*pu = p.x;
					*ps = p.z;
					return true;
				}
			}
			/* (no sides: below its rim the next deck down is the top) */
			continue;
		}
		double t = (d[k].top - o.y) / dir.y;
		double u = o.x + dir.x * t, s = o.z + dir.z * t;
		bool in = cc_deck_in(&d[k], u, s, &slot);
		if (in && !slot) {
			*pu = u;
			*ps = s;
			return true;
		}
		t = (d[k].base - o.y) / dir.y;
		double bu = o.x + dir.x * t, bs = o.z + dir.z * t;
		bool bslot;
		if (in) {
			/* down the slot: its floor is the ground, unless the ray runs into its wall */
			if (!cc_deck_in(&d[k], bu, bs, &bslot) || !bslot) {
				return false;
			}
			continue;
		}
		if (cc_deck_in(&d[k], bu, bs, &bslot) && !bslot) {
			return false;   /* the side */
		}
	}
	double t = (ground - o.y) / dir.y;
	*pu = o.x + dir.x * t;
	*ps = o.z + dir.z * t;
	return true;
}

/* The ray through a point of the screen (where the sheet is laid at ox, oy, sw by sh), in a
 * face's own coordinates. */
static void cc_face_ray(const struct room_cam *cam, const struct cc_face *f, double aspect,
	double ox, double oy, double sw, double sh, double x, double y, vec3 *o, vec3 *dir)
{
	vec3 ro, rd;
	room_ray(cam, (x - ox) / sw, (y - oy) / sh, &ro, &rd);
	*o = cc_to_face(f, vsub(ro, v3(0.0, 0.0, cc_dz(1.0, aspect))));
	*dir = v3(vdot(rd, f->ax), vdot(rd, f->an), vdot(rd, f->as));
}

/* Where on the screen the corners of a pasted face can land: its face, at the ground and at
 * the top of its tallest deck. */
static bool cc_paste_bounds(const struct room_cam *cam, double aspect, double ox, double oy,
	double sw, double sh, int i, double *x0, double *y0, double *x1, double *y1)
{
	struct cc_deck d[CC_DECKS];
	double ground = 0.0;
	int n = cc_decks(i, d, &ground);
	if (n == 0) {
		return false;
	}
	double top = ground;
	for (int k = 0; k < n; k++) {
		top = fmax(top, d[k].top);
	}
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	const struct cc_face *f = CC[i].tower ? &tower : &panel;
	*x0 = *y0 = 1e9;
	*x1 = *y1 = -1e9;
	for (int k = 0; k < 8; k++) {
		vec3 p = cc_world(f, CC[i].u + ((k & 1) ? CC[i].fu : -CC[i].fu), (k & 4) ? top : ground,
			CC[i].s + ((k & 2) ? CC[i].fs : -CC[i].fs), 1.0, aspect);
		double u, v;
		if (!room_project(cam, p, &u, &v)) {
			return false;
		}
		*x0 = fmin(*x0, ox + u * sw);
		*y0 = fmin(*y0, oy + v * sh);
		*x1 = fmax(*x1, ox + u * sw);
		*y1 = fmax(*y1, oy + v * sh);
	}
	return true;
}

/* Which pixel of the face (fw by fh) each pixel of the screen in a box shows: two floats a
 * pixel, NAN for none. Kept, two to a control (the set as it is shown, and the lid's own
 * picture as it swings), for as long as nothing they were worked out from changes. */
struct cc_paste_map {
	int i, x0, y0, w, h;
	double ox, oy, sw, sh, aspect, fw, fh;
	vec3 eye, ww;
	float *at;
};

static struct cc_paste_map g_cc_paste[CC_COUNT][2];
static int g_cc_paste_next[CC_COUNT];
static pthread_mutex_t g_cc_paste_lock = PTHREAD_MUTEX_INITIALIZER;

static const struct cc_paste_map *cc_paste_map(const struct room_cam *cam, double aspect,
	double ox, double oy, double sw, double sh, int i, double fw, double fh)
{
	for (int e = 0; e < 2; e++) {
		struct cc_paste_map *m = &g_cc_paste[i][e];
		if (m->at && m->ox == ox && m->oy == oy && m->sw == sw && m->sh == sh
			&& m->aspect == aspect && m->fw == fw && m->fh == fh
			&& m->eye.x == cam->eye.x && m->eye.y == cam->eye.y && m->eye.z == cam->eye.z
			&& m->ww.x == cam->ww.x && m->ww.y == cam->ww.y && m->ww.z == cam->ww.z) {
			return m;
		}
	}
	double bx0, by0, bx1, by1;
	if (!cc_paste_bounds(cam, aspect, ox, oy, sw, sh, i, &bx0, &by0, &bx1, &by1)) {
		return NULL;
	}
	int x0 = (int)floor(bx0) - 1, y0 = (int)floor(by0) - 1;
	int w = (int)ceil(bx1) + 1 - x0, h = (int)ceil(by1) + 1 - y0;
	if (w <= 0 || h <= 0 || (size_t)w * h > 4096u * 4096u) {
		return NULL;
	}
	float *at = malloc((size_t)w * h * 2 * sizeof(float));
	if (!at) {
		return NULL;
	}
	struct cc_deck d[CC_DECKS];
	double ground = 0.0;
	int n = cc_decks(i, d, &ground);
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	const struct cc_face *f = CC[i].tower ? &tower : &panel;
	double sx = fw / (2.0 * CC[i].fu), sy = fh / (2.0 * CC[i].fs);
	double u0 = CC[i].u - CC[i].fu, s1 = CC[i].s + CC[i].fs;
	/* The ray's direction, left unscaled, goes linearly with the pixel (room_ray before it
	 * normalizes), so it is stepped across the box rather than worked out a pixel at a time:
	 * the first opening of the set works out every face's map at once. */
	vec3 o = cc_to_face(f, vsub(cam->eye, v3(0.0, 0.0, cc_dz(1.0, aspect))));
	vec3 r0 = vadd(cam->ww, vadd(vmul(cam->uu, ((x0 + 0.5 - ox) / sw - 0.5) * cam->fov_x),
		vmul(cam->vv, (0.5 - (y0 + 0.5 - oy) / sh) * cam->fov_y)));
	vec3 rx = vmul(cam->uu, cam->fov_x / sw), ry = vmul(cam->vv, -cam->fov_y / sh);
	vec3 d0 = v3(vdot(r0, f->ax), vdot(r0, f->an), vdot(r0, f->as));
	vec3 dx = v3(vdot(rx, f->ax), vdot(rx, f->an), vdot(rx, f->as));
	vec3 dy = v3(vdot(ry, f->ax), vdot(ry, f->an), vdot(ry, f->as));
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float *a = at + ((size_t)y * w + x) * 2;
			vec3 dir = vadd(d0, vadd(vmul(dx, x), vmul(dy, y)));
			double pu, ps;
			if (cc_decks_ray(d, n, ground, o, dir, &pu, &ps)) {
				/* (the face's pixel centres are at half-pixels) */
				a[0] = (float)((pu - u0) * sx - 0.5);
				a[1] = (float)((s1 - ps) * sy - 0.5);
				if (a[0] < -1.0f || a[1] < -1.0f || a[0] > fw || a[1] > fh) {
					a[0] = a[1] = NAN;
				}
			} else {
				a[0] = a[1] = NAN;
			}
		}
	}
	int e = g_cc_paste_next[i];
	g_cc_paste_next[i] = (e + 1) % 2;
	struct cc_paste_map *m = &g_cc_paste[i][e];
	free(m->at);
	*m = (struct cc_paste_map){ i, x0, y0, w, h, ox, oy, sw, sh, aspect, fw, fh, cam->eye,
		cam->ww, at };
	return m;
}

/* the face's premultiplied pixel at (x, y), between its pixels, and nothing past its edge */
static inline void cc_paste_sample(const uint32_t *px, int stride, int fw, int fh, float x,
	float y, float out[4])
{
	int ix = (int)floorf(x), iy = (int)floorf(y);
	float ax = x - ix, ay = y - iy;
	out[0] = out[1] = out[2] = out[3] = 0.0f;
	for (int k = 0; k < 4; k++) {
		int xx = ix + (k & 1), yy = iy + (k >> 1);
		if (xx < 0 || yy < 0 || xx >= fw || yy >= fh) {
			continue;
		}
		float wgt = ((k & 1) ? ax : 1.0f - ax) * ((k >> 1) ? ay : 1.0f - ay);
		uint32_t p = px[(size_t)yy * stride + xx];
		out[0] += wgt * (float)(p >> 24);
		out[1] += wgt * (float)((p >> 16) & 0xff);
		out[2] += wgt * (float)((p >> 8) & 0xff);
		out[3] += wgt * (float)(p & 0xff);
	}
}

/* Paste a face (a cairo image fw by fh) onto control i's decks, into cr in the screen's
 * pixels; false if it cannot be (and then it is laid flat, as before). */
static bool cc_paste(cairo_t *cr, const struct room_cam *cam, double aspect, double ox,
	double oy, double sw, double sh, int i, cairo_surface_t *face, double alpha)
{
	int fw = cairo_image_surface_get_width(face), fh = cairo_image_surface_get_height(face);
	pthread_mutex_lock(&g_cc_paste_lock);
	const struct cc_paste_map *m = cc_paste_map(cam, aspect, ox, oy, sw, sh, i, fw, fh);
	if (!m) {
		pthread_mutex_unlock(&g_cc_paste_lock);
		return false;
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, m->w, m->h);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		pthread_mutex_unlock(&g_cc_paste_lock);
		cairo_surface_destroy(img);
		return false;
	}
	cairo_surface_flush(img);
	const uint32_t *src = (const uint32_t *)cairo_image_surface_get_data(face);
	int sstride = cairo_image_surface_get_stride(face) / 4;
	uint32_t *dst = (uint32_t *)cairo_image_surface_get_data(img);
	int dstride = cairo_image_surface_get_stride(img) / 4;
	for (int y = 0; y < m->h; y++) {
		const float *a = m->at + (size_t)y * m->w * 2;
		uint32_t *row = dst + (size_t)y * dstride;
		for (int x = 0; x < m->w; x++, a += 2) {
			if (isnan(a[0])) {
				continue;
			}
			float c[4];
			cc_paste_sample(src, sstride, fw, fh, a[0], a[1], c);
			row[x] = (uint32_t)(c[0] + 0.5f) << 24 | (uint32_t)(c[1] + 0.5f) << 16
				| (uint32_t)(c[2] + 0.5f) << 8 | (uint32_t)(c[3] + 0.5f);
		}
	}
	int x0 = m->x0, y0 = m->y0;
	pthread_mutex_unlock(&g_cc_paste_lock);
	cairo_surface_mark_dirty(img);
	cairo_set_source_surface(cr, img, x0, y0);
	cairo_paint_with_alpha(cr, alpha);
	cairo_surface_destroy(img);
	return true;
}

/* A point of the screen on a control's face, at a height off it, in the face's pixels. */
static bool cc_face_point(const struct room_cam *cam, double aspect, double ox, double oy,
	double sw, double sh, int i, double fw, double fh, double n, double x, double y,
	double *fx, double *fy)
{
	struct cc_face panel, tower;
	cc_faces(&panel, &tower);
	vec3 o, dir;
	cc_face_ray(cam, CC[i].tower ? &tower : &panel, aspect, ox, oy, sw, sh, x, y, &o, &dir);
	if (dir.y > -1e-6) {
		return false;
	}
	double t = (n - o.y) / dir.y;
	*fx = (o.x + dir.x * t - (CC[i].u - CC[i].fu)) * fw / (2.0 * CC[i].fu);
	*fy = (CC[i].s + CC[i].fs - (o.z + dir.z * t)) * fh / (2.0 * CC[i].fs);
	return true;
}
