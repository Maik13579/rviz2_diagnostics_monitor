// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <QApplication>
#include <gtest/gtest.h>
#include <pluginlib/class_loader.hpp>
#include <rviz_common/config.hpp>
#include <rviz_common/factory/pluginlib_factory.hpp>
#include <rviz_common/panel.hpp>

#include "rviz2_diagnostics_monitor/diagnostics_monitor_panel.hpp"

TEST(PluginExports, PanelIsDeclaredAndLoadable) {
  pluginlib::ClassLoader<rviz_common::Panel> loader("rviz_common",
                                                    "rviz_common::Panel");
  const auto classes = loader.getDeclaredClasses();
  const std::string class_name =
      "rviz2_diagnostics_monitor/DiagnosticsMonitorPanel";
  EXPECT_NE(std::find(classes.begin(), classes.end(), class_name),
            classes.end());

  const auto instance = loader.createSharedInstance(class_name);
  ASSERT_NE(instance, nullptr);
}

TEST(PackageMetadata, MatchesStandalonePackageRequirements) {
  const auto source_dir =
      std::filesystem::path(RVIZ2_DIAGNOSTICS_MONITOR_SOURCE_DIR);
  std::ifstream package_file(source_dir / "package.xml");
  ASSERT_TRUE(package_file.good());
  const std::string package_xml((std::istreambuf_iterator<char>(package_file)),
                                std::istreambuf_iterator<char>());
  EXPECT_NE(package_xml.find("maik.knof@gmx.de"), std::string::npos);
  EXPECT_NE(package_xml.find("<license>Apache-2.0</license>"),
            std::string::npos);
  EXPECT_NE(package_xml.find("<depend>diagnostic_msgs</depend>"),
            std::string::npos);
}

TEST(PluginMetadata, ExportsDiagnosticsMonitorPanel) {
  const auto source_dir =
      std::filesystem::path(RVIZ2_DIAGNOSTICS_MONITOR_SOURCE_DIR);
  std::ifstream plugin_file(source_dir / "rviz2_diagnostics_monitor_plugins.xml");
  ASSERT_TRUE(plugin_file.good());
  const std::string plugin_xml((std::istreambuf_iterator<char>(plugin_file)),
                               std::istreambuf_iterator<char>());
  EXPECT_NE(plugin_xml.find("rviz2_diagnostics_monitor/DiagnosticsMonitorPanel"),
            std::string::npos);
  EXPECT_NE(plugin_xml.find("rviz_common::Panel"), std::string::npos);
}

TEST(PluginMetadata, PanelIconIsLoadable) {
  const auto source_dir =
      std::filesystem::path(RVIZ2_DIAGNOSTICS_MONITOR_SOURCE_DIR);
  EXPECT_TRUE(std::filesystem::exists(
      source_dir / "icons/classes/DiagnosticsMonitorPanel.svg"));

  rviz_common::PluginlibFactory<rviz_common::Panel> factory(
      "rviz_common", "rviz_common::Panel");
  const auto info = factory.getPluginInfo(
      "rviz2_diagnostics_monitor/DiagnosticsMonitorPanel");
  EXPECT_FALSE(info.icon.isNull());
}

TEST(PanelConfig, RoundTripsSettings) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;
  rviz_common::Config input;
  input.mapSetValue("Diagnostics Topic", QString("/test_diagnostics"));
  input.mapSetValue("Stale Timeout Ms", 1200);
  input.mapSetValue("History Window Sec", 42);
  input.mapSetValue("Show OK Events", false);
  input.mapSetValue("Show WARN Events", true);
  input.mapSetValue("Show ERROR Events", false);
  input.mapSetValue("Show STALE Events", true);

  panel.load(input);

  rviz_common::Config output;
  panel.save(output);

  QString topic;
  int int_value = 0;
  bool bool_value = true;
  ASSERT_TRUE(output.mapGetString("Diagnostics Topic", &topic));
  EXPECT_EQ(topic, "/test_diagnostics");
  ASSERT_TRUE(output.mapGetInt("Stale Timeout Ms", &int_value));
  EXPECT_EQ(int_value, 1200);
  ASSERT_TRUE(output.mapGetInt("History Window Sec", &int_value));
  EXPECT_EQ(int_value, 42);
  ASSERT_TRUE(output.mapGetBool("Show OK Events", &bool_value));
  EXPECT_FALSE(bool_value);
  ASSERT_TRUE(output.mapGetBool("Show WARN Events", &bool_value));
  EXPECT_TRUE(bool_value);
  ASSERT_TRUE(output.mapGetBool("Show ERROR Events", &bool_value));
  EXPECT_FALSE(bool_value);
  ASSERT_TRUE(output.mapGetBool("Show STALE Events", &bool_value));
  EXPECT_TRUE(bool_value);
}

int main(int argc, char **argv) {
  setenv("QT_QPA_PLATFORM", "offscreen", 0);
  testing::InitGoogleTest(&argc, argv);
  QApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
