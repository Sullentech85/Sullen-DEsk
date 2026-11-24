/* Part of wm.c: xdg-shell and Xwayland windows.
 * Included by src/wm.c (see the list at its end); not compiled on its own. */

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	fingers_forget_view(toplevel);
	(void)data;
	struct desk_server *server = toplevel->server;
	if (toplevel->ime_popup) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
		if (server->seat->keyboard_state.focused_surface == view_surface(toplevel)) {
			focus_next(server);
		}
		return;
	}
	/* while what it last drew is still there to screw up */
	view_throw_away(toplevel);
	base_forget_view(server, toplevel);
	view_unfold_cancel(toplevel);
	view_sizing_stop(toplevel);
	if (toplevel == server->grabbed) {
		reset_cursor_mode(server);
	}
	switcher_forget(server, toplevel);
	termites_forget_view(server, toplevel);
	sheet_cleanup(toplevel->server, &toplevel->sheet);
	wl_list_remove(&toplevel->link);
	if (toplevel->fullscreen) {
		toplevel->fullscreen = false;
		wlr_scene_node_reparent(&toplevel->scene_tree->node, server->layer_win);
		fullscreen_layer_order(server);
	}
	/* the frame and the shadow are ours, not the client's: they go off
	 * the screen with it */
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	/* the keys go to whatever is on top now, not nowhere */
	if (server->seat->keyboard_state.focused_surface == view_surface(toplevel)) {
		focus_next(server);
	}
	end_check(server);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
	(void)data;
	if (toplevel->xdg_toplevel->base->initial_commit && ime_window_is(toplevel)) {
		/* its own size, and nothing of ours round it; and not a window
		 * for the pointer to find (view_at), so a click on a candidate
		 * leaves the keys where they are */
		toplevel->ime_popup = true;
		toplevel->scene_tree->node.data = NULL;
		struct desk_server *server = toplevel->server;
		for (size_t i = 0; i < sizeof server->ime_win / sizeof *server->ime_win; i++) {
			if (!server->ime_win[i]) {
				server->ime_win[i] = toplevel;
				break;
			}
		}
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
		if (toplevel->deco) {
			wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->deco,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		}
		return;
	}
	if (toplevel->ime_popup) {
		/* the same place as it grows and shrinks, and the cursor moves */
		if (toplevel->scene_tree->node.enabled) {
			ime_window_place(toplevel);
		}
		return;
	}
	if (toplevel->xdg_toplevel->base->initial_commit) {
		/* the size a new window is offered, within what the desk has room
		 * for -- the size of a sheet put down for it, too */
		int ow, oh;
		const struct desk_thing *thing = thing_coming(toplevel);
		window_offer(toplevel->server, &ow, &oh, thing);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, ow, oh);
		/* and the most there is room for, for a program that sizes itself (xdg_wm_base 4) */
		if (wl_resource_get_version(toplevel->xdg_toplevel->resource)
				>= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION) {
			int bw, bh;
			window_room(toplevel->server, &bw, &bh);
			wlr_xdg_toplevel_set_bounds(toplevel->xdg_toplevel, bw, bh);
		}
		/* a program that draws its own frame and is to be laid on our
		 * paper is told so in its first configure, so its first frame
		 * comes without a shadow of its own */
		if ((thing && thing->frame_always)
			|| (view_draws_own_frame(toplevel) && view_wants_paper(toplevel->server, toplevel))) {
			wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel,
				WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
		}
		/* asked for our frame before it could be told: tell it now */
		if (toplevel->deco) {
			wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->deco,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		}
	}
	view_repaint_chrome(toplevel);
}

static void deco_forget(struct desk_toplevel *view);

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
	(void)data;
	struct desk_server *server = toplevel->server;
	for (size_t i = 0; i < sizeof server->ime_win / sizeof *server->ime_win; i++) {
		if (server->ime_win[i] == toplevel) {
			server->ime_win[i] = NULL;
		}
	}
	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);
	wl_list_remove(&toplevel->request_minimize.link);
	wl_list_remove(&toplevel->set_title.link);
	deco_forget(toplevel);
	base_forget_view(toplevel->server, toplevel);
	sheet_cleanup(toplevel->server, &toplevel->sheet);
	/* and are thrown away with it: the frame points back at this */
	wlr_scene_node_destroy(&toplevel->scene_tree->node);
	free(toplevel->thing_data);
	free(toplevel);
}

static void xdg_toplevel_request_move(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	(void)data;
	if (!toplevel->ime_popup) {
		finger_gets_hold(toplevel);
		begin_interactive(toplevel, DESK_CUR_MOVE, 0);
	}
}

static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	if (!toplevel->ime_popup) {
		finger_gets_hold(toplevel);
		begin_interactive(toplevel, DESK_CUR_RESIZE, event->edges);
	}
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
	(void)data;
	if (!toplevel->xdg_toplevel->base->initialized || toplevel->ime_popup) {
		return;
	}
	/* set_maximized and unset_maximized both come here: what is asked for,
	 * not the other of what it is -- a browser coming back from the whole
	 * screen asks again for the opened-out sheet it had, and was given the
	 * small one. Asked for what it is, it is told so (a configure). */
	bool want = toplevel->xdg_toplevel->requested.maximized;
	if (want != (toplevel->maximized != 0)) {
		view_toggle_max(toplevel);
	} else {
		view_set_maximized(toplevel, toplevel->maximized != 0);
	}
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
	(void)data;
	if (toplevel->xdg_toplevel->base->initialized && !toplevel->ime_popup) {
		view_set_fullscreen(toplevel, toplevel->xdg_toplevel->requested.fullscreen);
	}
}

static void xdg_toplevel_request_minimize(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, request_minimize);
	(void)data;
	if (toplevel->sheet.phase == SHEET_IDLE && !toplevel->ime_popup) {
		view_fold_toggle(toplevel);
	}
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, set_title);
	(void)data;
	if (!toplevel->ime_popup) {
		view_repaint_chrome(toplevel);
	}
	view_check_above(toplevel);
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;
	struct desk_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	view_curl_reset(toplevel);
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->scene_tree = wlr_scene_tree_create(server->layer_win);
	toplevel->scene_tree->node.data = toplevel;
	/* shown at map, when it has somewhere to be */
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	toplevel->surface_tree = wlr_scene_xdg_surface_create(toplevel->scene_tree, xdg_toplevel->base);
	xdg_toplevel->base->data = toplevel->scene_tree;

	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);
	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);
	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
	toplevel->request_minimize.notify = xdg_toplevel_request_minimize;
	wl_signal_add(&xdg_toplevel->events.request_minimize, &toplevel->request_minimize);
	toplevel->set_title.notify = xdg_toplevel_set_title;
	wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);
}

/* A menu stays on its screen. One that would run off it -- a right click
 * near the foot of the screen, a submenu at its edge -- is turned over or
 * slid back on, as its program said it may be (xdg-positioner); the screen
 * is the one under where it hangs from. wlroots wants the screen in the
 * terms of the window's own surface: the window's geometry is where its
 * surface node is (surface_tree), and the surface starts that much above
 * and to the left of it. */
static void popup_keep_on_screen(struct desk_popup *popup)
{
	struct wlr_xdg_popup *xp = popup->xdg_popup;
	struct wlr_xdg_surface *root = NULL;
	for (struct wlr_surface *s = xp->parent; s;) {
		struct wlr_xdg_surface *x = wlr_xdg_surface_try_from_wlr_surface(s);
		if (!x) {
			return;
		}
		if (x->role != WLR_XDG_SURFACE_ROLE_POPUP) {
			root = x;
			break;
		}
		s = x->popup ? x->popup->parent : NULL;
	}
	if (!root || root->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL || !root->data) {
		return;
	}
	struct wlr_scene_tree *tree = root->data;
	struct desk_toplevel *view = tree->node.data;
	if (!view || !view->surface_tree) {
		return;
	}
	int gx, gy;
	wlr_scene_node_coords(&view->surface_tree->node, &gx, &gy);
	struct wlr_box geo;
	desk_xdg_surface_geometry(root, &geo);
	int ox = gx - geo.x, oy = gy - geo.y;   /* the surface's corner on the screens */
	const struct wlr_box *a = &xp->scheduled.rules.anchor_rect;
	int ax, ay;
	wlr_xdg_popup_get_toplevel_coords(xp, a->x + a->width / 2, a->y + a->height / 2, &ax, &ay);
	struct wlr_output *out = screen_at(popup->server, ox + ax, oy + ay);
	struct wlr_box sb;
	wlr_output_layout_get_box(popup->server->output_layout, out, &sb);
	if (wlr_box_empty(&sb)) {
		return;
	}
	struct wlr_box box = { sb.x - ox, sb.y - oy, sb.width, sb.height };
	wlr_xdg_popup_unconstrain_from_box(xp, &box);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data)
{
	struct desk_popup *popup = wl_container_of(listener, popup, commit);
	(void)data;
	if (popup->xdg_popup->base->initial_commit) {
		popup_keep_on_screen(popup);
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

/* moved by its program (a menu that grew, a list that scrolled): on its
 * screen again, wherever it is to go now */
static void xdg_popup_reposition(struct wl_listener *listener, void *data)
{
	struct desk_popup *popup = wl_container_of(listener, popup, reposition);
	(void)data;
	popup_keep_on_screen(popup);
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data)
{
	struct desk_popup *popup = wl_container_of(listener, popup, destroy);
	(void)data;
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->reposition.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_xdg_popup);
	struct wlr_xdg_popup *xdg_popup = data;
	/* A popup may come with no parent (another protocol, such as
	 * layer-shell, is meant to give it one later; there is none here, and
	 * wlroots refuses it at its first commit), or on such a popup, which
	 * was given no place. Neither is placed, and neither may bring the
	 * desk down. */
	struct wlr_xdg_surface *parent = xdg_popup->parent
		? wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent) : NULL;
	if (!parent || !parent->data) {
		return;
	}
	struct desk_popup *popup = calloc(1, sizeof(*popup));
	if (!popup) {
		return;
	}
	popup->server = server;
	popup->xdg_popup = xdg_popup;
	struct wlr_scene_tree *parent_tree = parent->data;
	/* A menu is placed against its window's geometry, and the window's
	 * surface node is where that is: inside our frame, not at the
	 * frame's corner (where a menu came up the frame's border to the left
	 * and its heading above where it was asked for). */
	if (parent->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && parent_tree
		&& parent_tree->node.data) {
		struct desk_toplevel *pv = parent_tree->node.data;
		if (pv->surface_tree) {
			parent_tree = pv->surface_tree;
		}
	}
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
	popup->reposition.notify = xdg_popup_reposition;
	wl_signal_add(&xdg_popup->events.reposition, &popup->reposition);
	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

/* A client asking for our frame. It gets it whatever mode it asks for --
 * the frame is the paper -- but it can only be told once its surface is
 * set up, which for most clients is not yet: then at its first commit. */
static void deco_request_mode(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, deco_request_mode);
	(void)data;
	if (view->deco && view->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(view->deco, view->ime_popup
			? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
			: WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
}

static void deco_forget(struct desk_toplevel *view)
{
	if (!view->deco) {
		return;
	}
	wl_list_remove(&view->deco_destroy.link);
	wl_list_remove(&view->deco_request_mode.link);
	view->deco = NULL;
}

static void deco_destroy(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, deco_destroy);
	(void)data;
	deco_forget(view);
	/* it draws its own from now on */
	if (view->scene_tree && view->scene_tree->node.enabled) {
		view_frame_update(view);
	}
}

static void server_new_decoration(struct wl_listener *listener, void *data)
{
	(void)listener;
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	struct wlr_scene_tree *tree = deco->toplevel->base->data;
	struct desk_toplevel *view = tree ? tree->node.data : NULL;
	if (!view || view->deco) {
		return;
	}
	view->deco = deco;
	view->deco_destroy.notify = deco_destroy;
	wl_signal_add(&deco->events.destroy, &view->deco_destroy);
	view->deco_request_mode.notify = deco_request_mode;
	wl_signal_add(&deco->events.request_mode, &view->deco_request_mode);
	if (deco->toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(deco,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	if (view->scene_tree && view->scene_tree->node.enabled) {
		/* asked for late, after it was already on the desk without one */
		view_frame_update(view);
	}
}

/* What a surface says by KDE's protocol has changed, or it has stopped
 * saying anything: if its window is on the desk, its frame is decided
 * again. (One not out yet is decided as it comes out.) */
static void kde_frame_again(struct desk_server *server, struct wlr_surface *surface)
{
	if (server->quitting) {
		return;
	}
	struct desk_toplevel *view = view_of_surface(server, surface);
	if (view && view->xdg_toplevel && view->scene_tree && view->scene_tree->node.enabled) {
		view_frame_update(view);
	}
}

static void kde_deco_mode(struct wl_listener *listener, void *data)
{
	struct desk_kde_deco *k = wl_container_of(listener, k, mode);
	(void)data;
	kde_frame_again(k->server, k->deco->surface);
}

static void kde_deco_destroy(struct wl_listener *listener, void *data)
{
	struct desk_kde_deco *k = wl_container_of(listener, k, destroy);
	(void)data;
	struct desk_server *server = k->server;
	struct wlr_surface *surface = k->deco->surface;
	wl_list_remove(&k->link);
	wl_list_remove(&k->mode.link);
	wl_list_remove(&k->destroy.link);
	free(k);
	/* it draws its own from now on, if it is still there */
	kde_frame_again(server, surface);
}

static void server_new_kde_deco(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_kde_deco);
	struct wlr_server_decoration *deco = data;
	struct desk_kde_deco *k = calloc(1, sizeof *k);
	if (!k) {
		return;
	}
	k->server = server;
	k->deco = deco;
	k->mode.notify = kde_deco_mode;
	wl_signal_add(&deco->events.mode, &k->mode);
	k->destroy.notify = kde_deco_destroy;
	wl_signal_add(&deco->events.destroy, &k->destroy);
	wl_list_insert(&server->kde_decos, &k->link);
	/* said late, when its window was already on the desk */
	kde_frame_again(server, deco->surface);
}

#ifdef DESK_HAVE_XWAYLAND
/* ---- X11 programs, through Xwayland -----------------------------------
 *
 * An X program talks to an X server of its own, which wlroots starts the
 * first time one connects and whose windows come to us as surfaces. Most
 * of them are windows like any other and go on the desk as sheets. The
 * rest are moments of a window -- a menu, a tooltip, a list dropping down
 * -- which X lets a program put anywhere without asking. Those go exactly
 * where they say, over everything, and are not ours to move. */

enum {
	XT_NORMAL, XT_DIALOG, XT_UTILITY, XT_TOOLBAR, XT_SPLASH,
	XT_MENU, XT_DROPDOWN_MENU, XT_POPUP_MENU, XT_TOOLTIP, XT_NOTIFICATION,
	XT_COMBO, XT_DND,
	/* and what a window asks to be as it is mapped (xview_map_request) */
	XS_STATE, XS_FULLSCREEN, XS_MAX_VERT, XS_MAX_HORZ, XT_COUNT
};
static const char *const xtype_names[XT_COUNT] = {
	"_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_DIALOG",
	"_NET_WM_WINDOW_TYPE_UTILITY", "_NET_WM_WINDOW_TYPE_TOOLBAR",
	"_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_MENU",
	"_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_POPUP_MENU",
	"_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
	"_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_DND",
	"_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN",
	"_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
};
_Static_assert(XT_COUNT <= sizeof ((struct desk_server *)0)->xatom
	/ sizeof ((struct desk_server *)0)->xatom[0], "xatom[] is too small");

static bool xview_is(const struct desk_server *server,
	const struct wlr_xwayland_surface *xs, int type)
{
	uint32_t atom = server->xatom[type];
	if (atom == XCB_ATOM_NONE) {
		return false;
	}
	for (size_t i = 0; i < xs->window_type_len; i++) {
		if (xs->window_type[i] == atom) {
			return true;
		}
	}
	return false;
}

static bool xview_is_splash(const struct desk_server *server,
	const struct wlr_xwayland_surface *xs)
{
	return xview_is(server, xs, XT_SPLASH);
}

/* A moment of a window rather than a window. Override-redirect says so
 * outright; the rest say it by their type. */
static bool xview_passing(const struct desk_server *server,
	const struct wlr_xwayland_surface *xs)
{
	if (ime_window_name(xs->instance) || ime_window_name(xs->class)) {
		/* the input method's, whatever it says it is (ime_window_is) */
		return true;
	}
	if (xs->override_redirect) {
		return true;
	}
	static const int passing[] = { XT_MENU, XT_DROPDOWN_MENU, XT_POPUP_MENU,
		XT_TOOLTIP, XT_NOTIFICATION, XT_COMBO, XT_DND };
	for (size_t i = 0; i < sizeof passing / sizeof passing[0]; i++) {
		if (xview_is(server, xs, passing[i])) {
			return true;
		}
	}
	return false;
}

/* Where a new X window goes on the desk, and how big it may be there --
 * which is no bigger than the screen. A program restoring the size it had
 * on a bigger one would otherwise hang off the edges, and come back that
 * way from every maximise. The client is told both. */
static void xview_place(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	struct wlr_xwayland_surface *xs = view->xsurface;
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	const xcb_size_hints_t *hints = xs->size_hints;
	/* the screen it goes on: its window's, for a dialog; the one it asked
	 * to be on; or the pointer's */
	struct desk_toplevel *parent = xs->parent ? xs->parent->data : NULL;
	bool asked = hints && (hints->flags & (XCB_ICCCM_SIZE_HINT_US_POSITION
		| XCB_ICCCM_SIZE_HINT_P_POSITION)) && (xs->x != 0 || xs->y != 0);
	struct wlr_output *out = parent && parent->scene_tree && !parent->unmanaged
		? view_screen(parent) : asked ? screen_at(server, xs->x, xs->y)
		: screen_at(server, server->cursor->x, server->cursor->y);
	struct wlr_box wa = screen_area(server, out);
	int iw = view->outer_w - l - r, ih = view->outer_h - t - b;
	bool resized = false;
	if (wa.width > 0 && wa.height > 0) {
		int max_w = wa.width - l - r;
		int max_h = wa.height - t - b;
		int min_w = 1, min_h = 1;
		if (hints && (hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)) {
			min_w = hints->min_width > 1 ? hints->min_width : 1;
			min_h = hints->min_height > 1 ? hints->min_height : 1;
		}
		int fw = iw > max_w ? max_w : iw;
		int fh = ih > max_h ? max_h : ih;
		fw = fw < min_w ? min_w : fw;
		fh = fh < min_h ? min_h : fh;
		if (fw != iw || fh != ih) {
			iw = fw;
			ih = fh;
			view->outer_w = iw + l + r;
			view->outer_h = ih + t + b;
			resized = true;
		}
	}
	int w = view->outer_w, h = view->outer_h;
	int x, y;
	if (parent && parent->scene_tree && !parent->unmanaged) {
		/* a dialog: over the window it belongs to */
		x = parent->scene_tree->node.x + (parent->outer_w - w) / 2;
		y = parent->scene_tree->node.y + (parent->outer_h - h) / 3;
	} else if (asked) {
		/* it asked to be somewhere: -geometry +x+y, or where it was left */
		x = xs->x - l;
		y = xs->y - t;
	} else if (xview_is(server, xs, XT_SPLASH) || xs->modal) {
		x = wa.x + (wa.width - w) / 2;
		y = wa.y + (wa.height - h) / 2;
	} else {
		struct desk_base *base = base_claim(server, view);
		if (base) {
			x = base->x;
			y = base->y;
		} else {
			cascade_next(server, &x, &y);
		}
	}
	/* all of it on its screen, where it fits, and none of it on the tray */
	if (x + w > wa.x + wa.width) {
		x = wa.x + wa.width - w;
	}
	if (y + h > wa.y + wa.height) {
		y = wa.y + wa.height - h;
	}
	if (x < wa.x) {
		x = wa.x;
	}
	if (y < wa.y) {
		y = wa.y;
	}
	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
	if (resized) {
		view_set_size(view, iw, ih);
	} else {
		view_send_position(view);
	}
}

static void xview_map(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, map);
	(void)data;
	struct desk_server *server = view->server;
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (view->scene_tree || !xs->surface) {
		return;
	}
	view->unmanaged = xview_passing(server, xs);
	if (view->unmanaged) {
		view->scene_tree = wlr_scene_tree_create(server->layer_xover);
		view->surface_tree = wlr_scene_subsurface_tree_create(view->scene_tree, xs->surface);
		wlr_scene_node_set_position(&view->scene_tree->node, xs->x, xs->y);
		/* never while the door is locked: the keys are the lock's; and
		 * never the input method's candidates, which say they take the
		 * keys (IBus's are a "normal" window that accepts input) and took
		 * them from the text they were for -- which put them away */
		if (desk_xwayland_or_wants_focus(xs) && !locked(server)
			&& !ime_window_name(xs->instance) && !ime_window_name(xs->class)) {
			struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
			wlr_seat_keyboard_notify_enter(server->seat, xs->surface,
				kb ? kb->keycodes : NULL, kb ? kb->num_keycodes : 0,
				kb ? &kb->modifiers : NULL);
		}
		return;
	}
	view_frame_init(view);
	view->scene_tree = wlr_scene_tree_create(server->layer_win);
	view->scene_tree->node.data = view;
	view->surface_tree = wlr_scene_subsurface_tree_create(view->scene_tree, xs->surface);
	view->chrome_key = 0;
	int iw = xs->surface->current.width > 0 ? xs->surface->current.width : xs->width;
	int ih = xs->surface->current.height > 0 ? xs->surface->current.height : xs->height;
	view_paint_chrome(view, iw, ih);
	xview_place(view);
	wl_list_insert(&server->toplevels, &view->link);
	view_sync_shadow(view);
	/* opened out first, so that leaving the whole screen leaves it so */
	if (xs->maximized_horz || xs->maximized_vert) {
		view_toggle_max_now(view);
	}
	if (xs->fullscreen) {
		view_set_fullscreen(view, true);
	}
	view_check_above(view);
	if (!view->thing || !(view->thing->no & THING_NO_KEYS)) {
		focus_toplevel(view);
	}
	base_after_map(server, view);
	wlr_log(WLR_INFO, "an X11 window comes onto the desk: %s (%s) at %d,%d, %dx%d%s",
		view_title(view), xs->class ? xs->class : "-",
		view->scene_tree->node.x, view->scene_tree->node.y, view->outer_w, view->outer_h,
		view->fullscreen ? ", on the whole screen" : view->maximized ? ", opened out" : "");
}

/* Off the screen: the scene goes with it, and comes back new if it is
 * mapped again -- as a window, or, if it has changed its mind in between,
 * as a menu. */
static void xview_unmap_now(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (!view->scene_tree) {
		return;
	}
	struct wlr_surface *s = view->xsurface->surface;
	bool had_keys = s && server->seat->keyboard_state.focused_surface == s;
	if (!view->unmanaged) {
		view_throw_away(view);
		base_forget_view(server, view);
		view_unfold_cancel(view);
		view_sizing_stop(view);
		if (view == server->grabbed) {
			reset_cursor_mode(server);
		}
		switcher_forget(server, view);
		termites_forget_view(server, view);
		sheet_cleanup(view->server, &view->sheet);
		wl_list_remove(&view->link);
	}
	wlr_scene_node_destroy(&view->scene_tree->node);
	view->scene_tree = NULL;
	view->surface_tree = NULL;
	view->chrome = NULL;
	view_curl_reset(view);
	memset(&view->shadow, 0, sizeof view->shadow);
	view->chrome_key = 0;
	view->fullscreen = false;
	fullscreen_layer_order(server);
	view->maximized = 0;
	view->unmanaged = false;
	if (had_keys) {
		focus_next(server);
	}
	end_check(server);
}

static void xview_unmap(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, unmap);
	fingers_forget_view(view);
	(void)data;
	xview_unmap_now(view);
}

static void xview_commit(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, commit);
	(void)data;
	if (view->scene_tree && !view->unmanaged) {
		view_repaint_chrome(view);
	}
}

/* What a window asked to be before it was mapped: the whole screen, or
 * opened out. wlroots 0.18 writes its own state over the window's as it
 * maps it, and reads the window's only afterwards, so the wish is gone by
 * then. It is read here, while it is still there, and handed to wlroots as
 * ours; xview_map does the rest. Wine puts a game made on the whole screen
 * there and asks no more (EWMH: honour _NET_WM_STATE when a withdrawn
 * window asks to be mapped). */
static void xview_map_request(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, map_request);
	(void)data;
	struct desk_server *server = view->server;
	struct wlr_xwayland_surface *xs = view->xsurface;
	xcb_connection_t *xc = wlr_xwayland_get_xwm_connection(server->xwayland);
	if (!xc || server->xatom[XS_STATE] == XCB_ATOM_NONE) {
		return;
	}
	xcb_get_property_reply_t *r = xcb_get_property_reply(xc,
		xcb_get_property(xc, 0, xs->window_id, server->xatom[XS_STATE], XCB_ATOM_ATOM, 0, 32),
		NULL);
	bool full = false, max = false;
	if (r && r->type == XCB_ATOM_ATOM && r->format == 32) {
		const xcb_atom_t *a = xcb_get_property_value(r);
		int n = xcb_get_property_value_length(r) / 4;
		for (int i = 0; i < n; i++) {
			full = full || a[i] == server->xatom[XS_FULLSCREEN];
			max = max || a[i] == server->xatom[XS_MAX_VERT] || a[i] == server->xatom[XS_MAX_HORZ];
		}
	}
	free(r);
	if (full && !xs->fullscreen) {
		wlr_xwayland_surface_set_fullscreen(xs, true);
	}
	if (max && !xs->maximized_horz && !xs->maximized_vert) {
		desk_xwayland_set_maximized(xs, true);
	}
}

static void xview_associate(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, associate);
	(void)data;
	struct wlr_surface *s = view->xsurface->surface;
	if (view->associated || !s) {
		return;
	}
	view->map.notify = xview_map;
	wl_signal_add(&s->events.map, &view->map);
	view->unmap.notify = xview_unmap;
	wl_signal_add(&s->events.unmap, &view->unmap);
	view->commit.notify = xview_commit;
	wl_signal_add(&s->events.commit, &view->commit);
	view->associated = true;
}

static void xview_dissociate(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, dissociate);
	(void)data;
	xview_unmap_now(view);
	if (view->associated) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		wl_list_remove(&view->commit.link);
		view->associated = false;
	}
}

/* A client asking to be a size, or somewhere. Before it is on the screen,
 * and for a menu, it gets what it asks for. A window on the desk gets the
 * size; where it lies is ours. */
static void xview_request_configure(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_configure);
	struct wlr_xwayland_surface_configure_event *ev = data;
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (!view->scene_tree || view->unmanaged) {
		wlr_xwayland_surface_configure(xs, ev->x, ev->y, ev->width, ev->height);
		if (view->scene_tree) {
			wlr_scene_node_set_position(&view->scene_tree->node, ev->x, ev->y);
		}
		return;
	}
	if (view->fullscreen) {
		/* the whole of the screen it is on, whatever it asks */
		struct wlr_box box;
		wlr_output_layout_get_box(view->server->output_layout, view_screen(view), &box);
		if (!wlr_box_empty(&box)) {
			wlr_xwayland_surface_configure(xs, (int16_t)box.x, (int16_t)box.y,
				(uint16_t)box.width, (uint16_t)box.height);
			return;
		}
	}
	view_set_size(view, ev->width, ev->height);
}

static void xview_set_geometry(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, set_geometry);
	(void)data;
	if (view->scene_tree && view->unmanaged) {
		wlr_scene_node_set_position(&view->scene_tree->node,
			view->xsurface->x, view->xsurface->y);
	}
}

static bool xview_on_desk(const struct desk_toplevel *view)
{
	return view->scene_tree && !view->unmanaged;
}

static void xview_request_activate(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_activate);
	(void)data;
	if (xview_on_desk(view) && view->sheet.phase == SHEET_IDLE) {
		focus_toplevel(view);
	}
}

static void xview_request_move(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_move);
	(void)data;
	if (xview_on_desk(view)) {
		begin_interactive(view, DESK_CUR_MOVE, 0);
	}
}

static void xview_request_resize(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_resize);
	struct wlr_xwayland_resize_event *ev = data;
	if (xview_on_desk(view)) {
		begin_interactive(view, DESK_CUR_RESIZE, ev->edges);
	}
}

static void xview_request_maximize(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_maximize);
	(void)data;
	if (!xview_on_desk(view)) {
		return;
	}
	struct wlr_xwayland_surface *xs = view->xsurface;
	bool want = xs->maximized_horz || xs->maximized_vert;
	if (want != (view->maximized != 0)) {
		view_toggle_max(view);
	} else {
		view_set_maximized(view, view->maximized != 0);
	}
}

static void xview_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_fullscreen);
	(void)data;
	if (xview_on_desk(view)) {
		wlr_log(WLR_INFO, "%s asks %s the whole screen", view_title(view),
			view->xsurface->fullscreen ? "for" : "to leave");
		view_set_fullscreen(view, view->xsurface->fullscreen);
	}
}

static void xview_request_minimize(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_minimize);
	struct wlr_xwayland_minimize_event *ev = data;
	if (xview_on_desk(view) && ev->minimize == (view->sheet.phase == SHEET_IDLE)) {
		view_fold_toggle(view);
	}
}

static void xview_set_title(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, set_title);
	(void)data;
	if (xview_on_desk(view)) {
		view_repaint_chrome(view);
		view_check_above(view);
	}
}

static void xview_set_decorations(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, set_decorations);
	(void)data;
	if (!xview_on_desk(view)) {
		return;
	}
	view_frame_update(view);
}

static void xview_destroy(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, destroy);
	(void)data;
	xview_unmap_now(view);
	if (view->associated) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		wl_list_remove(&view->commit.link);
	}
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->associate.link);
	wl_list_remove(&view->dissociate.link);
	wl_list_remove(&view->map_request.link);
	wl_list_remove(&view->request_configure.link);
	wl_list_remove(&view->request_activate.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->request_minimize.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->set_decorations.link);
	wl_list_remove(&view->set_geometry.link);
	sheet_cleanup(view->server, &view->sheet);
	view->xsurface->data = NULL;
	free(view->thing_data);
	free(view);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *xs = data;
	struct desk_toplevel *view = calloc(1, sizeof *view);
	if (!view) {
		return;
	}
	view->server = server;
	view->xsurface = xs;
	view_curl_reset(view);
	xs->data = view;
	struct { struct wl_signal *sig; struct wl_listener *l; wl_notify_func_t fn; } hooks[] = {
		{ &xs->events.destroy, &view->destroy, xview_destroy },
		{ &xs->events.associate, &view->associate, xview_associate },
		{ &xs->events.dissociate, &view->dissociate, xview_dissociate },
		{ &xs->events.map_request, &view->map_request, xview_map_request },
		{ &xs->events.request_configure, &view->request_configure, xview_request_configure },
		{ &xs->events.request_activate, &view->request_activate, xview_request_activate },
		{ &xs->events.request_move, &view->request_move, xview_request_move },
		{ &xs->events.request_resize, &view->request_resize, xview_request_resize },
		{ &xs->events.request_maximize, &view->request_maximize, xview_request_maximize },
		{ &xs->events.request_fullscreen, &view->request_fullscreen, xview_request_fullscreen },
		{ &xs->events.request_minimize, &view->request_minimize, xview_request_minimize },
		{ &xs->events.set_title, &view->set_title, xview_set_title },
		{ &xs->events.set_decorations, &view->set_decorations, xview_set_decorations },
		{ &xs->events.set_geometry, &view->set_geometry, xview_set_geometry },
	};
	for (size_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++) {
		hooks[i].l->notify = hooks[i].fn;
		wl_signal_add(hooks[i].sig, hooks[i].l);
	}
}

/* Two holes in wlroots 0.18's handling of an Xwayland that dies, both of
 * which take the whole compositor down with it -- and an X server does die:
 * a program crashes it, a driver does, somebody kills it. Both are patched
 * from out here, in the server's public state.
 *
 * The window manager's socket belongs to its xcb connection, which closes
 * it -- and wlroots closes it again when the server goes, by which time the
 * number may be somebody else's: the restart has just opened new ones. The
 * wrong file goes, the event loop is left dispatching into freed memory.
 * So wlroots is made to forget the socket once xcb has it.
 *
 * And if Xwayland dies before it has said it is ready, wlroots leaves the
 * dead client in place for the readiness handler to deal with -- which
 * destroys it a second time. So it is taken out of the server once gone. */
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR == 18
#define DESK_XWAYLAND_PATCHES 1
#endif

static void xwayland_client_gone(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, xwayland_client_gone);
	wl_list_init(&listener->link);
	struct wlr_xwayland_server *xs = server->xwayland ? server->xwayland->server : NULL;
	if (xs && xs->client == data) {
		xs->client = NULL;
	}
	/* and see, in a moment, whether wlroots has given up on it */
	if (server->xwayland_retry) {
		wl_event_source_timer_update(server->xwayland_retry, 1500);
	}
}

static void xwayland_started(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, xwayland_start);
	(void)data;
	struct wlr_xwayland_server *xs = server->xwayland->server;
	wl_list_remove(&server->xwayland_client_gone.link);
	wl_list_init(&server->xwayland_client_gone.link);
	if (xs && xs->client) {
		wl_client_add_destroy_listener(xs->client, &server->xwayland_client_gone);
	}
}

/* X is up -- the first X program has connected. Give it the seat, an
 * arrow for over its own windows, and learn the names of the window types. */
static void xwayland_ready(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, xwayland_ready);
	(void)data;
#ifdef DESK_XWAYLAND_PATCHES
	/* the window manager's xcb connection has the socket now */
	if (server->xwayland->server && server->xwayland->xwm) {
		server->xwayland->server->wm_fd[0] = -1;
	}
#endif
	wlr_xwayland_set_seat(server->xwayland, server->seat);
	if (wlr_xcursor_manager_load(server->cursor_mgr, 1)) {
		struct wlr_xcursor *xc = wlr_xcursor_manager_get_xcursor(server->cursor_mgr,
			"default", 1);
		if (xc && xc->image_count > 0) {
			struct wlr_xcursor_image *im = xc->images[0];
			desk_xwayland_set_cursor_image(server->xwayland, im);
		}
	}
	/* where windows go, for the programs that place their own */
	struct wlr_box wa = work_area(server);
	wlr_xwayland_set_workareas(server->xwayland, &wa, 1);
	xcb_connection_t *xc = wlr_xwayland_get_xwm_connection(server->xwayland);
	if (xc) {
		xcb_intern_atom_cookie_t ck[XT_COUNT];
		for (int i = 0; i < XT_COUNT; i++) {
			ck[i] = xcb_intern_atom(xc, 0, (uint16_t)strlen(xtype_names[i]), xtype_names[i]);
		}
		for (int i = 0; i < XT_COUNT; i++) {
			xcb_intern_atom_reply_t *rep = xcb_intern_atom_reply(xc, ck[i], NULL);
			server->xatom[i] = rep ? rep->atom : XCB_ATOM_NONE;
			free(rep);
		}
	}
	wlr_log(WLR_INFO, "X11 programs: Xwayland is up on DISPLAY=%s",
		server->xwayland->display_name);
}

/* Is there an Xwayland to start? WLR_XWAYLAND if it is set, else the first
 * on PATH -- which is then handed to wlroots, which otherwise only knows the
 * one path it was built with. */
static bool xwayland_find(void)
{
	const char *set = getenv("WLR_XWAYLAND");
	if (set && *set) {
		return access(set, X_OK) == 0;
	}
	const char *path = getenv("PATH");
	char buf[1024];
	for (const char *p = path ? path : ""; *p;) {
		const char *e = strchr(p, ':');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		if (n > 0 && n + sizeof "/Xwayland" < sizeof buf) {
			memcpy(buf, p, n);
			memcpy(buf + n, "/Xwayland", sizeof "/Xwayland");
			if (access(buf, X_OK) == 0) {
				setenv("WLR_XWAYLAND", buf, 1);
				return true;
			}
		}
		if (!e) {
			break;
		}
		p = e + 1;
	}
	return access("/usr/bin/Xwayland", X_OK) == 0;
}

static int xwayland_retry_cb(void *data);

static void xwayland_start(struct desk_server *server, struct wlr_compositor *compositor)
{
	const char *off = getenv("DESK_NO_XWAYLAND");
	if (off && *off && strcmp(off, "0") != 0) {
		wlr_log(WLR_INFO, "X11 programs: off (DESK_NO_XWAYLAND)");
		return;
	}
	if (!xwayland_find()) {
		wlr_log(WLR_INFO, "X11 programs: no Xwayland on this machine, "
			"so Wayland programs only");
		return;
	}
	/* Lazily: no X server runs, and nothing is spent on one, until the
	 * first X program connects. */
	server->xwayland = wlr_xwayland_create(server->wl_display, compositor, true);
	if (!server->xwayland) {
		wlr_log(WLR_ERROR, "X11 programs: could not set up Xwayland, "
			"so Wayland programs only");
		return;
	}
	server->xwayland_ready.notify = xwayland_ready;
	wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);
	server->new_xwayland_surface.notify = server_new_xwayland_surface;
	wl_signal_add(&server->xwayland->events.new_surface, &server->new_xwayland_surface);
	server->xwayland_client_gone.notify = xwayland_client_gone;
	wl_list_init(&server->xwayland_client_gone.link);
	server->xwayland_start.notify = xwayland_started;
	wl_list_init(&server->xwayland_start.link);
	if (!server->xwayland_retry) {
		server->xwayland_retry = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), xwayland_retry_cb, server);
	}
#ifdef DESK_XWAYLAND_PATCHES
	if (server->xwayland->server) {
		wl_signal_add(&server->xwayland->server->events.start, &server->xwayland_start);
	}
#endif
	/* what everything started from here on connects to -- in place of the
	 * X server of the session we may be running in a window of */
	setenv("DISPLAY", server->xwayland->display_name, 1);
	wlr_log(WLR_INFO, "X11 programs: DISPLAY=%s, Xwayland starts with the first one",
		server->xwayland->display_name);
}

static void xwayland_stop(struct desk_server *server);

/* An X server that dies while it is still starting is not started again:
 * wlroots takes its display down and leaves it down, and no X program opens
 * again until DEsk is restarted. So if that is what has happened, set X
 * up again from the beginning -- a few times, and then leave it, rather than
 * start one for ever that dies every time. */
static int xwayland_retry_cb(void *data)
{
	struct desk_server *server = data;
	struct wlr_xwayland_server *xs = server->xwayland ? server->xwayland->server : NULL;
	if (!server->xwayland || (xs && xs->display != -1)) {
		return 0;   /* still there, or restarting by itself */
	}
	double now = real_sec();
	if (now - server->xwayland_rebuilt_at > 60.0) {
		server->xwayland_rebuilds = 0;
	}
	if (server->xwayland_rebuilds >= 3) {
		wlr_log(WLR_ERROR, "X11 programs: Xwayland keeps dying as it starts; "
			"leaving it off until DEsk is restarted");
		return 0;
	}
	server->xwayland_rebuilds++;
	server->xwayland_rebuilt_at = now;
	wlr_log(WLR_INFO, "X11 programs: Xwayland died while starting; setting it up again");
	struct wlr_compositor *compositor = server->xwayland->compositor;
	xwayland_stop(server);
	xwayland_start(server, compositor);
	return 0;
}

static void xwayland_stop(struct desk_server *server)
{
	if (!server->xwayland) {
		return;
	}
	wl_list_remove(&server->xwayland_ready.link);
	wl_list_remove(&server->new_xwayland_surface.link);
	wl_list_remove(&server->xwayland_start.link);
	wl_list_remove(&server->xwayland_client_gone.link);
	wl_list_init(&server->xwayland_client_gone.link);
	wlr_xwayland_destroy(server->xwayland);
	server->xwayland = NULL;
}
#endif
