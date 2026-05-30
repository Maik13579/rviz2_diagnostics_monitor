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
