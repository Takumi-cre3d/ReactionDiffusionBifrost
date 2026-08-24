"""Viewport preview for reaction-diffusion scalar arrays.

The numerical simulation remains in the native Bifrost operator.  This module
only converts a top-level ``pattern`` array into vertex colors on a Maya mesh
so that an artist can see and frame the result immediately.
"""

from __future__ import annotations

from numbers import Real
from typing import Any, Iterable, List, Optional, Sequence, Tuple

import maya.api.OpenMaya as om
import maya.cmds as cmds


PREVIEW_GROUP = "RD_SimulationPreview_GRP"
PREVIEW_MESH = "RD_SimulationPreview"
COLOR_SET = "rdPattern"


def _flatten_numbers(value: Any) -> List[float]:
    """Flatten the shapes returned by Maya for Bifrost array attributes."""
    result: List[float] = []

    def visit(item: Any) -> None:
        if isinstance(item, Real):
            result.append(float(item))
        elif isinstance(item, (list, tuple)):
            for child in item:
                visit(child)

    visit(value)
    return result


def resolve_graph(graph: Optional[str] = None) -> str:
    """Return a Bifrost graph shape from an explicit name, selection or scene."""
    candidates: List[str] = []
    if graph:
        candidates.append(graph)
    candidates.extend(cmds.ls(selection=True, long=True) or [])
    candidates.extend(reversed(cmds.ls(type="bifrostGraphShape", long=True) or []))

    for candidate in candidates:
        if not candidate or not cmds.objExists(candidate):
            continue
        if cmds.nodeType(candidate) == "bifrostGraphShape":
            return candidate
        shapes = cmds.listRelatives(candidate, shapes=True, fullPath=True) or []
        for shape in shapes:
            if cmds.nodeType(shape) == "bifrostGraphShape":
                return shape
    raise RuntimeError("No bifrostGraphShape was found. Select a Bifrost graph or enter its name.")


def read_pattern(graph: str, attribute: str = "pattern") -> List[float]:
    graph = resolve_graph(graph)
    plug = f"{graph}.{attribute}"
    if not cmds.objExists(plug):
        raise RuntimeError(
            f"The graph has no top-level '{attribute}' output. "
            "Connect reaction_diffusion_grid_step.pattern to the graph output node."
        )
    try:
        raw = cmds.getAttr(plug)
    except RuntimeError as exception:
        raise RuntimeError(f"Bifrost could not evaluate '{plug}': {exception}") from exception
    values = _flatten_numbers(raw)
    if not values:
        raise RuntimeError(
            "The pattern output is empty. Check width/height and the native operator connections."
        )
    return values


def _preview_shape() -> Optional[str]:
    if not cmds.objExists(PREVIEW_MESH):
        return None
    shapes = cmds.listRelatives(PREVIEW_MESH, shapes=True, fullPath=True) or []
    return shapes[0] if shapes else None


def _stored_dimensions() -> Tuple[int, int]:
    if not cmds.objExists(PREVIEW_MESH):
        return 0, 0
    if not cmds.attributeQuery("rdWidth", node=PREVIEW_MESH, exists=True):
        return 0, 0
    return (
        int(cmds.getAttr(f"{PREVIEW_MESH}.rdWidth")),
        int(cmds.getAttr(f"{PREVIEW_MESH}.rdHeight")),
    )


def _create_preview_mesh(width: int, height: int) -> str:
    if width < 3 or height < 3:
        raise ValueError("Preview dimensions must be at least 3 x 3.")
    if cmds.objExists(PREVIEW_MESH):
        cmds.delete(PREVIEW_MESH)
    if not cmds.objExists(PREVIEW_GROUP):
        group = cmds.group(empty=True, name=PREVIEW_GROUP)
        cmds.setAttr(f"{group}.inheritsTransform", 0)

    created = cmds.polyPlane(
        name=PREVIEW_MESH,
        width=10.0,
        height=10.0 * float(height) / float(width),
        subdivisionsX=width - 1,
        subdivisionsY=height - 1,
        axis=(0.0, 0.0, 1.0),
        constructionHistory=False,
    )
    transform = created[0] if isinstance(created, (list, tuple)) else created
    cmds.parent(transform, PREVIEW_GROUP)
    for name, value in (("rdWidth", width), ("rdHeight", height)):
        if not cmds.attributeQuery(name, node=transform, exists=True):
            cmds.addAttr(transform, longName=name, attributeType="long")
        cmds.setAttr(f"{transform}.{name}", value)
        cmds.setAttr(f"{transform}.{name}", lock=True)
    shape = _preview_shape()
    if not shape:
        raise RuntimeError("Maya did not create the preview mesh shape.")
    cmds.setAttr(f"{shape}.displayColors", 1)
    cmds.setAttr(f"{shape}.doubleSided", 1)
    return shape


def _mesh_dag_path(shape: str) -> om.MDagPath:
    selection = om.MSelectionList()
    selection.add(shape)
    return selection.getDagPath(0)


def _color_ramp(value: float) -> Tuple[float, float, float, float]:
    """A high-contrast dark-blue to cyan/white artist preview ramp."""
    x = max(0.0, min(1.0, value))
    if x < 0.35:
        t = x / 0.35
        return 0.01 + 0.02 * t, 0.015 + 0.10 * t, 0.04 + 0.24 * t, 1.0
    if x < 0.72:
        t = (x - 0.35) / 0.37
        return 0.03 + 0.02 * t, 0.115 + 0.64 * t, 0.28 + 0.62 * t, 1.0
    t = (x - 0.72) / 0.28
    return 0.05 + 0.95 * t, 0.755 + 0.245 * t, 0.90 + 0.10 * t, 1.0


def _normalized(values: Sequence[float], normalize: bool) -> List[float]:
    if not normalize:
        return [max(0.0, min(1.0, value)) for value in values]
    minimum = min(values)
    maximum = max(values)
    extent = maximum - minimum
    if extent <= 1.0e-12:
        return [max(0.0, min(1.0, value)) for value in values]
    return [(value - minimum) / extent for value in values]


def _create_color_set(shape: str) -> None:
    """Create the preview RGBA set through Maya's supported command API.

    Maya 2026's Python API 2.0 MFnMesh binding does not expose
    createColorSetWithName, although older Maya builds did. polyColorSet is
    available across the supported Maya versions and is only needed when the
    preview mesh is first created; bulk color writes remain on MFnMesh.
    """
    cmds.polyColorSet(
        shape,
        create=True,
        colorSet=COLOR_SET,
        representation="RGBA",
        clamped=True,
    )


def update_preview(
    graph: Optional[str] = None,
    width: int = 64,
    height: int = 64,
    normalize: bool = False,
) -> str:
    """Evaluate the graph and display its pattern on a colored polygon plane."""
    graph_shape = resolve_graph(graph)
    values = read_pattern(graph_shape)
    expected = int(width) * int(height)
    if len(values) != expected:
        raise RuntimeError(
            f"Pattern size is {len(values)}, but preview dimensions require {expected} "
            f"values ({width} x {height})."
        )

    shape = _preview_shape()
    if not shape or _stored_dimensions() != (int(width), int(height)):
        shape = _create_preview_mesh(int(width), int(height))

    dag_path = _mesh_dag_path(shape)
    mesh = om.MFnMesh(dag_path)
    if mesh.numVertices != expected:
        shape = _create_preview_mesh(int(width), int(height))
        mesh = om.MFnMesh(_mesh_dag_path(shape))

    color_sets = mesh.getColorSetNames()
    if COLOR_SET not in color_sets:
        _create_color_set(shape)
        # Reattach after the command modifies the mesh's color-set data.
        mesh = om.MFnMesh(_mesh_dag_path(shape))
    mapped = _normalized(values, bool(normalize))
    colors = om.MColorArray()
    vertex_ids = om.MIntArray()
    for index, value in enumerate(mapped):
        colors.append(om.MColor(_color_ramp(value)))
        vertex_ids.append(index)
    mesh.setVertexColors(colors, vertex_ids, COLOR_SET)
    try:
        cmds.polyColorSet(shape, currentColorSet=True, colorSet=COLOR_SET)
    except RuntimeError:
        pass
    cmds.setAttr(f"{shape}.displayColors", 1)
    cmds.dgdirty(shape)
    cmds.refresh(force=True)
    return PREVIEW_MESH


def delete_preview() -> None:
    if cmds.objExists(PREVIEW_GROUP):
        cmds.delete(PREVIEW_GROUP)
    elif cmds.objExists(PREVIEW_MESH):
        cmds.delete(PREVIEW_MESH)
