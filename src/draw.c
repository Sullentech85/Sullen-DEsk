/* ================================================================
 * The drawing library (include/desk.h).
 *
 * Everything DEsk shows that is not a program's own window: the room, ray
 * traced, and everything drawn over it with cairo. The compositor (wm.c),
 * Shell Settings, the Experience Index, desk-preview and desk-gputest all
 * draw through here, so a thing looks the same wherever it is drawn.
 *
 *   The room            one signed-distance scene (scene_map), marched per
 *                       sample, traced as sheets -- the room, the drawer,
 *                       the bookcase, the Control Panel, the post box, the
 *                       films of the camera moving to them -- on every CPU
 *                       core, or on the GPU (room.glsl, gpu.c: a port of
 *                       this C, which stays the reference), and kept in
 *                       $XDG_CACHE_HOME/desk.
 *   Flat things         window frames and paper, folding, crumpling, the
 *                       book and binders, letters, the calendar and memo
 *                       pad, Shell Settings' form, the Control Panel's
 *                       live parts, the clock's displays.
 *   The front door      the lock screen: a second, small tracer for the
 *                       door and the walk in.
 *
 * Big sections open with a ==== banner and smaller ones with ---- name
 * ----: search for either for a table of contents. Coordinates given to the
 * public functions are the screen's pixels; sizes are worked out in points
 * (desk_px_per_pt). docs/ARCHITECTURE.md has the overview.
 * ================================================================ */

#include "calcart.h"
#include "notebook.h"
#include "fonts.h"
#include "i18n.h"
#include "memo.h"
#include "desk.h"
#include "agenda.h"
#include "apps.h"
#include "panel.h"
#include "post.h"
#include <pango/pangocairo.h>

#include <strings.h>

#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/resource.h>
#include <stdatomic.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "room_key.h"

#ifdef DESK_HAVE_GPU
#include "gpu.h"
#include "room_glsl.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double clampf(double v, double lo, double hi)
{
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static double fract(double v)
{
	return v - floor(v);
}

static double hash21(double x, double y)
{
	double s = sin(x * 127.1 + y * 311.7) * 43758.5453;
	return fract(s);
}

/* Value noise, hashed on integers. Everything textured in the room is
 * built out of this and room_fbm: wood, felt, paper, leather, the pebbling
 * on moulded plastic.
 *
 * It used to be hashed with hash21, the usual fract(sin(x) * 43758), which
 * is only as good as the sine it is handed. In double that is fine. In the
 * float a GPU works in, the argument has already lost the digits the
 * multiply then drags up into the answer, and it is a different number
 * altogether -- so a shader and this file would scratch the desk in
 * different places. The room is traced by both, so it is hashed where there
 * are no digits to lose: on the integer lattice, with PCG's output
 * permutation (Jarzynski and Olano), which a shader computes to the same
 * bit. room.glsl has the same functions.
 *
 * Only the traced room. Paper, chrome and letters are drawn here and
 * nowhere else, and keep hash21. */
static uint32_t room_mix(uint32_t v)
{
	uint32_t s = v * 747796405u + 2891336453u;
	uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
	return (w >> 22u) ^ w;
}

static double room_hash(int x, int y)
{
	uint32_t h = room_mix((uint32_t)x + room_mix((uint32_t)y));
	return (double)(h >> 8) * (1.0 / 16777216.0);
}

/* One number for the i-th of something -- a board, a book, a key -- off a
 * row of the lattice that no noise ever reaches. */
#define ROOM_SEED_ROW 0x100000
static double room_seed(int i, int salt)
{
	return room_hash(i, ROOM_SEED_ROW + salt);
}

static double room_noise(double x, double y)
{
	double xi = floor(x), yi = floor(y);
	double xf = x - xi, yf = y - yi;
	double u = xf * xf * (3.0 - 2.0 * xf);
	double v = yf * yf * (3.0 - 2.0 * yf);
	int ix = (int)xi, iy = (int)yi;
	double a = room_hash(ix, iy);
	double b = room_hash(ix + 1, iy);
	double c = room_hash(ix, iy + 1);
	double d = room_hash(ix + 1, iy + 1);
	double top = a + (b - a) * u;
	double bot = c + (d - c) * u;
	return top + (bot - top) * v;
}

static double room_fbm(double x, double y)
{
	double sum = 0.0, amp = 0.5, f = 1.0;
	for (int i = 0; i < 5; i++) {
		sum += amp * room_noise(x * f, y * f);
		amp *= 0.5;
		f *= 2.0;
	}
	return sum;
}

typedef struct {
	double x, y, z;
} vec3;

static vec3 v3(double x, double y, double z)
{
	return (vec3){ x, y, z };
}

static vec3 vadd(vec3 a, vec3 b)
{
	return (vec3){ a.x + b.x, a.y + b.y, a.z + b.z };
}

static vec3 vsub(vec3 a, vec3 b)
{
	return (vec3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static vec3 vmul(vec3 a, double s)
{
	return (vec3){ a.x * s, a.y * s, a.z * s };
}

static vec3 vmul3(vec3 a, vec3 b)
{
	return (vec3){ a.x * b.x, a.y * b.y, a.z * b.z };
}

static vec3 vmix(vec3 a, vec3 b, double t)
{
	return (vec3){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

static double vdot(vec3 a, vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static double vlen(vec3 a)
{
	return sqrt(vdot(a, a));
}

static vec3 vnorm(vec3 a)
{
	double l = vlen(a);
	return l > 1e-12 ? vmul(a, 1.0 / l) : a;
}

static vec3 vcross(vec3 a, vec3 b)
{
	return (vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

static vec3 rot_x(vec3 p, double a)
{
	double c = cos(a), s = sin(a);
	return v3(p.x, p.y * c - p.z * s, p.y * s + p.z * c);
}

static vec3 rot_y(vec3 p, double a)
{
	double c = cos(a), s = sin(a);
	return v3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

static vec3 rot_z(vec3 p, double a)
{
	double c = cos(a), s = sin(a);
	return v3(p.x * c - p.y * s, p.x * s + p.y * c, p.z);
}

/* The rest of this file is kept in parts, one to a subject, in draw/.
 * They are included here, in this order, and compiled as this one file:
 * what is static in one part is seen by the parts after it. */
#include "draw/room-measures.c"
#include "draw/room-things.c"
#include "draw/room-console.c"
#include "draw/room-bookcase.c"
#include "draw/room-wood.c"
#include "draw/room-render.c"
#include "draw/room-faces.c"
#include "draw/room-views.c"
#include "draw/room-kept.c"
#include "draw/flat.c"
#include "draw/paper.c"
#include "draw/fold-crumple.c"
#include "draw/drawer-tools.c"
#include "draw/shelf.c"
#include "draw/binder.c"
#include "draw/book.c"
#include "draw/settings-pad.c"
#include "draw/console.c"
#include "draw/console-live.c"
#include "draw/post.c"
#include "draw/desk-things.c"
#include "draw/clock.c"
#include "draw/cases.c"
#include "draw/calendar.c"
#include "draw/agenda-memo.c"
#include "draw/mat-papers.c"
#include "draw/preview.c"
#include "draw/door-scene.c"
#include "draw/door-light.c"
#include "draw/door-trace.c"
#include "draw/door-over.c"
