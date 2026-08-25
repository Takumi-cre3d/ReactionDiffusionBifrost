"""Maya 2026 integration check for frame-driven Bifrost Feedback State."""

from __future__ import annotations

import os
import sys
from pathlib import Path

import maya.standalone


SCRIPT_PATH = Path(globals().get(
    "__file__", Path.cwd() / "tests" / "test_maya_bifrost_stateful.py"
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
        from reaction_diffusion_bifrost import graph_setup, preview

        width, height = 32, 24
        cmds.currentTime(0)
        created = graph_setup.create_stateful_preview_graph(
            width=width,
            height=height,
            substeps_per_frame=12,
            start_frame=0,
        )
        graph = created["graph"]
        simulation = created["simulation_node"]
        print(
            "feedbackPortTypes=",
            cmds.vnnNode(graph, simulation, queryPortDataType="state"),
            cmds.vnnNode(graph, simulation, queryPortDataType="out_state"),
        )
        patterns = {}
        for frame in (0, 1, 2):
            cmds.currentTime(frame)
            preview.update_preview(graph, width, height, normalize=True)
            pattern = preview.read_pattern(graph)
            state = preview._flatten_numbers(cmds.getAttr(f"{graph}.state"))
            if len(pattern) != width * height:
                raise AssertionError(f"Frame {frame} pattern size is {len(pattern)}")
            if len(state) != 2 * width * height:
                raise AssertionError(f"Frame {frame} packed state size is {len(state)}")
            patterns[frame] = pattern
            print(
                f"frame={frame} patternRange={min(pattern):.6f}..{max(pattern):.6f} "
                f"stateSize={len(state)}"
            )

        difference_01 = max(abs(a - b) for a, b in zip(patterns[0], patterns[1]))
        difference_12 = max(abs(a - b) for a, b in zip(patterns[1], patterns[2]))
        if difference_01 <= 1.0e-6 or difference_12 <= 1.0e-6:
            raise AssertionError("Feedback pattern did not advance on consecutive frames.")
        backend = cmds.getAttr(f"{graph}.backend_used")
        status = cmds.getAttr(f"{graph}.status")
        if backend != "CUDA" or status != "auto_selected_cuda":
            raise AssertionError(f"Expected CUDA feedback step, got {backend!r}: {status!r}")

        transform = preview.update_preview(graph, width, height, normalize=True)
        if not cmds.objExists(transform):
            raise AssertionError("Stateful viewport preview was not created.")

        # Returning to the start frame must select the initialized state rather
        # than retaining a future feedback value.
        cmds.currentTime(0)
        preview.update_preview(graph, width, height, normalize=True)
        reset_pattern = preview.read_pattern(graph)
        reset_error = max(abs(a - b) for a, b in zip(patterns[0], reset_pattern))
        if reset_error > 1.0e-7:
            raise AssertionError(f"Start-frame reset mismatch: {reset_error}")

        print("ReactionDiffusion Bifrost Feedback State integration: PASS")
        print(f"backendUsed={backend}")
        print(f"status={status}")
        print(f"frameDifference01={difference_01:.7f}")
        print(f"frameDifference12={difference_12:.7f}")
        print(f"resetMaximumError={reset_error:.7f}")
    finally:
        if not already:
            maya.standalone.uninitialize()


if __name__ == "__main__":
    main()
