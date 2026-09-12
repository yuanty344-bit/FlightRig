#!/usr/bin/env python3
"""Validate one generated FlightBench city world without launching Gazebo."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from xml.etree import ElementTree


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--world-id", required=True)
    parser.add_argument("--scene", required=True, type=Path)
    parser.add_argument("--minimum-origin-clearance", type=float, default=20.0)
    parser.add_argument("--minimum-water-clearance", type=float, default=10.0)
    return parser.parse_args()


def point_in_polygon(point: list[float], polygon: list[list[float]]) -> bool:
    inside = False
    x, y = point
    for index, first in enumerate(polygon):
        second = polygon[(index + 1) % len(polygon)]
        x1, y1 = first
        x2, y2 = second
        if (y1 > y) != (y2 > y):
            crossing_x = (x2 - x1) * (y - y1) / (y2 - y1) + x1
            if x < crossing_x:
                inside = not inside
    return inside


def distance_to_segment(point: list[float], first: list[float], second: list[float]) -> float:
    px, py = point
    ax, ay = first
    bx, by = second
    dx = bx - ax
    dy = by - ay
    length_squared = dx * dx + dy * dy
    if length_squared == 0.0:
        return math.hypot(px - ax, py - ay)
    fraction = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length_squared))
    return math.hypot(px - (ax + fraction * dx), py - (ay + fraction * dy))


def polygon_clearance(point: list[float], polygon: list[list[float]]) -> float:
    if point_in_polygon(point, polygon):
        return 0.0
    return min(distance_to_segment(point, polygon[index], polygon[(index + 1) % len(polygon)])
               for index in range(len(polygon)))


def origin_clearance(scene: dict) -> tuple[float, float | None]:
    origin = [0.0, 0.0]
    building_clearance = min(
        polygon_clearance(origin, building["outline"])
        for building in scene.get("buildings", [])
    )
    water_polygons = [area["outline"] for area in scene.get("areas", [])
                      if area.get("kind") == "water"]
    water_clearance = min((polygon_clearance(origin, polygon) for polygon in water_polygons),
                          default=None)
    return building_clearance, water_clearance


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
    if args.minimum_origin_clearance < 0.0 or args.minimum_water_clearance < 0.0:
        raise ValueError("Origin-clearance limits cannot be negative")
    building_clearance, water_clearance = origin_clearance(scene)
    if building_clearance < args.minimum_origin_clearance:
        raise ValueError(
            f"World origin is only {building_clearance:.1f} m from a building; "
            f"at least {args.minimum_origin_clearance:.1f} m is required")
    if water_clearance is not None and water_clearance < args.minimum_water_clearance:
        raise ValueError(
            f"World origin is only {water_clearance:.1f} m from water; "
            f"at least {args.minimum_water_clearance:.1f} m is required")

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
        "originBuildingClearanceMeters": round(building_clearance, 1),
        "originWaterClearanceMeters": (round(water_clearance, 1)
                                        if water_clearance is not None else None),
        "obstacleTriangles": len(map_data["buildingRoofTriangles"]),
        "meshes": mesh_summary,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
