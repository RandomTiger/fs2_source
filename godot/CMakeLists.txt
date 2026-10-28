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

# Where the DLL goes. A build for L0 only (not for running) passes another folder so it never replaces the DLL a gate may be using.
set(FS2_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin" CACHE PATH "output folder of fs2godot.dll")

add_library(fs2godot SHARED src/register_types.cpp src/fs2_node.cpp src/fs2_node.h src/host_hooks_godot.cpp src/host_hooks_godot.h
	src/render_sink_godot.cpp src/render_sink_godot.h src/fs2_screen.cpp src/fs2_screen.h src/sink_selftest.cpp src/sink_selftest.h ../fs2game/render_sink.h)
target_include_directories(fs2godot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../fs2game ${CMAKE_CURRENT_SOURCE_DIR}/render_sink)
target_link_libraries(fs2godot PRIVATE godot-cpp)
set_target_properties(fs2godot PROPERTIES
	CXX_STANDARD 17
	OUTPUT_NAME "fs2godot"
	RUNTIME_OUTPUT_DIRECTORY "$<1:${FS2_BIN_DIR}>"
	LIBRARY_OUTPUT_DIRECTORY "$<1:${FS2_BIN_DIR}>"
	PDB_OUTPUT_DIRECTORY "$<1:${FS2_BIN_DIR}>")

# The render sink sources are held at zero warnings (L0)
set_source_files_properties(src/render_sink_godot.cpp src/fs2_screen.cpp src/sink_selftest.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX;/external:anglebrackets;/external:W0")
