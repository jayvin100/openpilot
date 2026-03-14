import requests
import threading
import time
import pyray as rl

from openpilot.common.api import api_get
from openpilot.common.params import Params
from openpilot.common.swaglog import cloudlog
from openpilot.selfdrive.ui.lib.api_helpers import get_token
from openpilot.selfdrive.ui.ui_state import ui_state, device
from openpilot.system.athena.registration import UNREGISTERED_DONGLE_ID
from openpilot.system.ui.lib.application import gui_app
from openpilot.system.ui.lib.multilang import tr, trn, tr_noop
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.widgets.scroller import NavScroller
from openpilot.selfdrive.ui.mici.widgets.button import GreyBigButton

FAQ_ITEMS = [
  (tr_noop("Does it matter how or\nwhere I drive?"), tr_noop("Nope, just drive as you\nnormally would.")),
  (tr_noop("Do all of my segments\nget pulled?"), tr_noop("No, we selectively pull a\nsubset of your segments.")),
  (tr_noop("What's a good\nUSB-C adapter?"), tr_noop("Any fast phone or laptop\ncharger should be fine.")),
  (tr_noop("Does it matter which\nsoftware I run?"), tr_noop("Yes, only upstream openpilot\n(and particular forks) can\nbe used for training.")),
]


class FirehoseLayoutBase(Widget):
  PARAM_KEY = "ApiCache_FirehoseStats"
  GREEN = rl.Color(46, 204, 113, 255)
  RED = rl.Color(231, 76, 60, 255)
  GRAY = rl.Color(68, 68, 68, 255)
  LIGHT_GRAY = rl.Color(228, 228, 228, 255)
  UPDATE_INTERVAL = 30

  def __init__(self):
    super().__init__()
    self._params = Params()
    self._session = requests.Session()
    self._segment_count = self._get_segment_count()

    self._running = True
    self._update_thread = threading.Thread(target=self._update_loop, daemon=True)
    self._update_thread.start()

  def __del__(self):
    self._running = False
    try:
      if self._update_thread and self._update_thread.is_alive():
        self._update_thread.join(timeout=1.0)
    except Exception:
      pass

  def _get_segment_count(self) -> int:
    stats = self._params.get(self.PARAM_KEY)
    if not stats:
      return 0
    try:
      return int(stats.get("firehose", 0))
    except Exception:
      cloudlog.exception(f"Failed to decode firehose stats: {stats}")
      return 0

  def _get_status(self) -> tuple[str, rl.Color]:
    network_type = ui_state.sm["deviceState"].networkType
    network_metered = ui_state.sm["deviceState"].networkMetered

    if not network_metered and network_type != 0:
      return tr("ACTIVE"), self.GREEN
    else:
      return tr("INACTIVE: connect to an unmetered network"), self.RED

  def _fetch_firehose_stats(self):
    try:
      dongle_id = self._params.get("DongleId")
      if not dongle_id or dongle_id == UNREGISTERED_DONGLE_ID:
        return
      identity_token = get_token(dongle_id)
      response = api_get(f"v1/devices/{dongle_id}/firehose_stats", access_token=identity_token, session=self._session)
      if response.status_code == 200:
        data = response.json()
        self._segment_count = data.get("firehose", 0)
        self._params.put(self.PARAM_KEY, data)
    except Exception as e:
      cloudlog.error(f"Failed to fetch firehose stats: {e}")

  def _update_loop(self):
    while self._running:
      if not ui_state.started and device._awake:
        self._fetch_firehose_stats()
      time.sleep(self.UPDATE_INTERVAL)


class FirehoseLayout(NavScroller, FirehoseLayoutBase):
  def __init__(self):
    super().__init__()

    self._status_card = GreyBigButton("", "")
    self._contrib_card = GreyBigButton("", "")
    self._contrib_card.set_visible(False)

    self._scroller.add_widgets([
      GreyBigButton("Firehose Mode", "maximize your\ntraining data uploads",
                     gui_app.texture("icons_mici/settings/firehose.png", 52, 62)),
      GreyBigButton("", "openpilot learns to drive by watching humans, like you, drive."),
      GreyBigButton("", "More data means bigger models, which means better Experimental Mode."),
      self._status_card,
      self._contrib_card,
      GreyBigButton("tips for maximum uploads", "bring your device inside and connect to USB-C and Wi-Fi weekly",
                     gui_app.texture("icons_mici/setup/green_info.png", 64, 64)),
      GreyBigButton("", "Firehose Mode can also work while driving if connected to a hotspot or unlimited SIM card."),
      GreyBigButton("FAQ", ""),
    ] + [GreyBigButton(tr(q), tr(a)) for q, a in FAQ_ITEMS])

  def _render(self, rect):
    self._update_dynamic_cards()
    super()._render(rect)

  def _update_dynamic_cards(self):
    network_type = ui_state.sm["deviceState"].networkType
    network_metered = ui_state.sm["deviceState"].networkMetered

    if not network_metered and network_type != 0:
      self._status_card.set_text("ACTIVE")
      self._status_card.set_value("uploading training data")
    else:
      self._status_card.set_text("INACTIVE")
      self._status_card.set_value("Connect to an unmetered network to upload.")

    if self._segment_count > 0:
      self._contrib_card.set_visible(True)
      self._contrib_card.set_value(trn("{} segment in the\ntraining dataset so far",
                                       "{} segments in the\ntraining dataset so far",
                                       self._segment_count).format(self._segment_count))
    else:
      self._contrib_card.set_visible(False)
