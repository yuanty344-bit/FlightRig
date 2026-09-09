"""Real gateway methods tested without ROS; isolated topic contract with the SDF."""
import ast
import math
from pathlib import Path
from types import SimpleNamespace
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class GimbalChannelTest(unittest.TestCase):
    def test_latest_feedback_is_bounded(self):
        source = ROOT / "docker/ros-sensor-bridge/sensor_gateway.py"
        tree = ast.parse(source.read_text(encoding="utf-8"))
        methods = {"_on_gimbal_joint_state", "_broadcast_gimbal_state", "_may_send"}
        nodes = [node for cls in tree.body if isinstance(cls, ast.ClassDef)
                 for node in cls.body if isinstance(node, ast.FunctionDef) and node.name in methods]
        self.assertEqual(len(nodes), 3)
        clock = SimpleNamespace(now=10.0)
        scope = {"math": math, "time": SimpleNamespace(monotonic=lambda: clock.now)}
        exec(compile(ast.Module(body=nodes, type_ignores=[]), str(source), "exec"), scope)
        payloads = []
        obj = SimpleNamespace(_gimbal_actual={"yaw":0,"pitch":0,"roll":0},
                              _gimbal_targets={"yaw":0,"pitch":0,"roll":0},
                              _last_sent={}, _client_hub=SimpleNamespace(broadcast=payloads.append))
        for name in methods:
            setattr(obj, name, lambda *args, name=name: scope[name](obj, *args))
        for i in range(250):
            clock.now = 10 + i / 250
            obj._on_gimbal_joint_state(SimpleNamespace(name=["cgo3_vertical_arm_joint"],position=[i/1000]))
        self.assertLessEqual(len(payloads), 10)
        self.assertGreaterEqual(len(payloads), 9)
        self.assertAlmostEqual(obj._gimbal_actual["yaw"], math.degrees(.249))
        self.assertTrue(all(p["actual"] and p["feedbackSource"] == "joint-state" for p in payloads))

    def test_topics_do_not_share_px4_zero_command(self):
        model = ET.parse(ROOT / "simulation/models/gimbal/model.sdf")
        topics = [node.text for node in model.findall(".//sub_topic")]
        bridge = (ROOT / "docker/ros-sensor-bridge/entrypoint.sh").read_text(encoding="utf-8")
        for axis in ("yaw", "pitch", "roll"):
            dedicated = "flightbench/gimbal_" + axis
            self.assertIn(dedicated, topics)
            self.assertIn('/model/${MODEL_INSTANCE}/' + dedicated, bridge)
            self.assertNotIn("command/gimbal_" + axis, topics)
            self.assertNotIn('/command/gimbal_' + axis, bridge)


if __name__ == "__main__":
    unittest.main()
