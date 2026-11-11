"""Editor view: wx widgets only. Forwards input to the presenter and draws
what the presenter / model expose. North is up: map y grows upward."""
import os

import wx

from . import colors
from .presenter import Presenter, TOOLS, LAYERS

TOOL_LABELS = {"paint": "Paint", "rect": "Rectangle", "eyedropper": "Eyedropper",
               "erase": "Erase"}


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
        layer = p.layer
        dc.SetPen(wx.Pen(wx.Colour(0, 0, 0)))
        for y in range(m.height):
            sy = (m.height - 1 - y) * z
            for x in range(m.width):
                c = m.cell(x, y)
                sx = x * z
                if c["wall"]:
                    rgb = colors.texture_rgb(c["wall"])
                    dc.SetBrush(wx.Brush(wx.Colour(*rgb)))
                    dc.DrawRectangle(sx, sy, z + 1, z + 1)
                    if layer == "wall" and z >= 8:  # mark solid blocks
                        dc.DrawLine(sx + 2, sy + 2, sx + z - 1, sy + z - 1)
                    continue
                val = c["ceil"] if layer == "ceil" else c["floor"]
                rgb = colors.texture_rgb(val, sky=(layer == "ceil"))
                dc.SetBrush(wx.Brush(wx.Colour(*rgb)))
                dc.DrawRectangle(sx, sy, z + 1, z + 1)
                if layer != "ceil" and c["ceil"] == 0 and z >= 8:  # sky marker
                    dc.SetBrush(wx.Brush(wx.Colour(*colors.SKY_RGB)))
                    dc.DrawPolygon([(sx + z - z // 3, sy + 1), (sx + z, sy + 1),
                                    (sx + z, sy + z // 3)])
        if p.selection:
            x0, y0, x1, y1 = p.selection
            lx, hx = min(x0, x1), max(x0, x1)
            ly, hy = min(y0, y1), max(y0, y1)
            dc.SetBrush(wx.TRANSPARENT_BRUSH)
            dc.SetPen(wx.Pen(wx.Colour(255, 230, 60), 2))
            dc.DrawRectangle(lx * z, (m.height - 1 - hy) * z, (hx - lx + 1) * z + 1,
                             (hy - ly + 1) * z + 1)


class EditorFrame(wx.Frame):
    """Implements the presenter's view interface."""

    def __init__(self, model=None):
        super().__init__(None, size=(1024, 720))
        self.presenter = None
        self.status = self.CreateStatusBar(2)
        self.status.SetStatusWidths([-3, -1])
        self._build_menu()
        root = wx.Panel(self)
        self.side = wx.Panel(root)
        self._ready = False
        self.presenter = Presenter(model, self)
        self.canvas = GridCanvas(root, self)
        self._build_side()
        s = wx.BoxSizer(wx.HORIZONTAL)
        s.Add(self.side, 0, wx.EXPAND | wx.ALL, 4)
        s.Add(self.canvas, 1, wx.EXPAND)
        root.SetSizer(s)
        self._ready = True
        self.textures_changed()
        self.title_changed()
        self.Bind(wx.EVT_CLOSE, self.on_close)

    # layout ---------------------------------------------------------------
    def _build_menu(self):
        mb = wx.MenuBar()
        f, e, v = wx.Menu(), wx.Menu(), wx.Menu()
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
        ]
        for menu, wid, label, fn in items:
            menu.Append(wid, label)
            self.Bind(wx.EVT_MENU, fn, id=wid)
        mb.Append(f, "&File")
        mb.Append(e, "&Edit")
        mb.Append(v, "&View")
        self.SetMenuBar(mb)

    def _build_side(self):
        s = wx.BoxSizer(wx.VERTICAL)
        self.tool_box = wx.RadioBox(self.side, label="Tool",
                                    choices=[TOOL_LABELS[t] for t in TOOLS],
                                    majorDimension=1, style=wx.RA_SPECIFY_COLS)
        self.tool_box.Bind(wx.EVT_RADIOBOX,
                           lambda e: self.presenter.set_tool(TOOLS[e.GetInt()]))
        self.layer_box = wx.RadioBox(self.side, label="Layer",
                                     choices=["Wall", "Floor", "Ceiling (0 = sky)"],
                                     majorDimension=1, style=wx.RA_SPECIFY_COLS)
        self.layer_box.Bind(wx.EVT_RADIOBOX,
                            lambda e: self.presenter.set_layer(LAYERS[e.GetInt()]))
        s.Add(self.tool_box, 0, wx.EXPAND | wx.BOTTOM, 4)
        s.Add(self.layer_box, 0, wx.EXPAND | wx.BOTTOM, 4)
        s.Add(wx.StaticText(self.side, label="Textures"), 0, wx.BOTTOM, 2)
        self.tex_list = wx.ListCtrl(self.side, size=(200, 300),
                                    style=wx.LC_REPORT | wx.LC_SINGLE_SEL)
        self.tex_list.InsertColumn(0, "#", width=44)
        self.tex_list.InsertColumn(1, "name", width=140)
        self.tex_list.Bind(wx.EVT_LIST_ITEM_SELECTED,
                           lambda e: self._on_tex_selected(e.GetIndex()))
        s.Add(self.tex_list, 1, wx.EXPAND | wx.BOTTOM, 4)
        add = wx.Button(self.side, label="Add texture...")
        add.Bind(wx.EVT_BUTTON, self.on_add_texture)
        s.Add(add, 0, wx.EXPAND)
        self.side.SetSizer(s)

    # presenter view interface ---------------------------------------------
    def refresh(self):
        if self._ready:
            self.canvas.Refresh()

    def set_status(self, text):
        self.status.SetStatusText(text, 0)

    def textures_changed(self):
        if not self._ready:
            return
        self._img = wx.ImageList(16, 16)
        self.tex_list.DeleteAllItems()
        self.tex_list.SetImageList(self._img, wx.IMAGE_LIST_SMALL)
        for i, name in enumerate(self.presenter.model.textures):
            bmp = wx.Bitmap(16, 16)
            dc = wx.MemoryDC(bmp)
            dc.SetBackground(wx.Brush(wx.Colour(*colors.texture_rgb(i, sky=True))))
            dc.Clear()
            dc.SelectObject(wx.NullBitmap)
            img = self._img.Add(bmp)
            self.tex_list.InsertItem(i, str(i), img)
            self.tex_list.SetItem(i, 1, name or "(none / sky)")
        self._select_tex_row()
        self.canvas.update_virtual()

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

    def on_save(self, e):
        if not self.presenter.model.path:
            return self.on_save_as(e)
        try:
            self.presenter.save()
        except Exception as ex:
            self._error(ex)

    def on_save_as(self, _e):
        m = self.presenter.model
        with wx.FileDialog(self, "Save map as", wildcard="Maps (*.vmap)|*.vmap",
                           defaultDir=os.path.dirname(m.path or "") or os.getcwd(),
                           style=wx.FD_SAVE | wx.FD_OVERWRITE_PROMPT) as d:
            if d.ShowModal() == wx.ID_OK:
                path = d.GetPath()
                if not path.endswith(".vmap"):
                    path += ".vmap"
                try:
                    self.presenter.save(path)
                except Exception as ex:
                    self._error(ex)

    def on_add_texture(self, _e):
        with wx.TextEntryDialog(self, "Texture name (max 16 bytes)", "Add texture") as d:
            if d.ShowModal() == wx.ID_OK:
                try:
                    self.presenter.add_texture(d.GetValue())
                except Exception as ex:
                    self._error(ex)

    def on_close(self, e):
        if e.CanVeto() and not self._confirm_discard():
            e.Veto()
            return
        e.Skip()
