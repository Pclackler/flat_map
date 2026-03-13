cmake_minimum_required(VERSION 3.15)
project(milo_hashmap VERSION 1.0.0 LANGUAGES CXX)

# -----------------------------------------------------------------------------
# C++ standard – the code uses C++20 (std::bit_ceil, std::has_single_bit)
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # -std=c++20, not -std=gnu++20

# -----------------------------------------------------------------------------
# Header‑only library target "milo"
# -----------------------------------------------------------------------------
add_library(milo INTERFACE)

# Users can #include <milo/flat_map.h> and <milo/char_32.h>
target_include_directories(milo INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Optionally propagate the required C++ standard
target_compile_features(milo INTERFACE cxx_std_20)

# -----------------------------------------------------------------------------
# Benchmark executable
# -----------------------------------------------------------------------------
add_executable(benchmark benchmark.cpp)

# Link against the milo interface library
target_link_libraries(benchmark PRIVATE milo)

# Place the executable in a predictable output directory
set_target_properties(benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# -----------------------------------------------------------------------------
# Install rules (optional, but makes the library usable system‑wide)
# -----------------------------------------------------------------------------
include(GNUInstallDirs)

# Install the headers under include/milo/
install(FILES
    milo/flat_map.h
    milo/char_32.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/milo
)

# Install the library target (so others can find it via find_package)
install(TARGETS milo EXPORT miloTargets)
install(EXPORT miloTargets
    FILE miloTargets.cmake
    NAMESPACE milo::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/milo
)

# -----------------------------------------------------------------------------
# Package version file (for find_package)
# -----------------------------------------------------------------------------
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/miloConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/miloConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/milo
)
