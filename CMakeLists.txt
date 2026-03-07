cmake_minimum_required(VERSION 3.20)
project(WarehouseRobotSim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Paths to pre-built libraries ───────────────────────────────────────────
set(MUJOCO_ROOT   "${CMAKE_SOURCE_DIR}/thirdparty")
set(RAYLIB_ROOT   "${CMAKE_SOURCE_DIR}/thirdparty/raylib-5.5_win64_msvc16")
set(PQ_ROOT       "C:/Program Files/PostgreSQL/18")

# ─── MuJoCo ─────────────────────────────────────────────────────────────────
add_library(mujoco SHARED IMPORTED)
set_target_properties(mujoco PROPERTIES
    IMPORTED_LOCATION          "${MUJOCO_ROOT}/bin/mujoco.dll"
    IMPORTED_IMPLIB            "${MUJOCO_ROOT}/lib/mujoco.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${MUJOCO_ROOT}/include"
)

# ─── Raylib ─────────────────────────────────────────────────────────────────
add_library(raylib STATIC IMPORTED)
set_target_properties(raylib PROPERTIES
    IMPORTED_LOCATION             "${RAYLIB_ROOT}/lib/raylib.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${RAYLIB_ROOT}/include"
)

# ─── libpq (PostgreSQL 18 client) ────────────────────────────────────────────
add_library(libpq SHARED IMPORTED)
set_target_properties(libpq PROPERTIES
    IMPORTED_LOCATION             "${PQ_ROOT}/bin/libpq.dll"
    IMPORTED_IMPLIB               "${PQ_ROOT}/lib/libpq.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${PQ_ROOT}/include"
)

# ─── Executable ─────────────────────────────────────────────────────────────
add_executable(robot_sim
    src/main.cpp
)

target_include_directories(robot_sim PRIVATE
    src
    "${MUJOCO_ROOT}/include"
    "${RAYLIB_ROOT}/include"
    "${PQ_ROOT}/include"
)

target_link_libraries(robot_sim PRIVATE
    mujoco
    raylib
    libpq
    winmm
    gdi32
    opengl32
    user32
    kernel32
    shell32
    ws2_32          # libpq needs winsock
    secur32         # libpq SSL
)

# Release build flags for speed
target_compile_options(robot_sim PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/O2 /W3 /wd4996>
)

# ─── Copy DLLs to output ─────────────────────────────────────────────────────
add_custom_command(TARGET robot_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MUJOCO_ROOT}/bin/mujoco.dll"
        "$<TARGET_FILE_DIR:robot_sim>"
    COMMENT "Copying mujoco.dll to output"
)
add_custom_command(TARGET robot_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PQ_ROOT}/bin/libpq.dll"
        "${PQ_ROOT}/bin/libssl-3-x64.dll"
        "${PQ_ROOT}/bin/libcrypto-3-x64.dll"
        "${PQ_ROOT}/bin/libintl-9.dll"
        "${PQ_ROOT}/bin/libiconv-2.dll"
        "${PQ_ROOT}/bin/libwinpthread-1.dll"
        "${PQ_ROOT}/bin/zlib1.dll"
        "$<TARGET_FILE_DIR:robot_sim>"
    COMMENT "Copying libpq and dependencies to output"
)

add_custom_command(TARGET robot_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/lighting.vs"
        "${CMAKE_SOURCE_DIR}/src/lighting.fs"
        "$<TARGET_FILE_DIR:robot_sim>"
    COMMENT "Copying GLSL shaders to output"
)

message(STATUS "MuJoCo root : ${MUJOCO_ROOT}")
message(STATUS "Raylib root : ${RAYLIB_ROOT}")
message(STATUS "libpq  root : ${PQ_ROOT}")
