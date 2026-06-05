# rviz2_diagnostics_monitor

Standalone RViz 2 panel for monitoring `diagnostic_msgs/msg/DiagnosticArray`
streams. The panel defaults to `/diagnostics`, groups diagnostic names into
an operator tree, keeps bounded health history, and records received diagnostic
updates in the event feed.

## Overview

The Overview tab groups diagnostics by their `/`-separated names, summarizes the
current robot health, and splits all/error/warn/stale views into compact
hierarchical trees. Double-click a diagnostic leaf to open its detail popup.

![Diagnostics monitor overview](doc/overview.png)

## Event Feed

The Event Feed tab records received diagnostic updates. It shows a compact
name/message table, can be filtered by severity, hardware ID, and free-text
search, can be switched back to state-change-only recording, and opens the full
diagnostic popup on row double-click.

![Diagnostics monitor event feed](doc/event_feed.png)
