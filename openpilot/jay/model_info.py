"""Identify the installed comma 3X driving model without loading or executing it."""
import pickletools
import re
import struct
from dataclasses import dataclass
from pathlib import Path

CHECKPOINT_PATTERN = re.compile(r"[0-9a-f-]{36}/\d+(?:/[0-9a-f-]{36}/\d+)*")

# Exact vision + policy checkpoint match, verified against both ONNX artifacts in:
# https://github.com/commaai/openpilot/commit/5b6436a90cf6902b8aaa71c2b6f3d7164d8ae391
# Dates are the upstream merge dates (UTC), not compilation or installation dates.
RELEASES = {
  "6a7d09ad-bcc9-43bc-916d-29287e60cee2/200/a27b3122-733e-4a65-938b-acfebebbe5e8/100":
    ("CD210", "4 Feb 2026"),
}


@dataclass(frozen=True)
class ModelInfo:
  name: str = "Unavailable"
  released: str = "Unavailable"
  checkpoint: str = ""


def read_checkpoint(path: Path) -> str:
  # The tinygrad artifact starts with an int64 opcode length followed by pickle
  # opcodes and then GPU buffers. Inspect only the metadata prefix, never unpickle.
  with path.open("rb") as f:
    size, = struct.unpack("<q", f.read(8))
    if size <= 0:
      return ""
    prefix = f.read(min(size, 65536))
  found = False
  for op, value, _ in pickletools.genops(prefix):
    if op.name in ("SHORT_BINUNICODE", "BINUNICODE", "BINUNICODE8", "UNICODE"):
      if found:
        return value if CHECKPOINT_PATTERN.fullmatch(value) else ""
      found = value == "model_checkpoint"
  return ""


def installed_model(path: Path) -> ModelInfo:
  try:
    checkpoint = read_checkpoint(path)
  except (OSError, ValueError, struct.error):
    return ModelInfo()
  if not checkpoint:
    return ModelInfo()
  name, released = RELEASES.get(checkpoint, ("comma driving model", "Unavailable"))
  return ModelInfo(name, released, checkpoint)


def model_info_items():
  from openpilot.common.basedir import BASEDIR
  from openpilot.system.ui.widgets.list_view import text_item

  info = installed_model(Path(BASEDIR) / "openpilot/selfdrive/modeld/models/driving_tinygrad.pkl")
  parts = info.checkpoint.split("/")
  short_id = " + ".join(f"{parts[i][:8]}/{parts[i+1]}" for i in range(0, len(parts) - 1, 2))
  return [
    text_item("Driving Model", info.name,
              description="The driving model bundled with your installed comma 3X build. "
                          "Changes automatically with openpilot updates. " + (f"Checkpoint: {short_id}." if short_id else "Model metadata unavailable.")),
    text_item("Model Released", info.released,
              description="Date comma merged this model into openpilot (UTC). "
                          "Shown only when verified for the installed checkpoint; independent of your software update date."),
  ]
