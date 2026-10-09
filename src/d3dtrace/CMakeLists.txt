# Diagnostic proxy for the game's renderer (not part of the player package; build it with --target d3dtrace).
add_library(d3dtrace SHARED EXCLUDE_FROM_ALL d3dtrace.c)
set_target_properties(d3dtrace PROPERTIES OUTPUT_NAME _d3d)
target_compile_definitions(d3dtrace PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
