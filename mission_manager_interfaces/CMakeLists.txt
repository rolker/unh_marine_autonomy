cmake_minimum_required(VERSION 3.8)
project(mission_manager_interfaces)

set(CMAKE_CXX_STANDARD 17)

find_package(ament_cmake REQUIRED)
find_package(marine_nav_interfaces REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(
    ${PROJECT_NAME}
    srv/TaskInformationIdList.srv
    srv/TaskInformationList.srv
    srv/TaskManagerCmd.srv
    DEPENDENCIES
    marine_nav_interfaces
)

ament_export_dependencies(
    marine_nav_interfaces
    rosidl_default_runtime
)   

ament_package()
