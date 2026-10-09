# hogsdraw: the game's DirectDraw 7 / Direct3D 7 on Direct3D 11 (replaces dgVoodoo2).
add_library(hogsdraw SHARED main.cpp ddraw7.cpp surface.cpp d3d7.cpp gpu.cpp capture.cpp ddraw.def)
set_target_properties(hogsdraw PROPERTIES OUTPUT_NAME ddraw CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(hogsdraw PRIVATE _CRT_SECURE_NO_WARNINGS)
