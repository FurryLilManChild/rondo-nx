cmake_minimum_required(VERSION 3.21)
project(rondo_nx C CXX)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Generated translated code (tools/xlat/gen.py output). Never committed.
set(RONDO_GENERATED_DIR "${CMAKE_SOURCE_DIR}/out/generated" CACHE PATH "Folder with xlat_*.cpp and xlat_table.inc")
if(NOT EXISTS "${RONDO_GENERATED_DIR}/xlat_table.inc")
  message(FATAL_ERROR "No generated code in ${RONDO_GENERATED_DIR}. Run tools/xlat/gen.py first (build_pc.ps1 does this).")
endif()
file(GLOB RONDO_GENERATED_SRC CONFIGURE_DEPENDS "${RONDO_GENERATED_DIR}/xlat_*.cpp")

# SDL3: a prebuilt package (VC for MSVC/clang-cl, mingw for MinGW), unless SDL3_DIR is given.
if(NOT SDL3_DIR)
  include(FetchContent)
  if(MINGW)
    FetchContent_Declare(sdl3_pkg
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.24/SDL3-devel-3.2.24-mingw.tar.gz)
  else()
    FetchContent_Declare(sdl3_pkg
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.24/SDL3-devel-3.2.24-VC.zip)
  endif()
  FetchContent_MakeAvailable(sdl3_pkg)
  set(SDL3_DIR "${sdl3_pkg_SOURCE_DIR}/cmake")
endif()
find_package(SDL3 REQUIRED CONFIG)

# Geargrafx core (GPL-3.0), patched for translated code.
set(GG "${CMAKE_SOURCE_DIR}/third_party/geargrafx")
set(GG_CORE
  geargrafx_core adpcm audio arcade_card_mapper cdrom_audio cdrom_chd_file_adapter cdrom_chd_image
  cdrom_cuebin_image cdrom_mmi_image media_file media_file_native media_file_slice mmi_archive
  ogg_vorbis_decoder cdrom_image cdrom_media cdrom huc6202 huc6260 huc6270 huc6280 rondo_trace
  huc6280_functors huc6280_opcodes huc6280_psg input laseractive mapper mb128 media memory
  scsi_controller sf2_mapper trace_logger profiler turbolink vgm_recorder)
set(GG_SRC)
foreach(f ${GG_CORE})
  list(APPEND GG_SRC "${GG}/src/${f}.cpp")
endforeach()
set(GG_DEPS_SRC
  ${GG}/dependencies/libchdr/src/libchdr_bitstream.c
  ${GG}/dependencies/libchdr/src/libchdr_cdrom.c
  ${GG}/dependencies/libchdr/src/libchdr_chd.c
  ${GG}/dependencies/libchdr/src/libchdr_flac.c
  ${GG}/dependencies/libchdr/src/libchdr_huffman.c
  ${GG}/dependencies/lzma/src/LzFind.c
  ${GG}/dependencies/lzma/src/LzmaEnc.c
  ${GG}/dependencies/lzma/src/LzmaDec.c
  ${GG}/dependencies/lzma/src/CpuArch.c
  ${GG}/dependencies/minivorbis/minivorbis.c
  ${GG}/dependencies/miniz/miniz.c
  ${GG}/dependencies/qon/qon.cpp)

add_library(geargrafx_core STATIC ${GG_SRC} ${GG_DEPS_SRC})
target_include_directories(geargrafx_core PUBLIC
  ${GG}/src
  ${GG}/dependencies/libchdr/include
  ${GG}/dependencies/lzma/include
  ${GG}/dependencies/miniz
  ${GG}/dependencies/minivorbis
  ${GG}/dependencies/zstd
  ${GG}/dependencies/json
  ${GG}/dependencies/qon)
target_compile_definitions(geargrafx_core PUBLIC Z7_ST ZSTD_DISABLE_ASM NDEBUG GG_DISABLE_DISASSEMBLER GG_DISABLE_VGMRECORDER EMULATOR_BUILD="rondo-nx")
if(MSVC)
  target_compile_definitions(geargrafx_core PUBLIC _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
  target_compile_options(geargrafx_core PRIVATE /W0)
else()
  target_compile_options(geargrafx_core PRIVATE -w)
endif()

# Translated game code: big and simple, so build it with light optimization.
add_library(rondo_xlat STATIC runtime/rondo_xlat.cpp ${RONDO_GENERATED_SRC})
target_include_directories(rondo_xlat PUBLIC runtime ${RONDO_GENERATED_DIR})
target_link_libraries(rondo_xlat PUBLIC geargrafx_core)
if(MSVC)
  target_compile_options(rondo_xlat PRIVATE /W0 /O1 /bigobj)
else()
  target_compile_options(rondo_xlat PRIVATE -w -O1)
endif()

add_executable(rondo-nx WIN32 app/main.cpp)
target_link_libraries(rondo-nx PRIVATE rondo_xlat SDL3::SDL3)
if(MINGW)
  target_link_options(rondo-nx PRIVATE -static-libgcc -static-libstdc++)
endif()

# Put SDL3.dll next to the exe.
if(WIN32)
  add_custom_command(TARGET rondo-nx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:rondo-nx>)
endif()

# Lockstep checker (console): interpreter vs translated code, compared every frame.
add_executable(rondo-lockstep tools/lockstep/lockstep.cpp)
target_link_libraries(rondo-lockstep PRIVATE rondo_xlat)
