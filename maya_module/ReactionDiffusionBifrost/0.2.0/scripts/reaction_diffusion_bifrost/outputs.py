"""Optional adapters made from standard Bifrost geometry/image nodes."""
import math
import maya.cmds as cmds
from . import graph_setup as setup


def add_image(graph, pattern, width, height):
    """Connect a 2D pattern (or a volume slice) to an ordinary Bifrost image."""
    if int(width) != width or int(height) != height or width < 1 or height < 1:
        raise ValueError("Image dimensions must be positive integers")
    pixels = setup._add_node_at(graph, "/", setup.NAMESPACE, "reaction_diffusion_pixels")
    image = setup._add_node_at(graph, "/", "File::Image", "construct_image")
    cmds.vnnConnect(graph, pattern, pixels+".pattern")
    cmds.vnnConnect(graph, pixels+".pixels", image+".pixels")
    for port, value in (("width", width), ("height", height)):
        setup._set_default(graph, image, port, str(int(value)))
    cmds.vnnNode(graph, "/output", createInputPort=("image", "Object"))
    cmds.vnnConnect(graph, image+".image", "/output.image")
    texture = setup._add_node_at(graph, "/", "File::Image", "construct_texture")
    cmds.vnnNode(graph, texture, createInputPort=("tile_images.image", "Object"))
    cmds.vnnNode(graph, texture, createInputPort=("tile_coordinates.uv", "Math::int2"))
    cmds.vnnNode(graph, texture, setPortDefaultValues=("tile_coordinates.uv", "{0,0}"))
    cmds.vnnConnect(graph, image+".image", texture+".tile_images.image")
    cmds.vnnNode(graph, "/output", createInputPort=("texture", "Object"))
    cmds.vnnConnect(graph, texture+".texture", "/output.texture")
    return image


def add_volume_mesh(graph, points, detail_size=0.04):
    """Voxelize threshold-selected particles and mesh their standard level set.

    This is a geometry conversion, not a lossless copy of the solver grid.
    The original concentration arrays remain available unchanged.
    """
    if not math.isfinite(detail_size) or detail_size <= 0:
        raise ValueError("detail_size must be finite and positive")
    volume = setup._add_node_at(graph, "/", "Geometry::Converters", "points_to_volume")
    mesh = setup._add_node_at(graph, "/", "Geometry::Converters", "volume_to_mesh")
    cmds.vnnConnect(graph, points, volume+".points")
    setup._set_default(graph, volume, "detail_size", str(detail_size))
    cmds.vnnNode(graph, mesh, createInputPort=("volumes.volume", "Object"))
    cmds.vnnConnect(graph, volume+".volume", mesh+".volumes.volume")
    for name, source in (("volume", volume+".volume"), ("mesh", mesh+".meshes")):
        datatype = cmds.vnnNode(graph, source.rsplit('.',1)[0], queryPortDataType=source.rsplit('.',1)[1])
        cmds.vnnNode(graph, "/output", createInputPort=(name,datatype))
        cmds.vnnConnect(graph, source, "/output."+name)
    return {"volume_node":volume,"mesh_node":mesh}
