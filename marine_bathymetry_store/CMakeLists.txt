cmake_minimum_required(VERSION 3.8)
project(marine_bathymetry_store)

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
# nlohmann_json backs the source registry (registry.json sidecar, #178). Declared
# explicitly here (and in package.xml) — it was only available transitively
# before, which is not a dependency this package may rely on.
find_package(nlohmann_json REQUIRED)
# GDAL is used directly by the GeoTIFF importer (geotiff_import.cpp): it opens
# the source raster and reads bands/geotransform/SRS. tile_io still delegates its
# GeoTIFF I/O to marine_tiled_raster_store; the importer needs GDAL directly, so
# declare it explicitly (also in package.xml).
find_package(GDAL REQUIRED)

add_library(${PROJECT_NAME}
  src/bathymetry_store.cpp
  src/geotiff_import.cpp
  src/query.cpp
  src/registry.cpp
  src/tile_io.cpp
)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Build with position-independent code so this (static) library can be linked
# into SHARED consumers — notably the bathymetry_layer Nav2 costmap plugin
# (#164), which is a shared object loaded by pluginlib. Without PIC the link of
# the plugin fails (R_X86_64_PC32 relocation against source_layers_by_priority).
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
# marine_autonomy + geographic_msgs are in the public headers (GGGS / GeoPoint);
# marine_tiled_raster_store is public too — BathymetryTile wraps its
# TiledRasterTile<double>. GDAL arrives transitively through that package (which
# uses it privately in tile_io.cpp); this package no longer references GDAL.
target_link_libraries(${PROJECT_NAME} PUBLIC
  marine_autonomy::marine_autonomy
  marine_tiled_raster_store::marine_tiled_raster_store
  ${geographic_msgs_TARGETS}
)
# nlohmann_json is header-only and used only inside registry.cpp (the public
# registry.hpp keeps it out of its interface); GDAL is used only inside
# geotiff_import.cpp (the public geotiff_import.hpp keeps it out of its
# interface). Both are PRIVATE.
target_link_libraries(${PROJECT_NAME} PRIVATE
  nlohmann_json::nlohmann_json
  ${GDAL_LIBRARIES}
)
target_include_directories(${PROJECT_NAME} PRIVATE ${GDAL_INCLUDE_DIRS})

# Headers are laid out as include/${PROJECT_NAME}/*.hpp, so install the include/
# tree to `include` (not include/${PROJECT_NAME}) to avoid a doubled
# include/${PROJECT_NAME}/${PROJECT_NAME}/ path; the INSTALL_INTERFACE above is
# `include` to match.
install(DIRECTORY include/ DESTINATION include)
install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

# The `import_geotiff` CLI: import a GeoTIFF into a store layer's fused surface.
add_executable(import_geotiff src/import_geotiff_main.cpp)
target_link_libraries(import_geotiff ${PROJECT_NAME})

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
# nlohmann_json and GDAL are PRIVATE link deps, but a static library propagates them
# into the export's INTERFACE_LINK_LIBRARIES as $<LINK_ONLY:...>, so a consumer that
# just find_package(marine_bathymetry_store) + links it must still resolve those
# targets at generate time. Export them so consumers don't each have to re-find them
# (#203; cube_bathymetry#57 hit this).
ament_export_dependencies(
  marine_autonomy marine_tiled_raster_store geographic_msgs nlohmann_json GDAL)

install(TARGETS import_geotiff RUNTIME DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_store test/test_store.cpp)
  target_link_libraries(test_store ${PROJECT_NAME})

  ament_add_gtest(test_query test/test_query.cpp)
  target_link_libraries(test_query ${PROJECT_NAME})

  ament_add_gtest(test_tile_io test/test_tile_io.cpp)
  # nlohmann_json is used directly in the test (registry tamper fixtures);
  # link it explicitly since it is PRIVATE in the library target.
  target_link_libraries(test_tile_io ${PROJECT_NAME} nlohmann_json::nlohmann_json)

  # The GeoTIFF importer test writes fixture rasters with GDAL directly, so it
  # links GDAL (PRIVATE in the library target).
  ament_add_gtest(test_geotiff_import test/test_geotiff_import.cpp)
  target_link_libraries(test_geotiff_import ${PROJECT_NAME} ${GDAL_LIBRARIES})
  target_include_directories(test_geotiff_import PRIVATE ${GDAL_INCLUDE_DIRS})
endif()

ament_package()
