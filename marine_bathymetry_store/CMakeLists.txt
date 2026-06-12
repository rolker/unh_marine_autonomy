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
find_package(geographic_msgs REQUIRED)
find_package(GDAL CONFIG REQUIRED)

add_library(${PROJECT_NAME}
  src/bathymetry_store.cpp
  src/geotiff_import.cpp
  src/query.cpp
  src/tile_io.cpp
)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
# marine_autonomy + geographic_msgs are in the public headers (GGGS / GeoPoint);
# GDAL is used only inside tile_io.cpp, so it is PRIVATE. It is still exported as
# a dependency below because this is a static library — CMake propagates the
# private link to consumers as $<LINK_ONLY:GDAL::GDAL>, so they must be able to
# resolve the GDAL target via find_package(marine_bathymetry_store).
target_link_libraries(${PROJECT_NAME} PUBLIC
  marine_autonomy::marine_autonomy
  ${geographic_msgs_TARGETS}
)
target_link_libraries(${PROJECT_NAME} PRIVATE
  GDAL::GDAL
)

# Headers are laid out as include/${PROJECT_NAME}/*.hpp, so install the include/
# tree to `include` (not include/${PROJECT_NAME}) to avoid a doubled
# include/${PROJECT_NAME}/${PROJECT_NAME}/ path; the INSTALL_INTERFACE above is
# `include` to match.
add_executable(import_geotiff src/import_geotiff_main.cpp)
target_link_libraries(import_geotiff ${PROJECT_NAME})

install(DIRECTORY include/ DESTINATION include)
install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(TARGETS import_geotiff
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(marine_autonomy geographic_msgs GDAL)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_store test/test_store.cpp)
  target_link_libraries(test_store ${PROJECT_NAME})

  ament_add_gtest(test_query test/test_query.cpp)
  target_link_libraries(test_query ${PROJECT_NAME})

  ament_add_gtest(test_tile_io test/test_tile_io.cpp)
  target_link_libraries(test_tile_io ${PROJECT_NAME})

  # The test writes synthetic GeoTIFFs itself, so it links GDAL directly
  # (the library keeps GDAL private).
  ament_add_gtest(test_geotiff_import test/test_geotiff_import.cpp)
  target_link_libraries(test_geotiff_import ${PROJECT_NAME} GDAL::GDAL)
endif()

ament_package()
