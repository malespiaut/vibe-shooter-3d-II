import os, subprocess, sys
sys.path.insert(0, "/work")
import wx
from editor.model import MapModel
from editor.view import EditorFrame

out = sys.argv[1]
app = wx.App(False)
reopen = len(sys.argv) > 2
f = EditorFrame(MapModel.open(sys.argv[2] if reopen else "/work/maps/m1test.vmap"))
f.SetPosition((0, 0)); f.SetSize((1024, 760)); f.Show()
sim = wx.UIActionSimulator()

def screen_of(cx, cy):
    c = f.canvas; z = c.zoom; m = f.presenter.model
    px, py = c.CalcScrolledPosition(cx * z + z // 2, (m.height - 1 - cy) * z + z // 2)
    return c.ClientToScreen(wx.Point(px, py))

def drag(a, b):
    sim.MouseMove(screen_of(*a)); wx.Yield()
    sim.MouseDown(); wx.Yield()
    sim.MouseMove(screen_of(*b)); wx.Yield()
    sim.MouseUp(); wx.Yield()

steps = []
def step():
    p = f.presenter
    if not steps and reopen:
        f.canvas.set_zoom(24); p.on_hover(14, 9); steps.append(1); wx.CallLater(800, step)
    elif not steps:
        f.canvas.set_zoom(24)
        tex = p.model.textures
        # walls of a room in the SE hall via the Tool/Layer widgets' presenter calls
        f.tool_box.SetSelection(1); p.set_tool("rect")
        f.layer_box.SetSelection(0); p.set_layer("wall")
        p.set_texture(tex.index("test03"))
        drag((11, 3), (17, 9))
        f.tool_box.SetSelection(3); p.set_tool("erase")
        p.set_tool("rect"); f.tool_box.SetSelection(1)
        p.set_texture(0)
        drag((12, 4), (16, 8))           # hollow the room out
        p.set_tool("erase"); f.tool_box.SetSelection(3)
        drag((14, 9), (14, 9))           # doorway on the north wall
        f.layer_box.SetSelection(1); p.set_layer("floor")
        p.set_tool("rect"); f.tool_box.SetSelection(1)
        p.set_texture(tex.index("test06"))
        drag((12, 4), (16, 8))           # different floor inside
        f.layer_box.SetSelection(2); p.set_layer("ceil")
        p.set_texture(tex.index("test05"))
        drag((12, 4), (16, 8))           # roof it (erased pillars were sky)
        f.layer_box.SetSelection(0); p.set_layer("wall")
        p.on_hover(14, 6)
        p.save(out)
        steps.append(1)
        wx.CallLater(800, step)
    else:
        subprocess.run(["import", "-window", "root", "/tmp/editor_reopen.png" if reopen else "/tmp/editor_shot.png"], check=True)
        f.presenter.model.dirty = False
        f.Close()

wx.CallLater(800, step)
app.MainLoop()
