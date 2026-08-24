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
PACKAGE_ROOT = ROOT / "maya_module" / "ReactionDiffusionBifrost" / "0.2.0" / "scripts"


def main() -> None:
    maya.standalone.initialize(name="python")
    try:
        import maya.cmds as cmds

        sys.path.insert(0, str(PACKAGE_ROOT))
        from reaction_diffusion_bifrost import preview

        transform = cmds.polyPlane(
            name="RD_ColorSetIntegrationTest",
            subdivisionsX=1,
            subdivisionsY=1,
            constructionHistory=False,
        )[0]
        shape = cmds.listRelatives(transform, shapes=True, fullPath=True)[0]
        preview._create_color_set(shape)
        color_sets = cmds.polyColorSet(shape, query=True, allColorSets=True) or []
        if preview.COLOR_SET not in color_sets:
            raise RuntimeError(
                f"Expected color set {preview.COLOR_SET!r}; Maya returned {color_sets!r}."
            )
        print("Maya preview color-set integration test: PASS")
    finally:
        maya.standalone.uninitialize()


if __name__ == "__main__":
    main()
