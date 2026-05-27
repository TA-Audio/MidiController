cmake_minimum_required(VERSION 3.14)
project(MidiControllerTests CXX)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(midi_controller_tests test_logic.cpp)
target_include_directories(midi_controller_tests PRIVATE ${CMAKE_SOURCE_DIR}/../midicontroller)
target_link_libraries(midi_controller_tests GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(midi_controller_tests)
