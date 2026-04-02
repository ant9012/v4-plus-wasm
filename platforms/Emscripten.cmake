cmake_minimum_required(VERSION 3.10)

project(RetroEngine)

message(STATUS "Configuring for WebAssembly (Emscripten) with OpenGL + pthreads")

# ─── Force-disable PC-only features ───
set(RETRO_NETWORKING OFF CACHE BOOL "Disabled for Web" FORCE)
set(RETRO_USE_DISCORD OFF CACHE BOOL "Disabled for Web" FORCE)
set(RETRO_USE_STEAM OFF CACHE BOOL "Disabled for Web" FORCE)
set(RETRO_USE_EOS OFF CACHE BOOL "Disabled for Web" FORCE)
set(RETRO_UPDATE_CHECKER OFF CACHE BOOL "Disabled for Web" FORCE)

# ─── Enable OpenGL renderer ───
set(RETRO_USE_HW_RENDER ON CACHE BOOL "Use OpenGL via WebGL" FORCE)

# ─── Dependencies ───
# Use Emscripten ports for OGG/Vorbis, compile Theora from source
set(COMPILE_OGG OFF)
set(COMPILE_VORBIS OFF)
set(COMPILE_THEORA FALSE) # We handle it manually below
set(DEP_PATH all)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

# ─── Build the executable ───
add_executable(RetroEngine ${RETRO_FILES})

# ─── Build libtheora (DECODE ONLY) from source ───
set(THEORA_DIR dependencies/all/libtheora)

add_library(libtheora STATIC
    # Decode
    ${THEORA_DIR}/lib/apiwrapper.c
    ${THEORA_DIR}/lib/bitpack.c
    ${THEORA_DIR}/lib/decapiwrapper.c
    ${THEORA_DIR}/lib/decinfo.c
    ${THEORA_DIR}/lib/decode.c
    ${THEORA_DIR}/lib/dequant.c
    ${THEORA_DIR}/lib/fragment.c
    ${THEORA_DIR}/lib/huffdec.c
    ${THEORA_DIR}/lib/idct.c
    ${THEORA_DIR}/lib/info.c
    ${THEORA_DIR}/lib/internal.c
    ${THEORA_DIR}/lib/mathops.c
    ${THEORA_DIR}/lib/quant.c
    ${THEORA_DIR}/lib/state.c

    # Encoder stubs (returns errors if encode is called — no real encoder needed)
    ${THEORA_DIR}/lib/encoder_disabled.c
)

# Theora needs OGG headers — use Emscripten's OGG port
target_compile_options(libtheora PRIVATE ${THEORA_FLAGS} -sUSE_OGG=1)
target_link_libraries(libtheora PRIVATE "-sUSE_OGG=1")
target_include_directories(libtheora PRIVATE ${THEORA_DIR}/include)

target_include_directories(RetroEngine PRIVATE ${THEORA_DIR}/include)
target_link_libraries(RetroEngine libtheora)

# ─── Compiler flags ───
set(EMSCRIPTEN_FLAGS
    -O3
    -fsigned-char
    -sUSE_SDL=2
    -sUSE_SDL_IMAGE=1
    -sUSE_OGG=1
    -sUSE_VORBIS=1
    -sUSE_PTHREADS=1
    -sEMULATE_FUNCTION_POINTER_CASTS=1
    -pthread
    -DRETRO_USING_OPENGL=1
    -DRETRO_USE_NETWORKING=0
    -DRSDK_REVISION=3
    -DRSDK_USE_SDL2=1
    -DRETRO_STANDALONE=1
    -DRETRO_USE_MOD_LOADER=1
    -DRETRO_PLATFORM=5
)

target_compile_options(RetroEngine PRIVATE ${EMSCRIPTEN_FLAGS})

set(emsc_link_options
    # Memory — fixed size avoids pthreads+growth slowdown
    -sINITIAL_MEMORY=536870912
    -sMAXIMUM_MEMORY=1073741824
    -sALLOW_MEMORY_GROWTH=1
    -Wno-pthreads-mem-growth

    # Threading
    -sUSE_PTHREADS=1
    -sPTHREAD_POOL_SIZE=4
    -pthread

    # Ports
    -sUSE_SDL=2
    -sUSE_SDL_IMAGE=1
    -sUSE_OGG=1
    -sUSE_VORBIS=1

    # OpenGL via WebGL
    -sLEGACY_GL_EMULATION=1
    -sGL_UNSAFE_OPTS=1
    -sMAX_WEBGL_VERSION=2

    # Filesystem & persistent storage
    -sFORCE_FILESYSTEM=1
    -lidbfs.js

    # Don't auto-run main
    -sINVOKE_RUN=0

    # Exports
    -sEXPORTED_RUNTIME_METHODS=['FS','callMain','cwrap','ccall','IDBFS']
    -sEXPORTED_FUNCTIONS=['_main','_RSDK_Initialize','_RSDK_Configure','_Mono_WakeAudio']

    # Misc
    -sELIMINATE_DUPLICATE_FUNCTIONS=1
    -sASSERTIONS=0
    -lm
    -flto

    # Link theora
    -Wl,--whole-archive
    $<TARGET_FILE:libtheora>
    -Wl,--no-whole-archive
)

target_link_options(RetroEngine PRIVATE ${EMSCRIPTEN_FLAGS} ${emsc_link_options})

# ─── C++17 for mod loader ───
if(RETRO_MOD_LOADER)
    set_target_properties(RetroEngine PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
endif()

# ─── Output settings ───
set(RETRO_OUTPUT_NAME "RSDKv4" CACHE STRING "Web Output" FORCE)

set_target_properties(RetroEngine PROPERTIES
    OUTPUT_NAME "${RETRO_OUTPUT_NAME}"
    SUFFIX ".js"
)
