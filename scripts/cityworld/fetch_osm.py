#!/usr/bin/env python3
"""Fetch a bounded OpenStreetMap extract without requiring Overpass Turbo."""

from __future__ import annotations

import argparse
from http.client import IncompleteRead
import json
import math
from pathlib import Path
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


USER_AGENT = "FlightRig-city-world-generator/0.13 (local development tool)"


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--place", help="Address or place name")
    parser.add_argument("--latitude", type=float, help="Known WGS84 center latitude")
    parser.add_argument("--longitude", type=float, help="Known WGS84 center longitude")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--radius", type=float, default=700.0,
                        help="Half-size of the square extract in metres")
    parser.add_argument("--nominatim", default="https://nominatim.openstreetmap.org/search")
    parser.add_argument("--overpass", default="https://overpass-api.de/api/interpreter")
    return parser.parse_args()


def request_json(url: str, data: bytes | None = None, attempts: int = 3) -> object:
    last_error: Exception | None = None
    for attempt in range(1, attempts + 1):
        request = Request(url, data=data, headers={
            "User-Agent": USER_AGENT,
            "Accept": "application/json",
            "Content-Type": "application/x-www-form-urlencoded; charset=utf-8",
        })
        try:
            with urlopen(request, timeout=240) as response:
                return json.loads(response.read().decode("utf-8"))
        except HTTPError as error:
            if error.code != 429 and error.code < 500:
                raise
            last_error = error
        except (IncompleteRead, TimeoutError, URLError, json.JSONDecodeError) as error:
            last_error = error
        if attempt < attempts:
            delay = 5 * attempt
            print(f"Request failed; retrying in {delay} seconds ({attempt}/{attempts})")
            time.sleep(delay)
    raise RuntimeError(f"OpenStreetMap request failed after {attempts} attempts: {last_error}")


def geocode(place: str, endpoint: str) -> tuple[float, float, str]:
    query = urlencode({"q": place, "format": "jsonv2", "limit": 1})
    result = request_json(f"{endpoint}?{query}")
    if not isinstance(result, list) or not result:
        raise RuntimeError(f"OpenStreetMap could not locate: {place}")
    item = result[0]
    return float(item["lat"]), float(item["lon"]), str(item.get("display_name", place))


def bounds_around(latitude: float, longitude: float, radius: float) -> list[float]:
    if radius < 100.0 or radius > 2000.0:
        raise ValueError("Radius must be between 100 and 2000 metres")
    latitude_delta = radius / 111_320.0
    longitude_delta = radius / (111_320.0 * max(0.1, math.cos(math.radians(latitude))))
    return [latitude - latitude_delta, longitude - longitude_delta,
            latitude + latitude_delta, longitude + longitude_delta]


def overpass_query(bounds: list[float]) -> str:
    box = ",".join(f"{value:.8f}" for value in bounds)
    return f"""[out:json][timeout:180];
(
  wr[\"building\"]({box});
  way[\"highway\"]({box});
  way[\"leisure\"~\"^(park|garden|pitch|track)$\"]({box});
  way[\"natural\"=\"water\"]({box});
  way[\"water\"]({box});
  way[\"landuse\"~\"^(grass|recreation_ground|forest)$\"]({box});
  node[\"amenity\"]({box});
  node[\"man_made\"]({box});
);
out body geom;
"""


def main() -> None:
    args = arguments()
    if args.place:
        if args.latitude is not None or args.longitude is not None:
            raise ValueError("Use either --place or --latitude/--longitude, not both")
        latitude, longitude, resolved_name = geocode(args.place, args.nominatim)
        requested_place = args.place
    else:
        if args.latitude is None or args.longitude is None:
            raise ValueError("Provide --place or both --latitude and --longitude")
        if not -90.0 <= args.latitude <= 90.0 or not -180.0 <= args.longitude <= 180.0:
            raise ValueError("Latitude or longitude is outside the WGS84 range")
        latitude, longitude = args.latitude, args.longitude
        resolved_name = f"{latitude:.6f}, {longitude:.6f}"
        requested_place = resolved_name
    bounds = bounds_around(latitude, longitude, args.radius)
    query = overpass_query(bounds)
    document = request_json(args.overpass, urlencode({"data": query}).encode("utf-8"))
    if not isinstance(document, dict) or not isinstance(document.get("elements"), list):
        raise RuntimeError("Overpass returned an invalid document")
    if not document["elements"]:
        raise RuntimeError("OpenStreetMap returned no usable objects for this area")

    document["_flightbench"] = {
        "requestedPlace": requested_place,
        "resolvedName": resolved_name,
        "center": [latitude, longitude],
        "bounds": bounds,
        "radiusMeters": args.radius,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    (args.output.parent / "overpass.query").write_text(query, encoding="utf-8")
    print(json.dumps({"resolvedName": resolved_name, "bounds": bounds,
                      "elementCount": len(document["elements"])}, ensure_ascii=False))


if __name__ == "__main__":
    main()
