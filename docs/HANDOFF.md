# Handoff

What was last done here, what was decided, and what comes next. Rewrite the
top section at the end of each working session; keep older ones below it.

## 2026-11-24 -- making the fork its own, CI, wlroots 0.19 and 0.20

Branch `make-it-mine`, on top of v0.2.1 (`afa666c`).

### What was built

1. **The fork's own name on things.** README (English and Japanese), the
   Nix packaging and `desk-report` point at `Sullentech85/Sullen-DEsk`. The
   previous homepage and chat links are gone, and `docs/design.md` (the
   Japanese development log) is removed. `LICENSE` is untouched.
2. **Shell calls audited.** Every place the C code starts another program
   was read (`system`, `popen`, `exec*`, `posix_spawn`), and every shell
   script was run through shellcheck. Nothing could be steered from
   outside. The one change: the Control Panel's volume knob used `popen()`
   and `system()` with fixed command lines; it now starts `wpctl` directly
   (`src/panel.c`, `wpctl_run`).
3. **`wm.c` and `draw.c` split** into 26 and 31 parts in `src/wm/` and
   `src/draw/`, included in order by a short head. Still one translation
   unit each; the preprocessed code was compared before and after and is
   the same but for the comment at the head of each part.
4. **CI**: `.github/workflows/ci.yml` and `scripts/ci.sh` (build, tests,
   `SHA256SUMS`, shellcheck, headless runs), and `scripts/make-sums.sh`.
5. **wlroots 0.19 and 0.20**: `include/wlrcompat.h`, the listeners taken
   off before shutdown (`server_listeners_drop` in `src/wm/main.c`), and
   `install_ubuntu.sh` taking the distribution's wlroots where it has one.
6. **A crash fixed on the way**: a division by zero in a log line when the
   session ends at 2D Fallback while the room is being made
   (`src/draw/room-views.c`). It was in v0.2.1 too.

### How it was checked

In chroots of Ubuntu 25.10 (wlroots 0.18.2), 26.04 (0.19.2) and 26.10
(0.20.2), on a machine with no GPU: `sh scripts/ci.sh deps lint build test
smoke` passes in all three. That is the build, the four meson tests, and the
desk started headless with the software renderer three times -- alone, with
`foot` and with `xclock` -- with its pictures looked at (the window is on
its paper in each).

### What was not checked

- **A real screen, a GPU, a login session, input devices.** Everything
  above ran headless on pixman.
- **NIGHT on wlroots 0.20.** Gamma tables became a colour transform there
  (`desk_output_state_set_gamma`); it compiles and nothing more is known.
- **Recording (Print's films) on 0.19 and later.** It reads a field wlroots
  now marks private (`desk_scene_output_pending_damage`). It compiles;
  no film was made.
- **Touch and pen on 0.19 and later**: two calls had their arguments
  swapped; compiled only.
- **Debian 13.** CI's 0.18 job runs in `debian:trixie-slim`, but the 0.18
  runs here were on Ubuntu 25.10 (Debian's package server could not be
  reached from where this was done).
- **The Arch and Fedora installers and the Nix flake** were not changed and
  still fetch or build wlroots 0.18. Arch's own wlroots is 0.20, which DEsk
  now builds on, so `install_arch.sh` could use it: not done, not testable
  here.

### Decisions

- The split keeps one translation unit on purpose. Making the parts compile
  on their own means giving a few thousand `static` functions external
  names and headers: a much larger change, and not one that can be shown to
  change nothing.
- Credits to contributors in `RELEASE-NOTES.md` were kept. They are not the
  previous owner, and their issue and pull-request numbers refer to the
  repository this was forked from.
- Git history was not rewritten: the commits before this branch still carry
  their author's name.
- This is **v1.0 (BETA)** (`meson.build` 1.0.0), merged to `main`. The plan
  from here: v1.1 adds features and more for speed and security; v2.0 is
  the live test on real machines. It stays "BETA" until that test.
- No release archive (`DEsk-v1.0-BETA.tar.gz`) has been made or published,
  and no tag: the release notes' "Installing" section speaks of one.

### Next

1. Try the branch on a real machine (GPU, login screen), on wlroots 0.19 or
   0.20: log in, NIGHT, a film with Print, a touch screen if there is one.
2. Tag v1.0 and, if it is to be downloaded, make its archive.
3. `install_arch.sh`: use Arch's wlroots 0.20 instead of building 0.18.3.
4. The Japanese documents were only touched where they named the previous
   owner or the removed log; "Working on DEsk" is in the English README
   only.
