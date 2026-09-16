"""Parameter values from TuringPattern_Generator, retrieved 2026-09-17."""
import maya.cmds as cmds
from . import preview, graph_setup

PRESETS={
    "Mitosis":(0.031,0.066,1.10,0.40), "Coral":(0.0545,0.062,1.0,0.5),
    "Spots":(0.010,0.055,1.17,0.24), "Stripes":(0.025,0.055,1.10,0.39),
    "Worms":(0.032,0.059,1.06,0.64),
}


def apply(graph, name):
    values=PRESETS[name]
    graph=preview.resolve_graph(graph)
    ports=("feed_rate","kill_rate","diffusion_a","diffusion_b")
    if cmds.objExists(graph+".feed_rate"):
        for port,value in zip(ports,values): cmds.setAttr(graph+"."+port,value)
    else:
        nodes=["/"+n for n in cmds.vnnCompound(graph,"/",listNodes=True) or []]
        for node in list(nodes):
            if "simulation_example" in node:
                nodes.extend(node+"/"+n for n in cmds.vnnCompound(graph,node,listNodes=True) or [])
        steps=[n for n in nodes if n.endswith(("reaction_diffusion_grid_step","reaction_diffusion_state_step"))]
        if len(steps)!=1: raise ValueError("Expected exactly one solver")
        for port,value in zip(ports,values): graph_setup._set_default(graph,steps[0],port,str(value))
    cmds.dgdirty(graph)
