cmake_minimum_required(VERSION 3.20)
project(ecomm LANGUAGES CXX)

# ---------------------------------------------------------------------------------------
# C++ standard and header-only interface library
# ---------------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ecomm INTERFACE)

# During build: include repo root so consumers write #include <ecomm/...>
# During install: place headers in include/ecomm
target_include_directories(ecomm
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/ecomm>
)

# ---------------------------------------------------------------------------------------
# Dependencies via FetchContent
# ---------------------------------------------------------------------------------------
include(FetchContent)

# Ensure etools doesn't build its own tests when vendored here
set(ETOOLS_BUILD_TESTS OFF CACHE BOOL "Disable etools unit tests" FORCE)

FetchContent_Declare(
  etools
  GIT_REPOSITORY https://github.com/MarikTik/etools.git
  GIT_TAG main
)
FetchContent_MakeAvailable(etools)

# Link ecomm -> etools (etools links eser internally)
target_link_libraries(ecomm INTERFACE etools)

# ---------------------------------------------------------------------------------------
# Examples (optional)
# ---------------------------------------------------------------------------------------
option(ECOMM_BUILD_EXAMPLES "Build example executables for ecomm" ON)
if(ECOMM_BUILD_EXAMPLES)
  file(GLOB_RECURSE ecomm_EXAMPLE_SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/example/*.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/example/**/*.cpp"
  )
  if(ecomm_EXAMPLE_SOURCES)
    add_executable(ecomm_examples ${ecomm_EXAMPLE_SOURCES})
    target_link_libraries(ecomm_examples PRIVATE ecomm)
  endif()
endif()

# ---------------------------------------------------------------------------------------
# Testing: GoogleTest for ecomm's own tests
# ---------------------------------------------------------------------------------------
include(CTest) # defines BUILD_TESTING
if(BUILD_TESTING)
  enable_testing()

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
  )
  FetchContent_MakeAvailable(googletest)

  add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------------------
# Installation
# We vendor the full dependency chain in the same export:
#   ecomm  -> etools -> eser
# This avoids "target not in any export set" errors on install(EXPORT ...).
# ---------------------------------------------------------------------------------------

# Install ecomm public headers
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ecomm/
  DESTINATION include/ecomm
  FILES_MATCHING PATTERN "*.hpp"
)

# Collect targets to export; require that etools/eser exist (they should, via FetchContent)
set(ecomm_EXPORT_TARGETS ecomm)

if(NOT TARGET etools)
  message(FATAL_ERROR "Expected target 'etools' to exist (fetched via FetchContent).")
endif()
list(APPEND ecomm_EXPORT_TARGETS etools)

if(NOT TARGET eser)
  message(FATAL_ERROR "Expected target 'eser' to exist (provided by etools).")
endif()
list(APPEND ecomm_EXPORT_TARGETS eser)

# Install targets (INTERFACE libs install as importable targets)
install(
  TARGETS ${ecomm_EXPORT_TARGETS}
  EXPORT ecommTargets
)

# Generate and install an export file containing all three targets.
# Consumers can then do: find_package(ecomm CONFIG) and link ecomm::ecomm
install(
  EXPORT ecommTargets
  NAMESPACE ecomm::
  DESTINATION lib/cmake/ecomm
)
