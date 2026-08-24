"""Create a ready-to-evaluate ReactionDiffusion Bifrost preview graph."""

from __future__ import annotations

from typing import Dict

import maya.cmds as cmds


GRAPH_NAME = "RD_ReactionDiffusionGraph"
NAMESPACE = "Takumi::ReactionDiffusion"
INITIALIZE_TYPE = "reaction_diffusion_initialize_grid"
STEP_TYPE = "reaction_diffusion_grid_step"


def _load_bifrost_plugins() -> None:
    for plugin in ("bifrostGraph", "mayaVnnPlugin"):
        if not cmds.pluginInfo(plugin, query=True, loaded=True):
            cmds.loadPlugin(plugin)


def _add_node(graph: str, node_type: str) -> str:
    created = cmds.vnnCompound(
        graph,
        "/",
        addNode=f"BifrostGraph,{NAMESPACE},{node_type}",
    )
    if not created:
        raise RuntimeError(f"Bifrost did not create {NAMESPACE}::{node_type}.")
    return str(created[-1]).lstrip("./")


def _node_path(node: str) -> str:
    return node if node.startswith("/") else f"/{node}"


def _set_default(graph: str, node: str, port: str, value: str) -> None:
    cmds.vnnNode(
        graph,
        _node_path(node),
        setPortDefaultValues=(port, value),
    )


def _connect(graph: str, source_node: str, source_port: str, target: str) -> None:
    cmds.vnnConnect(
        graph,
        f"{_node_path(source_node)}.{source_port}",
        target,
    )


def create_preview_graph(
    width: int = 64,
    height: int = 64,
    substeps: int = 120,
) -> Dict[str, str]:
    """Create a self-contained 2D graph with a visible centered sample seed.

    The graph exposes ``pattern`` and diagnostic values as Maya attributes so
    the controller can create its display-only vertex-color preview without
    requiring users to wire a graph by hand.
    """
    width = int(width)
    height = int(height)
    substeps = int(substeps)
    if width < 3 or height < 3:
        raise ValueError("Preview graph dimensions must be at least 3 x 3.")
    if substeps < 0:
        raise ValueError("Preview graph substeps must be zero or greater.")

    _load_bifrost_plugins()
    transform = cmds.createNode("transform", name=GRAPH_NAME)
    graph = cmds.createNode(
        "bifrostGraphShape",
        name=f"{transform}Shape",
        parent=transform,
    )
    try:
        if "output" not in (cmds.vnnCompound(graph, "/", listNodes=True) or []):
            cmds.vnnCompound(graph, "/", addIONode=False)

        initialize = _add_node(graph, INITIALIZE_TYPE)
        step = _add_node(graph, STEP_TYPE)
        for node in (initialize, step):
            _set_default(graph, node, "width", str(width))
            _set_default(graph, node, "height", str(height))
        for port, value in (
            ("feed_rate", "0.055"),
            ("kill_rate", "0.062"),
            ("diffusion_a", "1.0"),
            ("diffusion_b", "0.5"),
            ("time_step", "1.0"),
            ("substeps", str(substeps)),
            ("seed_u", "{0.5}"),
            ("seed_v", "{0.5}"),
            ("seed_radius", "{0.08}"),
            ("seed_strength", "{1.0}"),
            ("seed_mode", "{0}"),
        ):
            _set_default(graph, step, port, value)

        _connect(graph, initialize, "concentration_a", f"{_node_path(step)}.concentration_a")
        _connect(graph, initialize, "concentration_b", f"{_node_path(step)}.concentration_b")
        for port, data_type in (
            ("pattern", "array<float>"),
            ("backend_used", "string"),
            ("status", "string"),
            ("elapsed_milliseconds", "float"),
        ):
            cmds.vnnNode(graph, "/output", createInputPort=(port, data_type))
            _connect(graph, step, port, f"/output.{port}")

        cmds.dgdirty(graph)
        return {
            "transform": transform,
            "graph": graph,
            "initialize_node": _node_path(initialize),
            "step_node": _node_path(step),
        }
    except Exception:
        if cmds.objExists(transform):
            cmds.delete(transform)
        raise
