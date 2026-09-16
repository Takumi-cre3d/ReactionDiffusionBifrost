"""Reaction Diffusion Bifrost Maya tools."""

__version__ = "0.2.0"


def create_surface_graph(mesh, **kwargs):
    from .spatial import create_surface_graph as create
    return create(mesh, **kwargs)


def create_volume_graph(**kwargs):
    from .spatial import create_volume_graph as create
    return create(**kwargs)


def show():
    from .ui import show as show_ui
    return show_ui()


def set_target_from_selection():
    from .paint_context import set_target_from_selection as set_target
    return set_target()


def paint_add():
    from .paint_context import activate
    return activate("add")


def paint_erase():
    from .paint_context import activate
    return activate("erase")


def refresh_preview(graph=None, width=64, height=64, normalize=False):
    from .preview import update_preview
    return update_preview(graph, width, height, normalize)


def sync_seeds(graph=None):
    from .bridge import sync_painted_seeds
    return sync_painted_seeds(graph)


def create_preview_graph(width=64, height=64, substeps=120):
    from .graph_setup import create_preview_graph as create_graph
    return create_graph(width, height, substeps)


def create_stateful_preview_graph(width=64, height=64, substeps_per_frame=15, start_frame=1.0):
    from .graph_setup import create_stateful_preview_graph as create_graph
    return create_graph(width, height, substeps_per_frame, start_frame)
