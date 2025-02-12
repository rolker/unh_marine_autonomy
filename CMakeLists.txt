cmake_minimum_required(VERSION 3.5.1)

project(helm_manager)

set(CMAKE_CXX_STANDARD 17)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(project11_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)

add_executable(${PROJECT_NAME} src/helm_manager_node.cpp src/helm_manager.cpp src/piloting_mode.cpp)

ament_target_dependencies(${PROJECT_NAME}
  geometry_msgs
  project11_msgs
  rclcpp
  rclcpp_lifecycle
)


install(TARGETS ${PROJECT_NAME}
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

ament_package()
