"""Exercise the actual gateway ClientHub without requiring ROS or camera hardware.

Run: python tests/test_sensor_gateway_clock.py
"""
import ast
import json
from pathlib import Path
import socket
import threading
import time
import unittest


def gateway_hub():
    source = Path(__file__).resolve().parents[1] / "docker/ros-sensor-bridge/sensor_gateway.py"
    tree = ast.parse(source.read_text(encoding="utf-8"))
    hub = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "ClientHub")
    namespace = {"socket": socket, "threading": threading, "time": time, "json": json}
    exec(compile(ast.Module(body=[hub], type_ignores=[]), str(source), "exec"), namespace)
    return namespace["ClientHub"]()


class ClockProtocolTest(unittest.TestCase):
    def setUp(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        self.client = socket.create_connection(listener.getsockname(), timeout=2)
        self.server, _ = listener.accept()
        listener.close()
        self.reader = self.client.makefile("rb")
        self.hub = gateway_hub()
        self.hub.add(self.server)

    def tearDown(self):
        self.client.shutdown(socket.SHUT_RDWR)
        self.reader.close()
        self.client.close()
        self.hub._remove(self.server)

    def receive(self):
        return json.loads(self.reader.readline())

    def test_ping_does_not_enter_ros_command_queue(self):
        commands = []
        arrived = threading.Event()
        def command(value):
            commands.append(value)
            arrived.set()
        self.hub.set_command_handler(command)
        before = time.monotonic_ns() // 1_000_000
        self.client.sendall(b'{"type":"clock-ping","id":42}\n')
        pong = self.receive()
        after = time.monotonic_ns() // 1_000_000
        self.assertEqual((pong["type"], pong["id"]), ("clock-pong", 42))
        self.assertLessEqual(before, pong["serverReceiveMonoMs"])
        self.assertLessEqual(pong["serverReceiveMonoMs"], pong["serverSendMonoMs"])
        self.assertLessEqual(pong["serverSendMonoMs"], after)
        self.assertEqual(commands, [])
        self.client.sendall(b'{"type":"gimbal","yaw":10}\n')
        self.assertTrue(arrived.wait(2))
        self.assertEqual(commands[0]["yaw"], 10)

    def test_frames_have_monotonic_origin_independent_of_epoch(self):
        for stream, epoch in [("rgb", 1), ("depth", 9999999999999)]:
            message = {"type": "camera", "stream": stream, "sentAtEpochMs": epoch}
            before = time.monotonic_ns() // 1_000_000
            self.hub.broadcast(message)
            frame = self.receive()
            self.assertGreaterEqual(frame["sentAtMonotonicMs"], before)
            self.assertLessEqual(frame["sentAtMonotonicMs"], time.monotonic_ns() // 1_000_000)
            self.assertEqual(frame["sentAtEpochMs"], epoch)
            self.assertNotIn("sentAtMonotonicMs", message)


if __name__ == "__main__":
    unittest.main()
