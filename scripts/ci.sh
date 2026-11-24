#!/bin/sh
# ci.sh STEP... -- what CI runs, a step at a time, so the same can be run by
# hand: in a container or a chroot of the distribution, as root.
#
#   deps    the packages DEsk is built with (apt; WLROOTS=0.18 chooses the
#           libwlroots-*-dev package, else the newest the distribution has)
#   lint    SHA256SUMS against the files, and the shell scripts (shellcheck,
#           errors only)
#   build   meson setup and compile, into build-ci
#   test    meson test
#   smoke   install it, draw the preview and start the desk with no screen
#           and no GPU (headless, pixman), and take its picture: into
#           $CI_OUT (default out-ci)
#
# .github/workflows/ci.yml runs: deps lint build test smoke.
set -eu
top=$(cd "$(dirname "$0")/.." && pwd)
cd "$top"
out=${CI_OUT:-$top/out-ci}

step_deps() {
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -q
	wl=${WLROOTS:-}
	if [ -z "$wl" ]; then
		for v in 0.20 0.19 0.18; do
			if apt-cache show "libwlroots-$v-dev" >/dev/null 2>&1; then
				wl=$v
				break
			fi
		done
	fi
	[ -n "$wl" ] || { echo "ci.sh: this distribution has no libwlroots-0.18-dev or newer" >&2; exit 1; }
	echo "ci.sh: wlroots $wl"
	apt-get install -y -q --no-install-recommends \
		gcc libc6-dev meson ninja-build pkg-config python3 git ca-certificates shellcheck \
		"libwlroots-$wl-dev" libwayland-dev wayland-protocols libxkbcommon-dev libinput-dev \
		libcairo2-dev libpango1.0-dev libgtk-3-dev libpixman-1-dev libfontconfig-dev \
		libfreetype-dev libdrm-dev libegl-dev libgles-dev libgbm-dev libsystemd-dev \
		libpulse-dev libpam0g-dev libxcb1-dev libxcb-icccm4-dev libxcb-ewmh-dev \
		xwayland fonts-dejavu-core
}

step_lint() {
	# (a checkout made by another user, as CI's is, is still ours to read)
	git config --global --add safe.directory "$top" 2>/dev/null || true
	sh scripts/make-sums.sh --check
	shellcheck -S error scripts/*.sh scripts/desk-ctl scripts/desk-record scripts/desk-report \
		scripts/desk-screen-chooser scripts/desk-screenshot scripts/desk-session-helper \
		scripts/desk-window session/desk-session
	echo "shellcheck: no errors."
}

step_build() {
	[ -d build-ci ] || meson setup build-ci
	meson compile -C build-ci
}

step_test() {
	meson test -C build-ci --print-errorlogs
}

step_smoke() {
	meson install -C build-ci --no-rebuild --quiet
	rm -rf "$out"
	mkdir -p "$out"
	run=$(mktemp -d)
	mkdir -p "$run/xdg" "$run/config/desk" "$run/cache"
	chmod 700 "$run/xdg"
	export XDG_RUNTIME_DIR="$run/xdg" XDG_CONFIG_HOME="$run/config" XDG_CACHE_HOME="$run/cache"
	export XDG_SESSION_TYPE=wayland WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1
	# the lamp lit at once, and the desk as a machine without a GPU has it
	printf 'lights_at_login = no\ngraphics = 2d\n' > "$run/config/desk/shell.conf"
	desk --version
	timeout 900 desk-preview -o "$out/preview.png" -s 1280x800 > "$out/preview.log" 2>&1
	timeout 600 desk --headless --size 1280x800 --dump "$out/chrome.png" \
		--screenshot "$out/desktop.png" --delay 6000 --exit-after-screenshot \
		> "$out/desk.log" 2>&1
	rm -rf "$run"
	bad=0
	for f in preview.png chrome.png desktop.png; do
		# (a picture of the desk is hundreds of kilobytes; an empty one a few)
		if [ ! -s "$out/$f" ] || [ "$(wc -c < "$out/$f")" -lt 50000 ]; then
			echo "ci.sh: $f was not made, or is empty" >&2
			bad=1
		fi
	done
	if grep -q '\[ERROR\]' "$out/desk.log"; then
		echo "ci.sh: the desk logged errors:" >&2
		grep '\[ERROR\]' "$out/desk.log" >&2
		bad=1
	fi
	ls -l "$out"
	[ "$bad" -eq 0 ] || exit 1
	echo "smoke: the desk came up and its picture was taken."
}

[ $# -gt 0 ] || { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
for s in "$@"; do
	case "$s" in
	deps|lint|build|test|smoke) echo ">> ci.sh: $s"; "step_$s" ;;
	*) echo "ci.sh: no step called $s" >&2; exit 2 ;;
	esac
done
