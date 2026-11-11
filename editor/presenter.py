"""Editor presenter: tools, selection and commands. No wx here, so it can be
driven headlessly (tests/editor_roundtrip.py). The view is any object with
refresh(), set_status(text), textures_changed() and title_changed(); the
presenter calls them, the view calls the presenter's on_* / action methods
with cell coordinates (x east, y north, (0, 0) south-west)."""
from . import model as M

TOOLS = ("paint", "rect", "eyedropper", "erase")
LAYERS = M.LAYERS


class NullView:
    def refresh(self):
        pass

    def set_status(self, text):
        pass

    def textures_changed(self):
        pass

    def title_changed(self):
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
        self.model = None
        self.set_model(model or M.MapModel.new(32, 32))

    # model lifecycle ------------------------------------------------------
    def set_model(self, model):
        if self.model is not None:
            self.model.listeners.remove(self._on_model)
        self.model = model
        model.listeners.append(self._on_model)
        self.drag = self.selection = None
        self.stroke = False
        if self.texture >= len(model.textures):
            self.texture = 1 if len(model.textures) > 1 else 0
        self.view.textures_changed()
        self.view.title_changed()
        self.view.refresh()

    def _on_model(self, what):
        if what == "textures":
            self.view.textures_changed()
        self.view.title_changed()
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
        if self.tool == "eyedropper":
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
        if self.stroke:
            self._apply(x, y, self._tool_tex())
            self.view.refresh()
        elif self.drag is not None:
            x = min(max(x, 0), self.model.width - 1)
            y = min(max(y, 0), self.model.height - 1)
            self.selection = (self.drag[0], self.drag[1], x, y)
            self.view.refresh()

    def on_release(self, x, y):
        if self.stroke:
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
        return self.stroke or self.drag is not None

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

    # status ---------------------------------------------------------------
    def describe(self, x, y):
        if not self.model.inside(x, y):
            return ""
        c = self.model.cell(x, y)
        tn = self.model.texture_name
        return "(%d, %d)  wall %d %s  floor %d %s  ceil %d %s  dark %d  flags 0x%04x" % (
            x, y, c["wall"], tn(c["wall"]) or "-", c["floor"], tn(c["floor"]) or "-",
            c["ceil"], tn(c["ceil"]) or "sky", c["dark"], c["flags"])

    def on_hover(self, x, y):
        self.hover = (x, y)
        d = self.describe(x, y)
        if d:
            self.view.set_status(d)
