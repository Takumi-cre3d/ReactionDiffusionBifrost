"""Surface and volume Feedback graphs; numerical work stays in native nodes."""
from __future__ import annotations

import math
import maya.cmds as cmds
import maya.api.OpenMaya as om
from . import graph_setup as setup
from .bridge import _array_literal


def create_graph(domain, positions=(), triangles=(), dimensions=(24, 24, 24),
                 substeps=8, start_frame=0.0, seed_position=None, seed_radius=None):
    """Create Surface/Volume native feedback with editable top-level inputs.

    Surface positions and seeds share object-space units. Volume seeds use
    normalized coordinates. All exposed A/B outputs can feed later operators.
    """
    if domain not in ("surface", "volume"):
        raise ValueError("domain must be surface or volume")
    if not math.isfinite(substeps) or int(substeps) != substeps or substeps < 1 or not math.isfinite(start_frame):
        raise ValueError("Invalid substeps/start frame")
    if domain == "surface":
        if not positions or len(positions) % 3 or not triangles or len(triangles) % 3:
            raise ValueError("Surface requires XYZ and triangle triples")
        if not all(math.isfinite(v) for v in positions):
            raise ValueError("Non-finite positions")
        if any(not math.isfinite(i) or int(i) != i or i < 0 or i >= len(positions)//3 for i in triangles):
            raise ValueError("Triangle index out of range")
    elif len(dimensions) != 3 or any(int(v) != v or v < 3 for v in dimensions):
        raise ValueError("Volume dimensions must be three integers >= 3")
    if domain == "surface":
        seed_position = positions[:3] if seed_position is None else seed_position
        extent = max(max(positions[d::3])-min(positions[d::3]) for d in range(3))
        seed_radius = max(extent*0.1, 1e-6) if seed_radius is None else seed_radius
    else:
        seed_position = (0.5, 0.5, 0.5) if seed_position is None else seed_position
        seed_radius = 0.15 if seed_radius is None else seed_radius
    if len(seed_position) != 3 or not all(math.isfinite(v) for v in seed_position) or not math.isfinite(seed_radius) or seed_radius <= 0:
        raise ValueError("Invalid spatial seed")
    setup._load_bifrost_plugins()
    transform = cmds.createNode("transform", name="RD_"+domain.title()+"Simulation")
    graph = cmds.createNode("bifrostGraphShape", name=transform+"Shape", parent=transform)
    opened = False
    try:
        cmds.vnnChangeBracket(graph, open=True)
        opened = True
        for name, direction in (("input", True), ("output", False)):
            if name not in (cmds.vnnCompound(graph, "/", listNodes=True) or []):
                cmds.vnnCompound(graph, "/", addIONode=direction)
        add = lambda root, kind: setup._add_node_at(graph, root, setup.NAMESPACE, kind)
        connect = lambda source, target: cmds.vnnConnect(graph, source, target)
        default = lambda node, port, value: setup._set_default(graph, node, port, str(value))
        simulation = setup._add_node_at(graph, "/", "Simulation::Common", "simulation_example")
        cmds.vnnCompound(graph, simulation, setIsReferenced=False)
        for port in ("starting_data", "state", "out_state"):
            cmds.vnnNode(graph, simulation, setPortDataType=(port, "array<float>"))
            cmds.vnnCompound(graph, simulation, setPortDataType=(port, "array<float>"))
        kind = "reaction_diffusion_"+domain+"_step"
        initial = add("/", kind)
        step = add(simulation, kind)
        from .seed_events import connect_events
        connect_events(graph, step, domain)
        outputs = add("/", kind)
        initial_pack = add("/", "reaction_diffusion_pack_state")
        next_pack = add(simulation, "reaction_diffusion_pack_state")
        previous = add(simulation, "reaction_diffusion_unpack_state")
        current = add("/", "reaction_diffusion_unpack_state")
        for node in (initial, outputs):
            default(node, "substeps", 0)
        default(step, "substeps", substeps)
        default(simulation, "start_frame", start_frame)
        shared = [("feed_rate", "float", "0.055"), ("kill_rate", "float", "0.062"),
                  ("diffusion_a", "float", "1"), ("diffusion_b", "float", "0.5"), ("time_step", "float", "1")]
        if domain == "surface":
            shared += [("positions", "array<float>", _array_literal(positions)),
                       ("triangles", "array<int>", _array_literal(triangles, integer=True))]
        else:
            shared += [(name, "int", str(int(value))) for name, value in zip(("width", "height", "depth"), dimensions)]
            initialize = add("/", "reaction_diffusion_initialize_volume")
            for name, value in zip(("width", "height", "depth"), dimensions):
                default(initialize, name, int(value))
            for channel in ("a", "b"):
                connect(initialize+".concentration_"+channel, initial+".concentration_"+channel)
        for port, datatype, value in shared:
            cmds.vnnNode(graph, "/input", createOutputPort=(port, datatype))
            default("/input", port, value)
            cmds.vnnCompound(graph, simulation, createInputPort=(port, datatype))
            connect("/input."+port, simulation+"."+port)
            connect(simulation+"."+port, step+"."+port)
            for node in (initial, outputs):
                try:
                    connect("/input."+port, node+"."+port)
                except RuntimeError as error:
                    raise RuntimeError(f"Failed to connect input {port} ({datatype}) to {node}: {error}") from error
            if domain == "volume" and port in ("width", "height", "depth"):
                connect("/input."+port, initialize+"."+port)
        default(initial, "seed_radius", _array_literal([seed_radius]))
        default(initial, "seed_strength", "{1}")
        default(initial, "seed_mode", "{0}")
        if domain == "surface":
            default(initial, "seed_positions", _array_literal(seed_position))
        else:
            for port, value in zip(("seed_u", "seed_v", "seed_w"), seed_position):
                default(initial, port, _array_literal([value]))
        for channel in ("a", "b"):
            port = "concentration_"+channel
            connect(initial+".out_"+port, initial_pack+"."+port)
            connect(previous+"."+port, step+"."+port)
            connect(step+".out_"+port, next_pack+"."+port)
            connect(current+"."+port, outputs+"."+port)
        connect(initial_pack+".state", simulation+".starting_data")
        connect(initial_pack+".state", simulation+".state")
        connect(simulation+"/pass.output", simulation+"/manage_simulation_state.reset_case")
        connect(simulation+"/pass1.output", previous+".state")
        connect(next_pack+".state", simulation+"/manage_simulation_state.next_step_case")
        connect(simulation+".out_state", current+".state")
        for placeholder in ("YOUR_RESET", "YOUR_SIMULATION", "RESET_EXAMPLE", "SIMULATION_EXAMPLE"):
            if placeholder in (cmds.vnnCompound(graph, simulation, listNodes=True) or []):
                cmds.vnnCompound(graph, simulation, removeNode=placeholder)
        for port in ("concentration_a", "concentration_b", "pattern", "gradient_x", "gradient_y", "gradient_z", "state"):
            cmds.vnnNode(graph, "/output", createInputPort=(port, "array<float>"))
            source = simulation+".out_state" if port == "state" else outputs+"."+("out_" if port.startswith("concentration") else "")+port
            connect(source, "/output."+port)
        for port, datatype in (("backend_used", "string"), ("status", "string"), ("elapsed_milliseconds", "float")):
            cmds.vnnCompound(graph, simulation, createOutputPort=(port, datatype))
            connect(step+"."+port, simulation+"."+port)
            cmds.vnnNode(graph, "/output", createInputPort=(port, datatype))
            connect(simulation+"."+port, "/output."+port)
        samples, points_source = setup.add_points_output(graph, outputs+".pattern", dimensions,
                                           "/input.positions" if domain == "surface" else None)
        if domain == "volume":
            for port in ("width", "height", "depth"):
                connect("/input."+port, samples+"."+port)
        cmds.vnnChangeBracket(graph, close=True)
        opened = False
        for port, datatype, value in shared:
            plug = graph+"."+port
            if datatype.startswith("array"):
                values = positions if port == "positions" else triangles
                if cmds.attributeQuery(port, node=graph, multi=True):
                    cmds.setAttr(f"{plug}[0:{len(values)-1}]", *values, size=len(values))
                else:
                    cmds.setAttr(plug, len(values), *values, type=cmds.getAttr(plug, type=True))
            else:
                cmds.setAttr(plug, int(value) if datatype == "int" else float(value))
        cmds.addAttr(graph, longName="rdDomain", dataType="string")
        cmds.setAttr(graph+".rdDomain", domain, type="string")
        cmds.dgdirty(graph)
        return {"graph": graph, "transform": transform, "step_node": step,
                "simulation_node": simulation, "outputs_node": outputs, "initial_node": initial,
                "samples_node": samples, "points_source": points_source}
    except Exception:
        if opened:
            cmds.vnnChangeBracket(graph, close=True)
        cmds.delete(transform)
        raise


def mesh_arrays(mesh):
    selection = om.MSelectionList()
    selection.add(mesh)
    path = selection.getDagPath(0)
    if path.node().hasFn(om.MFn.kTransform):
        path.extendToShape()
    function = om.MFnMesh(path)
    positions = [v for p in function.getPoints(om.MSpace.kObject) for v in (p.x, p.y, p.z)]
    _, triangles = function.getTriangles()
    return positions, list(triangles)


def create_surface_graph(mesh, live=True, **kwargs):
    positions, triangles = mesh_arrays(mesh)
    made = create_graph("surface", positions, triangles, **kwargs)
    if not live:
        return made
    graph = made['graph']
    path = cmds.ls(mesh,long=True)[0]
    if cmds.nodeType(path) == 'transform':
        path = cmds.listRelatives(path,shapes=True,noIntermediate=True,fullPath=True)[0]
    try:
        cmds.vnnNode(graph,'/input',createOutputPort=('source_mesh','Object'),
                     portOptions='pathinfo={path="'+path+'";active=true;channels=*}')
        triangulate = setup._add_node_at(graph,'/','Geometry::Mesh','triangulate_mesh')
        structure = setup._add_node_at(graph,'/','Geometry::Mesh','get_mesh_structure')
        data = setup._add_node_at(graph,'/',setup.NAMESPACE,'reaction_diffusion_mesh_data')
        cmds.vnnConnect(graph,'/input.source_mesh',triangulate+'.mesh')
        cmds.vnnConnect(graph,triangulate+'.triangle_mesh',structure+'.mesh')
        for port in ('point_position','face_vertex'):
            cmds.vnnConnect(graph,structure+'.'+port,data+'.'+port)
        for port,dtype in (('positions','array<float>'),('triangles','array<int>')):
            for key in ('simulation_node','initial_node','outputs_node'):
                target=made[key]+'.'+port
                cmds.vnnConnect(graph,'/input.'+port,target,disconnect=True)
                cmds.vnnConnect(graph,data+'.'+port,target)
            cmds.vnnNode(graph,'/output',createInputPort=('surface_'+port,dtype))
            cmds.vnnConnect(graph,data+'.'+port,'/output.surface_'+port)
        cmds.vnnConnect(graph,'/input.positions',made['samples_node']+'.positions',disconnect=True)
        cmds.vnnConnect(graph,data+'.positions',made['samples_node']+'.positions')
        return made
    except Exception:
        cmds.delete(made['transform'])
        raise


def create_volume_graph(**kwargs):
    return create_graph("volume", **kwargs)


def update_preview(graph, normalize=False, refresh_viewport=True):
    """Surface vertex colors or the middle Z slice of a dense volume."""
    from . import preview
    domain = cmds.getAttr(graph+".rdDomain")
    cmds.dgdirty(graph)
    values = preview.read_pattern(graph)
    if domain == "volume":
        width, height, depth = (int(cmds.getAttr(graph+"."+p)) for p in ("width", "height", "depth"))
        if len(values) != width*height*depth:
            raise ValueError("Volume pattern size mismatch")
        values = values[(depth//2)*width*height:(depth//2+1)*width*height]
        shape = preview._preview_shape()
        if not shape or preview._stored_dimensions() != (width,height):
            shape = preview._create_preview_mesh(width,height)
    elif domain == "surface":
        prefix = '.surface_' if cmds.objExists(graph+'.surface_positions') else '.'
        xyz = preview._flatten_numbers(cmds.getAttr(graph+prefix+"positions"))
        triangles = [int(i) for i in preview._flatten_numbers(cmds.getAttr(graph+prefix+"triangles"))]
        if len(values)*3 != len(xyz):
            raise ValueError("Surface pattern size mismatch")
        shape = preview._preview_shape()
        points = [om.MPoint(*xyz[i:i+3]) for i in range(0,len(xyz),3)]
        if shape:
            mesh = om.MFnMesh(preview._mesh_dag_path(shape))
            _, existing = mesh.getTriangles()
            if mesh.numVertices != len(values) or list(existing) != triangles:
                preview.delete_preview()
                shape = None
        if not shape:
            function = om.MFnMesh()
            function.create(points, [3]*(len(triangles)//3), triangles)
            transform = cmds.listRelatives(function.fullPathName(), parent=True, fullPath=True)[0]
            cmds.rename(transform, preview.PREVIEW_MESH)
            shape = preview._preview_shape()
        mesh = om.MFnMesh(preview._mesh_dag_path(shape))
        if any(a != b for a,b in zip(mesh.getPoints(),points)):
            mesh.setPoints(points)
    else:
        raise ValueError("Unknown spatial domain: "+domain)
    preview.write_colors(shape, values, normalize, refresh_viewport)
    return preview.PREVIEW_MESH
