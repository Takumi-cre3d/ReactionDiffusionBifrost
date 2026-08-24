"""Bridge editable Maya seed strokes to native Bifrost operator ports."""

from __future__ import annotations

from typing import Any, Dict, Iterable, List, Optional, Sequence

import maya.cmds as cmds

from . import payload, preview


STEP_NODE_TOKEN = "reaction_diffusion_grid_step"
INITIALIZE_NODE_TOKEN = "reaction_diffusion_initialize_grid"
SEED_PORTS = ("seed_u", "seed_v", "seed_radius", "seed_strength", "seed_mode")


def _node_paths(graph: str) -> List[str]:
    nodes = cmds.vnnCompound(graph, "/", listNodes=True) or []
    result = []
    for node in nodes:
        name = str(node)
        result.append(name if name.startswith("/") else f"/{name}")
    return result


def _find_node(graph: str, token: str, required: bool = True) -> Optional[str]:
    matches = [path for path in _node_paths(graph) if token.lower() in path.lower()]
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        exact = [path for path in matches if path.rsplit("/", 1)[-1] == token]
        if len(exact) == 1:
            return exact[0]
        raise RuntimeError(f"Multiple Bifrost nodes match '{token}': {', '.join(matches)}")
    if required:
        raise RuntimeError(
            f"'{token}' was not found in the top-level graph. "
            "Open the graph that contains the reaction-diffusion nodes."
        )
    return None


def find_step_node(graph: Optional[str] = None) -> str:
    graph_shape = preview.resolve_graph(graph)
    result = _find_node(graph_shape, STEP_NODE_TOKEN, required=True)
    assert result is not None
    return result


def _array_literal(values: Sequence[Any], integer: bool = False) -> str:
    if integer:
        tokens = [str(int(value)) for value in values]
    else:
        tokens = [format(float(value), ".9g") for value in values]
    return "{" + ", ".join(tokens) + "}"


def _set_port_default(graph: str, node: str, port: str, value: str) -> None:
    try:
        cmds.vnnNode(graph, node, setPortDefaultValues=[port, value])
    except RuntimeError as exception:
        raise RuntimeError(
            f"Could not set {node}.{port}. The port may already be connected: {exception}"
        ) from exception


def set_grid_dimensions(graph: Optional[str], width: int, height: int) -> str:
    graph_shape = preview.resolve_graph(graph)
    width = int(width)
    height = int(height)
    if width < 3 or height < 3:
        raise ValueError("Grid dimensions must be at least 3 x 3.")
    step_node = find_step_node(graph_shape)
    for port, value in (("width", width), ("height", height)):
        _set_port_default(graph_shape, step_node, port, str(value))
    initialize_node = _find_node(graph_shape, INITIALIZE_NODE_TOKEN, required=False)
    if initialize_node:
        for port, value in (("width", width), ("height", height)):
            _set_port_default(graph_shape, initialize_node, port, str(value))
    return graph_shape


def sync_painted_seeds(graph: Optional[str] = None) -> Dict[str, int]:
    """Write the stored Painter arrays into unconnected native step-node ports."""
    graph_shape = preview.resolve_graph(graph)
    step_node = find_step_node(graph_shape)
    arrays = payload.flatten_for_bifrost()
    for port in SEED_PORTS:
        _set_port_default(
            graph_shape,
            step_node,
            port,
            _array_literal(arrays[port], integer=(port == "seed_mode")),
        )
    cmds.dgdirty(graph_shape)
    return {
        "strokes": payload.counts()[0],
        "samples": len(arrays["seed_u"]),
    }


def set_total_steps(graph: Optional[str], total_steps: int) -> str:
    graph_shape = preview.resolve_graph(graph)
    step_node = find_step_node(graph_shape)
    _set_port_default(graph_shape, step_node, "substeps", str(max(0, int(total_steps))))
    cmds.dgdirty(graph_shape)
    return graph_shape


def evaluate(
    graph: Optional[str],
    width: int,
    height: int,
    total_steps: int,
    normalize: bool = False,
    sync_seeds: bool = True,
) -> Dict[str, Any]:
    """Configure, evaluate, and visualize the deterministic 2D simulation."""
    graph_shape = set_grid_dimensions(graph, width, height)
    if sync_seeds:
        sync_painted_seeds(graph_shape)
    set_total_steps(graph_shape, total_steps)
    preview_mesh = preview.update_preview(graph_shape, width, height, normalize)
    result: Dict[str, Any] = {
        "graph": graph_shape,
        "preview": preview_mesh,
        "steps": max(0, int(total_steps)),
        "pattern_size": width * height,
    }
    for attribute in ("backend_used", "status", "elapsed_milliseconds"):
        plug = f"{graph_shape}.{attribute}"
        result[attribute] = cmds.getAttr(plug) if cmds.objExists(plug) else None
    return result
