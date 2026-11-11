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

[[ -d $BASE/work/repo ]] || git clone "https://github.com/$REPO.git" "$BASE/work/repo"

podman build -t vibe-agents -f "$BASE/bin/Containerfile" "$BASE/bin"
echo "install: ok"
