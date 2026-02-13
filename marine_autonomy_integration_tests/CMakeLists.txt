cmake_minimum_required(VERSION 3.5.1)

project(marine_autonomy_integration_tests)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)

ament_python_install_package(${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(launch_testing_ament_cmake REQUIRED)
  install(PROGRAMS
    marine_autonomy_integration_tests/mock_navigator.py
    DESTINATION lib/${PROJECT_NAME}
  )
  add_launch_test(test/test_mission_command_flow.py)
endif()

ament_package()
