"""Running the game on the edited map (no wx). The argv builders and the
runner are separate so tests can mock the subprocess (tests/editor_launch.py).

Window launch: `game --map <tmp.vmap> [--skill N]`, not waited for; its
stderr goes to a temp file read back if it exits non-zero.
Headless test: `game --headless --topdown --map <tmp.vmap> --max-tics 70
--frames-out <dir> --frame-every 70 [--skill N]`, waited for; the last frame
is <dir>/frame_000070.png (src/main.c names frames by tic)."""
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADLESS_TICS = 70
HEADLESS_TIMEOUT = 60


class LaunchError(Exception):
    pass


def default_game_path():
    """VS3D_GAME overrides; default build/game in the repo."""
    return os.environ.get("VS3D_GAME", os.path.join(ROOT, "build", "game"))


def _skill(skill):
    if skill is None:
        return []
    if not 1 <= skill <= 3:
        raise ValueError("skill %r outside 1..3" % skill)
    return ["--skill", str(skill)]


def game_argv(game, map_path, skill=None):
    return [game, "--map", map_path] + _skill(skill)


def headless_argv(game, map_path, frames_dir, skill=None):
    return [game, "--headless", "--topdown", "--map", map_path,
            "--max-tics", str(HEADLESS_TICS), "--frames-out", frames_dir,
            "--frame-every", str(HEADLESS_TICS)] + _skill(skill)


def frame_path(frames_dir):
    return os.path.join(frames_dir, "frame_%06d.png" % HEADLESS_TICS)


class SubprocessRunner:
    def spawn(self, argv, errfile):
        """Start without waiting; returns an object with poll()."""
        return subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                stderr=errfile)

    def run(self, argv, timeout):
        """Run to completion; returns (returncode, stdout, stderr)."""
        r = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True,
                           timeout=timeout)
        return (r.returncode, r.stdout.decode(errors="replace"),
                r.stderr.decode(errors="replace"))
