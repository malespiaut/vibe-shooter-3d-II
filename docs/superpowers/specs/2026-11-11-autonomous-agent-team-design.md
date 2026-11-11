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
- **No hard stop.** The orchestrator runs until the episode is accepted or the user kills it. There is no time or run cap. To make an endless polish loop visible rather than silent, `STATUS.md` is refreshed after every playtest (milestone progress, open issue count, last merges).
- Tools available on the host: Blender 5.2, Aseprite, wxPython, gcc/clang, CMake, Ninja, SDL2/SDL3, Python 3.14, Podman 6.2.

## Calls made by Claude (user may override)

- **C17, not Zig.** The installed Zig is a 0.18 dev build, and its APIs churn. C is stable and agents write it reliably. `pybast` becomes a C shared library (`libbast.so`) wrapped with Python `ctypes`, which needs no extra build tooling.
- **SDL3 as the platform layer only.** It provides the window/framebuffer present, keyboard/mouse, the audio buffer and file I/O. This is the "platform abstraction layer" `GAME.md` permits. Everything above it (renderer, mixer, formats, game) is written from scratch. SDL's renderer is used only to blit the 320×240 indexed→RGB converted buffer to the window, with integer scaling.
- **Claude authentication:** mount the user's existing Claude Code login (`~/.claude/.credentials.json`) into the container. If usage limits are hit, the orchestrator waits and retries. If `ANTHROPIC_API_KEY` is set on the host it is passed through instead, which removes subscription limits.
- **GitHub authentication:** a **fine-grained personal access token scoped to this one repository only** (contents, issues, pull requests: read/write). It is passed into the container as `GH_TOKEN`. The user's broad `gh` login is never mounted. The token is created once by the user during setup. This is the only manual step.
- **Commit identity:** commits are authored by the user's GitHub identity, with the `Co-Authored-By: Claude` trailer. Each agent role is named in the PR/commit body (e.g. `[developer]`).
- **Model:** the latest Opus for every role.
- **Claude auth, revised:** use a long-lived token from `claude setup-token` (stored in `~/vibe-agents/claude-token`, mode 600, passed as `CLAUDE_CODE_OAUTH_TOKEN`) instead of mounting `.credentials.json`. This avoids refresh-token races between parallel containers and the host session. If `ANTHROPIC_API_KEY` is set it takes precedence. Usage-limit backoff must tolerate multi-hour windows.
- **Orchestrator is not agent-editable at runtime.** The host runs a pinned copy of `orchestrate.sh` and the image built from a pinned `Containerfile`, both in `~/vibe-agents/bin/`. Agents may change `agents/roles/*.md` through PRs, because those only run inside containers. The Reviewer rejects any PR that touches `agents/orchestrate.sh` or `agents/Containerfile`.
- **Review state lives in labels and comments, not GitHub reviews.** Every agent shares the user's token, and GitHub forbids self-approval. Labels: `review:approved`, `review:changes-requested`, `art:approved`. The Reviewer merges through `gh pr merge --squash`.
- **Screenshots/evidence** are committed to a dedicated orphan branch `evidence` under `<pr-number>/`, and linked from PR comments as raw URLs.
- **Aseprite in the image:** the same package the host uses, installed in the Arch image. If that fails, it's built from source in the Containerfile.
- **Public-repo hygiene:** agents act only on issues, PRs and comments authored by the repo owner's account (everything the team writes). Content from any other account is ignored and never executed. The orchestrator filters by author before dispatching.
- **"Existing" things in `GAME.md`** (palette, TEX format, `pybast`, colormap) don't exist yet. The Designer's bootstrap creates issues to define them once, in M1, before any work depends on them. `GAME.md` stays unchanged; `CLAUDE.md` notes this.

## Architecture

### Execution environment

- One Podman image (`vibe-agents`) based on Arch Linux to match the host toolchain. It contains: claude CLI, gh, git, gcc/clang, cmake, ninja, SDL3, Python 3 + wxPython + Pillow + numpy, Blender (headless), Aseprite (CLI batch mode), xvfb, ImageMagick.
- Containers mount **only** a host work directory (`~/vibe-agents/work/`) holding the repo clone and worktrees, plus the Claude credentials file (read-only). No other host paths. Rootless Podman, so the agent's root is the user's unprivileged UID.
- Each agent run is a fresh `podman run --rm` container executing `claude -p --dangerously-skip-permissions --model <opus> "<role prompt>"` in its worktree. Containers are disposable. State lives in git and on GitHub.
- Headless display through `xvfb-run` so the game, Blender and wxPython can run and screenshot without a monitor.

### Orchestrator

A single shell script on the host, `agents/orchestrate.sh` (kept in the repo), run in a `tmux` session. It loops:

1. **Bootstrap (once):** if the repo has no `docs/design/`, run the **Designer** in bootstrap mode.
2. **Dispatch developers:** for each open issue labelled `ready` and not `in-progress`, up to `MAX_DEVS` (default 4) concurrently: add the `in-progress` label, create a git worktree on branch `issue-<n>`, then start a **Developer** container.
3. **Review:** for each open PR without a review from the current head commit, run the **Reviewer**, and for PRs labelled `art` also run the **Art Director**. The Reviewer merges approved PRs itself (squash merge) after `main` still builds with the PR applied.
4. **Playtest:** after every K merges (default 5), or when a milestone's issues are all closed, run the **Playtester** on `main`.
5. **Plan:** when the `ready` queue runs low, or a milestone closes, run the **Designer** in planning mode to file the next batch of issues.
6. **Stop:** when the `Episode 1` milestone is closed **and** the final playtest issue `Final acceptance` is closed by the Playtester with evidence attached. The script then exits and leaves a summary in `STATUS.md`.

Locking and recovery:
- GitHub labels are the source of truth for claims (`in-progress`). A stale claim, with no running container for that issue and no commits for 2 hours, is released.
- Each agent run has a wall-clock timeout (default 90 min). On timeout the container is killed and the issue gets a comment and returns to `ready`.
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
GAME.md, CLAUDE.md, STATUS.md
agents/orchestrate.sh, agents/Containerfile, agents/roles/*.md
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
4. Start the orchestrator in tmux.

## Left to the builder

- Exact role prompt wording, default concurrency and timeouts.
- How the orchestrator script detects review state and dependencies (gh JSON queries).
- Container base image details, as long as the toolchain listed above is present.
- Everything about the game itself, which belongs to the Designer.
