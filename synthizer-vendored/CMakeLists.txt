cmake_minimum_required(VERSION 3.15.0)

# -- VCPKG TOOLCHAIN: DEVE essere la prima cosa significativa, PRIMA di project() --
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
    message(STATUS "Usando vcpkg toolchain locale: ${CMAKE_TOOLCHAIN_FILE}")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
    message(STATUS "Usando vcpkg toolchain locale (livello superiore): ${CMAKE_TOOLCHAIN_FILE}")
  elseif(DEFINED ENV{CMAKE_TOOLCHAIN_FILE})
     set(CMAKE_TOOLCHAIN_FILE "$ENV{CMAKE_TOOLCHAIN_FILE}" CACHE STRING "Vcpkg toolchain file from ENV")
     message(STATUS "Usando CMAKE_TOOLCHAIN_FILE da variabile d'ambiente: ${CMAKE_TOOLCHAIN_FILE}")
  elseif(DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_TOOLCHAIN_CANDIDATE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    if(EXISTS "${VCPKG_TOOLCHAIN_CANDIDATE}")
      set(CMAKE_TOOLCHAIN_FILE "${VCPKG_TOOLCHAIN_CANDIDATE}" CACHE STRING "Vcpkg toolchain file from VCPKG_ROOT ENV")
      message(STATUS "Usando CMAKE_TOOLCHAIN_FILE da VCPKG_ROOT ENV: ${CMAKE_TOOLCHAIN_FILE}")
    else()
      message(WARNING "VCPKG_ROOT è impostato, ma ${VCPKG_TOOLCHAIN_CANDIDATE} non trovato.")
    endif()
  else()
    message(WARNING "CMAKE_TOOLCHAIN_FILE non definito esternamente o trovato in percorsi relativi. CMake potrebbe non trovare i pacchetti vcpkg.")
  endif()
endif()

project(synthizer VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "")
option(SYZ_INTEGRATING "Set to ON to avoid linking examples etc. when integrating into a language's bindings" OFF)

# Assumendo che deps/CMakeLists.txt chiami FetchContent_Declare per le dipendenze.
# Se FetchContent_Declare è qui, va bene lo stesso.
add_subdirectory(deps) 
add_compile_definitions(SYZ_MAJOR=0 SYZ_MINOR=1 SYZ_PATCH=9)

include(CTest)
include(CheckCXXSourceRuns)
enable_testing()

# --- Dipendenze di Sistema e VCPKG ---
find_package(Threads REQUIRED)
find_package(Vorbis REQUIRED)
find_package(Ogg REQUIRED)
find_package(Opus REQUIRED)
find_package(OpusFile REQUIRED)
find_package(FFMPEG REQUIRED) 
if(NOT FFMPEG_FOUND)
  message(FATAL_ERROR "FFMPEG non trovato! Verifica l'installazione vcpkg e CMAKE_TOOLCHAIN_FILE.")
endif()

# --- Dipendenze tramite FetchContent ---
# Nomi usati in FetchContent_Declare (o primo arg di FetchContent_MakeAvailable)
set(SYZ_DEPS_NAMES 
  boost_partial
  concurrentqueue
  cpp11-on-multicore
  dr_libs
  hedley
  miniaudio
  pdqsort
  # wdl è gestito separatamente sotto
)
if(NOT SYZ_INTEGRATING)
  set(SYZ_TEST_DEPS_NAMES catch2 benchmark) 
endif()

FetchContent_MakeAvailable(${SYZ_DEPS_NAMES})
if(NOT SYZ_INTEGRATING)
  FetchContent_MakeAvailable(${SYZ_TEST_DEPS_NAMES})
endif()
FetchContent_MakeAvailable(wdl) # Crea il target 'wdl_objlib'

# --- Configurazioni Generali e Specifiche del Compilatore ---
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Xclang -fno-caret-diagnostics -Wno-deprecated-declarations -Wno-logical-op-parentheses -Wno-unknown-pragmas -Wno-unknown-warning-option)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  add_compile_options(/wd4068 /wd5030 /wd4244 /wd4267 /Zc:preprocessor /wd5105 /wd4127 /experimental:external /external:W0 /external:anglebrackets /wd4702)
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

# --- Libreria di Oggetti per Single File Libs ---
add_library(synthizer_single_file_libs OBJECT
  src/single_file_libs.c
)
set_property(TARGET synthizer_single_file_libs PROPERTY POSITION_INDEPENDENT_CODE ON)
# Linka i target INTERFACE trovati per dr_libs e miniaudio
target_link_libraries(synthizer_single_file_libs INTERFACE
  dr_libs   # Target INTERFACE da FetchContent
  miniaudio # Target INTERFACE da FetchContent
)

# --- Libreria Principale Synthizer ---
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
  $<TARGET_OBJECTS:synthizer_single_file_libs>
  $<TARGET_OBJECTS:wdl_objlib> # Nome target OBJECT per wdl
)

if(MSVC)
  set_source_files_properties(src/decoding.cpp PROPERTIES COMPILE_FLAGS "/W4 /wd4245 /wd4456 /wd4457 /wd4701 /wd4245 /WX-")
endif()
target_compile_features(synthizer PUBLIC cxx_std_17)


# --- Linkaggio delle Dipendenze per la libreria synthizer ---
target_link_libraries(synthizer PRIVATE
  # Target da VCPKG
  Threads::Threads
  Vorbis::vorbis
  Vorbis::vorbisfile
  Ogg::ogg
  Opus::opus
  OpusFile::opusfile
  ${FFMPEG_LIBRARIES}   # Da FindFFMPEG.cmake

  ${CMAKE_DL_LIBS}      

  # Target INTERFACE da FetchContent (propagano i loro include e opzioni di compilazione)
  boost_partial      # Target INTERFACE
  concurrentqueue    # Target INTERFACE
  cpp11-on-multicore # Target INTERFACE
  hedley             # Target INTERFACE
  pdqsort            # Target INTERFACE
  # dr_libs e miniaudio sono già gestiti tramite synthizer_single_file_libs.
  # wdl_objlib è già incluso come TARGET_OBJECTS.
)

# --- Include Directories per la libreria synthizer ---
target_include_directories(synthizer 
  PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # Header pubblici
    $<INSTALL_INTERFACE:include> # Percorso installazione header pubblici
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS} # Da FindFFMPEG.cmake
    # Le librerie INTERFACE linkate sopra (boost_partial, concurrentqueue, etc.)
    # dovrebbero propagare automaticamente le loro directory di include a synthizer.
    # Lo stesso vale per dr_libs e miniaudio tramite synthizer_single_file_libs.
    # Se wdl_objlib ha header che devono essere usati da synthizer e non sono propagati:
    # $<TARGET_PROPERTY:wdl_objlib,INTERFACE_INCLUDE_DIRECTORIES> # (se wdl_objlib imposta questa proprietà)
)

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

# --- Target Personalizzati, Esempi, Test ---
add_custom_target(data
  COMMAND ${CMAKE_COMMAND} -E echo "Data processing command placeholder (python ${CMAKE_CURRENT_SOURCE_DIR}/data_processor/main.py)"
)

find_library(MATH_LIBRARY m)

function(example NAME EXT)
  if (NOT SYZ_INTEGRATING) 
    add_executable(${NAME} ./examples/${NAME}.${EXT})
    target_link_libraries(${NAME} PRIVATE synthizer) 
    if(MATH_LIBRARY AND NOT WIN32) 
      target_link_libraries(${NAME} PRIVATE ${MATH_LIBRARY})
    endif()
  endif()
endfunction()

example(automation_circle cpp)
example(basic_stream_handle c)
# ... (altri tuoi esempi)

if(SYNTHIZER_LIB_TYPE STREQUAL "STATIC" AND NOT SYZ_INTEGRATING)
  add_executable(file_test file_test.cpp)
  target_link_libraries(file_test PRIVATE synthizer)
  # ... (altri eseguibili di test) ...

  # Per gbench (Google Benchmark)
  # Assumendo che FetchContent_MakeAvailable(benchmark) da SYZ_TEST_DEPS_NAMES
  # si riferisca a Google Benchmark e crei un target come 'benchmark::benchmark' o 'benchmark'
  if(TARGET benchmark::benchmark) 
    add_executable(gbench
      benchmarks/gbench/block_buffer_cache.cpp
      benchmarks/gbench/hrtf.cpp
      benchmarks/gbench/main.cpp
      benchmarks/gbench/property_write.cpp
      benchmarks/gbench/standard_setup.cpp
    )
    target_link_libraries(gbench PRIVATE synthizer benchmark::benchmark)
    set_property(TARGET gbench PROPERTY CXX_STANDARD 17)
  # Se invece "benchmark" in SYZ_TEST_DEPS_NAMES si riferisce a Google Test, allora
  # dovresti linkare GTest::gtest e GTest::gtest_main se questo eseguibile usa Google Test.
  # else if(TARGET GTest::gtest)
  #   # ... logica per gbench con Google Test ...
  #   message(STATUS "gbench linkerà GTest::gtest (se necessario)")
  # endif()
  else()
    message(WARNING "Target per Google Benchmark (es. benchmark::benchmark) non trovato. L'eseguibile gbench potrebbe non essere compilato correttamente.")
  endif()

  # Per tests con Catch2
  # Hai trovato che FetchContent crea un target chiamato 'Catch2'
  if(TARGET Catch2) 
    add_executable(tests
      test/block_delay_line.cpp
      test/buffer_generator.cpp
      test/delay_line.cpp
      test/double_refcount.cpp
      test/effect_connection.cpp
      test/generation_thread.cpp
      test/latch.cpp
      test/main.cpp # Assicurati che uno dei file definisca main o usi CATCH_CONFIG_MAIN
      test/math.cpp
      test/mod_pointer.cpp
      test/property_automation_timeline.cpp
      test/random_float.cpp
      test/sse2_horizontal_sum.cpp
      test/verify_properties.cpp
    )
    target_link_libraries(tests PRIVATE synthizer Catch2) # Linka il target 'Catch2'
    # Se wdl è usato dai test e 'wdl_objlib' è il target object library corretto:
    # target_include_directories(tests PRIVATE $<TARGET_PROPERTY:wdl_objlib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(tests PRIVATE WDL_RESAMPLE_TYPE=float)
    if (WIN32)
      target_compile_definitions(tests PRIVATE NOMINMAX)
    endif()
    
    # Gestione CTest per Catch2
    include(Catch) 
    if(Catch_FOUND)
        catch_discover_tests(tests)
    else()
        message(WARNING "Modulo Catch.cmake non trovato. catch_discover_tests(tests) non sarà eseguito.")
    endif()
  else()
    message(WARNING "Target Catch2 da FetchContent non trovato. L'eseguibile 'tests' non sarà compilato correttamente.")
  endif()
endif()

# --- Installazione ---
install(
  TARGETS synthizer
  EXPORT synthizerTargets 
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT lib
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT lib
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT runtime
)
install(
  DIRECTORY include/synthizer/ 
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/synthizer" 
  COMPONENT dev
)
install(
  EXPORT synthizerTargets
  FILE SynthizerTargets.cmake
  NAMESPACE synthizer:: 
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/synthizer"
  COMPONENT dev
)
# Considera di aggiungere anche un SynthizerConfigVersion.cmake