#!/usr/bin/env python3
# Copyright 2026 Maik Knof
# SPDX-License-Identifier: Apache-2.0

import argparse
import math
import random

import rclpy
from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus, KeyValue
from rclpy.node import Node


def make_status(name, hardware_id, level, message, values):
    status = DiagnosticStatus()
    status.name = name
    status.hardware_id = hardware_id
    status.level = level
    status.message = message
    status.values = [KeyValue(key=key, value=str(value)) for key, value in values.items()]
    return status


class TestDiagnosticsPublisher(Node):
    def __init__(self, topic, rate_hz, include_stale):
        super().__init__("rviz2_diagnostics_monitor_test_data")
        self.publisher = self.create_publisher(DiagnosticArray, topic, 10)
        self.include_stale = include_stale
        self.tick = 0
        self.timer = self.create_timer(1.0 / rate_hz, self.publish)
        self.get_logger().info(f"Publishing synthetic diagnostics on {topic}")

    def publish(self):
        self.tick += 1
        phase = self.tick % 36
        battery_percent = max(0, 95 - self.tick % 120)
        motor_temp = 45.0 + 18.0 * math.sin(self.tick / 7.0)
        lidar_rate = 9.7 + random.uniform(-0.3, 0.3)

        battery_level = DiagnosticStatus.OK
        battery_message = "Battery nominal"
        if battery_percent < 25:
            battery_level = DiagnosticStatus.WARN
            battery_message = "Battery low"
        if battery_percent < 10:
            battery_level = DiagnosticStatus.ERROR
            battery_message = "Battery critical"

        motor_level = DiagnosticStatus.OK
        motor_message = "Motor controller nominal"
        if motor_temp > 58.0:
            motor_level = DiagnosticStatus.WARN
            motor_message = "Motor temperature elevated"
        if motor_temp > 62.0:
            motor_level = DiagnosticStatus.ERROR
            motor_message = "Motor over temperature"

        lidar_level = DiagnosticStatus.OK
        lidar_message = "Scan rate nominal"
        if 18 <= phase < 24:
            lidar_level = DiagnosticStatus.WARN
            lidar_message = "Scan rate jitter"
        if 24 <= phase < 30:
            lidar_level = DiagnosticStatus.ERROR
            lidar_message = "Packet loss detected"

        statuses = [
            make_status(
                "Power/Battery",
                "battery_pack_0",
                battery_level,
                battery_message,
                {
                    "percent": battery_percent,
                    "voltage": f"{22.0 + battery_percent * 0.04:.2f} V",
                    "current": f"{4.0 + random.uniform(-0.5, 0.5):.2f} A",
                },
            ),
            make_status(
                "Drive/Left Motor Controller",
                "motor_left",
                motor_level,
                motor_message,
                {
                    "temperature": f"{motor_temp:.1f} C",
                    "bus_voltage": "48.1 V",
                    "fault_code": "0x00" if motor_level != DiagnosticStatus.ERROR else "0x42",
                },
            ),
            make_status(
                "Sensors/Lidar/Front",
                "lidar_front",
                lidar_level,
                lidar_message,
                {
                    "scan_rate": f"{lidar_rate:.2f} Hz",
                    "dropped_packets": 0 if lidar_level == DiagnosticStatus.OK else self.tick % 7 + 1,
                    "frame_id": "front_laser",
                },
            ),
            make_status(
                "Compute/CPU",
                "ipc_0",
                DiagnosticStatus.OK if phase < 28 else DiagnosticStatus.WARN,
                "CPU load nominal" if phase < 28 else "CPU load high",
                {
                    "load_1m": f"{0.65 + 0.25 * math.sin(self.tick / 5.0):.2f}",
                    "temperature": f"{54.0 + 5.0 * math.sin(self.tick / 9.0):.1f} C",
                },
            ),
        ]

        if self.include_stale and 12 <= phase < 18:
            statuses.append(
                make_status(
                    "Sensors/Camera/Rear",
                    "rear_camera",
                    DiagnosticStatus.STALE,
                    "No fresh diagnostic data",
                    {"last_frame_age": f"{phase - 11} s", "frame_id": "rear_camera"},
                )
            )
        elif self.include_stale:
            statuses.append(
                make_status(
                    "Sensors/Camera/Rear",
                    "rear_camera",
                    DiagnosticStatus.OK,
                    "Camera stream nominal",
                    {"fps": "29.9", "frame_id": "rear_camera"},
                )
            )

        message = DiagnosticArray()
        message.header.stamp = self.get_clock().now().to_msg()
        message.status = statuses
        self.publisher.publish(message)


def main():
    parser = argparse.ArgumentParser(
        description="Publish synthetic diagnostics for rviz2_diagnostics_monitor."
    )
    parser.add_argument("--topic", default="/diagnostics_agg")
    parser.add_argument("--rate", type=float, default=1.0)
    parser.add_argument("--no-stale", action="store_true")
    args = parser.parse_args()

    rclpy.init()
    node = TestDiagnosticsPublisher(args.topic, max(args.rate, 0.1), not args.no_stale)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
