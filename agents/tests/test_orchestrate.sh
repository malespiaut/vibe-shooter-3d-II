#!/usr/bin/env bash
# Unit tests for agents/orchestrate.sh. Uses only the fakes in tests/fake.
here=$(cd "$(dirname "$0")" && pwd)
export PATH="$here/fake:$PATH"
fails=0
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

t() { # t <name>: run test function in a subshell with fresh fixtures
  local name=$1
  if (setup "$name" && "$name"); then echo "ok   $name"; else echo "FAIL $name"; fails=$((fails + 1)); fi
}

setup() {
  local tmp=$root/$1
  mkdir -p "$tmp"
  export FIXTURES=$tmp/fx WORK=$tmp/work LOGS=$tmp/logs STATE=$tmp/state
  mkdir -p "$FIXTURES" "$WORK" "$LOGS" "$STATE/roles"
  : > "$FIXTURES/calls.log"; : > "$FIXTURES/ps.txt"; echo 0 > "$FIXTURES/run-exit"
  echo common > "$STATE/roles/common.md"; echo dev > "$STATE/roles/developer.md"
  echo ghtok > "$tmp/gh-token"; echo cltok > "$tmp/claude-token"
  export GH_TOKEN_FILE=$tmp/gh-token CLAUDE_TOKEN_FILE=$tmp/claude-token
  # shellcheck source=SCRIPTDIR/../orchestrate.sh
  ORCH_LIB=1 source "$here/../orchestrate.sh"
}

issue() { # issue <n> <state> <author> <labels,csv> [body] [failure-comments]
  local labels comments
  labels=$(jq -cn --arg l "$4" '$l | split(",") | map(select(. != "") | {name: .})')
  comments=$(jq -cn --argjson k "${6:-0}" '[range($k)] | map({author: {login: "malespiaut"}, body: "[orchestrator] run failed: exit 1"})')
  jq -cn --argjson n "$1" --arg s "$2" --arg a "$3" --argjson l "$labels" --arg b "${5:-}" --argjson c "$comments" \
    '{number: $n, title: "issue \($n)", state: $s, author: {login: $a}, labels: $l, body: $b, comments: $c, updatedAt: "2026-01-01T00:00:00Z"}'
}
issues() { jq -s . > "$FIXTURES/issues.json"; }

test_ready_basic() {
  { issue 1 OPEN malespiaut ready; issue 2 OPEN malespiaut ready,in-progress; issue 3 OPEN malespiaut ""; } | issues
  [[ $(ready_issues) == 1 ]]
}

test_ignores_foreign_author() {
  issue 4 OPEN stranger ready | issues
  [[ -z $(ready_issues) ]]
}

test_deps_block_dispatch() {
  { issue 5 OPEN malespiaut ready "Depends on #6"; issue 6 OPEN malespiaut ""; } | issues
  [[ -z $(ready_issues) ]] || return 1
  { issue 5 OPEN malespiaut ready "Depends on #6"; issue 6 CLOSED malespiaut ""; } | issues
  [[ $(ready_issues) == 5 ]]
}

test_failure_releases_claim() {
  issue 5 OPEN malespiaut ready,in-progress | issues
  echo 1 > "$FIXTURES/run-exit"
  run_agent developer dev-5 "$WORK" "issue 5"
  finish_dev 5 $?
  grep -q -- '--remove-label in-progress' "$FIXTURES/calls.log" && grep -q 'run failed' "$FIXTURES/calls.log"
}

test_third_failure_marks_stuck() {
  issue 5 OPEN malespiaut ready,in-progress "" 2 | issues
  release 5 "exit 1"
  grep -q -- '--add-label stuck' "$FIXTURES/calls.log"
}

test_usage_limit_not_failure() {
  issue 5 OPEN malespiaut ready,in-progress | issues
  echo 1 > "$FIXTURES/run-exit"; echo 'Claude usage limit reached' > "$FIXTURES/run-output.txt"
  run_agent developer dev-5 "$WORK" "issue 5"
  local rc=$?
  finish_dev 5 $rc
  [[ $rc == 75 ]] && ! grep -q 'run failed' "$FIXTURES/calls.log"
}

test_protected_paths_pr_closed() {
  echo '[{"number": 8, "files": [{"path": "src/a.c"}, {"path": "agents/orchestrate.sh"}]},
         {"number": 9, "files": [{"path": "src/a.c"}]}]' > "$FIXTURES/prs.json"
  protected_pr 8 && ! protected_pr 9
}

test_review_skips_seen_sha() {
  echo '[{"number": 7, "author": {"login": "malespiaut"}, "headRefOid": "abc", "headRefName": "issue-1", "labels": []},
         {"number": 10, "author": {"login": "malespiaut"}, "headRefOid": "def", "headRefName": "issue-2", "labels": []}]' > "$FIXTURES/prs.json"
  mkdir -p "$STATE/reviewed"; echo abc > "$STATE/reviewed/7"
  [[ $(prs_needing_review) == 10 ]]
}

test_episode_done() {
  issue 99 CLOSED malespiaut "" | jq '.title = "Final acceptance"' | issues
  mkdir -p "$FIXTURES/api"
  local m
  m=$FIXTURES/api/$(printf '%s' "repos/$REPO/milestones?state=all" | tr -c 'A-Za-z0-9' _).json
  echo '[{"title": "Episode 1", "state": "open"}]' > "$m"
  ! episode_done || return 1
  echo '[{"title": "Episode 1", "state": "closed"}]' > "$m"
  episode_done
}

for f in $(declare -F | awk '$3 ~ /^test_/ {print $3}'); do t "$f"; done
if ((fails)); then echo "$fails test(s) failed"; exit 1; fi
echo "all tests passed"
