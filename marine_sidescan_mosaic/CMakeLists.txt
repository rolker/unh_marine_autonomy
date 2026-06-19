cmake_minimum_required(VERSION 3.8)
project(marine_sidescan_mosaic)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(geodesy REQUIRED)
find_package(marine_autonomy REQUIRED)
find_package(marine_tiled_raster_store REQUIRED)

# Core library (pure logic; node executable + more units land in later slices).
add_library(${PROJECT_NAME}
  src/projection.cpp
  src/accumulator.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
ament_target_dependencies(${PROJECT_NAME}
  geographic_msgs
  geodesy
  marine_autonomy
  marine_tiled_raster_store
)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(geographic_msgs geodesy marine_autonomy marine_tiled_raster_store)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_projection test/test_projection.cpp)
  target_link_libraries(test_projection ${PROJECT_NAME})
  ament_target_dependencies(test_projection geographic_msgs geodesy marine_autonomy)

  ament_add_gtest(test_accumulator test/test_accumulator.cpp)
  target_link_libraries(test_accumulator ${PROJECT_NAME})
  ament_target_dependencies(test_accumulator marine_autonomy marine_tiled_raster_store)
endif()

ament_package()
