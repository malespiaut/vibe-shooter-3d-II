"""Vibe Shooter 3D II map editor (wxPython, Model-View-Presenter).

model.py     map data through tools/bast.py (libbast), undo/redo of cell edits
presenter.py tools, selection and commands; never imports wx
view.py      wx frame, grid canvas, texture panel

Run: python3 -m editor [file.vmap]
"""
