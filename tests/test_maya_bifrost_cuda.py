"""Maya/Bifrost integration check for the installed CUDA operator pack.

Run with Maya 2026 mayapy after building and installing the Bifrost pack. The
test creates an unsaved temporary bifrostBoard, evaluates a small grid, and
checks that Backend::Auto selected the native CUDA implementation.
"""

from __future__ import annotations

import os

import maya.standalone


def _add_node(cmds, graph: str, namespace: str, node_type: str) -> str:
    created = cmds.vnnCompound(
        graph,
        ".",
        addNode=f"BifrostGraph,{namespace},{node_type}",
    )
    if not created:
        raise RuntimeError(f"Bifrost did not create {namespace}::{node_type}.")
    return str(created[-1])


def _set_default(cmds, graph: str, node: str, port: str, value: str) -> None:
    cmds.vnnNode(graph, f".{node}", setPortDefaultValues=(port, value))


def main() -> None:
    maya_already_initialized = os.environ.get("RD_MAYA_ALREADY_INITIALIZED") == "1"
    if not maya_already_initialized:
        maya.standalone.initialize(name="python")
    try:
        import maya.cmds as cmds

        print("integrationPhase=load_plugins", flush=True)
        if not cmds.pluginInfo("bifrostGraph", query=True, loaded=True):
            cmds.loadPlugin("bifrostGraph")
        if not cmds.pluginInfo("mayaVnnPlugin", query=True, loaded=True):
            cmds.loadPlugin("mayaVnnPlugin")

        print("integrationPhase=create_graph", flush=True)
        graph = cmds.createNode("bifrostBoard", name="rdCudaIntegrationTest")
        if "output" not in (cmds.vnnCompound(graph, ".", listNodes=True) or []):
            cmds.vnnCompound(graph, ".", addIONode=False)

        print("integrationPhase=create_nodes", flush=True)
        namespace = "Takumi::ReactionDiffusion"
        initialize = _add_node(
            cmds, graph, namespace, "reaction_diffusion_initialize_grid"
        )
        step = _add_node(cmds, graph, namespace, "reaction_diffusion_grid_step")

        print("integrationPhase=configure_nodes", flush=True)
        for node in (initialize, step):
            _set_default(cmds, graph, node, "width", "64")
            _set_default(cmds, graph, node, "height", "48")
        for port, value in (
            ("feed_rate", "0.055"),
            ("kill_rate", "0.062"),
            ("diffusion_a", "1.0"),
            ("diffusion_b", "0.5"),
            ("time_step", "1.0"),
        ):
            _set_default(cmds, graph, step, port, value)
        _set_default(cmds, graph, step, "substeps", "8")
        _set_default(cmds, graph, step, "seed_u", "{0.37}")
        _set_default(cmds, graph, step, "seed_v", "{0.61}")
        _set_default(cmds, graph, step, "seed_radius", "{0.08}")
        _set_default(cmds, graph, step, "seed_strength", "{1.0}")
        _set_default(cmds, graph, step, "seed_mode", "{0}")

        print("integrationPhase=connect_nodes", flush=True)
        cmds.vnnConnect(
            graph,
            f".{initialize}.concentration_a",
            f".{step}.concentration_a",
        )
        cmds.vnnConnect(
            graph,
            f".{initialize}.concentration_b",
            f".{step}.concentration_b",
        )
        for port, data_type in (
            ("backend_used", "string"),
            ("status", "string"),
            ("elapsed_milliseconds", "float"),
        ):
            cmds.vnnNode(graph, ".output", createInputPort=(port, data_type))
            cmds.vnnConnect(graph, f".{step}.{port}", f".output.{port}")

        print("integrationPhase=evaluate", flush=True)
        backend = cmds.getAttr(f"{graph}.backend_used")
        status = cmds.getAttr(f"{graph}.status")
        elapsed = float(cmds.getAttr(f"{graph}.elapsed_milliseconds"))
        if backend != "CUDA":
            raise AssertionError(f"Expected CUDA backend, got {backend!r}: {status}")
        if status != "auto_selected_cuda":
            raise AssertionError(f"Unexpected CUDA status: {status!r}")
        if elapsed < 0.0:
            raise AssertionError(f"Invalid elapsed time: {elapsed}")
        print("ReactionDiffusion Maya/Bifrost CUDA integration: PASS")
        print(f"backendUsed={backend}")
        print(f"status={status}")
        print(f"elapsedMilliseconds={elapsed:.6f}")

        # Invalid node values must become diagnostic outputs, never an
        # exception escaping the Bifrost ABI and terminating Maya.
        _set_default(cmds, graph, step, "time_step", "0.0")
        cmds.dgdirty(graph)
        invalid_backend = cmds.getAttr(f"{graph}.backend_used")
        invalid_status = cmds.getAttr(f"{graph}.status")
        if invalid_backend != "ERROR" or not invalid_status.startswith("error: "):
            raise AssertionError(
                f"Invalid input was not contained: {invalid_backend!r}, {invalid_status!r}"
            )
        print("invalidInputContainment=PASS")
        print(f"invalidStatus={invalid_status}")
    finally:
        if not maya_already_initialized:
            maya.standalone.uninitialize()


if __name__ == "__main__":
    main()
