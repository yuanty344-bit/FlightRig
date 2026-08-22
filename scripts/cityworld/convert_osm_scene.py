#!/usr/bin/env python3
"""Convert an Overpass JSON extract into a reusable FlightBench ENU scene.

The converter intentionally keeps the source data untouched. It uses an
equirectangular projection around the scene origin, which is sufficiently
accurate for a small urban simulation scene.
"""

from __future__ import annotations

import argparse
import html
import json
import math
from pathlib import Path
from typing import Any, Iterable


EARTH_RADIUS_METERS = 6_378_137.0
DEFAULT_LEVEL_HEIGHT_METERS = 3.2


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="Overpass JSON input")
    parser.add_argument("scene", type=Path, help="FlightBench scene JSON output")
    parser.add_argument("--preview", type=Path, help="Optional SVG preview output")
    parser.add_argument("--world-id", required=True,
                        help="Stable lowercase id, for example xian_block_01")
    parser.add_argument("--name", required=True, help="Display name")
    parser.add_argument("--scene-type", default="urban",
                        choices=("campus", "urban", "residential", "commercial",
                                 "industrial", "waterfront", "park"))
    boundary = parser.add_mutually_exclusive_group(required=True)
    boundary.add_argument("--boundary-way", type=int,
                          help="OSM way used as the clipping boundary")
    boundary.add_argument("--bounds", nargs=4, type=float,
                          metavar=("SOUTH", "WEST", "NORTH", "EAST"),
                          help="Rectangular WGS84 clipping boundary")
    return parser.parse_args()


def local_xy(latitude: float, longitude: float, origin_lat: float, origin_lon: float) -> list[float]:
    x_east = math.radians(longitude - origin_lon) * EARTH_RADIUS_METERS * math.cos(math.radians(origin_lat))
    y_north = math.radians(latitude - origin_lat) * EARTH_RADIUS_METERS
    return [round(x_east, 3), round(y_north, 3)]


def geometry_points(element: dict[str, Any], origin_lat: float, origin_lon: float) -> list[list[float]]:
    result: list[list[float]] = []
    for point in element.get("geometry", []):
        if point is None or point.get("lat") is None or point.get("lon") is None:
            continue
        result.append(local_xy(float(point["lat"]), float(point["lon"]), origin_lat, origin_lon))
    if len(result) > 1 and result[0] == result[-1]:
        result.pop()
    return result


def relation_outer_rings(
    element: dict[str, Any], origin_lat: float, origin_lon: float
) -> list[list[list[float]]]:
    """Join the outer way members of an OSM multipolygon relation.

    Overpass may return one closed outer way or several open way fragments.
    Inner rings are intentionally omitted because the lightweight Gazebo mesh
    does not model holes yet; retaining the outer footprint is safer than
    dropping the entire building.
    """
    rings: list[list[list[float]]] = []
    fragments: list[list[list[float]]] = []
    for member in element.get("members", []):
        if member.get("type") != "way" or member.get("role", "") not in {"", "outer"}:
            continue
        raw_geometry = member.get("geometry", [])
        points = geometry_points(member, origin_lat, origin_lon)
        if len(points) < 3:
            continue
        is_closed = (
            len(raw_geometry) > 2
            and raw_geometry[0].get("lat") == raw_geometry[-1].get("lat")
            and raw_geometry[0].get("lon") == raw_geometry[-1].get("lon")
        )
        if is_closed:
            rings.append(points)
        else:
            fragments.append(points)

    while fragments:
        chain = fragments.pop(0)
        joined = True
        while joined and chain[0] != chain[-1]:
            joined = False
            for index, fragment in enumerate(fragments):
                if chain[-1] == fragment[0]:
                    chain.extend(fragment[1:])
                elif chain[-1] == fragment[-1]:
                    chain.extend(reversed(fragment[:-1]))
                elif chain[0] == fragment[-1]:
                    chain = fragment[:-1] + chain
                elif chain[0] == fragment[0]:
                    chain = list(reversed(fragment[1:])) + chain
                else:
                    continue
                fragments.pop(index)
                joined = True
                break
        if len(chain) >= 4 and chain[0] == chain[-1]:
            chain.pop()
            rings.append(chain)
    return rings


def polygon_area(points: list[list[float]]) -> float:
    if len(points) < 3:
        return 0.0
    return abs(sum(
        points[index][0] * points[(index + 1) % len(points)][1]
        - points[(index + 1) % len(points)][0] * points[index][1]
        for index in range(len(points))
    )) / 2.0


def point_in_polygon(point: list[float], polygon: list[list[float]]) -> bool:
    x, y = point
    inside = False
    for index, first in enumerate(polygon):
        second = polygon[(index + 1) % len(polygon)]
        x1, y1 = first
        x2, y2 = second
        if (y1 > y) != (y2 > y):
            crossing_x = (x2 - x1) * (y - y1) / (y2 - y1) + x1
            if x < crossing_x:
                inside = not inside
    return inside


def segment_intersection_parameter(
    start: list[float], end: list[float], edge_start: list[float], edge_end: list[float]
) -> float | None:
    dx = end[0] - start[0]
    dy = end[1] - start[1]
    ex = edge_end[0] - edge_start[0]
    ey = edge_end[1] - edge_start[1]
    denominator = dx * ey - dy * ex
    if abs(denominator) < 1e-9:
        return None
    offset_x = edge_start[0] - start[0]
    offset_y = edge_start[1] - start[1]
    t = (offset_x * ey - offset_y * ex) / denominator
    u = (offset_x * dy - offset_y * dx) / denominator
    if -1e-9 <= t <= 1.0 + 1e-9 and -1e-9 <= u <= 1.0 + 1e-9:
        return min(1.0, max(0.0, t))
    return None


def interpolate(start: list[float], end: list[float], t: float) -> list[float]:
    return [
        round(start[0] + (end[0] - start[0]) * t, 3),
        round(start[1] + (end[1] - start[1]) * t, 3),
    ]


def clip_polyline_to_polygon(line: list[list[float]], polygon: list[list[float]]) -> list[list[list[float]]]:
    """Return the portions of a polyline inside a simple polygon."""
    chains: list[list[list[float]]] = []
    current: list[list[float]] = []
    for index in range(len(line) - 1):
        start, end = line[index], line[index + 1]
        parameters = [0.0, 1.0]
        for edge_index, edge_start in enumerate(polygon):
            edge_end = polygon[(edge_index + 1) % len(polygon)]
            parameter = segment_intersection_parameter(start, end, edge_start, edge_end)
            if parameter is not None:
                parameters.append(parameter)
        parameters = sorted(set(round(value, 10) for value in parameters))

        for interval_index in range(len(parameters) - 1):
            first_t, second_t = parameters[interval_index], parameters[interval_index + 1]
            if second_t - first_t < 1e-9:
                continue
            midpoint = interpolate(start, end, (first_t + second_t) / 2.0)
            if not point_in_polygon(midpoint, polygon):
                if len(current) >= 2:
                    chains.append(current)
                current = []
                continue
            first_point = interpolate(start, end, first_t)
            second_point = interpolate(start, end, second_t)
            if not current:
                current = [first_point, second_point]
            elif current[-1] == first_point:
                if current[-1] != second_point:
                    current.append(second_point)
            else:
                if len(current) >= 2:
                    chains.append(current)
                current = [first_point, second_point]
    if len(current) >= 2:
        chains.append(current)
    return chains


def numeric_tag(tags: dict[str, Any], key: str) -> float | None:
    raw = tags.get(key)
    if raw is None:
        return None
    text = str(raw).strip().lower().replace("meters", "").replace("meter", "").replace("m", "").strip()
    try:
        return float(text)
    except ValueError:
        return None


def building_height(tags: dict[str, Any], area: float) -> tuple[float, str]:
    explicit = numeric_tag(tags, "height")
    if explicit and explicit > 0:
        return round(explicit, 2), "osm-height"

    levels = numeric_tag(tags, "building:levels")
    if levels and levels > 0:
        return round(levels * DEFAULT_LEVEL_HEIGHT_METERS, 2), "osm-levels"

    # Stable area-based defaults: the same OSM geometry always gets the same height.
    if area >= 4_000:
        return 15.0, "area-default"
    if area >= 1_500:
        return 12.0, "area-default"
    return 9.0, "area-default"


def feature_id(element: dict[str, Any]) -> str:
    return f"osm-{element['type']}-{element['id']}"


def clean_tags(tags: dict[str, Any], keys: Iterable[str]) -> dict[str, Any]:
    return {key: tags[key] for key in keys if key in tags}


def bounds_from_points(point_sets: Iterable[list[list[float]]]) -> dict[str, float]:
    points = [point for point_set in point_sets for point in point_set]
    if not points:
        return {"minX": 0.0, "minY": 0.0, "maxX": 0.0, "maxY": 0.0}
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    return {
        "minX": round(min(xs), 3),
        "minY": round(min(ys), 3),
        "maxX": round(max(xs), 3),
        "maxY": round(max(ys), 3),
    }


def convert(source: Path, boundary_way_id: int | None, geographic_bounds: list[float] | None,
            world_id: str, name: str, scene_type: str) -> dict[str, Any]:
    document = json.loads(source.read_text(encoding="utf-8"))
    elements: list[dict[str, Any]] = document.get("elements", [])
    boundary = None
    if boundary_way_id is not None:
        boundary = next((
            element for element in elements
            if element.get("type") == "way" and int(element.get("id", -1)) == boundary_way_id
        ), None)
        if boundary is None:
            raise ValueError(f"Boundary way {boundary_way_id} was not found in the OSM extract")
        boundary_geo = boundary.get("geometry", [])
    else:
        south, west, north, east = geographic_bounds or []
        if north <= south or east <= west:
            raise ValueError("Invalid geographic bounds")
        boundary_geo = [
            {"lat": south, "lon": west}, {"lat": south, "lon": east},
            {"lat": north, "lon": east}, {"lat": north, "lon": west},
        ]
    if len(boundary_geo) < 3:
        raise ValueError("Scene boundary has no usable geometry")
    # Bounding-box centre is stable and keeps local coordinates balanced around zero.
    origin_lat = (min(p["lat"] for p in boundary_geo) + max(p["lat"] for p in boundary_geo)) / 2.0
    origin_lon = (min(p["lon"] for p in boundary_geo) + max(p["lon"] for p in boundary_geo)) / 2.0
    scene_boundary = [
        local_xy(float(point["lat"]), float(point["lon"]), origin_lat, origin_lon)
        for point in boundary_geo
    ]

    buildings: list[dict[str, Any]] = []
    roads: list[dict[str, Any]] = []
    areas: list[dict[str, Any]] = []
    points_of_interest: list[dict[str, Any]] = []
    skipped_building_relations: list[str] = []

    for element in elements:
        tags = element.get("tags") or {}
        if boundary is not None and element is boundary:
            continue
        points = geometry_points(element, origin_lat, origin_lon)

        if tags.get("building"):
            if element.get("type") == "way" and len(points) >= 3:
                outlines = [points]
            elif element.get("type") == "relation":
                outlines = relation_outer_rings(element, origin_lat, origin_lon)
            else:
                outlines = []
            if not outlines:
                skipped_building_relations.append(feature_id(element))
                continue
            for outline_index, outline in enumerate(outlines, start=1):
                center = [sum(point[0] for point in outline) / len(outline),
                          sum(point[1] for point in outline) / len(outline)]
                if not point_in_polygon(center, scene_boundary):
                    continue
                area = polygon_area(outline)
                height, source_name = building_height(tags, area)
                outline_id = feature_id(element)
                if len(outlines) > 1:
                    outline_id += f"-outer-{outline_index}"
                buildings.append({
                    "id": outline_id,
                    "sourceId": feature_id(element),
                    "name": tags.get("name") or f"建筑 {element['id']}",
                    "outline": outline,
                    "heightMeters": height,
                    "heightSource": source_name,
                    "levels": numeric_tag(tags, "building:levels"),
                    "footprintAreaSquareMeters": round(area, 2),
                    "tags": clean_tags(tags, ("building", "amenity", "name:en")),
                })
            continue

        if tags.get("highway") and len(points) >= 2:
            highway_type = tags["highway"]
            width = numeric_tag(tags, "width")
            if not width:
                width = 5.5 if highway_type in {"service", "residential", "unclassified"} else 2.0
            clipped_lines = clip_polyline_to_polygon(points, scene_boundary)
            for part_index, clipped_line in enumerate(clipped_lines, start=1):
                roads.append({
                    "id": f"{feature_id(element)}-part-{part_index}",
                    "sourceId": feature_id(element),
                    "name": tags.get("name") or "",
                    "kind": highway_type,
                    "centerline": clipped_line,
                    "widthMeters": round(width, 2),
                })
            continue

        area_kind = None
        if tags.get("natural") == "water" or tags.get("water"):
            area_kind = "water"
        elif tags.get("leisure") in {"park", "garden", "pitch", "track"}:
            area_kind = str(tags["leisure"])
        elif tags.get("landuse") in {"grass", "recreation_ground", "forest"}:
            area_kind = str(tags["landuse"])
        if area_kind and len(points) >= 3:
            center = [sum(point[0] for point in points) / len(points),
                      sum(point[1] for point in points) / len(points)]
            if not point_in_polygon(center, scene_boundary):
                continue
            areas.append({
                "id": feature_id(element),
                "name": tags.get("name") or "",
                "kind": area_kind,
                "outline": points,
            })
            continue

        if element.get("type") == "node" and element.get("lat") is not None and element.get("lon") is not None:
            if tags.get("amenity") or tags.get("man_made") or tags.get("name"):
                position = local_xy(float(element["lat"]), float(element["lon"]), origin_lat, origin_lon)
                if not point_in_polygon(position, scene_boundary):
                    continue
                points_of_interest.append({
                    "id": feature_id(element),
                    "name": tags.get("name") or tags.get("amenity") or tags.get("man_made"),
                    "position": position,
                    "kind": tags.get("amenity") or tags.get("man_made") or "place",
                })

    buildings.sort(key=lambda item: item["id"])
    roads.sort(key=lambda item: item["id"])
    areas.sort(key=lambda item: item["id"])
    points_of_interest.sort(key=lambda item: item["id"])

    bounds = bounds_from_points([scene_boundary])
    return {
        "schemaVersion": 1,
        "id": world_id,
        "name": name,
        "sceneType": scene_type,
        "coordinateSystem": {
            "type": "local-enu",
            "axes": {"x": "east", "y": "north", "z": "up"},
            "units": "meters",
            "origin": {
                "latitude": round(origin_lat, 8),
                "longitude": round(origin_lon, 8),
                "altitudeMeters": 0.0,
            },
        },
        "bounds": bounds,
        "sizeMeters": [round(bounds["maxX"] - bounds["minX"], 2), round(bounds["maxY"] - bounds["minY"], 2)],
        "boundary": scene_boundary,
        "buildings": buildings,
        "roads": roads,
        "areas": areas,
        "pointsOfInterest": points_of_interest,
        "landingPads": [],
        "fixedNoFlyZones": [],
        "source": {
            "provider": "OpenStreetMap",
            "license": "ODbL-1.0",
            "attribution": "© OpenStreetMap contributors",
            "url": "https://www.openstreetmap.org/copyright",
            "boundaryElement": (f"way/{boundary_way_id}"
                                if boundary_way_id is not None else "bounding-box"),
            "rawFile": "source/osm.json",
            "skippedBuildingRelations": skipped_building_relations,
        },
        "statistics": {
            "buildingCount": len(buildings),
            "roadCount": len(roads),
            "areaCount": len(areas),
            "pointOfInterestCount": len(points_of_interest),
        },
    }


def svg_points(points: list[list[float]], bounds: dict[str, float], width: float, height: float, padding: float) -> str:
    extent_x = max(bounds["maxX"] - bounds["minX"], 1.0)
    extent_y = max(bounds["maxY"] - bounds["minY"], 1.0)
    scale = min((width - 2 * padding) / extent_x, (height - 2 * padding) / extent_y)
    offset_x = (width - extent_x * scale) / 2.0
    offset_y = (height - extent_y * scale) / 2.0
    result = []
    for x, y in points:
        screen_x = offset_x + (x - bounds["minX"]) * scale
        screen_y = height - (offset_y + (y - bounds["minY"]) * scale)
        result.append(f"{screen_x:.2f},{screen_y:.2f}")
    return " ".join(result)


def write_preview(scene: dict[str, Any], destination: Path) -> None:
    width, height, padding = 1000.0, 720.0, 36.0
    bounds = scene["bounds"]
    layers: list[str] = [f'<rect width="{width:.0f}" height="{height:.0f}" fill="#f4f6f7"/>']

    for area in scene["areas"]:
        color = "#c9dfd0" if area["kind"] != "water" else "#b9d8e8"
        points = svg_points(area["outline"], bounds, width, height, padding)
        layers.append(f'<polygon points="{points}" fill="{color}" stroke="none"/>')
    for road in scene["roads"]:
        points = svg_points(road["centerline"], bounds, width, height, padding)
        stroke_width = max(1.0, min(6.0, road["widthMeters"] * 0.7))
        layers.append(f'<polyline points="{points}" fill="none" stroke="#d1d6da" stroke-width="{stroke_width:.2f}" stroke-linecap="round" stroke-linejoin="round"/>')
    for building in scene["buildings"]:
        points = svg_points(building["outline"], bounds, width, height, padding)
        layers.append(f'<polygon points="{points}" fill="#ffffff" stroke="#77838d" stroke-width="1.1"/>')

    boundary = svg_points(scene["boundary"], bounds, width, height, padding)
    layers.append(f'<polygon points="{boundary}" fill="none" stroke="#286fbe" stroke-width="2"/>')
    layers.append(f'<text x="36" y="32" font-family="sans-serif" font-size="18" fill="#26323b">{html.escape(scene["name"])} · OSM白模预览</text>')
    layers.append(f'<text x="964" y="696" text-anchor="end" font-family="Segoe UI, sans-serif" font-size="12" fill="#65727c">{html.escape(scene["source"]["attribution"])}</text>')
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width:.0f} {height:.0f}">' + "".join(layers) + "</svg>\n"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(svg, encoding="utf-8")


def main() -> None:
    args = parse_args()
    scene = convert(args.source, args.boundary_way, args.bounds,
                    args.world_id, args.name, args.scene_type)
    args.scene.parent.mkdir(parents=True, exist_ok=True)
    args.scene.write_text(json.dumps(scene, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.preview:
        write_preview(scene, args.preview)
    print(json.dumps(scene["statistics"], ensure_ascii=False))
    print(f"sizeMeters={scene['sizeMeters']}")


if __name__ == "__main__":
    main()
