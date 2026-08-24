"""Mesh ray-cast painting context for reaction-diffusion seed strokes."""

from __future__ import annotations

import math
import time
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple

import maya.api.OpenMaya as om
import maya.api.OpenMayaUI as omui
import maya.cmds as cmds

from . import payload


CONTEXT_NAME = "reactionDiffusionSeedPaintContext"
PREVIEW_GROUP = "RD_SeedStrokes_GRP"


class PaintState:
    def __init__(self) -> None:
        self.target = ""
        self.uv_set = "map1"
        self.mode = "add"
        self.radius = 0.02
        self.strength = 1.0
        self.spacing_pixels = 4.0
        self.current_samples: List[Dict[str, Any]] = []
        self.last_screen: Optional[Tuple[float, float]] = None
        self.stroke_committed_callback: Optional[Callable[[], None]] = None


STATE = PaintState()


def set_stroke_committed_callback(callback: Optional[Callable[[], None]]) -> None:
    """Set a UI callback without coupling the paint context to graph controls."""
    STATE.stroke_committed_callback = callback


def _mesh_dag_path(name: str) -> om.MDagPath:
    selection = om.MSelectionList()
    selection.add(name)
    path = selection.getDagPath(0)
    if path.node().hasFn(om.MFn.kTransform):
        path.extendToShape()
    if not path.node().hasFn(om.MFn.kMesh):
        raise RuntimeError("The paint target must be a polygon mesh.")
    return path


def set_target_from_selection() -> str:
    selected = cmds.ls(selection=True, long=True, objectsOnly=True) or []
    if not selected:
        raise RuntimeError("Select one polygon mesh before setting the paint target.")
    path = _mesh_dag_path(selected[0])
    STATE.target = path.fullPathName()
    return STATE.target


def configure(
    target: Optional[str] = None,
    uv_set: Optional[str] = None,
    radius: Optional[float] = None,
    strength: Optional[float] = None,
    spacing_pixels: Optional[float] = None) -> None:
    if target:
        _mesh_dag_path(target)
        STATE.target = target
    if uv_set:
        STATE.uv_set = uv_set
    if radius is not None:
        STATE.radius = max(0.0001, float(radius))
    if strength is not None:
        STATE.strength = max(0.0, min(1.0, float(strength)))
    if spacing_pixels is not None:
        STATE.spacing_pixels = max(1.0, float(spacing_pixels))


def _screen_point() -> Tuple[float, float]:
    point = cmds.draggerContext(CONTEXT_NAME, query=True, dragPoint=True)
    if not point:
        point = cmds.draggerContext(CONTEXT_NAME, query=True, anchorPoint=True)
    return float(point[0]), float(point[1])


def _ray_from_screen(x: float, y: float) -> Tuple[om.MPoint, om.MVector]:
    view = omui.M3dView.active3dView()
    try:
        source, direction = view.viewToWorld(int(x), int(y))
        return source, direction
    except TypeError:
        source = om.MPoint()
        direction = om.MVector()
        view.viewToWorld(int(x), int(y), source, direction)
        return source, direction


def _hit_sample(x: float, y: float) -> Optional[Dict[str, Any]]:
    if not STATE.target or not cmds.objExists(STATE.target):
        return None
    path = _mesh_dag_path(STATE.target)
    mesh = om.MFnMesh(path)
    source, direction = _ray_from_screen(x, y)
    hit = mesh.closestIntersection(
        om.MFloatPoint(source),
        om.MFloatVector(direction),
        om.MSpace.kWorld,
        1.0e10,
        False,
    )
    if not hit:
        return None

    # closestIntersection returns MFloatPoint, while getUVAtPoint and
    # getClosestNormal require MPoint in Maya Python API 2.0.
    hit_point = om.MPoint(hit[0])
    face_id = int(hit[2])
    triangle_id = int(hit[3])
    barycentric_1 = float(hit[4])
    barycentric_2 = float(hit[5])
    barycentric_0 = 1.0 - barycentric_1 - barycentric_2
    try:
        uv_value = mesh.getUVAtPoint(hit_point, om.MSpace.kWorld, STATE.uv_set)
        u, v = float(uv_value[0]), float(uv_value[1])
    except RuntimeError:
        cmds.warning(f"ReactionDiffusion: UV set '{STATE.uv_set}' could not be sampled.")
        return None
    try:
        normal, _ = mesh.getClosestNormal(hit_point, om.MSpace.kWorld)
    except RuntimeError:
        normal = om.MVector(0.0, 1.0, 0.0)

    return {
        "position": [float(hit_point.x), float(hit_point.y), float(hit_point.z)],
        "normal": [float(normal.x), float(normal.y), float(normal.z)],
        "face": face_id,
        "triangle": triangle_id,
        "barycentric": [barycentric_0, barycentric_1, barycentric_2],
        "uv": [u, v],
        "time": time.time(),
    }


def _capture(force: bool = False) -> None:
    screen = _screen_point()
    if not force and STATE.last_screen is not None:
        dx = screen[0] - STATE.last_screen[0]
        dy = screen[1] - STATE.last_screen[1]
        if math.sqrt(dx * dx + dy * dy) < STATE.spacing_pixels:
            return
    sample = _hit_sample(*screen)
    if sample is None:
        return
    STATE.current_samples.append(sample)
    STATE.last_screen = screen


def _on_press() -> None:
    if not STATE.target:
        try:
            set_target_from_selection()
        except RuntimeError as exception:
            cmds.warning(f"ReactionDiffusion: {exception}")
            return
    cmds.undoInfo(openChunk=True, chunkName="ReactionDiffusionPaintSeed")
    STATE.current_samples = []
    STATE.last_screen = None
    _capture(force=True)


def _on_drag() -> None:
    _capture(force=False)


def _ensure_preview_group() -> str:
    if not cmds.objExists(PREVIEW_GROUP):
        group = cmds.group(empty=True, name=PREVIEW_GROUP)
        cmds.setAttr(f"{group}.inheritsTransform", 0)
        cmds.setAttr(f"{group}.overrideEnabled", 1)
    return PREVIEW_GROUP


def _create_preview(stroke_id: int, samples: Sequence[Dict[str, Any]], mode: str) -> None:
    if not samples:
        return
    group = _ensure_preview_group()
    points = [sample["position"] for sample in samples]
    if len(points) == 1:
        normal = samples[0]["normal"]
        epsilon = 0.001
        points.append([
            points[0][0] + normal[0] * epsilon,
            points[0][1] + normal[1] * epsilon,
            points[0][2] + normal[2] * epsilon,
        ])
    curve = cmds.curve(degree=1, point=points, name=f"RD_SeedStroke_{stroke_id:04d}")
    cmds.parent(curve, group)
    shape = (cmds.listRelatives(curve, shapes=True, fullPath=True) or [curve])[0]
    cmds.setAttr(f"{shape}.overrideEnabled", 1)
    cmds.setAttr(f"{shape}.overrideColor", 13 if mode == "erase" else 14)
    cmds.setAttr(f"{shape}.lineWidth", 2.0)
    for name, value in (("rdRadius", STATE.radius), ("rdStrength", STATE.strength)):
        cmds.addAttr(curve, longName=name, attributeType="double", defaultValue=value)
        cmds.setAttr(f"{curve}.{name}", value)
    cmds.addAttr(curve, longName="rdMode", dataType="string")
    cmds.setAttr(f"{curve}.rdMode", mode, type="string")
    cmds.addAttr(curve, longName="rdStrokeId", attributeType="long", defaultValue=stroke_id)


def _on_release() -> None:
    committed = False
    try:
        if STATE.current_samples:
            data = payload.read()
            stroke_id = len(data.get("strokes", [])) + 1
            stroke = {
                "id": stroke_id,
                "mode": STATE.mode,
                "radius": STATE.radius,
                "strength": STATE.strength,
                "samples": list(STATE.current_samples),
            }
            payload.append_stroke(stroke, STATE.target, STATE.uv_set)
            _create_preview(stroke_id, STATE.current_samples, STATE.mode)
            committed = True
    finally:
        STATE.current_samples = []
        STATE.last_screen = None
        cmds.undoInfo(closeChunk=True)
    if committed and STATE.stroke_committed_callback:
        try:
            STATE.stroke_committed_callback()
        except Exception as exception:  # Painter data must survive bridge/UI failures.
            cmds.warning(f"ReactionDiffusion: Seed was stored, but auto preview failed: {exception}")


def activate(mode: str = "add") -> str:
    if mode not in {"add", "erase"}:
        raise ValueError("Paint mode must be 'add' or 'erase'.")
    if not STATE.target:
        set_target_from_selection()
    STATE.mode = mode
    if cmds.draggerContext(CONTEXT_NAME, exists=True):
        cmds.deleteUI(CONTEXT_NAME)
    cmds.draggerContext(
        CONTEXT_NAME,
        pressCommand=_on_press,
        dragCommand=_on_drag,
        releaseCommand=_on_release,
        cursor="crossHair",
        undoMode="step",
        space="screen",
    )
    cmds.setToolTo(CONTEXT_NAME)
    return CONTEXT_NAME


def stop() -> None:
    cmds.setToolTo("selectSuperContext")
