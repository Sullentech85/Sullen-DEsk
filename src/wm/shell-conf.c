/* Part of wm.c: shell.conf: the settings, read and applied.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* The standard terminal (DESK_STD_TERMINAL), or the first terminal on
 * $PATH if no program says it is one. */
static void terminal_open(void)
{
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = desk_apps_for_std(l, DESK_STD_TERMINAL);
	if (app) {
		desk_app_launch(app);
	}
	desk_apps_release(l);
	if (!app) {
		spawn(terminal_cmd());
	}
}

/* ---- the shell's own settings ----------------------------------------
 *
 * $XDG_CONFIG_HOME/desk/shell.conf, "key = value" a line, read at start
 * and again whenever it changes (looked at once a second). Shell Settings
 * writes it; so may anyone with an editor.
 *
 *   paper_for_all_windows = yes | no
 *       A program that draws its own frame and buttons (Firefox, GNOME's
 *       programs) is laid on the shell's paper anyway -- a margin of paper
 *       round it, its corner turned down, folded and thrown away like any
 *       other sheet -- or left as it draws itself. Yes unless it says no.
 *   paper.<program> = yes | no
 *       One program the other way: its desktop file id (with or without
 *       ".desktop"), its Wayland app id or its X class.
 *   throw_closed_windows = yes | no       a closed window is screwed up and
 *                                         thrown off the desk (yes), or goes
 *   corner_on_hover = yes | no            a corner turns up when the hand
 *                                         rests on its fold (yes), or only
 *                                         when it is clicked
 *   scale = auto | 1 | 1.25 | 1.5 | 2     how big things on the desk are:
 *                                         pixels to a point (DESK_SCALE,
 *                                         if set, comes first); a display
 *                                         tool's scale is written here
 *   motion = brisk | normal | gentle      how long everything that moves
 *                                         takes
 *   clock = 24h | 12h
 *   app.terminal = <desktop file id>      the standard terminal, and the
 *   app.browser, app.files, app.editor    other three of the drawer's
 *                                         things: what they open, instead
 *                                         of DEsk's choice (see apps.c)
 *   language = auto | en | ja | zh | ko | es | fr | de
 *                                         the desk's own words, and the
 *                                         names programs are filed under:
 *                                         auto follows the locale
 *   drawer_closes = yes | no              the drawer goes back in once
 *                                         something in it has been used
 *   take_sheet = yes | no                 a program taken out of ALL APPS
 *                                         has its sheet carried down to the
 *                                         desk, and its window comes onto it
 *   fold_into_drawer = yes | no           a window folded away goes into
 *                                         the drawer, not onto the desk
 *   sound = off | quiet | normal          the desk's sounds
 *   letter_sound = yes | no               a letter arriving in the post box
 *                                         is heard
 *   desk_mat = none | green | navy | burgundy | charcoal | cream | #rrggbb | leather
 *              | linoleum | photo | clear a mat on the desk under the windows
 *                                         (none): felt, green leather tooled in
 *                                         gold, linoleum; clear, a clear sheet over
 *                                         all the open desk, with pictures under it
 *   desk_mat_photo = /path/to/picture     the picture, for desk_mat = photo, and
 *                                         for a clear mat's whole
 *   desk_mat_size = small | medium | large | full, or 0% to 100%
 *                                         how much of the desk it covers (medium)
 *   desk_mat_under = free | whole         under a clear mat: prints laid where
 *                                         you like ($XDG_CONFIG_HOME/desk/desk-mat),
 *                                         or desk_mat_photo all over it (free)
 *   desk_wood = walnut | oak | cherry | maple | ebony
 *                                         what the desk is made of (walnut)
 *   desk_stain = natural | honey | brown | dark | grey
 *                                         over the desk's wood (natural); ebony
 *                                         is not stained brown or dark
 *   binder_ink = faded | normal | dark | darkest, or 0 to 1
 *                                         how dark the binders' labels are
 *                                         typed (normal)
 *   auto_rotate = yes | no                the built-in screen of a tablet turned the
 *                                         way it is held (yes)
 *   night_strength = light | normal | strong | strongest
 *                                         how warm NIGHT on the Control Panel
 *                                         makes the screens (normal)
 *   camera_bounce = normal | low | off    how far the camera goes past where
 *                                         it is looking and settles back, and
 *                                         how the head bobs walking up to the
 *                                         front door (off for anyone it makes
 *                                         feel unwell)
 *   lights_at_login = yes | no            the room starts dark at login and
 *                                         the lamp comes up as it is made
 *                                         (yes), or is lit at once
 *   screen_off = never | 1 | 2 | 5 | 10 | 15 | 30
 *                                         minutes nobody touches anything
 *                                         before the screen goes dark (10)
 *   lock_on_screen_off = yes | no         and the door comes down as it does
 *                                         (yes)
 *   onscreen_keyboard = auto | always | never  when the keyboard on the screen
 *                                         comes up by itself: with fingers and no
 *                                         keyboard to type on (auto), or always, or never
 *   touch_screens_swapped = yes | no      two touch screens on two screens alike, each
 *                                         working the other: the other way round (no)
 *   tap_to_click = yes | no               a touchpad: a tap is a click (yes)
 *   natural_scrolling = yes | no          a touchpad: the page goes the way
 *                                         the fingers do (no)
 *   natural_scrolling_mouse = yes | no    a mouse: the page goes the way the
 *                                         wheel turns (no)
 *   left_handed = yes | no                mice and touchpads: the buttons
 *                                         the other way round (no)
 *   pointer_speed = slow | normal | fast  or libinput's -1 to 1 (normal)
 *   keyboard_layout = system | <xkb>      the layout, as xkb names it (us,
 *                                         jp, de...); system: the one set
 *                                         for the machine (localectl)
 *   keyboard_second = none | <xkb>        a second layout to change to
 *   keyboard_switch = alt_shift | super_space | ctrl_shift | caps_lock
 *                                         the keys that change between them
 *   key_repeat = slow | normal | fast     how soon a held key repeats, and
 *                                         how fast */

static void shell_conf_path(char *out, size_t n)
{
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg) {
		snprintf(out, n, "%s/desk/shell.conf", cfg);
	} else if (home && *home) {
		snprintf(out, n, "%s/.config/desk/shell.conf", home);
	} else {
		out[0] = '\0';
	}
}

/* The directories a file kept in is to go in, the first time anything is
 * kept there. */
static void conf_dirs_for(const char *path)
{
	char dir[512];
	snprintf(dir, sizeof dir, "%s", path);
	for (char *p = dir + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(dir, 0755);
			*p = '/';
		}
	}
}

static bool conf_bool(const char *v, bool *out)
{
	if (!strcasecmp(v, "yes") || !strcasecmp(v, "true") || !strcasecmp(v, "on")
		|| !strcmp(v, "1")) {
		*out = true;
		return true;
	}
	if (!strcasecmp(v, "no") || !strcasecmp(v, "false") || !strcasecmp(v, "off")
		|| !strcmp(v, "0")) {
		*out = false;
		return true;
	}
	return false;
}

static char *conf_trim(char *s)
{
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n'
			|| s[n - 1] == '\r')) {
		s[--n] = '\0';
	}
	return s;
}

static void shell_conf_defaults(struct shell_conf *c)
{
	for (int i = 0; i < DESK_STD_COUNT; i++) {
		desk_apps_set_std(i, "");
	}
	c->paper_all = true;
	c->throw_closed = true;
	c->corner_hover = true;
	c->scale = 0.0;
	c->motion = 1.0;
	c->bounce = 1.0;
	c->clock_12h = false;
	c->binder_ink = 0.4;
	snprintf(c->desk_mat, sizeof c->desk_mat, "none");
	c->desk_mat_photo[0] = '\0';
	c->desk_mat_size = DESK_MAT_MEDIUM;
	c->desk_mat_under = DESK_MAT_UNDER_FREE;
	c->desk_wood = DESK_WOOD_WALNUT;
	c->desk_stain = DESK_STAIN_NATURAL;
	snprintf(c->language, sizeof c->language, "auto");
	c->drawer_closes = true;
	c->take_sheet = true;
	c->fold_drawer = false;
	c->sound = 1.0;
	c->letter_sound = true;
	c->night = 1.0;
	c->termites = false;
	c->termites_wait = 60;
	c->termites_level = 1;
	c->termites_windows = true;
	c->termites_packets = true;
	c->termites_keep = true;
	c->lights_at_login = true;
	c->auto_rotate = true;
	c->gfx = DESK_GFX_HIGH;
	c->gfx_own = false;
	c->gfx_set = false;
	c->bday_month = c->bday_day = 0;
	c->celebrations = true;
	c->weather = true;
	c->screen_off = 10;
	c->lock_screen_off = true;
	c->tap_click = true;
	c->natural_scroll = false;
	c->natural_wheel = false;
	c->left_handed = false;
	c->pointer_speed = 0.0;
	c->kb_layout[0] = c->kb_second[0] = '\0';
	snprintf(c->kb_switch, sizeof c->kb_switch, "grp:alt_shift_toggle");
	c->repeat_rate = 25;
	c->repeat_delay = 600;
	c->n_apps = 0;
}

/* The settings that are not yes or no. False if the key is not one. */
static bool conf_other(struct shell_conf *c, const char *path, const char *key, const char *val)
{
	if (!strcmp(key, "termites_wait")) {
		int sec = atoi(val);
		if (sec == 30 || sec == 60 || sec == 120 || sec == 300) {
			c->termites_wait = sec;
		}
		return true;
	}
	if (!strcmp(key, "termites_level")) {
		c->termites_level = !strcmp(val, "curious") ? 0 : !strcmp(val, "infestation") ? 2 : 1;
		return true;
	}
	if (!strcmp(key, "graphics")) {
		static const char *const LEVEL[DESK_GFX_COUNT] = { "2d", "low", "medium", "high",
			"ultra" };
		int g = -1;
		for (int i = 0; i < DESK_GFX_COUNT; i++) {
			if (!strcasecmp(val, LEVEL[i])) {
				g = i;
			}
		}
		if (!strcasecmp(val, "2d_fallback") || !strcasecmp(val, "fallback")) {
			g = DESK_GFX_2D;
		}
		if (g >= 0) {
			c->gfx = g;
			c->gfx_own = c->reading_own;
			c->gfx_set = true;
		} else {
			wlr_log(WLR_ERROR, "%s: graphics is ultra, high, medium, low or 2d, not \"%s\"",
				path, val);
		}
		return true;
	}
	if (!strcmp(key, "scale")) {
		char *end = NULL;
		double v = strtod(val, &end);
		if (!strcasecmp(val, "auto")) {
			c->scale = 0.0;
		} else if (end != val && v > 0.0) {
			/* 1.5, or 150% */
			c->scale = end && *end == '%' ? v / 100.0 : v;
			c->scale = c->scale < 0.5 ? 0.5 : (c->scale > 4.0 ? 4.0 : c->scale);
		} else {
			wlr_log(WLR_ERROR, "%s: scale is auto or a number, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "binder_ink")) {
		/* a word, as Shell Settings writes it, or a depth 0..1 by hand */
		char *end = NULL;
		double v = strtod(val, &end);
		if (!strcasecmp(val, "faded")) {
			c->binder_ink = 0.0;
		} else if (!strcasecmp(val, "normal")) {
			c->binder_ink = 0.4;
		} else if (!strcasecmp(val, "dark")) {
			c->binder_ink = 0.7;
		} else if (!strcasecmp(val, "darkest")) {
			c->binder_ink = 1.0;
		} else if (end != val && !*end && v >= 0.0 && v <= 1.0) {
			c->binder_ink = v;
		} else {
			wlr_log(WLR_ERROR, "%s: binder_ink is faded, normal, dark, darkest or 0 to 1, "
				"not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "desk_mat")) {
		struct desk_mat probe = { 0 };
		if (!strcasecmp(val, "none") || !strcasecmp(val, "photo") || !strcasecmp(val, "clear")
			|| desk_mat_preset(val, &probe)) {
			snprintf(c->desk_mat, sizeof c->desk_mat, "%s", val);
		} else {
			wlr_log(WLR_ERROR, "%s: desk_mat is none, green, navy, burgundy, charcoal, cream, "
				"#rrggbb, leather, linoleum, photo or clear, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "desk_wood")) {
		if (!desk_wood_parse(val, &c->desk_wood)) {
			wlr_log(WLR_ERROR, "%s: desk_wood is walnut, oak, cherry, maple or ebony, not \"%s\"",
				path, val);
		}
		return true;
	}
	if (!strcmp(key, "desk_stain")) {
		if (!desk_stain_parse(val, &c->desk_stain)) {
			wlr_log(WLR_ERROR, "%s: desk_stain is natural, honey, brown, dark or grey, not \"%s\"",
				path, val);
		}
		return true;
	}
	if (!strcmp(key, "desk_mat_under")) {
		if (!strcasecmp(val, "free")) {
			c->desk_mat_under = DESK_MAT_UNDER_FREE;
		} else if (!strcasecmp(val, "whole")) {
			c->desk_mat_under = DESK_MAT_UNDER_WHOLE;
		} else {
			wlr_log(WLR_ERROR, "%s: desk_mat_under is free or whole, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "desk_mat_photo")) {
		snprintf(c->desk_mat_photo, sizeof c->desk_mat_photo, "%s", val);
		return true;
	}
	if (!strcmp(key, "desk_mat_size")) {
		if (!desk_mat_size_parse(val, &c->desk_mat_size)) {
			wlr_log(WLR_ERROR, "%s: desk_mat_size is small, medium, large, full or a percentage, "
				"not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "night_strength")) {
		if (!strcasecmp(val, "light")) {
			c->night = 0.5;
		} else if (!strcasecmp(val, "normal")) {
			c->night = 1.0;
		} else if (!strcasecmp(val, "strong")) {
			c->night = 1.5;
		} else if (!strcasecmp(val, "strongest")) {
			c->night = 2.0;
		} else {
			wlr_log(WLR_ERROR, "%s: night_strength is light, normal, strong or strongest, "
				"not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "camera_bounce")) {
		if (!strcasecmp(val, "normal")) {
			c->bounce = 1.0;
		} else if (!strcasecmp(val, "low")) {
			c->bounce = 0.4;
		} else if (!strcasecmp(val, "off") || !strcasecmp(val, "none")
			|| !strcasecmp(val, "no")) {
			c->bounce = 0.0;
		} else {
			wlr_log(WLR_ERROR, "%s: camera_bounce is normal, low or off, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "motion")) {
		if (!strcasecmp(val, "brisk")) {
			c->motion = 0.70;
		} else if (!strcasecmp(val, "gentle")) {
			c->motion = 1.50;
		} else if (!strcasecmp(val, "normal")) {
			c->motion = 1.0;
		} else {
			wlr_log(WLR_ERROR, "%s: motion is brisk, normal or gentle, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "sound")) {
		if (!strcasecmp(val, "off") || !strcasecmp(val, "no")) {
			c->sound = 0.0;
		} else if (!strcasecmp(val, "quiet")) {
			c->sound = 0.45;
		} else if (!strcasecmp(val, "normal") || !strcasecmp(val, "yes")) {
			c->sound = 1.0;
		} else {
			wlr_log(WLR_ERROR, "%s: sound is off, quiet or normal, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strncmp(key, "app.", 4) && desk_std_parse(key + 4) >= 0) {
		/* auto: none set, the first of the usual ones (Shell Settings'
		 * Automatic) */
		desk_apps_set_std(desk_std_parse(key + 4), strcasecmp(val, "auto") ? val : "");
		return true;
	}
	if (!strcmp(key, "language")) {
		static const char *const CODES[] = { "auto", "en", "ja", "zh", "ko", "es", "fr", "de" };
		bool known = false;
		for (size_t i = 0; i < sizeof CODES / sizeof CODES[0]; i++) {
			known = known || !strcasecmp(val, CODES[i]);
		}
		if (known) {
			snprintf(c->language, sizeof c->language, "%s", val);
			for (char *p = c->language; *p; p++) {
				*p = (char)tolower((unsigned char)*p);
			}
		} else {
			wlr_log(WLR_ERROR, "%s: language is auto, en, ja, zh, ko, es, fr or de, not \"%s\"",
				path, val);
		}
		return true;
	}
	if (!strcmp(key, "birthday")) {
		/* 10-08, as Shell Settings writes it, or 1990-10-08 as it did for
		 * a day (its year let go); nothing, or none, for none. Only the
		 * month and the day: the year is the user's own. */
		int y = 0, m = 0, d = 0;
		if (!*val || !strcasecmp(val, "none") || !strcasecmp(val, "no")) {
			c->bday_month = c->bday_day = 0;
		} else if ((sscanf(val, "%d-%d-%d", &y, &m, &d) == 3 && y >= 1000 && m >= 1 && m <= 12
			&& d >= 1 && d <= 31) || (sscanf(val, "%d-%d", &m, &d) == 2 && m >= 1 && m <= 12
			&& d >= 1 && d <= 31)) {
			c->bday_month = m;
			c->bday_day = d;
		} else {
			wlr_log(WLR_ERROR, "%s: birthday is MM-DD, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "clock")) {
		if (!strcasecmp(val, "12h") || !strcmp(val, "12")) {
			c->clock_12h = true;
		} else if (!strcasecmp(val, "24h") || !strcmp(val, "24")) {
			c->clock_12h = false;
		} else {
			wlr_log(WLR_ERROR, "%s: clock is 24h or 12h, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "onscreen_keyboard")) {
		if (!strcasecmp(val, "auto")) {
			c->onscreen_keyboard = DESK_KEYS_AUTO;
		} else if (!strcasecmp(val, "always")) {
			c->onscreen_keyboard = DESK_KEYS_ALWAYS;
		} else if (!strcasecmp(val, "never")) {
			c->onscreen_keyboard = DESK_KEYS_NEVER;
		} else {
			wlr_log(WLR_ERROR, "%s: onscreen_keyboard is auto, always or never, not \"%s\"", path,
				val);
		}
		return true;
	}
	if (!strcmp(key, "pointer_speed")) {
		char *end = NULL;
		double v = strtod(val, &end);
		if (!strcasecmp(val, "slow")) {
			c->pointer_speed = -0.5;
		} else if (!strcasecmp(val, "normal")) {
			c->pointer_speed = 0.0;
		} else if (!strcasecmp(val, "fast")) {
			c->pointer_speed = 0.5;
		} else if (end != val && !*end && v >= -1.0 && v <= 1.0) {
			c->pointer_speed = v;
		} else {
			wlr_log(WLR_ERROR, "%s: pointer_speed is slow, normal, fast or -1 to 1, not \"%s\"",
				path, val);
		}
		return true;
	}
	if (!strcmp(key, "keyboard_layout") || !strcmp(key, "keyboard_second")) {
		bool first = key[9] == 'l';
		char *out = first ? c->kb_layout : c->kb_second;
		size_t n = first ? sizeof c->kb_layout : sizeof c->kb_second;
		/* an xkb layout's name: letters, digits, _ and - (and a variant
		 * in brackets, as us(intl)) */
		bool ok = strlen(val) < n;
		for (const char *q = val; ok && *q; q++) {
			ok = isalnum((unsigned char)*q) || strchr("_-()", *q);
		}
		if (!strcasecmp(val, first ? "system" : "none") || !*val) {
			out[0] = '\0';
		} else if (ok) {
			snprintf(out, n, "%s", val);
		} else {
			wlr_log(WLR_ERROR, "%s: %s is %s or an xkb layout, not \"%s\"", path, key,
				first ? "system" : "none", val);
		}
		return true;
	}
	if (!strcmp(key, "keyboard_switch")) {
		static const char *const SW[][2] = { { "alt_shift", "grp:alt_shift_toggle" },
			{ "super_space", "grp:win_space_toggle" }, { "ctrl_shift", "grp:ctrl_shift_toggle" },
			{ "caps_lock", "grp:caps_toggle" } };
		size_t i = 0;
		while (i < sizeof SW / sizeof SW[0] && strcasecmp(val, SW[i][0])) {
			i++;
		}
		if (i < sizeof SW / sizeof SW[0]) {
			snprintf(c->kb_switch, sizeof c->kb_switch, "%s", SW[i][1]);
		} else {
			wlr_log(WLR_ERROR, "%s: keyboard_switch is alt_shift, super_space, ctrl_shift or "
				"caps_lock, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "key_repeat")) {
		if (!strcasecmp(val, "slow")) {
			c->repeat_rate = 15;
			c->repeat_delay = 800;
		} else if (!strcasecmp(val, "normal")) {
			c->repeat_rate = 25;
			c->repeat_delay = 600;
		} else if (!strcasecmp(val, "fast")) {
			c->repeat_rate = 40;
			c->repeat_delay = 300;
		} else {
			wlr_log(WLR_ERROR, "%s: key_repeat is slow, normal or fast, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "screen_off")) {
		/* minutes; Shell Settings offers 1 to 30, an editor may say more */
		char *end = NULL;
		long v = strtol(val, &end, 10);
		if (!strcasecmp(val, "never")) {
			c->screen_off = 0;
		} else if (end != val && !*end && v >= 1 && v <= 24 * 60) {
			c->screen_off = (int)v;
		} else {
			wlr_log(WLR_ERROR, "%s: screen_off is never or minutes, not \"%s\"", path, val);
		}
		return true;
	}
	return false;
}

static void shell_conf_read_file(struct shell_conf *c, const char *path);

/* The machine's own defaults, under the user's: desk/shell.conf in the
 * first of $XDG_CONFIG_DIRS (/etc/xdg) that has one -- where the installer
 * writes the graphics it chose for the machine. */
static void shell_conf_system_path(char *out, size_t n)
{
	out[0] = '\0';
	const char *dirs = getenv("XDG_CONFIG_DIRS");
	if (!dirs || !*dirs) {
		dirs = "/etc/xdg";
	}
	while (*dirs) {
		size_t len = strcspn(dirs, ":");
		char path[512];
		if (len > 0 && (size_t)snprintf(path, sizeof path, "%.*s/desk/shell.conf", (int)len,
			dirs) < sizeof path && access(path, R_OK) == 0) {
			snprintf(out, n, "%s", path);
			return;
		}
		dirs += len + (dirs[len] == ':');
	}
}

static void shell_conf_load(struct shell_conf *c)
{
	shell_conf_defaults(c);
	char path[512];
	shell_conf_system_path(path, sizeof path);
	if (path[0]) {
		shell_conf_read_file(c, path);
	}
	shell_conf_path(path, sizeof path);
	if (path[0]) {
		c->reading_own = true;
		shell_conf_read_file(c, path);
		c->reading_own = false;
	}
	/* a stain not made for the wood (desk_stain_offered): natural, as the
	 * desk is drawn, so that it is no change to trace the room again for */
	if (!desk_stain_offered(c->desk_wood, c->desk_stain)) {
		wlr_log(WLR_INFO, "shell settings: ebony is not stained brown or dark brown; natural");
		c->desk_stain = DESK_STAIN_NATURAL;
	}
}

static void shell_conf_read_file(struct shell_conf *c, const char *path)
{
	FILE *f = fopen(path, "re");
	if (!f) {
		return;
	}
	char line[512];
	while (fgets(line, sizeof line, f)) {
		char *s = conf_trim(line);
		if (!*s || *s == '#') {
			continue;
		}
		char *eq = strchr(s, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		char *key = conf_trim(s), *val = conf_trim(eq + 1);
		if (conf_other(c, path, key, val)) {
			continue;
		}
		bool b;
		if (!conf_bool(val, &b)) {
			/* (every setting left is a yes or a no, or is nothing at all) */
			wlr_log(WLR_ERROR, "%s: %s is yes or no, not \"%s\"", path, key, val);
			continue;
		}
		if (!strcmp(key, "paper_for_all_windows")) {
			c->paper_all = b;
		} else if (!strcmp(key, "throw_closed_windows")) {
			c->throw_closed = b;
		} else if (!strcmp(key, "corner_on_hover")) {
			c->corner_hover = b;
		} else if (!strcmp(key, "drawer_closes")) {
			c->drawer_closes = b;
		} else if (!strcmp(key, "take_sheet")) {
			c->take_sheet = b;
		} else if (!strcmp(key, "fold_into_drawer")) {
			c->fold_drawer = b;
		} else if (!strcmp(key, "termites_enabled")) {
			c->termites = b;
		} else if (!strcmp(key, "termites_chew_windows")) {
			c->termites_windows = b;
		} else if (!strcmp(key, "termites_chew_folded")) {
			c->termites_packets = b;
		} else if (!strcmp(key, "termites_damage_persistent")) {
			c->termites_keep = b;
		} else if (!strcmp(key, "letter_sound")) {
			c->letter_sound = b;
		} else if (!strcmp(key, "lights_at_login")) {
			c->lights_at_login = b;
		} else if (!strcmp(key, "auto_rotate")) {
			c->auto_rotate = b;
		} else if (!strcmp(key, "celebrations")) {
			/* not on Shell Settings: the days are kept as a surprise, on
			 * their dates. This is for anyone who needs them not to be
			 * (the lamp going down, the show, the fireworks). */
			c->celebrations = b;
		} else if (!strcmp(key, "weather")) {
			c->weather = b;
		} else if (!strcmp(key, "lock_on_screen_off")) {
			c->lock_screen_off = b;
		} else if (!strcmp(key, "touch_screens_swapped")) {
			c->touch_swapped = b;
		} else if (!strcmp(key, "tap_to_click")) {
			c->tap_click = b;
		} else if (!strcmp(key, "natural_scrolling")) {
			c->natural_scroll = b;
		} else if (!strcmp(key, "natural_scrolling_mouse")) {
			c->natural_wheel = b;
		} else if (!strcmp(key, "left_handed")) {
			c->left_handed = b;
		} else if (!strncmp(key, "paper.", 6) && key[6]
			&& c->n_apps < (int)(sizeof c->apps / sizeof c->apps[0])) {
			snprintf(c->apps[c->n_apps].id, sizeof c->apps[0].id, "%s", key + 6);
			c->apps[c->n_apps].paper = b;
			c->n_apps++;
		} else {
			wlr_log(WLR_ERROR, "%s: nothing is called \"%s\"", path, key);
		}
	}
	fclose(f);
}

/* One setting written into shell.conf the way Shell Settings writes one:
 * its line changed, or added, and everything else left as it was. It is
 * read again from there like any other change (shell_conf_poll). */
static bool shell_conf_put(const char *key, const char *value)
{
	char path[512];
	shell_conf_path(path, sizeof path);
	if (!path[0]) {
		return false;
	}
	conf_dirs_for(path);
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *in = fopen(path, "re");
	FILE *out = fopen(tmp, "we");
	if (!out) {
		if (in) {
			fclose(in);
		}
		wlr_log(WLR_ERROR, "cannot write %s", tmp);
		return false;
	}
	bool done = false;
	if (in) {
		char line[512];
		while (fgets(line, sizeof line, in)) {
			char look[512];
			snprintf(look, sizeof look, "%s", line);
			char *s = conf_trim(look);
			char *eq = *s == '#' ? NULL : strchr(s, '=');
			if (eq) {
				*eq = '\0';
			}
			if (eq && !strcmp(conf_trim(s), key)) {
				if (!done) {
					fprintf(out, "%s = %s\n", key, value);
					done = true;
				}
				continue;
			}
			fputs(line, out);
		}
		fclose(in);
	} else {
		fputs("# DEsk's shell settings. Shell Settings writes this; so may you.\n", out);
	}
	if (!done) {
		fprintf(out, "%s = %s\n", key, value);
	}
	bool ok = fflush(out) == 0 && !ferror(out);
	ok = fclose(out) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		wlr_log(WLR_ERROR, "cannot write %s", path);
		unlink(tmp);
		return false;
	}
	return true;
}

/* A display tool's scale: the size of a point, which is Shell Settings'
 * to say -- so it is said there, where Shell Settings shows it and the
 * next start finds it, and holds from now on as if Shell Settings had set
 * it. In hundredths, as a person would put it. */
static void shell_conf_put_scale(struct desk_server *server, double scale)
{
	double v = round(scale * 100.0) / 100.0;
	char word[32];
	snprintf(word, sizeof word, "%g", v);
	server->conf.scale = v;
	if (shell_conf_put("scale", word)) {
		wlr_log(WLR_INFO, "shell.conf: scale = %s, as a display tool asked", word);
	}
}

static void shell_apply_all(struct desk_server *server);
static void input_apply(struct desk_server *server);
static void desk_relayout(struct desk_server *server);
static void idle_rearm(struct desk_server *server);
static void om_publish(struct desk_server *server);
static void orient_switched(struct desk_server *server);

/* Everything with words on it drawn again in another language: the names
 * of programs read again in it, every kept picture let go, and the desk
 * set out again as for a new screen (from the traced room, kept). */
/* Every picture kept of the desk's things let go, to be drawn again: the
 * binders' sheets, and every sprite's key moved on so none matches. */

static void pictures_forget(struct desk_server *server)
{
	desk_binder_forget();
	book_kept_forget(server);
	struct desk_sprite *all[] = { &server->cal_sp, &server->agd_sp, &server->agd_mark_sp,
		&server->pb_sprite, &server->memo_sp };
	for (size_t i = 0; i < sizeof all / sizeof *all; i++) {
		all[i]->key ^= 0x9e3779b97f4a7c15ull;
	}
	for (int i = 0; i < SH_LAYERS; i++) {
		server->sh_sprite[i].key ^= 0x9e3779b97f4a7c15ull;
	}
	for (int i = 0; i < DESK_CCP_COUNT; i++) {
		server->cc_sprite[i].key ^= 0x9e3779b97f4a7c15ull;
	}
	for (int i = 0; i < DESK_OVL_COUNT; i++) {
		server->ovl[i].key ^= 0x9e3779b97f4a7c15ull;
	}
}

static void lang_switch(struct desk_server *server, enum desk_lang lang, const char *setting)
{
	desk_lang_set(lang);
	desk_apps_set_language(strcmp(setting, "auto") ? desk_lang_code(lang) : "");
	desk_apps_rescan();
	pictures_forget(server);
	desk_draw_forget_words();
	shell_apply_all(server);
	desk_relayout(server);
}

/* Another level of drawing (desk_set_gfx has it already): every kept
 * picture drawn again (their shadows gone, or back), the windows' own
 * shadows, and the room traced again at the level -- the room that was
 * up stays up until the new one is there, as for a new screen. */
static void relight_begin(struct desk_server *server);
static void relight_end(struct desk_server *server);

/* The desk made of the wood Shell Settings says, to be traced in. */
static void wood_apply(struct desk_server *server)
{
	const struct shell_conf *c = &server->conf;
	desk_set_wood(c->desk_wood, c->desk_stain);
	server->wood_now = c->desk_wood;
	server->stain_now = c->desk_stain;
	wlr_log(WLR_INFO, "the desk is of another wood now: the room is traced again");
}

static void gfx_switch(struct desk_server *server)
{
	static const char *const NAME[DESK_GFX_COUNT] = { "2D", "Low", "Medium", "High",
		"Ultra" };
	wlr_log(WLR_INFO, "graphics: %s now", NAME[desk_gfx_level()]);
	pictures_forget(server);
	desk_lock_forget();
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->scene_tree && !view->unmanaged) {
			view->shadow.w = view->shadow.h = 0;   /* drawn again at the next look */
			view_sync_shadow(view);
		}
	}
	desk_relayout(server);
}

/* How much is drawn: as chosen -- and with the screen drawn on the CPU,
 * 2D Fallback unless the user chose otherwise in their own file (Shell
 * Settings writes only what is changed there): the machine's default, the
 * installer's guess, was a guess at a GPU that is not drawing. Chosen
 * nowhere -- no installer's look (a live USB, a build of one's own) and no
 * Shell Settings -- by graphics built into the processor, or a virtual
 * machine's, Medium: what is smooth on a desktop's card is heavy on them
 * (2026-11-16, the owner on real machines; desk-index --suggest has the
 * same). */
static int gfx_unchosen(const struct desk_server *server)
{
	return server->render_own || !server->render_name[0] ? DESK_GFX_HIGH : DESK_GFX_MEDIUM;
}

static int gfx_wanted(const struct desk_server *server)
{
	const struct shell_conf *c = &server->conf;
	return server->on_cpu && !c->gfx_own ? DESK_GFX_2D
		: !c->gfx_set ? gfx_unchosen(server) : c->gfx;
}

/* Once a second: read again if it has changed since it was last read. */
static void shell_conf_poll(struct desk_server *server)
{
	char path[512];
	shell_conf_path(path, sizeof path);
	struct stat st;
	bool there = path[0] && stat(path, &st) == 0;
	struct shell_conf *c = &server->conf;
	if (c->loaded && there == c->there && (!there || (st.st_mtim.tv_sec == c->mtime
			&& st.st_mtim.tv_nsec == c->mtime_ns && st.st_size == c->size))) {
		return;
	}
	c->there = there;
	if (there) {
		c->mtime = st.st_mtim.tv_sec;
		c->mtime_ns = st.st_mtim.tv_nsec;
		c->size = st.st_size;
	}
	bool first = !c->loaded;
	enum desk_lang was = desk_lang_get();
	double night_was = c->night;
	bool rotate_was = c->auto_rotate;
	shell_conf_load(c);
	if (!first && c->auto_rotate != rotate_was) {
		orient_switched(server);
	}
	if (!first && c->night != night_was && server->cc_night) {
		struct desk_output *no;
		wl_list_for_each(no, &server->outputs, link) {
			no->gamma_dirty = true;
		}
		server_schedule_frames(server);
	}
	c->loaded = true;
	idle_rearm(server);
	desk_sound_set_level(c->sound);
	enum desk_lang lang = desk_lang_parse(c->language);
	g_bounce = c->bounce;
	/* how much is drawn: before the first room is traced, at the level it
	 * is to be traced at */
	int want = gfx_wanted(server);
	bool gfx_changed = !first && want != (int)desk_gfx_level();
	desk_set_gfx(want);
	if (first) {
		motion_set(motion_of(c));
		desk_set_clock_12h(c->clock_12h);
		desk_set_binder_ink(c->binder_ink);
		mat_apply(server, c, false);
		desk_set_wood(c->desk_wood, c->desk_stain);
		server->wood_now = c->desk_wood;
		server->stain_now = c->desk_stain;
		/* before the first look at what is installed */
		desk_lang_set(lang);
		desk_apps_set_language(strcmp(c->language, "auto") ? desk_lang_code(lang) : "");
		char faces[512];
		desk_font_describe(faces, sizeof faces);
		wlr_log(WLR_INFO, "the desk speaks %s; text is set in %s", desk_lang_code(lang),
			faces);
	} else if (lang != was) {
		wlr_log(WLR_INFO, "the desk speaks %s now", desk_lang_code(lang));
		lang_switch(server, lang, c->language);
	} else {
		wlr_log(WLR_INFO, "shell settings read again: paper for all windows %s, %d by name",
			c->paper_all ? "yes" : "no", c->n_apps);
		shell_apply_all(server);
	}
	/* another wood: the room traced again in it, as for another level -- by
	 * itself, the lamp brought down over the room first and the eyes opened
	 * on the new one after (relight_begin) */
	bool wood_changed = !first && (c->desk_wood != server->wood_now
		|| c->desk_stain != server->stain_now);
	if (gfx_changed) {
		if (wood_changed) {
			relight_end(server);
			wood_apply(server);
		}
		gfx_switch(server);
	} else if (wood_changed) {
		relight_begin(server);
	}
	struct desk_occasion_conf oc = { c->bday_month, c->bday_day, c->celebrations, c->weather };
	desk_occasion_conf(&oc);
	if (!first && locked(server)) {
		paint_lock(server);
		server_schedule_frames(server);
	}
}

/* Is this program to be laid on paper when it draws its own frame? */
static bool view_wants_paper(struct desk_server *server, const struct desk_toplevel *view)
{
	const struct shell_conf *c = &server->conf;
	if (c->n_apps == 0) {
		return c->paper_all;
	}
	const char *names[4] = { NULL, NULL, NULL, NULL };
	char id[96] = "";
	if (view_app_id(view, id, sizeof id)) {
		names[0] = id;
	}
	if (view->xdg_toplevel) {
		names[1] = view->xdg_toplevel->app_id;
	}
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		names[2] = view->xsurface->class;
		names[3] = view->xsurface->instance;
	}
#endif
	for (int i = 0; i < c->n_apps; i++) {
		const char *want = c->apps[i].id;
		size_t wn = strlen(want);
		for (int k = 0; k < 4; k++) {
			const char *nm = names[k];
			if (!nm || !*nm) {
				continue;
			}
			if (!strcasecmp(nm, want)) {
				return c->apps[i].paper;
			}
			/* org.kde.kate.desktop is also org.kde.kate */
			size_t nn = strlen(nm);
			if (nn == wn + 8 && !strncasecmp(nm, want, wn) && !strcasecmp(nm + wn, ".desktop")) {
				return c->apps[i].paper;
			}
		}
	}
	return c->paper_all;
}

/* ---- whose frame a window has ----------------------------------------
 *
 * Ours, the sheet with its heading, for a program that asks for it; none
 * of ours for a splash screen, or for a program that draws its own frame
 * and buttons when the shell is told to leave those alone; and for a
 * program that draws its own and the shell is told to lay everything on
 * its paper (the default), a margin of paper round the program -- no
 * heading, the program has its own -- with the sheet's corner, its fold,
 * its shadow, and everything else a sheet does. */

static void view_repaint_chrome(struct desk_toplevel *view);
#ifdef DESK_HAVE_XWAYLAND
static bool xview_is_splash(const struct desk_server *server,
	const struct wlr_xwayland_surface *xs);
#endif
