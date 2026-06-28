cmake_minimum_required(VERSION 3.8)
project(marine_contacts)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(marine_interfaces REQUIRED)
find_package(rclcpp REQUIRED)

# Qt-free Contact builder + manual-curation store. Built with position-independent
# code so this static library can be linked into SHARED consumers transitively —
# the rqt_sonar_waterfall plugin (rqt_operator_tools#86) is a shared object that
# links it. Without PIC the plugin link fails (R_X86_64_PC32).
add_library(${PROJECT_NAME}
  src/contact_store.cpp
)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)

# marine_interfaces is the public (header) dependency; rclcpp is used only inside
# ContactStore::save/load for the CDR round-trip. Both are exported below: this is a
# static library, so CMake propagates even the implementation-only link to consumers.
ament_target_dependencies(${PROJECT_NAME} marine_interfaces rclcpp)

# Headers are laid out as include/${PROJECT_NAME}/*.hpp, so install the include/ tree
# to `include` (matching the $<INSTALL_INTERFACE:include> above).
install(DIRECTORY include/ DESTINATION include)
install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(marine_interfaces rclcpp)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)

  # Box->Contact build, in-box query, CDR save/load round-trip, GeoJSON export.
  ament_add_gtest(test_contact_store test/test_contact_store.cpp)
  target_link_libraries(test_contact_store ${PROJECT_NAME})
endif()

ament_package()
