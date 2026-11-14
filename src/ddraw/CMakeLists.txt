# ddraw.dll, the one DLL players get: the game's DirectDraw 7 / Direct3D 7 on Direct3D 11 (replaces
# dgVoodoo2), and the game fixes of src/hogs, which start only in an exe that imports CallDLL from it.
add_library(hogsdraw SHARED main.cpp ddraw7.cpp surface.cpp d3d7.cpp gpu.cpp capture.cpp ddraw.def
            ../hogs/hogs.c ../hogs/frames.c ../hogs/gamepad.c ../hogs/net.c)
set_target_properties(hogsdraw PROPERTIES OUTPUT_NAME ddraw CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(hogsdraw PRIVATE _CRT_SECURE_NO_WARNINGS)
# A plain import, not a delay-load: the fixes call winmm inside DllMain, so Windows must start the
# game's winmm.dll (ogg-winmm, the CD music) before this DLL.
target_link_libraries(hogsdraw PRIVATE winmm ws2_32 winhttp)
