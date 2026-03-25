cmake_minimum_required(VERSION 3.15)
project(DoIP_Core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(DoIP_Core SHARED
    DoIP_Core.cpp
    doip_protocol.cpp
    doip_tcp.cpp
    cdll.h
    doip_protocol.h
    doip_tcp.h
)

target_include_directories(DoIP_Core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(DoIP_Core PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    # DOIP_DEBUG          # 取消注释以启用调试日志
)

# 静态 CRT (/MT) — 与 ZipExtract 保持一致，避免目标机器缺少 vcruntime
set_target_properties(DoIP_Core PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)

target_link_libraries(DoIP_Core PRIVATE
    ws2_32
    kernel32
)
