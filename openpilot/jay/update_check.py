"""A lightweight, read-only remote check that can run while driving."""
import os
import subprocess


def check_remote(basedir: str, branch: str) -> str:
  env = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}

  def git(*args):
    return subprocess.check_output(["git", *args], cwd=basedir, env=env, timeout=20,
                                   stderr=subprocess.STDOUT, text=True).strip()

  if not branch or branch.startswith("-"):
    raise ValueError("No target branch selected")
  output = git("ls-remote", "--exit-code", "origin", f"refs/heads/{branch}")
  remote = next((line.split()[0] for line in output.splitlines()
                 if line.split()[1:] == [f"refs/heads/{branch}"]), None)
  if remote is None:
    raise ValueError("Target branch not found")
  current = git("rev-parse", "HEAD")
  return "Up to date" if current == remote else f"Update available ({remote[:7]})"
