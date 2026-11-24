/* Part of draw.c: the post box, as the compositor sees it.
 * Included by src/draw.c (see the list at its end); not compiled on its own. */

/* ================================================================
 * The post box, as the compositor sees it
 *
 * The box is traced; the letters in it are not, because what is in the
 * post changes and a ray tracer that takes a minute cannot be asked about
 * it. So the envelopes are printed over the bake on the quads the box's
 * own frame gives them -- the same way the binder's sheets and the
 * console's instrument faces are.
 * ================================================================ */

static int post_xform(int screen_w, int screen_h, struct room_render **out_rr,
	double *ox, double *oy, double *scale)
{
	struct room_render *rr = room_for_view(screen_w, screen_h, DESK_VIEW_POST);
	if (!rr || !rr->post) {
		return 0;
	}
	double sx = (double)screen_w / rr->w, sy = (double)screen_h / rr->h;
	*scale = sx < sy ? sy : sx;
	*ox = (screen_w - rr->w * *scale) * 0.5;
	*oy = (screen_h - rr->h * *scale) * 0.5;
	*out_rr = rr;
	return 1;
}

/* The application a letter came from, so its letter can carry the same
 * icon its sheet carries in the bookcase. */
static cairo_surface_t *post_icon(const struct desk_note *note, char *initial,
	size_t initial_n)
{
	/* (its first letter, not its first byte: 메신저 is 메) */
	desk_initial_of(note->app, initial, initial_n);
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = NULL;
	if (note->desktop[0]) {
		char id[128];
		snprintf(id, sizeof id, "%s.desktop", note->desktop);
		app = desk_apps_find(l, id);
	}
	if (!app && note->app[0]) {
		/* no hint: match on the name it gave, which is usually the one
		 * printed on its own sheet */
		for (int i = 0; i < l->n; i++) {
			if (strcasecmp(l->apps[i].name, note->app) == 0) {
				app = &l->apps[i];
				break;
			}
		}
	}
	cairo_surface_t *ic = app ? desk_app_icon(app) : NULL;
	if (app && app->initial[0]) {
		snprintf(initial, initial_n, "%s", app->initial);
	}
	desk_apps_release(l);
	return ic;
}

/* ---- the post, opened ------------------------------------------
 *
 * The letters stand in the box leaning well back, one in front of the
 * other and each a little lower, the way cards are laid out to be picked
 * from: the top of every one of them shows -- who it is from, and what it
 * is about -- over the one in front, and the one at the front shows its
 * whole face, the writing on it running away from you with the paper. A
 * sheet that has been folded and handled does not lie flat, either: the
 * top of it bends back a little further than the rest. */

#define POST_CARD_ROWS 40        /* strips it is laid down in */
/* How far a letter leans, radians back from upright (forward is below
 * zero), and how much further its top bends the same way, by where it
 * stands against the place the box has been turned to (t: 0 there, 1 the
 * one behind it, -1 the one in front): the way the sheets of a binder on
 * the shelf go over. The one at the place stands up to be read; the ones
 * in front of it have gone well over forward, out of its way; the ones
 * behind it lean back, each further than the last.
 *
 * Two fixed ways of standing them are still here to be compared --
 * DESK_LETTERS=forward (each nearer one tipped a little further forward)
 * and =back (each further one fallen a little further back) -- and ignore
 * the place. */
#define POST_FALL_FWD (-1.30)    /* the ones in front of the place: over onto the door */
#define POST_LEAN_STEP 0.22      /* each one behind it leans this much more */

struct post_lean {
	double lean_back, lean_front, curl_back, curl_front;
};
static const struct post_lean POST_LEAN_FORWARD = { 0.06, -0.17, -0.04, -0.13 };
static const struct post_lean POST_LEAN_BACKWARD = { 0.46, 0.02, 0.16, 0.03 };

static const struct post_lean *post_lean_fixed(void)
{
	const char *e = getenv("DESK_LETTERS");
	if (e && strcmp(e, "back") == 0) {
		return &POST_LEAN_BACKWARD;
	}
	if (e && strcmp(e, "forward") == 0) {
		return &POST_LEAN_FORWARD;
	}
	return NULL;
}

static void post_lean_at(double t, int i, int n, double *lean, double *curl)
{
	const struct post_lean *pl = post_lean_fixed();
	if (pl) {
		double f = n > 1 ? (double)(n - 1 - i) / (n - 1) : 1.0;
		*lean = pl->lean_back + (pl->lean_front - pl->lean_back) * f;
		*curl = pl->curl_back + (pl->curl_front - pl->curl_back) * f;
		return;
	}
	if (t < 0.0) {
		double k = smoothstepf(0.0, 1.0, fmin(-t, 1.0));
		*lean = POST_FALL_FWD * k;
		*curl = -0.10 * k;
	} else {
		*lean = 0.04 + POST_LEAN_STEP * t;
		*curl = 0.35 * (*lean - 0.04);
	}
}

#define POST_CARD_H 1.30         /* how tall a sheet is, in room units */
#define POST_CARD_RISE 0.34      /* each one behind stands this much higher */
#define POST_CARD_DZ 0.15        /* and this much further back */

struct post_card {
	/* where each row of it lands on screen, from its bottom edge (0) up */
	double xl[POST_CARD_ROWS + 1], xr[POST_CARD_ROWS + 1], y[POST_CARD_ROWS + 1];
	double lit[POST_CARD_ROWS + 1];   /* how square each row is to the lamp */
	bool back;               /* it has gone over far enough to show its back */
	double w, h;             /* the face, flat, in pixels */
	double qx[4], qy[4];     /* its corners: top left, top right, bottom left, bottom right */
	double clip[4];          /* the opening of the box: x0, y0, x1, y1 */
};

/* The opening of the box on screen, as the post camera sees it. */
static bool post_opening(struct room_render *rr, double ox, double oy, double sw, double sh,
	double box[4])
{
	double y0 = RM_PB_PLINTH + 0.14, y1 = RM_PB_SHOULDER - 0.26;
	double hx = RM_PB_HX - 0.26, z = RM_PB_CZ + RM_PB_HZ;
	double x[4], y[4];
	for (int k = 0; k < 4; k++) {
		double u, v;
		if (!room_project(&rr->post_cam, v3(RM_PB_CX + ((k & 1) ? hx : -hx),
				(k & 2) ? y0 : y1, z), &u, &v)) {
			return false;
		}
		x[k] = ox + u * sw;
		y[k] = oy + v * sh;
	}
	box[0] = fmax(x[0], x[2]);
	box[1] = fmax(y[0], y[1]);
	box[2] = fmin(x[1], x[3]);
	box[3] = fmin(y[2], y[3]);
	return box[2] > box[0] + 8.0 && box[3] > box[1] + 8.0;
}

/* Where letter i of the n shown stands (0 the front one): upright in the
 * box, leaning back a little and bending back further at the top, each
 * one behind standing a step higher on the one before so that its head
 * shows -- the way letters stand in a rack -- and all of it through the
 * camera that is looking into the box, row by row. */
static bool post_card_at(struct room_render *rr, double ox, double oy, double sw, double sh,
	int i, int n, double pos, struct post_card *c)
{
	if (!post_opening(rr, ox, oy, sw, sh, c->clip)) {
		return false;
	}
	/* where it stands against the place turned to: the fixed ways of
	 * standing them count from the front of the box */
	double t = post_lean_fixed() ? (double)i : (double)i - pos;
	double lean, curl;
	post_lean_at(t, i, n, &lean, &curl);
	double hw = (RM_PB_HX - 0.26) * 0.92;
	/* on the tier of the rack it has got to; the ones gone over are on
	 * the front one, a little further out */
	double tier = fmax(t, 0.0);
	double by = RM_PB_PLINTH + 0.14 + tier * POST_CARD_RISE;
	double bz = RM_PB_CZ + RM_PB_HZ - 0.26 - tier * POST_CARD_DZ + fmax(-t, 0.0) * 0.08;
	double y3 = 0.0, z3 = 0.0, ds = POST_CARD_H / POST_CARD_ROWS;
	/* the lamp is up and in front of it */
	vec3 lamp = vnorm(v3(0.0, 0.80, 0.60));
	for (int m = 0; m <= POST_CARD_ROWS; m++) {
		double tr = (double)m / POST_CARD_ROWS;
		double a = lean + curl * tr * tr;
		if (m > 0) {
			double tm = (m - 0.5) / POST_CARD_ROWS;
			double am = lean + curl * tm * tm;
			y3 += cos(am) * ds;
			z3 += sin(am) * ds;
		}
		double ul, vl, ur, vr;
		if (!room_project(&rr->post_cam, v3(RM_PB_CX - hw, by + y3, bz - z3), &ul, &vl)
			|| !room_project(&rr->post_cam, v3(RM_PB_CX + hw, by + y3, bz - z3), &ur, &vr)) {
			return false;
		}
		c->xl[m] = ox + ul * sw;
		c->xr[m] = ox + ur * sw;
		c->y[m] = oy + (vl + vr) * 0.5 * sh;
		c->lit[m] = vdot(v3(0.0, sin(a), cos(a)), lamp);
	}
	c->w = c->xr[0] - c->xl[0];
	c->h = c->w * POST_CARD_H / (2.0 * hw);
	/* gone over past the line of sight, it is its back that shows, and
	 * that is lit from the other side */
	c->back = c->y[POST_CARD_ROWS] > c->y[0];
	if (c->back) {
		for (int m = 0; m <= POST_CARD_ROWS; m++) {
			c->lit[m] = -c->lit[m];
		}
	}
	c->qx[0] = c->xl[POST_CARD_ROWS];
	c->qx[1] = c->xr[POST_CARD_ROWS];
	c->qy[0] = c->qy[1] = c->y[POST_CARD_ROWS];
	c->qx[2] = c->xl[0];
	c->qx[3] = c->xr[0];
	c->qy[2] = c->qy[3] = c->y[0];
	return c->w > 8.0;
}

/* A face drawn flat, laid onto the sheet as it stands: row by row, each
 * row where the camera puts it, and each lit as squarely as it faces the
 * lamp -- which is what makes the bend at the top read as a bend. deep is
 * how far back in the box it stands, where less of the light gets. */
static void post_card_paint(cairo_t *cr, const struct post_card *c, cairo_surface_t *img,
	double alpha, int deep, double cut)
{
	double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
	double lit0 = c->lit[0];
	/* The light on it, put on the face before it is laid down -- on the
	 * face its rows do not overlap, as the strips on screen must: brighter
	 * where it faces the lamp more squarely than at its foot, darker where
	 * less, and darker all over the deeper in the box it stands. */
	{
		cairo_t *lc = cairo_create(img);
		cairo_set_operator(lc, CAIRO_OPERATOR_ATOP);
		for (int m = 0; m < POST_CARD_ROWS; m++) {
			double r0 = ih * (1.0 - (double)(m + 1) / POST_CARD_ROWS);
			double r1 = ih * (1.0 - (double)m / POST_CARD_ROWS);
			double lm = (c->lit[m] + c->lit[m + 1]) * 0.5;
			double shade = (lm - lit0) * 2.2 - deep * 0.075;
			cairo_rectangle(lc, 0, r0, iw, r1 - r0);
			if (shade > 0.0) {
				cairo_set_source_rgba(lc, 1.0, 0.97, 0.90, fmin(shade, 0.35));
			} else {
				cairo_set_source_rgba(lc, 0.08, 0.05, 0.03, fmin(-shade, 0.6));
			}
			cairo_fill(lc);
		}
		cairo_destroy(lc);
		cairo_surface_flush(img);
	}
	for (int m = 0; m < POST_CARD_ROWS; m++) {
		/* the rows of the face this strip carries: row 0 of the face is its top */
		double r0 = ih * (1.0 - (double)(m + 1) / POST_CARD_ROWS);
		double r1 = ih * (1.0 - (double)m / POST_CARD_ROWS);
		double tlx = c->xl[m + 1], tly = c->y[m + 1];
		double trx = c->xr[m + 1], trY = c->y[m + 1];
		double blx = c->xl[m], bly = c->y[m];
		double brx = c->xr[m];
		if (fabs(bly - tly) < 0.01) {
			continue;   /* seen edge on */
		}
		cairo_save(cr);
		cairo_move_to(cr, tlx, tly);
		cairo_line_to(cr, trx, trY);
		/* a hair past its own bottom edge, so no seam shows between strips
		 * -- downwards on screen standing, upwards once it has gone over */
		double over = bly >= tly ? 0.6 : -0.6;
		cairo_line_to(cr, brx, bly + over);
		cairo_line_to(cr, blx, bly + over);
		cairo_close_path(cr);
		cairo_clip(cr);
		cairo_matrix_t mt;
		cairo_matrix_init(&mt, (trx - tlx) / iw, (trY - tly) / iw,
			(blx - tlx) / (r1 - r0), (bly - tly) / (r1 - r0), tlx, tly);
		cairo_transform(cr, &mt);
		cairo_translate(cr, 0.0, -r0);
		cairo_set_source_surface(cr, img, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint_with_alpha(cr, alpha);
		cairo_restore(cr);
	}
	/* the top edge of the sheet, catching the lamp: a paper has a thickness */
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	double x1 = c->qx[1] - (c->qx[1] - c->qx[0]) * cut;
	cairo_move_to(cr, c->qx[0] + 1.0, c->qy[0] + 0.5);
	cairo_line_to(cr, x1 - 1.0, c->qy[1] + 0.5);
	cairo_set_source_rgba(cr, 1.0, 0.99, 0.95, 0.85 * alpha);
	cairo_set_line_width(cr, 1.4);
	cairo_stroke(cr);
	cairo_move_to(cr, c->qx[0] + 1.0, c->qy[0] + 1.8);
	cairo_line_to(cr, x1 - 1.0, c->qy[1] + 1.8);
	cairo_set_source_rgba(cr, 0.35, 0.29, 0.22, 0.30 * alpha);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	cairo_restore(cr);
}

/* The dark in the narrow gap between a sheet and the one in front of it:
 * a band on the one behind, over the front one's top edge. */
static void post_card_gap(cairo_t *cr, const struct post_card *front, double alpha)
{
	double top = front->qy[0], band = (front->y[0] - front->y[POST_CARD_ROWS]) * 0.14;
	cairo_pattern_t *g = cairo_pattern_create_linear(0, top - band, 0, top + 2.0);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0.04, 0.025, 0.015, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 0.60, 0.04, 0.025, 0.015, 0.16 * alpha);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.04, 0.025, 0.015, 0.55 * alpha);
	cairo_set_source(cr, g);
	cairo_move_to(cr, front->qx[0] - 3.0, top + 2.0);
	cairo_line_to(cr, front->qx[1] + 3.0, top + 2.0);
	cairo_line_to(cr, front->qx[1] + 1.0, top - band);
	cairo_line_to(cr, front->qx[0] - 1.0, top - band);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
}

/* The length of the character at s if it is one a line does not begin
 * with -- a Japanese full stop or comma, a closing bracket, a middle dot, a
 * long vowel mark, and the Latin ones -- else 0. */
static size_t wrap_no_start(const char *s)
{
	static const char *const NO_START[] = {
		"\xe3\x80\x81", "\xe3\x80\x82", "\xef\xbc\x8c", "\xef\xbc\x8e",   /* 、。，． */
		"\xe3\x80\x8d", "\xe3\x80\x8f", "\xef\xbc\x89", "\xe3\x80\x91",   /* 」』）】 */
		"\xe3\x80\x89", "\xe3\x80\x8b", "\xe3\x83\xbb", "\xe3\x83\xbc",   /* 〉》・ー */
		"\xef\xbc\x81", "\xef\xbc\x9f", "\xe2\x80\xa6",                    /* ！？… */
		")", "]", ",", ".", "!", "?", ";", ":",
	};
	for (size_t i = 0; i < sizeof NO_START / sizeof *NO_START; i++) {
		size_t n = strlen(NO_START[i]);
		if (!strncmp(s, NO_START[i], n)) {
			return n;
		}
	}
	return 0;
}

/* Is the character at s, or the one before it (back), Chinese or
 * Japanese -- writing with no spaces, which a line may leave anywhere?
 * Not hangul: Korean is written in words with spaces between, and as in
 * the languages of Europe a word is not broken. */
static bool wrap_anywhere(const char *s, const char *start, bool back)
{
	if (back) {
		if (s <= start) {
			return false;
		}
		do {
			s--;
		} while (s > start && ((unsigned char)*s & 0xC0) == 0x80);
	}
	const unsigned char *u = (const unsigned char *)s;
	if ((u[0] & 0xF8) == 0xF0) {
		return true;   /* (beyond U+FFFF: the rarer ideographs) */
	}
	if ((u[0] & 0xF0) != 0xE0 || (u[1] & 0xC0) != 0x80 || (u[2] & 0xC0) != 0x80) {
		return false;
	}
	unsigned c = ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
	return (c >= 0x2E80 && c <= 0x312F) || (c >= 0x31F0 && c <= 0x9FFF)
		|| (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF);
}

/* s cut into lines no wider than w, at spaces where there are any and
 * anywhere in a run of writing that has none (Chinese, Japanese); at most
 * max. */
static int wrap_lines(cairo_t *cr, const char *s, double w, char out[][256], int max)
{
	int n = 0;
	const char *p = s;
	while (*p && n < max) {
		while (*p == ' ') {
			p++;
		}
		const char *best = NULL, *q = p;
		char buf[256];
		while (*q && *q != '\n') {
			const char *nx = q + 1;
			while (*nx && ((unsigned char)*nx & 0xC0) == 0x80) {
				nx++;
			}
			size_t len = (size_t)(nx - p);
			if (len >= sizeof buf) {
				break;
			}
			memcpy(buf, p, len);
			buf[len] = '\0';
			cairo_text_extents_t ext;
			cairo_text_extents(cr, buf, &ext);
			if (ext.x_advance > w && q > p) {
				break;
			}
			if (*q == ' ') {
				best = q;
			}
			q = nx;
		}
		const char *end = q;
		/* at the last space, if it is not so far back that the line comes
		 * out short -- in Japanese a space is rare and any place will do;
		 * in a word, at a space however short, rather than in the word */
		bool in_word = *q && *q != ' ' && !wrap_anywhere(q, p, false)
			&& !wrap_anywhere(q, p, true);
		if (in_word && best) {
			/* (unless the word is longer than a line, as a path is: it is
			 * broken anyway, and best where the line is full) */
			const char *we = best + 1;
			while (*we && *we != ' ' && *we != '\n' && we - best < 255) {
				we++;
			}
			while (we > best + 1 && ((unsigned char)*we & 0xC0) == 0x80) {
				we--;   /* not in the middle of a letter: cairo would refuse it */
			}
			memcpy(buf, best + 1, (size_t)(we - best - 1));
			buf[we - best - 1] = '\0';
			cairo_text_extents_t ext;
			cairo_text_extents(cr, buf, &ext);
			in_word = ext.x_advance <= w;
		}
		if (*q && *q != '\n' && best && best > p
			&& ((best - p) * 10 >= (q - p) * 6 || in_word)) {
			end = best;
		} else {
			/* but not before a full stop, a comma or a closing bracket, which
			 * do not begin a line: they hang off the end of this one */
			for (int k = 0; k < 2 && *end && *end != '\n'; k++) {
				size_t mark = wrap_no_start(end);
				if (!mark) {
					break;
				}
				end += mark;
			}
		}
		size_t len = (size_t)(end - p);
		if (len >= 256) {
			len = 255;
		}
		memcpy(out[n], p, len);
		out[n][len] = '\0';
		n++;
		p = end;
		if (*p == '\n') {
			p++;
		}
	}
	return n;
}

/* ---- what a letter says, emoji and all ------------------------------
 *
 * A letter's sender, heading and body are what a program sent, and that
 * has emoji in it as often as not. cairo's own text sets a string in one
 * face, which has none, and they came out as boxes with an X in them.
 * Pango falls back, a run at a time, to a face that has them -- in colour,
 * Noto Color Emoji, which the installers put in -- and to one for any
 * other script, Arabic joined up as it should be. The faces are the
 * typewriter's own, Latin and wide, and only the letters are set this
 * way. (barenakedchicken's PR #15 -- dentarthurdent on Discord.) */

static PangoLayout *note_layout(cairo_t *cr, const char *s, double size, bool bold)
{
	PangoLayout *pl = pango_cairo_create_layout(cr);
	char family[256];
	snprintf(family, sizeof family, "%s,%s", desk_font_family(DESK_FONT_TYPE, NULL),
		desk_font_family_wide(DESK_FONT_TYPE, NULL));
	PangoFontDescription *fd = pango_font_description_new();
	pango_font_description_set_family(fd, family);
	pango_font_description_set_weight(fd, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
	/* what was sent, as it was sent: never read as Pango's markup */
	pango_layout_set_text(pl, s ? s : "", -1);
	return pl;
}

static void note_layout_size(PangoLayout *pl, double size)
{
	PangoFontDescription *fd = pango_font_description_copy(pango_layout_get_font_description(pl));
	pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
}

/* One line of it -- who it is from, what it is about -- made smaller to
 * fit max_w, to about half its size, and cut short after that: x as
 * show_text_as has it by align, y its baseline. */
static void note_line(cairo_t *cr, const char *s, double x, double y, double size, bool bold,
	double r, double g, double b, double a, int align, double max_w)
{
	if (!s || !*s || max_w <= 0.0) {
		return;
	}
	PangoLayout *pl = note_layout(cr, s, size, bold);
	pango_layout_set_single_paragraph_mode(pl, TRUE);
	PangoRectangle box;
	double use = size;
	for (int i = 0; i < 24; i++) {
		note_layout_size(pl, use);
		pango_layout_get_extents(pl, NULL, &box);
		if ((double)box.width / PANGO_SCALE <= max_w || use < size * 0.55) {
			break;
		}
		use *= 0.94;
	}
	if ((double)box.width / PANGO_SCALE > max_w) {
		pango_layout_set_width(pl, (int)(max_w * PANGO_SCALE));
		pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
		pango_layout_get_extents(pl, NULL, &box);
	}
	double bw = (double)box.width / PANGO_SCALE;
	double dx = align == 0 ? -bw * 0.5 : align > 0 ? -bw : 0.0;
	cairo_save(cr);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_move_to(cr, x + dx, y);
	pango_cairo_show_layout_line(cr, pango_layout_get_line_readonly(pl, 0));
	cairo_restore(cr);
	g_object_unref(pl);
}

/* How many lines s takes, w wide at size: broken where the language breaks
 * (an emoji made of several, never in two; anywhere in a word too long). */
static int note_lines(cairo_t *cr, const char *s, double w, double size)
{
	PangoLayout *pl = note_layout(cr, s, size, false);
	pango_layout_set_width(pl, (int)(w * PANGO_SCALE));
	pango_layout_set_wrap(pl, PANGO_WRAP_WORD_CHAR);
	int n = pango_layout_get_line_count(pl);
	g_object_unref(pl);
	return n;
}

/* And set so: at most max lines, lh apart from the first baseline y -- the
 * ruled lines' pitch, not Pango's -- the last cut short if there is more. */
static void note_body(cairo_t *cr, const char *s, double x, double y, double w, double size,
	double lh, int max, double r, double g, double b, double a)
{
	if (!s || !*s || w <= 0.0 || max < 1) {
		return;
	}
	PangoLayout *pl = note_layout(cr, s, size, false);
	pango_layout_set_width(pl, (int)(w * PANGO_SCALE));
	pango_layout_set_wrap(pl, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_height(pl, -max);
	pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
	int n = pango_layout_get_line_count(pl);
	cairo_save(cr);
	cairo_set_source_rgba(cr, r, g, b, a);
	for (int i = 0; i < n && i < max; i++) {
		cairo_move_to(cr, x, y + i * lh);
		pango_cairo_show_layout_line(cr, pango_layout_get_line_readonly(pl, i));
	}
	cairo_restore(cr);
	g_object_unref(pl);
}

/* A letter as it stands in the box: letter paper, who it is from at the
 * top, what it is about under that, and the writing. */
static void post_card_face(cairo_t *cr, double w, double h, const struct desk_note *note,
	bool hot)
{
	unsigned seed = note->id * 2654435761u;
	fill_paper(cr, 0, 0, w, h, w * 0.012);
	cairo_save(cr);
	desk_rounded_path(cr, 0, 0, w, h, w * 0.012);
	cairo_clip(cr);
	paper_age(cr, w, h, seed, 0.20);
	double mx = w * 0.055;
	/* the writing is sized to the width of the sheet: h is only how much
	 * of it there is */
	const double u = w * 0.78;

	/* ruled, the way letter paper is */
	cairo_set_line_width(cr, fmax(1.0, u * 0.004));
	cairo_set_source_rgba(cr, 0.36, 0.52, 0.70, 0.22);
	for (double ry = u * 0.285; ry < h * 0.97; ry += u * 0.095) {
		cairo_move_to(cr, mx * 0.6, ry + u * 0.018);
		cairo_line_to(cr, w - mx * 0.6, ry + u * 0.018);
	}
	cairo_stroke(cr);

	/* The head of it, which is all that shows of the ones at the back:
	 * what it is about, and under that who it is from, beside the
	 * sender's mark; when it came, and the frank on what is unopened. */
	char initial[8];
	cairo_surface_t *ic = post_icon(note, initial, sizeof initial);
	double is = u * 0.125, ix = mx, iy = u * 0.030;
	if (ic) {
		cairo_save(cr);
		cairo_translate(cr, ix, iy);
		double iw = cairo_image_surface_get_width(ic);
		cairo_scale(cr, is / iw, is / iw);
		cairo_set_source_surface(cr, ic, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint_with_alpha(cr, 0.96);
		cairo_restore(cr);
	} else {
		cairo_set_source_rgba(cr, 0.22, 0.17, 0.13, 0.16);
		desk_rounded_path(cr, ix, iy, is, is, is * 0.18);
		cairo_fill(cr);
		show_text_as(cr, DESK_FONT_TYPE, initial, ix + is * 0.5, iy + is * 0.76, is * 0.70, true,
			0.26, 0.19, 0.14, 0.88, 0);
	}
	double tx = ix + is + w * 0.025;
	double l1 = u * 0.086, l2 = u * 0.138;
	char when[32] = "";
	struct tm tmv;
	if (note->at && localtime_r(&note->at, &tmv)) {
		fmt_hm(when, sizeof when, &tmv);
	}
	/* clear of the turned-down corner */
	double right = w - mx - w * 0.05;
	show_text_as(cr, DESK_FONT_TYPE, when, right, l1, u * 0.048, false, 0.42, 0.37, 0.31,
		0.85, 1);
	note_line(cr, note->summary[0] ? note->summary : "", tx, l1,
		u * 0.072, true,
		0.15, 0.12, 0.09, 0.96, -1, right - tx - u * 0.16);
	cairo_save(cr);
	desk_font_select(cr, DESK_FONT_TYPE, NULL, false, true);
	cairo_set_font_size(cr, u * 0.050);
	cairo_set_source_rgba(cr, 0.40, 0.34, 0.28, 0.90);
	cairo_move_to(cr, tx, l2);
	const char *from = TR("From");
	cairo_show_text(cr, from);
	cairo_text_extents_t fe;
	char from_sp[48];
	snprintf(from_sp, sizeof from_sp, "%s ", from);
	cairo_text_extents(cr, from_sp, &fe);
	cairo_restore(cr);
	note_line(cr, note->app[0] ? note->app : "\xe2\x80\x94",
		tx + fe.x_advance, l2,
		u * 0.052, true, 0.14, 0.18, 0.34, 0.92, -1, w - mx - tx - fe.x_advance);
	/* a hand-ruled line under the heading */
	cairo_set_source_rgba(cr, 0.30, 0.24, 0.18, 0.35);
	cairo_set_line_width(cr, fmax(1.0, u * 0.005));
	cairo_move_to(cr, mx, u * 0.172);
	cairo_line_to(cr, w - mx, u * 0.172);
	cairo_stroke(cr);

	/* and what it says, on the lines */
	if (note->body[0]) {
		int room = (int)((h * 0.97 - u * 0.27) / (u * 0.095)) + 1;
		note_body(cr, note->body, mx, u * 0.27, w - mx * 2.0, u * 0.064, u * 0.095,
			room < 5 ? (room > 0 ? room : 1) : 5, 0.26, 0.23, 0.30, 0.88);
	}
	if (note->urgency >= DESK_URGENCY_CRITICAL) {
		/* a red band down the edge: this one was sent express */
		cairo_set_source_rgba(cr, 0.68, 0.12, 0.09, 0.55);
		cairo_rectangle(cr, 0, 0, w * 0.014, h);
		cairo_fill(cr);
	}
	/* standing in a box, it gets less of the light at its sides, by the
	 * walls, and at its foot */
	for (int side = 0; side < 2; side++) {
		cairo_pattern_t *g = cairo_pattern_create_linear(side ? w : 0, 0,
			side ? w - w * 0.07 : w * 0.07, 0);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 0.08, 0.05, 0.03, 0.16);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 0.08, 0.05, 0.03, 0.0);
		cairo_set_source(cr, g);
		cairo_paint(cr);
		cairo_pattern_destroy(g);
	}
	{
		cairo_pattern_t *g = cairo_pattern_create_linear(0, h, 0, h * 0.84);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 0.08, 0.05, 0.03, 0.22);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 0.08, 0.05, 0.03, 0.0);
		cairo_set_source(cr, g);
		cairo_paint(cr);
		cairo_pattern_destroy(g);
	}
	if (hot) {
		cairo_set_source_rgba(cr, 1.0, 0.95, 0.80, 0.13);
		cairo_paint(cr);
	}
	paper_edge(cr, w, h, seed, 0.0);
	cairo_restore(cr);
}

/* ---- a letter's corner ----
 *
 * Every letter in the box has its top right corner turned down, as every
 * sheet on the desk has; what can be done with the letter is printed on
 * the back of it: take it out and read it, mark it read (or not), throw it
 * away -- and, where the program that sent it offered something of its
 * own to do about it, that, in place of the marking. */

/* The corner's sizes on a letter's face, w by h pixels. */
static void post_curl_sizes(double w, double h, double *rest, double *open)
{
	*rest = fmax(w * 0.045, 9.0);
	*open = fmin(w * 0.40, h * 0.90);
}

/* What the sender offers to do, if anything: its first action of its own,
 * else what clicking it is meant to do. */
const char *desk_post_letter_action(const struct desk_note *note, const char **label)
{
	int def = -1;
	for (int i = 0; i < note->n_actions; i++) {
		if (strcmp(note->action_key[i], "default") == 0) {
			def = i;
			continue;
		}
		if (label) {
			*label = note->action_label[i];
		}
		return note->action_key[i];
	}
	if (def >= 0) {
		if (label) {
			*label = TR("Open");
		}
		return note->action_key[def];
	}
	return NULL;
}

/* The instructions on the back of a letter's corner, top to bottom. */
static int post_corner_items(const struct desk_note *note, const char *words[3], int glyphs[3],
	bool warns[3], int codes[3])
{
	int n = 0;
	const char *label = NULL;
	const char *key = desk_post_letter_action(note, &label);
	if (key) {
		words[n] = label;
		glyphs[n] = CURL_G_ACTION;
		codes[n++] = DESK_LETTER_ACTION;
	}
	words[n] = TR("Read");
	glyphs[n] = CURL_G_READ;
	codes[n++] = DESK_LETTER_READ;
	if (!key) {
		words[n] = note->read ? TR("Mark unread") : TR("Mark read");
		glyphs[n] = note->read ? CURL_G_UNMARK : CURL_G_MARK;
		codes[n++] = DESK_LETTER_MARK;
	}
	words[n] = TR("Discard");
	glyphs[n] = DESK_CURL_DISCARD;
	codes[n++] = DESK_LETTER_DISCARD;
	for (int i = 0; i < n; i++) {
		warns[i] = codes[i] == DESK_LETTER_DISCARD;
	}
	return n;
}

/* The corner, turned down by cs, drawn over a letter's face: where the
 * paper was is cleared, so the letter behind shows through it. */
static void post_corner_paint(cairo_surface_t *face, const struct desk_note *note, double cs,
	double lift, int hot)
{
	double w = cairo_image_surface_get_width(face), h = cairo_image_surface_get_height(face);
	double rest, open;
	post_curl_sizes(w, h, &rest, &open);
	cs = clampf(cs, 0.0, h * 0.98);
	cairo_t *cr = cairo_create(face);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_move_to(cr, w - cs - 0.5, -1.0);
	cairo_line_to(cr, w + 1.0, -1.0);
	cairo_line_to(cr, w + 1.0, cs + 0.5);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	const char *words[3];
	int glyphs[3], codes[3];
	bool warns[3];
	int n = post_corner_items(note, words, glyphs, warns, codes);
	int hi = -1;
	for (int i = 0; i < n; i++) {
		if (codes[i] == hot) {
			hi = i;
		}
	}
	curl_draw(cr, w, cs, open, lift, hi, note->id * 2654435761u, n, words, glyphs, warns);
	cairo_destroy(cr);
	cairo_surface_flush(face);
}

/* A letter's face, drawn once and kept while nothing about it changes: a
 * turn of the letters moves them every frame, and drawing five faces of
 * text and paper each time was most of a frame. The copy handed out is the
 * caller's (the light is put on it). */
#define POST_FACE_KEEP 12
static struct {
	uint64_t key;
	cairo_surface_t *img;
	unsigned stamp;
} g_post_face[POST_FACE_KEEP];
static unsigned g_post_face_stamp;

static cairo_surface_t *post_face_copy(const struct desk_note *note, int fw, int fh, bool hot,
	bool back)
{
	uint64_t k = 14695981039346656037ull;
#define PF_MIX(p, n) do { const unsigned char *c_ = (const unsigned char *)(p); \
	for (size_t j_ = 0; j_ < (n); j_++) { k = (k ^ c_[j_]) * 1099511628211ull; } } while (0)
	int v[7] = { (int)note->id, note->read, note->urgency, fw, fh, hot | back << 1,
		(int)desk_lang_get() };
	PF_MIX(v, sizeof v);
	PF_MIX(&note->at, sizeof note->at);
	PF_MIX(note->app, strnlen(note->app, sizeof note->app));
	PF_MIX(note->summary, strnlen(note->summary, sizeof note->summary));
	PF_MIX(note->body, strnlen(note->body, sizeof note->body));
#undef PF_MIX
	int at = -1, old = 0;
	for (int i = 0; i < POST_FACE_KEEP; i++) {
		if (g_post_face[i].img && g_post_face[i].key == k) {
			at = i;
			break;
		}
		if (!g_post_face[i].img || (g_post_face[old].img
				&& g_post_face[i].stamp < g_post_face[old].stamp)) {
			old = i;
		}
	}
	if (at < 0) {
		at = old;
		if (g_post_face[at].img) {
			cairo_surface_destroy(g_post_face[at].img);
		}
		cairo_surface_t *face = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, fw, fh);
		cairo_t *fc = cairo_create(face);
		post_card_face(fc, fw, fh, note, hot);
		cairo_destroy(fc);
		cairo_surface_flush(face);
		if (back) {
			/* the back of the sheet, with what is written on the other side
			 * of it coming through */
			cairo_surface_t *bk = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, fw, fh);
			cairo_t *bc = cairo_create(bk);
			fill_paper(bc, 0, 0, fw, fh, fw * 0.012);
			cairo_save(bc);
			desk_rounded_path(bc, 0, 0, fw, fh, fw * 0.012);
			cairo_clip(bc);
			paper_age(bc, fw, fh, note->id * 2654435761u + 5u, 0.24);
			/* the lines of the other side run the same way here, for the
			 * strip laying it down already turns it over */
			cairo_translate(bc, fw, 0);
			cairo_scale(bc, -1.0, 1.0);
			cairo_set_source_surface(bc, face, 0, 0);
			cairo_paint_with_alpha(bc, 0.05);
			cairo_restore(bc);
			cairo_destroy(bc);
			cairo_surface_flush(bk);
			cairo_surface_destroy(face);
			face = bk;
		}
		g_post_face[at].img = face;
		g_post_face[at].key = k;
	}
	g_post_face[at].stamp = ++g_post_face_stamp;
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, fw, fh);
	cairo_t *cc = cairo_create(img);
	cairo_set_source_surface(cc, g_post_face[at].img, 0, 0);
	cairo_set_operator(cc, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cc);
	cairo_destroy(cc);
	cairo_surface_flush(img);
	return img;
}

/* Which letters stand in view for the place turned to: the one gone over
 * just in front of it, and up to four behind it (with the fixed ways of
 * standing them, the first RM_PB_FAN). */
static void post_shown(int n, double pos, int *first, int *last)
{
	if (post_lean_fixed()) {
		*first = 0;
		*last = (n < RM_PB_FAN ? n : RM_PB_FAN) - 1;
		return;
	}
	*first = (int)ceil(pos - 1.6);
	*last = (int)floor(pos + RM_PB_FAN - 0.4);
	if (*first < 0) {
		*first = 0;
	}
	if (*last > n - 1) {
		*last = n - 1;
	}
}

/* how much of a letter there is at t from the place: the one going over
 * out of view in front, and the one coming up from the bottom of the box
 * behind, come and go gradually */
static double post_fade(int n, double t)
{
	(void)n;
	if (post_lean_fixed()) {
		return 1.0;
	}
	return clampf((t + 1.6) / 0.6, 0.0, 1.0) * clampf((RM_PB_FAN - 0.4 - t) / 0.6, 0.0, 1.0);
}

void desk_draw_post(cairo_t *cr, int screen_w, int screen_h,
	const struct desk_note *notes, int n, int hover, double warm, double pos,
	const struct desk_post_curl *curl)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!post_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		desk_scene_warm(screen_w, screen_h, true);
		return;
	}
	struct lamp_px lamp_was = lamp_view(screen_w, screen_h);

	cairo_save(cr);
	cairo_translate(cr, ox, oy);
	cairo_scale(cr, scale, scale);
	cairo_set_source_surface(cr, rr->post, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	double sw = rr->w * scale, sh = rr->h * scale;
	int first, last;
	post_shown(n, pos, &first, &last);
	/* back to front, so the one at the front is drawn over the rest; and
	 * only inside the box: the tops of the ones at the back are behind
	 * the front of it */
	struct post_card c;
	/* every face the size of the front one standing at the front: one
	 * size, so that it can be kept */
	int fw = 0, fh = 0;
	if (post_card_at(rr, ox, oy, sw, sh, 0, 1, 0.0, &c)) {
		fw = (int)ceil(c.w);
		fh = (int)ceil(c.h);
	}
	if (last >= first && post_card_at(rr, ox, oy, sw, sh, first, n, pos, &c)) {
		/* inside the box's walls and under its top -- but not above the
		 * foot of the opening: one that has gone over forward is out over
		 * the door, which is down */
		cairo_save(cr);
		cairo_rectangle(cr, c.clip[0], c.clip[1], c.clip[2] - c.clip[0], screen_h - c.clip[1]);
		cairo_clip(cr);
		for (int i = last; i >= first; i--) {
			if (!post_card_at(rr, ox, oy, sw, sh, i, n, pos, &c)) {
				continue;
			}
			double a = clampf(warm, 0.0, 1.0) * post_fade(n, (double)i - pos);
			if (a <= 0.004 || fw < 8 || fh < 8) {
				continue;
			}
			cairo_surface_t *face = post_face_copy(&notes[i], fw, fh, hover == i, c.back);
			double cut = 0.0;
			if (!c.back) {
				/* its corner: turned down, or turned up under the hand */
				double rest, open;
				post_curl_sizes(fw, fh, &rest, &open);
				bool mine = curl && curl->idx == i;
				double cs = mine ? curl->s : rest;
				post_corner_paint(face, &notes[i], cs, mine ? curl->lift : 0.0,
					mine ? curl->hot : -1);
				cut = cs / fw;
			}
			double t = post_lean_fixed() ? (double)i : (double)i - pos;
			post_card_paint(cr, &c, face, a, (int)lround(fmax(t, 0.0)), cut);
			cairo_surface_destroy(face);
			/* and the dark of the gap between it and the one that goes in
			 * front of it next */
			struct post_card f;
			if (i > first && post_card_at(rr, ox, oy, sw, sh, i - 1, n, pos, &f)) {
				post_card_gap(cr, &f, a);
			}
		}
		cairo_restore(cr);
	}

	if (last < n - 1) {
		/* the rest are down in the box */
		char more[48];
		snprintf(more, sizeof more, TR("%d more below"), n - 1 - last);
		show_text_as(cr, DESK_FONT_TYPE, more, screen_w * 0.5, screen_h * 0.945, screen_h * 0.021,
			false, 0.74, 0.70, 0.62, 0.55 * warm, 0);
	} else if (n == 0) {
		show_text_as(cr, DESK_FONT_TYPE, TR("NO POST"), screen_w * 0.5, screen_h * 0.52,
			screen_h * 0.030, true, 0.72, 0.68, 0.60, 0.40 * warm, 0);
	}
	overlay_grain(cr, 0, 0, screen_w, screen_h, 0.45);
	g_light = lamp_was;
}

/* Where a letter lands on screen in the box's own view, as a box: the
 * compositor needs somewhere for the paper to start from when it comes
 * out. */
/* Where a point on screen is on letter i's face, in the face's pixels;
 * and how big the face is. False when the letter is not in view there or
 * the point is not on it. */
bool desk_post_on_letter(int screen_w, int screen_h, int i, int n, double pos,
	double px, double py, double *fx, double *fy, double *fw, double *fh)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!post_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	double sw = rr->w * scale, sh = rr->h * scale;
	struct post_card c;
	if (!post_card_at(rr, ox, oy, sw, sh, 0, 1, 0.0, &c)) {
		return false;
	}
	*fw = ceil(c.w);
	*fh = ceil(c.h);
	if (i < 0 || i >= n || !post_card_at(rr, ox, oy, sw, sh, i, n, pos, &c) || c.back) {
		return false;
	}
	/* a little off its edges still counts: a hand does not land on a line */
	for (int m = 0; m < POST_CARD_ROWS; m++) {
		double y0 = c.y[m], y1 = c.y[m + 1];
		double lo = fmin(y0, y1), hi = fmax(y0, y1);
		bool last = m == POST_CARD_ROWS - 1;
		if (py < lo - (last ? 8.0 : 0.0) || py > hi) {
			continue;
		}
		double f = fabs(y0 - y1) > 1e-6 ? clampf((py - y0) / (y1 - y0), 0.0, 1.0) : 0.0;
		if (last && py < lo) {
			f = 1.0 + (lo - py) / fmax(hi - lo, 1e-6);
		}
		double xl = c.xl[m] + (c.xl[m + 1] - c.xl[m]) * f;
		double xr = c.xr[m] + (c.xr[m + 1] - c.xr[m]) * f;
		if (xr - xl < 1.0) {
			return false;
		}
		*fx = (px - xl) / (xr - xl) * *fw;
		*fy = (1.0 - (m + f) / POST_CARD_ROWS) * *fh;
		return *fx >= -8.0 && *fx <= *fw + 8.0;
	}
	return false;
}

void desk_post_curl_sizes(double fw, double fh, double *rest, double *open)
{
	post_curl_sizes(fw, fh, rest, open);
}

/* Which instruction on the back of a letter's corner, turned up by s, is
 * at a point on its face: a DESK_LETTER_ code, or -1. */
int desk_post_curl_hit(const struct desk_note *note, double fw, double fh, double s,
	double fx, double fy)
{
	double rest, open;
	post_curl_sizes(fw, fh, &rest, &open);
	const char *words[3];
	int glyphs[3], codes[3];
	bool warns[3];
	int n = post_corner_items(note, words, glyphs, warns, codes);
	int i = curl_hit_n(fw, s, open, n, fx, fy);
	return i >= 0 ? codes[i] : -1;
}

bool desk_post_letters_area(int screen_w, int screen_h, double box[4])
{
	struct room_render *rr;
	double ox, oy, scale;
	double clip[4];
	if (!post_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)
		|| !post_opening(rr, ox, oy, rr->w * scale, rr->h * scale, clip)) {
		return false;
	}
	box[0] = floor(clip[0]) - 2.0;
	box[1] = floor(clip[1]) - 2.0;
	box[2] = ceil(clip[2]) - box[0] + 4.0;
	box[3] = screen_h - box[1];
	return true;
}

bool desk_post_letter_box(int screen_w, int screen_h, int i, int n, double pos,
	double *x, double *y, double *bw, double *bh)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!post_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	struct post_card c;
	if (i < 0 || i >= n
		|| !post_card_at(rr, ox, oy, rr->w * scale, rr->h * scale, i, n, pos, &c)) {
		return false;
	}
	/* as much of it as is in view: the top of it may be behind the box */
	double y0 = fmax(c.qy[0], c.clip[1]);
	*x = c.qx[2];
	*y = y0;
	*bw = c.qx[3] - c.qx[2];
	*bh = c.qy[2] - y0;
	return true;
}

/* The letter opened out: what was in the envelope, on a sheet you can put
 * down on the desk and read. */
cairo_surface_t *desk_letter_sheet(const struct desk_note *note,
	double w, double h)
{
	if (!note || w < 8 || h < 8) {
		return NULL;
	}
	cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		(int)w, (int)h);
	cairo_t *cr = cairo_create(sf);
	unsigned seed = note->id * 2654435761u + 11u;

	fill_paper(cr, 0, 0, w, h, 3.0);
	cairo_save(cr);
	desk_rounded_path(cr, 0, 0, w, h, 3.0);
	cairo_clip(cr);
	paper_age(cr, w, h, seed, 0.30);

	/* it was folded in three to go in the envelope, and it remembers */
	for (int i = 1; i < 3; i++) {
		double fy = h * i / 3.0;
		cairo_set_line_width(cr, 1.0);
		cairo_set_source_rgba(cr, 1.0, 0.98, 0.93, 0.34);
		cairo_move_to(cr, 0, fy - 1.0);
		cairo_line_to(cr, w, fy - 1.0);
		cairo_stroke(cr);
		cairo_set_source_rgba(cr, 0.30, 0.25, 0.19, 0.20);
		cairo_move_to(cr, 0, fy);
		cairo_line_to(cr, w, fy);
		cairo_stroke(cr);
	}

	/* the letterhead: who wrote, with their own mark on it */
	char initial[8];
	cairo_surface_t *ic = post_icon(note, initial, sizeof initial);
	double is = h * 0.085, ix = w * 0.075, iy = h * 0.070;
	if (ic) {
		cairo_save(cr);
		cairo_translate(cr, ix, iy);
		double iw = cairo_image_surface_get_width(ic);
		cairo_scale(cr, is / iw, is / iw);
		cairo_set_source_surface(cr, ic, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint_with_alpha(cr, 0.96);
		cairo_restore(cr);
	} else {
		cairo_set_source_rgba(cr, 0.22, 0.17, 0.13, 0.15);
		desk_rounded_path(cr, ix, iy, is, is, is * 0.18);
		cairo_fill(cr);
		show_text_as(cr, DESK_FONT_TYPE, initial, ix + is * 0.5, iy + is * 0.74, is * 0.66, true,
			0.26, 0.19, 0.14, 0.88, 0);
	}
	note_line(cr, note->app[0] ? note->app : "\xe2\x80\x94",
		ix + is + w * 0.030, iy + is * 0.70, h * 0.055, true,
		0.17, 0.14, 0.11, 0.95, -1, w * 0.60);

	char when[64];
	struct tm tmv;
	time_t t = note->at;
	if (localtime_r(&t, &tmv)) {
		fmt_hm(when, sizeof when, &tmv);
	} else {
		snprintf(when, sizeof when, "--:--");
	}
	show_text_as(cr, DESK_FONT_TYPE, when, w * 0.925, iy + is * 0.70, h * 0.048, false,
		0.40, 0.36, 0.32, 0.80, 1);

	wobble_line(cr, w * 0.075, h * 0.205, w * 0.925, h * 0.205, seed, 6, 0.6, 1.0,
		0.28, 0.22, 0.16, 0.55);

	/* what it says */
	note_line(cr, note->summary[0] ? note->summary : "",
		w * 0.075, h * 0.320, h * 0.072, true, 0.14, 0.12, 0.10, 0.96,
		-1, w * 0.85);

	/* the body, broken across the lines the way a typed letter breaks --
	 * at spaces, and anywhere in writing that has none */
	if (note->body[0]) {
		/* typed a little smaller when it is too long for the sheet (the
		 * same in Spanish runs a third longer than in English) */
		double size = h * 0.050;
		int room = 1;
		for (int k = 0; k < 6; k++) {
			room = (int)((h * 0.92 - h * 0.435) / (size * 1.24)) + 1;
			if (note_lines(cr, note->body, w * 0.85, size) <= room) {
				break;
			}
			size *= 0.95;
			room = (int)((h * 0.92 - h * 0.435) / (size * 1.24)) + 1;
		}
		note_body(cr, note->body, w * 0.075, h * 0.435, w * 0.85, size, size * 1.24,
			room < 12 ? room : 12, 0.30, 0.27, 0.24, 0.90);
	}

	paper_crease(cr, w, h, seed + 3, 0.05);
	paper_edge(cr, w, h, seed, 0.65);
	cairo_restore(cr);
	cairo_destroy(cr);
	cairo_surface_flush(sf);
	return sf;
}

/* ---- the letter on a first start ----------------------------------------
 *
 * Left in the middle of the desk while its room is made for the first time
 * (desk_scene_first_run): that it takes a while, a minute or two and more on
 * some machines, and that the desk can be used meanwhile -- where there was
 * nothing but the dark and a clock counting. The letter is the post box's
 * own, opened out (desk_letter_sheet), lying a little askew, with the shadow
 * a sheet lying on the desk has. In points: a bigger screen is more desk,
 * not a bigger letter. */
#define FIRST_LETTER_W_PT 380.0
#define FIRST_LETTER_H_PT 440.0
#define FIRST_LETTER_TILT (-0.022)

static void first_letter_size(int screen_w, int screen_h, double *lw, double *lh)
{
	double k = desk_px_per_pt(screen_w);
	*lw = FIRST_LETTER_W_PT * k;
	*lh = FIRST_LETTER_H_PT * k;
	/* never more than most of a small screen */
	double fit = fmin(1.0, fmin(screen_w * 0.8 / *lw, screen_h * 0.8 / *lh));
	*lw *= fit;
	*lh *= fit;
}

bool desk_first_letter_box(int screen_w, int screen_h, double box[4])
{
	if (screen_w < 64 || screen_h < 64) {
		return false;
	}
	double lw, lh;
	first_letter_size(screen_w, screen_h, &lw, &lh);
	/* the sheet turned, and its shadow down and to the right */
	double m = lh * 0.08 + 16.0;
	box[0] = floor((screen_w - lw) * 0.5 - m);
	box[1] = floor((screen_h - lh) * 0.5 - m);
	box[2] = ceil(lw + m * 2.0);
	box[3] = ceil(lh + m * 2.0);
	return true;
}

void desk_draw_first_letter(cairo_t *cr, int screen_w, int screen_h)
{
	double lw, lh;
	first_letter_size(screen_w, screen_h, &lw, &lh);
	double kx = 1.0, ky = 0.0;
	cairo_user_to_device_distance(cr, &kx, &ky);
	double k = fmax(hypot(kx, ky), 0.25);
	struct desk_note note;
	memset(&note, 0, sizeof note);
	note.id = 1;
	note.at = time(NULL);
	snprintf(note.app, sizeof note.app, "DEsk");
	snprintf(note.summary, sizeof note.summary, "%s", TR("Setting out the desk"));
	snprintf(note.body, sizeof note.body, "%s",
		TR("The first start can take a while: a minute or two, and longer on some "
		"machines. The desk works meanwhile; the bookcase, the Control Panel and the "
		"post box open as each is ready."));
	cairo_surface_t *sheet = desk_letter_sheet(&note, ceil(lw * k), ceil(lh * k));
	if (!sheet) {
		return;
	}
	double cx = screen_w * 0.5, cy = screen_h * 0.5;
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, FIRST_LETTER_TILT);
	/* lying on the desk: its shadow, soft, down and to the right of it */
	for (int i = 6; i >= 1; i--) {
		double g = i * lh * 0.006;
		desk_rounded(cr, -lw * 0.5 - g * 0.5 + lh * 0.012, -lh * 0.5 - g * 0.3 + lh * 0.020,
			lw + g, lh + g, 3.0 + g);
		cairo_set_source_rgba(cr, 0.05, 0.03, 0.02, 0.055);
		cairo_fill(cr);
	}
	cairo_translate(cr, -lw * 0.5, -lh * 0.5);
	cairo_scale(cr, 1.0 / k, 1.0 / k);
	cairo_set_source_surface(cr, sheet, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_surface_destroy(sheet);
}

/* Which letter is under the pointer, or -1. */
int desk_post_hit(int screen_w, int screen_h, int n, double pos, double px, double py)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!post_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return -1;
	}
	double sw = rr->w * scale, sh = rr->h * scale;
	int first, last;
	post_shown(n, pos, &first, &last);
	/* front to back: the one on top wins; and only inside the box */
	for (int i = first; i <= last; i++) {
		struct post_card c;
		if (!post_card_at(rr, ox, oy, sw, sh, i, n, pos, &c)) {
			continue;
		}
		if (px < c.clip[0] || px > c.clip[2] || py < c.clip[1]) {
			return -1;
		}
		if (post_fade(n, (double)i - pos) > 0.5 && cc_in_quad(c.qx, c.qy, px, py)) {
			return i;
		}
	}
	return -1;
}

/* The box's own quads, for "is the pointer on it" at either camera. */
static int pb_quads(struct room_render *rr, const struct room_cam *cam,
	double ox, double oy, double sw, double sh, double qx[2][4], double qy[2][4])
{
	(void)rr;
	int n = 0;
	for (int i = 0; i < 2; i++) {
		bool ok = true;
		for (int k = 0; k < 4; k++) {
			vec3 p;
			if (i == 0) {
				/* the cap, seen from above */
				p = pb_cap_to_world((k & 1) ? RM_PB_HX + 0.14 : -RM_PB_HX - 0.14,
					RM_PB_CAP_T * 0.5,
					(k & 2) ? -RM_PB_HZ - 0.14 : RM_PB_HZ + 0.14);
			} else {
				/* and the front of the body */
				p = v3(RM_PB_CX + ((k & 1) ? RM_PB_HX : -RM_PB_HX),
					(k & 2) ? RM_PB_PLINTH : RM_PB_SHOULDER,
					RM_PB_CZ + RM_PB_HZ);
			}
			double u, v;
			if (!room_project(cam, p, &u, &v)) {
				ok = false;
				break;
			}
			qx[i][k] = ox + u * sw;
			qy[i][k] = oy + v * sh;
		}
		if (ok) {
			n++;
		} else {
			for (int k = 0; k < 4; k++) {
				qx[i][k] = qy[i][k] = 0.0;
			}
		}
	}
	return n;
}

bool desk_post_desk_hit(int screen_w, int screen_h, double px, double py,
	double drawer_open)
{
	struct room_xf xf;
	if (!room_xform(screen_w, screen_h, drawer_open, &xf)) {
		return false;
	}
	double qx[2][4], qy[2][4];
	if (!pb_quads(xf.rr, &xf.rr->cam, xf.ox, xf.oy,
		xf.rr->w * xf.scale, xf.rr->h * xf.scale, qx, qy)) {
		return false;
	}
	return cc_in_quad(qx[0], qy[0], px, py) || cc_in_quad(qx[1], qy[1], px, py);
}

bool desk_post_shelf_hit(int screen_w, int screen_h, double px, double py)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!shelf_xform(screen_w, screen_h, &rr, &ox, &oy, &scale)) {
		return false;
	}
	double qx[2][4], qy[2][4];
	if (!pb_quads(rr, &rr->shelf_cam, ox, oy,
		rr->w * scale, rr->h * scale, qx, qy)) {
		return false;
	}
	return cc_in_quad(qx[0], qy[0], px, py) || cc_in_quad(qx[1], qy[1], px, py);
}

bool desk_post_view_hit(int screen_w, int screen_h, double px, double py)
{
	struct room_render *rr;
	double ox, oy, scale;
	if (!post_xform(screen_w, screen_h, &rr, &ox, &oy, &scale) || !rr->post_ids) {
		return false;
	}
	int sx = (int)((px - ox) / scale + 0.5);
	int sy = (int)((py - oy) / scale + 0.5);
	if (sx < 0 || sy < 0 || sx >= rr->w || sy >= rr->h) {
		return false;
	}
	/* generous: anywhere on the box or within a hand's width of it */
	double qx[2][4], qy[2][4];
	pb_quads(rr, &rr->post_cam, ox, oy, rr->w * scale, rr->h * scale, qx, qy);
	if (cc_in_quad(qx[0], qy[0], px, py) || cc_in_quad(qx[1], qy[1], px, py)) {
		return true;
	}
	return rr->post_ids[(size_t)sy * rr->w + sx] != 0;
}

/* What the desk sees of it: the nameplate on the cap, and -- when there is
 * something in the box that has not been read -- the corner of an envelope
 * that did not go all the way in. A flag would be the obvious signal and
 * the wrong one: raised, it is a line seen from straight above. A letter
 * standing in the slot is unmistakable from up there. */
void desk_draw_post_idle(cairo_t *cr, int screen_w, int screen_h,
	double drawer_open, int unread)
{
	struct room_xf xf;
	if (!room_xform(screen_w, screen_h, drawer_open, &xf)) {
		return;
	}
	struct room_render *rr = xf.rr;
	double sw = rr->w * xf.scale, sh = rr->h * xf.scale;

	/* The hood over the slot throws a band of shadow down the door under
	 * it: from above, the one sign that under the brass is an opening. */
	double hood_y = RM_PB_DOOR_Y0 + RM_PB_FS_Y + RM_PB_FS_HH + RM_PB_FS_FR - 0.01
		- RM_PB_HOOD_OUT * sin(RM_PB_HOOD_TILT);
	double hood_z = RM_PB_CZ + RM_PB_HZ + 0.14 + RM_PB_HOOD_OUT * cos(RM_PB_HOOD_TILT);
	{
		double hx = RM_PB_FS_HU + RM_PB_FS_FR + 0.02;
		vec3 q[4] = {
			v3(RM_PB_CX - hx, hood_y - 0.03, hood_z - 0.01),
			v3(RM_PB_CX + hx, hood_y - 0.03, hood_z - 0.01),
			v3(RM_PB_CX - hx, hood_y - 1.60, RM_PB_CZ + RM_PB_HZ + 0.11),
			v3(RM_PB_CX + hx, hood_y - 1.60, RM_PB_CZ + RM_PB_HZ + 0.11),
		};
		cairo_matrix_t mt;
		if (quad_matrix(&rr->cam, q, 1.0, 1.0, xf.ox, xf.oy, sw, sh, &mt)) {
			cairo_save(cr);
			cairo_transform(cr, &mt);
			cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, 1);
			cairo_pattern_add_color_stop_rgba(g, 0.0, 0.06, 0.02, 0.01, 0.62);
			cairo_pattern_add_color_stop_rgba(g, 0.35, 0.06, 0.02, 0.01, 0.20);
			cairo_pattern_add_color_stop_rgba(g, 1.0, 0.06, 0.02, 0.01, 0.0);
			cairo_set_source(cr, g);
			cairo_rectangle(cr, 0, 0, 1, 1);
			cairo_fill(cr);
			cairo_pattern_destroy(g);
			cairo_restore(cr);
		}
	}

	if (unread > 0) {
		/* What has come and not been read is sticking out of the slot:
		 * envelopes, pushed in as far as they would go and left, lying out
		 * over the desk, drooping a little, each at its own slant -- a
		 * slot is wider than a letter, and nobody posts one square. Up to
		 * three, the newest on top and furthest out. */
		int n = unread < 3 ? unread : 3;
		double more = clampf((unread - 1) / 4.0, 0.0, 1.0);
		double hz = RM_PB_CZ + RM_PB_HZ + 0.10;           /* the door's face */
		double y0 = RM_PB_DOOR_Y0 + RM_PB_FS_Y - RM_PB_FS_HH + 0.03;
		/* it shows from under the front edge of the hood, and a little of
		 * it under the hood, in its shadow */
		double in = hood_z - hz - 0.03;
		static const double TURN[3] = { -0.07, 0.13, -0.19 };
		static const double SLIDE[3] = { 0.10, -0.30, 0.34 };
		/* nothing of them above the front edge of the hood: they are
		 * under it there */
		double hu, hv;
		cairo_save(cr);
		if (room_project(&rr->cam, v3(RM_PB_CX, hood_y, hood_z), &hu, &hv)) {
			double top = xf.oy + hv * sh + 0.5;
			cairo_rectangle(cr, 0, top, 1e5, 1e5);
			cairo_clip(cr);
		}
		for (int k = n - 1; k >= 0; k--) {
			double out = 0.74 + 0.16 * more - k * 0.10;
			double hw = 0.92 - k * 0.05;
			double slide = SLIDE[k], turn = TURN[k];
			double y = y0 + (n - 1 - k) * 0.015;
			double droop = 0.12 + 0.03 * k;
			/* a sheet turns about where it goes into the slot */
			vec3 q[4];
			for (int c = 0; c < 4; c++) {
				bool tip = (c & 2) != 0;
				double dz = tip ? out : in;
				double dx = (c & 1) ? hw : -hw;
				double rx = dx * cos(turn) + dz * sin(turn);
				double rz = dz * cos(turn) - dx * sin(turn);
				q[c] = v3(RM_PB_CX + slide + rx, y - (tip ? droop : 0.0), hz + rz);
			}
			cairo_matrix_t mt;
			const double EW = 240.0, EH = 120.0;
			if (!quad_matrix(&rr->cam, q, EW, EH, xf.ox, xf.oy, sw, sh, &mt)) {
				continue;
			}
			/* its shadow, on the door and the desk under it */
			cairo_save(cr);
			cairo_transform(cr, &mt);
			for (int b = 3; b >= 1; b--) {
				cairo_set_source_rgba(cr, 0.05, 0.03, 0.02, 0.10);
				desk_rounded_path(cr, -6.0 * b + 10.0, 14.0 + 4.0 * b,
					EW + 12.0 * b - 20.0, EH - 10.0 + 4.0 * b, 6.0 * b);
				cairo_fill(cr);
			}
			cairo_restore(cr);

			struct face_print fe;
			if (!face_begin(&fe, cr, &mt, EW, EH)) {
				continue;
			}
			cairo_t *ec = fe.fc;
			unsigned seed = 17u + (unsigned)k * 11u;
			/* the envelope: square where it went into the slot, and at
			 * the end that is out a little lifted at the corners, the way
			 * paper that has been handled lies */
			cairo_new_path(ec);
			cairo_move_to(ec, 0, 0);
			cairo_line_to(ec, EW, 0);
			cairo_line_to(ec, EW, EH - 7.0);
			cairo_curve_to(ec, EW - 2.0, EH - 1.0, EW * 0.70, EH + 1.5, EW * 0.5, EH);
			cairo_curve_to(ec, EW * 0.30, EH + 1.5, 2.0, EH - 1.0, 0, EH - 7.0);
			cairo_close_path(ec);
			cairo_save(ec);
			cairo_clip_preserve(ec);
			double tone = (k == 1) ? 0.955 : 1.0;
			cairo_set_source_rgb(ec, 0.95 * tone, 0.93 * tone, 0.87 * tone);
			cairo_fill(ec);
			paper_age(ec, EW, EH, seed, 0.30);
			if (k == 1) {
				/* by air: the red and blue down its edges */
				for (double x = -EH; x < EW + EH; x += 26.0) {
					for (int e = 0; e < 2; e++) {
						double ey = e ? EH - 9.0 : 0.0;
						cairo_set_source_rgba(ec, ((int)(x / 26.0) & 1) ? 0.18 : 0.72,
							0.16, ((int)(x / 26.0) & 1) ? 0.56 : 0.16, 0.75);
						cairo_move_to(ec, x, ey);
						cairo_line_to(ec, x + 13.0, ey);
						cairo_line_to(ec, x + 22.0, ey + 9.0);
						cairo_line_to(ec, x + 9.0, ey + 9.0);
						cairo_close_path(ec);
						cairo_fill(ec);
					}
				}
			}
			if (k == 0) {
				/* the stamp, and the name and address */
				double sx = EW * 0.80, sy = EH * 0.40, ssz = EH * 0.36;
				cairo_set_source_rgba(ec, 0.96, 0.95, 0.90, 1.0);
				cairo_rectangle(ec, sx - 3.0, sy - 3.0, ssz * 0.8 + 6.0, ssz + 6.0);
				cairo_fill(ec);
				cairo_set_source_rgba(ec, 0.66, 0.16, 0.12, 0.92);
				cairo_rectangle(ec, sx, sy, ssz * 0.8, ssz);
				cairo_fill(ec);
				cairo_set_source_rgba(ec, 0.95, 0.90, 0.78, 0.9);
				cairo_arc(ec, sx + ssz * 0.4, sy + ssz * 0.5, ssz * 0.20, 0, 2 * M_PI);
				cairo_fill(ec);
				cairo_set_source_rgba(ec, 0.22, 0.20, 0.30, 0.55);
				cairo_set_line_width(ec, 3.0);
				for (int l = 0; l < 3; l++) {
					double ly = EH * (0.52 + l * 0.15);
					cairo_move_to(ec, EW * 0.24, ly);
					cairo_line_to(ec, EW * (0.62 - (l == 2) * 0.12), ly);
				}
				cairo_stroke(ec);
			}
			/* the hood's shadow across the end still in the slot, and the
			 * slot's own darkness at the very edge */
			cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, 0, EH);
			cairo_pattern_add_color_stop_rgba(g, 0.0, 0.06, 0.03, 0.02, 0.85);
			cairo_pattern_add_color_stop_rgba(g, 0.10, 0.06, 0.03, 0.02, 0.45);
			cairo_pattern_add_color_stop_rgba(g, 0.32, 0.06, 0.03, 0.02, 0.08);
			cairo_pattern_add_color_stop_rgba(g, 0.60, 0.06, 0.03, 0.02, 0.0);
			/* the sag: a little light where it bends, a little dark past it */
			cairo_pattern_add_color_stop_rgba(g, 0.86, 0.06, 0.03, 0.02, 0.07);
			cairo_pattern_add_color_stop_rgba(g, 1.0, 0.06, 0.03, 0.02, 0.0);
			cairo_set_source(ec, g);
			cairo_paint(ec);
			cairo_pattern_destroy(g);
			if (k > 0) {
				cairo_set_source_rgba(ec, 0.08, 0.05, 0.03, 0.10 * k);
				cairo_paint(ec);
			}
			cairo_restore(ec);
			/* its edge: a sheet has one */
			cairo_new_path(ec);
			cairo_move_to(ec, 0.5, 8.0);
			cairo_line_to(ec, 0.5, EH - 7.0);
			cairo_curve_to(ec, 2.0, EH - 1.0, EW * 0.30, EH + 1.0, EW * 0.5, EH - 0.5);
			cairo_curve_to(ec, EW * 0.70, EH + 1.0, EW - 2.0, EH - 1.0, EW - 0.5, EH - 7.0);
			cairo_line_to(ec, EW - 0.5, 8.0);
			cairo_set_source_rgba(ec, 0.30, 0.25, 0.18, 0.45);
			cairo_set_line_width(ec, 1.6);
			cairo_stroke(ec);
			face_end(&fe, cr, 1.0);
		}
		cairo_restore(cr);
	}

	/* the plate, on the cap */
	vec3 pq[4];
	for (int k = 0; k < 4; k++) {
		pq[k] = pb_cap_to_world((k & 1) ? RM_PB_HX - 0.10 : -RM_PB_HX + 0.10,
			RM_PB_CAP_T * 0.5 + 0.012,
			(k & 2) ? RM_PB_PLATE_S1 : RM_PB_PLATE_S0);
	}
	cairo_matrix_t mt;
	const double PW = 320.0, PH = 62.0;
	if (!quad_matrix(&rr->cam, pq, PW, PH, xf.ox, xf.oy, sw, sh, &mt)) {
		return;
	}
	struct face_print fpl;
	if (!face_begin(&fpl, cr, &mt, PW, PH)) {
		return;
	}
	cairo_t *fc = fpl.fc;
	double px = PW * 0.06, pw = PW * 0.88, py = PH * 0.20, ph = PH * 0.58;
	cairo_pattern_t *bg = cairo_pattern_create_linear(px, py, px, py + ph);
	cairo_pattern_add_color_stop_rgb(bg, 0.00, 0.726, 0.576, 0.292);
	cairo_pattern_add_color_stop_rgb(bg, 0.48, 0.548, 0.430, 0.216);
	cairo_pattern_add_color_stop_rgb(bg, 1.00, 0.638, 0.506, 0.258);
	cairo_set_source(fc, bg);
	desk_rounded_path(fc, px, py, pw, ph, PH * 0.12);
	cairo_fill(fc);
	cairo_pattern_destroy(bg);
	char lab[32];
	if (unread > 0) {
		snprintf(lab, sizeof lab, TR("POST %d \xe2\x86\x91"), unread);
	} else {
		snprintf(lab, sizeof lab, "%s", TR("POST \xe2\x86\x91"));
	}
	show_text_as(fc, DESK_FONT_TYPE, lab, PW * 0.5 + 1.0, PH * 0.665 + 1.0, PH * 0.40, true,
		0.92, 0.84, 0.62, 0.32, 0);
	show_text_as(fc, DESK_FONT_TYPE, lab, PW * 0.5, PH * 0.665, PH * 0.40, true,
		0.116, 0.084, 0.040, 0.92, 0);
	overlay_grain(fc, 0, 0, PW, PH, 0.9);
	face_end(&fpl, cr, 0.95);
}
