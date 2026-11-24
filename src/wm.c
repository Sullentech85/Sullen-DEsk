/* ================================================================
 * desk: the compositor.
 *
 * The whole desktop is this one program on wlroots 0.18: the outputs and
 * the input devices, the windows (xdg-shell and Xwayland) laid on paper,
 * and every part of the desk a hand can use -- the drawer, the bookcase and
 * its binders and book, the calendar and memo pad, the Control Panel and
 * the tray under it, the post box, the clock, the front door (the lock),
 * Ctrl+Alt+Del -- with shell.conf and displays.conf read and written here.
 * What any of it looks like is drawn by the drawing library (draw.c,
 * include/desk.h); this file decides when, where, and in answer to what.
 *
 * One struct desk_server holds the state. The scene is a stack of layers,
 * bottom to top in main(), and docs/ARCHITECTURE.md (4.1) says what goes on
 * which. A frame is output_frame: the animations moved on by real time
 * (output_frame_work), then the scene committed -- and nothing drawn at
 * all when nothing has changed. Pointer events go through
 * process_cursor_motion, server_cursor_button and server_cursor_axis in a
 * fixed order (the lock, what is carried, a window being moved, the
 * screens beside the desk's, the view turned to, the desk, the windows);
 * keys through handle_keybinding.
 *
 * The file is in sections, each starting with a comment of the form
 * ---- what it is ----: search for "---- " for a table of contents.
 * ================================================================ */

#include "compat.h"
#include "i18n.h"
#include "memo.h"
#include "session.h"
#include "lock.h"
#include "orient.h"
#include "camera.h"
#include "cards.h"
#include "occasion.h"
#include "fonts.h"
#include "desk.h"
#include "apps.h"
#include "panel.h"
#include "power.h"
#include "rec.h"
#include "sound.h"
#include "thing.h"
#include "looks.h"
#include "touch.h"
/* What a letter is, bus or no bus: without one the box is simply empty. */
#include "post.h"
#include "ime.h"
#include "agenda.h"
#ifdef DESK_HAVE_POST
#include "tray.h"
#include "saver.h"
#endif
#ifdef DESK_HAVE_GPU
/* what the screen is drawn with: a GPU, or the processor in its place */
#include "gpu.h"
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#endif

#include <assert.h>
#include <drm_fourcc.h>
#include <getopt.h>
#include <linux/input-event-codes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <xf86drm.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#ifdef DESK_HAVE_LIBINPUT
#include <wlr/backend/libinput.h>
#endif
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>
#include <wlr/render/pass.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_swapchain_manager.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/util/region.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_switch.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>
#include <wlr/util/transform.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/xcursor.h>
#include <wlr/config.h>
#include <wlr/backend/wayland.h>
#if WLR_HAS_X11_BACKEND
#include <wlr/backend/x11.h>
#endif
#ifdef DESK_HAVE_XWAYLAND
#include <wlr/version.h>
#include <wlr/xwayland.h>
#include <wlr/xwayland/server.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#endif

enum desk_cursor_mode {
	DESK_CUR_PASSTHROUGH,
	DESK_CUR_MOVE,
	DESK_CUR_RESIZE,
	/* the bottom edge of a sheet lying on the drawer's pull, pressed: a
	 * click lifts it off (view_peek), a pull is a pull (DESK_CUR_RESIZE) */
	DESK_CUR_PEEK,
};

enum desk_role {
	DESK_ROLE_VIEW = 1,
	DESK_ROLE_DOCK,
	DESK_ROLE_SHEET,
};

/* the most windows Alt+Tab spreads out (switcher_step) */
#define SWITCHER_MAX 48

enum desk_sheet_phase {
	SHEET_IDLE = 0,
	SHEET_FOLDING,
	SHEET_PARKED,
	SHEET_UNFOLDING,
};

/* A window's shadow lives in its own scene node instead of being painted
 * into the chrome. Moving a window then costs one node position update
 * rather than a repaint of the whole frame, which is what lets the shadow
 * swing round as the window crosses the lamp. */
struct desk_shadow {
	struct wlr_scene_buffer *node;
	int w, h;
	double side, amount;    /* which corner of the sheet is up, and how far */
	/* Where the shadow has got to, which is not where the sheet is. A sheet
	 * dragged across a desk does not carry its shadow rigidly: it bends,
	 * and the shadow comes along behind and catches up afterwards. */
	double lx, ly, vx, vy;  /* how far behind, and how fast it is closing */
	double px, py;          /* where the sheet was last time we looked */
	double held;            /* 0 down on the desk .. 1 picked up */
	double at;
	int settled;
	bool stretch;
	bool low;               /* it is thick and heavy, and lies flat: no air under it */
};

struct desk_sheet {
	enum desk_sheet_phase phase;
	cairo_surface_t *src;
	struct desk_fold *fold;   /* how it folds, and what is printed on its back */
	double start;
	double dur;
	double x0, y0, x1, y1;
	double hit_x, hit_y, hit_w, hit_h;
	int slot;
	unsigned seed;   /* which way this sheet's folds go wrong */
	bool book;       /* a notebook, which shuts (src is it with its wire) */
	/* put away in the drawer rather than lying on the desk (and then not
	 * holding a place along the front of it) */
	bool in_drawer;
	bool to_drawer;  /* being folded straight into it (Shell Settings) */
	/* gone past with Alt+Tab while it opened out: once open, it does not
	 * take the keys back from the window they went on to */
	bool passed;
	int snd_k;       /* the fold last heard going over (asked for, ahead) */
	/* its sounds asked for ahead and not yet heard, to be forgotten if
	 * it turns back; and whether the one for its landing has been */
	int snd_due[6], snd_due_i;
	bool snd_end;
	/* on the fingers of a hand: how far folded, and then let go of, how fast and which way */
	bool by_hand, hand_held, hand_free;
	double hand_t, hand_v, hand_at;
	char name[96];   /* what is printed on the outside of the packet */
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *buf;
	struct wlr_scene_tree *live;
	void *hit;
};

/* A window spread out over the desk by Alt+Tab: a picture of its sheet, as
 * it was when they were spread, flown between where it lies (h*) and its
 * place in the spread (s*), in screen coordinates. */
struct sw_paper {
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *img, *shadow;
	struct wlr_scene_rect *dim;
	/* the label on it: its program's mark and name (tag_pad: the shadow's
	 * room round it) */
	struct wlr_scene_buffer *tag;
	int tag_w, tag_h, tag_pad;
	/* the picture whole, and drawn again at its size in the spread (the
	 * one shown: small_on); src, the picture until then */
	struct wlr_buffer *full, *small;
	bool small_on;
	cairo_surface_t *src;
	int iw, ih;               /* the picture's own size */
	int shw, shh;             /* the size its shadow was drawn for */
	double hx, hy, hw, hh;    /* where it lies */
	double sx, sy, sw, sh;    /* where it is in the spread */
	double x, y, w, h;        /* and where it has got to */
	double lift;              /* 0 .. 1: the one it is on, picked up */
};

/* The shell's own settings (shell.conf): see "the shell's own settings". */
struct shell_conf {
	bool paper_all;             /* programs with frames of their own on paper too */
	bool throw_closed;          /* a closed window screwed up and thrown away */
	bool corner_hover;          /* a corner turns up when the hand rests on it */
	double scale;               /* points to pixels; 0: the screen's own */
	double motion;              /* how long things take, against normal */
	double night;               /* how warm NIGHT makes the screens: 1 as made,
	                             * 0.5 light .. 2 strongest (night_strength) */
	double bounce;              /* how much the camera goes past and settles,
	                             * and the head bobs walking: 1 as made, 0 none */
	bool clock_12h;
	double binder_ink;          /* how dark the binders' labels: 0 faded .. 1 darkest */
	char desk_mat[32];          /* the mat on the desk: none, a preset, #rrggbb, photo or clear */
	char desk_mat_photo[512];   /* the picture for photo, and for a clear mat's whole */
	double desk_mat_size;       /* desk_mat_size: 0 .. 1 */
	int desk_mat_under;         /* under a clear mat: DESK_MAT_UNDER_FREE or _WHOLE */
	int desk_wood, desk_stain;  /* what the desk is made of: enum desk_wood, desk_stain */
	char language[16];          /* "auto", "en", "ja", "zh", ... */
	bool drawer_closes;         /* the drawer goes back after something is used */
	bool take_sheet;            /* ALL APPS: the sheet is carried down for the window */
	bool fold_drawer;           /* a folded window goes into the drawer */
	double sound;               /* how loud the desk is: 0 silent */
	bool letter_sound;          /* a letter arriving is heard */
	/* Wood Maintenance (termites_*): whether termites come out, after how
	 * long left alone (seconds), how many (0 one, 1 a few, 2 a lot), what
	 * they may nibble besides the wood, and whether their marks stay */
	bool termites;
	int termites_wait, termites_level;
	bool termites_windows, termites_packets, termites_keep;
	bool lights_at_login;       /* the room starts dark and the lamp comes up */
	bool auto_rotate;           /* the built-in screen turned the way the machine is held */
	int gfx;                    /* how much is drawn: DESK_GFX_* (graphics) */
	bool gfx_own;               /* and chosen in the user's own file, not the machine's */
	bool gfx_set;               /* chosen in either file at all (gfx_wanted) */
	bool reading_own;           /* (the user's own file is being read) */
	int bday_month, bday_day;   /* the birthday; 0 for none */
	bool celebrations;          /* days kept: the door dressed, the clock's shows */
	bool weather;               /* the door out in the real weather (asked for) */
	int screen_off;             /* minutes untouched before the screen goes dark; 0 never */
	bool lock_screen_off;       /* and the door comes down as it does */
	/* the keyboard and the pointer (input_apply) */
	int onscreen_keyboard;      /* DESK_KEYS_* */
	bool touch_swapped;         /* two touch screens, the other way round */
	bool tap_click;             /* a touchpad: a tap is a click */
	bool natural_scroll;        /* a touchpad: the page follows the fingers */
	bool natural_wheel;         /* a mouse: the page goes the way the wheel turns */
	bool left_handed;           /* the buttons the other way round */
	double pointer_speed;       /* libinput's, -1 to 1 */
	char kb_layout[32];         /* an xkb layout; "" the system's */
	char kb_second[32];         /* a second one to change to; "" none */
	char kb_switch[40];         /* the xkb option that changes between them */
	int repeat_rate, repeat_delay;   /* keys a second, and ms before the first */
	int n_apps;
	struct {
		char id[96];
		bool paper;
	} apps[64];                 /* and one program the other way */
	bool loaded, there;
	time_t mtime;
	long mtime_ns;
	off_t size;
};

static bool mat_apply(struct desk_server *server, const struct shell_conf *c, bool roll);

/* One thing in the drawer: a program, kept there; the screwdriver (Shell
 * Settings); Print's camera; or a sheet folded away into it, which is there
 * only while it is. */
enum drawer_kind {
	DRAWER_APP,
	DRAWER_PANEL,
	DRAWER_SHEET,
	DRAWER_STD,      /* one of DEsk's standard programs, whichever it is now */
	DRAWER_CAMERA,   /* Print's camera: it is put up to the eye once the drawer is in */
	DRAWER_THING,    /* a thing kept here (thing.h): put down on the desk, and not in the drawer then */
};

#define DRAWER_MAX 32

/* What a program in the drawer looks like when it is not as it would be (looks.h): kept by what the
 * drawer's file calls it. */
#define DRAWER_LOOKS_MOST 128
#define DRAWER_LOOKS_ON_CARD 64
/* a program's icon lying there by itself: this long, in the room's units, and this thick */
#define DRAWER_ICON_ALONG 1.35
#define DRAWER_ICON_THICK 0.05
struct drawer_look {
	char key[128];
	char look[96];
};

struct drawer_entry {
	enum drawer_kind kind;
	char id[96];                  /* a program: its desktop file id */
	int std;                      /* a standard program: which (DESK_STD_*) */
	const struct desk_thing *thing;  /* a thing kept here */
	struct desk_sheet *sheet;   /* a sheet folded away */
	cairo_surface_t *packet;      /* and the packet it makes, made once */
	bool present;                 /* a program that is installed */
	double x, fit;                /* where it lies along the row, and how big */
	double x_to, fit_to;          /* and where it is on its way to */
};

/* A sheet taken out of a binder and put down on the desk: the program's
 * window comes to it (see "a sheet taken out of a binder"). */
enum base_state {
	BASE_FREE = 0,
	BASE_CARRIED,   /* in the hand, on its way down to the desk */
	BASE_OPENING,   /* put down, the paper coming out from behind it */
	BASE_WAITING,   /* open: the window, blank, the program not here yet */
	BASE_GOING,     /* the window has come, and it goes from under it */
};

#define DESK_BASES 4
#define TAKE_LIFT 0.22      /* off the ring and into the hand */
#define TAKE_TURN 0.22      /* when you turn back to the desk */
#define TAKE_DOWN 0.30      /* put down, once you are there */
#define BASE_OPEN_S 0.48    /* the paper coming out from behind it */
#define BASE_GO_S 0.25      /* and going from under the window */
#define BASE_WAIT_S 45.0    /* no window by then: thrown away */
#define BASE_GONE_S 5.0     /* and sooner, once nothing of the program runs */

struct desk_base {
	enum base_state state;
	char app[96];                 /* the program's desktop file id */
	char name[128];
	int x, y, w, h;               /* the frame of the window it is for, on the desk */
	double lw, lh;                /* the sheet that was put down, in the middle of it */
	cairo_surface_t *face;        /* what the catalogue printed on that sheet */
	cairo_surface_t *blank;       /* the paper the window will be */
	double at;                    /* when it went into the state it is in */
	double taken_at;
	unsigned seed;
	pid_t sid;                    /* the session its program was started in, or 0 */
	struct desk_toplevel *view; /* the window that has come to it */
	struct wlr_scene_buffer *node;
};

/* One thing lying over the desk, kept the way a game keeps a sprite: its
 * own small buffer, drawn again only when its key says what it shows has
 * changed, and otherwise only ever moved. */
struct desk_sprite {
	struct wlr_scene_buffer *node;
	uint64_t key;
	bool drawn;
	int x, y;        /* on screen, as drawn: with the drawer shut */
	int dw, dh;      /* how big it is shown there */
	int ox, oy;      /* the overlay's pixel its picture's first pixel is */
	int bw, bh;      /* and the picture's size */
};

/* The book of every program lying still, drawn ahead and kept (book_ahead). */
#define BOOK_AHEAD 6
struct book_kept {
	struct desk_buf *buf;   /* held by a lock of its own; NULL: an empty slot */
	uint64_t key;
	int x, y, dw, dh;       /* where it goes on the screen, as shown there */
	int ox, oy;             /* the overlay's pixel its first pixel is */
	double used;            /* when it was last wanted: the oldest goes first */
};

struct desk_server;

/* the shelf view's layers, bottom to top */
enum { SH_CASE, SH_HOVER, SH_GADGET0, SH_GADGET1, SH_GADGET2, SH_BINDER, SH_BOOKHOT,
	SH_BOOKPLATE, SH_LEAF, SH_LAYERS };

struct desk_output {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
	/* each frame as it goes out, for Print's picture of it (snip_want) */
	struct wl_listener commit;
	bool snip_want;
	/* Turned off from a display tool (off): that is how the screens are
	 * set up, and nothing but a display tool turns it on again. Gone dark
	 * (dark): nobody has touched anything for a while, and the next touch
	 * undoes it -- the mode it was in, to come back in; nested, where the
	 * window on the other desktop is not ours to turn off, the black laid
	 * over it instead and whether that has been shown yet. */
	bool off, dark, dark_shown;
	struct wlr_output_mode *dark_mode;
	int dark_w, dark_h, dark_refresh;
	struct wlr_scene_rect *dark_cover;
	/* the mode it was in when a display tool turned it off, for when one
	 * turns it on again without saying which (om_try) */
	struct wlr_output_mode *off_mode;
	int off_w, off_h, off_refresh;
	/* the night's warm screen to be put on it (or taken off) with its next frame */
	bool gamma_dirty;
	/* DISPLAY's duplicate (screens_set): this one showing the desk's own screen, laid on it, at
	 * the scale that fits the desk into it; and where it lay beside the desk before, for when
	 * the desk goes on onto it again */
	bool mirror;
	double mirror_scale;
	bool ext_known;
	int ext_x, ext_y;
	/* A screen beside the desk's own: the bare desk under its windows
	 * (side_desks_paint), and what it was drawn for -- where the screen
	 * is, how big, and the room it is cut from. */
	struct wlr_scene_buffer *side;
	struct wlr_box side_box;
	unsigned side_room;
	int side_level;
};

/* A program asking for the screen to stay lit (idle-inhibit): a film being
 * played, a call. It holds only while its window is out to be seen. */
struct desk_idle_hold {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	struct wl_listener destroy, map, unmap;
};

/* A window: a Wayland one, or an X one come through Xwayland. Exactly one
 * of xdg_toplevel and xsurface is set, and nearly everything below is the
 * same for both -- the paper, its frame, its shadow, the fold. Where the
 * two protocols differ the view_* helpers do, and nothing else asks. */
struct desk_toplevel {
	struct wl_list link;
	struct desk_server *server;
	bool above;      /* kept in front of the other sheets: a picture-in-picture */
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_xwayland_surface *xsurface;
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *surface_tree;
	struct wlr_scene_buffer *chrome;
	int outer_w, outer_h;
	int maximized;
	int saved_x, saved_y, saved_inner_w, saved_inner_h;
	/* laid over one half of its screen (view_lay_half): -1 the left, 1 the
	 * right, 0 neither; and where it was before, and how big, to go back */
	int half;
	int half_x, half_y, half_inner_w, half_inner_h;
	/* its bottom edge lifted off the drawer's pull (view_peek): how tall
	 * it was, and how tall it was made; 0, not lifted */
	int peek_inner_h, peek_to_h;
	/* No frame of ours: the client draws its own (or asks for none), or
	 * it has the whole screen. The client then starts at the sheet's
	 * corner, and the paper is only as big as the client. */
	bool bare;
	/* It draws its own frame and is laid on our paper anyway: a plain
	 * margin of paper round it, no heading (see "whose frame a window
	 * has"). */
	bool own_frame;
	/* What it is, if it is not paper (thing.h). Wanted is what it has been found to
	 * be, the other what its frame has been made for. */
	const struct desk_thing *thing, *thing_want;
	bool thing_placed;                  /* it has been put down where it goes */
	void *thing_data;                   /* what a thing keeps for this window */
	const struct desk_thing *thing_data_of;
	bool fullscreen;
	int fs_x, fs_y, fs_inner_w, fs_inner_h;   /* where it was before */
	/* A Wayland client that draws its own frame -- a GTK one with a header
	 * bar, which says so -- also draws its own shadow round it, in a margin
	 * of its surface outside the window proper. */
	bool own_shadow;
	/* it asked for ours by xdg-decoration (GTK 3 asks by KDE's protocol:
	 * kde_server_frame) */
	struct wlr_xdg_toplevel_decoration_v1 *deco;
	struct wl_listener deco_destroy;
	struct wl_listener deco_request_mode;
	/* what the frame was last drawn as: a commit that changes none of it
	 * -- which is nearly all of them -- draws nothing */
	uint64_t chrome_key;
	/* The turned-down top right corner, with what can be done with the
	 * sheet printed on its back (desk_draw_curl): how far it is turned
	 * down and how fast it is moving there, whether the pointer is near
	 * enough to lift it, the instruction under the pointer, and what was
	 * pressed while its turn of the corner plays out. */
	struct wlr_scene_buffer *curl;
	double curl_s, curl_v, curl_at, curl_box;
	double curl_finger;          /* on a finger, this far; 0 not */
	/* what lies under the corner, where the paper was: the desk and
	 * whatever sheets are beneath, put together once per lift */
	cairo_surface_t *curl_under;
	int curl_under_x, curl_under_y, curl_under_c;
	bool curl_under_full;
	bool curl_near;        /* turned up: its back can be read */
	double curl_hover_at;  /* when the pointer came to rest on the fold, or 0 */
	int curl_hot;
	int curl_act;
	double curl_act_at;
	bool curl_done;
	uint64_t curl_key;
	/* Being made bigger or smaller by an edge. The paper goes where the
	 * pointer takes it at once, and the program catches up when it can:
	 * the paper's inner rectangle on the screen, the edges being moved,
	 * how hard they are being pulled (the turn of the paper at the edge),
	 * and what is drawn over the sheet while it lasts. */
	bool sizing;
	int sz_x0, sz_y0, sz_x1, sz_y1;
	unsigned sz_edges;
	double sz_lip, sz_lip_to, sz_at, sz_released, sz_tick_at;
	struct wlr_scene_buffer *sz_over;
	uint64_t sz_key;
	unsigned sz_seed;
	/* held where it was while the program redraws, instead of against an
	 * edge: after the sheet has been opened out or folded back */
	bool sz_hold;
	int sz_hx, sz_hy;
	/* opened out, from sz_was (outer, on the screen): the creases are the
	 * ones it was opened out along, not a pulled-out sheet's */
	bool sz_opened;
	struct wlr_box sz_was;
	bool sz_dirty;   /* moved since the last frame: redrawn once, at the frame */
	/* Laid on a half or taken off it (view_lay_half): the edges pulled
	 * there by themselves, from the inner rectangle sz_from to sz_to, the
	 * program asked for the new size once, at the start. */
	bool sz_glide;
	double sz_glide_at;
	struct wlr_box sz_from, sz_to;
	/* Opening out to the whole desk, or folding back: the panels folded
	 * under the edges swinging out -- sides, then top and bottom -- or back
	 * under the other way round. */
	bool ua_on;
	/* lifted up the desk, out from under the keyboard on the screen */
	double room_lift, room_to, room_at;
	/* on the fingers of a hand: how far along, and then let go of, how fast and which way */
	bool ua_held, ua_free, ua_to_end;
	double ua_p, ua_v, ua_at;
	int ua_dir;                      /* +1 opening out, -1 folding back */
	int ua_phase;                    /* 0 the first pair of edges, 1 the second */
	double ua_start;                 /* when this phase began */
	struct wlr_box ua_from, ua_to;   /* outer rectangles on the screen */
	/* The sheet as it is opened out -- or as it was, folding back -- which
	 * everything on the screen during the move is drawn from: the part
	 * lying flat, the edges turning up, and the panels. */
	cairo_surface_t *ua_img;
	struct wlr_box ua_img_box;
	struct wlr_scene_buffer *ua_base;       /* the part lying flat */
	struct wlr_scene_buffer *ua_edge[4];    /* each edge, in the air */
	struct desk_unfold_edge ua_e[4];
	bool ua_e_on[4];
	/* X only. An override-redirect window -- a menu, a tooltip, a combo
	 * box's list -- is not ours to manage: it goes exactly where it says,
	 * with no frame, over every window, and never into the list. */
	bool unmanaged;
	/* Wayland: an input method's own window (ime_window_is) -- the same,
	 * but put where the text being written is, which it cannot say */
	bool ime_popup;
	int nibbles;     /* termites' marks on its edges, and its packet's */
	bool associated;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener map_request;
	struct wl_listener request_configure;
	struct wl_listener request_activate;
	struct wl_listener set_decorations;
	struct wl_listener set_geometry;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener request_minimize;
	struct wl_listener set_title;
	struct desk_sheet sheet;
	struct desk_shadow shadow;
};

/* A surface's word, by KDE's server-decoration protocol, on whose frame
 * it has: ours, its own, or none (see kde_server_frame). */
struct desk_kde_deco {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_server_decoration *deco;
	struct wl_listener mode;
	struct wl_listener destroy;
};

struct desk_popup {
	struct desk_server *server;
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener reposition;
	struct wl_listener destroy;
};

struct desk_keyboard {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

/* A mouse or a touchpad, kept so that a change in Shell Settings reaches
 * the ones already plugged in (input_apply). */
struct desk_pointer {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_input_device *device;
	struct wl_listener destroy;
};

/* A touch screen, kept so that the seat says there is one while there is. */
struct desk_touch_device {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_input_device *device;
	struct wlr_output *screen;    /* (only compared) */
	struct wl_listener destroy;
};

/* What a pen is used on: a screen, or a tablet of its own beside the machine. */
struct desk_tablet {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_tablet *wlr_tablet;
	struct wlr_tablet_v2_tablet *v2;
	struct wlr_output *screen;    /* (only compared) */
	struct wl_listener destroy;
};

/* A pen, or the other end of one: known from when it first comes near. */
struct desk_pen {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_tablet_tool *tool;
	struct wlr_tablet_v2_tablet_tool *v2;
	struct desk_tablet *tablet;   /* the one it is near */
	bool near;
	bool on_program;              /* its tip is down, and the program has it */
	uint32_t held_button;         /* what its side button came to mean, down */
	double sx, sy;                /* where it is over the program */
	double off_x, off_y;          /* from the screen to the surface its tip went down on */
	struct wl_listener set_cursor, destroy;
};

/* How long after a pen has gone a touch is still the hand that held it, in seconds */
#define PEN_PALM_AFTER 0.25

/* Whose a finger is. */
enum finger_goes {
	FINGER_PROGRAM,   /* on a program's window: told to it, as a finger */
	FINGER_HAND,      /* on the desk: it is the hand (hand_work) */
	FINGER_ASIDE,     /* on the desk too, with the hand already busy */
	FINGER_WHOLE,     /* one of a whole hand */
	FINGER_THING,     /* on a thing that has its fingers to itself */
	FINGER_NOWHERE,   /* it woke the screen */
};

struct desk_finger {
	bool on;
	int32_t id;
	enum finger_goes goes;
	double x, y;
	double ox, oy;                /* FINGER_PROGRAM: where its surface was, put down */
	struct desk_toplevel *view;   /* (only compared: it may have gone) */
	const struct desk_thing *thing;
};

/* A sheet being screwed up in the hand, its window hidden until it is known whether the
 * program lets it go. */
struct desk_hand_crumple {
	struct desk_toplevel *view;
	struct desk_crumple *c;
	struct wlr_scene_buffer *node;
	double x0, y0;                /* its top left, as it lay */
	int sw, sh;
	double dx, dy;                /* where the hand has taken it */
	double u, v, at;
	double closed_at;             /* it has been let go of, and the program told */
	double vx;
	bool back;                    /* the program kept it: it opens out again */
	double drawn_at, cost;
};

/* What a whole hand is doing. */
enum whole_work {
	WHOLE_NOTHING,    /* nothing that it could */
	WHOLE_FOLD,       /* a sheet folding, as far as the fingers have come */
	WHOLE_OPEN,       /* opening out, or folding back to what it was */
	WHOLE_CRUMPLE,    /* being screwed up, in the hand */
	WHOLE_CLOSE,      /* a thing that is not crumpled: closed, if the hand closes */
	WHOLE_CARRY,      /* carried, by the whole hand */
	WHOLE_PICK,       /* all of them taken up, to choose from */
};

/* What the hand is doing with the finger that is it. */
enum hand_work {
	HAND_WAITING,     /* put down: a tap, held, or going somewhere? */
	HAND_HOLDS,       /* it pressed on something, and has hold of it */
	HAND_SWIPES,      /* going somewhere over nothing: where it goes says what happens */
	HAND_DRAWER,      /* the drawer is on the finger */
	HAND_LOOK,        /* and so is the look round to one of the views (look_held) */
	HAND_SHEETS,      /* and so are the sheets of what is open */
	HAND_CURL,        /* and so is a sheet's corner */
	HAND_CARD,        /* and so is a slip's card, pulled out from under the machine */
	HAND_LID,         /* and so is the Control Panel's lid, lifted by its edge */
	HAND_DONE,        /* nothing more until it is lifted */
};

/* A note in the air between the binder and the calendar (agd_fly_*):
 * from pose a to pose b, each a place on the binder or on the calendar
 * close up (AGD_ON_*), so that it leaves from and lands on them where they
 * are as they move. */
struct agd_flight {
	int mode;                       /* AGD_FLY_* */
	struct desk_stick s;
	struct desk_note_pose a, b;
	int a_on, b_on;
	double start, dur;
	int snd;                        /* its landing, asked for (snd_end) */
	struct desk_sprite sp;
};
#define AGD_FLIGHTS 4
enum { AGD_FLY_NONE, AGD_FLY_TO_CAL, AGD_FLY_TO_BINDER, AGD_FLY_OFF };
enum { AGD_ON_BINDER, AGD_ON_CAL };

/* Keep destruction handlers for client-owned pointer constraints. */
struct desk_game_constraint {
	struct desk_server *server;
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
};

/* One termite out on the desk (Wood Maintenance): where it is, where it
 * is going and what it is doing there, and the mark it is making -- in the
 * wood (a scar), or on the edge of a sheet or a packet (its own node, on
 * that sheet). */
#define TERMITES_MAX 18
struct termite {
	double x, y, tx, ty, heading, pace, legs, at, bite;
	uint32_t rng, seed;
	int stage, bursts;
	int edge;                       /* 0 top, 1 right, 2 bottom, 3 left of a sheet */
	struct desk_toplevel *view;     /* the sheet it is at; NULL, the wood */
	bool packet;                    /* the view's packet, folded, not the view */
	int scar;                       /* the wood: its mark there, or -1 */
	struct wlr_scene_buffer *mark;  /* a sheet: its mark there */
	struct wlr_scene_buffer *node;  /* the termite */
};

/* The most screens Print takes a picture of at once. */
#define SNIP_SCREENS 8

/* A mark left in the wood: where on the desk, how deep, its crumbs. */
#define WOOD_SCARS_MAX 2048
struct wood_scar {
	double x, y, bite;
	uint32_t seed;
};

/* The desk made of another wood (relight_tick): the lamp going down, the
 * dark it is traced in, the eyes opening on it. */
enum { RELIGHT_NONE, RELIGHT_DOWN, RELIGHT_DARK, RELIGHT_OPEN };

struct desk_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	/* under everything, and moved by neither the drawer nor the camera:
	 * the bare desk on each screen beside the desk's own (see "more than
	 * one screen") */
	struct wlr_scene_tree *layer_side;
	struct wlr_scene_tree *layer_bg;
	struct wlr_scene_tree *layer_desk;
	struct wlr_scene_tree *layer_win;
	/* a window with the whole screen, and over that the menus X programs
	 * pop up -- which must clear every window, a fullscreen one included */
	struct wlr_scene_tree *layer_full;
	struct wlr_scene_tree *layer_xover;
	/* the drawer, pulled out: in front of the windows, which it comes out
	 * from under -- a sheet hanging over the edge of the desk does not
	 * cover it */
	struct wlr_scene_tree *layer_drawer;
	struct shell_conf conf;
	/* what is sounding: the drawer's slide, the sheet of the binder and the
	 * letter that last went over, the knob's last detent, and the newest
	 * letter heard arriving */
	int snd_drawer;
	int snd_page, snd_letter, snd_knob;
	/* the sounds for where a motion ends, asked for ahead of it (snd_end):
	 * the drawer at its stop, the binder down, the key turned, the glass
	 * down, the console landed */
	int snd_drawer_stop, snd_binder_down, snd_key_end, snd_guard_end, snd_cc_land;
	double snd_page_at;
	/* a window slid under the hand (slide_*): its two sounds going round,
	 * slowly and quickly, what they are of (a desk_sound, the slow one),
	 * how fast it goes (points a second, smoothed) and where the hand was
	 * last; the timer that lets it die away once the hand stops */
	int snd_slide[2], slide_of;
	double slide_v, slide_x, slide_y, slide_at;
	struct wl_event_source *slide_timer;
	/* the drawer on a finger, or let go of by one (drawer_snd_*): the same, its runs out or in,
	 * how far out it was last (pixels) and which way it was going */
	int snd_drun[2], drun_of;
	double drun_v, drun_lift, drun_at;
	bool drun_out;
	struct wl_event_source *drun_timer;
	unsigned post_seen;
	bool post_seen_set;
	/* sheets being folded away or opened out, over the windows; over the
	 * drawer too, for one on its way into it or out of it (lying on the
	 * desk they are clear of it: the desk slides up out of its way) -- and
	 * under the bookcase, the console and the post box, which you are not
	 * looking at the desk while you look at */
	struct wlr_scene_tree *layer_parked;
	/* and once folded, lying on the desk: under every window, as paper
	 * put down is, the last put down on top */
	struct wlr_scene_tree *layer_folded;
	/* how far what lies on the desk -- the windows, the folded sheets --
	 * has gone up with it, out of the drawer's way */
	int win_dy;
	/* sheets on their way to the bin: screwed up where they lay, and
	 * thrown off the side of the desk */
	struct wl_list throws;
	bool quitting;
	struct wlr_scene_tree *layer_shelf;
	struct wlr_scene_tree *layer_top;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;

	/* X11 programs: an X server of their own, started the first time one
	 * connects, whose windows come to us as surfaces like any other */
	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_ready;
	struct wl_listener new_xwayland_surface;
	struct wl_listener xwayland_start;
	struct wl_listener xwayland_client_gone;
	struct wl_event_source *xwayland_retry;
	int xwayland_rebuilds;
	double xwayland_rebuilt_at;
	uint32_t xatom[16];   /* the window types and states we look at, once X is up */

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener pointer_focus_change;
	/* Wayland games use relative motion and pointer constraints to look around. */
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wlr_pointer_constraint_v1 *active_constraint;
	/* where a pointer that says where it is (a tablet, a virtual machine's)
	 * last was, to make its movement from while a game holds it still */
	double abs_x, abs_y;
	bool abs_seen;
	struct wl_listener new_pointer_constraint;
	/* A program that asks for every key while it has them -- a remote
	 * desktop, a virtual machine, a game streamed from another machine
	 * (keyboard-shortcuts-inhibit): Alt+Tab, Super, Print and the rest are
	 * its, not the desk's (keys_asked). Super+Esc gives them back to the
	 * desk until the keys have gone elsewhere and come back; keys_back_key
	 * is that Esc, whose letting go is the desk's too. */
	struct wlr_keyboard_shortcuts_inhibit_manager_v1 *keys_inhibit;
	struct wl_listener new_keys_inhibitor;
	uint32_t keys_back_key;
	/* Typing into programs' windows through the input method (text-input,
	 * ti_*): the windows' text inputs, the one the input method is typing
	 * into, and for each key whether it is waiting on the input method's
	 * answer, gone through to the window, or kept by the input method. */
	struct wlr_text_input_manager_v3 *ti_mgr;
	struct wl_listener new_text_input, kb_focus_change;
	struct wl_list text_inputs;
	struct desk_ti *ti_ime;
	/* where the cursor of the text being written in a program is, on the
	 * screen: what the input method was told (ti_cursor); 0 high, none */
	struct wlr_box ime_at;
	/* the input method's own windows, put there (ime_window_place) */
	struct desk_toplevel *ime_win[4];
	uint8_t ti_keys[768];
	/* and what goes to the window, in the order it happened: keys, and
	 * the modifiers between them -- held up behind a key the input method
	 * has not yet answered for, so Ctrl is still down when the key it was
	 * held for gets there */
	struct ti_ev {
		uint8_t mods_only, pressed, waiting, taken;
		uint32_t code;
		struct wlr_keyboard_modifiers mods;
	} ti_q[64];
	int ti_q_head, ti_q_n;
	struct wl_event_source *ti_timer;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;
	/* dragging something from one program to another: the thing being
	 * dragged, drawn under the pointer over everything else */
	struct wl_listener request_start_drag;
	struct wl_listener start_drag;
	struct wl_listener drag_icon_gone;
	struct wlr_scene_tree *layer_drag;
	/* a button held down in a program: the pointer stays the program's
	 * until it is let go (pointer_hold) -- the surface it went down on,
	 * and the buttons the hand has down, whoever was told of them */
	struct wlr_scene_buffer *held;
	struct wl_listener held_gone;
	unsigned buttons_down;
	/* Alt+Tab held (switcher_step): the windows to go round as they were
	 * when it was pressed and the one it is on (sw_on); once Alt has been
	 * held a moment (sw_timer), the same spread out over the desk
	 * (sw_shown: until they are all back where they lie) -- each a
	 * picture of its sheet, the dark over the desk, and the name of the
	 * one it is on on a slip under it. sw_back: being put back, since
	 * sw_at, and sw_go the one to go to once they are. */
	bool sw_on, sw_shown, sw_back;
	int sw_n, sw_sel;
	struct desk_toplevel *sw_view[SWITCHER_MAX];
	struct sw_paper sw_paper[SWITCHER_MAX];
	struct wlr_scene_tree *layer_switcher;
	struct wlr_scene_rect *sw_veil;
	struct wl_event_source *sw_timer;
	double sw_at, sw_ticked;
	double sw_k, sw_k0;   /* how far out (1: spread), and from where it went back */
	struct desk_toplevel *sw_go;
	uint32_t sw_eat;   /* the button that chose one: its letting go goes nowhere */
	/* Print: an area of the screen being chosen for a picture, as through
	 * a camera's viewfinder (snip_*): each screen's picture a little in
	 * from its edges, the eyecup's dark round it and the viewfinder's
	 * display in the dark under it; the light on what is lit, the shade
	 * round it, the glass round it, the focusing circle in the middle of
	 * it; the two corners the hand has given it, of the screen's picture
	 * (snip_to_picture), on the screen it was begun on (snip_drag); then
	 * the shutter -- and the flash (snip_light_*), as the mirror goes up
	 * (snip_shut, till snip_timer) -- and what it took */
	bool snip_on, snip_held, snip_shot, snip_dirty;
	int snip_x0, snip_y0, snip_x1, snip_y1;
	int snip_drag;
	struct wlr_scene_tree *layer_snip;
	struct wlr_scene_rect *snip_dark[4];   /* the shade round it, at its deepest */
	struct wlr_scene_buffer *snip_pen[8];  /* and where the light falls off into it */
	double snip_shade;                     /* how deep it is (0..1) */
	struct wlr_scene_buffer *snip_glass[4];  /* the glass round it: what is under, bent */
	struct wlr_scene_buffer *snip_bevel[8];  /* its edge, catching the light; its corners */
	struct wlr_scene_buffer *snip_glint[4];  /* the lamp in it */
	double snip_glint_a[4];                  /* and how bright, each */
	struct wlr_scene_rect *snip_shut;
	struct wlr_scene_buffer *snip_ring, *snip_info;
	struct wlr_scene_tree *snip_backs;     /* the dark of the viewfinders, over the stills */
	struct wlr_scene_tree *snip_finders;   /* the screens as their viewfinders show them */
	struct wlr_scene_tree *snip_rims;      /* the viewfinders' glass edges, over those */
	struct wlr_scene_rect *snip_hole;      /* the dark in the eyepiece, the camera coming up */
	struct desk_finder_line snip_line;     /* the display, as drawn: for what, how wide */
	int snip_line_w, snip_info_bh;
	double snip_line_pt, snip_ring_pt;
	double snip_at, snip_k;   /* when the camera came up (the desk's time); how much of
	                           * the viewfinder is seen */
	double snip_dim;          /* how dark round the camera coming up, as it nears the eye */
	double snip_ticked;
	/* the camera itself (desk_draw_finder_camera): as it is set, and as it
	 * comes up to the eye -- its eyepiece's glass clear, the view through
	 * it opening out over the screen -- and goes down again */
	struct desk_finder_camera snip_cam, snip_hero_drawn;
	struct wlr_scene_buffer *snip_hero;
	double snip_hero_s;          /* pixels to the camera's unit */
	/* and where it is: its eyepiece's middle on the screen, pixels to its
	 * unit there, and how near the eye that is (1: at it) */
	double snip_eye_x, snip_eye_y, snip_eye_s, snip_eye_r;
	bool snip_eye_on;
	struct wlr_box snip_screen;  /* the screen it is held up to */
	double snip_down_at;         /* going down since (the desk's time; 0: not) */
	struct wlr_box snip_grim;    /* for grim to take once it is down (not all in the stills) */
	/* what it is set to take: the dial -- an area drawn out (0), the window
	 * (1) or the screen (2) under the hand -- and the self-timer (0 off, 1
	 * two seconds, 2 ten); what is lit, to be taken (snip_area): the area
	 * being drawn out, or that window or screen (snip_target) */
	int snip_mode, snip_timer_set;
	bool snip_lit;
	struct wlr_box snip_target;
	/* the camera in the hand, at the foot of the screen, to be worked: as
	 * drawn, how far up (0 out of sight .. 1), whether it is to keep out
	 * of the way of what is being drawn out; its parts on their way --
	 * from where, since when (the desk's time) */
	struct wlr_scene_buffer *snip_hand;
	double snip_hand_s, snip_hand_k;
	struct desk_finder_camera snip_hand_drawn;
	bool snip_hand_away;
	const char *snip_cursor;   /* the pointer's look, as last set */
	double snip_dial_from, snip_dial_at, snip_slide_from, snip_slide_at;
	double snip_power_at, snip_press_at;
	/* the self-timer counting down, the camera down meanwhile and the
	 * screens the hand's (snip_count_*): when it goes off (real time),
	 * its lamp, and the picture taken then of the screens as they are
	 * (snip_late), the flash over it after (snip_flash) */
	bool snip_counting, snip_count_lit, snip_late, snip_flash;
	double snip_count_end;
	struct wl_event_source *snip_count_timer;
	struct wlr_scene_tree *layer_count;
	struct wlr_scene_buffer *snip_count_node;
	struct wlr_box snip_take;
	struct wl_event_source *snip_timer;
	/* the screens as they were when Print was pressed: asked of each
	 * screen's next frame, as a screen recorder's are (snip_freeze), shown
	 * in their place while the camera is up, and what is taken is cut
	 * from them (snip_cut) -- written out on a thread of its own and
	 * handed to desk-screenshot (snip_save) */
	struct snip_still {
		struct wlr_output *output;
		struct wlr_box box;          /* the screen, and what its picture is of */
		struct desk_buf *buf;
		struct wlr_scene_buffer *node;     /* the picture, where the screen is */
		struct wlr_scene_rect *dark;       /* the viewfinder's dark over that */
		struct wlr_scene_buffer *finder;   /* and the picture in it */
		struct wlr_box view;         /* where that is, the camera at the eye */
		struct wlr_scene_buffer *rim[4];   /* its glass edge */
		struct wlr_box rim_part[4];  /* each, of the picture's pixels */
	} snip_still[SNIP_SCREENS];
	int snip_still_n, snip_still_wanted;
	bool snip_freezing;
	double snip_freeze_at;
	struct wlr_scene_tree *snip_stills;
	struct wl_event_source *snip_freeze_timer;
	int snip_save_fd[2];
	struct wl_event_source *snip_save_src;
	unsigned snip_saves;
	/* the viewfinder's glass edge, worked out on a thread of its own and
	 * said back the same way (snip_rim_*), for the time the camera is up
	 * (snip_gen) */
	int snip_rim_fd[2];
	struct wl_event_source *snip_rim_src;
	unsigned snip_gen;
	/* a tape in the camera (snip_tape): what the shutter takes is a film,
	 * not a picture -- kept, as the dial is, from one Print to the next;
	 * going in or coming out since (the desk's time; 0: not), from where;
	 * Shift+Print's to be put in once the camera is up (snip_load) */
	bool snip_tape, snip_load;
	bool snip_from_drawer;   /* the camera taken out of the drawer: up once it is in */
	double snip_tape_at, snip_tape_from;
	/* The film (rec_*): the shutter pressed with a tape in, desk-record
	 * is started (rec.c), and from rec_due -- and once Print is all gone
	 * -- each frame of the screen it was held up to, the part of it being
	 * recorded, is read as it goes out (rec_output_commit): no more often
	 * than REC_FPS, and once more when one was let go of too soon after
	 * the last (rec_owed). The clock says it is running, and for how long
	 * (rec_shown, the seconds it was last told). */
	struct desk_rec *rec;
	struct wlr_output *rec_output;
	struct wlr_box rec_box;        /* what is recorded, on the screens */
	struct wlr_box rec_src;        /* and in the screen's frames: their pixels */
	double rec_due;                /* recording from (real time) */
	double rec_read_at;            /* the last frame read (real time) */
	bool rec_owed;
	int rec_shown;
	struct wl_event_source *rec_timer;
	/* the flash, a picture taken (snip_light_*): what was taken lit white,
	 * the rest of the screen a little, dying away from snip_light_at (the
	 * desk's time; 0: none) */
	struct wlr_scene_rect *snip_light, *snip_glow;
	double snip_light_at;
	/* Wood Maintenance (termites_*): the termites, over the paper; their
	 * marks in the wood, on the desk under it (moved with the paper) */
	struct wlr_scene_tree *layer_bugs, *layer_scars;
	struct wlr_scene_buffer *scars_node;
	struct wl_event_source *tm_timer;
	struct termite tm[TERMITES_MAX];
	int tm_n, tm_level_seen;
	bool tm_on, tm_preview;
	double tm_preview_at, tm_flee_until, tm_last, tm_session;
	uint32_t tm_seed;
	struct wood_scar scars[WOOD_SCARS_MAX];
	int scars_n, scars_next, scars_w, scars_h;
	bool scars_loaded, scars_dirty, scars_unsaved;
	/* how far the desk and everything on it is moved with the camera, at
	 * the desk end of a move */
	int desk_dx, desk_dy;
	/* The clock on the back of the desk: which page is up, how lit its
	 * keys are as a hand comes near, the key held down, the MEDIA page's
	 * button under the hand and held, the timer for what the face does of
	 * itself (a title running along it), and a record of what the machine
	 * has drawn, a second at a time for the last hour, for the PC page's
	 * averages; and, while the PC page is up, the machine's meters, read
	 * with each look at the supply, and how many reads there have been
	 * since it came up (the first has no rate to give). */
	struct {
		int mode;
		double wake, wake_to;
		double glow[DESK_CLOCK_KEYS];
		int hot, pressed;
		int media_hot, media_down;
		bool near;
		double anim_at;
		bool animating;
		bool vol_drag;
		double vol_scroll;
		struct wl_event_source *timer;
		unsigned supply_seen;   /* the looks at it taken in (desk_supply_latest) */
		struct desk_supply supply;
		float hist[3600];
		int hist_n, hist_at;
		double sum_all;
		long n_all;
		struct desk_meters pc;
		int pc_reads;
	} clk;
	/* The tray's slips under the Control Panel -- how far each one has
	 * slid out to the pointer, kept by tray item so that it survives the
	 * list changing, and the one under it -- and the card of the one
	 * pulled out: whose, how far out, the submenus it has been taken down
	 * through, the entry under the pointer, and how far it is scrolled. */
	struct {
		unsigned lift_id[DESK_SLIPS_MAX];
		double lift[DESK_SLIPS_MAX];
		int n_lift;
		unsigned hot;
		double anim_at;
		bool animating;
		unsigned card;
		double pull, pull_from, pull_to, pull_start;
		bool pulling;
		int32_t path[6];
		char path_label[6][128];
		int depth;
		int hot_entry, top;
		struct wlr_scene_buffer *node;
		uint64_t key;
		/* more slips than room: how far the row is slid along, where it
		 * is going, and the wheel's part-notches on the way */
		double scroll, scroll_to;
		int wheel;
		/* the card's menu: the wheel's part-notches, so a trackpad glides */
		double card_scroll;
	} slp;
	struct wlr_scene_tree *drag_icon;
	struct wl_list keyboards;
	struct wl_list pointers;            /* struct desk_pointer */
	char kb_applied[160];               /* the layouts the keyboards have now */
	enum desk_cursor_mode cursor_mode;
	struct desk_toplevel *grabbed;
	double grab_x, grab_y;
	double move_from_x, move_from_y;    /* where a sheet was taken hold of: to move,
	                                     * or by its bottom edge (DESK_CUR_PEEK) */
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
	struct wl_listener new_decoration;
	/* GTK 3's word on whose frame a window has, by KDE's older protocol:
	 * one for each surface it has given it for */
	struct wlr_server_decoration_manager *kde_deco;
	struct wl_list kde_decos;
	struct wl_listener new_kde_deco;

	struct wlr_scene_buffer *wallpaper;
	struct wlr_scene_buffer *drawer_sheet;
	/* what lies over the desk: the sprites under the drawer's shade, the
	 * shade, and the sprites it does not reach */
	struct wlr_scene_tree *ovl_under, *ovl_over;
	/* over the desk's own pictures, under the paper: what is moved under
	 * the clear mat while it is lifted (thing-mat.c, desk_on_desk) */
	struct wlr_scene_tree *layer_mat;
	struct wlr_scene_rect *shade, *shade_edge;
	struct desk_sprite ovl[DESK_OVL_COUNT];
	int ovl_ox, ovl_oy;   /* where the desk was when they were drawn */
	const void *ovl_room; /* and which room it was */
	bool ovl_placed;
	/* What is in the drawer, in the order it lies there: programs and the
	 * screwdriver (Shell Settings), which are kept (drawer_save), and sheets
	 * folded away into it. */
	struct drawer_entry drw[DRAWER_MAX];
	int drw_n;
	bool drw_default;          /* nobody has filled it yet: what it comes with */
	bool drw_migrate;          /* kept by a DEsk that had no standard programs */
	unsigned drw_gen;          /* the list of programs it was last matched to */
	bool drw_dirty;            /* what lies in it has to be drawn again */
	struct wlr_scene_buffer *drw_node;   /* what lies in it, over the sheet */
	double drw_box[4];         /* what of the sheet that covers, in its pixels */
	double drw_res;            /* the buffer's pixels to one of the sheet's */
	int drw_hover;             /* the one under the hand, stood up; -1 */
	bool drw_sliding;          /* moving along the row to where they go now */
	double drw_slid_at;
	/* A hand down on one of them, or on a sheet folded on the desk, not yet
	 * moved far enough to be carrying it; and then carrying it. */
	int drw_press;
	struct desk_sheet *drw_press_sheet;
	double drw_px, drw_py;
	/* the button it is held with: the left takes it and does what it is
	 * for, the right only ever moves a folded sheet about */
	uint32_t drw_button;
	bool drw_carrying;
	struct drawer_entry drw_carry;
	int drw_carry_from;        /* its place in the row, or -1: off the desk */
	int drw_gap;               /* the place held open for it, or -1 */
	bool drw_over;             /* over the drawer, not the desk */
	bool drw_sprung;           /* the drawer has come out for it */
	double drw_gx, drw_gy;     /* where on it the hand has hold of it */
	struct wlr_scene_buffer *drw_carry_node;
	double drw_carry_ax, drw_carry_ay;
	double drw_poof_at;        /* a program taken out of it, fading */
	struct drawer_look drw_looks[DRAWER_LOOKS_MOST];
	int drw_looks_n;
	/* The card of looks, up over the one in it that it is for: what is on it, which it looks like
	 * now, the one under the hand; where it is, coming up or going */
	bool lk_open;
	int lk_entry;
	uint32_t lk_button;        /* a hand put down while it is up, and on which of them */
	int lk_press;
	int lk_cols;
	bool lk_more;              /* and one more on it, to choose a picture from anywhere with */
	/* desk-look choosing a picture for a program in the drawer, which it says it made as it goes */
	struct wl_event_source *lk_pick_src;
	int lk_pick_fd;
	char lk_pick_key[128];
	char lk_pick_said[256];
	size_t lk_pick_len;
	int lk_n, lk_chosen, lk_hover;
	char lk_ids[DRAWER_LOOKS_ON_CARD][96];
	char lk_names[DRAWER_LOOKS_ON_CARD][96];
	struct desk_drawer_item lk_items[DRAWER_LOOKS_ON_CARD];
	struct wlr_scene_buffer *lk_node;
	int lk_x, lk_y, lk_w, lk_h;
	double lk_pt, lk_at;
	bool lk_going;
	int desk_w, desk_h;
	/* Looking up at the bookcase. shelf_t runs 0 at the desk to 1 at the
	 * shelf; while it is strictly between, the frame is one of the traced
	 * keyframes of the move. */
	double shelf_t;
	double shelf_from, shelf_to, shelf_start, shelf_dur;
	int shelf_moving;
	int shelf_hover;
	/* the binder that has been taken down, and where in it we are */
	int shelf_binder;
	/* where the block has been turned to, and where it is heading: a
	 * fractional place so a notch of the wheel moves the paper rather than
	 * jumping it */
	double binder_scroll;     /* unspent scroll, so a trackpad glides */
	double binder_axis_at;    /* when the last scroll event arrived */
	int binder_burst;         /* sheets already spent on this one gesture */
	/* where the block of sheets is turned to, on a spring: the notches
	 * move where it is going, and it follows with the way it was going */
	double binder_pos, binder_pos_to, binder_pos_vel;
	double binder_pos_at;
	int binder_pos_moving;
	int binder_sel;
	/* The book of every program, when it is the one taken down
	 * (shelf_binder == DESK_SHELF_BOOK; binder_pos is then the spread it
	 * is open at): what is written on its line and where the caret is, the
	 * input method's part and its candidates, the keys handed to it, the
	 * line chosen with the keys and the thumb tab under the pointer. */
	char book_q[256];
	int book_caret;
	char book_pre[256];
	int book_pre_caret;
	char book_cand[16][96];
	const char *book_cand_p[16];
	int book_ncand, book_cand_cursor, book_cand_page;
	bool book_ime;
	struct {
		char utf8[8];
	} book_keyq[32];
	int book_keyq_n;
	int book_sel, book_tab;
	bool book_flag;   /* the hand on binder_sel's flag, in the margin */
	int book_cursor[4];
	/* A program taken out of the book or a binder: it is left open as you
	 * turn to the desk, and put back on the shelf once the shelf is out of
	 * sight. */
	bool binder_putback;
	/* the binder under the pointer, on its way out of the row */
	int hover_from;
	double hover_t, hover_start;
	int hover_moving;
	/* The desk calendar, taken off the shelf to be looked at close up:
	 * cal_t 0 on the shelf to 1 close up. The page showing; the one going
	 * over the wire and how many more were asked for while it went; the
	 * edge of the page the pointer is at (cal_zone: 1 the top half, 2 the
	 * bottom) and the edge lifted for it, by cal_lift_t. */
	double cal_t, cal_from, cal_to, cal_start, cal_dur;
	int cal_moving;
	/* the section being read and the day its page is about; a section
	 * being gone to by its tab, the pages between going over together;
	 * the tab the pointer is on */
	int cal_view, cal_to_view;
	double cal_switch_start, cal_switch_dur;
	int cal_hover_tab;
	int cal_year, cal_month, cal_day;
	int cal_turn, cal_more;
	double cal_turn_start, cal_turn_dur;
	int cal_zone, cal_lift;
	double cal_lift_t, cal_lift_at;
	double cal_scroll;
	struct wlr_scene_tree *cal_tree;
	/* the bookcase as it was when the calendar was taken, sharp and soft:
	 * stepped in towards, the one going over to the other */
	struct wlr_scene_buffer *cal_sharp, *cal_soft;
	struct desk_sprite cal_sp;
	double cal_nat_w, cal_nat_h;  /* the picture close up, before it is scaled */
	/* The memo pad, taken down to be written on, as the calendar is taken
	 * (memo_t, its pictures of the bookcase, its sprite). The sheet it is
	 * open at and what is written on it, kept by memo.c a moment after the
	 * last change (memo_keep) and whenever it is turned or put back; the
	 * caret; the input method's part and its candidates, the keys handed
	 * to it not yet answered, the key held down. A sheet going over the
	 * wire: memo_turn +1 on, -1 back, to memo_turn_to, what is written on
	 * that one in memo_other. */
	double memo_t, memo_from, memo_to, memo_start, memo_dur;
	int memo_moving;
	struct wlr_scene_tree *memo_tree;
	struct wlr_scene_buffer *memo_sharp, *memo_soft;
	struct desk_sprite memo_sp;
	double memo_nat_w, memo_nat_h;
	unsigned memo_gen_open;
	int memo_sheet;
	char memo_text[DESK_MEMO_TEXT];
	int memo_caret;
	bool memo_dirty;
	struct wl_event_source *memo_keep;
	char memo_pre[256];
	int memo_pre_caret;
	char memo_cand[16][96];
	const char *memo_cand_p[16];
	int memo_ncand, memo_cand_cursor;
	bool memo_typing, memo_ime;
	struct {
		char utf8[8];
	} memo_keyq[32];
	int memo_keyq_n;
	struct wl_event_source *memo_repeat;
	uint32_t memo_repeat_sym;
	uint32_t memo_held_sym;     /* the key last pressed on the memo, while it is down */
	int memo_cursor[4];
	int memo_turn, memo_turn_to;
	double memo_turn_start;
	char memo_other[DESK_MEMO_TEXT];
	double memo_scroll;
	/* The lamp coming up at login (boot_*): while it is on, the clock
	 * counts the room being made -- boot_p, as far as it has counted, and
	 * what is being made -- and the dark over everything (boot_night) is
	 * as the lamp is for that, boot_light; the clock's lit segments shine
	 * over it (boot_glow). When it began, the last frame, when the desk
	 * was up, how far the lamp has been brought up regardless (a machine
	 * slow to make the rest), when the count reached the end. */
	bool boot_on, boot_lamp;
	double boot_t0, boot_at, boot_p, boot_light, boot_floor;
	double boot_desk_at, boot_done_at, boot_warm_at;
	bool first_frame_logged, boot_lit;
	unsigned views_logged;   /* the views whose coming up is in the log, a bit each */
	bool soft_render;    /* the screen put together on the processor (pixman) */
	/* the screen drawn on the CPU at all -- pixman, or GL that is a
	 * rasterizer on the processor (render_check) -- and what draws it */
	bool on_cpu;
	char render_name[128];
	bool render_own;     /* by graphics of its own, not the processor's or a VM's (render_check) */
	/* the letter in the middle of the desk on a first start (first_letter) */
	struct desk_sprite first_letter;
	bool first_letter_up;
	double lamp_up_at;   /* when the lamp came up (boot_end): the door's films wait a minute */
	int orient_want;     /* the transform the accelerometer last said, or -1 (orient_said) */
	struct wl_event_source *orient_fake;
	double boot_real0;   /* when it began by the wall clock, for the log: in
	                      * 2D the desk's own runs a hundred times fast */
	int boot_stage, boot_shown;
	struct wlr_scene_buffer *boot_night;
	int boot_night_w, boot_night_h, boot_night_q;
	struct desk_sprite boot_glow;
	/* The desk made of another wood (relight_*): the lamp going down over
	 * the room as it is, the room traced again in the dark -- relight_p how
	 * far, as the clock counts it -- and the eyes opening on it (the lids
	 * over everything); since when, since when the room has been there, and
	 * when it was chosen (by the wall clock, for the log). What the room is
	 * traced in now. */
	/* The mat being laid or taken away (mat_roll_*): rolling the old one up, or
	 * unrolling the new; since when; how far the old one was out when it began;
	 * the one to come; and the one shell.conf wants. */
	int mat_roll;
	double mat_roll_t0, mat_roll_from;
	struct desk_mat mat_next, mat_want;
	int relight;
	double relight_t0, relight_p, relight_ready_at, relight_chosen;
	int relight_shown;
	bool relight_counting;   /* the tracer's count is this trace's (it has been seen low) */
	struct wlr_scene_buffer *lids;
	int wood_now, stain_now;
	/* What the desk was last painted from while its room is still being
	 * traced (wait_*): the room drawn -- its serial, 0 for the bare boards
	 * -- the size and the drawer. The tick asks after the room every 0.4
	 * s, and the desk is painted again only when one of them has changed:
	 * a screen-sized picture that has not changed, drawn and handed over
	 * again, was a slow machine's 65 ms in every 400 taken from the trace. */
	unsigned wait_room;
	int wait_w, wait_h;
	double wait_drawer;
	/* The binder of notes beside it (agd_*): agd_t 0 out of sight to 1 in;
	 * the day it is open at; the pad picked; the note being written -- its
	 * id, or DESK_AGENDA_NEW_PLAN/TODO -- its colour, what is written on
	 * it and where the caret is; the input method's part of it and its
	 * candidates on show; what the pointer is on; a new note on its way
	 * from its pad, a note being peeled off; the keys handed to the input
	 * method not yet answered, and the key being held down. */
	double agd_t, agd_from, agd_to, agd_start, agd_dur;
	int agd_moving;
	int agd_y, agd_m, agd_d;
	int agd_colour;
	int agd_edit, agd_edit_colour;
	char agd_text[256];
	int agd_caret;
	char agd_pre[256];
	int agd_pre_caret;
	char agd_cand[16][96];
	const char *agd_cand_p[16];
	int agd_ncand, agd_cand_cursor, agd_cand_page;
	int agd_hover, agd_hover_arg;
	bool agd_peeling;
	double agd_peel_start;
	int agd_gone_id;
	double agd_gone_start;
	bool agd_ime;
	struct {
		char utf8[8];
	} agd_keyq[32];
	int agd_keyq_n;
	struct wl_event_source *agd_repeat;
	uint32_t agd_repeat_sym;
	uint32_t agd_held_sym;      /* likewise for a note */
	struct desk_sprite agd_sp;
	struct desk_sprite agd_mark_sp;   /* the pencil mark on its ruler, on its own */
	/* the time of the plan being written (agd_when, -1 for none, and how
	 * long, agd_span), the ruler it is set on out by agd_ruler_t, the
	 * pointer on it (agd_mark) or held down on it from agd_drag_a; the
	 * note being written still on its way from the calendar; the calendar
	 * to turn to the binder's day once nothing is in the air on its way
	 * there; and the notes in the air */
	int agd_when, agd_span;
	double agd_ruler_t, agd_ruler_at;
	int agd_mark;
	bool agd_drag;
	int agd_drag_a, agd_drag_keep;
	bool agd_edit_away;
	bool agd_follow;
	double agd_scroll;
	struct agd_flight agd_fly[AGD_FLIGHTS];
	double binder_t;
	struct book_kept book_kept[BOOK_AHEAD];
	double binder_from, binder_to, binder_start, binder_dur;
	int binder_moving;
	/* what a sprite is drawn at for the while, against the overlay (sprite_picture): below 1
	 * for a thing in motion (motion_res); 0 is 1 */
	double sprite_shrink;
	/* Sheets taken out of binders and put down on the desk for programs
	 * whose windows have not come yet, and the one in the hand: where it
	 * hung, when you turn back to the desk with it, when it goes down. */
	struct desk_base bases[DESK_BASES];
	struct {
		int base;                 /* the one in the hand, or -1 */
		int idx;                  /* its place in the binder it came out of */
		double start, turn_at, land_at;
		bool turned;
		double cx0, cy0, w0, h0, a0;
		struct desk_sprite sp;
	} take;
	/* the shelf view, a layer a sprite: the case, the rectangle a binder
	 * slides out in, the three things on the lower board, the binder open
	 * on the desk, and a sheet in the air -- stacked in that order */
	struct wlr_scene_tree *sh_live;
	struct wlr_scene_tree *sh_layer[SH_LAYERS];
	struct desk_sprite sh_sprite[SH_LAYERS];
	int sh_w, sh_h;
	unsigned sh_room;   /* desk_view_room the case was drawn from */
	/* the traced tilt, hung on two nodes: a frame of the move is two source
	 * boxes and an opacity, with no cairo and no allocation. One film per
	 * place on the desk you can turn to look at. */
	struct wlr_scene_buffer *film_a[DESK_VIEW_COUNT], *film_b[DESK_VIEW_COUNT];
	int film_frames[DESK_VIEW_COUNT];
	int film_fw[DESK_VIEW_COUNT], film_fh[DESK_VIEW_COUNT];

	/* Turning to the control console at the back right. cc_t is the same
	 * kind of thing as shelf_t: 0 at the desk, 1 at the set. */
	double cc_t;
	double cc_from, cc_to, cc_start, cc_dur;
	int cc_moving;
	/* the filaments coming up, once you have got there */
	double cc_warm, cc_warm_start;
	struct desk_meters cc_meters;   /* as last read, at the tube's clock below */
	double cc_meters_clock;
	int cc_warming;
	int cc_hover;
	/* what the set has been left set to */
	int cc_wifi, cc_bt;
	double cc_vol, cc_bright;
	/* the screen's own light (a laptop's backlight): where its fader is,
	 * whether there is one, and the turn of it not yet sent, which goes a
	 * moment after the hand last moved it (cc_screen_send) */
	double cc_screen;
	bool cc_screen_ok;
	struct wl_event_source *cc_screen_send;
	/* The guarded set. The key turns itself once it is in, the lid swings
	 * up, and the button that was pushed starts the machine going dark --
	 * which is the only thing here that cannot be taken back. */
	double cc_key, cc_key_from, cc_key_to, cc_key_start, cc_key_dur;
	int cc_key_moving;
	double cc_guard, cc_guard_from, cc_guard_to, cc_guard_start, cc_guard_dur;
	int cc_guard_moving;
	int cc_pressed;          /* DESK_CC_PW_*, or -1 */
	/* (2026-11-10) A button that does a thing and comes back up -- SLEEP, SUSPEND, the four on
	 * the lid -- which, and since when; and what it does, a moment after it went down, so that
	 * the press is seen first (DESK_CC_SLEEP or DESK_CC_PW_SUSPEND, or -1). */
	int cc_push;
	double cc_push_at;
	int cc_then;
	double cc_then_at;
	/* what the four on the lid have on: power saving, as the power-profiles daemon has it (read
	 * as the set is turned to), and the night's warm screen, the desk's own */
	bool cc_save, cc_save_ok;
	bool cc_night;
	/* DISPLAY, and Super+P: how the screens are used (DESK_SCREENS_*), the screen that is the
	 * desk's when they are extended, and what the tube last said about it and when */
	int screens_mode;
	struct wlr_output *screens_home;
	int screens_said;
	double screens_said_at;
	/* the wireless aerial, going up and down on its motor with the lever */
	double cc_ant, cc_ant_from, cc_ant_to, cc_ant_start, cc_ant_dur;
	int cc_ant_moving, cc_ant_snd;
	double cc_dying, cc_die_start;
	/* a hand on the knob or the fader */
	int cc_grab;
	struct wl_event_source *cc_timer;
	struct wlr_scene_buffer *cc_base;   /* the traced sheet, put up once */
	int cc_base_w, cc_base_h;
	unsigned cc_base_room, cc_live_room;   /* and from which room (desk_console_room) */
	/* the instruments: a sprite a piece, each drawn only when it changes */
	struct wlr_scene_tree *cc_live;
	struct desk_sprite cc_sprite[DESK_CCP_COUNT];
	int cc_live_w, cc_live_h;
	struct wlr_scene_tree *layer_console;
	struct wlr_scene_buffer *cc_dim;

	/* Turning to the post box. The same kind of look as the console: 0 at
	 * the desk, 1 in front of the box with its door down. */
	double pb_t;
	double pb_from, pb_to, pb_start, pb_dur;
	int pb_moving;
	int pb_hover;
	/* where the letters in it have been turned to, on a spring, the way a
	 * binder's sheets are: a wheel notch is a letter */
	double pb_pos, pb_pos_to, pb_pos_vel, pb_pos_at;
	int pb_pos_moving;
	double pb_scroll;
	/* The turned-down corner of the letter the hand is at, as a sheet's
	 * corner is: which letter (and its id, should the box change under
	 * it), how far it is turned and how fast, whether it is turned up to
	 * be read, when the hand came to rest on its fold, the instruction
	 * under the hand, and the one being carried out. */
	int pbc_idx;
	unsigned pbc_id;
	double pbc_s, pbc_v, pbc_at, pbc_lift;
	bool pbc_near;
	double pbc_hover_at;
	int pbc_hot;
	int pbc_act;
	double pbc_act_at;
	/* The same corner on the binder sheet the hand is at: which sheet, how
	 * far it is turned and how fast, turned up or not, when the hand came
	 * to rest on its fold, the instruction under the hand and the one
	 * being carried out. */
	int bnc_idx;
	double bnc_s, bnc_v, bnc_at, bnc_lift;
	bool bnc_near;
	double bnc_hover_at;
	int bnc_hot;
	int bnc_act;
	double bnc_act_at;
	bool bnc_flag;   /* the hand on the place for its favourites' flag */
	struct wlr_scene_tree *layer_post;
	struct wlr_scene_buffer *pb_base;
	struct desk_sprite pb_sprite;   /* the letters, and one in the air */
	int pb_sprite_w, pb_sprite_h;
	/* how far the views have been warmed for this size, ahead of a look */
	int warm_step, warm_w, warm_h;
	int pb_base_w, pb_base_h;
	unsigned pb_base_room;
	/* a letter on its way out of the box and down onto the desk */
	cairo_surface_t *pb_fly;
	double pb_fly_x, pb_fly_y, pb_fly_w, pb_fly_h;
	double pb_fly_start, pb_fly_dur;
	int pb_fly_on;
	unsigned pb_fly_id;
	/* and where the ones already taken out are lying */
	struct desk_paper {
		struct wlr_scene_buffer *node;
		cairo_surface_t *sheet;
		unsigned id;
		int x, y, w, h;
		bool placed;         /* put somewhere by hand: kept there */
		double fx, fy;       /* where, as a part of the desk's width and height */
	} pb_paper[6];
	int pb_papers;
	/* A letter in the hand: which (+1; 0 none), where on it it was taken
	 * hold of, where the hand went down, and whether it has moved since --
	 * a letter clicked and not moved is put away, as before. */
	int pp_held;
	double pp_hx, pp_hy, pp_px, pp_py;
	bool pp_moved;
	/* Clicked from up at the bookcase: you have to come back down to the
	 * desk before you can turn to something on it, so the second move
	 * waits for the first to land. */
	int pending_look;        /* 0 none, 1 the console, 2 the post box */
	double drawer_t;
	double drawer_from;
	double drawer_to;
	double drawer_start;
	double drawer_dur;
	int drawer_moving;
	int cascade;
	bool headless;
	bool size_set;
	int headless_w, headless_h;
	char *startup_cmd;
	struct wl_listener request_activate;
	struct wlr_session *session;   /* the seat's, for changing VT; NULL nested */
	/* The screen lock (lock_*): the phase it is in, what is typed (wiped
	 * once it is handed to be checked) and how many letters that is, when
	 * it came down (walking up to it, fading in), when it went to be
	 * checked (and how far leant in to the key it was then), when a wrong
	 * one is turned away (refused: it has been, and the key has not yet
	 * been seen not to turn) and when the key turns to let you in; Caps
	 * Lock, the caret; going out (the unlocking backwards: lock_from, the
	 * view it starts from, whether the turn to the bookcase comes first,
	 * when the walk out began); who
	 * lives here and their picture; the room to go into and whether it was
	 * the desk you were at; the door's layer over everything, the black
	 * over every output under it, and the caret's timer. */
	int lock_phase;
	char lock_pass[512];
	int lock_len, lock_typed;
	double lock_at, lock_check_at, lock_lean0, lock_fail_at, lock_open_at, lock_leave_at;
	int lock_from;          /* LOCK_FROM_*: where you were, to come back to */
	double cc_lock_push;    /* the Control Panel's LOCK button, pushed in */
	bool lock_film;         /* turning to the bookcase before going out */
	bool lock_leaving;      /* going out of the room, backwards, and locking up */
	bool lock_fade_in, lock_walk, lock_refused, lock_caps, lock_caret_on, lock_from_desk;
	bool lock_picture_tried, lock_hand;
	double lock_films_at;   /* the door's moves last asked to be filmed ahead */
	int lock_films_asked;   /* and for what (lock_films), or -1 */
	char lock_name[128];
	cairo_surface_t *lock_picture, *lock_inside;
	struct wlr_scene_tree *layer_lock;
	struct wlr_scene_rect *lock_cover;
	struct wlr_scene_buffer *lock_node;
	struct wl_event_source *lock_timer;
	/* what moves out at the door, over it, drawn every frame while there
	 * is any (lock_fx), and when last */
	struct wlr_scene_buffer *lock_fx;
	double lock_fx_at;
	/* A day being kept (show_*): which (DESK_SHOW_*), since when, the
	 * next of its notes to ask for, the sounds asked for so far (to stop
	 * them), Esc having stopped it (its letting go is ours too); the day
	 * it was at the last look, for the stroke of the New Year; and over
	 * everything, the door included, the light of fireworks. */
	int show;
	double show_t0;
	int show_next;
#define SHOW_HANDLES 128
	int show_handles[SHOW_HANDLES];
	int show_nh;
	bool show_esc;
	int occ_ymd;
	struct wlr_scene_tree *layer_glow;
	struct wlr_scene_buffer *fw_node, *confetti_node;
	char *dump_path;
	char *screenshot_path;
	int screenshot_delay_ms;
	int exit_after_shot;
	struct wl_event_source *tick;
	/* the once-a-second round's longest chore, and when a long round was
	 * last put in the log (tick_cb) */
	const char *tick_worst;
	double tick_worst_ms, tick_said_at;
	struct wl_event_source *shot_timer;
	bool icons_shown;
	/* A resize arrives as a stream of events while an edge is dragged, and
	 * every distinct size is a room the ray tracer would have to bake. So
	 * the last one wins: nothing is rebuilt until the size stops moving. */
	struct wl_event_source *resize_timer;
	/* nested on an X desktop: the display it is on, and the window of ours
	 * there still to be named for its dock (see nested_name) */
	char *parent_display;
	/* DEsk's own window there, when it runs in one; a connection to find
	 * where on that screen it is */
	uint32_t host_window;
	void *host_xcb;
	int agd_cursor[4];      /* where the input method was last told it is */
	struct wl_event_source *name_timer;
	struct wlr_output *name_output;
	char name_tag[64];
	int name_tries;
	int resize_w, resize_h;
	/* The screen going dark when nobody has touched anything for a while
	 * (input_seen): when anybody last did, by the clock on the wall; the
	 * timer that looks; whether any screen has gone dark; when it was woken
	 * again, and the key or button that woke it, which is the waking's,
	 * down and up, and nobody else's. Programs that need the screen lit
	 * hold it up (idle_holds: idle-inhibit), programs that want to know how
	 * long it has been are told (idle_notifier: ext-idle-notify, for
	 * swayidle), and wlopm turns screens off and on through the same
	 * machinery (wlr-output-power-management). Over all of it, nested, the
	 * black. */
	double idle_at, wake_at;
	struct wl_event_source *idle_timer;
	bool dark, idle_held;
	/* dark with no screen left: the last went while dark (a monitor
	 * asleep that says it has been unplugged), and the next to come is
	 * to wait dark too, until somebody is back */
	bool dark_away;
	uint32_t wake_key, wake_button;
	struct wlr_idle_notifier_v1 *idle_notifier;
	struct wl_list idle_holds;
	struct wl_listener new_idle_hold;
	struct wl_listener power_set_mode;
	/* Fingers (the section on them). The pointer is not seen from the first
	 * finger until a mouse is used again; the hand is one finger at a time. */
	struct wl_listener touch_down, touch_motion, touch_up, touch_cancel, touch_frame;
	struct wl_list touch_devices;
	/* The pens (the section on them): told to a program that knows of pens, and to anything
	 * else as a mouse. A hand resting on the screen is not a finger while one is near. */
	struct wlr_tablet_manager_v2 *tablet_mgr;
	struct wl_list tablets, pens;
	struct wl_listener pen_axis, pen_proximity, pen_tip, pen_button;
	struct desk_pen *pen_now;     /* the pen this movement or this press is */
	bool pen_took;                /* and a program took it */
	bool pen_lately;              /* it has been the last thing used, a mouse not since */
	double pen_pressure;
	int pens_near;
	double pen_left_at;
	struct desk_touch touch;
	struct desk_finger fingers[DESK_FINGERS];
	struct wl_event_source *touch_timer;
	bool hand_unseen;
	bool touch_woke;
	uint32_t over_all_button;
	struct wlr_output *finger_screen;   /* where a finger last was (only compared, and looked for) */
	/* The keyboard on the screen: its layer, and the line what is typed in keeps above */
	struct wlr_scene_tree *layer_keys;
	int room_top;
	bool text_wanted;
	bool tablet_mode;
	struct wl_list switches;
	bool hand_on;
	int32_t hand_id;
	enum hand_work hand_work;
	/* the look on the finger was pulled down from what it goes to, at the top of the desk:
	 * it goes as far as the finger has come down (2026-11-12) */
	bool look_down;
	/* a binder's sheets on it: the finger's angle round the ring as it was put down, last, and how
	 * far round it has gone since */
	double sheets_a0, sheets_a, sheets_round, sheets_fx, sheets_fy;
	/* a slip's card on it: where the finger and the card were when it took hold */
	double card_y0, card_pull0;
	/* the lid on it: where the place on it the finger holds is on the screen, at each twentieth
	 * of the way up, and how far the finger was from it when it took hold */
	double lid_y[21], lid_off;
	/* a finger on one of the Control Panel's buttons that slid a little and did nothing else: let
	 * go on the same button, it pressed it (hand_slid_tap) */
	bool hand_may_tap;
	bool hand_edge;
	bool hand_across, hand_axis;
	double hand_x0, hand_y0, hand_fed, hand_t0;
	bool drawer_held;
	/* A look on the finger: one more than the view; how far the desk has gone on the
	 * screen, at each twentieth of the way */
	int look_held;
	double look_along[21], look_along0;
	/* And let go of: one more than the view, going on from the finger at this speed,
	 * in the look's own measure a second */
	int look_free;
	double look_v, look_at;
	/* The drawer let go of: where it is (how far the desk is up), how fast, and which end */
	bool drawer_free;
	double drawer_x, drawer_v, drawer_at;
	double sheets_from, sheets_to;
	struct desk_toplevel *curl_view;
	/* The whole hand */
	enum whole_work whole_work;
	struct desk_toplevel *whole_view;
	double whole_full;            /* how far the hand goes for all of it */
	double whole_dy;
	bool sw_held;                 /* the windows spread as far as the hand has come */
	double sw_hand;
	struct desk_hand_crumple *crumple;
	/* A sheet thrown: going on across the desk from the hand */
	struct desk_toplevel *throw_view;
	double throw_x, throw_y, throw_vx, throw_vy, throw_at;
	struct wlr_scene_tree *layer_dark;
	/* The display tools -- wlr-randr, kanshi, wdisplays (wlr-output-
	 * management): what they are told the screens are, and what they ask.
	 * The size of a point one of them asks for goes into shell.conf. */
	struct wlr_output_manager_v1 *om;
	struct wl_listener om_apply, om_test;
	/* the screens set out again as displays.conf has them, once the ones
	 * that are coming have all come (see "the screens as they were left");
	 * and that, when they came while the desk was dark, left until it is
	 * lit again (setting them out would light them) */
	struct wl_event_source *disp_idle;
	bool disp_after_dark;
	/* the screens set out some other way: the bare desk on the ones beside
	 * the desk's, and the windows on a screen that has gone put on one
	 * that is there -- once, after all of a change (screens_idle_cb) */
	struct wl_listener layout_change;
	struct wl_event_source *screens_idle;
	/* when the frame's work (output_frame) was last done: once for all
	 * the screens, not once for each */
	double ticked_at;
	/* how far the lamp is up for the bare desk beside the desk's screen:
	 * as it is for the desk at login and in a show (night_paint) */
	double side_light;
	/* Ending the session (end_begin): every window asked to close first,
	 * and the logging out, restart or power off done once they all have.
	 * ending is 0, or 1 + enum desk_end. */
	int ending;
	bool end_anyway;            /* asked again just after a window held it up */
	bool end_done;              /* the ending itself is under way */
	double end_held_at;         /* when a window last held it up */
	struct wl_event_source *end_timer;
	/* Ctrl+Alt+Del, counted: how many in a row, the last when (real time) */
	int cad_n;
	double cad_at;
	struct wl_event_source *cad_timer;
};

/* Started and let go of. Forked twice, so that what runs is not a child of
 * ours: it is nobody's to wait for, and leaves no zombie when it ends.
 * (Not by ignoring SIGCHLD, which does the same for every child there is
 * -- including the ones that are waited for, like the fork wlroots starts
 * Xwayland through, which then fails to start.) */
static void spawn_argv(char *const argv[])
{
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		if (fork() == 0) {
			desk_close_from(3);
			execvp(argv[0], argv);
			_exit(127);
		}
		_exit(0);
	}
	if (pid > 0) {
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
			;
		}
	}
}

/* The same, for a command line as a shell has it: -s's, and the words of
 * one of the desk's own (none of it anybody else's to choose). A path is
 * never put in one -- that goes as a word of its own (spawn_argv). */
static void spawn(const char *cmd)
{
	char *const argv[] = { (char *)"/bin/sh", (char *)"-c", (char *)cmd, NULL };
	spawn_argv(argv);
}

static void own_letter_action(struct desk_server *server, unsigned id, const char *key);

static bool on_path(const char *name)
{
	const char *path = getenv("PATH");
	char buf[1024];
	for (const char *p = path && *path ? path : "/usr/local/bin:/usr/bin:/bin"; *p;) {
		const char *e = strchr(p, ':');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		size_t m = strlen(name);
		if (n > 0 && n + 1 + m < sizeof buf) {
			memcpy(buf, p, n);
			buf[n] = '/';
			memcpy(buf + n + 1, name, m + 1);
			if (access(buf, X_OK) == 0) {
				return true;
			}
		}
		if (!e) {
			break;
		}
		p = e + 1;
	}
	return false;
}

/* The first terminal there is: the Wayland ones first, then the desktops'
 * own -- which a machine with a desktop already has -- and xterm, through
 * Xwayland, when there is nothing else. */
static const char *terminal_cmd(void)
{
	static const char *const terms[] = {
		"foot", "alacritty", "kitty", "wezterm", "weston-terminal",
		"konsole", "gnome-terminal", "kgx", "xfce4-terminal", "mate-terminal",
		"lxterminal", "tilix", "terminator",
	};
	for (size_t i = 0; i < sizeof terms / sizeof terms[0]; i++) {
		if (on_path(terms[i])) {
			return terms[i];
		}
	}
	return "xterm";
}

static void cairo_buffer_destroy(struct wlr_buffer *wlr_buffer)
{
	struct desk_buf *buffer = wl_container_of(wlr_buffer, buffer, base);
	if (buffer->cr) {
		cairo_destroy(buffer->cr);
	}
	cairo_surface_destroy(buffer->surface);
	free(buffer);
}

static bool cairo_buffer_begin(struct wlr_buffer *wlr_buffer, uint32_t flags,
	void **data, uint32_t *format, size_t *stride)
{
	struct desk_buf *buffer = wl_container_of(wlr_buffer, buffer, base);
	(void)flags;
	cairo_surface_flush(buffer->surface);
	*format = DRM_FORMAT_ARGB8888;
	*data = cairo_image_surface_get_data(buffer->surface);
	*stride = cairo_image_surface_get_stride(buffer->surface);
	return true;
}

static void cairo_buffer_end(struct wlr_buffer *wlr_buffer)
{
	(void)wlr_buffer;
}

static const struct wlr_buffer_impl cairo_buffer_impl = {
	.destroy = cairo_buffer_destroy,
	.begin_data_ptr_access = cairo_buffer_begin,
	.end_data_ptr_access = cairo_buffer_end,
};

/* The rest of this file is kept in parts, one to a subject, in wm/.
 * They are included here, in this order, and compiled as this one file:
 * what is static in one part is seen by the parts after it. */
#include "wm/scene.c"
#include "wm/sounds-sheets.c"
#include "wm/bookcase.c"
#include "wm/calendar.c"
#include "wm/memo.c"
#include "wm/agenda.c"
#include "wm/console.c"
#include "wm/post.c"
#include "wm/window-paper.c"
#include "wm/shell-conf.c"
#include "wm/drawer.c"
#include "wm/looks-focus.c"
#include "wm/window-moves.c"
#include "wm/clock-cards.c"
#include "wm/boot-termites.c"
#include "wm/switcher.c"
#include "wm/lock.c"
#include "wm/idle-outputs.c"
#include "wm/print.c"
#include "wm/keys.c"
#include "wm/seat-pointer.c"
#include "wm/touch-pen.c"
#include "wm/views.c"
#include "wm/outputs.c"
#include "wm/xdg-xwayland.c"
#include "wm/main.c"
