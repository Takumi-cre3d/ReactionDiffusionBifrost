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
        from reaction_diffusion_bifrost import graph_setup, preview, ui

        width, height = 24, 18
        cmds.currentTime(0)
        created = graph_setup.create_stateful_preview_graph(
            width=width,
            height=height,
            substeps_per_frame=8,
            start_frame=0,
        )
        graph = created["graph"]
        observed = {}
        original_refresh = ui._refresh_on_time_changed

        def refresh_from_callback(refresh_viewport=True):
            frame = float(cmds.currentTime(query=True))
            preview.update_preview(
                graph,
                width,
                height,
                normalize=True,
                refresh_viewport=bool(refresh_viewport),
            )
            observed[frame] = preview.read_pattern(graph)

        ui._refresh_on_time_changed = refresh_from_callback
        try:
            ui._install_time_callback()
            refresh_from_callback(refresh_viewport=False)
            for frame in (1, 2, 3):
                cmds.currentTime(frame, update=True)
            cmds.currentTime(0, update=True)
        finally:
            ui._remove_time_callback()
            ui._refresh_on_time_changed = original_refresh

        delivered_frames = set(observed)
        if not {0.0, 1.0, 2.0, 3.0}.issubset(delivered_frames):
            raise AssertionError(
                f"DG time callback missed frames; observed {observed!r}."
            )
        difference_01 = max(
            abs(a - b) for a, b in zip(observed[0.0], observed[1.0])
        )
        difference_12 = max(
            abs(a - b) for a, b in zip(observed[1.0], observed[2.0])
        )
        if difference_01 <= 1.0e-6 or difference_12 <= 1.0e-6:
            raise AssertionError("Pattern did not advance through timeline time changes.")

        # The final jump to frame zero overwrites the initial observation. It
        # must still be the initialized pattern, not the cached frame-three
        # result. The centered seed has a peak value of exactly one.
        if abs(max(observed[0.0]) - 1.0) > 1.0e-7:
            raise AssertionError("Frame-zero playback reset did not restore the initial seed.")
        print("ReactionDiffusion playback DG callback integration: PASS")
        print(f"observedFrames={sorted(observed)}")
        print(f"frameDifference01={difference_01:.7f}")
        print(f"frameDifference12={difference_12:.7f}")
    finally:
        if not already:
            maya.standalone.uninitialize()


if __name__ == "__main__":
    main()
