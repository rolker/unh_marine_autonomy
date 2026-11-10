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

# Modality-agnostic backscatter processing core. Shared by the sidescan + MBES
# layers (ADR-0006/0007): grazing-angle quality, provenance registry, the seabed
# geometry of the GeoCoder radiometry chain (slope-aware incidence, #304). The
# COMPOSITOR is per-store, not shared — this lib ships the sidescan best-source
# ProcessedAccumulator (uint16); MBES adds its own Welford mean+variance
# accumulator (float). See ADR-0007 D6. Also the store-backed height sampler
# BathyDem (moved from marine_sidescan_mosaic, #304), which reads the bathy
# store's value tiles directly off disk: a file-level coupling with no
# marine_bathymetry_store package dependency (ADR-0006 D9).
#
# ONE library on purpose (#304 plan review): it already links
# marine_tiled_raster_store through processed_accumulator, so a separate
# "pure geometry" target would not have kept the GeoTIFF reader out of a
# consumer's link. What IS dependency-free is seabed_geometry.hpp itself (std
# only).
add_library(${PROJECT_NAME}
  src/bathy_dem.cpp
  src/processed_accumulator.cpp
  src/quality.cpp
  src/registry.cpp
  src/seabed_geometry.cpp
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

  # Seabed geometry (#304) against synthetic surfaces. gz4d's LocalENU (in
  # marine_autonomy) gives the test an ENU conversion independent of the
  # module's own metres-per-degree helper.
  ament_add_gtest(test_seabed_geometry test/test_seabed_geometry.cpp)
  target_link_libraries(test_seabed_geometry ${PROJECT_NAME})
  ament_target_dependencies(test_seabed_geometry marine_autonomy)

  # DEM reader over synthetic bathy value tiles (#297, moved here in #304).
  # Fixtures are written with marine_tiled_raster_store::saveTile<double> — NOT
  # marine_bathymetry_store's writer, which would add a test-only package
  # dependency cutting against ADR-0006 D9's decoupling.
  ament_add_gtest(test_bathy_dem test/test_bathy_dem.cpp)
  target_link_libraries(test_bathy_dem ${PROJECT_NAME})
  ament_target_dependencies(test_bathy_dem
    geographic_msgs marine_autonomy marine_tiled_raster_store)
endif()

ament_package()
