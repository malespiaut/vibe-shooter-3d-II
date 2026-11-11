#!/usr/bin/env bash
# Host-side loop that runs the agent team. See docs/superpowers/specs/*-autonomous-agent-team-design.md.
# ORCH_LIB=1: define functions only. ORCH_ONCE=1: one iteration, then wait for children.

: "${WORK:=$HOME/vibe-agents/work}" "${LOGS:=$HOME/vibe-agents/logs}" "${STATE:=$HOME/vibe-agents/state}"
: "${REPO:=malespiaut/vibe-shooter-3d-II}" "${OWNER:=malespiaut}" "${IMAGE:=vibe-agents}"
: "${MAX_DEVS:=4}" "${AGENT_TIMEOUT:=90m}" "${POLL:=60}" "${PLAYTEST_EVERY:=5}" "${STALE_SECS:=7200}"
: "${DESIGNER_COOLDOWN:=1800}"
: "${GH_TOKEN_FILE:=$HOME/vibe-agents/gh-token}" "${CLAUDE_TOKEN_FILE:=$HOME/vibe-agents/claude-token}"

# stderr, so functions whose stdout is data stay clean
log() { mkdir -p "$LOGS"; printf '%s %s\n' "$(date -Is)" "$*" | tee -a "$LOGS/orchestrator.log" >&2; }
git_repo() { git -C "$WORK/repo" "$@"; }
running_names() { podman ps --filter label=vibe=1 --format '{{.Names}}'; }
running() { running_names | grep -qx -- "$1"; }

# ---- GitHub queries ----

ready_issues() {
  gh issue list --repo "$REPO" --state open --label ready --limit 500 --json number,state,author,labels \
    --jq '.[] | select(.state == "OPEN" and .author.login == "'"$OWNER"'"
      and any(.labels[]; .name == "ready")
      and all(.labels[]; .name != "in-progress" and .name != "stuck" and .name != "blocked")) | .number' |
    sort -n | while read -r n; do deps_closed "$n" && echo "$n"; done
}

deps_closed() {
  local body d
  body=$(gh issue view "$1" --repo "$REPO" --json body --jq .body) || return 1
  while read -r d; do
    [[ -z $d || $(gh issue view "$d" --repo "$REPO" --json state --jq .state) == CLOSED ]] || return 1
  done < <(grep -oE 'Depends on #[0-9]+' <<<"$body" | grep -oE '[0-9]+')
}

claim() { gh issue edit "$1" --repo "$REPO" --add-label in-progress >/dev/null; }

release() {
  local n=$1 prior
  prior=$(gh issue view "$n" --repo "$REPO" --json comments --jq '[.comments[] | select(.author.login == "'"$OWNER"'"
    and (.body | startswith("[orchestrator] run failed")))] | length')
  gh issue comment "$n" --repo "$REPO" --body "[orchestrator] run failed: $2" >/dev/null
  gh issue edit "$n" --repo "$REPO" --remove-label in-progress >/dev/null
  log "released #$n: $2"
  # count prior+1: the comment just posted may not be listed yet
  if (( ${prior:-0} + 1 >= 3 )); then
    gh issue edit "$n" --repo "$REPO" --add-label stuck --remove-label ready >/dev/null
    log "#$n marked stuck"
  fi
}

# "<number> <headRefOid> <headRefName>" of open PRs by OWNER, optionally only those with label $1
owner_prs() {
  gh pr list --repo "$REPO" --state open --limit 200 ${1:+--label "$1"} \
    --json number,author,headRefOid,headRefName,labels \
    --jq '.[] | select(.author.login == "'"$OWNER"'" and ("'"${1:-}"'" == "" or any(.labels[]; .name == "'"${1:-}"'")))
      | "\(.number) \(.headRefOid) \(.headRefName)"'
}

prs_needing_review() {
  local n sha br
  owner_prs | while read -r n sha br; do
    [[ $(cat "$STATE/reviewed/$n" 2>/dev/null) == "$sha" ]] && continue
    running "vibe-fix-$n" && continue                                  # fixer still pushing
    [[ $br == issue-* ]] && running "vibe-dev-${br#issue-}" && continue  # dev still pushing
    echo "$n"
  done
}

prs_needing_fix() {
  local n sha br
  owner_prs review:changes-requested | while read -r n sha br; do
    running "vibe-fix-$n" || running "vibe-review-$n" || running "vibe-art-$n" && continue
    [[ $(cat "$STATE/reviewed/$n" 2>/dev/null) == "$sha" ]] || continue  # label refers to an older head
    [[ $(cat "$STATE/fixed/$n" 2>/dev/null) == "$sha" ]] && continue     # already tried this head
    echo "$n"
  done
}

protected_pr() {
  gh pr view "$1" --repo "$REPO" --json files --jq '.files[].path' |
    grep -qxE 'agents/(orchestrate\.sh|Containerfile)'
}

# tail only: a long transcript can mention "rate limit" in game code without being limited
is_usage_limit() { tail -n 20 "$1" 2>/dev/null | grep -qiE 'usage limit|rate.?limit|429|overloaded'; }

episode_done() {
  [[ $(gh issue list --repo "$REPO" --state all --search '"Final acceptance" in:title' --json title,state \
        --jq '.[] | select(.title == "Final acceptance") | .state') == CLOSED ]] &&
  [[ $(gh api "repos/$REPO/milestones?state=all" --jq '.[] | select(.title == "Episode 1") | .state') == closed ]]
}

# ---- running agents ----

run_agent() {
  local role=$1 name=$2 wd=$3 ctx=$4 prompt=$WORK/.prompts/$2.md logf rc key=()
  if running "vibe-$name"; then log "vibe-$name already running"; return 1; fi
  [[ -r $GH_TOKEN_FILE && -r $CLAUDE_TOKEN_FILE ]] || { log "token files missing"; return 1; }
  mkdir -p "$WORK/.prompts" "$LOGS"
  cat "$STATE/roles/common.md" "$STATE/roles/$role.md" > "$prompt" || { log "missing role $role"; return 1; }
  printf '\n%s\n' "$ctx" >> "$prompt"
  logf=$LOGS/$name-$(date +%Y%m%dT%H%M%S).jsonl
  [[ -n ${ANTHROPIC_API_KEY:-} ]] && key=(-e ANTHROPIC_API_KEY)
  podman rm -f "vibe-$name" >/dev/null 2>&1  # a dead leftover would block --name
  log "start vibe-$name ($role) in $wd, log $logf"
  GH_TOKEN=$(<"$GH_TOKEN_FILE") CLAUDE_CODE_OAUTH_TOKEN=$(<"$CLAUDE_TOKEN_FILE") \
  GIT_AUTHOR_NAME=$(git config user.name) GIT_AUTHOR_EMAIL=$(git config user.email) \
    timeout -k 60 "$AGENT_TIMEOUT" podman run --rm --name "vibe-$name" --label vibe=1 --userns=keep-id \
      -v "$WORK:$WORK" -e HOME=/tmp/agent-home -e GH_TOKEN -e CLAUDE_CODE_OAUTH_TOKEN "${key[@]}" \
      -e GIT_AUTHOR_NAME -e GIT_AUTHOR_EMAIL "$IMAGE" "$wd" "$prompt" </dev/null >>"$logf" 2>&1
  rc=$?
  ((rc != 0)) && is_usage_limit "$logf" && rc=75
  log "end vibe-$name rc=$rc"
  return "$rc"
}

backoff() { # usage limit: hold all launches; 5 min doubling to 4 h
  local b
  b=$(cat "$STATE/backoff" 2>/dev/null || echo 300)
  echo $(($(date +%s) + b)) > "$STATE/backoff-until"
  echo $((b * 2 > 14400 ? 14400 : b * 2)) > "$STATE/backoff"
  log "usage limit: backing off ${b}s"
}

finish_run() { case $1 in 0) rm -f "$STATE/backoff" ;; 75) backoff ;; esac; }

finish_dev() {
  local n=$1 rc=$2
  finish_run "$rc"
  case $rc in
    0) ;;
    75) gh issue edit "$n" --repo "$REPO" --remove-label in-progress >/dev/null ;;
    *) release "$n" "exit $rc" ;;
  esac
}

wt_remove() { git_repo worktree remove --force "$1" >/dev/null 2>&1; rm -rf "$1"; git_repo worktree prune; }
wt_fresh() { wt_remove "$1"; git_repo worktree add -f --detach "$1" "$2" >/dev/null 2>&1; }
pr_wt() { git_repo fetch -q origin "+refs/heads/$2:refs/remotes/origin/$2" && wt_fresh "$1" "origin/$2"; }
pr_head() { gh pr view "$1" --repo "$REPO" --json headRefOid,headRefName,labels \
  --jq '"\(.headRefOid) \(.headRefName) \([.labels[].name] | join(","))"'; }

dispatch_dev() {
  local n=$1 wt=$WORK/wt/issue-$1
  claim "$n" || return
  wt_remove "$wt"
  if ! git_repo worktree add -f -B "issue-$n" "$wt" origin/main >/dev/null 2>&1; then
    release "$n" "worktree setup failed"; return
  fi
  (
    run_agent developer "dev-$n" "$wt" "Your issue is #$n in $REPO. You are on branch issue-$n.
Open a PR from issue-$n that closes #$n."
    rc=$?; wt_remove "$wt"; finish_dev "$n" "$rc"
  ) &
}

dispatch_fix() {
  local n=$1 wt=$WORK/wt/pr-$1 sha br labels
  read -r sha br labels < <(pr_head "$n")
  if [[ -z $br ]] || ! pr_wt "$wt" "$br"; then log "fix #$n: checkout failed"; return; fi
  (
    run_agent developer-fix "fix-$n" "$wt" "Your PR is #$n in $REPO, branch $br (detached checkout).
Push with: git push origin HEAD:$br"
    rc=$?; wt_remove "$wt"
    ((rc != 75)) && { mkdir -p "$STATE/fixed"; echo "$sha" > "$STATE/fixed/$n"; }
    finish_run "$rc"
  ) &
}

dispatch_review() {
  local n=$1 wt=$WORK/wt/pr-$1 sha br labels art=
  read -r sha br labels < <(pr_head "$n")
  if [[ -z $br ]] || ! pr_wt "$wt" "$br"; then log "review #$n: checkout failed"; return; fi
  [[ ,$labels, == *,art,* || ,$labels, == *,render,* ]] && art=1
  (
    ctx="Your PR is #$n in $REPO, head $sha, branch $br (detached checkout). Push with: git push origin HEAD:$br"
    if [[ $art ]]; then
      run_agent art-director "art-$n" "$wt" "$ctx"
      (($? == 75)) && { backoff; wt_remove "$wt"; exit; }
      ctx+=$'\nThis PR is labelled art or render: merge it only if it has the label art:approved.'
    fi
    run_agent reviewer "review-$n" "$wt" "$ctx"
    rc=$?; wt_remove "$wt"; finish_run "$rc"
    ((rc == 0)) && { mkdir -p "$STATE/reviewed"; echo "$sha" > "$STATE/reviewed/$n"; }
  ) &
}

# designer / playtester: fresh detached origin/main worktree, never $WORK/repo
dispatch_main() { # dispatch_main <role> <name> <context> [on-success-cmd...]
  local role=$1 name=$2 ctx=$3 wt=$WORK/wt/$2
  shift 3
  wt_fresh "$wt" origin/main || { log "$name: worktree failed"; return; }
  ( run_agent "$role" "$name" "$wt" "$ctx"; rc=$?; finish_run "$rc"; ((rc == 0)) && "$@" ) &
}

mark_playtested() { echo "$1" > "$STATE/last-playtest-sha"; }

sync_roles() {
  local tmp
  tmp=$(mktemp -d) || return
  if git_repo archive origin/main agents/roles 2>/dev/null | tar -x -C "$tmp" 2>/dev/null &&
     [[ -f $tmp/agents/roles/common.md ]]; then
    rm -rf "$STATE/roles.old"; [[ -d $STATE/roles ]] && mv "$STATE/roles" "$STATE/roles.old"
    mv "$tmp/agents/roles" "$STATE/roles"
  fi
  rm -rf "$tmp"
}

reap_stale() { # in-progress, no container, no open PR, untouched for STALE_SECS
  local cutoff n br
  cutoff=$(date -u -d "@$(($(date +%s) - STALE_SECS))" +%Y-%m-%dT%H:%M:%SZ)
  local open_brs
  open_brs=$(owner_prs | awk '{print $3}')
  gh issue list --repo "$REPO" --state open --label in-progress --limit 200 --json number,author,updatedAt \
    --jq '.[] | select(.author.login == "'"$OWNER"'" and .updatedAt < "'"$cutoff"'") | .number' |
  while read -r n; do
    running "vibe-dev-$n" && continue
    grep -qx "issue-$n" <<<"$open_brs" && continue
    gh issue edit "$n" --repo "$REPO" --remove-label in-progress >/dev/null && log "reaped stale claim #$n"
  done
}

# ---- main loop ----

loop_once() {
  local now hold names devs n merges last head
  now=$(date +%s); hold=$(cat "$STATE/backoff-until" 2>/dev/null || echo 0)
  if ((hold > now)); then log "backoff: sleeping $((hold - now))s"; sleep $((hold - now)); fi

  git_repo fetch -q origin || { log "fetch failed"; return; }
  sync_roles
  names=$(running_names)

  if [[ -z $(git_repo ls-tree origin/main docs/design) ]]; then
    grep -qx vibe-designer <<<"$names" || dispatch_main designer-bootstrap designer "Mode: bootstrap."
    return
  fi

  for n in $(gh pr list --repo "$REPO" --state open --limit 200 --json number --jq '.[].number'); do
    protected_pr "$n" && gh pr close "$n" --repo "$REPO" --comment "[orchestrator] closed: touches protected path" >/dev/null &&
      log "closed protected PR #$n"
  done

  devs=$(grep -cE '^vibe-(dev|fix)-' <<<"$names")
  for n in $(prs_needing_fix); do
    ((devs < MAX_DEVS)) || break
    dispatch_fix "$n"; devs=$((devs + 1))
  done
  for n in $(ready_issues); do
    ((devs < MAX_DEVS)) || break
    grep -qx "vibe-dev-$n" <<<"$names" && continue
    dispatch_dev "$n"; devs=$((devs + 1))
  done

  if ! grep -qE '^vibe-(review|art)-' <<<"$names"; then
    n=$(prs_needing_review | head -n1)
    [[ -n $n ]] && dispatch_review "$n"
  fi

  head=$(git_repo rev-parse origin/main)
  [[ -s $STATE/last-playtest-sha ]] || echo "$head" > "$STATE/last-playtest-sha"
  last=$(<"$STATE/last-playtest-sha")
  merges=$(git_repo rev-list --count "$last..origin/main" 2>/dev/null || echo 0)
  # also playtest when the team is idle, so the last few merges still get an acceptance pass
  if ! grep -qx vibe-playtester <<<"$names" &&
     { ((merges >= PLAYTEST_EVERY)) || { ((merges > 0)) && [[ -z $names ]]; }; }; then
    dispatch_main playtester playtester "Playtest main at $head." mark_playtested "$head"
  fi

  last=$(cat "$STATE/designer-at" 2>/dev/null || echo 0)
  if (($(ready_issues | wc -l) < 2 * MAX_DEVS)) && ! grep -qx vibe-designer <<<"$names" &&
     ((now - last >= DESIGNER_COOLDOWN)); then
    echo "$now" > "$STATE/designer-at"
    dispatch_main designer-plan designer "Mode: planning. The ready queue is low."
  fi

  reap_stale

  if episode_done; then log "episode done"; wait; exit 0; fi
}

main_loop() {
  mkdir -p "$WORK/wt" "$LOGS" "$STATE/reviewed" "$STATE/fixed"
  log "orchestrator start (MAX_DEVS=$MAX_DEVS)"
  while :; do
    loop_once
    [[ -n ${ORCH_ONCE:-} ]] && break
    sleep "$POLL"
  done
  wait
}

[[ -n ${ORCH_LIB:-} ]] || main_loop
