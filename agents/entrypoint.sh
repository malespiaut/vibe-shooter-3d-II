#!/usr/bin/env bash
# usage: entrypoint.sh <workdir> <prompt-file>
set -euo pipefail

# keep-id leaves HOME pointing at a path missing from the image
export HOME="${HOME:-/tmp/agent-home}"
mkdir -p "$HOME" 2>/dev/null || { export HOME=/tmp/agent-home; mkdir -p "$HOME"; }

workdir=${1:?workdir}
prompt=${2:?prompt-file}

cd "$workdir"
export GIT_COMMITTER_NAME="${GIT_AUTHOR_NAME:-}" GIT_COMMITTER_EMAIL="${GIT_AUTHOR_EMAIL:-}"
gh auth setup-git
git config --global user.name "${GIT_AUTHOR_NAME:?}"
git config --global user.email "${GIT_AUTHOR_EMAIL:?}"

exec xvfb-run -a claude -p --dangerously-skip-permissions --model claude-opus-5-5 \
  --output-format stream-json --verbose < "$prompt"
