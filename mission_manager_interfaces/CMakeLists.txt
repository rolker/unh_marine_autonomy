cmake_minimum_required(VERSION 3.8)
project(mission_manager_interfaces)

set(CMAKE_CXX_STANDARD 17)

find_package(ament_cmake REQUIRED)
find_package(project11_nav_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(
    ${PROJECT_NAME}
    srv/TaskInformationIdList.srv
    srv/TaskInformationList.srv
    srv/TaskManagerCmd.srv
    DEPENDENCIES
    project11_nav_msgs
)

ament_export_dependencies(
    project11_nav_msgs
    rosidl_default_runtime
)   

ament_package()
