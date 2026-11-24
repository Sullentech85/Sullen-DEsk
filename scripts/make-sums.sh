#!/bin/sh
# make-sums.sh [--check] -- SHA256SUMS, written again (or only checked).
#
# The installers check every file of a copy against SHA256SUMS before they
# build it, and stop if one is not as it was listed. So after any file in
# the repository changes, SHA256SUMS has to be written again, and committed
# with the change:
#
#     sh scripts/make-sums.sh
#
# --check writes nothing: it says whether SHA256SUMS lists exactly the files
# git tracks (but for itself) and whether each is as listed. CI runs that.
set -eu
top=$(cd "$(dirname "$0")/.." && pwd)
cd "$top"
command -v git >/dev/null 2>&1 || { echo "make-sums.sh: git is needed (the list is of the files git tracks)" >&2; exit 2; }
list() {
	git ls-files | grep -vx 'SHA256SUMS' | LC_ALL=C sort | sed 's|^|./|'
}
if [ "${1:-}" = "--check" ]; then
	want=$(list)
	have=$(sed 's/^[0-9a-f]*  //' SHA256SUMS)
	if [ "$want" != "$have" ]; then
		echo "make-sums.sh: SHA256SUMS does not list the files git tracks; run sh scripts/make-sums.sh" >&2
		exit 1
	fi
	sha256sum --quiet -c SHA256SUMS || {
		echo "make-sums.sh: a file is not as SHA256SUMS lists it; run sh scripts/make-sums.sh" >&2
		exit 1
	}
	echo "SHA256SUMS: $(wc -l < SHA256SUMS) files, each as listed."
	exit 0
fi
list | tr '\n' '\0' | xargs -0 sha256sum > SHA256SUMS.new
mv SHA256SUMS.new SHA256SUMS
echo "SHA256SUMS: $(wc -l < SHA256SUMS) files listed."
