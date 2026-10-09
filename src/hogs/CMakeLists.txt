add_library(hogs SHARED hogs.c hogs.def)
target_compile_definitions(hogs PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
target_link_libraries(hogs PRIVATE winmm)
