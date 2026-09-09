FROM ros:jazzy-ros-base-noble

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       ros-jazzy-ros-gz-bridge \
       python3-numpy \
       python3-pil \
    && rm -rf /var/lib/apt/lists/*

COPY docker/ros-sensor-bridge/entrypoint.sh /opt/flightbench/entrypoint.sh
COPY docker/ros-sensor-bridge/sensor_gateway.py /opt/flightbench/sensor_gateway.py

RUN chmod +x /opt/flightbench/entrypoint.sh /opt/flightbench/sensor_gateway.py

EXPOSE 8765

ENTRYPOINT ["/opt/flightbench/entrypoint.sh"]
