#!/usr/bin/env bash
# Pins the host-side orchestrator into ~/vibe-agents/bin (copied from this checkout, not origin).
# Idempotent. Does not start the orchestrator.
set -euo pipefail

SRC=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BASE=$HOME/vibe-agents
REPO=${REPO:-malespiaut/vibe-shooter-3d-II}

for t in claude-token gh-token; do
  [[ -f $BASE/$t ]] || { echo "install: missing $BASE/$t" >&2; exit 1; }
  [[ $(stat -c %a "$BASE/$t") == 600 ]] || { echo "install: $BASE/$t must be mode 600 (chmod 600 $BASE/$t)" >&2; exit 1; }
done

mkdir -p "$BASE"/{bin,work,logs,state/roles}
install -m 755 "$SRC/orchestrate.sh" "$SRC/entrypoint.sh" "$BASE/bin/"
install -m 644 "$SRC/Containerfile" "$BASE/bin/"

if [[ -d $SRC/roles ]]; then
  cp -a "$SRC/roles/." "$BASE/state/roles/"
else
  echo "install: warning: $SRC/roles missing (Task 5); orchestrator will sync roles from origin/main" >&2
fi

# Host-private mirror, outside the container mount: the host never runs git in agent-writable dirs.
if [[ -d $BASE/state/mirror ]]; then
  git -C "$BASE/state/mirror" fetch -q --prune origin
else
  git clone -q --mirror "https://github.com/$REPO.git" "$BASE/state/mirror"
fi
[[ -d $BASE/work/repo ]] && echo "install: note: $BASE/work/repo is no longer used (agents get fresh clones of state/mirror); safe to delete" >&2

podman build -t vibe-agents -f "$BASE/bin/Containerfile" "$BASE/bin"
echo "install: ok"
