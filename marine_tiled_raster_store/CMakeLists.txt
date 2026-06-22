cmake_minimum_required(VERSION 3.8)
project(marine_tiled_raster_store)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(marine_autonomy REQUIRED)
find_package(GDAL CONFIG REQUIRED)

add_library(${PROJECT_NAME}
  src/tile_io.cpp
)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Build with position-independent code so this (static) library can be linked
# into SHARED consumers transitively — the bathymetry_layer Nav2 costmap plugin
# (#164) is a shared object that links marine_bathymetry_store, which wraps this
# package's TiledRasterTile. Without PIC the plugin link fails (R_X86_64_PC32).
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
# marine_autonomy (GGGS) is in the public headers; GDAL is used only inside
# tile_io.cpp (the templates are explicitly instantiated there), so it is
# PRIVATE. It is still exported below because this is a static library — CMake
# propagates the private link to consumers as $<LINK_ONLY:GDAL::GDAL>, so they
# must be able to resolve the GDAL target via find_package(${PROJECT_NAME}).
target_link_libraries(${PROJECT_NAME} PUBLIC
  marine_autonomy::marine_autonomy
)
target_link_libraries(${PROJECT_NAME} PRIVATE
  GDAL::GDAL
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
ament_export_dependencies(marine_autonomy GDAL)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_tile_io test/test_tile_io.cpp)
  target_link_libraries(test_tile_io ${PROJECT_NAME})
endif()

ament_package()
