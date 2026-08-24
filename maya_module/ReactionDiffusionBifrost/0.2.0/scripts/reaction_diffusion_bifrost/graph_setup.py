"""Create a ready-to-evaluate ReactionDiffusion Bifrost preview graph."""

from __future__ import annotations

from typing import Dict

import maya.cmds as cmds


GRAPH_NAME = "RD_ReactionDiffusionGraph"
STATEFUL_GRAPH_NAME = "RD_ReactionDiffusionSimulation"
NAMESPACE = "Takumi::ReactionDiffusion"
INITIALIZE_TYPE = "reaction_diffusion_initialize_grid"
STEP_TYPE = "reaction_diffusion_grid_step"
INITIALIZE_STATE_TYPE = "reaction_diffusion_initialize_state"
STATE_STEP_TYPE = "reaction_diffusion_state_step"
STATE_OUTPUTS_TYPE = "reaction_diffusion_state_outputs"


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


def _add_node_at(
    graph: str,
    root: str,
    namespace: str,
    node_type: str,
) -> str:
    created = cmds.vnnCompound(
        graph,
        root,
        addNode=f"BifrostGraph,{namespace},{node_type}",
    )
    if not created:
        raise RuntimeError(f"Bifrost did not create {namespace}::{node_type}.")
    returned = str(created[-1])
    if returned.startswith("/"):
        return returned
    return f"{root.rstrip('/')}/{returned.lstrip('./')}"


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
    bracket_open = False
    try:
        cmds.vnnChangeBracket(graph, open=True)
        bracket_open = True
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

        cmds.vnnChangeBracket(graph, close=True)
        bracket_open = False
        cmds.dgdirty(graph)
        return {
            "transform": transform,
            "graph": graph,
            "initialize_node": _node_path(initialize),
            "step_node": _node_path(step),
        }
    except Exception:
        if bracket_open and cmds.objExists(graph):
            try:
                cmds.vnnChangeBracket(graph, close=True)
            except RuntimeError:
                pass
        if cmds.objExists(transform):
            cmds.delete(transform)
        raise


def create_stateful_preview_graph(
    width: int = 64,
    height: int = 64,
    substeps_per_frame: int = 15,
    start_frame: float = 1.0,
) -> Dict[str, str]:
    """Create a frame-driven graph using a native packed Feedback State.

    Bifrost owns the feedback cache. Native operators remain stateless and
    receive an explicit ``[A..., B...]`` float array on every execution.
    """
    width = int(width)
    height = int(height)
    substeps_per_frame = int(substeps_per_frame)
    start_frame = float(start_frame)
    if width < 3 or height < 3:
        raise ValueError("Stateful graph dimensions must be at least 3 x 3.")
    if substeps_per_frame < 1:
        raise ValueError("Substeps per frame must be at least 1.")

    _load_bifrost_plugins()
    transform = cmds.createNode("transform", name=STATEFUL_GRAPH_NAME)
    graph = cmds.createNode(
        "bifrostGraphShape",
        name=f"{transform}Shape",
        parent=transform,
    )
    bracket_open = False
    try:
        cmds.vnnChangeBracket(graph, open=True)
        bracket_open = True
        if "output" not in (cmds.vnnCompound(graph, "/", listNodes=True) or []):
            cmds.vnnCompound(graph, "/", addIONode=False)

        initialize = _add_node_at(graph, "/", NAMESPACE, INITIALIZE_STATE_TYPE)
        simulation = _add_node_at(
            graph, "/", "Simulation::Common", "simulation_example")
        cmds.vnnCompound(graph, simulation, setIsReferenced=False)
        for port in ("starting_data", "state", "out_state"):
            cmds.vnnNode(
                graph, simulation, setPortDataType=(port, "array<float>"))
            cmds.vnnCompound(
                graph, simulation, setPortDataType=(port, "array<float>"))
        step = _add_node_at(graph, simulation, NAMESPACE, STATE_STEP_TYPE)
        outputs = _add_node_at(graph, "/", NAMESPACE, STATE_OUTPUTS_TYPE)

        for node in (initialize, step, outputs):
            _set_default(graph, node, "width", str(width))
            _set_default(graph, node, "height", str(height))
        _set_default(graph, initialize, "seed_u", "{0.5}")
        _set_default(graph, initialize, "seed_v", "{0.5}")
        _set_default(graph, initialize, "seed_radius", "{0.08}")
        _set_default(graph, initialize, "seed_strength", "{1.0}")
        _set_default(graph, initialize, "seed_mode", "{0}")
        for port, value in (
            ("feed_rate", "0.055"),
            ("kill_rate", "0.062"),
            ("diffusion_a", "1.0"),
            ("diffusion_b", "0.5"),
            ("time_step", "1.0"),
            ("substeps", str(substeps_per_frame)),
        ):
            _set_default(graph, step, port, value)
        _set_default(graph, simulation, "start_frame", format(start_frame, ".9g"))

        # The standard simulation template expects its initial value on both
        # starting_data and the externally resolved feedback input.
        _connect(graph, initialize, "state", f"{simulation}.starting_data")
        _connect(graph, initialize, "state", f"{simulation}.state")
        cmds.vnnConnect(
            graph, f"{simulation}/pass.output",
            f"{simulation}/manage_simulation_state.reset_case")
        cmds.vnnConnect(graph, f"{simulation}/pass1.output", f"{step}.state")
        cmds.vnnConnect(
            graph, f"{step}.out_state",
            f"{simulation}/manage_simulation_state.next_step_case")
        for placeholder in (
            "YOUR_RESET", "YOUR_SIMULATION", "RESET_EXAMPLE", "SIMULATION_EXAMPLE"):
            nodes = cmds.vnnCompound(graph, simulation, listNodes=True) or []
            if placeholder in nodes:
                cmds.vnnCompound(graph, simulation, removeNode=placeholder)

        for port, data_type in (
            ("backend_used", "string"),
            ("status", "string"),
            ("elapsed_milliseconds", "float"),
        ):
            cmds.vnnCompound(graph, simulation, createOutputPort=(port, data_type))
            cmds.vnnConnect(graph, f"{step}.{port}", f"{simulation}.{port}")

        _connect(graph, simulation, "out_state", f"{outputs}.state")
        for port, data_type, source_node in (
            ("state", "array<float>", simulation),
            ("concentration_a", "array<float>", outputs),
            ("concentration_b", "array<float>", outputs),
            ("pattern", "array<float>", outputs),
            ("gradient_u", "array<float>", outputs),
            ("gradient_v", "array<float>", outputs),
            ("backend_used", "string", simulation),
            ("status", "string", simulation),
            ("elapsed_milliseconds", "float", simulation),
        ):
            cmds.vnnNode(graph, "/output", createInputPort=(port, data_type))
            source_port = "out_state" if port == "state" else port
            _connect(graph, source_node, source_port, f"/output.{port}")

        cmds.vnnChangeBracket(graph, close=True)
        bracket_open = False
        cmds.dgdirty(graph)
        return {
            "transform": transform,
            "graph": graph,
            "initialize_node": initialize,
            "simulation_node": simulation,
            "step_node": step,
            "outputs_node": outputs,
        }
    except Exception:
        if bracket_open and cmds.objExists(graph):
            try:
                cmds.vnnChangeBracket(graph, close=True)
            except RuntimeError:
                pass
        if cmds.objExists(transform):
            cmds.delete(transform)
        raise
