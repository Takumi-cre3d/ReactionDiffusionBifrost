"""Exercise the production callback with controlled UI/evaluation failures."""
import importlib
from unittest.mock import patch

from test_python_tools import PACKAGE_ROOT, install_maya_stubs
import sys

sys.path.insert(0, str(PACKAGE_ROOT))
install_maya_stubs()
ui = importlib.import_module("reaction_diffusion_bifrost.ui")


def main():
    ui.CONTROLS["graph"] = "graphField"
    with patch.object(ui.cmds, "textField", create=True, return_value="graph"), \
            patch.object(ui.cmds, "currentTime", create=True, return_value=2), \
            patch.object(ui.cmds, "getAttr", create=True, return_value="CUDA"), \
            patch.object(ui.cmds, "warning", create=True) as warning, \
            patch.object(ui, "_is_stateful_graph", return_value=True), \
            patch.object(ui, "_simulation_settings", return_value={"width": 24, "height": 18}), \
            patch.object(ui, "_status") as status, \
            patch.object(ui.preview, "update_preview") as update:
        update.side_effect = RuntimeError("Pattern dimensions mismatch")
        for _ in range(3):
            ui._on_dg_time_changed(None, None)
        assert warning.call_count == 1, "Repeated failures must warn once, not disappear or spam."
        assert "Pattern dimensions mismatch" in status.call_args.args[0]
        assert not ui._TIME_REFRESH_BUSY
        update.side_effect = None
        update.return_value = "preview"
        ui._on_dg_time_changed(None, None)
        assert update.call_args.kwargs["refresh_viewport"] is False
        assert "State frame 2" in status.call_args.args[0]
        update.side_effect = RuntimeError("Pattern dimensions mismatch")
        ui._on_dg_time_changed(None, None)
        assert warning.call_count == 2, "Recovery must allow a later failure to be reported."
    print("Timeline error reporting and recovery: PASS")


if __name__ == "__main__":
    main()
