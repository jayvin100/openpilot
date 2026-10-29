"""Keep the stock offroad updater; use a read-only check onroad."""
import threading

from openpilot.common.basedir import BASEDIR
from openpilot.jay.guard import guarded
from openpilot.jay.model_info import model_info_items
from openpilot.jay.update_check import check_remote
from openpilot.selfdrive.ui.layouts.settings.software import SoftwareLayout
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.multilang import tr
from openpilot.system.ui.widgets.scroller_tici import Scroller


class JaySoftwareLayout(SoftwareLayout):
  def __init__(self):
    super().__init__()
    self._check_thread = None
    self._check_status = "Not checked yet"
    items = self._scroller._items
    self._scroller = Scroller([*items[:2], *model_info_items(), *items[2:]], line_separator=True, spacing=0)

  @guarded()
  def _update_state(self):
    super()._update_state()
    if ui_state.is_onroad():
      self._download_btn.set_visible(True)
      self._download_btn.action_item.set_text(tr("CHECK"))
      self._download_btn.action_item.set_value(self._check_status)
      self._download_btn.action_item.set_enabled(self._check_thread is None or not self._check_thread.is_alive())

  @guarded()
  def _on_download_update(self):
    if ui_state.is_offroad():
      return super()._on_download_update()
    if self._check_thread is not None and self._check_thread.is_alive():
      return
    branch = ui_state.params.get("UpdaterTargetBranch") or ui_state.params.get("GitBranch") or ""
    self._check_status = "Checking..."

    def worker():
      try:
        self._check_status = check_remote(BASEDIR, branch)
      except Exception:
        self._check_status = "Check failed - retry"

    self._check_thread = threading.Thread(target=worker, daemon=True)
    self._check_thread.start()
