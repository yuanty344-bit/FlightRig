#!/usr/bin/env bash
set -eo pipefail

# ROS setup scripts legitimately inspect optional variables before defining
# them. Enable nounset only after the ROS environment has been loaded.
source /opt/ros/jazzy/setup.bash
set -u

export GZ_PARTITION="${GZ_PARTITION:-flightbench}"
export IGN_PARTITION="${IGN_PARTITION:-${GZ_PARTITION}}"

WORLD="${PX4_GZ_WORLD:-default}"
MODEL_INSTANCE="${PX4_MODEL_INSTANCE:-x500_lidar_2d_0}"
SENSOR_KIND="${SENSOR_KIND:-lidar-2d}"
GATEWAY_PORT="${SENSOR_GATEWAY_PORT:-8765}"
ROS_SCAN_TOPIC="${ROS_SCAN_TOPIC:-/flightbench/lidar/scan}"
ROS_RGB_TOPIC="${ROS_RGB_TOPIC:-/flightbench/camera/rgb}"
ROS_DEPTH_TOPIC="${ROS_DEPTH_TOPIC:-/flightbench/camera/depth}"
ROS_GIMBAL_JOINT_TOPIC="${ROS_GIMBAL_JOINT_TOPIC:-/flightbench/gimbal/joint_states}"
ROS_GIMBAL_POSE_TOPIC="${ROS_GIMBAL_POSE_TOPIC:-/flightbench/gimbal/poses}"
BRIDGE_CONFIG="/tmp/flightbench-sensor-bridge.yaml"

if [[ "${SENSOR_KIND}" == "lidar-2d" ]]; then
    GZ_SENSOR_TOPIC="${GZ_LIDAR_SCAN_TOPIC:-/world/${WORLD}/model/${MODEL_INSTANCE}/link/link/sensor/lidar_2d_v2/scan}"
    cat > "${BRIDGE_CONFIG}" <<YAML
- ros_topic_name: "${ROS_SCAN_TOPIC}"
  gz_topic_name: "${GZ_SENSOR_TOPIC}"
  ros_type_name: "sensor_msgs/msg/LaserScan"
  gz_type_name: "gz.msgs.LaserScan"
  direction: GZ_TO_ROS
YAML
elif [[ "${SENSOR_KIND}" == "mono-camera" ]]; then
    GZ_SENSOR_TOPIC="${GZ_RGB_TOPIC:-/world/${WORLD}/model/${MODEL_INSTANCE}/link/camera_link/sensor/camera/image}"
    cat > "${BRIDGE_CONFIG}" <<YAML
- ros_topic_name: "${ROS_RGB_TOPIC}"
  gz_topic_name: "${GZ_SENSOR_TOPIC}"
  ros_type_name: "sensor_msgs/msg/Image"
  gz_type_name: "gz.msgs.Image"
  direction: GZ_TO_ROS
YAML
elif [[ "${SENSOR_KIND}" == "depth-camera" ]]; then
    GZ_RGB_SENSOR_TOPIC="${GZ_RGB_TOPIC:-/world/${WORLD}/model/${MODEL_INSTANCE}/link/camera_link/sensor/IMX214/image}"
    GZ_DEPTH_SENSOR_TOPIC="${GZ_DEPTH_TOPIC:-/depth_camera}"
    cat > "${BRIDGE_CONFIG}" <<YAML
- ros_topic_name: "${ROS_RGB_TOPIC}"
  gz_topic_name: "${GZ_RGB_SENSOR_TOPIC}"
  ros_type_name: "sensor_msgs/msg/Image"
  gz_type_name: "gz.msgs.Image"
  direction: GZ_TO_ROS
- ros_topic_name: "${ROS_DEPTH_TOPIC}"
  gz_topic_name: "${GZ_DEPTH_SENSOR_TOPIC}"
  ros_type_name: "sensor_msgs/msg/Image"
  gz_type_name: "gz.msgs.Image"
  direction: GZ_TO_ROS
YAML
elif [[ "${SENSOR_KIND}" == "gimbal-camera" ]]; then
    GZ_RGB_SENSOR_TOPIC="${GZ_RGB_TOPIC:-/world/${WORLD}/model/${MODEL_INSTANCE}/link/camera_link/sensor/camera/image}"
    GZ_GIMBAL_JOINT_TOPIC="${GZ_GIMBAL_JOINT_TOPIC:-/world/${WORLD}/model/${MODEL_INSTANCE}/joint_state}"
    cat > "${BRIDGE_CONFIG}" <<YAML
- ros_topic_name: "${ROS_RGB_TOPIC}"
  gz_topic_name: "${GZ_RGB_SENSOR_TOPIC}"
  ros_type_name: "sensor_msgs/msg/Image"
  gz_type_name: "gz.msgs.Image"
  direction: GZ_TO_ROS
- ros_topic_name: "${ROS_GIMBAL_JOINT_TOPIC}"
  gz_topic_name: "${GZ_GIMBAL_JOINT_TOPIC}"
  ros_type_name: "sensor_msgs/msg/JointState"
  gz_type_name: "gz.msgs.Model"
  direction: GZ_TO_ROS
- ros_topic_name: "${ROS_GIMBAL_POSE_TOPIC}"
  gz_topic_name: "/world/${WORLD}/dynamic_pose/info"
  ros_type_name: "tf2_msgs/msg/TFMessage"
  gz_type_name: "gz.msgs.Pose_V"
  direction: GZ_TO_ROS
- ros_topic_name: "/flightbench/gimbal/yaw/command"
  gz_topic_name: "/model/${MODEL_INSTANCE}/flightbench/gimbal_yaw"
  ros_type_name: "std_msgs/msg/Float64"
  gz_type_name: "gz.msgs.Double"
  direction: ROS_TO_GZ
- ros_topic_name: "/flightbench/gimbal/pitch/command"
  gz_topic_name: "/model/${MODEL_INSTANCE}/flightbench/gimbal_pitch"
  ros_type_name: "std_msgs/msg/Float64"
  gz_type_name: "gz.msgs.Double"
  direction: ROS_TO_GZ
- ros_topic_name: "/flightbench/gimbal/roll/command"
  gz_topic_name: "/model/${MODEL_INSTANCE}/flightbench/gimbal_roll"
  ros_type_name: "std_msgs/msg/Float64"
  gz_type_name: "gz.msgs.Double"
  direction: ROS_TO_GZ
YAML
else
    echo "[flightbench-ros] Unsupported sensor kind: ${SENSOR_KIND}" >&2
    exit 2
fi

cat >> "${BRIDGE_CONFIG}" <<YAML
- ros_topic_name: "/clock"
  gz_topic_name: "/clock"
  ros_type_name: "rosgraph_msgs/msg/Clock"
  gz_type_name: "gz.msgs.Clock"
  direction: GZ_TO_ROS
YAML

echo "[flightbench-ros] Gazebo partition: ${GZ_PARTITION}"
echo "[flightbench-ros] Sensor kind: ${SENSOR_KIND}"

ros2 run ros_gz_bridge parameter_bridge \
    --ros-args -p config_file:="${BRIDGE_CONFIG}" &
bridge_pid="$!"

SENSOR_GATEWAY_PORT="${GATEWAY_PORT}" \
ROS_SCAN_TOPIC="${ROS_SCAN_TOPIC}" \
ROS_RGB_TOPIC="${ROS_RGB_TOPIC}" \
ROS_DEPTH_TOPIC="${ROS_DEPTH_TOPIC}" \
SENSOR_KIND="${SENSOR_KIND}" \
python3 /opt/flightbench/sensor_gateway.py &
gateway_pid="$!"

shutdown() {
    kill "${bridge_pid}" "${gateway_pid}" 2>/dev/null || true
    wait "${bridge_pid}" "${gateway_pid}" 2>/dev/null || true
}
trap shutdown EXIT INT TERM

wait -n "${bridge_pid}" "${gateway_pid}"
