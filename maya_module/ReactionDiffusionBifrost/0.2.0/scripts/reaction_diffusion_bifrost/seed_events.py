"""Author frame-addressed seeds; Bifrost time drives evaluation without Python."""
import json
import math
import maya.cmds as cmds
from . import graph_setup as setup
from .bridge import _array_literal


def connect_events(graph, step, domain="grid"):
    root=step.rsplit("/",1)[0] or "/"
    event=setup._add_node_at(graph,root,setup.NAMESPACE,"reaction_diffusion_seed_events")
    time=setup._add_node_at(graph,root,"Core::Time","time")
    cmds.vnnConnect(graph,time+".frame",event+".frame")
    ports=["seed_radius","seed_strength","seed_mode"]
    ports += ["seed_positions"] if domain=="surface" else ["seed_u","seed_v"]+(["seed_w"] if domain=="volume" else [])
    for port in ports:
        cmds.vnnConnect(graph,event+"."+port,step+"."+port)
    return event


def set_events(graph, events):
    """Replace event schedule; reset to start and replay after editing history.

    Each event: frame, position=(u,v,w) or object-space XYZ, radius, strength,
    mode (0 add, 1 erase, 2 set). Events on skipped frames are not applied.
    """
    arrays={name:[] for name in ("frames","positions","radii","strengths","modes")}
    for event in events:
        frame=float(event["frame"]); position=list(event["position"])
        radius=float(event["radius"]); strength=float(event.get("strength",1)); mode=event.get("mode",0)
        if len(position)!=3 or not all(math.isfinite(v) for v in [frame,radius,strength]+position) or radius<=0 or not 0<=strength<=1 or mode not in (0,1,2):
            raise ValueError("Invalid seed event")
        mode=int(mode)
        arrays["frames"].append(frame);arrays["positions"].extend(position)
        arrays["radii"].append(radius);arrays["strengths"].append(strength);arrays["modes"].append(mode)
    matches=[]
    for node in cmds.vnnCompound(graph,"/",listNodes=True) or []:
        if "simulation_example" in node:
            root="/"+node.lstrip("/")
            matches += [root+"/"+n for n in cmds.vnnCompound(graph,root,listNodes=True) or [] if "reaction_diffusion_seed_events" in n]
    if len(matches)!=1:
        raise ValueError("Graph needs exactly one seed-events node; create a new stateful graph")
    for port, values in arrays.items():
        setup._set_default(graph,matches[0],port,_array_literal(values,integer=port=="modes"))
    if not cmds.attributeQuery("rdSeedEvents",node=graph,exists=True):
        cmds.addAttr(graph,longName="rdSeedEvents",dataType="string")
    cmds.setAttr(graph+".rdSeedEvents",json.dumps(events,allow_nan=False),type="string")
    cmds.dgdirty(graph)
