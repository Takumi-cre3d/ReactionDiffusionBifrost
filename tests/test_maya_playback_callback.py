"""Maya integration check for playback-time DG callback delivery."""

from __future__ import annotations

import os
import sys
from pathlib import Path
from unittest.mock import patch

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
        original_update = preview.update_preview

        def record_update(*args, **kwargs):
            frame = float(cmds.currentTime(query=True))
            result = original_update(*args, **kwargs)
            observed[frame] = preview.read_pattern(graph)
            return result

        # Standalone has no UI controls. Substitute only their input values;
        # preserve the production refresh callback, guard and error handling.
        ui.CONTROLS["graph"] = "testGraphField"
        try:
            with patch.object(cmds, "textField", return_value=graph), \
                    patch.object(ui, "_simulation_settings", return_value={
                        "width": width, "height": height}), \
                    patch.object(preview, "update_preview", side_effect=record_update):
                ui._install_time_callback()
                ui._refresh_on_time_changed(refresh_viewport=False)
                initial_pattern = list(observed[0.0])
                for frame in (1, 2, 3):
                    cmds.currentTime(frame, update=True)
                cmds.currentTime(0, update=True)
        finally:
            ui._remove_time_callback()
            ui.CONTROLS.clear()

        delivered_frames = set(observed)
        if not {0.0, 1.0, 2.0, 3.0}.issubset(delivered_frames):
            raise AssertionError(
                f"DG time callback missed frames; observed {sorted(observed)!r}."
            )
        difference_01 = max(
            abs(a - b) for a, b in zip(observed[0.0], observed[1.0])
        )
        difference_12 = max(
            abs(a - b) for a, b in zip(observed[1.0], observed[2.0])
        )
        if difference_01 <= 1.0e-6 or difference_12 <= 1.0e-6:
            raise AssertionError("Pattern did not advance through timeline time changes.")

        reset_error = max(abs(a - b) for a, b in zip(initial_pattern, observed[0.0]))
        if reset_error > 1.0e-7:
            raise AssertionError("Frame-zero playback reset did not restore the initial seed.")
        print("ReactionDiffusion playback DG callback integration: PASS")
        print(f"observedFrames={sorted(observed)}")
        print(f"frameDifference01={difference_01:.7f}")
        print(f"frameDifference12={difference_12:.7f}")
        print(f"resetMaximumError={reset_error:.7f}")
    finally:
        if not already:
            maya.standalone.uninitialize()


if __name__ == "__main__":
    main()
