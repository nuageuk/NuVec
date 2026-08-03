cmake_minimum_required(VERSION 3.15)
project(NuVec C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

add_executable(NuVec src/main.c src/memory.c src/cpu.c src/display.c src/via.c)

# SDL2main must come before SDL2 in the link line: on Windows, SDL.h #defines
# main to SDL_main, and SDL2main's real entry point calls back into it.
target_link_libraries(NuVec PRIVATE SDL2::SDL2main SDL2::SDL2)