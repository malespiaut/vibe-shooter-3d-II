#!/usr/bin/env bash
# Unit tests for agents/orchestrate.sh. Uses only the fakes in tests/fake.
here=$(cd "$(dirname "$0")" && pwd)
export PATH="$here/fake:$PATH"
fails=0
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

t() { # t <name>: run test function in a subshell with fresh fixtures
  local name=$1
  # orchestrator logs go to stderr; show them only when the test fails
  if (setup "$name" && "$name") 2>"$root/$name.err"; then echo "ok   $name"
  else echo "FAIL $name"; sed 's/^/    /' "$root/$name.err"; fails=$((fails + 1)); fi
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

test_usage_limit_ignores_digits() {
  local f=$LOGS/x.jsonl
  echo '{"type":"assistant","message":"set the fire rate limit to 4"}' > "$f"
  echo '{"type":"result","is_error":true,"duration_ms":14290,"num_turns":429}' >> "$f"
  ! is_usage_limit "$f" || return 1
  local m
  for m in "Claude AI usage limit reached|1700000000" "You've hit your limit · resets 3pm (Europe/Paris)" \
           "5-hour limit reached ∙ resets 9pm" "Weekly limit reached · resets Mon 3am"; do
    jq -cn --arg m "$m" '{type: "result", is_error: true, result: $m}' > "$f"
    is_usage_limit "$f" || { echo "missed: $m"; return 1; }
  done
  echo 'API Error: 429 {"type":"error","error":{"type":"rate_limit_error"}}' > "$f"
  is_usage_limit "$f"
}

test_deps_multi() {
  local form
  for form in "Depends on #3, #6" "Depends on #3 and #6" "depends on #3 #6"; do
    { issue 5 OPEN malespiaut ready "Intro.
$form
See #9 for context."; issue 3 CLOSED malespiaut ""; issue 6 OPEN malespiaut ""; issue 9 OPEN malespiaut ""; } | issues
    [[ -z $(ready_issues) ]] || { echo "not blocked: $form"; return 1; }
  done
  { issue 5 OPEN malespiaut ready "Depends on #3 and #6"; issue 3 CLOSED malespiaut ""; issue 6 CLOSED malespiaut ""; issue 9 OPEN malespiaut ""; } | issues
  [[ $(ready_issues) == 5 ]]
}

test_episode_ignores_stranger() {
  issue 99 CLOSED stranger "" | jq '.title = "Final acceptance"' | issues
  mkdir -p "$FIXTURES/api"
  echo '[{"title": "Episode 1", "state": "closed"}]' > "$FIXTURES/api/$(printf '%s' "repos/$REPO/milestones?state=all" | tr -c 'A-Za-z0-9' _).json"
  ! episode_done
}

test_infra_failure_not_counted() {
  issue 5 OPEN malespiaut ready,in-progress | issues
  rm "$STATE/roles/developer.md"
  run_agent developer dev-5 "$WORK" "issue 5"
  local rc=$?
  finish_dev 5 $rc
  [[ $rc == "$RC_INFRA" ]] && grep -q -- '--remove-label in-progress' "$FIXTURES/calls.log" &&
    ! grep -q 'run failed' "$FIXTURES/calls.log" && ! grep -q 'podman run' "$FIXTURES/calls.log"
}

prs() { # prs <n:author:labels>...
  local a; for a in "$@"; do IFS=: read -r n au l <<<"$a"
    jq -cn --argjson n "$n" --arg a "$au" --arg l "$l" '{number: $n, author: {login: $a}, headRefOid: "sha\($n)",
      headRefName: "issue-1", labels: ($l | split(",") | map(select(. != "") | {name: .})), files: [{path: "src/a.c"}], comments: []}'
  done | jq -s . > "$FIXTURES/prs.json"
}

test_pr_stuck_after_three_review_failures() {
  prs 7:malespiaut:
  finish_pr 7 review-7 1; finish_pr 7 review-7 1
  ! grep -q -- 'pr edit 7.*--add-label stuck' "$FIXTURES/calls.log" || return 1
  [[ -z $(prs_needing_review) ]] || return 1  # cooling down
  finish_pr 7 review-7 1
  grep -q -- 'pr edit 7.*--add-label stuck' "$FIXTURES/calls.log" || return 1
  rm -rf "$STATE/fail"
  [[ -z $(prs_needing_review) ]]  # stuck label still excludes it
}

test_fix_cycle_cap() {
  prs 7:malespiaut:review:changes-requested
  mkdir -p "$STATE/cycles"; echo "$MAX_FIX_CYCLES" > "$STATE/cycles/7"
  dispatch_fix 7
  grep -q -- 'pr edit 7.*--add-label stuck' "$FIXTURES/calls.log" && ! grep -q 'podman run' "$FIXTURES/calls.log"
}

test_fix_usage_limit_keeps_cycle_count() {
  mkrepo
  prs 7:malespiaut:review:changes-requested
  mkdir -p "$STATE/cycles"; echo 2 > "$STATE/cycles/7"; echo fix > "$STATE/roles/developer-fix.md"
  echo 1 > "$FIXTURES/run-exit"; echo 'Claude AI usage limit reached|1700000000' > "$FIXTURES/run-output.txt"
  dispatch_fix 7; wait
  grep -q 'podman run .*--name vibe-fix-7 ' "$FIXTURES/calls.log" && [[ $(<"$STATE/cycles/7") == 2 ]] || return 1
  echo 'plain crash' > "$FIXTURES/run-output.txt"
  dispatch_fix 7; wait
  [[ $(<"$STATE/cycles/7") == 3 ]]
}

test_protected_close_owner_only_and_cached() {
  prs 8:malespiaut: 9:stranger:
  jq 'map(.files = [{path: "agents/Containerfile"}])' "$FIXTURES/prs.json" > "$FIXTURES/p" && mv "$FIXTURES/p" "$FIXTURES/prs.json"
  close_protected
  grep -q 'pr close 8' "$FIXTURES/calls.log" && ! grep -q 'pr close 9' "$FIXTURES/calls.log" || return 1
  prs 10:malespiaut:
  close_protected; close_protected
  [[ $(grep -c 'pr view 10 .*files' "$FIXTURES/calls.log") == 1 ]]
}

# ---- loop_once tests: a real local git origin + clone, fakes for gh and podman ----
mkrepo() {
  local src=$FIXTURES/src r
  git init -q -b main "$src"
  mkdir -p "$src/docs/design" "$src/agents/roles"
  for r in common designer-bootstrap designer-plan developer developer-fix reviewer art-director playtester; do
    echo "role $r" > "$src/agents/roles/$r.md"
  done
  echo x > "$src/docs/design/a.md"
  git -C "$src" add -A && git -C "$src" -c user.name=t -c user.email=t@t commit -qm init && git -C "$src" branch issue-1
  git clone -q --bare "$src" "$FIXTURES/origin.git" && git clone -q "$FIXTURES/origin.git" "$WORK/repo"
  echo '[]' > "$FIXTURES/prs.json"
}
loop() { loop_once; wait; }

test_loop_dev_fails_three_times_stuck() {
  mkrepo
  issue 1 OPEN malespiaut ready | issues
  echo 1 > "$FIXTURES/run-exit"
  loop; loop; loop; loop
  [[ $(grep -c 'podman run .*--name vibe-dev-1 ' "$FIXTURES/calls.log") == 3 ]] || return 1
  [[ $(jq -r '.[0].labels | map(.name) | sort | join(",")' "$FIXTURES/issues.json") == stuck ]]
}

test_loop_review_failure_cools_down() {
  mkrepo
  issues </dev/null
  prs 7:malespiaut:
  echo 1 > "$FIXTURES/run-exit"
  loop; loop
  [[ $(grep -c 'podman run .*--name vibe-review-7 ' "$FIXTURES/calls.log") == 1 ]]
}

test_loop_podman_down_dispatches_nothing() {
  mkrepo
  issue 1 OPEN malespiaut ready | issues
  prs 7:malespiaut:
  echo 1 > "$FIXTURES/ps-exit"
  loop
  ! grep -qE 'podman (run|rm)' "$FIXTURES/calls.log" && ! grep -q 'issue edit' "$FIXTURES/calls.log"
}

for f in $(declare -F | awk '$3 ~ /^test_/ {print $3}'); do t "$f"; done
if ((fails)); then echo "$fails test(s) failed"; exit 1; fi
echo "all tests passed"
