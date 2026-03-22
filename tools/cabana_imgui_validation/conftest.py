"""
Shared pytest fixtures for cabana validation.
"""

import subprocess
from pathlib import Path

import pytest

from .helpers import CABANA_BIN, DEMO_ROUTE, XvfbCabana  # noqa: TID251

ROOT = Path(__file__).resolve().parents[2]


def pytest_sessionstart(session):
  """Build the app binary once on the controller process before tests start."""
  if hasattr(session.config, "workerinput"):
    return

  subprocess.run(
    ["scons", "-j4", "tools/cabana_imgui/_cabana_imgui"],
    cwd=ROOT,
    check=True,
  )

  if not Path(CABANA_BIN).exists():
    raise RuntimeError(f"Cabana binary not found after build: {CABANA_BIN}")


@pytest.fixture
def cabana_no_route():
  """Launch cabana_imgui with no route."""
  with XvfbCabana(args=[], timeout=15) as c:
    yield c


@pytest.fixture
def cabana_demo():
  """Launch cabana_imgui with demo route loaded."""
  with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
    yield c
