cmake_minimum_required(VERSION 3.5.1)

project(helm_manager)

set(CMAKE_CXX_STANDARD 17)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(marine_interfaces REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)

add_executable(${PROJECT_NAME} src/helm_manager_node.cpp src/helm_manager.cpp src/piloting_mode.cpp)

ament_target_dependencies(${PROJECT_NAME}
  geometry_msgs
  marine_interfaces
  rclcpp
  rclcpp_lifecycle
)


install(TARGETS ${PROJECT_NAME}
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_placeholder test/test_placeholder.cpp)
  target_include_directories(test_placeholder PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )
  ament_target_dependencies(test_placeholder
    geometry_msgs
    marine_interfaces
    rclcpp
    rclcpp_lifecycle
  )
endif()

ament_package()
