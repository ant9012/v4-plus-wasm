

message(STATUS "Configuring for WebAssembly (Emscripten)")

# Disable PC features and Hardware OpenGL (Force SDL2 Software Renderer)
set(RETRO_NETWORKING OFF CACHE BOOL "Disabled for Web" FORCE)
set(RETRO_USE_DISCORD OFF CACHE BOOL "Disabled for Web" FORCE)
set(RETRO_USE_STEAM OFF CACHE BOOL "Disabled for Web" FORCE)
set(RETRO_USE_EOS OFF CACHE BOOL "Disabled for Web" FORCE)
set(RETRO_UPDATE_CHECKER OFF CACHE BOOL "Disabled for Web" FORCE)
set(RETRO_USE_HW_RENDER OFF CACHE BOOL "Force SDL2 Canvas Renderer" FORCE)

# Audio/Video dependencies
set(COMPILE_OGG OFF)
set(COMPILE_VORBIS OFF)
set(COMPILE_THEORA ON)
set(DEP_PATH "mac") 

# Tell CMake to build the RetroEngine executable using the source files
add_executable(RetroEngine ${RETRO_FILES})

# Compiler Flags formatted as a clean list to prevent string-splitting errors
set(EMSCRIPTEN_FLAGS
    -O3
    -fsigned-char
    -sUSE_SDL=2
    -sUSE_SDL_IMAGE=1
    -sUSE_OGG=1
    -sUSE_VORBIS=1
    -sEMULATE_FUNCTION_POINTER_CASTS=1
    -sUSE_PTHREADS=1
    -pthread
)

# Apply compiler flags cleanly
target_compile_options(RetroEngine PRIVATE ${EMSCRIPTEN_FLAGS})

# Define linker flags (Notice the preload-file flag is completely removed)
set(emsc_link_options
    -lidbfs.js
    -lm
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=536870912
    -sMAXIMUM_MEMORY=1073741824
    -sPTHREAD_POOL_SIZE=4
    -sASSERTIONS=0
    -sFORCE_FILESYSTEM=1
    -sINVOKE_RUN=0                  # <--- STOPS AUTO-BOOT (Waits for your JS Wrapper)
    -sEXPORTED_RUNTIME_METHODS=['FS','callMain','cwrap','ccall','IDBFS']
    -sEXPORTED_FUNCTIONS=['_main','_RSDK_Initialize','_RSDK_Configure','_Mono_WakeAudio']
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
)

# Apply linker flags (combining the base flags with the linker-specific ones)
target_link_options(RetroEngine PRIVATE ${EMSCRIPTEN_FLAGS} ${emsc_link_options})

# Force the output to be a .js file (which Emscripten uses to also generate the .wasm)
set_target_properties(RetroEngine PROPERTIES SUFFIX ".js")
set(RETRO_OUTPUT_NAME "RSDKv4" CACHE STRING "Web Output" FORCE)