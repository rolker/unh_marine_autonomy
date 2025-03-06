cmake_minimum_required(VERSION 3.8)

project(project11_nav_msgs)

set(CMAKE_CXX_STANDARD 17)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)

find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(project11_msgs REQUIRED)
find_package(std_msgs REQUIRED)

set(MSG_FILES
  action/Hover.action
  action/MultibeamCoverage.action
  action/RunTasks.action
  msg/BoundingBox.msg
  msg/Curve.msg
  msg/CurvedTrajectory.msg
  msg/GeoOccupancyPolygon.msg
  msg/GeoOccupancyVectorMap.msg
  msg/GeoPolygon.msg
  msg/OccupancyPolygon.msg
  msg/OccupancyVectorMap.msg
  msg/RobotState.msg
  msg/TaskFeedback.msg
  msg/TaskInformation.msg
  msg/TaskInformationList.msg
)

rosidl_generate_interfaces(
  ${PROJECT_NAME}
  ${MSG_FILES}
  DEPENDENCIES
  builtin_interfaces
  geometry_msgs
  nav_msgs
  project11_msgs
  std_msgs
)

ament_export_dependencies(
  rosidl_default_runtime
)

ament_package()

