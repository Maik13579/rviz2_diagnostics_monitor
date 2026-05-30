# rviz2_diagnostics_monitor

Standalone RViz 2 panel for monitoring `diagnostic_msgs/msg/DiagnosticArray`
streams. The panel defaults to `/diagnostics_agg`, groups diagnostic names into
an operator tree, keeps bounded health history, and records event-feed rows only
when a diagnostic state changes.

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
