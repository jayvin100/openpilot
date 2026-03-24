#!/usr/bin/env python3
"""Fuzz test for jotpluggler — hammers the UI with random interactions
and checks for crashes (process dies) and freezes (screenshot timeout).

Each test instance launches its own Xvfb + jotpluggler process, so all
instances can run in parallel across pytest-xdist workers.

Run:
  python -m pytest tools/jotpluggler/test_jotpluggler.py -v -s -n 12 --tb=short
"""
from __future__ import annotations

import os
import random
import re
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

import pytest

BINARY = Path(__file__).resolve().parent / "jotpluggler"
WIDTH = 1280
HEIGHT = 720

DEMO_ROUTE_FULL = "5beb9b58bd12b691/0000010a--a51155e496"
DEMO_ROUTE_QUICK = f"{DEMO_ROUTE_FULL}/0/q"
QUICK_ROUTE_PROB = 0.90  # 90% chance of using the quick single-segment route

FUZZ_ACTIONS = 300
SCREENSHOT_EVERY = 30
FREEZE_TIMEOUT_S = 8.0
SETTLE_AFTER_START_S = 2.0

ROUNDS_PER_SCENARIO = 10  # 10 rounds × 2 scenarios = 20 total tests

# interesting key combos to fuzz
KEYS = [
    "Escape", "Return", "Tab", "space", "Delete", "BackSpace",
    "Up", "Down", "Left", "Right",
    "ctrl+z", "ctrl+y", "ctrl+s", "ctrl+a", "ctrl+c", "ctrl+v",
    "Home", "End", "Page_Up", "Page_Down",
    "F1", "F5", "F11",
    "plus", "minus",
]

MOUSE_BUTTONS = [1, 1, 1, 1, 3]  # heavily bias left-click, occasional right-click


# ---------------------------------------------------------------------------
# Scenario definitions — add new scenarios here
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Scenario:
    name: str
    cabana: bool = False
    actions: int = FUZZ_ACTIONS


SCENARIOS = [
    Scenario("plot"),
    Scenario("cabana", cabana=True),
]


def _make_fuzz_params() -> list:
    params = []
    for scenario in SCENARIOS:
        for r in range(ROUNDS_PER_SCENARIO):
            params.append(pytest.param(scenario, r, id=f"{scenario.name}-r{r:02d}"))
    return params


# ---------------------------------------------------------------------------
# Xvfb + jotpluggler session (fully isolated per test)
# ---------------------------------------------------------------------------

class Session:
    def __init__(self) -> None:
        self.xvfb: subprocess.Popen | None = None
        self.jotp: subprocess.Popen | None = None
        self.display: str = ""
        self.winid: str = ""
        self.env: dict[str, str] = {}
        self.tmpdir = Path(tempfile.mkdtemp(prefix="jotp_fuzz_"))
        self._stdout_fh = None
        self._stderr_fh = None

    def start(self, *, route: str, cabana: bool = False) -> None:
        # Xvfb — each instance gets its own display via -displayfd
        xvfb_cmd = [
            "Xvfb", "-displayfd", "1",
            "-screen", "0", f"{WIDTH}x{HEIGHT}x24",
            "-dpi", "96", "-nolisten", "tcp",
        ]
        self.xvfb = subprocess.Popen(xvfb_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert self.xvfb.stdout is not None
        display_num = self.xvfb.stdout.readline().strip()
        if not display_num:
            raise RuntimeError("Xvfb failed to allocate display")
        self.display = f":{display_num}"

        self.env = os.environ.copy()
        self.env["DISPLAY"] = self.display
        if cabana:
            self.env["JOTP_START_CABANA"] = "1"

        jotp_cmd = [str(BINARY), "--show", "--width", str(WIDTH), "--height", str(HEIGHT), route]

        self._stdout_fh = open(self.tmpdir / "stdout.log", "w")
        self._stderr_fh = open(self.tmpdir / "stderr.log", "w")
        self.jotp = subprocess.Popen(jotp_cmd, env=self.env, stdout=self._stdout_fh, stderr=self._stderr_fh)

        # wait for the window to appear
        proc = subprocess.run(
            ["xdotool", "search", "--sync", "--name", "jotpluggler"],
            env=self.env, capture_output=True, text=True, timeout=30,
        )
        wids = proc.stdout.strip().splitlines()
        if not wids:
            raise RuntimeError("jotpluggler window never appeared")
        self.winid = wids[0]

        # position + resize
        self._xdo("windowmove", self.winid, "0", "0")
        self._xdo("windowsize", "--sync", self.winid, str(WIDTH), str(HEIGHT))
        self._xdo("windowfocus", "--sync", self.winid)
        time.sleep(SETTLE_AFTER_START_S)

    def stop(self) -> None:
        for proc in (self.jotp, self.xvfb):
            if proc and proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
        for fh in (self._stdout_fh, self._stderr_fh):
            if fh:
                fh.close()
        if self.xvfb:
            if self.xvfb.stdout:
                self.xvfb.stdout.close()
            if self.xvfb.stderr:
                self.xvfb.stderr.close()

    @property
    def alive(self) -> bool:
        return self.jotp is not None and self.jotp.poll() is None

    @property
    def exit_code(self) -> int | None:
        return self.jotp.poll() if self.jotp else None

    def stderr_tail(self, n: int = 30) -> str:
        p = self.tmpdir / "stderr.log"
        if not p.exists():
            return ""
        return "\n".join(p.read_text(errors="replace").splitlines()[-n:])

    def crash_reason(self) -> str:
        """Extract the crash assertion / signal from stderr."""
        text = (self.tmpdir / "stderr.log").read_text(errors="replace") if (self.tmpdir / "stderr.log").exists() else ""
        # look for assertion messages
        for pattern in [
            r"Assertion.*failed",
            r"SIGSEGV|SIGABRT|SIGFPE|SIGBUS",
            r"AddressSanitizer",
            r"runtime error",
        ]:
            m = re.search(pattern, text)
            if m:
                # grab the full line
                start = text.rfind("\n", 0, m.start()) + 1
                end = text.find("\n", m.end())
                if end == -1:
                    end = len(text)
                return text[start:end].strip()
        return f"exit code {self.exit_code}"

    # -- primitives ----------------------------------------------------------

    def _xdo(self, *args: str, timeout: float = 5.0) -> None:
        subprocess.run(["xdotool", *args], env=self.env, check=True, timeout=timeout)

    def click(self, x: int, y: int, button: int = 1) -> str:
        self._xdo("mousemove", str(x), str(y))
        time.sleep(0.02)
        self._xdo("click", str(button))
        time.sleep(0.05)
        return f"click at ({x}, {y}) button={button}"

    def doubleclick(self, x: int, y: int) -> str:
        self._xdo("mousemove", str(x), str(y))
        time.sleep(0.02)
        self._xdo("click", "--repeat", "2", "--delay", "80", "1")
        time.sleep(0.05)
        return f"doubleclick at ({x}, {y})"

    def drag(self, x1: int, y1: int, x2: int, y2: int) -> str:
        steps = 6
        duration = 0.12
        self._xdo("mousemove", str(x1), str(y1))
        time.sleep(0.02)
        self._xdo("mousedown", "1")
        for i in range(1, steps + 1):
            a = i / steps
            self._xdo("mousemove", str(round(x1 + (x2 - x1) * a)), str(round(y1 + (y2 - y1) * a)))
            time.sleep(duration / steps)
        self._xdo("mouseup", "1")
        time.sleep(0.05)
        return f"drag ({x1}, {y1}) -> ({x2}, {y2})"

    def scroll(self, x: int, y: int, clicks: int) -> str:
        self._xdo("mousemove", str(x), str(y))
        btn = "4" if clicks > 0 else "5"
        self._xdo("click", "--repeat", str(abs(clicks)), btn)
        time.sleep(0.05)
        return f"scroll at ({x}, {y}) clicks={clicks}"

    def key(self, keys: str) -> str:
        self._xdo("key", keys)
        time.sleep(0.05)
        return f"key {keys}"

    def type_text(self, text: str) -> str:
        self._xdo("type", "--clearmodifiers", text)
        time.sleep(0.05)
        return f"type {text!r}"

    def mousemove(self, x: int, y: int) -> str:
        self._xdo("mousemove", str(x), str(y))
        return f"mousemove to ({x}, {y})"

    def screenshot(self, path: Path) -> float:
        """Take screenshot, return elapsed seconds."""
        t0 = time.monotonic()
        xwd = Path(tempfile.mktemp(suffix=".xwd"))
        try:
            subprocess.run(
                ["xwd", "-silent", "-id", self.winid, "-out", str(xwd)],
                env=self.env, check=True, timeout=FREEZE_TIMEOUT_S,
            )
            subprocess.run(
                ["convert", str(xwd), str(path)],
                env=self.env, check=True, timeout=FREEZE_TIMEOUT_S,
            )
        finally:
            xwd.unlink(missing_ok=True)
        return time.monotonic() - t0


# ---------------------------------------------------------------------------
# Random action generator
# ---------------------------------------------------------------------------

def _rand_xy(rng: random.Random) -> tuple[int, int]:
    return rng.randint(0, WIDTH - 1), rng.randint(0, HEIGHT - 1)


def _rand_action(session: Session, rng: random.Random) -> str:
    kind = rng.choices(
        ["click", "doubleclick", "drag", "scroll", "key", "type", "mousemove"],
        weights=[35, 8, 15, 15, 15, 5, 7],
        k=1,
    )[0]

    if kind == "click":
        x, y = _rand_xy(rng)
        btn = rng.choice(MOUSE_BUTTONS)
        return session.click(x, y, btn)

    if kind == "doubleclick":
        x, y = _rand_xy(rng)
        return session.doubleclick(x, y)

    if kind == "drag":
        x1, y1 = _rand_xy(rng)
        dx = rng.randint(-400, 400)
        dy = rng.randint(-400, 400)
        x2 = max(0, min(WIDTH - 1, x1 + dx))
        y2 = max(0, min(HEIGHT - 1, y1 + dy))
        return session.drag(x1, y1, x2, y2)

    if kind == "scroll":
        x, y = _rand_xy(rng)
        clicks = rng.choice([-5, -3, -1, 1, 3, 5])
        return session.scroll(x, y, clicks)

    if kind == "key":
        k = rng.choice(KEYS)
        return session.key(k)

    if kind == "type":
        charset = "abcdefghijklmnopqrstuvwxyz0123456789 "
        text = "".join(rng.choices(charset, k=rng.randint(1, 8)))
        return session.type_text(text)

    if kind == "mousemove":
        x, y = _rand_xy(rng)
        return session.mousemove(x, y)

    raise ValueError(kind)


# ---------------------------------------------------------------------------
# Fuzz runner
# ---------------------------------------------------------------------------

def run_fuzz(scenario: Scenario, round_id: int) -> None:
    """Run one fuzz session. Manages its own Xvfb + jotpluggler."""
    rng = random.Random(round_id * 31 + hash(scenario.name))
    route = DEMO_ROUTE_FULL if rng.random() >= QUICK_ROUTE_PROB else DEMO_ROUTE_QUICK
    route_label = "full" if route == DEMO_ROUTE_FULL else "quick"
    label = f"{scenario.name}-r{round_id:02d}-{route_label}"

    session = Session()
    try:
        print(f"\n  starting jotpluggler ({label}, route={route_label}, cabana={scenario.cabana})...")
        session.start(route=route, cabana=scenario.cabana)
        print(f"  opened — pid={session.jotp.pid} display={session.display}")

        screenshots_dir = session.tmpdir / "screenshots"
        screenshots_dir.mkdir(exist_ok=True)

        crashes: list[str] = []
        freezes: list[str] = []
        action_log: list[str] = []
        screenshot_count = 0

        print(f"\n{'='*60}")
        print(f"  FUZZ: {label}  ({scenario.actions} actions, {WIDTH}x{HEIGHT})")
        print(f"  route: {route}")
        print(f"  tmpdir: {session.tmpdir}")
        print(f"{'='*60}")

        for i in range(1, scenario.actions + 1):
            # -- periodic screenshot / freeze check --------------------------
            if i % SCREENSHOT_EVERY == 0:
                shot_path = screenshots_dir / f"frame_{screenshot_count:04d}.png"
                try:
                    elapsed = session.screenshot(shot_path)
                    msg = f"[{i:03d}/{scenario.actions}] screenshot #{screenshot_count} -> {shot_path.name} ({elapsed:.2f}s)"
                    print(msg)
                    action_log.append(msg)
                    if elapsed > FREEZE_TIMEOUT_S * 0.8:
                        note = f"[{i:03d}] SLOW screenshot ({elapsed:.2f}s) — possible freeze"
                        print(f"  !! {note}")
                        freezes.append(note)
                except subprocess.TimeoutExpired:
                    note = f"[{i:03d}] FREEZE — screenshot timed out after {FREEZE_TIMEOUT_S}s"
                    print(f"  !! {note}")
                    freezes.append(note)
                    action_log.append(note)
                except subprocess.CalledProcessError as e:
                    note = f"[{i:03d}] screenshot failed: {e}"
                    print(f"  !! {note}")
                    action_log.append(note)
                screenshot_count += 1

            # -- random action -----------------------------------------------
            t0 = time.monotonic()
            try:
                desc = _rand_action(session, rng)
            except subprocess.TimeoutExpired:
                note = f"[{i:03d}] FREEZE — xdotool command timed out"
                print(f"  !! {note}")
                freezes.append(note)
                action_log.append(note)
                continue
            except subprocess.CalledProcessError as e:
                note = f"[{i:03d}] xdotool error: {e}"
                print(f"  !! {note}")
                action_log.append(note)
                continue
            elapsed = time.monotonic() - t0
            msg = f"[{i:03d}/{scenario.actions}] {desc} ({elapsed:.3f}s)"
            action_log.append(msg)
            if i <= 10 or i % 25 == 0:
                print(msg)

            # -- crash check -------------------------------------------------
            if not session.alive:
                reason = session.crash_reason()
                note = f"[{i:03d}] CRASH — {reason}"
                print(f"\n  !! {note}")
                print(f"  !! last action: {desc}")
                stderr_tail = session.stderr_tail(30)
                if stderr_tail:
                    print(f"  !! stderr tail:\n{stderr_tail}")
                crashes.append(note)
                action_log.append(note)
                break

        # -- final screenshot ------------------------------------------------
        if session.alive:
            final = screenshots_dir / "final.png"
            try:
                elapsed = session.screenshot(final)
                print(f"\n  final screenshot: {final} ({elapsed:.2f}s)")
            except Exception as e:
                print(f"\n  final screenshot failed: {e}")

        # -- summary ---------------------------------------------------------
        print(f"\n{'='*60}")
        print(f"  RESULTS: {label}")
        print(f"    actions executed: {len(action_log)}")
        print(f"    crashes: {len(crashes)}")
        print(f"    freezes: {len(freezes)}")
        if crashes:
            print(f"    crash details:")
            for c in crashes:
                print(f"      - {c}")
        if freezes:
            print(f"    freeze details:")
            for f in freezes:
                print(f"      - {f}")
        print(f"    screenshots saved to: {screenshots_dir}")
        print(f"{'='*60}\n")

        # -- write full log to disk ------------------------------------------
        log_path = session.tmpdir / "fuzz_actions.log"
        log_path.write_text("\n".join(action_log) + "\n")
        print(f"  full log written to: {log_path}")

        # -- assert no crashes or freezes ------------------------------------
        fail_parts = []
        if crashes:
            fail_parts.append(f"CRASHED: {crashes[0]}")
        if freezes:
            fail_parts.append(f"FROZE: {freezes[0]}")
        if fail_parts:
            pytest.fail(f"[{label}] {' | '.join(fail_parts)} (log: {log_path})")

    finally:
        session.stop()


# ---------------------------------------------------------------------------
# Test parametrization
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not BINARY.exists(), reason="jotpluggler binary not built")
class TestJotplugglerFuzz:
    """Fuzz the jotpluggler UI looking for crashes and freezes.

    Each parametrized instance is fully isolated (own Xvfb + process),
    so all 20 can run in parallel across xdist workers.
    """

    @pytest.mark.parametrize("scenario,round_id", _make_fuzz_params())
    def test_fuzz(self, scenario: Scenario, round_id: int) -> None:
        run_fuzz(scenario, round_id)


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-s", "-n", "12", "--tb=short"]))
