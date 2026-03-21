"""
Shared pytest fixtures for cabana validation.
"""

import pytest
from tools.cabana_imgui_validation.helpers import XvfbCabana, DEMO_ROUTE


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
