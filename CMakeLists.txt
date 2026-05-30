cmake_minimum_required(VERSION 3.8)
project(rviz2_diagnostics_monitor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rviz_common REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_library(${PROJECT_NAME} SHARED
  src/diagnostic_model.cpp
  src/diagnostics_monitor_panel.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME}
  Qt5::Widgets
)

ament_target_dependencies(${PROJECT_NAME}
  diagnostic_msgs
  pluginlib
  rclcpp
  rviz_common
)

pluginlib_export_plugin_description_file(
  rviz_common rviz2_diagnostics_monitor_plugins.xml)

install(
  TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  DIRECTORY include/
  DESTINATION include
)

install(
  FILES
    LICENSE
    README.md
    rviz2_diagnostics_monitor_plugins.xml
  DESTINATION share/${PROJECT_NAME}
)

install(
  DIRECTORY icons
  DESTINATION share/${PROJECT_NAME}
)

install(
  PROGRAMS
    scripts/publish_test_diagnostics.py
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_diagnostic_model
    test/test_diagnostic_model.cpp
  )
  if(TARGET test_diagnostic_model)
    target_link_libraries(test_diagnostic_model ${PROJECT_NAME})
    ament_target_dependencies(test_diagnostic_model diagnostic_msgs rclcpp)
  endif()

  ament_add_gtest(test_plugin_exports
    test/test_plugin_exports.cpp
    SKIP_LINKING_MAIN_LIBRARIES
    ENV QT_QPA_PLATFORM=offscreen
  )
  if(TARGET test_plugin_exports)
    target_link_libraries(test_plugin_exports ${PROJECT_NAME} Qt5::Widgets)
    target_compile_definitions(test_plugin_exports PRIVATE
      RVIZ2_DIAGNOSTICS_MONITOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    ament_target_dependencies(test_plugin_exports
      pluginlib
      rviz_common
    )
  endif()

  ament_add_gtest(test_panel_integration
    test/test_panel_integration.cpp
    SKIP_LINKING_MAIN_LIBRARIES
    ENV QT_QPA_PLATFORM=offscreen
        RMW_IMPLEMENTATION=rmw_fastrtps_cpp
        ROS_LOG_DIR=/tmp/rviz2_diagnostics_monitor_test_logs
  )
  if(TARGET test_panel_integration)
    target_link_libraries(test_panel_integration ${PROJECT_NAME} Qt5::Widgets)
    ament_target_dependencies(test_panel_integration diagnostic_msgs rclcpp)
  endif()
endif()

ament_export_include_directories(include)
ament_export_dependencies(
  diagnostic_msgs
  pluginlib
  rclcpp
  rviz_common
)

ament_package()
