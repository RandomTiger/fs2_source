# Render sink checks (R1-0..R1-3): the sink header, the recording fake and the batcher, built and tested with no Godot and no game.
# L0 for these (Release and Debug, warnings are errors):
#   cmake -S godot/render_sink -B godot/build/sink_check -G "Visual Studio 17 2022" -A x64
#   cmake --build godot/build/sink_check --config Debug ; cmake --build godot/build/sink_check --config Release
#   godot/build/sink_check/Debug/fs2_sink_check.exe   (and Release)
cmake_minimum_required(VERSION 3.20)
project(fs2sinkcheck CXX)

add_executable(fs2_sink_check sink_check.cpp render_sink.h fake_sink.h selftest_scenes.h ../gr_host/tex_convert.h)
target_compile_features(fs2_sink_check PRIVATE cxx_std_17)
target_compile_options(fs2_sink_check PRIVATE /W4 /WX)
