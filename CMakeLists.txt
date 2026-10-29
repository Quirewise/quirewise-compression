cmake_minimum_required(VERSION 3.21)
project(QuirewiseCompression VERSION 0.3.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

add_library(quirewise_compression STATIC
    src/internal/lzma_sdk/7zCrcOpt.c
    src/internal/lzma_sdk/7zCrc.c
    src/internal/lzma_sdk/Alloc.c
    src/internal/lzma_sdk/Bra.c
    src/internal/lzma_sdk/Bra86.c
    src/internal/lzma_sdk/BraIA64.c
    src/internal/lzma_sdk/CpuArch.c
    src/internal/lzma_sdk/Delta.c
    src/internal/lzma_sdk/Lzma2Dec.c
    src/internal/lzma_sdk/LzmaDec.c
    src/internal/lzma_sdk/Sha256.c
    src/internal/lzma_sdk/Sha256Opt.c
    src/internal/lzma_sdk/Xz.c
    src/internal/lzma_sdk/XzCrc64.c
    src/internal/lzma_sdk/XzCrc64Opt.c
    src/internal/lzma_sdk/XzDec.c
    src/internal/lzma_sdk_allocator.c
    src/checksum.c
    src/deflate.c
    src/seven_zip.c
    src/xz.c
    src/zlib.c)
add_library(Quirewise::Compression ALIAS quirewise_compression)
set_target_properties(quirewise_compression PROPERTIES
    EXPORT_NAME Compression
    POSITION_INDEPENDENT_CODE ON)
target_compile_features(quirewise_compression PUBLIC c_std_11)
target_include_directories(quirewise_compression PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_include_directories(quirewise_compression PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/internal/lzma_sdk")
target_compile_definitions(quirewise_compression PRIVATE Z7_ST=1)

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
