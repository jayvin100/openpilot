from dataclasses import dataclass


@dataclass
class Animation:
  frames: list[list[tuple[int, int]]]
  frame_duration: float = 0.15       # seconds each frame is shown
  animation_frequency: float = 5   # seconds between animation restarts (0 = play once then hold last frame)
  hold_end: float = 0.0             # seconds to hold the last frame before playing backward (-1 = no backward)


NORMAL = Animation(
  frames=[
    [
      # Left eye
      (3, 1), (4, 1),
      (2, 2), (3, 2), (4, 2), (5, 2),
      (2, 3), (3, 3), (4, 3), (5, 3),
      (3, 4), (4, 4),
      # Left eye brow
      (1, 0), (0, 1), (0, 2),
      # Right eye
      (3, 14), (4, 14),
      (2, 13), (3, 13), (4, 13), (5, 13),
      (2, 12), (3, 12), (4, 12), (5, 12),
      (3, 11), (4, 11),
      # Right eye brow
      (1, 15), (0, 14), (0, 13),
      # Mouth (4 circles at bottom middle)
      (6, 6), (7, 7), (7, 8), (6, 9)
    ],
    [
      # Left eye
      (4, 1),
      (4, 2), (5, 2),
      (4, 3), (5, 3),
      (4, 4),
      # Left eye brow
      (1, 0), (0, 1), (0, 2),
      # Right eye
      (4, 14),
      (4, 13), (5, 13),
      (4, 12), (5, 12),
      (4, 11),
      # Right eye brow
      (1, 15), (0, 14), (0, 13),
      # Mouth (4 circles at bottom middle)
      (6, 6), (7, 7), (7, 8), (6, 9)
    ],
    [
      # Left eye
      (4, 1),
      (5, 2),
      (5, 3),
      (4, 4),
      # Left eye brow
      (2, 0), (1, 1), (1, 2),
      # Right eye
      (4, 14),
      (5, 13),
      (5, 12),
      (4, 11),
      # Right eye brow
      (2, 15), (1, 14), (1, 13),
      # Mouth (4 circles at bottom middle)
      (6, 6), (7, 7), (7, 8), (6, 9)
    ],
  ],
)
