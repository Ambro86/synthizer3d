cmake_minimum_required(VERSION 3.15.0)

# -- VCPKG TOOLCHAIN: va PRIMA di project()! --
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
elseif(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

project(synthizer VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)


set(BENCHMARK_ENABLE_TESTING OFF)
option(SYZ_INTEGRATING "Set to ON to avoid linking examples etc. when integrating into a language's bindings" OFF)

add_subdirectory(deps)
add_compile_definitions(SYZ_MAJOR=0 SYZ_MINOR=1 SYZ_PATCH=9)

include(CTest)
include(CheckCXXSourceRuns)
enable_testing()

find_package(Threads REQUIRED)

# --- VCPKG Packages: libvorbis, libogg, opus, opusfile, soundtouch, faad2 ---
find_package(Vorbis REQUIRED)
find_package(Ogg REQUIRED)
find_package(Opus REQUIRED)
find_package(OpusFile REQUIRED)
find_package(SoundTouch REQUIRED)

# FAAD2 configuration - cross-platform approach
if(WIN32)
  # On Windows, use direct library linking via vcpkg
  set(FAAD2_FOUND TRUE)
  set(FAAD2_LIBRARIES faad2)
  set(FAAD2_INCLUDE_DIRS "")
else()
  # On Unix/Linux, use pkg-config if available
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(FAAD2 REQUIRED faad2)
  else()
    # Fallback for systems without pkg-config
    find_library(FAAD2_LIBRARIES NAMES faad2 faad REQUIRED)
    find_path(FAAD2_INCLUDE_DIRS NAMES neaacdec.h REQUIRED)
    set(FAAD2_FOUND TRUE)
  endif()
endif()


# Check if op_read_float is available
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES OpusFile::opusfile Opus::opus Ogg::ogg)
check_cxx_source_compiles([[
  extern "C" {
  #include <opusfile.h>
  }
  int main() {
    OggOpusFile* of = nullptr;
    float* pcm = nullptr;
    int result = op_read_float(of, pcm, 0, nullptr);
    return 0;
  }
]] OP_READ_FLOAT_AVAILABLE)

if(OP_READ_FLOAT_AVAILABLE)
  message(STATUS "op_read_float is available")
  add_compile_definitions(OP_READ_FLOAT_AVAILABLE)
else()
  message(STATUS "op_read_float is NOT available, will use op_read with conversion")
endif()

set(SYZ_DEPS
  boost_partial
  concurrentqueue
  cpp11-on-multicore
  dr_libs
  hedley
  miniaudio
  pdqsort
)

if(NOT "${SYZ_INTEGRATING}")
  set(SYZ_TEST_DEPS catch2 benchmark)
endif()

FetchContent_MakeAvailable(${SYZ_DEPS} ${SYZ_TEST_DEPS})
FetchContent_MakeAvailable(wdl)

include_directories(include)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Xclang -fno-caret-diagnostics
    -Wno-deprecated-declarations
    -Wno-logical-op-parentheses
    -Wno-unknown-pragmas
    -Wno-unknown-warning-option
  )
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  add_compile_options(
    /wd4068 /wd5030 /wd4244 /wd4267 /Zc:preprocessor /wd5105 /wd4127
    /experimental:external /external:W0 /external:anglebrackets /wd4702
  )
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wno-unknown-pragmas)
endif()

check_cxx_source_runs([[
  #include <cstdio>
  #include <cstdlib>
  #include <filesystem>
  int main() {
    auto cwd = std::filesystem::current_path();
    std::printf("%s", cwd.c_str());
    return 0;
  }
]] FILESYSTEM_OK)


if(NOT FILESYSTEM_OK)
  message(WARNING "C++17 filesystem not found or not functional on this platform")
else()
  message(STATUS "Found C++17 filesystem support")
endif()

add_library(synthizer_single_file_libs OBJECT
  src/single_file_libs.c
)
set_property(TARGET synthizer_single_file_libs PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(synthizer_single_file_libs dr_libs miniaudio)

set(SYNTHIZER_LIB_TYPE STATIC CACHE STRING "The build type for Synthizer. Either STATIC or SHARED")
add_library(synthizer ${SYNTHIZER_LIB_TYPE}
  src/audio_output.cpp
  src/base_object.cpp
  src/byte_stream.cpp
  src/c_api/unity.cpp
  src/context.cpp
  src/decoding.cpp
  src/error.cpp
  src/event_timeline.cpp
  src/events.cpp
  src/generator.cpp
  src/logging.cpp
  src/memory.cpp
  src/pausable.cpp
  src/property_internals.cpp
  src/routable.cpp
  src/router.cpp
  src/shared_object.cpp
  src/data/arrays.cpp
  src/data/hrtf.cpp
  src/streams/custom_stream.cpp
  src/streams/file.cpp
  src/streams/memory_stream.cpp
  include/synthizer/decoders/ogg.cpp
  include/synthizer/decoders/opus.cpp
  include/synthizer/decoders/aac.cpp
  $<TARGET_OBJECTS:wdl_objlib>
  $<TARGET_OBJECTS:synthizer_single_file_libs>
)

if(MSVC)
  set_source_files_properties(src/decoding.cpp PROPERTIES COMPILE_FLAGS "/W4 /wd4245 /wd4456 /wd4457 /wd4701 /wd4245 /WX-")
endif()
target_compile_features(synthizer PUBLIC cxx_std_17)

# --- Link a tutte le librerie (CMake e .lib) in modo unificato ---
set(SYZ_LIBRARIES
  ${SYZ_DEPS}
  Threads::Threads
  ${CMAKE_DL_LIBS}
)
if(WIN32)
  list(APPEND SYZ_LIBRARIES
    "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/lib/ogg.lib"
    "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/lib/vorbis.lib"
    "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/lib/vorbisfile.lib"
    "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/lib/opus.lib"
    "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/lib/opusfile.lib"
    "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/lib/vorbisenc.lib"
    "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/lib/SoundTouch.lib"
    "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/lib/faad.lib"
  )
elseif(UNIX AND NOT APPLE)
  # Linux-specific linking for static libraries with whole-archive
  # Force include all symbols to ensure op_read_float is available
  if(DEFINED ENV{VCPKG_INSTALLED_PATH})
    # Use vcpkg libraries directly with whole-archive
    list(APPEND SYZ_LIBRARIES
      -Wl,--whole-archive
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libopusfile.a"
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libopus.a"
      -Wl,--no-whole-archive
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libogg.a"
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libvorbis.a"
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libvorbisfile.a"
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libSoundTouch.a"
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libfaad.a"
      m  # Math library
      dl  # Dynamic loading library
    )
  else()
    # Fallback to CMake targets
    list(APPEND SYZ_LIBRARIES
      -Wl,--whole-archive
      OpusFile::opusfile
      Opus::opus
      -Wl,--no-whole-archive
      Ogg::ogg
      Vorbis::vorbis
      Vorbis::vorbisfile
      SoundTouch::SoundTouch
      ${FAAD2_LIBRARIES}
      m  # Math library
      dl  # Dynamic loading library
    )
  endif()
elseif(APPLE)
  # macOS-specific linking for static libraries with force_load
  # Force include all symbols to ensure op_read_float is available
  if(DEFINED ENV{VCPKG_INSTALLED_PATH})
    # Use vcpkg libraries directly with force_load
    list(APPEND SYZ_LIBRARIES
      -force_load "$ENV{VCPKG_INSTALLED_PATH}/lib/libopusfile.a"
      -force_load "$ENV{VCPKG_INSTALLED_PATH}/lib/libopus.a"
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libogg.a"
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libvorbis.a"
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libvorbisfile.a"
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libSoundTouch.a"
      "$ENV{VCPKG_INSTALLED_PATH}/lib/libfaad.a"
      m  # Math library
    )
  else()
    # Fallback to CMake targets
    list(APPEND SYZ_LIBRARIES
      Ogg::ogg
      Opus::opus
      Vorbis::vorbis
      Vorbis::vorbisfile
      OpusFile::opusfile
      SoundTouch::SoundTouch
      ${FAAD2_LIBRARIES}
      m  # Math library
    )
  endif()
endif()
target_link_libraries(synthizer PRIVATE ${SYZ_LIBRARIES})

target_include_directories(synthizer PRIVATE $<TARGET_PROPERTY:wdl_objlib,INTERFACE_INCLUDE_DIRECTORIES>)
if(NOT WIN32 AND FAAD2_INCLUDE_DIRS)
  target_include_directories(synthizer PRIVATE ${FAAD2_INCLUDE_DIRS})
endif()

# Add vcpkg include directories - unified approach for all platforms
if(DEFINED ENV{VCPKG_INSTALLED_PATH})
  # Use environment variable set by setup.py (preferred)
  target_include_directories(synthizer PRIVATE 
    "$ENV{VCPKG_INSTALLED_PATH}/include"
  )
  message(STATUS "Using vcpkg includes from VCPKG_INSTALLED_PATH: $ENV{VCPKG_INSTALLED_PATH}/include")
else()
  # Fallback to platform-specific hardcoded paths
  if(WIN32)
    target_include_directories(synthizer PRIVATE 
      "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/include"
    )
    message(STATUS "Using Windows vcpkg fallback path")
  elseif(UNIX AND NOT APPLE)
    target_include_directories(synthizer PRIVATE 
      "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-linux/include"
    )
    message(STATUS "Using Linux vcpkg fallback path")
  elseif(APPLE)
    target_include_directories(synthizer PRIVATE 
      "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-osx/include"
    )
    message(STATUS "Using macOS vcpkg fallback path")
  endif()
endif()

target_compile_definitions(synthizer PRIVATE BUILDING_SYNTHIZER WDL_RESAMPLE_TYPE=float)
if("${SYNTHIZER_LIB_TYPE}" STREQUAL "SHARED")
  target_compile_definitions(synthizer PRIVATE SYNTHIZER_SHARED)
endif()
if(${FILESYSTEM_OK})
  target_compile_definitions(synthizer PRIVATE SYZ_USE_FILESYSTEM)
endif()

if(MSVC)
  target_compile_options(synthizer PRIVATE /W4 /WX)
else()
  target_compile_options(synthizer PRIVATE -Wall -Wextra -Werror)
endif()

set_property(TARGET synthizer PROPERTY POSITION_INDEPENDENT_CODE ON)
if (WIN32)
  target_compile_definitions(synthizer PRIVATE NOMINMAX)
endif()

if(DEFINED CI_SYNTHIZER_NAME)
  set_target_properties(synthizer PROPERTIES OUTPUT_NAME ${CI_SYNTHIZER_NAME})
endif()

add_custom_target(data
  COMMAND python "${CMAKE_SOURCE_DIR}/data_processor/main.py"
)

find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
  message(STATUS "Math library found: ${MATH_LIBRARY}")
else()
  message(STATUS "Math library not found")
endif()

function(example NAME EXT)
  if (NOT "${SYZ_INTEGRATING}")
    add_executable(${NAME} ./examples/${NAME}.${EXT})
    target_link_libraries(${NAME} synthizer)
    if(MATH_LIBRARY)
      target_link_libraries(${NAME} ${MATH_LIBRARY})
    endif()
  endif()
endfunction()

example(automation_circle cpp)
example(basic_stream_handle c)
example(buffer_from_memory c)
example(buffer_from_raw_data c)
example(custom_stream c)
example(events cpp)
example(fast_sine_bank cpp)
example(load_libsndfile c)
example(play_note c)
example(print_version c)
example(scalar_panned_source cpp)
example(simple_automation c)

if(("${SYNTHIZER_LIB_TYPE}" STREQUAL "STATIC") AND (NOT "${SYZ_INTEGRATING}"))
  add_executable(file_test file_test.cpp)
  target_link_libraries(file_test synthizer)

  add_executable(test_filter_repl test/interactive/filter_repl.cpp)
  target_link_libraries(test_filter_repl synthizer)

  add_executable(decoding_bench benchmarks/decoding.cpp)
  target_link_libraries(decoding_bench synthizer)

  add_executable(test_noise test/interactive/noise.cpp)
  target_link_libraries(test_noise synthizer)

  add_executable(test_seeking test/interactive/seeking.cpp)
  target_link_libraries(test_seeking synthizer)

  add_executable(test_fast_sine_accuracy test/interactive/fast_sine_accuracy.cpp)
  target_link_libraries(test_fast_sine_accuracy synthizer)

  FetchContent_MakeAvailable(benchmark catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  INCLUDE(Catch)

  set(BENCHMARK_ENABLE_TESTING OFF)

  add_executable(gbench
    benchmarks/gbench/block_buffer_cache.cpp
    benchmarks/gbench/hrtf.cpp
    benchmarks/gbench/main.cpp
    benchmarks/gbench/property_write.cpp
    benchmarks/gbench/standard_setup.cpp
  )
  target_link_libraries(gbench synthizer benchmark::benchmark)
  set_property(TARGET gbench PROPERTY CXX_STANDARD 17)

  FetchContent_MakeAvailable(benchmark catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  INCLUDE(Catch)

  add_executable(tests
    test/block_delay_line.cpp
    test/buffer_generator.cpp
    test/delay_line.cpp
    test/double_refcount.cpp
    test/effect_connection.cpp
    test/generation_thread.cpp
    test/latch.cpp
    test/main.cpp
    test/math.cpp
    test/mod_pointer.cpp
    test/property_automation_timeline.cpp
    test/random_float.cpp
    test/sse2_horizontal_sum.cpp
    test/verify_properties.cpp
  )
  target_link_libraries(tests PRIVATE Catch2::Catch2 synthizer)
  target_include_directories(tests PRIVATE $<TARGET_PROPERTY:wdl_objlib,INTERFACE_INCLUDE_DIRECTORIES>)
  target_compile_definitions(tests PRIVATE WDL_RESAMPLE_TYPE=float)
  if (WIN32)
    target_compile_definitions(tests PRIVATE NOMINMAX)
  endif()

  catch_discover_tests(tests)
endif()


install(
  TARGETS synthizer
  LIBRARY DESTINATION  "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION  "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION  "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
