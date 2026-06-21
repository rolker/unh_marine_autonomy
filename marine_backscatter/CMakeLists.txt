cmake_minimum_required(VERSION 3.8)
project(marine_backscatter)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(marine_autonomy REQUIRED)
find_package(marine_tiled_raster_store REQUIRED)
find_package(geographic_msgs REQUIRED)

# Modality-agnostic backscatter processing core (shared by the sidescan + MBES
# layers): best-source compositor, grazing-angle quality, provenance registry.
add_library(${PROJECT_NAME}
  src/processed_accumulator.cpp
  src/quality.cpp
  src/registry.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
ament_target_dependencies(${PROJECT_NAME}
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
ament_export_dependencies(marine_autonomy marine_tiled_raster_store)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_processed_accumulator test/test_processed_accumulator.cpp)
  target_link_libraries(test_processed_accumulator ${PROJECT_NAME})
  ament_target_dependencies(test_processed_accumulator
    geographic_msgs marine_autonomy marine_tiled_raster_store)

  ament_add_gtest(test_quality test/test_quality.cpp)
  target_link_libraries(test_quality ${PROJECT_NAME})
endif()

ament_package()
