"""Editor view: wx widgets only. Forwards input to the presenter and draws
what the presenter / model expose. North is up: map y grows upward."""
import os

import wx

from . import colors
from . import entities as E
from . import launch as L
from .textures import TextureLibrary
from .model import bast
from .presenter import (Presenter, TOOLS, LAYERS, VIEW_MODES, LIGHT_FIELDS, DOOR_KEYS,
                        DOOR_MODES, ENT_DIRS, door_key, door_mode, entity_dir)

TOOL_LABELS = {"paint": "Paint", "rect": "Rectangle", "eyedropper": "Eyedropper",
               "erase": "Erase", "light": "Light", "flags": "Flags", "entity": "Entity"}
# One-letter overlay glyph per cell flag (SOLID is shown by the wall itself).
FLAG_GLYPHS = {"DOOR": "D", "PUSHWALL": "P", "SWITCH": "S", "EXIT": "X", "SECRET": "$",
               "DAMAGE": "!", "ONEWAY": ">", "START_OPEN": "o", "BLOCK_MONSTER": "m",
               "BLOCK_ALL": "#", "TRIGGER": "T"}
THUMB = 32        # texture picker icon size
TEX_MIN_ZOOM = 16  # cells drawn with texture thumbnails from this zoom up
SKILLS = ((None, "Game default"), (1, "1 Kitten"), (2, "2 Tomcat"), (3, "3 Sabretooth"))
ENT_FLAG_LABELS = (("SKILL1", "Skill 1 Kitten"), ("SKILL2", "Skill 2 Tomcat"),
                   ("SKILL3", "Skill 3 Sabretooth"), ("AMBUSH", "Ambush / deaf"),
                   ("SECRET_ONLY", "Secret only"), ("PICKUP_TRIGGER", "Pickup fires tag"))


class GridCanvas(wx.ScrolledWindow):
    def __init__(self, parent, frame):
        super().__init__(parent, style=wx.HSCROLL | wx.VSCROLL | wx.FULL_REPAINT_ON_RESIZE)
        self.frame = frame
        self.zoom = 16
        self.SetBackgroundStyle(wx.BG_STYLE_PAINT)
        self.SetBackgroundColour(wx.Colour(40, 40, 44))
        self.Bind(wx.EVT_PAINT, self.on_paint)
        self.Bind(wx.EVT_LEFT_DOWN, self.on_down)
        self.Bind(wx.EVT_LEFT_DCLICK, self.on_down)  # a fast second press arrives as DCLICK
        self.Bind(wx.EVT_LEFT_UP, self.on_up)
        self.Bind(wx.EVT_MOTION, self.on_motion)
        self.Bind(wx.EVT_MOUSEWHEEL, self.on_wheel)
        self.Bind(wx.EVT_KEY_DOWN, self.on_key)
        self.update_virtual()

    @property
    def p(self):
        return self.frame.presenter

    def update_virtual(self):
        m = self.p.model
        self.SetVirtualSize(m.width * self.zoom + 1, m.height * self.zoom + 1)
        self.SetScrollRate(self.zoom, self.zoom)
        self.Refresh()

    def set_zoom(self, z):
        self.zoom = max(4, min(48, z))
        self.update_virtual()

    def cell_at(self, pos):
        x, y = self.CalcUnscrolledPosition(pos.x, pos.y)
        cx = x // self.zoom
        cy = self.p.model.height - 1 - y // self.zoom
        return cx, cy

    def on_down(self, ev):
        if not self.HasCapture():
            self.CaptureMouse()
        self.p.on_press(*self.cell_at(ev.GetPosition()))

    def on_up(self, ev):
        if self.HasCapture():
            self.ReleaseMouse()
        self.p.on_release(*self.cell_at(ev.GetPosition()))

    def on_motion(self, ev):
        x, y = self.cell_at(ev.GetPosition())
        if ev.Dragging() and ev.LeftIsDown():
            self.p.on_drag(x, y)
        else:
            self.p.on_hover(x, y)

    def on_key(self, ev):
        """Entity shortcuts: Delete removes, R / Shift+R turn 45 deg CCW / CW."""
        k, e = ev.GetKeyCode(), self.p.selected_entity()
        if k in (wx.WXK_DELETE, wx.WXK_BACK) and e:
            self.p.delete_entity()
        elif k == ord("R") and e:
            d = entity_dir(e["angle"]) + (-1 if ev.ShiftDown() else 1)
            self.p.rotate_entity(d % 8)
        else:
            ev.Skip()

    def on_wheel(self, ev):
        if ev.ControlDown():
            self.set_zoom(self.zoom + (2 if ev.GetWheelRotation() > 0 else -2))
        else:
            ev.Skip()

    def on_paint(self, _ev):
        dc = wx.AutoBufferedPaintDC(self)
        self.DoPrepareDC(dc)
        dc.SetBackground(wx.Brush(self.GetBackgroundColour()))
        dc.Clear()
        p, m, z = self.p, self.p.model, self.zoom
        H = m.height
        if p.view_mode == "light":
            self._paint_light(dc, p, m, z)
        else:
            self._paint_textures(dc, p, m, z)
        self._paint_flags(dc, m, z)
        self._paint_entities(dc, p, m, z)
        dc.SetBrush(wx.TRANSPARENT_BRUSH)
        if p.cell_sel and p.tool == "flags":
            x, y = p.cell_sel
            dc.SetPen(wx.Pen(wx.Colour(80, 255, 255), 2))
            dc.DrawRectangle(x * z, (H - 1 - y) * z, z + 1, z + 1)
        if p.selection:
            x0, y0, x1, y1 = p.selection
            lx, hx = min(x0, x1), max(x0, x1)
            ly, hy = min(y0, y1), max(y0, y1)
            dc.SetPen(wx.Pen(wx.Colour(255, 230, 60), 2))
            dc.DrawRectangle(lx * z, (H - 1 - hy) * z, (hx - lx + 1) * z + 1,
                             (hy - ly + 1) * z + 1)

    def _tex_bitmap(self, index, z):
        """Thumbnail of map texture `index` at z x z, or None (no file, or
        zoomed out below TEX_MIN_ZOOM)."""
        if z < TEX_MIN_ZOOM or index == 0:
            return None
        return self.frame.tex_bitmap(self.p.model.texture_name(index), z)

    def _paint_textures(self, dc, p, m, z):
        layer = p.layer
        dc.SetPen(wx.Pen(wx.Colour(0, 0, 0)))
        for y in range(m.height):
            sy = (m.height - 1 - y) * z
            for x in range(m.width):
                c = m.cell(x, y)
                sx = x * z
                if c["wall"]:
                    bmp = self._tex_bitmap(c["wall"], z)
                    if bmp:
                        dc.DrawBitmap(bmp, sx, sy)
                        dc.SetBrush(wx.TRANSPARENT_BRUSH)
                    else:
                        dc.SetBrush(wx.Brush(wx.Colour(*colors.texture_rgb(c["wall"]))))
                    dc.DrawRectangle(sx, sy, z + 1, z + 1)
                    if layer == "wall" and z >= 8:  # mark solid blocks
                        dc.DrawLine(sx + 2, sy + 2, sx + z - 1, sy + z - 1)
                    continue
                val = c["ceil"] if layer == "ceil" else c["floor"]
                bmp = self._tex_bitmap(val, z)
                if bmp:
                    dc.DrawBitmap(bmp, sx, sy)
                    dc.SetBrush(wx.TRANSPARENT_BRUSH)
                else:
                    rgb = colors.texture_rgb(val, sky=(layer == "ceil"))
                    dc.SetBrush(wx.Brush(wx.Colour(*rgb)))
                dc.DrawRectangle(sx, sy, z + 1, z + 1)
                if layer != "ceil" and c["ceil"] == 0 and z >= 8:  # sky marker
                    dc.SetBrush(wx.Brush(wx.Colour(*colors.SKY_RGB)))
                    dc.DrawPolygon([(sx + z - z // 3, sy + 1), (sx + z, sy + 1),
                                    (sx + z, sy + z // 3)])

    def _paint_light(self, dc, p, m, z):
        """Grey by darkness of the chosen field (0 = bright), value printed."""
        field = p.light_field
        dc.SetFont(wx.Font(max(6, z // 3), wx.FONTFAMILY_TELETYPE, wx.FONTSTYLE_NORMAL,
                           wx.FONTWEIGHT_NORMAL))
        for y in range(m.height):
            sy = (m.height - 1 - y) * z
            for x in range(m.width):
                c = m.cell(x, y)
                sx = x * z
                g = colors.dark_grey(c[field])
                dc.SetPen(wx.Pen(wx.Colour(0, 0, 0)))
                dc.SetBrush(wx.Brush(wx.Colour(g, g, g)))
                dc.DrawRectangle(sx, sy, z + 1, z + 1)
                if c["wall"]:
                    dc.SetPen(wx.Pen(wx.Colour(110, 60, 40)))
                    dc.DrawLine(sx + 2, sy + 2, sx + z - 1, sy + z - 1)
                    dc.DrawLine(sx + z - 2, sy + 2, sx + 1, sy + z - 1)
                if z >= 16:
                    dc.SetTextForeground(wx.Colour(0, 0, 0) if g > 128 else
                                         wx.Colour(230, 230, 230))
                    dc.DrawText(str(c[field]), sx + 2, sy + z - z // 3 - 4)

    def _paint_flags(self, dc, m, z):
        if z < 12:
            return
        names = [(b, FLAG_GLYPHS[n]) for n, b in bast.cell_flags().items() if n in FLAG_GLYPHS]
        dc.SetFont(wx.Font(max(6, z // 3), wx.FONTFAMILY_TELETYPE, wx.FONTSTYLE_NORMAL,
                           wx.FONTWEIGHT_BOLD))
        dc.SetTextForeground(wx.Colour(255, 240, 80))
        dc.SetTextBackground(wx.Colour(0, 0, 0))
        dc.SetBackgroundMode(wx.SOLID)
        for y in range(m.height):
            sy = (m.height - 1 - y) * z
            for x in range(m.width):
                c = m.cell(x, y)
                g = "".join(ch for b, ch in names if c["flags"] & b)
                if c["tag"]:
                    g += str(c["tag"])
                if g:
                    dc.DrawText(g[:max(1, z // 7)], x * z + 1, sy + 1)
        dc.SetBackgroundMode(wx.TRANSPARENT)

    def _paint_entities(self, dc, p, m, z):
        H = m.height
        dc.SetFont(wx.Font(max(6, z // 3), wx.FONTFAMILY_SWISS, wx.FONTSTYLE_NORMAL,
                           wx.FONTWEIGHT_NORMAL))
        for i, e in enumerate(m.entities):
            cx = e["x"] * z // 65536
            cy = (H * 65536 - e["y"]) * z // 65536
            r = max(3, z * 3 // 10)
            sel = i == p.entity_sel
            dc.SetPen(wx.Pen(wx.Colour(255, 255, 0) if sel else wx.Colour(0, 0, 0),
                             3 if sel else 1))
            dc.SetBrush(wx.Brush(wx.Colour(*E.rgb_of(e["type"]))))
            dc.DrawCircle(cx, cy, r)
            d = entity_dir(e["angle"])
            dx, dy = ((1, 0), (1, -1), (0, -1), (-1, -1), (-1, 0), (-1, 1), (0, 1),
                      (1, 1))[d]
            k = r * 2 if dx and dy else r * 3 // 2 + r // 2
            dc.SetPen(wx.Pen(wx.Colour(0, 0, 0), 2))
            dc.DrawLine(cx, cy, cx + dx * k * 7 // 10, cy + dy * k * 7 // 10)
            if z >= 12:
                label = e["name"] or (E.name_of(e["type"])[:3] if z >= 20 else "")
                if label:
                    dc.SetTextForeground(wx.Colour(255, 255, 255))
                    dc.SetTextBackground(wx.Colour(20, 20, 20))
                    dc.SetBackgroundMode(wx.SOLID)
                    dc.DrawText(label, cx - r, cy + r)
                    dc.SetBackgroundMode(wx.TRANSPARENT)


class EditorFrame(wx.Frame):
    """Implements the presenter's view interface."""

    def __init__(self, model=None):
        super().__init__(None, size=(1280, 800))
        self.presenter = None
        self.status = self.CreateStatusBar(2)
        self.status.SetStatusWidths([-3, -1])
        self._build_menu()
        root = wx.Panel(self)
        self.side = wx.Panel(root)
        self._ready = False
        self.textures = TextureLibrary()
        self._bitmaps = {}
        self.presenter = Presenter(model, self)
        self.canvas = GridCanvas(root, self)
        self.props = wx.ScrolledWindow(root, style=wx.VSCROLL)
        self.props.SetScrollRate(0, 10)
        self._syncing = False
        self._build_side()
        self._build_props()
        s = wx.BoxSizer(wx.HORIZONTAL)
        s.Add(self.side, 0, wx.EXPAND | wx.ALL, 4)
        s.Add(self.canvas, 1, wx.EXPAND)
        s.Add(self.props, 0, wx.EXPAND | wx.ALL, 4)
        root.SetSizer(s)
        self._ready = True
        self.selection_changed()
        self.textures_changed()
        self.title_changed()
        self.Bind(wx.EVT_CLOSE, self.on_close)
        self.launch_timer = wx.Timer(self)
        self.Bind(wx.EVT_TIMER, self.on_launch_timer, self.launch_timer)

    def tex_bitmap(self, name, size):
        """wx.Bitmap of texture `name` scaled to size x size, nearest
        neighbour through the palette; None if the texture has no file."""
        key = (name, size)
        if key not in self._bitmaps:
            try:
                r = self.textures.rgb(name, size, size)
            except bast.BastError:
                r = None
            self._bitmaps[key] = (wx.Image(size, size, bytearray(r[2])).ConvertToBitmap()
                                  if r else None)
        return self._bitmaps[key]

    # layout ---------------------------------------------------------------
    def _build_menu(self):
        mb = wx.MenuBar()
        f, e, v, g = wx.Menu(), wx.Menu(), wx.Menu(), wx.Menu()
        items = [
            (f, wx.ID_NEW, "&New...\tCtrl+N", self.on_new),
            (f, wx.ID_OPEN, "&Open...\tCtrl+O", self.on_open),
            (f, wx.ID_SAVE, "&Save\tCtrl+S", self.on_save),
            (f, wx.ID_SAVEAS, "Save &As...\tCtrl+Shift+S", self.on_save_as),
            (f, wx.ID_EXIT, "&Quit\tCtrl+Q", lambda e: self.Close()),
            (e, wx.ID_UNDO, "&Undo\tCtrl+Z", lambda e: self.presenter.undo()),
            (e, wx.ID_REDO, "&Redo\tCtrl+Y", lambda e: self.presenter.redo()),
            (v, wx.ID_ZOOM_IN, "Zoom &in\tCtrl++", lambda e: self.canvas.set_zoom(self.canvas.zoom + 4)),
            (v, wx.ID_ZOOM_OUT, "Zoom &out\tCtrl+-", lambda e: self.canvas.set_zoom(self.canvas.zoom - 4)),
            (g, wx.NewIdRef(), "&Validate map\tF7", self.on_validate),
            (g, wx.NewIdRef(), "&Launch\tF5", self.on_launch),
            (g, wx.NewIdRef(), "Launch &headless test\tShift+F5", self.on_launch_headless),
            (g, wx.NewIdRef(), "Game &executable...", self.on_game_path),
        ]
        for menu, wid, label, fn in items:
            menu.Append(wid, label)
            self.Bind(wx.EVT_MENU, fn, id=wid)
        mb.Append(f, "&File")
        mb.Append(e, "&Edit")
        sk = wx.Menu()
        for skill, label in SKILLS:
            item = sk.AppendRadioItem(wx.ID_ANY, "Skill: " + label)
            self.Bind(wx.EVT_MENU, lambda e, s=skill: setattr(self.presenter, "skill", s),
                      item)
        g.AppendSubMenu(sk, "&Skill")
        mb.Append(v, "&View")
        mb.Append(g, "&Game")
        self.SetMenuBar(mb)

    def _build_side(self):
        s = wx.BoxSizer(wx.VERTICAL)
        self.tool_box = wx.RadioBox(self.side, label="Tool",
                                    choices=[TOOL_LABELS[t] for t in TOOLS],
                                    majorDimension=1, style=wx.RA_SPECIFY_COLS)
        self.tool_box.Bind(wx.EVT_RADIOBOX,
                           lambda e: (self.presenter.set_tool(TOOLS[e.GetInt()]),
                                      self.canvas.Refresh()))
        self.layer_box = wx.RadioBox(self.side, label="Layer",
                                     choices=["Wall", "Floor", "Ceiling (0 = sky)"],
                                     majorDimension=1, style=wx.RA_SPECIFY_COLS)
        self.layer_box.Bind(wx.EVT_RADIOBOX,
                            lambda e: self.presenter.set_layer(LAYERS[e.GetInt()]))
        self.mode_box = wx.RadioBox(self.side, label="View",
                                    choices=["Textures", "Light"],
                                    majorDimension=2, style=wx.RA_SPECIFY_COLS)
        self.mode_box.Bind(wx.EVT_RADIOBOX,
                           lambda e: self.presenter.set_view_mode(VIEW_MODES[e.GetInt()]))
        s.Add(self.tool_box, 0, wx.EXPAND | wx.BOTTOM, 4)
        s.Add(self.layer_box, 0, wx.EXPAND | wx.BOTTOM, 4)
        s.Add(self.mode_box, 0, wx.EXPAND | wx.BOTTOM, 4)
        s.Add(wx.StaticText(self.side, label="Textures"), 0, wx.BOTTOM, 2)
        self.tex_list = wx.ListCtrl(self.side, size=(220, 220),
                                    style=wx.LC_ICON | wx.LC_SINGLE_SEL | wx.LC_AUTOARRANGE)
        self.tex_list.Bind(wx.EVT_LIST_ITEM_SELECTED,
                           lambda e: self._on_tex_selected(e.GetIndex()))
        s.Add(self.tex_list, 1, wx.EXPAND | wx.BOTTOM, 4)
        add = wx.Button(self.side, label="Add texture...")
        add.Bind(wx.EVT_BUTTON, self.on_add_texture)
        s.Add(add, 0, wx.EXPAND | wx.BOTTOM, 4)
        s.Add(wx.StaticText(self.side, label="Problems (F7, click to select)"), 0, wx.BOTTOM, 2)
        self.problem_list = wx.ListBox(self.side, size=(220, 110))
        self.problem_list.Bind(wx.EVT_LISTBOX,
                               lambda e: self.presenter.select_problem(e.GetSelection()))
        s.Add(self.problem_list, 0, wx.EXPAND)
        self.side.SetSizer(s)

    def _guard(self, fn):
        """Wrap a widget handler: ignore events raised by selection_changed()
        itself, show model errors instead of raising."""
        def handler(ev):
            if self._syncing:
                return
            try:
                fn(ev)
            except Exception as ex:  # ModelError / ValueError from the presenter
                self.set_status(str(ex))
                self.selection_changed()
        return handler

    def _build_props(self):
        P, pr = self.props, lambda: self.presenter
        s = wx.BoxSizer(wx.VERTICAL)

        # Light
        box = wx.StaticBoxSizer(wx.VERTICAL, P, "Light (Light tool)")
        self.light_field = wx.RadioBox(P, choices=["dark", "dark_alt (after LIGHTS)"],
                                       majorDimension=1, style=wx.RA_SPECIFY_COLS)
        self.light_field.Bind(wx.EVT_RADIOBOX, self._guard(
            lambda e: pr().set_light_field(LIGHT_FIELDS[e.GetInt()])))
        self.light_slider = wx.Slider(P, value=0, minValue=0, maxValue=31,
                                      style=wx.SL_HORIZONTAL | wx.SL_LABELS)
        self.light_slider.Bind(wx.EVT_SLIDER, self._guard(
            lambda e: pr().set_light_value(self.light_slider.GetValue())))
        box.Add(self.light_field, 0, wx.EXPAND)
        box.Add(wx.StaticText(P, label="Darkness 0 (bright) .. 31"), 0, wx.TOP, 4)
        box.Add(self.light_slider, 0, wx.EXPAND)
        s.Add(box, 0, wx.EXPAND | wx.BOTTOM, 6)

        # Cell
        box = wx.StaticBoxSizer(wx.VERTICAL, P, "Cell (Flags tool)")
        self.cell_label = wx.StaticText(P, label="no cell selected")
        box.Add(self.cell_label, 0, wx.BOTTOM, 4)
        grid = wx.GridSizer(2, 2, 6)
        self.flag_checks = {}
        for name in bast.cell_flags():
            cb = wx.CheckBox(P, label=name)
            cb.Bind(wx.EVT_CHECKBOX, self._guard(
                lambda e, n=name: pr().set_cell_flag(n, e.IsChecked())))
            self.flag_checks[name] = cb
            grid.Add(cb)
        box.Add(grid, 0, wx.EXPAND | wx.BOTTOM, 4)
        g2 = wx.FlexGridSizer(2, 4, 4)
        self.cell_tag = wx.SpinCtrl(P, min=0, max=255)
        self.cell_param = wx.SpinCtrl(P, min=0, max=255)
        self.cell_tag.Bind(wx.EVT_SPINCTRL, self._guard(
            lambda e: pr().set_cell_values(tag=self.cell_tag.GetValue())))
        self.cell_param.Bind(wx.EVT_SPINCTRL, self._guard(
            lambda e: pr().set_cell_values(param=self.cell_param.GetValue())))
        self.door_key = wx.Choice(P, choices=list(DOOR_KEYS))
        self.door_mode = wx.Choice(P, choices=list(DOOR_MODES))
        self.door_key.Bind(wx.EVT_CHOICE, self._guard(
            lambda e: pr().set_door(key=e.GetSelection())))
        self.door_mode.Bind(wx.EVT_CHOICE, self._guard(
            lambda e: pr().set_door(mode=e.GetSelection())))
        for lbl, ctl in (("Tag", self.cell_tag), ("Param", self.cell_param),
                         ("Door key", self.door_key), ("Door mode", self.door_mode)):
            g2.Add(wx.StaticText(P, label=lbl), 0, wx.ALIGN_CENTER_VERTICAL)
            g2.Add(ctl, 1, wx.EXPAND)
        box.Add(g2, 0, wx.EXPAND)
        s.Add(box, 0, wx.EXPAND | wx.BOTTOM, 6)

        # Entity
        box = wx.StaticBoxSizer(wx.VERTICAL, P, "Entity (Entity tool)")
        self.ent_types = [t[0] for t in E.TYPES]
        self.ent_place = wx.Choice(P, choices=["%s (%s)" % (t[3], t[2]) for t in E.TYPES])
        self.ent_place.Bind(wx.EVT_CHOICE, self._guard(
            lambda e: pr().set_entity_type(self.ent_types[e.GetSelection()])))
        box.Add(wx.StaticText(P, label="Type to place"), 0)
        box.Add(self.ent_place, 0, wx.EXPAND | wx.BOTTOM, 6)
        self.ent_label = wx.StaticText(P, label="no entity selected")
        box.Add(self.ent_label, 0, wx.BOTTOM, 4)
        g3 = wx.FlexGridSizer(2, 4, 4)
        self.ent_type = wx.Choice(P, choices=[t[3] for t in E.TYPES])
        self.ent_type.Bind(wx.EVT_CHOICE, self._guard(
            lambda e: pr().set_entity_props(type=self.ent_types[e.GetSelection()])))
        self.ent_dir = wx.Choice(P, choices=list(ENT_DIRS))
        self.ent_dir.Bind(wx.EVT_CHOICE, self._guard(
            lambda e: pr().rotate_entity(e.GetSelection())))
        self.ent_tag = wx.SpinCtrl(P, min=0, max=255)
        self.ent_param = wx.SpinCtrl(P, min=0, max=255)
        self.ent_tag.Bind(wx.EVT_SPINCTRL, self._guard(
            lambda e: pr().set_entity_props(tag=self.ent_tag.GetValue())))
        self.ent_param.Bind(wx.EVT_SPINCTRL, self._guard(
            lambda e: pr().set_entity_props(param=self.ent_param.GetValue())))
        self.ent_name = wx.TextCtrl(P, style=wx.TE_PROCESS_ENTER)
        self.ent_name.SetMaxLength(16)
        apply_name = self._guard(lambda e: pr().set_entity_props(name=self.ent_name.GetValue()))
        self.ent_name.Bind(wx.EVT_TEXT_ENTER, apply_name)
        self.ent_name.Bind(wx.EVT_KILL_FOCUS, lambda e: (apply_name(e), e.Skip()))
        for lbl, ctl in (("Type", self.ent_type), ("Facing", self.ent_dir),
                         ("Tag", self.ent_tag), ("Param", self.ent_param),
                         ("Name", self.ent_name)):
            g3.Add(wx.StaticText(P, label=lbl), 0, wx.ALIGN_CENTER_VERTICAL)
            g3.Add(ctl, 1, wx.EXPAND)
        box.Add(g3, 0, wx.EXPAND | wx.BOTTOM, 4)
        self.ent_checks = {}
        for name, lbl in ENT_FLAG_LABELS:
            cb = wx.CheckBox(P, label=lbl)
            cb.Bind(wx.EVT_CHECKBOX, self._guard(
                lambda e, n=name: pr().set_entity_flag(n, e.IsChecked())))
            self.ent_checks[name] = cb
            box.Add(cb, 0, wx.BOTTOM, 2)
        dele = wx.Button(P, label="Delete entity")
        dele.Bind(wx.EVT_BUTTON, self._guard(lambda e: pr().delete_entity()))
        box.Add(dele, 0, wx.EXPAND | wx.TOP, 4)
        s.Add(box, 0, wx.EXPAND)
        P.SetSizer(s)
        P.SetMinSize((320, -1))

    # presenter view interface ---------------------------------------------
    def selection_changed(self):
        if not getattr(self, "_ready", False) and not hasattr(self, "ent_checks"):
            return
        p = self.presenter
        self._syncing = True
        try:
            self.mode_box.SetSelection(VIEW_MODES.index(p.view_mode))
            self.tool_box.SetSelection(TOOLS.index(p.tool))
            self.light_field.SetSelection(LIGHT_FIELDS.index(p.light_field))
            self.light_slider.SetValue(p.light_value)
            self.ent_place.SetSelection(self.ent_types.index(p.entity_type))
            c = p.selected_cell()
            for name, cb in self.flag_checks.items():
                cb.Enable(c is not None)
                cb.SetValue(bool(c and c["flags"] & bast.cell_flags()[name]))
            for ctl in (self.cell_tag, self.cell_param):
                ctl.Enable(c is not None)
            self.cell_label.SetLabel("cell (%d, %d)" % p.cell_sel if c else "no cell selected")
            self.cell_tag.SetValue(c["tag"] if c else 0)
            self.cell_param.SetValue(c["param"] if c else 0)
            door = p.is_door()
            self.door_key.Enable(door)
            self.door_mode.Enable(door)
            self.door_key.SetSelection(door_key(c["param"]) if door else wx.NOT_FOUND)
            self.door_mode.SetSelection(door_mode(c["param"]) if door else wx.NOT_FOUND)

            e = p.selected_entity()
            for ctl in (self.ent_type, self.ent_dir, self.ent_tag, self.ent_param,
                        self.ent_name, *self.ent_checks.values()):
                ctl.Enable(e is not None)
            if e:
                self.ent_label.SetLabel("entity #%d %s at (%.1f, %.1f)" % (
                    p.entity_sel, E.name_of(e["type"]), e["x"] / 65536, e["y"] / 65536))
                if e["type"] in self.ent_types:
                    self.ent_type.SetSelection(self.ent_types.index(e["type"]))
                self.ent_dir.SetSelection(entity_dir(e["angle"]))
                self.ent_tag.SetValue(e["tag"])
                self.ent_param.SetValue(e["param"])
                if self.ent_name.GetValue() != e["name"]:
                    self.ent_name.ChangeValue(e["name"])
                for name, cb in self.ent_checks.items():
                    cb.SetValue(bool(e["flags"] & bast.entity_flags()[name]))
            else:
                self.ent_label.SetLabel("no entity selected")
                self.ent_name.ChangeValue("")
            self.props.Layout()
            self.props.FitInside()
        finally:
            self._syncing = False

    # presenter view interface ---------------------------------------------
    def refresh(self):
        if self._ready:
            self.canvas.Refresh()

    def set_status(self, text):
        self.status.SetStatusText(text, 0)

    def textures_changed(self):
        if not self._ready:
            return
        self._img = wx.ImageList(THUMB, THUMB)
        self.tex_list.DeleteAllItems()
        self.tex_list.SetImageList(self._img, wx.IMAGE_LIST_NORMAL)
        for i, name in enumerate(self.presenter.model.textures):
            bmp = self.tex_bitmap(name, THUMB) if i else None
            if bmp is None:  # no TEX file (or index 0): palette swatch
                bmp = wx.Bitmap(THUMB, THUMB)
                dc = wx.MemoryDC(bmp)
                dc.SetBackground(wx.Brush(wx.Colour(*colors.texture_rgb(i, sky=True))))
                dc.Clear()
                dc.SelectObject(wx.NullBitmap)
            img = self._img.Add(bmp)
            self.tex_list.InsertItem(i, "%d %s" % (i, name or "none/sky"), img)
        self._select_tex_row()
        self.canvas.update_virtual()

    def problems_changed(self):
        if self._ready:
            self.problem_list.Set([str(p) for p in self.presenter.problems] or ["(no problems)"])

    def _select_tex_row(self):
        i = self.presenter.texture
        if 0 <= i < self.tex_list.GetItemCount() and \
                self.tex_list.GetFirstSelected() != i:
            self.tex_list.Select(i)
            self.tex_list.EnsureVisible(i)

    def _on_tex_selected(self, i):
        if i != self.presenter.texture:
            self.presenter.set_texture(i)

    def title_changed(self):
        if self._ready:
            self.SetTitle("VS3D II editor - " + self.presenter.title())
            self._select_tex_row()
            self.status.SetStatusText("%s / %s / tex %d" % (
                self.presenter.tool, self.presenter.layer, self.presenter.texture), 1)

    # commands -------------------------------------------------------------
    def _error(self, e):
        wx.MessageBox(str(e), "Editor", wx.OK | wx.ICON_ERROR, self)

    def _confirm_discard(self):
        if not self.presenter.model.dirty:
            return True
        return wx.MessageBox("Discard unsaved changes?", "Editor",
                             wx.YES_NO | wx.ICON_QUESTION, self) == wx.YES

    def on_new(self, _e):
        if not self._confirm_discard():
            return
        dlg = wx.Dialog(self, title="New map")
        s = wx.FlexGridSizer(2, 4, 4)
        w = wx.SpinCtrl(dlg, min=1, max=64, initial=32)
        h = wx.SpinCtrl(dlg, min=1, max=64, initial=32)
        n = wx.TextCtrl(dlg, value="Untitled")
        for lbl, ctl in (("Width", w), ("Height", h), ("Name", n)):
            s.Add(wx.StaticText(dlg, label=lbl), 0, wx.ALIGN_CENTER_VERTICAL)
            s.Add(ctl, 1, wx.EXPAND)
        outer = wx.BoxSizer(wx.VERTICAL)
        outer.Add(s, 1, wx.EXPAND | wx.ALL, 8)
        outer.Add(dlg.CreateStdDialogButtonSizer(wx.OK | wx.CANCEL), 0, wx.EXPAND | wx.ALL, 8)
        dlg.SetSizerAndFit(outer)
        if dlg.ShowModal() == wx.ID_OK:
            try:
                self.presenter.new_map(w.GetValue(), h.GetValue(), n.GetValue())
            except Exception as e:  # ModelError, bast name length
                self._error(e)
        dlg.Destroy()

    def on_open(self, _e):
        if not self._confirm_discard():
            return
        with wx.FileDialog(self, "Open map", wildcard="Maps (*.vmap)|*.vmap",
                           style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST) as d:
            if d.ShowModal() == wx.ID_OK:
                self.open_path(d.GetPath())

    def open_path(self, path):
        try:
            self.presenter.open(path)
        except Exception as e:
            self._error(e)

    def _validate_before_save(self):
        """Warn about problems; saving an unfinished map stays possible."""
        probs = self.presenter.validate()
        if not probs:
            return True
        text = "\n".join(str(p) for p in probs[:15])
        return wx.MessageBox("The map has %d problem(s):\n\n%s\n\nSave anyway?" % (
            len(probs), text), "Editor", wx.YES_NO | wx.ICON_WARNING, self) == wx.YES

    def on_save(self, e):
        if not self._validate_before_save():
            return
        if not self.presenter.model.path:
            return self.on_save_as(e, validated=True)
        try:
            self.presenter.save()
        except Exception as ex:
            self._error(ex)

    def on_save_as(self, _e=None, validated=False):
        """From the menu, validation runs after the file dialog; from on_save
        it already ran."""
        m = self.presenter.model
        with wx.FileDialog(self, "Save map as", wildcard="Maps (*.vmap)|*.vmap",
                           defaultDir=os.path.dirname(m.path or "") or os.getcwd(),
                           style=wx.FD_SAVE | wx.FD_OVERWRITE_PROMPT) as d:
            if d.ShowModal() == wx.ID_OK and (validated or self._validate_before_save()):
                path = d.GetPath()
                if not path.endswith(".vmap"):
                    path += ".vmap"
                try:
                    self.presenter.save(path)
                except Exception as ex:
                    self._error(ex)

    def on_add_texture(self, _e):
        """Pick one of the built textures (assets/gen), or type any name."""
        have = set(self.presenter.model.textures)
        names = [n for n in self.textures.available() if n not in have]
        other = "Other name..."
        name = None
        with wx.SingleChoiceDialog(self, "Texture", "Add texture", names + [other]) as d:
            if d.ShowModal() != wx.ID_OK:
                return
            name = d.GetStringSelection()
        if name == other:
            with wx.TextEntryDialog(self, "Texture name (max 16 bytes)", "Add texture") as d:
                if d.ShowModal() != wx.ID_OK:
                    return
                name = d.GetValue()
        try:
            self.presenter.add_texture(name)
        except Exception as ex:
            self._error(ex)

    # game -----------------------------------------------------------------
    def on_validate(self, _e):
        self.presenter.validate()

    def on_launch(self, _e):
        try:
            self.presenter.launch()
        except Exception as ex:  # LaunchError, ModelError
            self._error(ex)
            return
        self.launch_timer.Start(300)

    def on_launch_timer(self, _e):
        p = self.presenter
        err = p.poll_launch()
        if not p.launching:
            self.launch_timer.Stop()
        if err:
            self._error("The game failed:\n" + err)

    def on_launch_headless(self, _e):
        try:
            with wx.BusyCursor():
                frame = self.presenter.launch_headless()
        except Exception as ex:
            self._error(ex)
            return
        self.show_frame(frame)

    def show_frame(self, path):
        """Last headless frame at 2x, nearest neighbour."""
        img = wx.Image(path)
        img = img.Scale(img.GetWidth() * 2, img.GetHeight() * 2, wx.IMAGE_QUALITY_NEAREST)
        dlg = wx.Dialog(self, title="Headless test: tic %d" % L.HEADLESS_TICS)
        s = wx.BoxSizer(wx.VERTICAL)
        s.Add(wx.StaticBitmap(dlg, bitmap=img.ConvertToBitmap()), 0, wx.ALL, 6)
        s.Add(wx.StaticText(dlg, label=path), 0, wx.LEFT | wx.RIGHT, 6)
        s.Add(dlg.CreateStdDialogButtonSizer(wx.OK), 0, wx.EXPAND | wx.ALL, 6)
        dlg.SetSizerAndFit(s)
        self.frame_dialog = dlg
        dlg.Show()

    def on_game_path(self, _e):
        p = self.presenter
        with wx.FileDialog(self, "Game executable", defaultFile=p.game_path,
                           style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST) as d:
            if d.ShowModal() == wx.ID_OK:
                p.game_path = d.GetPath()
                self.set_status("game: " + p.game_path)

    def on_close(self, e):
        if e.CanVeto() and not self._confirm_discard():
            e.Veto()
            return
        e.Skip()
