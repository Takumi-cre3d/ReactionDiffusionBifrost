"""Maya UI for seed painting, Bifrost synchronization and viewport preview."""

from __future__ import annotations

import json

import maya.cmds as cmds

from . import bridge, graph_setup, paint_context, payload, preview


WINDOW = "reactionDiffusionBifrostWindow"
CONTROLS = {}


def _status(message: str) -> None:
    control = CONTROLS.get("status")
    if control and cmds.control(control, exists=True):
        cmds.text(control, edit=True, label=message)


def _refresh_counts() -> None:
    strokes, samples = payload.counts()
    _status(f"Strokes: {strokes}    Samples: {samples}")


def _set_target(*_args) -> None:
    try:
        target = paint_context.set_target_from_selection()
        cmds.textField(CONTROLS["target"], edit=True, text=target)
        _status("Target set. Choose Add or Erase Paint.")
    except RuntimeError as exception:
        cmds.warning(f"ReactionDiffusion: {exception}")


def _configure_painter() -> None:
    paint_context.configure(
        target=cmds.textField(CONTROLS["target"], query=True, text=True) or None,
        uv_set=cmds.textFieldGrp(CONTROLS["uv_set"], query=True, text=True),
        radius=cmds.floatSliderGrp(CONTROLS["radius"], query=True, value=True),
        strength=cmds.floatSliderGrp(CONTROLS["strength"], query=True, value=True),
        spacing_pixels=cmds.floatSliderGrp(CONTROLS["spacing"], query=True, value=True),
    )


def _paint(mode: str) -> None:
    try:
        _configure_painter()
        paint_context.activate(mode)
        _status(f"Painting {mode.upper()} seeds. Drag on the target mesh.")
    except (RuntimeError, ValueError) as exception:
        cmds.warning(f"ReactionDiffusion: {exception}")


def _stop(*_args) -> None:
    paint_context.stop()
    _refresh_counts()


def _graph_name() -> str:
    value = cmds.textField(CONTROLS["graph"], query=True, text=True).strip()
    return preview.resolve_graph(value or None)


def _use_latest_graph(*_args) -> None:
    try:
        graph = preview.resolve_graph()
        cmds.textField(CONTROLS["graph"], edit=True, text=graph)
        _status(f"Graph: {graph}")
    except RuntimeError as exception:
        cmds.warning(f"ReactionDiffusion: {exception}")


def _simulation_settings():
    return {
        "width": cmds.intField(CONTROLS["width"], query=True, value=True),
        "height": cmds.intField(CONTROLS["height"], query=True, value=True),
        "steps": cmds.intField(CONTROLS["total_steps"], query=True, value=True),
        "normalize": cmds.checkBox(CONTROLS["normalize"], query=True, value=True),
    }


def _evaluate(sync_seeds: bool = True) -> None:
    try:
        settings = _simulation_settings()
        result = bridge.evaluate(
            _graph_name(),
            settings["width"],
            settings["height"],
            settings["steps"],
            settings["normalize"],
            sync_seeds=sync_seeds,
        )
        cmds.textField(CONTROLS["graph"], edit=True, text=result["graph"])
        _status(
            f"Steps: {result['steps']}    Pattern: {result['pattern_size']}    "
            f"{result.get('backend_used') or '-'} / {result.get('elapsed_milliseconds') or 0.0:.3f} ms"
        )
    except (RuntimeError, ValueError) as exception:
        cmds.warning(f"ReactionDiffusion: {exception}")
        _status("Preview failed. See Script Editor for the complete warning.")


def _sync_and_preview(*_args) -> None:
    _evaluate(sync_seeds=True)


def _refresh_preview(*_args) -> None:
    try:
        settings = _simulation_settings()
        mesh = preview.update_preview(
            _graph_name(), settings["width"], settings["height"], settings["normalize"])
        _status(f"Viewport preview refreshed: {mesh}")
    except (RuntimeError, ValueError) as exception:
        cmds.warning(f"ReactionDiffusion: {exception}")


def _create_sample_graph(*_args) -> None:
    try:
        settings = _simulation_settings()
        sample_steps = max(120, settings["steps"])
        cmds.intField(CONTROLS["total_steps"], edit=True, value=sample_steps)
        created = graph_setup.create_preview_graph(
            settings["width"], settings["height"], sample_steps)
        graph = created["graph"]
        cmds.textField(CONTROLS["graph"], edit=True, text=graph)
        mesh = preview.update_preview(
            graph, settings["width"], settings["height"], True)
        backend = cmds.getAttr(f"{graph}.backend_used")
        status = cmds.getAttr(f"{graph}.status")
        elapsed = float(cmds.getAttr(f"{graph}.elapsed_milliseconds"))
        cmds.select(mesh, replace=True)
        try:
            cmds.viewFit()
        except RuntimeError:
            pass
        _status(
            f"Sample visible: {mesh}    {backend} / {elapsed:.3f} ms    {status}")
    except (RuntimeError, ValueError) as exception:
        cmds.warning(f"ReactionDiffusion: {exception}")
        _status("Sample graph creation failed. See Script Editor for details.")


def _step(*_args) -> None:
    increment = cmds.intField(CONTROLS["step_size"], query=True, value=True)
    current = cmds.intField(CONTROLS["total_steps"], query=True, value=True)
    cmds.intField(CONTROLS["total_steps"], edit=True, value=max(0, current + increment))
    # Advancing an auto-created sample must retain its centered seed. Painter
    # data is synchronized when it contains samples; the explicit Sync button
    # remains available when an empty payload should clear all graph seeds.
    _evaluate(sync_seeds=payload.counts()[1] > 0)


def _reset(*_args) -> None:
    cmds.intField(CONTROLS["total_steps"], edit=True, value=0)
    _evaluate(sync_seeds=payload.counts()[1] > 0)


def _delete_preview(*_args) -> None:
    preview.delete_preview()
    _status("Viewport preview deleted. Stroke data was preserved.")


def _auto_sync_after_stroke() -> None:
    control = CONTROLS.get("auto_sync")
    if not control or not cmds.control(control, exists=True):
        return
    if cmds.checkBox(control, query=True, value=True):
        cmds.evalDeferred(lambda: _evaluate(sync_seeds=True))
    else:
        cmds.evalDeferred(_refresh_counts)


def _clear(*_args) -> None:
    payload.clear()
    if cmds.checkBox(CONTROLS["auto_sync"], query=True, value=True):
        _evaluate(sync_seeds=True)
    else:
        _refresh_counts()


def _export(*_args) -> None:
    paths = cmds.fileDialog2(
        caption="Export Reaction-Diffusion Strokes",
        fileFilter="Reaction-Diffusion Stroke JSON (*.rdstroke.json)",
        dialogStyle=2,
        fileMode=0,
    )
    if paths:
        destination = payload.export_json(paths[0])
        _status(f"Exported: {destination}")


def _print_flattened(*_args) -> None:
    data = payload.flatten_for_bifrost()
    print("ReactionDiffusion Bifrost seed arrays:")
    print(json.dumps(data, ensure_ascii=False, indent=2))
    _refresh_counts()


def show():
    if cmds.window(WINDOW, exists=True):
        cmds.deleteUI(WINDOW)
    # Maya can retain a DPI-clamped window size between sessions. Remove that
    # preference so the scrollable Preview 2 layout starts from a known size.
    if cmds.windowPref(WINDOW, exists=True):
        cmds.windowPref(WINDOW, remove=True)
    CONTROLS.clear()
    window = cmds.window(
        WINDOW,
        title="Reaction Diffusion Controller 0.2.0 Preview 4",
        sizeable=True,
        widthHeight=(480, 720),
    )
    cmds.scrollLayout(childResizable=True, verticalScrollBarThickness=16)
    cmds.columnLayout(adjustableColumn=True, rowSpacing=7, columnAttach=("both", 10))

    cmds.text(label="Interactive Seed Painter", align="left", font="boldLabelFont", height=24)
    cmds.separator(style="in")
    cmds.rowLayout(numberOfColumns=2, adjustableColumn=1, columnWidth2=(350, 80))
    CONTROLS["target"] = cmds.textField(placeholderText="Select a polygon mesh")
    cmds.button(label="Set Target", command=_set_target)
    cmds.setParent("..")
    CONTROLS["uv_set"] = cmds.textFieldGrp(label="UV Set", text="map1", columnWidth2=(80, 340))
    CONTROLS["radius"] = cmds.floatSliderGrp(
        label="UV Radius", field=True, minValue=0.001, maxValue=0.2, value=0.02,
        fieldMinValue=0.0001, fieldMaxValue=1.0, columnWidth3=(80, 70, 270))
    CONTROLS["strength"] = cmds.floatSliderGrp(
        label="Strength", field=True, minValue=0.0, maxValue=1.0, value=1.0,
        columnWidth3=(80, 70, 270))
    CONTROLS["spacing"] = cmds.floatSliderGrp(
        label="Spacing px", field=True, minValue=1.0, maxValue=30.0, value=4.0,
        columnWidth3=(80, 70, 270))
    cmds.rowLayout(numberOfColumns=2, adjustableColumn=1, columnWidth2=(215, 215))
    cmds.button(label="Paint Add Seeds", height=36, command=lambda *_: _paint("add"))
    cmds.button(label="Paint Erase Seeds", height=36, command=lambda *_: _paint("erase"))
    cmds.setParent("..")
    cmds.button(label="Stop Painting", command=_stop)

    cmds.separator(style="in")
    cmds.text(label="Bifrost Simulation Preview", align="left", font="boldLabelFont", height=24)
    cmds.rowLayout(numberOfColumns=2, adjustableColumn=1, columnWidth2=(350, 80))
    CONTROLS["graph"] = cmds.textField(placeholderText="bifrostGraphShape")
    cmds.button(label="Use Latest", command=_use_latest_graph)
    cmds.setParent("..")
    cmds.button(
        label="Create Sample Graph + Visible Pattern",
        height=38,
        command=_create_sample_graph,
        annotation="Creates a centered sample seed, exposes pattern, and frames a colored preview.")
    cmds.rowLayout(numberOfColumns=4, adjustableColumn=4, columnWidth4=(65, 95, 65, 95))
    cmds.text(label="Width", align="right")
    CONTROLS["width"] = cmds.intField(value=64, minValue=3)
    cmds.text(label="Height", align="right")
    CONTROLS["height"] = cmds.intField(value=64, minValue=3)
    cmds.setParent("..")
    cmds.button(label="Refresh Existing Output", height=32, command=_refresh_preview)
    cmds.rowLayout(numberOfColumns=4, adjustableColumn=4, columnWidth4=(75, 90, 75, 90))
    cmds.text(label="Total Steps", align="right")
    CONTROLS["total_steps"] = cmds.intField(value=120, minValue=0)
    cmds.text(label="Step +", align="right")
    CONTROLS["step_size"] = cmds.intField(value=15, minValue=1)
    cmds.setParent("..")
    CONTROLS["normalize"] = cmds.checkBox(
        label="Normalize preview contrast", value=False,
        annotation="Display-only normalization; the numerical pattern is unchanged.")
    CONTROLS["auto_sync"] = cmds.checkBox(
        label="Auto sync and preview after each painted stroke", value=True)
    cmds.button(label="Sync Painted Seeds + Preview", height=34, command=_sync_and_preview)
    cmds.rowLayout(numberOfColumns=2, adjustableColumn=1, columnWidth2=(215, 215))
    cmds.button(label="Reset (0 steps)", command=_reset)
    cmds.button(label="Step + Preview", command=_step)
    cmds.setParent("..")
    cmds.button(label="Delete Preview Mesh", command=_delete_preview)

    cmds.separator(style="in")
    cmds.rowLayout(numberOfColumns=2, adjustableColumn=1, columnWidth2=(215, 215))
    cmds.button(label="Export Stroke JSON", command=_export)
    cmds.button(label="Print Bifrost Arrays", command=_print_flattened)
    cmds.setParent("..")
    cmds.button(label="Clear All Strokes", command=_clear)
    CONTROLS["status"] = cmds.text(label="Strokes: 0    Samples: 0", align="left", height=34)
    cmds.text(
        label="The preview mesh is display-only. Simulation values remain native Bifrost arrays.\n"
              "Step recomputes deterministically from the initialized grid at Total Steps.",
        align="left")
    cmds.showWindow(window)
    cmds.window(window, edit=True, widthHeight=(480, 720))

    paint_context.set_stroke_committed_callback(_auto_sync_after_stroke)
    _refresh_counts()
    _use_latest_graph()
    return window
