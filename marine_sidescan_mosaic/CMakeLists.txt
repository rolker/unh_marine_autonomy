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
find_package(rclcpp REQUIRED)
find_package(marine_acoustic_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(geodesy REQUIRED)
find_package(marine_autonomy REQUIRED)
find_package(marine_tiled_raster_store REQUIRED)
find_package(marine_backscatter REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(rosbag2_storage REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(tf2_msgs REQUIRED)

# Core library (pure logic; node executable + more units land in later slices).
add_library(${PROJECT_NAME}
  src/projection.cpp
  src/accumulator.cpp
  src/normalizer.cpp
  src/tier1.cpp
  src/decode.cpp
  src/overview_pyramid.cpp
  src/bathy_dem.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
ament_target_dependencies(${PROJECT_NAME}
  marine_acoustic_msgs
  geographic_msgs
  geodesy
  marine_autonomy
  marine_tiled_raster_store
)

# Node executable.
add_executable(sidescan_mosaic_node src/mosaic_node.cpp)
target_link_libraries(sidescan_mosaic_node ${PROJECT_NAME})
ament_target_dependencies(sidescan_mosaic_node
  rclcpp
  marine_acoustic_msgs
  sensor_msgs
  tf2
  tf2_ros
  geographic_msgs
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
install(TARGETS sidescan_mosaic_node DESTINATION lib/${PROJECT_NAME})

# Offline bag importer -> Tier-1 (issue #184).
add_executable(sidescan_mosaic_bag src/sidescan_mosaic_bag.cpp)
target_link_libraries(sidescan_mosaic_bag ${PROJECT_NAME})
ament_target_dependencies(sidescan_mosaic_bag
  rclcpp
  rosbag2_cpp
  rosbag2_storage
  marine_acoustic_msgs
  sensor_msgs
  geometry_msgs
  builtin_interfaces
  tf2
  tf2_msgs
)
install(TARGETS sidescan_mosaic_bag DESTINATION lib/${PROJECT_NAME})

# Flat-bottom Tier-2 projector: Tier-1 -> GGGS tiles (issue #184).
add_executable(sidescan_tier2_flat src/sidescan_tier2_flat.cpp)
target_link_libraries(sidescan_tier2_flat ${PROJECT_NAME})
ament_target_dependencies(sidescan_tier2_flat
  geographic_msgs
  marine_autonomy
  marine_tiled_raster_store
)
install(TARGETS sidescan_tier2_flat DESTINATION lib/${PROJECT_NAME})

# Durable processed Tier-2: best-source composite -> 3-band tiles + registry (#184).
add_executable(sidescan_tier2_processed src/sidescan_tier2_processed.cpp)
target_link_libraries(sidescan_tier2_processed ${PROJECT_NAME})
ament_target_dependencies(sidescan_tier2_processed
  geographic_msgs
  marine_autonomy
  marine_tiled_raster_store
  marine_backscatter
)
install(TARGETS sidescan_tier2_processed DESTINATION lib/${PROJECT_NAME})

# Overview-pyramid batch builder: fine tiles -> overviews/ sidecar (#188 / ADR-0011).
# Thin CLI over the library's overview_pyramid production path.
add_executable(build_sidescan_overviews src/build_sidescan_overviews.cpp)
target_link_libraries(build_sidescan_overviews ${PROJECT_NAME})
ament_target_dependencies(build_sidescan_overviews
  geographic_msgs
  marine_autonomy
  marine_tiled_raster_store
)
install(TARGETS build_sidescan_overviews DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

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

  ament_add_gtest(test_normalizer test/test_normalizer.cpp)
  target_link_libraries(test_normalizer ${PROJECT_NAME})

  ament_add_gtest(test_tier1 test/test_tier1.cpp)
  target_link_libraries(test_tier1 ${PROJECT_NAME})

  # Exercises the overview-builder production path on real on-disk tiles
  # (grid reconstruction, per-level fold, level loop, arg parsing) — #188.
  ament_add_gtest(test_overview_pyramid test/test_overview_pyramid.cpp)
  target_link_libraries(test_overview_pyramid ${PROJECT_NAME})
  ament_target_dependencies(test_overview_pyramid marine_autonomy marine_tiled_raster_store)

  # DEM reader over synthetic bathy value tiles (#297). Fixtures are written with
  # marine_tiled_raster_store::saveTile<double> — NOT marine_bathymetry_store's
  # writer, which would add a test-only package dependency cutting against
  # ADR-0006 D9's decoupling.
  ament_add_gtest(test_bathy_dem test/test_bathy_dem.cpp)
  target_link_libraries(test_bathy_dem ${PROJECT_NAME})
  ament_target_dependencies(test_bathy_dem marine_autonomy marine_tiled_raster_store)

endif()

ament_package()
