"""Persistent, editable stroke payload stored in the Maya scene."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, Iterable, List

import maya.cmds as cmds


SCHEMA = "takumi.reaction-diffusion-strokes/1"
DATA_NODE = "RD_SeedStrokes_DATA"


def empty_payload() -> Dict[str, Any]:
    return {
        "schema": SCHEMA,
        "target": "",
        "uvSet": "map1",
        "strokes": [],
    }


def ensure_data_node() -> str:
    node = DATA_NODE if cmds.objExists(DATA_NODE) else cmds.createNode("network", name=DATA_NODE)
    if not cmds.attributeQuery("rdSchema", node=node, exists=True):
        cmds.addAttr(node, longName="rdSchema", dataType="string")
    if not cmds.attributeQuery("rdStrokePayload", node=node, exists=True):
        cmds.addAttr(node, longName="rdStrokePayload", dataType="string")
    if not cmds.attributeQuery("rdStrokeCount", node=node, exists=True):
        cmds.addAttr(node, longName="rdStrokeCount", attributeType="long")
    if not cmds.attributeQuery("rdSampleCount", node=node, exists=True):
        cmds.addAttr(node, longName="rdSampleCount", attributeType="long")
    cmds.setAttr(f"{node}.rdSchema", SCHEMA, type="string")
    if not cmds.getAttr(f"{node}.rdStrokePayload"):
        initial = empty_payload()
        cmds.setAttr(
            f"{node}.rdStrokePayload",
            json.dumps(initial, separators=(",", ":")),
            type="string",
        )
        cmds.setAttr(f"{node}.rdStrokeCount", 0)
        cmds.setAttr(f"{node}.rdSampleCount", 0)
    return node


def read() -> Dict[str, Any]:
    node = ensure_data_node()
    raw = cmds.getAttr(f"{node}.rdStrokePayload") or ""
    if not raw:
        return empty_payload()
    try:
        data = json.loads(raw)
    except (TypeError, ValueError):
        cmds.warning("ReactionDiffusion: Invalid stored stroke payload; using an empty payload.")
        return empty_payload()
    if data.get("schema") != SCHEMA:
        cmds.warning("ReactionDiffusion: Unsupported stroke payload schema.")
    return data


def write(data: Dict[str, Any]) -> None:
    node = ensure_data_node()
    strokes = data.get("strokes", [])
    sample_count = sum(len(stroke.get("samples", [])) for stroke in strokes)
    cmds.setAttr(f"{node}.rdStrokePayload", json.dumps(data, separators=(",", ":")), type="string")
    cmds.setAttr(f"{node}.rdStrokeCount", len(strokes))
    cmds.setAttr(f"{node}.rdSampleCount", sample_count)


def append_stroke(stroke: Dict[str, Any], target: str, uv_set: str) -> None:
    data = read()
    data["target"] = target
    data["uvSet"] = uv_set
    data.setdefault("strokes", []).append(stroke)
    write(data)


def clear() -> None:
    write(empty_payload())
    if cmds.objExists("RD_SeedStrokes_GRP"):
        cmds.delete("RD_SeedStrokes_GRP")


def export_json(path: str) -> str:
    destination = Path(path)
    destination.write_text(json.dumps(read(), ensure_ascii=False, indent=2), encoding="utf-8")
    return str(destination)


def flatten_for_bifrost() -> Dict[str, List[Any]]:
    """Return flat arrays matching the v0.1 Bifrost operator seed ports."""
    result: Dict[str, List[Any]] = {
        "seed_u": [],
        "seed_v": [],
        "seed_radius": [],
        "seed_strength": [],
        "seed_mode": [],
        "stroke_id": [],
        "world_position": [],
        "face_id": [],
        "barycentric": [],
    }
    for stroke in read().get("strokes", []):
        mode = 1 if stroke.get("mode") == "erase" else 0
        stroke_id = int(stroke.get("id", 0))
        radius = float(stroke.get("radius", 0.02))
        strength = float(stroke.get("strength", 1.0))
        for sample in stroke.get("samples", []):
            uv = sample.get("uv", [0.0, 0.0])
            result["seed_u"].append(float(uv[0]))
            result["seed_v"].append(float(uv[1]))
            result["seed_radius"].append(radius)
            result["seed_strength"].append(strength)
            result["seed_mode"].append(mode)
            result["stroke_id"].append(stroke_id)
            result["world_position"].append(sample.get("position", [0.0, 0.0, 0.0]))
            result["face_id"].append(int(sample.get("face", -1)))
            result["barycentric"].append(sample.get("barycentric", [0.0, 0.0, 0.0]))
    return result


def counts() -> tuple[int, int]:
    data = read()
    strokes = data.get("strokes", [])
    return len(strokes), sum(len(stroke.get("samples", [])) for stroke in strokes)
