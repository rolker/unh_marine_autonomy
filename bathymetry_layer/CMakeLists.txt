cmake_minimum_required(VERSION 3.14.4)
project(bathymetry_layer)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(geodesy REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(marine_bathymetry_store REQUIRED)
find_package(nav2_costmap_2d REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/bathymetry_layer.cpp
)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

ament_target_dependencies(${PROJECT_NAME}
  geodesy
)

target_link_libraries(${PROJECT_NAME}
  ${geographic_msgs_TARGETS}
  ${geometry_msgs_TARGETS}
  marine_bathymetry_store::marine_bathymetry_store
  nav2_costmap_2d::nav2_costmap_2d_core
  pluginlib::pluginlib
  rclcpp::rclcpp
  tf2::tf2
  tf2_geometry_msgs::tf2_geometry_msgs
  tf2_ros::tf2_ros
)

install(
  TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

pluginlib_export_plugin_description_file(nav2_costmap_2d costmap_plugins.xml)

ament_export_targets(export_${PROJECT_NAME})
ament_export_libraries(${PROJECT_NAME})
ament_export_dependencies(
  geographic_msgs marine_bathymetry_store nav2_costmap_2d pluginlib rclcpp)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(nav2_util REQUIRED)

  ament_add_gtest(test_bathymetry_layer test/test_bathymetry_layer.cpp)
  target_include_directories(test_bathymetry_layer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_link_libraries(test_bathymetry_layer
    ${PROJECT_NAME}
    marine_bathymetry_store::marine_bathymetry_store
    nav2_costmap_2d::nav2_costmap_2d_core
    nav2_util::nav2_util_core
    rclcpp::rclcpp
    tf2_ros::tf2_ros
  )

  ament_add_gtest(test_plugin_loading test/test_plugin_loading.cpp)
  target_link_libraries(test_plugin_loading
    nav2_costmap_2d::nav2_costmap_2d_core
    pluginlib::pluginlib
  )

  find_package(ament_lint_auto REQUIRED)
  # Skip copyright: this package uses a short BSD header, not the ament default;
  # copyright is tracked at the repo level (consistent with sibling packages).
  set(ament_cmake_copyright_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
