cmake_minimum_required(VERSION 3.23)
# Based on the Claude-86 boilerplate (../Claude-86/CMakeLists.txt):
# src/core is its generic HTTPS/TLS engine, everything else is MacPlayer.
project(MacPlayer)

set(CMAKE_C_STANDARD 99)
enable_language(ASM)

# No -m68020 here: these flags also reach the link, and with it GCC picks
# Retro68's m68020 runtime multilib, which is built for a 68881 FPU (a
# build with it crashed in Basilisk, and a Classic II or LC has no FPU).
# Hot files get 68020 instructions per file below, as the TLS math does.
SET(CMAKE_C_FLAGS   "-Os")
SET(CMAKE_CXX_FLAGS "-Os -fno-exceptions -fno-rtti")

SET(SSL_ENABLED ON)

# "MacPlayer Log": every network step, with ticks, written next to the app.
# Off for normal builds; turn on with cmake -DMACPLAYER_LOG=ON to measure.
option(MACPLAYER_LOG "Write MacPlayer Log next to the application" OFF)
if(MACPLAYER_LOG)
    add_definitions(-DMACPLAYER_LOG)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/net)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core/helpers)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/api)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/ui)

add_library(MacHTTP STATIC
        src/core/net/HttpClient.cpp
        src/core/net/HttpResponse.cpp
        src/core/net/Uri.cpp
        src/core/net/RootCert.cpp
        src/core/net/http_parser.c
        src/core/net/yuarel.c
        src/core/net/gason.cpp
        src/core/net/net_sockets_mac.c
        src/core/net/uECC.c
        # 68020 assembly for the P-256 field math (Claude-86 OPT #5, #6, #6b);
        # unused_* files are reference only and never built
        src/core/net/vli_mult_comba_m68k.s
        src/core/net/vli_square_comba_m68k.s
        src/core/net/solinas_p256_m68k.s
)

set(UECC_FLAGS "-m68020 -O3 \
 -Dasm_mult=1 \
 -Dasm_square=1 \
 -Dasm_solinas_p256=1 \
 -DuECC_OPTIMIZATION_LEVEL=2 \
 -DuECC_SQUARE_FUNC=1 \
 -DuECC_WORD_SIZE=4 \
 -DuECC_SUPPORTS_secp256r1=1 \
 -DuECC_SUPPORTS_secp160r1=0 \
 -DuECC_SUPPORTS_secp192r1=0 \
 -DuECC_SUPPORTS_secp224r1=0 \
 -DuECC_SUPPORTS_secp256k1=0 \
 -DuECC_VLI_NATIVE_LITTLE_ENDIAN=0")
set_source_files_properties(src/core/net/uECC.c src/core/net/uecc_alt.c PROPERTIES COMPILE_FLAGS "${UECC_FLAGS}")
set_source_files_properties(src/core/net/vli_mult_comba_m68k.s src/core/net/vli_square_comba_m68k.s src/core/net/solinas_p256_m68k.s PROPERTIES COMPILE_FLAGS "-m68020")

IF(${SSL_ENABLED})
    ADD_DEFINITIONS(-DSSL_ENABLED)
    # Pre-built 68k mbedTLS and MacTCPHelper, as shipped with Claude-86
    include_directories(${CMAKE_SOURCE_DIR}/src/core/lib/mbedtls/include)
    include_directories(${CMAKE_SOURCE_DIR}/src/core/lib)
    target_link_libraries(MacHTTP ${CMAKE_SOURCE_DIR}/src/core/lib/mbedtls/libmbedtls.a
                                  ${CMAKE_SOURCE_DIR}/src/core/lib/mbedtls/libmbedx509.a
                                  ${CMAKE_SOURCE_DIR}/src/core/lib/mbedtls/libmbedcrypto.a
                                  ${CMAKE_SOURCE_DIR}/src/core/lib/mactcp/libMacTCPHelper.a)
ELSE()
    target_link_libraries(MacHTTP ${CMAKE_SOURCE_DIR}/src/core/lib/mactcp/libMacTCPHelper.a)
ENDIF()

set_target_properties(MacHTTP PROPERTIES COMPILE_OPTIONS -ffunction-sections)

# Application sources: everything in src/ except the core/net library files
file(GLOB_RECURSE APP_SOURCES
    src/*.cpp
    src/*.c
)
list(FILTER APP_SOURCES EXCLUDE REGEX ".*/src/core/net/.*")
list(FILTER APP_SOURCES EXCLUDE REGEX ".*/\\.[^/]*$")  # hidden files (editor temp copies)
list(APPEND APP_SOURCES
    src/core/net/fast_alloc.c
    # Entropy for the TLS keys: Time Manager jitter (see the file's header)
    src/core/net/mac_entropy.c
    src/core/net/uecc_alt.c
    # Saturated 8x32 X25519 on the Comba asm (Claude-86 OPT #8b)
    src/core/net/curve25519_sat.c
)

set_source_files_properties(src/core/net/curve25519_sat.c PROPERTIES COMPILE_FLAGS "-m68020 -O3 -Dsat_vli_asm=1")

# Album cover: JPEG decode and cover rendering are full of 32-bit multiplies
# and divides, native on a 68020 instead of library calls, and worth -O2
# (the decode is seconds on an SE/30). -msoft-float keeps FPU instructions
# out (stb_image has float code for unused formats).
set_source_files_properties(src/stb_image_impl.cpp src/CoverArt.cpp PROPERTIES COMPILE_FLAGS "-m68020 -msoft-float -O2")

add_application(MacPlayer
        ${APP_SOURCES}
        src/resources.r
        CREATOR "MPLY"
)

target_link_libraries(MacPlayer MacHTTP)

set_target_properties(MacPlayer PROPERTIES
    LINK_FLAGS "-Wl,--allow-multiple-definition -Wl,-gc-sections -Wl,-Map,MacPlayer.map")

# Touch BuildStamp.cpp before every build so the log names the exact build
add_custom_target(build_stamp_touch
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_SOURCE_DIR}/src/BuildStamp.cpp
    COMMENT "Updating build timestamp"
)
add_dependencies(MacPlayer build_stamp_touch)

# Plain `make` produces MacPlayer.APPL / .bin / .dsk
add_custom_target(build_appl ALL DEPENDS MacPlayer_APPL)
