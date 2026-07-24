cmake_minimum_required(VERSION 3.8)
project(marine_vertical_datum)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PROJ REQUIRED IMPORTED_TARGET proj)
find_package(yaml-cpp REQUIRED)
# yaml-cpp 0.8 exports the namespaced target; older versions export a bare one.
if(TARGET yaml-cpp::yaml-cpp)
  set(YAML_CPP_TARGET yaml-cpp::yaml-cpp)
else()
  set(YAML_CPP_TARGET yaml-cpp)
endif()

# One library, no ROS dependencies. PROJ is linked PRIVATE (the public header
# is PROJ-free by design); yaml-cpp likewise only backs load_datum_config.
add_library(${PROJECT_NAME} SHARED
  src/datum_config.cpp
  src/vdatum_query.cpp
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::PROJ ${YAML_CPP_TARGET})

# Headers install to plain include/ (not include/${PROJECT_NAME}), matching
# the sibling store packages and avoiding the doubled
# include/${PROJECT_NAME}/${PROJECT_NAME}/ path; the INSTALL_INTERFACE above
# is kept consistent with this.
install(DIRECTORY include/ DESTINATION include)
install(
  TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_datum_config test/test_datum_config.cpp)
  target_link_libraries(test_datum_config ${PROJECT_NAME})

  ament_add_gtest(test_vdatum_query test/test_vdatum_query.cpp)
  target_link_libraries(test_vdatum_query ${PROJECT_NAME})
endif()

ament_package()
