# GACL overlay port
#
# This in-repository overlay builds the current checkout. The official vcpkg
# registry port will acquire a tagged GitHub release instead.
#
# CLER FEATURE:
#   Enabled by default. Acquires Microsoft.ML.OnnxRuntime 1.24.2 from
#   nuget.org (SHA512-pinned).
#   Opt out: manifest "default-features": false  (classic: microsoft-gacl[core])

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/gacl-nuget.cmake")

get_filename_component(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

# ---------------------------------------------------------------------------
# CLER feature: acquire OnnxRuntime 1.24.2 (.nupkg = zip)
# ---------------------------------------------------------------------------
if("cler" IN_LIST FEATURES)
    set(_ORT_VERSION     "1.24.2")
    set(_ORT_PKG_LOWER   "microsoft.ml.onnxruntime")
    set(_ORT_FILENAME    "${_ORT_PKG_LOWER}.${_ORT_VERSION}.nupkg")
    set(_ORT_SHA512      "76b67c8dafc23c4f20ad09637057c2021f4a873701826cec9e274a04a2017ad11433879275a091f9a1d5997a9aea7fbb7589b49d036028b193b6168f99d9f460")
    gacl_get_nuget_package_url(
        _ORT_PACKAGE_URL
        "Microsoft.ML.OnnxRuntime"
        "${_ORT_VERSION}"
    )

    vcpkg_download_distfile(ORT_ARCHIVE
        URLS     "${_ORT_PACKAGE_URL}"
        FILENAME "${_ORT_FILENAME}"
        SHA512   ${_ORT_SHA512}
    )

    vcpkg_extract_source_archive(
        ORT_PACKAGE_PATH
        ARCHIVE          ${ORT_ARCHIVE}
        NO_REMOVE_ONE_LEVEL
    )

    if(VCPKG_TARGET_ARCHITECTURE MATCHES "arm64|arm64ec")
        set(_ORT_ARCH "arm64")
    else()
        set(_ORT_ARCH "${VCPKG_TARGET_ARCHITECTURE}")
    endif()

    # OnnxRuntime is a shared library; gacl_lib is static — allow DLLs alongside it
    set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)

    set(_cler_options
        -DGACL_ENABLE_CLER=ON
        -DONNXRUNTIME_ROOT=${ORT_PACKAGE_PATH}
    )
else()
    set(_cler_options -DGACL_ENABLE_CLER=OFF)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DGACL_INSTALL=ON
        -DVCPKG_MANIFEST_MODE=OFF
        ${_cler_options}
    OPTIONS_RELEASE
        -DCMAKE_BUILD_TYPE=Release
    OPTIONS_DEBUG
        -DCMAKE_BUILD_TYPE=Debug
)

vcpkg_cmake_install()

# Relocate the gacl command-line tool from bin/ to tools/${PORT}, so the installed package
# remains a library.  Built by the GACL_BUILD_TOOLS option in the root CMakeLists.
vcpkg_copy_tools(TOOL_NAMES gacl AUTO_CLEAN)

vcpkg_cmake_config_fixup(
    PACKAGE_NAME GACL
    CONFIG_PATH  lib/cmake/GACL
)

# Remove duplicate include from debug tree
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

# ---------------------------------------------------------------------------
# Install OnnxRuntime DLLs (shared runtime — required at load time)
# ---------------------------------------------------------------------------
if("cler" IN_LIST FEATURES)
    set(_ort_native "${ORT_PACKAGE_PATH}/runtimes/win-${_ORT_ARCH}/native")
    set(_ort_dlls
        "${_ort_native}/onnxruntime.dll"
        "${_ort_native}/onnxruntime_providers_shared.dll"
    )
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
    file(INSTALL ${_ort_dlls} DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
    file(INSTALL ${_ort_dlls} DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")

    # Install OnnxRuntime headers and import lib so that GACLConfig.cmake's
    # find_dependency(OnnxRuntime) succeeds in consumer cmake projects.
    # (gacl_lib is a static lib — its private OnnxRuntime dep must be resolvable
    #  at consumer link time, and cmake propagates it via find_dependency.)
    set(_ort_native_inc "${ORT_PACKAGE_PATH}/build/native/include")
    set(_ort_native_lib "${_ort_native}/onnxruntime.lib")
    file(INSTALL "${_ort_native_inc}/"
         DESTINATION "${CURRENT_PACKAGES_DIR}/include/onnxruntime")
    file(INSTALL "${_ort_native_lib}"
         DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    file(INSTALL "${_ort_native_lib}"
         DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

    # Generate OnnxRuntimeConfig.cmake pointing to the installed tree.
    # cmake/vcpkg will find this when GACLConfig.cmake calls find_dependency(OnnxRuntime).
    # Path: share/OnnxRuntime/ → ../../ → triplet root
    set(_ort_cmake_dir "${CURRENT_PACKAGES_DIR}/share/OnnxRuntime")
    file(MAKE_DIRECTORY "${_ort_cmake_dir}")
    file(WRITE "${_ort_cmake_dir}/OnnxRuntimeConfig.cmake" [=[
# OnnxRuntimeConfig.cmake — installed by the microsoft-gacl vcpkg overlay port.
# Provides OnnxRuntime::OnnxRuntime for GACLConfig.cmake's find_dependency call.
get_filename_component(_ort_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT TARGET OnnxRuntime::OnnxRuntime)
    add_library(OnnxRuntime::OnnxRuntime SHARED IMPORTED)
    set_target_properties(OnnxRuntime::OnnxRuntime PROPERTIES
        IMPORTED_IMPLIB             "${_ort_prefix}/lib/onnxruntime.lib"
        IMPORTED_LOCATION           "${_ort_prefix}/bin/onnxruntime.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${_ort_prefix}/include/onnxruntime"
    )
endif()
]=])
endif()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${SOURCE_PATH}/THIRD_PARTY_NOTICES.txt"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL
    "${SOURCE_PATH}/Tools/scripts/setupCLER.ps1"
    "${SOURCE_PATH}/Tools/scripts/onnxExporter.py"
    DESTINATION "${CURRENT_PACKAGES_DIR}/tools/${PORT}"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
