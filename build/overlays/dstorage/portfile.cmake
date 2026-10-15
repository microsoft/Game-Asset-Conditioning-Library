# This port overlay is set to a preview instead of the last release build.

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/gacl-nuget.cmake")

# Set VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY instead of using `vcpkg_check_linkage` because
# these DLLs don't link with a CRT.
set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)

# We use an overlay here to get the preview release instead of the last production release.
set(PREVIEW "-preview1-2603.504")

set(_SHA512 2ab4f0f2f6ebe41f8102b60d02c6db655b62e51dd0b1bf5d4a7d1cbfc0069b06541beb721adf0d98a0384c41456e7cd284716baf358c84589258770bf674fb0e)
set(_FILENAME "directstorage.${VERSION}${PREVIEW}.zip")
gacl_get_nuget_package_url(
    _PACKAGE_URL
    "Microsoft.Direct3D.DirectStorage"
    "${VERSION}${PREVIEW}"
)

vcpkg_download_distfile(ARCHIVE
    URLS "${_PACKAGE_URL}"
    FILENAME "${_FILENAME}"
    SHA512 ${_SHA512}
)

vcpkg_extract_source_archive(
    PACKAGE_PATH
    ARCHIVE ${ARCHIVE}
    NO_REMOVE_ONE_LEVEL
)

if(VCPKG_TARGET_ARCHITECTURE MATCHES "arm64|arm64ec")
    set(DS_ARCH arm64)
else()
    set(DS_ARCH ${VCPKG_TARGET_ARCHITECTURE})
endif()

file(INSTALL "${PACKAGE_PATH}/native/include/dstorage.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${PACKAGE_PATH}/native/include/dstorageerr.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")

file(INSTALL "${PACKAGE_PATH}/native/lib/${DS_ARCH}/dstorage.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

file(COPY "${PACKAGE_PATH}/native/bin/${DS_ARCH}/dstorage.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
file(COPY "${PACKAGE_PATH}/native/bin/${DS_ARCH}/dstoragecore.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/debug")
file(COPY "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${PACKAGE_PATH}/LICENSE.txt")

configure_file("${CMAKE_CURRENT_LIST_DIR}/dstorage-config.cmake.in" "${CURRENT_PACKAGES_DIR}/share/${PORT}/${PORT}-config.cmake" COPYONLY)
