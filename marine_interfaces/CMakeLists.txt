cmake_minimum_required(VERSION 3.8)

project(project11_msgs)

set(CMAKE_CXX_STANDARD 17)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)

find_package(geographic_msgs REQUIRED)
find_package(marine_acoustic_msgs REQUIRED)
find_package(std_msgs REQUIRED)

set(MSG_FILES
  msg/AcousticLayer.msg
  msg/AcousticSlice.msg
  msg/BehaviorInformation.msg
  msg/Contact.msg
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
  msg/RangeBearing.msg)

rosidl_generate_interfaces(
    ${PROJECT_NAME}
    ${MSG_FILES}
    DEPENDENCIES
    builtin_interfaces
    geographic_msgs
    marine_acoustic_msgs
    std_msgs
)

ament_export_dependencies(rosidl_default_runtime)

ament_package()
