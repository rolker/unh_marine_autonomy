cmake_minimum_required(VERSION 3.8)

project(marine_interfaces)

set(CMAKE_CXX_STANDARD 17)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)

find_package(geographic_msgs REQUIRED)
find_package(marine_acoustic_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)

set(MSG_FILES
  msg/AcousticLayer.msg
  msg/AcousticSlice.msg
  msg/BehaviorInformation.msg
  # Unified perception contact family (sensor-agnostic detect/contact)
  msg/Contact.msg
  msg/ContactArray.msg
  msg/Classification.msg
  msg/Kinematics.msg
  msg/Shape.msg
  msg/CourseMadeGood.msg
  msg/CourseMadeGoodStamped.msg
  msg/CTD.msg
  msg/CTDStamped.msg
  msg/Detect.msg
  msg/DifferentialDrive.msg
  msg/GeoPoseNavEuler.msg
  msg/GeoPoseNavEulerStamped.msg
  msg/GeoPoseNavEulerWithCovariance.msg
  msg/GeoPoseNavEulerWithCovarianceStamped.msg
  msg/Heartbeat.msg
  msg/Helm.msg
  msg/KeyValue.msg
  msg/NavEuler.msg
  msg/NavEulerStamped.msg
  msg/NavSource.msg
  msg/PIDDebug.msg
  msg/PIDParameters.msg
  msg/Platform.msg
  msg/PlatformList.msg
  msg/RangeBearing.msg
  msg/SoundSpeed.msg
  # Formerly project11_nav_msgs
  msg/BoundingBox.msg
  msg/Curve.msg
  msg/CurvedTrajectory.msg
  msg/GeoOccupancyPolygon.msg
  msg/GeoOccupancyVectorMap.msg
  msg/GeoPolygon.msg
  msg/OccupancyPolygon.msg
  msg/OccupancyVectorMap.msg
  msg/RobotState.msg
)

rosidl_generate_interfaces(
    ${PROJECT_NAME}
    ${MSG_FILES}
    DEPENDENCIES
    builtin_interfaces
    geographic_msgs
    marine_acoustic_msgs
    std_msgs
    geometry_msgs
    nav_msgs
    diagnostic_msgs
)

ament_export_dependencies(rosidl_default_runtime)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
