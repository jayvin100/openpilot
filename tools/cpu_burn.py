#!/usr/bin/env python3
"""Burn CPU to test fan controller. Started by manager when onroad."""
import os

def main():
  n = os.cpu_count() or 4
  print(f"cpu_burn: burning {n} cores")
  for _ in range(n - 1):
    if os.fork() == 0:
      while True:
        pass
  while True:
    pass

if __name__ == "__main__":
  main()
