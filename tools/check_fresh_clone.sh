#!/usr/bin/env bash
# Prove that a fresh clone builds, regenerates every asset and passes ctest
# with exactly the commands documented in README.md.
#
#   tools/check_fresh_clone.sh [<repo-url-or-path>] [<workdir>]
#
# <repo-url-or-path> defaults to this repository, <workdir> to a new mktemp -d.
# The clone uses --no-local, so untracked and ignored files (build/, assets/gen/)
# cannot leak in: only committed history is checked (HEAD of the source repo).
# Stops at the first failing step with a non-zero exit; prints FRESH CLONE OK on success.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
repo="${1:-$here}"
work="${2:-$(mktemp -d)}"
dest="$work/clone"

if [ -e "$dest" ]; then
    echo "check_fresh_clone: $dest already exists" >&2
    exit 1
fi

step() {
    echo "== $*"
    "$@" || { rc=$?; echo "check_fresh_clone: FAILED ($rc): $*" >&2; exit "$rc"; }
}

step git clone --quiet --no-local "$repo" "$dest"
cd "$dest"
echo "check_fresh_clone: $(git rev-parse --short HEAD) in $dest"

# README.md, "Build and run", in order.
step cmake -S . -B build -G Ninja
step cmake --build build
step tools/build_assets.sh
step python3 tools/palcheck.py assets/gen
step ctest --test-dir build --output-on-failure

echo "FRESH CLONE OK"
