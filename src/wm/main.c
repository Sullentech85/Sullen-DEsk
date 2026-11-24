/* Part of wm.c: the protocols set up, the command line and main().
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

/* Capture what the compositor is actually showing -- clients, shadows and
 * all -- rather than re-rendering the wallpaper. That needs the screencopy
 * protocol, which is also what makes grim work for the user. */
static int screenshot_done_cb(void *data)
{
	struct desk_server *server = data;
	wl_display_terminate(server->wl_display);
	return 0;
}

static int screenshot_cb(void *data)
{
	struct desk_server *server = data;
	bool captured = false;
	if (server->screenshot_path && server->desk_w > 0) {
		if (access("/usr/bin/grim", X_OK) == 0 || access("/bin/grim", X_OK) == 0) {
			char *const argv[] = { (char *)"grim", server->screenshot_path, NULL };
			spawn_argv(argv);
			wlr_log(WLR_INFO, "capturing screenshot to %s", server->screenshot_path);
			captured = true;
		} else {
			/* no grim: fall back to redrawing the desktop, which at least
			 * produces a file, though without any client windows in it */
			desk_write_png(server->screenshot_path, server->desk_w, server->desk_h, true);
			wlr_log(WLR_INFO, "grim not found; wrote desktop-only %s",
				server->screenshot_path);
		}
	}
	if (server->exit_after_shot) {
		if (!captured) {
			wl_display_terminate(server->wl_display);
			return 0;
		}
		/* give grim a moment to connect, capture and write */
		struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
		struct wl_event_source *src = wl_event_loop_add_timer(loop, screenshot_done_cb, server);
		wl_event_source_timer_update(src, 1500);
	}
	return 0;
}

static void usage(const char *argv0)
{
	printf("Usage: %s [options]\n"
		"  -s CMD          startup command\n"
		"  --version       which DEsk this is\n"
		"  --headless      force headless backend\n"
		"  --size WxH      output size (default 1600x1000 headless)\n"
		"  --dump FILE     write cairo desktop PNG when the output appears\n"
		"  --screenshot F  capture with grim after a delay\n"
		"  --delay MS      screenshot delay (default 2500)\n"
		"  --exit-after-screenshot\n"
		"Keybindings:\n"
		"  Super+Enter     terminal\n"
		"  Super+Q         close window\n"
		"  Super/Alt+Esc   quit\n"
		"  Alt+Tab (held)  the windows spread out to choose from\n"
		"  Super+Tab       cycle windows\n"
		"  Super+F         whole screen / back\n"
		"  Super+Left/Right  left / right half of the screen, again: back\n"
		"  Super+D         open / close drawer\n"
		"  Super+M         fold / unfold paper\n", argv0);
}

/* Only one of us.
 *
 * The room is ray traced, and a cold start is a minute of every core the
 * machine has. Two of those at once -- which is all a double click on the
 * desktop shortcut takes -- is two minutes of nothing but fans, twice the
 * work for one picture, and two processes racing to write the same cache
 * files at the end of it.
 *
 * flock rather than a pid file: the kernel drops the lock when the process
 * dies however it dies, so there is no stale lock to clean up after a
 * crash. Set DESK_ALLOW_MULTIPLE=1 to run a second one anyway. */
static int desk_claim_session(void)
{
	if (getenv("DESK_ALLOW_MULTIPLE")) {
		return -1;
	}
	const char *run = getenv("XDG_RUNTIME_DIR");
	char path[512];
	if (!run || !*run) {
		run = "/tmp";
	}
	if ((size_t)snprintf(path, sizeof path, "%s/desk.lock", run) >= sizeof path) {
		return -1;
	}
	int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0) {
		return -1;   /* cannot lock: better to run than to refuse */
	}
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		close(fd);
		return -2;   /* somebody else has it */
	}
	/* and who has it, for desk-ctl */
	if (ftruncate(fd, 0) == 0) {
		char pid[24];
		int n = snprintf(pid, sizeof pid, "%d\n", (int)getpid());
		ssize_t w = write(fd, pid, (size_t)n);
		(void)w;
	}
	return fd;
}

/* Buffers a client draws on the GPU and hands over (dmabufs). wlroots
 * checks each one on the way in by importing it on the GPU and closing the
 * handle again -- and some drivers do not let that handle be closed the
 * generic way: vmwgfx, the virtual GPU of a VMware machine, is one. There
 * the check fails for every buffer there is, and every client that draws
 * on the GPU -- Xwayland first of all, then any GL program -- is told its
 * buffers are no good and thrown off. The buffers are fine; only the check
 * is not. So it is tried once, on a buffer of our own, and only where it
 * fails like that is it left out: a buffer that really cannot be used is
 * then found out by the renderer, which says so without disconnecting
 * anybody. */
static bool dmabuf_check_nothing(struct wlr_dmabuf_attributes *attribs, void *data)
{
	(void)attribs;
	(void)data;
	return true;
}

static bool dmabuf_check_is_broken(struct desk_server *server)
{
	int rfd = wlr_renderer_get_drm_fd(server->renderer);
	char *node = rfd >= 0 ? drmGetRenderDeviceNameFromFd(rfd) : NULL;
	if (!node) {
		return false;
	}
	int fd = open(node, O_RDWR | O_CLOEXEC);
	free(node);
	if (fd < 0) {
		return false;
	}
	uint64_t mod = DRM_FORMAT_MOD_INVALID;
	struct wlr_drm_format fmt = {
		.format = DRM_FORMAT_XRGB8888, .len = 1, .capacity = 1, .modifiers = &mod,
	};
	bool broken = false;
	struct wlr_buffer *buf = wlr_allocator_create_buffer(server->allocator, 64, 64, &fmt);
	struct wlr_dmabuf_attributes a;
	if (buf && wlr_buffer_get_dmabuf(buf, &a) && a.n_planes > 0) {
		uint32_t handle = 0;
		if (drmPrimeFDToHandle(fd, a.fd[0], &handle) == 0
			&& drmCloseBufferHandle(fd, handle) != 0) {
			broken = true;
		}
	}
	if (buf) {
		wlr_buffer_drop(buf);
	}
	close(fd);   /* and anything left open on it with it */
	return broken;
}

/* What wlr_renderer_init_wl_display() does -- shared memory, and GPU
 * buffers where the renderer takes them -- but keeping hold of the second,
 * so its check can be seen to. */
static void init_client_buffers(struct desk_server *server)
{
	wlr_renderer_init_wl_shm(server->renderer, server->wl_display);
	if (!wlr_renderer_get_texture_formats(server->renderer, WLR_BUFFER_CAP_DMABUF)
		|| wlr_renderer_get_drm_fd(server->renderer) < 0) {
		return;
	}
	struct wlr_linux_dmabuf_v1 *dmabuf =
		wlr_linux_dmabuf_v1_create_with_renderer(server->wl_display, 4, server->renderer);
	if (dmabuf && dmabuf_check_is_broken(server)) {
		wlr_linux_dmabuf_v1_set_check_dmabuf_callback(dmabuf, dmabuf_check_nothing, NULL);
		wlr_log(WLR_INFO, "this GPU cannot close a shared buffer's handle "
			"the usual way; taking clients' GPU buffers on trust");
	}
}

/* On the machine's own screens, the firmware's framebuffer left out when a
 * real card is there too (cards.c), unless the cards are named already
 * (WLR_DRM_DEVICES) or the desk is nested in another's window. */
static bool cards_pick(void)
{
	const char *be = getenv("WLR_BACKENDS");
	bool nested = getenv("WAYLAND_DISPLAY") || getenv("WAYLAND_SOCKET") || getenv("DISPLAY");
	if (getenv("WLR_DRM_DEVICES") || (be ? !strstr(be, "drm") : nested)) {
		return false;
	}
	const char *seat = getenv("XDG_SEAT");
	char list[512], why[256];
	if (!desk_cards_pick("/sys/class/drm", "/run/udev/data", seat && *seat ? seat : "seat0",
			list, sizeof list, why, sizeof why)) {
		return false;
	}
	wlr_log(WLR_INFO, "screens: %s is the firmware's framebuffer, with a real card beside it: "
		"left out (the desk goes on %s)", why, list);
	setenv("WLR_DRM_DEVICES", list, 1);
	return true;
}

int main(int argc, char **argv)
{
	g_start_real = real_sec();
	desk_compat();
	wlr_log_init(WLR_INFO, NULL);
	/* asked which it is, before anything else -- before the lock a second
	 * one stops at; and else the first thing in the log, which wrote it */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) {
			printf("%s\n", DESK_RELEASE);
			return 0;
		}
	}
	wlr_log(WLR_INFO, "%s", DESK_RELEASE);
	/* SIGCHLD stays as it is: every child is either waited for or, like
	 * the programs spawn() starts, not ours at all. */
	const char *slow = getenv("DESK_SLOW");
	if (slow && atof(slow) >= 1.0) {
		g_time_slow = atof(slow);
	}
	const char *prof = getenv("DESK_PROFILE");
	g_prof.on = prof && *prof && strcmp(prof, "0") != 0;
	const char *trace = getenv("DESK_PROFILE_FRAMES");
	if (trace && *trace && (g_trace.f = fopen(trace, "we")) != NULL) {
		fputs("t,gap,wait,tick,tick_cpu,render,render_cpu,between_cpu,up_kb,bufs,painters\n",
			g_trace.f);
		g_prof.on = true;
	}
	int lock_fd = desk_claim_session();
	if (lock_fd == -2) {
		wlr_log(WLR_ERROR, "DEsk is already running. "
			"Set DESK_ALLOW_MULTIPLE=1 to start a second one anyway.");
		return 0;
	}
	struct desk_server server = {0};
	/* (before the drawer is read: what it offers asks after the fingers and pens there are) */
	wl_list_init(&server.touch_devices);
	wl_list_init(&server.tablets);
	wl_list_init(&server.pens);
	/* the desktop we may be nested in, before Xwayland puts its own
	 * display in the environment for the programs we start */
	if (getenv("DISPLAY") && *getenv("DISPLAY")) {
		server.parent_display = strdup(getenv("DISPLAY"));
	}
	server.headless_w = 1600;
	server.headless_h = 1000;
	server.screenshot_delay_ms = 2500;
	drawer_load(&server);
	drawer_looks_load(&server);
	server.lk_pick_fd = -1;
	shell_conf_poll(&server);
	/* the room starts dark, and the clock counts it being made */
	server.boot_on = true;
	server.boot_lamp = server.conf.lights_at_login && server.conf.gfx != DESK_GFX_2D;
	server.boot_shown = -1;
	server.boot_light = server.boot_lamp ? 0.0 : 1.0;
	server.side_light = server.boot_light;
	/* the sounds: made, and the sound server found, on a thread of their own */
	desk_sound_init("DEsk");
	server.bnc_idx = -1;
	server.take.base = -1;
	server.cal_view = server.cal_to_view = DESK_CAL_MONTH;
	server.cal_hover_tab = -1;
	server.agd_when = -1;
	server.agd_mark = -1;
	desk_calendar_set_view(server.cal_view);
	server.take.idx = -1;
	server.bnc_hot = -1;
	server.bnc_act = -1;
	server.clk.hot = -1;
	server.clk.pressed = -1;
	server.clk.media_hot = -1;
	server.clk.media_down = -1;
	server.slp.hot_entry = -1;
	/* The desk starts as a desk, with the drawer pushed in.
	 *
	 * It used to start open, and an open drawer lifts the desk by its own
	 * height to make room for itself -- which carries the whole back edge
	 * off the top of the screen. The console, the post box and the tray
	 * were not merely hidden up there, they were unclickable, and the
	 * first thing anybody did was click the post box and get nothing.
	 * Super+D or the brass pull opens it. */
	server.drawer_t = 0.0;
	server.drawer_to = 0.0;

	static struct option long_opts[] = {
		{"headless", no_argument, NULL, 'H'},
		{"size", required_argument, NULL, 'z'},
		{"dump", required_argument, NULL, 'D'},
		{"screenshot", required_argument, NULL, 'S'},
		{"delay", required_argument, NULL, 'd'},
		{"exit-after-screenshot", no_argument, NULL, 'E'},
		{"help", no_argument, NULL, 'h'},
		{0, 0, 0, 0},
	};
	int c;
	while ((c = getopt_long(argc, argv, "s:h", long_opts, NULL)) != -1) {
		switch (c) {
		case 's':
			server.startup_cmd = optarg;
			break;
		case 'H':
			server.headless = true;
			break;
		case 'z':
			server.size_set = true;
			if (sscanf(optarg, "%dx%d", &server.headless_w, &server.headless_h) != 2
				|| server.headless_w <= 0 || server.headless_h <= 0) {
				usage(argv[0]);
				return 1;
			}
			break;
		case 'D':
			server.dump_path = optarg;
			break;
		case 'S':
			server.screenshot_path = optarg;
			break;
		case 'd':
			server.screenshot_delay_ms = atoi(optarg);
			break;
		case 'E':
			server.exit_after_shot = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	server.wl_display = wl_display_create();
	struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
	/* before the backend starts: nested on Wayland, the window's first
	 * size comes while it does, and a size with no timer to wait on was
	 * dropped -- the desk stayed the size asked for, in a bigger window */
	server.resize_timer = wl_event_loop_add_timer(loop, resize_cb, &server);
	cad_signals(&server);
	if (server.headless) {
		setenv("WLR_RENDERER", "pixman", 0);
		setenv("WLR_LIBINPUT_NO_DEVICES", "1", 1);
		server.backend = wlr_headless_backend_create(loop);
	} else {
		bool picked = cards_pick();
		server.backend = wlr_backend_autocreate(loop, &server.session);
		if (picked) {
			/* read once, as the backend is made: not for what we start */
			unsetenv("WLR_DRM_DEVICES");
		}
	}
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create backend");
		return 1;
	}
	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create renderer");
		return 1;
	}
	server.soft_render = wlr_renderer_is_pixman(server.renderer);
	if (server.soft_render) {
		wlr_log(WLR_INFO, "the screen is put together on the processor (pixman)");
	}
	/* On the CPU: 2D Fallback, before anything is traced or drawn at the
	 * level the files had (unless the user chose one), and said so. And
	 * chosen nowhere, the level for what draws (gfx_unchosen). */
	render_check(&server);
	desk_set_gfx(gfx_wanted(&server));
	if (server.on_cpu) {
		motion_set(motion_of(&server.conf));
		server.boot_lamp = server.conf.lights_at_login && desk_gfx_level() != DESK_GFX_2D;
		server.boot_light = server.boot_lamp ? 0.0 : 1.0;
		server.side_light = server.boot_light;
	}
	/* The room is traced on the GPU the screen is drawn with. Left to find
	 * one for itself it takes the first render node, which on a laptop
	 * with two GPUs can be the discrete one: asleep until the trace wakes
	 * it, or under a driver that cannot clock it up. */
	int rfd = wlr_renderer_get_drm_fd(server.renderer);
	char *rnode = rfd >= 0 ? drmGetRenderDeviceNameFromFd(rfd) : NULL;
	if (rnode) {
		wlr_log(WLR_INFO, "the screen is drawn on %s; the room is traced there first", rnode);
		desk_room_gpu_prefer(rnode);
		free(rnode);
	}
	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create allocator");
		return 1;
	}
	init_client_buffers(&server);

	struct wlr_compositor *compositor =
		wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);
	/* middle-click paste, and the clipboard for the programs that manage
	 * it (clipboard histories, wl-copy and wl-paste) */
	wlr_primary_selection_v1_device_manager_create(server.wl_display);
	wlr_data_control_manager_v1_create(server.wl_display);
	/* so grim, and anything else that screenshots, can see the output --
	 * xdg-output as well, or the capture comes out zero sized */
	wlr_screencopy_manager_v1_create(server.wl_display);
	/* what the programs a desktop comes with take for granted: a program
	 * asking for its window to come to the front (a link opened from mail,
	 * a second start of a program that is already running), video scaled
	 * by the compositor rather than by the program, frame timing, and
	 * one-colour buffers */
	struct wlr_xdg_activation_v1 *activation = wlr_xdg_activation_v1_create(server.wl_display);
	if (activation) {
		server.request_activate.notify = xdg_request_activate;
		wl_signal_add(&activation->events.request_activate, &server.request_activate);
	}
	wlr_viewporter_create(server.wl_display);
	wlr_presentation_create(server.wl_display, server.backend);
	wlr_single_pixel_buffer_manager_v1_create(server.wl_display);

	server.output_layout = wlr_output_layout_create(server.wl_display);
	server.layout_change.notify = layout_change;
	wl_signal_add(&server.output_layout->events.change, &server.layout_change);
	wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);
	/* the display tools (wlr-randr, kanshi, wdisplays), and wlopm */
	server.om = wlr_output_manager_v1_create(server.wl_display);
	if (server.om) {
		server.om_apply.notify = om_apply_cb;
		wl_signal_add(&server.om->events.apply, &server.om_apply);
		server.om_test.notify = om_test_cb;
		wl_signal_add(&server.om->events.test, &server.om_test);
	}
	struct wlr_output_power_manager_v1 *power =
		wlr_output_power_manager_v1_create(server.wl_display);
	if (power) {
		server.power_set_mode.notify = power_set_mode;
		wl_signal_add(&power->events.set_mode, &server.power_set_mode);
	}
	/* the screen going dark, held up by a film playing, and told to
	 * swayidle */
	wl_list_init(&server.idle_holds);
	struct wlr_idle_inhibit_manager_v1 *inhibit = wlr_idle_inhibit_v1_create(server.wl_display);
	if (inhibit) {
		server.new_idle_hold.notify = server_new_idle_hold;
		wl_signal_add(&inhibit->events.new_inhibitor, &server.new_idle_hold);
	}
	server.idle_notifier = wlr_idle_notifier_v1_create(server.wl_display);

	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);
	server.layer_side = wlr_scene_tree_create(&server.scene->tree);
	server.layer_bg = wlr_scene_tree_create(&server.scene->tree);
	server.layer_desk = wlr_scene_tree_create(&server.scene->tree);
	/* termites' marks in the wood: on the desk, under the paper on it */
	server.layer_scars = wlr_scene_tree_create(&server.scene->tree);
	server.layer_folded = wlr_scene_tree_create(&server.scene->tree);
	server.layer_win = wlr_scene_tree_create(&server.scene->tree);
	server.layer_full = wlr_scene_tree_create(&server.scene->tree);
	server.layer_xover = wlr_scene_tree_create(&server.scene->tree);
	server.layer_drawer = wlr_scene_tree_create(&server.scene->tree);
	server.layer_parked = wlr_scene_tree_create(&server.scene->tree);
	/* and the termites themselves, walking over the paper too */
	server.layer_bugs = wlr_scene_tree_create(&server.scene->tree);
	/* over everything: while you are looking at the shelf you are not
	 * looking at the desk, and nothing on it should show through */
	server.layer_shelf = wlr_scene_tree_create(&server.scene->tree);
	server.shelf_hover = -1;
	server.hover_from = -1;
	server.shelf_binder = -1;
	server.binder_sel = -1;
	/* and the same again for the set at the back left */
	server.layer_console = wlr_scene_tree_create(&server.scene->tree);
	server.layer_post = wlr_scene_tree_create(&server.scene->tree);
	server.pb_hover = -1;
	server.pbc_idx = -1;
	server.pbc_hot = -1;
	server.pbc_act = -1;
	server.cc_hover = -1;
	server.cc_grab = -1;
	/* where the machine has actually left its radios, so the levers do not
	 * start out telling a story of their own */
	struct desk_meters m0;
	desk_meters_read(&m0);
	server.cc_wifi = m0.have_radio ? !m0.wifi_blocked : 1;
	server.cc_ant = server.cc_ant_to = server.cc_wifi ? 1.0 : 0.0;
	server.cc_bt = m0.have_radio ? !m0.bt_blocked : 0;
	server.cc_vol = 0.62;
	server.cc_bright = 1.0;
	server.cc_screen = 1.0;
	screen_read(&server);
	server.cc_pressed = -1;
	server.cc_push = -1;
	server.cc_then = -1;
	{
		/* the night's warm screen, if it was left on */
		char path[600];
		server.cc_night = night_path(path, sizeof path, false) && access(path, F_OK) == 0;
	}
	server.layer_top = wlr_scene_tree_create(&server.scene->tree);
	server.layer_drag = wlr_scene_tree_create(&server.scene->tree);
	/* Alt+Tab's windows spread out: over the desk and all that comes up
	 * in front of it, under the door */
	server.layer_switcher = wlr_scene_tree_create(&server.scene->tree);
	/* and Print's area being chosen, over that too */
	server.layer_snip = wlr_scene_tree_create(&server.scene->tree);
	{
		static const float black[4] = { 0.012f, 0.010f, 0.008f, 1.0f };
		/* the camera, as it comes: on, the dial at an area, the first
		 * frame next */
		server.snip_cam.power = 1.0;
		server.snip_cam.count = 1;
		/* the screens still, under all of it (snip_freeze); over them the
		 * viewfinders' dark, the dark in the eyepiece as it comes up, the
		 * pictures in the viewfinders and their glass edges
		 * (snip_finder_lay, snip_rim_make) */
		server.snip_drag = -1;
		server.snip_stills = wlr_scene_tree_create(server.layer_snip);
		server.snip_backs = wlr_scene_tree_create(server.layer_snip);
		server.snip_hole = wlr_scene_rect_create(server.snip_backs, 1, 1, black);
		wlr_scene_node_set_enabled(&server.snip_hole->node, false);
		server.snip_finders = wlr_scene_tree_create(server.layer_snip);
		server.snip_rims = wlr_scene_tree_create(server.layer_snip);
		/* the shade round the picture, and its penumbra (snip_shade_lay) */
		static const float none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		for (int i = 0; i < 4; i++) {
			server.snip_dark[i] = wlr_scene_rect_create(server.layer_snip, 1, 1, none);
		}
		for (int i = 0; i < 8; i++) {
			int w = i == 0 || i == 2 ? 1 : SNIP_PEN_N, h = i == 1 || i == 3 ? 1 : SNIP_PEN_N;
			struct desk_buf *b = buf_create(w, h);
			desk_draw_finder_penumbra(b->cr, SNIP_PEN_N, i);
			server.snip_pen[i] = scene_new(server.layer_snip, &b->base);
			wlr_buffer_drop(&b->base);
			snip_sprite_ends(server.snip_pen[i], w, h);
			server.snip_pen[i]->point_accepts_input = shadow_accepts;
			wlr_scene_node_set_enabled(&server.snip_pen[i]->node, false);
		}
		/* the glass round the picture: what is under it (a still's, by
		 * snip_glass_lay), its edge, and the lamp in it */
		for (int i = 0; i < 4; i++) {
			server.snip_glass[i] = scene_new(server.layer_snip, NULL);
			server.snip_glass[i]->point_accepts_input = shadow_accepts;
		}
		for (int i = 0; i < 8; i++) {
			int w = i == 0 || i == 2 ? 1 : SNIP_BEVEL_N, h = i == 1 || i == 3 ? 1 : SNIP_BEVEL_N;
			struct desk_buf *b = buf_create(w, h);
			desk_draw_finder_bevel(b->cr, SNIP_BEVEL_N, i);
			server.snip_bevel[i] = scene_new(server.layer_snip, &b->base);
			wlr_buffer_drop(&b->base);
			snip_sprite_ends(server.snip_bevel[i], w, h);
			server.snip_bevel[i]->point_accepts_input = shadow_accepts;
			wlr_scene_node_set_enabled(&server.snip_bevel[i]->node, false);
		}
		for (int i = 0; i < 4; i++) {
			int w = i == 0 || i == 2 ? 256 : 24, h = i == 0 || i == 2 ? 24 : 256;
			struct desk_buf *b = buf_create(w, h);
			desk_draw_finder_glint(b->cr, w, h);
			server.snip_glint[i] = scene_new(server.layer_snip, &b->base);
			wlr_buffer_drop(&b->base);
			server.snip_glint[i]->point_accepts_input = shadow_accepts;
			wlr_scene_node_set_enabled(&server.snip_glint[i]->node, false);
		}
		/* the shutter, over what is in the picture; the flash, over that
		 * and over all the screens */
		server.snip_shut = wlr_scene_rect_create(server.layer_snip, 1, 1, black);
		wlr_scene_node_set_enabled(&server.snip_shut->node, false);
		static const float unlit[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		server.snip_glow = wlr_scene_rect_create(server.layer_snip, 1, 1, unlit);
		server.snip_light = wlr_scene_rect_create(server.layer_snip, 1, 1, unlit);
		wlr_scene_node_set_enabled(&server.snip_glow->node, false);
		wlr_scene_node_set_enabled(&server.snip_light->node, false);
		/* the display and the circle; the camera in the hand, and the one
		 * coming up to the eye, over all of it (their pictures: when they
		 * are first shown) */
		struct wlr_scene_buffer **made[4] = { &server.snip_info, &server.snip_ring,
			&server.snip_hand, &server.snip_hero };
		for (int i = 0; i < 4; i++) {
			*made[i] = scene_new(server.layer_snip, NULL);
			(*made[i])->point_accepts_input = shadow_accepts;
			wlr_scene_node_set_enabled(&(*made[i])->node, false);
		}
		wlr_scene_node_set_enabled(&server.layer_snip->node, false);
		/* what is taken, and the viewfinder's edges, said back from the
		 * threads that make them */
		int *fds[2] = { server.snip_save_fd, server.snip_rim_fd };
		struct wl_event_source **srcs[2] = { &server.snip_save_src, &server.snip_rim_src };
		wl_event_loop_fd_func_t back[2] = { snip_saved, snip_rim_landed };
		for (int k = 0; k < 2; k++) {
			fds[k][0] = fds[k][1] = -1;
			if (pipe(fds[k]) != 0) {
				fds[k][0] = fds[k][1] = -1;
				continue;
			}
			for (int i = 0; i < 2; i++) {
				fcntl(fds[k][i], F_SETFD, FD_CLOEXEC);
				fcntl(fds[k][i], F_SETFL, fcntl(fds[k][i], F_GETFL) | O_NONBLOCK);
			}
			*srcs[k] = wl_event_loop_add_fd(wl_display_get_event_loop(server.wl_display),
				fds[k][0], WL_EVENT_READABLE, back[k], &server);
		}
	}
	/* the self-timer's count, over that */
	server.layer_count = wlr_scene_tree_create(&server.scene->tree);
	/* and over all of it, the front door when the screen is locked */
	server.layer_lock = wlr_scene_tree_create(&server.scene->tree);
	wlr_scene_node_set_enabled(&server.layer_lock->node, false);
	/* and over even that, the light of fireworks: nothing to see but light */
	server.layer_glow = wlr_scene_tree_create(&server.scene->tree);
	/* the keyboard on the screen, over all of that: a password is typed on it too */
	server.layer_keys = wlr_scene_tree_create(&server.scene->tree);
	server.room_top = -1;
	/* and over everything, nested, the black of the screen gone dark */
	server.layer_dark = wlr_scene_tree_create(&server.scene->tree);

	wl_list_init(&server.toplevels);
	wl_list_init(&server.throws);
	/* 4: configure_bounds, the room there is for a new window (2026-11-12, small screens) */
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 4);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	struct wlr_xdg_decoration_manager_v1 *deco = wlr_xdg_decoration_manager_v1_create(server.wl_display);
	server.new_decoration.notify = server_new_decoration;
	wl_signal_add(&deco->events.new_toplevel_decoration, &server.new_decoration);
	/* and GTK 3's way of asking, KDE's: our frame, unless a window says
	 * it has its own (see kde_server_frame) */
	wl_list_init(&server.kde_decos);
	server.kde_deco = wlr_server_decoration_manager_create(server.wl_display);
	if (server.kde_deco) {
		wlr_server_decoration_manager_set_default_mode(server.kde_deco,
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
		server.new_kde_deco.notify = server_new_kde_deco;
		wl_signal_add(&server.kde_deco->events.new_decoration, &server.new_kde_deco);
	}

	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
	/* the pointer as programs have it (desk-session takes it from the look
	 * of other programs) */
	const char *cur_theme = getenv("XCURSOR_THEME");
	const char *cur_size = getenv("XCURSOR_SIZE");
	int cur_px = cur_size && atoi(cur_size) > 0 && atoi(cur_size) <= 256 ? atoi(cur_size) : 24;
	server.cursor_mgr = wlr_xcursor_manager_create(cur_theme && *cur_theme ? cur_theme : NULL,
		(uint32_t)cur_px);
	server.cursor_mode = DESK_CUR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);
	wl_list_init(&server.switches);
	server.pen_left_at = -1e6;
	server.pen_axis.notify = server_pen_axis;
	wl_signal_add(&server.cursor->events.tablet_tool_axis, &server.pen_axis);
	server.pen_proximity.notify = server_pen_proximity;
	wl_signal_add(&server.cursor->events.tablet_tool_proximity, &server.pen_proximity);
	server.pen_tip.notify = server_pen_tip;
	wl_signal_add(&server.cursor->events.tablet_tool_tip, &server.pen_tip);
	server.pen_button.notify = server_pen_button;
	wl_signal_add(&server.cursor->events.tablet_tool_button, &server.pen_button);
	server.touch_down.notify = server_touch_down;
	wl_signal_add(&server.cursor->events.touch_down, &server.touch_down);
	server.touch_motion.notify = server_touch_motion;
	wl_signal_add(&server.cursor->events.touch_motion, &server.touch_motion);
	server.touch_up.notify = server_touch_up;
	wl_signal_add(&server.cursor->events.touch_up, &server.touch_up);
	server.touch_cancel.notify = server_touch_cancel;
	wl_signal_add(&server.cursor->events.touch_cancel, &server.touch_cancel);
	server.touch_frame.notify = server_touch_frame;
	wl_signal_add(&server.cursor->events.touch_frame, &server.touch_frame);

	wl_list_init(&server.keyboards);
	wl_list_init(&server.pointers);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.relative_pointer_manager = wlr_relative_pointer_manager_v1_create(server.wl_display);
	server.tablet_mgr = wlr_tablet_v2_create(server.wl_display);
	server.pointer_constraints = wlr_pointer_constraints_v1_create(server.wl_display);
	if (server.pointer_constraints) {
		server.new_pointer_constraint.notify = desk_new_pointer_constraint;
		wl_signal_add(&server.pointer_constraints->events.new_constraint,
			&server.new_pointer_constraint);
	}
	/* text-input: programs typing through the input method, with DEsk
	 * as the go-between */
	wl_list_init(&server.text_inputs);
	server.ti_mgr = wlr_text_input_manager_v3_create(server.wl_display);
	if (server.ti_mgr) {
		server.new_text_input.notify = server_new_text_input;
		wl_signal_add(&server.ti_mgr->events.text_input, &server.new_text_input);
	}
	server.kb_focus_change.notify = seat_kb_focus_change;
	wl_signal_add(&server.seat->keyboard_state.events.focus_change, &server.kb_focus_change);
	server.keys_inhibit = wlr_keyboard_shortcuts_inhibit_v1_create(server.wl_display);
	if (server.keys_inhibit) {
		server.new_keys_inhibitor.notify = server_new_keys_inhibitor;
		wl_signal_add(&server.keys_inhibit->events.new_inhibitor, &server.new_keys_inhibitor);
	}
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
	server.pointer_focus_change.notify = seat_pointer_focus_change;
	wl_signal_add(&server.seat->pointer_state.events.focus_change, &server.pointer_focus_change);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);
	server.request_set_primary_selection.notify = seat_request_set_primary_selection;
	wl_signal_add(&server.seat->events.request_set_primary_selection,
		&server.request_set_primary_selection);
	server.request_start_drag.notify = seat_request_start_drag;
	wl_signal_add(&server.seat->events.request_start_drag, &server.request_start_drag);
	server.start_drag.notify = seat_start_drag;
	wl_signal_add(&server.seat->events.start_drag, &server.start_drag);
	server.drag_icon_gone.notify = drag_icon_gone;
	wl_list_init(&server.drag_icon_gone.link);
	server.held_gone.notify = held_gone;
	wl_list_init(&server.held_gone.link);

	/* Nested in somebody else's session, the usual name is a trap: a
	 * desktop on X has no WAYLAND_DISPLAY of its own, so every program
	 * started out there tries the default, wayland-0 -- and would open in
	 * here instead of on that desktop. A name of our own, then, which only
	 * what is started from in here is told. */
	const char *socket = NULL;
	static char named[32];
	if (getenv("WAYLAND_DISPLAY") || getenv("DISPLAY")) {
		for (int i = 0; i < 32 && !socket; i++) {
			snprintf(named, sizeof named, "desk-%d", i);
			if (wl_display_add_socket(server.wl_display, named) == 0) {
				socket = named;
			}
		}
	}
	if (!socket) {
		socket = wl_display_add_socket_auto(server.wl_display);
	}
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	if (server.headless) {
		wlr_headless_add_output(server.backend, (unsigned)server.headless_w, (unsigned)server.headless_h);
	}

	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	render_publish(&server, socket);
	setenv("XDG_CURRENT_DESKTOP", "DEsk", true);
	setenv("XDG_SESSION_DESKTOP", "DEsk", true);
	setenv("XDG_SESSION_TYPE", "wayland", true);
	setenv("DESKTOP_SESSION", "DEsk", true);
#ifdef DESK_HAVE_XWAYLAND
	xwayland_start(&server, compositor);
#else
	(void)compositor;
#endif

	/* the desktop's own business at login, when DEsk is the desktop */
	desk_session_start(loop);
	/* and logind's say over the screen lock */
	desk_lock_watch(loop, lock_cb, unlock_cb, &server);
	/* and the way the machine is held, for a tablet's screen (orient_said) */
	server.orient_want = -1;
	if (!desk_orient_watch(loop, orient_said, &server) && getenv("DESK_ORIENT_FAKE")) {
		/* (the lab: no accelerometer, a way of holding it said in seconds from now) */
		server.orient_fake = wl_event_loop_add_timer(loop, orient_fake_cb, &server);
		const char *at = getenv("DESK_ORIENT_FAKE_AT");
		wl_event_source_timer_update(server.orient_fake, at ? (int)(atof(at) * 1000) : 2000);
	}
	if (server.startup_cmd) {
		spawn(server.startup_cmd);
	}

	server.tick = wl_event_loop_add_timer(loop, tick_cb, &server);
	server.clk.timer = wl_event_loop_add_timer(loop, clock_timer_cb, &server);
	/* the lamps on the clock that say a camera or a microphone is on */
	desk_camera_watch(loop, clock_changed_cb, &server);
	/* what day it is, where the house is, the weather out there */
	desk_occasion_init(loop, occasion_changed_cb, &server);
	server.cc_timer = wl_event_loop_add_timer(loop, console_live_cb, &server);
	server.slide_timer = wl_event_loop_add_timer(loop, slide_timer_cb, &server);
	server.slide_of = -1;
	server.drun_timer = wl_event_loop_add_timer(loop, drun_timer_cb, &server);
	server.drun_of = -1;
	/* the wait for the screen to go dark, counted from now */
	server.idle_timer = wl_event_loop_add_timer(loop, idle_cb, &server);
	server.idle_at = real_sec();
	idle_rearm(&server);
	if (idle_ms(&server) > 0) {
		wlr_log(WLR_INFO, "screen: dark after %d s with nobody at it%s",
			idle_ms(&server) / 1000,
			server.conf.lock_screen_off ? ", the door locked first" : "");
	} else {
		wlr_log(WLR_INFO, "screen: never dark by itself");
	}
#ifdef DESK_HAVE_POST
	/* From here on, what the rest of the machine has to say arrives in the
	 * post box rather than in somebody else's toast. */
	if (desk_post_init(loop)) {
		/* the box is empty when the desk starts: every letter after is new */
		server.post_seen_set = true;
		desk_post_on_change(post_changed, &server);
		wlr_log(WLR_INFO, "the post box has the bus: DEsk is the notification daemon");
	} else {
		wlr_log(WLR_INFO, "no session bus; the post box stays empty");
	}
	/* and the tray, and which input method is on, for the clock */
	if (desk_tray_init(loop, slips_bus_changed, &server)) {
		wlr_log(WLR_INFO, "the clock keeps the tray");
	}
	desk_ime_init(loop, slips_bus_changed, &server);
	/* and what is playing, for its page */
	desk_media_init(loop, media_changed, &server);
	/* and programs asking for the screen to stay lit that don't ask the
	 * Wayland way */
	if (desk_saver_init(loop, saver_changed, saver_awake, &server)) {
		wlr_log(WLR_INFO, "screen: org.freedesktop.ScreenSaver is the desk's: programs on the "
			"bus can keep it lit");
	} else {
		wlr_log(WLR_INFO, "screen: org.freedesktop.ScreenSaver is somebody else's, or there is "
			"no bus");
	}
#endif
	wl_event_source_timer_update(server.tick, 1000);

	if (server.screenshot_path) {
		server.shot_timer = wl_event_loop_add_timer(loop, screenshot_cb, &server);
		wl_event_source_timer_update(server.shot_timer, server.screenshot_delay_ms);
	}

	/* Print's camera read behind everything else, for the first Print not
	 * to wait on it */
	desk_finder_camera_preload();
	wlr_log(WLR_INFO, "DEsk running on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server.wl_display);

	/* what is in hand is put down first, as the shelf does: the memo's
	 * last second and a note being written are kept */
	cal_drop(&server);
	memo_drop(&server);
	for (const struct desk_thing *const *t = desk_things; *t; t++) {
		if ((*t)->quitting) {
			(*t)->quitting(&server);
		}
	}
	/* and a film being made, ended here */
	rec_end(&server);
	/* nothing is thrown away on the way out: it is all going */
	server.quitting = true;
	if (server.disp_idle) {
		wl_event_source_remove(server.disp_idle);
		server.disp_idle = NULL;
	}
	if (server.screens_idle) {
		wl_event_source_remove(server.screens_idle);
		server.screens_idle = NULL;
	}
	if (server.end_timer) {
		wl_event_source_remove(server.end_timer);
		server.end_timer = NULL;
	}
	/* and the room is not left being traced into exit() */
	desk_room_stop();
	render_unpublish();
#ifdef DESK_HAVE_XWAYLAND
	xwayland_stop(&server);
#endif
	wl_display_destroy_clients(server.wl_display);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_list_remove(&server.layout_change.link);
	desk_sound_shutdown();
#ifdef DESK_HAVE_POST
	desk_post_finish();
	desk_tray_finish();
	desk_ime_finish();
	desk_media_finish();
	desk_saver_finish();
#endif
	wl_display_destroy(server.wl_display);
	if (lock_fd >= 0) {
		close(lock_fd);
	}
	return 0;
}
