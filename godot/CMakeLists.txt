# The Godot wrapper extension (G1/G2). Build (from godot/):
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DGODOT_CPP_DIR=C:/dev/Tools/godot-cpp
#   cmake --build build --config Debug      (template_debug flavour; the stock editor binary loads this one)
# See godot/README.md for the pinned versions.
cmake_minimum_required(VERSION 3.20)
project(fs2godot CXX)

set(GODOT_CPP_DIR "C:/dev/Tools/godot-cpp" CACHE PATH "godot-cpp checkout (pinned in README.md)")
set(GODOTCPP_TARGET template_debug CACHE STRING "")
set(GODOTCPP_API_VERSION 4.7 CACHE STRING "")
add_subdirectory(${GODOT_CPP_DIR} godot-cpp)

add_library(fs2godot SHARED src/register_types.cpp src/fs2_node.cpp src/fs2_node.h)
target_include_directories(fs2godot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../fs2game)
target_link_libraries(fs2godot PRIVATE godot-cpp)
set_target_properties(fs2godot PROPERTIES
	CXX_STANDARD 17
	OUTPUT_NAME "fs2godot"
	RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/bin>"
	LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/bin>"
	PDB_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/bin>")
