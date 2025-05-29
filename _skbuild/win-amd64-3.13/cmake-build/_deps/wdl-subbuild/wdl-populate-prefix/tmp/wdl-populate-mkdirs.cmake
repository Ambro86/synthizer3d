# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/fork/synthizer3d/synthizer-vendored/deps/deps/third_party/wdl")
  file(MAKE_DIRECTORY "C:/fork/synthizer3d/synthizer-vendored/deps/deps/third_party/wdl")
endif()
file(MAKE_DIRECTORY
  "C:/fork/synthizer3d/_skbuild/win-amd64-3.13/cmake-build/_deps/wdl-build"
  "C:/fork/synthizer3d/_skbuild/win-amd64-3.13/cmake-build/_deps/wdl-subbuild/wdl-populate-prefix"
  "C:/fork/synthizer3d/_skbuild/win-amd64-3.13/cmake-build/_deps/wdl-subbuild/wdl-populate-prefix/tmp"
  "C:/fork/synthizer3d/_skbuild/win-amd64-3.13/cmake-build/_deps/wdl-subbuild/wdl-populate-prefix/src/wdl-populate-stamp"
  "C:/fork/synthizer3d/_skbuild/win-amd64-3.13/cmake-build/_deps/wdl-subbuild/wdl-populate-prefix/src"
  "C:/fork/synthizer3d/_skbuild/win-amd64-3.13/cmake-build/_deps/wdl-subbuild/wdl-populate-prefix/src/wdl-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/fork/synthizer3d/_skbuild/win-amd64-3.13/cmake-build/_deps/wdl-subbuild/wdl-populate-prefix/src/wdl-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/fork/synthizer3d/_skbuild/win-amd64-3.13/cmake-build/_deps/wdl-subbuild/wdl-populate-prefix/src/wdl-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
