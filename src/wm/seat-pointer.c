/* Part of wm.c: the seat and the pointer.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static void keyboard_handle_destroy(struct wl_listener *listener, void *data)
{
	struct desk_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	(void)data;
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

/* The keyboard as the machine is set up to have it. Left to itself
 * xkbcommon takes US, and on a Japanese keyboard that is more than the
 * odd symbol in the wrong place: the keys an input method is switched
 * with -- 半角/全角, 変換, 無変換, カタカナひらがな -- come out as an
 * accent and nothing, and there is no getting from A to あ. So: what the
 * environment says (XKB_DEFAULT_*, which xkbcommon reads itself), else
 * Debian's /etc/default/keyboard, else what systemd-localed wrote for X.
 * The strings point into a buffer that outlives the call. */
static char g_kb_buf[4][128];

static void kb_value(const char *line, const char *key, char *out, size_t n)
{
	size_t k = strlen(key);
	while (*line == ' ' || *line == '\t') {
		line++;
	}
	if (strncmp(line, key, k) || line[k] != '=') {
		return;
	}
	const char *v = line + k + 1;
	size_t i = 0;
	for (; *v && *v != '\n' && i + 1 < n; v++) {
		if (*v != '"' && *v != '\'') {
			out[i++] = *v;
		}
	}
	out[i] = '\0';
}

static bool kb_names(struct xkb_rule_names *names)
{
	if (getenv("XKB_DEFAULT_LAYOUT") && *getenv("XKB_DEFAULT_LAYOUT")) {
		return false;
	}
	memset(g_kb_buf, 0, sizeof g_kb_buf);
	char line[512];
	FILE *f = fopen("/etc/default/keyboard", "re");
	if (f) {
		while (fgets(line, sizeof line, f)) {
			kb_value(line, "XKBMODEL", g_kb_buf[0], sizeof g_kb_buf[0]);
			kb_value(line, "XKBLAYOUT", g_kb_buf[1], sizeof g_kb_buf[1]);
			kb_value(line, "XKBVARIANT", g_kb_buf[2], sizeof g_kb_buf[2]);
			kb_value(line, "XKBOPTIONS", g_kb_buf[3], sizeof g_kb_buf[3]);
		}
		fclose(f);
	}
	if (!g_kb_buf[1][0] && (f = fopen("/etc/X11/xorg.conf.d/00-keyboard.conf", "re"))) {
		static const char *const OPT[4] = { "XkbModel", "XkbLayout", "XkbVariant", "XkbOptions" };
		while (fgets(line, sizeof line, f)) {
			for (int i = 0; i < 4; i++) {
				char pat[32];
				snprintf(pat, sizeof pat, "\"%s\"", OPT[i]);
				char *at = strstr(line, pat);
				if (!at) {
					continue;
				}
				char *q0 = strchr(at + strlen(pat), '"'), *q1 = q0 ? strchr(q0 + 1, '"') : NULL;
				if (q0 && q1 && (size_t)(q1 - q0 - 1) < sizeof g_kb_buf[i]) {
					memcpy(g_kb_buf[i], q0 + 1, (size_t)(q1 - q0 - 1));
					g_kb_buf[i][q1 - q0 - 1] = '\0';
				}
			}
		}
		fclose(f);
	}
	if (!g_kb_buf[1][0]) {
		return false;
	}
	memset(names, 0, sizeof *names);
	names->model = g_kb_buf[0][0] ? g_kb_buf[0] : NULL;
	names->layout = g_kb_buf[1];
	names->variant = g_kb_buf[2][0] ? g_kb_buf[2] : NULL;
	names->options = g_kb_buf[3][0] ? g_kb_buf[3] : NULL;
	return true;
}

/* The keymap the keyboards are given: the layout Shell Settings names, or
 * the machine's (kb_names), and a second one with the keys that change to
 * it. desc says which, for knowing when it has changed. NULL if even the
 * default will not compile. */
static struct xkb_keymap *keyboard_keymap(const struct shell_conf *c, char *desc, size_t n)
{
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!context) {
		return NULL;
	}
	struct xkb_rule_names names;
	bool have = kb_names(&names);
	static char layout[80], variant[80], options[300];
	if (c->kb_layout[0] || c->kb_second[0]) {
		if (!have) {
			memset(&names, 0, sizeof names);
		}
		/* the machine's layout goes on being the first unless one is named */
		const char *base = c->kb_layout[0] ? c->kb_layout : have ? names.layout : "us";
		const char *base_var = c->kb_layout[0] ? "" : have && names.variant ? names.variant : "";
		const char *sys_opt = have && names.options ? names.options : "";
		if (c->kb_second[0]) {
			snprintf(layout, sizeof layout, "%s,%s", base, c->kb_second);
			snprintf(variant, sizeof variant, "%s,", base_var);
			snprintf(options, sizeof options, "%s%s%s", sys_opt, *sys_opt ? "," : "",
				c->kb_switch);
			names.options = options;
		} else {
			snprintf(layout, sizeof layout, "%s", base);
			snprintf(variant, sizeof variant, "%s", base_var);
		}
		names.layout = layout;
		names.variant = variant[0] && strcmp(variant, ",") ? variant : NULL;
		have = true;
	}
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, have ? &names : NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap && have) {
		/* a layout this xkbcommon does not know: the default, rather than none */
		wlr_log(WLR_ERROR, "keyboard: no layout \"%s\" here; using the default", names.layout);
		keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
		snprintf(desc, n, "the default");
	} else if (have) {
		snprintf(desc, n, "%s%s%s%s%s (%s)", names.layout, names.variant ? " " : "",
			names.variant ? names.variant : "", names.options ? ", " : "",
			names.options ? names.options : "", names.model ? names.model : "default model");
	} else {
		snprintf(desc, n, "the default");
	}
	xkb_context_unref(context);
	return keymap;
}

static void server_new_keyboard(struct desk_server *server, struct wlr_input_device *device)
{
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);
	struct desk_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (!keyboard) {
		return;
	}
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;
	char desc[sizeof server->kb_applied];
	struct xkb_keymap *keymap = keyboard_keymap(&server->conf, desc, sizeof desc);
	if (keymap) {
		wlr_keyboard_set_keymap(wlr_keyboard, keymap);
		xkb_keymap_unref(keymap);
	}
	if (strcmp(desc, server->kb_applied)) {
		wlr_log(WLR_INFO, "keyboard: %s", desc);
		snprintf(server->kb_applied, sizeof server->kb_applied, "%s", desc);
	}
	wlr_keyboard_set_repeat_info(wlr_keyboard, server->conf.repeat_rate,
		server->conf.repeat_delay);
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);
	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
}

/* A mouse or touchpad set up as shell.conf says. Only libinput's devices
 * can be (the nested backends' pointer is the host's to set). */
static void pointer_configure(struct desk_server *server, struct wlr_input_device *device)
{
#ifdef DESK_HAVE_LIBINPUT
	if (!wlr_input_device_is_libinput(device)) {
		return;
	}
	struct libinput_device *d = wlr_libinput_get_device_handle(device);
	if (!d) {
		return;
	}
	const struct shell_conf *c = &server->conf;
	/* a touchpad is what can be tapped */
	bool pad = libinput_device_config_tap_get_finger_count(d) > 0;
	if (pad) {
		libinput_device_config_tap_set_enabled(d,
			c->tap_click ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
	}
	/* the touchpad and the wheel each their own way: a laptop's pad
	 * natural and its mouse not is common */
	bool natural = pad ? c->natural_scroll : c->natural_wheel;
	if (libinput_device_config_scroll_has_natural_scroll(d)) {
		libinput_device_config_scroll_set_natural_scroll_enabled(d, natural);
	}
	/* what libinput took, for the log (a wheel it cannot turn round says no) */
	natural = libinput_device_config_scroll_has_natural_scroll(d)
		&& libinput_device_config_scroll_get_natural_scroll_enabled(d);
	if (libinput_device_config_accel_is_available(d)) {
		libinput_device_config_accel_set_speed(d, c->pointer_speed);
	}
	if (libinput_device_config_left_handed_is_available(d)) {
		libinput_device_config_left_handed_set(d, c->left_handed);
	}
	wlr_log(WLR_INFO, "pointer: %s (%s): %s%sspeed %.2f%s", device->name ? device->name : "?",
		pad ? "touchpad" : "mouse", pad ? (c->tap_click ? "tap to click, " : "no tapping, ") : "",
		natural ? "natural scrolling, " : "", c->pointer_speed,
		c->left_handed ? ", left-handed" : "");
#else
	(void)server;
	(void)device;
#endif
}

static void pointer_destroy(struct wl_listener *listener, void *data)
{
	struct desk_pointer *p = wl_container_of(listener, p, destroy);
	(void)data;
	wl_list_remove(&p->link);
	wl_list_remove(&p->destroy.link);
	free(p);
}

static void input_map_to_output(struct desk_server *server, struct wlr_input_device *device);

/* Is this screen the one built into the machine? */
static bool output_is_built_in(const struct wlr_output *out)
{
	return !strncmp(out->name, "eDP", 3) || !strncmp(out->name, "LVDS", 4)
		|| !strncmp(out->name, "DSI", 3);
}

static int by_name(const void *a, const void *b)
{
	const struct wlr_output *x = *(struct wlr_output *const *)a;
	const struct wlr_output *y = *(struct wlr_output *const *)b;
	return strcmp(x->name, y->name);
}

static bool touch_screen_taken(struct desk_server *server, const struct wlr_input_device *self,
	const struct wlr_output *out)
{
	struct desk_touch_device *t;
	wl_list_for_each(t, &server->touch_devices, link) {
		if (t->device != self && t->screen == out) {
			return true;
		}
	}
	return false;
}

static bool pen_screen_taken(struct desk_server *server, const struct wlr_input_device *self,
	const struct wlr_output *out)
{
	struct desk_tablet *t;
	wl_list_for_each(t, &server->tablets, link) {
		if (&t->wlr_tablet->base != self && t->screen == out) {
			return true;
		}
	}
	return false;
}

/* The screen a touch screen or a pen that does not say which it is must be on: the one of its
 * size, and one no other has; else one built in. Two of them on two screens alike (a machine
 * with two screens in it) are told apart only by going through both in order of name: and if
 * that is the wrong way round, touch_screens_swapped in shell.conf says so. */
static struct wlr_output *screen_of_size(struct desk_server *server, const struct wlr_input_device *self,
	double width_mm, double height_mm, bool any_built_in,
	bool (*taken)(struct desk_server *, const struct wlr_input_device *, const struct wlr_output *))
{
	struct wlr_output *out[16];
	size_t n = 0;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (n < sizeof out / sizeof *out && !o->off) {
			out[n++] = o->wlr_output;
		}
	}
	qsort(out, n, sizeof *out, by_name);
	for (int pass = 0; pass < (any_built_in ? 2 : 1); pass++) {
		for (size_t j = 0; j < n; j++) {
			struct wlr_output *try = out[server->conf.touch_swapped ? n - 1 - j : j];
			bool fits = pass == 0
				? width_mm > 1.0 && fabs(try->phys_width - width_mm) <= 8.0
					&& fabs(try->phys_height - height_mm) <= 8.0
				: output_is_built_in(try);
			if (fits && !taken(server, self, try)) {
				return try;
			}
		}
	}
	return NULL;
}

/* A touch screen spread over all the screens is on none of them. */
static struct wlr_output *touch_screen_guess(struct desk_server *server, struct wlr_touch *touch)
{
	struct wlr_output *out = screen_of_size(server, &touch->base, touch->width_mm, touch->height_mm, true,
		touch_screen_taken);
	return out ? out : first_output(server);
}

/* A pen's tablet the size of a screen is that screen. One of any other size is a tablet on the desk
 * beside the machine, and all the screens are on it. */
static struct wlr_output *pen_screen_guess(struct desk_server *server, struct wlr_tablet *tablet)
{
	return screen_of_size(server, &tablet->base, tablet->width_mm, tablet->height_mm, tablet->width_mm <= 1.0,
		pen_screen_taken);
}

/* All the touch screens over again, in order of name, whatever order they came in. */
static void touch_screens_map(struct desk_server *server)
{
	struct desk_touch_device *list[16];
	size_t n = 0;
	struct desk_touch_device *t;
	wl_list_for_each(t, &server->touch_devices, link) {
		t->screen = NULL;
		if (n < sizeof list / sizeof *list) {
			list[n++] = t;
		}
	}
	for (size_t i = 1; i < n; i++) {
		struct desk_touch_device *x = list[i];
		size_t j = i;
		while (j > 0 && strcmp(list[j - 1]->device->name ? list[j - 1]->device->name : "",
				x->device->name ? x->device->name : "") > 0) {
			list[j] = list[j - 1];
			j--;
		}
		list[j] = x;
	}
	for (size_t i = 0; i < n; i++) {
		input_map_to_output(server, list[i]->device);
	}
	/* and the pens, the same way */
	struct desk_tablet *pens[16];
	size_t np = 0;
	struct desk_tablet *pt;
	wl_list_for_each(pt, &server->tablets, link) {
		pt->screen = NULL;
		if (np < sizeof pens / sizeof *pens) {
			pens[np++] = pt;
		}
	}
	for (size_t i = 1; i < np; i++) {
		struct desk_tablet *x = pens[i];
		size_t j = i;
		while (j > 0 && strcmp(pens[j - 1]->wlr_tablet->base.name ? pens[j - 1]->wlr_tablet->base.name : "",
				x->wlr_tablet->base.name ? x->wlr_tablet->base.name : "") > 0) {
			pens[j] = pens[j - 1];
			j--;
		}
		pens[j] = x;
	}
	for (size_t i = 0; i < np; i++) {
		input_map_to_output(server, &pens[i]->wlr_tablet->base);
	}
}

/* A device that belongs to one screen -- a touch screen, or the pointer
 * of one window of a nested desk -- has its place on that screen, not
 * spread over all of them: once the screen it names is there. */
static void input_map_to_output(struct desk_server *server, struct wlr_input_device *device)
{
	const char *name = NULL;
	if (device->type == WLR_INPUT_DEVICE_POINTER) {
		name = wlr_pointer_from_input_device(device)->output_name;
	} else if (device->type == WLR_INPUT_DEVICE_TOUCH) {
		name = wlr_touch_from_input_device(device)->output_name;
	}
	struct wlr_output *out = NULL;
	struct desk_output *o;
	if (name) {
		wl_list_for_each(o, &server->outputs, link) {
			if (!strcmp(o->wlr_output->name, name)) {
				out = o->wlr_output;
				break;
			}
		}
	} else if (device->type == WLR_INPUT_DEVICE_TOUCH) {
		out = touch_screen_guess(server, wlr_touch_from_input_device(device));
	} else if (device->type == WLR_INPUT_DEVICE_TABLET) {
		struct wlr_tablet *tablet = wlr_tablet_from_input_device(device);
		out = pen_screen_guess(server, tablet);
		struct desk_tablet *t = tablet->data;
		if (t) {
			t->screen = out;
		}
		wlr_cursor_map_input_to_output(server->cursor, device, out);
		wlr_log(WLR_INFO, "pen: %s is on %s", device->name ? device->name : "a tablet",
			out ? out->name : "all the screens");
		return;
	}
	if (out) {
		wlr_cursor_map_input_to_output(server->cursor, device, out);
		if (device->type == WLR_INPUT_DEVICE_TOUCH) {
			struct desk_touch_device *t;
			wl_list_for_each(t, &server->touch_devices, link) {
				if (t->device == device) {
					t->screen = out;
				}
			}
			wlr_log(WLR_INFO, "touch: %s is on %s", device->name ? device->name : "a touch screen",
				out->name);
		}
	}
}

static void server_new_pointer(struct desk_server *server, struct wlr_input_device *device)
{
	wlr_cursor_attach_input_device(server->cursor, device);
	input_map_to_output(server, device);
	struct desk_pointer *p = calloc(1, sizeof *p);
	if (!p) {
		return;
	}
	p->server = server;
	p->device = device;
	p->destroy.notify = pointer_destroy;
	wl_signal_add(&device->events.destroy, &p->destroy);
	wl_list_insert(&server->pointers, &p->link);
	pointer_configure(server, device);
}

/* Shell Settings changed something: the keyboards and pointers already
 * here are set up again -- a new keymap only if the layouts changed. */
static void input_apply(struct desk_server *server)
{
	char desc[sizeof server->kb_applied];
	struct xkb_keymap *keymap = NULL;
	struct desk_keyboard *kb;
	wl_list_for_each(kb, &server->keyboards, link) {
		if (!keymap) {
			keymap = keyboard_keymap(&server->conf, desc, sizeof desc);
			if (!keymap || !strcmp(desc, server->kb_applied)) {
				break;
			}
			wlr_log(WLR_INFO, "keyboard: %s", desc);
			snprintf(server->kb_applied, sizeof server->kb_applied, "%s", desc);
		}
		wlr_keyboard_set_keymap(kb->wlr_keyboard, keymap);
	}
	if (keymap) {
		xkb_keymap_unref(keymap);
	}
	wl_list_for_each(kb, &server->keyboards, link) {
		wlr_keyboard_set_repeat_info(kb->wlr_keyboard, server->conf.repeat_rate,
			server->conf.repeat_delay);
	}
	touch_screens_map(server);
	struct desk_pointer *p;
	wl_list_for_each(p, &server->pointers, link) {
		pointer_configure(server, p->device);
	}
}

/* What programs are told this seat has. */
static void seat_says_what_it_has(struct desk_server *server)
{
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	if (!wl_list_empty(&server->touch_devices)) {
		caps |= WL_SEAT_CAPABILITY_TOUCH;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void touch_device_gone(struct wl_listener *listener, void *data)
{
	struct desk_touch_device *t = wl_container_of(listener, t, destroy);
	(void)data;
	wl_list_remove(&t->link);
	wl_list_remove(&t->destroy.link);
	seat_says_what_it_has(t->server);
	free(t);
}

static void server_new_touch(struct desk_server *server, struct wlr_input_device *device)
{
	wlr_cursor_attach_input_device(server->cursor, device);
	struct desk_touch_device *t = calloc(1, sizeof *t);
	if (!t) {
		return;
	}
	t->server = server;
	t->device = device;
	t->destroy.notify = touch_device_gone;
	wl_signal_add(&device->events.destroy, &t->destroy);
	wl_list_insert(&server->touch_devices, &t->link);
	touch_screens_map(server);
	/* with nothing else to type on, the seat has a keyboard all the same: programs ask for
	 * what to type before there is anything to see */
	if (wl_list_empty(&server->keyboards) && desk_touch_there(server)) {
		desk_onscreen_keyboard(server);
	}
	drawer_things_ensure(server);
	if (server->desk_w > 0) {
		drawer_layout(server, false);
	}
}

/* A laptop folded back, by its own switch: its keyboard is not to be typed on. */
struct desk_switch {
	struct wl_list link;
	struct desk_server *server;
	struct wl_listener toggle, destroy;
};

static void switch_toggled(struct wl_listener *listener, void *data)
{
	struct desk_switch *sw = wl_container_of(listener, sw, toggle);
	struct wlr_switch_toggle_event *event = data;
	if (event->switch_type == WLR_SWITCH_TYPE_TABLET_MODE) {
		sw->server->tablet_mode = event->switch_state == WLR_SWITCH_STATE_ON;
		wlr_log(WLR_INFO, "tablet mode: %s", sw->server->tablet_mode ? "on" : "off");
	}
}

static void switch_gone(struct wl_listener *listener, void *data)
{
	struct desk_switch *sw = wl_container_of(listener, sw, destroy);
	(void)data;
	wl_list_remove(&sw->toggle.link);
	wl_list_remove(&sw->destroy.link);
	wl_list_remove(&sw->link);
	free(sw);
}

static void server_new_switch(struct desk_server *server, struct wlr_input_device *device)
{
	struct desk_switch *sw = calloc(1, sizeof *sw);
	if (!sw) {
		return;
	}
	sw->server = server;
	sw->toggle.notify = switch_toggled;
	wl_signal_add(&wlr_switch_from_input_device(device)->events.toggle, &sw->toggle);
	sw->destroy.notify = switch_gone;
	wl_signal_add(&device->events.destroy, &sw->destroy);
	wl_list_insert(&server->switches, &sw->link);
}

static void tablet_gone(struct wl_listener *listener, void *data)
{
	struct desk_tablet *t = wl_container_of(listener, t, destroy);
	(void)data;
	struct desk_pen *pen;
	wl_list_for_each(pen, &t->server->pens, link) {
		if (pen->tablet == t) {
			if (pen->near) {
				t->server->pens_near--;
				pen->near = false;
			}
			pen->on_program = false;
			pen->tablet = NULL;
		}
	}
	t->wlr_tablet->data = NULL;
	wl_list_remove(&t->link);
	wl_list_remove(&t->destroy.link);
	free(t);
}

static void server_new_tablet(struct desk_server *server, struct wlr_input_device *device)
{
	struct wlr_tablet *tablet = wlr_tablet_from_input_device(device);
	struct desk_tablet *t = calloc(1, sizeof *t);
	if (!t) {
		return;
	}
	t->server = server;
	t->wlr_tablet = tablet;
	t->v2 = wlr_tablet_create(server->tablet_mgr, server->seat, device);
	tablet->data = t;
	t->destroy.notify = tablet_gone;
	wl_signal_add(&device->events.destroy, &t->destroy);
	wl_list_insert(&server->tablets, &t->link);
	wlr_cursor_attach_input_device(server->cursor, device);
	touch_screens_map(server);
	wlr_log(WLR_INFO, "pen: a tablet, %s, %.0f by %.0f mm", device->name ? device->name : "with no name",
		tablet->width_mm, tablet->height_mm);
	/* a pen can type as well as a finger can */
	if (wl_list_empty(&server->keyboards) && desk_pen_there(server)) {
		desk_onscreen_keyboard(server);
	}
	drawer_things_ensure(server);
	if (server->desk_w > 0) {
		drawer_layout(server, false);
	}
}

static void server_new_input(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		server_new_touch(server, device);
		break;
	case WLR_INPUT_DEVICE_TABLET:
		server_new_tablet(server, device);
		break;
	case WLR_INPUT_DEVICE_SWITCH:
		server_new_switch(server, device);
		break;
	default:
		break;
	}
	seat_says_what_it_has(server);
}

/* ---- Game pointer support ------------------------------------------------
 * This intentionally handles only the focused surface. On keyboard focus
 * changes and pointer leave, a lock is relinquished. No game receives raw
 * mouse events while another program has focus.
 */
/* A game letting go of the pointer it held still may say where over its
 * window it would like it to be: there. */
static void constraint_hint_warp(struct desk_server *server, struct wlr_pointer_constraint_v1 *c)
{
	if (c->type != WLR_POINTER_CONSTRAINT_V1_LOCKED || !c->current.cursor_hint.enabled
		|| !c->surface || !c->surface->mapped) {
		return;
	}
	struct desk_toplevel *view = view_of_surface(server, c->surface);
	struct wlr_scene_node *node = view ? view_content_node(view) : NULL;
	int lx, ly;
	if (!node || !wlr_scene_node_coords(node, &lx, &ly)) {
		return;
	}
	double hx = c->current.cursor_hint.x, hy = c->current.cursor_hint.y;
	wlr_cursor_warp(server->cursor, NULL, lx + hx, ly + hy);
	wlr_seat_pointer_warp(server->seat, hx, hy);
	wlr_log(WLR_INFO, "a game let go of the pointer, at %.0f,%.0f on its window", hx, hy);
}

static void desk_constraint_set(struct desk_server *server,
	struct wlr_pointer_constraint_v1 *next)
{
	struct wlr_pointer_constraint_v1 *old = server->active_constraint;
	if (next == old) {
		return;
	}
	server->active_constraint = NULL;
	if (old) {
		constraint_hint_warp(server, old);
		/* A one-shot constraint can be destroyed by deactivation. */
		wlr_pointer_constraint_v1_send_deactivated(old);
	}
	if (next) {
		server->active_constraint = next;
		/* a pointer that says where it is moves from where the cursor
		 * is now */
		server->abs_x = server->cursor->x;
		server->abs_y = server->cursor->y;
		server->abs_seen = true;
		wlr_pointer_constraint_v1_send_activated(next);
		if (next->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
			wlr_cursor_unset_image(server->cursor);
		}
	} else {
		cursor_image(server, "default");
	}
}

static void desk_constraint_refresh(struct desk_server *server)
{
	struct wlr_surface *under = server->seat->pointer_state.focused_surface;
	struct wlr_surface *keys = server->seat->keyboard_state.focused_surface;
	struct wlr_pointer_constraint_v1 *want = NULL;
	/* and none while you are looking up from the game: at the bookcase, the
	 * Control Panel, the post box, or into the drawer, the hand is yours */
	bool away = shelf_up(server) || console_up(server) || post_up(server)
		|| server->drawer_moving || server->drawer_t > 0.004 || server->snip_on
		|| server->sw_shown;
	if (!locked(server) && !away && under && keys == under
		&& server->cursor_mode == DESK_CUR_PASSTHROUGH && server->pointer_constraints) {
		want = wlr_pointer_constraints_v1_constraint_for_surface(
			server->pointer_constraints, under, server->seat);
	}
	desk_constraint_set(server, want);
}

static void desk_constraint_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct desk_game_constraint *entry = wl_container_of(listener, entry, destroy);
	if (entry->server->active_constraint == entry->constraint) {
		/* (a game lets go by taking its hold away) */
		constraint_hint_warp(entry->server, entry->constraint);
		entry->server->active_constraint = NULL;
		cursor_image(entry->server, "default");
	}
	wl_list_remove(&entry->destroy.link);
	free(entry);
}

static void desk_new_pointer_constraint(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_pointer_constraint);
	struct wlr_pointer_constraint_v1 *constraint = data;
	struct desk_game_constraint *entry = calloc(1, sizeof *entry);
	if (!entry) {
		return;
	}
	entry->server = server;
	entry->constraint = constraint;
	entry->destroy.notify = desk_constraint_destroy;
	wl_signal_add(&constraint->events.destroy, &entry->destroy);
	desk_constraint_refresh(server);
}

static void seat_request_cursor(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
	if (server->active_constraint && server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		return;  /* hide the hardware cursor while the game has it locked */
	}
	if (focused_client == event->seat_client && !server->hand_unseen) {
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

static void seat_pointer_focus_change(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, pointer_focus_change);
	struct wlr_seat_pointer_focus_change_event *event = data;
	desk_constraint_refresh(server);
	if (event->new_surface == NULL && !server->active_constraint) {
		cursor_image(server, "default");
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* What was last selected, for a middle click to paste -- the X habit, which
 * Wayland programs keep as well, and which Xwayland carries across. */
static void seat_request_set_primary_selection(struct wl_listener *listener, void *data)
{
	struct desk_server *server =
		wl_container_of(listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

/* A drag starts only if we say so, and we say so if the button it started
 * with really is held down in that client: otherwise nothing is dragged at
 * all, which is how it was until now. */
static void seat_request_start_drag(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, request_start_drag);
	struct wlr_seat_request_start_drag_event *event = data;
	if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
		wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
		return;
	}
	struct wlr_touch_point *point;
	if (wlr_seat_validate_touch_grab_serial(server->seat, event->origin, event->serial, &point)) {
		wlr_seat_start_touch_drag(server->seat, event->drag, event->serial, point);
		return;
	}
	wlr_data_source_destroy(event->drag->source);
}

static void drag_icon_gone(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, drag_icon_gone);
	(void)data;
	wl_list_remove(&listener->link);
	wl_list_init(&listener->link);
	server->drag_icon = NULL;
}

/* What is being dragged, if the client gave it a picture: under the pointer,
 * over everything, until it is dropped. */
static void seat_start_drag(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, start_drag);
	struct wlr_drag *drag = data;
	if (!drag->icon || server->drag_icon) {
		return;
	}
	server->drag_icon = wlr_scene_drag_icon_create(server->layer_drag, drag->icon);
	if (!server->drag_icon) {
		return;
	}
	wl_signal_add(&server->drag_icon->node.events.destroy, &server->drag_icon_gone);
	wlr_scene_node_set_position(&server->drag_icon->node,
		(int)server->cursor->x, (int)server->cursor->y);
}

/* A button held down in a program keeps the pointer with it until it is
 * let go, as on any desktop: a slider, a selection, a file pulled out of a
 * window are still the program's with the hand outside it, and it is told
 * where the hand is from its own window. An X program needs it most: an X
 * drag puts a window of its own under the pointer at once -- the picture
 * of what is dragged -- and Xwayland lets go of every button the moment
 * the pointer goes into another of its windows, so a file dragged from one
 * X program to another (inside Wine too) was dropped short, on its own
 * picture. A Wayland drag is the seat's own, and goes on finding what is
 * under the pointer. */
static void held_gone(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, held_gone);
	(void)data;
	wl_list_remove(&listener->link);
	wl_list_init(&listener->link);
	server->held = NULL;
}

/* The button just handed to the program under the pointer: hold on to the
 * surface it went down on. */
static void pointer_hold(struct desk_server *server)
{
	if (server->held) {
		held_gone(&server->held_gone, NULL);
	}
	struct wlr_surface *focus = server->seat->pointer_state.focused_surface;
	double sx, sy;
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
		server->cursor->x, server->cursor->y, &sx, &sy);
	if (!focus || !node || node->type != WLR_SCENE_NODE_BUFFER) {
		return;
	}
	struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	if (!scene_surface || scene_surface->surface != focus) {
		return;
	}
	server->held = buffer;
	wl_signal_add(&buffer->node.events.destroy, &server->held_gone);
}

/* The pointer moved with a button held in a program: told to it alone,
 * from where its surface now lies, wherever the hand is. False if no
 * button is held there (or what is held is a Wayland drag). */
static bool pointer_held(struct desk_server *server, uint32_t time)
{
	struct wlr_seat *seat = server->seat;
	if (!server->held || !server->buttons_down || seat->drag
		|| !seat->pointer_state.button_count) {
		return false;
	}
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(server->held);
	int lx, ly;
	if (!scene_surface || scene_surface->surface != seat->pointer_state.focused_surface
		|| !wlr_scene_node_coords(&server->held->node, &lx, &ly)) {
		return false;
	}
	double sx = server->cursor->x - lx, sy = server->cursor->y - ly;
	if (server->held->point_accepts_input) {
		/* the surface's own coordinates, as wlroots works them out under
		 * the pointer (a sheet partly clipped); outside it, as it is */
		(void)server->held->point_accepts_input(server->held, &sx, &sy);
	}
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	return true;
}

static void process_cursor_move(struct desk_server *server)
{
	struct desk_toplevel *toplevel = server->grabbed;
	if (!toplevel->scene_tree) {
		return;
	}
	if (toplevel->half && !toplevel->maximized
		&& (fabs(server->cursor->x - server->move_from_x) > 5.0
			|| fabs(server->cursor->y - server->move_from_y) > 5.0)) {
		view_half_lift(server, toplevel);
	}
	int x = (int)(server->cursor->x - server->grab_x);
	wlr_scene_node_set_position(&toplevel->scene_tree->node, x,
		work_clamp_y(server, x + toplevel->outer_w / 2,
			(int)(server->cursor->y - server->grab_y)));
	view_sync_shadow(toplevel);
	view_send_position(toplevel);
	slide_motion(server);
}

static void process_cursor_resize(struct desk_server *server)
{
	struct desk_toplevel *toplevel = server->grabbed;
	if (!toplevel->scene_tree) {
		return;
	}
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;
	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = (int)border_y;
		/* the content's top: the frame's title bar is above it */
		int l, t, r, b;
		view_insets(server->grabbed, &l, &t, &r, &b);
		int lowest = screen_area(server, view_screen(server->grabbed)).y + t;
		if (new_top < lowest) {
			new_top = lowest;
		}
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = (int)border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = (int)border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = (int)border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}
	if (view_framed(toplevel)) {
		view_sizing_move(toplevel, new_left, new_top, new_right, new_bottom,
			server->resize_edges);
		return;
	}
	int l, t, r, b;
	view_insets(toplevel, &l, &t, &r, &b);
	wlr_scene_node_set_position(&toplevel->scene_tree->node, new_left - l, new_top - t);
	view_sync_shadow(toplevel);
	view_set_size(toplevel, new_right - new_left, new_bottom - new_top);
}

/* The pointer moved with a lying sheet's bottom edge pressed (view_peek):
 * past PEEK_DRAG it is pulling the edge, from where it took hold of it. */
static void peek_press_motion(struct desk_server *server)
{
	double dx = server->cursor->x - server->move_from_x;
	double dy = server->cursor->y - server->move_from_y;
	if (fabs(dx) <= PEEK_DRAG && fabs(dy) <= PEEK_DRAG) {
		return;
	}
	struct desk_toplevel *view = server->grabbed;
	reset_cursor_mode(server);
	begin_interactive(view, DESK_CUR_RESIZE, WLR_EDGE_BOTTOM);
	if (server->cursor_mode == DESK_CUR_RESIZE) {
		server->grab_x -= dx;
		server->grab_y -= dy;
		process_cursor_resize(server);
	}
}

static void pointer_over_paper(struct desk_server *server, uint32_t time);
static void paper_press(struct desk_server *server, struct wlr_pointer_button_event *event);
static bool pen_over_program(struct desk_server *server, struct wlr_surface *surface, double sx, double sy);
static bool pen_down_on_program(struct desk_server *server, struct wlr_pointer_button_event *event);

/* The pointer gone over to a screen beside the desk's: whatever on the
 * desk's own screen it was over -- a thing on the desk, a binder, a
 * control, a letter -- is let go of. */
static void desk_hover_off(struct desk_server *server)
{
	drawer_hover_to(server, -1);
	clock_track(server, false);
	slips_track(server, false);
	if (shelf_up(server) && server->shelf_binder < 0) {
		hover_set(server, -1);
	}
	if (console_up(server) && server->cc_hover >= 0) {
		server->cc_hover = -1;
		paint_console(server);
		server_schedule_frames(server);
	}
	if (post_up(server) && server->pb_hover >= 0) {
		server->pb_hover = -1;
		paint_post(server);
		server_schedule_frames(server);
	}
}

static void process_cursor_motion(struct desk_server *server, uint32_t time)
{
	/* (the last finger lifted: nothing is under a hand that is not there) */
	if (server->hand_unseen && !server->hand_on) {
		return;
	}
	desk_sound_awake();
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->over_all && server->over_all_button && (*t)->drag
			&& (*t)->drag(server) != THING_NOT_HELD) {
			wlr_seat_pointer_clear_focus(server->seat);
			return;
		}
	}
	if (locked(server)) {
		/* at the door: nothing behind it sees the pointer; the key can
		 * be taken hold of */
		wlr_seat_pointer_clear_focus(server->seat);
		bool hand = server->lock_phase == LOCK_SHUT && desk_lock_key_at(server->desk_w,
			server->desk_h, server->cursor->x, server->cursor->y);
		if (hand != server->lock_hand) {
			server->lock_hand = hand;
			cursor_image(server, hand ? "pointer" : "default");
		}
		return;
	}
	/* Alt+Tab's windows spread out: the hand is on them */
	if (server->sw_shown) {
		switcher_motion(server);
		return;
	}
	/* the camera up: an area being drawn out, or the one under the hand */
	if (server->snip_on) {
		snip_motion(server);
		return;
	}
	if (server->drag_icon) {
		wlr_scene_node_set_position(&server->drag_icon->node,
			(int)server->cursor->x, (int)server->cursor->y);
	}
	/* the card of looks up: over it, or over nothing else */
	if (looks_motion(server)) {
		wlr_seat_pointer_clear_focus(server->seat);
		cursor_for_edges(server, 0);
		return;
	}
	/* something from the drawer, or a packet off the desk, in the hand */
	if (drawer_motion(server)) {
		wlr_seat_pointer_clear_focus(server->seat);
		if (server->drw_carrying) {
			cursor_image(server, "fleur");
		} else {
			cursor_for_edges(server, 0);
		}
		return;
	}
	if (server->cursor_mode == DESK_CUR_PEEK) {
		peek_press_motion(server);
		return;
	}
	if (server->cursor_mode == DESK_CUR_MOVE) {
		process_cursor_move(server);
		return;
	}
	if (server->cursor_mode == DESK_CUR_RESIZE) {
		process_cursor_resize(server);
		return;
	}
	/* a letter in the hand */
	if (paper_drag(server)) {
		wlr_seat_pointer_clear_focus(server->seat);
		if (server->pp_moved) {
			cursor_image(server, "fleur");
		}
		return;
	}
	/* a thing with a hand on it */
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		enum desk_thing_hold hold = (*t)->drag ? (*t)->drag(server) : THING_NOT_HELD;
		if (hold != THING_NOT_HELD) {
			wlr_seat_pointer_clear_focus(server->seat);
			if (hold == THING_CARRIED) {
				cursor_image(server, "fleur");
			}
			return;
		}
	}
	/* a hand on the clock's volume bar keeps hold of it */
	if (clock_drag(server)) {
		return;
	}
	/* the card pulled out from under the Control Panel is over it all */
	if (card_motion(server)) {
		cursor_for_edges(server, 0);
		wlr_seat_pointer_clear_focus(server->seat);
		return;
	}
	/* On a screen beside the desk's there is only paper: in reach whatever
	 * the desk's own screen is turned to, once the drawer and the camera
	 * have left it where it lies. What the hand was over on the desk is
	 * let go of. */
	if (pointer_on_side(server)) {
		desk_hover_off(server);
		if (paper_moved(server)) {
			wlr_seat_pointer_clear_focus(server->seat);
			cursor_for_edges(server, 0);
			return;
		}
		pointer_over_paper(server, time);
		return;
	}

	if (post_up(server)) {
		if (server->pb_t < 0.9995) {
			return;
		}
		const struct desk_note *notes = NULL;
		int n = post_count(&notes);
		int hv = desk_post_hit(server->desk_w, server->desk_h, n, server->pb_pos,
			server->cursor->x, server->cursor->y);
		if (hv != server->pb_hover) {
			server->pb_hover = hv;
			paint_post(server);
			server_schedule_frames(server);
		}
		pbc_track(server);
		return;
	}
	if (console_up(server)) {
		if (server->cc_grab >= 0) {
			console_drag(server);
			return;
		}
		if (server->cc_t < 0.9995) {
			return;
		}
		int hv = desk_console_hit_lid(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y, server->cc_guard);
		if (hv != server->cc_hover) {
			server->cc_hover = hv;
			paint_console(server);
			server_schedule_frames(server);
		}
		return;
	}
	if (shelf_up(server)) {
		if (server->shelf_t < 0.9995) {
			return;
		}
		if (memo_up(server)) {
			return;   /* the memo pad in your hand: the shelf is behind it */
		}
		if (cal_up(server)) {
			/* on the binder, what of it; else which edge of the page the
			 * pointer is at: it stands up; or which tab: it is drawn out a
			 * little */
			bool on_binder = false;
			if (server->agd_drag) {
				/* drawn along the ruler */
				agd_drag_to(server, server->cursor->x, server->cursor->y);
				return;
			}
			if (agd_up(server)) {
				agd_hover_at(server, server->cursor->x, server->cursor->y);
				on_binder = server->agd_hover != DESK_AGENDA_HIT_NONE;
			}
			double cx = server->cursor->x, cy = server->cursor->y;
			cal_unaside(server, &cx, &cy);
			struct desk_cal_look lk;
			cal_look_now(server, &lk);
			desk_calendar_unturn(server->desk_w, server->desk_h, &lk, &cx, &cy);
			int yy, mm, dd;
			int z = cal_settled(server) && !on_binder ? desk_calendar_hit(server->desk_w,
				server->desk_h, &lk, cx, cy, &yy, &mm, &dd) : 0;
			int tab = z >= DESK_CAL_HIT_TAB ? z - DESK_CAL_HIT_TAB : -1;
			z = z == DESK_CAL_HIT_PREV ? 1 : (z == DESK_CAL_HIT_NEXT ? 2 : 0);
			if (z != server->cal_zone) {
				server->cal_zone = z;
				server_schedule_frames(server);
			}
			if (tab != server->cal_hover_tab) {
				server->cal_hover_tab = tab;
				paint_calendar(server);
				server_schedule_frames(server);
			}
			return;
		}
		if (server->shelf_binder == DESK_SHELF_BOOK) {
			book_hover_at(server, server->cursor->x, server->cursor->y);
			return;
		}
		if (server->shelf_binder >= 0) {
			if (server->binder_moving || server->binder_to < 0.5) {
				return;   /* on its way, or left open as you turn away */
			}
			int sheet = desk_binder_hit(server->desk_w, server->desk_h,
				binder_count(server), server->binder_pos,
				server->cursor->x, server->cursor->y);
			if (sheet != server->binder_sel) {
				server->binder_sel = sheet;
				paint_shelf(server);
				server_schedule_frames(server);
			}
			bnc_track(server);
			return;
		}
		hover_set(server, desk_shelf_hit(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y));
		return;
	}
	bool on_clock = clock_track(server, !window_under(server));
	bool on_slip = slips_track(server, !window_under(server));
	drawer_hover_to(server, window_under(server) || on_clock || on_slip ? -1
		: drawer_entry_at(server, server->cursor->x, server->cursor->y));
	/* with the drawer out, the paper on the desk is not in reach */
	if (server->drawer_t > 0.004) {
		wlr_seat_pointer_clear_focus(server->seat);
		cursor_for_edges(server, 0);
		return;
	}
	pointer_over_paper(server, time);
}

/* The pointer over the paper -- a window, its frame, or the bare desk
 * between them: the corner it is near, the arrows of an edge that can be
 * pulled, or the program's own pointer over the program. */
static void pointer_over_paper(struct desk_server *server, uint32_t time)
{
	double sx, sy;
	struct wlr_surface *surface = NULL;
	if (pointer_held(server, time)) {
		return;
	}
	struct desk_toplevel *under =
		view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	curl_track(server, under, server->cursor->x, server->cursor->y);
	if (!surface) {
		/* over the desk or a frame: ours -- an arrow, or the edge's own
		 * arrows where the paper can be pulled. Over a client -- an X
		 * menu too, which is no window of ours -- the client's. */
		unsigned e = 0;
		if (under && view_framed(under) && under->scene_tree) {
			double lx = server->cursor->x - under->scene_tree->node.x;
			double ly = server->cursor->y - under->scene_tree->node.y;
			e = (unsigned)desk_frame_resize_edges(lx, ly, under->outer_w, under->outer_h);
			const struct desk_thing *thing = view_thing(under);
			if (thing && thing->edges) {
				e = thing->edges(under, lx, ly, e);
			}
		}
		cursor_for_edges(server, e);
	}
	if (surface && pen_over_program(server, surface, sx, sy)) {
		return;
	}
	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(server->seat);
	}
}

/* The pointer moved by (dx, dy): a mouse's own movement, or worked out from
 * where a tablet's or a virtual machine's pointer now is. A game is told
 * the movement as it is; a game that holds the pointer still keeps it
 * where it is, and one that keeps it inside its window keeps it there. */
static void pointer_moved(struct desk_server *server, struct wlr_input_device *dev,
	double dx, double dy, double udx, double udy, uint32_t time_msec)
{
	/* Relative events are required by Wayland and XWayland game clients
	 * (not while an area is being chosen for a picture) */
	if (server->relative_pointer_manager && (dx != 0.0 || dy != 0.0) && !server->snip_on
		&& !server->sw_shown) {
		wlr_relative_pointer_manager_v1_send_relative_motion(server->relative_pointer_manager,
			server->seat, (uint64_t)time_msec * 1000, dx, dy, udx, udy);
	}
	input_seen(server, "the pointer");
	if (hypot(dx, dy) > 0.5) {
		termites_disturb(server);
	}
	/* the bookcase or the drawer may have come up, or gone, since */
	desk_constraint_refresh(server);
	struct wlr_pointer_constraint_v1 *c = server->active_constraint;
	if (c && c->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		return; /* game gets the delta; desktop cursor stays fixed */
	}
	if (c && c->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
		double sx, sy;
		struct wlr_surface *surface = NULL;
		view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		double cx, cy;
		if (surface != c->surface || !wlr_region_confine(&c->region,
				sx, sy, sx + dx, sy + dy, &cx, &cy)) {
			return;
		}
		dx = cx - sx;
		dy = cy - sy;
	}
	wlr_cursor_move(server->cursor, dev, dx, dy);
	process_cursor_motion(server, time_msec);
}

/* A mouse used again, after fingers: its pointer is seen, and a program under
 * it is told it has come, so that it gives its picture of the pointer. */
static void mouse_back(struct desk_server *server)
{
	if (server->hand_unseen && !server->hand_on) {
		server->hand_unseen = false;
		wlr_seat_pointer_clear_focus(server->seat);
		cursor_image(server, "default");
	}
}

static void server_cursor_motion(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	server->pen_lately = false;
	mouse_back(server);
	pointer_moved(server, &event->pointer->base, event->delta_x, event->delta_y,
		event->unaccel_dx, event->unaccel_dy, event->time_msec);
}

/* A pointer that says where it is: the movement is from where the cursor is
 * -- or, while a game holds the cursor still, from where this pointer last
 * said it was, the cursor not having gone there. */
static void server_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly;
	server->pen_lately = false;
	mouse_back(server);
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base, event->x,
		event->y, &lx, &ly);
	desk_constraint_refresh(server);
	struct wlr_pointer_constraint_v1 *c = server->active_constraint;
	double fx = server->cursor->x, fy = server->cursor->y;
	if (c && c->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		fx = server->abs_seen ? server->abs_x : lx;
		fy = server->abs_seen ? server->abs_y : ly;
	}
	server->abs_x = lx;
	server->abs_y = ly;
	server->abs_seen = true;
	pointer_moved(server, &event->pointer->base, lx - fx, ly - fy, lx - fx, ly - fy,
		event->time_msec);
}

/* A button: a mouse's, or what a finger has come to mean (fingers). */
static void pointer_button(struct desk_server *server, struct wlr_pointer_button_event *event)
{
	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		server->buttons_down++;
	} else if (server->buttons_down) {
		server->buttons_down--;
	}
	/* a click on the dark screen -- or on it lit again, before it can have
	 * been seen -- wakes it and lands on nothing, nor does its letting go */
	if (!input_seen(server, "a click")) {
		server->wake_button = event->state == WL_POINTER_BUTTON_STATE_PRESSED
			? event->button : 0;
		return;
	}
	if (server->wake_button == event->button
		&& event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		server->wake_button = 0;
		return;
	}
	desk_sound_awake();
	/* what lies over everything has the hand first */
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if (!(*t)->over_all) {
			continue;
		}
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
			if ((*t)->hand_down && (*t)->hand_down(server, event->button)) {
				server->over_all_button = event->button;
				return;
			}
		} else if (server->over_all_button == event->button) {
			server->over_all_button = 0;
			if ((*t)->release) {
				(*t)->release(server);
			}
			return;
		}
	}
	if (locked(server)) {
		/* the key: turning it is Return */
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && event->button == BTN_LEFT
			&& server->lock_phase == LOCK_SHUT && desk_lock_key_at(server->desk_w,
				server->desk_h, server->cursor->x, server->cursor->y)) {
			lock_submit(server);
		}
		return;
	}
	if (console_dying(server)) {
		return;
	}
	/* Alt+Tab's windows spread out: a click is for them, and so is its
	 * letting go, even once they are back */
	if (server->sw_eat == event->button && event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		server->sw_eat = 0;
		return;
	}
	if (server->sw_shown) {
		switcher_button(server, event->button,
			event->state == WL_POINTER_BUTTON_STATE_PRESSED);
		return;
	}
	/* an area being drawn out for a picture */
	if (server->snip_on) {
		snip_button(server, event->button, event->state == WL_POINTER_BUTTON_STATE_PRESSED);
		return;
	}
	/* the card of looks up over the drawer: the hand is for it */
	if (looks_button(server, event->button, event->state == WL_POINTER_BUTTON_STATE_PRESSED)) {
		return;
	}
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		if (event->button == BTN_LEFT && paper_release(server)) {
			return;
		}
		if (event->button == BTN_LEFT) {
			for (const struct desk_thing *const *t = desk_things; *t; t++) {
				if ((*t)->release && (*t)->release(server)) {
					return;
				}
			}
		}
		if (server->agd_drag) {
			/* let go of the ruler */
			server->agd_drag = false;
			agd_hover_at(server, server->cursor->x, server->cursor->y);
			return;
		}
		if (server->drw_button && event->button == server->drw_button
			&& drawer_release(server)) {
			return;
		}
		if (clock_release(server)) {
			return;
		}
		if (server->cc_grab >= 0) {
			/* let go of the knob */
			server->cc_grab = -1;
			paint_console(server);
			return;
		}
		if (server->cursor_mode == DESK_CUR_RESIZE && server->grabbed
			&& server->grabbed->sizing) {
			/* let go of the edge: the turn of the paper settles */
			server->grabbed->sz_released = real_sec();
			server_schedule_frames(server);
		}
		if (event->button == BTN_LEFT) {
			if (server->cursor_mode == DESK_CUR_PEEK && server->grabbed) {
				/* the bottom edge clicked, not pulled */
				view_peek(server->grabbed);
			}
			move_release(server);
		}
		reset_cursor_mode(server);
		wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
		if (server->held && !server->seat->pointer_state.button_count) {
			/* let go: the pointer is back with whatever it is over */
			held_gone(&server->held_gone, NULL);
			process_cursor_motion(server, event->time_msec);
		}
		return;
	}

	/* On a screen beside the desk's, only paper: a hand put down there
	 * with the drawer out pushes it back in, as anywhere on the desk above
	 * it does; with the camera on its way, nothing yet. */
	if (pointer_on_side(server)) {
		if (server->drawer_t > 0.004) {
			if (!post_up(server) && !console_up(server) && !shelf_up(server)) {
				drawer_toggle(server);
			}
			return;
		}
		if (!paper_moved(server)) {
			/* a packet folded on this screen: taken hold of, as on the desk */
			if ((event->button == BTN_LEFT || event->button == BTN_RIGHT)
				&& drawer_press(server, event->button)) {
				return;
			}
			paper_press(server, event);
		}
		return;
	}

	/* Something in the drawer, or a packet folded on the desk: taken hold
	 * of, to be clicked or carried. */
	if ((event->button == BTN_LEFT || event->button == BTN_RIGHT)
		&& drawer_press(server, event->button)) {
		return;
	}

	/* With the drawer out you are looking into the drawer: a hand put down
	 * on the desk above it (on the paper there too) pushes it back in. */
	if (server->drawer_t > 0.004 && !post_up(server) && !console_up(server)
		&& !shelf_up(server) && server->cursor->y < drawer_line(server)) {
		drawer_toggle(server);
		return;
	}

	/* At the post box, the desk is not in reach either. */
	if (post_up(server)) {
		if (event->button != BTN_LEFT || server->pb_t < 0.9995) {
			return;
		}
		if (pbc_press(server)) {
			return;
		}
		const struct desk_note *notes = NULL;
		int n = post_count(&notes);
		int idx = desk_post_hit(server->desk_w, server->desk_h, n, server->pb_pos,
			server->cursor->x, server->cursor->y);
		if (idx >= 0 && idx < (int)lround(server->pb_pos)) {
			/* one that has gone over: turn back to it */
			pb_riffle(server, idx - (int)lround(server->pb_pos));
		} else if (idx >= 0) {
			post_take(server, idx);
		} else if (!desk_post_view_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			post_toggle(server);
		}
		return;
	}

	/* While you are round at the set, the desk is not in reach: your hands
	 * are on the machine. */
	if (console_up(server)) {
		if (server->cc_t < 0.9995) {
			return;   /* mid move: let it land first */
		}
		int which = desk_console_hit_lid(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y, server->cc_guard);
		if (event->button == BTN_RIGHT) {
			/* the other button: what is behind the control, in full */
			console_open_settings(server, which);
			return;
		}
		if (event->button != BTN_LEFT) {
			return;
		}
		if (server->cc_guard > 0.0005 && !server->cc_key_moving
			&& desk_console_guard_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y, server->cc_guard)) {
			/* the lid, up: back down */
			console_guard_to(server, server->cc_guard_to > 0.5 ? 0.0 : 1.0);
		} else if (which == DESK_CC_NET) {
			/* the network's meter: the networks themselves */
			console_open_settings(server, which);
		} else if (which >= 0) {
			console_press(server, which);
		} else if (!desk_console_view_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			/* off the machine altogether: you have looked away */
			console_toggle(server);
		}
		return;
	}

	/* While you are looking up at the bookcase, the desk is not in reach. */
	if (shelf_up(server)) {
		if (memo_up(server)) {
			memo_press(server, event->button);
			return;
		}
		if (cal_up(server)) {
			/* the calendar in your hand: the head of the page back a page,
			 * its foot on one, a day opens the binder of notes at it, a tab
			 * goes to its section; anywhere else -- or the other button --
			 * puts the binder back, or the calendar on the shelf */
			if (!cal_settled(server)) {
				return;
			}
			if (agd_up(server) && agd_press(server, server->cursor->x, server->cursor->y,
					event->button)) {
				return;
			}
			double cx = server->cursor->x, cy = server->cursor->y;
			cal_unaside(server, &cx, &cy);
			struct desk_cal_look lk;
			cal_look_now(server, &lk);
			desk_calendar_unturn(server->desk_w, server->desk_h, &lk, &cx, &cy);
			if ((event->button == BTN_LEFT || event->button == BTN_RIGHT) && !server->cal_turn
				&& !cal_switching(server)) {
				/* a note stuck on it: taken off to be written on again, or
				 * with the other button peeled off */
				int nid = desk_calendar_note_at(server->desk_w, server->desk_h, &lk, cx, cy);
				struct desk_stick ns;
				if (nid && desk_agenda_get(nid, &ns)) {
					if (event->button == BTN_RIGHT) {
						agd_peel_off(server, nid);
					} else {
						agd_open(server, ns.y, ns.m, ns.d, AGD_START_NONE, -1);
						agd_begin(server, nid);
					}
					return;
				}
			}
			int yy = 0, mm = 0, dd = 0;
			int z = event->button == BTN_LEFT ? desk_calendar_hit(server->desk_w,
				server->desk_h, &lk, cx, cy, &yy, &mm, &dd) : 0;
			if (z >= DESK_CAL_HIT_TAB) {
				cal_switch(server, z - DESK_CAL_HIT_TAB);
			} else if (z == DESK_CAL_HIT_PREV) {
				cal_turn_page(server, -1);
			} else if (z == DESK_CAL_HIT_NEXT) {
				cal_turn_page(server, 1);
			} else if (z == DESK_CAL_HIT_DATE || z == DESK_CAL_HIT_TODO) {
				/* on a day's page, at an hour: a plan at that hour */
				int hr = z == DESK_CAL_HIT_DATE ? desk_calendar_hour_at(server->desk_w,
					server->desk_h, &lk, cx, cy) : -1;
				agd_open(server, yy, mm, dd, z == DESK_CAL_HIT_TODO ? AGD_START_TODO
					: (hr >= 0 ? AGD_START_PLAN : AGD_START_NONE), hr >= 0 ? hr * 60 : -1);
			} else if (z == DESK_CAL_HIT_MONTH) {
				/* a month's name on the year's page: that month */
				int d2 = server->cal_day;
				server->cal_year = yy;
				server->cal_month = mm;
				/* (stepping no months is the day kept in the month) */
				desk_calendar_step(DESK_CAL_MONTH, 0, &server->cal_year, &server->cal_month,
					&d2);
				server->cal_day = d2;
				cal_switch(server, DESK_CAL_MONTH);
			} else if (z == DESK_CAL_HIT_PAGE) {
				agd_finish(server, true);
			} else if (agd_up(server)) {
				agd_close(server);
			} else {
				cal_close(server);
			}
			return;
		}
		if (event->button != BTN_LEFT) {
			return;
		}
		if (server->shelf_t < 0.9995) {
			return;   /* mid move: let it land first */
		}
		if (server->shelf_binder == DESK_SHELF_BOOK) {
			book_press(server, server->cursor->x, server->cursor->y);
			return;
		}
		if (server->shelf_binder >= 0) {
			if (server->binder_moving || server->binder_to < 0.5) {
				return;
			}
			if (bnc_press(server)) {
				return;
			}
			int sheet = desk_binder_hit(server->desk_w, server->desk_h,
				binder_count(server), server->binder_pos,
				server->cursor->x, server->cursor->y);
			if (sheet >= 0) {
				binder_take(server, sheet);
			} else {
				binder_close(server);
			}
			return;
		}
		int bn = desk_shelf_hit(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y);
		if (bn >= 0) {
			binder_open(server, bn);
			return;
		}
		/* the calendar on the lower board: taken down to be looked at */
		if (desk_shelf_calendar_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			cal_open(server);
			return;
		}
		/* and the memo pad beside it, to be written on */
		if (desk_shelf_memo_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			memo_open(server);
			return;
		}
		/* The desk is in the foreground up here, and what is built into
		 * its back edge is close enough to reach: come back down to it
		 * and turn to whichever of them was pointed at. */
		if (desk_console_shelf_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			server->pending_look = 1;
			shelf_toggle(server);
			return;
		}
		if (desk_post_shelf_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			server->pending_look = 2;
			shelf_toggle(server);
			return;
		}
		shelf_toggle(server);
		return;
	}
	/* the card pulled out from under the Control Panel is over it all */
	if (card_press(server, event->button)) {
		return;
	}
	/* A letter lying on the desk, uppermost under the pointer: taken hold
	 * of -- carried if the hand moves, put away if it is only a click.
	 * (Asked here: it lies in the windows' layer, and below this a click
	 * there was taken for a window's, so a letter could not be put away.) */
	if (event->button == BTN_LEFT) {
		int pi = post_paper_under(server);
		if (pi >= 0) {
			struct desk_paper *pp = &server->pb_paper[pi];
			server->pp_held = pi + 1;
			server->pp_moved = false;
			server->pp_px = server->cursor->x;
			server->pp_py = server->cursor->y;
			server->pp_hx = server->cursor->x - pp->x;
			server->pp_hy = server->cursor->y - pp->y;
			return;
		}
	}
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->hand_down && (*t)->hand_down(server, event->button)) {
			return;
		}
	}
	/* What lies on the desk, if no window lies over it. */
	bool on_window = window_under(server);
	if (!on_window) {
		bool taken;
		clock_press(server, event->button, &taken);
		if (taken) {
			return;
		}
		slips_press(server, event->button, &taken);
		if (taken) {
			return;
		}
		if (event->button == BTN_LEFT
			&& desk_allapps_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			shelf_toggle(server);
			return;
		}
		/* the set at the back left: you turn to it */
		if (event->button == BTN_LEFT
			&& desk_console_desk_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y, server->drawer_t)) {
			console_toggle(server);
			return;
		}
		/* and the post box itself */
		if (event->button == BTN_LEFT
			&& desk_post_desk_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y, server->drawer_t)) {
			post_toggle(server);
			return;
		}

		if (event->button == BTN_LEFT &&
			desk_drawer_handle_hit(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
				server->drawer_t)) {
			drawer_toggle(server);
			return;
		}
	}
	paper_press(server, event);
}

static void server_cursor_button(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, cursor_button);
	server->pen_lately = false;
	mouse_back(server);
	pointer_button(server, data);
}

/* ---- fingers -------------------------------------------------------------
 *
 * A finger is not a mouse. There is nothing to hover with: it has no place until it is put down.
 * It is wide, so what it can take is wider. It may be going somewhere when it is lifted. And
 * there are ten of them.
 *
 * On a program's window a finger is the program's, told to it as a finger: it scrolls, it
 * pinches, as the program has it. Anywhere else it is the hand, one finger at a time, and what it
 * does is said by where it goes:
 *
 *   a tap               what a click does there
 *   held still          what the other button does there
 *   gone, from a thing  has hold of it, as a hand does: paper by its head and its edges, a
 *                       knob, a packet, what is in the drawer
 *   gone, over nothing  up, the drawer comes out on the finger; down, you look up at the
 *                       bookcase; right, you turn to the set; left, to the post box. Back the
 *                       other way. Over what has sheets to turn, they turn.
 *
 * From the very rim of the screen it is the same, over whatever lies there: so the desk can be
 * reached with a window opened out over it. And three fingers or more, anywhere, are the whole
 * hand (FINGER_WHOLE), whatever is under them, and a program with fingers on it is told to forget
 * them: up, the windows are spread out to choose from; down, the sheet folds; to a side, it is
 * carried, and thrown hard it lies on that half; closed up, it is screwed up, or folded back to
 * what it was; opened out, it is opened out. All of them as far as the fingers have come.
 *
 * Whatever lies on the desk knows the hand by the pointer, so the hand is the pointer, moved
 * unseen to where the finger is; and a button is pressed for it only once it is known what the
 * finger is doing.
 */

/* The rim of the screen a finger can come in from, in points */
#define TOUCH_RIM_PT 10.0
/* How far past a sheet's edge a finger still has hold of it */
#define TOUCH_PAST_EDGE_PT 18.0
/* How far from a sheet's corner a finger has the corner */
#define TOUCH_CORNER_PT 26.0
/* How far a sheet is turned over for */
#define TOUCH_SHEET_PT 36.0

static void pointer_axis(struct desk_server *server, struct wlr_pointer_axis_event *event);
