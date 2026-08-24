"""Maya-independent regression checks for the 0.2.0 controller helpers."""

from __future__ import annotations

import importlib
import math
import sys
import types
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PACKAGE_ROOT = ROOT / "maya_module" / "ReactionDiffusionBifrost" / "0.2.0" / "scripts"


def install_maya_stubs() -> None:
    maya = types.ModuleType("maya")
    maya.__path__ = []
    maya_api = types.ModuleType("maya.api")
    maya_api.__path__ = []
    open_maya = types.ModuleType("maya.api.OpenMaya")
    open_maya_ui = types.ModuleType("maya.api.OpenMayaUI")
    cmds = types.ModuleType("maya.cmds")
    maya.api = maya_api
    maya.cmds = cmds
    maya_api.OpenMaya = open_maya
    maya_api.OpenMayaUI = open_maya_ui
    sys.modules.update({
        "maya": maya,
        "maya.api": maya_api,
        "maya.api.OpenMaya": open_maya,
        "maya.api.OpenMayaUI": open_maya_ui,
        "maya.cmds": cmds,
    })


def main() -> None:
    install_maya_stubs()
    sys.path.insert(0, str(PACKAGE_ROOT))
    preview = importlib.import_module("reaction_diffusion_bifrost.preview")
    bridge = importlib.import_module("reaction_diffusion_bifrost.bridge")

    flattened = preview._flatten_numbers(((0.0, 0.25), [0.5, (0.75, 1.0)]))
    assert flattened == [0.0, 0.25, 0.5, 0.75, 1.0]
    assert preview._normalized([2.0, 4.0, 6.0], True) == [0.0, 0.5, 1.0]
    assert preview._normalized([-1.0, 0.5, 2.0], False) == [0.0, 0.5, 1.0]
    for value in (-1.0, 0.0, 0.5, 1.0, 2.0):
        color = preview._color_ramp(value)
        assert len(color) == 4
        assert all(math.isfinite(channel) and 0.0 <= channel <= 1.0 for channel in color)

    assert bridge._array_literal([]) == "{}"
    assert bridge._array_literal([0.25, 0.5]) == "{0.25, 0.5}"
    assert bridge._array_literal([0, 1, 2], integer=True) == "{0, 1, 2}"

    class FakeCommands:
        @staticmethod
        def vnnCompound(_graph, _root, listNodes=False):
            assert listNodes
            return [
                "reaction_diffusion_initialize_grid",
                "reaction_diffusion_grid_step",
                "output",
            ]

    bridge.cmds = FakeCommands()
    assert bridge._find_node("graph", bridge.STEP_NODE_TOKEN) == "/reaction_diffusion_grid_step"
    assert bridge._find_node("graph", "missing", required=False) is None

    ui_source = (PACKAGE_ROOT / "reaction_diffusion_bifrost" / "ui.py").read_text(encoding="utf-8")
    assert "sizeable=True" in ui_source
    assert "cmds.scrollLayout(childResizable=True" in ui_source
    refresh_index = ui_source.index('label="Refresh Existing Output"')
    normalize_index = ui_source.index('label="Normalize preview contrast"')
    assert refresh_index < normalize_index
    print("ReactionDiffusion Maya Python helper tests: PASS")


if __name__ == "__main__":
    main()
