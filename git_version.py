"""Embed the current git commit so the device can compare itself to GitHub."""

import subprocess

# PlatformIO provides Import when it runs this SConscript.
Import("env")  # pylint: disable=undefined-variable

build_env = globals()["env"]


def git_output(args):
    """Return stripped stdout from a git command in the project directory."""
    return subprocess.check_output(
        args,
        cwd=build_env.subst("$PROJECT_DIR"),
        text=True,
    ).strip()


sha = "unknown"
committed_at = "0"
try:
    sha = git_output(["git", "rev-parse", "HEAD"])
    committed_at = git_output(["git", "log", "-1", "--format=%ct"])
except (OSError, subprocess.CalledProcessError):
    pass

print(f"Firmware git SHA: {sha}")
build_env.Append(
    CPPDEFINES=[
        ("FW_GIT_SHA", f'\\"{sha}\\"'),
        ("FW_GIT_UNIX", committed_at),
    ]
)
