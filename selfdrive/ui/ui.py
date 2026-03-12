#!/usr/bin/env python3
import os

from openpilot.system.hardware import TICI
from openpilot.common.realtime import config_realtime_process, set_core_affinity
from openpilot.system.ui.lib.application import gui_app
from openpilot.selfdrive.ui.layouts.main import MainLayout
from openpilot.selfdrive.ui.mici.layouts.main import MiciMainLayout
from openpilot.selfdrive.ui.ui_state import ui_state

BIG_UI = gui_app.big_ui()


def main():
  import time
  _t = time.monotonic
  _t_start = _t()

  cores = {5, }
  config_realtime_process(0, 51)

  gui_app.init_window("UI")

  _ts = _t()
  if BIG_UI:
    MainLayout()
  else:
    MiciMainLayout()
  print(f"STARTUP: layout construction   {(_t()-_ts)*1000:7.1f}ms  ({gui_app._texture_load_count} textures, {gui_app._texture_load_time*1000:.1f}ms)")
  print(f"STARTUP: pre-render TOTAL      {(_t()-_t_start)*1000:7.1f}ms")

  _first_frame = True
  for should_render in gui_app.render():
    ui_state.update()
    if should_render:
      if _first_frame:
        print(f"STARTUP: first frame TOTAL     {(_t()-_t_start)*1000:7.1f}ms")
        _first_frame = False
      # reaffine after power save offlines our core
      if TICI and os.sched_getaffinity(0) != cores:
        try:
          set_core_affinity(list(cores))
        except OSError:
          pass


if __name__ == "__main__":
  main()
