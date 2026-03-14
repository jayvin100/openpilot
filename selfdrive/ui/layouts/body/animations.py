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
  left_turn_remove: list[tuple[int, int]] | None = None   # dots to remove from frame when turning left
  right_turn_remove: list[tuple[int, int]] | None = None   # dots to remove from frame when turning right


def _mirror(dots: list[tuple[int, int]]) -> list[tuple[int, int]]:
  """Mirror a component from the left side of the face to the right"""
  return [(r, 15 - c) for r, c in dots]

def _mirror_no_flip(dots: list[tuple[int, int]]) -> list[tuple[int, int]]:
  """Move a component to the mirrored position on the right half without flipping its shape."""
  min_c = min(c for _, c in dots)
  max_c = max(c for _, c in dots)
  return [(r, 15 - max_c - min_c + c) for r, c in dots]


def _shift(dots: list[tuple[int, int]], rc: tuple[int, int]) -> list[tuple[int, int]]:
  dr, dc = rc
  return [(r + dr, c + dc) for r, c in dots]


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
BROW_HIGH = [
        (0, 1), (0, 2),
(1, 0),
]
BROW_LOWERED = [
        (1, 1), (1, 2),
(2, 0)
]
BROW_STRAIGHT = [(1, 0), (1, 1), (1, 2)]
BROW_DOWN = [
(0, 1), (0, 2),
                (1, 3)
]

# Mouths (centered, not mirrored)
MOUTH_SMILE = [
(6, 6),                 (6, 9),
        (7, 7), (7, 8),
]
MOUTH_NORMAL = [(7, 7), (7, 8)]
MOUTH_SAD = [
        (6, 7), (6, 8),
(7, 6),                 (7, 9)
]


# --- Animations ---

NORMAL = Animation(
  frames=[
    _make_frame(EYE_OPEN, _mirror(EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(EYE_HALF, _mirror(EYE_HALF), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(EYE_CLOSED, _mirror(EYE_CLOSED), BROW_LOWERED, _mirror(BROW_LOWERED), MOUTH_SMILE),
  ],
  left_turn_remove=[
    (3, 3), (3, 4),
    (4, 3), (4, 4),
  ] + _mirror_no_flip([
    (3, 1), (3, 2),
    (4, 1), (4, 2),
  ]),
  right_turn_remove=[
    (3, 1), (3, 2),
    (4, 1), (4, 2),
  ] + _mirror_no_flip([
    (3, 3), (3, 4),
    (4, 3), (4, 4),
  ])
)

ASLEEP = Animation(
  frames=[
    _make_frame(EYE_CLOSED, _mirror(EYE_CLOSED), [], [], MOUTH_NORMAL),
  ],
  # frame_duration=0.25,
)

SLEEPY = Animation(
  frames=[
    _make_frame(EYE_CLOSED, _mirror(EYE_CLOSED), [], _shift(_mirror(BROW_STRAIGHT), (1,0)), MOUTH_NORMAL),
    _make_frame(EYE_CLOSED, _mirror(EYE_HALF), [], _mirror(BROW_LOWERED), MOUTH_NORMAL),
    _make_frame(EYE_CLOSED, _mirror(EYE_OPEN), [], _mirror(BROW_HIGH), MOUTH_NORMAL)
  ],
  frame_duration=0.25,
  mode=AnimationMode.ONCE_FORWARD_BACKWARD,
  repeat_interval=10,
  hold_end=1.5,
)

INQUISITIVE = Animation(
  frames=[
    _make_frame(EYE_OPEN, _mirror(EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),

    _make_frame(EYE_LEFT_LOOK, _mirror(EYE_RIGHT_LOOK), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift(EYE_LEFT_LOOK, (0, -1)), _shift(_mirror(EYE_RIGHT_LOOK), (0, -1)), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift(EYE_LEFT_LOOK, (0, -1)), _shift(_mirror(EYE_RIGHT_LOOK), (0, -1)), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift(EYE_LEFT_LOOK, (0, -1)), _shift(_mirror(EYE_RIGHT_LOOK), (0, -1)), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(EYE_LEFT_LOOK, _mirror(EYE_RIGHT_LOOK), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),

    # _make_frame(EYE_OPEN, _mirror(EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),

    _make_frame(EYE_RIGHT_LOOK, _mirror(EYE_LEFT_LOOK), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift(EYE_RIGHT_LOOK, (0, 1)), _shift(_mirror(EYE_LEFT_LOOK), (0, 1)), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift(EYE_RIGHT_LOOK, (0, 1)), _shift(_mirror(EYE_LEFT_LOOK), (0, 1)), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(_shift(EYE_RIGHT_LOOK, (0, 1)), _shift(_mirror(EYE_LEFT_LOOK), (0, 1)), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(EYE_RIGHT_LOOK, _mirror(EYE_LEFT_LOOK), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),

    _make_frame(EYE_OPEN, _mirror(EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
  ],
  mode=AnimationMode.REPEAT_FORWARD,
  frame_duration=0.15,
  repeat_interval=10
)

WINK = Animation(
  frames=[
    _make_frame(EYE_OPEN, _mirror(EYE_OPEN), BROW_HIGH, _mirror(BROW_HIGH), MOUTH_SMILE),
    _make_frame(EYE_OPEN, _mirror(EYE_CLOSED), BROW_HIGH, _mirror(_shift(BROW_DOWN, (0, 2))), MOUTH_SMILE),
  ],
  mode=AnimationMode.ONCE_FORWARD_BACKWARD,
  frame_duration=0.75,
)
