# Drive Test XL: build with Retro68 (68000 code: Mac Plus ROM and up, so MacWorks Plus and Plus II).
#   bash build.sh
cmake_minimum_required(VERSION 3.10)
project(DriveTestXL C)
add_application(DriveTestXL
    src/main.c
    src/xlproto.c
    src/xl.r
    TYPE "APPL"
    CREATOR "DTXL"
    )
target_include_directories(DriveTestXL PRIVATE src)
set_target_properties(DriveTestXL PROPERTIES COMPILE_OPTIONS "-ffunction-sections;-Os;-Wall;-Wno-multichar;-Wno-pointer-sign;-Wno-array-bounds")
set_target_properties(DriveTestXL PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
