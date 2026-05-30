# rviz2_diagnostics_monitor

Standalone RViz 2 panel for monitoring `diagnostic_msgs/msg/DiagnosticArray`
streams. The panel defaults to `/diagnostics_agg`, groups diagnostic names into
an operator tree, keeps bounded health history, and records event-feed rows only
when a diagnostic state changes.

## Overview

The Overview tab groups diagnostics by their `/`-separated names, summarizes the
current robot health, highlights error/warn/stale devices, and shows details for
the selected diagnostic or group.

![Diagnostics monitor overview](doc/overview.png)

## Event Feed

The Event Feed tab records diagnostic state changes only. It can be filtered by
severity, hardware ID, and free-text search.

![Diagnostics monitor event feed](doc/event_feed.png)

## Running

Build the package and source the workspace install before starting RViz:

```bash
source /opt/ros/${ROS_DISTRO}/setup.bash
colcon build --base-paths /root/ros2_ws/src /root/ros2_ws/external/rviz2_diagnostics_monitor --packages-select rviz2_diagnostics_monitor
source /root/ros2_ws/install/setup.bash
rviz2
```

Add the panel from RViz with `Panels` -> `Add New Panel` ->
`rviz2_diagnostics_monitor/DiagnosticsMonitorPanel`.

Synthetic diagnostics can be published with:

```bash
ros2 run rviz2_diagnostics_monitor publish_test_diagnostics.py --topic /diagnostics_agg
```
