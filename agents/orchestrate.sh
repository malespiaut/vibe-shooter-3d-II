#!/usr/bin/env bash
# Host-side loop that runs the agent team. See docs/superpowers/specs/*-autonomous-agent-team-design.md.
# ORCH_LIB=1: define functions only. ORCH_ONCE=1: one iteration, then wait for children.

: "${LOGS:=$HOME/vibe-agents/logs}" "${STATE:=$HOME/vibe-agents/state}"
: "${REPO:=malespiaut/vibe-shooter-3d-II}" "${OWNER:=malespiaut}" "${IMAGE:=vibe-agents}"
: "${MAX_DEVS:=4}" "${AGENT_TIMEOUT:=90m}" "${POLL:=60}" "${PLAYTEST_EVERY:=5}" "${STALE_SECS:=7200}"
: "${DESIGNER_COOLDOWN:=1800}" "${MAX_FIX_CYCLES:=5}" "${ACCEPT_EVERY:=7200}" "${MAX_REVIEWS:=2}"
: "${AGENT_MEMORY:=12g}" "${AGENT_PIDS:=4096}" "${AGENT_CPUS:=8}" "${AUTH_HOLD:=1800}" "${GH_CHECK_EVERY:=600}"
RC_INFRA=69  # setup/infrastructure error: not the issue's fault, never counted as a failure
: "${MIRROR:=$STATE/mirror}"  # host-private: never mounted, so agents can't plant hooks or config in it
RUNS=$STATE/runs PROMPTS=$STATE/prompts
: "${GH_TOKEN_FILE:=$HOME/vibe-agents/gh-token}" "${CLAUDE_TOKEN_FILE:=$HOME/vibe-agents/claude-token}"

# stderr, so functions whose stdout is data stay clean
log() { mkdir -p "$LOGS"; printf '%s %s\n' "$(date -Is)" "$*" | tee -a "$LOGS/orchestrator.log" >&2; }
# Security: $STATE is never mounted. Each container mounts only its own run dir ($RUNS/<name>.XXXXXX,
# rw) and its own prompt file (ro). The host runs git in a run dir only in mkrun, before any container
# sees it; after the container exits it only does rm -rf -- on it.
# host git ignores global/system config: nothing outside the repo (helpers, hooksPath, aliases) applies
hgit() { GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 git "$@"; }
mirror() { hgit -C "$MIRROR" "$@"; }
running_names() { podman ps --filter label=vibe=1 --format '{{.Names}}'; }
# podman down: answer "running" so nothing is started or cleaned up on top of a live container
running() { local n; n=$(running_names) || return 0; grep -qx -- "$1" <<<"$n"; }

# ---- GitHub queries ----

open_issues() { gh issue list --repo "$REPO" --state open --limit 1000 --json number,title,state,author,labels,body; }

# ready_issues [open-issues-json]: one API call (or none, given the list); open issues carry both
# candidates and the set of open dependencies
ready_issues() {
  local all=${1:-} open n body
  [[ -n $all ]] || all=$(open_issues) || return
  open=$(jq -r '.[] | select(.state == "OPEN") | .number' <<<"$all")
  jq -r '.[] | select(.state == "OPEN" and .author.login == "'"$OWNER"'"
      and .title != "Team status" and .title != "Final acceptance"
      and any(.labels[]; .name == "ready")
      and all(.labels[]; .name != "in-progress" and .name != "stuck" and .name != "blocked")) | .number' <<<"$all" |
    sort -n | while read -r n; do
      body=$(jq -r --argjson n "$n" '.[] | select(.number == $n) | .body' <<<"$all")
      deps_ok "$body" "$open" && echo "$n"
    done
}

dep_numbers() { grep -iE 'depends on' <<<"$1" | grep -oE '#[0-9]+' | tr -d '#'; }
# deps_ok <body> <open-numbers>: a dependency absent from the open list counts as closed
deps_ok() { local d; for d in $(dep_numbers "$1"); do grep -qx "$d" <<<"$2" && return 1; done; return 0; }

deps_closed() {
  local body
  body=$(gh issue view "$1" --repo "$REPO" --json body --jq .body) || return 1
  deps_ok "$body" "$(gh issue list --repo "$REPO" --state open --limit 1000 --json number,state \
    --jq '.[] | select(.state == "OPEN") | .number')"
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

# "<number> <headRefOid> <headRefName> <labels,csv>" of open PRs by OWNER, optionally only those with label $1
owner_prs() {
  gh pr list --repo "$REPO" --state open --limit 200 ${1:+--label "$1"} \
    --json number,author,headRefOid,headRefName,labels \
    --jq '.[] | select(.author.login == "'"$OWNER"'" and ("'"${1:-}"'" == "" or any(.labels[]; .name == "'"${1:-}"'")))
      | "\(.number) \(.headRefOid) \(.headRefName) ,\([.labels[].name] | join(",")),"'
}

prs_needing_review() {
  local n sha br labels
  owner_prs | while read -r n sha br labels; do
    [[ $labels == *,stuck,* ]] && continue
    [[ $(cat "$STATE/reviewed/$n" 2>/dev/null) == "$sha" ]] && continue
    cooling "review-$n" && continue
    running "vibe-review-$n" || running "vibe-art-$n" && continue
    running "vibe-fix-$n" && continue                                  # fixer still pushing
    [[ $br == issue-* ]] && running "vibe-dev-${br#issue-}" && continue  # dev still pushing
    echo "$n"
  done
}

prs_needing_fix() {
  local n sha br labels
  owner_prs review:changes-requested | while read -r n sha br labels; do
    [[ $labels == *,stuck,* ]] && continue
    running "vibe-fix-$n" || running "vibe-review-$n" || running "vibe-art-$n" && continue
    [[ $(cat "$STATE/reviewed/$n" 2>/dev/null) == "$sha" ]] || continue  # label refers to an older head
    [[ $(cat "$STATE/fixed/$n" 2>/dev/null) == "$sha" ]] && continue     # already tried this head
    cooling "fix-$n" && continue
    echo "$n"
  done
}

close_protected() { # OWNER's PRs only; a clean result is cached per head SHA
  local n sha
  mkdir -p "$STATE/protected"
  owner_prs | while read -r n sha _; do
    [[ $(cat "$STATE/protected/$n" 2>/dev/null) == "$sha" ]] && continue
    if protected_pr "$n"; then
      gh pr close "$n" --repo "$REPO" --comment "[orchestrator] closed: touches protected path" >/dev/null &&
        log "closed protected PR #$n" && reready_closed_issues "$n"
    else
      echo "$sha" > "$STATE/protected/$n"
    fi
  done
}

# a PR closed unmerged would leave its issue with no state label: hand it back to the queue
reready_closed_issues() {
  local k
  for k in $(gh pr view "$1" --repo "$REPO" --json body --jq .body | grep -ioE 'closes #[0-9]+' | grep -oE '[0-9]+' | sort -u); do
    gh issue edit "$k" --repo "$REPO" --add-label ready --remove-label in-progress >/dev/null && log "#$k back to ready"
  done
}

# pinned host or image files: the same list as CLAUDE.md and agents/roles/common.md
protected_pr() {
  gh pr view "$1" --repo "$REPO" --json files --jq '.files[].path' |
    grep -qxE 'agents/(orchestrate\.sh|Containerfile|entrypoint\.sh|install\.sh)'
}

# The tail's top-level "type":"result" lines and non-JSON lines (stderr) only, so game talk in tool
# output about a "fire rate limit" or a duration_ms of 14290 never reads as a usage limit.
tail_errors() {
  tail -n 20 "$1" 2>/dev/null | jq -Rr '. as $l | (try fromjson catch null) as $j |
    if ($j | type) != "object" then $l elif $j.type == "result" then $l else empty end'
}
is_usage_limit() {
  tail_errors "$1" |
    grep -qiE 'usage limit|hit your limit|limit reached|limit.{1,5}resets|resets [0-9]|rate[ _-]?limit|overloaded_error|error:? 429\b|\b429 too many'
}
is_auth_error() { tail_errors "$1" | grep -qiE 'authentication_error|invalid x-api-key|OAuth token has expired|\b401\b'; }

episode_done() { # at least one OWNER "Final acceptance" closed, none open, Episode 1 closed
  [[ $(gh issue list --repo "$REPO" --state all --search '"Final acceptance" in:title' --json title,state,author \
        --jq '[.[] | select(.title == "Final acceptance" and .author.login == "'"$OWNER"'") | .state]
          | any(. == "CLOSED") and all(. != "OPEN")') == true ]] &&
  [[ $(gh api "repos/$REPO/milestones?state=all" --jq '.[] | select(.title == "Episode 1") | .state') == closed ]]
}

# an open "Final acceptance" by OWNER whose last playtest started over ACCEPT_EVERY ago: retry it even
# without merges, or a failed acceptance run would never be repeated
acceptance_due() {
  local started
  started=$(cat "$STATE/started-playtester" 2>/dev/null || echo 0)
  (($1 - started > ACCEPT_EVERY)) || return 1
  [[ -n $(gh issue list --repo "$REPO" --state open --author "$OWNER" --search '"Final acceptance" in:title' \
    --json number,title,state,author --jq '.[] | select(.state == "OPEN" and .title == "Final acceptance"
      and .author.login == "'"$OWNER"'") | .number') ]]
}

# ---- running agents ----

run_agent() {
  local role=$1 name=$2 wd=$3 ctx=$4 prompt logf rc key=()
  if running "vibe-$name"; then log "vibe-$name already running"; return "$RC_INFRA"; fi
  [[ -r $GH_TOKEN_FILE && -r $CLAUDE_TOKEN_FILE ]] || { log "token files missing"; return "$RC_INFRA"; }
  [[ -r $STATE/roles/common.md && -r $STATE/roles/$role.md ]] || { log "missing role $role"; return "$RC_INFRA"; }
  mkdir -p "$LOGS" "$PROMPTS" || return "$RC_INFRA"
  # fresh, unpredictable name: never a fixed path that something could pre-create
  prompt=$(mktemp --suffix=.md "$PROMPTS/$name-$(date +%Y%m%dT%H%M%S).XXXXXX") || return "$RC_INFRA"
  { cat "$STATE/roles/common.md" "$STATE/roles/$role.md" && printf '\n%s\n' "$ctx"; } > "$prompt" ||
    { rm -f -- "$prompt"; return "$RC_INFRA"; }
  logf=$LOGS/$name-$(date +%Y%m%dT%H%M%S)-$BASHPID.jsonl  # pid: two runs in one second must not share a log
  [[ -n ${ANTHROPIC_API_KEY:-} ]] && key=(-e ANTHROPIC_API_KEY)
  podman rm "vibe-$name" >/dev/null 2>&1  # a stopped leftover would block --name; never -f
  log "start vibe-$name ($role) in $wd, log $logf"
  # --init: the entrypoint's exec'd xvfb-run is PID 1 and would drop SIGTERM. --timeout: conmon kills
  # the container even if this orchestrator dies. 9>&-: containers must not inherit the run lock.
  GH_TOKEN=$(<"$GH_TOKEN_FILE") CLAUDE_CODE_OAUTH_TOKEN=$(<"$CLAUDE_TOKEN_FILE") \
  GIT_AUTHOR_NAME=$(git config user.name) GIT_AUTHOR_EMAIL=$(git config user.email) \
    timeout -k 60 "$AGENT_TIMEOUT" podman run --rm --init --timeout "$(($(secs "$AGENT_TIMEOUT") + 120))" \
      --name "vibe-$name" --label vibe=1 --userns=keep-id \
      --memory="$AGENT_MEMORY" --pids-limit="$AGENT_PIDS" --cpus="$AGENT_CPUS" \
      -v "$wd:/work" -v "$prompt:/prompt.md:ro" -e HOME=/tmp/agent-home -e GH_TOKEN -e CLAUDE_CODE_OAUTH_TOKEN "${key[@]}" \
      -e GIT_AUTHOR_NAME -e GIT_AUTHOR_EMAIL "$IMAGE" /work /prompt.md </dev/null >>"$logf" 2>&1 9>&-
  rc=$?
  rm -f -- "$prompt"
  case $rc in
    125 | 126 | 127) rc=$RC_INFRA ;;  # podman itself failed (no image, name clash): no agent ran
    0) ;;
    124 | 137)  # timeout killed only the podman client: make sure the container is gone too
      podman kill "vibe-$name" >/dev/null 2>&1; podman rm "vibe-$name" >/dev/null 2>&1 ;;
    *) if is_auth_error "$logf"; then
         rc=$RC_INFRA; echo $(($(date +%s) + AUTH_HOLD)) > "$STATE/backoff-until"
         log "auth error in vibe-$name: holding all launches ${AUTH_HOLD}s"
       elif is_usage_limit "$logf"; then rc=75; fi ;;
  esac
  log "end vibe-$name rc=$rc"
  return "$rc"
}

secs() { case $1 in *h) echo $((${1%h} * 3600)) ;; *m) echo $((${1%m} * 60)) ;; *) echo "${1%s}" ;; esac; }

backoff() { # usage limit: hold all launches; 5 min doubling to 4 h
  local b
  b=$(cat "$STATE/backoff" 2>/dev/null || echo 300)
  echo $(($(date +%s) + b)) > "$STATE/backoff-until"
  echo $((b * 2 > 14400 ? 14400 : b * 2)) > "$STATE/backoff"
  log "usage limit: backing off ${b}s"
}

# consecutive failures per key in $STATE/fail/<key> as "<count> <retry-after-epoch>";
# cooldown 5 min doubling to 4 h
fail_note() {
  local f=$STATE/fail/$1 c=0 d
  mkdir -p "$STATE/fail"; [[ -f $f ]] && read -r c _ < "$f"
  c=$((c + 1)); d=$((c > 6 ? 14400 : 300 << (c - 1))); ((d > 14400)) && d=14400
  echo "$c $(($(date +%s) + d))" > "$f"
  log "$1 failed ($c in a row), cooling down ${d}s"
  echo "$c"
}
cooling() { local c u; [[ -f $STATE/fail/$1 ]] && read -r c u < "$STATE/fail/$1" && ((u > $(date +%s))); }

# finish_run <rc> [key]: prints the consecutive failure count on a counted failure
finish_run() {
  case $1 in
    0) rm -f "$STATE/backoff"; [[ -n ${2:-} ]] && rm -f "$STATE/fail/$2" ;;
    75) backoff ;;
    "$RC_INFRA") ;;
    *) [[ -n ${2:-} ]] && fail_note "$2" ;;
  esac
}

pr_stuck() {
  gh pr edit "$1" --repo "$REPO" --add-label stuck >/dev/null
  gh pr comment "$1" --repo "$REPO" --body "[orchestrator] stuck: $2. Needs the designer." >/dev/null
  log "PR #$1 marked stuck: $2"
}

finish_pr() { # finish_pr <pr> <key> <rc>
  local c
  c=$(finish_run "$3" "$2")
  ((${c:-0} >= 3)) && pr_stuck "$1" "$2 failed $c times in a row"
  return 0
}

# open PRs by OWNER from branch $1
pr_from() {
  gh pr list --repo "$REPO" --state open --head "$1" --json number,author,headRefName \
    --jq '.[] | select(.author.login == "'"$OWNER"'" and .headRefName == "'"$1"'") | .number'
}

finish_dev() {
  local n=$1 rc=$2
  finish_run "$rc" >/dev/null
  case $rc in
    0) # exit 0 without a PR did nothing, unless the developer closed or blocked the issue on purpose
      if [[ -z $(pr_from "issue-$n") ]] && gh issue view "$n" --repo "$REPO" --json state,labels \
           --jq '.state == "OPEN" and all(.labels[]; .name != "blocked")' | grep -qx true; then
        release "$n" "no PR opened"
      fi ;;
    75 | "$RC_INFRA") gh issue edit "$n" --repo "$REPO" --remove-label in-progress >/dev/null ;;
    *) release "$n" "exit $rc" ;;
  esac
}

# mkrun <name> <checkout args...>: prints a new run dir $RUNS/<name>.XXXXXX holding an independent
# clone of the mirror (no hardlinks: an agent must not be able to corrupt mirror objects), pointed at
# GitHub and checked out, all before any container sees it
mkrun() {
  local name=$1 d; shift
  mkdir -p "$RUNS" && d=$(mktemp -d "$RUNS/$name.XXXXXX") || return 1
  if ! { hgit clone -q -n --no-hardlinks "$MIRROR" "$d" >/dev/null 2>&1 &&
          hgit -C "$d" remote set-url origin "https://github.com/$REPO.git" &&
          hgit -C "$d" checkout -q "$@" >/dev/null 2>&1; }; then
    rm -rf -- "$d"; return 1
  fi
  echo "$d"
}
# rmrun <dir> <container>: whatever the outcome. A container that outlived its timeout keeps its dir
# until the next orchestrator start (clean_orphans).
rmrun() {
  if running "vibe-$2"; then log "vibe-$2 still running: leaving $1"; return; fi
  rm -rf -- "$1"
}
# at startup: drop run dirs and prompts left by a killed orchestrator, unless their container still runs
clean_orphans() {
  local p n
  for p in "$RUNS"/* "$PROMPTS"/*; do
    [[ -e $p || -L $p ]] || continue
    n=${p##*/}
    if [[ $p == "$PROMPTS"/* ]]; then n=${n%-*}; else n=${n%.*}; fi  # dev-1-<ts>.X.md / dev-1.X
    running "vibe-$n" && continue
    [[ $n == review-* ]] && running "vibe-art-${n#review-}" && continue  # art and review share a dir
    rm -rf -- "$p" && log "removed orphan $p"
  done
}
pr_head() { gh pr view "$1" --repo "$REPO" --json headRefOid,headRefName,labels \
  --jq '"\(.headRefOid) \(.headRefName) \([.labels[].name] | join(","))"'; }

dispatch_dev() {
  local n=$1 wt
  claim "$n" || return
  if ! wt=$(mkrun "dev-$n" --no-track -B "issue-$n" origin/main); then
    log "#$n: run dir setup failed, dropping claim"
    gh issue edit "$n" --repo "$REPO" --remove-label in-progress >/dev/null; return
  fi
  (
    run_agent developer "dev-$n" "$wt" "Your issue is #$n in $REPO. You are on branch issue-$n.
Open a PR from issue-$n that closes #$n."
    rc=$?; rmrun "$wt" "dev-$n"; finish_dev "$n" "$rc"
  ) &
}

dispatch_fix() {
  local n=$1 wt sha br labels c
  c=$(($(cat "$STATE/cycles/$n" 2>/dev/null || echo 0) + 1))
  if ((c > MAX_FIX_CYCLES)); then pr_stuck "$n" "$MAX_FIX_CYCLES review/fix cycles without approval"; return; fi
  read -r sha br labels < <(pr_head "$n")
  if [[ -z $br ]] || ! mirror fetch -q --prune origin || ! wt=$(mkrun "fix-$n" --track -B "$br" "origin/$br"); then
    log "fix #$n: checkout failed"; return
  fi
  mkdir -p "$STATE/cycles" "$STATE/fixed"
  (
    run_agent developer-fix "fix-$n" "$wt" "Your PR is #$n in $REPO, on branch $br tracking origin/$br.
Push with: git push origin HEAD:$br"
    rc=$?; rmrun "$wt" "fix-$n"
    # a usage limit or setup error means no fix ran: don't spend a cycle
    ((rc != 75 && rc != RC_INFRA)) && echo "$c" > "$STATE/cycles/$n"
    if ((rc == 0)); then
      echo "$sha" > "$STATE/fixed/$n"
      # nothing pushed means nobody will look at this PR again
      [[ $(mirror ls-remote origin "refs/heads/$br" | cut -f1) == "$sha" ]] && pr_stuck "$n" "fix pushed no commits"
    fi
    finish_pr "$n" "fix-$n" "$rc"
  ) &
}

dispatch_review() {
  local n=$1 wt sha br labels art=
  read -r sha br labels < <(pr_head "$n")
  if [[ -z $br ]] || ! mirror fetch -q --prune origin || ! wt=$(mkrun "review-$n" --track -B "$br" "origin/$br"); then
    log "review #$n: checkout failed"; return
  fi
  [[ ,$labels, == *,art,* || ,$labels, == *,render,* ]] && art=1
  (
    ctx="Your PR is #$n in $REPO, head $sha, on branch $br tracking origin/$br. Push with: git push origin HEAD:$br"
    if [[ $art ]]; then
      run_agent art-director "art-$n" "$wt" "$ctx"
      (($? == 75)) && { backoff; rmrun "$wt" "art-$n"; exit; }
      ctx+=$'\nThis PR is labelled art or render: merge it only if it has the label art:approved.'
    fi
    run_agent reviewer "review-$n" "$wt" "$ctx"
    rc=$?; rmrun "$wt" "review-$n"
    # a verdict is a merge or the changes-requested label; exit 0 with neither means nothing was judged
    if ((rc == 0)) && ! gh pr view "$n" --repo "$REPO" --json state,labels \
         --jq '.state == "MERGED" or any(.labels[]; .name == "review:changes-requested")' | grep -qx true; then
      log "review #$n exited 0 without a verdict"; rc=1
    fi
    ((rc == 0)) && { mkdir -p "$STATE/reviewed"; echo "$sha" > "$STATE/reviewed/$n"; }
    finish_pr "$n" "review-$n" "$rc"
  ) &
}

# designer / playtester: fresh clone, detached at origin/main
dispatch_main() { # dispatch_main <role> <name> <context> [on-success-cmd...]
  local role=$1 name=$2 ctx=$3 wt
  shift 3
  cooling "$name" && return
  wt=$(mkrun "$name" --detach origin/main) || { log "$name: run dir setup failed"; return; }
  date +%s > "$STATE/started-$name"
  ( run_agent "$role" "$name" "$wt" "$ctx"; rc=$?; rmrun "$wt" "$name"; finish_run "$rc" "$name" >/dev/null; ((rc == 0)) && "$@" ) &
}

mark_playtested() { echo "$1" > "$STATE/last-playtest-sha"; }

# planning_needed <open-issues-json> <main-sha>: main moved since the last planning run, or something
# waits on the designer (stuck/blocked issues, untriaged bug/polish, stuck PRs)
planning_needed() {
  local sha
  read -r _ sha 2>/dev/null < "$STATE/designer-at"
  [[ $sha != "$2" ]] && return 0
  [[ $(jq '[.[] | select(.author.login == "'"$OWNER"'") | [.labels[].name] |
      select(index("stuck") or index("blocked") or ((index("bug") or index("polish")) and (index("ready") | not)))]
      | length' <<<"$1") != 0 ]] && return 0
  [[ -n $(owner_prs stuck) ]]
}

# playtest_merges <main-sha>: merges since the last playtest; a last sha that is no longer an
# ancestor (history rewritten) re-inits the counter and forces a playtest
playtest_merges() {
  [[ -s $STATE/last-playtest-sha ]] || echo "$1" > "$STATE/last-playtest-sha"
  mirror rev-list --count "$(<"$STATE/last-playtest-sha")..main" 2>/dev/null && return
  echo "$1" > "$STATE/last-playtest-sha"; echo "$PLAYTEST_EVERY"
}

# preflight: the scoped token can push to REPO; a result is cached for GH_CHECK_EVERY seconds
gh_push_ok() {
  local at ok now
  now=$(date +%s)
  read -r at ok 2>/dev/null < "$STATE/gh-ok"
  if ((now - ${at:-0} >= GH_CHECK_EVERY)); then
    ok=$(GH_TOKEN=$(<"$GH_TOKEN_FILE") gh api "repos/$REPO" --jq .permissions.push 2>/dev/null)
    echo "$now ${ok:-error}" > "$STATE/gh-ok"
  fi
  [[ $ok == true ]]
}

sync_roles() {
  local tmp
  tmp=$(mktemp -d) || return
  if mirror archive main agents/roles 2>/dev/null | tar -x -C "$tmp" 2>/dev/null &&
     [[ -f $tmp/agents/roles/common.md ]]; then
    if [[ -L $tmp/agents/roles || -n $(find "$tmp/agents/roles" -type l) ]]; then
      log "sync_roles: agents/roles on main contains a symlink: keeping the old roles"
      rm -rf -- "$tmp"; return
    fi
    rm -rf "$STATE/roles.old"; [[ -d $STATE/roles ]] && mv "$STATE/roles" "$STATE/roles.old"
    mv "$tmp/agents/roles" "$STATE/roles"
  fi
  rm -rf "$tmp"
}

reap_stale() { # in-progress, no container, no open PR, untouched for STALE_SECS
  local cutoff n open_brs
  cutoff=$(date -u -d "@$(($(date +%s) - STALE_SECS))" +%Y-%m-%dT%H:%M:%SZ)
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
  local now hold names devs n merges last head ready all reviews
  now=$(date +%s); hold=$(cat "$STATE/backoff-until" 2>/dev/null || echo 0)
  if ((hold > now)); then log "backoff: sleeping $((hold - now))s"; sleep $((hold - now)); fi

  mirror fetch -q --prune origin || { log "mirror fetch failed"; return; }
  sync_roles
  names=$(running_names) || { log "podman ps failed: skipping iteration"; return; }
  if ! [[ -r $GH_TOKEN_FILE && -r $CLAUDE_TOKEN_FILE && -r $STATE/roles/common.md ]] || ! podman image exists "$IMAGE"; then
    log "preflight failed (token files, $STATE/roles/common.md or image $IMAGE): skipping dispatch"; return
  fi
  gh_push_ok || { log "preflight failed: the gh token cannot push to $REPO: skipping dispatch"; return; }

  if [[ -z $(mirror ls-tree main docs/design) ]]; then
    grep -qx vibe-designer <<<"$names" || dispatch_main designer-bootstrap designer "Mode: bootstrap."
    return
  fi

  close_protected
  all=$(open_issues) || { log "issue list failed"; return; }
  ready=$(ready_issues "$all")

  devs=$(grep -cE '^vibe-(dev|fix)-' <<<"$names")
  for n in $(prs_needing_fix); do
    ((devs < MAX_DEVS)) || break
    dispatch_fix "$n"; devs=$((devs + 1))
  done
  for n in $ready; do
    ((devs < MAX_DEVS)) || break
    grep -qx "vibe-dev-$n" <<<"$names" && continue
    dispatch_dev "$n"; devs=$((devs + 1))
  done

  # up to MAX_REVIEWS pipelines (art + review of one PR count once), oldest PR first
  reviews=$(grep -oE '^vibe-(review|art)-[0-9]+$' <<<"$names" | sed 's/.*-//' | sort -u | grep -c .)
  for n in $(prs_needing_review | sort -n); do
    ((reviews < MAX_REVIEWS)) || break
    dispatch_review "$n"; reviews=$((reviews + 1))
  done

  head=$(mirror rev-parse main)
  merges=$(playtest_merges "$head")
  # also playtest when the team is idle, so the last few merges still get an acceptance pass
  if ! grep -qx vibe-playtester <<<"$names" &&
     { ((merges >= PLAYTEST_EVERY)) || { ((merges > 0)) && [[ -z $names ]]; } || acceptance_due "$now"; }; then
    dispatch_main playtester playtester "Playtest main at $head." mark_playtested "$head"
  fi

  read -r last _ 2>/dev/null < "$STATE/designer-at"
  if (($(grep -c . <<<"$ready") < 2 * MAX_DEVS)) && ! grep -qx vibe-designer <<<"$names" &&
     ((now - ${last:-0} >= DESIGNER_COOLDOWN)) && planning_needed "$all" "$head"; then
    echo "$now $head" > "$STATE/designer-at"
    dispatch_main designer-plan designer "Mode: planning. The ready queue is low."
  fi

  reap_stale

  if episode_done; then log "episode done"; wait; exit 0; fi
}

main_loop() {
  mkdir -p "$LOGS" "$STATE/reviewed" "$STATE/fixed"
  exec 9>"$STATE/lock"; flock -n 9 || { log "another orchestrator is running"; exit 1; }
  # the host's own gh calls use the scoped token, never the broad host login
  [[ -r $GH_TOKEN_FILE ]] && { GH_TOKEN=$(<"$GH_TOKEN_FILE"); export GH_TOKEN; }
  log "orchestrator start (MAX_DEVS=$MAX_DEVS)"
  clean_orphans
  while :; do
    loop_once
    [[ -n ${ORCH_ONCE:-} ]] && break
    sleep "$POLL"
  done
  wait
}

[[ -n ${ORCH_LIB:-} ]] || main_loop
