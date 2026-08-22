#!/usr/bin/env python3
"""Validate one generated FlightBench city world without launching Gazebo."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from xml.etree import ElementTree


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--world-id", required=True)
    parser.add_argument("--scene", required=True, type=Path)
    return parser.parse_args()


def validate_obj(path: Path, require_faces: bool, expected_material: str) -> dict[str, int]:
    vertices = 0
    normals = 0
    faces = 0
    lines = path.read_text(encoding="ascii").splitlines()
    if f"mtllib flightbench_city.mtl" not in lines:
        raise ValueError(f"OBJ has no FlightBench material library: {path}")
    if f"usemtl {expected_material}" not in lines:
        raise ValueError(f"OBJ has no expected material '{expected_material}': {path}")
    for line_number, line in enumerate(lines, start=1):
        if line.startswith("v "):
            vertices += 1
        elif line.startswith("vn "):
            normals += 1
        elif line.startswith("f "):
            faces += 1
            for token in line.split()[1:]:
                vertex_text, _, normal_text = token.partition("//")
                vertex = int(vertex_text)
                normal = int(normal_text)
                if not 1 <= vertex <= vertices or not 1 <= normal <= normals:
                    raise ValueError(f"Invalid OBJ index in {path.name}:{line_number}")
    if require_faces and (vertices == 0 or normals == 0 or faces == 0):
        raise ValueError(f"Required mesh is empty: {path}")
    return {"vertices": vertices, "normals": normals, "faces": faces}


def main() -> None:
    args = arguments()
    root = args.project_root.resolve()
    scene = json.loads(args.scene.resolve().read_text(encoding="utf-8"))
    if scene.get("id") != args.world_id:
        raise ValueError("Scene id does not match the requested world id")
    if scene.get("coordinateSystem", {}).get("type") != "local-enu":
        raise ValueError("Scene coordinate system must be local-enu")
    if not scene.get("buildings"):
        raise ValueError("A city world must contain at least one usable building")

    model_root = root / "simulation" / "models" / args.world_id
    world_path = root / "simulation" / "worlds" / f"{args.world_id}.sdf"
    ElementTree.parse(world_path)
    ElementTree.parse(model_root / "model.sdf")
    ElementTree.parse(model_root / "model.config")

    mesh_summary = {}
    material_path = model_root / "meshes" / "flightbench_city.mtl"
    material_text = material_path.read_text(encoding="ascii")
    for name in ("buildings", "roads", "green_areas", "water"):
        if f"newmtl {name}" not in material_text:
            raise ValueError(f"Material library is missing '{name}': {material_path}")
        mesh_summary[name] = validate_obj(
            model_root / "meshes" / f"{name}.obj",
            require_faces=name == "buildings",
            expected_material=name,
        )

    map_root = root / "resources" / "worldmaps"
    map_data = json.loads((map_root / f"{args.world_id}.json").read_text(encoding="utf-8"))
    if map_data.get("format") != "FlightBenchWorldMap":
        raise ValueError("Invalid FlightBench world-map format")
    if map_data.get("worldId") != args.world_id or not map_data.get("buildingRoofTriangles"):
        raise ValueError("World map has no matching building obstacles")
    for theme in ("light", "dark"):
        ElementTree.parse(map_root / f"{args.world_id}-{theme}.svg")

    catalog = json.loads((root / "simulation" / "worlds" / "catalog.json").read_text(encoding="utf-8"))
    if isinstance(catalog, dict):
        catalog = [catalog]
    if not any(item.get("id") == args.world_id for item in catalog):
        raise ValueError("World is missing from simulation/worlds/catalog.json")

    print(json.dumps({
        "worldId": args.world_id,
        "buildings": len(scene["buildings"]),
        "roads": len(scene.get("roads", [])),
        "areas": len(scene.get("areas", [])),
        "obstacleTriangles": len(map_data["buildingRoofTriangles"]),
        "meshes": mesh_summary,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
