cmake_minimum_required(VERSION 3.20)
project(HogsOfWarFix C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Hogs of War is a 32-bit game: configure with -A Win32")
endif()

# Static CRT, so players do not need a Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(src/hogs)
add_subdirectory(src/d3dtrace)
