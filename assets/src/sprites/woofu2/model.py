"""Woofu phase-2 set (`woofu2_*`): the Woofu model with the sarcophagus lid
gone (enemies.md, Boss phase 2). Runs ../woofu/model.py with --lidless, which
writes <out_dir>/woofu2.blend and copies this directory's sprite.json.

    blender -b --factory-startup --python assets/src/sprites/woofu2/model.py -- <out_dir>
"""
import os
import runpy
import sys

sys.argv.append("--lidless")
runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "woofu", "model.py"),
               run_name="__main__")
