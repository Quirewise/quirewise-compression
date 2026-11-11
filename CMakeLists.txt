cmake_minimum_required(VERSION 3.21)
project(QuirewiseCompression VERSION 0.3.0 LANGUAGES C)
option(QUIREWISE_BUILD_SHARED "Build Quirewise libraries as shared libraries" OFF)
if(QUIREWISE_BUILD_SHARED)
    set(QW_LIBRARY_TYPE SHARED)
else()
    set(QW_LIBRARY_TYPE STATIC)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

add_library(quirewise_compression     src/checksum.c
    src/deflate.c
    src/seven_zip.c
    src/xz.c
    src/zlib.c
    src/internal/lzma.c
    src/internal/xz_dec.c)
add_library(Quirewise::Compression ALIAS quirewise_compression)
set_target_properties(quirewise_compression PROPERTIES
    EXPORT_NAME Compression
    POSITION_INDEPENDENT_CODE ON)
target_compile_features(quirewise_compression PUBLIC c_std_11)
target_include_directories(quirewise_compression PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

if(BUILD_TESTING)
    add_executable(test_compression tests/test_compression.c)
    target_compile_features(test_compression PRIVATE c_std_11)
    target_link_libraries(test_compression PRIVATE Quirewise::Compression)
    add_test(NAME compression_vectors COMMAND test_compression)

    add_executable(test_seven_zip tests/test_seven_zip.c)
    target_compile_features(test_seven_zip PRIVATE c_std_11)
    target_link_libraries(test_seven_zip PRIVATE Quirewise::Compression)
    add_test(NAME compression_7z_vectors COMMAND test_seven_zip)

    add_executable(test_xz tests/test_xz.c)
    target_compile_features(test_xz PRIVATE c_std_11)
    target_link_libraries(test_xz PRIVATE Quirewise::Compression)
    add_test(NAME compression_xz_vectors COMMAND test_xz)
endif()

install(TARGETS quirewise_compression EXPORT QuirewiseCompressionTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT QuirewiseCompressionTargets
    FILE QuirewiseCompressionTargets.cmake
    NAMESPACE Quirewise::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseCompression")

configure_package_config_file(
    cmake/QuirewiseCompressionConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseCompressionConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseCompression")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseCompressionConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseCompressionConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseCompressionConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseCompression")

if(QUIREWISE_BUILD_SHARED AND WIN32)
    set_target_properties(
        quirewise_compression
        PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(quirewise_compression_fuzz_smoke tests/compression_fuzz_smoke.c)
    target_compile_features(quirewise_compression_fuzz_smoke PRIVATE c_std_11)
    target_link_libraries(quirewise_compression_fuzz_smoke PRIVATE Quirewise::Compression)
    add_test(NAME quirewise_compression_fuzz_smoke
        COMMAND quirewise_compression_fuzz_smoke)
endif()
