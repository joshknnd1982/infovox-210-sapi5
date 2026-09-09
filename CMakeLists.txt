cmake_minimum_required(VERSION 3.15)
project(InfovoxSAPI VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BUILD_X64 TRUE)
    message(STATUS "Building x64")
else()
    set(BUILD_X64 FALSE)
    message(STATUS "Building x86")
endif()

set(COMMON_DEFS _CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE
                WIN32_LEAN_AND_MEAN)

set(UNICORN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unicorn)

function(infovox_target_defaults tgt)
    target_compile_definitions(${tgt} PRIVATE ${COMMON_DEFS})
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W3 /MP)
        set_property(TARGET ${tgt} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# The SAPI engine.  Built for both architectures; neither loads the emulator,
# they both talk to the 64-bit worker.
# ---------------------------------------------------------------------------
add_library(InfovoxSAPI SHARED
    src/sapi/sapi_main.cpp
    src/sapi/com.cpp
    src/sapi/registry.cpp
    src/sapi/voice_registry.cpp
    src/sapi/ISpTTSEngineImpl.cpp
    src/sapi/pipe_client.cpp
    src/sapi/infovox_sapi.def
)
target_include_directories(InfovoxSAPI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/sapi)
target_link_libraries(InfovoxSAPI PRIVATE ole32 oleaut32 advapi32 shlwapi shell32)
infovox_target_defaults(InfovoxSAPI)

# ---------------------------------------------------------------------------
# Everything that emulates classic Mac OS is 64-bit only: that is where Unicorn
# lives, and one worker serves both SAPI architectures.
# ---------------------------------------------------------------------------
if(BUILD_X64)
    add_library(infovox_ppc STATIC
        src/ppc/pef.cpp
        src/ppc/ivpack.cpp
        src/ppc/macos.cpp
        src/ppc/infovox.cpp
    )
    target_include_directories(infovox_ppc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${UNICORN_DIR}/include
    )
    target_link_libraries(infovox_ppc PUBLIC ${UNICORN_DIR}/lib/x64/unicorn_import.lib)
    infovox_target_defaults(infovox_ppc)

    add_executable(infovox_host src/host/infovox_host.cpp)
    target_link_libraries(infovox_host PRIVATE infovox_ppc)
    infovox_target_defaults(infovox_host)

    add_executable(infovox_probe tools/infovox_probe.cpp)
    target_link_libraries(infovox_probe PRIVATE infovox_ppc)
    infovox_target_defaults(infovox_probe)

    # unicorn.dll brings its own CRT; stage it and the engine packs so the build
    # tree runs directly.
    add_custom_command(TARGET infovox_host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${UNICORN_DIR}/lib/x64/unicorn.dll $<TARGET_FILE_DIR:infovox_host>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/engine $<TARGET_FILE_DIR:infovox_host>/engine
        COMMENT "Staging unicorn.dll and the engine packs")
endif()

# ---------------------------------------------------------------------------
# The settings dialog and the diagnostics report ship with the product.  Both
# are 32-bit only: they are ordinary applications, and one build runs wherever
# the engine does.
# ---------------------------------------------------------------------------
if(NOT BUILD_X64)
    add_executable(InfovoxConfig WIN32
        tools/infovox_config.cpp
        tools/infovox_config.rc
        src/sapi/pipe_client.cpp
        src/sapi/voice_registry.cpp
    )
    target_include_directories(InfovoxConfig PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sapi
        ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    target_link_libraries(InfovoxConfig PRIVATE
        comctl32 ole32 oleaut32 advapi32 user32 gdi32 shlwapi sapi)
    infovox_target_defaults(InfovoxConfig)

    add_executable(InfovoxDiagnostics
        tools/infovox_diag.cpp
        src/sapi/pipe_client.cpp
        src/sapi/voice_registry.cpp
    )
    target_include_directories(InfovoxDiagnostics PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sapi)
    target_link_libraries(InfovoxDiagnostics PRIVATE
        ole32 oleaut32 advapi32 shlwapi sapi)
    infovox_target_defaults(InfovoxDiagnostics)
endif()
