# Lessons

Mistakes made here before, and what to do instead. Read this before
starting work; add to it when something goes wrong.

- **Change a file, write `SHA256SUMS` again.** The installers refuse a copy
  whose files differ from it. `sh scripts/make-sums.sh`, in the same commit.
- **A headless run that exits 0 has not shown a window was drawn.** The
  first client tests here "passed" with no window on the desk: the terminal
  could not open a PTY in the chroot and quit, and `xeyes` never maps
  headless. Look at the picture, and use `foot` (with `/dev/pts` mounted)
  and `xclock`.
- **Compare with the untouched build before calling something a
  regression.** The "Floating point exception" at the end of a run looked
  like the wlroots port's doing and was in v0.2.1 all along.
- **wlroots 0.19 and later abort on leftover listeners.** Any new
  `wl_signal_add` on a thing the server owns needs its listener in
  `server_listeners_drop` (`src/wm/main.c`), or taken off where the thing
  is destroyed.
- **wlroots differences go in `include/wlrcompat.h`**, under a `desk_`
  name, not in an `#if` at the place of use.
- **The parts of `wm.c` and `draw.c` are order-dependent.** A new part goes
  after the parts whose `static` functions it calls; a part is not compiled
  on its own.
