# Autonomous Agent Team for Vibe Shooter 3D II — Design

Date: 2026-11-11

## Intent

The user hands over one file, `GAME.md`, and walks away. A team of Claude Code agents running on the user's machine designs, builds, tests and finishes the game. They coordinate only through a public GitHub repository: issues, pull requests, labels, milestones, reviews. When the user comes back, the repository contains a complete, playable episode.

The user will not take part after setup: no reviews, no answers to questions, no art. Every decision the team needs is made by the team, recorded on GitHub, and justified against `GAME.md` and the team's own design docs.

## Goals

- A finished, polished, enjoyable **single episode of about 8 levels**, built according to every constraint in `GAME.md`.
- Fully unattended operation on this machine, running until the episode is done.
- All work visible and auditable on GitHub: each change is an issue, a PR, a review, then a merge.
- Each agent proves its claims by running the real game: builds, tests, recorded demos and screenshots.

## Non-goals

- Multiple episodes, multiplayer, mod support, or ports beyond Linux (the code stays portable, but only Linux is built and verified).
- GitHub Actions CI or any cloud execution. Everything runs locally.
- Human review gates.

## Anti-goals (failure even if it "works")

- A tech demo instead of a game: one test map, no progression, no ending.
- "AI slop" art: blurry, inconsistent pixel scale, off-palette, generic placeholder look. The `GAME.md` visual rules count as hard requirements.
- Violations of `GAME.md`'s core constraints: GPU rendering, truecolor at runtime, float math in gameplay, non-deterministic simulation, an engine framework.
- Agents declaring victory without evidence, or looping forever on polish with no end.
- Agents damaging anything outside the project: other repos, the user's home directory, the user's GitHub account.

## Decided by the user

- **Runs locally**, on this machine (CachyOS, 24 cores, 62 GB RAM). No spending limit.
- **Public GitHub repo** under the user's account (`malespiaut/vibe-shooter-3d-II`).
- **Language: C or Zig, no C++.** The `GAME.md` references to C++ and `pybast` are reinterpreted for that language.
- **A game designer agent invents all game content.** `GAME.md` holds no content design.
- **Scope: one episode, about 8 levels.**
- **Agents run inside a Podman container** with permission prompts disabled.
- **Start from nothing.** The earlier project `~/Code/vibe-shooter-3d` ("IRONHOLD") is off-limits: it is never mounted into containers and never referenced in prompts.
- **No hard stop.** The orchestrator runs until the episode is accepted or the user kills it. There is no time or run cap. To make an endless polish loop visible rather than silent, the Playtester rewrites the body of the pinned GitHub issue `Team status` after every playtest (milestone progress, open issue count, last merges). `STATUS.md` in the repo is only a pointer to that issue.
- Tools available on the host: Blender 5.2, Aseprite, wxPython, gcc/clang, CMake, Ninja, SDL2/SDL3, Python 3.14, Podman 6.2.

## Calls made by Claude (user may override)

- **C17, not Zig.** The installed Zig is a 0.18 dev build, and its APIs churn. C is stable and agents write it reliably. `pybast` becomes a C shared library (`libbast.so`) wrapped with Python `ctypes`, which needs no extra build tooling.
- **SDL3 as the platform layer only.** It provides the window/framebuffer present, keyboard/mouse, the audio buffer and file I/O. This is the "platform abstraction layer" `GAME.md` permits. Everything above it (renderer, mixer, formats, game) is written from scratch. SDL's renderer is used only to blit the 320×240 indexed→RGB converted buffer to the window, with integer scaling.
- **Claude authentication** (*replaced by "Claude auth, revised" below; nothing is mounted*): mount the user's existing Claude Code login (`~/.claude/.credentials.json`) into the container. If usage limits are hit, the orchestrator waits and retries. If `ANTHROPIC_API_KEY` is set on the host it is passed through instead, which removes subscription limits.
- **GitHub authentication:** a **fine-grained personal access token scoped to this one repository only** (contents, issues, pull requests: read/write). It is passed into the container as `GH_TOKEN`. The user's broad `gh` login is never mounted. The token is created once by the user during setup. This is the only manual step.
- **Commit identity:** commits are authored by the user's GitHub identity, with the `Co-Authored-By: Claude` trailer. Each agent role is named in the PR/commit body (e.g. `[developer]`).
- **Model:** the latest Opus for every role.
- **Claude auth, revised:** use a long-lived token from `claude setup-token` (stored in `~/vibe-agents/claude-token`, mode 600, passed as `CLAUDE_CODE_OAUTH_TOKEN`) instead of mounting `.credentials.json`. This avoids refresh-token races between parallel containers and the host session. If `ANTHROPIC_API_KEY` is set it takes precedence. Usage-limit backoff must tolerate multi-hour windows.
- **Orchestrator is not agent-editable at runtime.** The host runs a pinned copy of `orchestrate.sh` and the image built from a pinned `Containerfile`, both in `~/vibe-agents/bin/`. Agents may change `agents/roles/*.md` through PRs, because those only run inside containers. Protected files: `agents/orchestrate.sh`, `agents/Containerfile`, `agents/entrypoint.sh`, `agents/install.sh` (the same list in `CLAUDE.md` and `agents/roles/common.md`). The orchestrator closes any owner PR that touches one, and hands the issue it closes back to `ready`; the Reviewer also rejects such PRs.
- **Review state lives in labels and comments, not GitHub reviews.** Every agent shares the user's token, and GitHub forbids self-approval. Labels: `review:approved`, `review:changes-requested`, `art:approved`. The Reviewer merges through `gh pr merge --squash`.
- **Screenshots/evidence** are committed to a dedicated orphan branch `evidence` under `<pr-number>/` (`issue-<n>/` before a PR exists, `playtest-<sha7>/` for playtests), and linked from PR comments as raw URLs. Agents add them from a `git worktree` at `/tmp/ev` inside their own clone and push to `evidence`, rebasing and retrying on rejection. `evidence` is never merged into `main`.
- **Aseprite in the image:** the same package the host uses, installed in the Arch image. If that fails, it's built from source in the Containerfile.
- **Public-repo hygiene:** agents act only on issues, PRs and comments authored by the repo owner's account (everything the team writes). Content from any other account is ignored and never executed. The orchestrator filters by author before dispatching.
- **"Existing" things in `GAME.md`** (palette, TEX format, `pybast`, colormap) don't exist yet. The Designer's bootstrap creates issues to define them once, in M1, before any work depends on them. `GAME.md` stays unchanged; `CLAUDE.md` notes this.

## Architecture

### Execution environment

- One Podman image (`vibe-agents`) based on Arch Linux to match the host toolchain. It contains: claude CLI, gh, git, gcc/clang, cmake, ninja, SDL3, Python 3 + wxPython + Pillow + numpy, Blender (headless), Aseprite (CLI batch mode), xvfb, ImageMagick.
- Each agent run gets a **private clone** in a fresh host directory `~/vibe-agents/state/runs/<name>.XXXXXX`. The host clones it from a host-private mirror (`~/vibe-agents/state/mirror`, never mounted), points `origin` at GitHub and checks out the right ref, all before the container starts. The container mounts **only** that directory at `/work` (read-write) and a freshly created prompt file at `/prompt.md` (read-only). No other host paths: no `~/vibe-agents/work` (no longer used), and no credential files, because the tokens arrive as environment variables (`GH_TOKEN`, `CLAUDE_CODE_OAUTH_TOKEN`). Rootless Podman with `--userns=keep-id`, so the agent's root is the user's unprivileged UID. After the run the host only deletes the directory and never runs git in it again; leftovers from a killed orchestrator are removed at its next start.
- Each agent run is a fresh `podman run --rm --init` container executing `claude -p --dangerously-skip-permissions --model <opus>` with the prompt on stdin, in `/work`. `--init` makes the timeout's SIGTERM reach the agent, a conmon-side `--timeout` (agent timeout + 2 min) kills the container even if the orchestrator dies, and each container is limited by default to `--memory=12g --pids-limit=4096 --cpus=8`. Containers are disposable. State lives in git and on GitHub.
- All host git runs on the mirror or on a just-created run directory, with `GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1`. The host's own `gh` calls use the scoped PAT, never the broad host login.
- Headless display through `xvfb-run` so the game, Blender and wxPython can run and screenshot without a monitor.

### Orchestrator

A single shell script on the host, `agents/orchestrate.sh` (kept in the repo), run in a `tmux` session. It loops:

1. **Bootstrap (once):** if the repo has no `docs/design/`, run the **Designer** in bootstrap mode.
2. **Dispatch developers:** for each open issue labelled `ready` and not `in-progress` (excluding the `Team status` and `Final acceptance` issues), up to `MAX_DEVS` (default 4) concurrently: add the `in-progress` label, make a private clone on branch `issue-<n>`, then start a **Developer** container. PRs labelled `review:changes-requested` on a reviewed head get a developer-fix run from the same pool.
3. **Review:** for each open PR without a review from the current head commit, oldest PR first, up to `MAX_REVIEWS` (default 2) pipelines at once, run the **Reviewer**, and for PRs labelled `art` or `render` run the **Art Director** first. The Reviewer merges approved PRs itself (squash merge) after `main` still builds with the PR applied.
4. **Playtest:** run the **Playtester** on `main` after 5 merges since the last playtest, when the team is idle and anything merged since, or when `Final acceptance` has been open for more than 2 h since the last playtest started.
5. **Plan:** when the `ready` queue runs low (under `2*MAX_DEVS`), at most every 30 min, and only if `main` moved since the last planning run or something waits on the designer (`stuck`/`blocked` issues, untriaged `bug`/`polish`, `stuck` PRs), run the **Designer** in planning mode.
6. **Stop:** when the `Episode 1` milestone is closed **and** an owner issue `Final acceptance` is closed (none open) by the Playtester with evidence attached. The script then waits for running agents and exits; it writes no summary (the `Team status` issue holds the latest state).

Locking and recovery:
- GitHub labels are the source of truth for claims (`in-progress`). A stale claim is released: the issue's `updatedAt` is older than 2 hours, no container runs for it and no open PR comes from `issue-<n>`.
- Each agent run has a wall-clock timeout (default 90 min). On timeout the orchestrator kills and removes the container, and the run counts as a failure (the issue gets a comment and returns to the queue).
- A run that exits 0 without doing its job counts as a failure: a developer with no open PR (unless the issue was closed or labelled `blocked`), a reviewer that neither merged nor labelled `review:changes-requested`.
- A run lock (`flock` on `~/vibe-agents/state/lock`) keeps a second orchestrator from starting.
- Before dispatching, the orchestrator checks (cached 10 min) that the scoped PAT can still push. A Claude authentication error holds all launches for 30 min without counting a failure.
- An issue that fails 3 times gets labelled `stuck` and goes back to the Designer, who splits or rewrites it.
- Rate-limit or usage-limit errors from claude: exponential backoff, no state lost.
- Orchestrator logs go to `~/vibe-agents/logs/`. Each agent's full transcript is saved there per run.

### Roles

Each role is a prompt file in `agents/roles/<role>.md`, versioned in the repo so the agents can improve their own process through PRs reviewed like any other change. All roles share `CLAUDE.md` (generated from `GAME.md` plus team conventions).

- **Designer (game designer + producer).** Bootstrap: writes `docs/design/` (game pitch, weapons, enemies, pickups, the 8 levels with layouts sketched in text, progression, story, HUD, audio, difficulty, ending), the technical roadmap, and the milestone plan. Creates milestones and the first issues. Planning: keeps the issue queue fed in dependency order, splits `stuck` issues, triages playtest bugs, and decides when the episode is content-complete. The Designer owns the definition of done for each milestone.
- **Developer.** Takes one issue. Reads `GAME.md`, `CLAUDE.md`, the relevant design docs and code. Implements on its branch. Builds, runs tests, and for visible changes runs a recorded demo and attaches screenshots to the PR. Opens a PR that closes the issue. Handles review feedback on its own PR when re-dispatched (the orchestrator re-dispatches PRs with `changes-requested`). Developers do code, tools, assets (procedural, Blender, hand-drawn via Aseprite CLI scripts or direct pixel data) and levels.
- **Reviewer.** Checks each PR for correctness, adherence to `GAME.md` constraints, tests, a working build, and actual evidence. Approves and merges, or requests changes with specific reasons.
- **Art Director.** Reviews PRs touching assets or rendering. It checks palette conformance (an automated tool does this), pixel scale (1 world unit = 64 px), readability and silhouettes at 320×240, and consistency with the established style. It can block merges. It also maintains `docs/design/art-bible.md`.
- **Playtester.** Builds `main`, plays levels through recorded and scripted input demos, checks that every level can be completed start to finish, captures screenshots, judges fun, pacing, difficulty and clarity, and files `bug` / `polish` issues with screenshots. It owns the final acceptance check.

### Issue and PR conventions

- Labels: area (`engine`, `render`, `audio`, `tools`, `editor`, `art`, `level`, `gameplay`, `ui`), state (`ready`, `in-progress`, `stuck`, `blocked`), kind (`feature`, `bug`, `polish`).
- Milestones (initial; the Designer may refine them): `M1 Engine core` → `M2 Asset pipeline & editor` → `M3 Gameplay systems` → `M4 Content: levels 1–8` → `M5 Polish & audio` → `Episode 1` (final acceptance).
- Each issue states acceptance criteria that a reviewer can verify. Each PR body has: what changed, how it was verified (commands plus output), and screenshots for anything visible.
- Dependencies are written as `Depends on #n` in the issue body. The orchestrator does not dispatch an issue whose dependencies are still open.

### Verification tooling (built early, in M1)

These are what make "no human" safe, so they are the first issues filed:
- **Headless mode:** `game --headless --demo <file> --frames-out <dir>` runs the deterministic simulation from a recorded input file and dumps chosen frames as PNG.
- **Demo recording/playback:** input recorded per tick. Determinism is tested by replaying a demo twice and comparing state hashes.
- **Palette checker:** fails if any asset contains an index or color outside the palette.
- **Level completion check:** a per-level scripted demo that reaches the exit, run in tests, so every level is proven completable.
- Unit tests in C, plain asserts in a `tests/` runner, using no framework.

### Repository layout (starting point; agents may evolve it via PRs)

```
GAME.md, CLAUDE.md, STATUS.md (pointer to the pinned `Team status` issue)
agents/orchestrate.sh, agents/Containerfile, agents/entrypoint.sh, agents/install.sh, agents/roles/*.md
docs/design/                 game design (Designer-owned)
src/engine/  src/game/  src/platform/   C17 sources
src/bast/                    libbast.so API used by Python tools
tools/                       Python asset pipeline + pybast ctypes wrapper
editor/                      wxPython MVP map editor
assets/src/                  palette, source art, .blend, generator params
maps/                        native map files
tests/  demos/
```

## Setup steps (done once, by Claude with the user's go-ahead)

1. Create the public GitHub repo, push `GAME.md`, `CLAUDE.md`, `agents/`, and this spec.
2. The user creates a fine-grained PAT for that repo and saves it to `~/vibe-agents/gh-token` (mode 600). This is the only manual step.
3. Build the Podman image and run a smoke test: a container that runs `claude -p` and `gh issue list` against the repo.
4. Protect `main` on GitHub. *Enabled:* PR required with 0 approvals (every agent shares the owner's token), enforced for admins, no force pushes, no deletion.
5. Start the orchestrator in tmux.

## Left to the builder

- Exact role prompt wording, default concurrency and timeouts.
- How the orchestrator script detects review state and dependencies (gh JSON queries).
- Container base image details, as long as the toolchain listed above is present.
- Everything about the game itself, which belongs to the Designer.
