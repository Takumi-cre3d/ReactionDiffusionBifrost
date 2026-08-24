"""Maya 2026 integration check for the preview color-set compatibility fix."""

from __future__ import annotations

import sys
import os
from pathlib import Path

# Keep the integration test isolated from user shelves and third-party startup
# packages. The tested module is imported explicitly below.
os.environ.setdefault("MAYA_SKIP_USERSETUP_PY", "1")

import maya.standalone


ROOT = Path(__file__).resolve().parents[1]
PACKAGE_ROOT = Path(os.environ.get(
    "RD_TEST_PACKAGE_ROOT",
    ROOT / "maya_module" / "ReactionDiffusionBifrost" / "0.2.0" / "scripts",
))


def main() -> None:
    maya.standalone.initialize(name="python")
    try:
        import maya.cmds as cmds

        sys.path.insert(0, str(PACKAGE_ROOT))
        from reaction_diffusion_bifrost import preview

        width = 4
        height = 3
        expected = width * height
        values = [index / float(expected - 1) for index in range(expected)]
        preview.resolve_graph = lambda _graph=None: "rdTestGraph"
        preview.read_pattern = lambda _graph: values
        transform = preview.update_preview("rdTestGraph", width, height)
        shape = cmds.listRelatives(transform, shapes=True, fullPath=True)[0]
        color_sets = cmds.polyColorSet(shape, query=True, allColorSets=True) or []
        if preview.COLOR_SET not in color_sets:
            raise RuntimeError(
                f"Expected color set {preview.COLOR_SET!r}; Maya returned {color_sets!r}."
            )
        mesh = preview.om.MFnMesh(preview._mesh_dag_path(shape))
        if mesh.currentColorSetName() != preview.COLOR_SET:
            raise RuntimeError(
                f"Expected current color set {preview.COLOR_SET!r}; "
                f"Maya returned {mesh.currentColorSetName()!r}."
            )
        colors = mesh.getVertexColors(preview.COLOR_SET)
        if len(colors) != expected:
            raise RuntimeError(f"Expected {expected} vertex colors; Maya returned {len(colors)}.")
        if colors[0] == colors[-1] or colors[0].r < 0.0 or colors[-1].r < 0.0:
            raise RuntimeError("Vertex colors were not written to the preview color set.")
        print("Maya preview color-set creation and vertex-write integration test: PASS")
    finally:
        maya.standalone.uninitialize()


if __name__ == "__main__":
    main()
