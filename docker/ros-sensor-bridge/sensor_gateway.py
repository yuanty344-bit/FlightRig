#!/usr/bin/env python3
"""Read-only ROS 2 sensor gateway for FlightBench."""

import base64
import io
import json
import math
import os
import queue
import socket
import threading
import time

import numpy as np
import rclpy
from rclpy.executors import ExternalShutdownException
from PIL import Image
from rclpy.node import Node
from sensor_msgs.msg import Image as RosImage
from sensor_msgs.msg import JointState
from sensor_msgs.msg import LaserScan
from std_msgs.msg import Float64
from tf2_msgs.msg import TFMessage

HOST = "0.0.0.0"
PORT = int(os.environ.get("SENSOR_GATEWAY_PORT", "8765"))
SENSOR_KIND = os.environ.get("SENSOR_KIND", "lidar-2d")
PERFORMANCE_MODE = os.environ.get("SENSOR_PERFORMANCE_MODE", "standard")
SCAN_TOPIC = os.environ.get("ROS_SCAN_TOPIC", "/flightbench/lidar/scan")
RGB_TOPIC = os.environ.get("ROS_RGB_TOPIC", "/flightbench/camera/rgb")
DEPTH_TOPIC = os.environ.get("ROS_DEPTH_TOPIC", "/flightbench/camera/depth")
MAX_POINTS = max(90, int(os.environ.get("SENSOR_GATEWAY_MAX_POINTS", "720")))
MIN_SCAN_INTERVAL = max(0.03, float(os.environ.get("SENSOR_GATEWAY_MIN_INTERVAL", "0.08")))
MIN_IMAGE_INTERVAL = max(0.05, float(os.environ.get("SENSOR_GATEWAY_IMAGE_INTERVAL", "0.10")))
MAX_IMAGE_WIDTH = max(320, int(os.environ.get("SENSOR_GATEWAY_IMAGE_WIDTH", "960")))
JPEG_QUALITY = max(40, min(92, int(os.environ.get("SENSOR_GATEWAY_JPEG_QUALITY", "78"))))
DEPTH_GRID_WIDTH = 160
DEPTH_VALID_MIN = 0.2
DEPTH_VALID_MAX = 100.0
DEPTH_DISPLAY_MAX = 30.0

class ClientHub:
    def __init__(self):
        self._lock = threading.Lock()
        self._clients = {}
        self._command_handler = None

    def set_command_handler(self, handler):
        self._command_handler = handler

    def add(self, client):
        client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        state = {
            "pending": {},
            "ready": threading.Event(),
            "closed": False,
        }
        with self._lock:
            self._clients[client] = state
        threading.Thread(target=self._send_latest, args=(client, state), daemon=True).start()
        threading.Thread(target=self._read_commands, args=(client,), daemon=True).start()

    def _remove(self, client):
        with self._lock:
            state = self._clients.pop(client, None)
            if state is not None:
                state["closed"] = True
                state["ready"].set()
        try:
            client.close()
        except OSError:
            pass

    def _send_latest(self, client, state):
        """Send without blocking a ROS callback and replace stale sensor frames.

        A TCP client that briefly falls behind must not force Gazebo/ROS to queue
        seconds of old images. Each channel keeps only its newest unsent value;
        FlightBench therefore catches up to the live view immediately.
        """
        try:
            while True:
                state["ready"].wait()
                with self._lock:
                    if state["closed"]:
                        return
                    payloads = list(state["pending"].values())
                    state["pending"].clear()
                    state["ready"].clear()
                for payload in payloads:
                    if isinstance(payload, dict):
                        # Stamp at dispatch, not while queued behind an image.
                        payload["serverSendMonoMs"] = time.monotonic_ns() // 1_000_000
                        payload = (json.dumps(payload, separators=(",", ":")) + "\n").encode()
                    client.sendall(payload)
        except OSError:
            pass
        finally:
            self._remove(client)

    def _read_commands(self, client):
        buffer = bytearray()
        try:
            while True:
                chunk = client.recv(4096)
                if not chunk:
                    break
                buffer.extend(chunk)
                if len(buffer) > 65536:
                    buffer.clear()
                    continue
                while b"\n" in buffer:
                    line, _, remainder = buffer.partition(b"\n")
                    buffer = bytearray(remainder)
                    try:
                        message = json.loads(line.decode("utf-8"))
                    except (UnicodeDecodeError, json.JSONDecodeError):
                        continue
                    if isinstance(message, dict) and message.get("type") == "clock-ping":
                        reply = {"type": "clock-pong", "id": message.get("id"),
                                 "serverReceiveMonoMs": time.monotonic_ns() // 1_000_000}
                        # Use the sole socket writer, never interleave JSON bytes.
                        with self._lock:
                            state = self._clients.get(client)
                            if state is not None:
                                state["pending"]["clock-pong"] = reply
                                state["ready"].set()
                        continue
                    handler = self._command_handler
                    if handler is not None and isinstance(message, dict):
                        handler(message)
        except OSError:
            pass
        finally:
            self._remove(client)

    def broadcast(self, message):
        message = dict(message)
        # Gateway-ready timestamp: includes subsequent serialization, queueing
        # and transport, but not camera exposure / ROS processing before here.
        message["sentAtMonotonicMs"] = time.monotonic_ns() // 1_000_000
        payload = (json.dumps(message, separators=(",", ":"), allow_nan=False) + "\n").encode()
        message_type = str(message.get("type", "message"))
        stream = str(message.get("stream", ""))
        channel = f"{message_type}:{stream}"
        with self._lock:
            for state in self._clients.values():
                state["pending"][channel] = payload
                state["ready"].set()


def ros_image_to_pil(message):
    width, height = int(message.width), int(message.height)
    encoding = message.encoding.lower()
    raw = bytes(message.data)
    if encoding in ("rgb8", "8uc3"):
        return Image.frombytes("RGB", (width, height), raw, "raw", "RGB", message.step)
    if encoding == "bgr8":
        return Image.frombytes("RGB", (width, height), raw, "raw", "BGR", message.step)
    if encoding in ("rgba8", "8uc4"):
        return Image.frombytes("RGBA", (width, height), raw, "raw", "RGBA", message.step).convert("RGB")
    if encoding == "bgra8":
        return Image.frombytes("RGBA", (width, height), raw, "raw", "BGRA", message.step).convert("RGB")
    if encoding in ("mono8", "8uc1"):
        return Image.frombytes("L", (width, height), raw, "raw", "L", message.step).convert("RGB")
    raise ValueError(f"unsupported image encoding: {message.encoding}")


def jpeg_payload(image, maximum_width=MAX_IMAGE_WIDTH, quality=JPEG_QUALITY):
    if image.width > maximum_width:
        height = max(1, round(image.height * maximum_width / image.width))
        image = image.resize((maximum_width, height), Image.Resampling.BILINEAR)
    output = io.BytesIO()
    image.save(output, format="JPEG", quality=quality)
    return base64.b64encode(output.getvalue()).decode("ascii"), image.width, image.height


def depth_values(message):
    encoding = message.encoding.lower()
    width, height = int(message.width), int(message.height)
    raw = bytes(message.data)
    if encoding in ("32fc1", "32fc"):
        dtype = np.dtype(">f4" if message.is_bigendian else "<f4")
        values = np.ndarray((height, width), dtype=dtype, buffer=raw,
                            strides=(int(message.step), 4)).astype(np.float32, copy=True)
    elif encoding in ("16uc1", "mono16"):
        dtype = np.dtype(">u2" if message.is_bigendian else "<u2")
        values = np.ndarray((height, width), dtype=dtype, buffer=raw,
                            strides=(int(message.step), 2)).astype(np.float32, copy=True)
        values /= 1000.0
    else:
        raise ValueError(f"unsupported depth encoding: {message.encoding}")
    return width, height, values


def depth_visual_and_grid(width, height, values):
    valid_mask = (np.isfinite(values)
                  & (values >= DEPTH_VALID_MIN)
                  & (values <= DEPTH_VALID_MAX))

    # Use the forward-looking safety region for the clearance metric. The
    # lower edge sees the ground during take-off/landing and the outer border
    # can include the airframe; both previously produced false 0.6 m hazards.
    left, right = round(width * 0.05), round(width * 0.95)
    top, bottom = round(height * 0.05), round(height * 0.75)
    safety_values = values[top:bottom, left:right]
    safety_mask = valid_mask[top:bottom, left:right]
    nearest = float(np.min(safety_values[safety_mask])) if np.any(safety_mask) else 0.0

    center = float(values[height // 2, width // 2]) if values.size else 0.0
    if (not math.isfinite(center) or center < DEPTH_VALID_MIN
            or center > DEPTH_VALID_MAX):
        center = 0.0
    valid_ratio = float(np.count_nonzero(valid_mask)) / max(1, values.size)

    normalized = np.clip((values - DEPTH_VALID_MIN)
                         / (DEPTH_DISPLAY_MAX - DEPTH_VALID_MIN), 0.0, 1.0)
    stop_positions = np.array((0.0, 0.25, 0.5, 0.75, 1.0), dtype=np.float32)
    stop_colours = np.array(((239, 68, 68), (245, 158, 11), (253, 230, 138),
                             (56, 189, 248), (37, 99, 235)), dtype=np.float32)
    visual = np.empty((height, width, 3), dtype=np.uint8)
    flattened = normalized.ravel()
    for channel in range(3):
        visual[:, :, channel] = np.interp(
            flattened, stop_positions, stop_colours[:, channel]
        ).reshape(height, width).astype(np.uint8)
    visual[~valid_mask] = (18, 27, 38)

    grid_width = min(DEPTH_GRID_WIDTH, width)
    grid_height = max(1, round(height * grid_width / width))
    sample_x = np.rint(np.linspace(0, width - 1, grid_width)).astype(np.intp)
    sample_y = np.rint(np.linspace(0, height - 1, grid_height)).astype(np.intp)
    sampled = values[np.ix_(sample_y, sample_x)]
    sampled_valid = (np.isfinite(sampled)
                     & (sampled >= DEPTH_VALID_MIN) & (sampled <= 65.535))
    millimetres = np.where(sampled_valid, np.rint(sampled * 1000.0), 0)
    grid = millimetres.astype("<u2", copy=False).tobytes()
    return (Image.fromarray(visual), nearest,
            center, valid_ratio, grid_width, grid_height,
            base64.b64encode(grid).decode("ascii"))


class SensorGateway(Node):
    def __init__(self, client_hub):
        super().__init__("flightbench_sensor_gateway")
        self._client_hub = client_hub
        self._last_sent = {}
        self._last_received = {}
        self._commands = queue.SimpleQueue()
        self._gimbal_targets = {"yaw": 0.0, "pitch": 0.0, "roll": 0.0}
        self._gimbal_actual = dict(self._gimbal_targets)
        self._gimbal_command_received = False
        self._last_gimbal_joint_state = 0.0
        self._performance_mode = "standard"
        self._scan_interval = MIN_SCAN_INTERVAL
        self._image_interval = MIN_IMAGE_INTERVAL
        self._max_points = MAX_POINTS
        self._image_width = MAX_IMAGE_WIDTH
        self._jpeg_quality = JPEG_QUALITY
        self._apply_performance_mode(PERFORMANCE_MODE)
        client_hub.set_command_handler(self.enqueue_command)
        if SENSOR_KIND == "lidar-2d":
            self.create_subscription(LaserScan, SCAN_TOPIC, self._on_scan, 10)
        elif SENSOR_KIND == "mono-camera":
            self.create_subscription(RosImage, RGB_TOPIC, self._on_rgb, 2)
        elif SENSOR_KIND == "depth-camera":
            self.create_subscription(RosImage, RGB_TOPIC, self._on_rgb, 2)
            self.create_subscription(RosImage, DEPTH_TOPIC, self._on_depth, 2)
        elif SENSOR_KIND == "gimbal-camera":
            self.create_subscription(RosImage, RGB_TOPIC, self._on_rgb, 2)
            self.create_subscription(JointState, "/flightbench/gimbal/joint_states",
                                     self._on_gimbal_joint_state, 5)
            self.create_subscription(TFMessage, "/flightbench/gimbal/poses",
                                     self._on_gimbal_poses, 5)
            self._gimbal_publishers = {
                "yaw": self.create_publisher(Float64, "/flightbench/gimbal/yaw/command", 5),
                "pitch": self.create_publisher(Float64, "/flightbench/gimbal/pitch/command", 5),
                "roll": self.create_publisher(Float64, "/flightbench/gimbal/roll/command", 5),
            }
            # 10 Hz is smooth for a camera gimbal and leaves PX4/Gazebo ample
            # CPU time. The previous 25 Hz commands plus 250 Hz pose updates
            # could starve SITL after take-off on typical desktop hardware.
        else:
            raise RuntimeError(f"unsupported sensor kind: {SENSOR_KIND}")
        self.create_timer(0.10, self._process_commands)
        self.get_logger().info(f"Serving {SENSOR_KIND} on TCP {HOST}:{PORT}")

    def enqueue_command(self, message):
        if message.get("type") == "performance-command":
            self._commands.put(message)
        elif SENSOR_KIND == "gimbal-camera" and message.get("type") == "gimbal-command":
            self._commands.put(message)

    def _apply_performance_mode(self, mode):
        profiles = {
            "eco": (0.20, 0.20, 360, 640, 70),
            "standard": (0.10, 0.10, 720, 960, 78),
            "smooth": (1.0 / 15.0, 1.0 / 15.0, 1080, 960, 80),
        }
        normalized = mode if mode in profiles else "standard"
        (self._scan_interval, self._image_interval, self._max_points,
         self._image_width, self._jpeg_quality) = profiles[normalized]
        self._performance_mode = normalized

    @staticmethod
    def _bounded_number(value, minimum, maximum, fallback):
        try:
            number = float(value)
        except (TypeError, ValueError):
            return fallback
        if not math.isfinite(number):
            return fallback
        return max(minimum, min(maximum, number))

    def _process_commands(self):
        latest = None
        latest_performance = None
        while not self._commands.empty():
            command = self._commands.get_nowait()
            if command.get("type") == "performance-command":
                latest_performance = command
            else:
                latest = command
        if latest_performance is not None:
            self._apply_performance_mode(latest_performance.get("mode"))
        if SENSOR_KIND != "gimbal-camera":
            return
        limits = {
            "yaw": (-180.0, 180.0),
            "pitch": (-135.0, 45.0),
            "roll": (-45.0, 45.0),
        }
        if latest is not None:
            for axis, (minimum, maximum) in limits.items():
                self._gimbal_targets[axis] = self._bounded_number(
                    latest.get(axis), minimum, maximum, self._gimbal_targets[axis])
            self._gimbal_command_received = True
            self._broadcast_gimbal_state(False, "target")
        if not self._gimbal_command_received:
            return
        # The local SDF and bridge use dedicated flightbench/gimbal_* topics.
        # Never compete with PX4's command/gimbal_* zero-position publisher.
        # Keep a bounded 10 Hz refresh; faster publishing cannot fix contention.
        for axis in limits:
            message = Float64()
            message.data = math.radians(self._gimbal_targets[axis])
            self._gimbal_publishers[axis].publish(message)

    def _on_gimbal_joint_state(self, message):
        joint_suffixes = {
            "yaw": "cgo3_vertical_arm_joint",
            "roll": "cgo3_horizontal_arm_joint",
            "pitch": "cgo3_camera_joint",
        }
        changed = False
        for index, name in enumerate(message.name):
            if index >= len(message.position):
                continue
            for axis, suffix in joint_suffixes.items():
                if name.endswith(suffix):
                    self._gimbal_actual[axis] = math.degrees(message.position[index])
                    changed = True
        if changed:
            self._last_gimbal_joint_state = time.monotonic()
            self._broadcast_gimbal_state(True, "joint-state")

    @staticmethod
    def _quaternion_inverse(value):
        return (-value[0], -value[1], -value[2], value[3])

    @staticmethod
    def _quaternion_multiply(left, right):
        lx, ly, lz, lw = left
        rx, ry, rz, rw = right
        return (
            lw * rx + lx * rw + ly * rz - lz * ry,
            lw * ry - lx * rz + ly * rw + lz * rx,
            lw * rz + lx * ry - ly * rx + lz * rw,
            lw * rw - lx * rx - ly * ry - lz * rz,
        )

    @staticmethod
    def _relative_axis_angle(parent, child, axis):
        relative = SensorGateway._quaternion_multiply(
            SensorGateway._quaternion_inverse(parent), child)
        x, y, z, w = relative
        if axis == "x":
            value = math.atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y))
        elif axis == "y":
            value = math.asin(max(-1.0, min(1.0, 2.0 * (w * y - z * x))))
        else:
            value = math.atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z))
        return math.degrees(value)

    def _on_gimbal_poses(self, message):
        now = time.monotonic()
        # JointStatePublisher gives direct joint coordinates.  Do not mix its
        # authoritative values with a separately sampled transform chain,
        # otherwise the UI can alternate between two timestamps and look as if
        # the camera were bouncing.  Keep Pose_V only for older model assets.
        if now - self._last_gimbal_joint_state < 0.5:
            return
        if now - self._last_sent.get("gimbal-pose", 0.0) < 0.10:
            return
        self._last_sent["gimbal-pose"] = now
        poses = {}
        for transform in message.transforms:
            name = transform.child_frame_id.rsplit("/", 1)[-1]
            rotation = transform.transform.rotation
            if name:
                poses[name] = (rotation.x, rotation.y, rotation.z, rotation.w)
        required = ("cgo3_mount_link", "cgo3_vertical_arm_link",
                    "cgo3_horizontal_arm_link", "camera_link")
        if not all(name in poses for name in required):
            # ros_gz_bridge currently drops Pose_V names when converting to a
            # TFMessage. In PX4's stock x500_gimbal model the final four
            # dynamic transforms are, in model order, mount / yaw arm / roll
            # arm / camera. Keep the named path above for future bridge fixes.
            if len(message.transforms) < 4:
                return
            for name, transform in zip(required, message.transforms[-4:]):
                rotation = transform.transform.rotation
                poses[name] = (rotation.x, rotation.y, rotation.z, rotation.w)
        # Joint axes from the stock PX4 gimbal model: yaw -Z, roll X, pitch Y.
        self._gimbal_actual["yaw"] = -self._relative_axis_angle(
            poses[required[0]], poses[required[1]], "z")
        self._gimbal_actual["roll"] = self._relative_axis_angle(
            poses[required[1]], poses[required[2]], "x")
        self._gimbal_actual["pitch"] = self._relative_axis_angle(
            poses[required[2]], poses[required[3]], "y")
        self._broadcast_gimbal_state(True, "dynamic-pose")

    def _broadcast_gimbal_state(self, actual, feedback_source=""):
        # JointStatePublisher can run at the physics rate. Always consume the
        # latest joints, but do not queue hundreds of stale UI events per second.
        if actual and self._may_send("gimbal-feedback", 0.10) is None:
            return
        values = self._gimbal_actual if actual else self._gimbal_targets
        payload = {
            "type": "gimbal-state",
            "actual": bool(actual),
            "yaw": round(values["yaw"], 2),
            "pitch": round(values["pitch"], 2),
            "roll": round(values["roll"], 2),
            "targetYaw": round(self._gimbal_targets["yaw"], 2),
            "targetPitch": round(self._gimbal_targets["pitch"], 2),
            "targetRoll": round(self._gimbal_targets["roll"], 2),
        }
        if feedback_source:
            payload["feedbackSource"] = feedback_source
        self._client_hub.broadcast(payload)

    def _may_send(self, channel, interval):
        now = time.monotonic()
        previous = self._last_sent.get(channel)
        if previous is not None and now - previous < interval:
            return None
        self._last_sent[channel] = now
        return 0.0 if previous is None or now <= previous else round(1.0 / (now - previous), 1)

    def _on_scan(self, message):
        hz = self._may_send("scan", self._scan_interval)
        if hz is None:
            return
        step = max(1, math.ceil(len(message.ranges) / self._max_points))
        ranges = [round(float(v), 4) if math.isfinite(v) else None for v in message.ranges[::step]]
        self._client_hub.broadcast({
            "type": "scan", "sentAtEpochMs": round(time.time() * 1000),
            "angleMin": float(message.angle_min),
            "angleIncrement": float(message.angle_increment) * step,
            "rangeMin": float(message.range_min), "rangeMax": float(message.range_max),
            "hz": hz, "ranges": ranges,
        })

    def _on_rgb(self, message):
        hz = self._may_send("rgb", self._image_interval)
        if hz is None:
            return
        try:
            encoded, width, height = jpeg_payload(ros_image_to_pil(message),
                                                   self._image_width,
                                                   self._jpeg_quality)
        except (ValueError, OSError) as error:
            self.get_logger().warning(str(error))
            return
        self._client_hub.broadcast({
            "type": "camera", "stream": "rgb", "sentAtEpochMs": round(time.time() * 1000),
            "width": width, "height": height, "hz": hz, "jpegBase64": encoded,
        })

    def _on_depth(self, message):
        hz = self._may_send("depth", self._image_interval)
        if hz is None:
            return
        # Pair the map with the pose at callback arrival, before colourising,
        # JPEG encoding and grid packing add processing latency. Gazebo uses
        # simulation time while MAVSDK telemetry is timestamped on the host,
        # so a host epoch captured here is the reliable common clock.
        received_at_epoch_ms = round(time.time() * 1000)
        try:
            width, height, values = depth_values(message)
            visual, nearest, center, ratio, grid_width, grid_height, grid = depth_visual_and_grid(width, height, values)
            encoded, display_width, display_height = jpeg_payload(
                visual, self._image_width, self._jpeg_quality)
        except (ValueError, OSError) as error:
            self.get_logger().warning(str(error))
            return
        self._client_hub.broadcast({
            "type": "camera", "stream": "depth", "sentAtEpochMs": received_at_epoch_ms,
            "width": display_width, "height": display_height, "hz": hz,
            "jpegBase64": encoded, "nearestDistance": round(nearest, 3),
            "centerDistance": round(center, 3), "validRatio": round(ratio, 4),
            "gridWidth": grid_width, "gridHeight": grid_height,
            "depthMillimetresBase64": grid,
        })


def accept_clients(hub, stop_event):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(4)
    server.settimeout(0.5)
    try:
        while not stop_event.is_set():
            try:
                client, _ = server.accept()
                hub.add(client)
            except socket.timeout:
                continue
    finally:
        server.close()


def main():
    rclpy.init()
    hub = ClientHub()
    stop_event = threading.Event()
    threading.Thread(target=accept_clients, args=(hub, stop_event), daemon=True).start()
    node = SensorGateway(hub)
    try:
        rclpy.spin(node)
    except (ExternalShutdownException, KeyboardInterrupt):
        pass
    finally:
        stop_event.set()
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
