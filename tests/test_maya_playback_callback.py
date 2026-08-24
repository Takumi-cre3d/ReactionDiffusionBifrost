"""Maya integration check for playback-time DG callback delivery."""

from __future__ import annotations

import os
import sys
from pathlib import Path

import maya.standalone


SCRIPT_PATH = Path(globals().get(
    "__file__", Path.cwd() / "tests" / "test_maya_playback_callback.py"
)).resolve()
ROOT = SCRIPT_PATH.parents[1]
PACKAGE_ROOT = Path(os.environ.get(
    "RD_TEST_PACKAGE_ROOT",
    ROOT / "maya_module" / "ReactionDiffusionBifrost" / "0.2.0" / "scripts",
))


def main() -> None:
    already = os.environ.get("RD_MAYA_ALREADY_INITIALIZED") == "1"
    if not already:
        maya.standalone.initialize(name="python")
    try:
        import maya.cmds as cmds

        sys.path.insert(0, str(PACKAGE_ROOT))
        from reaction_diffusion_bifrost import ui

        observed = []
        original_refresh = ui._refresh_on_time_changed
        ui._refresh_on_time_changed = lambda refresh_viewport=True: observed.append(
            (float(cmds.currentTime(query=True)), bool(refresh_viewport))
        )
        try:
            ui._install_time_callback()
            for frame in (2, 3, 4):
                cmds.currentTime(frame, update=True)
        finally:
            ui._remove_time_callback()
            ui._refresh_on_time_changed = original_refresh

        delivered_frames = {frame for frame, _refresh in observed}
        if not {2.0, 3.0, 4.0}.issubset(delivered_frames):
            raise AssertionError(
                f"DG time callback missed frames; observed {observed!r}."
            )
        if any(refresh for _frame, refresh in observed):
            raise AssertionError("Playback callback must not force a recursive viewport refresh.")
        print("ReactionDiffusion playback DG callback integration: PASS")
        print(f"observed={observed!r}")
    finally:
        if not already:
            maya.standalone.uninitialize()


if __name__ == "__main__":
    main()
