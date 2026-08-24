"""Maya/Bifrost end-to-end check for the visible CUDA sample graph."""

from __future__ import annotations

import os
import sys
from pathlib import Path

import maya.standalone

SCRIPT_PATH = Path(globals().get(
    "__file__", Path.cwd() / "tests" / "test_maya_bifrost_cuda.py"
)).resolve()
ROOT = SCRIPT_PATH.parents[1]
PACKAGE_ROOT = Path(os.environ.get(
    "RD_TEST_PACKAGE_ROOT",
    ROOT / "maya_module" / "ReactionDiffusionBifrost" / "0.2.0" / "scripts",
))


def main() -> None:
    maya_already_initialized = os.environ.get("RD_MAYA_ALREADY_INITIALIZED") == "1"
    if not maya_already_initialized:
        maya.standalone.initialize(name="python")
    try:
        import maya.cmds as cmds

        sys.path.insert(0, str(PACKAGE_ROOT))
        from reaction_diffusion_bifrost import bridge, graph_setup, preview

        print("integrationPhase=create_visible_sample", flush=True)
        created = graph_setup.create_preview_graph(width=64, height=48, substeps=120)
        graph = created["graph"]
        step = created["step_node"]

        print("integrationPhase=evaluate_pattern", flush=True)
        values = preview.read_pattern(graph)
        if len(values) != 64 * 48:
            raise AssertionError(f"Unexpected pattern size: {len(values)}")
        if max(values) - min(values) <= 1.0e-5:
            raise AssertionError("The centered sample seed produced a uniform pattern.")

        backend = cmds.getAttr(f"{graph}.backend_used")
        status = cmds.getAttr(f"{graph}.status")
        elapsed = float(cmds.getAttr(f"{graph}.elapsed_milliseconds"))
        if backend != "CUDA":
            raise AssertionError(f"Expected CUDA backend, got {backend!r}: {status}")
        if status != "auto_selected_cuda":
            raise AssertionError(f"Unexpected CUDA status: {status!r}")
        if elapsed < 0.0:
            raise AssertionError(f"Invalid elapsed time: {elapsed}")
        print("integrationPhase=create_viewport_preview", flush=True)
        transform = preview.update_preview(graph, width=64, height=48, normalize=True)
        shape = cmds.listRelatives(transform, shapes=True, fullPath=True)[0]
        color_sets = cmds.polyColorSet(shape, query=True, allColorSets=True) or []
        if preview.COLOR_SET not in color_sets:
            raise AssertionError(f"Preview color set is missing: {color_sets!r}")
        mesh = preview.om.MFnMesh(preview._mesh_dag_path(shape))
        colors = mesh.getVertexColors(preview.COLOR_SET)
        unique_colors = {
            (round(color.r, 5), round(color.g, 5), round(color.b, 5)) for color in colors
        }
        if len(colors) != 64 * 48 or len(unique_colors) < 2:
            raise AssertionError("The preview mesh does not contain varying vertex colors.")

        print("integrationPhase=advance_sample", flush=True)
        advanced = bridge.evaluate(
            graph, width=64, height=48, total_steps=135,
            normalize=True, sync_seeds=False,
        )
        advanced_values = preview.read_pattern(graph)
        if advanced["steps"] != 135 or max(advanced_values) - min(advanced_values) <= 1.0e-5:
            raise AssertionError("Advancing the sample did not retain its centered seed.")

        print("ReactionDiffusion Maya/Bifrost visible CUDA sample: PASS")
        print(f"backendUsed={backend}")
        print(f"status={status}")
        print(f"elapsedMilliseconds={elapsed:.6f}")
        print(f"patternRange={min(values):.6f}..{max(values):.6f}")
        print(f"previewMesh={transform}")

        # Invalid node values must become diagnostic outputs, never an
        # exception escaping the Bifrost ABI and terminating Maya.
        cmds.vnnNode(graph, step, setPortDefaultValues=("time_step", "0.0"))
        cmds.dgdirty(graph)
        invalid_backend = cmds.getAttr(f"{graph}.backend_used")
        invalid_status = cmds.getAttr(f"{graph}.status")
        if invalid_backend != "ERROR" or not invalid_status.startswith("error: "):
            raise AssertionError(
                f"Invalid input was not contained: {invalid_backend!r}, {invalid_status!r}"
            )
        print("invalidInputContainment=PASS")
        print(f"invalidStatus={invalid_status}")
    finally:
        if not maya_already_initialized:
            maya.standalone.uninitialize()


if __name__ == "__main__":
    main()
