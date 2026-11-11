"""python3 -m editor [file.vmap]"""
import sys

import wx

from .model import MapModel, ModelError
from .view import EditorFrame


def main(argv):
    app = wx.App(False)
    model = None
    if len(argv) > 1:
        try:
            model = MapModel.open(argv[1])
        except ModelError as e:
            print("editor: %s" % e, file=sys.stderr)
            return 1
    frame = EditorFrame(model)
    frame.Show()
    app.MainLoop()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
