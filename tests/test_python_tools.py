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

    color_set_calls = []

    class FakeColorSetCommands:
        @staticmethod
        def polyColorSet(shape, **kwargs):
            color_set_calls.append((shape, kwargs))

    original_preview_commands = preview.cmds
    preview.cmds = FakeColorSetCommands()
    try:
        preview._create_color_set("previewShape")
    finally:
        preview.cmds = original_preview_commands
    assert color_set_calls == [(
        "previewShape",
        {
            "create": True,
            "colorSet": preview.COLOR_SET,
            "representation": "RGBA",
            "clamped": True,
        },
    )]

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
    assert 'label="Create Sample Graph + Visible Pattern"' in ui_source
    assert 'title="Reaction Diffusion Controller 0.2.0 Preview 7"' in ui_source
    assert 'label="Create Stateful Playback Graph"' in ui_source
    assert "om.MDGMessage.addForceUpdateCallback(_on_dg_time_changed)" in ui_source
    assert 'event=("timeChanged", _refresh_on_time_changed)' not in ui_source
    assert "refresh_viewport=refresh_viewport" in ui_source
    graph_setup_source = (
        PACKAGE_ROOT / "reaction_diffusion_bifrost" / "graph_setup.py"
    ).read_text(encoding="utf-8")
    assert '("pattern", "array<float>")' in graph_setup_source
    assert '("seed_u", "{0.5}")' in graph_setup_source
    assert '("seed_v", "{0.5}")' in graph_setup_source
    assert '("substeps", str(substeps))' in graph_setup_source
    assert "def create_stateful_preview_graph(" in graph_setup_source
    assert '"Simulation::Common", "simulation_example"' in graph_setup_source
    assert 'setPortDataType=(port, "array<float>")' in graph_setup_source
    assert 'STATE_STEP_TYPE = "reaction_diffusion_state_step"' in graph_setup_source
    assert "cmds.vnnChangeBracket(graph, open=True)" in graph_setup_source
    assert "cmds.vnnChangeBracket(graph, close=True)" in graph_setup_source
    assert '_evaluate(sync_seeds=payload.counts()[1] > 0)' in ui_source
    preview_source = (PACKAGE_ROOT / "reaction_diffusion_bifrost" / "preview.py").read_text(
        encoding="utf-8"
    )
    assert ".createColorSetWithName(" not in preview_source
    assert "cmds.polyColorSet(" in preview_source
    assert "mesh.setCurrentColorSetName(COLOR_SET)" in preview_source
    assert "mesh.setVertexColors(colors, vertex_ids)" in preview_source
    assert "cmds.dgdirty(graph_shape)" in preview_source
    assert "mesh.setVertexColors(colors, vertex_ids, COLOR_SET)" not in preview_source
    refresh_index = ui_source.index('label="Refresh Existing Output"')
    normalize_index = ui_source.index('label="Normalize preview contrast"')
    assert refresh_index < normalize_index
    installer_source = (ROOT / "scripts" / "install_maya_module.ps1").read_text(
        encoding="utf-8"
    )
    assert 'Join-Path $versionBackup "bifrost"' in installer_source
    assert "Preserved existing Bifrost pack" in installer_source
    build_script_source = (ROOT / "scripts" / "build_bifrost_pack.ps1").read_text(
        encoding="utf-8"
    )
    assert "ReactionDiffusionCuda.cu" in build_script_source
    assert "RD_HAS_CUDA=1" in build_script_source
    assert 'CUDA_ARCHITECTURES "75;86;89"' in build_script_source
    assert "Find-CudaToolkitRoot" in build_script_source
    assert "CUDA_RUNTIME_LIBRARY Static" in build_script_source
    assert "/NODEFAULTLIB:LIBCMT" in build_script_source
    hotfix_source = (ROOT / "scripts" / "apply_python_hotfix.ps1").read_text(
        encoding="utf-8"
    )
    assert "Preview 7 Python update installed successfully" in hotfix_source
    assert "unsupported Maya 2026 createColorSetWithName" in hotfix_source
    native_header = (ROOT / "native" / "bifrost" / "ReactionDiffusion.h").read_text(
        encoding="utf-8"
    )
    for state_operator in (
        "reaction_diffusion_initialize_state",
        "reaction_diffusion_state_step",
        "reaction_diffusion_state_outputs",
    ):
        assert state_operator in native_header
        assert state_operator in build_script_source
    manifest_source = (ROOT / "SOURCE_MANIFEST.txt").read_text(encoding="utf-8")
    assert "distributionVersion=0.2.0-preview7" in manifest_source
    assert "tests/test_maya_bifrost_stateful.py" in manifest_source
    assert "tests/test_maya_playback_callback.py" in manifest_source
    print("ReactionDiffusion Maya Python helper tests: PASS")


if __name__ == "__main__":
    main()
