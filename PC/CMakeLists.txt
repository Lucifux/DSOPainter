cmake_minimum_required(VERSION 3.10.0)
project(DSOPainter VERSION 0.1.0 LANGUAGES C)

add_executable(DSOPainter
    main.c
)

target_link_libraries(DSOPainter PRIVATE
    raylib
    opengl32
    gdi32
    winmm
    ws2_32
)