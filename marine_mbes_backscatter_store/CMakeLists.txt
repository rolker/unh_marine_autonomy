cmake_minimum_required(VERSION 3.8)
project(marine_mbes_backscatter_store)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(marine_autonomy REQUIRED)
# GDAL is not used directly here — tile_io delegates all GeoTIFF I/O to
# marine_tiled_raster_store, which carries (and re-exports) the GDAL dependency.
# The float value tile uses the GDT_Float32 instantiation added there (#194).
find_package(marine_tiled_raster_store REQUIRED)
# Modality-agnostic backscatter core (provenance schema + writeRegistry, future
# GeoCoder). The MBES store depends on it per ADR-0007 D6 but keeps its own
# multi-source SourceRegistry (writeRegistry is write-only / single-source).
find_package(marine_backscatter REQUIRED)
find_package(geographic_msgs REQUIRED)
# nlohmann_json backs the source registry (registry.json sidecar).
find_package(nlohmann_json REQUIRED)

add_library(${PROJECT_NAME}
  src/mbes_store.cpp
  src/query.cpp
  src/registry.cpp
  src/tile_io.cpp
)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
# marine_autonomy + geographic_msgs are in the public headers (GGGS / GeoPoint);
# marine_tiled_raster_store is public too — MbesTile wraps its TiledRasterTile<>
# rasters. GDAL arrives transitively through that package (private to its
# tile_io.cpp); this package never references GDAL.
target_link_libraries(${PROJECT_NAME} PUBLIC
  marine_autonomy::marine_autonomy
  marine_tiled_raster_store::marine_tiled_raster_store
  ${geographic_msgs_TARGETS}
)
# marine_backscatter is the modality-agnostic backscatter core this store depends
# on (ADR-0007 D6: shared provenance schema / writeRegistry, future GeoCoder); it
# is used in the implementation, not the public interface. nlohmann_json
# (registry.cpp only) is header-only and likewise kept out of the public
# interface. Both linked PRIVATE via the namespaced imported target so the
# library location resolves for downstream test executables.
target_link_libraries(${PROJECT_NAME} PRIVATE
  marine_backscatter::marine_backscatter
  nlohmann_json::nlohmann_json
)

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

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(
  marine_autonomy marine_tiled_raster_store marine_backscatter geographic_msgs)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_store test/test_store.cpp)
  target_link_libraries(test_store ${PROJECT_NAME})

  ament_add_gtest(test_query test/test_query.cpp)
  target_link_libraries(test_query ${PROJECT_NAME})

  ament_add_gtest(test_tile_io test/test_tile_io.cpp)
  # nlohmann_json is used directly in the test (registry round-trip fixtures) and
  # marine_tiled_raster_store for the companion-mismatch fixtures; link them
  # explicitly since nlohmann_json is PRIVATE in the library target.
  target_link_libraries(test_tile_io ${PROJECT_NAME}
    marine_tiled_raster_store::marine_tiled_raster_store
    nlohmann_json::nlohmann_json)
endif()

ament_package()
