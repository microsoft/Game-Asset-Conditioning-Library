# Prefer an original, SHA512-pinned package already staged in vcpkg's download cache.
# In particular, a cache hit must not require access to a NuGet service index.
function(gacl_download_nuget_package out_var package_id package_version filename sha512)
    set(_cached "${DOWNLOADS}/${filename}")
    if(EXISTS "${_cached}")
        file(SHA512 "${_cached}" _actual_sha512)
        if(NOT _actual_sha512 STREQUAL "${sha512}")
            message(FATAL_ERROR "Cached NuGet package failed SHA512 verification: ${filename}")
        endif()
        message(STATUS "Using cached NuGet package: ${filename}")
        set(${out_var} "${_cached}" PARENT_SCOPE)
        return()
    endif()

    gacl_get_nuget_package_url(_url "${package_id}" "${package_version}")
    vcpkg_download_distfile(_archive
        URLS "${_url}"
        FILENAME "${filename}"
        SHA512 "${sha512}"
    )
    set(${out_var} "${_archive}" PARENT_SCOPE)
endfunction()

function(gacl_get_nuget_package_url out_var package_id package_version)
    set(_source "$ENV{GACL_NUGET_SOURCE}")
    if(_source STREQUAL "")
        set(${out_var}
            "https://www.nuget.org/api/v2/package/${package_id}/${package_version}"
            PARENT_SCOPE)
        return()
    endif()

    set(_index_path "${CURRENT_BUILDTREES_DIR}/gacl-nuget-v3-index.json")
    file(DOWNLOAD "${_source}" "${_index_path}"
        STATUS _download_status
        TLS_VERIFY ON
    )
    list(GET _download_status 0 _download_error)
    if(NOT _download_error EQUAL 0)
        file(REMOVE "${_index_path}")
        message(FATAL_ERROR
            "Failed to read the NuGet v3 service index from GACL_NUGET_SOURCE.\n"
            "Status: ${_download_status}")
    endif()

    file(READ "${_index_path}" _service_index)
    string(JSON _resource_count ERROR_VARIABLE _json_error
        LENGTH "${_service_index}" resources)
    if(_json_error)
        message(FATAL_ERROR
            "GACL_NUGET_SOURCE did not return a valid NuGet v3 service index: "
            "${_json_error}")
    endif()

    set(_package_base "")
    if(_resource_count GREATER 0)
        math(EXPR _last_resource "${_resource_count} - 1")
        foreach(_resource_index RANGE 0 ${_last_resource})
            string(JSON _resource_type GET
                "${_service_index}" resources ${_resource_index} "@type")
            if(_resource_type MATCHES "^PackageBaseAddress/3\\.0\\.0")
                string(JSON _package_base GET
                    "${_service_index}" resources ${_resource_index} "@id")
                break()
            endif()
        endforeach()
    endif()

    if(_package_base STREQUAL "")
        message(FATAL_ERROR
            "The NuGet v3 service index in GACL_NUGET_SOURCE does not expose "
            "PackageBaseAddress/3.0.0.")
    endif()

    if(NOT _package_base MATCHES "/$")
        string(APPEND _package_base "/")
    endif()
    string(TOLOWER "${package_id}" _package_id_lower)
    string(TOLOWER "${package_version}" _package_version_lower)
    set(${out_var}
        "${_package_base}${_package_id_lower}/${_package_version_lower}/${_package_id_lower}.${_package_version_lower}.nupkg"
        PARENT_SCOPE)
endfunction()
