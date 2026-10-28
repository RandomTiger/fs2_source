# S-1 spike extension. cmake -S godot/spike_s1/ext -B godot/build/s1ext -G "Visual Studio 17 2022" -A x64 -DGODOT_CPP_DIR=C:/dev/Tools/godot-cpp
cmake_minimum_required(VERSION 3.20)
project(s1ext CXX)
set(GODOT_CPP_DIR "C:/dev/Tools/godot-cpp" CACHE PATH "")
set(GODOTCPP_TARGET template_debug CACHE STRING "")
set(GODOTCPP_API_VERSION 4.7 CACHE STRING "")
add_subdirectory(${GODOT_CPP_DIR} godot-cpp)
add_library(s1ext SHARED s1ext.cpp)
target_include_directories(s1ext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../fs2game)
target_link_libraries(s1ext PRIVATE godot-cpp)
set_target_properties(s1ext PROPERTIES CXX_STANDARD 17 OUTPUT_NAME "s1ext"
	RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/proj/bin>"
	LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/proj/bin>")
