# FindOnnxRuntime.cmake
# Locates the OnnxRuntime NuGet package.
#
# Usage:
#   find_package(OnnxRuntime REQUIRED)
#   target_link_libraries(my_target PRIVATE OnnxRuntime::OnnxRuntime)
#
# Hints:
#   ONNXRUNTIME_ROOT   - root of the NuGet package directory
#                        (defaults to <repo_root>/packages/Microsoft.ML.OnnxRuntime.<ver>)

cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

set(_ort_version "1.24.2")
set(_ort_pkg_name "Microsoft.ML.OnnxRuntime.${_ort_version}")

if(NOT ONNXRUNTIME_ROOT)
    set(_candidate "${CMAKE_SOURCE_DIR}/packages/${_ort_pkg_name}")
    if(EXISTS "${_candidate}")
        set(ONNXRUNTIME_ROOT "${_candidate}")
    endif()
endif()

find_path(OnnxRuntime_INCLUDE_DIR
    NAMES onnxruntime_c_api.h
    PATHS "${ONNXRUNTIME_ROOT}/build/native/include"
    NO_DEFAULT_PATH
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_ort_arch "x64")
else()
    set(_ort_arch "x86")
endif()

find_library(OnnxRuntime_LIBRARY
    NAMES onnxruntime
    PATHS "${ONNXRUNTIME_ROOT}/runtimes/win-${_ort_arch}/native"
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OnnxRuntime
    REQUIRED_VARS OnnxRuntime_INCLUDE_DIR OnnxRuntime_LIBRARY
    VERSION_VAR _ort_version
)

if(OnnxRuntime_FOUND AND NOT TARGET OnnxRuntime::OnnxRuntime)
    add_library(OnnxRuntime::OnnxRuntime SHARED IMPORTED)
    set_target_properties(OnnxRuntime::OnnxRuntime PROPERTIES
        IMPORTED_IMPLIB    "${OnnxRuntime_LIBRARY}"
        IMPORTED_LOCATION  "${ONNXRUNTIME_ROOT}/runtimes/win-${_ort_arch}/native/onnxruntime.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${OnnxRuntime_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(OnnxRuntime_INCLUDE_DIR OnnxRuntime_LIBRARY)
