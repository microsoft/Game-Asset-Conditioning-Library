cmake_minimum_required(VERSION 3.21)

add_executable(gacl
    main.cpp
    Processing.cpp
    ../../Helpers/FileUtility.cpp
    ../../Helpers/FormatHelper.cpp
)

add_executable(GACL::gacl ALIAS gacl)

target_compile_features(gacl PRIVATE cxx_std_17)

target_include_directories(gacl PRIVATE
    ${CMAKE_SOURCE_DIR}/Api/includes
    ${CMAKE_SOURCE_DIR}/Helpers
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# /WX intentionally absent: warnings-as-errors belongs in CI, not in a consumer-facing build.
# Same policy already applied to gacl_lib.
target_compile_options(gacl PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4
        /Zc:__cplusplus
        /ZH:SHA_256
        /utf-8
        /sdl
        $<$<CONFIG:Release>:/GL /guard:cast /d2CastGuardFailureMode:fastfail>
    >
)

target_link_libraries(gacl PRIVATE GACL::gacl_lib)

# CET compatibility (x64 only)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_link_options(gacl PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/CETCOMPAT>
    )
endif()

# Install
include(GNUInstallDirs)
# Deliberately NOT part of EXPORT GACLTargets, for a mechanical reason rather than a stylistic
# one.  The port runs vcpkg_copy_tools(... AUTO_CLEAN), which copies this binary to tools/${PORT}
# and then DELETES the copy in bin/ that the install() below produces.  An exported target records
# IMPORTED_LOCATION at install time, so it would point at that deleted file, and the generated
# GACLTargets.cmake turns a missing imported file into a FATAL_ERROR - breaking find_package(GACL)
# for every vcpkg consumer.
#
# Consumers who want to invoke the tool from their own build are not left without it: GACLConfig
# locates it after the relocation and defines GACL::gacl.  See cmake/GACLConfig.cmake.in.
install(TARGETS gacl
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
