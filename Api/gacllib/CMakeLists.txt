cmake_minimum_required(VERSION 3.21)

set(GACL_LIB_SOURCES
    # Helpers
    ../../Helpers/FileUtility.cpp
    ../../Helpers/FormatHelper.cpp
    ../../Helpers/Utility.cpp
    # RDO Block Entropy
    ../../Lib/RDO_B/blockentropy.cpp
    # Texture Shuffle
    ../../Lib/shuffle/bc1.cpp
    ../../Lib/shuffle/bc3.cpp
    ../../Lib/shuffle/bc4.cpp
    ../../Lib/shuffle/bc5.cpp
    ../../Lib/shuffle/bc7.cpp
    ../../Lib/shuffle/bc7modejoin.cpp
    ../../Lib/shuffle/bc7modesplit.cpp
    ../../Lib/shuffle/shuffle.cpp
)

if(GACL_ENABLE_CLER)
    list(APPEND GACL_LIB_SOURCES
        # RDO Machine Learning
        ../../Lib/RDO_ML/KMeansRDO.cpp
        ../../Lib/RDO_ML/LossMetrics.cpp
        ../../Lib/RDO_ML/ML_RDO.cpp
    )
endif()

add_library(gacl_lib STATIC ${GACL_LIB_SOURCES})

add_library(GACL::gacl_lib ALIAS gacl_lib)

target_compile_features(gacl_lib PUBLIC cxx_std_17)

target_include_directories(gacl_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Api/includes>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Lib>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Helpers
)

target_compile_definitions(gacl_lib
    PUBLIC
        GACL_INCLUDE_BLER=1
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:UNICODE _UNICODE>
)

if(GACL_ENABLE_CLER)
    target_compile_definitions(gacl_lib PUBLIC GACL_INCLUDE_CLER=1)
endif()

# MSVC-specific compiler flags (mirrors Directory.Build.Props)
target_compile_options(gacl_lib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4
        /Zc:__cplusplus
        /ZH:SHA_256
        /utf-8
        /sdl
        $<$<CONFIG:Release>:/guard:cast /d2CastGuardFailureMode:fastfail>
    >
)

target_compile_options(gacl_lib PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/Gw /Gy /GL>
)

# CFG (Control Flow Guard) - Release only
target_link_options(gacl_lib PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/guard:cf>
)

# Find and link dependencies
find_package(zstd CONFIG REQUIRED)
find_package(DirectXTex CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)

target_link_libraries(gacl_lib
    PUBLIC
        Microsoft::DirectXTex
        Microsoft::DirectX-Headers
        Microsoft::DirectXMath
    PRIVATE
        $<IF:$<TARGET_EXISTS:zstd::libzstd_static>,zstd::libzstd_static,zstd::libzstd_shared>
)

# OnnxRuntime (NuGet-based, required when CLER is enabled)
if(GACL_ENABLE_CLER)
    find_package(OnnxRuntime REQUIRED)
    target_link_libraries(gacl_lib PRIVATE OnnxRuntime::OnnxRuntime)
    target_compile_definitions(gacl_lib PRIVATE GACL_HAS_ONNXRUNTIME=1)
endif()

# Install rules
include(GNUInstallDirs)
install(TARGETS gacl_lib
    EXPORT GACLTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/Api/includes/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gacl
)
