from dataclasses import dataclass
from enum import Enum


class AnimationMode(Enum):
  ONCE_FORWARD = 1
  ONCE_FORWARD_BACKWARD = 2
  REPEAT_FORWARD = 3
  REPEAT_FORWARD_BACKWARD = 4


@dataclass
class Animation:
  frames: list[list[tuple[int, int]]]
  starting_frames: list[list[tuple[int, int]]] | None = None  # played once before the main loop
  frame_duration: float = 0.15       # seconds each frame is shown
  mode: AnimationMode = AnimationMode.REPEAT_FORWARD_BACKWARD
  repeat_interval: float = 5.0      # seconds between animation restarts (only for REPEAT modes)
  hold_end: float = 0.0             # seconds to hold the last frame before playing backward (only for *_BACKWARD modes)


def _mirror(dots: list[tuple[int, int]]) -> list[tuple[int, int]]:
  """Mirror a component from the left side of the face to the right"""
  return [(r, 15 - c) for r, c in dots]


def _mirror_no_flip(dots: list[tuple[int, int]], ref: list[tuple[int, int]] | None = None) -> list[tuple[int, int]]:
  """Move a component to the symmetric position without flipping the shape.
  ref: reference component defining the full bounding box (e.g. EYE_OPEN for any eye variant)"""
  ref = ref if ref is not None else dots
  shift = min(c for _, c in _mirror(ref)) - min(c for _, c in ref)
  return [(r, c + shift) for r, c in dots]


def _shift_up(dots: list[tuple[int, int]], n: int = 1) -> list[tuple[int, int]]:
  return [(r - n, c) for r, c in dots]


def _shift_down(dots: list[tuple[int, int]], n: int = 1) -> list[tuple[int, int]]:
  return [(r + n, c) for r, c in dots]


def _shift_left(dots: list[tuple[int, int]], n: int = 1) -> list[tuple[int, int]]:
  return [(r, c - n) for r, c in dots]


def _shift_right(dots: list[tuple[int, int]], n: int = 1) -> list[tuple[int, int]]:
  return [(r, c + n) for r, c in dots]


def _make_frame(left_eye: list[tuple[int, int]], right_eye: list[tuple[int, int]],
                 left_brow: list[tuple[int, int]], right_brow: list[tuple[int, int]],
                 mouth: list[tuple[int, int]]) -> list[tuple[int, int]]:
  return left_eye + left_brow + right_eye + right_brow + mouth


# Eyes (left side)
EYE_OPEN = [
        (2, 2), (2, 3),
(3, 1), (3, 2), (3, 3), (3, 4),
(4, 1), (4, 2), (4, 3), (4, 4),
        (5, 2), (5, 3)
]
EYE_HALF = [
(4, 1), (4, 2), (4, 3), (4, 4),
        (5, 2), (5, 3)
]
EYE_CLOSED = [
(4, 1),                 (4, 4),
        (5, 2), (5, 3),
]
EYE_LEFT_LOOK = [
        (2, 2), (2, 3),
(3, 1), (3, 2),
(4, 1), (4, 2),
        (5, 2), (5, 3),
]
EYE_RIGHT_LOOK = [
        (2, 2), (2, 3),
                (3, 3), (3, 4),
                (4, 3), (4, 4),
        (5, 2), (5, 3),
]

# Eyebrows (left side)
BROW_HIGH = [(1, 0), (0, 1), (0, 2)]
BROW_LOWERED = [(2, 0), (1, 1), (1, 2)]
BROW_STRAIGHT = [(2, 0), (2, 1), (2, 2)]
NO_BROW = []

# Mouths (centered, not mirrored)
MOUTH_SMILE = [(6, 6), (7, 7), (7, 8), (6, 9)]
MOUTH_NORMAL = [(7, 7), (7, 8)]
MOUTH_SAD = [(7, 6), (6, 7), (6, 8), (7, 9)]


# --- Animations ---

NORMAL = Animation(
  frames=[
    _make_frame(EYE_OPEN, _mirror(EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(EYE_HALF, _mirror(EYE_HALF), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(EYE_CLOSED, _mirror(EYE_CLOSED), BROW_LOWERED, _mirror(BROW_LOWERED), MOUTH_SMILE),
  ],
)

ASLEEP = Animation(
  frames=[
    _make_frame(EYE_CLOSED, _mirror(EYE_CLOSED), NO_BROW, NO_BROW, MOUTH_NORMAL),
  ],
  # frame_duration=0.25,
)

SLEEPY = Animation(
  frames=[
    _make_frame(EYE_CLOSED, _mirror(EYE_CLOSED), NO_BROW, _mirror(BROW_STRAIGHT), MOUTH_NORMAL),
    _make_frame(EYE_CLOSED, _mirror(EYE_HALF), NO_BROW, _mirror(BROW_LOWERED), MOUTH_NORMAL),
    _make_frame(EYE_CLOSED, _mirror(EYE_OPEN), NO_BROW, _mirror(BROW_HIGH), MOUTH_NORMAL)
  ],
  frame_duration=0.25,
  mode=AnimationMode.ONCE_FORWARD_BACKWARD,
  repeat_interval=10,
  hold_end=1.5,
)

INQUISITIVE = Animation(
  frames=[
    _make_frame(EYE_OPEN, _mirror(EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),

    _make_frame(EYE_LEFT_LOOK, _mirror_no_flip(EYE_LEFT_LOOK, EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift_left(EYE_LEFT_LOOK, 1), _shift_left(_mirror_no_flip(EYE_LEFT_LOOK, EYE_OPEN), 1), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift_left(EYE_LEFT_LOOK, 1), _shift_left(_mirror_no_flip(EYE_LEFT_LOOK, EYE_OPEN), 1), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift_left(EYE_LEFT_LOOK, 1), _shift_left(_mirror_no_flip(EYE_LEFT_LOOK, EYE_OPEN), 1), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(EYE_LEFT_LOOK, _mirror_no_flip(EYE_LEFT_LOOK, EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),

    # _make_frame(EYE_OPEN, _mirror(EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),

    _make_frame(EYE_RIGHT_LOOK, _mirror_no_flip(EYE_RIGHT_LOOK, EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift_right(EYE_RIGHT_LOOK, 1), _shift_right(_mirror_no_flip(EYE_RIGHT_LOOK, EYE_OPEN), 1), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift_right(EYE_RIGHT_LOOK, 1), _shift_right(_mirror_no_flip(EYE_RIGHT_LOOK, EYE_OPEN), 1), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift_right(EYE_RIGHT_LOOK, 1), _shift_right(_mirror_no_flip(EYE_RIGHT_LOOK, EYE_OPEN), 1), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(EYE_RIGHT_LOOK, _mirror_no_flip(EYE_RIGHT_LOOK, EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),

    _make_frame(EYE_OPEN, _mirror(EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
  ],
  mode=AnimationMode.REPEAT_FORWARD,
  frame_duration=0.2,
  repeat_interval=10
)
