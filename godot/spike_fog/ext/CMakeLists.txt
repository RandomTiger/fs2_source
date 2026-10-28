# Fog spike extension (S-1's extension plus fog). cmake -S godot/spike_fog/ext -B <build dir> -G "Visual Studio 17 2022" -A x64 -DGODOT_CPP_DIR=C:/dev/Tools/godot-cpp
cmake_minimum_required(VERSION 3.20)
project(fogext CXX)
set(GODOT_CPP_DIR "C:/dev/Tools/godot-cpp" CACHE PATH "")
set(GODOTCPP_TARGET template_debug CACHE STRING "")
set(GODOTCPP_API_VERSION 4.7 CACHE STRING "")
add_subdirectory(${GODOT_CPP_DIR} godot-cpp)
add_library(fogext SHARED fogext.cpp)
target_include_directories(fogext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../../../fs2game)
target_compile_definitions(fogext PRIVATE NOMINMAX)
target_link_libraries(fogext PRIVATE godot-cpp)
set_target_properties(fogext PROPERTIES CXX_STANDARD 17 OUTPUT_NAME "fogext"
	RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/proj/bin>"
	LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/proj/bin>")
