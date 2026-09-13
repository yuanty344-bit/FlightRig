#!/usr/bin/env python3
"""Generate FlightRig Gazebo/runtime-Qt assets from a normalized scene JSON."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable
from xml.sax.saxutils import escape


Point2 = tuple[float, float]
Point3 = tuple[float, float, float]

MATERIAL_LIBRARY_NAME = "flightbench_city.mtl"
MATERIAL_LIBRARY = """# FlightRig restrained city palette; generated file
newmtl buildings
Ka 0.70 0.73 0.76
Kd 0.84 0.86 0.88
Ks 0.05 0.05 0.05
Ns 12

newmtl roads
Ka 0.28 0.31 0.34
Kd 0.40 0.43 0.46
Ks 0.02 0.02 0.02
Ns 5

newmtl green_areas
Ka 0.22 0.37 0.25
Kd 0.34 0.52 0.37
Ks 0.01 0.01 0.01
Ns 4

newmtl water
Ka 0.16 0.35 0.49
Kd 0.28 0.56 0.71
Ks 0.18 0.18 0.18
Ns 28
"""


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("scene", type=Path)
    parser.add_argument("--project-root", type=Path, required=True)
    return parser.parse_args()


def clean_polygon(raw: Iterable[Iterable[float]]) -> list[Point2]:
    points: list[Point2] = []
    for value in raw:
        point = (float(value[0]), float(value[1]))
        if not points or point != points[-1]:
            points.append(point)
    if len(points) > 1 and points[0] == points[-1]:
        points.pop()
    changed = True
    while changed and len(points) >= 3:
        changed = False
        result: list[Point2] = []
        for index, current in enumerate(points):
            previous = points[index - 1]
            following = points[(index + 1) % len(points)]
            cross = ((current[0] - previous[0]) * (following[1] - current[1])
                     - (current[1] - previous[1]) * (following[0] - current[0]))
            if abs(cross) < 1e-7:
                changed = True
                continue
            result.append(current)
        points = result
    return points


def signed_area(points: list[Point2]) -> float:
    return sum(
        first[0] * second[1] - second[0] * first[1]
        for first, second in zip(points, points[1:] + points[:1])
    ) / 2.0


def cross(first: Point2, second: Point2, third: Point2) -> float:
    return ((second[0] - first[0]) * (third[1] - first[1])
            - (second[1] - first[1]) * (third[0] - first[0]))


def point_in_triangle(point: Point2, a: Point2, b: Point2, c: Point2) -> bool:
    ab = cross(a, b, point)
    bc = cross(b, c, point)
    ca = cross(c, a, point)
    return ab >= -1e-8 and bc >= -1e-8 and ca >= -1e-8


def triangulate(raw: Iterable[Iterable[float]]) -> tuple[list[Point2], list[tuple[int, int, int]]]:
    points = clean_polygon(raw)
    if len(points) < 3:
        return points, []
    if signed_area(points) < 0:
        points.reverse()

    remaining = list(range(len(points)))
    triangles: list[tuple[int, int, int]] = []
    guard = len(points) * len(points)
    while len(remaining) > 3 and guard > 0:
        guard -= 1
        ear_found = False
        for offset, current_index in enumerate(remaining):
            previous_index = remaining[offset - 1]
            next_index = remaining[(offset + 1) % len(remaining)]
            a, b, c = points[previous_index], points[current_index], points[next_index]
            if cross(a, b, c) <= 1e-8:
                continue
            if any(
                point_in_triangle(points[other], a, b, c)
                for other in remaining
                if other not in {previous_index, current_index, next_index}
            ):
                continue
            triangles.append((previous_index, current_index, next_index))
            remaining.pop(offset)
            ear_found = True
            break
        if not ear_found:
            raise ValueError("Unable to triangulate polygon; it may be self-intersecting")
    if len(remaining) == 3:
        triangles.append(tuple(remaining))
    return points, triangles


@dataclass
class ObjMesh:
    name: str
    transform: Callable[[Point3], Point3]
    vertices: list[Point3] = field(default_factory=list)
    faces: list[tuple[int, int, int]] = field(default_factory=list)

    def vertex(self, point: Point3) -> int:
        self.vertices.append(self.transform(point))
        return len(self.vertices)

    def triangle(self, first: int, second: int, third: int) -> None:
        self.faces.append((first, second, third))

    def add_flat_polygon(self, outline: Iterable[Iterable[float]], z: float) -> None:
        points, triangles = triangulate(outline)
        indices = [self.vertex((x, y, z)) for x, y in points]
        for first, second, third in triangles:
            self.triangle(indices[first], indices[second], indices[third])

    def add_extruded_polygon(self, outline: Iterable[Iterable[float]], height: float) -> None:
        points, triangles = triangulate(outline)
        if not triangles:
            return
        bottom = [self.vertex((x, y, 0.0)) for x, y in points]
        top = [self.vertex((x, y, height)) for x, y in points]
        for first, second, third in triangles:
            self.triangle(top[first], top[second], top[third])
            self.triangle(bottom[third], bottom[second], bottom[first])
        for index in range(len(points)):
            following = (index + 1) % len(points)
            self.triangle(bottom[index], bottom[following], top[following])
            self.triangle(bottom[index], top[following], top[index])

    def add_road_segment(self, start: Iterable[float], end: Iterable[float], width: float, z: float) -> None:
        x1, y1 = float(start[0]), float(start[1])
        x2, y2 = float(end[0]), float(end[1])
        dx, dy = x2 - x1, y2 - y1
        length = math.hypot(dx, dy)
        if length < 1e-6:
            return
        offset_x = -dy / length * width / 2.0
        offset_y = dx / length * width / 2.0
        corners = [
            (x1 + offset_x, y1 + offset_y, z),
            (x1 - offset_x, y1 - offset_y, z),
            (x2 - offset_x, y2 - offset_y, z),
            (x2 + offset_x, y2 + offset_y, z),
        ]
        indices = [self.vertex(point) for point in corners]
        self.triangle(indices[0], indices[1], indices[2])
        self.triangle(indices[0], indices[2], indices[3])

    def write(self, destination: Path, material_name: str) -> None:
        destination.parent.mkdir(parents=True, exist_ok=True)
        accumulated = [[0.0, 0.0, 0.0] for _ in self.vertices]
        for first, second, third in self.faces:
            a, b, c = self.vertices[first - 1], self.vertices[second - 1], self.vertices[third - 1]
            ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            normal = (
                ab[1] * ac[2] - ab[2] * ac[1],
                ab[2] * ac[0] - ab[0] * ac[2],
                ab[0] * ac[1] - ab[1] * ac[0],
            )
            for index in (first - 1, second - 1, third - 1):
                accumulated[index][0] += normal[0]
                accumulated[index][1] += normal[1]
                accumulated[index][2] += normal[2]

        normals: list[Point3] = []
        for normal in accumulated:
            length = math.sqrt(sum(value * value for value in normal))
            normals.append(
                (normal[0] / length, normal[1] / length, normal[2] / length)
                if length > 1e-12 else (0.0, 0.0, 1.0)
            )
        lines = [
            "# Generated by FlightRig; do not edit",
            f"mtllib {MATERIAL_LIBRARY_NAME}",
            f"o {self.name}",
            f"usemtl {material_name}",
        ]
        lines.extend(f"v {x:.4f} {y:.4f} {z:.4f}" for x, y, z in self.vertices)
        lines.extend(f"vn {x:.7f} {y:.7f} {z:.7f}" for x, y, z in normals)
        lines.extend(f"f {a}//{a} {b}//{b} {c}//{c}" for a, b, c in self.faces)
        destination.write_text("\n".join(lines) + "\n", encoding="ascii")

    def summary(self) -> dict[str, int]:
        return {"vertices": len(self.vertices), "triangles": len(self.faces)}


def identity(point: Point3) -> Point3:
    return point


def make_meshes(scene: dict, transform: Callable[[Point3], Point3]) -> dict[str, ObjMesh]:
    mesh_prefix = str(scene["id"]).replace("-", "_")
    meshes = {
        "buildings": ObjMesh(f"{mesh_prefix}_buildings", transform),
        "roads": ObjMesh(f"{mesh_prefix}_roads", transform),
        "green_areas": ObjMesh(f"{mesh_prefix}_green_areas", transform),
        "water": ObjMesh(f"{mesh_prefix}_water", transform),
    }
    for building in scene["buildings"]:
        meshes["buildings"].add_extruded_polygon(building["outline"], float(building["heightMeters"]))
    for road in scene["roads"]:
        line = road["centerline"]
        for start, end in zip(line, line[1:]):
            meshes["roads"].add_road_segment(start, end, float(road["widthMeters"]), 0.04)
    for area in scene["areas"]:
        target = "water" if area["kind"] == "water" else "green_areas"
        meshes[target].add_flat_polygon(area["outline"], 0.02 if target == "water" else 0.015)
    return meshes


def gazebo_model_sdf(scene: dict) -> str:
    world_id = escape(str(scene["id"]))
    return f"""<?xml version="1.0"?>
<sdf version="1.10">
  <model name="{world_id}_city">
    <static>true</static>
    <link name="city">
      <visual name="buildings_visual">
        <geometry><mesh><uri>model://{world_id}/meshes/buildings.obj</uri></mesh></geometry>
        <material><ambient>0.78 0.81 0.83 1</ambient><diffuse>0.88 0.90 0.91 1</diffuse><specular>0.08 0.08 0.08 1</specular></material>
      </visual>
      <collision name="buildings_collision">
        <geometry><mesh><uri>model://{world_id}/meshes/buildings.obj</uri></mesh></geometry>
      </collision>
      <visual name="roads_visual">
        <geometry><mesh><uri>model://{world_id}/meshes/roads.obj</uri></mesh></geometry>
        <material><ambient>0.42 0.45 0.47 1</ambient><diffuse>0.50 0.53 0.55 1</diffuse></material>
      </visual>
      <visual name="green_areas_visual">
        <geometry><mesh><uri>model://{world_id}/meshes/green_areas.obj</uri></mesh></geometry>
        <material><ambient>0.34 0.48 0.36 1</ambient><diffuse>0.42 0.58 0.44 1</diffuse></material>
      </visual>
      <visual name="water_visual">
        <geometry><mesh><uri>model://{world_id}/meshes/water.obj</uri></mesh></geometry>
        <material><ambient>0.25 0.48 0.63 1</ambient><diffuse>0.34 0.61 0.76 1</diffuse></material>
      </visual>
    </link>
  </model>
</sdf>
"""


def gazebo_world_sdf(scene: dict) -> str:
    origin = scene["coordinateSystem"]["origin"]
    world_id = escape(str(scene["id"]))
    bounds = scene["bounds"]
    ground_width = max(200.0, float(bounds["maxX"]) - float(bounds["minX"]) + 100.0)
    ground_height = max(200.0, float(bounds["maxY"]) - float(bounds["minY"]) + 100.0)
    return f"""<?xml version="1.0"?>
<sdf version="1.10">
  <world name="{world_id}">
    <physics name="px4_250hz" type="ode"><max_step_size>0.004</max_step_size><real_time_factor>1.0</real_time_factor><real_time_update_rate>250</real_time_update_rate></physics>
    <gravity>0 0 -9.8</gravity>
    <magnetic_field>6e-06 2.3e-05 -4.2e-05</magnetic_field>
    <atmosphere type="adiabatic"/>
    <plugin filename="gz-sim-physics-system" name="gz::sim::systems::Physics"/>
    <plugin filename="gz-sim-user-commands-system" name="gz::sim::systems::UserCommands"/>
    <plugin filename="gz-sim-scene-broadcaster-system" name="gz::sim::systems::SceneBroadcaster"/>
    <plugin filename="gz-sim-contact-system" name="gz::sim::systems::Contact"/>
    <plugin filename="gz-sim-imu-system" name="gz::sim::systems::Imu"/>
    <plugin filename="gz-sim-air-pressure-system" name="gz::sim::systems::AirPressure"/>
    <plugin filename="gz-sim-air-speed-system" name="gz::sim::systems::AirSpeed"/>
    <plugin filename="gz-sim-apply-link-wrench-system" name="gz::sim::systems::ApplyLinkWrench"/>
    <plugin filename="gz-sim-navsat-system" name="gz::sim::systems::NavSat"/>
    <plugin filename="gz-sim-magnetometer-system" name="gz::sim::systems::Magnetometer"/>
    <plugin filename="gz-sim-sensors-system" name="gz::sim::systems::Sensors"><render_engine>ogre2</render_engine></plugin>
    <scene><ambient>0.72 0.74 0.77</ambient><background>0.82 0.86 0.90</background><shadows>true</shadows></scene>
    <spherical_coordinates>
      <surface_model>EARTH_WGS84</surface_model><world_frame_orientation>ENU</world_frame_orientation>
      <latitude_deg>{origin['latitude']}</latitude_deg><longitude_deg>{origin['longitude']}</longitude_deg>
      <elevation>{origin['altitudeMeters']}</elevation><heading_deg>0</heading_deg>
    </spherical_coordinates>
    <light type="directional" name="sun"><cast_shadows>true</cast_shadows><pose>0 0 500 0 0 0</pose><diffuse>0.9 0.9 0.88 1</diffuse><specular>0.2 0.2 0.2 1</specular><direction>-0.4 0.2 -0.9</direction></light>
    <model name="ground"><static>true</static><link name="ground_link"><collision name="collision"><geometry><box><size>{ground_width:.1f} {ground_height:.1f} 0.2</size></box></geometry><pose>0 0 -0.1 0 0 0</pose></collision><visual name="visual"><geometry><box><size>{ground_width:.1f} {ground_height:.1f} 0.2</size></box></geometry><pose>0 0 -0.1 0 0 0</pose><material><ambient>0.56 0.59 0.55 1</ambient><diffuse>0.63 0.66 0.61 1</diffuse></material></visual></link></model>
    <include><uri>model://{world_id}</uri></include>
  </world>
</sdf>
"""


def model_config(scene: dict) -> str:
    scene_type = escape(str(scene.get("sceneType", "urban")))
    return f"""<?xml version="1.0"?>
<model><name>{escape(scene['name'])}</name><version>1.0</version><sdf version="1.10">model.sdf</sdf><author><name>FlightRig</name></author><description>OSM-derived {scene_type} white model. © OpenStreetMap contributors.</description></model>
"""


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    args = arguments()
    args.scene = args.scene.resolve()
    project_root = args.project_root.resolve()
    scene = json.loads(args.scene.read_text(encoding="utf-8"))
    world_id = str(scene["id"])

    gazebo_root = project_root / "simulation"
    gazebo_model = gazebo_root / "models" / world_id
    gazebo_mesh_dir = gazebo_model / "meshes"
    gazebo_meshes = make_meshes(scene, identity)
    for name, mesh in gazebo_meshes.items():
        mesh.write(gazebo_mesh_dir / f"{name}.obj", name)
    material_path = gazebo_mesh_dir / MATERIAL_LIBRARY_NAME
    material_path.write_text(MATERIAL_LIBRARY, encoding="ascii")
    (gazebo_model / "model.sdf").write_text(gazebo_model_sdf(scene), encoding="utf-8")
    (gazebo_model / "model.config").write_text(model_config(scene), encoding="utf-8")
    attribution_path = gazebo_model / "ATTRIBUTION.md"
    attribution_path.write_text(
        f"# {scene['name']}\n\n{scene['source']['attribution']} — "
        f"{scene['source']['url']}\n",
        encoding="utf-8",
    )
    world_path = gazebo_root / "worlds" / f"{world_id}.sdf"
    world_path.parent.mkdir(parents=True, exist_ok=True)
    world_path.write_text(gazebo_world_sdf(scene), encoding="utf-8")

    manifest = {
        "schemaVersion": 1,
        "sceneId": scene["id"],
        "sourceScene": args.scene.relative_to(project_root).as_posix(),
        "coordinateTransforms": {
            "gazebo": "ENU: (east, north, up)",
            "qtQuick3D": "RuntimeLoader rotates ENU to (east, up, south)",
        },
        "gazeboMeshes": {name: mesh.summary() for name, mesh in gazebo_meshes.items()},
        "generatedFiles": {},
        "attribution": scene["source"]["attribution"],
    }
    generated = [
        world_path,
        gazebo_model / "model.sdf",
        gazebo_model / "model.config",
        attribution_path,
        material_path,
        *(gazebo_mesh_dir / f"{name}.obj" for name in gazebo_meshes),
    ]
    for path in generated:
        manifest["generatedFiles"][path.relative_to(project_root).as_posix()] = file_hash(path)
    manifest_path = args.scene.parent / "scene-assets.manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"gazebo": manifest["gazeboMeshes"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
