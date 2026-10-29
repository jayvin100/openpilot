"""Remote screenshots for scripts/device.py: create REQUEST_PATH and the UI saves its next frame to SCREENSHOT_PATH."""
import os

import pyray as rl

REQUEST_PATH = "/tmp/jay_screenshot_request"
SCREENSHOT_PATH = "/tmp/jay_screenshot.png"


def maybe_save_screenshot() -> None:
  if not os.path.exists(REQUEST_PATH):
    return
  try:
    os.remove(REQUEST_PATH)
    rl.rl_draw_render_batch_active()  # flush batched draw calls, or the frame is only partly drawn
    image = rl.load_image_from_screen()
    tmp = SCREENSHOT_PATH.removesuffix(".png") + ".tmp.png"
    rl.export_image(image, tmp)
    rl.unload_image(image)
    os.replace(tmp, SCREENSHOT_PATH)
  except Exception:
    pass
