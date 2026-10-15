# GACL triplet: x64, dynamic CRT, static libs, built with the VS 2022 (v143) toolset.
#
# vcpkg selects the NEWEST MSVC toolset installed on the machine unless
# VCPKG_PLATFORM_TOOLSET is pinned.  On a box with both VS 2022 and VS 2026 that
# silently builds the ports with v145 while gacl.sln links with v143, producing
# unresolved externals such as __std_rotate (new in the 14.5x STL).
# Pinning the toolset here keeps the ports and the solution on the same STL.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v143)
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED GACL_NUGET_SOURCE)
