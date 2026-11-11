#!/usr/bin/env bash
# Unit tests for agents/orchestrate.sh. Uses only the fakes in tests/fake.
here=$(cd "$(dirname "$0")" && pwd)
REAL_GIT=$(command -v git); export REAL_GIT
export PATH="$here/fake:$PATH"
fails=0
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

t() { # t <name>: run test function in a subshell with fresh fixtures
  local name=$1
  # orchestrator logs go to stderr; show them only when the test fails
  # every test also checks that no podman run broke the mount contract (see fake/podman)
  if (setup "$name" && "$name" && no_violations) 2>"$root/$name.err"; then echo "ok   $name"
  else echo "FAIL $name"; sed 's/^/    /' "$root/$name.err"; fails=$((fails + 1)); fi
}

setup() {
  local tmp=$root/$1
  mkdir -p "$tmp"
  export FIXTURES=$tmp/fx LOGS=$tmp/logs STATE=$tmp/state
  mkdir -p "$FIXTURES" "$LOGS" "$STATE/roles"
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
no_violations() { [[ ! -s $FIXTURES/violations ]] || { cat "$FIXTURES/violations"; return 1; }; }
leftovers() { find "$STATE/runs" "$STATE/prompts" -mindepth 1 -maxdepth 1 2>/dev/null; }
rundir() { mkdir -p "$STATE/runs"; mktemp -d "$STATE/runs/$1.XXXXXX"; }

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
  run_agent developer dev-5 "$(rundir dev-5)" "issue 5"
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
  run_agent developer dev-5 "$(rundir dev-5)" "issue 5"
  local rc=$?
  finish_dev 5 $rc
  [[ $rc == 75 ]] && ! grep -q 'run failed' "$FIXTURES/calls.log"
}

test_protected_paths_pr_closed() {
  echo '[{"number": 8, "files": [{"path": "src/a.c"}, {"path": "agents/orchestrate.sh"}]},
         {"number": 9, "files": [{"path": "src/a.c"}]}, {"number": 10, "files": [{"path": "agents/install.sh"}]}]' > "$FIXTURES/prs.json"
  protected_pr 8 && ! protected_pr 9 && protected_pr 10
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
  run_agent developer dev-5 "$(rundir dev-5)" "issue 5"
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
  git clone -q --bare "$src" "$FIXTURES/origin.git" && git clone -q --mirror "$FIXTURES/origin.git" "$STATE/mirror"
  echo '[]' > "$FIXTURES/prs.json"
  mkdir -p "$FIXTURES/api"; echo '{"permissions": {"push": true}}' > "$FIXTURES/api/$(printf '%s' "repos/$REPO" | tr -c 'A-Za-z0-9' _).json"
}
loop() { loop_once; wait; }

# The fake agent plants every host-code-execution trick it can reach in its own run dir.
evil_agent() {
  cat > "$FIXTURES/agent.sh" <<EOF
#!/usr/bin/env bash
cd "\$1" || exit 0
printf '#!/bin/sh\ntouch $FIXTURES/pwned\n' > .git/hooks/post-checkout; chmod +x .git/hooks/post-checkout
cp .git/hooks/post-checkout .git/hooks/pre-push
\$REAL_GIT config core.fsmonitor "touch $FIXTURES/pwned"
\$REAL_GIT config core.hooksPath .git/hooks
\$REAL_GIT config remote.origin.url "ext::sh -c 'touch $FIXTURES/pwned'"
ln -s "$FIXTURES/sentinel" escape  # host cleanup must not follow it
EOF
  chmod +x "$FIXTURES/agent.sh"
}

# every host git call inside $STATE/runs must target a dir freshly cloned by the host (never reused)
# and not yet handed to a container
host_git_ok() {
  awk -v work="$STATE/runs/" '
    function under(p) { return index(p "/", work) == 1 }
    /^podman run / { for (d in fresh) if (index($0, d)) delete fresh[d]; next }
    /^git cwd=/ {
      cwd = substr($2, 5); t = cwd; i = 3
      if ($3 == "-C") { t = $4; i = 5 }
      sub_ = $i
      if (sub_ == "clone") { if (under(cwd)) { print "clone from inside a run dir: " $0; bad = 1 }
                             if ($NF in seen) { print "run dir reused: " $0; bad = 1 }
                             if (under($NF)) fresh[$NF] = seen[$NF] = 1; next }
      if (!under(t)) next
      if (!(t in fresh) || (sub_ != "remote" && sub_ != "checkout")) { print "unsafe: " $0; bad = 1 }
    }
    END { exit bad }' "$FIXTURES/calls.log"
}

test_host_git_never_touches_agent_dirs() {
  mkrepo
  issue 1 OPEN malespiaut ready | issues
  prs 7:malespiaut:art
  evil_agent
  mkdir -p "$FIXTURES/sentinel"; echo keep > "$FIXTURES/sentinel/f"
  loop; echo 1 > "$FIXTURES/run-exit"; rm -rf "$STATE/fail"; loop; loop
  grep -q 'podman run .*vibe-dev-1 ' "$FIXTURES/calls.log" && grep -q 'podman run .*vibe-review-7 ' "$FIXTURES/calls.log" || return 1
  [[ ! -e $FIXTURES/pwned ]] || { echo "host executed agent-planted code"; return 1; }
  [[ $(cat "$FIXTURES/sentinel/f") == keep ]] || { echo "cleanup followed a planted symlink"; return 1; }
  host_git_ok
}

test_run_dirs_removed_whatever_the_outcome() {
  mkrepo
  issue 1 OPEN malespiaut ready | issues
  prs 7:malespiaut:
  loop
  [[ -z $(leftovers) ]] || { echo "left after success: $(leftovers)"; return 1; }
  echo 1 > "$FIXTURES/run-exit"; rm -rf "$STATE/fail" "$STATE/reviewed"
  loop
  [[ -z $(leftovers) ]] || { echo "left after failure: $(leftovers)"; return 1; }
  ! grep -q 'worktree' "$FIXTURES/calls.log"
}

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

# two dev agents run at once; each tries to reach every other run dir
test_concurrent_agents_cannot_reach_each_other() {
  mkrepo
  { issue 1 OPEN malespiaut ready; issue 2 OPEN malespiaut ready; } | issues
  cat > "$FIXTURES/agent.sh" <<EOF
#!/usr/bin/env bash
sleep 0.5
for o in "$STATE"/runs/*; do
  [[ \$o == "\$1" ]] && continue
  echo "\$o" >> "$FIXTURES/seen"
  case " \$MOUNTED " in *" \$o "*) echo "reachable: \$o" >> "$FIXTURES/leak"; touch "\$o/intruder" ;; esac
done
EOF
  chmod +x "$FIXTURES/agent.sh"
  MAX_DEVS=2 loop
  [[ $(grep -c 'podman run ' "$FIXTURES/calls.log") -ge 2 ]] || return 1
  [[ -s $FIXTURES/seen ]] || { echo "agents never ran concurrently"; return 1; }
  [[ ! -e $FIXTURES/leak ]] || { cat "$FIXTURES/leak"; return 1; }
  host_git_ok
}

test_prompt_files_unique_private_removed() {
  issue 5 OPEN malespiaut ready,in-progress | issues
  run_agent developer dev-5 "$(rundir dev-5)" "issue 5"
  grep -q 'issue 5' "$FIXTURES/last-prompt" && grep -q common "$FIXTURES/last-prompt" || return 1
  run_agent developer dev-5 "$(rundir dev-5)" "issue 5"
  local p
  p=$(grep -oE -- '-v [^ ]+:/prompt.md:ro' "$FIXTURES/calls.log" | sed 's/^-v //; s/:\/prompt.md:ro$//')
  [[ $(sort -u <<<"$p" | grep -c "^$STATE/prompts/dev-5-") == 2 ]] || { echo "prompts: $p"; return 1; }
  [[ -z $(ls -A "$STATE/prompts") ]]
}

test_symlinked_roles_rejected() {
  mkrepo
  local src=$FIXTURES/src
  sync_roles; [[ $(<"$STATE/roles/common.md") == "role common" ]] || { echo "clean sync failed"; return 1; }
  echo common > "$STATE/roles/common.md"
  # a symlinked file inside roles
  ln -sf /etc/passwd "$src/agents/roles/reviewer.md"
  git -C "$src" add -A && git -C "$src" -c user.name=t -c user.email=t@t commit -qm evil1 &&
    git -C "$src" push -q "$FIXTURES/origin.git" main && mirror fetch -q --prune origin || return 1
  sync_roles
  [[ $(<"$STATE/roles/common.md") == common && ! -L $STATE/roles/reviewer.md ]] || { echo "file symlink accepted"; return 1; }
  # roles itself a symlink
  git -C "$src" rm -rq agents/roles && mkdir -p "$src/real" "$src/agents" && echo "role common" > "$src/real/common.md" &&
    ln -s ../real "$src/agents/roles" && git -C "$src" add -A &&
    git -C "$src" -c user.name=t -c user.email=t@t commit -qm evil2 &&
    git -C "$src" push -q "$FIXTURES/origin.git" main && mirror fetch -q --prune origin || return 1
  sync_roles
  [[ $(<"$STATE/roles/common.md") == common && ! -L $STATE/roles ]]
}

test_orphans_cleaned_at_startup() {
  declare -f main_loop | grep -q clean_orphans || { echo "main_loop does not call clean_orphans"; return 1; }
  printf 'vibe-dev-2\nvibe-art-7\n' > "$FIXTURES/ps.txt"
  mkdir -p "$STATE/runs" "$STATE/prompts" "$FIXTURES/sentinel"; echo keep > "$FIXTURES/sentinel/f"
  mkdir "$STATE/runs/dev-1.aaaaaa" "$STATE/runs/dev-2.bbbbbb" "$STATE/runs/review-7.cccccc"
  ln -s "$FIXTURES/sentinel" "$STATE/runs/dev-1.aaaaaa/escape"
  touch "$STATE/prompts/dev-1-20260101T000000.dddddd.md" "$STATE/prompts/dev-2-20260101T000000.eeeeee.md"
  clean_orphans
  [[ ! -e $STATE/runs/dev-1.aaaaaa && ! -e $STATE/prompts/dev-1-20260101T000000.dddddd.md ]] || { echo "orphan kept"; return 1; }
  [[ -d $STATE/runs/dev-2.bbbbbb && -d $STATE/runs/review-7.cccccc && -e $STATE/prompts/dev-2-20260101T000000.eeeeee.md ]] ||
    { echo "live run removed"; return 1; }
  [[ $(cat "$FIXTURES/sentinel/f") == keep ]]
}

acceptance_fixture() { # acceptance_fixture <author> <hours since last playtest start>
  mkrepo
  issue 50 OPEN "$1" "" | jq '.title = "Final acceptance"' | issues
  echo $(($(date +%s) - $2 * 3600)) > "$STATE/started-playtester"
}
playtested() { grep -q 'podman run .*--name vibe-playtester ' "$FIXTURES/calls.log"; }

test_acceptance_retried_after_2h() {
  acceptance_fixture malespiaut 3
  loop
  playtested && (($(<"$STATE/started-playtester") > $(date +%s) - 60))
}

test_acceptance_not_retried_within_2h() {
  acceptance_fixture malespiaut 1
  loop
  ! playtested
}

test_acceptance_ignores_stranger() {
  acceptance_fixture stranger 3
  loop
  ! playtested
}

# ---- final fix wave ----
test_timeout_kills_container() {
  issue 5 OPEN malespiaut ready,in-progress | issues
  echo 124 > "$FIXTURES/run-exit"
  echo '{"type":"result","is_error":true,"result":"rate limit"}' > "$FIXTURES/run-output.txt"
  run_agent developer dev-5 "$(rundir dev-5)" "issue 5"
  local rc=$?
  [[ $rc == 124 ]] && grep -q '^podman kill vibe-dev-5$' "$FIXTURES/calls.log" &&
    grep -q 'podman run .*--timeout 5520 ' "$FIXTURES/calls.log"
}

test_usage_limit_only_result_lines() {
  issue 5 OPEN malespiaut ready,in-progress | issues
  echo 1 > "$FIXTURES/run-exit"
  jq -cn '{type: "user", message: {content: [{type: "tool_result", content: "API rate limit exceeded, usage limit reached"}]}}' > "$FIXTURES/run-output.txt"
  run_agent developer dev-5 "$(rundir dev-5)" "issue 5"
  [[ $? == 1 ]]
}

test_auth_error_holds_without_counting() {
  issue 5 OPEN malespiaut ready,in-progress | issues
  echo 1 > "$FIXTURES/run-exit"
  echo 'API Error: 401 {"type":"error","error":{"type":"authentication_error","message":"OAuth token has expired"}}' > "$FIXTURES/run-output.txt"
  run_agent developer dev-5 "$(rundir dev-5)" "issue 5"
  local rc=$?
  finish_dev 5 $rc
  [[ $rc == "$RC_INFRA" ]] && (($(<"$STATE/backoff-until") > $(date +%s) + 1700)) && ! grep -q 'run failed' "$FIXTURES/calls.log"
}

test_gh_token_preflight_cached() {
  mkrepo
  issue 1 OPEN malespiaut ready | issues
  echo '{"permissions": {"push": false}}' > "$FIXTURES/api/$(printf '%s' "repos/$REPO" | tr -c 'A-Za-z0-9' _).json"
  loop; loop
  ! grep -q 'podman run' "$FIXTURES/calls.log" && [[ $(grep -c "^api repos/$REPO " "$FIXTURES/calls.log") == 1 ]]
}

designer_ran() { grep -q 'podman run .*--name vibe-designer ' "$FIXTURES/calls.log"; }
test_planning_only_when_something_changed() {
  mkrepo
  issues </dev/null
  mirror rev-parse main > "$STATE/planned-sha"
  loop
  ! designer_ran || { echo "planned with nothing changed"; return 1; }
  issue 3 OPEN malespiaut stuck | issues
  loop
  designer_ran || { echo "stuck issue did not trigger planning"; return 1; }
  [[ $(<"$STATE/planned-sha") == "$(mirror rev-parse main)" ]] || return 1
  : > "$FIXTURES/calls.log"; issues </dev/null; echo oldsha > "$STATE/planned-sha"; rm "$STATE/designer-at"
  loop
  designer_ran
}

# A: a failed planning run records no sha, so planning runs again once cooldowns pass
test_failed_planning_retried() {
  mkrepo
  issues </dev/null
  echo 1 > "$FIXTURES/run-exit"
  loop
  designer_ran && [[ ! -e $STATE/planned-sha ]] || { echo "failed run recorded a sha"; return 1; }
  : > "$FIXTURES/calls.log"; rm -rf "$STATE/fail"  # failure cooldown elapsed
  DESIGNER_COOLDOWN=0 loop
  designer_ran
}

# B: a successful planning run records main; with main unchanged and nothing waiting, skip
test_successful_planning_then_skipped() {
  mkrepo
  issues </dev/null
  loop
  designer_ran && [[ $(<"$STATE/planned-sha") == "$(mirror rev-parse main)" ]] || return 1
  : > "$FIXTURES/calls.log"
  DESIGNER_COOLDOWN=0 loop
  ! designer_ran
}

test_auth_401_anchored() {
  local f=$LOGS/a.jsonl m
  for m in "Merged PR #401" '{"type":"result","is_error":true,"usage":{"output_tokens":401}}' "took 0.401 s"; do
    printf '%s\n' "$m" > "$f"
    ! is_auth_error "$f" || { echo "false auth hit: $m"; return 1; }
  done
  echo 'API Error: 401 {"type":"error"}' > "$f"
  is_auth_error "$f"
}

test_review_without_verdict_counts_failure() {
  mkrepo; sync_roles
  prs 7:malespiaut:
  dispatch_review 7; wait
  [[ ! -e $STATE/reviewed/7 && -e $STATE/fail/review-7 ]] || { echo "no-verdict review accepted"; return 1; }
  jq 'map(.state = "MERGED")' "$FIXTURES/prs.json" > "$FIXTURES/p" && mv "$FIXTURES/p" "$FIXTURES/prs.json"
  dispatch_review 7; wait
  [[ $(<"$STATE/reviewed/7") == sha7 && ! -e $STATE/fail/review-7 ]]
}

test_dev_exit0_without_pr_releases() {
  issue 5 OPEN malespiaut in-progress | issues
  prs 9:malespiaut:   # from issue-1, not issue-5
  finish_dev 5 0
  grep -q 'run failed: no PR opened' "$FIXTURES/calls.log" || return 1
  : > "$FIXTURES/calls.log"
  jq 'map(.headRefName = "issue-5")' "$FIXTURES/prs.json" > "$FIXTURES/p" && mv "$FIXTURES/p" "$FIXTURES/prs.json"
  finish_dev 5 0
  ! grep -q 'run failed' "$FIXTURES/calls.log" || return 1
  echo '[]' > "$FIXTURES/prs.json"; issue 5 OPEN malespiaut blocked | issues
  finish_dev 5 0
  ! grep -q 'run failed' "$FIXTURES/calls.log"
}

test_protected_close_rereadies_issue() {
  prs 8:malespiaut:
  jq 'map(.files = [{path: "agents/entrypoint.sh"}] | .body = "Does things.\nCloses #12")' "$FIXTURES/prs.json" > "$FIXTURES/p" &&
    mv "$FIXTURES/p" "$FIXTURES/prs.json"
  close_protected
  grep -q 'pr close 8' "$FIXTURES/calls.log" && grep -q 'issue edit 12 .*--add-label ready --remove-label in-progress' "$FIXTURES/calls.log"
}

test_review_oldest_first_and_concurrency() {
  mkrepo
  prs 9:malespiaut: 7:malespiaut: 8:malespiaut:
  MAX_REVIEWS=1 loop
  grep -q 'podman run .*--name vibe-review-7 ' "$FIXTURES/calls.log" && ! grep -qE 'vibe-review-(8|9) ' "$FIXTURES/calls.log" || return 1
  : > "$FIXTURES/calls.log"; rm -rf "$STATE/reviewed" "$STATE/fail"; echo vibe-art-7 > "$FIXTURES/ps.txt"
  loop  # MAX_REVIEWS=2, one pipeline (art-7) running: only the next oldest, 8
  grep -q 'podman run .*--name vibe-review-8 ' "$FIXTURES/calls.log" && ! grep -qE 'vibe-review-(7|9) ' "$FIXTURES/calls.log"
}

test_run_lock() {
  exec 8>"$STATE/lock"; flock 8
  (ORCH_ONCE=1 main_loop); local rc=$?
  [[ $rc == 1 ]] && grep -q 'another orchestrator is running' "$LOGS/orchestrator.log"
}

test_episode_done_several_acceptance_issues() {
  { issue 98 CLOSED malespiaut ""; issue 99 OPEN malespiaut ""; } | jq '.title = "Final acceptance"' | issues
  mkdir -p "$FIXTURES/api"
  echo '[{"title": "Episode 1", "state": "closed"}]' > "$FIXTURES/api/$(printf '%s' "repos/$REPO/milestones?state=all" | tr -c 'A-Za-z0-9' _).json"
  ! episode_done || return 1
  { issue 98 CLOSED malespiaut ""; issue 99 CLOSED malespiaut ""; } | jq '.title = "Final acceptance"' | issues
  episode_done
}

test_ready_excludes_status_issues() {
  { issue 1 OPEN malespiaut ready; issue 2 OPEN malespiaut ready | jq '.title = "Team status"'
    issue 3 OPEN malespiaut ready | jq '.title = "Final acceptance"'; } | issues
  [[ $(ready_issues) == 1 ]]
}

test_playtest_counter_reinit_on_rewrite() {
  mkrepo
  local head; head=$(mirror rev-parse main)
  echo 0123456789abcdef0123456789abcdef01234567 > "$STATE/last-playtest-sha"
  [[ $(playtest_merges "$head") == "$PLAYTEST_EVERY" && $(<"$STATE/last-playtest-sha") == "$head" ]] || return 1
  [[ $(playtest_merges "$head") == 0 ]]
}

test_host_git_ignores_global_config() {
  mkrepo
  printf '[core]\n\tfsmonitor = touch %s\n' "$FIXTURES/pwned" > "$FIXTURES/gitconfig"
  local d
  d=$(GIT_CONFIG_GLOBAL=$FIXTURES/gitconfig mkrun dev-1 --detach origin/main) || return 1
  [[ -d $d/.git && ! -e $FIXTURES/pwned ]]
}

for f in $(declare -F | awk '$3 ~ /^test_/ {print $3}'); do t "$f"; done
if ((fails)); then echo "$fails test(s) failed"; exit 1; fi
echo "all tests passed"
