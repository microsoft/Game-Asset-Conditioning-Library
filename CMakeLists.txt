cmake_minimum_required(VERSION 3.21)

set(GACL_VERSION_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Api/includes/gacl.h")
if(NOT EXISTS "${GACL_VERSION_HEADER}")
    message(FATAL_ERROR "GACL version header not found: ${GACL_VERSION_HEADER}")
endif()

file(STRINGS "${GACL_VERSION_HEADER}" GACL_VERSION_MAJOR_LINE REGEX "^#define[ \t]+GACL_VERSION_MAJOR[ \t]+[0-9]+")
file(STRINGS "${GACL_VERSION_HEADER}" GACL_VERSION_MINOR_LINE REGEX "^#define[ \t]+GACL_VERSION_MINOR[ \t]+[0-9]+")
file(STRINGS "${GACL_VERSION_HEADER}" GACL_VERSION_PATCH_LINE REGEX "^#define[ \t]+GACL_VERSION_PATCH[ \t]+[0-9]+")
file(STRINGS "${GACL_VERSION_HEADER}" GACL_VERSION_LABEL_LINE REGEX "^#define[ \t]+GACL_VERSION_LABEL[ \t]+[A-Za-z0-9_.-]+")

if(NOT GACL_VERSION_MAJOR_LINE OR NOT GACL_VERSION_MINOR_LINE OR NOT GACL_VERSION_PATCH_LINE OR NOT GACL_VERSION_LABEL_LINE)
    message(FATAL_ERROR "Failed to parse version macros from ${GACL_VERSION_HEADER}")
endif()

string(REGEX REPLACE "^#define[ \t]+GACL_VERSION_MAJOR[ \t]+([0-9]+).*" "\\1" GACL_VERSION_MAJOR "${GACL_VERSION_MAJOR_LINE}")
string(REGEX REPLACE "^#define[ \t]+GACL_VERSION_MINOR[ \t]+([0-9]+).*" "\\1" GACL_VERSION_MINOR "${GACL_VERSION_MINOR_LINE}")
string(REGEX REPLACE "^#define[ \t]+GACL_VERSION_PATCH[ \t]+([0-9]+).*" "\\1" GACL_VERSION_PATCH "${GACL_VERSION_PATCH_LINE}")
string(REGEX REPLACE "^#define[ \t]+GACL_VERSION_LABEL[ \t]+([A-Za-z0-9_.-]+).*" "\\1" GACL_VERSION_LABEL "${GACL_VERSION_LABEL_LINE}")

set(GACL_VERSION "${GACL_VERSION_MAJOR}.${GACL_VERSION_MINOR}.${GACL_VERSION_PATCH}")
set(GACL_VERSION_SEMVER "${GACL_VERSION}-${GACL_VERSION_LABEL}")

project(GACL
    VERSION ${GACL_VERSION}
    DESCRIPTION "Game Asset Conditioning Library"
    LANGUAGES CXX
)

set(GACL_OVERLAY_VCPKG_JSON "${CMAKE_CURRENT_SOURCE_DIR}/build/overlays/microsoft-gacl/vcpkg.json")
if(EXISTS "${GACL_OVERLAY_VCPKG_JSON}")
    file(READ "${GACL_OVERLAY_VCPKG_JSON}" GACL_OVERLAY_VCPKG_JSON_TEXT)
    string(REGEX MATCH "\"version-semver\"[ \t]*:[ \t]*\"([^\"]+)\"" GACL_OVERLAY_VERSION_MATCH "${GACL_OVERLAY_VCPKG_JSON_TEXT}")
    if(NOT GACL_OVERLAY_VERSION_MATCH)
        message(FATAL_ERROR "Failed to read version-semver from ${GACL_OVERLAY_VCPKG_JSON}")
    endif()

    set(GACL_OVERLAY_VERSION_SEMVER "${CMAKE_MATCH_1}")
    if(NOT GACL_OVERLAY_VERSION_SEMVER STREQUAL GACL_VERSION_SEMVER)
        message(FATAL_ERROR
            "Version mismatch detected:\n"
            "  gacl.h version: ${GACL_VERSION_SEMVER}\n"
            "  microsoft-gacl vcpkg.json version-semver: ${GACL_OVERLAY_VERSION_SEMVER}\n"
            "Keep gacl.h as single source of truth and align overlay manifest."
        )
    endif()
endif()

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add cmake/ to module path for FindOnnxRuntime.cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# vcpkg toolchain is expected to be set by the caller via
# -DCMAKE_TOOLCHAIN_FILE=<vcpkg root>/scripts/buildsystems/vcpkg.cmake
# When using the VS-bundled vcpkg the toolchain is set automatically.

option(GACL_INSTALL       "Generate install rules"              ON)
option(GACL_ENABLE_CLER   "Enable Component-Level Entropy Reduction (requires OnnxRuntime)" OFF)
option(GACL_BUILD_TOOLS   "Build the gacl command-line tool"    ON)

# ---- Dependencies (resolved via vcpkg) ----
find_package(zstd          CONFIG REQUIRED)
find_package(DirectXTex    CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(directxmath   CONFIG REQUIRED)

# ---- Targets ----
add_subdirectory(Api/gacllib)

if(GACL_BUILD_TOOLS)
    add_subdirectory(Api/gaclexe)
endif()

# ---- Install / Export ----
if(GACL_INSTALL)
    include(CMakePackageConfigHelpers)
    include(GNUInstallDirs)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/GACLConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GACLConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/GACLConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GACL"
    )

    install(EXPORT GACLTargets
        FILE       GACLTargets.cmake
        NAMESPACE  GACL::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GACL"
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/GACLConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/GACLConfigVersion.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindOnnxRuntime.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GACL"
    )
endif()
