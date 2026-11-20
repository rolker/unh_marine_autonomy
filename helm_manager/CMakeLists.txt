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

# Library target for unit testing (excludes main)
add_library(${PROJECT_NAME}_lib STATIC
  src/helm_manager.cpp
  src/piloting_mode.cpp
)

target_include_directories(${PROJECT_NAME}_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(${PROJECT_NAME}_lib PUBLIC
  ${geometry_msgs_TARGETS}
  ${marine_interfaces_TARGETS}
  rclcpp::rclcpp
  rclcpp_lifecycle::rclcpp_lifecycle
)

add_executable(${PROJECT_NAME} src/helm_manager_node.cpp)
target_link_libraries(${PROJECT_NAME} ${PROJECT_NAME}_lib)

install(TARGETS ${PROJECT_NAME}
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_placeholder test/test_placeholder.cpp)
  target_link_libraries(test_placeholder
    ${geometry_msgs_TARGETS}
    ${marine_interfaces_TARGETS}
    rclcpp::rclcpp
    rclcpp_lifecycle::rclcpp_lifecycle
  )

  find_package(lifecycle_msgs REQUIRED)

  # Piloting mode tests
  ament_add_gtest(test_piloting_mode test/test_piloting_mode.cpp)
  target_link_libraries(test_piloting_mode ${PROJECT_NAME}_lib ${lifecycle_msgs_TARGETS})

  # Helm manager integration tests (lifecycle, mode switching, command arbitration)
  ament_add_gtest(test_helm_manager test/test_helm_manager.cpp)
  target_link_libraries(test_helm_manager ${PROJECT_NAME}_lib ${lifecycle_msgs_TARGETS})

  # Curvature-preserving speed regulation pure-logic tests (ADR-0012, #292)
  ament_add_gtest(test_curvature_regulation test/test_curvature_regulation.cpp)

  # Command conversion and clamping tests
  ament_add_gtest(test_command_conversion test/test_command_conversion.cpp)
  target_link_libraries(test_command_conversion ${PROJECT_NAME}_lib ${lifecycle_msgs_TARGETS})
endif()

ament_package()
