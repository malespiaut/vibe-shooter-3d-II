"""Editor presenter: tools, selection and commands. No wx here, so it can be
driven headlessly (tests/editor_roundtrip.py). The view is any object with
refresh(), set_status(text), textures_changed(), title_changed(),
selection_changed() (the selected cell / entity or their values changed) and
problems_changed() (the validation list changed); the
presenter calls them, the view calls the presenter's on_* / action methods
with cell coordinates (x east, y north, (0, 0) south-west)."""
import os
import shutil
import tempfile

from . import entities as E
from . import launch as L
from . import model as M
from .validate import validate

TOOLS = ("paint", "rect", "eyedropper", "erase", "light", "flags", "entity")
LAYERS = M.LAYERS
VIEW_MODES = ("texture", "light")
LIGHT_FIELDS = ("dark", "dark_alt")
# Door param (tech.md, Map): bits 0-1 key, bits 2-3 mode.
DOOR_KEYS = ("none", "green", "blue", "violet")
DOOR_MODES = ("auto-close (140 tics)", "stays open", "switch-only", "timed (switch param)")
ENT_DIRS = ("E", "NE", "N", "NW", "W", "SW", "S", "SE")  # angle = dir * 8192, 0 = east, CCW
ANG45 = 8192


def door_key(param):
    return param & 3


def door_mode(param):
    return (param >> 2) & 3


def door_param(param, key=None, mode=None):
    """`param` with the key and/or mode fields replaced; other bits kept."""
    if key is not None:
        if not 0 <= key <= 3:
            raise ValueError(key)
        param = (param & ~3) | key
    if mode is not None:
        if not 0 <= mode <= 3:
            raise ValueError(mode)
        param = (param & ~0xC) | (mode << 2)
    return param


def entity_dir(angle):
    """Nearest of the 8 directions (0 = east, counter-clockwise)."""
    return ((angle + ANG45 // 2) >> 13) & 7


def cell_centre(c):
    return (c << 16) | 0x8000


class NullView:
    def refresh(self):
        pass

    def set_status(self, text):
        pass

    def textures_changed(self):
        pass

    def title_changed(self):
        pass

    def selection_changed(self):
        pass

    def problems_changed(self):
        pass


class Presenter:
    def __init__(self, model=None, view=None):
        self.view = view or NullView()
        self.tool = "paint"
        self.layer = "wall"
        self.texture = 1
        self.drag = None        # (x0, y0) while a rect drag is held
        self.selection = None   # (x0, y0, x1, y1) inclusive, rect preview
        self.stroke = False
        self.hover = None
        self.view_mode = "texture"
        self.light_field = "dark"
        self.light_value = 0
        self.cell_sel = None     # (x, y) picked by the flags tool
        self.entity_type = E.BY_NAME["MUTT_TROOPER"][0]
        self.entity_sel = None   # index of the selected entity
        self.moving = False      # entity place/move drag held
        self.problems = []       # validate.Problem list from the last check
        self.game_path = L.default_game_path()
        self.skill = None        # None: the game's default (2)
        self.runner = L.SubprocessRunner()
        self._launched = None    # (proc, stderr file) of the window launch
        self._tmpdir = None
        self.model = None
        self.set_model(model or M.MapModel.new(32, 32))

    # model lifecycle ------------------------------------------------------
    def set_model(self, model):
        if self.model is not None:
            self.model.listeners.remove(self._on_model)
        self.model = model
        model.listeners.append(self._on_model)
        self.drag = self.selection = None
        self.stroke = self.moving = False
        self.cell_sel = self.entity_sel = None
        if self.texture >= len(model.textures):
            self.texture = 1 if len(model.textures) > 1 else 0
        self.view.textures_changed()
        self.view.title_changed()
        self.view.selection_changed()
        self.view.refresh()

    def _on_model(self, what):
        if what == "textures":
            self.view.textures_changed()
        if self.entity_sel is not None and self.entity_sel >= self.model.entity_count:
            self.entity_sel = None
        if self.cell_sel is not None and not self.model.inside(*self.cell_sel):
            self.cell_sel = None
        self.view.title_changed()
        self.view.selection_changed()
        self.view.refresh()

    def new_map(self, width, height, name="Untitled"):
        self.set_model(M.MapModel.new(width, height, name))

    def open(self, path):
        self.set_model(M.MapModel.open(path))
        self.view.set_status("opened %s" % path)

    def save(self, path=None):
        self.model.save(path)
        self.view.set_status("saved %s" % self.model.path)

    def title(self):
        m = self.model
        return "%s%s - %s (%dx%d)" % ("*" if m.dirty else "", m.path or "untitled",
                                       m.name, m.width, m.height)

    # tool state -----------------------------------------------------------
    def set_tool(self, tool):
        if tool not in TOOLS:
            raise ValueError(tool)
        self.tool = tool

    def set_view_mode(self, mode):
        if mode not in VIEW_MODES:
            raise ValueError(mode)
        self.view_mode = mode
        self.view.refresh()

    def set_light_field(self, field):
        if field not in LIGHT_FIELDS:
            raise ValueError(field)
        self.light_field = field
        self.view.refresh()

    def set_light_value(self, v):
        if not 0 <= v <= M.MAX_DARK:
            raise ValueError(v)
        self.light_value = v

    def set_entity_type(self, t):
        t = E.BY_NAME[t][0] if isinstance(t, str) else t
        if t not in E.BY_NUMBER:
            raise ValueError(t)
        self.entity_type = t

    def set_layer(self, layer):
        if layer not in LAYERS:
            raise ValueError(layer)
        self.layer = layer
        self.view.refresh()

    def set_texture(self, index):
        if not 0 <= index < len(self.model.textures):
            raise ValueError(index)
        self.texture = index
        self.view.set_status("texture %d %s" % (index, self.model.texture_name(index) or
                                                 "(none / sky)"))

    def add_texture(self, name):
        i = self.model.add_texture(name)
        self.set_texture(i)
        return i

    # cell edits -----------------------------------------------------------
    def _apply(self, x, y, tex):
        """Write `tex` into the current layer of (x, y). Wall cells carry SOLID:
        a non-zero wall sets it, clearing the wall clears it."""
        if not self.model.inside(x, y):
            return
        if self.layer == "wall":
            flags = self.model.cell(x, y)["flags"]
            solid = M.solid_flag()
            flags = flags | solid if tex else flags & ~solid
            self.model.set_cell(x, y, wall=tex, flags=flags)
        else:
            self.model.set_cell(x, y, **{self.layer: tex})

    def _tool_tex(self):
        return 0 if self.tool == "erase" else self.texture

    def on_press(self, x, y):
        if not self.model.inside(x, y):
            return
        if self.tool == "light":
            self.model.begin()
            self.stroke = True
            self.model.set_cell(x, y, **{self.light_field: self.light_value})
        elif self.tool == "flags":
            self.select_cell(x, y)
        elif self.tool == "entity":
            self._entity_press(x, y)
        elif self.tool == "eyedropper":
            self.set_texture(self.model.cell(x, y)[self.layer])
        elif self.tool == "rect":
            self.drag = (x, y)
            self.selection = (x, y, x, y)
            self.view.refresh()
        else:
            self.model.begin()
            self.stroke = True
            self._apply(x, y, self._tool_tex())

    def on_drag(self, x, y):
        self.on_hover(x, y)
        if self.moving:
            self._entity_move(x, y)
        elif self.stroke and self.tool == "light":
            if self.model.inside(x, y):
                self.model.set_cell(x, y, **{self.light_field: self.light_value})
            self.view.refresh()
        elif self.stroke:
            self._apply(x, y, self._tool_tex())
            self.view.refresh()
        elif self.drag is not None:
            x = min(max(x, 0), self.model.width - 1)
            y = min(max(y, 0), self.model.height - 1)
            self.selection = (self.drag[0], self.drag[1], x, y)
            self.view.refresh()

    def on_release(self, x, y):
        if self.moving:
            self._entity_move(x, y)
            self.moving = False
            self.model.end()
        elif self.stroke:
            self.stroke = False
            self.model.end()
        elif self.drag is not None:
            self.on_drag(x, y)
            x0, y0, x1, y1 = self.selection
            self.drag = self.selection = None
            self.fill_rect(x0, y0, x1, y1)

    def fill_rect(self, x0, y0, x1, y1, tex=None):
        """Fill the inclusive rectangle on the current layer as one command."""
        tex = self.texture if tex is None else tex
        self.model.begin()
        for y in range(min(y0, y1), max(y0, y1) + 1):
            for x in range(min(x0, x1), max(x0, x1) + 1):
                self._apply(x, y, tex)
        n = self.model.end()
        self.view.set_status("filled %d cells" % n)
        self.view.refresh()

    def _busy(self):
        """True while a stroke or rect drag is held; undo/redo would split it."""
        return self.stroke or self.moving or self.drag is not None

    def undo(self):
        if self._busy():
            return
        if not self.model.undo():
            self.view.set_status("nothing to undo")

    def redo(self):
        if self._busy():
            return
        if not self.model.redo():
            self.view.set_status("nothing to redo")

    # cell flags / tag / param (flags tool) -------------------------------
    def select_cell(self, x, y):
        self.cell_sel = (x, y) if self.model.inside(x, y) else None
        self.view.selection_changed()
        self.view.refresh()

    def selected_cell(self):
        return self.model.cell(*self.cell_sel) if self.cell_sel else None

    def set_cell_flag(self, name, on):
        if self.cell_sel is None:
            return
        bit = M.bast.cell_flags()[name]
        flags = self.selected_cell()["flags"]
        self.model.set_cell(*self.cell_sel, flags=flags | bit if on else flags & ~bit)

    def set_cell_values(self, **fields):
        """tag / param / dark / dark_alt of the selected cell, one undo step."""
        if self.cell_sel is None:
            return
        for f in fields:
            if f not in ("tag", "param", "dark", "dark_alt"):
                raise ValueError(f)
        self.model.set_cell(*self.cell_sel, **fields)

    def set_door(self, key=None, mode=None):
        """Door key / mode of the selected cell, written into its param."""
        if self.cell_sel is None:
            return
        c = self.selected_cell()
        self.model.set_cell(*self.cell_sel, param=door_param(c["param"], key, mode))

    def is_door(self):
        c = self.selected_cell()
        return bool(c and c["flags"] & M.bast.cell_flags()["DOOR"])

    # entities (entity tool) ------------------------------------------------
    def _entity_press(self, x, y):
        here = self.model.entities_at(x, y)
        self.model.begin()
        if here:
            self.entity_sel = here[-1]
        else:
            self.entity_sel = self.model.add_entity(self.entity_type, cell_centre(x),
                                                    cell_centre(y))
        self.moving = True
        self.view.selection_changed()
        self.view.refresh()

    def _entity_move(self, x, y):
        if self.entity_sel is None:
            return
        x = min(max(x, 0), self.model.width - 1)
        y = min(max(y, 0), self.model.height - 1)
        self.model.set_entity(self.entity_sel, x=cell_centre(x), y=cell_centre(y))
        self.view.refresh()

    def place_entity(self, x, y, type=None, **fields):
        """Place an entity at the centre of cell (x, y), select it, one undo step."""
        t = self.entity_type if type is None else type
        t = E.BY_NAME[t][0] if isinstance(t, str) else t
        self.entity_sel = self.model.add_entity(t, cell_centre(x), cell_centre(y), **fields)
        self.view.selection_changed()
        return self.entity_sel

    def select_entity(self, i):
        self.entity_sel = i if i is not None and 0 <= i < self.model.entity_count else None
        self.view.selection_changed()
        self.view.refresh()

    def selected_entity(self):
        return self.model.entity(self.entity_sel) if self.entity_sel is not None else None

    def set_entity_props(self, **fields):
        """type / tag / param / name / flags of the selected entity, one undo step."""
        if self.entity_sel is None:
            return
        if isinstance(fields.get("type"), str):
            fields["type"] = E.BY_NAME[fields["type"]][0]
        self.model.set_entity(self.entity_sel, **fields)

    def set_entity_flag(self, name, on):
        e = self.selected_entity()
        if e is None:
            return
        bit = M.bast.entity_flags()[name]
        self.set_entity_props(flags=e["flags"] | bit if on else e["flags"] & ~bit)

    def rotate_entity(self, d):
        """Face direction d (0..7, 0 = east, counter-clockwise)."""
        if not 0 <= d <= 7:
            raise ValueError(d)
        self.set_entity_props(angle=d * ANG45)

    def move_entity(self, x, y):
        if self.entity_sel is not None and self.model.inside(x, y):
            self.model.set_entity(self.entity_sel, x=cell_centre(x), y=cell_centre(y))

    def delete_entity(self):
        if self.entity_sel is None or self._busy():
            return
        self.model.remove_entity(self.entity_sel)
        self.entity_sel = None
        self.view.selection_changed()
        self.view.refresh()

    # validation / launch ------------------------------------------------
    def validate(self):
        """Run the map checks; keeps and returns the problem list."""
        self.problems = validate(self.model)
        n = len(self.problems)
        self.view.set_status("%d problem%s" % (n, "" if n == 1 else "s") if n else
                             "map valid")
        self.view.problems_changed()
        return self.problems

    def select_problem(self, i):
        """Select the cell of problem i (with the flags tool, so the
        selection is drawn and its properties shown)."""
        if not 0 <= i < len(self.problems) or self.problems[i].cell is None:
            return
        self.set_tool("flags")
        self.select_cell(*self.problems[i].cell)
        self.view.set_status(str(self.problems[i]))

    def _work_dir(self):
        if self._tmpdir is None or not os.path.isdir(self._tmpdir):
            self._tmpdir = tempfile.mkdtemp(prefix="vs3d-editor-")
        return self._tmpdir

    def _prepare_launch(self):
        """Validate, then write the current map to a temp file. The model's
        own path and dirty flag are untouched."""
        if self.validate():
            raise L.LaunchError("map has %d problem(s), not launched:\n%s" % (
                len(self.problems), "\n".join(str(p) for p in self.problems[:20])))
        base = os.path.splitext(os.path.basename(self.model.path or "untitled.vmap"))[0]
        path = os.path.join(self._work_dir(), base + ".vmap")
        self.model.export(path)
        return path

    def launch(self):
        """Start the game in its window on the current map (F5)."""
        path = self._prepare_launch()
        argv = L.game_argv(self.game_path, path, self.skill)
        self._close_launch()
        err = tempfile.TemporaryFile(dir=self._work_dir())
        try:
            proc = self.runner.spawn(argv, err)
        except OSError as e:
            err.close()
            raise L.LaunchError("cannot run %s: %s" % (self.game_path, e.strerror or e))
        self._launched = (proc, err, argv)
        self.view.set_status("launched: " + " ".join(argv))
        return proc

    def _close_launch(self):
        if self._launched:
            self._launched[1].close()
            self._launched = None

    @property
    def launching(self):
        """True while a window launch is being watched."""
        return self._launched is not None

    def poll_launch(self):
        """None while the game runs or after a clean exit; once, after a
        failed exit, the error text with its stderr."""
        if not self._launched:
            return None
        proc, err, argv = self._launched
        rc = proc.poll()
        if rc is None:
            return None
        err.seek(0)
        text = err.read().decode(errors="replace")
        self._close_launch()
        if rc == 0:
            self.view.set_status("game exited")
            return None
        return "%s: exit %d\n\n%s" % (" ".join(argv), rc, text.strip() or "(no stderr)")

    def launch_headless(self):
        """Run the 70-tic headless top-down test; returns the last frame's
        PNG path."""
        path = self._prepare_launch()
        frames = os.path.join(self._work_dir(), "frames")
        shutil.rmtree(frames, ignore_errors=True)
        os.makedirs(frames)
        argv = L.headless_argv(self.game_path, path, frames, self.skill)
        try:
            rc, out, err = self.runner.run(argv, L.HEADLESS_TIMEOUT)
        except OSError as e:
            raise L.LaunchError("cannot run %s: %s" % (self.game_path, e.strerror or e))
        except Exception as e:  # subprocess.TimeoutExpired
            raise L.LaunchError("%s: %s" % (" ".join(argv), e))
        frame = L.frame_path(frames)
        if rc != 0 or not os.path.isfile(frame):
            raise L.LaunchError("%s: exit %d\n\n%s" % (" ".join(argv), rc,
                                                        (err.strip() or "no frame written")))
        stats = [ln for ln in out.splitlines() if ln.startswith("STATS")]
        self.view.set_status(stats[-1] if stats else "headless test done")
        return frame

    # status ---------------------------------------------------------------
    def describe(self, x, y):
        if not self.model.inside(x, y):
            return ""
        c = self.model.cell(x, y)
        tn = self.model.texture_name
        flags = [n for n, b in M.bast.cell_flags().items() if c["flags"] & b]
        ents = [E.label_of(self.model.entity(i)["type"]) for i in self.model.entities_at(x, y)]
        return ("(%d, %d)  wall %d %s  floor %d %s  ceil %d %s  dark %d/%d  tag %d  param %d"
                "  %s%s" % (
                    x, y, c["wall"], tn(c["wall"]) or "-", c["floor"], tn(c["floor"]) or "-",
                    c["ceil"], tn(c["ceil"]) or "sky", c["dark"], c["dark_alt"], c["tag"],
                    c["param"], "|".join(flags) or "-",
                    ("  [" + ", ".join(ents) + "]") if ents else ""))

    def on_hover(self, x, y):
        self.hover = (x, y)
        d = self.describe(x, y)
        if d:
            self.view.set_status(d)
